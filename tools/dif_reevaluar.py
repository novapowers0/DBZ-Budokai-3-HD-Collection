r"""dif_reevaluar.py (P2, 7/11) - vuelve a puntuar la camara de los minijuegos IW con la metrica actual SIN lanzar el juego.

  python tools\dif_reevaluar.py [--solo 0x7D5,0x7DA]

Para cada escena IW toma la ultima salida de barrido_dif (E:\DBZ3HD_trabajo\noche_0711\N4\iw_<clave>.txt), saca la carpeta de
evidencia de la corrida del port ("evidencia y resultados: ...") y repite `comparar_modo.py iw <clave> --log <dbz3.log>` con el dorado
actual. Copia exe/region/copia del resultado anterior al nuevo. No toca el juego ni PCSX2."""
import json
import re
import subprocess
import sys
from pathlib import Path

AQUI = Path(__file__).resolve().parent
RES = AQUI / "dorados" / "resultados"
TMP = Path(r"E:\DBZ3HD_trabajo\noche_0711\N4")


def main(a):
    solo = a[a.index("--solo") + 1].split(",") if "--solo" in a else None
    for k in range(0x7D0, 0x7E2):
        c = f"0x{k:X}"
        if solo and c not in solo:
            continue
        txt = TMP / f"iw_{c}.txt"
        if not txt.exists():
            print(c, "sin salida del barrido")
            continue
        m = re.search(r"evidencia y resultados: (\S.*?)\s*$", txt.read_text(encoding="utf-8", errors="replace"), re.M)
        log = Path(m.group(1)) / "dbz3.log" if m else None
        if not log or not log.exists():
            print(c, "sin dbz3.log del port")
            continue
        antes = sorted(RES.glob(f"iw_{c}_*.json"), key=lambda p: p.stat().st_mtime)
        previo = json.loads(antes[-1].read_text(encoding="utf-8")) if antes else {}
        r = subprocess.run([sys.executable, str(AQUI / "comparar_modo.py"), "iw", c, "--log", str(log)], capture_output=True, text=True)
        nuevos = sorted(RES.glob(f"iw_{c}_*.json"), key=lambda p: p.stat().st_mtime)
        if not nuevos or (antes and nuevos[-1] == antes[-1]):
            print(c, "comparar_modo no escribio resultado:", r.stdout[-200:], r.stderr[-200:])
            continue
        d = json.loads(nuevos[-1].read_text(encoding="utf-8"))
        for k2 in ("exe", "region", "copia"):
            if k2 in previo:
                d[k2] = previo[k2]
        d["log_port"] = str(log)
        nuevos[-1].write_text(json.dumps(d, indent=1, ensure_ascii=False), encoding="utf-8")
        print(c, "->", nuevos[-1].name)


if __name__ == "__main__":
    main(sys.argv[1:])
