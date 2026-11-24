"""Prepara guiones cortos de tablero B2; nunca lanza el juego."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tomllib

TOOLS = Path(__file__).resolve().parent
MODOS = ('pm23', 'radar', 'cierre11', 'shenron')


def guion(modo, fase, ps2_games):
    esperar_carga = [f'log "tablero b2: fase {fase} leida" 180']
    env = {'DBZ3_PS2_GAMES': str(ps2_games),
           'DBZ3_TEST_MISIONES': f'1,auto,reset,desde={fase-1}'}
    cfg = ['dbz3_language=5']
    if modo in ('pm23', 'radar'):
        env.update(DBZ3_TEST_B2_SIN_MOVIMIENTO='1', DBZ3_TEST_B2_STATE='1',
                   DBZ3_TEST_B2_ENTRADA=f'S{fase}' if modo == 'pm23' else f'S{fase}_radar')
        pasos = esperar_carga + [r'log "dbz3 b2 state: .*carga" 180']
        if modo == 'radar':
            env.update(DBZ3_TEST_B2_RADAR='1', DBZ3_TEST_B2_BOLAS='1')
            pasos += ['log "tablero b2: B2-P07b: acaban los guiones del turno 1" 90',
                      'esperar 1500', 'captura radar_activo']
    else:
        cfg += ['dbz3_b2_shenron=true', 'dbz3_b2_shenron_original=true', 'dbz3_b2_fx_nativo=true']
        env.update(DBZ3_TEST_B2_BOLAS='127', DBZ3_TEST_B2_DESEO='2', DBZ3_TEST_B2_DESEO_RNG='1')
        if modo == 'cierre11':
            env.update(DBZ3_TEST_B2_CIERRE='11', DBZ3_TEST_B2_IRIS='4', DBZ3_TEST_B2_SIN_MOVIMIENTO='1')
            pasos = esperar_carga + ['log "tablero b2: CIERRES escena 11" 120', 'captura cierre11',
                                      'log "PUL-B2: Shenron UV 0xF0" 120', 'captura luz',
                                      'esperar 19000', 'captura tras_aparicion']
        else:
            env.update(DBZ3_TEST_MISIONES='1,reset,desde=8', DBZ3_TEST_SHEN_T='4,9.5,16,22')
            pasos = ['log "tablero b2: Shenron 3D:" 180', 'esperar 1200', 'captura bolas_4s']
            for toma in ('luz_9_5s', 'dragon_16s', 'cielo_22s'):
                pasos += ['tecla Space 1000', f'captura {toma}']
    env['DBZ3_TEST_CFG'] = ';'.join(cfg)
    return dict(descripcion=f'B2 {modo}, fase {fase}', turbo=False,
                env=[f'{k}={v}' for k, v in env.items()], pasos=pasos)


def texto(d):
    # Las cadenas JSON ASCII de este esquema son también cadenas TOML básicas.
    # UTF-8 literal evita que una ruta con emoji se convierta en pares sustitutos JSON.
    quote = lambda x: json.dumps(x, ensure_ascii=False)
    return (f'descripcion = {quote(d["descripcion"])}\nturbo = false\n'
            + ''.join(f'{k} = [\n' + ''.join(f'  {quote(x)},\n' for x in d[k]) + ']\n'
                      for k in ('env', 'pasos')))


def selfcheck():
    """Comprueba la generacion TOML de los 4 modos sin escribir disco ni abrir el juego."""
    ruta = Path('D:/Prueba Con Espacios/ps2_games')
    for modo, fase in (('pm23', 1), ('pm23', 7), ('radar', 1), ('cierre11', 9), ('shenron', 9)):
        d = guion(modo, fase, ruta)
        if d['turbo'] is not False or tomllib.loads(texto(d)) != d:
            raise ValueError(f'roundtrip TOML invalido en {modo} fase {fase}')
        if not d['env'] or not d['pasos']:
            raise ValueError(f'guion vacio en {modo}')
    print('b2_escenario autoprueba: OK (4 modos / 5 combinaciones TOML a x1)')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--self-check', '--autoprueba', dest='self_check', action='store_true',
                   help='comprueba la generacion TOML de los 4 modos sin escribir disco ni abrir el juego')
    p.add_argument('modo', nargs='?', choices=MODOS)
    p.add_argument('--fase', type=int, choices=range(1, 10), help='1–9; cierre11 y shenron usan IX')
    p.add_argument('--ps2-games', type=Path, default=Path(os.getenv('DBZ3_PS2_GAMES', 'D:/DBZ3HD/ps2_games')))
    p.add_argument('--salida', type=Path, help='TOML nuevo, fuera de las instalaciones de juego')
    p.add_argument('--preflight', action='store_true', help='valida el TOML con pruebas.py sin arrancar nada')
    a = p.parse_args(argv)
    if a.self_check:
        if a.modo is not None or a.salida is not None:
            p.error('--self-check no se combina con modo ni --salida')
        selfcheck()
        return 0
    if a.modo is None or a.salida is None:
        p.error('the following arguments are required: modo, --salida')
    fijo9 = a.modo in ('cierre11', 'shenron')
    fase = a.fase or (9 if fijo9 else 1)
    if fijo9 and fase != 9:
        p.error(f'{a.modo} requiere fase 9')
    if a.salida.suffix.lower() != '.toml':
        p.error('--salida debe terminar en .toml')
    if any(ord(ch) < 32 or ord(ch) == 127 for ch in str(a.ps2_games)):
        p.error('--ps2-games contiene un carácter de control')
    d = guion(a.modo, fase, a.ps2_games)
    t = texto(d)
    if tomllib.loads(t) != d:
        raise ValueError('el TOML no conserva el guion')
    try:
        a.salida.parent.mkdir(parents=True, exist_ok=True)
        with a.salida.open('x', encoding='utf-8', newline='\n') as f:
            f.write(t)
    except OSError as e:
        p.error(str(e))
    print(a.salida, flush=True)
    if a.preflight:
        return subprocess.run([sys.executable, '-B', str(TOOLS / 'pruebas.py'), 'preflight',
                               str(a.salida.resolve())], cwd=TOOLS.parent, timeout=90).returncode
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
