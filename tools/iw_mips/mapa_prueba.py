# SPDX-License-Identifier: MIT
"""tanda4 iw-mapa (8/11): el mismo recorrido corto del mapa (aparecer, mover con palanca/cruceta/correr, quedarse sobre una
casilla, confirmar) sobre (a) el savestate del PCSX2 (mapa_banco.Mapa) y (b) el espejo desde cero (mapa_espejo.Espejo).
Imprime cuantos campos distintos hay; 0 = el espejo hace lo mismo que el juego."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import mapa_banco as MB  # noqa: E402
import mapa_espejo as ME  # noqa: E402


def poner(m, x, y):
    m.c.m[m.M + 0x1D0:m.M + 0x1D8] = struct.pack('<ff', x, y)
    m.c.m[m.M + 0x29C:m.M + 0x2A4] = struct.pack('<ff', x, y)


def esc(m):
    c, M = m.c, m.M
    c.w32(M + 0x330, 0); c.w32(M + 0x18, 0); c.w32(M + 0x1C, 0)
    m.aparecer()
    n = [x for x in m.nodos() if x[1] == 3]
    r = [tuple(m.nodos())]
    x, y = n[-1][4]
    poner(m, x, y)
    out = []
    pads = [dict(), dict(lx=100), dict(lx=100), dict(lx=-100), dict(ly=100), dict(ly=-100), dict(mant=0x20),
            dict(mant=0x80 | 0x8000), dict(lx=70, ly=70, mant=0x8000)]
    for pad in pads:
        for _ in range(3):
            xx, yy, sel = m.cuadro(**pad)
            out.append((xx, yy, (sel - m.ctx) // 0x30 if sel else -1, c.r32(M + 0x220)))
    r.append(tuple(out))
    poner(m, x, y)
    m.cuadro()
    m.cuadro(pulsa=0x4000, mant=0x4000)
    r.append((tuple(c.r32(M + k) for k in range(0x10, 0x20, 4)) , tuple(c.r32(0x5D9220 + 4 * k) for k in range(0, 12)),
              tuple(c.r32(0x5D9260 + 4 * k) for k in range(0, 12)), c.r32(0x5D92A0), tuple(m.ev)))
    return r


def main():
    a = esc(MB.Mapa())
    b = esc(ME.Espejo(goku=(0, 0)))
    nd = 0
    for i, (p, q) in enumerate(zip(a, b)):
        if p != q:
            nd += 1
            print('DIF', i, str(p)[:400], '\n    ', str(q)[:400])
    print('seccion 0 = casillas, 1 = 27 cuadros de mando, 2 = confirmar: %d secciones distintas' % nd)
    print('seleccion en los cuadros de mando:', [t[2] for t in a[1]][:9])
    return nd


if __name__ == '__main__':
    sys.exit(1 if main() else 0)
