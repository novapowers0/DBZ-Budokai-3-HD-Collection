#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Acceso desde `tools/` al generador real del Modo Heroe en Dragon Universe.

El generador vive en `mod center hd/heroe_sagas_a_du.py` porque es el que se instala junto al juego
(`vista_heroe.py --combate` lo llama al confirmar la ficha). Este envoltorio solo reenvia los argumentos:

  python tools/heroe_sagas_a_du.py --comprobar
  python tools/heroe_sagas_a_du.py --montar --mods D:\\DBZ3HD\\ds20\\mods --us us
"""
import os
import subprocess
import sys

REAL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "mod center hd", "heroe_sagas_a_du.py")

if __name__ == "__main__":
    sys.exit(subprocess.call([sys.executable, "-X", "utf8", REAL] + sys.argv[1:]))
