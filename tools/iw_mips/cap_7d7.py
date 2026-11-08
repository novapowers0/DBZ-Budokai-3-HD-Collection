# SPDX-License-Identifier: MIT
"""N2 (noche 6-7/11): dorado CON SEMILLA de la esquiva de Freezer (IW EU 0x7D7) en PCSX2 (plaza pool_N ya lanzada).

Sonda en RAM (no en la ISO), como iwx_bubbles_ia: la llamada 0x35DA08 (jal Char_PoseInputTransition 0x35DF50,
dentro de Char_PoseModeUpdateAll) pasa a un trampolin en 0xFA000 que copia las entradas (semilla de Memory, las 11
palabras 0x5D9D60.., estado, +0xC74, Krillin, camara +0x80..+0x9C, posicion, tabla de ronda, ronda, paso), llama a la
funcion original y copia las salidas. La llamada 0x35D440 (jal Math_WorldToScreen dentro de
Char_SelectBoneScreenDirection) va a un segundo trampolin que guarda la x,y de pantalla que devuelve (hasta 3).
Anillo de 128 registros de 256 B en 0xF0100; contador en 0xF0000. No cambia ninguna decision del juego.
Uso: python tools/iw_mips/cap_7d7.py N --salida <carpeta> [--seg 300]   (PCSX2 de la plaza N ya en marcha con el estado)
"""
import argparse
import json
import struct
import sys
import time
from pathlib import Path

R = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(R))
import pcsx2_pool as PP  # noqa: E402
import importlib.util  # noqa: E402

BASE, CUR = 0xF0000, 0xF0004
REC0, NREC, TREC = 0xF0100, 128, 0x100
CODE1, CODE2 = 0xFA000, 0xFA400
SITIO1, TRANS = 0x35DA08, 0x35DF50
SITIO2, PANT = 0x35D440, 0x2EF870
SEMILLA, G, TABLA, PCAM, PVIEW, PASO, CNT = 0x49BE08, 0x5D9D60, 0x5D9B48, 0x49CD6C, 0x49CE78, 0x49C7DC, 0x4A4D40
zero, v0, a0, a1, t0, t1, t2, t3, t4, sp, ra = 0, 2, 4, 5, 8, 9, 10, 11, 12, 29, 31


def i(op, rs, rt, imm): return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)
def lw(rt, off, rs): return i(0x23, rs, rt, off)
def lh(rt, off, rs): return i(0x21, rs, rt, off)
def sw(rt, off, rs): return i(0x2B, rs, rt, off)
def sd(rt, off, rs): return i(0x3F, rs, rt, off)
def ld(rt, off, rs): return i(0x37, rs, rt, off)
def addiu(rt, rs, imm): return i(9, rs, rt, imm)
def andi(rt, rs, imm): return i(0xC, rs, rt, imm)
def sltiu(rt, rs, imm): return i(0xB, rs, rt, imm)
def lui(rt, imm): return i(0xF, 0, rt, imm)
def beq(rs, rt, off): return i(4, rs, rt, off)
def sll(rd, rt, sa): return (rt << 16) | (rd << 11) | (sa << 6)
def addu(rd, rs, rt): return (rs << 21) | (rt << 16) | (rd << 11) | 0x21
def jal(a): return (3 << 26) | (a >> 2)
def jr(rs): return (rs << 21) | 8


def hi_lo(a):
    lo = a & 0xFFFF
    return ((a >> 16) + (1 if lo & 0x8000 else 0)) & 0xFFFF, lo


def cargar_abs(reg, a):
    h, l = hi_lo(a)
    return [lui(reg, h), lw(reg, l, reg)]


def copia_abs(a, k):  # *(a) -> rec[k] (rec en t2)
    return cargar_abs(t0, a) + [sw(t0, 4 * k, t2)]


# disposicion del registro (palabras)
CAMPOS_IN = dict(seed=0, glob=1, estado=12, c74=13, krillin=14, cam=15, pos=23, tabla=27, ronda=30, paso=31, x=32, j=33, cnt=39)
CAMPOS_OUT = dict(seed=40, glob=41, estado=52, b60=53, c74=54, nscr=55, scr=56)


def codigo1():
    c = [addiu(sp, sp, -48), sd(ra, 0, sp), sd(a0, 8, sp), sd(a1, 16, sp),
         lui(t3, BASE >> 16), lw(t0, 0, t3), andi(t1, t0, NREC - 1), sll(t1, t1, 8), addu(t2, t1, t3),
         addiu(t2, t2, REC0 - BASE), sw(t2, CUR - BASE, t3), sw(t2, 24, sp), sw(zero, 4 * 55, t2)]
    c += copia_abs(SEMILLA, 0)
    for k in range(11):
        c += copia_abs(G + 4 * k, 1 + k)
    c += [lh(t0, 0xCBC, a0), sw(t0, 4 * 12, t2), lw(t0, 0xC74, a0), sw(t0, 4 * 13, t2), lh(t0, 0xCBC, a1), sw(t0, 4 * 14, t2)]
    c += cargar_abs(t1, PCAM)
    for k in range(8):
        c += [lw(t0, 0x80 + 4 * k, t1), sw(t0, 4 * (15 + k), t2)]
    for k in range(4):
        c += [lw(t0, 0x40 + 4 * k, a0), sw(t0, 4 * (23 + k), t2)]
    for k in range(3):
        c += copia_abs(TABLA + 4 * k, 27 + k)
    c += cargar_abs(t1, PVIEW) + [lw(t0, 0x14, t1), sw(t0, 4 * 30, t2)]
    c += copia_abs(PASO, 31) + [sw(a0, 4 * 32, t2), sw(a1, 4 * 33, t2)] + copia_abs(CNT, 39)
    c += [jal(TRANS), 0, ld(a0, 8, sp), lw(t2, 24, sp)]
    c += copia_abs(SEMILLA, 40)
    for k in range(11):
        c += copia_abs(G + 4 * k, 41 + k)
    c += [lh(t0, 0xCBC, a0), sw(t0, 4 * 52, t2), lw(t0, 0xB60, a0), sw(t0, 4 * 53, t2), lw(t0, 0xC74, a0), sw(t0, 4 * 54, t2),
          lui(t3, BASE >> 16), sw(zero, CUR - BASE, t3), lw(t0, 0, t3), addiu(t0, t0, 1), sw(t0, 0, t3),
          ld(ra, 0, sp), ld(a0, 8, sp), ld(a1, 16, sp), jr(ra), addiu(sp, sp, 48)]
    return c


def codigo2():
    c = [addiu(sp, sp, -32), sd(ra, 0, sp), sd(a0, 8, sp), jal(PANT), 0,
         ld(t0, 8, sp), lui(t3, BASE >> 16), lw(t2, CUR - BASE, t3)]
    fin_ = len(c) + 15  # indice de la etiqueta fin
    c += [beq(t2, zero, 0), 0,                       # -> fin (parcheado abajo)
          lw(t1, 4 * 55, t2), addiu(t4, t1, 1), sw(t4, 4 * 55, t2),
          sltiu(t3, t1, 3), beq(t3, zero, 0), 0,     # -> fin
          sll(t3, t1, 3), addu(t3, t3, t2),
          lw(t4, 0, t0), sw(t4, 4 * 56, t3), lw(t4, 4, t0), sw(t4, 4 * 57, t3), 0]
    assert len(c) == fin_
    for k in (8, 14):
        c[k] |= (fin_ - (k + 1)) & 0xFFFF
    c += [ld(ra, 0, sp), ld(a0, 8, sp), jr(ra), addiu(sp, sp, 32)]
    return c


def instalar(p):
    o1, o2 = p.u32(SITIO1), p.u32(SITIO2)
    if o1 != jal(TRANS) or o2 != jal(PANT):
        raise RuntimeError('codigo EU distinto del esperado: %08X %08X' % (o1, o2))
    if any(p.leer(BASE, 0x400)) or any(p.leer(REC0 + 0x400, 0x400)) or any(p.leer(CODE1, 0x800)):
        raise RuntimeError('el hueco 0xF0000/0xFA000 no esta a cero')
    c1, c2 = codigo1(), codigo2()
    assert len(c1) * 4 <= CODE2 - CODE1
    p.escribir(BASE, bytes(REC0 - BASE + NREC * TREC))
    p.escribir(CODE1, struct.pack(f'<{len(c1)}I', *c1))
    p.escribir(CODE2, struct.pack(f'<{len(c2)}I', *c2))
    p.escribir(SITIO2, struct.pack('<I', jal(CODE2)))
    p.escribir(SITIO1, struct.pack('<I', jal(CODE1)))
    return len(c1), len(c2)


def desinstalar(p):
    p.escribir(SITIO1, struct.pack('<I', jal(TRANS)))
    p.escribir(SITIO2, struct.pack('<I', jal(PANT)))


def decodificar(b):
    w = struct.unpack('<64I', b)
    s16 = lambda v: (v & 0xFFFF) - 0x10000 if v & 0x8000 else v & 0xFFFF
    return dict(seed_in=w[0], glob_in=list(w[1:12]), estado=s16(w[12]), c74=w[13], krillin=s16(w[14]), cam=list(w[15:23]),
                pos=list(w[23:27]), tabla=list(w[27:30]), ronda=w[30], paso=w[31], x=w[32], j=w[33], cnt=w[39],
                seed_out=w[40], glob_out=list(w[41:52]), estado_out=s16(w[52]), b60=w[53], c74_out=w[54],
                nscr=w[55], scr=list(w[56:62]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('n', type=int)
    ap.add_argument('--salida', required=True)
    ap.add_argument('--seg', type=float, default=300)
    ap.add_argument('--seco', action='store_true', help='solo ensambla y muestra tamanos')
    a = ap.parse_args()
    if a.seco:
        print(len(codigo1()), len(codigo2()), 'palabras'); return
    out = Path(a.salida); out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('_sonda_n2', R / 'pcsx2_sonda.py')
    s = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)
    PP.registrar_en_sonda(s, a.n)
    p = s.Pine(PP.puerto(a.n), espera=200)
    t1 = time.time()
    while time.time() - t1 < 150:
        try:
            st, c0 = p.status(), p.u32(CNT)
            print('estado', st, 'contador', c0, flush=True)
            if st == 0 and c0 and p.u32(SITIO1) == jal(TRANS):
                break
        except OSError as e:
            print('pine', e, flush=True)
        time.sleep(2)
    else:
        raise SystemExit('PINE no listo')
    time.sleep(3)
    print('instalado', instalar(p), flush=True)
    f = (out / 'llamadas.jsonl').open('w', encoding='utf-8')
    visto, t0, ultimo, perdidas = 0, time.time(), 0, 0
    try:
        while time.time() - t0 < a.seg:
            try:
                n = p.u32(BASE)
            except OSError:
                time.sleep(0.05); continue
            if n - visto > NREC - 8:
                perdidas += n - visto - (NREC - 8); visto = n - (NREC - 8)
            while visto < n:
                k = visto % NREC
                r = decodificar(p.leer(REC0 + k * TREC, TREC))
                r['i'] = visto
                f.write(json.dumps(r) + '\n')
                visto += 1
            if time.time() - ultimo > 10:
                ultimo = time.time(); f.flush()
                print('t', round(time.time() - t0), 'llamadas', n, 'perdidas', perdidas, flush=True)
            time.sleep(0.1)
    finally:
        f.close()
        try:
            desinstalar(p)
        except OSError:
            pass
        print('fin llamadas', visto, 'perdidas', perdidas, flush=True)


if __name__ == '__main__':
    main()
