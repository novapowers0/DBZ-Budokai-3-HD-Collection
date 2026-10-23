#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""catalogo_tecnicas.py - Catalogo de tecnicas por personaje para el Kit (E1, studies 5).

Junta dos cosas, sin duplicar lo que ya hace cac/tecnicas_cartas.py (linea C):
  * nativas: cac.tecnicas_cartas.catalogo() lee el #SKC del juego del jugador (id, donante, ki, rareza, clase);
    los nombres de las nativas son imagenes del propio juego (las cartas de tecnicas_cartas.py), no texto.
  * importadas: los [[capsula]] de mods/*/personaje.toml (nombre, tipo, ki, origen) con el personaje al que pertenecen.

  python catalogo_tecnicas.py --mods DIR [--salida catalogo_tecnicas.json] [--sin-nativas] [--us DIR]
"""
import argparse
import glob
import json
import os
import sys
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, "cac")]


def importadas(mods):
    out = []
    for f in sorted(glob.glob(os.path.join(mods, "*", "personaje.toml"))):
        with open(f, "rb") as fh:
            d = tomllib.load(fh)
        p = d.get("personaje", {})
        out.append({"mod": os.path.basename(os.path.dirname(f)), "personaje": p.get("nombre"), "donante": p.get("donante"),
                    "origen": p.get("preset"), "capsulas": [{k: c[k] for k in ("nombre", "tipo", "ki") if k in c}
                                                            for c in d.get("capsula", [])]})
    return out


def nativas(us):
    import roster_build as rb
    import tecnicas_cartas as tc
    import tempfile
    work = tempfile.mkdtemp(prefix="cat_tec_", dir="D:/tmp" if os.path.isdir("D:/tmp") else None)
    return tc.catalogo(rb.Afs(os.path.join(rb.us_dir(us), "data_usi.afs"), work).entry(4))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mods", required=True)
    ap.add_argument("--salida", default="catalogo_tecnicas.json")
    ap.add_argument("--sin-nativas", action="store_true")
    ap.add_argument("--us")
    a = ap.parse_args()
    cat = {"importadas": importadas(a.mods), "nativas": [] if a.sin_nativas else nativas(a.us)}
    with open(a.salida, "w", encoding="utf-8") as fh:
        json.dump(cat, fh, ensure_ascii=False, indent=1)
    n = sum(len(p["capsulas"]) for p in cat["importadas"])
    print("OK %s: %d personajes importados, %d capsulas, %d nativas" % (a.salida, len(cat["importadas"]), n, len(cat["nativas"])))
    assert all(c.get("nombre") for p in cat["importadas"] for c in p["capsulas"]), "capsula sin nombre"


if __name__ == "__main__":
    main()
