#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ssim_material.py - SSIM del MATERIAL: textura horneada desde los datos de XV2 (referencia) frente a la que queda
dentro del AZT del bin B3 convertido (lo que ve el juego, tras DXT3 y reduccion). No ejecuta XV2.
  python ssim_material.py JRN 050 <bin.bin> [--piel R,G,B]
Se compara cada textura (RGB, sin alfa) y se saca la media ponderada por pixeles."""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, "..")]
import numpy as np  # noqa: E402
import xv2_a_b3 as X  # noqa: E402
import model_render as MR  # noqa: E402
from skimage.metrics import structural_similarity as ssim  # noqa: E402


def referencia(clave, forma, piel):
    """[(nombre, RGB horneado a tamano completo)] en el orden en que xv2_a_b3 crea las texturas."""
    import sdbh_model as S
    X.S = S
    c = X.L.cpk_xv2()
    tpl = S.Template(os.path.join(os.environ.get("E3_TMP", r"E:\DBZ3HD_trabajo\xv2"), "plantilla_goku.bin"))
    esk, bcs, partes, bset = X.cargar(c, clave, forma)
    out = {}
    for tipo, d in partes.items():
        for sm in d["subs"]:
            if sm.texdefs[0] < len(d["emb"]) and d["emb"][sm.texdefs[0]][1] is not None:
                out[sm.name] = X.textura(bcs, bset, tipo, d, sm, piel)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("clave")
    ap.add_argument("forma")
    ap.add_argument("bin")
    ap.add_argument("--piel")
    a = ap.parse_args()
    piel = [float(v) for v in a.piel.split(",")] if a.piel else None
    ref = referencia(a.clave, a.forma, piel)
    tex = MR._textures(open(a.bin, "rb").read())
    from PIL import Image
    res = []
    for i, t in enumerate(tex):
        t8 = (np.clip(np.asarray(t), 0, 1) * 255).astype(np.uint8) if np.asarray(t).dtype != np.uint8 else np.asarray(t)
        if t8.shape[0] <= 64 and t8.shape[1] <= 64:
            continue                      # rampas nativas de la plantilla, no son del personaje
        h, w = t8.shape[:2]
        # la textura de B3 es la de la referencia reducida: se compara con cada referencia reducida al mismo tamano
        mejor = (-1, None)
        for nm, r in ref.items():
            rr = np.asarray(Image.fromarray(r[..., :3]).resize((w, h), Image.LANCZOS))
            v = ssim(rr, t8[..., :3], channel_axis=2, data_range=255)
            if v > mejor[0]:
                mejor = (v, nm)
        res.append((i, w, h, mejor[1], mejor[0]))
        print("textura %2d %4dx%-4d <- %-14s SSIM %.4f" % (i, w, h, mejor[1], mejor[0]))
    tot = sum(w * h for _, w, h, _, _ in res)
    print("SSIM medio ponderado por pixeles: %.4f (%d texturas)" % (sum(w * h * v for _, w, h, _, v in res) / tot, len(res)))


if __name__ == "__main__":
    main()
