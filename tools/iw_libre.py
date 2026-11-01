"""IW campo a pie como un jugador: palanca, dash y salto por mando virtual (dbz3_input.req), estado REAL leido de RAM.

python tools/iw_libre.py 0x7D0 [--copia opus] [--env K=V]
Muestrea X+3244 (accion de B3), X+2928 y A+0x520/B+0x520 (codigo de cada cuerpo) y cuadro; imprime cada CAMBIO y deja
capturas x0,5 en la carpeta de la prueba. FALLA si durante el mando libre el cuerpo A lleva un codigo de B3 (guardia o
reposo de combate) o la accion de B3 no es 0.
"""
import argparse
import json
import struct
import sys
import threading
import time
from pathlib import Path

import pruebas as P


def muestra(j):
    mgr = struct.unpack('>I', P.leer(j.h, P.GBASE + 0x823D6CF4, 4))[0]
    x = mgr + 2096
    raw = P.leer(j.h, P.GBASE + x, 0x1400)
    u32 = lambda o: struct.unpack_from('>I', raw, o)[0]
    return {'estado': struct.unpack('>I', P.leer(j.h, P.GBASE + mgr + 35128, 4))[0],
            'accion': struct.unpack_from('>H', raw, 3244)[0],
            'x2928': u32(2928) & 0xFFF, 'a': u32(0x650 + 0x520) & 0xFFF, 'b': u32(0xCD0 + 0x520) & 0xFFF,
            'cuadro': round(struct.unpack_from('>f', raw, 0x650 + 0x608)[0], 1),
            'dur': u32(0x650 + 0x60C), 'congelado': raw[0x650 + 0x66C],
            'y': round(struct.unpack_from('>f', raw, 68)[0], 1)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('clave')
    ap.add_argument('--copia', default='opus')
    ap.add_argument('--env', action='append', default=[])
    ap.add_argument('--final', choices=('bien', 'mal'), help='solo el final: serie de codigos y una captura cada 2 s')
    ap.add_argument('--rafaga', action='store_true', help='solo salto: 16 capturas, una cada 3 cuadros')
    a = ap.parse_args()
    clave = int(a.clave, 0)
    malos = []

    def env(_):
        e = {'DBZ3_TEST_IWMINI': f'0x{clave:X}' + (',auto' if a.final == 'bien' else ''), 'DBZ3_PS2_GAMES': r'D:\DBZ3HD\ps2_games'}
        if a.final == 'mal':
            e['DBZ3_SOLIW6_FINAL'] = '2'
        return e

    def jugar(j, _):
        j.log(r'iwmini: prueba: empieza', 90)
        fase = ['intro']
        fin = [False]
        serie = []

        def hilo():
            prev = None
            t0 = time.monotonic()
            while not fin[0]:
                try:
                    m = muestra(j)
                except Exception:
                    break
                c = (m['estado'], m['accion'], m['x2928'], m['a'], m['b'])
                if c != prev:
                    prev = c
                    m.update(t=round(time.monotonic() - t0, 3), fase=fase[0])
                    serie.append(m)
                    print(f"{m['t']:7.2f} {fase[0]:8} est {m['estado']} acc {m['accion']:3} x2928 {m['x2928']:03X} "
                          f"A {m['a']:03X} B {m['b']:03X} cuadro {m['cuadro']}/{m['dur']} y {m['y']}", flush=True)
                    if fase[0] != 'intro' and m['estado'] == 4 and (m['accion'] != 0 or not (m['a'] >= 0x800 or 0x19 <= m['a'] <= 0x1E)):
                        malos.append(m)
                time.sleep(0.008)

        h = threading.Thread(target=hilo, daemon=True)
        h.start()

        def foto(n):
            from PIL import Image
            try:
                p = j.captura(n)
            except P.Fallo as e:   # el campo 900 tarda en cargar: una captura perdida no invalida la serie
                print('sin captura', n, e, flush=True)
                return
            with Image.open(p) as im:
                im.resize((im.width // 2, im.height // 2)).save(j.run / f'{n}_05.png')

        if a.final:
            fase[0] = 'final'
            j.log(r'iwmini: final de IW', 240)
            for k in range(11):
                foto(f'z{k:02}')
                P.dormir(2)
            fin[0] = True
            return
        for k, seg in enumerate((3, 5, 5, 5, 1, 1, 1, 1, 1)):   # sobrevuelo (15 s) y presentacion
            P.dormir(seg)
            foto(f'intro{k}')
        j.log(r'iwmini: entrada acabada', 90)
        P.dormir(1)
        foto('intro_ready')
        P.dormir(2)      # cartel
        fase[0] = 'quieto'
        foto('a_quieto')
        if a.rafaga:
            fase[0] = 'salto'
            (j.dest / 'dbz3_shot.req').write_text('16 3', encoding='ascii')
            P.dormir(0.15)
            j.tecla('Semicolon', 120)
            P.dormir(2)
            from PIL import Image
            for k, b in enumerate(sorted(j.dest.glob('dbz3_shot_*.bmp'))[:16]):
                with Image.open(b) as im:
                    im.convert('RGB').resize((im.width // 2, im.height // 2)).save(j.run / f'r{k:02}_05.png')
            fin[0] = True
            return
        fase[0] = 'andar'
        j.tecla('W 2500', 1500)
        foto('b_corre')
        P.dormir(1.2)
        fase[0] = 'freno'
        P.dormir(0.8)
        foto('c_paro')
        fase[0] = 'dash'
        j.tecla('Quote', 300)
        P.dormir(0.25)
        foto('d_dash')
        P.dormir(1.5)
        fase[0] = 'salto'
        j.tecla('Semicolon', 120)
        foto('e_salto')
        P.dormir(0.2)
        j.tecla('Semicolon', 120)
        foto('f_salto2')
        P.dormir(2)
        fase[0] = 'salto-marcha'
        j.tecla('W 1400', 500)
        j.tecla('Semicolon', 120)
        foto('f2_salto_marcha')
        P.dormir(2)
        fase[0] = 'lado'
        j.tecla('A 1500', 800)
        foto('g_lado')
        P.dormir(1)
        fase[0] = 'atras'
        j.tecla('S 1500', 800)
        foto('h_atras')
        P.dormir(1.2)
        foto('i_fin')
        fase[0] = 'doble'
        j.tecla('Semicolon', 350)
        j.tecla('Semicolon', 100)
        foto('j_doble')
        P.dormir(2.5)
        fase[0] = 'cansa'
        for _ in range(5):
            j.tecla('Quote', 1150)
        foto('k_cansa')
        P.dormir(4)
        fase[0] = 'pared'
        j.tecla('W 7000', 5000)
        foto('l_pared')
        P.dormir(3)
        fin[0] = True
        (j.run / 'serie.json').write_text(json.dumps(serie, indent=1), encoding='utf-8')
        print('MALOS', len(malos), 'carpeta', j.run, flush=True)

    nombre = f'iwopus_{clave:X}'
    P.ESCENARIOS[nombre] = ('IW campo: como un jugador', env, jugar)
    r = P.correr(a.copia, nombre, ['DBZ3_TEST_TURBO=1', 'DBZ3_TEST_OCULTO=0'] + a.env, forzar=True)
    print(r)
    sys.exit(0 if not malos else 1)


if __name__ == '__main__':
    main()
