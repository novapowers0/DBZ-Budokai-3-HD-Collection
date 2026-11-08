# SPDX-License-Identifier: MIT
"""Fase 1 (7/11): cuanto tarda el Piccolo ORIGINAL del 0x7DD en alcanzar a un jugador QUIETO, con varias semillas.

PCSX2 del pool (solo lee la RAM y escribe la semilla de Memory 0x49BE08): carga el savestate del 0x7DD (slot 43 de
pool_estados), espera a que Piccolo entre (estado 13 en +0xCBC), pone una semilla y cuenta cuadros (contador 0x4A4D40,
50 por segundo en EU) hasta que el JUGADOR pasa al estado 9 (atrapado; +0xCBC) o se acaba el
limite. Sin teclas: el jugador no se mueve. Escribe una linea JSON por partida en --sal.
Uso: python tools/iw_mips/alcance7dd.py [--pool 3] [--n 8] [--limite 150] [--sal E:/.../alcance7dd.jsonl]
"""
import argparse
import json
import math
import random
import struct
import subprocess
import sys
import time
from pathlib import Path

AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI.parent))
import dif_grabar_pool as G  # noqa: E402

CNT, SEMILLA, P_MGR = 0x4A4D40, 0x49BE08, 0x49CA70


def leer(p, mgr, con_jugador=False):
    v = p.lote([(mgr + 0x800 + 0x40 + 4 * i, 4) for i in range(3)] + [(mgr + 0x28D0 + 0x40 + 4 * i, 4) for i in range(3)]
               + [(mgr + 0x28D0 + 0xCBC, 4), (mgr + 0x800 + 0xCBC, 4)])
    f = lambda w: struct.unpack('<f', struct.pack('<I', w & 0xFFFFFFFF))[0]   # noqa: E731
    s16 = lambda w: struct.unpack('<h', struct.pack('<I', w)[:2])[0]   # noqa: E731
    r = tuple(f(x) for x in v[0:3]), tuple(f(x) for x in v[3:6]), s16(v[6])
    return r + (s16(v[7]),) if con_jugador else r


def partida(p, semilla, limite):
    t0 = time.time()
    mgr = 0
    while time.time() - t0 < 400:   # entrada de Piccolo (el savestate tarda ~70 s en arrancar y ~60 s en llegar)
        mgr = p.u32(P_MGR)
        if 0x100000 <= mgr < 0x2000000 and leer(p, mgr)[2] == 13:
            break
        time.sleep(0.05)
    else:
        return dict(semilla=semilla, error='Piccolo no entra')
    if semilla is None:   # la del savestate tal cual (la del dorado de ~42 s)
        semilla = p.u32(SEMILLA)
    else:
        p.escribir(SEMILLA, struct.pack('<I', semilla))
    c0 = p.u32(CNT)
    estados, minimo = {}, 9e9
    while True:
        j, q, est, je = leer(p, mgr, True)
        c = p.u32(CNT)
        d = math.dist(j, q)
        minimo = min(minimo, d)
        estados[est] = estados.get(est, 0) + 1
        if je == 9:   # el jugador atrapado (dorado del tick: estado 9 en el cuadro 20344, Piccolo a 11,7 u en 3D)
            return dict(semilla=semilla, alcanza=True, cuadros=c - c0, seg=round((c - c0) / 50, 1), estado=est, dist3d=round(d, 1),
                        jugador=[round(x, 1) for x in j], estados=estados)
        if (c - c0) / 50 > limite:
            return dict(semilla=semilla, alcanza=False, cuadros=c - c0, seg=round((c - c0) / 50, 1), minimo=round(minimo, 1),
                        jugador=[round(x, 1) for x in j], estados=estados)
        time.sleep(0.05)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pool', type=int, default=3)
    ap.add_argument('--n', type=int, default=8)
    ap.add_argument('--limite', type=float, default=150)
    ap.add_argument('--sal', default=r'E:\DBZ3HD_trabajo\pendientes_0711\P1\alcance7dd.jsonl')
    ap.add_argument('--semilla0', type=lambda x: int(x, 0), default=0x7DD0001)
    ap.add_argument('--con-savestate', action='store_true', help='primero una partida con la semilla del savestate')
    a = ap.parse_args()
    est = G.estado_por_defecto('0x7DD')
    pool = [sys.executable, str(AQUI.parent / 'pcsx2_pool.py')]
    import pcsx2_pool as PP
    import pcsx2_sonda as S
    rnd = random.Random(a.semilla0)
    # la primera, con la semilla del savestate (la del dorado de ~42 s), para comprobar que se reproduce
    sems = ([None] if a.con_savestate else []) + [rnd.getrandbits(32) for _ in range(a.n)]
    for sem in sems:   # una partida por arranque: la carga por PINE (slot 43) no vale
        subprocess.run(pool + ['preparar', str(a.pool), '--estado', str(est)], check=True, timeout=120, capture_output=True)
        subprocess.run(pool + ['lanzar', str(a.pool), '--iso', G.ISO_EU, '--estado', str(est), '--ejecutar'], check=True,
                       timeout=120, capture_output=True)
        pid = G.pid_del_pool(a.pool)
        try:
            PP.registrar_en_sonda(S, a.pool)
            p = None
            t = time.time()
            while time.time() - t < 300:
                try:
                    p = S.Pine()
                    x = p.u32(CNT); time.sleep(2)
                    if p.u32(CNT) != x:
                        break
                except Exception:   # noqa: BLE001
                    p = None
                    time.sleep(4)
            try:
                p.s.settimeout(30)
                r = partida(p, sem, a.limite)
            except Exception as ex:   # noqa: BLE001  (PINE se corta a veces: se apunta y se sigue con la siguiente)
                r = dict(semilla=sem, error=repr(ex)[:120])
            r['semilla_savestate'] = sem is None
            r['pool'] = a.pool
            r['t'] = time.strftime('%H:%M:%S')
            print(json.dumps(r), flush=True)
            with open(a.sal, 'a', encoding='utf-8') as fo:
                fo.write(json.dumps(r) + chr(10))
        finally:
            if pid:
                subprocess.run(['taskkill', '/F', '/PID', str(pid)], capture_output=True)
                time.sleep(2)
            subprocess.run(pool + ['liberar', str(a.pool)], timeout=60, capture_output=True)


if __name__ == '__main__':
    main()
