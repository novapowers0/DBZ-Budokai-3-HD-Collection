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
import hashlib
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


FORM_MASK, FORM_ID = 0x1000, 0x0010     # w5: solo en las formas de la mascara w10 / en la forma w10


def ccm_merge_forms(ccms):
    """#CCM de un equipo (Tag Team): los golpes del miembro k solo en la forma k (su forma
    normal); las suyas de otras formas quedan inalcanzables. Sin transformar (P+K+G): el relevo
    es SELECT (src/tagteam.cpp), que no choca con ningun golpe.
    Cabecera: +4 bloques, +0x1E inicios, inicios en +0x50; bloque 0x40 B + hijos (u32)."""
    out_blocks, starters = [], []
    for k, ccm in enumerate(ccms):
        reach = ccm_blocks(ccm)
        size = {o: 0x40 + 4 * w(ccm, o, 7) for o, _ in reach}
        for o, st in reach:
            b = bytearray(ccm[o:o + size[o]])
            c1 = w(b, 0, COND)
            if c1 & 0x0004:                       # transformar: fuera (el relevo es SELECT, runtime)
                b[0:0x40] = bytes(0x40)           # inalcanzable (sin botones ni ramas)
            elif c1 & FORM_ID:                    # golpe de una forma concreta: solo si es la normal
                setw(b, 0, 10, k if w(b, 0, 10) == 0 else 7)
            else:
                mask = (w(b, 0, 10) & 1) if c1 & FORM_MASK else 1
                setw(b, 0, COND, c1 | FORM_MASK)
                setw(b, 0, 10, mask << k)
            out_blocks.append((k, o, b))
            if st:
                starters.append((k, o))
    hdr = bytearray(ccms[0][:0x50])
    at = 0x50 + 4 * len(starters)
    where = {}
    for k, o, b in out_blocks:
        where[(k, o)] = at
        at += len(b)
    body = bytearray()
    for k, o, b in out_blocks:
        for j in range(w(b, 0, 7)):              # hijos: offsets del #CCM nuevo
            old = struct.unpack_from(">I", b, 0x40 + 4 * j)[0]
            struct.pack_into(">I", b, 0x40 + 4 * j, where.get((k, old), 0))
        body += b
    struct.pack_into(">I", hdr, 4, len(out_blocks))
    struct.pack_into(">H", hdr, 0x1E, len(starters))
    table = b"".join(struct.pack(">I", where[s]) for s in starters)
    return bytes(hdr + table + body)


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


# Infinite World (moveset original): las tecnicas NO llevan capsula en w8 (0) sino el ID de
# habilidad de IW en w11 (3 niveles seguidos por tecnica; los ports de la comunidad ponen
# capsulas de B3 en w8). Condicion 0x8000 = tecnica, +0x0008 = definitiva, 0x1000 = solo en
# las formas de la mascara w10 (bit 0 = forma 1: el Kamehameha de Goku GT en normal/SSJ/SSJ3
# y otra entrada con el x10 en SSJ4), 0x0010 = exige el ID de forma w10 (como B3). w4 = ranura
# (1 = >E, 2 = <E; +0x10 = tras combo), w9 = ki x1000. Las cadenas (Super Dragon Fist) llevan
# la capsula de B3 del golpe en sus hijos.
IW_FORM_MASK = 0x1000
HYPER_CODES = (0x258, 0x358)    # modo hiper de B3 (suelo, aire): los de todos los nativos
FREE_CODES = 0x5C0              # golpes de IW que usaban esos codigos: se mueven aqui


def iw_key(blk, o=0):
    """Clave de la tecnica de IW de una entrada: su ID de habilidad (los ports de la comunidad
    lo conservan) o, si no lo tiene, la capsula."""
    return w(blk, o, 11) or w(blk, o, 8)


def iw_skills(ccm, fila=None):
    """Tecnicas y definitivas de un BCM de IW, en orden de ID: [{clave, tipo, formas (mascara,
    0 = todas), ki, variantes}] (las variantes por forma son la misma tecnica).
    `fila` = fila del registro de IW que ocupa la forma 1 (fusiones como Gotenks): las mascaras
    van por filas de esa tabla (bit 3 = Gotenks normal), asi que se desplazan a los indices de
    forma del port (bit 0 = normal); una mascara que se queda en 0 es de otro personaje de la
    fusion y no se lista. La clave es (tipo, ID): un especial y una definitiva pueden compartir
    el ID de IW (Gotenks 941) y son dos capsulas distintas."""
    out = {}
    for o, st in ccm_blocks(ccm):
        c1 = w(ccm, o, COND)
        if not (st and c1 & 0x8000) or w(ccm, o, TYPE) & 0x10:
            continue
        key = iw_key(ccm, o)
        if not key:
            continue
        tipo = "definitiva" if c1 & 0x0008 else "especial"
        m = w(ccm, o, 10) if c1 & IW_FORM_MASK else 0
        if m and fila:
            m >>= fila
            if not m:               # solo en la fila de un componente: no es suya
                continue
        s = out.setdefault((tipo, key), {"clave": key, "tipo": tipo,
                                         "formas": 0, "todas": False, "ki": 0, "variantes": set()})
        s["todas"] |= not m
        s["formas"] |= m
        s["variantes"].add(m)
        s["ki"] = max(s["ki"], (w(ccm, o, 9) + 500) // 1000)
    for s in out.values():
        s["formas"] = 0 if s.pop("todas") else s["formas"]
        s["variantes"] = len(s["variantes"])
    return sorted(out.values(), key=lambda s: (s["tipo"] != "especial", s["clave"]))


def _lowbit(m):
    return (m & -m).bit_length() if m else 99


def adapt_iw_bcm(ccm, skill_caps, nforms=1, key_map=None, ajenos=(), fila=None):
    """BCM de Infinite World -> mecanica de Budokai 3. skill_caps = {clave IW: capsula B3} o
    {(tipo, clave): capsula} (un especial y una definitiva pueden compartir el ID de IW: Gotenks
    941; se separan por tipo y cada una lleva su capsula).
    key_map = {clave del BCM: clave real de IW}: los ports de la comunidad a veces dejan en
    w11 un ID que en la hoja de IW es un objeto (p.ej. Janemba: 884 Senzu Bean, 887 Super Holy
    Water); con el mapa se traduce a la tecnica real (364 Hell Gate, 367 Rakshasa's Claw).
    -> (#CCM nuevo, injertos por forma {forma 0..: {codigo: codigo de la variante}}, informe).
      - variantes por forma (0x1000): una sola entrada (la de la forma 1) y, en las formas de
        las otras, su animacion en el mismo codigo (como los movesets por forma de B3)
      - aura burst -> modo hiper (P+K+G+E / LT), transformarse (arriba+E) -> P+K+G
      - tecnicas: una entrada por capsula en >E (en orden) y en <E (al reves): con dos
        equipadas, cada direccion da una; las de 'tras combo' las pone add_combo_specials
      - definitivas: P+K+G+E en modo hiper, cada una con su capsula
    ajenos = IDs de forma de otros personajes (fusiones: el moveset de Gogeta trae tambien los golpes de Goku, Vegeta y
    Veku con 0x0010 + ese ID en w10): esos golpes se quitan, no valen para el port."""
    KI.clear()
    st, bl = ccm_parse(ccm)
    nodes = {o: [bytearray(b), list(k)] for o, (b, k) in bl.items()}
    rep, grafts = [], {}

    def wn(k, i):
        return w(nodes[k][0], 0, i)

    km = key_map or {}

    def bkey(b):
        k = iw_key(b)
        return km.get(k, k)

    def cap_for(tipo, key):
        """Capsula de una tecnica: por (tipo, clave) si el mapa las separa; si no, por clave."""
        if (tipo, key) in skill_caps:
            return skill_caps[(tipo, key)]
        return skill_caps.get(key, 0)

    # fusiones: las mascaras de forma van por filas del registro de IW (Gotenks: bit 3 = normal);
    # se pasan a indices de forma del port antes de agrupar variantes. Mascara 0 = de un componente.
    if fila:
        for k in nodes:
            if wn(k, COND) & IW_FORM_MASK:
                setw(nodes[k][0], 0, 10, wn(k, 10) >> fila)

    def collapse(keys):
        groups = {}
        for k in keys:
            c1 = wn(k, COND)
            if c1 & IW_FORM_MASK:
                g = (wn(k, 0), wn(k, 1), wn(k, TYPE), c1, iw_key(nodes[k][0]), wn(k, 6))
                groups.setdefault(g, []).append(k)
        drop = set()
        for ks in groups.values():
            ms = [wn(k, 10) or 0xFF for k in ks]
            canon = next((k for k, m in zip(ks, ms) if m & 1), min(zip(ks, ms), key=lambda x: _lowbit(x[1]))[0])
            cm = ms[ks.index(canon)]
            for k, m in zip(ks, ms):
                if k == canon:
                    continue
                pairs = [(canon, k)]
                while pairs:        # el golpe y su cadena (hijos en el mismo orden)
                    a, b = pairs.pop()
                    for i in (12, 13, 14, 15):
                        dc, sc = wn(a, i), wn(b, i)
                        for f in range(nforms):
                            if dc and sc and dc != sc and m >> f & 1 and not cm >> f & 1:
                                prev = grafts.setdefault(f, {}).setdefault(dc, sc)
                                if prev != sc:
                                    rep.append("aviso: forma %d, codigo %#x con dos variantes" % (f + 1, dc))
                    ka, kb = nodes[a][1], nodes[b][1]
                    if len(ka) == len(kb):
                        pairs += [(x, y) for x, y in zip(ka, kb) if x in nodes and y in nodes]
                drop.add(k)
            setw(nodes[canon][0], 0, COND, wn(canon, COND) & ~IW_FORM_MASK)
            setw(nodes[canon][0], 0, 10, 0)
        return [k for k in keys if k not in drop]

    def ajeno(k):
        if k not in nodes:
            return False
        # 0x1000 + mascara sin la fila de la fusion: Gogeta SS4 trae asi los golpes del SS4 de Goku
        # (w10 = 2). Con `fila` la mascara ya viene desplazada: 0 = solo la fila de un componente.
        return ((wn(k, COND) & 0x0010 and wn(k, 10) in ajenos) or
                (fila is not None and wn(k, COND) & IW_FORM_MASK and not wn(k, 10)))
    if ajenos or fila is not None:
        nq = len({x for k in nodes for x in nodes[k][1] if ajeno(x)} | {k for k in st if ajeno(k)})
        for k in nodes:
            nodes[k][1] = [x for x in nodes[k][1] if not ajeno(x)]
        st = [k for k in st if not ajeno(k)]
        if nq:
            rep.append("%d golpes de otros personajes de la fusion (IDs %s) quitados" % (nq, sorted(ajenos)))
    order, todo = [], list(st)
    done = set()
    while todo:                         # recorre todo el arbol una vez
        k = todo.pop()
        if k in done or k not in nodes:
            continue
        done.add(k)
        nodes[k][1] = collapse(nodes[k][1])
        todo += nodes[k][1]
    st = collapse([o for o in st if o in nodes])
    nvar = sum(len(v) for v in grafts.values())
    if nvar:
        rep.append("variantes por forma: %d codigos en %s" % (
            nvar, ", ".join("forma %d" % (f + 1) for f in sorted(grafts))))
    # requisito de ID de forma (0x0010 + w10): estados de IW que el port no tiene (Buu con
    # alguien absorbido, fusiones, Majin): si el golpe tiene version sin requisito con los
    # mismos botones, esa (la que IW usa fuera de ese estado); si no, vale siempre
    def needs_id(k):
        return wn(k, COND) & 0x0010 and wn(k, 10)

    def drop_state_variants(keys):
        plain = {(wn(k, 0), wn(k, 1), wn(k, TYPE), wn(k, 6)) for k in keys if not needs_id(k)}
        out = [k for k in keys if not (needs_id(k) and (wn(k, 0), wn(k, 1), wn(k, TYPE), wn(k, 6)) in plain)]
        return out, len(keys) - len(out)
    ndrop = 0
    for k in list(nodes):
        nodes[k][1], n = drop_state_variants(nodes[k][1])
        ndrop += n
    st, n = drop_state_variants(st)
    ndrop += n
    nid = 0
    for k in nodes:
        if needs_id(k):
            # 30/10 (#54): antes w10 = 0 con 0x0010 puesto = "solo en la forma con ID de personaje 0" (la de Goku): el
            # port solo la cumplia en una forma prestada de Goku y al transformarse perdia especiales y definitiva
            setw(nodes[k][0], 0, COND, wn(k, COND) & ~0x0010)
            setw(nodes[k][0], 0, 10, 0)
            nid += 1
    if ndrop:
        rep.append("%d golpes de estados de IW (absorcion, fusion...) con version normal: la normal" % ndrop)
    if nid:
        rep.append("%d golpes de estados de IW sin version normal: valen en todas las formas" % nid)

    def convert_chain(k, cid, seen):
        if k in seen or k not in nodes:
            return
        seen.add(k)
        b = nodes[k][0]
        setw(b, 0, COND, w(b, 0, COND) & ~(0x8000 | IW_FORM_MASK | 0x0001))
        setw(b, 0, TYPE, 0)
        setw(b, 0, 8, cid)
        setw(b, 0, 9, 0)
        setw(b, 0, 11, 0)
        for ch in nodes[k][1]:
            convert_chain(ch, cid, seen)

    # golpes de IW en los codigos del modo hiper (Gohan nino, Buu...): a codigos libres; el
    # injerto copia su animacion alli en todas las formas (grafts[-1] = {nuevo: viejo})
    moved = {}
    for k in nodes:
        if w(nodes[k][0], 0, COND) & 0x4000 and w(nodes[k][0], 0, 1) == 0x20:
            continue
        for i in (12, 13, 14, 15):
            c = wn(k, i)
            if c in HYPER_CODES:
                moved.setdefault(c, FREE_CODES + len(moved))
                setw(nodes[k][0], 0, i, moved[c])
    if moved:
        grafts[-1] = {v: c for c, v in moved.items()}
        rep.append("golpes en los codigos del modo hiper movidos: %s" % ", ".join(
            "%#x->%#x" % x for x in sorted(moved.items())))
    out, specials, first_sp = [], {}, None
    otra_ranura = {}
    # fusiones de IW: tecnicas y definitiva con la misma clave; la tecnica lleva la capsula de
    # tecnica, no la de la definitiva (con claves (tipo, ID) ya son capsulas distintas)
    caps_ult = {cap_for("definitiva", bkey(nodes[k][0])) for k in st
                if w(nodes[k][0], 0, COND) & 0x8000 and w(nodes[k][0], 0, COND) & 0x0008}
    for k in st:
        b = nodes[k][0]
        c1 = w(b, 0, COND)
        if c1 & 0x4000 and w(b, 0, 1) == 0x20:          # aura burst -> modo hiper
            setw(b, 0, 0, 0)
            setw(b, 0, 1, 0x0F)
            setw(b, 0, COND, 0x0400)
            setw(b, 0, 6, 0x0001)
            setw(b, 0, 9, 0)
            # en los codigos del modo hiper de B3 (0x258/0x358, como los nativos); la animacion
            # que pone su bloque (injertado del donante) sera la del aura burst (grafts[-2])
            grafts[-2] = {HYPER_CODES[0]: w(b, 0, 12), HYPER_CODES[1]: w(b, 0, 13)}
            for i, v in zip((12, 13, 14, 15), HYPER_CODES + (HYPER_CODES[1], 0)):
                setw(b, 0, i, v)
            rep.append("modo hiper: entrada de 'aura burst' de IW convertida (LT/L2)")
        elif c1 & 0x0004 and w(b, 0, 1) == 8 and w(b, 0, 0) & 0x30:   # arriba+E -> P+K+G
            setw(b, 0, 0, 0)
            setw(b, 0, 1, 0x07)
            setw(b, 0, TYPE, 0)
            setw(b, 0, 9, 0)
            # sin el bit 0x0001 (coste de ki): sub_82101... mira primero ese y ya no pregunta si hay forma a la que subir
            # (sub_82119108): con 5 el COM de Vegito repetia P+K+G en su ultima forma (accion 50, 42 % del combate)
            setw(b, 0, COND, c1 & ~0x0001)
            rep.append("transformarse: arriba+E de IW -> P+K+G (sus animaciones %#x/%#x)" % (
                w(b, 0, 12), w(b, 0, 13)))
        elif c1 & 0x8000 and c1 & 0x0008:                # definitiva
            key = bkey(b)
            cid = cap_for("definitiva", key)
            for i, v in ((0, 0), (1, 0x0F), (COND, 0x000A), (TYPE, 0), (6, 0x8001), (8, cid),
                         (9, 0), (10, 0), (11, 0)):
                setw(b, 0, i, v)
            if cid:
                KI[cid] = max(KI.get(cid, 0), 4)
            rep.append("definitiva (IW %d) -> capsula %s" % (key, cid or "ninguna: no saldra"))
        elif c1 & 0x8000:                                 # tecnica
            if first_sp is None:
                first_sp = len(out)
            key = bkey(b)
            if w(b, 0, TYPE) & 0x10:                      # tras combo
                continue
            if key in specials:                           # la de la otra ranura: su golpe, si es otro (Gogeta <E)
                if w(b, 0, 12) != w(nodes[specials[key]][0], 0, 12):
                    otra_ranura[key] = k
                continue
            specials[key] = k
            continue
        out.append(k)
    cap_special = ([k for (t, k) in skill_caps if t == "especial"]
                   if any(isinstance(x, tuple) for x in skill_caps) else list(skill_caps))
    sp_order = [key for key in cap_special if key in specials] + [key for key in specials if key not in cap_special]
    new = []
    for d, keys in ((1, sp_order), (2, sp_order[::-1])):
        for key in keys:
            k = otra_ranura.get(key, specials[key]) if d == 2 else specials[key]
            cid = cap_for("especial", key)
            if cid in caps_ult:
                cid = next((v for v in skill_caps.values() if v not in caps_ult), cid)
            cid = cid or UNUSED_CAP
            b = bytearray(nodes[k][0])
            ki = (w(b, 0, 9) + 500) // 1000
            setw(b, 0, 0, d)
            setw(b, 0, COND, (w(b, 0, COND) & ~(0x8000 | IW_FORM_MASK | 0x0001)) | 0x0002)
            for i, v in ((TYPE, 0), (6, 0x0001), (8, cid), (9, 0), (10, 0), (11, 0)):
                setw(b, 0, i, v)
            kids = nodes[k][1]
            seen = set()
            for ch in kids:
                convert_chain(ch, cid, seen)
            nk = ("iw", d, key)
            nodes[nk] = [b, list(kids)]
            new.append(nk)
            if cid != UNUSED_CAP:
                KI[cid] = max(KI.get(cid, 0), ki)
    for key in sp_order:
        rep.append("tecnica (IW %d) -> capsula %s" % (key, cap_for("especial", key) or "ninguna: no saldra"))
    if first_sp is None:
        first_sp = len(out)
    out[first_sp:first_sp] = new
    built = bytearray(ccm_build(ccm, out, nodes))
    if hyper_first(built):
        rep.append("modo hiper: su entrada pasa delante de los definitivos (mismos botones)")
    return bytes(built), grafts, rep


def hyper_tail_from(ccm, donor_ccm):
    """Copia en la entrada de modo hiper la 2a mitad (palabras 16-31) de la del donante: la
    de IW (aura burst) traia w21 = 0x1e en vez de 0xff y el juego oscurecia el escenario sin
    entrar en modo hiper (Goku GT, 2026-10-10). -> si cambio algo."""
    def hyp(c):
        return next((o for o, st in ccm_blocks(c) if st and w(c, o, COND) & 0x0400), None)
    o, d = hyp(ccm), hyp(donor_ccm)
    if o is None or d is None:
        return False
    ccm[o + 32:o + 64] = donor_ccm[d + 32:d + 64]
    return True


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


# animaciones de la transformacion de B3 (suelo, aire), una por forma destino: la accion 50 pide base + forma
# (sub_8210E100). Sin 0x2E1.. el luchador se quedaba 3 s en la accion 50 sin animacion ni transformarse (Gogeta de
# IW en COM, issue #58)
# y tres fases por forma (sub_8210E250 suma 8 al acabar cada una: arranque, espera, final)
ID_MAX_NUEVA = 1984    # el inventario de la partida acaba en 2048 y 1984+ son las capsulas unicas (src/form_selection.h)
TRANSFORM_CODES = tuple(range(0x2E0, 0x2F8)) + tuple(range(0x3E0, 0x3F8))


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
_RENDER_CACHE = {}


def render_name(text, h=NAME_H, color=(255, 255, 255, 255)):
    """Nombre de capsula al estilo del juego (blanco con borde oscuro): mismo tamano y
    posicion que los nativos (calibrado con "Kamehameha" de los bancos 2682 / SCMKLL).
    Recordado por CONTENIDO (el montaje rinde el mismo rotulo una vez por idioma)."""
    key = ("nombre", text, h, color)
    hit = _RENDER_CACHE.get(key)
    if hit is not None:
        return hit.copy()
    f = load_font(NAME_FONT, 18)
    wid = int(f.getlength(text)) + 10
    out = Image.new("RGBA", (min(wid, 508), h), (0, 0, 0, 0))
    ImageDraw.Draw(out).text((3, h // 2 - (0 if h >= 28 else 1)), text, font=f, anchor="lm", fill=color,
                             stroke_width=2, stroke_fill=(30, 20, 20, 255))
    img = np.array(out)
    if len(_RENDER_CACHE) < 4096:
        _RENDER_CACHE[key] = img
    return img.copy()


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
    """Lineas del panel de descripcion; recordado por CONTENIDO (mismo texto -> misma imagen)."""
    if not text:
        return np.zeros((24, 16, 4), np.uint8)
    hit = _RENDER_CACHE.get(("lineas", text))
    if hit is not None:
        return hit.copy()
    f = load_font(DESC_FONT, 16)
    lines = _wrap(text, f, DESC_W)
    wid = min(DESC_W + 8, int(max(f.getlength(t) for t in lines)) + 6)
    h = DESC_TOP + DESC_PITCH * len(lines) + 5
    out = Image.new("RGBA", (max(16, wid), h), (0, 0, 0, 0))
    d = ImageDraw.Draw(out)
    for k, t in enumerate(lines):
        d.text((1, DESC_TOP + DESC_PITCH * k), t, font=f, fill=DESC_COLOR)
    img = np.array(out)
    if len(_RENDER_CACHE) < 4096:
        _RENDER_CACHE[("lineas", text)] = img
    return img.copy()


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


_AZT_CACHE = {}


def azt_read(b, k):
    """Textura k del #AZT (con recorte logico); recordada por CONTENIDO (las mismas
    texturas nativas se leen una vez por personaje e idioma)."""
    o = azt_entries(b)[k]
    if not o:
        return None
    do, ds = struct.unpack(">II", b[o + 0x14:o + 0x1C])
    lw, lh = struct.unpack(">HH", b[o + 0x10:o + 0x14])
    blob = bytes(b[do:do + ds])
    key = hashlib.sha1(blob).digest()
    hit = _AZT_CACHE.get(key)
    if hit is None:
        hit = np.array(Image.open(io.BytesIO(blob)).convert("RGBA"))
        if len(_AZT_CACHE) < 4096:
            _AZT_CACHE[key] = hit
    return hit[:lh, :lw].copy()


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


def csk_block_at(csk, code):
    """Offset del bloque de ataque `code` de un #CSK (None si no tiene)."""
    n, lst = struct.unpack(">II", csk[0x10:0x18])
    if code >= n:
        return None
    return struct.unpack_from(">I", csk, lst + 4 * code)[0] or None


def csk_set_anim(csk, code, src_csk, src_code):
    """El bloque `code` reproduce la animacion del bloque src_code (cabecera: u16 animacion,
    u16 banco). El modo hiper injertado del donante pedia la animacion 9 de Goku: en el
    moveset de IW es otra y el personaje se quedaba quieto con la pantalla oscura."""
    o, so = csk_block_at(csk, code), csk_block_at(src_csk, src_code)
    if o is None or so is None:
        return False
    csk[o:o + 4] = src_csk[so:so + 4]
    return True


def csk_ap_lines(csk, code):
    """[(tipo AP, offset de la linea)] del bloque `code` (cada linea: u16 fotograma + datos)."""
    o = csk_block_at(csk, code)
    if o is None:
        return []
    nap, apo = struct.unpack_from(">II", csk, o + 0x28)
    out = []
    for a in range(nap):
        t, nl, do = struct.unpack_from(">HHI", csk, apo + 8 * a)
        out += [(t, do + 16 * i) for i in range(nl)]
    return out


# AP tipo 7 clase 4: efecto del banco que se carga por personaje (Cell/Cooler/Freeza 352-356,
# Bardock/Tenshinhan 139-149...). Uno que el donante no carga deja un puntero nulo y el juego
# se cierra al usar el golpe (Final Flash de Vegeta GT pedia 352 con el banco de Vegeta).
AP7_FX_BANK = 4


def csk_fx_values(csk):
    """Valores de clase 4 (AP tipo 7) que usa un #CSK."""
    out = set()
    n, lst = struct.unpack(">II", csk[0x10:0x18])
    for code in range(n):
        for t, lo in csk_ap_lines(csk, code) if struct.unpack_from(">I", csk, lst + 4 * code)[0] else ():
            if t == 7 and struct.unpack_from(">I", csk, lo + 4)[0] == AP7_FX_BANK:
                out.add(struct.unpack_from(">I", csk, lo + 8)[0])
    return out


def csk_drop_fx(csk, allowed):
    """Desactiva (fotograma 0xFFFF: nunca llega) los efectos de clase 4 fuera de `allowed`.
    -> {valor: veces}."""
    out = {}
    n, lst = struct.unpack(">II", csk[0x10:0x18])
    for code in range(n):
        if not struct.unpack_from(">I", csk, lst + 4 * code)[0]:
            continue
        for t, lo in csk_ap_lines(csk, code):
            if t == 7 and struct.unpack_from(">I", csk, lo + 4)[0] == AP7_FX_BANK:
                v = struct.unpack_from(">I", csk, lo + 8)[0]
                if v not in allowed and struct.unpack_from(">H", csk, lo)[0] != 0xFFFF:
                    struct.pack_into(">H", csk, lo, 0xFFFF)
                    out[v] = out.get(v, 0) + 1
    return out


def csk_remap_fx(csk, mapping):
    """Cambia valores de clase 4 (AP tipo 7) segun `mapping` {viejo: nuevo}. -> veces."""
    n_ = 0
    n, lst = struct.unpack(">II", csk[0x10:0x18])
    for code in range(n):
        if not struct.unpack_from(">I", csk, lst + 4 * code)[0]:
            continue
        for t, lo in csk_ap_lines(csk, code):
            if t == 7 and struct.unpack_from(">I", csk, lo + 4)[0] == AP7_FX_BANK:
                v = struct.unpack_from(">I", csk, lo + 8)[0]
                if v in mapping:
                    struct.pack_into(">I", csk, lo + 8, mapping[v])
                    n_ += 1
    return n_


# Rayos/bolas del BSP (#CST, 0xF0 por entrada, codigo u16 en +0x12): los codigos >= 300 van a
# un banco por grupo de personajes (350/351 Goku/Vegeta/Buu, 352-356 Freeza/Cell/Cooler) que
# solo se carga si el donante es del grupo; los < 300 se buscan en el propio BSP.
FX_GROUP_BANK = 300


def _bsp_cst(bsp):
    n, t = struct.unpack_from(">II", bsp, 0x10)
    o0 = struct.unpack_from(">I", bsp, t)[0]
    n1, t1 = struct.unpack_from(">II", bsp, o0 + 0x10)
    cst = o0 + struct.unpack_from(">I", bsp, o0 + t1)[0]
    cnt, s0 = struct.unpack_from(">II", bsp, cst + 8)
    return [cst + s0 + k * 0xF0 + 0x12 for k in range(cnt)]


def bsp_fx_codes(bsp):
    """Codigos de efecto de un BSP: rayos/bolas (#CST) y efectos (#CSE)."""
    n, t = struct.unpack_from(">II", bsp, 0x10)
    o1 = struct.unpack_from(">I", bsp, t + 16)[0]
    n1, t1 = struct.unpack_from(">II", bsp, o1 + 0x10)
    cse = o1 + struct.unpack_from(">I", bsp, o1 + t1)[0]
    cnt, s1 = struct.unpack_from(">II", bsp, cse + 8)
    return ({struct.unpack_from(">H", bsp, a)[0] for a in _bsp_cst(bsp)},
            {struct.unpack_from(">H", bsp, cse + s1 + k * 0xD0 + 0x6A)[0] for k in range(cnt)})


def bsp_rename_cst(bsp, mapping):
    """Copia del BSP con los codigos de #CST cambiados segun `mapping` {viejo: nuevo}."""
    out = bytearray(bsp)
    for a in _bsp_cst(bsp):
        v = struct.unpack_from(">H", out, a)[0]
        if v in mapping:
            struct.pack_into(">H", out, a, mapping[v])
    return bytes(out)


def csk_last_frame(csk, code):
    """Ultimo fotograma con evento del bloque `code` (su duracion aproximada; 0 si no tiene)."""
    return max((struct.unpack_from(">H", csk, lo)[0] for _, lo in csk_ap_lines(csk, code)), default=0)


def csk_fit_frames(csk, code, length):
    """Comprime los fotogramas de los eventos del bloque `code` para que el ultimo caiga en
    `length` (si ya caben, no toca nada). -> si cambio algo."""
    last = csk_last_frame(csk, code)
    if not length or last <= length:
        return False
    for _, lo in csk_ap_lines(csk, code):
        f = struct.unpack_from(">H", csk, lo)[0]
        struct.pack_into(">H", csk, lo, f * length // last)
    return True


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
    # eventos del bloque hiper comprimidos a la duracion de la animacion propia (IW: 60 de 120)
    ev = lambda f: struct.pack(">H", f) + bytes(14)
    hb = bytearray(csk(0x300, {0x258: [(5, [ev(0), ev(120)]), (7, [ev(88), ev(100)])]}, [bytes(128)]))
    assert csk_last_frame(hb, 0x258) == 120 and csk_fit_frames(hb, 0x258, 60)
    assert [struct.unpack_from(">H", hb, lo)[0] for _, lo in csk_ap_lines(hb, 0x258)] == [0, 60, 44, 50]
    assert not csk_fit_frames(hb, 0x258, 60)
    fx = lambda f, v: struct.pack(">HHII", f, 2, AP7_FX_BANK, v) + bytes(4)
    fb = bytearray(csk(0x300, {0x24A: [(7, [fx(3, 0x12C), fx(9, 0x160)])]}, [bytes(128)]))
    assert csk_fx_values(fb) == {0x12C, 0x160} and csk_drop_fx(fb, {0x12C}) == {0x160: 1}
    assert csk_fx_values(fb) == {0x12C, 0x160} and csk_drop_fx(fb, {0x12C}) == {}
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
    # IW original (Goku GT): habilidad en w11, variante del SSJ4 por mascara, cadena con la
    # capsula de B3 en los hijos, entradas de las dos ranuras y de 'tras combo'
    aura = blk(0, 0x20, 0, 0, 0, 0x4000, 1, 0, 0, 0, 0, 0, 0x20, 0x120)
    up = blk(0x20, 8, 0, 0, 0, 0x0004, 1, 0, 0, 0, 0, 0, 0x2E0, 0x3E0)
    k1 = blk(1, 8, 0, 0, 1, 0x9001, 1, 0, 0, 1000, 7, 524, 0x24A, 0x34A)      # formas 1-3
    k4 = blk(1, 8, 0, 0, 1, 0x9001, 1, 0, 0, 1000, 8, 524, 0x24D, 0x34D)      # SSJ4
    k2 = blk(2, 8, 0, 0, 2, 0x9001, 1, 0, 0, 1000, 7, 524, 0x24A, 0x34A)
    kc = blk(1, 8, 0, 0x7530, 0x11, 0x9001, 0x2000, 0, 0, 1000, 7, 524, 0x24A, 0x34A)
    df = blk(2, 8, 0, 0, 2, 0x9001, 1, 1, 0, 2000, 0xE, 530, 0x26D, 0x36D)
    dfk = blk(0, 8, 0x44, 0x4A, 2, 0x8800, 0, 0, 7, 0, 0, 530, 0x26E, 0x36E)
    iu = blk(0x10, 8, 0, 0, 0, 0x8009, 1, 0, 0, 5000, 0, 917, 0x259, 0x359)
    iw = ccm_build(head, list("auxyzcdi"), {"a": [aura, []], "u": [up, []], "x": [k1, []], "y": [k4, []],
                                            "z": [k2, []], "c": [kc, []], "d": [df, ["e"]], "e": [dfk, []],
                                            "i": [iu, []]})
    sk = iw_skills(iw)
    assert [(s["clave"], s["tipo"], s["formas"], s["ki"], s["variantes"]) for s in sk] == [
        (524, "especial", 0xF, 1, 2), (530, "especial", 0xE, 2, 1), (917, "definitiva", 0, 5, 1)], sk
    new, grafts, rep = adapt_iw_bcm(iw, {524: 600, 530: 601, 917: 602}, 4)
    assert grafts == {3: {0x24A: 0x24D, 0x34A: 0x34D}, -2: {0x258: 0x20, 0x358: 0x120}}, grafts
    st, bl = ccm_parse(new)
    rows = [tuple(w(bl[o][0], 0, i) for i in (0, 1, COND, 8)) for o in st]
    assert rows == [(0, 0xF, 0x400, 0), (0, 7, 0x0004, 0), (1, 8, 2, 600), (1, 8, 2, 601), (2, 8, 2, 601),
                    (2, 8, 2, 600), (0, 0xF, 0x000A, 602)], rows
    kid = bl[st[3]][1][0]
    assert w(bl[kid][0], 0, 8) == 601 and w(bl[kid][0], 0, COND) == 0x0800
    assert all(not w(bl[o][0], 0, i) for o in st for i in (9, 10, 11))
    # golpe de un estado de IW (exige el ID de forma 78) con version normal: queda la normal
    pn = blk(0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0x200, 0x300)
    pb = blk(0, 1, 0, 0, 0, 0x0010, 1, 0, 0, 0, 78, 0, 0x2A0, 0x3A0)
    solo = blk(0, 2, 0, 0, 0, 0x0010, 1, 0, 0, 0, 78, 0, 0x2A1, 0x3A1)
    new, _, rep = adapt_iw_bcm(ccm_build(head, ["b", "n", "s"], {"b": [pb, []], "n": [pn, []], "s": [solo, []]}), {})
    st, bl = ccm_parse(new)
    assert [w(bl[o][0], 0, 12) for o in st] == [0x200, 0x2A1] and not w(bl[st[1]][0], 0, 10), rep
    print("capsulas.py: autocomprobacion OK")


if __name__ == "__main__":
    _selftest()
