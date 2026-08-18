"""
Analizador detallado de un bloque #AWG (HD 360) para mapear su estructura
interna: ejes, huesos, mesh parts y vértices. Compara contra el #AMG PS2.

Uso:
  python analyze_awg.py <archivo_awo> <indice_awg>

El archivo debe ser un bin HD descomprimido (#AMB BE completo).
"""

import struct
import sys


def read_u32(b, off, endian):
    return struct.unpack(endian + 'I', b[off:off + 4])[0]


def read_u16(b, off, endian):
    return struct.unpack(endian + 'H', b[off:off + 2])[0]


def hexdump(b, off, n, base=0):
    lines = []
    for i in range(0, n, 16):
        chunk = b[off + i:off + i + 16]
        hexs = ' '.join('%02X' % x for x in chunk)
        asc = ''.join(chr(x) if 32 <= x < 127 else '.' for x in chunk)
        lines.append('  0x%04X  %-47s  %s' % (off + i, hexs, asc))
    return '\n'.join(lines)


def find_awgs(seg):
    awgs = []
    idx = 0
    while True:
        i = seg.find(b'#AWG', idx)
        if i < 0:
            break
        awgs.append(i)
        idx = i + 1
    return awgs


def find_vertex_types(seg, endian):
    """Busca headers de mesh part (type1: B5/B4/90)."""
    found = []
    for t in (0x01B5, 0x01B4, 0x0190):
        b = struct.pack(endian + 'I', t)
        idx = 0
        while True:
            i = seg.find(b, idx)
            if i < 0:
                break
            # verificar que +4 sea el type2 (0x21B5 etc) para evitar falsos
            t2 = read_u32(seg, i + 4, endian)
            if (t == 0x01B5 and t2 == 0x21B5) or (t == 0x01B4 and t2 == 0x21B4) or (t == 0x0190 and (t2 == 0x2190 or t2 == 0x0190 or t2 == 0x2190)):
                found.append((i, t))
            idx = i + 1
    return found


def analyze_awg(seg, awg_off, endian, name):
    E = endian
    base = awg_off
    print('=' * 70)
    print(f'{name} @0x{base:X}')
    print('=' * 70)
    print('Header (0x00-0x3F):')
    print(hexdump(seg, base, 0x40))

    magic = seg[base:base + 8]
    bone_am = read_u32(seg, base + 0x10, E)
    axes_loc = read_u32(seg, base + 0x14, E)
    axis_lines = read_u32(seg, base + 0x18, E)
    label_loc = read_u32(seg, base + 0x1C, E)
    f20 = read_u32(seg, base + 0x20, E)
    f24 = read_u32(seg, base + 0x24, E)
    f28 = read_u32(seg, base + 0x28, E)
    f2C = read_u32(seg, base + 0x2C, E)
    f30 = read_u32(seg, base + 0x30, E)
    f34 = read_u32(seg, base + 0x34, E)
    f38 = read_u32(seg, base + 0x38, E)
    f3C = read_u32(seg, base + 0x3C, E)
    print(f'bone_am={bone_am} axes_loc=0x{axes_loc:X} axis_lines={axis_lines} label_loc=0x{label_loc:X}')
    print(f'+20={f20} +24={f24} +28={f28} +2C={f2C} +30={f30} +34={f34} +38={f38} +3C={f3C}')

    # labels
    if 0 < label_loc < 0x10000:
        labels = []
        for i in range(bone_am):
            o = base + label_loc + i * 32
            raw = seg[o:o + 32]
            labels.append(raw.split(b'\x00')[0].decode('ascii', 'replace'))
        print('Labels (%d): %s' % (len(labels), labels[:8]))

    # ejes: en PS2 axis_lines*los huesos... buscar la estructura de ejes de 80B
    # En PS2: axes en axes_loc, cada eje 80B, primero los huesos del AMG
    print()
    print('Ejes (desde 0x%X):' % axes_loc)
    for i in range(min(4, bone_am)):
        o = base + axes_loc + i * 80
        if o + 80 > len(seg):
            break
        axis = seg[o:o + 80]
        # entrada: 48B transform + 4 pad + ptr + child + sibling + parent + pad
        p52 = read_u32(axis, 52, E)
        p56 = read_u32(axis, 56, E)
        p60 = read_u32(axis, 60, E)
        p64 = read_u32(axis, 64, E)
        print('  eje %d @0x%X: +52=0x%X +56=0x%X +60=0x%X +64=0x%X' % (i, o, p52, p56, p60, p64))

    # Mesh parts: buscar type1 B5/B4/90
    print()
    print('Mesh parts (type1/type2):')
    parts = find_vertex_types(seg, E)
    for off, t in parts:
        print('  0x%X type=0x%04X' % (off, t))


def main():
    if len(sys.argv) < 2:
        print('Uso: python analyze_awg.py <bin_amb_hd> [indice_awg]')
        return
    with open(sys.argv[1], 'rb') as f:
        b = f.read()
    idx = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    awgs = find_awgs(b)
    print('AWGs encontrados: %d' % len(awgs))
    if idx >= len(awgs):
        print('Indice fuera de rango')
        return
    # analizar el AWG idx: segmento desde ese AWG hasta el siguiente (o fin del AWO)
    off = awgs[idx]
    end = awgs[idx + 1] if idx + 1 < len(awgs) else None
    seg = b if end is None else b[:end]
    analyze_awg(seg, off, '>', f'#AWG[{idx}]')


if __name__ == '__main__':
    main()
