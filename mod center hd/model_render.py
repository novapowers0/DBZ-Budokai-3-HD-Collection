#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""model_render.py - Render cel-shaded (estilo B3) de un bin #AMB HD, sin GPU.

Sirve para generar automaticamente el icono de la rueda y los retratos del select
de un personaje nuevo a partir de SU MODELO (fondo transparente, encuadre exacto),
en vez de recortar capturas.

Sombreado (deducido de las texturas del juego, calibrado con los iconos): cada material tiene una textura
base y una RAMPA toon de 64x64 (material +0x34). La rampa guarda el COMPLEMENTO
del tono de sombra: piel humana = rampa azul, Ginyu (morado) = rampa verde. El
color es  base - k * rampa[u]  (RESTA; con las bases casi blancas de los nativos coincide
con base*(1-k*rampa))  con u = iluminacion (0 sombra .. 63 luz). El
alfa de la textura base marca zonas sin sombrear (blanco de los ojos, lineas).
Materiales sin rampa (flags 0x1b4) = sin iluminar. Contorno negro por silueta y
discontinuidad de profundidad (como el casco invertido del juego).

  python model_render.py <bin> <out.png> [--yaw 0] [--pitch 0] [--size 512] [--head]
"""
import argparse
import io
import math
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
for p in (os.path.join(HERE, "lib"), os.path.join(HERE, "..", "awo_tools")):
    if os.path.isdir(p) and p not in sys.path:
        sys.path.append(p)
from awg_vertex_buffer import AwgVertexBuffer, WINDOW, qmat, mmul, be32, be_f  # noqa: E402
from b3_gateway import DrawTable, strip_tri  # noqa: E402
from skin_slots import SkinSlots  # noqa: E402

RAMP_K = 1.0           # factor de la rampa (calibrado con la piel de Goku/Ginyu = iconos oficiales)
# Sombreado toon: el juego RESTA la rampa (color = base - rampa); aqui se usaba el PRODUCTO
# (base * (1 - rampa)), exacto solo con las bases casi blancas de los nativos. Con bases de
# color (PSP/SDBH) se desvia. Ver docs/03_formatos/TOON_Y_BRILLO_HD.md ("Pasarla a resta").
RAMP_MODE = "resta"    # "resta" (fiel al juego) | "producto" (comportamiento antiguo)



def _dds(data, at, size):
    return np.array(Image.open(io.BytesIO(bytes(data[at:at + size]))).convert("RGBA"))


def _textures(data):
    z = data.find(b"#AZT")
    n, idx = struct.unpack(">II", data[z + 0x10:z + 0x18])
    offs = [struct.unpack(">I", data[z + idx + 4 * t:z + idx + 4 * t + 4])[0] for t in range(n)]
    dos = [struct.unpack(">I", data[z + o + 20:z + o + 24])[0] for o in offs]
    out = []
    for t in range(n):
        end = dos[t + 1] if t + 1 < n else len(data) - z
        try:
            out.append(_dds(data, z + dos[t], end - dos[t]).astype(np.float32) / 255.0)
        except Exception:      # noqa: BLE001
            out.append(np.ones((4, 4, 4), np.float32))
    return out


def _parse_awg(b, k):
    """AwgVertexBuffer del AWG k del #AWO (0 = cuerpo; 1.. = variantes de cara/manos con el
    mismo layout de ventanas)."""
    if k == 0:
        return AwgVertexBuffer._parse(b)
    awo = 0x40
    n_awg, tbl = be32(b, awo + 0x18), awo + be32(b, awo + 0x1C)
    a = awo + be32(b, tbl + 4 * k)
    g = lambda o: be32(b, a + o)       # noqa: E731
    ib_abs = a + g(0x30)
    n = g(0x2C) // WINDOW
    return AwgVertexBuffer(bytes(b), a, ib_abs - n * WINDOW, n, ib_abs, (a + g(0x38) - ib_abs) // 2,
                           g(0x34), g(0x2C), g(0x38), g(0x20), g(0x28), tbl, n_awg)


class Model:
    """Geometria del AWG0 (cuerpo + cara/manos por defecto) en bind-pose."""

    def __init__(self, data, awg=0):
        if isinstance(data, str):
            data = open(data, "rb").read()
        self.data = bytes(data)
        self.avb = _parse_awg(self.data, awg)
        self.table = DrawTable(self.avb)
        # material: +0x00 color difuso (RGB float BE: 1.0 normal, 0.8 sin iluminar, 0 = pelo
        # negro SIN textura, tex FFFFFFFF: masa del pelo de Goku/Goten); flag 0x80000000 =
        # translucido (lente del rastreador)
        a0 = self.avb.awg0
        mt = a0 + be32(self.data, a0 + 0x20)
        # vtype 0xF5 (bit 0x40; 0xB5 el normal) = reflejo: el VS del juego (2DD4268D7EE12280) no usa la uv de la
        # malla sino la normal en camara, uv = (0.5 + 0.5 nx, 0.5 - 0.5 ny) (sphere map / matcap): cupula y
        # hombreras de Freezer y Cooler, visera del Gran Saiyaman, Cell...
        self.mats = [dict(m_, diffuse=np.array([be_f(self.data, mt + k * 0x50 + 4 * c) for c in range(3)],
                                               np.float32), blend=bool(m_["flags"][0] & 0x80000000),
                          env=bool(m_["flags"][0] & 0x40))
                     for k, m_ in enumerate(self.table.materials)]
        self.labels = self.avb.bone_labels()
        self.local, self.parent = self._locals()
        self.tex = _textures(self.data)
        ss = SkinSlots(self.avb)
        V = self.avb.vertices()
        I = self.avb.indices()
        pos = np.array([v["pos"] for v in V], np.float64)
        nrm = np.array([v["nrm"] for v in V], np.float64)
        uv = np.array([v["uv"] for v in V], np.float64)
        tris, mats, vbone = [], [], np.zeros(len(V), int)
        skull = np.zeros(len(V), bool)      # vertices validos para medir el craneo
        for d in self.table.draws:
            s, c = d["B"]
            mat = self.table.materials[d["material"]]
            if not c or (mat["tex"] >= len(self.tex) and mat["tex"] != 0xFFFFFFFF):
                continue
            seq = I[s:s + c + 2] if d["prim"] == 5 else I[s:s + 3 * c]
            tt = [strip_tri(seq, i) for i in range(len(seq) - 2)] if d["prim"] == 5 else \
                [tuple(seq[i:i + 3]) for i in range(0, len(seq) - 2, 3)]
            for t in tt:
                if len(set(t)) != 3 or max(t) >= len(V):
                    continue
                for k in t:
                    # el slot de paleta manda tambien en listas (pelo de Jeice: lista del
                    # grupo BODY con slots HEAD/HAIR*); slot 0 = rigido al hueso del grupo
                    s_ = V[k]["bone"] & 0xFF
                    vbone[k] = ss.bone_of[s_] if s_ in ss.bone_of else \
                        (0 if d["prim"] == 5 else d["group_bone"])
                tris.append(t)
                mats.append(d["material"])
                # mallas sueltas (listas del grupo BODY: pelo de Jeice/Recoome) y la masa de
                # pelo sin textura NO cuentan para el craneo (encuadre calibrado sin ellas)
                if not ((d["prim"] == 4 and d["group_bone"] == 0) or mat["tex"] >= len(self.tex)):
                    skull[list(t)] = True
        self.pos, self.nrm, self.uv = pos, nrm, uv
        self.tris = np.array(tris, int)
        self.tri_mat = np.array(mats, int)
        self.vbone = vbone
        self.skull = skull

    def variants(self):
        """Variantes de cara/manos (AWG auxiliares): {etiqueta: indice de AWG}. La variante
        'XGOK_L01_S00_FACE' sustituye a la parte 'XGOK_L00_S00_FACE' del AWG0."""
        b = self.data
        awo = 0x40
        n_awg, tbl = be32(b, awo + 0x18), awo + be32(b, awo + 0x1C)
        out = {}
        for k in range(1, n_awg):
            a = awo + be32(b, tbl + 4 * k)
            out[bytes(b[a + 0x40:a + 0x60]).split(b"\0")[0].decode("latin1")] = k
        return out

    def use_variant(self, label):
        """Cambia la parte L00 equivalente del AWG0 por la variante `label` (expresion de
        cara o postura de mano). Devuelve False si no existe."""
        import re  # noqa: PLC0415
        k = self.variants().get(label)
        target = re.sub(r"_L\d\d_", "_L00_", label)
        if k is None or target not in self.labels:
            return False
        bone = self.labels.index(target)
        keep = ~(self.vbone[self.tris] == bone).all(1)
        self.tris, self.tri_mat = self.tris[keep], self.tri_mat[keep]
        return self.attach(Model(self.data, awg=k), bone, local=True)

    def attach(self, data, bone=None, local=False):
        """Anade un accesorio (bin #AMB de 1 hueso, p.ej. el rastreador nativo: entrada
        anterior al modelo) pegado al hueso `bone` (por defecto el *SCOUT* del cuerpo)."""
        acc = data if isinstance(data, Model) else Model(data)
        if bone is None:
            bone = next((i for i, l in enumerate(self.labels) if "SCOUT" in l), None)
        if bone is None:
            return False
        n, nt = len(self.pos), len(self.tex)
        W = acc.worlds() if not local else [np.eye(4)] * len(acc.local)   # local: ya en el marco de `bone`
        P = np.einsum("nij,nj->ni", np.array(W)[acc.vbone, :3, :3], acc.pos) + np.array(W)[acc.vbone, :3, 3]
        N = np.einsum("nij,nj->ni", np.array(W)[acc.vbone, :3, :3], acc.nrm)
        self.pos = np.concatenate([self.pos, P])
        self.nrm = np.concatenate([self.nrm, N])
        self.uv = np.concatenate([self.uv, acc.uv])
        self.vbone = np.concatenate([self.vbone, np.full(len(P), bone)])
        self.skull = np.concatenate([self.skull, np.zeros(len(P), bool)])
        self.tris = np.concatenate([self.tris, acc.tris + n])
        self.tri_mat = np.concatenate([self.tri_mat, acc.tri_mat + len(self.mats)])
        shift = lambda t: t + nt if t < len(acc.tex) else t       # noqa: E731
        self.mats += [dict(m_, tex=shift(m_["tex"]), shader=shift(m_["shader"])) for m_ in acc.mats]
        self.tex += acc.tex
        return True

    def _locals(self):
        b, a0 = self.avb.data, self.avb.awg0
        axes = a0 + be32(b, a0 + 0x14)
        loc, par = [], []
        for i in range(self.avb.bone_count()):
            o = axes + i * 80
            q = [be_f(b, o + 4 * k) for k in range(4)]
            p = [be_f(b, o + 16 + 4 * k) for k in range(3)]
            poff = be32(b, o + 0x40)
            par.append((a0 + poff - axes) // 80 if poff else -1)
            loc.append(np.array(qmat(*q, *p), np.float64))
        return loc, par

    def bone(self, suffix):
        for i, lab in enumerate(self.labels):
            s = lab.lstrip("X")
            if s.split("_", 1)[-1] == suffix:
                return i
        return None

    def worlds(self, pose=None):
        """pose = {indice_hueso: rotacion 3x3 en espacio modelo alrededor del pivote del
        hueso}; se hereda por los hijos."""
        out = [None] * len(self.local)
        for i in range(len(self.local)):
            p = self.parent[i]
            W = out[p] @ self.local[i] if 0 <= p < len(out) and p != i and out[p] is not None \
                else self.local[i].copy()
            if pose and i in pose:
                W = W.copy()
                W[:3, :3] = pose[i] @ W[:3, :3]
            out[i] = W
        return out

    def arms_down(self, angle=68.0):
        """Pose para retratos: baja los brazos de la T (rota *ARMROT hacia abajo)."""
        pose = {}
        W = self.worlds()
        P, _ = self.posed()
        for i, lab in enumerate(self.labels):
            if not lab.lstrip("X").split("_", 1)[-1] in ("RARMROT", "LARMROT"):
                continue
            kids = np.isin(self.vbone, list(self.descendants(i)))
            if not kids.any():
                continue
            d = P[kids].mean(0) - W[i][:3, 3]
            pose[i] = rot_z(angle if d[0] < 0 else -angle)
        return pose

    def mouth_closed(self, frac=0.75):
        """Pose que cierra la boca (en bind queda entreabierta y se ven los dientes; en los
        iconos/retratos oficiales esta cerrada): gira el hueso M_JAW hasta que el labio
        inferior (M_*MOUTH2, hijo de JAW) llega al superior (M_*MOUTH1), x `frac` (grosor)."""
        jaw = self.bone("M_JAW")
        lo = [self.bone(s) for s in ("M_LMOUTH2", "M_RMOUTH2")]
        up = [self.bone(s) for s in ("M_LMOUTH1", "M_RMOUTH1")]
        if jaw is None or None in lo or None in up:
            return {}
        W = self.worlds()
        piv = W[jaw][:3, 3]
        a = np.mean([W[k][:3, 3] for k in lo], 0) - piv
        b = np.mean([W[k][:3, 3] for k in up], 0) - piv
        # angulo (en el plano y-z) que lleva el labio inferior a la altura del superior
        ang = math.degrees(math.atan2(a[1], a[2]) - math.atan2(b[1], b[2])) * frac
        if not 0 < -ang < 12 and not 0 < ang < 12:
            return {}
        return {jaw: rot_x(ang)}

    def descendants(self, root):
        kids = {root}
        changed = True
        while changed:
            changed = False
            for i, p in enumerate(self.parent):
                if p in kids and i not in kids:
                    kids.add(i)
                    changed = True
        return kids

    def posed(self, pose=None):
        W = np.array(self.worlds(pose))
        R, T = W[self.vbone, :3, :3], W[self.vbone, :3, 3]
        P = np.einsum("nij,nj->ni", R, self.pos) + T
        N = np.einsum("nij,nj->ni", R, self.nrm)
        N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-12
        return P, N


# Accesorios nativos que van en un #AMB de 1 hueso en la entrada de data_cmn ANTERIOR al
# modelo: rastreadores (el cuerpo solo trae el auricular, hueso *SCOUT1; brazo y lente
# translucida van aparte) y el afro de Mr. Satan (STN_HEAIR, pegado a la cabeza).
# (Goku 263 y Vegeta 415 tambien tienen rastrador, pero su cuerpo no lleva el hueso: modo
# historia; no sale en sus iconos oficiales.)  prefijo -> (entrada, sufijo del hueso)
NATIVE_ACCESSORY = {"RAD": (359, "SCOUT"), "NAP": (344, "SCOUT"), "GNY": (257, "SCOUT"),
                    "RCM": (365, "SCOUT"), "BDK": (98, "SCOUT"), "STN": (376, "HEAD")}


def native_accessory(model):
    """(entrada de data_cmn, hueso) del accesorio de un cuerpo nativo, o None."""
    acc = NATIVE_ACCESSORY.get(model.labels[0].lstrip("X").split("_", 1)[0])
    if acc is None:
        return None
    entry, suffix = acc
    bone = next((i for i, lab in enumerate(model.labels) if suffix in lab.lstrip("X").split("_", 1)[-1]), None)
    return None if bone is None else (entry, bone)


def rot_y(deg):
    a = math.radians(deg)
    return np.array([[math.cos(a), 0, math.sin(a)], [0, 1, 0], [-math.sin(a), 0, math.cos(a)]])


def rot_x(deg):
    a = math.radians(deg)
    return np.array([[1, 0, 0], [0, math.cos(a), -math.sin(a)], [0, math.sin(a), math.cos(a)]])


def rot_z(deg):
    a = math.radians(deg)
    return np.array([[math.cos(a), -math.sin(a), 0], [math.sin(a), math.cos(a), 0], [0, 0, 1]])


def _sample(img, u, v):
    h, w = img.shape[:2]
    x = np.mod(u * w - 0.5, w)
    y = np.mod(v * h - 0.5, h)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = (x - x0)[:, None], (y - y0)[:, None]
    x1, y1 = (x0 + 1) % w, (y0 + 1) % h
    return (img[y0, x0] * (1 - fx) * (1 - fy) + img[y0, x1] * fx * (1 - fy)
            + img[y1, x0] * (1 - fx) * fy + img[y1, x1] * fx * fy)


def _shift(a, dy, dx, fill):
    """Desplaza sin dar la vuelta (np.roll llevaba el cuerpo del borde de abajo a la fila 0)."""
    out = np.full_like(a, fill)
    H, W = a.shape[:2]
    out[max(dy, 0):H + min(dy, 0), max(dx, 0):W + min(dx, 0)] =         a[max(-dy, 0):H + min(-dy, 0), max(-dx, 0):W + min(-dx, 0)]
    return out


def _dilate(m, r):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy > r * r + r:
                continue
            out |= _shift(m, dy, dx, False)
    return out


BLEND_ALPHA = 0.6      # opacidad de los materiales translucidos (lente del rastreador)


def _raster(tris, sx, sy, sz, W, H):
    """z-buffer de triangulos -> (zbuf, indice de triangulo, baricentricas b1, b2)."""
    zbuf = np.full((H, W), -np.inf)
    tid = np.full((H, W), -1, int)
    b1 = np.zeros((H, W))
    b2 = np.zeros((H, W))
    if not len(tris):
        return zbuf, tid, b1, b2
    X0, Y0, X1, Y1 = sx[tris].min(1), sy[tris].min(1), sx[tris].max(1), sy[tris].max(1)
    vis = (X1 >= 0) & (Y1 >= 0) & (X0 < W) & (Y0 < H)
    for t in np.nonzero(vis)[0]:
        a, b, c = tris[t]
        x0, x1 = max(int(X0[t]), 0), min(int(X1[t]) + 1, W - 1)
        y0, y1 = max(int(Y0[t]), 0), min(int(Y1[t]) + 1, H - 1)
        if x1 < x0 or y1 < y0:
            continue
        ax, ay, bx, by, cx, cy = sx[a], sy[a], sx[b], sy[b], sx[c], sy[c]
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9:
            continue
        xs = np.arange(x0, x1 + 1) + 0.5
        ys = np.arange(y0, y1 + 1)[:, None] + 0.5
        w1 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / den
        w2 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / den
        w3 = 1 - w1 - w2
        inside = (w1 >= -1e-6) & (w2 >= -1e-6) & (w3 >= -1e-6)
        if not inside.any():
            continue
        z = w1 * sz[a] + w2 * sz[b] + w3 * sz[c]
        zb = zbuf[y0:y1 + 1, x0:x1 + 1]
        win = inside & (z > zb)
        zb[win] = z[win]
        tid[y0:y1 + 1, x0:x1 + 1][win] = t
        b1[y0:y1 + 1, x0:x1 + 1][win] = w1[win]
        b2[y0:y1 + 1, x0:x1 + 1][win] = w2[win]
    return zbuf, tid, b1, b2


def _shade(model, tris, mats, T, w1, w2, Nc, light):
    """Color toon de los pixeles (triangulo T, baricentricas w1/w2)."""
    w1, w2 = w1[:, None], w2[:, None]
    w3 = 1 - w1 - w2
    ia, ib, ic = tris[T, 0], tris[T, 1], tris[T, 2]
    uv = model.uv[ia] * w1 + model.uv[ib] * w2 + model.uv[ic] * w3
    n = Nc[ia] * w1 + Nc[ib] * w2 + Nc[ic] * w3
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    L = np.asarray(light, float)
    L /= np.linalg.norm(L)
    lit = np.clip(0.5 + 0.5 * (n @ L), 0, 1)
    col = np.zeros((len(T), 3), np.float32)
    M = mats[T]
    for mi in np.unique(M):
        sel = M == mi
        mat = model.mats[mi]
        if mat["tex"] >= len(model.tex):      # sin textura: color plano del material
            col[sel] = mat["diffuse"]
            continue
        if mat["env"]:
            base = _sample(model.tex[mat["tex"]], 0.5 + 0.5 * n[sel, 0], 0.5 - 0.5 * n[sel, 1])
        else:
            base = _sample(model.tex[mat["tex"]], uv[sel, 0], uv[sel, 1])
        rgb = base[:, :3]
        if mat["shader"] != 0xFFFFFFFF and mat["shader"] < len(model.tex):
            ramp = model.tex[mat["shader"]]
            rh, rw = ramp.shape[:2]
            xi = np.clip((lit[sel] * (rw - 1)).astype(int), 0, rw - 1)
            # fila = alfa de la base (DXT3: 16 niveles -> bandas de 4 filas); alfa
            # maximo = sin sombrear (blanco de los ojos)
            band = np.clip(np.round(base[:, 3] * 15), 0, 15)
            yi = np.clip(((band + 0.5) * rh / 16).astype(int), 0, rh - 1)
            r3 = ramp[yi, xi, :3]
            if RAMP_MODE == "resta":
                shaded = np.clip(rgb - RAMP_K * r3, 0.0, 1.0)
            else:
                shaded = rgb * (1 - RAMP_K * r3)
            rgb = np.where(band[:, None] >= 15, rgb, shaded)
        elif mat["blend"]:                     # lente: textura x color del material
            rgb = rgb * mat["diffuse"]
        col[sel] = rgb
    return col


def render(model, size=(512, 512), view=np.eye(3), center=(0, 0, 0), scale=1.0, pose=None,
           light=(-0.45, 0.55, 0.70), outline=1.6, ss=3, hide=None, persp=None):
    """Proyeccion ortografica: p_cam = view . (p - center); x a la derecha, y arriba,
    z hacia la camara. `scale` = pixeles finales por unidad del modelo.
    Devuelve RGBA uint8 (H, W, 4)."""
    W, H = size[0] * ss, size[1] * ss
    P, N = model.posed(pose)
    C = (P - np.asarray(center)) @ np.asarray(view).T
    Nc = N @ np.asarray(view).T
    # perspectiva opcional: camara a `persp` unidades del centro (los oficiales se ven con
    # camara cercana: frente/pelo agrandados, menton pequeno); el centro conserva la escala
    k = persp / np.maximum(persp - C[:, 2], 0.1 * persp) if persp else 1.0
    sx = C[:, 0] * k * scale * ss + W / 2
    sy = H / 2 - C[:, 1] * k * scale * ss
    sz = C[:, 2]
    tris = model.tris
    mats = model.tri_mat
    if hide is not None:
        keep = ~np.isin(model.vbone[tris].min(axis=1), list(hide))
        tris, mats = tris[keep], mats[keep]
    blend = np.array([m_["blend"] for m_ in model.mats], bool)[mats] if len(mats) else np.zeros(0, bool)
    zbuf, tid, b1, b2 = _raster(tris[~blend], sx, sy, sz, W, H)
    opaque = np.nonzero(~blend)[0]
    tid = np.where(tid >= 0, opaque[np.maximum(tid, 0)], -1)
    cov = tid >= 0
    out = np.zeros((H, W, 4), np.float32)
    if cov.any():
        col = _shade(model, tris, mats, tid[cov], b1[cov], b2[cov], Nc, light)
        out[cov, :3] = col
        out[cov, 3] = 1
        # contorno: silueta + saltos de profundidad
        zz = np.where(cov, zbuf, -1e9)
        span = max(np.ptp(sz) if len(sz) else 1, 1e-6)
        edge = np.zeros_like(cov)
        for dy, dx in ((0, 1), (1, 0), (0, -1), (-1, 0)):
            nz = _shift(zz, dy, dx, np.inf)   # sin contorno en el borde de la imagen
            edge |= cov & ((zz - nz) > 0.02 * span)
        r = max(1, int(round(outline * ss / 2)))
        line = _dilate(edge, r)
        sil = _dilate(cov, r) & ~cov
        line = (line & cov) | sil
        out[line, :3] = 0
        out[line, 3] = 1
    if blend.any():     # lentes translucidas por encima (no tapadas por lo opaco)
        tz, ttid, tb1, tb2 = _raster(tris[blend], sx, sy, sz, W, H)
        trans = np.nonzero(blend)[0]
        vis = (ttid >= 0) & (tz > np.where(cov, zbuf, -np.inf))
        if vis.any():
            T = trans[ttid[vis]]
            c = _shade(model, tris, mats, T, tb1[vis], tb2[vis], Nc, light)
            a_ = BLEND_ALPHA
            base_a = out[vis, 3:4]
            new_a = base_a * (1 - a_) + a_
            out[vis, :3] = (out[vis, :3] * base_a * (1 - a_) + c * a_) / np.maximum(new_a, 1e-6)
            out[vis, 3] = new_a[:, 0]
    img = Image.fromarray((out * 255).astype(np.uint8))
    if ss > 1:   # reduccion por canal (premultiplicado para no ensuciar bordes)
        a = out[..., 3:4]
        pm = np.concatenate([out[..., :3] * a, a], axis=2)
        small = np.stack([np.array(Image.fromarray(pm[..., k]).resize(size, Image.BOX)) for k in range(4)], 2)
        al = small[..., 3:4]
        rgb = np.where(al > 1e-4, small[..., :3] / np.maximum(al, 1e-4), 0)
        img = np.concatenate([rgb, al], 2)
        return (np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)
    return np.array(img)


def core_bones(model, hb):
    """Craneo + cara: el hueso HEAD y sus descendientes faciales (M_*, FACE, JAW)."""
    return {i for i in model.descendants(hb)
            if i == hb or any(t in model.labels[i] for t in ("_M_", "FACE", "JAW"))}


def _top(P):
    """Lo mas alto del modelo (18 % de la altura): la cabeza cuando no hay nada pegado al hueso HEAD."""
    return P[:, 1] > P[:, 1].max() - 0.18 * np.ptp(P[:, 1])


def head_info(model, pose=None, core=False):
    """Caja de la cabeza (hueso HEAD y descendientes) en model-space. core=True se queda
    con el craneo y la cara (hueso HEAD + huesos faciales M_*/FACE/JAW): sin pelo,
    cuernos, crestas, pendientes ni rastreador."""
    hb = model.bone("HEAD")
    P, _ = model.posed(pose)
    if hb is None:
        sel = _top(P)
    else:
        bones = core_bones(model, hb) if core else model.descendants(hb)
        sel = np.isin(model.vbone, list(bones))
        if (sel & model.skull).any():   # sin mallas sueltas de pelo ni accesorios
            sel &= model.skull
    if not sel.any():   # nada pegado a la cabeza (algunos de Heroes): como sin hueso HEAD (antes: error, sin imagen 3D)
        sel = _top(P)
    hp = P[sel]
    return hp.min(0), hp.max(0), hb


# ---------------------------------------------------------------- icono de la rueda
# Medido sobre los 38 iconos oficiales (84x84 px HD = 56 logicos): circulo de radio
# 36,2 centrado en (42,42), aro azul marino (22,22,57) de ~2,2 px por dentro del borde,
# fondo azul (0,80..144,176..224) con trazos claros, cabeza casi de frente vista un poco
# desde arriba, boca cerrada (escala y camara: ver ICON_SCALE_* / ICON_PITCH).
ICON_PX = 84
ICON_R = 36.2
ICON_RING = 2.2
ICON_RING_RGB = (22, 22, 57)
ICON_CHIN_Y = 0.87         # menton (fraccion del alto del icono)
ICON_LIGHT = (-0.45, 0.55, 0.70)
# Camara por defecto, ajustada contra los iconos oficiales (2026-10-04, 16 nativos): un poco
# desde arriba (frente grande, menton pequeno) y casi de frente. Luz: fraccion de piel en
# sombra 33 % (oficial 36 %), sombra al mismo lado y altura que los oficiales.
ICON_PITCH = 10.0
ICON_YAW = 3.0
ICON_DY = -0.085
# Escala (px del icono por unidad del modelo): los oficiales usan una camara casi FIJA, no
# «el craneo llena el circulo». Ajuste sobre los 38 nativos (mejor zoom por personaje):
# escala = K * craneo^E (E ~ 0: casi constante), dispersion residual 12 %.
ICON_SCALE_K = 27.26
ICON_SCALE_E = -0.139
ICON_SKULL_RANGE = (0.67, 1.48)   # craneo / diametro en los oficiales (tope para ports raros)


def icon_background(px=ICON_PX, ss=4):
    """Fondo azul con rafaga de trazos claros (como el de los iconos del select)."""
    n = px * ss
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / ss
    cx = cy = px / 2
    r = np.hypot(x - cx, y - cy) / (px / 2)
    th = np.arctan2(y - cy, x - cx)
    base = np.array([0, 84, 178], float)
    deep = np.array([0, 128, 210], float)
    light = np.array([70, 170, 236], float)
    t = np.clip(r, 0, 1)[..., None]
    col = base * (1 - t) + deep * t
    streak = np.clip(np.cos(7 * th + 2.2 * r) * 1.6 - 0.75, 0, 1) * np.clip(r * 1.4 - 0.25, 0, 1)
    col = col * (1 - 0.6 * streak[..., None]) + light * 0.6 * streak[..., None]
    return np.concatenate([col, np.full((n, n, 1), 255.0)], 2)


def icon_mask(px=ICON_PX, ss=4):
    n = px * ss
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / ss
    r = np.hypot(x - px / 2, y - px / 2)
    alpha = np.clip(ICON_R - r + 0.5, 0, 1)
    ring = np.clip(r - (ICON_R - ICON_RING) + 0.5, 0, 1) * (alpha > 0)
    return alpha, ring


def _compose(layers_rgba, bg):
    out = bg.copy()
    for lay in layers_rgba:
        a = lay[..., 3:4] / 255.0
        out[..., :3] = out[..., :3] * (1 - a) + lay[..., :3] * a
    return out


def make_icon(model, px=ICON_PX, yaw=None, pitch=None, zoom=1.0, dx=0.0, dy=0.0, pose=None, art=None,
              light=None, outline=1.1, persp=None, roll=0.0):
    """Icono redondo de la rueda (RGBA px x px) a partir del modelo (o de `art`, una
    imagen RGBA del modder ya recortada a la cara, que se encaja igual)."""
    ss = 4
    n = px * ss
    if art is None:
        if pose is None:
            pose = model.mouth_closed()
        yaw = ICON_YAW if yaw is None else yaw
        pitch = ICON_PITCH if pitch is None else pitch
        dy = dy + ICON_DY
        lo, hi, _ = head_info(model, pose, core=True)
        h = max(hi[1] - lo[1], 1e-6)
        w = hi[0] - lo[0]
        if w < 1.5 * h:         # pelo en huesos aparte (craneo "corto"): manda la anchura
            h = max(h, 0.92 * w)  # (no con cuernos/orejas anchas: Ginyu, Freezer, Cell)
        base = ICON_SCALE_K * h ** ICON_SCALE_E        # px (icono de 84) por unidad
        lo_f, hi_f = ICON_SKULL_RANGE
        base = min(max(base, lo_f * 2 * ICON_R / h), hi_f * 2 * ICON_R / h)
        scale = base * zoom * px / ICON_PX
        # El giro en el plano corrige modelos cuyo rostro viene inclinado en su pose
        # de referencia. Es independiente del yaw horizontal.
        view = rot_z(roll) @ rot_x(pitch) @ rot_y(yaw)
        # centro del render: x = centro del craneo; y tal que el menton caiga en ICON_CHIN_Y
        cx = (lo[0] + hi[0]) / 2
        chin_px = ICON_CHIN_Y * px
        cy = lo[1] + (chin_px - px / 2) / scale
        ctr = np.array([cx, cy, (lo[2] + hi[2]) / 2])
        ctr = ctr - np.linalg.inv(view) @ np.array([dx * px, -dy * px, 0]) / scale
        img = render(model, (n, n), view, ctr, scale * ss, pose=pose, ss=2, outline=outline * ss,
                     light=ICON_LIGHT if light is None else light, persp=persp)
        char = img.astype(float)
    else:
        a = art.convert("RGBA")
        s = max(n / a.width, n / a.height) * zoom
        a = a.resize((max(1, int(a.width * s)), max(1, int(a.height * s))), Image.LANCZOS)
        canvas = Image.new("RGBA", (n, n), (0, 0, 0, 0))
        canvas.alpha_composite(a, (int((n - a.width) / 2 + dx * n), int((n - a.height) / 2 + dy * n)))
        char = np.array(canvas).astype(float)
    out = _compose([char], icon_background(px, ss))
    alpha, ring = icon_mask(px, ss)
    out[..., :3] = out[..., :3] * (1 - ring[..., None]) + np.array(ICON_RING_RGB) * ring[..., None]
    out[..., 3] = alpha * 255
    pm = np.concatenate([out[..., :3] * alpha[..., None], alpha[..., None]], 2)
    small = np.stack([np.array(Image.fromarray(pm[..., k].astype(np.float32)).resize((px, px), Image.BOX))
                      for k in range(4)], 2)
    al = small[..., 3:4]
    rgb = np.where(al > 1e-4, small[..., :3] / np.maximum(al, 1e-4), 0)
    return np.clip(np.concatenate([rgb, al * 255], 2) + 0.5, 0, 255).astype(np.uint8)


# ---------------------------------------------------------------- retratos P1/P2
# Medido sobre los 38 retratos oficiales (zona util 288x352 logicos x 1,3): cielo por
# bandas (P1 azul, P2 rojo), personaje en 3/4 mirando al centro de la pantalla, menton a
# ~0,61 del alto, cabeza centrada en x ~0,53 (P1; P2 es el espejo).
PORTRAIT_CHIN_Y = 0.61
PORTRAIT_HEAD_X = 0.53
# Camara y luz por defecto ajustadas contra los retratos oficiales (2026-10-04, 12 nativos):
# 3/4 a 35 grados, algo desde arriba, luz casi cenital (sombras suaves como los oficiales).
PORTRAIT_YAW = 35.0
PORTRAIT_PITCH = 14.0
# Escala: K * cabeza_efectiva^E por pixel de alto (ajuste sobre los 38 nativos; sustituye a
# la regla de «cabezones», dispersion residual 13 %).
PORTRAIT_SCALE_K = 164.25 / 458
PORTRAIT_SCALE_E = -0.561
PORTRAIT_DX = 0.005
PORTRAIT_DY = 0.035
PORTRAIT_LIGHT = (0.0, 0.996, 0.087)
# (posicion relativa en alto, color) del fondo
PORTRAIT_SKY = {
    0: [(0.0, (22, 110, 191)), (0.38, (22, 110, 191)), (0.47, (77, 169, 193)), (0.70, (77, 168, 193)),
        (0.78, (40, 76, 119)), (1.0, (40, 80, 130))],
    1: [(0.0, (143, 58, 41)), (0.38, (148, 68, 46)), (0.48, (179, 139, 103)), (0.56, (164, 116, 78)),
        (0.85, (162, 105, 77)), (1.0, (146, 97, 60))],
}
# Tinte del personaje (los retratos se guardan apagados y teñidos hacia el fondo):
# out = c * gain + offset, por canal (ajustado contra los retratos nativos).
PORTRAIT_TINT = {0: ((0.62, 0.62, 0.66), (14, 22, 36)), 1: ((0.60, 0.52, 0.48), (46, 22, 16))}


def portrait_background(w, h, variant):
    stops = PORTRAIT_SKY[variant]
    ys = np.linspace(0, 1, h)
    col = np.stack([np.interp(ys, [s[0] for s in stops], [s[1][k] for s in stops]) for k in range(3)], 1)
    return np.repeat(col[:, None, :], w, 1)


def make_portraits(model, size, yaw=None, pitch=None, zoom=1.0, dx=0.0, dy=0.0, art=None, tint=True,
                   light=None, outline=1.3, persp=None, roll=0.0):
    """Retratos del select [P1, P2] (RGBA alto x ancho = size[1] x size[0]). Desde el
    modelo (3/4, brazos bajados) o desde `art` (imagen del modder, sin fondo o con fondo
    propio), que se encaja igual. P2 renderiza la cámara desde el lado contrario; nunca se
    invierte el bitmap de P1."""
    w, h = size
    yaw = PORTRAIT_YAW if yaw is None else yaw
    if art is None:
        pitch = PORTRAIT_PITCH if pitch is None else pitch
        dx, dy = dx + PORTRAIT_DX, dy + PORTRAIT_DY
        pose = model.arms_down()
        pose.update(model.mouth_closed())
        P, _ = model.posed(pose)
        _, _, hb = head_info(model, pose)
        sel = np.isin(model.vbone, list(core_bones(model, hb))) if hb is not None else np.ones(len(P), bool)
        if (sel & model.skull).any():
            sel &= model.skull
        if not sel.any():
            sel = _top(P)
        chars = []
        for variant, yaw_v in enumerate((yaw, -yaw)):
            view = rot_z(roll) @ rot_x(pitch) @ rot_y(yaw_v)
            C = P[sel] @ view.T
            clo, chi = C.min(0), C.max(0)
            # Cabezas anchas (niños, cúpulas, cuernos): manda la anchura vista en 3/4.
            eff = max(chi[1] - clo[1], (chi[0] - clo[0]) / 1.1, 1e-6)
            scale = PORTRAIT_SCALE_K * h * eff ** PORTRAIT_SCALE_E * zoom
            head_x = PORTRAIT_HEAD_X if variant == 0 else 1.0 - PORTRAIT_HEAD_X
            cx_cam = (clo[0] + chi[0]) / 2 - (head_x - 0.5) * w / scale
            cy_cam = clo[1] + (PORTRAIT_CHIN_Y * h - h / 2) / scale
            cam = np.array([cx_cam - dx * w / scale, cy_cam + dy * h / scale, (clo[2] + chi[2]) / 2])
            ctr = view.T @ cam            # view es ortonormal
            chars.append(render(model, (w, h), view, ctr, scale, pose=pose, ss=3, outline=outline,
                                light=PORTRAIT_LIGHT if light is None else light, persp=persp).astype(float))
    else:
        a = art.convert("RGBA")
        s = max(w / a.width, h / a.height) * zoom
        a = a.resize((max(1, int(a.width * s)), max(1, int(a.height * s))), Image.LANCZOS)
        canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        canvas.alpha_composite(a, (int((w - a.width) / 2 + dx * w), int((h - a.height) / 2 + dy * h)))
        chars = [np.array(canvas).astype(float)] * 2
    out = []
    for variant in (0, 1):
        ch = chars[variant].copy()
        if tint:
            g, o = PORTRAIT_TINT[variant]
            ch[..., :3] = ch[..., :3] * np.array(g) + np.array(o)
        img = _compose([ch], np.concatenate([portrait_background(w, h, variant), np.full((h, w, 1), 255.0)], 2))
        img = np.clip(img + 0.5, 0, 255).astype(np.uint8)
        out.append(img)
    return out


# ---------------------------------------------------------------- cara de la barra de vida
# Medido sobre las caras oficiales (*_HUD.amt de data_cmn: #AZT con una textura por forma,
# DDS 256x128 A8R8G8B8, zona util 192x120 px HD = 128x80 logicos): render del modelo casi de
# frente y algo desde arriba con camara FIJA (~31 px por unidad del modelo), centro de la
# cabeza en x = 91 y menton en y = 85; personaje con alfa 205 y halo azul difuso alrededor.
HUD_SIZE = (192, 120)
HUD_CANVAS = (256, 128)
HUD_PX_PER_UNIT = 30.0
# El HUD oficial de PS2 ocupa 128x80 lógicos. La cara debe quedar en su eje
# horizontal (64 lógicos = 96 px HD en la zona útil de 192 px).
HUD_HEAD_X = 96
HUD_CHIN_Y = 85
HUD_PITCH = 10.0
HUD_YAW = 0.0
HUD_ALPHA = 205
HUD_GLOW_RGB = (48, 137, 192)
HUD_GLOW_SIGMA = 11.0
HUD_GLOW_GAIN = 1.9
# recorte de los hombros: elipse centrada en la cabeza (borde de las oficiales) y ultimas
# filas fundidas (alfa medido en la fila 116..119 de las oficiales)
HUD_ELLIPSE = (93.0, 40.0, 93.0, 120.0)        # cx, cy, rx, ry (px HD)
HUD_BOTTOM_FADE = (0.98, 0.88, 0.52, 0.13)


def _blur(a, sigma):
    r = int(3 * sigma)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    p = np.pad(a, r)
    p = np.apply_along_axis(lambda v: np.convolve(v, k, "same"), 0, p)
    p = np.apply_along_axis(lambda v: np.convolve(v, k, "same"), 1, p)
    return p[r:-r, r:-r]


def make_hud(model, yaw=None, pitch=None, zoom=1.0, dx=0.0, dy=0.0, art=None, pose=None, outline=1.4,
             center_ps2=True):
    """Cara de la barra de vida (RGBA 128 x 256, contenido en 120 x 192) desde el modelo o
    desde `art` (imagen del modder, mejor sin fondo), con el halo azul de las oficiales.
    `center_ps2=True` centra la cara en el eje de la zona útil; False conserva el x=91 histórico."""
    w, h = HUD_SIZE
    if art is None:
        if pose is None:
            pose = model.arms_down()
            pose.update(model.mouth_closed())
        view = rot_x(HUD_PITCH if pitch is None else pitch) @ rot_y(HUD_YAW if yaw is None else yaw)
        lo, hi, _ = head_info(model, pose, core=True)
        scale = HUD_PX_PER_UNIT * zoom
        head_x = HUD_HEAD_X if center_ps2 else 91  # 91 = encuadre Xbox 360 histórico
        cx = (lo[0] + hi[0]) / 2 - (head_x - w / 2) / scale
        cy = lo[1] + (HUD_CHIN_Y - h / 2) / scale
        cam = np.array([cx - dx * w / scale, cy + dy * h / scale, (lo[2] + hi[2]) / 2])
        char = render(model, (w, h), view, view.T @ cam, scale, pose=pose, ss=3, outline=outline,
                      light=ICON_LIGHT).astype(float)
    else:
        a = art.convert("RGBA")
        s = max(w / a.width, h / a.height) * zoom
        a = a.resize((max(1, int(a.width * s)), max(1, int(a.height * s))), Image.LANCZOS)
        canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        canvas.alpha_composite(a, (int((w - a.width) / 2 + dx * w), int((h - a.height) / 2 + dy * h)))
        char = np.array(canvas).astype(float)
    W, H = HUD_CANVAS
    a = np.zeros((H, W))
    a[:h, :w] = char[..., 3] / 255.0
    ex, ey, rx, ry = HUD_ELLIPSE
    yy, xx = np.mgrid[0:H, 0:W] + 0.5
    r = np.hypot((xx - ex) / rx, (yy - ey) / ry)
    a *= np.clip((1 - r) * rx + 0.5, 0, 1)
    # halo: silueta difuminada (el personaje sigue por debajo del borde: sin halo abajo)
    ext = a.copy()
    ext[h:, :w] = a[h - 1:h, :w]
    glow = np.clip(_blur(ext, HUD_GLOW_SIGMA) * HUD_GLOW_GAIN, 0, 1)
    glow[h:] = 0
    rgb = np.zeros((H, W, 3))
    rgb[:] = HUD_GLOW_RGB
    rgb[:h, :w] = rgb[:h, :w] * (1 - a[:h, :w, None]) + char[..., :3] * a[:h, :w, None]
    alpha = HUD_ALPHA * np.maximum(a, (1 - a) * glow)
    for k, f in enumerate(HUD_BOTTOM_FADE):
        alpha[h - len(HUD_BOTTOM_FADE) + k] *= f
    out = np.concatenate([rgb, alpha[..., None]], 2)
    out[alpha < 0.5] = 0
    return np.clip(out + 0.5, 0, 255).astype(np.uint8)


def main():
    global RAMP_MODE
    ap = argparse.ArgumentParser()
    ap.add_argument("bin")
    ap.add_argument("out")
    ap.add_argument("--yaw", type=float, default=0)
    ap.add_argument("--pitch", type=float, default=0)
    ap.add_argument("--roll", type=float, default=0, help="inclinacion de camara en grados")
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--head", action="store_true")
    ap.add_argument("--toon", choices=("resta", "producto"), default=RAMP_MODE,
                    help="sombreado de la rampa (resta = fiel al juego)")
    a = ap.parse_args()
    RAMP_MODE = a.toon
    m = Model(a.bin)
    view = rot_z(a.roll) @ rot_x(a.pitch) @ rot_y(a.yaw)
    P, _ = m.posed()
    if a.head:
        lo, hi, _ = head_info(m)
    else:
        lo, hi = P.min(0), P.max(0)
    ctr = (lo + hi) / 2
    sc = a.size * 0.9 / max(hi[1] - lo[1], hi[0] - lo[0], 1e-6)
    img = render(m, (a.size, a.size), view, ctr, sc)
    Image.fromarray(img).save(a.out)
    print(a.out, len(m.tris), "tris")


if __name__ == "__main__":
    sys.exit(main())
