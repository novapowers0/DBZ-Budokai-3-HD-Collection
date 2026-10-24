#!/usr/bin/env python3
"""campana.py - Campanas de Dragon Universe con mods (Mod Kit).

En Budokai 3 cada linea de dialogo o de narracion de la historia es una IMAGEN por idioma
(#AZT en data_usi y data_eng/spn/fra/ger/ita, una textura por bocadillo). Las escenas son
contenedores #LST de data_cmn y cada combate un #BPC (el rival es el u32 de +0x10). El juego
elige la escena de cada "codigo de historia" con su propia tabla (du_codigos.json); el port
deja que un mod la cambie (campana.toml, seccion [codigos]). Ver docs/11_campanas/CAMPANAS.md.

  python campana.py ver 359                 # PNG de los textos de data_usi 359 (todos los idiomas)
  python campana.py escenas 5500            # escenas del bloque de historia 5500 y sus textos
  python campana.py montar mi_campana.toml  # crea el mod

mi_campana.toml:

  [campana]
  nombre = "La saga de Freezer"
  mod = "campana_freezer"            # carpeta en mods/

  [[texto]]                          # una entrada de textos de data_usi
  entrada = 359
  estilo = "dialogo"                 # "dialogo" (oscuro) o "narracion" (blanco con borde)
  en = ["Sorry... but you left yourself wide open.", "Why... you..."]
  es = ["Lo siento... pero has bajado la guardia.", "Tu..."]

  [[combate]]
  bpl = 708                          # #BPC de data_cmn
  rival = 27                         # ID del rival (27 = Freezer)

  [codigos]                          # escena del juego -> contenedor #LST (en tiempo de juego)
  5500 = 2138

  [rueda]                            # personajes de mas en la rueda de Dragon Universe
  personajes = [27]                  # (27 = Freezer: su casilla tiene historia propia en los datos)

  [[escena]]                         # eventos que se anaden a una entrada de un contenedor #LST
  contenedor = 1725                  # (p. ej. el paso de la intro al mapa que le falta a una historia)
  entrada = 22
  anadir = [[22, 0, 0]]              # [evento, p1, p2]; los del mapa llevan mas valores
                                     # (MAP_EVENT_WORDS): 44 = salida (44, 0, x, z, y)
"""
import argparse
import io
import json
import os
import struct
import sys
import tempfile
import atexit
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

import capsulas
import png2pic  # noqa: E402
import roster_build as rb  # noqa: E402

try:
    import tomllib  # noqa: E402
except ImportError:  # Python < 3.11
    import tomli as tomllib  # type: ignore  # noqa: E402

LANG_AFS = {"en": ("data_usi.afs", "data_eng.afs"), "es": ("data_spn.afs",), "fr": ("data_fra.afs",),
            "de": ("data_ger.afs",), "it": ("data_ita.afs",)}
RIVAL_SUITS = {}   # [traje_rival] del campana.toml: palabra del combate -> traje del rival
START_FORMS = {}   # [forma_jugador] del campana.toml: palabra del combate -> forma inicial
STORY_CODES = {}   # [codigos_historia]: codigo -> contenedor, solo mientras se juega una historia de [rueda]
MAP_ANIMS = {}     # [animacion_mapa]: palabra -> ID cuyo vuelo del mapa se usa (Mecha Freezer: 38 Cooler)
MAP_SUITS = {}     # [traje_mapa]: palabra del combate -> traje en el mapa (Freezer 2 = en su nave)
MAP_MODELS = {}    # [modelo_mapa] del campana.toml: palabra del combate -> modelo con el que vuelas hacia el
TOML_DIR = [HERE]   # carpeta del .toml que se monta (las paginas .png van relativas a ella)


def work_dir():
    """Carpeta temporal de trabajo de esta ejecucion; se limpia al salir (antes cada montaje dejaba sus
    entradas descomprimidas en %TEMP%, en el disco C)."""
    d = tempfile.mkdtemp(prefix="campana_")
    atexit.register(shutil.rmtree, d, ignore_errors=True)
    return d
WRITTEN = set()     # (afs, entrada) escritas en este montaje: lo que sobre en su data_cmn va a mods_respaldo
CMN = {}            # bloque de contenedores propios de data_cmn (reservar_cmn): base, siguiente
SHARED = {}         # contenedor compartido -> su copia en el bloque propio
CMN_LO, CMN_HI, CMN_N = 6000, 8192, 64   # lejos de _roster (3990+) y de los escenarios (5000+)
NATIVE_DU = 0x400023C9D   # casillas con historia del juego (Goku 0 ... Broly 34)


def write_entry(mod_dir, afs, n, data, work):
    WRITTEN.add((afs, int(n)))
    rb.write_entry(mod_dir, afs, n, data, work)


def old_toml(mod):
    try:
        return tomllib.load(open(os.path.join(mod, "campana.toml"), "rb"))
    except (OSError, ValueError, tomllib.TOMLDecodeError):
        return {}


def reservar_cmn(mods, mod):
    """Bloque de CMN_N entradas de data_cmn de esta campana (copias de los contenedores que comparten todas
    las historias, imagenes, eventos del mapa), sin pisar a otros mods (tambien desactivados). Estable al
    rehacer: campana.toml [contenedores]. Depende del nombre del mod (crc32), como los textos."""
    import zlib  # noqa: PLC0415
    me = os.path.basename(mod)
    taken = set()
    for d in os.listdir(mods) if os.path.isdir(mods) else []:
        p = os.path.join(mods, d, "us", "data_cmn.afs")
        if d != me and os.path.isdir(p):
            taken.update(int(e) for e in os.listdir(p) if e.isdigit())
    slots = (CMN_HI - CMN_LO) // CMN_N
    old = old_toml(mod).get("contenedores", {}).get("base")
    cands = ([int(old)] if old else []) + [CMN_LO + CMN_N * ((zlib.crc32(me.encode("utf-8")) + i) % slots)
                                           for i in range(slots)]
    for b in cands:
        if CMN_LO <= b <= CMN_HI - CMN_N and not any(b + k in taken for k in range(CMN_N)):
            CMN.update(base=b, next=b)
            return b
    raise SystemExit("no quedan contenedores libres en data_cmn para %s (demasiadas campanas)" % me)


def nueva_entrada():
    """Siguiente entrada libre del bloque propio de data_cmn."""
    if CMN["next"] >= CMN["base"] + CMN_N:
        raise SystemExit("la campana usa mas de %d entradas propias de data_cmn" % CMN_N)
    CMN["next"] += 1
    return CMN["next"] - 1


def shared_codes(cont_n):
    """Codigos de historia que llevan al contenedor `cont_n` si lo comparten todas las historias (fuera de
    los bloques por casilla 5000-6199: introducciones 100-106, finales 1, eventos del mapa 2100...)."""
    reg = json.load(open(os.path.join(HERE, "du_codigos.json"), encoding="utf-8"))["codigos"]
    codes = sorted(int(k) for k, v in reg.items() if int(v) == int(cont_n))
    return codes if any(not 5000 <= c < 6200 for c in codes) else []


def write_container(mod, cont_n, data, work):
    """Un contenedor compartido no se cambia para todos: su copia va al bloque propio y el juego la usa
    solo mientras se juega la casilla de esta campana ([codigos_historia]); asi conviven varias campanas.
    Los de una sola casilla se escriben en su sitio."""
    codes = shared_codes(cont_n)
    if not codes:
        write_entry(mod, "data_cmn.afs", int(cont_n), data, work)
        return int(cont_n)
    e = SHARED.setdefault(int(cont_n), nueva_entrada())
    write_entry(mod, "data_cmn.afs", e, data, work)
    for c in codes:
        STORY_CODES[c] = e
    return e


def other_campaigns(mods, mod):
    """{casilla: mod} de las otras campanas de la carpeta de mods (tambien desactivadas)."""
    out = {}
    for d in os.listdir(mods) if os.path.isdir(mods) else []:
        p = os.path.join(mods, d)
        if d != os.path.basename(mod) and os.path.isfile(os.path.join(p, "campana.toml")):
            c = old_toml(p).get("rueda", {}).get("casilla")
            if c is not None:
                out[int(c)] = d
    return out


def validar(cfg, mods, mod):
    """(errores, avisos) antes de montar: la casilla de DU de la historia (codigo % 100) no puede ser la de
    otra campana ni la de una historia del juego."""
    err, warn = [], []
    for e in cfg.get("escena", []):
        for ev in e.get("cambiar", []) + e.get("anadir", []):
            try:
                words = len(pack_event(tuple(ev))) // 4
            except (TypeError, ValueError):
                err.append("[[escena]] %s.%s: evento %r mal escrito" % (e.get("contenedor"), e.get("entrada"), ev))
                continue
            need = MAP_EVENT_WORDS.get(int(ev[0]), 3)
            if words != need:
                err.append("[[escena]] %s.%s: el evento %s ocupa %d palabras en el juego y %r da %d"
                           % (e.get("contenedor"), e.get("entrada"), ev[0], need, ev, words))
    h = cfg.get("historia")
    if not h:
        return err, warn
    if "codigo" not in h:
        return ["[historia] necesita `codigo` (5000 + 100*saga + casilla: Freezer 5122)"], warn
    slot = int(h["codigo"]) % 100
    virt = [int(v) for v in cfg.get("rueda", {}).get("casillas_virtuales", [])]   # el Heroe: entra por una celda virtual
    if not cfg.get("rueda", {}).get("personajes") and not virt:
        err.append("falta [rueda] personajes = [ID]: sin el, la historia no sale en la rueda de Dragon Universe")
    if virt and slot not in virt:
        err.append("[historia] codigo %d es de la casilla %d, pero [rueda] casillas_virtuales = %s" % (int(h["codigo"]), slot, virt))
    # casilla de cada ID: la inversa de la formula del juego (sub_82156E88: casilla < 22 igual, 22-25 +5, >= 26 +6)
    slot_of = lambda i: i if i < 22 else i - 5 if 27 <= i <= 30 else i - 6 if 32 <= i <= 43 else None
    ids = cfg.get("rueda", {}).get("personajes") or []
    ids = [int(i) for i in (ids if isinstance(ids, list) else [ids])]
    if ids and slot not in [slot_of(i) for i in ids]:
        err.append("[historia] codigo %d es de la casilla %d, pero [rueda] personajes = %s van a las casillas %s: el "
                   "codigo es 5000 + 100*saga + casilla del protagonista" % (int(h["codigo"]), slot, ids,
                                                                          [slot_of(i) for i in ids]))
    for e in cfg.get("escena", []):
        bad = sorted({int(ev[0]) for ev in e.get("cambiar", []) + e.get("anadir", []) if int(ev[0]) in BAD_OPS})
        if bad:
            err.append("[[escena]] %s.%s usa los eventos %s, que el juego lee mal (pila sin inicializar)"
                       % (e.get("contenedor"), e.get("entrada"), bad))
    if slot >= 38:
        err.append("casilla %d: la rueda solo tiene 38 (0-37); no hay sitio en la partida para mas" % slot)
    elif (NATIVE_DU >> slot) & 1:
        warn.append("la casilla %d ya tiene historia del juego: esta campana la sustituye" % slot)
    if slot in other_campaigns(mods, mod):
        err.append("la casilla %d ya la usa la campana '%s': elige otro protagonista (otra casilla)"
                   % (slot, other_campaigns(mods, mod)[slot]))
    return err, warn


def entry_conflicts(mods, mod):
    """Entradas de data_cmn de este mod que otro mod tambien trae con otros datos (solo una ganaria)."""
    import hashlib  # noqa: PLC0415
    mine = os.path.join(mod, "us", "data_cmn.afs")
    out = []
    for d in os.listdir(mods) if os.path.isdir(mods) else []:
        p = os.path.join(mods, d, "us", "data_cmn.afs")
        if d == os.path.basename(mod) or d.startswith((".", "_")) or not os.path.isdir(p) or not os.path.isdir(mine):
            continue
        h = lambda f: hashlib.md5(open(f, "rb").read()).digest() if os.path.isfile(f) else None
        for e in sorted(set(os.listdir(mine)) & set(os.listdir(p)), key=lambda x: int(x) if x.isdigit() else 0):
            if h(os.path.join(mine, e, "geom.bin")) != h(os.path.join(p, e, "geom.bin")):
                out.append((int(e) if e.isdigit() else e, d))
    return out


def move_stale(mods, mod):
    """Entradas de data_cmn que este montaje ya no escribe (p. ej. contenedores compartidos que antes se
    cambiaban en su sitio): a mods_respaldo, nunca se borran."""
    import shutil  # noqa: PLC0415
    import time  # noqa: PLC0415
    d = os.path.join(mod, "us", "data_cmn.afs")
    stale = [e for e in os.listdir(d) if e.isdigit() and ("data_cmn.afs", int(e)) not in WRITTEN] if os.path.isdir(d) else []
    if stale:
        bak = os.path.join(os.path.dirname(os.path.abspath(mods)), "mods_respaldo",
                           "%s_cmn_%s" % (os.path.basename(mod), time.strftime("%Y%m%d_%H%M%S")), "data_cmn.afs")
        os.makedirs(bak, exist_ok=True)
        for e in stale:
            shutil.move(os.path.join(d, e), os.path.join(bak, e))
        rb.log("data_cmn que ya no usa (a %s): %s" % (bak, " ".join(sorted(stale, key=int))))


# ---------------------------------------------------------------- texto -> imagen
# Estilos medidos sobre los nativos (data_usi 359/1023 dialogo y mapa, 653/661 narracion, 1913 final).
# Cada historia puede cambiarlos con un bloque [historia.estilos.<nombre>] (mismas claves); `fuente`
# puede ser un .ttf junto al .toml o el nombre de una fuente de Windows.
STYLES = {
    "dialogo":   {"fuente": "arialbd.ttf", "tamano": 17, "interlineado": 22, "ancho": 205,
                  "color": [8, 8, 8], "borde": 0, "color_borde": [0, 0, 0]},
    "narracion": {"fuente": "verdanab.ttf", "tamano": 19, "interlineado": 41, "ancho": 290,
                  "color": [0, 40, 33], "borde": 2, "color_borde": [239, 235, 239]},
}
STYLES["final"] = dict(STYLES["narracion"])


def render(text, style):
    """Una linea que acaba en .png es una imagen propia (RGBA, relativa al .toml) y se usa tal cual.
    Si no, se dibuja con el estilo de STYLES (texto centrado, una linea cada `interlineado` px)."""
    if text.lower().endswith(".png"):           # pagina hecha a mano: la imagen tal cual
        path = text if os.path.isabs(text) else os.path.join(TOML_DIR[0], text)
        return np.array(Image.open(path).convert("RGBA"))
    st = STYLES.get(style, STYLES["dialogo"])
    font = os.path.join(TOML_DIR[0], st["fuente"])
    if not os.path.exists(font):
        font = os.path.join("C:/Windows/Fonts", st["fuente"])
    f, pitch, stroke = capsulas.load_font(font, int(st["tamano"])), int(st["interlineado"]), int(st["borde"])
    lines = capsulas._wrap(text, f, int(st["ancho"]))
    w = int(max(f.getlength(t) for t in lines)) + 8
    img = Image.new("RGBA", (max(16, w), max(16, pitch * len(lines) + 6)), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for k, t in enumerate(lines):
        d.text((w // 2, 3 + pitch * k + pitch // 2), t, font=f, anchor="mm", fill=tuple(st["color"]) + (255,),
               stroke_width=stroke, stroke_fill=tuple(st["color_borde"]) + (255,))
    return np.array(img)


def text_bank(lines, style):
    """#AZT de dialogo: texturas 0..n-1 (DXT3, flags 0x21, +0x28 = 0x40 como las nativas)."""
    return capsulas.build_bank(0, {k: render(t, style) for k, t in enumerate(lines)}, flag28=0x40, tall=True)


# ---------------------------------------------------------------- lectura
def afs_path(us, name):
    return os.path.join(us, name)


def read_entry(us, afs, n, work):
    return rb.Afs(afs_path(us, afs), work).entry(n)


def textures(b):
    out = []
    for k in range(len(capsulas.azt_entries(b))):
        img = capsulas.azt_read(b, k)
        out.append(img)
    return out


def cmd_ver(a):
    us, work = a.us or rb.default_us(), work_dir()
    out = a.salida or os.path.join(HERE, "campanas_textos")
    os.makedirs(out, exist_ok=True)
    for lang, afss in LANG_AFS.items():
        p = afs_path(us, afss[0])
        if not os.path.exists(p):
            continue
        b = read_entry(us, afss[0], a.entrada, work)
        if b[:4] != b"#AZT":
            print("data_usi %d no es un banco de textos (%r)" % (a.entrada, b[:4]))
            return 1
        for k, img in enumerate(textures(b)):
            if img is None:
                continue
            f = os.path.join(out, "%d_%s_%d.png" % (a.entrada, lang, k))
            Image.fromarray(img).save(f)
            print(f)
    return 0


def cmd_escenas(a):
    reg = json.load(open(os.path.join(HERE, "du_codigos.json"), encoding="utf-8"))
    codes = {int(k): v for k, v in reg["codigos"].items()}
    us, work = a.us or rb.default_us(), work_dir()
    for code in range(a.bloque, a.bloque + 100):
        if code not in codes:
            continue
        cont = codes[code]
        b = read_entry(us, "data_cmn.afs", cont, work)
        texts = set()
        battles = []
        if b[:4] == b"#LST":
            n, toff, doff = struct.unpack_from("<3I", b, 4)
            table = [struct.unpack_from("<2I", b, toff + 8 * i) for i in range(n)]
            for i, (rel, cnt) in enumerate(table):
                nxt = table[i + 1][0] if i + 1 < n else len(b) - doff
                hsize = nxt - rel - 12 * cnt
                hsize = hsize if hsize in (16, 28) else 28
                for k in range(cnt):
                    o = doff + rel + hsize + 12 * k
                    if o + 12 > len(b):
                        break
                    op, p1, p2 = struct.unpack_from("<3I", b, o)
                    if op == 10 and (p2 >> 16) & 0xFF == 1:
                        texts.add(p2 & 0xFFFF)
                    if op == 16:
                        battles.append(p2)
        print("codigo %d -> contenedor %d  textos data_usi %s  combates %s" % (
            code, cont, sorted(texts) or "-", battles or "-"))
    return 0


# ---------------------------------------------------------------- montaje
def word(v):
    """Evento de escena: numero, o "codigo.entrada" (la palabra codigo << 16 | entrada)."""
    if isinstance(v, str) and "." in v:
        code, entry = v.split(".")
        return (int(code) << 16) | int(entry)
    return int(v) & 0xFFFFFFFF


def pack_event(ev):
    """(op, p1, p2, *mas): op 24/48 (marca del mapa: icono, tipo, x, y, z, escala[, 0]), 35, 44
    (posicion de salida) y 29 (objeto del mapa por nombre: texto de hasta 16 letras y estado)
    llevan palabras de mas; un texto se guarda como 16 bytes."""
    op, p1, p2, *more = ev
    out = struct.pack("<3I", op, int(p1) & 0xFFFFFFFF, word(p2) if not isinstance(p2, str) or "." in p2 else 0)
    if isinstance(p2, str) and "." not in p2:      # op 29: p2 es el nombre
        out = struct.pack("<2I", op, int(p1) & 0xFFFFFFFF) + p2.encode("ascii")[:16].ljust(16, b"\0")
    return out + b"".join(struct.pack("<I", int(v) & 0xFFFFFFFF) for v in more)


def lst_replace(b, entry, hdr, events):
    """#LST con la entrada `entry` cambiada entera (las demas se copian tal cual, aunque tengan
    eventos de tamano variable)."""
    n, toff, doff = struct.unpack_from("<3I", b, 4)
    table = [list(struct.unpack_from("<2I", b, toff + 8 * i)) for i in range(n)]
    start = doff + table[entry][0]
    end = doff + table[entry + 1][0] if entry + 1 < n else len(b)
    new = struct.pack("<%dI" % len(hdr), *hdr) + b"".join(pack_event(e) for e in events)
    out = bytearray(b[:start] + new + b[end:])
    delta = len(new) - (end - start)
    table[entry][1] = len(events)
    for i in range(entry + 1, n):
        table[i][0] += delta
    for i, (r, c) in enumerate(table):
        struct.pack_into("<2I", out, toff + 8 * i, r, c)
    return bytes(out)


# u32 por evento (los demas, 3); 3, 30, 31, 34, 41 y 49 medidos por el frente DU (docs/14_dragon_universe/02)
MAP_EVENT_WORDS = {3: 4, 24: 9, 29: 7, 30: 7, 31: 6, 34: 4, 35: 4, 36: 5, 41: 9, 44: 5, 48: 10, 49: 4, 54: 3}
BAD_OPS = {25, 26, 27, 28, 32, 33, 37}   # leen pila sin inicializar (frente DU): no se usan


def lst_events(b, entry):
    """Eventos de una entrada de un #LST con su tamano real (los del mapa son mas largos), como tuplas."""
    n, toff, doff = struct.unpack_from("<3I", b, 4)
    rel, cnt = struct.unpack_from("<2I", b, toff + 8 * entry)
    end = doff + (struct.unpack_from("<I", b, toff + 8 * (entry + 1))[0] if entry + 1 < n else len(b) - doff)
    for hs in (28, 16):
        p, out = doff + rel + hs, []
        for _ in range(cnt):
            w = MAP_EVENT_WORDS.get(struct.unpack_from("<I", b, p)[0], 3) if p + 4 <= end else 0
            if not w or p + 4 * w > end:
                break
            out.append(struct.unpack_from("<%dI" % w, b, p))
            p += 4 * w
        if len(out) == cnt and p == end:
            return out
    raise ValueError("no se pueden leer los eventos de la entrada %d" % entry)


def lst_build(entries):
    """#LST (LE) a partir de [(cabecera de 4 o 7 u32, [(op, p1, p2)][, datos de detras])]."""
    n = len(entries)
    doff = (0x10 + 8 * n + 15) & ~15
    table, data = [], b""
    for e in entries:
        hdr, events = e[0], e[1]
        table.append((len(data), len(events)))
        data += struct.pack("<%dI" % len(hdr), *hdr)
        data += b"".join(pack_event(ev) for ev in events)
        data += e[2] if len(e) > 2 else b""
    out = b"#LST" + struct.pack("<3I", n, 0x10, doff) + b"".join(struct.pack("<2I", *t) for t in table)
    return out + b"\0" * (doff - len(out)) + data


def lst_append(b, entries):
    """#LST con `entries` [(cabecera, eventos)] anadidas al final; las suyas se copian byte a byte (sus
    eventos del mapa son de tamano variable: no se releen)."""
    n, toff, doff = struct.unpack_from("<3I", b, 4)
    table = [struct.unpack_from("<2I", b, toff + 8 * i) for i in range(n)]
    data = bytes(b[doff:])
    for hdr, events in entries:
        table.append((len(data), len(events)))
        data += struct.pack("<%dI" % len(hdr), *hdr) + b"".join(pack_event(e) for e in events)
    nd = (0x10 + 8 * len(table) + 15) & ~15
    out = b"#LST" + struct.pack("<3I", len(table), 0x10, nd) + b"".join(struct.pack("<2I", *t) for t in table)
    return out + bytes(nd - len(out)) + data


def lst_entries(b):
    """[(cabecera, eventos)] de un #LST (LE), para anadirle entradas."""
    n, toff, doff = struct.unpack_from("<3I", b, 4)
    table = [struct.unpack_from("<2I", b, toff + 8 * i) for i in range(n)]
    out = []
    for i, (rel, cnt) in enumerate(table):
        nxt = table[i + 1][0] if i + 1 < n else len(b) - doff
        hsize = nxt - rel - 12 * cnt
        hsize = hsize if hsize in (16, 28) else 28
        o = doff + rel
        out.append((list(struct.unpack_from("<%dI" % (hsize // 4), b, o)),
                    [struct.unpack_from("<3I", b, o + hsize + 12 * k) for k in range(cnt)],
                    bytes(b[o + hsize + 12 * cnt:doff + nxt])))
    return out


TEXT_BLOCK = {}   # [textos] del campana.toml: base y entradas del bloque de texto propio (si es automatico)


def native_count(us, afs=None):
    """Entradas del AFS del juego (u32 en +4): las del mod van detras (el runtime las sirve). Sin `afs`, la
    mayor de los AFS de textos que haya (data_usi en la US; la EU trae data_eng, data_spn...)."""
    names = [afs] if afs else [x for afss in LANG_AFS.values() for x in afss]
    counts = []
    for n in names:
        if os.path.exists(afs_path(us, n)):
            with open(afs_path(us, n), "rb") as f:
                counts.append(struct.unpack("<I", f.read(8)[4:8])[0])
    if not counts:
        raise SystemExit("no encuentro los textos del juego (%s) en %s" % (", ".join(names), us))
    return max(counts)


def reservar_textos(mods, mod, need, us, keep=()):
    """Bloque de `need` entradas de texto propias de esta campana, detras de las del juego y sin
    pisar las de otros mods (personajes, otras campanas). Fijo por nombre de mod (crc32) y estable:
    al rehacer, si su bloque anterior (campana.toml [textos]) sigue libre, se reusa. Los textos que
    el mod ya no usa (otro bloque, o los de `textos_desde` al pasar a automatico) van a mods_respaldo;
    `keep`: sus [[texto]] a mano."""
    import zlib  # noqa: PLC0415
    native = native_count(us)
    me = os.path.basename(mod)
    old = {}
    try:
        old = tomllib.load(open(os.path.join(mod, "campana.toml"), "rb")).get("textos", {})
    except (OSError, ValueError, tomllib.TOMLDecodeError):
        pass
    # ocupadas: las de cualquier otro mod, tambien desactivado (al activarlo chocarian), y de este solo sus
    # [[texto]] a mano (escritos antes en cmd_montar); el resto de las suyas se rehace o va a mods_respaldo
    taken = set()
    for d in os.listdir(mods) if os.path.isdir(mods) else []:
        p = os.path.join(mods, d)
        if d.startswith(".") or not os.path.isdir(p):
            continue
        for afss in LANG_AFS.values():
            for afs in afss:
                for e in os.listdir(os.path.join(p, "us", afs)) if os.path.isdir(os.path.join(p, "us", afs)) else []:
                    if e.isdigit() and (d != me or int(e) in keep):
                        taken.add(int(e))
    # las 1024 primeras tras las del juego quedan para _roster (nombres de personajes nuevos, crece al
    # anadir personajes); cada campana empieza en un hueco de 256 que depende de su nombre
    lo = native + 1024
    # tope medido 2026-10-15: con el bloque en 14997 el juego leyo un #SPX en vez del texto y se cerro; hasta
    # 5691 bien. Se queda por debajo de 8192 (ponytail: tope prudente; subirlo solo midiendo 8192..14996)
    hi = min(native + 0x4000, 8192)
    free = lambda b: b >= lo and b + need <= hi and not any(b + k in taken for k in range(need))
    b = None
    try:
        if int(old.get("entradas", 0)) >= need and free(int(old["base"])):
            b = int(old["base"])
    except (ValueError, KeyError):
        pass
    if b is None:
        slots = max(1, (hi - lo) // 256)
        b = lo + 256 * (zlib.crc32(me.encode("utf-8")) % slots)
        for _ in range(slots + 1):
            if free(b):
                break
            b += 256
            if b + need > hi:
                b = lo
        else:
            raise SystemExit("no quedan entradas de texto libres para %s (demasiados mods con textos)" % me)
    # los textos que ya no usa (bloque anterior, o los fijos de `textos_desde`) no se quedan en el mod
    # ocupando entradas (chocarian con los nombres de _roster): van a mods_respaldo
    import shutil  # noqa: PLC0415
    import time  # noqa: PLC0415
    bak = os.path.join(os.path.dirname(os.path.abspath(mods)), "mods_respaldo",
                       "%s_textos_%s" % (me, time.strftime("%Y%m%d_%H%M%S")))
    for afss in LANG_AFS.values():
        for afs in afss:
            d = os.path.join(mod, "us", afs)
            for e in os.listdir(d) if os.path.isdir(d) else []:
                if e.isdigit() and int(e) not in keep and not b <= int(e) < b + need:
                    os.makedirs(os.path.join(bak, afs), exist_ok=True)
                    shutil.move(os.path.join(d, e), os.path.join(bak, afs, e))
    return b


def portrait_entry(native, art):
    """Retrato de dialogo (como data_cmn 1416): copia del nativo con `art` (PIL RGBA, se encaja en 416x408) en las dos
    texturas (hablando / callado) y el resto transparente."""
    import zlib  # noqa: PLC0415
    art = art.convert("RGBA")
    if art.size != (416, 408):
        art = art.resize((416, 408), Image.LANCZOS)
    b = bytearray(native)
    px = np.zeros((1024, 1024, 4), np.uint8)
    px[:408, :416] = np.array(art)
    raw = np.ascontiguousarray(px[..., [2, 1, 0, 3]]).tobytes()
    for e in capsulas.azt_entries(bytes(b)):
        do, ds = struct.unpack(">II", b[e + 0x14:e + 0x1C])
        assert ds == 128 + len(raw), "retrato nativo con otro formato (%d)" % ds
        b[do + 128:do + 128 + len(raw)] = raw
    struct.pack_into(">I", b, 0x1C, zlib.crc32(bytes(b[0xA0:])) & 0xFFFFFFFF)
    return bytes(b)


def story(cfg, us, mod, work):
    """[historia] + [[capitulo]]: historia como las del juego. Cada capitulo es un combate con su
    dialogo en el escenario antes (op 21) y despues (la escena de victoria, que deja en el mapa la
    marca del combate siguiente). Un capitulo con `saga = true` abre saga: al ganar el anterior sale
    el cierre de la saga (`cierre_es`: panel, "guardar?" y mapa de `planeta`, 0 Tierra / 1 Namek) y la
    introduccion de la nueva (`es`: panel, salida `salida` y la marca de su primer combate). La
    primera saga la abre la introduccion del personaje ([[escena]] con la marca de combates.codigo.0).
    El ultimo capitulo sin combate es el final. Devuelve las entradas escritas."""
    h, chapters = cfg["historia"], cfg.get("capitulo", [])
    reg = json.load(open(os.path.join(HERE, "du_codigos.json"), encoding="utf-8"))["codigos"]
    codes_nuevos = cfg.setdefault("codigos", {})

    def cont(code):
        """Contenedor de un codigo: el del juego o, para los codigos virtuales (sagas 4+ de una casilla: el bloque
        5000-6199 solo da 3-4 sagas), un contenedor propio que se registra en [codigos] (campaigns.cpp lo traduce)."""
        if str(code) in reg:
            return int(reg[str(code)])
        if str(code) not in codes_nuevos:
            codes_nuevos[str(code)] = nueva_entrada()
        return int(codes_nuevos[str(code)])
    code = int(h["codigo"])                    # 5122: saga 1 (Namek), casilla 22
    slot, saga0 = code % 100, (code - 5000) // 100
    dlg = h["dialogos"]
    if "textos_desde" in h and "textos_desde" in dlg:   # a mano, como las primeras historias (Freezer 3600/3620)
        txt, dtxt = int(h["textos_desde"]), int(dlg["textos_desde"])
        narr, cierre = (lambda j: txt + j), (lambda j: txt + 200 + j)
        talk_b, vict_b, mapa_b, lleg_b = (lambda i: dtxt + i), (lambda i: dtxt + 40 + i), (lambda i: dtxt + 80 + i), \
            (lambda i: dtxt + 120 + i)
        extra_b = lambda j: txt + 400 + j
    else:
        # automatico: un bloque propio de 6 grupos (narracion, cierre de saga, charla, victoria, vuelta al
        # mapa, llegada) de len(capitulos)+1 entradas cada uno, detras de las del juego
        st = len(chapters) + 1
        if "textos_desde" in h or "textos_desde" in dlg:
            rb.log("aviso: `textos_desde` va en [historia] y en dialogos a la vez; con uno solo se reparten solos")
        # y uno por cada imagen de mas de las narraciones con `imagenes` (cada parte lleva su banco)
        xn = sum(max(0, len(c.get(k) or []) - 1) for c in chapters for k in ("imagenes", "cierre_imagenes"))
        base = reservar_textos(os.path.dirname(mod), mod, 6 * st + xn, us, {int(t["entrada"]) for t in cfg.get("texto", [])})
        TEXT_BLOCK.update(base=base, entradas=6 * st + xn)
        extra_b = lambda j: base + 6 * st + j
        narr, cierre = (lambda j: base + j), (lambda j: base + st + j)
        talk_b, vict_b, mapa_b, lleg_b = (lambda i: base + 2 * st + i), (lambda i: base + 3 * st + i), \
            (lambda i: base + 4 * st + i), (lambda i: base + 5 * st + i)
        rb.log("textos de la historia: entradas %d-%d (automatico)" % (base, base + 6 * st - 1))
    for k, v in h.get("estilos", {}).items():   # estilos de texto propios de esta historia
        STYLES.setdefault(k, dict(STYLES["dialogo"])).update(v)
    s_narr, s_end = int(h.get("guion_narracion", 0xADB)), int(h.get("guion_final", 2790))
    s_fight, s_after = int(h.get("guion_combate", 2866)), int(h.get("guion_tras_combate", 2495))
    panel = int(h.get("fondo", 1411))
    # C4 (sagas 4-8 del Heroe): las 3 primeras sagas de la historia usan los codigos del juego (5100+, 5500+, 5900+...);
    # de la 4.a en adelante, codigos VIRTUALES (el juego no los calcula: son datos de las escenas): mapa 12000+, combate
    # 12500+ (campaigns.cpp los reconoce), dialogo 13000+, introduccion 14001+, cierre 14000+, de 100 en 100 por saga.
    # H4: tras los 3 bloques desde saga0 (5100-5300) queda el bloque 0 (5000/5400/5800 + casilla, introduccion 100),
    # libre en las casillas 35-37 y dentro del rango que el motor DU reproduce: es la saga 4 REAL. Lo demas, virtual.
    blk = lambda s: s if s - saga0 < 3 else s - saga0 - 3
    virt = lambda s: s >= 2 * saga0 + 3
    vj = lambda s: s - 2 * saga0 - 3
    mapc = lambda s: 12000 + 100 * vj(s) + slot if virt(s) else 5000 + 100 * blk(s) + slot
    bcode = lambda s: 12500 + 100 * vj(s) + slot if virt(s) else int(h["combates"]["codigo"]) + 100 * (blk(s) - saga0)
    dcode = lambda s: 13000 + 100 * vj(s) + slot if virt(s) else int(dlg["codigo"]) + 100 * (blk(s) - saga0)
    intro_c = lambda s: 14001 + 2 * vj(s) if virt(s) else 100 + 2 * blk(s)
    outro_c = lambda s: 14000 + 2 * vj(s) if virt(s) else 99 + 2 * blk(s)
    n = 0

    def text(bank, c, key, style):
        """`paginas` (p. ej. dialogo_paginas = ["1252.1", 0, "1252.2"]): paginas del juego ("banco.pagina"
        de data_usi, cada idioma con la suya: texto y traduccion originales) mezcladas con frases propias
        (numero = frase de dialogo_<idioma>). Sin `paginas`, todas las frases propias."""
        nonlocal n
        pages = c.get(key + "paginas")
        for lang, afss in LANG_AFS.items():
            # `idioma` en [historia]: ese idioma en todos (p. ej. "en" mientras falten traducciones)
            lines = c.get(key + h.get("idioma", lang)) or c.get(key + "en") or c.get(key + "es")
            for afs in afss:
                if not os.path.exists(afs_path(us, afs)):
                    continue
                if pages:
                    imgs = {}
                    for k, p in enumerate(pages):
                        if isinstance(p, str):
                            nb, pg = (int(v) for v in p.split("."))
                            imgs[k] = capsulas.azt_read(read_entry(us, afs, nb, work), pg)
                        else:
                            imgs[k] = render(lines[int(p)], style)
                    data = capsulas.build_bank(0, imgs, flag28=0x40, tall=True)
                else:
                    data = text_bank(lines, style)
                write_entry(mod, afs, bank, data, work)
                n += 1
        return len(pages) if pages else len(c.get(key + "en") or c.get(key + "es"))

    music = int(h.get("musica_narracion", 135269))   # 2.4197, la de las introducciones del juego
    # imagenes propias: desde `imagenes_desde` (a mano) o en el bloque propio de data_cmn (conviven campanas)
    pics = [int(h["imagenes_desde"])] if "imagenes_desde" in h else []
    xb = [0]   # bancos de texto de mas ya usados (partes de las narraciones con `imagenes`)

    def new_pic():
        if not pics:
            return nueva_entrada()
        pics[0] += 1
        return pics[0] - 1

    # C4: `retrato_heroe` = png RGBA 416x408 (o vacio/ausente = retrato transparente): el retrato del protagonista en los
    # dialogos (en vez del de Goku), con el formato de los nativos (data_cmn 1416: 2 texturas BGRA 1024x1024, 416x408)
    if "retrato_heroe" in h:
        rp = str(h["retrato_heroe"])
        rp = rp if os.path.isabs(rp) else os.path.join(TOML_DIR[0], rp)
        art = Image.open(rp).convert("RGBA") if os.path.isfile(rp) else Image.new("RGBA", (416, 408), (0, 0, 0, 0))
        ent = new_pic()
        write_entry(mod, "data_cmn.afs", ent, portrait_entry(read_entry(us, "data_cmn.afs", 1416, work), art), work)
        for c in chapters:
            if "retratos" in c:
                c["retratos"] = [c["retratos"][0], ent]
        rb.log("retrato del protagonista: entrada %d (%s)" % (ent, "propio" if os.path.isfile(rp) else "vacio"))

    writer = []

    def voice(v):
        if isinstance(v, int) or (isinstance(v, str) and v.strip().isdigit()):
            return 0x20000 | int(v)
        if isinstance(v, str) and os.path.splitext(v)[1].lower() in (".wav", ".mp3", ".ogg", ".flac"):
            import sb_voces  # noqa: PLC0415
            import voces  # noqa: PLC0415
            if not writer:
                writer.append(voces.VoiceWriter(us, mod))
            path = v if os.path.isabs(v) else os.path.join(TOML_DIR[0], v)
            pcm = sb_voces.ff_pcm(open(path, "rb").read(), sb_voces.ADX_RATE, os.path.splitext(v)[1])
            adx = sb_voces.adx_encode(pcm)
            return 0x20000 | writer[0].add(adx, adx)
        return word(v)

    def picture(img):
        """`imagen`: numero = imagen de data_cmn del juego (1411 panel verde, 1536 Namek, 1549 la Tierra);
        texto = un .png/.webp/.jpg/.avif propio (660x420, recorte centrado); vacio = el panel de la historia."""
        if img in (None, ""):
            return panel
        if isinstance(img, int) or str(img).strip().isdigit():
            return int(img)
        path = os.path.join(TOML_DIR[0], img)
        if not os.path.isfile(path):   # sin la imagen (p. ej. el Kit no lleva arte de terceros): el panel del juego
            rb.log("aviso: no encuentro %s; se usa el panel del juego" % img)
            return panel
        pic = new_pic()
        write_entry(mod, "data_cmn.afs", pic, png2pic.build_picture(Image.open(path)), work)
        return pic

    def narration(bank, c, key):
        """Panel de narracion: [(cabecera, eventos)], una escena por imagen. `imagen` (o `cierre_imagen`) = una
        imagen para todas las paginas. `imagenes` (o `cierre_imagenes`) = [{imagen = ..., paginas = N}, ...]:
        cada imagen con sus N paginas siguientes (y sus voces); el juego las encadena (op 20) como 101 -> 102."""
        parts = c.get(key + "imagenes") or [{"imagen": c.get(key + "imagen")}]
        out, a = [], 0
        for j, p in enumerate(parts):
            k = int(p.get("paginas", 1)) if j + 1 < len(parts) else 1 << 30   # la ultima: las que queden
            sub = {"p_" + x: c[key + x][a:a + k] for x in list(LANG_AFS) + ["voz"] + ["paginas"] * (len(parts) == 1) if c.get(key + x)}
            a += k
            if j:
                bank = extra_b(xb[0])
                xb[0] += 1
            pages = text(bank, sub, "p_", "narracion")
            pic = picture(p.get("imagen"))
            # locucion: `voz` (o `cierre_voz`), una por frase como las del juego (14, pagina, 2.N: la
            # entrada N de adx_usa/adx_jpn; la intro de Namek trae 14,1..3 = 2.1231..1233). Vale un
            # numero (entrada ADX del juego), "2.N" o un fichero de audio propio (.wav/.mp3/.ogg)
            voices = [(14, n + 1, voice(v)) for n, v in enumerate(sub.get("p_voz", [])) if str(v).strip() not in ("", "-1")]
            out.append(([9, 3, pages + 1, 2, 0, 0, s_narr],
                        [(10, 0, pic), (10, 1, 0x10000 | bank), (10, 2, 0x10000 | bank), (13, 0, music),
                         (11, 0, 1414), (12, 0, 1415), (11, 1, 1414), (12, 1, 1415)] + voices))
        return out

    def talk(bank, c, key):
        """Dialogo en el escenario (cabecera de 16 B): retratos (izquierda, derecha; hablan por
        turnos, empieza el de la izquierda) y una frase por pagina, sin voz."""
        nl = text(bank, c, key, "dialogo")
        # `retratos_dialogo` = [izquierda, derecha]: habla primero el de la izquierda (en el juego, 5800.1
        # pone a Goku a la izquierda cuando empieza el). Por defecto `retratos` (rival, tu personaje)
        a, b = c.get("retratos_dialogo", c["retratos"])
        return ([4 + 2 * nl, 5, nl + 1, 5],
                [(10, 0, int(a)), (10, 1, int(b)), (10, 2, 0x1026E), (10, 3, 0x10000 | bank), (10, 4, 0x10000 | bank),
                 (13, 0, 0xFFFFFFFF)] + [ev for i in range(4) for ev in ((11, i, 1556), (12, i, 1557))] +
                [(11, 4, 0x1026D), (12, 4, 0x1026E)] + voices(c, key))

    def voices(c, key):
        """`voz` por pagina (dialogo_voz, victoria_voz), como las del juego (5800.0: 14, pagina 1.., 2.N);
        0 o -1 = pagina sin voz."""
        return [(14, k + 1, voice(v)) for k, v in enumerate(c.get(key + "voz", [])) if str(v).strip() not in ("", "-1", "0")]

    # saga y posicion de cada combate
    s, k, place = saga0, {}, []
    for c in chapters:
        if "combate" in c:
            if c.get("saga") and place:
                s += 1
            k[s] = k.get(s, -1) + 1
        place.append((s, k.get(s, 0)))
    bword = lambda i: (bcode(place[i][0]) << 16) | (2 * place[i][1])
    # casilla 0 como las marcas sueltas del juego; el op 24 siempre lleva 9 palabras
    mark = lambda i: [24, 0, bword(i)] + list(chapters[i].get("marca", [1, 2, 5462, 54, -5448, 1000]))
    fights, talks, intros, maps = {}, {}, {}, {}
    for i, c in enumerate(chapters):
        if "combate" not in c:
            continue
        s, kk = place[i]
        if kk == 0:
            # llegada al mapa de la saga: como el juego (102.0 -> 5100.0, 104.0 -> 5200.4), la
            # introduccion pasa por una escena del mapa (una frase de tu personaje, `llegada_es`); sin
            # ella el personaje salia volando a ras del suelo. Es la entrada 0 de las escenas del mapa
            # de la saga: la introduccion de la primera saga ([[escena]]) acaba en (20, 0, "codigo.0")
            bank = lleg_b(i)
            text(bank, {"mapa_es": c.get("llegada_es", ["¡Adelante!"]), "mapa_en": c.get("llegada_en", ["Let's go!"])},
                 "mapa_", "dialogo")
            maps.setdefault(s, []).append(([3, 3, 2, 2, 0, 0, 2899],
                                           [(10, 0, int(c.get("fondo_dialogo", h.get("fondo_dialogo", 1536)))),
                                            (10, 1, int(c["retratos"][1])), (10, 2, 0x10000 | bank), (13, 0, 0xFFFFFFFF),
                                            (11, 0, 1556), (12, 0, 1557), (11, 1, 1556), (12, 1, 1557)] + ([(170, 0, 0)] if h.get("esferas_en_mapa") else [])))
        if "forma_jugador" in c:                # tu personaje empieza transformado (el juego lo lee)
            START_FORMS[bword(i)] = int(c["forma_jugador"])
        # modelo y traje en el mapa mientras ese combate espera (Freezer: modelo 5 = Mecha, traje 2 =
        # en su nave, el modelo 193); [historia] da el de todos los capitulos que no digan otro
        for key, dst in (("modelo_mapa", MAP_MODELS), ("traje_mapa", MAP_SUITS), ("animacion_mapa", MAP_ANIMS)):
            v = c.get(key, h.get(key))
            if v is not None and v != "":
                dst[bword(i)] = int(v)
                if kk == 0:   # el mapa crea al personaje con la introduccion de la saga pendiente (102.22, 104.22...)
                    dst[(intro_c(s) << 16) | slot] = int(v)
                    dst[mapc(s) << 16] = int(v)   # o con su llegada al mapa

        if "traje_rival" in c:                  # traje del rival (0 el normal; Vegeta 3 = armadura de Namek)
            RIVAL_SUITS[bword(i)] = int(c["traje_rival"])
        if "plantilla" in c or "plantilla" in h["combates"]:
            # los #BPC sin usar (654-667) son de un formato anterior (1216 B; el juego lee 1248 B) y
            # dan escenario equivocado y cuelgues: el combate se rehace sobre uno del juego
            # (`plantilla`: su escenario, +0x90) con el rival (+0x10) del viejo y `forma_rival` (+0x3c)
            old = read_entry(us, "data_cmn.afs", int(c["combate"]), work)
            new = bytearray(read_entry(us, "data_cmn.afs", int(c.get("plantilla", h["combates"].get("plantilla", 0))), work))
            rival = int(c.get("rival", struct.unpack_from(">I", old, 0x10)[0]))
            form = int(c.get("forma_rival", 0))
            struct.pack_into(">I", new, 0x10, rival)
            if "escenario" in c:                # escenario propio (0..16, el del juego en +0x90); sin el, el de la plantilla
                struct.pack_into(">I", new, 0x90, int(c["escenario"]))
            # 8 registros de 0x98 B desde 0x10 (uno por nivel): forma en +0x2c y +0x58 de cada uno y
            # sus capsulas (+0x38 cuenta) fuera: las de la plantilla salian como capsula gris al ganar
            # nivel del rival y experiencia (+0x64, +0x60 de cada registro): el juego elegia el ultimo
            # (nivel 99, 100.000 de experiencia); todos pasan a uno progresivo por capitulo (0..4, o
            # `nivel` en el capitulo)
            nb = sum(1 for x in chapters if "combate" in x)
            lv = int(c.get("nivel", min(4, len([x for x in chapters[:i] if "combate" in x]) * 5 // max(1, nb))))
            lv = max(0, min(7, lv))             # solo hay 8 registros (la historia del Heroe pedia nivel 8)
            exp, level = struct.unpack_from(">2I", new, 0x10 + 0x98 * lv + 0x60)
            for r in range(0x10, 0x10 + 8 * 0x98, 0x98):
                struct.pack_into(">2I", new, r + 0x60, exp, level)
                struct.pack_into(">I", new, r + 0x2c, form)
                struct.pack_into(">I", new, r + 0x58, form)
                struct.pack_into(">I", new, r + 0x38, 0)
            write_entry(mod, "data_cmn.afs", int(c["combate"]), bytes(new), work)
            n += 1
        talks.setdefault(s, []).append(talk(talk_b(i), c, "dialogo_"))
        fights.setdefault(s, []).append(([0, 0, 0, 0, 0, 0, s_fight],
                                         [(16, 0, int(c["combate"])), (15, 0, s_after),
                                          (23, 0, bword(i)),                          # quita su marca
                                          ] + ([] if os.environ.get("HEROE_SIN_CHARLA") and s > 1 else [(21, 0, (dcode(s) << 16) | kk)]) + [   # dialogo al empezar
                                          (20, 0, bword(i) + 1)]))                    # gana: escena de victoria
        nxt = chapters[i + 1] if i + 1 < len(chapters) else None
        if nxt is None:
            tail = []
        elif "combate" not in nxt:                      # el final (entrada nueva del contenedor de finales)
            # se rehace una entrada de final que no usa nadie (`entrada`, 5 = 3 paginas) con su cabecera
            # (y su guion) original: una entrada nueva al final del contenedor cuelga el juego
            fin = h["final"]
            # con la cabecera (guion) y la musica del final de Goku (`estilo`, 1728.0: logo, texto y
            # creditos); 2 paginas. Con el guion de las entradas 4-5 la pantalla se quedaba en negro
            # `fondo`: "heroe" (el del final de Goku, 1728.0: cielo azul, imagenes 1563/1564) o "villano"
            # (el de Broly, 1729.20: imagenes 1561/1562 y su musica); cabecera (guion) y musica de ese final
            k = int(fin.get("entrada", 39))
            b = read_entry(us, "data_cmn.afs", int(fin["contenedor"]), work)
            villain = fin.get("fondo", "heroe") == "villano"
            src = lst_entries(read_entry(us, "data_cmn.afs", 1729, work))[20] if villain else lst_entries(b)[int(fin.get("estilo", 0))]
            hdr = list(src[0])
            fin_music = [e for e in src[1] if e[0] == 13][0]   # no `music`: narration() lo usa (la musica de los paneles)
            pic = (1561, 1562) if villain else (1563, 1564)
            text(narr(i + 1), nxt, "", "final")
            tail = [(20, 0, (int(fin["codigo"]) << 16) | k)]
            b = lst_replace(b, k, hdr, [(10, 0, 0x10000 | narr(i + 1)), (10, 1, 0x10000 | narr(i + 1)),
                                        tuple(fin_music), (11, 0, pic[0]), (12, 0, pic[1]), (11, 1, pic[0]), (12, 1, pic[1])])
            write_container(mod, int(fin["contenedor"]), b, work)
            n += 1
        elif nxt.get("saga") and not virt(place[i + 1][0]):   # cierre de saga -> guardar -> introduccion (las virtuales 4-8: abajo, vuelta al mapa)
            s2 = place[i + 1][0]
            intro, outro = intro_c(s2), outro_c(s2)
            if str(outro) in reg:   # H4: la 99 (cierre del bloque 0) no existe en el juego; sin 103 de todos modos (C8)
                intros[outro] = (narration(cierre(i + 1), nxt, "cierre_"),
                             ([(20, 0, (intro << 16) | slot)] + ([(47, 0, int(nxt.get("planeta", 0)))] if os.environ.get("HEROE_PLANETA_SIN_GUARDAR") else [])) if h.get("sin_guardar") else
                             [(20, 0, (intro << 16) | slot), (50, 0, 0), (47, 0, int(nxt.get("planeta", 0)))])
            parts = narration(narr(i + 1), nxt, "")
            # `objetos_mapa_de` = "104.0": los objetos del mapa (op 29, por nombre) de una introduccion del
            # juego, como hace la de Namek a mano. Sin ellos R1 en la Tierra cerraba el juego (2026-10-12)
            objs = []
            if nxt.get("objetos_mapa_de"):
                oc, oe = str(nxt["objetos_mapa_de"]).split(".")
                objs = [e for e in lst_events(read_entry(us, "data_cmn.afs", cont(int(oc)), work), int(oe)) if e[0] == 29]
            _ops = [(22, 0, 0), tuple([44, 0] + list(nxt["salida"]))] + objs +                    [(54, 0, 0), tuple(mark(i + 1)), (20, 0, mapc(s2) << 16)]   # a su llegada al mapa
            _quitar = {int(x) for x in os.environ.get("HEROE_QUITAR_OPS", "").split(",") if x.strip()}   # C7: pruebas de bisecacion
            intros[intro] = (parts, [e for e in _ops if e[0] not in _quitar])
            tail = [(22, 0, 0)] + ([(47, 0, int(nxt.get("planeta", 0)))] if h.get("sin_guardar") and not os.environ.get("HEROE_SIN_PLANETA") and int(nxt.get("planeta", 0)) != next((int(x["planeta"]) for x in reversed(chapters[:i + 1]) if "planeta" in x), 1) else []) + [(20, 0, ((outro if os.environ.get("HEROE_CON_103") else intro) << 16) | slot)]   # C8: SIN la escena de cierre 103 (con ella, el 1.er combate de la saga siguiente se cuelga: la cola de cargas del juego no termina); HEROE_CON_103=1 la deja
        else:                                           # sigue la saga: la marca del combate siguiente
            # y, como el juego (2138.1 -> 5100.5), se vuelve al mapa por una escena del mapa: una frase
            # de tu personaje (`mapa_es`; guion 2899, el de 5000.8); sin ella el dialogo del combate
            # siguiente se cuelga (sub_820AAD30)
            m = nxt.get("marca", [1, 2, 5462, 54, -5448, 1000])
            bank = mapa_b(i)
            line = {"mapa_es": c.get("mapa_es", ["Siguiente."]), "mapa_en": c.get("mapa_en", ["Next."])}
            text(bank, line, "mapa_", "dialogo")
            mapk = len(maps.setdefault(s, []))
            maps[s].append(([3, 3, 2, 2, 0, 0, 2899],
                            [(10, 0, int(c.get("fondo_dialogo", h.get("fondo_dialogo", 1536)))), (10, 1, int(c["retratos"][1])),
                             (10, 2, 0x10000 | bank), (13, 0, 0xFFFFFFFF),
                             (11, 0, 1556), (12, 0, 1557), (11, 1, 1556), (12, 1, 1557)]))
            # salida a 500 de la marca (x, z, altura), como el juego: encima la tapa y la toca al aparecer
            tail = [(44, 0, int(m[2]), int(m[4]) + 500, int(h.get("altura_mapa", 400))), (54, 0, 0)] +                    ([] if nxt.get("saga") else [tuple(mark(i + 1))]) + [(20, 0, (mapc(s) << 16) | mapk)]   # sagas 4-8 en desarrollo: sin marca virtual ni intro 14001, vuelve al mapa nativo
        # la escena de victoria es como 2138.1 del juego: cabecera de 28 B con el guion de despues
        # del combate (op 15 + 1) y sus imagenes en orden fondo, jugador, rival (`fondo_dialogo`:
        # 1536 Namek, 1549 la Tierra); con otro orden el guion libera lo que no es y se cuelga
        nl = text(vict_b(i), c, "victoria_", "dialogo")
        rival, player = c["retratos"]
        bank = vict_b(i)
        # las frases van rival, jugador (como dialogo_*) y el guion habla primero con la imagen 1:
        # el rival en la 1 y tu personaje en la 2 para que cada frase salga con su cara
        ev = ([(10, 0, int(c.get("fondo_dialogo", h.get("fondo_dialogo", 1536)))), (10, 1, int(rival)), (10, 2, int(player)),
               (10, 3, 0x10000 | bank), (10, 4, 0x10000 | bank),
               # musica de la charla tras ganar (`musica_dialogo`, por capitulo o en [historia]; 2.4205 como el juego)
               (13, 0, int(c.get("musica_dialogo", h.get("musica_dialogo", 0x2106D))))] +
              [e for k in range(4) for e in ((11, k, 1556), (12, k, 1557))] + [(11, 4, 0x10755), (12, 4, 0x10756)] +
              voices(c, "victoria_"))
        if c.get("premio"):                      # capsula de premio al ganar (op 19, como 2138.x)
            ev.append((19, 0, int(c["premio"])))
        fights[s].append(([4 + 2 * nl, 5, nl + 1, 5, 0, 0, s_after + 1], ev + tail))
    if h.get("intro_de") and chapters and "combate" in chapters[0]:
        # La introduccion de la primera saga de esta casilla, sin [[escena]] a mano (el Heroe): el panel con el texto
        # del primer capitulo (es/en), la salida (`salida`), los objetos del mapa (op 29) de la introduccion `intro_de`
        # ("100.0" = la de Goku en la Tierra), la marca de su primer combate y la llegada al mapa de la saga.
        c0, s0 = chapters[0], place[0][0]
        oc, oe = str(h["intro_de"]).split(".")
        objs = [(29, 0, struct.pack("<4I", *e[2:6]).rstrip(bytes(1)).decode("ascii"), e[6])   # op 29: nombre (16 B) y estado
                for e in lst_events(read_entry(us, "data_cmn.afs", cont(int(oc)), work), int(oe)) if e[0] == 29]
        intros[intro_c(s0)] = (narration(narr(0), c0, ""),
                                [(22, 0, 0), tuple([44, 0] + list(c0["salida"]))] + objs + [(54, 0, 0), tuple(mark(0)),
                                 (20, 0, mapc(s0) << 16)])
    for s in fights:
        write_entry(mod, "data_cmn.afs", cont(bcode(s)), lst_build(fights[s]), work)
        write_entry(mod, "data_cmn.afs", cont(dcode(s)), lst_build(talks[s]), work)
        n += 2
    for s in maps:                       # escenas del mapa (vuelta tras ganar)
        write_entry(mod, "data_cmn.afs", cont(mapc(s)), lst_build(maps[s]), work)
        n += 1
    # introducciones y cierres de saga: la entrada de la casilla en una copia del contenedor (lo comparten
    # todas las historias); las imagenes de mas van en entradas nuevas de la copia, encadenadas con op 20
    for ic, (parts, tail) in intros.items():
        b = read_entry(us, "data_cmn.afs", cont(ic if str(ic) in reg else 106), work)   # virtual: copia de la plantilla 106
        n0 = struct.unpack_from("<I", b, 4)[0]
        chain = [[(20, 0, (ic << 16) | (n0 + j))] for j in range(len(parts) - 1)] + [tail]
        b = lst_replace(b, slot, parts[0][0], parts[0][1] + chain[0])
        if len(parts) > 1:
            b = lst_append(b, [(hdr, ev + ch) for (hdr, ev), ch in zip(parts[1:], chain[1:])])
        write_container(mod, cont(ic), b, work)
        n += 1
    return n


def azt_textures(b):
    """[(entrada, datos, tamano)] de las texturas de un #AZT (indice en +0x14, n en +0x10)."""
    n, idx = struct.unpack_from(">2I", b, 0x10)
    out = []
    for k in range(n):
        o = struct.unpack_from(">I", b, idx + 4 * k)[0]
        out.append((o,) + struct.unpack_from(">2I", b, o + 0x14))
    return out


def portraits(cfg, us, mod, work):
    """[[retrato]] entrada = N: retrato de dialogo (#AZT de 2 texturas 1024x1024 BGRA, como 1463).
    `reparar = true`: los retratos sin terminar del juego (1464, 1465, 1486: Freezer 2.a y 3.a forma,
    Mecha) traen una sola textura y cuelgan el dialogo; se rehacen con la suya en las dos.
    `png = "pack:Nombre"`: el PNG "Nombre.png" de la carpeta Potraits del pack opcional del usuario (retratos_pack.py;
    nunca se reparte; si falta, el de `sin_png`). `png = "x.png"`: un retrato propio (RGBA; el arte se encaja en la caja de 541x530, abajo;
    `escala` 1.0 y `desplazamiento` [x, y] en px para colocarlo)."""
    n = 0
    tpl = read_entry(us, "data_cmn.afs", 1463, work)
    for c in cfg.get("retrato", []):
        out = bytearray(tpl)
        path = (c["png"] if os.path.isabs(c["png"]) else os.path.join(TOML_DIR[0], c["png"])) if c.get("png") else ""
        if str(c.get("png", "")).startswith("pack:"):      # "pack:Zarbon" = Potraits/Zarbon.png del usuario (arte de terceros)
            import retratos_pack  # noqa: PLC0415
            path = retratos_pack.buscar(c["png"][5:], exacto=True) or ""
            if not path:
                rb.log("aviso: el retrato %s no esta en la carpeta Potraits del usuario" % c["png"])
        if path and not os.path.isfile(path):
            # sin el retrato propio (el Kit no lleva arte de terceros): el de Freezer forma final del juego
            # (`sin_png`), para que la entrada exista y el dialogo no se cuelgue
            rb.log("aviso: no encuentro %s; retrato %s = el %s del juego" % (c["png"], c["entrada"], c.get("sin_png", 1466)))
            path = ""
        if path:
            im = Image.open(path).convert("RGBA")
            k = min(541 / im.width, 530 / im.height) * float(c.get("escala", 1.0))
            im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
            canvas = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
            dx, dy = c.get("desplazamiento", [0, 0])      # x, y en px (y positivo = mas abajo)
            canvas.paste(im, ((541 - im.width) // 2 + int(dx), 530 - im.height + int(dy)), im)
            canvas = canvas.crop((0, 0, 1024, 1024))
            pix = np.ascontiguousarray(np.array(canvas)[..., [2, 1, 0, 3]]).tobytes()
            for _, do, ds in azt_textures(tpl):
                out[do + 0x80:do + ds] = pix
        else:
            src = read_entry(us, "data_cmn.afs", int(c.get("sin_png", 1466) if c.get("png") else c["entrada"]), work)
            (so, sd, ss), = azt_textures(src)[:1]
            for o, do, ds in azt_textures(tpl):
                if ss != ds:
                    raise SystemExit("retrato %s: textura de %d bytes" % (c["entrada"], ss))
                out[do:do + ds] = src[sd:sd + ss]
                out[o + 0x08:o + 0x14] = src[so + 0x08:so + 0x14]    # caja del arte
        write_entry(mod, "data_cmn.afs", int(c["entrada"]), bytes(out), work)
        n += 1
    return n


CLEANED = {}   # panel -> sus bytes ya limpios ([[panel_limpio]]), para las imagenes que falten


def clean_panel(cfg, us, mod, work):
    """[[panel_limpio]] cmn = N, filas = R: la textura N de data_cmn (DXT, sin tiling) con sus R
    primeras filas tapadas por la fila de bloques de debajo (la etiqueta de pruebas que dejaron
    los desarrolladores en el panel verde de la introduccion de Freezer)."""
    import texturas_originales as tex
    n = 0
    for c in cfg.get("panel_limpio", []):
        b = bytearray(read_entry(us, "data_cmn.afs", int(c["cmn"]), work))
        for off, w, hgt, fmt, base in tex.dds_textures(bytes(b))[:1]:
            if fmt not in ("DXT3", "DXT5"):
                raise SystemExit("panel_limpio: %s no es DXT3/DXT5" % fmt)
            row = (w // 4) * 16                       # bytes por fila de bloques de 4x4
            src = (int(c.get("filas", 17)) + 3) // 4  # primera fila de bloques limpia
            data = off + 128
            for by in range(src):
                b[data + by * row:data + (by + 1) * row] = b[data + src * row:data + (src + 1) * row]
        write_entry(mod, "data_cmn.afs", int(c["cmn"]), bytes(b), work)
        CLEANED[int(c["cmn"])] = bytes(b)
        n += 1
    return n


def map_events(cfg, us, mod, work):
    """[historia] eventos_mapa = { codigos = [2100], retrato = 1463, desde = 4600 }: los eventos del
    mapa que comparte el juego (2100 = "All right, I found something!", con la cara de Goku 1416)
    con el retrato del protagonista. Se copian a entradas nuevas de data_cmn (desde `desde`) y el
    juego las usa solo mientras se juega una historia de este mod ([codigos_historia]); las del juego
    no cambian para nadie mas."""
    ev = cfg.get("historia", {}).get("eventos_mapa")
    if not ev:
        return 0
    reg = json.load(open(os.path.join(HERE, "du_codigos.json"), encoding="utf-8"))["codigos"]
    old = int(ev.get("retrato_original", 1416))
    n = 0
    for i, code in enumerate(ev.get("codigos", [])):
        b = bytearray(read_entry(us, "data_cmn.afs", int(reg[str(code)]), work))
        hits = 0
        for o in range(0, len(b) - 11, 4):     # evento (10, 1, retrato): la cara de quien habla
            if struct.unpack_from("<3I", b, o) == (10, 1, old):
                struct.pack_into("<I", b, o + 8, int(ev["retrato"]))
                hits += 1
        entry = int(ev["desde"]) + i if "desde" in ev else nueva_entrada()
        write_entry(mod, "data_cmn.afs", entry, bytes(b), work)
        STORY_CODES[int(code)] = entry
        print("eventos del mapa %d: %d retratos cambiados (contenedor nuevo %d)" % (code, hits, entry))
        n += 1
    return n


def _events_end(b, start, cnt, end):
    """Fin de los eventos de una entrada que empieza en `start` (cabecera de 28 o 16 bytes); si detras de los
    eventos hay datos propios (intros: posiciones del mapa), la cabecera es la de 28."""
    def walk(hs):
        p = start + hs
        for _ in range(cnt):
            if p + 4 > end:
                return None
            p += 4 * MAP_EVENT_WORDS.get(struct.unpack_from("<I", b, p)[0], 3)
        return p if p <= end else None
    for hs in (28, 16):
        if walk(hs) == end:
            return end
    p = walk(28)
    if p is None:
        raise ValueError("no se pueden leer los eventos de la entrada")
    return p


def lst_add(b, entry, events):
    """#LST (LE) con `events` [(op, p1, p2)] anadidos al final de la entrada `entry`."""
    if b[:4] != b"#LST":
        raise ValueError("no es un contenedor #LST")
    n, toff, doff = struct.unpack_from("<3I", b, 4)
    table = [list(struct.unpack_from("<2I", b, toff + 8 * i)) for i in range(n)]
    if not 0 <= entry < n:
        raise ValueError("el contenedor tiene %d entradas" % n)
    rel, cnt = table[entry]
    nxt = table[entry + 1][0] if entry + 1 < n else len(b) - doff
    # cabecera de 16 o 28 bytes, los eventos y, en las intros de los personajes jugables, datos
    # propios detras (posiciones del mapa): los eventos nuevos van justo tras los eventos
    # (los eventos del mapa son mas largos que 3 palabras: MAP_EVENT_WORDS; antes se contaban de 12 bytes y un
    # 44 en la entrada dejaba el evento nuevo en medio de otro)
    at = _events_end(b, doff + rel, cnt, doff + nxt)
    add = b"".join(pack_event(e) for e in events)
    out = bytearray(b[:at] + add + b[at:])
    table[entry][1] += len(events)
    for i in range(entry + 1, n):
        table[i][0] += len(add)
    for i, (r, c) in enumerate(table):
        struct.pack_into("<2I", out, toff + 8 * i, r, c)
    return bytes(out)


def cmd_montar(a):
    try:
        with open(a.toml, "rb") as f:
            cfg = tomllib.load(f)
    except OSError as e:
        raise SystemExit("no se puede leer %s: %s" % (a.toml, e.strerror or e))
    except tomllib.TOMLDecodeError as e:
        raise SystemExit("%s esta mal escrito (TOML): %s" % (os.path.basename(a.toml), e))
    TOML_DIR[0] = os.path.dirname(os.path.abspath(a.toml))
    info = cfg.get("campana", {})
    mods = a.mods or rb.default_mods()
    name = info.get("mod") or os.path.splitext(os.path.basename(a.toml))[0]
    # solo un nombre de carpeta: "..\\x" o una ruta absoluta escribian fuera de mods/
    if not name or os.path.basename(name) != name or name in (".", "..") or os.path.isabs(name):
        raise SystemExit("[campana] mod = %r: tiene que ser solo el nombre de la carpeta del mod" % name)
    mod = os.path.join(mods, name)
    us, work = a.us or rb.default_us(), work_dir()
    err, warn = validar(cfg, mods, mod)
    for w in warn:
        rb.log("aviso: " + w)
    if err:
        for e in err:
            rb.log("ERROR: " + e)
        raise SystemExit("la campana no se monta: corrige lo de arriba")
    os.makedirs(mod, exist_ok=True)
    rb.log("contenedores propios de data_cmn: %d-%d" % (reservar_cmn(mods, mod), CMN["base"] + CMN_N - 1))
    n = 0
    for t in cfg.get("texto", []):
        style = t.get("estilo", "dialogo")
        for lang, afss in LANG_AFS.items():
            lines = t.get(lang) or t.get("en")
            if not lines:
                continue
            bank = text_bank(lines, style)
            for afs in afss:
                if os.path.exists(afs_path(us, afs)):
                    write_entry(mod, afs, int(t["entrada"]), bank, work)
                    n += 1
    for c in cfg.get("combate", []):
        b = bytearray(read_entry(us, "data_cmn.afs", int(c["bpl"]), work))
        if b[:4] != b"#BPC":
            raise SystemExit("data_cmn %s no es un combate (#BPC)" % c["bpl"])
        struct.pack_into(">I", b, 0x10, int(c["rival"]))
        write_entry(mod, "data_cmn.afs", int(c["bpl"]), bytes(b), work)
        n += 1
    by_cont = {}
    for e in cfg.get("escena", []):
        by_cont.setdefault(int(e["contenedor"]), []).append(e)
    for cont, edits in by_cont.items():
        b = read_entry(us, "data_cmn.afs", cont, work)
        for e in edits:
            if "cambiar" in e:
                b = lst_replace(b, int(e["entrada"]), e["cabecera"], e["cambiar"])
            else:
                b = lst_add(b, int(e["entrada"]), [tuple(ev) for ev in e["anadir"]])
        write_container(mod, cont, b, work)
        n += 1
    if "historia" in cfg:
        n += story(cfg, us, mod, work)
    n += clean_panel(cfg, us, mod, work)
    for c in cfg.get("imagen", []):      # [[imagen]] entrada = N, archivo = "x.webp": imagen de narracion
        path = os.path.join(TOML_DIR[0], c["archivo"])
        if os.path.isfile(path):
            pic = png2pic.build_picture(Image.open(path))
        else:   # sin la imagen (el Kit no lleva arte de terceros): el panel de la historia, del juego
            fondo = int(cfg.get("historia", {}).get("fondo", 1411))
            rb.log("aviso: no encuentro %s; la entrada %s = el panel %d del juego" % (c["archivo"], c["entrada"], fondo))
            pic = CLEANED.get(fondo) or read_entry(us, "data_cmn.afs", fondo, work)   # el ya limpio
        write_entry(mod, "data_cmn.afs", int(c["entrada"]), pic, work)
        n += 1
    n += portraits(cfg, us, mod, work)
    n += map_events(cfg, us, mod, work)
    rb.flush_entries()
    move_stale(mods, mod)
    for e, other in entry_conflicts(mods, mod):
        rb.log("aviso: data_cmn %s tambien lo cambia el mod '%s' (solo uno ganara)" % (e, other))
    codes = cfg.get("codigos", {})
    wheel = cfg.get("rueda", {}).get("personajes", [])
    virt = cfg.get("rueda", {}).get("casillas_virtuales", [])
    with open(os.path.join(mod, "campana.toml"), "w", encoding="utf-8") as f:
        f.write("# Generado por campana.py: escenas de Dragon Universe que cambia este mod.\n[codigos]\n")
        for k, v in sorted(codes.items(), key=lambda kv: int(kv[0])):
            f.write("%d = %d\n" % (int(k), int(v)))
        if wheel or virt:
            f.write("\n[rueda]\n")
            if wheel:
                f.write("personajes = [%s]\n" % ", ".join(str(int(i)) for i in wheel))   # TOML valido con varios
            if virt:   # casillas sin celda propia: se entra por una celda virtual del roster ([celdas_rueda])
                f.write("casillas_virtuales = [%s]\n" % ", ".join(str(int(i)) for i in virt))
            if "historia" in cfg:   # casilla de DU de la historia: dos campanas no pueden compartirla (validar)
                f.write("casilla = %d\n" % (int(cfg["historia"]["codigo"]) % 100))
        if virt and cfg.get("historia", {}).get("intro_de"):   # la historia empieza en la intro de su saga (102 = Namek)
            f.write("\n[intro_casilla]\n%d = %d\n" % (int(virt[0]), int(str(cfg["historia"]["intro_de"]).split(".")[0])))
        for sec in ("casilla_luchador",):   # casilla -> ID del luchador (mapa, menus y combate de DU)
            if cfg.get(sec):
                f.write("\n[%s]\n" % sec + "".join("%d = %d\n" % (int(k), int(v)) for k, v in sorted(cfg[sec].items(), key=lambda kv: int(kv[0]))))
        if START_FORMS:
            f.write("\n[forma_jugador]\n" + "".join("%d = %d\n" % kv for kv in sorted(START_FORMS.items())))
        if RIVAL_SUITS:
            f.write("\n[traje_rival]\n" + "".join("%d = %d\n" % kv for kv in sorted(RIVAL_SUITS.items())))
        if STORY_CODES:
            f.write("\n[codigos_historia]\n" + "".join("%d = %d\n" % kv for kv in sorted(STORY_CODES.items())))
        if MAP_MODELS:
            f.write("\n[modelo_mapa]\n" + "".join("%d = %d\n" % kv for kv in sorted(MAP_MODELS.items())))
        if MAP_ANIMS:
            f.write("\n[animacion_mapa]\n" + "".join("%d = %d\n" % kv for kv in sorted(MAP_ANIMS.items())))
        if MAP_SUITS:
            f.write("\n[traje_mapa]\n" + "".join("%d = %d\n" % kv for kv in sorted(MAP_SUITS.items())))
        if CMN.get("next", 0) > CMN.get("base", 0):   # contenedores propios: al rehacer se reusa el bloque
            f.write("\n[contenedores]\nbase = %d\nentradas = %d\n" % (CMN["base"], CMN_N))
        if TEXT_BLOCK:   # bloque de textos automatico: al rehacer la campana se reusa (reservar_textos)
            f.write("\n[textos]\nbase = %d\nentradas = %d\n" % (TEXT_BLOCK["base"], TEXT_BLOCK["entradas"]))
    with open(os.path.join(mod, "manifest.txt"), "w", encoding="utf-8") as f:
        f.write("name=%s\ntype=campana\ndescription=%s\n" % (info.get("nombre", "Campana"),
                                                          info.get("descripcion", "Campana de Dragon Universe")))
    print("mod %s: %d entradas, %d escenas redirigidas" % (mod, n, len(codes)))
    return 0


def selftest():
    img = render("Lo siento... pero has bajado la guardia.", "dialogo")
    assert img.shape[2] == 4 and img.shape[1] <= 200 and img[..., 3].max() > 0
    bank = text_bank(["Hola", "Adios, amigo mio"], "narracion")
    assert bank[:4] == b"#AZT" and struct.unpack(">I", bank[0x10:0x14])[0] == 2
    assert capsulas.azt_read(bank, 1) is not None
    lst = b"#LST" + struct.pack("<3I", 2, 0x10, 0x20) + struct.pack("<4I", 0, 1, 28, 1)
    lst += struct.pack("<4I", 1, 2, 3, 4) + struct.pack("<3I", 10, 0, 5) + struct.pack("<4I", 5, 6, 7, 8) + struct.pack("<3I", 14, 1, 9)
    out = lst_add(lst, 0, [(22, 0, 0), (44, 0, -5)])
    assert struct.unpack_from("<4I", out, 0x10) == (0, 3, 52, 1), struct.unpack_from("<4I", out, 0x10)
    assert struct.unpack_from("<3I", out, 0x20 + 28 + 12) == (44, 0, 0xFFFFFFFB)
    assert struct.unpack_from("<3I", out, 0x20 + 52 + 16) == (14, 1, 9)
    assert pack_event((29, 0, "SAITYOU", 1)) == struct.pack("<2I", 29, 0) + b"SAITYOU".ljust(16, b"\0") + struct.pack("<I", 1)
    assert len(pack_event((24, 3, "5100.2", 2625, 2, 5462, 54, -5448, 1000))) == 36
    rep = lst_replace(lst, 0, [9, 9, 9, 9, 0, 0, 1], [(22, 0, 0), (44, 0, 5930, -1800, 100, 54, 0, 0)])
    assert struct.unpack_from("<2I", rep, 0x10) == (0, 2) and struct.unpack_from("<I", rep, 0x18)[0] == 28 + 12 + 32
    assert struct.unpack_from("<3I", rep, 0x20 + 72 + 16) == (14, 1, 9)
    built = lst_build([([1, 2, 3, 4, 0, 0, 7], [(16, 0, 654), (20, 0, "5122.3")]), ([1, 2, 3, 4], [(0, 0, 0)])])
    back = lst_entries(built)
    assert back[0][1][1] == (20, 0, (5122 << 16) | 3) and back[1][0] == [1, 2, 3, 4] and back[0][2] == b"", back
    tailed = lst_build([([1, 2, 3, 4, 0, 0, 7], [(16, 0, 1)], b"MAPA" * 3), ([1, 2, 3, 4], [])])
    assert lst_entries(tailed)[0][2] == b"MAPA" * 3 and lst_entries(lst_build(lst_entries(tailed))) == lst_entries(tailed)
    t2 = lst_add(tailed, 0, [(20, 0, 9)])
    assert lst_entries(t2)[0][1] == [(16, 0, 1), (20, 0, 9)] and lst_entries(t2)[0][2] == b"MAPA" * 3
    # eventos del mapa (44 = 5 palabras): el nuevo va tras los de verdad, con su tamano
    mapa = lst_build([([1, 2, 3, 4, 0, 0, 7], [(44, 0, 1, 2, 3), (20, 0, 9)]), ([1, 2, 3, 4], [])])
    m2 = lst_add(mapa, 0, [(44, 0, 4, 5, 6)])
    assert lst_events(m2, 0) == [(44, 0, 1, 2, 3), (20, 0, 9), (44, 0, 4, 5, 6)] and lst_events(m2, 1) == [], "lst_add 44"
    assert validar({"escena": [{"contenedor": 1, "entrada": 0, "anadir": [[44, 0, 5930]]}]}, HERE, HERE)[0], "44 corto"
    assert not validar({"escena": [{"contenedor": 1, "entrada": 0, "anadir": [[44, 0, 1, 2, 3], [22, 0, 0]]}]},
                       HERE, HERE)[0], "44 bien"
    assert lst_entries(lst_build(back)) == back
    ap = lst_append(rep, [([9, 3, 2, 2, 0, 0, 1], [(20, 0, "102.38")])])
    od, nd = struct.unpack_from("<I", rep, 12)[0], struct.unpack_from("<I", ap, 12)[0]
    assert struct.unpack_from("<I", ap, 4)[0] == 3 and ap[nd:nd + len(rep) - od] == rep[od:], "lst_append"
    assert lst_events(ap, 2) == [(20, 0, (102 << 16) | 38)] and lst_events(ap, 1) == lst_events(rep, 1)
    # dos campanas no comparten casilla; el codigo tiene que ser el de la casilla del protagonista
    md = tempfile.mkdtemp()
    os.makedirs(os.path.join(md, "otra"))
    with open(os.path.join(md, "otra", "campana.toml"), "w") as f:
        f.write("[rueda]\npersonajes = [27]\ncasilla = 22\n")
    cfg = {"historia": {"codigo": 5122}, "rueda": {"personajes": [27]}}
    assert validar(cfg, md, os.path.join(md, "mia"))[0] and not validar(cfg, md, os.path.join(md, "otra"))[0]
    cfg["rueda"]["personajes"] = [28]
    assert "casillas [23]" in " ".join(validar(cfg, md, os.path.join(md, "otra"))[0])
    print("campana.py: autocomprobacion OK")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--us", help="carpeta us/ del juego (por defecto la del proyecto)")
    p.add_argument("--mods", help="carpeta mods/ (por defecto la del juego)")
    sub = p.add_subparsers(dest="cmd")
    v = sub.add_parser("ver")
    v.add_argument("entrada", type=int)
    v.add_argument("--salida")
    e = sub.add_parser("escenas")
    e.add_argument("bloque", type=int)
    m = sub.add_parser("montar")
    m.add_argument("toml")
    sub.add_parser("prueba")
    a = p.parse_args(argv)
    if a.cmd == "ver":
        return cmd_ver(a)
    if a.cmd == "escenas":
        return cmd_escenas(a)
    if a.cmd == "montar":
        return cmd_montar(a)
    selftest()
    return 0


if __name__ == "__main__":
    sys.exit(main())
