"""b2_babidi_estados.py N piso : desde el estado 23 (selector de pisos de Babidi) recorre piso -> elegir personajes -> estadio -> combate y guarda
slot 40+piso (elegir personajes), 50+piso (seleccion de estadio), 60+piso (primer fotograma del combate). Cargar antes el 23 en OTRO proceso
(la primera tecla tras cargar un estado se pierde)."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcsx2_sonda as s, pcsx2_pool as P
from PIL import Image
import numpy as np


def media(f):
    return float(np.array(Image.open(f).convert('L')).mean())


def espera_estable(nombre, seg=60):
    t0, prev = time.time(), None
    while time.time() - t0 < seg:
        a = np.array(Image.open(s.captura(nombre)).convert('L')).astype(float)
        if a.mean() > 25 and prev is not None and abs(a - prev).mean() < 8:
            return
        prev = a; time.sleep(1.5)
    raise SystemExit('no se estabiliza')


if __name__ == '__main__':
    n, piso = int(sys.argv[1]), int(sys.argv[2]); P.registrar_en_sonda(s, n); p = s.Pine(espera=30)
    if piso > 1:
        s.teclas('down espera1200 ' * (piso - 1))
    s.teclas('espera800 cruz'); espera_estable('bab_sel.png'); time.sleep(1)
    p.guardar(40 + piso); print('char select', 40 + piso, flush=True); time.sleep(2)
    s.teclas('cruz espera2500 cruz espera3000 cruz espera3500'); espera_estable('bab_est.png'); time.sleep(1)
    p.guardar(50 + piso); print('estadio', 50 + piso, flush=True); time.sleep(2)
    s.teclas('cruz')
    negro = False; t0 = time.time()
    while time.time() - t0 < 90:
        m = media(s.captura('bab_f.png'))
        if m < 8: negro = True
        elif negro and m > 30:
            p.guardar(60 + piso); print('combate', 60 + piso, round(time.time() - t0), flush=True); break
        time.sleep(0.5)
