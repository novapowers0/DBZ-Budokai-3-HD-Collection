# SPDX-License-Identifier: MIT
"""Sesion sintetica del espejo del 0x7DD (iw_7dd_raiz, 7/11): jugador QUIETO donde estaba en el dorado del tick y Piccolo
entrando donde entro en el original; cuenta los campos hasta que la distancia horizontal baja de 14 (criterio del port,
iw_minijuegos.cpp). Rayos y barridos: el arnes rayo7dd.exe sobre el #WCF 907/0 (= el juego). Original (PCSX2): ~42 s.
Uso: python tools/iw_mips/sintetica7dd.py [--campos 4000] [--sin-raiz]
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--campos', type=int, default=4000)
    ap.add_argument('--sin-raiz', action='store_true')
    ap.add_argument('--raiz-previa', action='store_true', help='(ya es lo de serie; se mantiene por compatibilidad)')
    ap.add_argument('--sin-raiz-previa', action='store_true', help='el espejo de antes: +0x50 sin escribir y la Update antes de la tarea')
    ap.add_argument('--cintura', action='store_true', help='hueso 1 con el giro de su clip (no la pose fija)')
    ap.add_argument('--entre', action='store_true', help='avanza el LFSR entre ticks como el resto del juego original')
    ap.add_argument('--inicio4', action='store_true', help='Piccolo sale como en el original: estado 4 en (-9; -49,7; -879,7)')
    ap.add_argument('--traza', type=lambda x: [int(v) for v in x.split(':')], default=None, help='A:B campos a imprimir')
    ap.add_argument('--cabeza-fija', action='store_true', help='rayo desde ancla + 5,5 (lo de antes) y no la tabla CABEZA')
    ap.add_argument('--semilla', type=lambda x: int(x, 0), default=0x12345678)
    ap.add_argument('--col-original', action='store_true',
                    help='rayos y barridos con Collision_Raycast/MoveSphere del ELF sobre el arbol del savestate (PS2)')
    a = ap.parse_args()
    meta = json.loads((P.CAP_TICK / 'parejas.json').read_text())
    dat = (P.CAP_TICK / 'parejas.bin').read_bytes()
    b0 = meta['base']
    tam = max(o + n for bl in meta['bloques'].values() for _, _, n, o in bl) - b0
    mem, _ = B.cargar(B.ELF, P.P2S)
    base = bytes(mem)
    jug = pic0 = None
    for par in meta['parejas']:
        d = dat[par['off']:par['off'] + tam]
        mem[:] = base
        P._bloques(meta, d, b0, mem, 'A')
        mgr = P.u32(mem, 0x49CA70)
        if struct.unpack_from('<h', mem, mgr + 0x28D0 + 0xCBC)[0] == 13:
            f = lambda x, o: [P.u32(mem, x + o + 4 * i) for i in range(3)]
            jug = (f(mgr + 0x800, 0x40), f(mgr + 0x800, 0x640))
            pic0 = f(mgr + 0x28D0, 0x40)
            print('entrada en el cuadro', par['cnt'], 'jugador', [round(P.b2f(x), 1) for x in jug[0]],
                  'piccolo', [round(P.b2f(x), 1) for x in pic0])
            break
    pr = sp.Popen([str(P.RAYO_EXE), str(P.WCF)], stdin=sp.PIPE, stdout=sp.PIPE, text=True, bufsize=1)

    def proveedor(k, x, y, radio, m):
        q = ' '.join(repr(P.b2f(v)) for v in x + y)
        pr.stdin.write(('S %s %r %d\n' % (q, P.b2f(radio), m)) if k == 'S' else ('%s %d\n' % (q, m)))
        pr.stdin.flush()
        h, px, py, pz, t, mt, nx, ny, nz, dd = pr.stdout.readline().split()
        return (int(h), [B.f2b(float(px)), B.f2b(float(py)), B.f2b(float(pz))],
                [B.f2b(float(nx)), B.f2b(float(ny)), B.f2b(float(nz))], B.f2b(float(dd)), int(mt), int(t))
    if a.col_original:   # colision del PS2: el codigo original sobre la RAM del savestate (arbol fijo del campo)
        proveedor = P.proveedor_ps2(base)   # noqa: F811
    esp = P.Espejo()
    esp.proveedor = proveedor
    esp.cintura = a.cintura
    esp.raiz_previa = not a.sin_raiz_previa
    if a.sin_raiz:
        esp.R = None
    seed = a.semilla
    if a.inicio4:   # fase 1 (7/11): la salida del original (pareja 238, cuadro 18287: estado 4 en el suelo)
        pic0 = [B.f2b(-9.0), B.f2b(-49.7), B.f2b(-879.7)]
    primer13 = None
    jx, jz = P.b2f(jug[0][0]), P.b2f(jug[0][2])
    estados, modos, vuela, alturas, dists, tipos, vis, con_tarea, frente, prev_p = {}, {}, {}, {}, {}, {}, {}, {}, {}, None
    import random
    rnd_entre = random.Random(a.semilla)
    pasos_n, pesos = zip(*P.PASOS_ENTRE)
    for n in range(a.campos):
        c = esp.cpu
        p640 = [c.r32(P.PIC + 0x640 + 4 * i) for i in range(3)] if n else [B.f2b(P.b2f(pic0[i]) + (0.97 if i == 1 else 0))
                                                                            for i in range(3)]
        p770 = ([p640[0], B.f2b(P.b2f(p640[1]) + 5.5), p640[2]] if a.cabeza_fija or not n else
                P.cabeza(p640, P.b2f(c.r32(P.W1 + 0x50)), P.b2f(c.r32(P.W1 + 0x58)), c.r32(P.PIC + 0xB60)))
        dist = math.sqrt(sum((P.b2f(jug[1][i]) - P.b2f(p640[i])) ** 2 for i in range(3)))
        e = dict(ini=int(n == 0), estado_ini=4 if a.inicio4 else 13, seed=seed, pic0=pic0, j40=jug[0], j640=jug[1], p640=p640, p770=p770,
                 dist=B.f2b(dist), jcbc=1, dif=2, t25a=1, w180=0, rayos=[])
        o = esp.tick(e)
        seed = o['seed']
        if a.entre:   # el resto del juego tira del LFSR entre dos ticks (persecucion.PASOS_ENTRE, medido en el dorado)
            seed = P.lfsr(seed, rnd_entre.choices(pasos_n, pesos)[0])
        if 0x7A000000 in esp.ev:   # tarea nueva: su modo (1 = va a por el jugador, 2/3 = vaga) en +0x60
            m_ = c.r16(P.TAREA + 0x60)
            modos[m_] = modos.get(m_, 0) + 1
            if m_ == 1 and modos[m_] <= 3:
                print('campo', n, 'tarea modo 1 (va a por el jugador) en estado', o['estado'])
        estados[o['estado']] = estados.get(o['estado'], 0) + 1
        if primer13 is None and o['estado'] == 13 and (n or not a.inicio4):
            primer13 = n
        vuela[(o['estado'], esp.mem[P.PIC + 0x2061])] = vuela.get((o['estado'], esp.mem[P.PIC + 0x2061]), 0) + 1
        if a.traza and a.traza[0] <= n < a.traza[1]:
            print('  hueso1 fila2', [round(P.b2f(c.r32(P.MATS + 0x60 + 4 * i)), 2) for i in range(3)], 'clip_raiz', esp.clip_raiz)
        for d_, h_ in esp.vision:
            vis[h_] = vis.get(h_, 0) + 1
        esp.vision = []
        con_tarea[int(c.r32(P.W1 + 0xB8) != 0)] = con_tarea.get(int(c.r32(P.W1 + 0xB8) != 0), 0) + 1
        if c.r32(P.W1 + 0xB8) and a.traza and a.traza[0] <= n < a.traza[1]:
            print('  tarea t50', round(P.b2f(c.r32(P.TAREA + 0x50)), 1), 't54', round(P.b2f(c.r32(P.TAREA + 0x54)), 1), 'modo', c.r16(P.TAREA + 0x60))
        qx_, qz_ = P.b2f(o['p'][0]), P.b2f(o['p'][2])
        if prev_p is not None:
            v_ = (qx_ - prev_p[0], qz_ - prev_p[1]); nv = math.hypot(*v_)
            if nv > 0.3:
                fx_, fz_ = P.b2f(c.r32(P.W1 + 0x50)), P.b2f(c.r32(P.W1 + 0x58))
                ang_ = math.degrees(math.acos(max(-1, min(1, (v_[0] * fx_ + v_[1] * fz_) / nv))))
                frente[int(ang_ // 30) * 30] = frente.get(int(ang_ // 30) * 30, 0) + 1
        prev_p = (qx_, qz_)
        t25 = c.r16(P.MGW + 0x25A)
        tipos[t25] = tipos.get(t25, 0) + 1
        yb = int(P.b2f(c.r32(P.PIC + 0x644)) // 20) * 20
        alturas[yb] = alturas.get(yb, 0) + 1
        px, pz = P.b2f(o['p'][0]), P.b2f(o['p'][2])
        hb = int(math.hypot(px - jx, pz - jz) // 100) * 100
        dists[hb] = dists.get(hb, 0) + 1
        # fase 1 (7/11): en el original acaba cuando el JUGADOR pasa al estado 9 (dorado del tick: cuadro 20344, Piccolo
        # a 11,7 u en 3D bajando en vuelo); la distancia horizontal tambien cuenta las pasadas por encima. 3D < 14.
        dh = math.dist((px, P.b2f(o['p'][1]), pz), (jx, P.b2f(jug[0][1]), jz))
        je = c.r16(P.JUG + 0xCBC)
        if je != 1:
            print('campo', n, 'el jugador pasa al estado', je, 'dist3d', round(dh, 1))
        if a.traza and a.traza[0] <= n < a.traza[1]:
            print('  t', n, 'est', o['estado'], 'choques', [(k[0], k[1], [round(P.b2f(x), 1) for x in k[2]]) for k in esp.col],
                  'ev', [hex(x) for x in esp.ev])
        if n % 250 == 0 or (a.traza and a.traza[0] <= n < a.traza[1] and n % 4 == 0):
            print('campo', n, 'estado', o['estado'], 'pos', round(px, 1), round(P.b2f(o['p'][1]), 1), round(pz, 1), 'dist', round(dh, 1))
        if dh < 14:
            print('ALCANZA en el campo', n, '= %.1f s' % (n * 1.2 / 60), 'desde el primer 13:', None if primer13 is None else n - primer13, 'modos de tarea', modos, '(estado, vuela)', sorted(vuela.items()), 'estados', estados)
            return
    print('mgw+0x25A tras el tick', sorted(tipos.items()), 'rayos de vision (choca: veces)', sorted(vis.items()), 'tarea activa', sorted(con_tarea.items()), 'frente vs desplazamiento', sorted(frente.items()))
    print('y640', sorted(alturas.items()), 'dist horizontal', sorted(dists.items()))
    print('NO alcanza en', a.campos, 'campos (%.1f s)' % (a.campos * 1.2 / 60), 'modos de tarea', modos, '(estado, vuela)', sorted(vuela.items()), 'estados', estados)


if __name__ == '__main__':
    main()
