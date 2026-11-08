"""7D7 (Freezer): cadencia y variantes de la esquiva, medidas en el juego.

Solo herramienta de prueba. Repite el minijuego con el piloto `...,auto` hasta
que sale al menos una esquiva (la tirada B50 puede fallar y el piloto mantiene
la mira). Lee del log las lineas de diagnostico del port: tirada, inicio
(direccion 0..3, `40 + azar%80` cuadros) y fin (cuadros vividos y salida).
No escribe memoria ni toca el producto.
"""
import argparse
import json
import os
import re
import time
from pathlib import Path

import pruebas as P
from iwx_bubbles_focal import FUENTE, HOME, pantalla2

RE_TIRADA = re.compile(r'esquiva tirada B50 (\d+) \((\d+)\), (sale|no sale)')
RE_INICIO = re.compile(r'esquiva inicio dir (\d+) cuadros (\d+)')
RE_FIN = re.compile(r'esquiva fin dir (\d+) vivida (\d+) de (\d+) fuera (true|false)')


def analizar(texto, intentos):
    tiradas = [dict(b50=int(m.group(1)), r=int(m.group(2)), sale=m.group(3) == 'sale')
               for m in RE_TIRADA.finditer(texto)]
    inicios = [dict(dir=int(m.group(1)), cuadros=int(m.group(2))) for m in RE_INICIO.finditer(texto)]
    fines = [dict(dir=int(m.group(1)), vivida=int(m.group(2)), pedida=int(m.group(3)), fuera=m.group(4) == 'true')
             for m in RE_FIN.finditer(texto)]
    if not inicios:
        raise ValueError(f'sin esquivas en {intentos} intento(s); tiradas={len(tiradas)}')
    if len(fines) != len(inicios):
        raise ValueError(f'{len(inicios)} esquivas y {len(fines)} cierres')
    if any(not 40 <= i['cuadros'] <= 119 for i in inicios):
        raise ValueError(f'duracion pedida fuera de 40..119: {[i["cuadros"] for i in inicios]}')
    if any(not 1 <= f['vivida'] <= f['pedida'] or f['pedida'] != i['cuadros']
           for f, i in zip(fines, inicios)):
        raise ValueError('cierre incoherente con el inicio')
    variantes = {str(d): sum(1 for i in inicios if i['dir'] == d) for d in range(4)}
    return dict(intentos=intentos, tiradas=len(tiradas),
                exits=sum(1 for t in tiradas if t['sale']),
                inicios=len(inicios), fines=len(fines),
                pedidos=[i['cuadros'] for i in inicios],
                vividos=[f['vivida'] for f in fines],
                variantes=variantes, paridad_original=False,
                limite='Solo el port: 40..119/B50/B48 salen del ELF; el reparto de variantes es de '
                       'las esquivas observadas, no de una pareja PS2 sincronizada')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ms', type=int, default=18000, help='juego por intento')
    ap.add_argument('--intentos', type=int, default=5)
    ap.add_argument('--log', type=Path, help='analizar un dbz3.log ya cerrado (no lanza el juego)')
    a = ap.parse_args()
    if a.log:
        medida = analizar(a.log.read_text(encoding='utf-8', errors='replace'), 0)
        print(json.dumps(medida, indent=1, ensure_ascii=False))
        return 0
    os.environ['DBZ3_AGENTE'] = 'tIW'
    os.environ['PYTHONUTF8'] = '1'
    out = HOME / f'esquiva_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA', out, flush=True)
    pantalla2()
    medida, r = {}, None
    for intento in range(1, a.intentos + 1):
        clave = f'tIW_esquiva_focal_{intento}'

        def env(_):
            return dict(DBZ3_TEST_IWMINI='0x7D7,auto', DBZ3_PS2_GAMES=FUENTE)

        def jugar(j, _):
            j.log(r'iwdisparo: juego .* empieza', 200)
            P.dormir(a.ms / 1000)
            j.captura('fin')

        P.ESCENARIOS[clave] = (f'7D7: {a.ms} ms de esquivas (intento {intento})', env, jugar)
        r = P.correr('smoke_b', clave, [], None, {'timeout': '420'}, True)
        if not r['ok']:
            print(P.tabla([r]), flush=True)
            continue
        texto = (Path(r['run']) / 'dbz3.log').read_text(encoding='utf-8', errors='replace')
        try:
            medida = analizar(texto, intento)
            break
        except ValueError as exc:
            print('INTENTO', intento, ':', exc, flush=True)
    with (out / 'medida.json').open('x', encoding='utf-8') as f:
        json.dump(medida, f, indent=1, ensure_ascii=False)
    if r:
        with (out / 'resultado.json').open('x', encoding='utf-8') as f:
            json.dump(r, f, indent=1, default=str, ensure_ascii=False)
        print(P.tabla([r]), flush=True)
    print(json.dumps(medida, indent=1, ensure_ascii=False), flush=True)
    return 0 if medida else 1


if __name__ == '__main__':
    raise SystemExit(main())
