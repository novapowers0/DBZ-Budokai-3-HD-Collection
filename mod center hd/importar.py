#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""importar.py - Asistente de importacion de personajes (lo usa el launcher).

Lee los juegos y colecciones que ya estan en disco (sin extraer nada) y crea el mod fuente
de un personaje nuevo (mods/<carpeta>/personaje.toml + modelos + moveset) con
roster_build.new_char; el montaje (_roster) se hace como siempre al pulsar JUGAR.

Fuentes:
  b1         Budokai 1 (ISO PS2): modelo + moveset + combos + gritos propios (b1port.py)
  b2         Budokai 2 (ISO PS2): modelos (#AMB [AMO, AMT]); moveset del donante
  b3         Budokai 3 / mods de la comunidad: .amb y parejas .amo/.amt en "modding resources"
             (y en la carpeta que se pase con --carpeta)
  iw         Infinite World (carpeta del juego PS2): modelos, voces y gritos de IW, y su moveset
             original (golpes, tecnicas, definitiva; tabla del ejecutable, iw_movesets) o el del
             port de la comunidad si lo hay; --golpes-donante = los del donante
  sb1 / sb2  Shin Budokai / Another Road (ISO PSP): modelos BC<XXX>B0n (formas, psp_amo.py);
             golpes, camara y tecnicas de SB si awo_tools/sbport.py y sb_tecnicas.py estan
             listos (si no, los del donante, como b2)
  sdbh       Super Dragon Ball Heroes World Mission (PC): modelos HD model/bc<xxx>/bc<xxx>bNN
             (awo_tools/sdbh_model.py); golpes de SB si el personaje esta en SB y sbport listo

Modelos de un AFS de PS2: entradas #AMB cuyos dos primeros hijos son #AMO y #AMT; el
prefijo de los huesos (X16G_, XFRZ_...) da el personaje (catalog_b3.cat) y el donante.

Contrato con las herramientas de Shin Budokai (se llaman solo si su fichero tiene __main__;
si fallan, el personaje se importa igual con los golpes del donante y queda un aviso):
  sbport.py --juego sb1|sb2 --personaje XXX --donante ID --salida DIR --modelos m1.bin ...
            -> DIR/anm_forma1.bin + DIR/camara.bin (HD)
  sb_tecnicas.py bsp --personaje XXX --juego sb1|sb2 --donante ID --salida DIR2 -> DIR2/tecnicas.bin
  sb_tecnicas.py aplicar --moveset DIR --personaje XXX --tecnicas DIR2 --donante ID
            -> DIR2/anm_forma1.bin y DIR2/camara.bin si las cambia (si no, se usan las de DIR)
  sdbh_model.py convertir <carpeta bcXXXbNN> <plantilla HD.bin> <salida.bin>
  voces/gritos: clave "sb1:XXX" / "sb2:XXX" si gritos.py o voces.py la entienden.

Uso (salida en lineas TAB para el launcher):
  python importar.py fuentes
  python importar.py lista FUENTE [--carpeta DIR]
  python importar.py importar FUENTE CLAVE --mod CARPETA --nombre NOMBRE [--donante ID]
                     [--id ID] [--despues-de ID] [--carpeta DIR]
"""
import argparse
import glob
import json
import os
import re
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "awo_tools")]

PS2 = os.path.join(ROOT, "ps2_games")
RESOURCES = os.path.join(ROOT, "modding resources")
PORTS = os.path.join(RESOURCES, "Infinite World to Budokai 3 Moveset Ports")
CACHE = os.path.join(HERE, "__pycache__", "importar_cache.json")
MAX_COSTUMES = 4

GAMES = [
    # id, nombre, estado por defecto
    ("b1", "Budokai 1", "listo"),
    ("b2", "Budokai 2", "listo"),
    ("b3", "Budokai 3 (mods de la comunidad)", "listo"),
    ("iw", "Infinite World", "listo"),
    ("sb1", "Shin Budokai", "listo"),
    ("sb2", "Shin Budokai: Another Road", "listo"),
    ("sdbh", "Super Dragon Ball Heroes: World Mission", "listo"),
]
NOTES = {
    "b1": "Modelo, golpes, combos y gritos del Budokai 1 original; definitiva de su equivalente en B3.",
    "b2": "Modelo de Budokai 2; golpes, tecnicas y definitiva de su equivalente en Budokai 3.",
    "b3": "Modelos de la comunidad (.amb / .amo+.amt); golpes del donante.",
    "iw": "Modelo, voces, gritos, golpes, tecnicas y definitiva de Infinite World (con modo hiper).",
    "sb1": "Modelos de PSP con sus formas; golpes de Shin Budokai (sbport) o del donante.",
    "sb2": "Modelos de PSP con sus formas; golpes de Shin Budokai (sbport) o del donante.",
    "sdbh": "Modelos HD de Heroes (boca, 7 caras, rampas); golpes de Shin Budokai o del donante.",
}

# Budokai 1: registro del ELF -> ID de B3 cuyo moveset sirve de base (agarre, modo hiper...)
B1_NAMES = ["Goku", "Kid Gohan", "Teen Gohan", "Vegeta", "Krillin", "Trunks", "Piccolo", "Tien",
            "Yamcha", "Raditz", "Nappa", "Ginyu", "Recoome", "Zarbon", "Dodoria", "Frieza",
            "Android 16", "Android 17", "Android 18", "Android 19", "Cell", "Hercule",
            "Saibaman", "Cell Jr.", "Great Saiyaman"]
B1_DONOR = [0, 2, 3, 7, 10, 8, 11, 12, 13, 18, 19, 20, 21, 38, 19, 27, 28, 29, 30, 32, 33, 14,
            42, 43, 5]


def str_list(v):
    return "[" + ", ".join('"%s"' % x for x in v) + "]"


def emit(*cols):
    print("\t".join(str(c).replace("\t", " ").replace("\n", " ") for c in cols))


# ---------------------------------------------------------------- utilidades
def find_iso(*patterns):
    for pat in patterns:
        hits = sorted(glob.glob(os.path.join(PS2, pat)))
        if hits:
            return hits[0]
    return None


def iw_dir():
    """Infinite World: carpeta extraida o ISO (cualquier region)."""
    hits = sorted(glob.glob(os.path.join(PS2, "*Infinite World*")), key=lambda p: not os.path.isdir(p))
    return next((p for p in hits if os.path.isdir(os.path.join(p, "USR")) or os.path.isdir(os.path.join(p, "usr"))
                 or p.lower().endswith(".iso")), None)


def sdbh_dir():
    hits = glob.glob(os.path.join(RESOURCES, "Super Dragon Ball Heroes*"))
    return hits[0] if hits else None


def source_path(game):
    if game == "b1":
        import b1port  # noqa: PLC0415  (cualquier region: '... Budokai (Europe)...', '(USA)'...)
        p = b1port.find_b1_iso()
        return p if os.path.isfile(p) else None
    if game == "b2":
        import iso  # noqa: PLC0415  (cualquier region)
        return iso.find_game("b2")
    if game == "b3":
        return RESOURCES if os.path.isdir(RESOURCES) else None
    if game == "iw":
        return iw_dir()
    if game == "sb1":
        import iso  # noqa: PLC0415
        return iso.find_game("sb1")
    if game == "sb2":
        import iso  # noqa: PLC0415
        return iso.find_game("sb2")
    if game == "sdbh":
        return sdbh_dir()
    return None


def catalog():
    """prefijo de huesos -> nombre (catalog_b3.cat) e ID de B3 que lo usa."""
    names = {}
    with open(os.path.join(HERE, "catalog_b3.cat"), encoding="utf-8") as fh:
        for ln in fh:
            if ln.startswith("#") or "|" not in ln:
                continue
            b, nm, lab, var, j = ln.rstrip("\n").split("|")[:5]
            names.setdefault(lab.split("_")[0], (nm, int(b)))
    import roster_build  # noqa: PLC0415
    by_fid = {}
    for e in roster_build.DB["ids"]:
        for m in e.get("models") or []:
            if m < 0xFFFFFFFF:
                by_fid.setdefault(m, e["id"])
    return {p: (nm, by_fid.get(b)) for p, (nm, b) in names.items()}


EXTRA_NAMES = {  # personajes que B3 no tiene (prefijos de B2 / IW)
    "XBAB": "Babidi", "XGKG": "Goku GT", "XGKT": "Goku GT", "XVGB": "Baby Vegeta",
    "XS17": "Super 17", "XJNB": "Janemba", "XPKN": "Pikkon", "XPAN": "Pan",
    "XGVT": "Vegeta GT", "XGS2": "Great Saiyaman 2", "XBBR": "Broly Super",
    "XGTA": "Gogeta", "XVTO": "Vegito", "XGXL": "Gotenks (adulto)", "XGXG": "Gotenks fantasma",
    "XKBT": "Kibito", "XKOK": "Kibito Kai", "BCDBR": "Dabura (alternativo)", "SNR": "Shenron",
}
EXTRA_DONORS = {"XRCM": ("Recoome", 21), "XSBM": ("Saibaman", 42), "TSH": ("Tenshinhan", 12)}
# sin ID propio en B3 (fusiones, Babidi, Kibito...): el personaje de B3 mas parecido, con modo hiper
# y definitiva si lo hay (sin esto caian en el 21, Recoome)
SIMILAR_DONOR = {"XBAB": 37, "BCDBR": 37, "XGTA": 0, "XVTO": 7, "XGTX": 8, "XGXL": 8, "XGXG": 8,
                 "XKBT": 16, "XKOK": 16}


def cache_load():
    try:
        with open(CACHE, encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return {}


def cache_save(c):
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE, "w", encoding="utf-8") as fh:
        json.dump(c, fh)


class AfsFile:
    """AFS de PS2 leido en su sitio (fichero suelto o dentro de una ISO)."""

    def __init__(self, path, inner=None):
        self.key = "%s|%s" % (path, inner or "")
        if inner:
            import iso  # noqa: PLC0415
            img = iso.Iso(path)
            self.f = img.open(img.find(inner) or inner)      # sin distinguir mayusculas
        else:
            self.f = open(path, "rb")
        st = os.stat(path)
        self.stamp = "%d-%d" % (st.st_size, int(st.st_mtime))
        self.f.seek(4)
        n = struct.unpack("<I", self.f.read(4))[0]
        self.tab = struct.unpack("<%dI" % (2 * n), self.f.read(8 * n))
        self.n = n

    def entry(self, i):
        self.f.seek(self.tab[2 * i])
        return self.f.read(self.tab[2 * i + 1])

    def head(self, i, size=0x60):
        self.f.seek(self.tab[2 * i])
        return self.f.read(size)


BONE_RE = re.compile(rb"([A-Z0-9]{2,5})_(?:WAIST|HIPS|BODY|HEAD)")


def model_prefix(d):
    m = BONE_RE.search(d)
    return m.group(1).decode() if m else None


def scan_models(afs):
    """[(fid, prefijo)] de los modelos (#AMB [AMO, AMT]) de un AFS, con cache en disco."""
    c = cache_load()
    key = afs.key + "|v3"            # (cache anterior: incluia el #AMB v2 de Shenron)
    hit = c.get(key)
    if hit and hit.get("stamp") == afs.stamp:
        return [tuple(x) for x in hit["models"]]
    out = []
    for i in range(afs.n):
        s = afs.tab[2 * i + 1]
        if s < 60000 or s > 4000000:
            continue
        h = afs.head(i)
        # version 3 = la de los luchadores de PS2; Shenron (B2) es un #AMB v2 que el montaje no lee
        if h[:4] != b"#AMB" or struct.unpack_from("<I", h, 0xC)[0] != 3 or struct.unpack_from("<I", h, 0x10)[0] < 2:
            continue
        o0 = struct.unpack_from("<I", h, 0x20)[0]
        o1 = struct.unpack_from("<I", h, 0x30)[0]
        if o0 + 4 > s or o1 + 4 > s:
            continue
        afs.f.seek(afs.tab[2 * i] + o0)
        t0 = afs.f.read(4)
        afs.f.seek(afs.tab[2 * i] + o1)
        t1 = afs.f.read(4)
        if (t0, t1) != (b"#AMO", b"#AMT"):
            continue
        afs.f.seek(afs.tab[2 * i] + o0)
        p = model_prefix(afs.f.read(o1 - o0))
        if p:
            out.append((i, p))
    c[key] = {"stamp": afs.stamp, "models": out}
    cache_save(c)
    return out


def group_by_character(models):
    """Modelos con el mismo prefijo de huesos = un personaje (sus trajes)."""
    cat = catalog()
    groups = {}
    for fid, p in models:
        groups.setdefault(p, []).append(fid)
    out = []
    for p, fids in groups.items():
        nm, donor = cat.get(p) or EXTRA_DONORS.get(p) or (EXTRA_NAMES.get(p), None)
        if donor is None:
            donor = SIMILAR_DONOR.get(p)
        out.append({"clave": p, "nombre": nm or "Modelo %s" % p, "modelos": fids, "donante": donor})
    # mismo nombre (Gohan adulto / joven...): se distinguen por el personaje de B3 que lo usa
    import roster_build  # noqa: PLC0415
    b3 = {e["id"]: e["name"].title() for e in roster_build.DB["ids"]}
    seen = {}
    for e in out:
        seen.setdefault(e["nombre"].lower(), []).append(e)
    for same in seen.values():
        if len(same) > 1:
            for e in same:
                e["nombre"] += " (%s)" % (b3.get(e["donante"]) if e["donante"] is not None else e["clave"])
    out.sort(key=lambda e: e["nombre"].lower())
    return out


def amb_pair(amo, amt):
    """#AMB PS2 de un modelo a partir de su AMO y su AMT (como los de Budokai 1)."""
    import b1port  # noqa: PLC0415
    return b1port.amb_build([(amo, 1), (amt, 2)])


# ---------------------------------------------------------------- colecciones (b3)
IW_MODELS = os.path.join(RESOURCES, "All Character Models from IW into AMB format")


def community_files(roots):
    out = {}
    for d in roots:
        for dp, _, files in os.walk(d):
            for fn in files:
                low = fn.lower()
                if low.endswith(".amb") or low.endswith(".amo"):
                    out[os.path.join(dp, fn)] = fn
    return out


SKIP = re.compile(r"scouter|hair\.amb|forearms|\[npc\]|\[cutscene|\[select|\[overworld|question mark|"
                  r"shenron \[|bubbles|giru", re.I)


def donor_by_name(name):
    """ID de B3 cuyo nombre (catalogo) aparece en el del modelo (el mas largo gana)."""
    low = name.lower()
    ids = {}
    for nm, did in catalog().values():
        if did is not None and nm:
            ids[nm.lower()] = min(did, ids.get(nm.lower(), did))
    for alias, did in (("hercule", 14), ("frieza", 27), ("tien", 12), ("captain ginyu", 20),
                       ("kid gohan", 2), ("teen gohan", 3), ("adult gohan", 4), ("kid trunks", 9),
                       ("future trunks", 8), ("majin buu", 34), ("kid buu", 36), ("super buu", 35),
                       # sin casilla propia en B3: el mas parecido (modo hiper, agarre, aura)
                       ("babidi", 37), ("gogeta", 0), ("vegito", 7), ("janemba", 10), ("omega shenron", 41),
                       ("pan", 15), ("pikkon", 11), ("recoome", 21), ("saibaman", 42), ("super 17", 29),
                       ("shenron", 41)):
        ids[alias] = did
    hits = [n for n in ids if n in low]
    return ids[max(hits, key=len)] if hits else None


def community_list(roots):
    """Un personaje por modelo base: 'Goku (GT) (Default).amb' con sus formas
    'Goku (GT) (Default) - SSJ.amb'... (la misma pieza en .amb y .amo cuenta una vez)."""
    groups = {}
    for path, fn in sorted(community_files(roots).items()):
        if SKIP.search(fn):
            continue
        if fn.lower().endswith(".amo") and not os.path.exists(os.path.splitext(path)[0] + ".amt"):
            continue
        stem = re.sub(r"^\d+[a-z]?\.\s*", "", os.path.splitext(fn)[0]).strip()
        parts = stem.split(" - ")    # 'Goku (EoZ) - (Halo) - SSJ': variante (Halo), forma SSJ
        if len(parts) > 1 and not parts[-1].startswith("("):
            base, form = " - ".join(parts[:-1]), parts[-1]
        else:
            base, form = stem, ""
        g = groups.setdefault(base.lower(), {"nombre": base.replace(" - ", " "), "formas": {}})
        g["formas"].setdefault(form.lower(), path)        # duplicados .amb/.amo: el primero
    out = []
    for k, g in sorted(groups.items()):
        files = [g["formas"][f] for f in sorted(g["formas"], key=lambda f: (f != "", ))]
        nm = re.sub(r"\s*\((default|default armour)\)", "", g["nombre"], flags=re.I).strip()
        out.append({"clave": "c:" + k, "nombre": nm, "ficheros": files, "donante": donor_by_name(nm)})
    return out


def community_model(path):
    if path.lower().endswith(".amo"):
        return amb_pair(open(path, "rb").read(), open(os.path.splitext(path)[0] + ".amt", "rb").read())
    return open(path, "rb").read()


# ---------------------------------------------------------------- Shin Budokai (PSP) y Heroes (PC)
# codigo de 3 letras (SB y SDBH usan los mismos) -> (nombre, ID de B3 donante: hiper, agarre...)
SB_CHARS = {
    "GOK": ("Goku", 0), "VGT": ("Vegeta", 7), "PIC": ("Piccolo", 11), "KLL": ("Krillin", 10),
    "GHM": ("Teen Gohan", 3), "GHL": ("Adult Gohan", 4), "GHF": ("Future Gohan", 4), "TRX": ("Future Trunks (sword)", 8),
    "TRF": ("Future Trunks (melee)", 8), "FRZ": ("Frieza", 27), "CEL": ("Cell", 33), "COO": ("Cooler", 38),
    "BRL": ("Broly", 40), "18G": ("Android 18", 30), "BUS": ("Kid Buu", 36), "BUL": ("Majin Buu", 34),
    "BUM": ("Super Buu", 35), "BDK": ("Bardock", 39), "DBR": ("Dabura", 37), "GGT": ("Gogeta", 7),
    "VTO": ("Vegito", 7), "GTX": ("Gotenks", 6), "JNB": ("Janemba", 10), "PKH": ("Pikkon", 11),
}
SB_NEW = {"GHF", "TRF", "GGT", "VTO", "GTX", "JNB", "PKH"}     # sin casilla propia en B3
MAX_FORMS = 6


def awo_tool(name):
    """Ruta de una herramienta de awo_tools si ya tiene linea de comandos (__main__); si no, None."""
    p = os.path.join(ROOT, "awo_tools", name)
    try:
        with open(p, encoding="utf-8", errors="replace") as fh:
            return p if "__main__" in fh.read() else None
    except OSError:
        return None


def run_tool(path, args, what):
    """Ejecuta una herramienta (el mismo Python); su salida va al registro. Error -> excepcion."""
    import subprocess  # noqa: PLC0415
    import roster_build as rb  # noqa: PLC0415
    r = subprocess.run([sys.executable, path] + [str(x) for x in args], capture_output=True, text=True,
                       encoding="utf-8", errors="replace", env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    for ln in (r.stdout + r.stderr).strip().splitlines()[-8:]:
        rb.log("   %s: %s" % (what, ln))
    if r.returncode != 0:
        raise RuntimeError("%s fallo (codigo %d)" % (what, r.returncode))


class SbAfs:
    """data_btl_cmn.afs de una ISO de PSP (con tabla de nombres de 48 B), leido en su sitio."""

    def __init__(self, iso_path):
        import iso  # noqa: PLC0415
        img = iso.Iso(iso_path)
        self.f = img.open(img.find("data_btl_cmn.afs"))
        self.f.seek(4)
        n = struct.unpack("<I", self.f.read(4))[0]
        self.tab = [struct.unpack("<II", self.f.read(8)) for _ in range(n)]
        noff, nsz = struct.unpack("<II", self.f.read(8))
        self.names = [""] * n
        if noff and nsz == 48 * n:
            self.f.seek(noff)
            blob = self.f.read(nsz)
            self.names = [blob[48 * i:48 * i + 32].split(b"\0")[0].decode("ascii", "replace") for i in range(n)]

    def entry(self, i):
        self.f.seek(self.tab[i][0])
        return self.f.read(self.tab[i][1])


def sb_list(game):
    """BC<XXX>.amb (moveset), BC<XXX>B0n.amb (modelos: formas) y BC<XXX>M<n>.amb por personaje."""
    afs = SbAfs(source_path(game))
    chars = {}
    for i, nm in enumerate(afs.names):
        m = re.match(r"BC(\w{3})(B\d\d|M\d)?\.amb$", nm, re.I)
        if not m or m.group(1).upper() == "CMN":
            continue
        c = chars.setdefault(m.group(1).upper(), {"modelos": [], "forma_m": [], "moveset": None})
        part = (m.group(2) or "").upper()
        if not part:
            c["moveset"] = i
        else:
            c["modelos" if part.startswith("B") else "forma_m"].append(i)
    out = []
    for code, c in chars.items():
        nm, donor = SB_CHARS.get(code, ("SB %s" % code, None))
        out.append(dict(c, clave=code, nombre=nm, donante=donor, nuevo=code in SB_NEW))
    out.sort(key=lambda e: e["nombre"].lower())
    return out


def sdbh_list():
    """Personajes de SDBH con equivalente en SB/B3: model/bc<xxx>/bc<xxx>bNN (una carpeta por forma)."""
    root = os.path.join(source_path("sdbh") or "", "model")
    out = []
    for code, (nm, donor) in SB_CHARS.items():
        d = os.path.join(root, "bc" + code.lower())
        forms = sorted(glob.glob(os.path.join(d, "bc%sb[0-9][0-9]" % code.lower())))
        if forms:
            out.append({"clave": code, "nombre": nm, "carpetas": forms, "donante": donor, "nuevo": code in SB_NEW})
    out.sort(key=lambda e: e["nombre"].lower())
    return out


def sb_moveset_ready():
    return awo_tool("sbport.py") is not None


def sb_game_of(code):
    """Juego de SB que trae el moveset de un codigo (Another Road primero)."""
    for g in ("sb2", "sb1"):
        if source_path(g):
            try:
                if any(x["clave"] == code and x["moveset"] is not None for x in sb_list(g)):
                    return g
            except (OSError, ValueError):
                continue
    return None


def voice_key(sb_game, code):
    """voces = "sb2:XXX" si voces.py ya entiende el prefijo (agente de voces); los gritos siguen a
    las voces de SB en roster_build."""
    try:
        with open(os.path.join(HERE, "voces.py"), encoding="utf-8", errors="replace") as fh:
            src = fh.read()
    except OSError:
        return None
    return '"%s:%s"' % (sb_game, code) if '"%s:' % sb_game in src else None


def sb_import(game, e, donor, work, us):
    """Modelos (+ golpes, camara y tecnicas de SB si sbport esta listo) -> (modelos, formas,
    claves toml, {ruta en el mod: fichero})."""
    import roster_build as rb  # noqa: PLC0415
    dn = next(x for x in rb.DB["ids"] if x["id"] == donor)
    models = []
    if game == "sdbh":
        import afs_pair  # noqa: PLC0415
        conv = awo_tool("sdbh_model.py")
        if not conv:
            raise SystemExit("ERROR: el conversor de modelos de Heroes (awo_tools/sdbh_model.py) aun no esta listo")
        tpl = os.path.join(work, "plantilla.bin")      # esqueleto y materiales HD del donante
        with open(tpl, "wb") as fh:
            fh.write(bytes(afs_pair.hd(dn["models"][0], region=us or afs_pair.HD_US)))
        for k, folder in enumerate(e["carpetas"][:MAX_FORMS]):
            out = os.path.join(work, "traje%d.bin" % (k + 1))
            run_tool(conv, ["convertir", folder, tpl, out], "sdbh_model")
            models.append(out)
    else:
        import psp_amo  # noqa: PLC0415
        afs = SbAfs(source_path(game))
        for k, i in enumerate(e["modelos"][:MAX_FORMS]):
            out = os.path.join(work, "traje%d.bin" % (k + 1))
            with open(out, "wb") as fh:
                fh.write(psp_amo.convert_model(afs.entry(i)))
            models.append(out)
    keys, files = {}, {}
    sbg = (game if game != "sdbh" else sb_game_of(e["clave"])) if sb_moveset_ready() else None
    if sbg:
        try:
            out = os.path.join(work, "sbport")
            run_tool(awo_tool("sbport.py"), ["--juego", sbg, "--personaje", e["clave"], "--donante", donor,
                                             "--salida", out, "--modelos"] + models, "sbport")
            anm, cam = os.path.join(out, "anm_forma1.bin"), os.path.join(out, "camara.bin")
            if not (os.path.isfile(anm) and os.path.isfile(cam)):
                raise RuntimeError("sbport no dejo anm_forma1.bin y camara.bin en %s" % out)
            tec, evo_anm = awo_tool("sb_tecnicas.py"), []
            if tec:
                try:
                    t = os.path.join(work, "tecnicas")
                    run_tool(tec, ["aplicar", "--personaje", e["clave"], "--juego", sbg, "--donante", donor,
                                   "--moveset", out, "--salida", t], "sb_tecnicas")
                    got = [os.path.join(t, n) for n in ("tecnicas.bin", "anm_forma1.bin", "camara.bin")]
                    if not all(os.path.isfile(p) for p in got):
                        raise RuntimeError("sb_tecnicas no dejo tecnicas.bin, anm_forma1.bin y camara.bin")
                    files["moveset/tecnicas.bin"], anm, cam = got     # moveset con las tecnicas aplicadas
                    keys["tecnicas"] = '"moveset/tecnicas.bin"'
                    # tecnicas que evolucionan con la forma: un moveset por forma (anm_formaK)
                    # (las formas que falten usan el de la forma 1: ahi ninguna tecnica cambia)
                    for k in range(2, len(models) + 1):
                        p = os.path.join(t, "anm_forma%d.bin" % k)
                        if not os.path.isfile(p):
                            break
                        evo_anm.append(p)
                    if os.path.isfile(os.path.join(t, "capsulas.toml")):   # nombres oficiales (propuesta)
                        files["capsulas.toml"] = os.path.join(t, "capsulas.toml")
                    if os.path.isfile(os.path.join(t, "definitiva.json")):   # su definitiva, traducida
                        files["moveset/definitiva.json"] = os.path.join(t, "definitiva.json")
                        keys["definitiva_animaciones"] = '"moveset/definitiva.json"'
                except Exception as ex:  # noqa: BLE001
                    rb.log("aviso: tecnicas de SB sin portar, se usan las del donante (%s)" % ex)
            # formas: las del donante como maximo (el runtime no pasa de ellas; sin su P+K+G no se
            # llega a las demas: Androide 18, los Buu); los modelos de mas no se montan
            nf = max(1, min(len(models), int(dn.get("forms") or 1)))
            if nf < len(models):
                rb.log("el donante tiene %d formas: se usan los %d primeros modelos" % (nf, nf))
                models = models[:nf]
                evo_anm = evo_anm[:nf - 1]
            rel = ["moveset/anm_forma1.bin"]
            files.update({rel[0]: anm, "moveset/camara.bin": cam})
            for k, p in enumerate(evo_anm):
                rel.append("moveset/anm_forma%d.bin" % (k + 2))
                files[rel[-1]] = p
            keys.update(moveset=str_list(rel), camara='"moveset/camara.bin"')
            if len(models) > 1:          # claves del agente de formas (docs/03_formatos/FORMAS_Y_KI.md)
                keys.update(formas=str(len(models)), transformacion='"donante"')
            keys["fisica"] = '"donante"'    # colas del cinturon y pelo con la fisica del donante
            vk = voice_key(sbg, e["clave"])
            if vk:
                keys["voces"] = vk
            rb.log("golpes, combos y camara de %s (%s)" % ("Shin Budokai" if sbg == "sb1" else "Another Road",
                                                           e["clave"]))
            return models, len(models), keys, files
        except Exception as ex:  # noqa: BLE001
            rb.log("aviso: golpes de SB sin portar (%s): se usan los del donante" % ex)
            keys, files = {}, {}
    forms = max(1, min(len(models), int(dn.get("forms") or 1)))   # el runtime solo reduce las del donante
    if forms < len(models):
        rb.log("el donante tiene %d formas: se usan los %d primeros modelos" % (forms, forms))
    return models[:forms], forms, keys, files


def sb_transform_caps(donor, forms):
    """Texto TOML de las [[capsula]] de transformacion (formas 1..n-1) con el nombre de las del donante
    (lista de B3); sin `ki`: la plantilla es la capsula nativa del donante para esa forma (sus barras)."""
    import roster_build as rb  # noqa: PLC0415
    dn = next(x for x in rb.DB["ids"] if x["id"] == donor)
    ids = (dn.get("skills") or {}).get("transformaciones") or []
    lst = os.path.join(RESOURCES, "Budokai_3_Capsules_IDs.txt")
    names = rb.read_name_list(lst)[0] if os.path.isfile(lst) else {}
    out = []
    for k in range(1, forms):
        nm = names.get(ids[k - 1] if k - 1 < len(ids) else None) or "Form %d" % (k + 1)
        out.append('\n[[capsula]]\nnombre = %s\ntipo = "transformacion"\nforma = %d\n' % (json.dumps(nm), k))
        rb.log("capsula de transformacion: %s (forma %d)" % (nm, k))
    return "".join(out)


# ---------------------------------------------------------------- fuentes
def b1_list():
    import b1port  # noqa: PLC0415
    b1 = b1port.B1(source_path("b1"))
    out = []
    for i, nm in enumerate(B1_NAMES):
        r = b1.record(i)
        ms = [m for m in dict.fromkeys(r["models"]) if m > 0]
        out.append({"clave": str(i), "nombre": nm, "modelos": ms, "donante": B1_DONOR[i]})
    return out


def iw_ports():
    """Ports IW -> B3 de la comunidad: {nombre en minusculas: carpeta B3}."""
    out = {}
    for d in sorted(glob.glob(os.path.join(PORTS, "*"))):
        b3 = os.path.join(d, "B3")
        if os.path.isdir(b3):
            out[os.path.basename(d).lower()] = b3
    return out


def port_for(name, ports=None):
    ports = iw_ports() if ports is None else ports
    norm = re.sub(r"[^a-z0-9]", "", re.sub(r"\(alt colou?r pallete\)", "", name.lower()))
    return next((p for k, p in ports.items() if norm == re.sub(r"[^a-z0-9]", "", k)), None)


# Tabla de personajes del ejecutable de IW (SLUS_218.42, USA; RE 2026-10-09): registro de
# 0x190 B por ID de IW con los fids del moveset de cada forma (+0x120, 8 x u32) y la camara
# (+0x140); el BSP de tecnicas en otra tabla por ID. Los ports de la comunidad (Janemba, Pikkon,
# Pan...) son estos mismos bins tal cual, con un hijo #AML en la camara.
IW_REC_SIZE, IW_IDS = 0x190, 80
AML = b"#AML\x03"
_IW_CACHE = {}


def iw_afs(iw):
    """DATA_CMN.AFS de Infinite World, de su carpeta extraida o de su ISO."""
    if os.path.isfile(iw):
        return AfsFile(iw, "/USR/DATA_CMN.AFS")
    return AfsFile(os.path.join(iw, "USR", "DATA_CMN.AFS"))


def _kid_magics(afs, i):
    """Magics de los hijos de una entrada #AMB del AFS (sin leerla entera)."""
    head = afs.head(i, 0x40)
    if head[:4] != b"#AMB":
        return set()
    n, tbl = struct.unpack_from("<II", head, 0x10)
    if not 0 < n < 64:
        return set()
    t = afs.head(i, tbl + 16 * n)[tbl:]
    out = set()
    for k in range(n):
        off, size = struct.unpack_from("<II", t, 16 * k)
        if size and off + 4 <= afs.tab[2 * i + 1]:
            afs.f.seek(afs.tab[2 * i] + off)
            out.add(afs.f.read(4))
    return out


def iw_tables(elf, afs):
    """(offset de la tabla de personajes, offset de la tabla de BSP) del ejecutable de IW, por
    contenido (valen la version americana y la europea; en SLUS_218.42 = 0x34FB20 y 0x348D94):
    registro de 0x190 B por ID con id << 16 en +8 (IDs 2, 10, 11, 20 y 40), camara (#AMC) en
    +0x140; la de BSP es la lista de u32 por ID que apunta a #AMB con efectos (#AME)."""
    def u32(o):
        return struct.unpack_from("<I", elf, o)[0] if 0 <= o <= len(elf) - 4 else None
    rec = None
    for p in range(0, len(elf) - IW_REC_SIZE * 41, 4):
        if u32(p + 8 + IW_REC_SIZE * 2) == 2 << 16 and all(
                u32(p + 8 + IW_REC_SIZE * k) == k << 16 for k in (10, 11, 20, 40)):
            rec = p
            break
    if rec is None:
        raise ValueError("no encuentro la tabla de personajes en el ejecutable de Infinite World")
    ids = [k for k in range(IW_IDS) if 0 < (u32(rec + IW_REC_SIZE * k + 0x140) or 0) < afs.n]
    bsp_like = {}

    def is_bsp(v):
        if v not in bsp_like:
            bsp_like[v] = 0 < v < afs.n and b"#AME" in _kid_magics(afs, v)
        return bsp_like[v]
    best, best_n = None, 0
    k0 = ids[0]
    for q in range(0, len(elf) - 4, 4):
        v = u32(q)
        if not (0 < v < afs.n) or not is_bsp(v):
            continue
        t = q - 4 * k0
        n = sum(1 for k in ids[:12] if is_bsp(u32(t + 4 * k) or 0))
        if n > best_n:
            best, best_n = t, n
            if n == len(ids[:12]):
                break
    return rec, best


def iw_movesets(iw=None):
    """{ID de IW: {"cam": fid, "anm": [fid por forma], "bsp": fid o None}} (cualquier region,
    carpeta o ISO)."""
    iw = iw or source_path("iw")
    if not iw:
        return {}
    if iw in _IW_CACHE:
        return _IW_CACHE[iw]
    import voces  # noqa: PLC0415
    files = voces.GameFiles(iw)
    elf = files.elf()
    if not elf:
        return {}
    d = files.read(elf)
    afs = iw_afs(iw)
    rec, bsp_t = iw_tables(d, afs)
    out = {}
    for cid in range(IW_IDS):
        r = rec + IW_REC_SIZE * cid
        cam = struct.unpack_from("<I", d, r + 0x140)[0]
        # por forma (como el anm de B3: la 1a completa, las demas solo #BSK con los golpes que
        # cambian sobre ella); un hueco en medio = la de la forma 1
        anm = [x if 0 < x < afs.n else 0 for x in struct.unpack_from("<8I", d, r + 0x120)]
        while anm and not anm[-1]:
            anm.pop()
        anm = [x or anm[0] for x in anm]
        if not (0 < cam < afs.n and anm) or b"#AMC" not in afs.head(cam, 0x200):
            continue
        bsp = struct.unpack_from("<I", d, bsp_t + 4 * cid)[0] if bsp_t is not None else 0
        out[cid] = {"cam": cam, "anm": anm, "bsp": bsp if 0 < bsp < afs.n else None}
    _IW_CACHE[iw] = out
    return out


def iw_id_of_model(model, iw=None, ms=None):
    """ID de IW de un modelo de la coleccion (#AMB [AMO, AMT]): su #AMO esta en DATA_CMN justo
    antes de la camara de su personaje (modelos, camara, bocas, moveset)."""
    import b1port  # noqa: PLC0415
    amo = next((k for k, t in b1port.amb_kids(model) if k[:4] == b"#AMO"), None)
    iw = iw or source_path("iw")
    if amo is None or not iw:
        return None
    afs = iw_afs(iw)
    hit = next((i for i in range(afs.n) if afs.tab[2 * i + 1] == len(amo) and afs.head(i, 4096) == amo[:4096]), None)
    if hit is None:
        return None
    ms = iw_movesets(iw) if ms is None else ms
    after = sorted((v["cam"], k) for k, v in ms.items() if v["cam"] > hit)
    return after[0][1] if after else None


def iw_moveset_files(cid, work, iw=None):
    """Bins PS2 del moveset de IW del personaje en work -> {"anm": [...], "cam": p, "bsp": p}
    (lo mismo que port_files de un port de la comunidad)."""
    import b1port  # noqa: PLC0415
    iw = iw or source_path("iw")
    m = iw_movesets(iw).get(cid)
    if not m:
        return {}
    afs = iw_afs(iw)
    out = {"anm": []}
    for k, f in enumerate(m["anm"]):
        p = os.path.join(work, "iw_anm_%d.bin" % f)
        open(p, "wb").write(afs.entry(f))
        out["anm"].append(p)
    ks = b1port.amb_kids(afs.entry(m["cam"]))
    if not any(d[:4] == b"#AML" for d, _ in ks):        # B3 lee los hijos de la camara por posicion
        ks = ks[:1] + [(AML, 6)] + ks[1:]
    out["cam"] = os.path.join(work, "iw_camara.bin")
    open(out["cam"], "wb").write(b1port.amb_build(ks))
    if m["bsp"]:
        out["bsp"] = os.path.join(work, "iw_tecnicas.bin")
        open(out["bsp"], "wb").write(afs.entry(m["bsp"]))
    return out


def list_source(game, extra=None):
    if game == "b1":
        return b1_list()
    if game == "b2":
        return group_by_character(scan_models(AfsFile(source_path("b2"), "/USR/DATA_CMN.AFS")))
    if game == "iw":
        if os.path.isdir(IW_MODELS):      # la coleccion de modelos de IW ya trae los nombres
            lst = community_list([IW_MODELS])
        else:
            lst = group_by_character(scan_models(iw_afs(source_path("iw"))))
        ports = iw_ports()
        for e in lst:
            e["port"] = port_for(e["nombre"], ports)
        return lst
    if game == "b3":
        return community_list([d for d in (RESOURCES, extra) if d and os.path.isdir(d)])
    if game in ("sb1", "sb2"):
        return [e for e in sb_list(game) if e["modelos"]]
    if game == "sdbh":
        return sdbh_list()
    raise ValueError("fuente desconocida: %s" % game)


def suggested_name(game, e):
    """El de B3 ya existe: 'Vegeta B1' para distinguirlos en la rueda."""
    if game in ("sb1", "sb2", "sdbh"):
        return e["nombre"] if e.get("nuevo") else "%s %s" % (e["nombre"], game.upper())
    if game in ("b1", "b2") and e.get("donante") is not None:
        same = e["donante"] == B1_DONOR[int(e["clave"])] and int(e["clave"]) not in B1_OWN if game == "b1"             else True
        if same:
            return "%s %s" % (e["nombre"], game.upper())
    if e.get("ficheros"):        # 'Goku (GT) (ALT Colour Pallete)' -> 'Goku GT'
        short = re.sub(r"\s*\((?:alt colou?r pall?ete|default|recolou?r)\)", "", e["nombre"], flags=re.I)
        return re.sub(r"[()]", "", short).strip()
    return e["nombre"]


B1_OWN = (13, 14, 19)          # Zarbon, Dodoria y Androide 19 no estan en B3


def entry_kind(game, e):
    """(clase, cuantos) de la columna del launcher: b1 | trajes | formas | sb (golpes de SB)."""
    if game == "b1":
        return "b1", len(e["modelos"])
    if game in ("sb1", "sb2", "sdbh"):
        n = min(len(e.get("carpetas") or e.get("modelos") or []), MAX_FORMS)
        own = sb_moveset_ready() and (game != "sdbh" or sb_game_of(e["clave"]) is not None)
        return ("sb" if own else "formas"), n
    if e.get("ficheros"):
        return "formas", min(len(e["ficheros"]), 6)
    return "trajes", min(len(e["modelos"]), MAX_COSTUMES)


def note_for(game, e):
    if game == "b1":
        return "%d modelos; golpes, combos y gritos de B1" % len(e["modelos"])
    if game == "iw" and not e.get("port"):
        e = dict(e, port="iw")        # su moveset de IW (iw_movesets)
    if e.get("ficheros"):
        n = min(len(e["ficheros"]), 6)
        extra = ("; golpes y tecnicas de IW" if e.get("port") == "iw" else
                 "; moveset del port de la comunidad" if e.get("port") else "")
        return ("1 traje" if n == 1 else "1 traje, %d formas" % n) + extra
    n = min(len(e["modelos"]), MAX_COSTUMES)
    extra = ("; golpes y tecnicas de IW" if e.get("port") == "iw" else
             "; moveset del port de la comunidad" if e.get("port") else "")
    return "%d trajes%s" % (n, extra)


# ---------------------------------------------------------------- importar
def port_files(b3dir):
    """Ficheros de un port IW->B3 de la comunidad (unnamed_<fid>.bin, fids del personaje que
    sustituia) -> (ID donante, {'anm': [...], 'cam': path, 'bsp': path})."""
    import roster_build  # noqa: PLC0415
    bins = {}
    for fn in os.listdir(b3dir):
        m = re.match(r"(?:unnamed_)?(\d+)\.bin$", fn, re.I)
        if m:
            bins[int(m.group(1))] = os.path.join(b3dir, fn)
    for e in roster_build.DB["ids"]:
        anm = [a for a in (e.get("anm") or []) if a < 0xFFFFFFFF and a]
        if anm and anm[0] in bins:
            return e["id"], {"anm": [bins[a] for a in anm if a in bins], "cam": bins.get(e.get("cam")),
                             "bsp": bins.get(e.get("bsp"))}
    return None, {}


def iw_voice_name(nombre):
    import voces  # noqa: PLC0415
    try:
        iw = voces.IwVoices(source_path("iw"))
    except Exception:  # noqa: BLE001
        return None
    key = re.sub(r"[^a-z0-9]", "", nombre.lower())
    alias = {"janemba": "janenba", "pikkon": "paikuhan", "gokugt": "gtgokou", "vegetagt": "gtvegeta",
             "babyvegeta": "vegetababy", "super17": "superandroidno17", "greatsaiyaman2": "greatsaiyaman2"}
    key = alias.get(key, key)
    for nm in iw.names:
        if re.sub(r"[^a-z0-9]", "", nm.lower()) == key:
            return nm
    return None


def do_import(a):
    import roster_build as rb  # noqa: PLC0415
    game = a.fuente
    work = tempfile.mkdtemp(prefix="importar_")
    lst = list_source(game, a.carpeta)
    e = next((x for x in lst if x["clave"] == a.clave), None)
    if e is None:
        raise SystemExit("ERROR: no encuentro %r en %s" % (a.clave, game))
    donor = a.donante if a.donante is not None else (e.get("donante") if e.get("donante") is not None else 21)
    models, keys, port = [], {}, None
    forms = 1
    if game == "b1":
        import b1port  # noqa: PLC0415
        b1 = b1port.B1(source_path("b1"))
        for m in e["modelos"][:MAX_COSTUMES]:
            p = os.path.join(work, "traje%d.amb" % (len(models) + 1))
            open(p, "wb").write(amb_pair(b1.entry(m), b1.entry(m + 1)))
            models.append(p)
    elif game in ("sb1", "sb2", "sdbh"):  # PSP / Heroes: formas de un traje (+ golpes de SB)
        models, forms, sb_keys, sb_files = sb_import(game, e, donor, work, a.us)
    elif e.get("ficheros"):            # colecciones: modelo base y sus formas (un traje)
        for f in e["ficheros"][:6]:
            p = os.path.join(work, "traje%d.amb" % (len(models) + 1))
            open(p, "wb").write(community_model(f))
            models.append(p)
        forms = len(models)
        port = e.get("port") or port_for(e["nombre"])
    else:                             # modelos de un AFS: un traje cada uno
        afs = (AfsFile(source_path("b2"), "/USR/DATA_CMN.AFS") if game == "b2"
               else iw_afs(source_path("iw")))
        for m in e["modelos"][:MAX_COSTUMES]:
            p = os.path.join(work, "traje%d.amb" % (len(models) + 1))
            open(p, "wb").write(afs.entry(m))
            models.append(p)
        port = e.get("port")
    rb.log("importando %s de %s: %d modelos, donante %d" % (e["nombre"], dict((g, n) for g, n, s in GAMES)[game],
                                                         len(models), donor))
    files, own_donor = {}, donor
    if port:
        pd, files = port_files(port)
        if pd is not None and files.get("anm"):
            donor = pd
            rb.log("port de la comunidad: moveset de %s (sustituia al ID %d)" % (os.path.dirname(port), pd))
    if game == "iw" and files.get("cam"):
        # un port de la comunidad sin modo hiper ni aura de IW esta incompleto (el de Pan: 4
        # entradas en su BCM): mejor el moveset original de IW
        try:
            import capsulas  # noqa: PLC0415
            pc = rb.load_bin(files["cam"], work)
            at = capsulas.ccm_child(pc)
            sm = capsulas.bcm_summary(pc[at[0]:at[0] + at[1]]) if at else {}
            if not (sm.get("hiper") or sm.get("aura_iw")):
                rb.log("aviso: el port de la comunidad esta incompleto (sin modo hiper): moveset original de IW")
                files, port, donor = {}, None, own_donor
        except Exception as ex:  # noqa: BLE001
            rb.log("aviso: port de la comunidad sin comprobar (%s)" % ex)
    if game == "iw" and not files.get("anm") and models and not a.golpes_donante:
        try:                          # golpes, tecnicas y definitiva originales de IW
            cid = iw_id_of_model(open(models[0], "rb").read())
            files = iw_moveset_files(cid, work) if cid is not None else {}
            if files.get("anm"):
                port = "iw"
                rb.log("golpes, tecnicas y definitiva de Infinite World (personaje %d de IW)" % cid)
            else:
                rb.log("aviso: sin moveset de IW para este modelo: golpes del donante")
        except Exception as ex:  # noqa: BLE001
            files = {}
            rb.log("aviso: moveset de IW sin leer (%s): golpes del donante" % ex)
    if game == "b1":                  # golpes, combos y gritos de B1 sobre el moveset del donante
        import afs_pair  # noqa: PLC0415
        import ps2hd  # noqa: PLC0415
        dn = next(x for x in rb.DB["ids"] if x["id"] == donor)
        rep = []
        b1_ult = {}               # su definitiva de B1 traducida a la cinematica del donante
        anm, cam = b1port.port(b1, int(e["clave"]), afs_pair.ps2(dn["anm"][0]), afs_pair.ps2(dn["cam"]), rep,
                               models, [0x64], b1port.donor_model(dn["anm"][0]), ult_out=b1_ult)
        for ln in rep:
            rb.log("   " + ln)
        # con transformacion (P+K+G) los modelos van de dos en dos: traje 1 normal, traje 1 forma 2...
        if any(ln.startswith("transformacion:") for ln in rep) and len(models) >= 2 and len(models) % 2 == 0:
            forms = 2
        b1_anm, b1_cam = bytes(ps2hd.convert_block(anm)), bytes(ps2hd.convert_block(cam))
    ns = argparse.Namespace(mods=a.mods, us=a.us, mod=a.mod, nombre=a.nombre, donante=donor, id=a.id,
                            modelo=models, cara=None, icono=None, retrato=None, retrato_p1=None,
                            retrato_p2=None, captura=None, despues_de=a.despues_de, por_traje=forms)
    rb.new_char(ns)
    d = os.path.join(a.mods or rb.default_mods(), a.mod)
    toml = os.path.join(d, "personaje.toml")
    os.makedirs(os.path.join(d, "moveset"), exist_ok=True)
    if game == "b1":
        open(os.path.join(d, "moveset", "anm.bin"), "wb").write(b1_anm)
        open(os.path.join(d, "moveset", "camara.bin"), "wb").write(b1_cam)
        keys.update({"moveset": str_list(["moveset/anm.bin"] * forms), "camara": '"moveset/camara.bin"',
                     "gritos": '"b1:%s"' % e["clave"], "voces": '"ninguna"'})
        if b1_ult.get("receta"):
            with open(os.path.join(d, "moveset", "definitiva.json"), "w", encoding="utf-8") as fh:
                json.dump(dict({"_capsulas_b1": b1_ult["capsulas"]}, **b1_ult["receta"]), fh, indent=1)
            keys["definitiva_animaciones"] = '"moveset/definitiva.json"'
        if forms > 1:
            keys["formas"] = str(forms)
    elif game in ("sb1", "sb2", "sdbh"):
        import shutil  # noqa: PLC0415
        for rel, src in sb_files.items():
            if rel.startswith("moveset/"):
                shutil.copyfile(src, os.path.join(d, rel))
        keys.update(sb_keys)
    elif files.get("anm"):
        import shutil  # noqa: PLC0415
        rel = []
        if port == "iw":            # IW: uno por forma; las que no tengan, los golpes de la 1a
            files["anm"] = files["anm"][:max(forms, 1)]
        seen = {}
        for k, f in enumerate(files["anm"]):
            if f not in seen:         # el mismo bin en varias formas se copia una vez
                seen[f] = "moveset/anm_forma%d.bin" % (k + 1)
                shutil.copyfile(f, os.path.join(d, seen[f]))
            rel.append(seen[f])
        keys["moveset"] = str_list(rel)
        if files.get("cam"):
            shutil.copyfile(files["cam"], os.path.join(d, "moveset", "camara.bin"))
            keys["camara"] = '"moveset/camara.bin"'
        if files.get("bsp"):
            shutil.copyfile(files["bsp"], os.path.join(d, "moveset", "tecnicas.bin"))
            keys["tecnicas"] = '"moveset/tecnicas.bin"'
    if game in ("iw", "b3"):
        vn = iw_voice_name(e["nombre"]) if source_path("iw") else None
        if vn:
            keys["voces"] = '"iw:%s"' % vn
            rb.log("voces y gritos de Infinite World: %s" % vn)
    if keys:
        rb.set_toml_keys(toml, keys)
    sb_caps = False
    if game in ("sb1", "sb2", "sdbh"):    # capsulas: tecnicas con nombre oficial (sb_tecnicas) + formas
        extra = ""
        if sb_files.get("capsulas.toml"):
            with open(sb_files["capsulas.toml"], encoding="utf-8") as fh:
                extra += "\n" + fh.read().strip() + "\n"
            sb_caps = True
            rb.log("capsulas de las tecnicas de SB: nombres oficiales (sb_tecnicas)")
        if sb_keys.get("transformacion"):
            try:
                extra += sb_transform_caps(donor, forms)
            except Exception as ex:  # noqa: BLE001
                rb.log("aviso: capsulas de transformacion sin crear (%s)" % ex)
        if extra:
            with open(toml, "a", encoding="utf-8") as fh:
                fh.write(extra)
    if keys.get("camara") and not sb_caps:     # capsulas propias desde su moveset (B1 / IW)
        try:
            cap_args = argparse.Namespace(importar="b1" if game == "b1" else "auto", lista=None, catalogo=None)
            with open(toml, "rb") as fh:
                c = rb.tomllib.load(fh).get("personaje", {})
            caps = rb.import_caps(cap_args, toml, c, d)
            # su definitiva: la de B1 traducida (nombre de B1) o la del donante si su capsula
            # chocaba con una de B1 (b1port la paso a la 595)
            cu = b1_ult.get("capsula_donante") if game == "b1" else None
            if cu and (b1_ult.get("receta") or cu != b1_ult.get("capsula_donante_b3")):
                nm = (rb.b1_cap_name(b1_ult["capsulas"][0], "Ultimate") if b1_ult.get("receta") else
                      rb.b3_cap_names().get(b1_ult["capsula_donante_b3"], "Ultimate"))
                caps = (caps or []) + [{"nombre": nm, "tipo": "definitiva", "reemplaza": cu}]
                rb.log("definitiva de Budokai 1 traducida: %s (sobre la cinematica de %s)" % (nm, dn["name"])
                       if b1_ult.get("receta") else "definitiva: %s (la del donante)" % nm)
            if caps:
                rb.write_caps(toml, caps)
                rb.log("capsulas propias: %d (renombralas en el editor si quieres)" % len(caps))
        except Exception as ex:  # noqa: BLE001
            rb.log("aviso: capsulas sin importar (%s)" % ex)
    rb.log("listo: %s (se monta al pulsar JUGAR o Reconstruir ahora)" % a.mod)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("fuentes")
    p = sub.add_parser("lista")
    p.add_argument("fuente")
    p.add_argument("--carpeta")
    i = sub.add_parser("importar")
    i.add_argument("fuente")
    i.add_argument("clave")
    i.add_argument("--mod", required=True)
    i.add_argument("--nombre", required=True)
    i.add_argument("--donante", type=int)
    i.add_argument("--id", type=int)
    i.add_argument("--despues-de", type=int)
    i.add_argument("--carpeta")
    i.add_argument("--mods")
    i.add_argument("--us")
    i.add_argument("--golpes-donante", action="store_true",
                   help="iw: golpes del donante en vez de los de Infinite World")
    a = ap.parse_args()
    if a.cmd == "fuentes":
        for g, nm, st in GAMES:
            path = source_path(g)
            if g == "sdbh" and path and not awo_tool("sdbh_model.py"):
                st = "desarrollo"         # el conversor de modelos de Heroes aun no tiene linea de comandos
            emit("fuente", g, nm, st if path else "falta", path or "", NOTES[g])
        return 0
    if a.cmd == "lista":
        for e in list_source(a.fuente, a.carpeta):
            # clave, nombre, donante, clase (b1 | trajes | formas), cuantos, port, nombre sugerido
            kind, n = entry_kind(a.fuente, e)
            emit("personaje", e["clave"], e["nombre"], "" if e.get("donante") is None else e["donante"],
                 kind, n, 1 if e.get("port") else 0, suggested_name(a.fuente, e))
        return 0
    return do_import(a)


if __name__ == "__main__":
    sys.exit(main())
