"""Resuelve un muestreo (ms tid rip) contra el mapa del exe.

  python tools\\cpu_perfil\\sym.py <muestreo.txt> [<mapa>]

Sin mapa usa la variable de entorno MAP.

ATENCION: la atribucion es APROXIMADA. El mapa de lld solo lista simbolos publicos; una
funcion interna (estatica, clon de LTO o cuerpo plegado por ICF) no aparece y sus muestras
caen en el simbolo publico anterior. Por eso la tabla "por funcion" es una CANDIDATA: sirve
para localizar el codigo caliente, no para afirmar el tamano exacto de una funcion. Las
entradas con '~' son las que quedan a mas de 4 KB del simbolo anterior (tramo sospechoso).
"""
import bisect
import collections
import os
import re
import sys

SOSPECHOSO = 0x1000


def load_map(path):
    best = {}
    pat = re.compile(r'\s+[0-9a-fA-F]{4}:([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{16})')
    for line in open(path, encoding='utf-8', errors='replace'):
        m = pat.match(line)
        if not m:
            continue
        va = int(m.group(3), 16)
        if va < 0x140000000:
            continue
        name = m.group(2)
        if name.startswith('__imp_'):
            continue
        best.setdefault(va - 0x140000000, name)
    return best, sorted(best)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    sample = sys.argv[1]
    mapa = sys.argv[2] if len(sys.argv) > 2 else os.environ.get('MAP')
    if not mapa:
        print('falta el mapa')
        return 2
    best, rv = load_map(mapa)
    mods = {}
    rows = []
    for line in open(sample, encoding='utf-8', errors='replace'):
        if line.startswith('#mod'):
            p = line.split()
            mods[p[1].lower()] = (int(p[2], 16), int(p[3], 16))
            continue
        if line.startswith('#'):
            continue
        rows.append(int(line.split()[2], 16))
    exe = mods.get('dbz3.exe', (0, 0))[0]
    by = collections.Counter()
    deep = collections.Counter()
    aprox = 0
    for rip in rows:
        hit = None
        for k, (b, s) in mods.items():
            if b <= rip < b + s:
                hit = k
                break
        if not hit:
            by['??'] += 1
            continue
        by[hit] += 1
        if hit == 'dbz3.exe':
            i = bisect.bisect_right(rv, rip - exe) - 1
            if i < 0:
                deep['PRE'] += 1
                continue
            name, off = best[rv[i]], rip - exe - rv[i]
            span = (rv[i + 1] - rv[i]) if i + 1 < len(rv) else 0x10000
            if off < span:
                if span > SOSPECHOSO:
                    deep['~' + name] += 1
                    aprox += 1
                else:
                    deep[name] += 1
            else:
                deep['GAP:%s+0x%X' % (name, off)] += 1
        else:
            deep['%s+0x%X' % (hit, rip - mods[hit][0])] += 1
    n = len(rows)
    print('muestras %d (atribucion aproximada por simbolo publico anterior)' % n)
    print('--- por modulo')
    for k, v in by.most_common(8):
        print('  %6.2f%% %7d %s' % (100 * v / n, v, k))
    print('--- por funcion (candidatas; ~ = tramo > 4 KB, puede incluir codigo sin simbolo)')
    for k, v in deep.most_common(40):
        print('  %6.2f%% %7d %s' % (100 * v / n, v, k))
    if aprox:
        print('aviso: %d muestras en tramos con simbolo poco fiable (marcadas ~)' % aprox)
    return 0


if __name__ == '__main__':
    sys.exit(main())
