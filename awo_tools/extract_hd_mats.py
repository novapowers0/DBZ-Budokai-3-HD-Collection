"""Extraer matrices world (bind pose) de los huesos de un AWG HD B1 (#AWG).

RE (2026-08-14): cada eje de 80 bytes:
  +0x00..+0x0C: quaternion local [x, y, z, w] (big-endian floats)
  +0x10..+0x1C: posicion local [px, py, pz] (big-endian floats)
  +0x30: sello (0x9000020C hueso con mesh / 0x204 shadow / 0x1000020C
         transicion / 0x6000020F raiz / 0x8000020C)
  +0x34: armature ptr (rel AWG) -> arm block [bone_idx, ...]
  +0x38: child ptr (rel AWG)
  +0x3C: sibling ptr (rel AWG)
  +0x40: parent ptr (rel AWG)

Los arm blocks (20B) dan el bone_idx de cada eje (los indices pares =
labels). Las world mats se computan componiendo las transformaciones
locales siguiendo la jerarquia parent.

Uso:
  from extract_hd_mats import build_hd_world_mats
  mats, parents = build_hd_world_mats(awo_bytes, awg_offset)
  # mats[i] = (rot3x3, pos3) matriz world bind del hueso i
"""
import struct
import math

U32 = struct.Struct('>I')
F32 = struct.Struct('>f')


def u32r(b, o):
    return U32.unpack_from(b, o)[0]


def f32r(b, o):
    return F32.unpack_from(b, o)[0]


def quat_to_mat(x, y, z, w):
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
    return (m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2] + p[0],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2] + p[1],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2] + p[2])


def inv_rigid(M, p):
    Rt = [[M[j][i] for j in range(3)] for i in range(3)]
    tp = [-sum(Rt[i][j] * p[j] for j in range(3)) for i in range(3)]
    return Rt, tp


def build_hd_world_mats(awo, awg=0, axes_start=None, n_axes=None):
    """Devuelve (mats, parents) con matrices world bind por bone index HD.
    mats[i] = (rot3x3, pos3). La zona de ejes empieza en axes_start (rel AWG)
    y tiene n_axes bloques de 80B. Si no se pasan, se autodetectan."""
    # autodeteccion: buscar el primer bloque de 80B con quat normalizado,
    # arm ptr valido (arm block con bone_idx 0..51)
    if axes_start is None:
        for rel in range(0x400, 0x3000):
            o = awg + rel
            if o + 0x50 > len(awo):
                break
            q = [f32r(awo, o + i * 4) for i in range(4)]
            n = math.sqrt(sum(v * v for v in q))
            arm = u32r(awo, o + 0x34)
            if not (0.99 < n < 1.01) or not arm:
                continue
            if awg + arm + 20 > len(awo):
                continue
            bone = u32r(awo, awg + arm)
            if 0 <= bone <= 51:
                axes_start = rel
                break
        if axes_start is None:
            return {}, {}
    if n_axes is None:
        n_axes = 0
        for i in range(0, 200):
            rel = axes_start + i * 0x50
            o = awg + rel
            if o + 0x50 > len(awo):
                break
            q = [f32r(awo, o + j * 4) for j in range(4)]
            n = math.sqrt(sum(v * v for v in q))
            arm = u32r(awo, o + 0x34)
            if not (0.99 < n < 1.01) or not arm:
                break
            n_axes += 1

    eje_data = {}
    for i in range(n_axes):
        rel = axes_start + i * 0x50
        o = awg + rel
        q = [f32r(awo, o + j * 4) for j in range(4)]
        p = [f32r(awo, o + 16 + j * 4) for j in range(3)]
        arm = u32r(awo, o + 0x34)
        parent = u32r(awo, o + 0x40)
        bone = -1
        if arm and awg + arm + 20 <= len(awo):
            bone = u32r(awo, awg + arm)
        eje_data[rel] = {'bone': bone, 'parent': parent, 'quat': q, 'pos': p}

    cache = {}

    def world_mat(rel):
        if rel in cache:
            return cache[rel]
        d = eje_data.get(rel)
        if not d:
            cache[rel] = None
            return None
        m = quat_to_mat(d['quat'][0], d['quat'][1], d['quat'][2], d['quat'][3])
        p = list(d['pos'])
        pr = d['parent']
        if pr:
            pw = world_mat(pr)
            if not pw:
                for r2, d2 in eje_data.items():
                    if abs(r2 - pr) <= 0x18:
                        pw = world_mat(r2)
                        if pw:
                            break
            if pw:
                pm, pp = pw
                m = mat_mul(pm, m)
                p = [pm[0][0] * p[0] + pm[0][1] * p[1] + pm[0][2] * p[2] + pp[0],
                     pm[1][0] * p[0] + pm[1][1] * p[1] + pm[1][2] * p[2] + pp[1],
                     pm[2][0] * p[0] + pm[2][1] * p[1] + pm[2][2] * p[2] + pp[2]]
        cache[rel] = (m, p)
        return m, p

    mats = {}
    parents = {}
    for rel, d in eje_data.items():
        bi = d['bone']
        if bi < 0:
            continue
        wm = world_mat(rel)
        if wm:
            mats[bi] = wm
        pd = eje_data.get(d['parent'])
        parents[bi] = pd['bone'] if pd else -1
    return mats, parents
