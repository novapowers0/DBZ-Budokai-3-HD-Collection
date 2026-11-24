#!/usr/bin/env python3
"""HEROE-REC R1: valida res/heroe/recompensas.toml sin juego.
  python tools/test_heroe_recompensas.py [--juego D:/DBZ3HD/sol]
Comprueba: ids unicos y cortos (<48), cada tramo de sagas.toml (indices 0-7 de siempre y 8+ variantes) da >= 1 premio, ninguna
tecnica repite capsula, y (con --juego) toda capsula esta en heroe_tecnicas/catalogo.json y en heroe_vista/tecnicas_tipo.txt
(sin tipo no cabe en ningun hueco) y ninguna es de mod (>= 596)."""
import argparse
import json
import sys
import tomllib
from pathlib import Path

R = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--juego")
    a = ap.parse_args()
    rec = tomllib.loads((R / "res/heroe/recompensas.toml").read_text(encoding="utf-8"))["recompensa"]
    sagas = tomllib.loads((R / "res/heroe/sagas.toml").read_text(encoding="utf-8"))["saga"]
    errores = []
    ids = [r["id"] for r in rec]
    if len(ids) != len(set(ids)):
        errores.append("ids repetidos")
    for r in rec:
        if len(r["id"]) >= 48:
            errores.append("id largo " + r["id"])
        if r["tipo"] not in ("tecnica", "titulo", "escenario"):
            errores.append("tipo " + r["id"])
        if r["tipo"] == "tecnica" and r.get("capsula", 0) <= 0:
            errores.append("sin capsula " + r["id"])
        if r["tipo"] == "escenario" and (type(r.get("escenario")) is not int or r["escenario"] < 5000):
            errores.append("escenario invalido " + r["id"])
        if not any(k in r for k in ("tramo", "vence", "marca", "maestro", "coleccion")):
            errores.append("sin condicion " + r["id"])
        for k in ("nombre_es", "nombre_en", "cond_es", "cond_en"):
            if not r.get(k):
                errores.append("falta %s en %s" % (k, r["id"]))
    por_tramo = {}
    for r in rec:
        if "tramo" in r:
            por_tramo.setdefault(r["tramo"], []).append(r["id"])
    for i in range(len(sagas)):
        if i not in por_tramo:
            errores.append("el tramo %d (%s) no da ningun premio" % (i, sagas[i].get("nombre_es")))
    caps = [r["capsula"] for r in rec if r["tipo"] == "tecnica"]
    if len(caps) != len(set(caps)):
        errores.append("capsulas repetidas")
    if any(c >= 596 for c in caps):
        errores.append("capsula de mod (>= 596)")
    if a.juego:
        j = Path(a.juego)
        instalados = set()
        for stage in (j / "mods").glob("*/stage.toml"):
            try:
                instalados.update(e.get("entrada") for e in tomllib.loads(stage.read_text(encoding="utf-8")).get("escenario", []))
            except (OSError, tomllib.TOMLDecodeError) as exc:
                print("AVISO stage ignorado %s: %s" % (stage, exc))
        for r in rec:
            if r["tipo"] == "escenario" and r.get("escenario") not in instalados:
                errores.append("escenario no instalado " + r["id"])
        cat = {c["id"] for c in json.loads((j / "ingame_ui/heroe_tecnicas/catalogo.json").read_text(encoding="utf-8"))}
        tipos = {int(l.split()[0]) for l in (j / "ingame_ui/heroe_vista/tecnicas_tipo.txt").read_text().splitlines() if l.strip()}
        for c in caps:
            if c not in cat:
                errores.append("capsula %d fuera de catalogo.json" % c)
            if c not in tipos:
                errores.append("capsula %d sin tipo en tecnicas_tipo.txt" % c)
    print("%d recompensas, %d tramos, %d capsulas" % (len(rec), len(sagas), len(caps)))
    for e in errores:
        print("ERROR", e)
    sys.exit(1 if errores else 0)


if __name__ == "__main__":
    main()
