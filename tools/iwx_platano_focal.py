"""Platano y resbalon de 7D1 con platanos NATURALES de Bubbles.

Solo herramienta de prueba. El piloto automatico (`...,auto`) espera
`--retardo` ms sin moverse para que Bubbles tire platanos por el camino; despues
recorre esa lista pisandolos. El resbalon que se mide es el del juego
(`dist < 6 -> 60 cuadros, -25 de energia, anim 0xCA`), no un estado inventado.
No toca el producto: todo va bajo DBZ3_TEST_IWMINI.
"""
import argparse
import json
import os
import re
import time
from pathlib import Path

import pruebas as P
from iwx_bubbles_focal import FUENTE, HOME, pantalla2


def analizar(log, retardo, ms):
    tirados = [l for l in log.splitlines() if 'platano tirado' in l]
    resbalones = [l for l in log.splitlines() if 'platano: resbala' in l]
    anim = [l for l in log.splitlines() if 'anim resbalon' in l]
    energias = [float(m.group(1)) for l in resbalones
                for m in [re.search(r'queda (-?\d+)', l)] if m]
    mision = re.findall(r'platanos de la mision \(grupo D\): (\d+)', log)
    iniciales = int(mision[-1]) if mision else None
    if not resbalones:
        raise ValueError('el piloto no llego a pisar ningun platano')
    if not anim:
        raise ValueError('no se reprodujo la animacion 0xCA del resbalon')
    if not energias or energias[0] > 75.0:
        raise ValueError('la primera pisada no gasto los 25 de energia (energia inicial 100)')
    if any(b > a + 1.01 for a, b in zip(energias, energias[1:])):
        raise ValueError('la energia subio entre resbalones sin ser la regeneracion de 0,5/cuadro')
    return dict(tirados=len(tirados), platanos_mision=iniciales,
                resbalones=len(resbalones), animaciones_8CA=len(anim),
                energias=energias, energia_primera=energias[0], retardo_ms=retardo, observacion_ms=ms,
                primera_pisada=resbalones[0], paridad_original=False,
                limite='Pisada dirigida por el piloto de prueba; no es una partida manual '
                       'ni acredita cadencia/scheduler de Bubbles')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--retardo', type=int, default=14000,
                    help='ms quieto antes de buscar platanos (Bubbles los tira huyendo)')
    ap.add_argument('--ms', type=int, default=12000, help='ms de recorrido de platanos')
    a = ap.parse_args()
    os.environ['DBZ3_AGENTE'] = 'tIW'
    os.environ['PYTHONUTF8'] = '1'
    out = HOME / f'platano_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA', out, flush=True)
    pantalla2()

    def env(_):
        return dict(DBZ3_TEST_IWMINI='0x7D1,auto', DBZ3_TEST_IWMINI_PLATANO=str(a.retardo),
                    DBZ3_PS2_GAMES=FUENTE)

    def jugar(j, _):
        j.log(r'entrada acabada: mando al jugador', 200)
        P.dormir(1.0)
        j.captura('a_inicio')
        P.dormir(a.retardo / 1000 + a.ms / 1000)
        j.captura('b_final')

    clave = 'tIW_platano_focal'
    P.ESCENARIOS[clave] = (f'7D1: platanos naturales y resbalon (retardo {a.retardo} ms)', env, jugar)
    r = P.correr('smoke_b', clave, [], None, {'timeout': '360'}, True)
    log = (Path(r['run']) / 'dbz3.log').read_text(encoding='utf-8', errors='replace')
    medida = {}
    try:
        medida = analizar(log, a.retardo, a.ms)
    except ValueError as exc:
        r['ok'] = False
        r['msg'] = (r['msg'] + '; ' if r['msg'] else '') + str(exc)
    with (out / 'medida.json').open('x', encoding='utf-8') as f:
        json.dump(medida, f, indent=1, ensure_ascii=False)
    with (out / 'resultado.json').open('x', encoding='utf-8') as f:
        json.dump(dict(resultado=r, medida=medida), f, indent=1, default=str, ensure_ascii=False)
    print(P.tabla([r]), flush=True)
    print(json.dumps(medida, indent=1, ensure_ascii=False), flush=True)
    return 0 if r['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
