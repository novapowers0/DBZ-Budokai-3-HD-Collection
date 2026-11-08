"""dsSERP: ¿la niebla de la entrada alcanza al cielo/nubes del campo 884?

Un solo arranque (copia fija smoke_b) con la linea `niebla` del stage.toml cambiada SOLO
de color (00FF00, mismo 1700..2000). Si el cielo o las nubes se ponen verdes, la niebla
del escenario los alcanza; si no, no. Restaura el stage.toml al cerrar el juego.

  python tools/dsSERP_niebla.py [--color 00FF00] [--ini 1700] [--fin 2000]
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import time

import pruebas as P
import solcam_pruebas as S

HOME = Path('D:/tmp/dsSERP')


def sha(b):
    return hashlib.sha256(b).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--color', default='00FF00')
    ap.add_argument('--ini', type=float, default=1700.0)
    ap.add_argument('--fin', type=float, default=2000.0)
    a = ap.parse_args()
    if not re.fullmatch(r'[0-9A-Fa-f]{6}', a.color):
        ap.error('color RRGGBB')
    os.environ.update(DBZ3_AGENTE='dsSERP', PYTHONUTF8='1')
    out = HOME/f'niebla_{time.time_ns()}'
    out.mkdir(parents=True, exist_ok=True)
    print('EVIDENCIA', out, flush=True)
    estado = {}

    def env(par):
        return {**S.entorno(par), 'DBZ3_TEST_INTRO_SONDA': '1', 'DBZ3_HUB_MODE': '1'}

    def preparar(j, _par, _env):
        ruta = j.dest/'mods'/'Campo IW 884'/'stage.toml'
        antes = ruta.read_bytes()
        texto = antes.decode('utf-8')
        linea = 'niebla = "%s,%.0f,%.0f"' % (a.color.upper(), a.ini, a.fin)
        nuevo, n = re.subn(r'(?m)^niebla\s*=\s*"[^"]+"', linea, texto)
        if n != 1:
            raise ValueError('el stage.toml del 884 no tiene una sola linea niebla (%d)' % n)
        (out/'stage_antes.toml').write_bytes(antes)
        (out/'stage_despues.toml').write_text(nuevo, encoding='utf-8')
        ruta.write_text(nuevo, encoding='utf-8')
        estado.update(ruta=str(ruta), sha_antes=sha(antes), sha_despues=sha(ruta.read_bytes()),
                      linea=linea)
        cerrar = j.cerrar

        def cerrar_y_restaurar():
            cerrar()
            if j.proc is not None and j.proc.poll() is None:
                raise RuntimeError('juego propio vivo: no restauro')
            if sha(ruta.read_bytes()) != estado['sha_despues']:
                raise RuntimeError('stage.toml cambiado por otro proceso; respaldo en la evidencia')
            ruta.write_bytes(antes)
            estado['restaurado'] = ruta.read_bytes() == antes
            (out/'restauracion.json').write_text(json.dumps(estado, indent=1), encoding='utf-8')
        j.cerrar = cerrar_y_restaurar

    def jugar(j, par):
        S.ejecutar(j, par)

    clave = 'dsSERP_niebla884'
    P.ESCENARIOS[clave] = ('dsSERP: niebla teñida en el campo 884', env, jugar, preparar)
    r = P.correr('smoke_b', clave, [], None,
                 {'modo': 'observar', 'ids': '0,7,30', 'seg': '2'}, True)
    if not estado.get('restaurado'):
        r['ok'] = False
        r['msg'] = (r['msg']+'; ' if r['msg'] else '')+'no consta restauracion del stage.toml'
    (out/'resultado.json').write_text(json.dumps(r, indent=1, default=str), encoding='utf-8')
    print(P.tabla([r]), flush=True)
    return 0 if r['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
