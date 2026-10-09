#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""psp_amo.py - Modelos de Shin Budokai (PSP, #AMB v3 LE) -> bin HD de Budokai 3.

RE 2026-10-04 sobre BCGHFB00.amb (Gohan del Futuro, Another Road). El #AMO de PSP tiene la
misma cabecera, esqueleto, AMGs, ejes (80 B) y "arm" por hueso que el de PS2 (amo2awo.py);
cambia lo de dentro:

  arm [i, grupo, copia, piel]  (PS2: [i, grupo, piel, caja])
    grupo / copia: [n, 0x10, 0, 0, offs[n]] -> partes; la copia es identica (doble buffer:
    la piel de PSP escribe en ella) y es la que se usa aqui.
  parte: cabecera 0x40 (+0x2C = textura) y paquetes de display list del GE:
    [u32, u32, VTYPE 0x12000182, PRIM 0x04 04 nnnn] + nnnn vertices de 16 B
    (u16 u, u16 v /32768, f32 x, y, z); PRIM 4 = tira de triangulos. Sin normales.
  piel (por hueso): [nconj, ptr conj...]; conjunto [peso f32, n, base, ptr entrada x n];
    entrada [pos xyz 1][normal xyz, k][k refs]: base + ref = direccion del xyz de un vertice
    de la copia (relativo al AMG). Mismo concepto que la piel de PS2 (amo2awo.PS2AMG.skin).
  #AMT: como PS2 (amt_ps2.entries) pero psm 4 = indices de 4 bits del GE con la imagen
    "swizzled" (bloques de 16 B x 8 filas) y paleta RGBA8888 (alfa 0..255).

Uso:  python psp_amo.py <BC....amb de PSP> <salida.bin HD>
"""
import struct
import sys

import numpy as np

import amo2awo as A
import amt_ps2

# material de una parte HD (copiado de los nativos: Gohan adulto, data_cmn 225). El campo
# "shader" (+0x34 del material HD) es la textura RAMPA toon: los nativos llevan texturas casi
# blancas y el color sale de la rampa. Las de PSP ya traen el color, asi que se anade una
# rampa NEUTRA (psp_ramp.npy: la rampa de tela de Gohan en grises, tono medio = blanco) como
# ultima textura del #AZT; RAMP_SLOT la fija convert_model antes de convertir el #AMO.
MAT_VTYPE, MAT_VFLAGS = 0x1B5, 0x29BD
RAMP_SLOT = None
MAT_REGS = {0x46: 0x1, 0x08: 0x5, 0x09: 0x5, 0x42: 0x44, 0x43: 0x44}
ONE4 = struct.pack("<4f", 1, 1, 1, 1)


def le(b, o):
    return struct.unpack_from("<I", b, o)[0]


class PSPAMG:
    """Mismo interfaz que amo2awo.PS2AMG (lo que usa amo2awo.build_awg)."""

    def __init__(self, b, a):
        self.b, self.a = b, a
        if b[a:a + 4] != b"#AMG":
            raise A.ConvError("AMG sin magic en 0x%x" % a)
        self.word0c = le(b, a + 0xC)
        self.nb = le(b, a + 0x10)
        self.axes_rel = le(b, a + 0x14)
        self.ntex = le(b, a + 0x18) + (1 if RAMP_SLOT is not None else 0)
        self.names_rel = le(b, a + 0x1C)
        raw_arms = []
        for i in range(self.nb):
            arm = le(b, a + self.axes_rel + 80 * i + 0x34)
            raw_arms.append((arm, struct.unpack_from("<4I", b, a + arm) if arm else None))
        # para el escritor: [i, grupo, piel (no se usa), caja = 0]
        self.arms = [(arm, (w[0], w[2] or w[1], 0, 0) if w else None) for arm, w in raw_arms]
        self.parts, self.vert_of, self.groups = [], {}, []
        for i, (arm, w) in enumerate(raw_arms):
            if not w or not (w[2] or w[1]):
                continue
            g = a + (w[2] or w[1])
            n, tbl = le(b, g), le(b, g + 4)
            idx = []
            for k in range(n):
                idx.append(len(self.parts))
                self.parts.append(self._part(i, g + le(b, g + tbl + 4 * k)))
            self.groups.append((i, idx))
        self.skin = []
        for i, (arm, w) in enumerate(raw_arms):
            if not w or not w[3]:
                continue
            s = a + w[3]
            for c in range(le(b, s)):
                st = a + le(b, s + 4 + 4 * c)
                wt = struct.unpack_from("<f", b, st)[0]
                cnt, base = le(b, st + 4), le(b, st + 8)
                for k in range(cnt):
                    e = a + le(b, st + 12 + 4 * k)
                    pos = list(struct.unpack_from("<3f", b, e))
                    nrm = list(struct.unpack_from("<3f", b, e + 16))
                    for r in range(le(b, e + 28)):
                        xyz = base + le(b, e + 32 + 4 * r)
                        self.skin.append((i, wt, pos, nrm, xyz))
        # partes rigidas (sin piel): normales calculadas de la propia malla
        skinned = {s[4] for s in self.skin}
        for p in self.parts:
            if p.verts and p.verts[0][2] is None and not any(v[0] in skinned for v in p.verts):
                _smooth_normals(p)

    def _part(self, bone, po):
        b, a = self.b, self.a
        p = A.Part()
        p.bone = bone
        p.tex = le(b, po + 0x2C)
        p.vtype, p.vflags = MAT_VTYPE, MAT_VFLAGS
        p.shader = RAMP_SLOT if RAMP_SLOT is not None else 0xFFFFFFFF
        p.regs = dict(MAT_REGS)
        p.hdr = bytes(0x20) + ONE4 + ONE4 + bytes(0x60)
        p.strips, p.verts = [], []
        pi = len(self.parts)
        x = po + 0x40
        while x + 16 <= len(b) and (le(b, x + 8) >> 24) == 0x12 and (le(b, x + 12) >> 24) == 0x04:
            vt, prim = le(b, x + 8) & 0xFFFFFF, le(b, x + 12)
            n, ptype = prim & 0xFFFF, (prim >> 16) & 7
            fmt = ge_vertex(vt)
            strip = []
            for k in range(n):
                o = x + 16 + fmt["size"] * k
                uv, col, nrm, xyz = fmt["read"](b, o)
                self.vert_of[o + fmt["pos_off"] - a] = (pi, len(p.verts))
                strip.append(len(p.verts))
                p.verts.append((o + fmt["pos_off"] - a, xyz, nrm, uv, col))
            p.strips.append((1 if ptype == 4 else 0, strip))
            x += 16 + fmt["size"] * n
            x = (x + 15) // 16 * 16
        pts = np.array([v[1] for v in p.verts] or [[0, 0, 0]], np.float64)
        c = (pts.min(0) + pts.max(0)) / 2
        p.center = [float(t) for t in c]
        p.radius = float(np.sqrt(((pts - c) ** 2).sum(1)).max())
        return p


# vertice del GE de PSP: textura, color, normal y posicion, cada uno alineado a su tamano
_TEX = {0: (0, None), 1: (1, "B"), 2: (2, "H"), 3: (4, "f")}
_NRM = {0: (0, None), 1: (1, "b"), 2: (2, "h"), 3: (4, "f")}
_COL = {0: 0, 4: 2, 5: 2, 6: 2, 7: 4}


def ge_vertex(vt):
    if vt & 0x7E00 or (vt >> 7) & 3 == 0:
        raise A.ConvError("VTYPE de PSP no soportado %#x (pesos/indices/sin posicion)" % vt)
    t, c, nr, ps = vt & 3, (vt >> 2) & 7, (vt >> 5) & 3, (vt >> 7) & 3
    lay, off, big = [], 0, 1
    for kind, (sz, ch, cnt) in (("t", _TEX[t] + (2,)), ("c", (_COL[c], "c", 1)),
                                 ("n", _NRM[nr] + (3,)), ("p", _NRM[ps] + (3,))):
        if not sz:
            continue
        off = (off + sz - 1) // sz * sz
        lay.append((kind, off, sz, ch, cnt))
        off += sz * cnt
        big = max(big, sz)
    size = (off + big - 1) // big * big
    scale = {"B": 128.0, "H": 32768.0, "f": 1.0, "b": 127.0, "h": 32767.0}

    def read(b, o):
        uv = col = nrm = xyz = None
        for kind, ko, sz, ch, cnt in lay:
            if kind == "c":
                if sz == 4:
                    r, g, bl, al = b[o + ko:o + ko + 4]
                    col = [r / 255.0, g / 255.0, bl / 255.0, al / 255.0]
                continue
            v = [x / scale[ch] for x in struct.unpack_from("<%d%s" % (cnt, ch), b, o + ko)]
            if kind == "t":
                uv = v
            elif kind == "n":
                nrm = v
            else:
                xyz = v
        return uv or [0.0, 0.0], col, nrm, xyz

    pos_off = next(ko for kind, ko, *_ in lay if kind == "p")
    return {"size": size, "read": read, "pos_off": pos_off}


def _smooth_normals(p):
    pts = np.array([v[1] for v in p.verts], np.float64)
    acc = np.zeros_like(pts)
    for t in A.tris_of(p.strips):
        n = np.cross(pts[t[1]] - pts[t[0]], pts[t[2]] - pts[t[0]])
        for i in t:
            acc[i] += n
    # vertices con la misma posicion comparten normal (costuras de uv)
    key = {}
    for i, q in enumerate(pts):
        key.setdefault(tuple(np.round(q, 4)), []).append(i)
    for ids in key.values():
        s = acc[ids].sum(0)
        for i in ids:
            acc[i] = s
    ln = np.linalg.norm(acc, axis=1)
    ln[ln == 0] = 1
    acc /= ln[:, None]
    p.verts = [(o, xyz, [float(t) for t in acc[i]], uv, col) for i, (o, xyz, _, uv, col) in enumerate(p.verts)]


class PSPAMO:
    def __init__(self, b):
        if b[:4] != b"#AMO":
            raise A.ConvError("no es #AMO0")
        self.b = b
        self.nb, self.bones, self.namg, self.amg_tbl, self.nlist, self.names = \
            struct.unpack_from("<6I", b, 0x10)
        self.amg_off = [le(b, self.amg_tbl + 4 * i) for i in range(self.namg)]
        self.amgs = [PSPAMG(b, o) for o in self.amg_off]


def convert_amo(b):
    """#AMO de PSP -> #AWO HD (con el escritor de amo2awo)."""
    old = A.PS2AMO
    A.PS2AMO = PSPAMO
    try:
        return A.convert_amo(bytes(b))
    finally:
        A.PS2AMO = old


# ---------------------------------------------------------------- texturas
def unswizzle(buf, width_bytes, h):
    out = bytearray(len(buf))
    i = 0
    for by in range(h // 8):
        for bx in range(width_bytes // 16):
            for r in range(8):
                o = (by * 8 + r) * width_bytes + bx * 16
                out[o:o + 16] = buf[i:i + 16]
                i += 16
    return bytes(out)


def decode_tex(amt, t):
    w, h = t["w"], t["h"]
    if t["psm"] == 4:                 # indices de 4 bits
        raw = unswizzle(amt[t["data"]:t["data"] + w * h // 2], w // 2, h)
        a = np.frombuffer(raw, np.uint8)
        idx = np.empty(w * h, np.uint8)
        idx[0::2], idx[1::2] = a & 15, a >> 4
    elif t["psm"] == 5:               # indices de 8 bits
        idx = np.frombuffer(unswizzle(amt[t["data"]:t["data"] + w * h], w, h), np.uint8)
    elif t["psm"] == 3:               # RGBA 8888 directo (Bardock de Another Road)
        raw = unswizzle(amt[t["data"]:t["data"] + w * h * 4], w * 4, h)
        return np.frombuffer(raw, np.uint8).reshape(h, w, 4).copy()
    else:
        raise A.ConvError("textura de PSP psm=%d no soportada" % t["psm"])
    clut = np.frombuffer(bytes(amt[t["clut"]:t["clut"] + t["csize"]]), np.uint8).reshape(-1, 4)
    return clut[idx].reshape(h, w, 4).copy()


def ramp_image():
    import os  # noqa: PLC0415
    return np.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), "psp_ramp.npy"))


def amt_slots(amt):
    return struct.unpack("<I", amt[0x10:0x14])[0]


def convert_amt(amt, ramp=True):
    """#AMT de PSP -> #AZT HD; con ramp, la rampa neutra va en el hueco n (el siguiente)."""
    import os  # noqa: PLC0415
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mod center hd"))
    from texture_b3 import encode_dxt3  # noqa: PLC0415
    import zlib  # noqa: PLC0415
    n, idx = struct.unpack("<II", amt[0x10:0x18])
    ts = amt_ps2.entries(amt, keep_empty=True)
    items = [(t["idx"], t["flags"], t["clut"], t["csize"], decode_tex(amt, t)) if t else None for t in ts]
    if ramp:
        items.append((n, items[0][1] if items and items[0] else 0, 0, 0, ramp_image()))
    n2 = len(items)
    m = sum(1 for t in items if t)
    first = idx + 4 * n2
    data_base = (first + 0x30 * m + 0x1F) // 0x20 * 0x20
    hdr = bytearray(struct.pack(">4s7I", b"#AZT", 0, 0, 0, n2, idx, 0, 0))
    hdr += bytes(idx - len(hdr))
    index, ent, body = bytearray(), bytearray(), bytearray()
    for t in items:
        if t is None:
            index += bytes(4)
            continue
        tid, flags, clut, csize, img = t
        from texture_b3 import pad4  # noqa: PLC0415
        img = pad4(img)
        w, h = img.shape[1], img.shape[0]
        blob = amt_ps2.dds_dxt3_header(w, h) + encode_dxt3(img)
        lw, lh = (w - 1).bit_length(), (h - 1).bit_length()
        index += struct.pack(">I", first + len(ent))
        ent += struct.pack(">3I2H2H7I", tid, flags, (w << 16) | h, lw, lh, w, h,
                           data_base + len(body), len(blob), 1, 0, clut, csize, 0)
        body += blob
    out = hdr + index + ent
    out += bytes(data_base - len(out))
    out += body
    struct.pack_into(">I", out, 0x1C, zlib.crc32(bytes(body)))
    return bytes(out)


def convert_model(b):
    """#AMB de modelo PSP (AMO + AMT [+ RPT]) -> #AMB HD (AWO + AZT), como los nativos."""
    import ps2hd  # noqa: PLC0415
    n, tbl = struct.unpack("<II", b[0x10:0x18])
    kids = [struct.unpack("<4I", b[tbl + 16 * k:tbl + 16 * k + 16]) for k in range(n)]
    global RAMP_SLOT
    amts = [b[o:o + s] for o, s, _, _ in kids if b[o:o + 4] == b"#AMT"]
    RAMP_SLOT = amt_slots(amts[0]) if amts else None
    parts = []
    try:
        for off, size, typ, _ in kids:
            blk = b[off:off + size]
            if blk[:4] == b"#AMO":
                parts.append((convert_amo(blk), typ))
            elif blk[:4] == b"#AMT":
                parts.append((convert_amt(blk), typ))
    finally:
        RAMP_SLOT = None
    out = bytearray(struct.pack(">4s7I", b"#AMB", 0x20, 0, 2, len(parts), 0x20, 0, 0))
    start = ps2hd.align(0x20 + 16 * len(parts), 32)
    out += bytes(start - len(out))
    ents = []
    for data, typ in parts:
        out += bytes((-len(out)) % 32)
        ents.append((len(out), len(data), typ, 0))
        out += data
    out += bytes((-len(out)) % 32)
    for k, e in enumerate(ents):
        struct.pack_into(">4I", out, 0x20 + 16 * k, *e)
    struct.pack_into(">I", out, 0x18, start)
    return bytes(out)


if __name__ == "__main__":
    src = open(sys.argv[1], "rb").read()
    open(sys.argv[2], "wb").write(convert_model(src))
    print("ok", sys.argv[2])
