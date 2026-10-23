"""A3b: cinematica directa de un #AMO de B1 con un clip de un #AMM (la misma regla que ps2_modelo.cpp Posar: la clave SUSTITUYE al reposo)
y comparacion con las matrices posadas de la RAM de PCSX2 (esqueleto en el espacio del modelo, 4x4 por filas).
Uso como modulo: fk(amo, amm, clip, cuadro) -> posiciones (n_huesos, 3) en el espacio del modelo."""
import struct
import numpy as np

def rot(x, y, z):
    cx, sx, cy, sy, cz, sz = np.cos(x), np.sin(x), np.cos(y), np.sin(y), np.cos(z), np.sin(z)
    return np.array([[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
                     [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
                     [-sy, cy * sx, cy * cx]])

def amo_info(amo):
    nb, bones, namg, tbl, nl, nom = struct.unpack_from("<6I", amo, 0x10)
    names = [amo[nom + 32 * j:nom + 32 * j + 32].split(b"\0")[0].decode() for j in range(nb)]
    g = struct.unpack_from("<I", amo, tbl)[0]                  # primer #AMG
    nbg, axes_rel = struct.unpack_from("<2I", amo, g + 0x10)
    local, parent = [], []
    for i in range(nbg):
        o = g + axes_rel + 80 * i
        q = struct.unpack_from("<4f", amo, o)
        t = struct.unpack_from("<3f", amo, o + 16)
        par = struct.unpack_from("<I", amo, o + 0x40)[0]
        x, y, z, w = q
        R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                      [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                      [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
        M = np.eye(4); M[:3, :3] = R; M[:3, 3] = t
        local.append(M)
        parent.append((par - axes_rel) // 80 if par else -1)
    return names, local, parent

def amm_info(amm):
    n, tbl, nb, nom = struct.unpack_from("<4I", amm, 0x10)
    names = [amm[nom + 32 * j:nom + 32 * j + 32].split(b"\0")[0].decode() for j in range(nb)]
    return n, tbl, nb, names

def clip_tracks(amm, clip):
    n, tbl, nb, names = amm_info(amm)
    flags, var, nf, off = struct.unpack_from("<4I", amm, tbl + 16 * clip)
    per = 3 if flags & 0x10 else 2
    ang, pos = {}, {}
    for j in range(nb):
        rp, pp = struct.unpack_from("<2I", amm, off + 4 * per * j)
        if rp:
            k = struct.unpack_from("<I", amm, rp + 8)[0]
            ang[names[j]] = [struct.unpack_from("<4H", amm, rp + 12 + 8 * i) for i in range(min(k, 4096))]
        if pp:
            k = struct.unpack_from("<I", amm, pp + 8)[0]
            pos[names[j]] = [(struct.unpack_from("<I", amm, pp + 12 + 16 * i)[0],) + struct.unpack_from("<3f", amm, pp + 16 + 16 * i)
                             for i in range(min(k, 4096))]
    return nf, ang, pos

def interp_ang(ks, f):
    ia = 0
    while ia + 1 < len(ks) and ks[ia + 1][0] <= f: ia += 1
    ib = ia + 1 if ia + 1 < len(ks) else ia
    u = 0.0 if ks[ib][0] <= ks[ia][0] else min(max((f - ks[ia][0]) / (ks[ib][0] - ks[ia][0]), 0), 1)
    out = []
    for e in (1, 2, 3):
        d = ((ks[ib][e] - ks[ia][e] + 32768) & 0xFFFF) - 32768
        out.append((ks[ia][e] + u * d) * 9.5873799e-5)
    return out

def interp_pos(ks, f):
    ia = 0
    while ia + 1 < len(ks) and ks[ia + 1][0] <= f: ia += 1
    ib = ia + 1 if ia + 1 < len(ks) else ia
    u = 0.0 if ks[ib][0] <= ks[ia][0] else min(max((f - ks[ia][0]) / (ks[ib][0] - ks[ia][0]), 0), 1)
    return [ks[ia][e] + u * (ks[ib][e] - ks[ia][e]) for e in (1, 2, 3)]

def fk(amo, amm, clip, cuadro, info=None, tr=None, pos_mode='replace', rot_mode='replace'):
    names, local, parent = info or amo_info(amo)
    nf, ang, pos = tr or clip_tracks(amm, clip)
    f = cuadro % max(nf, 1)
    W = [None] * len(names)
    for i, nm in enumerate(names):
        L = local[i].copy()
        if nm in ang and ang[nm]:
            Rk = rot(*interp_ang(ang[nm], f))
            L[:3, :3] = Rk if rot_mode == 'replace' else local[i][:3, :3] @ Rk
        if nm in pos and pos[nm]:
            p = np.array(interp_pos(pos[nm], f))
            L[:3, 3] = p if pos_mode == 'replace' else local[i][:3, 3] + p
        W[i] = W[parent[i]] @ L if parent[i] >= 0 else L
    return np.array([w[:3, 3] for w in W]), W



