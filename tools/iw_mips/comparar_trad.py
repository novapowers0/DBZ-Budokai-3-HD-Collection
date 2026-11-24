# SPDX-License-Identifier: MIT
"""R23 G2/G3: el IW traducido DENTRO DEL JUEGO frente al original (oraculo), cuadro a cuadro.

  python tools/iw_mips/comparar_trad.py ORACULO_gs.jsonl JUEGO_volcado.txt [--datos DIR]

ORACULO = E:\\DBZ3HD_trabajo\\iw_volcado\\<clave>\\gs.jsonl (volcar.py). JUEGO = DBZ3_IWTRAD_VOLCADO con
DBZ3_IWTRAD_VOLCADO_DESDE (src/iw_trad_bucle.cpp), con el mismo guion de mando y cambio de mision que volcar.py.

- 2D (G3): las primitivas que el traductor saca de las ordenes GS (posicion, textura, color, mezcla, recorte) y las
  subidas de textura con su contenido: el oraculo pasa por el gemelo gs_primitivas.Traductor; el juego, por el C++.
  Iguales => el GS pinta los mismos pixeles en los dos (misma entrada al GS).
- 3D (G2): la entrada de la VU1 (UNPACK con md5 de los datos, MSCAL, MPG con md5): iguales => la salida XGKICK es la
  del original (misma entrada y mismo microprograma).
Sale 0 si todos los cuadros comunes son iguales.
"""
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import gs_primitivas as G  # noqa: E402


def fnv(b):
    h = 0xCBF29CE484222325
    for x in b:
        h = ((h ^ x) * 0x100000001B3) & ((1 << 64) - 1)
    return h


class Twin(G.Traductor):
    """El gemelo empieza la captura (cuadro 4250) con los registros a 0: lo que el original fijo ANTES y no vuelve a
    mandar en la ventana (FOGCOL, FOG...) no se conoce. Se apunta que registros se han escrito para tapar solo esos."""
    def __init__(s):
        super().__init__()
        s.escritos = set()
        s.conocidos = []   # por primitiva: registros escritos cuando se disparo

    def registro(s, r, v):
        s.escritos.add(r)
        k = len(s.prims)
        super().registro(r, v)
        s.conocidos += [frozenset(s.escritos)] * (len(s.prims) - k)

    def fin_cuadro(s):
        super().fin_cuadro()
        s.conocidos = []


# campo de la linea P -> registro GS (c = contexto); None = siempre conocido
CAMPOS = [None, 6, 0x14, 8, 0x42, 0x47, 0x40, 0x18, 0x4C, 0x4E, 0x4A, 0x49, 0x44, 0x45, 0x46, 0x3D, 0x3B]
CON_CTX = {6, 0x14, 8, 0x42, 0x47, 0x40, 0x18, 0x4C, 0x4E, 0x4A}


def tapar(lineas, kn):
    """Cambia por '?' los campos que dependen de registros que el original no escribio en la ventana."""
    out, ip = [], 0
    for l in lineas:
        if not l.startswith('P '):
            out.append(l)
            continue
        partes = l.split(' | ')
        cab = partes[0].split()
        escritos = kn[ip] if ip < len(kn) else frozenset()
        ip += 1
        cab[6] = '%X' % (int(cab[6], 16) & 0x7FFFF)   # TEST: el GS solo usa 19 bits (los altos son restos de la pila)
        # Semantica del GS (manual cap. 5): sin TME no se leen TEX0/TEX1/CLAMP/TEXA ni ST/UV; sin FGE, ni FOGCOL ni la
        # niebla del vertice. Esos valores no cambian ningun pixel: se tapan en los dos lados.
        prim = int(cab[1], 16)
        tme, fge = prim & 0x10, prim & 0x20
        if not tme:
            for i in (2, 3, 4, 17):
                cab[i] = '-'
        if not fge:
            cab[16] = '-'
        # Z: si no se escribe (ZMSK) y la prueba es ALWAYS (o no hay prueba), el valor no llega a ningun pixel.
        test = int(cab[6], 16)
        zbuf = int(cab[10], 16) if cab[10] not in '?-' else 0
        sin_z = (zbuf >> 32) & 1 and (not (test >> 16) & 1 or ((test >> 17) & 3) == 1)
        c = 1 if int(cab[1], 16) & 0x200 else 0
        for i, r in enumerate(CAMPOS):
            if r is not None and (r + (c if r in CON_CTX else 0)) not in escritos:
                cab[i + 1] = '?'
        vs = []
        for v in partes[1:]:
            f = v.split()
            if 0xA not in escritos:
                f[3] = '?'          # niebla del registro FOG (los XYZF la traen en el vertice y si se conoce)
            if not fge:
                f[3] = '-'
            if not tme:
                f[6] = f[7] = '-'
            if sin_z:
                f[2] = '-'
            vs.append(' '.join(f))
        out.append(' | '.join([' '.join(cab)] + vs))
    return out


def oraculo(ruta, datos, ventana):
    t = Twin()
    for l in open(ruta, encoding='utf-8'):
        f = json.loads(l)
        n = f['n']
        if ventana and n not in ventana:
            continue
        vif = []
        for o in f['ordenes']:
            if o[0] == 'G':
                t.registro(o[1], o[2])
            elif o[0] == 'I':
                p = os.path.join(datos, o[2] + '.bin') if len(o) > 2 else ''
                t.imagen(open(p, 'rb').read() if p and os.path.exists(p) else bytes(o[1] * 16))
            elif o[0] == 'U':
                d = o[5]
                h = d[4:] if d.startswith('md5:') else hashlib.md5(bytes.fromhex(d)).hexdigest()
                vif.append('VU %d %d %d %d %s' % (o[1], o[2], o[3], o[4], h))
            elif o[0] == 'M':
                vif.append('VM %d' % o[1])
            elif o[0] == 'P':
                vif.append('VP %d %d %s' % (o[1], o[2], o[3]))
        dos, kn = t.texto(), list(t.conocidos)
        t.fin_cuadro()
        yield n, dos, vif, kn


def juego(ruta):
    dos, vif = [], []
    for l in open(ruta, encoding='utf-8', errors='replace'):
        l = l.rstrip('\n')
        if l.startswith('F '):
            yield int(l[2:]), dos, vif
            dos, vif = [], []
        elif l.startswith('V'):
            vif.append(l)
        elif l:
            dos.append(l)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('oraculo')
    ap.add_argument('juego')
    ap.add_argument('--datos', default=r'E:\DBZ3HD_trabajo\iw_volcado\_datos')
    ap.add_argument('--max-dif', type=int, default=3, help='diferencias que se imprimen por tipo')
    o = ap.parse_args()
    j = {n: (d, v) for n, d, v in juego(o.juego)}
    res = {'2D': [0, 0, 0, []], '3D': [0, 0, 0, []]}   # cuadros, iguales, lineas, primeras diferencias
    comunes = 0
    primero = json.loads(open(o.oraculo, encoding='utf-8').readline())['n']
    for n, d, v, esc in oraculo(o.oraculo, o.datos, set(j)):
        if n not in j or n == primero:
            continue                   # el primer cuadro de la captura es de calentamiento: registros aun sin fijar
        comunes += 1
        for k, a, b in (('2D', tapar(d, esc), tapar(j[n][0], esc)), ('3D', v, j[n][1])):
            r = res[k]
            r[0] += 1
            r[2] += len(a)
            if a == b:
                r[1] += 1
            elif len(r[3]) < o.max_dif:
                i = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
                r[3].append((n, i, len(a), len(b), a[i:i + 1], b[i:i + 1]))
    print('cuadros: oraculo y juego en comun %d (juego %d: %d..%d)' % (comunes, len(j), min(j, default=0), max(j, default=0)))
    ok = comunes > 0
    for k, (c, i, l, difs) in res.items():
        print('%s: %d/%d cuadros iguales (%d lineas del original)' % (k, i, c, l))
        for n, p, la, lb, a, b in difs:
            print('  DIF cuadro %d linea %d (original %d, juego %d)\n    orig  %s\n    juego %s' % (n, p, la, lb, a, b))
        ok = ok and i == c
    print('VEREDICTO', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
