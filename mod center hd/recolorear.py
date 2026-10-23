#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""recolorear.py - Trajes de color propios por region (HSV) para personajes del juego (E1, studies 5).

Es el motor que usara el menu del select (fase 2): lista las regiones de color de un personaje, crea un traje extra con
tono/saturacion/brillo por region y lo quita (reset). Usa lo que ya existia: traje.toml (roster_build.build_trajes anade
el traje DETRAS de los del juego: nunca se pisa uno nativo) y colores.recolor_regiones (HSV sobre las texturas DXT3 del
modelo; el alfa, que elige la banda toon, no se toca).

  python recolorear.py regiones --personaje 0 [--forma 0]
  python recolorear.py crear --personaje 0 --mod color_goku --mods DIR [--nombre "Goku rojo"] \\
         --region todas:tono=40 --region 2:sat=1.3,val=0.8
  python recolorear.py ficha --personaje 0 [--juego DIR]          regiones con nombre (Piel, Pelo, Ropa 1...), en cache
  python recolorear.py vista --personaje 0 --regiones-archivo R.json --salida X.png   vista previa (lo usa el select)
  python recolorear.py reset --mod color_goku --mods DIR        (mueve traje.toml a _respaldo/, no borra)

Region = indice de textura DXT3 del modelo (salen de `regiones`) o "todas". El mismo ajuste se aplica al modelo de cada
forma (por indice de textura). Solo personajes del juego (los puertos/mods se recolorean con tono/colores en su traje).
"""
import argparse
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def modelos_por_forma(rb, cid):
    """['juego:N' por forma] como importar.traje_desde_port: el modelo del traje 1 de cada forma."""
    dn = rb.IDS[cid]
    per = max(1, dn["models_per_costume"])
    fm = dn.get("form_models") or [[cid, k] for k in range(per)]
    out = []
    for fid, mi in fm:
        d = dn if fid == cid else rb.DB.get("form_ids", {}).get(str(fid), {})
        ms = d.get("models") or dn["models"]
        out.append(ms[min(mi, max(0, d.get("models_per_costume", per) - 1))])
    return out


def parse_region(txt):
    tex, _, kv = txt.partition(":")
    r = {"textura": "todas" if tex == "todas" else int(tex)}
    for par in filter(None, kv.split(",")):
        k, v = par.split("=")
        if k not in ("tono", "sat", "val"):
            raise SystemExit("ajuste desconocido %r (tono, sat, val)" % k)
        r[k] = float(v)
    return r


def toml_traje(cid, nombre, nats, regs):
    """cid = ID del juego (modelos = entradas `juego:N`) o carpeta de un mod (modelos = sus ficheros, `nats` = rutas)."""
    s = ["# Traje de color propio (recolorear.py): HSV por region sobre los modelos del personaje; reset = mover este fichero.",
         "[traje]", "personaje = %s" % (json.dumps(cid) if isinstance(cid, str) else cid),
         "nombre = %s" % json.dumps(nombre, ensure_ascii=False),
         "modelos = [%s]" % ", ".join(json.dumps(n) if isinstance(n, str) else '"juego:%d"' % n for n in nats), ""]
    for r in regs:
        s += ["[[traje.colores]]"] + ["%s = %s" % (k, json.dumps(v)) for k, v in r.items()] + [""]
    return "\n".join(s)


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    for nm in ("regiones", "crear", "reset", "ficha", "vista"):
        p = sp.add_parser(nm)
        p.add_argument("--us")
        p.add_argument("--mods")
        if nm in ("ficha", "vista"):
            p.add_argument("--juego", default=os.path.dirname(HERE), help="carpeta del juego (cache en ingame_ui/color)")
            p.add_argument("--salida")
            p.add_argument("--regiones-archivo", help="JSON [{textura, tono, sat, val}] (vista)")
            p.add_argument("--ss", type=int, default=2)
        if nm != "reset":
            p.add_argument("--personaje", type=int, required=True)
        if nm == "regiones":
            p.add_argument("--forma", type=int, default=0)
        if nm == "crear":
            p.add_argument("--mod", required=True)
            p.add_argument("--nombre")
            p.add_argument("--region", action="append", required=True)
        if nm == "reset":
            p.add_argument("--mod", required=True)
    a = ap.parse_args()
    import roster_build as rb  # noqa: PLC0415
    mods = a.mods or rb.default_mods()
    if a.cmd == "reset":
        f = os.path.join(mods, a.mod, "traje.toml")
        if not os.path.isfile(f):
            raise SystemExit("%s no tiene traje.toml" % a.mod)
        bak = os.path.join(mods, a.mod, "_respaldo")
        os.makedirs(bak, exist_ok=True)
        shutil.move(f, os.path.join(bak, "traje_%s.toml" % time.strftime("%Y%m%d_%H%M%S")))
        print("OK: traje.toml movido a", bak)
        return
    import color_vista as cv  # noqa: PLC0415
    cv.MODS = mods
    if a.cmd in ("ficha", "vista"):
        if a.cmd == "ficha":
            cache = os.path.join(a.juego, "ingame_ui", "color")
            f = cv.ficha(a.personaje, cache)
            with open(os.path.join(cache, "ficha_%d.tsv" % a.personaje), "w", encoding="utf-8", newline="\n") as fh:   # lo lee el select (C++)
                fh.writelines("%d\t%s\n" % (r["textura"], r["nombre"]) for r in f["regiones"])
            print(json.dumps(f, ensure_ascii=False))
        else:
            regs = json.load(open(a.regiones_archivo, encoding="utf-8")) if a.regiones_archivo else []
            print("vista %.2f s -> %s" % (cv.vista(a.personaje, regs, a.salida, a.ss), a.salida))
        return
    src = cv.fuente(a.personaje)                   # personaje de mod (carpeta) o None si es del juego
    if src is None:
        nats = modelos_por_forma(rb, a.personaje)
        quien, nombre_base = a.personaje, rb.IDS[a.personaje]["name"]
    else:
        nats, quien, nombre_base = src["ficheros"], src["carpeta"], src["nombre"]
    if a.cmd == "regiones":
        print(json.dumps({"personaje": nombre_base, "formas": len(nats), "regiones": cv.ficha(a.personaje, os.path.join(
            os.path.dirname(HERE), "ingame_ui", "color"))["regiones"]}, ensure_ascii=False, indent=1))
        return
    regs = [parse_region(r) for r in a.region]
    d = os.path.join(mods, a.mod)
    os.makedirs(d, exist_ok=True)
    f = os.path.join(d, "traje.toml")
    if os.path.isfile(f):                          # nunca se pisa: el anterior va a _respaldo
        os.makedirs(os.path.join(d, "_respaldo"), exist_ok=True)
        shutil.move(f, os.path.join(d, "_respaldo", "traje_%s.toml" % time.strftime("%Y%m%d_%H%M%S")))
    with open(f, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(toml_traje(quien, a.nombre or ("%s color" % nombre_base), nats, regs))
    print("OK: %s (modelos de las formas: %s, %d region(es))" % (f, nats, len(regs)))


if __name__ == "__main__":
    main()
