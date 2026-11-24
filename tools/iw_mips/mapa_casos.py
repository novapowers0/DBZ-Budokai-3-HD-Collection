# SPDX-License-Identifier: MIT
"""tanda4 iw-mapa (10/11): banco SIN JUEGO de los flujos del mapa que el dorado de PCSX2 no cubre.
El dorado de `mapa_pares.py` pulsa A/L1/R1 pero su estado (saga 0, una sola area abierta) no tiene esfera ni cambio de
saga: los 200/200 no prueban esos caminos. Aqui el ORACULO es el propio ELF (se ejecuta en ee.py), no una tabla escrita a
mano, y se comprueba el CONTRATO que consume src/iw_mips_mapa.inl + src/pasarela_iw.cpp:

  mision (A)  -> cola [9] y M+0x1C = nodo, M+0x14 = tipo (1), M+0x10 = indice
  esfera (A)  -> cola [0x10] y Transition_SetupWithParams(0x11, indice) con indice = #DMD +0x1C; M+0x14 = 2
  L1/R1       -> cola [2,5,0xE,10,3,1,8]; el area destino es el ARGUMENTO DEL COMANDO 5 (tabla 0x49C348/50), no el del 2
                 (ese es la direccion de salida: 1/2 -> 180/240 cuadros); la posicion por defecto sale de 0x47DB60
  borde       -> sin otra area abierta, L1/R1 NO empujan nada (iVar22 = 0): no se puede inventar el cambio

Uso: python tools/iw_mips/mapa_casos.py [--region eu] [--verbose]
Region USA: BLOQUEADO sin savestate/GP USA (este espejo es EU); las direcciones USA se verifican con mapa_usa.py."""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import mapa_banco as MB  # noqa: E402
import mapa_espejo as ME  # noqa: E402

EU = dict(mision_ok=0x2FE270, trans=0x2ADD80, pos_dm=0x47DB60, area_tab=0x49C348, area_tab_road=0x49C350)


def _estado_abierto(sp, byte):
    for i in range(0x580):
        sp.c.m[0x5C1490 + 0xC4 + i] = byte
    sp.c.call(0x2AB590, 0x5C1490, limite=5_000_000)


def _nodos(sp):
    out = []
    for i in range(0x80):
        n = sp.ctx + i * 0x30
        fl = struct.unpack_from('<I', sp.c.m, n + 4)[0]
        if fl:
            out.append((n, fl, struct.unpack_from('<H', sp.c.m, n)[0]))
    return out


def _a(sp, nodo):
    c, M = sp.c, sp.M
    c.w32(M + 0x330, nodo)
    c.w32(M + 0x4A8, 7)
    c.w32(M + 0x1C, 0)
    c.w32(0x5D92A0, 0)
    sp.ev.clear()
    sp.cuadro(pulsa=0x4000)
    cola = [c.r32(0x5D9220 + 4 * j) for j in range(c.r32(0x5D92A0))]
    args = [c.r32(0x5D9260 + 4 * j) for j in range(len(cola))]
    trans = [e for e in sp.ev if e[0].startswith('Transition')]
    return cola, args, trans, c.r32(M + 0x14), c.r32(M + 0x10), c.r32(M + 0x1C)


def caso_misiones(verbose):
    ok = tot = 0
    mal = []
    nodos_vistos = 0
    for saga in range(6):
        sp = ME.Espejo(saga=saga, goku=(0, 0))
        _estado_abierto(sp, 0x80)
        sp.aparecer()
        for n, fl, fila in _nodos(sp):
            # A solo tiene efecto en tipos 0-2; los 3/4 no los elige el cursor (no se fuerzan aqui)
            cola, args, trans, tipo, idx, sel = _a(sp, n)
            if tipo not in (0, 1):
                continue
            if tipo != 1:
                continue
            nodos_vistos += 1
            tot += 1
            bien = cola == [9] and sel == n and tipo == 1
            ok += bien
            if not bien:
                mal.append((saga, fila, cola, args, tipo, hex(sel), hex(n)))
    print('A mision: %d/%d (cola [9], M+0x1C = nodo, M+0x14 = 1)' % (ok, tot))
    for x in mal[:5]:
        print('  DIF', x)
    return ok == tot


def caso_esferas(verbose):
    ok = tot = 0
    mal = []
    for saga in range(6):
        sp = ME.Espejo(saga=saga, goku=(0, 0))
        _estado_abierto(sp, 0x80)
        sp.aparecer()
        for n, fl, fila in _nodos(sp):
            cola, args, trans, tipo, idx, sel = _a(sp, n)
            if tipo != 2:
                continue
            tot += 1
            indice = struct.unpack_from('<H', sp.c.m, n)[0]   # solo para el mensaje
            rec = _record_esfera(sp, fila)
            bien = (cola == [0x10] and len(trans) == 1 and trans[0][1:] == (0x11, rec) and idx == rec and sel == n)
            ok += bien
            if not bien:
                mal.append((saga, fila, rec, cola, args, trans, idx, hex(sel)))
            if verbose and bien:
                print('  esfera fila %d: 0x%X -> escena %#x' % (fila, 0x850 + rec, rec))
    print('A esfera: %d/%d (cola [0x10] y Transition(0x11, indice); M+0x10 = indice)' % (ok, tot))
    for x in mal[:6]:
        print('  DIF', x)
    return ok == tot


_REC = {}


def _record_esfera(sp, fila):
    """Indice de escena (#DMD +0x1C) del registro `fila`; se lee del #DMD ya reubicado en el espejo."""
    if not _REC:
        dmd = sp.c.m[ME.DMD_:ME.DMD_ + 0x20000]
        n = struct.unpack_from('<I', dmd, 8)[0]
        for k in range(n):
            o = struct.unpack_from('<I', dmd, 0x10 + 4 * k)[0] - ME.DMD_   # la tabla ya es absoluta
            _REC[k] = struct.unpack_from('<I', dmd, o + 0x1C)[0] if 0 <= o and o + 0x20 < len(dmd) else 0
    return _REC.get(fila, -1)


def _dir_salida(actual, destino):
    """Direccion de salida (argumento del comando 2) del decompilado EU 0x2FE270: 1 = 180 cuadros, 2 = 240, 0 = 20."""
    if actual == 0 and destino == 1:
        return 1
    if actual == 1 and destino in (0, 2):
        return 1 if destino == 0 else 2
    return 0


def caso_pick(verbose):
    """Story_PickObjectAtCursor (EU 0x2F8200) exige (flags&1)&&(flags&2)&&!(flags&4). El bit 4 lo limpia el callback
    del fundido (Story_RefreshSpriteNode 0x31B960), que va por las funciones de sprite MUDAS: por eso src/iw_mips_mapa.inl
    lo limpia por tiempo (90 cuadros). Aqui se comprueba el contrato del ELF: con bit 4 el pick NO elige; sin bit 4, SI.
    """
    sp = ME.Espejo(saga=0, goku=(0, 0))
    _estado_abierto(sp, 0x80)   # 0x80 = desbloqueada sin recoger: el nodo nace con bit 4
    sp.aparecer()
    nodos = [(n, fila) for n, fl, fila in _nodos(sp)]
    # un nodo de mision visible
    mision = next(((n, f) for n, f in nodos if f < 149), None)
    if not mision:
        print('pick: no hay nodo de mision visible; no se puede medir')
        return False
    n, fila = mision
    x, y = struct.unpack_from('<ff', sp.c.m, n + 0x18)
    sp.poner_goku(x, y)
    c, M = sp.c, sp.M
    con_bit = sin_bit = 0
    for con in (True, False):
        sp.poner_goku(x, y)
        for i in range(0x80):
            nn = sp.ctx + i * 0x30
            fl = struct.unpack_from('<I', c.m, nn + 4)[0]
            if fl:
                c.w32(nn + 4, (fl & ~4) if not con else fl)
        c.w32(M + 0x330, 0)
        sp.cuadro()
        sel = c.r32(M + 0x330)
        if con:
            con_bit = sel == n
        else:
            sin_bit = sel == n
    print('pick: con bit 4 elige el nodo (%s), sin bit 4 elige el nodo (%s)' %
          ('si' if con_bit else 'no', 'si' if sin_bit else 'no'))
    return (not con_bit) and sin_bit


def caso_l1r1(verbose, byte=0x81):
    """0x81 = area abierta y su destello ya procesado: no dispara el cambio automatico de 0x80 sin 0x01."""
    ok = tot = 0
    mal = []
    tab = None
    for saga in range(6):
        for tecla, nombre in ((0x400, 'L1'), (0x800, 'R1')):
            sp = ME.Espejo(saga=saga, goku=(0, 0))
            _estado_abierto(sp, byte)
            sp.aparecer()
            if tab is None:
                tab = list(sp.c.m[EU['area_tab']:EU['area_tab'] + 6])
            c, M = sp.c, sp.M
            c.w32(0x5D92A0, 0)
            sp.ev.clear()
            c.w32(M + 0x4A8, 7)
            sp.cuadro(pulsa=tecla)
            cola = [c.r32(0x5D9220 + 4 * j) for j in range(c.r32(0x5D92A0))]
            args = [c.r32(0x5D9260 + 4 * j) for j in range(len(cola))]
            tot += 1
            destino = min(saga + 1, 5) if tecla == 0x800 else max(saga - 1, 0)   # el original topea en iVar21 = 5
            if destino == saga:   # borde: el original no empuja
                bien = cola == []
            else:
                area = args[1] if len(args) > 1 else -1
                px, py = (struct.unpack_from('<ff', sp.c.m, EU['pos_dm'] + 16 * area) if 0 <= area < 6 else (-1.0, -1.0))
                bien = (cola == [2, 5, 0xE, 10, 3, 1, 8] and area == tab[destino] and args[0] == _dir_salida(saga, destino)
                        and args[4] == args[0] and args[5] == args[0] and 0 <= px <= 1280 and 0 <= py <= 960)
            ok += bien
            if not bien:
                mal.append((saga, nombre, cola, args, destino, tab, px if destino != saga else None))
            elif verbose:
                print('  %s saga %d -> %d%s' % (nombre, saga, destino, '' if destino == saga else ' en %d,%d' % (px, py)))
    print('L1/R1 (areas abiertas y procesadas, 0x81): %d/%d (cola de 7, area = comando 5, salida 1/2/0)' % (ok, tot))
    for x in mal[:6]:
        print('  DIF', x)
    return ok == tot


def caso_borde(verbose):
    ok = tot = 0
    mal = []
    for tecla, nombre in ((0x400, 'L1'), (0x800, 'R1')):
        sp = ME.Espejo(saga=0, goku=(0, 0))
        sp.aparecer()
        c, M = sp.c, sp.M
        c.w32(0x5D92A0, 0)
        sp.ev.clear()
        c.w32(M + 0x4A8, 7)
        sp.cuadro(pulsa=tecla)
        cola = [c.r32(0x5D9220 + 4 * j) for j in range(c.r32(0x5D92A0))]
        tot += 1
        ok += cola == []
        if cola:
            mal.append((nombre, cola))
    print('borde (saga 0, sin otra area abierta): %d/%d sin orden (no se inventa el cambio)' % (ok, tot))
    for x in mal:
        print('  DIF', x)
    return ok == tot


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--region', choices=('eu', 'usa'), default='eu')
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    if a.region != 'eu':
        print('region usa: BLOQUEADO (este espejo es EU: sin savestate ni GP USA). Direcciones/ELF USA: mapa_usa.py')
        return 3
    res = [caso_misiones(a.verbose), caso_esferas(a.verbose), caso_pick(a.verbose), caso_l1r1(a.verbose), caso_borde(a.verbose)]
    print('RESUMEN mapa_casos eu: %d/5 casos en verde' % sum(res))
    return 0 if all(res) else 1


if __name__ == '__main__':
    sys.exit(main())
