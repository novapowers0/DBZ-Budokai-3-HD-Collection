#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""port_ps2_b3_decimate.py - Decima la geometria PS2 para caber en los buffers
de la plantilla HD con delta=0 (conteos EXACTOS: sec34, vb2, ib de la plantilla).

El HD decima la geometria PS2 a ~50% (Krillin HD = ~50% de la geometria PS2).
Nuestro port sin decimar fuerza un mid-insert interno (crecer sec34) que segun
el historial (AGENTS 27: v6 delta=0 funciono, v4 con crecimiento crasheo) rompe
el parseo del guest.

Metodo: voxel merge POR PART (agrupa vertices del mismo hueso por celda) y
reconstruye el strip como indices consecutivos de la part (strip valido).

Uso:
  python port_ps2_b3_decimate.py <geometry.json> <target_sec> <target_vb2> <target_ib> <salida.json>
"""
import struct
import sys
import json


def be_u16(v): return struct.pack('>H', v & 0xFFFF)


def parse_sec(hexsec):
    b = bytes.fromhex(hexsec)
    out = []
    for i in range(len(b) // 44):
        o = i * 44
        out.append({'x': struct.unpack('>f', b[o+16:o+20])[0],
                    'y': struct.unpack('>f', b[o+20:o+24])[0],
                    'z': struct.unpack('>f', b[o+12:o+16])[0],
                    'u': struct.unpack('>f', b[o+4:o+8])[0],
                    'v': struct.unpack('>f', b[o+8:o+12])[0],
                    'w': struct.unpack('>f', b[o+24:o+28])[0],
                    'bone': struct.unpack('>I', b[o+28:o+32])[0],
                    'nz': struct.unpack('>f', b[o+32:o+36])[0],
                    'ny': struct.unpack('>f', b[o+36:o+40])[0],
                    'nx': struct.unpack('>f', b[o+40:o+44])[0]})
    return out


def pack_sec(v):
    return (struct.pack('>I', 0xFFFFFFFF) + struct.pack('>f', v['u']) +
            struct.pack('>f', v['v']) + struct.pack('>f', v['z']) +
            struct.pack('>f', v['x']) + struct.pack('>f', v['y']) +
            struct.pack('>f', v['w']) + struct.pack('>I', v['bone']) +
            struct.pack('>f', v['nz']) + struct.pack('>f', -v['ny']) +
            struct.pack('>f', v['nx']))


def main():
    if len(sys.argv) < 6:
        print('Uso: port_ps2_b3_decimate.py <geometry.json> <target_sec> <target_vb2> <target_ib> <salida.json>')
        return
    geom = json.load(open(sys.argv[1]))
    t_sec, t_vb2, t_ib = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    sec = parse_sec(geom['sec34'])
    groups = geom['groups']

    # El orden del pool sec34 es: part 0 verts, part 1 verts, ... (contiguo por
    # part). Los grupos A/B indican los rangos. Reconstruir por part.
    # NOTA: los grupos con A[0] >= n_sec apuntan al pool VB2 (vtype B4, cara).
    # Para el test de hipotesis (delta=0) se descartan (el cuerpo basta).
    n_sec_orig = geom['n_sec']
    parts = []
    for g in groups:
        a0, a1 = g['A']
        b0, b1 = g['B']
        if a0 >= n_sec_orig:
            print('  (descarta part vb2: A=(%d,%d) bone=%d tex=%d vtype=0x%02X)' % (a0, a1, g['bone'], g['tex'], g['vtype']))
            continue
        parts.append({'bone': g['bone'], 'tex': g['tex'], 'vtype': g['vtype'],
                      'sec': sec[a0:a0 + a1], 'b0': b0, 'b1': b1})

    # Voxel merge iterativo: aumenta el tamaño de celda hasta caber.
    total = sum(len(p['sec']) for p in parts)
    voxel = 0.05
    rounds = 0
    while total > t_sec and rounds < 20:
        # La celda depende del hueso (mismo espacio local por hueso).
        new_parts = []
        total = 0
        for p in parts:
            kept = []
            cell_map = {}
            for v in p['sec']:
                key = (v['bone'], int(v['x'] / voxel), int(v['y'] / voxel), int(v['z'] / voxel))
                if key in cell_map:
                    continue
                cell_map[key] = True
                kept.append(v)
            new_parts.append({'bone': p['bone'], 'tex': p['tex'], 'vtype': p['vtype'],
                              'sec': kept, 'b0': p['b0'], 'b1': p['b1']})
            total += len(kept)
        parts = new_parts
        voxel *= 1.6
        rounds += 1
    print('decimation: voxel=%.2f rounds=%d sec=%d (target %d)' % (voxel / 1.6, rounds, total, t_sec))

    # Reconstruir pool sec34 + IB + grupos (strip consecutivo por part con
    # RESTARTS degenerados entre parts, como el HD real: [last,last,first,first]
    # rompe el strip sin triángulo conector).
    new_sec = []
    ib = []
    new_groups = []
    pool_of = 0
    prev_last = None
    for p in parts:
        ib_start = len(ib)
        restart_n = 0
        if prev_last is not None:
            # restart: repetir el último índice de la part anterior y el primero
            # de esta 2 veces cada uno -> 2 triángulos degenerados, sin conector.
            ib.append(prev_last)
            ib.append(prev_last)
            ib.append(pool_of)
            ib.append(pool_of)
            restart_n = 4
        for v in p['sec']:
            new_sec.append(v)
        for i in range(len(p['sec'])):
            ib.append(pool_of + i)
        prev_last = pool_of + len(p['sec']) - 1
        n_uni = len(p['sec'])
        new_groups.append({'part': len(new_groups), 'bone': p['bone'], 'tex': p['tex'],
                           'vtype': p['vtype'], 'A': [pool_of, n_uni],
                           'B': [ib_start, len(p['sec']) + restart_n]})
        pool_of += n_uni
    n_sec = pool_of

    # vb2: se mantiene (hex original), pad a target_vb2 si hace falta (lo hace pack).
    vb2_hex = geom['vb2']

    # IB: pad a target_ib con 0xFFFF.
    n_ib = len(ib)
    if n_ib > t_ib:
        print('ERROR: ib %d > target %d' % (n_ib, t_ib))
        return
    ib.extend([0xFFFF] * (t_ib - n_ib))
    ib_bytes = b''.join(be_u16(i) for i in ib)

    sec_bytes = b''.join(pack_sec(v) for v in new_sec)
    print('salida: sec=%d vb2=%d ib=%d groups=%d' % (n_sec, len(vb2_hex) // 88, t_ib, len(new_groups)))
    json.dump({'sec34': sec_bytes.hex(), 'vb2': vb2_hex, 'ib': ib_bytes.hex(),
               'n_sec': n_sec, 'n_vb2': len(vb2_hex) // 88, 'n_ib': t_ib,
               'groups': new_groups}, open(sys.argv[5], 'w'))
    print('guardado:', sys.argv[5])


if __name__ == '__main__':
    main()