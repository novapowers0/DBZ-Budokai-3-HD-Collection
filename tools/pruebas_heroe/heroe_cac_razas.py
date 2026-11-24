"""R23 CAC: una ficha por raza del creador (6) y comprobacion por registro de que el Heroe apunta a lo elegido.

Por cada raza: Habitacion > Aspecto (foto) > Evolucion (foto). El registro debe decir:
  - "creacion forzada (raza R, cuerpo C ..." con la ficha pedida;
  - "Evolucion abierta (raza R, N formas) ficha raza R cuerpo ID": la pestana es la raza de la ficha y el cuerpo, el suyo;
  - una linea "Evolucion: forma ..." por forma de SU raza (y ninguna de otra), con "combate" 1/0/-1.
Uso:  python tools/pruebas_heroe/heroe_cac_razas.py [--copia smoke_b] [--solo 3,4] [--forzar]
"""
import argparse
import re
import subprocess
import sys
import tomllib
from pathlib import Path

R = Path(__file__).resolve().parents[2]
PRUEBAS = Path(r"D:\DBZ3HD\_pruebas")
TMP = Path(r"E:\DBZ3HD_trabajo\cac_coherencia\guiones")
# raza (kRazas de heroe.cpp) -> (nombre, indice de cuerpo, ID del cuerpo, indice de estilo con el mismo ID: formas nativas)
RAZAS = {0: ("saiyan", 0, 0, 0), 1: ("humano", 0, 10, 3), 2: ("namekiano", 0, 11, 2),
         3: ("freezer", 0, 27, 4), 4: ("majin", 0, 34, 16), 5: ("androide", 3, 33, 5)}
PASOS = '''pasos = [
  'log "Habitacion abierta" 300',
  "esperar 5000",
  "tecla Space 1500",
  "esperar 6000",
  "captura aspecto",
  "tecla Backspace 1500",
  'log "Habitacion abierta" 20',
  "esperar 2500",
  "tecla S 700",
  "tecla Space 1500",
  'log "Evolucion abierta" 30',
  "esperar 4000",
  "captura evolucion",
  "tecla D 900",
  "esperar 1500",
  "captura evolucion_nodo",
  "tecla Backspace 1500",
  'log "Evolucion: atras" 20',
]
'''


def formas_de(raza):
    t = tomllib.loads((R / "res" / "heroe" / "desbloqueos.toml").read_text(encoding="utf-8"))
    return [d["id"] for d in t["desbloqueo"] if d.get("tipo") == "forma" and d.get("raza") == raza]


def guion(raza):
    nombre, cuerpo, _, estilo = RAZAS[raza]
    env = ["DBZ3_TEST_MODO=heroe", "DBZ3_HUB_MODE=5", "DBZ3_TEST_HEROE_PANTALLA=habitacion",
           f"DBZ3_TEST_HEROE_RAZA={raza}", f"DBZ3_TEST_HEROE_CUERPO={cuerpo}", "DBZ3_TEST_HEROE_TRAJE=0",
           f"DBZ3_TEST_HEROE_ESTILO={estilo}", "DBZ3_TEST_HEROE_FORMAS=" + ",".join(formas_de(raza)),
           "DBZ3_TEST_HEROE_MARCAS=camino_ciudad,camino_lab,camino_maldad,camino_nave,camino_puro,camino_vegeta,maestro_freezer,maestro_goku,maestro_krilin,maestro_piccolo,opc_namek,villanos_revividos",
           "DBZ3_TEST_CFG=dbz3_heroe_ui_nueva=true;dbz3_heroe_habitacion=true;dbz3_heroe_evolucion=true",
           "REX_DBZ3_LANGUAGE=5", "REX_DBZ3_UI_LANGUAGE=5"]
    TMP.mkdir(parents=True, exist_ok=True)
    p = TMP / f"cac_raza_{raza}_{nombre}.toml"
    p.write_text(f'descripcion = "R23 CAC: ficha {nombre} (raza {raza}) y Evolucion"\nenv = [\n'
                 + ",\n".join(f'  "{e}"' for e in env) + "]\n" + PASOS, encoding="utf-8")
    return p


def comprobar(raza, log):
    nombre, _, cid, _ = RAZAS[raza]
    fallos = []
    m = re.search(r"creacion forzada \(raza (\d+), cuerpo (\d+)", log)
    if not m or int(m.group(1)) != raza:
        fallos.append(f"ficha no aplicada ({m.group(0) if m else 'sin linea'})")
    m = re.search(r"Evolucion abierta \(raza (\d+), (\d+) formas\) ficha raza (\d+) cuerpo (\d+) traje (\d+) id propio (-?\d+)", log)
    if not m:
        return ["sin la linea 'Evolucion abierta' con la ficha"], {}
    pest, n, fr, fc, _, idp = map(int, m.groups())
    if pest != raza:
        fallos.append(f"Evolucion abre en la pestana {pest}, la ficha es raza {raza}")
    if fr != raza or fc != cid:
        fallos.append(f"ficha en juego raza {fr} cuerpo {fc}, se pidio raza {raza} cuerpo {cid}")
    formas = {f: (int(e), int(c)) for f, e, c in
              re.findall(r"Evolucion: forma (\S+) estado (\d) capsula \d+ combate (-?\d)", log)}
    esperadas = set(formas_de(raza))
    if set(formas) != esperadas:
        fallos.append(f"formas listadas {sorted(formas)} != las de la raza {sorted(esperadas)}")
    cerradas = [f for f, (e, _) in formas.items() if e != 0]
    if cerradas:
        fallos.append(f"formas dadas y no abiertas: {cerradas}")
    return fallos, dict(formas=formas, id_propio=idp, formas_n=n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--copia", default="smoke_b")
    ap.add_argument("--solo", default="0,1,2,3,4,5")
    ap.add_argument("--forzar", action="store_true")
    a = ap.parse_args()
    total = 0
    for raza in [int(x) for x in a.solo.split(",")]:
        p = guion(raza)
        cmd = [sys.executable, str(R / "tools" / "pruebas.py"), "correr", a.copia, str(p)] + (["--forzar"] if a.forzar else [])
        r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        corridas = sorted(PRUEBAS.glob(f"*_{a.copia}_{p.stem}"), key=lambda d: d.stat().st_mtime)
        log = (corridas[-1] / "dbz3.log").read_text(encoding="utf-8", errors="replace") if corridas else ""
        fallos, datos = comprobar(raza, log)
        if r.returncode != 0:
            fallos.insert(0, f"pruebas.py codigo {r.returncode}: {(r.stdout + r.stderr)[-200:].strip()}")
        estado = "PASS" if not fallos else "FAIL"
        total += bool(fallos)
        print(f"{estado} raza {raza} {RAZAS[raza][0]:<10} {corridas[-1].name if corridas else '-'}")
        for f, (e, c) in sorted(datos.get("formas", {}).items()):
            print(f"     {f:<18} combate {c:>2}")
        for f in fallos:
            print("     -", f)
    sys.exit(1 if total else 0)


if __name__ == "__main__":
    main()
