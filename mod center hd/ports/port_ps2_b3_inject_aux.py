#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""port_ps2_b3_inject_aux.py - Extiende la Via A (inyeccion NPM) a los 16 AWGs
de 1 hueso (huesos globales 48-63) de la plantilla HD.

En Cell F2 (bin 147) esos 16 AWGs son:
  AWG 1-5   -> manos izquierdas  (world[23], surface PS2 bone 23)
  AWG 6-10  -> manos derechas    (world[30], surface PS2 bone 30)
  AWG 11-16 -> cara              (world[32], surface PS2 bone 40)

Cada AWG usa una de 6 familias de layout de vertice (stride 44), detectadas
empiricamente (docs/07_ports/PLAN_PS2_B3/04_FORMATO_RE.md). Solo se reescriben
posicion y normal; NO se tocan weight/pad/marker/uv/IB/descriptores/arms.

Spec: docs/07_ports/PLAN_PS2_B3/04_FORMATO_RE.md §7.

Uso:
  python port_ps2_b3_inject_aux.py <plantilla.bin> <extract.json> <salida.amb>
         [--face-thr 0.8] [--hand-thr 1.0] [--dry]
"""
import struct
import sys
import json
import numpy as np

sys.path.insert(0, __file__.rsplit('\\', 1)[0])
import port_ps2_b3_inject as inj

# --- tabla de layouts (04_FORMATO_RE §2/§3) ---------------------------------
# campo: marker, weight, pad, normal[3] (x,y,z), uv[2] (u,v), pos[3] (x,y,z), r
LAYOUT = {
    1:  dict(marker=32, weight=12, pad=16, nrm=(20, 24, 28), uv=(36, 40), pos=(0, 4, 8),   r=0),
    2:  dict(marker=24, weight=4,  pad=8,  nrm=(12, 16, 20), uv=(28, 32), pos=(36, 40, 0), r=0),
    3:  dict(marker=12, weight=36, pad=40, nrm=(0, 4, 8),    uv=(16, 20), pos=(24, 28, 32), r=0),
    4:  dict(marker=32, weight=12, pad=16, nrm=(20, 24, 28), uv=(36, 40), pos=(0, 4, 8),   r=0),
    5:  dict(marker=0,  weight=24, pad=28, nrm=(32, 36, 40), uv=(4, 8),   pos=(12, 16, 20), r=0),
    6:  dict(marker=32, weight=12, pad=16, nrm=(20, 24, 28), uv=(36, 40), pos=(0, 4, 8),   r=0),
    7:  dict(marker=24, weight=4,  pad=8,  nrm=(12, 16, 20), uv=(28, 32), pos=(36, 40, 0), r=0),
    8:  dict(marker=12, weight=36, pad=40, nrm=(0, 4, 8),    uv=(16, 20), pos=(24, 28, 32), r=0),
    9:  dict(marker=32, weight=12, pad=16, nrm=(20, 24, 28), uv=(36, 40), pos=(0, 4, 8),   r=0),
    10: dict(marker=0,  weight=24, pad=28, nrm=(32, 36, 40), uv=(4, 8),   pos=(12, 16, 20), r=0),
    11: dict(marker=28, weight=8,  pad=12, nrm=(16, 20, 24), uv=(32, 36), pos=(40, 0, 4),   r=0),
    12: dict(marker=20, weight=0,  pad=4,  nrm=(8, 12, 16),  uv=(24, 28), pos=(32, 36, 40), r=0),
    13: dict(marker=32, weight=12, pad=16, nrm=(20, 24, 28), uv=(36, 40), pos=(0, 4, 8),   r=0),
    14: dict(marker=28, weight=8,  pad=12, nrm=(16, 20, 24), uv=(32, 36), pos=(40, 0, 4),   r=2),
    15: dict(marker=28, weight=8,  pad=12, nrm=(16, 20, 24), uv=(32, 36), pos=(40, 0, 4),   r=2),
    16: dict(marker=32, weight=12, pad=16, nrm=(20, 24, 28), uv=(36, 40), pos=(0, 4, 8),   r=0),
}

HOST = {1: 23, 2: 23, 3: 23, 4: 23, 5: 23,
        6: 30, 7: 30, 8: 30, 9: 30, 10: 30,
        11: 32, 12: 32, 13: 32, 14: 32, 15: 32, 16: 32}
REGION = {23: {23}, 30: {30}, 32: {40}}


def awg_offsets(t):
    tbl = 0x40 + inj.be32(t, 0x40 + 0x1C)
    out = []
    i = 0
    while True:
        off = inj.be32(t, tbl + i*4)
        if off == 0 or t[0x40+off:0x40+off+4] != b'#AWG':
            break
        out.append(0x40 + off)
        i += 1
    return out


def build_region_surface(extract, bones):
    T, N = [], []
    for p in extract['parts']:
        if p['bone'] not in bones:
            continue
        V = [np.array(v[1:4], dtype=np.float64) for v in p['verts']]
        NV = [np.array(v[4:7], dtype=np.float64) for v in p['verts']]
        for (a, b, c) in p['tris']:
            T.append([V[a], V[b], V[c]])
            N.append([NV[a], NV[b], NV[c]])
    return np.array(T, dtype=np.float64), np.array(N, dtype=np.float64)


def read_pos(t, o, lay):
    return np.array([inj.be_f(t, o + lay['pos'][0]),
                     inj.be_f(t, o + lay['pos'][1]),
                     inj.be_f(t, o + lay['pos'][2])])


def read_nrm(t, o, lay):
    return np.array([inj.be_f(t, o + lay['nrm'][0]),
                     inj.be_f(t, o + lay['nrm'][1]),
                     inj.be_f(t, o + lay['nrm'][2])])


def nearest_on_surface(p, T, N):
    cp = inj.closest_point_triangles(p, T)
    d2 = ((cp - p)**2).sum(axis=1)
    k = int(np.argmin(d2))
    d = float(np.sqrt(d2[k]))
    a, b, c = T[k, 0], T[k, 1], T[k, 2]
    u, v, w = inj.barycentric(cp[k], a, b, c)
    n = u*N[k, 0] + v*N[k, 1] + w*N[k, 2]
    ln = np.linalg.norm(n)
    n = n/ln if ln > 1e-12 else np.array([0.0, 0.0, 1.0])
    return cp[k], n, d


def main():
    args = [a for a in sys.argv[1:]]
    dry = '--dry' in args
    if dry:
        args.remove('--dry')
    face_thr = 0.8
    hand_thr = 1.0
    if '--face-thr' in args:
        k = args.index('--face-thr')
        face_thr = float(args[k+1]); del args[k:k+2]
    if '--hand-thr' in args:
        k = args.index('--hand-thr')
        hand_thr = float(args[k+1]); del args[k:k+2]
    only = 'all'
    if '--only' in args:
        k = args.index('--only')
        only = args[k+1]; del args[k:k+2]
    if len(args) < 2:
        print(__doc__)
        return
    templ_p, ext_p = args[0], args[1]
    out_p = args[2] if len(args) > 2 else None

    templ = bytearray(open(templ_p, 'rb').read())
    extract = json.load(open(ext_p))
    world, AWG0 = inj.world_mats(templ, 0x40)
    inv = [np.linalg.inv(w) for w in world]
    AWGS = awg_offsets(templ)
    if len(AWGS) < 17:
        print('AVISO: se esperaban >=17 AWGs, hay %d' % len(AWGS))

    surfaces = {}
    total_inj = 0
    total_v = 0
    for i in range(1, 17):
        if i not in LAYOUT:
            continue
        lay = LAYOUT[i]
        host = HOST[i]
        if only == 'face' and host != 32:
            continue
        if only == 'hands' and host == 32:
            continue
        if host not in surfaces:
            surfaces[host] = build_region_surface(extract, REGION[host])
        T, N = surfaces[host]
        awg = AWGS[i]
        sec = awg + inj.be32(templ, awg + 0x34) + lay['r']
        ib = awg + inj.be32(templ, awg + 0x30)
        n = (ib - sec)//44
        W = world[host]
        invW = inv[host]
        Rinv = invW[:3, :3]
        thr = face_thr if host == 32 else hand_thr

        d_before = []
        d_after = []
        injc = 0
        for k in range(n):
            o = sec + k*44
            p_loc = read_pos(templ, o, lay)
            p_mod = W[:3, :3].dot(p_loc) + W[:3, 3]
            cp, nn, d = nearest_on_surface(p_mod, T, N)
            d_before.append(d)
            if d > thr:
                d_after.append(d)
                continue
            lc = invW.dot(np.concatenate([cp, [1.0]]))[:3]
            nloc = Rinv.dot(nn)
            if not dry:
                inj.f32i(templ, o + lay['pos'][0], float(lc[0]))
                inj.f32i(templ, o + lay['pos'][1], float(lc[1]))
                inj.f32i(templ, o + lay['pos'][2], float(lc[2]))
                inj.f32i(templ, o + lay['nrm'][0], float(nloc[0]))
                inj.f32i(templ, o + lay['nrm'][1], float(nloc[1]))
                inj.f32i(templ, o + lay['nrm'][2], float(nloc[2]))
            d_after.append(0.0)
            injc += 1
        total_inj += injc
        total_v += n
        res = {23: 'L-mano', 30: 'R-mano', 32: 'cara'}[host]
        print('AWG%2d %-6s n=%3d inyectados=%3d (thr %.1f)  dist media %.3f -> %.3f  max %.3f' % (
            i, res, n, injc, thr, np.mean(d_before), np.mean(d_after), np.max(d_before)))
    print('TOTAL: %d/%d vertices inyectados' % (total_inj, total_v))
    if out_p and not dry:
        open(out_p, 'wb').write(bytes(templ))
        print('guardado:', out_p)


if __name__ == '__main__':
    main()
