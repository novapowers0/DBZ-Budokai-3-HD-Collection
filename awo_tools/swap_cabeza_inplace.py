#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""swap_cabeza_inplace.py - Inyeccion de cabeza Goku en Vegeta SIN mover offsets.

Parte del bin de Vegeta armadura (que ya funciona) y copia la geometria de los
AWG de cara de Goku (XGOK_Lxx_S00_FACE) en los buffers EXISTENTES de los AWG de
cara de Vegeta (XVGT_Lxx_S00_FACE), por correspondencia de label. MANTIENE el
tamano de cada AWG de Vegeta (sin mid-insert, sin mover offsets, sin tocar el AWG0).

Si el buffer de Goku es mas pequeno que el de Vegeta, se rellena con 0xFF (markers).
Si es mas grande, se trunca a la capacidad de Vegeta (se pierde detalle pero no crashea).

Uso:
  python swap_cabeza_inplace.py <goku.bin> <vegeta.bin> <salida.bin>
"""
import struct, sys, re

def u32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def u16(b, o): return struct.unpack('>H', b[o:o+2])[0]
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


def get_face_awg(d, awo, tbl, idx):
    """Devuelve offsets y conteos del AWG de cara idx (sin leer contenido)."""
    h = awo + u32(d, awo + tbl + idx * 4)
    buf_size = u32(d, h + 0x2C)
    ib_rel = u32(d, h + 0x30)
    ib_size = u32(d, h + 0x34)
    desc = h + 0x180
    n_verts = u32(d, desc + 0x1C)
    n_tris = u32(d, desc + 0x24)
    buf = h + 0x1F0
    return {'h': h, 'buf': buf, 'buf_size': buf_size, 'n_verts': n_verts,
            'n_tris': n_tris, 'ib_abs': h + ib_rel, 'ib_size': ib_size,
            'cap_verts': buf_size // 44}


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

    for gi, vi in pairs:
        src = get_face_awg(g, awo, g_tbl, gi)
        dst = get_face_awg(v, awo, v_tbl, vi)
        cap = dst['cap_verts']       # capacidad del buffer de Vegeta
        n_copy = min(src['n_verts'], cap)  # cuantos verts de Goku caben
        # Copiar vertices de Goku al buffer de Vegeta
        for i in range(n_copy):
            so = src['buf'] + i * 44
            do = dst['buf'] + i * 44
            v[do:do+44] = g[so:so+44]
        # Rellenar el resto del buffer de Vegeta con 0xFF (markers)
        for i in range(n_copy, cap):
            do = dst['buf'] + i * 44
            v[do:do+44] = b'\xff' * 44
        # Copiar IB de Goku (min(src, cap) indices -> n_tris*3)
        n_src_idx = src['ib_size'] // 2
        n_dst_idx = dst['ib_size'] // 2
        n_copy_idx = min(n_src_idx, n_dst_idx)
        # El IB de Goku usa indices 0..n_verts-1. Si truncamos a cap verts,
        # los indices >= cap quedarian OOB. Limitar a indices < cap.
        copied = 0
        for k in range(0, n_src_idx - 2, 3):
            if copied + 3 > n_dst_idx:
                break
            a, b, c = (u16(g, src['ib_abs'] + k*2),
                       u16(g, src['ib_abs'] + (k+1)*2),
                       u16(g, src['ib_abs'] + (k+2)*2))
            # Saltar triangulos con indices OOB (>= cap)
            if a >= cap or b >= cap or c >= cap:
                continue
            struct.pack_into('>H', v, dst['ib_abs'] + copied*2, a)
            struct.pack_into('>H', v, dst['ib_abs'] + (copied+1)*2, b)
            struct.pack_into('>H', v, dst['ib_abs'] + (copied+2)*2, c)
            copied += 3
        # Rellenar el resto del IB con 0xFFFF (restart markers)
        for k in range(copied, n_dst_idx):
            struct.pack_into('>H', v, dst['ib_abs'] + k*2, 0xFFFF)
        # Actualizar conteos en el descriptor (n_verts, n_tris)
        struct.pack_into('>I', v, dst['h'] + 0x180 + 0x1C, n_copy)
        struct.pack_into('>I', v, dst['h'] + 0x180 + 0x24, copied // 3)
        print('AWG Goku%d -> Veg%d: verts %d->%d (cap %d), IB %d->%d tris' % (
            gi, vi, src['n_verts'], n_copy, cap, src['n_tris'], copied//3))

    open(out_path, 'wb').write(bytes(v))
    print('Bin generado: %s (%d bytes)' % (out_path, len(v)))


if __name__ == '__main__':
    main()