#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""awg_parts.py - Listar por-AWG la geometria de un bin HD B3: bounds, huesos
usados, y exportar cada AWG a OBJ separado para identificar cabeza/manos/pies.

Uso:
  python awg_parts.py <bin_amb_hd> [--outdir <dir>] [--obj]
"""
import struct, sys, os, io
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
from awg_to_obj_b3 import (u32, u16, f32, find_awo, world_mats_from_axes,
                           quat_to_mat, mat_mul, apply)

def parse_awg(d, awo, awg0_abs):
    sec_rel = u32(d, awg0_abs + 0x34)
    vb2_rel = u32(d, awg0_abs + 0x2C)
    ib_rel = u32(d, awg0_abs + 0x30)
    end_rel = u32(d, awg0_abs + 0x38)
    nb = u32(d, awg0_abs + 0x10)
    return {
        'nb': nb,
        'sec_abs': awg0_abs + sec_rel + 2,
        'vb2_abs': awg0_abs + vb2_rel,
        'ib_abs': awg0_abs + ib_rel,
        'n_sec': (vb2_rel - sec_rel - 2) // 44,
        'n_vb2': (ib_rel - vb2_rel) // 44,
        'n_ib': (end_rel - ib_rel) // 2,
    }


def awg_bounds(d, awg):
    sec = awg['sec_abs']
    n = awg['n_sec']
    # leer posiciones: sec34 local [z,x,y] en +12/+16/+20; bone en +28
    mn = [1e9, 1e9, 1e9]; mx = [-1e9, -1e9, -1e9]
    bones = set()
    for i in range(n):
        o = sec + i*44
        x = f32(d, o+16); y = f32(d, o+20); z = f32(d, o+12)
        bone = u32(d, o+28)
        bones.add(bone if bone != 0xFFFFFFFF else -1)
        for k, v in enumerate((x, y, z)):
            if v < mn[k]: mn[k] = v
            if v > mx[k]: mx[k] = v
    return mn, mx, bones


def export_awg_obj(d, awo, awg0_abs, path, mats):
    st = parse_awg(d, awo, awg0_abs)
    verts = []
    for i in range(st['n_sec']):
        o = st['sec_abs'] + i*44
        x = f32(d, o+16); y = f32(d, o+20); z = f32(d, o+12)
        bone = u32(d, o+28)
        if bone != 0xFFFFFFFF and bone < st['nb']:
            m, p = mats.get(bone, ([[1,0,0],[0,1,0],[0,0,1]], (0,0,0)))
            x, y, z = apply(m, p, (x, y, z))
        verts.append((x, y, z))
    for i in range(st['n_vb2']):
        o = st['vb2_abs'] + i*44
        verts.append((f32(d, o), f32(d, o+4), f32(d, o+8)))
    ib = [u16(d, st['ib_abs'] + i*2) for i in range(st['n_ib'])]
    tris = []
    i = 0
    while i + 2 < len(ib):
        a, b, c = ib[i], ib[i+1], ib[i+2]
        if 0xFFFF in (a, b, c) or a >= len(verts) or b >= len(verts) or c >= len(verts):
            i += 3; continue
        tris.append((a, b, c)); i += 3
    with open(path, 'w') as f:
        for x, y, z in verts:
            f.write('v %.6f %.6f %.6f\n' % (x, y, z))
        for a, b, c in tris:
            f.write('f %d %d %d\n' % (a+1, b+1, c+1))
    return len(verts), len(tris)


def main():
    path = sys.argv[1]
    outdir = None
    do_obj = '--obj' in sys.argv
    if '--outdir' in sys.argv:
        outdir = sys.argv[sys.argv.index('--outdir') + 1]
        os.makedirs(outdir, exist_ok=True)
    d = open(path, 'rb').read()
    awo = find_awo(d)
    n_awg = u32(d, awo + 0x18)
    tbl = u32(d, awo + 0x1C)
    print('AWO@0x%X awgs=%d bones=%d' % (awo, n_awg, u32(d, awo + 0x10)))
    for i in range(n_awg):
        off = u32(d, awo + tbl + i*4)
        h = awo + off
        if d[h:h+4] != b'#AWG':
            print('AWG%d: no es #AWG' % i); continue
        st = parse_awg(d, awo, h)
        mn, mx, bones = awg_bounds(d, st)
        span = [mx[k] - mn[k] for k in range(3)]
        tag = ''
        cy = (mn[1] + mx[1]) / 2
        if mx[1] > 2.5 and span[1] < 3: tag = 'CABEZA?'
        elif mn[1] < 0.3 and mx[1] > 1.0 and span[2] > 1.5: tag = 'PIERNAS?'
        elif span[0] < 1.5 and span[1] < 1.5: tag = 'MANO/PIE?'
        print('AWG%-2d nb=%-3d sec=%-4d vb2=%-3d ib=%-4d bones=%-12s x[%6.2f..%6.2f] y[%6.2f..%6.2f] z[%6.2f..%6.2f] %s' % (
            i, st['nb'], st['n_sec'], st['n_vb2'], st['n_ib'],
            str(sorted(bones))[:26], mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], tag))
        if do_obj and outdir:
            mats = world_mats_from_axes(d, h, st['nb']) if st['nb'] > 1 else {0: ([[1,0,0],[0,1,0],[0,0,1]], (0,0,0))}
            p = os.path.join(outdir, '%s_awg%02d.obj' % (os.path.splitext(os.path.basename(path))[0], i))
            nv, nt = export_awg_obj(d, awo, h, p, mats)
            print('   -> %s (%d verts, %d tris)' % (p, nv, nt))


if __name__ == '__main__':
    main()