"""
Analizador de la estructura jerárquica completa de un #AWG (HD 360).

Mapea cómo el AWG organiza sus secciones: labels, mesh groups (anidados),
mesh-ref blocks, vertex buffer, index buffer, restart buffer.

El objetivo es derivar el layout exacto para poder reconstruir un AWG válido.

Uso:
  python analyze_awg_full.py <bin_amb_hd> <awg_index>
"""

import struct
import sys


def u32(b, off):
    return struct.unpack('>I', b[off:off + 4])[0]


def f32(b, off):
    return struct.unpack('>f', b[off:off + 4])[0]


def hexdump(b, off, n=64):
    for i in range(0, n, 16):
        chunk = b[off + i:off + i + 16]
        vals = [u32(b, off + i + j) for j in range(0, 16, 4)]
        asc = ''.join(chr(x) if 32 <= x < 127 else '.' for x in chunk)
        print('    +%04X  %-39s  %s' % (i, '  '.join('%08X' % v for v in vals), asc))


def find_awgs(b):
    awgs = []
    idx = 0
    while True:
        i = b.find(b'#AWG', idx)
        if i < 0:
            break
        awgs.append(i)
        idx = i + 1
    return awgs


def main():
    if len(sys.argv) < 2:
        print('Uso: python analyze_awg_full.py <bin_amb_hd> [awg_index]')
        return
    b = open(sys.argv[1], 'rb').read()
    awgs = find_awgs(b)
    idx = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    awg_off = awgs[idx]
    awg_end = awgs[idx + 1] if idx + 1 < len(awgs) else len(b)
    print('AWG[%d] @0x%X a 0x%X (size 0x%X)' % (idx, awg_off, awg_end, awg_end - awg_off))

    # header
    print()
    print('=== Header AWG ===')
    fields = {
        0x04: 'header_size', 0x08: '?', 0x0C: 'version',
        0x10: 'bone_am', 0x14: 'axes_loc', 0x18: 'axis_lines',
        0x1C: 'label_loc', 0x20: 'sec20', 0x24: 'sec24',
        0x28: 'meshgroup', 0x2C: 'vb', 0x30: 'ib', 0x34: 'sec34',
        0x38: 'restart', 0x3C: 'sec3C',
    }
    for off, name in fields.items():
        print('  +0x%02X %-12s = 0x%08X' % (off, name, u32(b, awg_off + off)))

    bone_am = u32(b, awg_off + 0x10)
    lbl_loc = u32(b, awg_off + 0x1C)
    print()
    print('=== Labels (%d) @0x%X ===' % (bone_am, awg_off + lbl_loc))
    labels = []
    for i in range(bone_am):
        o = awg_off + lbl_loc + i * 32
        name = b[o:o + 32].split(b'\x00')[0].decode('ascii', 'replace')
        labels.append(name)
    print(' ', labels[:10], '...')

    # Secciones: cada campo +0x20..+0x3C apunta a una seccion relativa al AWG
    print()
    print('=== Secciones del AWG ===')
    for name, rel in [('sec20', 0x20), ('sec24', 0x24), ('meshgroup', 0x28),
                      ('vb', 0x2C), ('ib', 0x30), ('sec34', 0x34),
                      ('restart', 0x38), ('sec3C', 0x3C)]:
        r = u32(b, awg_off + rel)
        if r == 0:
            continue
        o = awg_off + r
        print()
        print('=== %s (rel 0x%X -> abs 0x%X) ===' % (name, r, o))
        hexdump(b, o, 0x30)


if __name__ == '__main__':
    main()
