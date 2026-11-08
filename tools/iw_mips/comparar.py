# SPDX-License-Identifier: MIT
"""Comparador cuadro a cuadro generico (T2-A, 7/11) para cualquier clave 0x7D0-0x7E1: dorado (PCSX2 o el interprete del ELF)
frente a un espejo (el C++ del juego, el interprete u otro). Dice cuantos cuadros coinciden, el PRIMER tick en el que se separan
y que variable lo causa.

Una "fila" es un dict {campo: valor}; un dorado/espejo es una lista de filas con un campo de tick. Lectores: jsonl, log del juego
(`dbz3 iwmips <mini> <que> k=v ...`). Valores: int, float (se compara con --tol) u otros (== exacto); '0x..' y listas se normalizan.

Causa: un campo es *entrada* si acaba en `_in`, empieza por `in_` o va en --entradas. Si en el primer tick distinto las entradas
coinciden, la causa es la logica de ese tick y se nombra la primera variable de salida distinta; si ya difieren entradas, la causa
viene de fuera de la funcion (tick anterior, puente o dato que el espejo no da): se nombran esas entradas.

Uso (cualquier clave, dos ficheros):
  python tools/iw_mips/comparar.py dorado.jsonl espejo.jsonl [--tick i] [--ignorar a,b] [--tol 1e-6] [--entradas x,y] [--mapa d=e,...]
  python tools/iw_mips/comparar.py log dbz3.log <mini> <que> dorado.jsonl ...        (espejo = lineas `dbz3 iwmips`)
Registrados (dorado + espejo propios):  comparar.py 7D1            -> IA de Bubbles: dorado PCSX2 (cap_ia) frente al espejo del ELF
                                        comparar.py 7D1-juego LOG  -> el C++ del juego frente al interprete (prueba_juego)
Otra clave = una entrada en REGISTRO (funcion que devuelve (dorado, espejo, opciones)). --autoprueba: casos sinteticos.
"""
import argparse
import json
import math
import re
import struct
import sys
from pathlib import Path

AQUI = Path(__file__).parent
sys.path.insert(0, str(AQUI))


def norm(v):
    """Normaliza un valor de texto: '0x1F' / hex de seed -> int cuando es claramente numerico; listas recursivas."""
    if isinstance(v, str):
        t = v.strip()
        if re.fullmatch(r'-?\d+', t):
            return int(t)
        if re.fullmatch(r'0[xX][0-9a-fA-F]+', t):
            return int(t, 16)
        if re.fullmatch(r'-?\d*\.\d+(?:[eE][-+]?\d+)?|-?\d+[eE][-+]?\d+', t):
            return float(t)
        return t
    if isinstance(v, (list, tuple)):
        return [norm(x) for x in v]
    return v


def f32(bits):
    return struct.unpack('<f', struct.pack('<I', bits & 0xFFFFFFFF))[0]


def igual(a, b, tol):
    if isinstance(a, list) or isinstance(b, list):
        return isinstance(a, list) and isinstance(b, list) and len(a) == len(b) and all(igual(x, y, tol) for x, y in zip(a, b))
    if isinstance(a, float) or isinstance(b, float):
        try:
            return math.isclose(float(a), float(b), rel_tol=tol, abs_tol=tol)
        except (TypeError, ValueError):
            return False
    return a == b


def es_entrada(campo, entradas):
    return campo in entradas or campo.endswith('_in') or campo.startswith('in_') or campo.endswith('in')


def primer_dif(dorado, espejo, tick='i', ignorar=(), tol=1e-6, entradas=(), mapa=None, max_dif=6):
    """Alinea por el campo `tick` (o por posicion si falta) y devuelve el resultado del cotejo.
    mapa: {campo_dorado: campo_espejo}. Solo se comparan los campos comunes (tras el mapa) que no estan en `ignorar`."""
    mapa = mapa or {}
    ign = set(ignorar) | {tick}
    esp = {}
    for k, f in enumerate(espejo):
        esp.setdefault(f.get(tick, k), f)
    res = dict(cuadros=0, iguales=0, sin_pareja=0, primer_tick=None, causa=None, variables=[], por_campo={}, difs=[])
    for k, d in enumerate(dorado):
        t = d.get(tick, k)
        e = esp.get(t)
        if e is None:
            res['sin_pareja'] += 1
            continue
        res['cuadros'] += 1
        malos = []
        for c, v in d.items():
            ce = mapa.get(c, c)
            if c in ign or ce not in e:
                continue
            if not igual(norm(v), norm(e[ce]), tol):
                malos.append((c, v, e[ce]))
        if not malos:
            res['iguales'] += 1
            continue
        for c, _, _ in malos:
            res['por_campo'][c] = res['por_campo'].get(c, 0) + 1
        if len(res['difs']) < max_dif:
            res['difs'].append(dict(tick=t, vars=[dict(campo=c, dorado=v, espejo=w) for c, v, w in malos]))
        if res['primer_tick'] is None:
            res['primer_tick'] = t
            ent = [m for m in malos if es_entrada(m[0], entradas)]
            sal = [m for m in malos if not es_entrada(m[0], entradas)]
            res['variables'] = [m[0] for m in malos]
            if ent:
                res['causa'] = dict(tipo='entrada', vars=[m[0] for m in ent],
                                    texto='ya entran distintas: la causa esta fuera de esta funcion (tick anterior, puente o dato sin dar)')
            else:
                c0 = sal[0]
                res['causa'] = dict(tipo='logica', vars=[c0[0]], texto=f'mismas entradas, sale distinto {c0[0]}: dorado {c0[1]!r} vs espejo {c0[2]!r}')
    res['ok'] = res['cuadros'] > 0 and res['iguales'] == res['cuadros'] and res['sin_pareja'] == 0
    return res


def informe(r, nombre=''):
    L = [f"{nombre} {r['iguales']}/{r['cuadros']} cuadros iguales" + (f", {r['sin_pareja']} sin pareja" if r['sin_pareja'] else '')]
    if r['primer_tick'] is not None:
        L.append(f"  primer tick distinto: {r['primer_tick']}; variables: {', '.join(r['variables'])}")
        L.append(f"  causa ({r['causa']['tipo']}): {r['causa']['texto']}")
        L.append('  cuadros distintos por variable: ' + ', '.join(f'{c}={n}' for c, n in sorted(r['por_campo'].items(), key=lambda x: -x[1])[:8]))
    return '\n'.join(L)


# ------------------------------------------------------------------ lectores
def leer_jsonl(p):
    return [json.loads(l) for l in open(p, encoding='utf-8') if l.strip()]


def leer_log(p, mini, que=None):
    """Lineas `dbz3 iwmips <mini> [que] k=v k=v`: una fila por linea (valores en texto; norm() los normaliza)."""
    filas = []
    rx = re.compile(r'dbz3 iwmips ' + re.escape(mini) + r' ' + (re.escape(que) + r' ' if que else r'(?:(\w+) )?'))
    for n, l in enumerate(open(p, encoding='utf-8', errors='replace')):
        m = rx.search(l)
        if not m or '=' not in l[m.end():]:
            continue
        kv = dict(re.findall(r'(\w+)=(\S+)', l[m.end():]))
        kv.setdefault('i', n)
        filas.append(kv)
    return filas


# ------------------------------------------------------------------ registro (claves con dorado y espejo propios)
def reg_7d1():
    """IA de Bubbles: dorado = sonda PCSX2 cap_ia (semilla y banderas tras cada llamada); espejo = el montaje del juego sobre el ELF."""
    import bubbles as B
    fl = lambda v: f32(v)
    s16 = lambda v: (v & 0xFFFF) - 0x10000 if v & 0x8000 else v & 0xFFFF
    esp, dor, mir = B.Espejo(), [], []
    for linea in open(B.CAP_IA, encoding='utf-8'):
        e = json.loads(linea)
        jug = s16(e['jugador'])
        o = esp.ia(dict(seed_in=e['seed_in'], estado=s16(e['estado']), anterior=0, jugador=jug, dif=2, tarea=bool(e['tarea_in']),
                        bub=[fl(e['bx']), fl(e['by']), fl(e['bz'])], jug=[fl(e['px']), fl(e['py']), fl(e['pz'])],
                        frente=[fl(e['dx']), fl(e['dz'])], stick=[0.0, 0.0],
                        gates=[e['b248'], e['b180']] if 'b248' in e else [0, 1 if jug == 0 else 0], icono_in=0,
                        **({'f200in': e['f200_in'], 'f210in': e['f210_in'], 'f214in': e['f214_in'], 'f218in': e['f218_in']}
                           if 'f218_in' in e else {})))
        base = dict(i=e['i'], seed_in=e['seed_in'], estado=s16(e['estado']), jugador=jug)
        dor.append(dict(base, seed_out=e['seed_out'], flags=''.join('1' if e[k] else '0' for k in ('f200', 'f210', 'f214', 'f218'))))
        mir.append(dict(base, seed_out=o['seed_out'], flags=o['flags']))
    return dor, mir, dict(tick='i')


def reg_7d1_juego(log):
    """El C++ del juego (lineas `dbz3 iwmips bubbles ...` de un dbz3.log) frente al interprete en Python (= ELF)."""
    import bubbles as B
    r = B.prueba_juego(Path(log))
    rs = r['resumen']
    n = rs.get('ia', 0) + rs.get('upd', 0)
    malas = n - rs.get('ia_iguales', rs.get('ia', 0)) - rs.get('upd_iguales', rs.get('upd', 0))
    dor, mir = [dict(i=i) for i in range(n)], [dict(i=i) for i in range(n)]
    for x in r['diferencias']:
        j, o, t = x['juego'], x['oraculo'], int(x['juego'].get('tick', 0))
        if 0 <= t < n and 'seed_out' in j:
            dor[t] = dict(i=t, seed_out=o['seed_out'], flags=o['flags'], voz=o['voz'], icono=o['icono'])
            mir[t] = dict(i=t, seed_out=int(j['seed_out'], 16), flags=j['flags'], voz=int(j['voz']), icono=int(j['icono']))
    for k in range(max(0, malas - len(r['diferencias']))):   # prueba_juego solo lista las 10 primeras: el resto cuenta como distinto
        dor.append(dict(i=n + k, _no_listada=1))
        mir.append(dict(i=n + k, _no_listada=0))
    return dor, mir, dict(tick='i')


REGISTRO = {'7D1': reg_7d1}


# ------------------------------------------------------------------ autoprueba
def autoprueba():
    dor = [dict(i=k, seed_in=k, estado=1, seed_out=k * 2, x=float(k)) for k in range(10)]
    esp = [dict(f) for f in dor]
    r = primer_dif(dor, esp)
    assert r['ok'] and r['cuadros'] == 10, r
    # 1) la logica se separa en el tick 4 por seed_out (entradas iguales)
    esp[4]['seed_out'] = 99
    esp[7]['x'] = 70.0
    r = primer_dif(dor, esp)
    assert r['primer_tick'] == 4 and r['causa']['tipo'] == 'logica' and r['causa']['vars'] == ['seed_out'], r
    assert r['iguales'] == 8 and r['por_campo'] == {'seed_out': 1, 'x': 1}, r
    # 2) la entrada ya llega distinta: la culpa es de fuera
    esp = [dict(f) for f in dor]
    esp[2]['seed_in'] = 5
    esp[2]['seed_out'] = 5
    r = primer_dif(dor, esp)
    assert r['primer_tick'] == 2 and r['causa']['tipo'] == 'entrada' and r['causa']['vars'] == ['seed_in'], r
    # 3) tolerancia de float, mapa de nombres, valores en texto
    esp = [dict(i=k, seed_in=str(k), estado='1', so=hex(k * 2), x=k + 1e-9) for k in range(10)]
    r = primer_dif(dor, esp, mapa={'seed_out': 'so'})
    assert r['ok'], r
    # 4) lector de log
    import tempfile
    with tempfile.NamedTemporaryFile('w', suffix='.log', delete=False, encoding='utf-8') as f:
        f.write('basura\n[x] dbz3 iwmips cell tick tick=3 pos=1.5 seed_out=0x1F\n')
    fl = leer_log(f.name, 'cell')
    assert fl and fl[0]['tick'] == '3' and norm(fl[0]['seed_out']) == 31, fl
    print('comparar.py autoprueba OK (primer tick, causa logica/entrada, tolerancia, mapa, log)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('args', nargs='*')
    ap.add_argument('--tick', default='i')
    ap.add_argument('--ignorar', default='')
    ap.add_argument('--entradas', default='')
    ap.add_argument('--mapa', default='')
    ap.add_argument('--tol', type=float, default=1e-6)
    ap.add_argument('--autoprueba', action='store_true')
    a = ap.parse_args()
    if a.autoprueba:
        return autoprueba()
    ig = [x for x in a.ignorar.split(',') if x]
    en = [x for x in a.entradas.split(',') if x]
    mapa = dict(x.split('=') for x in a.mapa.split(',') if x)
    p = a.args
    if p and p[0] in REGISTRO:
        dor, esp, op = REGISTRO[p[0]]()
        nombre = f'0x{p[0]}'
    elif p and p[0] == '7D1-juego':
        dor, esp, op = reg_7d1_juego(p[1])
        nombre = '0x7D1 juego'
    elif p and p[0] == 'log':
        dor, esp, op = leer_jsonl(p[4]), leer_log(p[1], p[2], p[3]), {}
        nombre = f'{p[2]} {p[3]}'
    else:
        dor, esp, op = leer_jsonl(p[0]), leer_jsonl(p[1]), {}
        nombre = Path(p[0]).name
    r = primer_dif(dor, esp, op.get('tick', a.tick), ig, a.tol, en, mapa)
    print(informe(r, nombre))
    if not r['ok']:
        raise SystemExit(1)
    return r


if __name__ == '__main__':
    main()
