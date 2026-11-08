# SPDX-License-Identifier: MIT
"""Marcas ORIGINALES de la carrera de Cell (IW EU 0x7DE, Char_InitTrailMarkerRandom 0x31D8F0) ejecutadas con ee.py.

N2 (noche 6-7/11). Mismo espejo que src/iw_mips.cpp (Marcas7DE): la funcion sobre la tabla 0x5D9500, Memory y el paso
de campo (0x49C7DC = 1,2 PAL). Nada del juego se copia al repo.
Uso: python tools/iw_mips/cell.py dorado [--cap marcas.jsonl]   (sonda cap_7de.py en PCSX2: cada llamada, entrada -> salida)
     python tools/iw_mips/cell.py juego --log <dbz3.log>          (marcas del juego C++ reejecutadas en ee.py)
"""
import argparse
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import ee  # noqa: E402
from bubbles import ELF, GP, SEMILLA  # noqa: E402

FN, TABLA, PASO = 0x31D8F0, 0x5D9500, 0x49C7DC
CAP = Path('E:/DBZ3HD_trabajo/dorados_noche/7DE/marcas.jsonl')
OFFS = (0, 0x10, 0x18, 0x20, 0x24)


class Espejo:
    def __init__(s, elf=ELF):
        d = elf.read_bytes()
        s.base = bytearray(ee.TOTAL)
        phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
        for i in range(phnum):
            tipo, off, va, _, fsz, _, _, _ = struct.unpack_from('<8I', d, phoff + 32 * i)
            if tipo == 1 and fsz:
                s.base[va:va + fsz] = d[off:off + fsz]
        s.mem = bytearray(s.base)
        s.cpu = ee.Ee(s.mem, GP)

    def marca(s, seed, a0=TABLA, paso=0x3F99999A):
        """Una llamada (la sonda guarda cada una): -> semilla de salida y las 12 palabras de la marca."""
        s.mem[:] = s.base
        c = s.cpu
        c.w32(SEMILLA, seed); c.w32(PASO, paso)
        c.call(FN, a0)
        return c.r32(SEMILLA), [c.r32(a0 + 4 * k) for k in range(12)]

    def cinco(s, seed, paso=0x3F99999A):
        s.mem[:] = s.base
        c = s.cpu
        c.w32(SEMILLA, seed); c.w32(PASO, paso)
        for k in range(5):
            c.call(FN, TABLA + 0x30 * k)
        return c.r32(SEMILLA), [c.r32(TABLA + 0x30 * k + o) for k in range(5) for o in OFFS]


def prueba_dorado(cap=CAP):
    esp, tot, malas = Espejo(), Counter(), []
    for linea in Path(cap).open(encoding='utf-8'):
        r = json.loads(linea)
        so, m = esp.marca(r['seed_in'])
        quiere = [r['seed_out']] + [r['m%d' % k] for k in (0, 4, 5, 6, 7, 8, 9)]
        sale = [so] + [m[k] for k in (0, 4, 5, 6, 7, 8, 9)]
        tot['llamadas'] += 1
        tot['iguales'] += quiere == sale
        tot['sitio_%X' % (r['ra'] - 8)] += 1
        if quiere != sale:
            malas.append((r['i'], [hex(x) for x in quiere], [hex(x) for x in sale]))
    return tot, malas


RX = re.compile(r'dbz3 iwmips cell t=(\d+) seed_in=([0-9A-F]+) seed_out=([0-9A-F]+) paso=([0-9A-F]+) m=(\S+)')


def prueba_juego(log):
    esp, tot, malas = Espejo(), Counter(), []
    for linea in Path(log).read_text(encoding='utf-8', errors='replace').splitlines():
        g = RX.search(linea)
        if not g:
            continue
        t, si, so, paso, m = g.groups()
        s2, m2 = esp.cinco(int(si, 16), int(paso, 16))
        quiere = [int(so, 16)] + [int(x, 16) for x in m.split(',') if x]
        sale = [s2] + m2
        tot['llamadas'] += 1
        tot['iguales'] += quiere == sale
        if quiere != sale:
            malas.append((t, quiere, sale))
    return tot, malas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('que', choices=['dorado', 'juego'])
    ap.add_argument('--cap', default=str(CAP))
    ap.add_argument('--log')
    a = ap.parse_args()
    tot, malas = prueba_dorado(a.cap) if a.que == 'dorado' else prueba_juego(a.log)
    print(json.dumps(tot))
    for m in malas[:8]:
        print('DISTINTA', m)
    sys.exit(0 if tot['llamadas'] and tot['iguales'] == tot['llamadas'] else 1)


if __name__ == '__main__':
    main()
