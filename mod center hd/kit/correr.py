# -*- coding: utf-8 -*-
"""Lanza un motor (colores.py...) usando la carpeta us/ que se le diga: correr.py US SCRIPT ARGS...
colores.py no tiene --us: lee swap_b3.DEFAULT_AFS. Aqui se fija antes de ejecutarlo ("-" = la de siempre)."""
import os
import runpy
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
us, script = sys.argv[1], sys.argv[2]
sys.path.insert(0, HERE)
if us != "-":
    import swap_b3
    swap_b3.DEFAULT_AFS = os.path.join(us, "data_cmn.afs")
sys.argv = [script] + sys.argv[3:]
runpy.run_path(os.path.join(HERE, script), run_name="__main__")
