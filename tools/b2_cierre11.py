"""Genera y juzga una prueba dirigida del cierre 11 de B2 sin lanzar el juego."""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tomllib
from pathlib import Path

import b2_escenario

AMP_PAL = (91001051, 3767576663)
ALFA = "dbz3_b2_shenron_luz_alfa_pal"


def guion(ps2_games: Path, alfa_pal: bool) -> dict:
    d = b2_escenario.guion("cierre11", 9, ps2_games)
    d["descripcion"] = "AP_NB2G: salto dirigido a cierre 11, aparición de Shenron a x1"
    d["env"] = [f"{e};{ALFA}={'true' if alfa_pal else 'false'}"
                if e.startswith("DBZ3_TEST_CFG=") else e for e in d["env"]]
    d["pasos"] = [
        'log "tablero b2: fase 9 leida" 90',
        'log "tablero b2: CIERRES escena 11" 45',
        'captura 01_cierre11',
        'log "tablero b2: Shenron: [0-9]+ deseos" 45',
        'log "PUL-B2: Shenron sonido en el cuadro [0-9]+" 10',
        'esperar 4500',
        'captura 02_bolas',
        'log "PUL-B2: Shenron UV 0xF0 en la luz" 15',
        'esperar 1800',
        'captura 03_columna_luz',
        'esperar 9000',
        'captura 04_shenron',
        'esperar 3500',
        'captura 05_cielo_nocturno',
        'esperar 5500',
        'captura 06_tras_aparicion',
    ]
    return d


def juzgar(txt: str, exigir_alfa_pal: bool = False) -> dict:
    """Lee hitos en orden; un salto dirigido no cuenta como ejecución del script 303."""
    cierres = list(re.finditer(r"tablero b2: CIERRES escena 11\b(?! ->)", txt))
    cierre = cierres[-1] if cierres else None
    antes = txt[:cierre.start()] if cierre else txt
    despues = txt[cierre.end():] if cierre else ""
    hitos = {"fase_9": bool(re.search(r"tablero b2: fase 9 leida\b", antes)),
             "script_303_orden_42": bool(re.search(r"tablero b2: script 303: orden 42\b", antes)),
             "cierre_11": cierre is not None, "alfa_pal_texturas": None}
    fallos, pendientes = [], []
    for nombre in ("fase_9", "cierre_11"):
        if not hitos[nombre]:
            pendientes.append(nombre)
    for nombre, patron in (
        ("transicion_10", r"tablero b2: CIERRES escena 11 -> escena 10 dato 1\b"),
        ("deseos", r"tablero b2: Shenron: \d+ deseos\b"),
        ("amp", r"AMP firma estados: bolas (\d+) luz (\d+)"),
        ("shenron_3d", r"tablero b2: Shenron 3D: \d+ triangulos\b"),
        ("sonido_cuadro", r"PUL-B2: Shenron sonido en el cuadro (\d+)"),
        ("luz_uv", r"PUL-B2: Shenron UV 0xF0 en la luz\b"),
    ):
        m = re.search(patron, despues)
        if not m:
            pendientes.append(nombre)
        if nombre == "amp":
            # El alfa se registra durante la carga, entre «deseos» y la firma AMP.
            if m and hitos.get("deseos"):
                alfa = re.search(r"tablero b2: Shenron alfa PAL de la luz: (\d+) texturas", despues[:m.start()])
                if alfa:
                    hitos["alfa_pal_texturas"] = int(alfa[1])
            hitos[nombre] = list(map(int, m.groups())) if m else None
            if m and tuple(hitos[nombre]) != AMP_PAL:
                fallos.append("firma AMP distinta de PAL")
        elif nombre == "sonido_cuadro":
            hitos[nombre] = int(m[1]) if m else None
            if m and not 1 <= hitos[nombre] < 100:
                fallos.append("sonido fuera del arranque de la aparición (salto o retardo)")
        else:
            hitos[nombre] = bool(m)
        if m:
            despues = despues[m.end():]
    if exigir_alfa_pal and hitos.get("deseos"):
        if hitos.get("alfa_pal_texturas") is None:
            pendientes.append("alfa_pal_texturas")
        elif hitos["alfa_pal_texturas"] != 5:
            fallos.append("se esperaban 5 texturas en la luz con alfa PAL")
    rc = 1 if fallos else 2 if pendientes else 0
    return dict(rc=rc, hitos=hitos, fallos=fallos, pendientes=pendientes)


def self_check() -> None:
    for activar in (False, True):
        d = guion(Path("D:/Ruta de prueba/PS2"), activar)
        if tomllib.loads(b2_escenario.texto(d)) != d or d["turbo"]:
            raise ValueError("el guion no se puede reproducir a x1")
    log = """tablero b2: fase 9 leida
tablero b2: CIERRES escena 11 (orden 42): recursos de b2_cierres
tablero b2: CIERRES escena 11 -> escena 10 dato 1
tablero b2: Shenron: 40 deseos
tablero b2: Shenron alfa PAL de la luz: 5 texturas
AMP firma estados: bolas 91001051 luz 3767576663
tablero b2: Shenron 3D: 11325 triangulos
PUL-B2: Shenron sonido en el cuadro 1
PUL-B2: Shenron UV 0xF0 en la luz
"""
    ok = juzgar(log, True)
    if ok["rc"] != 0 or ok["hitos"]["script_303_orden_42"]:
        raise ValueError("el juez confunde salto directo y script 303")
    if juzgar(log.replace("cuadro 1", "cuadro 1513"), True)["rc"] != 1:
        raise ValueError("el juez no detecta el salto de la aparición")
    antes_uv, uv = log.split("PUL-B2: Shenron UV")
    if juzgar("PUL-B2: Shenron UV" + uv + antes_uv, True)["rc"] != 2:
        raise ValueError("el juez acepta un hito fuera de secuencia")
    if juzgar(log.replace("5 texturas", "0 texturas"), True)["rc"] != 1:
        raise ValueError("el juez acepta una carga de luz vacía")


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                epilog="Salida: 0 confirmado; 1 divergencia; 2 faltan hitos; 3 error de lectura/escritura.")
    p.add_argument("--self-check", action="store_true", help="comprueba el generador y el juez con controles locales")
    sub = p.add_subparsers(dest="cmd")
    r = sub.add_parser("ruta", help="genera un TOML nuevo para el salto dirigido")
    r.add_argument("--ps2-games", type=Path, default=Path(os.getenv("DBZ3_PS2_GAMES", "D:/DBZ3HD/ps2_games")))
    r.add_argument("--salida", type=Path, required=True)
    r.add_argument("--alfa-pal", action="store_true", help="activar la transparencia PS2 de la columna")
    r.add_argument("--preflight", action="store_true", help="validar el guion con pruebas.py sin ejecutar el juego")
    j = sub.add_parser("juzgar", help="comprueba los hitos de un dbz3.log de Budokai 2 PAL")
    j.add_argument("--log", type=Path, required=True)
    j.add_argument("--exigir-alfa-pal", action="store_true", help="exigir la carga de las cinco texturas con alfa PS2")
    a = p.parse_args(argv)
    try:
        if a.self_check:
            self_check()
            print("AP_NB2G: autoprueba OK")
        elif a.cmd == "ruta":
            if a.salida.suffix.lower() != ".toml" or any(ord(c) < 32 or ord(c) == 127 for c in str(a.ps2_games)):
                p.error("--salida debe ser .toml y --ps2-games no admite caracteres de control")
            txt = b2_escenario.texto(guion(a.ps2_games, a.alfa_pal))
            with a.salida.open("x", encoding="utf-8", newline="\n") as f:
                f.write(txt)
            print(a.salida, flush=True)
            if a.preflight:
                tools = Path(__file__).resolve().parent
                return subprocess.run([sys.executable, "-B", str(tools / "pruebas.py"), "preflight",
                                       str(a.salida.resolve())], cwd=tools.parent, timeout=90).returncode
        elif a.cmd == "juzgar":
            v = juzgar(a.log.read_text(encoding="utf-8-sig", errors="replace"), a.exigir_alfa_pal)
            print(json.dumps(v, ensure_ascii=False, indent=2))
            return v["rc"]
        else:
            p.print_help()
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(exc, file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
