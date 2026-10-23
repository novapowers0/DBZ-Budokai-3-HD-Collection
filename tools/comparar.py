#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""comparar.py (copia de R7/emu/comparar.py + metricas de X0b) - original (captura de PCSX2) contra port (captura de pruebas.py), para que un revisor
(humano o Haiku) liste diferencias mirando una sola imagen.

  python comparar.py original.png port.png salida_prefijo [--umbral 40]

Alinea el original sobre el port con puntos ORB + RANSAC (transformacion afin: sirve para 4:3 estirado
contra 16:9 y para otra posicion de camara en el tablero 2D), y escribe:
  <prefijo>_lado.png    original alineado | port | mapa de diferencias (rojo = distinto), con recuadros
  <prefijo>.txt         escala/desplazamiento medidos, % de zona comun distinta y lista de recuadros
                        (coordenadas del port) ordenados por tamano, para revisar uno a uno.
Si no hay suficientes puntos comunes (pantallas muy distintas) solo pone las dos imagenes juntas.
  python comparar.py --metricas original.png port.png    -> JSON con ssim, phash_hamming, zona comun (sin humano)
"""
import sys

import numpy as np
from PIL import Image, ImageDraw
from skimage.feature import ORB, match_descriptors
from skimage.filters import gaussian
from skimage.measure import label, ransac, regionprops
from skimage.transform import AffineTransform, warp


def _gris(im, ancho):
    k = ancho / im.width
    g = np.asarray(im.convert("L").resize((ancho, round(im.height * k))), dtype=float) / 255
    return g, k


def alinear(org, port, ancho=800):
    """-> AffineTransform de coordenadas del port (px reales) a coordenadas del original (px reales), inliers."""
    go, ko = _gris(org, ancho)
    gp, kp = _gris(port, ancho)
    orb = ORB(n_keypoints=2000, fast_threshold=0.03)
    orb.detect_and_extract(go)
    ko_, do_ = orb.keypoints, orb.descriptors
    orb.detect_and_extract(gp)
    kp_, dp_ = orb.keypoints, orb.descriptors
    m = match_descriptors(dp_, do_, cross_check=True, max_ratio=0.95)
    if len(m) < 12:
        return None, 0
    src = kp_[m[:, 0]][:, ::-1] / kp     # (x, y) en px reales del port
    dst = ko_[m[:, 1]][:, ::-1] / ko     # (x, y) en px reales del original
    # Modelo sin giro: x' = sx*x + tx, y' = sy*y + ty (pantallas de juego: otra relacion de aspecto/camara 2D).
    # ponytail: RANSAC propio de 2 puntos; si hubiera giro o perspectiva, cambiar a AffineTransform/ProjectiveTransform
    rng = np.random.default_rng(0)
    tol = 4 / ko
    mejor = None
    for _ in range(4000):
        i, j = rng.choice(len(src), 2, replace=False)
        dx, dy = src[i] - src[j]
        if abs(dx) < 20 or abs(dy) < 20:
            continue
        s = (dst[i] - dst[j]) / (dx, dy)
        if not (0.2 < s[0] < 5 and 0.2 < s[1] < 5):
            continue
        tr = dst[i] - s * src[i]
        inl = np.hypot(*(dst - (src * s + tr)).T) < tol
        if mejor is None or inl.sum() > mejor.sum():
            mejor = inl
    if mejor is None or mejor.sum() < 10:
        return None, 0
    p = []
    for k in (0, 1):   # minimos cuadrados con los inliers
        a = np.vstack([src[mejor, k], np.ones(mejor.sum())]).T
        p.append(np.linalg.lstsq(a, dst[mejor, k], rcond=None)[0])
    t = AffineTransform(matrix=np.array([[p[0][0], 0, p[0][1]], [0, p[1][0], p[1][1]], [0, 0, 1]]))
    return t, int(mejor.sum())


def comparar(org_p, port_p, pref, umbral=40, min_area=300):
    org = Image.open(org_p).convert("RGB")
    port = Image.open(port_p).convert("RGB")
    t, n = alinear(org, port)
    W, H = port.size
    rep = ["original: %s" % org_p, "port: %s" % port_p]
    if t is None:
        lado = Image.new("RGB", (org.width + W, max(org.height, H)))
        lado.paste(org, (0, 0))
        lado.paste(port, (org.width, 0))
        lado.save(pref + "_lado.png")
        rep.append("SIN ALINEAR: pocas coincidencias (pantallas distintas). Revisar a ojo %s_lado.png" % pref)
        open(pref + ".txt", "w", encoding="utf-8").write("\n".join(rep) + "\n")
        return rep
    a = np.asarray(org, dtype=float) / 255
    ali = warp(a, t, output_shape=(H, W), order=1, cval=-1)          # original visto en el encuadre del port
    comun = ali[..., 0] >= 0
    b = np.asarray(port, dtype=float) / 255
    # difuminar: ignora filtrado/aliasing (PS2 512x448 contra HD) y deja cambios de contenido o color
    a2 = np.where(comun[..., None], ali, b)
    d = np.abs(gaussian(a2, 2.5, channel_axis=2) - gaussian(b, 2.5, channel_axis=2)).max(axis=2) * 255
    tono = (a2[comun].mean(0) - b[comun].mean(0)) * 255
    malo = (d > umbral) & comun
    regs = sorted((r for r in regionprops(label(malo)) if r.area >= min_area), key=lambda r: -r.area)
    m = t.params
    rep += ["puntos comunes (RANSAC): %d" % n,
            "port->original: escala x %.3f, y %.3f; desplazamiento (%.1f, %.1f) px" % (
                np.hypot(m[0, 0], m[1, 0]), np.hypot(m[0, 1], m[1, 1]), m[0, 2], m[1, 2]),
            "zona comun: %.1f %% del port; distinta (> %d/255): %.2f %%" % (
                100 * comun.mean(), umbral, 100 * malo.sum() / max(1, comun.sum())),
            "tono medio original-port (R,G,B): %+.0f %+.0f %+.0f" % tuple(tono),
            "recuadros (x0,y0,x1,y1 en px del port, area px):"]
    vis = Image.fromarray((np.clip(np.where(comun[..., None], ali, 0), 0, 1) * 255).astype(np.uint8))
    mapa = (b * 0.35 * 255).astype(np.uint8)
    mapa[malo] = [255, 40, 40]
    mapa = Image.fromarray(mapa)
    dr = [ImageDraw.Draw(x) for x in (vis, port, mapa)]
    for i, r in enumerate(regs[:25]):
        y0, x0, y1, x1 = r.bbox
        rep.append("  %2d  (%d,%d,%d,%d)  %d" % (i + 1, x0, y0, x1, y1, r.area))
        for g in dr:
            g.rectangle([x0, y0, x1, y1], outline=(255, 255, 0), width=2)
            g.text((x0 + 3, y0 + 2), str(i + 1), fill=(255, 255, 0))
    lado = Image.new("RGB", (W * 3, H))
    for k, im in enumerate((vis, port, mapa)):
        lado.paste(im, (k * W, 0))
    lado.save(pref + "_lado.png")
    open(pref + ".txt", "w", encoding="utf-8").write("\n".join(rep) + "\n")
    return rep


def phash(im, lado=32, bits=8):
    """pHash de 64 bits (DCT 32x32, 8x8 bajos, mediana): robusto a escala y filtrado; Hamming bajo = misma imagen."""
    from scipy.fft import dctn
    g = np.asarray(im.convert("L").resize((lado, lado), Image.LANCZOS), dtype=float)
    d = dctn(g, norm="ortho")[:bits, :bits].ravel()
    return int("".join("1" if v > np.median(d[1:]) else "0" for v in d), 2)


def hamming(a, b):
    return bin(a ^ b).count("1")


def metricas(org_p, port_p, ancho=640):
    """Numeros para el revisor automatico (X0b): SSIM del original alineado contra el port, pHash (Hamming 0-64) de las
    dos imagenes tal cual, y % de zona comun distinta. Sin humano. Si no alinea: ssim se calcula a imagen completa."""
    from skimage.metrics import structural_similarity
    org = Image.open(org_p).convert("RGB")
    port = Image.open(port_p).convert("RGB")
    k = ancho / port.width
    port_s = port.resize((ancho, round(port.height * k)))
    try:
        t, n = alinear(org, port)
    except RuntimeError:   # ORB sin rasgos (captura casi lisa): se mide sin alinear
        t, n = None, 0
    if t is not None:
        ali = warp(np.asarray(org, dtype=float) / 255, t, output_shape=(port.height, port.width), order=1, cval=-1)
        comun = ali[..., 0] >= 0
        a = Image.fromarray((np.clip(np.where(comun[..., None], ali, 0), 0, 1) * 255).astype(np.uint8)).resize(port_s.size)
        modo = "alineado"
    else:
        a, comun, modo = org.resize(port_s.size), None, "sin alinear"
    ga = np.asarray(a.convert("L"), dtype=float) / 255
    gb = np.asarray(port_s.convert("L"), dtype=float) / 255
    ssim = float(structural_similarity(ga, gb, data_range=1.0))
    return {"ssim": round(ssim, 4), "phash_hamming": hamming(phash(org), phash(port)), "alineado": modo,
            "puntos_comunes": n, "zona_comun_pct": None if comun is None else round(100 * float(comun.mean()), 1)}

def _prueba():
    """Autocomprobacion: una imagen contra si misma desplazada y escalada -> casi 0 % distinto."""
    rng = np.random.default_rng(1)
    base = Image.fromarray((rng.random((300, 400, 3)) * 255).astype(np.uint8)).resize((800, 600), Image.NEAREST)
    port = base.resize((1000, 750)).crop((50, 40, 950, 690))
    t, n = alinear(base, port)
    assert t is not None and n >= 20, n
    assert abs(t.params[0, 0] - 0.8) < 0.02, t.params


if __name__ == "__main__":
    if "--metricas" in sys.argv:           # python comparar.py --metricas original.png port.png
        import json
        a = [x for x in sys.argv[1:] if x != "--metricas"]
        print(json.dumps(metricas(a[0], a[1])))
        sys.exit(0)
    if len(sys.argv) < 4:
        _prueba()
        print("autoprueba OK")
        print(__doc__)
    else:
        u = int(sys.argv[sys.argv.index("--umbral") + 1]) if "--umbral" in sys.argv else 40
        print("\n".join(comparar(sys.argv[1], sys.argv[2], sys.argv[3], u)))
