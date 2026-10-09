#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""amt_ps2.py - Texturas PS2 #AMT (Budokai 3 / IW) <-> imagenes y conversion a #AZT HD.

#AMT (little-endian): [magic, 0x20, 0, 2, n, 0x20 (indice), 0, 0] + indice n x u32 (offset
de entrada, rel #AMT) + entradas de 0x30:
  +0 idx | +4 flags (0x21 = 4 bpp, 0x80000001 = 8 bpp) | +8 PSM (0x14 PSMT4, 0x13 PSMT8)
  +0xC log2 w u16, log2 h u16 | +0x10 w u16, h u16 | +0x14 off datos | +0x18 tam datos
  +0x1C 1 | +0x20 0 | +0x24 off CLUT | +0x28 tam CLUT | +0x2C ?
  Datos y CLUT: cabecera GIF de 32 B + payload (off/tam apuntan a la cabecera/payload).
  Pixeles lineales (4 bpp: nibble bajo = pixel par). CLUT RGBA8888 con alfa 0..0x80; la de
  8 bpp va en orden CSM1 (en cada bloque de 32 se intercambian las entradas 8-15 y 16-23).
#AZT (HD, big-endian): misma cabecera/indice/entradas (+8 = w,h u16; +0x14 off datos,
  +0x18 tam = 128 + w*h; datos = cabecera DDS 128 B + DXT3), datos contiguos.

  python amt_ps2.py dump <bin_ps2> <carpeta>     # PNG de cada textura
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
GIF_HDR = 0x20   # cada bloque (pixeles / CLUT) va precedido de una cabecera GIF de 32 B


def find_amt(b):
    i = bytes(b).find(b"#AMT")
    if i < 0:
        raise ValueError("sin #AMT")
    return i


def entries(amt, keep_empty=False):
    """Texturas del #AMT. El indice es disperso: offset 0 = hueco (None si keep_empty)."""
    n, idx = struct.unpack("<II", amt[0x10:0x18])
    out = []
    for k in range(n):
        o = struct.unpack("<I", amt[idx + 4 * k:idx + 4 * k + 4])[0]
        if not o:
            if keep_empty:
                out.append(None)
            continue
        e = struct.unpack("<3I2H2H7I", amt[o:o + 0x30])
        out.append({"entry": o, "idx": e[0], "flags": e[1], "psm": e[2], "lw": e[3], "lh": e[4],
                    "w": e[5], "h": e[6], "data": e[7], "dsize": e[8], "clut": e[11],
                    "csize": e[12]})
    return out


def clut_rgba(amt, t):
    n = t["csize"] // 4
    o = t["clut"] + GIF_HDR
    c = np.frombuffer(bytes(amt[o:o + 4 * n]), dtype=np.uint8).reshape(n, 4).copy()
    if n == 256:   # CSM1: intercambiar 8-15 <-> 16-23 en cada bloque de 32
        c = c.reshape(8, 4, 8, 4)
        c = c[:, [0, 2, 1, 3]].reshape(256, 4)
    a = c[:, 3].astype(np.int32) * 2
    c[:, 3] = np.clip(a, 0, 255).astype(np.uint8)
    return c


def decode(amt, t):
    w, h = t["w"], t["h"]
    o = t["data"] + GIF_HDR
    raw = np.frombuffer(bytes(amt[o:o + t["dsize"]]), dtype=np.uint8)
    pal = clut_rgba(amt, t)
    if t["psm"] == 0x14:            # PSMT4
        idx = np.empty(raw.size * 2, dtype=np.uint8)
        idx[0::2] = raw & 0xF
        idx[1::2] = raw >> 4
    else:                           # PSMT8
        idx = raw
    idx = idx[:w * h]
    return pal[idx].reshape(h, w, 4)


def dds_dxt3_header(w, h):
    return (b"DDS \x7c\x00\x00\x00\x07\x10\x08\x00" + struct.pack("<IIIII", h, w, h * w, 0, 0) +
            bytes(44) + b"\x20\x00\x00\x00\x04\x00\x00\x00" + b"DXT3" + bytes(20) +
            struct.pack("<I", 0x1000) + bytes(16))


def to_azt(amt, images=None):
    """#AMT PS2 -> #AZT HD. images: {idx: ndarray RGBA} para sustituir texturas."""
    sys.path.insert(0, os.path.join(HERE, "..", "mod center hd"))
    from texture_b3 import encode_dxt3
    n, idx = struct.unpack("<II", amt[0x10:0x18])
    ts = entries(amt, keep_empty=True)
    m = sum(1 for t in ts if t)
    first = idx + 4 * n          # la HD pone las entradas presentes justo tras el indice
    data_base = (first + 0x30 * m + 0x1F) // 0x20 * 0x20
    hdr = bytearray(struct.pack(">4s7I", b"#AZT", 0, 0, 0, n, idx, 0, 0))
    hdr += bytes(idx - len(hdr))
    index, ent, body = bytearray(), bytearray(), bytearray()
    for k, t in enumerate(ts):
        if t is None:
            index += bytes(4)
            continue
        img = images.get(k) if images else None
        if img is None:
            img = decode(amt, t)
        from texture_b3 import pad4  # noqa: PLC0415
        img = pad4(img)
        w, h = img.shape[1], img.shape[0]
        blob = dds_dxt3_header(w, h) + encode_dxt3(img)
        lw, lh = (w - 1).bit_length(), (h - 1).bit_length()
        index += struct.pack(">I", first + len(ent))
        ent += struct.pack(">3I2H2H7I", t["idx"], t["flags"], (w << 16) | h, lw, lh, w, h,
                           data_base + len(body), len(blob), 1, 0, t["clut"], t["csize"], 0)
        body += blob
    out = hdr + index + ent
    out += bytes(data_base - len(out))
    out += body
    import zlib
    struct.pack_into(">I", out, 0x1C, zlib.crc32(bytes(body)))   # la HD guarda un hash aqui
    return bytes(out)


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "dump":
        from PIL import Image
        b = open(sys.argv[2], "rb").read()
        amt = b[find_amt(b):]
        os.makedirs(sys.argv[3], exist_ok=True)
        for k, t in enumerate(entries(amt)):
            Image.fromarray(decode(amt, t), "RGBA").save(os.path.join(sys.argv[3], "tex%02d.png" % k))
            print(k, t["w"], t["h"], "psm=%#x" % t["psm"])
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())
