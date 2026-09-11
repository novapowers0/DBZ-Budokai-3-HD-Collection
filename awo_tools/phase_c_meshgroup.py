#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_c_meshgroup.py - Volcado de la estructura mesh-group y de los arms del
AWG0 para localizar el 'consumidor posicional' del pool de vertices (Vía B).

Hallazgo previo: cada arm con datos (7 huesos: 0,23,30,36,38,40,47) apunta
(vals[1]) a un struct que contiene (offset,count) en +0x38/+0x3C, p.ej.
bone23 -> (2176,116), bone40 -> (2713,109). Estos son RANGOS del pool.

Uso: python phase_c_meshgroup.py <bin>
"""
import re
import struct
import sys

def be32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def bf(b, o): return struct.unpack(">f", b[o:o+4])[0]

def parse(b):
    awo = 0x40
    awg_tbl = awo + be32(b, awo + 0x1C)
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

def dump_words(b, base, count, tag):
    print("--- %s (AWG0+0x%X) %d words ---" % (tag, base, count))
    for r in range(0, count, 4):
        o = base + r * 4
        u = [be32(b, o + j * 4) for j in range(4)]
        f = [bf(b, o + j * 4) for j in range(4)]
        print("  +0x%04X  %s | %s" % (
            r * 4, " ".join("%08X" % x for x in u),
            " ".join("%9.3f" % x for x in f)))

def main():
    path = sys.argv[1]
    b = open(path, "rb").read()
    awg0, h = parse(b)
    n_pool = h["n_sec"] + h["n_vb2"]
    print("%s AWG0=0x%X n_bones=%d n_sec=%d n_vb2=%d n_ib=%d n_pool=%d" % (
        path.split("\\")[-1], awg0, h["n_bones"], h["n_sec"], h["n_vb2"],
        h["n_ib"], n_pool))
    print("mg=+0x%X size=0x%X sec=+0x%X ib=+0x%X end=+0x%X groups=%d" % (
        h["mg"], h["mg_size"], h["sec"], h["ib"], h["end"], h["groups"]))

    # Modo volcado de region:  phase_c_meshgroup.py <bin> --region OFF LEN
    if len(sys.argv) >= 4 and sys.argv[2] == "--region":
        off = int(sys.argv[3], 0)
        ln = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x100
        dump_words(b, awg0 + off, ln // 4, "region +0x%X" % off)
        return

    # 1) Structs de los arms (vals[1]) completos
    axes = awg0 + h["axes"]
    seen = set()
    for i in range(h["n_bones"]):
        e = axes + i * 80
        arm = be32(b, e + 0x34)
        if arm == 0:
            continue
        ao = awg0 + arm
        vals = [be32(b, ao + k * 4) for k in range(5)]
        p1 = vals[1]
        if p1 == 0 or p1 in seen:
            continue
        seen.add(p1)
        dump_words(b, awg0 + p1, 24, "arm bone%d vals[1]" % i)

    # 2) Zona mesh-group como bloques de 0x50 (mesh-ref) marcando (start,count)
    print("\n--- mesh-group [mg, sec) en bloques de 0x50 ---")
    lo, hi = awg0 + h["mg"], awg0 + h["sec"]
    for blk in range(lo, hi - 0x50, 0x50):
        rel = blk - awg0
        ws = [be32(b, blk + k * 4) for k in range(0x50 // 4)]
        # marcar pares (v0,v1) con v1 razonable como count
        pairs = [(k * 4, ws[k], ws[k + 1]) for k in range(0, 0x50 // 4 - 1)
                 if 0 < ws[k + 1] <= n_pool and 0 <= ws[k] <= n_pool]
        name = bytes(b[blk:blk + 16]).split(b"\x00")[0]
        if b"max" in name or pairs:
            print(" blk +0x%05X name=%r pairs=%s" % (
                rel, name[:16], pairs[:6]))

    # 3) Scan de descriptores de parte: nombre ASCII (+0x48) con (start,count)
    #    en +0x38/+0x3C y data_off en +0x40.  name_off = struct+0x48.
    print("\n--- tabla de PARTES (nombre @+0x48; start@+0x38 count@+0x3C data@+0x40 cnt@+0x44) ---")
    name_re = re.compile(rb"[0-9A-Z_]{6,16}\x00")
    parts = []
    for o in range(awg0 + 0x100, len(b) - 0x50):
        raw = bytes(b[o:o + 0x11])
        m = name_re.match(raw)
        if not m:
            continue
        name = m.group(0)[:-1].decode("latin1")
        if not any(t in name for t in ("BODY", "L00", "L01", "L02", "L04", "L05", "L09", "L10", "L18", "M_", "T_", "HAIR", "FACE")):
            continue
        st = be32(b, o - 0x10)
        ct = be32(b, o - 0x0C)
        da = be32(b, o - 0x08)
        cc = be32(b, o - 0x04)
        if 0 <= st <= n_pool and 0 < ct <= n_pool and st + ct <= n_pool + 64:
            parts.append((o - 0x48 - awg0, name, st, ct, da, cc))
    parts.sort()
    for (rel, name, st, ct, da, cc) in parts:
        print("  ref +0x%05X  %-16s start=%4d count=%4d data=0x%05X cnt=%d  -> [%d,%d)" % (
            rel, name, st, ct, da, cc, st, st + ct))
    print("  total partes con rango: %d" % len(parts))

    # 4) Censo ampliado: pares (u32 start,u32 count) que cubran pool en TODA la
    #    zona no-buffer (mg + tail), con count en [4, n_pool].
    print("\n--- pares (start,count) 4<=count<=n_pool en mg/tail ---")
    for (nm, a, z) in (("mg", h["mg"], h["sec"]), ("tail", h["end"], len(b) - awg0)):
        cnt = 0
        for off in range(a, z - 8, 4):
            s0 = be32(b, awg0 + off)
            c0 = be32(b, awg0 + off + 4)
            if 4 <= c0 <= n_pool and 0 <= s0 and s0 + c0 <= n_pool:
                print("   [%s] +0x%05X start=%d count=%d end=%d" % (nm, off, s0, c0, s0 + c0))
                cnt += 1
                if cnt > 80:
                    print("   ...")
                    break

if __name__ == "__main__":
    main()
