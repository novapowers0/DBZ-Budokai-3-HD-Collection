r"""dif_usa_diag.py (P2, 7/11) - por que un estado IW USA no entra en el minijuego: lanza el estado en una plaza del pool y apunta cada
pocos segundos contador de cuadros, clave de mision y los punteros raiz (gestor, camara de campo, ui, contexto) con las direcciones USA.

  python tools\dif_usa_diag.py <clave 0x7NN> [--pool 3] [--seg 150] [--eu]

Escribe una linea por muestra en la salida; cierra solo su pid y libera la plaza. --eu usa la ISO/estado EU (control)."""
import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI))
import dif_grabar_pool as G  # noqa: E402

RAICES = {"mgr": 0x49CA70, "cam_campo": 0x49CBBC, "cam": 0x49CD6C, "ui": 0x49CD8C, "ctx": 0x49CBA4, "vuelo": 0x49CE3C}


def main(a):
    ap = argparse.ArgumentParser()
    ap.add_argument("clave")
    ap.add_argument("--pool", type=int, default=3)
    ap.add_argument("--seg", type=float, default=150)
    ap.add_argument("--eu", action="store_true")
    o = ap.parse_args(a)
    import dif_mapa_iw_usa as M
    usa = not o.eu
    est = G.estado_por_defecto(o.clave, usa)
    iso = G.ISO_USA if usa else G.ISO_EU
    if not est:
        print("sin savestate", o.clave)
        return 2
    pool = [sys.executable, str(AQUI / "pcsx2_pool.py")]
    subprocess.run(pool + ["preparar", str(o.pool), "--estado", str(est)], check=True, timeout=120)
    subprocess.run(pool + ["lanzar", str(o.pool), "--iso", iso, "--estado", str(est), "--ejecutar"], check=True, timeout=120)
    pid = G.pid_del_pool(o.pool)
    try:
        import pcsx2_pool as P
        import pcsx2_sonda as S
        P.registrar_en_sonda(S, o.pool)
        d = (lambda x: M.usa_datos(x)) if usa else (lambda x: x)
        cont, clave = (M.COUNT, M.CLAVE) if usa else (0x4A4D40, 0x5C0DC8)
        p, t0 = None, time.time()
        while time.time() - t0 < 300 and p is None:
            try:
                p = S.Pine()
                p.lote([(cont, 4)])
            except Exception:
                p = None
                time.sleep(4)
        if p is None:
            print("PINE no contesta")
            return 3
        p.s.settimeout(30)
        t0, prev = time.time(), None
        while time.time() - t0 < o.seg:
            dirs = [(k, d(x)) for k, x in RAICES.items()]
            if usa:                                  # control: la misma raiz con el delta fijo -0x800 si la tabla da otro
                dirs += [(k + "_m800", x - 0x800) for k, x in RAICES.items() if d(x) != x - 0x800]
            v = p.lote([(cont, 4), (clave, 4)] + [(a, 4) for _, a in dirs])
            fila = {"t": round(time.time() - t0), "cuadro": v[0], "clave": hex(v[1])}
            fila.update({k: hex(x) for (k, _), x in zip(dirs, v[2:])})
            if (fila["clave"], fila["mgr"], fila["cam_campo"], fila["cam"], fila["ui"], fila["vuelo"], fila.get("cam_campo_m800")) != prev or fila["t"] % 60 < 3:
                print(fila, flush=True)
                prev = (fila["clave"], fila["mgr"], fila["cam_campo"], fila["cam"], fila["ui"], fila["vuelo"], fila.get("cam_campo_m800"))
            time.sleep(3)
        print("fin: ultimo cuadro", v[0])
        return 0
    finally:
        if pid:
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
            time.sleep(2)
        subprocess.run(pool + ["liberar", str(o.pool)], capture_output=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
