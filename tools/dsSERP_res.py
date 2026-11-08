"""dsSERP #29: una pasada de 'batalla' con el render forzado a ANCHOxALTO.

No cambia nada del producto: usa el hook de prueba DBZ3_TEST_RENDER (src/diagnostico_var.inl).
Captura 01_combate (HUD) y 02_vuelta_menu (menus) y deja la linea `perf` en el log.

  python tools/dsSERP_res.py --render 426x240 [--lento]
"""
import argparse
import json
import os
from pathlib import Path
import time

import pruebas as P


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--render', required=True)
    ap.add_argument('--lento', action='store_true', help='sin turbo xN (para leer fps con sentido)')
    ap.add_argument('--copia', default='smoke_a')
    ap.add_argument('--cvar', action='store_true', help='usa dbz3_low_render (ruta de producto) en vez de DBZ3_TEST_RENDER')
    a = ap.parse_args()
    os.environ.update(DBZ3_AGENTE='dsSERP', PYTHONUTF8='1')
    if a.lento:
        os.environ['DBZ3_PRUEBAS_LENTO'] = '1'
    P.OVERRIDES['dbz3_perf_logging'] = 'true'
    P.OVERRIDES['dbz3_low_render'] = '0' if a.render == 'auto' else a.render
    out = Path('D:/tmp/dsSERP')
    out.mkdir(parents=True, exist_ok=True)

    def env(_par):
        env = {'DBZ3_TEST_BATTLE': '0,7,2', 'DBZ3_TEST_RINGOUT': '1'}
        if not a.cvar and a.render != 'auto':
            env['DBZ3_TEST_RENDER'] = a.render
        return env

    clave = 'dsSERP_render_' + a.render.replace('x', '_')
    P.ESCENARIOS[clave] = ('dsSERP: render %s (#29)' % a.render, env, P._batalla)
    r = P.correr(a.copia, clave, [], None, {}, True)
    (out/('render_%s_%d.json' % (a.render, time.time_ns()))).write_text(
        json.dumps(r, indent=1, default=str), encoding='utf-8')
    print(P.tabla([r]), flush=True)
    return 0 if r['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
