"""
Analiza mesh parts dentro de un #AWG HD: localiza cada part (type1 en +0x08),
su tamaño (vértices) y estructura. Compara contra el #AMG PS2 del mismo hueso.

Uso:
  python analyze_mesh.py <bin_hd> <awg_offset> [--ps2 <bin_ps2> <amg_offset>]
"""

import struct
import sys


def read_u32(b, off, endian):
    return struct.unpack(endian + 'I', b[off:off + 4])[0]


def find_type_parts(seg, start, end, endian, type1_val=0x01B5):
    """Encuentra mesh parts: patron type1 (u32) seguido de type2."""
    found = []
    pat = struct.pack(endian + 'I', type1_val)
    i = start
    while True:
        i = seg.find(pat, i, end)
        if i < 0:
            break
        found.append(i)
        i += 1
    return found


def analyze_part(hd, off, endian, seg_end, label):
    E = endian
    # header HD: 0x50 bytes
    # +0x08 type1, +0x0C type2, +0x10/+0x14 = 0x44
    t1 = read_u32(hd, off + 0x08, E)
    t2 = read_u32(hd, off + 0x0C, E)
    f10 = read_u32(hd, off + 0x10, E)
    f14 = read_u32(hd, off + 0x14, E)
    print(f'  {label} @0x{off:X}: type1=0x{t1:X} type2=0x{t2:X} +0x10=0x{f10:X} +0x14=0x{f14:X}')


def main():
    if len(sys.argv) < 2:
        print('Uso: python analyze_mesh.py <bin_hd> <awg_off>')
        return
    hd = open(sys.argv[1], 'rb').read()
    awg_off = int(sys.argv[2], 0)

    # el AWG va hasta el siguiente #AWG o el fin del AWO
    nxt = hd.find(b'#AWG', awg_off + 4)
    seg_end = nxt if nxt > 0 else len(hd)
    print(f'AWG @0x{awg_off:X} hasta 0x{seg_end:X} (size=0x{seg_end-awg_off:X})')

    # Mesh parts: buscar B5 en BE dentro del AWG
    offs = find_type_parts(hd, awg_off, seg_end, '>', 0x01B5)
    print(f'Mesh parts B5: {[hex(o) for o in offs]}')
    for i, o in enumerate(offs):
        analyze_part(hd, o - 8, '>', seg_end, f'part[{i}]')

    offs_b4 = find_type_parts(hd, awg_off, seg_end, '>', 0x01B4)
    if offs_b4:
        print(f'Mesh parts B4: {[hex(o) for o in offs_b4]}')
        for i, o in enumerate(offs_b4):
            analyze_part(hd, o - 8, '>', seg_end, f'partB4[{i}]')

    offs_90 = find_type_parts(hd, awg_off, seg_end, '>', 0x0190)
    if offs_90:
        print(f'Mesh parts 90: {[hex(o) for o in offs_90]}')
        for i, o in enumerate(offs_90):
            analyze_part(hd, o - 8, '>', seg_end, f'part90[{i}]')

    # datos tras el último part: buscar el "triángulo" 08 00 00 14 / restart FFFFFFFF
    print()
    # buscar vértices: indices uint16 con primitive restart
    idx = seg_end - 4
    # buscar patron de inicio de malla
    i = awg_off
    restart = hd.find(b'\xFF\xFF\xFF\xFF', awg_off, seg_end)
    print(f'Primer restart FFFFFFFF en 0x{restart:X}' if restart > 0 else 'Sin restart FFFFFFFF')


if __name__ == '__main__':
    main()
