#!/usr/bin/env python3
"""Prueba offline del historial NCAC2 contra el C++ integrado, sin abrir el juego."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-check", action="store_true", help="Compila y ejecuta el arnés de regresión (C++20).")
    parser.add_argument("--cxx", help="Compilador compatible con g++/Clang; por defecto busca en PATH.")
    args = parser.parse_args()
    if not args.self_check:
        parser.print_help()
        return 0
    compiler = args.cxx or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        parser.error("No se encontró g++ ni clang++ en PATH")
    scripts = Path(__file__).resolve().parent
    root = scripts.parents[1]
    try:
        with tempfile.TemporaryDirectory(prefix="dbz3_historial_cac_") as tmp:
            exe = Path(tmp) / "test_historial_creador.exe"
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                            str(scripts / "test_historial_creador.cpp"), "-o", str(exe)], check=True, timeout=90)
            subprocess.run([str(exe)], check=True, timeout=15)
    except (OSError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"Prueba del historial fallida: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
