#!/usr/bin/env python3
"""Genera estudios originales de antenas y orejas; no son piezas para combate."""
import argparse
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
BLENDER = Path(r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe")
VARIANTES = (
    ("antena_corta", "antenna", .65, -.15),
    ("antena_larga", "antenna", 1.25, -.15),
    ("antena_curva", "antenna", 1.0, -.65),
    ("antena_erguida", "antenna", 1.0, .0),
    ("antena_abierta", "antenna", 1.0, .45),
    ("antena_arqueada", "antenna", 1.15, -.95),
    ("oreja_corta", "ear", .65, -.15),
    ("oreja_larga", "ear", 1.1, -.15),
    ("oreja_curva", "ear", .9, .45),
)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--salida", type=Path, required=True, help="directorio nuevo")
    p.add_argument("--blender", type=Path, default=BLENDER)
    a = p.parse_args()
    if not a.blender.is_file():
        p.error("Blender no encontrado")
    a.salida.mkdir(parents=False, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix="namek_recetas_") as temp:
        for nombre, tipo, tamano, curva in VARIANTES:
            receta = Path(temp) / (nombre + ".toml")
            receta.write_text(
                f'kind = "{tipo}"\nsize = {tamano}\ncurve = {curva}\n'
                'segments = 10\nsides = 8\ncolour = [0.30, 0.65, 0.20]\n'
                'colour_region = "piel"\nbone = "HEAD"\n', encoding="utf-8")
            subprocess.run([str(a.blender), "-b", "--factory-startup", "--python",
                            str(ROOT / "tools" / "blender_partes.py"), "--", str(receta),
                            "--salida", str(a.salida.resolve())], check=True)
    print("PASS: 9 prototipos originales, solo vista estatica; no instalables")


if __name__ == "__main__":
    main()
