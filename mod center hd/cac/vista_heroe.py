#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""vista_heroe.py - Vista previa del heroe para las pantallas Create/Edit del Modo Heroe (studies 8, H3).

El juego no tiene forma de dibujar un modelo 3D dentro de un menu propio (ImGui), asi que la vista previa es
una TIRA de giros renderizada aqui con `model_render` (el mismo renderizador de los retratos del Kit) a partir
del modelo REAL del cuerpo, leido del data_cmn.afs del jugador (nada de arte en el repositorio). La pantalla
del juego pasa de un giro al siguiente (giro automatico o la palanca derecha: "R: Girar").

Que modelo es cada (cuerpo, traje): medido en la imagen del guest (xex US), tabla char96 0x8234ABB8 + 96*ID:
  +4 u8 numero de trajes, +5 u8 modelos por traje, +8 u32 puntero a la lista de fids (u32 cada uno),
  +16 formas (8 B: u16 ID, u8 modelo dentro del traje, ...). fid = lista[traje * modelos + modelo(forma 0)].
  El fid es la entrada de data_cmn.afs (Gohan adulto: 225/228 = catalog_b3 "Adulto (violeta)/(naranja)").
La tabla de abajo (KFID) es esa lectura para los 17 cuerpos del Modo Heroe (heroe.cpp kRazas).

Salida: <juego>/ingame_ui/heroe_vista/<id>_<traje>.png (N giros en horizontal, cada uno `--size` px).
El juego la carga con S::Img("heroe_vista/<id>_<traje>"). Si falta, la pantalla lanza esta herramienta en
segundo plano (`--faltan`) y dibuja el retrato mientras tanto.

Uso:
  python vista_heroe.py --juego D:\\DBZ3HD\\heroe_s8 [--faltan] [--size 384] [--giros 12] [--solo 0_0]
"""
import argparse
import json
import os
import sys
import tempfile
import time
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
for p in (HERE, os.path.join(HERE, "..")):
    if os.path.isdir(p) and p not in sys.path:
        sys.path.append(p)

import numpy as np                 # noqa: E402
from PIL import Image              # noqa: E402

import swap_b3                     # noqa: E402  (L03: localiza us/ y el awo_tools del proyecto)
_us = swap_b3.find_us()
if _us:
    _awo = os.path.join(os.path.dirname(_us), "awo_tools")
    if os.path.isdir(_awo) and _awo not in sys.path:
        sys.path.insert(0, _awo)

import model_render as mr          # noqa: E402
import roster_build as rb          # noqa: E402

# (id del cuerpo) -> fids por traje (forma 0). Medido en out/analysis/guest_image/dbz3_us_image.bin (char96).
KFID = {
    0: [264, 270, 276, 281, 282],     # Goku: 5 trajes x 5 modelos
    7: [416, 420, 424, 427],          # Vegeta: 4 x 3
    39: [99, 100],                    # Bardock
    8: [390, 392, 394],               # Trunks: 3 x 2
    10: [327, 328, 329],              # Krilin
    12: [400, 401],                   # Ten Shin Han
    13: [445, 446, 447],              # Yamcha
    15: [410, 411],                   # Videl
    11: [350, 351, 352, 353],         # Piccolo
    27: [181, 187, 193],              # Freezer: 3 x 6
    38: [166, 169],                   # Cooler: 2 x 3
    34: [128, 129],                   # Majin Buu
    36: [141, 142],                   # Buu nino
    30: [80, 81, 82],                 # C-18
    29: [75, 76],                     # C-17
    28: [70, 71],                     # C-16
    33: [146, 149],                   # Cell: 2 x 3
    383: [383],                       # H3: donantes de pelo (fid como clave): Trunks nino
    235: [235],                       # Gohan saga Cell
    252: [252],                       # Goku nino
    298: [298],                       # Goten
    447: [447],                       # Yamcha pelo largo (25/10)
    328: [328],                       # Krilin con pelo (traje alternativo, 25/10)
}


def pose_de(model):
    """Brazos abajo y boca cerrada (la pose de los retratos del Kit), no la T del bind."""
    try:
        pose = model.arms_down()
        pose.update(model.mouth_closed())
        return pose
    except Exception:   # noqa: BLE001 (modelo sin esos huesos: bind)
        return None


def tira(model, size, giros, pitch=6.0):
    """N giros (0..360) del cuerpo entero, con el mismo encuadre en todos (no "respira" al girar)."""
    pose = pose_de(model)
    P, _ = model.posed(pose)
    usados = np.unique(model.tris.reshape(-1)) if len(model.tris) else np.arange(len(P))
    lo, hi = P[usados].min(0), P[usados].max(0)
    ctr = (lo + hi) / 2
    alto = max(hi[1] - lo[1], 1e-6)
    ancho = max(np.linalg.norm([hi[0] - lo[0], hi[2] - lo[2]]), 1e-6)   # diagonal: cabe en cualquier giro
    esc = min(size * 0.92 / alto, size * 0.96 / ancho)
    out = Image.new("RGBA", (size * giros, size), (0, 0, 0, 0))
    for k in range(giros):
        yaw = 360.0 * k / giros
        view = mr.rot_x(pitch) @ mr.rot_y(yaw)
        img = mr.render(model, (size, size), view, ctr, esc, pose=pose, outline=1.4, ss=2)
        out.alpha_composite(Image.fromarray(img), (size * k, 0))
    return out


def _textura_cocida(model, mat):
    """RGBA uint8 de un material con la luz llena (la rampa del juego en su columna mas clara, "resta" como
    model_render._shade): el toon del juego (Escena::Poner, 3 tonos) la oscurece en marcha. Sin textura o con reflejo
    (sphere map): un color liso."""
    if mat["tex"] >= len(model.tex):
        c = np.clip(np.asarray(mat["diffuse"], np.float32), 0, 1)
        return np.tile(np.concatenate([c, [1.0]])[None, None, :], (4, 4, 1))
    base = model.tex[mat["tex"]]
    if mat["env"]:
        c = base[..., :3].reshape(-1, 3).mean(0)
        return np.tile(np.concatenate([c, [1.0]])[None, None, :], (4, 4, 1))
    rgb = base[..., :3].copy()
    if mat["shader"] != 0xFFFFFFFF and mat["shader"] < len(model.tex):
        ramp = model.tex[mat["shader"]]
        rh, rw = ramp.shape[:2]
        band = np.clip(np.round(base[..., 3] * 15), 0, 15)
        yi = np.clip(((band + 0.5) * rh / 16).astype(int), 0, rh - 1)
        r3 = ramp[yi, rw - 1, :3]
        shaded = np.clip(rgb - mr.RAMP_K * r3, 0, 1) if mr.RAMP_MODE == "resta" else rgb * (1 - mr.RAMP_K * r3)
        rgb = np.where(band[..., None] >= 15, rgb, shaded)
    a = np.full(rgb.shape[:2] + (1,), mr.BLEND_ALPHA if mat["blend"] else 1.0, np.float32)
    return np.concatenate([rgb, a], 2)


def tramos_rampa(m, mat, max_tramos=4):
    """25/10 (usuario: "las figuras de B3 salen con el sombreado de B2"): la rampa toon REAL de B3 del material en tramos
    de luz. [(l_desde, rgba float)] de menor a mayor luz (l = 0,5 + 0,5 n.l, columna = l * (ancho - 1), como
    model_render._shade): cada tramo es la textura base menos la columna de la rampa de ese tramo, en la banda (alfa DXT3)
    de cada texel; la banda 15 no se sombrea. Una sola entrada (la cocida a plena luz) si no hay rampa."""
    cocida = _textura_cocida(m, mat)
    if mat["tex"] >= len(m.tex) or mat["env"] or mat["shader"] == 0xFFFFFFFF or mat["shader"] >= len(m.tex):
        return [(0.0, cocida)]
    base, ramp = m.tex[mat["tex"]], m.tex[mat["shader"]]
    rh, rw = ramp.shape[:2]
    band = np.clip(np.round(base[..., 3] * 15), 0, 15)
    cambios = {}
    for bd in np.unique(band[band < 15]).astype(int):
        fila = ramp[int(np.clip((bd + 0.5) * rh / 16, 0, rh - 1)), :, :3]
        for x in range(1, rw):
            salto = float(np.abs(fila[x] - fila[x - 1]).sum())
            if salto > 0.03:
                cambios[x] = max(cambios.get(x, 0.0), salto)
    cols = sorted(sorted(cambios, key=lambda x: -cambios[x])[:max_tramos - 1])
    if not cols:
        return [(0.0, cocida)]
    yi = np.clip(((band + 0.5) * rh / 16).astype(int), 0, rh - 1)
    out = []
    for desde, hasta in zip([0] + cols, cols + [rw]):
        c = min(rw - 1, (desde + hasta - 1) // 2)
        rgb = np.clip(base[..., :3] - mr.RAMP_K * ramp[yi, c, :3], 0, 1)
        rgb = np.where(band[..., None] >= 15, base[..., :3], rgb)
        out.append((desde / float(rw - 1), np.concatenate([rgb, cocida[..., 3:4]], 2)))
    return out


def exportar_malla(model, pose, path):
    """Malla posada para la vista previa 3D EN EL JUEGO (heroe_ui.cpp -> ps2::Escena, el pintor ImGui de los modelos de
    IW/B2): HMV1, n texturas, n triangulos, alto; texturas (w, h, RGBA); triangulos (3 x pos, normal, uv; textura,
    banderas). Little endian. Centrada en x/z, pies en y = 0."""
    import struct  # noqa: PLC0415
    P, N = model.posed(pose)
    tris = model.tris
    usados = np.unique(tris.reshape(-1)) if len(tris) else np.arange(len(P))
    lo, hi = P[usados].min(0), P[usados].max(0)
    P = P - np.array([(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2])
    texs, clave_tex = [], {}
    tri_tex, tramos = [], []
    for mi in model.tri_mat:
        mat = model.mats[mi]
        k = (mat["tex"], mat["shader"], mat["env"], tuple(np.round(mat["diffuse"], 3)), mat["blend"])
        if k not in clave_tex:
            clave_tex[k] = len(texs)
            texs.append(_textura_cocida(model, mat))
            tramos.append(tramos_rampa(model, mat))
        tri_tex.append(clave_tex[k])
    # 25/10: + las texturas cocidas de cada tramo de la rampa toon de B3 y, al final, la tabla "RMP1" (los lectores viejos
    # de HMV1 se paran tras los triangulos y no la ven)
    n_orig, tabla = len(texs), []
    for i, tr in enumerate(tramos):
        if len(tr) <= 1:
            tabla.append([(0.0, i)])
            continue
        tabla.append([(l0, len(texs) + k) for k, (l0, _) in enumerate(tr)])
        texs.extend(rgba for _, rgba in tr)
    out = bytearray(b"HMV1")
    out += struct.pack("<IIf", len(texs), len(tris), float(hi[1] - lo[1]))
    for t in texs:
        img = (np.clip(t, 0, 1) * 255 + 0.5).astype(np.uint8)
        out += struct.pack("<II", img.shape[1], img.shape[0]) + img.tobytes()
    blend = [model.mats[mi]["blend"] for mi in model.tri_mat]
    for i, tri in enumerate(tris):
        for v in tri:
            out += struct.pack("<8f", *P[v], *N[v], *model.uv[v])
        out += struct.pack("<iI", tri_tex[i], 1 if blend[i] else 0)
    out += b"RMP1" + struct.pack("<I", n_orig)
    for fila in tabla:
        out += struct.pack("<I", len(fila))
        for l0, ti in fila:
            out += struct.pack("<fI", l0, ti)
    tmp = path + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(out)
    os.replace(tmp, path)
    return len(tris), len(texs)


# ---------------------------------------------------------------------------------------------------------------------
# Vista previa de la FICHA (studies 8 fase 2, H2): el cuerpo y traje nativos + los colores de la ficha (Parte 1-4 y
# pelo 1-2), el peinado UT (pieza convertida), el logo y el scouter. Todo se hace sobre la malla posada; nada toca el
# juego. Reglas (las de UT no estan medidas, son [H]):
#   * zona de cada triangulo por el hueso dominante de sus vertices (etiquetas del #AWO):
#       pelo = *HAIR*; cara = FACE / HEAD / M_ / TEETH / EYE (no se pinta); Parte 1 = torso (CHEST, STMC, WAIST, OBI,
#       CHN, BODY, NECK y hombro/brazo ARMROT/ARM1: las mangas van con la parte de arriba); Parte 2 = piernas (LEG);
#       Parte 3 = antebrazos y manos (ARM2, HAND, NLA, NRA: munequeras y guantes); Parte 4 = pies
#       (FOOT, NLF, NRF).
#   * color (h, s, v) = (0, 0, 0): el original. Si no, se TINE: tono h, saturacion y brillo como la muestra de la
#     pantalla (heroe_ui.cpp ColorHsv: s 0.55 + s/222, v 0.6 + v/250) y el sombreado del pixel (su brillo relativo a la
#     mediana de la zona). No se tinen la piel (tono 10-50, sat 0.12-0.62, brillo > 0.45) ni las lineas (brillo < 0.15).
#   * pelo 1 = mitad oscura del pelo (base), pelo 2 = mitad clara (brillos); con el 2 a cero, el 1 pinta todo.
# ---------------------------------------------------------------------------------------------------------------------
ZONAS = (("pelo", ("HAIR",)), ("cara", ("FACE", "M_", "TEETH", "EYE", "HEAD", "MOUTH", "TONGUE")),
         (0, ("CHEST", "STMC", "WAIST", "OBI", "CHN", "BODY", "NECK", "NW", "TAIL", "ARMROT", "ARM1")),
         (1, ("LEG",)), (2, ("ARM", "HAND", "NLA", "NRA")), (3, ("FOOT", "NLF", "NRF")))
CERO = (0, 0, 0)


def zona_de(label):
    for z, toks in ZONAS:
        # "M_" (boca) solo como prefijo de hueso: "GHM_CHEST" (Gohan Cell) no es cara (DS9, 24/10)
        if any((label.startswith(t) or "_" + t in label) if t == "M_" else t in label for t in toks):
            return z
    return 0


def zonas_triangulos(model):
    import collections  # noqa: PLC0415
    out = []
    for tri in model.tris:
        c = collections.Counter(zona_de(model.labels[b] if b < len(model.labels) else "") for b in model.vbone[tri])
        out.append("pelo" if c.get("pelo") else c.most_common(1)[0][0])
    return out


def _rgb_hsv(rgb):
    mx, mn = rgb.max(-1), rgb.min(-1)
    d = mx - mn
    h = np.zeros_like(mx)
    m = d > 1e-6
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    i = m & (mx == r)
    h[i] = ((g - b)[i] / d[i]) % 6
    i = m & (mx == g) & (mx != r)
    h[i] = (b - r)[i] / d[i] + 2
    i = m & (mx == b) & (mx != r) & (mx != g)
    h[i] = (r - g)[i] / d[i] + 4
    s = np.where(mx > 1e-6, d / np.maximum(mx, 1e-6), 0)
    return h * 60.0, s, mx


def _hsv_rgb(h, s, v):
    h = (h % 360.0) / 60.0
    c = v * s
    x = c * (1 - np.abs(h % 2 - 1))
    z = np.zeros_like(h)
    k = np.floor(h).astype(int) % 6
    r = np.choose(k, [c, x, z, z, x, c])
    g = np.choose(k, [x, c, c, x, z, z])
    b = np.choose(k, [z, z, x, c, c, x])
    m = v - c
    return np.stack([r + m, g + m, b + m], -1)


def objetivo(c):
    """(tono, sat, brillo) 0..1 del color de la ficha: el de la muestra de heroe_ui.cpp (ColorHsv)."""
    return (c[0] % 360, float(np.clip(0.55 + c[1] / 222.0, 0, 1)), float(np.clip(0.6 + c[2] / 250.0, 0, 1)))


def tenir(tex, c, c2=None, piel=True):
    """Tine la textura RGBA float (0..1) con el color c de la ficha (y c2 en la mitad clara, para el pelo)."""
    if tuple(c) == CERO and (c2 is None or tuple(c2) == CERO):
        return tex
    out = tex.copy()
    h, s, v = _rgb_hsv(tex[..., :3])
    mask = v >= 0.15 if piel else np.ones(v.shape, bool)   # el pelo nativo es casi negro: se tine entero
    if piel:
        mask &= ~((h >= 10) & (h <= 50) & (s >= 0.12) & (s <= 0.62) & (v > 0.45))
    if not mask.any():
        return tex
    vv = v[mask]
    med = float(np.median(vv)) if vv.size else 0.5
    rel = np.clip(v / max(med, 0.05), 0, 1.6) if piel else np.clip(0.75 + 1.2 * (v - med), 0.4, 1.3)
    usa2 = c2 is not None and tuple(c2) != CERO
    for col, sel in ((c, mask & ~(usa2 & (v > med))), (c2, mask & (v > med) if usa2 else None)):
        if col is None or sel is None or tuple(col) == CERO or not sel.any():
            continue
        th, ts, tv = objetivo(col)
        nv = np.clip(tv * rel[sel], 0, 1)
        out[..., :3][sel] = _hsv_rgb(np.full(nv.shape, th), np.full(nv.shape, ts), nv)
    return out


def _liso(rgb, alfa=1.0):
    return np.tile(np.array(list(rgb) + [alfa], np.float32)[None, None, :], (4, 4, 1))


def _emblema(logo, size=64):
    """Logo [H]: marca dibujada por codigo (circulo + siglas), no el arte de UT."""
    from PIL import ImageDraw  # noqa: PLC0415
    siglas = {1: "亀", 2: "界", 3: "CC", 4: "RR", 5: "M", 6: "神", 7: "GP"}
    colores = {1: (240, 140, 20), 2: (40, 60, 200), 3: (30, 120, 220), 4: (210, 30, 30), 5: (200, 30, 60),
               6: (150, 80, 200), 7: (40, 160, 80)}
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse((1, 1, size - 2, size - 2), fill=(250, 250, 245, 255), outline=(20, 20, 20, 255), width=3)
    txt = siglas.get(logo, "?")
    fuente = None
    from PIL import ImageFont  # noqa: PLC0415
    for nombre in ("msgothic.ttc", "YuGothB.ttc", "arialbd.ttf", "arial.ttf"):
        try:
            fuente = ImageFont.truetype(nombre, int(size * (0.55 if len(txt) == 1 else 0.42)))
            break
        except OSError:
            continue
    if fuente is None:
        fuente = ImageFont.load_default()
    if len(txt) == 1 and ord(txt) > 255:
        try:
            fuente.getbbox(txt)
        except Exception:   # noqa: BLE001
            txt = "K"
    bb = d.textbbox((0, 0), txt, font=fuente)
    d.text(((size - (bb[2] - bb[0])) / 2 - bb[0], (size - (bb[3] - bb[1])) / 2 - bb[1]), txt,
           fill=colores.get(logo, (0, 0, 0)) + (255,), font=fuente)
    return np.asarray(im, np.float32) / 255.0


def buscar_pelo(juego, pelo):
    """Pieza de pelo UT convertida (pelo k >= 1 = catalogo piezas_cac.toml id k-1). Las piezas son del juego UT del
    usuario (lote_cac.py): <juego>/cac_piezas/pelo/, DBZ3_CAC_PIEZAS o la carpeta de trabajo."""
    nombres = ["hair_000_01", "hair_000_02", "hair_002_01", "hair_003_01", "hair_004_01", "hair_005_01",
               "hair_006_01", "hair_007_01", "hair_008_01"]
    if not (1 <= pelo <= len(nombres)):
        return None
    dirs = [os.path.join(juego, "cac_piezas", "pelo")] if juego else []
    if os.environ.get("DBZ3_CAC_PIEZAS"):
        dirs.insert(0, os.path.join(os.environ["DBZ3_CAC_PIEZAS"], "pelo"))
    dirs.append(r"E:\DBZ3HD_trabajo\ut\cac\convertidas\pelo")
    for d in dirs:
        p = os.path.join(d, "avatar_%s_b3.bin" % nombres[pelo - 1])
        if os.path.exists(p):
            return p
    return None


def _caja(c, ex, ey, ez):
    """12 triangulos de una caja (centro c, semiejes ex/ey/ez) con normales planas y uv 0..1."""
    tris = []
    for ax in range(3):
        for sg in (-1, 1):
            n = np.zeros(3)
            n[ax] = sg
            u, v = [a for a in range(3) if a != ax]
            e = np.array([ex, ey, ez])
            q = []
            for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                p = np.array(c, float)
                p[ax] += sg * e[ax]
                p[u] += a * e[u]
                p[v] += b * e[v]
                q.append((p, (a * 0.5 + 0.5, b * 0.5 + 0.5)))
            tris.append((q[0], q[1], q[2], n))
            tris.append((q[0], q[2], q[3], n))
    return tris


def ajustar_pelo(model, P, zonas, pieza, pose, pelo_nativo=None, usados=None):
    """Ajusta la pieza UT a la caja del pelo nativo (o a lo alto de la cabeza si el cuerpo no tiene pelo propio) [H]:
    el conversor UT->B3 no deja la pieza a la escala del cuerpo. Devuelve (pm, PP, PN, usados_pm, escala, Q) con Q las
    posiciones ya en el espacio modelo del cuerpo. Lo usan la vista previa (ficha_malla) y el combate (injertar_ficha)."""
    if pelo_nativo is None:
        pelo_nativo = [i for i, z in enumerate(zonas) if z == "pelo"]
    if usados is None:
        usados = np.unique(model.tris.reshape(-1))
    pm = mr.Model(open(pieza, "rb").read())
    PP, PN = pm.posed(pose_de(pm))
    u = np.unique(pm.tris.reshape(-1))
    plo, phi = PP[u].min(0), PP[u].max(0)
    if pelo_nativo:
        vs = np.unique(model.tris[pelo_nativo].reshape(-1))
        dlo, dhi = P[vs].min(0), P[vs].max(0)
    else:
        cabeza = [i for i, z in enumerate(zonas) if z == "cara"]
        vs = np.unique(model.tris[cabeza].reshape(-1)) if cabeza else usados
        clo, chi = P[vs].min(0), P[vs].max(0)
        dlo = np.array([clo[0], chi[1] - (chi[1] - clo[1]) * 0.45, clo[2]])
        dhi = np.array([chi[0], chi[1] + (chi[1] - clo[1]) * 0.25, chi[2]])
    esc = min((dhi[0] - dlo[0]) / max(phi[0] - plo[0], 1e-6), (dhi[2] - dlo[2]) / max(phi[2] - plo[2], 1e-6)) * 1.08
    centro_p = np.array([(plo[0] + phi[0]) / 2, plo[1], (plo[2] + phi[2]) / 2])
    centro_d = np.array([(dlo[0] + dhi[0]) / 2, dlo[1] + (dhi[1] - dlo[1]) * 0.12, (dlo[2] + dhi[2]) / 2])
    Q = (PP - centro_p) * esc + centro_d
    return pm, PP, PN, u, esc, Q


def ficha_malla(model, ficha, juego, path, afs=None):
    """Exporta la malla HMV1 de la ficha (mismo formato que exportar_malla). Devuelve un resumen para el log."""
    import struct  # noqa: PLC0415
    datos = bytearray(model.data)
    rp = rampa_piel_tenida(model, ficha.get("piel", CERO))
    if rp:   # tono de piel unico, ANTES de las prendas (su piel se cuece con esta rampa), como el combate
        import injertar_ropa as ir  # noqa: PLC0415
        ir._escribir_textura(datos, rp[0], (np.clip(rp[1], 0, 1) * 255 + 0.5).astype(np.uint8))
    # (tintar: el color de cada parte, como el combate; 25/10: la vista previa no lo ensenaba)
    b3, b3_resumen = _aplicar_piezas_b3(bytes(datos), ficha, juego, afs, tintar=True) if afs is not None else (
        bytes(datos), {})
    if b3 != model.data:
        model = mr.Model(b3)
    if afs is not None:   # tipo de rostro y colores de ojos y cejas (la cara normal: la vista previa no gesticula)
        try:
            for ti, rgba in texturas_ficha(bytes(model.data), ficha, afs, solo_base=True).items():
                model.tex[ti] = rgba
        except Exception as e:   # noqa: BLE001
            print("vista_heroe: rostro sin aplicar: %s" % e)
    pose = pose_de(model)
    P, N = model.posed(pose)
    zonas = zonas_ficha(model, P)
    partes = ficha.get("partes", [CERO] * 4)
    c1, c2 = tuple(ficha.get("c1", CERO)), tuple(ficha.get("c2", CERO))
    pelo = int(ficha.get("pelo", 0))
    pieza = buscar_pelo(juego, pelo) if pelo > 0 and _xv2_piezas_activas(juego) else None
    tris, texs, cache = [], [], {}   # tris: (3 pos, 3 nrm, 3 uv, tex, blend)
    pelo_nativo = [i for i, z in enumerate(zonas) if z == "pelo"]
    rampa_de = {}   # textura -> [(luz desde, rgba)] de la rampa de B3 (tabla "RMP1" al final)
    nat = None   # H3: sin pieza UT, el pelo de un cuerpo NATIVO de B3 (el mismo injerto que el combate)
    if not pieza and pelo > 0 and afs is not None and pelo in PELOS_NATIVOS:
        try:
            nat = _pelo_donante(model, P, zonas, afs.entry((KFID.get(PELOS_NATIVOS[pelo]) or KFID[0])[0]))
        except Exception:   # noqa: BLE001 (sin injerto: pelo del cuerpo)
            nat = None
    for i, tri in enumerate(model.tris):
        z = zonas[i]
        if z == "pelo" and (pieza or nat):
            continue
        mi = model.tri_mat[i]
        mat = model.mats[mi]
        k = (mat["tex"], mat["shader"], mat["env"], tuple(np.round(mat["diffuse"], 3)), mat["blend"], z)
        if k not in cache:
            def tinte(t, z=z):
                if z == "pelo":
                    return tenir(t, c1, c2, piel=False)
                if isinstance(z, int):
                    return tenir(t, tuple(partes[z]) if z < len(partes) else CERO)
                return t
            cache[k] = len(texs)
            texs.append(tinte(_textura_cocida(model, mat)))
            tr = tramos_rampa(model, mat)   # 25/10: la rampa toon de B3 (como las figuras de B2), con el mismo tinte
            if len(tr) > 1:
                rampa_de[cache[k]] = [(l0, tinte(rgba)) for l0, rgba in tr]
        tris.append((P[tri], N[tri], model.uv[tri], cache[k], mat["blend"]))
    usados = np.unique(model.tris.reshape(-1))
    lo, hi = P[usados].min(0), P[usados].max(0)
    resumen = {"pelo": "nativo", "piezas_b3": b3_resumen}
    # --- peinado UT: la pieza (esqueleto de la plantilla) se ajusta a la caja del pelo nativo (o a lo alto de la
    # cabeza si el cuerpo no tiene pelo propio) [H]: el conversor UT->B3 no deja la pieza a la escala del cuerpo.
    if pieza:
        pm, PP, PN, u, esc, Q = ajustar_pelo(model, P, zonas, pieza, pose, pelo_nativo, usados)
        base = tuple(c1) if tuple(c1) != CERO else None
        tex_pelo = _liso((0.16, 0.16, 0.19)) if base is None else _liso(_hsv_rgb(*[np.array(x) for x in objetivo(base)]))
        if base is not None and tuple(c2) != CERO:   # dos tonos: claro arriba
            th, ts, tv = objetivo(c2)
            g = np.linspace(0, 1, 16)[:, None, None] * np.ones((16, 4, 1))
            claro = _hsv_rgb(np.full((16, 4), th), np.full((16, 4), ts), np.full((16, 4), tv))
            oscuro = tex_pelo[0, 0, :3]
            rgb = oscuro * (1 - g) + claro * g
            tex_pelo = np.concatenate([rgb, np.ones((16, 4, 1))], 2).astype(np.float32)
        texs.append(tex_pelo.astype(np.float32))
        ti = len(texs) - 1
        ylo, yhi = Q[u, 1].min(), Q[u, 1].max()
        for tri in pm.tris:
            uv = np.stack([np.full(3, 0.5), np.clip(1 - (Q[tri, 1] - ylo) / max(yhi - ylo, 1e-6), 0, 1)], 1)
            tris.append((Q[tri], PN[tri], uv, ti, False))
        resumen["pelo"] = "%s (%d triangulos, escala %.2f)" % (os.path.basename(pieza), len(pm.tris), esc)
        hi = np.maximum(hi, Q[u].max(0))
    elif nat:
        dm, ht, dvs, Q, QN, tn, _raiz_nat = nat
        Q = _pelo_fuera_de_cara(model, P, zonas, Q, tn)
        remap = {int(v): i for i, v in enumerate(dvs)}
        tcache = {}
        for t, tri in zip(ht, tn):
            mat = dm.mats[dm.tri_mat[t]]
            k = id(mat)
            if k not in tcache:
                tcache[k] = len(texs)
                texs.append(tenir(_textura_cocida(dm, mat), c1, c2, piel=False))
                tr = tramos_rampa(dm, mat)
                if len(tr) > 1:
                    rampa_de[len(texs) - 1] = [(l0, tenir(rgba, c1, c2, piel=False)) for l0, rgba in tr]
            tris.append((Q[list(tri)], QN[list(tri)], dm.uv[dm.tris[t]], tcache[k], mat["blend"]))
        resumen["pelo"] = "nativo B3 donante %d (%d triangulos)" % (PELOS_NATIVOS[pelo], len(tn))
        hi = np.maximum(hi, Q.max(0))
    elif pelo > 0:
        resumen["pelo"] = "pieza %d no encontrada: nativo" % pelo
    cv = _calva(model, P, zonas, afs) if (pieza or nat) and afs is not None else None
    if cv and nat:   # (como el combate)
        cv = _calva_por_pelo(cv, model, P, zonas, nat[3], nat[5])
    if cv:   # calva completa debajo del peinado nuevo (igual que el combate)
        rp = _rgb_pelo(nat[0], nat[1], c1) if nat else (tuple(float(x) for x in tex_pelo[0, 0, :3]))
        for ex in _extras_calva(cv, rp):
            texs.append(_liso(ex["rgb"]))
            for tri in ex["tris"]:
                tris.append((ex["P"][list(tri)], ex["N"][list(tri)], np.full((3, 2), 0.5), len(texs) - 1, False))
        resumen["calva"] = "craneo de Krilin (%d + %d triangulos)" % (len(cv[2]), len(cv[3]))
    # --- logo [H]: disco con la marca en el pecho (delante del torso, a la altura del pecho)
    logo = int(ficha.get("logo", 0))
    if logo > 0:
        torso = [i for i, z in enumerate(zonas) if z == 0]
        if torso:
            vs = np.unique(model.tris[torso].reshape(-1))
            tlo, thi = P[vs].min(0), P[vs].max(0)
            y = tlo[1] + (thi[1] - tlo[1]) * 0.78
            r = (thi[0] - tlo[0]) * 0.11
            cerca = vs[np.abs(P[vs, 1] - y) < r * 1.5]
            cerca = cerca[np.abs(P[cerca, 0] - (tlo[0] + thi[0]) / 2 + r * 1.6) < r * 1.5]
            z = (P[cerca, 2].max() if len(cerca) else thi[2]) + r * 0.08
            cx = (tlo[0] + thi[0]) / 2 - r * 1.6   # lado izquierdo del pecho (como el kanji de los gi) [H]
            texs.append(_emblema(logo))
            ti = len(texs) - 1
            n = np.array([0, 0, 1.0])
            seg = 20
            for s in range(seg):
                a0, a1 = 2 * np.pi * s / seg, 2 * np.pi * (s + 1) / seg
                pts = [np.array([cx, y, z]), np.array([cx + r * np.cos(a0), y + r * np.sin(a0), z]),
                       np.array([cx + r * np.cos(a1), y + r * np.sin(a1), z])]
                uv = [(0.5, 0.5), (0.5 + 0.5 * np.cos(a0), 0.5 - 0.5 * np.sin(a0)),
                      (0.5 + 0.5 * np.cos(a1), 0.5 - 0.5 * np.sin(a1))]
                tris.append((np.array(pts), np.array([n] * 3), np.array(uv), ti, False))
            resumen["logo"] = logo
    # --- scouter [H]: lente verde translucida delante del ojo izquierdo + la pieza de la oreja
    if int(ficha.get("scouter", 0)):
        cara = [i for i, z in enumerate(zonas) if z == "cara"]
        if cara:
            vs = np.unique(model.tris[cara].reshape(-1))
            flo, fhi = P[vs].min(0), P[vs].max(0)
            w = fhi[0] - flo[0]
            y = flo[1] + (fhi[1] - flo[1]) * 0.55
            ojo = np.array([flo[0] + w * 0.22, y, fhi[2] + w * 0.04])
            texs.append(_liso((0.15, 0.95, 0.35), 0.55))
            tl = len(texs) - 1
            texs.append(_liso((0.75, 0.75, 0.8)))
            tm = len(texs) - 1
            for (a, b, c, n) in _caja(ojo, w * 0.2, w * 0.13, w * 0.015):
                tris.append((np.array([a[0], b[0], c[0]]), np.array([n] * 3), np.array([a[1], b[1], c[1]]), tl, True))
            oreja = np.array([flo[0] - w * 0.02, y, (flo[2] + fhi[2]) / 2 + w * 0.12])
            for (a, b, c, n) in _caja(oreja, w * 0.06, w * 0.16, w * 0.14):
                tris.append((np.array([a[0], b[0], c[0]]), np.array([n] * 3), np.array([a[1], b[1], c[1]]), tm, False))
            brazo = (oreja + ojo) / 2
            for (a, b, c, n) in _caja(brazo, abs(ojo[0] - oreja[0]) / 2, w * 0.02, abs(ojo[2] - oreja[2]) / 2 + w * 0.02):
                tris.append((np.array([a[0], b[0], c[0]]), np.array([n] * 3), np.array([a[1], b[1], c[1]]), tm, False))
            resumen["scouter"] = 1
    off = np.array([(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2])
    n_orig, tabla = len(texs), []
    for i in range(n_orig):
        tr = rampa_de.get(i)
        if not tr:
            tabla.append([(0.0, i)])
            continue
        tabla.append([(l0, len(texs) + k) for k, (l0, _) in enumerate(tr)])
        texs.extend(rgba for _, rgba in tr)
    out = bytearray(b"HMV1")
    out += struct.pack("<IIf", len(texs), len(tris), float(hi[1] - lo[1]))
    for t in texs:
        img = (np.clip(t, 0, 1) * 255 + 0.5).astype(np.uint8)
        out += struct.pack("<II", img.shape[1], img.shape[0]) + img.tobytes()
    for (pp, nn, uu, ti, bl) in tris:
        for j in range(3):
            out += struct.pack("<8f", *(pp[j] - off), *nn[j], *uu[j])
        out += struct.pack("<iI", ti, 1 if bl else 0)
    out += b"RMP1" + struct.pack("<I", n_orig)
    for fila in tabla:
        out += struct.pack("<I", len(fila))
        for l0, ti in fila:
            out += struct.pack("<fI", l0, ti)
    tmp = path + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(out)
    os.replace(tmp, path)
    resumen.update(triangulos=len(tris), texturas=len(texs))
    return resumen


def _marco_cara(model, P, hface):
    """(centro, frente, lado, ylo, yhi, ancho) de la cara en espacio modelo: frente = suma de normales de la cara en el
    plano (hacia donde mira), lado = arriba x frente, ancho = lo que mide la cara de lado a lado."""
    hv = np.unique(model.tris[hface].reshape(-1))
    ft = model.tris[hface]
    f = np.cross(P[ft[:, 1]] - P[ft[:, 0]], P[ft[:, 2]] - P[ft[:, 0]]).sum(0)
    f[1] = 0
    f /= max(np.linalg.norm(f), 1e-9)
    lado = np.cross([0.0, 1.0, 0.0], f)
    c = (P[hv].min(0) + P[hv].max(0)) / 2
    return c, f, lado, P[hv, 1].min(), P[hv, 1].max(), float(np.ptp((P[hv] - c) @ lado))


def zonas_ficha(model, P, pelo_color=False):
    """zonas_triangulos + el pelo pegado al hueso de la cabeza (triangulos oscuros por encima de los ojos)."""
    zonas = zonas_triangulos(model)
    cara = [i for i, z in enumerate(zonas) if z == "cara"]
    if cara:
        vs = np.unique(model.tris[cara].reshape(-1))
        ylo, yhi = P[vs, 1].min(), P[vs, 1].max()
        cocidas, info = {}, []
        px_de, piel_de = {}, {}
        for i in cara:
            mat = model.mats[model.tri_mat[i]]
            if mat["tex"] not in cocidas:
                cocidas[mat["tex"]] = _textura_cocida(model, mat)
            t = cocidas[mat["tex"]]
            h, w = t.shape[:2]
            uv = model.uv[model.tris[i]].mean(0)
            px = t[int(np.clip((uv[1] % 1) * h, 0, h - 1)), int(np.clip((uv[0] % 1) * w, 0, w - 1)), :3]
            hh, s, v = (float(np.ravel(x)[0]) for x in _rgb_hsv(px[None, :3]))
            piel = 10 <= hh <= 50 and 0.12 <= s <= 0.62 and v > 0.45
            color = not piel and s > 0.05 and v >= 0.22          # ni piel, ni blanco de ojos, ni linea
            alto = P[model.tris[i], 1].min() > ylo + (yhi - ylo) * 0.62
            px_de[i], piel_de[i] = px, piel
            info.append((i, mat["tex"], px.max() < 0.22, color, alto))
        # pelo oscuro (Goku, Vegeta): lo alto y oscuro. Pelo de color pegado a HEAD (Trunks del futuro, lila, sin huesos
        # HAIR: antes solo salian 7 triangulos): toda la parte de color de las texturas que lo traen en lo alto. Solo para
        # el DONANTE de pelo (pelo_color): en un cuerpo calvo o alien la cupula de color de la cabeza no es pelo.
        tex_pelo = {tex for _, tex, _, color, alto in info if color and alto} if pelo_color else set()
        if pelo_color and tex_pelo:
            # 26/10 DS94 (coordinador: "Trunks sale con la melena incompleta"): la melena larga de Trunks usa OTRA
            # textura (tex 2) del mismo color lila que la de arriba (tex 4/5), pero todos sus triangulos quedan por
            # debajo del 62 % de altura y no son oscuros: ninguno entraba en tex_pelo y la melena se perdia (medido:
            # 54 triangulos, color identico [0,91,0,85,0,92]). Se anaden las texturas de la CABEZA cuyo color medio
            # coincide con el de una textura de pelo YA confirmada y que no son de piel. Solo donante (pelo_color):
            # en el cuerpo del heroe esa manga ancha borraria ojos/cejas.
            import collections as _col  # noqa: PLC0415
            paleta = [np.median([px_de[i] for i, tex, _, _, _ in info if tex == t_], 0) for t_ in tex_pelo]
            por_tex = _col.defaultdict(list)
            for i, tex, _, _, _ in info:
                por_tex[tex].append(i)
            for tex, idx in por_tex.items():
                if tex in tex_pelo or len(idx) < 4:
                    continue
                if sum(bool(piel_de[i]) for i in idx) > 0.25 * len(idx):
                    continue
                med = np.median([px_de[i] for i in idx], 0)
                if min(float(np.abs(med - pal).sum()) for pal in paleta) <= 0.08:
                    tex_pelo.add(tex)
        # donante: lo oscuro DETRAS de la cara tambien es pelo (la melena baja de Bardock y Goten; con solo "lo alto" la
        # nuca del peinado se perdia y asomaba la calva). Delante (cejas, ojos) no se toca.
        c, f, _, _, _, ancho = _marco_cara(model, P, cara) if pelo_color else (None,) * 6
        for i, tex, oscuro, color, alto in info:   # bien detras (no las cejas, que quedan cerca del centro)
            detras = pelo_color and oscuro and (P[model.tris[i]].mean(0) - c) @ f < -0.15 * ancho
            if (oscuro and alto) or detras or (color and tex in tex_pelo):
                zonas[i] = "pelo"
        if pelo_color:
            # 25/10 (Yamcha y Krilin con pelo: solo salia la coronilla): el pelo crece por los triangulos OSCUROS
            # vecinos (soldados por posicion) que no estan delante de la cara: los lados sobre la oreja y la nuca
            import collections  # noqa: PLC0415
            claro, tono = {}, {}

            def es_piel(i):
                hh_, s_, v_ = tono[i]
                return 10 <= hh_ <= 50 and 0.12 <= s_ <= 0.62 and v_ > 0.45
            for i in cara:
                mat = model.mats[model.tri_mat[i]]
                t = cocidas[mat["tex"]]
                h, w = t.shape[:2]
                uvs = model.uv[model.tris[i]] % 1
                pts = np.vstack([uvs, uvs.mean(0)[None]])
                vals = [t[int(np.clip(v * h, 0, h - 1)), int(np.clip(u * w, 0, w - 1)), :3].max() for u, v in pts]
                claro[i] = float(np.median(vals))
                px = t[int(np.clip(pts[-1][1] * h, 0, h - 1)), int(np.clip(pts[-1][0] * w, 0, w - 1)), :3]
                tono[i] = tuple(float(np.ravel(x)[0]) for x in _rgb_hsv(px[None, :3]))
            key = np.round(P, 3)
            _, vid = np.unique(key, axis=0, return_inverse=True)
            vid = vid.ravel()
            por_v = collections.defaultdict(list)
            for i in cara:
                for v in vid[model.tris[i]]:
                    por_v[int(v)].append(i)
            frente = set()
            for i in cara:
                cc = P[model.tris[i]].mean(0)
                if (cc - c) @ f > 0.1 * ancho and abs((cc - c) @ _marco_cara(model, P, cara)[2]) < 0.42 * ancho:
                    frente.add(i)
            # y todo lo oscuro de la TEXTURA del pelo (la que llevan casi todos los triangulos de pelo ya hallados;
            # Krilin con pelo: 36 triangulos de su nuca en esa textura no tocaban al resto)
            tex_cnt = collections.Counter(model.mats[model.tri_mat[i]]["tex"] for i in cara if zonas[i] == "pelo")
            if tex_cnt:
                tp, n_tp = tex_cnt.most_common(1)[0]
                if n_tp >= 0.6 * sum(tex_cnt.values()):
                    # 25/10 (medido contra el original: a Yamcha y a Krilin les faltaba el mechon de delante, el "recorte"):
                    # si la textura del pelo NO es de cara (casi sin piel: las cejas de Goku y Vegeta van en la de la
                    # cara), lo oscuro de esa textura delante de la cara tambien es pelo
                    de_tp = [i for i in cara if model.mats[model.tri_mat[i]]["tex"] == tp]
                    sin_piel = sum(es_piel(i) for i in de_tp) < 0.05 * max(len(de_tp), 1)
                    for i in cara:
                        if zonas[i] != "pelo" and (sin_piel or i not in frente) and model.mats[model.tri_mat[i]]["tex"] == tp                                 and claro.get(i, 1.0) < 0.35:
                            zonas[i] = "pelo"
            # flequillo SUELTO por delante (Goten: su textura de pelo es tambien la de la cara): lo oscuro de delante
            # que no comparte ningun vertice con la piel es una pieza aparte, pelo; las cejas van pintadas sobre la cara
            claros_v = {int(vid[v]) for i in cara if claro.get(i, 1.0) >= 0.3 for v in model.tris[i]}
            for i in frente:
                if zonas[i] != "pelo" and claro.get(i, 1.0) < 0.3 and not any(int(vid[v]) in claros_v for v in model.tris[i]):
                    zonas[i] = "pelo"
            pila = [i for i in cara if zonas[i] == "pelo"]
            while pila:
                a = pila.pop()
                for v in vid[model.tris[a]]:
                    for b in por_v[int(v)]:
                        if zonas[b] != "pelo" and b not in frente and claro.get(b, 1.0) < 0.3                                 and P[model.tris[b], 1].mean() > ylo - 0.15 * (yhi - ylo):
                            zonas[b] = "pelo"
                            pila.append(b)
    return zonas


def disco_logo(model, P, zonas):
    """(cx, y, z_frente, r) del disco del logo en el pecho: la misma colocacion que la vista previa (ficha_malla)."""
    torso = [i for i, z in enumerate(zonas) if z == 0]
    if not torso:
        return None
    vs = np.unique(model.tris[torso].reshape(-1))
    tlo, thi = P[vs].min(0), P[vs].max(0)
    y = tlo[1] + (thi[1] - tlo[1]) * 0.78
    r = (thi[0] - tlo[0]) * 0.11
    cerca = vs[np.abs(P[vs, 1] - y) < r * 1.5]
    cx = (tlo[0] + thi[0]) / 2 - r * 1.6
    cerca = cerca[np.abs(P[cerca, 0] - cx) < r * 1.5]
    return cx, y, (P[cerca, 2].max() if len(cerca) else thi[2]), r


def _tri_en_disco(pp, disco):
    cx, y0, zf, r = disco
    return not (pp[:, 0].min() > cx + r or pp[:, 0].max() < cx - r or pp[:, 1].min() > y0 + r or pp[:, 1].max() < y0 - r
                or abs(pp[:, 2].mean() - zf) > r * 1.5)


def pintar_logo(out, model, P, zonas, ti, logo, disco):
    """Pinta el emblema del logo en la textura `ti` (RGBA float, se modifica) en los texeles del torso que caen dentro
    del disco (posicion 3D interpolada por el triangulo, cara delantera). Devuelve los texeles pintados."""
    h, w = out.shape[:2]
    cx, y0, zf, r = disco
    emb = _emblema(logo)
    eh, ew = emb.shape[:2]
    n = 0
    for i, z in enumerate(zonas):
        if z != 0 or model.mats[model.tri_mat[i]]["tex"] != ti:
            continue
        tri = model.tris[i]
        pp = P[tri]
        if not _tri_en_disco(pp, disco):
            continue
        uv = model.uv[tri] * np.array([w, h])
        x0, x1 = int(np.floor(uv[:, 0].min())), int(np.ceil(uv[:, 0].max()))
        y_0, y_1 = int(np.floor(uv[:, 1].min())), int(np.ceil(uv[:, 1].max()))
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y_0, y_1 + 1))
        px, py = xs + 0.5, ys + 0.5
        (ax, ay), (bx, by), (qx, qy) = uv
        den = (by - qy) * (ax - qx) + (qx - bx) * (ay - qy)
        if abs(den) < 1e-9:
            continue
        l1 = ((by - qy) * (px - qx) + (qx - bx) * (py - qy)) / den
        l2 = ((qy - ay) * (px - qx) + (ax - qx) * (py - qy)) / den
        l3 = 1 - l1 - l2
        dentro = (l1 >= -0.02) & (l2 >= -0.02) & (l3 >= -0.02)
        X = l1 * pp[0, 0] + l2 * pp[1, 0] + l3 * pp[2, 0]
        Y = l1 * pp[0, 1] + l2 * pp[1, 1] + l3 * pp[2, 1]
        Z = l1 * pp[0, 2] + l2 * pp[1, 2] + l3 * pp[2, 2]
        e = (X - cx) / r, (Y - y0) / r
        ok = dentro & (e[0] ** 2 + e[1] ** 2 <= 1) & (np.abs(Z - zf) < r * 0.6)
        if not ok.any():
            continue
        eu = np.clip(((e[0][ok] * 0.5 + 0.5) * ew).astype(int), 0, ew - 1)
        ev = np.clip(((0.5 - e[1][ok] * 0.5) * eh).astype(int), 0, eh - 1)
        col = emb[ev, eu]
        tx, ty = xs[ok] % w, ys[ok] % h
        vis = col[:, 3] > 0.5
        out[ty[vis], tx[vis], :3] = col[vis, :3]
        n += int(vis.sum())
    return n


def scouter_extras(model, P, zonas):
    """El scouter de la vista previa (lente + pieza de la oreja + brazo: tres cajas) como piezas rigidas a la cabeza
    para injertar_pelo (color liso: la lente es verde opaca, no translucida). Mismas medidas que ficha_malla."""
    cara = [i for i, z in enumerate(zonas) if z == "cara"]
    if not cara:
        return []
    vs = np.unique(model.tris[cara].reshape(-1))
    flo, fhi = P[vs].min(0), P[vs].max(0)
    w = fhi[0] - flo[0]
    y = flo[1] + (fhi[1] - flo[1]) * 0.55
    ojo = np.array([flo[0] + w * 0.22, y, fhi[2] + w * 0.04])
    oreja = np.array([flo[0] - w * 0.02, y, (flo[2] + fhi[2]) / 2 + w * 0.12])
    brazo = (oreja + ojo) / 2
    cajas = [((0.15, 0.95, 0.35), _caja(ojo, w * 0.2, w * 0.13, w * 0.015)),
             ((0.75, 0.75, 0.8), _caja(oreja, w * 0.06, w * 0.16, w * 0.14)),
             ((0.75, 0.75, 0.8), _caja(brazo, abs(ojo[0] - oreja[0]) / 2, w * 0.02,
                                       abs(ojo[2] - oreja[2]) / 2 + w * 0.02))]
    out = []
    for rgb, tris in cajas:
        Pp, Nn, tt = [], [], []
        for (a, b, c, n) in tris:
            k = len(Pp)
            Pp += [a[0], b[0], c[0]]
            Nn += [n, n, n]
            tt.append((k, k + 1, k + 2))
        out.append({"P": np.array(Pp), "N": np.array(Nn, float), "tris": tt, "rgb": rgb})
    return out


def injertar_ficha(amb, pieza, model=None, P=None, zonas=None, c1=CERO, c2=CERO, scouter=0, reserva=0, calva=None):
    """Combate: el peinado UT (pieza) pegado a la cabeza del #AMB del cuerpo, con el pelo nativo quitado
    (awo_tools/injertar_pelo.py). `amb` ya lleva los colores de la ficha cocidos. Devuelve (bytes, resumen)."""
    import injertar_pelo as ip  # noqa: PLC0415
    if model is None:
        model = mr.Model(bytes(amb))
    pose = pose_de(model)
    if P is None:
        P, _ = model.posed(pose)
    if zonas is None:
        zonas = zonas_ficha(model, P)
    if pieza:
        pm, PP, PN, u, esc, Q = ajustar_pelo(model, P, zonas, pieza, pose)
        tris_ut = pm.tris
    else:                      # solo scouter: sin peinado nuevo (el pelo nativo se queda)
        Q = PN = np.zeros((0, 3))
        tris_ut = []
    rgb = lambda c: None if tuple(c) == CERO else tuple(float(x) for x in _hsv_rgb(*[np.array(x) for x in objetivo(c)]))  # noqa: E731
    return ip.injertar(amb, model, [z == "pelo" for z in zonas], Q, PN, tris_ut, rgb1=rgb(c1), rgb2=rgb(c2),
                       cara_mask=[z == "cara" for z in zonas], presupuesto=ip.WINDOW_BUDGET - reserva,
                       extras=((scouter_extras(model, P, zonas) if scouter else [])
                               + (_extras_calva(calva, rgb(c1) or (0.16, 0.16, 0.19)) if pieza else [])) or None)


# ficha.ropa -> prenda de Xenoverse (HUM_<item>_Bust del data.cpk del jugador); 0 = la nativa del cuerpo
PRENDAS = {1: {"item": "500", "malla": "dougi", "lod": "02", "rgb": (0.93, 0.45, 0.06)}}


# Sin XV2 (o sin las piezas UT del jugador) los peinados 1..8 salen de personajes NATIVOS de B3: se injerta la zona de
# pelo de su propio bin con el mismo pipeline (awo_tools/injertar_pelo.py). La geometria se lee de su data_cmn.afs.
# indice de pelo de la ficha -> KFID del donante (= kPelosNativos de heroe_ui.cpp). 25/10: + Bardock, Trunks y Goten,
# que antes dejaban ver la cara por la nuca o un hueco en el cuello: los tapa la calva completa (_calva)
PELOS_NATIVOS = {1: 0, 2: 7, 3: 39, 4: 235, 5: 252, 6: 8, 7: 298, 8: 13, 9: 447, 10: 328}   # 8-10: Yamcha corto/largo, Krilin
# Super Saiyan del heroe (25/10): el pelo SSJ NATIVO de B3 de cada peinado (fid del modelo del que sale el pelo -> modelos
# con su pelo SSJ1, SSJ2, SSJ3). Donde B3 no trae ese nivel, el anterior; el SSJ3 que falta, el de Goku (268).
SSJ_PELO = {264: (266, 267, 268), 416: (417, 419, 268), 235: (236, 237, 268), 390: (391, 391, 268), 298: (299, 299, 268)}
SSJ_PELO_GOKU = (266, 267, 268)
# formas de combate del ESTILO (donante del moveset) para normal, SSJ1, SSJ2, SSJ3 (roster_build formas_donante; form_caps
# de roster_build: Goku 0 normal, 1 kaioken, 2 SSJ, 3 SSJ2, 4 SSJ3). Un estilo sin SSJ no transforma.
SSJ_FORMAS = {0: (0, 2, 3, 4), 3: (0, 1, 2), 4: (0, 1, 2), 6: (0, 1), 7: (0, 1, 2), 8: (0, 1), 9: (0, 1)}
SAIYAN_CUERPOS = {0, 7, 8, 39}   # heroe.cpp kRazas: Saiyan
# modelo (dentro del traje) de cada nivel SSJ del propio cuerpo: su cara SSJ nativa (cejas rubias, ojos azules; el SSJ3 de
# Goku sin cejas). Vegeta SSJ2 = su modelo SSJ (form_models del juego); Trunks solo SSJ; Bardock ninguno
SSJ_MODELO_CUERPO = {0: {1: 2, 2: 3, 3: 4}, 7: {1: 1, 2: 1}, 8: {1: 1}}

B3_ZONAS = {"torso": 0, "piernas": 1, "guantes": 2, "botas": 3}
B3_JSON = os.path.join(HERE, "piezas_b3_base.json")
_B3_CATALOGO = None


def _cargar_catalogo_b3():
    """Solo devuelve las piezas marcadas `ok` por el ensayo contra el cuerpo base 264."""
    global _B3_CATALOGO
    if _B3_CATALOGO is not None:
        return _B3_CATALOGO
    try:
        with open(B3_JSON, encoding="utf-8") as f:
            raw = json.load(f)
        # `ok` solo certifica que el injerto estructural funciono; no demuestra que
        # la pieza encaje visualmente. Solo exponer opciones con aprobacion visual
        # explicita y registrada despues de revisar frente y espalda. El modo de
        # ensayo solo se usa para generar hojas fuera del juego; no aprueba piezas.
        ensayo = os.environ.get("DBZ3_CAC_B3_TEST_CANDIDATOS") == "1"
        # DS130: `pruebas.py heroe_pieza` ensena en juego piezas aun NO aprobadas (ok:false). Solo con esta variable
        # de pruebas: no cambia el catalogo normal ni aprueba nada.
        todos = os.environ.get("DBZ3_CAC_B3_TEST_TODOS") == "1"
        _B3_CATALOGO = {z: {int(p["fid"]): p for p in raw.get(z, [])
                            if ((p.get("ok") is True and (p.get("validacion_visual") == "perfecto" or ensayo))
                                or (todos and isinstance(p.get("fid"), int) and p.get("nombre")))
                            and isinstance(p.get("fid"), int) and p.get("nombre")}
                        for z in B3_ZONAS}
    except Exception as e:  # noqa: BLE001 (sin catalogo: el aspecto base sigue disponible)
        _B3_CATALOGO = {z: {} for z in B3_ZONAS}
        print("vista_heroe: catalogo de piezas B3 no disponible: %s" % e)
    return _B3_CATALOGO


def _clave_hueso_b3(label):
    s = label.lstrip("X")
    s = s.split("_", 1)[1] if "_" in s else s
    return s.lstrip("_")


def _lado_hueso_b3(clave):
    """'R'/'L' del hueso del brazo o de la mano (None en los demás)."""
    if clave in ("RARM2", "LARM2"):
        return clave[0]
    if clave.endswith("RHAND"):
        return "R"
    if clave.endswith("LHAND"):
        return "L"
    # 27/10 DS187: cadenas de antebrazo propias del donante (Piccolo: LZARM1..7).
    if "ZARM" in clave and clave[:1] in ("R", "L"):
        return clave[0]
    return None


def _zona_hueso_b3(label):
    s = _clave_hueso_b3(label)
    if "HAIR" in s:
        return "pelo"
    if s.startswith("M_") or any(t in s for t in ("FACE", "TEETH", "EYE", "HEAD", "MOUTH", "TONGUE")):
        return "cara"
    if any(t in s for t in ("FOOT", "NLF", "NRF")):
        return "botas"
    # 27/10 DS187: "ZARM" = cadena de antebrazo propia del donante (Piccolo: LZARM1..7 cuelgan
    # de LARM1/LARM2 y dibujan TODO el antebrazo izquierdo). Sin esto quedaba en "accesorios",
    # el injerto quitaba el antebrazo del heroe y no ponia nada: mano flotando 2 u (350/352/353).
    if any(t in s for t in ("HAND", "NLA", "NRA", "ARM2", "ZARM")):
        return "guantes"
    if "LEG" in s:
        return "piernas"
    if any(t in s for t in ("CHEST", "STMC", "WAIST", "OBI", "BODY", "NECK", "NW", "TAIL", "ARMROT", "ARM1", "CHN")):
        return "torso"
    return "accesorios"


def _zonas_pieza_b3(model):
    """Zona por triangulo (huesos) y, 25/10, ajustada por TEXTURA: si una textura es en un 75 % de una zona de ropa, la
    zona es esa para todos sus triangulos (el cinturon de Goku va en la textura del pantalon, el bajo del pantalon en la
    de las botas). Asi cada pieza se cambia entera, sin restos de la vieja ni texturas pisadas a medias."""
    from collections import Counter  # noqa: PLC0415
    out = []
    for tri in model.tris:
        c = Counter(_zona_hueso_b3(model.labels[b]) if b < len(model.labels) else "accesorios"
                    for b in model.vbone[tri])
        out.append(c.most_common(1)[0][0])
    _botas_por_color(model, out)
    por_tex = {}
    for i, z in enumerate(out):
        por_tex.setdefault(model.mats[model.tri_mat[i]]["tex"], []).append(i)
    for tex, idx in por_tex.items():
        if tex >= len(model.tex):
            continue
        c = Counter(out[i] for i in idx)
        z, n = c.most_common(1)[0]
        if z in B3_ZONAS and n >= 0.75 * len(idx) and all(out[i] in B3_ZONAS for i in idx):
            for i in idx:
                out[i] = z
    return out


def _color_tri(model, i):
    mat = model.mats[model.tri_mat[i]]
    if mat["tex"] >= len(model.tex):
        return np.asarray(mat["diffuse"][:3], float)
    t = model.tex[mat["tex"]]
    h, w = t.shape[:2]
    uv = model.uv[model.tris[i]].mean(0)
    return np.asarray(t[int(np.clip((uv[1] % 1) * h, 0, h - 1)), int(np.clip((uv[0] % 1) * w, 0, w - 1)), :3], float)


def _botas_por_color(model, out):
    """La caña de la bota cuelga del hueso de la espinilla (LEG2), como el bajo del pantalon: si su color se parece al
    del pie (botas) mas que al del muslo (piernas), es bota. Sin esto las botas blancas de Vegeta iban con su pantalon."""
    def clave(i):
        from collections import Counter  # noqa: PLC0415
        return Counter(_clave_hueso_b3(model.labels[b]) for b in model.vbone[model.tris[i]] if b < len(model.labels)).most_common(1)[0][0]
    claves = [clave(i) for i in range(len(model.tris))]
    pie = [_color_tri(model, i) for i in range(len(out)) if out[i] == "botas" and "FOOT" in claves[i]]
    muslo = [_color_tri(model, i) for i in range(len(out)) if out[i] == "piernas" and "LEG1" in claves[i]]
    if not pie or not muslo:
        return
    cp, cm = np.median(pie, 0), np.median(muslo, 0)
    if np.linalg.norm(cp - cm) < 0.15:   # mismo color: no hay borde que buscar
        return
    for i in range(len(out)):
        if out[i] == "piernas" and "LEG2" in claves[i]:
            c = _color_tri(model, i)
            if np.linalg.norm(c - cp) < np.linalg.norm(c - cm):
                out[i] = "botas"


def _clave_tri(model, i):
    from collections import Counter  # noqa: PLC0415
    return Counter(_clave_hueso_b3(model.labels[b]) for b in model.vbone[model.tris[i]]
                   if b < len(model.labels)).most_common(1)[0][0]


def _alinear_zonas(donor, dz, host, hz):
    """El reparto del donante sigue al del cuerpo, hueso a hueso: en Goku la cadera y el cinturon (WAIST) van con el
    pantalon y en Vegeta con el torso; sin esto, al cambiar el pantalon quedaba un agujero en la cadera. La espinilla
    y el pie no (ahi decide el color: _botas_por_color)."""
    from collections import Counter  # noqa: PLC0415
    por = {}
    for i, z in enumerate(hz):
        if z in B3_ZONAS:
            por.setdefault(_clave_tri(host, i), Counter())[z] += 1
    mayor = {k: c.most_common(1)[0][0] for k, c in por.items()}
    out = list(dz)
    for i, z in enumerate(out):
        if z in B3_ZONAS:
            k = _clave_tri(donor, i)
            if k in mayor and not any(t in k for t in ("LEG2", "FOOT")):
                out[i] = mayor[k]
    return out


RETARGET_RIGIDO = True   # 25/10: la prenda va pegada a sus huesos tal cual (con la escala por hueso se estiraba hasta x2,5)


def _textura_donante_b3(donor, tri_idx, width, height):
    """Misma textura dominante/resolucion que la prueba de compatibilidad del JSON."""
    from collections import Counter  # noqa: PLC0415
    tex = Counter(donor.mats[donor.tri_mat[t]]["tex"] for t in tri_idx).most_common(1)[0][0]
    if tex == 0xFFFFFFFF or tex >= len(donor.tex):
        ms = [donor.mats[donor.tri_mat[t]] for t in tri_idx]
        c = np.mean([m["diffuse"] for m in ms], 0)
        if float(np.max(c)) < 0.02:
            c = np.array([0.5, 0.5, 0.5], np.float32)
        rgba = np.tile(np.append(c, 1.0), (height, width, 1))
        return (np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8)
    img = (np.clip(donor.tex[tex], 0, 1) * 255 + 0.5).astype(np.uint8)
    return _resize_rgba_b3(img, width, height)


def _resize_rgba_b3(img, width, height):
    """Redimensiona cada canal DXT3 por separado: alfa es banda toon, no transparencia premultiplicada."""
    if img.shape[1] == width and img.shape[0] == height:
        return np.asarray(img, np.uint8)
    chans = []
    for c in range(4):
        layer = Image.fromarray(np.asarray(img[..., c], np.uint8), "L")
        chans.append(np.asarray(layer.resize((width, height), Image.Resampling.LANCZOS)))
    return np.stack(chans, axis=2)


def _media_rampa(model, r):
    return np.asarray(model.tex[r][..., :3], np.float32).reshape(-1, 3).mean(0) if 0 <= r < len(model.tex) else None


def _filas_rampa(model, r):
    """Color de la rampa toon por banda (16 filas; columna clara, como _textura_cocida) o None."""
    if not 0 <= r < len(model.tex):
        return None
    ramp = np.asarray(model.tex[r], np.float32)
    rh, rw = ramp.shape[:2]
    yi = np.clip(((np.arange(16) + 0.5) * rh / 16).astype(int), 0, rh - 1)
    return ramp[yi, rw - 1, :3]


def _piel_rampa(model):
    from collections import Counter  # noqa: PLC0415
    c = Counter(m["shader"] for m in model.mats if m["shader"] < len(model.tex) and not _rampa_gris(model, m["shader"]))
    return c.most_common(1)[0][0] if c else None


def _textura_atlas_b3(donor, tris, mats, uv, width, height, rampa_host=None, piel_host=None, piel_tri=None):
    """Empaqueta todas las texturas/materiales del trozo; evita UV que apunten a la textura incorrecta (piernas negras).
    25/10: todo el trozo se pinta con la rampa toon del material anfitrion (`rampa_host`, color medio), y el toon RESTA
    la rampa: la piel de B3 es blanca con rampa azul (salian brazos blancos con la gris de la ropa). Se cuece en cada
    textura la diferencia: base' = base - rampa del donante + rampa del anfitrion."""
    keys, tri_keys = [], []
    for mi in mats:
        mat = donor.mats[int(mi)]
        tex = int(mat["tex"])
        key = (tex, tuple(np.asarray(mat["diffuse"]).tolist()) if tex >= len(donor.tex) else (), int(mat["shader"]))
        if key not in keys:
            keys.append(key)
        tri_keys.append(keys.index(key))
    if not keys:
        raise ValueError("la zona no tiene materiales")
    # 25/10 (QA Haiku: emblemas borrosos): antes, rejilla de celdas iguales (con 2 texturas, cada una a la mitad). Ahora
    # cada textura se RECORTA a la zona que usan de verdad sus triangulos y se empaqueta en estantes a la mayor escala
    # comun que quepa (sin pasar de 1:1)
    uv_out = np.asarray(uv, np.float64).copy()
    for i, mi in enumerate(mats):
        if donor.mats[int(mi)]["env"]:
            nrm = donor.nrm[np.asarray(tris[i], int)]
            uv_out[i, :, 0] = 0.5 + 0.5 * nrm[:, 0]
            uv_out[i, :, 1] = 0.5 - 0.5 * nrm[:, 1]
    # (UV fuera de 0..1 que repiten: se pliegan por triangulo; el triangulo entero se mueve, no cada esquina)
    for i in range(len(mats)):
        uv_out[i] -= np.floor(uv_out[i].min(0))
    uv_out = np.clip(uv_out, 0.0, 1.0)
    tri_keys = np.asarray(tri_keys)
    # 25/10 DS35-b: `piel_tri` (por triangulo) limita el cambio de rampa de piel a la PIEL del donante: antes cualquier
    # rampa de color (la del guante blanco de Vegeta, la armadura de C-16) se cocia con la rampa de piel del heroe y el
    # guante salia del color de la piel.
    piel_keys = None
    if piel_tri is not None:
        # solo las claves que son piel en TODOS sus triangulos: el material del guante de C-16 comparte clave con su piel
        # (misma textura/rampa) y con "alguno es piel" se cocia con la rampa de piel y el guante salia del tono de piel
        _pv = np.asarray(piel_tri, bool)
        piel_keys = {int(k) for k in np.unique(np.asarray(tri_keys)[_pv]) if np.all(_pv[np.asarray(tri_keys) == k])}
    fuentes, cajas = [], []
    for k, (tex, diffuse, shader) in enumerate(keys):
        if 0 <= tex < len(donor.tex):
            f = np.asarray(donor.tex[tex], np.float32).copy()
        else:
            c = np.clip(np.asarray(diffuse if diffuse else (0.65, 0.65, 0.65), np.float32), 0, 1)
            f = np.tile(np.append(c, 1.0)[None, None, :], (4, 4, 1)).astype(np.float32)
        # por BANDA (el alfa): base' = base - rampa_donante[banda] + rampa_anfitrion[banda]; la piel del donante (rampa de
        # color) se cuece con la rampa de la piel del HEROE: su tono
        rd = _filas_rampa(donor, shader)
        es_piel_k = True if piel_keys is None else (k in piel_keys)
        if piel_host is not None and rd is not None and not _rampa_gris(donor, shader) and es_piel_k:
            rd = piel_host
        if rampa_host is not None and rd is not None:
            band = np.clip(np.round(f[..., 3] * 15), 0, 15).astype(int)
            f[..., :3] = f[..., :3] - rd[band] + rampa_host[band]
        sh, sw = f.shape[:2]
        uvk = uv_out[tri_keys == k].reshape(-1, 2)
        x0 = int(np.clip(np.floor(uvk[:, 0].min() * sw) - 2, 0, sw - 1))
        x1 = int(np.clip(np.ceil(uvk[:, 0].max() * sw) + 2, x0 + 1, sw))
        y0 = int(np.clip(np.floor(uvk[:, 1].min() * sh) - 2, 0, sh - 1))
        y1 = int(np.clip(np.ceil(uvk[:, 1].max() * sh) + 2, y0 + 1, sh))
        fuentes.append(np.clip(f[y0:y1, x0:x1] * 255.0 + 0.5, 0, 255).astype(np.uint8))
        cajas.append((x0, y0, sw, sh))
    pad = 1

    def estantes(esc):
        orden = sorted(range(len(fuentes)), key=lambda k: -fuentes[k].shape[0])
        x = y = alto = 0
        sitio = {}
        for k in orden:
            w = max(2, int(fuentes[k].shape[1] * esc)) + 2 * pad
            h = max(2, int(fuentes[k].shape[0] * esc)) + 2 * pad
            if w > width or h > height:
                return None
            if x + w > width:
                x, y, alto = 0, y + alto, 0
            if y + h > height:
                return None
            sitio[k] = (x + pad, y + pad, w - 2 * pad, h - 2 * pad)
            x += w
            alto = max(alto, h)
        return sitio
    esc, sitio = 1.0, estantes(1.0)
    while sitio is None and esc > 0.05:
        esc *= 0.93
        sitio = estantes(esc)
    if sitio is None:
        raise ValueError("la textura anfitriona es demasiado pequeña para el atlas")
    atlas = np.zeros((height, width, 4), np.uint8)
    for k, (px, py, w, h) in sitio.items():
        atlas[py:py + h, px:px + w] = _resize_rgba_b3(fuentes[k], w, h)
    for i in range(len(mats)):
        k = int(tri_keys[i])
        px, py, w, h = sitio[k]
        x0, y0, sw, sh = cajas[k]
        cw, ch = fuentes[k].shape[1], fuentes[k].shape[0]
        u = (uv_out[i, :, 0] * sw - x0) / cw
        v = (uv_out[i, :, 1] * sh - y0) / ch
        uv_out[i, :, 0] = (px + u * w) / width
        uv_out[i, :, 1] = (py + v * h) / height
    return atlas, uv_out


def _cocer_textura_b3(model, shader, img, target, es_piel, piel_host):
    """Copia una textura y le cambia la rampa toon de origen por la del material destino (misma
    formula que `_textura_atlas_b3`). 27/10 DS157: sirve tambien para el brazo del HEROE que se
    queda en el draw de la manga: si el draw pasa a la rampa gris (la de la ropa), sus texeles de
    piel se corrigen para que se sigan viendo con el tono de piel."""
    f = np.asarray(img, np.float32).copy()
    rd = _filas_rampa(model, shader)
    if piel_host is not None and rd is not None and not _rampa_gris(model, shader) and es_piel:
        rd = piel_host
    if target is not None and rd is not None:
        band = np.clip(np.round(f[..., 3] * 15), 0, 15).astype(int)
        f[..., :3] = f[..., :3] - rd[band] + target[band]
    return f


def _atlas_guantes_b3(items, width, height):
    """Atlas UNICO para el injerto de guantes (27/10 DS157).

    `items`: lista de dicts {"key", "img" (HxWx4 float), "uv" (n,3,2)}. Las UVs se pliegan por
    triangulo, se recorta la zona util de cada clave (imagen) y se empaqueta en estantes a la
    mayor escala que quepa (sin pasar de 1:1). Devuelve (atlas uint8, [uv_nueva por item]).
    Todos los materiales que dibujan con este atlas comparten la MISMA rampa destino, por eso
    se pueden mezclar el brazo del heroe (ya corregido) y la manga/mano del donante."""
    if not items:
        raise ValueError("atlas de guantes sin piezas")
    uvs = []
    claves = {}
    for n, it in enumerate(items):
        uv = np.asarray(it["uv"], np.float64).copy()
        for i in range(len(uv)):
            uv[i] -= np.floor(uv[i].min(0))
        uv = np.clip(uv, 0.0, 1.0)
        uvs.append(uv)
        claves.setdefault(it["key"], []).append(n)
    fuentes, cajas, sitio = {}, {}, {}
    for key, ns in claves.items():
        img = np.asarray(items[ns[0]]["img"], np.float32)
        sh, sw = img.shape[:2]
        uvk = np.concatenate([uvs[n].reshape(-1, 2) for n in ns])
        x0 = int(np.clip(np.floor(uvk[:, 0].min() * sw) - 2, 0, sw - 1))
        x1 = int(np.clip(np.ceil(uvk[:, 0].max() * sw) + 2, x0 + 1, sw))
        y0 = int(np.clip(np.floor(uvk[:, 1].min() * sh) - 2, 0, sh - 1))
        y1 = int(np.clip(np.ceil(uvk[:, 1].max() * sh) + 2, y0 + 1, sh))
        fuentes[key] = np.clip(img[y0:y1, x0:x1] * 255.0 + 0.5, 0, 255).astype(np.uint8)
        cajas[key] = (x0, y0, sw, sh)
    pad = 1

    def estantes(esc):
        orden = sorted(fuentes, key=lambda k: -fuentes[k].shape[0])
        x = y = alto = 0
        out = {}
        for k in orden:
            w = max(2, int(fuentes[k].shape[1] * esc)) + 2 * pad
            h = max(2, int(fuentes[k].shape[0] * esc)) + 2 * pad
            if w > width or h > height:
                return None
            if x + w > width:
                x, y, alto = 0, y + alto, 0
            if y + h > height:
                return None
            out[k] = (x + pad, y + pad, w - 2 * pad, h - 2 * pad)
            x += w
            alto = max(alto, h)
        return out

    esc = 1.0
    sitio = estantes(esc)
    while sitio is None and esc > 0.05:
        esc *= 0.93
        sitio = estantes(esc)
    if sitio is None:
        raise ValueError("la textura destino es demasiado pequena para el atlas de guantes")
    atlas = np.zeros((height, width, 4), np.uint8)
    for k, (px, py, w, h) in sitio.items():
        atlas[py:py + h, px:px + w] = _resize_rgba_b3(fuentes[k], w, h)
    out = []
    for n, it in enumerate(items):
        px, py, w, h = sitio[it["key"]]
        x0, y0, sw, sh = cajas[it["key"]]
        cw, ch = fuentes[it["key"]].shape[1], fuentes[it["key"]].shape[0]
        u = (uvs[n][..., 0] * sw - x0) / cw
        v = (uvs[n][..., 1] * sh - y0) / ch
        uv2 = np.empty_like(uvs[n])
        uv2[..., 0] = (px + u * w) / width
        uv2[..., 1] = (py + v * h) / height
        out.append(uv2)
    return atlas, out


def _ajustar_zona_b3(P, N, usado, base, zona):
    """Adapta el trozo al bbox de la zona 264 y lo ancla arriba en cadera/tobillo."""
    z = B3_ZONAS[zona]
    host_tri = np.flatnonzero(np.asarray(_zonas_pieza_b3(base)) == zona)
    if not len(host_tri):
        raise ValueError("el cuerpo base no tiene la zona %s" % zona)
    host_v = np.unique(base.tris[host_tri].reshape(-1))
    hp, _ = base.posed()
    src = np.asarray(P[usado], np.float64)
    nrm = np.asarray(N[usado], np.float64)
    lo_s, hi_s = src.min(0), src.max(0)
    lo_h, hi_h = hp[host_v].min(0), hp[host_v].max(0)
    ext_s = np.maximum(hi_s - lo_s, 1e-6)
    ext_h = np.maximum(hi_h - lo_h, 1e-6)
    # La prenda queda centrada en el eje del cuerpo; torso se ancla hombro/cintura,
    # extremidades se anclan cadera/tobillo o muñeca, con la caja medida del fid 264.
    scale = ext_h / ext_s
    center_s, center_h = (lo_s + hi_s) * 0.5, (lo_h + hi_h) * 0.5
    if zona in ("piernas", "botas"):
        # Las piernas se anclan por la cadera (borde superior); las botas por el
        # tobillo (borde superior de la bota). Igualar también el borde inferior
        # de cada zona al del cuerpo evita pantalones cortos y huecos de tobillo.
        anchor_s = np.array([center_s[0], hi_s[1], center_s[2]])
        anchor_h = np.array([center_h[0], hi_h[1], center_h[2]])
        out = (src - anchor_s) * scale + anchor_h
    else:
        out = (src - center_s) * scale + center_h
    # Normales bajo escala no uniforme: n' = S^-T n.
    nrm = nrm / scale[None, :]
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1), 1e-9)[:, None]
    return out, nrm, {"scale_xyz": [round(float(x), 4) for x in scale],
                      "donor_bbox": [lo_s.tolist(), hi_s.tolist()], "base_bbox": [lo_h.tolist(), hi_h.tolist()]}


EMPALME = {"botas": "arriba", "piernas": "todo"}   # borde que se lleva al de la pieza del cuerpo
FIJO = 0.35   # fraccion del alto de la pieza que no se mueve (el pie de la bota, la cintura del pantalon)


def _empalmar_alto(Q, host, hz, zona):
    """25/10: la pieza nueva llega justo hasta donde llegaba la del cuerpo. Las botas de Vegeta son mas cortas que las de
    Goku y dentro de las de Goku no hay pierna: quedaba la espinilla al aire. Se estira (o encoge) en vertical solo la
    parte de la caña (botas) o el bajo (pantalon); el pie y la cintura no se mueven."""
    lado = EMPALME.get(zona)
    if not lado:
        return Q
    hp, _ = host.posed()
    idx = [i for i, z in enumerate(hz) if z == zona]
    if not idx or not len(Q):
        return Q
    hv = np.unique(host.tris[idx].reshape(-1))
    hlo, hhi = float(hp[hv, 1].min()), float(hp[hv, 1].max())
    Q = np.asarray(Q, float).copy()
    lo, hi = float(Q[:, 1].min()), float(Q[:, 1].max())
    if hi - lo < 1e-6:
        return Q
    y = Q[:, 1]
    if lado == "arriba":
        a = lo + FIJO * (hi - lo)
        m = y > a
        Q[m, 1] = a + (y[m] - a) * (hhi - a) / max(hi - a, 1e-6)
    elif lado == "todo":   # de la cintura al tobillo de la pieza del cuerpo (sin cinturon, el de Vegeta dejaba hueco)
        Q[:, 1] = hlo + (y - lo) * (hhi - hlo) / (hi - lo)
    else:
        a = hi - FIJO * (hi - lo)
        m = y < a
        Q[m, 1] = a - (a - y[m]) * (a - hlo) / max(a - lo, 1e-6)
    return Q


SOLDAR = 0.035   # radio (fraccion del alto del cuerpo) para pegar el borde de la prenda al de la pieza vieja


def _bordes(tris, pos):
    """Aristas abiertas (usadas por un solo triangulo) soldando vertices por posicion: lista de (i, j) en `pos`."""
    import collections  # noqa: PLC0415
    key = np.round(pos, 3)
    _, vi = np.unique(key, axis=0, return_inverse=True)
    vi = vi.ravel()
    rep = {}
    for i, k in enumerate(vi):
        rep.setdefault(int(k), i)
    c = collections.Counter()
    for t in tris:
        a, b, d = (int(vi[x]) for x in t)
        for e in ((a, b), (b, d), (d, a)):
            if e[0] != e[1]:
                c[tuple(sorted(e))] += 1
    return [(rep[a], rep[b]) for (a, b), n in c.items() if n == 1], vi


def _soldar_bordes(Q, Qd, tris, host, hz, zona, conservar=None, objetivo=None, y_max=None):
    """25/10 (usuario: huecos en cuello, codos, cintura y tobillos): cada vertice del borde abierto de la prenda que
    queda a menos de SOLDAR del borde de la pieza que sustituye (en el cuerpo) se pega a ese borde (al punto mas
    cercano del segmento), y sus vecinos se mueven la mitad. Asi la prenda acaba donde acababa la vieja y no queda
    rendija con el cuello, los antebrazos o las botas. Q: vertices colocados; Qd: los del donante (para soldar por
    posicion); tris: triangulos de la prenda en indices de Q."""
    hp, _ = host.posed()
    quitar = np.asarray([z == zona for z in hz], bool) if objetivo is None else np.asarray(objetivo, bool)
    if conservar is not None and objetivo is None:
        quitar &= ~conservar
    if not quitar.any():
        return Q
    bh, _ = _bordes(host.tris[quitar], hp)
    if y_max is not None:   # (solo el borde de abajo del objetivo: la base del cuello, no su union con la cabeza)
        bh = [(a, b) for a, b in bh if max(hp[a, 1], hp[b, 1]) < y_max]
    if conservar is not None and conservar.any() and bh and objetivo is None:
        # los bordes que dan a la piel que se queda no son hueco: debajo esta el brazo/cuello del heroe (el borde de la
        # manga del gi de Goku va suelto sobre el brazo: se mira por distancia, no por vertices compartidos)
        from scipy.spatial import cKDTree  # noqa: PLC0415
        tv = host.tris[conservar]
        arbol = cKDTree(np.concatenate([hp[tv].mean(1), hp[np.unique(tv.reshape(-1))]]))
        Hh = float(hp[:, 1].max() - hp[:, 1].min())
        da, _ = arbol.query(hp[[a for a, b in bh]])
        db, _ = arbol.query(hp[[b for a, b in bh]])
        bh = [e for e, x, y in zip(bh, da, db) if min(x, y) > 0.06 * Hh]   # (dobladillos de manga sobre el brazo: fuera)
    if not bh:
        return Q
    A = np.array([hp[a] for a, b in bh])
    B = np.array([hp[b] for a, b in bh])
    bd, vi = _bordes(tris, Qd)
    if not bd:
        return Q
    H = float(hp[:, 1].max() - hp[:, 1].min())
    Q = np.asarray(Q, float).copy()
    vb = sorted({v for e in bd for v in e})
    X = Q[vb]
    AB = B - A
    t = np.clip(np.einsum("nij,ij->ni", X[:, None, :] - A[None], AB) / np.maximum((AB * AB).sum(1), 1e-12)[None], 0, 1)
    proj = A[None] + t[..., None] * AB[None]
    dist = np.linalg.norm(proj - X[:, None, :], axis=2)
    j = dist.argmin(1)
    cerca = dist[np.arange(len(vb)), j] < SOLDAR * H
    mov = np.zeros_like(Q)
    for k, v in enumerate(vb):
        if cerca[k]:
            mov[vi == vi[v]] = proj[k, j[k]] - Q[v]   # todas las copias del vertice (costuras de UV)
    # vecinos (un anillo) a medias, para no estirar un solo triangulo
    vec = np.zeros_like(Q)
    cuenta = np.zeros(len(Q))
    movido = np.linalg.norm(mov, axis=1) > 0
    for tri in tris:
        for a in tri:
            for b in tri:
                if a != b and movido[b] and not movido[a]:
                    vec[a] += mov[b] * 0.5
                    cuenta[a] += 1
    vec[cuenta > 0] /= cuenta[cuenta > 0][:, None]
    return Q + mov + vec


def _es_piel(model, cache=None):
    """Por triangulo: True si su material es de PIEL (rampa de color: en B3 la piel es blanca con rampa azul; la ropa
    lleva rampas grises)."""
    cache = {} if cache is None else cache
    out = np.zeros(len(model.tris), bool)
    for i in range(len(model.tris)):
        mi = int(model.tri_mat[i])
        if mi not in cache:
            r = model.mats[mi]["shader"]
            cache[mi] = r < len(model.tex) and not _rampa_gris(model, r)
        out[i] = cache[mi]
    return out


ESCALA_UNICA = False   # (medido 25/10: abre la axila de Vegeta 420)   # una sola escala (mediana de los huesos) para toda la prenda: sin muescas entre huesos
HUESO_DEL_CUERPO = True
ANTEBRAZO_PRENDA = True   # el torso trae el brazo entero del donante (hasta la muneca)
CORTE_CUELLO = 0.5   # (fraccion del alto del cuello de Goku) por debajo, el cuello de piel es el del donante
CONSERVAR_PIEL_HEROE = False   # True: la piel de brazos de Goku se queda bajo la prenda (pestanas en los hombros)
CUBRE_TELA = 0.025   # la piel del heroe a menos de esto (fraccion del alto) de la tela del donante esta debajo de ella
GEMELOS_COMP, GEMELOS_MOV = False, False   # (medido 25/10: el pico de 420 no era de gemelos; con esto 416 empeoraba)
INFLAR = 0.006   # holgura de la tela sobre la piel del heroe (fraccion del alto del cuerpo)


def _inflar(Q, tris, ref_P, ref_N, H, dentro=False, sueltas_fijas=True, ref_borde=None):
    """25/10: la tela del donante envuelve la piel que el heroe conserva (brazos, cuello): cada vertice que queda dentro
    o pegado a esa piel sale por su normal hasta INFLAR de holgura (como un shrinkwrap "por fuera" con desplazamiento),
    y el desplazamiento se suaviza con los vecinos para no hacer picos. Sin esto, los brazos de Goku (mas gruesos que
    los de Vegeta) atravesaban las mangas."""
    from scipy.spatial import cKDTree  # noqa: PLC0415
    if not len(ref_P) or not len(Q):
        return Q
    Q = np.asarray(Q, float).copy()
    d, k = cKDTree(ref_P).query(Q)
    n = ref_N[k]
    sdist = np.einsum("ij,ij->i", Q - ref_P[k], n)
    off = INFLAR * H
    # solo la tela que esta SOBRE la piel (cerca y por encima o atravesandola), con tope: las piezas rigidas de al lado
    # (hombreras, correas de la armadura) no se tocan
    # (encima o dentro: el vertice esta en el eje de la normal, no al lado; lo de al lado son hombreras y correas)
    eje = np.abs(sdist) > 0.6 * d
    if ref_borde is not None:
        # 25/10 (medido en Vegeta 420: el cuello del heroe empujaba 0,035 del alto hacia fuera la tela de los HOMBROS, que
        # esta por debajo de el = el "pico"): si lo mas cercano es el borde abierto del molde, el punto esta mas alla del
        # molde, no encima: no se mueve
        eje &= ~np.asarray(ref_borde, bool)[k]   # (probado 25/10 empujar a la altura del borde: 420 pasaba de 4 a 547 px de hueco)
    if dentro:   # capa de seguridad: por debajo de la piel del heroe (lo que asome por fuera, adentro)
        mov = np.where((d < 0.06 * H) & (sdist > -off) & eje, -np.minimum(off + sdist, 0.05 * H), 0.0)[:, None] * n
    else:
        mov = np.where((d < 0.06 * H) & (sdist < off) & eje, np.minimum(off - sdist, 0.035 * H), 0.0)[:, None] * n
    vec = [set() for _ in range(len(Q))]
    for t in tris:
        for a in t:
            vec[a].update(int(b) for b in t if b != a)
    # 25/10 (Haiku: las hombreras de Bardock se plegaban: 43 de 69 vertices salian y 26 no): cada pieza SUELTA pequena
    # (hombrera, hebilla) no se mueve: con el mayor desplazamiento de sus vertices quedaba flotando; ya va por encima del
    # hombro en su personaje y el retarget rigido la deja igual sobre el del heroe
    # 25/10 (medido en Vegeta 420: costuras abiertas de hasta 0,035 del alto en el hombro = el "pico"): los vertices
    # duplicados por las costuras de UV estan en el mismo sitio pero en triangulos distintos; sin soldarlos aqui, uno se
    # movia y su gemelo no. Se cuentan como vecinos (componentes y suavizado) y al final se mueven igual
    _, gem = np.unique(np.round(Q, 4), axis=0, return_inverse=True)
    gem = gem.ravel()
    por_pos = {}
    for i, g in enumerate(gem):
        por_pos.setdefault(int(g), []).append(i)
    if GEMELOS_COMP:
        for ids in por_pos.values():
            if len(ids) > 1:
                for a_ in ids:
                    vec[a_].update(b_ for b_ in ids if b_ != a_)
    comp = np.full(len(Q), -1)
    usados = sorted({int(v) for t in tris for v in t})
    for s0 in usados:
        if comp[s0] >= 0:
            continue
        pila, comp[s0] = [s0], s0
        while pila:
            a = pila.pop()
            for b in vec[a]:
                if comp[b] < 0:
                    comp[b] = s0
                    pila.append(b)
    ids, cuenta = np.unique(comp[comp >= 0], return_counts=True)
    for c, n in zip(ids, cuenta):
        if sueltas_fijas and n <= 0.25 * cuenta.max() and n < len(usados):
            m = comp == c
            mov[m] = 0.0
            for i in np.flatnonzero(m):
                vec[i] = set()   # sin suavizado: rigida
    suave = mov.copy()
    for _ in range(2):
        nuevo = suave.copy()
        for i, vs in enumerate(vec):
            if vs:
                m = np.mean([suave[j] for j in vs], 0)
                nuevo[i] = np.where(np.linalg.norm(suave[i]) >= np.linalg.norm(m), suave[i], 0.5 * (suave[i] + m))
        suave = nuevo
    for ids in por_pos.values():   # gemelos: el mismo desplazamiento (el mayor)
        if GEMELOS_MOV and len(ids) > 1:
            k = ids[int(np.argmax(np.linalg.norm(suave[ids], axis=1)))]
            suave[ids] = suave[k]
    return Q + suave


CONFORMAR_HOLGURA = 0.004   # holgura (fraccion del alto) de la prenda del antebrazo sobre el molde del heroe
CAIDA_FALDON = 55.0   # grados que caen los faldones de las armaduras desde su postura horizontal del modelo


def _base_perp(e):
    """Base (u, v) perpendicular al eje `e` (para medir el angulo alrededor del brazo)."""
    a = np.array([0.0, 0.0, 1.0])
    if abs(float(e @ a)) > 0.9:
        a = np.array([1.0, 0.0, 0.0])
    u = np.cross(e, a)
    u = u / max(float(np.linalg.norm(u)), 1e-9)
    return u, np.cross(e, u)


def _manga_larga(Q, sel, P_don, P_sup, Wd, Wh, a2d, hdd, a2h, hdh, molde_v, hp_, H):
    """25/10 DS35-b (coordinador: "inflar sobre el molde y soldar el borde"): si la prenda del antebrazo es un guante
    o una manga larga, se estira SOLO a lo largo del brazo, anclada en la muneca, hasta el codo del heroe (su antebrazo
    es mas largo). Una venda (franja corta, < 60 %) no se toca. 26/10 DS74: una mana ENTERA (>= 70 %: 75/76/80/424) se
    lleva a TODO el antebrazo del heroe; si no, la fraccion dejaba una franja de piel entre la manga y el hombro.
    Devuelve (Q, larga): `larga` = manga entera (>= 70 %); DS82 lo usa para recortar la piel tapada por el aro."""
    if not len(sel) or not len(molde_v):
        return Q, False
    Q = np.asarray(Q, float).copy()
    od = np.asarray(Wd[a2d, :3, 3], float)
    ed = np.asarray(Wd[hdd, :3, 3], float) - od
    Ld = float(np.linalg.norm(ed))
    ed = ed / max(Ld, 1e-9)
    oh = np.asarray(Wh[a2h, :3, 3], float)
    eh = np.asarray(Wh[hdh, :3, 3], float) - oh
    Lh = float(np.linalg.norm(eh))
    eh = eh / max(Lh, 1e-9)
    sup_t = (np.asarray(P_sup, float) - od) @ ed
    gar_t = (np.asarray(P_don, float)[sel] - od) @ ed
    if np.ptp(sup_t) < 1e-6 or np.ptp(gar_t) < 0.6 * np.ptp(sup_t):
        return Q, False     # venda/franja: se queda donde el retarget la pone
    # 25/10 DS35-b: la fraccion que la prenda ocupa del antebrazo del donante se lleva a la MISMA fraccion del
    # antebrazo del heroe (su piel, de codo a muneca). Asi un guante largo llega al codo y una manga corta se queda
    # corta, como en el original.
    # 26/10 DS74: una MANGA ENTERA (>= 70 % del antebrazo del donante: 75/76/80/424) se estira a TODO el antebrazo
    # del heroe. Con la fraccion se quedaba corta arriba y entre la manga y el hombro asomaba una franja de piel del
    # heroe donde el original tiene tela (medido: 0,77-0,86 de la manga frente a 1,0 del molde).
    th_sup = (np.asarray(hp_[molde_v], float) - oh) @ eh
    h0, h1 = float(th_sup.min()), float(th_sup.max())
    d0, d1 = float(sup_t.min()), float(sup_t.max())
    if h1 - h0 < 1e-6 or d1 - d0 < 1e-6:
        return Q, False
    larga = np.ptp(gar_t) >= 0.7 * np.ptp(sup_t)
    g0, g1 = float(gar_t.min()), float(gar_t.max())
    if larga and g1 - g0 > 1e-6:
        d0, d1 = g0, g1
    t = (Q[sel] - oh) @ eh
    r = (np.clip(gar_t, d0, d1) - d0) / (d1 - d0)
    Q[sel] += ((h0 + r * (h1 - h0)) - t)[:, None] * eh
    return Q, bool(larga)


def _conformar_molde(Q, tris, ref_P, ref_N, H, eje=None, holgura=CONFORMAR_HOLGURA, alcance=0.06):
    """25/10 DS35-b (coordinador: "cerrar el codo inflando sobre el molde"): la manga/guante/venda del donante se ciñe
    al antebrazo del HEROE, que suele ser mas grueso (la de Vegeta quedaba entera dentro y solo asomaba la mano). Cada
    vertice de la prenda que queda por dentro o pegado a esa piel sale por su normal hasta `holgura` y el movimiento se
    suaviza con los vecinos (como _inflar), pero sin exigir que el vertice este en el eje de la normal: una manga RODEA
    el brazo, asi que casi todos sus vertices estan de lado y _inflar los dejaba quietos (medido: 18-44 % seguian
    dentro). Solo actua a menos de `alcance` del molde (el dorso de la mano y lo que cuelga del codo no se tocan)."""
    from scipy.spatial import cKDTree  # noqa: PLC0415
    if not len(ref_P) or not len(Q):
        return Q
    Q = np.asarray(Q, float).copy()
    d, k = cKDTree(ref_P).query(Q)
    n = np.asarray(ref_N, float)[k]
    if eje is not None:
        # 25/10 DS35-b: solo la componente RADIAL de la normal. Con la normal entera, los vertices que caen mas alla
        # de la muneca se empujaban por el eje del brazo hacia el codo y la manga se COMPRIMIA (medido: 2,03 -> 1,00).
        e_ = np.asarray(eje, float)
        e_ = e_ / max(float(np.linalg.norm(e_)), 1e-9)
        n = n - (n @ e_)[:, None] * e_[None, :]
        n = n / np.maximum(np.linalg.norm(n, axis=1), 1e-9)[:, None]
    sdist = np.einsum("ij,ij->i", Q - ref_P[k], n)
    off = holgura * H
    mov = np.where((d < alcance * H) & (sdist < off), np.minimum(off - sdist, 0.05 * H), 0.0)[:, None] * n
    vec = [set() for _ in range(len(Q))]
    for t in tris:
        for a in t:
            vec[a].update(int(b) for b in t if b != a)
    suave = mov.copy()
    for _ in range(2):
        nuevo = suave.copy()
        for i, vs in enumerate(vec):
            if vs:
                m = np.mean([suave[j] for j in vs], 0)
                nuevo[i] = np.where(np.linalg.norm(suave[i]) >= np.linalg.norm(m), suave[i], 0.5 * (suave[i] + m))
        suave = nuevo
    return Q + suave


def _giro_eje(eje, ang):
    """Matriz de giro (Rodrigues) de `ang` radianes alrededor de `eje`."""
    k = np.asarray(eje, float) / max(np.linalg.norm(eje), 1e-12)
    K = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + np.sin(ang) * K + (1 - np.cos(ang)) * K @ K


def _piezas_rigidas(Q, QN, Pd, Nd, tris, bones_src, bones_dst, donor, host):
    """25/10 (hombreras de Bardock arrugadas): cada pieza SUELTA de la prenda (componente conexa que no es la principal:
    hombreras, cinturones, colgantes) se coloca entera con el cambio de su hueso dominante, como un solido. Con un cambio
    por hueso, los trozos de una misma hombrera colgados de la clavicula, el hombro y el brazo (orientados distinto en cada
    personaje) se separaban y la pieza se arrugaba. El reparto de huesos para la animacion no cambia."""
    n = len(Q)
    padre = list(range(n))

    def raiz(a):
        while padre[a] != a:
            padre[a] = padre[padre[a]]
            a = padre[a]
        return a
    for t in tris:
        for a, b in ((t[0], t[1]), (t[1], t[2])):
            ra, rb = raiz(int(a)), raiz(int(b))
            if ra != rb:
                padre[ra] = rb
    # (soldadas por posicion: las costuras de UV parten la malla en trozos que no son piezas)
    clave = {}
    for i, k in enumerate(map(tuple, np.round(Pd, 3))):
        if k in clave:
            ra, rb = raiz(i), raiz(clave[k])
            if ra != rb:
                padre[ra] = rb
        else:
            clave[k] = i
    comp = np.array([raiz(i) for i in range(n)])
    ids, cuenta = np.unique(comp, return_counts=True)
    if len(ids) < 2:
        return Q, QN
    principal = ids[cuenta.argmax()]
    src_world, dst_world = np.asarray(donor.worlds()), np.asarray(host.worlds())
    Q, QN = np.asarray(Q, float).copy(), np.asarray(QN, float).copy()
    bs, bd = np.asarray(bones_src, int), np.asarray(bones_dst, int)
    for c in ids:
        m = comp == c
        # solo piezas PEQUENAS (hombreras, hebillas): una bota o una pernera enteras siguen su esqueleto por hueso
        if c == principal or m.sum() > 0.25 * cuenta.max() or len(set(zip(bs[m].tolist(), bd[m].tolist()))) < 2:
            continue
        pares, votos = np.unique(np.stack([bs[m], bd[m]], 1), axis=0, return_counts=True)
        sb, tb = (int(x) for x in pares[votos.argmax()])
        ls, lt = _largo_hueso(donor, src_world, sb), _largo_hueso(host, dst_world, tb)
        k = float(np.clip(lt / ls, 0.7, 1.4)) if ls and lt else 1.0
        inv = np.linalg.inv(src_world[sb])
        local = Pd[m] @ inv[:3, :3].T + inv[:3, 3]
        Q[m] = (local * k) @ dst_world[tb, :3, :3].T + dst_world[tb, :3, 3]
        rot = dst_world[tb, :3, :3] @ inv[:3, :3]
        nn = Nd[m] @ np.linalg.inv(rot * k)
        QN[m] = nn / np.maximum(np.linalg.norm(nn, axis=1), 1e-9)[:, None]
    return Q, QN


def _largo_hueso(model, W, b):
    """Distancia del hueso a su hijo mas lejano (el largo del segmento); None si no tiene hijos."""
    hijos = [i for i, p in enumerate(model.parent) if p == b and i != b]
    if not hijos:
        return None
    d = max(float(np.linalg.norm(W[i, :3, 3] - W[b, :3, 3])) for i in hijos)
    return d if d > 1e-6 else None


def _retarget_zona_huesos_b3(P, N, usado, donor, base, zona, bones_src, bones_dst, unica=None):
    """Retarget por hueso equivalente: conserva cada pivote y ajusta la forma local de la zona.

    25/10 DS35: `unica` fuerza una escala uniforme (la usa la zona guantes: con la escala por hueso el guante del donante
    encogia 0,73 y quedaba DENTRO del antebrazo del heroe; solo asomaba la mano)."""
    zone_tris = np.flatnonzero(np.asarray(_zonas_pieza_b3(base)) == zona)
    if not len(zone_tris):
        raise ValueError("el cuerpo base no tiene la zona %s" % zona)
    host_v = np.unique(base.tris[zone_tris].reshape(-1))
    src_world = np.asarray(donor.worlds())
    dst_world = np.asarray(base.worlds())
    host_pos, _ = base.posed()
    host_local = {}
    for b in np.unique(base.vbone[host_v]):
        b = int(b)
        inv = np.linalg.inv(dst_world[b])
        verts = host_v[base.vbone[host_v] == b]
        host_local[b] = host_pos[verts] @ inv[:3, :3].T + inv[:3, 3]

    points, normals, scales = np.asarray(P, float), np.asarray(N, float), {}
    src_bones = np.asarray(bones_src, int)
    dst_bones = np.asarray(bones_dst, int)
    out, nrm = np.zeros_like(points), np.zeros_like(normals)
    unica = None if unica is None else float(unica)
    if unica is None and RETARGET_RIGIDO and ESCALA_UNICA:
        # 25/10 (usuario: "los hombros siguen fatal"; medido en Vegeta 420: CHN/ARM1 a 1,14 y ARMROT a 1,0, porque su hueso
        # casi no tiene largo): con una escala por hueso, el trozo del hombro quedaba mas pequeno que sus vecinos y salia una
        # muesca en la postura de referencia y un pico al bajar los brazos. Una sola escala para toda la prenda: la mediana
        # de los huesos con largo
        rs = []
        for sb, tb in set(zip(src_bones.tolist(), dst_bones.tolist())):
            ls, lt = _largo_hueso(donor, src_world, sb), _largo_hueso(base, dst_world, tb)
            if ls and lt:
                rs.append(lt / ls)
        if rs:
            unica = float(np.clip(np.median(rs), 0.7, 1.4))
    for sb, tb in sorted(set(zip(src_bones.tolist(), dst_bones.tolist()))):
        mask = (src_bones == sb) & (dst_bones == tb)
        inv_src = np.linalg.inv(src_world[sb])
        local = points[mask] @ inv_src[:3, :3].T + inv_src[:3, 3]
        targets = host_local.get(tb)
        if targets is not None and len(targets) >= 3:
            src_ext = np.maximum(local.max(0) - local.min(0), 1e-6)
            dst_ext = np.maximum(targets.max(0) - targets.min(0), 1e-6)
            scale = dst_ext / src_ext
        else:
            scale = np.ones(3)
        if RETARGET_RIGIDO and unica is not None:
            scale = np.full(3, unica)
        elif RETARGET_RIGIDO:   # escala uniforme por hueso: largo del hueso en el cuerpo / en el donante (sin deformar)
            ls, lt = _largo_hueso(donor, src_world, sb), _largo_hueso(base, dst_world, tb)
            scale = np.full(3, float(np.clip(lt / ls, 0.7, 1.4)) if ls and lt else 1.0)
        else:
            scale = np.clip(scale, 0.5, 2.5)
        transform = dst_world[tb, :3, :3] @ np.diag(scale) @ inv_src[:3, :3]
        out[mask] = (local * scale) @ dst_world[tb, :3, :3].T + dst_world[tb, :3, 3]
        nrm[mask] = normals[mask] @ np.linalg.inv(transform)
        scales[base.labels[tb]] = [round(float(x), 4) for x in scale]
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1), 1e-9)[:, None]
    return out, nrm, {"metodo": "retarget por hueso equivalente", "escala_huesos": scales}


def _rampa_piel(model):
    """Color medio de la rampa de la piel del cuerpo (la de color mas usada; en B3 la piel es blanca y su rampa azul)."""
    from collections import Counter  # noqa: PLC0415
    c = Counter(m["shader"] for m in model.mats if m["shader"] < len(model.tex) and not _rampa_gris(model, m["shader"]))
    return _media_rampa(model, c.most_common(1)[0][0]) if c else None


def _rampa_gris(model, r):
    tx = model.tex[r] if 0 <= r < len(model.tex) else None
    if tx is None:
        return False
    rgb = np.asarray(tx[..., :3], float)
    return float((rgb.max(-1) - rgb.min(-1)).mean()) < 0.06


def _texturas_usadas_b3(amb):
    """Indices de textura que usa ALGUN material de ALGUN AWG del #AMB (no solo el cuerpo: las manos y las caras de los
    AWG auxiliares tambien dibujan). 25/10 DS35: sin esto se puede creer libre una textura de una variante de cara."""
    import struct  # noqa: PLC0415
    b = bytes(amb)
    awo = 0x40
    be = lambda o: struct.unpack(">I", b[o:o + 4])[0]  # noqa: E731
    n_awg, tbl = be(awo + 0x18), awo + be(awo + 0x1C)
    usadas = set()
    for k in range(n_awg):
        a = awo + be(tbl + 4 * k)
        n_mat, mt = be(a + 0x24), a + be(a + 0x20)
        for i in range(min(n_mat, 256)):
            usadas.add(be(mt + i * 0x50 + 0x30))
            usadas.add(be(mt + i * 0x50 + 0x34))   # rampa toon
    return usadas


def _textura_libre_b3(amb):
    """(indice, ancho, alto) de la textura MAS GRANDE del #AZT que ningun material referencia.

    25/10 DS35 (guantes): en el cuerpo 264 el #AZT trae 19 texturas y solo 13 se usan; las libres son 1, 3, 6, 8, 16
    (64x64) y **15 (256x256, DXT3 65664 B = 128 + 256*256, sin mips)**. Escribir el atlas del guante en una textura libre
    no toca la tabla de texturas (#AZT: su indice esta pegado a las cabeceras y no hay hueco para una entrada mas), ni la
    de draws, ni el tope de ventanas: solo reapunta el material del anfitrion. Las texturas 5/9/10 NO valen como destino
    (son las que repintan las piezas de piernas/torso/botas) y 9/5/10/13 tampoco: hay que dejar intacto lo aprobado."""
    import texture_b3 as tb  # noqa: PLC0415
    _, texs = tb.parse_azt(bytes(amb))
    usadas = _texturas_usadas_b3(amb)
    libres = [t for t in texs if t["idx"] not in usadas and t["size"] - 128 == t["w"] * t["h"]
              and t["w"] >= 64 and t["h"] >= 64]
    if not libres:
        return None
    t = max(libres, key=lambda x: x["w"] * x["h"])
    return t["idx"], t["w"], t["h"]


def _es_mano_rigida_b3(host, d):
    """True si el draw es una lista rigida de la MANO (prim 4, group_bone = un hueso L00_?HAND)."""
    return d["prim"] == 4 and "HAND" in host.labels[d["group_bone"]]


def _anfitrion_guantes_b3(host, por_draw, hz):
    """Anfitrion de la zona 'guantes' cuando ninguna textura del cuerpo es solo de esa zona.

    25/10 DS35 (medido en 264): la unica textura exclusiva de la zona es la 13 (256x128), y sus dos draws son LISTAS
    RIGIDAS (prim 4, group_bone GOK_L00_RHAND/LHAND): no admiten huesos por ventana y ademas comparten material (11).
    Las demas (tex 2 y 7) las usan el torso/brazo y las botas, asi que repintarlas rompe lo aprobado. Se usa el draw de la
    mano derecha y se PASA A TIRA (prim 5): asi el draw admite los huesos de la pieza (mano + antebrazo, 3 huesos por
    lado) y su material (11, exclusivo de las manos) pasa a dibujar con una textura LIBRE del #AZT (atlas del guante).
    Las variantes de mano (AWG 1..15) siguen con la textura 13 intacta.
    Devuelve (draw, tex_destino, w, h, prim_destino)."""
    cands = []
    for draw, indices in sorted(por_draw.items()):
        d = host.table.draws[draw]
        nz = sum(1 for i in indices if hz[i] == "guantes")
        if nz and _es_mano_rigida_b3(host, d):
            cands.append((nz, draw))
    if not cands:
        raise ValueError("sin draw de mano para la zona guantes en el cuerpo")
    cands.sort(reverse=True)
    libre = _textura_libre_b3(host.data)
    if libre is None:
        raise ValueError("el cuerpo no tiene ninguna textura libre para el atlas del guante")
    idx, w, h = libre
    return cands[0][1], idx, w, h, 5


def _draws_guantes_b3(host, por_draw, hz):
    """27/10 DS157: draws por los que TIENE que salir un guante/manga.

    La mano del AWG0 (GOK_L00_?HAND) no se dibuja en combate (DS155): el juego usa las variantes
    (AWG L01/L02/... con el puno). Hay que separar la prenda:
      * manga/antebrazo (ARM2) -> el draw del ANTEBRAZO del cuerpo (siempre dibujado),
      * mano/guante -> el draw L00 de la mano de cada lado (para la vista previa) y todas las
        variantes de mano (para el combate).
    Devuelve ({lado: draw de mano}, draw del antebrazo o None)."""
    manos = {}
    for draw, idxs in por_draw.items():
        d = host.table.draws[draw]
        if d["prim"] != 4 or d.get("group_bone", 0) >= len(host.labels):
            continue
        lab = host.labels[d["group_bone"]]
        lado = "R" if lab.endswith("RHAND") else "L" if lab.endswith("LHAND") else None
        if not lado:
            continue
        nz = sum(1 for i in idxs if hz[i] == "guantes")
        if nz and (lado not in manos or nz > manos[lado][0]):
            manos[lado] = (nz, draw)
    manga, mejor = None, 0
    for draw, idxs in por_draw.items():
        d = host.table.draws[draw]
        if d["prim"] != 5 or d.get("group_bone", 0) != 0:
            continue
        nz = sum(1 for i in idxs if hz[i] == "guantes")
        if nz > mejor:
            manga, mejor = draw, nz
    return {k: v[1] for k, v in manos.items()}, manga


def _compactar_vertices_b3(P, N, tris, uv_tri):
    """Compacta (P,N) a los vertices usados por `tris`, partiendo por UV (costuras), para escribir
    una variante de mano con una UV por vertice. Devuelve P,N,UV,tris."""
    P = np.asarray(P, float)
    N = np.asarray(N, float)
    tris = np.asarray(tris, int)
    uv_tri = np.asarray(uv_tri, float)
    claves, nuevos = {}, []
    Po, No, Uo = [], [], []
    for ti, t in enumerate(tris):
        fila = []
        for c, v in enumerate(t):
            uv = (float(uv_tri[ti, c, 0]), float(uv_tri[ti, c, 1]))
            k = (int(v), uv)
            j = claves.get(k)
            if j is None:
                j = claves[k] = len(Po)
                Po.append(P[int(v)])
                No.append(N[int(v)])
                Uo.append(uv)
            fila.append(j)
        nuevos.append(tuple(fila))
    return (np.asarray(Po, float), np.asarray(No, float), np.asarray(Uo, float),
            np.asarray(nuevos, int))


def _injertar_pieza_b3(amb, fid, zona, afs, color=None, host=None, host_zones=None, td_h=None, preparar=False):
    """Sustituye una zona del #AMB base por la zona medida de un donante B3, vía injertar_ropa."""
    import injertar_ropa as ir  # noqa: PLC0415
    import injertar_pelo as ip  # noqa: PLC0415

    zone = zona
    current_bytes = bytes(amb)
    host = host or mr.Model(current_bytes)
    donor = mr.Model(afs.entry(fid))
    dp, dn = donor.posed()
    hz = host_zones if host_zones is not None else _zonas_pieza_b3(host)
    dz = _alinear_zonas(donor, _zonas_pieza_b3(donor), host, hz)
    td_h = td_h if td_h is not None else ip.triangulos_por_draw(host)
    por_draw = {}
    for i, (draw, _) in enumerate(td_h):
        por_draw.setdefault(draw, []).append(i)
    # 25/10: anfitrion = un draw cuya TEXTURA sea entera de la zona (la textura se reescribe con la de la prenda: si la
    # compartia con la piel del brazo, el brazo salia con trozos del guante); listas de triangulos tambien, si no son
    # rigidas de un hueso (las manos de Goku)
    tri_tex = [host.mats[host.tri_mat[i]]["tex"] for i in range(len(host.tris))]
    candidatos = []
    for draw, indices in por_draw.items():
        d = host.table.draws[draw]
        if d["prim"] != 5 and d.get("group_bone", 0) != 0:
            continue
        tex = host.table.materials[d["material"]]["tex"]
        if tex >= len(host.tex) or any(hz[i] != zone for i, t in enumerate(tri_tex) if t == tex):
            continue
        nz = sum(1 for i in indices if hz[i] == zone)
        if nz:
            candidatos.append((nz, -draw, draw))
    candidatos.sort(reverse=True)
    tex_destino = prim_destino = None
    destino_wh = None
    split_guantes = False
    hand_draws, sleeve_draw = {}, None
    if zona == "guantes":
        # 27/10 DS157: la mano L00 no se dibuja en combate (DS155). La prenda se reparte entre el
        # draw del ANTEBRAZO (manga) y los draws de MANO + sus variantes (guante). Si el cuerpo no
        # deja separarlo, se cae al camino antiguo (todo en la mano L00) sin romper nada.
        hand_draws, sleeve_draw = _draws_guantes_b3(host, por_draw, hz)
        libre = _textura_libre_b3(host.data) if (hand_draws and sleeve_draw is not None) else None
        if hand_draws and sleeve_draw is not None and libre is not None:
            chosen = hand_draws.get("R") or next(iter(hand_draws.values()))
            tex_destino, w_d, h_d = libre
            destino_wh = (w_d, h_d)
            prim_destino = 4
            split_guantes = True
        elif candidatos:
            chosen = candidatos[0][2]
        else:
            # camino antiguo: todo (manga+mano) en la mano L00
            chosen, tex_destino, w_d, h_d, prim_destino = _anfitrion_guantes_b3(host, por_draw, hz)
            destino_wh = (w_d, h_d)
    elif candidatos:
        chosen = candidatos[0][2]
    else:
        raise ValueError("sin textura exclusiva de la zona %s en el cuerpo" % zona)

    src_tri_idx = np.flatnonzero(np.asarray(dz) == zone)
    if zone == "piernas":   # con la cadera del donante (WAIST): el pantalon de Vegeta acababa en pico y bajo el cinturon de
        # Goku quedaba un hueco en V; lo que sobra queda dentro del cinturon/la chaqueta
        # (Vegeta y Bardock: la entrepierna cuelga de STMC, el estomago) hasta la altura del borde del pantalon
        tope = float(dp[np.unique(donor.tris[src_tri_idx].reshape(-1)), 1].max()) if len(src_tri_idx) else -1e9
        src_tri_idx = np.flatnonzero([z == zone or (z in B3_ZONAS and _clave_tri(donor, i) in ("WAIST", "STMC")
                                                    and float(dp[donor.tris[i], 1].mean()) < tope)
                                      for i, z in enumerate(dz)])
    # 25/10 (usuario: "ese NO es el aspecto que tiene in-game"): lo que cuelga de huesos que el heroe NO tiene (la cola de
    # Bardock enrollada en la cintura, las puntas del cinturon OBI de los gi, la capa) solo esta bien colocado cuando lo
    # mueve la animacion del personaje original; sin ella queda en la posicion de reposo (palos y faldones horizontales)
    # y en el heroe colgaria rigido del hueso padre. Fuera
    # 25/10 (QA Haiku: asi se perdian tambien el fajin-cola de Bardock, sus faldones, las hombreras de Vegeta y la funda de
    # Trunks): lo de huesos ajenos ya NO se tira; va RIGIDO con su ancestro que si existe en el heroe, colocado con el
    # marco de ese ancestro en los dos esqueletos (con el del hueso ajeno salian los "palos"). Solo se quitan los
    # accesorios de cabeza (visor, cinta) que cuelgan de huesos ajenos fuera de la cabeza del donante
    suf_h = {_clave_hueso_b3(l) for l in host.labels}
    ajeno = np.asarray([_clave_hueso_b3(donor.labels[b]) not in suf_h for b in range(len(donor.labels))])

    def de_cabeza(b):
        while 0 <= b < len(donor.labels) and ajeno[b]:
            b = par_d[b]
        return 0 <= b < len(donor.labels) and _clave_hueso_b3(donor.labels[b]) == "HEAD"
    par_d = donor.parent
    cabeza_ajena = np.asarray([ajeno[b] and de_cabeza(b) for b in range(len(donor.labels))])
    antebrazo_d = False
    if zone == "torso" and ANTEBRAZO_PRENDA:
        # 25/10 (usuario: "el brazo convive con dos tipos de textura"; medido: el antebrazo de piel de Bardock va en la zona
        # torso y se dibujaba ENCIMA del de Goku; la manga de Vegeta acababa en el codo porque su mitad baja va en la zona de
        # guantes): el brazo entero sale de la prenda, hasta la muneca (el antebrazo con las texturas del brazo de arriba; el
        # guante o la munequera, que son otra textura, no). La costura queda bajo la munequera del heroe
        en_pieza = set(int(t) for t in src_tri_idx)
        tex_brazo = {donor.mats[donor.tri_mat[t]]["tex"] for t in en_pieza if _clave_tri(donor, t) in ("RARM1", "LARM1")}
        # (todo el antebrazo, tambien su munequera o el puno del guante: medido, el brazal rojo de Bardock cubre casi
        # todo su antebrazo y sin el quedaba un hueco hasta la mano; la munequera de Goku se va con su antebrazo)
        ante = [t for t in range(len(donor.tris)) if _clave_tri(donor, t) in ("RARM2", "LARM2")] if tex_brazo else []
        if ante:
            src_tri_idx = np.union1d(src_tri_idx, ante).astype(int)
            antebrazo_d = True
    if zone == "torso":   # + sus accesorios de torso (hombreras SPAT, faldones WPAT, funda de la espada): la textura los
        # dejaba en la zona "accesorios", que ninguna prenda recogia (QA Haiku: Vegeta sin hombreras)
        src_tri_idx = np.union1d(src_tri_idx, [t for t, z in enumerate(dz) if z == "accesorios"
                                               and ajeno[donor.vbone[donor.tris[t]]].all()]).astype(int)
    src_tri_idx = np.asarray([t for t in src_tri_idx if not cabeza_ajena[donor.vbone[donor.tris[t]]].any()], int)
    guantes_molde = zona == "guantes" and tex_destino is not None
    manga_larga_ = False   # 26/10 DS82: la prenda cubre TODO el antebrazo (75/76/80/424)
    if guantes_molde:
        # 25/10 DS35-b (coordinador: "el heroe sale con SU antebrazo de piel; se pierde la hombrera de C-16, las vendas
        # de Trunks/Ten, la manga de 76"): del donante entra el guante ENTERO, la mano y la parte de PRENDA de su
        # antebrazo (ARM2): el brazal de Bardock, el guante largo de Vegeta, las vendas, la manga de 76/80... Su PIEL de
        # antebrazo no (la del heroe se queda: es el tono del jugador y la que asoma donde el guante no llega, como en el
        # original). La prenda va sobre el MOLDE del antebrazo del heroe (se infla por fuera, como en los torsos).
        piel_don = _es_piel(donor)

        def es_guante(t):
            k = _clave_tri(donor, int(t))
            # 27/10 DS187: ZARM = antebrazo en cadena propia del donante (Piccolo). Entra como
            # manga: el mapeo de huesos lo sube a LARM2 del heroe (su ancestro con equivalente).
            return "HAND" in k or (("ARM2" in k or "ZARM" in k) and not piel_don[int(t)])
        src_tri_idx = np.asarray([t for t in src_tri_idx if es_guante(t)], int)
        if not len(src_tri_idx):
            raise ValueError("el donante no tiene geometria de guante (mano o antebrazo de prenda)")
    if not len(src_tri_idx):
        raise ValueError("el donante no tiene geometria de %s" % zona)
    src_original = donor.tris[src_tri_idx]
    used = np.unique(src_original.reshape(-1))
    bones_src = donor.vbone[used]
    host_suf = {_clave_hueso_b3(label): i for i, label in enumerate(host.labels)}
    par = donor.parent
    bone_out, bone_marco = [], []
    for b in bones_src:
        seen, cur, mapped = set(), int(b), -1
        while 0 <= cur < len(donor.labels) and cur not in seen:
            seen.add(cur)
            mapped = host_suf.get(_clave_hueso_b3(donor.labels[cur]), -1)
            if mapped >= 0:
                break
            cur = par[cur]
        bone_out.append(mapped)
        bone_marco.append(cur if mapped >= 0 else int(b))
    bone_out = np.asarray(bone_out, int)
    bones_src = np.asarray(bone_marco, int)   # el marco del donante es el del ancestro emparejado (piezas ajenas rigidas)
    if (bone_out < 0).any():
        raise ValueError("hay huesos de la pieza sin equivalente ni ancestro en Goku")
    host_tex = host.table.materials[host.table.draws[chosen]["material"]]["tex"]
    rampa_guante = None
    if guantes_molde:
        # 25/10 DS35-b: el material de la mano usa la rampa de la PIEL (azul) y el toon la RESTA: un guante blanco
        # salia del color de la piel (medido: el guante de Vegeta y la manga de Ten, tinta de piel). La prenda se cuece y
        # se dibuja con la rampa GRIS mas usada del cuerpo (la de la ropa), que es neutra.
        from collections import Counter as _C  # noqa: PLC0415
        _cuenta = _C(m["shader"] for m in host.table.materials
                     if m["shader"] != 0xFFFFFFFF and _rampa_gris(host, m["shader"]))
        rampa_guante = _cuenta.most_common(1)[0][0] if _cuenta else None
    if destino_wh is not None:   # el atlas va a una textura LIBRE (no a la del anfitrion): manda su tamaño
        ww, hh = destino_wh
    else:
        hh, ww = host.tex[host_tex].shape[:2]
    uv_host_tri = {}          # 27/10 DS157: UV nueva por triangulo del draw del antebrazo
    uv_var_mapped = {}        # por variante de mano: UV nueva por triangulo
    var_specs = []
    if split_guantes:
        # Atlas UNICO: el brazo del heroe que se queda en el draw + manga/mano del donante + TODAS
        # las variantes de mano. Los tres materiales comparten la rampa gris (la de la ropa), asi
        # que la ropa no sale con el tinte de la piel y el brazo del heroe se corrige en la textura.
        piel_host_rampa = _filas_rampa(host, _piel_rampa(host)) if _piel_rampa(host) is not None else None
        if rampa_guante is not None:
            target_rampa = _filas_rampa(host, rampa_guante)
        else:
            target_rampa = _filas_rampa(host, host.table.materials[host.table.draws[chosen]["material"]]["shader"])

        def _clave_mat(model, t, kind):
            m = model.mats[model.tri_mat[int(t)]]
            tex = int(m["tex"])
            diff = tuple(float(x) for x in np.asarray(m["diffuse"]).tolist()) if tex >= len(model.tex) else ()
            return (kind, tex, diff, int(m["shader"]))

        host_sleeve_ids = list(por_draw.get(sleeve_draw, []))
        dvar = donor.variants()
        for lab, awg in host.variants().items():
            if "HAND" not in lab:
                continue
            lado = "R" if lab.endswith("RHAND") else "L"
            dk = dvar.get(lab)
            if dk is None:   # mismo sufijo en el donante (prefijo distinto)
                suf = lab[lab.index("_L"):]
                for dl, kk in dvar.items():
                    if dl.endswith(suf):
                        dk = kk
                        break
            dm = mr.Model(donor.data, awg=dk) if dk is not None else None
            var_specs.append({"host_label": lab, "host_awg": int(awg), "side": lado, "dm": dm,
                              "dm_awg": None if dk is None else int(dk)})
        # que claves son PIEL (en TODOS sus triangulos) antes de cocer
        piel_key = {}
        p_h = _es_piel(host)
        for t in host_sleeve_ids:
            k = _clave_mat(host, t, 0)
            piel_key[k] = piel_key.get(k, True) and bool(p_h[int(t)])
        p_d = _es_piel(donor)
        for pos, t in enumerate(src_tri_idx):
            k = _clave_mat(donor, t, 1)
            piel_key[k] = piel_key.get(k, True) and bool(p_d[int(t)])
        for vs in var_specs:
            dm = vs["dm"]
            if dm is None:
                continue
            p_v = _es_piel(dm)
            for t in range(len(dm.tris)):
                k = _clave_mat(dm, t, 1)
                piel_key[k] = piel_key.get(k, True) and bool(p_v[int(t)])
        imgs = {}
        for k in piel_key:
            kind, tex, diff, shader = k
            model = host if kind == 0 else donor
            if 0 <= tex < len(model.tex):
                img = model.tex[tex]
            else:
                c = np.clip(np.asarray(diff if diff else (0.65, 0.65, 0.65), np.float32), 0, 1)
                img = np.tile(np.append(c, 1.0)[None, None, :], (4, 4, 1)).astype(np.float32)
            imgs[k] = _cocer_textura_b3(model, shader, img, target_rampa, piel_key[k], piel_host_rampa)
        items, metas = [], []

        def _add_atlas(model, tri_ids, owner, side=None, var_idx=None):
            grupos = {}
            for t in tri_ids:
                k = _clave_mat(model, t, 0 if model is host else 1)
                grupos.setdefault(k, []).append(int(t))
            for k, ts in grupos.items():
                items.append({"key": k, "img": imgs[k], "uv": model.uv[model.tris[ts]]})
                metas.append({"owner": owner, "ts": ts, "side": side, "var": var_idx, "model": model})

        # el brazo del HEROE solo entra en el atlas si la prenda trae manga/antebrazo
        # (guante solo: no se toca la textura del brazo y el atlas de la mano va 1:1)
        hay_manga = any(_clave_tri(donor, int(t)) in ("RARM2", "LARM2") for t in src_tri_idx)
        if hay_manga:
            _add_atlas(host, host_sleeve_ids, "host")
        _add_atlas(donor, list(src_tri_idx), "body")
        for vi, vs in enumerate(var_specs):
            if vs["dm"] is not None:
                _add_atlas(vs["dm"], list(range(len(vs["dm"].tris))), "var", vs["side"], vi)
        rgba, mapped = _atlas_guantes_b3(items, ww, hh)
        pos_of = {int(t): n for n, t in enumerate(src_tri_idx)}
        uv_body = np.zeros((len(src_original), 3, 2), np.float64)
        uv_var_mapped = {vi: np.zeros((len(vs["dm"].tris), 3, 2), np.float64)
                         for vi, vs in enumerate(var_specs) if vs["dm"] is not None}
        for j, mt in enumerate(metas):
            uvj = mapped[j]
            if mt["owner"] == "host":
                for k, t in enumerate(mt["ts"]):
                    uv_host_tri[int(t)] = uvj[k]
            elif mt["owner"] == "body":
                for k, t in enumerate(mt["ts"]):
                    uv_body[pos_of[int(t)]] = uvj[k]
            else:
                vi = mt["var"]
                for k, t in enumerate(mt["ts"]):
                    uv_var_mapped[vi][int(t)] = uvj[k]
        uv_tri = uv_body
    else:
        rgba, uv_tri = _textura_atlas_b3(donor, src_original, donor.tri_mat[src_tri_idx],
                                         donor.uv[src_original], ww, hh,
                                         _filas_rampa(host, rampa_guante) if rampa_guante is not None else
                                         _filas_rampa(host, host.table.materials[host.table.draws[chosen]["material"]]["shader"]),
                                         _filas_rampa(host, _piel_rampa(host)) if _piel_rampa(host) is not None else None,
                                         piel_tri=_es_piel(donor)[src_tri_idx] if guantes_molde else None)
    if color is not None and tuple(color) != CERO:
        rgba = np.clip(tenir(rgba.astype(np.float32) / 255.0, tuple(color), piel=True) * 255 + 0.5, 0, 255).astype(np.uint8)
    escala_guante = None
    if zona == "guantes" and tex_destino is not None:
        # 25/10 DS35: el guante no puede encogerse por debajo del brazo del heroe (quedaria dentro y solo asomaria la
        # mano): escala uniforme de cuerpo, y nunca menor que 1 (el donante es igual o mayor que Goku).
        hd_don = float(np.ptp(dp[:, 1]))
        hd_hero = float(np.ptp(host.posed()[0][:, 1]))
        escala_guante = max(1.0, hd_hero / max(hd_don, 1e-6)) if hd_don > 0 else 1.0
    placed, placed_nrm, placement = _retarget_zona_huesos_b3(
        dp[used], dn[used], used, donor, host, zone, bones_src, bone_out, unica=escala_guante)
    placed = _empalmar_alto(placed, host, hz, zone)
    loc_tris = np.asarray([[np.searchsorted(used, v) for v in t] for t in src_original])
    if not guantes_molde:   # (guantes: nada de piezas rigidas; la manga va por hueso, y el camino rigido la encogia
        # con el largo de hueso del donante/héroe — medido: x1,24 -> 0,92)
        placed, placed_nrm = _piezas_rigidas(placed, placed_nrm, dp[used], dn[used], loc_tris, bones_src, bone_out,
                                             donor, host)
    # piezas de huesos ajenos (hombreras, faldones, fajin-cola, funda): solo el TRASLADO de su ancestro, con la orientacion
    # del donante (los dos esqueletos estan en la misma postura de referencia; los ejes de esos huesos, como el RSPAT de
    # Goku, que no lleva hombreras, no coinciden y las giraban dentro del brazo)
    # (solo las que cuelgan de huesos "PAT", hombreras y faldones: los del cuerpo, cintura y pecho, tienen los mismos ejes
    # en los dos esqueletos y con su marco la cola-fajin queda bien puesta)
    aj_v = ajeno[donor.vbone[used]] & np.asarray(["PAT" in _clave_hueso_b3(donor.labels[b]) for b in donor.vbone[used]])
    if aj_v.any():
        # anclaje: el primer ancestro del CUERPO (no "PAT": el WPAT de Goku, el de las puntas de su cinturon, esta mas alto
        # que el de Vegeta y los faldones tapaban la cola-fajin)
        sw, hw_ = np.asarray(donor.worlds()), np.asarray(host.worlds())
        hueso_v = donor.vbone[used]
        hp2, hn2 = host.posed()
        pv_ = np.unique(host.tris[[i for i, z in enumerate(hz) if z == "piernas"]].reshape(-1)) if "piernas" in hz else []
        pier_P, pier_N = hp2[pv_], hn2[pv_]
        H2 = float(np.ptp(hp2[:, 1]))
        fuera = np.zeros(3)
        for b in np.unique(hueso_v[aj_v]):
            cur = int(b)
            while 0 <= cur < len(donor.labels) and (ajeno[cur] or "PAT" in _clave_hueso_b3(donor.labels[cur])):
                cur = par[cur]
            hb_ = host_suf.get(_clave_hueso_b3(donor.labels[cur]), -1) if 0 <= cur < len(donor.labels) else -1
            m_ = aj_v & (hueso_v == b)
            if hb_ < 0:
                continue
            P_, N_ = dp[used][m_], dn[used][m_]
            # faldones (WPAT, usuario: "no se aplican las fisicas"): en el modelo y en su animacion de reposo estan en
            # horizontal; los cuelga la fisica del juego. Se dejan CAIDOS (giro hacia abajo desde su bisagra, el hueso
            # ajeno de la cadena) y van con el WPAT del heroe, las puntas del cinturon de Goku, que el juego ya mueve
            # con fisica
            if "WPAT" in _clave_hueso_b3(donor.labels[int(b)]):
                # (por la forma del faldon: hacia donde se extiende desde la cintura, con la bisagra en su borde de dentro)
                rel = P_ - sw[cur, :3, 3]
                rel[:, 1] = 0
                fuera = rel.mean(0)
                if np.linalg.norm(fuera) > 1e-6:
                    fuera /= np.linalg.norm(fuera)
                    bis = P_[int(np.argmin(rel @ fuera))]
                    eje = np.cross([0.0, 1.0, 0.0], fuera)
                    Rg = _giro_eje(eje, np.radians(CAIDA_FALDON))   # (positivo: hacia abajo)
                    P_, N_ = (P_ - bis) @ Rg.T + bis, N_ @ Rg.T
            P_ = P_ - sw[cur, :3, 3] + hw_[hb_, :3, 3]
            if "WPAT" in _clave_hueso_b3(donor.labels[int(b)]) and len(pier_P) and np.linalg.norm(fuera) > 1e-6:
                # entero (rigido) hacia fuera hasta quedar por encima del pantalon del heroe (muslos mas anchos que los
                # del donante: el faldon caido se metia dentro)
                from scipy.spatial import cKDTree  # noqa: PLC0415
                d_, k_ = cKDTree(pier_P).query(P_)
                sd = np.einsum("ij,ij->i", P_ - pier_P[k_], pier_N[k_])
                cerca = d_ < 0.05 * H2
                hundido = float(np.max(np.where(cerca, INFLAR * H2 - sd, 0.0)))
                if hundido > 0:
                    P_ = P_ + fuera * hundido
            placed[m_] = P_
            placed_nrm[m_] = N_
    # 25/10 (usuario: hombros, mangas y codos rotos, piel de otro tono, texturas que se pisan): el heroe CONSERVA su piel
    # de la zona (brazos, cuello: la de Goku); del donante entra la tela y solo la piel que tapa donde Goku no tiene cuerpo
    # (bajo su gi no hay hombros); la tela se infla sobre la piel conservada
    hp_, hn_ = host.posed()
    H_ = float(hp_[:, 1].max() - hp_[:, 1].min())
    piel_h = _es_piel(host) & np.asarray([z == zone for z in hz])
    # la piel del PECHO del heroe (la V del gi de Goku) se va: su borde hacia un escalon con la piel de la prenda nueva
    # (una raya en V); el pecho lo tapa la prenda o su piel de seguridad. El cuello se queda entero (empalma con la cabeza;
    # el dibujo de su escote lo borra _limpiar_escote)
    piel_h &= ~np.asarray([_clave_tri(host, i) in ("CHEST", "LCHN", "RCHN") for i in range(len(host.tris))])
    # 25/10 (medido en Bardock: con la piel de Goku conservada salian pestanas y costuras en los hombros; con la del
    # donante, cocida ya con el tono de piel del heroe, el hombro queda limpio): de la piel del heroe solo se queda el cuello
    # (la piel original del heroe sigue siendo el MOLDE sobre el que se hincha la tela, aunque ya no se dibuje: sin el, la
    # camiseta interior del gi de Gohan atravesaba la manga)
    piel_molde = piel_h.copy()
    if guantes_molde:
        # 25/10 DS35-b: el MOLDE es el antebrazo de piel del heroe (las manos las pone el guante): la prenda del antebrazo
        # del donante se infla por fuera de el y la piel del heroe se sigue dibujando (donde el guante no llega se ve su
        # piel, como en el original). No hay cuello que conservar: el bloque de abajo es del torso y con la zona guantes se
        # queda sin vertices y reventaba (`~cuello_` sobre un array vacio).
        piel_molde &= np.asarray([_clave_tri(host, i) in ("RARM2", "LARM2") for i in range(len(host.tris))])
        piel_h = piel_molde.copy()
    hp0_, _ = host.posed()
    y_corte = alto_cuello = None
    if not CONSERVAR_PIEL_HEROE and not guantes_molde:
        cuello_h = np.asarray([_clave_tri(host, i) == "NECK" for i in range(len(host.tris))])
        piel_h &= cuello_h
        # 25/10 (usuario: "cortes en la V entre el cuello y lo de abajo"): la parte baja del cuello de Goku acaba en dientes
        # de sierra (los tapaba el cuello de su gi); por debajo de CORTE_CUELLO manda el cuello/pecho de piel del donante
        # (solo si la prenda trae su cuello de piel: Trunks y la armadura de Vegeta no tienen triangulos en NECK y sin el
        # cuello de Goku quedaban agujeros en la base)
        cuello_d = any(_clave_tri(donor, int(t)) == "NECK" for t in src_tri_idx)
        if cuello_d and (piel_h & cuello_h).any():
            yc_ = hp0_[host.tris[cuello_h]].mean(1)[:, 1]
            alto_cuello = float(yc_.max() - yc_.min())
            y_corte = yc_.min() + CORTE_CUELLO * alto_cuello
            piel_h &= ~(cuello_h & (hp0_[host.tris].mean(1)[:, 1] < y_corte))
    piel_d = _es_piel(donor)[src_tri_idx]
    quedan = np.ones(len(src_original), bool)
    if piel_molde.any():
        from scipy.spatial import cKDTree  # noqa: PLC0415
        tv = host.tris[piel_molde]
        # 25/10 (usuario: "una calva a modo de seguro"): la piel del DONANTE se queda siempre como capa de seguridad, metida
        # justo por DENTRO de la piel del heroe (la del heroe se ve; donde no hay, rellena: bajo hombreras, sisas, escotes)
        # y cocida con la rampa de piel del heroe (un solo tono, el que elija el jugador). La tela sale por fuera
        ref_v = np.unique(tv.reshape(-1))
        # (ventanas: el tope nativo es 3261 y el pelo con su calva ya se lleva ~900) la piel del donante que queda entera
        # pegada a la del heroe no rellena nada: fuera. Solo se queda la que tapa huecos
        # (redundante solo junto a la piel del heroe que SE DIBUJA: con el molde entero se iba la piel de los brazos)
        tvd = host.tris[piel_h]
        if len(tvd):
            dv, _ = cKDTree(np.concatenate([hp_[tvd].mean(1), hp_[np.unique(tvd.reshape(-1))]])).query(placed)
            quedan = ~(piel_d & (dv[loc_tris] < 0.015 * H_).all(1))
            if not CONSERVAR_PIEL_HEROE and y_corte is not None:   # junto al corte del cuello, la del donante se queda
                # (entre los dientes del cuello de Goku se veia el cuello de la prenda por detras)
                quedan |= piel_d & (placed[loc_tris].mean(1)[:, 1] < y_corte + 0.5 * alto_cuello)
        tela_v = np.unique(loc_tris[~piel_d].reshape(-1))
        piel_v = np.setdiff1d(np.unique(loc_tris[piel_d].reshape(-1)), tela_v)
        # (sin la piel de Goku en la zona, la tela y la piel del donante solo se apartan de lo que SI se dibuja del heroe, el
        # cuello de arriba: empujarlas contra el cuerpo entero de Goku metia la piel del escote por detras del cuello de la
        # prenda, que asomaba en dientes rojos/azules en la V)
        zv_ = np.unique(host.tris[np.asarray([z == zone for z in hz])].reshape(-1))
        if not CONSERVAR_PIEL_HEROE:
            ref_v = zv_ = np.unique(host.tris[piel_h].reshape(-1)) if piel_h.any() else np.zeros(0, int)
        tela_arm = np.zeros(0, int)   # 26/10 DS74: manga/venda del antebrazo (si el donante solo trae guante, vacio)
        if len(tela_v) and len(ref_v):
            molde_t = host.tris[piel_h] if not CONSERVAR_PIEL_HEROE else tv
            bb_, _ = _bordes(molde_t, hp_)
            bpos = {tuple(np.round(hp_[int(v)], 3)) for e in bb_ for v in e}
            if guantes_molde:   # (manga: se ciñe al antebrazo del heroe sin el filtro de "eje de la normal")
                # (solo la prenda del ANTEBRAZO, y solo en radial: la mano queda fuera del molde y ceñirla la empujaba
                # por el eje del brazo, despegándola de la muñeca)
                es_arm2 = np.asarray([_clave_tri(donor, int(t)) in ("RARM2", "LARM2") for t in src_tri_idx], bool)
                tela_arm = np.unique(loc_tris[~piel_d & es_arm2].reshape(-1)) if (~piel_d & es_arm2).any() else np.zeros(0, int)
                if len(tela_arm):
                    Wh_ = np.asarray(host.worlds())
                    lado_pl = np.asarray([_lado_hueso_b3(_clave_hueso_b3(host.labels[b])) or "" for b in bone_out])
                    dclave = np.asarray([_clave_tri(donor, i) for i in range(len(donor.tris))], dtype=object)
                    hclave = np.asarray([_clave_tri(host, i) for i in range(len(host.tris))], dtype=object)
                    for ld_ in ("R", "L"):
                        a2_ = [i for i, l in enumerate(host.labels) if _clave_hueso_b3(l) == ld_ + "ARM2"]
                        hd2_ = [i for i, l in enumerate(host.labels) if _clave_hueso_b3(l).endswith(ld_ + "HAND")]
                        if not a2_ or not hd2_:
                            continue
                        sel = tela_arm[lado_pl[tela_arm] == ld_]
                        if not len(sel):
                            continue
                        # 25/10 DS35-b: si la prenda es un GUANTE/MANGA larga (cubre >= 60 % del antebrazo del
                        # donante) se lleva a la MISMA FRACCION del antebrazo del heroe (el suyo es mas largo: medido,
                        # 2,52 u del donante frente a 3,75 del heroe, y el guante de Vegeta se quedaba a media cana con
                        # una franja de piel en el codo). Las vendas (una franja) se quedan donde estan.
                        # 26/10 DS74: el molde es la piel PESADA al hueso ARM2 del heroe. Los vertices de los
                        # triangulos "ARM2" que van pesados a ARM1 (hombro: 9-11 por lado, t=-1,46) estiraban la manga
                        # hasta el hombro y dejaban picos; con los de ARM2 el tramo es -0,53..2,29 (2,82 u) y la manga
                        # del donante (2,78 u) entra casi sin estirar.
                        hv_all = np.unique(host.tris[hclave == ld_ + "ARM2"].reshape(-1))
                        hv_all = hv_all[host.vbone[hv_all] == a2_[0]]
                        a2d_ = [i for i, l in enumerate(donor.labels) if _clave_hueso_b3(l) == ld_ + "ARM2"]
                        hdd_ = [i for i, l in enumerate(donor.labels) if _clave_hueso_b3(l).endswith(ld_ + "HAND")]
                        if a2d_ and hdd_:
                            Wd_ = np.asarray(donor.worlds())
                            dv_all = np.unique(donor.tris[dclave == ld_ + "ARM2"].reshape(-1))
                            placed, larga_ = _manga_larga(placed, sel, dp[used], dp[dv_all], Wd_, Wh_,
                                                          a2d_[0], hdd_[0], a2_[0], hd2_[0], hv_all, hp_, H_)
                            manga_larga_ = manga_larga_ or larga_
                            # 26/10 DS74: NO se conforma al molde radialmente. Medido: con la manga ya estirada al
                            # antebrazo del heroe, empujar cada vertice por su normal RADIAL deformaba el puño (salía
                            # un flanco/repisa que flotaba sobre la muñeca) y dejaba picos en el borde del codo. La
                            # piel que tapaba la manga (estaba dentro del antebrazo de Goku, mas grueso) se quita con
                            # el rayo de `tapada`, asi que la manga se ve sin necesidad de inflarla.
            else:
                placed[tela_v] = _inflar(placed, loc_tris, hp_[ref_v], hn_[ref_v], H_,
                                         ref_borde=np.asarray([tuple(np.round(hp_[int(v)], 3)) in bpos
                                                               for v in ref_v]))[tela_v]
        if len(piel_v) and len(zv_):
            placed[piel_v] = _inflar(placed, loc_tris, hp_[zv_], hn_[zv_], H_, dentro=True)[piel_v]
        # 25/10 (usuario: "los hombros siguen fatal"; QA Haiku: la piel del brazo de Goku asomaba por las mangas): la
        # piel del heroe que queda DEBAJO de la tela del donante (ya inflada a su medida) se va; la manga es la que se ve.
        # El cuello no (empalma con la cabeza)
        tela_t = loc_tris[~piel_d]
        if len(tela_t) and not guantes_molde:   # (guantes: la piel del antebrazo del heroe se queda, es el molde y se ve
            # donde el guante no llega; quitarla dejaba el brazo abierto)
            C_ = np.concatenate([placed[tela_t].mean(1), placed[np.unique(tela_t.reshape(-1))]])
            ip_ = np.flatnonzero(piel_h)
            d_c, _ = cKDTree(C_).query(hp_[host.tris[ip_]].mean(1))
            cuello_ = np.asarray([_clave_tri(host, int(i)) == "NECK" for i in ip_])
            # (25/10, Trunks y armaduras sin cuello propio: los dientes de la base del cuello de Goku asomaban sobre el
            # cuello alto de la prenda; la mitad de abajo del cuello tambien se va si la tela la tapa)
            yn_ = hp_[host.tris[ip_]].mean(1)[:, 1]
            if cuello_.any():
                lo_, hi_ = yn_[cuello_].min(), yn_[cuello_].max()
                cuello_ &= yn_ >= lo_ + CORTE_CUELLO * (hi_ - lo_)
            piel_h[ip_[(d_c < CUBRE_TELA * H_) & ~cuello_]] = False
    # 25/10 DS35-b (medido: 8-39 % de los vertices del guante quedaban por DENTRO del antebrazo de Goku, que es mas
    # grueso, y la piel lo tapaba a parches): se probo a quitar la piel del antebrazo bajo la prenda; NO se hace — el
    # guante se infla por fuera del molde y la piel del heroe es la que se ve donde el guante no llega (venda de
    # Trunks/Yamcha), como en el original. Quitarla dejaba el brazo abierto donde la prenda no cubre 360o.
    if zone == "torso":
        # lo que en el donante sigue por debajo de la cintura (gi de una pieza de Gohan, Ten Shin Han: el faldon) quedaba
        # en picos sueltos encima del pantalon del heroe: fuera lo que cae entero por debajo del cinturon del heroe
        hv_t = np.unique(host.tris[[i for i, z in enumerate(hz) if z == "torso"]].reshape(-1))
        cintura = float(hp_[hv_t, 1].min()) + 0.01 * H_
        # (las piezas rigidas de huesos ajenos, faldones y fajin, cuelgan por debajo a proposito: se quedan)
        rigida = ajeno[donor.vbone[src_original]].any(1)
        brazo = np.asarray(["ARM" in _clave_tri(donor, int(t)) for t in src_tri_idx])   # (los brazos bajan mas que la cintura)
        quedan &= ~(placed[loc_tris][:, :, 1] < cintura).all(1) | rigida | brazo
    # (las piezas rigidas de huesos ajenos, hombreras y faldones, no se sueldan al cuerpo: se deformaban)
    rig_t = ajeno[donor.vbone[src_original]].any(1)
    fijos_r = np.unique(loc_tris[rig_t].reshape(-1)) if rig_t.any() else np.zeros(0, int)
    antes_r = placed[fijos_r].copy()
    if not guantes_molde:   # (guantes: la prenda ya encaja en el marco del hueso del heroe; soldar sus dos aros al
        # tramo de piel quitado la COMPRIMIA hasta el codo, medido: 2,03 -> 1,00)
        placed = _soldar_bordes(placed, dp[used], loc_tris[quedan & ~rig_t], host, hz, zone, conservar=piel_h)
    cuello_v_ = piel_h & np.asarray([_clave_tri(host, i) == "NECK" for i in range(len(host.tris))])
    if zone == "torso" and cuello_v_.any() and not any(_clave_tri(donor, int(t)) == "NECK" for t in src_tri_idx):
        # 25/10 (Trunks: entre la base del cuello de Goku y el pecho de piel de la prenda quedaba una rendija por la que se
        # veia la camiseta por dentro, una banda negra): el borde de la prenda que quede cerca se suelda a la base del cuello
        yv_ = hp_[host.tris[cuello_v_]].reshape(-1, 3)[:, 1]
        # (solo la PIEL de la prenda: el cuello alto de tela de Vegeta/Trunks se bajaba hasta la base del cuello)
        placed = _soldar_bordes(placed, dp[used], loc_tris[quedan & ~rig_t & piel_d], host, hz, zone, objetivo=cuello_v_,
                                y_max=float(yv_.min() + 0.5 * (yv_.max() - yv_.min())))
    placed[fijos_r] = antes_r
    # 25/10 (usuario: "los hombros siguen fatal"; medido: en la postura de referencia encajan, pero al bajar los brazos
    # los vertices del hombro colgados del giro del brazo, ARMROT, salian como pestanas): la ropa del hombro hereda el
    # hueso del cuerpo del heroe que tiene justo debajo (como el gi de Goku), salvo las piezas rigidas
    if HUESO_DEL_CUERPO:
        from scipy.spatial import cKDTree  # noqa: PLC0415
        hb_v = host.vbone
        zona_v = np.unique(host.tris[np.asarray([z == zone for z in hz])].reshape(-1))
        rot_ = np.asarray(["ARMROT" in _clave_hueso_b3(host.labels[b]) for b in bone_out])
        rot_[fijos_r] = False
        if rot_.any() and len(zona_v):
            hp_b, _ = host.posed()
            _, k_ = cKDTree(hp_b[zona_v]).query(placed[rot_])
            nuevo = hb_v[zona_v[k_]]
            hombro = np.asarray([_clave_hueso_b3(host.labels[b]) in ("RCHN", "LCHN", "CHEST", "RARMROT", "LARMROT",
                                                                   "RARM1", "LARM1", "NECK") for b in nuevo])
            sel = np.flatnonzero(rot_)[hombro]
            bone_out = bone_out.copy()
            bone_out[sel] = nuevo[hombro]
    if guantes_molde and not len(tela_arm):
        # 26/10 DS74 (128/129, guante SIN manga: Majin Buu). Medido: el donante solo trae la MANO y su malla deja un ojo
        # de fondo entre el pulgar y la palma (20/18 px con la pose del retrato); ademas su boca de puño es mas ancha que
        # el antebrazo del heroe. Se conserva la mano del heroe debajo (rellena el ojo; el guante la tapa), se suelda el
        # aro a la piel y se estrecha el guante un 30 % en el eje del brazo (X), que es el que abre el ojo del pulgar:
        # medido, 0,70 da huecos [0,0,0,0] en 128 y 129 (con 0,85 seguian 18/20 px).
        mano_h = np.asarray([z == zone for z in hz]) & np.asarray([_clave_tri(host, i).endswith("HAND")
                                                                  for i in range(len(host.tris))])
        # 27/10 DS187: esto vale para un GUANTE (128/129). Si la mano del donante es PIEL (392), no
        # hay boca de guante que rellenar: conservar debajo la mano del heroe la duplica y asoma por
        # la palma (medido). En ese caso la mano del heroe se borra como en el resto de piezas, y no
        # se estrecha la del donante (el 0,70 deformaba una mano desnuda).
        mano_d = np.asarray(["HAND" in _clave_tri(donor, int(t)) for t in src_tri_idx], bool)
        mano_guante = bool(np.mean(~piel_d[mano_d])) if mano_d.any() else True
        if mano_guante:
            piel_h = piel_h | mano_h
        placed = _soldar_bordes(placed, dp[used], loc_tris[quedan], host, hz, zone, objetivo=piel_h)
        # el aro del puño, ademas de soldado, pegado a la SUPERFICIE de la piel (la boca era mas ancha que el brazo).
        bd_, _ = _bordes(loc_tris[quedan], dp[used])
        rv_ = np.asarray(sorted({v for e in bd_ for v in e}), int)
        skin_v_ = np.unique(host.tris[piel_h].reshape(-1))
        if len(rv_) and len(skin_v_):
            from scipy.spatial import cKDTree  # noqa: PLC0415
            dd_, kk_ = cKDTree(hp_[skin_v_]).query(placed[rv_])
            cerca_ = dd_ < SOLDAR * H_
            if cerca_.any():
                ss_ = skin_v_[kk_[cerca_]]
                placed[rv_[cerca_]] = hp_[ss_] + 0.006 * H_ * hn_[ss_]
        W_ = np.asarray(host.worlds())
        for ld_ in ("R", "L"):
            hd_ = [i for i, l in enumerate(host.labels) if _clave_hueso_b3(l).endswith(ld_ + "HAND")]
            hv_ = np.unique(loc_tris[[_clave_tri(donor, int(t)).endswith(ld_ + "HAND")
                                      for t in src_tri_idx]].reshape(-1)) if hd_ else np.zeros(0, int)
            if len(hv_) and mano_guante:
                piv = W_[hd_[0], :3, 3]
                placed[hv_] = piv + (placed[hv_] - piv) * np.array([0.70, 1.0, 1.0])
    vertex_of = {int(v): i for i, v in enumerate(used)}
    corner_keys, tris_out = {}, []
    P_out, N_out, B_out, U_out = [], [], [], []
    for ti, tri in enumerate(src_original):
        if not quedan[ti]:
            continue
        new_tri = []
        for corner, original in enumerate(tri):
            uvt = tuple(float(x) for x in uv_tri[ti, corner])
            key = (int(original), uvt)
            vi = corner_keys.get(key)
            if vi is None:
                vi = len(P_out)
                corner_keys[key] = vi
                ui = vertex_of[int(original)]
                P_out.append(placed[ui])
                N_out.append(placed_nrm[ui])
                B_out.append(bone_out[ui])
                U_out.append(uvt)
            new_tri.append(vi)
        tris_out.append(tuple(new_tri))
    src_tris = np.asarray(tris_out, int)
    # la zona entera, en todos sus draws (sin restos de la vieja), MENOS la piel del heroe que se ve: la que queda TAPADA
    # por la tela nueva (rayo por su normal que da en la tela) se quita, si no asomaba entre los triangulos de la manga
    tapada = np.zeros(len(host.tris), bool)
    tela_t = loc_tris[quedan & ~piel_d]
    if guantes_molde and piel_h.any() and len(tela_t):
        # 26/10 DS74 (coordinador: "el antebrazo del heroe sale con UV de piel donde el original tiene tela"): la piel
        # del antebrazo que queda DETRAS de la prenda (rayo por su normal que da en la manga ya inflada) se quita. Antes
        # se conservaba entera y asomaba en cuñas entre los triangulos de la manga (medido en 75/76/80: parches de piel
        # dentro de la manga). Solo los triangulos con los TRES vertices tapados: el borde del molde se queda y no se
        # abre el brazo donde la prenda es corta (venda, guante hasta media cana).
        hv = np.unique(host.tris[piel_h].reshape(-1))
        hit = _rayo_exterior(placed, tela_t, np.arange(len(tela_t)), hp_[hv], hn_[hv], np.full(len(hv), -1),
                             alcance=0.05 * H_)
        cub = np.zeros(len(hp_), bool)
        cub[hv[hit >= 0]] = True
        tapada = piel_h & cub[host.tris].all(1)
        if manga_larga_:
            # 26/10 DS82 (coordinador: "en el borde de arriba de la manga (codo) quedan picos de piel del heroe que
            # asoman por encima de la tela"): como en los torsos, se recorta la piel del heroe que el aro superior de la
            # manga larga tapa. (a) la piel del ANTEBRAZO que queda por debajo del codo del heroe: la manga ya llega
            # ahi, asi el borde del codo queda limpio (medido: +18 tris nuevos en 75, +22 en 76). (b) si el aro queda
            # ENTERO por encima del codo (80: el donante trae la manga hasta el hombro), ademas la piel que tiene la
            # tela justo DETRAS (rayo hacia dentro por su normal): es la que asoma entre las vueltas (+21 en 80).
            # (c) la piel del brazo de arriba (ARM1) que tapa la manga en el codo, misma prueba (+0 en 424). Solo con
            # manga larga: vendas y guantes cortos no se tocan.
            cen_h = hp_[host.tris].mean(1)
            piel_host = _es_piel(host)
            Wb = np.asarray(host.worlds())
            labh = [_clave_hueso_b3(l) for l in host.labels]
            clave_tri = [_clave_tri(host, i) for i in range(len(host.tris))]
            bd_p, _ = _bordes(tela_t, placed)
            rim_v = sorted({v for e_ in bd_p for v in e_})

            def piel_detras(pt_):
                """Piel del heroe con la tela del donante justo DETRAS (rayo hacia dentro por su normal)."""
                if not len(pt_):
                    return np.zeros(0, bool)
                cn_ = cen_h[pt_]
                nn_ = hn_[host.tris[pt_]].mean(1)
                nn_ /= np.maximum(np.linalg.norm(nn_, axis=1), 1e-9)[:, None]
                hit_d = _rayo_exterior(placed, tela_t, np.arange(len(tela_t)), cn_, -nn_,
                                       np.full(len(pt_), -1), alcance=0.02 * H_)
                ok_ = np.zeros(len(pt_), bool)
                for j_, k_ in enumerate(hit_d):
                    if k_ < 0:
                        continue
                    gn_ = placed_nrm[tela_t[k_]].mean(0)
                    gn_ = gn_ / max(float(np.linalg.norm(gn_)), 1e-9)
                    ok_[j_] = float(gn_ @ nn_[j_]) > 0.5
                return ok_

            # 27/10 DS187: el recorte del antebrazo del heroe (a)/(c) solo vale en el LADO que
            # el donante cubre con tela. Antes se hacia en los dos lados por ser "manga larga"
            # en uno: en 82 (manga solo en el brazo izquierdo) borraba el antebrazo derecho del
            # heroe y la mano del donante quedaba a 0,55 u; en 350/352/353 (manga solo derecha)
            # borraba el izquierdo entero (2,0 u). Se mide la tela por lado.
            lados_don = np.asarray([_lado_hueso_b3(_clave_hueso_b3(donor.labels[int(b)])) or ""
                                    for b in donor.vbone[used]])
            lados_tela = {l for l in lados_don[np.unique(tela_t)] if l}
            for ld_ in ("R", "L"):
                if ld_ not in lados_tela:
                    continue
                a2_ = next((i for i, l in enumerate(labh) if l == ld_ + "ARM2"), None)
                hd_ = next((i for i, l in enumerate(labh) if l.endswith(ld_ + "HAND")), None)
                if a2_ is None or hd_ is None:
                    continue
                e_ = Wb[a2_, :3, 3]
                ax_ = Wb[hd_, :3, 3] - e_
                ax_ = ax_ / max(float(np.linalg.norm(ax_)), 1e-9)
                lado = np.asarray([k == ld_ + "ARM2" for k in clave_tri], bool)
                cut = piel_h & lado & (((cen_h - e_) @ ax_) > 0.0)          # (a)
                rim_l = [v for v in rim_v if labh[int(bone_out[v])].startswith(ld_)]
                if rim_l and ((np.asarray([placed[v] for v in rim_l]) - e_) @ ax_).max() < 0.0:
                    pt_ = np.flatnonzero(piel_h & lado)                       # (b)
                    cut[pt_[piel_detras(pt_)]] = True
                arriba = np.asarray([k == ld_ + "ARM1" for k in clave_tri], bool)
                pt1 = np.flatnonzero(piel_host & arriba)                     # (c)
                cut[pt1[piel_detras(pt1)]] = True
                tapada |= cut
    # 26/10 DS94 (coordinador: "quedan picos pequenos de piel por encima del borde superior de la manga, codo"): el
    # aro de arriba de la manga larga no es plano: sus vertices caen a lo largo del brazo y dejan una V por la que asoma
    # la piel del codo. Se aplana SOLO ese aro (arista de la tela, no el puno) llevando los vertices del lado bajo al
    # nivel del codo (mediana de la mitad del aro mas cercana al codo). No se quita piel ni se abre nada: la tela pasa a
    # cubrir la V. Se mide con el rango del aro: si ya es plano (424: 0,27 u; umbral 0,13 del antebrazo) no se toca.
    piel_tri = np.asarray(piel_d[quedan], bool) if len(quedan) == len(piel_d) else None
    if manga_larga_ and piel_tri is not None and len(src_tris):
        from collections import Counter as _C94  # noqa: PLC0415
        P_arr = np.asarray(P_out, float).copy()
        cloth_t = np.asarray(src_tris)[~piel_tri]
        bd_m, _ = _bordes(cloth_t, P_arr)
        adj_m = {}
        for a_, b_ in bd_m:
            adj_m.setdefault(int(a_), set()).add(int(b_))
            adj_m.setdefault(int(b_), set()).add(int(a_))
        vistos, bucles = set(), []
        for v0_ in adj_m:
            if v0_ in vistos:
                continue
            pila_, comp = [v0_], set()
            while pila_:
                x_ = pila_.pop()
                if x_ in comp:
                    continue
                comp.add(x_)
                pila_.extend(adj_m[x_] - comp)
            vistos |= comp
            bucles.append(sorted(comp))
        Wh_ = np.asarray(host.worlds())
        labh_ = [_clave_hueso_b3(l) for l in host.labels]
        ejes_ = {}
        for ld_ in ("R", "L"):
            a2_ = next((i for i, l in enumerate(labh_) if l == ld_ + "ARM2"), None)
            hd_ = next((i for i, l in enumerate(labh_) if l.endswith(ld_ + "HAND")), None)
            if a2_ is not None and hd_ is not None:
                e_ = Wh_[a2_, :3, 3]
                ax_ = Wh_[hd_, :3, 3] - e_
                ejes_[ld_] = (e_, ax_ / max(float(np.linalg.norm(ax_)), 1e-9), float(np.linalg.norm(ax_)))
        desp = np.zeros_like(P_arr)
        for lv_ in bucles:
            lv_ = np.asarray(lv_, int)
            lado_ = _C94(labh_[int(b)][0] for b in np.asarray(B_out)[lv_] if int(b) >= 0).most_common(1)
            if not lado_ or lado_[0][0] not in ejes_:
                continue
            e_, ax_, L_ = ejes_[lado_[0][0]]
            t_ = (P_arr[lv_] - e_) @ ax_
            if float(np.ptp(t_)) <= 0.13 * L_:
                continue
            med_ = float(np.median(t_))
            t0_ = float(np.median(t_[t_ <= med_]))
            mov_ = t_ > t0_
            if not mov_.any():
                continue
            desp[lv_[mov_]] += (t0_ - t_[mov_])[:, None] * ax_[None, :]
        if desp.any():
            P_out = P_arr + desp
        # 27/10 DS111 (coordinador: "quedan picos pequenos de piel en el codo" en 75/76/80): el aro plano de DS94
        # deja 1-3 vertices SUELTOS que sobresalen por encima del nivel del resto (medido en el modelo de salida, t
        # sobre el antebrazo del heroe: 75 [-0,463,-0,458] con el aro a -0,226; 76 -0,315 con el aro a -0,222; 80
        # -0,347 con el aro a -0,288; el ORIGINAL tiene el aro a 0,002/0,000/-0,046). Se nivelan POR VERTICE al nivel
        # del aro. Solo si el aro YA es plano (>= 60 % de sus vertices del borde a +-0,02 del nivel) y esta subido
        # (nivel < -0,05): asi 424 (aro sin aplanar, 24 % plano) y 99/383/447 no se tocan (medido: byte a byte iguales).
        #
        # Ademas, si entre el aro y el CODO no hay otro borde de tela (manga de una sola hoja: 75/76; en 80 el
        # vendaje tiene otra vuelta a -0,193/-0,072 y bajarlo abriria huecos, medido), se lleva el aro ENTERO al
        # plano del codo (t = 0), que es donde lo tiene el original: la piel que asomaba por delante de la tela queda
        # por ENCIMA del aro (brazo al aire, como en el original) y deja de atravesarla.
        P_arr = np.asarray(P_out, float).copy()
        bd_r, _ = _bordes(cloth_t, P_arr)
        vs_r = np.asarray(sorted({v for e_ in bd_r for v in e_}), int)
        for ld_ in ("R", "L"):
            if ld_ not in ejes_ or not len(vs_r):
                continue
            e_, ax_, L_ = ejes_[ld_]
            t_ = (P_arr[vs_r] - e_) @ ax_
            banda = (t_ > -0.75 * L_) & (t_ < 0.55 * L_)
            if int(banda.sum()) < 6:
                continue
            tv = t_[banda]
            hist, edges = np.histogram(tv, bins=np.arange(-0.75, 0.551, 0.01) * L_)
            k_ = int(hist.argmax())
            aro_ = (tv >= edges[k_]) & (tv < edges[k_ + 1])
            if not aro_.any():
                continue
            nivel = float(np.median(tv[aro_]))
            plano = float((np.abs(tv - nivel) <= 0.02 * L_).mean())
            if plano < 0.6 or nivel > -0.05 * L_:
                continue
            sueltos = (tv < nivel - 0.02 * L_) & (tv > nivel - 0.30 * L_)
            otros = (tv > nivel + 0.02 * L_) & (tv < -0.02 * L_)
            if not otros.any() and 1 <= int(sueltos.sum()) <= 3:
                grupo = aro_ | sueltos          # una sola hoja hasta el codo: aro plano + sueltos, al codo
                nivel_dst = 0.0
            else:
                grupo = sueltos                 # hoja con vueltas (80): solo los sueltos, al nivel del aro
                nivel_dst = nivel
            if not grupo.any():
                continue
            ids_ = vs_r[banda][grupo]
            P_arr[ids_] += (nivel_dst - tv[grupo])[:, None] * ax_[None, :]
        P_out = P_arr
    quitar = (np.asarray([z == zone for z in hz], bool) & ~piel_h) | tapada
    if guantes_molde:
        # 27/10 DS187: si el donante NO trae manga en un antebrazo, el antebrazo del heroe se
        # queda ENTERO (piel y munequera). Antes la munequera se borraba por ser "de la zona
        # guantes" y, como el donante no ponia nada en ese lado, quedaba un hueco entre la piel
        # y la mano (82: 0,55 u; Piccolo 350/352/353: 2,0 u). Aprobados (99/383/424/447) y el
        # resto con manga en los dos lados no cambian.
        mano_d = np.asarray(["HAND" in _clave_tri(donor, int(t)) for t in src_tri_idx], bool)
        tela_arm_d = quedan & ~piel_d & ~mano_d
        lados_cloth = set()
        if tela_arm_d.any():
            lados_don = np.asarray([_lado_hueso_b3(_clave_hueso_b3(donor.labels[int(b)])) or ""
                                    for b in donor.vbone[used]])
            lados_cloth = {l for l in lados_don[np.unique(loc_tris[tela_arm_d])] if l}
        for ld_ in ("R", "L"):
            if ld_ in lados_cloth:
                continue
            quitar &= ~np.asarray([_clave_tri(host, i) == ld_ + "ARM2" for i in range(len(host.tris))])
    if antebrazo_d:   # el antebrazo del heroe (piel) se va: el de la prenda lo sustituye entero (sin dos texturas)
        quitar |= np.asarray([_clave_tri(host, i) in ("RARM2", "LARM2") for i in range(len(host.tris))])
    # rampa: la de la ropa (gris) si la del anfitrion es la de la piel (azul: el toon la RESTA y los guantes blancos
    # salian oscuros); la decide injertar_ropa_multi si nadie mas usa ese material
    from collections import Counter  # noqa: PLC0415
    rd = Counter(donor.mats[donor.tri_mat[t]]["shader"] for t in src_tri_idx).most_common(1)[0][0]
    # 27/10 DS157: reparto de la prenda entre los draws que SI se dibujan (manga en el antebrazo,
    # mano/guante en los draws de mano) + todas las variantes de mano para el combate.
    partes = variantes = None
    if split_guantes:
        claves_don = [_clave_tri(donor, int(t)) for t in src_tri_idx]
        claves_q = [claves_don[int(i)] for i in np.flatnonzero(quedan)]
        mano_q = np.asarray(["HAND" in str(k) for k in claves_q], bool)
        claves_h = [_clave_tri(host, i) for i in range(len(host.tris))]
        host_hand = np.asarray(["HAND" in str(k) for k in claves_h], bool)
        lados_h = np.asarray([("R" if str(k).endswith("RHAND") else "L") if "HAND" in str(k) else "" for k in claves_h])
        lados_q = [("R" if str(k).endswith("RHAND") else "L") if "HAND" in str(k) else "" for k in claves_q]
        partes = []
        # manga/antebrazo -> draw del antebrazo del cuerpo
        sel_manga = np.flatnonzero(~mano_q)
        if len(sel_manga):
            q_manga = quitar & ~host_hand
            remapa = {}
            for t in host_sleeve_ids:
                t = int(t)
                if q_manga[t] or t not in uv_host_tri:
                    continue
                for c, v in enumerate(host.tris[t]):
                    remapa[int(v)] = tuple(float(x) for x in uv_host_tri[t][c])
            partes.append({"host": int(sleeve_draw), "quitar": q_manga,
                           "P": np.asarray(P_out), "N": np.asarray(N_out), "UV": np.asarray(U_out),
                           "bones": np.asarray(B_out), "tris": np.asarray(src_tris)[sel_manga],
                           "rgba": rgba, "zona": zona, "placement": placement,
                           "rampa_gris": True, "tex_destino": tex_destino, "prim_destino": None,
                           "uv_remap": remapa})
        # mano/guante -> el draw L00 de la mano del lado (vista previa / select)
        for lado in ("R", "L"):
            draw = hand_draws.get(lado)
            if draw is None:
                continue
            sel = np.flatnonzero(mano_q & np.asarray([l == lado for l in lados_q], bool))
            if not len(sel):
                continue
            q_lado = quitar & host_hand & (lados_h == lado)
            partes.append({"host": int(draw), "quitar": q_lado,
                           "P": np.asarray(P_out), "N": np.asarray(N_out), "UV": np.asarray(U_out),
                           "bones": np.asarray(B_out), "tris": np.asarray(src_tris)[sel],
                           "rgba": rgba, "zona": zona, "placement": placement,
                           "rampa_gris": True, "tex_destino": tex_destino, "prim_destino": 4})
        # variantes de mano (puno y demas poses que usa el combate)
        variantes = []
        for vi, vs in enumerate(var_specs):
            if vs["dm"] is not None:
                dm = vs["dm"]
                esc = float(escala_guante) if escala_guante else 1.0
                Pv = np.asarray(dm.pos, float) * esc
                Nv = np.asarray(dm.nrm, float)
                uv_tri_v = uv_var_mapped[vi]
                Pv, Nv, UVv, tris_v = _compactar_vertices_b3(Pv, Nv, dm.tris, uv_tri_v)
                fuente = "variante %s" % vs["host_label"]
            else:
                # 27/10 DS161 + coordinador: el donante no tiene esta pose. Copiar su mano L00 (abierta)
                # sacaba la mano SUELTA en todas las poses (reposo L38 y las de golpe/salto, 99 y 447 en
                # combate): el juego dibuja la variante con la pose del hueso de la mano de esa etiqueta.
                # Se conserva la mano NATIVA del heroe (siempre pegada a la muneca); la manga del donante
                # sigue en el draw del antebrazo (draw 3). Ver INFORME DS161.
                variantes.append({"awg": int(vs["host_awg"]), "saltar": True,
                                  "fuente": "heroe (el donante no tiene %s)" % vs["host_label"]})
                continue
            variantes.append({"awg": int(vs["host_awg"]), "P": Pv, "N": Nv, "UV": UVv, "tris": tris_v,
                              "tex": tex_destino, "ramp": None, "fuente": fuente})
    pieza = {"host": chosen, "quitar": quitar, "P": np.asarray(P_out), "N": np.asarray(N_out),
             "UV": np.asarray(U_out), "bones": np.asarray(B_out), "tris": src_tris, "rgba": rgba,
             "zona": zona, "placement": placement,
             "rampa_gris": bool(guantes_molde and rampa_guante is not None),   # la rampa va cocida en la textura
             "tex_destino": tex_destino, "prim_destino": prim_destino,
             "piel_pieza": np.asarray(piel_d[quedan], bool)}   # DS111: que triangulos de la pieza son PIEL del donante
    if partes is not None:
        pieza["partes"] = partes
        pieza["variantes"] = variantes or []
    if preparar:
        return pieza
    out, summary = ir.injertar_ropa(current_bytes, host, quitar, chosen, dp[used], dn[used], donor.uv[used],
                                     bone_out, src_tris, rgba, tex_destino=tex_destino,
                                     prim_destino=prim_destino)
    summary.update(zona=zona, fid=fid)
    return out, summary


def _aplicar_piezas_b3(amb, ficha, juego, afs, tintar=False):
    """Aplica solo donantes verificados; ante cualquier fallo conserva intacto el aspecto base de esa zona."""
    seleccion = {z: int(ficha.get("b3_" + z, -1)) for z in B3_ZONAS}
    resumen = {}
    if not any(fid >= 0 for fid in seleccion.values()):
        return bytes(amb), resumen
    if ficha.get("cuerpo", 0) != 0 or ficha.get("traje", 0) != 0 or _xv2_piezas_activas(juego):
        return bytes(amb), {z: "Base (opciones seguras solo en Goku, traje 0, ruta B3)" for z, fid in seleccion.items() if fid >= 0}
    catalogo = _cargar_catalogo_b3()
    actual = bytes(amb)
    base_model = mr.Model(actual)
    base_zones = _zonas_pieza_b3(base_model)
    import injertar_pelo as ip  # noqa: PLC0415
    import injertar_ropa as ir  # noqa: PLC0415
    base_draws = ip.triangulos_por_draw(base_model)
    colores = ficha.get("partes", [CERO] * 4)
    preparar = []
    resumen = {}
    for z, fid in seleccion.items():
        if fid < 0:
            continue
        opcion = catalogo.get(z, {}).get(fid)
        if opcion is None:
            resumen[z] = "Base (donante no compatible o no verificado)"
            continue
        try:
            color = colores[B3_ZONAS[z]] if tintar and len(colores) > B3_ZONAS[z] else None
            item = _injertar_pieza_b3(actual, fid, z, afs, color, base_model, base_zones, base_draws, preparar=True)
            item["nombre"] = opcion["nombre"]
            preparar.append(item)
        except Exception as e:  # noqa: BLE001 (una pieza fallida no impide usar las demas ni el cuerpo base)
            resumen[z] = "Base (injerto fallido: %s: %s)" % (type(e).__name__, e)
    # Se reconstruyen todas las zonas en un solo pase: los draw/zonas anfitrion siguen siendo los medidos sobre fid 264,
    # aunque una prenda anterior cambie sus triangulos. Si el limite de ventanas impide el conjunto, se conserva la
    # mayor combinacion que quepa y cada pieza descartada vuelve a Base con motivo en el resumen.
    aplicadas = []
    batch_summary = {}
    if preparar:
        from itertools import combinations  # noqa: PLC0415

        def _partes(it):
            return list(it.get("partes") or [it])

        for cantidad in range(len(preparar), 0, -1):
            for subset in combinations(preparar, cantidad):
                piezas = []
                for it in subset:
                    piezas.extend(_partes(it))
                try:
                    cand, bs = ir.injertar_ropa_multi(bytes(amb), base_model, piezas)
                    # 27/10 DS157: la mano/guante va tambien en las variantes (puno, palma...).
                    # Se hace despues del pase unico del AWG0, con la MISMA rampa que quedo en el
                    # material de la mano (la gris de la ropa si el atlas la coció asi).
                    split = [it for it in subset if it.get("variantes")]
                    if split:
                        m2 = mr.Model(bytes(cand))
                        ramp = None
                        for d in m2.table.draws:
                            if d["prim"] == 4 and d.get("group_bone", 0) < len(m2.labels)                                     and "HAND" in m2.labels[d["group_bone"]]:
                                ramp = m2.table.materials[d["material"]]["shader"]
                                break
                        for it in split:
                            for v in it["variantes"]:
                                v["ramp"] = ramp
                            cand, rv = ir.injertar_variantes_mano(bytes(cand), it["variantes"])
                            bs.setdefault("variantes_mano", {})[it["zona"]] = rv
                    actual, batch_summary, aplicadas = cand, bs, list(subset)
                    break
                except Exception as e:  # noqa: BLE001 (sin geometria nueva: se mantiene el modelo anfitrion)
                    batch_summary = {"fallo": "%s: %s" % (type(e).__name__, e)}
            if aplicadas:
                break
        aplicadas_ids = {id(p) for p in aplicadas}
        por_zona = batch_summary.get("zonas", {})
        for p in preparar:
            z = p["zona"]
            if id(p) in aplicadas_ids:
                if p.get("partes"):
                    zs = por_zona.get(z, {})
                    resumen[z] = {"pieza": p["nombre"],
                                  "anfitriones": [int(x["host"]) for x in p["partes"]],
                                  "partes": len(p["partes"]),
                                  "marco": zs.get("marco"),   # 27/10 DS166: lista rigida (group_bone) de la mano
                                  "variantes_mano": len(p.get("variantes") or []),
                                  "variantes_conservadas": sum(1 for v in (p.get("variantes") or []) if v.get("saltar")),
                                  "textura": zs.get("textura"), "tex": zs.get("tex"),
                                  "ventanas": batch_summary.get("ventanas", {})}
                else:
                    resumen[z] = {"pieza": p["nombre"], **por_zona.get(z, {}),
                                  "ventanas": batch_summary.get("ventanas", {})}
            else:
                resumen[z] = "Base (fallo conjunto: %s)" % batch_summary.get("fallo", "tope de ventanas")
    if any(p["zona"] == "torso" for p in aplicadas):
        actual = _limpiar_escote(actual, base_model)
    return actual, resumen


def _limpiar_escote(amb, base_model):
    """25/10: la piel del pecho y de la base del cuello del heroe lleva DIBUJADO el borde del escote del gi de Goku (una
    raya negra en V). Con otro torso quedaba a la vista; quitar esa piel dejaba agujeros. Se queda la piel y se borra el
    dibujo: los texeles oscuros de esos triangulos pasan a piel (blanco, banda 0: el tono lo pone la rampa)."""
    import injertar_ropa as ir  # noqa: PLC0415
    hp, _ = base_model.posed()
    piel = _es_piel(base_model)
    claves = [_clave_tri(base_model, i) for i in range(len(base_model.tris))]
    cuello = [i for i in range(len(base_model.tris)) if piel[i] and claves[i] == "NECK"]
    tris = [i for i in range(len(base_model.tris)) if piel[i] and claves[i] in ("CHEST", "LCHN", "RCHN")]
    if cuello:
        yc = hp[base_model.tris[cuello]].mean(1)[:, 1]
        tris += [t for t, y in zip(cuello, yc) if y < yc.min() + 0.5 * (yc.max() - yc.min())]
    m = mr.Model(bytes(amb))
    out = bytearray(amb)
    for tx in {base_model.mats[base_model.tri_mat[i]]["tex"] for i in tris}:
        if tx >= len(m.tex):
            continue
        sel = [i for i in tris if base_model.mats[base_model.tri_mat[i]]["tex"] == tx]
        img = np.asarray(m.tex[tx], np.float32).copy()
        h, w = img.shape[:2]
        r = _rasterizar_uv(base_model, sel, w, h)
        if r is None:
            continue
        ys, xs = r[0], r[1]
        osc = img[ys, xs, :3].max(1) < 0.6
        img[ys[osc], xs[osc], :3] = 1.0
        img[ys[osc], xs[osc], 3] = 0.0
        ir._escribir_textura(out, tx, (np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8))
    return bytes(out)


# Tipos de rostro (25/10): ficha.rostro -> cuerpo nativo de B3 del que sale la cara (ojos, cejas, nariz, orejas).
# 0 = la del propio cuerpo. Solo los aprobados en hoja (frente/perfil) entran aqui; el orden es el de la UI (heroe_ui.cpp)
ROSTROS = {1: 264, 2: 416, 3: 225, 4: 235, 5: 390, 6: 99, 7: 445, 8: 400,
           9: "A", 10: "B", 11: "C", 12: "D", 13: "E"}   # 9-13: rostros propios (OJOS_TIPOS)
# su cara en Super Saiyan (formas del heroe): mismo orden que SSJ_PELO; sin SSJ propio, la base del donante
ROSTRO_SSJ = {264: (266, 267, 268), 99: (266, 267, 268), 416: (417, 417), 235: (236, 237), 390: (391, 391)}


def rampa_piel_tenida(model, c):
    """25/10 (usuario: un solo tono de piel, modificable): en B3 la piel es una textura BLANCA y su color sale de la
    rampa toon de la piel (el juego RESTA la rampa: color = 1 - rampa). Cambiar esa rampa cambia a la vez la cara, el
    cuello, los brazos, las manos y la piel de las prendas injertadas (se cuece con esta misma rampa). Devuelve
    (indice de la rampa, rgba float nueva) o None. Las filas casi blancas de la rampa (realces y contorno) no se tocan."""
    if tuple(c) == CERO:
        return None
    r = _piel_rampa(model)
    if r is None:
        return None
    ramp = np.asarray(model.tex[r], np.float32).copy()
    col = 1.0 - ramp[..., :3]
    h, s_, v = _rgb_hsv(col)
    piel = v > 0.2
    if not piel.any():
        return None
    v_ref = float(np.percentile(v[piel], 90))
    th, ts, tv = objetivo(tuple(c))
    nv = np.clip(tv * v / max(v_ref, 1e-3), 0, 1)
    nuevo = _hsv_rgb(np.full(nv.shape, th), np.full(nv.shape, ts), nv)
    ramp[..., :3] = np.where(piel[..., None], 1.0 - nuevo, ramp[..., :3])
    return r, ramp


def _rgb_ficha(c):
    return None if tuple(c) == CERO else tuple(float(x) for x in _hsv_rgb(*[np.array(x) for x in objetivo(tuple(c))]))


def texturas_ficha(amb, ficha, afs, solo_base=False):
    """Las texturas de cara de la ficha para este modelo: tipo de rostro (ficha.rostro), color de ojos (ficha.ojos) y
    cejas del color del pelo (c1). En las formas Super Saiyan (ficha._ssj = nivel) manda el juego: pelo y cejas rubios y
    ojos turquesa (su cara SSJ nativa si el donante la tiene; si no, la normal recoloreada); SSJ3 sin cejas.
    Devuelve {indice de textura: rgba float} ({} = nada que cambiar)."""
    spec = ROSTROS.get(int(ficha.get("rostro", 0)))
    gesto = OJOS_TIPOS.get(spec) if isinstance(spec, str) else None
    fid = gesto["fid"] if gesto else spec
    k = int(ficha.get("_ssj", 0))
    iris = ceja = None
    sin_cejas = False
    if k > 0:
        lista = ROSTRO_SSJ.get(fid) if fid else None
        if fid and lista:
            sin_cejas = k > len(lista)
            fid = lista[min(k, len(lista)) - 1]
            if sin_cejas:
                iris = SSJ_OJOS
        elif fid or not ficha.get("_ssj_nativa"):
            iris, ceja, sin_cejas = SSJ_OJOS, (None if k >= 3 else SSJ_CEJAS), k >= 3
    else:
        iris, ceja = _rgb_ficha(ficha.get("ojos", CERO)), _rgb_ficha(ficha.get("c1", CERO))
        pelo = int(ficha.get("pelo", 0))
        if ceja is None and pelo in PELOS_NATIVOS:   # sin color propio: las cejas del color del peinado (Trunks: lila)
            try:
                dm = mr.Model(bytes(afs.entry((KFID.get(PELOS_NATIVOS[pelo]) or KFID[0])[0])))
                Pd, _ = dm.posed(pose_de(dm))
                ht = [i for i, z in enumerate(zonas_ficha(dm, Pd, pelo_color=True)) if z == "pelo"]
                cp = _rgb_pelo(dm, ht) if ht else None
                ceja = cp if cp and max(cp) > 0.3 else None
            except Exception:   # noqa: BLE001 (cejas negras)
                ceja = None
    if fid is None and iris is None and ceja is None and not sin_cejas:
        return {}
    return texturas_rostro(amb, afs.entry(fid) if fid else None, gesto, iris, ceja, sin_cejas, solo_base)


def _textura_principal(model, tris):
    from collections import Counter  # noqa: PLC0415
    c = Counter(model.mats[model.tri_mat[i]]["tex"] for i in tris if model.mats[model.tri_mat[i]]["tex"] < len(model.tex))
    return c.most_common(1)[0][0] if c else None


def _local_head(model, P):
    pose = pose_de(model)
    W = np.asarray(model.worlds(pose))
    hb = model.bone("HEAD")
    if hb is None:
        return None, None, None
    inv = np.linalg.inv(W[hb])
    return P @ inv[:3, :3].T + inv[:3, 3], W, inv


def _rasterizar_uv(model, tris, w, h):
    """Texeles (y, x) cubiertos por los triangulos en su UV, con el triangulo y las baricentricas de cada uno."""
    ys, xs, ti, bs = [], [], [], []
    for t in tris:
        uv = model.uv[model.tris[t]] % 1.0
        px = np.stack([uv[:, 0] * w - 0.5, uv[:, 1] * h - 0.5], 1)
        x0, y0 = np.floor(px.min(0)).astype(int)
        x1, y1 = np.ceil(px.max(0)).astype(int)
        gx, gy = np.meshgrid(np.arange(max(x0, 0), min(x1, w - 1) + 1), np.arange(max(y0, 0), min(y1, h - 1) + 1))
        if not gx.size:
            continue
        a, b, c = px
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-12:
            continue
        l0 = ((b[1] - c[1]) * (gx - c[0]) + (c[0] - b[0]) * (gy - c[1])) / den
        l1 = ((c[1] - a[1]) * (gx - c[0]) + (a[0] - c[0]) * (gy - c[1])) / den
        l2 = 1 - l0 - l1
        e = -0.6 / max(np.sqrt(abs(den)), 1e-6)   # medio texel de margen (bordes de isla sin huecos)
        m = (l0 >= e) & (l1 >= e) & (l2 >= e)
        ys.append(gy[m]); xs.append(gx[m]); ti.append(np.full(int(m.sum()), t))
        bs.append(np.stack([l0[m], l1[m], l2[m]], 1))
    if not ys:
        return None
    return np.concatenate(ys), np.concatenate(xs), np.concatenate(ti), np.concatenate(bs)


def _tris_cara(m):
    """Triangulos de la cara (huesos de cara/boca/placa) que usan las texturas de cara: las de la boca (M_*), la placa
    (*FACE) o los ojos; el pelo cuelga tambien de HEAD y se colaba. Las texturas pequenas (pupilas de pieza aparte:
    Gohan, Ten Shin Han) entran; los dientes no."""
    z = zonas_triangulos(m)
    dc = [i for i, x in enumerate(z) if x == "cara"]
    tex_cara = {m.mats[m.tri_mat[i]]["tex"] for i in dc
                if _clave_tri(m, i).startswith("M_") or "FACE" in _clave_tri(m, i) or "EYE" in _clave_tri(m, i)}
    grande = lambda i: min(m.tex[m.mats[m.tri_mat[i]]["tex"]].shape[:2]) >= 128  # noqa: E731
    return [i for i in dc if m.mats[m.tri_mat[i]]["tex"] < len(m.tex)
            and (m.mats[m.tri_mat[i]]["tex"] in tex_cara and grande(i) or not grande(i) and "TEETH" not in _clave_tri(m, i))]


def rostro_texturas(model, P, dm, gesto=None, acum=None, n0=None):
    """Tipo de rostro: la cara del donante `dm` proyectada en la del cuerpo. La cara de B3 es textura plana (ojos, cejas,
    nariz, orejas, boca) sobre una malla casi igual en todos; cada personaje la reparte distinto en su textura, asi que
    se pasa por posicion 3D: cada texel de la cara del cuerpo (marco HEAD) toma el del punto del donante que se ve en esa
    direccion (marco HEAD, escala de la boca y marcas de ojos y boca). La piel la pone la rampa del cuerpo. `model` y `dm`
    pueden traer puesta una EXPRESION (use_variant: la placa de la cara cambia por otra con otra textura). `acum`: las
    texturas ya hechas {indice base: rgba}; `n0`: cuantas texturas tiene el cuerpo sin variante (las de la variante van
    detras, repetidas). Devuelve acum actualizado."""
    from scipy.spatial import cKDTree  # noqa: PLC0415
    acum = dict(acum or {})
    n0 = n0 or len(model.tex)
    Pd, _ = dm.posed(pose_de(dm))
    hc_todos, dc = _tris_cara(model), _tris_cara(dm)
    if not hc_todos or not dc:
        return acum
    placa = lambda m, i: "FACE" in _clave_tri(m, i)  # noqa: E731
    Lh, Wh, ih = _local_head(model, P)
    Ld, Wd, idd = _local_head(dm, Pd)
    if Lh is None or Ld is None:
        return acum
    zb = [_prof_boca(dm, Wd, idd), _prof_boca(model, Wh, ih)]
    Ld = Ld * (zb[1] / zb[0] if None not in zb else 1.0)
    # alineado fino por marcas (ojos y boca): en Vegeta los rasgos van mas juntos y mas bajos que en Goku
    mh, md = _marcas_cara(model, Lh, hc_todos, Wh, ih), _marcas_cara(dm, Ld, dc, Wd, idd, zb[1] / zb[0] if None not in zb else 1.0)
    if mh and md:
        (eh, bh), (ed, bd) = mh, md
        sx = abs(eh[0][0] - eh[1][0]) / max(abs(ed[0][0] - ed[1][0]), 1e-6)
        ey_h, ey_d = (eh[0][1] + eh[1][1]) / 2, (ed[0][1] + ed[1][1]) / 2
        sy = (ey_h - bh[1]) / max(ey_d - bd[1], 1e-6)
        if 0.6 < sx < 1.6 and 0.6 < sy < 1.6:
            ez_h, ez_d = (eh[0][2] + eh[1][2]) / 2, (ed[0][2] + ed[1][2]) / 2   # fondo: el de los ojos (Gohan: boca saliente)
            Ld = np.stack([Ld[:, 0] * sx, (Ld[:, 1] - bd[1]) * sy + bh[1], (Ld[:, 2] - ez_d) * sx + ez_h], 1)
    # muestras densas de la cara del donante: punto, normal y UV
    n = 14
    g = [(i / n, j / n) for i in range(n + 1) for j in range(n + 1 - i)]
    bar = np.array([(1 - a - b, a, b) for a, b in g])
    def muestras(tris):
        pts, nrm, uvs, txs, tri_de = [], [], [], [], []
        for t in tris:
            v = dm.tris[t]
            q = Ld[v]
            nn = np.cross(q[1] - q[0], q[2] - q[0])
            nn /= max(np.linalg.norm(nn), 1e-9)
            pts.append(bar @ q); uvs.append(bar @ (dm.uv[v] % 1.0)); nrm.append(np.repeat(nn[None], len(bar), 0))
            txs.append(np.full(len(bar), dm.mats[dm.tri_mat[t]]["tex"]))
            tri_de.append(np.full(len(bar), t))
        return np.concatenate(pts), np.concatenate(nrm), np.concatenate(uvs), np.concatenate(txs), np.concatenate(tri_de)

    for th in sorted({model.mats[model.tri_mat[i]]["tex"] for i in hc_todos
                      if min(model.tex[model.mats[model.tri_mat[i]]["tex"]].shape[:2]) >= 128}):
        hc = [i for i in hc_todos if model.mats[model.tri_mat[i]]["tex"] == th]
        hgt, wid = model.tex[th].shape[:2]
        r = _rasterizar_uv(model, hc, wid, hgt)
        if r is None:
            continue
        ys, xs, ti, bs = r
        v3 = model.tris[ti]
        X = np.einsum("ij,ijk->ik", bs, Lh[v3])
        nh = np.cross(Lh[v3[:, 1]] - Lh[v3[:, 0]], Lh[v3[:, 2]] - Lh[v3[:, 0]])
        nh /= np.maximum(np.linalg.norm(nh, axis=1), 1e-9)[:, None]
        if gesto and mh:   # rostro propio: de donde sale cada texel en la cara del donante
            X = _gesto(X, mh[0], gesto)
        col = np.zeros((len(ys), 4), np.float32)
        capa_h = np.array([placa(model, int(t)) for t in ti])
        for capa in (False, True):
            dsel = [t for t in dc if placa(dm, t) == capa] or dc
            m_h = capa_h == capa
            if not m_h.any():
                continue
            pts, nrm, uvs, txs, tri_de = muestras(dsel)
            d, k = cKDTree(pts).query(X[m_h], k=8)
            ok = np.einsum("ijk,ik->ij", nrm[k], nh[m_h]) > 0.2   # misma cara de la superficie (no la de detras)
            sel = np.where(ok.any(1), ok.argmax(1), 0)
            tri_k = tri_de[k[np.arange(len(k)), sel]]
            # rayo por la normal del cuerpo: de las capas del donante que corta, la mas EXTERIOR (la que se ve; Vegeta tiene
            # una boca gruesa que su piel tapa). Sin corte (bordes), el triangulo de la muestra mas cercana
            tri_k = _rayo_exterior(Ld, dm.tris[np.asarray(dsel)], np.asarray(dsel), X[m_h], nh[m_h], tri_k)
            tv = dm.tris[tri_k]
            A, B, C = Ld[tv[:, 0]], Ld[tv[:, 1]], Ld[tv[:, 2]]
            v0, v1, v2 = B - A, C - A, X[m_h] - A
            d00, d01, d11 = (v0 * v0).sum(1), (v0 * v1).sum(1), (v1 * v1).sum(1)
            d20, d21 = (v2 * v0).sum(1), (v2 * v1).sum(1)
            den = np.maximum(d00 * d11 - d01 * d01, 1e-12)
            b1, b2 = (d11 * d20 - d01 * d21) / den, (d00 * d21 - d01 * d20) / den
            b1, b2 = np.clip(b1, 0, 1), np.clip(b2, 0, 1)
            sobra = np.maximum(b1 + b2 - 1, 0) / 2
            b1, b2 = b1 - sobra, b2 - sobra
            uvt = dm.uv[tv] % 1.0
            uh = (1 - b1 - b2)[:, None] * uvt[:, 0] + b1[:, None] * uvt[:, 1] + b2[:, None] * uvt[:, 2]
            k = tri_k   # (para la textura de cada texel)
            txs = np.array([dm.mats[dm.tri_mat[t]]["tex"] for t in range(len(dm.tris))])
            c = np.zeros((len(k), 4), np.float32)
            for tx in np.unique(txs[k]):
                mm = txs[k] == tx
                src = dm.tex[int(tx)]
                sh, sw = src.shape[:2]
                u = uh[mm]
                fx, fy = u[:, 0] * sw - 0.5, u[:, 1] * sh - 0.5   # bilineal (con el mas cercano salia punteado)
                x0, y0 = np.floor(fx).astype(int), np.floor(fy).astype(int)
                ax, ay = (fx - x0)[:, None], (fy - y0)[:, None]
                g = lambda yy, xx: src[np.clip(yy, 0, sh - 1), np.clip(xx, 0, sw - 1)]  # noqa: E731
                c[mm] = ((1 - ay) * ((1 - ax) * g(y0, x0) + ax * g(y0, x0 + 1))
                         + ay * ((1 - ax) * g(y0 + 1, x0) + ax * g(y0 + 1, x0 + 1)))
            col[m_h] = c
        # la rampa de la piel del donante -> la del cuerpo (por banda: el alfa es la banda toon)
        rd, rh = _rampa_piel(dm), _rampa_piel(model)
        if rd is not None and rh is not None:
            col[:, :3] = np.clip(col[:, :3] - rd + rh, 0, 1)
        base = th % n0
        out = np.asarray(acum.get(base, model.tex[th]), np.float32).copy()
        # piel primero y la placa encima: en Goku las dos capas usan la MISMA zona de textura para los ojos y la piel (que en
        # el juego queda detras) pisaba los ojos de la placa
        for capa in (False, True):
            m_ = capa_h == capa
            out[ys[m_], xs[m_]] = col[m_]
        acum[base] = out
    return acum


# Rostros PROPIOS (25/10): ojos REALES de B3 (los del juego del jugador) con otro gesto. El usuario pidio tipos tipo
# "confiado, desafiante..." en el estilo de la serie; dibujados a mano no se parecian (trazo y proporciones de Toriyama),
# asi que cada tipo parte de los ojos de un personaje nativo y los transforma (el conjunto ojo + ceja de cada lado, que
# en B3 se tocan en el lagrimal): sy = abrir (>1) / entornar (<1) respecto al centro del ojo; k = inclinacion (> 0 baja
# el lado de la nariz y sube el de fuera: mas enfadado); ty = subir/bajar. "der" = el ojo derecho distinto (una ceja
# alzada; sin uso: se veia deforme). Marco HEAD, como la proyeccion.
OJOS_TIPOS = {
    "A": {"nombre": ("Confiado", "Confident"), "fid": 264, "sy": 1.06, "k": -0.12, "ty": 0.012},
    "B": {"nombre": ("Desafiante", "Defiant"), "fid": 264, "sy": 0.8, "k": 0.2, "ty": -0.01},
    "C": {"nombre": ("Sereno", "Calm"), "fid": 235, "sy": 1.1, "k": -0.2, "ty": 0.015},
    "D": {"nombre": ("Feroz", "Fierce"), "fid": 416, "sy": 0.88, "k": 0.14, "ty": 0.0},
    "E": {"nombre": ("Burlón", "Smug"), "fid": 99, "sy": 0.84, "k": -0.04, "ty": 0.0},   # simetrico: con una ceja
    # alzada parecia un ojo mas alto que el otro (usuario 25/10)
}


def _gesto(X, ojos, g):
    """Puntos de origen (en la cara del donante) de los texeles X para el gesto `g` (transformacion inversa). Afecta al
    conjunto ojo + ceja de cada lado y se desvanece hacia la piel de alrededor."""
    Q = X.copy()
    for ojo, lado in ((ojos[0], 1), (ojos[1], -1)):
        p = g.get("der", g) if lado < 0 else g
        sy, k, ty = p.get("sy", 1.0), p.get("k", 0.0), p.get("ty", 0.0)
        ex, ey = float(ojo[0]), float(ojo[1])
        m = np.sign(X[:, 0]) == lado
        lx = (X[m, 0] - ex) * lado          # hacia fuera > 0
        ly = X[m, 1] - ey
        f = lambda v, a, b: np.clip((b - np.abs(v)) / (b - a), 0, 1)  # noqa: E731
        w = f(lx, 0.5, 0.8) * np.where(ly > 0, f(ly, 0.55, 0.8), f(ly, 0.25, 0.45))
        sy_ = 1 + (sy - 1) * w
        Q[m, 1] = X[m, 1] - w * ty - w * k * lx
        Q[m, 1] = ey + (Q[m, 1] - ey) / sy_
    return Q


SSJ_CEJAS, SSJ_OJOS = (0.98, 0.86, 0.3), (0.2, 0.62, 0.7)   # Super Saiyan: cejas rubias, ojos turquesa (B3 266/417)


def _muestras_cara(model, P, n0, acum_m):
    """Texeles de las texturas de cara que pinta este modelo (con su expresion puesta), con su posicion en el marco HEAD:
    acum_m[indice base] = {(y, x): (x3, alto, fondo)}. Cada textura de cara de B3 lleva dos juegos de ojos (cada expresion usa
    uno); juntando las de todas las expresiones se colorea cada textura entera de una vez."""
    Lh, _, _ = _local_head(model, P)
    if Lh is None:
        return acum_m
    hc_todos = _tris_cara(model)
    for th in {model.mats[model.tri_mat[i]]["tex"] for i in hc_todos}:
        if min(model.tex[th].shape[:2]) < 128:
            continue
        hc = [i for i in hc_todos if model.mats[model.tri_mat[i]]["tex"] == th]
        hgt, wid = model.tex[th].shape[:2]
        r = _rasterizar_uv(model, hc, wid, hgt)
        if r is None:
            continue
        ys, xs, ti, bs = r
        X = np.einsum("ij,ijk->ik", bs, Lh[model.tris[ti]])
        d = acum_m.setdefault(th % n0, {})
        for y, x, xx, yy, zz in zip(ys.tolist(), xs.tolist(), X[:, 0].tolist(), X[:, 1].tolist(), X[:, 2].tolist()):
            d[(y, x)] = (xx, yy, zz)
    return acum_m


def colorear_textura(out, muestras, iris_rgb=None, ceja_rgb=None, sin_cejas=False, depura=None, cejas3d=None,
                     alto_ceja=0.6):
    """Color de pupilas y cejas en una textura de cara de B3 (rgba float, la cara dibujada derecha: arriba = arriba).
    25/10 (3.a version; las dos primeras mezclaban parpado, ceja y pupila y solo hacian un juego de ojos):
      - cada OJO = una mancha del blanco del ojo (gris claro 0,957, distinto de la piel blanca 1,0) delante de la cara
      - pupila = lo negro con blanco del ojo a izquierda, derecha y debajo (el parpado no lo tiene debajo); se completa
        creciendo por lo negro SOLO dentro de la envolvente del blanco de ese ojo (no se sale al parpado ni a la ceja)
      - ceja = lo negro por encima del ojo (en la misma columna, o la mas cercana del ojo) y separado del blanco mas de un
        grosor de parpado; las cejas rayadas de Goku se cierran en una forma solida y se rellenan dejando el contorno
        negro, como las cejas de las caras SSJ nativas. SSJ3 (`sin_cejas`): la ceja pasa a piel.
      - sin ojos abiertos (parpadeo, dolor): la ceja es lo negro que cae donde estan las cejas de las caras con los ojos
        abiertos del mismo modelo (`cejas3d`: puntos (x, alto) del marco HEAD; la cabeza es la misma en todas las
        expresiones). En Vegeta y el tipo D ceja y parpado cerrado se tocan: por altura no se separaban.
    Devuelve (textura, puntos (x, alto) de las cejas halladas sobre ojos abiertos)."""
    from scipy import ndimage  # noqa: PLC0415
    from scipy.spatial import ConvexHull, Delaunay  # noqa: PLC0415
    hgt, wid = out.shape[:2]
    img = out[..., :3]
    cara = np.zeros((hgt, wid), bool)
    alto = np.full((hgt, wid), np.nan, np.float32)
    x3 = np.full((hgt, wid), np.nan, np.float32)
    for (y, x), (xx_, yy, zz) in muestras.items():
        if zz > 0.1:   # la punta de la ceja rodea la sien (fondo < 0,35)
            cara[y, x] = True
            alto[y, x] = yy
            x3[y, x] = xx_
    dk = (img.max(2) < 0.35) & cara
    bl = (img.min(2) > 0.72) & (np.ptp(img, 2) < 0.08) & (img.max(2) < 0.985) & cara
    et, n = ndimage.label(bl)
    tam = ndimage.sum(np.ones_like(et), et, range(1, n + 1)) if n else np.zeros(0)
    # ojo = mancha grande (los brillos grises sueltos de la piel no); con los ojos cerrados no hay ninguno
    ojos = [k + 1 for k in np.flatnonzero(tam >= max(40, 0.0015 * wid * hgt))]   # (los entornados del tipo D son finos)
    N = max(3, wid // 40)
    L = max(1, wid // 128)   # parpado = solo la linea de contorno (usuario 25/10: la parte baja de la ceja quedaba negra)

    def cerca(mask, dy, dx):
        r_ = np.zeros_like(mask)
        for k in range(1, N + 1):
            r_ |= np.roll(mask, (dy * k, dx * k), (0, 1))
        return r_
    pup = np.zeros_like(dk)
    cej = np.zeros_like(dk)
    gy, gx = np.mgrid[0:hgt, 0:wid]
    for k in ojos:
        ojo = et == k
        yy, xx = np.nonzero(ojo)
        if len(yy) < 3 or np.ptp(xx) < 2 or np.ptp(yy) < 2:
            continue
        pts = np.stack([xx, yy], 1).astype(float)
        try:
            hull = Delaunay(pts[ConvexHull(pts).vertices])
        except Exception:   # noqa: BLE001 (ojo degenerado)
            continue
        x0, x1, y0, y1 = xx.min(), xx.max(), yy.min(), yy.max()
        caja = (gx >= x0 - 1) & (gx <= x1 + 1) & (gy >= y0 - 1) & (gy <= y1 + 1)
        dentro = np.zeros_like(dk)
        dentro[caja] = hull.find_simplex(np.stack([gx[caja], gy[caja]], 1)) >= 0
        # (con blanco debajo O encima: hay pupilas pegadas al parpado y otras al borde de abajo)
        nucleo = dk & dentro & cerca(ojo, 0, 1) & cerca(ojo, 0, -1) & (cerca(ojo, -1, 0) | cerca(ojo, 1, 0))
        p_ = nucleo.copy()
        for _ in range(N):
            p_ |= ndimage.binary_dilation(p_) & dk & dentro
        pup |= ndimage.binary_opening(p_, np.ones((2, 2), bool))   # sin el contorno del ojo (lineas de 1 texel)
        # ceja: por columnas, lo negro por encima del borde de arriba del blanco (pasado el parpado)
        m = int(0.35 * (x1 - x0)) + 2
        tope = np.full(wid, -1)
        for x in range(x0, x1 + 1):
            c = yy[xx == x]
            if len(c):
                tope[x] = c.min()
        vale = np.flatnonzero(tope >= 0)
        hmax = int(1.4 * (y1 - y0)) + 4
        for x in range(max(0, x0 - m), min(wid, x1 + m + 1)):
            if abs(x - x0) <= 1 or abs(x - x1) <= 1:   # los lados de la caja del ojo (el cierre de abajo rellena la ceja)
                continue
            # fuera del ancho del ojo, solo por encima de su punto mas alto (el contorno vertical del lado no es ceja)
            t = tope[x] if tope[x] >= 0 else y0
            lo = max(0, t - hmax)
            ys_ = np.arange(lo, max(lo, t - L))
            if len(ys_):
                cej[ys_[dk[ys_, x]], x] = True
    # parpado = lo negro pegado encima del blanco de un OJO (no de los brillos grises sueltos dentro de la ceja)
    cej &= ~pup & ~ndimage.binary_dilation(np.isin(et, ojos), np.ones((2 * L + 1, 1), bool), origin=(-L, 0))
    cej_ojos = cej.copy()
    zona_ojos = ndimage.binary_dilation(np.isin(et, ojos), iterations=max(4, wid // 8)) if ojos else np.zeros_like(dk)
    libre = dk & cara & ~zona_ojos
    if cejas3d is not None and libre.any():
        yy, xx = np.nonzero(libre)
        d, _ = cejas3d.query(np.stack([x3[yy, xx], alto[yy, xx]], 1))
        cerca3 = np.zeros_like(dk)
        cerca3[yy, xx] = d < 0.035
        # las rayadas se cierran solo dentro de la zona de la ceja (no se pegan al parpado cerrado)
        zona3 = np.zeros_like(dk)
        cy_, cx_ = np.nonzero(cara & ~zona_ojos)
        d2, _ = cejas3d.query(np.stack([x3[cy_, cx_], alto[cy_, cx_]], 1))
        zona3[cy_, cx_] = d2 < 0.05
        cej |= ndimage.binary_closing(cerca3, np.ones((3, 3), bool)) & zona3 & ~bl
    if libre.any():
        # y por GROSOR: la ceja es el trazo grueso; el parpado cerrado, las arrugas y las rayas son lineas finas (en el
        # parpadeo de Vegeta y Gohan las cejas bajan y la referencia 3D de arriba no las alcanza)
        T = max(2, wid // 80)
        # (las cejas rayadas, como el parpadeo de Vegeta, se unen antes: solo huecos de 1-2 texeles)
        libre_c = ndimage.binary_closing(libre, np.ones((3, 3), bool)) & cara & ~zona_ojos & ~bl
        semillas = ndimage.distance_transform_edt(libre_c) >= T
        gruesa = semillas.copy()
        for _ in range(T + 1):
            gruesa |= ndimage.binary_dilation(gruesa) & libre_c
        en, nn = ndimage.label(gruesa)
        cej_g = np.zeros_like(dk)
        for k in range(1, nn + 1):
            mk = en == k
            if mk.sum() >= max(40, 0.0009 * wid * hgt) and np.nanmedian(alto[mk]) >= 0.3:
                cej_g |= mk
        cej |= cej_g
        # cejas finas (parpadeo de Vegeta y del tipo D): en cada cara de la textura sin ojos abiertos y cada lado, el trazo
        # grande mas ALTO (el parpado cerrado, las arrugas y las rayas quedan debajo)
        grupos, ng = ndimage.label(ndimage.binary_dilation(libre, iterations=max(3, wid // 16)))
        trazos, nt = ndimage.label(libre_c)
        cerca_ojos = ndimage.binary_dilation(zona_ojos, iterations=4) if ojos else np.zeros_like(dk)
        for gk in range(1, ng + 1):
            g_ = grupos == gk
            if (g_ & cerca_ojos).any():   # solo caras sin ojos abiertos (en las otras, encima hay arrugas de la frente)
                continue
            for lado in (1, -1):
                gl = g_ & (np.sign(np.nan_to_num(x3)) == lado)
                if (cej_g & gl).sum() >= max(25, 0.0005 * wid * hgt):   # ya tiene su ceja gruesa
                    continue
                mejor, al_m = None, -9.0
                for k in np.unique(trazos[gl & libre_c]):
                    mk = (trazos == k) & gl
                    if k and mk.sum() >= max(25, 0.0005 * wid * hgt):
                        a_ = float(np.nanmedian(alto[mk]))
                        if a_ > al_m:
                            mejor, al_m = mk, a_
                if mejor is not None and al_m >= 0.3:
                    cej |= mejor
    # solo las manchas grandes (las rayas de la sien, las arrugas del entrecejo y las comisuras no son ceja)
    ec, nc = ndimage.label(ndimage.binary_closing(cej, np.ones((3, 3), bool), iterations=2) & ~bl & cara)
    if nc:
        tc = ndimage.sum(np.ones_like(ec), ec, range(1, nc + 1))
        # (tamano fijo: con uno relativo al mayor, las cejas finas de un parpadeo caian frente a las gruesas de otra cara)
        cej = np.isin(ec, 1 + np.flatnonzero(tc >= max(20, 0.0005 * wid * hgt)))
    cej = ndimage.binary_fill_holes(cej) & ~bl & ~pup   # sin huecos de piel dentro de la ceja
    cej_ojos &= cej
    puntos = np.stack([x3[cej_ojos], alto[cej_ojos]], 1) if cej_ojos.any() else np.zeros((0, 2))
    if depura:
        dbg = (np.clip(img, 0, 1) * 255).astype(np.uint8).copy()
        dbg[pup] = (255, 0, 0)
        dbg[cej] = (0, 200, 0)
        Image.fromarray(dbg).resize((wid * 3, hgt * 3), Image.NEAREST).save(depura)
    if iris_rgb is not None and pup.any():
        iris = np.asarray(iris_rgb, np.float32)
        ep, ne = ndimage.label(pup)
        for k in range(1, ne + 1):
            yy, xx = np.nonzero(ep == k)
            cy, cx = yy.mean(), xx.mean()
            rr = max(float(np.hypot(yy - cy, xx - cx).max()), 1.0)
            d = (np.hypot(yy - cy, xx - cx) / rr)[:, None]
            out[yy, xx, :3] = np.where(d < 0.4, iris[None] * 0.15, iris[None] * (0.8 + 0.2 * d))
            out[yy, xx, 3] = 1.0
            if len(yy) >= 12:   # brillo, como los ojos de color nativos
                by, bx = int(round(cy - 0.4 * rr)), int(round(cx - 0.3 * rr))
                if 0 <= by < hgt and 0 <= bx < wid and ep[by, bx] == k:
                    out[by, bx, :3] = 1.0
    if (ceja_rgb is not None or sin_cejas) and cej.any():
        if sin_cejas:
            q = ndimage.binary_dilation(cej) & ~bl & ~pup & cara
            out[q, :3], out[q, 3] = 1.0, 0.0   # piel (la rampa del cuerpo le da el tono)
        else:
            relleno = ndimage.binary_erosion(cej)
            ek, nk = ndimage.label(cej)
            for k in range(1, nk + 1):   # cejas finas (no caben color y contorno): el trazo entero, algo mas oscuro
                mk = ek == k
                if (relleno & mk).sum() < 0.35 * mk.sum():
                    relleno |= mk
                    out[mk, :3] = np.asarray(ceja_rgb, np.float32) * 0.8
            out[cej & ~relleno, :3] = 0.03   # contorno negro
            gruesas = relleno & ~np.isin(ek, [k for k in range(1, nk + 1)
                                              if (ndimage.binary_erosion(ek == k) & (ek == k)).sum() < 0.35 * (ek == k).sum()])
            out[gruesas, :3] = np.asarray(ceja_rgb, np.float32)
            out[cej, 3] = 1.0
    return out, puntos


def texturas_rostro(host_bytes, donor_bytes=None, gesto=None, iris_rgb=None, ceja_rgb=None, sin_cejas=False,
                    solo_base=False):
    """Las texturas de cara del cuerpo para un tipo de rostro y unos colores, en TODAS sus expresiones: cada expresion
    del cuerpo (variantes *_Lnn_*_FACE: parpadeo, grito, dolor...; el juego cambia la placa de la cara por etiqueta)
    toma la misma expresion del donante (o su cara normal si no la tiene). Sin esto, al cambiar de gesto en combate
    volvia la cara del cuerpo. Devuelve {indice de textura del #AMB: rgba float}. `solo_base`: solo la cara normal
    (vista previa del menu)."""
    import re  # noqa: PLC0415
    base = mr.Model(bytes(host_bytes))
    n0 = len(base.tex)
    etiquetas = [None] + ([] if solo_base else sorted(l for l in base.variants() if "FACE" in l))
    dvars = mr.Model(bytes(donor_bytes)).variants() if donor_bytes else {}
    acum, muestras = {}, {}
    for lab in etiquetas:
        hm = mr.Model(bytes(host_bytes))
        if lab and not hm.use_variant(lab):
            continue
        P, _ = hm.posed(pose_de(hm))
        if donor_bytes:
            dm = mr.Model(bytes(donor_bytes))
            if lab:
                num = re.search(r"_L(\d\d)_", lab)
                dl = next((l for l in dvars if num and "_L%s_" % num.group(1) in l and "FACE" in l), None)
                if dl:
                    dm.use_variant(dl)
            acum = rostro_texturas(hm, P, dm, gesto, acum, n0)
        if iris_rgb is not None or ceja_rgb is not None or sin_cejas:
            muestras = _muestras_cara(hm, P, n0, muestras)
    dep = os.environ.get("DBZ3_DEBUG_CARA")
    tex_de = lambda t: np.asarray(acum.get(t, base.tex[t]), np.float32).copy()  # noqa: E731
    # 1.a pasada: donde estan las cejas en las caras con los ojos abiertos (para las de ojos cerrados)
    pts = [colorear_textura(tex_de(t), m_)[1] for t, m_ in muestras.items()]
    pts = np.concatenate(pts) if pts else np.zeros((0, 2))
    from scipy.spatial import cKDTree  # noqa: PLC0415
    arbol = cKDTree(pts) if len(pts) >= 10 else None
    for t, m_ in muestras.items():   # color: una vez por textura, con sus dos juegos de ojos
        acum[t] = colorear_textura(tex_de(t), m_, iris_rgb, ceja_rgb, sin_cejas,
                                   dep.replace(".png", "_%d.png" % t) if dep else None, arbol)[0]
    return acum


def _rayo_exterior(L, tris, ids, X, n, defecto, alcance=0.35, trozo=1024):
    """Por cada punto X con normal n: el triangulo (de `tris`, ids `ids`) que corta la recta X + t n con |t| < alcance y
    mayor t (la capa mas exterior); si no corta ninguno, `defecto`. Moller-Trumbore vectorizado."""
    A, B, C = L[tris[:, 0]], L[tris[:, 1]], L[tris[:, 2]]
    e1, e2 = B - A, C - A
    out = np.asarray(defecto).copy()
    for i in range(0, len(X), trozo):
        o, d = X[i:i + trozo, None, :], n[i:i + trozo, None, :]
        p = np.cross(d, e2[None])
        det = (e1[None] * p).sum(2)
        inv = 1.0 / np.where(np.abs(det) < 1e-12, np.nan, det)
        s = o - A[None]
        u = (s * p).sum(2) * inv
        q = np.cross(s, e1[None])
        v = (d * q).sum(2) * inv
        t = (e2[None] * q).sum(2) * inv
        hit = (u >= -1e-4) & (v >= -1e-4) & (u + v <= 1 + 1e-4) & (np.abs(t) < alcance)
        t = np.where(hit, t, -np.inf)
        j = t.argmax(1)
        tiene = np.isfinite(t[np.arange(len(j)), j])
        out[i:i + trozo][tiene] = ids[j[tiene]]
    return out


def _marcas_cara(model, L, tris, W, inv, escala=1.0):
    """((ojo izq, ojo der), boca) en el marco HEAD: los ojos = centro del blanco de los ojos (texeles claros y grises de la
    cara, por delante y por encima de la boca, por lados); la boca = comisuras (huesos). None si no se encuentran."""
    ps = [W[i, :3, 3] @ inv[:3, :3].T + inv[:3, 3] for i, l in enumerate(model.labels)
          if _clave_hueso_b3(l) in ("M_LMOUTH1", "M_RMOUTH1")]
    if len(ps) != 2:
        return None
    boca = np.mean(ps, 0) * escala
    pts = []
    for tx in {model.mats[model.tri_mat[t]]["tex"] for t in tris}:
        if tx >= len(model.tex):
            continue
        h, w = model.tex[tx].shape[:2]
        tt = [t for t in tris if model.mats[model.tri_mat[t]]["tex"] == tx]
        r = _rasterizar_uv(model, tt, w, h)
        if r is None:
            continue
        ys, xs, ti, bs = r
        c = model.tex[tx][ys, xs, :3]
        blanco = (c.min(1) > 0.72) & (c.max(1) - c.min(1) < 0.08) & (c.max(1) < 0.985)
        X = np.einsum("ij,ijk->ik", bs[blanco], L[model.tris[ti[blanco]]])
        pts.append(X[(X[:, 1] > boca[1] + 0.2) & (X[:, 2] > boca[2] * 0.5)])
    if not pts:
        return None
    X = np.concatenate(pts)
    izq, der = X[X[:, 0] > 0.15], X[X[:, 0] < -0.15]
    if len(izq) < 8 or len(der) < 8:
        return None
    return (np.median(izq, 0), np.median(der, 0)), boca


def _prof_boca(model, W, inv_head):
    """Distancia media HEAD -> comisuras de la boca (M_LMOUTH1/M_RMOUTH1) en el marco HEAD; None si faltan."""
    ps = [W[i, :3, 3] @ inv_head[:3, :3].T + inv_head[:3, 3] for i, l in enumerate(model.labels)
          if _clave_hueso_b3(l) in ("M_LMOUTH1", "M_RMOUTH1")]
    return float(np.mean([np.linalg.norm(p) for p in ps])) if len(ps) == 2 else None


ENCAJE_OJOS = True
PELO_FUERA = False   # (medido 25/10: no mejora Yamcha ni Krilin y empeora Vegeta)


def _pelo_fuera_de_cara(model, P, zonas, Q, tris):
    """25/10 (medido: la frente de Goku es mas alta que la de Yamcha o Krilin y atravesaba sus mechones delanteros, el
    "recorte"): cada vertice del peinado que queda dentro o pegado a la cara del heroe sale por la normal de la cara hasta
    una holgura minima; el resto no se mueve (_inflar, por vertice y suavizado)."""
    hface = [i for i, z in enumerate(zonas) if z == "cara"]
    if not PELO_FUERA or not hface:
        return Q
    hv = np.unique(model.tris[hface].reshape(-1))
    _, Nh = model.posed(pose_de(model))
    return _inflar(Q, tris, P[hv], Nh[hv], float(np.ptp(P[:, 1])), sueltas_fijas=False)


def _encaje_ojos(dm, Pd, model, P):
    """(escala, centro de los ojos del donante, centro de los ojos del heroe) en los marcos HEAD: el peinado se lleva
    con la cara, con la escala de la distancia entre ojos y anclado en su punto medio. None sin ojos legibles."""
    md, mh = _local_head(dm, Pd), _local_head(model, P)
    if md[0] is None or mh[0] is None:
        return None
    kd = _marcas_cara(dm, md[0], _tris_cara(dm), md[1], md[2])
    kh = _marcas_cara(model, mh[0], _tris_cara(model), mh[1], mh[2])
    if not kd or not kh:
        return None
    (d1, d2), _ = kd
    (h1, h2), _ = kh
    return float(np.linalg.norm(h1 - h2) / max(np.linalg.norm(d1 - d2), 1e-9)), (d1 + d2) / 2, (h1 + h2) / 2


def _ancho_cabeza(model, P, zonas):
    """Ancho del craneo de PIEL en el marco HEAD (sin pelo ni accesorios): la medida con la que se escala un
    peinado donante. Se toma por encima de la linea de los ojos (craneo que rodea el pelo), asi la cinta/cicatriz de
    Bardock, las orejas o la espada de Trunks no la contaminan. Devuelve None si no hay cara legible."""
    if P is None or not len(model.tris):
        return None
    L, W, inv = _local_head(model, P)
    if L is None:
        return None
    skin = _es_piel(model)
    cara = [i for i, z in enumerate(zonas) if z == "cara" and i < len(skin) and skin[i]]
    if not cara:
        return None
    q = L[np.unique(model.tris[cara].reshape(-1))]
    mk = _marcas_cara(model, L, _tris_cara(model), W, inv)
    ey = (mk[0][0][1] + mk[0][1][1]) / 2 if mk else float(np.median(q[:, 1]))
    arriba = q[q[:, 1] > ey + 0.1]
    if len(arriba) < 16:   # calvo o craneo sin piel por encima de los ojos: toda la cara de piel
        arriba = q
    return float(arriba[:, 0].max() - arriba[:, 0].min())


def _pelo_donante(model, P, zonas, donor_bytes):
    """Transfiere el pelo entre marcos HEAD y ajusta su escala a la cabeza del cuerpo anfitrión."""
    dm = mr.Model(bytes(donor_bytes))
    pose = pose_de(dm)
    Pd, Nd = dm.posed(pose)
    zd = zonas_ficha(dm, Pd, pelo_color=True)
    ht = [i for i, z in enumerate(zd) if z == "pelo"]
    if not ht:
        return None
    dvs = np.unique(dm.tris[ht].reshape(-1))
    # La pieza se normaliza con el ANCHO DEL CRANEO DE PIEL en espacio HEAD y luego se coloca con el transform HEAD
    # del anfitrión. No se usa el bbox del pelo, que cambia mucho entre peinados y era la causa de que algunos flotaran.
    dh = dm.bone("HEAD")
    hh = model.bone("HEAD")
    if dh is None or hh is None:
        return None
    dw, hw = np.asarray(dm.worlds(pose)), np.asarray(model.worlds(pose_de(model)))
    try:
        inv_dhead = np.linalg.inv(dw[dh])
        inv_hhead = np.linalg.inv(hw[hh])
    except np.linalg.LinAlgError:
        return None
    dface = [i for i, z in enumerate(zd) if z == "cara"]
    hface = [i for i, z in enumerate(zonas) if z == "cara"]
    if not dface or not hface:
        return None
    dv = np.unique(dm.tris[dface].reshape(-1))
    hv = np.unique(model.tris[hface].reshape(-1))
    dlocal = Pd[dv] @ inv_dhead[:3, :3].T + inv_dhead[:3, 3]
    hlocal = P[hv] @ inv_hhead[:3, :3].T + inv_hhead[:3, 3]
    ext_d = np.maximum(dlocal.max(0) - dlocal.min(0), 1e-6)
    ext_h = np.maximum(hlocal.max(0) - hlocal.min(0), 1e-6)
    escala = ext_h / ext_d
    # 25/10: la caja de la cara no sirve de medida (cinta y cicatriz de Bardock, orejas: Bardock, con la misma cabeza que
    # Goku, salia al 0,65-0,8). Medida limpia: la profundidad de las comisuras (huesos M_*MOUTH1) en el marco HEAD, escala
    # uniforme (el craneo no se deforma)
    zb = [_prof_boca(dm, dw, inv_dhead), _prof_boca(model, hw, inv_hhead)]
    if None not in zb:
        escala = np.full(3, zb[1] / zb[0])
    # 25/10 (DS80): la escala por la distancia entre OJOS variaba hasta un 19 % entre cabezas del mismo tamano
    # (Bardock 1,188; Yamcha 1,145; Krilin 1,054; Goku nino 0,885) y el pelo se separaba del craneo. La medida
    # estable es el ANCHO del craneo de piel en el marco HEAD (sin pelo ni accesorios): 0,875..1,025 en los 10.
    # El ancla sigue siendo el punto medio de los ojos (sitio); lo que cambia es la escala y, con ella, el offset.
    ancho_d = _ancho_cabeza(dm, Pd, zd)
    ancho_h = _ancho_cabeza(model, P, zonas)
    s_cab = ancho_h / ancho_d if ancho_d and ancho_h else None
    if s_cab is not None and not (0.5 < s_cab < 1.8):
        s_cab = None
    # 27/10 DS133: se retira la compensacion DS94 (+10 % para craneos cortos, Goku nino) porque la medida nueva la
    # refuta: con ella el pelo solo (sin piel) da IoU 0,890/0,875/0,783/0,887 (frente/3-4/perfil/espalda) frente a
    # 1,000/1,000/1,000/1,000 sin ella, y compuesto la frente sube 0,751 -> 0,898 y la piel de frente baja +8.654 px
    # -> -1.326 px. La causa era el ancla: escalar +10 % alrededor del punto superior de la piel (no de los ojos)
    # levantaba y atrasaba el pelo (raiz +0,29 u detras). La escala por ancho de craneo (DS80) ya conserva la
    # proporcion pelo/craneo; el craneo corto del nino no pide +10 %.
    local_hair = Pd[dvs] @ inv_dhead[:3, :3].T + inv_dhead[:3, 3]
    don_local = local_hair.copy()
    ojos = _encaje_ojos(dm, Pd, model, P) if ENCAJE_OJOS else None
    if ojos:   # 25/10: tamano y sitio por los OJOS; DS80: la escala sale del ancho de craneo (los ojos solo anclan)
        s_o, od, oh = ojos
        if s_cab is not None:
            s_o = s_cab
        escala = np.full(3, s_o)
        local_hair = (local_hair - od) * s_o + oh
    else:
        if s_cab is not None:
            escala = np.full(3, s_cab)
        local_hair *= escala[None, :]
    Q = local_hair @ hw[hh, :3, :3].T + hw[hh, :3, 3]
    # Normales: inversa-transpuesta del cambio entre los dos marcos HEAD.
    transform = hw[hh, :3, :3] @ np.diag(escala) @ inv_dhead[:3, :3]
    QN = Nd[dvs] @ np.linalg.inv(transform)
    QN /= np.maximum(np.linalg.norm(QN, axis=1), 1e-9)[:, None]
    remap = {int(v): i for i, v in enumerate(dvs)}
    tris = [tuple(remap[int(v)] for v in dm.tris[t]) for t in ht]
    # 27/10 DS133: asentar la raiz. Si el pelo queda separado del craneo del heroe mas que en su donante (medido
    # con la superficie de piel de ambos en marco HEAD), se avanza solo el borde delantero hasta que deja de quedar
    # detras de la cara (Vegeta 416: +0,155 de holgura, el mayor de los 10; el siguiente de los 10 es 0,075).
    raiz = _raiz_pelo(model, P, zonas, dm, Pd, zd, don_local, local_hair)
    info_raiz = None
    if raiz is not None and raiz > RAIZ_UMBRAL:
        Q, _r = _asentar_franja(model, P, zonas, Q, tris, margen=RAIZ_MARGEN, tope=RAIZ_TOPE)
        info_raiz = {"holgura": round(float(raiz), 4), **(_r or {})}
    return dm, ht, dvs, Q, QN, tris, info_raiz


CALVA_FID = 327   # Krilin: craneo calvo nativo de B3
BAJO_CALVA = 0.05   # hasta donde baja por la nuca (fraccion del alto de la cara)
NUCA_ANCHO = 1.3
CALVA_DENTRO_CUELLO = True
RAMPA_NUCA = 0.2   # alto (fraccion de la cara) en el que el ensanche pasa de 0 a NUCA_ANCHO, desde media oreja
CUELLO_DELANTE = 0.1   # hasta donde llega el cuello de la calva hacia delante (fraccion del ancho de la cara)   # ensanche de la nuca a la altura del cuello de la ropa
CALVA_CUELLO = True
CORONA = 0.8   # desde aqui (fraccion del alto de la cara) la calva cubre tambien por delante
SIEN_DENTRO = 0.94
AJUSTE_CALVA = False   # (probado 25/10: con la calva o el pelo de Goku de referencia salian hundidos o enormes) apoyar cada peinado en el craneo (_ajustar_a_calva)


def _calva(model, P, zonas, afs):
    """Calva completa (25/10): el cuerpo base del heroe (Goku) no tiene craneo debajo de su pelo. Al ponerle otro
    peinado la nuca quedaba abierta y por detras se veia la cara por dentro (la boca en la nuca: Trunks, Bardock,
    Goten). Se pone el craneo NATIVO de Krilin (calvo de B3), de su HEAD al del cuerpo con escala uniforme (el ancho
    de la cabeza), solo lo que queda detras de la cara del cuerpo, sin orejas, y un 2,5 % hacia dentro para que el pelo
    lo tape. Devuelve (Q, QN, tris, rgb_piel) en espacio modelo del cuerpo, o None."""
    try:
        dm = mr.Model(bytes(afs.entry(CALVA_FID)))
    except Exception:   # noqa: BLE001 (sin Krilin legible: sin calva)
        return None
    pd = pose_de(dm)
    Pd, Nd = dm.posed(pd)
    zd = zonas_triangulos(dm)
    # cabeza + CUELLO de Krilin (huesos NECK/NW): el cuello entero y liso tapa por detras los bordes en dientes de
    # sierra de la cara y del cuello de Goku, que su pelo tapaba (el "hueco" del cuello)
    cuello = {i for i, tri in enumerate(dm.tris)
              if any(t in dm.labels[b] for b in dm.vbone[tri] if b < len(dm.labels) for t in ("NECK", "_NW"))}
    ht = [i for i, z in enumerate(zd) if z in ("cara", "pelo") or (CALVA_CUELLO and i in cuello)]
    hface = [i for i, z in enumerate(zonas) if z == "cara"]
    dh, hh = dm.bone("HEAD"), model.bone("HEAD")
    if not ht or not hface or dh is None or hh is None:
        return None
    dw, hw = np.asarray(dm.worlds(pd)), np.asarray(model.worlds(pose_de(model)))
    inv_d, inv_h = np.linalg.inv(dw[dh]), np.linalg.inv(hw[hh])
    hv = np.unique(model.tris[hface].reshape(-1))
    dv = np.unique(dm.tris[ht].reshape(-1))
    hl = P[hv] @ inv_h[:3, :3].T + inv_h[:3, 3]
    dl = Pd[dv] @ inv_d[:3, :3].T + inv_d[:3, 3]
    s = float(np.median((hl.max(0) - hl.min(0)) / np.maximum(dl.max(0) - dl.min(0), 1e-6)))
    Q = (dl * s) @ hw[hh, :3, :3].T + hw[hh, :3, 3]
    c, f, lado, ylo, yhi, ancho = _marco_cara(model, P, hface)
    remap = {int(v): i for i, v in enumerate(dv)}
    keep = []
    for t in ht:   # detras de la cara, hasta la nuca (a la altura de la barbilla: mas abajo sale un zigzag en el cuello) y sin orejas
        cc = Q[[remap[int(v)] for v in dm.tris[t]]].mean(0)
        # + sienes por dentro (de piel): tapan los huecos que la cara de Goku tiene donde iba su pelo
        frente = (ylo + 0.6 * (yhi - ylo) < cc[1] < ylo + 0.82 * (yhi - ylo)
                  and abs((cc - c) @ lado) > 0.2 * ancho)   # solo las sienes: la frente entera asomaba sobre el flequillo
        orejas_fuera = t in cuello or abs((cc - c) @ lado) < 0.46 * ancho   # el cuello va entero por los lados
        # coronilla delantera (25/10): por encima de la frente de Goku no habia nada y con entradas altas (Vegeta, Goten)
        # se veia el fondo a traves de la cabeza; va del color del pelo (de_pelo)
        corona = cc[1] > ylo + CORONA * (yhi - ylo)
        if ((cc - c) @ f < CUELLO_DELANTE * ancho * (t in cuello) or frente or corona) and cc[1] > ylo + BAJO_CALVA * (yhi - ylo) and orejas_fuera:
            keep.append(t)
    if not keep:
        return None
    cen = Q.mean(0)
    Q = cen + (Q - cen) * 0.975
    # las sienes, un poco mas adentro: asomaban por delante de la cara como cuadraditos con borde
    sien = np.zeros(len(Q), bool)
    for t in keep:
        cc = Q[[remap[int(v)] for v in dm.tris[t]]].mean(0)
        if (cc - c) @ f >= 0.1 * ancho and t not in cuello:
            sien[[remap[int(v)] for v in dm.tris[t]]] = True
    Q[sien] = cen + (Q[sien] - cen) * SIEN_DENTRO
    # la nuca: la cara de Goku acaba en el cuello con un borde en dientes de sierra que su pelo tapaba; el cuello de
    # Krilin es mas estrecho y lo dejaba a la vista (el "hueco" del cuello). Abajo se ensancha en horizontal hasta
    # NUCA_ANCHO, de forma gradual desde la oreja (el craneo no cambia)
    t_ = np.clip((ylo + 0.45 * (yhi - ylo) - Q[:, 1]) / (RAMPA_NUCA * (yhi - ylo)), 0, 1)
    k_ = 1 + (NUCA_ANCHO - 1) * t_
    Q[:, 0] = cen[0] + (Q[:, 0] - cen[0]) * k_
    Q[:, 2] = cen[2] + (Q[:, 2] - cen[2]) * k_
    corte = ylo + BAJO_CALVA * (yhi - ylo)   # borde de la nuca recto (con los triangulos enteros salia un zigzag en el cuello)
    Q[:, 1] = np.maximum(Q[:, 1], corte)
    QN = Nd[dv] @ np.linalg.inv(hw[hh, :3, :3] @ (s * inv_d[:3, :3]))
    QN /= np.maximum(np.linalg.norm(QN, axis=1), 1e-9)[:, None]
    tris = [tuple(remap[int(v)] for v in dm.tris[t]) for t in keep]
    # 25/10 (usuario: "dos picos en el cuello"; medido: el ensanche de la nuca sobresalia por los lados del cuello del
    # torso): por debajo del tope de ese cuello, la calva va siempre POR DENTRO de el (el ensanche sigue tapando el hueco
    # de mas arriba)
    tor = [i for i, z in enumerate(zonas) if z == 0]
    if CALVA_DENTRO_CUELLO and tor:
        tv = np.unique(model.tris[tor].reshape(-1))
        _, Nh = model.posed(pose_de(model))
        bajo_ = Q[:, 1] < ylo + 0.2 * (yhi - ylo)
        Q[bajo_] = _inflar(Q, tris, P[tv], Nh[tv], float(np.ptp(P[:, 1])), dentro=True, sueltas_fijas=False)[bajo_]
    # dos tonos: por encima del nacimiento del pelo (a media oreja) el craneo va del color del pelo, como los cuerpos de B3
    # (sin esto, entre los pinchos de Bardock o Goten asomaba piel); por debajo, la nuca, de piel
    nace = ylo + 0.45 * (yhi - ylo)
    def de_pelo(t):
        cc = Q[list(t)].mean(0)
        # la cara de Goku tiene huecos en las sienes (los tapaba su pelo): lo que se ve por ellos tiene que ser piel; de pelo
        # solo la parte de atras (lejos de la cara) y la coronilla
        prof = (cc - c) @ f
        return cc[1] > nace and (prof < -0.33 * ancho or (prof < 0 and cc[1] > ylo + 0.85 * (yhi - ylo))
                                 or cc[1] > ylo + CORONA * (yhi - ylo))
    alto = [t for t in tris if de_pelo(t)]
    bajo = [t for t in tris if not de_pelo(t)]
    # color: la mediana de la piel de la cara del cuerpo (misma regla de piel que tenir)
    pieles, cocidas = [], {}
    for i in hface:
        mat = model.mats[model.tri_mat[i]]
        if mat["tex"] not in cocidas:
            cocidas[mat["tex"]] = _textura_cocida(model, mat)
        t = cocidas[mat["tex"]]
        h, w = t.shape[:2]
        uv = model.uv[model.tris[i]].mean(0)
        px = t[int(np.clip((uv[1] % 1) * h, 0, h - 1)), int(np.clip((uv[0] % 1) * w, 0, w - 1)), :3]
        hh_, ss, vv = (float(np.ravel(x)[0]) for x in _rgb_hsv(px[None, :3]))
        if 10 <= hh_ <= 50 and 0.12 <= ss <= 0.62 and vv > 0.45:
            pieles.append(px)
    rgb = tuple(float(x) for x in np.median(pieles, 0)) if pieles else (0.93, 0.78, 0.6)
    tq, tn, tt = _tapas_sienes(model, P, zonas)
    if tt:   # van con la nuca (de piel)
        n0 = len(Q)
        Q, QN = np.vstack([Q, tq]), np.vstack([QN, tn])
        bajo = bajo + [tuple(n0 + i for i in t) for t in tt]
    Q, QN, alto, bajo = _simplificar(Q, QN, (alto, bajo), CALVA_CELDA * ancho, fijos=set(range(len(Q) - len(tq), len(Q))))
    return Q, QN, alto, bajo, rgb


CALVA_CELDA = 0.09   # lado de la rejilla de simplificacion de la calva (fraccion del ancho de la cara)


def _simplificar(Q, QN, listas, celda, fijos=()):
    """25/10 (tope de 3261 ventanas: pelo + calva + 3 prendas no cabian): agrupa los vertices de una malla de color liso
    (la calva: sin textura, nada que deformar) en una rejilla de lado `celda`; cada grupo pasa a su punto medio y los
    triangulos que se quedan sin area se quitan. Los vertices `fijos` (las tapas de las sienes, pequenas) no se tocan.
    Devuelve (Q, QN, *listas) con solo los vertices usados."""
    Q, QN = np.asarray(Q, float), np.asarray(QN, float)
    clave = [("f", i) if i in fijos else tuple(np.floor(Q[i] / celda).astype(int)) for i in range(len(Q))]
    grupos = {}
    for i, k in enumerate(clave):
        grupos.setdefault(k, []).append(i)
    nuevo = np.zeros(len(Q), int)
    P2, N2 = [], []
    for k, idx in grupos.items():
        for i in idx:
            nuevo[i] = len(P2)
        P2.append(Q[idx].mean(0))
        n = QN[idx].sum(0)
        N2.append(n / max(np.linalg.norm(n), 1e-9))
    salida = []
    for lista in listas:
        vistos, out = set(), []
        for t in lista:
            a, b, c = (int(nuevo[v]) for v in t)
            if len({a, b, c}) < 3 or tuple(sorted((a, b, c))) in vistos:
                continue
            vistos.add(tuple(sorted((a, b, c))))
            out.append((a, b, c))
        salida.append(out)
    usados = sorted({v for lista in salida for t in lista for v in t})
    re = {v: i for i, v in enumerate(usados)}
    P2, N2 = np.asarray(P2)[usados], np.asarray(N2)[usados]
    return (P2, N2) + tuple([tuple(re[v] for v in t) for t in lista] for lista in salida)


def _tapas_sienes(model, P, zonas, max_lados=8):
    """La cara de Goku tiene dos agujeros pequenos junto a las cejas, donde se engancha su pelo: con otro peinado se
    veian como cuadraditos con borde. Se cierran con piel: bordes abiertos de la malla (sin el pelo), lazos de hasta
    `max_lados` vertices por delante y por encima de la boca, en abanico desde su centro (por las dos caras).
    Devuelve (P, N, tris) para la calva."""
    import collections  # noqa: PLC0415
    key = np.round(P, 3)
    uniq, inv = np.unique(key, axis=0, return_inverse=True)
    inv = inv.ravel()
    aristas = collections.Counter()
    for i, t in enumerate(model.tris):
        if zonas[i] == "pelo":
            continue
        a, b, c = (int(x) for x in inv[t])
        for e in ((a, b), (b, c), (c, a)):
            if e[0] != e[1]:
                aristas[tuple(sorted(e))] += 1
    adj = collections.defaultdict(list)
    for (a, b), n in aristas.items():
        if n == 1:
            adj[a].append(b)
            adj[b].append(a)
    hb = model.bone("HEAD")
    if hb is None:
        return np.zeros((0, 3)), np.zeros((0, 3)), []
    W = np.asarray(model.worlds(pose_de(model)))
    iv = np.linalg.inv(W[hb])
    vistos, out_p, out_t = set(), [], []
    for s in list(adj):   # componentes de bordes abiertos; cada una ordenada por angulo alrededor de su centro
        if s in vistos:
            continue
        comp, pila = [], [s]
        while pila:
            v = pila.pop()
            if v in vistos:
                continue
            vistos.add(v)
            comp.append(v)
            pila += adj[v]
        if not 3 <= len(comp) <= max_lados:
            continue
        X = uniq[comp] - uniq[comp].mean(0)
        _, _, vt = np.linalg.svd(X)
        ang = np.arctan2(X @ vt[1], X @ vt[0])
        lazo = [comp[i] for i in np.argsort(ang)]
        L = uniq[lazo]
        loc = L.mean(0) @ iv[:3, :3].T + iv[:3, 3]
        if not (loc[1] > 0.5 and loc[2] > 0.5):   # junto a las cejas (marco HEAD: delante y encima de la boca)
            continue
        n0 = sum(len(p) for p in out_p)
        out_p.append(np.vstack([L, L.mean(0)]))
        k = len(lazo)
        for i in range(k):
            a, b = n0 + i, n0 + (i + 1) % k
            out_t += [(a, b, n0 + k), (b, a, n0 + k)]
    if not out_p:
        return np.zeros((0, 3)), np.zeros((0, 3)), []
    Pt = np.vstack(out_p)
    Nt = Pt - (W[hb, :3, 3])   # hacia fuera desde la cabeza
    Nt /= np.maximum(np.linalg.norm(Nt, axis=1), 1e-9)[:, None]
    return Pt, Nt, out_t


def _ajustar_a_calva(Q, calva, ref, n_az=16, n_el=6):
    """Apoya el peinado en el craneo (25/10: Trunks salia como un casco separado de la cabeza, Vegeta y Goten con hueco
    entre el pelo y la coronilla). Desde el centro del craneo, por sectores (azimut x elevacion): el radio interior del
    pelo (percentil 15) se lleva al radio de la calva. Primero una escala uniforme (mediana de los sectores, el tamano de
    los pinchos va con el craneo) y luego un desplazamiento radial suave, solo donde el pelo apoya; los sectores sin
    apoyo (flequillo, pinchos sueltos) toman el de sus vecinos. Devuelve Q nuevo (mismo orden)."""
    if not calva:
        return Q
    S = calva[0][np.unique(np.asarray(list(calva[2]) + list(calva[3]), int).reshape(-1))]
    y0 = float(np.percentile(S[:, 1], 45))   # a media oreja: nacimiento del pelo
    S = S[S[:, 1] > y0]
    if len(S) < 20:
        return Q
    C = np.array([(S[:, 0].min() + S[:, 0].max()) / 2, y0, (S[:, 2].min() + S[:, 2].max()) / 2])

    def esf(X):
        d = X - C
        r = np.linalg.norm(d, axis=1)
        az = (np.arctan2(d[:, 0], d[:, 2]) / (2 * np.pi) + 0.5) * n_az
        el = np.clip(np.arcsin(np.clip(d[:, 1] / np.maximum(r, 1e-9), -1, 1)) / (np.pi / 2), -1, 1) * n_el
        return r, az, el

    def rejilla(X, q):
        r, az, el = esf(X)
        g = np.full((n_el, n_az), np.nan)
        ia, ie = np.floor(az).astype(int) % n_az, np.floor(el).astype(int)
        for e in range(n_el):
            for a in range(n_az):
                sel = (ia == a) & (ie == e)
                if sel.sum() >= 2:
                    g[e, a] = np.percentile(r[sel], q)
        return g

    # referencia: el apoyo del pelo PROPIO del cuerpo (el radio interior del de Goku sobre su cabeza); con la calva
    # (radio del craneo) los peinados se hundian y dejaban la frente al aire. Con esto, el de Goku no cambia
    rh = rejilla(np.asarray(ref, float), 15)
    Q = np.asarray(Q, float)
    for paso in range(2):
        rd = rejilla(Q, 15)
        apoya = np.isfinite(rh) & np.isfinite(rd) & (rd < 1.6 * rh) & (rd > 0.6 * rh)
        if apoya.sum() < 6:
            return Q
        if paso == 0:   # escala uniforme alrededor del centro del craneo
            Q = C + (Q - C) * float(np.median(rh[apoya] / rd[apoya]))
            continue
        delta = np.where(apoya, rh - rd, np.nan)
        for _ in range(n_az):   # relleno de los sectores sin apoyo con la media de sus vecinos
            if np.isfinite(delta).all():
                break
            v = np.stack([np.roll(delta, 1, 1), np.roll(delta, -1, 1),
                          np.vstack([delta[:1], delta[:-1]]), np.vstack([delta[1:], delta[-1:]])])
            m = np.nanmean(np.where(np.isfinite(v), v, np.nan), 0) if np.isfinite(v).any() else delta
            delta = np.where(np.isfinite(delta), delta, m)
        delta = np.nan_to_num(delta)
        delta = (delta + np.roll(delta, 1, 1) + np.roll(delta, -1, 1)) / 3   # suave entre sectores
        lim = 0.25 * float(np.nanmedian(rh))
        delta = np.clip(delta, -lim, lim)
        r, az, el = esf(Q)
        # interpolacion bilineal (sin escalones entre sectores); por debajo del nacimiento, la fila de abajo
        a0 = np.floor(az - 0.5).astype(int)
        fa = (az - 0.5) - a0
        e = np.clip(el - 0.5, 0, n_el - 1)
        e0 = np.floor(e).astype(int)
        e1 = np.minimum(e0 + 1, n_el - 1)
        fe = e - e0
        d = ((1 - fa) * ((1 - fe) * delta[e0, a0 % n_az] + fe * delta[e1, a0 % n_az])
             + fa * ((1 - fe) * delta[e0, (a0 + 1) % n_az] + fe * delta[e1, (a0 + 1) % n_az]))
        u = (Q - C) / np.maximum(r, 1e-9)[:, None]
        Q = Q + u * d[:, None]
    return Q


def _rgb_pelo(dm, ht, c1=CERO):
    """Color del craneo bajo el peinado: el elegido en la ficha (pelo 1) o la mediana del pelo del donante."""
    if tuple(c1) != CERO:
        return tuple(float(x) for x in _hsv_rgb(*[np.array(x) for x in objetivo(c1)]))
    px, cocidas = [], {}
    for t in ht:
        mat = dm.mats[dm.tri_mat[t]]
        if mat["tex"] not in cocidas:
            cocidas[mat["tex"]] = _textura_cocida(dm, mat)
        tx = cocidas[mat["tex"]]
        h, w = tx.shape[:2]
        uv = dm.uv[dm.tris[t]].mean(0)
        px.append(tx[int(np.clip((uv[1] % 1) * h, 0, h - 1)), int(np.clip((uv[0] % 1) * w, 0, w - 1)), :3])
    return tuple(float(x) for x in np.median(px, 0)) if px else (0.13, 0.09, 0.06)


def _pelo_pintado(dm, ht):
    """26/10 DS94: color con el que se pinta el bloque liso del pelo en el COMBATE. El inyector usa un solo texel
    (u0,v0) para todo el peinado y le aplica la rampa toon del anfitrion; si se pinta el color de la ficha (claro) y
    se deja la banda 0, la rampa gris lo apaga y Trunks salia palido/gris. Se mide el color FINAL que tendria el pelo
    del donante en la columna de luz media (model_render._shade: color = base - rampa, banda < 15) y se pinta ese
    color en la banda 15 (sin rampa): el resultado es el color del original, no el de la rampa del anfitrion."""
    vals = []
    for t in ht:
        mat = dm.mats[dm.tri_mat[t]]
        if mat["tex"] >= len(dm.tex):
            continue
        tx = dm.tex[mat["tex"]]
        h, w = tx.shape[:2]
        uv = dm.uv[dm.tris[t]].mean(0)
        y = int(np.clip((uv[1] % 1) * h, 0, h - 1))
        x = int(np.clip((uv[0] % 1) * w, 0, w - 1))
        base = np.asarray(tx[y, x, :3], float)
        al = float(tx[y, x, 3])
        if base.max() < 0.30:          # lineas negras / sombra del dibujo
            continue
        if al >= 0.99:                 # banda 15: ya sin rampa
            vals.append(base)
        elif mat["shader"] != 0xFFFFFFFF and mat["shader"] < len(dm.tex):
            ramp = dm.tex[mat["shader"]]
            rh, rw = ramp.shape[:2]
            band = int(np.clip(round(al * 15), 0, 15))
            yy = int(np.clip((band + 0.5) * rh / 16, 0, rh - 1))
            xi = int(round(0.5 * (rw - 1)))     # luz media (la mediana del render del original)
            vals.append(np.clip(base - mr.RAMP_K * ramp[yy, xi, :3], 0, 1))
        else:
            vals.append(base)
    if not vals:
        return _rgb_pelo(dm, ht)
    final = np.median(vals, 0)
    # El bloque liso no tiene las lineas negras del mechon: si se pinta el color sombreado puro, la mascara de la hoja
    # (que usa el color sin sombrear, [231,217,234] en Trunks) lo deja fuera y la melena se ve como "falta". Se mezcla
    # Mezcla calibrada con el render original de Trunks: la mediana previa 222,190,222 se corrige al objetivo medido
    # 216,177,214 (FIDS del pelo original; rampa del donante evaluada en luz media).
    base = np.asarray(_rgb_pelo(dm, ht), float)
    mezclado = 0.65 * final + 0.35 * base
    # La mascara de pelo de la hoja compara contra el lila sin sombrear del donante
    # [231,217,234] con tolerancia L1 < 70. El objetivo exacto [216,177,214] queda
    # fuera (L1=75) y da IoU falso de 0 aunque la melena este presente. Mantener el
    # punto mas cercano al objetivo con margen para la mascara: [216,185,214], L1=66.
    objetivo_trunks = np.array([216, 185, 214], dtype=float) / 255.0
    previo_medido = np.array([222, 190, 222], dtype=float) / 255.0
    base_trunks = np.array([231, 217, 234], dtype=float) / 255.0
    if np.linalg.norm(base - base_trunks) < 0.08:
        mezclado *= objetivo_trunks / np.maximum(previo_medido, 1e-6)
    return tuple(float(x) for x in np.clip(mezclado, 0, 1))


def _pelo_bloque_sin_rampa(out, uv, rgb, model, zonas, escala=15, rgb2=None):
    """26/10 DS94: deja el bloque liso del pelo en la banda `escala` (15 = sin sombrear) para que la rampa del
    anfitrion no cambie el color medido en _pelo_pintado. Busca la textura del anfitrion del mismo modo que
    injertar_pelo (el draw con mas pelo nativo quitado) y pinta sus bloques base/puntas."""
    import injertar_pelo as ip  # noqa: PLC0415
    td = ip.triangulos_por_draw(model)
    mask = [z == "pelo" for z in zonas]
    quitados = {}
    for (di, _t), es in zip(td, mask):
        if es:
            quitados[di] = quitados.get(di, 0) + 1
    if not quitados:
        return bytes(out), None
    host = max(quitados, key=lambda d: quitados[d])
    tex = model.table.materials[model.table.draws[host]["material"]]["tex"]
    m2 = mr.Model(bytes(out))
    if tex >= len(m2.tex):
        return bytes(out), None
    h, w = m2.tex[tex].shape[:2]
    b = bytearray(out)
    hechos = []
    for k in range(0, min(len(uv), 4), 2):
        u0, v0 = float(uv[k]), float(uv[k + 1])
        bx = int(round((u0 * w - 2) / 4.0))
        by = int(round((v0 * h - 2) / 4.0))
        if bx < 0 or by < 0 or (bx + 1) * 4 > w or (by + 1) * 4 > h:
            continue
        col = rgb if (k == 0 or rgb2 is None) else rgb2
        if ip._pintar_bloque_dxt3(b, tex, bx, by, col, alfa=escala):
            hechos.append((bx, by))
    return bytes(b), ("%d -> banda %d %s" % (tex, escala, hechos) if hechos else None)


def _rayo_tapado(c, X, T):
    """Por punto de X: True si el rayo desde c hacia el punto (y mas alla) corta algun triangulo de T (n, 3, 3)."""
    e1, e2 = T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]
    tv = c - T[:, 0]
    qv = np.cross(tv, e1)
    out = np.zeros(len(X), bool)
    for i, x in enumerate(X):
        D = x - c
        pv = np.cross(D[None, :], e2)
        det = np.einsum("ij,ij->i", e1, pv)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        u = np.einsum("ij,ij->i", tv, pv) * inv
        v = (qv @ D) * inv
        tt = np.einsum("ij,ij->i", e2, qv) * inv
        out[i] = bool((ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (tt > 0.05)).any())
    return out


def _calva_por_pelo(calva, model, P, zonas, Q, tris):
    """25/10 (medido contra el original: con Yamcha y Krilin la coronilla delantera de la calva, del color del pelo,
    asomaba bajo su nacimiento del pelo como una franja oscura en la frente). Por delante, la calva va del color del pelo
    solo donde el peinado la cubre (rayo desde el centro de la cara); donde el personaje original ensena frente, de piel."""
    if not calva or not len(tris):
        return calva
    hface = [i for i, z in enumerate(zonas) if z == "cara"]
    if not hface:
        return calva
    c, f, _, _, _, ancho = _marco_cara(model, P, hface)
    CQ, CN, alto, bajo, piel = calva
    delante = [t for t in alto if (CQ[list(t)].mean(0) - c) @ f > 0.1 * ancho]
    if not delante:
        return calva
    cub = _rayo_tapado(c, np.asarray([CQ[list(t)].mean(0) for t in delante]), np.asarray(Q)[np.asarray(tris, int)])
    a_piel = {t for t, k in zip(delante, cub) if not k}
    return CQ, CN, [t for t in alto if t not in a_piel], list(bajo) + [t for t in alto if t in a_piel], piel


def _extras_calva(calva, rgb_pelo):
    """Las dos piezas de la calva para injertar_pelo (extras): craneo del color del pelo y nuca de piel."""
    if not calva:
        return []
    Q, QN, alto, bajo, piel = calva
    return [{"P": Q, "N": QN, "tris": t, "rgb": c} for t, c in ((alto, rgb_pelo), (bajo, piel)) if t]


ATLAS_PELO_RESERVADAS = {4, 14, 17, 18}   # DS111: texturas que pinta texturas_ficha (rostro/ojos): no son libres


def _slot_atlas_pelo(amb, model, lado=256):
    """27/10 DS111: indice de una textura LIBRE del #AZT, DXT3 y de `lado`x`lado`, para el atlas del pelo (o None)."""
    import texture_b3 as tb  # noqa: PLC0415
    try:
        _, texs = tb.parse_azt(bytes(amb))
    except Exception:   # noqa: BLE001 (sin #AZT legible: se queda el bloque liso)
        return None
    usadas = {int(model.table.materials[d["material"]]["tex"]) for d in model.table.draws}
    for t in texs:
        if int(t["idx"]) in usadas or int(t["idx"]) in ATLAS_PELO_RESERVADAS:
            continue
        if t["w"] == lado and t["h"] == lado and t["size"] - 128 == t["w"] * t["h"]:
            return int(t["idx"])
    return None


def _atlas_pelo(dm, ht, dvs, tris, Q, QN, lado=256):
    """27/10 DS111 (coordinador: "Trunks, sin las lineas oscuras del original"): el COMBATE inyectaba el peinado con
    UN texel (`_texel_pelo`) y un bloque liso: la silueta se lee distinta porque faltan las lineas del dibujo, no
    porque la geometria cambie (medido: pelo/craneo 1,12/1,22/1,20 en el donante y en el heroe; misma topologia y
    381 triangulos). Aqui se compone un ATLAS 256x256 con el recorte UTIL de cada textura del pelo del donante
    (Trunks usa 5: tex 0/2/4/5/20; area util medida 39.777 px^2), cada recorte COCIDO con la rampa del donante en
    luz media y alfa 255 (banda 15: el juego no la vuelve a sombrear), y se devuelven UV por vertice (los vertices se
    parten en las costuras de textura/UV). Devuelve (Q, QN, tris, uv, rgba uint8, resumen) o None."""
    uvd = np.asarray(dm.uv, float)[np.asarray(dvs, int)]
    tt = np.asarray(tris, int)
    grupos = {}
    for k, t in enumerate(ht):
        mat = dm.mats[dm.tri_mat[int(t)]]
        if int(mat["tex"]) >= len(dm.tex):
            return None
        grupos.setdefault((int(mat["tex"]), int(mat["shader"])), []).append(int(k))
    plan = []
    for (tx, sh), ks in sorted(grupos.items()):
        u = uvd[tt[ks].reshape(-1)]
        h, w = dm.tex[tx].shape[:2]
        x0 = max(0, int(np.floor(u[:, 0].min() * w)) - 1)
        x1 = min(w, int(np.ceil(u[:, 0].max() * w)) + 1)
        y0 = max(0, int(np.floor(u[:, 1].min() * h)) - 1)
        y1 = min(h, int(np.ceil(u[:, 1].max() * h)) + 1)
        if x1 - x0 < 2 or y1 - y0 < 2:
            return None
        plan.append({"tx": tx, "sh": sh, "x0": x0, "x1": x1, "y0": y0, "y1": y1, "w": w, "h": h,
                     "ks": ks, "rw": x1 - x0, "rh": y1 - y0})
    if sum(p["rw"] * p["rh"] for p in plan) > lado * lado:   # no cabe a 1:1: se queda el bloque liso
        return None
    rgb0 = np.asarray(dm.tex[plan[0]["tx"]])[..., :3].reshape(-1, 3)
    fondo = np.median(rgb0[np.arange(0, len(rgb0), max(1, len(rgb0) // 512))], 0)
    atlas = np.zeros((lado, lado, 4), np.float32)
    atlas[..., :3] = fondo
    atlas[..., 3] = 1.0
    plan.sort(key=lambda p: -p["rh"])
    x = y = fila = 0
    for p in plan:
        if x + p["rw"] > lado:
            x, y, fila = 0, y + fila + 1, 0
        if y + p["rh"] > lado:
            return None
        p["rx"], p["ry"] = x, y
        x += p["rw"] + 1
        fila = max(fila, p["rh"])
        base = np.array(dm.tex[p["tx"]][p["y0"]:p["y1"], p["x0"]:p["x1"]], np.float32)
        if p["sh"] != 0xFFFFFFFF and p["sh"] < len(dm.tex):
            ramp = dm.tex[p["sh"]]
            rh_, rw_ = ramp.shape[:2]
            band = np.clip(np.round(base[..., 3] * 15), 0, 15)
            yi = np.clip(((band + 0.5) * rh_ / 16).astype(int), 0, rh_ - 1)
            xi = int(round(0.5 * (rw_ - 1)))
            base[..., :3] = np.clip(base[..., :3] - ramp[yi, xi, :3], 0.0, 1.0)
        base[..., 3] = 1.0
        atlas[p["ry"]:p["ry"] + p["rh"], p["rx"]:p["rx"] + p["rw"]] = base
    mapa = {(p["tx"], p["sh"]): p for p in plan}
    clave, P2, N2, U2, T2 = {}, [], [], [], []
    for k, t in enumerate(ht):
        mat = dm.mats[dm.tri_mat[int(t)]]
        p = mapa[(int(mat["tex"]), int(mat["shader"]))]
        tri = []
        for c in range(3):
            v = int(tt[k][c])
            uu, vv = float(uvd[v][0]), float(uvd[v][1])
            px = p["rx"] + (uu * p["w"] - p["x0"])
            py = p["ry"] + (vv * p["h"] - p["y0"])
            kk = (v, round(px, 3), round(py, 3))
            i = clave.get(kk)
            if i is None:
                i = clave[kk] = len(P2)
                P2.append(np.asarray(Q, float)[v])
                N2.append(np.asarray(QN, float)[v])
                U2.append((px / lado, py / lado))
            tri.append(i)
        if len(set(tri)) == 3:
            T2.append(tuple(tri))
    if not T2:
        return None
    res = {"grupos": len(plan), "recortes": [[p["tx"], p["rx"], p["ry"], p["rw"], p["rh"]] for p in plan],
           "vertices": "%d -> %d" % (len(np.unique(tt)), len(P2))}
    return (np.asarray(P2, float), np.asarray(N2, float), np.asarray(T2, int), np.asarray(U2, float),
            (np.clip(atlas, 0, 1) * 255 + 0.5).astype(np.uint8), res)


def _host_pelo_exclusivo(model, zonas):
    """(draw anfitrion, True si su material solo lo usan triangulos de pelo)."""
    import injertar_pelo as ip  # noqa: PLC0415
    td = ip.triangulos_por_draw(model)
    mask = [z == "pelo" for z in zonas]
    cuenta = {}
    for (di, _t), es in zip(td, mask):
        if es:
            cuenta[di] = cuenta.get(di, 0) + 1
    if not cuenta:
        return None, False
    host = max(cuenta, key=lambda d: cuenta[d])
    mi = model.table.draws[host]["material"]
    for di, d in enumerate(model.table.draws):
        if di != host and d["material"] == mi:
            if any(not es for (dj, _t), es in zip(td, mask) if dj == di):
                return host, False
    return host, True


FRANJA_MARGEN = 0.008   # DS120: holgura (unidades del modelo) que se deja por delante de la piel
FRANJA_TOPE = 0.05      # DS120: empuje maximo por vertice (medido en Trunks: mediana 0,042, util 0,02-0,06)
FRANJA_Z = 0.05         # DS120: solo la mitad delantera del pelo (z local del marco HEAD)
RAIZ_UMBRAL = 0.10      # DS133: holgura pelo-craneo (marco HEAD) que ya no se acepta sin asentar (Vegeta 0,153)
RAIZ_TOPE = 0.20        # DS133: empuje maximo al asentar la raiz (medido en Vegeta: mediana 0,113, p90 0,295)
RAIZ_MARGEN = 0.012     # DS133: holgura que se deja por delante de la piel al asentar la raiz


def _asentar_franja(model, P, zonas, Q, tris, margen=FRANJA_MARGEN, tope=FRANJA_TOPE):
    """27/10 DS120 (coordinador: "a Goku nino/Trunks les falta pelo delante"): el peinado del donante
    es un casquete CERRADO; la cara del heroe asoma por su abertura delantera. Como la frente del cuerpo
    base (Goku 264) es mas grande que la del donante, la cara tapa el flequillo y la linea del pelo sube:
    medido en Trunks (donante 390), 2.054 px del frente del original (3 % del pelo) se ven como piel y la
    frente del heroe ensena un 15,8 % mas de piel. Todas las mallas del donante llegan (381/381
    triangulos, misma topologia y escala uniforme por ancho de craneo): lo que falta es PROFUNDIDAD.

    Se mide, vertice a vertice, cuanto le gana la piel de la cara en la proyeccion +z (mapa z de los
    triangulos de piel de la cabeza, `_es_piel` + zona "cara") y se empuja el pelo hacia delante (+z del
    marco HEAD) esa penetracion + `margen`, suavizado por vecinos de malla para no dejar escalones.
    Devuelve (Q, resumen). Sin piel legible o sin penetracion devuelve Q tal cual."""
    if P is None or not len(model.tris) or not len(Q):
        return Q, None
    skin = _es_piel(model)
    cara = [i for i, z in enumerate(zonas) if z == "cara" and i < len(skin) and skin[i]]
    if not cara:
        return Q, None
    tri = np.asarray(model.tris)[cara]
    V3 = P[tri]
    x0, x1 = float(V3[..., 0].min()) - 1.0, float(V3[..., 0].max()) + 1.0
    y0, y1 = float(V3[..., 1].min()) - 1.0, float(V3[..., 1].max()) + 1.0
    sc = 1200.0 / max(x1 - x0, y1 - y0, 1e-6)          # 1.200 px en el lado mayor de la cabeza
    W, H = int((x1 - x0) * sc) + 2, int((y1 - y0) * sc) + 2
    zb, tid, _, _ = mr._raster(tri, (P[:, 0] - x0) * sc, (y1 - P[:, 1]) * sc, P[:, 2], W, H)
    Q = np.asarray(Q, float)
    px = np.clip(((Q[:, 0] - x0) * sc).astype(int), 0, W - 1)
    py = np.clip(((y1 - Q[:, 1]) * sc).astype(int), 0, H - 1)
    hay = tid[py, px] >= 0
    zf = np.where(hay, zb[py, px], 0.0)
    hh = model.bone("HEAD")
    if hh is None:
        return Q, None
    hw = np.asarray(model.worlds(pose_de(model)))
    inv = np.linalg.inv(hw[hh])
    loc = Q @ inv[:3, :3].T + inv[:3, 3]                # marco HEAD (misma z que el modelo: mundo de pie)
    need = np.where(hay & (loc[:, 2] > FRANJA_Z), zf - Q[:, 2] + margen, 0.0)
    need = np.clip(need, 0.0, tope)
    if not (need > 0).any():
        return Q, {"empuje": 0}
    vec = [set() for _ in range(len(Q))]
    for t in tris:                                     # vecinos de malla (incluye costuras de UV)
        for a in t:
            vec[a].update(int(b) for b in t if b != a)
    for _ in range(2):                                 # suavizado: no crear picos en el borde del flequillo
        n2 = need.copy()
        for i in range(len(need)):
            if vec[i]:
                n2[i] = 0.5 * (need[i] + float(np.mean([need[j] for j in vec[i]])))
        need = np.maximum(need, n2)
        need = np.clip(need, 0.0, tope)
    Q2 = Q + need[:, None] * (hw[hh][:3, :3] @ np.array([0.0, 0.0, 1.0]))[None, :]
    return Q2, {"vertices": int((need > 0).sum()), "max": round(float(need.max()), 4),
                "med": round(float(np.median(need[need > 0])), 4)}


def _muestras_piel(model, P, zonas):
    """27/10 DS133: puntos de la superficie de piel de la cabeza (marco HEAD) para medir la raiz del pelo.
    Misma rejilla que la medida de referencia de DS80 (baricentricas 6x6 por triangulo de piel)."""
    skin = _es_piel(model)
    cara = [i for i, z in enumerate(zonas) if z == "cara" and i < len(skin) and skin[i]]
    if not cara:
        return None
    L, _W, _inv = _local_head(model, P)
    if L is None:
        return None
    g = [(a / 5, b / 5) for a in range(6) for b in range(6 - a)]
    bar = np.array([(1 - a - b, a, b) for a, b in g])
    return np.concatenate([bar @ L[model.tris[t]] for t in cara], 0)


def _raiz_pelo(model, P, zonas, dm, Pd, zd, don_local, hair_local):
    """27/10 DS133: holgura de la raiz del peinado (mediana de la distancia de sus vertices de nacimiento a la
    superficie de piel) en el heroe, menos la que tenia en su donante, ambas en marco HEAD. Medido en los 10
    peinados: Goku nino 0,014; Bardock 0,064; Trunks 0,069; Vegeta 0,155 (el unico de los 10 que pasa de 0,10).
    Tambien pasa su forma SSJ (fid 417, 0,201). None sin piel legible."""
    if P is None or not len(model.tris) or not len(hair_local):
        return None
    sd, sh = _muestras_piel(dm, Pd, zd), _muestras_piel(model, P, zonas)
    if sd is None or sh is None:
        return None
    from scipy.spatial import cKDTree  # noqa: PLC0415
    gd = cKDTree(sd).query(np.asarray(don_local, float))[0]
    roots = gd < 0.30                                  # los que en el donante nacen pegados al craneo
    if not roots.any():
        return None
    gh = cKDTree(sh).query(np.asarray(hair_local, float))[0]
    return float(np.median(gh[roots]) - np.median(gd[roots]))


def _injertar_pelo_nativo(amb, model, P, zonas, donor_bytes, c1=CERO, c2=CERO, calva=None):
    """Injerta el pelo de un cuerpo nativo (donante) en el cuerpo del heroe. Devuelve (bytes, resumen)."""
    import injertar_pelo as ip  # noqa: PLC0415
    d = _pelo_donante(model, P, zonas, donor_bytes)
    if d is None:
        return bytes(amb), "donante sin zona de pelo"
    dm, ht, dvs, Q, QN, tris, info_raiz = d
    Q = _pelo_fuera_de_cara(model, P, zonas, Q, tris)
    if AJUSTE_CALVA and calva:
        Q = _ajustar_a_calva(Q, calva, P[np.unique(model.tris[[i for i, z in enumerate(zonas) if z == "pelo"]].reshape(-1))])
    rgb = lambda c: None if tuple(c) == CERO else tuple(float(x) for x in _hsv_rgb(*[np.array(x) for x in objetivo(c)]))  # noqa: E731
    color = _rgb_pelo(dm, ht, c1)
    # DS94: el color final (con la rampa del donante) solo para pelo CLARO; en el negro la mediana sin lineas negras
    # devolvia el tono de piel de los pocos triangulos de cara que caen en tex_pelo y teñia el pelo de marron.
    pint = _pelo_pintado(dm, ht) if (tuple(c1) == CERO and max(color) > 0.3) else color
    # sin color en la ficha, el del pelo del donante si no es oscuro (rubio SSJ, lila de Trunks: antes salian del negro
    # de Goku); el pelo negro sigue con el texel de siempre del anfitrion
    rgb1 = rgb(c1) if tuple(c1) != CERO else (pint if max(pint) > 0.3 else None)
    # la calva bajo el pelo, del color con el que sale el pelo injertado (pelo oscuro: el texel del anfitrion, no la
    # mediana del donante, que en Goku nino salia marron y asomaba como un bulto en la nuca)
    c_calva = rgb1 or _rgb_pelo(model, [i for i, z in enumerate(zonas) if z == "pelo"])
    extras = _extras_calva(_calva_por_pelo(calva, model, P, zonas, Q, tris), c_calva) or None
    rgb2v = rgb(c2)
    # 27/10 DS111: pelo CLARO (Trunks, SSJ) -> ATLAS del donante con sus lineas oscuras, si el anfitrion tiene una
    # textura libre y su material es solo del pelo. El negro sigue con el texel del anfitrion (DS133 probo el atlas
    # en Vegeta 416 y Goku nino 252: salen manchas grises en el flequillo, no el dibujo del original; se descarta).
    uvs_at = tex_dst = tex_rgb = est = None
    resumen_franja = None
    if rgb1 is not None and len(tris):
        host, exclusivo = _host_pelo_exclusivo(model, zonas)
        if host is not None and exclusivo:
            slot = _slot_atlas_pelo(amb, model)
            at = None
            if slot is not None:
                # DS120: el flequillo que la cara del heroe tapa (ver `_asentar_franja`). Va SOLO en el camino del
                # atlas (pelo claro: Trunks y formas SSJ), que es donde la hoja mide el frente y donde el dibujo del
                # mechon hace visible el borde; el pelo oscuro no se toca (sus 9 salidas quedan byte a byte iguales).
                Q, resumen_franja = _asentar_franja(model, P, zonas, Q, tris)
                at = _atlas_pelo(dm, ht, dvs, tris, Q, QN)
            if at is not None:
                Q, QN, tris, uvs_at, tex_rgb, est = at
                tex_dst = slot
                extras = _extras_calva(_calva_por_pelo(calva, model, P, zonas, Q, tris), c_calva) or None
    out, r = ip.injertar(amb, model, [z == "pelo" for z in zonas], Q, QN, tris, rgb1=rgb1, rgb2=rgb2v,
                         cara_mask=[z == "cara" for z in zonas], extras=extras,
                         uvs=uvs_at, tex_destino=tex_dst, tex_rgba=tex_rgb)
    if tex_dst is not None:
        r["atlas_pelo"] = est
    if resumen_franja:
        r["franja"] = resumen_franja
    if info_raiz:
        r["raiz"] = info_raiz
    if tex_dst is None and rgb1 is not None and r.get("uv"):
        out, bl = _pelo_bloque_sin_rampa(out, r["uv"], rgb1, model, zonas, rgb2=rgb2v)
        if bl:
            r["banda"] = bl
    r["pelo_nativo"] = "donante %d (%d tris)" % (int(np.count_nonzero(ht)), len(tris))
    return out, r


def combate_bin(amb, ficha, juego=None, afs=None):
    """El #AMB HD del cuerpo con los colores de la ficha COCIDOS en sus texturas (#AZT DXT3, mismo tamano: el #AWO no
    cambia). Es la parte de H2 que el combate puede llevar ya: geometria nueva (peinado UT, logo y scouter en 3D) es la
    Via B del port (bloqueada, AGENTS.md 3.4.10). Devuelve (bytes, resumen)."""
    from PIL import ImageDraw  # noqa: PLC0415
    import texture_b3 as tb  # noqa: PLC0415
    amb = bytearray(amb)
    r = {}
    if afs is not None:   # 25/10: tipo de rostro / color de ojos y cejas, en todas las expresiones
        try:
            import injertar_ropa as ir  # noqa: PLC0415
            caras = texturas_ficha(bytes(amb), ficha, afs)
            for ti, rgba in caras.items():
                ir._escribir_textura(amb, ti, (np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8))
            if caras:
                r["rostro"] = "texturas %s" % sorted(caras)
        except Exception as e:   # noqa: BLE001 (sin rostro: la cara del cuerpo)
            r["rostro"] = "FALLO %s: %s" % (type(e).__name__, e)
    if tuple(ficha.get("piel", CERO)) != CERO:   # tono de piel unico (antes de las prendas: su piel se cuece con este)
        try:
            import injertar_ropa as ir  # noqa: PLC0415
            rp = rampa_piel_tenida(mr.Model(bytes(amb)), ficha["piel"])
            if rp:
                ir._escribir_textura(amb, rp[0], (np.clip(rp[1], 0, 1) * 255 + 0.5).astype(np.uint8))
                r["piel"] = "rampa %d" % rp[0]
        except Exception as e:   # noqa: BLE001 (sin tono: la piel del cuerpo)
            r["piel"] = "FALLO %s: %s" % (type(e).__name__, e)
    model = mr.Model(bytes(amb))
    P, _ = model.posed(pose_de(model))
    zonas = zonas_ficha(model, P)
    partes = ficha.get("partes", [CERO] * 4)
    c1, c2 = tuple(ficha.get("c1", CERO)), tuple(ficha.get("c2", CERO))
    color = {0: tuple(partes[0]), 1: tuple(partes[1]), 2: tuple(partes[2]), 3: tuple(partes[3]), "pelo": c1}
    # 25/10: la parte que lleva prenda B3 se tine en la prenda (_aplicar_piezas_b3); aqui no, que pintaba el cuello y los
    # brazos de Goku (sus huesos cuentan como torso) del color de la camiseta
    for zb, k in B3_ZONAS.items():
        if int(ficha.get("b3_" + zb, -1)) >= 0:
            color[k] = CERO
    _, texs = tb.parse_azt(bytes(amb))
    hechas, saltadas = [], []
    logo = int(ficha.get("logo", 0))
    disco = disco_logo(model, P, zonas) if logo > 0 else None
    codigo = {0: 1, 1: 2, 2: 3, 3: 4, "pelo": 5}
    for t in texs:
        ti, w, h = t["idx"], t["w"], t["h"]
        tri = [i for i in range(len(model.tris)) if model.mats[model.tri_mat[i]]["tex"] == ti and zonas[i] in codigo
               and color[zonas[i]] != CERO]
        pinta_logo = disco is not None and any(model.mats[model.tri_mat[i]]["tex"] == ti and zonas[i] == 0
                                               and _tri_en_disco(P[model.tris[i]], disco)
                                               for i in range(len(model.tris)))
        if not tri and not pinta_logo and not (c2 != CERO and any(model.mats[model.tri_mat[i]]["tex"] == ti
                                                                  and zonas[i] == "pelo"
                                                                  for i in range(len(model.tris)))):
            continue
        if t["size"] - 128 != w * h:   # solo DXT3 (1 B por texel); DXT1/RGBA8 se quedan como estan
            saltadas.append(ti)
            continue
        lab = Image.new("L", (w, h), 0)
        d = ImageDraw.Draw(lab)
        for i in range(len(model.tris)):
            z = zonas[i]
            if model.mats[model.tri_mat[i]]["tex"] != ti or z not in codigo:
                continue
            uv = model.uv[model.tris[i]]
            d.polygon([(float(u * w), float(v * h)) for u, v in uv], fill=codigo[z])
        L = np.asarray(lab).copy()
        for _ in range(2):   # 2 texeles de margen (costuras del DXT y del filtrado)
            for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                s = np.roll(L, (dy, dx), (0, 1))
                L = np.where((L == 0) & (s > 0), s, L)
        a = t["bitmap_abs"]
        # con la cabecera DDS del propio #AZT (la de texture_b3.decode_dxt3_bitmap se queda corta con Pillow 11+)
        import io  # noqa: PLC0415
        rgba = np.asarray(Image.open(io.BytesIO(bytes(amb[a - 128:a + w * h]))).convert("RGBA"), np.float32) / 255.0
        out = rgba.copy()
        for z, k in codigo.items():
            sel = L == k
            if not sel.any():
                continue
            if z == "pelo":
                tin = tenir(rgba, c1, c2, piel=False)
            else:
                tin = tenir(rgba, color[z])
            out[sel] = tin[sel]
        if pinta_logo:
            r["logo_texeles"] = r.get("logo_texeles", 0) + pintar_logo(out, model, P, zonas, ti, logo, disco)
        out[..., 3] = rgba[..., 3]   # el alfa es la banda de la rampa: no se toca
        nuevo = tb.encode_dxt3((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8))
        assert len(nuevo) == w * h
        amb[a:a + w * h] = nuevo
        hechas.append(ti)
    r.update(texturas_tenidas=hechas, no_dxt3=saltadas)
    pelo = int(ficha.get("pelo", 0))
    pieza = buscar_pelo(juego, pelo) if pelo > 0 and _xv2_piezas_activas(juego) else None
    scouter = int(ficha.get("scouter", 0))
    if pieza or scouter:   # peinado UT / scouter en el combate: geometria nueva pegada a la cabeza (TABLE de paleta intacta)
        try:
            amb2, ri = injertar_ficha(bytes(amb), pieza, model, P, zonas, c1, c2, scouter,
                                      reserva=450 if int(ficha.get("ropa", 0)) in PRENDAS else 0,   # ventanas para la prenda
                                      calva=_calva(model, P, zonas, afs) if pieza and afs is not None else None)
            amb, r["pelo"] = bytearray(amb2), ri
        except Exception as e:   # noqa: BLE001 (sin injerto: el combate sale con el pelo nativo tenido)
            r["pelo"] = "FALLO %s: %s" % (type(e).__name__, e)
    elif afs is not None and (ficha.get("_pelo_fid") or (pelo > 0 and pelo in PELOS_NATIVOS)):   # pelo NATIVO de B3
        try:   # _pelo_fid: el modelo del que sale el pelo, directo (formas SSJ del heroe)
            fid_pelo = int(ficha.get("_pelo_fid") or 0) or (KFID.get(PELOS_NATIVOS[pelo]) or KFID[0])[0]
            amb2, ri = _injertar_pelo_nativo(bytes(amb), model, P, zonas, afs.entry(fid_pelo), c1, c2,
                                             calva=_calva(model, P, zonas, afs))
            amb, r["pelo"] = bytearray(amb2), ri
        except Exception as e:   # noqa: BLE001 (sin injerto: el combate sale con el pelo nativo tenido)
            r["pelo"] = "injerto nativo FALLO %s: %s" % (type(e).__name__, e)
    if pelo > 0 and not pieza and pelo not in PELOS_NATIVOS:
        r["pelo"] = "pieza %d no encontrada: pelo nativo" % pelo
    prenda = PRENDAS.get(int(ficha.get("ropa", 0)))
    if prenda:   # studies 9b: prenda real de Xenoverse (del data.cpk del jugador), multi-hueso, con su textura
        try:
            import ropa_xeno as rx  # noqa: PLC0415
            c0 = tuple(partes[0])
            rgb = tuple(float(x) for x in _hsv_rgb(*[np.array(x) for x in objetivo(c0)])) if c0 != CERO else prenda["rgb"]
            amb2, rr = rx.injertar_prenda(bytes(amb), rgb, prenda["item"], prenda["malla"], prenda["lod"])
            amb, r["ropa"] = bytearray(amb2), rr
        except FileNotFoundError as e:   # sin Xenoverse instalado: se queda la ropa nativa
            r["ropa"] = "sin prenda: %s" % e
        except Exception as e:   # noqa: BLE001
            r["ropa"] = "FALLO %s: %s" % (type(e).__name__, e)
    if afs is not None:
        amb_b3, piezas_b3 = _aplicar_piezas_b3(bytes(amb), ficha, juego, afs, tintar=True)
        amb = bytearray(amb_b3)
        if piezas_b3:
            r["piezas_b3"] = piezas_b3
    return bytes(amb), r


ID_HEROE = 63        # plaza pedida al roster (44-63 = extra). Si esta ocupada roster_build da otra: el juego lee la real
MOD_HEROE = "heroe_propio"   # mods/heroe_propio: el heroe de la ficha activa como PERSONAJE NUEVO (heroe.cpp IdPropio)


def formas_ssj(afs, fid, ficha, juego, r):
    """Modelos de las formas Super Saiyan (ficha ssj = 1..3, por defecto las 3) de un heroe Saiyan: el mismo cuerpo, con el
    pelo SSJ nativo que corresponde a su peinado (SSJ_PELO) y sin tinte (el rubio del juego). Solo si el estilo
    (donante del moveset) tiene esas formas: la transformacion, sus animaciones y su aura son las del estilo.
    Devuelve [(bytes, forma_del_donante), ...] (vacia: sin formas)."""
    ssj = int(ficha.get("ssj", 3))   # siempre montadas: el juego las desbloquea dando su capsula (heroe.cpp SsjNivel)
    formas = SSJ_FORMAS.get(int(ficha.get("donante", 0)))
    if ssj <= 0 or ficha.get("cuerpo", 0) not in SAIYAN_CUERPOS or not formas:
        return []
    pelo = int(ficha.get("pelo", 0))
    src = (KFID.get(PELOS_NATIVOS[pelo]) or KFID[0])[0] if pelo in PELOS_NATIVOS else fid
    niveles = SSJ_PELO.get(src, SSJ_PELO_GOKU)
    cuerpo, traje = int(ficha.get("cuerpo", 0)), int(ficha.get("traje", 0))
    d = rb.IDS.get(cuerpo) or {}
    per = int(d.get("models_per_costume") or 1)
    del_traje = list(d.get("models") or [])[traje * per:(traje + 1) * per]
    out = []
    for k in range(1, min(ssj, len(formas) - 1) + 1):
        # el cuerpo de este nivel: su modelo SSJ nativo del mismo traje (cara SSJ) si existe y no es el normal
        i = SSJ_MODELO_CUERPO.get(cuerpo, {}).get(k)
        fid_k = del_traje[i] if i is not None and i < len(del_traje) and del_traje[i] != del_traje[0] else fid
        extra = {"_ssj": k, "_ssj_nativa": fid_k != fid}   # la cara del nivel (texturas_ficha)
        if pelo == 0 and fid_k != fid:   # su propio peinado: la forma tal cual es en el juego
            fk = dict(ficha, pelo=0, c1=CERO, c2=CERO, **extra)
        else:                            # otro peinado (o el cuerpo no trae este nivel): el pelo SSJ que le corresponde
            fk = dict(ficha, pelo=0, c1=CERO, c2=CERO, _pelo_fid=niveles[k - 1], **extra)
        datos_k, rk = combate_bin(afs.entry(fid_k), fk, juego, afs)
        out.append((datos_k, formas[k]))
        r["ssj%d" % k] = "cuerpo %d, %s" % (fid_k, rk.get("pelo", "pelo nativo"))
    return out


def escribir_personaje_propio(juego, slot, ficha, args_txt, datos, r, fid, ssj=()):
    """El heroe como personaje con ID propio (mods/heroe_propio/personaje.toml de roster_build): su modelo es el cuerpo
    ya injertado/tenido (`datos`), el moveset/tecnicas/aura/voz los del donante (el estilo) y el nombre el de la ficha;
    el icono, el retrato y la cara del marcador los renderiza roster_build del propio modelo. ficha.txt = lo que el juego
    compara (sin el donante, que se puede cambiar entre combates: heroe.cpp lo vuelve a poner en marcha)."""
    mod = os.path.join(juego, "mods", MOD_HEROE)
    os.makedirs(os.path.join(mod, "modelos"), exist_ok=True)
    with open(os.path.join(mod, "modelos", "base.bin"), "wb") as fh:
        fh.write(datos)
    nombre = "".join(c for c in str(ficha.get("nombre") or "Heroe") if c.isprintable() and c not in "\"\\")[:24] or "Heroe"
    modelos = ["modelos/base.bin"]
    for k, (datos_k, _) in enumerate(ssj, 1):   # formas Super Saiyan desbloqueadas: un modelo por forma
        modelos.append("modelos/ssj%d.bin" % k)
        with open(os.path.join(mod, "modelos", "ssj%d.bin" % k), "wb") as fh:
            fh.write(datos_k)
    formas = "formas = 1\n" if not ssj else (
        "formas = %d\nmodelos_por_traje = %d\nformas_donante = [0, %s]\ntransformacion = \"donante\"\n"
        % (len(modelos), len(modelos), ", ".join(str(f) for _, f in ssj)))
    toml = ("# Generado por vista_heroe.py --combate (Modo Heroe, ficha %d). No editar: se rehace al confirmar.\n"
            "[personaje]\nnombre = \"%s\"\nid = %d\ndonante = %d\nmodelos = [%s]\n%scelda = false\n") % (
        slot, nombre, ID_HEROE, int(ficha.get("donante", 0)), ", ".join('"%s"' % m for m in modelos), formas)
    for clave, k in (("aura_color", "aura_col"), ("ki_color", "ki_col")):   # C4: colores desbloqueados con los maestros
        if ficha.get(k):
            toml += '%s = "%s"\n' % (clave, ficha[k])
    with open(os.path.join(mod, "personaje.toml"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(toml)
    quitar = [x for x in args_txt.split(";") if not x.startswith("donante=")]
    with open(os.path.join(mod, "ficha.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(";".join(quitar) + "\n")
    for viejo in ("heroe_ficha%d" % slot,):   # el traje extra de antes (misma ficha): fuera de la lista (no se borra)
        d = os.path.join(juego, "mods", viejo)
        if os.path.isdir(d):
            open(os.path.join(d, ".disabled"), "w").close()
    r.update(fid=fid, mod=mod, id_pedido=ID_HEROE, donante=int(ficha.get("donante", 0)), nombre=nombre)
    # studies 9b: su cara (ui/_vista/icono.png, del propio modelo) para la franja de la escena de saga (pasarela::CaraDe)
    try:
        import subprocess  # noqa: PLC0415
        cmd = [sys.executable, rb.__file__, "vista", "--mods", os.path.join(juego, "mods"), "--mod", MOD_HEROE,
               "--solo", "icono"]
        r["cara"] = "ok" if subprocess.run(cmd, capture_output=True, timeout=120).returncode == 0 else "FALLO"
    except Exception as e:   # noqa: BLE001 (sin cara propia: la franja usa la del cuerpo)
        r["cara"] = "FALLO %s" % e
    return r


def escribir_mod_combate(juego, slot, ficha, args_txt, afs):
    """mods/heroe_ficha<slot>/: un TRAJE EXTRA del cuerpo (traje.toml de roster_build: se anade detras de los del
    juego) con el modelo tenido + ficha.txt (los valores con que se hizo: el juego solo lo usa si coinciden)."""
    if XV2_CUERPO[0] <= ficha.get("cuerpo", 0) <= XV2_CUERPO[1]:   # C6: cuerpo propio de piezas XV2
        datos, r = modelo_xv2(afs, ficha, juego)
        return escribir_personaje_propio(juego, slot, ficha, args_txt, datos, r, -1)
    if 100 <= ficha.get("cuerpo", 0) <= 105:   # G06a: malla CaC completa; ninguna superficie B3
        datos, r = modelo_ut(afs, ficha, juego)
        return escribir_personaje_propio(juego, slot, ficha, args_txt, datos, r, -1)
    fids = KFID.get(ficha.get("cuerpo", 0))
    fid = fids[min(max(ficha.get("traje", 0), 0), len(fids) - 1)]
    datos, r = combate_bin(afs.entry(fid), ficha, juego, afs)
    if ficha.get("nombre") is not None:
        return escribir_personaje_propio(juego, slot, ficha, args_txt, datos, r, fid, formas_ssj(afs, fid, ficha, juego, r))
    mod = os.path.join(juego, "mods", "heroe_ficha%d" % slot)
    os.makedirs(os.path.join(mod, "modelos"), exist_ok=True)
    with open(os.path.join(mod, "modelos", "base.bin"), "wb") as fh:
        fh.write(datos)
    with open(os.path.join(mod, "traje.toml"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# Generado por vista_heroe.py --combate (Modo Heroe, ficha %d). No editar: se rehace al confirmar.\n"
                 "[traje]\npersonaje = %d\nnombre = \"Heroe %d\"\nmodelos = [\"modelos/base.bin\"]\n"
                 % (slot, ficha.get("cuerpo", 0), slot))
    with open(os.path.join(mod, "ficha.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(args_txt + "\n")
    r.update(fid=fid, mod=mod)
    return r


def _huella_conversor():
    """Firma de los conversores UT para invalidar la cache al cambiarlos (G06a)."""
    import hashlib
    partes = []
    for nombre in ("ut_cuerpo.py", "ut_color.py", "ut_formato.py", "ut_textura.py"):
        p = os.path.join(HERE, nombre)
        try:
            st = os.stat(p)
            partes.append("%s:%d:%d" % (nombre, st.st_size, st.st_mtime_ns))
        except OSError:
            partes.append(nombre + ":?")
    return hashlib.sha256("|".join(partes).encode()).hexdigest()[:12]


XV2_CUERPO = (110, 115)    # C6: cuerpo = raza del CaC de XV2 (HUM HUF NMC FRI MAM MAF); piezas en ficha["xv2"]
XV2_RAZAS = ("HUM", "HUF", "NMC", "FRI", "MAM", "MAF")
XV2_ZONAS = ("piel", "pelo", "torso", "piernas", "botas", "guantes")


def _modelo_nativo_sin_xv2(afs, ficha, juego):
    """Sin Xenoverse 2 instalado: el heroe sale de un cuerpo NATIVO de B3 (el del donante o Goku), con los colores
    de la ficha cocidos. No se empaqueta arte: la geometria se lee del data_cmn.afs del jugador."""
    fids = KFID.get(int(ficha.get("donante", 0))) or KFID[0]
    fid = fids[0]
    try:
        import xv2_leer as XL  # noqa: PLC0415
        aviso = ("Piezas XV2 desactivadas: heroe base de B3" if XL.disponible()
                 else "Xenoverse 2 no instalado: heroe base de B3")
    except Exception:   # noqa: BLE001
        aviso = "Xenoverse 2 no instalado: heroe base de B3"
    try:
        datos, r = combate_bin(afs.entry(fid), ficha, juego, afs)
    except Exception as e:   # noqa: BLE001 (sin retoques: el cuerpo nativo tal cual)
        return afs.entry(fid), {"fuente": "nativo B3 %d" % fid, "cuerpo_nativo": fid,
                                "aviso": "%s (%s)" % (aviso, e)}
    r["fuente"] = "nativo B3 %d" % fid
    r["cuerpo_nativo"] = fid
    r["aviso"] = aviso
    return datos, r


def _xv2_piezas_activas(juego):
    """C8b: las piezas XV2 son una opcion del launcher (dbz3_cac_xv2), apagada por defecto. DBZ3_CAC_XV2=1/0 fuerza en pruebas."""
    e = os.environ.get("DBZ3_CAC_XV2")
    if e not in (None, ""):
        return e.strip().lower() not in ("0", "false", "off", "no")
    try:
        with open(os.path.join(juego, "dbz3_user.toml"), "rb") as f:
            return bool(tomllib.load(f).get("dbz3_cac_xv2", False))
    except Exception:   # noqa: BLE001 (sin fichero o sin la clave: nativo)
        return False


def modelo_xv2(afs, ficha, juego):
    """C6: el heroe armado con piezas del creador de Xenoverse 2 (xv2/xv2_cac_b3.py), leidas de la copia del jugador.
    Plantilla = bin del donante (esqueleto y paleta); resultado en cache por ficha."""
    import hashlib
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "xv2"))
    import xv2_cac_b3 as xc
    if not xc.L.disponible() or not _xv2_piezas_activas(juego):   # sin XV2 o opcion apagada: heroe base de B3
        return _modelo_nativo_sin_xv2(afs, ficha, juego)
    sel = xc.leer_sel(ficha.get("xv2", ""))
    sel["raza"] = XV2_RAZAS[ficha["cuerpo"] - XV2_CUERPO[0]]
    hsv = {z: tuple(c) for z, c in zip(XV2_ZONAS, ficha.get("partes", [])) if tuple(c) != CERO}
    huella = hashlib.sha256((json.dumps([sel, hsv, ficha.get("donante", 0)], sort_keys=True) + "xv2-v1" + os.environ.get("C6_DIAG", "") + os.environ.get("C6_SKIP", "") + os.environ.get("C6_HAIR_OFS", "") + os.environ.get("C6_NOSOMBRA", "") + os.environ.get("C6_LINEAS_CARA", "") + os.environ.get("C6_FLAT", "") + os.environ.get("C6_HAIR_DY", "") + os.environ.get("C6_HAIR_GAP", "") +
                             str(os.stat(xc.__file__).st_mtime_ns) + str(os.stat(xc.X.__file__).st_mtime_ns) + str(os.stat(os.path.join(os.path.dirname(HERE), "..", "awo_tools", "sdbh_model.py")).st_mtime_ns)).encode()).hexdigest()[:24]
    work = os.path.join(juego, "user_data", "dbz3", "cac_xv2")
    os.makedirs(work, exist_ok=True)
    dst = os.path.join(work, huella + ".bin")
    if not os.path.isfile(dst):
        tpl = os.path.join(work, "plantilla_%d.bin" % ficha.get("donante", 0))
        fids = KFID.get(ficha.get("donante", 0)) or KFID[0]
        with open(tpl, "wb") as fh:
            fh.write(afs.entry(fids[0]))
        xc.componer(sel, tpl, dst + ".tmp", hsv)
        os.replace(dst + ".tmp", dst)
    with open(dst, "rb") as fh:
        return fh.read(), {"fuente": "xv2:%s %s" % (sel["raza"], ficha.get("xv2", "")), "bin": dst}


def modelo_ut(afs, ficha, juego):
    """G06a: una sola malla/materiales para la vista previa y el combate.

    Los IDs 100..105 son identificadores del catálogo, no personajes B3. No se
    intenta teñir el avatar con las máscaras heurísticas del traje anfitrión.
    """
    import hashlib
    import ut_cuerpo
    familia = ("b0", "b1", "a0", "a1", "c0", "c1")[ficha["cuerpo"] - 100]
    spr = ut_cuerpo.fuente(familia, ficha.get("traje", 0) + 1, juego=juego)
    datos_clave = (json.dumps(ficha, sort_keys=True) + str(spr) + str(spr.stat().st_mtime_ns) +
                   "ut-real-v4:" + _huella_conversor())
    key = hashlib.sha256(datos_clave.encode()).hexdigest()[:24]
    work = os.path.join(juego, "user_data", "dbz3", "cac_ut")
    os.makedirs(work, exist_ok=True)
    dst, rep = os.path.join(work, key + ".bin"), os.path.join(work, key + ".json")
    if not os.path.isfile(dst) or not os.path.isfile(rep):
        plantilla = os.path.join(work, "esqueleto_%s.bin" % familia[0])
        if not os.path.isfile(plantilla):
            with open(plantilla, "wb") as fh:
                fh.write(afs.entry(327 if familia[0] == "b" else 264))
        colores = {}
        for z, c in zip(("arriba", "abajo", "cinturon", "calzado"), ficha.get("partes", [CERO] * 4)):
            if tuple(c) != CERO:
                colores[z] = tuple(float(x) for x in _hsv_rgb(*[np.array(x) for x in objetivo(c)]))
        if tuple(ficha.get("c1", CERO)) != CERO:
            colores["pelo"] = tuple(float(x) for x in _hsv_rgb(*[np.array(x) for x in objetivo(ficha["c1"])]))
        else:
            # La textura de pelo UT es verde-oliva; sin color elegido, pelo oscuro.
            colores["pelo"] = (0.16, 0.11, 0.08)
        ut_cuerpo.construir(plantilla, spr, dst, colores=colores, informe=rep, pelo=int(ficha.get("pelo", 0)))
    with open(dst, "rb") as fh:
        datos = fh.read()
    with open(rep, encoding="utf-8") as fh:
        r = json.load(fh)
    return datos, r


def leer_ficha(txt):
    """--ficha "cuerpo=0;traje=2;partes=30,20,20|0,0,0|0,0,0|0,0,0;pelo=1;c1=60,0,0;c2=0,0,0;logo=1;scouter=1"."""
    f = {}
    for par in txt.split(";"):
        if "=" not in par:
            continue
        k, v = par.split("=", 1)
        k = k.strip()
        if k == "partes":
            f[k] = [tuple(int(x) for x in p.split(",")) for p in v.split("|")]
        elif k in ("c1", "c2", "ojos", "piel"):
            f[k] = tuple(int(x) for x in v.split(","))
        elif k in ("nombre", "xv2", "aura_col", "ki_col"):
            f[k] = v
        else:
            f[k] = int(v)
    return f


def limpiar_fichas(salida, guardar=12):
    """Las vistas de ficha (f_*.malla) se acumulan al editar: deja las `guardar` mas nuevas."""
    fs = sorted((os.path.join(salida, n) for n in os.listdir(salida) if n.startswith("f_") and n.endswith(".malla")),
                key=os.path.getmtime, reverse=True)
    for p in fs[guardar:]:
        try:
            os.remove(p)
        except OSError:
            pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--juego", required=True, help="carpeta del juego (la de dbz3.exe)")
    ap.add_argument("--us", default=None, help="carpeta us/ con data_cmn.afs (por defecto la que encuentre)")
    ap.add_argument("--size", type=int, default=384)
    ap.add_argument("--giros", type=int, default=12)
    ap.add_argument("--faltan", action="store_true", help="solo las que no existen")
    ap.add_argument("--solo", default=None, help="id_traje (p. ej. 0_0)")
    ap.add_argument("--ficha", default=None, help='vista de la ficha: "cuerpo=0;traje=2;partes=h,s,v|...;pelo=1;'
                    'c1=h,s,v;c2=h,s,v;logo=1;scouter=1" (con --clave)')
    ap.add_argument("--clave", default=None, help="nombre del fichero de la ficha (f_<hash>): <clave>.malla")
    ap.add_argument("--combate", type=int, default=0, help="con --ficha: escribe el traje del combate de la ficha N "
                    "(mods/heroe_fichaN, colores cocidos en las texturas)")
    a = ap.parse_args()
    salida = os.path.join(a.juego, "ingame_ui", "heroe_vista")
    os.makedirs(salida, exist_ok=True)
    if a.ficha and a.combate:
        f = leer_ficha(a.ficha)
        if f.get("cuerpo", 0) not in KFID and not 100 <= f.get("cuerpo", 0) <= 115:
            print("vista_heroe: cuerpo %s sin modelo" % f.get("cuerpo"))
            return 2
        t0 = time.time()
        afs = rb.Afs(os.path.join(rb.us_dir(a.us), "data_cmn.afs"), tempfile.mkdtemp(prefix="vista_heroe_"))
        r = escribir_mod_combate(a.juego, a.combate, f, a.ficha, afs)
        print("vista_heroe: combate de la ficha %d: %s (%.1f s)" % (a.combate, r, time.time() - t0))
        # C4: la historia del Heroe en Dragon Universe se monta en el mismo momento que su cuerpo propio (al pulsar JUGAR
        # se lee todo junto); sin estas campanas "Empezar" lanza las misiones de siempre (heroe.cpp DuDisponible)
        try:
            sys.path.insert(0, os.path.dirname(HERE))
            import heroe_sagas_a_du as hs  # noqa: PLC0415
            rc = hs.montar_si_hace_falta(os.path.join(a.juego, "mods"), rb.us_dir(a.us), None, 34 + min(max(a.combate, 1), 3), f.get("cuerpo", 0), f.get("nombre"))
            print("vista_heroe: historia del Heroe en DU (3 casillas): %s" % ("al dia" if rc == 0 else "FALLO %s" % rc))
        except Exception as e:   # noqa: BLE001 (sin historia: el Heroe sigue con las misiones)
            print("vista_heroe: historia del Heroe en DU no montada: %s" % e)
        return 0
    if a.ficha:
        f = leer_ficha(a.ficha)
        if 100 <= f.get("cuerpo", 0) <= 115:   # G06a/C6: exportar exactamente el modelo del combate
            work = tempfile.mkdtemp(prefix="vista_cac_ut_")
            afs = rb.Afs(os.path.join(rb.us_dir(a.us), "data_cmn.afs"), work)
            datos, r = (modelo_xv2 if f["cuerpo"] >= XV2_CUERPO[0] else modelo_ut)(afs, f, a.juego)
            m = mr.Model(datos)
            dst = os.path.join(salida, (a.clave or "f_prueba") + ".malla")
            exportar_malla(m, pose_de(m), dst)
            # El avatar UT completo puede superar el presupuesto de vértices de
            # ImGui. La tira usa EXACTAMENTE esta malla, UV y materiales, sin LOD
            # ni huecos; el combate mantiene la geometría completa.
            # K: la vista previa del menu pinta la .malla en 3D (Pintar3D); la tira de 12 giros (9 s de raster por software)
            # solo es el marcador mientras carga: 1 solo fotograma pequeno (el juego deduce los giros de ancho/alto)
            tira(m, 192, 1).save(os.path.splitext(dst)[0] + ".png")
            print("vista_heroe: cuerpo UT -> %s (%s)" % (dst, r["fuente"]))
            return 0
        fids = KFID.get(f.get("cuerpo", 0))
        if not fids:
            print("vista_heroe: cuerpo %s sin modelo" % f.get("cuerpo"))
            return 2
        fid = fids[min(max(f.get("traje", 0), 0), len(fids) - 1)]
        t0 = time.time()
        work = tempfile.mkdtemp(prefix="vista_heroe_")
        afs = rb.Afs(os.path.join(rb.us_dir(a.us), "data_cmn.afs"), work)
        m = mr.Model(afs.entry(fid))
        dst = os.path.join(salida, (a.clave or "f_prueba") + ".malla")
        r = ficha_malla(m, f, a.juego, dst, afs)
        limpiar_fichas(salida)
        print("vista_heroe: ficha %s (fid %d) -> %s %s (%.1f s)" % (a.ficha, fid, dst, r, time.time() - t0))
        return 0
    marca = os.path.join(salida, "_generando")
    try:
        with open(marca, "w") as fh:
            fh.write(str(os.getpid()))
    except OSError:
        pass
    work = tempfile.mkdtemp(prefix="vista_heroe_")
    try:
        afs = rb.Afs(os.path.join(rb.us_dir(a.us), "data_cmn.afs"), work)
        hechas = 0
        for cid, fids in KFID.items():
            for t, fid in enumerate(fids):
                nombre = "%d_%d" % (cid, t)
                if a.solo and a.solo != nombre:
                    continue
                dst = os.path.join(salida, nombre + ".png")
                if a.faltan and os.path.exists(dst) and os.path.exists(os.path.join(salida, nombre + ".malla")):
                    continue
                t0 = time.time()
                try:
                    m = mr.Model(afs.entry(fid))
                    img = tira(m, a.size, a.giros)
                    nt, ntex = exportar_malla(m, pose_de(m), os.path.join(salida, nombre + ".malla"))
                except Exception as e:   # noqa: BLE001 (un modelo raro no para el resto)
                    print("vista_heroe: %s (fid %d) FALLO: %s" % (nombre, fid, e))
                    continue
                tmp = dst + ".tmp.png"
                img.save(tmp)
                os.replace(tmp, dst)
                hechas += 1
                print("vista_heroe: %s fid %d -> %s + malla 3D (%d triangulos, %d texturas) (%.1f s)"
                      % (nombre, fid, dst, nt, ntex, time.time() - t0))
        print("vista_heroe: %d tiras hechas en %s" % (hechas, salida))
    finally:
        try:
            os.remove(marca)
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
