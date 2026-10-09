#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""auditar_importar.py - Importa (y monta) en una carpeta aparte TODOS los personajes de una fuente
del importador y anota los fallos, para que nadie se los encuentre al portearlos.

  python auditar_importar.py --fuente sb2 --mods D:/tmp/aud_sb2 [--desde 0 --hasta 20] [--construir]

Cada personaje se importa en su propio proceso (importar.py importar ...) dentro de --mods (que no
debe ser la carpeta de mods del juego). Con --construir se monta el _roster de esa carpeta (de 18 en
18: las plazas libres) y se leen sus avisos. Informe TSV en --mods/auditoria_<fuente>.tsv:
  clave  nombre  estado(OK|AVISO|ERROR)  detalle
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
ENV = dict(os.environ, PYTHONIOENCODING="utf-8")
LOTE = 18          # plazas nuevas por montaje (IDs 44-63 menos margen)


def run(args, timeout=1800):
    try:
        r = subprocess.run([PY] + args, capture_output=True, text=True, encoding="utf-8", errors="replace",
                           env=ENV, timeout=timeout, cwd=HERE)
        return r.returncode, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return -1, "tiempo agotado (%d s)" % timeout


def problemas(texto):
    """Lineas de aviso / error de un registro (sin duplicados, en orden)."""
    out = []
    for ln in texto.splitlines():
        s = ln.strip()
        if re.search(r"aviso|ERROR|Traceback|Error:|Unsupported|Exception:|fallo|CON ERRORES|^!! ", s, re.I) and s not in out:
            out.append(s)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fuente", required=True, choices=("b1", "b2", "b3", "iw", "sb1", "sb2", "sdbh"))
    ap.add_argument("--mods", required=True, help="carpeta de trabajo (se crea; NO la de mods del juego)")
    ap.add_argument("--desde", type=int, default=0)
    ap.add_argument("--hasta", type=int, default=10 ** 6)
    ap.add_argument("--solo-port", action="store_true", help="iw: solo los que tienen port de la comunidad")
    ap.add_argument("--construir", action="store_true", help="monta el _roster y lee sus avisos")
    a = ap.parse_args()
    mods = os.path.abspath(a.mods)
    if os.path.basename(mods).lower() == "mods" and os.path.isfile(os.path.join(os.path.dirname(mods), "dbz3.exe")):
        raise SystemExit("ERROR: --mods es la carpeta de mods del juego; usa una carpeta aparte")
    os.makedirs(mods, exist_ok=True)
    rc, txt = run(["importar.py", "lista", a.fuente])
    lst = [ln.split("\t") for ln in txt.splitlines() if ln.startswith("personaje\t")]
    if a.solo_port:
        lst = [x for x in lst if x[6] == "1"]
    lst = lst[a.desde:a.hasta]
    print("%s: %d personajes" % (a.fuente, len(lst)), flush=True)
    rows = {}
    for k, x in enumerate(lst):
        clave, nombre = x[1], x[2]
        mod = "aud_%s_%02d" % (a.fuente, a.desde + k)
        d = os.path.join(mods, mod)
        if os.path.isdir(d):
            shutil.rmtree(d)          # carpeta de trabajo propia de la auditoria
        rc, txt = run(["importar.py", "importar", a.fuente, clave, "--mod", mod, "--nombre",
                       (x[7] or nombre)[:20], "--mods", mods])
        p = problemas(txt)
        estado = "ERROR" if rc != 0 else ("AVISO" if p else "OK")
        rows[mod] = [clave, nombre, estado, p]
        print("%-6s %-32s %s %s" % (estado, nombre[:32], clave, (p[-1] if p else "")[:120]), flush=True)
    if a.construir:
        mods_ok = [m for m, r in rows.items() if r[2] != "ERROR"]
        for i in range(0, len(mods_ok), LOTE):
            lote = set(mods_ok[i:i + LOTE])
            for m in mods_ok:           # solo el lote activo (los demas, desactivados)
                toml = os.path.join(mods, m, "personaje.toml")
                off = toml + ".off"
                if m in lote and os.path.isfile(off):
                    os.replace(off, toml)
                elif m not in lote and os.path.isfile(toml):
                    os.replace(toml, off)
            rc, txt = run(["roster_build.py", "construir", "--mods", mods, "--force"], timeout=7200)
            for m in lote:
                nm = rows[m][1]
                mine = [ln for ln in problemas(txt) if nm.lower()[:12] in ln.lower() or m in ln]
                if rc != 0 and not mine:
                    mine = ["montaje fallo: " + (problemas(txt) or [txt.strip()[-200:]])[-1]]
                if mine:
                    rows[m][3] += ["montaje: " + s for s in mine]
                    rows[m][2] = "ERROR" if rc != 0 or any("Traceback" in s or "ERROR" in s for s in mine) \
                        else ("AVISO" if rows[m][2] == "OK" else rows[m][2])
            print("montaje %d-%d: codigo %d" % (i, i + len(lote) - 1, rc), flush=True)
    rep = os.path.join(mods, "auditoria_%s_%d.tsv" % (a.fuente, a.desde))
    with open(rep, "w", encoding="utf-8") as fh:
        for m, (clave, nombre, estado, p) in rows.items():
            fh.write("\t".join([clave, nombre, estado, " | ".join(p)]) + "\n")
    n = {s: sum(1 for r in rows.values() if r[2] == s) for s in ("OK", "AVISO", "ERROR")}
    print("RESUMEN %s: %s -> %s" % (a.fuente, n, rep))
    return 1 if n["ERROR"] else 0


if __name__ == "__main__":
    sys.exit(main())
