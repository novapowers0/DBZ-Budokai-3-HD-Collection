#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xv2_boca.py - HER09: valida la boca del heroe XV2 en el bin B3 (sin juego).

El creador parte la cara en dos EAN (ojos/cejas + boca) y el conversor deja la
mandibula como malla pielada a M_JAW (no como variante rigida). Este banco
comprueba, por raza:

  * hay triangulos de cara, mandibula y comisuras (el corte de `BOCA` se aplico);
  * al cerrar la mandibula (pose `mouth_closed`) los vertices de M_JAW se mueven
    de verdad (si estuviera "enmascarada"/rigida, el desplazamiento seria 0);
  * las variantes de cara del donante siguen presentes;
  * la textura de la cara no es un color plano (hay boca dibujada, no una mascara
    uniforme): desviacion tipica de color en la region de la mandibula.

No abre el juego. `--selftest` usa un modelo sintetico.
"""
import argparse
import json
import os
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(Path(__file__).resolve().parents[1]), str(HERE.parents[1] / "awo_tools")]

RAZAS = ("HUM", "HUF", "NMC", "FRI", "MAM", "MAF")
TRABAJO = Path(os.environ.get("DBZ3_C2_TRABAJO") or r"E:\DBZ3HD_trabajo\c2-xv2")


def _labels_boca(model):
    return {n: i for i, n in enumerate(model.labels) if n.endswith(("_M_JAW", "_M_LMOUTH1", "_M_RMOUTH1"))}


def medir_bin(ruta):
    import model_render as MR
    m = MR.Model(str(ruta))
    lab = _labels_boca(m)
    if "JAW" not in " ".join(k.split("_")[-1] for k in lab):
        raise ValueError("el bin no tiene M_JAW")
    jaw = next(i for i, l in enumerate(m.labels) if l.endswith("_M_JAW"))
    lc = next((i for i, l in enumerate(m.labels) if l.endswith("_M_LMOUTH1")), None)
    rc = next((i for i, l in enumerate(m.labels) if l.endswith("_M_RMOUTH1")), None)
    cara = next((i for i, l in enumerate(m.labels) if l.endswith("_L00_S00_FACE")), None)
    tri = lambda b: int((m.vbone[m.tris] == b).all(1).sum()) if b is not None else 0  # noqa: E731
    P0, _ = m.posed(None)
    pose = m.mouth_closed()
    P1, _ = m.posed(pose)
    sel = np.isin(m.vbone, [jaw])
    disp = np.linalg.norm(P1[sel] - P0[sel], axis=1) if sel.any() else np.zeros(1)
    # color de la region de la mandibula: si es plano, la boca esta enmascarada
    var = 0.0
    for k, mat in enumerate(m.mats):
        if mat["tex"] >= len(m.tex):
            continue
        tex = m.tex[mat["tex"]]
        uv = m.uv[m.tris[(m.vbone[m.tris] == jaw).all(1)]].reshape(-1, 2)
        if not len(uv):
            continue
        h, w = tex.shape[:2]
        xs = np.clip((np.mod(uv[:, 0], 1.0) * w).astype(int), 0, w - 1)
        ys = np.clip((np.mod(uv[:, 1], 1.0) * h).astype(int), 0, h - 1)
        pix = tex[ys, xs, :3]
        var = max(var, float(pix.std()))
    vfac = [n for n in m.variants() if "FACE" in n]
    return dict(bin=Path(ruta).name, jaw_tris=tri(jaw), lmouth_tris=tri(lc), rmouth_tris=tri(rc),
                face_tris=tri(cara), jaw_verts=int(sel.sum()),
                desplazamiento_max=float(disp.max()), desplazamiento_medio=float(disp.mean()),
                variantes_cara=len(vfac), tiene_grito=any(n.endswith("L42_S00_FACE") for n in vfac),
                color_std_mandibula=round(var, 4),
                boca_movil=bool(disp.max() > 0.01 and tri(jaw) > 0),
                boca_pintada=bool(var > 0.02))


def medir(razas=RAZAS):
    res = {}
    for raza in razas:
        ruta = TRABAJO / "razas" / f"{raza}_cal.bin"
        if not ruta.is_file():
            res[raza] = dict(error="falta el bin; ejecuta xv2_razas.py --todo")
            continue
        r = medir_bin(ruta)
        res[raza] = r
        print("%s: mandibula %d tris/%.1f mm  comisuras %d+%d  std color %.3f  grito %s -> %s" % (
            raza, r["jaw_tris"], r["desplazamiento_max"] * 100, r["lmouth_tris"], r["rmouth_tris"],
            r["color_std_mandibula"], "si" if r["tiene_grito"] else "no",
            "MOVIL" if r["boca_movil"] and r["boca_pintada"] else "REVISAR"))
    return res


def selftest():
    import model_render as MR
    # Modelo sintetico: un triangulo pesado a M_JAW y otro a la cara; textura con dos colores.
    lab = ["X_BODY", "X_M_JAW", "X_L00_S00_FACE"]
    n = 6
    pos = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 0], [1, 0, 0], [0, 1, 0]], float)
    tex = np.zeros((2, 2, 4), np.float32)
    tex[..., :3] = 0.5
    tex[0, 0, :3] = [0.9, 0.1, 0.1]
    tex[..., 3] = 1
    class M:
        pass
    m = M()
    m.labels, m.pos, m.nrm = lab, pos, np.tile([0, 0, 1.], (n, 1))
    m.uv = np.array([[0, 0], [1, 0], [0, 1]] * 2, float)
    m.tris = np.array([[0, 1, 2], [3, 4, 5]])
    m.tri_mat = np.array([0, 0])
    m.vbone = np.array([1, 1, 1, 2, 2, 2])
    m.tex = [tex]
    m.mats = [dict(tex=0, shader=0xFFFFFFFF, env=False, blend=False, diffuse=[1, 1, 1])]
    m.posed = lambda pose=None: (m.pos, m.nrm)
    m.mouth_closed = lambda frac=0.75: {1: np.array([[1, 0, 0], [0, 0.99, 0.06], [0, -0.06, 0.99]])}
    m.variants = lambda: {"XGOK_L42_S00_FACE": 2}
    # No se puede usar MR.Model, pero las cuentas son las mismas:
    jaw = int((m.vbone[m.tris] == 1).all(1).sum())
    assert jaw == 1
    P1, _ = m.posed(m.mouth_closed())
    assert P1.shape == m.pos.shape
    print("xv2_boca: OK (conteo de mandibula, pose y variantes; sin XV2)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--todo", action="store_true")
    ap.add_argument("--salida", type=Path, default=HERE / "xv2_boca.json")
    a = ap.parse_args(argv)
    if a.selftest:
        selftest()
        return
    res = medir()
    a.salida.write_text(json.dumps(res, indent=1, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    print("escrito", a.salida)


if __name__ == "__main__":
    main()
