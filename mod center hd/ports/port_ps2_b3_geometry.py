#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""port_ps2_b3_geometry.py - Paso 2 del pipeline port PS2 -> B3 HD.

Convierte la geometria PS2 (coords locales + bone via rig) a los buffers HD
(formato A, el validado en juego con sw_goten_nativo / sw_vegeta424 C):
  - sec34 (stride 44, skinned): [0xFFFFFFFF, u, v, z, x, y, peso, BONE, nz,-ny,nx]
  - vb2  (stride 44, estatico): [x, y, z, 0,0,0, 0, 0xFFFFFFFF, nx, ny, nz]

Clasificacion por part (replica la estructura del Krillin HD real):
  - parts tipo cuerpo (B5/B6/F5/BD/FD/3D) -> sec34 (skinned, BONE del rig)
  - parts faciales (B4/A4/32B)           -> vb2  (estatico, BONE=0xFFFFFFFF)

Genera ademas los GRUPOS por part (rango A = vertice en el pool, rango B =
indice del IB) que port_ps2_b3_draw.py convierte en descriptores.

Uso:
  python port_ps2_b3_geometry.py <extract.json> <salida_geometry.json>
"""
import struct
import sys
import json

def be_f32(v): return struct.pack('>f', v)
def be_u32(v): return struct.pack('>I', v & 0xFFFFFFFF)
def be_u16(v): return struct.pack('>H', v & 0xFFFF)

BODY_VTYPES = (0xB5, 0xB6, 0xF5, 0xBD, 0xFD, 0x3D)


def build_buffers(parts, skin):
    """Devuelve (sec34, vb2, ib, n_sec, n_vb2, n_ib, groups).
    groups = [{part, bone, tex, vtype, A:(s,c), B:(s,c)}] en orden de part.
    A = rango de vertices de la part en el pool (contiguo: la part va entera a
    un buffer). B = segmento del IB (strip) de la part."""
    # Pase 1: asignar indices globales. sec34 primero (0..n_sec-1), vb2 despues
    # (n_sec..n_sec+n_vb2-1), replicando el orden de buffers del HD real.
    buf_of = {}   # voff -> 'sec'|'vb2'
    for part in parts:
        t = 'sec' if part['vtype'] in BODY_VTYPES else 'vb2'
        for (oa, *_ ) in part['verts']:
            buf_of[oa] = t
    vmap = {}
    n_sec = 0
    for part in parts:
        for (oa, *_ ) in part['verts']:
            if oa not in vmap and buf_of[oa] == 'sec':
                vmap[oa] = n_sec
                n_sec += 1
    n_vb2 = 0
    for part in parts:
        for (oa, *_ ) in part['verts']:
            if oa not in vmap:
                vmap[oa] = n_sec + n_vb2
                n_vb2 += 1

    # Pase 2: construir buffers e IB en orden de part, y los grupos.
    sec34, vb2, ib = [], [], []
    groups = []
    for part in parts:
        to_sec = part['vtype'] in BODY_VTYPES
        bone_fb = part['bone']
        ib_start = len(ib)
        g_first = None
        g_count = 0
        for (oa, vx, vy, vz, nx, ny, nz, u, v) in part['verts']:
            idx = vmap[oa]
            if g_first is None:
                g_first = idx
            g_count += 1
            ib.append(idx)
        if g_first is None:
            continue
        # construir los bytes del vertice para cada voff unico de esta part
        seen = set()
        for (oa, vx, vy, vz, nx, ny, nz, u, v) in part['verts']:
            if oa in seen:
                continue
            seen.add(oa)
            if to_sec:
                bw = skin.get(oa)
                bone, wraw = (bw[0], bw[1]) if bw else (bone_fb, 0x3F800000)
                w = struct.unpack('<f', struct.pack('<I', wraw))[0] if wraw else 1.0
                sec34.append(be_u32(0xFFFFFFFF) + be_f32(u) + be_f32(v) +
                             be_f32(vz) + be_f32(vx) + be_f32(vy) +
                             be_f32(w) + be_u32(bone) +
                             be_f32(nz) + be_f32(-ny) + be_f32(nx))
            else:
                vb2.append(be_f32(vx) + be_f32(vy) + be_f32(vz) +
                           be_f32(0.0) + be_f32(0.0) + be_f32(0.0) +
                           be_f32(0.0) + be_u32(0xFFFFFFFF) +
                           be_f32(nx) + be_f32(ny) + be_f32(nz))
        # conteo A = nº de vértices unicos de la part (contiguos en su buffer)
        n_unique = len(seen)
        groups.append({'part': len(groups), 'bone': bone_fb, 'tex': part['tex'],
                       'vtype': part['vtype'],
                       'A': [g_first, n_unique],
                       'B': [ib_start, len(part['verts'])]})
    sec34_bytes = b''.join(sec34)
    vb2_bytes = b''.join(vb2)
    ib_bytes = b''.join(be_u16(i) for i in ib)
    return (sec34_bytes, vb2_bytes, ib_bytes, n_sec, n_vb2, len(ib), groups)


def main():
    if len(sys.argv) < 3:
        print('Uso: port_ps2_b3_geometry.py <extract.json> <salida_geometry.json>')
        return
    ex = json.load(open(sys.argv[1]))
    parts = ex['parts']
    skin = {int(k): v for k, v in ex['skin'].items()}
    sec, vb2, ib, n_sec, n_vb2, n_ib, groups = build_buffers(parts, skin)
    print('HD: sec34=%d vb2=%d ib=%d (%d tris) | groups=%d' %
          (n_sec, n_vb2, n_ib, n_ib // 3, len(groups)))
    for g in groups:
        print('  part %2d bone=%2d tex=%3d vtype=0x%02X A=(%d,%d) B=(%d,%d)' %
              (g['part'], g['bone'], g['tex'], g['vtype'], g['A'][0], g['A'][1], g['B'][0], g['B'][1]))
    json.dump({'sec34': sec.hex(), 'vb2': vb2.hex(), 'ib': ib.hex(),
               'n_sec': n_sec, 'n_vb2': n_vb2, 'n_ib': n_ib, 'groups': groups},
              open(sys.argv[2], 'w'))
    print('guardado:', sys.argv[2])


if __name__ == '__main__':
    main()