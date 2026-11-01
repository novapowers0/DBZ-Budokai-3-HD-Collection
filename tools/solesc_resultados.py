"""Resume medidas solESC y compara fases; no afirma sincronía si no está medida."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import time
from PIL import Image, ImageDraw


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("original", type=Path)
    p.add_argument("--port", type=Path)
    p.add_argument("--salida", type=Path, default=Path(r"D:\tmp\solESC\resumen.json"))
    p.add_argument("--archivar-binarios", action="store_true")
    a = p.parse_args()
    filas = json.loads((a.original / "medida.json").read_text(encoding="utf-8"))
    eventos, anterior = [], None
    for f in filas:
        clave = (f["zona"], f["evento_activo"][0])
        if clave != anterior or "captura" in f:
            eventos.append(f)
            anterior = clave
    datos = dict(original=str(a.original), eventos=eventos,
                 nota="Campos PAL: 1 campo = 1,2 cuadros de animacion. Fotos por fase, no prueba de cuadros identicos.")
    for f in eventos:
        print("original", f["campo"], "zona", f["zona"], "evento", f["evento_activo"],
              "posiciones", f["posiciones"], "foto", f.get("captura", {}).get("imagen"))
    if a.port:
        datos["port"] = json.loads((a.port / "medida.json").read_text(encoding="utf-8"))
        a.salida.parent.mkdir(parents=True, exist_ok=True)
        for original, port in (("antes", "antes"), ("inicio_escena", "inicio"), ("cambio", "cambio"), ("despues", "despues")):
            iz, de = a.original / (original + ".png"), a.port / (port + "_05.png")
            if not iz.exists() or not de.exists():
                continue
            with Image.open(iz) as i, Image.open(de) as d:
                altura = min(i.height, d.height)
                i = i.convert("RGB").resize((int(i.width * altura / i.height), altura))
                d = d.convert("RGB").resize((int(d.width * altura / d.height), altura))
                par = Image.new("RGB", (i.width + d.width, altura + 38), "#151515")
                par.paste(i, (0, 38))
                par.paste(d, (i.width, 38))
                t = ImageDraw.Draw(par)
                t.text((8, 4), "B2 original | B3 HD: camara parcial", fill="white")
                t.text((8, 20), "Fase: " + original + " (sincronia de cuadro NO validada)", fill="white")
                par.save(a.salida.parent / ("par_" + original + ".png"))
    a.salida.write_text(json.dumps(datos, indent=2), encoding="utf-8")
    if a.archivar_binarios:
        # Solo clones de ESTA sonda, tras verificar que no ejecutan un emulador.
        activos = subprocess.run(["powershell", "-NoProfile", "-Command",
            "Get-Process pcsx2 -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Path"],
            text=True, errors="replace", capture_output=True).stdout.lower()
        if "d:\\tmp\\solesc\\" in activos:
            raise RuntimeError("Hay un emulador propio abierto; no se archiva")
        respaldo = Path(r"D:\_PARA_BORRAR_CLAUDE\solESC") / ("emuladores_" + time.strftime("%H%M%S"))
        respaldo.mkdir()
        raiz = Path(r"D:\tmp\solESC")
        for nombre in ("esb", "esb2", "esb3", "esb4"):
            if (raiz / nombre).exists():
                shutil.move(str(raiz / nombre), respaldo / nombre)
        ejecutable = respaldo / "ejecutable_medida"
        ejecutable.mkdir()
        for f in a.original.iterdir():
            if f.suffix.lower() in (".dll", ".exe") or f.name in ("resources", "QtPlugins", "cache"):
                shutil.move(str(f), ejecutable / f.name)
        print("Clones archivados sin borrar:", respaldo)


if __name__ == "__main__":
    main()
