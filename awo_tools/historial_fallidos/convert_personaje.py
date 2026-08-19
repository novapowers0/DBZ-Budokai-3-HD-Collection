"""
Conversor de personaje IW (PS2 #AMO0) al formato HD B3 (#AWO).

Pipeline:
  1. Extraer geometria PS2 (vertices + triangulos por mesh part).
  2. Aplicar skinning (rig PS2 -> coords locales por hueso).
  3. Convertir vertices al layout HD (stride 44, big-endian).
  4. Deduplicar vertices (por contenido) y reconstruir IB.
  5. Empaquetar en la plantilla AWO de Krillin (conteos fijos) o en un
     AWO nuevo con conteos del personaje.

Uso:
  python convert_personaje.py <bin_ps2_amo0> <personaje> <output>
"""

import struct
import sys

sys.path.insert(0, r'C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\awo_tools')
from extract_geometry import PS2Model

F32 = struct.Struct('>f')
U32 = struct.Struct('>I')
U16 = struct.Struct('>H')


def f32(v):
    return F32.pack(v)


def u32(v):
    return U32.pack(v)


def u16(v):
    return U16.pack(v)


class SkinData:
    """Rig PS2: mapeo vertice(mesh) -> (bone, weight, coords_locales)."""

    def __init__(self, ps2, amg_abs):
        self.ps2 = ps2
        self.amg_abs = amg_abs
        self.entries = []
        b = ps2
        amg = amg_abs
        bone_am = struct.unpack('<I', b[amg + 0x10:amg + 0x14])[0]
        axes_loc = struct.unpack('<I', b[amg + 0x14:amg + 0x18])[0]
        for bi in range(bone_am):
            e0 = amg + axes_loc + bi * 80
            p34 = struct.unpack('<I', b[e0 + 0x34:e0 + 0x38])[0]
            if p34 == 0:
                continue
            arm = amg + p34
            rig_ptr = struct.unpack('<I', b[arm + 8:arm + 12])[0]
            if not rig_ptr:
                continue
            r = amg + rig_ptr
            wv_am = struct.unpack('<I', b[r + 12:r + 16])[0]
            for wg in range(wv_am):
                wo = r + 16 + wg * 32
                weight = struct.unpack('<f', b[wo:wo + 4])[0]
                vvn_am = struct.unpack('<I', b[wo + 4:wo + 8])[0]
                vvn_loc = struct.unpack('<I', b[wo + 8:wo + 12])[0]
                v_am = struct.unpack('<I', b[wo + 12:wo + 16])[0]
                v_loc = struct.unpack('<I', b[wo + 16:wo + 20])[0]
                vvn_abs = amg + vvn_loc
                for i in range(vvn_am):
                    e = vvn_abs + i * 32
                    coords = struct.unpack('<fff', b[e:e + 12])
                    voff = struct.unpack('<I', b[e + 12:e + 16])[0]
                    self.entries.append((bi, weight, coords, voff))
                if v_am and v_loc:
                    v_abs = amg + v_loc
                    for i in range(v_am):
                        e = v_abs + i * 16
                        coords = struct.unpack('<fff', b[e:e + 12])
                        voff = struct.unpack('<I', b[e + 12:e + 16])[0]
                        self.entries.append((bi, weight, coords, voff))


def map_offset_to_vertex(part_abs, offset):
    """Resuelve un offset del rig al vertice del mesh part."""
    for i, p in enumerate(part_abs):
        nxt = part_abs[i + 1] if i + 1 < len(part_abs) else 0x100000
        if p <= offset < nxt:
            verts_start = p + 0xA0 + 0x20
            rel = offset - verts_start
            if rel >= 0 and rel % 48 == 0:
                return i, rel // 48
            verts_start2 = p + 0xA0 + 0x10
            rel2 = offset - verts_start2
            if rel2 >= 0 and rel2 % 48 == 0:
                return i, rel2 // 48
            return i, -1
    return None, -1


def build_vertex_hd(vert, bone_idx, weight):
    """Vertice PS2 + skinning -> layout HD (stride 44, alineado +2).

    Layout del vertice HD (B3, verificado vs Krillin):
      +00: nan (flag)
      +04: VT.u   +08: V.v
      +12: pos.z_local  +16: pos.x_local  +20: pos.y_local
      +24: peso (float)
      +28: BONE INDEX (u32)   <-- el guest skinea con la matriz de este hueso
      +32: normal.z  +36: -normal.y  +40: normal.x
    """
    vx, vy, vz = vert['pos']
    nx, ny, nz = vert['nrm']
    u, vt = vert['uv']
    return (f32(float('nan')) +
            f32(u) + f32(vt) +
            f32(vz) +
            f32(vx) + f32(vy) +
            f32(float(weight)) +
            struct.pack('>I', bone_idx & 0xFFFFFFFF) +
            f32(nz) + f32(-ny) + f32(nx))


def extract_geometry_skinned(ps2_bin):
    """Devuelve los vertices convertidos al layout HD (con skinning)."""
    model = PS2Model(ps2_bin)
    amgs = model.amg_offsets()
    converted = []
    tri_sets = []
    for amg_off in amgs:
        amg_base = model.amo0 + amg_off
        parts = model.mesh_parts(amg_base)
        if not parts:
            continue
        part_abs = [p['po'] for p in parts]
        mg_abs = part_abs[0] - 0x50
        part_abs = [mg_abs + (p['po'] - mg_abs) for p in parts]
        # skin
        skin = SkinData(ps2_bin, amg_base)
        vert_skin = {}
        for bone_idx, weight, coords, voff in skin.entries:
            pi, vi = map_offset_to_vertex(part_abs, voff)
            if pi is not None and vi >= 0:
                vert_skin[(pi, vi)] = (bone_idx, weight, coords)
        # convertir
        part_conv = []
        for pi, p in enumerate(parts):
            conv_verts = []
            for vi, v in enumerate(p['verts']):
                key = (pi, vi)
                if key in vert_skin:
                    bone_idx, weight, coords = vert_skin[key]
                    nv = {'pos': list(coords), 'nrm': v['nrm'], 'uv': v['uv']}
                    conv_verts.append(build_vertex_hd(nv, bone_idx, weight))
                else:
                    conv_verts.append(build_vertex_hd(v, 0, 1.0))
            part_conv.append(conv_verts)
            converted.extend(conv_verts)
        tri_sets.append((part_conv, parts))
    return converted, tri_sets


def main():
    if len(sys.argv) < 4:
        print('Uso: python convert_personaje.py <bin_ps2> <nombre> <output>')
        return
    ps2 = open(sys.argv[1], 'rb').read()
    name = sys.argv[2]
    out = sys.argv[3]

    converted, tri_sets = extract_geometry_skinned(ps2)
    print('%s: %d vertices convertidos (layout HD)' % (name, len(converted)))
    print('  Tri sets por AMG: %d' % len(tri_sets))

    # Dedup por contenido (bytes exactos)
    dedup = {}
    unique = []
    for vb in converted:
        if vb not in dedup:
            dedup[vb] = len(unique)
            unique.append(vb)
    print('  Vertices unicos: %d' % len(unique))

    with open(out, 'wb') as f:
        f.write(b''.join(unique))
    print('Guardado: %s (%d bytes)' % (out, len(unique) * 44))


if __name__ == '__main__':
    main()
