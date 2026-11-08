# SPDX-License-Identifier: MIT
"""Interprete propio (MIT) del EE R5900 de PS2, subconjunto entero + COP1 + VU0 macro.

Ejecuta funciones del ELF del jugador (nada del juego se copia aqui ni se distribuye).
Decodificacion de nombres con rabbitizer (MIT); campos y semantica escritos a mano.
No usa ni copia Unicorn ni PS2Recomp (GPL).

Semantica de la FPU del R5900 (y de PCSX2 por defecto, que es donde se capturan los dorados):
- sin inf/NaN: exponente 255 se lee como numero normal (2^128); el resultado que desborda se recorta a +-0x7F7FFFFF;
- denormales a cero (entrada y salida);
- redondeo hacia cero (Chop) exacto: suma con TwoSum, division/raiz con fma para el resto;
- madd/msub/adda...: dos redondeos (producto y suma), como el interprete de PCSX2.
- cvt.w.s satura a 0x7FFFFFFF/0x80000000.
- dia1E6: FPU_HW = reglas del R5900 medidas con vectores de hardware (fpu_hw.py --comprobar); FPU_HW_SERIE las de serie.

GPR de 128 bits: r[] lleva los 64 bits bajos (sin signo), up[] los altos (solo lq/sq/MMI los tocan).

Llamadas: `hooks[dir] = f(cpu)` puente Python (lee a0-a3/f12.., escribe v0/f0); con `whitelist` (conjunto
de entradas) toda llamada fuera de la lista y sin puente va a `sin_puente` (se cuenta y devuelve 0, o
se lanza si `estricto`).
"""
import math
import struct
from collections import Counter

import rabbitizer as RB

M32, M64 = 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF
SX = 0xFFFFFFFF00000000
U32, U16, U64, F32 = struct.Struct('<I'), struct.Struct('<H'), struct.Struct('<Q'), struct.Struct('<f')
RAM = 0x2000000           # 32 MB del EE
PILA = 0x2000000          # pila propia del interprete (1 MB tras la RAM: no pisa nada de la instantanea)
SPR = 0x2100000           # scratchpad 0x70000000 (16 KB) mapeado aqui
TOTAL = SPR + 0x4000
RETORNO = 0xFFFFFF00      # ra falso: fin de la llamada
LLAMADA = 1 << 40         # marca de destino de jal/jalr
SALTAR = -1               # branch likely no tomado: anula el hueco
MAXF = 3.4028234663852886e38
MINF = 1.1754943508222875e-38


def sx32(x):
    x &= M32
    return x | SX if x & 0x80000000 else x


def s64(x):
    return x - (1 << 64) if x >> 63 else x


def fv(b):
    """bits -> float con la lectura del R5900 (sin inf/NaN, denormal = 0)."""
    e = (b >> 23) & 0xFF
    if e == 0:
        return -0.0 if b >> 31 else 0.0
    if e == 255:
        v = math.ldexp(1 + (b & 0x7FFFFF) / 8388608.0, 128)
        return -v if b >> 31 else v
    return F32.unpack(U32.pack(b))[0]


def _abajo(b):  # un ulp hacia cero (magnitud), bits sin signo
    return b - 1 if b & 0x7FFFFFFF else b


def fb(x, err=0.0, cercano=False):
    """float (+ error exacto) -> bits, hacia cero (o al mas cercano), recorte y denormal a 0."""
    neg = x < 0 or (x == 0 and math.copysign(1, x) < 0)
    ax = abs(x)
    if ax >= MAXF:
        b = 0x7F7FFFFF
    elif ax < MINF:
        b = 0
    else:
        b = U32.unpack(F32.pack(ax))[0]
        y = F32.unpack(U32.pack(b))[0]
        if cercano:
            pass  # ponytail: doble redondeo posible en sumas muy desiguales; exacto con TwoSum si hiciera falta
        elif y > ax:
            b -= 1
        elif y == ax and err and ((err < 0) != neg):
            b = _abajo(b)  # el valor exacto esta un poco por debajo de |x|
    return b | (0x80000000 if neg else 0)


# Redondeo: el R5900 real trunca (hacia cero). PCSX2 tiene un modo por unidad (EE FPU y VU0) en su ini;
# lo que cuenta es igualar los dorados de PCSX2 (medido: ver INFORME_fase0E).
FPU_CERCANO = False
VU_CERCANO = {}  # familia -> True = al mas cercano (add, addbc, adda, sub, mul..., madd, maddmul, opmul, opsub, div, sqrt)


FPU_SUMA_PS2 = False  # add.s/sub.s como PCSX2 (medido 6/11 en la esquiva de Freezer: 149/149); Bubbles sin

# dia1E6: reglas del R5900 medidas con los vectores de HARDWARE de ps2autotests (tools/iw_mips/fpu_hw.py).
# De serie las que se ejecutan en los 4 minijuegos y no cambian ningun dorado de PCSX2 (INFORME_dia1E 12):
# suma, mul, div, sqrt, rsqrt, madd. Sin ejercitar, apagadas (= PCSX2): ovf, maxmin, flags.
# Mismo orden de bits que kFpuHw* de iw_mips.cpp.
FPU_HW_NOMBRES = ('suma', 'mul', 'div', 'sqrt', 'rsqrt', 'madd', 'ovf', 'maxmin', 'flags')
FPU_HW_SERIE = frozenset({'suma', 'mul', 'div', 'sqrt', 'rsqrt', 'madd'})
FPU_HW_ENTORNO = FPU_HW_SERIE | {h.replace('fpu_hw_', '') for h in __import__('os').environ.get(
    'DBZ3_TEST_FPU_HW', '').replace(',', ' ').split()}  # solo pruebas
FPU_HW = set(FPU_HW_ENTORNO)


def suma_ps2(a, b):
    """PCSX2 FPU_ADD_SUB: el operando de exponente menor pierde los bits que el sumador no ve (deja uno de guarda)."""
    d = ((a >> 23) & 0xFF) - ((b >> 23) & 0xFF)
    if d >= 25:
        b &= 0x80000000
    elif d > 0:
        b &= (M32 << (d - 1)) & M32
    elif d <= -25:
        a &= 0x80000000
    elif d < 0:
        a &= (M32 << (-d - 1)) & M32
    return a, b


def f_add(a, b, cercano=None):
    s = a + b
    bb = s - a
    err = (a - (s - bb)) + (b - bb)
    return fb(s, err, FPU_CERCANO if cercano is None else cercano)


def f_mul(a, b, cercano=None):
    return fb(a * b, 0.0, FPU_CERCANO if cercano is None else cercano)  # producto de dos float32: exacto


def f_div(a, b, cercano=None):
    if b == 0:  # R5900: divide por cero -> +-max
        return 0x7F7FFFFF | ((0x80000000 if (math.copysign(1, a) < 0) != (math.copysign(1, b) < 0) else 0))
    q = a / b
    r = math.fma(-q, b, a)
    return fb(q, r / b if r else 0.0, FPU_CERCANO if cercano is None else cercano)


def f_sqrt(a, cercano=None):
    a = abs(a)  # R5900: raiz de |x|
    q = math.sqrt(a)
    r = math.fma(-q, q, a)
    return fb(q, r, FPU_CERCANO if cercano is None else cercano)


# ---- dia1E6: COP1 con reglas de hardware (FPU_HW no vacio). Con el conjunto vacio no se llama: = lo de arriba ----
_BAND = [0]  # banderas FCR31 de la ultima operacion (causa | pegajosa): I 0x20040, D 0x10020, O 0x8010
P128, P129 = 2.0 ** 128, 2.0 ** 129


def _fbh(x, err=0.0, cercano=False):
    """fb + 'ovf': exponente 255 es un numero y el desborde da +-0x7FFFFFFF (no 0x7F7FFFFF de PCSX2)."""
    ax = abs(x)
    if 'ovf' in FPU_HW and ax >= MAXF:
        r = fb(x / P128, err / P128, cercano)  # mismos bits de mantisa, exponente -128
        if (r >> 23) & 0xFF < 128:
            return r + 0x40000000
        _BAND[0] |= 0x8010
        return (r & 0x80000000) | 0x7FFFFFFF
    if ax >= P128:
        _BAND[0] |= 0x8010
    return fb(x, err, cercano)


def _mul_h(a, b):
    """'mul': el multiplicador quita 1 al producto de 48 bits si el bit 1 de la mantisa de ft esta (booth):
    solo se nota si el producto es exacto (-1 ulp). Devuelve (producto exacto, booth)."""
    return fv(a) * fv(b), 'mul' in FPU_HW and (b >> 1) & 1 and b & 0x7F800000


def _suma_h(a, b, guarda):
    if guarda:
        a, b = suma_ps2(a, b)
    x, y = fv(a), fv(b)
    s = x + y
    bb = s - x
    return _fbh(s, (x - (s - bb)) + (y - bb))


def fpu_h(op, a, b=0, c=0, guarda=False):
    """Una operacion COP1 en bits con las reglas de FPU_HW (cada una por separado; sin ninguna = PCSX2)."""
    H = FPU_HW
    _BAND[0] = 0
    if op in ('add', 'sub', 'adda', 'suba'):
        return _suma_h(a, b ^ (0x80000000 if op[0] == 's' else 0), guarda or 'suma' in H)
    if op in ('mul', 'mula'):
        p, booth = _mul_h(a, b)
        r = _fbh(p, -p if booth else 0.0)
        return r & 0x80000000 if booth and not r & 0x7F800000 else r
    if op == 'div':
        x, y = fv(a), fv(b)
        if y == 0:
            _BAND[0] |= 0x20040 if x == 0 else 0x10020
            return ((a ^ b) & 0x80000000) | (0x7FFFFFFF if 'ovf' in H else 0x7F7FFFFF)
        q = x / y
        if 'div' in H:  # al mas cercano (par); el doble redondeo double->float es inocuo en la division
            return _fbh(q, 0.0, True)
        r = math.fma(-q, y, x)
        return _fbh(q, r / y if r else 0.0)
    if op == 'sqrt':
        if a >> 31 and a & 0x7F800000:
            _BAND[0] |= 0x20040
        x = abs(fv(a))
        q = math.sqrt(x)
        return _fbh(q, 0.0, True) if 'sqrt' in H else _fbh(q, math.fma(-q, q, x))
    if op == 'rsqrt':  # fs / sqrt(ft)
        if 'rsqrt' in H:
            _BAND[0] = 0x20040 if b >> 31 and b & 0x7F800000 else 0
            s = fv(_fbh(math.sqrt(abs(fv(b))), 0.0, True))
        else:
            s = fv(fpu_h('sqrt', b))
        x = fv(a)
        if s == 0:
            _BAND[0] |= 0x20040 if x == 0 else 0x10020
            return (a & 0x80000000) | (0x7FFFFFFF if 'ovf' in H else 0x7F7FFFFF)
        if 'rsqrt' in H:  # 'rsqrt': fs * (1/raiz), raiz e inverso al mas cercano, producto truncado (39/39 vectores)
            return _fbh(x * fv(_fbh(1.0 / s, 0.0, True)))
        q = x / s
        r = math.fma(-q, s, x)
        return _fbh(q, r / s if r else 0.0)
    if op in ('max', 'min'):
        if 'maxmin' in H:  # bits con signo y magnitud: -0 < +0, sin pasar denormales a 0
            k = (a & 0x7FFFFFFF) if not a >> 31 else -(a & 0x7FFFFFFF) - 1
            j = (b & 0x7FFFFFFF) if not b >> 31 else -(b & 0x7FFFFFFF) - 1
            return a if (k >= j) == (op == 'max') else b
        return _fbh((max if op == 'max' else min)(fv(a), fv(b)))
    if op in ('madd', 'msub', 'madda', 'msuba'):
        neg = 0x80000000 if op[1] == 's' else 0
        if 'madd' not in H:  # producto redondeado y recortado, luego la suma (como PCSX2)
            return _suma_h(c, fpu_h('mul', a, b) ^ neg, False)
        # 'madd': producto truncado a 24 bits con exponente libre; la suma alinea con un bit de guarda
        p, booth = _mul_h(a, b)
        if p:
            m, e = math.frexp(p)
            q = math.trunc(m * 16777216.0)
            if booth and q == m * 16777216.0:
                q -= 1 if q > 0 else -1
            p = math.ldexp(q, e - 24)
        if neg:
            p = -p
        x = fv(c)
        if x and p:
            ex, ep = math.frexp(x)[1], math.frexp(p)[1]
            u = 2.0 ** (max(ex, ep) - 25)
            if ex < ep:
                x = math.trunc(x / u) * u
            elif ep < ex:
                p = math.trunc(p / u) * u
        return _fbh(x + p)  # exacta: los dos caben en 26 bits alineados
    raise KeyError(op)


def v_add(a, b): return f_add(a, b, VU_CERCANO.get('opsub', False))
def v_mul(a, b): return f_mul(a, b, VU_CERCANO.get('opmul', False))
def v_div(a, b): return f_div(a, b, VU_CERCANO.get('div', False))
def v_sqrt(a): return f_sqrt(a, VU_CERCANO.get('sqrt', False))


class Ee:
    def __init__(s, mem, gp, entradas=(), nombres=None):
        s.m = mem
        s.r = [0] * 32
        s.up = [0] * 32
        s.f = [0] * 32
        s.vf = [[0, 0, 0, 0] for _ in range(32)]
        s.vf[0] = [0, 0, 0, 0x3F800000]
        s.vi = [0] * 16
        s.acc = 0
        s.vacc = [0, 0, 0, 0]
        s.q = 0x3F800000
        s.c = False  # FCR31.C
        s.fcr = 0  # dia1E6: banderas de FCR31 (causa y pegajosa), solo con FPU_HW 'flags'
        s.lo = s.hi = s.lo1 = s.hi1 = 0
        s.gp = gp
        s.code = {}
        s.hooks = {}
        s.whitelist = None
        s.estricto = False
        s.entradas = set(entradas)
        s.nombres = nombres or {}
        s.llamadas = Counter()
        s.sin_puente = Counter()
        s.io = Counter()
        s.n = 0
        s.desconocidas = Counter()

    # ---------------- memoria ----------------
    def ph(s, a):
        a &= M32
        if a < RAM:
            return a
        if 0x70000000 <= a < 0x70004000:
            return SPR + (a - 0x70000000)
        if a >= 0x80000000 or 0x20000000 <= a < 0x40000000:
            b = a & 0x1FFFFFFF
            if b < RAM:
                return b
        if PILA <= a < SPR:
            return a
        return None

    def r8(s, a):
        p = s.ph(a)
        if p is None:
            s.io[a & M32] += 1
            return 0
        return s.m[p]

    def r16(s, a):
        p = s.ph(a)
        if p is None:
            s.io[a & M32] += 1
            return 0
        return U16.unpack_from(s.m, p)[0]

    def r32(s, a):
        p = a if a < RAM else s.ph(a)
        if p is None:
            s.io[a & M32] += 1
            return 0
        return U32.unpack_from(s.m, p)[0]

    def r64(s, a):
        p = a if a < RAM else s.ph(a)
        if p is None:
            s.io[a & M32] += 1
            return 0
        return U64.unpack_from(s.m, p)[0]

    def w8(s, a, v):
        p = s.ph(a)
        if p is None:
            s.io[a & M32] += 1
            return
        s.m[p] = v & 0xFF

    def w16(s, a, v):
        p = s.ph(a)
        if p is None:
            s.io[a & M32] += 1
            return
        U16.pack_into(s.m, p, v & 0xFFFF)

    def w32(s, a, v):
        p = a if a < RAM else s.ph(a)
        if p is None:
            s.io[a & M32] += 1
            return
        U32.pack_into(s.m, p, v & M32)

    def w64(s, a, v):
        p = a if a < RAM else s.ph(a)
        if p is None:
            s.io[a & M32] += 1
            return
        U64.pack_into(s.m, p, v & M64)

    def rf(s, a):
        return fv(s.r32(a))

    # ---------------- ejecucion ----------------
    def call(s, addr, *args, fargs=(), sp=PILA + 0xFF000, limite=50_000_000):
        r = s.r
        for i, v in enumerate(args):
            r[4 + i] = sx32(v) if v >= 0 else v & M64
        for i, v in enumerate(fargs):
            s.f[12 + i] = fb(v)
        r[28], r[29], r[31] = s.gp, sp, RETORNO
        s.run(addr, limite)
        return r[2]

    def run(s, pc, limite):
        code, r, hooks = s.code, s.r, s.hooks
        n0 = s.n
        dt = None
        n = 0
        while True:
            fn = code.get(pc)
            if fn is None:
                fn = s.decodificar(pc)
            res = fn()
            n += 1
            if dt is not None:
                pc, dt = dt, None
                if pc >= LLAMADA:
                    pc = s.entrar(pc - LLAMADA)
                if pc == RETORNO:
                    break
                if n > limite:
                    s.n = n0 + n
                    raise RuntimeError(f'limite de instrucciones en {pc:#x}')
                continue
            if res is None:
                pc += 4
            elif res == SALTAR:
                pc += 8
            else:
                dt = res
                pc += 4
        s.n = n0 + n

    def entrar(s, t):
        s.llamadas[t] += 1
        h = s.hooks.get(t)
        if h is not None:
            h(s)
            return s.r[31] & M32
        if s.whitelist is not None and t not in s.whitelist:
            s.sin_puente[t] += 1
            if s.estricto:
                raise RuntimeError(f'llamada sin puente {t:#x} {s.nombres.get(t, "")}')
            s.r[2] = 0
            s.f[0] = 0
            return s.r[31] & M32
        return t

    # ---------------- decodificacion ----------------
    def decodificar(s, pc):
        w = s.r32(pc)
        ins = RB.Instruction(w, pc, RB.InstrCategory.R5900)
        nom = ins.getOpcodeName()
        for suf in ('.ni', '.i'):  # qmfc2.ni / ctc2.i: sin interbloqueo con microprogramas (VU0 macro: igual)
            if nom.endswith(suf):
                nom = nom[:-len(suf)]
        g = getattr(s, 'i_' + nom.replace('.', '_'), None)
        if g is None:
            s.desconocidas[nom] += 1
            raise NotImplementedError(f'{pc:#x}: {ins.disassemble()} ({nom})')
        rs, rt, rd, sa = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31
        imm = w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        fn = g(pc, w, rs, rt, rd, sa, imm, simm)
        s.code[pc] = fn
        return fn

    # ----- helpers de generacion -----
    def _w(s, rd, f):  # instruccion que escribe rd (r0 = nop)
        if rd == 0:
            return _nop
        return f

    # ===== enteras =====
    def i_nop(s, *a): return _nop
    def i_sync(s, *a): return _nop
    def i_cache(s, *a): return _nop
    def i_pref(s, *a): return _nop
    def i_ei(s, *a): return _nop
    def i_di(s, *a): return _nop

    def i_lui(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; v = sx32(imm << 16)
        def f(): r[rt] = v
        return s._w(rt, f)

    def i_addiu(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rt] = sx32(r[rs] + simm)
        return s._w(rt, f)
    i_addi = i_addiu

    def i_daddiu(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rt] = (r[rs] + simm) & M64
        return s._w(rt, f)
    i_daddi = i_daddiu

    def i_andi(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rt] = r[rs] & imm
        return s._w(rt, f)

    def i_ori(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rt] = r[rs] | imm
        return s._w(rt, f)

    def i_xori(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rt] = r[rs] ^ imm
        return s._w(rt, f)

    def i_slti(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rt] = 1 if s64(r[rs]) < simm else 0
        return s._w(rt, f)

    def i_sltiu(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; u = simm & M64
        def f(): r[rt] = 1 if r[rs] < u else 0
        return s._w(rt, f)

    def _r3(s, rd, op):
        return s._w(rd, op)

    def i_addu(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = sx32(r[rs] + r[rt])
        return s._w(rd, f)
    i_add = i_addu

    def i_subu(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = sx32(r[rs] - r[rt])
        return s._w(rd, f)
    i_sub = i_subu

    def i_daddu(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = (r[rs] + r[rt]) & M64
        return s._w(rd, f)
    i_dadd = i_daddu

    def i_dsubu(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = (r[rs] - r[rt]) & M64
        return s._w(rd, f)
    i_dsub = i_dsubu

    def i_move(s, pc, w, rs, rt, rd, sa, imm, simm):  # alias de or/daddu con cero
        r = s.r
        def f(): r[rd] = r[rs] | r[rt]
        return s._w(rd, f)

    def i_and(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = r[rs] & r[rt]
        return s._w(rd, f)

    def i_or(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = r[rs] | r[rt]
        return s._w(rd, f)

    def i_xor(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = r[rs] ^ r[rt]
        return s._w(rd, f)

    def i_nor(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = ~(r[rs] | r[rt]) & M64
        return s._w(rd, f)

    def i_not(s, pc, w, rs, rt, rd, sa, imm, simm):
        return s.i_nor(pc, w, rs, rt, rd, sa, imm, simm)

    def i_negu(s, pc, w, rs, rt, rd, sa, imm, simm):
        return s.i_subu(pc, w, rs, rt, rd, sa, imm, simm)
    i_neg = i_negu

    def i_slt(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = 1 if s64(r[rs]) < s64(r[rt]) else 0
        return s._w(rd, f)

    def i_sltu(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = 1 if r[rs] < r[rt] else 0
        return s._w(rd, f)

    def i_movz(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            if r[rt] == 0: r[rd] = r[rs]
        return s._w(rd, f)

    def i_movn(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            if r[rt] != 0: r[rd] = r[rs]
        return s._w(rd, f)

    # desplazamientos (32 bits con extension de signo)
    def i_sll(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = sx32(r[rt] << sa)
        return s._w(rd, f)

    def i_srl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = sx32((r[rt] & M32) >> sa)
        return s._w(rd, f)

    def i_sra(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = sx32(s64(sx32(r[rt])) >> sa)
        return s._w(rd, f)

    def i_sllv(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = sx32(r[rt] << (r[rs] & 31))
        return s._w(rd, f)

    def i_srlv(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = sx32((r[rt] & M32) >> (r[rs] & 31))
        return s._w(rd, f)

    def i_srav(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = sx32(s64(sx32(r[rt])) >> (r[rs] & 31))
        return s._w(rd, f)

    def i_dsll(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = (r[rt] << sa) & M64
        return s._w(rd, f)

    def i_dsll32(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; k = sa + 32
        def f(): r[rd] = (r[rt] << k) & M64
        return s._w(rd, f)

    def i_dsrl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = r[rt] >> sa
        return s._w(rd, f)

    def i_dsrl32(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; k = sa + 32
        def f(): r[rd] = r[rt] >> k
        return s._w(rd, f)

    def i_dsra(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = (s64(r[rt]) >> sa) & M64
        return s._w(rd, f)

    def i_dsra32(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; k = sa + 32
        def f(): r[rd] = (s64(r[rt]) >> k) & M64
        return s._w(rd, f)

    def i_dsllv(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = (r[rt] << (r[rs] & 63)) & M64
        return s._w(rd, f)

    def i_dsrlv(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = r[rt] >> (r[rs] & 63)
        return s._w(rd, f)

    def i_dsrav(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = (s64(r[rt]) >> (r[rs] & 63)) & M64
        return s._w(rd, f)

    # multiplicacion / division (pipeline 0 y 1); forma de 3 operandos del R5900 (rd = LO)
    def _mult(s, rs, rt, rd, uns, p1, acum=0):
        r = s.r
        def f():
            a, b = (r[rs] & M32, r[rt] & M32) if uns else (s64(sx32(r[rs])), s64(sx32(r[rt])))
            p = a * b
            if acum:  # madd/maddu: HI:LO += rs*rt
                prev = ((s.hi1 if p1 else s.hi) & M32) << 32 | ((s.lo1 if p1 else s.lo) & M32)
                p += prev if uns else s64(prev)
            lo, hi = sx32(p), sx32(p >> 32)
            if p1: s.lo1, s.hi1 = lo, hi
            else: s.lo, s.hi = lo, hi
            if rd: r[rd] = lo
        return f

    def i_mult(s, pc, w, rs, rt, rd, sa, imm, simm): return s._mult(rs, rt, rd, False, False)
    def i_multu(s, pc, w, rs, rt, rd, sa, imm, simm): return s._mult(rs, rt, rd, True, False)
    def i_mult1(s, pc, w, rs, rt, rd, sa, imm, simm): return s._mult(rs, rt, rd, False, True)
    def i_multu1(s, pc, w, rs, rt, rd, sa, imm, simm): return s._mult(rs, rt, rd, True, True)
    def i_madd(s, pc, w, rs, rt, rd, sa, imm, simm): return s._mult(rs, rt, rd, False, False, 1)
    def i_maddu(s, pc, w, rs, rt, rd, sa, imm, simm): return s._mult(rs, rt, rd, True, False, 1)
    def i_madd1(s, pc, w, rs, rt, rd, sa, imm, simm): return s._mult(rs, rt, rd, False, True, 1)
    def i_maddu1(s, pc, w, rs, rt, rd, sa, imm, simm): return s._mult(rs, rt, rd, True, True, 1)

    def _div(s, rs, rt, uns, p1):
        r = s.r
        def f():
            if uns:
                a, b = r[rs] & M32, r[rt] & M32
                if b == 0: q, m = M32, a
                else: q, m = a // b, a % b
            else:
                a, b = s64(sx32(r[rs])), s64(sx32(r[rt]))
                if b == 0: q, m = (1 if a < 0 else -1), a
                elif a == -0x80000000 and b == -1: q, m = a, 0
                else:
                    q = abs(a) // abs(b)
                    if (a < 0) != (b < 0): q = -q
                    m = a - q * b
            if p1: s.lo1, s.hi1 = sx32(q), sx32(m)
            else: s.lo, s.hi = sx32(q), sx32(m)
        return f

    def i_div(s, pc, w, rs, rt, rd, sa, imm, simm): return s._div(rs, rt, False, False)
    def i_divu(s, pc, w, rs, rt, rd, sa, imm, simm): return s._div(rs, rt, True, False)
    def i_div1(s, pc, w, rs, rt, rd, sa, imm, simm): return s._div(rs, rt, False, True)
    def i_divu1(s, pc, w, rs, rt, rd, sa, imm, simm): return s._div(rs, rt, True, True)

    def i_mflo(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = s.lo & M64
        return s._w(rd, f)

    def i_mfhi(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = s.hi & M64
        return s._w(rd, f)

    def i_mflo1(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = s.lo1 & M64
        return s._w(rd, f)

    def i_mfhi1(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rd] = s.hi1 & M64
        return s._w(rd, f)

    def i_mtlo(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): s.lo = r[rs]
        return f

    def i_mthi(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): s.hi = r[rs]
        return f

    # ===== cargas / almacenes =====
    def i_lb(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; r8 = s.r8
        def f():
            v = r8(r[rs] + simm)
            r[rt] = (v | ~0xFF) & M64 if v & 0x80 else v
        return s._w(rt, f) if rt else s._lectura_muda(rs, simm, r8)

    def i_lbu(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; r8 = s.r8
        def f(): r[rt] = r8(r[rs] + simm)
        return s._w(rt, f) if rt else _nop

    def i_lh(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; r16 = s.r16
        def f():
            v = r16(r[rs] + simm)
            r[rt] = (v | ~0xFFFF) & M64 if v & 0x8000 else v
        return s._w(rt, f) if rt else _nop

    def i_lhu(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; r16 = s.r16
        def f(): r[rt] = r16(r[rs] + simm)
        return s._w(rt, f) if rt else _nop

    def i_lw(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; r32 = s.r32
        def f(): r[rt] = sx32(r32((r[rs] + simm) & M32))
        return s._w(rt, f) if rt else _nop

    def i_lwu(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; r32 = s.r32
        def f(): r[rt] = r32((r[rs] + simm) & M32)
        return s._w(rt, f) if rt else _nop

    def i_ld(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; r64 = s.r64
        def f(): r[rt] = r64((r[rs] + simm) & M32)
        return s._w(rt, f) if rt else _nop

    def i_lq(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up; r64 = s.r64
        def f():
            a = (r[rs] + simm) & 0xFFFFFFF0
            r[rt] = r64(a); up[rt] = r64(a + 8)
        return s._w(rt, f) if rt else _nop

    def _lectura_muda(s, rs, simm, rd):
        return _nop

    def i_sb(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; w8 = s.w8
        def f(): w8(r[rs] + simm, r[rt])
        return f

    def i_sh(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; w16 = s.w16
        def f(): w16(r[rs] + simm, r[rt])
        return f

    def i_sw(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; w32 = s.w32
        def f(): w32((r[rs] + simm) & M32, r[rt])
        return f

    def i_sd(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; w64 = s.w64
        def f(): w64((r[rs] + simm) & M32, r[rt])
        return f

    def i_sq(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up; w64 = s.w64
        def f():
            a = (r[rs] + simm) & 0xFFFFFFF0
            w64(a, r[rt]); w64(a + 8, up[rt])
        return f

    def i_lwl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            a = (r[rs] + simm) & M32; k = a & 3
            m = s.r32(a & ~3)
            sh = (3 - k) * 8
            keep = (1 << sh) - 1
            r[rt] = sx32(((r[rt] & M32) & keep) | ((m << sh) & M32))
        return s._w(rt, f)

    def i_lwr(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            a = (r[rs] + simm) & M32; k = a & 3
            m = s.r32(a & ~3)
            sh = k * 8
            keep = (M32 << (32 - sh)) & M32 if sh else 0
            v = ((r[rt] & M32) & keep) | (m >> sh)
            r[rt] = sx32(v) if k == 0 else (r[rt] & SX) | v
        return s._w(rt, f)

    def i_swl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            a = (r[rs] + simm) & M32; k = a & 3
            m = s.r32(a & ~3); v = r[rt] & M32
            sh = (3 - k) * 8
            keep = (M32 << (8 * (k + 1))) & M32 if k < 3 else 0
            s.w32(a & ~3, (m & keep) | (v >> sh))
        return f

    def i_swr(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            a = (r[rs] + simm) & M32; k = a & 3
            m = s.r32(a & ~3); v = r[rt] & M32
            sh = k * 8
            keep = (1 << sh) - 1
            s.w32(a & ~3, (m & keep) | ((v << sh) & M32))
        return f

    def i_ldl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            a = (r[rs] + simm) & M32; k = a & 7
            m = s.r64(a & ~7); sh = (7 - k) * 8
            keep = (1 << sh) - 1
            r[rt] = (r[rt] & keep) | ((m << sh) & M64)
        return s._w(rt, f)

    def i_ldr(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            a = (r[rs] + simm) & M32; k = a & 7
            m = s.r64(a & ~7); sh = k * 8
            keep = (M64 << (64 - sh)) & M64 if sh else 0
            r[rt] = (r[rt] & keep) | (m >> sh)
        return s._w(rt, f)

    def i_sdl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            a = (r[rs] + simm) & M32; k = a & 7
            m = s.r64(a & ~7); v = r[rt]
            keep = (M64 << (8 * (k + 1))) & M64 if k < 7 else 0
            s.w64(a & ~7, (m & keep) | (v >> ((7 - k) * 8)))
        return f

    def i_sdr(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            a = (r[rs] + simm) & M32; k = a & 7
            m = s.r64(a & ~7); v = r[rt]; sh = k * 8
            s.w64(a & ~7, (m & ((1 << sh) - 1)) | ((v << sh) & M64))
        return f

    # ===== saltos =====
    def _br(s, pc, simm, cond, likely=False, link=False):
        t = (pc + 4 + (simm << 2)) & M32
        r = s.r
        if likely:
            def f():
                if link: r[31] = pc + 8
                return t if cond() else SALTAR
        else:
            def f():
                if link: r[31] = pc + 8
                return t if cond() else None
        return f

    def i_beq(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] == r[rt])
    def i_bne(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] != r[rt])
    def i_beql(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] == r[rt], True)
    def i_bnel(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] != r[rt], True)
    def i_b(s, pc, w, rs, rt, rd, sa, imm, simm):
        return s._br(pc, simm, lambda: True)
    def i_beqz(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] == 0)
    def i_bnez(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] != 0)
    def i_beqzl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] == 0, True)
    def i_bnezl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] != 0, True)
    def i_blez(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: s64(r[rs]) <= 0)
    def i_bgtz(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: s64(r[rs]) > 0)
    def i_bltz(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] >> 63 == 1)
    def i_bgez(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] >> 63 == 0)
    def i_blezl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: s64(r[rs]) <= 0, True)
    def i_bgtzl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: s64(r[rs]) > 0, True)
    def i_bltzl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] >> 63 == 1, True)
    def i_bgezl(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] >> 63 == 0, True)
    def i_bltzal(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] >> 63 == 1, link=True)
    def i_bgezal(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; return s._br(pc, simm, lambda: r[rs] >> 63 == 0, link=True)
    def i_bal(s, pc, w, rs, rt, rd, sa, imm, simm):
        return s._br(pc, simm, lambda: True, link=True)

    def i_j(s, pc, w, rs, rt, rd, sa, imm, simm):
        t = ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
        if t in s.entradas or t in s.hooks:  # salto de cola a otra funcion: pasa por puentes
            t |= LLAMADA
        def f(): return t
        return f

    def i_jal(s, pc, w, rs, rt, rd, sa, imm, simm):
        t = (((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)) | LLAMADA
        r = s.r; ra = pc + 8
        def f():
            r[31] = ra
            return t
        return f

    def i_jr(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        if rs == 31:
            def f(): return r[31] & M32
        else:
            def f(): return r[rs] & M32
        return f

    def i_jalr(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; ra = pc + 8
        def f():
            t = r[rs] & M32
            if rd: r[rd] = ra
            return t | LLAMADA
        return f

    def i_syscall(s, pc, w, rs, rt, rd, sa, imm, simm):
        def f():
            n = s64(s.r[3])
            h = s.hooks.get(('syscall', n))
            if h is None:
                raise RuntimeError(f'syscall {n} en {pc:#x} sin puente')
            h(s)
        return f

    def i_break(s, pc, w, rs, rt, rd, sa, imm, simm):
        def f(): raise RuntimeError(f'break en {pc:#x}')
        return f

    def i_mfc0(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rt] = sx32(s.n & M32) if rd == 9 else 0  # Count = contador propio; resto 0
        return s._w(rt, f)

    def i_mtc0(s, *a): return _nop

    # ===== MMI (los que aparecen) =====
    def i_pcpyld(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up
        def f(): up[rd], r[rd] = r[rs], r[rt]
        return s._w(rd, f)

    def i_pcpyud(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up
        def f(): r[rd], up[rd] = up[rs], up[rt]
        return s._w(rd, f)

    def i_pcpyh(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up
        def f():
            a, b = r[rt] & 0xFFFF, up[rt] & 0xFFFF
            r[rd], up[rd] = a * 0x0001000100010001, b * 0x0001000100010001
        return s._w(rd, f)

    def i_por(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up
        def f(): r[rd], up[rd] = r[rs] | r[rt], up[rs] | up[rt]
        return s._w(rd, f)

    def i_pand(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up
        def f(): r[rd], up[rd] = r[rs] & r[rt], up[rs] & up[rt]
        return s._w(rd, f)

    def i_pxor(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up
        def f(): r[rd], up[rd] = r[rs] ^ r[rt], up[rs] ^ up[rt]
        return s._w(rd, f)

    def i_pnor(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up
        def f(): r[rd], up[rd] = ~(r[rs] | r[rt]) & M64, ~(up[rs] | up[rt]) & M64
        return s._w(rd, f)

    def i_pextlw(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up
        def f():
            a, b = r[rs], r[rt]
            r[rd], up[rd] = (b & M32) | ((a & M32) << 32), (b >> 32) | ((a >> 32) << 32)
        return s._w(rd, f)

    def i_plzcw(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def lz(x):
            x &= M32; b = x >> 31; n = 0
            for i in range(30, -1, -1):
                if (x >> i) & 1 != b: break
                n += 1
            return n
        def f(): r[rd] = lz(r[rs]) | (lz(r[rs] >> 32) << 32)
        return s._w(rd, f)

    # ===== COP1 (FPU) =====
    def i_mfc1(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; fr = s.f
        def f(): r[rt] = sx32(fr[rd])
        return s._w(rt, f)

    def i_mtc1(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; fr = s.f
        def f(): fr[rd] = r[rt] & M32
        return f

    def i_cfc1(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rt] = (0x01000001 | (0x800000 if s.c else 0) | (s.fcr if 'flags' in FPU_HW else 0)) if rd == 31 else 0x2E30
        return s._w(rt, f)

    def i_ctc1(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            if rd == 31:
                s.c = bool(r[rt] & 0x800000)
                s.fcr = r[rt] & 0x3C078  # dia1E6: causa + pegajosas (solo se leen con 'flags')
        return f

    def i_lwc1(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; fr = s.f; r32 = s.r32
        def f(): fr[rt] = r32((r[rs] + simm) & M32)
        return f

    def i_swc1(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; fr = s.f; w32 = s.w32
        def f(): w32((r[rs] + simm) & M32, fr[rt])
        return f

    def _h(s, op, a, b=0, c=0, guarda=False):  # dia1E6: COP1 por fpu_h (solo con FPU_HW no vacio)
        r = fpu_h(op, a, b, c, guarda)
        s.fcr |= _BAND[0]
        return r

    def _fop(s, w, op, nom=None):
        ft, fs, fd = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31
        fr = s.f
        def f(): fr[fd] = s._h(nom, fr[fs], fr[ft]) if FPU_HW else op(fv(fr[fs]), fv(fr[ft]))
        return f

    def _suma(s, w, signo):  # add.s/sub.s; con FPU_SUMA_PS2 el operando menor pierde bits como en PCSX2 (FPU_ADD_SUB)
        ft, fs, fd = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31
        fr = s.f

        def f():
            a, b = fr[fs], fr[ft]
            if FPU_HW:
                fr[fd] = s._h('add' if signo > 0 else 'sub', a, b, guarda=FPU_SUMA_PS2)
                return
            if FPU_SUMA_PS2:
                a, b = suma_ps2(a, b)
            fr[fd] = f_add(fv(a), signo * fv(b))
        return f

    def i_add_s(s, pc, w, *a): return s._suma(w, 1.0)
    def i_sub_s(s, pc, w, *a): return s._suma(w, -1.0)
    def i_mul_s(s, pc, w, *a): return s._fop(w, f_mul, 'mul')
    def i_div_s(s, pc, w, *a): return s._fop(w, f_div, 'div')

    def i_sqrt_s(s, pc, w, *a):
        ft, fd = (w >> 16) & 31, (w >> 6) & 31; fr = s.f
        def f(): fr[fd] = s._h('sqrt', fr[ft]) if FPU_HW else f_sqrt(fv(fr[ft]))
        return f

    def i_c1(s, pc, w, *a):  # rabbitizer no nombra algunos COP1.S del R5900 (p. ej. sqrt.s f0,f0 = 0x46000004)
        if (w >> 21) & 31 == 16 and w & 63 == 4:
            return s.i_sqrt_s(pc, w)
        raise NotImplementedError(f'{pc:#x}: COP1 {w:#010x}')

    def i_rsqrt_s(s, pc, w, *a):
        ft, fs, fd = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31; fr = s.f
        def f():
            if FPU_HW:
                fr[fd] = s._h('rsqrt', fr[fs], fr[ft])
                return
            q = fv(f_sqrt(fv(fr[ft])))
            fr[fd] = f_div(fv(fr[fs]), q)
        return f

    def i_abs_s(s, pc, w, *a):
        fs, fd = (w >> 11) & 31, (w >> 6) & 31; fr = s.f
        def f(): fr[fd] = fr[fs] & 0x7FFFFFFF
        return f

    def i_neg_s(s, pc, w, *a):
        fs, fd = (w >> 11) & 31, (w >> 6) & 31; fr = s.f
        def f(): fr[fd] = fr[fs] ^ 0x80000000
        return f

    def i_mov_s(s, pc, w, *a):
        fs, fd = (w >> 11) & 31, (w >> 6) & 31; fr = s.f
        def f(): fr[fd] = fr[fs]
        return f

    def i_max_s(s, pc, w, *a): return s._fop(w, lambda a, b: fb(max(a, b)), 'max')
    def i_min_s(s, pc, w, *a): return s._fop(w, lambda a, b: fb(min(a, b)), 'min')

    def i_adda_s(s, pc, w, *a):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31; fr = s.f
        def f(): s.acc = s._h('adda', fr[fs], fr[ft]) if FPU_HW else f_add(fv(fr[fs]), fv(fr[ft]))
        return f

    def i_suba_s(s, pc, w, *a):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31; fr = s.f
        def f(): s.acc = s._h('suba', fr[fs], fr[ft]) if FPU_HW else f_add(fv(fr[fs]), -fv(fr[ft]))
        return f

    def i_mula_s(s, pc, w, *a):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31; fr = s.f
        def f(): s.acc = s._h('mula', fr[fs], fr[ft]) if FPU_HW else f_mul(fv(fr[fs]), fv(fr[ft]))
        return f

    def i_madd_s(s, pc, w, *a):
        ft, fs, fd = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31; fr = s.f
        def f(): fr[fd] = s._h('madd', fr[fs], fr[ft], s.acc) if FPU_HW else f_add(fv(s.acc), fv(f_mul(fv(fr[fs]), fv(fr[ft]))))
        return f

    def i_msub_s(s, pc, w, *a):
        ft, fs, fd = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31; fr = s.f
        def f(): fr[fd] = s._h('msub', fr[fs], fr[ft], s.acc) if FPU_HW else f_add(fv(s.acc), -fv(f_mul(fv(fr[fs]), fv(fr[ft]))))
        return f

    def i_madda_s(s, pc, w, *a):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31; fr = s.f
        def f(): s.acc = s._h('madda', fr[fs], fr[ft], s.acc) if FPU_HW else f_add(fv(s.acc), fv(f_mul(fv(fr[fs]), fv(fr[ft]))))
        return f

    def i_msuba_s(s, pc, w, *a):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31; fr = s.f
        def f(): s.acc = s._h('msuba', fr[fs], fr[ft], s.acc) if FPU_HW else f_add(fv(s.acc), -fv(f_mul(fv(fr[fs]), fv(fr[ft]))))
        return f

    def i_cvt_s_w(s, pc, w, *a):
        fs, fd = (w >> 11) & 31, (w >> 6) & 31; fr = s.f
        def f():
            v = fr[fs]
            fr[fd] = fb(float(v - (1 << 32) if v & 0x80000000 else v))
        return f

    def i_cvt_w_s(s, pc, w, *a):
        fs, fd = (w >> 11) & 31, (w >> 6) & 31; fr = s.f
        def f():
            b = fr[fs]
            if (b & 0x7F800000) >= 0x4F000000:  # |x| >= 2^31: satura
                fr[fd] = 0x80000000 if b >> 31 else 0x7FFFFFFF
            else:
                fr[fd] = int(fv(b)) & M32
        return f

    def _cmp(s, w, op):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31; fr = s.f
        def f(): s.c = op(fv(fr[fs]), fv(fr[ft]))
        return f

    def i_c_f_s(s, pc, w, *a): return s._cmp(w, lambda a, b: False)
    def i_c_eq_s(s, pc, w, *a): return s._cmp(w, lambda a, b: a == b)
    def i_c_lt_s(s, pc, w, *a): return s._cmp(w, lambda a, b: a < b)
    def i_c_le_s(s, pc, w, *a): return s._cmp(w, lambda a, b: a <= b)

    def i_bc1t(s, pc, w, rs, rt, rd, sa, imm, simm): return s._br(pc, simm, lambda: s.c)
    def i_bc1f(s, pc, w, rs, rt, rd, sa, imm, simm): return s._br(pc, simm, lambda: not s.c)
    def i_bc1tl(s, pc, w, rs, rt, rd, sa, imm, simm): return s._br(pc, simm, lambda: s.c, True)
    def i_bc1fl(s, pc, w, rs, rt, rd, sa, imm, simm): return s._br(pc, simm, lambda: not s.c, True)

    # ===== COP2 (VU0 modo macro), subconjunto =====
    def i_lqc2(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; vf = s.vf; r32 = s.r32
        def f():
            a = (r[rs] + simm) & 0xFFFFFFF0
            if rt: vf[rt] = [r32(a), r32(a + 4), r32(a + 8), r32(a + 12)]
        return f

    def i_sqc2(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; vf = s.vf; w32 = s.w32
        def f():
            a = (r[rs] + simm) & 0xFFFFFFF0
            v = vf[rt]
            w32(a, v[0]); w32(a + 4, v[1]); w32(a + 8, v[2]); w32(a + 12, v[3])
        return f

    def i_qmfc2(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up; vf = s.vf
        def f():
            v = vf[rd]
            r[rt], up[rt] = v[0] | (v[1] << 32), v[2] | (v[3] << 32)
        return s._w(rt, f)

    def i_qmtc2(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r; up = s.up; vf = s.vf
        def f():
            if rd: vf[rd] = [r[rt] & M32, r[rt] >> 32, up[rt] & M32, up[rt] >> 32]
        return f

    def i_cfc2(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f(): r[rt] = sx32(s.vi[rd]) if rd < 16 else (s.q if rd == 22 else 0)
        return s._w(rt, f)

    def i_ctc2(s, pc, w, rs, rt, rd, sa, imm, simm):
        r = s.r
        def f():
            if 0 < rd < 16: s.vi[rd] = r[rt] & 0xFFFF
        return f

    def _vdest(w):
        return [i for i, b in enumerate((24, 23, 22, 21)) if (w >> b) & 1]

    def _vop(s, w, op, bc=None, src=None, acc=False, acc_in=False):
        """op(a, b) sobre floats; bc = campo de difusion; src='q'|'i'; acc: destino ACC; acc_in: ACC + op."""
        ft, fs, fd = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31
        dest = Ee._vdest(w); vf = s.vf
        def f():
            a = vf[fs]
            if src == 'q': b = [s.q] * 4
            elif bc is not None: b = [vf[ft][bc]] * 4
            else: b = vf[ft]
            out = list(s.vacc) if acc else list(vf[fd])
            for i in dest:
                x = op(fv(a[i]), fv(b[i]))
                if acc_in:
                    x = acc_in(fv(s.vacc[i]), fv(x))
                out[i] = x
            if acc: s.vacc = out
            elif fd: vf[fd] = out
        return f

    def __getattr__(s, nom):
        # familia vadd/vsub/vmul/vmadd/vmsub[a][x|y|z|w|q|i]
        if not nom.startswith('i_v'):
            raise AttributeError(nom)
        base = nom[2:]
        def R(k, sub=False):  # redondeo por familia (VU_CERCANO[k]); se mira en cada ejecucion
            if sub:
                return lambda a, b: f_add(a, -b, VU_CERCANO.get(k, False))
            return lambda a, b: (f_mul if k.endswith('mul') else f_add)(a, b, VU_CERCANO.get(k, False))
        ops = {'add': None, 'sub': None, 'mul': None}
        for pre, accin in (('madd', lambda c, x: f_add(c, x, VU_CERCANO.get('madd', False))),
                           ('msub', lambda c, x: f_add(c, -x, VU_CERCANO.get('madd', False)))):
            if base.startswith('v' + pre):
                rest = base[len(pre) + 1:]
                acc = rest.startswith('a'); rest = rest[1:] if acc else rest
                bc = 'xyzw'.index(rest) if rest in ('x', 'y', 'z', 'w') else None
                src = 'q' if rest == 'q' else None
                if rest not in ('', 'x', 'y', 'z', 'w', 'q'):
                    raise AttributeError(nom)
                return lambda pc, w, *a: s._vop(w, R('maddmul'), bc, src, acc, accin)
        for k in ops:
            if base.startswith('v' + k):
                rest = base[len(k) + 1:]
                acc = rest.startswith('a'); rest = rest[1:] if acc else rest
                if rest not in ('', 'x', 'y', 'z', 'w', 'q'):
                    raise AttributeError(nom)
                bc = 'xyzw'.index(rest) if rest in ('x', 'y', 'z', 'w') else None
                src = 'q' if rest == 'q' else None
                cat = k + ('a' if acc else '') + ('bc' if bc is not None or src else '')
                op = R(cat, sub=(k == 'sub')) if k != 'mul' else R(cat + 'mul' if not cat.endswith('mul') else cat)
                return lambda pc, w, *a: s._vop(w, op, bc, src, acc)
        raise AttributeError(nom)

    def i_vnop(s, *a): return _nop
    def i_vwaitq(s, *a): return _nop

    def i_vmove(s, pc, w, *a):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31; dest = Ee._vdest(w); vf = s.vf
        def f():
            if ft:
                o = list(vf[ft])
                for i in dest: o[i] = vf[fs][i]
                vf[ft] = o
        return f

    def i_vmr32(s, pc, w, *a):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31; dest = Ee._vdest(w); vf = s.vf
        def f():
            if ft:
                src = vf[fs]; rot = [src[1], src[2], src[3], src[0]]
                o = list(vf[ft])
                for i in dest: o[i] = rot[i]
                vf[ft] = o
        return f

    def i_vabs(s, pc, w, *a):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31; dest = Ee._vdest(w); vf = s.vf
        def f():
            if ft:
                o = list(vf[ft])
                for i in dest: o[i] = vf[fs][i] & 0x7FFFFFFF
                vf[ft] = o
        return f

    def i_vopmula(s, pc, w, *a):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31; vf = s.vf
        def f():
            A, B = vf[fs], vf[ft]
            s.vacc = [v_mul(fv(A[1]), fv(B[2])), v_mul(fv(A[2]), fv(B[0])), v_mul(fv(A[0]), fv(B[1])), s.vacc[3]]
        return f

    def i_vopmsub(s, pc, w, *a):
        ft, fs, fd = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31; vf = s.vf
        def f():
            A, B, C = vf[fs], vf[ft], s.vacc
            o = list(vf[fd])
            for i, (j, k) in enumerate(((1, 2), (2, 0), (0, 1))):  # fd = ACC - (fs.yzx * ft.zxy)
                o[i] = v_add(fv(C[i]), -fv(v_mul(fv(A[j]), fv(B[k]))))
            if fd: vf[fd] = o
        return f

    def _vq(s, w, op):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31
        fsf, ftf = (w >> 21) & 3, (w >> 23) & 3; vf = s.vf
        def f(): s.q = op(fv(vf[fs][fsf]), fv(vf[ft][ftf]))
        return f

    def i_vdiv(s, pc, w, *a): return s._vq(w, v_div)
    def i_vsqrt(s, pc, w, *a): return s._vq(w, lambda a, b: v_sqrt(b))
    def i_vrsqrt(s, pc, w, *a):
        def op(a, b):
            if VU_CERCANO.get('rsqrt1') and b:  # un solo redondeo (como x87/double)
                return fb(a / math.sqrt(abs(b)), 0.0, VU_CERCANO.get('div', False))
            return v_div(a, fv(v_sqrt(b)))
        return s._vq(w, op)

    def _vcvt(s, w, op):
        ft, fs = (w >> 16) & 31, (w >> 11) & 31; dest = Ee._vdest(w); vf = s.vf
        def f():
            if ft:
                o = list(vf[ft])
                for i in dest: o[i] = op(vf[fs][i])
                vf[ft] = o
        return f

    def i_vftoi0(s, pc, w, *a): return s._vcvt(w, lambda b: _ftoi(b, 0))
    def i_vftoi4(s, pc, w, *a): return s._vcvt(w, lambda b: _ftoi(b, 4))
    def i_vftoi12(s, pc, w, *a): return s._vcvt(w, lambda b: _ftoi(b, 12))
    def i_vftoi15(s, pc, w, *a): return s._vcvt(w, lambda b: _ftoi(b, 15))
    def i_vitof0(s, pc, w, *a): return s._vcvt(w, lambda b: _itof(b, 0))
    def i_vitof4(s, pc, w, *a): return s._vcvt(w, lambda b: _itof(b, 4))
    def i_vitof12(s, pc, w, *a): return s._vcvt(w, lambda b: _itof(b, 12))
    def i_vitof15(s, pc, w, *a): return s._vcvt(w, lambda b: _itof(b, 15))


def _ftoi(b, k):
    x = math.ldexp(fv(b), k)
    if x >= 2147483647: return 0x7FFFFFFF
    if x <= -2147483648: return 0x80000000
    return int(x) & M32


def _itof(b, k):
    v = b - (1 << 32) if b & 0x80000000 else b
    return fb(math.ldexp(float(v), -k))


def _nop():
    return None


# ---------------- autocomprobacion ----------------
def _asm(mem, base, palabras):
    for i, x in enumerate(palabras):
        U32.pack_into(mem, base + 4 * i, x)


def demo():
    mem = bytearray(TOTAL)
    # LFSR igual que Memory: v0 = semilla; bucle a0 veces; guarda y devuelve v0-1 (mismo patron que el ELF)
    gp = 0x10000
    prog = [0x8F828018, 0x08000409, 0x2084FFFF, 0x00021FC2, 0x30210001, 0x00021040, 0x00611826, 0x2084FFFF,
            0x00431025, 0x0481FFF9, 0x00020902, 0xAF828018, 0x2442FFFF, 0x03E00008, 0x00000000]
    _asm(mem, 0x1000, prog)
    U32.pack_into(mem, gp - 0x7FE8, 3635744825)
    cpu = Ee(mem, gp)
    st = 3635744825
    for n in (1, 32, 255, 7):
        v = cpu.call(0x1000, n) & M32
        for _ in range(n):
            st = ((st << 1) | (((st >> 31) ^ (st >> 4)) & 1)) & M32
        assert v == (st - 1) & M32, (n, v, st)
    # FPU: hacia cero y recorte
    assert fb(1 / 3) == 0x3EAAAAAA                    # nearest daria ...AB
    assert f_add(1.0, -2.0 ** -40) == 0x3F7FFFFF      # 1 - epsilon trunca hacia abajo
    assert f_mul(fv(0x7F7FFFFF), 2.0) == 0x7F7FFFFF    # sin inf
    assert fv(0x7F800000) == 2.0 ** 128                # exponente 255 = numero
    assert fb(1e-40) == 0                              # denormal a cero
    assert f_div(1.0, 3.0) == 0x3EAAAAAA and f_sqrt(2.0) == 0x3FB504F3
    print('ee.py autocomprobacion OK')


if __name__ == '__main__':
    demo()
