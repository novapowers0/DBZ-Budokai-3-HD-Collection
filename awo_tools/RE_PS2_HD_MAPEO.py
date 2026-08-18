#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""RE_PS2_HD_MAPEO.py - Descomposicion comparativa AMG (PS2) vs AWG (HD).

Objetivo: mapear campo a campo la estructura de un mesh part PS2 (dentro del
#AMO0) y su equivalente HD (#AWO), para construir el conversor universal
PS2/OBJ -> bin HD autocontenido (como Bulma/Babidi que funcionan en swap).

Uso:
  python RE_PS2_HD_MAPEO.py <janemba.amb> <b327_hd.bin> [b327_ps2.bin]
"""
import struct
import sys

def le32(b, o): return struct.unpack('<I', b[o:o+4])[0]
def le16(b, o): return struct.unpack('<H', b[o:o+2])[0]
def lef(b, o): return struct.unpack('<f', b[o:o+4])[0]
def be32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def bef(b, o): return struct.unpack('>f', b[o:o+4])[0]


def parse_amg_ps2(d, amg_off):
    """Descompone un AMG PS2: header, ejes, mesh parts, submeshes."""
    n_bones = le32(d, amg_off+0x10)
    axes = le32(d, amg_off+0x14)
    n_groups = le32(d, amg_off+0x18)
    labels_off = le32(d, amg_off+0x1C)
    axes_abs = amg_off + axes
    print('  header: n_bones=%d axes@0x%X n_groups=%d labels@0x%X' %
          (n_bones, axes_abs, n_groups, amg_off+labels_off))
    # ejes
    print('  ejes (%d x 80B):' % n_bones)
    for i in range(min(n_bones, 6)):
        e = axes_abs + i*80
        # sello en +0x30, ptr mp en +0x34?
        sello = le32(d, e+0x30)
        p1 = le32(d, e+0x34)
        p2 = le32(d, e+0x38)
        print('    eje %2d: sello=0x%08X p0x34=0x%X p0x38=0x%X' % (i, sello, p1, p2))
    return axes_abs, n_bones


def parse_awg_hd(d, awg_off):
    """Descompone un AWG HD: header, labels, ejes, buffers."""
    n_bones = be32(d, awg_off+0x10)
    axes = be32(d, awg_off+0x14)
    n_groups = be32(d, awg_off+0x18)
    vb2 = be32(d, awg_off+0x2C)
    ib = be32(d, awg_off+0x30)
    sec = be32(d, awg_off+0x34)
    end = be32(d, awg_off+0x38)
    print('  header: n_bones=%d axes=0x%X n_groups=%d' % (n_bones, axes, n_groups))
    print('  buffers: sec34@0x%X vb2@0x%X ib@0x%X end@0x%X' % (sec, vb2, ib, end))
    # labels (16B x bones*2, en +0x40)
    print('  labels (@+0x40):')
    for i in range(min(n_bones*2, 8)):
        lb = d[awg_off+0x40+i*16:awg_off+0x40+i*16+16].split(b'\x00')[0]
        if lb:
            print('    %2d: %s' % (i, lb.decode('latin1')))
    # ejes
    axes_abs = awg_off + axes
    print('  ejes (%d x 80B) @0x%X:' % (n_bones, axes_abs))
    for i in range(min(n_bones, 6)):
        e = axes_abs + i*80
        sello = be32(d, e+0x30)
        p1 = be32(d, e+0x34)
        p2 = be32(d, e+0x38)
        print('    eje %2d: sello=0x%08X p0x34=0x%X p0x38=0x%X' % (i, sello, p1, p2))


def main():
    jan = open(sys.argv[1], 'rb').read()
    hd = open(sys.argv[2], 'rb').read()
    print('========== AMG0 PS2 (Janemba) ==========')
    parse_amg_ps2(jan, 0x84C0)
    print()
    print('========== AWG0 HD (Krillin, plantilla) ==========')
    parse_awg_hd(hd, 0xD80)


if __name__ == '__main__':
    main()
