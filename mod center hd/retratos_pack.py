#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""retratos_pack.py - Retratos de hablante opcionales del usuario para los dialogos de las historias (arte de terceros).

Igual que el pack opcional de Dragon HUB: el arte NUNCA va en el release, el repo ni el zip del Kit. Se lee en
tiempo de importacion de la carpeta del usuario (<raiz de docs/13_assets_externos/indice.json>/
Dragon HUB (pack opcional)/Potraits/, o DBZ3_RETRATOS=<carpeta>). Cada PNG es un personaje: el nombre del fichero
(sin .png) es el nombre del hablante. Lo usan las historias/campanas: en campana.py, `[[retrato]] png = "pack:Zarbon"`
(se encaja en la caja del retrato de dialogo nativo). Soltar un PNG nuevo en la carpeta basta; si la carpeta o el PNG no
estan, no pasa nada y se usa el retrato del juego (`sin_png`). NO se usa para el select ni para crear personajes.

  python retratos_pack.py [lista]            lista los retratos y la carpeta
  python retratos_pack.py buscar NOMBRE      ruta del retrato que corresponde a ese personaje (o nada)
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
INDICE = os.path.join(os.path.dirname(HERE), "docs", "13_assets_externos", "indice.json")
SUBCARPETA = os.path.join("Dragon HUB (pack opcional)", "Potraits")


def _norm(s):
    return re.sub(r"[^a-z0-9]", "", str(s).lower())


def carpeta():
    """Carpeta de retratos del usuario o None si no existe."""
    c = os.environ.get("DBZ3_RETRATOS")
    if not c:
        try:
            with open(INDICE, encoding="utf-8") as fh:
                c = os.path.join(json.load(fh)["raiz"], SUBCARPETA)
        except (OSError, KeyError, ValueError):
            return None
    return c if os.path.isdir(c) else None


def lista():
    """[(nombre, ruta)] de los PNG de la carpeta, por nombre."""
    c = carpeta()
    return sorted((os.path.splitext(f)[0], os.path.join(c, f)) for f in os.listdir(c)
                  if f.lower().endswith(".png")) if c else []


def buscar(*nombres, exacto=False):
    """Ruta del retrato cuyo nombre esta contenido en alguno de los nombres (el mas largo gana); exacto: el mismo nombre."""
    ns = [_norm(n) for n in nombres if n]
    if exacto:
        return next((r for n, r in lista() if _norm(n) in ns), None)
    hit = [(len(_norm(n)), r) for n, r in lista() if _norm(n) and any(_norm(n) in x for x in ns)]
    return max(hit)[1] if hit else None


def _prueba():
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        for f in ("Zarbon.png", "Dodoria.png", "Zarbon Final.png", "nota.txt"):
            open(os.path.join(d, f), "wb").close()
        os.environ["DBZ3_RETRATOS"] = d
        assert [n for n, _ in lista()] == ["Dodoria", "Zarbon", "Zarbon Final"]
        assert buscar("Zarbon B1 E1").endswith("Zarbon.png") and buscar("DODORIA (B1)").endswith("Dodoria.png")
        assert buscar("Zarbon Final Form").endswith("Zarbon Final.png")      # el nombre mas largo gana
        assert buscar("Goku") is None and buscar("zarbon", exacto=True).endswith("Zarbon.png")
        assert buscar("Zarbon B1", exacto=True) is None
        os.environ["DBZ3_RETRATOS"] = os.path.join(d, "no_existe")
        assert lista() == [] or carpeta() is not None
    print("retratos_pack: prueba OK")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["--prueba"]:
        _prueba()
    elif a[:1] == ["buscar"]:
        print(buscar(*a[1:]) or "")
    else:
        print("carpeta:", carpeta() or "(no existe: se usan los retratos de los modelos)")
        for n, r in lista():
            print("  %-24s %s" % (n, r))
