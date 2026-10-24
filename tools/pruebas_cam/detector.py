r"""CAM 24/10: detector de camara fija en combate (issue #52) sobre las capturas de combate_camara.toml.

Con la camara bien, la camara de combate sigue a los luchadores: el fondo se mueve entre c08, c15 y los agarres.
Con la camara fija (gancho de sub_820F4678 saltando la actualizacion) la escena queda clavada en la posicion de la
presentacion: entre capturas solo cambian la barra de vida y el reloj (arriba, fuera de la zona medida).
Uso: python tools\pruebas_cam\detector.py <carpeta del run> [umbral]
"""
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

PARES = [("c08", "c15"), ("c15", "g2"), ("g2", "g4"), ("g4", "g6")]


def zona(p):
    im = Image.open(p).convert("L")
    w, h = im.size
    return im.crop((0, int(h * 0.22), w, int(h * 0.95))).resize((320, 146))   # sin HUD de arriba


def medir(run, umbral=6.0):
    run = Path(run)
    difs = []
    for a, b in PARES:
        pa, pb = run / f"{a}.png", run / f"{b}.png"
        if pa.exists() and pb.exists():
            difs.append((f"{a}-{b}", ImageStat.Stat(ImageChops.difference(zona(pa), zona(pb))).mean[0]))
    quietos = sum(d < umbral for _, d in difs)
    ok = len(difs) == len(PARES) and quietos == 0      # una pareja quieta = camara clavada (1.5.5.1: g2-g4 = 0,5)
    return ok, difs


if __name__ == "__main__":
    ok, difs = medir(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 6.0)
    print(" ".join(f"{n}={d:.1f}" for n, d in difs))
    print("PASS camara viva" if ok else "FAIL camara fija o sin capturas")
    sys.exit(0 if ok else 1)
