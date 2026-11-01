#!/usr/bin/env python3
r"""grabar_vitrina.py - clips de la vitrina (t16/t17) del video 1.5.6: un comando por clip.

Cada clip es un combate 1P contra un rival quieto (DBZ3_COMBATE_MUNECO=quieto) con la camara de vitrina
(DBZ3_TEST_VITRINA: orbita alrededor del luchador 1, sin marcador). Se graba con tools/grabar.py, de uno en uno
(una sola ventana a la vez). Sale en D:\DBZ3HD\video\clips\v156.

  python tools\grabar_vitrina.py --dry-run              (imprime los comandos, no graba)
  python tools\grabar_vitrina.py --solo t16_trunks_dbs  (un clip)
  python tools\grabar_vitrina.py                        (todos)

La copia (--copia, por defecto smoke_b) tiene que tener instalados los mods de traje (mods\<carpeta>\traje.toml): el
traje se pide por la CARPETA del mod (DBZ3_TEST_TRAJES) y el juego lo resuelve con el roster montado.
"""
import argparse
import ctypes
import ctypes.wintypes
import re
import shlex
import subprocess
import sys
from pathlib import Path

R = Path(__file__).resolve().parent.parent
BASE = Path(r"D:\DBZ3HD")
SALIDA = BASE / "video" / "clips" / "v156"
TMP = Path(r"D:\tmp\dsVID")
VITRINA = "14 22 8"          # segundos por vuelta, distancia, alto (DBZ3_TEST_VITRINA)
RIVAL, ESCENARIO = 7, 2      # Vegeta, escenario por defecto (fuera de plano: la camara mira al luchador 1)

# (mod o "" si es un personaje del juego, personaje [None = el de su traje.toml], forma, nombre del clip)
CLIPS = [
    ("traje_goku_gt_adulto", None, 0, "t16_goku_gt"),
    ("traje_goku_gt_adulto", None, 5, "t16_goku_gt_ssj4"),
    ("traje_androide17_eoz", None, 0, "t16_androide17_eoz"),
    ("traje_gohan_saiyaman_b1", None, 0, "t16_gohan_saiyaman"),
    ("traje_gohan_kai_supremo", None, 0, "t16_gohan_kai_supremo"),
    ("traje_trunks_dbs", None, 0, "t16_trunks_dbs"),
    ("traje_trunks_dbs", None, 2, "t16_trunks_dbs_rage"),
    ("traje_vegeta_namek", None, 0, "t16_vegeta_namek"),
    ("traje_vegeta_namek", None, 4, "t16_vegeta_namek_majin"),
    ("traje_tien_mistico_1", None, 0, "t16_tien_mistico_1"),
    ("traje_tien_mistico_2", None, 0, "t16_tien_mistico_2"),
    ("traje_roshi_outfit2", None, 0, "t16_roshi_outfit2"),
    ("traje_roshi_outfit2", None, 1, "t16_roshi_outfit2_full_power"),
    ("traje_roshi_jackie_chun", None, 0, "t16_roshi_jackie_chun"),
    ("", 14, 1, "t17_satan_high_tension"),      # Mr. Satan (14), forma 1 = High Tension
]


def _traje_personaje(copia, mod):
    """personaje de mods\\<mod>\\traje.toml: un id o la carpeta de un personaje de mod (Roshi)."""
    f = copia / "mods" / mod / "traje.toml"
    if not f.is_file():
        raise SystemExit(f"falta el mod {mod} en {copia / 'mods'} (instalalo con el launcher o copia la carpeta)")
    m = re.search(r'^\s*personaje\s*=\s*(.+?)\s*$', f.read_text(encoding="utf-8", errors="replace"), re.M)
    if not m:
        raise SystemExit(f"{f}: sin 'personaje ='")
    v = m.group(1).strip().strip('"\'')
    return int(v) if v.lstrip("-").isdigit() else v


def _id_roster(copia, carpeta):
    """id de la plaza de un personaje de mod (mods\\_roster\\roster.toml, "# <NOMBRE> (mod <carpeta>)")."""
    f = copia / "mods" / "_roster" / "roster.toml"
    if not f.is_file():
        return None
    lineas = f.read_text(encoding="utf-8", errors="replace").splitlines()
    for i, l in enumerate(lineas):
        if l.strip().startswith("#") and f"(mod {carpeta})" in l:
            for sig in lineas[i + 1:i + 6]:
                m = re.match(r"\s*id\s*=\s*(\d+)", sig)
                if m:
                    return int(m.group(1))
    return None


def _personaje(copia, mod, personaje):
    if personaje is not None:
        return personaje
    p = _traje_personaje(copia, mod)
    if isinstance(p, int):
        return p
    i = _id_roster(copia, p)
    if i is None:
        raise SystemExit(f"{mod}: el personaje {p} no esta en mods/_roster/roster.toml (abre el juego una vez o "
                         f"python \"mod center hd\\roster_build.py\" construir --mods {copia / 'mods'})")
    return i


def _escenario(copia, nombre, segundos):
    """Escenario minimo: espera el empiezo de la vitrina y deja el juego vivo hasta acabar el clip."""
    TMP.mkdir(parents=True, exist_ok=True)
    f = TMP / "vitrina.toml"
    f.write_text("# generado por tools/grabar_vitrina.py (temporal)\n"
                 f'descripcion = "Vitrina 1.5.6: {nombre}"\n'
                 'pasos = [\n'
                 '  \'log "dbz3 cine: vitrina empieza" 240\',\n'
                 f'  "esperar {int((segundos + 6) * 1000)}",\n'
                 ']\n', encoding="utf-8")
    return f


def _ventana_1080():
    """Tamano de ventana para grabar 1920x1080 reales: la ventana va en unidades logicas (125 % = x1,25)."""
    dpi = 96
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)   # por monitor: el DPI real del monitor, no el del sistema
        mon = ctypes.windll.user32.MonitorFromPoint(ctypes.wintypes.POINT(0, 0), 1)   # MONITOR_DEFAULTTOPRIMARY
        dx, dy = ctypes.c_uint(), ctypes.c_uint()
        ctypes.windll.shcore.GetDpiForMonitor(mon, 0, ctypes.byref(dx), ctypes.byref(dy))   # MDT_EFFECTIVE_DPI
        dpi = int(dx.value) or 96
    except (AttributeError, OSError):
        pass
    k = dpi / 96.0
    return f"{round(1920 / k)}x{round(1080 / k)}"


def _comando(copia, mod, personaje, forma, nombre, segundos):
    esc = _escenario(copia, nombre, segundos)
    traje = mod if mod else "0"
    cmd = [sys.executable, str(R / "tools" / "grabar.py"), copia.name, str(esc), nombre, str(segundos),
           "--desde-log", "dbz3 cine: vitrina empieza", "--retraso", "0.6",
           "--salida", str(SALIDA), "--par", "ventana=" + _ventana_1080(), "--escala", "1920x1080",
           "--env", f"DBZ3_TEST_BATTLE={personaje},{RIVAL},{ESCENARIO},0,0,0,{forma}",
           "--env", f"DBZ3_TEST_TRAJES={traje},0",
           "--env", "DBZ3_COMBATE_MUNECO=quieto",
           "--env", "DBZ3_HUB_MODE=5",
           "--env", f"DBZ3_TEST_VITRINA={VITRINA}"]
    return cmd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--copia", default="smoke_b")
    ap.add_argument("--solo", help="nombres separados por coma (por defecto, todos)")
    ap.add_argument("--segundos", type=int, default=14, help="duracion de cada clip (por defecto 14 = una vuelta)")
    ap.add_argument("--dry-run", action="store_true", help="solo imprime los comandos")
    a = ap.parse_args()
    copia = BASE / a.copia
    if not copia.is_dir():
        raise SystemExit(f"no existe la copia {copia}")
    solo = {s for s in (a.solo or "").split(",") if s}
    clips = [c for c in CLIPS if not solo or c[3] in solo]
    if not clips:
        raise SystemExit("--solo no casa con ningun clip: " + ", ".join(c[3] for c in CLIPS))
    for mod, personaje, forma, nombre in clips:
        try:
            pid = _personaje(copia, mod, personaje)
        except SystemExit as e:
            if not a.dry_run:
                raise
            print(f"# {nombre}: {e}", file=sys.stderr)
            pid = "<id>"
        cmd = _comando(copia, mod, pid, forma, nombre, a.segundos)
        print(shlex.join(cmd), flush=True)
        if not a.dry_run:
            r = subprocess.run(cmd, cwd=str(R))
            if r.returncode != 0:
                raise SystemExit(f"{nombre}: grabar.py termino con {r.returncode}")


if __name__ == "__main__":
    main()
