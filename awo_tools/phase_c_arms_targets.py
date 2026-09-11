#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_c_arms_targets.py - Volcado de los destinos de los arms del AWG0.

Hipotesis a validar (Fase C, Vía B): el "consumidor posicional" del pool podria
ser una tabla por hueso (start,count) o una lista de "mesh-ref" apuntada por el
arm. El arm es [bone, p1, 0, p2, 0] (5 u32). En e147:
  - p2 crece EXACTAMENTE +64 por hueso -> matriz 4x4 (bind pose).
  - p1 apunta a algo mayor (8608, 10832, 11440...) -> a inspeccionar.
  - solo 7 huesos (0,23,30,36,38,40,47) tienen arm con datos !=0.
Uso: python phase_c_arms_targets.py <bin> [n_words]
"""
import struct
import sys

def be32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def be16(b, o): return struct.unpack(">H", b[o:o+2])[0]
def bf(b, o): return struct.unpack(">f", b[o:o+4])[0]

def parse(b):
    awo = 0x40
    awg_tbl = awo + be32(b, awo + 0x1C)
    n_awg = be32(b, awo + 0x18)
    awg0 = awo + be32(b, awg_tbl)
    h = {
        "n_bones": be32(b, awg0 + 0x10),
        "axes": be32(b, awg0 + 0x14),
        "groups": be32(b, awg0 + 0x18),
        "mg": be32(b, awg0 + 0x20),
        "mg_size": be32(b, awg0 + 0x28),
        "vb2": be32(b, awg0 + 0x2C),
        "ib": be32(b, awg0 + 0x30),
        "sec": be32(b, awg0 + 0x34),
        "end": be32(b, awg0 + 0x38),
    }
    h["n_sec"] = (h["vb2"] - h["sec"] - 2) // 44
    h["n_vb2"] = (h["ib"] - h["vb2"]) // 44
    h["n_ib"] = (h["end"] - h["ib"]) // 2
    return awg0, h

def main():
    path = sys.argv[1]
    nw = int(sys.argv[2]) if len(sys.argv) > 2 else 16
    b = open(path, "rb").read()
    awg0, h = parse(b)
    print("%s AWG0=0x%X n_bones=%d mg=+0x%X(+0x%X) n_sec=%d n_vb2=%d n_ib=%d" % (
        path.split("\\")[-1], awg0, h["n_bones"], h["mg"], h["mg_size"],
        h["n_sec"], h["n_vb2"], h["n_ib"]))
    axes = awg0 + h["axes"]
    print("--- arms con datos (vals[1]/vals[3] != 0) ---")
    interesting = []
    for i in range(h["n_bones"]):
        e = axes + i * 80
        arm = be32(b, e + 0x34)
        if arm == 0:
            continue
        ao = awg0 + arm
        vals = [be32(b, ao + k * 4) for k in range(5)]
        if not (vals[1] or vals[3]):
            continue
        interesting.append((i, arm, vals))
        print(" bone %2d arm=0x%05X vals=%s" % (i, arm, vals))

    print("\n--- volcado de destinos (u32/u16/f32) ---")
    for (i, arm, vals) in interesting:
        for name, off in (("p1", vals[1]), ("p2", vals[3])):
            if off == 0:
                continue
            t = awg0 + off
            print("\n bone%2d %s AWG0+0x%05X (abs 0x%X):" % (i, name, off, t))
            if t + nw * 4 > len(b):
                print("   fuera de rango")
                continue
            for row in range(0, nw, 4):
                u = [be32(b, t + (row + j) * 4) for j in range(4)]
                f = [bf(b, t + (row + j) * 4) for j in range(4)]
                print("   +0x%02X  u32=%s  f32=%s" % (
                    row * 4, " ".join("%08X" % x for x in u),
                    " ".join("%8.3f" % x for x in f)))
            # interpretar como u16
            u16 = [be16(b, t + k * 2) for k in range(min(nw * 2, 32))]
            print("   u16:", " ".join("%d" % x for x in u16))

    # Buscar estructuras (start,count) que particionen [0,n_sec) en toda la zona mg
    print("\n--- candidatos (u32 start,u32 count) con count<=n_sec y start+count<=n_sec ---")
    lo, hi = awg0 + h["mg"], awg0 + h["sec"]
    n = h["n_sec"]
    cands = []
    for off in range(lo, hi - 8, 4):
        s0 = be32(b, off)
        c0 = be32(b, off + 4)
        if 0 < c0 <= n and 0 <= s0 and s0 + c0 <= n and c0 >= 8:
            cands.append((off - awg0, s0, c0))
    for (o, s0, c0) in cands[:60]:
        print("   +0x%05X  start=%d count=%d (end=%d)" % (o, s0, c0, s0 + c0))
    print("   total candidatos: %d" % len(cands))

if __name__ == "__main__":
    main()
