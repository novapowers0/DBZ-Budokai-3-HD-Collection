"""Analizar estructura AWO HD 360 (B1 o B3) para decimar geometria.

Offsets reales del AWG (verificados empiricamente en Krillin/Goten B3 360):
  +0x00 '#AWG'  +0x04 0x40  +0x0C flag(4)  +0x10 numberOfBones
  +0x14 rigging_data_ptr  +0x18 unk(13)  +0x1C 0x40(ptrBones)
  +0x20 meshgrp_ptr  +0x24 unk(0xB)  +0x28 0x2700
  +0x2C vb2_offset  +0x30 ib_offset  +0x34 sec34_offset  +0x38 end/restart
  +0x3C unk(36)  +0x40 labels de bones (32B c/u)
  Buffers en memoria: sec34 (skinned, stride 44, align+2) -> vb2 (static,
  stride 44) -> ib (u16 indices).

Uso:
  python analyze_awo_b1.py <awo_bin> [--dump]
"""
import struct
import sys

U32 = struct.Struct('>I')


def u32(b, o):
    return U32.unpack_from(b, o)[0]


def main():
    if len(sys.argv) < 2:
        print('Uso: analyze_awo_b1.py <awo_bin> [--dump]')
        return
    path = sys.argv[1]
    dump = '--dump' in sys.argv
    d = open(path, 'rb').read()
    if d[0:4] != b'#AWO':
        print('ERROR: no es #AWO (magic=%s).' % d[0:4])
        return
    AWO = 0
    n_bones = u32(d, AWO + 0x10)
    n_awgs = u32(d, AWO + 0x18)
    awg_tbl = u32(d, AWO + 0x1C)
    lbl = u32(d, AWO + 0x24)
    print('#AWO %d bytes | %d bones | %d AWGs | tabla@0x%X | labels@0x%X' % (
        len(d), n_bones, n_awgs, awg_tbl, lbl))
    total_sec = total_vb2 = total_ib = 0
    for i in range(n_awgs):
        awg_off = u32(d, AWO + awg_tbl + i * 4)
        awg = AWO + awg_off
        if awg + 0x40 > len(d) or d[awg:awg + 4] != b'#AWG':
            print('  AWG %d @0x%X: magic invalido %s' % (i, awg_off, d[awg:awg + 4]))
            continue
        n_b = u32(d, awg + 0x10)
        flag = u32(d, awg + 0x0C)
        vb2 = u32(d, awg + 0x2C)
        ib = u32(d, awg + 0x30)
        sec34 = u32(d, awg + 0x34)
        end = u32(d, awg + 0x38)
        n_sec = (vb2 - sec34 - 2) // 44 if vb2 > sec34 else 0
        n_vb2 = (ib - vb2) // 44 if ib > vb2 else 0
        n_ib = (end - ib) // 2 if end > ib else 0
        total_sec += n_sec
        total_vb2 += n_vb2
        total_ib += n_ib
        line = ('  AWG %d @0x%X flag=%d bones=%d sec34=0x%X(%d v) vb2=0x%X(%d v) '
                'ib=0x%X(%d i) end=0x%X meshgrp=0x%X' % (
                    i, awg_off, flag, n_b, sec34, n_sec, vb2, n_vb2,
                    ib, n_ib, end, u32(d, awg + 0x20)))
        print(line)
        if dump and i == 0:
            print('    sec34+2[0:44]: %s' % d[awg + sec34 + 2:awg + sec34 + 46].hex())
            print('    vb2[0:44]: %s' % d[awg + vb2:awg + vb2 + 44].hex())
            print('    ib[0:20]: %s' % d[awg + ib:awg + ib + 20].hex())
    print('TOTAL: sec34=%d vb2=%d ib=%d' % (total_sec, total_vb2, total_ib))
    print('Estimacion bytes crudos: %d' % (total_sec * 44 + total_vb2 * 44 + total_ib * 2))


if __name__ == '__main__':
    main()