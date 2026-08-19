"""
Conversor IW -> HD B3 v3: agrupar triangulos por material y re-mapear arms.

El problema del v2 era que los triangulos de Janemba iban en orden de mesh part
PS2 (50 parts), pero los arms de Krillin dibujan 13 rangos del IB. Aqui se
agrupan los triangulos en 13 rangos (material dominante primero) y se
actualizan los offsets de los 13 arms para los nuevos rangos.

Pipeline:
  1. Extraer geometria PS2 de Janemba (posiciones ABSOLUTAS).
  2. Decimar por voxel a ~2190 vertices.
  3. Agrupar triangulos en 13 rangos por material (como el HD).
  4. Construir el IB agrupado.
  5. Usar AWO de Krillin como plantilla, reemplazar sec34 + IB.
  6. Actualizar los offsets de los arms (mesh group) para los nuevos rangos.

Uso:
  python build_janemba3.py <bin_krillin> <bin_ps2_janemba> <output_amb>
"""

import struct
import sys

sys.path.insert(0, r'C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\awo_tools')
from extract_geometry import PS2Model
from convert_personaje import extract_geometry_skinned

F32 = struct.Struct('>f')
U32 = struct.Struct('>I')
U16 = struct.Struct('>H')


def f32(v):
    return F32.pack(v)


def u32r(data, off):
    return struct.unpack('>I', data[off:off + 4])[0]


def pack_u32(data, off, val):
    struct.pack_into('>I', data, off, val)


def main():
    if len(sys.argv) < 4:
        print('Uso: python build_janemba3.py <bin_krillin> <bin_ps2_janemba> <output>')
        return
    krillin = open(sys.argv[1], 'rb').read()
    janemba = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    # 1) Geometria de Janemba con posiciones ABSOLUTAS
    model = PS2Model(janemba)
    amgs = model.amg_offsets()
    # Recolectar: (part_abs, tex, verts_absolutos, n_tri)
    all_tris = []  # lista de (tex, shader, lista de 3 vertices expandidos)
    all_verts_abs = []  # todas las posiciones absolutas (para decimar)
    for amg_off in amgs:
        amg_base = model.amo0 + amg_off
        for p in model.mesh_parts(amg_base):
            verts = p['verts']
            ntri = len(verts) // 3
            for t in range(ntri):
                tri = verts[t*3:(t+1)*3]
                all_tris.append((p['tex'], p['shader'], tri))
    print('Janemba: %d triangulos, %d verts expandidos' % (
        len(all_tris), len(all_tris)*3))

    # 2) Decimar por voxel grid (fusionar posiciones cercanas)
    cell = 0.10
    verts = []
    for tex, shader, tri in all_tris:
        for v in tri:
            verts.append(v)
    # decimar: cada posicion unica (voxel) -> indice
    cell_map = {}
    unique = []  # vertices unicos (bytes HD)
    for v in verts:
        vx, vy, vz = v['pos']
        key = (int(vx/cell), int(vy/cell), int(vz/cell))
        if key not in cell_map:
            cell_map[key] = len(unique)
            vx, vy, vz = v['pos']
            nx, ny, nz = v['nrm']
            u, vt = v['uv']
            unique.append(f32(float('nan')) + f32(vt) + f32(u) + f32(vz) +
                          f32(vx) + f32(vy) + f32(1.0) + f32(0.0) +
                          f32(nz) + f32(-ny) + f32(nx))
    print('Decimado: %d vertices unicos' % len(unique))

    # 3) Agrupar triangulos en 13 rangos por material
    #    Material dominante (tex=16) primero = cuerpo. Luego los demas.
    from collections import defaultdict
    by_tex = defaultdict(list)
    for tex, shader, tri in all_tris:
        by_tex[(tex, shader)].append(tri)
    # Ordenar materiales: dominante primero, luego por tamaño desc
    materials = sorted(by_tex.items(), key=lambda x: -len(x[1]))

    # 4) Construir IB agrupado (indices de los vertices unicos)
    #    Para cada material, agregar sus triangulos al IB
    ib = bytearray()
    ranges = []  # (start_index, end_index) por rango
    total_idx = 0
    n_ranges = min(13, len(materials))
    for mi in range(n_ranges):
        (tex, shader), tris = materials[mi]
        start = total_idx
        for tri in tris:
            for v in tri:
                vx, vy, vz = v['pos']
                key = (int(vx/cell), int(vy/cell), int(vz/cell))
                ib += U16.pack(cell_map[key])
                total_idx += 1
        ranges.append((start, total_idx))
        print('  rango %d (tex=%d shader=%d): [%d, %d) = %d indices' % (
            mi, tex, shader, start, total_idx, total_idx-start))
    # Materiales sobrantes -> añadir al ultimo rango
    for mi in range(n_ranges, len(materials)):
        (tex, shader), tris = materials[mi]
        for tri in tris:
            for v in tri:
                vx, vy, vz = v['pos']
                key = (int(vx/cell), int(vy/cell), int(vz/cell))
                ib += U16.pack(cell_map[key])
                total_idx += 1
    print('IB total: %d indices' % total_idx)

    # 5) Construir AWO usando Krillin como plantilla
    awo = bytearray(krillin[0x40:])
    amg_am = u32r(awo, 0x18)
    tbl = u32r(awo, 0x1C)
    offs = [u32r(awo, tbl + i * 4) for i in range(amg_am)]
    awg0_off, awg1_off = offs[0], offs[1]
    AWG = awg0_off
    sec34_rel = u32r(awo, AWG + 0x34)
    vb2_rel = u32r(awo, AWG + 0x2C)
    ib_rel = u32r(awo, AWG + 0x30)
    restart_rel = u32r(awo, AWG + 0x38)
    awg0_size = awg1_off - awg0_off

    n_sec34 = len(unique)
    n_vb2 = 226
    vb2_size = n_vb2 * 44
    sec34_start = sec34_rel + 2
    new_vb2_rel = ((sec34_start + n_sec34 * 44) + 0xF) & ~0xF
    new_ib_rel = new_vb2_rel + vb2_size
    new_ib_size = ((total_idx + 1) & ~1) * 2
    new_restart_rel = ((new_ib_rel + new_ib_size) + 0xF) & ~0xF
    new_awg0_size = new_restart_rel + (awg0_size - restart_rel)
    delta_awgs = new_awg0_size - awg0_size

    awg0_data = bytes(awo[awg0_off:awg1_off])
    new_awg0 = bytearray(awg0_data[:sec34_rel])
    new_awg0 += bytes(2)
    new_awg0 += b''.join(unique)
    new_awg0 += bytes(new_vb2_rel - len(new_awg0))
    new_awg0 += awg0_data[vb2_rel:ib_rel][:vb2_size]
    new_awg0 += bytes(new_ib_rel - len(new_awg0))
    new_awg0 += bytes(ib)
    if len(new_awg0) % 2:
        new_awg0 += b'\x00'
    new_awg0 += bytes(new_restart_rel - len(new_awg0))
    new_awg0 += awg0_data[restart_rel:]
    if len(new_awg0) < new_awg0_size:
        new_awg0 += bytes(new_awg0_size - len(new_awg0))

    pack_u32(new_awg0, 0x2C, new_vb2_rel)
    pack_u32(new_awg0, 0x30, new_ib_rel)
    pack_u32(new_awg0, 0x34, sec34_rel)
    pack_u32(new_awg0, 0x38, new_restart_rel)

    # 6) Re-mapear los offsets del IB en los bloques shadow (sello 0x204) del arm.
    #    ATENCION: el runtime (guest) valida que los conteos sec34/vb2/IB del
    #    AWG0 sean coherentes con los offsets de los arms. Re-mapear los arms a
    #    rangos que no coinciden con la estructura original provoca cuelgue de
    #    arranque. Mantener los arms INTACTOS (como janemba_amb10 que si
    #    cargaba). El re-mapeo de geometria se hace reordenando el IB.
    mg = 0x1F80
    count = u32r(new_awg0, mg + 0x00)
    tbl_mr_rel = u32r(new_awg0, mg + 0x28)  # rel magic
    tbl_mr = tbl_mr_rel  # dentro de new_awg0 (rel magic)
    print('Mesh group count: %d, tabla rel magic @0x%X (arms intactos)' % (count, tbl_mr))

    # 7) Ensamblar AWO + tabla AMG + punteros zona ejes
    new_awo = bytearray(awo[:awg0_off])
    new_awo += new_awg0
    new_awo += awo[awg1_off:]
    for i in range(1, amg_am):
        pack_u32(new_awo, tbl + i * 4, offs[i] + delta_awgs)
    axes_base = u32r(awo, 0x34)
    for j in range(0x34, 0x700, 4):
        if tbl <= j < tbl + amg_am * 4:
            continue
        v = u32r(new_awo, j)
        if axes_base <= v < len(awo):
            pack_u32(new_awo, j, v + delta_awgs)

    # 8) Repack AMB
    n_orig = u32r(krillin, 0x0C)
    azt_loc = None
    for i in range(n_orig):
        e = 0x20 + i * 16
        loc, sz = u32r(krillin, e), u32r(krillin, e + 4)
        if krillin[loc:loc + 4] == b'#AZT':
            azt_loc, azt_size = loc, sz
            break
    if azt_loc is None:
        print('ERROR: AZT no encontrado')
        return
    azt_start = ((0x40 + len(new_awo) + 15) & ~15)
    amb = bytearray()
    amb += b'#AMB'
    amb += struct.pack('>I', 0x20) + struct.pack('>I', 0)
    amb += struct.pack('>I', 2) + struct.pack('>I', 2)
    amb += struct.pack('>I', 0x20) + struct.pack('>I', 0x40)
    amb += struct.pack('>I', 0) + struct.pack('>I', 0x40)
    amb += struct.pack('>I', len(new_awo))
    amb += struct.pack('>I', 1) + struct.pack('>I', 0)
    amb += struct.pack('>I', azt_start) + struct.pack('>I', azt_size)
    amb += struct.pack('>I', 2) + struct.pack('>I', 0)
    amb += bytes(0x40 - len(amb))
    amb += new_awo
    amb += bytes(azt_start - len(amb))
    amb += krillin[azt_loc:azt_loc + azt_size]

    with open(out, 'wb') as f:
        f.write(bytes(amb))
    print('AMB Janemba v3: %d bytes' % len(amb))
    print('Guardado: %s' % out)


if __name__ == '__main__':
    main()
