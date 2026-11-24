"""Simulador offline de Survivors (NSUR, adaptado en AP_NSUR). No abre el juego."""
from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLANG = Path("C:/Program Files/LLVM/bin/clang++.exe")
PERFILES = (
    "todos", "bot_c10_medio_auto", "bot_c10_medio_noauto", "bot_c10_exp_auto",
    "bot_c10_exp_noauto", "mando_novato", "mando_medio", "mando_veterano",
    "dpad_medio", "mando_novato_auto", "mando_medio_auto", "mando_veterano_auto",
)


def compilar(raiz: Path, exe: Path, clang: str) -> None:
    src = raiz / "src/survivors"
    subprocess.run([
        clang, "-fuse-ld=lld", "-std=c++23", "-O2", "-DNOMINMAX",
        "-I", str(raiz), "-I", str(src), "-I", str(raiz / "tests"),
        "-isystem", str(raiz / "rexglue/include"),
        str(ROOT / "tools/survivors_sim_mando.cpp"),
        *(str(src / f) for f in ("juego.cpp", "sim.cpp", "tienda.cpp")), "-o", str(exe),
    ], cwd=raiz, check=True, timeout=180)


def entero(bajo: int, alto: int):
    def convertir(s: str) -> int:
        n = int(s)
        if not bajo <= n <= alto:
            raise argparse.ArgumentTypeError(f"debe estar entre {bajo} y {alto}")
        return n
    return convertir


def multiplicador(s: str) -> float:
    n = float(s)
    if not math.isfinite(n) or not 0.001 <= n <= 100:
        raise argparse.ArgumentTypeError("multiplicador fuera de 0.001..100")
    return n


def pares(s: str) -> list[tuple[float, float]]:
    try:
        resultado = [tuple(map(multiplicador, par.split(":"))) for par in s.split(",")]
        if len(resultado) > 32 or any(len(par) != 2 for par in resultado):
            raise ValueError
        return resultado
    except (ValueError, argparse.ArgumentTypeError):
        raise argparse.ArgumentTypeError("usa hasta 32 pares vida:dano, p.ej. 0.50:0.30,0.45:0.25") from None


def opciones(p, barrido=False):
    p.add_argument("--toml", default="res/survivors/survivors_v2.toml", type=Path,
                   help="catálogo de simulación (ruta relativa a --raiz o absoluta)")
    p.add_argument("--perfil", choices=PERFILES, default="mando_medio" if barrido else "todos")
    p.add_argument("--dif", choices=("0", "1", "2") if barrido else ("0", "1", "2", "todos"),
                   default="0" if barrido else "todos", help="0 normal, 1 difícil, 2 extremo (si figura en el catálogo)")
    p.add_argument("--reps", type=entero(1, 100), default=4, help="repeticiones por mapa y perfil (por defecto 4)")
    p.add_argument("--mejoras", type=entero(0, 5), default=0, help="nivel uniforme de mejoras permanentes")
    p.add_argument("--zoom", type=float, default=1.8, help="zoom del piloto sintético (1.0..2.2)")
    p.add_argument("--semilla", type=entero(0, 2**64 - 1 - 100_000), default=5000, help="semilla base de la matriz emparejada")
    p.add_argument("--solo-3pj", action="store_true", help="limitar al trío inicial")
    p.add_argument("--json", type=Path, help="JSON nuevo en una carpeta existente")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Survivors: simulación con pilotos sintéticos de XInput y cruceta (sin abrir el juego).")
    ap.add_argument("--raiz", type=Path, default=ROOT, help="raíz alternativa de fuentes y datos")
    ap.add_argument("--clang", default=str(CLANG) if CLANG.is_file() else shutil.which("clang++"),
                    help="ruta del compilador Clang C++ con lld disponible")
    sub = ap.add_subparsers(dest="cmd", required=True)
    eq = sub.add_parser("equilibrio", help="simula un catálogo y exporta resultados")
    opciones(eq)
    eq.add_argument("--csv", type=Path, help="CSV nuevo con una fila por partida")
    sw = sub.add_parser("barrido", help="compara multiplicadores con las mismas semillas")
    opciones(sw, barrido=True)
    sw.add_argument("--pares", type=pares, required=True, help="parejas vida:dano separadas por coma")
    sub.add_parser("autoprueba", help="comprueba zona muerta, latencia, repetibilidad y rechazo de perfiles inválidos")
    a = ap.parse_args(argv)
    raiz = a.raiz.resolve()
    if not a.clang or not (raiz / "src/survivors/juego.cpp").is_file():
        ap.error("falta Clang C++ o la raíz del proyecto")
    if a.cmd != "autoprueba":
        if not (raiz / a.toml).is_file() or not 1 <= a.zoom <= 2.2:
            ap.error("catálogo no encontrado o zoom fuera de 1.0..2.2")
        destinos = [d.resolve() for d in (a.json, getattr(a, "csv", None)) if d]
        if len(set(destinos)) != len(destinos) or any(d.exists() or not d.parent.is_dir() for d in destinos):
            ap.error("las salidas deben ser rutas nuevas, distintas y con padre existente")
    try:
        with tempfile.TemporaryDirectory(prefix="survivors_mando_") as tmp:
            exe = Path(tmp) / "sim_mando.exe"
            compilar(raiz, exe, a.clang)
            if a.cmd == "autoprueba":
                subprocess.run([exe, "--selftest"], cwd=raiz, check=True, timeout=60)
                r = subprocess.run([exe, "--perfil", "perfil_inexistente"], cwd=raiz,
                                   capture_output=True, timeout=60)
                if r.returncode != 2 or b"Perfil desconocido:" not in r.stderr:
                    raise ValueError("el simulador no rechazo correctamente el perfil inexistente")
                print("AUTOPRUEBA MANDO OK")
                return 0

            cmd = [exe, "--toml", a.toml, "--perfil", a.perfil, "--dif", a.dif, "--reps", str(a.reps),
                   "--mejoras", str(a.mejoras), "--zoom", str(a.zoom), "--semilla", str(a.semilla)]
            if a.solo_3pj:
                cmd += ["--solo-3pj"]
            if a.cmd == "equilibrio":
                for formato, ruta in (("--csv", a.csv), ("--json", a.json)):
                    if ruta:
                        cmd += [formato, ruta.resolve()]
                subprocess.run(cmd, cwd=raiz, check=True, timeout=300)
            else:
                resultados = []
                muestra = Path(tmp) / "muestra.json"
                for vida, dano in a.pares:
                    subprocess.run(cmd + ["--vida", str(vida), "--dano", str(dano), "--ovr-dif", a.dif,
                                          "--json", muestra], cwd=raiz, check=True, timeout=300)
                    resultados.append(json.loads(muestra.read_text(encoding="utf-8")))
                if a.json:
                    with a.json.open("x", encoding="utf-8") as f:
                        json.dump(resultados, f, ensure_ascii=False, indent=2)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        ap.exit(1, f"Error del simulador: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
