#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""piezas_cac.py - Genera el CATALOGO de piezas del creador (piezas_cac.toml).

Fuente: `studies 6/ENTREGAS/02_creador_personaje_cac/datos/inventario_piezas.tsv`
(102 cuerpos, 9 pelos resueltos, tipos dentro del cuerpo, color, voz, logos) y las
piezas convertidas a B3 HD (`<pieza>_b3.bin`) que haya en la carpeta de trabajo.

Salida: `mod center hd/cac/piezas_cac.toml` con bloques `[[pieza]]` que lee
`src/cac/cac.cpp` (CargarCatalogo). Cada pieza lleva: tipo, id, nombre, idx_afs y
archivo (bin convertido, "" = nativa de B3).

Uso:
  python piezas_cac.py [inventario.tsv] [salida.toml] [--bins DIR]
"""
import argparse
import json
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))


def filas_inventario(path):
    out = []
    if not os.path.exists(path):
        return out
    for ln in open(path, encoding="utf-8"):
        p = ln.rstrip("\n").split("\t")
        if len(p) >= 3 and p[0] != "categoria":
            out.append(p)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inventario", nargs="?",
                    default=os.path.join(AQUI, "..", "..", "studies 6", "ENTREGAS",
                                         "02_creador_personaje_cac", "datos", "inventario_piezas.tsv"))
    ap.add_argument("salida", nargs="?", default=os.path.join(AQUI, "piezas_cac.toml"))
    ap.add_argument("--bins", default=r"E:\DBZ3HD_trabajo\ut\cac",
                    help="carpeta con las piezas ya convertidas (*_b3.bin)")
    a = ap.parse_args()

    bins = {}
    if os.path.isdir(a.bins):
        for n in os.listdir(a.bins):
            if n.endswith("_b3.bin"):
                bins[n[:-len("_b3.bin")]] = n

    piezas = []
    tipos = {"cuerpo": 0, "pelo": 0, "cara": 0, "ropa": 0, "color": 0, "voz": 0, "aura": 0, "logo": 0}
    for f in filas_inventario(a.inventario):
        tipo, sub, pieza = f[0], f[1], f[2]
        if tipo not in tipos:
            continue
        idx = f[3] if len(f) > 3 and f[3].isdigit() else -1
        arch = bins.get(pieza, "")
        nombre = pieza.replace("avatar_", "").replace("_X360", "")
        piezas.append({"tipo": tipo, "id": tipos[tipo], "nombre": nombre,
                       "idx_afs": idx, "archivo": arch})
        tipos[tipo] += 1

    # La ropa de UT va DENTRO de cada cuerpo (grupos C_/D_/E_/F_), no como pieza suelta:
    # el catalogo ofrece las opciones nativas de B3 que el creador puede vestir.
    for nombre in ("Gi naranja", "Armadura Saiyan", "Gi de Namek", "Ropa de calle"):
        piezas.append({"tipo": "ropa", "id": tipos["ropa"], "nombre": nombre,
                       "idx_afs": -1, "archivo": ""})
        tipos["ropa"] += 1
    # Auras: color propio (la FORMA es la textura compartida del juego; ver colores.py).
    for nombre in ("Blanca", "Dorada", "Azul", "Roja", "Violeta"):
        piezas.append({"tipo": "aura", "id": tipos["aura"], "nombre": nombre,
                       "idx_afs": -1, "archivo": ""})
        tipos["aura"] += 1

    # piezas terminadas del frente (si existen los bins)
    for clave, rotulo in (("hair000_01", "Pelo 01 (UT)"), ("head_a0_01p", "Cabeza a0 (UT)")):
        if clave in bins and not any(p["archivo"] == bins[clave] for p in piezas):
            tipo = "pelo" if "hair" in clave else "cara"
            piezas.append({"tipo": tipo, "id": tipos[tipo], "nombre": rotulo,
                           "idx_afs": -1, "archivo": bins[clave]})
            tipos[tipo] += 1

    # R16: recetas propias (IDs estables); no contienen modelos/texturas del jugador.
    import partes_originales
    piezas.extend(partes_originales.catalogo())
    import partes_majin
    piezas.extend(partes_majin.catalogo_b3())
    import namek_original
    piezas.extend(namek_original.catalogo_b3())
    import ia_namek
    piezas.extend(ia_namek.catalogo_b3())

    with open(a.salida, "w", encoding="utf-8") as o:
        o.write("# Catalogo del creador de personaje (frente 02, studies 6).\n")
        o.write("# Generado por mod center hd/cac/piezas_cac.py. NO editar a mano.\n\n")
        for p in piezas:
            o.write("[[pieza]]\n")
            for clave in ("tipo", "id", "nombre", "idx_afs", "archivo", "cuerpos"):
                if clave not in p:
                    continue
                valor = p[clave]
                if clave == "idx_afs":
                    valor = int(valor)
                elif clave == "archivo":
                    valor = valor.replace("\\", "/")
                o.write("%s = %s\n" % (clave, json.dumps(valor, ensure_ascii=False)))
            o.write("\n")
    print("piezas:", tipos, "->", a.salida)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
