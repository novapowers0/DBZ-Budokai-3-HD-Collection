# SPDX-License-Identifier: MIT
"""dia1E6: la FPU del interprete (ee.py y src/iw_mips.cpp) frente a los vectores de HARDWARE de ps2autotests.

Los vectores (tests/cpu/ee_fpu/*.expected de unknownbrackets/ps2autotests, licencia ISC, solo datos) estan
ya leidos en fpu_hw_vectores.json (con su aviso de licencia). Cada vector se ejecuta como el programa de la
prueba: mismas instrucciones (ACC con adda.s x,-0 y leido con madd.s -0,+0) por el camino real de ee.py
(los cierres i_*_s) y por el bloque FPU de iw_mips.cpp compilado aparte (clang++).

  python tools/iw_mips/fpu_hw.py --extraer DIR      # .expected -> fpu_hw_vectores.json
  python tools/iw_mips/fpu_hw.py --clasificar [--hw div,ovf] [--cpp]
  python tools/iw_mips/fpu_hw.py --comprobar        # autocomprobacion (Python y C++)
"""
import json
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

AQUI = Path(__file__).resolve().parent
RAIZ = AQUI.parents[1]
sys.path.insert(0, str(AQUI))
import ee  # noqa: E402

JSON = AQUI / 'fpu_hw_vectores.json'
CF = {'CF_ZERO': 0, 'CF_NEGZERO': 0x80000000, 'CF_MAX': 0x7FFFFFFF, 'CF_MIN': 0xFFFFFFFF,
      'CF_MAX_MANTISSA': 0x3FFFFFFF, 'CF_MAX_EXP': 0x7F800001, 'CF_MIN_EXP': 0x00000001,
      'CF_NEGONE': 0xBF800000, 'CF_ONE': 0x3F800000, 'CF_GARBAGE1': 0x00001337, 'CF_GARBAGE2': 0xDEADBEEF}
FN = {'add': 0, 'sub': 1, 'mul': 2, 'div': 3, 'sqrt': 4, 'abs': 5, 'mov': 6, 'neg': 7, 'rsqrt': 0x16,
      'adda': 0x18, 'suba': 0x19, 'mula': 0x1A, 'madd': 0x1C, 'msub': 0x1D, 'madda': 0x1E, 'msuba': 0x1F,
      'cvt.w.s': 0x24, 'max': 0x28, 'min': 0x29, 'f': 0x30, 'eq': 0x32, 'lt': 0x34, 'le': 0x36}
NOMBRE = {'cvt.w.s': 'cvt_w_s', 'cvt.s.w': 'cvt_s_w', 'f': 'c_f_s', 'eq': 'c_eq_s', 'lt': 'c_lt_s', 'le': 'c_le_s'}
ACC_ESCRIBE = {'adda', 'suba', 'mula', 'madda', 'msuba'}
FLAGS = {'sqrt(-1)': ('sqrt', 0xBF800000, 0xBF800000),'Divide zero by zero': ('div', 0, 0),
         'Divide one by zero': ('div', 0x3F800000, 0), 'NAN math': ('add', 0x7F800001, 0x7F800001),
         'Overflow': ('mul', 0x7F7FFFFF, 0x7F7FFFFF), 'Underflow': ('div', 0x00800000, 0x40400000),
         'Inexact': ('div', 0x3F800000, 0x40400000)}  # fcr.cpp: una operacion con FCR31 limpio, se lee FCR31
LINEA = re.compile(r'^\s+(\S+) (?:\((\S+?)\) )?(\S+?)(?:, (\S+?))?: (\S+?)(?: \((\S+)\))?$')


def _val(t):
    return CF[t] if t in CF else int(t.split('/')[0], 16)


def extraer(d):
    d = Path(d)
    out = []
    for f in ('arithmetic', 'compare', 'convert', 'muldiv', 'sqrt'):
        for x in (d / f'{f}.expected').read_text().splitlines():
            m = LINEA.match(x)
            if not m:
                continue
            op, acc, a, b, r, acc2 = m.groups()
            v = {'op': op, 'a': _val(a), 'r': (r == 'true') if op in ('eq', 'f', 'le', 'lt') else _val(r)}
            if b is not None: v['b'] = _val(b)
            if acc is not None: v['acc'] = _val(acc)
            if acc2 is not None: v['acc2'] = _val(acc2)
            out.append(v)
    txt = (d / 'fcr.expected').read_text().splitlines()
    for i, x in enumerate(txt):
        if x.rstrip(':') in FLAGS:
            op, a, b = FLAGS[x.rstrip(':')]
            out.append({'op': 'fcr31.' + op, 'a': a, 'b': b, 'r': int(txt[i + 1].split('fcr31: ')[1], 16),
                        'caso': x.rstrip(':')})
    lic = (d / 'LICENSE').read_text().strip()
    JSON.write_text(json.dumps({'_fuente': 'github.com/unknownbrackets/ps2autotests tests/cpu/ee_fpu/*.expected '
                                '(resultados de una PS2 real), solo datos', '_licencia': lic, 'vectores': out},
                               indent=0), encoding='utf-8')
    return out


def cargar():
    return json.loads(JSON.read_text(encoding='utf-8'))['vectores']


# ---------------- Python: el camino real de ee.py ----------------
def _w(op, fs=1, ft=2, fd=3):
    if op == 'cvt.s.w':
        return 0x44000000 | (20 << 21) | (fs << 11) | (fd << 6) | 0x20
    return 0x44000000 | (16 << 21) | (ft << 16) | (fs << 11) | (fd << 6) | FN[op]


def _paso(cpu, op, fd=3, fs=1, ft=2):
    getattr(cpu, 'i_' + NOMBRE.get(op, op + '_s'))(0, _w(op, fs, ft, fd), 0, 0, 0, 0, 0, 0)()


def py_uno(v):
    """Devuelve (resultado, acc2) como lo imprime la prueba."""
    cpu = ee.Ee(bytearray(16), 0)
    op = v['op']
    f = cpu.f
    if op.startswith('fcr31.'):
        f[1], f[2] = v['a'], v['b']
        _paso(cpu, op[6:])
        cpu.i_cfc1(0, 0x4448F800, 0, 8, 31, 0, 0, 0)()  # cfc1 $t0, $31
        return cpu.r[8] & ee.M32, None
    f[1], f[2] = v['a'], v.get('b', v['a'])
    if 'acc' in v or op in ACC_ESCRIBE:
        f[4], f[5], f[6] = v.get('acc', 0), 0x80000000, 0
        _paso(cpu, 'adda', fs=4, ft=5)          # SET_ACC: adda.s $f4, -0

    def leer_acc():                             # PRINT_ACC: madd.s $f4, -0, +0
        _paso(cpu, 'madd', fd=4, fs=5, ft=6)
        return f[4]
    f[5], f[6] = 0x80000000, 0
    _paso(cpu, op)
    if op in ('eq', 'f', 'le', 'lt'):
        return cpu.c, None
    if op in ACC_ESCRIBE:
        return leer_acc(), None
    r = f[3]
    return r, (leer_acc() if 'acc2' in v else None)


def correr_py(vs, hw):
    ee.FPU_HW = set(hw)
    try:
        return [py_uno(v) for v in vs]
    finally:
        ee.FPU_HW = set(ee.FPU_HW_ENTORNO)


# ---------------- C++: el bloque FPU de src/iw_mips.cpp, compilado aparte ----------------
CPP_DIR = Path(r'E:/DBZ3HD_trabajo/iw_mips/fpu_ps2/cpp')
CLANG = Path(r'C:/Program Files/LLVM/bin/clang++.exe')
DRIVER = r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
namespace t {
#include "bloque.inl"
}
using namespace t;
int main() {
  char op[16]; unsigned hw, a, b, acc, tiene_acc, tiene_acc2;
  while (std::scanf("%15s %x %x %x %x %u %u", op, &hw, &a, &b, &acc, &tiene_acc, &tiene_acc2) == 7) {
    uint32_t A = 0, fcr = 0; bool c = false;
    auto acc_desde = [&](uint32_t x) { A = Fpu(kAdd, x, 0x80000000u, 0, hw, false, fcr); };
    auto leer_acc = [&]() { return Fpu(kMadd, 0x80000000u, 0, A, hw, false, fcr); };
    if (tiene_acc) acc_desde(acc);
    const char* o = op;
    if (!std::strncmp(o, "fcr31.", 6)) { o += 6; fcr = 0; }
    unsigned long long r = 0, r2 = 0xFFFFFFFFFFull; bool flags = o != op;
    #define X(nom, k) if (!std::strcmp(o, nom)) r = Fpu(k, a, b, A, hw, false, fcr);
    #define XA(nom, k) if (!std::strcmp(o, nom)) { A = Fpu(k, a, b, A, hw, false, fcr); r = leer_acc(); }
    X("add", kAdd) X("sub", kSub) X("mul", kMul) X("div", kDiv) X("sqrt", kSqrt) X("rsqrt", kRsqrt)
    X("max", kMax) X("min", kMin) X("cvt.w.s", kCvtWS) X("cvt.s.w", kCvtSW) X("abs", kAbs) X("neg", kNeg) X("mov", kMov)
    if (!std::strcmp(o, "madd") || !std::strcmp(o, "msub")) {
      r = Fpu(o[1] == 'a' ? kMadd : kMsub, a, b, A, hw, false, fcr);
      if (tiene_acc2) r2 = leer_acc();
    }
    XA("adda", kAdd) XA("suba", kSub) XA("mula", kMul) XA("madda", kMadd) XA("msuba", kMsub)
    if (!std::strcmp(o, "eq")) r = FpuCmp(kCeq, a, b, hw); if (!std::strcmp(o, "lt")) r = FpuCmp(kClt, a, b, hw);
    if (!std::strcmp(o, "le")) r = FpuCmp(kCle, a, b, hw); if (!std::strcmp(o, "f")) r = 0;
    if (flags) r = 0x01000001u | ((hw & kHwFlags) ? fcr : 0);
    std::printf("%llx %llx\n", r, r2);
    (void)c;
  }
}
'''


def bloque_cpp():
    t = (RAIZ / 'src' / 'iw_mips.cpp').read_text(encoding='utf-8')
    i, j = t.index('// ---- FPU/VU del R5900'), t.index('// ---- fin FPU ----')
    return t[i:j]


def compilar_cpp():
    CPP_DIR.mkdir(parents=True, exist_ok=True)
    (CPP_DIR / 'bloque.inl').write_text('constexpr uint64_t M32 = 0xFFFFFFFFull;\nconstexpr double MAXF = '
                                        '3.4028234663852886e38, MINF = 1.1754943508222875e-38;\n' + bloque_cpp(),
                                        encoding='utf-8')
    (CPP_DIR / 'fpu_hw_test.cpp').write_text(DRIVER, encoding='utf-8')
    exe = CPP_DIR / 'fpu_hw_test.exe'
    subprocess.run([str(CLANG), '-std=c++20', '-O2', '-ffp-contract=off', '-Wno-everything',
                    str(CPP_DIR / 'fpu_hw_test.cpp'), '-o', str(exe)], check=True)
    return exe


def correr_cpp(vs, hw, exe):
    mask = sum(1 << ee.FPU_HW_NOMBRES.index(h) for h in hw)
    entrada = ''.join(f"{v['op']} {mask:x} {v['a']:x} {v.get('b', v['a']):x} {v.get('acc', 0):x} "
                      f"{int('acc' in v or v['op'] in ACC_ESCRIBE)} {int('acc2' in v)}\n" for v in vs)
    sal = subprocess.run([str(exe)], input=entrada, capture_output=True, text=True, check=True).stdout.split('\n')
    res = []
    for v, x in zip(vs, sal):
        r, r2 = (int(y, 16) for y in x.split())
        if v['op'] in ('eq', 'f', 'le', 'lt'):
            r = bool(r)
        res.append((r, None if r2 == 0xFFFFFFFFFF else r2))
    return res


# ---------------- clasificacion ----------------
def fallos(vs, res):
    out = []
    for v, (r, r2) in zip(vs, res):
        if r != v['r'] or ('acc2' in v and r2 != v['acc2']):
            out.append((v, r, r2))
    return out


def fmt(v, r, r2):
    a = f"{v['op']} " + (f"({v['acc']:08x}) " if 'acc' in v else '') + f"{v['a']:08x}" + \
        (f", {v['b']:08x}" if 'b' in v else '')
    def h(x): return str(x) if isinstance(x, bool) else f'{x:08x}'
    return f"{a}: hw {h(v['r'])}" + (f" ({v['acc2']:08x})" if 'acc2' in v else '') + \
        f" / nuestro {h(r)}" + (f" ({r2:08x})" if r2 is not None else '')


def clasificar(vs, res):
    por = defaultdict(list)
    for v, r, r2 in fallos(vs, res):
        por[v['op']].append(fmt(v, r, r2))
    tot = Counter(v['op'] for v in vs)
    return tot, por


def contar(argv):
    """--contar script.py args...: ejecuta un dorado con la semantica de serie (por fpu_h sin reglas) y cuenta, por
    operacion COP1, cuantas se ejecutan y en cuantas cambiaria el resultado cada regla sola (y lecturas de FCR31)."""
    import runpy
    cnt, dif, ctl = Counter(), Counter(), Counter()
    orig = ee.fpu_h
    dentro = [False]

    def envuelta(op, a, b=0, c=0, guarda=False):
        if dentro[0]:  # llamada anidada de fpu_h (madd sin regla -> mul, rsqrt -> sqrt): no es una instruccion
            return orig(op, a, b, c, guarda)
        dentro[0] = True
        try:
            return _envuelta(op, a, b, c, guarda)
        finally:
            dentro[0] = False

    def _envuelta(op, a, b, c, guarda):
        H = ee.FPU_HW
        r = orig(op, a, b, c, guarda)
        band = ee._BAND[0]
        cnt[op] += 1
        for h in ee.FPU_HW_NOMBRES:
            ee.FPU_HW = {h}
            if orig(op, a, b, c, guarda) != r or (h == 'flags' and ee._BAND[0]):
                dif[f'{h}:{op}'] += 1
        ee.FPU_HW, ee._BAND[0] = H, band
        return r

    for nom in ('i_cfc1', 'i_ctc1'):
        m = getattr(ee.Ee, nom)

        def w(s, pc, wd, rs, rt, rd, *x, _m=m, _n=nom):
            f = _m(s, pc, wd, rs, rt, rd, *x)

            def g():
                ctl[f'{_n}${rd}'] += 1
                return f()
            return g
        setattr(ee.Ee, nom, w)
    ee.fpu_h = envuelta
    ee.FPU_HW = {'contar'}  # ninguna regla: fpu_h da la semantica de serie (comprobado en los 908 vectores)
    sys.argv = argv
    rc = 0
    try:
        runpy.run_path(argv[0], run_name='__main__')
    except SystemExit as e:
        rc = e.code or 0
    print('FPU_CUENTA', json.dumps({'rc': rc, 'ops': cnt, 'difiere': dif, 'fcr': ctl}, sort_keys=True))
    return rc


def main(argv):
    if '--contar' in argv:
        return contar(argv[argv.index('--contar') + 1:])
    if '--extraer' in argv:
        vs = extraer(argv[argv.index('--extraer') + 1])
        print(len(vs), 'vectores ->', JSON)
        return 0
    vs = cargar()
    hw = []
    if '--hw' in argv:
        hw = [h.replace('fpu_hw_', '') for h in argv[argv.index('--hw') + 1].split(',') if h]
    if '--comprobar' in argv:
        return comprobar(vs)
    if '--tabla' in argv:
        tabla(vs)
        return 0
    res = correr_py(vs, hw)
    if '--cpp' in argv:
        rc = correr_cpp(vs, hw, compilar_cpp())
        dif = [(v, a, b) for v, a, b in zip(vs, res, rc) if a != b]
        print(f'C++ frente a ee.py: {len(vs) - len(dif)}/{len(vs)} iguales')
        for v, a, b in dif[:10]:
            print('  ', v, a, b)
    tot, por = clasificar(vs, res)
    nf = sum(len(x) for x in por.values())
    print(f'hw={",".join(hw) or "-"}: {len(vs) - nf}/{len(vs)} iguales al hardware, {nf} distintos')
    for op in tot:
        if por[op]:
            print(f'  {op}: {len(por[op])}/{tot[op]} distintos')
            for x in por[op][:int(os.environ.get('EJEMPLOS', '4'))]:
                print('     ', x)
    return 0


def tabla(vs, todas=ee.FPU_HW_NOMBRES):
    """Por regla: vectores que fallan sin ella con todas las demas puestas (lo que esa regla arregla)."""
    base = fallos(vs, correr_py(vs, ()))
    tot = fallos(vs, correr_py(vs, todas))
    print(f'{len(vs)} vectores. De serie (PCSX2): {len(vs) - len(base)} iguales, {len(base)} distintos '
          f'{dict(Counter(v["op"] for v, *_ in base))}')
    print(f'Con las {len(todas)} reglas: {len(vs) - len(tot)} iguales, {len(tot)} distintos')
    out = {}
    for h in todas:
        f = fallos(vs, correr_py(vs, [x for x in todas if x != h]))
        c = Counter(v['op'] for v, *_ in f) - Counter(v['op'] for v, *_ in tot)
        sola = len(base) - len(fallos(vs, correr_py(vs, [h])))
        out[h] = (sum(c.values()), sola, dict(c), [fmt(*x) for x in f if x[0]['op'] in c][:2])
        print(f'  fpu_hw_{h}: hacen falta en {sum(c.values())} vectores {dict(c)}; sola arregla {sola}')
        for x in out[h][3]:
            print('      ', x)
    return base, tot, out


def comprobar(vs):
    """Autocomprobacion: (1) las reglas juntas = hardware en todos los vectores; (2) de serie = las cuentas medidas
    el 6/11 (si cambian, ha cambiado la FPU de serie); (3) 'flags' sola no toca ningun resultado; (4) el bloque FPU
    de iw_mips.cpp da lo mismo que ee.py bit a bit, de serie y con cada regla."""
    todas = ee.FPU_HW_NOMBRES
    m = re.search(r'constexpr uint32_t kFpuHwSerie = ([^;]+);', bloque_cpp())
    serie_cpp = {h for h in todas if f'kHw{h.capitalize()}' in m.group(1).replace('MaxMin', 'Maxmin')}
    assert serie_cpp == set(ee.FPU_HW_SERIE), ('de serie distinto en C++ y ee.py', serie_cpp, ee.FPU_HW_SERIE)
    base = fallos(vs, correr_py(vs, ()))
    assert len(vs) == 908, len(vs)
    assert len(base) == 271, len(base)
    assert not fallos(vs, correr_py(vs, todas)), [fmt(*x) for x in fallos(vs, correr_py(vs, todas))]
    a, b = correr_py(vs, ()), correr_py(vs, ['flags'])
    assert all(x == y for v, x, y in zip(vs, a, b) if not v['op'].startswith('fcr31')), 'flags cambia resultados'
    exe = compilar_cpp()
    for hw in [(), todas] + [[h] for h in todas]:
        p, c = correr_py(vs, hw), correr_cpp(vs, hw, exe)
        d = [(v['op'], x, y) for v, x, y in zip(vs, p, c) if x != y]
        assert not d, (hw, len(d), d[:5])
    serie = fallos(vs, correr_py(vs, ee.FPU_HW_SERIE))
    print(f'OK: {len(vs)} vectores de hardware; sin reglas (PCSX2) {len(vs) - len(base)} iguales y {len(base)} distintos; '
          f'de serie ({",".join(sorted(ee.FPU_HW_SERIE)) or "ninguna"}) {len(vs) - len(serie)} iguales; con las '
          f'{len(todas)} reglas {len(vs)}/{len(vs)}; C++ = ee.py en {len(todas) + 2} configuraciones')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
