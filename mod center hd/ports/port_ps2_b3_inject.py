#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""port_ps2_b3_inject.py - Inyeccion: plantilla HD + geometria PS2 convertida.

Via establecida (2026-08-26):
- El port (reconstruir pool/IB/descriptores) rompe la consistencia de los arms
  de la plantilla -> amorfo.
- La inyeccion (plantilla COMPLETA intacta + solo reescribir +12/+16/+20 de los
  slots sec34) SI funciona y es reconocible cuando las posiciones PS2 estan en
  el MISMO espacio que los slots (bone-local).

Metodo:
1. Leer los ejes de la plantilla -> matrices world de cada hueso.
   (El parent en eje+0x40 es un OFFSET relativo al AWG0 que apunta al eje padre,
   NO un indice: parent_idx = (AWG0 + poff - axes_base)//80.)
2. Posiciones PS2 (model-space del geometry.json) como candidatos.
3. Para cada slot sec34 de la plantilla (hueso B, local p): su world =
   world[B]*p. Buscar el vert PS2 (CUALQUIER hueso) mas cercano a ese world.
4. Convertir el vert PS2 a bone-local del hueso B: local = inv(world[B])*vert.
   Escribirlo en el slot. Si la distancia > umbral, conservar la posicion HD
   original (el HD es Cell correcto, evita el estirado peor).

Uso:
  python port_ps2_b3_inject.py <plantilla.bin> <geometry.json> <umbral> <salida.amb>
  python port_ps2_b3_inject.py <plantilla.bin> <extract.json> <umbral> <salida.amb> --npm
    (--npm: nearest-point-on-surface, proyecta cada slot sobre los triangulos PS2)
"""
import struct
import sys
import json
import numpy as np


def be_f(b, o): return struct.unpack('>f', b[o:o+4])[0]
def be32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def f32i(b, o, v): struct.pack_into('>f', b, o, v)


def qm(qx, qy, qz, qw, px, py, pz):
    x, y, z, w = qx, qy, qz, qw
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w), px],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w), py],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y), pz],
                     [0, 0, 0, 1]])


def world_mats(t, awo):
    AWG0 = awo + be32(t, awo + be32(t, awo + 0x1C))
    mg = AWG0 + be32(t, AWG0 + 0x20)
    axes_base = mg + 0x6E0
    nb = be32(t, AWG0 + 0x10)
    axes = []
    for i in range(nb):
        o = axes_base + i*80
        q = (be_f(t, o), be_f(t, o+4), be_f(t, o+8), be_f(t, o+12))
        p = (be_f(t, o+16), be_f(t, o+20), be_f(t, o+24))
        poff = be32(t, o + 0x40)
        pidx = (AWG0 + poff - axes_base)//80 if poff else -1
        axes.append((qm(q[0], q[1], q[2], q[3], p[0], p[1], p[2]), pidx))
    world = [None]*nb
    for i in range(nb):
        m, par = axes[i]
        world[i] = (world[par].dot(m) if (par >= 0 and par < nb and
                    par != i and world[par] is not None) else m.copy())
    return world, AWG0


def build_surface(extract):
    """Devuelve (T, N): triangulos (T,3,3) y sus normales de vertice (T,3,3),
    en model-space."""
    tris = []
    nrm = []
    for p in extract['parts']:
        V = [np.array(v[1:4], dtype=np.float64) for v in p['verts']]
        NV = [np.array(v[4:7], dtype=np.float64) for v in p['verts']]
        for (a, b, c) in p['tris']:
            tris.append([V[a], V[b], V[c]])
            nrm.append([NV[a], NV[b], NV[c]])
    return np.array(tris, dtype=np.float64), np.array(nrm, dtype=np.float64)


def barycentric(p, a, b, c):
    v0, v1, v2 = b-a, c-a, p-a
    d00 = float(v0@v0); d01 = float(v0@v1); d11 = float(v1@v1)
    d20 = float(v2@v0); d21 = float(v2@v1)
    denom = d00*d11 - d01*d01
    if abs(denom) < 1e-12:
        return 1.0, 0.0, 0.0
    v = (d11*d20 - d01*d21)/denom
    w = (d00*d21 - d01*d20)/denom
    u = 1.0 - v - w
    return u, v, w


def closest_point_triangles(p, T):
    """Punto mas cercano a p sobre CADA triangulo (Ericson), vectorizado."""
    a, b, c = T[:, 0], T[:, 1], T[:, 2]
    ab, ac, ap = b-a, c-a, p-a
    d1 = np.einsum('ij,ij->i', ab, ap)
    d2 = np.einsum('ij,ij->i', ac, ap)
    bp, d3 = p-b, np.einsum('ij,ij->i', ab, p-b)
    d4 = np.einsum('ij,ij->i', ac, bp)
    vc = d1*d4 - d3*d2
    cp, d5 = p-c, np.einsum('ij,ij->i', ab, p-c)
    d6 = np.einsum('ij,ij->i', ac, cp)
    vb = d5*d2 - d1*d6
    va = d3*d6 - d5*d4
    # regiones
    N = len(T)
    out = np.empty((N, 3))
    # region 0 (a)
    m0 = (d1 <= 0) & (d2 <= 0)
    out[m0] = a[m0]
    # region 1 (b)
    m1 = (d3 >= 0) & (d4 <= d3)
    out[m1] = b[m1]
    # region 3 (c)
    m3 = (d6 >= 0) & (d5 <= d6)
    out[m3] = c[m3]
    # region 5 (ab)
    m5 = (~m0) & (~m1) & (vc <= 0) & (d1 >= 0) & (d3 <= 0)
    t5 = np.where(m5, d1/(d1-d3), 0.0)
    out[m5] = a[m5] + t5[m5, None]*ab[m5]
    # region 6 (ac)
    m6 = (~m0) & (~m1) & (~m3) & (vb <= 0) & (d2 >= 0) & (d6 <= 0)
    t6 = np.where(m6, d2/(d2-d6), 0.0)
    out[m6] = a[m6] + t6[m6, None]*ac[m6]
    # region 4 (bc)
    m4 = (~m0) & (~m1) & (~m3) & (va <= 0) & ((d4-d3) >= 0) & ((d5-d6) >= 0)
    t4 = np.where(m4, (d4-d3)/((d4-d3)+(d5-d6)), 0.0)
    out[m4] = b[m4] + t4[m4, None]*(c[m4]-b[m4])
    # region 2 (interior)
    m2 = ~(m0 | m1 | m3 | m4 | m5 | m6)
    denom = 1.0/(va+vb+vc)
    v2 = vb[m2]*denom[m2]
    w2 = vc[m2]*denom[m2]
    out[m2] = a[m2] + v2[:, None]*ab[m2] + w2[:, None]*ac[m2]
    return out


def npm_surface_mapping(slot_world, T, N, thr):
    """Para cada slot world, proyeccion a la superficie PS2 (nearest point).
    Devuelve lista de (punto, normal_suavizada, distancia) o None."""
    res = []
    for w in slot_world:
        cp = closest_point_triangles(np.asarray(w, np.float64), T)
        d2 = ((cp - w)**2).sum(axis=1)
        k = int(np.argmin(d2))
        d = float(np.sqrt(d2[k]))
        if d > thr:
            res.append(None)
        else:
            a, b, c = T[k, 0], T[k, 1], T[k, 2]
            u, v, ww = barycentric(cp[k], a, b, c)
            na, nb, nc = N[k, 0], N[k, 1], N[k, 2]
            n = u*na + v*nb + ww*nc
            ln = np.linalg.norm(n)
            n = n/ln if ln > 1e-12 else np.array([0.0, 0.0, 1.0])
            res.append((cp[k], n, d))
    return res


def main():
    npm = '--npm' in sys.argv
    if npm:
        sys.argv.remove('--npm')
    bone_thr = {}
    if '--bone-thr' in sys.argv:
        k = sys.argv.index('--bone-thr')
        for pair in sys.argv[k+1].split(','):
            b, t = pair.split(':')
            bone_thr[int(b)] = float(t)
        del sys.argv[k:k+2]
    soft = None
    if '--soft' in sys.argv:
        k = sys.argv.index('--soft')
        soft = (float(sys.argv[k+1]), float(sys.argv[k+2]))
        del sys.argv[k:k+3]
    if len(sys.argv) < 5:
        print('Uso: port_ps2_b3_inject.py <plantilla.bin> <geometry.json> <umbral> <salida.amb> [--npm] [--soft lo hi] [--bone-thr B:THR,B:THR...]')
        return
    templ = bytearray(open(sys.argv[1], 'rb').read())
    thr = float(sys.argv[3])

    world, AWG0 = world_mats(templ, 0x40)
    inv = [np.linalg.inv(w) for w in world]

    sec_rel = be32(templ, AWG0 + 0x34)
    vb2_rel = be32(templ, AWG0 + 0x2C)
    sec_real = AWG0 + sec_rel + 2
    n_slots = (vb2_rel - sec_rel - 2)//44

    # World de cada slot del template (hueso del slot).
    slot_world = []
    for i in range(n_slots):
        o = sec_real + i*44
        bone = be32(templ, o + 28)
        z, x, y = be_f(templ, o+12), be_f(templ, o+16), be_f(templ, o+20)
        slot_world.append(world[bone].dot(np.array([x, y, z, 1.0]))[:3])

    if npm:
        T, N = build_surface(json.load(open(sys.argv[2])))
        print('NPM: %d triangulos PS2' % len(T))
        # umbral por hueso: la proyeccion NPM se hace con el umbral MAXIMO global,
        # y el descarte por hueso se decide en el bucle de escritura
        mapping = npm_surface_mapping(slot_world, T, N, max([thr] + list(bone_thr.values())))
    else:
        geom = json.load(open(sys.argv[2]))
        b = bytes.fromhex(geom['sec34'])
        pts = []
        for i in range(geom['n_sec']):
            o = i*44
            pts.append(np.array([be_f(b, o+16), be_f(b, o+20), be_f(b, o+12)]))
        mn = np.array([min(p[0] for p in pts), min(p[1] for p in pts), min(p[2] for p in pts)])
        mx = np.array([max(p[0] for p in pts), max(p[1] for p in pts), max(p[2] for p in pts)])
        mid = (mn + mx)/2
        bins = {}
        for idx, p in enumerate(pts):
            key = tuple(0 if p[k] < mid[k] else 1 for k in range(3))
            bins.setdefault(key, []).append(idx)

        def nearest(w):
            key = tuple(0 if w[k] < mid[k] else 1 for k in range(3))
            cand = bins.get(key)
            if not cand:
                cand = list(range(len(pts)))
            best = None
            bd = 1e30
            for idx in cand:
                d = float(((pts[idx]-w)**2).sum())
                if d < bd:
                    bd = d
                    best = idx
            return best, bd**0.5

        mapping = []
        for w in slot_world:
            idx, d = nearest(w)
            mapping.append(pts[idx] if d <= thr else None)

    filled = 0
    kept_hd = 0
    for i in range(n_slots):
        if mapping[i] is None:
            kept_hd += 1
            continue
        o = sec_real + i*44
        bone = be32(templ, o + 28)
        th = bone_thr.get(bone, thr)
        if npm:
            if mapping[i] is None:
                kept_hd += 1
                continue
            pos, nrm, d = mapping[i]
            if d > th:
                kept_hd += 1
                continue
            hd_local = np.array([be_f(templ, o+12), be_f(templ, o+16), be_f(templ, o+20)])
            hd_nrm = np.array([be_f(templ, o+32), -be_f(templ, o+36), be_f(templ, o+40)])
            hd_nrm = hd_nrm/np.linalg.norm(hd_nrm) if np.linalg.norm(hd_nrm) > 1e-12 else hd_nrm
            lc = inv[bone].dot(np.concatenate([pos, [1.0]]))
            f32i(templ, o+12, float(lc[2]))
            f32i(templ, o+16, float(lc[0]))
            f32i(templ, o+20, float(lc[1]))
            # formato normal HD: [nz, -ny, nx]
            f32i(templ, o+32, float(nrm[2]))
            f32i(templ, o+36, float(-nrm[1]))
            f32i(templ, o+40, float(nrm[0]))
        else:
            if mapping[i] is None:
                kept_hd += 1
                continue
            pos = mapping[i]
            lc = inv[bone].dot(np.concatenate([pos, [1.0]]))
            f32i(templ, o+12, float(lc[2]))
            f32i(templ, o+16, float(lc[0]))
            f32i(templ, o+20, float(lc[1]))
        filled += 1
    print('slots=%d inyectados=%d conservados HD=%d (umbral %.2f%s%s%s)' %
          (n_slots, filled, kept_hd, thr, ' NPM' if npm else '',
           (' soft[%.1f,%.1f]' % soft) if soft else '',
           (' bone_thr=%s' % bone_thr) if bone_thr else ''))
    open(sys.argv[4], 'wb').write(bytes(templ))
    print('guardado:', sys.argv[4])


if __name__ == '__main__':
    main()