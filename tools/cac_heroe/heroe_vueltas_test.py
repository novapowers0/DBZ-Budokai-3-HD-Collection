#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Regresion offline de NHER4 (dbz3_heroe_coleccion_vueltas) contra el C++ y UI integrados, sin abrir el juego.

Compila y enlaza directamente src/heroe.cpp, src/cac/cac.cpp y los .inl de Coleccion/Mapa/Habitacion
en una carpeta temporal usando clang++ (C++20) y ejecuta:
  - 8 escenarios C++ reales (338 comprobaciones): default_off, corrupt_no_overwrite, io_extra_keys,
    shared_consumers, four_laps_p1, four_laps_p2, du_hp_mirror y tabs_equip_parity.
  - Arnes UI real (38 comprobaciones): pestanas 0/3, CapsulaV2, etiqueta Cuenta, MapaUI/DibujarMapaV2
    y ValorHabitacion(4).

Uso:
  python -B -X utf8 tools/cac_heroe/heroe_vueltas_test.py --help
  python -B -X utf8 tools/cac_heroe/heroe_vueltas_test.py --self-check
  python -B -X utf8 tools/cac_heroe/heroe_vueltas_test.py --self-check --json reporte.json
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

CLANG_CANDIDATES = [
    Path(r"C:\Program Files\LLVM\bin\clang++.exe"),
    Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang++.exe"),
]

SCENARIOS = [
    ("default_off", "u_off"),
    ("corrupt_no_overwrite", "u_corrupt"),
    ("io_extra_keys", "u_io"),
    ("shared_consumers", "u_shared"),
    ("four_laps_p1", "u_laps"),
    ("four_laps_p2", "u_laps"),
    ("du_hp_mirror", "u_mirror"),
    ("tabs_equip_parity", "u_tabs"),
]


def find_cxx(explicit: str | None) -> str:
    if explicit:
        return explicit
    found = shutil.which("clang++")
    if found:
        return found
    for c in CLANG_CANDIDATES:
        if c.is_file():
            return str(c)
    raise FileNotFoundError("clang++ no encontrado en PATH ni rutas estandar (pasa --cxx)")


def run_suite(root: Path, scripts: Path, cxx: str) -> dict:
    stubs = scripts / "stubs_nher4"
    results: dict = {"scenarios": {}, "ui": {}, "ok": False}

    with tempfile.TemporaryDirectory(prefix="dbz3_heroe_vueltas_") as td:
        tdir = Path(td)
        common = [
            cxx, "-std=c++20", "-O2", "-DNOMINMAX", "-D_CRT_SECURE_NO_WARNINGS",
            "-I", str(stubs),
            "-I", str(root / "src"),
            "-I", str(root / "rexglue" / "include"),
        ]
        heroe_obj = tdir / "heroe.o"
        cac_obj = tdir / "cac.o"
        for src, obj in [(root / "src" / "heroe.cpp", heroe_obj), (root / "src" / "cac" / "cac.cpp", cac_obj)]:
            r = subprocess.run([*common, "-c", str(src), "-o", str(obj)], capture_output=True, text=True, timeout=120)
            if r.returncode != 0:
                raise RuntimeError(f"Error compilando {src.name}:\n{r.stderr}")

        exe1 = tdir / "test_heroe_nher4.exe"
        r1 = subprocess.run(
            [*common, str(scripts / "test_heroe_nher4.cpp"), str(heroe_obj), str(cac_obj), "-o", str(exe1)],
            capture_output=True, text=True, timeout=120,
        )
        if r1.returncode != 0:
            raise RuntimeError(f"Error enlazando test_heroe_nher4:\n{r1.stderr}")

        exe2 = tdir / "test_heroe_ui_nher4.exe"
        r2 = subprocess.run(
            [*common, str(scripts / "test_heroe_ui_nher4.cpp"), str(heroe_obj), str(cac_obj), "-o", str(exe2)],
            capture_output=True, text=True, timeout=120,
        )
        if r2.returncode != 0:
            raise RuntimeError(f"Error enlazando test_heroe_ui_nher4:\n{r2.stderr}")

        fake_root = tdir / "game"
        shutil.copytree(root / "res" / "heroe", fake_root / "res" / "heroe")
        mods_dir = fake_root / "mods"
        mods_dir.mkdir(parents=True, exist_ok=True)

        all_ok = True
        for sc, udir in SCENARIOS:
            r = subprocess.run(
                [str(exe1), "--scenario", sc, "--userdir", str(tdir / udir), "--modsdir", str(mods_dir)],
                capture_output=True, text=True, timeout=30,
            )
            results["scenarios"][sc] = {"rc": r.returncode, "out": r.stdout.strip(), "err": r.stderr.strip()}
            if r.returncode != 0:
                all_ok = False

        rui = subprocess.run(
            [str(exe2), "--userdir", str(tdir / "u_ui"), "--modsdir", str(mods_dir)],
            capture_output=True, text=True, timeout=30,
        )
        results["ui"] = {"rc": rui.returncode, "out": rui.stdout.strip(), "err": rui.stderr.strip()}
        if rui.returncode != 0:
            all_ok = False
        results["ok"] = all_ok
    return results


def main() -> int:
    scripts = Path(__file__).resolve().parent
    default_root = scripts.parents[1]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-check", "--autoprueba", dest="self_check", action="store_true",
                    help="Compila y ejecuta los 8 escenarios C++ y el arnes UI sobre el arbol integrado.")
    ap.add_argument("--root", type=Path, default=default_root, help="Raiz del proyecto DBZ3 HD.")
    ap.add_argument("--cxx", help="Compilador C++20 (por defecto busca clang++).")
    ap.add_argument("--json", type=Path, help="Guarda el resultado estructurado en JSON.")
    args = ap.parse_args()
    if not args.self_check:
        ap.print_help()
        return 0

    try:
        cxx = find_cxx(args.cxx)
        rep = run_suite(args.root.resolve(), scripts, cxx)
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        ap.exit(1, f"FAIL: {exc}\n")

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(rep, indent=2, ensure_ascii=False), encoding="utf-8")

    for sc, info in rep["scenarios"].items():
        print(f"  C++ {sc}: {info['out'] or info['err']}")
    print(f"  UI  ui_nher4: {rep['ui']['out'] or rep['ui']['err']}")
    if not rep["ok"]:
        ap.exit(1, "FAIL: al menos un escenario NHER4 fallo\n")
    print("HEROE_VUELTAS_TEST OK (338 C++ + 38 UI checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
