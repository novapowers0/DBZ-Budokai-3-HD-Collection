# SPDX-License-Identifier: MIT
"""MSN019 (0x7E1, Videl): reparto ORIGINAL de cada fila (Memory(32) % N = objeto oculto y Results_RespawnItem 0x2EB000
por objeto) ejecutado desde el ELF con ee.py. Mismo espejo que src/iw_mips.cpp (msn019::Fila).
Uso: python tools/iw_mips/msn019.py dorado --cap <filas.jsonl>   (captura de PCSX2: tools/iw_mips/cap_7e1.py)
     python tools/iw_mips/msn019.py juego --log <dbz3.log>
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
from bubbles import ELF, GP, SEMILLA, paso  # noqa: E402

MEMORY, RESPAWN = 0x127CA0, 0x2EB000
UIA, CTX, NOBJ = 0x1040000, 0x1048000, 0x30   # objetos UI propios (400 B cada uno), contexto *(0x49CD68)
P_UIA, P_NOBJ, P_CTX, PASOS, ANCHOS, ANCHO, Y = 0x49CD8C, 0x49CD90, 0x49CD68, 0x5D9162, 0x5D9164, 0x49C84C, 0x5D90D8
# Medido en PCSX2 (cap_7e1, dificultad 2): pantalla 600 x 448 (0x49C84C), pasos 4, y 395, flags +0x150 = 7.
# La caja (+0x30..+0x3C) no se ha leido: con los huecos de todas las dificultades dos objetos nunca quedan a menos de 40 px.
CAJA = (-16.0, -16.0, 16.0, 16.0)


def f2b(x): return struct.unpack('<I', struct.pack('<f', x))[0]
def b2f(b): return struct.unpack('<f', struct.pack('<I', b & ee.M32))[0]


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

    def fila(s, e):
        """e: seed, n, mascara, dif, pasos, ancho (bits), y, caja, excluir, primera, prev [(x bits, y bits, estado, tipo, hueco)]."""
        s.mem[:] = s.base
        c, w32 = s.cpu, s.cpu.w32
        w32(SEMILLA, e['seed']); w32(P_UIA, UIA); w32(P_NOBJ, NOBJ); w32(P_CTX, CTX)
        c.w16(CTX + 0x66, e['n']); c.w16(CTX + 0x64, e['mascara']); w32(CTX + 0x114, e['dif'])
        c.w16(PASOS, e['pasos']); w32(ANCHOS + 4 * e['dif'], e['ancho']); w32(ANCHO, f2b(600.0)); c.w16(Y, e['y'])
        for i in range(e['n']):
            b = UIA + (0x11 + i) * 400
            x, y, est, tipo, hueco = e['prev'][i]
            w32(b + 0x150, 7); w32(b + 0x20, x); w32(b + 0x24, y); w32(b + 0x28, f2b(10.0)); w32(b + 0x2C, 0x3F800000)
            for k, v in enumerate(e['caja']):
                w32(b + 0x30 + 4 * k, v)
            w32(b + 0x44, est); w32(b + 0x48, tipo); w32(b + 0x4C, hueco)
        c.call(MEMORY, 0x20)
        oculto = (c.r[2] & ee.M32) % e['n'] + 0x11 if e['excluir'] else ee.M32
        for i in range(e['n']):
            k = 0x11 + i
            c.call(RESPAWN, k, i if e['primera'] else ee.M32, oculto)
        sale = []
        for i in range(e['n']):
            b = UIA + (0x11 + i) * 400
            sale.append((c.r32(b + 0x20), c.r32(b + 0x24), c.r32(b + 0x44), c.r32(b + 0x48), c.r32(b + 0x4C)))
        return dict(seed=c.r32(SEMILLA), items=sale, oculto=oculto)


CAP_E = 'E:/DBZ3HD_trabajo/dorados_noche/7E1/filas.jsonl'                  # N2: tools/iw_mips/cap_7e1.py
CAP_D = 'E:/DBZ3HD_trabajo/iw_mips/dia1E3/cap7e1_1793985608265802800/filas.jsonl'          # dia1E3 (borrado)


def prueba_dorado(cap):
    filas = [json.loads(x) for x in Path(cap).open(encoding='utf-8')]
    cfg = filas[0]['cfg']
    evs = [f for f in filas[1:] if 'items' in f]
    esp, tot, malas = Espejo(), Counter(), []
    caja = [r[0:4] for r in cfg['rect']]
    assert all(c == caja[0] for c in caja), 'cajas distintas por objeto'
    prev, ajustes = None, []
    tot['ajustes'] = ajustes
    for ev in evs:
        items = ev['items']   # x, y, estado, tipo, hueco, flags
        nuevo = [(it[0], it[1], it[2], it[3], it[4]) for it in items]
        # una fila nueva = todos los objetos con estado 0/3 y posiciones nuevas (los demas eventos son aciertos/ocultar)
        if prev is None or not all(it[2] in (0, 3) for it in nuevo) or [x[0] for x in nuevo] == [x[0] for x in prev]:
            prev = nuevo if prev is None else prev if not all(it[2] in (0, 3) for it in nuevo) else nuevo
            continue
        tot['filas'] += 1
        primera = all(p[4] == 0 for p in prev)
        excluir = any(it[2] == 3 for it in nuevo)
        hallada = None
        # sin ajuste: la semilla LEIDA en una de las 5 ultimas lecturas antes del cambio, sin pasos extra del LFSR
        for t, cnt, s0 in list(reversed(ev['semillas']))[:5]:
            st = s0
            for k in range(1):
                e = dict(seed=st, n=cfg['N'], mascara=cfg['mask'], dif=cfg['dif'], pasos=cfg['pasos'],
                         ancho=cfg['ancho_tabla'][cfg['dif']], y=cfg['y'], caja=caja[0], excluir=excluir,
                         primera=primera, prev=prev)
                # filtro barato: el oculto sale de Memory(32) % N
                if excluir:
                    s32 = st
                    for _ in range(32):
                        s32 = paso(s32)
                    if ((s32 - 1) & ee.M32) % cfg['N'] != [i for i, it in enumerate(nuevo) if it[2] == 3][0]:
                        st = paso(st)
                        continue
                o = esp.fila(e)
                if [(x[0], x[2], x[3]) for x in o['items']] == [(x[0], x[2], x[3]) for x in nuevo]:
                    hallada = (s0, k)
                    break
                st = paso(st)
            if hallada:
                break
        tot['iguales'] += hallada is not None
        if hallada:   # muestra = cuantas lecturas antes del cambio; pasos = pasos del LFSR desde esa lectura
            idx = max(i for i, x in enumerate(ev['semillas']) if x[2] == hallada[0])
            ajustes.append((ev['fila'], len(ev['semillas']) - 1 - idx, hallada[1], int(excluir)))
        if not hallada:
            malas.append((ev['t'], ev['fila'], nuevo))
        prev = nuevo
    return tot, malas


RX = re.compile(r'dbz3 iwmips msn019 fila=(\d+) seed_in=([0-9A-F]+) seed_out=([0-9A-F]+) cfg=(\S+) prev=(\S+) out=(\S+)')


def prueba_juego(log):
    esp, tot, malas = Espejo(), Counter(), []
    H = lambda t: [int(x, 16) for x in t.split(',') if x]
    for linea in Path(log).read_text(encoding='utf-8', errors='replace').splitlines():
        m = RX.search(linea)
        if not m:
            continue
        fila, si, so, cfg, prev, out = m.groups()
        c = H(cfg)
        n = c[0]
        p = H(prev)
        e = dict(seed=int(si, 16), n=n, mascara=c[1], dif=c[2], pasos=c[3], ancho=c[4], y=c[5], caja=c[6:10],
                 excluir=c[10], primera=c[11], prev=[tuple(p[5 * i:5 * i + 5]) for i in range(n)])
        o = esp.fila(e)
        sale = [o['seed']] + [v for it in o['items'] for v in it]
        tot['filas'] += 1
        tot['iguales'] += sale == [int(so, 16)] + H(out)
        tot['ocultas'] += o['oculto'] != ee.M32
        if sale != [int(so, 16)] + H(out):
            malas.append(fila)
    return tot, malas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('que', choices=['dorado', 'juego'])
    ap.add_argument('--cap')
    ap.add_argument('--log')
    a = ap.parse_args()
    if a.que == 'dorado' and not a.cap:   # N2: dorado de E: (cap_7e1.py rehecho); el de D:/tmp/dia1E3 se borro el 7/11
        a.cap = next((c for c in (CAP_E, CAP_D) if Path(c).exists()), CAP_E)
        print('captura:', a.cap)
    tot, malas = prueba_dorado(a.cap) if a.que == 'dorado' else prueba_juego(a.log)
    print(json.dumps(tot))
    for m in malas[:8]:
        print('DISTINTA', m)
    k = 'filas'
    sys.exit(0 if tot[k] and tot['iguales'] == tot[k] else 1)


if __name__ == '__main__':
    main()
