# -*- coding: utf-8 -*-
r"""lamina.py (kit b3hd_conv, 12/11) - lamina original | port | diferencias, automatica, de dos tandas de capturas.
  python tools\b3hd_conv\b3hd_conv.py comparar <original> <port> [--salida DIR] [--pares auto|orden] [--fps 2] [--max 12]
<original>/<port>: carpeta de PNG/JPG, un .mp4 (se sacan cuadros con ffmpeg a --fps) o el nombre de una corrida de
D:\DBZ3HD\_pruebas (p. ej. 20261112-132635_sol_r2_shen_foto). Emparejado `auto`: cada captura del port con el cuadro
del original de pHash mas parecido, respetando el orden en el tiempo (programacion dinamica); `orden`: 1-1 por orden.
Por pareja usa tools/comparar.py (alineado ORB+RANSAC, mapa rojo de diferencias, recuadros) y junta todo en:
  DIR/lamina.png   una fila por pareja (original alineado | port | diferencias) con su cifra encima
  DIR/resumen.json y resumen.md   pareja, ficheros, pHash, % distinto de la zona comun, recuadros
Codigo de salida 0 (la lamina no juzga: da las cifras; el umbral lo pone quien la lee).
  python tools\b3hd_conv\lamina.py --autoprueba
"""
import glob
import json
import os
import re
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(AQUI))   # tools/
import comparar as cmp  # noqa: E402  (tools/comparar.py: alinear, comparar, phash, hamming)
from PIL import Image, ImageDraw  # noqa: E402

PRUEBAS = r"D:\DBZ3HD\_pruebas"
FUERA = ("hoja", "lamina", "_lado")   # montajes que ya hay en las carpetas de capturas: no son cuadros


def cuadros(src, trabajo, fps):
    """Lista ordenada de imagenes de `src` (carpeta, .mp4 o corrida de _pruebas)."""
    if not os.path.exists(src) and os.path.isdir(os.path.join(PRUEBAS, src)):
        src = os.path.join(PRUEBAS, src)
    if os.path.isfile(src) and src.lower().endswith((".mp4", ".mkv", ".avi")):
        os.makedirs(trabajo, exist_ok=True)
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-vf", "fps=%g" % fps, os.path.join(trabajo, "c%05d.png")],
                       check=True)
        src = trabajo
    if not os.path.isdir(src):
        raise SystemExit("no encuentro capturas en %s" % src)
    fs = [f for e in ("png", "jpg") for f in glob.glob(os.path.join(src, "*." + e))]
    fs = [f for f in fs if not any(x in os.path.basename(f).lower() for x in FUERA)]
    return sorted(fs)


def emparejar(org_h, port_h):
    """-> [indice del original por cada captura del port], con orden no decreciente y suma de Hamming minima."""
    n, m = len(port_h), len(org_h)
    INF = 1 << 30
    coste = [[cmp.hamming(p, o) for o in org_h] for p in port_h]
    mejor = [[INF] * m for _ in range(n)]
    desde = [[0] * m for _ in range(n)]
    for j in range(m):
        mejor[0][j] = coste[0][j]
    for i in range(1, n):
        k, kmin = 0, INF   # minimo prefijo de la fila anterior
        for j in range(m):
            if mejor[i - 1][j] < kmin:
                kmin, k = mejor[i - 1][j], j
            mejor[i][j], desde[i][j] = kmin + coste[i][j], k
    j = min(range(m), key=lambda x: mejor[n - 1][x])
    out = [0] * n
    for i in range(n - 1, -1, -1):
        out[i] = j
        j = desde[i][j]
    return out


def comparar(original, port, salida, pares="auto", fps=2.0, maximo=12, umbral=40):
    os.makedirs(salida, exist_ok=True)
    org = cuadros(original, os.path.join(salida, "cuadros_original"), fps)
    prt = cuadros(port, os.path.join(salida, "cuadros_port"), fps)
    if not org or not prt:
        raise SystemExit("sin capturas: original %d, port %d" % (len(org), len(prt)))
    if len(prt) > maximo:   # ponytail: muestra repartida; para mas parejas, varias laminas
        prt = [prt[round(k * (len(prt) - 1) / (maximo - 1))] for k in range(maximo)]
    ph_o = [cmp.phash(Image.open(f).convert("RGB")) for f in org]
    ph_p = [cmp.phash(Image.open(f).convert("RGB")) for f in prt]
    if pares == "auto":
        idx = emparejar(ph_o, ph_p)
    elif pares == "orden":
        idx = [min(i, len(org) - 1) for i in range(len(prt))]
    else:   # "t00=sh_054,t01=sh_058": a mano (cuando el port esta tan lejos que el pHash no encuentra pareja)
        nom = lambda f: os.path.splitext(os.path.basename(f))[0]  # noqa: E731
        dic = dict(x.split("=") for x in pares.split(","))
        prt = [f for f in prt if nom(f) in dic]
        ph_p = [cmp.phash(Image.open(f).convert("RGB")) for f in prt]
        io = {nom(f): j for j, f in enumerate(org)}
        idx = [io[dic[nom(f)]] for f in prt]
    filas, ANCHO, CAB = [], 1200, 26
    for k, (p, j) in enumerate(zip(prt, idx)):
        pref = os.path.join(salida, "par%02d" % k)
        rep = cmp.comparar(org[j], p, pref, umbral)
        txt = "\n".join(rep)
        m = re.search(r"distinta \(> \d+/255\): ([\d.]+) %", txt)
        fila = {"par": k, "original": org[j], "port": p, "phash": cmp.hamming(ph_o[j], ph_p[k]),
                "distinto_pct": float(m.group(1)) if m else None, "alineado": m is not None,
                "recuadros": len(re.findall(r"^\s+\d+\s+\(", txt, re.M)), "lado": pref + "_lado.png",
                "dudosa": cmp.hamming(ph_o[j], ph_p[k]) > 20}   # pHash > 20 de 64: puede no ser el mismo momento
        filas.append(fila)
    tiras = []
    for f in filas:
        im = Image.open(f["lado"]).convert("RGB")
        im = im.resize((ANCHO, max(1, round(im.height * ANCHO / im.width))))
        t = Image.new("RGB", (ANCHO, im.height + CAB), (16, 16, 20))
        t.paste(im, (0, CAB))
        cifra = ("%.2f %% distinto" % f["distinto_pct"]) if f["alineado"] else "SIN ALINEAR (revisar a ojo)"
        ImageDraw.Draw(t).text((6, 6), "par %d | %s | %s | pHash %d%s | %s | %d recuadros" % (
            f["par"], os.path.basename(f["original"]), os.path.basename(f["port"]), f["phash"],
            " (pareja dudosa)" if f["dudosa"] else "", cifra, f["recuadros"]),
            fill=(255, 230, 0))
        tiras.append(t)
    lam = Image.new("RGB", (ANCHO, sum(t.height for t in tiras)), (0, 0, 0))
    y = 0
    for t in tiras:
        lam.paste(t, (0, y))
        y += t.height
    lam.save(os.path.join(salida, "lamina.png"))
    json.dump({"original": original, "port": port, "pares": pares, "filas": filas},
              open(os.path.join(salida, "resumen.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    md = ["# Lamina original | port | diferencias", "", "original: `%s`  " % original, "port: `%s`" % port, "",
          "| par | original | port | pHash | % distinto | recuadros |", "|---|---|---|---|---|---|"]
    md += ["| %d | %s | %s | %d | %s | %d |" % (f["par"], os.path.basename(f["original"]), os.path.basename(f["port"]),
                                               f["phash"], "%.2f" % f["distinto_pct"] if f["alineado"] else "sin alinear",
                                               f["recuadros"]) for f in filas]
    open(os.path.join(salida, "resumen.md"), "w", encoding="utf-8").write("\n".join(md) + "\n")
    return filas


def main(argv):
    import argparse  # noqa: PLC0415
    ap = argparse.ArgumentParser(prog="b3hd_conv comparar")
    ap.add_argument("original")
    ap.add_argument("port")
    ap.add_argument("--salida", default=None, help="carpeta (por defecto E:\\DBZ3HD_trabajo\\b3hd_conv\\comparar\\<port>)")
    ap.add_argument("--pares", default="auto", help="auto | orden | t00=sh_054,t01=sh_058 (a mano)")
    ap.add_argument("--fps", type=float, default=2.0)
    ap.add_argument("--max", type=int, default=12)
    ap.add_argument("--umbral", type=int, default=40)
    a = ap.parse_args(argv)
    sal = a.salida or os.path.join(r"E:\DBZ3HD_trabajo\b3hd_conv\comparar", os.path.basename(os.path.normpath(a.port)))
    filas = comparar(a.original, a.port, sal, a.pares, a.fps, a.max, a.umbral)
    for f in filas:
        print("par %d: %s <-> %s  pHash %d  %s" % (f["par"], os.path.basename(f["original"]), os.path.basename(f["port"]),
                                                  f["phash"], "%.2f %%" % f["distinto_pct"] if f["alineado"] else "sin alinear"))
    print("lamina: %s" % os.path.join(sal, "lamina.png"))
    return 0


def autoprueba():
    """Sin juego: 4 originales distintos; el port = el 1 escalado y el 3 escalado con un parche rojo."""
    import tempfile  # noqa: PLC0415
    import numpy as np  # noqa: PLC0415
    d = tempfile.mkdtemp(prefix="b3hd_lamina_", dir="D:/tmp" if os.path.isdir("D:/tmp") else None)   # C: casi lleno
    rng = np.random.default_rng(3)
    os.makedirs(os.path.join(d, "o"))
    os.makedirs(os.path.join(d, "p"))
    org = []
    for k in range(4):
        im = Image.fromarray((rng.random((60, 80, 3)) * 120).astype(np.uint8)).resize((640, 480), Image.NEAREST)
        ImageDraw.Draw(im).ellipse([60 + 130 * k, 40 + 90 * (k % 2), 260 + 130 * k, 300 + 90 * (k % 2)], fill=(250, 250, 250))
        im.save(os.path.join(d, "o", "c%02d.png" % k))
        org.append(im)
    org[0].save(os.path.join(d, "o", "hoja.png"))   # un montaje en la carpeta no cuenta como cuadro
    org[1].resize((960, 720)).crop((40, 30, 920, 690)).save(os.path.join(d, "p", "t00.png"))
    p = org[3].resize((960, 720)).crop((40, 30, 920, 690))
    ImageDraw.Draw(p).rectangle([300, 200, 520, 420], fill=(255, 0, 0))
    p.save(os.path.join(d, "p", "t01.png"))
    filas = comparar(os.path.join(d, "o"), os.path.join(d, "p"), os.path.join(d, "s"))
    assert [os.path.basename(f["original"]) for f in filas] == ["c01.png", "c03.png"], filas
    assert filas[0]["alineado"] and filas[1]["alineado"], filas
    assert filas[0]["distinto_pct"] < 1.0 < filas[1]["distinto_pct"], filas
    assert emparejar([0, 0xFF, 0xFFFF], [0xFFFF, 0]) in ([2, 2], [0, 0]), "orden en el tiempo (nunca [2, 0])"
    assert os.path.getsize(os.path.join(d, "s", "lamina.png")) > 0
    print("lamina autoprueba OK (%s)" % d)
    return 0


if __name__ == "__main__":
    sys.exit(autoprueba() if "--autoprueba" in sys.argv else main(sys.argv[1:]))
