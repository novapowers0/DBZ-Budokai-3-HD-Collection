"""
Constructor de un #AWO (HD 360) desde la geometría PS2 extraída.

Genera un AWO HD válido: header + tabla relaciones + tabla offsets AMG +
labels + AWGs, usando la geometría PS2 (vértices convertidos al layout HD).

Pendiente de validación empírica: el runtime debe aceptar la geometría PS2
dentro de la estructura AWO HD.

Uso:
  python build_awo.py <bin_amb_ps2> <output.bin>
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


def build_vertex_hd(vert):
    """Convierte un vértice PS2 al layout HD (stride 0x2C)."""
    vx, vy, vz = vert['pos']
    nx, ny, nz = vert['nrm']
    u, vt = vert['uv']
    return (f32(vz) +              # +00 V.z
            f32(0.5) + f32(0.5) +  # +04 +08 (weights placeholder)
            f32(0.0) + f32(0.0) +  # +0C +10
            f32(nz) +              # +14 VN.z
            f32(-ny) +             # +18 -VN.y
            f32(nx) +              # +1C VN.x
            f32(1.0) +             # +20 (nan placeholder -> 1.0)
            f32(u) + f32(vt))      # +24 +28 VT.u/v


def build_awg(bone_am, parts_by_bone, labels):
    """
    Construye un AWG HD desde los mesh parts PS2 de ese AWG.
    Returns: (awg_bytes, awg_rel_offset)
    """
    # Estructura AWG HD:
    #   header 0x40
    #   labels (bone_am x 32) en +0x40
    #   ... mesh-ref blocks, vertex buffer, index buffer, restart
    # Simplificación: usar un solo mesh group con todos los parts.
    parts = []
    for bp in parts_by_bone:
        parts.extend(bp)
    if not parts:
        return b''

    # Vertex buffer (atributos, stride 0x2C)
    all_verts = []
    for p in parts:
        all_verts.extend(p['verts'])
    vb = b''.join(build_vertex_hd(v) for v in all_verts)
    n_verts = len(all_verts)

    # Index buffer: triangle list secuencial
    # Cada part PS2 expande triángulos (3 vértices consecutivos por triángulo)
    ib = b''
    offset = 0
    for p in parts:
        nv = p['n_verts']
        for i in range(nv // 3):
            ib += u16(offset + i * 3) + u16(offset + i * 3 + 1) + u16(offset + i * 3 + 2)
        offset += nv

    # Layout del AWG (todo relativo al AWG):
    hdr = 0x40
    labels_off = hdr                       # +0x40
    labels = b''
    for name in labels:
        labels += name.encode('ascii').ljust(32, b'\x00')
    labels_end = labels_off + len(labels)

    # Tabla de mesh-ref blocks (para el mesh group)
    # Simplificación: 1 mesh group con N parts
    n_parts = len(parts)
    mesh_group_off = labels_end

    # Colocar: mesh group + tabla parts + vertex buffer + index buffer + restart
    # Alinear
    def align(o, n=16):
        return (o + n - 1) & ~(n - 1)

    # Los bloques mesh-ref (0x50B c/u) para cada part
    mesh_ref_off = align(mesh_group_off + 16 + n_parts * 4)  # mg hdr + tabla
    mesh_refs = []
    for i in range(n_parts):
        mesh_refs.append(mesh_ref_off + i * 0x50)

    vb_off = align(mesh_ref_off + n_parts * 0x50)
    ib_off = align(vb_off + len(vb))
    restart_off = align(ib_off + len(ib))

    total = restart_off + 0x100  # zona restart

    awg = bytearray(total)

    # header
    awg[0:4] = b'#AWG'
    awg[0x04:0x08] = u32(0x40)
    awg[0x0C:0x10] = u32(0x04)
    awg[0x10:0x14] = u32(bone_am)
    awg[0x14:0x18] = u32(0x40)       # axes_loc (simplificado)
    awg[0x18:0x1C] = u32(1)          # axis_lines
    awg[0x1C:0x20] = u32(0x40)       # label_loc
    awg[0x28:0x2C] = u32(mesh_group_off)  # mesh group ptr
    awg[0x34:0x38] = u32(vb_off)          # vertex buffer
    awg[0x38:0x3C] = u32(restart_off)     # restart buffer

    # labels
    awg[labels_off:labels_off + len(labels)] = labels

    # mesh group header
    mg = mesh_group_off
    awg[mg:mg + 4] = u32(n_parts)
    awg[mg + 4:mg + 8] = u32(0x10)
    for i in range(n_parts):
        rel = mesh_refs[i] - mg
        awg[mg + 16 + i * 4: mg + 20 + i * 4] = u32(rel)

    # mesh-ref blocks
    for i in range(n_parts):
        o = mesh_refs[i]
        awg[o:o + 4] = f32(1.0)
        awg[o + 4:o + 8] = f32(0.0)
        awg[o + 8:o + 12] = f32(0.0)
        awg[o + 0x18:o + 0x1C] = u32(0x9000020C)  # sello
        awg[o + 0x1C:o + 0x20] = u32(vb_off)      # dat ptr
        awg[o + 0x20:o + 0x24] = u32(vb_off)      # transform ptr

    # vertex buffer
    awg[vb_off:vb_off + len(vb)] = vb
    # index buffer
    awg[ib_off:ib_off + len(ib)] = ib
    # restart buffer
    for i in range(0, 0x100, 2):
        awg[restart_off + i:restart_off + i + 2] = u16(0xFFFF)

    return bytes(awg), n_verts, len(ib) // 2


def main():
    if len(sys.argv) < 3:
        print('Uso: python build_awo.py <bin_amb_ps2> <output.bin>')
        return
    with open(sys.argv[1], 'rb') as f:
        b = f.read()
    model = PS2Model(b)
    amgs = model.amg_offsets()

    # Etiquetas (del primer AMG)
    amg0 = model.amo0 + amgs[0]
    labels = model.labels(amg0)

    # Construir cada AWG
    awg_blobs = []
    awg_offsets = []
    for i, amg_off in enumerate(amgs):
        amg_base = model.amo0 + amg_off
        parts = model.mesh_parts(amg_base)
        if not parts:
            continue
        bone_am = struct.unpack('<I', b[amg_base + 0x10:amg_base + 0x14])[0]
        awg_data, nv, ni = build_awg(bone_am, [parts], labels if i == 0 else ['X'])
        awg_blobs.append(awg_data)
        print('AWG%d: %d parts, %d verts, %d indices -> %d bytes' % (i, len(parts), nv, ni, len(awg_data)))

    # Layout del AWO:
    #   header 0x30
    #   tabla_relaciones (bone x 32)
    #   tabla_offsets_AMG (amg x 4)
    #   labels (bone x 32)
    #   AWGs
    bone_am = model.bone_am
    amg_am = len(awg_blobs)

    hdr = 0x30
    rel_off = hdr
    rel_end = rel_off + bone_am * 32
    amg_tbl_off = rel_end
    amg_tbl_end = amg_tbl_off + amg_am * 4
    lbl_off = amg_tbl_end
    lbl_end = lbl_off + bone_am * 32

    # data offset (primer AWG)
    data_off = lbl_end
    # calcular offsets de AWGs
    cur = data_off
    awg_abs = []
    for a in awg_blobs:
        awg_abs.append(cur)
        cur += len(a)

    total = cur
    awo = bytearray(total)
    awo[0:4] = b'#AWO'
    awo[0x04:0x08] = u32(0x30)
    awo[0x0C:0x10] = u32(0x04)
    awo[0x10:0x14] = u32(bone_am)
    awo[0x14:0x18] = u32(rel_off)
    awo[0x18:0x1C] = u32(amg_am)
    awo[0x1C:0x20] = u32(amg_tbl_off)
    awo[0x20:0x24] = u32(3)   # array_am
    awo[0x24:0x28] = u32(lbl_off)
    awo[0x34:0x38] = u32(total)

    # tabla relaciones (simplificada: cada hueso apunta a si mismo, sin hijos)
    for i in range(bone_am):
        o = rel_off + i * 32
        awo[o:o + 4] = u32(i)
        awo[o + 8:o + 12] = u32(0)  # child
        awo[o + 12:o + 16] = u32(0)  # sibling
        awo[o + 16:o + 20] = u32(0)  # parent

    # tabla offsets AMG
    for i, off in enumerate(awg_abs):
        awo[amg_tbl_off + i * 4: amg_tbl_off + i * 4 + 4] = u32(off)

    # labels
    for i, name in enumerate(labels[:bone_am]):
        o = lbl_off + i * 32
        awo[o:o + len(name.encode())] = name.encode()

    # AWGs
    for a, off in zip(awg_blobs, awg_abs):
        awo[off:off + len(a)] = a

    # Envolver en #AMB (formato HD: header 0x40, entradas de 16B desde 0x20)
    amb = bytearray()
    amb += b'#AMB'
    amb += u32(0x20)          # header size
    amb += u32(0)             # +0x08
    amb += u32(1)             # +0x0C entry count
    amb += u32(1)             # +0x10 models
    amb += u32(0x20)          # +0x14
    amb += u32(0x40)          # +0x18 primer bloque
    amb += u32(0)             # +0x1C
    # tabla de entradas desde 0x20: [loc, size, idx, 0]
    amb += u32(0x40)          # loc
    amb += u32(len(awo))      # size
    amb += u32(1)             # idx
    amb += u32(0)             # 0
    amb += bytes(0x40 - len(amb))  # padding alineado a 0x40
    amb += awo

    with open(sys.argv[2], 'wb') as f:
        f.write(amb)
    print()
    print('AWO generado: %d bytes (AMB: %d bytes)' % (len(awo), len(amb)))
    print('huesos=%d amg=%d' % (bone_am, amg_am))


if __name__ == '__main__':
    main()
