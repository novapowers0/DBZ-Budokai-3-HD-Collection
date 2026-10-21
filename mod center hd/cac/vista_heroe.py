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
    tri_tex = []
    for mi in model.tri_mat:
        mat = model.mats[mi]
        k = (mat["tex"], mat["shader"], mat["env"], tuple(np.round(mat["diffuse"], 3)), mat["blend"])
        if k not in clave_tex:
            clave_tex[k] = len(texs)
            texs.append(_textura_cocida(model, mat))
        tri_tex.append(clave_tex[k])
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
        if any(t in label for t in toks):
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


def ficha_malla(model, ficha, juego, path):
    """Exporta la malla HMV1 de la ficha (mismo formato que exportar_malla). Devuelve un resumen para el log."""
    import struct  # noqa: PLC0415
    pose = pose_de(model)
    P, N = model.posed(pose)
    zonas = zonas_ficha(model, P)
    partes = ficha.get("partes", [CERO] * 4)
    c1, c2 = tuple(ficha.get("c1", CERO)), tuple(ficha.get("c2", CERO))
    pelo = int(ficha.get("pelo", 0))
    pieza = buscar_pelo(juego, pelo) if pelo > 0 else None
    tris, texs, cache = [], [], {}   # tris: (3 pos, 3 nrm, 3 uv, tex, blend)
    pelo_nativo = [i for i, z in enumerate(zonas) if z == "pelo"]
    for i, tri in enumerate(model.tris):
        z = zonas[i]
        if z == "pelo" and pieza:
            continue
        mi = model.tri_mat[i]
        mat = model.mats[mi]
        k = (mat["tex"], mat["shader"], mat["env"], tuple(np.round(mat["diffuse"], 3)), mat["blend"], z)
        if k not in cache:
            t = _textura_cocida(model, mat)
            if z == "pelo":
                t = tenir(t, c1, c2, piel=False)
            elif isinstance(z, int):
                t = tenir(t, tuple(partes[z]) if z < len(partes) else CERO)
            cache[k] = len(texs)
            texs.append(t)
        tris.append((P[tri], N[tri], model.uv[tri], cache[k], mat["blend"]))
    usados = np.unique(model.tris.reshape(-1))
    lo, hi = P[usados].min(0), P[usados].max(0)
    resumen = {"pelo": "nativo"}
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
    elif pelo > 0:
        resumen["pelo"] = "pieza %d no encontrada: nativo" % pelo
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
    out = bytearray(b"HMV1")
    out += struct.pack("<IIf", len(texs), len(tris), float(hi[1] - lo[1]))
    for t in texs:
        img = (np.clip(t, 0, 1) * 255 + 0.5).astype(np.uint8)
        out += struct.pack("<II", img.shape[1], img.shape[0]) + img.tobytes()
    for (pp, nn, uu, ti, bl) in tris:
        for j in range(3):
            out += struct.pack("<8f", *(pp[j] - off), *nn[j], *uu[j])
        out += struct.pack("<iI", ti, 1 if bl else 0)
    tmp = path + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(out)
    os.replace(tmp, path)
    resumen.update(triangulos=len(tris), texturas=len(texs))
    return resumen


def zonas_ficha(model, P):
    """zonas_triangulos + el pelo pegado al hueso de la cabeza (triangulos oscuros por encima de los ojos)."""
    zonas = zonas_triangulos(model)
    cara = [i for i, z in enumerate(zonas) if z == "cara"]
    if cara:
        vs = np.unique(model.tris[cara].reshape(-1))
        ylo, yhi = P[vs, 1].min(), P[vs, 1].max()
        cocidas = {}
        for i in cara:
            mat = model.mats[model.tri_mat[i]]
            if mat["tex"] not in cocidas:
                cocidas[mat["tex"]] = _textura_cocida(model, mat)
            t = cocidas[mat["tex"]]
            h, w = t.shape[:2]
            uv = model.uv[model.tris[i]].mean(0)
            px = t[int(np.clip((uv[1] % 1) * h, 0, h - 1)), int(np.clip((uv[0] % 1) * w, 0, w - 1)), :3]
            if px.max() < 0.22 and P[model.tris[i], 1].min() > ylo + (yhi - ylo) * 0.62:
                zonas[i] = "pelo"
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


def injertar_ficha(amb, pieza, model=None, P=None, zonas=None, c1=CERO, c2=CERO, scouter=0, reserva=0):
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
                       extras=scouter_extras(model, P, zonas) if scouter else None)


# ficha.ropa -> prenda de Xenoverse (HUM_<item>_Bust del data.cpk del jugador); 0 = la nativa del cuerpo
PRENDAS = {1: {"item": "500", "malla": "dougi", "lod": "02", "rgb": (0.93, 0.45, 0.06)}}


def combate_bin(amb, ficha, juego=None):
    """El #AMB HD del cuerpo con los colores de la ficha COCIDOS en sus texturas (#AZT DXT3, mismo tamano: el #AWO no
    cambia). Es la parte de H2 que el combate puede llevar ya: geometria nueva (peinado UT, logo y scouter en 3D) es la
    Via B del port (bloqueada, AGENTS.md 3.4.10). Devuelve (bytes, resumen)."""
    from PIL import ImageDraw  # noqa: PLC0415
    import texture_b3 as tb  # noqa: PLC0415
    amb = bytearray(amb)
    model = mr.Model(bytes(amb))
    P, _ = model.posed(pose_de(model))
    zonas = zonas_ficha(model, P)
    partes = ficha.get("partes", [CERO] * 4)
    c1, c2 = tuple(ficha.get("c1", CERO)), tuple(ficha.get("c2", CERO))
    color = {0: tuple(partes[0]), 1: tuple(partes[1]), 2: tuple(partes[2]), 3: tuple(partes[3]), "pelo": c1}
    _, texs = tb.parse_azt(bytes(amb))
    r = {}
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
    pieza = buscar_pelo(juego, pelo) if pelo > 0 else None
    scouter = int(ficha.get("scouter", 0))
    if pieza or scouter:   # peinado UT / scouter en el combate: geometria nueva pegada a la cabeza (TABLE de paleta intacta)
        try:
            amb2, ri = injertar_ficha(bytes(amb), pieza, model, P, zonas, c1, c2, scouter,
                                      reserva=450 if int(ficha.get("ropa", 0)) in PRENDAS else 0)   # ventanas para la prenda
            amb, r["pelo"] = bytearray(amb2), ri
        except Exception as e:   # noqa: BLE001 (sin injerto: el combate sale con el pelo nativo tenido)
            r["pelo"] = "FALLO %s: %s" % (type(e).__name__, e)
    if pelo > 0 and not pieza:
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
    return bytes(amb), r


ID_HEROE = 63        # plaza pedida al roster (44-63 = extra). Si esta ocupada roster_build da otra: el juego lee la real
MOD_HEROE = "heroe_propio"   # mods/heroe_propio: el heroe de la ficha activa como PERSONAJE NUEVO (heroe.cpp IdPropio)


def escribir_personaje_propio(juego, slot, ficha, args_txt, datos, r, fid):
    """El heroe como personaje con ID propio (mods/heroe_propio/personaje.toml de roster_build): su modelo es el cuerpo
    ya injertado/tenido (`datos`), el moveset/tecnicas/aura/voz los del donante (el estilo) y el nombre el de la ficha;
    el icono, el retrato y la cara del marcador los renderiza roster_build del propio modelo. ficha.txt = lo que el juego
    compara (sin el donante, que se puede cambiar entre combates: heroe.cpp lo vuelve a poner en marcha)."""
    mod = os.path.join(juego, "mods", MOD_HEROE)
    os.makedirs(os.path.join(mod, "modelos"), exist_ok=True)
    with open(os.path.join(mod, "modelos", "base.bin"), "wb") as fh:
        fh.write(datos)
    nombre = "".join(c for c in str(ficha.get("nombre") or "Heroe") if c.isprintable() and c not in "\"\\")[:24] or "Heroe"
    toml = ("# Generado por vista_heroe.py --combate (Modo Heroe, ficha %d). No editar: se rehace al confirmar.\n"
            "[personaje]\nnombre = \"%s\"\nid = %d\ndonante = %d\n"
            "modelos = [\"modelos/base.bin\"]\nformas = 1\n") % (slot, nombre, ID_HEROE, int(ficha.get("donante", 0)))
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
    if 100 <= ficha.get("cuerpo", 0) <= 105:   # G06a: malla CaC completa; ninguna superficie B3
        datos, r = modelo_ut(afs, ficha, juego)
        return escribir_personaje_propio(juego, slot, ficha, args_txt, datos, r, -1)
    fids = KFID.get(ficha.get("cuerpo", 0))
    fid = fids[min(max(ficha.get("traje", 0), 0), len(fids) - 1)]
    datos, r = combate_bin(afs.entry(fid), ficha, juego)
    if ficha.get("nombre") is not None:
        return escribir_personaje_propio(juego, slot, ficha, args_txt, datos, r, fid)
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
        elif k in ("c1", "c2"):
            f[k] = tuple(int(x) for x in v.split(","))
        elif k == "nombre":
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
        if f.get("cuerpo", 0) not in KFID and not 100 <= f.get("cuerpo", 0) <= 105:
            print("vista_heroe: cuerpo %s sin modelo" % f.get("cuerpo"))
            return 2
        t0 = time.time()
        afs = rb.Afs(os.path.join(rb.us_dir(a.us), "data_cmn.afs"), tempfile.mkdtemp(prefix="vista_heroe_"))
        r = escribir_mod_combate(a.juego, a.combate, f, a.ficha, afs)
        print("vista_heroe: combate de la ficha %d: %s (%.1f s)" % (a.combate, r, time.time() - t0))
        return 0
    if a.ficha:
        f = leer_ficha(a.ficha)
        if 100 <= f.get("cuerpo", 0) <= 105:   # G06a: exportar exactamente el modelo del combate
            work = tempfile.mkdtemp(prefix="vista_cac_ut_")
            afs = rb.Afs(os.path.join(rb.us_dir(a.us), "data_cmn.afs"), work)
            datos, r = modelo_ut(afs, f, a.juego)
            m = mr.Model(datos)
            dst = os.path.join(salida, (a.clave or "f_prueba") + ".malla")
            exportar_malla(m, pose_de(m), dst)
            # El avatar UT completo puede superar el presupuesto de vértices de
            # ImGui. La tira usa EXACTAMENTE esta malla, UV y materiales, sin LOD
            # ni huecos; el combate mantiene la geometría completa.
            tira(m, a.size, a.giros).save(os.path.splitext(dst)[0] + ".png")
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
        r = ficha_malla(m, f, a.juego, dst)
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
