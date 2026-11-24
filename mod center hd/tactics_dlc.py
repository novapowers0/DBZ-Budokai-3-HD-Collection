#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""tactics_dlc.py - Contrato B3-solo de Dragon Tactics (TAC06).

Comprueba, sin juego, que el catalogo actual NO depende de Infinite World (ni de B2) y
que sigue siendo coherente con el manifiesto: cada unidad base del catalogo existe en el
manifiesto, todas sus formas (estrellas) son modelos de B3 del roster base (ids 0-43) y no
hay ninguna clave de fuente/DLC que esconda una dependencia externa.

Decision D2 TAC06: las figuras DLC de IW NO entran en esta version (la ISO de IW es del
jugador y la decision final es suya). Si algun dia entran, deben ser un bloque opcional
que, sin ISO, deje el catalogo y la partida exactamente como hoy.

Uso: python -B "mod center hd/tactics_dlc.py"
Codigo de salida 0 = B3 solo y coherente; 1 = hay una dependencia o un hueco.
"""
import json
import os
import re
import sys
import tomllib

AQUI = os.path.dirname(os.path.abspath(__file__))


def main():
    with open(os.path.join(AQUI, "tactics_units.toml"), "rb") as fh:
        unidades = tomllib.load(fh).get("unidad", [])
    with open(os.path.join(AQUI, "tactics_manifest.toml"), "rb") as fh:
        manifest = tomllib.load(fh).get("unidad", [])
    with open(os.path.join(AQUI, "roster_db.json"), encoding="utf-8") as fh:
        roster = json.load(fh)["ids"]
    por_id = {u["id"]: u for u in manifest}
    base = {u["id"]: u for u in manifest if not re.search(r"_\d$", u["id"])}
    b3_ok = {int(e["id"]) for e in roster if 0 <= int(e["id"]) <= 43}
    problemas = []
    if not unidades:
        problemas.append("tactics_units.toml no tiene unidades")
    for u in unidades:
        uid = u.get("id", "?")
        if uid not in base:
            problemas.append("%s: no esta como unidad base en tactics_manifest.toml" % uid)
        for clave in ("fuente", "dlc", "iw", "origen"):
            if clave in u:
                problemas.append("%s: clave '%s' (dependencia externa): %r" % (uid, clave, u[clave]))
        if int(base.get(uid, {}).get("b3", -1)) not in b3_ok:
            problemas.append("%s: b3 %s no es del roster base de B3 (0-43)" % (uid, base.get(uid, {}).get("b3")))
        for fid in u.get("formas", []):
            if not any(int(m.get("modelo", -1)) == int(fid) for m in por_id.values()):
                problemas.append("%s: forma fid %s no esta en tactics_manifest.toml" % (uid, fid))
    # Unidades del manifiesto que no esten en el catalogo jugable: no rompen el modo, pero se avisan.
    huerfanas = sorted(set(base) - {u.get("id") for u in unidades})
    if huerfanas:
        problemas.append("unidades del manifiesto sin ficha jugable: %s" % ", ".join(huerfanas))
    total_formas = sum(len(u.get("formas", [])) for u in unidades)
    print("tactics_dlc: %d unidades jugables, %d formas (estrellas), %d entradas en el manifiesto; roster B3 base 0-43" % (
        len(unidades), total_formas, len(manifest)))
    if problemas:
        for p in problemas:
            print("FALLO:", p)
        return 1
    print("B3 solo OK: 0 referencias a IW/B2 y 0 huecos (sin ISO de IW el modo queda completo)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
