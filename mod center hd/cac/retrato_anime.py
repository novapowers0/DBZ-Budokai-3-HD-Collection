#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""retrato_anime.py - retrato de DIALOGO del Heroe (416x408 RGBA, como data_cmn 1416) hecho de su modelo 3D con aspecto
de dibujo: render toon del juego (model_render) + colores planos (posterizado) + tinta gruesa de silueta y contornos.

  python retrato_anime.py --modelo mods/heroe_propio/modelos/base.bin --salida retrato.png [--cmp nativo.png]
  python retrato_anime.py --medir  (compara con 4 retratos nativos: colores, linea, SSIM de siluetas; necesita el AFS)
Cache por huella del modelo: heroe_sagas_a_du la llama al montar (si falla, queda la plantilla).
"""
import argparse
import hashlib
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import model_render as mr  # noqa: E402

W, H = 416, 408
# Camara medida a ojo sobre data_cmn 1416-1499 (3/4 hacia la derecha, algo de picado, busto): ver --medir
YAW, PITCH = 22.0, 4.0
CABEZA_PX = 135      # alto de la cabeza (craneo+cara) en el retrato nativo de Goku, medido
COLORES = 22         # tonos planos (el nativo: ~10-16 por zona)


LUZ = np.array([-0.45, 0.55, 0.70])
LUZ /= np.linalg.norm(LUZ)


def _suave(P, N):
    """Normales promediadas por posicion (el pelo y la cara de PS2 traen normales duras: facetas)."""
    key = np.round(P * 800).astype(np.int64)
    _, inv = np.unique(key, axis=0, return_inverse=True)
    S = np.zeros((inv.max() + 1, 3))
    np.add.at(S, inv.ravel(), N)
    S /= np.linalg.norm(S, axis=1, keepdims=True) + 1e-12
    return S[inv.ravel()]


def _normal_con_ndl(v):
    """Vector unitario cuyo n.L = v (para pedir a _shade una fila fija de la rampa toon)."""
    perp = np.cross(LUZ, [0.0, 1.0, 0.0])
    perp /= np.linalg.norm(perp)
    return v * LUZ + np.sqrt(max(0.0, 1 - v * v)) * perp


def render_celda(model, view, center, scale, size, pose, tono=0.22, sombra=-0.45, cabeza=None):
    """Celda de 2 tonos: color iluminado y color en sombra de la rampa toon del juego (base*(1-rampa)), elegido por un umbral
    NITIDO de n.L con normales suavizadas; ojos y boca salen de la textura nativa (alfa 15 = sin sombrear). Devuelve
    (rgb float, cobertura bool, profundidad, normal suavizada por pixel, id de material)."""
    W, H = size
    P, N = model.posed(pose)
    C = (P - np.asarray(center)) @ np.asarray(view).T
    Nb = _suave(P, N)
    if cabeza is not None:   # normales de proxy de esfera en cara y pelo (receta del informe de referencias): la luz corta limpio
        hc, hr = cabeza
        d = P - hc
        dist = np.linalg.norm(d, axis=1)
        proxy = d / (dist[:, None] + 1e-9)
        w = np.clip((1.6 * hr - dist) / (0.6 * hr), 0, 1) * 0.8
        Nb = Nb * (1 - w[:, None]) + proxy * w[:, None]
        Nb /= np.linalg.norm(Nb, axis=1, keepdims=True) + 1e-12
    Ns = Nb @ np.asarray(view).T
    sx, sy, sz = C[:, 0] * scale + W / 2, H / 2 - C[:, 1] * scale, C[:, 2]
    tris, mats = model.tris, model.tri_mat
    blend = np.array([m_["blend"] for m_ in model.mats], bool)[mats] if len(mats) else np.zeros(0, bool)
    tris, mats = tris[~blend], mats[~blend]
    zbuf, tid, b1, b2 = mr._raster(tris, sx, sy, sz, W, H)
    cov = tid >= 0
    rgb = np.zeros((H, W, 3), np.float32)
    nrm = np.zeros((H, W, 3), np.float32)
    mid = np.full((H, W), -1, int)
    if cov.any():
        T, w1, w2 = tid[cov], b1[cov], b2[cov]
        n = Ns[tris[T, 0]] * w1[:, None] + Ns[tris[T, 1]] * w2[:, None] + Ns[tris[T, 2]] * (1 - w1 - w2)[:, None]
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
        luz = np.tile(_normal_con_ndl(1.0), (len(P), 1))
        som = np.tile(_normal_con_ndl(sombra), (len(P), 1))
        c_luz = mr._shade(model, tris, mats, T, w1, w2, luz, tuple(LUZ))
        c_som = mr._shade(model, tris, mats, T, w1, w2, som, tuple(LUZ))
        ndl = n @ LUZ
        m = np.zeros((H, W), np.float32)
        m[cov] = (ndl < tono).astype(np.float32)
        m = np.array(Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))) / 255.0 > 0.5
        enSombra = m[cov]
        rgb[cov] = np.where(enSombra[:, None], c_som, c_luz)
        nrm[cov] = n
        mid[cov] = mats[T]
    return rgb, cov, zbuf, nrm, mid


def kuwahara(rgb, cov, r):
    """Kuwahara de 4 cuadrantes (aplana la textura y conserva bordes) dentro de lo cubierto."""
    from scipy import ndimage as ndi  # noqa: PLC0415
    f = rgb.astype(np.float32)
    n = r + 1
    mean = [ndi.uniform_filter(f[..., c], n) for c in range(3)]
    sq = [ndi.uniform_filter(f[..., c] ** 2, n) for c in range(3)]
    var = sum(sq[c] - mean[c] ** 2 for c in range(3))
    sh = r // 2 + 1
    cand_m, cand_v = [], []
    for dy, dx in ((-sh, -sh), (-sh, sh), (sh, -sh), (sh, sh)):
        cand_m.append(np.stack([np.roll(mean[c], (dy, dx), (0, 1)) for c in range(3)], 2))
        cand_v.append(np.roll(var, (dy, dx), (0, 1)))
    best = np.argmin(np.stack(cand_v), 0)
    out = np.take_along_axis(np.stack(cand_m), best[None, ..., None].repeat(3, 3), 0)[0]
    return np.where(cov[..., None], out, f)


def contorno(cov, zbuf, nrm, mid, grosor):
    """Tinta de grosor uniforme: silueta + saltos de profundidad + saltos de normal + cambio de material (ojos, pelo/piel)."""
    H, W = cov.shape
    z = np.where(cov, zbuf, -1e9)
    span = max(np.ptp(zbuf[cov]), 1e-6)
    e = np.zeros((H, W), bool)
    for dy, dx in ((0, 1), (1, 0)):
        zn = np.roll(z, (dy, dx), (0, 1))
        cn = np.roll(cov, (dy, dx), (0, 1))
        e |= cov & cn & (np.abs(z - zn) > 0.03 * span)
        nn = np.roll(nrm, (dy, dx), (0, 1))
        e |= cov & cn & ((nrm * nn).sum(2) < 0.55)
        mn = np.roll(mid, (dy, dx), (0, 1))
        e |= cov & cn & (mid != mn) & (mn >= 0)
    sil = cov & ~np.array(Image.fromarray((cov * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(3))).astype(bool)
    linea = e | sil
    img = Image.fromarray((linea * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(2 * grosor + 1))
    return np.array(img) > 0


def render_base(model, yaw=YAW, pitch=PITCH, boca=0.0, k=3):
    pose = model.arms_down()
    pose.update(model.mouth_closed(0.75 - boca))
    lo, hi, _ = mr.head_info(model, pose, core=True)
    alto = max(hi[1] - lo[1], 1e-6)
    scale = CABEZA_PX / alto * k
    view = mr.rot_x(pitch) @ mr.rot_y(yaw)
    c = np.array([(lo[0] + hi[0]) / 2, lo[1] + alto * 0.75, (lo[2] + hi[2]) / 2])
    ctr = c - view.T @ np.array([(0.58 - 0.5) * W * k / scale, 0.0, 0.0])
    hc = (lo + hi) / 2
    hr = float(np.linalg.norm(hi - lo)) * 0.5
    rgb, cov, zb, nrm, mid = render_celda(model, view, ctr, scale, (W * k, H * k), pose, cabeza=(hc, hr))
    # ponytail: Kuwahara probado (22/10): deja manchas en la piel; queda definido pero sin usar
    tinta = contorno(cov, zb, nrm, mid, grosor=k)   # ~2 px finales
    out = np.zeros((H * k, W * k, 4), np.uint8)
    out[..., :3] = np.clip(rgb * 255 + 0.5, 0, 255).astype(np.uint8)
    out[..., 3] = (cov * 255).astype(np.uint8)
    sil = np.array(Image.fromarray(out[..., 3]).filter(ImageFilter.MaxFilter(2 * k + 1))) > 0
    out[tinta & cov, :3] = (10, 8, 12)
    out[sil & ~cov, :3] = (10, 8, 12)
    out[sil & ~cov, 3] = 255
    return out


def reducir(rgba):
    k = rgba.shape[1] // W
    pm = rgba.astype(np.float32)
    a = pm[..., 3:4] / 255
    pm[..., :3] *= a
    small = np.array(Image.fromarray(np.concatenate([pm[..., :3], pm[..., 3:4]], 2).astype(np.uint8)).resize((W, H), Image.BOX)).astype(np.float32)
    al = small[..., 3:4] / 255
    rgb = np.where(al > 1e-3, small[..., :3] / np.maximum(al, 1e-3), 0)
    return np.clip(np.concatenate([rgb, small[..., 3:4]], 2) + 0.5, 0, 255).astype(np.uint8)


def retrato(modelo_bin, boca=0.0):
    """-> RGBA (408, 416): render 3x con normales suavizadas, celda de 2 tonos y tinta uniforme."""
    m = mr.Model(modelo_bin)
    return reducir(render_base(m, boca=boca))


def huella(modelo_bin):
    return hashlib.sha1(open(modelo_bin, "rb").read() + open(__file__, "rb").read()).hexdigest()[:12]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo")
    ap.add_argument("--salida")
    ap.add_argument("--cmp", help="png nativo para ver al lado")
    a = ap.parse_args()
    im = Image.fromarray(retrato(a.modelo))
    im.save(a.salida)
    if a.cmp:
        n = Image.open(a.cmp).convert("RGBA")
        hoja = Image.new("RGBA", (W * 2, H), (255, 255, 255, 255))
        hoja.alpha_composite(n, (0, 0))
        hoja.alpha_composite(im, (W, 0))
        hoja.save(os.path.splitext(a.salida)[0] + "_cmp.png")


if __name__ == "__main__":
    main()
