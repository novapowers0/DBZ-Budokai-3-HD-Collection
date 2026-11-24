"""Monta cuatro pares de capturas del juego, sin alterar sus proporciones.

Uso: python tools/cac_lamina_r16.py CARPETA SALIDA.png --raza namekiano
CARPETA contiene cac_frente.png, nativo_frente.png y los pares derecha,
espalda e izquierda. Las capturas deben tomarse con la misma luz y escala.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw

VISTAS = ("frente", "derecha", "espalda", "izquierda")
RAZAS = ("saiyan", "humano", "namekiano", "freezer", "majin")


def montar(pares, raza):
    if raza not in RAZAS or len(pares) != 4:
        raise ValueError("se requieren una raza valida y cuatro pares")
    medidas = {im.size for par in pares for im in par}
    if len(medidas) != 1:
        raise ValueError("las ocho capturas deben tener identica resolucion")
    w, h = medidas.pop()
    if w < 2 or h < 2:
        raise ValueError("captura demasiado pequena")
    w, h = w // 2, h // 2
    cabecera = 24
    hoja = Image.new("RGB", (2 * w, 4 * (h + cabecera)), (22, 22, 22))
    dib = ImageDraw.Draw(hoja)
    for i, (cac, nativo) in enumerate(pares):
        y = i * (h + cabecera)
        dib.text((4, y + 5), f"{raza} | {VISTAS[i]} | CaC", fill="white")
        dib.text((w + 4, y + 5), "B3 nativo", fill="white")
        for j, im in enumerate((cac, nativo)):
            hoja.paste(im.convert("RGB").resize((w, h), Image.Resampling.LANCZOS), (j * w, y + cabecera))
    return hoja


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("carpeta", type=Path)
    p.add_argument("salida", type=Path)
    p.add_argument("--raza", required=True, choices=RAZAS)
    a = p.parse_args()
    if a.salida.suffix.lower() != ".png":
        p.error("la salida debe ser PNG")
    pares = []
    for vista in VISTAS:
        par = []
        for personaje in ("cac", "nativo"):
            ruta = a.carpeta / f"{personaje}_{vista}.png"
            with Image.open(ruta) as im:
                im.load()
                par.append(im.copy())
        pares.append(par)
    hoja = montar(pares, a.raza)
    # x impide reemplazar cualquier trabajo anterior, incluso si aparece durante la ejecucion.
    with a.salida.open("xb") as f:
        hoja.save(f, format="PNG")
    print(f"lamina: {a.salida} ({hoja.width}x{hoja.height}); escala 0.5")


if __name__ == "__main__":
    main()
