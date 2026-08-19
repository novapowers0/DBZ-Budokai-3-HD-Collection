#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""awg_to_obj_b3.py - Exportar un bin HD B3 (#AMB/#AWO) a OBJ, AUTOCONTENIDO.

NO necesita el PS2 de referencia: las matrices world se reconstruyen desde
los ejes del propio bin (verificado 51/51 contra PS2 en Krillin).

Layout verificado empiricamente (X360, AGENTS 3.2 / 12.3):
  AWG0 header (rel #AWG):
    +0x10 n_bones | +0x14 axes_rel | +0x18 n_groups | +0x1C
    +0x20 mg_off | +0x24 | +0x28 mg_size
    +0x2C vb2_rel | +0x30 ib_rel | +0x34 sec34_rel | +0x38 end_rel | +0x3C
  Eje (80B): quat.xyz@0, w@0xC, pos.xyz@0x10, [5x1.0]@0x1C, sello@0x30,
    arm_ptr@0x34, p38@0x38, child?@0x3C, PARENT_ptr@0x40 (rel AWG0), ...
  Vertice sec34 (44B): [0xFFFFFFFF, u, v, z, x, y, peso, BONE, nz, -ny, nx]
    (posiciones LOCALES por hueso, datos en sec34_rel+2 por el align)
  Vertice vb2 (44B):   [x, y, z, 0,0,0, 0, 0xFFFFFFFF, nx, ny, nz]
    (posiciones ABSOLUTAS, sin skinning)

Uso:
  python awg_to_obj_b3.py <bin_amb_hd> [salida.obj] [--skip-oob] [--no-skin]
"""
import struct, sys, io, os

def u32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def u16(b, o): return struct.unpack('>H', b[o:o+2])[0]
def f32(b, o): return struct.unpack('>f', b[o:o+4])[0]


def quat_to_mat(x, y, z, w):
    xx, xy, xz, xw = x*x, x*y, x*z, x*w
    yy, yz, yw = y*y, y*z, y*w
    zz, zw = z*z, z*w
    return [
        [1 - 2*(yy+zz), 2*(xy-zw),   2*(xz+yw)],
        [2*(xy+zw),   1 - 2*(xx+zz), 2*(yz-xw)],
        [2*(xz-yw),   2*(yz+xw),   1 - 2*(xx+yy)],
    ]


def mat_mul(A, B):
    return [[sum(A[i][k]*B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def apply(m, p, v):
    return (m[0][0]*v[0]+m[0][1]*v[1]+m[0][2]*v[2]+p[0],
            m[1][0]*v[0]+m[1][1]*v[1]+m[1][2]*v[2]+p[1],
            m[2][0]*v[0]+m[2][1]*v[1]+m[2][2]*v[2]+p[2])


def find_awo(d):
    if d[:4] == b'#AMB':
        return 0x40
    i = d.find(b'#AWO')
    return i


def world_mats_from_axes(d, awg0, n_bones):
    """Construye las matrices world (rot3x3, pos3) de cada hueso usando los
    ejes del AWG0 HD (quat+pos locales + puntero padre en +0x40)."""
    axes_rel = u32(d, awg0 + 0x14)
    axes_abs = awg0 + axes_rel
    axes = []
    for i in range(n_bones):
        e = axes_abs + i*80
        q = [f32(d, e+j*4) for j in range(3)]
        w = f32(d, e+0x0C)
        pos = [f32(d, e+0x10+j*4) for j in range(3)]
        parent = u32(d, e+0x40)
        axes.append((q, w, pos, parent))

    def idx_of_ptr(ptr):
        if not ptr:
            return None
        rel = ptr - axes_rel
        if rel < 0 or rel % 80:
            return None
        return rel // 80

    cache = {}
    def wm(i):
        if i in cache:
            return cache[i]
        q, w, pos, parent = axes[i]
        m = quat_to_mat(q[0], q[1], q[2], -w)
        p = pos[:]
        pi = idx_of_ptr(parent)
        if pi is not None and pi != i and 0 <= pi < n_bones:
            pm, pp = wm(pi)
            m = mat_mul(pm, m)
            p = [pm[0][0]*p[0]+pm[0][1]*p[1]+pm[0][2]*p[2]+pp[0],
                 pm[1][0]*p[0]+pm[1][1]*p[1]+pm[1][2]*p[2]+pp[1],
                 pm[2][0]*p[0]+pm[2][1]*p[1]+pm[2][2]*p[2]+pp[2]]
        cache[i] = (m, p)
        return m, p

    mats = {}
    for i in range(n_bones):
        mats[i] = wm(i)
    return mats


def parse_awg0(d, awo):
    """Devuelve dict con la estructura AWG0 (el cuerpo)."""
    n_awg = u32(d, awo + 0x18)
    tbl = u32(d, awo + 0x1C)
    awg_off = u32(d, awo + tbl)
    awg0 = awo + awg_off
    n_bones = u32(d, awg0 + 0x10)
    sec_rel = u32(d, awg0 + 0x34)
    vb2_rel = u32(d, awg0 + 0x2C)
    ib_rel = u32(d, awg0 + 0x30)
    end_rel = u32(d, awg0 + 0x38)
    mg_off = u32(d, awg0 + 0x20)
    mg_size = u32(d, awg0 + 0x28)

    sec_abs = awg0 + sec_rel + 2
    vb2_abs = awg0 + vb2_rel
    ib_abs = awg0 + ib_rel
    n_sec = (vb2_rel - sec_rel - 2) // 44
    n_vb2 = (ib_rel - vb2_rel) // 44
    n_ib = (end_rel - ib_rel) // 2

    return {
        'awg0': awg0, 'n_bones': n_bones, 'n_awg': n_awg,
        'sec_abs': sec_abs, 'vb2_abs': vb2_abs, 'ib_abs': ib_abs,
        'n_sec': n_sec, 'n_vb2': n_vb2, 'n_ib': n_ib,
        'mg_off': mg_off, 'mg_size': mg_size,
        'end_abs': awg0 + end_rel, 'file_size': len(d),
    }


def read_sec34(d, st, n):
    """Lee vertices sec34 (44B): (local, bone, u, v)."""
    verts = []
    for i in range(n):
        o = st + i*44
        marker = u32(d, o)
        u = f32(d, o+4); v = f32(d, o+8)
        z = f32(d, o+12); x = f32(d, o+16); y = f32(d, o+20)
        bone = u32(d, o+28)
        verts.append((x, y, z, bone, u, v, marker))
    return verts


def read_vb2(d, st, n):
    """Lee vertices vb2 (44B): posiciones absolutas."""
    verts = []
    for i in range(n):
        o = st + i*44
        x = f32(d, o); y = f32(d, o+4); z = f32(d, o+8)
        bone = u32(d, o+28)
        verts.append((x, y, z, bone))
    return verts


def build_mesh(d, st, skin=True):
    """Devuelve (verts_world, tris) para el AWG0."""
    sec = read_sec34(d, st['sec_abs'], st['n_sec'])
    vb2 = read_vb2(d, st['vb2_abs'], st['n_vb2'])
    mats = world_mats_from_axes(d, st['awg0'], st['n_bones'])

    # stream combinado: [sec34 (world)] + [vb2 (world)]
    verts = []
    for x, y, z, bone, u, v, marker in sec:
        if skin and bone != 0xFFFFFFFF and bone < st['n_bones']:
            m, p = mats[bone]
            wx, wy, wz = apply(m, p, (x, y, z))
        else:
            wx, wy, wz = x, y, z
        verts.append((wx, wy, wz, u, v))
    for x, y, z, bone in vb2:
        verts.append((x, y, z, 0.0, 0.0))

    ib = [u16(d, st['ib_abs'] + i*2) for i in range(st['n_ib'])]
    # triangulos: list (3 a 3), saltando restart 0xFFFF e indices OOB
    tris = []
    n_stream = len(verts)
    oob = 0
    i = 0
    while i + 2 < len(ib):
        a, b, c = ib[i], ib[i+1], ib[i+2]
        if 0xFFFF in (a, b, c):
            i += 3
            continue
        if a >= n_stream or b >= n_stream or c >= n_stream:
            oob += 1
            i += 3
            continue
        tris.append((a, b, c))
        i += 3
    return verts, tris, oob


def export_obj(d, path, skin=True):
    st = parse_awg0(d, 0x40 if d[:4] == b'#AMB' else find_awo(d))
    verts, tris, oob = build_mesh(d, st, skin)
    with open(path, 'w') as f:
        f.write('# awg_to_obj_b3 export (autocontenido)\n')
        f.write('# AWG0 sec34=%d vb2=%d ib=%d tris=%d oob_skip=%d\n' % (
            st['n_sec'], st['n_vb2'], st['n_ib'], len(tris), oob))
        for x, y, z, u, v in verts:
            f.write('v %.6f %.6f %.6f\n' % (x, y, z))
            f.write('vt %.6f %.6f\n' % (u, v))
        for a, b, c in tris:
            f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (
                a+1, a+1, a+1, b+1, b+1, b+1, c+1, c+1, c+1))
    return st, len(verts), len(tris), oob


def main():
    if len(sys.argv) < 2:
        print('Uso: python awg_to_obj_b3.py <bin_amb_hd> [salida.obj] [--no-skin]')
        return
    d = open(sys.argv[1], 'rb').read()
    skin = '--no-skin' not in sys.argv
    out = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith('--') \
        else os.path.splitext(sys.argv[1])[0] + '.obj'
    st, nv, nt, oob = export_obj(d, out, skin)
    print('OK %s: sec34=%d vb2=%d ib=%d -> %d verts, %d tris (oob_skip=%d)' % (
        os.path.basename(out), st['n_sec'], st['n_vb2'], st['n_ib'], nv, nt, oob))


if __name__ == '__main__':
    main()