#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xv2_a_b3.py - un personaje de Dragon Ball Xenoverse 2 (CPK del jugador) -> bin HD de Budokai 3 (E3, studies 5).

Reutiliza awo_tools/sdbh_model.py (el conversor de Heroes: plantilla HD del donante, rig, texturas, AWG, verificacion):
solo se le sustituye la LECTURA de la fuente (load_source) por una sintetica hecha con los datos de XV2.

  python xv2_a_b3.py convertir JRN 000 <plantilla HD.bin> <salida.bin> [--bcs-set N]
  python xv2_a_b3.py info JRN 000

Medido (22/10/2026, GOK_000 de XV2 contra el Goku de B3 como plantilla):
  * esqueleto XV2 (122 huesos, T-pose, giros ~identidad) frente a B3: se emparejan por nombre 13 huesos por lado/centro
    (Base->waist, Spine1->stmc, Spine2->chest, Neck1, Head, Shoulder->chn, Arm1, Arm2, Hand->handrot, Leg1, Leg2, Foot->foot1,
    Toe->foot2); los demas suben al padre emparejado. Posiciones de XV2 (x10, z espejada); los GIROS de cada hueso son los
    de la plantilla de B3, asi las animaciones de B3 giran sobre los mismos ejes.
  * escala S_XV = 1.1255 (cuello-tobillo del Goku B3 = 13.99 frente a 12.43 del de XV2); el modelo se baja para que el
    tobillo quede a la altura del tobillo de la plantilla.
  * textura XV2: R,G,B = mascaras de zona, A = lineas de detalle; el color sale del BCS (grupo de color, fila) o, si no, del
    EMM (MatCol0..3). Aqui: albedo = C1*(1-R-G-B) + C2*R + C3*G + C4*B, y las lineas (A) oscurecen. [H] aproximacion.
"""
import argparse
import os
import re
import sys

import re
import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xv2_leer as L  # noqa: E402

xm = L.xm
S = None  # sdbh_model (se importa en uso)

S_XV = 1.1255

# hueso de XV2 -> nombre estilo Heroes ({s} = r/l)
REN = {"Base": "waist", "Spine1": "stmc", "Spine2": "chest", "Neck1": "neck", "Head": "head",
       "Shoulder": "{s}chn", "Arm1": "{s}arm1", "Arm2": "{s}arm2", "Hand": "{s}handrot",
       "Leg1": "{s}leg1", "Leg2": "{s}leg2", "Foot": "{s}foot1", "Toe": "{s}foot2"}


def renombrar(n):
    p = n.split("_")
    if MANOS and len(p) == 3 and p[0] == "h" and p[1] in ("L", "R") and re.fullmatch(r"(Thumb|Index|Middle|Ring|Pinky)[123]", p[2]):
        return p[1] + p[2]          # K: dedos con el nombre de sdbh_model.pose_hand (RIndex1...): las variantes de mano posan el dedo
    if len(p) < 3 or p[0] != "b":
        return n
    clave, lado = "_".join(p[2:]), p[1].lower()
    if p[1] == "C":
        return REN.get(clave, n) if "{s}" not in REN.get(clave, "{s}") else n
    r = REN.get(clave)
    return r.format(s=lado) if r else n


class EskSint:
    """ESK de XV2 con la interfaz del Esk de sdbh_model (names, parent, world 4x4, index, descendants)."""

    def __init__(self, esk, tpl, s, dy):
        self.names = [renombrar(n) for n in esk.names]
        self.parent = list(esk.parent)
        self.index = {n: i for i, n in enumerate(self.names)}
        TW = tpl.world()
        rot = {}
        for i, n in enumerate(self.names):
            suf = S.sdbh_suffix(n)
            j = next((k for k, l in enumerate(tpl.labels) if S.bone_suffix(l) == suf), None)
            rot[i] = TW[j][:3, :3] if j is not None else np.eye(3)
        self.world = []
        for i in range(len(self.names)):
            m = np.eye(4)
            m[:3, :3] = rot[i]
            m[:3, 3] = esk.world[i] * s + np.array([0.0, dy, 0.0])
            self.world.append(m)
        self.local = [None] * len(self.names)      # K: local = inv(mundo del padre) @ mundo (los dedos giran en sus ejes de T, como el bind de XV2)
        for i in range(len(self.names)):
            pr = self.parent[i]
            self.local[i] = np.linalg.inv(self.world[pr]) @ self.world[i] if pr >= 0 else self.world[i].copy()

    descendants = None


def _desc(self, root):
    out = {root}
    for i in range(len(self.names)):
        j = i
        while j >= 0 and j not in out:
            j = self.parent[j]
        if j >= 0:
            out.add(i)
    return out


EskSint.descendants = _desc


# ------------------------------------------------------------------ color
def _colores_bcs(bcs, juego_set, tipo):
    """4 colores RGB (0-1) que el BCS asigna a la parte `tipo` del partset, o None."""
    ps = bcs["partsets"][juego_set] if juego_set < len(bcs["partsets"]) else None
    p = (ps or {}).get(tipo)
    if not p or not p["colores"]:
        return None
    g, f = p["colores"][0]
    grp = bcs["colores"][g] if g < len(bcs["colores"]) else None
    if not grp or f >= len(grp["entradas"]):
        return None
    return grp["entradas"][f][:, :3].clip(0, 1)


def hornear(img, col, lineas=0.4):
    """RGBA de XV2 + UN color base -> RGBA de B3. R,G,B de la textura son mascaras de MANCHAS (STAIN: sangre, polvo; en
    reposo no se ven) y se ignoran; solo las lineas finas de detalle (alfa alto; el halo suave del alfa es oclusion y
    no se usa) oscurecen un poco el color, como las lineas del toon de B3."""
    a = img[..., 3].astype(np.float32) / 255.0
    linea = np.clip((a - 0.6) / 0.4, 0, 1)[..., None]
    c = np.asarray(col, np.float32)[None, None, :] * (1.0 - lineas * linea)
    out = np.empty(img.shape, np.uint8)
    out[..., :3] = np.clip(c * 255 + 0.5, 0, 255)
    out[..., 3] = 255
    return out


def color_base(bcs, bset, tipo, nm, m, dye, piel=None):
    if os.environ.get("XV2_DIAG"):         # diagnostico: colores chillones por submalla para ver que cara dibuja el juego
        d = {"jacket_A": (0, 1, 1), "jacket_B": (1, 1, 0), "jacket_B_R": (1, 0, 1), "jacket_botton": (1, 0, 0)}
        if nm in d:
            return np.array(d[nm], np.float32)
    return _color_base(bcs, bset, tipo, nm, m, dye, piel)


def _color_base(bcs, bset, tipo, nm, m, dye, piel=None):
    """Color base (RGB 0-1) de una submalla, por datos: 1) BCS: el grupo de color de la parte cuyo nombre es prefijo del de la
    submalla (CC00_BUST_ -> CC00_BUST_A), fila elegida por el partset; 2) piel: rampa del .dyt.emb (bloque MatScale1X, a u=0.85: calibrado con el fotograma de referencia, la luz de B3 sube ~x1.2);
    3) MatCol0 del EMM; 4) negro si es 0 (como XV2). `piel` fuerza el color de la piel."""
    es_piel = nm.lower().startswith(PIEL) or "skin" in nm.lower()
    if es_piel and piel is not None:
        return np.array(piel, np.float32)
    ps = bcs["partsets"][bset] if bset < len(bcs["partsets"]) else None
    for g, fila in ((ps or {}).get(tipo) or {"colores": []})["colores"]:
        grp = bcs["colores"][g] if g < len(bcs["colores"]) else None
        if grp and nm.upper().startswith(grp["nombre"].upper().rstrip("_")) and fila < len(grp["entradas"]):
            return grp["entradas"][fila][0, :3].clip(0, 1)
    p = (m or {"params": {}})["params"]
    c0 = np.array([p.get("MatCol0%s" % ch, 0.0) for ch in "RGB"], np.float32)
    if c0.sum() > 0:                                   # color fijo del material
        return c0
    if dye:                                            # materiales DFD: el color esta en la linea MatScale1X del .dyt.emb
        blk = int(p.get("MatScale1X", 0))              # (validado: Zamasu pelo plata, piel verde, chaqueta negra con ribete oro,
        r = dye[0][1][min(127, 16 * blk)]              # camisa morada = fotograma de XV2 de su pantalla VS)
        r = r[:, :3].astype(np.float32) / 255.0
        # punto de luz (U_DYE) mezclado con la media de la mitad iluminada de la rampa: las rampas con subida brusca en el
        # extremo (banda de tela celeste) quedaban pastel; las planas (pantalon) no cambian (medido contra la foto de XV2)
        if nm.lower().startswith(PIEL) or "skin" in nm.lower() or "hair" in nm.lower():   # piel y pelo plata: luz alta
            for u_ in (0.92, 0.8, 0.7, 0.6, 0.5, 0.4):      # pieles claras (Jiren): bajar hasta que el tono no se queme a blanco (referencia XV2 ~ (171,163,197))
                c_ = r[int(u_ * 127)]
                if c_.mean() <= 0.70 or "hair" in nm.lower():
                    break
            return c_
        if r[127].mean() < 0.35:                        # materiales oscuros (chaqueta gris-azulada de Zamasu): punto de luz, no la media (quedaba negro)
            return r[127] * 0.95                       # (en juego la luz de B3 sube ~x1.3: medido, hombrera 98 frente a 75 de la referencia)
        return 0.25 * r[int(U_DYE * 127)] + 0.75 * r[int(0.45 * 127):int(0.85 * 127)].mean(0)
    return np.full(3, 0.10, np.float32)


HAIR_OFS = float(os.environ.get("C6_HAIR_OFS", "0.0"))   # C6: pelo separado de la piel (unidades de XV2 x S_XV)
PANT_SOFT = 0.5   # idem para el pantalon (brillo HD de borde mas flojo)
IRIS_ESC = 1.33
ARM_F = float(os.environ.get("E3_ARM_F", "0.3"))   # parte del peso de ArmorParts que va al giro del hombro
FACE_SOFT = 0.8   # fraccion de normal hacia delante mezclada en la cara
U_DYE = 0.82      # punto de la rampa toon del dye: termino medio medido entre el pelo (claro) y el traje de Jiren (oscuro)


def ojo_tex(img, m, dye=None, zoom=None, tinte=None, zoomx=None):
    """Ojo de XV2 (medido en ZMS): la textura es una mascara, no color: B = anillo del iris, alfa = perfil de pupila y borde
    (centro 203, anillo 167, halo -> 0). Color del iris = rampa del .dyt.emb (linea MatScale1X; ZMS: gris plata
    (74,81,90) -> (214,211,231)), blanco del ojo = MatCol3. Pupila = alfa alto fuera del anillo; borde fino oscuro por el halo."""
    p = (m or {"params": {}})["params"]
    esc = np.array([p.get("MatCol3%s" % ch, 1.0) for ch in "RGB"], np.float32)
    h_, w_ = img.shape[:2]
    c_ = int(round(w_ * (1 - 1 / (zoom or IRIS_ESC)) / 2))        # el iris de XV2 ocupa ~0.4 del ancho del ojo visible; el nuestro 0.3
    cx_ = int(round(w_ * (1 - 1 / zoomx) / 2)) if zoomx else c_
    img = np.asarray(Image.fromarray(img).crop((cx_, c_, w_ - cx_, h_ - c_)).resize((w_, h_), Image.BILINEAR))
    iris = esc * 0.6
    if dye:
        r = dye[0][1][min(127, 16 * int(p.get("MatScale1X", 0)))]
        iris = r[int(0.05 * 127), :3].astype(np.float32) / 255.0     # gris azulado oscuro (74,81,90): iris de XV2 medido en la tarjeta
    if tinte is not None:      # C6: iris con color elegido (la rampa del dye solo da el brillo)
        iris = np.asarray(tinte, np.float32) * (iris.mean() / 0.3 + 0.35).clip(0.5, 1.4)
    a = img[..., 3].astype(np.float32) / 255.0
    ring = (img[..., 2].astype(np.float32) / 255.0)[..., None]
    pupila = ((a > 0.6) & (img[..., 2] < 40)).astype(np.float32)[..., None]
    col = esc[None, None, :] * (1 - ring) + iris[None, None, :] * ring
    col = col * (1 - pupila) + 0.06 * pupila
    col = col * (1.0 - 0.30 * np.clip(a * 1.2, 0, 1)[..., None] * (1 - ring) * (1 - pupila))   # borde suave, no linea negra
    o = np.empty(img.shape, np.uint8)
    o[..., :3] = np.clip(col * 255 + 0.5, 0, 255)
    o[..., 3] = 255
    return o


def ojo_malla(sm, n=20):
    """C6: malla de ojo en forma de ALMENDRA (en vez del rectangulo de 35 vertices): se muestrea la malla original en el espacio UV
    (baricentricas) sobre dos anillos y el centro. Asi no hace falta alfa y detras se ve la piel real de la cara."""
    import copy
    uv = sm.uv
    u0, u1, v0, v1 = uv[:, 0].min(), uv[:, 0].max(), uv[:, 1].min(), uv[:, 1].max()
    a, b = (u1 - u0) / 2 * EYE_A, (v1 - v0) / 2 * EYE_B
    uc, vc = (u0 + u1) / 2, (v0 + v1) / 2
    ang = np.linspace(0, 2 * np.pi, n, endpoint=False)
    xs = np.cos(ang)
    ys = np.sign(np.sin(ang)) * np.clip(1 - np.abs(xs) ** 1.8, 0, 1) ** 0.8
    pts = [(uc, vc)]
    for r in (0.5, 1.0):
        pts += [(uc + a * r * x, vc + b * r * y) for x, y in zip(xs, ys)]
    T = sm.tris
    A, B, C = uv[T[:, 0]], uv[T[:, 1]], uv[T[:, 2]]
    d = (B[:, 1] - C[:, 1]) * (A[:, 0] - C[:, 0]) + (C[:, 0] - B[:, 0]) * (A[:, 1] - C[:, 1])
    d = np.where(np.abs(d) < 1e-12, 1e-12, d)
    tri = np.zeros((len(pts), 3), int)
    bar = np.zeros((len(pts), 3))
    for k, p in enumerate(pts):
        w0 = ((B[:, 1] - C[:, 1]) * (p[0] - C[:, 0]) + (C[:, 0] - B[:, 0]) * (p[1] - C[:, 1])) / d
        w1 = ((C[:, 1] - A[:, 1]) * (p[0] - C[:, 0]) + (A[:, 0] - C[:, 0]) * (p[1] - C[:, 1])) / d
        w2 = 1 - w0 - w1
        j = int(np.minimum(np.minimum(w0, w1), w2).argmax())
        tri[k], bar[k] = T[j], (w0[j], w1[j], w2[j])
    w = bar[:, :, None]
    o = copy.copy(sm)
    o.pos = (sm.pos[tri] * w).sum(1)
    o.nrm = (sm.nrm[tri] * w).sum(1)
    o.nrm /= np.linalg.norm(o.nrm, axis=1, keepdims=True) + 1e-9
    o.uv = (sm.uv[tri] * w).sum(1)
    k0 = tri[np.arange(len(pts)), bar.argmax(1)]            # huesos/pesos del vertice de mayor peso baricentrico
    o.bones, o.weights = sm.bones[k0], sm.weights[k0]
    m = len(ang)
    tris = []
    for i in range(m):                                       # abanico centro -> anillo interior y banda interior -> exterior
        j = (i + 1) % m
        tris += [(0, 1 + i, 1 + j), (1 + i, 1 + m + i, 1 + m + j), (1 + i, 1 + m + j, 1 + j)]
    o.tris = np.array(tris, T.dtype)
    return o


def ojo_almendra(img, m, dye, sm, ref, razon=1.0):
    """C6: ojo del CaC de XV2. Su malla es un rectangulo (35 vertices) y el ojo es SOLO el iris: el blanco en forma de almendra
    lo da el hueco de la cara en XV2. Aqui se hornea: almendra blanca con el iris dentro, y el resto del rectangulo del color
    de la piel de alrededor (mediana de la textura de la cara bajo el ojo), para que no se vea el bloque blanco."""
    base = ojo_tex(img, m, dye, zoom=IRIS_ZOOM, tinte=IRIS_CAC, zoomx=IRIS_ZOOM * razon)
    h_, w_ = base.shape[:2]
    uv = sm.uv
    _uc, _vc = (uv[:, 0].min() + uv[:, 0].max()) / 2, (uv[:, 1].min() + uv[:, 1].max()) / 2
    base = np.roll(base, (int(round(((_vc % 1.0) - 0.5) * h_)), int(round(((_uc % 1.0) - 0.5) * w_))), axis=(0, 1))     # iris al centro de la almendra
    u0, u1, v0, v1 = uv[:, 0].min(), uv[:, 0].max(), uv[:, 1].min(), uv[:, 1].max()
    a, b = (u1 - u0) / 2 * EYE_A, (v1 - v0) / 2 * EYE_B           # la almendra cubre la ventana del ojo; el iris cae donde lo pone la textura
    uc, vc = (u0 + u1) / 2, (v0 + v1) / 2
    ys, xs = (np.mgrid[0:h_, 0:w_] + 0.5) / np.array([h_, w_])[:, None, None]
    x, y = (((xs - uc + 0.5) % 1.0) - 0.5) / a, (((ys - vc + 0.5) % 1.0) - 0.5) / b
    lim = np.clip(1 - np.abs(x) ** 1.8, 0, 1) ** 0.8
    d = (np.abs(y) - lim) * b * 0.5 * min(h_, w_)             # distancia aprox. al borde, en texeles
    cob = np.clip(0.5 - d, 0, 1)[..., None]
    piel = np.array(ref, np.float32)[None, None, :]
    out = base.copy()
    cuerpo = base[..., :3] * cob + piel * (1 - cob)
    # brillo blanco del iris (arriba a la izquierda del centro 0.5,0.5 del iris) y parpado superior grueso y negro
    hl = np.clip(1.5 - np.hypot(((xs - uc + 0.5) % 1.0) - 0.5 + 0.045, ((ys - vc + 0.5) % 1.0) - 0.5 + 0.05) / 0.028, 0, 1)[..., None]
    cuerpo = cuerpo * (1 - hl) + 255.0 * hl
    lid = (np.clip(LID_GROSOR - np.abs(d + 0.4), 0, 1) * (y < 0.3))[..., None]          # parpado superior: borde del ojo grueso y negro
    out[..., :3] = np.clip(cuerpo * (1 - lid) + np.array([12.0, 9.0, 9.0]) * lid + 0.5, 0, 255)
    out[..., 3] = 255
    return out


def color_piel_ojo(face_sm, tx, ojo_sm):
    """Mediana del color de la textura de la cara (ya horneada) bajo la caja del ojo (+30 %)."""
    lo, hi = ojo_sm.pos[:, :2].min(0), ojo_sm.pos[:, :2].max(0)
    pad = (hi - lo) * 0.3
    sel = np.all((face_sm.pos[:, :2] > lo - pad) & (face_sm.pos[:, :2] < hi + pad), axis=1)
    uv = (face_sm.uv[sel] % 1.0) if sel.any() else np.array([[0.5, 0.3]])
    H, W = tx.shape[:2]
    px = tx[(uv[:, 1] * H).astype(int) % H, (uv[:, 0] * W).astype(int) % W, :3]
    return np.median(px, axis=0)


LUZ_BAKE = np.array([-0.4, 0.6, 0.7]) / np.linalg.norm([-0.4, 0.6, 0.7])


def sombra_uv(sm, nrm, tam, lo=0.78):
    """Mapa (tam, tam) de factor de sombra suave en espacio UV: lambert por vertice (normales ORIGINALES de XV2) interpolado
    en cada triangulo, remapeado a [lo, 1]. La rampa de B3 solo tiene 2 bandas; el degradado suave de XV2 (cara, hombreras)
    se hornea en la textura. Fuera de los triangulos = 1."""
    lam = np.clip(nrm @ LUZ_BAKE, 0, 1) ** 0.8
    fac = lo + (1 - lo) * lam
    out = np.ones((tam, tam), np.float32)
    done = np.zeros((tam, tam), bool)
    uv = (sm.uv % 1.0) * tam
    for tri in sm.tris:
        P = uv[tri]
        x0, x1 = int(np.floor(P[:, 0].min())), int(np.ceil(P[:, 0].max()))
        y0, y1 = int(np.floor(P[:, 1].min())), int(np.ceil(P[:, 1].max()))
        if x1 - x0 > tam // 2 or y1 - y0 > tam // 2:
            continue                                   # triangulo que cruza el borde de la textura
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (P[1, 1] - P[2, 1]) * (P[0, 0] - P[2, 0]) + (P[2, 0] - P[1, 0]) * (P[0, 1] - P[2, 1])
        if abs(d) < 1e-9:
            continue
        w0 = ((P[1, 1] - P[2, 1]) * (xs - P[2, 0]) + (P[2, 0] - P[1, 0]) * (ys - P[2, 1])) / d
        w1 = ((P[2, 1] - P[0, 1]) * (xs - P[2, 0]) + (P[0, 0] - P[2, 0]) * (ys - P[2, 1])) / d
        w2 = 1 - w0 - w1
        m = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)
        val = w0 * fac[tri[0]] + w1 * fac[tri[1]] + w2 * fac[tri[2]]
        xi, yi = xs[m].astype(int) % tam, ys[m].astype(int) % tam
        out[yi, xi] = val[m]
        done[yi, xi] = True
    for _ in range(3):                                  # rellena huecos de 1-2 texeles junto a los bordes
        sin = ~done
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            v = np.roll(np.roll(out, dy, 0), dx, 1)
            dn = np.roll(np.roll(done, dy, 0), dx, 1)
            sel = sin & dn
            out[sel] = v[sel]
            done |= sel
    return out


def textura(bcs, bset, tipo, d, sm, piel=None):
    """RGBA a tamano completo de la submalla (lo que va al AZT antes de reducir)."""
    img = d["emb"][sm.texdefs[0]][1]
    m = next((x for x in d["emm"] if x["nombre"] == sm.name), None)
    if tipo == "FaceEye":
        return ojo_tex(img, m, d["dye"])
    return hornear(img, color_base(bcs, bset, tipo, sm.name, m, d["dye"], piel))


def pow2(img, lado):
    """Reduce a potencias de 2 con el lado largo <= lado (DXT3 de B3: nada de tamanos 224x112)."""
    h, w = img.shape[:2]
    s = min(1.0, lado / max(w, h))
    nw, nh = [max(4, 1 << int(round(np.log2(max(4, v * s))))) for v in (w, h)]
    return img if (nw, nh) == (w, h) else S.resize_rgba(img, nw, nh)


PIEL = ("skin", "mouth", "tongue", "face")


def _ci(c, ruta):
    """Ruta real del CPK ignorando mayusculas (XV2 mezcla Face_ear / Face_Ear), o None."""
    if not hasattr(c, "_bajas"):
        c._bajas = {n.lower(): n for n in c.toc}
    return c._bajas.get(ruta.lower())


def cargar(c, clave, forma, bcs_set=None):
    """Lista (parte -> (submallas, imagenes, emm)) del personaje; devuelve dict con esk, partes."""
    r = "data/chara/%s/%s_%s.esk" % (clave, clave, forma)
    esk = xm.Esk(c.get(r if r in c.toc else "data/chara/%s/%s_000.esk" % (clave, clave)))   # 050 usa el esqueleto 000
    bcs = L.leer_bcs(c.get("data/chara/%s/%s.bcs" % (clave, clave)))
    if bcs_set is None:      # partset cuyo modelo es la forma pedida
        bcs_set = next((i for i, p in enumerate(bcs["partsets"]) if p and p.get("Bust") and p["Bust"]["modelo"] == int(forma)),
                       next((i for i, p in enumerate(bcs["partsets"]) if p and p.get("Bust") and p["Bust"]["modelo"]), 0))
    partes = {}
    for tipo, suf in (("Bust", "Bust"), ("Pants", "Pants"), ("Boots", "Boots"), ("Rist", "Rist"), ("FaceBase", "Face_base"),
                      ("FaceEye", "Face_eye"), ("Hair", "Hair"), ("FaceEar", "Face_Ear"), ("FaceForehead", "Face_forehead"),
                      ("FaceNose", "Face_nose")):
        base = "data/chara/%s/%s_%s_%s" % (clave, clave, forma, suf)
        if not _ci(c, base + ".emd"):      # JRN 050 no trae cara: las caras son las de la 000 (medido)
            base = "data/chara/%s/%s_000_%s" % (clave, clave, suf)
        if not _ci(c, base + ".emd"):
            continue
        g = lambda ext: _ci(c, base + ext)      # noqa: E731
        partes[tipo] = {"subs": xm.leer_emd(c.get(g(".emd"))), "emb": xm.leer_emb(c.get(g(".emb"))),
                        "emm": L.leer_emm(c.get(g(".emm"))) if g(".emm") else [],
                        "dye": L.dye_filas(c.get(g(".dyt.emb"))) if g(".dyt.emb") else []}
    # piezas fisicas (SCD): faldon, banda y delantal de Zamasu... son un EMD aparte con su esqueleto; en reposo cuelgan en bind
    ps = bcs["partsets"][bcs_set] if bcs_set < len(bcs["partsets"]) else None
    for tipo, v in (ps or {}).items():
        for fz in v["fisicas"]:
            nom = fz["emd"]
            base = "data/chara/%s/%s" % (clave, nom)
            if base + ".emd" not in c.toc:
                continue
            alt = "data/chara/%s/%s" % (clave, nom.replace("_%s_" % forma, "_000_", 1))     # 050 sin emb propio: el de la 000
            dueno = partes.get(tipo, {})
            emb_p = base + ".emb" if base + ".emb" in c.toc else alt + ".emb"
            emm_p = base + ".emm" if base + ".emm" in c.toc else alt + ".emm"
            partes["Fis_" + nom] = {"subs": [s for s in xm.leer_emd(c.get(base + ".emd"))],
                                    "emb": xm.leer_emb(c.get(emb_p)) if emb_p in c.toc else [],
                                    "emm": L.leer_emm(c.get(emm_p)) if emm_p in c.toc else [], "dye": dueno.get("dye", []),
                                    "cols_tipo": tipo}
    for tipo, d in partes.items():            # marcadores de 3 vertices de la pieza duena (apron, skirt_*): fuera
        if tipo == "FaceForehead" and any(k.startswith("Fis_") for k in partes):
            d["subs"] = [s for s in d["subs"] if len(s.pos) > 10]
    return esk, bcs, partes, bcs_set


def frontera(sm):
    """Fraccion de aristas de borde (soldando vertices por posicion): ~0 = malla cerrada, alto = lamina de una cara."""
    key = np.round(sm.pos, 4)
    _, idx = np.unique(key, axis=0, return_inverse=True)
    T = idx.ravel()[sm.tris]
    E = np.sort(np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]]), axis=1)
    _, cnt = np.unique(E, axis=0, return_counts=True)
    return float((cnt == 1).mean())


def doblar(sm, eps=0.03):
    """Doble cara: copia de vertices con normal invertida y triangulos al reves (hombreras y telas de XV2 son laminas de una
    cara; B3 las dibuja solo por un lado y desaparecen vistas por detras)."""
    n = len(sm.pos)
    sm.pos = np.concatenate([sm.pos, sm.pos - sm.nrm * eps])        # la copia interior se separa 'eps' hacia su cara: sin z-fighting
    sm.nrm = np.concatenate([sm.nrm, -sm.nrm])
    sm.uv = np.concatenate([sm.uv, sm.uv])
    sm.bones = np.concatenate([sm.bones, sm.bones])
    sm.weights = np.concatenate([sm.weights, sm.weights])
    sm.tris = np.concatenate([sm.tris, sm.tris[:, [0, 2, 1]] + n])


def soldar_normales(sm, max_deg=75.0):
    """Vertices en la misma posicion (costuras de UV / aristas duras) con normales distintas: el contorno toon de B3 (casco
    invertido a lo largo de la normal) se abre en esas costuras y deja motas negras sueltas en el borde. Se promedian las
    normales de los vertices coincidentes cuando difieren menos de `max_deg` (las aristas realmente duras se conservan)."""
    key = np.round(sm.pos, 4)
    _, idx = np.unique(key, axis=0, return_inverse=True)
    idx = idx.ravel()
    n = sm.nrm / (np.linalg.norm(sm.nrm, axis=1, keepdims=True) + 1e-9)
    acc = np.zeros((idx.max() + 1, 3))
    np.add.at(acc, idx, n)
    m = acc / (np.linalg.norm(acc, axis=1, keepdims=True) + 1e-9)
    ok = (n * m[idx]).sum(1) > np.cos(np.radians(max_deg))
    sm.nrm = np.where(ok[:, None], m[idx], n)


def suavizar(sm, veces=2):
    """Suaviza las normales entre vecinos (1 anillo, vertices soldados por posicion): las arrugas de la tela daban manchas de
    brillo de borde (rim) en B3; XV2 se ve mate."""
    key = np.round(sm.pos, 4)
    _, idx = np.unique(key, axis=0, return_inverse=True)
    idx = idx.ravel()
    nv = idx.max() + 1
    T = idx[sm.tris]
    n = sm.nrm.copy()
    for _ in range(veces):
        acc = np.zeros((nv, 3))
        np.add.at(acc, idx, n)
        nb = np.zeros((nv, 3))
        for a, b in ((0, 1), (1, 2), (2, 0)):
            np.add.at(nb, T[:, a], acc[T[:, b]])
            np.add.at(nb, T[:, b], acc[T[:, a]])
        m = acc + 0.5 * nb
        m /= np.linalg.norm(m, axis=1, keepdims=True) + 1e-9
        n = m[idx]
    sm.nrm = n


def repartir_armorparts(sm, f=0.3):
    """XV2: las hombreras exteriores pesan ~83 % en ArmorParts (hueso pegado a la clavicula que apenas gira con el brazo) y ~13 % en
    Shoulder. B3 no tiene ese hueso: se reparte como (1-f) clavicula (chn) + f giro del hombro (armrot), asi la hombrera sigue
    sobresaliendo casi horizontal y cae solo un poco con el brazo."""
    nombres = list(sm.bone_names)
    for lado, s in (("R", "r"), ("L", "l")):
        raw = "b_%s_ArmorParts" % lado
        if raw not in nombres:
            continue
        for n in (s + "chn", s + "armrot"):
            if n not in nombres:
                nombres.append(n)
        ia, ic, ir = nombres.index(raw), nombres.index(s + "chn"), nombres.index(s + "armrot")
        for k in range(len(sm.pos)):
            nuevo = {}
            for bi, w in zip(sm.bones[k], sm.weights[k]):
                if w <= 0:
                    continue
                if bi == ia:
                    nuevo[ic] = nuevo.get(ic, 0) + w * (1 - f)
                    nuevo[ir] = nuevo.get(ir, 0) + w * f
                else:
                    nuevo[bi] = nuevo.get(bi, 0) + w
            top = sorted(nuevo.items(), key=lambda x: -x[1])[:4]
            tot = sum(w for _, w in top) or 1.0
            sm.bones[k] = [b for b, _ in top] + [0] * (4 - len(top))
            sm.weights[k] = [w / tot for _, w in top] + [0.0] * (4 - len(top))
    sm.bone_names = nombres


def pesos_muslo(sm, es, maxw=0.2):
    """Telas colgadas de la cadera (faldon, delantal): los vertices por debajo de la cadera siguen parcialmente al muslo de su
    lado (par muslo + cadera, que es lo que admite B3) para no atravesar las piernas al levantarlas."""
    yl = es.world[es.index["rleg1"]][1, 3]
    yk = es.world[es.index["rleg2"]][1, 3]
    names = list(sm.bone_names)
    for nm_ in ("waist", "rleg1", "lleg1"):
        if nm_ not in names:
            names.append(nm_)
    iw, ir, il = names.index("waist"), names.index("rleg1"), names.index("lleg1")
    u = np.clip((yl - sm.pos[:, 1]) / max(yl - yk, 1e-6) * 1.2 - 0.2, 0, 1)        # empieza algo por debajo de la cadera
    t = (u * u * (3 - 2 * u)) * np.clip(np.abs(sm.pos[:, 0]) / 0.8, 0, 1) * maxw      # suave: sin saltos que rasguen la tela
    for k in np.nonzero(t > 0.02)[0]:
        sm.bones[k] = [iw, ir if sm.pos[k, 0] < 0 else il, 0, 0]
        sm.weights[k] = [1 - t[k], t[k], 0, 0]
    sm.bone_names = names


def hsv_region(col, c):
    """C6: HSV de la ficha (h 0-360, s/v como objetivo() de vista_heroe) sobre el color base: tono y saturacion de la ficha,
    brillo del color base (relativo a 0.7) para no perder las diferencias entre zonas. (0,0,0) = sin cambio."""
    import colorsys
    if not c or tuple(c) == (0, 0, 0):
        return col
    h, s_, v = (c[0] % 360) / 360.0, float(np.clip(0.55 + c[1] / 222.0, 0, 1)), float(np.clip(0.6 + c[2] / 250.0, 0, 1))
    vb = float(np.clip(max(col) / 0.7, 0.35, 1.3))
    return np.array(colorsys.hsv_to_rgb(h, s_, min(1.0, v * vb)), np.float32)


def region_de(tipo, nm):
    """Region HSV de una submalla: piel | pelo | torso | piernas | botas | guantes."""
    l = nm.lower()
    if l.startswith(PIEL) or "skin" in l:
        return "piel"
    return {"Hair": "pelo", "Bust": "torso", "Pants": "piernas", "Boots": "botas", "Rist": "guantes"}.get(tipo)


HAIR_SOFT = 0.35
CEJA_DIL = 5
CARA_LO = 0.72      # C6: sombra suave horneada de la cara del CaC (E3 usa 0.78)
IRIS_CAC = (0.50, 0.30, 0.18)
EYE_A, EYE_B = 1.0, 0.98      # semiejes de la almendra respecto a la ventana UV del ojo
IRIS_ZOOM = 1.8       # K2: antes 2.35 (el iris llenaba la almendra y no se veia el blanco)
BROW_CORTE = 0.80      # K2: fraccion del largo de la ceja que se conserva
EYE_PIEL = 1.0         # K2: la piel bajo el ojo sale mas clara que la de alrededor (sombra horneada)
BROW_FINA = 0.6       # K2: grosor de la ceja del CaC respecto a XV2
LID_GROSOR = 3.2
CEJA_COL = (0.14, 0.10, 0.08)
HAIR_GAP = float(os.environ.get("C6_HAIR_GAP", "0.24"))


def separar_pelo(hair, piel, gap=None):
    """C6: el contorno toon de B3 (casco negro invertido) de las caras INTERIORES del pelo se clavaba en la frente y dibujaba
    trazos negros punteados bajo el flequillo (medido en juego: con el pelo subido 4 unidades la cara sale limpia). Se separa cada
    vertice de pelo de la piel de la cara hasta `gap` (unidades B3) empujandolo en la direccion piel -> pelo."""
    gap = HAIR_GAP if gap is None else gap
    if not gap or not hair or not piel:
        return 0
    P = np.concatenate([s.pos for s in piel])
    n = 0
    for sm in hair:
        d2 = ((sm.pos[:, None, :] - P[None, :, :]) ** 2).sum(-1)
        j = d2.argmin(1)
        d = np.sqrt(d2[np.arange(len(j)), j])
        dirv = sm.pos - P[j]
        dirv /= np.linalg.norm(dirv, axis=1, keepdims=True) + 1e-9
        push = np.clip(gap - d, 0, None)[:, None]
        sm.pos = sm.pos + dirv * push
        n += int((push > 0).sum())
    return n


def sombra_cara_cac(partes, tam=512):
    """C6: UN solo mapa de sombra suave para TODA la piel de la cara (base, frente, nariz, orejas), horneado a la vez: las piezas
    comparten el atlas de la cara y, horneadas por separado, dejaban costuras claras/oscuras (banda bajo los ojos). Normales =
    mitad originales, mitad radiales desde el centro de la cabeza; se toma el valor mas sombreado donde se solapan."""
    sk = [sm for t in ("FaceBase", "FaceForehead", "FaceNose", "FaceEar") for sm in (partes.get(t) or {"subs": []})["subs"]
          if sm.name.lower().startswith("skin")]
    if not sk:
        return None
    ctr = np.concatenate([sm.pos for sm in sk]).mean(0)
    out = np.ones((tam, tam), np.float32)
    for sm in sk:
        rad = sm.pos - ctr
        rad /= np.linalg.norm(rad, axis=1, keepdims=True) + 1e-9
        n = sm.nrm * 0.5 + rad * 0.5
        n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
        out = np.minimum(out, sombra_uv(sm, n, tam, lo=CARA_LO))
    return out


def decimar_cache(sm, objetivo):
    """K: S.decimate (0,2 s por pieza grande) con cache en disco: clave = geometria de la pieza + triangulos objetivo
    (redondeados a 50). Misma carpeta local del jugador que el Cpk rapido; si falla, decima normal."""
    import hashlib
    import pickle
    import tempfile
    try:
        h = hashlib.sha1(np.ascontiguousarray(sm.pos).tobytes() + np.ascontiguousarray(sm.tris).tobytes() + b"%d" % objetivo).hexdigest()[:24]
        d = os.path.join(os.environ.get("DBZ3_CAC_CACHE") or os.path.join(tempfile.gettempdir(), "dbz3_xv2_cache"), "decim")
        fp = os.path.join(d, h + ".pkl")
        try:
            with open(fp, "rb") as fh:
                return pickle.load(fh)
        except Exception:  # noqa: BLE001
            pass
        out = S.decimate(sm, objetivo)
        os.makedirs(d, exist_ok=True)
        with open(fp + ".tmp", "wb") as fh:
            pickle.dump(out, fh, protocol=4)
        os.replace(fp + ".tmp", fp)
        return out
    except Exception:  # noqa: BLE001
        return S.decimate(sm, objetivo)


def fuente_sintetica(c, clave, forma, tpl, bcs_set=None, piel=None, cargado=None, hsv=None, decim=1.0):
    esk, bcs, partes, bset = cargado or cargar(c, clave, forma, bcs_set)
    TW = tpl.world()
    j_foot = next(k for k, l in enumerate(tpl.labels) if S.bone_suffix(l) == "RFOOT1")
    y_foot = esk.world[esk.index["b_R_Foot"]][1] * S_XV
    dy = TW[j_foot][1, 3] - y_foot
    es = EskSint(esk, tpl, S_XV, dy)
    emb, body, face, hechas = [], [], [], {}
    manos = {"L": [], "R": []}
    cac = cargado is not None      # C6: personaje armado con piezas del CaC (los del roster siguen el camino de E3)
    cara_ref = None
    hair_sms, piel_cara = [], []
    cara_sombra = sombra_cara_cac(partes) if cac else None
    hair_col = None
    gris = len(emb)
    emb.append(("rampa", np.full((4, 4, 4), 128, np.uint8), b""))

    for tipo, d in partes.items():
        for k_sm, sm in enumerate(d["subs"]):
            if cac and decim < 1.0 and not tipo.startswith("Face") and len(sm.tris) > 500:      # C6: recorte automatico (piezas > 1 MB)
                sm = decimar_cache(sm, max(400, int(len(sm.tris) * decim) // 50 * 50))
                d["subs"][k_sm] = sm
            if "%s:%d" % (tipo, k_sm) in os.environ.get("C6_SKIP", "").split(","):    # diagnostico C6
                continue
            sm.pos = sm.pos * S_XV + np.array([0.0, dy, 0.0])
            sm.uv = sm.uv - np.floor(sm.uv.mean(0)) if len(sm.uv) else sm.uv
            if tipo == "Bust" and sm.name.startswith("jacket"):
                repartir_armorparts(sm, ARM_F)
            sm.bone_names = [renombrar(b) for b in sm.bone_names]
            # K: el giro del hombro ("?armrot", que repartir_armorparts anade) no existe en XV2: se agarra al hombro (arm1, mismo pivote)
            # y no a la cadera (antes: ~25 % de la hombrera de las chaquetas quedaba pegada a la pelvis al mover el brazo)
            sm.bone_names = [(b if b in es.index else {"rarmrot": "rarm1", "larmrot": "larm1"}.get(b, "b_C_Pelvis")) for b in sm.bone_names]    # huesos SCD de cloth -> cadera
            if cac and tipo == "FaceEye" and clave in ("HUM", "HUF", "NMC", "FRI", "MAM", "MAF") and len(sm.pos) < 80 and not os.environ.get("C6_FLAT"):
                sm = ojo_malla(sm)          # C6: ojo en forma de almendra (sin rectangulo)
                d["subs"][k_sm] = sm
            if cac and "eyebrow" in sm.name.lower() and len(sm.pos) > 8 and not os.environ.get("C6_BROW_FULL"):
                # K2: cejas del CaC ("HAIR_eyebrow") mas finas: cada ceja (mitad izquierda/derecha) se aplasta en y hacia su centro
                for lado in (sm.pos[:, 0] > np.median(sm.pos[:, 0]), sm.pos[:, 0] <= np.median(sm.pos[:, 0])):
                    if lado.any():
                        cy = sm.pos[lado, 1].mean()
                        sm.pos[lado, 1] = cy + (sm.pos[lado, 1] - cy) * BROW_FINA
                ax = np.abs(sm.pos[:, 0] - np.median(sm.pos[:, 0]) * 0)      # x respecto al centro de la cara (0)
                fuera = ax[sm.tris].mean(1) > BROW_CORTE * ax.max()          # extremo exterior: la cinta se curva hacia atras y se ve de canto (rayas)
                if fuera.any() and (~fuera).any():
                    sm.tris = sm.tris[~fuera]
            soldar_normales(sm)                       # antes de hornear sombras y mezclar normales
            es_cara = (tipo in ("FaceBase", "FaceForehead", "FaceNose", "FaceEar") if cac else tipo == "FaceBase") and (sm.name.lower().startswith("skin") or "face" in sm.name.lower())
            sombra = None if os.environ.get("C6_NOSOMBRA") else sm.nrm.copy() if (es_cara or (tipo == "Bust" and sm.name.startswith("jacket")) or tipo.startswith("Fis_")) else None   # degradado suave horneado
            if es_cara:
                # cara de XV2: sombreado suave y casi todo iluminado (normales de la cara hacia delante); la rampa de B3 parte la cara
                f = np.array([0.0, 0.0, 1.0])
                n = sm.nrm * (1.0 - FACE_SOFT) + f * FACE_SOFT
                sm.nrm = n / (np.linalg.norm(n, axis=1, keepdims=True) + 1e-9)
            nm = sm.name
            if tipo == "Bust" and nm.startswith("jacket"):
                # hombreras: laminas abiertas; el brillo HD de borde (rim) dibujaba rayas blancas a lo largo del canto (normales
                # perpendiculares a la vista); normales suavizadas entre vecinos (diagnostico en juego, 22/10)
                suavizar(sm, veces=4)
                # (probado y descartado: mezclar la normal de la hombrera hacia delante da cebra blanca/negra en juego)
            # (una prueba de doble cara para hombreras/telas dio z-fighting en juego: B3 ya dibuja las dos caras; no se duplica)
            if tipo == "Hair" and os.environ.get("C6_HAIR_DY"):      # diagnostico: pelo flotando
                sm.pos = sm.pos + np.array([0.0, float(os.environ["C6_HAIR_DY"]), 0.0])
            if cac and tipo == "Hair":      # C6: pelo toon con normales suavizadas y algo hacia delante (menos brillo HD de borde, mismas bandas de la rampa)
                suavizar(sm, veces=2)
                n_ = sm.nrm * (1 - HAIR_SOFT) + np.array([0.0, 0.0, 1.0]) * HAIR_SOFT
                sm.nrm = n_ / (np.linalg.norm(n_, axis=1, keepdims=True) + 1e-9)
            if tipo == "Hair" and HAIR_OFS:
                sm.pos = sm.pos + sm.nrm * HAIR_OFS
            if tipo == "Pants":
                suavizar(sm)
            if tipo == "Pants":                                  # mate: normales hacia delante a medias (menos brillo de borde)
                n = sm.nrm * (1 - PANT_SOFT) + np.array([0.0, 0.0, 1.0]) * PANT_SOFT
                sm.nrm = n / (np.linalg.norm(n, axis=1, keepdims=True) + 1e-9)
            if tipo.startswith("Fis_"):
                pesos_muslo(sm, es)
            if tipo == "Hair":
                hair_sms.append(sm)
            elif es_cara or (tipo.startswith("Face") and nm.lower().startswith("skin")):
                piel_cara.append(sm)
            texi = sm.texdefs[0] if sm.texdefs else 0
            img = d["emb"][texi][1] if texi < len(d["emb"]) else None
            if img is None:
                continue
            m = next((x for x in d["emm"] if x["nombre"] == nm), None)
            col = color_base(bcs, bset, tipo, nm, m, d["dye"], piel)
            if hsv and region_de(tipo, nm) in hsv:
                col = hsv_region(col, hsv[region_de(tipo, nm)])
            if os.environ.get("C6_FLAT"):      # diagnostico C6: TODAS las submallas planas, un color distinto cada una, sin lineas
                col = np.array([(1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 0), (1, 0, 1), (0, 1, 1), (1, .5, 0), (.5, 0, 1), (.5, 1, 0), (0, .5, 1)][len(hechas) % 10], np.float32)
            elif os.environ.get("C6_DIAG") and tipo.startswith("Face"):      # diagnostico C6: un color por submalla de la cara
                col = np.array([(1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 1, 0), (1, 0, 1), (0, 1, 1), (1, .5, 0), (.5, 0, 1)][len(hechas) % 8], np.float32)
            if cac and tipo == "Hair" and hair_col is None:
                hair_col = col
            if cac and "eyebrow" in nm.lower() and hair_col is not None:
                col = np.asarray(hair_col, np.float32) * 0.85      # K2: ceja del color del pelo (no una linea gris-verde)
            elif nm.lower().startswith("eyebrow"):
                col = np.array([0.16, 0.20, 0.17], np.float32)      # XV2: linea fina oscura de parpado/ceja, no el color del pelo
            ojo = tipo == "FaceEye" and not os.environ.get("C6_DIAG") and not os.environ.get("C6_FLAT")
            clave_t = (tipo, texi, np.round(col, 3).tobytes(), (nm, len(sm.pos)) if (sombra is not None or ojo) else "")
            if cac and es_cara and cara_sombra is not None:
                clave_t = ("FACESKIN", 0, np.round(col, 3).tobytes(), "")
            if clave_t not in hechas:       # mismas textura y colores = una sola entrada (el AZT tiene tope)
                plano = (tipo.startswith("Face") if cac else tipo in ("FaceBase", "FaceEye")) or (tipo == "Bust" and nm.startswith("jacket"))     # cara sin iluminar: el sombreado suave de XV2 va horneado en la textura
                if ojo:
                    if cara_ref is not None and clave in ("HUM", "HUF", "NMC", "FRI", "MAM", "MAF") and len(sm.pos) < 80 and len(sm.pos) > 0 and (sm.uv.max(0) - sm.uv.min(0)).max() < 0.9:
                        A_ = np.c_[sm.uv, np.ones(len(sm.uv))]
                        razon_ojo = abs(np.linalg.lstsq(A_, sm.pos[:, 1], rcond=None)[0][1]) / max(abs(np.linalg.lstsq(A_, sm.pos[:, 0], rcond=None)[0][0]), 1e-6)   # mundo por v / mundo por u
                        rgb_ = color_piel_ojo(cara_ref[0], cara_ref[1], sm) * EYE_PIEL         # C6: ojo rectangular de CaC -> almendra
                        e_ = pow2(ojo_almendra(img, m, d["dye"], sm, rgb_ * 0.9, razon_ojo), 128)     # *0.9: el /0.9 de abajo ya esta en la media de la cara
                        e_[..., :3] = np.clip(e_[..., :3].astype(np.float32) / 0.9, 0, 255).astype(np.uint8)   # material plano = x0.9 (como la cara)
                    else:
                        e_ = pow2(ojo_tex(img, m, d["dye"]), 128)
                        e_[..., :3] = np.clip(e_[..., :3].astype(np.float32) / 0.9, 0, 255).astype(np.uint8)   # material plano de B3 = x0.9
                    emb.append((nm, e_, b""))
                elif cac and nm.lower() == "hair_eyebrow" and cara_ref is not None:
                    # C6: la ceja es una tira cuya forma esta en el alfa del atlas de la cara; se recorta SOLO esa zona del atlas
                    # (resolucion real, sin adelgazar con el reescalado del atlas entero), se engrosa 1 px y se pinta sobre la piel
                    H_, W_ = img.shape[:2]
                    uvm = sm.uv % 1.0
                    u0, u1 = max(0.0, uvm[:, 0].min() - 0.01), min(1.0, uvm[:, 0].max() + 0.01)
                    v0, v1 = max(0.0, uvm[:, 1].min() - 0.01), min(1.0, uvm[:, 1].max() + 0.01)
                    r0, r1, c0, c1 = int(v0 * H_), int(np.ceil(v1 * H_)), int(u0 * W_), int(np.ceil(u1 * W_))
                    a_ = Image.fromarray(img[r0:r1, c0:c1, 3]).filter(ImageFilter.MaxFilter(CEJA_DIL))
                    a_ = np.asarray(a_).astype(np.float32) / 255.0
                    m_ = np.clip((a_ - 0.10) / 0.30, 0, 1)[..., None]
                    skin_ = np.median(cara_ref[1][..., :3].reshape(-1, 3), axis=0)
                    ceja = (np.asarray(hair_col, np.float32) * 0.5 if hair_col is not None else np.array(CEJA_COL, np.float32)) * 255.0
                    tx_ = np.empty(a_.shape + (4,), np.uint8)
                    tx_[..., :3] = np.clip(skin_ * (1 - m_) + ceja * m_ + 0.5, 0, 255)
                    tx_[..., 3] = 255
                    sm.uv = np.stack([(uvm[:, 0] - u0) / max(u1 - u0, 1e-6) * ((c1 - c0) / (W_ * (u1 - u0))),
                                      (uvm[:, 1] - v0) / max(v1 - v0, 1e-6) * ((r1 - r0) / (H_ * (v1 - v0)))], 1)
                    emb.append((nm, pow2(tx_, 256), b""))
                elif cac and es_cara and cara_sombra is not None:
                    tx_ = pow2(hornear(img, col, float(os.environ.get("C6_LINEAS_CARA", "0.5"))), 512)
                    tx_ = tx_.copy()
                    f_ = cara_sombra if cara_sombra.shape == tx_.shape[:2] else np.asarray(Image.fromarray(cara_sombra).resize((tx_.shape[1], tx_.shape[0]), Image.BILINEAR))
                    tx_[..., :3] = np.clip(tx_[..., :3].astype(np.float32) * f_[..., None] / 0.82 + 0.5, 0, 255).astype(np.uint8)
                    emb.append((nm, tx_, b""))
                    cara_ref = (sm, tx_)
                else:
                    lado = (512 if cac else 256) if (tipo == "FaceBase" and "skin" in nm.lower()) else (64 if len(sm.pos) < 400 else 128)   # tope del AZT (610 KB)
                    tx_ = pow2(hornear(img, col, 0.0 if os.environ.get("C6_FLAT") else (float(os.environ.get("C6_LINEAS_CARA", "0.55")) if tipo == "FaceBase" else 0.4)), lado)
                    if sombra is not None:
                        f_ = sombra_uv(sm, sombra, tx_.shape[0], lo=(CARA_LO if cac else 0.60) if tipo.startswith("Face") else (0.5 if (cac and tipo == "Hair") else (0.9 if tipo.startswith("Fis_") else 0.74)))   # contraste +25 % (la luz de B3 se come parte del horneado)
                        if f_.shape != tx_.shape[:2]:      # C6: textura no cuadrada (pow2 32x64): el mapa de sombra se estira
                            f_ = np.asarray(Image.fromarray(f_).resize((tx_.shape[1], tx_.shape[0]), Image.BILINEAR))
                        if cac and tipo == "Hair":      # pelo plano de 2 tonos marcados (sin rampa ni brillo HD de borde: sin normales)
                            f_ = np.where(f_ > 0.76, 1.0, 0.68).astype(np.float32)
                        tx_ = tx_.copy()
                        tx_[..., :3] = np.clip(tx_[..., :3].astype(np.float32) * f_[..., None] + 0.5, 0, 255).astype(np.uint8)
                    if plano:
                        tx_ = tx_.copy()
                        gan = 0.82 if (tipo == "FaceBase" and "skin" in nm.lower()) else 0.9     # 0.9 = material plano de B3; la piel un 10 % mas clara (medido: 160 frente a 186)
                        tx_[..., :3] = np.clip(tx_[..., :3].astype(np.float32) / gan, 0, 255).astype(np.uint8)
                    emb.append((nm, tx_, b""))
                    if tipo == "FaceBase" and nm.lower().startswith("skin"):
                        cara_ref = (sm, tx_)
                hechas[clave_t] = len(emb) - 1
            skin = nm.lower().startswith(PIEL) or "skin" in nm.lower()
            sm.texdefs = [hechas[clave_t]] if ((tipo.startswith("Face") if cac else tipo in ("FaceBase", "FaceEye")) or (tipo == "Bust" and nm.startswith("jacket"))) else [hechas[clave_t], gris]   # 1 textura = material plano (sin rampa ni brillo de borde)
            sm.texdefs_orig = None      # rampa gris: la de piel de B3 tiñe de ocre y XV2 tiene piel de otros colores
            if tipo.startswith("Face") if cac else tipo in ("FaceBase", "FaceEye"):
                face.append(sm)
            elif cac and tipo == "Rist" and MANOS:        # K: la mano (piel + guante) va aparte: pieza rigida a la mano + variantes posadas
                izq = sum(float(sm.weights[sm.bones == j].sum()) for j, b in enumerate(sm.bone_names) if "_L_" in b or b.startswith("L") or b.startswith("l"))
                der = sum(float(sm.weights[sm.bones == j].sum()) for j, b in enumerate(sm.bone_names) if "_R_" in b or b.startswith("R") or b.startswith("r"))
                manos["L" if izq >= der else "R"].append(sm)
            else:
                body.append(sm)
    if cac:
        separar_pelo(hair_sms, piel_cara)
    src = {"name": "%s_%s" % (clave, forma), "esk": es, "emb": emb,
           "parts": {"xv2_body": body, "L00_face": face}}
    for lado in "LR":
        if manos[lado]:
            src["parts"]["%s_hand" % lado] = manos[lado]
    return src


# K: DESACTIVADO por defecto (K_MANOS=1 lo activa): con las manos como piezas aparte el bin no baja de ~1043 KB (tope 1024 KB):
# cada variante de mano son ~800 triangulos (las costuras de UV impiden recortar mas) y la cara (2500) y el pelo (2400) no se recortan.
MANOS = os.environ.get("K_MANOS", "0") == "1"      # K: manos del heroe como piezas rigidas con variantes posadas (puno, abierta...)
MANOS_TRIS = 110
MANOS_SETS = {"L": (1, 2), "R": (1, 2)}
ACCESORIOS = ("SOWD", "SOWDCASE")      # K: huesos rigidos del donante que el heroe debe llevar (espada y vaina de Trunks futuro)


def injertar_accesorios(src, plantilla):
    """K: el donante (plantilla = su bin nativo) trae accesorios como geometria rigida de huesos propios (espada y vaina de
    Trunks del futuro: huesos SOWD/SOWDCASE, sin pistas de animacion: siguen al pecho). Se copian TAL CUAL (posiciones en el
    marco del hueso, uv y textura del donante, sin arte nuevo) como piezas 'rigidlocal_<hueso>' que sdbh_model.convert
    pega al mismo hueso de la plantilla, con la textura y la rampa NATIVAS del donante (copiadas de la plantilla: ni arte nuevo ni
    reconversion de color; la AZT apenas crece). Tambien las variantes de mano con hoja (L22): solo la geometria extra."""
    sys.path.insert(0, os.path.join(HERE, ".."))
    import model_render as mr  # noqa: PLC0415
    m = mr.Model(plantilla)
    ids = {}
    for b, lab in enumerate(m.labels):
        suf = lab.lstrip("X").split("_", 1)[-1]
        if suf in ACCESORIOS:
            ids[b] = suf
    n = 0

    def pieza(mm, tt, mi, nombre, local):
        vi, inv = np.unique(tt, return_inverse=True)
        sm = type("Sub", (), {})()
        sm.name, sm.flags, sm.local = nombre, 0x6, local
        sm.pos, sm.nrm, sm.uv = mm.pos[vi], mm.nrm[vi], mm.uv[vi]
        sm.tris = inv.reshape(-1, 3)
        sm.texdefs = [0, 0]
        sm.tpl_tex, sm.tpl_ramp = mm.mats[mi]["tex"], mm.mats[mi]["shader"]      # material del DONANTE: textura y rampa nativas
        sm.bone_names, sm.bones, sm.weights = [], np.zeros((len(vi), 4), int), np.c_[np.ones(len(vi)), np.zeros((len(vi), 3))]
        return sm
    for b, suf in ids.items():
        sel = (m.vbone[m.tris] == b).all(1)
        for mi in np.unique(m.tri_mat[sel]):
            tt = m.tris[sel & (m.tri_mat == mi)]
            src["parts"].setdefault("rigidlocal_%s" % suf, []).append(pieza(m, tt, mi, "acc_%s" % suf, False))
            n += len(tt)
    T0 = {}
    for b, lab in enumerate(m.labels):          # texturas de la mano base del donante: lo que no sea eso es accesorio (hoja)
        mt = re.search(r"_L00_([LR])HAND$", lab)
        if mt:
            T0[mt.group(1)] = {m.mats[x]["tex"] for x in m.tri_mat[(m.vbone[m.tris] == b).all(1)]}
    for lab, k in m.variants().items():
        mt = re.search(r"_L(\d\d)_([LR])HAND$", lab)
        if not mt or int(mt.group(1)) == 0 or mt.group(2) not in T0:
            continue
        mm = mr.Model(m.data, awg=k)
        sel = np.array([mm.mats[x]["tex"] not in T0[mt.group(2)] for x in mm.tri_mat], bool)
        for mi in np.unique(mm.tri_mat[sel]):
            tt = mm.tris[sel & (mm.tri_mat == mi)]
            src["parts"].setdefault("rigidvar_%d_%s" % (int(mt.group(1)), mt.group(2)), []).append(pieza(mm, tt, mi, "acc_hoja", True))
            n += len(tt)
            print("K: variante de mano %s: %d triangulos de accesorio" % (lab, len(tt)))
    print("K: accesorios del donante injertados: %s (%d triangulos)" % (sorted(set(ids.values())), n))
    return n


def convertir(clave, forma, plantilla, salida, bcs_set=None, piel=None, cargado=None, hsv=None, decim=1.0, **kw):
    global S
    sys.path.insert(0, os.path.join(HERE, "..", "..", "awo_tools"))
    import sdbh_model  # noqa: PLC0415
    S = sdbh_model
    c = L.cpk_xv2()
    tpl = S.Template(plantilla)
    src = fuente_sintetica(c, clave, forma, tpl, bcs_set, piel, cargado, hsv, decim)
    injertar_accesorios(src, plantilla)
    if MANOS:
        S.HAND_SETS = MANOS_SETS      # K: solo las poses que mas se ven (puno, abierta, una de la derecha): el presupuesto del bin no da para las 14
    S.load_source = lambda folder: src
    S.drop_broken = lambda s: None
    return S.convert("xv2", plantilla, salida, hand_pose=MANOS, hand_tris=MANOS_TRIS, cap=256, auto_cap=False, **kw)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd")
    a = sub.add_parser("convertir")
    a.add_argument("clave")
    a.add_argument("forma")
    a.add_argument("plantilla")
    a.add_argument("salida")
    a.add_argument("--bcs-set", type=int)
    a.add_argument("--piel", help="R,G,B 0-1 del color de piel")
    a = sub.add_parser("info")
    a.add_argument("clave")
    a.add_argument("forma")
    x = ap.parse_args(argv)
    if x.cmd == "convertir":
        convertir(x.clave, x.forma, x.plantilla, x.salida, x.bcs_set, [float(v) for v in x.piel.split(",")] if x.piel else None)
    elif x.cmd == "info":
        esk, bcs, partes, bset = cargar(L.cpk_xv2(), x.clave, x.forma)
        for t, d in partes.items():
            print(t, [(s.name, len(s.pos), s.texdefs) for s in d["subs"]], "dye", len(d["dye"]))
    else:
        ap.print_help()


if __name__ == "__main__":
    main()



