#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xv2_fuente.py - fuente "xv2" del importador del Mod Kit (importar.py): personajes de Dragon Ball Xenoverse 2.

Todo se lee del data.cpk/data1.cpk del jugador (Steam, solo lectura); nada del juego se guarda en el repo.
  lista()                                  [{clave, nombre, modelos(formas), donante}]
  convertir_forma(clave, forma, plantilla, salida)   bin HD de B3 (xv2_a_b3.convertir)
  previa(clave, forma, donante, salida_png, us)      render offline del resultado (sin importar)
  python xv2_fuente.py lista | previa CLAVE [--forma 000] [--donante 0] [--salida x.png]
"""
import argparse
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
import xv2_leer as L  # noqa: E402

CAC = ("HUM", "HUF", "NMC", "FRI", "MAM", "MAF")      # razas del creador, no personajes
DONANTE = 0                                            # Goku: cuerpo medio, el moveset mas completo
TMP = os.environ.get("DBZ3_TMP") or (r"D:\tmp" if os.path.isdir(r"D:\tmp") else None)   # temporales fuera de C:


def lista():
    ruta = L.buscar_xv2()
    if not ruta:
        return []
    c = L.cpk_xv2()
    try:
        nombres = L.nombres_xv2(ruta)
    except Exception:  # noqa: BLE001  (sin data1.cpk: se usa el codigo)
        nombres = {}
    formas = {}
    for n in c.toc:
        m = re.match(r"data/chara/([A-Z0-9]{3})/\1_(\d{3})_Bust\.emd$", n)
        if m and m.group(1) not in CAC and f"data/chara/{m.group(1)}/{m.group(1)}_000.esk" in c.toc:
            formas.setdefault(m.group(1), []).append(m.group(2))
    out = []
    for k, fs in sorted(formas.items()):
        out.append({"clave": k, "nombre": nombres.get(k, "XV2 " + k), "modelos": sorted(fs), "donante": DONANTE})
    out.sort(key=lambda e: e["nombre"].lower())
    return out


def convertir_forma(clave, forma, plantilla, salida, **kw):
    import xv2_a_b3  # noqa: PLC0415
    return xv2_a_b3.convertir(clave, forma, plantilla, salida, **kw)


def plantilla_donante(donante, us=None, destino=None):
    """Bin HD del donante (esqueleto y paleta), como hace importar.sb_import."""
    sys.path[:0] = [os.path.join(HERE, ".."), os.path.join(HERE, "..", "..", "awo_tools")]
    import afs_pair  # noqa: PLC0415
    import roster_build as rb  # noqa: PLC0415
    dn = next(x for x in rb.DB["ids"] if x["id"] == donante)
    destino = destino or os.path.join(tempfile.mkdtemp(prefix="xv2_", dir=TMP), "plantilla.bin")
    with open(destino, "wb") as fh:
        fh.write(bytes(afs_pair.hd(dn["models"][0], region=us or afs_pair.HD_US)))
    return destino


def previa(clave, forma="000", donante=DONANTE, salida="previa.png", us=None):
    """Convierte a un bin temporal y lo renderiza (frente, cel-shaded, sin GPU): lo que vera el modder antes de importar."""
    tmp = tempfile.mkdtemp(prefix="xv2_previa_", dir=TMP)
    tpl = plantilla_donante(donante, us, os.path.join(tmp, "plantilla.bin"))
    out = os.path.join(tmp, "previa.bin")
    convertir_forma(clave, forma, tpl, out)
    import subprocess  # noqa: PLC0415
    subprocess.run([sys.executable, os.path.join(HERE, "..", "model_render.py"), out, salida, "--size", "512"], check=True,
                   capture_output=True)
    return salida


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    s = ap.add_subparsers(dest="cmd")
    s.add_parser("lista")
    p = s.add_parser("previa")
    p.add_argument("clave")
    p.add_argument("--forma", default="000")
    p.add_argument("--donante", type=int, default=DONANTE)
    p.add_argument("--salida", default="previa.png")
    x = ap.parse_args()
    if x.cmd == "lista":
        for e in lista():
            print("%s\t%s\t%s" % (e["clave"], e["nombre"], ",".join(e["modelos"])))
    elif x.cmd == "previa":
        print(previa(x.clave, x.forma, x.donante, x.salida))
    else:
        ap.print_help()


if __name__ == "__main__":
    main()
