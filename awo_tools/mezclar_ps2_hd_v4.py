"""
Mezclar posiciones PS2 en slots del sec34 HD con transformación de pose CORRECTA.

Hallazgo item 48: las matrices de pose HD y PS2 son IDÉNTICAS (51/51). Las
coords locales de AMBOS modelos están en el espacio del hueso. Para mapear:
  - coords locales HD -> world HD: aplicar matriz world del hueso HD
  - coords locales PS2 -> world PS2: aplicar matriz world del hueso PS2
  - buscar el vértice PS2 del MISMO hueso cuya world coincida con la world HD

Luego escribir la coords LOCAL PS2 (no la world) en el slot HD, porque el
runtime skinnea con la matriz del hueso.

Uso:
  python mezclar_ps2_hd_v4.py <bin_hd> <bin_ps2> <output_bin>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..\..', 'awo_tools')))
from extract_geometry import PS2Model
from convert_personaje import SkinData
from pose_matrix import quat_to_mat, mat_mul, apply_mat, build_world_mats

U32 = struct.Struct('>I')
F32 = struct.Struct('>f')


def u32(b, o): return U32.unpack_from(b, o)[0]
def f32(b, o): return F32.unpack_from(b, o)[0]
def pack_f32(v): return F32.pack(v)


def read_hd_world_mats(hd):
    """Matrices world del HD (zona -> idx -> local, jerarquia = PS2)."""
    AWO = 0x40
    bones = u32(hd, AWO + 0x10)
    local = {}
    for i in range(bones):
        zptr = u32(hd, AWO + 0x34 + i * 0x20)
        z = AWO + zptr
        idx = u32(hd, z + 4)
        mptr = u32(hd, z + 8)
        if mptr:
            m = AWO + mptr
            v = struct.unpack('>12f', hd[m:m + 48])
            local[idx] = (v[:4], v[4:7])
    z0 = AWO + 0x42360
    m0 = u32(hd, z0 + 8)
    v0 = struct.unpack('>12f', hd[AWO + m0:AWO + m0 + 48])
    local[0] = (v0[:4], v0[4:7])
    return local


def make_world(local_map, parents, cnt):
    cache = {}
    def get(i):
        if i in cache:
            return cache[i]
        q, p = local_map[i]
        m = quat_to_mat(q[0], q[1], q[2], q[3])
        pos = list(p)
        pid = parents.get(i, 0)
        if pid and pid <= cnt:
            pm, pp = get(pid - 1)
            m = mat_mul(pm, m)
            pos = [pm[0][0]*pos[0]+pm[0][1]*pos[1]+pm[0][2]*pos[2]+pp[0],
                   pm[1][0]*pos[0]+pm[1][1]*pos[1]+pm[1][2]*pos[2]+pp[1],
                   pm[2][0]*pos[0]+pm[2][1]*pos[1]+pm[2][2]*pos[2]+pp[2]]
        cache[i] = (m, pos)
        return m, pos
    return {i: get(i) for i in local_map}


def main():
    if len(sys.argv) < 4:
        print('Uso: mezclar_ps2_hd_v4.py <bin_hd> <bin_ps2> <output>')
        return
    hd = open(sys.argv[1], 'rb').read()
    ps2 = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    # PS2 world mats (con jerarquia del PS2, que es la misma)
    ps2_local, parents = {}, {}
    ps2_mats, ps2_parents = build_world_mats(ps2, 0x40)
    amo0 = 0x40
    cnt = struct.unpack('<I', ps2[amo0 + 0x10:amo0 + 0x14])[0]
    start = struct.unpack('<I', ps2[amo0 + 0x14:amo0 + 0x18])[0]
    for bi in range(cnt):
        e = amo0 + start + bi * 32
        bt = struct.unpack('<I', ps2[e + 4:e + 8])[0]
        bo = struct.unpack('<I', ps2[amo0 + bt + 8:amo0 + bt + 12])[0] if bt else 0
        if not bo:
            continue
        o = amo0 + bo
        v = [struct.unpack('<f', ps2[o + j * 4:o + j * 4 + 4])[0] for j in range(12)]
        ps2_local[bi] = (v[:4], v[4:7])
    ps2_world = make_world(ps2_local, ps2_parents, cnt)

    # PS2 skin: bone -> (coords locales, coords world)
    model = PS2Model(ps2)
    amg0_base = model.amo0 + model.amg_offsets()[0]
    skin = SkinData(ps2, amg0_base)
    ps2_by_bone = {}  # bone -> lista de coords locales
    ps2_world_by_bone = {}  # bone -> lista de coords world
    for bone, w, coords, voff in skin.entries:
        ps2_by_bone.setdefault(bone, []).append(coords)
        m, p = ps2_world.get(bone, (None, None))
        if m:
            ps2_world_by_bone.setdefault(bone, []).append(apply_mat(m, p, coords))

    # HD world mats
    hd_local = read_hd_world_mats(hd)
    hd_world = make_world(hd_local, ps2_parents, cnt)

    # HD sec34
    awo = bytearray(hd[0x40:])
    AWG = u32(awo, u32(awo, 0x1C))
    sec34 = u32(awo, AWG + 0x34)
    vb2 = u32(awo, AWG + 0x2C)
    n_sec = (vb2 - sec34 - 2) // 44

    changed = 0
    no_cov = 0
    for i in range(n_sec):
        off = AWG + sec34 + 2 + i * 44
        hb = u32(awo, off + 28)
        hm, hp = hd_world.get(hb, (None, None))
        cand = ps2_world_by_bone.get(hb)
        cand_local = ps2_by_bone.get(hb)
        if hm is None or not cand:
            no_cov += 1
            continue
        # coords local HD -> world HD
        hx = f32(awo, off + 16); hy = f32(awo, off + 20); hz = f32(awo, off + 12)
        hw = apply_mat(hm, hp, (hx, hy, hz))
        # buscar el PS2 world mas cercano del mismo hueso
        best = None
        best_d = 1e18
        best_local = None
        for idx, pw in enumerate(cand):
            d = (pw[0]-hw[0])**2 + (pw[1]-hw[1])**2 + (pw[2]-hw[2])**2
            if d < best_d:
                best_d = d
                best = pw
                best_local = cand_local[idx]
        if best_local is None:
            no_cov += 1
            continue
        # escribir coords LOCAL PS2 (el runtime skinnea con la matriz del hueso)
        awo[off + 12:off + 16] = pack_f32(best_local[2])
        awo[off + 16:off + 20] = pack_f32(best_local[0])
        awo[off + 20:off + 24] = pack_f32(best_local[1])
        changed += 1

    print('Slots reescritos (pose correcta): %d | sin cobertura: %d' % (changed, no_cov))

    amb = bytearray(hd[:0x40])
    amb += awo
    with open(out, 'wb') as f:
        f.write(bytes(amb))
    print('Guardado: %s (%d bytes)' % (out, len(amb)))


if __name__ == '__main__':
    main()
