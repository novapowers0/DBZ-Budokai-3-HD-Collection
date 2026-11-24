"""DLC local: se instala desde una ISO del jugador, sin distribuir recursos."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import tomllib

HERE = Path(__file__).resolve().parent
RECIPE = HERE / "receta" / "personajes.toml"
MARK = "dlcb1.json"


def require(ok, why):
    if not ok:
        raise ValueError(why)


def source(mod):
    return tomllib.loads((mod / "personaje.toml").read_text(encoding="utf-8"))


def check_source(mod, r):
    d = source(mod)
    p = d["personaje"]
    require((p.get("origen_fuente"), p.get("origen_clave"), p.get("id")) == ("b1", r["clave"], r["id"]),
            f"{mod.name}: origen o plaza incorrectos")
    require(p.get("formas", 1) == r["formas"] and d.get("capsula"), f"{mod.name}: faltan formas o cápsulas")
    require(p.get("modelos"), f"{mod.name}: no declara modelos")
    for f in [*p["modelos"], *p["moveset"], p["camara"]]:
        q = mod / f
        require(q.is_file() and q.stat().st_size > 16, f"{mod.name}: recurso ausente {f}")


def check_roster(roster, recipe, installed):
    p = roster / "roster.toml"
    raw = p.read_text(encoding="utf-8") if p.is_file() else ""
    # El roster usa literales propios (@o0, etc.); inspeccionar solo campos estándar.
    blocks = raw.split("[[personaje]]")[1:]
    for name, r in recipe.items():
        rows = [b for b in blocks if re.search(rf'^# .* \(mod {name}\)$', b, re.M)]
        require(len(rows) == int(installed), f"{name}: presencia inesperada en _roster")
        if installed:
            row = rows[0]
            for key, val in [("id", r["id"]), ("celda", "true"), ("modelos_por_traje", r["formas"])]:
                require(re.search(rf'^{key} = {val}$', row, re.M), f"{name}: falta {key}={val}")
            for key in ("modelos", "capsulas"):
                require(re.search(rf'^{key} = \[\d', row, re.M), f"{name}: falta {key}")


def file_id(p):
    s = p.stat()
    return [str(p), s.st_size, s.st_mtime_ns]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("accion", choices=("instalar", "quitar", "verificar"))
    ap.add_argument("--mods", type=Path, required=True)
    ap.add_argument("--us", type=Path, required=True)
    ap.add_argument("--iso", type=Path)
    ap.add_argument("--proyecto", type=Path, default=HERE.parent, help="raíz del kit (para pruebas fuera del árbol)")
    ap.add_argument("--roster-coherente", action="store_true",
                    default=os.environ.get("DBZ3_DLCB1_ROSTER_COHERENTE") == "1",
                    help="confirma fuentes, plantel y reparto juntos (experimental)")
    a = ap.parse_args()
    kit = a.proyecto.resolve() / "mod center hd"
    sys.dont_write_bytecode = True
    sys.path[:0] = [str(kit), str(kit.parent / "awo_tools")]
    import iso
    mods = a.mods.resolve()
    if a.accion == "verificar":
        require(not mods.exists(), "verificar exige una carpeta mods nueva y aislada")
    us = a.us.resolve()
    require((us / "data_cmn.afs").is_file(), "faltan los datos locales de Budokai 3 HD")
    raw = RECIPE.read_bytes()
    recipe = tomllib.loads(raw.decode("utf-8"))
    recipe.pop("version")
    image = None
    if a.accion != "quitar":
        found = a.iso or iso.find_game("b1")
        require(found, "necesita la ISO de Budokai 1")
        image = Path(found).resolve()
        require(iso.serial_of_path(str(image)) in iso.SERIES["b1"], "DLC B1: se requiere una ISO europea SLES-51233")
    mods.mkdir(parents=True, exist_ok=True)
    backup = mods.parent / "mods_respaldo"
    backup.mkdir(exist_ok=True)
    txn = Path(tempfile.mkdtemp(prefix="dlcb1_", dir=backup))
    os.environ["DBZ3_TMP"] = str(txn)
    import afs_pair
    import b1port
    import importar
    import roster_build as rb
    # Acotar solo B1 a la ISO que escogió el launcher, sin cambiar las rutas de IW/B2.
    if image is not None:
        b1port.find_b1_iso = lambda: str(image)
    # El DLC no requiere B3 de PS2 ni modifica los AFS. Las plantillas salen de B3 HD.
    hd = afs_pair.hd
    afs_pair.hd = lambda n, afs="data_cmn.afs", region=None: hd(n, afs, str(us) if region is None else region)
    ps2 = afs_pair.ps2
    ps2_missing = str(txn / "sin_b3_ps2")
    afs_pair.PS2_GH = ps2_missing
    afs_pair.ps2 = lambda n, afs="data_cmn.afs", game=ps2_missing, real=False: ps2(n, afs, game, real)
    rb.asegurar_copias_pt_ja = lambda _us: None  # datos originales en solo lectura; el launcher gestiona la caché ISO
    validate = rb.validate_roster

    def validate_dlc(stage):
        validate(stage)
        check_roster(Path(stage), recipe, image is not None)

    rb.validate_roster = validate_dlc
    args_roster = argparse.Namespace(mods=str(mods), us=str(us), force=False,
                                     dlcb1_coherente=a.roster_coherente)
    old = [mods / folder for n in recipe for folder in (n, n + ".disabled") if (mods / folder).exists()]
    for p in old:
        require(p.is_dir() and not p.is_symlink() and (p / MARK).is_file(), f"{p.name}: carpeta ajena al DLC")
    if image is not None:
        stamp = [file_id(image), file_id(us / "data_cmn.afs"), hashlib.sha256(
            raw + Path(__file__).read_bytes() + Path(importar.__file__).read_bytes() + Path(b1port.__file__).read_bytes()
        ).hexdigest()]
        cached = len(old) == len(recipe) and all(p.name in recipe and not (p / ".disabled").exists() for p in old)
        if cached:
            try:
                for p in old:
                    check_source(p, recipe[p.name])
                    require(json.loads((p / MARK).read_text(encoding="utf-8")) == stamp, "caché obsoleta")
            except (OSError, ValueError, KeyError):
                cached = False
        if cached:
            rb.build(args_roster)
            if not a.roster_coherente:
                check_roster(mods / "_roster", recipe, True)
            print("OK DLC B1: caché reutilizada", flush=True)
            return 0
        stage = txn / "nuevos"
        stage.mkdir()
        for name, r in recipe.items():
            importar.do_import(argparse.Namespace(fuente="b1", clave=r["clave"], mod=name, nombre=r["nombre"],
                donante=r["donante"], id=r["id"], despues_de=r["donante"], carpeta=None, mods=str(stage),
                us=str(us), golpes_donante=False, preset=None, opcion=None))
            check_source(stage / name, r)
            (stage / name / MARK).write_text(json.dumps(stamp), encoding="utf-8")
        require(file_id(image) == stamp[0] and file_id(us / "data_cmn.afs") == stamp[1], "cambió la fuente durante la conversión")
    afs_pair.ps2 = ps2
    # Restablecer la fuente PS2 que usan posibles mods de otros juegos durante el montaje.
    afs_pair.PS2_GH = ps2.__defaults__[1]
    moved, added = [], []
    try:
        for p in old:
            p.rename(txn / p.name)
            moved.append(p)
        if image is not None:
            srcs = rb.source_mods(str(mods))
            keys = {r["clave"] for r in recipe.values()}
            require(not any(c.get("origen_fuente") == "b1" and c.get("origen_clave") in keys for _, _, c in srcs),
                    "ya hay otro Zarbon/Dodoria importado: desactívalo antes de instalar el DLC")
            for name in recipe:
                (stage / name).rename(mods / name)
                added.append(name)
            ids, _, _ = rb.plan_ids(str(mods), rb.source_mods(str(mods)))
            require(all(ids.get(n) == r["id"] for n, r in recipe.items()), "plazas 25/26 ocupadas o selección llena")
        rb.build(args_roster)
    except (Exception, SystemExit):
        for name in reversed(added):
            (mods / name).rename(stage / name)
        for p in reversed(moved):
            (txn / p.name).rename(p)
        raise
    if not a.roster_coherente:
        check_roster(mods / "_roster", recipe, image is not None)
    if image is not None:
        for r in recipe.values():
            print(f"VERIFICADO: {r['nombre']} ID {r['id']}, celda, modelos y cápsulas", flush=True)
    print("OK DLC B1: " + ("montado" if image is not None else "retirado a mods_respaldo"), flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:
        sys.exit(f"ERROR DLC B1: {e}")
