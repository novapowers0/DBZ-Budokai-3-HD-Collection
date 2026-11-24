"""Test SIN JUEGO de la Evolucion por raza (regla raza + marcas -> formas abiertas).
Lee las formas REALES de res/heroe/desbloqueos.toml, comprueba que los datos son coherentes y compila y lanza
tools/test_heroe_formas.cpp (src/heroe_formas.h, la misma regla que usa el juego). Uso: python tools/test_heroe_formas.py"""
import os, shutil, subprocess, sys, tempfile, tomllib
from pathlib import Path

R = Path(__file__).resolve().parent.parent


def compilador():
    for n in ("clang++", "g++", "c++"):
        p = shutil.which(n)
        if p:
            return p
    base = Path.home() / ".local/share/retcomm/toolchains/cmake-clang-v1/latest/bin"
    for n in ("clang++.exe", "g++.exe"):
        if (base / n).exists():
            return str(base / n)
    sys.exit("no hay compilador de C++")


def main():
    datos = tomllib.loads((R / "res/heroe/desbloqueos.toml").read_text(encoding="utf-8"))
    formas = [d for d in datos["desbloqueo"] if d.get("tipo") == "forma"]
    ids = [d["id"] for d in formas]
    assert len(ids) == len(set(ids)), "ids de forma repetidos"
    por_id = {d["id"]: d for d in formas}
    for d in formas:
        assert 0 <= d["raza"] <= 5, d["id"]
        assert d.get("rama", "") in ("", "A", "B"), d["id"]
        p = d.get("previa", "")
        assert not p or (p in por_id and por_id[p]["raza"] == d["raza"]), f"{d['id']}: previa {p} no es de su raza"
        for k in ("nombre_es", "nombre_en", "cond_es", "cond_en"):
            assert d.get(k), f"{d['id']}: falta {k}"
        if d.get("rama"):
            assert d.get("camino_es") and d.get("camino_en"), f"{d['id']}: rama sin nombre de camino"
    for r in range(6):
        assert any(d["raza"] == r for d in formas), f"raza {r} sin formas"
    tmp = Path(tempfile.mkdtemp(prefix="formas_", dir=os.environ.get("TEMP")))
    tsv = tmp / "formas.tsv"
    v = lambda s: s if s else "-"
    tsv.write_text("\n".join(f"{d['id']} {d['raza']} {v(d.get('rama',''))} {v(d.get('previa',''))} {v(d.get('marca',''))} "
                             f"{v(d.get('abre_con',''))} {d.get('vence', -1)}" for d in formas) + "\n", encoding="utf-8")
    exe = tmp / "test_formas.exe"
    r = subprocess.run([compilador(), "-std=c++20", "-O0", "-o", str(exe), str(R / "tools/test_heroe_formas.cpp")],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("no compila:\n" + r.stdout + r.stderr)
    r = subprocess.run([str(exe), str(tsv)], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip())
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
