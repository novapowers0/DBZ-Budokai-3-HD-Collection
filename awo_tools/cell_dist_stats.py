#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""cell_dist_stats.py - Estadisticas por hueso del NPM global (distancias slot->PS2).

Uso: python cell_dist_stats.py <plantilla.bin> <extract.json>
"""
import sys, json
import numpy as np
sys.path.insert(0, r'C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\mod center hd\ports')
import port_ps2_b3_inject as inj

def main():
    templ = open(sys.argv[1], 'rb').read()
    ext = json.load(open(sys.argv[2]))
    world, AWG0 = inj.world_mats(bytearray(templ), 0x40)
    inv = [np.linalg.inv(w) for w in world]
    sec_rel = inj.be32(templ, AWG0 + 0x34); vb2_rel = inj.be32(templ, AWG0 + 0x2C)
    sec = AWG0 + sec_rel + 2; n = (vb2_rel - sec_rel - 2)//44
    sw = []; sb = []
    for i in range(n):
        o = sec + i*44; b = inj.be32(templ, o + 28)
        x, y, z = inj.be_f(templ, o+16), inj.be_f(templ, o+20), inj.be_f(templ, o+12)
        sw.append(world[b].dot(np.array([x, y, z, 1.0]))[:3]); sb.append(b)
    T, N, TB = inj.build_surface(ext)
    mapping = inj.npm_surface_mapping(sw, T, N, 1e9)
    labels = ext.get('labels', [])
    from collections import defaultdict
    byb = defaultdict(list)
    for i, b in enumerate(sb):
        d = mapping[i][2] if mapping[i] else 1e9
        byb[b].append(d)
    print("%-16s %5s %6s %6s %6s %6s %6s   %s" % ("label","n","<0.5","<0.8","<1.2","<1.5","med","inyectados con 0.8/1.2/1.5"))
    for b in sorted(byb):
        a = np.array(byb[b]); lab = labels[b] if b < len(labels) else '?'
        c = lambda t: int((a < t).sum())
        print("%-16s %5d %6d %6d %6d %6d %6.2f   %d/%d/%d" % (lab, len(a), c(.5), c(.8), c(1.2), c(1.5), float(np.median(a)), c(.8), c(1.2), c(1.5)))
    allv = np.array([d for v in byb.values() for d in v])
    print("TOTAL slots=%d  <0.8=%d  <1.2=%d  <1.5=%d  med=%.2f" % (
        len(allv), int((allv<.8).sum()), int((allv<1.2).sum()), int((allv<1.5).sum()), float(np.median(allv))))

if __name__ == '__main__': main()
