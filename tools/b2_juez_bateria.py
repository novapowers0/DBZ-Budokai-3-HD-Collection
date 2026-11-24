"""Bateria del juez B2: corre tools/b2_juez.py sobre parejas A/B y resume OK / FAIL / [H].

A = linea base de R20 (por defecto `E:\\DBZ3HD_trabajo\\respaldos_r20\\dsJ0_JUEZ`).
B = volcados de hoy (`DBZ3_TEST_B2_STATE=<ruta.jsonl>`); por defecto se buscan, el mas
reciente primero, bajo `D:\\DBZ3HD\\_pruebas` o la carpeta que se indique con `--b`.

Codigos del juez y la bateria: 0 = OK, 1 = FAIL, 2 = PENDIENTE_DATO ([H], no PASS).

Uso:
    python tools\\b2_juez_bateria.py
    python tools\\b2_juez_bateria.py --a "E:\\...\\dsJ0_JUEZ" --b "D:\\DBZ3HD\\_pruebas"
    python tools\\b2_juez_bateria.py --b-fase 1="D:\\...\\actual_fase1_S1.jsonl"

Nunca modifica los volcados: solo lee y escribe los JSON de resultado bajo --salida.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[1]
JUEZ = RAIZ / "tools" / "b2_juez.py"
A_DEF = Path(r"E:\DBZ3HD_trabajo\respaldos_r20\dsJ0_JUEZ")
B_DEF = Path(r"D:\DBZ3HD\_pruebas")
SALIDA_DEF = Path(r"E:\DBZ3HD_trabajo\respaldos_r21\dsB2_B\juez")

# Nombres de la linea base en R20 (se prueban en orden) y de los volcados de hoy.
NOMBRES_A = ("baseline_estado_fase%d.jsonl", "b2_estado_f%d.jsonl", "actual_fase%d_S1.jsonl")
PATRON_B = r"(?:fase|_f)[ _-]?0?%d(?:\D|$)"


def volcado_a(a_dir, fase):
    for patron in NOMBRES_A:
        ruta = a_dir / (patron % fase)
        if ruta.is_file():
            return ruta
    return None


def volcado_b(b_dir, fase, fijos):
    if fase in fijos:
        ruta = Path(fijos[fase])
        return ruta if ruta.is_file() else None
    # Los .jsonl de estado de B2 se llaman p.ej. `actual_fase4_S4.jsonl` o `b2_fase4_state.jsonl`.
    patron = re.compile(PATRON_B % fase, re.IGNORECASE)
    candidatos = [p for p in b_dir.rglob("*.jsonl") if patron.search(p.name)]
    if not candidatos:
        return None
    return max(candidatos, key=lambda p: p.stat().st_mtime)


def main(argv):
    ap = argparse.ArgumentParser(description="Resumen del juez B2 sobre las 9 fases (0 OK / 1 FAIL / 2 PENDIENTE_DATO).")
    ap.add_argument("--a", default=str(A_DEF), help="carpeta de la linea base (R20)")
    ap.add_argument("--b", default=str(B_DEF), help="carpeta donde buscar los volcados de hoy")
    ap.add_argument("--salida", default=str(SALIDA_DEF), help="carpeta de los JSON de resultado")
    ap.add_argument("--b-fase", action="append", default=[], metavar="N=RUTA",
                    help="fuerza el volcado B de una fase (repetible)")
    a_par = ap.parse_args(argv)
    a_dir, b_dir, salida = Path(a_par.a), Path(a_par.b), Path(a_par.salida)
    fijos = {}
    for item in a_par.b_fase:
        if "=" not in item:
            print("--b-fase espera N=RUTA; se ignora: " + item)
            continue
        n, r = item.split("=", 1)
        fijos[int(n)] = r
    salida.mkdir(parents=True, exist_ok=True)

    # La autoprueba va primero: si el juez no distingue FAIL de PENDIENTE_DATO, no se mide nada.
    auto = subprocess.run([sys.executable, str(JUEZ), "--autoprueba"], capture_output=True, text=True)
    print((auto.stdout or "").strip())
    if auto.returncode != 0:
        print("AUTOPRUEBA del juez en rojo: se detiene la bateria")
        return 1

    ok = fallos = pend = 0
    for fase in range(1, 10):
        a = volcado_a(a_dir, fase)
        b = volcado_b(b_dir, fase, fijos) if b_dir.is_dir() or fase in fijos else None
        if a is None or b is None:
            if a is None and b is None:
                print(f"[H] fase {fase}: faltan los volcados A y B (PENDIENTE_DATO, no PASS)")
            else:
                falta = "A" if a is None else "B"
                print(f"[H] fase {fase}: falta el volcado {falta} (PENDIENTE_DATO, no PASS)")
            pend += 1
            continue
        r = subprocess.run([sys.executable, str(JUEZ), str(a), str(b),
                            "--salida", str(salida / ("juez_fase%d.json" % fase))],
                           capture_output=True, text=True)
        linea = [l for l in (r.stdout or "").splitlines() if l.startswith("B2:") or " ok," in l]
        print("fase %d: codigo %d (0 OK / 1 FAIL / 2 PENDIENTE_DATO) <- %s" % (fase, r.returncode, b.name))
        for l in (linea or (r.stdout or "").splitlines())[-3:]:
            print("  " + l.strip())
        if r.returncode == 0:
            ok += 1
        elif r.returncode == 1:
            fallos += 1
        else:
            pend += 1
    print("RESUMEN: %d fases OK, %d con FAIL, %d PENDIENTE_DATO ([H], no PASS)" % (ok, fallos, pend))
    return 1 if fallos else 2 if pend else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
