"""R23 CAC: lamina de Super Saiyan por cuerpo: una fila por prueba (base, Kaioken/SSJ..., SSJ3) y la cabeza ampliada en la
ultima forma (su foto, 2-5 s despues de subir).
python lamina_ssj.py <salida.png> <carpeta_prueba>=<rotulo> [...]"""
import re
import sys
from datetime import datetime
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

out, *pruebas = sys.argv[1:]
W, H = 384, 216
filas = []
for p in pruebas:
    ruta, rot = p.rsplit("=", 1)
    d = Path(ruta)
    log = (d / "dbz3.log").read_text(encoding="utf-8", errors="replace")
    subes = [(datetime.strptime(t, "%Y-%m-%d %H:%M:%S.%f").timestamp(), int(n))
             for t, n in re.findall(r"\[(\S+ \S+)\] .*formas: prueba: sube a la forma (\d+)", log)]
    fotos = sorted((f.stat().st_mtime, f) for f in d.glob("c_*.png"))
    formas = sorted(d.glob("f_forma*.png"))
    filas.append((rot, formas, formas[-1] if formas else None))
cols = max(len(f) for _, f, _ in filas) + 1
im = Image.new("RGB", (cols * (W + 8) + 8, len(filas) * (H + 26) + 8), (20, 20, 28))
dr = ImageDraw.Draw(im)
for i, (rot, formas, cab) in enumerate(filas):
    y = 8 + i * (H + 26)
    dr.text((8, y), rot, fill=(255, 220, 80))
    for k, f in enumerate(formas):
        im.paste(Image.open(f).convert("RGB").resize((W, H)), (8 + k * (W + 8), y + 18))
        dr.text((12 + k * (W + 8), y + 20), f.stem.replace("f_forma", "forma "), fill=(120, 230, 255))
    if cab:   # cabeza ampliada: centrada en la cara, al lado del pelo rubio que mira al rival (derecha) y en su parte baja
        ic = Image.open(cab).convert("RGB")
        a = np.asarray(ic).astype(int)[:, :int(ic.width * 0.62)]
        r, g, b_ = a[..., 0], a[..., 1], a[..., 2]
        m = (r > 170) & (g < 0.9 * r) & (g > 0.55 * r) & (b_ < 0.4 * r)   # rubio SSJ (el aura es verdosa o palida)
        ys, xs = np.nonzero(m)
        if len(xs) > 200:
            cx = int(np.percentile(xs, 90))
            cy = int(np.percentile(ys[xs > np.percentile(xs, 75)], 85))
        else:
            cx, cy = 420, 330
        x0, y0 = max(0, cx - 170), max(0, cy - 110)
        c = ic.crop((x0, y0, x0 + 288, y0 + 162)).resize((W, H))
        im.paste(c, (8 + (cols - 1) * (W + 8), y + 18))
        dr.text((12 + (cols - 1) * (W + 8), y + 20), "ultima forma, ampliada", fill=(120, 230, 255))
im.save(out)
print(out)
