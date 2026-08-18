"""
Matriz de pose de los huesos PS2 (#AMO0) — del MaxScript budokai_updated.ms.

Estructura (verificado en Krillin b327_ps2.bin):
  AMO0 header:
    +0x10 AmoData_Count | +0x14 AmoData_Start | +0x18 AmgCount
    +0x1C AmgOffsetTable | +0x20 Bonecount | +0x24 BoneNameTableOffset
  AmoData_Start: 32B por hueso [boneID, boneTable, t1, t2, t3, pad(0xc)]
    getOffset(boneTable): lee long en (amo0+boneTable+8) -> boneOffset
    getID(t3): lee long en (amo0+t3), +1 -> parentID (1-based)
  boneOffset: 12 floats = [c11..c14 quaternion, c21..c23 pos, c24..]
    tfm = quat(c11,c12,c13,-c14) as matrix3 ; tfm.row4 = [c21,c22,c23]
    Si parentID: tfm *= world(parent)  -> matriz world (bind pose)

Uso:
  from pose_matrix import build_world_mats
  mats, parents = build_world_mats(ps2_bin)   # mats[i] = (rot3x3, pos3)
  # transformar vert local del hueso i a world:
  wx = r00*vx + r01*vy + r02*vz + px
"""

import struct

R32 = struct.Struct('<I')
F32 = struct.Struct('<f')


def r32(b, o):
    return R32.unpack_from(b, o)[0]


def f32(b, o):
    return F32.unpack_from(b, o)[0]


def quat_to_mat(x, y, z, w):
    """Quaternion -> matriz de rotacion 3x3."""
    xx, xy, xz, xw = x * x, x * y, x * z, x * w
    yy, yz, yw = y * y, y * z, y * w
    zz, zw = z * z, z * w
    return [
        [1 - 2 * (yy + zz), 2 * (xy - zw), 2 * (xz + yw)],
        [2 * (xy + zw), 1 - 2 * (xx + zz), 2 * (yz - xw)],
        [2 * (xz - yw), 2 * (yz + xw), 1 - 2 * (xx + yy)],
    ]


def mat_mul(A, B):
    return [[sum(A[i][k] * B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def apply_mat(m, p, v):
    """Aplica rot+trans a un vertice (x,y,z)."""
    return (m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2] + p[0],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2] + p[1],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] + p[2])


def build_world_mats(ps2, amo0=0x40):
    """Devuelve (mats, parents) donde mats[i]=(rot3x3, pos3) es la matriz
    world (bind pose) del hueso i, y parents[i] su padre 1-based."""
    cnt = r32(ps2, amo0 + 0x10)
    start = r32(ps2, amo0 + 0x14)
    bone_off = {}
    parents = {}
    for bi in range(cnt):
        e = amo0 + start + bi * 32
        bone_table = r32(ps2, e + 4)
        t3 = r32(ps2, e + 0x10)
        pid = r32(ps2, amo0 + t3) + 1 if t3 else 0
        bo = r32(ps2, amo0 + bone_table + 8) if bone_table else 0
        bone_off[bi] = amo0 + bo if bo else 0
        parents[bi] = pid

    cache = {}

    def get_mat(i):
        if i in cache:
            return cache[i]
        b = bone_off[i]
        if not b:
            m = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
            p = [0, 0, 0]
        else:
            c = [f32(ps2, b + j * 4) for j in range(12)]
            m = quat_to_mat(c[0], c[1], c[2], -c[3])
            p = [c[4], c[5], c[6]]
        pid = parents[i]
        if pid and pid <= cnt:
            pm, pp = get_mat(pid - 1)
            m = mat_mul(pm, m)
            p = [pm[0][0] * p[0] + pm[0][1] * p[1] + pm[0][2] * p[2] + pp[0],
                 pm[1][0] * p[0] + pm[1][1] * p[1] + pm[1][2] * p[2] + pp[1],
                 pm[2][0] * p[0] + pm[2][1] * p[1] + pm[2][2] * p[2] + pp[2]]
        cache[i] = (m, p)
        return m, p

    mats = {}
    for bi in range(cnt):
        mats[bi] = get_mat(bi)
    return mats, parents
