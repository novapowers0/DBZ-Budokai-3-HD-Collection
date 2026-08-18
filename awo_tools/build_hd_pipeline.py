"""
Pipeline completo PS2 -> HD con el parser de malla correcto.

Usa parse_ps2_mesh (FaceType/VertCount de submeshes) para obtener los
triangulos reales, y SkinData para el skinning (posiciones locales + bone).

Flujo:
  1. Parsear todos los AMGs del AMO0 PS2 -> verts (pos absoluta) + tris.
  2. Para cada part, mapear los voff del skin a los indices de vertice.
  3. Construir verts HD (layout: [nan,u,v,z,x,y,peso,bone,nz,-ny,nx]).
  4. Decimar por voxel (fusion de posiciones locales).
  5. Reconstruir IB de triangulos.
  6. Empaquetar en el bin HD (build_janemba2).

Uso:
  python build_hd_pipeline.py <bin_ps2> <bin_hd_template> <cell> <max_tri> <out_bin>
"""

import os
import struct
import sys

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..\..', 'awo_tools')))
from parse_ps2_mesh import r32, parse_part_mesh, VERT_STRIDE
from convert_personaje import SkinData
from extract_geometry import PS2Model
from pose_matrix import build_world_mats, apply_mat

F32 = struct.Struct('>f')
U32 = struct.Struct('>I')


def f32(v):
    return F32.pack(v)


def u32(v):
    return U32.pack(v)


def build_vertex_hd(pos_local, bone_idx, weight, nrm, uv):
    """Vertice local HD: [nan, u, v, z, x, y, peso, BONE, nz, -ny, nx]."""
    x, y, z = pos_local
    nx, ny, nz = nrm
    u, v = uv
    return (f32(float('nan')) + f32(u) + f32(v) +
            f32(z) + f32(x) + f32(y) +
            f32(float(weight)) + u32(bone_idx & 0xFFFFFFFF) +
            f32(nz) + f32(-ny) + f32(nx))


def build_vertex_vb2(pos_abs, nrm, uv):
    """Vertice del buffer secundario HD (vb2), layout del bin e326 de Krillin.

    Verificado en real_e326.bin (= b327_hd.bin, el bin visible de Krillin):
      +0 pos.x_abs +4 pos.y_abs +8 pos.z_abs
      +12 0 +16 0 +20 0 +24 peso=0
      +28 0xFFFFFFFF (bone = sin skin, posiciones absolutas)
      +32 nx +36 ny +40 nz
    """
    x, y, z = pos_abs
    nx, ny, nz = nrm
    u, v = uv
    return (f32(x) + f32(y) + f32(z) +
            f32(0.0) + f32(0.0) + f32(0.0) +
            f32(0.0) + u32(0xFFFFFFFF) +
            f32(nx) + f32(ny) + f32(nz))


def main():
    if len(sys.argv) < 6:
        print('Uso: build_hd_pipeline.py <bin_ps2> <bin_hd_template> <cell> <max_tri> <out_bin> [scale]')
        return
    ps2 = open(sys.argv[1], 'rb').read()
    templ = open(sys.argv[2], 'rb').read()
    cell = float(sys.argv[3])
    max_tri = int(sys.argv[4])
    out = sys.argv[5]
    scale = float(sys.argv[6]) if len(sys.argv) > 6 else 0.5

    amo = 0x40
    model = PS2Model(ps2)
    amg_offsets = model.amg_offsets()

    # Matriz de pose world de cada hueso (bind pose PS2). Se usa para
    # transformar las parts estaticas (sin skin) de su espacio local de hueso
    # al espacio absoluto del modelo, que es lo que espera el vb2 del HD.
    world_mats, _parents = build_world_mats(ps2, model.amo0)

    # Recolectar mesh parts de todos los AMGs
    all_parts = []  # (amg_base, part dict)
    for ai, amg_off in enumerate(amg_offsets):
        amg_base = model.amo0 + amg_off
        for p in model.mesh_parts(amg_base):
            all_parts.append((ai, amg_base, p))

    # Para cada part: parsear verts (pos abs, nrm, uv) + tris (indices locales a la part)
    # y mapear el skin (voff -> vertice local)
    hd_sec34 = []      # bytes layout HD skinned
    hd_vb2 = []        # bytes layout HD estatico
    hd_sec_abs = []    # posiciones absolutas originales sec34 (para poda area)
    hd_vb_abs = []     # posiciones absolutas originales vb2
    hd_tris = []       # triangulos con indices globales HD
    sec_total = 0      # acumulado global de sec34
    VB_SENTINEL = 0x7FFFFFFF

    # Skin por AMG: voff = offset del vertice REL AMG base. Construimos un
    # mapa (amg_idx, voff_rel) -> (bone, weight, coords).
    skin_map = {}
    for ai, amg_off in enumerate(amg_offsets):
        amg_base = model.amo0 + amg_off
        skin = SkinData(ps2, amg_base)
        for bone_idx, weight, coords, voff in skin.entries:
            skin_map[(ai, voff)] = (bone_idx, weight, coords)
    # Contar skin mapeado para reportar
    total_skinned = 0
    total_verts = 0

    for i, (ai, amg_base, p) in enumerate(all_parts):
        po = p['po']
        size_field = r32(ps2, po + 0x90)
        mesh_size = (size_field - 0x60000000) * 16 if size_field >= 0x60000000 else 0
        md = po + 0xA0
        # El mesh_size del PS2 cubre TODOS los submeshes de la part (es el
        # tamano real del mesh data). No limitar al siguiente po (eso corta
        # submeshes). Solo detener si excede el archivo.
        end = min(md + mesh_size if mesh_size > 0 else po + 0x400, len(ps2))
        # Stride real del vertice: viene del MeshType[1] = primer byte del type1
        # del mesh part (0x1B5 -> B5 48B, 0x1B4 -> B4 32B facial, 0x190 -> 16B).
        # Si el type1 no tiene flag, leer el byte del offset po.
        t1 = p['type1']
        # El Goku/B1 usa stride 48 en TODAS las parts (incluso 0xB4 que el
        # parser asume 32B facial). Forzar 48 para personajes.
        vtype = 0xB5  # forzar B5 (48B) para modelos de personaje
        stride = VERT_STRIDE.get(vtype, 48)
        # parsear submeshes registrando el OFFSET REAL de cada vertice (rel AMG),
        # porque los headers 0x20 entre submeshes rompen la contiguidad.
        verts_raw = []  # posiciones absolutas
        nrm_raw = []
        uv_raw = []
        voff_raw = []  # offset real del vertice rel AMG base
        sub_tris = []
        pos = md
        while pos + 0x20 < min(end, len(ps2)):
            facetype = r32(ps2, pos + 0x10)
            vertcount = r32(ps2, pos + 0x14)
            if vertcount == 0 or vertcount > 0xFFFF:
                break
            vpos = pos + 0x20
            if vpos + vertcount * stride > min(end, len(ps2)):
                break
            base_v = len(verts_raw)
            for x in range(vertcount):
                px, py, pz = struct.unpack_from('<fff', ps2, vpos)
                nx, ny, nz = struct.unpack_from('<fff', ps2, vpos + 16)
                tu, tv = struct.unpack_from('<ff', ps2, vpos + 32)
                verts_raw.append((px, py, pz))
                nrm_raw.append((nx, ny, nz))
                uv_raw.append((tu, tv))
                voff_raw.append(vpos - amg_base)
                vpos += stride
            # caras segun facetype
            if facetype == 1:  # strip
                f1, f2 = 0, 1
                direction = -1
                for x in range(2, vertcount):
                    f3 = x
                    direction *= -1
                    if f1 != f2 and f2 != f3 and f3 != f1:
                        if direction > 0:
                            sub_tris.append((base_v + f1, base_v + f2, base_v + f3))
                        else:
                            sub_tris.append((base_v + f1, base_v + f3, base_v + f2))
                    f1, f2 = f2, f3
            else:  # tripletes
                for x in range(1, vertcount + 1, 3):
                    if x + 2 <= vertcount:
                        sub_tris.append((base_v + x - 1, base_v + x, base_v + x + 1))
            pos = vpos

        # Skin por vertice: las coords locales del SkinData (posicion en el
        # espacio del hueso). El skin mapea voff (offset real) -> (bone, weight, coords).
        part_skin = {}
        for vi in range(len(verts_raw)):
            key = (ai, voff_raw[vi])
            if key in skin_map:
                part_skin[vi] = skin_map[key]

        # Construir verts HD. Dos buffers como el HD original de Krillin:
        #   sec34 = skinned (layout [nan,u,v,z,x,y,peso,bone,nz,-ny,nx])
        #   vb2   = estatico (layout [x_abs,y_abs,z_abs,0,0,0,0,0xFFFFFFFF,nx,ny,nz])
        # El HD de Krillin NO skinnea bones >35 (piernas 38-47, pies, NLA).
        # Esos vertices van al vb2 estatico (el guest no tiene sus matrices).
        part_sec34 = []
        part_vb2 = []
        part_sec_abs = []  # posiciones absolutas originales (para poda de area)
        part_vb_abs = []
        # Matriz world del hueso de la part para transformar estaticos
        part_bone = p['bone_idx']
        bm, bp = world_mats.get(part_bone, ([[1, 0, 0], [0, 1, 0], [0, 0, 1]], [0, 0, 0]))
        for vi in range(len(verts_raw)):
            if vi in part_skin:
                bone, weight, coords = part_skin[vi]
                if bone > 35:
                    # El HD de Krillin NO skinnea bones 36-50 (verificado: su
                    # sec34 solo usa 0-35). Las piernas/pies van al vb2 estatico.
                    wx, wy, wz = apply_mat(bm, bp, verts_raw[vi])
                    part_vb2.append(build_vertex_vb2((wx * scale, wy * scale, wz * scale), nrm_raw[vi], uv_raw[vi]))
                    part_vb_abs.append((wx, wy, wz))
                else:
                    # coords locales del skin PS2 -> sec34 skinned
                    lx, ly, lz = coords[0] * scale, coords[1] * scale, coords[2] * scale
                    part_sec34.append(build_vertex_hd((lx, ly, lz), bone, weight, nrm_raw[vi], uv_raw[vi]))
                    sbm, sbp = world_mats.get(bone, ([[1, 0, 0], [0, 1, 0], [0, 0, 1]], [0, 0, 0]))
                    wx, wy, wz = apply_mat(sbm, sbp, coords)
                    part_sec_abs.append((wx, wy, wz))
            else:
                # part estatica (manos/cara): coords locales del hueso -> absoluto
                wx, wy, wz = apply_mat(bm, bp, verts_raw[vi])
                part_vb2.append(build_vertex_vb2((wx * scale, wy * scale, wz * scale), nrm_raw[vi], uv_raw[vi]))
                part_vb_abs.append((wx, wy, wz))
        # Remapear indices locales (orden original verts_raw) al orden
        # sec34+vb2 de part_hd
        local_remap = {}
        idx_sk = 0
        idx_vb = 0
        for vi in range(len(verts_raw)):
            is_skinned = vi in part_skin and part_skin[vi][0] <= 35
            if is_skinned:
                local_remap[vi] = idx_sk
                idx_sk += 1
            else:
                local_remap[vi] = len(part_sec34) + idx_vb
                idx_vb += 1
        n_sk = len(part_sec34)
        total_skinned += n_sk
        total_verts += len(part_sec34) + len(part_vb2)

        # Emitir en espacio unificado: sec34 GLOBAL contiguo (0..n_sec_global-1),
        # luego vb2 global (n_sec_global..). Marcamos los vb2 con un sentinel
        # para corregirlos al final cuando se conozca n_sec_total.
        VB_SENTINEL = 0x7FFFFFFF
        n_sk_part = len(part_sec34)
        n_vb_part = len(part_vb2)
        sec_so_far = sec_total
        vb_so_far = len(hd_vb2)
        for t0, t1, t2 in sub_tris:
            r0, r1, r2 = local_remap[t0], local_remap[t1], local_remap[t2]
            g0 = sec_so_far + r0 if r0 < n_sk_part else VB_SENTINEL + vb_so_far + (r0 - n_sk_part)
            g1 = sec_so_far + r1 if r1 < n_sk_part else VB_SENTINEL + vb_so_far + (r1 - n_sk_part)
            g2 = sec_so_far + r2 if r2 < n_sk_part else VB_SENTINEL + vb_so_far + (r2 - n_sk_part)
            hd_tris.append((g0, g1, g2))
        hd_sec34.extend(part_sec34)
        hd_vb2.extend(part_vb2)
        hd_sec_abs.extend(part_sec_abs)
        hd_vb_abs.extend(part_vb_abs)
        sec_total += n_sk_part

    # Corregir los sentinels: los vb2 van despues de TODO el sec34 global
    n_sec_total = sec_total
    n_vb2_total = len(hd_vb2)
    hd_tris = [(i0 - VB_SENTINEL + n_sec_total if i0 >= VB_SENTINEL else i0,
                i1 - VB_SENTINEL + n_sec_total if i1 >= VB_SENTINEL else i1,
                i2 - VB_SENTINEL + n_sec_total if i2 >= VB_SENTINEL else i2)
               for i0, i1, i2 in hd_tris]

    print('PS2 -> sec34=%d (skinned) + vb2=%d (estatico) = %d verts, %d triangulos' % (
        len(hd_sec34), len(hd_vb2), len(hd_sec34) + len(hd_vb2), len(hd_tris)))
    print('Skin mapeado: %d/%d (%.0f%%)' % (total_skinned, total_verts,
                                            100 * total_skinned / total_verts if total_verts else 0))

    # --- Decimar sec34 por voxel. Usamos la posicion WORLD PS2 (hd_sec_abs)
    # para fusionar, porque las coords locales por hueso no son comparables
    # entre huesos (cada hueso tiene su origen) y con escala darian celdas raras.
    n_sec = len(hd_sec34)
    cell_map = {}
    unique_sec = []
    sec_abs_unique = []  # pos absoluta promedio por celda sec34
    remap_sec = {}
    for i, vb in enumerate(hd_sec34):
        px, py, pz = hd_sec_abs[i]
        key = (int(px / cell), int(py / cell), int(pz / cell))
        if key not in cell_map:
            cell_map[key] = len(unique_sec)
            unique_sec.append(vb)
            sec_abs_unique.append(hd_sec_abs[i])
        remap_sec[i] = cell_map[key]

    # El vb2 (cabeza/caras estaticas) no se decima: se mantiene tal cual.
    unique_vb2 = hd_vb2
    # Indices globales: sec34 (0..n_sec-1) primero, luego vb2 (n_sec..)
    # Construir tris con remapeo sec34 -> unique_sec, vb2 -> +len(unique_sec)
    remap_all = {}
    for i in range(n_sec):
        remap_all[i] = remap_sec[i]
    n_sec_unique = len(unique_sec)
    for i in range(len(hd_vb2)):
        remap_all[n_sec + i] = n_sec_unique + i

    new_tris = []
    for t in hd_tris:
        r0, r1, r2 = remap_all[t[0]], remap_all[t[1]], remap_all[t[2]]
        if r0 == r1 or r1 == r2 or r0 == r2:
            continue
        new_tris.append((r0, r1, r2))

    # Si exceden max_tri, eliminar triangulos grandes. El area se calcula con
    # las posiciones del VERTICE HD FINAL (coords locales escaladas para
    # sec34 en +12/+16/+20, absolutas escaladas para vb2 en +0/+4/+8). Asi se
    # refleja lo que el renderizador ve, y no se elimina el cuerpo por tener
    # posiciones absolutas grandes en el espacio PS2.
    all_unique = unique_sec + unique_vb2
    n_sec_u = len(unique_sec)
    while len(new_tris) > max_tri:
        def vert_pos(vb, is_vb2):
            if is_vb2:
                return (struct.unpack('>f', vb[0:4])[0],
                        struct.unpack('>f', vb[4:8])[0],
                        struct.unpack('>f', vb[8:12])[0])
            return (struct.unpack('>f', vb[16:20])[0],
                    struct.unpack('>f', vb[20:24])[0],
                    struct.unpack('>f', vb[12:16])[0])

        def tri_area(t):
            a = vert_pos(all_unique[t[0]], t[0] >= n_sec_u)
            b = vert_pos(all_unique[t[1]], t[1] >= n_sec_u)
            c = vert_pos(all_unique[t[2]], t[2] >= n_sec_u)
            abx, aby = b[0] - a[0], b[1] - a[1]
            acx, acy = c[0] - a[0], c[1] - a[1]
            return abs(abx * acy - aby * acx)
        wi, wa = 0, -1
        step = max(1, len(new_tris) // 2000)
        for i in range(0, len(new_tris), step):
            a = tri_area(new_tris[i])
            if a > wa:
                wa, wi = a, i
        del new_tris[wi]

    # Compactar indices: primero los sec34 usados (contiguos 0..n1-1),
    # luego los vb2 usados (n1..). Reconstruir verts en ese orden.
    remap2 = {}
    final_sec34 = []
    final_vb2 = []
    # pase 1: sec34
    for t in new_tris:
        for v in t:
            if v < n_sec_unique and v not in remap2:
                remap2[v] = len(final_sec34)
                final_sec34.append(all_unique[v])
    n1 = len(final_sec34)
    # pase 2: vb2
    for t in new_tris:
        for v in t:
            if v >= n_sec_unique and v not in remap2:
                remap2[v] = n1 + len(final_vb2)
                final_vb2.append(all_unique[v])

    final_ib = b''.join(struct.pack('>HHH', remap2[t[0]], remap2[t[1]], remap2[t[2]]) for t in new_tris)

    print('Final: sec34=%d + vb2=%d = %d verts, %d tris' % (
        len(final_sec34), len(final_vb2), len(final_sec34) + len(final_vb2), len(new_tris)))

    # Escribir verts separados e IB
    with open(out + '.sec34', 'wb') as f:
        f.write(b''.join(final_sec34))
    with open(out + '.vb2', 'wb') as f:
        f.write(b''.join(final_vb2))
    with open(out + '.ib', 'wb') as f:
        f.write(final_ib)
    print('Guardado: %s.{sec34,vb2,ib} (%d, %d, %d)' % (out,
          len(final_sec34) * 44, len(final_vb2) * 44, len(final_ib)))


if __name__ == '__main__':
    main()
