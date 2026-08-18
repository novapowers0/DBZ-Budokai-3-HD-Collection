#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""scan_bones.py - Escanear bins HD del AFS y contar los bones de cada personaje.

Para encontrar plantillas de referencia: personajes con N bones (p.ej. 48,
como Janemba) que sirvan como plantilla estructural para el conversor
PS2->B3 HD (en vez de usar Babidi, que tiene 41).

Uso:
  python scan_bones.py <data_cmn.afs> <bins_a_escanear.txt|numeros>
"""
import struct
import sys
import subprocess
import os
import tempfile


def read_afs_index(afs_path):
    with open(afs_path, 'rb') as f:
        head = f.read(0x10)
    count = struct.unpack('<I', head[4:8])[0]
    entries = []
    with open(afs_path, 'rb') as f:
        f.seek(8)
        for _ in range(count):
            raw = f.read(8)
            a, s = struct.unpack('<II', raw)
            entries.append((a, s))
    return entries


def count_bones_hd(data):
    """Cuenta los bones del primer AWG de un bin HD descomprimido."""
    if data[:4] == b'#AMB':
        awo = 0x40
    else:
        awo = data.find(b'#AWO')
    if awo < 0:
        return None
    try:
        n_bones = struct.unpack('>I', data[awo+0x10:awo+0x14])[0]
        n_awg = struct.unpack('>I', data[awo+0x18:awo+0x1C])[0]
        tbl = struct.unpack('>I', data[awo+0x1C:awo+0x20])[0]
        awg0 = awo + struct.unpack('>I', data[awo+tbl:awo+tbl+4])[0]
        # labels del AWG0 (primer label)
        label = data[awg0+0x40:awg0+0x40+16].split(b'\x00')[0]
        return n_bones, n_awg, label
    except Exception:
        return None


def main():
    afs = sys.argv[1]
    bins = [int(x) for x in sys.argv[2:]] if len(sys.argv) > 2 else []
    entries = read_afs_index(afs)
    xbd = os.path.join(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'), 'xbdecompress.exe')
    tmp = tempfile.mkdtemp()
    results = []
    for bn in bins:
        if bn >= len(entries):
            continue
        addr, size = entries[bn]
        with open(afs, 'rb') as f:
            f.seek(addr)
            data = f.read(size)
        if data[:4] == b'\x0f\xf5\x12\xee':
            lzx = os.path.join(tmp, 'b%d.lzx' % bn)
            out = os.path.join(tmp, 'b%d.bin' % bn)
            open(lzx, 'wb').write(data)
            subprocess.run([xbd, lzx, out], capture_output=True)
            data = open(out, 'rb').read()
        nb = count_bones_hd(data)
        results.append((bn, nb))
    for bn, nb in results:
        if nb:
            print('bin %4d: bones=%d awgs=%d label=%s' % (bn, nb[0], nb[1], nb[2].decode('latin1', 'replace')))
        else:
            print('bin %4d: NO HD' % bn)


if __name__ == '__main__':
    main()
