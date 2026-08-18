"""port_ps2_to_b3.py - Port COMPLETO de un modelo PS2 (#AMO0) a un bin HD B3 (#AWO).

Estrategia (leccion B1 SESION11 + B3 sesion Janemba v6):
  - El runtime HD B3 exige que el AWG0 mantenga TAMANO y conteos compatibles
    (sec34=1956, vb2=226, IB=5140). Cambiarlos crashea (item 23/24 AGENTS).
  - Se construye sec34 + IB REALES desde los triangulos del PS2 (FaceType),
    se decima a los conteos de Krillin, y se rellenan los buffers del bin
    plantilla EN SU POSICION (delta=0, tail intacto).
  - Se regeneran descriptores de submesh (+50/+54/+58/+5C) y arms con los
    rangos reales del nuevo IB. La pieza que faltaba en Janemba.

Requisito: esqueleto PS2 1:1 con el HD (Krillin KLL <-> KLL, 51 bones).

Layout vertice B3 (44B, verificado en b327_hd.bin):
  +0  0xFFFFFFFF (marker u32)
  +4  u (float)  +8  v (float)
  +12 z_local    +16 x_local  +20 y_local
  +24 peso       +28 BONE(u32)+32 nz +36 -ny +40 nx

Descriptores submesh B3 (stride 0x60, entre arms y sec34):
  +00 label 16B | +10 0x09000000 | +14 0x0F000000 | +18 "max N m"
  +50 A_start<<8 | +54 A_size<<8 | +58 B_start<<8 | +5C B_size<<8|1

AWG0 header B3: +0x2C vb2 rel | +0x30 IB rel | +0x34 sec34 rel | +0x38 end rel

Uso:
  python port_ps2_to_b3.py <ps2.amb|amo> <hd.awo> <out.awo>
"""
import struct
import sys
import re

U32 = struct.Struct('>I')
F32 = struct.Struct('>f')
U16 = struct.Struct('>H')
R32 = struct.Struct('<I')
RF32 = struct.Struct('<f')

MAX_SEC = 1956
MAX_IB = 5140


def u32r(b, o):
    return U32.unpack_from(b, o)[0]


def f32r(b, o):
    return F32.unpack_from(b, o)[0]


def r32(b, o):
    return R32.unpack_from(b, o)[0]


def rf32(b, o):
    return RF32.unpack_from(b, o)[0]


def f32(v):
    return F32.pack(v)


def u32(v):
    return U32.pack(v & 0xFFFFFFFF)


def u16(v):
    return U16.pack(v & 0xFFFF)


VERT_STRIDE = {0xBD: 48, 0xFD: 48, 0x3D: 48, 0xB5: 48, 0xB6: 48, 0xF5: 48,
               0x199: 32, 0xB4: 32, 0xA4: 32, 0x99: 32, 0x92: 32, 0x19: 32,
               0x90: 16}


def read_ps2_vert(b, off, vtype):
    if vtype in (0xBD, 0xFD, 0x3D, 0xB5, 0xB6, 0xF5):
        v = (rf32(b, off), rf32(b, off + 4), rf32(b, off + 8))
        n = (rf32(b, off + 16), rf32(b, off + 20), rf32(b, off + 24))
        u = (rf32(b, off + 32), rf32(b, off + 36))
        return v, n, u
    if vtype == 0x199:
        return (rf32(b, off), rf32(b, off + 4), rf32(b, off + 8)), \
               (rf32(b, off + 16), rf32(b, off + 20), rf32(b, off + 24)), (0.0, 0.0)
    if vtype in (0xB4, 0xA4, 0x99, 0x92, 0x19):
        return (rf32(b, off), rf32(b, off + 4), rf32(b, off + 8)), \
               (0.0, 0.0, 0.0), (rf32(b, off + 16), rf32(b, off + 20))
    return (rf32(b, off), rf32(b, off + 4), rf32(b, off + 8)), (0.0, 0.0, 0.0), (0.0, 0.0)


def read_faces(vertcount, facetype):
    faces = []
    if facetype == 1:
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
        for x in range(1, vertcount + 1, 3):
            if x + 2 <= vertcount:
                faces.append((x - 1, x, x + 1))
    return faces


def parse_ps2_full(ps2, amo0):
    """Parsea TODOS los AMGs del PS2.

    Devuelve:
      parts = [{'bone_idx', 'vtype', 'verts': [(pos,nrm,uv)], 'tris': [(i0,i1,i2)],
                'abs_off', 'gi'}]
      skin_map[(gi, pi, vi)] = (bone, weight, coords_locales)
    """
    n_amg = r32(ps2, amo0 + 0x18)
    amg_tbl = amo0 + 0x30
    parts_all = []
    skin_map = {}

    for gi in range(n_amg):
        amg_off = r32(ps2, amg_tbl + gi * 4)
        amg = amo0 + amg_off
        bone_am = r32(ps2, amg + 0x10)
        axes_loc = r32(ps2, amg + 0x14)
        part_ranges = []

        for bi in range(bone_am):
            e0 = amg + axes_loc + bi * 80
            p34 = r32(ps2, e0 + 0x34)
            if not p34:
                continue
            arm = amg + p34
            mesh_hdr = r32(ps2, arm + 4)
            if not mesh_hdr:
                continue
            mg = amg + mesh_hdr
            mp_amnt = r32(ps2, mg)
            if mp_amnt == 0 or mp_amnt > 64:
                continue
            part_offs = [r32(ps2, mg + 16 + i * 4) for i in range(mp_amnt)]
            for pi, rel in enumerate(part_offs):
                po = mg + rel
                type1 = r32(ps2, po)
                vtype = type1 & 0xFF
                stride = VERT_STRIDE.get(vtype, 48)
                size_field = r32(ps2, po + 0x90)
                mesh_size = (size_field - 0x60000000) * 16 if size_field >= 0x60000000 else 0
                md = po + 0xA0
                end = md + mesh_size if mesh_size > 0 else po + 0x400
                end = min(end, len(ps2))
                verts, tris = [], []
                pos = md
                abs_off = amg + md
                base_v = 0
                while pos + 0x20 < end:
                    facetype = r32(ps2, pos + 0x10)
                    vertcount = r32(ps2, pos + 0x14)
                    if vertcount == 0 or vertcount > 0xFFFF:
                        break
                    vp = pos + 0x20
                    if vp + vertcount * stride > end:
                        break
                    for x in range(vertcount):
                        verts.append(read_ps2_vert(ps2, vp + x * stride, vtype))
                    for f0, f1, f2 in read_faces(vertcount, facetype):
                        tris.append((base_v + f0, base_v + f1, base_v + f2))
                    base_v = len(verts)
                    pos = vp + vertcount * stride
                part = {'bone_idx': bi, 'vtype': vtype, 'verts': verts,
                        'tris': tris, 'abs_off': abs_off, 'gi': gi}
                parts_all.append(part)
                part_ranges.append((len(parts_all) - 1, abs_off, len(verts) * stride))

        for bi in range(bone_am):
            e0 = amg + axes_loc + bi * 80
            p34 = r32(ps2, e0 + 0x34)
            if not p34:
                continue
            arm = amg + p34
            rig_ptr = r32(ps2, arm + 8)
            if not rig_ptr:
                continue
            r = amg + rig_ptr
            chunk_amnt = r32(ps2, r + 12)
            for i in range(chunk_amnt):
                c = r + 16 + i * 32
                weight = rf32(ps2, c)
                ch_len = r32(ps2, c + 4)
                ch_loc = r32(ps2, c + 8)
                if not ch_loc:
                    continue
                for e in range(ch_len):
                    entry = amg + ch_loc + e * 32
                    coords = (rf32(ps2, entry), rf32(ps2, entry + 4), rf32(ps2, entry + 8))
                    voff = r32(ps2, entry + 12)
                    abs_off = amg + voff
                    for pi2, p_abs, p_len in part_ranges:
                        if p_abs <= abs_off < p_abs + p_len:
                            vi = (abs_off - p_abs) // 48
                            skin_map[(gi, pi2, vi)] = (bi, weight, coords)
                            break
    return parts_all, skin_map


def build_vertex_44(pos, weight, bone, nrm, uv):
    x, y, z = pos
    nx, ny, nz = nrm
    u, v = uv
    return (u32(0xFFFFFFFF) + f32(u) + f32(v) + f32(z) + f32(x) + f32(y) +
            f32(weight) + u32(bone) + f32(nz) + f32(-ny) + f32(nx))


def build_buffers(parts, skin_map):
    """sec34 (dedup por bytes) + IB REAL desde los triangulos PS2.

    Devuelve (sec_bytes, ib_indices, part_a, part_b, ib_bones).
    """
    sec = []
    sec_map = {}
    ib = []
    part_a = []
    part_b = []
    for pi, p in enumerate(parts):
        a_start = len(sec)
        b_start = len(ib)
        # 1) expandir verts -> sec34 (dedup)
        vremap = []
        for vi, (pos, nrm, uv) in enumerate(p['verts']):
            sk = skin_map.get((p['gi'], pi, vi))
            if sk:
                bone, weight, coords = sk
                pos = coords
            else:
                bone, weight = p['bone_idx'], 1.0
            vb = build_vertex_44(pos, weight, bone, nrm, uv)
            if vb not in sec_map:
                sec_map[vb] = len(sec)
                sec.append(vb)
            vremap.append(sec_map[vb])
        # 2) triangulos reales -> IB
        for a, bb, c in p['tris']:
            if a < len(vremap) and bb < len(vremap) and c < len(vremap):
                ia, ib2, ic = vremap[a], vremap[bb], vremap[c]
                if ia != ib2 and ib2 != ic and ia != ic:
                    ib.append(ia)
                    ib.append(ib2)
                    ib.append(ic)
        part_a.append((a_start, len(sec) - a_start))
        part_b.append((b_start, len(ib) - b_start))
    return sec, ib, part_a, part_b


def decimate(sec, ib, cell):
    """Decima verts por (bone, voxel) y reconstruye IB; descarta degenerados."""
    if isinstance(sec, list):
        sec = b''.join(sec)
    n_orig = len(sec) // 44
    cell_of = {}
    rep_of = {}

    for i in range(n_orig):
        off = i * 44
        bone = u32r(sec[off + 28:off + 32], 0)
        px = struct.unpack('>f', sec[off + 16:off + 20])[0]
        py = struct.unpack('>f', sec[off + 20:off + 24])[0]
        pz = struct.unpack('>f', sec[off + 12:off + 16])[0]
        key = (bone, int(px / cell), int(py / cell), int(pz / cell))
        cell_of[i] = key
        if key not in rep_of:
            rep_of[key] = i

    new_ib = []
    for t in range(0, len(ib) - 2, 3):
        a, b, c = ib[t], ib[t + 1], ib[t + 2]
        if a >= n_orig or b >= n_orig or c >= n_orig:
            continue
        ra, rb, rc = rep_of[cell_of[a]], rep_of[cell_of[b]], rep_of[cell_of[c]]
        if ra == rb or rb == rc or ra == rc:
            continue
        new_ib.append((ra, rb, rc))

    rep_list = []
    rep_idx = {}
    for t in new_ib:
        for v in t:
            if v not in rep_idx:
                rep_idx[v] = len(rep_list)
                rep_list.append(v)
    compact_ib = []
    for t in new_ib:
        compact_ib.append((rep_idx[t[0]], rep_idx[t[1]], rep_idx[t[2]]))

    out_vb = b''.join(sec[r * 44:(r + 1) * 44] for r in rep_list)
    out_ib = []
    for t in compact_ib:
        out_ib.extend(t)
    return out_vb, out_ib, len(rep_list)


def main():
    if len(sys.argv) < 4:
        print('Uso: port_ps2_to_b3.py <ps2.amb|amo> <hd.awo> <out.awo>')
        return
    ps2 = open(sys.argv[1], 'rb').read()
    base = bytearray(open(sys.argv[2], 'rb').read())
    out = sys.argv[3]

    amo0 = 0x40 if ps2[:4] == b'#AMB' else 0
    if ps2[amo0:amo0 + 4] not in (b'#AMO0', b'#AMO'):
        print('No es #AMO0 en 0x%X: %s' % (amo0, ps2[amo0:amo0 + 4]))
        return

    parts, skin_map = parse_ps2_full(ps2, amo0)
    nv = sum(len(p['verts']) for p in parts)
    nt = sum(len(p['tris']) for p in parts)
    print('PS2: %d parts, %d verts expandidos, %d tris reales, skin %d' % (
        len(parts), nv, nt, len(skin_map)))

    sec, ib, part_a, part_b = build_buffers(parts, skin_map)
    print('sin decimar: sec34=%d unicos | IB=%d indices (%d tris)' % (len(sec), len(ib), len(ib) // 3))

    # ---- decimar para caber en MAX_SEC/MAX_IB ----
    cell = 0.01
    while True:
        sec_b, ib_b, n_u = decimate(sec, ib, cell)
        n_tri = len(ib_b) // 3
        if n_u <= MAX_SEC and len(ib_b) <= MAX_IB:
            break
        cell *= 1.4
        if cell > 1.0:
            break
    print('decimado cell=%.3f: sec34=%d (max %d) | IB=%d (max %d) | %d tris' % (
        cell, n_u, MAX_SEC, len(ib_b), MAX_IB, n_tri))
    sec_blob = sec_b if isinstance(sec_b, bytes) else b''.join(sec_b)
    ib = ib_b

    # ---- rangos A/B por part tras decimar (no exactos, se regeneran globales) ----
    # ---- arms [bone, fin, 0, ini, 0] por bone del IB ----
    sec_blob = sec_blob if isinstance(sec_blob, bytes) else b''.join(sec_blob)
    ib_bone = []
    for i in range(0, len(ib), 3):
        for v in ib[i:i + 3]:
            off = v * 44
            ib_bone.append(u32r(sec_blob[off + 28:off + 32], 0))
    first_idx = {}
    last_idx = {}
    for i, bn in enumerate(ib_bone):
        first_idx.setdefault(bn, i)
        last_idx[bn] = i
    arms_range = {}
    for bn in first_idx:
        arms_range[bn] = (first_idx[bn] * 2, last_idx[bn] * 2 + 2)

    # ---- RE-EMPAQUETAR en el bin plantilla (TAMANO FIJO, delta=0) ----
    awo = 0x40
    n_awgs = u32r(base, awo + 0x18)
    awg_tbl = awo + u32r(base, awo + 0x1C)
    AWG0 = awo + u32r(base, awg_tbl)
    axes_rel = u32r(base, AWG0 + 0x14)
    arm_root_rel = u32r(base, AWG0 + axes_rel + 0x34)
    arm_root = AWG0 + arm_root_rel
    n_bones = u32r(base, AWG0 + 0x10)
    arms_zone_end = arm_root + n_bones * 0x14
    sec_rel = u32r(base, AWG0 + 0x34)
    struct_end = AWG0 + sec_rel
    # El sec34 REAL empieza en sec_rel+2 (align de 2 bytes, marker FFFFFFFF en +0).
    # El offset sec_rel+0 son 2 bytes de padding previos.
    sec_abs = struct_end + 2
    vb2_rel = u32r(base, AWG0 + 0x2C)
    ib_rel = u32r(base, AWG0 + 0x30)
    end_rel = u32r(base, AWG0 + 0x38)
    sec_size = (AWG0 + vb2_rel) - struct_end
    ib_size = (AWG0 + end_rel) - (AWG0 + ib_rel)
    print('AWG0 @0x%X | arms @0x%X..0x%X | desc @0x%X..0x%X | sec34 @0x%X(+2 align)..0x%X | vb2 @0x%X | IB @0x%X (+%#x) | end @0x%X' % (
        AWG0, arm_root, arms_zone_end, arms_zone_end, struct_end,
        sec_abs, AWG0 + vb2_rel, AWG0 + vb2_rel, AWG0 + ib_rel, ib_size, AWG0 + end_rel))
    n_sec_final = n_u
    # slots disponibles en el buffer sec34 (desde sec_rel+2 hasta vb2_rel)
    n_sec_slots = (sec_size - 2) // 44
    print('slots: sec=%d/%d | ib=%d/%d' % (n_sec_final, n_sec_slots, len(ib), ib_size // 2))

    if n_sec_final > n_sec_slots:
        print('ERROR: sec34 excede el buffer (%d > %d)' % (n_sec_final, n_sec_slots))
        return
    if len(ib) > ib_size // 2:
        print('ERROR: IB excede el buffer (%d > %d)' % (len(ib), ib_size // 2))
        return

    # ---- llenar sec34 (desde sec_rel+2) ----
    sec_bytes = sec_blob if isinstance(sec_blob, bytes) else b''.join(sec_blob)
    pad = (sec_size - 2) - len(sec_bytes)
    new_sec = sec_bytes + b'\x00' * pad
    base[sec_abs:sec_abs + (sec_size - 2)] = new_sec

    # ---- llenar IB (rellenar con 0xFFFF = restart) ----
    ib_bytes = b''.join(u16(i) for i in ib)
    pad_ib = ib_size - len(ib_bytes)
    new_ib = ib_bytes + b'\xff\xff' * (pad_ib // 2)
    base[AWG0 + ib_rel:AWG0 + ib_rel + ib_size] = new_ib

    # ---- descriptores de submesh (regenerar rangos A/B uniformes) ----
    z = bytearray(base[arms_zone_end:struct_end])
    anchors = sorted(m.start() for m in re.finditer(rb'max \d+ m', z))
    n_desc = len(anchors)
    print('Template: %d descriptores' % n_desc)
    n_sec_f = len(sec_blob) // 44
    if n_desc > 0:
        # distribuir sec34 e IB uniformemente entre los descriptores para que
        # los rangos queden DENTRO de los buffers reales (evita lecturas OOB).
        for k in range(n_desc):
            d = anchors[k] - 0x18
            a_start = (n_sec_f * k) // n_desc
            a_end = (n_sec_f * (k + 1)) // n_desc
            b_start = (len(ib) * k) // n_desc
            b_end = (len(ib) * (k + 1)) // n_desc
            z[d + 0x50:d + 0x54] = struct.pack('>I', (a_start << 8))
            z[d + 0x54:d + 0x58] = struct.pack('>I', ((a_end - a_start) << 8))
            z[d + 0x58:d + 0x5C] = struct.pack('>I', (b_start << 8))
            z[d + 0x5C:d + 0x60] = struct.pack('>I', ((b_end - b_start) << 8) | 1)
    base[arms_zone_end:struct_end] = bytes(z)

    # ---- arms regenerados ----
    for bn in range(n_bones):
        ao = arm_root + bn * 0x14
        if bn in arms_range:
            ini, fin = arms_range[bn]
            base[ao:ao + 4] = u32(bn)
            base[ao + 4:ao + 8] = u32(fin)
            base[ao + 8:ao + 12] = u32(0)
            base[ao + 12:ao + 16] = u32(ini)
            base[ao + 16:ao + 20] = u32(0)
        else:
            base[ao:ao + 20] = b'\x00' * 20

    open(out, 'wb').write(bytes(base))
    print('Guardado: %s (%d bytes)' % (out, len(base)))


if __name__ == '__main__':
    main()