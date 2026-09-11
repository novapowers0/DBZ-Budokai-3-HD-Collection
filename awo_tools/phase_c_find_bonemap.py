#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""phase_c_find_bonemap.py - Busca una tabla posicion->hueso en el AWG.

T9 sugiere que el guest mapea POSICION del pool -> hueso (no solo el +28).
Buscamos en todo el AWG un tramo cuya secuencia de valores coincida con la
secuencia de huesos (+28) del pool, en u8/u16/u32.
Uso: python phase_c_find_bonemap.py <bin>
"""
import struct
import sys

def be32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def be16(b, o): return struct.unpack(">H", b[o:o+2])[0]

def main():
    b = open(sys.argv[1], "rb").read()
    awo = 0x40
    tbl = awo + be32(b, awo + 0x1C)
    awg0 = awo + be32(b, tbl)
    def g(o): return be32(b, awg0 + o)
    n_sec = (g(0x2C) - g(0x34) - 2) // 44
    n_vb2 = (g(0x30) - g(0x2C)) // 44
    n_pool = n_sec + n_vb2
    sec = awg0 + g(0x34) + 2
    vb2 = awg0 + g(0x2C)
    end = awg0 + g(0x38)
    seq32 = []
    for v in range(n_pool):
        o = sec + v * 44 if v < n_sec else vb2 + (v - n_sec) * 44
        seq32.append(be32(b, o + 28))
    print("n_pool=%d huesos distintos=%d" % (n_pool, len(set(seq32))))

    # buffer regions a excluir (el propio pool)
    excl = set()
    for v in range(n_sec):
        for k in range(44):
            excl.add(sec + v * 44 + k)
    for v in range(n_vb2):
        for k in range(44):
            excl.add(vb2 + v * 44 + k)
    for k in range(end - (awg0 + g(0x30))):
        excl.add(awg0 + g(0x30) + k)

    def is_buff(off):
        return off in excl

    # u8: buscar tramo (no buffer) donde b[o+i]==seq32[i]
    wins = []
    for o in range(awg0, end):
        if is_buff(o):
            continue
        # comprobar 64 consecutivos
        ok = True
        for i in range(64):
            p = o + i
            if p >= end or is_buff(p) or b[p] != (seq32[i] & 0xFF):
                ok = False
                break
        if ok:
            # medir longitud total
            i = 64
            while o + i < end and not is_buff(o + i) and b[o + i] == (seq32[i] & 0xFF):
                i += 1
            wins.append((o - awg0, "u8", i))
    # u16
    for o in range(awg0, end - 2, 1):
        if is_buff(o):
            continue
        ok = True
        for i in range(64):
            p = o + i * 2
            if p + 2 > end or is_buff(p) or be16(b, p) != (seq32[i] & 0xFFFF):
                ok = False
                break
        if ok:
            i = 64
            while o + i * 2 + 2 <= end and not is_buff(o + i * 2) and be16(b, o + i * 2) == (seq32[i] & 0xFFFF):
                i += 1
            wins.append((o - awg0, "u16", i))
    # u32
    for o in range(awg0, end - 4, 1):
        if is_buff(o):
            continue
        ok = True
        for i in range(64):
            p = o + i * 4
            if p + 4 > end or is_buff(p) or be32(b, p) != seq32[i]:
                ok = False
                break
        if ok:
            i = 64
            while o + i * 4 + 4 <= end and not is_buff(o + i * 4) and be32(b, o + i * 4) == seq32[i]:
                i += 1
            wins.append((o - awg0, "u32", i))
    if not wins:
        print("NO se encontro tabla posicion->hueso (u8/u16/u32) en el AWG.")
    else:
        for (off, kind, ln) in wins[:20]:
            print("  CANDIDATO %s @+0x%05X  coincidencias=%d" % (kind, off, ln))

if __name__ == "__main__":
    main()
