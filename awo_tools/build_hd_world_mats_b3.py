# -*- coding: utf-8 -*-
"""
build_hd_world_mats_b3.py — World mats del AWG B3 (del B1, adaptado).

En el B3, el rig_ptr (+0x14 del AWG) apunta a la ZONA DE EJES: bloques de
80B con quat(+0), pos(+0x10), sello(+0x30), arm_ptr(+0x34), parent(+0x40).
Se componen por jerarquia parent para obtener world mats por bone index.
"""
import sys, io, struct, math

def u32r(b, o): return struct.unpack('>I', b[o:o+4])[0]
def f32r(b, o): return struct.unpack('>f', b[o:o+4])[0]

def quat_to_mat(x, y, z, w):
    xx, xy, xz, xw = x*x, x*y, x*z, x*w
    yy, yz, yw = y*y, y*z, y*w
    zz, zw = z*z, z*w
    return [
        [1-2*(yy+zz), 2*(xy-zw), 2*(xz+yw)],
        [2*(xy+zw), 1-2*(xx+zz), 2*(yz-xw)],
        [2*(xz-yw), 2*(yz+xw), 1-2*(xx+yy)],
    ]

def mat_mul(A, B):
    return [[sum(A[i][k]*B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]

def build_hd_world_mats_b3(bin_bytes, awo, awg_rel):
    awg_abs = awo + awg_rel
    nb = u32r(bin_bytes, awg_abs + 0x10)
    rig_rel = u32r(bin_bytes, awg_abs + 0x14)
    axes_start = awg_abs + rig_rel
    n_axes = 0
    # contar ejes: bloques 80B consecutivos con sello valido
    for i in range(200):
        o = axes_start + i*0x50
        sello = u32r(bin_bytes, o + 0x30)
        arm = u32r(bin_bytes, o + 0x34)
        if not (0x200 <= (sello & 0xFFFFFF) <= 0x1000) or not arm:
            break
        n_axes += 1
    if n_axes == 0:
        n_axes = nb
    eje = {}
    for i in range(n_axes):
        o = axes_start + i*0x50
        q = [f32r(bin_bytes, o+j*4) for j in range(4)]
        p = [f32r(bin_bytes, o+16+j*4) for j in range(3)]
        sello = u32r(bin_bytes, o+0x30)
        arm = u32r(bin_bytes, o+0x34)
        par = u32r(bin_bytes, o+0x40)
        bone = -1
        if arm and awg_abs+arm+8 <= len(bin_bytes):
            bone = u32r(bin_bytes, awg_abs+arm)
        eje[i] = {'bone': bone, 'parent': par, 'quat': q, 'pos': p}
    cache = {}
    def world(i):
        if i in cache: return cache[i]
        e = eje.get(i)
        if not e: cache[i]=None; return None
        m = quat_to_mat(e['quat'][0], e['quat'][1], e['quat'][2], e['quat'][3])
        p = list(e['pos'])
        pr = e['parent']
        if pr:
            # parent es rel AWG; buscar el eje cuyo offset coincide
            for j2, e2 in eje.items():
                if axes_start + j2*0x50 - awg_abs == pr - awg_rel:
                    pw = world(j2)
                    if pw:
                        pm, pp = pw
                        m = mat_mul(pm, m)
                        p = [pm[0][0]*p[0]+pm[0][1]*p[1]+pm[0][2]*p[2]+pp[0],
                             pm[1][0]*p[0]+pm[1][1]*p[1]+pm[1][2]*p[2]+pp[1],
                             pm[2][0]*p[0]+pm[2][1]*p[1]+pm[2][2]*p[2]+pp[2]]
                    break
        cache[i] = (m, p)
        return m, p
    mats = {}
    for i, e in eje.items():
        if e['bone'] >= 0:
            wm = world(i)
            if wm: mats[e['bone']] = wm
    return mats

if __name__ == '__main__':
    path = sys.argv[1]
    d = open(path, 'rb').read()
    awo = d.find(b'#AWO') if '#AWO' in path else 0x40
    awo = 0x40
    tbl = u32r(d, awo+0x1C)
    awg0_rel = u32r(d, awo+tbl)
    print('AWO@0x%X AWG0@0x%X' % (awo, awg0_rel))
    mats = build_hd_world_mats_b3(d, awo, awg0_rel)
    print('World mats B3: %d huesos' % len(mats))
    for i in sorted(mats)[:10]:
        m, p = mats[i]
        print('  bone %d: pos=(%.3f, %.3f, %.3f)' % (i, p[0], p[1], p[2]))
