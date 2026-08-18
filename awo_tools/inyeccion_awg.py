# -*- coding: utf-8 -*-
"""
inyeccion_awg.py — Inyectar geometria de Janemba en el buffer principal (vb2)
del AWG0 de Krillin, manteniendo el mesh group/IB intacto.

Estrategia (pragmatica, basada en mix1 que funciono + override por entrada):
  1. Parsear Janemba.amb AMG0 -> verts+IB
  2. Skin por hueso (parse rig PS2) -> (bone JNB, weight) por vert
  3. Transformar local JNB -> world -> local KLL (world mats de ambos)
  4. Llenar los slots del vb2 de Krillin (2190 slots, stride 44, bone@+28)
  5. Si Janemba tiene mas verts que slots, decimar; si menos, rellenar

Uso:
  python inyeccion_awg.py <janemba.amb> <krillin_hd> <krillin_ps2> <out>
"""

import os
import sys, io, struct
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..\..', 'awo_tools')))

from build_awo_desde_cero import (parse_ps2_amg, read_labels_ps2, read_labels_hd,
    build_mapping, build_vertex_hd, build_world_mats_ps2, inv_rigid, apply_mat)

def r32(b, o): return struct.unpack_from('<I', b, o)[0]
def rf(b, o): return struct.unpack_from('<f', b, o)[0]
def u32be(b, o): return struct.unpack('>I', b[o:o+4])[0]
def u32(o): return struct.unpack('>I', d[o:o+4])[0] if False else None

def main():
    janemba = open(sys.argv[1], 'rb').read()
    krillin_hd = open(sys.argv[2], 'rb').read()
    krillin_ps2 = open(sys.argv[3], 'rb').read()
    out = sys.argv[4]

    # ---- Parsear AMG0 de Janemba (cuerpo) ----
    amo0 = 64
    if janemba[amo0:amo0+4] not in (b'#AMO0', b'#AMO'):
        for i in range(len(janemba)-4):
            if janemba[i:i+4] == b'#AMO0': amo0 = i; break
    n_amg = r32(janemba, amo0+0x18)
    amg_table = r32(janemba, amo0+0x1C)
    if amg_table < 0x100: amg_table = amo0+0x30
    else: amg_table = amo0+amg_table
    amg0_off = r32(janemba, amg_table+0)  # AMG0 = cuerpo
    amg0_abs = amo0 + amg0_off
    print('Janemba AMG0 @0x%X' % amg0_abs)
    verts, tris = parse_ps2_amg(janemba, amg0_abs)
    print('  AMG0: %d verts, %d tris' % (len(verts), len(tris)))

    # ---- Skin: asignar hueso JNB a cada vert ----
    # (parse_ps2_mesh no da skin; usamos la aproximacion: bone del part)
    # Para simplificar: el AMG0 de Janemba tiene 21 parts; asignamos bone por
    # posicion del part en el AMG. Aqui usamos bone 0 (BODY) como base y luego
    # re-mapeamos.

    # ---- Labels y mapping ----
    kll_labels = read_labels_hd(krillin_hd)
    jnb_labels = read_labels_ps2(janemba, amg0_abs)
    mapping = build_mapping(jnb_labels, kll_labels)
    print('Mapeo JNB->KLL: %d huesos' % sum(1 for v in mapping.values() if v >= 0))

    # ---- World mats ----
    mats_jnb = build_world_mats_ps2(janemba, amo0)
    mats_kll = build_world_mats_ps2(krillin_ps2, 0x40)
    print('World mats: JNB=%d KLL=%d' % (len(mats_jnb), len(mats_kll)))

    # ---- Slots del vb2 de Krillin ----
    d = krillin_hd
    awo = 0x40; tbl = u32be(d, awo+0x1C); off0 = u32be(d, awo+tbl); h = awo+off0
    pvb = u32be(d, h+0x28); vbs = u32be(d, h+0x2C)
    n_slots = vbs//44
    print('Krillin AWG0 vb2: %d slots' % n_slots)

    # ---- Transformar verts de Janemba -> local KLL ----
    hd_verts = []
    for v in verts:
        # usar bone 0 (BODY) para todo el cuerpo (aprox; el skin fino requiere rig)
        bone_jnb = 0
        bone_kll = mapping.get(bone_jnb, 0)
        # local JNB -> world
        Mj, pj = mats_jnb.get(bone_jnb, ([[1,0,0],[0,1,0],[0,0,1]], (0,0,0)))
        world = apply_mat(Mj, pj, v['pos'])
        # world -> local KLL
        Mk, pk = mats_kll.get(bone_kll, ([[1,0,0],[0,1,0],[0,0,1]], (0,0,0)))
        iM, ip = inv_rigid(Mk, pk)
        local = apply_mat(iM, ip, world)
        hd_verts.append(build_vertex_hd(local, bone_kll, 1.0, v['nrm'], v['uv']))

    # ---- Rellenar slots (decimar si hace falta) ----
    if len(hd_verts) > n_slots:
        # decimacion voxel
        step = max(1, len(hd_verts)//n_slots)
        hd_verts = hd_verts[::step][:n_slots]
        print('  Decimado a %d' % len(hd_verts))
    pad = n_slots - len(hd_verts)
    if pad > 0:
        pad_vtx = (struct.pack('>f', float('nan')) + struct.pack('>f',0)*2 +
                   struct.pack('>f',0)*3 + struct.pack('>f',1.0) +
                   struct.pack('>I', 0) + struct.pack('>f',0)*3)
        hd_verts += [pad_vtx]*pad
        print('  Rellenado %d slots' % pad)

    # ---- Escribir en el bin ----
    new_bin = bytearray(krillin_hd)
    base = awo + pvb
    for i, vtx in enumerate(hd_verts):
        new_bin[base+i*44 : base+i*44+44] = vtx
    print('Escrito %d slots en vb2 @0x%X' % (n_slots, base))

    with open(out, 'wb') as f:
        f.write(bytes(new_bin))
    print('Guardado: %s (%d bytes)' % (out, len(new_bin)))

if __name__ == '__main__':
    main()
