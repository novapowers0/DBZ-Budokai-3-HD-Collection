#!/usr/bin/env python3
"""clip_gemini.py - clip de un modo SACADO DEL JUEGO (fotos seguidas, sirve con la ventana tapada) + revision de Gemini.

  python tools/clip_gemini.py NOMBRE --escena iw:mini:0x7D8 --ref URL [--espera 8000] [--fotos 120] [--env K=V]
                              [--copia opus] [--pregunta "..."] [--sin-gemini]

1) escribe studies 5/ENTREGAS/OPUS/clips/NOMBRE.toml (salto a la escena, espera, NFOTOS capturas seguidas ~2 por segundo);
2) lo corre con tools/pruebas.py (una partida); 3) monta D:/tmp/clips/NOMBRE.mp4 con los tiempos reales de cada foto;
4) pide a Gemini (tools/gemini_video.py) la lista de diferencias con el original (--ref, YouTube) y la deja en
   studies 5/ENTREGAS/OPUS/GEMINI_NOMBRE.md. Nada sale del equipo salvo el clip del juego que se manda a Gemini.
"""
import argparse
import glob
import os
import subprocess
import sys
from pathlib import Path

R = Path(__file__).resolve().parent.parent
OPUS = R / "studies 5" / "ENTREGAS" / "OPUS"
CLIPS = Path("D:/tmp/clips")
PREGUNTA = ("El primer vídeo es el juego ORIGINAL. El segundo es nuestro port de ese mismo modo a Budokai 3 HD, sacado "
            "del juego a unas 2 fotos por segundo (los saltos entre fotos son normales; un cambio brusco de sitio o "
            "tamaño sin corte de cámara SÍ es un fallo). Busca en el original el tramo equivalente y haz una lista "
            "exhaustiva de TODOS los problemas del segundo vídeo frente al original: HUD (posición, tamaño, textos, "
            "elementos que faltan o sobran, solapes), cámara, escenario, modelos, animaciones, efectos, orden y ritmo, "
            "temblores, contornos, textos. Para cada uno: marca de tiempo del segundo vídeo y la equivalente del "
            "original, qué se ve y qué debería verse.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("nombre")
    ap.add_argument("--escena", required=True)
    ap.add_argument("--ref", required=True, help="vídeo original (YouTube)")
    ap.add_argument("--espera", type=int, default=8000, help="ms tras llegar a la escena antes de la primera foto")
    ap.add_argument("--fotos", type=int, default=120)
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--copia", default="opus")
    ap.add_argument("--pregunta", default=PREGUNTA)
    ap.add_argument("--sin-gemini", action="store_true")
    ap.add_argument("--solo-gemini", action="store_true", help="no juega: usa el clip ya montado")
    a = ap.parse_args()
    mp4 = CLIPS / (a.nombre + ".mp4")
    if a.solo_gemini:
        return revisar(a, mp4)
    (OPUS / "clips").mkdir(parents=True, exist_ok=True)
    CLIPS.mkdir(parents=True, exist_ok=True)
    toml = OPUS / "clips" / (a.nombre + ".toml")
    pasos = ['  "esperar %d",' % a.espera] + ['  "esperar 60", "captura c%03d",' % i for i in range(a.fotos)]
    toml.write_text('descripcion = "clip_gemini %s"\nescena = "%s"\nenv = [%s]\npasos = [\n%s\n]\n'
                    % (a.nombre, a.escena, ", ".join('"%s"' % e for e in a.env), "\n".join(pasos).rstrip(",")),
                    encoding="utf-8")
    r = subprocess.run([sys.executable, str(R / "tools" / "pruebas.py"), "correr", a.copia, str(toml), "--forzar"],
                       cwd=R, capture_output=True, text=True, encoding="utf-8", errors="replace")
    print("\n".join(l for l in r.stdout.splitlines() if "PASS" in l or "FAIL" in l or "evidencia" in l))
    dirs = sorted(glob.glob("D:/DBZ3HD/_pruebas/*_%s_%s" % (a.copia, a.nombre)), key=os.path.getmtime)
    fotos = sorted(glob.glob(os.path.join(dirs[-1], "c[0-9][0-9][0-9].png"))) if dirs else []
    if len(fotos) < 4:
        sys.exit("clip_gemini: sin fotos (%d)" % len(fotos))
    t = [os.path.getmtime(f) for f in fotos]
    lista = CLIPS / (a.nombre + ".txt")
    with open(lista, "w", encoding="utf-8") as fh:
        for i, f in enumerate(fotos):
            fh.write("file '%s'\nduration %.3f\n" % (f.replace("\\", "/"), (t[i + 1] - t[i]) if i + 1 < len(t) else 0.5))
        fh.write("file '%s'\n" % fotos[-1].replace("\\", "/"))
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i", str(lista), "-vf",
                    "scale=1280:-2,fps=10,format=yuv420p", "-c:v", "libx264", "-crf", "23", str(mp4)], check=True)
    print("clip: %s (%d fotos, %.0f s)" % (mp4, len(fotos), t[-1] - t[0]))
    if not a.sin_gemini:
        revisar(a, mp4)


def revisar(a, mp4):
    import time  # noqa: PLC0415
    for intento in range(4):   # (503 de alta demanda: es pasajero)
        g = subprocess.run([sys.executable, str(R / "tools" / "gemini_video.py"), a.ref, str(mp4), a.pregunta],
                           cwd=R, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=900)
        if g.stdout.strip() and "Ninguna clave" not in g.stdout + g.stderr:
            break
        time.sleep(90 * (intento + 1))
    informe = OPUS / ("GEMINI_%s.md" % a.nombre)
    informe.write_text("# Gemini: %s (port vs original)\n\nOriginal: %s · Port: %s (escena %s)\n\n%s\n"
                       % (a.nombre, a.ref, mp4, a.escena, g.stdout.strip() or g.stderr.strip()), encoding="utf-8")
    print("informe:", informe)


if __name__ == "__main__":
    main()
