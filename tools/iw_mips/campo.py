# SPDX-License-Identifier: MIT
"""Tanda3 (8/11): reejecuta en ee.py la funcion por campo (s_SPACE 0x31F280) del 0x7D0 desde las parejas A->B de
cap_campo.py y compara lo que escribe con B. Uso: python tools/iw_mips/campo.py <carpeta de cap_campo> [--base p2s] [--n 40]"""
import argparse
import json
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import bubbles as B  # noqa: E402
import ee  # noqa: E402

import os
MUTE = os.environ.get('MUTE', '1') == '1'
FN, SEM, PASO = 0x31F280, 0x49BE08, 0x49BFDC


class Espejo:
    def __init__(s, p2s, meta):
        s.base, _ = B.cargar(B.ELF, Path(p2s))
        s.nom = B.leer_simbolos()
        s.meta = meta
        s.cpu = ee.Ee(s.base, B.GP, entradas=s.nom.keys(), nombres=s.nom)
        s.cpu.estricto = False
        # mudos: dibujo, efectos y sonido no deciden la logica (lista cerrada por nombre del decompilado)
        for t in (0x105E70, 0x106070, 0x13F7A0, 0x11DD40, 0x142440, 0x2366D0, 0x296340, 0x296350, 0x299850, 0x2F7950):
            s.cpu.hooks[t] = lambda c: c.r.__setitem__(2, 0)
        for t, n in s.nom.items():
            if n.startswith(('Effect_', 'UvAnim_', 'MgEffect_', 'ExecuteScript', 'Hits_', 's_PLY_FADE', 'UI_Set', 'MgPly_SpawnEffect',
                             'GameEngineDisplay', 'CharacterAxisPosition1', 'StagesAndCharacters', 'FUN_001352d0',
                             'FUN_00135320', 'FUN_001353d0', 'Audio_')):
                if MUTE: s.cpu.hooks[t] = lambda c: c.r.__setitem__(2, 0)

    def pareja(s, datos, seed):
        """Carga A, ejecuta la funcion, devuelve {region B: bytes} de la CPU y B capturada."""
        m = s.base
        for d, n, off in s.meta['bloques']['A']:
            m[d:d + n] = datos[off - s.meta['base']:off - s.meta['base'] + n]
        c = s.cpu
        c.w32(SEM, seed)
        c.call(FN, limite=3_000_000)
        return c


def comparar(esp, datos, seed, nombres):
    meta = esp.meta
    c = esp.cpu
    c = esp.pareja(datos, seed)
    dif = []
    for d, n, off in meta['bloques']['B']:
        cap = datos[off - meta['base']:off - meta['base'] + n]
        mine = bytes(c.m[d:d + n])
        if cap != mine:
            for k in range(0, n, 4):
                if cap[k:k + 4] != mine[k:k + 4]:
                    dif.append((d + k, struct.unpack_from('<I', cap, k)[0], struct.unpack_from('<I', mine, k)[0]))
    return dif


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('carpeta')
    ap.add_argument('--base', default='D:/DBZ3HD/_pcsx2_pool/3/sstates/SLES-55347 (335A5A1F).09.p2s')
    ap.add_argument('--n', type=int, default=40)
    ap.add_argument('--ver', type=int, default=0)
    a = ap.parse_args()
    carp = Path(a.carpeta)
    meta = json.loads((carp / 'parejas.json').read_text())
    meta['bloques'] = {k: [tuple(x) for x in v] for k, v in meta['bloques'].items()}
    esp = Espejo(a.base, meta)
    total = meta['bloques']['A'][-1][2] + meta['bloques']['A'][-1][1] - meta['base']
    tamB = meta['bloques']['B'][-1][2] + meta['bloques']['B'][-1][1] - meta['base']
    bloque = tamB
    f = (carp / 'parejas.bin').open('rb')
    malas, tot = Counter(), 0
    for k, pr in enumerate(meta['parejas'][:a.n]):
        f.seek(pr['off']); datos = f.read(bloque)
        try:
            d = comparar(esp, datos, pr['sem'], esp.nom)
        except RuntimeError as e:
            print(k, 'ERROR', str(e)[:90]); continue
        tot += 1
        for x in d: malas[x[0] & ~0xF] += 1
        o = [x for x in d if x[0] not in (SEM,) and not (0x49CB40 <= x[0] < 0x49CB70)]
        if o: print(k, [(hex(x[0]), hex(x[1]), hex(x[2])) for x in o[:8]])
        if d and k < a.ver: print(k, [(hex(x[0]), hex(x[1]), hex(x[2])) for x in d[:12]])
    print('parejas', tot, 'con diferencia en', sum(1 for _ in malas), 'zonas')
    for z, n in malas.most_common(25): print(hex(z), n)


if __name__ == '__main__':
    main()
