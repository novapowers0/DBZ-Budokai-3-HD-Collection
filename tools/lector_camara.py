r"""lector_camara.py (X0b, studies 5) - UN lector de camara y luchadores para el original (PCSX2 por PINE) y para el port
(lineas del log del juego), con el mismo formato de series. Sustituye a medir_mini_iw.py (IW) y pista_camara.py (B1).

Serie = lista de filas dict: t (s), fov, eye (x,y,z), tgt (x,y,z), j (x,y,z) jugador, r (x,y,z) rival, amc (indice).

  python tools\lector_camara.py grabar iw 30 salida.tsv [--estado 4]      original: PCSX2 abierto (PINE 28011), 1 a la vez
  python tools\lector_camara.py grabar b1 30 salida.tsv [--estado 3]
  python tools\lector_camara.py resumen iw ruta.tsv                       numeros de una serie ya grabada
  python tools\lector_camara.py dorado iw ruta.tsv dorado.json            graba el dorado (resumen + muestras)

Direcciones medidas (EE de PCSX2): ver JUEGOS. El lado port solo conoce lo que el juego escribe en el log
(iwmini: jugador (...) ... ojo (...)); lo que el port no registra queda como [H] en el informe.
"""
import json
import math
import os
import re
import statistics
import struct
import sys
import time
from datetime import datetime

R7_EMU = os.environ.get("DBZ3_R7_EMU", r"E:\DBZ3HD_trabajo\R7\emu")

# direccion EE -> campos. "ptr": la direccion guarda un puntero a la estructura (IW); "base": estructura fija (B1).
JUEGOS = {
    "iw": {   # SLES-55347, studies 8 (medir_mini_iw.py): *(0x49CA70)+0x800 jugador, +0x28D0 rival; *(0x49CD6C) camara
        "ptr_lucha": 0x49CA70, "off_j": 0x800, "off_r": 0x28D0, "pos": 0x40,
        "ptr_cam": 0x49CD6C, "eye": 0x80, "tgt": 0x90, "fov": 0xB4, "amc": 0x180,
    },
    "b1": {   # SLES-51233, studies 1 F03 (pista_camara.py): camara fija 0x555B00 (fov, ojo +0x10, objetivo +0x20)
        "base_cam": 0x555B00, "fov": 0x0, "eye": 0x10, "tgt": 0x20, "j": 0x728430, "r": 0x723FF0,
    },
}
COLUMNAS = ["t", "fov", "ex", "ey", "ez", "ox", "oy", "oz", "jx", "jy", "jz", "rx", "ry", "rz", "amc"]


def _f(v):
    return round(struct.unpack("<f", struct.pack("<I", v & 0xFFFFFFFF))[0], 3)


# ------------------------------------------------------------------ original: PCSX2 por PINE
def grabar(juego, seg, salida, estado=None):
    """Graba la serie del original en un TSV con COLUMNAS (una fila cada vez que algo cambia)."""
    sys.path.insert(0, R7_EMU)
    import sonda                                                  # noqa: E402  (E:\DBZ3HD_trabajo\R7\emu\sonda.py)
    spec = JUEGOS[juego]
    p = sonda.Pine()
    if estado is not None:
        p.cargar(estado)
    t0, prev, n = time.time(), None, 0
    with open(salida, "w", encoding="utf-8") as f:
        f.write("\t".join(COLUMNAS) + "\n")
        while time.time() - t0 < seg:
            if juego == "iw":
                ca, cam = p.lote([(spec["ptr_lucha"], 4), (spec["ptr_cam"], 4)])
                if not ca or not cam:
                    time.sleep(0.005)
                    continue
                pares = [(cam + spec["fov"], 4)] + [(cam + spec["eye"] + 4 * i, 4) for i in range(3)] \
                    + [(cam + spec["tgt"] + 4 * i, 4) for i in range(3)] \
                    + [(ca + spec["off_j"] + spec["pos"] + 4 * i, 4) for i in range(3)] \
                    + [(ca + spec["off_r"] + spec["pos"] + 4 * i, 4) for i in range(3)] + [(cam + spec["amc"], 4)]
            else:
                b = spec["base_cam"]
                pares = [(b + spec["fov"], 4)] + [(b + spec["eye"] + 4 * i, 4) for i in range(3)] \
                    + [(b + spec["tgt"] + 4 * i, 4) for i in range(3)] \
                    + [(spec["j"] + 4 * i, 4) for i in range(3)] + [(spec["r"] + 4 * i, 4) for i in range(3)]
            v = p.lote(pares)
            fila = [_f(x) for x in v[:13]] + [v[13] if len(v) > 13 else 0]
            if fila != prev:
                f.write("%.3f\t" % (time.time() - t0) + "\t".join(str(x) for x in fila) + "\n")
                n += 1
                prev = fila
    return n


# ------------------------------------------------------------------ lectores de series
def _fila(t, fov, eye, tgt, j, r, amc=0):
    return {"t": t, "fov": fov, "eye": eye, "tgt": tgt, "j": j, "r": r, "amc": amc}


def serie_tsv(juego, ruta):
    """TSV de grabar() o el formato de medir_mini_iw.py (J40,J44.. C80..): a filas comunes."""
    with open(ruta, encoding="utf-8") as f:
        cab = f.readline().rstrip("\n").split("\t")
        filas = []
        for l in f:
            c = l.rstrip("\n").split("\t")
            if len(c) != len(cab):
                continue
            d = dict(zip(cab, c))
            g = lambda k: float(d[k])               # noqa: E731
            if "ex" in d:
                filas.append(_fila(g("t"), g("fov"), (g("ex"), g("ey"), g("ez")), (g("ox"), g("oy"), g("oz")),
                                   (g("jx"), g("jy"), g("jz")), (g("rx"), g("ry"), g("rz")), int(float(d["amc"]))))
            else:                                   # medir_mini_iw.py
                filas.append(_fila(g("t"), g("CB4"), (g("C80"), g("C84"), g("C88")), (g("C90"), g("C94"), g("C98")),
                                   (g("J40"), g("J44"), g("J48")), (g("R40"), g("R44"), g("R48")), int(float(d["C180"]))))
    return filas


_MARCA = re.compile(r"\[(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d\.\d+)\]")
_IW = re.compile(r"iwmini: jugador \(([-\d.]+), ([-\d.]+), ([-\d.]+)\).*?ojo \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)")


def _seg(l):
    m = _MARCA.match(l)
    return int(m.group(4)) * 3600 + int(m.group(5)) * 60 + float(m.group(6)) if m else None


def serie_log_port(juego, texto):
    """Serie del port desde su log. IW: 'iwmini: jugador (x, y, z) ... ojo (x, y, z)'. fov/objetivo/rival: [H]
    (el juego no los escribe todavia); quedan en 0."""
    filas = []
    if juego != "iw":
        raise SystemExit(f"el port no escribe camara en el log para '{juego}' todavia [H]")
    for l in texto.splitlines():
        m = _IW.search(l)
        t = _seg(l)
        if m and t is not None:
            v = [float(x) for x in m.groups()]
            filas.append(_fila(t, 0.0, tuple(v[3:6]), (0.0, 0.0, 0.0), tuple(v[0:3]), (0.0, 0.0, 0.0)))
    return filas


# ------------------------------------------------------------------ resumen y comparacion
def _d(a, b):
    return math.dist(a, b)


def resumen(filas):
    """Numeros que se pueden comparar aunque la partida no sea la misma (el original lo jugo una persona, el port
    juega solo): salida del jugador, y la camara RELATIVA al jugador (distancia ojo-jugador, altura, yaw del ojo)."""
    # B1c: los minijuegos de botones/disparo/vuelo tienen al jugador en el ORIGEN de IW (0,0,0) a proposito; antes se
    # descartaban todas sus filas. Vale la fila si hay jugador O camara (el ojo se mueve con el clip).
    vivas = [r for r in filas if any(abs(x) > 1e-6 for x in r["j"]) or any(abs(x) > 1e-6 for x in r["eye"])]
    if not vivas:
        return {"n": len(filas), "n_vivas": 0}
    rel = [(_d(r["eye"], r["j"]), r["eye"][1] - r["j"][1],
            math.degrees(math.atan2(r["eye"][0] - r["j"][0], r["eye"][2] - r["j"][2]))) for r in vivas
           if any(abs(x) > 1e-6 for x in r["eye"])]
    out = {"n": len(filas), "n_vivas": len(vivas), "dur_s": round(vivas[-1]["t"] - vivas[0]["t"], 2)}
    # la salida absoluta solo se compara si el original NO juega en el origen (los de campo); en botones el jugador
    # esta en (0,0,0) y la camara es relativa a el.
    if any(abs(x) > 1e-6 for x in vivas[0]["j"]):
        out["salida"] = [round(x, 2) for x in vivas[0]["j"]]
    else:
        out["origen_jugador"] = True
    if rel:
        for i, nom in enumerate(("dist_ojo_jugador", "altura_ojo", "yaw_ojo_grados")):
            col = [x[i] for x in rel]
            out[nom] = {"min": round(min(col), 2), "mediana": round(statistics.median(col), 2), "max": round(max(col), 2)}
    fov = sorted({r["fov"] for r in vivas if r["fov"]})
    if fov:
        out["fov"] = {"min": fov[0], "max": fov[-1]}
    amc = sorted({r["amc"] for r in vivas})
    if len(amc) > 1 or amc[0]:
        out["amc"] = amc
    return out


def comparar_resumen(oro, port, tol_pos=2.0, tol_rel=0.15):
    """Lista de (nombre, ok, detalle). Posiciones: tolerancia absoluta tol_pos; relativas: tol_rel de la mediana del
    original. Lo que el port no registra (fov, amc) sale como [H], no como fallo."""
    out = []
    if not port.get("n_vivas"):
        return [("serie del port", False, "sin muestras 'iwmini: jugador/ojo' en el log")]
    if "salida" in oro and "salida" in port:
        d = _d(oro["salida"], port["salida"])
        out.append(("salida del jugador", d <= tol_pos, f"original {oro['salida']} port {port['salida']} (dif {d:.2f})"))
    elif oro.get("origen_jugador"):
        out.append(("salida del jugador", None, "el original juega en el origen (0,0,0): la camara se compara relativa"))
    for nom in ("dist_ojo_jugador", "altura_ojo"):
        if nom in oro and nom in port:
            a, b = oro[nom]["mediana"], port[nom]["mediana"]
            lim = max(abs(a) * tol_rel, 0.5)
            out.append((f"{nom} (mediana)", abs(a - b) <= lim, f"original {a} port {b} (limite {lim:.2f})"))
    if "yaw_ojo_grados" in oro and "yaw_ojo_grados" in port:
        a, b = oro["yaw_ojo_grados"]["mediana"], port["yaw_ojo_grados"]["mediana"]
        d = abs((a - b + 180) % 360 - 180)
        out.append(("yaw del ojo (mediana)", d <= 15, f"original {a} port {b} (dif {d:.1f} grados, limite 15)"))
    if "fov" in oro:
        out.append(("fov", None, "[H] el port no escribe el fov en el log"))
    if "amc" in oro:
        out.append(("indice #AMC", None, "[H] el port no escribe el #AMC de la camara en el log"))
    return out


def main(a):
    if len(a) >= 4 and a[0] == "grabar":
        est = int(a[a.index("--estado") + 1]) if "--estado" in a else None
        print("filas", grabar(a[1], float(a[2]), a[3], est))
    elif len(a) >= 3 and a[0] == "resumen":
        print(json.dumps(resumen(serie_tsv(a[1], a[2])), indent=1))
    elif len(a) >= 4 and a[0] == "dorado":
        s = serie_tsv(a[1], a[2])
        paso = max(1, len(s) // 200)
        oro = {"juego": a[1], "fuente": os.path.basename(a[2]), "creado": datetime.now().isoformat(timespec="seconds"),
               "resumen": resumen(s), "muestras": [s[i] for i in range(0, len(s), paso)]}
        with open(a[3], "w", encoding="utf-8") as f:
            json.dump(oro, f, indent=1)
        print("dorado", a[3], oro["resumen"].get("n_vivas"), "filas vivas")
    else:
        print(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
