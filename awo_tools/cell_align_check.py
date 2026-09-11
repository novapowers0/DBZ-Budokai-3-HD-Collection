#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""cell_align_check.py - Compara posiciones por hueso: slots HD (world) vs
vertices PS2 (model-space, via skin). Si los centroides difieren mucho por hueso,
la plantilla HD y el modelo PS2 no comparten pose/forma en ese hueso.

Uso: python cell_align_check.py <plantilla.bin> <extract.json>
"""
import sys, json, struct
import numpy as np
sys.path.insert(0, r'C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\mod center hd\ports')
import port_ps2_b3_inject as inj

def main():
    templ = open(sys.argv[1], 'rb').read()
    ext = json.load(open(sys.argv[2]))
    world, AWG0 = inj.world_mats(bytearray(templ), 0x40)
    sec_rel = inj.be32(templ, AWG0 + 0x34); vb2_rel = inj.be32(templ, AWG0 + 0x2C)
    sec = AWG0 + sec_rel + 2; n = (vb2_rel - sec_rel - 2)//44
    hd = {}
    for i in range(n):
        o = sec + i*44
        b = inj.be32(templ, o + 28)
        x, y, z = inj.be_f(templ, o+16), inj.be_f(templ, o+20), inj.be_f(templ, o+12)
        w = world[b].dot(np.array([x, y, z, 1.0]))[:3]
        hd.setdefault(b, []).append(w)
    hdc = {b: np.mean(v, axis=0) for b, v in hd.items()}
    # PS2 centroids by skin bone
    skin = ext['skin']
    ps = {}
    for p in ext['parts']:
        for v in p['verts']:
            oa = v[0]
            sv = skin.get(str(oa)) or skin.get(oa)
            b = int(sv[0]) if sv else p['bone']
            ps.setdefault(b, []).append([v[1], v[2], v[3]])
    psc = {b: np.mean(np.array(v), axis=0) for b, v in ps.items()}
    labels = ext.get('labels', [])
    print("%-14s %-3s %-28s %-28s %s" % ("label", "bn", "HD_world(x,y,z)", "PS2_model(x,y,z)", "dist"))
    tot = []
    for b in sorted(set(hdc) | set(psc)):
        lab = labels[b] if b < len(labels) else '?'
        a = hdc.get(b); c = psc.get(b)
        if a is None or c is None:
            print("%-14s %-3d %-28s %-28s %s" % (lab, b, '%.2f,%.2f,%.2f'%tuple(a) if a is not None else '-', '%.2f,%.2f,%.2f'%tuple(c) if c is not None else '-', 'solo uno')); continue
        d = float(np.linalg.norm(a-c)); tot.append(d)
        print("%-14s %-3d %-28s %-28s %.2f" % (lab, b, '%.2f,%.2f,%.2f'%tuple(a), '%.2f,%.2f,%.2f'%tuple(c), d))
    print("mediana dist centroides = %.2f  max = %.2f" % (float(np.median(tot)), float(np.max(tot))))

if __name__ == '__main__': main()
