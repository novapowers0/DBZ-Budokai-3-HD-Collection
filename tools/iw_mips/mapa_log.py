# SPDX-License-Identifier: MIT
"""tanda4 iw-mapa (8/11): reejecuta en ee.py (espejo desde cero) un log del juego con DBZ3_TEST_IWMINI=1 y compara posicion de
Goku, camara y casilla elegida cuadro a cuadro. Cifra: cuadros iguales / total. El juego deja dos tipos de linea:
  dbz3 iwmips mapa ini t=.. road=.. saga=.. de_golpe=.. marcas=CLAVE:MASCARA,.. paso=.. cuadros=.. goku=x,y
  dbz3 iwmips mapa t=.. mant=.. pulsa=.. lx=.. ly=.. paso=.. goku=x,y cam=x,y anim=.. sel=.. ord=.. conf=..
Uso: python tools/iw_mips/mapa_log.py <dbz3.log> [--verbose]"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import mapa_espejo as ME  # noqa: E402
import mapa_banco as MB  # noqa: E402

INI = re.compile(r'dbz3 iwmips mapa ini t=(\d+) road=(\d) saga=(\d+) de_golpe=(\d) marcas=(\S+) paso=(\S+) cuadros=(\d+) goku=(\S+),(\S+)')
FR = re.compile(r'dbz3 iwmips mapa t=(\d+) mant=(\w+) pulsa=(\w+) lx=(-?\d+) ly=(-?\d+) paso=(\S+) goku=(\S+),(\S+) cam=(\S+),(\S+) '
                r'anim=(\d+) sel=(-?\d+) ord=(\d+) conf=(-?\d+)')


def espejo_de(m):
    t, road, saga, golpe, marcas, paso, cuadros, gx, gy = m.groups()
    sp = ME.Espejo(saga=int(saga), road=bool(int(road)), goku=(float(gx), float(gy)))
    if marcas != '-':
        for par in marcas.strip(',').split(','):
            k, mask = par.split(':')
            sp.marca(int(k, 16), int(mask, 16))
        sp.c.call(0x2AB590, 0x5C1490, limite=5_000_000)
    import struct as _s
    sp.c.w32(MB.G_PASO, _s.unpack('<I', _s.pack('<f', float(paso)))[0])
    k = sp.aparecer(de_vuelta=bool(int(golpe)))
    return sp, k, int(cuadros)


def main():
    texto = Path(sys.argv[1]).read_text(encoding='utf-8', errors='replace')
    verbose = '--verbose' in sys.argv
    sp, ok, tot, dif = None, 0, 0, []
    for linea in texto.splitlines():
        m = INI.search(linea)
        if m:
            sp, k, k_juego = espejo_de(m)
            print('ini t=%s: aparicion %d cuadros (juego %d) %s' % (m.group(1), k, k_juego, 'IGUAL' if k == k_juego else 'DISTINTO'))
            continue
        m = FR.search(linea)
        if m and sp:
            t, mant, pulsa, lx, ly, paso, gx, gy, cx, cy, anim, sel, nord, conf = m.groups()
            sp.c.w32(MB.G_PASO, struct.unpack('<I', struct.pack('<f', float(paso)))[0])
            x, y, nodo = sp.cuadro(mant=int(mant, 16), pulsa=int(pulsa, 16), lx=int(lx), ly=int(ly))
            fila = (nodo - sp.ctx) // 0x30 if nodo else -1
            fila_id = struct.unpack_from('<H', sp.c.m, nodo)[0] if nodo else -1
            esperado = (float(gx), float(gy), float(cx), float(cy), int(sel))
            propio = (x, y, sp.f(sp.M + 0x29C), sp.f(sp.M + 0x2A0), fila_id)
            tot += 1
            if all(abs(a - b) <= 1e-3 for a, b in zip(esperado[:4], propio[:4])) and esperado[4] == propio[4]:
                ok += 1
            else:
                dif.append((t, esperado, propio))
    print('cuadros de mapa libre: %d/%d iguales' % (ok, tot))
    for d in dif[: (50 if verbose else 5)]:
        print('  DIF t=%s juego=%s ee.py=%s' % d)
    return 0 if tot and ok == tot else 1


if __name__ == '__main__':
    sys.exit(main())
