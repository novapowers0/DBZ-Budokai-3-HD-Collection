#!/usr/bin/env python3
"""R23: vuelve a montar las historias del Heroe (casillas 35-37) en una copia de prueba con el generador de HOY.
Las pruebas con DBZ3_TEST_DU_HEROE usan lo montado la ultima vez (el juego solo remonta al confirmar la ficha), asi que tras
cambiar sagas.toml o heroe_sagas_a_du.py hay que correr esto:   python tools/pruebas_heroe/remontar_historia.py smoke_b"""
import os
import sys

RAIZ = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
copia = os.path.join("D:\DBZ3HD", sys.argv[1] if len(sys.argv) > 1 else "smoke_b")
cmh = os.path.join(RAIZ, "mod center hd")
sys.path.insert(0, cmh)
sys.path.insert(0, os.path.join(cmh, "cac"))
import heroe_sagas_a_du as hs  # noqa: E402
import roster_build as rb  # noqa: E402

for cas in (35, 36, 37):
    sello = os.path.join(copia, "mods", "historia_heroe_%d" % cas, ".sello")
    if os.path.isfile(sello):
        os.remove(sello)   # fuerza el remontaje
rc = hs.montar_si_hace_falta(os.path.join(copia, "mods"), rb.us_dir(None), os.path.join(RAIZ, "res", "heroe", "sagas.toml"), 36, 0, None)
print("remontar_historia: codigo", rc)
sys.exit(rc)
