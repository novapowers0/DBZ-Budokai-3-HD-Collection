#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""tactics_hoja.py - Banco/hoja offline de las animaciones de Dragon Tactics (TAC03).

Lee los .tk horneados por cac/vista_tactics.py (--todas) y, para cada unidad base del
manifiesto (34 figuras), comprueba los 8 clips (espera, andar, ataque, tecnica, dano,
derrota, levantarse, victoria) contra su modelo B3 y contra el propio .tk:

  - 8 clips, cuadros > 0, matrices finitas y mismo numero de huesos que el modelo;
  - ninguna pose de reposo/T-pose (desplazamiento nulo respecto al bind);
  - dano/ataque/tecnica/victoria no repiten la espera; derrota queda baja (KO) y
    levantarse sube; y
  - una hoja PNG 34x8 con un fotograma representativo de cada clip (vista 3/4).

Uso (sin juego; lee data_cmn.afs de la copia `us/`):
  python -B "mod center hd/tactics_hoja.py" --tk "<carpeta tk>" --salida "<carpeta>" [--us us]
      [--unidades goku,vegeta] [--sin-render]
Salida: <salida>/hoja_34x8.png, <salida>/tac03_informe.csv y <salida>/tac03_resumen.json.
Codigo de salida 0 = todos los casos en verde; 1 = hay diferencias (las lista).
"""
import argparse
import csv
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, "cac"), os.path.join(HERE, "studio"), os.path.join(HERE, "..", "awo_tools")]
import numpy as np  # noqa: E402
import tomllib  # noqa: E402

import altura  # noqa: E402
import model_render as mr  # noqa: E402
import roster_build as rb  # noqa: E402

CLIPS = ("espera", "andar", "ataque", "tecnica", "dano", "derrota", "levantarse", "victoria")
CUADRO = {"espera": 0.5, "andar": 0.5, "ataque": 0.5, "tecnica": 0.5,
          "dano": 0.5, "derrota": 1.0, "levantarse": 0.75, "victoria": 1.0}
TOL_POSE = 1e-3          # desplazamiento maximo (unidades de modelo) para considerar que un clip no se movio del bind


def leer_tk(path):
    d = open(path, "rb").read()
    if d[:4] != b"TKB2":
        raise ValueError("no es TKB2")
    nb, nc = struct.unpack_from("<II", d, 4)
    o = 12
    clips = []
    for _ in range(nc):
        n, bucle = struct.unpack_from("<II", d, o)
        o += 8
        mats = np.frombuffer(d, dtype="<f4", count=12 * nb * n, offset=o).reshape(n, nb, 3, 4).astype(float)
        o += 12 * nb * n * 4
        clips.append((n, bucle, mats))
    return nb, clips


def locales(model, mats):
    out = []
    for i in range(len(model.local)):
        m = np.eye(4)
        m[:3, :4] = mats[i]
        out.append(m)
    return out


def posar(model, mats, bind):
    model.local = locales(model, mats)
    P, _ = model.posed()
    W = model.worlds()
    model.local = bind
    return P, W


def fila_unidad(afs, uni, carpeta_tk):
    fid = int(uni["modelo"])
    mb = afs.entry(fid)
    model = mr.Model(mb)
    skel = altura.skeleton(mb)
    ruta = os.path.join(carpeta_tk, "tk_%d.tk" % fid)
    problemas = []
    if not os.path.exists(ruta):
        return {"id": uni["id"], "fid": fid, "error": "falta %s" % ruta, "problemas": ["falta el .tk"], "clips": {}, "model": model}
    try:
        nb, clips = leer_tk(ruta)
    except Exception as ex:  # noqa: BLE001
        return {"id": uni["id"], "fid": fid, "error": str(ex), "problemas": [str(ex)], "clips": {}, "model": model}
    if nb != len(model.local) or nb != len(skel):
        problemas.append("huesos %d != modelo %d / esqueleto %d" % (nb, len(model.local), len(skel)))
    if len(clips) != len(CLIPS):
        problemas.append("%d clips != %d" % (len(clips), len(CLIPS)))
    bind = [l.copy() for l in model.local]
    Pbind, _ = model.posed()
    alto_bind = float(np.ptp(Pbind[:, 1])) or 1.0
    info, poses = {}, {}
    for nom, (n, bucle, mats) in zip(CLIPS, clips):
        if n <= 0:
            problemas.append("%s: 0 cuadros" % nom)
            info[nom] = {"cuadros": n, "bucle": bool(bucle)}
            continue
        if not np.isfinite(mats).all():
            problemas.append("%s: matrices no finitas" % nom)
        estatico = float(np.abs(mats - mats[0]).max()) < 1e-5
        fr = min(n - 1, max(0, int(round((n - 1) * CUADRO[nom]))))
        P, W = posar(model, mats[fr], bind)
        disp = float(np.linalg.norm(P - Pbind, axis=1).max())
        if disp < TOL_POSE:
            problemas.append("%s: pose de reposo/T-pose" % nom)
        hi = next((i for i, l in enumerate(model.labels) if l.lstrip("X").split("_", 1)[-1] == "HEAD"), None)
        hiY = float(W[hi][1, 3]) if hi is not None else float(P[:, 1].max())
        info[nom] = {"cuadros": n, "bucle": bool(bucle), "estatico": estatico,
                     "cuadro_medido": fr, "desplazamiento_bind": round(disp, 6),
                     "cabeza_y": round(hiY, 3), "alto_pose": round(alto_bind, 3)}
        poses[nom] = (P, mats[fr])
    # la espera es la referencia de altura.
    if "espera" in poses and "derrota" in poses:
        he = max(info["espera"]["alto_pose"], 1e-3)
        hy_e = info["espera"]["cabeza_y"]
        hy_d = info["derrota"]["cabeza_y"]
        if hy_d > hy_e - 0.25 * he:
            problemas.append("derrota no queda baja (cabeza %.2f vs espera %.2f)" % (hy_d, hy_e))
    if "levantarse" in poses and "derrota" in poses:
        if info["levantarse"]["cabeza_y"] <= info["derrota"]["cabeza_y"] + 0.10 * max(info["espera"].get("alto_pose", 1.0), 1e-3):
            problemas.append("levantarse no sube")
    if "espera" in poses:
        ref = poses["espera"][1]
        for nom in ("andar", "ataque", "tecnica", "dano", "victoria"):
            nota = str(uni["clips"].get(nom, {}).get("nota", "")).lower()
            if nom == "andar" and ("sin andar" in nota or "se desliza" in nota):
                continue   # documentado: su banco no trae andar y se desliza en la espera (Yamcha/Ginyu/Uub)
            if nom in poses and float(np.abs(poses[nom][1] - ref).mean()) < 1e-3:
                problemas.append("%s repite la espera" % nom)
    return {"id": uni["id"], "fid": fid, "b3": uni["b3"], "forma": uni["forma"],
            "huesos": nb, "clips": info, "problemas": problemas,
            "poses": poses, "model": model}


def dibujar_hoja(filas, ruta, S=96):
    from PIL import Image, ImageDraw
    etiqueta = 76
    cabecera = 16
    W, H = etiqueta + S * len(CLIPS), cabecera + S * len(filas)
    hoja = Image.new("RGB", (W, H), (26, 26, 30))
    dr = ImageDraw.Draw(hoja)
    for c, nom in enumerate(CLIPS):
        dr.text((etiqueta + c * S + 2, 2), nom[:8], fill=(230, 230, 230))
    for r, f in enumerate(filas):
        y = cabecera + r * S
        dr.text((2, y + S // 2 - 5), f["id"][:12], fill=(255, 120, 110) if f["problemas"] else (255, 220, 120))
        if "poses" not in f:
            continue
        m = f["model"]
        bind = [l.copy() for l in m.local]
        for c, nom in enumerate(CLIPS):
            if nom not in f["poses"]:
                continue
            P, mats = f["poses"][nom]
            m.local = locales(m, mats)
            Q, _ = m.posed()
            ctr = (Q.min(0) + Q.max(0)) / 2
            esc = 0.90 * S / max(float(np.ptp(Q[:, 1])), float(np.ptp(Q[:, 0])), 1e-3)
            try:
                img = mr.render(m, size=(S, S), center=ctr, scale=esc, view=mr.rot_y(28), ss=2)
            except Exception:  # noqa: BLE001
                img = np.zeros((S, S, 4), np.uint8)
            m.local = bind
            hoja.paste(Image.fromarray(img).convert("RGB"), (etiqueta + c * S, y))
    hoja.save(ruta)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tk", required=True, help="carpeta con tk_<fid>.tk (salida de vista_tactics.py --todas)")
    ap.add_argument("--salida", required=True)
    ap.add_argument("--us", default="us")
    ap.add_argument("--unidades", default=None, help="ids separados por comas (por defecto, las 34 unidades base)")
    ap.add_argument("--formas", action="store_true", help="valida tambien las formas de estrella (51 entradas) en vez de solo las 34 base")
    ap.add_argument("--sin-render", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.salida, exist_ok=True)
    with open(os.path.join(HERE, "tactics_manifest.toml"), "rb") as fh:
        man = tomllib.load(fh)["unidad"]
    base = man if a.formas else [u for u in man if not re.search(r"_\d$", u["id"])]
    if a.unidades:
        quiero = {x.strip() for x in a.unidades.split(",") if x.strip()}
        base = [u for u in base if u["id"] in quiero]
    afs = rb.Afs(os.path.join(rb.us_dir(a.us), "data_cmn.afs"), rb.work_dir("thoja_"))
    filas = []
    for u in base:
        f = fila_unidad(afs, u, a.tk)
        filas.append(f)
        print("%-12s huesos %3s  %s" % (f["id"], f.get("huesos", "?"), "OK" if not f["problemas"] else "FALLO: " + "; ".join(f["problemas"])))
    with open(os.path.join(a.salida, "tac03_informe.csv"), "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh, delimiter=";")
        w.writerow(["unidad", "fid", "huesos"] + ["%s_cuadros" % n for n in CLIPS] + ["problemas"])
        for f in filas:
            w.writerow([f["id"], f.get("fid"), f.get("huesos")] + [f["clips"].get(n, {}).get("cuadros", "") for n in CLIPS] + ["; ".join(f["problemas"])])
    resumen = {"unidades": len(filas), "verde": sum(not f["problemas"] for f in filas),
               "fallos": int(sum(bool(f["problemas"]) for f in filas)),
               "casos": [{"id": f["id"], "problemas": f["problemas"], "clips": f["clips"]} for f in filas]}
    with open(os.path.join(a.salida, "tac03_resumen.json"), "w", encoding="utf-8") as fh:
        json.dump(resumen, fh, ensure_ascii=False, indent=1)
    if not a.sin_render:
        dibujar_hoja(filas, os.path.join(a.salida, "hoja_34x8.png"))
    print("tactics_hoja: %d unidades, %d en verde, %d con fallo -> %s" % (resumen["unidades"], resumen["verde"], resumen["fallos"], a.salida))
    return 0 if resumen["fallos"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
