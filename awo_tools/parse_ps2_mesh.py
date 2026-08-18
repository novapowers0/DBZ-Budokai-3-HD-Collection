"""
Parser de malla PS2 de DBZ Budokai 3 basado en el MaxScript budokai_updated.ms
(modding resources update 2 + lean bone tutorial + budokai_updated.ms).

El formato del mesh part PS2 NO tiene un index buffer explicito: la geometria
se define por submeshes con FaceType + VertCount:
  - FaceType == 1: triangle strip (winding alternado zig-zag)
  - FaceType == 0: triangulos consecutivos (cada 3 vertices = 1 triangulo)

Estructura (del MaxScript):
  AMG: #AMG, HeaderLength, unk1, unkcount, BlockCount, BlockStart, unk2, NameOffset
    para cada block:
      fseek 0x30 (bone matrix)
      DataType(short), DataType2(short), DataOffset(long)
      Ukw1, Ukw2, fseek 0x10
    en DataOffset:
      ModelNumber(long)
      modelOffset[3]
      si z==1 (malla):
        meshCount, meshOffsetTable
        para cada mesh:
          meshType[8] (primer byte = formato vertice)
          Ukw3, Ukw4, fseek 0x30, fseek 0x50
          SubMeshBufferLength = readshort*0x10
          SubMeshBufferStart = readshort + addagain
          fseek 0xc
          DO:
            fseek 0xc
            Ukw1(short), VertBufferLength=readbyte*0x10, Ukw2(byte)
            NextSubMeshOffset = VertBufferLength + ftell
            FaceType(long), VertCount(long), Null(8)
            [VertCount vertices]
            ReadFaces

Formatos de vertice (primer byte de meshType):
  0xBD/0xFD/0x3D: 48B  pos(3)+null+normal(3)+null+uv(2)+null+null
  0xB5/0xB6/0xF5: 48B  pos(3)+null+normal(3)+null+uv(2)+null+skip4
  0x199:          32B  pos(3)+null+normal(3)+null (sin UV)
  0xB4/0xA4/0x99/0x92/0x19: 32B pos(3)+null+uv(2)+skip8 (faciales)
  0x90:           16B  pos(3)+null (sombras)

Uso:
  python parse_ps2_mesh.py <bin_amo0> <amg_index> <output>
  Escribe: output.verts (raw LE) + output.tris (u16 indices)
"""

import struct
import sys


def r8(b, o): return b[o]
def r16(b, o): return struct.unpack_from('<H', b, o)[0]
def r32(b, o): return struct.unpack_from('<I', b, o)[0]
def rf(b, o): return struct.unpack_from('<f', b, o)[0]


VERT_STRIDE = {
    0xBD: 48, 0xFD: 48, 0x3D: 48,
    0xB5: 48, 0xB6: 48, 0xF5: 48,
    0x199: 32,
    0xB4: 32, 0xA4: 32, 0x99: 32, 0x92: 32, 0x19: 32,
    0x90: 16,
}


def read_vert(b, off, vtype):
    """Lee un vertice PS2 segun el formato. Devuelve (pos, nrm, uv)."""
    if vtype in (0xBD, 0xFD, 0x3D, 0xB5, 0xB6, 0xF5):
        # 48B: pos(3)+null+normal(3)+null+uv(2)+null(+skip4)
        vx, vy, vz = rf(b, off), rf(b, off+4), rf(b, off+8)
        nx, ny, nz = rf(b, off+16), rf(b, off+20), rf(b, off+24)
        tu, tv = rf(b, off+32), rf(b, off+36)
        return (vx, vy, vz), (nx, ny, nz), (tu, tv)
    elif vtype == 0x199:
        vx, vy, vz = rf(b, off), rf(b, off+4), rf(b, off+8)
        nx, ny, nz = rf(b, off+16), rf(b, off+20), rf(b, off+24)
        return (vx, vy, vz), (nx, ny, nz), (0, 0)
    elif vtype in (0xB4, 0xA4, 0x99, 0x92, 0x19):
        vx, vy, vz = rf(b, off), rf(b, off+4), rf(b, off+8)
        tu, tv = rf(b, off+16), rf(b, off+20)
        return (vx, vy, vz), (0, 0, 0), (tu, tv)
    elif vtype == 0x90:
        vx, vy, vz = rf(b, off), rf(b, off+4), rf(b, off+8)
        return (vx, vy, vz), (0, 0, 0), (0, 0)
    return (0, 0, 0), (0, 0, 0), (0, 0)


def read_faces(vertcount, facetype):
    """Genera los triangulos de un submesh segun FaceType.
    FaceType 1 = triangle strip (winding alternado).
    FaceType 0 = triangulos consecutivos (cada 3)."""
    faces = []
    if facetype == 1:
        # triangle strip: [f1,f2,f3] con direccion alterna
        f1, f2 = 0, 1
        direction = -1
        for x in range(2, vertcount):
            f3 = x
            direction *= -1
            if f1 != f2 and f2 != f3 and f3 != f1:
                if direction > 0:
                    faces.append((f1, f2, f3))
                else:
                    faces.append((f1, f3, f2))
            f1, f2 = f2, f3
    else:
        # triangulos consecutivos
        for x in range(1, vertcount + 1, 3):
            if x + 2 <= vertcount:
                faces.append((x - 1, x, x + 1))
    return faces


def parse_part_mesh(b, md, mesh_data_end, vtype=0xB5):
    """Parsea el mesh_data de un mesh part (face blocks en cadena).

    Cada submesh (del MaxScript):
      fseek 0xc
      Ukw1(short), VertBufferLength = readbyte*0x10, Ukw2(byte)
      NextSubMeshOffset = VertBufferLength + ftell
      FaceType(long)   <- 1=strip, 0=triplete
      VertCount(long)
      Null(8)
      [VertCount vertices de 48B]
    Los submeshes se encadenan hasta mesh_data_end.
    """
    verts = []
    tris = []
    pos = md
    stride = VERT_STRIDE.get(vtype, 48)
    while pos + 0x20 < mesh_data_end and pos < len(b):
        # header del submesh (0x20): FaceType en +0x10, VertCount en +0x14
        facetype = r32(b, pos + 0x10)
        vertcount = r32(b, pos + 0x14)
        if vertcount == 0 or vertcount > 0xFFFF:
            break
        vpos = pos + 0x20
        if vpos + vertcount * stride > mesh_data_end:
            break
        base_vcount = len(verts)
        for x in range(vertcount):
            vx, vy, vz = rf(b, vpos), rf(b, vpos + 4), rf(b, vpos + 8)
            verts.append((vx, vy, vz))
            vpos += stride
        faces = read_faces(vertcount, facetype)
        for f0, f1, f2 in faces:
            tris.append((base_vcount + f0, base_vcount + f1, base_vcount + f2))
        pos = vpos
    return verts, tris


def parse_amg(b, amg_off, base):
    """Parsea un AMG y devuelve (verts, tris, part_info).
    base = offset base del AMB (0x40) para offsets absolutos.

    AMG header (b327_ps2 real):
      +0x00 #AMG  +0x04 header_len +0x10 bone_am +0x14 axes_loc
      +0x18 mesh_groups (13) +0x1C name_off
    """
    amg_abs = base + amg_off
    magic = b[amg_abs:amg_abs + 4]
    bone_am = r32(b, amg_abs + 0x10)
    axes_loc = r32(b, amg_abs + 0x14)
    mesh_groups = r32(b, amg_abs + 0x18)

    all_verts = []
    all_tris = []
    part_info = []

    # Recorrer los bones con mesh group (como extract_geometry.mesh_parts)
    for bi in range(bone_am):
        e0 = amg_abs + axes_loc + bi * 80
        p34 = r32(b, e0 + 0x34)
        if p34 == 0:
            continue
        arm = amg_abs + p34
        arm_idx = r32(b, arm)
        mesh_hdr = r32(b, arm + 4)
        if mesh_hdr == 0:
            continue
        mg = amg_abs + mesh_hdr
        mp_amnt = r32(b, mg)
        if mp_amnt == 0 or mp_amnt > 64:
            continue
        part_offs = [r32(b, mg + 16 + i * 4) for i in range(mp_amnt)]
        for pi, rel in enumerate(part_offs):
            po = mg + rel
            # mesh part: header 0xA0, mesh_data en po+0xA0
            # mesh_size del flag en +0x90
            size_field = r32(b, po + 0x90)
            mesh_size = (size_field - 0x60000000) * 16 if size_field >= 0x60000000 else 0
            tex = r32(b, po + 8)
            shader = r32(b, po + 0xC)
            type1 = r32(b, po)
            vtype = 0xB5
            md = po + 0xA0
            end = md + mesh_size if mesh_size > 0 else po + 0x400
            verts, tris = parse_part_mesh(b, md, min(end, len(b)), vtype)
            offset = len(all_verts)
            all_verts.extend(verts)
            for t0, t1, t2 in tris:
                all_tris.append((offset + t0, offset + t1, offset + t2))
            part_info.append((bi, tex, shader, len(verts), len(tris)))

    return all_verts, all_tris, part_info


def main():
    if len(sys.argv) < 4:
        print('Uso: parse_ps2_mesh.py <bin_amo0> <amg_index> <output_base>')
        return
    b = open(sys.argv[1], 'rb').read()
    amg_idx = int(sys.argv[2])
    out = sys.argv[3]

    # AMO0 header
    base = 0x40
    if b[base:base + 4] != b'#AMO':
        # quizas ya es AMB con AMO0 en 0x40
        if b[0:4] == b'#AMB':
            # AMO0 en la entrada 0
            base = r32(b, 0x20)
    amo = b[base:]
    magic = amo[0:4]
    if magic not in (b'#AMO', b'#AMO0'):
        print('ERROR: no es #AMO0 (%s)' % magic)
        return
    # header del AMO0 (verificado b327_ps2):
    #   +0x10 = num huesos, +0x14 = offset datos huesos, +0x18 = num AMGs,
    #   +0x1C = 0x30, +0x20 = 0x18, +0x24 = 0x93AD0,
    #   +0x30 = TABLA de offsets AMG (los offsets en si, no un puntero)
    amo_base = base
    n_amg = r32(b, amo_base + 0x18)
    amg_table = amo_base + 0x30
    print('AMO0: %d AMGs, tabla @0x%X' % (n_amg, amg_table))
    if amg_idx >= n_amg:
        print('ERROR: AMG %d fuera de rango (%d)' % (amg_idx, n_amg))
        return
    amg_off = r32(b, amg_table + amg_idx * 4)
    print('AMG %d @ rel 0x%X' % (amg_idx, amg_off))

    verts, tris, parts = parse_amg(b, amg_off, amo_base)
    print('Verts: %d | Tris: %d' % (len(verts), len(tris)))

    # Guardar verts (raw floats LE) y tris (u16 LE)
    with open(out + '.verts', 'wb') as f:
        for v in verts:
            f.write(struct.pack('<fff', *v))
    with open(out + '.tris', 'wb') as f:
        for t in tris:
            f.write(struct.pack('<HHH', *t))
    print('Guardado: %s.verts (%d) y %s.tris (%d)' % (out, len(verts) * 12, out, len(tris) * 6))


if __name__ == '__main__':
    main()
