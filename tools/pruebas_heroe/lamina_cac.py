"""R23 CAC: lamina por raza para el juez: fotos del juego con su rotulo, en rejilla de 3 columnas a 640x360.
python lamina_cac.py <salida.png> "<titulo>" <foto.png>=<rotulo> [...]"""
import sys
from PIL import Image, ImageDraw

out, titulo, *fotos = sys.argv[1:]
W, H, C = 640, 360, 3
filas = (len(fotos) + C - 1) // C
im = Image.new("RGB", (C * W + (C + 1) * 10, 40 + filas * (H + 34)), (20, 20, 28))
d = ImageDraw.Draw(im)
d.text((10, 12), titulo, fill=(255, 220, 80))
for k, f in enumerate(fotos):
    ruta, rot = f.rsplit("=", 1)
    x, y = 10 + (k % C) * (W + 10), 40 + (k // C) * (H + 34)
    im.paste(Image.open(ruta).convert("RGB").resize((W, H), Image.LANCZOS), (x, y + 22))
    d.text((x, y + 4), rot, fill=(120, 230, 255))
im.save(out)
print(out)
