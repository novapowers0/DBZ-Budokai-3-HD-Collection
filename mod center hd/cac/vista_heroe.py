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
    b3, b3_resumen = _aplicar_piezas_b3(bytes(model.data), ficha, juego, afs, tintar=False) if afs is not None else (
        bytes(model.data), {})
    if b3 != model.data:
        model = mr.Model(b3)
    pose = pose_de(model)
    P, N = model.posed(pose)
    zonas = zonas_ficha(model, P)
    partes = ficha.get("partes", [CERO] * 4)
    c1, c2 = tuple(ficha.get("c1", CERO)), tuple(ficha.get("c2", CERO))
    pelo = int(ficha.get("pelo", 0))
    pieza = buscar_pelo(juego, pelo) if pelo > 0 and _xv2_piezas_activas(juego) else None
    tris, texs, cache = [], [], {}   # tris: (3 pos, 3 nrm, 3 uv, tex, blend)
    pelo_nativo = [i for i, z in enumerate(zonas) if z == "pelo"]
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
        dm, ht, dvs, Q, QN, tn = nat
        remap = {int(v): i for i, v in enumerate(dvs)}
        tcache = {}
        for t, tri in zip(ht, tn):
            mat = dm.mats[dm.tri_mat[t]]
            k = id(mat)
            if k not in tcache:
                tcache[k] = len(texs)
                texs.append(tenir(_textura_cocida(dm, mat), c1, c2, piel=False))
            tris.append((Q[list(tri)], QN[list(tri)], dm.uv[dm.tris[t]], tcache[k], mat["blend"]))
        resumen["pelo"] = "nativo B3 donante %d (%d triangulos)" % (PELOS_NATIVOS[pelo], len(tn))
        hi = np.maximum(hi, Q.max(0))
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


# Sin XV2 (o sin las piezas UT del jugador) los peinados 1..8 salen de personajes NATIVOS de B3: se injerta la zona de
# pelo de su propio bin con el mismo pipeline (awo_tools/injertar_pelo.py). La geometria se lee de su data_cmn.afs.
PELOS_NATIVOS = {1: 0, 8: 39}   # 1.5.5: solo los perfectos (misma cabeza que Goku); el resto queda para cuando se adapte la altura. Antes: {2: 7, 4: 235, 5: 252, 6: 13, 9: 298}   # 24/10: fuera 3 Trunks nino (bola) y 7 Piccolo (antenas flotan); 4 Gohan vuelve tras arreglar zona_de |   # Goku, Vegeta, Trunks nino, Gohan (Cell), Goku nino, Yamcha, Piccolo, Bardock, Goten (= kPelosNativos de heroe_ui.cpp); H3: Trunks/Krilin/Ten/Videl no tenian pelo detectable

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
        _B3_CATALOGO = {z: {int(p["fid"]): p for p in raw.get(z, [])
                            if p.get("ok") is True and isinstance(p.get("fid"), int) and p.get("nombre")}
                        for z in B3_ZONAS}
    except Exception as e:  # noqa: BLE001 (sin catalogo: el aspecto base sigue disponible)
        _B3_CATALOGO = {z: {} for z in B3_ZONAS}
        print("vista_heroe: catalogo de piezas B3 no disponible: %s" % e)
    return _B3_CATALOGO


def _clave_hueso_b3(label):
    s = label.lstrip("X")
    s = s.split("_", 1)[1] if "_" in s else s
    return s.lstrip("_")


def _zona_hueso_b3(label):
    s = _clave_hueso_b3(label)
    if "HAIR" in s:
        return "pelo"
    if s.startswith("M_") or any(t in s for t in ("FACE", "TEETH", "EYE", "HEAD", "MOUTH", "TONGUE")):
        return "cara"
    if any(t in s for t in ("FOOT", "NLF", "NRF")):
        return "botas"
    if any(t in s for t in ("HAND", "NLA", "NRA", "ARM2")):
        return "guantes"
    if "LEG" in s:
        return "piernas"
    if any(t in s for t in ("CHEST", "STMC", "WAIST", "OBI", "BODY", "NECK", "NW", "TAIL", "ARMROT", "ARM1", "CHN")):
        return "torso"
    return "accesorios"


def _zonas_pieza_b3(model):
    out = []
    for tri in model.tris:
        from collections import Counter  # noqa: PLC0415
        c = Counter(_zona_hueso_b3(model.labels[b]) if b < len(model.labels) else "accesorios"
                    for b in model.vbone[tri])
        out.append(c.most_common(1)[0][0])
    return out


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
    return np.asarray(Image.fromarray(img).resize((width, height), Image.Resampling.LANCZOS))


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
    dz = _zonas_pieza_b3(donor)
    td_h = td_h if td_h is not None else ip.triangulos_por_draw(host)
    por_draw = {}
    for i, (draw, _) in enumerate(td_h):
        por_draw.setdefault(draw, []).append(i)
    candidatos = []
    for draw, indices in por_draw.items():
        if host.table.draws[draw]["prim"] != 5:
            continue
        nz = sum(1 for i in indices if hz[i] == zone)
        if nz:
            candidatos.append((all(hz[i] == zone for i in indices), nz, -draw, draw))
    candidatos.sort(reverse=True)
    if not candidatos:
        raise ValueError("sin draw anfitrion exclusivo para la zona %s" % zona)
    chosen = candidatos[0][3]

    src_tri_idx = np.flatnonzero(np.asarray(dz) == zone)
    if not len(src_tri_idx):
        raise ValueError("el donante no tiene geometria de %s" % zona)
    src_original = donor.tris[src_tri_idx]
    used = np.unique(src_original.reshape(-1))
    remap = {int(v): i for i, v in enumerate(used)}
    src_tris = np.asarray([[remap[int(v)] for v in tri] for tri in src_original], int)
    bones_src = donor.vbone[used]
    host_suf = {_clave_hueso_b3(label): i for i, label in enumerate(host.labels)}
    par = donor.parent
    bone_out = []
    for b in bones_src:
        seen, cur, mapped = set(), int(b), -1
        while 0 <= cur < len(donor.labels) and cur not in seen:
            seen.add(cur)
            mapped = host_suf.get(_clave_hueso_b3(donor.labels[cur]), -1)
            if mapped >= 0:
                break
            cur = par[cur]
        bone_out.append(mapped)
    bone_out = np.asarray(bone_out, int)
    if (bone_out < 0).any():
        raise ValueError("hay huesos de la pieza sin equivalente ni ancestro en Goku")
    host_tex = host.table.materials[host.table.draws[chosen]["material"]]["tex"]
    hh, ww = host.tex[host_tex].shape[:2]
    rgba = _textura_donante_b3(donor, src_tri_idx, ww, hh)
    if color is not None and tuple(color) != CERO:
        rgba = np.clip(tenir(rgba.astype(np.float32) / 255.0, tuple(color), piel=True) * 255 + 0.5, 0, 255).astype(np.uint8)
    quitar = np.asarray([(z == zone) and (draw == chosen) for (draw, _), z in zip(td_h, hz)], bool)
    pieza = {"host": chosen, "quitar": quitar, "P": dp[used], "N": dn[used], "UV": donor.uv[used],
             "bones": bone_out, "tris": src_tris, "rgba": rgba, "zona": zona}
    if preparar:
        return pieza
    out, summary = ir.injertar_ropa(current_bytes, host, quitar, chosen, dp[used], dn[used], donor.uv[used],
                                     bone_out, src_tris, rgba)
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
        for cantidad in range(len(preparar), 0, -1):
            for subset in combinations(preparar, cantidad):
                try:
                    actual, batch_summary = ir.injertar_ropa_multi(bytes(amb), base_model, subset)
                    aplicadas = list(subset)
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
                resumen[z] = {"pieza": p["nombre"], **por_zona.get(z, {}),
                              "ventanas": batch_summary.get("ventanas", {})}
            else:
                resumen[z] = "Base (fallo conjunto: %s)" % batch_summary.get("fallo", "tope de ventanas")
    return actual, resumen


def _pelo_donante(model, P, zonas, donor_bytes):
    """Pelo de un cuerpo nativo ajustado a la cabeza del heroe: (dm, ht, dvs, Q, QN, tris) o None si el donante no tiene pelo.
    Lo usan el combate (_injertar_pelo_nativo) y la vista previa del menu (ficha_malla): el mismo injerto en las dos."""
    dm = mr.Model(bytes(donor_bytes))
    pose = pose_de(dm)
    Pd, Nd = dm.posed(pose)
    zd = zonas_ficha(dm, Pd)
    ht = [i for i, z in enumerate(zd) if z == "pelo"]
    if not ht:
        return None
    dvs = np.unique(dm.tris[ht].reshape(-1))
    plo, phi = Pd[dvs].min(0), Pd[dvs].max(0)
    host = [i for i, z in enumerate(zonas) if z == "pelo"]
    if host:
        vs = np.unique(model.tris[host].reshape(-1))
        dlo, dhi = P[vs].min(0), P[vs].max(0)
    else:   # calvo: caja de la cabeza (mismas medidas que ajustar_pelo)
        cara = [i for i, z in enumerate(zonas) if z == "cara"]
        u = np.unique(model.tris[cara].reshape(-1)) if cara else np.unique(model.tris.reshape(-1))
        clo, chi = P[u].min(0), P[u].max(0)
        dlo = np.array([clo[0], chi[1] - (chi[1] - clo[1]) * 0.45, clo[2]])
        dhi = np.array([chi[0], chi[1] + (chi[1] - clo[1]) * 0.25, chi[2]])
    esc = min((dhi[0] - dlo[0]) / max(phi[0] - plo[0], 1e-6), (dhi[2] - dlo[2]) / max(phi[2] - plo[2], 1e-6)) * 1.02
    centro_p = np.array([(plo[0] + phi[0]) / 2, plo[1], (plo[2] + phi[2]) / 2])
    centro_d = np.array([(dlo[0] + dhi[0]) / 2, dlo[1] + (dhi[1] - dlo[1]) * 0.12, (dlo[2] + dhi[2]) / 2])
    Q = (Pd[dvs] - centro_p) * esc + centro_d
    QN = Nd[dvs].copy()
    QN /= np.maximum(np.linalg.norm(QN, axis=1), 1e-9)[:, None]
    remap = {int(v): i for i, v in enumerate(dvs)}
    tris = [tuple(remap[int(v)] for v in dm.tris[t]) for t in ht]
    return dm, ht, dvs, Q, QN, tris


def _injertar_pelo_nativo(amb, model, P, zonas, donor_bytes, c1=CERO, c2=CERO):
    """Injerta el pelo de un cuerpo nativo (donante) en el cuerpo del heroe. Devuelve (bytes, resumen)."""
    import injertar_pelo as ip  # noqa: PLC0415
    d = _pelo_donante(model, P, zonas, donor_bytes)
    if d is None:
        return bytes(amb), "donante sin zona de pelo"
    dm, ht, dvs, Q, QN, tris = d
    rgb = lambda c: None if tuple(c) == CERO else tuple(float(x) for x in _hsv_rgb(*[np.array(x) for x in objetivo(c)]))  # noqa: E731
    out, r = ip.injertar(amb, model, [z == "pelo" for z in zonas], Q, QN, tris, rgb1=rgb(c1), rgb2=rgb(c2),
                         cara_mask=[z == "cara" for z in zonas])
    r["pelo_nativo"] = "donante %d (%d tris)" % (int(np.count_nonzero(ht)), len(tris))
    return out, r


def combate_bin(amb, ficha, juego=None, afs=None):
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
    pieza = buscar_pelo(juego, pelo) if pelo > 0 and _xv2_piezas_activas(juego) else None
    scouter = int(ficha.get("scouter", 0))
    if pieza or scouter:   # peinado UT / scouter en el combate: geometria nueva pegada a la cabeza (TABLE de paleta intacta)
        try:
            amb2, ri = injertar_ficha(bytes(amb), pieza, model, P, zonas, c1, c2, scouter,
                                      reserva=450 if int(ficha.get("ropa", 0)) in PRENDAS else 0)   # ventanas para la prenda
            amb, r["pelo"] = bytearray(amb2), ri
        except Exception as e:   # noqa: BLE001 (sin injerto: el combate sale con el pelo nativo tenido)
            r["pelo"] = "FALLO %s: %s" % (type(e).__name__, e)
    elif pelo > 0 and afs is not None and pelo in PELOS_NATIVOS:   # sin pieza UT/XV2: pelo de un personaje NATIVO de B3
        try:
            fids = KFID.get(PELOS_NATIVOS[pelo]) or KFID[0]
            amb2, ri = _injertar_pelo_nativo(bytes(amb), model, P, zonas, afs.entry(fids[0]), c1, c2)
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
            "modelos = [\"modelos/base.bin\"]\nformas = 1\ncelda = false\n") % (slot, nombre, ID_HEROE, int(ficha.get("donante", 0)))
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
        elif k in ("c1", "c2"):
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
