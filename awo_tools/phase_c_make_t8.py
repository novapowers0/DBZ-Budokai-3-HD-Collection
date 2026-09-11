#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_c_make_t8.py - Test T8: permutacion de PARTES ENTERAS (identidad).

Motor reutilizable para la Via B: identifica las "partes" del pool (rangos A de
los descriptores 0x60 + rangos de los descriptores de parte de los arms),
intercambia DOS partes de igual numero de vertices (bloques completos),
actualiza sus rangos y remapea el IB. Es una IDENTIDAD GEOMETRICA: si el guest
dibuja por IB+rangos, el render no debe cambiar. Si algo cambia -> hay un
consumidor POSICIONAL oculto.

Uso:
  python phase_c_make_t8.py <entrada.bin> <salida.bin> [--pair A B]
Sin --pair elige automaticamente dos partes de igual count (las mayores).
"""
import re
import struct
import sys

def be32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def be16(b, o): return struct.unpack(">H", b[o:o+2])[0]

def load(path):
    b = bytearray(open(path, "rb").read())
    awo = 0x40
    awg_tbl = awo + be32(b, awo + 0x1C)
    awg0 = awo + be32(b, awg_tbl)
    def g(o): return be32(b, awg0 + o)
    h = {"n_bones": g(0x10), "axes": g(0x14), "mg": g(0x20),
         "vb2": g(0x2C), "ib": g(0x30), "sec": g(0x34), "end": g(0x38),
         "awg0": awg0}
    h["n_sec"] = (h["vb2"] - h["sec"] - 2) // 44
    h["n_vb2"] = (h["ib"] - h["vb2"]) // 44
    h["n_ib"] = (h["end"] - h["ib"]) // 2
    h["n_pool"] = h["n_sec"] + h["n_vb2"]
    return b, h

def collect_parts(b, h):
    """[(site, start, count, label)] donde site = offset AWG0 del campo start."""
    awg0 = h["awg0"]
    parts = []
    # 1) descriptores 0x60 ("max N m"): A_start<<8 en +0x50, A_count<<8 en +0x54
    mg = awg0 + h["mg"]
    for m in re.finditer(rb"max \d+ m", bytes(b[mg:awg0 + h["sec"]])):
        dd = mg + (m.start() - 0x18)
        if be32(b, dd + 0x44) != 0x2C00:
            continue
        As = be32(b, dd + 0x50) >> 8
        Ac = be32(b, dd + 0x54) >> 8
        label = bytes(b[dd:dd + 0x10]).split(b"\x00")[0].decode("latin1", "replace")
        if 0 < Ac <= h["n_pool"] and As + Ac <= h["n_pool"]:
            parts.append((dd + 0x50 - awg0, As, Ac, label))
    # 2) descriptores de parte de los arms: start +0x38, count +0x3C
    axes = awg0 + h["axes"]
    for i in range(h["n_bones"]):
        arm = be32(b, axes + i * 80 + 0x34)
        if not arm:
            continue
        vals = [be32(b, awg0 + arm + k * 4) for k in range(5)]
        p = awg0 + vals[1]
        if not vals[1]:
            continue
        st = be32(b, p + 0x38)
        ct = be32(b, p + 0x3C)
        label = bytes(b[p + 0x48:p + 0x58]).split(b"\x00")[0].decode("latin1", "replace")
        if 0 < ct <= h["n_pool"] and st + ct <= h["n_pool"]:
            parts.append((p + 0x38 - awg0, st, ct, "arm%d:%s" % (i, label)))
    # dedupe por (start,count,label-ish)
    seen = set(); out = []
    for (site, st, ct, lab) in parts:
        key = (st, ct, lab)
        if key in seen:
            continue
        seen.add(key); out.append((site, st, ct, lab))
    return out

def main():
    path, out = sys.argv[1], sys.argv[2]
    pair = None
    if "--pair" in sys.argv:
        i = sys.argv.index("--pair")
        pair = (int(sys.argv[i + 1]), int(sys.argv[i + 2]))
    b, h = load(path)
    awg0 = h["awg0"]
    sec = awg0 + h["sec"] + 2
    ib_a = awg0 + h["ib"]
    n_sec, n_ib, n_pool = h["n_sec"], h["n_ib"], h["n_pool"]
    parts = collect_parts(b, h)
    print("partes detectadas: %d" % len(parts))
    for k, (site, st, ct, lab) in enumerate(parts):
        print("  [%2d] site=+0x%05X [%4d,%4d) n=%3d %s" % (k, site, st, st + ct, ct, lab))

    # elegir dos con el mismo count
    if pair is None:
        best = None
        for i in range(len(parts)):
            for j in range(i + 1, len(parts)):
                if parts[i][2] == parts[j][2]:
                    if best is None or parts[i][2] > best[2]:
                        best = (i, j, parts[i][2])
        if best is None:
            print("ERROR: no hay dos partes con el mismo count")
            return 1
        pair = (best[0], best[1])
    i, j = pair
    (si, sti, ci, li) = parts[i]
    (sj, stj, cj, lj) = parts[j]
    print("\nT8: intercambio partes [%d]=%s [%d,%d) y [%d]=%s [%d,%d)" % (
        i, li, sti, sti + ci, j, lj, stj, stj + cj))
    if ci != cj:
        print("ERROR: counts distintos (%d vs %d): requiere desplazar A_start" % (ci, cj))
        return 1

    # permutacion (solo sec34; vb2 fuera)
    perm = list(range(n_pool))
    for x in range(ci):
        perm[sti + x] = stj + x
        perm[stj + x] = sti + x

    orig = bytes(b)
    # 1) mover los bloques del pool
    new_sec = bytearray(n_sec * 44)
    for o in range(n_sec):
        m = perm[o]
        new_sec[m * 44:m * 44 + 44] = orig[sec + o * 44:sec + o * 44 + 44]
    b[sec:sec + n_sec * 44] = new_sec
    # 2) remapear el IB
    for k in range(n_ib):
        v = be16(b, ib_a + k * 2)
        if v == 0xFFFF:
            continue
        if v < n_pool:
            struct.pack_into(">H", b, ib_a + k * 2, perm[v])
    # 3) actualizar los rangos de las dos partes (intercambiar los starts)
    struct.pack_into(">I", b, awg0 + si, stj << 8)
    struct.pack_into(">I", b, awg0 + sj, sti << 8)

    open(out, "wb").write(bytes(b))

    # validacion: cada triangulo resuelve al mismo registro de 44 B
    b2, _ = load(out)
    def grec(bb, v):
        o = sec + v * 44 if v < n_sec else (awg0 + h["vb2"] + (v - n_sec) * 44)
        return bytes(bb[o:o + 44])
    def ib2(v): return be16(b2, ib_a + v * 2)
    same = True; checked = 0
    for k in range(n_ib):
        a1 = struct.unpack(">H", orig[ib_a + k * 2:ib_a + k * 2 + 2])[0]
        a2 = ib2(k)
        if a1 == 0xFFFF or a2 == 0xFFFF or a1 >= n_pool or a2 >= n_pool:
            continue
        checked += 1
        if grec(orig, a1) != grec(b2, a2):
            same = False
            print("  DIFF en IB[%d]: origen idx %d -> nuevo idx %d" % (k, a1, a2))
            break
    print("validacion (triangulos->mismos bytes): %s  (revisados %d)" % (
        "OK" if same else "FALLO", checked))
    print("escrito:", out, "(%d bytes)" % len(b2))
    return 0 if same else 1

if __name__ == "__main__":
    sys.exit(main())
