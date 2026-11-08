"""Una partida para cotejar el cache corporal B3 con su matriz de hueso real."""
import argparse
import json
import os
import threading
import time

import pruebas as P
from iwx_ancla import leer, analizar_port
from iwx_seguimiento import analizar_port as analizar_seguimiento
from iwx_bubbles_focal import FUENTE, HOME, pantalla2


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--clave', type=lambda x:int(x, 0), choices=(0x7D0, 0x7D1, 0x7D2), default=0x7D0)
    ap.add_argument('--candidato', action='store_true', help='Cotejar la nueva publicacion, no exigir cache antiguo actual')
    a = ap.parse_args()
    os.environ['DBZ3_AGENTE'] = 'solIWX'
    os.environ['PYTHONUTF8'] = '1'
    out = HOME / f'ancla_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA', out, flush=True)
    pantalla2()
    samples, errors = [], []

    def env(_):
        variables = dict(DBZ3_TEST_IWMINI=hex(a.clave), DBZ3_PS2_GAMES=FUENTE)
        if a.candidato:
            variables.update(TEST_IWX_SEGUIMIENTO=str(out/'seguimiento.csv'),
                TEST_IWX_ANCLA=str(out/'ancla.csv'))
            if a.clave != 0x7D2:
                variables['TEST_IWX_SEGUIMIENTO_PREVIO'] = '1'
        return variables

    def jugar(j, _):
        j.log(r'entrada acabada: mando al jugador', 190)
        P.dormir(3)
        stop = threading.Event()

        def sample():
            try:
                while not stop.is_set():
                    mgr, n = P._freecam_mem(j)
                    if mgr and n is not None and (not samples or samples[-1]['n'] != n):
                        r = leer(lambda addr, count:P.leer(j.h, P.GBASE+addr, count), mgr+2096, hd=True)
                        _, after = P._freecam_mem(j)
                        if n == after:
                            r['n'] = n
                            samples.append(r)
                    stop.wait(.003)
            except Exception as exc:
                errors.append(repr(exc))

        thread = threading.Thread(target=sample)
        thread.start()
        try:
            j.tecla('W 1800', 2000)
            P.dormir(.7)
            j.tecla('D 1200', 1400)
            P.dormir(.7)
            j.tecla('Backspace 100', 300)
            P.dormir(1)
        finally:
            stop.set()
            thread.join()
        if errors:
            raise P.Fallo('Lectura corporal: '+str(errors))
        if len(samples) < 60:
            raise P.Fallo('Faltan muestras del cuerpo en movimiento')
        if {r['indice'] for r in samples} != {1}:
            raise P.Fallo('El ancla B3 no usa el primer hueso esperado')
        if not a.candidato and max(r['error'] for r in samples) > .001:
            raise P.Fallo('El cache B3 no coincide con la matriz actual del hueso1')

    key = f'solIWX_{a.clave:X}_ancla_corporal'
    P.ESCENARIOS[key] = ('B3: primer ancla contra matriz durante marcha, freno y dash', env, jugar)
    result = P.correr('smoke_b', key, [], None, {'timeout':'350'}, True)
    measure = dict(clave=hex(a.clave), muestras=len(samples), errores_lectura=errors,
        max_error=max((r['error'] for r in samples), default=None),
        indices=sorted({r['indice'] for r in samples}),
        paridad_camara=False, normal_activado=False, filas=samples)
    if a.candidato:
        try:
            measure['publicacion'] = analizar_port(out/'ancla.csv', out/'seguimiento.csv')
            measure['filtro'] = analizar_seguimiento(out/'seguimiento.csv', .3 if a.clave==0x7D2 else .12)
        except (OSError, ValueError, KeyError) as exc:
            result['ok'] = False
            result['msg'] = (result['msg']+'; ' if result['msg'] else '')+str(exc)
    for name, obj in (('medida.json', measure), ('resultado.json', result)):
        with (out/name).open('x', encoding='utf-8') as f:
            json.dump(obj, f, default=str, indent=1, ensure_ascii=False, allow_nan=False)
    print(P.tabla([result]), flush=True)
    print(json.dumps({k:v for k,v in measure.items() if k!='filas'}, indent=1), flush=True)
    return 0 if result['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
