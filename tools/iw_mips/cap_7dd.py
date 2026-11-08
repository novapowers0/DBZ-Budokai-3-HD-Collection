# SPDX-License-Identifier: MIT
"""N2 (noche 6-7/11): dorado por MUESTREO de la CPU de Piccolo del 0x7DD (EU Piccolo_sCPUScript 0x3638C0) en PCSX2.

La CPU lee mucho (actores, tabla work, contexto, globales y el arbol de colisiones del escenario). El arbol es fijo y sale
del savestate; lo que cambia se copia en cada muestra: cuando el PC arma la bandera 0xF0000, el trampolin de la llamada
0x363D44 copia las regiones de abajo ANTES de llamar a la funcion original y la tabla work/global DESPUES (semilla
incluida). Solo lectura y copia: no cambia ninguna decision del juego.
Uso: python tools/iw_mips/cap_7dd.py N --salida <carpeta> [--seg 200] [--muestras 400]   (plaza N con el 0x7DD en marcha)
"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import cap_7d7 as K  # noqa: E402
from cap_7d7 import lw, sw, sd, ld, addiu, lui, jal, jr, cargar_abs, zero, a0, t0, t1, t2, t3, sp, ra, i  # noqa: E402

SITIO, IA = 0x363D44, 0x3638C0
BASE, BUF, CODE = 0xF0000, 0x96000, 0xFC000   # 0x96000-0xF0000 a cero en el savestate de la persecucion
# (puntero o None, direccion, bytes): con puntero, la region empieza en *(direccion)
PRE = [('mgr', 0x49CA70, 0x49A0), ('work', 0x49CBC0, 0x1380), (None, 0x5D8800, 0x500), (None, 0x49C300, 0xB00),
       (None, 0x4AC800, 0x40), (None, 0x8E3340, 0x240), ('mgf', 0x49CBA4, 0x100),
       # modelo/huesos del jugador que leen los rayos (Collision_RaycastVsModel), medidos con la traza de ee.py
       (None, 0x138A900, 0x6800), (None, 0x1391100, 0x6800), (None, 0x1417E00, 0x1000), (None, 0x149E100, 0x400),
       (None, 0x14AC300, 0x100), (None, 0x14B2F00, 0x1100), (None, 0x1EE4700, 0x2900)]
POST = [('work', 0x49CBC0, 0x1380), (None, 0x5D8800, 0x500)]
t4, t5 = 12, 13


def bne(rs, rt, off): return i(5, rs, rt, off)


def copiar(ptr, dir_, n, dst):
    """*(dir_) (o dir_) -> dst (n bytes). Usa t0..t3; dst absoluto."""
    c = cargar_abs(t0, dir_) if ptr else [lui(t0, K.hi_lo(dir_)[0]), addiu(t0, t0, K.hi_lo(dir_)[1])]
    h, l = K.hi_lo(dst)
    c += [lui(t1, h), addiu(t1, t1, l), lui(t2, n >> 16), addiu(t2, t2, n & 0xFFFF) if n & 0xFFFF < 0x8000 else 0]
    assert n & 0xFFFF < 0x8000
    c += [lw(t3, 0, t0), sw(t3, 0, t1), addiu(t0, t0, 4), addiu(t1, t1, 4), addiu(t2, t2, -4), bne(t2, zero, -6), 0]
    return c


def disposicion():
    off, pre, post = BUF + 0x10, [], []
    for r in PRE:
        pre.append((r, off)); off += r[2]
    for r in POST:
        post.append((r, off)); off += r[2]
    assert off <= BASE - 0x100
    return pre, post, off


def codigo():
    pre, post, _ = disposicion()
    c = [addiu(sp, sp, -48), sd(ra, 0, sp), sd(a0, 8, sp),
         lui(t4, BASE >> 16), lw(t0, 0x1C, t4), addiu(t0, t0, 1), sw(t0, 0x1C, t4), lw(t5, 0, t4), sw(t5, 16, sp)]
    salto = len(c); c += [0, 0]                       # beq t5, zero, -> sin_copia (parcheado)
    c += cargar_abs(t0, K.SEMILLA) + [sw(t0, 4, t4)] + cargar_abs(t0, K.CNT) + [sw(t0, 8, t4), sw(a0, 12, t4)]
    for (ptr, d, n), dst in pre:
        c += copiar(ptr, d, n, dst)
        c += [lui(t4, BASE >> 16)]
    sin = len(c)
    c[salto] = i(4, t5, zero, sin - (salto + 1)); c[salto + 1] = 0
    c += [ld(a0, 8, sp), jal(IA), 0, lw(t5, 16, sp)]
    fin_i = len(c); c += [0, 0]                       # beq t5, zero, -> fin
    c += [lui(t4, BASE >> 16)] + cargar_abs(t0, K.SEMILLA) + [sw(t0, 0x18, t4)]
    for (ptr, d, n), dst in post:
        c += copiar(ptr, d, n, dst)
    c += [lui(t4, BASE >> 16), sw(zero, 0, t4), lw(t0, 0x14, t4), addiu(t0, t0, 1), sw(t0, 0x14, t4)]
    fin = len(c)
    c[fin_i] = i(4, t5, zero, fin - (fin_i + 1))
    c += [ld(ra, 0, sp), ld(a0, 8, sp), jr(ra), addiu(sp, sp, 48)]
    return c


def instalar(p):
    if p.u32(SITIO) != jal(IA):
        raise RuntimeError('codigo EU distinto: %08X' % p.u32(SITIO))
    _, _, fin = disposicion()
    if any(p.leer(BASE, 0x100)) or any(p.leer(CODE, 0x1000)) or any(p.leer(BUF, 0x1000)):
        raise RuntimeError('el hueco no esta a cero')
    c = codigo()
    p.escribir(BASE, bytes(0x100))
    p.escribir(CODE, struct.pack(f'<{len(c)}I', *c))
    p.escribir(SITIO, struct.pack('<I', jal(CODE)))
    return len(c), hex(fin)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('n', type=int)
    ap.add_argument('--salida', required=True)
    ap.add_argument('--seg', type=float, default=200)
    ap.add_argument('--muestras', type=int, default=400)
    ap.add_argument('--cargar', type=int)
    ap.add_argument('--seco', action='store_true')
    a = ap.parse_args()
    if a.seco:
        print(len(codigo()), 'palabras', disposicion()[2] and hex(disposicion()[2])); return
    import importlib.util
    import pcsx2_pool as PP
    out = Path(a.salida); out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('_sonda_n2c', K.R / 'pcsx2_sonda.py')
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
    if a.cargar is not None:
        time.sleep(3); p.cargar(a.cargar); time.sleep(3)
    t2 = time.time()
    while time.time() - t2 < 300:
        try:
            if p.u32(0x49CA70) and p.u32(SITIO) == jal(IA):
                break
        except OSError:
            pass
        time.sleep(1)
    print('instalado', instalar(p), flush=True)
    pre, post, fin = disposicion()
    t2 = time.time()
    while time.time() - t2 < 400:   # esperar a que la CPU de Piccolo corra (fin de la presentacion)
        n_ = p.u32(BASE + 0x1C)
        print('esperando llamadas', n_, 'cnt', p.u32(K.CNT), flush=True)
        if n_ > 60:
            break
        time.sleep(10)
    f = (out / 'muestras.bin').open('wb')
    idx = []
    t0_, hechas, ultimo = time.time(), 0, 0
    try:
        while time.time() - t0_ < a.seg and hechas < a.muestras:
            p.escribir(BASE + 0x14, b'') if False else None
            n0 = p.u32(BASE + 0x14)
            p.escribir(BASE, struct.pack('<I', 1))
            t3_ = time.time()
            while p.u32(BASE + 0x14) == n0 and time.time() - t3_ < 5:
                time.sleep(0.005)
            if p.u32(BASE + 0x14) == n0:
                print('sin muestra; llamadas', p.u32(BASE + 0x1C), flush=True)
                continue
            cab = struct.unpack('<8I', p.leer(BASE, 32))
            datos = p.leer(BUF + 0x10, fin - (BUF + 0x10))
            idx.append(dict(seed_in=cab[1], cnt=cab[2], actor=cab[3], seed_out=cab[6], off=f.tell()))
            f.write(datos)
            hechas += 1
            if time.time() - ultimo > 10:
                ultimo = time.time(); print('muestras', hechas, 'cnt', cab[2], flush=True)
            time.sleep(0.05)
    finally:
        f.close()
        try:
            p.escribir(BASE, struct.pack('<I', 0)); time.sleep(0.1)
            p.escribir(SITIO, struct.pack('<I', jal(IA)))
        except OSError:
            pass
        (out / 'muestras.json').write_text(json.dumps(dict(pre=[(r[0], r[1], r[2], o) for r, o in pre],
                                                            post=[(r[0], r[1], r[2], o) for r, o in post],
                                                            base=BUF + 0x10, muestras=idx)), encoding='utf-8')
        print('fin', hechas, flush=True)


if __name__ == '__main__':
    main()
