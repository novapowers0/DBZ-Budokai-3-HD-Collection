# SPDX-License-Identifier: MIT
"""R23 G2/G3: referencia del original (oraculo) por el MISMO camino que el juego.

El juego arranca desde la entrada del ELF con el guion de src/iw_trad_bucle.cpp; el oraculo, por ese camino, es
identico en RAM (py1000/py3000/py4000 frente al juego: 0 palabras salvo el texto de error de CRI que el oraculo no
escribe porque intercepta su printf). Los volcados de iw_volcado salieron de otro camino (nodo_7d0.pkl de oraculo_puntos,
ya distinto en el titulo), asi que no sirven para comparar cuadro a cuadro.

Este script parte de trad5/py3000.pkl con el guion de trad_escenarios.guion() (el mismo que recibe el juego), cambia
la mision en el 4101 y captura 4250..5050 en E:\\DBZ3HD_trabajo\\g23\\orac_<CLAVE>\\gs.jsonl.
  python tools/iw_mips/orac_g23.py CLAVE_HEX
"""
import os
import sys
from pathlib import Path

AQUI = Path(__file__).parent
sys.path.insert(0, str(AQUI))
import gs_captura  # noqa: E402
import oraculo  # noqa: E402
import trad_escenarios  # noqa: E402

PUNTO = r'E:\DBZ3HD_trabajo\iwfin\p3\trad5\py3000.pkl'
DATOS = r'E:\DBZ3HD_trabajo\iw_volcado\_datos'   # subidas IMAGE por md5 (compartido con iw_volcado: solo se anade)

clave = int(sys.argv[1], 16)
indice = clave - 0x7D0 + (clave >= 0x7D4)
out = Path(r'E:\DBZ3HD_trabajo\g23') / ('orac_%X' % clave)
out.mkdir(parents=True, exist_ok=True)

# las subidas de imagen se guardan por md5 como en volcar.py (comparar_trad.py lee su contenido)
import hashlib  # noqa: E402
_gif = gs_captura.Captura.gif


class _Guarda:
    def __init__(s, b):
        s.b = bytes(b)
        s.h = hashlib.md5(s.b).hexdigest()

    def hexdigest(s):
        p = os.path.join(DATOS, s.h + '.bin')
        if not os.path.exists(p):
            open(p, 'wb').write(s.b)
        return s.h


def _gif_datos(s, d, o):
    gs_captura.hashlib = type('h', (), {'md5': staticmethod(_Guarda)})
    try:
        return _gif(s, d, o)
    finally:
        gs_captura.hashlib = hashlib


gs_captura.Captura.gif = _gif_datos

escrito = []
_vb = oraculo.Reloj.vblank


def vblank(s, c):
    if s.n >= 4101 and not escrito:
        c.w32(0x5C0DC4, indice)
        c.w32(0x5C0DC8, clave)
        escrito.append(1)
    return _vb(s, c)


oraculo.Reloj.vblank = vblank
guion = {int(k): int(v, 16) for k, v in (x.split(':') for x in trad_escenarios.guion().split(','))}
try:
    oraculo.arranque(8_000_000_000, guion, PUNTO, None, 5051, str(out / 'gs.jsonl'), 4250)
except BaseException as ex:  # noqa: BLE001  (la parada pedida sale como excepcion)
    print('fin:', type(ex).__name__, ex)
print('FIN', hex(clave))
