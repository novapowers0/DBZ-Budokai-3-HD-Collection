#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""awg_invariants.py - Invariantes del AWG0 que debe cumplir un bin Vía B.

Comprueba lo que el T8 confirmo que exige el guest:
  1. Las partes (A de descriptores 0x60 + rangos de los descriptores de parte de
     los arms) TESELAN el pool [0,n_pool) sin solapes y sin huecos.
  2. Cada parte es HOMOGENEA de hueso (los +28 de sus vertices son un unico
     hueso) => equivale a un "run de hueso contiguo".
  3. Los indices del IB de cada rango B caen dentro de A y el conjunto A es
     contiguo.
Uso: python awg_invariants.py <bin>
"""
import re
import struct
import sys

def be32(b, o): return struct.unpack(">I", b[o:o+4])[0]

def main():
    path = sys.argv[1]
    b = open(path, "rb").read()
    awo = 0x40
    awg_tbl = awo + be32(b, awo + 0x1C)
    awg0 = awo + be32(b, awg_tbl)
    def g(o): return be32(b, awg0 + o)
    n_sec = (g(0x2C) - g(0x34) - 2) // 44
    n_vb2 = (g(0x30) - g(0x2C)) // 44
    n_ib = (g(0x38) - g(0x30)) // 2
    n_pool = n_sec + n_vb2
    sec = awg0 + g(0x34) + 2
    vb2 = awg0 + g(0x2C)
    ib_a = awg0 + g(0x30)
    print("%s  n_sec=%d n_vb2=%d n_ib=%d n_pool=%d" % (
        path.split("\\")[-1], n_sec, n_vb2, n_ib, n_pool))

    def bone_of(v):
        if v >= n_pool:
            return None
        o = sec + v * 44 if v < n_sec else vb2 + (v - n_sec) * 44
        return be32(b, o + 28)

    parts = []
    # descriptores 0x60
    mg = awg0 + g(0x20)
    for m in re.finditer(rb"max \d+ m", b[mg:awg0 + g(0x34)]):
        dd = mg + (m.start() - 0x18)
        if be32(b, dd + 0x44) != 0x2C00:
            continue
        As = be32(b, dd + 0x50) >> 8; Ac = be32(b, dd + 0x54) >> 8
        Bs = be32(b, dd + 0x58) >> 8; Bc = be32(b, dd + 0x5C) >> 8
        lab = bytes(b[dd:dd + 0x10]).split(b"\x00")[0].decode("latin1", "replace")
        if 0 < Ac <= n_pool and As + Ac <= n_pool:
            parts.append((As, Ac, lab, Bs, Bc))
    # partes de los arms
    axes = awg0 + g(0x14)
    for i in range(g(0x10)):
        arm = be32(b, axes + i * 80 + 0x34)
        if not arm:
            continue
        vals = [be32(b, awg0 + arm + k * 4) for k in range(5)]
        if not vals[1]:
            continue
        p = awg0 + vals[1]
        st = be32(b, p + 0x38); ct = be32(b, p + 0x3C)
        lab = bytes(b[p + 0x48:p + 0x58]).split(b"\x00")[0].decode("latin1", "replace")
        if 0 < ct <= n_pool and st + ct <= n_pool:
            parts.append((st, ct, "arm%d:%s" % (i, lab), None, None))

    # 1) cobertura
    cov = [0] * n_pool
    for (As, Ac, lab, Bs, Bc) in parts:
        for i in range(As, As + Ac):
            cov[i] += 1
    gaps = [i for i, c in enumerate(cov) if c == 0]
    over = [i for i, c in enumerate(cov) if c > 1]
    print("1) cobertura: partes=%d  cubiertos=%d/%d  solapes=%d  huecos=%d" % (
        len(parts), sum(1 for c in cov if c), n_pool, len(over), len(gaps)))
    if gaps:
        rng = []; s = gaps[0]; p0 = gaps[0]
        for i in gaps[1:]:
            if i == p0 + 1: p0 = i
            else: rng.append((s, p0 + 1)); s = i; p0 = i
        rng.append((s, p0 + 1))
        print("   huecos:", rng[:12])

    # 2) homogeneidad de hueso por parte
    print("2) homogeneidad de hueso por parte:")
    nonhomo = 0
    for (As, Ac, lab, Bs, Bc) in sorted(parts):
        bones = {}
        for v in range(As, As + Ac):
            bn = bone_of(v)
            bones[bn] = bones.get(bn, 0) + 1
        top = sorted(bones.items(), key=lambda kv: -kv[1])[:4]
        if len(bones) > 1:
            nonhomo += 1
            if nonhomo <= 12:
                print("   [%4d,%4d) %-18s huesos=%s" % (As, As + Ac, lab, top))
    print("   partes NO homogeneas: %d/%d" % (nonhomo, len(parts)))

    # 3) A/B: indices de B dentro de A
    IB = [struct.unpack(">H", b[ib_a + 2 * k:ib_a + 2 * k + 2])[0] for k in range(n_ib)]
    bad = 0; checked = 0
    for (As, Ac, lab, Bs, Bc) in parts:
        if Bs is None:
            continue
        idxs = [IB[x] for x in range(Bs, min(Bs + Bc, n_ib)) if IB[x] != 0xFFFF]
        if not idxs:
            continue
        checked += 1
        if not (min(idxs) >= As and max(idxs) < As + Ac):
            bad += 1
    print("3) B dentro de A: %d/%d OK" % (checked - bad, checked))

    # 4) estructura de runs globales de hueso en el pool
    bones = [bone_of(v) for v in range(n_pool)]
    runs = 1
    for v in range(1, n_pool):
        if bones[v] != bones[v - 1]:
            runs += 1
    cnt = {}
    for bn in bones:
        cnt[bn] = cnt.get(bn, 0) + 1
    print("4) runs de hueso globales: %d  huesos distintos=%d  (ideal: 1 run/hueso)" % (
        runs, len(cnt)))

if __name__ == "__main__":
    main()
