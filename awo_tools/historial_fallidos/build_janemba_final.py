# -*- coding: utf-8 -*-
"""
build_janemba_final.py — Construir el AWO HD de Janemba completo.

Estrategia validada (Piccolo B1 que funciono + override por entrada):
  - Reutilizar la ESTRUCTURA del bin e326 de Krillin (mesh group, headers,
    labels, zonas, rigData, arms) como plantilla EXACTA.
  - Reemplazar SOLO el contenido del buffer principal (vb2) del AWG0 con la
    geometria de Janemba, con skin por hueso (bone del part PS2 -> KLL).
  - Mantener IB/sec34/mesh group intactos (el guest dibuja por ellos).

Diferencia vs v7/mix1: aqui el skin por hueso se hace CORRECTO (cada part
del AMG0 PS2 asigna su bone), transformando local JNB -> world -> local KLL
con las matrices de pose de ambos esqueletos (mismas poses, 51/51).

Uso:
  python build_janemba_final.py <janemba.amb> <krillin_hd> <krillin_ps2> <out_amb>
"""

import sys, io, struct
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
sys.path.insert(0, r'C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\awo_tools')

from build_awo_desde_cero import (parse_ps2_amg, read_labels_ps2, read_labels_hd,
    build_mapping, build_vertex_hd, build_world_mats_ps2, inv_rigid, apply_mat,
    read_vert, VERT_STRIDE)

def r32(b, o): return struct.unpack_from('<I', b, o)[0]
def u32be(b, o): return struct.unpack('>I', b[o:o+4])[0]

def parse_amg0_with_parts(b, amg_abs):
    """Parsea el AMG0 devolviendo verts con su bone (del part)."""
    bone_am = r32(b, amg_abs+0x10)
    axes_loc = r32(b, amg_abs+0x14)
    all_verts = []
    part_bones = []  # (bone, nverts) por part
    for bi in range(bone_am):
        e0 = amg_abs + axes_loc + bi*80
        p34 = r32(b, e0+0x34)
        if not p34: continue
        arm = amg_abs + p34
        mesh_hdr = r32(b, arm+4)
        if not mesh_hdr: continue
        mg = amg_abs + mesh_hdr
        mp_amnt = r32(b, mg)
        if not mp_amnt or mp_amnt > 64: continue
        part_offs = [r32(b, mg+16+i*4) for i in range(mp_amnt)]
        for rel in part_offs:
            po = mg + rel
            size_field = r32(b, po+0x90)
            mesh_size = (size_field - 0x60000000)*16 if size_field >= 0x60000000 else 0
            first_type = r32(b, po)
            vtype = 0xB5
            for vt in VERT_STRIDE:
                if first_type & 0xFF == vt: vtype = vt; break
            stride = VERT_STRIDE.get(vtype, 48)
            md = po + 0xA0
            end = md + mesh_size if mesh_size > 0 else po + 0x400
            pos = md
            n_before = len(all_verts)
            while pos+0x20 < min(end, len(b)):
                facetype = r32(b, pos+0x10)
                vertcount = r32(b, pos+0x14)
                if not vertcount or vertcount > 0xFFFF: break
                vpos = pos + 0x20
                if vpos + vertcount*stride > min(end, len(b)): break
                for x in range(vertcount):
                    p, n, u = read_vert(b, vpos, vtype)
                    all_verts.append({'pos': p, 'nrm': n, 'uv': u, 'bone': bi})
                    vpos += stride
                pos = vpos
            part_bones.append((bi, len(all_verts)-n_before))
    return all_verts, part_bones

def main():
    if len(sys.argv) < 5:
        print('Uso: build_janemba_final.py <janemba.amb> <krillin_hd> <krillin_ps2> <out>')
        return
    janemba = open(sys.argv[1], 'rb').read()
    krillin = open(sys.argv[2], 'rb').read()
    krillin_ps2 = open(sys.argv[3], 'rb').read()
    out = sys.argv[4]

    # ---- Parsear AMG0 de Janemba con bones ----
    amo0 = 64
    if janemba[amo0:amo0+4] not in (b'#AMO0', b'#AMO'):
        for i in range(len(janemba)-4):
            if janemba[i:i+4] == b'#AMO0': amo0 = i; break
    n_amg = r32(janemba, amo0+0x18)
    amg_table = r32(janemba, amo0+0x1C)
    if amg_table < 0x100: amg_table = amo0+0x30
    else: amg_table = amo0+amg_table
    amg0_abs = amo0 + r32(janemba, amg_table+0)
    print('Janemba AMG0 @0x%X' % amg0_abs)
    verts, part_bones = parse_amg0_with_parts(janemba, amg0_abs)
    print('  verts=%d (con bone), parts=%d' % (len(verts), len(part_bones)))
    # distribucion por bone
    from collections import Counter
    bc = Counter(v['bone'] for v in verts)
    print('  bones:', dict(sorted(bc.items())))

    # ---- Labels y mapping ----
    kll_labels = read_labels_hd(krillin)
    jnb_labels = read_labels_ps2(janemba, amg0_abs)
    mapping = build_mapping(jnb_labels, kll_labels)
    print('Mapeo JNB->KLL:', {k: v for k, v in sorted(mapping.items()) if v >= 0})

    # ---- World mats ----
    mats_jnb = build_world_mats_ps2(janemba, amo0)
    mats_kll = build_world_mats_ps2(krillin_ps2, 0x40)
    print('World mats: JNB=%d KLL=%d' % (len(mats_jnb), len(mats_kll)))

    # ---- Slots del vb2 de Krillin ----
    awo = 0x40; tbl = u32be(krillin, awo+0x1C); off0 = u32be(krillin, awo+tbl); h = awo+off0
    pvb = u32be(krillin, h+0x28); vbs = u32be(krillin, h+0x2C)
    n_slots = vbs//44
    print('Krillin AWG0 vb2: %d slots @0x%X' % (n_slots, awo+pvb))

    # ---- Transformar verts: local JNB -> world -> local KLL ----
    hd_verts = []
    for v in verts:
        bone_jnb = v['bone']
        bone_kll = mapping.get(bone_jnb, 0)
        Mj, pj = mats_jnb.get(bone_jnb, ([[1,0,0],[0,1,0],[0,0,1]], (0,0,0)))
        world = apply_mat(Mj, pj, v['pos'])
        Mk, pk = mats_kll.get(bone_kll, ([[1,0,0],[0,1,0],[0,0,1]], (0,0,0)))
        iM, ip = inv_rigid(Mk, pk)
        local = apply_mat(iM, ip, world)
        hd_verts.append(build_vertex_hd(local, bone_kll, 1.0, v['nrm'], v['uv']))

    # ---- Rellenar/decimar a n_slots ----
    if len(hd_verts) > n_slots:
        # decimar por bone (mantener proporcion) - simple: muestrear
        step = len(hd_verts) / n_slots
        keep = [int(i*step) for i in range(n_slots) if int(i*step) < len(hd_verts)]
        hd_verts = [hd_verts[i] for i in keep]
        while len(hd_verts) < n_slots:
            hd_verts.append(hd_verts[-1])
        print('  Decimado %d -> %d' % (len(verts), len(hd_verts)))
    elif len(hd_verts) < n_slots:
        pad_vtx = (struct.pack('>f', float('nan')) + struct.pack('>f',0)*2 +
                   struct.pack('>f',0)*3 + struct.pack('>f',1.0) +
                   struct.pack('>I', 0) + struct.pack('>f',0)*3)
        hd_verts += [pad_vtx]*(n_slots-len(hd_verts))
        print('  Rellenado a %d' % n_slots)

    # ---- Escribir en el bin de Krillin (estructura intacta) ----
    new_bin = bytearray(krillin)
    base = awo + pvb
    for i, vtx in enumerate(hd_verts):
        new_bin[base+i*44 : base+i*44+44] = vtx
    print('Escrito %d slots en vb2' % n_slots)

    with open(out, 'wb') as f:
        f.write(bytes(new_bin))
    print('Guardado: %s (%d bytes)' % (out, len(new_bin)))

if __name__ == '__main__':
    main()
