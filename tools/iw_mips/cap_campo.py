# SPDX-License-Identifier: MIT
"""Tanda3 (8/11): dorado por PAREJAS de cuadros de una funcion por campo de los minijuegos de campo MSNMW de IW
(0x7D0/7D2/7D9/7DF comparten codigo; EU, PCSX2 PINE). Solo lee y copia; no cambia ninguna decision del juego.

El trampolin sustituye las dos primeras instrucciones de la funcion (por defecto s_SPACE 0x31F280: addiu sp,-0x130 y
sd ra,0xC0(sp), se ejecutan en el trampolin) y pone ra a su propio retorno. Armado: copia las regiones ANTES (A) y
DESPUES (B). A->B es la llamada original; ee.py (campo.py) reejecuta A y se compara con B.
Uso: python tools/iw_mips/cap_campo.py N --salida <carpeta> [--parejas 300] [--fn 31F280]   (plaza N con el minijuego)
"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import cap_7d7 as K  # noqa: E402
from cap_7d7 import lw, sw, sd, ld, addiu, lui, jal, jr, zero, t0, t1, t2, t3, sp, ra, i  # noqa: E402

BASE, BUF, CODE = 0xF0000, 0x96000, 0xFC000
t4, t5 = 12, 13
# regiones absolutas (heap fijo por savestate): (direccion, bytes)
REG_A = [(0x49B000, 0x2000), (0x4AC000, 0x1000), (0x52E000, 0xA000), (0x5C0000, 0x2000), (0x5D8800, 0x500),
         (0x8E3000, 0x13000), (0x1497000, 0x7000), (0x1563000, 0x2000)]
REG_B = [(0x49B000, 0x2000), (0x4AC000, 0x1000), (0x52E000, 0xA000), (0x8E3000, 0x13000)]
SEM = 0x49BE08


def bne(rs, rt, off): return i(5, rs, rt, off)
def beq(rs, rt, off): return i(4, rs, rt, off)
def j(a): return (2 << 26) | (a >> 2)
def ori(rt, rs, imm): return i(0xD, rs, rt, imm)


def copiar(dir_, n, dst):
    h, l = K.hi_lo(dir_)
    c = [lui(t0, h), addiu(t0, t0, l)]
    hd, ld_ = K.hi_lo(dst)
    c += [lui(t1, hd), addiu(t1, t1, ld_), lui(t2, n >> 16), ori(t2, t2, n & 0xFFFF)]
    c += [lw(t3, 0, t0), sw(t3, 0, t1), addiu(t0, t0, 4), addiu(t1, t1, 4), addiu(t2, t2, -4), bne(t2, zero, -6), 0]
    return c


def disposicion():
    off, sal = BUF + 0x10, {}
    for nombre, lista in (('A', REG_A), ('B', REG_B)):
        sal[nombre] = []
        for d, n in lista:
            sal[nombre].append((d, n, off)); off += n
    assert off <= BASE - 0x100, hex(off)
    return sal, off


def codigo(fn, w0, w1):
    lay, _ = disposicion()
    b = lui(t4, BASE >> 16)
    c = [b, sw(ra, 0x20, t4), lw(t5, 0, t4), sw(t5, 0x28, t4)]
    bloqueA = []
    h, l = K.hi_lo(SEM)
    bloqueA += [lui(t0, h), lw(t0, l, t0), b, sw(t0, 4, t4)]
    for d, n, dst in lay['A']:
        bloqueA += copiar(d, n, dst) + [b]
    c += [beq(t5, zero, len(bloqueA)), 0] + bloqueA   # beq salta tras el bloque (delay slot incluido en len+1)
    # ra de retorno: se parchea al conocer la direccion
    idx_ra = len(c)
    c += [0, 0, w0, w1, j(fn + 8), 0]
    ret = len(c)
    bloqueB = []
    for d, n, dst in lay['B']:
        bloqueB += copiar(d, n, dst) + [b]
    bloqueB += [lui(t0, h), lw(t0, l, t0), b, sw(t0, 0x18, t4), sw(zero, 0, t4), lw(t0, 0x14, t4), addiu(t0, t0, 1),
                sw(t0, 0x14, t4)]
    c += [b, lw(t5, 0x28, t4), beq(t5, zero, len(bloqueB)), 0] + bloqueB
    c += [b, lw(t0, 0x2C, t4), addiu(t0, t0, 1), sw(t0, 0x2C, t4), lw(ra, 0x20, t4), jr(ra), 0]
    dir_ret = CODE + 4 * ret
    hr, lr = K.hi_lo(dir_ret)
    c[idx_ra], c[idx_ra + 1] = lui(ra, hr), addiu(ra, ra, lr)
    return c


def instalar(p, fn):
    w0, w1 = p.u32(fn), p.u32(fn + 4)
    if w0 != 0x27bdfed0 or w1 != 0xffbf00c0:
        raise RuntimeError('prologo distinto: %08X %08X' % (w0, w1))
    propio = p.u32(CODE) == lui(t4, BASE >> 16)   # restos de una captura anterior de esta misma sonda
    if not propio and (any(p.leer(BASE, 0x100)) or any(p.leer(CODE, 0x1000)) or any(p.leer(BUF, 0x1000))):
        raise RuntimeError('el hueco no esta a cero')
    c = codigo(fn, w0, w1)
    assert len(c) * 4 < 0x3000, len(c)
    p.escribir(BASE, bytes(0x100))
    p.escribir(CODE, struct.pack(f'<{len(c)}I', *c))
    p.escribir(fn + 4, struct.pack('<I', 0))
    p.escribir(fn, struct.pack('<I', j(CODE)))
    return len(c)


def desinstalar(p, fn):
    p.escribir(BASE, struct.pack('<I', 0)); time.sleep(0.2)
    p.escribir(fn, struct.pack('<II', 0x27bdfed0, 0xffbf00c0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('n', type=int)
    ap.add_argument('--salida', required=True)
    ap.add_argument('--parejas', type=int, default=300)
    ap.add_argument('--fn', default='31F280')
    ap.add_argument('--paso', type=float, default=0.0, help='espera extra entre parejas (s)')
    ap.add_argument('--teleport', action='store_true', help='coloca al jugador sobre los puntos del #ITM (cubre recogida, oculto, meta)')
    ap.add_argument('--seco', action='store_true')
    a = ap.parse_args()
    fn = int(a.fn, 16)
    lay, fin = disposicion()
    if a.seco:
        print(len(codigo(fn, 0x27bdfed0, 0xffbf00c0)), 'palabras, buffer hasta', hex(fin)); return
    import importlib.util
    import pcsx2_pool as PP
    out = Path(a.salida); out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('_sonda_campo', K.R / 'pcsx2_sonda.py')
    s = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)
    PP.registrar_en_sonda(s, a.n)
    p = s.Pine(PP.puerto(a.n), espera=200)
    p.s.settimeout(30)
    t1_ = time.time()
    while time.time() - t1_ < 400:
        try:
            g = p.u32(0x49CDE8)
            if 0x100000 <= g < 0x2000000 and struct.unpack('<h', p.leer(g + 0x165A, 2))[0] > 0:
                break
        except OSError:
            pass
        time.sleep(1)
    time.sleep(2)
    print('instalado', instalar(p, fn), 'palabras', flush=True)
    meta = dict(base=BUF + 0x10, bloques={k: v for k, v in lay.items()}, fn=fn, G=p.u32(0x49CDE8), mgr=p.u32(0x49CA70))
    f = (out / 'parejas.bin').open('wb')
    idx = []
    try:
        t0_ = time.time()
        pts = []
        if a.teleport:
            itm = p.u32(meta['G'] + 0x148)
            raw = p.leer(itm + 0x20, 16 * 112)
            pts = [struct.unpack_from('<3f', raw, 16 * k) for k in range(112)]
            pts = [q for q in pts if abs(q[0]) + abs(q[2]) > 1e-3]
            print('puntos', len(pts), flush=True)
        while len(idx) < a.parejas and time.time() - t0_ < 900:
            n0 = p.u32(BASE + 0x14)
            if pts:
                q = pts[len(idx) % len(pts)]
                jug = meta['mgr'] + 0x800
                p.escribir(jug + 0x40, struct.pack('<3f', *q))
                p.escribir(jug + 0x640, struct.pack('<3f', q[0], q[1] + 9.2, q[2]))
                time.sleep(0.01)
            p.escribir(BASE, struct.pack('<I', 1))
            t3_ = time.time()
            while p.u32(BASE + 0x14) == n0 and time.time() - t3_ < 5:
                time.sleep(0.003)
            if p.u32(BASE + 0x14) == n0:
                print('sin pareja', flush=True)
                p.escribir(BASE, struct.pack('<I', 0))
                continue
            cab = p.leer(BASE, 0x30)
            datos = p.leer(BUF + 0x10, fin - (BUF + 0x10))
            idx.append(dict(off=f.tell(), cnt=p.u32(K.CNT), sem=struct.unpack_from('<I', cab, 4)[0],
                            sem_b=struct.unpack_from('<I', cab, 0x18)[0], llamadas=struct.unpack_from('<I', cab, 0x2C)[0]))
            f.write(datos)
            if len(idx) % 10 == 0:
                print('parejas', len(idx), flush=True)
            if a.paso:
                time.sleep(a.paso)
    finally:
        f.close()
        try:
            desinstalar(p, fn)
        except OSError:
            pass
        meta['parejas'] = idx
        (out / 'parejas.json').write_text(json.dumps(meta), encoding='utf-8')
        print('fin', len(idx), flush=True)


if __name__ == '__main__':
    main()
