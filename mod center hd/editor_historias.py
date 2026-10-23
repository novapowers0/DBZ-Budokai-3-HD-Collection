#!/usr/bin/env python3
"""Editor de historias / Story editor: crea y edita las campanas (.toml) que monta campana.py.

  python editor_historias.py                    abre la ventana (idioma del Mod Kit)
  python editor_historias.py mi_historia.toml   abre una campana
  python editor_historias.py --lang en          ventana en ingles (es | en)
  python editor_historias.py --selftest         autocomprobacion (sin ventana)

Dentro del Mod Kit (pestaña «Historias») el editor se incrusta: App(marco, ruta, embedded=True).
"""
import argparse
import copy
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import tkinter as tk
import tkinter.font as tkfont
from tkinter import colorchooser, filedialog, messagebox, ttk

try:
    import tomllib
except ImportError:  # Python < 3.11
    import tomli as tomllib  # type: ignore

try:                                                     # vistas previas de PNG: opcionales
    from PIL import Image, ImageDraw, ImageFont, ImageTk
except ImportError:
    Image = ImageDraw = ImageFont = ImageTk = None

HERE = os.path.dirname(os.path.abspath(__file__))
CAMPANAS = os.path.join(HERE, "campanas")
PLANTILLA = os.path.join(CAMPANAS, "plantilla_historia.toml")
FREEZER = os.path.join(CAMPANAS, "historia_freezer.toml")
ROSTER_JSON = os.path.join(HERE, "roster_db.json")
CAPS_JSON = os.path.join(os.path.dirname(HERE), "docs", "07_ports", "GOHAN_FUTURO_EXPLORACION_2026-10-06",
                         "04_transformaciones", "b3_caps.json")
RAW_KEYS = ("escena", "panel_limpio")                    # tablas que se guardan tal cual estaban
RAW_HEAD = re.compile(r"\[\[(escena|panel_limpio)\]\]")
TABLE_HEAD = re.compile(r"\[\[?[A-Za-z0-9_.\-]+\]\]?")   # cabecera de tabla (empieza en columna 0)

LANG = "es"                                              # "es" o "en": se fija al arrancar

# Colores y letra del Mod Kit (C en modkit_gui.py): el editor se ve igual dentro y fuera del Mod Kit.
PAL = {"bg": "#111318", "card": "#1A1D24", "card_hi": "#20232B", "frame": "#262A34", "hover": "#323642",
       "line": "#323641", "text": "#EDF0F5", "dim": "#8F96A6", "accent": "#F58C1C", "accent_dim": "#9E5912",
       "ok": "#5CD17A", "warn": "#FFBD4D", "err": "#FF6B5C", "on_accent": "#16120C", "log_bg": "#0C0E12"}
FONT = {}                                                # se rellena con init_fonts()
THUMB, THUMB_BIG = 96, 200                               # tamaño de las vistas previas (px)


MONTAR_ARGS = []   # --mods/--us de la linea de ordenes (Mod Kit, ventana aparte): se pasan a campana.py


def mods_dir():
    """Carpeta mods/ pasada con --mods (None = la del juego)."""
    return MONTAR_ARGS[MONTAR_ARGS.index("--mods") + 1] if "--mods" in MONTAR_ARGS else None


def T(es, en):
    """Texto en el idioma elegido. Solo se muestra uno, nunca los dos a la vez."""
    return en if LANG == "en" else es


def settings_lang():
    """Mismo idioma que el Mod Kit: su settings.json (APPDATA/DBZ3_ModKit). None si no hay."""
    path = os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~"), "DBZ3_ModKit", "settings.json")
    try:
        with open(path, encoding="utf-8") as f:
            lang = json.load(f).get("lang")
    except (OSError, ValueError, AttributeError):
        return None
    return lang if lang in ("es", "en") else None


# ---------------------------------------------------------------- TOML: leer y escribir
def parse_text(text):
    """Separa el texto en (datos, bloques crudos). [[escena]] y [[panel_limpio]] no se tocan."""
    blocks = []
    for line in text.splitlines(True):
        if not blocks or TABLE_HEAD.match(line):
            blocks.append([])
        blocks[-1].append(line)
    body, raw = [], []
    for b in blocks:
        if RAW_HEAD.match(b[0]):
            raw.append("".join(b).rstrip())
        else:
            body.append("".join(b))
    return tomllib.loads("".join(body)), raw


def _key(k):
    return k if re.fullmatch(r"[A-Za-z0-9_\-]+", k) else json.dumps(k, ensure_ascii=False)


def _val(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, str):
        return json.dumps(v, ensure_ascii=False)
    if isinstance(v, list):
        return "[" + ", ".join(_val(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{" + ", ".join("%s = %s" % (_key(a), _val(b)) for a, b in v.items()) + "}"
    raise TypeError("valor no soportado: %r" % (v,))


def _pairs(d):
    return ["%s = %s" % (_key(a), _val(b)) for a, b in d.items()]


def _is_tree(d):
    """Tabla cuyas claves son todas tablas (p. ej. historia.estilos): sus hijas van como [a.b.c]."""
    return bool(d) and all(isinstance(x, dict) for x in d.values())


def _table(path, d, out):
    """Escribe una tabla TOML. Las tablas hijas van como cabecera [a.b] si son de tipo tabla-de-tablas;
    el resto (p. ej. final = {...}) sigue en linea, como en historia_freezer.toml."""
    out += ["", "[%s]" % ".".join(_key(p) for p in path)]
    kids = []
    for a, b in d.items():
        if isinstance(b, dict) and (_is_tree(b) or _is_tree(d)):
            kids.append((a, b))
        else:
            out.append("%s = %s" % (_key(a), _val(b)))
    for a, b in kids:
        _table(path + [a], b, out)


def build_text(data, raw=()):
    """Escribe el diccionario como TOML; los bloques de `raw` se copian tal cual."""
    head, tables, arrays = [], [], []
    for k, v in data.items():
        if k in RAW_KEYS:
            continue
        if isinstance(v, dict):
            tables.append((k, v))
        elif isinstance(v, list) and v and all(isinstance(x, dict) for x in v):
            arrays.append((k, v))
        else:
            head.append("%s = %s" % (_key(k), _val(v)))
    out = ["# Generado por editor_historias.py (Editor de historias)."] + head
    for k, t in tables:
        _table([k], t, out)
    for k, items in arrays:
        for it in items:
            out += ["", "[[%s]]" % _key(k)] + _pairs(it)
    for block in raw:
        out += ["", block.strip("\n")]
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- datos
def _is_int(v):
    return type(v) is int


def _ints(v, n=None):
    return isinstance(v, list) and all(_is_int(x) for x in v) and (n is None or len(v) == n)


def _num(t):
    return int(t) if re.fullmatch(r"-?\d+", t) else t


def _path(key):
    return key if isinstance(key, tuple) else (key,)


def _nested(d, path):
    for p in path:
        if not isinstance(d, dict) or p not in d:
            return None
        d = d[p]
    return d


def _set_nested(d, path, v):
    if v is None:                       # vacio = la clave no se guarda (no crea tablas vacias)
        parent = _nested(d, path[:-1])
        if isinstance(parent, dict):
            parent.pop(path[-1], None)
        return
    for p in path[:-1]:
        if not isinstance(d.get(p), dict):
            d[p] = {}
        d = d[p]
    d[path[-1]] = v


def apply(root, values):
    for key, v in values.items():
        _set_nested(root, _path(key), v)


def _abs(p, base):
    if str(p).startswith("pack:"):     # "pack:Zarbon" = Potraits/Zarbon.png del pack opcional del usuario (arte de terceros)
        import retratos_pack  # noqa: PLC0415
        return retratos_pack.buscar(str(p)[5:], exacto=True) or os.path.join(base, "(sin retrato del pack)")
    return p if os.path.isabs(p) else os.path.join(base, p)


def load_capsulas():
    """Cápsulas del juego: (texto, texto, número). Vacío si falta el archivo."""
    try:
        with open(CAPS_JSON, encoding="utf-8") as f:
            rows = json.load(f)
    except (OSError, ValueError):
        return []
    out = []
    for r in rows if isinstance(rows, list) else []:
        a = str(r.get("A", "")).strip()
        if a.isdigit():
            lab = "%s - %s" % (a, r.get("B", ""))
            out.append((lab, lab, int(a)))
    return out


def load_chars():
    """Personajes para el desplegable del rival (roster_db.json: id -> nombre). Vacío = se escribe el número."""
    try:
        with open(ROSTER_JSON, encoding="utf-8") as f:
            rows = json.load(f).get("ids", [])
    except (OSError, ValueError, AttributeError):
        return []
    out = []
    for r in rows if isinstance(rows, list) else []:
        if isinstance(r, dict) and _is_int(r.get("id")) and r.get("name"):
            lab = "%d - %s" % (r["id"], r["name"])
            out.append((lab, lab, r["id"]))
    return out


# Preajustes de marca de historia_freezer.toml: (es, en, icono, zona, x, y, z, escala)
MARCAS = [("Namek 1 (combates 654, 658)", "Namek 1 (fights 654, 658)", [1, 2, 4918, 67, -4916, 1000]),
          ("Namek 2 (combates 655, 662)", "Namek 2 (fights 655, 662)", [1, 2, 5010, 20, -5399, 1000]),
          ("Namek 3 (combate 656)", "Namek 3 (fight 656)", [1, 2, 5517, 39, -5053, 1000]),
          ("Namek 4 (combate 657)", "Namek 4 (fight 657)", [1, 3, 6400, 40, 3800, 1000]),
          ("Namek 5 (combate 659)", "Namek 5 (fight 659)", [1, 2, -4160, 130, -3202, 1000]),
          ("Namek 6 (combates 660, 663)", "Namek 6 (fights 660, 663)", [1, 1, -2800, 11, 2650, 1000]),
          ("Namek 7 (combate 661)", "Namek 7 (fight 661)", [1, 2, 5462, 54, -5448, 1000]),
          ("Tierra 1 (combate 664)", "Earth 1 (fight 664)", [1, 2, 3213, 48, -6083, 1500]),
          ("Tierra 2 (combate 666)", "Earth 2 (fight 666)", [1, 2, 2717, 20, 3422, 1500])]
PLANETAS = [("0 - La Tierra", "0 - Earth", 0), ("1 - Namek", "1 - Namek", 1)]
PLANTILLAS = [("709 - Namek (escenario de Namek)", "709 - Namek (Namek stage)", 709),
              ("717 - La Tierra (escenario de la Tierra)", "717 - Earth (Earth stage)", 717)]
NIVELES = [("0 - Muy fácil", "0 - Very easy", 0), ("1 - Fácil", "1 - Easy", 1),
           ("2 - Medio-fácil", "2 - Fairly easy", 2), ("3 - Normal", "3 - Normal", 3),
           ("4 - Medio-difícil", "4 - Fairly hard", 4), ("5 - Difícil", "5 - Hard", 5),
           ("6 - Muy difícil", "6 - Very hard", 6), ("7 - Máximo", "7 - Maximum", 7)]
CAPSULAS = load_capsulas()
CHARS = load_chars()


def char_name(i):
    """Nombre del personaje del roster (id), en Titulo; None si no existe."""
    for lab, _, cid in CHARS:
        if cid == i:
            return lab.split(" - ", 1)[-1].title()
    return None


def _opts(lst):
    """Opciones (es, en, valor) -> (texto en el idioma elegido, valor)."""
    return [(T(es, en), v) for es, en, v in lst]


# Especificaciones de campos: (clave o ruta, tipo, (etiqueta es, en), (ayuda es, en), opciones).
# Etiqueta con * = obligatorio. Tipos: text, csv, int, spin (0-9), combo, bool, lines, ints, marca, file.
CAMP_SPECS = [
    (("campana", "nombre"), "text", ("Nombre de la historia", "Story name"),
     ("Lo que ve el jugador en el mod. Ej.: Historia de Freezer.", "What the player sees in the mod. E.g.: Freezer's story."), None),
    (("campana", "mod"), "text", ("Carpeta del mod *", "Mod folder *"),
     ("Nombre de la carpeta que se crea en mods/. Solo letras, números, _ o -. Obligatorio.",
      "Name of the folder created in mods/. Only letters, numbers, _ or -. Required."), None),
    (("campana", "descripcion"), "text", ("Descripción", "Description"),
     ("Texto corto que aparece en el manifiesto del mod.", "Short text shown in the mod's manifest."), None),
    (("rueda", "personajes"), "csv", ("Personajes de la rueda (IDs)", "Wheel characters (IDs)"),
     ("Números de los personajes que se añaden a la rueda de Dragon Universe, separados por comas. Freezer = 27.",
      "Numbers of the characters added to the Dragon Universe wheel, separated by commas. Freezer = 27."), None),
]

# [historia], agrupado en pestaña 4. Avanzado
HIST_GROUPS = [
    (("Escenas de la historia", "Story scenes"), [
        (("historia", "codigo"), "int", ("Código de escenas *", "Scene code *"),
         ("Código de las escenas de la historia: 5000 + 100 x saga + casilla. Freezer = 5122.",
          "Code of the story scenes: 5000 + 100 x saga + slot. Freezer = 5122."), None),
        (("historia", "contenedor"), "int", ("Contenedor de escenas", "Scene container"),
         ("#LST de la historia (1820 en Freezer). Se guarda tal cual.", "#LST of the story (1820 in Freezer). Saved as is."), None),
        (("historia", "altura_mapa"), "int", ("Altura de vuelo en el mapa", "Flight height on the map"),
         ("Altura a la que vuelas por el mapa al volver de un combate. Vacío = 400.",
          "Height you fly at on the map after a fight. Empty = 400."), None),
        (("historia", "textos_desde"), "int", ("Textos desde", "Texts from"),
         ("Vacío = automático: la campaña recibe su propio bloque de textos sin pisar el juego ni otros mods. "
          "A mano, la primera entrada de texto (Freezer = 3600).",
          "Empty = automatic: the campaign gets its own text block without overwriting the game or other mods. "
          "By hand, the first text entry (Freezer = 3600)."), None),
        (("historia", "fondo"), "int", ("Fondo de los textos de saga", "Saga text background"),
         ("Panel de fondo de la introducción de cada saga (1411 = el panel verde de Freezer). Vacío = 1411.",
          "Background panel of each saga intro (1411 = Freezer's green panel). Empty = 1411."), None),
        (("historia", "fondo_dialogo"), "int", ("Fondo de charla por defecto", "Default talk background"),
         ("Fondo de la charla de después si el capítulo no pone uno (1536 Namek, 1549 la Tierra). Vacío = 1536.",
          "Background of the after-talk if the chapter sets none (1536 Namek, 1549 Earth). Empty = 1536."), None),
    ]),
    (("Final", "Ending"), [
        (("historia", "final", "contenedor"), "int", ("Final: contenedor *", "Ending: container *"),
         ("#LST donde se reescribe una entrada de final libre. Freezer = 1728.",
          "#LST where a free ending entry is rewritten. Freezer = 1728."), None),
        (("historia", "final", "codigo"), "int", ("Final: código *", "Ending: code *"),
         ("Código de escena del final. Freezer = 1.", "Scene code of the ending. Freezer = 1."), None),
        (("historia", "final", "entrada"), "int", ("Final: entrada libre", "Ending: free entry"),
         ("Entrada libre del final que se reescribe: 5 = 3 páginas, 4 = 2 páginas. Freezer = 5.",
          "Free ending entry that gets rewritten: 5 = 3 pages, 4 = 2 pages. Freezer = 5."), None),
    ]),
    (("Combates", "Fights"), [
        (("historia", "combates", "codigo"), "int", ("Combates: código base *", "Fights: base code *"),
         ("Código base de los combates de la historia; cada saga suma 100. Freezer = 5522.",
          "Base code of the story fights; each saga adds 100. Freezer = 5522."), None),
        (("historia", "combates", "contenedor"), "int", ("Combates: contenedor", "Fights: container"),
         ("#LST de los combates. Freezer = 2132.", "#LST of the fights. Freezer = 2132."), None),
        (("historia", "combates", "plantilla"), "combo", ("Combates: escenario por defecto", "Fights: default stage"),
         ("Escenario que usan los combates si el capítulo no pone uno. Cada capítulo puede cambiarlo.",
          "Stage the fights use if the chapter sets none. Each chapter can change it."), PLANTILLAS),
    ]),
    (("Charlas antes del combate", "Talks before fights"), [
        (("historia", "dialogos", "codigo"), "int", ("Charlas: código *", "Talks: code *"),
         ("Código de las charlas en el escenario, antes de cada combate. Freezer = 5922.",
          "Code of the talks on the stage, before each fight. Freezer = 5922."), None),
        (("historia", "dialogos", "contenedor"), "int", ("Charlas: contenedor", "Talks: container"),
         ("#LST de las charlas. Freezer = 1659.", "#LST of the talks. Freezer = 1659."), None),
        (("historia", "dialogos", "textos_desde"), "int", ("Charlas: textos desde", "Talks: texts from"),
         ("Vacío = automático (junto con «Textos desde»). Freezer = 3620.",
          "Empty = automatic (together with 'Texts from'). Freezer = 3620."), None),
    ]),
    (("Guiones (solo si sabes lo que haces)", "Scripts (only if you know what you are doing)"), [
        (("historia", "guion_narracion"), "int", ("Guion de narración", "Narration script"),
         ("Guion de las escenas de texto. Vacío = 2779 (0xADB).", "Script of the text scenes. Empty = 2779 (0xADB)."), None),
        (("historia", "guion_final"), "int", ("Guion del final", "Ending script"),
         ("Vacío = 2790.", "Empty = 2790."), None),
        (("historia", "guion_combate"), "int", ("Guion del combate", "Fight script"),
         ("Vacío = 2866.", "Empty = 2866."), None),
        (("historia", "guion_tras_combate"), "int", ("Guion tras el combate", "After-fight script"),
         ("Vacío = 2495.", "Empty = 2495."), None),
    ]),
]
HIST_SPECS = [s for _, specs in HIST_GROUPS for s in specs]

RET_SPECS = [
    ("entrada", "int", ("Entrada del retrato *", "Portrait entry *"),
     ("Número de la entrada del retrato en data_cmn que se reemplaza (ej. 1464 = Freezer, 2.ª forma).",
      "Number of the portrait entry in data_cmn that gets replaced (e.g. 1464 = Freezer, 2nd form)."), None),
    ("reparar", "bool", ("Reparar: el retrato del juego está sin terminar", "Repair: the game's portrait is unfinished"),
     ("Márcalo si el retrato del juego está sin terminar (una sola textura) y hay que rehacerlo.",
      "Tick it if the game's portrait is unfinished (a single texture) and must be remade."), None),
    ("png", "file", ("Mi propia imagen PNG", "My own PNG image"),
     ("Imagen PNG (fondo transparente) que sustituye al retrato; se ajusta a su caja. Vacío = retrato del juego. «pack:Nombre» = Nombre.png de tu carpeta Potraits del pack opcional.",
      "PNG image (transparent background) that replaces the portrait; it fits its box. Empty = the game's portrait. 'pack:Name' = Name.png from your optional pack's Potraits folder."), None),
]

# Música: palabras de música (banco << 16 | entrada). Se muestran como «2.4205» y se guardan como número.
MUS_NARR, MUS_TALK, MUS_KEEP = 135269, 135277, 0xFFFFFFFF   # 2.4197 narración, 2.4205 charla, mantener la actual
MUSIC_PRESETS = [("Narración (por defecto del juego) 2.4197", "Narration (game default) 2.4197", MUS_NARR),
                 ("Charla tras el combate (por defecto del juego) 2.4205", "Post-battle talk (game default) 2.4205", MUS_TALK),
                 ("Mantener la música actual", "Keep current music", MUS_KEEP),
                 ("Música del juego 2.4211", "Game music 2.4211", 135283),   # 2 << 16 | 4211
                 ("Música del juego 2.4212", "Game music 2.4212", 135284)]   # 2 << 16 | 4212
MUSIC_CHAP = [("Igual que en la historia", "Same as in the story", None)] + MUSIC_PRESETS
MUSIC_SPECS = [
    (("historia", "musica_narracion"), "music", ("Música de la narración", "Narration music"),
     ("Música de las narraciones y de la introducción de cada saga. Vacío o 2.4197 = la del juego. Puedes escribir "
      "un número (ej. 2.4205) o elegir de la lista. «Mantener la música actual» deja la que suena.",
      "Music of the narrations and of each saga intro. Empty or 2.4197 = the game's. You can type a number "
      "(e.g. 2.4205) or pick from the list. 'Keep current music' keeps the one playing."), MUSIC_PRESETS),
    (("historia", "musica_dialogo"), "music", ("Música de la charla tras el combate", "Post-battle talk music"),
     ("Música de la charla después de ganar un combate. Vacío o 2.4205 = la del juego. Cada capítulo puede cambiarla.",
      "Music of the talk after winning a fight. Empty or 2.4205 = the game's. Each chapter can change it."),
     MUSIC_PRESETS),
]

# Capitulo: secciones Combate / Rival / Tu personaje / Mapa / Musica / Textos
CAP_GROUPS = [
    (("Combate", "Fight"), [
        ("combate", "int", ("Número de combate", "Fight number"),
         ("Combate del juego (#BPC de data_cmn) que se rehace; Freezer usa 654-667. Vacío = este capítulo es el final.",
          "Game fight (#BPC in data_cmn) that gets remade; Freezer uses 654-667. Empty = this chapter is the ending."), None),
        ("nivel", "combo", ("Nivel del rival", "Rival level"),
         ("0 = muy fácil ... 7 = máximo. Vacío = sube solo con la historia.",
          "0 = very easy ... 7 = maximum. Empty = rises only with the story."), NIVELES),
        ("plantilla", "combo", ("Escenario del combate", "Fight stage"),
         ("709 Namek, 717 la Tierra. Vacío = el de la pestaña 4 (Combates: escenario).",
          "709 Namek, 717 Earth. Empty = the one in tab 4 (Fights: stage)."), PLANTILLAS),
        ("premio", "combo", ("Cápsula de premio", "Reward capsule"),
         ("Cápsula que ganas al vencer. Elígela de la lista o escribe su número. Vacío = sin premio.",
          "Capsule you win by beating it. Pick it from the list or type its number. Empty = no reward."), CAPSULAS),
        ("fondo_dialogo", "int", ("Fondo de la charla de después", "Background of the after-talk"),
         ("1536 Namek, 1549 la Tierra. Vacío = el de la pestaña 4.", "1536 Namek, 1549 Earth. Empty = the one in tab 4."), None),
    ]),
    (("Rival", "Rival"), [
        ("rival", "combo" if CHARS else "int", ("Personaje rival", "Rival character"),
         ("Quién es el rival. Vacío = el rival que ya trae el combate.",
          "Who the rival is. Empty = the rival the fight already has."), CHARS or None),
        ("forma_rival", "spin", ("Forma del rival (0-9)", "Rival form (0-9)"),
         ("0 = normal. 2 = Super Saiyan en la historia de Freezer. Si no sabes, deja 0.",
          "0 = normal. 2 = Super Saiyan in Freezer's story. If unsure, leave 0."), None),
        ("traje_rival", "spin", ("Traje del rival (0-9)", "Rival outfit (0-9)"),
         ("0 = traje normal. Vegeta 3, Krilin 2 y Gohan niño 1 = armadura de Namek.", "0 = normal outfit. Vegeta 3, Krillin 2 and Kid Gohan 1 = Namek armor."), None),
        ("retratos", "ints", ("Retratos: rival y tú *", "Portraits: rival and you *"),
         ("Dos números: retrato del rival (izquierda) y el tuyo (derecha). Ej.: 1436, 1463.",
          "Two numbers: the rival's portrait (left) and yours (right). E.g.: 1436, 1463."), 2),
    ]),
    (("Tu personaje", "Your character"), [
        ("forma_jugador", "spin", ("Tu forma (0-9)", "Your form (0-9)"),
         ("0 = normal. 1, 2, 3... = transformaciones (Freezer: 4 = al 100%, 5 = Mecha). Si no sabes, deja 0.",
          "0 = normal. 1, 2, 3... = transformations (Frieza: 4 = 100%, 5 = Mecha). If unsure, leave 0."), None),
        ("modelo_mapa", "spin", ("Modelo en el mapa (0-9)", "Model on the map (0-9)"),
         ("Con qué modelo vuelas por el mapa hasta este combate. Vacío = el normal. Freezer: 5 = Mecha.",
          "Which model you fly with on the map towards this fight. Empty = the normal one. Frieza: 5 = Mecha."), None),
        ("traje_mapa", "spin", ("Traje en el mapa (0-9)", "Outfit on the map (0-9)"),
         ("Con qué traje vuelas por el mapa hasta este combate. Vacío = el normal. Freezer: 2 = en su nave.",
          "Which outfit you fly with on the map towards this fight. Empty = the normal one. Frieza: 2 = in his pod."), None),
        ("animacion_mapa", "spin", ("Vuelo en el mapa: el de la casilla (0-37)", "Map flight: the one of slot (0-37)"),
         ("Casilla de otro personaje cuyo vuelo usas en el mapa. Vacío = el tuyo. Mecha Freezer: 0 (vuela de pie, como Goku).",
          "Slot of another character whose flight you use on the map. Empty = your own. Mecha Frieza: 0 (flies standing, like Goku)."),
         None),
    ]),
    (("Mapa", "Map"), [
        ("saga", "bool", ("Abre una saga nueva", "Opens a new saga"),
         ("Solo en el primer capítulo de una saga nueva: sale el cierre de la anterior, la pregunta de guardar y el mapa nuevo.",
          "Only on the first chapter of a new saga: shows the end of the previous one, the save question and the new map."), None),
        ("planeta", "combo", ("Planeta donde empieza la saga", "Planet where the saga starts"),
         ("0 = la Tierra, 1 = Namek. Solo si abre saga.", "0 = Earth, 1 = Namek. Only if it opens a saga."), PLANETAS),
        ("salida", "ints", ("Posición de aparición (x, z, altura)", "Spawn position (x, z, height)"),
         ("3 números. Obligatorio si abre saga. Ej.: 5700, 100, 1700.",
          "3 numbers. Required if it opens a saga. E.g.: 5700, 100, 1700."), 3),
        ("marca", "marca", ("Marca en el mapa", "Map marker"),
         ("6 números: icono, zona, x, y, z, escala. Usa un preajuste o copia los de historia_freezer.toml. Vacío = marca por defecto.",
          "6 numbers: icon, zone, x, y, z, scale. Use a preset or copy them from historia_freezer.toml. Empty = default marker."), 6),
    ]),
    (("Música", "Music"), [
        ("musica_dialogo", "music", ("Música de la charla tras ganar", "Music of the talk after winning"),
         ("Música de la charla después de este combate. «Igual que en la historia» = la de la pestaña 1.",
          "Music of the talk after this fight. 'Same as in the story' = the one on tab 1."), MUSIC_CHAP),
    ]),
    (("Textos", "Texts"), [
        ("dialogo_es", "lines", ("Charla antes del combate: en español *", "Talk before the fight: Spanish *"),
         ("Frases antes del combate, por turnos: empieza el rival (izquierda), luego tú. Una línea por frase.",
          "Lines before the fight, in turns: the rival starts (left), then you. One line per phrase."), None),
        ("dialogo_en", "lines", ("Charla antes del combate: en inglés *", "Talk before the fight: English *"),
         ("Versión en inglés. Una línea por frase.", "English version. One line per phrase."), None),
        ("victoria_es", "lines", ("Tras la victoria: en español *", "After victory: Spanish *"),
         ("Frases tras ganar, por turnos (rival y tú). Una línea por frase.",
          "Lines after winning, in turns (rival and you). One line per phrase."), None),
        ("victoria_en", "lines", ("Tras la victoria: en inglés *", "After victory: English *"),
         ("Versión en inglés. Una línea por frase.", "English version. One line per phrase."), None),
        ("mapa_es", "lines", ("Vuelta al mapa: en español", "Back to the map: Spanish"),
         ("Frase de tu personaje al volver al mapa. Vacío = «Siguiente.»",
          "Your character's line when back on the map. Empty = 'Next.'"), None),
        ("mapa_en", "lines", ("Vuelta al mapa: en inglés", "Back to the map: English"),
         ("Versión en inglés. Vacío = 'Next.'", "English version. Empty = 'Next.'"), None),
        ("llegada_es", "lines", ("Llegada al mapa de la saga: en español", "Arrival on the saga map: Spanish"),
         ("Frase de tu personaje al llegar al mapa (primer capítulo de cada saga). Vacío = «¡Adelante!»",
          "Your character's line when arriving on the map (first chapter of each saga). Empty = 'Let's go!'"), None),
        ("llegada_en", "lines", ("Llegada al mapa de la saga: en inglés", "Arrival on the saga map: English"),
         ("Versión en inglés. Vacío = 'Let's go!'", "English version. Empty = 'Let's go!'"), None),
        ("cierre_es", "lines", ("Cierre de la saga anterior: en español", "End of the previous saga: Spanish"),
         ("Texto final de la saga anterior. Una línea por página. Una línea que termina en .png usa esa imagen. Solo si abre saga.",
          "Ending text of the previous saga. One line per page. A line ending in .png uses that image. Only if it opens a saga."), None),
        ("cierre_en", "lines", ("Cierre de la saga anterior: en inglés", "End of the previous saga: English"),
         ("Versión en inglés. Una línea por página.", "English version. One line per page."), None),
        ("cierre_voz", "lines", ("Voz del cierre (una por página)", "Ending voice-over (one per page)"),
         ("Locución de cada página del cierre: número de una voz del juego (adx_usa, p. ej. 1231) o un "
          "archivo de audio propio (.wav, .mp3, .ogg) junto al .toml. Línea vacía = sin voz.",
          "Voice-over for each ending page: number of a game voice (adx_usa, e.g. 1231) or your own audio "
          "file (.wav, .mp3, .ogg) next to the .toml. Empty line = no voice."), None),
        ("es", "lines", ("Introducción de la saga o final: en español", "Saga intro or ending: Spanish"),
         ("Introducción de la saga nueva. En el capítulo final (sin combate) es el texto del final.",
          "Intro of the new saga. In the final chapter (no fight) this is the ending text."), None),
        ("en", "lines", ("Introducción de la saga o final: en inglés", "Saga intro or ending: English"),
         ("Versión en inglés.", "English version."), None),
        ("voz", "lines", ("Voz de la introducción (una por página)", "Intro voice-over (one per page)"),
         ("Locución de cada página de la introducción o del final: número de una voz del juego "
          "(adx_usa, p. ej. 1231) o un archivo de audio propio (.wav, .mp3, .ogg). Línea vacía = sin voz.",
          "Voice-over for each intro or ending page: number of a game voice (adx_usa, e.g. 1231) or your "
          "own audio file (.wav, .mp3, .ogg). Empty line = no voice."), None),
    ]),
]
CAP_SPECS = [s for _, specs in CAP_GROUPS for s in specs]

# Idioma de los textos de la historia (pestaña 1): None = "cada idioma el suyo" (la clave no se guarda)
IDIOMAS = [("Cada idioma el suyo", "Each language its own", None),
           ("en: inglés en todos", "en: English for all", "en"),
           ("es: español en todos", "es: Spanish for all", "es")]
IDIOMA_SPECS = [
    (("historia", "idioma"), "combo", ("Idioma de los textos de la historia", "Story text language"),
     ("«Cada idioma el suyo»: cada idioma del juego usa su texto. «en» o «es»: ese idioma en todos "
      "(útil mientras faltan traducciones).",
      "'Each language its own': each game language uses its own text. 'en' or 'es': that language for "
      "everyone (useful while translations are missing)."), IDIOMAS),
]

# Imágenes de la saga (capítulos con saga = true): sustituyen al panel de narración
IMG_EXT = (".png", ".webp", ".jpg", ".jpeg", ".avif")
IMG_SPECS = [
    ("imagen", "file", ("Imagen de la narración de apertura", "Opening narration image"),
     ("Imagen (.png, .webp, .jpg o .avif) que sustituye al panel de la introducción de la saga. "
      "Ruta relativa al .toml. Vacío = panel de siempre.",
      "Image (.png, .webp, .jpg or .avif) that replaces the saga intro panel. Path relative to the .toml. "
      "Empty = the usual panel."), None),
    ("cierre_imagen", "file", ("Imagen del cierre de la saga anterior", "Previous saga ending image"),
     ("Imagen que sustituye al panel del cierre de la saga anterior. Vacío = panel de siempre.",
      "Image that replaces the previous saga's ending panel. Empty = the usual panel."), None),
]

# Estilos de texto ([historia.estilos.<nombre>] de campana.py): nombre, etiqueta es, en
STYLE_NAMES = [("dialogo", "Diálogo: charlas con retratos", "Dialogue: talks with portraits"),
               ("narracion", "Narración: paneles de la saga", "Narration: saga panels"),
               ("final", "Final: créditos y final", "Ending: credits and ending")]
# Campos: (clave, tipo, (es, en), (ayuda es, en), rango de los números)
STYLE_FIELDS = [
    ("fuente", "text", ("Fuente", "Font"),
     ("Un .ttf junto al .toml (ej. mi_fuente.ttf) o un nombre de fuente de Windows (ej. arialbd.ttf).",
      "A .ttf next to the .toml (e.g. my_font.ttf) or a Windows font name (e.g. arialbd.ttf)."), None),
    ("tamano", "int", ("Tamaño (px)", "Size (px)"),
     ("Tamaño de la letra en píxeles. Diálogo 17, narración 19.", "Font size in pixels. Dialogue 17, narration 19."),
     (4, 200)),
    ("interlineado", "int", ("Interlineado (px)", "Line spacing (px)"),
     ("Distancia entre líneas, en píxeles. Diálogo 22, narración 41.",
      "Distance between lines, in pixels. Dialogue 22, narration 41."), (1, 300)),
    ("ancho", "int", ("Ancho del texto (px)", "Text width (px)"),
     ("Ancho antes de saltar de línea, en píxeles. Diálogo 205, narración 290.",
      "Width before wrapping to a new line, in pixels. Dialogue 205, narration 290."), (20, 2000)),
    ("borde", "int", ("Grosor del borde (px)", "Border width (px)"),
     ("Grosor del contorno de las letras. 0 = sin borde.", "Thickness of the outline around the letters. 0 = no border."),
     (0, 12)),
    ("color", "color", ("Color del texto", "Text color"),
     ("Color de las letras. Se elige con el botón.", "Color of the letters. Choose it with the button."), None),
    ("color_borde", "color", ("Color del borde", "Border color"),
     ("Color del contorno. Se elige con el botón.", "Color of the outline. Choose it with the button."), None),
]
WIN_FONTS = os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts")
SAMPLE = "Lo siento... pero has bajado la guardia."       # frase de la vista previa de estilos
CP, DEF_STYLES = None, {}                                  # campana.py (perezoso) y sus estilos por defecto

NUEVO_CAP = {"retratos": [1436, 1463], "marca": [1, 2, 4918, 67, -4916, 1000],
             "dialogo_es": ["Frase del rival."], "dialogo_en": ["Rival line."],
             "victoria_es": ["El rival cae."], "victoria_en": ["The rival falls."]}
CHAP_INTS = ("rival", "forma_rival", "forma_jugador", "modelo_mapa", "traje_mapa", "animacion_mapa", "traje_rival",
             "premio", "nivel", "plantilla", "fondo_dialogo", "planeta")
TEXT_GROUPS = ("dialogo_", "victoria_", "mapa_", "llegada_", "cierre_", "")   # "" = es / en (intro y final)
GROUP_NAME = {"dialogo_": ("la charla antes del combate", "the talk before the fight"),
              "victoria_": ("la charla tras la victoria", "the talk after the victory"),
              "mapa_": ("la vuelta al mapa", "the back-to-map line"),
              "llegada_": ("la llegada al mapa de la saga", "the saga map arrival line"),
              "cierre_": ("el cierre de la saga anterior", "the previous saga ending"),
              "": ("la introducción o el final", "the saga intro or ending")}


def label_for(key):
    """Etiqueta (en el idioma elegido) de una clave de capitulo, retrato o historia."""
    for s in CAP_SPECS + RET_SPECS + HIST_SPECS + CAMP_SPECS + IDIOMA_SPECS + IMG_SPECS + STYLE_FIELDS + MUSIC_SPECS:
        if s[0] == key:
            return T(*s[2]).rstrip(" *")
    return str(key)


def check(data, base):
    """Revisa la historia. Devuelve (errores, avisos) en lenguaje normal. Con errores no se guarda."""
    err, warn = [], []
    mod = str(data.get("campana", {}).get("mod", ""))
    if not re.fullmatch(r"[A-Za-z0-9_\-]+", mod):
        err.append(T("Carpeta del mod (campana > mod): usa solo letras, números, _ o -, sin espacios.",
                     "Mod folder (campaign > mod): use only letters, numbers, _ or -, no spaces."))
    rueda = data.get("rueda", {}).get("personajes")
    if rueda is not None and not _ints(rueda):
        err.append(T("Personajes de la rueda: deben ser números separados por comas (ej. 27).",
                     "Wheel characters: must be numbers separated by commas (e.g. 27)."))
    for key, kind, label, _tip, _opts in HIST_SPECS:
        v = _nested(data, _path(key))
        name = T(*label).rstrip(" *")
        if v is None:
            if label[0].endswith("*"):
                err.append(T("Avanzado: falta %s.", "Advanced: missing %s.") % name)
        elif kind in ("int", "combo") and not _is_int(v):
            err.append(T("Avanzado: %s debe ser un número.", "Advanced: %s must be a number.") % name)
    idi = data.get("historia", {}).get("idioma")
    if idi is not None and idi not in ("es", "en"):
        err.append(T("Idioma de los textos: debe ser «en», «es» o vacío (cada idioma el suyo).",
                     "Story text language: must be 'en', 'es' or empty (each language its own)."))
    for k in ("musica_narracion", "musica_dialogo"):
        v = data.get("historia", {}).get(k)
        if v is not None and not _music_ok(v):
            err.append(T("Música de la historia: debe ser un número como 135277 o «2.4205» (banco.entrada).",
                         "Story music: must be a number like 135277 or '2.4205' (bank.entry)."))
            break
    est = data.get("historia", {}).get("estilos", {})
    for sname, st in (est.items() if isinstance(est, dict) else []):
        if sname not in [n for n, _, _ in STYLE_NAMES]:
            warn.append(T("Estilo «%s»: no existe (usa dialogo, narracion o final).",
                          "Style '%s': does not exist (use dialogo, narracion or final).") % sname)
        for key, kind, lab, _tip, rng in STYLE_FIELDS:
            if not isinstance(st, dict) or key not in st:
                continue
            v = st[key]
            if kind == "color":
                ok = isinstance(v, list) and len(v) == 3 and all(_is_int(x) and 0 <= x <= 255 for x in v)
            elif kind == "text":
                ok = isinstance(v, str) and v != ""
            else:
                ok = _is_int(v) and rng[0] <= v <= rng[1]
            if not ok:
                err.append(T("Estilo «%s»: %s no es válido.", "Style '%s': %s is not valid.") % (sname, T(*lab)))
    for i, r in enumerate(data.get("retrato", []), 1):
        if not _is_int(r.get("entrada")):
            err.append(T("Retrato %d: falta el número de entrada (ej. 1464).",
                         "Portrait %d: missing the entry number (e.g. 1464).") % i)
        if r.get("png") and not os.path.isfile(_abs(str(r["png"]), base)):
            warn.append(T("Retrato %d: no encuentro la imagen %s (se usará un retrato del juego).",
                          "Portrait %d: can't find the image %s (a game portrait will be used).") % (i, r["png"]))
    caps = data.get("capitulo", [])
    if not caps:
        err.append(T("La historia necesita al menos un capítulo.", "The story needs at least one chapter."))
    for i, c in enumerate(caps, 1):
        t = T("Capítulo %d", "Chapter %d") % i
        fight = "combate" in c
        if fight and not _is_int(c["combate"]):
            err.append(T("%s: el número de combate debe ser un número (ej. 654). Déjalo vacío si es el final.",
                         "%s: the fight number must be a number (e.g. 654). Leave it empty if it is the ending.") % t)
        for k in CHAP_INTS:
            if k in c and not _is_int(c[k]):
                err.append(T("%s: %s debe ser un número.", "%s: %s must be a number.") % (t, label_for(k)))
        if _is_int(c.get("nivel")) and not 0 <= c["nivel"] <= 7:
            err.append(T("%s: el nivel va de 0 a 7.", "%s: the level goes from 0 to 7.") % t)
        if "planeta" in c and c["planeta"] not in (0, 1):
            err.append(T("%s: el planeta es 0 (Tierra) o 1 (Namek).",
                         "%s: the planet is 0 (Earth) or 1 (Namek).") % t)
        if "musica_dialogo" in c and not _music_ok(c["musica_dialogo"]):
            err.append(T("%s: la música debe ser un número como 135277 o «2.4205».",
                         "%s: the music must be a number like 135277 or '2.4205'.") % t)
        if "marca" in c and not _ints(c["marca"], 6):
            err.append(T("%s: la marca necesita 6 números (icono, zona, x, y, z, escala).",
                         "%s: the marker needs 6 numbers (icon, zone, x, y, z, scale).") % t)
        if "salida" in c and not _ints(c["salida"], 3):
            err.append(T("%s: la posición de aparición necesita 3 números (x, z, altura).",
                         "%s: the spawn position needs 3 numbers (x, z, height).") % t)
        for k in ("imagen", "cierre_imagen"):
            if k not in c:
                continue
            if not (isinstance(c[k], str) and c[k].lower().endswith(IMG_EXT)):
                err.append(T("%s: %s debe ser una imagen .png, .webp, .jpg o .avif.",
                             "%s: %s must be a .png, .webp, .jpg or .avif image.") % (t, label_for(k)))
            elif not os.path.isfile(_abs(c[k], base)):
                warn.append(T("%s: no encuentro la imagen %s (se usará el panel del juego).",
                              "%s: can't find the image %s (the game panel will be used).") % (t, c[k]))
            if not c.get("saga"):
                warn.append(T("%s: %s solo se usa en capítulos que abren saga.",
                              "%s: %s is only used in chapters that open a saga.") % (t, label_for(k)))
        if "retratos" in c and not _ints(c["retratos"], 2):
            err.append(T("%s: retratos necesita 2 números (rival y tú).",
                         "%s: portraits need 2 numbers (rival and you).") % t)
        for g in TEXT_GROUPS:
            if (c.get(g + "es") or c.get(g + "en")) and not c.get(g + "en"):
                err.append(T("%s: falta el texto en inglés de %s.", "%s: the English text of %s is missing.")
                           % (t, T(*GROUP_NAME[g])))
            for lang in ("es", "en"):
                k = g + lang
                if k not in c:
                    continue
                if not (isinstance(c[k], list) and all(isinstance(x, str) for x in c[k])):
                    err.append(T("%s: %s debe ser una lista de textos.", "%s: %s must be a list of texts.")
                               % (t, label_for(k)))
                    continue
                for line in c[k]:
                    if line.lower().endswith(".png"):
                        if not os.path.isfile(_abs(line, base)):
                            err.append(T("%s: no encuentro la imagen %s.", "%s: can't find the image %s.") % (t, line))
                    elif len(line) > 60:
                        warn.append(T("%s, %s: línea de %d caracteres (máximo recomendado 60).",
                                      "%s, %s: line of %d characters (recommended maximum 60).")
                                    % (t, label_for(k), len(line)))
        if fight:
            if "retratos" not in c:
                err.append(T("%s: faltan los retratos (rival y tú).", "%s: portraits are missing (rival and you).") % t)
            if not (c.get("dialogo_es") or c.get("dialogo_en")):
                err.append(T("%s: falta la charla antes del combate.", "%s: the talk before the fight is missing.") % t)
            if not (c.get("victoria_es") or c.get("victoria_en")):
                err.append(T("%s: falta la charla tras la victoria.", "%s: the talk after the victory is missing.") % t)
            if c.get("saga") and not _ints(c.get("salida"), 3):
                err.append(T("%s: una saga nueva necesita posición de aparición (3 números).",
                             "%s: a new saga needs a spawn position (3 numbers).") % t)
            if i == len(caps):
                err.append(T("El último capítulo debe quedar sin combate: es el final de la historia.",
                             "The last chapter must have no fight: it is the story's ending."))
        else:
            if not (c.get("es") or c.get("en")):
                err.append(T("%s: el final necesita texto (en español o en inglés).",
                             "%s: the ending needs text (Spanish or English).") % t)
            if i < len(caps):
                warn.append(T("%s: sin combate es el final; los capítulos siguientes se ignoran.",
                              "%s: without a fight it is the ending; the chapters after it are ignored.") % t)
            if c.get("saga"):
                warn.append(T("%s: 'saga' no tiene efecto en el final.", "%s: 'saga' has no effect on the ending.") % t)
    # la casilla de Dragon Universe (codigo % 100) es de una sola campana: dos en la misma no conviven
    cp, code = _campana(), (data.get("historia") or {}).get("codigo")
    if cp and _is_int(code) and re.fullmatch(r"[A-Za-z0-9_\-]+", mod):
        try:
            mods = mods_dir() or cp.rb.default_mods()
            other = cp.other_campaigns(mods, os.path.join(mods, mod)).get(int(code) % 100)
        except OSError:
            other = None
        if other:
            err.append(T("La casilla %d de Dragon Universe ya la usa la campaña «%s»: elige otro protagonista.",
                         "Dragon Universe slot %d is already used by the campaign '%s': pick another hero.")
                       % (int(code) % 100, other))
        elif (cp.NATIVE_DU >> (int(code) % 100)) & 1:
            warn.append(T("La casilla %d ya tiene historia del juego: esta campaña la sustituye.",
                          "Slot %d already has a game story: this campaign replaces it.") % (int(code) % 100))
    return err, warn


def ret_label(r):
    s = T("entrada %s", "entry %s") % r.get("entrada", T("(falta)", "(missing)"))
    if r.get("reparar"):
        s += T("   reparar", "   repair")
    if r.get("png"):
        s += "   png"
    return s


def cap_label(i, c):
    """Título amigable del capítulo: «3 · vs Piccolo (combate 656)»; el final, «13 · Final»."""
    if "combate" not in c:
        s = T("Final", "Ending")
    else:
        rival = c.get("rival")
        if _is_int(rival):
            who = char_name(rival) or T("personaje %d", "character %d") % rival
            s = T("vs %s (combate %s)", "vs %s (fight %s)") % (who, c["combate"])
        else:
            s = T("Combate %s", "Fight %s") % c["combate"]
    return "%d · %s%s" % (i + 1, s, T(" · saga nueva", " · new saga") if c.get("saga") else "")


def backup(p):
    """Antes de sobrescribir, la version anterior va a campanas/respaldo/ (nada se borra)."""
    d = os.path.join(os.path.dirname(os.path.abspath(p)), "respaldo")
    os.makedirs(d, exist_ok=True)
    stem = os.path.splitext(os.path.basename(p))[0]
    shutil.copy2(p, os.path.join(d, "%s.%s.toml" % (stem, time.strftime("%Y%m%d-%H%M%S"))))


def _campana():
    """campana.py de esta carpeta (para la vista previa de estilos y el recorte de imágenes).
    None si falta numpy o Pillow: el editor sigue funcionando sin esas vistas previas."""
    global CP
    if CP is None:
        try:
            import campana as cp
        except ImportError:
            return None
        CP = cp
        DEF_STYLES.update(copy.deepcopy(cp.STYLES))
    return CP


def rel_ref(p, base):
    """Ruta relativa a la carpeta del .toml (o absoluta si está en otra unidad), con «/»."""
    try:
        return os.path.relpath(p, base).replace("\\", "/")
    except ValueError:
        return p.replace("\\", "/")


def font_ref(p, base):
    """Cómo se escribe una fuente: solo su nombre si está en Windows\\Fonts; si no, ruta relativa al .toml."""
    if os.path.normcase(os.path.dirname(os.path.abspath(p))) == os.path.normcase(WIN_FONTS):
        return os.path.basename(p)
    return rel_ref(p, base)


def style_default(name):
    _campana()
    return copy.deepcopy(DEF_STYLES.get(name) or DEF_STYLES["dialogo"])


def style_effective(data, name):
    """Valores que usa el juego para un estilo: los de la historia encima de los de campana.py."""
    eff = style_default(name)
    stored = ((data.get("historia") or {}).get("estilos") or {}).get(name) or {}
    eff.update(copy.deepcopy(stored))
    return eff


def style_diff(values, default):
    """Solo las claves que cambian respecto al valor por defecto (lo que se guarda en el TOML)."""
    return {k: v for k, v in values.items() if v != default.get(k)}


def put_style(data, name, stored):
    """Guarda (o quita, si `stored` esta vacio) un estilo; sin tablas vacias."""
    hist = data.setdefault("historia", {})
    est = dict(hist.get("estilos") or {})
    if stored:
        est[name] = dict(stored)
    else:
        est.pop(name, None)
    if est:
        hist["estilos"] = est
    else:
        hist.pop("estilos", None)


def style_preview(name, values, base, text=SAMPLE):
    """RGBA (numpy) de `text` dibujada con `values` como estilo `name` (campana.render, la misma imagen que
    va al juego). Mientras dibuja, pone esos valores en campana.STYLES y luego lo deja como estaba."""
    cp = _campana()
    old, old_dir = cp.STYLES.get(name), cp.TOML_DIR[0]
    try:
        cp.STYLES[name] = dict(values)
        cp.TOML_DIR[0] = base
        return cp.render(text, name)
    finally:
        if old is None:
            cp.STYLES.pop(name, None)
        else:
            cp.STYLES[name] = old
        cp.TOML_DIR[0] = old_dir


def text_bases(c):
    """Prefijos de los pares de texto de un capitulo: "" (es / en), "dialogo_" (dialogo_es / dialogo_en)..."""
    return sorted({k[:-2] for k, v in c.items() if (k in ("es", "en") or k.endswith(("_es", "_en")))
                   and isinstance(v, list)})


def fill_missing(data):
    """Si un texto existe en un idioma y falta en el otro, copia el que hay y lo marca como pendiente
    en [historia] traducir_pendiente = ["capitulo N clave", ...]. Sin red. Devuelve las marcas nuevas."""
    hist = data.setdefault("historia", {})
    pend = [p for p in hist.get("traducir_pendiente", []) if isinstance(p, str)]
    new = []
    for i, c in enumerate(data.get("capitulo", [])):
        for base in text_bases(c):
            es_k, en_k = base + "es", base + "en"
            if bool(c.get(es_k)) == bool(c.get(en_k)):
                continue                                   # ambos o ninguno: nada que copiar
            src, dst = (en_k, es_k) if c.get(en_k) else (es_k, en_k)
            c[dst] = list(c[src])
            new.append("capitulo %d %s" % (i + 1, dst))
    pend += [m for m in new if m not in pend]
    if pend:
        hist["traducir_pendiente"] = pend
    else:
        hist.pop("traducir_pendiente", None)
    return new


def prune_pending(data):
    """Quita de los pendientes los textos ya traducidos (distintos del otro idioma) o que ya no existen."""
    hist = data.get("historia", {})
    pend = hist.get("traducir_pendiente")
    if not isinstance(pend, list):
        return
    caps, keep = data.get("capitulo", []), []
    for p in pend:
        m = re.fullmatch(r"capitulo (\d+) (\S+)", str(p))
        if not m:
            keep.append(p)                                 # no es marca nuestra: se deja tal cual
            continue
        n, key = int(m.group(1)), m.group(2)
        if not (1 <= n <= len(caps) and key.endswith(("es", "en"))):
            continue
        c = caps[n - 1]
        other = key[:-2] + ("en" if key.endswith("es") else "es")
        if c.get(key) and c.get(key) == c.get(other):
            keep.append(p)
    if keep:
        hist["traducir_pendiente"] = keep
    else:
        hist.pop("traducir_pendiente", None)


def _music_ok(v):
    return _is_int(v) and 0 <= v <= 0xFFFFFFFF


def music_show(v):
    """135277 -> «2.4205» (banco.entrada)."""
    if _music_ok(v):
        return "%d.%d" % (v >> 16, v & 0xFFFF)
    return str(v)


def music_num(t):
    """«2.4205» o «135277» -> número. Cualquier otro texto se devuelve tal cual (check() lo marca)."""
    m = re.fullmatch(r"(\d+)\.(\d+)", t)
    if m and int(m.group(1)) <= 0xFFFF and int(m.group(2)) <= 0xFFFF:
        return int(m.group(1)) << 16 | int(m.group(2))
    return int(t) if re.fullmatch(r"\d+", t) else t


def music_view(data):
    """Casillas de música de la historia: lo que no está escrito en el TOML muestra el del juego."""
    hist = data.get("historia") or {}
    return {"historia": {"musica_narracion": hist.get("musica_narracion", MUS_NARR),
                         "musica_dialogo": hist.get("musica_dialogo", MUS_TALK)}}


def music_clean(data):
    """El TOML solo lleva la música que cambia: la del juego se quita; en un capítulo, la que es igual
    a la de la historia se quita (hereda). Así las claves aparecen solo si difieren del defecto."""
    hist = data.get("historia", {})
    for key, default in (("musica_narracion", MUS_NARR), ("musica_dialogo", MUS_TALK)):
        if hist.get(key) == default:
            hist.pop(key, None)
    talk = hist.get("musica_dialogo", MUS_TALK)
    for c in data.get("capitulo", []):
        if c.get("musica_dialogo") == talk:
            c.pop("musica_dialogo", None)


# ---------------------------------------------------------------- ventana: estilo y piezas
def init_fonts(root):
    fams = {f.lower() for f in tkfont.families(root.winfo_toplevel())}
    ui = "Segoe UI" if "segoe ui" in fams else "Helvetica"
    semi = "Segoe UI Semibold" if "segoe ui semibold" in fams else ui
    FONT.update(body=(ui, 10), small=(ui, 9), bold=(semi, 10), h1=(semi, 18), h3=(semi, 12))


def set_style(root, embedded):
    """Estilo del Mod Kit. Dentro del Mod Kit ya viene puesto; fuera, se aplica el mismo (modkit_gui.py)."""
    if not embedded:
        import modkit_gui as kit          # misma carpeta: colores, letra y estilos ttk idénticos
        kit.init_fonts(root)
        kit.setup_style(root)
        root.configure(bg=PAL["bg"])
    init_fonts(root)
    st = ttk.Style(root)
    st.configure("Ed.Card.TFrame", background=PAL["card"])
    st.configure("Ed.Card.TLabel", background=PAL["card"], foreground=PAL["text"])
    st.configure("Ed.Card.Field.TLabel", background=PAL["card"], foreground=PAL["text"], font=FONT["bold"])
    st.configure("Ed.Card.Hint.TLabel", background=PAL["card"], foreground=PAL["dim"], font=FONT["small"])
    st.configure("Ed.Card.TLabelframe", background=PAL["card"], bordercolor=PAL["line"],
                 lightcolor=PAL["card"], darkcolor=PAL["card"])
    st.configure("Ed.Card.TLabelframe.Label", background=PAL["card"], foreground=PAL["accent"], font=FONT["h3"])
    st.configure("Ed.Hint.TLabel", foreground=PAL["dim"], font=FONT["small"])
    st.configure("Ed.Title.TLabel", foreground=PAL["text"], font=FONT["h3"])
    st.configure("Ed.Field.TLabel", font=FONT["bold"])


def style_text(w, bg=None):
    """Caja de texto con el aspecto de los campos del Mod Kit."""
    w.configure(bg=bg or PAL["frame"], fg=PAL["text"], insertbackground=PAL["text"], relief="flat", bd=0,
                highlightthickness=1, highlightbackground=PAL["line"], highlightcolor=PAL["accent"],
                selectbackground=PAL["accent_dim"], selectforeground=PAL["text"], font=FONT["body"],
                padx=8, pady=6)


def style_list(w):
    """Lista (capítulos, retratos) con el aspecto del Mod Kit."""
    w.configure(bg=PAL["frame"], fg=PAL["text"], selectbackground=PAL["accent_dim"],
                selectforeground=PAL["text"], relief="flat", bd=0, highlightthickness=1,
                highlightbackground=PAL["line"], highlightcolor=PAL["accent"], activestyle="none",
                font=FONT["body"])


def card(parent, title, pad=14):
    """Tarjeta del Mod Kit: marco con título, fondo de tarjeta y borde discreto."""
    return ttk.LabelFrame(parent, text=title, style="Ed.Card.TLabelframe", padding=pad)


def follow_width(frame, labels, pad=60):
    """Los textos de la tarjeta se ajustan a su ancho (sin cortarse en ventanas pequeñas)."""
    def fit(e):
        for lb in labels:
            lb.configure(wraplength=max(260, e.width - pad))
    frame.bind("<Configure>", fit, add="+")


class Tip:
    """Globo de ayuda: aparece al pasar el raton por encima."""

    def __init__(self, widget, text):
        self.widget, self.text, self.win = widget, text, None
        widget.bind("<Enter>", self.show, add="+")
        widget.bind("<Leave>", self.hide, add="+")

    def show(self, _e=None):
        if self.win or not self.text:
            return
        x = self.widget.winfo_rootx() + 16
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4
        self.win = w = tk.Toplevel(self.widget)
        w.wm_overrideredirect(True)
        w.wm_geometry("+%d+%d" % (x, y))
        w.configure(bg=PAL["line"])
        tk.Label(w, text=self.text, justify="left", wraplength=460, bg=PAL["card_hi"], fg=PAL["text"],
                 font=FONT["small"], padx=10, pady=6).pack(padx=1, pady=1)

    def hide(self, _e=None):
        if self.win:
            self.win.destroy()
            self.win = None


class ScrollBox(ttk.Frame):
    """Zona con barra de desplazamiento: el contenido va en .inner (la rueda del raton la mueve)."""

    def __init__(self, parent):
        super().__init__(parent)
        self.canvas = tk.Canvas(self, highlightthickness=0, bd=0, bg=PAL["bg"])
        vs = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=vs.set)
        vs.pack(side="right", fill="y")
        self.canvas.pack(side="left", fill="both", expand=True)
        self.inner = ttk.Frame(self.canvas, padding=(4, 0, 12, 0))
        win = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.inner.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>", lambda e: self.canvas.itemconfigure(win, width=e.width))
        self.canvas._scrollframe = self.inner._scrollframe = self     # App._wheel / Mod Kit: rueda

    def scroll(self, delta):
        if self.inner.winfo_reqheight() > self.canvas.winfo_height():
            self.canvas.yview_scroll((-1 if delta > 0 else 1) * max(1, abs(int(delta / 120))) * 3, "units")


def thumb_image(path, base, size, bg, master, crop=False):
    """Miniatura PNG sobre el color de la tarjeta. None si falta Pillow, el archivo o no se puede leer.
    crop=True: recorte centrado 660x420 como en el juego (png2pic.fit)."""
    if Image is None or not path:
        return None
    full = _abs(str(path), base)
    if not os.path.isfile(full):
        return None
    try:
        with Image.open(full) as src:
            im = src.convert("RGBA")
        cp = _campana() if crop else None
        if cp is not None:
            im = Image.fromarray(cp.png2pic.fit(im, (660, 420)))
        im.thumbnail((size, size))
        back = Image.new("RGBA", im.size, tuple(int(bg[i:i + 2], 16) for i in (1, 3, 5)) + (255,))
        back.alpha_composite(im)
        return ImageTk.PhotoImage(back.convert("RGB"), master=master)
    except (OSError, ValueError, TypeError, tk.TclError):      # imagen rota: sin vista previa
        return None


SCENE_W, SCENE_H, SCENE_K = 420, 300, 0.45     # vista previa del dialogo; la caja 541x530 se reduce x0.45
_SRC = {}                                        # cache de PNG: ruta -> (fecha, imagen)


def portrait_art(path, escala, dx, dy):
    """Arte del retrato con las mismas cuentas que campana.py, recortado a la caja 541x530 del lienzo 1024x1024."""
    mt = os.path.getmtime(path)
    if path not in _SRC or _SRC[path][0] != mt:
        with Image.open(path) as f:
            _SRC[path] = (mt, f.convert("RGBA"))
    im = _SRC[path][1]
    k = min(541 / im.width, 530 / im.height) * escala
    im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    canvas = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
    canvas.paste(im, ((541 - im.width) // 2 + int(dx), 530 - im.height + int(dy)), im)
    return canvas.crop((0, 0, 541, 530))


def _mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def scene_image(art, line=None):
    """Dialogo aproximado (420x300): cielo y suelo, retrato abajo a la derecha y globo de texto a la izquierda.
    `line`: RGBA (numpy) de la frase tal como va al juego (campana.render); sin ella, un texto de muestra."""
    W, H = SCENE_W, SCENE_H
    im = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(im)
    h0 = int(H * 0.72)
    for y in range(H):
        if y < h0:
            c = _mix((96, 150, 220), (205, 233, 250), y / h0)
        else:
            c = _mix((104, 150, 84), (58, 96, 52), (y - h0) / (H - h0))
        d.line([(0, y), (W, y)], fill=c)
    bw, bh = round(541 * SCENE_K), round(530 * SCENE_K)
    x0 = W - bw - 6
    if art is not None:
        small = art.resize((bw, bh), Image.LANCZOS)
        im.paste(small, (x0, H - bh), small)
    d.rectangle([x0, H - bh, x0 + bw - 1, H - 1], outline=(235, 235, 235))   # limite de la caja
    x1, y1, x2, y2 = 12, 16, x0 - 16, 118
    d.rounded_rectangle([x1, y1, x2, y2], radius=12, fill=(255, 255, 255), outline=(40, 40, 40), width=2)
    d.polygon([(x2 - 30, y2 - 2), (x2 - 8, y2 - 2), (x2 + 4, y2 + 12)], fill=(255, 255, 255))
    if line is not None:                               # la imagen real del texto, encajada en el globo
        t = Image.fromarray(line)
        k = min(1.0, (x2 - x1 - 16) / t.width, (y2 - y1 - 12) / t.height)
        t = t.resize((max(1, round(t.width * k)), max(1, round(t.height * k))), Image.LANCZOS)
        im.paste(t, ((x1 + x2 - t.width) // 2, (y1 + y2 - t.height) // 2), t)
        return im
    try:                                               # letra del sistema (con acentos y ¡); si no, la de Pillow
        font = ImageFont.truetype("segoeui.ttf", 13)
    except OSError:
        try:
            font = ImageFont.load_default(size=13)
        except TypeError:                              # Pillow antiguo
            font = ImageFont.load_default()
    d.multiline_text((x1 + 12, y1 + 10), T("¡Vamos allá!\nNo te confíes.", "Let's go!\nDon't get cocky."),
                     fill=(20, 20, 20), font=font, spacing=4)
    return im


class Preview(tk.Frame):
    """Vista previa de una PNG. Sin imagen (o sin Pillow) no muestra nada."""

    def __init__(self, master, caption="", size=THUMB, bg=None, crop=False):
        super().__init__(master, bg=bg or PAL["card"])
        self.size, self.bg_hex, self.img, self.crop = size, bg or PAL["card"], None, crop
        self.pic = tk.Label(self, bg=self.bg_hex, bd=0)
        self.pic.pack()
        if caption:
            tk.Label(self, text=caption, bg=self.bg_hex, fg=PAL["dim"], font=FONT["small"]).pack()

    def show(self, path, base):
        self.img = thumb_image(path, base, self.size, self.bg_hex, self, self.crop)
        self.pic.configure(image=self.img if self.img is not None else "")
        return self.img is not None


class FieldSet:
    """Formulario: cada clave (o ruta) se lee y escribe en el diccionario TOML. Vacio = no se guarda."""

    def __init__(self, parent, specs, browse=None):
        self.w, self.extra, self.filled, self.browse = {}, {}, {}, browse
        self.row_of = {}                    # clave -> fila de la rejilla (para poner cosas junto a un campo)
        fr = self.frame = ttk.Frame(parent, style="Ed.Card.TFrame")
        fr.pack(fill="x", expand=True)
        fr.columnconfigure(1, weight=1)
        row = 0
        for key, kind, label, tip, opts in specs:
            self.row_of[key] = row
            text, tip_t = T(*label), T(*tip)
            if kind == "lines":
                lb = ttk.Label(fr, text=text, style="Ed.Card.Field.TLabel")
                lb.grid(row=row, column=0, columnspan=2, sticky="w", pady=(12, 3))
                t = tk.Text(fr, height=3, width=70, wrap="word")
                style_text(t)
                t.grid(row=row + 1, column=0, columnspan=2, sticky="ew")
                Tip(lb, tip_t)
                Tip(t, tip_t)
                self.w[key] = (kind, t, None)
                row += 2
                continue
            if kind == "bool":
                var = tk.BooleanVar(fr)
                cb = ttk.Checkbutton(fr, text=text, variable=var, style="Card.TCheckbutton")
                cb.grid(row=row, column=0, columnspan=2, sticky="w", pady=6)
                Tip(cb, tip_t)
                self.w[key] = (kind, var, None)
                row += 1
                continue
            lb = ttk.Label(fr, text=text, style="Ed.Card.TLabel")
            lb.grid(row=row, column=0, sticky="w", padx=(0, 16), pady=5)
            Tip(lb, tip_t)
            box = ttk.Frame(fr, style="Ed.Card.TFrame")
            box.grid(row=row, column=1, sticky="w", pady=5)
            built = None
            if kind in ("ints", "marca"):
                wd, line = [], ttk.Frame(box, style="Ed.Card.TFrame")
                line.pack(side="top", anchor="w")
                for _ in range(opts):
                    e = ttk.Entry(line, width=8)
                    e.pack(side="left", padx=(0, 6))
                    Tip(e, tip_t)
                    wd.append(e)
                if kind == "marca":           # el preajuste va debajo: así no se sale de la tarjeta
                    pre = ttk.Frame(box, style="Ed.Card.TFrame")
                    pre.pack(side="top", anchor="w", pady=(6, 0))
                    ttk.Label(pre, text=T("o preajuste:", "or preset:"), style="Ed.Card.Hint.TLabel").pack(side="left")
                    cb = ttk.Combobox(pre, values=[T(m[0], m[1]) for m in MARCAS], state="readonly", width=34)
                    cb.pack(side="left", padx=(8, 0))
                    cb.bind("<<ComboboxSelected>>", lambda _e, es=wd, c=cb: self._preset(es, c))
                    Tip(cb, T("Rellena la marca con una de las de historia_freezer.toml.",
                              "Fills the marker with one of the ones in historia_freezer.toml."))
                built = opts
            elif kind in ("combo", "music"):
                built = _opts(opts)
                wd = ttk.Combobox(box, values=[x[0] for x in built], width=46)
                wd.pack(side="left")
                Tip(wd, tip_t)
            elif kind == "spin":
                wd = ttk.Spinbox(box, from_=0, to=37 if key == "animacion_mapa" else 9, width=5, justify="center")
                wd.pack(side="left")
                ttk.Label(box, text=tip_t, style="Ed.Card.Hint.TLabel").pack(side="left", padx=(12, 0))
                Tip(wd, tip_t)
            elif kind == "file":
                wd = ttk.Entry(box, width=40)
                wd.pack(side="left")
                ttk.Button(box, text=T("Elegir...", "Choose..."), style="Small.TButton",
                           command=lambda e=wd: self.browse(e)).pack(side="left", padx=8)
                Tip(wd, tip_t)
            else:
                wd = ttk.Entry(box, width=12 if kind == "int" else 46)
                wd.pack(side="left")
                Tip(wd, tip_t)
            self.w[key] = (kind, wd, built)
            row += 1

    def _preset(self, ents, cb):
        i = cb.current()
        if i >= 0:
            for e, v in zip(ents, MARCAS[i][2]):
                e.delete(0, "end")
                e.insert(0, str(v))

    def get(self):
        out = {}
        for key, (kind, wd, opts) in self.w.items():
            v = _read(kind, wd, opts)
            if key in self.extra and _texts(wd) == self.filled.get(key):
                v = self.extra[key]                 # lista con mas valores de los que caben: se conserva
            out[key] = v
        return out

    def set(self, root):
        for key, (kind, wd, opts) in self.w.items():
            v = _nested(root, _path(key))
            _write(kind, wd, opts, v)
            if kind in ("ints", "marca"):
                self.filled[key] = _texts(wd)
                if isinstance(v, list) and len(v) != opts:
                    self.extra[key] = v
                else:
                    self.extra.pop(key, None)


def _read(kind, wd, opts):
    if kind == "music":                             # «2.4205», número o vacío (vacío = no se guarda)
        t = wd.get().strip()
        if not t:
            return None
        for lab, x in opts:
            if lab == t:
                return x
        return music_num(t)
    if kind == "bool":
        return True if wd.get() else None
    if kind == "lines":
        return [x.strip() for x in wd.get("1.0", "end").splitlines() if x.strip()] or None
    if kind == "combo":
        t = wd.get().strip()
        if not t:
            return None
        for lab, x in opts:
            if lab == t:
                return x
        m = re.match(r"(-?\d+)(\s|$)", t)          # "96 - Nombre" o "96"
        return int(m.group(1)) if m else t
    if kind == "csv":
        ts = [x.strip() for x in wd.get().split(",") if x.strip()]
        return [_num(x) for x in ts] or None
    if kind in ("int", "spin"):
        t = wd.get().strip()
        return _num(t) if t else None
    if kind in ("ints", "marca"):
        ts = [e.get().strip() for e in wd]
        return [_num(t) for t in ts] if any(ts) else None
    return wd.get().strip() or None                 # text, file


def _write(kind, wd, opts, v):
    if kind == "music":
        lab = next((lab for lab, x in opts if x == v), None)
        wd.set(lab if lab is not None else ("" if v is None else music_show(v)))
    elif kind == "bool":
        wd.set(v is True)
    elif kind == "lines":
        wd.delete("1.0", "end")
        wd.insert("1.0", "\n".join(v) if isinstance(v, list) else ("" if v is None else str(v)))
    elif kind == "combo":
        wd.set(next((lab for lab, x in opts if x == v), "" if v is None else str(v)))
    elif kind in ("ints", "marca"):
        vals = v if isinstance(v, list) else []
        for i, e in enumerate(wd):
            e.delete(0, "end")
            e.insert(0, str(vals[i]) if i < len(vals) else "")
    else:
        if isinstance(v, list):
            text = ", ".join(str(x) for x in v)
        else:
            text = "" if v is None else str(v)
        wd.delete(0, "end")
        wd.insert(0, text)


def _texts(wd):
    return tuple(e.get() for e in wd)


# ---------------------------------------------------------------- ventana principal
class App:
    """Editor. embedded=True: se construye dentro de un marco del Mod Kit (sin ventana propia).
    runner(cmd, titulo): ejecuta montar/probar con el registro del Mod Kit (si no, lo hace el propio editor)."""

    def __init__(self, root, path=None, embedded=False, runner=None, log_fn=None):
        self.root, self.embedded, self.runner, self.log_fn = root, embedded, runner, log_fn
        self.path = None
        self.data, self.raw = {}, []
        self.saved = None                # texto guardado; None = nunca guardado
        self.sel_cap = self.sel_ret = None
        self._ret_loading = False        # True mientras se ponen los deslizadores (no escriben en los datos)
        self.q, self.running = queue.Queue(), False
        self.sty_w, self.sty_cur = None, "dialogo"   # pestaña de estilos (None si falta numpy/Pillow)
        if not embedded:
            root.title(T("Editor de historias", "Story editor"))
            root.geometry("1240x900")
            root.minsize(1000, 700)
            root.protocol("WM_DELETE_WINDOW", self.on_close)
            root.bind_all("<MouseWheel>", self._wheel, add="+")
        set_style(root, embedded)
        self._build()
        if not (path and self.open_path(path)):
            self.new(ask=False)

    # ----- interfaz
    def _build(self):
        r = self.root
        # arriba, en una fila: acciones siempre a la vista (Montar mod es la principal)
        top = ttk.Frame(r, padding=(14, 10, 14, 8))
        top.pack(fill="x")
        self.btn_montar = ttk.Button(top, text=T("Montar mod", "Build mod"), style="Big.Accent.TButton",
                                     command=self.montar)
        self.btn_montar.pack(side="right")
        Tip(self.btn_montar, T("Revisa la historia, la guarda y crea el mod en la carpeta mods/.",
                               "Checks the story, saves it and creates the mod in the mods/ folder."))
        self.btn_probar = ttk.Button(top, text=T("Probar", "Test"), command=self.probar)
        self.btn_probar.pack(side="right", padx=(0, 10))
        Tip(self.btn_probar, T("Ejecuta la autocomprobación del creador de campañas.",
                               "Runs the self-check of the campaign builder."))
        ttk.Button(top, text=T("Guardar como...", "Save as..."), command=self.save_as).pack(side="right", padx=(0, 10))
        ttk.Button(top, text=T("Guardar", "Save"), command=self.save).pack(side="right", padx=(0, 10))
        ttk.Button(top, text=T("Nueva historia", "New story"), command=lambda: self.new(True)).pack(side="left")
        ttk.Button(top, text=T("Abrir...", "Open..."), command=self.open_dialog).pack(side="left", padx=6)
        self.title_lbl = ttk.Label(top, style="Ed.Title.TLabel")
        self.title_lbl.pack(side="left", padx=16)

        self.logt = None                     # dentro del Mod Kit los mensajes van a su registro (log_fn)
        if not self.embedded:
            lg = card(r, T("Registro: avisos y mensajes", "Log: warnings and messages"), pad=6)
            lg.pack(side="bottom", fill="x", padx=14, pady=(0, 6))
            self.logt = tk.Text(lg, height=6, wrap="word", state="disabled")
            style_text(self.logt, bg=PAL["log_bg"])
            ls = ttk.Scrollbar(lg, orient="vertical", command=self.logt.yview)
            self.logt.configure(yscrollcommand=ls.set)
            self.logt.pack(side="left", fill="both", expand=True)
            ls.pack(side="left", fill="y")

        # dentro del Mod Kit, altura mínima: si la ventana es baja, la página se desplaza en vez de recortarse
        self.nb = ttk.Notebook(r, height=230) if self.embedded else ttk.Notebook(r)
        self.nb.pack(fill="both", expand=True, padx=14, pady=(4, 4))
        self._tab_story()
        self._tab_chapters()
        self._tab_portraits()
        self._tab_advanced()
        self._tab_styles()
        self.nb.bind("<<NotebookTabChanged>>", lambda _e: self._tab_shown(), add="+")

    def _tab(self, title, helper):
        fr = ttk.Frame(self.nb, padding=(16, 14, 16, 10))
        self.nb.add(fr, text=title)
        lb = ttk.Label(fr, text=helper, style="Ed.Hint.TLabel", justify="left")
        lb.pack(fill="x", pady=(0, 12))
        fr.bind("<Configure>", lambda e, w=lb: w.configure(wraplength=max(300, e.width - 40)), add="+")
        return fr

    def _tab_story(self):
        t = self._tab(T("1. Tu historia", "1. Your story"),
                      T("Pon el nombre de tu historia y la carpeta donde se guardará el mod. Los personajes de la "
                        "rueda son los que se añaden a la rueda de selección de Dragon Universe.",
                        "Give your story a name and choose the folder where the mod is saved. Wheel characters are "
                        "the ones added to the Dragon Universe selection wheel."))
        sb = ScrollBox(t)
        sb.pack(fill="both", expand=True)
        steps = [T("1.  Pon el nombre de la historia y la carpeta donde se crea el mod.",
                   "1.  Give the story a name and the folder where the mod is created."),
                 T("2.  En «Capítulos», cada capítulo es un combate con su rival y sus charlas. El último es el "
                   "final, sin combate.",
                   "2.  In 'Chapters', each chapter is a fight with its rival and talks. The last one is the "
                   "ending, with no fight."),
                 T("3.  En «Retratos», elige las caras que salen en las charlas, o tu propia imagen PNG.",
                   "3.  In 'Portraits', pick the faces shown in the talks, or your own PNG image."),
                 T("4.  Pulsa «Montar mod»: se revisa la historia, se guarda y el mod aparece en la carpeta mods/.",
                   "4.  Press 'Build mod': the story is checked, saved and the mod appears in the mods/ folder.")]
        how = card(sb.inner, T("¿Cómo funciona?", "How does it work?"))
        how.pack(fill="x", pady=(0, 12), padx=(0, 4))
        labels = [ttk.Label(how, text=s, style="Ed.Card.TLabel", justify="left") for s in steps]
        labels.append(ttk.Label(how, text=T("Los campos con * son obligatorios. Un campo vacío no se guarda. "
                                            "Pasa el ratón por encima de un campo para ver su ayuda.",
                                            "Fields marked * are required. An empty field is not saved. Hover "
                                            "over a field to see its help."), style="Ed.Card.Hint.TLabel",
                                justify="left"))
        for lb in labels:
            lb.pack(fill="x", anchor="w", pady=2)
        follow_width(how, labels)
        box = card(sb.inner, T("Datos generales", "General info"))
        box.pack(fill="x", anchor="n", padx=(0, 4))
        self.f_camp = FieldSet(box, CAMP_SPECS)
        lang = card(sb.inner, T("Idioma de los textos", "Story text language"))
        lang.pack(fill="x", anchor="n", padx=(0, 4), pady=(12, 0))
        self.f_lang = FieldSet(lang, IDIOMA_SPECS)
        ttk.Label(lang, style="Ed.Card.Hint.TLabel", justify="left", wraplength=640,
                  text=T("Si un texto está en un idioma y falta en el otro, se copia el que hay y se marca como "
                         "pendiente de traducir. No usa internet: la traducción la haces tú.",
                         "If a text is in one language and missing in the other, the existing one is copied and "
                         "marked as pending translation. No internet: you do the translation.")).pack(anchor="w", pady=(10, 6))
        bt = ttk.Button(lang, text=T("Rellenar traducciones que faltan", "Fill missing translations"),
                        command=self.fill_translations)
        bt.pack(anchor="w")
        Tip(bt, T("Copia los textos que faltan desde el otro idioma y los marca como pendientes.",
                  "Copies the missing texts from the other language and marks them as pending."))
        mus = card(sb.inner, T("Música", "Music"))
        mus.pack(fill="x", anchor="n", padx=(0, 4), pady=(12, 0))
        self.f_music = FieldSet(mus, MUSIC_SPECS)
        ttk.Label(mus, style="Ed.Card.Hint.TLabel", justify="left", wraplength=640,
                  text=T("Escribe un número (ej. 2.4205) o elige de la lista. Solo se guarda lo que cambia respecto "
                         "al juego. En cada capítulo puedes cambiar la música de la charla tras el combate.",
                         "Type a number (e.g. 2.4205) or pick from the list. Only what differs from the game is saved. "
                         "Each chapter can change the music of the talk after the fight.")).pack(anchor="w", pady=(10, 0))

    def _tab_chapters(self):
        t = self._tab(T("2. Capítulos", "2. Chapters"),
                      T("Cada capítulo es un combate; el último es el final (sin combate). Elige un capítulo de la "
                        "lista para editarlo. Los campos vacíos no se guardan. En los textos, escribe cada uno en "
                        "español y en inglés: el juego usa el que corresponde a su idioma.",
                        "Each chapter is a fight; the last one is the ending (no fight). Pick a chapter from the list "
                        "to edit it. Empty fields are not saved. For texts, write each one in Spanish and in English: "
                        "the game uses the one for its language."))
        pw = ttk.Panedwindow(t, orient="horizontal")
        pw.pack(fill="both", expand=True)
        left = ttk.Frame(pw, padding=(0, 0, 12, 0))
        pw.add(left, weight=1)
        ttk.Label(left, text=T("Capítulos", "Chapters"), style="Ed.Field.TLabel").pack(anchor="w", pady=(0, 6))
        lf = ttk.Frame(left)
        lf.pack(fill="both", expand=True)
        self.lst_cap = tk.Listbox(lf, exportselection=False, width=34)
        style_list(self.lst_cap)
        sc = ttk.Scrollbar(lf, orient="vertical", command=self.lst_cap.yview)
        self.lst_cap.configure(yscrollcommand=sc.set)
        self.lst_cap.pack(side="left", fill="both", expand=True)
        sc.pack(side="left", fill="y")
        self.lst_cap.bind("<<ListboxSelect>>", self._on_cap)
        cb = ttk.Frame(left)
        cb.pack(fill="x", pady=(8, 0))
        for c in (0, 1):
            cb.columnconfigure(c, weight=1, uniform="cb")
        ttk.Button(cb, text=T("Añadir", "Add"), command=self.cap_add).grid(row=0, column=0, sticky="ew", padx=(0, 3), pady=2)
        ttk.Button(cb, text=T("Duplicar", "Duplicate"), command=self.cap_dup).grid(row=0, column=1, sticky="ew", padx=(3, 0), pady=2)
        ttk.Button(cb, text=T("Subir", "Up"), command=lambda: self.cap_move(-1)).grid(row=1, column=0, sticky="ew", padx=(0, 3), pady=2)
        ttk.Button(cb, text=T("Bajar", "Down"), command=lambda: self.cap_move(1)).grid(row=1, column=1, sticky="ew", padx=(3, 0), pady=2)
        ttk.Button(cb, text=T("Borrar", "Delete"), style="Danger.TButton", command=self.cap_del).grid(
            row=2, column=0, columnspan=2, sticky="ew", pady=(8, 2))
        right = ttk.Frame(pw)
        pw.add(right, weight=4)
        self.box_cap = ScrollBox(right)
        self.box_cap.pack(fill="both", expand=True)
        self.f_caps = []
        for title, specs in CAP_GROUPS:
            g = card(self.box_cap.inner, T(*title))
            g.pack(fill="x", pady=(0, 12), padx=(0, 4))
            self.f_caps.append(FieldSet(g, specs))
        self.card_img = card(self.box_cap.inner, T("Imágenes de la saga (solo si el capítulo abre saga)",
                                                   "Saga images (only if the chapter opens a saga)"))
        self.f_img = FieldSet(self.card_img, IMG_SPECS, browse=self.pick_img)   # se muestra con _img_sync
        self.f_caps.append(self.f_img)
        self.img_prev = []
        for key in ("imagen", "cierre_imagen"):
            p = Preview(self.f_img.frame, T("Vista previa (recorte 660x420)", "Preview (660x420 crop)"), 150, None, True)
            p.grid(row=self.f_img.row_of[key], column=2, padx=(14, 0), sticky="w")
            p.grid_remove()
            self.img_prev.append(p)
            for seq in ("<KeyRelease>", "<FocusOut>", "<Return>"):
                self.f_img.w[key][1].bind(seq, lambda _e: self.refresh_img_prev(), add="+")
        self._cap_extras()

    def _cap_extras(self):
        """Títulos de la lista al cambiar combate o rival; vistas previas junto a los retratos del capítulo."""
        rival = self.f_caps[1].w["rival"][1]
        for wd in (self.f_caps[0].w["combate"][1], rival):
            wd.bind("<FocusOut>", lambda _e: self.flush_cap(), add="+")
        rival.bind("<<ComboboxSelected>>", lambda _e: self.flush_cap(), add="+")
        row = self.f_caps[1].row_of["retratos"]                  # fila de «Retratos» en el capítulo
        self.cap_prev = []
        for k, who in enumerate((T("Rival", "Rival"), T("Tú", "You"))):
            p = Preview(self.f_caps[1].frame, who, THUMB)
            p.grid(row=row, column=2 + k, padx=(14, 0), sticky="w")
            p.grid_remove()
            self.cap_prev.append(p)
        for e in self.f_caps[1].w["retratos"][1]:
            e.bind("<FocusOut>", lambda _e: self.refresh_cap_thumbs(), add="+")
            e.bind("<KeyRelease>", lambda _e: self.refresh_cap_thumbs(), add="+")
        self.f_caps[3].w["saga"][1].trace_add("write", lambda *_a: self._img_sync())   # «Mapa» > saga

    def _tab_portraits(self):
        t = self._tab(T("3. Retratos", "3. Portraits"),
                      T("Los retratos son las caras que salen en las charlas. Si el retrato del juego sale sin "
                        "terminar, marca «Reparar». Para usar tu propia imagen, elige un PNG con fondo transparente.",
                        "Portraits are the faces shown in the talks. If the game's portrait is unfinished, tick "
                        "'Repair'. To use your own image, choose a PNG with a transparent background."))
        sb = ScrollBox(t)                    # la colocación con vista previa es alta: la pestaña se desplaza
        sb.pack(fill="both", expand=True)
        body = sb.inner
        left = card(body, T("Tus retratos", "Your portraits"), pad=12)
        left.pack(side="left", fill="y", padx=(0, 12), anchor="n")
        lf = ttk.Frame(left)
        lf.pack(fill="both", expand=True)
        self.lst_ret = tk.Listbox(lf, height=12, width=28, exportselection=False)
        style_list(self.lst_ret)
        self.lst_ret.pack(side="left", fill="y")
        self.lst_ret.bind("<<ListboxSelect>>", self._on_ret)
        rb = ttk.Frame(lf)
        rb.pack(side="left", padx=(8, 0), anchor="n")
        ttk.Button(rb, text=T("Añadir", "Add"), command=self.ret_add).pack(fill="x", pady=2)
        ttk.Button(rb, text=T("Borrar", "Delete"), style="Danger.TButton", command=self.ret_del).pack(fill="x", pady=2)
        right = card(body, T("Vista previa", "Preview"), pad=12)
        right.pack(side="right", fill="y", padx=(12, 0), anchor="n")
        self.ret_big = Preview(right, "", THUMB_BIG, PAL["card"])
        self.ret_big.pack(pady=(4, 8))
        self.ret_hint = ttk.Label(right, style="Ed.Card.Hint.TLabel", justify="center", wraplength=230)
        self.ret_hint.pack()
        box = card(body, T("Retrato seleccionado", "Selected portrait"))
        box.pack(side="left", fill="both", expand=True, anchor="n")
        self.f_ret = FieldSet(box, RET_SPECS, browse=self.pick_png)
        pe = self.f_ret.w["png"][1]
        for seq in ("<KeyRelease>", "<FocusOut>", "<Return>"):
            pe.bind(seq, lambda _e: self.refresh_ret_preview(), add="+")
        # colocacion del PNG propio (solo si el retrato tiene PNG): escala, X, Y y vista previa en vivo
        self.ret_place = ttk.Frame(box, style="Ed.Card.TFrame")
        ttk.Label(self.ret_place, text=T("Colocación del PNG", "PNG placement"),
                  style="Ed.Card.Field.TLabel").pack(anchor="w")
        ttk.Label(self.ret_place, text="forma_jugador 5 = Mecha Freezer (Freezer)",
                  style="Ed.Card.Hint.TLabel").pack(anchor="w")
        self.sl = {}
        for name, lo, hi, res, lab in (("escala", 0.5, 1.5, 0.01, ("Tamaño", "Size")),
                                       ("x", -200, 200, 1, ("Izquierda / derecha", "Left / right")),
                                       ("y", -200, 200, 1, ("Arriba / abajo (+ abajo)", "Up / down (+ down)"))):
            row = ttk.Frame(self.ret_place, style="Ed.Card.TFrame")
            row.pack(fill="x", pady=2)
            ttk.Label(row, text=T(*lab), style="Ed.Card.TLabel", width=24).pack(side="left")
            s = tk.Scale(row, from_=lo, to=hi, resolution=res, orient="horizontal", length=260, showvalue=False,
                         command=self._ret_place_write, bg=PAL["card"], fg=PAL["text"], troughcolor=PAL["frame"],
                         activebackground=PAL["accent"], highlightthickness=0, bd=0, relief="flat",
                         sliderrelief="flat", font=FONT["body"])
            s.pack(side="left")
            val = ttk.Label(row, style="Ed.Card.Hint.TLabel", width=9)
            val.pack(side="left", padx=(8, 0))
            self.sl[name] = (s, val)
        ttk.Button(self.ret_place, text=T("Restablecer", "Reset"), style="Small.TButton",
                   command=self._ret_reset).pack(anchor="w", pady=(6, 0))
        self.ret_scene = tk.Label(self.ret_place, bd=0, bg=PAL["card"], fg=PAL["dim"], font=FONT["small"])
        self.ret_scene.pack(anchor="w", pady=(10, 0))
        self.ret_scene_img = None

    def _tab_advanced(self):
        t = self._tab(T("4. Avanzado", "4. Advanced"),
                      T("Opciones técnicas de la historia. Si no sabes qué hacen, déjalas: ya traen los valores de "
                        "la historia de Freezer. Los campos con * son obligatorios.",
                        "Technical story options. If you don't know what they do, leave them: they already have "
                        "Freezer's story values. Fields marked * are required."))
        sb = ScrollBox(t)
        sb.pack(fill="both", expand=True)
        self.f_hists = []
        for title, specs in HIST_GROUPS:
            g = card(sb.inner, T(*title))
            g.pack(fill="x", pady=(0, 12), padx=(0, 4))
            self.f_hists.append(FieldSet(g, specs))

    def _tab_shown(self):
        self.refresh_cap_thumbs()
        self.refresh_ret_preview()

    def _wheel(self, e):                  # solo ventana propia (dentro del Mod Kit lo hace el Mod Kit)
        try:
            w = self.root.winfo_containing(e.x_root, e.y_root)
        except (KeyError, tk.TclError):
            return
        while w is not None:
            if isinstance(w, (ttk.Treeview, ttk.Combobox, ttk.Spinbox)):
                return
            if isinstance(w, (tk.Text, tk.Listbox)):
                top, bot = w.yview()
                if bot - top < 0.999:           # tiene contenido propio que desplazar
                    return
            sf = getattr(w, "_scrollframe", None)
            if sf is not None:
                sf.scroll(e.delta)
                return
            w = w.master

    def log(self, msg):
        if self.logt is None:                # dentro del Mod Kit: a su registro
            if self.log_fn:
                self.log_fn(msg)
            return
        self.logt.configure(state="normal")
        self.logt.insert("end", msg + "\n")
        self.logt.see("end")
        self.logt.configure(state="disabled")

    def update_title(self):
        name = os.path.basename(self.path) if self.path else T("sin guardar", "not saved yet")
        if not self.embedded:
            self.root.title("%s - %s" % (T("Editor de historias", "Story editor"), name))
        self.title_lbl.configure(text=T("Historia: ", "Story: ") +
                                 (name if self.path else T("(sin guardar)", "(not saved)")))

    def _base(self):
        return os.path.dirname(os.path.abspath(self.path)) if self.path else CAMPANAS

    # ----- datos <-> formularios
    def flush_all(self):
        self.flush_cap()
        self.flush_ret()
        apply(self.data, self.f_camp.get())
        apply(self.data, self.f_lang.get())
        apply(self.data, self.f_music.get())
        for fs in self.f_hists:
            apply(self.data, fs.get())
        music_clean(self.data)
        prune_pending(self.data)

    def to_text(self):
        self.flush_all()
        return build_text(self.data, self.raw)

    def is_dirty(self):
        return self.saved is None or self.to_text() != self.saved

    def confirm_discard(self):
        if not self.is_dirty():
            return True
        return messagebox.askyesno(T("Cambios sin guardar", "Unsaved changes"),
                                   T("Hay cambios sin guardar. ¿Descartarlos?",
                                     "There are unsaved changes. Discard them?"), parent=self.root)

    def load(self, data, raw, path, saved):
        self.data, self.raw, self.path = data, raw, path
        self.data.setdefault("campana", {})
        self.data.setdefault("historia", {})
        self.data.setdefault("capitulo", [])
        self.data.setdefault("retrato", [])
        self.sel_cap = self.sel_ret = None
        self.f_camp.set(self.data)
        self.f_lang.set(self.data)
        self.f_music.set(music_view(self.data))
        for fs in self.f_hists:
            fs.set(self.data)
        if self.sty_w is not None:
            self._sty_show(self.sty_cur)
        self.refresh_ret(0 if self.data["retrato"] else None)
        self.refresh_caps(0 if self.data["capitulo"] else None)
        self.saved = self.to_text() if saved else None
        self.update_title()

    def new(self, ask=True):
        if ask and not self.confirm_discard():
            return
        try:
            with open(PLANTILLA, encoding="utf-8") as f:
                data, raw = parse_text(f.read())
        except OSError:   # Kit sin campanas/plantilla_historia.toml: historia vacia en vez de no abrir
            data, raw = parse_text("")
        self.load(data, raw, None, False)
        self.log(T("Nueva historia desde la plantilla.", "New story from the template."))

    def open_path(self, p):
        if not os.path.isfile(p):                  # no existe: se abre la plantilla, sin ventana de error
            self.new(ask=False)
            self.log(T("No existe %s: se abre la plantilla.", "%s not found: opening the template.") % p)
            return True
        try:
            with open(p, encoding="utf-8") as f:
                data, raw = parse_text(f.read())
        except (OSError, ValueError) as e:
            messagebox.showerror(T("No se pudo abrir", "Could not open"), str(e), parent=self.root)
            return False
        self.load(data, raw, p, True)
        self.log(T("Abierto: %s", "Opened: %s") % p)
        return True

    def open_dialog(self):
        if not self.confirm_discard():
            return
        p = filedialog.askopenfilename(parent=self.root, title=T("Abrir historia", "Open story"), initialdir=CAMPANAS,
                                       filetypes=[(T("Historias TOML", "Story TOML"), "*.toml"),
                                                  (T("Todos", "All"), "*.*")])
        if p:
            self.open_path(p)

    def save(self):
        return self._save(False)

    def save_as(self):
        return self._save(True)

    def _save(self, as_new=False):
        self.flush_all()
        p = self.path
        if as_new or not p:
            p = filedialog.asksaveasfilename(
                parent=self.root, title=T("Guardar historia", "Save story"), initialdir=CAMPANAS,
                initialfile=os.path.basename(p) if p else "mi_historia.toml",
                defaultextension=".toml", filetypes=[(T("Historias TOML", "Story TOML"), "*.toml")])
            if not p:
                return False
        errs, warns = check(self.data, os.path.dirname(os.path.abspath(p)))
        if errs:
            self.log(T("No guardado. Hay que corregir:", "Not saved. Please fix:"))
            for e in errs:
                self.log("  - " + e)
            messagebox.showerror(T("Revisa la historia", "Check the story"),
                                 "\n".join("• " + e for e in errs), parent=self.root)
            return False
        if warns:
            for w in warns:
                self.log(T("Aviso: ", "Warning: ") + w)
            extra = "\n..." if len(warns) > 12 else ""
            if not messagebox.askyesno(T("Avisos", "Warnings"),
                                       "\n".join("• " + w for w in warns[:12]) + extra + "\n\n" +
                                       T("¿Guardar igualmente?", "Save anyway?"), parent=self.root):
                return False
        text = build_text(self.data, self.raw)
        try:
            if os.path.exists(p):
                backup(p)
            with open(p, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
        except OSError as e:
            messagebox.showerror(T("No se pudo guardar", "Could not save"), str(e), parent=self.root)
            return False
        self.path, self.saved = p, text
        self.update_title()
        self.log(T("Guardado: %s", "Saved: %s") % p)
        return True

    def pick_png(self, entry):
        base = os.path.dirname(self.path) if self.path else CAMPANAS
        p = filedialog.askopenfilename(parent=self.root, title=T("Imagen PNG", "PNG image"), initialdir=base,
                                       filetypes=[(T("Imagen PNG", "PNG image"), "*.png")])
        if p:
            try:
                rel = os.path.relpath(p, base)
            except ValueError:                  # otra unidad de disco: ruta absoluta
                rel = p
            entry.delete(0, "end")
            entry.insert(0, rel.replace("\\", "/"))
            self.refresh_ret_preview()

    # ----- retratos
    def flush_ret(self):
        rs, i = self.data.get("retrato", []), self.sel_ret
        if i is None or not 0 <= i < len(rs):
            return
        apply(rs[i], self.f_ret.get())
        self.lst_ret.delete(i)
        self.lst_ret.insert(i, ret_label(rs[i]))
        self.lst_ret.selection_set(i)

    def refresh_ret(self, sel):
        self.sel_ret = None
        self.lst_ret.delete(0, "end")
        for r in self.data.get("retrato", []):
            self.lst_ret.insert("end", ret_label(r))
        if sel is None or not self.data.get("retrato"):
            self.f_ret.set({})
            self._ret_show_place({})
        else:
            self.select_ret(sel)
        self.refresh_ret_preview()
        self.refresh_cap_thumbs()

    def select_ret(self, i):
        self.flush_ret()
        self.sel_ret = i
        self.lst_ret.selection_clear(0, "end")
        self.lst_ret.selection_set(i)
        self.lst_ret.see(i)
        self.f_ret.set(self.data["retrato"][i])
        self._ret_show_place(self.data["retrato"][i])
        self.refresh_ret_preview()
        self.refresh_cap_thumbs()

    def refresh_ret_preview(self):
        """Vista previa grande del retrato seleccionado (solo si tiene PNG propio)."""
        if not hasattr(self, "ret_big"):
            return
        png = self.f_ret.w["png"][1].get().strip()
        base = self._base()
        self.ret_big.show(png or None, base)
        if not png:
            msg = T("Sin imagen propia: se usa el retrato del juego.", "No own image: the game's portrait is used.")
        elif not os.path.isfile(_abs(png, base)):
            msg = T("No encuentro esa imagen.", "I can't find that image.")
        else:
            msg = ""
        self.ret_hint.configure(text=msg)
        if png and not self.ret_place.winfo_manager():          # colocacion: solo con PNG propio
            self.ret_place.pack(fill="x", pady=(10, 0))
        elif not png and self.ret_place.winfo_manager():
            self.ret_place.pack_forget()
        self.refresh_ret_scene()

    def _ret_place_get(self):
        """(escala, x, y) del retrato seleccionado, tal como están en los datos (por defecto si faltan)."""
        rs, i = self.data.get("retrato", []), self.sel_ret
        r = rs[i] if i is not None and 0 <= i < len(rs) else {}
        dx, dy = (r.get("desplazamiento") or [0, 0])[:2]
        return float(r.get("escala", 1.0)), int(dx), int(dy)

    def _ret_show_place(self, r):
        """Pone los deslizadores con los valores de un retrato (sin escribir nada en los datos)."""
        dx, dy = (r.get("desplazamiento") or [0, 0])[:2]
        self._ret_loading = True
        try:
            self.sl["escala"][0].set(float(r.get("escala", 1.0)))
            self.sl["x"][0].set(int(dx))
            self.sl["y"][0].set(int(dy))
        finally:
            self._ret_loading = False
        self._ret_place_labels()

    def _ret_place_labels(self):
        self.sl["escala"][1].configure(text="x %.2f" % float(self.sl["escala"][0].get()))
        self.sl["x"][1].configure(text="%+d px" % int(self.sl["x"][0].get()))
        self.sl["y"][1].configure(text="%+d px" % int(self.sl["y"][0].get()))

    def _ret_place_write(self, _v=None):
        """Los deslizadores escriben en el retrato seleccionado; los valores por defecto (1.0, [0, 0]) no se guardan."""
        if self._ret_loading or self.sel_ret is None:
            return
        r = self.data["retrato"][self.sel_ret]
        esc = round(float(self.sl["escala"][0].get()), 2)
        dx, dy = int(self.sl["x"][0].get()), int(self.sl["y"][0].get())
        _set_nested(r, ("escala",), None if esc == 1.0 else esc)
        _set_nested(r, ("desplazamiento",), None if (dx, dy) == (0, 0) else [dx, dy])
        self._ret_place_labels()
        self.refresh_ret_scene()

    def _ret_reset(self):
        self._ret_show_place({"escala": 1.0})   # deslizadores a su valor por defecto (sin escribir)
        self._ret_place_write()                 # y luego se escribe en el retrato (quita las claves)

    def refresh_ret_scene(self):
        """Vista previa aproximada del diálogo. El arte se monta igual que en campana.py."""
        if not hasattr(self, "ret_scene") or not self.ret_place.winfo_manager():
            return
        if Image is None:
            self.ret_scene.configure(image="", text=T("Hace falta Pillow para la vista previa.",
                                                      "Pillow is needed for the preview."))
            return
        png = self.f_ret.w["png"][1].get().strip()
        esc, dx, dy = self._ret_place_get()
        art = None
        full = _abs(png, self._base()) if png else ""
        if full and os.path.isfile(full):
            try:
                art = portrait_art(full, esc, dx, dy)
            except (OSError, ValueError):            # PNG roto: se ve el escenario sin arte
                art = None
        self.ret_scene_img = ImageTk.PhotoImage(scene_image(art, self._scene_line()), master=self.ret_scene)
        self.ret_scene.configure(image=self.ret_scene_img, text="")

    def _scene_line(self):
        """La primera frase de diálogo de un capítulo con este retrato (o la de muestra), dibujada como en el
        juego (fuente, tamaño y ancho del estilo «dialogo»). None sin numpy/Pillow."""
        rs, i = self.data.get("retrato", []), self.sel_ret
        ent = str(rs[i].get("entrada", "")) if i is not None and i < len(rs) else ""
        text = SAMPLE
        for c in self.data.get("capitulo", []):
            lines = c.get("dialogo_" + LANG) or c.get("dialogo_es") or c.get("dialogo_en")
            if ent and ent in [str(r) for r in c.get("retratos", [])] and lines and isinstance(lines[0], str):
                text = lines[0]
                break
        try:
            return style_preview("dialogo", style_effective(self.data, "dialogo"), self._base(), text) \
                if _campana() else None
        except (OSError, ValueError, KeyError):
            return None

    def _on_ret(self, _e=None):
        sel = self.lst_ret.curselection()
        if sel and sel[0] != self.sel_ret:
            self.select_ret(sel[0])

    def ret_add(self):
        self.flush_ret()
        rs = self.data.setdefault("retrato", [])
        rs.append({})
        self.refresh_ret(len(rs) - 1)

    def ret_del(self):
        i = self.sel_ret
        if i is None or not messagebox.askyesno(T("Quitar retrato", "Remove portrait"),
                                                T("¿Quitar este retrato de la lista?",
                                                  "Remove this portrait from the list?"), parent=self.root):
            return
        self.flush_ret()
        rs = self.data["retrato"]
        del rs[i]
        self.refresh_ret(min(i, len(rs) - 1) if rs else None)

    def _png_for(self, n):
        """PNG propio del retrato con ese número de entrada (vacío si no tiene)."""
        if not n:
            return None
        for r in self.data.get("retrato", []):
            if r.get("png") and str(r.get("entrada")) == n:
                return str(r["png"])
        return None

    def refresh_cap_thumbs(self):
        """Junto a «Retratos» del capítulo: la imagen PNG propia de cada retrato (si la tiene)."""
        self._img_sync()
        if not getattr(self, "cap_prev", None):
            return
        self.flush_ret()
        base = self._base()
        for p, e in zip(self.cap_prev, self.f_caps[1].w["retratos"][1]):
            n = e.get().strip()
            png = self._png_for(n)
            shown = bool(png) and p.show(png, base)
            (p.grid if shown else p.grid_remove)()

    # ----- capitulos
    def cap_get(self):
        vals = {}
        for fs in self.f_caps:
            vals.update(fs.get())
        return vals

    def flush_cap(self):
        caps, i = self.data.get("capitulo", []), self.sel_cap
        if i is None or not 0 <= i < len(caps):
            return
        apply(caps[i], self.cap_get())
        self.lst_cap.delete(i)
        self.lst_cap.insert(i, cap_label(i, caps[i]))
        self.lst_cap.selection_set(i)

    def refresh_caps(self, sel):
        self.sel_cap = None
        self.lst_cap.delete(0, "end")
        caps = self.data.get("capitulo", [])
        for i, c in enumerate(caps):
            self.lst_cap.insert("end", cap_label(i, c))
        if sel is None or not caps:
            for fs in self.f_caps:
                fs.set({})
            self.refresh_cap_thumbs()
            return
        self.select_cap(sel)

    def select_cap(self, i):
        self.flush_cap()
        self.sel_cap = i
        self.lst_cap.selection_clear(0, "end")
        self.lst_cap.selection_set(i)
        self.lst_cap.see(i)
        for fs in self.f_caps:
            fs.set(self.data["capitulo"][i])
        self.refresh_cap_thumbs()

    def _on_cap(self, _e=None):
        sel = self.lst_cap.curselection()
        if sel and sel[0] != self.sel_cap:
            self.select_cap(sel[0])

    def cap_add(self):
        self.flush_cap()
        caps = self.data.setdefault("capitulo", [])
        at = len(caps) if self.sel_cap is None else self.sel_cap + 1
        caps.insert(at, copy.deepcopy(NUEVO_CAP))
        self.refresh_caps(at)
        self.log(T("Capítulo nuevo: escribe su número de combate, o déjalo vacío si es el final.",
                   "New chapter: type its fight number, or leave it empty if it is the ending."))

    def cap_dup(self):
        i = self.sel_cap
        if i is None:
            return
        self.flush_cap()
        caps = self.data["capitulo"]
        caps.insert(i + 1, copy.deepcopy(caps[i]))
        self.refresh_caps(i + 1)

    def cap_del(self):
        i = self.sel_cap
        if i is None or not messagebox.askyesno(
                T("Quitar capítulo", "Remove chapter"),
                T("¿Quitar este capítulo? Se quita del documento; la versión guardada anterior queda en respaldo "
                  "al guardar.", "Remove this chapter? It is removed from the document; the previous saved version "
                  "is kept as a backup when you save."), parent=self.root):
            return
        self.flush_cap()
        caps = self.data["capitulo"]
        del caps[i]
        self.refresh_caps(min(i, len(caps) - 1) if caps else None)

    def cap_move(self, d):
        i = self.sel_cap
        if i is None:
            return
        self.flush_cap()
        caps = self.data["capitulo"]
        j = i + d
        if 0 <= j < len(caps):
            caps[i], caps[j] = caps[j], caps[i]
            self.refresh_caps(j)

    # ----- imagenes de la saga (solo capitulos con saga = true)
    def _img_sync(self):
        """Las imágenes de la saga solo se ven en los capítulos que abren saga; después, sus vistas previas."""
        if not hasattr(self, "card_img"):
            return
        on = bool(self.f_caps[3].w["saga"][1].get())
        shown = bool(self.card_img.winfo_manager())
        if on and not shown:
            self.card_img.pack(fill="x", pady=(0, 12), padx=(0, 4))
        elif shown and not on:
            self.card_img.pack_forget()
        self.refresh_img_prev()

    def refresh_img_prev(self):
        if not getattr(self, "img_prev", None):
            return
        base = self._base()
        for key, p in zip(("imagen", "cierre_imagen"), self.img_prev):
            path = self.f_img.w[key][1].get().strip()
            shown = bool(path) and p.show(path, base)
            (p.grid if shown else p.grid_remove)()

    def pick_img(self, entry):
        p = filedialog.askopenfilename(
            parent=self.root, title=T("Imagen de la saga", "Saga image"), initialdir=self._base(),
            filetypes=[(T("Imágenes", "Images"), "*.png *.webp *.jpg *.jpeg *.avif"), (T("Todos", "All"), "*.*")])
        if p:
            entry.delete(0, "end")
            entry.insert(0, rel_ref(p, self._base()))
            self.refresh_img_prev()

    # ----- traducciones que faltan
    def fill_translations(self):
        self.flush_all()
        new = fill_missing(self.data)
        self.refresh_caps(self.sel_cap)                     # refresca el formulario con los textos copiados
        total = len(self.data.get("historia", {}).get("traducir_pendiente", []))
        if new:
            self.log(T("Traducciones: %d textos copiados del otro idioma y marcados como pendientes (%d en total). "
                       "Sin internet: tradúcelos tú.",
                       "Translations: %d texts copied from the other language and marked as pending (%d in total). "
                       "No internet: translate them yourself.") % (len(new), total))
        else:
            self.log(T("No falta ninguna traducción: todos los textos tienen sus dos idiomas. Pendientes: %d.",
                       "No translation is missing: every text has both languages. Pending: %d.") % total)

    # ----- estilos de texto ([historia.estilos.<nombre>])
    def _tab_styles(self):
        t = self._tab(T("5. Estilos de texto", "5. Text styles"),
                      T("Cómo se ven los textos que dibuja el juego. Son tres estilos separados: «Diálogo» (charlas "
                        "con retratos), «Narración» (paneles de la introducción y el cierre de cada saga) y «Final» "
                        "(créditos y final de la historia). Cambiar uno no cambia los demás. Solo se guardan los "
                        "valores que cambias.",
                        "How the texts drawn by the game look. There are three separate styles: 'Dialogue' (talks "
                        "with portraits), 'Narration' (panels of each saga's intro and ending) and 'Ending' (credits "
                        "and the story's ending). Changing one does not change the others. Only the values you "
                        "change are saved."))
        if _campana() is None:
            ttk.Label(t, style="Ed.Hint.TLabel", text=T("Hace falta numpy y Pillow para la vista previa de los estilos.",
                                                        "numpy and Pillow are needed for the style preview.")).pack(anchor="w")
            return
        self.sty_w, self.sty_col = {}, {}
        left = card(t, T("Estilo", "Style"), pad=12)
        left.pack(side="left", fill="y", padx=(0, 12), anchor="n")
        self.sty_pick = ttk.Combobox(left, state="readonly", width=38,
                                     values=[T(es, en) for _n, es, en in STYLE_NAMES])
        self.sty_pick.pack(anchor="w")
        self.sty_pick.bind("<<ComboboxSelected>>", self._sty_pick)
        box = card(t, T("Ajustes", "Settings"), pad=12)
        box.pack(side="left", fill="both", expand=True, anchor="n", padx=(0, 12))
        for r, (key, kind, lab, tip, rng) in enumerate(STYLE_FIELDS):
            ttk.Label(box, text=T(*lab), style="Ed.Card.TLabel").grid(row=r, column=0, sticky="w", padx=(0, 16), pady=5)
            fr = ttk.Frame(box, style="Ed.Card.TFrame")
            fr.grid(row=r, column=1, sticky="w", pady=5)
            if kind == "color":
                sw = tk.Label(fr, width=4, bd=1, relief="solid", bg=PAL["frame"])
                sw.pack(side="left")
                hx = ttk.Label(fr, style="Ed.Card.Hint.TLabel", width=20)
                hx.pack(side="left", padx=8)
                ttk.Button(fr, text=T("Elegir color...", "Choose color..."), style="Small.TButton",
                           command=lambda k=key: self._sty_color(k)).pack(side="left")
                self.sty_w[key] = (sw, hx)
                Tip(sw, T(*tip))
                continue
            if kind == "text":
                w = ttk.Entry(fr, width=30)
                w.pack(side="left")
                ttk.Button(fr, text=T("Elegir...", "Choose..."), style="Small.TButton",
                           command=self._sty_font_browse).pack(side="left", padx=8)
            else:
                w = ttk.Spinbox(fr, from_=rng[0], to=rng[1], width=8, justify="center", command=self._sty_write)
                w.pack(side="left")
            for seq in ("<KeyRelease>", "<FocusOut>", "<Return>"):
                w.bind(seq, self._sty_write, add="+")
            Tip(w, T(*tip))
            self.sty_w[key] = w
        n = len(STYLE_FIELDS)
        ttk.Button(box, text=T("Restablecer este estilo", "Reset this style"), style="Small.TButton",
                   command=self._sty_reset).grid(row=n, column=0, columnspan=2, sticky="w", pady=(10, 0))
        self.sty_msg = ttk.Label(box, style="Ed.Card.Hint.TLabel", justify="left", wraplength=460)
        self.sty_msg.grid(row=n + 1, column=0, columnspan=2, sticky="w", pady=(8, 0))
        right = card(t, T("Vista previa", "Preview"), pad=12)
        right.pack(side="left", fill="y", anchor="n")
        self.sty_img = tk.Label(right, bd=0, bg=PAL["card"], fg=PAL["dim"], font=FONT["small"])
        self.sty_img.pack(pady=(4, 8))
        ttk.Label(right, style="Ed.Card.Hint.TLabel", justify="center", wraplength=300,
                  text=T("Así se ve el texto sobre un fondo gris medio.",
                         "How the text looks over a mid-grey background.")).pack()
        self.sty_pick.current(0)
        self._sty_show("dialogo")

    def _sty_pick(self, _e=None):
        name = STYLE_NAMES[self.sty_pick.current()][0]
        if name != self.sty_cur:
            self._sty_write()                              # guarda lo del estilo anterior
            self._sty_show(name)

    def _sty_show(self, name):
        """Pone en el formulario los valores del estilo (los guardados o los de campana.py)."""
        self.sty_cur = name
        eff = style_effective(self.data, name)
        for key, kind, _l, _t, _r in STYLE_FIELDS:
            if kind == "color":
                self.sty_col[key] = list(eff[key])
                self._sty_paint(key)
            else:
                w = self.sty_w[key]
                w.delete(0, "end")
                w.insert(0, str(eff[key]))
        self.sty_msg.configure(text="")
        self._sty_render(name, eff)

    def _sty_paint(self, key):
        sw, hx = self.sty_w[key]
        rgb = self.sty_col[key]
        hexv = "#%02x%02x%02x" % tuple(rgb)
        sw.configure(bg=hexv)
        hx.configure(text="%s  (%d, %d, %d)" % (hexv, rgb[0], rgb[1], rgb[2]))

    def _sty_read(self):
        """Valores del formulario y avisos. Lo que no es válido se queda como estaba."""
        eff = style_effective(self.data, self.sty_cur)
        vals, errs = {}, []
        for key, kind, lab, _tip, rng in STYLE_FIELDS:
            name = T(*lab)
            if kind == "color":
                vals[key] = list(self.sty_col[key])
            elif kind == "text":
                v = self.sty_w[key].get().strip()
                if not v:
                    errs.append(T("%s: escribe una fuente.", "%s: type a font.") % name)
                    v = eff[key]
                elif not os.path.isfile(_abs(v, self._base())) and not os.path.isfile(os.path.join(WIN_FONTS, v)):
                    errs.append(T("%s: no encuentro «%s» (se usa la de Windows con ese nombre, si existe).",
                                  "%s: can't find '%s' (the Windows font with that name is used, if it exists).")
                                % (name, v))
                vals[key] = v
            else:
                s = self.sty_w[key].get().strip()
                if re.fullmatch(r"\d+", s) and rng[0] <= int(s) <= rng[1]:
                    vals[key] = int(s)
                else:
                    errs.append(T("%s: pon un número de %d a %d.", "%s: type a number from %d to %d.")
                                % (name, rng[0], rng[1]))
                    vals[key] = eff[key]
        return vals, errs

    def _sty_write(self, _e=None):
        """Lleva el formulario a la historia (solo lo que difiere del defecto) y refresca la vista previa."""
        if self.sty_w is None:
            return
        vals, errs = self._sty_read()
        put_style(self.data, self.sty_cur, style_diff(vals, style_default(self.sty_cur)))
        self.sty_msg.configure(text="\n".join("• " + e for e in errs))
        self._sty_render(self.sty_cur, vals)

    def _sty_render(self, name, vals):
        try:
            arr = style_preview(name, vals, self._base())
        except (OSError, ValueError, TypeError, KeyError, IndexError) as e:   # p. ej. fuente que no se abre
            self.sty_img.configure(image="", text=T("No puedo dibujar con esta fuente:\n%s",
                                                    "I can't draw with this font:\n%s") % e)
            return
        im = Image.fromarray(arr).convert("RGBA")
        bg = Image.new("RGBA", (im.width + 40, im.height + 30), (128, 128, 128, 255))
        bg.alpha_composite(im, (20, 15))
        self.sty_photo = ImageTk.PhotoImage(bg.convert("RGB"), master=self.sty_img)
        self.sty_img.configure(image=self.sty_photo, text="")

    def _sty_color(self, key):
        lab = next(s[2] for s in STYLE_FIELDS if s[0] == key)
        rgb, _hx = colorchooser.askcolor(color="#%02x%02x%02x" % tuple(self.sty_col[key]), parent=self.root,
                                         title=T(*lab))
        if rgb:
            self.sty_col[key] = [int(round(v)) for v in rgb]
            self._sty_paint(key)
            self._sty_write()

    def _sty_font_browse(self):
        p = filedialog.askopenfilename(parent=self.root, title=T("Fuente", "Font"), initialdir=self._base(),
                                       filetypes=[(T("Fuentes TrueType", "TrueType fonts"), "*.ttf"),
                                                  (T("Todos", "All"), "*.*")])
        if p:
            w = self.sty_w["fuente"]
            w.delete(0, "end")
            w.insert(0, font_ref(p, self._base()))
            self._sty_write()

    def _sty_reset(self):
        put_style(self.data, self.sty_cur, {})
        self._sty_show(self.sty_cur)

    # ----- montar y probar
    def montar(self):
        if self.running or not self._save():
            return
        self.log(T("Montando el mod…", "Building the mod…"))
        self.run_cmd([sys.executable, "campana.py"] + MONTAR_ARGS + ["montar", self.path], T("Montar mod", "Build mod"))

    def probar(self):
        self.run_cmd([sys.executable, "campana.py", "prueba"], T("Probar", "Test"))

    def run_cmd(self, cmd, title=""):
        if self.runner:                     # dentro del Mod Kit: su registro y su barra de tareas
            self.runner(cmd, title)
            return
        if self.running:
            messagebox.showinfo(T("Ocupado", "Busy"), T("Ya hay un proceso en marcha.", "A job is already running."),
                                parent=self.root)
            return
        self.running = True
        self.btn_montar.state(["disabled"])
        self.btn_probar.state(["disabled"])
        self.log("> python " + " ".join(cmd[1:]))
        threading.Thread(target=self._worker, args=(cmd,), daemon=True).start()
        self.root.after(80, self._poll)

    def _worker(self, cmd):
        env = dict(os.environ, PYTHONIOENCODING="utf-8")
        try:
            p = subprocess.Popen(cmd, cwd=HERE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            for raw in p.stdout:
                self.q.put(raw.decode("utf-8", "replace").rstrip("\r\n"))
            self.q.put(("fin", p.wait()))
        except OSError as e:
            self.q.put(T("No se pudo ejecutar: %s", "Could not run: %s") % e)
            self.q.put(("fin", -1))

    def _poll(self):
        try:
            while True:
                item = self.q.get_nowait()
                if isinstance(item, tuple):
                    self.running = False
                    self.btn_montar.state(["!disabled"])
                    self.btn_probar.state(["!disabled"])
                    if item[1] == 0:
                        self.log(T("Terminado: correcto.", "Done: success."))
                    else:
                        self.log(T("Terminado con error (código %s).", "Done with an error (code %s).") % item[1])
                    return
                self.log(item)
        except queue.Empty:
            pass
        self.root.after(80, self._poll)

    def on_close(self):
        if self.running and not messagebox.askyesno(T("Ocupado", "Busy"),
                                                    T("Un proceso sigue en marcha. ¿Cerrar?",
                                                      "A job is still running. Close?"), parent=self.root):
            return
        if self.confirm_discard():
            for p in [self.ret_big] + self.cap_prev:       # soltar las imágenes antes de cerrar Tk
                p.show(None, CAMPANAS)
            self.root.destroy()


# ---------------------------------------------------------------- autocomprobacion
def selftest_window():
    """Ventana oculta: el editor dentro de un marco (como en el Mod Kit) y el independiente, en ES y EN."""
    global LANG
    try:
        root = tk.Tk()
    except tk.TclError as ex:                       # sin pantalla: se omite (lo demas ya se ha probado)
        print("editor_historias.py: ventana omitida (%s)" % ex)
        return
    root.withdraw()
    try:
        for lg in ("es", "en"):
            LANG = lg
            host = tk.Frame(root)
            host.pack()
            app = App(host, FREEZER, embedded=True)
            root.update_idletasks()
            n = len(app.data["capitulo"])
            assert app.nb.index("end") == 5 and app.lst_cap.size() == n, "pestañas o lista de capítulos"
            assert app.lst_cap.get(0).startswith("1 · "), app.lst_cap.get(0)
            assert app.lst_cap.get(n - 1) == "%d · %s" % (n, T("Final", "Ending")), app.lst_cap.get(n - 1)
            if not os.path.isfile(os.path.join(CAMPANAS, "retratos", "mecha_freezer.png")):
                host.destroy()   # el Kit no lleva el retrato aportado: el resto se prueba en el proyecto
                continue
            app.select_cap(10)                      # cap. 11: su retrato 4501 tiene PNG propio (Freezer)
            if Image is not None:
                assert app.cap_prev[1].winfo_manager() == "grid" and app.cap_prev[0].winfo_manager() == "", \
                    "vista previa del retrato del capítulo"
            app.select_ret(2)                       # retrato 4501 (con PNG): sin aviso de «no encuentro»
            assert app.ret_hint.cget("text") == "", repr(app.ret_hint.cget("text"))
            r4501 = app.data["retrato"][2]          # sus valores del archivo (el usuario los cambia)
            assert app.ret_place.winfo_manager() == "pack", "colocación del PNG oculta"
            assert abs(app.sl["escala"][0].get() - float(r4501.get("escala", 1.0))) < 1e-9 and \
                app.sl["y"][0].get() == r4501.get("desplazamiento", [0, 0])[1], "deslizadores"
            if Image is not None:
                assert app.ret_scene.cget("image") != "", "vista previa del diálogo vacía"
            for name, v in (("escala", 1.25), ("x", -30), ("y", 45)):   # un arrastre: mueve y escribe
                app.sl[name][0].set(v)
                app._ret_place_write()
            assert r4501["escala"] == 1.25 and r4501["desplazamiento"] == [-30, 45], r4501
            back = parse_text(app.to_text())[0]["retrato"][2]     # ida y vuelta por el TOML
            assert back["escala"] == 1.25 and back["desplazamiento"] == [-30, 45], back
            app._ret_reset()                        # valores por defecto: se quitan las claves
            assert "escala" not in r4501 and "desplazamiento" not in r4501, r4501
            assert app.sl["escala"][0].get() == 1.0 and app.sl["y"][0].get() == 0, "reset"
            app.select_ret(0)                       # retrato 1464 (sin PNG): sin colocación
            assert app.ret_place.winfo_manager() == "", "la colocación debe ocultarse sin PNG"
            # imagenes de la saga: solo en capitulos con saga; vista previa con recorte 660x420
            sg = [i for i, c in enumerate(app.data["capitulo"]) if c.get("saga")]
            ns = [i for i, c in enumerate(app.data["capitulo"]) if not c.get("saga")]
            app.select_cap(sg[0])
            assert app.card_img.winfo_manager() == "pack", "imagenes de la saga ocultas en un capitulo con saga"
            app.f_img.w["imagen"][1].delete(0, "end")
            app.f_img.w["imagen"][1].insert(0, "retratos/mecha_freezer.png")
            app.refresh_img_prev()
            if Image is not None:
                assert app.img_prev[0].winfo_manager() == "grid", "vista previa de la imagen de la saga"
            app.select_cap(ns[0])
            assert app.card_img.winfo_manager() == "", "imagenes de la saga visibles en un capitulo sin saga"
            # estilos: elegir narracion, cambiar un valor (se guarda) y restablecer (se quita)
            app.sty_pick.current(1)
            app._sty_pick()
            assert app.sty_cur == "narracion", app.sty_cur
            if Image is not None:
                assert app.sty_img.cget("image") != "", "vista previa del estilo vacia"
            app.sty_w["tamano"].delete(0, "end")
            app.sty_w["tamano"].insert(0, "21")
            app._sty_write()
            assert app.data["historia"]["estilos"]["narracion"]["tamano"] == 21, app.data["historia"].get("estilos")
            app._sty_reset()
            assert "estilos" not in app.data["historia"], app.data["historia"]
            app.fill_translations()                       # solo escribe en el registro; no guarda nada
            # musica: la casilla de la historia muestra «2.4205»; un capitulo sin musica propia dice «igual»
            assert "2.4205" in app.f_music.w[("historia", "musica_dialogo")][1].get(), app.f_music.w[
                ("historia", "musica_dialogo")][1].get()
            fm = next(fs for fs in app.f_caps if "musica_dialogo" in fs.w)
            app.select_cap(0)
            assert fm.w["musica_dialogo"][1].get() == T("Igual que en la historia", "Same as in the story")
            host.destroy()
        LANG = "es"
        if char_name(11) == "Piccolo":               # titulo con rival (roster): «3 · vs Piccolo (combate 656)»
            assert cap_label(2, {"combate": 656, "rival": 11}) == "3 · vs Piccolo (combate 656)"
        assert cap_label(12, {"es": ["x"]}) == "13 · Final", cap_label(12, {"es": ["x"]})
        assert thumb_image(None, CAMPANAS, 50, PAL["card"], root) is None
        app = App(root, FREEZER)                     # independiente: aplica el estilo del Mod Kit
        root.update_idletasks()
        assert app.nb.index("end") == 5 and app.root.title().startswith("Editor"), "ventana independiente"
        missing = App(root, os.path.join(CAMPANAS, "no_existe_xyz.toml"), embedded=True)   # sin aviso emergente
        assert missing.data.get("capitulo") and missing.path is None, "archivo inexistente"
    finally:
        LANG = "es"
        root.destroy()
    print("editor_historias.py: ventana OK (dentro del Mod Kit y independiente, ES y EN)")


def _selftest_features():
    """Idioma, traducciones pendientes, estilos de texto e imágenes de la saga: se escribe una copia de
    la historia de Freezer en una carpeta temporal, se relee y debe salir igual. No toca campanas/."""
    cp = _campana()
    assert cp is not None, "hace falta numpy y Pillow (campana.py)"
    with open(FREEZER, encoding="utf-8") as f:
        base, _raw = parse_text(f.read())
    tmpd = tempfile.mkdtemp()
    made = []
    try:
        png = os.path.join(tmpd, "pan.png")
        Image.new("RGB", (800, 600), (200, 80, 40)).save(png)
        made.append(png)
        mini = copy.deepcopy(base)
        mini["retrato"] = []                             # las rutas de retratos no son de esta prueba
        mini["capitulo"] = [
            {"combate": 654, "retratos": [1436, 1463], "dialogo_es": ["Hola."],
             "victoria_es": ["Bien."], "victoria_en": ["Good."]},
            {"combate": 655, "retratos": [1436, 1463], "saga": True, "salida": [5700, 100, 1700], "planeta": 1,
             "imagen": "pan.png", "cierre_imagen": "pan.png", "es": ["Intro."], "dialogo_en": ["Talk."],
             "dialogo_es": ["Charla."], "victoria_en": ["Win."], "victoria_es": ["Gana."], "cierre_es": ["Cierre."]},
            {"es": ["Fin."]},
        ]
        mini["historia"]["idioma"] = "en"
        # 1. traducciones: faltan textos en ingles; al rellenar, se copian y quedan como pendientes
        errs, _ = check(mini, tmpd)
        assert any("inglés" in e for e in errs), errs
        new = fill_missing(mini)
        assert sorted(new) == sorted(["capitulo 1 dialogo_en", "capitulo 2 en", "capitulo 2 cierre_en",
                                      "capitulo 3 en"]), new
        assert mini["capitulo"][0]["dialogo_en"] == ["Hola."] and mini["capitulo"][2]["en"] == ["Fin."]
        assert len(mini["historia"]["traducir_pendiente"]) == 4
        errs, _ = check(mini, tmpd)
        assert not errs, errs
        # 2. al traducir uno, sale de los pendientes
        mini["capitulo"][0]["dialogo_en"] = ["Hi."]
        prune_pending(mini)
        assert len(mini["historia"]["traducir_pendiente"]) == 3, mini["historia"]
        # 3. estilos: solo lo que difiere del defecto; el estilo sin cambios no aparece
        d = style_default("dialogo")
        stored = style_diff({**d, "tamano": 20, "color": [1, 2, 3]}, d)
        assert stored == {"tamano": 20, "color": [1, 2, 3]}, stored
        put_style(mini, "dialogo", stored)
        put_style(mini, "final", style_diff(style_default("final"), style_default("final")))
        eff = style_effective(mini, "dialogo")
        assert eff["tamano"] == 20 and eff["fuente"] == d["fuente"], eff
        before = copy.deepcopy(cp.STYLES)
        arr = style_preview("dialogo", eff, tmpd)        # vista previa: no deja cambios en campana.STYLES
        assert arr.shape[2] == 4 and arr[..., 3].max() > 0, arr.shape
        assert cp.STYLES == before, "la vista previa cambio campana.STYLES"
        # 4. imagenes: una que no existe da aviso (se usa el panel del juego); las que existen, nada
        bad = copy.deepcopy(mini)
        bad["capitulo"][1]["cierre_imagen"] = "no_existe.png"
        bad["historia"]["idioma"] = "fr"
        errs, warns = check(bad, tmpd)
        assert any("no_existe.png" in w for w in warns) and any("Idioma de los textos" in e for e in errs), (errs, warns)
        thumb = cp.png2pic.fit(Image.open(png).convert("RGBA"), (660, 420))
        assert thumb.shape[:2] == (420, 660), thumb.shape
        # 5. ida y vuelta por el TOML de la copia: mismos datos al releer
        text = build_text(mini)
        path = os.path.join(tmpd, "copia.toml")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        made.append(path)
        with open(path, "rb") as f:
            back = tomllib.load(f)
        assert back == mini, "al guardar y releer cambian los datos"
        assert "[historia.estilos.dialogo]" in text and "estilos.narracion" not in text and "estilos.final" not in text
        assert parse_text(text)[0] == mini, "la segunda lectura no coincide"
        # 6. «cada idioma el suyo» (None) quita la clave
        apply(mini, {("historia", "idioma"): None})
        assert "idioma" not in tomllib.loads(build_text(mini))["historia"]
        # 7. musica: «2.4205» <-> 135277; solo se escriben las claves que difieren del juego
        assert music_show(MUS_TALK) == "2.4205" and music_show(MUS_NARR) == "2.4197", music_show(MUS_TALK)
        assert music_num("2.4205") == MUS_TALK and music_num("135277") == MUS_TALK, music_num("2.4205")
        assert music_num("2.99999") == "2.99999" and not _music_ok(-1)
        mus = copy.deepcopy(mini)
        mus["historia"]["musica_narracion"] = MUS_NARR          # la del juego: no se escribe
        mus["historia"]["musica_dialogo"] = MUS_KEEP            # mantener la musica actual: si
        mus["capitulo"][0]["musica_dialogo"] = MUS_TALK         # distinta de la historia: si
        mus["capitulo"][1]["musica_dialogo"] = MUS_KEEP         # igual que la historia: no
        mus["capitulo"][2]["musica_dialogo"] = 135278           # 2.4206: si
        music_clean(mus)
        back_m = tomllib.loads(build_text(mus))
        assert "musica_narracion" not in back_m["historia"], back_m["historia"]
        assert back_m["historia"]["musica_dialogo"] == MUS_KEEP, back_m["historia"]
        assert back_m["capitulo"][0]["musica_dialogo"] == MUS_TALK and "musica_dialogo" not in back_m["capitulo"][1]
        assert back_m["capitulo"][2]["musica_dialogo"] == 135278
        assert parse_text(build_text(mus))[0] == mus, "la musica no vuelve igual"
        assert music_view({"historia": {}})["historia"] == {"musica_narracion": MUS_NARR, "musica_dialogo": MUS_TALK}
        errs, _ = check(mus, tmpd)
        assert not errs, errs
        mus["historia"]["musica_narracion"] = "2.99999"         # valores no validos: dos avisos de musica
        mus["capitulo"][2]["musica_dialogo"] = "2.4206"
        errs, _ = check(mus, tmpd)
        assert sum("música" in e.lower() for e in errs) == 2, errs
    finally:
        for p in made:
            if os.path.exists(p):
                os.remove(p)
        os.rmdir(tmpd)
    print("editor_historias.py: idioma, traducciones pendientes, estilos, imágenes y música OK (copia en carpeta temporal)")


def selftest():
    global LANG
    LANG = "es"                                         # las comprobaciones de texto van en español
    with open(FREEZER, encoding="utf-8") as f:
        orig = f.read()
    data, raw = parse_text(orig)
    assert data.get("capitulo") and len(raw) == 2, "no separa [[escena]] y [[panel_limpio]]"
    text = build_text(data, raw)
    fd, tmp = tempfile.mkstemp(suffix=".toml")
    os.close(fd)
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(text)
        with open(tmp, "rb") as f:
            back = tomllib.load(f)
    finally:
        os.remove(tmp)
    assert back == tomllib.loads(orig), "al guardar y releer cambian los datos"
    d2, r2 = parse_text(text)
    assert d2 == data and r2 == raw, "la segunda lectura no coincide"
    with open(PLANTILLA, "rb") as f:
        plant = tomllib.load(f)
    assert len(plant["capitulo"]) == 3 and "historia" in plant, "la plantilla no tiene la forma esperada"
    errs, _ = check(plant, CAMPANAS)
    assert not errs, errs
    errs, _ = check(data, CAMPANAS)
    assert not errs, errs
    bad = {"campana": {"mod": "mi mod"}, "historia": {}, "retrato": [{"entrada": 1, "png": "no_existe.png"}],
           "capitulo": [{"combate": "x", "retratos": [1]}, {"combate": 5, "retratos": [1, 2]}]}
    errs, bad_warns = check(bad, CAMPANAS)
    for frag in ("Carpeta del mod", "número de combate", "retratos necesita",
                 "último capítulo", "Avanzado: falta"):
        assert any(frag in e for e in errs), (frag, errs)
    assert any("no encuentro la imagen" in w for w in bad_warns), bad_warns   # falta: retrato del juego
    long = {"campana": {"mod": "x"}, "historia": {},
            "capitulo": [{"combate": 1, "retratos": [1, 2], "dialogo_es": ["a" * 61], "dialogo_en": ["a"],
                          "victoria_es": ["b"], "victoria_en": ["b"]}, {"es": ["x"], "en": ["y"]}]}
    _, warns = check(long, CAMPANAS)
    assert any("61 caracteres" in w for w in warns), warns
    # ingles: mismos avisos, solo en ingles
    LANG = "en"
    errs_en, bad_warns_en = check(bad, CAMPANAS)
    for frag in ("Mod folder", "fight number", "2 numbers", "last chapter", "Advanced: missing"):
        assert any(frag in e for e in errs_en), (frag, errs_en)
    assert any("can't find the image" in w for w in bad_warns_en), bad_warns_en
    assert not check(data, CAMPANAS)[0], "en ingles la historia de Freezer da errores"
    _, warns_en = check(long, CAMPANAS)
    assert any("61 characters" in w for w in warns_en), warns_en
    assert cap_label(0, {"combate": 654}) == "1 · Fight 654" and cap_label(12, {}) == "13 · Ending"
    LANG = "es"
    # cada texto de la interfaz existe en los dos idiomas
    for spec in CAMP_SPECS + HIST_SPECS + RET_SPECS + CAP_SPECS + IDIOMA_SPECS + IMG_SPECS + STYLE_FIELDS + MUSIC_SPECS:
        assert spec[2][0] and spec[2][1] and spec[3][0] and spec[3][1], spec[0]
    for lst in (PLANETAS, PLANTILLAS, NIVELES, CAPSULAS, MUSIC_PRESETS, MUSIC_CHAP):
        for x in lst:
            assert x[0] and x[1], x
    for m in MARCAS:
        assert m[0] and m[1] and len(m[2]) == 6, m
    # escala y desplazamiento de los retratos: ida y vuelta por el TOML; los valores por defecto no se escriben
    pl = copy.deepcopy(data)
    pl["retrato"] = [{"entrada": 4501, "png": "x.png", "escala": 1.25, "desplazamiento": [-30, 45]},
                     {"entrada": 1464, "png": "y.png"}]
    d3, r3 = parse_text(build_text(pl, raw))
    assert d3["retrato"][0]["escala"] == 1.25 and d3["retrato"][0]["desplazamiento"] == [-30, 45], d3["retrato"]
    assert "escala" not in d3["retrato"][1] and "desplazamiento" not in d3["retrato"][1], d3["retrato"]
    assert d3 == pl and r3 == raw, "escala/desplazamiento no vuelven igual"
    if Image is not None:                                # la vista previa del diálogo tiene su tamaño
        assert scene_image(None).size == (SCENE_W, SCENE_H)
        if _campana():   # la frase real (campana.render) encajada en el globo
            assert scene_image(None, style_preview("dialogo", style_default("dialogo"), HERE, "¡Hola!")).size == (SCENE_W, SCENE_H)
        mecha = os.path.join(CAMPANAS, "retratos", "mecha_freezer.png")   # aportado: no va en el Kit
        assert not os.path.isfile(mecha) or portrait_art(mecha, 1.0, 0, 0).size == (541, 530)
    assert cap_label(0, {"combate": 654}) == "1 · Combate 654"
    print("editor_historias.py: autocomprobacion OK (ida y vuelta de historia_freezer.toml, plantilla, "
          "validaciones en español e inglés, títulos de capítulos)")
    _selftest_features()
    selftest_window()
    return 0


def main(argv=None):
    global LANG
    ap = argparse.ArgumentParser(description="Editor de historias / Story editor")
    ap.add_argument("archivo", nargs="?", help="historia .toml a abrir (story .toml to open)")
    ap.add_argument("--lang", choices=("es", "en"), help="idioma: es o en (por defecto, el del Mod Kit)")
    ap.add_argument("--selftest", action="store_true", help="autocomprobacion sin ventana (self-check)")
    ap.add_argument("--mods", help="carpeta mods/ donde se monta (por defecto la del juego); se pasa a campana.py")
    ap.add_argument("--us", help="carpeta us/ del juego; se pasa a campana.py")
    a = ap.parse_args(sys.argv[1:] if argv is None else argv)
    MONTAR_ARGS[:] = [x for k in ("mods", "us") if getattr(a, k) for x in ("--" + k, os.path.abspath(getattr(a, k)))]
    if a.selftest:
        return selftest()
    LANG = a.lang or settings_lang() or "es"
    root = tk.Tk()
    App(root, os.path.abspath(a.archivo) if a.archivo else None)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
