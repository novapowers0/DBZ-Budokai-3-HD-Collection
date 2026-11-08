"""Mide el fundido de entrada de la intro de Serpiente (campo IW 884) en el port.

No escribe memoria, mods ni guiones: solo lee la camara por RAM y pide capturas
de un cuadro al presentador (dbz3_shot.req). Mide la luminancia del cielo y de
las nubes en cada captura para localizar el fin del fundido y los cambios de
tanda. NO es pareja visual con el original: mide solo el port; el material se
compara por otro lado.
"""
import argparse
import json
import os
import struct
import time
from pathlib import Path

import pruebas as P
from iwx_bubbles_focal import HOME, pantalla2


def sha(data):
    return __import__('hashlib').sha256(data).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--indice', type=int, default=30)
    ap.add_argument('--segundos', type=float, default=9.0)
    ap.add_argument('--cada', type=int, default=2, choices=(1, 2, 3))
    a = ap.parse_args()
    os.environ['DBZ3_AGENTE'] = 'nIW'
    out = HOME/f'fundido_port_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA', out, flush=True)
    pantalla2()
    meta = {}

    def config(_):
        return dict(DBZ3_TEST_BATTLE=f'0,7,{a.indice}', DBZ3_COMBATE_MUNECO='quieto',
                    DBZ3_TEST_INTRO_SONDA='1', DBZ3_HUB_MODE='1')

    def play(j, _):
        meta['exe_sha256'] = sha((j.dest/'dbz3.exe').read_bytes())
        meta['stage_sha256'] = sha((j.dest/'mods'/'Campo IW 884'/'stage.toml').read_bytes())

        def camera():
            def read(addr, n):
                value = P.leer(j.h, P.GBASE+addr, n)
                if value is None or len(value) != n:
                    raise P.Fallo('lectura de camara incompleta')
                return value
            mgr = int.from_bytes(read(0x823D6CF4, 4), 'big')
            if not mgr:
                return None
            b = read(mgr, 36000)
            u = lambda o: struct.unpack_from('>I', b, o)[0]
            f = lambda o: struct.unpack_from('>f', b, o)[0]
            return dict(mgr=mgr, estado=u(35128), clip=u(560), modo=u(548),
                        acc=u(564), reloj=f(476), paso=f(464), limite=f(576),
                        t_host=time.monotonic())

        # Esperar la intro (estado1 de camara, modo7) sin forzar relojes.
        fin = time.monotonic()+150
        intro = None
        while time.monotonic() < fin:
            c = camera()
            if c and c['estado'] == 1 and c['modo'] == 7:
                intro = c
                break
            time.sleep(.02)
        if not intro:
            raise P.Fallo('No aparece la intro de camara (estado1/modo7)')
        meta['camara_detectada'] = {k: intro[k] for k in ('estado', 'modo', 'clip', 'reloj')}

        t0 = time.monotonic()
        fotos, tiradas = [], []
        fin_t = t0+a.segundos
        while time.monotonic() < fin_t:
            antes = camera()
            marca = time.monotonic()
            try:
                picture = j.captura(f'f{len(fotos):03d}')
            except P.Fallo:
                break
            fotos.append(picture)
            tiradas.append(dict(t_rel=round(marca-t0, 3),
                                reloj=antes['reloj'] if antes else None,
                                clip=antes['clip'] if antes else None,
                                estado=antes['estado'] if antes else None,
                                modo=antes['modo'] if antes else None))
            time.sleep(.05)
        if len(fotos) < 6:
            raise P.Fallo(f'Muestras insuficientes ({len(fotos)})')
        meta['muestras_camara'] = tiradas
        meta['capturas'] = len(fotos)
        meta['cadencia_s'] = round((tiradas[-1]['t_rel']-tiradas[0]['t_rel'])/max(1, len(tiradas)-1), 3)

        from PIL import Image, ImageStat
        filas = []
        for i, p in enumerate(fotos):
            with Image.open(p) as im:
                im = im.convert('RGB')
                w, hh = im.size
                cielo = im.crop((0, 0, w, int(hh*.25)))
                nubes = im.crop((int(w*.55), int(hh*.5), w, int(hh*.8)))
                sc = ImageStat.Stat(cielo)
                sn = ImageStat.Stat(nubes)
                filas.append(dict(i=i, tam=[w, hh],
                                  cielo=[round(x, 1) for x in sc.mean],
                                  cielo_sd=[round(x, 1) for x in sc.stddev],
                                  nubes=[round(x, 1) for x in sn.mean],
                                  nubes_sd=[round(x, 1) for x in sn.stddev]))
        meta['series'] = filas
        lum = [sum(r['cielo'])/3.0 for r in filas]
        meta['luminancia_cielo'] = [round(x, 1) for x in lum]
        # Referencia = maximo de los primeros 3 s (antes de que la camara empiece
        # a moverse y cambie la composicion). El fundido es lo que sube hasta ahi.
        n3 = max(2, int(round(3.0 / max(meta.get('cadencia_s') or .5, .05))))
        ref = max(lum[:n3]) if lum else 0.0
        meta['cielo_referencia'] = round(ref, 1)
        fin_fundido = next((i for i, v in enumerate(lum) if ref and v >= .98*ref), None)
        meta['fin_fundido_captura'] = fin_fundido
        if fin_fundido is not None:
            meta['fin_fundido_segundos'] = round(tiradas[fin_fundido]['t_rel'], 3)
            meta['inicio_relativo'] = round(lum[0]/ref, 3) if ref else None
            meta['curva'] = [round(x/ref, 3) for x in lum]
        # Cambios de tanda (reinicio del reloj de clip) con su tiempo.
        clips = []
        for m in tiradas:
            if not clips or clips[-1]['clip'] != m['clip']:
                clips.append(dict(t_rel=m['t_rel'], clip=m['clip'], reloj=m['reloj']))
        meta['cambios_clip'] = clips
        return None

    clave = 'nIW_fundido_serpiente'
    P.ESCENARIOS[clave] = ('Serpiente: curva del fundido de entrada en el port (solo port)', config, play, None)
    r = P.correr('smoke_b', clave, [], None, {'modo': 'observar'}, True)
    with (out/'resultado.json').open('x', encoding='utf-8') as f:
        json.dump(dict(resultado=r, meta=meta), f, indent=1, default=str, ensure_ascii=False)
    if meta.get('fin_fundido_captura') is not None:
        print('FUNDIDO fin en', meta['fin_fundido_captura'], 'capturas =',
              meta['fin_fundido_segundos'], 's; referencia cielo', meta['cielo_referencia'])
        print('INICIO relativo', meta.get('inicio_relativo'))
        print('CURVA', meta.get('curva'))
        print('CLIPS', meta.get('cambios_clip'))
    print(P.tabla([r]), flush=True)
    return 0 if r['ok'] and meta.get('fin_fundido_captura') is not None else 1


if __name__ == '__main__':
    raise SystemExit(main())
