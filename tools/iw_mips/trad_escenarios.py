# SPDX-License-Identifier: MIT
"""R23 G2/G3: escribe los escenarios TRAD_7XX.toml que repiten en el juego la partida de iw_volcado/volcar.py
(guion de menus de src/iw_trad_bucle.cpp kGuion hasta el punto nodo_7d0 = vblank 3700, luego el guion de volcar.py y el
cambio de mision en el 4101, sin el parche panoramico: el oraculo corre el ELF 4:3) y vuelcan los cuadros 4250..5000 para comparar_trad.py.
  python tools/iw_mips/trad_escenarios.py CARPETA [--sin-soltar]
"""
import sys
from pathlib import Path

MENUS = ('320:4000,324:0,410:40,414:0,430:4000,434:0,2050:4000,2054:0,2150:4000,2154:0,2250:4000,2254:0,2290:40,'
         '2294:0,2330:4000,2334:0,2650:4000,2654:0,3260:4000,3264:0,3630:10,3654:0,3700:4000')


def guion(soltar=True, parar=5000):
    g = dict(x.split(':') for x in MENUS.split(','))
    if soltar:
        g['3704'] = '0'
    for b in range(3800, 4200, 100):                      # volcar.py
        g[str(b)], g[str(b + 4)] = '4000', '0'
    botones = [0x4000, 0x2000, 0x8000, 0x1000, 0x0010, 0x0040, 0x0020, 0x0080, 0x0008]
    for k, b in enumerate(range(4300, parar, 24)):
        m = botones[k % len(botones)] if (k % 9) != 8 or b > parar - 300 else 0x4000
        g[str(b)], g[str(b + 6)] = '%x' % m, '0'
    return ','.join('%s:%s' % (k, g[k]) for k in sorted(g, key=int))


def escenario(clave, carpeta, soltar):
    indice = clave - 0x7D0 + (clave >= 0x7D4)
    vol = Path(r'E:\DBZ3HD_trabajo\g23') / ('v_%X.txt' % clave)
    env = [
        'DBZ3_AGENTE=iw-g23',
        'DBZ3_TEST_IWMINI=0x%X' % clave,
        'DBZ3_TEST_CFG=dbz3_iw_traducido=true;dbz3_iw_%x_original=true;dbz3_trad_panoramico=false' % clave,
        r'DBZ3_PS2_GAMES=D:\DBZ3HD\ps2_games',
        'DBZ3_IWTRAD_GUION=' + guion(soltar),
        'DBZ3_IWTRAD_ESCRIBE=4101:5c0dc4:%x,4101:5c0dc8:%x' % (indice, clave),
        'DBZ3_IWTRAD_HASTA=5001',
        'DBZ3_IWTRAD_VOLCADO_DESDE=4250',
        'DBZ3_IWTRAD_VOLCADO=' + str(vol),
    ]
    pasos = ['log "iwmini: prueba: empieza" 400', 'log "iwtrad: volcado completo" 1500']
    q = lambda s: "'" + s + "'"
    txt = ('descripcion = "G2/G3 0x%X: partida de volcar.py en el juego, cuadros 4250-5000 para comparar_trad.py"\n'
           'env = [\n%s\n]\npasos = [\n%s\n]\n') % (clave, ',\n'.join('  ' + q(e) for e in env),
                                                 ',\n'.join('  ' + q(p) for p in pasos))
    (Path(carpeta) / ('TRAD_%X.toml' % clave)).write_text(txt, encoding='utf-8')


if __name__ == '__main__':
    Path(sys.argv[1]).mkdir(parents=True, exist_ok=True)
    for c in range(0x7D0, 0x7E2):
        escenario(c, sys.argv[1], '--sin-soltar' not in sys.argv)
    print('18 escenarios en', sys.argv[1])
