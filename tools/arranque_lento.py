"""POST3/ARRANQUE: mide el arranque del juego en los registros de las pruebas (sin lanzar nada).
python tools/arranque_lento.py [carpeta de _pruebas] [--desde AAAAMMDD]
Por cada dbz3.log: crear D3D12 (DXGI adapter -> device features), presentacion (-> quick settings ready), lanzar el modulo
(OnPreLaunchModule -> OnPostLaunchModule: incluye el SetupContext del hilo de GPU y el almacen de shaders) y cuantos otros
juegos ARRANCABAN a la vez (+-40 s). Tabla de los lentos y resumen."""
import collections
import datetime
import glob
import os
import re
import sys

RX = re.compile(r"\[(\d+-\d+-\d+ \d+:\d+:\d+\.\d+)\]")


def T(s):
    return datetime.datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f")


def leer(p):
    t = {}
    with open(p, encoding="utf-8", errors="replace") as fh:
        for i, l in enumerate(fh):
            if i > 900:
                break
            m = RX.match(l)
            if not m:
                continue
            for k, txt in (("ini", "phase 'OnPreSetup'"), ("adp", "DXGI adapter:"), ("dev", "Direct3D 12 device and OS features"),
                           ("qs", "quick settings menu ready"), ("pre", "phase 'OnPreLaunchModule'"), ("post", "phase 'OnPostLaunchModule'")):
                if k not in t and txt in l:
                    t[k] = T(m.group(1))
    return t


def main():
    desde = sys.argv[sys.argv.index("--desde") + 1] if "--desde" in sys.argv else ""
    arg = [a for a in sys.argv[1:] if not a.startswith("--") and a != desde]
    raiz = arg[0] if arg else "D:/DBZ3HD/_pruebas"
    filas = []
    for d in sorted(glob.glob(os.path.join(raiz, "2*"))):
        if desde and os.path.basename(d)[:8] < desde:
            continue
        p = os.path.join(d, "dbz3.log")
        if not os.path.isfile(p):
            continue
        try:
            t = leer(p)
        except OSError:
            continue
        if "adp" not in t:
            continue
        dif = lambda a, b: (t[b] - t[a]).total_seconds() if a in t and b in t else -1.0   # noqa: E731
        filas.append({"n": os.path.basename(d), "t": t["adp"], "dev": dif("adp", "dev"), "pres": dif("dev", "qs"), "mod": dif("pre", "post")})
    for f in filas:
        f["cerca"] = sum(1 for g in filas if g is not f and abs((g["t"] - f["t"]).total_seconds()) < 40)
    lento = lambda f: f["dev"] > 3 or f["pres"] > 12 or f["mod"] > 12 or (f["dev"] >= 0 and f["pres"] < 0)   # noqa: E731
    print("arranques: %d" % len(filas))
    for f in [f for f in filas if lento(f)][-30:]:
        print("  %-50s D3D12 %5.1f s  presentacion %6.1f s  modulo %6.1f s  arrancando a la vez %d" %
              (f["n"][:50], f["dev"], f["pres"], f["mod"], f["cerca"]))
    por = collections.defaultdict(lambda: [0, 0])
    for f in filas:
        por[min(f["cerca"], 3)][0] += 1
        por[min(f["cerca"], 3)][1] += 1 if lento(f) else 0
    for k in sorted(por):
        n, m = por[k]
        print("otros juegos arrancando a la vez = %d%s: %d arranques, %d lentos (%.1f %%)" % (k, "+" if k == 3 else "", n, m, 100.0 * m / max(n, 1)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
