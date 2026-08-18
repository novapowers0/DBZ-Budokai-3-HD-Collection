"""
Trazador de la jerarquía de huesos dentro de un #AMG (PS2) o #AWG (HD).

Sigue la cadena: eje (80B) -> bloque de hueso -> mesh group header -> mesh parts.
Compara las estructuras PS2 y HD para derivar el mapeo.

Uso:
  python trace_bone.py <bin_ps2> <bin_hd>
"""

import struct
import sys


def u32(b, off, E):
    return struct.unpack(E + 'I', b[off:off + 4])[0]


def dump_region(b, off, n=64, E='<', label=''):
    print(f'  [{label}] @0x{off:X} (rel 0x{off - (0x40+0x5360) if False else 0:X}):')
    for i in range(0, n, 16):
        chunk = b[off + i:off + i + 16]
        vals = [u32(b, off + i + j, E) for j in range(0, 16, 4)]
        print('    +%02X  %s' % (i, '  '.join('%08X' % v for v in vals)))


def trace_ps2(ps2, amg_abs):
    E = '<'
    print('=' * 60)
    print('=== PS2 AMG @0x%X ===' % amg_abs)
    bone_am = u32(ps2, amg_abs + 0x10, E)
    axes_loc = u32(ps2, amg_abs + 0x14, E)
    print('bone_am=%d axes_loc=0x%X' % (bone_am, axes_loc))
    for bi in range(min(3, bone_am)):
        e0 = amg_abs + axes_loc + bi * 80
        p34 = u32(ps2, e0 + 0x34, E)  # ptr a armature (rel AMG)
        arm = amg_abs + p34
        print(f'  bone {bi}: eje@0x{e0:X} +0x34=0x{p34:X} -> arm@0x{arm:X}')
        dump_region(ps2, arm, 32, E, 'armature')
        # siguiendo Decompiler: y=read4 (mesh hdr ptr)
        y = u32(ps2, arm + 4, E)
        if y:
            mg = amg_abs + y
            mp_amnt = u32(ps2, mg, E)
            print(f'    mesh group @0x{mg:X} (rel 0x{y:X}): mp_amnt={mp_amnt}')
            offs = [u32(ps2, mg + 16 + i * 4, E) for i in range(min(mp_amnt, 4))]
            print('    part offsets:', [hex(o) for o in offs])
        break  # solo hueso 0


def trace_hd(hd, awg_abs):
    E = '>'
    print('=' * 60)
    print('=== HD AWG @0x%X ===' % awg_abs)
    bone_am = u32(hd, awg_abs + 0x10, E)
    axes_loc = u32(hd, awg_abs + 0x14, E)
    print('bone_am=%d axes_loc=0x%X (rel AWG) -> abs 0x%X' % (bone_am, axes_loc, awg_abs + axes_loc))
    for bi in range(min(3, bone_am)):
        e0 = awg_abs + axes_loc + bi * 80
        p34 = u32(hd, e0 + 0x34, E)  # ptr armature (rel AWG)
        arm = awg_abs + p34
        print(f'  bone {bi}: eje@0x{e0:X} +0x34=0x{p34:X} -> arm@0x{arm:X}')
        dump_region(hd, arm, 32, E, 'armature')
        y = u32(hd, arm + 4, E)
        if y:
            mg = awg_abs + y
            print(f'    +4 ptr=0x{y:X} -> @0x{mg:X}')
            dump_region(hd, mg, 48, E, 'mesh-group?')
        break  # solo hueso 0


def main():
    ps2 = open(sys.argv[1], 'rb').read()
    hd = open(sys.argv[2], 'rb').read()
    amg_abs = 0x40 + 0x5360
    awg_abs = 0x40 + 0xD40
    trace_ps2(ps2, amg_abs)
    print()
    trace_hd(hd, awg_abs)


if __name__ == '__main__':
    main()
