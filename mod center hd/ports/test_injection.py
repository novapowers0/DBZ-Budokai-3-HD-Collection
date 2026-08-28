#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Injection test: template topology (IB+descriptors+everything) + our PS2
positions mapped per-bone into the template's pool order.

Modifies ONLY +12/+16/+20 (z/x/y) of the template's sec34 slots, keeping the
template's IB, descriptors, vb2, other AWGs, axes and arms intact.
"""
import struct, sys, math

def be32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def f32(b, o): return struct.unpack('>f', b[o:o+4])[0]

def main():
    templ = open(sys.argv[1], 'rb').read()
    geom = json.load(open(sys.argv[2]))
    out = bytearray(templ)

    awo = 0x40
    AWG0 = awo + be32(out, awo + be32(out, awo + 0x1C))
    sec_rel = be32(out, AWG0 + 0x34)
    vb2_rel = be32(out, AWG0 + 0x2C)
    sec_real = AWG0 + sec_rel + 2
    n_slots = (vb2_rel - sec_rel - 2) // 44

    # Parse template slots: (bone, x, y, z)
    slots = []
    for i in range(n_slots):
        o = sec_real + i * 44
        bone = be32(out, o + 28)
        x, y, z = f32(out, o + 16), f32(out, o + 20), f32(out, o + 12)
        slots.append((bone, x, y, z))

    # Parse our PS2 verts from geometry JSON: per bone, list of (x,y,z)
    sec34 = bytes.fromhex(geom['sec34'])
    ours = {}
    for i in range(geom['n_sec']):
        o = i * 44
        bone = be32(sec34, o + 28)
        x, y, z = f32(sec34, o + 16), f32(sec34, o + 20), f32(sec34, o + 12)
        ours.setdefault(bone, []).append((x, y, z))

    filled = 0
    for bone in set(b for b, _, _, _ in slots):
        lst = ours.get(bone, [])
        if not lst:
            continue
        used = [False] * len(lst)
        # assign template slots (that bone) greedily nearest-neighbor
        for si, (sb, sx, sy, sz) in enumerate(slots):
            if sb != bone:
                continue
            best = None; bd = 1e30
            for oi, (ox, oy, oz) in enumerate(lst):
                if used[oi]:
                    continue
                d = (ox-sx)**2 + (oy-sy)**2 + (oz-sz)**2
                if d < bd:
                    bd = d; best = oi
            if best is not None:
                used[best] = True
                ox, oy, oz = lst[best]
                off = sec_real + si * 44
                struct.pack_into('>f', out, off + 16, ox)
                struct.pack_into('>f', out, off + 20, oy)
                struct.pack_into('>f', out, off + 12, oz)
                filled += 1
    print('slots sec34: %d | posiciones PS2 inyectadas: %d' % (n_slots, filled))
    open(sys.argv[3], 'wb').write(bytes(out))
    print('guardado:', sys.argv[3])

if __name__ == '__main__':
    import json
    main()