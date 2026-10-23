#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""sdbh_model.py - Modelos de Super Dragon Ball Heroes World Mission (PC) -> bin HD de Budokai 3.

SDBH WM usa formatos de Xenoverse (little-endian, version 0x9300) y el rig de Dimps heredado de B3:
  ESK  esqueleto: [n, flag, off indices (padre, hijo, hermano, idx: 4 x i16), off nombres,
       off TRS local por hueso (pos xyzw, quat xyzw, escala xyzw)]. Metros, Z hacia atras.
  EMD  malla: modelo -> mallas -> submallas [AABB 0x30, flags de vertice, tam, n, off vertices,
       nombre, nTexDef, nGrupos, off texdefs (12 B: [_, textura, _, _, 2f]), off grupos
       (grupo: [n indices, n huesos, off indices u16, off nombres])]. Vertice (flags 0x207, 48 B):
       pos, normal, uv, 4 x u8 indice de hueso (en la lista del grupo), 3 pesos (el 4.o = resto).
  EMB  contenedor de DDS (DXT1): texturas base + rampas toon 64x64 que MULTIPLICAN.
Hallazgos (2026-10-06, Gohan del Futuro `bcghf`):
  * El esqueleto es el de Gohan adulto de B3 (char 4, fid 225) x 0,1 con Z espejada: posiciones
    locales x10 y q(x, y, z, w) -> (-x, -y, z, w). Mismos nombres por sufijo (f_jaw = M_JAW).
  * Las 7 caras (L00 L01 L04 L05 L06 L09 L18), dientes y boca (huesos f_*) son las de B3.
  * Manos: una malla por mano pielada a 15 huesos de dedos. B3 usa manos rigidas por postura
    (L00..L22, AWG auxiliares); aqui se hornean las posturas (tabla MANOS, ajustada a las
    variantes nativas de Gohan adulto con `ajustar-manos`).
  * Sombreado: B3 RESTA la rampa (color = base - rampa; base.a alta = sin sombra; ver
    shaders_dump PS 5F27AACEB38B1088). Las bases de SDBH ya traen el color (piel blanca con
    lineas, ropa de color), igual que los nativos HD; se usan las rampas NATIVAS de la plantilla
    (piel = rampa de piel de Gohan, ropa y pelo = rampa gris) con alfa 0 en las bases, alfa 255
    solo en los ojos (submalla *Eye_cons*) y en lo que no se ilumina (boca, dientes).
Salida: #AMB (AWO + AZT) DESCOMPRIMIDO con la estructura de la plantilla HD (mismo esqueleto,
mismos huesos de fisica de cinturon/pelo, boca M_*), AWG0 = cuerpo + cara L00 + dientes + manos
L00 y un AWG auxiliar por variante de cara/mano.

Uso:
  python sdbh_model.py convertir <carpeta bcXXXbNN> <plantilla HD.bin> <salida.bin> [opciones]
  python sdbh_model.py info <carpeta bcXXXbNN>
  python sdbh_model.py --selftest
"""
import argparse
import glob
import io
import math
import os
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

MIRROR = np.diag([1.0, 1.0, -1.0])     # SDBH (Z atras) -> B3 (Z delante)
SCALE = 10.0                            # metros -> unidades de B3


# =============================================================================
# Lectura (little-endian)
# =============================================================================
def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _cstr(b, o):
    return b[o:b.index(b"\0", o)].decode("latin1")


def read_emb(path):
    """[(nombre, RGBA uint8, DDS en bruto)] de un EMB; entradas vacias -> (nombre, None, b'')."""
    from PIL import Image
    b = open(path, "rb").read()
    n, off, names = _u32(b, 0xC), _u32(b, 0x18), _u32(b, 0x1C)
    out = []
    for i in range(n):
        o, size = struct.unpack_from("<II", b, off + 8 * i)
        d = b[off + 8 * i + o:off + 8 * i + o + size]
        nm = _cstr(b, _u32(b, names + 4 * i)) if names else "tex%d" % i
        img = np.array(Image.open(io.BytesIO(d)).convert("RGBA")) if d[:4] == b"DDS " else None
        out.append((nm, img, d))
    return out


def qmat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def mat_q(R):
    """Matriz de rotacion -> cuaternion (x, y, z, w) con w >= 0."""
    t = np.trace(R)
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        q = [(R[2, 1] - R[1, 2]) / s, (R[0, 2] - R[2, 0]) / s, (R[1, 0] - R[0, 1]) / s, 0.25 * s]
    else:
        i = int(np.argmax(np.diag(R)))
        j, k = (i + 1) % 3, (i + 2) % 3
        s = math.sqrt(1.0 + R[i, i] - R[j, j] - R[k, k]) * 2
        q = [0.0] * 4
        q[i] = 0.25 * s
        q[j] = (R[j, i] + R[i, j]) / s
        q[k] = (R[k, i] + R[i, k]) / s
        q[3] = (R[k, j] - R[j, k]) / s
    q = np.array(q)
    q /= np.linalg.norm(q)
    return q if q[3] >= 0 else -q


def trs(pos, q):
    m = np.eye(4)
    m[:3, :3] = qmat(q)
    m[:3, 3] = pos
    return m


class Esk:
    """Esqueleto: names, parent, local (4x4, YA en el espacio de B3), world (bind)."""

    def __init__(self, path):
        b = open(path, "rb").read()
        if b[:4] != b"#ESK":
            raise ValueError("no es ESK: %s" % path)
        so = _u32(b, 0x10)
        n = _u16(b, so)
        o_idx, o_nm, o_trs = (_u32(b, so + 4 + 4 * k) for k in range(3))
        self.names, self.parent, self.local = [], [], []
        for i in range(n):
            p = struct.unpack_from("<4h", b, so + o_idx + 8 * i)[0]
            self.names.append(_cstr(b, so + _u32(b, so + o_nm + 4 * i)))
            self.parent.append(p)
            t = struct.unpack_from("<12f", b, so + o_trs + 48 * i)
            pos = MIRROR @ np.array(t[0:3]) * SCALE
            q = (-t[4], -t[5], t[6], t[7])
            self.local.append(trs(pos, q))
        self.world = []
        for i in range(n):
            p = self.parent[i]
            self.world.append(self.world[p] @ self.local[i] if p >= 0 else self.local[i])
        self.index = {nm: i for i, nm in enumerate(self.names)}

    def descendants(self, root):
        out = {root}
        for i in range(len(self.names)):
            j = i
            while j >= 0 and j not in out:
                j = self.parent[j]
            if j >= 0:
                out.add(i)
        return out


class Submesh:
    """pos/nrm (n,3) en espacio de modelo de B3, uv (n,2), bones (n,4) nombres de hueso como
    indices en self.bone_names, weights (n,4), tris (m,3) con el winding de B3, texdefs."""


def read_emd(path):
    b = open(path, "rb").read()
    if b[:4] != b"#EMD":
        raise ValueError("no es EMD: %s" % path)
    subs = []
    for mi in range(_u16(b, 0x12)):
        mo = _u32(b, _u32(b, 0x14) + 4 * mi)
        for j in range(_u16(b, mo + 2)):
            me = mo + _u32(b, mo + _u32(b, mo + 4) + 4 * j)
            for k in range(_u16(b, me + 0x36)):
                s = me + _u32(b, me + _u32(b, me + 0x38) + 4 * k)
                subs.append(_submesh(b, s))
    return subs


def _submesh(b, s):
    flags, vsz, nv, vo = struct.unpack_from("<4I", b, s + 0x30)
    sm = Submesh()
    sm.name = _cstr(b, s + _u32(b, s + 0x40))
    sm.flags = flags
    ntd, to = b[s + 0x45], s + _u32(b, s + 0x48)
    sm.texdefs = [b[to + 12 * t + 1] for t in range(ntd)]
    if flags & 0x8000:
        raise ValueError("%s: vertices comprimidos (0x8000) no soportados" % sm.name)
    raw = np.frombuffer(b, np.uint8, nv * vsz, s + vo).reshape(nv, vsz)
    o = 0

    def take(cnt, dt):
        nonlocal o
        sz = np.dtype(dt).itemsize * cnt
        v = raw[:, o:o + sz].copy().view(dt).reshape(nv, cnt)
        o += sz
        return v
    P = take(3, "<f4").astype(np.float64)
    N = take(3, "<f4").astype(np.float64) if flags & 0x2 else np.zeros((nv, 3))
    UV = take(2, "<f4").astype(np.float64) if flags & 0x4 else np.zeros((nv, 2))
    if flags & 0x8:
        take(2, "<f4")
    if flags & 0x40:
        take(4, "u1")
    if flags & 0x80:
        take(3, "<f4")
    if flags & 0x200:
        BI = take(4, "u1").astype(int)
        BW = take(3, "<f4").astype(np.float64)
        BW = np.c_[BW, np.clip(1 - BW.sum(1), 0, 1)]
    else:
        BI, BW = np.zeros((nv, 4), int), np.c_[np.ones(nv), np.zeros((nv, 3))]
    sm.pos = (P @ MIRROR) * SCALE
    sm.nrm = N @ MIRROR
    # SDBH guarda v en [-1, 0] (convenio GL, con repeticion); B3 lee [0, 1]: se desplaza cada
    # submalla un numero entero de texturas (identico con repeticion, correcto con recorte)
    sm.uv = UV - np.floor(UV.mean(0)) if nv else UV
    sm.bone_names, bones, tris = [], np.zeros((nv, 4), int), []
    for g in range(_u16(b, s + 0x46)):
        go = s + _u32(b, s + _u32(b, s + 0x4C) + 4 * g)
        ni, nb, io_, no = struct.unpack_from("<4I", b, go)
        names = [_cstr(b, go + _u32(b, go + no + 4 * q)) for q in range(nb)]
        base = len(sm.bone_names)
        sm.bone_names += names
        idx = np.frombuffer(b, "<u2" if nv <= 0xFFFF else "<u4", ni, go + io_).astype(int)
        used = np.unique(idx)
        bones[used] = BI[used] + base
        # espejo -> se invierte el winding
        tris += [(idx[i], idx[i + 2], idx[i + 1]) for i in range(0, ni - 2, 3)]
    sm.bones, sm.weights = bones, BW
    sm.tris = np.array(tris, int).reshape(-1, 3)
    return sm


def load_source(folder):
    """{'esk', 'emb': [(nombre, img)], 'parts': {nombre_emd_sin_prefijo: [Submesh]}}"""
    esk = glob.glob(os.path.join(folder, "*.esk"))
    emb = glob.glob(os.path.join(folder, "*.emb"))
    if not esk or not emb:
        raise SystemExit("[X] %s: falta el .esk o el .emb" % folder)
    base = os.path.splitext(os.path.basename(esk[0]))[0]
    parts = {}
    for f in sorted(glob.glob(os.path.join(folder, "*.emd"))):
        key = os.path.splitext(os.path.basename(f))[0][len(base) + 1:]
        # algunos personajes escriben _BODY / _FACE en mayusculas (bcbbr, bcmag, bcmnb, bcmnh, bctlh, bctls):
        # antes "no hay cuerpo" (StopIteration) y la conversion fallaba
        key = re.sub(r"_(body|face)$", lambda m: "_" + m.group(1).lower(), key, flags=re.I)
        parts[key] = read_emd(f)
    return {"name": base, "esk": Esk(esk[0]), "emb": read_emb(emb[0]), "parts": parts}


def drop_broken(src):
    """Quita las submallas que piden texturas que el .emb no trae o no se pueden leer (bcgo2b62: cara L42 con la
    textura 16 de 14; antes IndexError) y las partes que se quedan vacias, con un aviso."""
    emb = src["emb"]
    for key in list(src["parts"]):
        subs = src["parts"][key]
        ok = [sm for sm in subs if all(t < len(emb) and emb[t][1] is not None for t in sm.texdefs)]
        if len(ok) < len(subs):
            print("aviso: %s: %d submalla(s) sin su textura en el .emb, fuera" % (key, len(subs) - len(ok)))
        if ok:
            src["parts"][key] = ok
        else:
            del src["parts"][key]


def cmd_info(folder):
    src = load_source(folder)
    print("%s: %d huesos, %d texturas" % (src["name"], len(src["esk"].names), len(src["emb"])))
    for i, (nm, img, _) in enumerate(src["emb"]):
        print("  tex %2d %-20s %s" % (i, nm, None if img is None else img.shape[1::-1]))
    for key, subs in src["parts"].items():
        for sm in subs:
            print("  %-22s %-16s flags=%#06x vtx=%4d tris=%4d tex=%s huesos=%d" % (
                key, sm.name, sm.flags, len(sm.pos), len(sm.tris), sm.texdefs, len(set(sm.bone_names))))


# =============================================================================
# Plantilla HD (big-endian)
# =============================================================================
def _be(b, o):
    return struct.unpack_from(">I", b, o)[0]


def _bef(b, o, n):
    return struct.unpack_from(">%df" % n, b, o)


def amb_kids(data):
    n, tbl = _be(data, 0x10), _be(data, 0x14)
    return [struct.unpack_from(">4I", data, tbl + 16 * k) for k in range(n)]


def amb_pack(kids):
    """[(bytes, tipo)] -> #AMB HD (como psp_amo.convert_model / los nativos)."""
    start = (0x20 + 16 * len(kids) + 31) // 32 * 32
    out = bytearray(struct.pack(">4s7I", b"#AMB", 0x20, 0, 2, len(kids), 0x20, start, 0))
    out += bytes(start - len(out))
    ents = []
    for data, typ in kids:
        out += bytes((-len(out)) % 32)
        ents.append((len(out), len(data), typ, 0))
        out += data
    out += bytes((-len(out)) % 32)
    for k, e in enumerate(ents):
        struct.pack_into(">4I", out, 0x20 + 16 * k, *e)
    return bytes(out)


def bone_suffix(label):
    s = label.lstrip("X")
    return s.split("_", 1)[1] if "_" in s else s


class Template:
    """Lo que se reutiliza de un bin HD nativo: esqueleto (registros, nombres, ejes del AWG0),
    paleta, ejes de las variantes, rampas y su tabla de draws (para elegir rampas)."""

    def __init__(self, path):
        data = open(path, "rb").read()
        if data[:4] != b"#AMB":
            raise SystemExit("[X] la plantilla debe ser un #AMB HD descomprimido: %s" % path)
        kids = amb_kids(data)
        self.kid_types = {bytes(data[o:o + 4]): t for o, s, t, _ in kids}
        a = next(o for o, s, t, _ in kids if data[o:o + 4] == b"#AWO")
        z = next((o, s) for o, s, t, _ in kids if data[o:o + 4] == b"#AZT")
        self.azt = bytes(data[z[0]:z[0] + z[1]])
        h = struct.unpack_from(">4s11I", data, a)
        self.nb, bones, self.namg, tbl, self.nlist, names = h[4], h[5], h[6], h[7], h[8], h[9]
        self.hdr_tail = h[10:12]
        nb = self.nb
        self.bone_recs = [list(struct.unpack_from(">8I", data, a + bones + 0x20 * i)) for i in range(nb)]
        self.bones_off = bones
        self.names = bytes(data[a + names:a + names + 32 * nb])
        self.labels = [self.names[32 * i:32 * i + 32].split(b"\0")[0].decode("latin1") for i in range(nb)]
        awgs = [a + _be(data, a + tbl + 4 * k) for k in range(self.namg)]
        g0 = awgs[0]
        hd = struct.unpack_from(">4s15I", data, g0)
        self.word0c = hd[3]
        ax = g0 + hd[5]
        self.axes = [bytearray(data[ax + 80 * i:ax + 80 * i + 80]) for i in range(nb)]
        self.links = []
        for i in range(nb):
            ln = []
            for f in (0x38, 0x3C, 0x40):
                p = _be(self.axes[i], f)
                ln.append((g0 + p - ax) // 80 if p else -1)
            self.links.append(ln)
        self.parent = [ln[2] for ln in self.links]
        self.local = [trs(_bef(self.axes[i], 16, 3), _bef(self.axes[i], 0, 4)) for i in range(nb)]
        self.flags = [_be(self.axes[i], 0x30) for i in range(nb)]
        pal, npal = g0 + hd[14], hd[15]
        self.palette = list(struct.unpack_from(">%dI" % npal, data, pal))
        mt, nm = g0 + hd[8], hd[9]
        self.materials = [bytes(data[mt + 0x50 * m:mt + 0x50 * m + 0x50]) for m in range(nm)]
        # draws del AWG0: (hueso de grupo, prim, material)
        self.draws = []
        for i in range(nb):
            arm = _be(self.axes[i], 0x34)
            grp = _be(data, g0 + arm + 4) if arm else 0
            if not grp:
                continue
            for k in range(_be(data, g0 + grp)):
                d = g0 + grp + 16 + 0x60 * k
                self.draws.append((i, _be(data, d + 0x20), _be(data, d + 0x14) >> 16))
        # ejes de las variantes (AWG de 1 hueso): uno de mano y uno de cara
        self.aux_axis = {}
        for g in awgs[1:]:
            hh = struct.unpack_from(">4s15I", data, g)
            lab = bytes(data[g + hh[7]:g + hh[7] + 32]).split(b"\0")[0].decode("latin1")
            self.aux_axis.setdefault("FACE" if "FACE" in lab else "HAND",
                                     bytes(data[g + hh[5]:g + hh[5] + 80]))
        self.textures = _azt_entries(self.azt)

    def world(self, local=None):
        local = local or self.local
        W = [None] * self.nb
        for i in range(self.nb):
            p = self.parent[i]
            W[i] = W[p] @ local[i] if p >= 0 else local[i].copy()
        return W

    def bone(self, suffix):
        return next((i for i, lab in enumerate(self.labels) if bone_suffix(lab) == suffix), None)

    def ramp_of(self, pred):
        """Rampa (textura) mas frecuente de los draws iluminados cuyo grupo cumple pred."""
        cnt = {}
        for g, prim, m in self.draws:
            r = _be(self.materials[m], 0x34)
            if pred(self.labels[g]) and r != 0xFFFFFFFF:
                cnt[r] = cnt.get(r, 0) + 1
        return max(cnt, key=cnt.get) if cnt else None


def _azt_entries(azt):
    """[(w, h, flags, blob DDS)] del #AZT (None = hueco)."""
    n, idx = struct.unpack_from(">II", azt, 0x10)
    out = []
    for t in range(n):
        o = _be(azt, idx + 4 * t)
        if not o:
            out.append(None)
            continue
        tid, flags, wh, lw, lh, w, h, do, ds = struct.unpack_from(">3I2H2H2I", azt, o)
        out.append((w, h, flags, azt[do:do + ds]))
    return out


# =============================================================================
# Texturas -> #AZT
# =============================================================================
def dds_dxt3(w, h):
    return (b"DDS \x7c\x00\x00\x00\x07\x10\x08\x00" + struct.pack("<IIIII", h, w, h * w, 0, 0) +
            bytes(44) + b"\x20\x00\x00\x00\x04\x00\x00\x00" + b"DXT3" + bytes(20) +
            struct.pack("<I", 0x1000) + bytes(16))


def encode_dxt3(rgba):
    sys.path.insert(0, os.path.join(HERE, "..", "mod center hd"))
    from texture_b3 import encode_dxt3 as enc
    return enc(np.ascontiguousarray(rgba))


def alpha4(a):
    """Alfa (h, w) uint8 -> bloques DXT3 de 8 B (orden de bloques 4x4 por filas)."""
    h, w = a.shape
    q = np.minimum(15, (a.astype(np.int32) + 8) // 17).astype(np.uint8)
    blk = q.reshape(h // 4, 4, w // 4, 4).transpose(0, 2, 1, 3).reshape(-1, 16)
    return (blk[:, 0::2] | (blk[:, 1::2] << 4)).astype(np.uint8)


def dxt1_to_dxt3(dds, alpha):
    """Transcodifica un DDS DXT1 (bytes) a bitmap DXT3 SIN perdida: el bloque de color DXT3 es
    el de DXT1 en modo 4 colores; los bloques DXT1 de 3 colores (c0 <= c1) se recodifican."""
    from PIL import Image
    h, w = struct.unpack_from("<II", dds, 12)
    blocks = np.frombuffer(dds, np.uint8, (w // 4) * (h // 4) * 8, 128).reshape(-1, 8).copy()
    c0 = blocks[:, 0].astype(int) | (blocks[:, 1].astype(int) << 8)
    c1 = blocks[:, 2].astype(int) | (blocks[:, 3].astype(int) << 8)
    bad = np.nonzero(c0 <= c1)[0]
    if len(bad):
        img = np.array(Image.open(io.BytesIO(dds)).convert("RGBA"))
        for k in bad:
            by, bx = divmod(int(k), w // 4)
            blk = img[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4].copy()
            blk[..., 3] = 255
            blocks[k] = np.frombuffer(encode_dxt3(blk)[8:], np.uint8)
    return np.concatenate([alpha4(alpha), blocks], 1).tobytes()


def build_azt(items):
    """items: [(w, h, flags, blob DDS DXT3 completo)] -> #AZT HD (layout de amt_ps2.to_azt)."""
    import zlib
    n = len(items)
    idx = 0x20
    first = idx + 4 * n
    data_base = (first + 0x30 * n + 0x1F) // 0x20 * 0x20
    hdr = bytearray(struct.pack(">4s7I", b"#AZT", 0, 0, 0, n, idx, 0, 0))
    index, ent, body = bytearray(), bytearray(), bytearray()
    for k, (w, h, flags, blob) in enumerate(items):
        lw, lh = (w - 1).bit_length(), (h - 1).bit_length()
        index += struct.pack(">I", first + len(ent))
        csize = 0x400 if flags & 0x80000000 else 0x40
        ent += struct.pack(">3I2H2H7I", k, flags, (w << 16) | h, lw, lh, w, h,
                           data_base + len(body), len(blob), 1, 0, 0, csize, 0)
        body += blob
    out = hdr + index + ent
    out += bytes(data_base - len(out))
    out += body
    struct.pack_into(">I", out, 0x1C, zlib.crc32(bytes(body)))
    return bytes(out)


def resize_rgba(img, w, h):
    from PIL import Image
    im = Image.fromarray(img)
    return np.array(Image.merge("RGBA", [c.resize((w, h), Image.LANCZOS) for c in im.split()]))


def raster_uv_mask(shape, uv, tris, grow=1):
    """Mascara (h, w) de los texeles que cubren los triangulos en espacio UV (+ dilatacion)."""
    from PIL import Image, ImageDraw
    h, w = shape
    im = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(im)
    for t in tris:
        d.polygon([(uv[i, 0] % 1.0 * w, uv[i, 1] % 1.0 * h) for i in t], fill=255)
    m = np.array(im) > 0
    for _ in range(grow):
        m = m | np.roll(m, 1, 0) | np.roll(m, -1, 0) | np.roll(m, 1, 1) | np.roll(m, -1, 1)
    return m


# =============================================================================
# Escritura HD: AWG y AWO (layout de amo2awo, validado contra los nativos)
# =============================================================================
def _box(P):
    if not len(P):
        return bytes(0x40)
    lo, hi = P.min(0), P.max(0)
    c = (lo + hi) / 2
    r1 = float(np.linalg.norm(P - c, axis=1).max())
    return struct.pack(">16f", *lo, 1.0, *hi, 1.0, *c, 1.0, r1, float(np.linalg.norm(hi - c)), 0, 0)


def awg_bytes(names, axes, links, word0c, gid0, groups, materials, palette):
    """groups: {hueso: [parte]}; parte = {"skinned", "mat", "win": [ventana 44 B], "tris",
    "pts": (n,3) en el marco del hueso del grupo, "label", "nmax"}.
    Devuelve (bytes, offset de los ejes, estadisticas)."""
    import amo2awo as A
    from b3_gateway import check_strip
    nb = len(axes)
    windows, ib, recs = [], [], {}
    for bone in sorted(groups):
        recs[bone] = []
        for p in groups[bone]:
            a0 = len(windows)
            uniq, remap = {}, []
            for w in p["win"]:
                j = uniq.get(w)
                if j is None:
                    j = uniq[w] = len(uniq)
                    windows.append(w)
                remap.append(a0 + j)
            tris = [t for t in ((remap[a], remap[b], remap[c]) for a, b, c in p["tris"]) if len(set(t)) == 3]
            if p["skinned"]:
                seq = A.stripify(tris)
                ok, miss, _ = check_strip(seq, tris)
                if not ok:
                    raise SystemExit("[X] stripify perdio %d triangulos (%s)" % (miss, p["label"]))
                prim, nprim = 5, max(0, len(seq) - 2)
            else:
                seq, prim, nprim = [i for t in tris for i in t], 4, len(tris)
            pts = p["pts"]
            c = (pts.min(0) + pts.max(0)) / 2 if len(pts) else np.zeros(3)
            # radio 0 en los pielados, como los nativos: el juego cullea el draw con esta esfera
            # en bind y los pies animados se salian de ella (desaparecian en el reposo, 2026-10-06)
            rad = 0.0 if p["skinned"] or not len(pts) else float(np.linalg.norm(pts - c, axis=1).max())
            recs[bone].append({"c": c, "r": rad, "mat": p["mat"], "prim": prim, "skinned": p["skinned"],
                               "A": (a0, len(windows) - a0), "B": (len(ib), nprim),
                               "label": p["label"], "nmax": p.get("nmax", 0)})
            ib += seq
    if len(windows) > 0xFFFF:
        raise SystemExit("[X] %d ventanas: no caben en una IB de 16 bits" % len(windows))
    o = A.Out()
    o.b += bytes(0x40)
    names_off = len(o.b)
    o.b += names
    mats_off = len(o.b)
    for m in materials:
        o.b += m
    axes_off = len(o.b)
    o.b += bytes(80 * nb)
    arms_off = len(o.b)
    o.b += bytes(0x14 * nb)
    o.pad(16)
    box_off, grp_off = {}, {}
    for bone in sorted(groups):
        box_off[bone] = len(o.b)
        pts = np.concatenate([p["pts"] for p in groups[bone]]) if groups[bone] else np.zeros((0, 3))
        bx = bytearray(_box(pts))
        if bone == 0 and gid0 == 0:      # el grupo raiz del cuerpo (pielado) lleva radios 0
            bx[0x30:0x38] = bytes(8)
        o.b += bx
    for bone in sorted(groups):
        grp_off[bone] = len(o.b)
        o.u32(len(recs[bone]), 0, 0, 0)
        for d in recs[bone]:
            rec = bytearray(0x60)
            struct.pack_into(">4f", rec, 0, *d["c"], 1.0)
            struct.pack_into(">fIII", rec, 0x10, d["r"], d["mat"] << 16, 0x1158, 44)
            struct.pack_into(">6I", rec, 0x20, d["prim"], 0, *d["A"], *d["B"])
            rec[0x38:0x60] = A.label_bytes(1 if d["skinned"] else 0, d["label"], d["nmax"])
            o.b += rec
    vb0 = len(o.b)
    for w in windows:
        o.b += w
    ib_off = len(o.b)
    o.b += struct.pack(">%dH" % len(ib), *ib)
    o.pad(4)
    pal_off = len(o.b)
    o.u32(*palette)
    o.pad(16)
    for i in range(nb):
        raw = bytearray(axes[i])
        struct.pack_into(">I", raw, 0x34, arms_off + 0x14 * i)
        for f, k in zip((0x38, 0x3C, 0x40), links[i]):
            struct.pack_into(">I", raw, f, axes_off + 80 * k if k >= 0 else 0)
        o.b[axes_off + 80 * i:axes_off + 80 * i + 80] = raw
        struct.pack_into(">5I", o.b, arms_off + 0x14 * i, gid0 + i, grp_off.get(i, 0), 0, box_off.get(i, 0), 0)
    texs = set()
    for m in materials:
        for f in (0x30, 0x34):
            t = _be(m, f)
            if t != 0xFFFFFFFF:
                texs.add(t)
    struct.pack_into(">4s15I", o.b, 0, b"#AWG", 0x40, 0, word0c, nb, axes_off, len(texs), names_off,
                     mats_off, len(materials), vb0, 44 * len(windows), ib_off, 2 * len(ib), pal_off,
                     len(palette))
    return bytes(o.b), axes_off, {"windows": len(windows), "ib": len(ib),
                                  "draws": sum(len(r) for r in recs.values())}


def awo_bytes(tpl, awgs, lists, nlist):
    """awgs: [(bytes, axes_off)]; lists[hueso] = [(awg, id global, hueso dentro del awg)] x nlist."""
    import amo2awo as A
    nb = tpl.nb
    o = A.Out()
    o.b += bytes(0x30)
    bones_off = 0x30
    o.b += bytes(0x20 * nb)
    tbl_off = len(o.b)
    o.b += bytes(4 * len(awgs))
    names_off = len(o.b)
    o.b += tpl.names
    awg_abs = []
    for raw, _ in awgs:
        o.pad(32)
        awg_abs.append(len(o.b))
        o.b += raw
    o.pad(32)
    list_off = []
    for i in range(nb):
        list_off.append(len(o.b))
        for k, gid, bi in lists[i]:
            o.u32(k, gid, awg_abs[k] + awgs[k][1] + 80 * bi, 0)

    def rb(p):
        return bones_off + 0x20 * ((p - tpl.bones_off) // 0x20) if p else 0
    for i in range(nb):
        w = tpl.bone_recs[i]
        struct.pack_into(">8I", o.b, bones_off + 0x20 * i, w[0], list_off[i], rb(w[2]), rb(w[3]), rb(w[4]),
                         w[5], w[6], w[7])
    for k, x in enumerate(awg_abs):
        struct.pack_into(">I", o.b, tbl_off + 4 * k, x)
    struct.pack_into(">4s11I", o.b, 0, b"#AWO", 0x30, 0, 0, nb, bones_off, len(awgs), tbl_off, nlist,
                     names_off, *tpl.hdr_tail)
    o.pad(32)
    return bytes(o.b)


def material(lit, tex, ramp, diffuse=1.0, env=False):
    """env: vtype 0xF5 (0xB5 + bit 0x40) = reflejo, como la cupula de Freezer o la visera del Gran Saiyaman:
    el juego pinta la textura con la normal en camara (sphere map), no con la uv de la malla."""
    vt = (0x1F5 if env else 0x1B5, 0x29BD) if lit else (0x1B4, 0x1B4)
    return struct.pack(">8f12I", diffuse, diffuse, diffuse, 0, 1, 1, 1, 0, 0, 5, 0, 5, tex,
                       ramp if lit else 0xFFFFFFFF, vt[0], vt[1], 0x44, 0x44, 0, 0)


# =============================================================================
# Conversion
# =============================================================================
NULL_MARKERS = {"NLF", "NRF", "NW", "NLA", "NRA", "NH"}   # marcadores de B3: se quedan los de la plantilla


def sdbh_suffix(name):
    """'xghf_lobi1' -> 'LOBI1', 'f_jaw' -> 'M_JAW', 'lhand' -> 'L00_LHAND'."""
    m = re.match(r"^x[a-z0-9]{3}_(.*)$", name)
    s = (m.group(1) if m else name).upper()
    if s.startswith("F_"):
        s = "M_" + s[2:]
    if s in ("LHAND", "RHAND"):
        s = "L00_" + s
    return s


def map_bones(esk, tpl):
    """{indice ESK: indice plantilla}. Solo el arbol de 'waist' es esqueleto; el resto de hijos de
    Root son nodos de malla (cuerpo, caras, manos, dientes) -> raiz. Los huesos sin equivalente
    (dedos, g_*_Hand) suben al padre."""
    by_suf = {bone_suffix(l): i for i, l in reversed(list(enumerate(tpl.labels)))}
    waist = esk.index.get("waist")
    skel = esk.descendants(waist) if waist is not None else set()
    out = {}
    for i, nm in enumerate(esk.names):
        if i not in skel:
            out[i] = 0
            continue
        j = i
        while j >= 0 and sdbh_suffix(esk.names[j]) not in by_suf:
            j = esk.parent[j]
        out[i] = by_suf[sdbh_suffix(esk.names[j])] if j >= 0 else 0
    return out, skel


def rot_x(deg):
    a = math.radians(deg)
    return np.array([[1, 0, 0], [0, math.cos(a), -math.sin(a)], [0, math.sin(a), math.cos(a)]])


class Rig:
    """Bind nuevo: huesos de la plantilla con la pose de SDBH (los que existen en ambos), mas el
    giro de reposo de las colas del cinturon. geomW = bind en el que estan los vertices."""

    def __init__(self, tpl, esk, belt_deg=80.0):
        self.tpl = tpl
        self.bmap, skel = map_bones(esk, tpl)
        self.bmap_name = {esk.names[i]: t for i, t in self.bmap.items()}
        src_of = {}
        for i in skel:
            t = self.bmap[i]
            if sdbh_suffix(esk.names[i]) == bone_suffix(tpl.labels[t]):
                src_of[t] = i
        W = [None] * tpl.nb
        self.overridden = []
        for i in self._order():
            p = tpl.parent[i]
            s = src_of.get(i)
            if s is not None and bone_suffix(tpl.labels[i]) not in NULL_MARKERS:
                W[i] = esk.world[s].copy()
                if np.abs(W[i] - (W[p] @ tpl.local[i] if p >= 0 else tpl.local[i])).max() > 1e-3:
                    self.overridden.append(tpl.labels[i])
            else:
                W[i] = W[p] @ tpl.local[i] if p >= 0 else tpl.local[i].copy()
        self.geomW = [w.copy() for w in W]
        # colas del cinturon: en SDBH salen en horizontal (la fisica las cuelga); B3 anula la
        # fisica de los modelos nuevos -> se cuelgan en el bind (giro en X sobre su raiz)
        self.belt = []
        if belt_deg:
            for root in ("LOBI1", "ROBI1"):
                r = tpl.bone(root)
                if r is None or r not in src_of:
                    continue
                piv = W[r][:3, 3].copy()
                R = np.eye(4)
                R[:3, :3] = rot_x(belt_deg)
                T = np.eye(4)
                T[:3, 3] = piv
                Ti = np.eye(4)
                Ti[:3, 3] = -piv
                for d in self._descendants(r):
                    W[d] = T @ R @ Ti @ W[d]
                self.belt.append(tpl.labels[r])
        self.W = W
        self.local = [np.linalg.inv(W[tpl.parent[i]]) @ W[i] if tpl.parent[i] >= 0 else W[i]
                      for i in range(tpl.nb)]
        self.inv_geo = [np.linalg.inv(w) for w in self.geomW]

    def _order(self):
        done, out = set(), []
        while len(out) < self.tpl.nb:
            for i in range(self.tpl.nb):
                p = self.tpl.parent[i]
                if i not in done and (p < 0 or p in done):
                    done.add(i)
                    out.append(i)
        return out

    def _descendants(self, r):
        out = {r}
        for i in self._order():
            if self.tpl.parent[i] in out:
                out.add(i)
        return out

    def is_ancestor(self, a, b, depth=2):
        p = self.tpl.parent[b]
        for _ in range(depth):
            if p < 0:
                return False
            if p == a:
                return True
            p = self.tpl.parent[p]
        return False

    def axes(self):
        out = []
        for i in range(self.tpl.nb):
            raw = bytearray(self.tpl.axes[i])
            L = self.local[i]
            q = mat_q(L[:3, :3])
            # mismo signo que el cuaternion nativo (q y -q son el mismo giro, pero el juego
            # interpola desde el de reposo: con w = 0 el signo cambiado congelaba la boca, 2026-10-06)
            if np.dot(q, _bef(self.tpl.axes[i], 0, 4)) < 0:
                q = -q
            struct.pack_into(">4f", raw, 0, *q)
            struct.pack_into(">3f", raw, 16, *L[:3, 3])
            out.append(bytes(raw))
        return out


def skin_vertex(rig, sm, k, slots):
    """(hueso, peso) de B3 para el vertice k: el resto del peso va al PADRE (semantica de la
    HD/PS2: vertices con w < 1 a la altura de la articulacion; ver informe)."""
    infl = {}
    for bi, w in zip(sm.bones[k], sm.weights[k]):
        if w <= 0:
            continue
        t = rig.bmap_name[sm.bone_names[bi]]
        infl[t] = infl.get(t, 0.0) + float(w)
    items = sorted(infl.items(), key=lambda x: -x[1])
    b, w = items[0][0], 1.0
    if len(items) > 1:
        (b1, w1), (b2, w2) = items[0], items[1]
        if rig.is_ancestor(b2, b1):
            b, w = b1, w1 / (w1 + w2)
        elif rig.is_ancestor(b1, b2):
            b, w = b2, w2 / (w1 + w2)
    while b not in slots and b > 0:
        b, w = rig.tpl.parent[b], 1.0
    if b not in slots:
        b, w = min(slots), 1.0
    return b, (1.0 if w > 0.985 else round(w, 3))


def windows_skinned(rig, sm, slots, lit):
    import amo2awo as A
    out, pts, nmax = [], [], 0
    for k in range(len(sm.pos)):
        b, w = skin_vertex(rig, sm, k, slots)
        M = rig.inv_geo[b]
        p = M[:3, :3] @ sm.pos[k] + M[:3, 3]
        n = M[:3, :3] @ sm.nrm[k] if lit else None
        out.append(A.win_bytes(p.tolist(), w, slots[b], None if n is None else n.tolist(), None,
                               sm.uv[k].tolist()))
        pts.append(rig.W[b][:3, :3] @ p + rig.W[b][:3, 3])     # bind nuevo (colas colgadas)
        nmax = max(nmax, b)
    return out, np.array(pts), nmax


def windows_rigid(M, pos, nrm, uv, lit):
    import amo2awo as A
    P = pos @ M[:3, :3].T + M[:3, 3]
    N = nrm @ M[:3, :3].T
    return [A.win_bytes(P[k].tolist(), 1.0, 0, N[k].tolist() if lit else None, None, uv[k].tolist())
            for k in range(len(P))], P


def smooth_seams(subs, max_deg=30.0):
    """Une las normales de los vertices que comparten posicion (costuras de UV y entre submallas)
    cuando difieren menos de max_deg: el brillo de borde HD (|N.V|) no marca la costura. Los
    pliegues intencionados (> max_deg) se respetan."""
    P = np.concatenate([sm.pos for sm in subs])
    N = np.concatenate([sm.nrm for sm in subs])
    groups = {}
    for i, p in enumerate(np.round(P, 3)):
        groups.setdefault(tuple(p), []).append(i)
    cos = math.cos(math.radians(max_deg))
    n = 0
    for ids in groups.values():
        if len(ids) < 2:
            continue
        nn = N[ids]
        if (nn @ nn.T).min() >= cos and (nn @ nn.T).min() < 0.9999:
            m = nn.sum(0)
            N[ids] = m / (np.linalg.norm(m) or 1.0)
            n += len(ids)
    k = 0
    for sm in subs:
        sm.nrm = N[k:k + len(sm.pos)]
        k += len(sm.pos)
    return n


# Variantes de pelo derivadas (cuando SDBH no trae la forma): (alargar, levantar, levantar flequillo)
HAIR_PRESETS = {"ssj2": (0.32, 42.0, 55.0),     # SSJ2 desde el SSJ: puntas mas largas y de pie, flequillo arriba
                "pu": (0.12, 18.0, 0.0)}        # Potencial: algo mas de punta, flequillo intacto


def spikier_hair(sm, center, grow, lift, front_lift, keep_x=0.25):
    """Pelo de punta (copia de la submalla): cada vertice se separa del craneo (esfera de radio r0
    = percentil 25 de las distancias al centro) en e = r - r0; e crece un `grow` (relativo, mas en
    las puntas) y la direccion gira hacia +Y `lift` grados (mas en los mechones que caen) y
    `front_lift` mas en el flequillo (delante y por debajo de la coronilla), salvo el mechon central
    (|x| < keep_x). Continuo: la raiz (e = 0) no se mueve. Las normales giran con su vertice."""
    import copy
    P = sm.pos - center
    r = np.linalg.norm(P, axis=1)
    d = P / r[:, None]
    r0 = np.percentile(r, 25)
    e = np.clip(r - r0, 0, None)
    t = e / max(e.max(), 1e-9)
    ang = lift * t * (1 - np.clip(d[:, 1], 0, None))
    front = (d[:, 2] > 0.3) & (d[:, 1] < 0.45) & (np.abs(P[:, 0]) > keep_x)
    ang = np.radians(ang + np.where(front, front_lift * t, 0.0))
    ax = np.cross(d, [0.0, 1.0, 0.0])
    n = np.linalg.norm(ax, axis=1, keepdims=True)
    ax = np.where(n > 1e-6, ax / np.maximum(n, 1e-12), 0.0)

    def rodrigues(v):
        c, s = np.cos(ang)[:, None], np.sin(ang)[:, None]
        return v * c + np.cross(ax, v) * s + ax * (np.einsum("ij,ij->i", ax, v)[:, None]) * (1 - c)
    out = copy.copy(sm)
    out.pos = center + d * np.minimum(r, r0)[:, None] + rodrigues(d * (e * (1 + grow * t))[:, None])
    out.nrm = rodrigues(sm.nrm)
    return out


def uv_tint(img, uv):
    """Color medio de la textura bajo las uv de una submalla (cuantizado: mismas claves de textura)."""
    if not len(uv):
        return (255, 255, 255)
    h, w = img.shape[:2]
    x = np.clip((np.mod(uv[:, 0], 1.0) * w).astype(int), 0, w - 1)
    y = np.clip((np.mod(uv[:, 1], 1.0) * h).astype(int), 0, h - 1)
    return tuple(int(min(255, round(c / 16) * 16)) for c in img[y, x, :3].astype(float).mean(0))


def colored(img):
    """True si una rampa de SDBH tiene color (piel, pelo rubio); False si es gris (ropa, pelo negro)."""
    if img is None:
        return False
    c = img[..., :3].astype(float)
    return bool((c.max(2) - c.min(2)).mean() > 25)


# =============================================================================
# Manos: posturas de B3 (L00..L22, mallas rigidas) horneadas con los dedos de SDBH
# =============================================================================
FINGERS = ("Thumb", "Index", "Middle", "Ring", "Pinky")
# Posturas (mano IZQUIERDA; la derecha es su espejo). Dedo = (flexion j1, j2, j3, separacion,
# + = lejos del corazon); pulgar = (z, y, x de la base, flexion j2, j3). Grados, marco local del
# hueso (flexion = +Z: los dedos se cierran hacia +Y, la palma). Las flexiones se fijaron a mano
# mirando las variantes nativas de Gohan adulto (225) y de Another Road (L06); el pulgar se
# ajusto despues (`ajustar-pulgares`: chamfer de superficie contra la variante nativa).
#   L00 reposo  L01 puño  L02 palma plana  L04 ahuecada  L05 abierta  L06 señalar  L10 garra  L11 gancho
HAND_POSES = {
    0: {"Index": (12, 18, 10, -4), "Middle": (14, 20, 10, 0), "Ring": (16, 22, 12, 4), "Pinky": (18, 24, 12, 10),
        "Thumb": (12, 5, -14, 12, 4)},
    1: {"Index": (90, 100, 45, 0), "Middle": (90, 100, 45, 0), "Ring": (90, 100, 45, 0), "Pinky": (90, 100, 45, 0),
        "Thumb": (7, -6, 35, -9, 69)},
    2: {"Index": (0, 0, 0, -3), "Middle": (0, 0, 0, 0), "Ring": (0, 0, 0, -3), "Pinky": (0, 0, 0, -4),
        "Thumb": (-7, -11, 10, -10, -10)},
    4: {"Index": (30, 50, 30, 8), "Middle": (35, 55, 30, 0), "Ring": (40, 55, 30, 6), "Pinky": (45, 55, 30, 14),
        "Thumb": (6, 1, -12, 30, 13)},
    5: {"Index": (0, 0, 0, 12), "Middle": (0, 0, 0, 2), "Ring": (0, 0, 0, 10), "Pinky": (0, 0, 0, 22),
        "Thumb": (14, 9, -29, 17, 22)},
    6: {"Index": (0, 0, 0, 0), "Middle": (90, 100, 45, 0), "Ring": (90, 100, 45, 0), "Pinky": (90, 100, 45, 0),
        "Thumb": (9, -5, 14, -10, 63)},
    10: {"Index": (10, 60, 45, 12), "Middle": (10, 60, 45, 2), "Ring": (10, 60, 45, 10), "Pinky": (10, 60, 45, 20),
         "Thumb": (23, 20, 6, 39, 67)},
    11: {"Index": (70, 70, 20, 0), "Middle": (70, 70, 20, 0), "Ring": (70, 70, 20, 0), "Pinky": (70, 70, 20, 0),
         "Thumb": (-10, 30, 40, 56, 70)},
}
# variantes por mano: las de Gohan adulto (donante) + L06 derecha de Another Road (moveset de SB)
HAND_SETS = {"L": (1, 2, 4, 5, 10, 11), "R": (1, 2, 4, 5, 6, 10, 11)}
SPREAD_SIGN = {"Index": 1, "Middle": 1, "Ring": -1, "Pinky": -1}
# limites del ajuste del pulgar (sin hiperextension): z, y, x de la base, j2, j3
THUMB_BOUNDS = [(-30, 60), (-45, 45), (-45, 45), (-10, 70), (-10, 70)]


def hand_angles(pose, side):
    """Postura semantica (HAND_POSES) -> {(dedo, articulacion): {eje: grados}} para `side`."""
    m = 1 if side == "L" else -1          # espejo: los giros en Y y X cambian de signo
    out = {}
    for f, v in pose.items():
        if f == "Thumb":
            z, y, x, j2, j3 = v
            out[(f, 1)] = {"z": z, "y": m * y, "x": m * x}
        else:
            j1, j2, j3, s = v
            out[(f, 1)] = {"z": j1, "y": m * SPREAD_SIGN[f] * s}
        j2, j3 = (v[3], v[4]) if f == "Thumb" else (v[1], v[2])
        out[(f, 2)], out[(f, 3)] = {"z": j2}, {"z": j3}
    return out


def _rot(axis, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    i, j = {"x": (1, 2), "y": (2, 0), "z": (0, 1)}[axis]
    R = np.eye(4)
    R[i, i], R[i, j], R[j, i], R[j, j] = c, -s, s, c
    return R


def pose_hand(esk, sm, side, angles):
    """Malla de la mano `sm` (bind, espacio de modelo B3) con los dedos girados (angles de
    hand_angles, en el marco local de cada hueso). Piel lineal con los 4 pesos de SDBH.
    Devuelve (pos, nrm)."""
    W = list(esk.world)
    rot = {}
    for (f, k), axes in angles.items():
        i = esk.index.get("%s%s%d" % (side, f, k))
        if i is not None:
            R = np.eye(4)
            for ax in "zyx":
                if axes.get(ax):
                    R = R @ _rot(ax, axes[ax])
            rot[i] = R
    for i in range(len(W)):
        p = esk.parent[i]
        if p >= 0 and (i in rot or W[p] is not esk.world[p]):
            W[i] = W[p] @ esk.local[i] @ rot.get(i, np.eye(4))
    skin = {nm: W[esk.index[nm]] @ np.linalg.inv(esk.world[esk.index[nm]]) for nm in set(sm.bone_names)}
    M = np.array([skin[nm] for nm in sm.bone_names])          # (nb, 4, 4)
    Mv = np.einsum("vk,vkij->vij", sm.weights, M[sm.bones])   # (n, 4, 4)
    P = np.einsum("vij,vj->vi", Mv[:, :3, :3], sm.pos) + Mv[:, :3, 3]
    N = np.einsum("vij,vj->vi", Mv[:, :3, :3], sm.nrm)
    N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-12
    return P, N


def decimate(sm, target_tris, max_flip_deg=40.0):
    """Reduce una submalla pielada a ~target_tris triangulos (copia): colapso de MEDIA arista u->v
    con error cuadrico (Garland-Heckbert), sin vertices nuevos (v conserva su pos/uv/normal/pesos).
    Restricciones: no se mueven vertices de costura de UV (posicion compartida) ni de borde; u y v
    con el mismo hueso dominante y pesos parecidos (las articulaciones de los dedos siguen
    doblando bien); se rechaza el colapso que gire una cara mas de max_flip_deg."""
    import copy
    import heapq
    P, T = sm.pos, [list(t) for t in sm.tris]
    n = len(P)
    key = {}
    for i, p in enumerate(np.round(P, 4)):
        key.setdefault(tuple(p), []).append(i)
    locked = np.zeros(n, bool)
    for ids in key.values():
        if len(ids) > 1:
            locked[ids] = True
    edges = {}
    for t in T:
        for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            e = (min(a, b), max(a, b))
            edges[e] = edges.get(e, 0) + 1
    for (a, b), c in edges.items():
        if c == 1:
            locked[a] = locked[b] = True
    W = np.zeros((n, len(sm.bone_names)))
    for k in range(4):
        np.add.at(W, (np.arange(n), sm.bones[:, k]), sm.weights[:, k])
    dom = W.argmax(1)
    Q = np.zeros((n, 4, 4))
    faces_of = [set() for _ in range(n)]
    for fi, (a, b, c) in enumerate(T):
        nrm = np.cross(P[b] - P[a], P[c] - P[a])
        ar = np.linalg.norm(nrm)
        if ar < 1e-12:
            continue
        pl = np.append(nrm / ar, -np.dot(nrm / ar, P[a]))
        K = np.outer(pl, pl)             # sin ponderar por area: protege puntas y nudillos
        for v in (a, b, c):
            Q[v] += K
            faces_of[v].add(fi)
    alive = [True] * len(T)
    nf = sum(alive)
    ver = np.zeros(n, int)
    heap = []

    def push(u):
        if locked[u]:
            return
        nb = {x for fi in faces_of[u] for x in T[fi]} - {u}
        for v in nb:
            if dom[u] != dom[v] or np.abs(W[u] - W[v]).sum() > 0.3:
                continue
            h = np.append(P[v], 1.0)
            heapq.heappush(heap, (float(h @ (Q[u] + Q[v]) @ h), int(ver[u]), u, v))
    for u in range(n):
        push(u)
    cos_lim = math.cos(math.radians(max_flip_deg))
    while nf > target_tris and heap:
        c, vu, u, v = heapq.heappop(heap)
        if vu != ver[u] or not faces_of[u]:
            continue
        if not any(v in T[fi] for fi in faces_of[u]):
            continue
        ok = True
        for fi in faces_of[u]:
            t = T[fi]
            if v in t:
                continue
            old = np.cross(P[t[1]] - P[t[0]], P[t[2]] - P[t[0]])
            t2 = [v if x == u else x for x in t]
            new = np.cross(P[t2[1]] - P[t2[0]], P[t2[2]] - P[t2[0]])
            ln = np.linalg.norm(old) * np.linalg.norm(new)
            if ln < 1e-14 or np.dot(old, new) / ln < cos_lim:
                ok = False
                break
        if not ok:
            continue
        for fi in list(faces_of[u]):
            t = T[fi]
            if v in t:
                alive[fi] = False
                nf -= 1
                for x in t:
                    faces_of[x].discard(fi)
            else:
                T[fi] = [v if x == u else x for x in t]
                faces_of[v].add(fi)
        faces_of[u] = set()
        Q[v] += Q[u]
        ver[v] += 1
        for x in {x for fi in faces_of[v] for x in T[fi]}:
            ver[x] += 1
            push(x)
    keep_t = [T[fi] for fi in range(len(T)) if alive[fi]]
    used = sorted({x for t in keep_t for x in t})
    remap = {o: i for i, o in enumerate(used)}
    out = copy.copy(sm)
    out.pos, out.nrm, out.uv = sm.pos[used], sm.nrm[used], sm.uv[used]
    out.bones, out.weights = sm.bones[used], sm.weights[used]
    out.tris = np.array([[remap[x] for x in t] for t in keep_t], int)
    return out


def posed_hands(src, subs_of=None):
    """{'L'|'R': {numero: [Submesh posada]}} con HAND_POSES (L00 + HAND_SETS)."""
    import copy
    out = {}
    for side in "LR":
        subs = (subs_of or {}).get(side) or src["parts"].get("%s_hand" % side)
        if not subs:
            continue
        out[side] = {}
        for num in (0,) + HAND_SETS[side]:
            posed = []
            for sm in subs:
                q = copy.copy(sm)
                q.pos, q.nrm = pose_hand(src["esk"], sm, side, hand_angles(HAND_POSES[num], side))
                posed.append(q)
            out[side][num] = posed
    return out


def surface_samples(P, T, n, rng):
    """(triangulo, baricentricas) de n puntos repartidos por area sobre la malla."""
    a = np.linalg.norm(np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]]), axis=1)
    t = rng.choice(len(T), n, p=a / a.sum())
    r1, r2 = rng.random(n), rng.random(n)
    s = np.sqrt(r1)
    return t, np.c_[1 - s, s * (1 - r2), s * r2]


def _at(P, T, smp):
    t, w = smp
    return np.einsum("nk,nkj->nj", w, P[T[t]])


def fit_thumb(esk, sm, side, M, target, target_tris, pose, cut=0.2, n=1500):
    """Ajusta el pulgar de `pose` (dedos fijos) para que la mano de SDBH, en el marco local M de la
    mano B3, se parezca a la variante nativa target/target_tris (en ese marco): chamfer simetrico
    entre puntos de la SUPERFICIE de ambas (los nativos tienen pocos vertices), sin la muñequera
    (x local < cut). Devuelve (pulgar, error antes, error despues)."""
    from scipy.optimize import minimize
    from scipy.spatial import cKDTree
    rng = np.random.default_rng(7)
    Tp = _at(target, target_tris, surface_samples(target, target_tris, n, rng))
    Tp = Tp[Tp[:, 0] >= cut]
    tree_t = cKDTree(Tp)
    smp = surface_samples(sm.pos, sm.tris, n, rng)

    def cost(th):
        P, _ = pose_hand(esk, sm, side, hand_angles(dict(pose, Thumb=tuple(th)), side))
        Q = _at(P @ M[:3, :3].T + M[:3, 3], sm.tris, smp)
        Q = Q[Q[:, 0] >= cut]
        return float(np.mean(tree_t.query(Q)[0] ** 2) + np.mean(cKDTree(Q).query(Tp)[0] ** 2))
    best = None
    for start in (np.clip(pose["Thumb"], *np.array(THUMB_BOUNDS).T), np.zeros(5)):
        r = minimize(cost, start, method="Powell", bounds=THUMB_BOUNDS,
                     options={"maxiter": 3000, "xtol": 0.5, "ftol": 1e-6})
        if best is None or r.fun < best.fun:
            best = r
    return tuple(int(round(float(v))) for v in best.x), cost(pose["Thumb"]), best.fun


def cmd_fit_thumbs(folder, template, extra=None):
    """Reajusta el pulgar de HAND_POSES contra las variantes nativas (mano izquierda de la
    plantilla; las que solo tenga `extra`, p.ej. L06 derecha de Another Road, con su mano) e
    imprime los valores para pegarlos en HAND_POSES."""
    sys.path.insert(0, os.path.join(HERE, "..", "mod center hd"))
    from model_render import Model
    src = load_source(folder)
    tm = Model(template)
    W = tm.worlds()
    done = set()
    for path in [template] + ([extra] if extra else []):
        m = Model(path)
        for side in "LR":
            sm = src["parts"]["%s_hand" % side][0]
            b = m.bone("L00_%sHAND" % side)
            M = np.linalg.inv(W[tm.bone("L00_%sHAND" % side)])
            nat = {0: (m.pos, m.tris[np.all(m.vbone[m.tris] == b, 1)])}
            for lab, k in m.variants().items():
                mm = re.search(r"_L(\d\d)_%sHAND$" % side, lab)
                if mm:
                    v = Model(path, awg=k)
                    nat[int(mm.group(1))] = (v.pos, v.tris)
            # un port de PSP es algo mas pequeño: se escala a la mano de SDBH
            t0 = nat[0][0][np.unique(nat[0][1])]
            P0 = sm.pos @ M[:3, :3].T + M[:3, 3]
            s = 1.0 if path == template else np.ptp(P0[:, 0]) / max(np.ptp(t0[:, 0]), 1e-6)
            for num, (TP, TT) in sorted(nat.items()):
                if num in done or num not in HAND_POSES:
                    continue
                th, e0, e1 = fit_thumb(src["esk"], sm, side, M, TP * s, TT, HAND_POSES[num])
                if side == "R":
                    th = (th[0], -th[1], -th[2], th[3], th[4])
                done.add(num)
                print("    %d: pulgar %s  (error %.4f -> %.4f, %s %s)" % (num, th, e0, e1, side,
                                                                         os.path.basename(path)))


class Textures:
    """#AZT de salida: texturas base de SDBH (DXT1 -> DXT3 sin perdida; alfa 0, 255 en ojos y en
    lo no iluminado) + rampas nativas de la plantilla copiadas tal cual."""

    def __init__(self, emb, tpl, cap):
        self.emb, self.tpl, self.cap = emb, tpl, cap
        self.keys, self.masks, self.unlit, self.caps = [], {}, set(), {}

    def _get(self, key):
        if key not in self.keys:
            self.keys.append(key)
        return self.keys.index(key)

    def sdbh(self, i):
        return self._get(("sdbh", i))

    def ramp(self, i):
        return self._get(("tpl", i))

    def env(self, i, tint):
        """Reflejo de un material ToonEnvironment_W de SDBH (su 3.a textura) tenido con el color de su base."""
        return self._get(("env", i, tint))

    def conv_ramp(self, i, rows_from):
        """Rampa de SDBH convertida a la resta de B3: rampa = 1 - rampa SDBH (exacto con base blanca);
        las filas 56-63 (contorno) se copian de la rampa nativa `rows_from`."""
        return self._get(("conv", i, rows_from))

    def mask(self, i, uv, tris):
        img = self.emb[i][1]
        m = raster_uv_mask(img.shape[:2], uv, tris)
        self.masks[i] = self.masks.get(i, False) | m

    def build(self, cap=None, hand_cap=None):
        """#AZT de salida. cap/hand_cap permiten reintentar con menos resolucion sin cambiar el
        orden de `keys` (los materiales ya referencian esos indices)."""
        cap = self.cap if cap is None else cap
        items, info = [], []
        for kind, i, *extra in self.keys:
            if kind == "tpl":
                items.append(self.tpl.textures[i])
                info.append(("rampa plantilla %d" % i,) + tuple(self.tpl.textures[i][:2]))
                continue
            if kind == "env":       # alfa 0: la rampa de ropa lo sombrea, como los F5 nativos
                rgba = self.emb[i][1].copy()
                rgba[..., :3] = np.clip(np.rint(rgba[..., :3] * (np.array(extra[0], np.float32) / 255.0)), 0, 255)
                rgba[..., 3] = 0
                h, w = rgba.shape[:2]
                # 128 px: el reflejo no necesita mas y no se pasa del presupuesto del AZT (Xeno Janemba: 642 KB
                # con 256 px, por encima de los 610 de los nativos; 592 KB con 128)
                s = min(1.0, min(cap, 128) / max(w, h))
                if s < 1.0:
                    w, h = int(w * s), int(h * s)
                    rgba = resize_rgba(rgba, w, h)
                    rgba[..., 3] = 0
                items.append((w, h, 0x21, dds_dxt3(w, h) + encode_dxt3(rgba)))
                info.append(("reflejo SDBH %d" % i, w, h))
                continue
            if kind == "conv":
                from PIL import Image
                nat = np.array(Image.open(io.BytesIO(self.tpl.textures[extra[0]][3])).convert("RGBA"))
                src = resize_rgba(self.emb[i][1], 64, 64)
                ramp = nat.copy()
                ramp[:56, :, :3] = 255 - src[0, :, :3][None]
                ramp[..., 3] = 255
                items.append((64, 64, 0x80000001, dds_dxt3(64, 64) + encode_dxt3(ramp)))
                info.append(("rampa SDBH %d convertida" % i, 64, 64))
                continue
            name, img, raw = self.emb[i]
            h, w = img.shape[:2]
            alpha = np.full((h, w), 255 if i in self.unlit else 0, np.uint8)
            if i in self.masks and i not in self.unlit:
                alpha[self.masks[i]] = 255
            own = self.caps.get(i)
            lim = hand_cap if (own is not None and hand_cap is not None) else (own if own is not None else cap)
            s = min(1.0, lim / max(w, h))
            if s == 1.0 and raw[84:88] == b"DXT1":
                bm = dxt1_to_dxt3(raw, alpha)
            else:
                rgba = img.copy()
                rgba[..., 3] = alpha
                if s < 1.0:
                    w, h = int(w * s), int(h * s)
                    rgba = resize_rgba(rgba, w, h)
                    rgba[..., 3] = np.where(rgba[..., 3] > 127, 255, 0)
                bm = encode_dxt3(rgba)
            items.append((w, h, 0x21, dds_dxt3(w, h) + bm))
            info.append((name, w, h))
        return build_azt(items), info


def _merge(parts):
    """Junta [(ventanas, tris, pts, nmax)] en una sola parte."""
    win, tris, pts, nmax = [], [], [], 0
    for w, t, p, n in parts:
        tris += [(a + len(win), b + len(win), c + len(win)) for a, b, c in t]
        win += w
        pts.append(p)
        nmax = max(nmax, n)
    return win, tris, np.concatenate(pts) if pts else np.zeros((0, 3)), nmax


def convert(folder, template, out, belt_deg=80.0, cap=256, hand_tris=300, hand_cap=128, hands=None,
            report=None, ramps=None, hair=None, face_base=None, hand_pose=True, auto_cap=True,
            azt_budget=600000):
    """Carpeta SDBH (bcXXXbNN) + plantilla HD -> bin #AMB HD descomprimido. Devuelve un informe.
    template: bin nativo del que se toman esqueleto, etiquetas y paleta (la forma del donante:
    p.ej. 225 normal, 226 SSJ, 227 definitivo). ramps: bin nativo del que se copian las rampas de
    piel y ropa (por defecto el mismo; p.ej. 228/229, Gohan adulto con el traje naranja).
    hair: preset de HAIR_PRESETS para derivar un pelo (SSJ2 desde el SSJ). face_base: numero de
    expresion de SDBH que hace de cara por defecto (L00 y su copia L09).
    Presupuesto (nativos: bin <= 948 KB, AZT <= 610 KB; crecer el AZT corrompe la memoria del
    guest): las manos (una malla por variante) se reducen a hand_tris triangulos y su textura a
    hand_cap; el resto de texturas a `cap`. Con auto_cap, si el AZT pasa de azt_budget se reintenta
    con menos resolucion (224/192/160/128) en vez de entregar un AZT que corrompe al guest.
    hand_pose=False hornea la mano de SDBH TAL CUAL (su reposo), sin las posturas manuales
    HAND_POSES: se une bien a la muneca y no deforma el pulgar, pero todas las variantes usan L00."""
    src = load_source(folder)
    drop_broken(src)
    tpl = Template(template)
    rtpl = Template(ramps) if ramps else tpl
    esk, emb = src["esk"], src["emb"]
    rig = Rig(tpl, esk, belt_deg)
    slots = {b: s for s, b in enumerate(tpl.palette) if s > 0}
    tx = Textures(emb, rtpl, cap)
    # el cuerpo de Trunks nino y de Omega Shenron se llama XTRS_BODY_1 / 1SD_BODY_1: antes sin rampa de ropa y
    # cualquier modelo de Heroes con esos donantes fallaba (TypeError en build); sin ninguna, la mas usada
    any_ramp = rtpl.ramp_of(lambda lab: True)
    ramp_skin = rtpl.ramp_of(lambda lab: "HAND" in lab)
    ramp_cloth = rtpl.ramp_of(lambda lab: re.fullmatch(r"BODY(_\d+)?", bone_suffix(lab)) is not None)
    ramp_skin = any_ramp if ramp_skin is None else ramp_skin
    ramp_cloth = any_ramp if ramp_cloth is None else ramp_cloth
    if hair:
        c = esk.world[esk.index["head"]][:3, 3] + np.array([0.0, 1.1, 0.0])
        for key, subs in src["parts"].items():
            if key.endswith("body"):
                src["parts"][key] = [spikier_hair(sm, c, *HAIR_PRESETS[hair]) if sm.name.lower().startswith("hair")
                                     else sm for sm in subs]
    if face_base is not None:
        pat = re.compile(r"L(\d\d)(?:_s\d\d)?_face$")
        faces = {int(m.group(1)): k for k in src["parts"] for m in [pat.search(k)] if m}
        for num in (0, 9):
            if num in faces and face_base in faces:
                src["parts"][faces[num]] = src["parts"][faces[face_base]]

    skin_sd = next((sm.texdefs[1] for k in ("L_hand", "R_hand") for sm in src["parts"].get(k, [])
                    if len(sm.texdefs) > 1), None)

    def ramp_for(r):
        """Rampa de B3 para la rampa SDBH r: piel -> la nativa de piel de la plantilla (la forma
        decide: piel normal o la palida del SSJ); otra con color (pelo rubio, base blanca) -> la de
        SDBH convertida; gris (ropa y pelo negro sobre base de color) -> la nativa de ropa."""
        if r is None or r == skin_sd:
            return tx.ramp(ramp_skin)
        if colored(emb[r][1]):
            return tx.conv_ramp(r, ramp_skin)
        return tx.ramp(ramp_cloth)

    def mat_for(sm, mats, lit=None, ramp_sd=-1):
        if getattr(sm, "tpl_tex", None) is not None:   # K: accesorio del donante: su textura y su rampa NATIVAS, copiadas tal cual
            m = material(True, tx.ramp(sm.tpl_tex), tx.ramp(sm.tpl_ramp))
            if m not in mats:
                mats.append(m)
            return mats.index(m)
        base = sm.texdefs[0]
        lit = len(sm.texdefs) > 1 if lit is None else lit
        if len(sm.texdefs) >= 3:
            # ToonEnvironment_W (las unicas submallas de SDBH con 3 texturas: base, rampa, reflejo) -> F5 con
            # el reflejo como textura (antes: B5 con la base, casi siempre blanca: cupula de Freezer blanca)
            m = material(True, tx.env(sm.texdefs[2], uv_tint(emb[base][1], sm.uv)), tx.ramp(ramp_cloth), env=True)
        elif lit:
            r = ramp_sd if ramp_sd != -1 else (sm.texdefs[1] if len(sm.texdefs) > 1 else None)
            m = material(True, tx.sdbh(base), ramp_for(r))
        else:
            tx.unlit.add(base)
            m = material(False, tx.sdbh(base), 0, 0.9)
        if m not in mats:
            mats.append(m)
        return mats.index(m)

    def rigid_parts(subs, M, mats):
        """Submallas rigidas (cara, dientes, manos) en el marco M; los ojos (*Eye_cons*, sin rampa
        en SDBH) van con el material de la cara y alfa 255 (sin sombra, como los nativos)."""
        by = {}
        lit_face = next((s for s in subs if len(s.texdefs) > 1), None)
        for sm in subs:
            eye = "eye" in sm.name.lower() and lit_face is not None
            lit = True if eye else len(sm.texdefs) > 1
            mi = mat_for(sm, mats, lit, lit_face.texdefs[1] if eye else -1)
            if eye:
                tx.mask(sm.texdefs[0], sm.uv, sm.tris)
            win, P = windows_rigid(M, sm.pos, sm.nrm, sm.uv, lit)
            by.setdefault(mi, []).append((win, [tuple(t) for t in sm.tris], P, 0))
        return {mi: _merge(v) for mi, v in by.items()}

    parts = src["parts"]
    face_b = next(i for i, l in enumerate(tpl.labels) if re.fullmatch(r"L00_.*FACE", bone_suffix(l)))
    hand_b = {"L": tpl.bone("L00_LHAND"), "R": tpl.bone("L00_RHAND")}
    if hands is None:
        dec = {side: [decimate(sm, hand_tris) if hand_tris and len(sm.tris) > hand_tris else sm
                      for sm in src["parts"].get("%s_hand" % side, [])] for side in "LR"}
        hands = posed_hands(src, dec) if hand_pose else {side: {0: dec[side]} for side in "LR" if dec[side]}
        for side in "LR":
            for sm in src["parts"].get("%s_hand" % side, []):
                tx.caps[sm.texdefs[0]] = hand_cap

    # ---------------- AWG0
    mats0, groups0 = [], {}
    body = next(v for k, v in parts.items() if k.endswith("body"))
    smooth_seams(body)
    by = {}
    for sm in body:
        lit = len(sm.texdefs) > 1
        mi = mat_for(sm, mats0)
        win, P, nmax = windows_skinned(rig, sm, slots, lit)
        by.setdefault(mi, []).append((win, [tuple(t) for t in sm.tris], P, nmax))
    groups0[0] = []
    for mi, v in by.items():
        win, tris, P, nmax = _merge(v)
        groups0[0].append({"skinned": True, "mat": mi, "win": win, "tris": tris, "pts": P,
                           "label": tpl.labels[0], "nmax": nmax})

    def add_rigid(groups, bone, subs, mats, M):
        for mi, (win, tris, P, _) in rigid_parts(subs, M, mats).items():
            groups.setdefault(bone, []).append({"skinned": False, "mat": mi, "win": win, "tris": tris,
                                                "pts": P, "label": tpl.labels[bone]})
    inv = [np.linalg.inv(w) for w in rig.W]
    variants = []                          # (etiqueta, tipo, hueso L00, [submallas])
    for key, subs in parts.items():
        m = re.search(r"L(\d\d)(?:_s\d\d)?_face$", key)
        if m:
            if m.group(1) == "00":
                add_rigid(groups0, face_b, subs, mats0, inv[face_b])
            else:
                variants.append((int(m.group(1)), "FACE", face_b, subs))
            continue
        m = re.fullmatch(r"rigidlocal_(\w+)", key)      # K: accesorio rigido del donante (espada de Trunks...), ya en el marco del hueso
        if m:
            b = tpl.bone(m.group(1))
            if b is not None:
                add_rigid(groups0, b, subs, mats0, np.eye(4))
            continue
        m = re.search(r"M_(\w+)$", key)
        if m:
            b = tpl.bone("M_" + m.group(1).upper())
            if b is not None:
                add_rigid(groups0, b, subs, mats0, inv[b])
            continue
        m = re.fullmatch(r"([LR])_hand", key)
        if m:
            side = m.group(1)
            poses = hands.get(side) or {0: subs}
            for num, ss in sorted(poses.items()):
                if num == 0:
                    add_rigid(groups0, hand_b[side], ss, mats0, inv[hand_b[side]])
                else:
                    variants.append((num, "HAND", hand_b[side], ss))
    for key, subs in parts.items():     # K: variante de mano del donante con accesorio (hoja de la espada de Trunks, L22...)
        m = re.fullmatch(r"rigidvar_(\d+)_([LR])", key)
        if m:
            side = m.group(2)
            base = (hands.get(side) or {0: parts.get("%s_hand" % side, [])}).get(0, [])
            variants.append((int(m.group(1)), "HAND", hand_b[side], list(base) + list(subs)))
    awg0 = awg_bytes(tpl.names, rig.axes(), tpl.links, tpl.word0c, 0, groups0, mats0, tpl.palette)
    awgs = [awg0[:2]]
    stats = {"awg0": awg0[2], "aux": []}

    # ---------------- variantes (AWG auxiliares de 1 hueso)
    order = {"HAND": 0, "FACE": 1}
    variants.sort(key=lambda v: (order[v[1]], v[2] != hand_b["L"], v[0]))
    var_of = {}                            # hueso L00 -> {numero: indice de AWG}
    for num, kind, bone, subs in variants:
        label = tpl.labels[bone].replace("L00_", "L%02d_" % num, 1)
        mats, groups = [], {}
        add_rigid(groups, 0, [s_ for s_ in subs if not getattr(s_, "local", False)], mats, inv[bone])
        add_rigid(groups, 0, [s_ for s_ in subs if getattr(s_, "local", False)], mats, np.eye(4))   # K: ya en el marco de la mano
        groups[0] = [dict(p, label=label) for p in groups[0]]
        k = len(awgs)
        raw, ax_off, st = awg_bytes(label.encode("latin1").ljust(32, b"\0"), [tpl.aux_axis[kind]],
                                    [(-1, -1, -1)], tpl.word0c, tpl.nb + k - 1, groups, mats,
                                    [0xFFFFFFFF])
        awgs.append((raw, ax_off))
        var_of.setdefault(bone, {})[num] = k
        stats["aux"].append((label, st["windows"], st["ib"]))
    nlist = max([tpl.nlist] + [n + 1 for n, _, _, _ in variants])
    lists = []
    for i in range(tpl.nb):
        vs = var_of.get(i, {})
        row = []
        for e in range(nlist):
            best = max([n for n in vs if n <= e], default=0)
            row.append((vs[best], tpl.nb + vs[best] - 1, 0) if best else (0, i, i))
        lists.append(row)
    awo = awo_bytes(tpl, awgs, lists, nlist)
    azt, tex_info = tx.build()
    cap_usado = cap
    if auto_cap:
        for c in (cap, 224, 192, 160, 128):
            if c >= cap and len(azt) <= azt_budget:
                break
            if c < cap:
                azt, tex_info = tx.build(cap=c, hand_cap=min(hand_cap, c))
                cap_usado = c
            if len(azt) <= azt_budget:
                break
    data = amb_pack([(awo, tpl.kid_types.get(b"#AWO", 1)), (azt, tpl.kid_types.get(b"#AZT", 2))])
    open(out, "wb").write(data)
    stats.update({"bin": len(data), "awo": len(awo), "azt": len(azt), "textures": tex_info, "cap": cap_usado,
                  "overridden": rig.overridden, "belt": rig.belt, "variants": len(variants)})
    print("OK -> %s: %d B (AWO %d, AZT %d) | AWG0 %d ventanas, %d indices, %d draws | %d variantes"
          % (out, len(data), len(awo), len(azt), stats["awg0"]["windows"], stats["awg0"]["ib"],
             stats["awg0"]["draws"], len(variants)))
    errs = verify(out)
    stats["verificacion"] = errs
    print("verificacion: %s" % ("OK" if not errs else "; ".join(errs[:5])))
    return stats
    if report:
        import json
        json.dump(stats, open(report, "w"), indent=1, default=str)
    return stats


def verify(path):
    """Comprobacion estructural de un bin escrito: listas por hueso del AWO (cada entrada apunta al
    eje del AWG que dice; las variantes con la de numero <= la pedida, como los nativos), rangos de
    ventanas/IB de los draws de TODOS los AWG, paleta (slot 0 = FFFFFFFF) y cadenas de fisica del
    cinturon (LOBI1 -> 2 -> 3 -> 4 por primer hijo). Devuelve la lista de errores."""
    data = open(path, "rb").read()
    a = next(o for o, s, t, _ in amb_kids(data) if data[o:o + 4] == b"#AWO")
    h = struct.unpack_from(">4s11I", data, a)
    nb, bones, namg, tbl, nlist, names = h[4], h[5], h[6], h[7], h[8], h[9]
    awgs = [_be(data, a + tbl + 4 * k) for k in range(namg)]
    errs = []
    labels = [data[a + names + 32 * i:a + names + 32 * i + 32].split(b"\0")[0].decode("latin1") for i in range(nb)]
    aux_label = {}
    for k, g in enumerate(awgs):
        hd = struct.unpack_from(">4s15I", data, a + g)
        G = a + g
        n_win, n_ib = hd[11] // 44, hd[13] // 2
        ib = struct.unpack_from(">%dH" % n_ib, data, G + hd[12]) if n_ib else ()
        pal = struct.unpack_from(">%dI" % hd[15], data, G + hd[14]) if hd[15] else ()
        if pal and pal[0] != 0xFFFFFFFF:
            errs.append("AWG %d: paleta sin FFFFFFFF en el slot 0" % k)
        if hd[14] and hd[14] != (hd[12] + 2 * n_ib + 3) // 4 * 4:
            errs.append("AWG %d: la paleta no va pegada a la IB" % k)
        if k:
            aux_label[k] = data[G + hd[7]:G + hd[7] + 32].split(b"\0")[0].decode("latin1")
        ax = G + hd[5]
        ends = []
        for i in range(hd[4]):
            arm = _be(data, ax + 80 * i + 0x34)
            grp = _be(data, G + arm + 4)
            for d in range(_be(data, G + grp) if grp else 0):
                r = G + grp + 16 + 0x60 * d
                prim, _, a0, an, b0, bn = struct.unpack_from(">6I", data, r + 0x20)
                end = b0 + (bn + 2 if prim == 5 and bn else 3 * bn)
                ends.append((b0, end))
                if a0 + an > n_win or end > n_ib or any(not a0 <= x < a0 + an for x in ib[b0:end]):
                    errs.append("AWG %d draw %d: rango A/B fuera de sitio" % (k, d))
        ends.sort()
        if any(e0[1] > e1[0] for e0, e1 in zip(ends, ends[1:])):
            errs.append("AWG %d: un draw lee la IB del siguiente" % k)
    g0 = a + awgs[0]
    ax0 = g0 + _be(data, g0 + 0x14)
    for i in range(nb):
        lp = a + _be(data, a + bones + 0x20 * i + 4)
        for e in range(nlist):
            k, gid, axp, _ = struct.unpack_from(">4I", data, lp + 16 * e)
            want = awgs[k] + _be(data, a + awgs[k] + 0x14) + (80 * i if k == 0 else 0)
            if axp != want or gid != (i if k == 0 else nb + k - 1):
                errs.append("lista del hueso %s, entrada %d: (%d, %d, %#x)" % (labels[i], e, k, gid, axp))
                break
            if k:
                m = re.search(r"_L(\d\d)_", aux_label[k])
                if not m or int(m.group(1)) > e or bone_suffix(labels[i])[3:] not in aux_label[k]:
                    errs.append("lista del hueso %s, entrada %d -> variante %s" % (labels[i], e, aux_label[k]))
                    break
    for side in "LR":
        chain = []
        i = next((j for j, l in enumerate(labels) if bone_suffix(l) == "%sOBI1" % side), None)
        while i is not None and len(chain) < 8:
            chain.append(bone_suffix(labels[i]))
            c = _be(data, ax0 + 80 * i + 0x38)
            i = (c - (ax0 - g0)) // 80 if c else None
        if chain and chain[:4] != ["%sOBI%d" % (side, n) for n in range(1, 5)][:len(chain)]:
            errs.append("cadena de %sOBI1 por primer hijo: %s" % (side, chain))
    return errs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd")
    c = sub.add_parser("convertir")
    c.add_argument("carpeta")
    c.add_argument("plantilla")
    c.add_argument("salida")
    c.add_argument("--cinturon", type=float, default=80.0, help="giro (grados) de las colas del cinturon")
    c.add_argument("--tope", type=int, default=256, help="lado maximo de las texturas")
    c.add_argument("--manos-tris", type=int, default=300, help="triangulos por mano (0 = sin reducir)")
    c.add_argument("--tope-manos", type=int, default=128, help="lado maximo de la textura de las manos")
    c.add_argument("--informe")
    c.add_argument("--rampas", help="bin HD nativo del que copiar las rampas de piel y ropa")
    c.add_argument("--pelo", choices=sorted(HAIR_PRESETS), help="deriva el pelo (ssj2 = SSJ2 desde el SSJ)")
    c.add_argument("--cara-base", type=int, help="expresion de SDBH que hace de cara por defecto (p.ej. 5)")
    c.add_argument("--manos", choices=("pose", "reposo"), default="pose",
                   help="reposo = mano de SDBH tal cual (sin las posturas manuales; se une mejor a la muneca)")
    c.add_argument("--presupuesto-azt", type=int, default=600000,
                   help="tamano maximo del #AZT (los nativos llegan a 610 KB; crecerlo corrompe la memoria)")
    c.add_argument("--sin-tope-auto", action="store_true",
                   help="no reducir la resolucion si el #AZT pasa del presupuesto")
    i = sub.add_parser("info")
    i.add_argument("carpeta")
    f = sub.add_parser("ajustar-pulgares", help="reajusta el pulgar de HAND_POSES contra las manos nativas")
    f.add_argument("carpeta")
    f.add_argument("plantilla")
    f.add_argument("--extra", help="otro bin HD con variantes que la plantilla no tenga (L06 de AR)")
    v = sub.add_parser("verificar", help="comprobacion estructural de bins HD (listas, AWG, cadenas)")
    v.add_argument("bins", nargs="+")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if a.cmd == "info":
        cmd_info(a.carpeta)
    elif a.cmd == "verificar":
        bad = 0
        for p in a.bins:
            errs = verify(p)
            bad += bool(errs)
            print("%s: %s" % (os.path.basename(p), "OK" if not errs else "; ".join(errs[:8])))
        return 1 if bad else 0
    elif a.cmd == "ajustar-pulgares":
        cmd_fit_thumbs(a.carpeta, a.plantilla, a.extra)
    elif a.cmd == "convertir":
        convert(a.carpeta, a.plantilla, a.salida, a.cinturon, a.tope, a.manos_tris, a.tope_manos,
                report=a.informe, ramps=a.rampas, hair=a.pelo, face_base=a.cara_base,
                hand_pose=a.manos != "reposo", auto_cap=not a.sin_tope_auto, azt_budget=a.presupuesto_azt)
    else:
        ap.print_help()
    return 0


def selftest():
    """Transcodificacion DXT1 -> DXT3 sin perdida, alfa DXT3 y cuaterniones."""
    rng = np.random.default_rng(1)
    for _ in range(20):
        R = qmat(rng.normal(size=4) / 2)
        q = rng.normal(size=4)
        q /= np.linalg.norm(q)
        assert np.allclose(qmat(mat_q(qmat(q))), qmat(q), atol=1e-9)
    a = np.zeros((8, 8), np.uint8)
    a[0, 1] = 255
    blk = alpha4(a)
    assert blk.shape == (4, 8) and blk[0, 0] == 0xF0 and blk[1:].sum() == 0
    # DDS DXT1 4x4 en modo 4 colores: debe copiarse tal cual detras del alfa
    dds = bytearray(128)
    dds[0:4] = b"DDS "
    struct.pack_into("<II", dds, 12, 4, 4)
    dds[84:88] = b"DXT1"
    blk1 = struct.pack("<HHI", 0xF800, 0x001F, 0x1B1B1B1B)
    out = dxt1_to_dxt3(bytes(dds) + blk1, np.zeros((4, 4), np.uint8))
    assert out == bytes(8) + blk1, out.hex()
    # reflejo: ToonEnvironment_W -> vtype F5 (el normal B5) y el tinte de la base bajo sus uv
    assert struct.unpack_from(">I", material(True, 1, 2, env=True), 0x38)[0] == 0x1F5
    assert struct.unpack_from(">I", material(True, 1, 2), 0x38)[0] == 0x1B5
    img = np.zeros((4, 4, 4), np.uint8)
    img[..., 0] = 255
    assert uv_tint(img, np.array([[0.1, 0.2], [1.6, -0.3]])) == (255, 0, 0)
    print("selftest OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

