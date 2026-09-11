#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_c_descriptors.py - Mapa de TODAS las tablas de descriptores 0x60 del
AWG0 (y demas AWGs). Un descriptor 0x60 tiene:
  +0x00 char label[]      +0x18 "max N m"
  +0x44 == 0x2C00 (type)  +0x50 A_s<<8  +0x54 A_c<<8  +0x58 B_s<<8  +0x5C B_c<<8
Los "arms" apuntan (vals[1]) a descriptores con (start,count) de vertice en
+0x38/+0x3C -> el consumidor posicional del pool.
Uso: python phase_c_descriptors.py <bin>
"""
import re
import struct
import sys

def be32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def bf(b, o): return struct.unpack(">f", b[o:o+4])[0]

def pars_awg(b, awg):
    h = {}
    for k, o in (("n_bones", 0x10), ("axes", 0x14), ("groups", 0x18),
                 ("mg", 0x20), ("mg_size", 0x28), ("vb2", 0x2C),
                 ("ib", 0x30), ("sec", 0x34), ("end", 0x38)):
        h[k] = be32(b, awg + o)
    h["n_sec"] = (h["vb2"] - h["sec"] - 2) // 44
    h["n_vb2"] = (h["ib"] - h["vb2"]) // 44
    h["n_ib"] = (h["end"] - h["ib"]) // 2
    h["n_pool"] = h["n_sec"] + h["n_vb2"]
    return h

def main():
    path = sys.argv[1]
    b = open(path, "rb").read()
    awo = 0x40
    awg_tbl = awo + be32(b, awo + 0x1C)
    n_awg = be32(b, awo + 0x18)
    offs = [awo + be32(b, awg_tbl + i * 4) for i in range(n_awg)]
    print("%s  n_awg=%d" % (path.split("\\")[-1], n_awg))

    # Todas las etiquetas "max N m" del fichero, con su AWG contenedor.
    for wi, awg in enumerate(offs):
        h = pars_awg(b, awg)
        end = awg + h["end"]
        lo, hi = awg, min(end, len(b) - 0x60)
        tags = [m.start() for m in re.finditer(rb"max \d+ m", b[lo:hi])]
        if not tags:
            continue
        rows = []
        for t in tags:
            dd = lo + t - 0x18
            if dd < lo:
                continue
            label = bytes(b[dd:dd + 0x10]).split(b"\x00")[0].decode("latin1", "replace")
            typ = be32(b, dd + 0x44)
            As = be32(b, dd + 0x50) >> 8
            Ac = be32(b, dd + 0x54) >> 8
            Bs = be32(b, dd + 0x58) >> 8
            Bc = be32(b, dd + 0x5C) >> 8
            flag = be32(b, dd + 0x5C) & 0xFF
            rows.append((dd - awg, label, typ, As, Ac, Bs, Bc, flag))
        print("\nAWG%d @0x%X n_bones=%d n_pool=%d n_ib=%d  descriptores(+ max m)=%d" % (
            wi, awg, h["n_bones"], h["n_pool"], h["n_ib"], len(rows)))
        for (rel, label, typ, As, Ac, Bs, Bc, flag) in rows:
            print("  +0x%05X  %-16s type=0x%X A=%4d,%-4d B=%4d,%-4d flag=0x%02X" % (
                rel, label, typ, As, Ac, Bs, Bc, flag))

    # Arms del AWG0 -> descriptores con (start,count) de vertice
    awg0 = offs[0]
    h = pars_awg(b, awg0)
    axes = awg0 + h["axes"]
    print("\n--- AWG0: arms -> descriptores con rango de vertice (+0x38/+0x3C) ---")
    part_re = re.compile(rb"[0-9A-Za-z_]{4,20}\x00")
    for i in range(h["n_bones"]):
        arm = be32(b, axes + i * 80 + 0x34)
        if not arm:
            continue
        vals = [be32(b, awg0 + arm + k * 4) for k in range(5)]
        if not (vals[1] or vals[3]):
            continue
        p1 = awg0 + vals[1]
        st = be32(b, p1 + 0x38)
        ct = be32(b, p1 + 0x3C)
        # nombre: primera etiqueta ASCII en [p1, p1+0x60)
        nm = "?"
        m = part_re.search(bytes(b[p1:p1 + 0x80]))
        if m:
            nm = m.group(0)[:-1].decode("latin1", "replace")
        print("  bone %2d arm=+0x%05X p1=+0x%05X  range=[%4d,%4d)  %s" % (
            i, arm, vals[1], st, st + ct, nm))

if __name__ == "__main__":
    main()
