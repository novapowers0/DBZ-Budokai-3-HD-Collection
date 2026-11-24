"""R23 CAC: de las fotos cada 2 s (c_NNN.png) de heroe_exportado --par cada=1, la de cada forma: la primera tomada entre
2,2 s y 4,8 s despues de "sube a la forma N" (pausa de 5 s por forma); la forma 0, la ultima antes de la primera subida.
Copia la elegida a f_formaN.png en la carpeta de la prueba.  python formas_fotos.py <carpeta de la prueba>"""
import re
import shutil
import sys
from datetime import datetime
from pathlib import Path

d = Path(sys.argv[1])
log = (d / "dbz3.log").read_text(encoding="utf-8", errors="replace")
subes = [(datetime.strptime(t, "%Y-%m-%d %H:%M:%S.%f").timestamp(), int(n))
         for t, n in re.findall(r"\[(\S+ \S+)\] .*formas: prueba: sube a la forma (\d+)", log)]
fotos = sorted((f.stat().st_mtime, f) for f in d.glob("c_*.png"))
elegidas = {}
if subes and fotos:
    antes = [f for t, f in fotos if t < subes[0][0] - 3.0]   # lejos de la carga de ki (oscurece la escena)
    if antes:
        elegidas[0] = antes[-1]
    for t0, n in subes:
        if n in elegidas:
            continue
        en = [f for t, f in fotos if t0 + 2.2 <= t <= t0 + 4.8]
        if en:
            elegidas[n] = en[0]
for n, f in sorted(elegidas.items()):
    shutil.copy2(f, d / f"f_forma{n}.png")
print(d.name, "formas con foto:", sorted(elegidas), "subidas en el log:", [n for _, n in subes])
