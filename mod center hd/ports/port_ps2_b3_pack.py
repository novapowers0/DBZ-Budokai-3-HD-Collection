#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""port_ps2_b3_pack.py - Paso 4 del pipeline port PS2 -> B3 HD.

Clona un bin HD (plantilla, #AMB) y reemplaza en su estructura de dibujo:
  1. La geometria (sec34/vb2/IB) con la generada por port_ps2_b3_geometry.
     Si no cabe en los buffers de la plantilla, aplica un MID-INSERT INTERNO
     (igual que el runtime): los buffers crecen in-place y los offsets del AWG
     header, la tabla de AWGs y el AZT del AMB se desplazan.
  2. Los DESCRIPTORES con los rangos A/B reales de port_ps2_b3_draw (por mesh
     part, no reparto uniforme).
  3. El resto del IB con 0xFFFF (restart marker) para no dibujar residuos.

Se mantienen los ejes y arms de la plantilla (requiere esqueleto 1:1: mismo
nº de huesos y orden). Los mesh-ref blocks se conservan (v1: solo descriptores).

Uso:
  python port_ps2_b3_pack.py <plantilla.bin> <geometry.json> <draw.json> <salida.amb>
"""
import struct
import sys
import json
import re

def be32(b, o): return struct.unpack('>I', b[o:o+4])[0]


def find_real_descriptors(out, AWG0):
    """Localiza los descriptores reales de la plantilla (stride 0x2C00 y
    offset buffer != 0), igual que build_from_template.py."""
    mg_start = AWG0 + be32(out, AWG0 + 0x20)
    zone_end = AWG0 + be32(out, AWG0 + 0x34)
    zone = bytes(out[mg_start:zone_end])
    anchors = [m.start() for m in re.finditer(rb'max \d+ m', zone)]
    descs = []
    for an in anchors:
        dd = mg_start + (an - 0x18)
        try:
            if be32(bytes(out), dd + 0x44) == 0x2C00 and be32(bytes(out), dd + 0x40) != 0:
                descs.append(dd)
        except Exception:
            pass
    return descs


def main():
    if len(sys.argv) < 5:
        print('Uso: port_ps2_b3_pack.py <plantilla.bin> <geometry.json> <draw.json> <salida.amb>')
        return
    plant = open(sys.argv[1], 'rb').read()
    geom = json.load(open(sys.argv[2]))
    draw = json.load(open(sys.argv[3]))

    sec34 = bytes.fromhex(geom['sec34'])
    vb2 = bytes.fromhex(geom['vb2'])
    ib = bytes.fromhex(geom['ib'])
    n_sec, n_vb2, n_ib = geom['n_sec'], geom['n_vb2'], geom['n_ib']

    awo = 0x40
    n_awg = be32(plant, awo + 0x18)
    awg_tbl = awo + be32(plant, awo + 0x1C)
    AWG0 = awo + be32(plant, awg_tbl)
    n_bones = be32(plant, AWG0 + 0x10)
    sec_rel = be32(plant, AWG0 + 0x34)
    vb2_rel = be32(plant, AWG0 + 0x2C)
    ib_rel = be32(plant, AWG0 + 0x30)
    end_rel = be32(plant, AWG0 + 0x38)

    # Capacidad de los buffers de la plantilla (layout HD real)
    buf_sec = (AWG0 + vb2_rel) - (AWG0 + sec_rel) - 2
    buf_vb2 = (AWG0 + ib_rel) - (AWG0 + vb2_rel)
    buf_ib = (AWG0 + end_rel) - (AWG0 + ib_rel)
    cap_sec, cap_vb2, cap_ib = buf_sec // 44, buf_vb2 // 44, buf_ib // 2
    print('Plantilla: n_bones=%d sec_cap=%d vb2_cap=%d ib_cap=%d' % (n_bones, cap_sec, cap_vb2, cap_ib))
    print('Port: sec=%d vb2=%d ib=%d' % (n_sec, n_vb2, n_ib))

    delta_sec = max(0, n_sec * 44 - buf_sec)
    delta_vb2 = max(0, n_vb2 * 44 - buf_vb2)
    delta_ib = max(0, n_ib * 2 - buf_ib)
    total_delta = delta_sec + delta_vb2 + delta_ib
    if total_delta > 0:
        print('mid-insert interno: +%d bytes' % total_delta)

    out = bytearray(plant)
    sec_real_start = AWG0 + sec_rel + 2
    vb2_abs = AWG0 + vb2_rel
    ib_abs = AWG0 + ib_rel
    end_abs = AWG0 + end_rel

    if total_delta > 0:
        ib_old = bytes(out[ib_abs:end_abs])
        prefix = bytes(out[:sec_real_start])
        suffix = bytes(out[end_abs:])
        new_sec = sec34.ljust(buf_sec + delta_sec, b'\x00')
        new_vb2 = vb2.ljust(buf_vb2 + delta_vb2, b'\x00')
        out = bytearray(prefix + new_sec + new_vb2 + ib_old + suffix)
        vb2_abs = sec_real_start + len(new_sec)
        ib_abs = vb2_abs + len(new_vb2)
        end_abs = ib_abs + len(ib_old)
        struct.pack_into('>I', out, AWG0 + 0x2C, vb2_abs - AWG0)
        struct.pack_into('>I', out, AWG0 + 0x30, ib_abs - AWG0)
        struct.pack_into('>I', out, AWG0 + 0x38, end_abs - AWG0)
        for i in range(1, n_awg):
            off = awg_tbl + i * 4
            if be32(out, off) > be32(out, awg_tbl):
                struct.pack_into('>I', out, off, be32(out, off) + total_delta)
        if out[:4] == b'#AMB':
            az_off = be32(out, 0x30)
            if az_off != 0:
                struct.pack_into('>I', out, 0x30, az_off + total_delta)
            awo_size = (az_off + total_delta) - 0x40
            struct.pack_into('>I', out, 0x24, awo_size)
        print('offsets actualizados (delta=%d)' % total_delta)

    # Rellenar geometria
    out[sec_real_start:sec_real_start + n_sec * 44] = sec34
    out[vb2_abs:vb2_abs + n_vb2 * 44] = vb2
    out[ib_abs:ib_abs + n_ib * 2] = ib
    for i in range(n_ib, cap_ib):
        out[ib_abs + i * 2: ib_abs + i * 2 + 2] = b'\xff\xff'

    # Descriptores reales de la plantilla -> reescribir A/B con los del draw
    descs = find_real_descriptors(bytes(out), AWG0)
    n_plant = len(descs)
    n_draw = len(draw['descriptors'])
    print('descriptores plantilla=%d draw=%d' % (n_plant, n_draw))
    if n_draw > n_plant:
        print('ERROR: %d descriptores draw > %d de la plantilla' % (n_draw, n_plant))
        return
    for k, dd in enumerate(descs):
        if k < n_draw:
            a_s, a_c = draw['descriptors'][k]['A']
            b_s, b_c = draw['descriptors'][k]['B']
            struct.pack_into('>I', out, dd + 0x50, a_s << 8)
            struct.pack_into('>I', out, dd + 0x54, a_c << 8)
            struct.pack_into('>I', out, dd + 0x58, b_s << 8)
            struct.pack_into('>I', out, dd + 0x5C, (b_c << 8) | 1)
        else:
            # descriptores de sobra: neutralizar (A fuera de rango, B count 0)
            struct.pack_into('>I', out, dd + 0x50, 0x1158 << 8)
            struct.pack_into('>I', out, dd + 0x54, 44 << 8)
            struct.pack_into('>I', out, dd + 0x58, 4 << 8)
            struct.pack_into('>I', out, dd + 0x5C, 0)

    open(sys.argv[4], 'wb').write(bytes(out))
    print('Bin generado: %s (%d bytes)' % (sys.argv[4], len(out)))


if __name__ == '__main__':
    main()