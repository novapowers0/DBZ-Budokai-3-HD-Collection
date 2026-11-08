# SPDX-License-Identifier: MIT
"""P1 (7/11): dorado por PAREJAS de cuadros del tick de Piccolo en el 0x7DD (EU, PCSX2 PINE). Solo lee y copia.

Sitio: el jal Char_DispatchPoseTick 0x363E70 de Battle_UpdateAllCharacters (0x2AA9BC, solo lo usa el actor 1 del tipo 0xE).
Al armar (BASE = 2) el trampolin copia en el cuadro N: A = estado completo ANTES del tick (las regiones de cap_7dd + la
tarea de Piccolo), B = lo que cambia DESPUES del tick; y en el cuadro N+1: C = lo mismo ANTES del tick. A->B es el tick
original; B->C es todo lo que pasa entre dos ticks (Update de pose, la tarea, la entrada del cuadro siguiente).
Uso: python tools/iw_mips/cap_tick7dd.py N --salida <carpeta> [--seg 240] [--parejas 300]   (plaza N con el 0x7DD)
"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import cap_7d7 as K  # noqa: E402
import cap_7dd as D  # noqa: E402
from cap_7d7 import lw, sw, sd, ld, addiu, lui, jal, jr, cargar_abs, zero, a0, t0, t1, t2, t3, sp, ra, i  # noqa: E402

SITIO, TICK = 0x2AA9BC, 0x363E70
BASE, BUF, CODE = 0xF0000, 0x96000, 0xFC000
t4, t5 = 12, 13
# (tipo, direccion, bytes): None = direccion fija; 'p' = *(direccion); ('pp', off) = *(*(direccion) + off)
SEM = (None, 0x49BE00, 0x10)                        # semilla Memory (0x49BE08)
TAREA = (('pp', 0x318), 0x49CBC0, 0x100)            # *(work + 0x260 + 0xB8): la tarea de Piccolo
COMPLETO = [SEM, TAREA] + [('p' if p else None, d, n) for p, d, n in D.PRE]
PEQUE = [SEM, TAREA, ('p', 0x49CA70, 0x49A0), ('p', 0x49CBC0, 0x1380), (None, 0x8E3340, 0x300), (None, 0x49C300, 0xB00),
         (None, 0x5D8800, 0x500)]


def bne(rs, rt, off): return i(5, rs, rt, off)
def beq(rs, rt, off): return i(4, rs, rt, off)


def copiar(tipo, dir_, n, dst):
    h, l = K.hi_lo(dir_)
    if tipo is None:
        c = [lui(t0, h), addiu(t0, t0, l)]
    elif tipo == 'p':
        c = cargar_abs(t0, dir_)
    else:
        c = cargar_abs(t0, dir_) + [lw(t0, tipo[1], t0)]
    hd, ld_ = K.hi_lo(dst)
    assert n & 0xFFFF < 0x8000 and n % 4 == 0
    c += [lui(t1, hd), addiu(t1, t1, ld_), lui(t2, n >> 16), addiu(t2, t2, n & 0xFFFF)]
    c += [lw(t3, 0, t0), sw(t3, 0, t1), addiu(t0, t0, 4), addiu(t1, t1, 4), addiu(t2, t2, -4), bne(t2, zero, -6), 0]
    return c


def disposicion():
    off, sal = BUF + 0x10, {}
    for nombre, lista in (('A', COMPLETO), ('B', PEQUE), ('C', PEQUE)):
        sal[nombre] = []
        for r in lista:
            sal[nombre].append((r, off))
            off += r[2]
    assert off <= BASE - 0x100, hex(off)
    return sal, off


def codigo():
    lay, _ = disposicion()
    c = [addiu(sp, sp, -48), sd(ra, 0, sp), sd(a0, 8, sp), lui(t4, BASE >> 16), lw(t5, 0, t4), sw(t5, 16, sp)]

    def bloque(nombre):
        b = []
        for (tipo, d, n), dst in lay[nombre]:
            b += copiar(tipo, d, n, dst)
        return b

    def si_igual(valor, cuerpo):   # if t5 == valor: cuerpo   (t5 se recarga de la pila)
        return [lw(t5, 16, sp), addiu(t4, zero, valor), bne(t5, t4, len(cuerpo) + 1), 0] + cuerpo
    c += si_igual(2, bloque('A'))
    c += si_igual(1, bloque('C') + [lui(t4, BASE >> 16), sw(zero, 0, t4), lw(t0, 0x14, t4), addiu(t0, t0, 1), sw(t0, 0x14, t4)])
    c += [ld(a0, 8, sp), jal(TICK), 0]
    c += si_igual(2, bloque('B') + [lui(t4, BASE >> 16), addiu(t0, zero, 1), sw(t0, 0, t4)])
    c += [ld(ra, 0, sp), ld(a0, 8, sp), jr(ra), addiu(sp, sp, 48)]
    return c


def instalar(p):
    if p.u32(SITIO) != jal(TICK):
        raise RuntimeError('codigo EU distinto: %08X' % p.u32(SITIO))
    if any(p.leer(BASE, 0x100)) or any(p.leer(CODE, 0x1000)) or any(p.leer(BUF, 0x1000)):
        raise RuntimeError('el hueco no esta a cero')
    c = codigo()
    assert len(c) * 4 < 0x3000, len(c)
    p.escribir(BASE, bytes(0x100))
    p.escribir(CODE, struct.pack(f'<{len(c)}I', *c))
    p.escribir(SITIO, struct.pack('<I', jal(CODE)))
    return len(c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('n', type=int)
    ap.add_argument('--salida', required=True)
    ap.add_argument('--seg', type=float, default=240)
    ap.add_argument('--parejas', type=int, default=300)
    ap.add_argument('--seco', action='store_true')
    a = ap.parse_args()
    lay, fin = disposicion()
    if a.seco:
        print(len(codigo()), 'palabras, buffer hasta', hex(fin)); return
    import importlib.util
    import pcsx2_pool as PP
    out = Path(a.salida); out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('_sonda_p1', K.R / 'pcsx2_sonda.py')
    s = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)
    PP.registrar_en_sonda(s, a.n)
    p = s.Pine(PP.puerto(a.n), espera=200)
    t1 = time.time()
    while time.time() - t1 < 150:
        try:
            if p.status() == 0 and p.u32(K.CNT):
                break
        except OSError:
            pass
        time.sleep(2)
    t2 = time.time()
    while time.time() - t2 < 300:   # el tick del actor 1 corre cuando Piccolo ya esta en juego
        try:
            if p.u32(0x49CA70) and p.u32(SITIO) == jal(TICK):
                break
        except OSError:
            pass
        time.sleep(1)
    print('instalado', instalar(p), 'palabras', flush=True)
    f = (out / 'parejas.bin').open('wb')
    idx = []
    t0_, ultimo = time.time(), 0
    try:
        while time.time() - t0_ < a.seg and len(idx) < a.parejas:
            n0 = p.u32(BASE + 0x14)
            p.escribir(BASE, struct.pack('<I', 2))
            t3_ = time.time()
            while p.u32(BASE + 0x14) == n0 and time.time() - t3_ < 5:
                time.sleep(0.005)
            if p.u32(BASE + 0x14) == n0:
                print('sin pareja (tick parado?)', flush=True)
                p.escribir(BASE, struct.pack('<I', 0))
                continue
            datos = p.leer(BUF + 0x10, fin - (BUF + 0x10))
            idx.append(dict(off=f.tell(), cnt=p.u32(K.CNT)))
            f.write(datos)
            if time.time() - ultimo > 10:
                ultimo = time.time(); print('parejas', len(idx), flush=True)
            time.sleep(0.05)
    finally:
        f.close()
        try:
            p.escribir(BASE, struct.pack('<I', 0)); time.sleep(0.1)
            p.escribir(SITIO, struct.pack('<I', jal(TICK)))
        except OSError:
            pass
        (out / 'parejas.json').write_text(json.dumps(dict(
            base=BUF + 0x10, bloques={k: [(r[0], r[1], r[2], o) for r, o in v] for k, v in lay.items()}, parejas=idx)),
            encoding='utf-8')
        print('fin', len(idx), flush=True)


if __name__ == '__main__':
    main()
