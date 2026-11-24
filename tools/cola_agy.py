r"""cola_agy.py - run Antigravity (Gemini) briefs N at a time, respecting the subscription quota.

  python tools\cola_agy.py --modelo agy-gemini-4-neon-high --a-la-vez 2 NOMBRE=ENCARGO.md [...]

Each brief is launched with tools/oc.py.
- When one finishes, the next one starts.
- If the log says "Resets in XhYm" (a 429 quota error), the queue sleeps until then, plus 2 minutes.
- It then relaunches the same brief under the name suffixed with r1, r2...
ponytail: sequential polling every 30 s; no daemon.
"""
import argparse, json, re, subprocess, sys, time
from pathlib import Path

R = Path(__file__).resolve().parents[1]
OC = Path(r"E:\DBZ3HD_trabajo\oc")
RESET = re.compile(r"Resets in (?:(\d+)h)?(?:(\d+)m)?(?:(\d+)s)?")


def segundos_reset(log):
    m = None
    for m in RESET.finditer(Path(log).read_text(encoding="utf-8", errors="replace")[-4000:]):
        pass
    if not m:
        return 0
    h, mi, s = (int(x or 0) for x in m.groups())
    return h * 3600 + mi * 60 + s


def vivo(pid):
    out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, text=True).stdout
    return str(pid) in out


def lanzar(nombre, encargo, modelo):
    out = subprocess.run([sys.executable, str(R / "tools" / "oc.py"), "lanzar", encargo, "--modelo", modelo,
                          "--nombre", nombre], cwd=R, capture_output=True, text=True).stdout
    print(time.strftime("%H:%M"), out.strip(), flush=True)
    return int(re.search(r"pid (\d+)", out).group(1)), OC / f"oc_{nombre}.log"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", default="agy-gemini-4-neon-high")
    ap.add_argument("--a-la-vez", type=int, default=2)
    ap.add_argument("--esperar", type=int, default=0, help="segundos antes de empezar")
    ap.add_argument("trabajos", nargs="+")
    a = ap.parse_args()
    cola = [t.split("=", 1) for t in a.trabajos]
    intentos = {}
    time.sleep(a.esperar)
    activos = []   # (nombre_base, encargo, pid, log)
    while cola or activos:
        while cola and len(activos) < a.a_la_vez:
            base, enc = cola.pop(0)
            n = intentos.get(base, 0)
            pid, log = lanzar(base + (f"r{n}" if n else ""), enc, a.modelo)
            activos.append((base, enc, pid, log))
        time.sleep(30)
        for t in list(activos):
            base, enc, pid, log = t
            if vivo(pid):
                continue
            activos.remove(t)
            espera = segundos_reset(log) if log.exists() else 0
            if espera:
                intentos[base] = intentos.get(base, 0) + 1
                if intentos[base] > 6:
                    print(f"{base}: demasiados reintentos, se deja", flush=True)
                    continue
                cola.insert(0, (base, enc))
                print(f"{time.strftime('%H:%M')} {base}: cuota agotada, espero {espera // 60} min", flush=True)
                time.sleep(espera + 120)
            else:
                print(f"{time.strftime('%H:%M')} {base}: terminado", flush=True)


if __name__ == "__main__":
    main()
