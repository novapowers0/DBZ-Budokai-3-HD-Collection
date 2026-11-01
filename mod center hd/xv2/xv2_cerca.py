#!/usr/bin/env python3
"""Primeros planos EN JUEGO de un bin (cara, ojos, boca, pelo, manos, ropa; de frente y de lado) en combate.

Pone el bin sobre una entrada de data_cmn en una copia fija y saca capturas con la camara de prueba DBZ3_TEST_CARA.
No toca la partida; deja los mods como estaban. Solo prueba: el entorno lo pone este script.
  python xv2_cerca.py D:/tmp/GOK.bin --salida D:/tmp/xvcara/antes_gok [--entrada 264] [--ids 0,7,2,15] [--copia sol]
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'mod center hd'), str(ROOT/'awo_tools')]
# dbz3_cam.req: "cabeza frente dist lado alto" (lado con signo; alto sube camara y objetivo: la cabeza baja del marcador)
VISTAS = (("cara_b", "cabeza frente 5 3.5 1.3"), ("cara_d", "cabeza frente 3 6 1.3"), 
          ("cara_f", "cabeza frente -3 7 1.6"),
          ("mano_der", "mano_der frente 3 0.8 punto"), ("mano_izq", "mano_izq frente 3 0.8 punto"),
          ("ropa_der", "codo_der frente 8 2"), ("ropa_esp", "codo_izq espalda 8 2"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('bin', type=Path)
    ap.add_argument('--salida', type=Path, required=True)
    ap.add_argument('--entrada', type=int, default=264)
    ap.add_argument('--ids', default='0,7,2,99')
    ap.add_argument('--copia', default='sol')
    ap.add_argument('--golpes', type=int, default=4)
    ap.add_argument('--pies', action='store_true', help='guarda en pies.txt la altura de cintura y tobillos del log (DBZ3_TEST_PIES)')
    ap.add_argument('--otro', help='BIN:ENTRADA: tambien el rival (prueba de memoria con dos modelos grandes)')
    a = ap.parse_args()
    import pruebas as P
    from PIL import Image
    import swap_b3
    from xeno_prueba import padded, slot_size
    dest = P.BASE/a.copia
    a.salida.mkdir(parents=True, exist_ok=True)
    lzx = a.salida/'modelo.lzx'
    subprocess.run([swap_b3.XBCOMPRESS, '/N:2048', str(a.bin.resolve()), str(lzx)], check=True, capture_output=True)
    served = padded(lzx.read_bytes(), slot_size(ROOT/'us/data_cmn.afs', a.entrada))
    lzx.unlink()
    mod = dest/'mods'/'xvcara_prueba'
    apagados = []

    def prep(j, par, env):
        for o in (dest/'mods').iterdir():
            if o != mod and o.is_dir() and not (o/'.disabled').exists():
                (o/'.disabled').write_text('xvcara temporal', encoding='utf-8')
                apagados.append(o/'.disabled')
        t = mod/'us/data_cmn.afs'/str(a.entrada)
        t.mkdir(parents=True, exist_ok=True)
        (t/'geom.bin').write_bytes(served)
        (mod/'.disabled').unlink(missing_ok=True)
        if a.otro:
            ruta, ent = a.otro.rsplit(':', 1)
            lz = a.salida/'otro.lzx'
            subprocess.run([swap_b3.XBCOMPRESS, '/N:2048', ruta, str(lz)], check=True, capture_output=True)
            t2 = mod/'us/data_cmn.afs'/ent
            t2.mkdir(parents=True, exist_ok=True)
            (t2/'geom.bin').write_bytes(padded(lz.read_bytes(), slot_size(ROOT/'us/data_cmn.afs', int(ent))))
            lz.unlink()

    def foto(j, nombre):
        p = j.captura(nombre)
        shutil.copy2(p, a.salida/(nombre+'.png'))

    def escena(j, par):
        j.log(r'tag: empieza un combate', 90)
        (j.dest/'dbz3_cam.req').write_text(VISTAS[0][1]+chr(10), encoding='ascii')   # quieto desde el primer cuadro: el muneco ataca luego
        for k in range(3):   # presentacion del combate: el plano mas parecido a un dialogo en 3D
            time.sleep(.5)
            foto(j, 'intro_%d' % k)
        P._esperar_pelea(j, 90)
        j.log(r'dbz3 cinematica: sonda de cara', 30)
        req = j.dest/'dbz3_cam.req'
        time.sleep(1.5)
        for nombre, linea in VISTAS:
            req.write_text(linea+'\n', encoding='ascii')
            time.sleep(0.7)
            foto(j, nombre)
        # parpadeo: rafaga de la cara en reposo
        req.write_text('cabeza frente 6 4 1.6\n', encoding='ascii')
        time.sleep(0.5)
        for k in range(6):
            foto(j, 'parpadeo_%d' % k)
            time.sleep(0.25)
        # golpe y dano: sin quieto (el muneco ataca), la camara sigue la cabeza
        req.write_text('cabeza\n', encoding='ascii')
        tecla = j.dest/'dbz3_input.req'
        for k in range(a.golpes):
            while tecla.exists():
                time.sleep(.05)
            tecla.write_text('J 500\n', encoding='ascii')
            time.sleep(.25)
            foto(j, 'golpe_%d' % k)
            time.sleep(.5)
        time.sleep(1.5)
        for k in range(a.golpes):
            foto(j, 'dano_%d' % k)
            time.sleep(1.1)
        req.unlink(missing_ok=True)
        (j.dest/'dbz3_cam.req').write_text('off\n', encoding='ascii')
        m = re.findall(r'perf fps=([0-9.]+)', j.texto_log())
        print('fps', m[-6:])
        if a.pies:
            (a.salida/'pies.txt').write_text(chr(10).join(l for l in j.texto_log().splitlines() if 'dbz3 pies' in l), encoding='utf-8')

    P.VENTANAS = 1
    P.OVERRIDES.update(dbz3_cac_xv2='false', dbz3_combate_prueba_muneco='"ataca"')
    P.ESCENARIOS['xvcara'] = ('primeros planos del modelo', lambda p: {
        'DBZ3_TEST_BATTLE': a.ids, 'DBZ3_HUB_MODE': '5', 'DBZ3_COMBATE_MUNECO': 'ataca', 'DBZ3_TEST_TURBO': '1',
        'DBZ3_TEST_CARA': '0', 'DBZ3_TEST_CARA_ESPERA': '60', 'DBZ3_TEST_CARA_QUIETO': '1', 'DBZ3_TEST_CARA_DURACION': '0',
        'DBZ3_TEST_CARA_DIST': '6', 'DBZ3_TEST_CARA_LADO': '3', **({'DBZ3_TEST_PIES': '1'} if a.pies else {})}, escena, prep)
    with P.copia_en_uso(dest):
        P.comprobar_libre(dest)
        try:
            r = P._correr(a.copia, 'xvcara', par={'arranque': '150'}, forzar=True)
        finally:
            if mod.is_dir():
                (mod/'.disabled').write_text('prueba terminada', encoding='utf-8')
            for m in apagados:
                if m.is_file() and 'xvcara temporal' in m.read_text(encoding='utf-8'):
                    m.unlink()
            (dest/'dbz3_cam.req').unlink(missing_ok=True)
    print('PASS' if r['ok'] else 'FAIL', r.get('msg', ''), r['run'], a.salida)
    return 0 if r['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
