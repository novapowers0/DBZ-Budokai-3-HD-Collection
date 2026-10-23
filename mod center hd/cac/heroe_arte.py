#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""heroe_arte.py - capa visual PROPIA del Modo Heroe, derivada de piezas del propio Budokai 3 del jugador.

Las pantallas del Heroe (selector de 3 heroes, tecnicas...) no llevan arte propio ni copiado: se recolorean (giro de tono)
y se recomponen en marcha las piezas nativas de los menus (data_usi del jugador), de modo que no chocan con el resto del
juego pero tienen su identidad. Nada de esto se distribuye: se genera en <juego>/ingame_ui/heroe_arte/ al abrir la pantalla.

Piezas (data_usi entrada:textura, medidas en la copia US):
  anillo   2041:1  aro luminoso del selector de personajes (cian)  -> dorado
  esfera   2041:2  esfera de la ficha (azul)                       -> dorada
  agua     2039:17 franja azul del texto elegido                   -> esmeralda
Uso: python heroe_arte.py --juego D:\\DBZ3HD\\copia
"""
import argparse
import colorsys
import os
import sys
import shutil
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
for p in (HERE, os.path.join(HERE, "..")):
    if os.path.isdir(p) and p not in sys.path:
        sys.path.append(p)

import numpy as np                 # noqa: E402
from PIL import Image              # noqa: E402

import roster_build as rb          # noqa: E402

PIEZAS = {
    "anillo": (2041, 1, 0.115, 1.05, 1.0),     # (entrada, textura, tono destino 0..1, saturacion x, brillo x)
    "esfera": (2041, 2, 0.115, 0.95, 1.0),
    "agua": (2039, 17, 0.40, 1.0, 0.95),
}


def girar_tono(rgba, tono, sat, val):
    """Lleva el tono medio de los pixeles con color al tono destino (conserva luces y sombras)."""
    a = rgba.astype(np.float32) / 255.0
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    mx, mn = a[..., :3].max(-1), a[..., :3].min(-1)
    d = mx - mn
    s = np.where(mx > 0, d / np.maximum(mx, 1e-6), 0)
    h = np.zeros_like(mx)
    m = d > 1e-6
    rc = np.where(m, (mx - r) / np.maximum(d, 1e-6), 0)
    gc = np.where(m, (mx - g) / np.maximum(d, 1e-6), 0)
    bc = np.where(m, (mx - b) / np.maximum(d, 1e-6), 0)
    h = np.where(mx == r, bc - gc, np.where(mx == g, 2.0 + rc - bc, 4.0 + gc - rc))
    h = (h / 6.0) % 1.0
    w = (s > 0.15) & (al > 0.05)
    medio = float(np.angle(np.sum(np.exp(2j * np.pi * h[w])))) / (2 * np.pi) % 1.0 if w.any() else 0.0
    h2 = np.where(s > 0.08, (h - medio + tono) % 1.0, h)
    s2 = np.clip(s * sat, 0, 1)
    v2 = np.clip(mx * val, 0, 1)
    i = np.floor(h2 * 6).astype(int) % 6
    f = h2 * 6 - np.floor(h2 * 6)
    p = v2 * (1 - s2)
    q = v2 * (1 - f * s2)
    t = v2 * (1 - (1 - f) * s2)
    ro = np.choose(i, [v2, q, p, p, t, v2])
    go = np.choose(i, [t, v2, v2, q, p, p])
    bo = np.choose(i, [p, p, t, v2, v2, q])
    out = np.stack([ro, go, bo, al], -1)
    return (np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--juego", required=True)
    ap.add_argument("--us", default=None)
    a = ap.parse_args()
    work = tempfile.mkdtemp(prefix="heroe_arte_", dir="D:/tmp" if os.path.isdir("D:/tmp") else None)   # no llenar el C:
    usi = rb.Afs(os.path.join(rb.us_dir(a.us), "data_usi.afs"), work)
    salida = os.path.join(a.juego, "ingame_ui", "heroe_arte")
    os.makedirs(salida, exist_ok=True)
    cache = {}
    for nombre, (ent, tex, tono, sat, val) in PIEZAS.items():
        if ent not in cache:
            b = usi.entry(ent)
            cache[ent] = (b, rb.azt_textures(b))
        b, t = cache[ent]
        img = rb.azt_read(b, t[tex])
        Image.fromarray(girar_tono(img, tono, sat, val)).save(os.path.join(salida, nombre + ".png"))
    with open(os.path.join(salida, "listo.txt"), "w", encoding="utf8") as f:
        f.write("%d\n" % len(PIEZAS))
    print("arte del heroe:", len(PIEZAS), "piezas ->", salida)
    shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
