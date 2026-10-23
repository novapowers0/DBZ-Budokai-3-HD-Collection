#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xv2_bcs_datos.py - vuelca los BCS de las 6 razas del CaC de XV2 a res/datos/xv2_cac.toml (E3 -> linea C).

Solo indices y nombres (nada de arte ni modelos); se genera EN LA PC del jugador desde su copia de XV2 y no se
distribuye (queda ignorado por git si el jugador no tiene XV2). Formato:
  [raza.HUM]  genero, codigo_raza, colores = ["SKIN_", ...]   (grupo de color = indice; cada uno con N filas HSV/RGBA en el CPK)
  [[raza.HUM.parte]]  tipo ("Bust"), set (indice de partset), modelo (-> HUM_<modelo:03>_<tipo>.emd), textura,
                      shader, colores = [[grupo, fila], ...], ocultar (mascara de tipos que oculta), existe (0/1)
  [[raza.HUM.cuerpo]] i, escalas = ["b_C_Base=0.83,0.83,0.83", ...]   (constitucion del cuerpo: escala por hueso)
  python xv2_bcs_datos.py [salida.toml]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import xv2_leer as L  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
RAZAS = ("HUM", "HUF", "NMC", "FRI", "MAM", "MAF")


def volcar(salida):
    c = L.cpk_xv2()
    out = ["# generado por mod center hd/xv2/xv2_bcs_datos.py desde la copia de Xenoverse 2 del jugador (no distribuir)"]
    for r in RAZAS:
        b = L.leer_bcs(c.get("data/chara/%s/%s.bcs" % (r, r)))
        out += ["", "[raza.%s]" % r, "codigo_raza = %d" % b["raza"], "genero = %d" % b["genero"],
                "colores = [%s]" % ", ".join('"%s"' % (x["nombre"] if x else "") for x in b["colores"])]
        for i, esc in enumerate(b["cuerpos"]):
            out += ["", "[[raza.%s.cuerpo]]" % r, "i = %d" % i,
                    "escalas = [%s]" % ", ".join('"%s=%.3g,%.3g,%.3g"' % (n, *s) for n, s in esc)]
        for i, ps in enumerate(b["partsets"]):
            for tipo, p in (ps or {}).items():
                if tipo.startswith("Face"):
                    suf = tipo.replace("Face", "Face_").lower().replace("face_", "Face_")
                else:
                    suf = tipo
                ruta = "data/chara/%s/%s_%03d_%s.emd" % (r, r, p["modelo"], suf[0].upper() + suf[1:] if suf[0].islower() else suf)
                if p["modelo"] == 0 and not p["emd"] and not p["colores"] and p["ocultar"] == 0 and p["ocultar_mat"] == 0:
                    continue          # parte vacia
                out += ["", "[[raza.%s.parte]]" % r, 'tipo = "%s"' % tipo, "set = %d" % i, "modelo = %d" % p["modelo"],
                        "textura = %d" % p["textura"], "shader = %d" % p["shader"],
                        "colores = [%s]" % ", ".join("[%d, %d]" % t for t in p["colores"]),
                        "ocultar = %d" % p["ocultar"], "existe = %d" % (1 if ruta in c.toc else 0)]
    os.makedirs(os.path.dirname(salida), exist_ok=True)
    open(salida, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print("OK -> %s (%d lineas)" % (salida, len(out)))


if __name__ == "__main__":
    volcar(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "res", "datos", "xv2_cac.toml"))
