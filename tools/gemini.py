"""Pregunta de texto/programación a Gemini (por defecto Gemini 4 Hydrogen), con archivos opcionales.

Uso:
  python tools/gemini.py "pregunta" [--archivo a.cpp --archivo b.py ...] [--modelo M] [--salida respuesta.md]

Claves: las mismas que gemini_video.py (%USERPROFILE%\\.dbz3_gemini_keys, una por línea, nunca en el repo).
Una clave cada vez: se usa la última que funcionó (índice en E:\\DBZ3HD_trabajo\\gemini_clave.txt) y solo
se pasa a la siguiente cuando esa agota su cuota o falla. Nunca hay dos claves en uso a la vez.
"""
import argparse
import sys
from pathlib import Path

from google import genai

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gemini_video import claves  # mismo fichero de claves y mismo orden

MODELO = "gemini-4-hydrogen"
ESTADO = Path(r"E:\DBZ3HD_trabajo\gemini_clave.txt")
GUIA = "Responde en español, directo y sin relleno. Si das código, completo y listo para pegar."


def preguntar(pregunta, archivos=(), modelo=MODELO):
    texto = pregunta
    for a in archivos:
        texto += f"\n\n=== {a} ===\n" + Path(a).read_text(encoding="utf-8", errors="replace")
    ks = claves()
    try:
        ini = int(ESTADO.read_text()) % len(ks)
    except (OSError, ValueError):
        ini = 0
    errores = []
    for n in range(len(ks)):
        i = (ini + n) % len(ks)
        try:
            cliente = genai.Client(api_key=ks[i])   # guardado en variable: si no, se cierra antes de la peticion
            r = cliente.models.generate_content(model=modelo, contents=GUIA + "\n\n" + texto)
            ESTADO.write_text(str(i))
            return f"[{modelo}, clave {i + 1}/{len(ks)}]\n{r.text}"
        except Exception as e:  # cuota agotada o error de esa cuenta: la siguiente
            errores.append(f"clave {i + 1}: {str(e)[:160]}")
    raise RuntimeError("Ninguna clave respondió:\n" + "\n".join(errores))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("pregunta")
    ap.add_argument("--archivo", action="append", default=[])
    ap.add_argument("--modelo", default=MODELO)
    ap.add_argument("--salida")
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    try:
        out = preguntar(a.pregunta, a.archivo, a.modelo)
    except RuntimeError as e:
        sys.exit(str(e))
    if a.salida:
        Path(a.salida).write_text(out, encoding="utf-8")
    print(out)
