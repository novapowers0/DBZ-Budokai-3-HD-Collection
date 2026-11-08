"""Bubbles 7D1: el jugador se acerca de verdad y se mide la maquina.

Solo herramienta de prueba. Por defecto mantiene **W** (acercarse al sitio
real de Bubbles); con `--gp2` activa la maquina candidata (igual que
iwx_bubbles_focal) y entonces el juego escribe el CSV que se analiza. Sin `--gp2`
solo se cuentan las caidas/reapariciones del log. Sirve para medir el recorrido
del jugador y la mezcla de estados segun la distancia. No toca el producto.
"""
import argparse
import csv
import json
import math
import os
import time
from pathlib import Path

import pruebas as P
from iwx_bubbles_focal import FUENTE, HOME, analizar_csv, pantalla2


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tecla', default='W')
    ap.add_argument('--ms', type=int, default=12000)
    ap.add_argument('--clave', default='0x7D1')
    ap.add_argument('--gp2', action='store_true', help='activar la maquina candidata de Bubbles (solo 0x7D1)')
    ap.add_argument('--esperar', type=int, default=0,
                    help='segundos de juego adicionales sin pulsar nada (p. ej. 250 para el final por tiempo)')
    ap.add_argument('--auto', action='store_true',
                    help='piloto automatico del port (teletransporta a los objetos); NO usar para medir el andar')
    a = ap.parse_args()
    os.environ['DBZ3_AGENTE'] = 'nIW'
    os.environ['PYTHONUTF8'] = '1'
    out = HOME/f'bubbles_caza_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA', out, flush=True)
    pantalla2()

    def env(_):
        e = dict(DBZ3_TEST_IWMINI=a.clave + (',auto' if a.auto else ''), DBZ3_PS2_GAMES=FUENTE)
        if a.gp2:
            e.update(DBZ3_TEST_GP2_BUBBLES='1', TEST_GP2_BB_CSV=str(out/'bubbles.csv'))
        return e

    def jugar(j, _):
        j.log(r'entrada acabada: mando al jugador', 200)
        P.dormir(.4)
        j.captura('a_inicio')
        if a.ms > 0:
            j.tecla(f'{a.tecla} {a.ms}', a.ms+800)
        if a.esperar > 0:
            P.dormir(float(a.esperar))
        P.dormir(.4)
        j.captura('b_final')

    clave = 'nIW_bubbles_caza'
    P.ESCENARIOS[clave] = (f'caminar {a.clave} con {a.tecla} y medir el recorrido', env, jugar)
    r = P.correr('smoke_b', clave, [], None, {'timeout': '400'}, True)
    resumen = {}
    try:
        csvp = out/'bubbles.csv'
        if not csvp.exists():
            # Sin maquina: el recorrido se mide en el log (no hay CSV).
            log = (Path(r['run'])/'dbz3.log').read_text(encoding='utf-8', errors='replace')
            resumen = dict(caidas=log.count('caida en ('),
                           atrapado='Bubbles atrapado' in log, tecla=a.tecla, ms=a.ms,
                           clave=a.clave, gp2=a.gp2, auto=a.auto)
            with (out/'resumen.json').open('x', encoding='utf-8') as f:
                json.dump(resumen, f, indent=1, ensure_ascii=False)
            print('CAIDAS', resumen['caidas'])
            with (out/'resultado.json').open('x', encoding='utf-8') as f:
                json.dump(dict(resultado=r, resumen=resumen), f, indent=1, default=str, ensure_ascii=False)
            print(P.tabla([r]), flush=True)
            return 0 if r['ok'] else 1
        resumen = analizar_csv(csvp)
        filas = list(csv.DictReader(csvp.open(encoding='utf-8')))
        # Distancia al jugador con la que empieza y con la que acaba.
        d0 = math.dist([float(filas[0][k]) for k in ('x', 'y', 'z')],
                       [float(filas[0][k]) for k in ('jugadorx', 'jugadory', 'jugadorz')])
        d1 = math.dist([float(filas[-1][k]) for k in ('x', 'y', 'z')],
                       [float(filas[-1][k]) for k in ('jugadorx', 'jugadory', 'jugadorz')])
        resumen.update(distancia_inicio=round(d0, 1), distancia_fin=round(d1, 1),
                       atrapado='Bubbles atrapado' in (Path(r['run'])/'dbz3.log').read_text(
                           encoding='utf-8', errors='replace'),
                       tecla=a.tecla, ms=a.ms)
        with (out/'resumen.json').open('x', encoding='utf-8') as f:
            json.dump(resumen, f, indent=1, ensure_ascii=False)
    except (OSError, ValueError, KeyError) as e:
        r['ok'] = False
        r['msg'] = (r['msg']+'; ' if r['msg'] else '')+str(e)
    with (out/'resultado.json').open('x', encoding='utf-8') as f:
        json.dump(dict(resultado=r, resumen=resumen), f, indent=1, default=str, ensure_ascii=False)
    print('ESTADOS', resumen.get('estados'), 'DIST', resumen.get('distancia_inicio'),
          '->', resumen.get('distancia_fin'), 'atrapado', resumen.get('atrapado'))
    print(P.tabla([r]), flush=True)
    return 0 if r['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
