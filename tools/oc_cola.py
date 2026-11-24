r"""oc_cola.py - lanza solo los encargos que se dejan en E:\DBZ3HD_trabajo\oc\pendientes\ (sprint del 12/11).

  python tools\oc_cola.py [--sol 3] [--ds 5] [--hasta 08:30]

Cada encargo es un .md cuya PRIMERA linea dice el modelo: "sol" o "ds" (solo esos dos: Sol 6.1 y DeepSeek de
pago; nada de modelos gratis). Respeta el tope de vivos por modelo, lanza con oc.py y mueve el .md a
pendientes\lanzados\. Para: a la hora --hasta o si existe pendientes\PARAR.
"""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oc  # noqa: E402

PEND = oc.OC / "pendientes"


def vivos_por_modelo():
    n = {}
    for e in oc._lanzados().values():
        if oc._vivo(e["pid"]):
            n[e.get("modelo")] = n.get(e.get("modelo"), 0) + 1
    return n


def una_vuelta(topes):
    (PEND / "lanzados").mkdir(parents=True, exist_ok=True)
    vivos = vivos_por_modelo()
    for md in sorted(PEND.glob("*.md"), key=lambda p: p.stat().st_mtime):
        modelo = md.read_text(encoding="utf-8", errors="replace").lstrip().split("\n", 1)[0].strip().lower()
        if modelo not in topes:
            print(f"oc_cola: {md.name}: primera linea '{modelo}' no es sol ni ds; se deja", flush=True)
            continue
        if vivos.get(modelo, 0) >= topes[modelo]:
            continue
        destino = PEND / "lanzados" / md.name
        md.replace(destino)
        try:
            oc.lanzar(destino, modelo, nombre=md.stem)
            # uno por vuelta: opencode guarda sus sesiones en una base SQLite comun y varios arranques a la vez
            # chocan ("Failed to execute statement", 12/11 01:29: murieron 7 de golpe)
            return True
        except SystemExit as e:  # nombre vivo o invalido: se devuelve para verlo
            print(f"oc_cola: {md.name}: {e}", flush=True)
            destino.replace(md)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sol", type=int, default=3)
    ap.add_argument("--ds", type=int, default=5)
    ap.add_argument("--hasta", default="08:30")
    a = ap.parse_args()
    PEND.mkdir(parents=True, exist_ok=True)
    topes = {"sol": a.sol, "ds": a.ds}
    print(f"oc_cola: vigilando {PEND} (sol {a.sol}, ds {a.ds}) hasta {a.hasta}", flush=True)
    while time.strftime("%H:%M") < a.hasta and not (PEND / "PARAR").exists():
        time.sleep(60 if una_vuelta(topes) else 20)   # 60 s entre arranques
    print("oc_cola: fin", flush=True)


if __name__ == "__main__":
    main()
