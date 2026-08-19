#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""swap_cabeza.py - Swap de cabeza Goku -> Vegeta armadura (reconstruccion de bloque).

Toma el bin de Vegeta armadura (base) y reemplaza su bloque de AWG de cara/cabello
(AWG19-25, XVGT_Lxx_S00_FACE) por el bloque correspondiente de Goku (AWG16-22,
XGOK_Lxx_S00_FACE), por correspondencia de label numerico. Mantiene el cuerpo.

ESTRATEGIA: reconstruir TODO el bloque de AWG de cara (19-25) de una sola vez.
Se extrae la geometria de los 7 AWG de cara de Goku, se re-empaquetan como los
AWG de Vegeta (misma estructura, buffer en h+0x1F0, IB que solapa 32B), y se
sustituye el bloque completo de Vegeta, recalculando todos los offsets de una vez.

Layout AWG de cara (AGENTS 13.9), vertice 44B: [0xFFFFFFFF,u,v,x,y,z,1.0,0,nx,ny,nz]
  +0x10 n_bones=1 | +0x2C tamano buffer(n*44) | +0x30 ib_rel | +0x34 tamano IB(bytes)
  +0x38 end_rel | descriptor h+0x180: +0x1C n_verts, +0x24 n_tris
  buffer SIEMPRE en h+0x1F0. IB en h+ib_rel, solapa los ultimos 32B del buffer.

Uso:
  python swap_cabeza.py <goku.bin> <vegeta.bin> <salida.bin>
"""
import struct, sys, re

def u32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def u16(b, o): return struct.unpack('>H', b[o:o+2])[0]
def f32(b, o): return struct.unpack('>f', b[o:o+4])[0]
def pack_u32(v): return struct.pack('>I', v & 0xFFFFFFFF)
def pack_u16(v): return struct.pack('>H', v & 0xFFFF)


def get_awg_labels(d, awo, n_awg, tbl):
    labels = {}
    for i in range(n_awg):
        h = awo + u32(d, awo + tbl + i * 4)
        if d[h:h+4] != b'#AWG':
            continue
        mg = h + u32(d, h + 0x20)
        mgsz = u32(d, h + 0x28)
        zone = bytes(d[mg:mg + mgsz])
        m = re.search(rb'X?[A-Z0-9]{3}_L([0-9A-Z]+)_S[0-9A-Z]+_FACE', zone)
        if m:
            labels[m.group(1).decode()] = i
    return labels


def read_awg_cara(d, awo, tbl, idx):
    """Lee un AWG de cara completo. Devuelve header (bytes), verts, ib, n_verts, n_tris."""
    h = awo + u32(d, awo + tbl + idx * 4)
    header = bytes(d[h:h + 0x1F0])  # header + mesh group (0x1F0)
    desc = h + 0x180
    n_verts = u32(d, desc + 0x1C)
    n_tris = u32(d, desc + 0x24)
    buf = h + 0x1F0
    verts = [bytes(d[buf + i*44:buf + i*44 + 44]) for i in range(n_verts)]
    ib_rel = u32(d, h + 0x30)
    ib_size = u32(d, h + 0x34)
    ib_abs = h + ib_rel
    n_idx = ib_size // 2
    ib = [u16(d, ib_abs + k*2) for k in range(n_idx)]
    return {'header': header, 'n_verts': n_verts, 'n_tris': n_tris,
            'verts': verts, 'ib': ib, 'n_idx': n_idx,
            'ib_rel': ib_rel, 'ib_size': ib_size}


def build_awg_cara(header, verts, ib, n_tris):
    """Re-empaqueta un AWG de cara manteniendo el header BASE (del destino).

    Solo se reemplaza la geometria: buffer + IB + conteos. El header (incluido el
    mesh group con la matriz del hueso y el label) se mantiene del AWG destino.
    Layout: [header 0x1F0][buffer n*44][IB n*2 solapando 32B]
    """
    n_verts = len(verts)
    n_idx = len(ib)
    buf_size = n_verts * 44
    ib_rel = 0x1F0 + buf_size - 32
    end_rel = ib_rel + n_idx * 2
    awg = bytearray(header)  # header completo del destino (0x1F0)
    struct.pack_into('>I', awg, 0x2C, buf_size)
    struct.pack_into('>I', awg, 0x30, ib_rel)
    struct.pack_into('>I', awg, 0x34, n_idx * 2)
    struct.pack_into('>I', awg, 0x38, end_rel)
    struct.pack_into('>I', awg, 0x180 + 0x1C, n_verts)
    struct.pack_into('>I', awg, 0x180 + 0x24, n_tris)
    buf_bytes = b''.join(verts)
    ib_bytes = b''.join(struct.pack('>H', x) for x in ib)
    data = buf_bytes[:buf_size - 32] + ib_bytes
    awg += data
    return bytes(awg)


def main():
    goku_path, veg_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    g = open(goku_path, 'rb').read()
    v = bytearray(open(veg_path, 'rb').read())
    awo = 0x40
    g_tbl = u32(g, awo + 0x1C); g_nawg = u32(g, awo + 0x18)
    v_tbl = u32(v, awo + 0x1C); v_nawg = u32(v, awo + 0x18)
    g_lbl = get_awg_labels(g, awo, g_nawg, g_tbl)
    v_lbl = get_awg_labels(v, awo, v_nawg, v_tbl)
    print('Goku face:', sorted(g_lbl.items(), key=lambda x: x[1]))
    print('Vegeta face:', sorted(v_lbl.items(), key=lambda x: x[1]))

    # mapeo Goku -> Vegeta por label
    pairs = []
    for num, gi in g_lbl.items():
        if gi == 0:
            continue
        vi = v_lbl.get(num)
        if vi is None:
            alt = {'42': '44', '09': '00_S09'}
            if num in alt and alt[num] in v_lbl:
                vi = v_lbl[alt[num]]
        if vi is None and num == '09':
            cand = v_lbl.get('00')
            if cand is not None and cand != 0:
                vi = cand
        if vi is not None and vi != 0:
            pairs.append((gi, vi))
    print('Pares (Goku->Vegeta):', pairs)
    # ordenar por Vegeta AWG para reconstruir el bloque en orden
    pairs.sort(key=lambda p: p[1])
    if not pairs:
        print('Sin pares'); return

    # Leer geometria (verts+ib+conteos) de los AWG de cara de Goku (fuente),
    # y el HEADER (con matriz del hueso + label) de los AWG de Vegeta (destino).
    g_geoms = []
    v_headers = {}
    for gi, vi in pairs:
        src = read_awg_cara(g, awo, g_tbl, gi)
        dst = read_awg_cara(v, awo, v_tbl, vi)
        g_geoms.append((src, dst))  # (geometria Goku, header Vegeta)

    # El bloque de cara de Vegeta va de primer_AWG_h a (fin del ultimo AWG de cara)
    first_vi = pairs[0][1]
    last_vi = pairs[-1][1]
    v_first_h = awo + u32(v, awo + v_tbl + first_vi * 4)
    v_last_h = awo + u32(v, awo + v_tbl + last_vi * 4)
    v_block_start = v_first_h
    v_block_end = v_last_h + u32(v, v_last_h + 0x38)
    print('Bloque de cara Vegeta: 0x%X..0x%X (%d bytes)' % (v_block_start, v_block_end, v_block_end - v_block_start))

    # Construir el bloque nuevo: header de VEGETA + geometria de GOKU
    new_block = b''
    for src, dst in g_geoms:
        new_block += build_awg_cara(dst['header'], src['verts'], src['ib'], src['n_tris'])

    delta = len(new_block) - (v_block_end - v_block_start)
    print('delta = %d' % delta)

    # Reemplazar el bloque
    v[v_block_start:v_block_end] = new_block
    # Actualizar offsets de AWGs posteriores al bloque (despues de last_vi) y AZT/AWO size
    for j in range(last_vi + 1, v_nawg):
        off = awo + v_tbl + j * 4
        cur = u32(v, off)
        if cur != 0:
            struct.pack_into('>I', v, off, cur + delta)
    az = u32(v, 0x30)
    if az != 0:
        struct.pack_into('>I', v, 0x30, az + delta)
    awo_size = u32(v, 0x24)
    struct.pack_into('>I', v, 0x24, awo_size + delta)

    # Actualizar offsets del bloque de cara en la tabla AWO (ahora empiezan en v_block_start)
    # El bloque nuevo tiene N AWGs encadenados. Recalcular cada offset.
    off = v_block_start
    for k, (gi, vi) in enumerate(pairs):
        struct.pack_into('>I', v, awo + v_tbl + vi * 4, off - awo)
        off += len(new_block)  # placeholder; se ajusta abajo
    # Recalcular offsets correctos: cada AWG empieza donde termina el anterior
    pos = v_block_start
    idx = 0
    for gi, vi in pairs:
        struct.pack_into('>I', v, awo + v_tbl + vi * 4, pos - awo)
        # avance: tamano de este AWG = su end_rel
        h = pos
        end_rel = u32(v, h + 0x38)
        pos += end_rel
        idx += 1

    open(out_path, 'wb').write(bytes(v))
    print('Bin generado: %s (%d bytes)' % (out_path, len(v)))


if __name__ == '__main__':
    main()