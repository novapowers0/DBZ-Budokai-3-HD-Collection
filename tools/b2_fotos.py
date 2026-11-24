"""B2-P04: fotos 30/120/300 de una transicion de escenario B2 del port y tabla frente al original.

  python tools/b2_fotos.py llanura                # lanza el juego (copia opus2), saca las 3 fotos y compara
  python tools/b2_fotos.py llanura nieve ciudad   # varias escenas, una tras otra
  python tools/b2_fotos.py nieve --evidencia DIR  # no lanza nada: compara una carpeta de evidencia ya hecha

Foto atomica: el motor se detiene en el cuadro pedido, el arnes (b2_f4_final.foto) lee el
sidecar (camara + posiciones), captura la ventana y libera el evento; el juego tambien lo
libera solo a los 30 s. Original: tomas ya grabadas en E:/DBZ3HD_trabajo/B2_FASE2/transiciones/original.
Salida: 0 todo igual; 1 hay diferencias; 2 la transicion NO arranco (0 lineas "B2 HD": no se da por buena);
3 fallo del arnes. Resultado en <evidencia>/b2_fotos.json y tabla por pantalla.
"""
import argparse
import glob
import json
import math
import re
import subprocess
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
TOMLS = RAIZ / "docs/14_frentes/escenarios_pruebas/final"
ORIG = Path("E:/DBZ3HD_trabajo/B2_FASE2/transiciones/original")
ESCENAS = {"llanura": 4, "nieve": 7, "ciudad": 8, "kami": 2, "base": 0}  # nombre -> grupo
FOTOS = (30, 120, 300)
# ponytail: umbrales de la ficha B2-P06 (ojo +-1 %, duracion +-2 cuadros); suben o bajan aqui.
TOPE_OJO_PCT, TOPE_CUADROS = 1.0, 2.0


def original(grupo):
    """Toma original mas nueva del grupo con las 3 fotos y un evento medido."""
    for d in sorted(glob.glob(str(ORIG / f"original_g{grupo}_e0_*")), reverse=True):
        d = Path(d)
        if not all((d / f"cuadro_{n}.json").exists() for n in FOTOS):
            continue
        t = json.loads((d / "traza.json").read_text(encoding="utf-8"))
        ev = [r["reloj"] for r in t if r.get("evento")]
        if ev:
            return d, ev[-1] - ev[0]
    return None, None


def vec(v, w):
    return math.dist(v, w)


def velocidad_rival(traza, reloj):
    """B2-TRANS: rapidez de la victima original (u por cuadro de 60 Hz) entre la muestra de la foto y la
    siguiente. El original avanza de 1,2 en 1,2 y el port de 1 en 1: con la victima en su trayectoria
    (11-17 u/cuadro) medio cuadro de desfase ya son 5-8 u de 'rival err' sin que el juego haga nada mal."""
    esc = [r for r in traza if r.get("evento")]
    for a, b in zip(esc, esc[1:]):
        if abs(a["reloj"] - reloj) < 1e-3 and b["reloj"] > a["reloj"]:
            return vec(a["actores"][1]["pos"], b["actores"][1]["pos"]) / (b["reloj"] - a["reloj"])
    return None


def comparar(grupo, ev, dur_port=None):
    """ev: carpeta de evidencia del port. Devuelve dict con filas y veredicto."""
    log = (ev / "dbz3.log").read_text(encoding="utf-8", errors="replace") if (ev / "dbz3.log").exists() else ""
    r = {"evidencia": str(ev), "lineas_b2_hd": len(re.findall(r"dbz3 B2 HD:", log)), "filas": []}
    if r["lineas_b2_hd"] == 0 and dur_port is None:
        r["veredicto"] = "SIN TRANSICION: 0 lineas 'B2 HD' en el log; no se compara"
        return r
    b = re.search(r"dbz3 B2 HD: inicio bloqueado:.*", log)
    if b and "inicio por colision real" not in log:
        r["veredicto"] = "SIN TRANSICION: " + b[0]
        return r
    m = re.search(r"guion finalizado cuadro=([0-9.]+)", log)
    r["duracion_port"] = dur_port if dur_port is not None else float(m[1]) if m else None
    od, dur = original(grupo)
    r["original"] = str(od) if od else None
    r["duracion_original"] = dur
    if od is None:
        r["veredicto"] = "sin toma original de este grupo"
        return r
    ok = r["duracion_port"] is not None and abs(r["duracion_port"] - dur) <= TOPE_CUADROS
    traza = json.loads((od / "traza.json").read_text(encoding="utf-8"))
    for n in FOTOS:
        pf = ev / f"f4_cuadro_{n}.json"
        if not pf.exists():
            pf = ev / f"dbz3_b2_foto_{n}.json"  # fotos antiguas (B2_FASE2)
        if not pf.exists():
            r["filas"].append({"foto": n, "falta": True})
            ok = False
            continue
        p = json.loads(pf.read_text(encoding="utf-8"))
        o = json.loads((od / f"cuadro_{n}.json").read_text(encoding="utf-8"))
        c = o["escena_original"]["camaras"]["aplicada"]
        e_ojo = vec(p["ojo"], c["ojo"])
        pct = 100 * e_ojo / max(1e-9, math.hypot(*c["ojo"]))
        f = {"foto": n, "ojo_port": p["ojo"], "ojo_orig": c["ojo"], "ojo_err": e_ojo, "ojo_pct": pct,
             "obj_err": vec(p["objetivo"], c["objetivo"]), "fov_err": abs(p["fov"] - c["fov"]),
             "rival_err": vec(p["actores"][1]["pos"], o["actores"][1]["pos"]),
             "reloj_port": p["cuadro"], "reloj_orig": o["reloj"], "camara_hd": p.get("camara_hd", 0)}
        v = velocidad_rival(traza, o["reloj"])
        f["rival_vel"] = v
        f["rival_cuadros"] = f["rival_err"] / v if v and v > 0.05 else None  # error expresado en cuadros de desfase
        r["filas"].append(f)
        ok = ok and pct <= TOPE_OJO_PCT
    r["veredicto"] = "IGUAL" if ok else "DIFERENTE"
    return r


def tabla(nombre, r):
    print(f"\n== {nombre}: {r['veredicto']}")
    if "duracion_original" not in r or r["duracion_original"] is None:
        return
    print(f"  duracion cuadros: port {r['duracion_port']} / original {r['duracion_original']:.1f}")
    print("  foto | ojo err (%)       | objetivo err | fov err | rival err (vel orig u/c; en cuadros)")
    for f in r["filas"]:
        if f.get("falta"):
            print(f"  {f['foto']:>4} | FALTA")
            continue
        v, c = f.get("rival_vel"), f.get("rival_cuadros")
        extra = f" ({v:.2f}; {c:.2f})" if v is not None and c is not None else ""
        hd = " [camara HD]" if f.get("camara_hd") else ""
        print(f"  {f['foto']:>4} | {f['ojo_err']:8.2f} ({f['ojo_pct']:.2f}%) | {f['obj_err']:12.2f} | {f['fov_err']:.1e} | {f['rival_err']:.2f}{extra}{hd}")


def lanzar(esc, copia, forzar):
    toml = TOMLS / f"f4_b2_{esc}.toml"
    cmd = [sys.executable, str(RAIZ / "tools/pruebas.py"), "correr", copia, str(toml)] + (["--forzar"] if forzar else [])
    t0 = time.time()
    p = subprocess.run(cmd, cwd=RAIZ, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=1800)  # incluye la espera de turno de la copia
    out = p.stdout + p.stderr
    m = re.search(r"evidencia y resultados:\s*(.+)", out)
    print(f"[{esc}] {time.time()-t0:.0f} s; pruebas.py codigo {p.returncode}")
    return (Path(m[1].strip()) if m else None), out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("escenas", nargs="+", choices=sorted(ESCENAS))
    ap.add_argument("--copia", default="opus2")
    ap.add_argument("--evidencia", type=Path, help="comparar esta carpeta sin lanzar el juego (una sola escena)")
    ap.add_argument("--duracion-port", type=float, help="con --evidencia sin log: cuadros del port (p. ej. fotos antiguas)")
    ap.add_argument("--forzar", action="store_true", help="no reutilizar la cache PASS de pruebas.py")
    a = ap.parse_args()
    if a.evidencia and len(a.escenas) != 1:
        ap.error("--evidencia admite una sola escena")
    peor = 0
    for esc in a.escenas:
        ev = a.evidencia
        if ev is None:
            ev, out = lanzar(esc, a.copia, a.forzar)
            if ev is None or not ev.exists():
                print(f"[{esc}] el arnes no dio carpeta de evidencia:\n" + out[-800:])
                peor = max(peor, 3)
                continue
        r = comparar(ESCENAS[esc], ev, a.duracion_port)
        (ev / "b2_fotos.json").write_text(json.dumps(r, indent=2), encoding="utf-8")
        tabla(esc, r)
        peor = max(peor, 0 if r["veredicto"] == "IGUAL" else 2 if r["veredicto"].startswith("SIN TRANS") else 1)
    return peor


if __name__ == "__main__":
    sys.exit(main())
