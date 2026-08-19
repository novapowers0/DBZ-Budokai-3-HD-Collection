#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""awg_parts2.py - Exportar AWGs de un bin HD B3 a OBJ, detectando el formato
de vertice por AWG (A: sec34 marker-FFFF@+0; C: posiciones@+0).

Uso:
  python awg_parts2.py <bin_amb_hd> <outdir> [--awg N]
"""
import struct, sys, os, re

def u32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def u16(b, o): return struct.unpack('>H', b[o:o+2])[0]
def f32(b, o): return struct.unpack('>f', b[o:o+4])[0]

def quat_to_mat(x, y, z, w):
    xx, xy, xz, xw = x*x, x*y, x*z, x*w
    yy, yz, yw = y*y, y*z, y*w
    zz, zw = z*z, z*w
    return [[1-2*(yy+zz), 2*(xy-zw), 2*(xz+yw)],
            [2*(xy+zw), 1-2*(xx+zz), 2*(yz-xw)],
            [2*(xz-yw), 2*(yz+xw), 1-2*(xx+yy)]]

def mat_mul(A, B):
    return [[sum(A[i][k]*B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]

def apply(m, p, v):
    return (m[0][0]*v[0]+m[0][1]*v[1]+m[0][2]*v[2]+p[0],
            m[1][0]*v[0]+m[1][1]*v[1]+m[1][2]*v[2]+p[1],
            m[2][0]*v[0]+m[2][1]*v[1]+m[2][2]*v[2]+p[2])

def world_mats_from_axes(d, awg0, n_bones):
    axes_rel = u32(d, awg0 + 0x14); axes_abs = awg0 + axes_rel
    axes = []
    for i in range(n_bones):
        e = axes_abs + i*80
        q = [f32(d, e+j*4) for j in range(3)]; w = f32(d, e+0x0C)
        pos = [f32(d, e+0x10+j*4) for j in range(3)]; parent = u32(d, e+0x40)
        axes.append((q, w, pos, parent))
    def idx_of_ptr(ptr):
        if not ptr: return None
        rel = ptr - axes_rel
        return rel // 80 if rel >= 0 and rel % 80 == 0 else None
    cache = {}
    def wm(i):
        if i in cache: return cache[i]
        q, w, pos, parent = axes[i]
        m = quat_to_mat(q[0], q[1], q[2], -w); p = pos[:]
        pi = idx_of_ptr(parent)
        if pi is not None and pi != i and 0 <= pi < n_bones:
            pm, pp = wm(pi); m = mat_mul(pm, m)
            p = [pm[0][0]*p[0]+pm[0][1]*p[1]+pm[0][2]*p[2]+pp[0],
                 pm[1][0]*p[0]+pm[1][1]*p[1]+pm[1][2]*p[2]+pp[1],
                 pm[2][0]*p[0]+pm[2][1]*p[1]+pm[2][2]*p[2]+pp[2]]
        cache[i] = (m, p); return m, p
    return {i: wm(i) for i in range(n_bones)}

def detect_format(d, sec_abs, n_sec):
    """Detecta formato del buffer sec34: 'A' si marker FFFF en +0, 'C' si posiciones en +0."""
    if n_sec == 0: return 'A'
    o = sec_abs
    m0 = u32(d, o)            # marker en +0
    m28 = u32(d, o+28)        # bone/FFFF en +28
    # formato A: marker 0xFFFFFFFF en +0
    if m0 == 0xFFFFFFFF:
        return 'A'
    # formato C: marker 0xFFFFFFFF en +28
    if m28 == 0xFFFFFFFF:
        return 'C'
    # heuristica: si el campo +28 es nan (FF) -> C; si hay marker FFFF en +0 en >=50% -> A
    ff_count = 0
    for i in range(min(n_sec, 20)):
        if u32(d, sec_abs + i*44) == 0xFFFFFFFF: ff_count += 1
    if ff_count >= 10: return 'A'
    return 'C'

def read_verts(d, awg, mats):
    """Lee los vertices del AWG segun formato detectado. Devuelve lista (x,y,z)."""
    fmt = awg['fmt']
    verts = []
    nb = awg['nb']
    for i in range(awg['n_sec']):
        o = awg['sec_abs'] + i*44
        if fmt == 'A':
            # [FFFF, u,v, z,x,y, peso, BONE, nz,-ny,nx]  pos local en +16/+20/+12
            x = f32(d, o+16); y = f32(d, o+20); z = f32(d, o+12)
            bone = u32(d, o+28)
            if bone != 0xFFFFFFFF and bone < nb:
                m, p = mats.get(bone, ([[1,0,0],[0,1,0],[0,0,1]], (0,0,0)))
                x, y, z = apply(m, p, (x, y, z))
            verts.append((x, y, z))
        else:  # formato C: posiciones en +0
            x = f32(d, o); y = f32(d, o+4); z = f32(d, o+8)
            verts.append((x, y, z))
    for i in range(awg['n_vb2']):
        o = awg['vb2_abs'] + i*44
        if fmt == 'A':
            x = f32(d, o); y = f32(d, o+4); z = f32(d, o+8)
        else:
            x = f32(d, o); y = f32(d, o+4); z = f32(d, o+8)
        verts.append((x, y, z))
    return verts

def main():
    path = sys.argv[1]; outdir = sys.argv[2]
    sel = None
    if '--awg' in sys.argv: sel = int(sys.argv[sys.argv.index('--awg')+1])
    os.makedirs(outdir, exist_ok=True)
    d = open(path, 'rb').read()
    awo = 0x40 if d[:4]==b'#AMB' else d.find(b'#AWO')
    n_awg = u32(d, awo+0x18); tbl = u32(d, awo+0x1C)
    base = os.path.splitext(os.path.basename(path))[0]
    for i in range(n_awg):
        if sel is not None and i != sel: continue
        off = u32(d, awo+tbl+i*4); h = awo+off
        if d[h:h+4] != b'#AWG': continue
        nb = u32(d, h+0x10)
        sec_rel = u32(d, h+0x34); vb2_rel = u32(d, h+0x2C)
        ib_rel = u32(d, h+0x30); end_rel = u32(d, h+0x38)
        awg = {'nb': nb,
               'sec_abs': h + sec_rel + 2, 'vb2_abs': h + vb2_rel,
               'ib_abs': h + ib_rel,
               'n_sec': (vb2_rel - sec_rel - 2)//44,
               'n_vb2': (ib_rel - vb2_rel)//44,
               'n_ib': (end_rel - ib_rel)//2}
        awg['fmt'] = detect_format(d, awg['sec_abs'], awg['n_sec'])
        mats = world_mats_from_axes(d, h, nb) if nb > 1 else {}
        verts = read_verts(d, awg, mats)
        ib = [u16(d, awg['ib_abs'] + k*2) for k in range(awg['n_ib'])]
        tris = []
        k = 0
        while k + 2 < len(ib):
            a, b, c = ib[k], ib[k+1], ib[k+2]
            if 0xFFFF in (a, b, c) or a >= len(verts) or b >= len(verts) or c >= len(verts):
                k += 3; continue
            tris.append((a, b, c)); k += 3
        # bounds
        if verts:
            xs = [v[0] for v in verts]; ys = [v[1] for v in verts]; zs = [v[2] for v in verts]
            bstr = 'x[%6.2f..%6.2f] y[%6.2f..%6.2f] z[%6.2f..%6.2f]' % (
                min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))
        else:
            bstr = 'sin verts'
        with open(os.path.join(outdir, '%s_awg%02d.obj' % (base, i)), 'w') as f:
            for x, y, z in verts:
                f.write('v %.6f %.6f %.6f\n' % (x, y, z))
            for a, b, c in tris:
                f.write('f %d %d %d\n' % (a+1, b+1, c+1))
        print('AWG%-2d fmt=%s nb=%-3d sec=%-4d vb2=%-3d ib=%-4d tris=%-4d %s' % (
            i, awg['fmt'], nb, awg['n_sec'], awg['n_vb2'], awg['n_ib'], len(tris), bstr))

if __name__ == '__main__':
    main()