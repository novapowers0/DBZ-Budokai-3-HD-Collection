# SPDX-License-Identifier: MIT
"""tanda4 iw-mapa (8/11): dorado por PAREJAS del manejador de mandos del mapa de Dragon Mission de IW (EU, PCSX2 PINE).
Como cap_campo.py: un trampolin sustituye las dos primeras instrucciones de la funcion (GeneralMapScripts 0x2FE270 o
Story_MapCameraUpdate 0x2F9280), copia las regiones ANTES (A) y DESPUES (B). Aqui ademas, antes de copiar A, el trampolin
escribe en el pad del juego (0x4ADECC mantenidos, +2 recien pulsados, +0xE/+0xF palanca) los valores del buzon BASE+0x40..
que pone este script: el mando entra como si lo hubiera pulsado el jugador. No cambia ninguna otra decision del juego.
Uso: python tools/iw_mips/cap_mapa.py N --salida <carpeta> [--fn 2FE270] [--parejas 200]   (plaza N con el mapa: savestate .04)
     python tools/iw_mips/cap_mapa.py --lanzar --salida <carpeta>   (adquiere el turno de grabacion, lanza el juego y captura)"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import cap_7d7 as K  # noqa: E402
import cap_campo as C  # noqa: E402
from cap_7d7 import lw, sw, addiu, lui, jr, zero, t0, t1, t2, t3, sp, ra, i  # noqa: E402

BASE, BUF, CODE = C.BASE, C.BUF, C.CODE
t4, t5 = C.t4, C.t5
PAD = 0x4ADECC
SAVESTATE = Path('D:/DBZ3HD/_pcsx2_pool/3/sstates/SLES-55347 (335A5A1F).04.p2s')
CONT = 0x4A4D40
# gestor del mapa + nodos (heap fijo por savestate: 0x8E7300 + 0x1E00) y los globales que leen las dos funciones
REG_A = [(0x8E7000, 0x2200), (0x49C000, 0x1000), (0x4AD000, 0x1000), (0x5C1000, 0x1000), (0x5C5400, 0xC00), (0x5C6000, 0x5900),
         (0x5D9200, 0x100), (0x4A4000, 0x1000)]
REG_B = list(REG_A)
C.REG_A, C.REG_B = REG_A, REG_B


def sh(rt, off, rs): return i(0x29, rs, rt, off)
def sb(rt, off, rs): return i(0x28, rs, rt, off)


def inyeccion():
    h, l = K.hi_lo(PAD)
    return [lw(t0, 0x40, t4), lui(t1, h), sh(t0, l, t1), lw(t0, 0x44, t4), sh(t0, l + 2, t1), lw(t0, 0x48, t4), sb(t0, l + 0xE, t1),
            lw(t0, 0x4C, t4), sb(t0, l + 0xF, t1)]


def codigo(fn, w0, w1):
    """Como cap_campo.codigo con la inyeccion del mando antes de copiar A."""
    lay, _ = C.disposicion()
    b = lui(t4, BASE >> 16)
    c = [b, sw(ra, 0x20, t4), lw(t5, 0, t4), sw(t5, 0x28, t4)]
    bloqueA = inyeccion() + [b]
    h, l = K.hi_lo(C.SEM)
    bloqueA += [lui(t0, h), lw(t0, l, t0), b, sw(t0, 4, t4)]
    for d, n, dst in lay['A']:
        bloqueA += C.copiar(d, n, dst) + [b]
    c += [C.beq(t5, zero, len(bloqueA)), 0] + bloqueA
    idx_ra = len(c)
    c += [0, 0, w0, w1, C.j(fn + 8), 0]
    ret = len(c)
    bloqueB = []
    for d, n, dst in lay['B']:
        bloqueB += C.copiar(d, n, dst) + [b]
    bloqueB += [lui(t0, h), lw(t0, l, t0), b, sw(t0, 0x18, t4), sw(zero, 0, t4), lw(t0, 0x14, t4), addiu(t0, t0, 1),
                sw(t0, 0x14, t4)]
    c += [b, lw(t5, 0x28, t4), C.beq(t5, zero, len(bloqueB)), 0] + bloqueB
    c += [b, lw(t0, 0x2C, t4), addiu(t0, t0, 1), sw(t0, 0x2C, t4), lw(ra, 0x20, t4), jr(ra), 0]
    hr, lr = K.hi_lo(CODE + 4 * ret)
    c[idx_ra], c[idx_ra + 1] = lui(ra, hr), addiu(ra, ra, lr)
    return c


def instalar(p, fn):
    w0, w1 = p.u32(fn), p.u32(fn + 4)
    if (w0 >> 26) in (2, 3, 4, 5, 1, 6, 7) or (w1 >> 26) in (2, 3, 4, 5, 1, 6, 7):
        raise RuntimeError('prologo con salto: %08X %08X' % (w0, w1))
    c = codigo(fn, w0, w1)
    assert len(c) * 4 < 0x3000, len(c)
    p.escribir(BASE, bytes(0x100))
    p.escribir(CODE, struct.pack(f'<{len(c)}I', *c))
    p.escribir(fn + 4, struct.pack('<I', 0))
    p.escribir(fn, struct.pack('<I', C.j(CODE)))
    return len(c), w0, w1


def desinstalar(p, fn, w0, w1):
    p.escribir(BASE, struct.pack('<I', 0)); time.sleep(0.2)
    p.escribir(fn, struct.pack('<II', w0, w1))


def ruta(k):
    """Mando del par k: (mantenidos, pulsados, palanca x, palanca y, teleport a la casilla n o None). Palanca y: abajo +."""
    fases = [(20, 0, 0, 0, 0, None), (40, 0, 100, 0, 0, None), (25, 0, 0, 0, 100, None), (25, 0x8000, 100, 0, 0, None),
             (15, 0x20, 0, 0, 0, None), (20, 0x400, 0, 0, 0, None), (10, 0, 0, 0, 0, 'nodo'), (3, 0x4000, 0, 0, 0, 'nodo'),
             (20, 0, -100, -100, 0, None)]
    m = 0
    for dur, hold, lx, ly, _x, tele in [(f[0], f[1], f[2], f[3], 0, f[5]) for f in fases]:
        if k < m + dur:
            pulsa = hold if hold in (0x400, 0x4000) else 0
            return hold, pulsa, lx, ly, tele
        m += dur
    return 0, 0, 0, 0, None


def captura(p, fn, salida, n_pares):
    lay, fin = C.disposicion()
    n, w0, w1 = instalar(p, fn)
    meta = dict(base=BUF + 0x10, bloques={k: v for k, v in lay.items()}, fn=fn, M=p.u32(0x49CDAC), ctx=p.u32(0x49CDDC))
    print('instalado', n, 'palabras; M', hex(meta['M']), flush=True)
    # el savestate .04 tiene el menu de mision abierto (estado 9): se devuelve el mapa libre (estado 7, manejador 0x2FE270)
    a0 = p.u32(meta['M'])
    p.escribir(meta['M'] + 0x4A8, struct.pack('<I', 7)); p.escribir(a0 + 0x1C, struct.pack('<I', 0x2FE270))
    p.escribir(0x5D92A0, struct.pack('<I', 0)); p.escribir(meta['M'] + 0x330, struct.pack('<I', 0))
    time.sleep(0.5)
    f = (salida / 'parejas.bin').open('wb')
    idx = []
    nodos = []
    try:
        for k in range(n_pares):
            hold, pulsa, lx, ly, tele = ruta(k)
            if tele == 'nodo':
                for fl_i in range(0x80):
                    b = p.leer(meta['ctx'] + fl_i * 0x30, 0x30)
                    if struct.unpack_from('<I', b, 4)[0] == 3:
                        nodos.append(struct.unpack_from('<ff', b, 0x18))
                if nodos:
                    x, y = nodos[0]
                    p.escribir(meta['M'] + 0x1D0, struct.pack('<ff', x, y)); p.escribir(meta['M'] + 0x29C, struct.pack('<ff', x, y))
                nodos.clear()
            p.escribir(BASE + 0x40, struct.pack('<4I', hold, pulsa, lx & 0xFF, ly & 0xFF))
            n0 = p.u32(BASE + 0x14)
            p.escribir(BASE, struct.pack('<I', 1))
            t3_ = time.time()
            while p.u32(BASE + 0x14) == n0 and time.time() - t3_ < 5:
                time.sleep(0.003)
            if p.u32(BASE + 0x14) == n0:
                print('sin pareja', flush=True)
                p.escribir(BASE, struct.pack('<I', 0))
                continue
            datos = p.leer(BUF + 0x10, fin - (BUF + 0x10))
            idx.append(dict(off=f.tell(), k=k, mant=hold, pulsa=pulsa, lx=lx, ly=ly))
            f.write(datos)
            if len(idx) % 20 == 0:
                print('parejas', len(idx), flush=True)
    finally:
        f.close()
        try:
            desinstalar(p, fn, w0, w1)
        except OSError:
            pass
        meta['parejas'] = idx
        (salida / 'parejas.json').write_text(json.dumps(meta), encoding='utf-8')
        print('fin', len(idx), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('n', nargs='?', type=int)
    ap.add_argument('--salida', required=True)
    ap.add_argument('--fn', default='2FE270')
    ap.add_argument('--parejas', type=int, default=200)
    ap.add_argument('--lanzar', action='store_true')
    ap.add_argument('--pool', type=int, default=3)
    ap.add_argument('--seco', action='store_true')
    a = ap.parse_args()
    fn = int(a.fn, 16)
    salida = Path(a.salida)
    salida.mkdir(parents=True, exist_ok=True)
    if a.seco:
        print(len(codigo(fn, 0x27bdfe90, 0x24020010)), 'palabras'); return
    import importlib.util
    import pcsx2_pool as PP
    spec = importlib.util.spec_from_file_location('_sonda_mapa', K.R / 'pcsx2_sonda.py')
    s = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)
    if a.lanzar:
        from pcsx2_turnos import Grabacion
        iso = r'E:\DBZ3HD_trabajo\iw_eu\Dragon Ball Z - Infinite World (Europe).iso'
        with Grabacion('iw mapa cap_mapa fn=%s' % a.fn, tope_s=300):
            with PP.Sesion(a.pool, iso, SAVESTATE, CONT, limite=300, velocidad=1) as ses:
                ses.ejecutar(lambda p, desde: captura(p, fn, salida, a.parejas))
        return
    PP.registrar_en_sonda(s, a.n)
    p = s.Pine(PP.puerto(a.n), espera=200)
    p.s.settimeout(30)
    captura(p, fn, salida, a.parejas)


if __name__ == '__main__':
    main()
