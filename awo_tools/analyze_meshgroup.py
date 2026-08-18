#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""analyze_meshgroup.py - Descomponer el mesh group de un AWG HD (estructura de dibujo).

Mapa la zona del mesh group (mg_off..mg_off+mg_size) de un bin HD:
  - Mesh-ref blocks (mesh parts)
  - Ejes (huesos con quat+pos+arm_ptr)
  - Arms (rangos del IB por bone)
  - Descriptores (rangos A=sec34, B=IB)

Uso:
  python analyze_meshgroup.py <bin_hd> [awg_index]
"""
import struct
import sys
import re

def be32(b, o): return struct.unpack('>I', b[o:o+4])[0]
def bef(b, o): return struct.unpack('>f', b[o:o+4])[0]


def analyze(b, awo, awg_i):
    def awg_off():
        tbl = be32(b, awo+0x1C)
        return awo + be32(b, awo + tbl + awg_i*4)
    AWG0 = awg_off()
    n_bones = be32(b, AWG0+0x10)
    axes = be32(b, AWG0+0x14)
    groups = be32(b, AWG0+0x18)
    mg_off = be32(b, AWG0+0x20)
    mg_size = be32(b, AWG0+0x28)
    vb2 = be32(b, AWG0+0x2C)
    ib = be32(b, AWG0+0x30)
    sec = be32(b, AWG0+0x34)
    end = be32(b, AWG0+0x38)
    print('AWG%d @0x%X: n_bones=%d axes=0x%X groups=%d mg_off=0x%X mg_size=0x%X' %
          (awg_i, AWG0, n_bones, axes, groups, mg_off, mg_size))
    print('  sec34=0x%X (%d) vb2=0x%X (%d) ib=0x%X (%d) end=0x%X' %
          (sec, (ib-sec-2)//44, vb2, (end-ib)//2, ib, 0, end))
    mg = AWG0 + mg_off
    # 1. Ejes (en axes, rel AWG)
    axes_abs = AWG0 + axes
    print('\n=== Ejes (%d x 80B) ===' % n_bones)
    for i in range(n_bones):
        e = axes_abs + i*80
        sello = be32(b, e+0x30)
        arm = be32(b, e+0x34)
        p38 = be32(b, e+0x38)
        print('  eje %2d @0x%X: sello=0x%08X arm=0x%X p38=0x%X' % (i, e, sello, arm, p38))
    # 2. Mesh group: buscar mesh part headers (B5 01 / B4 01)
    print('\n=== Mesh group (0x%X, size 0x%X) ===' % (mg, mg_size))
    print('  header:', b[mg:mg+0x40].hex())
    # mesh-ref blocks de 0x50: buscar el patron de mesh part B5/B4
    mesh_parts = []
    for off in range(0, mg_size-4):
        v = be32(b, mg+off)
        if v in (0x000001B5, 0x000001B4) or v == 0x1B5:
            # posible mesh part header
            mesh_parts.append(off)
    print('  posibles mesh part headers en offsets:', [hex(x) for x in mesh_parts[:20]])


def main():
    b = open(sys.argv[1], 'rb').read()
    awo = 0x40 if b[:4] == b'#AMB' else b.find(b'#AWO')
    awg_i = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    analyze(b, awo, awg_i)


if __name__ == '__main__':
    main()
