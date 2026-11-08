# SPDX-License-Identifier: MIT
"""Tanda3 (8/11): espejo DESDE CERO del 0x7D0 (como lo hara src/iw_mips_campo.inl): imagen = ELF + #ITM + punteros; el
init original 0x323840 llena G; s_SPACE 0x31F280 por campo. Se alimenta con las entradas de cada pareja de cap_campo.py
(ancla y estado del jugador, camara, G dinamico) y se compara lo logico con B. Uso: campo_espejo.py <carpeta> [--n 74]"""
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
USA = os.environ.get('USA') == '1'
FN, INIT, SEM = 0x31F280, 0x323840, 0x49BE08
MAPA = {}
if USA:   # ELF USA: funciones por emparejado de instrucciones (T3/mapa_usa.py) y datos con los tramos medidos de MapaUsa()
    sys.path.insert(0, str(Path(__file__).parents[1]))
    import dif_mapa_iw_usa as _M
    MAPA = {int(k, 16): int(v, 16) for k, v in json.load(open(r'E:\DBZ3HD_trabajo\T3\mapa_usa.json')).items()}
    FN, INIT = MAPA[FN], MAPA[INIT]
    SEM = _M.usa_datos(SEM)
def D(a):   # direccion de datos del ELF activo
    return _M.usa_datos(a) if USA else a
K_G, K_MGR, K_OBJS, K_CAM, K_GRID, K_CTXP, K_ITM = 0x1180000, 0x1182000, 0x118C000, 0x118D000, 0x118E000, 0x118F000, 0x1190000
CTX = D(0x5C0DC0)
PTRS = {D(0x49CA70): K_MGR, D(0x49CDE8): K_G, D(0x49CBA4): CTX, D(0x49CBBC): K_CAM, D(0x49CBC0): K_OBJS, D(0x49CDB8): K_GRID, D(0x49CDC0): K_CTXP}
MUDOS = (0x105E70, 0x106070, 0x13F7A0, 0x11DD40, 0x142440, 0x2366D0, 0x296340, 0x296350, 0x299850, 0x2F7950)
PREF = ('Effect_', 'UvAnim_', 'MgEffect_', 'ExecuteScript', 'Hits_', 's_PLY_FADE', 'UI_Set', 'MgPly_SpawnEffect', 'GameEngineDisplay',
        'CharacterAxisPosition1', 'StagesAndCharacters', 'FUN_001352d0', 'FUN_00135320', 'FUN_001353d0', 'Audio_',
        'MiscellaneousMapMesh', 's_PLY_VIB', 'READ_SYSTEM')


def imagen_vacia():
    d = (Path('D:/DBZ3HD/ps2_games/Infinite World (USA)/SLUS_218.42') if USA else B.ELF).read_bytes()
    mem = bytearray(ee.TOTAL)
    phoff, phnum = struct.unpack_from('<I', d, 28)[0], struct.unpack_from('<H', d, 44)[0]
    for i in range(phnum):
        tipo, off, va, _, fsz, _, _, _ = struct.unpack_from('<8I', d, phoff + 32 * i)
        if tipo == 1 and fsz:
            mem[va:va + fsz] = d[off:off + fsz]
    return mem


class Espejo:
    def __init__(s, itm, tipo=0, dif=2):
        s.nom = B.leer_simbolos()
        s.mem = imagen_vacia()
        nom = {MAPA.get(t, t): n for t, n in s.nom.items()} if USA else s.nom
        s.nom = nom
        s.cpu = ee.Ee(s.mem, 0x4A35F0 if USA else B.GP, entradas=nom.keys(), nombres=nom)
        c = s.cpu
        for t in MUDOS:
            c.hooks[MAPA.get(t, t)] = lambda cc: cc.r.__setitem__(2, 0)
        for t, n in nom.items():
            if n.startswith(PREF):
                c.hooks[t] = lambda cc: cc.r.__setitem__(2, 0)
        s.mem[K_ITM:K_ITM + len(itm)] = itm
        for p, v in PTRS.items():
            c.w32(p, v)
        c.w32(CTX + 4, tipo)
        c.w32(K_G + 0x148, K_ITM)
        # rejilla del campo (Node_WrapPositionInGrid): medida en el savestate del 0x7D0
        c.w32(K_GRID + 0x630, 7); c.w32(K_GRID + 0xAD4, 0x453B8000); c.w32(K_GRID + 0xAD8, 0x453B8000)
        c.w32(K_GRID + 0xADC, 10); c.w32(K_GRID + 0xAE0, 10); c.w32(K_GRID + 0xAF0, 0xC4A8C000); c.w32(K_GRID + 0xAF8, 0xC4A8C000)
        c.w32(K_MGR + 0x8B80, 0x3F99999A)
        s.dif = dif

    def init(s, seed=1):
        c = s.cpu
        c.w32(SEM, seed)
        c.call(INIT, limite=3_000_000)

    def campo(s):
        s.cpu.call(FN, limite=3_000_000)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('carpeta')
    ap.add_argument('--n', type=int, default=74)
    ap.add_argument('--ver', type=int, default=0)
    a = ap.parse_args()
    carp = Path(a.carpeta)
    meta = json.loads((carp / 'parejas.json').read_text())
    bl = {k: [tuple(x) for x in v] for k, v in meta['bloques'].items()}
    base = meta['base']
    f = (carp / 'parejas.bin').open('rb')
    tam = bl['B'][-1][2] + bl['B'][-1][1] - base

    def reg(dat, tipo, a0, n):
        for d, nn, o in bl[tipo]:
            if d <= a0 and a0 + n <= d + nn:
                return dat[o - base + a0 - d:o - base + a0 - d + n]
        raise KeyError(hex(a0))
    f.seek(meta['parejas'][0]['off']); d0 = f.read(tam)
    itm = reg(d0, 'A', 0x1563E10, 0x11F0)
    esp = Espejo(itm)
    esp.init()
    c, m = esp.cpu, esp.mem
    G0, MG0 = meta['G'], meta['mgr']
    malas, ok = Counter(), 0
    for k, pr in enumerate(meta['parejas'][:a.n]):
        f.seek(pr['off']); dat = f.read(tam)
        # estado del invitado = G entero (0x1654..0x1700), entradas del jugador/camara y el paso
        m[K_G + 0x1620:K_G + 0x1700] = reg(dat, 'A', G0 + 0x1620, 0xE0)
        m[K_MGR:K_MGR + 0xA000 - 0x1EE90 + 0x1EE90 if False else K_MGR + 0x9000] = reg(dat, 'A', MG0, 0x9000)
        m[K_CAM + 0x80:K_CAM + 0xD0] = reg(dat, 'A', 0x8F3380 + 0x80, 0x50)
        m[K_OBJS + 0x180:K_OBJS + 0x184] = reg(dat, 'A', 0x8F2E80 + 0x180, 4)
        m[K_OBJS + 0x248:K_OBJS + 0x24C] = reg(dat, 'A', 0x8F2E80 + 0x248, 4)
        m[D(0x49C7DC):D(0x49C7DC) + 12] = reg(dat, 'A', 0x49C7DC, 12)
        m[D(0x49BFDC):D(0x49BFDC) + 4] = reg(dat, 'A', 0x49BFDC, 4)
        c.w32(SEM, pr['sem'])
        try:
            esp.campo()
        except RuntimeError as e:
            print(k, 'ERROR', str(e)[:80]); malas['error'] += 1; continue
        dif = []
        for nombre, mine, cap, ofs in (('G', bytes(m[K_G + 0x1620:K_G + 0x1700]), reg(dat, 'B', G0 + 0x1620, 0xE0), 0x1620),
                                       ('mgr', bytes(m[K_MGR:K_MGR + 0x9000]), reg(dat, 'B', MG0, 0x9000), 0)):
            for x in range(0, len(cap), 4):
                if cap[x:x + 4] != mine[x:x + 4]:
                    dif.append((nombre, ofs + x, cap[x:x + 4].hex(), mine[x:x + 4].hex()))
        for dd in dif: malas[(dd[0], dd[1])] += 1
        if not dif: ok += 1
        elif k < a.ver: print(k, dif[:10])
    print('parejas', min(a.n, len(meta['parejas'])), 'iguales', ok)
    for z, n in malas.most_common(20): print(z if isinstance(z, str) else (z[0], hex(z[1])), n)


if __name__ == '__main__':
    main()
