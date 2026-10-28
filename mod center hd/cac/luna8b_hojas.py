#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Hojas de revisión de peinados B3 injertados en la cabeza base del héroe.

Genera vistas de frente, perfil, espalda y perfil contrario. Solo produce
evidencia en --salida; no modifica el juego ni aprueba candidatos.
"""
import argparse
import os
import sys
import tempfile
from types import SimpleNamespace

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
for p in (HERE, ROOT):
    if p not in sys.path:
        sys.path.insert(0, p)

import numpy as np
from PIL import Image, ImageDraw

_us = next((sys.argv[i + 1] for i, x in enumerate(sys.argv[:-1]) if x == "--us"), None)
if _us:
    _game_root = os.path.dirname(os.path.abspath(_us))
    for _p in (_game_root, os.path.join(_game_root, "mod center hd"), os.path.join(_game_root, "awo_tools")):
        if os.path.isdir(_p) and _p not in sys.path:
            sys.path.insert(0, _p)

import model_render as mr
import roster_build as rb
import vista_heroe as vh


# FID del cuerpo donante en data_cmn.afs. Se retargetea el pelo por HEAD al
# Goku base (264), que es la cabeza anfitriona del héroe del juego.
CANDIDATOS = {
    "Bardock": 99,
    "Yamcha": 445,
    "Goten": 298,
    "Trunks": 390,
    "Trunks_nino": 383,
    "Gohan_adulto": 225,
    "Vegeta_SS": 416,
}


def malla_compuesta(base, pose, zones, donor, hair):
    dm, hair_tri_ids, donor_vertices, Q, QN, tris = hair
    P, N = base.posed(pose)
    nbase = len(P)
    keep = np.asarray(zones) != "pelo"
    tex_offset = len(base.tex)
    mats = list(base.mats)
    for original in dm.mats:
        m = dict(original)
        if m["tex"] < len(dm.tex):
            m["tex"] += tex_offset
        if m["shader"] != 0xFFFFFFFF and m["shader"] < len(dm.tex):
            m["shader"] += tex_offset
        mats.append(m)
    hair_tris = np.asarray(tris, int) + nbase
    return SimpleNamespace(
        tris=np.concatenate([base.tris[keep], hair_tris]),
        tri_mat=np.concatenate([base.tri_mat[keep], dm.tri_mat[np.asarray(hair_tri_ids, int)] + len(base.mats)]),
        mats=mats,
        tex=list(base.tex) + list(dm.tex),
        uv=np.concatenate([base.uv, dm.uv[np.asarray(donor_vertices, int)]]),
        posed=lambda _pose=None: (np.concatenate([P, Q]), np.concatenate([N, QN])),
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--juego", required=True)
    ap.add_argument("--us", default=None)
    ap.add_argument("--salida", default=r"D:\tmp\cac_fotos\luna8b")
    ap.add_argument("--size", type=int, default=520)
    a = ap.parse_args()
    os.makedirs(a.salida, exist_ok=True)
    afs = rb.Afs(os.path.join(rb.us_dir(a.us), "data_cmn.afs"), tempfile.mkdtemp(prefix="luna8b_"))
    base = mr.Model(afs.entry(264))
    pose = vh.pose_de(base)
    P, _ = base.posed(pose)
    zones = vh.zonas_ficha(base, P)
    angles = (("frente", 0), ("perfil", 90), ("espalda", 180), ("perfil_2", 270))
    for name, fid in CANDIDATOS.items():
        donor_bytes = afs.entry(fid)
        hair = vh._pelo_donante(base, P, zones, donor_bytes)
        if hair is None:
            print(f"{name}: no se pudo injertar (sin geometría compatible)", flush=True)
            continue
        donor = hair[0]
        comp = malla_compuesta(base, pose, zones, donor, hair)
        used = np.unique(comp.tris.reshape(-1))
        all_pos, _ = comp.posed()
        lo, hi = all_pos[used].min(0), all_pos[used].max(0)
        center = (lo + hi) / 2
        scale = min(a.size * 0.88 / max(hi[1] - lo[1], 1e-6),
                    a.size * 0.90 / max(np.linalg.norm([hi[0] - lo[0], hi[2] - lo[2]]), 1e-6))
        sheet = Image.new("RGB", (a.size * 4, a.size), "white")
        for i, (view_name, yaw) in enumerate(angles):
            view = mr.rot_x(6.0) @ mr.rot_y(yaw)
            image = mr.render(comp, (a.size, a.size), view, center, scale, outline=1.3, ss=2)
            tile = Image.fromarray(image).convert("RGBA")
            bg = Image.new("RGBA", tile.size, "white")
            bg.alpha_composite(tile)
            ImageDraw.Draw(bg).text((8, 8), view_name, fill="black")
            sheet.paste(bg.convert("RGB"), (i * a.size, 0))
        path = os.path.join(a.salida, f"{name}.png")
        sheet.save(path)
        print(f"{name}: fid={fid}, tris_pelo={len(hair[1])}, -> {path}", flush=True)


if __name__ == "__main__":
    main()
