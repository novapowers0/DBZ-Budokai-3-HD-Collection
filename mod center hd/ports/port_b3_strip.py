#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""fix_port_strip.py <port.bin> <out.bin> [--max-bone=N] - Vía B: reescribe el IB
del AWG0 como UNA tira (strip) que reproduce la malla con el WINDING original, y
ajusta los descriptores 0x60 para que el guest (que dibuja el cuerpo como strip)
la dibuje entera.

--max-bone=N: omite los triángulos que toquen un vértice de hueso > N (test de
aislamiento: p.ej. 33 = solo cuerpo, sin boca/cara ni cola). Se aplica DESPUÉS de
recorrer el IB-lista de la plantilla y ANTES de stripificar.

Uso: python fix_port_strip.py <in.bin> <out.bin> [--max-bone=N]
"""
import re, struct, sys

def be32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def set32(b, o, v): struct.pack_into(">I", b, o, v & 0xFFFFFFFF)
def be16(b, o): return struct.unpack(">H", b[o:o+2])[0]
def set16(b, o, v): struct.pack_into(">H", b, o, v & 0xFFFF)


def runs_in_order(tris):
    runs, cur = [], None
    for t in tris:
        if cur is None:
            cur = [t[0], t[1], t[2]]; continue
        s = set(t)
        if len(cur) >= 2 and cur[-2] in s and cur[-1] in s:
            o = [x for x in t if x not in (cur[-2], cur[-1])]
            if o:
                cur.append(o[0]); continue
        if len(cur) >= 2 and cur[0] in s and cur[1] in s:
            o = [x for x in t if x not in (cur[0], cur[1])]
            if o:
                cur.insert(0, o[0]); continue
        runs.append(cur); cur = [t[0], t[1], t[2]]
    if cur:
        runs.append(cur)
    return runs


def join_runs(runs):
    out = []
    for r in runs:
        if not r:
            continue
        if not out:
            out.extend(r); continue
        for extra in (2, 3):
            start = len(out) + extra - 2
            if start % 2 == 0:
                out = out + [out[-1]] + [r[0]] * (extra - 1) + r
                break
        else:
            out = out + [out[-1], r[0]] + r
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    max_bone = None
    for a in sys.argv[1:]:
        if a.startswith("--max-bone="):
            max_bone = int(a.split("=", 1)[1])
    src, dst = args[0], args[1]
    b = bytearray(open(src, "rb").read())
    awo = 0x40
    awg_tbl = awo + be32(b, awo + 0x1C)
    awg0 = awo + be32(b, awg_tbl)
    g = lambda o: be32(b, awg0 + o)
    ib_abs = awg0 + g(0x30)
    n_ib = (g(0x38) - g(0x30)) // 2
    n_win = g(0x2C) // 44
    vb0 = ib_abs - n_win * 44
    ib = [be16(b, ib_abs + k * 2) for k in range(n_ib)]
    # trunca el padding (0xFFFF o indice fuera de rango) antes de stripificar
    nv = len(ib)
    for k, v in enumerate(ib):
        if v == 0xFFFF or v >= n_win:
            nv = k; break
    ib = ib[:nv - (nv % 3)]
    tris = [(ib[i], ib[i+1], ib[i+2]) for i in range(0, len(ib) - 2, 3)]
    if max_bone is not None:
        bone = lambda v: be32(b, vb0 + v * 44 + 16) & 0xFF
        keep = [t for t in tris if max(bone(v) for v in t) <= max_bone]
        print("filtro hueso<=%d: tris %d -> %d (descartados %d)"
              % (max_bone, len(tris), len(keep), len(tris) - len(keep)))
        tris = keep
    runs = runs_in_order(tris)
    seq = join_runs(runs)
    print("tris=%d runs=%d strip=%d n_ib=%d" % (len(tris), len(runs), len(seq), n_ib))
    assert len(seq) <= n_ib, "strip no cabe (usa grow)"
    full = seq + [seq[-1]] * (n_ib - len(seq))
    for k, v in enumerate(full):
        set16(b, ib_abs + k * 2, v)

    end = awg0 + g(0x38)
    tags = [m.start() for m in re.finditer(rb"max \d+ m", bytes(b[awg0:end]))]
    descs = [d for d in (awg0 + t - 0x18 for t in tags) if d >= awg0]
    first = next((d for d in descs if be32(b, d + 0x44) == 0x2C00), None)
    for d in descs:
        flag = be32(b, d + 0x5C) & 0xFF
        if d == first:
            set32(b, d + 0x50, 0)
            set32(b, d + 0x54, n_ib << 8)
            set32(b, d + 0x58, 0)
            set32(b, d + 0x5C, (n_ib << 8) | flag)
        else:
            set32(b, d + 0x5C, 0)
    print("desc +0x%05X B=[0,%d)" % (first - awg0, n_ib))

    axes = awg0 + g(0x14)
    for i in range(g(0x10)):
        arm = be32(b, axes + i * 80 + 0x34)
        if not arm:
            continue
        vals = [be32(b, awg0 + arm + k * 4) for k in range(5)]
        if vals[1]:
            # part-descriptor: vert[+0x38,+0x3C) e idx[+0x40,+0x44). El port
            # dibuja TODO via desc[0] (B=[0,n_ib)), asi que anulamos AMBOS
            # rangos de las partes (si no, las manos/cara siguen dibujando el
            # IB del port en los offsets de la plantilla -> flap).
            set32(b, awg0 + vals[1] + 0x3C, 0)
            set32(b, awg0 + vals[1] + 0x44, 0)
    open(dst, "wb").write(bytes(b))
    print("OK ->", dst)
    return 0

if __name__ == "__main__":
    sys.exit(main())
