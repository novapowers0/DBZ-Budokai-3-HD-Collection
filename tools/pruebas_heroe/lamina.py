"""R23: lamina maqueta | captura a escala 0,5 (800x450 cada una) para el juez.  python lamina.py <salida.png> <maqueta.png> <captura.png> [titulo]"""
import sys
from PIL import Image, ImageDraw

out, maq, cap = sys.argv[1:4]
t = sys.argv[4] if len(sys.argv) > 4 else ""
a, b = (Image.open(p).convert("RGB").resize((800, 450), Image.LANCZOS) for p in (maq, cap))
im = Image.new("RGB", (1620, 490), (20, 20, 28))
im.paste(a, (0, 40)); im.paste(b, (820, 40))
d = ImageDraw.Draw(im)
d.text((8, 12), "MAQUETA  " + t, fill=(255, 220, 80)); d.text((828, 12), "JUEGO  " + cap.replace("\\", "/").split("/")[-2][:40], fill=(120, 230, 255))
im.save(out); print(out)
