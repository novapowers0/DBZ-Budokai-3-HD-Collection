"""Pruebas sin disco de composicion y rechazo de capturas incompatibles."""
from PIL import Image
from cac_lamina_r16 import montar

pares = [(Image.new("RGB", (100, 80), "red"), Image.new("RGB", (100, 80), "blue")) for _ in range(4)]
hoja = montar(pares, "majin")
assert hoja.size == (100, 256)
assert hoja.getpixel((10, 30)) == (255, 0, 0)
assert hoja.getpixel((60, 30)) == (0, 0, 255)
pares[2] = (Image.new("RGB", (102, 80)), pares[2][1])
try:
    montar(pares, "majin")
except ValueError:
    pass
else:
    raise AssertionError("se admitieron capturas de tamanos distintos")
print("PASS: lamina 4 vistas, escala 0.5, orden y validacion")
