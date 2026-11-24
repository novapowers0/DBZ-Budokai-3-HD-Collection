#!/usr/bin/env python3
"""Nucleo del Mod Kit, sacado de modkit_gui.py (KIT-05): constantes, idioma, entorno, trabajos, lectura
de herramientas y mods, widgets y la clase base Page. Las paginas (kit/paginas/) hacen `from kit.nucleo import *`.
LANG / ADV_LABELS se leen y escriben como nucleo.LANG (no se exportan con *: serian copias congeladas).
G = el modulo modkit_gui (el `g` que reciben portada, vistas, asistente...); lo pone modkit_gui al cargar."""
from __future__ import annotations

import argparse
import ast
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import traceback
import webbrowser

try:
    import tomllib  # Python 3.11+
except ImportError:  # pragma: no cover - las herramientas piden 3.11+, la ventana se abre igual
    tomllib = None

import tkinter as tk
import tkinter.font as tkfont
from tkinter import filedialog, messagebox, ttk

MODKIT_VERSION = "1.1.0"
KIT_VERSION = "1.4.2.3"

# =================================================================================================
# COMUNIDAD / COMMUNITY: pega aqui el enlace de invitacion del servidor de Discord.
# Mientras este vacio, la pestana "Ayuda" muestra "Proximamente".   Ej.: "https://discord.gg/XXXXXXX"
DISCORD_URL = ""
# =================================================================================================

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # .../mod center hd
KIT = os.path.dirname(HERE)                                  # carpeta del kit (junto a dbz3.exe)
AWO = os.path.join(KIT, "awo_tools")
sys.path.insert(0, HERE)
import swap_b3  # noqa: E402  (deteccion validada del data_cmn.afs del Budokai 3 HD, la misma que las herramientas)
from kit import asistente, ayudas, errores, portada, prueba, revisar, vistas  # noqa: E402,F401  (fase 0: piezas sacadas de este fichero)
IS_WIN = os.name == "nt"
NO_WINDOW = 0x08000000 if IS_WIN else 0                      # CREATE_NO_WINDOW
SELF_NAME = "modkit_gui.py"

# Paleta del launcher (src/launcher/ui_kit.h)
C = {
    "bg": "#111318", "card": "#1A1D24", "card_hi": "#20232B", "frame": "#262A34",
    "hover": "#323642", "active": "#3D414E", "line": "#323641", "text": "#EDF0F5",
    "dim": "#8F96A6", "accent": "#F58C1C", "accent_dim": "#9E5912", "accent_hi": "#FFA445",
    "accent_soft": "#3A2A17", "blue": "#78B0FF", "blue_dim": "#24395A", "ok": "#5CD17A",
    "warn": "#FFBD4D", "err": "#FF6B5C", "log_bg": "#0C0E12", "on_accent": "#16120C",
}
F = {}          # fuentes (init_fonts)
LANG = "es"
ADV_LABELS = False      # modo avanzado: IDs internos junto a los nombres (plazas, personajes base)


def T(es, en):
    """Texto en el idioma actual (como i18n::T del launcher)."""
    return en if LANG == "en" else es


# Donantes: personajes con casilla propia en el select (launcher_state.cpp kDonors)
DONORS = [
    (0, "Goku", "Goku"), (1, "Goku (niño)", "Kid Goku"), (2, "Gohan (niño)", "Kid Gohan"),
    (3, "Gohan (adolescente)", "Teen Gohan"), (4, "Gohan (adulto)", "Adult Gohan"),
    (5, "Gran Saiyaman", "Great Saiyaman"), (6, "Goten", "Goten"), (7, "Vegeta", "Vegeta"),
    (8, "Trunks", "Trunks"), (9, "Trunks (niño)", "Kid Trunks"), (10, "Krillin", "Krillin"),
    (11, "Piccolo", "Piccolo"), (12, "Tenshinhan", "Tien"), (13, "Yamcha", "Yamcha"),
    (14, "Mr. Satán", "Hercule"), (15, "Videl", "Videl"), (16, "Kaio-shin", "Supreme Kai"),
    (17, "Uub", "Uub"), (18, "Raditz", "Raditz"), (19, "Nappa", "Nappa"), (20, "Ginyu", "Ginyu"),
    (21, "Recoome", "Recoome"), (27, "Freezer", "Frieza"), (28, "Androide 16", "Android 16"),
    (29, "Androide 17", "Android 17"), (30, "Androide 18", "Android 18"), (32, "Dr. Gero", "Dr. Gero"),
    (33, "Cell", "Cell"), (34, "Majin Buu", "Majin Buu"), (35, "Super Buu", "Super Buu"),
    (36, "Kid Buu", "Kid Buu"), (37, "Dabura", "Dabura"), (38, "Cooler", "Cooler"),
    (39, "Bardock", "Bardock"), (40, "Broly", "Broly"), (41, "Omega Shenron", "Omega Shenron"),
    (42, "Saibaman", "Saibaman"), (43, "Cell Jr.", "Cell Jr."),
]
# Plazas: los 6 IDs recortados de fabrica y los 20 IDs extra (launcher_state.cpp kFreeSlots)
FREE_SLOTS = [(22, "Guldo"), (23, "Jeice"), (24, "Burter"), (25, "Zarbon"), (26, "Dodoria"),
              (31, "Android 19")] + [(44 + i, "Extra %d" % (i + 1)) for i in range(20)]

SOURCE_NOTES = {
    "b1": ("Modelo, golpes, combos y gritos del Budokai 1 original; definitiva de su equivalente en Budokai 3.",
           "Model, moves, combos and yells from the original Budokai 1; ultimate from its Budokai 3 counterpart."),
    "b2": ("Modelo de Budokai 2 con su pelo y cara; golpes y combos de B2; técnicas, definitiva y voz del donante.",
           "Budokai 2 model with its hair and face; B2 moves and combos; techniques, ultimate and voice from the donor."),
    "b3": ("Modelos de la comunidad (.amb / .amo + .amt) de 'modding resources'.",
           "Community models (.amb / .amo + .amt) from 'modding resources'."),
    "iw": ("Modelo, voces, gritos, golpes, técnicas y definitiva de Infinite World (con modo hiper).",
           "Infinite World model, voices, yells, moves, techniques and ultimate (with hyper mode)."),
    "sdbh": ("Modelos HD de Heroes (boca, 7 caras, rampas); golpes de Shin Budokai o del donante.",
             "HD models from Heroes (mouth, 7 faces, ramps); Shin Budokai or donor moves."),
    "xv2": ("Modelos de Dragon Ball Xenoverse 2 de tu copia de Steam (solo lectura); colores del BCS; golpes y voces del donante.",
            "Dragon Ball Xenoverse 2 models from your Steam copy (read-only); BCS colours; donor moves and voices."),
    "psp": ("Modelos de PSP con sus formas; golpes, combos y cámara de Shin Budokai (o del donante).",
            "PSP models with their forms; Shin Budokai moves, combos and camera (or the donor's)."),
}


def donor_name(did):
    for i, es, en in DONORS:
        if i == did:
            return T(es, en)
    return "?"


def source_note(sid):
    es, en = SOURCE_NOTES.get(sid, SOURCE_NOTES["psp"])
    return T(es, en)


# ================================================================================ utilidades
def norm(p):
    return os.path.normcase(os.path.normpath(os.path.abspath(p))) if p else ""


def first_existing(*paths):
    for p in paths:
        if p and os.path.exists(p):
            return p
    return ""


def split_args(text):
    """Trocea argumentos al estilo Windows: las comillas dobles agrupan y las barras invertidas
    se respetan (rutas como C:\\juegos\\dbz)."""
    out, cur, quoted, started = [], [], False, False
    for ch in text or "":
        if ch == '"':
            quoted = not quoted
            started = True
        elif ch.isspace() and not quoted:
            if started:
                out.append("".join(cur))
                cur, started = [], False
        else:
            cur.append(ch)
            started = True
    if started:
        out.append("".join(cur))
    return out


def cmdline(cmd):
    return subprocess.list2cmdline([str(c) for c in cmd])


def mod_slug(source, name):
    """Igual que ModSlug() del launcher: imp_<fuente>_<nombre en minusculas y _>."""
    out = "imp_" + source + "_"
    for ch in name:
        if ch.isascii() and ch.isalnum():
            out += ch.lower()
        elif out and out[-1] != "_":
            out += "_"
    return out.rstrip("_")


def slug(name, prefix=""):
    out = prefix
    for ch in name:
        if ch.isascii() and ch.isalnum():
            out += ch.lower()
        elif out and out[-1] != "_":
            out += "_"
    return out.rstrip("_") or prefix.rstrip("_") or "mod"


def unique_mod_name(mods_dir, base):
    unique, n = base, 2
    while os.path.exists(os.path.join(mods_dir, unique)):
        unique = "%s_%d" % (base, n)
        n += 1
    return unique


def python_exe():
    """Interprete para las herramientas: DBZ3_PYTHON (como el launcher) o el que abre esta ventana
    (python.exe en vez de pythonw.exe, para que la salida llegue al registro)."""
    env = os.environ.get("DBZ3_PYTHON", "").strip().strip('"')
    if env and os.path.exists(env):
        return env
    exe = sys.executable or "python"
    if os.path.basename(exe).lower() == "pythonw.exe":
        cand = os.path.join(os.path.dirname(exe), "python.exe")
        if os.path.exists(cand):
            return cand
    return exe


def open_path(path):
    """Abre una carpeta o un archivo con el programa del sistema."""
    if not path:
        return False
    try:
        if IS_WIN:
            os.startfile(path)  # noqa: S606
        elif sys.platform == "darwin":
            subprocess.Popen(["open", path])
        else:
            subprocess.Popen(["xdg-open", path])
        return True
    except OSError:
        if IS_WIN and os.path.isfile(path):
            try:
                subprocess.Popen(["notepad.exe", path])
                return True
            except OSError:
                pass
        return False


# ================================================================================ ajustes
def settings_path():
    base = os.environ.get("APPDATA") or os.path.expanduser("~")
    return os.path.join(base, "DBZ3_ModKit", "settings.json")


def load_settings():
    try:
        with open(settings_path(), encoding="utf-8") as fh:
            data = json.load(fh)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def detect_lang(game_dir=""):
    """Idioma inicial del Kit (si el jugador aun no ha elegido): el del juego (dbz3_language del dbz3_user.toml,
    5 = espanol) y, si no hay juego, el de Windows. Antes salia siempre en espanol."""
    try:
        with open(os.path.join(game_dir, "dbz3_user.toml"), encoding="utf-8", errors="replace") as fh:
            m = re.search(r"(?m)^\s*dbz3_language\s*=\s*(\d+)", fh.read())
        if m:
            return "es" if m.group(1) == "5" else "en"
    except OSError:
        pass
    if IS_WIN:
        try:
            import ctypes  # noqa: PLC0415
            return "es" if ctypes.windll.kernel32.GetUserDefaultUILanguage() & 0x3FF == 0x0A else "en"
        except (AttributeError, OSError):
            pass
    return "es" if os.environ.get("LANG", "").lower().startswith("es") else "en"


def save_settings(data):
    try:
        os.makedirs(os.path.dirname(settings_path()), exist_ok=True)
        with open(settings_path(), "w", encoding="utf-8") as fh:
            json.dump(data, fh, indent=1, ensure_ascii=False)
    except OSError:
        pass


# ================================================================================ entorno
class Env:
    """Rutas del kit, del juego y de los datos, con la misma deteccion que el launcher."""

    def __init__(self, settings=None):
        self.settings = settings if settings is not None else {}
        self.refresh()

    def refresh(self):
        s = self.settings
        self.kit, self.mch, self.awo = KIT, HERE, AWO
        self.game_override = s.get("game_dir") or ""
        self.us_override = s.get("us_dir") or ""
        self.game = self._find_game()
        self.mods = self._find_mods()
        self.us = self._find_us()
        self.ps2 = os.path.join(KIT, "ps2_games")
        self.resources = os.path.join(KIT, "modding resources")
        self.python = python_exe()
        # mismo orden que swap_b3.TOOLS_DIR
        self.xdk = first_existing(
            os.path.join(HERE, "tools"),
            os.path.join(KIT, "mod center", "Xbox 360 Compression - Decompression tool from the XBOX "
                                            "Development Kit"),
            os.path.join(KIT, "tools"))
        self.backups = os.path.join(self.game or KIT, "modkit_backups")

    def _find_game(self):
        if self.game_override and os.path.isdir(self.game_override):
            return os.path.abspath(self.game_override)
        for d in (KIT, os.path.dirname(KIT), os.path.join(KIT, "out", "build", "win-amd64-release")):
            if os.path.isfile(os.path.join(d, "dbz3.exe")):
                return os.path.abspath(d)
        return ""

    def _find_mods(self):
        # launcher (ModsOutDir): sube desde la carpeta del exe buscando "mods"
        if self.game:
            probe = self.game
            for _ in range(4):
                cand = os.path.join(probe, "mods")
                if os.path.isdir(cand):
                    return cand
                parent = os.path.dirname(probe)
                if parent == probe:
                    break
                probe = parent
            return os.path.join(self.game, "mods")
        # sin juego: lo mismo que roster_build.default_mods()
        for p in (os.path.join(KIT, "mods"), os.path.join(KIT, "out", "build", "win-amd64-release", "mods")):
            if os.path.isdir(p):
                return p
        return os.path.join(KIT, "mods")

    def _find_us(self):
        # el data_cmn.afs del Budokai 3 HD comprobado (entradas y LZX): la carpeta elegida, la del launcher
        # (dbz3_game_dir), DBZ3/us de un volcado del disco... Antes valia cualquier us/ (un jugador con el de
        # otro juego veia "IndexError" en la vista previa).
        good = swap_b3.find_us(self.us_override if os.path.isdir(self.us_override) else None,
                               roots=(self.game, KIT))
        if good:
            return good
        if self.us_override and os.path.isdir(self.us_override):
            return os.path.abspath(self.us_override)
        # sin datos buenos: la primera us/ que haya, para decir en «Comprobar instalación» qué le pasa
        probe = self.game or KIT
        for _ in range(6):
            for sub in ("us", os.path.join("assets", "us")):
                cand = os.path.join(probe, sub)
                if os.path.isdir(cand):
                    return cand
            parent = os.path.dirname(probe)
            if parent == probe:
                break
            probe = parent
        return ""

    @property
    def tool_default_us(self):
        """La carpeta us/ que las herramientas usan solas (swap_b3.DEFAULT_AFS)."""
        return os.path.dirname(swap_b3.DEFAULT_AFS)

    def pass_us(self):
        """El launcher solo pasa --us/--afs con una ruta propia; aqui tambien cuando el juego no
        esta junto al kit (si no, las herramientas no encontrarian los datos)."""
        return bool(self.us) and (bool(self.us_override) or norm(self.us) != norm(self.tool_default_us))

    def us_args(self):
        return ["--us", self.us] if self.pass_us() else []

    def afs_args(self):
        return ["--afs", os.path.join(self.us, "data_cmn.afs")] if self.pass_us() else []

    @property
    def afs(self):
        return os.path.join(self.us, "data_cmn.afs") if self.us else ""

    def script(self, name):
        for d in (HERE, AWO, os.path.join(HERE, "ports")):
            p = os.path.join(d, name)
            if os.path.isfile(p):
                return p
        return os.path.join(HERE, name)

    def cwd(self):
        return self.game if self.game and os.path.isdir(self.game) else KIT

    def tools_installed(self):
        return os.path.isfile(self.script("importar.py")) and os.path.isfile(self.script("roster_build.py"))


# ================================================================================ comandos
# Mismas llamadas que src/launcher/mod_pipeline.cpp (orden de argumentos incluido).
def cmd_script(env, script, args):
    return [env.python, env.script(script) if not os.path.isabs(script) else script] + [str(a) for a in args]


def cmd_importer(env, args):
    """ImporterQuery(): importar.py fuentes | lista FUENTE ..."""
    return cmd_script(env, "importar.py", args)


def cmd_import_character(env, source, key, mod, name, donor=-1, extra=()):
    """ImportCharacter()."""
    args = ["importar", source, key, "--mod", mod, "--nombre", name, "--mods", env.mods]
    if donor is not None and donor >= 0:
        args += ["--donante", str(donor)]
    args += env.us_args()
    args += list(extra)
    return cmd_importer(env, args)


def cmd_roster_build(env, force=True):
    """BuildRoster(force)."""
    args = ["construir", "--mods", env.mods] + env.us_args()
    if force:
        args.append("--force")
    return cmd_script(env, "roster_build.py", args)


def cmd_roster_status(env, as_json=False):
    return cmd_script(env, "roster_build.py", ["estado", "--mods", env.mods] + (["--json"] if as_json else []))


def cmd_roster_new(env, nc):
    """CreateCharacter(): nc = dict(mod, nombre, donante, por_traje, id, modelos, cara, retrato,
    despues_de, + avanzadas icono / retrato_p1 / retrato_p2 / captura)."""
    args = ["nuevo", "--mods", env.mods, "--mod", nc["mod"], "--nombre", nc["nombre"],
            "--donante", str(nc["donante"])]
    if nc.get("por_traje", 1) > 1:
        args += ["--por-traje", str(nc["por_traje"])]
    if nc.get("id", -1) >= 0:
        args += ["--id", str(nc["id"])]
    for m in nc["modelos"]:
        args += ["--modelo", m]
    if nc.get("cara"):
        args += ["--cara", nc["cara"]]
    if nc.get("retrato"):
        args += ["--retrato", nc["retrato"]]
    if nc.get("despues_de", -1) >= 0:
        args += ["--despues-de", str(nc["despues_de"])]
    for key, flag in (("icono", "--icono"), ("retrato_p1", "--retrato-p1"), ("retrato_p2", "--retrato-p2"),
                      ("captura", "--captura")):
        if nc.get(key):
            args += [flag, nc[key]]
    args += env.us_args()
    return cmd_script(env, "roster_build.py", args)


def cmd_roster_preview(env, mod, extra=()):
    """PreviewCharacter()."""
    return cmd_script(env, "roster_build.py", ["vista", "--mods", env.mods, "--mod", mod] + env.us_args()
                      + [str(x) for x in extra])


def cmd_capsules(env, mod, extra=()):
    """EditCapsules()."""
    return cmd_script(env, "roster_build.py", ["capsulas", "--mods", env.mods, "--mod", mod]
                      + [str(x) for x in extra])


def cmd_swap(env, src_bin, dst_bin, mod=None, extra=()):
    """SwapB3ToB3() / SwapArgs()."""
    mod = mod or "swap_%d_on_%d" % (src_bin, dst_bin)
    args = ["--origen", str(src_bin), "--dest", str(dst_bin), "--mod", mod, "--out", env.mods]
    return cmd_script(env, "swap_b3.py", args + env.afs_args() + list(extra))


def cmd_tex_extract(env, bin_, mod, folder=""):
    """ExtractTextures() / TextureArgs()."""
    args = ["extract", "--bin", str(bin_), "--mod", mod, "--out", env.mods]
    if folder:
        args += ["--dir", folder]
    return cmd_script(env, "texture_b3.py", args + env.afs_args())


def cmd_tex_build(env, mod, slot=-1, folder=""):
    """BuildTextures() / BuildTextureArgs()."""
    args = ["build", "--mod", mod, "--out", env.mods]
    if slot is not None and slot >= 0:
        args += ["--slot", str(slot)]
    if folder:
        args += ["--dir", folder]
    return cmd_script(env, "texture_b3.py", args + env.afs_args())


def parse_sources(text):
    """Lineas 'fuente<TAB>id<TAB>nombre<TAB>estado<TAB>ruta<TAB>nota' de importar.py fuentes."""
    out = []
    for ln in text.splitlines():
        cols = ln.rstrip("\r").split("\t")
        if len(cols) >= 5 and cols[0] == "fuente":
            out.append({"id": cols[1], "name": cols[2], "state": cols[3], "path": cols[4],
                        "note": cols[5] if len(cols) > 5 else ""})
    return out


def preset_label(key):
    """Nombre legible de un preset de presets_importar.toml (los nuevos, tal cual)."""
    return {"original": T("Original (respeta el juego de origen)", "Original (faithful to the source)"),
            "solo_modelos": T("Solo modelos (golpes del donante)", "Models only (donor moves)")}.get(key, key)


def parse_entries(text):
    """Lineas 'personaje<TAB>clave<TAB>nombre<TAB>donante<TAB>clase<TAB>n<TAB>port<TAB>sugerido'."""
    out = []
    for ln in text.splitlines():
        cols = ln.rstrip("\r").split("\t")
        if len(cols) >= 8 and cols[0] == "personaje":
            try:
                count = int(cols[5])
            except ValueError:
                count = 0
            out.append({"key": cols[1], "name": cols[2], "donor": int(cols[3]) if cols[3].strip().lstrip("-").isdigit()
                        else -1, "kind": cols[4], "count": count, "port": cols[6] == "1",
                        "suggested": cols[7]})
    return out


def last_line(text):
    for ln in reversed((text or "").splitlines()):
        if ln.strip() and not ln.startswith("[exit code"):
            return ln.strip()
    return ""


# ================================================================================ procesos
def spawn(cmd, cwd=None):
    env = dict(os.environ)
    env["PYTHONIOENCODING"] = "utf-8"       # que una tilde en la salida no rompa la herramienta
    env["PYTHONUNBUFFERED"] = "1"           # registro en vivo
    env["DBZ3_LANG"] = LANG                 # avisos de las herramientas en el idioma del Kit (swap_b3.ui_lang)
    return subprocess.Popen([str(c) for c in cmd], cwd=cwd or None, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env,
                            creationflags=NO_WINDOW)


def kill_tree(proc):
    try:
        if IS_WIN:
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, creationflags=NO_WINDOW, check=False)
        else:
            proc.kill()
    except OSError:
        pass


def run_sync(cmd, cwd=None, timeout=300):
    """Igual que un trabajo de la ventana pero esperando el resultado (autotest)."""
    p = spawn(cmd, cwd)
    try:
        out, _ = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        kill_tree(p)
        out, _ = p.communicate()
        return -9, out.decode("utf-8", "replace")
    return p.returncode, out.decode("utf-8", "replace")


class Job:
    def __init__(self, cmd, title, done=None, cwd=None, quiet=False):
        self.cmd, self.title, self.done, self.cwd, self.quiet = [str(c) for c in cmd], title, done, cwd, quiet
        self.out, self.proc, self.rc, self.cancelled, self.t0 = [], None, None, False, time.time()

    @property
    def text(self):
        return "".join(self.out)


class JobRunner:
    """Un trabajo a la vez (como ModPipeline::RunAsync), en un hilo; la ventana nunca se bloquea."""

    def __init__(self, app):
        self.app, self.q, self.job = app, queue.Queue(), None

    def busy(self):
        return self.job is not None

    def start(self, job):
        self.job = job
        threading.Thread(target=self._worker, args=(job,), daemon=True).start()
        self.app.root.after(50, self._poll)

    def _worker(self, job):
        try:
            job.proc = spawn(job.cmd, job.cwd)
        except Exception as ex:  # noqa: BLE001
            self.q.put((job, "line", "ERROR: %s\n" % ex))
            self.q.put((job, "done", -1))
            return
        try:
            for raw in iter(job.proc.stdout.readline, b""):
                self.q.put((job, "line", raw.decode("utf-8", "replace")))
        finally:
            try:
                job.proc.stdout.close()
            except OSError:
                pass
        self.q.put((job, "done", job.proc.wait()))

    def _poll(self):
        chunks, finished = [], None
        try:
            while True:
                job, kind, data = self.q.get_nowait()
                if kind == "line":
                    job.out.append(data)
                    if not job.quiet:
                        chunks.append(data)
                else:
                    finished = (job, data)
                    break
        except queue.Empty:
            pass
        if chunks:
            text = "".join(chunks)
            if LANG == "en":    # las herramientas escriben en espanol: el registro sale en ingles
                import log_en  # noqa: PLC0415
                text = log_en.traducir(text)
            self.app.log_text(text)
            try:                # paso a paso: la ultima linea de la herramienta en la barra de abajo
                self.app.status.job_progress(last_line(text))
            except (tk.TclError, AttributeError):
                pass
        if finished:
            job, rc = finished
            job.rc = rc
            self.job = None
            self.app.job_finished(job)
        if self.job is not None:
            try:
                self.app.root.after(60, self._poll)
            except tk.TclError:
                pass

    def cancel(self):
        job = self.job
        if job and job.proc and job.proc.poll() is None:
            job.cancelled = True
            kill_tree(job.proc)


# ================================================================================ herramientas
CATS = [
    ("personajes", "Personajes nuevos", "New characters"),
    ("texturas", "Texturas", "Textures"),
    ("modelos", "Modelos y conversión", "Models & conversion"),
    ("moveset", "Moveset y habilidades", "Moveset & skills"),
    ("audio", "Voces y gritos", "Voices & yells"),
    ("campanas", "Campañas de Dragon Universe", "Dragon Universe campaigns"),
    ("archivos", "AFS, ISO y archivos", "AFS, ISO & files"),
    ("validacion", "Validación", "Validation"),
    ("analisis", "Análisis / investigación (RE)", "Analysis / research (RE)"),
    ("ports", "Ports PS2 → B3 (investigación)", "PS2 → B3 ports (research)"),
    ("experimental", "Experimental / histórico", "Experimental / legacy"),
    ("biblioteca", "Módulos (sin línea de comandos)", "Modules (no command line)"),
]
RESEARCH_CATS = ("analisis", "ports", "experimental")
# archivo -> (categoria, descripcion ES, descripcion EN, rellenar {dest: "mods" | "us" | "afs"})
CURATED = {
    "importar.py": ("personajes", "Importador de personajes de Budokai 1/2, Infinite World y la comunidad "
                    "(el mismo que usa el launcher).", "Character importer from Budokai 1/2, Infinite World "
                    "and the community (the one the launcher uses).", {"mods": "mods"}),
    "catalogo_tecnicas.py": ("moveset", "Catálogo de técnicas por personaje (nativas e importadas) en JSON.",
                             "Technique catalogue per character (native and imported) as JSON.", {"mods": "mods"}),
    "retratos_pack.py": ("campanas", "Lista los retratos de hablante de tu carpeta Potraits (un PNG por personaje); en una "
                         "historia, [[retrato]] png = «pack:Nombre».", "Lists the speaker portraits in your Potraits "
                         "folder (one PNG per character); in a story, [[retrato]] png = 'pack:Name'.", {}),
    "roster_build.py": ("personajes", "Personajes nuevos: montar _roster, crear, vista previa, cápsulas y "
                        "plazas.", "New characters: build _roster, create, preview, capsules and slots.",
                        {"mods": "mods"}),
    "model_render.py": ("personajes", "Render cel-shading (estilo B3) de un bin #AMB HD a PNG.",
                        "Cel-shaded (B3 style) render of an HD #AMB bin to PNG.", {}),
    "name_banner.py": ("personajes", "Cambia el rótulo de nombre de un personaje en el select.",
                       "Changes a character's name banner on the select screen.", {"out": "mods"}),
    "portrait_hd.py": ("personajes", "Retrato del select de PS2 (B3/IW) → retrato HD.",
                       "PS2 select portrait (B3/IW) → HD portrait.", {"out": "mods"}),
    "campana.py": ("campanas", "Campañas: textos nuevos (imágenes por idioma), rivales de los combates y "
                   "escenas redirigidas de Dragon Universe. Historias propias: copia campanas/plantilla_historia.toml "
                   "y usa «montar».",
                   "Campaigns: new texts (images per language), battle rivals and redirected Dragon Universe "
                   "scenes. Your own stories: copy campanas/plantilla_historia.toml and use 'montar'.", {"mods": "mods"}),
    "editor_historias.py": ("campanas", "Editor visual de historias de Dragon Universe: capítulos, rivales, formas, "
                            "trajes, premios, retratos y textos sin tocar archivos; monta el mod con un botón.",
                            "Visual Dragon Universe story editor: chapters, rivals, forms, outfits, rewards, portraits "
                            "and texts without editing files; builds the mod with one button.", {}),
    "du_registro.py": ("campanas", "Saca del código del juego la tabla código de historia → escena (du_codigos.json).",
                       "Extracts the story code → scene table from the game code (du_codigos.json).", {}),
    "capsulas.py": ("personajes", "Módulo de cápsulas (lo usa «roster_build capsulas»).",
                    "Capsule module (used by 'roster_build capsulas').", {}),
    "texture_b3.py": ("texturas", "Extrae las texturas de un personaje a PNG y reconstruye el mod con tus "
                      "ediciones.", "Extracts a character's textures to PNG and rebuilds the mod with your "
                      "edits.", {"out": "mods"}),
    "texture_pack.py": ("texturas", "Valida o lista un pack de texturas (estilo PCSX2).",
                        "Validates or lists a texture pack (PCSX2 style).", {}),
    "texture_upscale_b3.py": ("texturas", "Reescala las texturas de un bin (IA o Lanczos) y reconstruye el "
                              "#AZT.", "Upscales a bin's textures (AI or Lanczos) and rebuilds the #AZT.",
                              {"out": "mods"}),
    "texture_dump_import.py": ("texturas", "Organiza un volcado de texturas del juego (dbz3_texture_dump).",
                               "Organises a texture dump from the game (dbz3_texture_dump).", {}),
    "amt_ps2.py": ("texturas", "Texturas PS2 #AMT → PNG (dump <amt> <carpeta>).",
                   "PS2 #AMT textures → PNG (dump <amt> <folder>).", {}),
    "extract_azt_afs.py": ("texturas", "Extrae entradas #AZT sueltas de un AFS de 360.",
                           "Extracts standalone #AZT entries from a 360 AFS.", {}),
    "swap_b3.py": ("modelos", "Cambio de modelo B3 → B3 (pestaña Model Swap del launcher).",
                   "B3 → B3 model swap (the launcher's Model Swap tab).", {"out": "mods"}),
    "swap_matrix.py": ("modelos", "Mueve cualquier recurso (modelo, moveset, retrato, textura...) entre "
                       "entradas AFS.", "Moves any resource (model, moveset, portrait, texture...) between AFS "
                       "entries.", {"out": "mods"}),
    "ps2hd.py": ("modelos", "Bin de PS2 (B3 GH / ports) → bin HD de 360.", "PS2 bin (B3 GH / ports) → 360 HD bin.",
                 {}),
    "amo2awo.py": ("modelos", "Modelo PS2 #AMO0 / #AMB → #AWO HD (sin plantilla).",
                   "PS2 #AMO0 / #AMB model → HD #AWO (no template).", {}),
    "b3_gateway.py": ("modelos", "Pasarela de modelos 3D (OBJ / glTF / PS2) → bin #AMB de B3 HD.",
                      "3D model gateway (OBJ / glTF / PS2) → B3 HD #AMB bin.", {}),
    "dae_a_b3.py": ("modelos", "Modelo DAE con huesos de estilo Xenoverse/Legends → traje de B3 HD (--mod crea el mod).",
                    "DAE model with Xenoverse/Legends-style bones → B3 HD costume (--mod creates the mod).", {}),
    "altura.py": ("modelos", "Altura de los pies con una animación (corrige personajes que flotan).",
                  "Foot height with an animation (fixes floating characters).", {}),
    "awg_to_obj_b3.py": ("modelos", "Exporta un bin HD de B3 a OBJ.", "Exports a B3 HD bin to OBJ.", {}),
    "awg0_export.py": ("modelos", "Exporta el AWG0 de un bin HD a OBJ.", "Exports the AWG0 of an HD bin to OBJ.",
                       {}),
    "awg_cara_export.py": ("modelos", "Exporta las caras/manos (AWG nb=1) a OBJ.",
                           "Exports faces/hands (AWG nb=1) to OBJ.", {}),
    "awg_to_obj.py": ("modelos", "Exporta un #AWO/#AMB HD a OBJ (world space).",
                      "Exports an HD #AWO/#AMB to OBJ (world space).", {}),
    "obj_to_awg_hd.py": ("modelos", "Conversor universal OBJ → bin HD de B3.", "Universal OBJ → B3 HD bin converter.",
                         {}),
    "psp_amo.py": ("modelos", "Modelo de Shin Budokai (PSP) → bin HD (lo usa el importador).",
                   "Shin Budokai (PSP) model → HD bin (used by the importer).", {}),
    "sdbh_model.py": ("modelos", "Modelo de Super Dragon Ball Heroes World Mission (PC) → bin HD de B3.",
                      "Super Dragon Ball Heroes World Mission (PC) model → B3 HD bin.", {}),
    "sbport.py": ("moveset", "Moveset de Shin Budokai 1/2 → moveset de B3 (golpes, combos, cámara; lo usa "
                  "el importador).", "Shin Budokai 1/2 moveset → B3 moveset (hits, combos, camera; used by "
                  "the importer).", {}),
    "sb_tecnicas.py": ("moveset", "Técnicas de Shin Budokai (efectos BSP, nombres oficiales) → B3.",
                       "Shin Budokai techniques (BSP effects, official names) → B3.", {}),
    "studio_gui.py": ("moveset", "Studio: editor de cámaras de técnicas (ventana propia).",
                      "Studio: technique camera editor (own window).", {"mods": "mods"}),
    "studio_core.py": ("moveset", "Núcleo del Studio: cámaras #ACC, guion #SPX, glTF y autocomprobación.",
                       "Studio core: #ACC cameras, #SPX script, glTF and self-check.", {}),
    "port_b1_to_b3.py": ("modelos", "Port B1 HD → B3 HD (bloqueado por tamaño, ver su ayuda).",
                         "B1 HD → B3 HD port (blocked by size, see its help).", {}),
    "b1port.py": ("moveset", "Moveset de Budokai 1 → moveset de Budokai 3 (lo usa el importador).",
                  "Budokai 1 moveset → Budokai 3 moveset (used by the importer).", {}),
    "sb_amm.py": ("moveset", "Animaciones de Shin Budokai (PSP) → #AMM de B3 (lo usa sbport.py).",
                  "Shin Budokai (PSP) animations → B3 #AMM (used by sbport.py).", {}),
    "csk_edit.py": ("moveset", "Edita el daño de una habilidad en el #CSK HD.",
                    "Edits a skill's damage in the HD #CSK.", {}),
    "csk_chain.py": ("moveset", "Recorre la cadena #CSK (attack code → HR).", "Walks the #CSK chain (attack code → HR).",
                     {}),
    "csk_analyze.py": ("moveset", "Análisis del bloque #CSK (moveset).", "#CSK block (moveset) analysis.", {}),
    "acm_parse.py": ("moveset", "Lee el contenedor #AMB/#ACM (moveset y stages).",
                     "Reads the #AMB/#ACM container (moveset and stages).", {}),
    "acm_analyze.py": ("moveset", "Análisis del pool de animaciones #ACM.", "#ACM animation pool analysis.", {}),
    "voces.py": ("audio", "Voces de combate: sin argumentos lista las voces de Infinite World (ruta opcional a "
                 "su carpeta).", "Battle voices: with no arguments lists the Infinite World voices (optional "
                 "path to its folder).", {}),
    "gritos.py": ("audio", "Módulo de gritos de combate (lo usa roster_build).",
                  "Battle yells module (used by roster_build).", {}),
    "iso.py": ("archivos", "Lista el contenido de una ISO de PS2/PSP sin extraerla.",
               "Lists the contents of a PS2/PSP ISO without extracting it.", {}),
    "afs_pair.py": ("archivos", "Lee entradas AFS de la PS2 GH y de la HD (pares del mismo contenido).",
                    "Reads AFS entries from the PS2 GH and the HD (same-content pairs).", {}),
    "build_afs.py": ("archivos", "Reconstruye un AFS reemplazando una entrada (método antiguo).",
                     "Rebuilds an AFS replacing one entry (old method).", {}),
    "corpus_scan.py": ("archivos", "Escanea todas las entradas de un AFS y clasifica formatos.",
                       "Scans every entry of an AFS and classifies formats.", {}),
    "tools_manifest_check.py": ("validacion", "Valida el inventario de herramientas (tools_manifest.json).",
                                "Validates the tools inventory (tools_manifest.json).", {}),
    "ps2hd_validate.py": ("validacion", "Valida ps2hd.py contra los pares nativos.",
                          "Validates ps2hd.py against the native pairs.", {}),
    "amo2awo_validate.py": ("validacion", "Valida amo2awo.py contra los pares nativos.",
                            "Validates amo2awo.py against the native pairs.", {}),
}

# Herramientas sin entrada en CURATED: en ingles, su descripcion (en espanol sale la 1a linea del
# docstring). Sin esto la pestana Herramientas en EN las mostraba en espanol.
TOOL_EN = {
    "auditar_importar.py": "Imports (and builds) EVERY character of a source in a separate folder and lists the failures",
    "build_awo_from_json.py": "Builds an HD AWO from JSON v2 (SDBH WM / any FBX model)",
    "build_awo_v20.py": "Builds HD AWO v20: Android 18 (SDBH WM) with rebuilt IB and remapped arms",
    "build_awo_v22.py": "Builds HD AWO v22: Android 18 (SDBH) with Android 18's IB, exact counts",
    "colores.py": "Changes the aura and ki (technique effects) color of a B3 HD character",
    "diagnostico.py": "Explains a game log (dbz3_*.log) in plain language",
    "emd_to_awo_hd.py": "EMD (SDBH WM / Xenoverse) -> B3 HD AWO converter, from scratch",
    "empaquetar_v20.py": "Packs HD AWO v20: Android 18 sec34/vb2/ib plus shadow-arm remap",
    "hexedit.py": "Hex editor for expert modders (Hex tab of the advanced Mod Kit)",
    "inject_a18.py": "Injects Android 18 (from JSON) into the sec34 slots of Krillin e326",
    "inject_a18_v21.py": "Injector v21: Android 18 (SDBH WM) into 100% of Krillin's sec34 slots",
    "json_to_obj.py": "SDBH JSON -> OBJ v1: exports an SDBH model (parsed FBX) to OBJ",
    "log_en.py": "Translates the kit tools' output lines into English (Mod Kit log in EN)",
    "obj_to_awg.py": "OBJ to AWG v1: re-imports an edited OBJ into the B3 HD bin",
    "sb_voces.py": "Shin Budokai voices (PSP: SB1 and SB2/Another Road) for ports to B3 HD",
    "studio_core.py": "Studio core (no UI): B3 HD technique cameras",
    "studio_gui.py": "DBZ3 HD Studio: technique camera editor (own Mod Kit window)",
    "port_b3_strip.py": "Route B: rewrites the IB of a port (strip) with an optional bone limit",
    "port_b3_windows.py": "Route B: ports a PS2 geometry to a B3 HD bin using the native windows",
    "port_ps2_b3_decimate.py": "Decimates the PS2 geometry to fit the HD buffers",
    "port_ps2_b3_draw.py": "Step 3 of the PS2 -> B3 HD port pipeline",
    "port_ps2_b3_extract.py": "Step 1 of the PS2 -> B3 HD port pipeline",
    "port_ps2_b3_geometry.py": "Step 2 of the PS2 -> B3 HD port pipeline",
    "port_ps2_b3_inject.py": "Injection: HD template plus converted PS2 geometry",
    "port_ps2_b3_inject_aux.py": "Extends Route A (NPM injection) to the 16 AWGs",
    "port_ps2_b3_pack.py": "Step 4 of the PS2 -> B3 HD port pipeline",
    "port_ps2_b3_verify.py": "Step 5 of the PS2 -> B3 HD port pipeline",
    "test_injection.py": "Injection test: template topology plus our PS2 geometry",
    "afs_extract_hd.py": "Extracts entries from an HD AFS",
    "afs_list.py": "Lists the entries of an AFS",
    "afs_probe.py": "Probes the entries of an AFS (magic and size)",
    "afs_scan.py": "Scans an AFS for known formats",
    "analisis_completo_hd.py": "Exhaustive map of a B3 HD bin (#AMB -> #AWO + #AZT)",
    "analyze_awg.py": "Detailed analyzer of an HD #AWG block (structure mapping)",
    "analyze_awg_full.py": "Analyzer of the full hierarchical structure of an HD #AWG",
    "analyze_awo_b1.py": "Analyzes an HD AWO structure (B1 or B3) to decimate geometry",
    "analyze_bin_hd.py": "Analyzes an HD bin (#AWO/#AWG) with the template",
    "analyze_mesh.py": "Analyzes the mesh parts inside an HD #AWG",
    "analyze_meshgroup.py": "Breaks down the mesh group of an HD AWG (draw structure)",
    "awg_diff.py": "Field-by-field diff of the AWGs of two B3 HD #AMB bins",
    "awg_fields.py": "Checks the fields the guest uses to size the VB fetch",
    "awg_invariants.py": "AWG0 invariants a Route B bin must meet",
    "awg_normal_fix.py": "Normal fix for Route A (PS2 -> B3 HD injection)",
    "awg_parts.py": "Lists the geometry of a B3 HD bin per AWG: bounds, bones",
    "awg_parts2.py": "Exports the AWGs of a B3 HD bin to OBJ, detecting the format",
    "awg_vertex_buffer.py": "Real model of the B3 HD vertex buffer (Route B)",
    "axis_probe.py": "Tests whether the port positions are PERMUTED relative to the native one",
    "bind_oracle.py": "Compares the model-space geometry of two #AMB bins",
    "bind_oracle_bones.py": "Native vs port model-space error PER BONE",
    "bone_probe.py": "Checks where the real bone lives in the window (44 B), native vs port",
    "build_awo.py": "Builds an HD #AWO from the extracted PS2 geometry",
    "build_awo_autocontenido.py": "Builds a self-contained HD #AWO bin",
    "build_awo_desde_cero.py": "Builds a character's HD AWO from scratch",
    "build_awo_template.py": "#AMO0 (PS2) -> #AWO (HD) converter based on a TEMPLATE",
    "build_big_amb.py": "Full AMB re-layout: grows the AWG0 (sec34 or vb2) and repacks",
    "build_from_template.py": "Builds Janemba's HD bin using Cell form 2 as template",
    "build_hd_pipeline.py": "Full PS2 -> HD pipeline with the correct mesh parser",
    "build_hd_world_mats_b3.py": "World matrices of the B3 AWG (from B1, adapted)",
    "build_ib_from_ps2.py": "Builds HD verts+IB from expanded skinned verts plus PS2 triangles",
    "cell_align_check.py": "Compares positions per bone: HD slots (world) vs PS2",
    "cell_dist_stats.py": "Per-bone statistics of the global NPM (slot -> PS2 distances)",
    "cinematica.py": "Ultimates with their own animations on the donor's B3 cinematic",
    "decimar.py": "Vertex decimation by voxel grid (merges close positions)",
    "decimar_tri.py": "Triangle decimation: fewer triangles and vertices",
    "draw_oracle.py": "OFFLINE draw oracle of a B3 HD #AMB bin (AWG0)",
    "extract_geometry.py": "Geometry extractor of a PS2 #AMO0/#AMG: vertices and triangles per mesh part",
    "inyeccion_awg.py": "Injects Janemba's geometry into the main buffer (vb2)",
    "mezclar_ps2_hd.py": "Mixes PS2 positions into the existing sec34 slots of the HD bin",
    "mezclar_ps2_hd_v2.py": "Mixes PS2 positions into sec34 slots with PER-BONE SCALE",
    "mezclar_ps2_hd_v3.py": "Mixes PS2 positions into HD sec34 slots in SEQUENTIAL skin order",
    "mezclar_ps2_hd_v4.py": "Mixes PS2 positions into HD sec34 slots with the CORRECT pose transform",
    "mezclar_ps2_hd_v5.py": "Mixes PS2 (B3) positions into HD sec34 slots with the REAL layout",
    "mezclar_ps2_hd_v6.py": "Mixes PS2 (B3) positions into HD sec34 slots by WORLD COORDS",
    "parse_model.py": "Parser of #AMO0/#AMG (PS2) and #AWO/#AWG/#AZT (HD) models",
    "parse_ps2_mesh.py": "B3 PS2 mesh parser based on the budokai_updated.ms MaxScript",
    "phase_b_ab_compare.py": "Do A (block) and B (IB) describe the SAME mesh?",
    "phase_b_arms_dump.py": "Dump of the AWG0 arms (skinning)",
    "phase_b_census.py": "Phase B census of the AWG0 structures",
    "phase_b_consumer_scan.py": "Phase B scan of the buffer consumers",
    "phase_b_deep_scan.py": "Deep analysis of the pool <-> draw binding",
    "phase_b_desc_detail.py": "Detailed dump of the A/B descriptors and the IB",
    "phase_b_make_t2.py": "Phase B test T2",
    "phase_b_make_t3.py": "Test T3: reverse done right (Phase B / C1)",
    "phase_b_make_t4.py": "Test T4: permute ONLY inside each A block",
    "phase_b_make_t5.py": "Decisive test T5: reverse the sec34 pool WITHOUT touching the IB",
    "phase_b_make_t6.py": "Test T6: touch ONLY the A ranges (pool and IB intact)",
    "phase_b_make_t7.py": "Test T7: reverse the whole IB, pool INTACT",
    "phase_c_arms_targets.py": "Dump of the AWG0 arm targets",
    "phase_c_descriptors.py": "Map of ALL the 0x60 descriptor tables of the AWG",
    "phase_c_find_bonemap.py": "Looks for a position -> bone table in the AWG",
    "phase_c_make_t10.py": "Test T10: CONSISTENT relabeling in the vertex space",
    "phase_c_make_t11.py": "Test T11: permute the GPU vertex buffer WINDOWS",
    "phase_c_make_t8.py": "Test T8: permutation of WHOLE PARTS (identity)",
    "phase_c_make_t9.py": "Test T9: reorder single-bone RUNS inside an A block",
    "phase_c_meshgroup.py": "Dump of the mesh-group structure and the AWG0 arms",
    "phase_d_cmp_guest_ib.py": "Phase D: compares the guest IB with the bin's",
    "phase_d_descriptor_corr.py": "Phase D: descriptor correlation",
    "port_ps2_to_b3.py": "FULL port of a PS2 model (#AMO0) to a B3 HD bin (#AWO)",
    "ps2_rig_skin.py": "Extracts the skin (bone + weight per vertex) of a PS2 #AMO0",
    "ps2_to_hd_geometry.py": "Converts PS2 geometry to HD buffers (sec34/vb2/IB)",
    "RE_PS2_HD_MAPEO.py": "Side-by-side breakdown of AMG (PS2) vs AWG (HD)",
    "relayout_awg.py": "AWG0 re-layout: grows sec34 (main vertex buffer)",
    "relayout_sec34_remap.py": "sec34 re-layout (main buffer) with IB REMAP",
    "relayout_vb2.py": "AWG0 re-layout: grows vb2 (secondary buffer, +0x2C) for more slots",
    "render_bin_windows.py": "Front-view render of a bin's model to PNG",
    "scan_bones.py": "Scans the HD bins of the AFS and counts each character's bones",
    "skin_oracle.py": "Offline lint of the 'bone' field domain of a B3 HD #AMB bin",
    "space_probe.py": "Space hypothesis: positions written in MODEL space by the port pipeline",
    "stage_analyze.py": "Analyzes STAGE candidate bins of data_cmn.afs",
    "strip_order_winding.py": "Port stripifier that PRESERVES order and winding",
    "swap_cabeza.py": "Head swap Goku -> armored Vegeta (block rebuild)",
    "swap_cabeza_inplace.py": "Injects Goku's head into Vegeta WITHOUT moving offsets",
    "swap_cuerpo_hd.py": "Injects Goten's BODY geometry into the AWG0",
    "swap_cuerpo_hd_v2.py": "Transforms Goten's geometry into KLL space",
    "topology_check.py": "Checks the connectivity hypothesis (port keeps the native IB)",
    "trace_bone.py": "Bone hierarchy tracer inside a #AMG (PS2) or #AWG (HD)",
    "vbdump_info.py": "Analyzes dbz3_vbdump.bin (captured vertex buffers)",
    "vbdump_vs_bin.py": "Compares the captured VBs (dbz3_vbdump.bin) with the bin's windows",
}


class ArgSpec:
    __slots__ = ("flags", "dest", "positional", "action", "nargs", "type", "default", "default_txt", "choices",
                 "required", "help", "metavar", "const", "group")

    def __init__(self):
        self.flags, self.dest, self.positional, self.action, self.nargs = [], "", False, "store", None
        self.type, self.default, self.default_txt, self.choices, self.required = "str", None, "", None, False
        self.help, self.metavar, self.const, self.group = "", None, None, None

    @property
    def label(self):
        if self.positional:
            return self.metavar if isinstance(self.metavar, str) else self.dest
        longs = [f for f in self.flags if f.startswith("--")]
        return longs[0] if longs else self.flags[0]

    @property
    def flag(self):
        longs = [f for f in self.flags if f.startswith("--")]
        return longs[0] if longs else self.flags[0]


class ParserSpec:
    def __init__(self, desc=""):
        self.desc, self.args, self.sub = desc, [], None     # sub = {dest, required, parsers{name: (help, spec)}}


def _literal(node, consts, keys):
    try:
        return True, ast.literal_eval(node)
    except Exception:  # noqa: BLE001
        pass
    if isinstance(node, ast.Name):
        if node.id in consts:
            return True, consts[node.id]
        if node.id in keys:
            return True, list(keys[node.id])
    if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in ("list", "tuple", "sorted")
            and len(node.args) == 1):
        ok, v = _literal(node.args[0], consts, keys)
        if ok:
            try:
                return True, list(v)
            except TypeError:
                pass
    return False, None


def _unparse(node):
    fn = getattr(ast, "unparse", None)
    try:
        return fn(node) if fn else ""
    except Exception:  # noqa: BLE001
        return ""


def _type_name(node):
    if node is None:
        return "str"
    if isinstance(node, ast.Name):
        return {"int": "int", "float": "float"}.get(node.id, "str")
    src = _unparse(node)
    if isinstance(node, ast.Lambda) and "int(" in src:
        return "hex" if "16" in src else "int"
    if isinstance(node, ast.Lambda) and "float(" in src:
        return "float"
    return "str"


def _kw(call, name):
    for k in call.keywords:
        if k.arg == name:
            return k.value
    return None


def _make_arg(call, consts, keys, doc):
    flags = []
    for a in call.args:
        ok, v = _literal(a, consts, keys)
        if ok and isinstance(v, str):
            flags.append(v)
    if not flags:
        return None
    s = ArgSpec()
    s.flags = flags
    s.positional = not flags[0].startswith("-")
    act = _kw(call, "action")
    ok, v = _literal(act, consts, keys) if act is not None else (True, "store")
    s.action = v if ok and isinstance(v, str) else "store"
    if s.action in ("help", "version"):
        return None
    ok, v = _literal(_kw(call, "nargs"), consts, keys) if _kw(call, "nargs") is not None else (True, None)
    s.nargs = v if ok else "*"
    s.type = _type_name(_kw(call, "type"))
    dnode = _kw(call, "default")
    if dnode is not None:
        ok, v = _literal(dnode, consts, keys)
        if ok:
            s.default = v
            s.default_txt = "" if v is None or v == "" or v == [] else str(v)
        else:
            s.default_txt = _unparse(dnode)
    cnode = _kw(call, "choices")
    if cnode is not None:
        ok, v = _literal(cnode, consts, keys)
        if ok and v:
            s.choices = [str(x) for x in v]
    ok, v = _literal(_kw(call, "required"), consts, keys) if _kw(call, "required") is not None else (True, False)
    s.required = bool(v) if ok else False
    if s.positional:
        s.required = s.nargs not in ("?", "*")
    hnode = _kw(call, "help")
    if hnode is not None:
        ok, v = _literal(hnode, consts, keys)
        if ok and isinstance(v, str):
            s.help = v.replace("%(default)s", s.default_txt or "-").replace("%%", "%")
        elif "SUPPRESS" in _unparse(hnode):
            return None
    mnode = _kw(call, "metavar")
    if mnode is not None:
        ok, v = _literal(mnode, consts, keys)
        s.metavar = v if ok else None
    cn = _kw(call, "const")
    if cn is not None:
        ok, v = _literal(cn, consts, keys)
        s.const = v if ok else None
    dn = _kw(call, "dest")
    if dn is not None:
        ok, v = _literal(dn, consts, keys)
        s.dest = v if ok and isinstance(v, str) else ""
    if not s.dest:
        if s.positional:
            s.dest = flags[0]
        else:
            longs = [f for f in flags if f.startswith("--")]
            s.dest = (longs[0] if longs else flags[0]).lstrip("-").replace("-", "_")
    return s


def extract_argparse(tree, doc):
    """Reconstruye el arbol de argparse (parser, subcomandos, argumentos) leyendo el codigo con ast,
    SIN ejecutar la herramienta. Devuelve el ParserSpec raiz o None."""
    consts, keys = {}, {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            try:
                consts[name] = ast.literal_eval(node.value)
            except Exception:  # noqa: BLE001
                if isinstance(node.value, ast.Dict):
                    try:
                        keys[name] = [ast.literal_eval(k) for k in node.value.keys if k is not None]
                    except Exception:  # noqa: BLE001
                        pass
    nodes = [n for n in ast.walk(tree)
             if isinstance(n, (ast.Assign, ast.Expr)) and isinstance(getattr(n, "value", None), ast.Call)]
    nodes.sort(key=lambda n: (n.lineno, n.col_offset))
    objs, root, gid = {}, None, 0
    for node in nodes:
        call = node.value
        var = None
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            var = node.targets[0].id
        f = call.func
        meth = f.attr if isinstance(f, ast.Attribute) else (f.id if isinstance(f, ast.Name) else None)
        obj = objs.get(f.value.id) if isinstance(f, ast.Attribute) and isinstance(f.value, ast.Name) else None
        if meth == "ArgumentParser":
            dnode = _kw(call, "description")
            desc = ""
            if dnode is not None:
                ok, v = _literal(dnode, consts, keys)
                desc = v if ok and isinstance(v, str) else (doc.split("\n")[0] if "__doc__" in _unparse(dnode)
                                                              else "")
            p = ParserSpec(desc)
            if root is None:
                root = p
            if var:
                objs[var] = ("parser", p, None)
        elif meth == "add_subparsers" and obj and obj[0] == "parser":
            p = obj[1]
            dn = _kw(call, "dest")
            rq = _kw(call, "required")
            okd, dest = _literal(dn, consts, keys) if dn is not None else (True, None)
            okr, req = _literal(rq, consts, keys) if rq is not None else (True, False)
            p.sub = {"dest": dest if okd else None, "required": bool(req) if okr else False, "parsers": {}}
            if var:
                objs[var] = ("sub", p, None)
        elif meth == "add_parser" and obj and obj[0] == "sub" and call.args:
            ok, name = _literal(call.args[0], consts, keys)
            if not ok or not isinstance(name, str):
                continue
            hn = _kw(call, "help") or _kw(call, "description")
            okh, hv = _literal(hn, consts, keys) if hn is not None else (True, "")
            child = ParserSpec(hv if okh and isinstance(hv, str) else "")
            obj[1].sub["parsers"][name] = (child.desc, child)
            if var:
                objs[var] = ("parser", child, None)
        elif meth in ("add_argument_group", "add_mutually_exclusive_group") and obj and obj[0] in ("parser", "group"):
            g = None
            if meth == "add_mutually_exclusive_group":
                gid += 1
                rq = _kw(call, "required")
                ok, req = _literal(rq, consts, keys) if rq is not None else (True, False)
                g = (gid, bool(req) if ok else False)
            if var:
                objs[var] = ("group", obj[1], g or obj[2])
        elif meth == "add_argument" and obj and obj[0] in ("parser", "group"):
            a = _make_arg(call, consts, keys, doc)
            if a is not None:
                a.group = obj[2]
                obj[1].args.append(a)
    return root


def usage_text(doc):
    lines = (doc or "").splitlines()
    for i, ln in enumerate(lines):
        if re.match(r"\s*(uso|usage)\b", ln, re.I):
            block, blank = [], 0
            for x in lines[i:i + 24]:
                if not x.strip():
                    blank += 1
                    if blank >= 2:
                        break
                else:
                    blank = 0
                block.append(x)
            return "\n".join(block).strip()
    hits = [ln.strip() for ln in lines if "python " in ln]
    return "\n".join(hits[:8])


class ToolInfo:
    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        self.folder = os.path.basename(os.path.dirname(path))
        self.doc, self.usage, self.kind, self.spec, self.error = "", "", "argv", None, ""
        self.cat, self.desc_es, self.desc_en, self.fill = "experimental", "", "", {}

    @property
    def desc(self):
        if self.desc_es:
            return T(self.desc_es, self.desc_en)
        first = self.doc.split("\n")[0] if self.doc else ""
        return T(first, TOOL_EN.get(self.name, first))

    @property
    def research(self):
        return self.cat in RESEARCH_CATS


def inspect_tool(path):
    info = ToolInfo(path)
    try:
        with open(path, encoding="utf-8-sig", errors="replace") as fh:
            src = fh.read()
        tree = ast.parse(src)
    except (OSError, SyntaxError, ValueError) as ex:
        info.kind, info.error = "error", str(ex)
        return info
    info.doc = (ast.get_docstring(tree) or "").strip()
    info.usage = usage_text(info.doc)
    has_main = any(isinstance(n, ast.If) and "__main__" in _unparse(n.test) for n in tree.body)
    side = any(isinstance(n, (ast.For, ast.While, ast.With, ast.Try)) or
               (isinstance(n, ast.Expr) and isinstance(n.value, ast.Call)) for n in tree.body)
    spec = extract_argparse(tree, info.doc) if "argparse" in src else None
    if spec is not None and ("parse_args" in src or "parse_known_args" in src):
        info.kind, info.spec = "argparse", spec
    elif has_main:
        info.kind = "argv"
    elif side:
        info.kind = "script"
    else:
        info.kind = "library"
    cur = CURATED.get(info.name)
    if cur and info.folder in ("mod center hd", "awo_tools"):
        info.cat, info.desc_es, info.desc_en, info.fill = cur
    elif info.folder == "ports":
        info.cat = "ports"
    elif info.folder == "awo_tools":
        info.cat = "analisis"
    else:
        info.cat = "experimental"
    if info.kind == "library":
        info.cat = "biblioteca"
    return info


def scan_tools():
    out = []
    for d in (HERE, os.path.join(HERE, "studio"), os.path.join(HERE, "ports"), AWO):
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d), key=str.lower):
            if fn.endswith(".py") and fn != SELF_NAME and not fn.startswith("__"):
                out.append(inspect_tool(os.path.join(d, fn)))
    return out


# ================================================================================ mods
def read_manifest(d):
    out = {}
    try:
        with open(os.path.join(d, "manifest.txt"), encoding="utf-8", errors="replace") as fh:
            for ln in fh:
                ln = ln.strip()
                if not ln or ln.startswith("#") or "=" not in ln:
                    continue
                k, v = ln.split("=", 1)
                out[k.strip()] = v.strip()
    except OSError:
        pass
    return out


def toml_value(text, key):
    """TomlValue() del launcher: valor de 'clave = ...' (sin comillas)."""
    for ln in text.splitlines():
        if "=" not in ln:
            continue
        k, v = ln.split("=", 1)
        if k.strip() != key:
            continue
        v = v.split("#", 1)[0] if not v.strip().startswith('"') else v
        return v.strip().strip('"').strip()
    return ""


def read_toml(path):
    """dict del TOML (tomllib) o, sin tomllib, lo basico con el lector del launcher."""
    try:
        with open(path, "rb") as fh:
            raw = fh.read()
    except OSError:
        return {}
    if tomllib is not None:
        try:
            return tomllib.loads(raw.decode("utf-8", "replace"))
        except Exception:  # noqa: BLE001
            pass
    text = raw.decode("utf-8", "replace")
    sec = "personaje" if "[personaje]" in text else "traje"
    d = {}
    for k in ("nombre", "donante", "id", "despues_de", "voces", "gritos", "personaje", "camara", "formas"):
        v = toml_value(text.split("[[capsula]]")[0], k)
        if v:
            d[k] = int(v) if v.lstrip("-").isdigit() else v
    caps = []
    for block in text.split("[[capsula]]")[1:]:
        caps.append({"nombre": toml_value(block, "nombre") or "?", "tipo": toml_value(block, "tipo") or "especial",
                     "forma": int(toml_value(block, "forma") or 1)})
    return {sec: d, "capsula": caps}


def scan_mods(mods_dir):
    """Como dbz3::ListMods(): carpetas de mods/, activas si no tienen el marcador .disabled."""
    out = []
    try:
        names = sorted(os.listdir(mods_dir), key=str.lower)
    except OSError:
        return out
    for raw in names:
        p = os.path.join(mods_dir, raw)
        if not os.path.isdir(p):
            continue
        suffixed = raw.endswith(".disabled")
        name = raw[:-len(".disabled")] if suffixed else raw
        if suffixed and os.path.isdir(os.path.join(mods_dir, name)):
            continue
        m = read_manifest(p)
        info = {"name": name, "raw": raw, "dir": p,
                "enabled": not (suffixed or os.path.exists(os.path.join(p, ".disabled"))),
                "display": m.get("name", ""), "desc": m.get("description", ""), "author": m.get("author", ""),
                "version": m.get("version", ""), "type": m.get("type", ""), "source": m.get("source", ""),
                "toml": "", "icon": ""}
        for fn, kind in (("personaje.toml", "personaje"), ("traje.toml", "traje"), ("roster.toml", "generado")):
            tp = os.path.join(p, fn)
            if os.path.isfile(tp):
                if kind != "generado":
                    info["toml"] = tp
                    if not info["display"]:
                        data = read_toml(tp).get(kind, {})
                        info["display"] = str(data.get("nombre", "") or "")
                if not info["type"] or (kind != "generado" and info["type"] in ("data", "other")):
                    info["type"] = kind
                break
        if not info["type"]:
            if os.path.isfile(os.path.join(p, "textures", "textures_meta.json")):
                info["type"] = "texturas"
            else:
                info["type"] = infer_mod_type(p)
        icon = os.path.join(p, "ui", "_vista", "icono.png")
        info["icon"] = icon if os.path.isfile(icon) else ""
        out.append(info)
    out.sort(key=lambda m: (not m["enabled"], m["name"].lower()))
    return out


def infer_mod_type(d, limit=3000):
    has_data = has_audio = False
    n = 0
    for dp, _, files in os.walk(d):
        for fn in files:
            rel = os.path.relpath(os.path.join(dp, fn), d).lower()
            if "adx_" in rel and (".afs" in rel or ".adx" in rel):
                has_audio = True
            if "data_" in rel and ".afs" in rel:
                has_data = True
            n += 1
            if n > limit:
                break
        if n > limit:
            break
    return "audio" if has_audio else "data" if has_data else "other"


def mod_stats(d):
    count = size = 0
    for dp, _, files in os.walk(d):
        for fn in files:
            count += 1
            try:
                size += os.path.getsize(os.path.join(dp, fn))
            except OSError:
                pass
    return count, size


def human_size(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return ("%d %s" % (n, unit)) if unit == "B" else ("%.1f %s" % (n, unit))
        n /= 1024.0
    return str(n)


def set_mod_enabled(mods_dir, name, enable):
    """dbz3::SetModEnabled(): activar = quitar el marcador .disabled; desactivar = crearlo."""
    d = os.path.join(mods_dir, name)
    marker = os.path.join(d, ".disabled")
    if enable:
        if os.path.isfile(marker):
            os.remove(marker)      # el marcador solo dice "disabled": no contiene datos del mod
        suffixed = os.path.join(mods_dir, name + ".disabled")
        if not os.path.exists(d) and os.path.isdir(suffixed):
            os.rename(suffixed, d)
    else:
        os.makedirs(d, exist_ok=True)
        with open(marker, "w", encoding="utf-8") as fh:
            fh.write("disabled\n")


def backup_file(path, env):
    """Copia de seguridad antes de editar (modkit_backups/<fecha>/<ruta relativa a mods>).
    Cada copia queda anotada en modkit_backups/indice.tsv para poder deshacerla."""
    if not os.path.isfile(path):
        return ""
    try:
        rel = os.path.relpath(path, env.mods)
        if rel.startswith(".."):
            rel = os.path.basename(path)
    except ValueError:
        rel = os.path.basename(path)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    dst = os.path.join(env.backups, stamp, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(path, dst)
    anotar_indice(env, stamp, "copia", path, os.path.relpath(dst, env.backups))
    return dst


INDICE_COPIAS = "indice.tsv"


def anotar_indice(env, stamp, tipo, original, rel_copia):
    """Una linea por operacion: fecha, tipo («copia» o «deshecho»), fichero original, copia (tabuladas)."""
    os.makedirs(env.backups, exist_ok=True)
    with open(os.path.join(env.backups, INDICE_COPIAS), "a", encoding="utf-8") as fh:
        fh.write("\t".join((stamp, tipo, original, rel_copia)) + "\n")


def leer_indice(env):
    p = os.path.join(env.backups, INDICE_COPIAS)
    if not os.path.isfile(p):
        return []
    with open(p, encoding="utf-8") as fh:
        return [ln.rstrip("\n").split("\t") for ln in fh if ln.count("\t") == 3]


def deshacer_ultimo(env, donde=None):
    """Restaura el ultimo lote de copias que aun no se ha deshecho (con `donde` = carpeta de un mod: el ultimo lote
    que toco ese mod; KIT-10 «Deshacer» de cada fila de Mis mods). Lo que habia antes de deshacer se
    guarda en modkit_backups/<fecha>-antes-de-deshacer (nada se pierde). Devuelve los ficheros restaurados.
    Tipos del indice: «copia» (fichero editado), «nuevo» (carpeta creada por un asistente: se aparta) y
    «movido» (carpeta o fichero apartado por un asistente: vuelve a su sitio)."""
    filas = leer_indice(env)
    hechos = {f[0] for f in filas if f[1] == "deshecho"}
    lotes = [f for f in filas if f[1] in ("copia", "nuevo", "movido") and f[0] not in hechos]
    if donde:
        donde = os.path.normcase(os.path.normpath(donde))
        lotes = [f for f in lotes if any(os.path.normcase(os.path.normpath(x[2])) in (donde, donde + ".disabled") or
                                         os.path.normcase(x[2]).startswith(donde + os.sep) for x in lotes if x[0] == f[0])]
    if not lotes:
        return []
    stamp = lotes[-1][0]
    antes = os.path.join(env.backups, time.strftime("%Y%m%d-%H%M%S") + "-antes-de-deshacer")
    restaurados = []

    def apartar(path):
        os.makedirs(antes, exist_ok=True)
        os.replace(path, os.path.join(antes, "%d_%s" % (len(os.listdir(antes)), os.path.basename(path))))

    mias = [f for f in lotes if f[0] == stamp]
    for _, tipo, original, _rel in mias:          # lo que creo el asistente, primero (libera el sitio)
        if tipo == "nuevo" and os.path.exists(original):
            apartar(original)
            restaurados.append(original)
    for _, tipo, original, rel in mias:
        origen = os.path.join(env.backups, rel)
        if tipo == "movido" and os.path.exists(origen):
            if os.path.exists(original):
                apartar(original)
            os.replace(origen, original)
            restaurados.append(original)
        elif tipo == "copia" and os.path.isfile(origen):
            if os.path.isfile(original):
                os.makedirs(antes, exist_ok=True)
                shutil.copy2(original, os.path.join(antes, "%d_%s" % (len(restaurados), os.path.basename(original))))
            shutil.copy2(origen, original)
            restaurados.append(original)
    anotar_indice(env, stamp, "deshecho", "", "")
    return restaurados


def toml_literal(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    return '"%s"' % str(v).replace("\\", "\\\\").replace('"', '\\"')


# colores de aura / ki del Mod Kit: (valor en personaje.toml = nombre de colores.NOMBRES, es, en, tono)
COLOR_PRESETS = [("rojo", "Rojo", "Red", 0), ("naranja", "Naranja", "Orange", 28), ("dorado", "Dorado", "Gold", 45),
                 ("amarillo", "Amarillo", "Yellow", 55), ("lima", "Lima", "Lime", 90),
                 ("verde", "Verde", "Green", 120), ("turquesa", "Turquesa", "Teal", 165),
                 ("cian", "Cian", "Cyan", 185), ("azul", "Azul", "Blue", 225), ("violeta", "Violeta", "Violet", 265),
                 ("morado", "Morado", "Purple", 280), ("magenta", "Magenta", "Magenta", 300),
                 ("rosa", "Rosa", "Pink", 325)]


def toml_set_keys(path, section, values):
    """Como roster_build.set_toml_keys(): escribe/actualiza 'clave = valor' en [seccion]
    (valor en sintaxis TOML; None quita la clave). Conserva comentarios y el resto."""
    with open(path, encoding="utf-8") as fh:
        lines = fh.read().splitlines()
    sec, end = None, len(lines)
    for i, ln in enumerate(lines):
        s = ln.strip()
        if s.startswith("["):
            if s == "[%s]" % section:
                sec = i
            elif sec is not None and end == len(lines):
                end = i
    if sec is None:
        lines.insert(0, "[%s]" % section)
        sec, end = 0, end + 1
    for k, v in values.items():
        hit = None
        for i in range(sec + 1, end):
            if lines[i].split("=")[0].strip() == k:
                hit = i
                break
        if v is None:
            if hit is not None:
                del lines[hit]
                end -= 1
        elif hit is not None:
            old = lines[hit]
            comment = ""
            m = re.search(r"\s+#[^\"]*$", old)
            if m:
                comment = "  " + m.group(0).strip()
            lines[hit] = "%s = %s%s" % (k, v, comment)
        else:
            lines.insert(end, "%s = %s" % (k, v))
            end += 1
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")


def assign_slots(chars):
    """LauncherDialog::AssignSlots(): primero las plazas pedidas (orden de carpeta), luego la primera
    libre. chars: [(carpeta, activo, id_pedido)] ordenados por carpeta."""
    out, used = {}, []
    for folder, on, req in chars:
        if not on:
            continue
        for sid, _ in FREE_SLOTS:
            if sid == req and sid not in used:
                out[folder] = sid
                used.append(sid)
    for folder, on, req in chars:
        if not on or folder in out:
            continue
        out[folder] = -1
        for sid, _ in FREE_SLOTS:
            if sid not in used:
                out[folder] = sid
                used.append(sid)
                break
    return out


def load_catalog(env):
    """catalog_b3.cat: bin|nombre|label|variante|jugable|nota (ModPipeline::LoadCatalog)."""
    out = []
    try:
        with open(env.script("catalog_b3.cat"), encoding="utf-8", errors="replace") as fh:
            for ln in fh:
                ln = ln.rstrip("\r\n")
                if not ln or ln.startswith("#"):
                    continue
                parts = ln.split("|")
                if len(parts) < 3:
                    continue
                try:
                    b = int(parts[0] or 0)
                except ValueError:
                    continue
                out.append({"bin": b, "name": parts[1], "label": parts[2],
                            "variant": parts[3] if len(parts) > 3 else "",
                            "playable": (parts[4].strip() not in ("", "0")) if len(parts) > 4 else True,
                            "note": parts[5] if len(parts) > 5 else ""})
    except OSError:
        pass
    return out


def char_label(c):
    s = c["name"] + (" (%s)" % c["variant"] if c["variant"] else "") + ("   [bin %d]" % c["bin"] if ADV_LABELS else "")
    if not c["playable"]:
        s += "   · " + T("no jugable", "not playable")
    return s


def check_modules(python, names=("numpy", "PIL", "scipy")):
    if norm(os.path.dirname(python)) == norm(os.path.dirname(sys.executable)):
        import importlib.util
        return {n: importlib.util.find_spec(n) is not None for n in names}
    code = "import importlib.util as u; print(','.join('1' if u.find_spec(n) else '0' for n in %r))" % (list(names),)
    try:
        r = subprocess.run([python, "-c", code], capture_output=True, text=True, timeout=20,
                           creationflags=NO_WINDOW)
        flags = r.stdout.strip().split(",")
        return {n: f == "1" for n, f in zip(names, flags)}
    except (OSError, subprocess.SubprocessError):
        return {n: False for n in names}


def python_version(python):
    if norm(os.path.dirname(python)) == norm(os.path.dirname(sys.executable)):
        return sys.version_info[:3]
    try:
        r = subprocess.run([python, "-c", "import sys; print('%d.%d.%d' % sys.version_info[:3])"],
                           capture_output=True, text=True, timeout=20, creationflags=NO_WINDOW)
        return tuple(int(x) for x in r.stdout.strip().split("."))
    except (OSError, ValueError, subprocess.SubprocessError):
        return (0, 0, 0)


# ================================================================================ estilo
def init_fonts(root):
    fams = {f.lower() for f in tkfont.families(root)}
    ui = "Segoe UI" if "segoe ui" in fams else "Helvetica"
    semi = "Segoe UI Semibold" if "segoe ui semibold" in fams else ui
    mono = next((m for m in ("Cascadia Mono", "Cascadia Code", "Consolas", "Courier New") if m.lower() in fams),
                "Courier")
    icon = next((m for m in ("Segoe Fluent Icons", "Segoe MDL2 Assets") if m.lower() in fams), None)
    banner = "Comic Sans MS" if "comic sans ms" in fams else semi
    F.update(body=(ui, 10), small=(ui, 9), bold=(semi, 10), bold_sm=(semi, 9), h1=(semi, 20), h2=(semi, 15),
             h3=(semi, 12), mono=(mono, 9), banner=(banner, 17, "bold"), big=(semi, 11),
             icon=(icon, 22) if icon else (ui, 20), icon_sm=(icon, 11) if icon else (ui, 11))
    F["has_icons"] = bool(icon)


ICONS = {  # (Segoe MDL2 / Fluent, alternativa)
    "setup": ("\uE9D9", "⚙"), "mods": ("\uE8F1", "▤"), "importer": ("\uE896", "⇩"),
    "characters": ("\uE716", "☺"), "create": ("\uE8FA", "✚"), "textures": ("\uEB9F", "✎"),
    "swap": ("\uE8AB", "⇄"), "help": ("\uE897", "?"), "tools": ("\uE90F", "⚒"), "home": ("\uE80F", "⌂"),
    "w_personaje": ("", "✚"), "w_aspecto": ("", "✎"), "w_traje": ("\uE790", "♟"),
    "w_zip": ("", "⇩"), "w_musica": ("", "♪"),
    "obra_tecnica": ("\uE945", "⚡"), "obra_escenario": ("\uE909", "◍"), "obra_fusiones": ("\uE902", "☯"),
    "obra_probar": ("\uE768", "▶"), "arte": ("\uE91B", "▣"), "biblioteca": ("\uE8F1", "▤"),
    "diag": ("\uE9D2", "♥"), "studio": ("\uE714", "◉"), "hex": ("\uE943", "#"), "stories": ("\uE8A5", "\u00B6"),
}


def icon(key):
    a, b = ICONS.get(key, ("", "•"))
    return a if F.get("has_icons") else b


def setup_style(root):
    st = ttk.Style(root)
    try:
        st.theme_use("clam")
    except tk.TclError:
        pass
    st.configure(".", background=C["bg"], foreground=C["text"], fieldbackground=C["frame"], bordercolor=C["line"],
                 lightcolor=C["card"], darkcolor=C["card"], troughcolor=C["bg"], focuscolor=C["accent"],
                 selectbackground=C["accent_dim"], selectforeground=C["text"], insertcolor=C["text"],
                 font=F["body"])
    st.configure("TFrame", background=C["bg"])
    st.configure("TLabel", background=C["bg"], foreground=C["text"])
    st.configure("TButton", background=C["frame"], foreground=C["text"], bordercolor=C["line"],
                 lightcolor=C["frame"], darkcolor=C["frame"], padding=(12, 6), relief="flat", focusthickness=0,
                 font=F["bold"])
    st.map("TButton", background=[("disabled", C["card"]), ("pressed", C["active"]), ("active", C["hover"])],
           foreground=[("disabled", C["dim"])], bordercolor=[("focus", C["accent_dim"])])
    st.configure("Accent.TButton", background=C["accent"], foreground=C["on_accent"], bordercolor=C["accent"],
                 lightcolor=C["accent"], darkcolor=C["accent"])
    st.map("Accent.TButton", background=[("disabled", C["accent_soft"]), ("pressed", C["accent_dim"]),
                                         ("active", C["accent_hi"])],
           foreground=[("disabled", C["dim"])], bordercolor=[("disabled", C["accent_soft"])])
    st.configure("Big.Accent.TButton", padding=(26, 11), font=F["big"])
    st.configure("Blue.TButton", background=C["blue_dim"], foreground=C["text"], bordercolor=C["blue_dim"],
                 lightcolor=C["blue_dim"], darkcolor=C["blue_dim"])
    st.map("Blue.TButton", background=[("disabled", C["card"]), ("pressed", C["blue_dim"]), ("active", "#31507E")])
    st.configure("Ghost.TButton", background=C["bg"], foreground=C["blue"], bordercolor=C["bg"], lightcolor=C["bg"],
                 darkcolor=C["bg"], padding=(6, 4))
    st.map("Ghost.TButton", background=[("active", C["card"])], foreground=[("disabled", C["dim"])])
    st.configure("Small.TButton", padding=(8, 3), font=F["bold_sm"])
    st.configure("Danger.TButton", background="#4A2222", foreground=C["text"], bordercolor="#4A2222",
                 lightcolor="#4A2222", darkcolor="#4A2222", padding=(10, 3), font=F["bold_sm"])
    st.map("Danger.TButton", background=[("active", "#6A2C2C")])
    st.configure("TEntry", fieldbackground=C["frame"], foreground=C["text"], insertcolor=C["text"],
                 bordercolor=C["line"], lightcolor=C["frame"], darkcolor=C["frame"], padding=5)
    st.map("TEntry", bordercolor=[("focus", C["accent"])], lightcolor=[("focus", C["accent"])],
           fieldbackground=[("readonly", C["card_hi"]), ("disabled", C["card"])], foreground=[("disabled", C["dim"])])
    st.configure("TCombobox", fieldbackground=C["frame"], background=C["frame"], foreground=C["text"],
                 arrowcolor=C["text"], bordercolor=C["line"], lightcolor=C["frame"], darkcolor=C["frame"], padding=4)
    st.map("TCombobox", fieldbackground=[("readonly", C["frame"]), ("disabled", C["card"])],
           selectbackground=[("readonly", C["frame"])], selectforeground=[("readonly", C["text"])],
           foreground=[("disabled", C["dim"])], bordercolor=[("focus", C["accent"])],
           background=[("active", C["hover"])], arrowcolor=[("disabled", C["dim"])])
    st.configure("TSpinbox", fieldbackground=C["frame"], background=C["frame"], foreground=C["text"],
                 arrowcolor=C["text"], bordercolor=C["line"], lightcolor=C["frame"], darkcolor=C["frame"], padding=4)
    st.map("TSpinbox", bordercolor=[("focus", C["accent"])])
    for name, bg in (("TCheckbutton", C["bg"]), ("Card.TCheckbutton", C["card"]),
                     ("TRadiobutton", C["bg"]), ("Card.TRadiobutton", C["card"])):
        st.configure(name, background=bg, foreground=C["text"], indicatorbackground=C["frame"],
                     indicatorforeground=C["on_accent"], upperbordercolor=C["line"], lowerbordercolor=C["line"],
                     focuscolor=bg)
        st.map(name, background=[("active", bg)], indicatorbackground=[("selected", C["accent"]),
                                                                        ("active", C["hover"])],
               foreground=[("disabled", C["dim"])])
    st.configure("TNotebook", background=C["bg"], borderwidth=0, tabmargins=(16, 8, 16, 0))
    st.configure("TNotebook.Tab", background=C["card"], foreground=C["dim"], padding=(14, 7), borderwidth=0,
                 lightcolor=C["card"], bordercolor=C["bg"], font=F["bold"])
    st.map("TNotebook.Tab", background=[("selected", C["card_hi"]), ("active", C["hover"])],
           foreground=[("selected", C["accent"]), ("active", C["text"])],
           lightcolor=[("selected", C["accent"])])
    st.configure("Treeview", background=C["card"], fieldbackground=C["card"], foreground=C["text"],
                 bordercolor=C["line"], lightcolor=C["card"], darkcolor=C["card"], rowheight=28)
    st.map("Treeview", background=[("selected", C["accent_soft"])], foreground=[("selected", C["text"])])
    st.configure("Icons.Treeview", rowheight=48)
    st.configure("Treeview.Heading", background=C["card_hi"], foreground=C["dim"], relief="flat",
                 bordercolor=C["line"], lightcolor=C["card_hi"], darkcolor=C["card_hi"], font=F["bold_sm"],
                 padding=(8, 5))
    st.map("Treeview.Heading", background=[("active", C["hover"])])
    for o in ("Vertical", "Horizontal"):
        st.configure("%s.TScrollbar" % o, background=C["frame"], troughcolor=C["bg"], bordercolor=C["bg"],
                     arrowcolor=C["dim"], lightcolor=C["frame"], darkcolor=C["frame"], gripcount=0, arrowsize=12)
        st.map("%s.TScrollbar" % o, background=[("active", C["hover"])])
    st.configure("TProgressbar", background=C["accent"], troughcolor=C["frame"], bordercolor=C["frame"],
                 lightcolor=C["accent"], darkcolor=C["accent"])
    st.configure("TPanedwindow", background=C["bg"])
    st.configure("Sash", sashthickness=6, gripcount=0, background=C["line"])
    st.configure("TSeparator", background=C["line"])
    root.option_add("*TCombobox*Listbox.background", C["frame"])
    root.option_add("*TCombobox*Listbox.foreground", C["text"])
    root.option_add("*TCombobox*Listbox.selectBackground", C["accent"])
    root.option_add("*TCombobox*Listbox.selectForeground", C["on_accent"])
    root.option_add("*TCombobox*Listbox.font", F["body"])
    root.option_add("*Menu.background", C["card_hi"])
    root.option_add("*Menu.foreground", C["text"])
    root.option_add("*Menu.activeBackground", C["accent"])
    root.option_add("*Menu.activeForeground", C["on_accent"])
    return st


def dragonball_image(size, bg=None):
    """Icono dibujado (bola de dragon de 4 estrellas), sin archivos externos."""
    import math
    img = tk.PhotoImage(width=size, height=size)
    cx = cy = (size - 1) / 2.0
    r = size / 2.0 - 0.6
    light, dark, hi = (255, 196, 92), (214, 104, 12), (255, 241, 205)
    stars = [(-0.22, -0.18), (0.22, -0.16), (-0.1, 0.24), (0.2, 0.22)]
    sr = max(1.6, size * 0.11)
    rows = []
    trans = []
    for y in range(size):
        row = []
        for x in range(size):
            dx, dy = x - cx, y - cy
            d = math.hypot(dx, dy)
            if d > r:
                row.append(bg or "#111318")
                trans.append((x, y))
                continue
            t = min(1.0, d / r) ** 1.6
            col = [light[i] + (dark[i] - light[i]) * t for i in range(3)]
            hd = math.hypot(dx + r * 0.38, dy + r * 0.42) / r          # brillo arriba-izquierda
            if hd < 0.32:
                k = (0.32 - hd) / 0.32
                col = [col[i] + (hi[i] - col[i]) * k * 0.9 for i in range(3)]
            for sx, sy in stars:                                        # estrellas rojas
                px, py = dx - sx * size, dy - sy * size
                ang = math.atan2(py, px) + math.pi / 2
                seg = (ang % (2 * math.pi / 5)) / (2 * math.pi / 5)
                lim = sr * (0.45 + 0.55 * abs(2 * seg - 1))
                if math.hypot(px, py) < lim:
                    col = [222, 36, 28]
            if d > r - 1.0:
                col = [c * 0.7 for c in col]
            row.append("#%02x%02x%02x" % tuple(int(max(0, min(255, c))) for c in col))
        rows.append("{" + " ".join(row) + "}")
    img.put(" ".join(rows))
    if bg is None and hasattr(img, "transparency_set"):
        for x, y in trans:
            img.transparency_set(x, y, True)
    return img


# ================================================================================ widgets
class ScrollFrame(tk.Frame):
    """Marco con barra de desplazamiento (la rueda del raton la gestiona App._wheel)."""

    def __init__(self, master, bg=None, **kw):
        bg = bg or C["bg"]
        super().__init__(master, bg=bg, **kw)
        self.canvas = tk.Canvas(self, bg=bg, highlightthickness=0, bd=0)
        self.vsb = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.inner = tk.Frame(self.canvas, bg=bg)
        self._win = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.canvas.configure(yscrollcommand=self.vsb.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        self.vsb.pack(side="right", fill="y")
        self.inner.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>", lambda e: self.canvas.itemconfigure(self._win, width=e.width))
        self.canvas._scrollframe = self
        self.inner._scrollframe = self

    def scroll(self, delta):
        if self.inner.winfo_reqheight() > self.canvas.winfo_height():
            step = -1 if delta > 0 else 1
            self.canvas.yview_scroll(step * max(1, abs(int(delta / 120))) * 3, "units")

    def top(self):
        self.canvas.yview_moveto(0)


class Tooltip:
    def __init__(self, widget, text):
        self.w, self.text, self.tip, self.job = widget, text, None, None
        widget._con_tip = True     # kit/ayudas.py no le pone otro
        widget.bind("<Enter>", self._sched, add="+")
        widget.bind("<Leave>", self._hide, add="+")
        widget.bind("<ButtonPress>", self._hide, add="+")

    def _sched(self, _e=None):
        self._cancel()
        self.job = self.w.after(500, self._show)

    def _cancel(self):
        if self.job:
            try:
                self.w.after_cancel(self.job)
            except tk.TclError:
                pass
            self.job = None

    def _show(self):
        if self.tip or not self.text:
            return
        try:
            x = self.w.winfo_rootx() + 14
            y = self.w.winfo_rooty() + self.w.winfo_height() + 6
            self.tip = tk.Toplevel(self.w)
            self.tip.wm_overrideredirect(True)
            self.tip.configure(bg=C["line"])
            tk.Label(self.tip, text=self.text, bg=C["card_hi"], fg=C["text"], font=F["small"], justify="left",
                     wraplength=440, padx=10, pady=6).pack(padx=1, pady=1)
            self.tip.wm_geometry("+%d+%d" % (x, y))
        except tk.TclError:
            self.tip = None

    def _hide(self, _e=None):
        self._cancel()
        if self.tip:
            try:
                self.tip.destroy()
            except tk.TclError:
                pass
            self.tip = None


class Segmented(tk.Frame):
    def __init__(self, master, options, value, command):
        super().__init__(master, bg=C["frame"], highlightthickness=1, highlightbackground=C["line"])
        self.cmd, self.labels, self.value = command, {}, value
        for val, text in options:
            lb = tk.Label(self, text=text, font=F["bold_sm"], padx=14, pady=5, cursor="hand2")
            lb.pack(side="left")
            lb.bind("<Button-1>", lambda _e, v=val: self.set(v, True))
            self.labels[val] = lb
        self.set(value)

    def set(self, val, fire=False):
        changed = val != self.value
        self.value = val
        for v, lb in self.labels.items():
            on = v == val
            lb.configure(bg=C["accent"] if on else C["frame"], fg=C["on_accent"] if on else C["dim"])
        if fire and changed and self.cmd:
            self.cmd(val)


class Card(tk.Frame):
    def __init__(self, master, title=None, subtitle=None, pad=16, accent=False):
        super().__init__(master, bg=C["card"], highlightthickness=1,
                         highlightbackground=C["accent_dim"] if accent else C["line"], highlightcolor=C["line"])
        if title:
            h = tk.Frame(self, bg=C["card"])
            h.pack(fill="x", padx=pad, pady=(pad - 4, 6))
            tk.Label(h, text=title, bg=C["card"], fg=C["accent"] if accent else C["text"], font=F["h3"],
                     anchor="w").pack(fill="x")
            if subtitle:
                tk.Label(h, text=subtitle, bg=C["card"], fg=C["dim"], font=F["small"], anchor="w", justify="left",
                         wraplength=900).pack(fill="x", pady=(2, 0))
        self.body = tk.Frame(self, bg=C["card"])
        self.body.pack(fill="both", expand=True, padx=pad, pady=(0 if title else pad, pad))


def label(parent, text, fg=None, font=None, bg=None, wrap=0, **kw):
    bg = bg or parent.cget("bg")
    return tk.Label(parent, text=text, fg=fg or C["text"], bg=bg, font=font or F["body"], anchor="w",
                    justify="left", wraplength=wrap, **kw)


def hint(parent, text, wrap=640, bg=None):
    return label(parent, text, fg=C["dim"], font=F["small"], bg=bg, wrap=wrap)


def make_clickable(frame, command, normal_bg, hover_bg):
    """Tarjeta entera clicable con efecto al pasar el raton."""
    def widgets(w):
        yield w
        for ch in w.winfo_children():
            yield from widgets(ch)

    def paint(bg, border):
        for w in widgets(frame):
            try:
                if not isinstance(w, ttk.Widget):
                    w.configure(bg=bg)
            except tk.TclError:
                pass
        frame.configure(highlightbackground=border)

    def enter(_e):
        try:
            paint(hover_bg, C["accent"])
        except tk.TclError:
            pass

    def leave(_e):
        try:
            x, y = frame.winfo_pointerxy()
            w = frame.winfo_containing(x, y)
            if w is not None and str(w).startswith(str(frame)):
                return
            paint(normal_bg, C["line"])
        except (tk.TclError, KeyError):
            pass

    for w in widgets(frame):
        w.bind("<Enter>", enter, add="+")
        w.bind("<Leave>", leave, add="+")
        w.bind("<Button-1>", lambda _e: command(), add="+")
        try:
            w.configure(cursor="hand2")
        except tk.TclError:
            pass


class PathEntry(tk.Frame):
    def __init__(self, master, var, mode="file", filetypes=None, width=48, bg=None):
        super().__init__(master, bg=bg or master.cget("bg"))
        self.var, self.mode, self.filetypes = var, mode, filetypes
        self.entry = ttk.Entry(self, textvariable=var, width=width)
        self.entry.pack(side="left", fill="x", expand=True)
        b = ttk.Button(self, text="…", width=3, style="Small.TButton", command=self.browse)
        b.pack(side="left", padx=(4, 0))
        Tooltip(b, {"dir": T("Elegir carpeta", "Choose folder"), "save": T("Guardar como", "Save as")}.get(
            mode, T("Elegir archivo (clic derecho: carpeta)", "Choose file (right click: folder)")))
        if mode == "any":
            b.bind("<Button-3>", lambda _e: self.browse(force_dir=True))

    def _initial(self):
        cur = self.var.get().strip()
        if cur and os.path.isdir(cur):
            return cur
        if cur and os.path.isdir(os.path.dirname(cur)):
            return os.path.dirname(cur)
        return None

    def browse(self, force_dir=False):
        init = self._initial()
        if self.mode == "dir" or force_dir:
            p = filedialog.askdirectory(initialdir=init, mustexist=False)
        elif self.mode == "save":
            p = filedialog.asksaveasfilename(initialdir=init)
        else:
            p = filedialog.askopenfilename(initialdir=init, filetypes=self.filetypes or [(T("Todos", "All"), "*.*")])
        if p:
            self.var.set(os.path.normpath(p))


class BannerPreview(tk.Canvas):
    """Rotulo del select como se vera (blanco con contorno negro, como LiveBanner del launcher)."""

    def __init__(self, master, width=300, height=40):
        super().__init__(master, width=width, height=height, bg=master.cget("bg"), highlightthickness=0)
        self.h = height

    def set(self, text):
        self.delete("all")
        if not text:
            return
        y = self.h // 2
        for dx in (-2, -1, 0, 1, 2):
            for dy in (-2, -1, 0, 1, 2):
                if dx or dy:
                    self.create_text(6 + dx, y + dy, text=text, anchor="w", fill="#000000", font=F["banner"])
        self.create_text(6, y, text=text, anchor="w", fill="#FFFFFF", font=F["banner"])


class CharPicker(tk.Frame):
    """Desplegable de personajes del catalogo con busqueda al escribir."""

    def __init__(self, master, chars, none_label=None, width=52, on_change=None):
        super().__init__(master, bg=master.cget("bg"))
        self.items = [(None, none_label)] if none_label else []
        seen = {}
        for c in chars:                    # sin el numero interno (modo sencillo) un nombre puede repetirse
            lb = char_label(c)
            seen[lb] = seen.get(lb, 0) + 1
            self.items.append((c, lb if seen[lb] == 1 else "%s (%d)" % (lb, seen[lb])))
        self.var = tk.StringVar(value=none_label or "")
        self.cb = ttk.Combobox(self, textvariable=self.var, values=[lb for _, lb in self.items], width=width,
                               height=18)
        self.cb.pack(fill="x")
        self.on_change = on_change
        self.cb.bind("<KeyRelease>", self._filter)
        self.cb.bind("<<ComboboxSelected>>", lambda _e: self._changed())
        self.cb.bind("<FocusOut>", lambda _e: self._changed())

    def _filter(self, e):
        if e.keysym in ("Up", "Down", "Return", "Escape", "Tab"):
            return
        t = self.var.get().lower().strip()
        vals = [lb for _, lb in self.items if t in lb.lower()] if t else [lb for _, lb in self.items]
        self.cb.configure(values=vals or [lb for _, lb in self.items])

    def _changed(self):
        if self.on_change:
            self.on_change()

    def get(self):
        cur = self.var.get()
        for c, lb in self.items:
            if lb == cur:
                return c
        return None

    def set_char(self, c):
        for cc, lb in self.items:
            if cc is c or (cc and c and cc["bin"] == c["bin"]):
                self.var.set(lb)
                return


def slot_label(sid, original, owner=""):
    """Plaza por su nombre (el numero interno solo en modo avanzado)."""
    name = ("%d (%s)" % (sid, original)) if ADV_LABELS else original
    if owner:
        return T("%s - ocupada por %s", "%s - taken by %s") % (name, owner)
    return T("%s - libre", "%s - free") % name


def slot_values():
    return [("%d (%s)" % (s, o)) if ADV_LABELS else o for s, o in FREE_SLOTS]


def donor_values(auto_label=None):
    """Personajes base por su nombre (con su ID solo en modo avanzado). Las listas se leen por
    posicion (donor_from_index) o con donor_id()."""
    vals = [auto_label] if auto_label else []
    return vals + [("%s  (ID %d)" % (T(es, en), i)) if ADV_LABELS else T(es, en) for i, es, en in DONORS]


def donor_id(label):
    """ID del personaje base de una etiqueta de donor_values() (None si no es ninguna)."""
    vals = donor_values()
    return DONORS[vals.index(label)][0] if label in vals else None


def donor_from_index(idx, has_auto):
    if has_auto:
        idx -= 1
    return DONORS[idx][0] if 0 <= idx < len(DONORS) else -1


def donor_index(did, has_auto):
    for k, (i, _, _) in enumerate(DONORS):
        if i == did:
            return k + (1 if has_auto else 0)
    return 0


def load_png(path, max_w=None, max_h=None):
    """PhotoImage de un PNG (Tk 8.6 lee PNG con alfa), reducido por un factor entero si hace falta."""
    if not path or not os.path.isfile(path):
        return None
    try:
        img = tk.PhotoImage(file=path)
    except tk.TclError:
        return None
    w, h = img.width(), img.height()
    f = 1
    while (max_w and w / f > max_w) or (max_h and h / f > max_h):
        f += 1
    return img.subsample(f, f) if f > 1 else img


# ================================================================================ paginas
class Page(tk.Frame):
    key = ""

    def __init__(self, master, app, adv):
        super().__init__(master, bg=C["bg"])
        self.app, self.adv = app, adv
        self.alive = True
        self.bind("<Destroy>", self._destroyed, add="+")
        self.build()

    @property
    def env(self):
        return self.app.env

    def _destroyed(self, e):
        if e.widget is self:
            self.alive = False

    def build(self):
        pass

    def on_show(self):
        pass

    def header(self, title, subtitle=""):
        h = tk.Frame(self, bg=C["bg"])
        h.pack(fill="x", padx=22, pady=(12, 8))
        if not self.adv:
            ttk.Button(h, text="←  " + T("Inicio", "Home"), style="Ghost.TButton",
                       command=lambda: self.app.goto("home")).pack(side="left", anchor="n", padx=(0, 14))
        tf = tk.Frame(h, bg=C["bg"])
        tf.pack(side="left", fill="x", expand=True)
        label(tf, title, font=F["h2"] if self.adv else F["h1"]).pack(fill="x")
        if subtitle:
            sub = label(tf, subtitle, fg=C["dim"], wrap=620)
            sub.pack(fill="x", pady=(2, 0))
            tf.bind("<Configure>", lambda e: sub.configure(wraplength=max(320, e.width - 10)), add="+")
        self.header_right = tk.Frame(h, bg=C["bg"])
        self.header_right.pack(side="right", anchor="n")
        if self.key in ayudas.COMO:
            ttk.Button(self.header_right, text="?  " + T("¿Cómo se hace?", "How is it done?"), style="Small.TButton",
                       command=lambda: ayudas.abrir_como(self, G)).pack(side="right")
        return h

    def scroll_body(self):
        sf = ScrollFrame(self)
        sf.pack(fill="both", expand=True, padx=(22, 8), pady=(0, 10))
        return sf, sf.inner

    def status_label(self, parent):
        return tk.Label(parent, text="", bg=parent.cget("bg"), fg=C["dim"], font=F["body"], anchor="w",
                        justify="left", wraplength=760)

    def set_status(self, lbl, level, text):
        if lbl is None or not self.alive:
            return
        try:
            lbl.configure(text=text, fg={"ok": C["ok"], "err": C["err"], "warn": C["warn"]}.get(level, C["dim"]))
        except tk.TclError:
            pass

    def run(self, cmd, title, done=None, status=None, ok_text=None, quiet=False, cwd=None):
        def _done(job):
            if status is not None:
                lvl, txt = self.app.outcome(job, ok_text)
                self.set_status(status, lvl, txt)
            if done is not None:
                done(job)
        started = self.app.run(cmd, title, _done, quiet=quiet, cwd=cwd)
        if started and status is not None:
            self.set_status(status, "warn", T("Trabajando…  (el detalle sale en el registro de abajo)",
                                              "Working…  (details in the log below)"))
        return started


# --------------------------------------------------------------------------------- inicio
PAGE_DEFS = portada.PAGE_DEFS


G = None

__all__ = [n for n in list(globals()) if not n.startswith("_") and n not in ("LANG", "ADV_LABELS", "G", "annotations")]
