#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ps2_to_hd_geometry.py - Convertir geometria PS2 a buffers HD (sec34/vb2/IB).

Paso 2 del conversor universal. Toma la geometria PS2 de un #AMO0 (coords
locales + bone via rig) y la convierte a los buffers HD:
  - sec34 (stride 44, skinned): [0xFFFFFFFF, u, v, z, x, y, peso, BONE, nz,-ny,nx]
  - vb2  (stride 44, estatico): [x_abs, y_abs, z_abs, 0,0,0, 0, 0xFFFFFFFF, nx,ny,nz]
  - IB (indices u16 big-endian)

Nota: las coords locales PS2 y HD son las MISMAS (mismo esqueleto/pose,
verificado en AGENTS 12.2). Solo cambia el layout del vertice.

Uso:
  python ps2_to_hd_geometry.py <janemba.amb> <salida.json>
"""
import struct
import sys
import json

def le32(b, o): return struct.unpack('<I', b[o:o+4])[0]
def lef(b, o): return struct.unpack('<f', b[o:o+4])[0]
def be_f32(v): return struct.pack('>f', v)
def be_u32(v): return struct.pack('>I', v & 0xFFFFFFFF)
def be_u16(v): return struct.pack('>H', v & 0xFFFF)


def parse_mesh_with_verts(d, amg_off, base=0x40):
    """Parsea los mesh parts PS2 devolviendo por part:
       (bone, tex, shader, [(voff_abs, (x,y,z), (nx,ny,nz), (u,v))...], tris_locales)
    Los tris son indices LOCALES al part (base_vcount + ...)."""
    amg_abs = base + amg_off
    bone_am = le32(d, amg_abs + 0x10)
    axes_loc = le32(d, amg_abs + 0x14)
    parts = []
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
        for rel in part_offs:
            po = mg + rel
            size_field = le32(d, po + 0x90)
            mesh_size = (size_field - 0x60000000) * 16 if size_field >= 0x60000000 else 0
            tex = le32(d, po + 8)
            shader = le32(d, po + 0xC)
            # vtype del mesh part: primer byte del part (0xB5/B6/F5=48B, 0xB4/A4=32B)
            type1 = le32(d, po)
            vtype = type1 & 0xFF
            if vtype in (0xB5, 0xB6, 0xF5):
                stride = 48
                has_uv = True
            elif vtype in (0xB4, 0xA4, 0x19, 0x99, 0x92):
                stride = 32
                has_uv = True
            elif vtype == 0x90:
                stride = 16
                has_uv = False
            else:
                stride = 48
                has_uv = True
            md = po + 0xA0
            end = md + mesh_size if mesh_size > 0 else po + 0x400
            pos = md
            verts = []
            tris = []
            base_vcount = 0
            while pos + 0x20 < min(end, len(d)):
                facetype = le32(d, pos + 0x10)
                vertcount = le32(d, pos + 0x14)
                if vertcount == 0 or vertcount > 0xFFFF:
                    break
                vpos = pos + 0x20
                if vpos + vertcount * stride > min(end, len(d)):
                    break
                for x in range(vertcount):
                    oa = vpos + x * stride
                    vx, vy, vz = lef(d, oa), lef(d, oa+4), lef(d, oa+8)
                    if stride == 48:
                        nx, ny, nz = lef(d, oa+16), lef(d, oa+20), lef(d, oa+24)
                        tu, tv = lef(d, oa+32), lef(d, oa+36)
                    elif stride == 32:
                        nx, ny, nz = 0.0, 0.0, 0.0
                        tu, tv = lef(d, oa+16), lef(d, oa+20)
                    else:
                        nx, ny, nz = 0.0, 0.0, 0.0
                        tu, tv = 0.0, 0.0
                    verts.append((oa, (vx, vy, vz), (nx, ny, nz), (tu, tv)))
                # triangulos: facetype 1=strip (winding alternado), 0=tripletes
                if facetype == 1:
                    for i in range(vertcount - 2):
                        a, b, c = i, i+1, i+2
                        if i % 2 == 1:
                            a, b = b, a
                        tris.append((base_vcount+a, base_vcount+b, base_vcount+c))
                else:
                    for i in range(0, vertcount - 2, 3):
                        tris.append((base_vcount+i, base_vcount+i+1, base_vcount+i+2))
                base_vcount += vertcount
                pos = vpos + vertcount * stride
            parts.append({'bone': bi, 'tex': tex, 'shader': shader,
                          'verts': verts, 'tris': tris})
    return parts


def apply_rig_skin(d, amg_off, base, all_verts):
    """Asigna (bone, peso) a cada vertice PS2 via rig. Devuelve dict voff_abs -> (bone, weight)."""
    amg_abs = base + amg_off
    n_bones = le32(d, amg_abs + 0x10)
    vert_bone = {}
    for bi in range(n_bones):
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
        for i in range(chunk_amnt):
            ch = rig + 16 + i * 32
            weight = le32(d, ch)
            ch_len = le32(d, ch + 4)
            ch_loc = le32(d, ch + 8)
            sb_len = le32(d, ch + 12)
            sb_loc = le32(d, ch + 16)
            if ch_loc:
                for k in range(ch_len):
                    off = le32(d, amg_abs + ch_loc + k * 32 + 12) + amg_abs
                    if off in all_verts and off not in vert_bone:
                        vert_bone[off] = (bi, weight)
            if sb_loc:
                for k in range(sb_len):
                    off = le32(d, amg_abs + sb_loc + k * 16 + 12) + amg_abs
                    if off in all_verts and off not in vert_bone:
                        vert_bone[off] = (bi, weight)
    return vert_bone


def build_hd_buffers(parts, vert_bone):
    """Construye sec34, vb2 e IB HD.
    - Verts con bone -> sec34 (skinned, coords locales HD = PS2).
    - Verts sin bone -> vb2 (estaticos, coords locales tambien).
    Devuelve (sec34_bytes, vb2_bytes, ib_bytes, n_sec, n_vb2, n_ib)."""
    sec34 = []
    vb2 = []
    ib = []
    # mapa voff_abs -> indice de vertice HD (global sec34+vb2)
    vmap = {}
    global_idx = 0
    for part in parts:
        for (oa, pos, nrm, uv) in part['verts']:
            if oa not in vmap:
                (vx, vy, vz) = pos
                (nx, ny, nz) = nrm
                (u, v) = uv
                if oa in vert_bone:
                    bone, w = vert_bone[oa]
                    wf = struct.unpack('<f', struct.pack('<I', w))[0] if w else 1.0
                    # sec34: [FFFF, u, v, z, x, y, peso, BONE, nz, -ny, nx]
                    vb = (be_u32(0xFFFFFFFF) + be_f32(u) + be_f32(v) +
                          be_f32(vz) + be_f32(vx) + be_f32(vy) +
                          be_f32(wf) + be_u32(bone) +
                          be_f32(nz) + be_f32(-ny) + be_f32(nx))
                    sec34.append(vb)
                else:
                    # vb2: [x_abs, y_abs, z_abs, 0,0,0, 0, FFFFFFFF, nx, ny, nz]
                    vb = (be_f32(vx) + be_f32(vy) + be_f32(vz) +
                          be_f32(0.0) + be_f32(0.0) + be_f32(0.0) +
                          be_f32(0.0) + be_u32(0xFFFFFFFF) +
                          be_f32(nx) + be_f32(ny) + be_f32(nz))
                    vb2.append(vb)
                vmap[oa] = global_idx
                global_idx += 1
            ib.append(vmap[oa])
    sec34_bytes = b''.join(sec34)
    vb2_bytes = b''.join(vb2)
    ib_bytes = b''.join(be_u16(i) for i in ib)
    return sec34_bytes, vb2_bytes, ib_bytes, len(sec34), len(vb2), len(ib)


def main():
    d = open(sys.argv[1], 'rb').read()
    base = 0x40
    amg_rel = 0x84C0 - base  # AMG0 de Janemba
    parts = parse_mesh_with_verts(d, amg_rel, base)
    all_verts = set(oa for p in parts for (oa, *_ ) in p['verts'])
    print('parts: %d, verts: %d' % (len(parts), len(all_verts)))
    vert_bone = apply_rig_skin(d, amg_rel, base, all_verts)
    print('verts skinned: %d, estaticos: %d' % (len(vert_bone), len(all_verts)-len(vert_bone)))
    sec, vb2, ib, n_sec, n_vb2, n_ib = build_hd_buffers(parts, vert_bone)
    print('sec34=%d vb2=%d ib=%d (%d tris)' % (n_sec, n_vb2, n_ib, n_ib//3))
    out = sys.argv[2] if len(sys.argv) > 2 else 'hd_geometry.json'
    json.dump({'sec34': sec.hex(), 'vb2': vb2.hex(), 'ib': ib.hex(),
               'n_sec': n_sec, 'n_vb2': n_vb2, 'n_ib': n_ib},
              open(out, 'w'))
    print('guardado:', out, '(%d B)' % (len(sec)+len(vb2)+len(ib)))


if __name__ == '__main__':
    main()
