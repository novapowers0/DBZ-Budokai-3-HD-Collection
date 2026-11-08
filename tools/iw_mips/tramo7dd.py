# SPDX-License-Identifier: MIT
"""Tramo libre del espejo del 0x7DD desde un estado del dorado del tick (iw_7dd_raiz, 7/11). Copia el estado A de la
pareja --desde (Piccolo, su work, la tarea, la semilla, el jugador) y corre el espejo campo a campo, solo (rayos del
#WCF por el arnes), comparando la posicion de Piccolo con la de PCSX2 en cada pareja posterior (por numero de cuadro).
Uso: python tools/iw_mips/tramo7dd.py --desde 540 [--hasta 600]
"""
import argparse
import json
import math
import struct
import subprocess as sp
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import bubbles as B  # noqa: E402
import persecucion as P  # noqa: E402

u32, b2f = P.u32, P.b2f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--desde', type=int, default=540)
    ap.add_argument('--hasta', type=int, default=600)
    ap.add_argument('--sin-raiz', action='store_true')
    ap.add_argument('--col-original', action='store_true', help='choques con la colision del PS2 (persecucion.proveedor_ps2)')
    ap.add_argument('--extra', type=int, default=0, help='campos de mas tras la ultima pareja')
    ap.add_argument('--traza', type=int, default=0, help='imprime los N primeros ticks del espejo')
    a = ap.parse_args()
    meta = json.loads((P.CAP_TICK / 'parejas.json').read_text())
    dat = (P.CAP_TICK / 'parejas.bin').read_bytes()
    b0 = meta['base']
    tam = max(o + n for bl in meta['bloques'].values() for _, _, n, o in bl) - b0
    mem, _ = B.cargar(B.ELF, P.P2S)
    base = bytes(mem)

    def estado(k):
        par = meta['parejas'][k]
        mem[:] = base
        P._bloques(meta, dat[par['off']:par['off'] + tam], b0, mem, 'A')
        return bytes(mem), par['cnt']
    eA, cnt0 = estado(a.desde)
    mgr = u32(eA, 0x49CA70); pic = mgr + 0x28D0; jug = mgr + 0x800; w = u32(eA, 0x49CBC0); w1 = w + 0x260
    pr = sp.Popen([str(P.RAYO_EXE), str(P.WCF)], stdin=sp.PIPE, stdout=sp.PIPE, text=True, bufsize=1)

    def proveedor(k, x, y, radio, m):
        q = ' '.join(repr(b2f(v)) for v in x + y)
        pr.stdin.write(('S %s %r %d\n' % (q, b2f(radio), m)) if k == 'S' else ('%s %d\n' % (q, m)))
        pr.stdin.flush()
        h, px, py, pz, t, mt, nx, ny, nz, dd = pr.stdout.readline().split()
        return (int(h), [B.f2b(float(px)), B.f2b(float(py)), B.f2b(float(pz))],
                [B.f2b(float(nx)), B.f2b(float(ny)), B.f2b(float(nz))], B.f2b(float(dd)), int(mt), int(t))
    if a.col_original:
        proveedor = P.proveedor_ps2(base)   # noqa: F811
    esp = P.Espejo()
    esp.proveedor = proveedor
    esp.preparar()
    m2, A_ = esp.mem, esp.A
    PUNT = {0xB64, 0xB78, 0xB7C, 0xBB8, 0xBC0, 0x1380, 0x1384}
    for o in range(0, 0x20D0, 4):
        if o not in PUNT:
            m2[P.PIC + o:P.PIC + o + 4] = eA[pic + o:pic + o + 4]
    m2[P.W0:P.W0 + 0x4C0] = eA[w:w + 0x4C0]
    t = u32(eA, w1 + 0xB8)
    if t:
        m2[P.TAREA:P.TAREA + 0x100] = eA[t:t + 0x100]
        struct.pack_into('<I', m2, P.W1 + 0xB8, P.TAREA)
    mgw = u32(eA, 0x49CBB8)
    m2[P.MGW + 0x1E8:P.MGW + 0x260] = eA[mgw + 0x1E8:mgw + 0x260]
    G = P.GRID
    for a_, v_ in ((A_.P_GRID, G), (G + 0x630, 7), (G + 0xAD4, B.f2b(3000.0)), (G + 0xAD8, B.f2b(3000.0)),
                   (G + 0xADC, 10), (G + 0xAE0, 10), (G + 0xAF0, B.f2b(-1350.0)), (G + 0xAF8, B.f2b(-1350.0))):
        struct.pack_into('<I', m2, a_, v_)
    # el clip como lo deja el puente de BonesMovement del juego (banco propio +0xC64 = 0x10000, cuadros en modelo+0x708)
    struct.pack_into('<I', m2, P.PIC + 0xC64, 0x10000)
    struct.pack_into('<I', m2, P.MODELO + 0x708, P.LARGOS.get(u32(eA, pic + 0xB60), P.LARGO_OTRO))
    if esp.R and not a.sin_raiz:
        esp.clip_raiz = esp.R.clip(u32(eA, pic + 0xB60))
        esp.vivo = esp.R.muestra(esp.clip_raiz, b2f(u32(eA, pic + 0xC68)))
    else:
        esp.R = None
    seed = u32(eA, B.SEMILLA)
    j40 = [u32(eA, jug + 0x40 + 4 * i) for i in range(3)]
    j640 = [u32(eA, jug + 0x640 + 4 * i) for i in range(3)]
    jcbc = struct.unpack_from('<H', eA, jug + 0xCBC)[0]
    dif = struct.unpack_from('<H', eA, u32(eA, 0x49CBA4) + 0xC)[0]
    # objetivos: posicion de PCSX2 en A de cada pareja posterior
    obj = {}
    for k in range(a.desde + 1, min(a.hasta, len(meta['parejas']))):
        e, c = estado(k)
        obj[c - cnt0] = ([b2f(u32(e, pic + 0x40 + 4 * i)) for i in range(3)], struct.unpack_from('<h', e, pic + 0xCBC)[0])
    ult = max(obj) if obj else 0
    malas, alcanza = 0, None
    alc_pcsx2 = next((n for n in sorted(obj) if math.hypot(obj[n][0][0], obj[n][0][2]) < 14), None)
    for n in range(ult + 1 + a.extra):
        q = [b2f(u32(m2, P.PIC + 0x40 + 4 * i)) for i in range(3)]
        if alcanza is None and math.hypot(q[0] - b2f(j40[0]), q[2] - b2f(j40[2])) < 14:
            alcanza = n
        if n in obj:
            q = [b2f(u32(m2, P.PIC + 0x40 + 4 * i)) for i in range(3)]
            o, est = obj[n]
            d = math.dist(q, o)
            malas += d > 1
            print('campo %4d estado PCSX2 %2d espejo %2d  PCSX2 %s  espejo %s  dif %.2f  a %.1f del jugador' % (
                n, est, struct.unpack_from('<h', m2, P.PIC + 0xCBC)[0], [round(x, 1) for x in o],
                [round(x, 1) for x in q], d, math.hypot(q[0], q[2])))
        p640 = [u32(m2, P.PIC + 0x640 + 4 * i) for i in range(3)]
        p770 = [p640[0], B.f2b(b2f(p640[1]) + 5.5), p640[2]]
        dist = math.sqrt(sum((b2f(j640[i]) - b2f(p640[i])) ** 2 for i in range(3)))
        e = dict(ini=0, seed=seed, pic0=[0, 0, 0], j40=j40, j640=j640, p640=p640, p770=p770, dist=B.f2b(dist),
                 jcbc=jcbc, dif=dif, t25a=1, w180=0, rayos=[])
        o_ = esp.tick(e)
        seed = o_['seed']
        if a.traza and n < a.traza:
            g = lambda o: round(b2f(u32(m2, P.PIC + o)), 1)   # noqa: E731
            print('tick', n, 'estado', struct.unpack_from('<h', m2, P.PIC + 0xCBC)[0], 'clip', hex(u32(m2, P.PIC + 0xB60)),
                  'c68', g(0xC68), 'c70', g(0xC70), 'a0', round(b2f(u32(m2, P.W1 + 0x1A0)), 1), 'choques',
                  [(k[0], k[1]) for k in esp.col], 'ev', [hex(x) for x in esp.ev])
    print('parejas con mas de 1 u de diferencia:', malas, 'de', len(obj))
    print('desde', a.desde, 'cuadro', cnt0, ': PCSX2 alcanza (<14) en el campo', alc_pcsx2, '; espejo en el campo', alcanza)


if __name__ == '__main__':
    main()
