"""Retargeting de bind pose HD: resolver esqueletos con rotaciones diferidas.

Basado en anim_utils (DFKI) align_joint: alinea el eje twist (y) y swing (x)
de un hueso origen al destino. Para esqueletos de juegos distintos con
90-180° de diferencia de pose (B1/B2/B3/IW/Xenoverse).

Para CADA vértice skinned:
  local_dest = inv(bind_dest[bone_dest]) * R_align * bind_src[bone_src] * local_src

donde R_align es la rotación que alinea los ejes del hueso origen al destino.

Uso:
  from retarget_hd import retarget_local
  local = retarget_local(bind_src, bind_dest, local_src, bone_src, bone_dest)
"""
import math


def quat_to_mat(x, y, z, w):
    xx, xy, xz, xw = x * x, x * y, x * z, x * w
    yy, yz, yw = y * y, y * z, y * w
    zz, zw = z * z, z * w
    return [
        [1 - 2 * (yy + zz), 2 * (xy - zw), 2 * (xz + yw)],
        [2 * (xy + zw), 1 - 2 * (xx + zz), 2 * (yz - xw)],
        [2 * (xz - yw), 2 * (yz + xw), 1 - 2 * (xx + yy)],
    ]


def mat_to_quat(M):
    """Matriz 3x3 -> quaternion [x, y, z, w] (como transformations)."""
    tr = M[0][0] + M[1][1] + M[2][2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        w = 0.25 * s
        x = (M[2][1] - M[1][2]) / s
        y = (M[0][2] - M[2][0]) / s
        z = (M[1][0] - M[0][1]) / s
    elif M[0][0] > M[1][1] and M[0][0] > M[2][2]:
        s = math.sqrt(1.0 + M[0][0] - M[1][1] - M[2][2]) * 2
        w = (M[2][1] - M[1][2]) / s
        x = 0.25 * s
        y = (M[0][1] + M[1][0]) / s
        z = (M[0][2] + M[2][0]) / s
    elif M[1][1] > M[2][2]:
        s = math.sqrt(1.0 + M[1][1] - M[0][0] - M[2][2]) * 2
        w = (M[0][2] - M[2][0]) / s
        x = (M[0][1] + M[1][0]) / s
        y = 0.25 * s
        z = (M[1][2] + M[2][1]) / s
    else:
        s = math.sqrt(1.0 + M[2][2] - M[0][0] - M[1][1]) * 2
        w = (M[1][0] - M[0][1]) / s
        x = (M[0][2] + M[2][0]) / s
        y = (M[1][2] + M[2][1]) / s
        z = 0.25 * s
    n = math.sqrt(x * x + y * y + z * z + w * w)
    return [x / n, y / n, z / n, w / n]


def mat_mul(A, B):
    return [[sum(A[i][k] * B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def mat_transpose(M):
    return [[M[j][i] for j in range(3)] for i in range(3)]


def mat_inv(M):
    # inversa de rotacion 3x3 = transpuesta
    return mat_transpose(M)


def apply_mat(m, v):
    return (m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
            m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2])


def norm(v):
    n = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)
    return (v[0] / n, v[1] / n, v[2] / n) if n else v


def quat_from_vec_to_vec(a, b):
    """Quaternion que rota a->b."""
    a = norm(a); b = norm(b)
    dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
    if dot >= 1.0 - 1e-9:
        return [0, 0, 0, 1]
    if dot <= -1.0 + 1e-9:
        # 180 grados: rotar sobre un eje perpendicular
        axis = norm((1, 0, 0)) if abs(a[0]) < 0.9 else norm((0, 1, 0))
        # cruce
        c = (a[1] * axis[2] - a[2] * axis[1], a[2] * axis[0] - a[0] * axis[2], a[0] * axis[1] - a[1] * axis[0])
        c = norm(c)
        return [c[0], c[1], c[2], 0.0]
    cross = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    w = dot + 1.0
    n = math.sqrt(cross[0] ** 2 + cross[1] ** 2 + cross[2] ** 2 + w * w)
    return [cross[0] / n, cross[1] / n, cross[2] / n, w / n]


def quat_mul(q1, q2):
    x1, y1, z1, w1 = q1
    x2, y2, z2, w2 = q2
    return [
        w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
        w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
        w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2,
        w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
    ]


def align_joint(src_up, src_x, tgt_up, tgt_x):
    """Alinea ejes src -> tgt. Devuelve quaternion (rotación de alineación).
    Primero alinea twist (y/up), luego swing (x)."""
    # 1. alinear eje up (y)
    q_twist = quat_from_vec_to_vec(src_up, tgt_up)
    # aplicar a x
    m_twist = quat_to_mat(*q_twist)
    new_x = apply_mat(m_twist, src_x)
    # 2. alinear x
    q_swing = quat_from_vec_to_vec(new_x, tgt_x)
    q = quat_mul(q_swing, q_twist)
    n = math.sqrt(q[0] ** 2 + q[1] ** 2 + q[2] ** 2 + q[3] ** 2)
    return [v / n for v in q]


def align_bone_pair(M_src, M_dst):
    """R_align que alinea el sistema de ejes del hueso src al dst.
    M_src, M_dst: matrices 3x3 de bind pose world de cada hueso."""
    # ejes del src en world: columnas de M_src
    src_y = (M_src[0][1], M_src[1][1], M_src[2][1])  # columna y (up)
    src_x = (M_src[0][0], M_src[1][0], M_src[2][0])
    dst_y = (M_dst[0][1], M_dst[1][1], M_dst[2][1])
    dst_x = (M_dst[0][0], M_dst[1][0], M_dst[2][0])
    return align_joint(src_y, src_x, dst_y, dst_x)


def retarget_local(bind_src, bind_dst, local_src, bone_src, bone_dst):
    """local_dest = inv(bind_dst) * R_align * bind_src * local_src.
    bind_src/bind_dst: dicts bone -> (rot3x3, pos3) world bind.
    local_src: coords locales al hueso origen.
    Devuelve coords locales al hueso destino."""
    M3, p3 = bind_dst.get(bone_dst, (None, None))
    if M3 is None:
        return local_src
    Ms, ps = bind_src.get(bone_src, (None, None))
    if Ms is None:
        # sin origen: solo inv del destino
        pass
    # world del vertice origen
    if Ms is not None:
        wx = Ms[0][0] * local_src[0] + Ms[0][1] * local_src[1] + Ms[0][2] * local_src[2] + ps[0]
        wy = Ms[1][0] * local_src[0] + Ms[1][1] * local_src[1] + Ms[1][2] * local_src[2] + ps[1]
        wz = Ms[2][0] * local_src[0] + Ms[2][1] * local_src[1] + Ms[2][2] * local_src[2] + ps[2]
    else:
        wx, wy, wz = local_src
    # alinear rotaciones si los esqueletos difieren
    if Ms is not None:
        R = mat_to_quat(align_bone_pair(Ms, M3))
        mR = quat_to_mat(*R)
        vx, vy, vz = apply_mat(mR, (wx, wy, wz))
    else:
        vx, vy, vz = wx, wy, wz
    # local del destino
    iM = mat_inv(M3)
    lx = iM[0][0] * vx + iM[0][1] * vy + iM[0][2] * vz
    ly = iM[1][0] * vx + iM[1][1] * vy + iM[1][2] * vz
    lz = iM[2][0] * vx + iM[2][1] * vy + iM[2][2] * vz
    return (lx, ly, lz)
