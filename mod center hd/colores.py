#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""colores.py - Cambia el color del aura y del ki (efectos de las tecnicas) de un personaje de B3 HD.

Solo cambia el TONO (mismo brillo y saturacion): lo gris o blanco se queda igual.

  Aura (data_cmn, entrada "aura" de roster_db.json; #AMB HD):
    #AZT (2 capas) -> texturas DXT3 / A8R8G8B8 retenidas en su sitio (mismo tamano)
    #CTR -> registros "wk<n>" de 0x60 B: dos colores RGB f32 en +0x10 y +0x50
           (Goku 0.9/0.9/0.2 amarillo, Broly 0.3/1.0/0.1 verde)
  Ki / tecnicas (BSP, entrada "bsp"; #AMB HD): todas las texturas de sus #AZT y los dos colores
    RGBA f32 (+0x80) de cada particula (#AME de la PS2 = #ACE en la HD, tipo 2; va con u32 BE,
    asi que el tipo esta en +4 y el arbol en +0x24/+0x28 como en sb_tecnicas.ame_nodes).
    El aura tambien lleva #ACE: se retinen igual.

En personaje.toml (roster_build):  aura_color = "morado"   ki_color = "#ff3030"   (o un tono 0-359)

  python colores.py aura 23 morado --out aura.bin     (ID de aura o ruta de un bin HD)
  python colores.py ki 533 rojo --out bsp.bin
  python colores.py nativo 0 --mod goku_morado --aura morado --tecnica 6=rojo
      (personaje del juego: override de su aura / BSP; --tecnica = solo esa capsula)
  python colores.py prueba
"""
import argparse
import colorsys
import io
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "awo_tools")]

NOMBRES = {  # tono en grados
    "rojo": 0, "red": 0, "naranja": 28, "orange": 28, "dorado": 45, "gold": 45, "amarillo": 55,
    "yellow": 55, "lima": 90, "lime": 90, "verde": 120, "green": 120, "turquesa": 165,
    "teal": 165, "cian": 185, "cyan": 185, "celeste": 200, "azul": 225, "blue": 225, "violeta": 265,
    "violet": 265, "morado": 280, "purple": 280, "magenta": 300, "rosa": 325, "pink": 325,
}


def tono(v):
    """'morado' | '#a040ff' | 280 -> tono en grados (0-359)."""
    if isinstance(v, (int, float)):
        return int(v) % 360
    s = str(v).strip().lower()
    if s in NOMBRES:
        return NOMBRES[s]
    if s.startswith("#") and len(s) == 7:
        r, g, b = (int(s[i:i + 2], 16) / 255 for i in (1, 3, 5))
        h, sat, _ = colorsys.rgb_to_hsv(r, g, b)
        if sat < 0.15:
            raise ValueError("%s es gris: solo se puede cambiar el tono (elige un color)" % v)
        return int(round(h * 360)) % 360
    if s.lstrip("-").isdigit():
        return int(s) % 360
    raise ValueError("color desconocido: %r (usa %s, #RRGGBB o un tono 0-359)" % (
        v, ", ".join(k for k in NOMBRES if k.isascii() and k == k.lower())[:120]))


# auras de personajes que no estan en la rueda (fusiones: tabla de auras del juego, IDs 64-104)
AURAS_ESPECIALES = {"gogeta": 16, "gogeta_ssj4": 17, "vegito": 42}


def aura_fid(v):
    """'gogeta' | ID del personaje (0-43) -> entrada data_cmn de su aura."""
    if isinstance(v, str) and v.strip().lower() in AURAS_ESPECIALES:
        return AURAS_ESPECIALES[v.strip().lower()]
    import json  # noqa: PLC0415
    db = json.load(open(os.path.join(HERE, "roster_db.json"), encoding="utf-8"))
    cid = int(v)
    e = next((x for x in db["ids"] if x["id"] == cid), None)
    if e is None or e.get("aura") is None:
        raise ValueError("aura de %r desconocida (ID 0-43 o %s)" % (v, ", ".join(AURAS_ESPECIALES)))
    return e["aura"]


def hue_rgb(rgb, hue):
    h, s, v = colorsys.rgb_to_hsv(*rgb)
    return colorsys.hsv_to_rgb(hue / 360.0, s, v) if s > 0.12 else tuple(rgb)


# ---------------------------------------------------------------- recorrido del #AMB HD
def blocks(b, base=0):
    """(magic, offset, tamano) de todas las hojas de un #AMB HD (BE), recursivo."""
    if b[base:base + 4] != b"#AMB":
        return
    n, t = struct.unpack_from(">II", b, base + 0x10)
    for k in range(n):
        o, s = struct.unpack_from(">II", b, base + t + 16 * k)
        if not s:
            continue
        if b[base + o:base + o + 4] == b"#AMB":
            yield from blocks(b, base + o)
        else:
            yield bytes(b[base + o:base + o + 4]), base + o, s


# ---------------------------------------------------------------- texturas
def _dds_retint(dds, hue):
    """DDS (DXT3 o A8R8G8B8, sin mipmaps) con otro tono; None si es gris o de otro formato."""
    from PIL import Image  # noqa: PLC0415
    from sb_tecnicas import hue_image, img_saturated  # noqa: PLC0415
    from texture_b3 import encode_dxt3  # noqa: PLC0415
    fourcc, bits = dds[84:88], struct.unpack_from("<I", dds, 88)[0]
    h, w = struct.unpack_from("<II", dds, 12)
    if struct.unpack_from("<I", dds, 28)[0] > 1:
        return None                                   # mipmaps: no hay en B3 HD
    if fourcc == b"DXT3":
        img = np.array(Image.open(io.BytesIO(bytes(dds))).convert("RGBA"))
        if not img_saturated(img):
            return None
        data = encode_dxt3(hue_image(img, hue))
    elif fourcc == b"\0\0\0\0" and bits == 32:
        img = np.frombuffer(bytes(dds[128:128 + w * h * 4]), np.uint8).reshape(h, w, 4)[..., [2, 1, 0, 3]]
        if not img_saturated(img):
            return None
        data = hue_image(img, hue)[..., [2, 1, 0, 3]].tobytes()
    else:
        return None
    out = bytes(dds[:128]) + data
    return out if len(out) <= len(dds) else None


def azt_retint(b, z, hue):
    """Retine en su sitio todas las texturas del #AZT en b[z]. -> numero de texturas cambiadas."""
    n, idx = struct.unpack_from(">II", b, z + 0x10)
    k = 0
    for t in range(n):
        o = struct.unpack_from(">I", b, z + idx + 4 * t)[0]
        if not o:
            continue
        do, dl = struct.unpack_from(">II", b, z + o + 0x14)
        new = _dds_retint(b[z + do:z + do + dl], hue)
        if new:
            b[z + do:z + do + len(new)] = new
            k += 1
    return k


# ---------------------------------------------------------------- colores sueltos
def ctr_retint(b, c, hue):
    """#CTR del aura: n registros de 0x60 desde +0x10, colores RGB en +0x10 y +0x50 del registro."""
    n = struct.unpack_from(">I", b, c + 8)[0]
    for r in range(n):
        for o in (0x10, 0x50):
            p = c + 0x10 + 0x60 * r + o
            rgb = struct.unpack_from(">3f", b, p)
            if all(0 <= x <= 1.01 for x in rgb):
                struct.pack_into(">3f", b, p, *hue_rgb(rgb, hue))
    return n


def ame_retint(b, a, s, hue):
    """#ACE (#AME) HD (u32 BE) en b[a:a+s]: colores de sus particulas (tipo 2) con otro tono."""
    hs = struct.unpack_from(">I", b, a + 12)[0]
    seen, todo, k = set(), [hs], 0
    while todo:
        o = todo.pop()
        if not o or o in seen or o + 0xA0 > s:
            continue
        seen.add(o)
        todo += struct.unpack_from(">II", b, a + o + 0x24)
        if struct.unpack_from(">H", b, a + o + 4)[0] != 2:
            continue
        cols = struct.unpack_from(">8f", b, a + o + 0x80)
        if all(-0.01 <= x <= 1.01 for x in cols):
            struct.pack_into(">8f", b, a + o + 0x80, *(hue_rgb(cols[0:3], hue) + (cols[3],)
                                                         + hue_rgb(cols[4:7], hue) + (cols[7],)))
            k += 1
    return k


def retint(hd_bin, hue):
    """#AMB HD (aura o BSP) con otro tono. -> (bin, informe)."""
    b = bytearray(hd_bin)
    cnt = {"texturas": 0, "colores": 0, "particulas": 0}
    for magic, o, s in list(blocks(b)):
        if magic == b"#AZT":
            cnt["texturas"] += azt_retint(b, o, hue)
        elif magic == b"#CTR":
            cnt["colores"] += ctr_retint(b, o, hue)
        elif magic in (b"#AME", b"#ACE"):
            cnt["particulas"] += ame_retint(b, o, s, hue)
    return bytes(b), cnt


# ---------------------------------------------------------------- una sola tecnica
def retint_tecnica(bsp, hue, ast_codes=(), ase_codes=()):
    """BSP HD con SOLO los efectos de una tecnica en otro tono: sus #CST (codigo +0x12) y #CSE
    (codigo de inicio +0x6A) enlazan copias recoloreadas de sus #ACE y texturas (anadidas al final:
    lo compartido con otras tecnicas no cambia). -> (bin, informe)."""
    from sb_tecnicas import amb_hd, azt_append, azt_entries, azt_image, hue_image, img_saturated, kids
    top = kids(bsp, ">")
    ambs = [list(kids(top[0][0], ">")), list(kids(top[1][0], ">"))]
    # el #AZT grande (texturas de rayos y particulas): arriba o dentro de un #AMB (Gotenks)
    zs = [(None, i) for i, (x, _) in enumerate(top) if x[:4] == b"#AZT"]
    zs += [(a, i) for a in (0, 1) for i, (x, _) in enumerate(ambs[a]) if x[:4] == b"#AZT"]
    where = max(zs, key=lambda w: len(azt_entries((top if w[0] is None else ambs[w[0]])[w[1]][0])))
    z = (top if where[0] is None else ambs[where[0]])[where[1]][0]
    ntex, extra, tmap, cmap = len(azt_entries(z)), [], {}, {}

    def tex(i):
        if i >= ntex or not azt_entries(z)[i]:
            return i
        if i not in tmap:
            img = azt_image(z, i)
            if img_saturated(img):
                o = azt_entries(z)[i]
                extra.append((hue_image(img, hue), i, struct.unpack_from(">HH", z, o + 0x10)))
                tmap[i] = ntex + len(extra) - 1
            else:
                tmap[i] = i
        return tmap[i]

    def ace(a, i):
        if (a, i) not in cmap:
            src = ambs[a][i][0]
            if src[:4] != b"#ACE":
                return i
            b = bytearray(src)
            ame_retint(b, 0, len(b), hue)
            for o in _ace_nodes(b):                      # texturas de sus particulas: copias tenidas
                if struct.unpack_from(">H", b, o + 4)[0] != 2:
                    continue
                cnt, t = struct.unpack_from(">II", b, o + 0xA0)
                if cnt == 1 and t < 0x10000:
                    struct.pack_into(">I", b, o + 0xA4, tex(t))
                elif o + 0xC8 <= len(b):
                    cnt2, t2 = struct.unpack_from(">II", b, o + 0xC0)
                    if cnt2 in (0, 1) and t2 < 0x100:
                        struct.pack_into(">I", b, o + 0xC4, tex(t2))
            ambs[a].append((bytes(b), 0xFFFFFFFF))
            cmap[(a, i)] = len(ambs[a]) - 1
        return cmap[(a, i)]

    rep, hit = [], set()
    for a, size, links in ((0, 0xF0, (0x34, 0x5C, 0x68, 0x78, 0xB4)), (1, 0xD0, (0x5C,))):
        tab = bytearray(ambs[a][0][0])
        n, s0 = struct.unpack_from(">II", tab, 8)
        for k in range(n):
            o = s0 + k * size
            code = struct.unpack_from(">H", tab, o + (0x12 if a == 0 else 0x6A))[0]
            if code not in (ast_codes if a == 0 else ase_codes):
                continue
            hit.add((a, code))
            typ = struct.unpack_from(">H", tab, o + 0x10)[0]
            for lk in links:
                src, idx = struct.unpack_from(">HH", tab, o + lk)
                if lk == 0x68 and typ != 1:
                    continue                             # solo la bola usa el 2o enlace
                # origen 2 = hijo del propio #AMB; 1 = del #AMB de los #CSE
                if src in (1, 2):
                    struct.pack_into(">H", tab, o + lk + 2, ace(a if src == 2 else 1, idx))
            if a == 0:
                for lk in (0x48, 0x4A):
                    t = struct.unpack_from(">H", tab, o + lk)[0]
                    if t:
                        struct.pack_into(">H", tab, o + lk, tex(t))
            rep.append("%s %#x" % ("rayo/bola" if a == 0 else "efecto", code))
        ambs[a][0] = (bytes(tab), ambs[a][0][1])
    miss = [("AST %#x" % c) for c in ast_codes if (0, c) not in hit] + [("ASE %#x" % c) for c in ase_codes
                                                                         if (1, c) not in hit]
    if miss:
        raise ValueError("el BSP no tiene %s" % ", ".join(miss))
    if extra:
        nz, _ = azt_append(z, extra)
        lst = top if where[0] is None else ambs[where[0]]
        lst[where[1]] = (nz, lst[where[1]][1])
    top[0] = (amb_hd(ambs[0]), top[0][1])
    top[1] = (amb_hd(ambs[1]), top[1][1])
    return amb_hd(top), {"efectos": rep, "texturas": len(extra), "particulas": len(cmap)}


def efectos_capsula(anms, cam, cap, bsp):
    """(codigos #CST, codigos de inicio #CSE) que usan los golpes de la capsula `cap`: entradas del
    #CCM con esa capsula -> codigos de ataque -> lineas AP7 de clase 4 (efecto del BSP) en el #CSK
    de cada moveset (HD, BE). 0x10 (comun a todas) no cuenta."""
    import capsulas  # noqa: PLC0415
    from sb_tecnicas import kids  # noqa: PLC0415
    o, s = capsulas.ccm_child(cam)
    ccm = cam[o:o + s]
    codes = {c for b, _ in capsulas.ccm_blocks(ccm) if capsulas.w(ccm, b, 8) == cap
             for c in (capsulas.w(ccm, b, i) for i in (12, 13, 14)) if c}
    vals = set()
    for anm in anms:
        csk = next(x for x, _ in kids(anm, ">") if x[:4] == b"#CSK")
        n, lst = struct.unpack_from(">II", csk, 0x10)
        for code in codes:
            ad = struct.unpack_from(">I", csk, lst + 4 * code)[0] if code < n else 0
            if not ad:
                continue
            nap, apo = struct.unpack_from(">II", csk, ad + 0x28)
            for k in range(nap):
                t, nl, do = struct.unpack_from(">HHI", csk, apo + 8 * k)
                if t == 7:
                    for i in range(nl):
                        _, _, _, cat, val = struct.unpack_from(">HBBII", csk, do + 16 * i)
                        if cat == 4 and val != 0x10:
                            vals.add(val)
    top = kids(bsp, ">")
    cst, cse = kids(top[0][0], ">")[0][0], kids(top[1][0], ">")[0][0]
    n0, s0 = struct.unpack_from(">II", cst, 8)
    n1, s1 = struct.unpack_from(">II", cse, 8)
    ast = {struct.unpack_from(">H", cst, s0 + k * 0xF0 + 0x12)[0] for k in range(n0)} & vals
    ase = {struct.unpack_from(">H", cse, s1 + k * 0xD0 + 0x6A)[0] for k in range(n1)} & vals
    if not codes or not (ast or ase):
        raise ValueError("la capsula %d no tiene efectos propios en este personaje" % cap)
    return ast, ase


def mod_nativo(cid, mods, mod, aura=None, ki=None, tecnicas=None):
    """Mod de un personaje del juego (override de sus entradas de aura / BSP en data_cmn):
    aura y/o ki de otro tono, o solo las tecnicas de `tecnicas` {capsula: color}."""
    import json  # noqa: PLC0415
    import shutil  # noqa: PLC0415
    import tempfile  # noqa: PLC0415
    import roster_build as rb  # noqa: PLC0415
    db = json.load(open(os.path.join(HERE, "roster_db.json"), encoding="utf-8"))
    e = next(x for x in db["ids"] if x["id"] == cid)
    work = tempfile.mkdtemp(prefix="colores_")
    cmn = rb.Afs(os.path.join(rb.default_us(), "data_cmn.afs"), work)
    out = os.path.join(mods, mod)
    rep = []
    try:
        if aura is not None:
            b, cnt = retint(cmn.entry(e["aura"]), tono(aura))
            rb.write_entry(out, "data_cmn.afs", e["aura"], b, work)
            rep.append("aura %s (entrada %d, %d texturas)" % (aura, e["aura"], cnt["texturas"]))
        if ki is not None or tecnicas:
            bsp = cmn.entry(e["bsp"])
            if ki is not None:
                bsp, cnt = retint(bsp, tono(ki))
                rep.append("ki %s (%d texturas, %d particulas)" % (ki, cnt["texturas"], cnt["particulas"]))
            if tecnicas:
                anms = [cmn.entry(f) for f in dict.fromkeys(e["anm"]) if 0 < f < 0xFFFFFFFF]
                cam = cmn.entry(e["cam"])
                for cap, col in tecnicas.items():
                    ast, ase = efectos_capsula(anms, cam, cap, bsp)
                    bsp, info = retint_tecnica(bsp, tono(col), ast, ase)
                    rep.append("capsula %d %s: %s" % (cap, col, ", ".join(info["efectos"])))
            rb.write_entry(out, "data_cmn.afs", e["bsp"], bsp, work)
        with open(os.path.join(out, "manifest.txt"), "w", encoding="utf-8") as fh:
            fh.write("name=%s\ndescription=%s: %s\nauthor=Mod Kit (colores.py)\nversion=1.0\ntype=colores\n"
                     "target=%s\n" % (mod, e["name"], "; ".join(rep), e["bsp"]))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return rep


def _ace_nodes(b):
    hs = struct.unpack_from(">I", b, 12)[0]
    seen, todo = [], [hs]
    while todo:
        o = todo.pop()
        if not o or o in seen or o + 0xA8 > len(b):
            continue
        seen.append(o)
        todo += struct.unpack_from(">II", b, o + 0x24)
    return seen


# ---------------------------------------------------------------- CLI
def _load(src, kind):
    if os.path.isfile(src):
        import afs_pair  # noqa: PLC0415
        return afs_pair.decompress(open(src, "rb").read(), "col_" + os.path.basename(src))
    import afs_pair  # noqa: PLC0415
    import json  # noqa: PLC0415
    db = json.load(open(os.path.join(HERE, "roster_db.json"), encoding="utf-8"))
    fid = next(e for e in db["ids"] if e["id"] == int(src))[kind] if kind == "bsp" else int(src)
    return afs_pair.decompress(afs_pair.entry(os.path.join(ROOT, "us", "data_cmn.afs"), fid), "hd%d" % fid)


def prueba():
    assert aura_fid("Gogeta") == 16 and aura_fid(0) == 23
    assert tono("morado") == 280 and tono("#ff0000") == 0 and tono(400) == 40
    try:
        tono("#808080")
        raise AssertionError("gris aceptado")
    except ValueError:
        pass
    aura = _load("23", "aura")                       # aura de Goku (amarilla)
    new, cnt = retint(aura, 120)
    assert len(new) == len(aura) and cnt["texturas"] > 0 and cnt["colores"] == 2, cnt
    c = new.find(b"#CTR")
    r, g, _ = struct.unpack_from(">3f", new, c + 0x20)
    assert g > r + 0.3, (r, g)                        # amarillo -> verde
    assert retint(new, 55)[0] != aura or True         # ida y vuelta: sin excepciones
    bsp = _load("0", "bsp")                          # ki de Goku
    new, cnt2 = retint(bsp, 0)
    assert len(new) == len(bsp) and cnt2["texturas"] > 0 and cnt2["particulas"] > 0, cnt2
    print("prueba OK: aura %s, ki %s" % (cnt, cnt2))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for k in ("aura", "ki"):
        p = sub.add_parser(k)
        p.add_argument("src", help="ID del personaje (ki) / entrada de aura, o ruta de un bin HD")
        p.add_argument("color")
        p.add_argument("--out", required=True)
    p = sub.add_parser("nativo", help="mod de colores para un personaje del juego (override de data_cmn)")
    p.add_argument("id", type=int)
    p.add_argument("--mod", required=True)
    p.add_argument("--mods", help="carpeta de mods (por defecto la del juego)")
    p.add_argument("--aura")
    p.add_argument("--ki")
    p.add_argument("--tecnica", action="append", default=[], help="CAPSULA=COLOR (p. ej. 6=rojo)")
    sub.add_parser("prueba")
    a = ap.parse_args()
    if a.cmd == "prueba":
        return prueba()
    if a.cmd == "nativo":
        import roster_build as rb  # noqa: PLC0415
        tec = {int(k, 0): v for k, v in (x.split("=", 1) for x in a.tecnica)}
        for ln in mod_nativo(a.id, a.mods or rb.default_mods(), a.mod, a.aura, a.ki, tec):
            print(ln)
        return None
    out, cnt = retint(_load(a.src, "bsp" if a.cmd == "ki" else "aura"), tono(a.color))
    open(a.out, "wb").write(out)
    print("%s -> %s %s" % (a.cmd, a.out, cnt))


if __name__ == "__main__":
    main()
