#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""capsulas.py - Capsulas (habilidades) de Budokai 3 HD para los personajes nuevos.

Como funcionan en el juego (RE 2026-10-04, ver docs/03_formatos/CAPSULAS_B3.md):

* Catalogo: data_usi 4 = #SKC (PS2: #SKA), cabecera 0x20 (+0x10 n = 596) + n registros
  de 40 B (BE en la HD). El juego lo tiene residente en 0x824A60E8 y lo usa a traves del
  puntero 0x8237560C (registros); el runtime (roster_ext) copia el catalogo, le anade las
  capsulas nuevas (IDs >= 596) y mueve ese puntero.
    +0  u64 duenos: bit k = ID de personaje k (0..63; los objetos comunes = bits 0..43)
    +8  u8 clase (0x11 transformacion/habilidad, 0x21 ataque, 0x17 fusion/despertar...)
    +10 u8 rareza (nibble alto 0..3) | +14 u8 mascara de formas | +15 u8 coste
    +16 u16 capsula requerida (p.ej. SSJ2 pide SSJ; +18 = 0) | +20.. efecto | +38 u16 precio/100
  Transformaciones: +14 = formas desde las que se puede usar (SSJ 0x01, SSJ2 0x03...) y
  +15 = ki EXIGIDO en decimas de barra (40 = 4 barras; no se gasta).
* Lista por defecto ("Original") de cada personaje: char96 +80 (u16 n) +82 (7 x u16 IDs).
* Transformaciones: char372 +212 + 20*forma, +4 u16 = capsula que exige esa forma.
* Ataques: el bloque del #CCM (BCM) de cada golpe lleva la capsula en w8 (u16 en +16).
* Nombres en combate: data_usi[tabla 0x82373D68 (u16 por ID)] = #AZT con las texturas de
  nombre del personaje; la cabecera +0x18 es el primer ID y cada entrada lleva su ID.
  Los menus usan 2663 (nombres cortos) y 2684 (largos), indexados por ID de capsula.

Ports de Infinite World: su BCM usa otra mecanica (sin modo hiper; "aura burst" con el
boton B, especiales con capsula en 2 ranuras, definitivo con ^E). adapt_iw_bcm() lo pasa
a la de Budokai 3: modo hiper (LT/L2) -> Dragon Rush y definitivos, especiales ligados a
sus capsulas.
"""
import io
import os
import struct
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFont

SKC_ENTRY = 4            # data_usi: catalogo #SKC
NAMES_SHORT = 2663       # data_usi: nombres cortos por ID (menus)
NAMES_LONG = 2684        # data_usi: nombres largos por ID
N_NATIVE = 596           # registros del catalogo original
REC = 40
NAME_FONT = "C:/Windows/Fonts/ARLRDBD.TTF"
# Not every PC has every font (Arial Rounded comes with Office): fall back instead of failing
# the whole build with "OSError: cannot open resource".
# Shipped with the Kit (Apache 2.0), so a missing system font can never stop a build.
BUNDLED_FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts", "Roboto-Bold.ttf")
FONT_FALLBACKS = ("ARLRDBD.TTF", "arialbd.ttf", "comicbd.ttf", "arial.ttf", "segoeuib.ttf",
                  "DejaVuSans-Bold.ttf", "DejaVuSans.ttf")


def load_font(path, size):
    """ImageFont.truetype(path) or the first font that exists; Pillow's default as last resort."""
    for p in (path, BUNDLED_FONT) + FONT_FALLBACKS:
        for cand in (p, os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts", os.path.basename(p))):
            try:
                return ImageFont.truetype(cand, size)
            except OSError:
                pass
    try:
        return ImageFont.load_default(size)
    except TypeError:          # Pillow < 10.1
        return ImageFont.load_default()
NAME_H = 28              # alto logico de los nombres (las texturas son de 32)

KINDS = ("transformacion", "especial", "definitiva")


# ---------------------------------------------------------------- catalogo #SKC
def skc_records(skc):
    n, start = struct.unpack(">II", skc[0x10:0x18])
    return [bytes(skc[start + REC * i:start + REC * (i + 1)]) for i in range(n)]


def skc_owner(rec):
    return struct.unpack(">Q", rec[:8])[0]


def make_record(template, owner_ids, requires=0):
    """Registro nuevo a partir de uno nativo de la misma clase."""
    r = bytearray(template)
    mask = 0
    for i in owner_ids:
        mask |= 1 << i
    struct.pack_into(">Q", r, 0, mask)
    # capsula exigida: u16 en +16 y +18 = 0, como las nativas (SSJ2 de Gohan: 0x0017 0000)
    struct.pack_into(">HH", r, 16, requires, 0)
    r[28:36] = bytes(8)          # sin "se convierte en" (X10 Kamehameha, etc.)
    return bytes(r)


def first_owned(recs, cid):
    """Primer ID de capsula con el bit del personaje (= base de su banco de nombres)."""
    for i, r in enumerate(recs):
        m = skc_owner(r)
        if m & (1 << cid) and m != (1 << 64) - 1 and bin(m).count("1") < 40:
            return i
    return None


def owned(recs, cid):
    """Capsulas propias del personaje (solo su bit; sin los objetos comunes)."""
    return [i for i, r in enumerate(recs) if skc_owner(r) == (1 << cid)]


# ---------------------------------------------------------------- #CCM (BCM HD)
def ccm_child(amb):
    """(offset, tamano) del #CCM dentro del bin de camara (#AMB HD)."""
    n, tbl = struct.unpack(">II", amb[0x10:0x18])
    for k in range(n):
        off, size = struct.unpack(">II", amb[tbl + 16 * k:tbl + 16 * k + 8])
        if size and amb[off:off + 4] == b"#CCM":
            return off, size
    return None


def ccm_blocks(ccm):
    """[(offset, es_starter)] de todos los bloques de 0x40 B alcanzables."""
    n = struct.unpack(">H", ccm[0x1E:0x20])[0]
    starters = [struct.unpack(">I", ccm[0x50 + 4 * k:0x54 + 4 * k])[0] for k in range(n)]
    seen, out, todo = set(), [], [(o, True) for o in starters]
    while todo:
        o, st = todo.pop(0)
        if o in seen or not (0x50 <= o <= len(ccm) - 0x40):
            continue
        seen.add(o)
        out.append((o, st))
        nb = struct.unpack(">H", ccm[o + 0x0E:o + 0x10])[0]
        for k in range(min(nb, 64)):
            q = o + 0x40 + 4 * k
            if q + 4 <= len(ccm):
                todo.append((struct.unpack(">I", ccm[q:q + 4])[0], False))
    return out


def w(ccm, o, i):
    return struct.unpack(">H", ccm[o + 2 * i:o + 2 * i + 2])[0]


def setw(ccm, o, i, v):
    struct.pack_into(">H", ccm, o + 2 * i, v & 0xFFFF)


# Bloque de golpe del #CCM (64 B, u16 BE salvo +8 que es u32: en la HD su mitad alta es la
# condicion y la baja el tipo de especial; en el #BCM de la PS2 van al reves):
#   w0 direccion | w1 botones (1 P, 2 K, 4 G, 8 E) | w2-w3 ventana | w4 tipo de especial (IW:
#   ranura 1/2, +0x10 = tras combo) | w5 condicion (0x0400 modo hiper, 0x0004 transformar,
#   0x0008 definitivo, 0x0002 especial con capsula (B3), 0x8000 idem (IW), 0x4000 aura IW,
#   0x0001 cuesta ki) | w6 condicion 2 | w7 ramas | w8 capsula | w9 ki | w10 formas |
#   w12-w15 codigos de ataque (#CSK)
TYPE, COND = 4, 5


def bcm_summary(ccm):
    """Lo que importa para las capsulas: modo hiper, especiales, definitivos, transformar."""
    out = {"hiper": False, "aura_iw": False, "especiales": [], "definitivos": [], "transforma": False}
    for o, st in ccm_blocks(ccm):
        c1, cap = w(ccm, o, COND), w(ccm, o, 8)
        if st and c1 & 0x0400:
            out["hiper"] = True
        if st and c1 & 0x4000:
            out["aura_iw"] = True
        if st and c1 & 0x0004:
            out["transforma"] = True
        if c1 & 0x0008 and (cap or c1 & 0x8000):
            out["definitivos"].append(cap)
        elif cap and c1 & 0x8002:
            out["especiales"].append(cap)
    for k in ("especiales", "definitivos"):
        out[k] = list(dict.fromkeys(out[k]))
    return out


UNUSED_CAP = 595         # registro vacio del catalogo: nadie la lleva -> el golpe no sale
KI = {}                  # capsula -> barras de ki (del BCM de IW), para el texto de la ficha


def adapt_iw_bcm(ccm, special_ids, ultimate_ids):
    """BCM de Infinite World -> mecanica de Budokai 3 (in place). special_ids: IDs de capsula
    B3 para la 1a, 2a... capsula especial del BCM; ultimate_ids: para los definitivos.
    Devuelve lineas de informe."""
    rep = []
    KI.clear()
    blocks = ccm_blocks(ccm)
    iw_specials = []
    for o, _ in blocks:
        c1, cap = w(ccm, o, COND), w(ccm, o, 8)
        if c1 & 0x8000 and cap and not c1 & 0x0008 and cap not in iw_specials:
            iw_specials.append(cap)
    for o, st in blocks:
        c1, cap = w(ccm, o, COND), w(ccm, o, 8)
        # aura burst de IW (boton B) -> activar el modo hiper de B3 (LT / L2)
        if st and c1 & 0x4000 and w(ccm, o, 1) == 0x20:
            setw(ccm, o, 0, 0)
            setw(ccm, o, 1, 0x0F)
            setw(ccm, o, COND, 0x0400)
            setw(ccm, o, 6, 0x0001)
            rep.append("modo hiper: entrada de 'aura burst' de IW convertida (LT/L2)")
            continue
        # definitivo (^E en IW, sin capsula) -> P+K+G+E en modo hiper con su capsula
        if c1 & 0x0008 and c1 & 0x8000:
            k = len([1 for r in rep if r.startswith("definitivo")])
            cid = ultimate_ids[min(k, len(ultimate_ids) - 1)] if ultimate_ids else 0
            setw(ccm, o, 0, 0)
            setw(ccm, o, 1, 0x0F)
            setw(ccm, o, COND, 0x000A)
            setw(ccm, o, TYPE, 0)
            setw(ccm, o, 6, 0x8001)
            setw(ccm, o, 8, cid)
            KI[cid] = max(KI.get(cid, 0), (w(ccm, o, 9) + 500) // 1000)
            setw(ccm, o, 9, 0)
            setw(ccm, o, 11, 0)
            rep.append("definitivo -> capsula %d" % cid)
            continue
        # especiales: en IW la direccion elige la ranura (1 = >E, 2 = <E) y cada capsula
        # tiene entradas para las dos; en B3 cada capsula tiene su direccion fija.
        if c1 & 0x8000 and cap in iw_specials:
            k = iw_specials.index(cap)
            slot = w(ccm, o, TYPE) & 0x0F
            cid = special_ids[k] if k < len(special_ids) else 0
            setw(ccm, o, COND, (c1 & ~0x8001) | 0x0002)
            setw(ccm, o, TYPE, 0)
            setw(ccm, o, 11, 0)
            setw(ccm, o, 8, cid if (slot == k + 1 or slot == 0) and cid else UNUSED_CAP)
            if cid:
                KI[cid] = max(KI.get(cid, 0), (w(ccm, o, 9) + 500) // 1000)
    for k, cap in enumerate(iw_specials):
        rep.append("especial %d (IW %#x) -> capsula %s" % (k + 1, cap, special_ids[k] if k < len(special_ids) else "-"))
    if hyper_first(ccm):
        rep.append("modo hiper: su entrada pasa delante de los definitivos (mismos botones)")
    return rep


def hyper_first(ccm):
    """El modo hiper y los definitivos usan los mismos botones (P+K+G+E) y el juego se queda
    con la primera entrada de la lista: en todos los personajes de B3 la del modo hiper va
    antes. Mueve su entrada delante de la primera de definitivo. Devuelve si cambio algo."""
    n = struct.unpack(">H", ccm[0x1E:0x20])[0]
    st = [struct.unpack(">I", ccm[0x50 + 4 * k:0x54 + 4 * k])[0] for k in range(n)]
    hyp = [k for k, o in enumerate(st) if w(ccm, o, COND) & 0x0400]
    ult = [k for k, o in enumerate(st) if w(ccm, o, COND) & 0x0008 and w(ccm, o, 1) == w(ccm, st[hyp[0]], 1)] if hyp else []
    if not hyp or not ult or hyp[0] < ult[0]:
        return False
    st.insert(ult[0], st.pop(hyp[0]))
    for k, o in enumerate(st):
        struct.pack_into(">I", ccm, 0x50 + 4 * k, o)
    return True


def remap_caps(ccm, mapping):
    """Cambia IDs de capsula en todos los golpes (p.ej. las del donante por las propias)."""
    n = 0
    for o, _ in ccm_blocks(ccm):
        cap = w(ccm, o, 8)
        if cap in mapping:
            setw(ccm, o, 8, mapping[cap])
            n += 1
    return n


def ccm_parse(ccm):
    """#CCM -> (starters [offset], {offset: [bloque 64 B, [offsets hijos]]})."""
    n = struct.unpack(">H", ccm[0x1E:0x20])[0]
    starters = [struct.unpack_from(">I", ccm, 0x50 + 4 * k)[0] for k in range(n)]
    blocks = {}
    todo = list(starters)
    while todo:
        o = todo.pop()
        if o in blocks or not (0x50 <= o <= len(ccm) - 0x40):
            continue
        nb = min(w(ccm, o, 7), 64)
        kids = [struct.unpack_from(">I", ccm, o + 0x40 + 4 * k)[0] for k in range(nb)]
        blocks[o] = [bytearray(ccm[o:o + 0x40]), kids]
        todo += kids
    return starters, blocks


def ccm_build(head, starters, nodes):
    """#CCM nuevo (como los nativos: +4 = bloques + 1, sin relleno). nodes = {clave: [bloque
    64 B, [claves hijas]]}, starters = [claves]; los bloques sueltos se descartan."""
    order, seen = [], set()

    def visit(k):
        if k in seen:
            return
        seen.add(k)
        order.append(k)
        for ch in nodes[k][1]:
            visit(ch)
    for s in starters:
        visit(s)
    at, pos = {}, 0x50 + 4 * len(starters)
    for k in order:
        at[k] = pos
        pos += 0x40 + 4 * len(nodes[k][1])
    out = bytearray(head[:0x50])
    struct.pack_into(">I", out, 0x04, len(order) + 1)
    struct.pack_into(">H", out, 0x1E, len(starters))
    for s in starters:
        out += struct.pack(">I", at[s])
    for k in order:
        blk = bytearray(nodes[k][0])
        setw(blk, 0, 7, len(nodes[k][1]))
        out += blk
        for ch in nodes[k][1]:
            out += struct.pack(">I", at[ch])
    return bytes(out)


TRANSFORM_CODES = (0x2E0, 0x3E0)      # animaciones de la transformacion de B3 (suelo, aire)


def is_transform_entry(blk):
    """Entrada de transformarse: la de B3 (P+K+G, condicion 0x0004) o la de Shin Budokai
    (abajo+E: w0 0x20 con condicion 0x0020 o codigo 0x500..0x517, coste 4000)."""
    w0, w1, cond, code = (w(blk, 0, i) for i in (0, 1, COND, 12))
    if w1 == 0x0007 and cond & 0x0004:
        return True
    return bool(w0 & 0x30) and bool(cond & 0x0020 or 0x500 <= code <= 0x517)


def has_transform(cam):
    """El bin de camara tiene la entrada P+K+G de transformarse de B3."""
    at = ccm_child(cam)
    if not at:
        return False
    ccm = cam[at[0]:at[0] + at[1]]
    st, bl = ccm_parse(ccm)
    return any(w(bl[o][0], 0, 1) == 0x0007 and is_transform_entry(bl[o][0]) for o in st if o in bl)


def add_b3_transform(ccm, donor_ccm):
    """Pone en el #CCM la entrada P+K+G del donante (transformarse sin gastar ki: el requisito
    lo pone la capsula) en lugar de las que haya de transformarse (la de Shin Budokai gastaba
    4 barras sin cambiar de forma). Va delante del modo hiper, como en los nativos.
    -> (#CCM nuevo, informe)."""
    st, bl = ccm_parse(ccm)
    dst, dbl = ccm_parse(donor_ccm)
    src = [o for o in dst if o in dbl and w(dbl[o][0], 0, 1) == 0x0007 and is_transform_entry(dbl[o][0])]
    if not src:
        raise ValueError("el donante no tiene entrada de transformacion (P+K+G)")
    nodes = {("p", o): [blk, [("p", k) for k in kids]] for o, (blk, kids) in bl.items()}

    def copy(o):
        if ("d", o) not in nodes:
            nodes[("d", o)] = [bytearray(dbl[o][0]), []]
            nodes[("d", o)][1] = [copy(k) for k in dbl[o][1]]
        return ("d", o)
    st = [o for o in st if o in bl]
    old = [o for o in st if is_transform_entry(bl[o][0])]
    starters = [("p", o) for o in st if o not in old]
    hyp = next((i for i, k in enumerate(starters) if w(nodes[k][0], 0, COND) & 0x0400), len(starters))
    starters.insert(hyp, copy(src[0]))
    rep = ["entrada P+K+G del donante (codigos %s)" % "/".join(
        "%#x" % w(dbl[src[0]][0], 0, i) for i in (12, 13))]
    for o in old:
        rep.append("quitada la entrada de transformarse del moveset (w0 %#x, botones %#x, ki %d, "
                   "codigo %#x)" % tuple(w(bl[o][0], 0, i) for i in (0, 1, 9, 12)))
    return ccm_build(ccm, starters, nodes), rep


def add_combo_specials(ccm, donor_ccm):
    """Tecnicas al final de un combo (B3: P,P,P,P y ->E lanza la especial): son hijas normales
    de un nodo de combo, E con direccion, condicion 0x0002 y la capsula de la especial. Los ports
    (Shin Budokai, IW) no las traen. Por cada rama combo -> E del donante se busca en el BCM
    propio el mismo combo (direcciones y botones; si no, solo botones) y se le cuelgan las
    especiales PROPIAS de esa direccion (las entradas de arranque ->E / <-E: su capsula, sus
    codigos y sus formas). -> (#CCM nuevo, informe)."""
    st, bl = ccm_parse(ccm)
    dst, dbl = ccm_parse(donor_ccm)

    def is_special(blk):
        return w(blk, 0, 1) == 8 and w(blk, 0, COND) & 0x0002 and w(blk, 0, 8)
    # especiales de arranque: capsula del donante -> direccion; direccion -> entradas propias
    dcap_dir = {}
    for o in dst:
        if o in dbl and is_special(dbl[o][0]):
            dcap_dir.setdefault(w(dbl[o][0], 0, 8), w(dbl[o][0], 0, 0))
    own = {}
    for o in st:
        if o in bl and is_special(bl[o][0]):
            own.setdefault(w(bl[o][0], 0, 0), []).append(bl[o][0])

    def paths(starters, blocks):
        out, seen = {}, set()

        def walk(o, path):
            if (o, path) in seen or len(path) > 8:
                return
            seen.add((o, path))
            blk, kids = blocks[o]
            p = path + ((w(blk, 0, 0), w(blk, 0, 1)),)
            out.setdefault(p, o)
            for k in kids:
                if k in blocks:
                    walk(k, p)
        for o in starters:
            if o in blocks:
                walk(o, ())
        return out
    dpaths, ppaths = paths(dst, dbl), paths(st, bl)
    loose = {}
    for p, o in ppaths.items():
        loose.setdefault(tuple(b for _, b in p), o)
    nodes = {o: [blk, list(kids)] for o, (blk, kids) in bl.items()}
    rep, added = [], 0
    for p, o in sorted(dpaths.items(), key=lambda x: len(x[0])):
        for k in dbl[o][1]:
            ch = dbl.get(k)
            if not ch or not is_special(ch[0]):
                continue
            dirn = w(ch[0], 0, 0)
            mine = own.get(dcap_dir.get(w(ch[0], 0, 8), dirn))
            target = ppaths.get(p) or loose.get(tuple(b for _, b in p))
            if not mine or target is None or w(nodes[target][0], 0, 1) == 8:
                continue
            have = {(w(nodes[x][0], 0, 0), w(nodes[x][0], 0, 8)) for x in nodes[target][1] if x in nodes}
            for src in mine:
                if (dirn, w(src, 0, 8)) in have:
                    continue
                blk = bytearray(ch[0])
                setw(blk, 0, 0, dirn)
                for i in (8, 10, 12, 13, 14, 15):        # capsula, formas y codigos: los propios
                    setw(blk, 0, i, w(src, 0, i))
                setw(blk, 0, 6, 0)
                setw(blk, 0, 9, 0)
                key = ("combo", target, dirn, w(src, 0, 8))
                nodes[key] = [blk, []]
                nodes[target][1].append(key)
                added += 1
            rep.append("%s -> %sE" % (">".join("PKGE"[(b & -b).bit_length() - 1] if b in (1, 2, 4, 8) else "%x" % b
                                                for _, b in p), {1: "->", 2: "<-"}.get(dirn, "")))
    if not added:
        return bytes(ccm), []
    return ccm_build(ccm, [o for o in st if o in bl], nodes), ["tecnicas tras combo (%d): %s" % (
        added, ", ".join(dict.fromkeys(rep)))]


# ---------------------------------------------------------------- nombres (#AZT)
def render_name(text, h=NAME_H, color=(255, 255, 255, 255)):
    """Nombre de capsula al estilo del juego (blanco con borde oscuro): mismo tamano y
    posicion que los nativos (calibrado con "Kamehameha" de los bancos 2682 / SCMKLL)."""
    f = load_font(NAME_FONT, 18)
    wid = int(f.getlength(text)) + 10
    out = Image.new("RGBA", (min(wid, 508), h), (0, 0, 0, 0))
    ImageDraw.Draw(out).text((3, h // 2 - (0 if h >= 28 else 1)), text, font=f, anchor="lm", fill=color,
                             stroke_width=2, stroke_fill=(30, 20, 20, 255))
    return np.array(out)


# ---------------------------------------------------------------- panel de descripcion
# data_usi 2079 + ID: #AZT de 5 texturas que el panel de "Edit Skills" pinta en columna:
# 0 nombre (como los rotulos), 1 quien la usa, 2 que hace, 3 botones y coste, 4 nota
# (16 x 24 vacia si no hay). Texto oscuro (18, 10, 0) sobre transparente, ~16 px, 19-20 px
# por linea, 6 px arriba, ajustado a 208 px (medido sobre Kamehameha, Spirit Bomb, LSSJ).
DESC_FONT = "C:/Windows/Fonts/arial.ttf"
DESC_W, DESC_PITCH, DESC_TOP = 208, 19, 6
DESC_COLOR = (18, 10, 0, 255)


def _wrap(text, f, width):
    out = []
    for para in str(text).split("\n"):
        line = ""
        for word in para.split(" "):
            cand = (line + " " + word).strip()
            if line and f.getlength(cand) > width:
                out.append(line)
                line = word
            else:
                line = cand
        out.append(line)
    return out


def render_lines(text):
    if not text:
        return np.zeros((24, 16, 4), np.uint8)
    f = load_font(DESC_FONT, 16)
    lines = _wrap(text, f, DESC_W)
    wid = min(DESC_W + 8, int(max(f.getlength(t) for t in lines)) + 6)
    h = DESC_TOP + DESC_PITCH * len(lines) + 5
    out = Image.new("RGBA", (max(16, wid), h), (0, 0, 0, 0))
    d = ImageDraw.Draw(out)
    for k, t in enumerate(lines):
        d.text((1, DESC_TOP + DESC_PITCH * k), t, font=f, fill=DESC_COLOR)
    return np.array(out)


# Textos generados de las capsulas nuevas por idioma del juego (data_usi/eng = en,
# spn = es, fra = fr, ger = de, ita = it). {n} = barras de ki; [s|p] = singular|plural.
TEXTS = {
    "en": {"ult": "Can launch Ultimate-move\n{name}", "trans": "Fight as\n{name}",
           "special": "Can launch Death-move\n{name}",
           "ult_cmd": "P+K+G+E attack hits\nopponent in Hyper Mode\n(Consumes {n} Ki [Gauge|Gauges])",
           "trans_cmd": "P+K+G\nWith {n} or more Ki Gauges", "special_cmd": "(Consumes {n} Ki [Gauge|Gauges])",
           "ki": "{n} Ki [gauge|gauges] consumed", "over": "With over {n} Ki gauges"},
    "es": {"ult": "Puede lanzar el ataque definitivo\n{name}", "trans": "Lucha como\n{name}",
           "special": "Puede lanzar el ataque mortal\n{name}",
           "ult_cmd": "El ataque P+K+G+E golpea\nal rival en Modo Híper\n(Gasta {n} [barra|barras] de Ki)",
           "trans_cmd": "P+K+G\nCon {n} o más barras de Ki", "special_cmd": "(Gasta {n} [barra|barras] de Ki)",
           "ki": "Gasta {n} [barra|barras] de Ki", "over": "Con más de {n} barras de Ki"},
    "fr": {"ult": "Peut lancer l'attaque ultime\n{name}", "trans": "Combat en tant que\n{name}",
           "special": "Peut lancer l'attaque mortelle\n{name}",
           "ult_cmd": "L'attaque P+K+G+E touche\nl'adversaire en mode Hyper\n(Consomme {n} [jauge|jauges] de Ki)",
           "trans_cmd": "P+K+G\nAvec {n} jauges de Ki ou plus", "special_cmd": "(Consomme {n} [jauge|jauges] de Ki)",
           "ki": "Consomme {n} [jauge|jauges] de Ki", "over": "Avec plus de {n} jauges de Ki"},
    "de": {"ult": "Kann den Ultimativen Angriff\n{name} einsetzen", "trans": "Kampf als\n{name}",
           "special": "Kann den Todesangriff\n{name} einsetzen",
           "ult_cmd": "Der Angriff P+K+G+E trifft\nden Gegner im Hyper-Modus\n(Verbraucht {n} [Ki-Leiste|Ki-Leisten])",
           "trans_cmd": "P+K+G\nAb {n} Ki-Leisten", "special_cmd": "(Verbraucht {n} [Ki-Leiste|Ki-Leisten])",
           "ki": "Verbraucht {n} [Ki-Leiste|Ki-Leisten]", "over": "Mit mehr als {n} Ki-Leisten"},
    "it": {"ult": "Può usare l'attacco finale\n{name}", "trans": "Combatti come\n{name}",
           "special": "Può usare l'attacco mortale\n{name}",
           "ult_cmd": "L'attacco P+K+G+E colpisce\nl'avversario in modalità Hyper\n(Consuma {n} [barra|barre] di Ki)",
           "trans_cmd": "P+K+G\nCon {n} o più barre di Ki", "special_cmd": "(Consuma {n} [barra|barre] di Ki)",
           "ki": "Consuma {n} [barra|barre] di Ki", "over": "Con più di {n} barre di Ki"},
}


def lang_of(afs_name):
    """Idioma de un data_*.afs: usi/eng/us/en -> en, spn -> es, fra -> fr, ger -> de, ita -> it."""
    n = str(afs_name).lower()
    for key, lang in (("spn", "es"), ("fra", "fr"), ("ger", "de"), ("ita", "it")):
        if key in n:
            return lang
    return "en"


def text(key, lang="en", n=0, name=""):
    import re  # noqa: PLC0415
    t = TEXTS.get(lang, TEXTS["en"])[key]
    t = re.sub(r"\[([^|\]]*)\|([^\]]*)\]", lambda m: m.group(1) if n == 1 else m.group(2), t)
    return t.replace("{n}", str(n)).replace("{name}", name)


def desc_texts(kind, name, who, ki, extra=None, lang="en"):
    """Textos por defecto (estilo de las nativas) de una capsula nueva, en el idioma del juego.
    Los textos que escribe el usuario (quien/descripcion/botones/nota) van tal cual."""
    extra = extra or {}
    k = {"definitiva": "ult", "transformacion": "trans"}.get(kind, "special")
    what = text(k, lang, ki, name)
    cmd = text(k + "_cmd", lang, ki, name)
    return (extra.get("quien") or who, extra.get("descripcion") or what,
            extra.get("botones") or cmd, extra.get("nota") or "")


def build_desc(name, who, what, cmd, note=""):
    """#AZT del panel de descripcion (data_usi 2079 + ID de las nativas)."""
    return build_bank(0, {0: render_name(name), 1: render_lines(who), 2: render_lines(what),
                          3: render_lines(cmd), 4: render_lines(note)}, tall=True)


def _pow2(v):
    p = 16
    while p < v:
        p *= 2
    return p


def _dds_dxt3(w_, h_):
    return (b"DDS \x7c\x00\x00\x00\x07\x10\x08\x00" + struct.pack("<IIIII", h_, w_, h_ * w_, 0, 0) +
            bytes(44) + b"\x20\x00\x00\x00\x04\x00\x00\x00" + b"DXT3" + bytes(20) +
            struct.pack("<I", 0x1000) + bytes(16))


def _tex_blob(rgba, tall=False):
    """DXT3 de un rotulo: alto 32 (nombres) o, con tall, el de las descripciones nativas
    (potencia de 2, minimo 64; las lineas de texto miden hasta ~84 px)."""
    from texture_b3 import encode_dxt3  # noqa: PLC0415
    lh, lw = rgba.shape[:2]
    W, H = _pow2(lw), (max(64, _pow2(lh)) if tall else 32)
    canvas = np.zeros((H, W, 4), np.uint8)
    canvas[:lh, :lw] = rgba[:H, :W]
    return _dds_dxt3(W, H) + encode_dxt3(canvas), W, (lw, lh)


def azt_entries(b):
    n, idx = struct.unpack(">II", b[0x10:0x18])
    return [struct.unpack(">I", b[idx + 4 * k:idx + 4 * k + 4])[0] for k in range(n)]


def azt_read(b, k):
    o = azt_entries(b)[k]
    if not o:
        return None
    do, ds = struct.unpack(">II", b[o + 0x14:o + 0x1C])
    lw, lh = struct.unpack(">HH", b[o + 0x10:o + 0x14])
    img = np.array(Image.open(io.BytesIO(bytes(b[do:do + ds]))).convert("RGBA"))
    return img[:lh, :lw]


def build_azt(base, items, flag28=0x400):
    """#AZT con las texturas items = {id: rgba}, indice disperso desde `base`."""
    return build_bank(base, items, flag28)


def build_bank(base, items, flag28=0x400, tall=False):
    """#AZT de nombres de un personaje: items = {id_capsula: rgba (alto 28)}. Indice disperso
    desde `base` (cabecera +0x18) hasta el ID mas alto."""
    ids = sorted(items)
    n = ids[-1] - base + 1
    idx = 0x20
    first = idx + 4 * n
    first = (first + 0xF) // 0x10 * 0x10
    data_base = (first + 0x30 * len(ids) + 0x7F) // 0x80 * 0x80
    index = bytearray(4 * n)
    ent, body = bytearray(), bytearray()
    vram = 0
    for cid in ids:
        blob, W, (lw, lh) = _tex_blob(items[cid], tall)
        H = max(64, _pow2(lh)) if tall else 64
        k = cid - base
        struct.pack_into(">I", index, 4 * k, first + len(ent))
        ent += struct.pack(">II", cid, 0x21) + struct.pack(">HHHH", lw, lh, (W - 1).bit_length(), (H - 1).bit_length())
        ent += struct.pack(">HH", lw, lh) + struct.pack(">II", data_base + len(body), len(blob))
        ent += struct.pack(">IIIII", 1, 0, 0, vram, flag28)
        vram += len(blob) // 0x20
        body += blob + bytes((-len(blob)) % 0x80)
    hdr = struct.pack(">4s7I", b"#AZT", 0, 0, 0, n, idx, base, 0)
    out = bytearray(hdr) + index
    out += bytes(first - len(out)) + ent
    out += bytes(data_base - len(out)) + body
    struct.pack_into(">I", out, 0x1C, zlib.crc32(bytes(body)))
    return bytes(out)


def extend_bank(b, items):
    """Anade texturas (id_capsula -> rgba) a un #AZT existente indexado por ID (base de la
    cabecera), alargando el indice disperso. No mueve los datos existentes."""
    base = struct.unpack(">I", b[0x18:0x1C])[0]
    offs = azt_entries(b)
    n_new = max(len(offs), max(items) - base + 1)
    offs = offs + [0] * (n_new - len(offs))
    out = bytearray(b)
    out += bytes((-len(out)) % 0x80)
    last = max(offs)
    vram = struct.unpack(">I", b[last + 0x24:last + 0x28])[0] + struct.unpack(">I", b[last + 0x18:last + 0x1C])[0] // 0x20
    ents = []
    for cid in sorted(items):
        blob, W, (lw, lh) = _tex_blob(items[cid])
        do = len(out)
        out += blob + bytes((-len(blob)) % 0x80)
        e = struct.pack(">II", cid, 0x21) + struct.pack(">HHHH", lw, lh, (W - 1).bit_length(), 6)
        e += struct.pack(">HH", lw, lh) + struct.pack(">II", do, len(blob))
        e += struct.pack(">IIIII", 1, 0, 0, vram, 0x400)
        vram += len(blob) // 0x20
        ents.append((cid, e))
    out += bytes((-len(out)) % 0x10)
    for cid, e in ents:
        offs[cid - base] = len(out)
        out += e
    new_idx = len(out)
    out += struct.pack(">%dI" % n_new, *offs)
    out += bytes((-len(out)) % 0x10)
    struct.pack_into(">II", out, 0x10, n_new, new_idx)
    return bytes(out)


# ---------------------------------------------------------------- #CSK (BSK HD)
# El modo hiper de B3 lo activa la animacion de su codigo de ataque: su bloque del #CSK
# lleva las propiedades (AP) que ponen el estado. El "aura burst" de IW tiene otras, asi
# que en un port se injerta el bloque hiper de un personaje de B3 (todos usan el mismo:
# animacion comun del banco 3 y las mismas AP).
def amb_children(b):
    n, tbl = struct.unpack(">II", b[0x10:0x18])
    return [list(struct.unpack(">4I", b[tbl + 16 * k:tbl + 16 * k + 16])) for k in range(n)]


def amb_rebuild(b, replace):
    """#AMB HD con los hijos `replace` {indice: bytes} sustituidos (hijos alineados a 32)."""
    kids = amb_children(b)
    n = len(kids)
    tbl = struct.unpack(">I", b[0x14:0x18])[0]
    start = (tbl + 16 * n + 0x1F) // 0x20 * 0x20
    out = bytearray(b[:start])
    for k, (off, size, typ, z) in enumerate(kids):
        data = replace.get(k, b[off:off + size]) if size else b""
        if size or k in replace:
            out += bytes((-len(out)) % 0x20)
            kids[k] = [len(out), len(data), typ, z]
            out += data
    out += bytes((-len(out)) % 0x20)
    for k, e in enumerate(kids):
        struct.pack_into(">4I", out, tbl + 16 * k, *e)
    return bytes(out)


def csk_child(amb):
    for k, (off, size, typ, _) in enumerate(amb_children(amb)):
        if size and amb[off:off + 4] == b"#CSK":
            return k, off, size
    return None


def csk_hr_block(csk, code):
    """Bloque HR (8 lineas x 16 B) `code` del #CSK (cabecera +0x18 n, +0x1C offset)."""
    nhr, ho = struct.unpack(">II", csk[0x18:0x20])
    return bytes(csk[ho + 128 * code:ho + 128 * (code + 1)]) if code < nhr else None


def csk_hr_add(out, blk):
    """Codigo HR de `blk` en el #CSK `out` (bytearray, se modifica): uno identico si ya esta;
    si no, se anade al final de la tabla (si la tabla no es lo ultimo del #CSK, se copia
    al final: los golpes la indexan por codigo, no por offset)."""
    nhr, ho = struct.unpack(">II", out[0x18:0x20])
    for c in range(nhr):
        if out[ho + 128 * c:ho + 128 * (c + 1)] == blk:
            return c
    if ho + 128 * nhr != len(out):
        out += bytes((-len(out)) % 0x10)
        table = bytes(out[ho:ho + 128 * nhr])
        ho = len(out)
        out += table
    out += blk
    struct.pack_into(">II", out, 0x18, nhr + 1, ho)
    return nhr


def csk_graft(dst, dst_code, src, src_code):
    """Copia el bloque de ataque src_code de otro #CSK (con sus AP) al final de dst y lo
    enlaza en dst_code. Los golpes (AP tipo 1, codigo HR en +4) se llevan su bloque HR
    (p.ej. el empujon de la transformacion: cada personaje lo tiene en otro codigo).
    Devuelve el #CSK nuevo."""
    n, lst = struct.unpack(">II", src[0x10:0x18])
    so = struct.unpack(">I", src[lst + 4 * src_code:lst + 4 * src_code + 4])[0]
    blk = bytearray(src[so:so + 48])
    nap, apo = struct.unpack(">II", blk[0x28:0x30])
    aps = [struct.unpack(">HHI", src[apo + 8 * a:apo + 8 * a + 8]) for a in range(nap)]
    out = bytearray(dst)
    lines = []
    for t, nl, do in aps:
        data = bytearray(src[do:do + 16 * nl])
        for i in range(nl if t == 1 else 0):
            hr = struct.unpack_from(">I", data, 16 * i + 4)[0]
            hb = csk_hr_block(src, hr) if hr != 0xFFFFFFFF else None
            if hb is not None:
                struct.pack_into(">I", data, 16 * i + 4, csk_hr_add(out, hb))
        lines.append((t, nl, data))
    out += bytes((-len(out)) % 0x10)
    new_lines = []
    for t, nl, data in lines:
        new_lines.append((t, nl, len(out)))
        out += data
    out += bytes((-len(out)) % 0x10)
    new_apo = len(out)
    for t, nl, do in new_lines:
        out += struct.pack(">HHI", t, nl, do)
    out += bytes((-len(out)) % 0x10)
    struct.pack_into(">II", blk, 0x28, nap, new_apo)
    new_blk = len(out)
    out += blk
    dn, dl = struct.unpack(">II", dst[0x10:0x18])
    if dst_code >= dn:
        # lista corta (port IW de Goku GT: 0x120 codigos): una lista mas larga al final, con
        # los mismos punteros y ceros (= sin golpe) hasta el codigo nuevo
        n2 = dst_code + 1
        old = bytes(out[dl:dl + 4 * dn])
        out += bytes((-len(out)) % 0x10)
        dl = len(out)
        out += old + bytes(4 * (n2 - dn))
        struct.pack_into(">II", out, 0x10, n2, dl)
    struct.pack_into(">I", out, dl + 4 * dst_code, new_blk)
    return bytes(out)


def hyper_codes(ccm):
    """Codigos de ataque (suelo, aire) de la entrada de modo hiper de un #CCM B3."""
    for o, st in ccm_blocks(ccm):
        if st and w(ccm, o, COND) & 0x0400:
            return w(ccm, o, 12), w(ccm, o, 13)
    return None


def hyper_check(ccm, anm_bins):
    """Por que no podria entrar en modo hiper o lanzar su definitiva: [texto] (vacio = bien).
    Entrada hiper (condicion 0x0400) y definitivas (0x0008) en el BCM, y sus codigos de
    ataque con bloque en el #CSK de la forma 1."""
    blocks = ccm_blocks(ccm)
    hyp = [o for o, st in blocks if st and w(ccm, o, COND) & 0x0400]
    ult = [o for o, _ in blocks if w(ccm, o, COND) & 0x0008]
    out = [] if hyp else ["sin entrada de modo hiper (P+K+G) en su BCM"]
    if not ult:
        out.append("sin definitiva en su BCM")
    # solo la forma 1: las demas (B3 e IW) cambian unos golpes sobre los suyos
    for k, anm in enumerate(anm_bins[:1]):
        cc = csk_child(anm)
        if not cc:
            out.append("forma %d: moveset sin #CSK" % (k + 1))
            continue
        csk = anm[cc[1]:cc[1] + cc[2]]
        n, lst = struct.unpack(">II", csk[0x10:0x18])
        for o in hyp[:1] + ult:
            for c in (w(ccm, o, 12), w(ccm, o, 13)):
                if c and (c >= n or not struct.unpack(">I", csk[lst + 4 * c:lst + 4 * c + 4])[0]):
                    out.append("forma %d: el codigo %#x del %s no tiene animacion" % (
                        k + 1, c, "modo hiper" if o in hyp else "definitivo"))
    return list(dict.fromkeys(out))


# ---------------------------------------------------------------- ficha de habilidades (SCM)
# data_usi 5..52 = "SCM<codigo>.amb": la lista de habilidades de la pausa y el rotulo de la
# capsula al usarla en combate. #AMB HD con un #AZT (por capsula, en el orden
# transformaciones + ataques: nombre y condicion/coste) y un #CFC (filas de 16 B: u32
# capsula (0xFFFFFFFF = transformarse), u8 variante, glifos de los botones). El juego la
# elige por personaje con la tabla 0x82324468 (ID, trajes, HUD, indice) y el indice apunta
# a un registro de 16 B (u32 fid, ptr ataques u16, ptr transformaciones u16, n, n).
def scm_parts(b):
    kids = amb_children(b)
    azt = cfc = None
    for off, size, typ, _ in kids:
        if b[off:off + 4] == b"#AZT":
            azt = b[off:off + size]
        elif b[off:off + 4] == b"#CFC":
            cfc = b[off:off + size]
    rows = []
    if cfc:
        n, start = struct.unpack(">II", cfc[4:12])
        rows = [cfc[start + 16 * i:start + 16 * (i + 1)] for i in range(n)]
    imgs = [azt_read(azt, k) for k in range(len(azt_entries(azt)))] if azt else []
    return imgs, rows


def build_scm(imgs, rows):
    """#AMB HD de ficha de habilidades: imgs = [rgba] (nombre, condicion, ...), rows = [16 B]."""
    azt = build_azt(0, {k: im for k, im in enumerate(imgs)}, 0x40)
    cfc = bytearray(b"#CFC" + struct.pack(">III", len(rows), 0x10, 0))
    for r in rows:
        cfc += r
    kids = [(azt, 2), (bytes(cfc), 0xFFFFFFFF)]
    hdr = bytearray(struct.pack(">4s7I", b"#AMB", 0x20, 0, 2, len(kids), 0x20, 0x60, 0))
    hdr += bytes(0x20 * 2)
    out = bytearray(hdr[:0x20]) + bytes(16 * len(kids))
    out += bytes((-len(out)) % 0x20)
    ents = []
    for data, typ in kids:
        out += bytes((-len(out)) % 0x20)
        ents.append((len(out), len(data), typ, 0))
        out += data
    out += bytes((-len(out)) % 0x20)
    for k, e in enumerate(ents):
        struct.pack_into(">4I", out, 0x20 + 16 * k, *e)
    struct.pack_into(">I", out, 0x18, ents[0][0])
    return bytes(out)


# Glifos de la ficha de habilidades por (direccion, botones) del BCM: censo de las 38 fichas
# nativas frente a las rutas de su BCM (2026-10-08). Fila de 16 B: u32 capsula, u8 numero de
# fila; en +5..+11 la secuencia (hasta 6 glifos) alineada a la derecha y, encima, alineada a
# la izquierda con 0x1B de cierre.
GLYPH = {(0, 1): 2, (0, 2): 3, (0, 8): 5, (0, 7): 0x15, (0, 0xF): 0x1A,
         (1, 1): 9, (1, 2): 10, (1, 8): 0x19, (2, 1): 6, (2, 2): 7, (2, 8): 0x18}


def skill_rows(cam, cap, limit=4):
    """Filas de la ficha para la capsula `cap` sacadas de las rutas del BCM que llegan a un golpe
    con esa capsula (combos primero, como las nativas). [] si ninguna ruta se puede dibujar."""
    at = ccm_child(cam)
    if not at:
        return []
    ccm = cam[at[0]:at[0] + at[1]]
    n = struct.unpack(">H", ccm[0x1E:0x20])[0]
    seqs = []

    def walk(o, pre, depth):
        if depth > 8 or not 0x50 <= o <= len(ccm) - 0x40:
            return
        g = GLYPH.get((w(ccm, o, 0), w(ccm, o, 1)))
        if g is None:
            return
        seq = pre + [g]
        if w(ccm, o, 8) == cap:           # la tecnica sale aqui: no se sigue la cadena
            if len(seq) <= 6 and seq not in seqs:
                seqs.append(seq)
            return
        for k in range(min(w(ccm, o, 7), 64)):
            walk(struct.unpack_from(">I", ccm, o + 0x40 + 4 * k)[0], seq, depth + 1)

    for k in range(n):
        walk(struct.unpack_from(">I", ccm, 0x50 + 4 * k)[0], [], 0)
    rows = []
    for i, s in enumerate(seqs[:limit]):
        r = bytearray(16)
        struct.pack_into(">IB", r, 0, cap, i)
        r[12 - len(s):12] = bytes(s)         # la secuencia alineada a la derecha en +5..+11 y
        r[5:5 + len(s)] = bytes(s)           # encima, a la izquierda, con su cierre (nativas)
        r[5 + len(s)] = 0x1B
        rows.append(bytes(r))
    return rows


def ki_text(n, lang="en"):
    return text("ki", lang, n)


# ---------------------------------------------------------------- autocomprobacion
def _selftest_texts():
    assert ki_text(1) == "1 Ki gauge consumed" and ki_text(3) == "3 Ki gauges consumed"
    assert ki_text(1, "es") == "Gasta 1 barra de Ki" and ki_text(2, "de") == "Verbraucht 2 Ki-Leisten"
    assert lang_of("data_spn.afs") == "es" and lang_of("data_usi.afs") == "en" and lang_of("data_ger.afs") == "de"
    w, d, c, _ = desc_texts("definitiva", "Cannon", "Zarbon", 4, None, "fr")
    assert d == "Peut lancer l'attaque ultime\nCannon" and "4 jauges" in c, (d, c)
    assert desc_texts("especial", "X", "Y", 1, {"descripcion": "mio"}, "it")[1] == "mio"


def _selftest():
    _selftest_texts()
    """python capsulas.py: P+K+G del donante en un #CCM y golpe injertado con su bloque HR."""
    def blk(*ws):
        b = bytearray(0x40)
        for i, v in enumerate(ws):
            setw(b, 0, i, v)
        return b
    head = bytearray(b"#CCM" + bytes(0x4C))
    normal = blk(0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0x200, 0x300)
    child = blk(0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0x201, 0x301)
    sb = blk(0x20, 8, 0, 0, 0, 0x20, 0x101, 0, 0, 4000, 0, 0, 0x500, 0x700)
    hyper = blk(0, 0xF, 0, 0, 0, 0x400, 1, 0, 0, 0, 0, 0, 0x259, 0x359)
    trans = blk(0, 7, 0, 0, 0, 4, 1, 0, 0, 0, 0, 0, 0x2E0, 0x3E0, 0x3E0)
    ccm = ccm_build(head, ["n", "s", "h"], {"n": [normal, ["c"]], "c": [child, []], "s": [sb, []], "h": [hyper, []]})
    donor = ccm_build(head, ["t"], {"t": [trans, []]})
    new, rep = add_b3_transform(ccm, donor)
    st, bl = ccm_parse(new)
    assert [w(bl[o][0], 0, 1) for o in st] == [1, 7, 0xF], rep        # SB fuera, P+K+G antes del hiper
    assert struct.unpack(">I", new[4:8])[0] == len(bl) + 1 == 5
    assert ccm_build(new, st, {o: [b, k] for o, (b, k) in bl.items()}) == new
    assert has_transform(b"#AMB" + bytes(12) + struct.pack(">II", 1, 0x20) + bytes(8)
                         + struct.pack(">II", 0x30, len(new)) + bytes(8) + new)

    def csk(n_codes, blocks, hrs, hr_last=True):
        """#CSK minimo: lista de codigos, bloques {codigo: [(tipo AP, [lineas 16 B])]} y HR."""
        out = bytearray(b"#CSK" + bytes(0x1C))
        lst = len(out)
        out += bytes(4 * n_codes)
        for code, aps in blocks.items():
            lines = []
            for t, ls in aps:
                lines.append((t, len(ls), len(out)))
                out += b"".join(ls)
            apo = len(out)
            for t, nl, do in lines:
                out += struct.pack(">HHI", t, nl, do)
            struct.pack_into(">I", out, lst + 4 * code, len(out))
            out += bytes(0x28) + struct.pack(">II", len(lines), apo)
        if not hr_last:
            out += bytes(16)                                           # algo detras de la tabla
        ho = len(out) - (0 if hr_last else 16)
        out[ho:ho] = b"".join(hrs)
        struct.pack_into(">IIII", out, 0x10, n_codes, lst, len(hrs), ho)
        return bytes(out)
    hit = struct.pack(">HHIII", 4, 1, 1, 0xE00012, 0x50000000)        # golpe -> HR 1
    src = csk(0x400, {0x2E0: [(1, [hit]), (7, [bytes(16)])]}, [bytes(128), b"\x07" * 128])
    dst = csk(0x400, {}, [b"\x01" * 128], hr_last=False)
    out = csk_graft(dst, 0x2E0, src, 0x2E0)
    out = csk_graft(out, 0x3E0, src, 0x2E0)                            # el HR igual no se repite
    assert struct.unpack(">I", out[0x18:0x1C])[0] == 2
    for code in (0x2E0, 0x3E0):
        bo = struct.unpack(">I", out[0x20 + 4 * code:0x24 + 4 * code])[0]
        nap, apo = struct.unpack(">II", out[bo + 0x28:bo + 0x30])
        t, nl, do = struct.unpack(">HHI", out[apo:apo + 8])
        assert (nap, t) == (2, 1) and csk_hr_block(out, struct.unpack(">I", out[do + 4:do + 8])[0]) == b"\x07" * 128
    assert csk_hr_block(out, 0) == b"\x01" * 128
    # ficha: P P E (combo) y ->E directo de la capsula 26, igual que las filas nativas
    p1, p2 = blk(0, 1, 0, 0, 0, 0, 1, 1), blk(0, 1, 0, 0, 0, 0, 1, 1)
    e1, fe = blk(0, 8, 0, 0, 0, 2, 1, 0, 26), blk(1, 8, 0, 0, 0, 2, 1, 0, 26)
    fich = ccm_build(head, ["a", "f"], {"a": [p1, ["b"]], "b": [p2, ["e"]], "e": [e1, []], "f": [fe, []]})
    cam = b"#AMB" + bytes(12) + struct.pack(">II", 1, 0x20) + bytes(8) + struct.pack(">II", 0x30, len(fich)) + bytes(8) + fich
    assert [r[4:12].hex() for r in skill_rows(cam, 26)] == ["000202051b020205", "01191b0000000019"]
    # modo hiper / definitiva: entradas en el BCM y sus codigos con bloque en el #CSK (forma 1)
    ult = blk(0, 0xF, 0, 0, 0, 0x000A, 0x8001, 0, 49, 0, 0, 0, 0x25A, 0x35A)
    hc = ccm_build(head, ["h", "u"], {"h": [hyper, []], "u": [ult, []]})

    def anm(c):
        return b"#AMB" + bytes(12) + struct.pack(">II", 1, 0x20) + bytes(8) + struct.pack(">II", 0x30, len(c)) + \
            bytes(8) + c
    full = csk(0x400, {k: [(7, [bytes(16)])] for k in (0x259, 0x359, 0x25A, 0x35A)}, [bytes(128)])
    assert hyper_check(hc, [anm(full)]) == []
    holes = hyper_check(hc, [anm(csk(0x400, {0x259: [(7, [bytes(16)])]}, [bytes(128)]))])
    assert len(holes) == 3 and "0x359 del modo hiper" in holes[0] and "definitivo" in holes[1], holes
    assert hyper_check(ccm_build(head, ["h"], {"h": [hyper, []]}), [anm(full)]) == ["sin definitiva en su BCM"]
    print("capsulas.py: autocomprobacion OK")


if __name__ == "__main__":
    _selftest()
