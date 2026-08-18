#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ps2_rig_skin.py - Extraer el skin (bone+peso por vertice) de un #AMO0 PS2.

Algoritmo del Model-Rig Extractor v0.6 (SamuelDBZMAAM):
- Cada bone (eje de 80B) tiene en +0x34 un ptr al arm.
- El arm en +8 tiene el rig_start (rel AMG).
- El rig en +12 tiene chunk_amnt.
- Cada chunk de 32B: [weight, ch_len, ch_loc, sb_len, sb_loc] (rel AMG).
- Los chunks (ch_loc) son bloques de 32B con el OFFSET del vertice en +12.
- Los sub-chunks (sb_loc) bloques de 16B con el offset en +12.
- Se busca cada vertice PS2 (por su offset absoluto) en los chunks de cada
  bone → ese bone + peso.

Uso:
  python ps2_rig_skin.py <janemba.amb> <salida.json>
"""
import struct
import sys
import json

def le32(b, o): return struct.unpack('<I', b[o:o+4])[0]
def lef(b, o): return struct.unpack('<f', b[o:o+4])[0]


def extract_mesh_parts_offsets(d, amg_off, base=0x40):
    """Devuelve (parts, all_vert_offsets) con los offsets absolutos de cada
    vertice PS2. parts = [(bone, tex, shader, [(voff, x,y,z), ...])].
    Sigue la logica de parse_ps2_mesh.parse_amg (que funciona)."""
    amg_abs = base + amg_off
    bone_am = le32(d, amg_abs + 0x10)
    axes_loc = le32(d, amg_abs + 0x14)
    mesh_groups = le32(d, amg_abs + 0x18)
    parts = []
    all_off = []
    for bi in range(bone_am):
        e0 = amg_abs + axes_loc + bi * 80
        p34 = le32(d, e0 + 0x34)
        if p34 == 0:
            continue
        arm = amg_abs + p34
        arm_idx = le32(d, arm)
        mesh_hdr = le32(d, arm + 4)
        if mesh_hdr == 0:
            continue
        mg = amg_abs + mesh_hdr
        mp_amnt = le32(d, mg)
        if mp_amnt == 0 or mp_amnt > 64:
            continue
        part_offs = [le32(d, mg + 16 + i * 4) for i in range(mp_amnt)]
        for pi, rel in enumerate(part_offs):
            po = mg + rel
            size_field = le32(d, po + 0x90)
            mesh_size = (size_field - 0x60000000) * 16 if size_field >= 0x60000000 else 0
            tex = le32(d, po + 8)
            shader = le32(d, po + 0xC)
            md = po + 0xA0
            end = md + mesh_size if mesh_size > 0 else po + 0x400
            pos = md
            vert_offsets = []
            while pos + 0x20 < min(end, len(d)):
                facetype = le32(d, pos + 0x10)
                vertcount = le32(d, pos + 0x14)
                if vertcount == 0 or vertcount > 0xFFFF:
                    break
                vpos = pos + 0x20
                if vpos + vertcount * 48 > min(end, len(d)):
                    break
                for x in range(vertcount):
                    oa = vpos + x * 48
                    vert_offsets.append((oa, lef(d, oa), lef(d, oa+4), lef(d, oa+8)))
                pos = vpos + vertcount * 48
            parts.append((bi, tex, shader, vert_offsets))
            all_off.extend(v[0] for v in vert_offsets)
    return parts, set(all_off)


def extract_rig(d, amg_off, n_bones, base=0x40):
    """Devuelve por cada bone: [(weight, set(vertex_offsets_absolutos))]."""
    amg_abs = base + amg_off
    axes = le32(d, amg_abs + 0x14)
    axes_abs = amg_abs + axes
    bone_skin = {}  # bone -> [(weight, [offsets])]
    for bi in range(n_bones):
        # bone_loc = AMG0 + 32 + i*80 + 52 (del Model-Rig Extractor)
        bone_loc = amg_abs + 32 + bi * 80 + 52
        p = le32(d, bone_loc)
        if p == 0:
            continue
        cur = amg_abs + p
        rig_start = le32(d, cur + 8)
        if rig_start == 0:
            continue
        rig = amg_abs + rig_start
        chunk_amnt = le32(d, rig + 12)
        if chunk_amnt > 256:
            continue
        chunks = []
        for i in range(chunk_amnt):
            ch = rig + 16 + i * 32
            weight = le32(d, ch)
            ch_len = le32(d, ch + 4)
            ch_loc = le32(d, ch + 8)
            sb_len = le32(d, ch + 12)
            sb_loc = le32(d, ch + 16)
            offs = []
            if ch_loc:
                for k in range(ch_len):
                    # offset del vertice en +12 del bloque de 32B.
                    # El valor leido es RELATIVO al AMG -> sumar amg_abs para
                    # obtener el offset ABSOLUTO (verificado: delta=amg_abs da
                    # 3788/4651 coincidencias, el maximo).
                    offs.append(le32(d, amg_abs + ch_loc + k * 32 + 12) + amg_abs)
            if sb_loc:
                for k in range(sb_len):
                    offs.append(le32(d, amg_abs + sb_loc + k * 16 + 12) + amg_abs)
            chunks.append((weight, offs))
        bone_skin[bi] = chunks
    return bone_skin


def main():
    path = sys.argv[1]
    d = open(path, 'rb').read()
    base = 0x40
    # AMG0 de Janemba esta en 0x84C0 absoluto -> relativo al base = 0x8480
    amg0_rel = 0x84C0 - base
    amg0_abs = base + amg0_rel
    n_bones = le32(d, amg0_abs + 0x10)
    parts, all_offs = extract_mesh_parts_offsets(d, amg0_rel, base)
    print('parts: %d, verts unicos: %d' % (len(parts), len(all_offs)))
    bone_skin = extract_rig(d, amg0_rel, n_bones, base)
    print('bones con skin: %d' % len(bone_skin))
    # asignar bone a cada vertice
    vert_bone = {}  # off -> (bone, weight)
    for bi, chunks in bone_skin.items():
        for weight, offs in chunks:
            for off in offs:
                if off in all_offs:
                    vert_bone[off] = (bi, weight)
    print('verts con bone asignado: %d / %d' % (len(vert_bone), len(all_offs)))
    # ejemplos
    sample = list(vert_bone.items())[:5]
    for off, (bi, w) in sample:
        print('  off=0x%X bone=%d w=%s' % (off, bi, w))


if __name__ == '__main__':
    main()
