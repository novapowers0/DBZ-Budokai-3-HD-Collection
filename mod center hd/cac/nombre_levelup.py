#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""nombre_levelup.py - rotulo con el nombre del Heroe para la pantalla de subida de nivel de Dragon Universe (C4).

La pantalla nativa (data_usi 1974 = level_up, #AZT de 65 texturas) dibuja el nombre de la casilla como la textura 27+casilla
(36 = 63 = 'Saibamen'; textura 256x128 BGRA, logica 168x48). Para las casillas del Heroe (35-37) se anaden tres texturas
nuevas (65, 66, 67 = casilla + 30) con su nombre en el mismo estilo (azul con perfil negro y aro claro) en un mod propio
(`mods/heroe_nombres`); el gancho de src/campaigns.cpp (sub_82096FF0) cambia el id durante la historia del Heroe.
  python nombre_levelup.py --mods D:\\DBZ3HD\\copia\\mods --us <carpeta us> 35=Kakaroto 36=Mirai 37=Hero
"""
import argparse
import os
import struct
import sys
import tempfile

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import capsulas  # noqa: E402
import roster_build as rb  # noqa: E402

ENTRADA = 1974
PLANTILLA = 63            # 'Saibamen' (casilla 36)
CASILLAS = (35, 36, 37)
LANGS = ("data_usi.afs", "data_eng.afs", "data_spn.afs", "data_fra.afs", "data_ger.afs", "data_ita.afs")


def id_nuevo(casilla):
    return casilla + 30   # 65, 66, 67


def fuente(tam):
    from PIL import ImageFont  # noqa: PLC0415
    for n in ("arialbd.ttf", "verdanab.ttf", "segoeuib.ttf"):
        try:
            return ImageFont.truetype(os.path.join("C:/Windows/Fonts", n), tam)
        except OSError:
            continue
    return ImageFont.load_default()


def render(texto, size=(256, 128)):
    """Texto azul degradado (0,158,239 -> 0,104,208), perfil negro y aro claro, arriba a la izquierda como el nativo."""
    W, H = size
    tam = 46
    while tam > 18:
        f = fuente(tam)
        if f.getlength(texto) <= 212:
            break
        tam -= 2
    f = fuente(tam)
    x0, y0 = 22, 14
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).text((x0, y0), texto, font=f, fill=255)
    aro = mask.filter(ImageFilter.MaxFilter(11))
    negro = mask.filter(ImageFilter.MaxFilter(7))
    grad = np.zeros((H, W, 3), np.uint8)
    t = np.clip((np.arange(H) - y0) / max(1, tam), 0, 1)[:, None]
    grad[..., 0] = 0
    grad[..., 1] = (158 + (104 - 158) * t).astype(np.uint8)
    grad[..., 2] = (239 + (208 - 239) * t).astype(np.uint8)
    out = np.zeros((H, W, 4), np.uint8)
    out[..., :3] = (225, 232, 245)
    out[..., 3] = np.array(aro)
    nm = np.array(negro) > 0
    out[nm, :3] = (0, 0, 0)
    fm = np.array(mask) > 100
    out[fm, :3] = grad[fm]
    return Image.fromarray(out)


def plantilla(b):
    """(entrada de 48 B, cabecera DDS de 128 B) de la textura 63 del data_usi (ingles): el formato nativo."""
    plt = capsulas.azt_entries(b)[PLANTILLA]
    ent0 = bytes(b[plt:plt + 48])
    do0, _ = struct.unpack(">II", ent0[0x14:0x1C])
    return ent0, bytes(b[do0:do0 + 128])


def extender(b, imgs, tpl):
    """#AZT con las texturas nuevas {id: PIL RGBA 256x128}: copia la entrada de la textura 63 (formato nativo)."""
    offs = capsulas.azt_entries(b)
    ent0, cab = tpl
    base = struct.unpack(">I", b[0x18:0x1C])[0]
    n_new = max(len(offs), max(imgs) - base + 1)
    offs = offs + [0] * (n_new - len(offs))
    out = bytearray(b)
    out += bytes((-len(out)) % 0x80)
    last = max(offs)
    vram = struct.unpack(">I", b[last + 0x24:last + 0x28])[0] + struct.unpack(">I", b[last + 0x18:last + 0x1C])[0] // 0x20
    ents = []
    for cid in sorted(imgs):
        a = np.array(imgs[cid].convert("RGBA").resize((256, 128)))
        blob = cab + np.ascontiguousarray(a[..., [2, 1, 0, 3]]).tobytes()
        do = len(out)
        out += blob + bytes((-len(blob)) % 0x80)
        e = bytearray(ent0)
        struct.pack_into(">I", e, 0, cid)
        struct.pack_into(">II", e, 0x14, do, len(blob))
        struct.pack_into(">I", e, 0x24, vram)
        vram += len(blob) // 0x20
        ents.append((cid, bytes(e)))
    out += bytes((-len(out)) % 0x10)
    for cid, e in ents:
        offs[cid - base] = len(out)
        out += e
    new_idx = len(out)
    out += struct.pack(">%dI" % n_new, *offs)
    out += bytes((-len(out)) % 0x10)
    struct.pack_into(">II", out, 0x10, n_new, new_idx)
    return bytes(out)


def montar(mods, us, nombres):
    """nombres {casilla: texto}. Escribe mods/heroe_nombres (1974 de cada idioma con las 3 texturas)."""
    mod = os.path.join(mods, "heroe_nombres")
    os.makedirs(mod, exist_ok=True)
    import json  # noqa: PLC0415
    fj = os.path.join(mod, "nombres.json")
    try:
        previos = {int(k): v for k, v in json.load(open(fj, encoding="utf-8")).items()}
    except (OSError, ValueError):
        previos = {}
    antes = dict(previos)
    previos.update({int(k): v for k, v in nombres.items()})
    for c in CASILLAS:
        previos.setdefault(c, "Hero")
    if previos == antes and os.path.isdir(os.path.join(mod, "us")):   # ya al dia
        return 0
    work = tempfile.mkdtemp(prefix="nombre_lv_")
    imgs = {id_nuevo(c): render(previos[c]) for c in CASILLAS}
    n = 0
    tpl = plantilla(rb.Afs(os.path.join(us, "data_usi.afs"), work).entry(ENTRADA))
    for afs_name in LANGS:
        p = os.path.join(us, afs_name)
        if not os.path.exists(p):
            continue
        b = rb.Afs(p, work).entry(ENTRADA)
        if b[:4] != b"#AZT":
            continue
        rb.write_entry(mod, afs_name, ENTRADA, extender(b, imgs, tpl), work)
        n += 1
    with open(fj, "w", encoding="utf-8") as f:
        json.dump(previos, f, ensure_ascii=False)
    print("nombre_levelup: %d idiomas, nombres %s" % (n, previos))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mods", required=True)
    ap.add_argument("--us", default=None)
    ap.add_argument("--png", help="solo guarda el rotulo de este texto en ese .png")
    ap.add_argument("pares", nargs="*", help="casilla=Nombre")
    a = ap.parse_args()
    if a.png:
        render(" ".join(a.pares)).save(a.png)
        return 0
    nombres = {int(p.split("=")[0]): p.split("=", 1)[1] for p in a.pares}
    return montar(a.mods, rb.us_dir(a.us), nombres)


if __name__ == "__main__":
    sys.exit(main())
