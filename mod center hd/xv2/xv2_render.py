#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xv2_render.py - capturas emparejadas (nuestro modelo B3 frente a la referencia de XV2) con el render cel-shaded del Kit.
  python xv2_render.py <bin> <salida_prefijo> [--ref-cara ref.png] [--ref-cuerpo ref.png]
Genera <prefijo>_cara.png y <prefijo>_cuerpo.png (pose neutra: brazos abajo) y, si hay referencias, <prefijo>_par_*.png."""
import argparse
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "..", "..", "awo_tools"))
import model_render as MR  # noqa: E402


def sobre_fondo(im, fondo=(255, 255, 255)):
    bg = Image.new("RGBA", im.size, fondo + (255,))
    bg.alpha_composite(im)
    return bg.convert("RGB")


def render_cara(m, size=700, zoom=1.0, dy=0.0):
    lo, hi, _ = MR.head_info(m)
    c = (lo + hi) / 2
    sc = size * 0.9 / max(hi[1] - lo[1], hi[0] - lo[0], 1e-6) * zoom
    c = c + np.array([0.0, dy, 0.0])
    return Image.fromarray(MR.render(m, (size, size), np.eye(3), c, sc, outline=1.0))


def render_cuerpo(m, size=1000, pose=True):
    pz = m.arms_down(68.0) if pose else None
    P, _ = m.posed(pz)
    lo, hi = P.min(0), P.max(0)
    ctr = (lo + hi) / 2
    sc = size * 0.9 / max(hi[1] - lo[1], hi[0] - lo[0], 1e-6)
    return Image.fromarray(MR.render(m, (size, size), np.eye(3), ctr, sc, pose=pz, outline=1.2))


def par(ours, ref, alto=800):
    a = sobre_fondo(ours.convert("RGBA")) if ours.mode == "RGBA" else ours
    w = int(a.width * alto / a.height)
    r = Image.open(ref).convert("RGB")
    rw = int(r.width * alto / r.height)
    out = Image.new("RGB", (w + rw, alto), (255, 255, 255))
    out.paste(r.resize((rw, alto), Image.LANCZOS), (0, 0))
    out.paste(a.resize((w, alto), Image.LANCZOS), (rw, 0))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bin")
    ap.add_argument("salida")
    ap.add_argument("--ref-cara")
    ap.add_argument("--ref-cuerpo")
    a = ap.parse_args()
    m = MR.Model(a.bin)
    cara, cuerpo = render_cara(m), render_cuerpo(m)
    sobre_fondo(cara).save(a.salida + "_cara.png")
    sobre_fondo(cuerpo).save(a.salida + "_cuerpo.png")
    if a.ref_cara:
        par(cara, a.ref_cara).save(a.salida + "_par_cara.png")
    if a.ref_cuerpo:
        par(cuerpo, a.ref_cuerpo).save(a.salida + "_par_cuerpo.png")


if __name__ == "__main__":
    main()
