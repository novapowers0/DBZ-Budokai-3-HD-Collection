#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_c_make_t9.py - Test T9: reordenar RUNS mono-hueso dentro de un bloque A.

Distincion clave tras T8:
  - T8 movio PARTES mono-hueso -> identico.
  - T4 invirtio vertices DENTRO de bloques multi-hueso -> deforme.
T9 reordena (invierte el orden de) los runs contiguos de un mismo hueso dentro de
UN descriptor A, manteniendo cada run internamente intacto y remapeando el IB.

  - Identico  -> la regla es "respetar runs mono-hueso" (el builder debe emitirlos
                 contiguos; su orden es libre).
  - Deforme   -> el orden intra-bloque exacto importa (no basta con runs).

Uso: python phase_c_make_t9.py <in.bin> <out.bin> [--block START]
"""
import re
import struct
import sys

def be32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def be16(b, o): return struct.unpack(">H", b[o:o+2])[0]

def load(path):
    b = bytearray(open(path, "rb").read())
    awo = 0x40
    tbl = awo + be32(b, awo + 0x1C)
    awg0 = awo + be32(b, tbl)
    def g(o): return be32(b, awg0 + o)
    h = {"mg": g(0x20), "vb2": g(0x2C), "ib": g(0x30), "sec": g(0x34),
         "end": g(0x38), "awg0": awg0}
    h["n_sec"] = (h["vb2"] - h["sec"] - 2) // 44
    h["n_vb2"] = (h["ib"] - h["vb2"]) // 44
    h["n_ib"] = (h["end"] - h["ib"]) // 2
    h["n_pool"] = h["n_sec"] + h["n_vb2"]
    return b, h

def main():
    path, out = sys.argv[1], sys.argv[2]
    b, h = load(path)
    awg0 = h["awg0"]
    sec = awg0 + h["sec"] + 2
    ib_a = awg0 + h["ib"]
    n_sec, n_ib, n_pool = h["n_sec"], h["n_ib"], h["n_pool"]

    # bloques A (max N m) con su hueso(+28) por vertice
    def bone(v):
        o = sec + v * 44 if v < n_sec else awg0 + h["vb2"] + (v - n_sec) * 44
        return be32(b, o + 28)
    blocks = []
    mg = awg0 + h["mg"]
    for m in re.finditer(rb"max \d+ m", bytes(b[mg:awg0 + h["sec"]])):
        dd = mg + (m.start() - 0x18)
        if be32(b, dd + 0x44) != 0x2C00:
            continue
        As = be32(b, dd + 0x50) >> 8; Ac = be32(b, dd + 0x54) >> 8
        lab = bytes(b[dd:dd + 0x10]).split(b"\x00")[0].decode("latin1", "replace")
        if 0 < Ac <= n_sec and As + Ac <= n_sec:
            blocks.append((As, Ac, lab))
    # elegir el bloque con mas runs (o el indicado por --block START)
    want = None
    if "--block" in sys.argv:
        want = int(sys.argv[sys.argv.index("--block") + 1])
    target = None
    for (As, Ac, lab) in sorted(blocks, key=lambda x: -x[1]):
        if want is not None and As != want:
            continue
        # contar runs
        runs = []
        s = As
        for v in range(As + 1, As + Ac):
            if bone(v) != bone(v - 1):
                runs.append((s, v - s, bone(s)))
                s = v
        runs.append((s, As + Ac - s, bone(s)))
        if len(runs) >= 2:
            target = (As, Ac, lab, runs)
            break
    if target is None:
        print("ERROR: no hay bloque multi-run")
        return 1
    As, Ac, lab, runs = target
    print("T9 bloque [%d,%d) %s  runs=%d huesos=%s" % (
        As, As + Ac, lab, len(runs), [r[2] for r in runs][:20]))

    # permutacion: invertir el ORDEN de los runs (cada run intacto)
    perm = list(range(n_pool))
    pos = As
    for (rs, rc, rb) in reversed(runs):
        for x in range(rc):
            perm[rs + x] = pos + x
        pos += rc

    orig = bytes(b)
    new_sec = bytearray(n_sec * 44)
    for o in range(n_sec):
        m = perm[o]
        new_sec[m * 44:m * 44 + 44] = orig[sec + o * 44:sec + o * 44 + 44]
    b[sec:sec + n_sec * 44] = new_sec
    for k in range(n_ib):
        v = be16(b, ib_a + k * 2)
        if v != 0xFFFF and v < n_pool:
            struct.pack_into(">H", b, ib_a + k * 2, perm[v])
    open(out, "wb").write(bytes(b))

    # validacion offline (identidad geometrica)
    b2 = open(out, "rb").read()
    def grec(bb, v):
        o = sec + v * 44 if v < n_sec else awg0 + h["vb2"] + (v - n_sec) * 44
        return bytes(bb[o:o + 44])
    same = True; checked = 0
    for k in range(n_ib):
        a1 = struct.unpack(">H", orig[ib_a + k * 2:ib_a + k * 2 + 2])[0]
        a2 = be16(b2, ib_a + k * 2)
        if a1 == 0xFFFF or a2 == 0xFFFF or a1 >= n_pool or a2 >= n_pool:
            continue
        checked += 1
        if grec(orig, a1) != grec(b2, a2):
            same = False
            break
    print("validacion: %s (revisados %d)" % ("OK" if same else "FALLO", checked))
    print("escrito:", out)
    return 0 if same else 1

if __name__ == "__main__":
    sys.exit(main())
