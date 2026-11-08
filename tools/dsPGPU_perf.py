"""dsPGPU: mide la linea `perf` del runtime en un combate nativo (misma copia, mismo combate).

No cambia nada del producto. Sirve para comparar ajustes de GPU/VRAM antes y despues.

  python tools/dsPGPU_perf.py --etiqueta base --cfg "dbz3_hd_textures=1"
  python tools/dsPGPU_perf.py --etiqueta hd3 --cfg 'dbz3_hd_textures=3;dbz3_hd_texture_max_texels=0'
  python tools/dsPGPU_perf.py --etiqueta 240p --render 426x240
  python tools/dsPGPU_perf.py --etiqueta menu --escena menu

--cfg usa la misma sintaxis que DBZ3_TEST_CFG (cvar=valor;cvar="texto") y se escribe en el
dbz3_user.toml de la copia (el resto de ajustes de prueba son los de pruebas.py).
"""
import argparse
import json
import os
import re
from pathlib import Path
import time

import pruebas as P

# Base explicita de cada medida: la copia de pruebas conserva el toml entre corridas, asi que
# cada pasada fija TODOS los ajustes que afectan a GPU/VRAM (lo que ponga --cfg gana).
BASE_CFG = {
    'dbz3_quality_preset': '"manual"',       # que el preset automatico no pise escala/MSAA/aniso
    'dbz3_resolution_scale': '1',
    'dbz3_native_2x_msaa': 'false',
    'dbz3_anisotropic': '0',
    'dbz3_present_effect': '"fsr"',
    'dbz3_hd_textures': '1',
    'dbz3_hd_texture_max_texels': '524288',
    'dbz3_low_res': 'false',
    'dbz3_low_render': '0',
    'dbz3_relight': '0',
    'dbz3_fxaa': '"none"',
    'dbz3_present_dither': 'false',
    'texture_cache_memory_limit_soft': '384',
    'texture_cache_memory_limit_hard': '768',
}


def _cfg(extra=''):
    cfg = dict(BASE_CFG)
    for trozo in filter(None, (x.strip() for x in extra.split(';'))):
        k, _, v = trozo.partition('=')
        cfg[k.strip()] = v.strip()
    return ';'.join(f'{k}={v}' for k, v in cfg.items())


PERF = re.compile(
    r"perf fps=(?P<fps>[\d.]+) p1=(?P<p1>[\d.]+) p99_ms=(?P<p99>[\d.]+).*?"
    r"cfg=scale:(?P<scx>\S+)x(?P<scy>\S+).*?msaa:(?P<msaa>\S+).*?hdtex:(?P<hdtex>\S+).*?"
    r"aniso:(?P<aniso>\S+).*?upx=(?P<upx>\d+).*?upx_dyn=(?P<upxdyn>\d+).*?"
    r"texload=(?P<texload>\d+).*?vram=(?P<vram>\d+)MB/(?P<vramb>\d+)MB.*?"
    r"lim=(?P<lim>\d+).*?gpu_wait=(?P<gpu>[\d.]+)ms/f.*?cp_wait=(?P<cp>[\d.]+)ms/f")


def _batalla_larga(j, par):
    # Como _batalla, pero sin ring out y con una ventana de pelea larga para tener
    # varias lineas `perf` (una cada 5 s) del MISMO combate.
    j.log(r"hub: combate nativo", 60)
    P._esperar_pelea(j, 90)
    P.dormir(float(par.get("seg", "14")))
    j.captura("fin")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--etiqueta', required=True)
    ap.add_argument('--cfg', default='')
    ap.add_argument('--render', default='', help='DBZ3_TEST_RENDER=ANCHOxALTO (solo D3D12)')
    ap.add_argument('--escena', default='batalla', choices=['batalla', 'menu'])
    ap.add_argument('--copia', default='smoke_a')
    ap.add_argument('--seg', default='14')
    ap.add_argument('--instantanea', default=None)
    ap.add_argument('--sin-base', action='store_true', help='no fija BASE_CFG (solo lo de --cfg)')
    ap.add_argument('--ventana', default='', help='ANCHOxALTO de la ventana (prueba el pase de presentacion)')
    a = ap.parse_args()

    os.environ.update(DBZ3_AGENTE='dsPGPU', PYTHONUTF8='1')
    os.environ['DBZ3_PRUEBAS_LENTO'] = '1'          # sin turbo: los FPS y las esperas tienen sentido
    P.OVERRIDES['dbz3_perf_logging'] = 'true'
    P.OVERRIDES['dbz3_skip_launcher'] = 'true'
    os.environ['DBZ3_TEST_CFG'] = a.cfg if a.sin_base else _cfg(a.cfg)

    if a.escena == 'batalla':
        def env(_par):
            e = {'DBZ3_TEST_RINGOUT': '0', 'DBZ3_TEST_BATTLE': '0,7,2'}
            if a.render:
                e['DBZ3_TEST_RENDER'] = a.render
            return e
        P.ESCENARIOS['pgpu_batalla'] = ('pGPU: combate nativo largo', env, _batalla_larga)
        clave = 'pgpu_batalla'
    else:
        P.ESCENARIOS['pgpu_menu'] = ('pGPU: menu principal', lambda p: {}, P._menu)
        clave = 'pgpu_menu'

    par = {'seg': a.seg}
    if a.ventana:
        par['ventana'] = a.ventana
    r = P.correr(a.copia, clave, [], a.instantanea, par, True)

    filas = []
    dentro = a.escena != 'batalla'
    if r.get('run'):
        # Solo las ventanas del combate (desde 'hub: combate nativo' hasta su fin).
        for sl, l in P._lineas_log(r['run']):
            if not dentro and 'hub: combate nativo' in l:
                dentro = True
            if dentro and 'hub: fin del combate nativo' in l:
                dentro = False
            m = PERF.search(l)
            if m and dentro:
                d = {}
                for k, v in m.groupdict().items():
                    try:
                        d[k] = float(v) if '.' in v else int(v)
                    except ValueError:
                        d[k] = v
                d['hora'] = sl.split()[0] if sl else ''
                filas.append(d)

    # Estado estacionario: las 3 ultimas ventanas (la VRAM sube mientras carga).
    cola = filas[-3:] if len(filas) > 3 else filas

    def med(k):
        v = sorted(f[k] for f in cola if isinstance(f.get(k), (int, float)))
        return v[len(v) // 2] if v else None

    res = {
        'etiqueta': a.etiqueta, 'ok': bool(r.get('ok')), 'msg': r.get('msg'),
        'cfg': a.cfg, 'render': a.render, 'escena': a.escena, 'copia': a.copia,
        'n_ventanas': len(filas),
        'fps': med('fps'), 'p99_ms': med('p99'), 'gpu_wait': med('gpu'), 'cp_wait': med('cp'),
        'vram_mb': med('vram'), 'vram_budget_mb': med('vramb'),
        'hdtex': med('hdtex'), 'msaa': med('msaa'), 'upx': med('upx'), 'upx_dyn': med('upxdyn'),
        'texload': med('texload'), 'lim': med('lim'),
        'ventanas': filas,
        'log': str(r.get('run') / 'dbz3.log') if r.get('run') else None,
    }
    out = Path('D:/tmp/dsPGPU')
    out.mkdir(parents=True, exist_ok=True)
    f = out / f"{time.strftime('%Y%m%d-%H%M%S')}_{a.etiqueta}.json"
    f.write_text(json.dumps(res, indent=1, ensure_ascii=False), encoding='utf-8')
    print(json.dumps({k: v for k, v in res.items() if k != 'ventanas'}, ensure_ascii=False), flush=True)
    print(f"json: {f}", flush=True)
    return 0 if res['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
