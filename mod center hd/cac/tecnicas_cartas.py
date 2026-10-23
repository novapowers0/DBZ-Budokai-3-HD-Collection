#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tecnicas_cartas.py - cartas de tecnica del inventario del Heroe, generadas AUTOMATICAMENTE.

Para cada capsula de ataque (#SKC clase 0x21, un solo duenyo) arma una carta al estilo de las tarjetas de B3 con
piezas del propio juego del jugador (nada se distribuye):

  * nombre         data_usi 2663[ID] (rotulo blanco con borde, el de los menus de capsulas)
  * duenyo         data_usi 2079+ID textura 1 (el nombre del luchador "dueno" que escribe el propio juego)
  * descripcion    data_usi 2079+ID texturas 2..4 (que hace, botones, coste de ki, requisitos)
  * coste de ki    #SKC +15 (decimas de barra) dibujado como orbes
  * rareza         #SKC +10 (nibble alto) = color del borde
  * cara del dueno data_cmn HUD (entrada que da la tabla de fichas del juego, pasada con --caras ID:entrada,...);
                   sin cara (luchador de un mod): circulo con la inicial.

No hay datos por personaje a mano: cualquier donante (nativo, port o importado) sale igual si el juego conoce su
capsula. Las capsulas de IDs >= 596 (mods) se leen de la carpeta mods/ si traen sus entradas (--mods); si no, se
omiten (E1 trae el catalogo completo).

Uso:  python tecnicas_cartas.py --juego D:\\DBZ3HD\\copia [--solo cap_6] [--caras 0:476,1:474] [--lista]
Salida: <juego>/ingame_ui/heroe_tecnicas/cap_<id>.png  y  catalogo.json (id, donante, ki, rareza, clase)
"""
import argparse
import json
import os
import struct
import sys
import shutil
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
for p in (HERE, os.path.join(HERE, "..")):
    if os.path.isdir(p) and p not in sys.path:
        sys.path.append(p)

import numpy as np                 # noqa: E402
from PIL import Image, ImageDraw, ImageFont   # noqa: E402

import roster_build as rb          # noqa: E402
import capsulas as cp              # noqa: E402

ESC = 2                            # las texturas de B3 son pequenyas: la carta sale al doble
RAREZA = {0: (150, 150, 150), 1: (70, 140, 230), 2: (230, 180, 40), 3: (220, 90, 60)}
PERGAMINO = (232, 224, 190)
CABECERA = (28, 74, 88)


def duenyos(owner):
    """IDs de luchador (bit k) de la mascara de duenyos; vacio si es 'de todos' (>=3 bits o objetos comunes)."""
    bits = [k for k in range(64) if owner >> k & 1]
    return bits if 1 <= len(bits) <= 2 else []


def catalogo(skc):
    out = []
    for i, r in enumerate(cp.skc_records(skc)):
        if r[8] != 0x21:
            continue
        d = duenyos(cp.skc_owner(r))
        if d:
            out.append(dict(id=i, donante=d[0], ki=r[15], rareza=r[10] >> 4, clase=r[8]))
    return out


def recorte_logico(img, t):
    """La textura del juego va rellenada hasta potencia de 2; el dibujo mide `hd` (ancho, alto): se recorta a eso."""
    w, h = t["hd"]
    return img[:h, :w]


def textura(blob, k):
    t = rb.azt_textures(blob)
    return recorte_logico(rb.azt_read(blob, t[k]), t[k]) if k in t else None


def cara_png(afs_cmn, entrada):
    try:
        b = afs_cmn.entry(entrada)
        return Image.fromarray(textura(b, 0))
    except Exception:
        return None


ANCHO = 292                        # ancho logico de la carta (las tiras del juego miden hasta 272; mods pueden medir mas)


def carta(rec, nombre_img, panel, cara):
    cab_h = 52 * ESC
    partes = [(1, 24), (2, 44), (3, 84), (4, 44)]
    cuerpo = []
    for k, h in partes:
        a = textura(panel, k) if panel is not None else None
        if a is None or a.shape[0] < 5 or (k == 4 and a.shape[1] <= 16):
            continue
        cuerpo.append(Image.fromarray(a))
    # ancho = el mayor de las tiras de texto (medido, no supuesto) + margenes; nunca menos que ANCHO
    W = max(ANCHO, max([im.width for im in cuerpo] + [0]) + 20) * ESC
    H = cab_h + 8 * ESC + sum(c.height * ESC + 3 * ESC for c in cuerpo) + 10 * ESC
    c = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(c)
    col = RAREZA.get(rec["rareza"], RAREZA[0])
    d.rounded_rectangle((0, 0, c.width - 1, H - 1), 14, fill=PERGAMINO, outline=col, width=5)
    d.rounded_rectangle((5, 5, c.width - 6, cab_h), 10, fill=CABECERA)
    # cara del duenyo (circulo) a la izquierda de la cabecera
    cx, cy, rd = 12 * ESC + 16 * ESC, 5 + (cab_h - 5) // 2, 17 * ESC
    if cara is not None:
        w, h = cara.size
        recorte = cara.crop((w // 2 - 96, 4, w // 2 + 96, 4 + 120)).resize((rd * 2, int(rd * 2 * 120 / 192)), Image.LANCZOS)
        m = Image.new("L", recorte.size, 0)
        ImageDraw.Draw(m).ellipse((0, 0, m.width - 1, m.height - 1), fill=255)
        rec2 = recorte.convert("RGBA")
        rec2.putalpha(Image.composite(rec2.getchannel("A"), Image.new("L", m.size, 0), m))
        c.alpha_composite(rec2, (cx - rd, cy - m.height // 2))
    else:
        d.ellipse((cx - rd, cy - rd, cx + rd, cy + rd), fill=(60, 110, 125), outline=col, width=3)
        d.text((cx, cy), str(rec["donante"]), fill=(255, 255, 255), anchor="mm", font=ImageFont.load_default())
    # nombre: al doble, y si no cabe en la cabecera se reduce MIDIENDO su ancho real
    nx = cx + rd + 8 * ESC
    if nombre_img is not None:
        disp = c.width - nx - 12 * ESC
        n = nombre_img.resize((nombre_img.width * ESC, nombre_img.height * ESC), Image.NEAREST)
        if n.width > disp:
            n = n.resize((disp, max(1, int(n.height * disp / n.width))), Image.LANCZOS)
        c.alpha_composite(n, (nx, 8 * ESC))
    # orbes de ki (coste en decimas de barra)
    orbes = max(0, min(5, rec["ki"] // 10))
    for i in range(orbes):
        ox = nx + 6 * ESC + i * 10 * ESC
        d.ellipse((ox - 4 * ESC, cab_h - 15 * ESC, ox + 4 * ESC, cab_h - 7 * ESC), fill=(90, 200, 255), outline=(255, 255, 255))
    # texto del propio juego sobre pergamino
    y = cab_h + 8 * ESC
    for im in cuerpo:
        t = im.resize((im.width * ESC, im.height * ESC), Image.NEAREST)
        c.alpha_composite(t, (9 * ESC, y))
        y += t.height + 3 * ESC
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--juego", required=True)
    ap.add_argument("--us", default=None)
    ap.add_argument("--solo", default="")
    ap.add_argument("--caras", default="", help="ID:entrada de data_cmn,... (tabla de fichas del juego)")
    ap.add_argument("--lista", action="store_true")
    a = ap.parse_args()
    work = tempfile.mkdtemp(prefix="tec_cartas_", dir="D:/tmp" if os.path.isdir("D:/tmp") else None)   # no llenar el C:
    us = rb.us_dir(a.us)
    usi = rb.Afs(os.path.join(us, "data_usi.afs"), work)
    skc = usi.entry(4)
    cat = catalogo(skc)
    salida = os.path.join(a.juego, "ingame_ui", "heroe_tecnicas")
    os.makedirs(salida, exist_ok=True)
    with open(os.path.join(salida, "catalogo.json"), "w", encoding="utf8") as f:
        json.dump(cat, f)
    if a.lista:
        print(len(cat), "tecnicas")
        return 0
    caras = {}
    for par in a.caras.split(","):
        if ":" in par:
            k, v = par.split(":")
            caras.setdefault(int(k), int(v))   # la tabla repite el ID por traje: vale la primera fila (forma normal)
    solo = int(a.solo.split("_")[1]) if a.solo.startswith("cap_") else None
    nombres = usi.entry(cp.NAMES_SHORT)
    nt = rb.azt_textures(nombres)
    cmn = rb.Afs(os.path.join(us, "data_cmn.afs"), work)
    cache_caras = {}
    hechas = 0
    for rec in cat:
        if solo is not None and rec["id"] != solo:
            continue
        cid = rec["id"]
        dst = os.path.join(salida, "cap_%d.png" % cid)
        nom = Image.fromarray(recorte_logico(rb.azt_read(nombres, nt[cid]), nt[cid])) if cid in nt else None
        try:
            panel = usi.entry(2079 + cid)
        except Exception:
            panel = None
        d = rec["donante"]
        if d not in cache_caras:
            cache_caras[d] = cara_png(cmn, caras[d]) if d in caras else None
        carta(rec, nom, panel, cache_caras[d]).save(dst)
        hechas += 1
    if solo is None:
        with open(os.path.join(salida, "listo.txt"), "w", encoding="utf8") as f:
            f.write("%d\n" % hechas)
    print("cartas:", hechas, "->", salida)
    shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
