# -*- coding: utf-8 -*-
"""
swap_cuerpo_hd_v2.py — Transformar geometria de Goten al espacio KLL.

VERSION CORRECTA: usa las world mats del HD de AMBOS esqueletos (zona de ejes
de 80B del AWG0) y transforma cada vertice:
  local_goten --M_goten--> world --inv(M_krillin)--> local_krillin

Esto resuelve el crash del swap anterior (que copiaba vertices crudos sin
transformar al espacio KLL).

Pasos:
  1. Leer world mats de Goten y Krillin (build_hd_world_mats_b3)
  2. Mapear huesos GOT->KLL por labels
  3. Transformar cada vertice del buffer principal (vb) de Goten
  4. Escribir en los slots del bin de Krillin (estructura intacta)

Uso:
  python swap_cuerpo_hd_v2.py <goten.bin> <krillin.bin> <out_amb>
"""

import os
import sys, io, struct
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..\..', 'awo_tools')))
from build_hd_world_mats_b3 import build_hd_world_mats_b3

def u32be(b, o): return struct.unpack('>I', b[o:o+4])[0]
def f32be(b, o): return struct.unpack('>f', b[o:o+4])[0]
def packf(b, o, v): struct.pack_into('>f', b, o, v)

def apply_mat(M, p, v):
    return (M[0][0]*v[0]+M[0][1]*v[1]+M[0][2]*v[2]+p[0],
            M[1][0]*v[0]+M[1][1]*v[1]+M[1][2]*v[2]+p[1],
            M[2][0]*v[0]+M[2][1]*v[1]+M[2][2]*v[2]+p[2])

def inv_rigid(M, p):
    Rt = [[M[j][i] for j in range(3)] for i in range(3)]
    tp = [-sum(Rt[i][j]*p[j] for j in range(3)) for i in range(3)]
    return Rt, tp

def read_labels(d, awo):
    lbl_off = u32be(d, awo+0x24)
    labels = {}
    for bi in range(70):
        s = d[awo+lbl_off+bi*32:awo+lbl_off+bi*32+32]
        s = s.split(b'\x00')[0].decode('latin1', errors='replace')
        if s: labels[bi] = s
    return labels

def build_map(got_labels, kll_labels):
    kll_by_label = {l: i for i, l in kll_labels.items()}
    m = {}
    for gi, l in got_labels.items():
        kl = l.replace('XGTN_','XKLL_').replace('GTN_','KLL_')
        m[gi] = kll_by_label.get(kl, -1)
    return m

def main():
    goten = open(sys.argv[1], 'rb').read()
    krillin = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    awo_g = goten.find(b'#AWO')
    awo_k = 0x40
    tbl_g = u32be(goten, awo_g+0x1C)
    tbl_k = u32be(krillin, awo_k+0x1C)
    awg0_g = u32be(goten, awo_g+tbl_g)
    awg0_k = u32be(krillin, awo_k+tbl_k)

    mats_g = build_hd_world_mats_b3(goten, awo_g, awg0_g)
    mats_k = build_hd_world_mats_b3(krillin, awo_k, awg0_k)
    print('World mats: Goten=%d Krillin=%d' % (len(mats_g), len(mats_k)))

    labels_g = read_labels(goten, awo_g)
    labels_k = read_labels(krillin, awo_k)
    mapping = build_map(labels_g, labels_k)
    print('Huesos GOT->KLL mapeados:', sum(1 for v in mapping.values() if v>=0), '/', len(labels_g))

    # buffers del AWG0
    def awg_buffers(d, awo, awg0):
        h = awo + awg0
        pvb = u32be(d, h+0x28); vbs = u32be(d, h+0x2C)
        return h, pvb, vbs, vbs//44
    hg, pvb_g, vbs_g, n_g = awg_buffers(goten, awo_g, awg0_g)
    hk, pvb_k, vbs_k, n_k = awg_buffers(krillin, awo_k, awg0_k)
    print('Buffers: Goten vb=%d slots | Krillin vb=%d slots' % (n_g, n_k))

    new_bin = bytearray(krillin)
    base_g = awo_g + pvb_g
    base_k = awo_k + pvb_k

    # LAYOUT REAL del vertice (verificado 2026-08-14):
    #   +00 pos.x  +04 pos.y  +08 pos.z  +0C peso
    #   +10 BONE(u32)  +14 nrm.x  +18 nrm.y  +1C nrm.z
    #   +20 0xFFFFFFFF  +24 u  +28 v
    transformed = 0
    unmapped = 0
    for i in range(min(n_g, n_k)):
        o = base_g + i*44
        x = f32be(goten, o+0); y = f32be(goten, o+4); z = f32be(goten, o+8)
        w = f32be(goten, o+0x0C)
        bone_g = u32be(goten, o+0x10)
        nx = f32be(goten, o+0x14); ny = f32be(goten, o+0x18); nz = f32be(goten, o+0x1C)
        u = f32be(goten, o+0x24); v = f32be(goten, o+0x28)
        # saltar padding (todo ceros + flag FFFF)
        if x==0 and y==0 and z==0 and bone_g==0 and nx==0 and ny==0 and nz==0:
            continue
        bone_k = mapping.get(bone_g, -1)
        if bone_k < 0:
            bone_k = 0
            unmapped += 1
        Mg, pg = mats_g.get(bone_g, ([[1,0,0],[0,1,0],[0,0,1]], (0,0,0)))
        Mk, pk = mats_k.get(bone_k, ([[1,0,0],[0,1,0],[0,0,1]], (0,0,0)))
        # local_goten -> world
        world = apply_mat(Mg, pg, (x, y, z))
        # world -> local_krillin
        iM, ip = inv_rigid(Mk, pk)
        lx, ly, lz = apply_mat(iM, ip, world)
        # escribir en slot Krillin con layout real
        ok = base_k + i*44
        packf(new_bin, ok+0, lx); packf(new_bin, ok+4, ly); packf(new_bin, ok+8, lz)
        packf(new_bin, ok+0x0C, w)
        struct.pack_into('>I', new_bin, ok+0x10, bone_k)
        packf(new_bin, ok+0x14, nx); packf(new_bin, ok+0x18, ny); packf(new_bin, ok+0x1C, nz)
        packf(new_bin, ok+0x24, u); packf(new_bin, ok+0x28, v)
        transformed += 1

    print('Vertices transformados: %d | sin mapeo (->bone0): %d' % (transformed, unmapped))
    with open(out, 'wb') as f:
        f.write(bytes(new_bin))
    print('Guardado: %s (%d bytes)' % (out, len(new_bin)))

if __name__ == '__main__':
    main()
