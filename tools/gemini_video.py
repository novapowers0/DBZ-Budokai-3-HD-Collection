"""Pregunta a Gemini por un vídeo de YouTube (lo ve entero) y devuelve la respuesta con marcas de tiempo.

Uso:
  python tools/gemini_video.py URL|VIDEO [URL|VIDEO ...] "pregunta"
  Con dos vídeos (B1 original y nuestro port) sirve para listar diferencias momento a momento.
  Opciones antes de las fuentes: [--inicio S] [--fin S] [--fps N] [--modelo M]

Las claves se leen de %USERPROFILE%\\.dbz3_gemini_keys (una por línea; nunca en el repo).
Si una clave agota su cuota, pasa a la siguiente; si el modelo falla, prueba el siguiente de MODELOS.
Después, sacar los fotogramas con la receta de studies 5\\referencias\\RECETA_REFERENCIAS_VIDEO.md.
"""
import argparse
import os
import sys
import time

from google import genai
from google.genai import types

# ponytail: la capa gratuita no da cuota a los modelos pro (429); pasar --modelo gemini-pro-latest si hay facturación.
MODELOS = ["gemini-flash-latest", "gemini-3.8-flash"]
GUIA = ("Responde en español. Para cada cosa que encuentres, da la marca de tiempo exacta MM:SS "
        "(o H:MM:SS) de inicio y fin, y describe en una línea lo que se ve. No inventes: si no aparece, dilo.")


def claves():
    ruta = os.path.expanduser("~/.dbz3_gemini_keys")
    vistas = []
    for k in open(ruta, encoding="utf-8").read().split():
        if k not in vistas:
            vistas.append(k)
    return vistas


def _parte(cliente, fuente, meta):
    """URL de YouTube tal cual; fichero local (p. ej. grabación nuestra o de PCSX2) se sube a la cuenta de esa clave."""
    if not os.path.exists(fuente):
        return types.Part(file_data=types.FileData(file_uri=fuente), video_metadata=meta)
    f = cliente.files.upload(file=fuente)
    while f.state.name == "PROCESSING":
        time.sleep(3)
        f = cliente.files.get(name=f.name)
    return types.Part(file_data=types.FileData(file_uri=f.uri, mime_type=f.mime_type), video_metadata=meta)


def preguntar(fuentes, pregunta, inicio=None, fin=None, fps=None, modelos=MODELOS):
    meta = None
    if inicio is not None or fin is not None or fps:
        meta = types.VideoMetadata(
            start_offset=f"{inicio}s" if inicio is not None else None,
            end_offset=f"{fin}s" if fin is not None else None,
            fps=fps)
    errores = []
    for i, k in enumerate(claves()):
        cliente = genai.Client(api_key=k)
        try:
            partes = [types.Part(text=f"Vídeo {n + 1}: {os.path.basename(f)}")
                      for n, f in enumerate(fuentes)]
            partes = [p for n, f in enumerate(fuentes) for p in (partes[n], _parte(cliente, f, meta))]
        except Exception as e:
            errores.append(f"subida clave {i + 1}: {str(e)[:160]}")
            continue
        contenido = types.Content(parts=partes + [types.Part(text=GUIA + "\n\n" + pregunta)])
        for modelo in modelos:
            try:
                r = cliente.models.generate_content(model=modelo, contents=contenido)
                return f"[{modelo}, clave {i + 1}]\n{r.text}"
            except Exception as e:  # cuota agotada, modelo no disponible...: siguiente
                errores.append(f"{modelo} clave {i + 1}: {str(e)[:160]}")
    raise RuntimeError("Ninguna clave/modelo respondió:\n" + "\n".join(errores))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("fuentes", nargs="+", help="URL(s) de YouTube y/o vídeos locales; la última palabra es la pregunta")
    ap.add_argument("--inicio", type=int)
    ap.add_argument("--fin", type=int)
    ap.add_argument("--fps", type=float)
    ap.add_argument("--modelo")
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    try:
        print(preguntar(a.fuentes[:-1], a.fuentes[-1], a.inicio, a.fin, a.fps, [a.modelo] if a.modelo else MODELOS))
    except RuntimeError as e:
        sys.exit(str(e))
