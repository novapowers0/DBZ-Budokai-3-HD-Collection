# SPDX-License-Identifier: MIT
"""Dorado de MSN019 (IW EU 0x7E1) en PCSX2: solo LECTURA de RAM por PINE (plaza pool_N ya lanzada).

Rehecho por N2 (7/11) a partir de D:/tmp/dia1E3/cap_7e1.py (dia1E3, borrado con D:/tmp). Lee en bucle la semilla de
Memory() y los objetos UI de la fila (0x11..0x11+N) y guarda cada cambio con las ultimas semillas vistas.
Uso: python tools/iw_mips/cap_7e1.py N --salida <carpeta> [--seg 330] [--cargar 47]
"""
import argparse
import importlib.util
import json
import sys
import time
from collections import deque
from pathlib import Path

R = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(R))
import pcsx2_pool as PP  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('n', type=int)
    ap.add_argument('--salida', required=True)
    ap.add_argument('--seg', type=float, default=330)
    ap.add_argument('--cargar', type=int)
    a = ap.parse_args()
    out = Path(a.salida); out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('_sonda_e3', R / 'pcsx2_sonda.py')
    s = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)
    PP.registrar_en_sonda(s, a.n)
    f = (out / 'filas.jsonl').open('w', encoding='utf-8')
    try:
        p = s.Pine(PP.puerto(a.n), espera=200)
        t1 = time.time()
        while time.time() - t1 < 150:
            try:
                st = p.status(); c0 = p.u32(0x4A4D40)
                if st == 0 and c0:
                    break
            except OSError as e:
                print('pine', e, flush=True)
            time.sleep(2)
        if a.cargar is not None:
            time.sleep(3); p.cargar(a.cargar); time.sleep(2)
        print('listo', p.status(), hex(p.u32(0x5C0DC8)), flush=True)
        ultimo = 0
        semillas = deque(maxlen=200)
        previo, cfg, t0 = None, None, time.time()
        n_ev = 0
        while time.time() - t0 < a.seg:
            try:
                seed, cd68, cd8c, cnt = p.lote([(0x49BE08, 4), (0x49CD68, 4), (0x49CD8C, 4), (0x4A4D40, 4)])
            except OSError:
                time.sleep(0.2)
                continue
            semillas.append((round(time.time() - t0, 4), cnt, seed))
            if time.time() - ultimo > 10:
                ultimo = time.time(); print('t', round(time.time() - t0), 'cnt', cnt, 'eventos', n_ev, flush=True)
            if not (cd68 and cd8c):
                time.sleep(0.002)
                continue
            N, fila, mask, dif = p.lote([(cd68 + 0x66, 2), (cd68 + 0x68, 2), (cd68 + 0x64, 2), (cd68 + 0x114, 4)])
            if not (0 < N <= 12):
                continue
            if cfg is None:
                g = lambda ad, t=4: p.lote([(ad, t)])[0]
                cfg = dict(N=N, mask=mask, dif=dif, pasos=g(0x5D9162, 2), y=g(0x5D90D8, 2), d917c=g(0x5D917C, 2),
                           filas=g(0x5D91C4), ancho_tabla=[g(0x5D9164 + 4 * i) for i in range(6)], W=g(0x49C84C), H=g(0x49C850),
                           cd90=g(0x49CD90), cd68=cd68, cd8c=cd8c)
                cfg['rect'] = [p.lote([(cd8c + k * 400 + o, 4) for o in (0x30, 0x34, 0x38, 0x3C, 0x150, 0x40)])
                               for k in range(0x11, 0x11 + N)]
                f.write(json.dumps(dict(cfg=cfg)) + '\n')
            pares = []
            for k in range(0x11, 0x11 + N):
                b = cd8c + k * 400
                pares += [(b + 0x20, 4), (b + 0x24, 4), (b + 0x44, 4), (b + 0x48, 4), (b + 0x4C, 4), (b + 0x150, 4)]
            v = p.lote(pares)
            seed2, cnt2 = p.lote([(0x49BE08, 4), (0x4A4D40, 4)])
            items = [v[i:i + 6] for i in range(0, len(v), 6)]
            clave = [(it[0], it[2], it[3]) for it in items]
            if clave != previo:
                n_ev += 1
                f.write(json.dumps(dict(t=round(time.time() - t0, 4), cnt=cnt, cnt2=cnt2, seed2=seed2, fila=fila, N=N,
                                        items=items, semillas=list(semillas)[-60:])) + '\n')
                f.flush()
                previo = clave
            time.sleep(0.001)
    finally:
        f.close()
        print('fin', flush=True)


if __name__ == '__main__':
    main()
