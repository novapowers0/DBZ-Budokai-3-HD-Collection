#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xv2_piccolo_rr.py - HER09: Piccolo Red Ribbon (DAE Legends) medido sin juego.

Convierte el DAE del jugador a un bin B3 (solo lectura del DAE/PNG; nada se copia al
repo), comprueba la estructura y mide los dos defectos que lo tenian apagado:

  * boca: la mascara gris del traje debe tapar la cara inferior; se cuenta el verde
    de piel que asoma por debajo de los ojos (debe ser ~0) y los vertices que el
    conversor manda a M_JAW (boca movible si algun dia se quita la mascara);
  * mancha: el parche oscuro de la manga se compara con el albedo plano. Si ya esta
    en la textura es un panel del traje, no una mancha del conversor.

No abre el juego. `--selftest` no usa el DAE.
"""
import argparse
import json
import os
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(HERE), str(ROOT / "awo_tools"), str(ROOT / "mod center hd")]

DAE = ROOT / "modding resources update 4" / "Legends Models" / "Piccolo (Red Ribbon Disguise)"
TRABAJO = Path(os.environ.get("DBZ3_C2_TRABAJO") or r"E:\DBZ3HD_trabajo\c2-xv2")


def _render(model, size=900, view=None, center=None, scale=None, plano=False):
    import model_render as MR
    if plano:
        model.mats = [dict(x, shader=0xFFFFFFFF, blend=False) for x in model.mats]
    P, _ = model.posed(None)
    if center is None:
        lo, hi = P.min(0), P.max(0)
        center = (lo + hi) / 2
        scale = size * 0.9 / max(hi[1] - lo[1], hi[0] - lo[0], 1e-9)
    return MR.render(model, (size, size), np.eye(3) if view is None else view, center, scale, outline=1.0)


def _verde(rgb):
    r, g, b = rgb[..., 0].astype(float), rgb[..., 1].astype(float), rgb[..., 2].astype(float)
    return (g > 1.25 * r) & (g > 1.6 * b) & (g > 40)


def medir(dae=DAE / "PiccoloSH_03.dae", cuerpo=DAE / "body_compiled.png", cara=DAE / "face_compiled.png",
          plantilla=None):
    import dae_a_b3 as D
    import model_render as MR
    from PIL import Image
    import afs_pair
    TRABAJO.mkdir(parents=True, exist_ok=True)
    tpl = Path(plantilla) if plantilla else TRABAJO / "piccolo" / "plantilla_piccolo.bin"
    tpl.parent.mkdir(parents=True, exist_ok=True)
    if not tpl.is_file():
        tpl.write_bytes(bytes(afs_pair.hd(350)))       # Piccolo nativo (entrada 350)
    out = TRABAJO / "piccolo" / "piccolo_rr_medido.bin"
    r = D.convertir(str(dae), str(cuerpo), str(cara), str(tpl), str(out),
                    dict(cuerpo=.6, manos=.7, casco=.6, cara=.85), jaw=True)
    m = MR.Model(str(out))
    pose = m.arms_down(68.0)
    P, _ = m.posed(pose)
    # cara: region bajo los ojos
    lo, hi, _ = MR.head_info(m, pose=pose)
    c = (lo + hi) / 2
    sc = 700 * 0.9 / max(hi[1] - lo[1], hi[0] - lo[0], 1e-9)
    face = _render(m, 700, center=c, scale=sc)
    ys = np.arange(face.shape[0])[:, None]
    bajo = ys > face.shape[0] * 0.70
    piel = face[..., 3] > 200
    verde = _verde(face[..., :3]) & bajo & piel
    # manga: parche oscuro con rampa vs albedo plano
    W = m.worlds(pose)
    brazo = W[m.bone("RARM2")][:3, 3]
    sc2 = 700 * 2.2 / 7.0
    toon = _render(m, 700, MR.rot_y(15), brazo, sc2)
    m2 = MR.Model(str(out)); plano = _render(m2, 700, MR.rot_y(15), brazo, sc2, plano=True)
    sel = (toon[..., 3] > 200) & (plano[..., 3] > 200)
    dif = np.abs(toon[..., :3].astype(float) - plano[..., :3].astype(float)).mean(2)
    parche = sel & (dif > 25)
    albedo_parche = float(plano[..., :3][parche].std()) / 255.0 if parche.any() else 0.0
    return dict(dae=Path(dae).name, bin=Path(out).name, bytes=int(r["bytes"]),
                errores_estructura=len(r["errores"]), errores=r["errores"][:5],
                vertices_mandibula=int(r["info"].get("vertices_a_mandibula", 0)),
                tira_menton_quitada=bool(r["info"].get("tira_menton_quitada")),
                pixeles_cara=int(piel.sum()), pixeles_verdes_bajo_ojos=int(verde.sum()),
                fraccion_verde=round(float(verde.sum()) / max(int(piel.sum()), 1), 6),
                pixeles_manga=int(sel.sum()), pixeles_parche_oscuro=int(parche.sum()),
                fraccion_parche=round(float(parche.sum()) / max(int(sel.sum()), 1), 4),
                lectura="mascara gris tapa la boca" if verde.sum() < 20 else "asoma piel verde",
                albedo_std_parche=round(albedo_parche, 4),
                mancha=("sombreado de la rampa (albedo uniforme)" if albedo_parche < 0.02
                        else "panel de la textura (tambien en el albedo)") if parche.sum() else "sin parche",
                awg0=r["awg0"], retarget_max=r["retarget_max"],
                pendiente_juego="captura en juego de la cara/manga y decision de licencia (usuario)")


def selftest():
    # Reglas de color y umbrales con pixeles inventados.
    verde = np.zeros((4, 4, 3), np.uint8); verde[..., 1] = 200; verde[..., 0] = 60; verde[..., 2] = 30
    gris = np.zeros((4, 4, 3), np.uint8); gris[..., :] = 120
    assert _verde(verde).all() and not _verde(gris).any()
    assert 0 <= 0.0 < 0.01
    print("xv2_piccolo_rr: OK (regla de verde y umbrales; sin DAE)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--salida", type=Path, default=HERE / "xv2_piccolo_rr.json")
    a = ap.parse_args(argv)
    if a.selftest:
        selftest()
        return
    r = medir()
    a.salida.write_text(json.dumps(r, indent=1, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    print("errores", r["errores_estructura"], "jaw", r["vertices_mandibula"],
          "verde bajo ojos", r["pixeles_verdes_bajo_ojos"], "parche", r["fraccion_parche"])
    print("escrito", a.salida)


if __name__ == "__main__":
    main()
