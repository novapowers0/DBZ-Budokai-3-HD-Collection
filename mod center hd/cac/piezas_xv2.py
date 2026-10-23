#!/usr/bin/env python3
"""piezas_xv2.py - C6: modelos de XV2 disponibles por raza y tipo (solo indices, de res/datos/xv2_cac.toml).
  lista("HUM", "Bust") -> [0, 290, 300, ...]     tipos: Bust Pants Boots Rist Hair FaceBase
  MINIMO = 3 piezas por tipo para el menu del Heroe (todo lo demas, bajo 'ver mas')."""
import os
import sys
import tomllib

RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def disponible():
    """True si XV2 esta instalado (data.cpk localizado). Sin el, el creador usa el heroe base de B3."""
    try:
        sys.path.insert(0, os.path.join(RAIZ, "mod center hd", "xv2"))
        import xv2_leer  # noqa: PLC0415
        return xv2_leer.disponible()
    except Exception:   # noqa: BLE001
        return False
MINIMO = {"Hair": [1, 2, 3], "FaceBase": [1, 2, 3], "FaceEye": [1, 2, 3], "Bust": [300, 301, 302], "Pants": [300, 301, 302],
          "Boots": [300, 301, 302], "Rist": [300, 301, 302]}


def lista(raza, tipo):
    try:
        with open(os.path.join(RAIZ, "res", "datos", "xv2_cac.toml"), "rb") as f:
            d = tomllib.load(f)
    except OSError:
        return []
    return sorted({p["modelo"] for p in d["raza"][raza]["parte"] if p["tipo"] == tipo and p["existe"]})


def minimo(raza):
    """3 piezas por tipo para esa raza (las que existen de verdad: 'existe=1' del catalogo)."""
    out = {}
    for t in MINIMO:
        ok = [m for m in lista(raza, t) if m not in (0, 290)] or lista(raza, t)
        out[t] = ok[:3]
    return out


if __name__ == "__main__":
    for t in MINIMO:
        print(t, lista("HUM", t)[:12])
