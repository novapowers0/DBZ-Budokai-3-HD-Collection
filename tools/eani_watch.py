"""eANI #63: quien escribe la posicion del atacante (actor+64/+72) durante y tras el Dragon Rush.
Reusa el depurador de A3b (watch_camara.py sin su escenario) y el flujo de tpend_pruebas.
Uso: DBZ3_AGENTE=eANI python tools/eani_watch.py correr smoke_f eaniwatch --par ids=0,7,0 --par entrada=r1b --par seg=40"""
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
src = (ROOT / "studies 5/ENTREGAS/A3b/herramientas/watch_camara.py").read_text(encoding="utf-8")
src = src[:src.index("def _esc(j, par):")]
ns = {"__name__": "watchlib"}
sys.path.insert(0, str(ROOT / "tools"))
exec(compile(src, "watch_camara", "exec"), ns)
import pruebas as P  # noqa: E402
import tpend_pruebas as T  # noqa: E402


def ejecutar(j, par):
    def vigia():
        t1 = time.monotonic()
        while time.monotonic() - t1 < 150:
            cb = T._mgr(j)
            if cb and T._cod0(cb) in (0x41F, 0x420, 0x421):
                mgr = int.from_bytes(P.leer(j.h, P.GBASE + 0x823D6CF4, 4), "big")
                G = ns["G"]
                a = G + mgr + 2096
                ns["vigilar"](j, j.proc.pid, [a + 64, a + 72], float(par.get("watch", "14")), 400)
                return
            time.sleep(0.02)
    th = threading.Thread(target=vigia)
    th.start()
    try:
        T.ejecutar(j, par)
    finally:
        th.join()


P.ESCENARIOS["eaniwatch"] = ("eANI: write-watch de la posicion del atacante en el Rush", T.entorno, ejecutar)
if __name__ == "__main__":
    sys.exit(P.main())
