"""
Conversor #AMO0 (PS2) → #AWO (HD) v5 — con TRANSFORMACIÓN DE SKINNING.

Completa el pipeline: extrae la geometría PS2 (vértices + rig de skinning),
mapea cada vértice a su hueso (vía el rig data), transforma las posiciones
absolutas PS2 al espacio local del hueso, y reordena al layout HD (stride 44).

El resultado se inyecta en el AWO HD (plantilla del bin 327 de Krillin)
manteniendo los tamaños de buffers fijos (para no romper los punteros).

Uso:
  python build_awo_v5.py <bin_ps2> <bin_amb_hd_template> <output.bin>
"""

import struct
import sys
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


def read_amb(amb):
    n = struct.unpack('>I', amb[0x0C:0x10])[0]
    entries = []
    for i in range(n):
        e = 0x20 + i * 16
        loc, size = struct.unpack('>II', amb[e:e + 8])
        entries.append((loc, size))
    return entries


class SkinData:
    """Rig PS2: mapeo vértice(mesh) -> (bone, weight, coords_locales)."""

    def __init__(self, ps2, amg_abs):
        self.ps2 = ps2
        self.amg_abs = amg_abs
        self.entries = []  # (bone_idx, weight, coords_local, vertex_offset_abs)
        self._parse()

    def _parse(self):
        b = self.ps2
        amg = self.amg_abs
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
                # entradas rig1 (32B): coords(12) + offset(4) + normal(16)
                vvn_abs = amg + vvn_loc
                for i in range(vvn_am):
                    e = vvn_abs + i * 32
                    coords = struct.unpack('<fff', b[e:e + 12])
                    voff = struct.unpack('<I', b[e + 12:e + 16])[0]
                    self.entries.append((bi, weight, coords, voff))
                # entradas rig2 (16B): coords(12) + offset(4)
                if v_am and v_loc:
                    v_abs = amg + v_loc
                    for i in range(v_am):
                        e = v_abs + i * 16
                        coords = struct.unpack('<fff', b[e:e + 12])
                        voff = struct.unpack('<I', b[e + 12:e + 16])[0]
                        self.entries.append((bi, weight, coords, voff))


def map_offset_to_vertex(ps2, amg_abs, mg, part_abs, offset):
    """Resuelve un offset del rig al vértice del mesh.
    El offset es absoluto al mesh group. Busca el part que lo contiene y
    calcula el índice de vértice (relativo al inicio de vértices del part)."""
    x = offset
    for i, p in enumerate(part_abs):
        nxt = part_abs[i + 1] if i + 1 < len(part_abs) else 0x100000
        if p <= x < nxt:
            rel = x - p
            verts_start = p + 0xA0 + 0x20  # mesh data + header 32B
            rel_verts = x - verts_start
            if rel_verts >= 0 and rel_verts % 48 == 0:
                return i, rel_verts // 48
            # intentar sin el +0x20 (quizas el header es de 16B)
            verts_start2 = p + 0xA0 + 0x10
            rel2 = x - verts_start2
            if rel2 >= 0 and rel2 % 48 == 0:
                return i, rel2 // 48
            return i, -1
    return None, -1


def build_vertex_hd(vert, bone_idx, weight):
    """Vértice PS2 + skinning -> layout HD (stride 44, alineado +2).
    Layout: [nan, VT.v, VT.u, V.z, pos.x_local, pos.y_local, bone/weight, 0, VN.z, -VN.y, VN.x]
    """
    vx, vy, vz = vert['pos']
    nx, ny, nz = vert['nrm']
    u, vt = vert['uv']
    return (f32(float('nan')) +   # +00 flag
            f32(vt) + f32(u) +     # +04 VT.v, +08 VT.u
            f32(vz) +              # +12 V.z
            f32(vx) + f32(vy) +    # +16 +20 pos local
            f32(float(weight)) +   # +24 weight
            f32(0.0) +             # +28
            f32(nz) + f32(-ny) + f32(nx))  # +32 +36 +40 VN


def main():
    if len(sys.argv) < 4:
        print('Uso: python build_awo_v5.py <bin_ps2> <bin_amb_hd_template> <output.bin>')
        return
    ps2 = open(sys.argv[1], 'rb').read()
    templ = open(sys.argv[2], 'rb').read()

    entries = read_amb(templ)
    awo_loc, awo_size = entries[0]
    awo = templ[awo_loc:awo_loc + awo_size]
    print('Plantilla AWO: %d bytes' % len(awo))

    tbl = struct.unpack('>I', awo[0x1C:0x20])[0]
    amg_am = struct.unpack('>I', awo[0x18:0x1C])[0]
    offs = [struct.unpack('>I', awo[tbl + i * 4:tbl + i * 4 + 4])[0] for i in range(amg_am)]
    awg0_off = offs[0]
    awg1_off = offs[1] if amg_am > 1 else len(awo)
    print('AWG0 @0x%X, AWG1 @0x%X' % (awg0_off, awg1_off))

    # Zonas del AWG0 (relativos al AWG)
    vb2_off = struct.unpack('>I', awo[awg0_off + 0x2C:awg0_off + 0x30])[0]  # buffer secundario
    ib_off = struct.unpack('>I', awo[awg0_off + 0x30:awg0_off + 0x34])[0]
    restart_off = struct.unpack('>I', awo[awg0_off + 0x38:awg0_off + 0x3C])[0]
    vb2_size = ib_off - vb2_off
    ib_size = restart_off - ib_off
    print('AWG0: vb2=0x%X(%dB) ib=0x%X(%dB) restart=0x%X' % (
        vb2_off, vb2_size, ib_off, ib_size, restart_off))

    # Geometría PS2
    model = PS2Model(ps2)
    amgs = model.amg_offsets()
    amg0_base = model.amo0 + amgs[0]
    parts = model.mesh_parts(amg0_base)

    # mesh group y parts del PS2
    mg = 0
    # extraer del modelo: el mesh group del hueso0
    # (reusar la lógica de extract_geometry: los parts tienen 'po')
    part_abs_list = [p['po'] for p in parts]

    # Rig PS2 (skinning)
    skin = SkinData(ps2, amg0_base)
    print('Rig PS2: %d entradas de skinning' % len(skin.entries))

    # Mapear cada entrada del rig a un vértice del mesh
    # Construir un mapa offset -> vértice (part_idx, vert_idx)
    # El offset es absoluto al mesh group. Necesitamos el mesh group base.
    # El mesh group del hueso0 en PS2: del armature del hueso0.
    bones = model.bones()
    # mesh group = primer part - su offset relativo. Usar part_abs[0] como ref.
    # Los parts PS2 'po' son absolutos al AMB. El mesh group es ~po[0] - 0x50.
    # (del análisis: mg=0x63C0, part0=0x6410 = mg+0x50)
    mg_abs = part_abs_list[0] - 0x50
    part_abs = [mg_abs + (p['po'] - mg_abs) for p in parts]

    # Mapear rig entries -> vértices
    # El offset del rig es absoluto al mesh group (mg_abs).
    # Construir lista de vértices únicos por mesh part.
    vert_skin = {}  # (part_idx, vert_idx) -> (bone, weight, coords_local)
    for bone_idx, weight, coords, voff in skin.entries:
        pi, vi = map_offset_to_vertex(ps2, amg0_base, mg_abs, part_abs, voff)
        if pi is not None and vi >= 0:
            vert_skin[(pi, vi)] = (bone_idx, weight, coords)

    # Aplicar a los vértices PS2
    # Los parts PS2 tienen 'verts' (lista). El vértice del mesh en el part
    # corresponde a la posición del mesh data. Los verts extraídos ya son los
    # del mesh (ordenados). Mapear por (part_idx, vert_idx).
    converted = []
    for pi, p in enumerate(parts):
        for vi, v in enumerate(p['verts']):
            key = (pi, vi)
            if key in vert_skin:
                bone_idx, weight, coords = vert_skin[key]
                # usar coords locales del rig como posición
                new_vert = {
                    'pos': list(coords),
                    'nrm': v['nrm'],
                    'uv': v['uv'],
                }
                converted.append(build_vertex_hd(new_vert, bone_idx, weight))
            else:
                converted.append(build_vertex_hd(v, 0, 1.0))
    print('Vértices convertidos: %d' % len(converted))

    # --- Re-layout de buffers ---
    # Los 4331 vértices PS2 están expandidos por triángulo (cada part PS2
    # expande triángulos: 3 vértices consecutivos por triángulo).
    # La geometría HD usa vértices ÚNICOS indexados por un IB.
    # Estrategia: deduplicar los vértices convertidos por su contenido,
    # construir el IB apuntando a los únicos.
    #
    # NOTA: los vértices convertidos conservan el orden de los parts PS2.
    # Cada part expande triángulos. El IB PS2 original sería 0,1,2,3,4,5...
    # (cada 3 consecutivos). Deduplicar mantiene el render correcto.
    dedup = {}      # bytes del vértice -> índice único
    unique_verts = []
    new_ib_data = bytearray()
    for i, vb in enumerate(converted):
        if vb not in dedup:
            dedup[vb] = len(unique_verts)
            unique_verts.append(vb)
        new_ib_data += u16(dedup[vb])
    print('Vértices únicos: %d (de %d)' % (len(unique_verts), len(converted)))

    # Ahora construir el AMB con el AWO HD + geometría re-layout
    # Reutilizar build_awo_v4: reemplazar vb2 (sec2C) e ib con los nuevos.
    # El buffer principal (sec34) no se toca (es del template HD).
    # PERO: el IB nuevo apunta a los vértices únicos que van en vb2 (sec2C).
    # Como el buffer principal sec34 queda intacto con los vértices HD, y el
    # IB del HD original indexa sec34+sec2C, reemplazar solo sec2C+IB con los
    # PS2 únicos podría funcionar si el count de vértices calza.
    vb2_data = b''.join(unique_verts)
    print('VB2 nuevo: %d bytes (cabe en %d?)' % (len(vb2_data), vb2_size))

    # Construir el AWG0 con la geometría nueva
    awg0 = bytearray(awo[awg0_off:awg1_off])
    # reemplazar sec2C (buffer secundario)
    if len(vb2_data) <= vb2_size:
        awg0[vb2_off:vb2_off + len(vb2_data)] = vb2_data
    else:
        print('ERROR: vb2 no cabe (%d > %d)' % (len(vb2_data), vb2_size))
        return
    # reemplazar IB (con relleno FFFF)
    ib = bytearray(new_ib_data[:ib_size])
    while len(ib) < ib_size:
        ib += u16(0xFFFF)
    awg0[ib_off:ib_off + ib_size] = ib

    # Ensamblar el AWO completo (mismo tamaño)
    new_awo = bytearray()
    new_awo += awo[:awg0_off]
    new_awo += awg0
    new_awo += awo[awg1_off:]
    assert len(new_awo) == len(awo), 'Tamaño AWO cambió'

    # Empaquetar en AMB (AWO + AZT del template)
    n_orig = struct.unpack('>I', templ[0x0C:0x10])[0]
    azt_loc, azt_size = None, None
    for i in range(n_orig):
        e = 0x20 + i * 16
        loc, size = struct.unpack('>II', templ[e:e + 8])
        if templ[loc:loc + 4] == b'#AZT':
            azt_loc, azt_size = loc, size
            break
    azt_start = ((0x40 + len(new_awo) + 15) & ~15)
    amb = bytearray()
    amb += b'#AMB'
    amb += u32(0x20)
    amb += u32(0)
    amb += u32(2)
    amb += u32(2)
    amb += u32(0x20)
    amb += u32(0x40)
    amb += u32(0)
    amb += u32(0x40)
    amb += u32(len(new_awo))
    amb += u32(1)
    amb += u32(0)
    amb += u32(azt_start)
    amb += u32(azt_size)
    amb += u32(2)
    amb += u32(0)
    amb += bytes(0x40 - len(amb))
    amb += new_awo
    amb += bytes(azt_start - len(amb))
    amb += templ[azt_loc:azt_loc + azt_size]

    with open(sys.argv[3], 'wb') as f:
        f.write(bytes(amb))
    print('AMB final: %d bytes (AWO %d + AZT %d)' % (len(amb), len(new_awo), azt_size))


if __name__ == '__main__':
    main()
