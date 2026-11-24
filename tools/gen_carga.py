r"""Generador del banco de slots: N personajes sobre una copia fija, por el camino normal (personaje.toml -> roster_build).
Sin copias nuevas del juego: los N mods son ENLACES DUROS a una plantilla (mismo volumen que la copia) con nombre propio.

  python tools/gen_carga.py gen 120 [--copia sol] [--origen imp_iw_gogeta_ss4]   crea mods/carga_000..119 (+ respaldo del roster)
  python tools/gen_carga.py quitar [--copia sol]                                  mueve carga_* a D:\_PARA_BORRAR_CLAUDE y restaura roster_ids
  python tools/gen_carga.py construir [--copia sol]                               roster_build construir (lo mismo que hace JUGAR)

Entradas (origen de la plantilla): E:\DBZ3HD_trabajo\slots\entradas\<origen> (se copia una vez a <copia>\mods\_carga_src).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

R = Path(__file__).resolve().parent.parent
BASE = Path(r"D:\DBZ3HD")
ENTRADAS = Path(r"E:\DBZ3HD_trabajo\slots\entradas")
RESPALDO = Path(r"E:\DBZ3HD_trabajo\slots\respaldo")
BORRAR = Path(r"D:\_PARA_BORRAR_CLAUDE")
PLANTILLA = "_carga_src"
NOMBRE = re.compile(r'^nombre = ".*"$', re.M)


def _mods(copia):
    return BASE / copia / "mods"


def gen(copia, n, origen="imp_iw_gogeta_ss4"):
    mods = _mods(copia)
    src = mods / PLANTILLA
    if not src.is_dir():
        shutil.copytree(ENTRADAS / origen, src)
    if (src / "personaje.toml").is_file():      # la plantilla NO es un mod: sin personaje.toml el roster no la cuenta
        (src / "personaje.toml").rename(src / "personaje.base")
    base = (src / "personaje.base").read_text(encoding="utf-8-sig")
    RESPALDO.mkdir(parents=True, exist_ok=True)
    ids = mods / "roster_ids.toml"
    if ids.is_file() and not (RESPALDO / f"roster_ids_{copia}.toml").exists():
        shutil.copy2(ids, RESPALDO / f"roster_ids_{copia}.toml")      # para volver a <=24 sin rastro
    nuevos = 0
    for k in range(n):
        d = mods / ("carga_%03d" % k)
        if d.is_dir():
            continue
        for root, _, files in os.walk(src):
            rel = os.path.relpath(root, src)
            (d / rel).mkdir(parents=True, exist_ok=True)
            for f in files:
                if f not in ("personaje.toml", "personaje.base") and not (d / rel / f).exists():
                    os.link(os.path.join(root, f), d / rel / f)
        (d / "personaje.toml").write_text(NOMBRE.sub('nombre = "Carga %03d"' % k, base, count=1), encoding="utf-8")
        nuevos += 1
    sobran = [d for d in sorted(mods.glob("carga_*")) if int(d.name[6:]) >= n]
    for d in sobran:
        _a_borrar(d)
    print(f"gen: {n} mods en {mods} ({nuevos} nuevos, {len(sobran)} de sobra movidos)")


def _a_borrar(d):
    BORRAR.mkdir(exist_ok=True)
    dst = BORRAR / f"{d.parent.parent.name}_{d.name}_{time.strftime('%H%M%S')}"
    shutil.move(str(d), str(dst))      # mismo volumen: renombrado, los enlaces duros no se copian


def quitar(copia):
    mods = _mods(copia)
    for d in sorted(mods.glob("carga_*")):
        _a_borrar(d)
    r = RESPALDO / f"roster_ids_{copia}.toml"
    if r.is_file():
        shutil.copy2(r, mods / "roster_ids.toml")
        r.unlink()
    print("quitar: carga_* movidos a", BORRAR, "; roster_ids restaurado")


def construir(copia):
    t0 = time.time()
    r = subprocess.run([sys.executable, str(R / "mod center hd" / "roster_build.py"), "construir", "--mods", str(_mods(copia))],
                       capture_output=True, text=True, errors="replace")
    print(f"construir: codigo {r.returncode} en {time.time() - t0:.0f} s: {(r.stdout.strip().splitlines() or [''])[-1]}")
    if r.returncode:
        print((r.stderr or r.stdout)[-600:])
    return r.returncode


def revisar(copia, esperados=None):
    """Sin juego: cada personaje del roster montado tiene icono con casilla propia (sin repetir) y el icono fuente no esta vacio."""
    import tomllib
    doc = tomllib.loads((_mods(copia) / "_roster" / "roster.toml").read_text(encoding="utf-8"))
    ps = doc.get("personaje", [])
    malos, vistos = [], {}
    for p in ps:
        if p.get("celda") is False:
            continue
        ic = p.get("icono")
        if not ic or len(ic) < 5 or ic[3] <= 0 or ic[4] <= 0:
            malos.append(f"{p.get('nombre')}: sin icono")
        elif tuple(ic[:3]) in vistos:
            malos.append(f"{p.get('nombre')}: icono repetido con {vistos[tuple(ic[:3])]}")
        else:
            vistos[tuple(ic[:3])] = p.get("nombre")
    fuente = _mods(copia) / PLANTILLA / "ui" / "_vista" / "icono.rgba"
    if fuente.is_file() and not any(fuente.read_bytes()[3::4]):
        malos.append("icono.rgba de la plantilla totalmente transparente")
    print(f"revisar: {len(ps)} personajes en el roster, {len(vistos)} iconos distintos, {len(malos)} malos {malos[:3]}")
    if esperados is not None and len(ps) < esperados:
        malos.append(f"faltan personajes ({len(ps)} < {esperados})")
    return malos


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("modo", choices=["gen", "quitar", "construir", "revisar"])
    ap.add_argument("n", nargs="?", type=int, default=120)
    ap.add_argument("--copia", default="sol")
    ap.add_argument("--origen", default="imp_iw_gogeta_ss4")
    a = ap.parse_args()
    if a.modo == "gen":
        gen(a.copia, a.n, a.origen)
    elif a.modo == "quitar":
        quitar(a.copia)
    elif a.modo == "revisar":
        sys.exit(1 if revisar(a.copia) else 0)
    else:
        sys.exit(construir(a.copia))
