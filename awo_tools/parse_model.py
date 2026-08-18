"""
Parser del formato #AMO0/#AMG (PS2, little-endian) y #AWO/#AWG/#AZT (HD 360,
big-endian) de DBZ Budokai 3 HD Collection.

Estructura basada en AMO Decompiler.py / AMO Compiler.py de Nexus-sama
(mod center) y la RE propia sobre Krillin (bin 327 GH PS2 vs HD 360).

Uso:
  python parse_model.py <archivo_amb|amo|awo> [--detail]

Output: resumen de la estructura interna (headers, huesos, mesh groups).
"""

import struct
import sys


def read_u32(b, off, endian):
    return struct.unpack(endian + 'I', b[off:off + 4])[0]


def read_u16(b, off, endian):
    return struct.unpack(endian + 'H', b[off:off + 2])[0]


def read_f32(b, off, endian):
    return struct.unpack(endian + 'f', b[off:off + 4])[0]


def find_magic(b, magic, start=0):
    idx = b.find(magic, start)
    return idx if idx >= 0 else None


class Amo0:
    """#AMO0 (PS2) — cabecera del archivo de modelo."""

    def __init__(self, b, base=0):
        self.b = b
        self.base = base
        self.bone_am = read_u32(b, base + 0x10, '<')
        self.bone_loc = read_u32(b, base + 0x14, '<')
        self.amg_am = read_u32(b, base + 0x18, '<')
        self.label_loc = read_u32(b, base + 0x24, '<')
        self.array_am = read_u32(b, base + 0x20, '<')
        # amg_loc: primer AMG. En PS2 se lee en +0x30 (ver Decompiler: f.seek(48))
        self.amg_loc = read_u32(b, base + 0x30, '<')

    def dump(self, indent='  '):
        print(indent + '== #AMO0 (PS2 LE) ==')
        print(indent + f'  bone_am  = {self.bone_am}')
        print(indent + f'  bone_loc = 0x{self.bone_loc:X}')
        print(indent + f'  amg_am   = {self.amg_am}')
        print(indent + f'  array_am = {self.array_am}')
        print(indent + f'  label_loc= 0x{self.label_loc:X}')
        print(indent + f'  amg_loc  = 0x{self.amg_loc:X}')


class Awo:
    """#AWO (HD 360) — cabecera del archivo de modelo."""

    def __init__(self, b, base=0):
        self.b = b
        self.base = base
        self.bone_am = read_u32(b, base + 0x10, '>')
        self.bone_loc = read_u32(b, base + 0x14, '>')
        self.amg_am = read_u32(b, base + 0x18, '>')
        self.amg_table = read_u32(b, base + 0x1C, '>')
        self.array_am = read_u32(b, base + 0x20, '>')
        self.label_loc = read_u32(b, base + 0x24, '>')
        self.unk30 = read_u32(b, base + 0x30, '>')
        self.unk34 = read_u32(b, base + 0x34, '>')

    def amg_offsets(self):
        offs = []
        for i in range(self.amg_am):
            offs.append(read_u32(self.b, self.base + self.amg_table + i * 4, '>'))
        return offs

    def dump(self, indent='  '):
        print(indent + '== #AWO (HD BE) ==')
        print(indent + f'  bone_am    = {self.bone_am}')
        print(indent + f'  bone_loc   = 0x{self.bone_loc:X}')
        print(indent + f'  amg_am     = {self.amg_am}')
        print(indent + f'  amg_table  = 0x{self.amg_table:X}')
        print(indent + f'  array_am   = {self.array_am}')
        print(indent + f'  label_loc  = 0x{self.label_loc:X}')
        print(indent + f'  unk30      = 0x{self.unk30:X}')
        print(indent + f'  unk34      = 0x{self.unk34:X}')
        print(indent + f'  AMG offsets: {[hex(o) for o in self.amg_offsets()]}')


class Amg:
    """#AMG (PS2) o #AWG (HD) — bloque de malla."""

    def __init__(self, b, base, endian, magic):
        self.b = b
        self.base = base
        self.endian = endian
        self.magic = magic
        self.bone_am = read_u32(b, base + 0x10, endian)
        self.axes_loc = read_u32(b, base + 0x14, endian)
        self.axis_lines = read_u32(b, base + 0x18, endian)
        self.label_loc = read_u32(b, base + 0x1C, endian)

    def labels(self):
        labels = []
        for i in range(self.bone_am):
            o = self.base + self.label_loc + i * 32
            raw = self.b[o:o + 32]
            name = raw.split(b'\x00')[0].decode('ascii', 'replace')
            labels.append(name)
        return labels

    def dump(self, indent='    '):
        print(indent + f'== {self.magic.decode()} @0x{self.base:X} ({self.endian}) ==')
        print(indent + f'  bone_am    = {self.bone_am}')
        print(indent + f'  axes_loc   = 0x{self.axes_loc:X}')
        print(indent + f'  axis_lines = {self.axis_lines}')
        print(indent + f'  label_loc  = 0x{self.label_loc:X}')
        lbls = self.labels()
        print(indent + f'  labels ({len(lbls)}): {lbls[:6]}...' if len(lbls) > 6 else indent + f'  labels: {lbls}')


def parse_amb(b, endian):
    """Parsea un contenedor #AMB completo."""
    E = endian
    magic = b[0:4]
    print(f'AMB magic: {magic} | endian: {E}')
    if magic != b'#AMB':
        print('ERROR: no es un #AMB')
        return
    count = read_u32(b, 0x0C, E)
    print(f'Entradas AMB: {count}')
    entries = []
    for i in range(count):
        e = 0x20 + i * 16
        loc, size = struct.unpack(E + 'II', b[e:e + 8])
        entries.append((loc, size))
        print(f'  [{i}] @0x{loc:X} size={size}')
    for idx, (loc, size) in enumerate(entries):
        seg = b[loc:loc + size]
        sub = seg[0:8]
        print(f'  entrada {idx}: magic={sub}')
        if sub.startswith(b'#AMO0'):
            amo = Amo0(seg, 0)
            amo.dump()
            # parsear AMGs
            amg_off = amo.amg_loc
            for i in range(amo.amg_am):
                if amg_off >= len(seg):
                    break
                m = seg[amg_off:amg_off + 8]
                if m.startswith(b'#AMG'):
                    amg = Amg(seg, amg_off, '<', b'#AMG')
                    amg.dump()
                    # estimar fin: siguiente magic #AMG o #AMT o fin
                    nxt = find_magic(seg, b'#AMG', amg_off + 8)
                    amt = find_magic(seg, b'#AMT', amg_off + 8)
                    cands = [x for x in (nxt, amt, len(seg)) if x is not None]
                    nxt = min(cands)
                    # los offsets son del archivo seg, pero los punteros del AMG
                    # son relativos al inicio del AMG (base). Decompiler lee
                    # amg.seek(16) directamente sobre el archivo amg aislado.
                    amg_off = nxt
                else:
                    print(f'  (no #AMG en 0x{amg_off:X}, magic={m})')
                    break
        elif sub.startswith(b'#AWO'):
            awo = Awo(seg, 0)
            awo.dump()
            for off in awo.amg_offsets():
                if off >= len(seg):
                    print(f'  (offset AMG 0x{off:X} fuera de rango)')
                    continue
                m = seg[off:off + 8]
                if m.startswith(b'#AWG'):
                    amg = Amg(seg, off, '>', b'#AWG')
                    amg.dump()
        elif sub.startswith(b'#AMT') or sub.startswith(b'#AZT'):
            print(f'  (textura {sub[:4].decode()}, {size} bytes)')
        else:
            print(f'  (otro contenido: {sub[:4]}...)')


def main():
    if len(sys.argv) < 2:
        print('Uso: python parse_model.py <archivo> [--detail]')
        return
    path = sys.argv[1]
    with open(path, 'rb') as f:
        b = f.read()
    magic = b[0:4]
    if magic == b'#AMB':
        # detectar endianness: entradas en 0x20 (loc+size)
        n_le = read_u32(b, 0x0C, '<')
        n_be = read_u32(b, 0x0C, '>')
        # el conteo real: buscar cual tiene sentido (< 50 y loc en rango)
        endian = '<' if n_le < 500 else '>'
        parse_amb(b, endian)
    else:
        print(f'No es #AMB: {magic}')


if __name__ == '__main__':
    main()
