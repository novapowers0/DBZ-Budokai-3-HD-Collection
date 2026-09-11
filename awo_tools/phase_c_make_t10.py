#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_c_make_t10.py - Test T10: relabeling CONSISTENTE en el espacio de
indices del GUEST (el bug de base de T4/T9).

Evidencia (docs/07_ports/SESION_GPU_DRAW_2026-09-11.md §6):
  - El guest construye un vertex buffer copiando el pool VERBATIM en orden:
    buffer[i+10] == sec34_tool[i]   (indices guest = indice tool + OFF)
  - El guest usa el IB del fichero (substring contiguo exacto).
  => Un relabeling consistente DEBE ser identidad.

El IB referencia indices en el espacio del GUEST (0..n_pool_guest), pero el tool
historico (T2/T9) remapeaba usando `sec` como base 0, cuando el sec34 empieza en
el indice guest OFF=10. T10 remapea con la base correcta.

Permutacion: REVERSE completo del sec34 (lo mas agresivo y sin ambiguedad).
  - Identico  -> confirmado: era un bug de base de indices -> Via B desbloqueada.
  - Deforme   -> hay un consumidor posicional real (volver a GPU).

Uso: python phase_c_make_t10.py <in.bin> <out.bin> [--off N] [--swap I J]
"""
import struct
import sys

OFF_DEFAULT = 10  # indice guest = indice tool (sec34) + OFF

def be32(b, o): return struct.unpack(">I", b[o:o + 4])[0]
def be16(b, o): return struct.unpack(">H", b[o:o + 2])[0]

def load(path):
    b = bytearray(open(path, "rb").read())
    awo = 0x40
    tbl = awo + be32(b, awo + 0x1C)
    awg0 = awo + be32(b, tbl)
    def g(o): return be32(b, awg0 + o)
    h = {"mg": g(0x20), "vb2": g(0x2C), "ib": g(0x30), "sec": g(0x34), "end": g(0x38)}
    h["n_sec"] = (h["vb2"] - h["sec"] - 2) // 44
    h["n_vb2"] = (h["ib"] - h["vb2"]) // 44
    h["n_ib"] = (h["end"] - h["ib"]) // 2
    h["awg0"] = awg0
    return b, h

def main():
    path, out = sys.argv[1], sys.argv[2]
    off = OFF_DEFAULT
    if "--off" in sys.argv:
        off = int(sys.argv[sys.argv.index("--off") + 1])
    swap = None
    if "--swap" in sys.argv:
        i = sys.argv.index("--swap")
        swap = (int(sys.argv[i + 1]), int(sys.argv[i + 2]))

    b, h = load(path)
    awg0, n_sec, n_ib = h["awg0"], h["n_sec"], h["n_ib"]
    sec = awg0 + h["sec"] + 2          # inicio del sec34 (base del tool)
    ib_a = awg0 + h["ib"]
    orig = bytes(b)

    # permutacion sobre indices TOOL (sec34): reverse o swap
    if swap is None:
        perm = [n_sec - 1 - t for t in range(n_sec)]
        kind = "REVERSE sec34"
    else:
        perm = list(range(n_sec))
        i, j = swap
        perm[i], perm[j] = j, i
        kind = "SWAP tool %d<->%d" % (i, j)

    # mover registros fisicos del sec34
    new_sec = bytearray(n_sec * 44)
    for t in range(n_sec):
        new_sec[perm[t] * 44:perm[t] * 44 + 44] = orig[sec + t * 44:sec + t * 44 + 44]
    b[sec:sec + n_sec * 44] = new_sec

    # remapear IB en el espacio GUEST: v_guest -> perm[v_guest - off] + off
    remapped = 0
    outside = 0
    for k in range(n_ib):
        v = be16(b, ib_a + k * 2)
        if v == 0xFFFF:
            continue
        gidx = v - off
        if 0 <= gidx < n_sec:
            struct.pack_into(">H", b, ib_a + k * 2, perm[gidx] + off)
            remapped += 1
        else:
            outside += 1
    open(out, "wb").write(bytes(b))
    print("T10 %s off=%d: n_sec=%d n_ib=%d  IB remapeados=%d  fuera de sec34=%d"
          % (kind, off, n_sec, n_ib, remapped, outside))

    # validacion geometrica en el espacio GUEST
    b2 = open(out, "rb").read()
    def grec(bb, gidx):
        return bytes(bb[sec + (gidx - off) * 44:sec + (gidx - off) * 44 + 44])
    same = True
    checked = 0
    for k in range(n_ib):
        v1 = be16(orig, ib_a + k * 2)
        v2 = be16(b2, ib_a + k * 2)
        if v1 == 0xFFFF:
            continue
        if not (off <= v1 < off + n_sec and off <= v2 < off + n_sec):
            continue
        checked += 1
        if grec(orig, v1) != grec(b2, v2):
            same = False
            break
    print("validacion (registro resuelto identico por vertice):",
          "OK" if same else "FALLO", "(revisados %d)" % checked)
    print("escrito:", out)
    return 0 if same else 1

if __name__ == "__main__":
    sys.exit(main())
