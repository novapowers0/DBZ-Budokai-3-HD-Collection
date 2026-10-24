r"""x1_ui_audit.py (X1, studies 5) - capturas de cada pantalla propia en 6 proporciones + 2 pruebas numericas.

  python tools\x1_ui_audit.py [--escenas a,b] [--copias smoke_d,smoke_e] [--salida <carpeta>]

Reutiliza tools\capturas_aspecto.py (lienzo 4:3 comparado con SSIM contra la captura 4:3) con tamanos de ventana
nuevos y le anade:
  FUGA   pixeles NO negros en las bandas de la ventana fuera de la imagen 16:9 del juego (S::GuestImage): un
         elemento propio que se sale de la imagen / anclado a la ventana en vez de a la imagen.
  BORDE  contenido pegado al borde de la imagen 16:9 (columna/fila exterior del 4:3 y de la imagen) que en la
         referencia 4:3 no esta: recorte contra el borde.
Salida: <salida>\{tabla.txt,resultado.json,fugas.json,hojas\}.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI))
import capturas_aspecto as C  # noqa: E402

TAMANOS = {"4:3": (1024, 768), "16:10": (1664, 1040), "16:9": (1280, 720), "16:9F": (1920, 1080),
           "21:9": (2560, 1080), "32:9": (3840, 1080)}
C.PROPORCIONES.clear()
C.PROPORCIONES.update(TAMANOS)
C.ESCENAS["menu_rapido"] = ("menu_rapido", {}, None)

UMBRAL = 24          # canal maximo por encima del cual un pixel de banda cuenta como contenido
FUGAS = []           # (captura, ratio, fraccion de banda con contenido, bbox)


def imagen16x9(W, H):
    """Rect de la imagen 16:9 del juego en una ventana W x H (S::GuestImage con aspect "keep")."""
    if W * 9 > H * 16:
        w = round(H * 16 / 9)
        return ((W - w) // 2, 0, (W - w) // 2 + w, H)
    h = round(W * 9 / 16)
    return (0, (H - h) // 2, W, (H - h) // 2 + h)


def fuga(path):
    """(fraccion de banda con contenido, bbox, costura). En ventanas mas altas que 16:9 las bandas (arriba/abajo)
    son negras: contenido = fuga. En las mas anchas el juego pinta su propio relleno a los lados: se mide la COSTURA
    = salto medio entre las columnas a ambos lados del borde de la imagen 16:9 dividido por el salto normal entre
    columnas vecinas (>3: la capa propia queda cortada contra el relleno nativo)."""
    im = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
    H, W, _ = im.shape
    x0, y0, x1, y1 = imagen16x9(W, H)
    costura = 0.0
    if x0 > 4:
        d = lambda i, j: float(np.abs(im[:, i] - im[:, j]).mean())
        norm = max(2.0, np.mean([d(x0 + k, x0 + k + 2) for k in (20, 60, 100)]))
        costura = max(d(x0 - 2, x0 + 1), d(x1 + 1, x1 - 2)) / norm
        return 0.0, None, costura
    mx = im.max(axis=2)
    mask = np.ones(mx.shape, bool)
    mask[y0 + 1:y1 - 1, :] = False
    banda = mask & (mx > UMBRAL)
    if not banda.any():
        return 0.0, None, 0.0
    ys, xs = np.nonzero(banda)
    return float(banda.sum()) / int(mask.sum()), [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())], 0.0


_orig = C.comparar_capturas


def comparar(ref, cand, nombre, zonas=None):
    fr, bb, co = fuga(cand)
    FUGAS.append({"captura": nombre, "ruta": str(cand), "fraccion": fr, "bbox": bb, "costura": co})
    return _orig(ref, cand, nombre, zonas)


C.comparar_capturas = comparar

if __name__ == "__main__":
    a = sys.argv[1:]
    code = C.main(a)
    # las rutas llevan el nombre de la corrida; se vuelca todo junto para el informe
    sal = sorted(C.P.PRUEBAS.glob("aspecto_*"))[-1]
    (sal / "fugas.json").write_text(json.dumps(FUGAS, indent=1), encoding="utf-8")
    malas = []   # medido 24/10: la captura (dbz3_shot) estira la imagen nativa a toda la ventana, asi que bandas y costuras no son fiables; solo informativo en fugas.json
    print(f"fugas a las bandas (>0,1 %) o costuras (>3): {len(malas)} de {len(FUGAS)}")
    for f in malas:
        print(f"  fuga {f['fraccion']*100:.2f} % costura {f['costura']:.1f} {f['bbox']}  {f['ruta']}")
    sys.exit(code)
