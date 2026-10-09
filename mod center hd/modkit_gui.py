#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""modkit_gui.py - DBZ3 HD Mod Kit: ventana para TODAS las herramientas de modding, sin consola.

Modo BASICO: tarjetas guiadas con pocos botones grandes y texto sencillo.
Modo AVANZADO: pestanas con todas las opciones de cada herramienta, registro y rutas.

Llama a las mismas herramientas Python, con los mismos argumentos, que el launcher del juego
(src/launcher/mod_pipeline.cpp + launcher_state.cpp), asi que el resultado es identico.

  python modkit_gui.py                abre la ventana (o doble clic en DBZ3_ModKit.bat)
  python modkit_gui.py --lang en      en ingles
  python modkit_gui.py --advanced     directamente en modo avanzado
  python modkit_gui.py --selftest     construye todas las pantallas SIN mostrarlas, prueba el
                                      cableado (importar.py fuentes) y sale (codigo 0 = OK)

Solo usa la biblioteca estandar (tkinter). Las herramientas que lanza necesitan numpy, Pillow y
scipy (instalar_requisitos.bat).
"""
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

HERE = os.path.dirname(os.path.abspath(__file__))          # .../mod center hd
KIT = os.path.dirname(HERE)                                  # carpeta del kit (junto a dbz3.exe)
AWO = os.path.join(KIT, "awo_tools")
IS_WIN = os.name == "nt"
NO_WINDOW = 0x08000000 if IS_WIN else 0                      # CREATE_NO_WINDOW
SELF_NAME = os.path.basename(os.path.abspath(__file__))

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
    "b2": ("Modelo de Budokai 2; golpes, técnicas y definitiva de su equivalente en Budokai 3.",
           "Budokai 2 model; moves, techniques and ultimate from its Budokai 3 counterpart."),
    "b3": ("Modelos de la comunidad (.amb / .amo + .amt) de 'modding resources'.",
           "Community models (.amb / .amo + .amt) from 'modding resources'."),
    "iw": ("Modelo, voces, gritos, golpes, técnicas y definitiva de Infinite World (con modo hiper).",
           "Infinite World model, voices, yells, moves, techniques and ultimate (with hyper mode)."),
    "sdbh": ("Modelos HD de Heroes (boca, 7 caras, rampas); golpes de Shin Budokai o del donante.",
             "HD models from Heroes (mouth, 7 faces, ramps); Shin Budokai or donor moves."),
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
        if self.us_override and os.path.isdir(self.us_override):
            return os.path.abspath(self.us_override)
        # launcher (ProjectRoot): sube desde el exe buscando us/ o assets/us/
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
        p = first_existing(os.path.join(KIT, "assets", "us", "data_cmn.afs"),
                           os.path.join(KIT, "us", "data_cmn.afs"))
        return os.path.dirname(p) if p else os.path.join(KIT, "assets", "us")

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
    "roster_build.py": ("personajes", "Personajes nuevos: montar _roster, crear, vista previa, cápsulas y "
                        "plazas.", "New characters: build _roster, create, preview, capsules and slots.",
                        {"mods": "mods"}),
    "model_render.py": ("personajes", "Render cel-shading (estilo B3) de un bin #AMB HD a PNG.",
                        "Cel-shaded (B3 style) render of an HD #AMB bin to PNG.", {}),
    "name_banner.py": ("personajes", "Cambia el rótulo de nombre de un personaje en el select.",
                       "Changes a character's name banner on the select screen.", {"out": "mods"}),
    "portrait_hd.py": ("personajes", "Retrato del select de PS2 (B3/IW) → retrato HD.",
                       "PS2 select portrait (B3/IW) → HD portrait.", {"out": "mods"}),
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
    """Copia de seguridad antes de editar (modkit_backups/<fecha>/<ruta relativa a mods>)."""
    if not os.path.isfile(path):
        return ""
    try:
        rel = os.path.relpath(path, env.mods)
        if rel.startswith(".."):
            rel = os.path.basename(path)
    except ValueError:
        rel = os.path.basename(path)
    dst = os.path.join(env.backups, time.strftime("%Y%m%d-%H%M%S"), rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(path, dst)
    return dst


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
    "characters": ("\uE716", "☺"), "create": ("\uE8FA", "✚"), "textures": ("\uE790", "✎"),
    "swap": ("\uE8AB", "⇄"), "help": ("\uE897", "?"), "tools": ("\uE90F", "⚒"), "home": ("\uE80F", "⌂"),
    "diag": ("\uE9D2", "♥"), "studio": ("\uE714", "◉"), "hex": ("\uE943", "#"),
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
PAGE_DEFS = [
    ("setup", "Comprobar instalación", "Check installation",
     "Python, librerías, carpeta del juego y tus juegos de PS2.", "Python, libraries, game folder and your PS2 games."),
    ("mods", "Mis mods", "My mods",
     "Activa, desactiva y revisa los mods instalados.", "Enable, disable and inspect installed mods."),
    ("importer", "Importar personaje", "Import a character",
     "Trae personajes de Budokai 1, 2, Infinite World o de la comunidad.",
     "Bring characters from Budokai 1, 2, Infinite World or the community."),
    ("characters", "Personajes nuevos", "New characters",
     "Nombre, plaza en la rueda, imágenes y cápsulas de tus personajes.",
     "Name, wheel slot, images and capsules of your characters."),
    ("create", "Crear con mis modelos", "Create from my models",
     "Un personaje nuevo a partir de tus propios archivos de modelo.",
     "A new character from your own model files."),
    ("textures", "Texturas", "Textures",
     "Saca las texturas de un personaje a PNG, edítalas y vuelve a montarlas.",
     "Export a character's textures to PNG, edit them and rebuild."),
    ("swap", "Cambio de modelo", "Model swap",
     "Pon el modelo de un personaje en el sitio de otro.", "Put one character's model in another's slot."),
    ("studio", "Studio de cámaras", "Camera Studio",
     "Rehaz las cámaras de las técnicas: línea de tiempo, plantillas, vista previa y Blender.",
     "Redo technique cameras: timeline, templates, preview and Blender."),
    ("diag", "Diagnóstico", "Diagnostics",
     "¿Va lento o se cierra? Analiza el registro del juego y te dice qué hacer.",
     "Slow or crashing? Analyse the game log and get what to do."),
    ("help", "Ayuda y comunidad", "Help & community",
     "Guías, primeros pasos y el Discord de la comunidad.", "Guides, first steps and the community Discord."),
]


class HomePage(Page):
    key = "home"

    def build(self):
        sf, body = self.scroll_body()
        self.sf = sf
        top = Card(body, accent=True)
        top.pack(fill="x", pady=(12, 14))
        label(top.body, T("¡Hola! ¿Qué quieres hacer hoy?", "Hi! What do you want to do today?"),
              font=F["h1"]).pack(fill="x")
        label(top.body, T("Elige una tarjeta. Cada paso explica lo que hace y no toca tus partidas guardadas. "
                          "Los personajes nuevos se añaden al juego al pulsar JUGAR en el launcher.",
                          "Pick a card. Every step explains what it does and never touches your saves. New "
                          "characters are added to the game when you press PLAY in the launcher."),
              fg=C["dim"], wrap=860).pack(fill="x", pady=(4, 10))
        row = tk.Frame(top.body, bg=C["card"])
        row.pack(fill="x")
        self.dot = tk.Label(row, text="●", bg=C["card"], fg=C["dim"], font=F["h3"])
        self.dot.pack(side="left")
        self.summary = label(row, "", font=F["bold"], bg=C["card"])
        self.summary.pack(side="left", padx=(6, 12))
        ttk.Button(row, text=T("Ver detalles", "Details"), style="Small.TButton",
                   command=lambda: self.app.goto("setup")).pack(side="left")
        grid = tk.Frame(body, bg=C["bg"])
        grid.pack(fill="both", expand=True)
        cols = 4
        for c in range(cols):
            grid.columnconfigure(c, weight=1, uniform="cards")
        for i, (key, es, en, des, den) in enumerate(PAGE_DEFS):
            card = tk.Frame(grid, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
            card.grid(row=i // cols, column=i % cols, sticky="nsew", padx=(0 if i % cols == 0 else 7, 0 if
                                                                           i % cols == cols - 1 else 7), pady=7)
            tk.Label(card, text=icon(key), bg=C["card"], fg=C["accent"], font=F["icon"]).pack(anchor="w", padx=18,
                                                                                            pady=(18, 6))
            tk.Label(card, text=T(es, en), bg=C["card"], fg=C["text"], font=F["h3"], anchor="w").pack(
                fill="x", padx=18)
            tk.Label(card, text=T(des, den), bg=C["card"], fg=C["dim"], font=F["small"], anchor="nw",
                     justify="left", wraplength=200).pack(fill="both", expand=True, padx=18, pady=(4, 18))
            make_clickable(card, lambda k=key: self.app.goto(k), C["card"], C["card_hi"])
        tip = tk.Frame(body, bg=C["bg"])
        tip.pack(fill="x", pady=(10, 6))
        hint(tip, T("¿Ya sabes modear? Cambia arriba a «Avanzado»: todas las herramientas del kit con todas sus "
                    "opciones, el registro completo y las rutas.",
                    "Already a modder? Switch to 'Advanced' at the top: every kit tool with all its options, the "
                    "full log and raw paths."), wrap=860).pack(fill="x")

    def on_show(self):
        checks = self.app.checks()
        bad = [c for c in checks if c["level"] == "err"]
        warn = [c for c in checks if c["level"] == "warn"]
        if bad:
            self.dot.configure(fg=C["err"])
            self.summary.configure(text=T("Falta algo: %s", "Something is missing: %s") % ", ".join(
                c["title"] for c in bad))
        elif warn:
            self.dot.configure(fg=C["warn"])
            self.summary.configure(text=T("Casi listo. Revisa: %s", "Almost ready. Check: %s") % ", ".join(
                c["title"] for c in warn))
        else:
            self.dot.configure(fg=C["ok"])
            self.summary.configure(text=T("Todo listo para modear.", "All set for modding."))


# --------------------------------------------------------------------------------- comprobar
class SetupPage(Page):
    key = "setup"

    def build(self):
        self.header(T("Comprobar instalación", "Check installation") if not self.adv else
                    T("Entorno", "Environment"),
                    T("Comprueba que el kit, Python, las librerías y los datos del juego están en su sitio.",
                      "Checks that the kit, Python, its libraries and the game data are in place."))
        ttk.Button(self.header_right, text="⟳  " + T("Comprobar de nuevo", "Check again"),
                   command=lambda: self.refresh(True)).pack(side="right")
        sf, body = self.scroll_body()
        self.req = Card(body, T("Lo necesario", "Requirements"))
        self.req.pack(fill="x", pady=(0, 12))
        self.rows = tk.Frame(self.req.body, bg=C["card"])
        self.rows.pack(fill="x")
        if self.adv:
            pc = Card(body, T("Rutas", "Paths"), T("Detectadas como en el launcher. Cambia la carpeta del juego o la "
                                                   "de datos (us) si están en otro sitio.",
                                                   "Detected like the launcher does. Change the game or data (us) "
                                                   "folder if they live elsewhere."))
            pc.pack(fill="x", pady=(0, 12))
            g = pc.body
            g.columnconfigure(1, weight=1)
            self.v_game = tk.StringVar(value=self.env.game_override)
            self.v_us = tk.StringVar(value=self.env.us_override)
            r = 0
            for lab, var, tip in ((T("Carpeta del juego (dbz3.exe)", "Game folder (dbz3.exe)"), self.v_game,
                                   T("Vacío = automática", "Empty = automatic")),
                                  (T("Carpeta de datos us/ (data_cmn.afs)", "Data folder us/ (data_cmn.afs)"),
                                   self.v_us, T("Vacío = automática (como el launcher)",
                                                "Empty = automatic (like the launcher)"))):
                label(g, lab, bg=C["card"]).grid(row=r, column=0, sticky="w", padx=(0, 12), pady=3)
                PathEntry(g, var, "dir", bg=C["card"]).grid(row=r, column=1, sticky="ew", pady=3)
                hint(g, tip, bg=C["card"]).grid(row=r, column=2, sticky="w", padx=8)
                r += 1
            bf = tk.Frame(g, bg=C["card"])
            bf.grid(row=r, column=1, sticky="w", pady=(6, 8))
            ttk.Button(bf, text=T("Aplicar rutas", "Apply paths"), style="Accent.TButton",
                       command=self.apply_paths).pack(side="left")
            ttk.Button(bf, text=T("Automáticas", "Automatic"), command=self.auto_paths).pack(side="left", padx=6)
            r += 1
            self.path_info = tk.Frame(g, bg=C["card"])
            self.path_info.grid(row=r, column=0, columnspan=3, sticky="ew")
        self.ps2 = Card(body, T("Tus juegos de PS2 (carpeta ps2_games)", "Your PS2 games (ps2_games folder)"),
                        T("El importador lee TUS copias tal cual (sin extraer las ISO). Ponlas en ps2_games con "
                          "estos nombres; lee ps2_games\\LEEME.txt.",
                          "The importer reads YOUR copies as they are (ISOs are not extracted). Put them in "
                          "ps2_games with these names; see ps2_games\\LEEME.txt."))
        self.ps2.pack(fill="x", pady=(0, 12))
        self.ps2_rows = tk.Frame(self.ps2.body, bg=C["card"])
        self.ps2_rows.pack(fill="x")
        bf = tk.Frame(self.ps2.body, bg=C["card"])
        bf.pack(fill="x", pady=(8, 0))
        ttk.Button(bf, text=T("Abrir ps2_games", "Open ps2_games"), command=self.open_ps2).pack(side="left")
        ttk.Button(bf, text=T("Cómo nombrar los juegos", "How to name the games"),
                   command=self.ps2_readme).pack(side="left", padx=6)
        self.ps2_status = self.status_label(self.ps2.body)
        self.ps2_status.pack(fill="x", pady=(6, 0))

    def on_show(self):
        self.refresh(False)

    def refresh(self, force):
        if force:
            self.env.refresh()
            self.app.cache.pop("checks", None)
            self.app.status.refresh_env()
        for w in self.rows.winfo_children():
            w.destroy()
        for i, c in enumerate(self.app.checks(force)):
            self._row(self.rows, i, c)
        if self.adv:
            for w in self.path_info.winfo_children():
                w.destroy()
            e = self.env
            items = [("Kit", e.kit), (T("Juego", "Game"), e.game or T("(no encontrado)", "(not found)")),
                     ("mods", e.mods), ("us", e.us or T("(no encontrada)", "(not found)")),
                     ("ps2_games", e.ps2), ("Python", e.python), ("XDK (xbcompress)", e.xdk or "-"),
                     (T("Copias de seguridad", "Backups"), e.backups),
                     (T("Pasar --us/--afs", "Pass --us/--afs"), T("sí", "yes") if e.pass_us() else
                      T("no (las herramientas lo encuentran solas)", "no (tools find it on their own)"))]
            for k, (a, b) in enumerate(items):
                label(self.path_info, a, fg=C["dim"], bg=C["card"], font=F["small"]).grid(row=k, column=0, sticky="w",
                                                                                         padx=(0, 12))
                ent = ttk.Entry(self.path_info, width=90)
                ent.insert(0, b)
                ent.configure(state="readonly")
                ent.grid(row=k, column=1, sticky="ew", pady=1)
            self.path_info.columnconfigure(1, weight=1)
        self.render_sources()
        if force or "sources" not in self.app.cache:
            self.query_sources()

    def _row(self, parent, i, c):
        f = tk.Frame(parent, bg=C["card"])
        f.pack(fill="x", pady=4)
        col = {"ok": C["ok"], "warn": C["warn"], "err": C["err"]}.get(c["level"], C["dim"])
        tk.Label(f, text="●", bg=C["card"], fg=col, font=F["h3"]).pack(side="left", anchor="n")
        tf = tk.Frame(f, bg=C["card"])
        tf.pack(side="left", fill="x", expand=True, padx=(8, 8))
        label(tf, c["title"], font=F["bold"], bg=C["card"]).pack(fill="x")
        if c.get("detail"):
            label(tf, c["detail"], fg=C["dim"], font=F["small"], bg=C["card"], wrap=820).pack(fill="x")
        act = c.get("action")
        if act:
            ttk.Button(f, text=act[0], style="Accent.TButton" if c["level"] == "err" else "TButton",
                       command=act[1]).pack(side="right", anchor="n")

    def query_sources(self):
        if not self.env.tools_installed():
            self.set_status(self.ps2_status, "err", T("Faltan las herramientas del kit (importar.py).",
                                                       "Kit tools are missing (importar.py)."))
            return
        if self.app.jobs.busy():
            return

        def done(job):
            if job.rc == 0:
                self.app.cache["sources"] = parse_sources(job.text)
            if self.alive:
                self.render_sources()
                if job.rc != 0:
                    self.set_status(self.ps2_status, "err", last_line(job.text))
        self.run(cmd_importer(self.env, ["fuentes"]), T("Buscando juegos", "Looking for games"), done, quiet=True)

    def render_sources(self):
        for w in self.ps2_rows.winfo_children():
            w.destroy()
        srcs = self.app.cache.get("sources")
        if not srcs:
            hint(self.ps2_rows, T("Buscando…", "Looking…"), bg=C["card"]).pack(fill="x")
            return
        for s in srcs:
            f = tk.Frame(self.ps2_rows, bg=C["card"])
            f.pack(fill="x", pady=2)
            ready, dev = s["state"] == "listo", s["state"] == "desarrollo"
            col = C["ok"] if ready else C["warn"] if dev else C["dim"]
            tk.Label(f, text="●", bg=C["card"], fg=col, font=F["body"]).pack(side="left")
            nm = T("Budokai 3 (mods de la comunidad)", "Budokai 3 (community mods)") if s["id"] == "b3" else s["name"]
            label(f, nm, font=F["bold"], bg=C["card"], width=34).pack(side="left", padx=6)
            st = T("Listo para importar", "Ready to import") if ready else T("En desarrollo", "In development") \
                if dev else T("No encontrado", "Not found")
            label(f, st, fg=col, bg=C["card"], width=20).pack(side="left")
            label(f, s["path"] if s["path"] else source_note(s["id"]), fg=C["dim"], font=F["small"],
                  bg=C["card"]).pack(side="left", fill="x", expand=True)

    def open_ps2(self):
        os.makedirs(self.env.ps2, exist_ok=True)
        open_path(self.env.ps2)

    def ps2_readme(self):
        p = first_existing(os.path.join(self.env.ps2, "LEEME.txt"),
                           os.path.join(KIT, "tools", "modpacks", "LEEME_PS2_GAMES.txt"))
        if p:
            DocViewer(self.app, p, "ps2_games")

    def apply_paths(self):
        g, u = self.v_game.get().strip(), self.v_us.get().strip()
        if g and not os.path.isfile(os.path.join(g, "dbz3.exe")):
            if not self.app.ask(T("No hay dbz3.exe en esa carpeta. ¿Usarla igualmente?",
                                  "There is no dbz3.exe in that folder. Use it anyway?")):
                return
        if u and not os.path.isfile(os.path.join(u, "data_cmn.afs")):
            if not self.app.ask(T("No hay data_cmn.afs en esa carpeta. ¿Usarla igualmente?",
                                  "There is no data_cmn.afs in that folder. Use it anyway?")):
                return
        self.app.settings["game_dir"], self.app.settings["us_dir"] = g, u
        self.app.save()
        self.refresh(True)

    def auto_paths(self):
        self.v_game.set("")
        self.v_us.set("")
        self.apply_paths()


def install_requirements(app):
    bat = first_existing(os.path.join(KIT, "instalar_requisitos.bat"),
                         os.path.join(KIT, "tools", "modpacks", "instalar_requisitos.bat"))
    if not bat:
        app.info(T("No encuentro instalar_requisitos.bat junto al kit. Instala a mano con:\n\n"
                   "py -3 -m pip install --user numpy Pillow scipy",
                   "instalar_requisitos.bat was not found next to the kit. Install manually with:\n\n"
                   "py -3 -m pip install --user numpy Pillow scipy"))
        return
    if not app.ask(T("Se abrirá una ventana que descarga e instala numpy, Pillow y scipy desde internet "
                     "(pip, solo para tu usuario). ¿Continuar?",
                     "A window will open that downloads and installs numpy, Pillow and scipy from the internet "
                     "(pip, for your user only). Continue?")):
        return
    try:
        flags = subprocess.CREATE_NEW_CONSOLE if IS_WIN else 0
        subprocess.Popen(["cmd", "/c", bat] if IS_WIN else ["sh", bat], cwd=os.path.dirname(bat), creationflags=flags)
        app.log_text(T("Abierto %s. Cuando termine, pulsa «Comprobar de nuevo».\n",
                       "Opened %s. When it finishes, press 'Check again'.\n") % bat, "ok")
    except OSError as ex:
        app.info(str(ex))


def collect_checks(app):
    env = app.env
    out = []
    ver = python_version(env.python)
    vs = ".".join(str(x) for x in ver)
    out.append({"key": "python", "title": "Python %s" % vs, "level": "ok" if ver >= (3, 11) else "err",
                "detail": env.python if ver >= (3, 11) else T(
                    "Las herramientas necesitan Python 3.11 o superior (python.org, marca «Add python.exe to PATH»).",
                    "The tools need Python 3.11 or newer (python.org, tick 'Add python.exe to PATH').")})
    mods = check_modules(env.python)
    missing = [{"PIL": "Pillow"}.get(n, n) for n, ok in mods.items() if not ok]
    out.append({"key": "libs", "title": T("Librerías numpy, Pillow y scipy", "numpy, Pillow and scipy libraries"),
                "level": "ok" if not missing else "err",
                "detail": T("Instaladas.", "Installed.") if not missing else
                T("Faltan: %s. Pulsa «Instalar requisitos» (una sola vez).",
                  "Missing: %s. Press 'Install requirements' (only once).") % ", ".join(missing),
                "action": (T("Instalar requisitos", "Install requirements"), lambda: install_requirements(app))})
    out.append({"key": "game", "title": T("Carpeta del juego", "Game folder"),
                "level": "ok" if env.game else "err",
                "detail": env.game if env.game else T(
                    "No encuentro dbz3.exe. Copia el kit junto a dbz3.exe o elige la carpeta del juego.",
                    "dbz3.exe not found. Copy the kit next to dbz3.exe or choose the game folder."),
                "action": None if env.game else (T("Elegir carpeta…", "Choose folder…"), app.pick_game)})
    afs = env.afs
    out.append({"key": "data", "title": T("Datos del juego", "Game data"),
                "level": "ok" if afs and os.path.isfile(afs) else "warn",
                "detail": afs if afs and os.path.isfile(afs) else T(
                    "No encuentro los datos del juego (carpeta us). Los mods necesitan el juego extraído en una "
                    "CARPETA (jugando desde la ISO no se aplican).",
                    "The game data (us folder) was not found. Mods need the game extracted to a FOLDER (they "
                    "do not apply when playing from the ISO).")})
    out.append({"key": "mods", "title": T("Carpeta de mods", "Mods folder"),
                "level": "ok" if os.path.isdir(env.mods) else "warn",
                "detail": env.mods + ("" if os.path.isdir(env.mods) else T("  (se creará al hacer el primer mod)",
                                                                             "  (created with the first mod)"))})
    tools_ok = env.tools_installed()
    xdk_ok = bool(env.xdk) and os.path.isfile(os.path.join(env.xdk, "xbcompress.exe"))
    out.append({"key": "tools", "title": T("Herramientas del kit", "Kit tools"),
                "level": "ok" if tools_ok and xdk_ok else "err" if not tools_ok else "warn",
                "detail": (T("Todo en su sitio: puedes importar y crear personajes.",
                             "All in place: you can import and create characters.")
                           if tools_ok and xdk_ok else T("Falta: ", "Missing: ") + ", ".join(
                               x for x, ok in (("importar.py / roster_build.py", tools_ok),
                                               ("mod center hd\\tools\\xbcompress.exe", xdk_ok)) if not ok))})
    srcs = app.cache.get("sources") or []
    ready = [s for s in srcs if s["state"] == "listo"]
    out.append({"key": "ps2", "title": T("Juegos para importar (opcional)", "Games to import from (optional)"),
                "level": "ok" if ready else "info",
                "detail": (", ".join(T("Budokai 3 (comunidad)", "Budokai 3 (community)") if s["id"] == "b3" else
                                     s["name"] for s in ready) if ready else
                           T("Ninguno encontrado todavía en ps2_games (opcional: solo para importar).",
                             "None found in ps2_games yet (optional: only for importing).") if srcs else
                           T("Pendiente de comprobar.", "Not checked yet."))})
    return out


# --------------------------------------------------------------------------------- mods
TYPE_NAMES = {
    "personaje": ("Personaje nuevo", "New character"), "traje": ("Traje extra", "Extra costume"),
    "generado": ("Generado (no tocar)", "Generated (do not edit)"), "texturas": ("Texturas", "Textures"),
    "data": ("Datos / modelo", "Data / model"), "audio": ("Audio", "Audio"), "other": ("Otro", "Other"),
    "studio": ("Cámaras (Studio)", "Cameras (Studio)"),
}


def type_name(t):
    es, en = TYPE_NAMES.get(t, (t or "?", t or "?"))
    return T(es, en)


class ModsPage(Page):
    key = "mods"

    def build(self):
        self.header(T("Mis mods", "My mods"),
                    T("Un mod está activo si su casilla está marcada. Desactivar no borra nada: solo deja de "
                      "cargarse. Los personajes nuevos se aplican al pulsar JUGAR.",
                      "A mod is active when ticked. Disabling deletes nothing: it just stops loading. New "
                      "characters apply when you press PLAY."))
        ttk.Button(self.header_right, text="⟳  " + T("Refrescar", "Refresh"), command=self.refresh).pack(side="right")
        ttk.Button(self.header_right, text=T("Abrir carpeta mods", "Open mods folder"),
                   command=lambda: open_path(self.env.mods if os.path.isdir(self.env.mods) else self.env.game)
                   ).pack(side="right", padx=6)
        if self.adv:
            ttk.Button(self.header_right, text="✚  " + T("Nuevo mod…", "New mod…"),
                       command=lambda: NewModDialog(self.app, self)).pack(side="right")
        main = tk.Frame(self, bg=C["bg"])
        main.pack(fill="both", expand=True, padx=22, pady=(0, 12))
        left = tk.Frame(main, bg=C["bg"])
        left.pack(side="left", fill="both", expand=True)
        sr = tk.Frame(left, bg=C["bg"])
        sr.pack(fill="x", pady=(0, 6))
        self.search = tk.StringVar()
        self.search.trace_add("write", lambda *_: self.fill())
        label(sr, "⌕", fg=C["dim"], font=F["h3"]).pack(side="left")
        ttk.Entry(sr, textvariable=self.search, width=30).pack(side="left", padx=6)
        self.count = label(sr, "", fg=C["dim"])
        self.count.pack(side="left", padx=8)
        hint(sr, T("Clic en «Estado» (o barra espaciadora) = activar / desactivar",
                   "Click 'Status' (or space) = enable / disable"), bg=C["bg"]).pack(side="right")
        cols = ("estado", "tipo", "carpeta", "autor", "version") if self.adv else ("estado", "tipo")
        tf = tk.Frame(left, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
        tf.pack(fill="both", expand=True)
        self.tree = ttk.Treeview(tf, columns=cols, show="tree headings", style="Icons.Treeview", selectmode="browse")
        self.tree.heading("#0", text=T("Mod", "Mod"))
        self.tree.column("#0", width=260, minwidth=160, stretch=True)
        heads = {"estado": (T("Estado", "Status"), 110), "tipo": (T("Tipo", "Type"), 140),
                 "carpeta": (T("Carpeta", "Folder"), 150), "autor": (T("Autor", "Author"), 110),
                 "version": (T("Versión", "Version"), 60)}
        for c in cols:
            self.tree.heading(c, text=heads[c][0])
            self.tree.column(c, width=heads[c][1], minwidth=50, stretch=c in ("carpeta", "autor", "tipo"))
        vsb = ttk.Scrollbar(tf, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        self.tree.tag_configure("off", foreground=C["dim"])
        self.tree.tag_configure("gen", foreground=C["blue"])
        self.tree.bind("<<TreeviewSelect>>", lambda _e: self.show_details())
        self.tree.bind("<ButtonRelease-1>", self._click)
        self.tree.bind("<space>", lambda _e: self.toggle())

        bottom = tk.Frame(left, bg=C["bg"])
        bottom.pack(fill="x", pady=(8, 0))
        ttk.Button(bottom, text="⟳  " + T("Reconstruir personajes nuevos", "Rebuild new characters"),
                   command=self.rebuild).pack(side="left")
        self.status = self.status_label(bottom)
        self.status.configure(bg=C["bg"])
        self.status.pack(side="left", padx=10, fill="x", expand=True)
        self.side = Card(main, pad=16)
        self.side.configure(width=340)
        self.side.pack(side="right", fill="y", padx=(14, 0))
        self.side.pack_propagate(False)
        self.mods, self.images = [], {}

    def on_show(self):
        self.refresh()

    def refresh(self):
        self.mods = scan_mods(self.env.mods)
        self.images = {}
        self.fill()

    def fill(self):
        sel = self.selected_name()
        self.tree.delete(*self.tree.get_children())
        needle = self.search.get().lower().strip()
        shown = 0
        for m in self.mods:
            title = m["display"] or m["name"]
            if needle and needle not in (title + " " + m["name"] + " " + m["type"]).lower():
                continue
            img = self.images.get(m["name"])
            if img is None and m["icon"]:
                img = load_png(m["icon"], 42, 42)
                self.images[m["name"]] = img
            vals = [("✔ " + T("Activo", "Active")) if m["enabled"] else ("✖ " + T("Desactivado", "Disabled")),
                    type_name(m["type"])]
            if self.adv:
                vals += [m["name"], m["author"], m["version"]]
            tags = ("off",) if not m["enabled"] else ("gen",) if m["type"] == "generado" else ()
            kw = {"image": img} if img else {}
            self.tree.insert("", "end", iid=m["name"], text="  " + title, values=vals, tags=tags, **kw)
            shown += 1
        self.count.configure(text=T("%d de %d mods", "%d of %d mods") % (shown, len(self.mods)))
        if not os.path.isdir(self.env.mods):
            self.count.configure(text=T("No hay carpeta mods todavía: %s", "No mods folder yet: %s") % self.env.mods)
        if sel and self.tree.exists(sel):
            self.tree.selection_set(sel)
            self.tree.see(sel)
        self.show_details()

    def _click(self, e):
        """Un clic en la columna Estado activa / desactiva ese mod."""
        if self.tree.identify_region(e.x, e.y) != "cell" or self.tree.identify_column(e.x) != "#1":
            return
        row = self.tree.identify_row(e.y)
        if row:
            self.tree.selection_set(row)
            self.toggle()

    def selected_name(self):
        s = self.tree.selection()
        return s[0] if s else None

    def selected(self):
        n = self.selected_name()
        return next((m for m in self.mods if m["name"] == n), None)

    def show_details(self):
        for w in self.side.body.winfo_children():
            w.destroy()
        m = self.selected()
        b = self.side.body
        if not m:
            label(b, T("Elige un mod de la lista para ver qué es y activarlo o desactivarlo.",
                       "Pick a mod from the list to see what it is and enable or disable it."), fg=C["dim"],
                  bg=C["card"], wrap=300).pack(fill="x")
            return
        img = self.images.get(m["name"])
        if img:
            tk.Label(b, image=img, bg=C["card"]).pack(anchor="w")
        label(b, m["display"] or m["name"], font=F["h2"], bg=C["card"], wrap=300).pack(fill="x")
        label(b, T("Carpeta: ", "Folder: ") + m["raw"], fg=C["dim"], font=F["small"], bg=C["card"]).pack(fill="x")
        pill = tk.Label(b, text=("✔ " + T("Activo", "Active")) if m["enabled"] else ("✖ " + T("Desactivado",
                                                                                              "Disabled")),
                        bg=C["accent_soft"] if m["enabled"] else C["frame"],
                        fg=C["ok"] if m["enabled"] else C["dim"], font=F["bold_sm"], padx=10, pady=3)
        pill.pack(anchor="w", pady=(8, 8))
        label(b, T("Tipo: ", "Type: ") + type_name(m["type"]), bg=C["card"]).pack(fill="x")
        if m["author"] or m["version"]:
            label(b, T("Autor: ", "Author: ") + (m["author"] or "-") + ("   v" + m["version"] if m["version"] else ""),
                  bg=C["card"]).pack(fill="x")
        if m["desc"]:
            label(b, m["desc"], fg=C["dim"], bg=C["card"], wrap=300).pack(fill="x", pady=(6, 0))
        if m["type"] == "generado":
            label(b, T("Lo genera «Reconstruir personajes nuevos» a partir de tus personajes: no hace falta "
                       "tocarlo.", "Built by 'Rebuild new characters' from your characters: no need to touch it."),
                  fg=C["blue"], bg=C["card"], wrap=300).pack(fill="x", pady=(6, 0))
        if self.adv:
            n, size = mod_stats(m["dir"])
            label(b, T("%d archivos, %s", "%d files, %s") % (n, human_size(size)), fg=C["dim"], font=F["small"],
                  bg=C["card"]).pack(fill="x", pady=(6, 0))
            ent = ttk.Entry(b)
            ent.insert(0, m["dir"])
            ent.configure(state="readonly")
            ent.pack(fill="x", pady=(4, 0))
        bf = tk.Frame(b, bg=C["card"])
        bf.pack(fill="x", pady=(14, 0))
        ttk.Button(bf, text=T("Desactivar", "Disable") if m["enabled"] else T("Activar", "Enable"),
                   style="Accent.TButton", command=self.toggle).pack(fill="x")
        ttk.Button(bf, text=T("Abrir carpeta", "Open folder"), command=lambda: open_path(m["dir"])).pack(
            fill="x", pady=(6, 0))
        if m["type"] == "personaje":
            ttk.Button(bf, text=T("Editar personaje", "Edit character"),
                       command=lambda: self.app.goto("characters", mod=m["name"])).pack(fill="x", pady=(6, 0))
        if self.adv and m["toml"]:
            ttk.Button(bf, text=T("Editar ajustes (formulario)", "Edit settings (form)"),
                       command=lambda: TomlFormDialog(self.app, m["toml"], self.refresh)).pack(fill="x", pady=(6, 0))
        if self.adv:
            files = [f for f in ("personaje.toml", "traje.toml", "manifest.txt", "README.md", "roster.toml")
                     if os.path.isfile(os.path.join(m["dir"], f))]
            if files:
                ttk.Button(bf, text=T("Editar texto…", "Edit text…"),
                           command=lambda: TextEditor(self.app, [os.path.join(m["dir"], f) for f in files],
                                                      self.refresh)).pack(fill="x", pady=(6, 0))

    def toggle(self):
        m = self.selected()
        if not m:
            return
        if m["type"] == "generado" and m["enabled"]:
            if not self.app.ask(T("Este mod es la lista de tus personajes nuevos (la crea el kit). Si lo desactivas, "
                                  "ninguno aparecerá en el juego. ¿Desactivar?",
                                  "This mod is the list of your new characters (the kit makes it). If you disable it "
                                  "none of them will show up in game. Disable?")):
                return
        try:
            set_mod_enabled(self.env.mods, m["name"], not m["enabled"])
        except OSError as ex:
            self.set_status(self.status, "err", str(ex))
            return
        self.app.log_text("%s: %s\n" % (m["name"], T("activado", "enabled") if not m["enabled"] else
                                        T("desactivado", "disabled")), "ok")
        if m["type"] in ("personaje", "traje"):
            self.set_status(self.status, "ok", T("Hecho. Los personajes se actualizan al pulsar JUGAR (o con "
                                                 "«Reconstruir personajes nuevos»).",
                                                 "Done. Characters update when you press PLAY (or with 'Rebuild "
                                                 "new characters')."))
        else:
            self.set_status(self.status, "ok", T("Hecho.", "Done."))
        self.refresh()

    def rebuild(self):
        self.run(cmd_roster_build(self.env, True), T("Reconstruir personajes nuevos", "Rebuild new characters"),
                 lambda job: self.alive and self.refresh(), self.status,
                 T("Personajes nuevos montados.", "New characters built."))


class TextEditor(tk.Toplevel):
    """Editor de texto de los archivos de un mod (con copia de seguridad al guardar)."""

    def __init__(self, app, files, on_save=None):
        super().__init__(app.root)
        if app.selftest:
            self.withdraw()
        self.app, self.files, self.on_save = app, files, on_save
        self.title(T("Editar", "Edit") + " - " + os.path.basename(os.path.dirname(files[0])))
        self.configure(bg=C["bg"])
        self.geometry("900x640")
        top = tk.Frame(self, bg=C["bg"])
        top.pack(fill="x", padx=14, pady=10)
        self.cur = tk.StringVar(value=files[0])
        cb = ttk.Combobox(top, textvariable=self.cur, values=files, state="readonly", width=90)
        cb.pack(side="left", fill="x", expand=True)
        cb.bind("<<ComboboxSelected>>", lambda _e: self.load())
        body = tk.Frame(self, bg=C["log_bg"])
        body.pack(fill="both", expand=True, padx=14)
        self.text = tk.Text(body, bg=C["log_bg"], fg=C["text"], insertbackground=C["text"], font=F["mono"],
                            relief="flat", bd=0, padx=10, pady=8, undo=True, wrap="none",
                            selectbackground=C["accent_dim"])
        vsb = ttk.Scrollbar(body, orient="vertical", command=self.text.yview)
        self.text.configure(yscrollcommand=vsb.set)
        self.text.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        bot = tk.Frame(self, bg=C["bg"])
        bot.pack(fill="x", padx=14, pady=10)
        self.msg = label(bot, T("Al guardar se hace antes una copia en modkit_backups.",
                                "A copy is saved to modkit_backups before writing."), fg=C["dim"])
        self.msg.pack(side="left", fill="x", expand=True)
        ttk.Button(bot, text=T("Cerrar", "Close"), command=self.destroy).pack(side="right")
        ttk.Button(bot, text=T("Guardar", "Save"), style="Accent.TButton", command=self.save).pack(side="right",
                                                                                                    padx=6)
        self.load()

    def load(self):
        try:
            with open(self.cur.get(), encoding="utf-8", errors="replace") as fh:
                data = fh.read()
        except OSError as ex:
            data = str(ex)
        self.text.delete("1.0", "end")
        self.text.insert("1.0", data)
        self.text.edit_reset()

    def save(self):
        path = self.cur.get()
        data = self.text.get("1.0", "end-1c")
        if path.endswith(".toml") and tomllib is not None:
            try:
                tomllib.loads(data)
            except Exception as ex:  # noqa: BLE001
                if not self.app.ask(T("El TOML tiene un error:\n%s\n\n¿Guardar igualmente?",
                                      "The TOML has an error:\n%s\n\nSave anyway?") % ex, parent=self):
                    return
        try:
            bk = backup_file(path, self.app.env)
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(data if data.endswith("\n") else data + "\n")
        except OSError as ex:
            self.msg.configure(text=str(ex), fg=C["err"])
            return
        self.msg.configure(text=T("Guardado. Copia: %s", "Saved. Backup: %s") % (bk or "-"), fg=C["ok"])
        self.app.log_text(T("Guardado %s (copia en %s)\n", "Saved %s (backup at %s)\n") % (path, bk or "-"), "ok")
        if self.on_save:
            self.on_save()


class TomlFormDialog(tk.Toplevel):
    """Formulario para personaje.toml / traje.toml (las claves sencillas; el resto se conserva)."""

    FIELDS_P = [("nombre", "str"), ("donante", "donor"), ("id", "slot"), ("despues_de", "after"),
                ("formas", "int"), ("modelos_por_traje", "int"), ("modelo_forma", "raw"), ("ki_base", "raw"),
                ("fisica", "fis"), ("transformacion", "str"), ("voces", "str"), ("gritos", "str"),
                ("icono_fuente", "src"), ("retrato_fuente", "src")]
    FIELDS_T = [("personaje", "donor"), ("nombre", "str")]

    def __init__(self, app, path, on_save=None):
        super().__init__(app.root)
        if app.selftest:
            self.withdraw()
        self.app, self.path, self.on_save = app, path, on_save
        self.section = "traje" if os.path.basename(path) == "traje.toml" else "personaje"
        self.title(T("Ajustes", "Settings") + " - " + os.path.basename(os.path.dirname(path)))
        self.configure(bg=C["bg"])
        data = read_toml(path).get(self.section, {})
        card = Card(self, os.path.basename(path), T("Deja un campo vacío para quitar esa clave (se usa el valor "
                                                    "por defecto). Se guarda una copia antes de escribir.",
                                                    "Leave a field empty to remove that key (the default is used). A "
                                                    "copy is saved before writing."))
        card.pack(fill="both", expand=True, padx=14, pady=14)
        g = card.body
        g.columnconfigure(1, weight=1)
        self.vars = {}
        hints = {
            "voces": T("iw:NOMBRE | b3:ID | donante | ninguna",
                       "iw:NAME | b3:ID | donante (= the donor's) | ninguna (= none)"),
            "gritos": T("iw:NOMBRE | b1:N | donante | ninguno",
                        "iw:NAME | b1:N | donante (= the donor's) | ninguno (= none)"),
            "formas": T("número de formas (vacío = las del donante)", "number of forms (empty = the donor's)"),
            "despues_de": T("su casilla aparece tras este personaje", "its cell shows up after this character"),
            "id": T("plaza en la rueda (vacío = automática)", "wheel slot (empty = automatic)"),
            "modelo_forma": T("modelo del traje por forma, p. ej. [0, 1, 2, 3]", "costume model per form, e.g. "
                              "[0, 1, 2, 3]"),
            "ki_base": T("barras a las que tiende cada forma, p. ej. [3, 4, 4, 5]", "bars each form drifts to, e.g. "
                         "[3, 4, 4, 5]"),
            "fisica": T("donante | ID de personaje (pelo y cinturón con física)", "donante | character ID (hair and "
                        "belt physics)"),
            "transformacion": T("donante = P+K+G del donante en su moveset propio", "donante = donor's P+K+G in "
                                "its own moveset"),
        }
        fields = self.FIELDS_T if self.section == "traje" else self.FIELDS_P
        for r, (key, kind) in enumerate(fields):
            label(g, key, bg=C["card"], font=F["bold"]).grid(row=r, column=0, sticky="w", padx=(0, 12), pady=4)
            cur = data.get(key)
            var = tk.StringVar()
            if kind in ("donor", "after"):
                vals = [""] + donor_values()
                w = ttk.Combobox(g, textvariable=var, values=vals, state="readonly", width=40)
                if isinstance(cur, int):
                    var.set(vals[donor_index(cur, True)] if any(d[0] == cur for d in DONORS) else str(cur))
            elif kind == "slot":
                vals = [""] + slot_values()
                w = ttk.Combobox(g, textvariable=var, values=vals, state="readonly", width=40)
                if isinstance(cur, int):
                    var.set(next((v for (sid, _), v in zip(FREE_SLOTS, vals[1:]) if sid == cur), str(cur)))
            elif kind == "src":
                w = ttk.Combobox(g, textvariable=var, values=["", "modelo", "imagen", "terminado"], state="readonly",
                                 width=40)
                var.set(cur or "")
            elif kind == "raw":
                w = ttk.Entry(g, textvariable=var, width=44)
                var.set("" if cur is None else str(list(cur) if isinstance(cur, (list, tuple)) else cur))
            else:
                w = ttk.Entry(g, textvariable=var, width=44)
                var.set("" if cur is None else str(cur))
            w.grid(row=r, column=1, sticky="ew", pady=4)
            if key in hints:
                hint(g, hints[key], bg=C["card"]).grid(row=r, column=2, sticky="w", padx=8)
            self.vars[key] = (var, kind)
        bf = tk.Frame(g, bg=C["card"])
        bf.grid(row=len(fields), column=1, sticky="w", pady=(12, 0))
        ttk.Button(bf, text=T("Guardar", "Save"), style="Accent.TButton", command=self.save).pack(side="left")
        ttk.Button(bf, text=T("Cancelar", "Cancel"), command=self.destroy).pack(side="left", padx=6)
        self.msg = label(g, "", bg=C["card"])
        self.msg.grid(row=len(fields) + 1, column=0, columnspan=3, sticky="ew", pady=(8, 0))

    def save(self):
        values = {}
        for key, (var, kind) in self.vars.items():
            v = var.get().strip()
            if not v:
                values[key] = None
            elif kind == "raw":
                try:
                    if tomllib:
                        tomllib.loads("x = " + v)
                except Exception:  # noqa: BLE001
                    self.msg.configure(text=T("Valor no válido en %s", "Invalid value in %s") % key, fg=C["err"])
                    return
                values[key] = v
            elif kind == "fis":
                values[key] = v if re.fullmatch(r"\d+", v) else toml_literal(v)
            elif kind in ("donor", "after") and donor_id(v) is not None:
                values[key] = toml_literal(donor_id(v))
            elif kind == "slot" and v in slot_values():
                values[key] = toml_literal(FREE_SLOTS[slot_values().index(v)][0])
            elif kind in ("donor", "after", "slot", "int"):
                m = re.search(r"ID (-?\d+)\)$", v) or re.match(r"(-?\d+)", v)
                if not m:
                    self.msg.configure(text=T("Valor no válido en %s", "Invalid value in %s") % key, fg=C["err"])
                    return
                values[key] = toml_literal(int(m.group(1)))
            else:
                values[key] = toml_literal(v)
        if self.section == "personaje" and not values.get("nombre"):
            self.msg.configure(text=T("El nombre no puede quedar vacío.", "The name cannot be empty."), fg=C["err"])
            return
        try:
            bk = backup_file(self.path, self.app.env)
            toml_set_keys(self.path, self.section, values)
        except OSError as ex:
            self.msg.configure(text=str(ex), fg=C["err"])
            return
        self.app.log_text(T("Guardado %s (copia en %s)\n", "Saved %s (backup at %s)\n") % (self.path, bk), "ok")
        if self.on_save:
            self.on_save()
        self.destroy()


class NewModDialog(tk.Toplevel):
    """Crea el esqueleto de un mod (personaje.toml / traje.toml / vacio) DESACTIVADO hasta completarlo."""

    def __init__(self, app, page):
        super().__init__(app.root)
        if app.selftest:
            self.withdraw()
        self.app, self.page = app, page
        self.title(T("Nuevo mod", "New mod"))
        self.configure(bg=C["bg"])
        card = Card(self, T("Nuevo mod", "New mod"), T(
            "Crea la carpeta del mod con su manifest.txt y una plantilla. Se crea DESACTIVADO: actívalo cuando "
            "hayas puesto tus archivos. Para un personaje con modelos ya hechos es más fácil «Crear con mis "
            "modelos».", "Creates the mod folder with its manifest.txt and a template. It is created DISABLED: "
            "enable it once your files are in place. For a character with ready models, 'Create from my models' "
            "is easier."))
        card.pack(fill="both", expand=True, padx=14, pady=14)
        g = card.body
        g.columnconfigure(1, weight=1)
        self.kind = tk.StringVar(value="personaje")
        kf = tk.Frame(g, bg=C["card"])
        kf.grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 8))
        for val, txt in (("personaje", T("Personaje nuevo (personaje.toml)", "New character (personaje.toml)")),
                         ("traje", T("Traje extra (traje.toml)", "Extra costume (traje.toml)")),
                         ("vacio", T("Vacío (solo manifest.txt)", "Empty (manifest.txt only)"))):
            ttk.Radiobutton(kf, text=txt, value=val, variable=self.kind, style="Card.TRadiobutton").pack(side="left",
                                                                                                         padx=(0, 14))
        self.v = {k: tk.StringVar() for k in ("folder", "name", "author", "desc")}
        self.donor = tk.StringVar(value=donor_values()[donor_index(10, False)])
        rows = [("folder", T("Carpeta", "Folder")), ("name", T("Nombre", "Name")), ("author", T("Autor", "Author")),
                ("desc", T("Descripción", "Description"))]
        for r, (k, txt) in enumerate(rows, start=1):
            label(g, txt, bg=C["card"]).grid(row=r, column=0, sticky="w", padx=(0, 12), pady=3)
            ttk.Entry(g, textvariable=self.v[k], width=52).grid(row=r, column=1, sticky="ew", pady=3)
        label(g, T("Personaje base", "Base character"), bg=C["card"]).grid(row=5, column=0, sticky="w",
                                                                                  padx=(0, 12), pady=3)
        ttk.Combobox(g, textvariable=self.donor, values=donor_values(), state="readonly", width=50).grid(
            row=5, column=1, sticky="ew", pady=3)
        self.v["name"].trace_add("write", self._auto_folder)
        self._folder_auto = True
        bf = tk.Frame(g, bg=C["card"])
        bf.grid(row=6, column=1, sticky="w", pady=(12, 0))
        ttk.Button(bf, text=T("Crear", "Create"), style="Accent.TButton", command=self.create).pack(side="left")
        ttk.Button(bf, text=T("Cancelar", "Cancel"), command=self.destroy).pack(side="left", padx=6)
        self.msg = label(g, "", bg=C["card"], wrap=520)
        self.msg.grid(row=7, column=0, columnspan=2, sticky="ew", pady=(8, 0))

    def _auto_folder(self, *_):
        name = self.v["name"].get()
        if name:
            prefix = {"personaje": "nuevo_", "traje": "traje_"}.get(self.kind.get(), "mod_")
            self.v["folder"].set(slug(name, prefix))

    def create(self):
        env = self.app.env
        folder, name = self.v["folder"].get().strip(), self.v["name"].get().strip()
        if not folder or not name or re.search(r'[<>:"/\\|?*]', folder):
            self.msg.configure(text=T("Pon un nombre y una carpeta válida.", "Enter a name and a valid folder."),
                               fg=C["err"])
            return
        d = os.path.join(env.mods, folder)
        if os.path.exists(d):
            self.msg.configure(text=T("Ya existe esa carpeta.", "That folder already exists."), fg=C["err"])
            return
        did = DONORS[max(0, donor_values().index(self.donor.get()))][0] if self.donor.get() in donor_values() else 10
        kind = self.kind.get()
        try:
            os.makedirs(os.path.join(d, "modelos") if kind != "vacio" else d)
            with open(os.path.join(d, "manifest.txt"), "w", encoding="utf-8") as fh:
                fh.write("name=%s\ndescription=%s\nauthor=%s\nversion=1.0\n%s" % (
                    name, self.v["desc"].get().strip(), self.v["author"].get().strip(),
                    "type=%s\n" % kind if kind != "vacio" else ""))
            if kind == "personaje":
                with open(os.path.join(d, "personaje.toml"), "w", encoding="utf-8") as fh:
                    fh.write(T(PERSONAJE_TEMPLATE, PERSONAJE_TEMPLATE_EN) % {"nombre": name.replace('"', ""), "donante": did})
            elif kind == "traje":
                with open(os.path.join(d, "traje.toml"), "w", encoding="utf-8") as fh:
                    fh.write(T(TRAJE_TEMPLATE, TRAJE_TEMPLATE_EN) % {"nombre": name.replace('"', ""), "personaje": did})
            set_mod_enabled(env.mods, folder, False)
        except OSError as ex:
            self.msg.configure(text=str(ex), fg=C["err"])
            return
        self.app.log_text(T("Creado %s (desactivado)\n", "Created %s (disabled)\n") % d, "ok")
        open_path(d)
        self.page.refresh()
        self.destroy()


PERSONAJE_TEMPLATE = """[personaje]
nombre = "%(nombre)s"         # rotulo del select
donante = %(donante)d                 # moveset, tecnicas, aura y voz de partida
# Pon tus modelos (uno por traje: #AMB de B3 PS2, #AMB HD o LZX) en modelos/ y listalos aqui:
modelos = ["modelos/traje1.amb"]
# id = 44                     # opcional: plaza (22-26, 31 o 44-63)
# despues_de = 10             # opcional: su casilla aparece tras este ID
# voces = "donante"           # iw:NOMBRE | b3:ID | donante | ninguna
# gritos = "donante"          # iw:NOMBRE | b1:N | donante | ninguno
# Capsulas propias (opcional, varias):
# [[capsula]]
# nombre = "Mi ataque"
# tipo = "especial"           # especial | definitiva | transformacion (+ forma = 1)
"""

TRAJE_TEMPLATE = """[traje]
personaje = %(personaje)d               # ID del personaje (0 = Goku)
nombre = "%(nombre)s"
# Un modelo por forma, en el orden de sus transformaciones (si faltan, se repite el ultimo):
modelos = ["modelos/base.amb"]
"""


# las mismas claves (las lee roster_build); solo cambian los comentarios
PERSONAJE_TEMPLATE_EN = """[personaje]
nombre = "%(nombre)s"         # name banner on the select screen
donante = %(donante)d                 # base character: moves, techniques, aura and match voice
# Put your models (one per costume: B3 PS2 #AMB, HD #AMB or LZX) in modelos/ and list them here:
modelos = ["modelos/traje1.amb"]
# id = 44                     # optional: slot (22-26, 31 or 44-63)
# despues_de = 10             # optional: its cell shows up after this ID
# voces = "donante"           # iw:NAME | b3:ID | donante (base character's) | ninguna (none)
# gritos = "donante"          # iw:NAME | b1:N | donante (base character's) | ninguno (none)
# Own capsules (optional, several):
# [[capsula]]
# nombre = "My attack"
# tipo = "especial"           # especial (special) | definitiva (ultimate) | transformacion (+ forma = 1)
"""

TRAJE_TEMPLATE_EN = """[traje]
personaje = %(personaje)d               # character ID (0 = Goku)
nombre = "%(nombre)s"
# One model per form, in transformation order (if some are missing, the last one repeats):
modelos = ["modelos/base.amb"]
"""


# --------------------------------------------------------------------------------- importar
class ImporterPage(Page):
    key = "importer"

    def build(self):
        self.header(T("Importar personaje de otro juego", "Import a character from another game"),
                    T("Elige el juego, después el personaje, revisa el nombre y pulsa Importar. Se añade al juego "
                      "al pulsar JUGAR (o con «Reconstruir ahora»).",
                      "Pick the game, then the character, check the name and press Import. It is added to the "
                      "game when you press PLAY (or with 'Rebuild now')."))
        sf, body = self.scroll_body()
        self.src, self.sel = None, None
        c1 = Card(body, "1. " + T("Elige el juego", "Pick the game"),
                  T("Pon TUS copias en la carpeta ps2_games (mira «Comprobar instalación»).",
                    "Put YOUR copies in the ps2_games folder (see 'Check installation')."))
        c1.pack(fill="x", pady=(0, 12))
        self.tiles = tk.Frame(c1.body, bg=C["card"])
        self.tiles.pack(fill="x")
        bf = tk.Frame(c1.body, bg=C["card"])
        bf.pack(fill="x", pady=(8, 0))
        ttk.Button(bf, text="⟳  " + T("Buscar de nuevo", "Look again"), style="Small.TButton",
                   command=lambda: self.query(True)).pack(side="left")
        ttk.Button(bf, text=T("Abrir ps2_games", "Open ps2_games"), style="Small.TButton",
                   command=lambda: (os.makedirs(self.env.ps2, exist_ok=True), open_path(self.env.ps2))).pack(
            side="left", padx=6)
        self.c1_status = self.status_label(c1.body)
        self.c1_status.pack(fill="x", pady=(6, 0))

        c2 = Card(body, "2. " + T("Elige el personaje", "Pick the character"))
        c2.pack(fill="x", pady=(0, 12))
        sr = tk.Frame(c2.body, bg=C["card"])
        sr.pack(fill="x", pady=(0, 6))
        self.search = tk.StringVar()
        self.search.trace_add("write", lambda *_: self.fill_entries())
        label(sr, "⌕", fg=C["dim"], font=F["h3"], bg=C["card"]).pack(side="left")
        ttk.Entry(sr, textvariable=self.search, width=34).pack(side="left", padx=6)
        self.count = label(sr, "", fg=C["dim"], bg=C["card"])
        self.count.pack(side="left", padx=8)
        if self.adv:
            self.v_folder = tk.StringVar()
            label(sr, T("--carpeta (b3)", "Extra folder (B3)"), fg=C["dim"], bg=C["card"]).pack(side="left", padx=(16, 4))
            PathEntry(sr, self.v_folder, "dir", width=30, bg=C["card"]).pack(side="left")
            Tooltip(sr.winfo_children()[-1], T("Carpeta extra con modelos de la comunidad (.amb / .amo+.amt) para la "
                                               "fuente Budokai 3.", "Extra folder with community models (.amb / "
                                               ".amo+.amt) for the Budokai 3 source."))
        tf = tk.Frame(c2.body, bg=C["card"])
        tf.pack(fill="x")
        self.tree = ttk.Treeview(tf, columns=("contenido", "base"), show="tree headings", height=10,
                                 selectmode="browse")
        self.tree.heading("#0", text=T("Personaje", "Character"))
        self.tree.heading("contenido", text=T("Contenido", "Content"))
        self.tree.heading("base", text=T("Golpes", "Moves"))
        self.tree.column("#0", width=260, minwidth=140)
        self.tree.column("contenido", width=240, minwidth=120)
        self.tree.column("base", width=260, minwidth=120)
        vsb = ttk.Scrollbar(tf, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="x", expand=True)
        vsb.pack(side="right", fill="y")
        self.tree.bind("<<TreeviewSelect>>", lambda _e: self.pick_entry())
        self.c2_status = self.status_label(c2.body)
        self.c2_status.pack(fill="x", pady=(6, 0))

        c3 = Card(body, "3. " + T("Nombre y golpes base", "Name and base moves"))
        c3.pack(fill="x", pady=(0, 12))
        g = c3.body
        g.columnconfigure(1, weight=1)
        self.name = tk.StringVar()
        label(g, T("Nombre en el select", "Select-screen name"), bg=C["card"]).grid(row=0, column=0, sticky="w",
                                                                                  padx=(0, 12), pady=4)
        ttk.Entry(g, textvariable=self.name, width=34).grid(row=0, column=1, sticky="w", pady=4)
        self.banner = BannerPreview(g, 360, 42)
        self.banner.grid(row=1, column=1, sticky="w")
        self.name.trace_add("write", lambda *_: self._name_changed())
        label(g, T("Golpes de (personaje base)", "Moves from (base character)"), bg=C["card"]).grid(row=2, column=0, sticky="w",
                                                                                    padx=(0, 12), pady=4)
        self.donor = tk.StringVar()
        self.donor_cb = ttk.Combobox(g, textvariable=self.donor, state="readonly", width=40, height=20)
        self.donor_cb.grid(row=2, column=1, sticky="w", pady=4)
        hint(g, T("El personaje del juego del que toma lo que le falte (agarres, modo hiper...).",
                  "The game character it takes whatever it lacks from (throws, hyper mode...)."),
             bg=C["card"]).grid(row=3, column=1, sticky="w")
        r = 4
        if self.adv:
            self.slot = tk.StringVar()
            self.after_var = tk.StringVar()
            self.mod = tk.StringVar()
            label(g, "--id (" + T("plaza", "slot") + ")", bg=C["card"]).grid(row=r, column=0, sticky="w", pady=4)
            self.slot_cb = ttk.Combobox(g, textvariable=self.slot, state="readonly", width=40, height=20,
                                        values=[T("Automática", "Automatic")] + slot_values())
            self.slot_cb.grid(row=r, column=1, sticky="w", pady=4)
            self.slot.set(T("Automática", "Automatic"))
            r += 1
            label(g, T("--despues-de", "Cell after"), bg=C["card"]).grid(row=r, column=0, sticky="w", pady=4)
            self.after_cb = ttk.Combobox(g, textvariable=self.after_var, state="readonly", width=40, height=20,
                                         values=donor_values(T("Automática (tras su personaje base)",
                                                               "Automatic (after its base character)")))
            self.after_cb.grid(row=r, column=1, sticky="w", pady=4)
            self.after_var.set(T("Automática (tras su personaje base)", "Automatic (after its base character)"))
            r += 1
            label(g, "--mod (" + T("carpeta", "folder") + ")", bg=C["card"]).grid(row=r, column=0, sticky="w", pady=4)
            ttk.Entry(g, textvariable=self.mod, width=40).grid(row=r, column=1, sticky="w", pady=4)
            r += 1
            hint(g, T("Vacío = automática (imp_<juego>_<nombre>, como el launcher).",
                      "Empty = automatic (imp_<game>_<name>, like the launcher)."), bg=C["card"]).grid(
                row=r, column=1, sticky="w")
            r += 1
        bf = tk.Frame(g, bg=C["card"])
        bf.grid(row=r, column=0, columnspan=2, sticky="w", pady=(12, 0))
        self.import_btn = ttk.Button(bf, text="⇩  " + T("Importar", "Import"), style="Big.Accent.TButton",
                                     command=self.do_import)
        self.import_btn.pack(side="left")
        self.after_frame = tk.Frame(bf, bg=C["card"])
        ttk.Button(self.after_frame, text="⟳  " + T("Reconstruir ahora", "Rebuild now"),
                   command=self.rebuild).pack(side="left", padx=(12, 0))
        ttk.Button(self.after_frame, text=T("Ver en Personajes nuevos", "Show in New characters"),
                   command=lambda: self.app.goto("characters", mod=getattr(self, "last_mod", None))).pack(
            side="left", padx=6)
        self.c3_status = self.status_label(g)
        self.c3_status.grid(row=r + 1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        self._set_donor_choices(None)

    def on_show(self):
        if "sources" not in self.app.cache:
            self.query(False)
        else:
            self.render_tiles()
            if self.src:
                self.fill_entries()

    def query(self, force):
        if not self.env.tools_installed():
            self.set_status(self.c1_status, "err", T(
                "Para importar instala el «Kit de modding»: cópialo junto a dbz3.exe y sigue su LEEME_KIT.txt.",
                "To import, install the 'Modding kit': copy it next to dbz3.exe and follow its LEEME_KIT.txt."))
            return
        if force:
            self.app.cache.pop("sources", None)
            self.app.cache.pop("entries", None)
        self.render_tiles()

        def done(job):
            srcs = parse_sources(job.text)
            if srcs:
                self.app.cache["sources"] = srcs
            if self.alive:
                self.render_tiles()
                if not srcs:
                    self.set_status(self.c1_status, "err", last_line(job.text) or T("No se encontró nada.",
                                                                                    "Nothing found."))
                else:
                    self.set_status(self.c1_status, "dim", "")
        self.run(cmd_importer(self.env, ["fuentes"]), T("Buscando juegos", "Looking for games"), done, quiet=True)

    def render_tiles(self):
        for w in self.tiles.winfo_children():
            w.destroy()
        srcs = self.app.cache.get("sources")
        if not srcs:
            hint(self.tiles, T("Buscando juegos…", "Looking for games…"), bg=C["card"]).pack(anchor="w")
            return
        cols = 4
        for c in range(cols):
            self.tiles.columnconfigure(c, weight=1, uniform="tiles")
        for i, s in enumerate(srcs):
            ready, dev = s["state"] == "listo", s["state"] == "desarrollo"
            sel = s["id"] == self.src
            bg = C["accent_soft"] if sel else C["frame"]
            t = tk.Frame(self.tiles, bg=bg, highlightthickness=2 if sel else 1,
                         highlightbackground=C["accent"] if sel else C["line"])
            t.grid(row=i // cols, column=i % cols, sticky="nsew", padx=4, pady=4)
            nm = T("Budokai 3 (mods de la comunidad)", "Budokai 3 (community mods)") if s["id"] == "b3" else s["name"]
            tk.Label(t, text=nm, bg=bg, fg=C["text"] if ready else C["dim"], font=F["bold"], anchor="w").pack(
                fill="x", padx=12, pady=(9, 0))
            st = ("✔  " + T("Listo para importar", "Ready to import")) if ready else \
                ("⚠  " + T("En desarrollo", "In development")) if dev else \
                ("✖  " + T("No encontrado en ps2_games", "Not found in ps2_games"))
            tk.Label(t, text=st, bg=bg, fg=C["ok"] if ready else C["warn"] if dev else C["dim"], font=F["small"],
                     anchor="w").pack(fill="x", padx=12, pady=(2, 9))
            Tooltip(t, source_note(s["id"]) + ("\n" + s["path"] if s["path"] else ""))
            make_clickable(t, lambda sid=s["id"]: self.pick_source(sid), bg, C["hover"] if not sel else bg)

    def pick_source(self, sid):
        s = next((x for x in self.app.cache.get("sources") or [] if x["id"] == sid), None)
        if not s:
            return
        if s["state"] != "listo":
            self.src = None
            self.render_tiles()
            self.set_status(self.c1_status, "warn", source_note(sid) + (
                "" if s["state"] == "desarrollo" else "  " + T("(no está en ps2_games)", "(not in ps2_games)")))
            return
        self.src, self.sel = sid, None
        self.set_status(self.c1_status, "dim", source_note(sid))
        self.render_tiles()
        self.search.set("")
        ent = (self.app.cache.get("entries") or {}).get(self._entries_key())
        if ent is not None:
            self.fill_entries()
            return
        self.tree.delete(*self.tree.get_children())
        self.count.configure(text=T("Leyendo el juego… (la primera vez puede tardar un poco)",
                                    "Reading the game… (the first time can take a moment)"))
        args = ["lista", sid]
        if self.adv and sid == "b3" and self.v_folder.get().strip():
            args += ["--carpeta", self.v_folder.get().strip()]
        key = self._entries_key()

        def done(job):
            lst = parse_entries(job.text)
            if job.rc == 0:
                self.app.cache.setdefault("entries", {})[key] = lst
            if self.alive and self.src == sid:
                self.fill_entries()
                if not lst:
                    self.set_status(self.c2_status, "err", last_line(job.text) or T("No se encontró nada.",
                                                                                    "Nothing found."))
        if not self.run(cmd_importer(self.env, args), T("Leyendo %s", "Reading %s") % s["name"], done, quiet=True):
            self.count.configure(text="")

    def _entries_key(self):
        extra = self.v_folder.get().strip() if self.adv and self.src == "b3" else ""
        return "%s|%s" % (self.src, extra)

    def fill_entries(self):
        self.tree.delete(*self.tree.get_children())
        lst = (self.app.cache.get("entries") or {}).get(self._entries_key()) or []
        needle = self.search.get().lower().strip()
        n = 0
        for i, e in enumerate(lst):
            if needle and needle not in e["name"].lower():
                continue
            if e["kind"] == "b1":
                note = T("%d modelos  ·  golpes, combos y gritos de B1", "%d models  ·  B1 moves, combos and yells") \
                    % e["count"]
            elif e["kind"] == "trajes":
                note = T("1 traje", "1 costume") if e["count"] == 1 else T("%d trajes", "%d costumes") % e["count"]
            elif e["kind"] == "sb":
                note = (T("1 modelo", "1 model") if e["count"] == 1 else T("%d formas", "%d forms") % e["count"]) + \
                    T("  ·  golpes de Shin Budokai", "  ·  Shin Budokai moves")
            else:
                note = T("1 modelo", "1 model") if e["count"] == 1 else T("%d formas", "%d forms") % e["count"]
            base = T("golpes propios (port de la comunidad)", "own moves (community port)") if e["port"] else (
                T("base: ", "base: ") + donor_name(e["donor"]) if e["donor"] >= 0 else T("base: automática",
                                                                                         "base: automatic"))
            self.tree.insert("", "end", iid=str(i), text="  " + e["name"], values=(note, base))
            n += 1
        self.count.configure(text=(T("%d personajes", "%d characters") % n) if self.src else "")

    def pick_entry(self):
        s = self.tree.selection()
        lst = (self.app.cache.get("entries") or {}).get(self._entries_key()) or []
        if not s or int(s[0]) >= len(lst):
            return
        self.sel = lst[int(s[0])]
        self.name.set(self.sel["suggested"] or self.sel["name"])
        self._set_donor_choices(self.sel)
        self.after_frame.pack_forget()
        self.set_status(self.c3_status, "dim", "")

    def _set_donor_choices(self, e):
        auto = T("Automático", "Automatic")
        if e and e["donor"] >= 0:
            auto += " (%s)" % donor_name(e["donor"])
        self.donor_cb.configure(values=donor_values(auto))
        self.donor.set(auto)

    def _name_changed(self):
        self.banner.set(self.name.get())

    def do_import(self):
        e, name = self.sel, self.name.get().strip()
        if not self.src or not e:
            self.set_status(self.c3_status, "warn", T("Elige primero un juego y un personaje.",
                                                      "Pick a game and a character first."))
            return
        if not name:
            self.set_status(self.c3_status, "warn", T("Escribe el nombre.", "Type the name."))
            return
        donor = donor_from_index(self.donor_cb.current(), True)
        extra = []
        mod = ""
        if self.adv:
            mod = self.mod.get().strip()
            si = self.slot_cb.current()
            if si > 0:
                extra += ["--id", str(FREE_SLOTS[si - 1][0])]
            ai = donor_from_index(max(0, self.after_cb.current()), True)
            if ai >= 0:
                extra += ["--despues-de", str(ai)]
            if self.src == "b3" and self.v_folder.get().strip():
                extra += ["--carpeta", self.v_folder.get().strip()]
            if mod and os.path.exists(os.path.join(self.env.mods, mod)):
                if not self.app.ask(T("La carpeta %s ya existe y se sobrescribirá. ¿Seguir?",
                                      "Folder %s already exists and will be overwritten. Continue?") % mod):
                    return
        if not mod:
            mod = unique_mod_name(self.env.mods, mod_slug(self.src, name))
        self.last_mod = mod
        cmd = cmd_import_character(self.env, self.src, e["key"], mod, name, donor, extra)

        def done(job):
            ok = "listo: " in job.text
            self.app.cache.pop("mods", None)
            if not self.alive:
                return
            if ok:
                self.set_status(self.c3_status, "ok", T(
                    "Importado en mods\\%s. Se añade al juego al pulsar JUGAR (o con «Reconstruir ahora»).",
                    "Imported into mods\\%s. It is added to the game when you press PLAY (or with 'Rebuild now').")
                                % mod)
                self.after_frame.pack(side="left")
            else:
                lvl, txt = self.app.outcome(job)
                self.set_status(self.c3_status, "err", T("No se pudo importar: ", "Import failed: ") + (
                    last_line(job.text) or txt))
        self.run(cmd, T("Importando %s", "Importing %s") % name, done, self.c3_status)

    def rebuild(self):
        self.run(cmd_roster_build(self.env, True), T("Reconstruir personajes nuevos", "Rebuild new characters"),
                 None, self.c3_status, T("Listo: personajes nuevos montados.", "Done: new characters built."))


# --------------------------------------------------------------------------------- personajes
class CharactersPage(Page):
    key = "characters"

    def build(self):
        self.header(T("Personajes nuevos", "New characters"),
                    T("Tus personajes en casillas NUEVAS de la rueda (no sustituyen a nadie). Cambia su nombre, su "
                      "plaza, sus imágenes y sus cápsulas. Todo se aplica al pulsar JUGAR o «Reconstruir ahora».",
                      "Your characters in NEW wheel cells (they replace nobody). Change their name, slot, images "
                      "and capsules. Everything applies on PLAY or 'Rebuild now'."))
        main = tk.Frame(self, bg=C["bg"])
        main.pack(fill="both", expand=True, padx=22, pady=(0, 12))
        left = tk.Frame(main, bg=C["bg"], width=340)
        left.pack(side="left", fill="y")
        left.pack_propagate(False)
        tf = tk.Frame(left, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
        tf.pack(fill="both", expand=True)
        self.tree = ttk.Treeview(tf, columns=("plaza",), show="tree headings", style="Icons.Treeview",
                                 selectmode="browse")
        self.tree.heading("#0", text=T("Personaje", "Character"))
        self.tree.heading("plaza", text=T("Plaza", "Slot"))
        self.tree.column("#0", width=230, minwidth=140)
        self.tree.column("plaza", width=80, anchor="center")
        vsb = ttk.Scrollbar(tf, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        self.tree.tag_configure("off", foreground=C["dim"])
        self.tree.tag_configure("bad", foreground=C["err"])
        self.tree.tag_configure("group", foreground=C["accent"])
        self.tree.bind("<<TreeviewSelect>>", lambda _e: self.show_editor(False))
        lb = tk.Frame(left, bg=C["bg"])
        lb.pack(fill="x", pady=(8, 0))
        self.toggle_btn = ttk.Button(lb, text=T("Activar / desactivar", "Enable / disable"), command=self.toggle)
        self.toggle_btn.pack(side="left")
        ttk.Button(lb, text="⟳  " + T("Reconstruir ahora", "Rebuild now"), style="Accent.TButton",
                   command=self.rebuild).pack(side="left", padx=6)
        self.slots_lbl = label(left, "", fg=C["dim"], font=F["small"], wrap=330)
        self.slots_lbl.pack(fill="x", pady=(6, 0))
        if self.adv:
            ab = tk.Frame(left, bg=C["bg"])
            ab.pack(fill="x", pady=(4, 0))
            ttk.Button(ab, text=T("Estado de plazas", "Slot status"), style="Small.TButton",
                       command=lambda: self.run(cmd_roster_status(self.env), T("Estado de plazas", "Slot status"),
                                                None, self.status)).pack(side="left")
            ttk.Button(ab, text=T("Construir solo si cambió", "Build only if changed"), style="Small.TButton",
                       command=lambda: self.run(cmd_roster_build(self.env, False), "roster_build construir",
                                                lambda j: self.alive and self.reload(), self.status)).pack(
                side="left", padx=6)
        self.status = self.status_label(left)
        self.status.configure(bg=C["bg"], wraplength=330)
        self.status.pack(fill="x", pady=(6, 0))
        self.right = ScrollFrame(main)
        self.right.pack(side="left", fill="both", expand=True, padx=(14, 0))
        self.chars, self.trajes, self.slots, self.images, self.prev = [], [], {}, {}, []
        self.want, self.ed_status = None, None

    def on_show(self):
        self.reload()

    def focus_item(self, mod=None, **_):
        self.want = mod
        self.reload()

    def reload(self):
        mods = scan_mods(self.env.mods)
        self.chars, self.trajes = [], []
        for m in mods:
            if m["type"] == "personaje" and m["toml"]:
                data = read_toml(m["toml"])
                p = data.get("personaje", {})
                m["data"], m["caps"] = p, data.get("capsula", []) or []
                self.chars.append(m)
            elif m["type"] == "traje" and m["toml"]:
                m["data"] = read_toml(m["toml"]).get("traje", {})
                self.trajes.append(m)
        self.chars.sort(key=lambda m: m["name"])
        self.slots = assign_slots([(m["name"], m["enabled"], m["data"].get("id") if isinstance(
            m["data"].get("id"), int) else -1) for m in self.chars])
        sel = self.want or (self.tree.selection()[0] if self.tree.selection() else None)
        self.want = None
        self.tree.delete(*self.tree.get_children())
        self.images = {}
        if not self.chars and not self.trajes:
            self.tree.insert("", "end", iid="__none", text="  " + T("Ninguno todavía", "None yet"))
        for m in self.chars:
            img = load_png(m["icon"], 42, 42) if m["icon"] else None
            self.images[m["name"]] = img
            sid = self.slots.get(m["name"], -1)
            plaza = "-" if not m["enabled"] else (str(sid) if sid >= 0 else T("sin plaza", "no slot"))
            tags = ("off",) if not m["enabled"] else ("bad",) if sid < 0 else ()
            mark = "✔ " if m["enabled"] else "✖ "
            kw = {"image": img} if img else {}
            self.tree.insert("", "end", iid=m["name"], text="  " + mark + (m["display"] or m["name"]),
                             values=(plaza,), tags=tags, **kw)
        if self.trajes:
            self.tree.insert("", "end", iid="__trajes", text="  " + T("Trajes extra", "Extra costumes"), open=True,
                             tags=("group",))
            for m in self.trajes:
                mark = "✔ " if m["enabled"] else "✖ "
                self.tree.insert("__trajes", "end", iid=m["name"], text="  " + mark + (m["display"] or m["name"]),
                                 values=(donor_name(m["data"].get("personaje", -1)),),
                                 tags=() if m["enabled"] else ("off",))
        used = sum(1 for v in self.slots.values() if v >= 0)
        lines = [T("%d de %d plazas ocupadas.", "%d of %d slots taken.") % (used, len(FREE_SLOTS))]
        for m in self.chars:
            if not m["enabled"]:
                continue
            got, req = self.slots.get(m["name"], -1), m["data"].get("id")
            if got < 0:
                lines.append(T("%s no cabe: no quedan plazas (desactiva otro).",
                               "%s does not fit: no slots left (disable another).") % m["name"])
            elif isinstance(req, int) and req >= 0 and req != got:
                lines.append(T("%s pide la plaza %d (ocupada): usará la %d.", "%s asks for slot %d (taken): it will "
                               "use %d.") % (m["name"], req, got))
        self.slots_lbl.configure(text="\n".join(lines), fg=C["warn"] if len(lines) > 1 else C["dim"])
        if sel and self.tree.exists(sel):
            self.tree.selection_set(sel)
            self.tree.see(sel)
        elif self.chars:
            self.tree.selection_set(self.chars[0]["name"])
        self.show_editor(True)

    def current(self):
        s = self.tree.selection()
        if not s:
            return None
        return next((m for m in self.chars + self.trajes if m["name"] == s[0]), None)

    def toggle(self):
        m = self.current()
        if not m:
            return
        try:
            set_mod_enabled(self.env.mods, m["name"], not m["enabled"])
        except OSError as ex:
            self.set_status(self.status, "err", str(ex))
            return
        self.app.log_text("%s: %s\n" % (m["name"], T("activado", "enabled") if not m["enabled"] else
                                        T("desactivado", "disabled")), "ok")
        self.set_status(self.status, "ok", T("Hecho. Se aplica al pulsar JUGAR o «Reconstruir ahora».",
                                             "Done. Applies on PLAY or 'Rebuild now'."))
        self.want = m["name"]
        self.reload()

    def rebuild(self):
        self.run(cmd_roster_build(self.env, True), T("Reconstruir personajes nuevos", "Rebuild new characters"),
                 lambda j: self.alive and self.reload(), self.status,
                 T("Listo: personajes nuevos montados.", "Done: new characters built."))

    def preview(self, m, extra=()):
        self.want = m["name"]
        self.run(cmd_roster_preview(self.env, m["name"], extra), T("Vista previa de %s", "Preview of %s") % m["name"],
                 lambda j: self.alive and self.reload(), self.ed_status, T("Vista previa actualizada.",
                                                                           "Preview updated."))

    def caps(self, m, extra, confirm=None):
        if confirm and not self.app.ask(confirm):
            return
        self.want = m["name"]
        self.run(cmd_capsules(self.env, m["name"], extra), T("Cápsulas de %s", "Capsules of %s") % m["name"],
                 lambda j: self.alive and self.reload(), self.ed_status, T("Cápsulas guardadas.", "Capsules saved."))

    def show_editor(self, force=True):
        m = self.current()
        shown = m["name"] if m else None
        if not force and shown == getattr(self, "_shown", "?"):
            return             # el <<TreeviewSelect>> que sigue a reload(): ya esta dibujado
        self._shown = shown
        body = self.right.inner
        for w in body.winfo_children():
            w.destroy()
        self.prev = []
        if not m:
            c = Card(body)
            c.pack(fill="x")
            label(c.body, T("Elige un personaje de la lista. Para añadir uno nuevo usa «Importar personaje» o «Crear "
                            "con mis modelos».", "Pick a character from the list. To add one use 'Import a "
                            "character' or 'Create from my models'."), fg=C["dim"], bg=C["card"], wrap=640).pack(
                fill="x")
            bf = tk.Frame(c.body, bg=C["card"])
            bf.pack(fill="x", pady=(10, 0))
            ttk.Button(bf, text=T("Importar personaje", "Import a character"), style="Accent.TButton",
                       command=lambda: self.app.goto("importer")).pack(side="left")
            ttk.Button(bf, text=T("Crear con mis modelos", "Create from my models"),
                       command=lambda: self.app.goto("create")).pack(side="left", padx=6)
            return
        if m["type"] == "traje":
            self._traje_editor(body, m)
            return
        d = m["data"]
        vista = os.path.join(m["dir"], "ui", "_vista")
        own = bool(d.get("camara"))
        c = Card(body)
        c.pack(fill="x", pady=(0, 12))
        top = tk.Frame(c.body, bg=C["card"])
        top.pack(fill="x")
        label(top, m["display"] or m["name"], font=F["h2"], fg=C["accent"], bg=C["card"]).pack(side="left")
        info = T("carpeta %s, moveset propio", "folder %s, own moveset") % m["name"] if own else \
            T("carpeta %s, moveset de %s", "folder %s, %s moveset") % (m["name"], donor_name(d.get("donante", 21)))
        label(top, "   (" + info + ")", fg=C["dim"], bg=C["card"]).pack(side="left")
        ttk.Button(top, text=T("Abrir carpeta", "Open folder"), style="Small.TButton",
                   command=lambda: open_path(m["dir"])).pack(side="right")
        if self.adv:
            ttk.Button(top, text=T("Ajustes…", "Settings…"), style="Small.TButton",
                       command=lambda: TomlFormDialog(self.app, m["toml"], self.reload)).pack(side="right", padx=6)
        sb = ttk.Button(top, text=icon("studio") + "  " + T("Cámaras (Studio)", "Cameras (Studio)"),
                        style="Small.TButton", command=lambda: self.app.open_studio(m["name"]))
        sb.pack(side="right", padx=(0, 6))
        if not own:
            sb.state(["disabled"])
            Tooltip(sb, T("Usa las cámaras de su personaje base: para cámaras propias necesita su propio moveset "
                          "(camara.bin). Las de su personaje base se editan eligiéndolo en el Studio.",
                          "It uses its base character's cameras: own cameras need its own moveset (camara.bin). Edit "
                          "the base character's by picking it in the Studio."))
        pv = tk.Frame(c.body, bg=C["card"])
        pv.pack(fill="x", pady=(10, 4))
        for fn, mw, mh, cap in (("icono.png", 84, 84, T("Icono", "Icon")), ("rotulo.png", 200, 50, T("Rótulo",
                                                                                                     "Banner")),
                                ("retrato_p1.png", 110, 140, "P1"), ("retrato_p2.png", 110, 140, "P2"),
                                ("hud.png", 120, 70, T("Barra de vida", "Health bar"))):
            f = tk.Frame(pv, bg=C["frame"], highlightthickness=1, highlightbackground=C["line"])
            f.pack(side="left", padx=(0, 8), anchor="n")
            img = load_png(os.path.join(vista, fn), mw, mh)
            self.prev.append(img)
            if img:
                tk.Label(f, image=img, bg=C["frame"]).pack(padx=4, pady=4)
            else:
                tk.Label(f, text="—", bg=C["frame"], fg=C["dim"], width=8, height=3).pack(padx=4, pady=4)
            tk.Label(f, text=cap, bg=C["frame"], fg=C["dim"], font=F["small"]).pack(pady=(0, 4))
        bf = tk.Frame(c.body, bg=C["card"])
        bf.pack(fill="x", pady=(6, 0))
        ttk.Button(bf, text="⟳  " + T("Regenerar vista previa", "Refresh preview"), style="Small.TButton",
                   command=lambda: self.preview(m)).pack(side="left")
        self.ed_status = self.status_label(bf)
        self.ed_status.pack(side="left", padx=10, fill="x", expand=True)

        c2 = Card(body, T("Nombre y casilla", "Name and cell"))
        c2.pack(fill="x", pady=(0, 12))
        g = c2.body
        g.columnconfigure(1, weight=1)
        name = tk.StringVar(value=str(d.get("nombre", "")))
        label(g, T("Nombre", "Name"), bg=C["card"]).grid(row=0, column=0, sticky="w", padx=(0, 12), pady=4)
        nf = tk.Frame(g, bg=C["card"])
        nf.grid(row=0, column=1, sticky="w")
        ttk.Entry(nf, textvariable=name, width=30).pack(side="left")
        ttk.Button(nf, text=T("Guardar nombre", "Save name"), style="Small.TButton",
                   command=lambda: name.get().strip() and self.preview(
                       m, ["--nombre", name.get().strip(), "--guardar", "--solo", "icono"])).pack(side="left", padx=6)
        bp = BannerPreview(g, 360, 42)
        bp.grid(row=1, column=1, sticky="w")
        bp.set(name.get())
        name.trace_add("write", lambda *_: bp.set(name.get()))
        label(g, T("Plaza", "Slot"), bg=C["card"]).grid(row=2, column=0, sticky="w", padx=(0, 12), pady=4)
        got = self.slots.get(m["name"], -1)
        owners = {v: k for k, v in self.slots.items() if k != m["name"]}
        slot_vals = [T("Automática (primera libre)", "Automatic (first free)")] + [
            slot_label(s, o, owners.get(s, "")) for s, o in FREE_SLOTS]
        slot = tk.StringVar()
        req = d.get("id") if isinstance(d.get("id"), int) else -1
        slot.set(slot_vals[0] if req < 0 else next((v for v in slot_vals[1:] if v.startswith("%d " % req)),
                                                     slot_vals[0]))
        cb = ttk.Combobox(g, textvariable=slot, values=slot_vals, state="readonly", width=44, height=20)
        cb.grid(row=2, column=1, sticky="w", pady=4)

        def slot_changed(_e=None):
            i = cb.current()
            sid = -1 if i <= 0 else FREE_SLOTS[i - 1][0]
            self.preview(m, ["--id", str(sid), "--guardar", "--solo", "icono"])
        cb.bind("<<ComboboxSelected>>", slot_changed)
        hint(g, T("Ahora: %s", "Now: %s") % (str(got) if got >= 0 else T("sin plaza", "no slot")) if m["enabled"] else
             T("Desactivado: no ocupa plaza.", "Disabled: takes no slot."), bg=C["card"]).grid(row=2, column=2,
                                                                                            sticky="w", padx=8)
        label(g, T("Casilla tras", "Cell after"), bg=C["card"]).grid(row=3, column=0, sticky="w", padx=(0, 12), pady=4)
        auto = T("Automática (tras su personaje base)", "Automatic (after its base character)")
        after_vals = donor_values(auto)
        after = tk.StringVar()
        a = d.get("despues_de")
        after.set(after_vals[donor_index(a, True)] if isinstance(a, int) and a >= 0 else auto)
        acb = ttk.Combobox(g, textvariable=after, values=after_vals, state="readonly", width=44, height=20)
        acb.grid(row=3, column=1, sticky="w", pady=4)
        acb.bind("<<ComboboxSelected>>", lambda _e: self.preview(
            m, ["--despues-de", str(donor_from_index(acb.current(), True)), "--guardar", "--solo", "icono"]))

        self._forms_card(body, m, own)
        self._colors_card(body, m)
        self._ult_card(body, m)

        c3 = Card(body, T("Imágenes", "Images"), T(
            "El icono y los retratos se generan desde el modelo 3D con el estilo del juego. Puedes usar tu arte.",
            "The icon and portraits are rendered from the 3D model in the game's style. You can use your own art."))
        c3.pack(fill="x", pady=(0, 12))
        g = c3.body
        art = tk.StringVar()
        af = tk.Frame(g, bg=C["card"])
        af.pack(fill="x")
        label(af, T("Tu imagen (PNG)", "Your image (PNG)"), bg=C["card"]).pack(side="left", padx=(0, 8))
        PathEntry(af, art, "file", [("PNG", "*.png"), (T("Imágenes", "Images"), "*.png *.jpg *.jpeg *.webp"),
                                    (T("Todos", "All"), "*.*")], width=40, bg=C["card"]).pack(side="left")
        bf = tk.Frame(g, bg=C["card"])
        bf.pack(fill="x", pady=(6, 0))
        for txt, flag, key in ((T("Como cara (icono)", "As face (icon)"), "--cara", "icono"),
                               (T("Como icono terminado", "As finished icon"), "--icono", "icono"),
                               (T("Como retrato", "As portrait"), "--retrato", "retrato")):
            ttk.Button(bf, text=txt, style="Small.TButton",
                       command=lambda f=flag, k=key: art.get().strip() and self.preview(
                           m, [f, art.get().strip(), "--guardar", "--solo", k])).pack(side="left", padx=(0, 6))
        orig = os.path.isfile(os.path.join(m["dir"], "ui", "icono.png")) or os.path.isfile(
            os.path.join(m["dir"], "ui", "retrato_p1.png"))
        of = tk.Frame(g, bg=C["card"])
        of.pack(fill="x", pady=(8, 0))
        if orig:
            label(of, T("Trae imágenes propias:", "Ships its own images:"), bg=C["card"]).pack(side="left")
            ttk.Button(of, text=T("Conservar las originales", "Keep the originals"), style="Small.TButton",
                       command=lambda: self.preview(m, ["--imagenes", "originales", "--guardar"])).pack(side="left",
                                                                                                      padx=6)
        ttk.Button(of, text=T("Generar desde el modelo 3D", "Render from the 3D model"), style="Small.TButton",
                   command=lambda: self.preview(m, ["--imagenes", "modelo", "--guardar"])).pack(side="left")
        if self.adv:
            for key, title, yaw in (("icono", T("Icono de la rueda", "Wheel icon"), 3.0),
                                    ("retrato", T("Retratos P1/P2", "P1/P2 portraits"), 35.0)):
                fr = tk.Frame(g, bg=C["card"])
                fr.pack(fill="x", pady=(10, 0))
                label(fr, title, font=F["bold"], bg=C["card"], width=18).pack(side="left")
                src = tk.StringVar(value=str(d.get(key + "_fuente", "") or "modelo"))
                ttk.Combobox(fr, textvariable=src, values=["modelo", "imagen", "terminado"], state="readonly",
                             width=11).pack(side="left")
                adj = d.get(key + "_ajuste") or [1.0, 0, 0, yaw]
                av = tk.StringVar(value=",".join("%g" % float(x) for x in adj))
                label(fr, "  zoom,dx,dy,giro", fg=C["dim"], bg=C["card"], font=F["small"]).pack(side="left")
                ttk.Entry(fr, textvariable=av, width=18).pack(side="left", padx=4)
                ttk.Button(fr, text=T("Aplicar", "Apply"), style="Small.TButton",
                           command=lambda k=key, s=src, v=av: self.preview(
                               m, ["--%s-fuente" % k, s.get(), "--%s-ajuste" % k, v.get().replace(" ", ""),
                                   "--guardar", "--solo", k])).pack(side="left", padx=4)

        c4 = Card(body, T("Cápsulas (habilidades)", "Capsules (skills)"))
        c4.pack(fill="x", pady=(0, 12))
        g = c4.body
        caps = m["caps"]
        if not caps:
            label(g, T("Usa las cápsulas de su personaje base (%s). Añade cápsulas propias para darle nombres propios y una "
                       "transformación con su propia cápsula.", "Uses its base character's capsules (%s). Add own capsules "
                       "for its own names and a transformation with its own capsule.") % donor_name(
                d.get("donante", 21)), fg=C["dim"], bg=C["card"], wrap=700).pack(fill="x")
        else:
            label(g, T("Las especiales y definitivas sustituyen, en orden, a las de su personaje base; una transformación "
                       "lleva su propia cápsula.", "Specials and ultimates replace its base character's in order; a "
                       "transformation gets its own capsule.") if not own else T(
                "Port con moveset propio: sus especiales y su definitiva se ligan, en este orden, a estas cápsulas.",
                "Port with its own moveset: its specials and ultimate are linked, in this order, to these capsules."),
                fg=C["dim"], bg=C["card"], wrap=700).pack(fill="x")
        kinds = {"especial": T("Especial", "Special"), "definitiva": T("Definitiva", "Ultimate"),
                 "transformacion": T("Transformación", "Transformation")}
        if caps:
            ct = ttk.Treeview(g, columns=("tipo",), show="tree headings", height=min(8, max(2, len(caps))),
                              selectmode="browse")
            ct.heading("#0", text=T("Nombre", "Name"))
            ct.heading("tipo", text=T("Tipo", "Type"))
            ct.column("#0", width=320)
            ct.column("tipo", width=220)
            for i, cp in enumerate(caps):
                t = kinds.get(cp.get("tipo", "especial"), cp.get("tipo", "?"))
                if cp.get("tipo") == "transformacion":
                    t += T(" (forma %s)", " (form %s)") % cp.get("forma", 1)
                    if cp.get("ki") is not None:
                        t += T(", %s barras", ", %s bars") % cp["ki"]
                ct.insert("", "end", iid=str(i), text="  %d. %s" % (i, cp.get("nombre", "?")), values=(t,))
            ct.pack(fill="x", pady=(8, 4))
            cb_ = tk.Frame(g, bg=C["card"])
            cb_.pack(fill="x")

            def sel_idx():
                s = ct.selection()
                return int(s[0]) if s else -1
            ttk.Button(cb_, text="▲ " + T("Subir", "Up"), style="Small.TButton",
                       command=lambda: sel_idx() > 0 and self.caps(m, ["--subir", str(sel_idx())])).pack(side="left")
            ttk.Button(cb_, text="✖ " + T("Quitar", "Remove"), style="Small.TButton",
                       command=lambda: sel_idx() >= 0 and self.caps(
                           m, ["--quitar", str(sel_idx())], T("¿Quitar la cápsula seleccionada?",
                                                              "Remove the selected capsule?"))).pack(side="left",
                                                                                                    padx=6)
            kv = tk.StringVar(value="4")
            label(cb_, "   " + T("Ki (barras):", "Ki (bars):"), bg=C["card"]).pack(side="left")
            ttk.Spinbox(cb_, from_=0, to=7, width=3, textvariable=kv).pack(side="left", padx=4)

            def set_ki():
                i = sel_idx()
                if i < 0 or caps[i].get("tipo") != "transformacion":
                    self.set_status(self.ed_status, "warn", T("Elige una cápsula de transformación.",
                                                              "Pick a transformation capsule."))
                    return
                self.caps(m, ["--ki", str(i), kv.get().strip() or "4"])
            kb = ttk.Button(cb_, text=T("Fijar ki", "Set ki"), style="Small.TButton", command=set_ki)
            kb.pack(side="left")
            Tooltip(kb, T("Barras de ki que hay que TENER para transformarse (no se gastan). Sale en la ficha de "
                          "la pausa y en Edit Skills.", "Ki bars you must HAVE to transform (they are not spent). "
                          "Shown in the pause list and in Edit Skills."))
            if self.adv:
                rn = tk.StringVar()
                ttk.Entry(cb_, textvariable=rn, width=22).pack(side="left", padx=(12, 4))
                ttk.Button(cb_, text=T("Renombrar", "Rename"), style="Small.TButton",
                           command=lambda: sel_idx() >= 0 and rn.get().strip() and self.caps(
                               m, ["--renombrar", str(sel_idx()), rn.get().strip()])).pack(side="left")
        add = tk.Frame(g, bg=C["card"])
        add.pack(fill="x", pady=(10, 0))
        cname, ckind, cform = tk.StringVar(), tk.StringVar(value=kinds["especial"]), tk.StringVar(value="1")
        label(add, T("Nueva:", "New:"), bg=C["card"]).pack(side="left")
        ttk.Entry(add, textvariable=cname, width=26).pack(side="left", padx=6)
        kc = ttk.Combobox(add, textvariable=ckind, values=list(kinds.values()), state="readonly", width=15)
        kc.pack(side="left")
        label(add, T(" forma", " form"), bg=C["card"]).pack(side="left")
        ttk.Spinbox(add, from_=1, to=7, textvariable=cform, width=4).pack(side="left", padx=4)

        def add_cap():
            nm = cname.get().strip()
            if not nm:
                return
            kid = list(kinds)[max(0, kc.current())]
            args = ["--anadir", nm, kid] + ([cform.get().strip() or "1"] if kid == "transformacion" else [])
            self.caps(m, args)
        ttk.Button(add, text="✚ " + T("Añadir cápsula", "Add capsule"), style="Small.TButton",
                   command=add_cap).pack(side="left", padx=6)
        if own:
            imp = tk.Frame(g, bg=C["card"])
            imp.pack(fill="x", pady=(10, 0))
            games = [("auto", T("Detectar", "Detect")), ("iw", "Infinite World"), ("b1", "Budokai 1"),
                     ("b2", "Budokai 2"), ("b3", "Budokai 3")]
            gv = tk.StringVar(value=games[0][1])
            gcb = ttk.Combobox(imp, textvariable=gv, values=[x[1] for x in games], state="readonly", width=16)
            gcb.pack(side="left")
            lst, cat = tk.StringVar(), tk.StringVar()
            if self.adv:
                label(imp, " --lista", fg=C["dim"], bg=C["card"]).pack(side="left")
                PathEntry(imp, lst, "file", width=18, bg=C["card"]).pack(side="left", padx=4)
                label(imp, " --catalogo", fg=C["dim"], bg=C["card"]).pack(side="left")
                PathEntry(imp, cat, "file", width=18, bg=C["card"]).pack(side="left", padx=4)

            def do_imp():
                args = ["--importar", games[max(0, gcb.current())][0]]
                if lst.get().strip():
                    args += ["--lista", lst.get().strip()]
                if cat.get().strip():
                    args += ["--catalogo", cat.get().strip()]
                self.caps(m, args)
            ttk.Button(imp, text=T("Traer las cápsulas de su juego", "Bring capsules from its game"),
                       style="Small.TButton", command=do_imp).pack(side="left", padx=6)
            hint(g, T("Crea una cápsula por cada ataque que pida su moveset. Si ya tenía, se guarda una copia "
                      "(personaje.toml.antes_de_importar).", "Creates one capsule per attack its moveset needs. "
                      "Existing ones are backed up (personaje.toml.antes_de_importar)."), bg=C["card"]).pack(
                fill="x", pady=(4, 0))
        self.right.top()

    def _forms_card(self, body, m, own):
        """Formas, ki base, fisica y transformacion (claves de docs/03_formatos/FORMAS_Y_KI.md)."""
        d = m["data"]
        c = Card(body, T("Formas, física y aspecto", "Forms, physics and look"), T(
            "Vacío = lo del donante. Las formas no pueden superar las del donante.",
            "Empty = the donor's. Forms cannot exceed the donor's."))
        c.pack(fill="x", pady=(0, 12))
        g = c.body
        g.columnconfigure(1, weight=1)

        def lst(v):
            return ", ".join(str(x) for x in v) if isinstance(v, list) else ("" if v is None else str(v))
        fv = tk.StringVar(value=lst(d.get("formas")))
        kv = tk.StringVar(value=lst(d.get("ki_base")))
        mv = tk.StringVar(value=lst(d.get("modelo_forma")))
        phys = d.get("fisica")
        fis_vals = [T("Sin física (rígido)", "No physics (rigid)"), T("La de su personaje base", "Its base character's")] + \
            donor_values()
        pv = tk.StringVar(value=fis_vals[1] if phys == "donante" else fis_vals[donor_index(phys, False) + 2]
                          if isinstance(phys, int) and any(x[0] == phys for x in DONORS) else fis_vals[0])
        tv = tk.BooleanVar(value=d.get("transformacion") == "donante")
        rows = ((T("Formas", "Forms"), ttk.Spinbox(g, from_=1, to=8, width=5, textvariable=fv),
                 T("vacío = las de su personaje base (%s)", "empty = its base character's (%s)") % donor_name(d.get("donante", 21))),
                (T("Ki base por forma", "Base ki per form"), ttk.Entry(g, textvariable=kv, width=18),
                 T("barras a las que tiende cada forma, p. ej. 3, 4, 4, 5",
                   "bars each form drifts to, e.g. 3, 4, 4, 5")),
                (T("Modelo por forma", "Model per form"), ttk.Entry(g, textvariable=mv, width=18),
                 T("qué modelo del traje usa cada forma, p. ej. 0, 1, 2, 3", "which costume model each form uses, "
                   "e.g. 0, 1, 2, 3")),
                (T("Física de pelo y cinturón", "Hair and belt physics"),
                 ttk.Combobox(g, textvariable=pv, values=fis_vals, state="readonly", width=30),
                 T("sin física las colas del cinturón quedan rígidas", "without physics belt tails stay rigid")))
        for r, (txt, w, h) in enumerate(rows):
            label(g, txt, bg=C["card"]).grid(row=r, column=0, sticky="w", padx=(0, 12), pady=3)
            w.grid(row=r, column=1, sticky="w")
            hint(g, h, bg=C["card"], wrap=420).grid(row=r, column=2, sticky="w", padx=8)
        cb = ttk.Checkbutton(g, text=T("Transformación de su personaje base en sus golpes propios (P+K+G)",
                                       "Donor's transformation in its own moveset (P+K+G)"), variable=tv,
                             style="Card.TCheckbutton")
        cb.grid(row=len(rows), column=0, columnspan=3, sticky="w", pady=(6, 0))
        if not own:
            cb.state(["disabled"])
        st = self.status_label(g)

        def save():
            vals = {}
            try:
                for key, var, n_max in (("formas", fv, 8), ("ki_base", kv, 7), ("modelo_forma", mv, 7)):
                    nums = [int(x) for x in re.split(r"[\s,;\[\]]+", var.get()) if x != ""]
                    if any(not 0 <= x <= n_max for x in nums) or (key == "formas" and len(nums) > 1):
                        raise ValueError(key)
                    vals[key] = None if not nums else str(nums[0]) if key == "formas" else \
                        "[%s]" % ", ".join(map(str, nums))
            except ValueError as ex:
                self.set_status(st, "err", T("Valor no válido en %s", "Invalid value in %s") % ex)
                return
            i = fis_vals.index(pv.get()) if pv.get() in fis_vals else 0
            vals["fisica"] = None if i == 0 else '"donante"' if i == 1 else str(DONORS[i - 2][0])
            vals["transformacion"] = '"donante"' if tv.get() and own else None
            try:
                bk = backup_file(m["toml"], self.env)
                toml_set_keys(m["toml"], "personaje", vals)
            except OSError as ex:
                self.set_status(st, "err", str(ex))
                return
            self.app.log_text(T("Guardado %s (copia en %s)\n", "Saved %s (backup at %s)\n") % (m["toml"], bk), "ok")
            self.want = m["name"]
            self.reload()
        bf = tk.Frame(g, bg=C["card"])
        bf.grid(row=len(rows) + 1, column=0, columnspan=3, sticky="w", pady=(8, 0))
        ttk.Button(bf, text=T("Guardar", "Save"), style="Small.TButton", command=save).pack(side="left")
        st.grid(row=len(rows) + 2, column=0, columnspan=3, sticky="ew")
        hint(g, T("Aspecto: el brillo HD de borde (rim light) se regula para todo el juego en el launcher, "
                  "pestaña «Mods nativos» → «Brillo HD de los personajes» (y en el menú F1). Si un modelo sale "
                  "plano, sin sombra toon, sus texturas tienen alfa 255 («sin sombrear»): conviértelo con alfa 0 "
                  "y rampa por material.",
                  "Look: the HD rim light is set for the whole game in the launcher, 'Native mods' tab → 'HD "
                  "shine on characters' (also in the F1 menu). If a model looks flat, without toon shading, its "
                  "textures have alpha 255 ('unshaded'): convert it with alpha 0 and a ramp per material."),
             bg=C["card"], wrap=820).grid(row=len(rows) + 3, column=0, columnspan=3, sticky="w", pady=(8, 0))

    def _ult_card(self, body, m):
        """Definitiva: la de su personaje base o, si su juego de origen tenia otra (B1, SB...), con
        sus animaciones dentro de esa cinematica (definitiva_animaciones); y su nombre."""
        d, mdir = m["data"], os.path.dirname(m["toml"])
        rel = d.get("definitiva_animaciones") or "moveset/definitiva.json"
        has_rec = os.path.isfile(os.path.join(mdir, rel))
        c = Card(body, T("Definitiva (WIP)", "Ultimate (WIP)"), T(
            "En modo hiper: P+K+G+E. La cámara, el rival y los efectos son los de la definitiva de su personaje "
            "base; si su juego de origen tenía otra, sus animaciones van dentro. Primera versión: puede fallar.",
            "In hyper mode: P+K+G+E. Camera, opponent and effects come from its base character's ultimate; "
            "if its original game had another one, its animations go inside. First version: may have bugs."))
        c.pack(fill="x", pady=(0, 12))
        g = c.body
        st = self.status_label(g)
        if has_rec:
            try:
                with open(os.path.join(mdir, rel), encoding="utf-8") as fh:
                    n = sum(1 for k in json.load(fh) if not str(k).startswith("_"))
            except (OSError, ValueError):
                n = 0
            var = tk.BooleanVar(value=bool(d.get("definitiva_animaciones")))

            def toggle():
                try:
                    bk = backup_file(m["toml"], self.env)
                    toml_set_keys(m["toml"], "personaje", {"definitiva_animaciones": toml_literal(rel)
                                                           if var.get() else None})
                except OSError as ex:
                    self.set_status(st, "err", str(ex))
                    return
                self.app.log_text(T("Guardado %s (copia en %s). Se aplica al montar el roster.\n",
                                    "Saved %s (backup at %s). Applied when the roster is built.\n") % (m["toml"], bk),
                                  "ok")
                self.want = m["name"]
                self.reload()
            ttk.Checkbutton(g, text=T("Con sus animaciones originales (%d momentos de la cinemática de %s)",
                                      "With its original animations (%d moments of %s's cinematic)") % (
                n, donor_name(d.get("donante", 21))), variable=var, command=toggle).pack(anchor="w")
        else:
            label(g, T("La de su personaje base (%s).", "Its base character's (%s).") % donor_name(
                d.get("donante", 21)), fg=C["dim"], bg=C["card"]).pack(anchor="w")
        ults = [(i, cc) for i, cc in enumerate(m["caps"]) if cc.get("tipo") == "definitiva"]
        if ults:
            i, cap = ults[0]
            row = tk.Frame(g, bg=C["card"])
            row.pack(fill="x", pady=(6, 0))
            label(row, T("Nombre", "Name"), bg=C["card"], width=10).pack(side="left")
            nm = tk.StringVar(value=cap.get("nombre", ""))
            ttk.Entry(row, textvariable=nm, width=34).pack(side="left")
            ttk.Button(row, text=T("Renombrar", "Rename"), style="Small.TButton", command=lambda: self.caps(
                m, ["--renombrar", str(i), nm.get().strip()])).pack(side="left", padx=6)
        st.pack(fill="x")

    def _colors_card(self, body, m):
        """Color del aura y del ki (aura_color / ki_color, ver colores.py): presets + selector."""
        import colorsys  # noqa: PLC0415
        d = m["data"]
        c = Card(body, T("Colores del aura y del ki", "Aura and ki colors"), T(
            "Cambia el tono de su aura y de los efectos de sus técnicas (rayos, ráfagas). Lo blanco se queda blanco.",
            "Changes the hue of its aura and of its techniques' effects (beams, blasts). White stays white."))
        c.pack(fill="x", pady=(0, 12))
        g = c.body
        donor = T("Como su personaje base (%s)", "Like its base character (%s)") % donor_name(d.get("donante", 21))
        custom = T("Personalizado…", "Custom…")
        names = [(n, T(es, en), h) for n, es, en, h in COLOR_PRESETS]

        def swatch_hex(v):
            h = None if v is None else next((x[2] for x in names if x[0] == v), None)
            if h is None and isinstance(v, int):
                h = v
            if h is None and isinstance(v, str) and v.startswith("#"):
                return v
            if h is None:
                return C["card"]
            r, g_, b = colorsys.hsv_to_rgb(h / 360.0, 0.85, 1.0)
            return "#%02x%02x%02x" % (int(r * 255), int(g_ * 255), int(b * 255))
        vals = [donor] + [x[1] for x in names] + [custom]
        st = self.status_label(g)
        chosen = {}
        for r, (key, txt) in enumerate((("aura_color", T("Aura", "Aura")), ("ki_color", T("Ki y técnicas",
                                                                                         "Ki and techniques")))):
            cur = d.get(key)
            chosen[key] = cur
            label(g, txt, bg=C["card"], width=16).grid(row=r, column=0, sticky="w", pady=3)
            var = tk.StringVar(value=donor if cur is None else next((x[1] for x in names if x[0] == cur),
                                                                    "%s %s" % (custom, cur)))
            cb = ttk.Combobox(g, textvariable=var, values=vals, state="readonly", width=26)
            cb.grid(row=r, column=1, sticky="w")
            sw = tk.Label(g, width=4, bg=swatch_hex(cur), relief="solid", bd=1)
            sw.grid(row=r, column=2, sticky="w", padx=8)

            def picked(_e=None, k=key, cb=cb, sw=sw, var=var):
                i = cb.current()
                if i == 0:
                    chosen[k] = None
                elif i == len(vals) - 1:
                    from tkinter import colorchooser  # noqa: PLC0415
                    rgb, hx = colorchooser.askcolor(parent=self, title=T("Elige un color", "Pick a color"))
                    if not hx:
                        return
                    rr, gg, bb = (x / 255 for x in rgb)
                    if colorsys.rgb_to_hsv(rr, gg, bb)[1] < 0.15:
                        self.set_status(st, "warn", T("Elige un color con más color: los grises no cambian el tono.",
                                                      "Pick a more colorful color: greys do not change the hue."))
                        return
                    chosen[k] = hx.lower()
                    var.set("%s %s" % (custom, hx.lower()))
                else:
                    chosen[k] = names[i - 1][0]
                sw.configure(bg=swatch_hex(chosen[k]))
            cb.bind("<<ComboboxSelected>>", picked)

        # aura de otro personaje (aura_de): fusiones del juego o cualquier personaje
        specials = [("gogeta", "Gogeta"), ("gogeta_ssj4", "Gogeta SSJ4"), ("vegito", "Vegito")]
        avals = [T("La suya (la de su personaje base)", "Its own (its base character's)")] + [x[1] for x in specials] + donor_values()
        cur = d.get("aura_de")
        chosen["aura_de"] = cur
        av = tk.StringVar(value=avals[0] if cur is None else next(
            (x[1] for x in specials if x[0] == str(cur).lower()), None) or (
            avals[1 + len(specials) + donor_index(cur, False)] if isinstance(cur, int) else avals[0]))
        label(g, T("Forma del aura", "Aura shape"), bg=C["card"], width=16).grid(row=2, column=0, sticky="w", pady=3)
        acb = ttk.Combobox(g, textvariable=av, values=avals, state="readonly", width=26)
        acb.grid(row=2, column=1, sticky="w")

        def aura_picked(_e=None):
            i = acb.current()
            chosen["aura_de"] = None if i <= 0 else specials[i - 1][0] if i <= len(specials) else                 DONORS[i - 1 - len(specials)][0]
        acb.bind("<<ComboboxSelected>>", aura_picked)
        hint(g, T("las fusiones tienen auras propias; el color de arriba se aplica encima",
                  "fusions have their own auras; the color above is applied on top"), bg=C["card"],
             wrap=300).grid(row=2, column=2, sticky="w", padx=8)

        def save():
            try:
                bk = backup_file(m["toml"], self.env)
                toml_set_keys(m["toml"], "personaje", {k: None if v is None else toml_literal(v)
                                                       for k, v in chosen.items()})
            except OSError as ex:
                self.set_status(st, "err", str(ex))
                return
            self.app.log_text(T("Guardado %s (copia en %s). Se aplica al montar el roster.\n",
                                "Saved %s (backup at %s). Applied when the roster is built.\n") % (m["toml"], bk), "ok")
            self.want = m["name"]
            self.reload()
        ttk.Button(g, text=T("Guardar", "Save"), style="Small.TButton", command=save).grid(
            row=3, column=0, sticky="w", pady=(8, 0))
        st.grid(row=4, column=0, columnspan=3, sticky="ew")

    def _traje_editor(self, body, m):
        d = m["data"]
        c = Card(body, m["display"] or m["name"], T("Traje extra para %s: se añade detrás de los trajes del juego.",
                                                    "Extra costume for %s: added after the game's costumes.") %
                 donor_name(d.get("personaje", -1)))
        c.pack(fill="x")
        mods_list = d.get("modelos") or []
        label(c.body, T("Modelos por forma:", "Models per form:"), bg=C["card"], font=F["bold"]).pack(fill="x")
        for x in mods_list:
            label(c.body, "  · " + str(x), bg=C["card"], fg=C["dim"]).pack(fill="x")
        bf = tk.Frame(c.body, bg=C["card"])
        bf.pack(fill="x", pady=(10, 0))
        ttk.Button(bf, text=T("Abrir carpeta", "Open folder"), command=lambda: open_path(m["dir"])).pack(side="left")
        ttk.Button(bf, text=T("Ajustes…", "Settings…"),
                   command=lambda: TomlFormDialog(self.app, m["toml"], self.reload)).pack(side="left", padx=6)
        self.ed_status = self.status_label(c.body)
        self.ed_status.pack(fill="x")


# --------------------------------------------------------------------------------- crear
class CreatePage(Page):
    key = "create"

    def build(self):
        self.header(T("Crear personaje con mis modelos", "Create a character from my models"),
                    T("Para tus propios modelos (.amb o .bin de Budokai 3, de PS2 o HD). El icono, el rótulo y "
                      "los retratos se generan solos desde el modelo.",
                      "For your own models (Budokai 3 .amb or .bin, PS2 or HD). The icon, banner and portraits are "
                      "generated from the model."))
        sf, body = self.scroll_body()
        c = Card(body, T("Datos del personaje", "Character data"))
        c.pack(fill="x", pady=(0, 12))
        g = c.body
        g.columnconfigure(1, weight=1)
        self.name, self.mod = tk.StringVar(), tk.StringVar()
        self._mod_auto = True
        r = 0
        label(g, T("Nombre (rótulo)", "Name (banner)"), bg=C["card"]).grid(row=r, column=0, sticky="w", padx=(0, 12),
                                                                          pady=4)
        ttk.Entry(g, textvariable=self.name, width=34).grid(row=r, column=1, sticky="w", pady=4)
        r += 1
        self.banner = BannerPreview(g, 360, 42)
        self.banner.grid(row=r, column=1, sticky="w")
        r += 1
        label(g, T("Carpeta del mod", "Mod folder"), bg=C["card"]).grid(row=r, column=0, sticky="w", padx=(0, 12),
                                                                       pady=4)
        me = ttk.Entry(g, textvariable=self.mod, width=34)
        me.grid(row=r, column=1, sticky="w", pady=4)
        me.bind("<Key>", lambda _e: setattr(self, "_mod_auto", False))
        r += 1
        self.name.trace_add("write", lambda *_: self._name_changed())
        label(g, T("Personaje base (golpes y técnicas)", "Base character (moves and techniques)"), bg=C["card"]).grid(
            row=r, column=0, sticky="w", padx=(0, 12), pady=4)
        self.donor = ttk.Combobox(g, values=donor_values(), state="readonly", width=40, height=20)
        self.donor.current(donor_index(0, False))
        self.donor.grid(row=r, column=1, sticky="w", pady=4)
        r += 1
        hint(g, T("El personaje del juego del que toma los golpes, las técnicas, la voz y las cápsulas.",
                  "The game character it takes its moves, techniques, voice and capsules from."),
             bg=C["card"]).grid(row=r, column=1, sticky="w")
        r += 1
        label(g, T("Casilla tras", "Cell after"), bg=C["card"]).grid(row=r, column=0, sticky="w", padx=(0, 12), pady=4)
        self.after_cb = ttk.Combobox(g, values=donor_values(T("Automática (tras su personaje base)", "Automatic (after its base "
                                                                                           "character)")),
                                  state="readonly", width=40, height=20)
        self.after_cb.current(0)
        self.after_cb.grid(row=r, column=1, sticky="w", pady=4)
        r += 1
        label(g, T("Plaza", "Slot"), bg=C["card"]).grid(row=r, column=0, sticky="w", padx=(0, 12), pady=4)
        self.slot = ttk.Combobox(g, values=[T("Automática (primera libre)", "Automatic (first free)")] + [
            x for x in slot_values()], state="readonly", width=40, height=20)
        self.slot.current(0)
        self.slot.grid(row=r, column=1, sticky="w", pady=4)
        r += 1
        label(g, T("Formas por traje", "Forms per costume"), bg=C["card"]).grid(row=r, column=0, sticky="w",
                                                                               padx=(0, 12), pady=4)
        self.forms = tk.StringVar(value="1")
        ttk.Spinbox(g, from_=1, to=6, textvariable=self.forms, width=5).grid(row=r, column=1, sticky="w", pady=4)

        c2 = Card(body, T("Modelos", "Models"), T(
            "Uno por traje. Con transformaciones, en orden: traje 1 normal, traje 1 transformado, traje 2 normal... "
            "Valen los .amb/.bin de Budokai 3 (PS2 o HD).", "One per costume. With transformations, in order: "
            "costume 1 normal, costume 1 transformed, costume 2 normal... Budokai 3 .amb/.bin files work (PS2 or HD)."))
        c2.pack(fill="x", pady=(0, 12))
        lf = tk.Frame(c2.body, bg=C["card"])
        lf.pack(fill="x")
        self.models = tk.Listbox(lf, height=6, bg=C["frame"], fg=C["text"], selectbackground=C["accent"],
                                 selectforeground=C["on_accent"], relief="flat", highlightthickness=1,
                                 highlightbackground=C["line"], font=F["body"], activestyle="none")
        self.models.pack(side="left", fill="x", expand=True)
        bcol = tk.Frame(lf, bg=C["card"])
        bcol.pack(side="left", padx=(8, 0), anchor="n")
        for txt, fn in (("✚ " + T("Añadir…", "Add…"), self.add_models), ("✖ " + T("Quitar", "Remove"),
                                                                           self.remove_model),
                        ("▲ " + T("Subir", "Up"), lambda: self.move(-1)), ("▼ " + T("Bajar", "Down"),
                                                                          lambda: self.move(1))):
            ttk.Button(bcol, text=txt, style="Small.TButton", command=fn).pack(fill="x", pady=(0, 4))

        c3 = Card(body, T("Arte (opcional)", "Art (optional)"), T(
            "Si no pones nada, el icono y los retratos se generan desde el modelo. También puedes cambiarlo después "
            "en «Personajes nuevos».", "If empty, the icon and portraits are rendered from the model. You can also "
            "change it later in 'New characters'."))
        c3.pack(fill="x", pady=(0, 12))
        g = c3.body
        g.columnconfigure(1, weight=1)
        self.art = {}
        rows = [("cara", T("Arte de la cara", "Face art")), ("retrato", T("Arte del retrato", "Portrait art"))]
        if self.adv:
            rows += [("icono", T("Icono terminado (--icono)", "Finished icon (--icono)")),
                     ("retrato_p1", T("Retrato P1 terminado 512x512", "Finished P1 portrait 512x512")),
                     ("retrato_p2", T("Retrato P2 terminado 512x512", "Finished P2 portrait 512x512")),
                     ("captura", T("Collage 3x2 de capturas (--captura)", "3x2 screenshot collage (--captura)"))]
        for r, (k, txt) in enumerate(rows):
            self.art[k] = tk.StringVar()
            label(g, txt, bg=C["card"]).grid(row=r, column=0, sticky="w", padx=(0, 12), pady=3)
            PathEntry(g, self.art[k], "file", [(T("Imágenes", "Images"), "*.png *.jpg *.jpeg *.webp"),
                                               (T("Todos", "All"), "*.*")], bg=C["card"]).grid(row=r, column=1,
                                                                                               sticky="ew", pady=3)
        bf = tk.Frame(body, bg=C["bg"])
        bf.pack(fill="x", pady=(0, 12))
        ttk.Button(bf, text="✚  " + T("Crear", "Create"), style="Big.Accent.TButton", command=self.create).pack(
            side="left")
        self.status = self.status_label(bf)
        self.status.configure(bg=C["bg"])
        self.status.pack(side="left", padx=12, fill="x", expand=True)

    def _name_changed(self):
        self.banner.set(self.name.get())
        if self._mod_auto:
            n = self.name.get().strip()
            self.mod.set(unique_mod_name(self.env.mods, slug(n, "nuevo_")) if n else "")

    def add_models(self):
        ps = filedialog.askopenfilenames(filetypes=[(T("Modelos", "Models"), "*.amb *.bin *.lzx"),
                                                    (T("Todos", "All"), "*.*")])
        for p in ps or ():
            self.models.insert("end", os.path.normpath(p))

    def remove_model(self):
        for i in reversed(self.models.curselection()):
            self.models.delete(i)

    def move(self, d):
        s = self.models.curselection()
        if not s:
            return
        i = s[0]
        j = i + d
        if 0 <= j < self.models.size():
            v = self.models.get(i)
            self.models.delete(i)
            self.models.insert(j, v)
            self.models.selection_set(j)

    def gather(self):
        nc = {"nombre": self.name.get().strip(), "mod": self.mod.get().strip(),
              "donante": donor_from_index(self.donor.current(), False),
              "despues_de": donor_from_index(self.after_cb.current(), True),
              "id": -1 if self.slot.current() <= 0 else FREE_SLOTS[self.slot.current() - 1][0],
              "modelos": list(self.models.get(0, "end"))}
        try:
            nc["por_traje"] = max(1, min(6, int(self.forms.get())))
        except ValueError:
            nc["por_traje"] = 1
        for k, v in self.art.items():
            nc[k] = v.get().strip()
        return nc

    def create(self):
        nc = self.gather()
        if not nc["nombre"] or not nc["mod"] or not nc["modelos"]:
            self.set_status(self.status, "warn", T("Faltan el nombre, la carpeta del mod o los modelos.",
                                                   "The name, mod folder or models are missing."))
            return
        if re.search(r'[<>:"/\\|?*]', nc["mod"]):
            self.set_status(self.status, "warn", T("La carpeta no puede llevar < > : \" / \\ | ? *",
                                                   "The folder cannot contain < > : \" / \\ | ? *"))
            return
        if os.path.exists(os.path.join(self.env.mods, nc["mod"])) and not self.app.ask(
                T("La carpeta %s ya existe: se actualizará. ¿Seguir?", "Folder %s already exists: it will be "
                  "updated. Continue?") % nc["mod"]):
            return
        mod = nc["mod"]

        def done(job):
            if job.rc == 0 and self.alive:
                self._mod_auto = True
        self.run(cmd_roster_new(self.env, nc), T("Crear %s", "Create %s") % nc["nombre"], done, self.status,
                 T("Creado en mods\\%s. Revísalo en «Personajes nuevos»; se añade al pulsar JUGAR.",
                   "Created in mods\\%s. Check it in 'New characters'; it is added on PLAY.") % mod)


# --------------------------------------------------------------------------------- texturas
class TexturesPage(Page):
    key = "textures"

    def build(self):
        self.header(T("Mod de texturas", "Texture mod"),
                    T("Extrae las texturas de un personaje como PNG editables y, al reconstruir, reinserta tus "
                      "ediciones (mismas dimensiones).", "Extracts a character's textures as editable PNGs and, "
                      "when you rebuild, re-inserts your edits (same dimensions)."))
        sf, body = self.scroll_body()
        self.chars = load_catalog(self.env)
        c = Card(body, T("1. Personaje", "1. Character"))
        c.pack(fill="x", pady=(0, 12))
        g = c.body
        g.columnconfigure(1, weight=1)
        if not self.chars:
            label(g, T("No encuentro catalog_b3.cat: instala el Kit de modding junto a dbz3.exe.",
                       "catalog_b3.cat not found: install the Modding kit next to dbz3.exe."), fg=C["err"],
                  bg=C["card"]).grid(row=0, column=0, sticky="w")
        label(g, T("Personaje (origen de las texturas)", "Character (texture source)"), bg=C["card"]).grid(
            row=1, column=0, sticky="w", padx=(0, 12), pady=4)
        self.src = CharPicker(g, self.chars, on_change=self.update_state)
        self.src.grid(row=1, column=1, sticky="w", pady=4)
        label(g, T("Slot destino", "Destination slot"), bg=C["card"]).grid(row=2, column=0, sticky="w", padx=(0, 12),
                                                                          pady=4)
        self.dst = CharPicker(g, self.chars, T("El mismo personaje (sin swap)", "Same character (no swap)"),
                              on_change=self.update_state)
        self.dst.grid(row=2, column=1, sticky="w", pady=4)
        hint(g, T("Si eliges otro personaje, el bin del origen con sus texturas editadas se coloca en el slot de ese "
                  "personaje (para combinar con un cambio de modelo).", "If you pick another character, the source "
                  "bin with its edited textures is placed in that character's slot (to combine with a model swap)."),
             bg=C["card"]).grid(row=3, column=1, sticky="w")
        self.mod = tk.StringVar()
        self.folder = tk.StringVar()
        label(g, T("Nombre del mod", "Mod name"), bg=C["card"]).grid(row=4, column=0, sticky="w", padx=(0, 12), pady=4)
        ttk.Entry(g, textvariable=self.mod, width=30).grid(row=4, column=1, sticky="w", pady=4)
        label(g, T("Carpeta de texturas (PNG)", "Texture folder (PNG)"), bg=C["card"]).grid(row=5, column=0,
                                                                                           sticky="w", padx=(0, 12),
                                                                                           pady=4)
        PathEntry(g, self.folder, "dir", bg=C["card"]).grid(row=5, column=1, sticky="ew", pady=4)
        self.def_lbl = hint(g, "", bg=C["card"])
        self.def_lbl.grid(row=6, column=1, sticky="w")
        self.mod.trace_add("write", lambda *_: self.update_state())
        self.folder.trace_add("write", lambda *_: self.update_state())

        c2 = Card(body, T("2. Extraer, editar y reconstruir", "2. Extract, edit and rebuild"))
        c2.pack(fill="x", pady=(0, 12))
        bf = tk.Frame(c2.body, bg=C["card"])
        bf.pack(fill="x")
        self.b_extract = ttk.Button(bf, text="⇩  " + T("Extraer texturas a PNG", "Extract textures to PNG"),
                                    style="Accent.TButton", command=self.extract)
        self.b_extract.pack(side="left")
        self.b_open = ttk.Button(bf, text=T("Abrir carpeta de texturas", "Open textures folder"),
                                 command=lambda: open_path(self.active_dir()))
        self.b_open.pack(side="left", padx=6)
        self.b_build = ttk.Button(bf, text="⟳  " + T("Reconstruir mod con texturas editadas",
                                                     "Rebuild mod with edited textures"), style="Accent.TButton",
                                  command=self.rebuild)
        self.b_build.pack(side="left", padx=6)
        self.status = self.status_label(c2.body)
        self.status.pack(fill="x", pady=(8, 0))
        self.files_lbl = hint(c2.body, "", bg=C["card"], wrap=900)
        self.files_lbl.pack(fill="x", pady=(6, 0))
        if self.adv:
            c3 = Card(body, T("Más herramientas de texturas", "More texture tools"))
            c3.pack(fill="x", pady=(0, 12))
            bf = tk.Frame(c3.body, bg=C["card"])
            bf.pack(fill="x")
            for fn in ("texture_pack.py", "texture_dump_import.py", "texture_upscale_b3.py", "extract_azt_afs.py",
                       "amt_ps2.py"):
                ttk.Button(bf, text=fn, style="Small.TButton",
                           command=lambda f=fn: self.app.goto("tools", tool=f)).pack(side="left", padx=(0, 6))
        self.update_state()

    def mod_name(self):
        m = self.mod.get().strip()
        c = self.src.get()
        return m or ("tex_%d" % c["bin"] if c else "")

    def default_dir(self):
        m = self.mod_name()
        return os.path.join(self.env.mods, m, "textures") if m else ""

    def valid_cfg(self):
        cfg = self.folder.get().strip()
        if not cfg:
            return ""
        for i, ch in enumerate(cfg):          # mismo filtro que el launcher
            if ch in '<>"|?*' or (ch == ":" and i != 1):
                return ""
        return cfg

    def active_dir(self):
        return self.folder.get().strip() or self.default_dir()

    def update_state(self):
        if not hasattr(self, "b_build"):
            return
        d = self.active_dir()
        ok_dir = bool(d) and os.path.isfile(os.path.join(d, "textures_meta.json"))
        self.b_extract.state(["!disabled"] if self.src.get() else ["disabled"])
        self.b_open.state(["!disabled"] if ok_dir else ["disabled"])
        self.b_build.state(["!disabled"] if ok_dir else ["disabled"])
        self.def_lbl.configure(text=(T("por defecto: %s", "default: %s") % self.default_dir()) if
                               self.default_dir() and not self.folder.get().strip() else "")
        if ok_dir:
            pngs = sorted(f for f in os.listdir(d) if f.lower().endswith(".png"))
            self.files_lbl.configure(text=T("Edita los PNG en: %s  (%d imágenes: %s)", "Edit the PNGs in: %s  (%d "
                                            "images: %s)") % (d, len(pngs), ", ".join(pngs[:12]) +
                                                              (" …" if len(pngs) > 12 else "")))
        else:
            self.files_lbl.configure(text=T("Extrae primero las texturas para editar los PNG.",
                                            "Extract the textures first to edit the PNGs."))

    def extract(self):
        c = self.src.get()
        if not c:
            return
        mod = self.mod_name()
        cfg = self.valid_cfg()

        def done(job):
            if self.alive:
                if not self.folder.get().strip() and job.rc == 0:
                    self.folder.set(self.default_dir())
                self.update_state()
        self.run(cmd_tex_extract(self.env, c["bin"], mod, cfg), T("Extraer texturas (bin %d)", "Extract textures "
                                                                                              "(bin %d)") % c["bin"],
                 done, self.status, T("Texturas extraídas. Edítalas y pulsa «Reconstruir».",
                                      "Textures extracted. Edit them and press 'Rebuild'."))

    def rebuild(self):
        mod = self.mod_name()
        dst = self.dst.get()
        self.run(cmd_tex_build(self.env, mod, dst["bin"] if dst else -1, self.active_dir()),
                 T("Reconstruir texturas (%s)", "Rebuild textures (%s)") % mod, lambda j: self.alive and
                 self.update_state(), self.status, T("Mod de texturas generado y activo (mira «Mis mods»).",
                                                     "Texture mod generated and active (see 'My mods')."))


# --------------------------------------------------------------------------------- swap
class SwapPage(Page):
    key = "swap"

    def build(self):
        self.header(T("Cambio de modelo (B3 → B3)", "Model swap (B3 → B3)"),
                    T("Pone el modelo HD de un personaje en el sitio de otro. El mod generado se activa solo y "
                      "aparece en «Mis mods».", "Puts one character's HD model in another's slot. The generated "
                      "mod activates itself and shows up in 'My mods'."))
        sf, body = self.scroll_body()
        self.chars = load_catalog(self.env)
        c = Card(body)
        c.pack(fill="x", pady=(0, 12))
        g = c.body
        g.columnconfigure(1, weight=1)
        label(g, T("Personaje HD (origen)", "HD character (source)"), bg=C["card"]).grid(row=0, column=0, sticky="w",
                                                                                       padx=(0, 12), pady=4)
        self.src = CharPicker(g, self.chars, on_change=self.update_preview)
        self.src.grid(row=0, column=1, sticky="w", pady=4)
        label(g, T("Slot destino", "Destination slot"), bg=C["card"]).grid(row=1, column=0, sticky="w", padx=(0, 12),
                                                                          pady=4)
        self.dst = CharPicker(g, self.chars, on_change=self.update_preview)
        self.dst.grid(row=1, column=1, sticky="w", pady=4)
        r = 2
        self.mod = tk.StringVar()
        self.verify = tk.BooleanVar(value=False)
        if self.adv:
            label(g, "--mod", bg=C["card"]).grid(row=r, column=0, sticky="w", padx=(0, 12), pady=4)
            ttk.Entry(g, textvariable=self.mod, width=30).grid(row=r, column=1, sticky="w", pady=4)
            r += 1
            hint(g, T("Vacío = swap_<origen>_on_<destino> (como el launcher).",
                      "Empty = swap_<source>_on_<target> (like the launcher)."), bg=C["card"]).grid(row=r, column=1,
                                                                                                   sticky="w")
            r += 1
            ttk.Checkbutton(g, text="--verify  " + T("(comprueba el override con MD5 al terminar)",
                                                     "(checks the override with MD5 at the end)"),
                            variable=self.verify, style="Card.TCheckbutton").grid(row=r, column=1, sticky="w", pady=4)
            r += 1
        self.preview = tk.Frame(g, bg=C["frame"], highlightthickness=1, highlightbackground=C["line"])
        self.preview.grid(row=r, column=0, columnspan=2, sticky="ew", pady=(10, 4))
        r += 1
        bf = tk.Frame(g, bg=C["card"])
        bf.grid(row=r, column=0, columnspan=2, sticky="w", pady=(8, 0))
        self.b_swap = ttk.Button(bf, text="⇄  " + T("Cambiar B3 → B3", "Swap B3 → B3"), style="Big.Accent.TButton",
                                 command=self.swap)
        self.b_swap.pack(side="left")
        if self.adv:
            ttk.Button(bf, text=T("Catálogo (--list)", "Catalog (--list)"),
                       command=lambda: self.run(cmd_script(self.env, "swap_b3.py", ["--list"]), "swap_b3 --list",
                                                None, self.status)).pack(side="left", padx=6)
            ttk.Button(bf, text="swap_matrix.py", command=lambda: self.app.goto("tools", tool="swap_matrix.py")).pack(
                side="left")
        self.status = self.status_label(g)
        self.status.grid(row=r + 1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        self.update_preview()

    def update_preview(self):
        if not hasattr(self, "preview"):
            return
        for w in self.preview.winfo_children():
            w.destroy()
        s, d = self.src.get(), self.dst.get()
        label(self.preview, T("Vista previa", "Preview"), fg=C["accent"], font=F["bold"], bg=C["frame"]).pack(
            fill="x", padx=12, pady=(8, 2))
        for who, c, kind in ((T("Origen:", "Source:"), s, "bin"), (T("Destino:", "Target:"), d, "slot")):
            txt = "%s  %s" % (who, (c["name"] + (" (%s)" % c["variant"] if c["variant"] else "") + "   %s %d" % (
                kind, c["bin"]) + ("" if c["playable"] else "   " + T("(no jugable)", "(not playable)"))) if c else "—")
            label(self.preview, txt, bg=C["frame"]).pack(fill="x", padx=12)
        same = s and d and s["bin"] == d["bin"]
        if same:
            label(self.preview, T("Origen y destino son el mismo personaje.", "Source and destination are the same "
                                  "character."), fg=C["warn"], bg=C["frame"]).pack(fill="x", padx=12)
        tk.Frame(self.preview, bg=C["frame"], height=8).pack()
        self.b_swap.state(["!disabled"] if s and d and not same else ["disabled"])

    def swap(self):
        s, d = self.src.get(), self.dst.get()
        if not s or not d or s["bin"] == d["bin"]:
            return
        extra = ["--verify"] if self.verify.get() else []
        mod = self.mod.get().strip() or None
        self.run(cmd_swap(self.env, s["bin"], d["bin"], mod, extra), T("Cambio de modelo %d → %d",
                                                                       "Model swap %d → %d") % (s["bin"], d["bin"]),
                 None, self.status, T("Hecho. El mod está activo en «Mis mods».", "Done. The mod is active in "
                                                                                  "'My mods'."))


# --------------------------------------------------------------------------------- herramientas
PATH_HINT = re.compile(r"(path|dir|carpeta|folder|file|fichero|archivo|afs|iso|json|obj|png|gltf|glb|amb|"
                       r"pack|dump|template|plantilla|modelo|cara|retrato|icono|captura|lista|catalog|report|"
                       r"bonemap|work|tool|mods|^us$|^iw$|font|upscaler|salida|^out|^bin$|textures|source|src|dst|"
                       r"bin-out|pairs|xbd|amt|anm|cam)", re.I)


class ArgForm(tk.Frame):
    """Formulario generado del argparse de una herramienta (sin ejecutarla)."""

    def __init__(self, master, spec, env, fills, on_change):
        super().__init__(master, bg=C["card"])
        self.spec, self.env, self.fills, self.on_change = spec, env, fills or {}, on_change
        self.getters = []
        self.sub_var = tk.StringVar()
        self.columnconfigure(1, weight=1)
        self.row = 0
        if spec.desc:
            hint(self, spec.desc.strip().split("\n")[0], bg=C["card"], wrap=760).grid(row=self.row, column=0,
                                                                                      columnspan=3, sticky="w",
                                                                                      pady=(0, 6))
            self.row += 1
        self.top_getters = self._rows(self, spec.args)
        self.subframe = None
        self.sub_getters = []
        if spec.sub and spec.sub["parsers"]:
            names = list(spec.sub["parsers"])
            label(self, T("Subcomando", "Subcommand"), font=F["bold"], bg=C["card"], fg=C["accent"]).grid(
                row=self.row, column=0, sticky="w", pady=(8, 4), padx=(0, 12))
            vals = names if spec.sub["required"] else [""] + names
            cb = ttk.Combobox(self, textvariable=self.sub_var, values=vals, state="readonly", width=24)
            cb.grid(row=self.row, column=1, sticky="w", pady=(8, 4))
            self.sub_help = hint(self, "", bg=C["card"])
            self.sub_help.grid(row=self.row, column=2, sticky="w", padx=8)
            self.row += 1
            self.subframe = tk.Frame(self, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
            self.subframe.grid(row=self.row, column=0, columnspan=3, sticky="ew", pady=(4, 0))
            self.subframe.columnconfigure(1, weight=1)
            self.row += 1
            cb.bind("<<ComboboxSelected>>", lambda _e: self._build_sub())
            self.sub_var.set(names[0])
            self._build_sub()

    def _build_sub(self):
        for w in self.subframe.winfo_children():
            w.destroy()
        name = self.sub_var.get()
        if not name:
            self.sub_getters = []
            self.sub_help.configure(text="")
            self._changed()
            return
        help_, spec = self.spec.sub["parsers"][name]
        self.sub_help.configure(text=help_ or "")
        inner = tk.Frame(self.subframe, bg=C["card"])
        inner.pack(fill="x", padx=10, pady=8)
        inner.columnconfigure(1, weight=1)
        self.sub_getters = self._rows(inner, spec.args)
        if not spec.args:
            hint(inner, T("Sin opciones.", "No options."), bg=C["card"]).grid(row=0, column=0, sticky="w")
        self._changed()

    def _changed(self):
        if self.on_change:
            self.on_change()

    def _fill_value(self, a):
        what = self.fills.get(a.dest)
        if what == "mods":
            return self.env.mods
        if what == "us":
            return self.env.us
        if what == "afs":
            return self.env.afs
        return ""

    def _rows(self, parent, args):
        getters = []
        row = 0 if parent is not self else self.row
        groups = {}
        for a in args:
            if a.group:
                groups.setdefault(a.group[0], []).append(a.label)
        for a in args:
            lab = a.label + (" *" if a.required else "")
            lf = tk.Frame(parent, bg=C["card"])
            lf.grid(row=row, column=0, sticky="nw", padx=(0, 12), pady=(5, 0))
            tk.Label(lf, text=lab, bg=C["card"], fg=C["accent"] if a.required else C["text"],
                     font=F["mono"]).pack(anchor="w")
            wf = tk.Frame(parent, bg=C["card"])
            wf.grid(row=row, column=1, sticky="ew", pady=(3, 0))
            getter = self._widget(wf, a)
            getters.append((a, getter))
            row += 1
            notes = []
            if a.help:
                notes.append(a.help)
            if a.default_txt and a.action not in ("store_true", "store_false"):
                notes.append(T("por defecto: %s", "default: %s") % a.default_txt)
            if a.type in ("int", "float", "hex"):
                notes.append({"int": T("número entero", "integer"), "float": T("número", "number"),
                              "hex": T("hexadecimal (p. ej. 3c)", "hexadecimal (e.g. 3c)")}[a.type])
            if a.nargs in ("+", "*") or isinstance(a.nargs, int) or a.action in ("append", "extend"):
                notes.append(T("varios valores: sepáralos con espacios (comillas si llevan espacios)",
                               "several values: separate with spaces (quotes if they contain spaces)"))
            if a.group:
                notes.append(T("elige solo una de: %s", "pick only one of: %s") % " / ".join(groups[a.group[0]]))
            if notes:
                hint(parent, "  ·  ".join(notes), bg=C["card"], wrap=700).grid(row=row, column=1, sticky="w")
                row += 1
        if parent is self:
            self.row = row
        return getters

    def _widget(self, wf, a):
        trace = lambda *_: self._changed()  # noqa: E731
        flag = a.flag
        if a.action in ("store_true", "store_false", "store_const", "append_const"):
            v = tk.BooleanVar(value=False)
            v.trace_add("write", trace)
            ttk.Checkbutton(wf, text=T("activar", "enable"), variable=v, style="Card.TCheckbutton").pack(anchor="w")
            return lambda: [flag] if v.get() else []
        if a.action == "count":
            v = tk.StringVar(value="0")
            v.trace_add("write", trace)
            ttk.Spinbox(wf, from_=0, to=5, textvariable=v, width=4).pack(anchor="w")
            return lambda: [flag] * max(0, int(v.get() or 0)) if (v.get() or "0").isdigit() else []
        multi = a.nargs in ("+", "*") or isinstance(a.nargs, int) or a.action in ("append", "extend")
        v = tk.StringVar(value=self._fill_value(a))
        v.trace_add("write", trace)
        if a.choices and not multi:
            vals = list(a.choices) if a.required and a.default is None else [""] + list(a.choices)
            ttk.Combobox(wf, textvariable=v, values=vals, state="readonly", width=28).pack(anchor="w")
        else:
            pathy = a.type == "str" and (PATH_HINT.search(a.dest or "") or PATH_HINT.search(str(a.metavar or "")))
            if pathy:
                PathEntry(wf, v, "any", width=60, bg=C["card"]).pack(fill="x")
            else:
                ttk.Entry(wf, textvariable=v, width=40 if not multi else 60).pack(anchor="w", fill="x" if multi
                                                                                  else None)
        use = None
        if a.nargs == "?" and not a.positional:
            use = tk.BooleanVar(value=False)
            use.trace_add("write", trace)
            ttk.Checkbutton(wf, text=T("usar esta opción (valor opcional)", "use this option (value optional)"),
                            variable=use, style="Card.TCheckbutton").pack(anchor="w")

        def get():
            val = v.get().strip()
            if use is not None:
                if not use.get() and not val:
                    return []
                return [flag] + ([val] if val else [])
            if not val:
                if a.required:
                    raise ValueError(T("Falta %s", "Missing %s") % a.label)
                return []
            if multi:
                toks = split_args(val)
                if a.action in ("append", "extend") and not a.positional:
                    out = []
                    for t in toks:
                        out += [flag, t]
                    return out
                return toks if a.positional else [flag] + toks
            if a.type in ("int", "float"):
                try:
                    float(val) if a.type == "float" else int(val)
                except ValueError:
                    raise ValueError(T("%s debe ser un número", "%s must be a number") % a.label) from None
            if a.type == "hex":
                try:
                    int(val, 16)
                except ValueError:
                    raise ValueError(T("%s debe ser hexadecimal", "%s must be hexadecimal") % a.label) from None
            return [val] if a.positional else [flag, val]
        return get

    def get_args(self):
        out = []
        top_pos, top_opt = [], []
        for a, g in self.top_getters:
            (top_pos if a.positional else top_opt).extend(g())
        out += top_opt + top_pos
        if self.spec.sub and self.sub_var.get():
            out.append(self.sub_var.get())
            pos, opt = [], []
            for a, g in self.sub_getters:
                (pos if a.positional else opt).extend(g())
            out += pos + opt
        elif self.spec.sub and self.spec.sub["required"]:
            raise ValueError(T("Elige un subcomando", "Pick a subcommand"))
        return out


class ToolsPage(Page):
    key = "tools"

    def build(self):
        self.header(T("Todas las herramientas", "All tools"),
                    T("Cada herramienta de «mod center hd» y «awo_tools» con sus opciones. Las de investigación "
                      "son para usuarios que saben lo que hacen.", "Every tool in 'mod center hd' and 'awo_tools' "
                      "with its options. The research ones are for users who know what they do."))
        pw = ttk.Panedwindow(self, orient="horizontal")
        pw.pack(fill="both", expand=True, padx=22, pady=(0, 12))
        left = tk.Frame(pw, bg=C["bg"])
        sr = tk.Frame(left, bg=C["bg"])
        sr.pack(fill="x", pady=(0, 6))
        self.search = tk.StringVar()
        self.search.trace_add("write", lambda *_: self.fill())
        label(sr, "⌕", fg=C["dim"], font=F["h3"]).pack(side="left")
        ttk.Entry(sr, textvariable=self.search).pack(side="left", fill="x", expand=True, padx=6)
        tf = tk.Frame(left, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
        tf.pack(fill="both", expand=True)
        self.tree = ttk.Treeview(tf, show="tree", selectmode="browse")
        self.tree.column("#0", width=290)
        vsb = ttk.Scrollbar(tf, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        self.tree.tag_configure("cat", foreground=C["accent"], font=F["bold"])
        self.tree.tag_configure("res", foreground=C["dim"])
        self.tree.bind("<<TreeviewSelect>>", lambda _e: self.on_pick())
        self.count = hint(left, "", bg=C["bg"])
        self.count.pack(fill="x", pady=(4, 0))
        pw.add(left, weight=1)
        self.detail = ScrollFrame(pw)
        pw.add(self.detail, weight=3)
        self.tools, self.current, self.form, self.confirmed = None, None, None, set()
        self.want = None

    def on_show(self):
        if self.tools is None:
            self.load()

    def load(self):
        self.tools = self.app.cache.get("tools")
        if self.tools is None:
            self.tools = scan_tools()
            self.app.cache["tools"] = self.tools
        self.fill()
        if self.want:
            self.select(self.want)
            self.want = None
        elif not self.current:
            self.show_intro()

    def focus_item(self, tool=None, **_):
        if self.tools is None:
            self.want = tool
            self.load()
        elif tool:
            self.select(tool)

    def fill(self):
        if self.tools is None:
            return
        self.tree.delete(*self.tree.get_children())
        needle = self.search.get().lower().strip()
        n = 0
        for cat, es, en in CATS:
            items = [t for t in self.tools if t.cat == cat and (not needle or needle in (
                t.name + " " + t.desc + " " + t.doc[:400]).lower())]
            if not items:
                continue
            self.tree.insert("", "end", iid="cat:" + cat, text=T(es, en) + "  (%d)" % len(items), open=bool(needle) or
                             cat not in RESEARCH_CATS + ("biblioteca",), tags=("cat",))
            for t in items:
                iid = t.path
                self.tree.insert("cat:" + cat, "end", iid=iid, text=t.name, tags=("res",) if t.research else ())
                n += 1
        self.count.configure(text=T("%d herramientas", "%d tools") % n)

    def on_pick(self):
        s = self.tree.selection()
        if not s or s[0].startswith("cat:"):
            return
        t = next((x for x in self.tools if x.path == s[0]), None)
        if t and t is not self.current:
            self.show_tool(t)

    def select(self, name):
        t = next((x for x in self.tools or [] if x.name == name or x.path == name), None)
        if t:
            if self.tree.exists(t.path):
                self.tree.see(t.path)
                self.tree.selection_set(t.path)
            self.show_tool(t)

    def show_intro(self):
        body = self.detail.inner
        for w in body.winfo_children():
            w.destroy()
        c = Card(body, T("Elige una herramienta", "Pick a tool"))
        c.pack(fill="x")
        label(c.body, T("A la izquierda están todas, por categorías. Las del kit tienen un formulario con todas sus "
                        "opciones (leído de su código, sin ejecutarlas); las demás aceptan argumentos libres. El "
                        "comando exacto sale abajo para copiarlo, y la salida va al registro.",
                        "All of them are on the left, by category. Kit tools get a form with every option (read "
                        "from their code, without running them); the rest take free arguments. The exact command "
                        "is shown below to copy it, and the output goes to the log."), bg=C["card"], fg=C["dim"],
              wrap=760).pack(fill="x")

    def show_tool(self, t):
        self.current = t
        self.preview = None
        body = self.detail.inner
        for w in body.winfo_children():
            w.destroy()
        self.form = None
        c = Card(body)
        c.pack(fill="x", pady=(0, 12))
        top = tk.Frame(c.body, bg=C["card"])
        top.pack(fill="x")
        label(top, t.name, font=F["h2"], bg=C["card"]).pack(side="left")
        kinds = {"argparse": (T("formulario", "form"), C["ok"]), "argv": (T("argumentos libres", "free arguments"),
                                                                           C["blue"]),
                 "library": (T("módulo", "module"), C["dim"]), "script": (T("script sin opciones", "script, no options"),
                                                                          C["warn"]),
                 "error": (T("no se pudo leer", "unreadable"), C["err"])}
        kt, kc = kinds.get(t.kind, ("?", C["dim"]))
        tk.Label(top, text=kt, bg=C["frame"], fg=kc, font=F["bold_sm"], padx=8, pady=2).pack(side="left", padx=10)
        cat = next((T(es, en) for k, es, en in CATS if k == t.cat), t.cat)
        label(top, cat, fg=C["dim"], bg=C["card"]).pack(side="left")
        ttk.Button(top, text=T("Abrir carpeta", "Open folder"), style="Small.TButton",
                   command=lambda: open_path(os.path.dirname(t.path))).pack(side="right")
        if t.desc:
            label(c.body, t.desc, bg=C["card"], wrap=820).pack(fill="x", pady=(6, 0))
        pe = ttk.Entry(c.body)
        pe.insert(0, t.path)
        pe.configure(state="readonly")
        pe.pack(fill="x", pady=(6, 0))
        if t.research:
            label(c.body, "⚠  " + T("Herramienta de investigación: puede usar rutas del proyecto de desarrollo o "
                                    "escribir archivos de prueba. Úsala solo si sabes lo que hace.",
                                    "Research tool: it may use development-tree paths or write test files. Use it "
                                    "only if you know what it does."), fg=C["warn"], bg=C["card"], wrap=820).pack(
                fill="x", pady=(8, 0))
        if t.doc:
            dt = tk.Text(c.body, height=min(12, max(3, t.doc.count("\n") + 1)), bg=C["log_bg"], fg=C["dim"],
                         font=F["mono"], relief="flat", bd=0, padx=10, pady=8, wrap="word")
            dt.insert("1.0", t.doc)
            dt.configure(state="disabled")
            dt.pack(fill="x", pady=(10, 0))
        if t.kind in ("library", "error"):
            label(c.body, (T("Módulo sin línea de comandos: lo usan otras herramientas.",
                             "Module without a command line: other tools use it.") if t.kind == "library" else t.error),
                  fg=C["dim"], bg=C["card"]).pack(fill="x", pady=(10, 0))
            return
        oc = Card(body, T("Opciones", "Options"))
        oc.pack(fill="x", pady=(0, 12))
        self.extra = tk.StringVar()
        self.workdir = tk.StringVar(value=KIT)
        if t.kind == "argparse":
            self.form = ArgForm(oc.body, t.spec, self.env, t.fill, self.update_preview)
            self.form.pack(fill="x")
            if not t.spec.args and not t.spec.sub:
                hint(oc.body, T("No se pudieron leer sus opciones: usa «Argumentos extra» o «Ver ayuda».",
                                "Its options could not be read: use 'Extra arguments' or 'Show help'."),
                     bg=C["card"]).pack(fill="x")
        else:
            if t.usage:
                ut = tk.Text(oc.body, height=min(10, t.usage.count("\n") + 1), bg=C["log_bg"], fg=C["blue"],
                             font=F["mono"], relief="flat", bd=0, padx=10, pady=6, wrap="none")
                ut.insert("1.0", t.usage)
                ut.configure(state="disabled")
                ut.pack(fill="x", pady=(0, 8))
            if t.kind == "script":
                label(oc.body, T("Se ejecuta entero al abrirlo (no tiene opciones).", "Runs entirely when started (no "
                                 "options)."), fg=C["warn"], bg=C["card"]).pack(fill="x")
        xf = tk.Frame(oc.body, bg=C["card"])
        xf.pack(fill="x", pady=(10, 0))
        label(xf, T("Argumentos", "Arguments") if t.kind != "argparse" else T("Argumentos extra", "Extra arguments"),
              bg=C["card"], font=F["bold"]).pack(side="left", padx=(0, 8))
        ttk.Entry(xf, textvariable=self.extra).pack(side="left", fill="x", expand=True)
        ttk.Button(xf, text="+ " + T("Archivo", "File"), style="Small.TButton",
                   command=lambda: self._append_path(False)).pack(side="left", padx=(6, 0))
        ttk.Button(xf, text="+ " + T("Carpeta", "Folder"), style="Small.TButton",
                   command=lambda: self._append_path(True)).pack(side="left", padx=(4, 0))
        wf = tk.Frame(oc.body, bg=C["card"])
        wf.pack(fill="x", pady=(8, 0))
        label(wf, T("Carpeta de trabajo", "Working folder"), bg=C["card"], font=F["bold"]).pack(side="left",
                                                                                                padx=(0, 8))
        PathEntry(wf, self.workdir, "dir", bg=C["card"]).pack(side="left", fill="x", expand=True)
        self.extra.trace_add("write", lambda *_: self.update_preview())
        rc = Card(body, T("Comando", "Command"))
        rc.pack(fill="x", pady=(0, 12))
        self.preview = tk.Text(rc.body, height=3, bg=C["log_bg"], fg=C["text"], font=F["mono"], relief="flat", bd=0,
                               padx=10, pady=6, wrap="word")
        self.preview.pack(fill="x")
        bf = tk.Frame(rc.body, bg=C["card"])
        bf.pack(fill="x", pady=(8, 0))
        ttk.Button(bf, text="▶  " + T("Ejecutar", "Run"), style="Accent.TButton", command=self.run_tool).pack(
            side="left")
        if t.kind == "argparse":
            ttk.Button(bf, text=T("Ver ayuda (--help)", "Show help (--help)"), command=self.show_help).pack(
                side="left", padx=6)
        ttk.Button(bf, text=T("Copiar comando", "Copy command"), command=self.copy_cmd).pack(side="left")
        self.status = self.status_label(rc.body)
        self.status.pack(fill="x", pady=(8, 0))
        self.update_preview()
        self.detail.top()

    def _append_path(self, folder):
        p = filedialog.askdirectory() if folder else filedialog.askopenfilename()
        if p:
            p = os.path.normpath(p)
            cur = self.extra.get().rstrip()
            self.extra.set((cur + " " if cur else "") + ('"%s"' % p if " " in p else p))

    def build_cmd(self):
        t = self.current
        args = self.form.get_args() if self.form is not None else []
        return [self.env.python, t.path] + args + split_args(self.extra.get())

    def update_preview(self):
        if not self.current or getattr(self, "preview", None) is None:
            return
        try:
            txt, col = cmdline(self.build_cmd()), C["text"]
        except ValueError as ex:
            txt, col = "⚠ " + str(ex), C["warn"]
        try:
            self.preview.configure(state="normal")
            self.preview.delete("1.0", "end")
            self.preview.insert("1.0", txt)
            self.preview.configure(state="disabled", fg=col)
        except tk.TclError:
            pass

    def copy_cmd(self):
        try:
            txt = cmdline(self.build_cmd())
        except ValueError as ex:
            self.set_status(self.status, "warn", str(ex))
            return
        self.app.root.clipboard_clear()
        self.app.root.clipboard_append(txt)
        self.set_status(self.status, "ok", T("Copiado.", "Copied."))

    def run_tool(self):
        t = self.current
        try:
            cmd = self.build_cmd()
        except ValueError as ex:
            self.set_status(self.status, "warn", str(ex))
            return
        if (t.research or t.kind == "script") and t.path not in self.confirmed:
            if not self.app.ask(T("«%s» es una herramienta de investigación. Puede tardar, usar rutas del proyecto "
                                  "o escribir archivos. ¿Ejecutar?", "'%s' is a research tool. It may take long, use "
                                  "project paths or write files. Run it?") % t.name):
                return
            self.confirmed.add(t.path)
        wd = self.workdir.get().strip() or KIT
        self.run(cmd, t.name, None, self.status, cwd=wd if os.path.isdir(wd) else KIT)

    def show_help(self):
        t = self.current
        cmd = [self.env.python, t.path]
        if self.form is not None and self.form.spec.sub and self.form.sub_var.get():
            cmd.append(self.form.sub_var.get())
        self.run(cmd + ["--help"], t.name + " --help", None, self.status)


# --------------------------------------------------------------------------------- ayuda
DOCS = [
    ("Léeme del kit", "Kit readme", "Instalación y contenido del kit.", "Kit install and contents.",
     ["LEEME_KIT.txt", "tools/modpacks/LEEME_KIT.txt"]),
    ("Cómo hacer mods", "How to make mods", "Tipos de mod, override por entrada, activar/desactivar.",
     "Mod types, per-entry override, enable/disable.", ["docs/COMO_HACER_MODS.md", "docs/02_mods/COMO_HACER_MODS.md"]),
    ("Cambio de modelo", "Model swap", "Cómo funciona el swap de modelos B3 → B3.", "How B3 → B3 model swaps work.",
     ["docs/MODEL_SWAP.md", "docs/02_mods/MODEL_SWAP.md"]),
    ("Mod de texturas", "Texture mod", "Extraer, editar y reconstruir texturas.", "Extract, edit and rebuild textures.",
     ["docs/TEXTURAS_MOD.md", "docs/02_mods/TEXTURAS_MOD.md"]),
    ("Packs de texturas", "Texture packs", "Packs estilo PCSX2 (reemplazo por hash).",
     "PCSX2-style packs (replacement by hash).", ["docs/PACKS_DE_TEXTURAS.md", "docs/02_mods/PACKS_DE_TEXTURAS.md"]),
    ("Juegos de PS2 (ps2_games)", "PS2 games (ps2_games)", "Qué nombres deben tener tus copias.",
     "Which names your copies must have.", ["ps2_games/LEEME.txt", "tools/modpacks/LEEME_PS2_GAMES.txt"]),
    ("Cápsulas de B3", "B3 capsules", "Formato de las cápsulas (habilidades).", "Capsule (skill) format.",
     ["docs/formatos/CAPSULAS_B3.md", "docs/03_formatos/CAPSULAS_B3.md"]),
    ("Mapa del roster HD", "HD roster map", "IDs, entradas AFS y tablas por personaje.",
     "IDs, AFS entries and per-character tables.", ["docs/formatos/MAPA_ROSTER_HD.md",
                                                    "docs/03_formatos/MAPA_ROSTER_HD.md"]),
    ("Formato #ACM (moveset)", "#ACM format (moveset)", "Animaciones y moveset HD.", "HD animations and moveset.",
     ["docs/formatos/ACM_FORMAT.md", "docs/03_formatos/ACM_FORMAT.md"]),
    ("Formato AMO / AWO", "AMO / AWO format", "Modelos PS2 y HD.", "PS2 and HD models.",
     ["docs/formatos/AMO_AWO.md", "docs/03_formatos/AMO_AWO.md"]),
    ("Estructura de un bin", "Bin layout", "Contenedor #AMB HD.", "HD #AMB container.",
     ["docs/formatos/BIN_LAYOUT.md", "docs/03_formatos/BIN_LAYOUT.md"]),
    ("Escenarios", "Stages", "Formato de los escenarios.", "Stage format.",
     ["docs/formatos/STAGES_FORMAT.md", "docs/03_formatos/STAGES_FORMAT.md"]),
    ("Guía de swaps y ports", "Swaps & ports guide", "Principios de los swaps y ports entre juegos.",
     "Principles of cross-game swaps and ports (technical notes, Spanish only).", ["mod center hd/GUIA_SWAPS_Y_PORTS.md"]),
    ("Inventario de herramientas", "Tools inventory", "Todas las herramientas del proyecto.",
     "Every tool in the project (technical list, Spanish only).", ["docs/04_herramientas/TOOLS.md"]),
]


class HexPage(Page):
    """Editor hexadecimal (hexedit.py) para modders expertos: sabe de #AMB/#AMO y guarda con copia."""
    key = "hex"

    def build(self):
        self.header(T("Editor hexadecimal", "Hex editor"),
                    T("Para modders expertos: edita bytes como en HxD. El árbol de la izquierda entiende los #AMB "
                      "(PS2 en little endian, HD en big endian) y las partes de los #AMO (ocultar una parte para "
                      "un traje alternativo). Al guardar se hace una copia de seguridad.",
                      "For expert modders: edit bytes like in HxD. The tree on the left understands #AMB (PS2 "
                      "little endian, HD big endian) and #AMO parts (hide a part for an alternate costume). "
                      "Saving makes a backup first."))
        try:
            import hexedit  # noqa: PLC0415
            hexedit.COLORS.update(bg=C["bg"], card=C["card"], text=C["text"], dim=C["dim"], accent=C["accent"],
                                  err=C["err"])
            self.ed = hexedit.HexEditor(self, fonts=F, tr=T)
            self.ed.pack(fill="both", expand=True, padx=22, pady=(0, 10))
        except Exception as ex:  # noqa: BLE001
            label(self, T("No se pudo cargar el editor: %s", "Could not load the editor: %s") % ex,
                  fg=C["err"]).pack(fill="x", padx=22)


class DiagPage(Page):
    """Lee el log del juego y explica en sencillo que pasa (diagnostico.py), y revisa que el mod de
    personajes nuevos tenga todos los idiomas (en espanol/aleman/... el juego lee otros ficheros)."""
    key = "diag"

    def build(self):
        self.header(T("Diagnóstico", "Diagnostics"),
                    T("¿Va lento, se cierra o no salen los personajes? Analiza el registro del juego y te dice "
                      "qué pasa y qué hacer.",
                      "Slow, crashing or new characters missing? Analyse the game log and get what is wrong "
                      "and what to do."))
        sf, body = self.scroll_body()
        c = Card(body, T("Registro del juego", "Game log"),
                 T("El juego guarda uno por partida en la carpeta logs (dbz3_XXX.log). También vale el log "
                   "que te pase otro jugador.",
                   "The game writes one per session in the logs folder (dbz3_XXX.log). A log from another "
                   "player works too."), accent=True)
        c.pack(fill="x", pady=(12, 12))
        row = tk.Frame(c.body, bg=C["card"])
        row.pack(fill="x")
        ttk.Button(row, text=T("Analizar mi último registro", "Analyse my latest log"), style="Accent.TButton",
                   command=self.latest).pack(side="left")
        ttk.Button(row, text=T("Abrir otro registro…", "Open another log…"), style="Small.TButton",
                   command=self.pick).pack(side="left", padx=8)
        ttk.Button(row, text=T("Copiar informe", "Copy report"), style="Small.TButton",
                   command=self.copy).pack(side="left")
        self.src = label(c.body, "", fg=C["dim"], bg=C["card"], font=F["small"])
        self.src.pack(fill="x", pady=(8, 0))
        self.out = tk.Frame(body, bg=C["bg"])
        self.out.pack(fill="x")
        self.report = ""
        m = Card(body, T("Mod de personajes nuevos", "New characters mod"),
                 T("Comprueba que esté montado para todos los idiomas del juego.",
                   "Checks it is built for every game language."))
        m.pack(fill="x", pady=(12, 12))
        self.mods_lbl = label(m.body, "", bg=C["card"], wrap=820)
        self.mods_lbl.pack(fill="x")

    def on_show(self):
        roster = os.path.join(self.env.mods, "_roster", "us")
        if not os.path.isdir(roster):
            self.mods_lbl.configure(text=T("Aún no hay personajes nuevos montados (se montan al pulsar JUGAR).",
                                           "No new characters built yet (they are built when you press PLAY)."),
                                    fg=C["dim"])
            return
        need = ["data_usi.afs"] + ["data_%s.afs" % k for k in ("eng", "spn", "fra", "ger", "ita")]
        miss = [n for n in need if not os.path.exists(os.path.join(roster, n))]
        if miss:
            self.mods_lbl.configure(text=T("Faltan idiomas: %s. Con el juego en esos idiomas las casillas nuevas "
                                           "salen vacías. Pulsa «Reconstruir» en «Personajes nuevos».",
                                           "Missing languages: %s. With the game in those languages the new "
                                           "cells are blank. Press 'Rebuild' in 'New characters'.")
                                    % ", ".join(miss), fg=C["err"])
        else:
            self.mods_lbl.configure(text=T("Correcto: montado para inglés, español, francés, alemán e italiano.",
                                           "OK: built for English, Spanish, French, German and Italian."),
                                    fg=C["ok"])

    def latest(self):
        logs = os.path.join(self.env.game, "logs") if self.env.game else ""
        cands = []
        if logs and os.path.isdir(logs):
            cands = [os.path.join(logs, n) for n in os.listdir(logs) if n.startswith("dbz3_") and n.endswith(".log")]
        if not cands:
            messagebox.showinfo(T("Diagnóstico", "Diagnostics"),
                                T("No encuentro registros del juego. Juega una partida o elige uno con «Abrir otro "
                                  "registro…».", "No game logs found. Play once or pick one with 'Open another "
                                  "log…'."))
            return
        self.analyse(max(cands, key=os.path.getmtime))

    def pick(self):
        p = filedialog.askopenfilename(filetypes=[(T("Registros", "Logs"), "*.log *.txt"), (T("Todos", "All"), "*.*")])
        if p:
            self.analyse(p)

    def analyse(self, path):
        import diagnostico
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError as e:
            messagebox.showerror(T("Diagnóstico", "Diagnostics"), str(e))
            return
        findings = diagnostico.analizar(text, en=LANG == "en")
        self.report = diagnostico.informe(text, en=LANG == "en")
        self.src.configure(text=path)
        for w in self.out.winfo_children():
            w.destroy()
        color = {"ok": C["ok"], "info": C["blue"], "warn": C["warn"], "err": C["err"]}
        mark = {"ok": "✔", "info": "ℹ", "warn": "!", "err": "✖"}
        for level, title, detail in findings:
            card = tk.Frame(self.out, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
            card.pack(fill="x", pady=4)
            tk.Label(card, text=mark[level], bg=C["card"], fg=color[level], font=F["h3"], width=2).pack(
                side="left", anchor="n", padx=(10, 4), pady=8)
            tf = tk.Frame(card, bg=C["card"])
            tf.pack(side="left", fill="x", expand=True, pady=8, padx=(0, 12))
            label(tf, title, font=F["bold"], bg=C["card"], fg=color[level]).pack(fill="x")
            if detail:
                label(tf, detail, bg=C["card"], wrap=820).pack(fill="x", pady=(2, 0))
        self.app.log_text(self.report + "\n")

    def copy(self):
        if not self.report:
            return
        self.app.root.clipboard_clear()
        self.app.root.clipboard_append(self.report)
        self.src.configure(text=T("Informe copiado: pégalo en Discord al pedir ayuda.",
                                  "Report copied: paste it on Discord when asking for help."))


class HelpPage(Page):
    key = "help"

    def build(self):
        self.header(T("Ayuda y comunidad", "Help & community"),
                    T("Guías incluidas en el kit, primeros pasos y dónde pedir ayuda.",
                      "Guides shipped with the kit, first steps and where to ask for help."))
        sf, body = self.scroll_body()
        cols = tk.Frame(body, bg=C["bg"])
        cols.pack(fill="both", expand=True)
        cols.columnconfigure(0, weight=3, uniform="h")
        cols.columnconfigure(1, weight=2, uniform="h")
        left = tk.Frame(cols, bg=C["bg"])
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 12))
        right = tk.Frame(cols, bg=C["bg"])
        right.grid(row=0, column=1, sticky="nsew")
        g = Card(left, T("Guías", "Guides"), T("Se abren aquí mismo. «Abrir» las abre con tu programa.",
                                              "They open right here. 'Open' uses your own program."))
        g.pack(fill="x", pady=(0, 12))
        found = 0
        for es, en, des, den, cands in DOCS:
            if LANG == "en":    # X.md -> X_EN.md / X.txt -> X_EN.txt (guia traducida, si existe)
                cands = [re.sub(r"(\.\w+)$", r"_EN\1", c) for c in cands] + cands
            p = first_existing(*[os.path.join(KIT, *c.split("/")) for c in cands])
            if not p:
                continue
            found += 1
            row = tk.Frame(g.body, bg=C["card"])
            row.pack(fill="x", pady=3)
            tf = tk.Frame(row, bg=C["card"])
            tf.pack(side="left", fill="x", expand=True)
            label(tf, T(es, en), font=F["bold"], bg=C["card"]).pack(fill="x")
            label(tf, T(des, den), fg=C["dim"], font=F["small"], bg=C["card"]).pack(fill="x")
            ttk.Button(row, text=T("Abrir", "Open"), style="Small.TButton",
                       command=lambda q=p: open_path(q)).pack(side="right")
            ttk.Button(row, text=T("Leer", "Read"), style="Small.TButton",
                       command=lambda q=p, t=T(es, en): DocViewer(self.app, q, t)).pack(side="right", padx=6)
        if not found:
            label(g.body, T("No hay guías junto al kit.", "No guides next to the kit."), fg=C["dim"],
                  bg=C["card"]).pack(fill="x")
        docs = first_existing(os.path.join(KIT, "docs"))
        if docs:
            ttk.Button(g.body, text=T("Abrir carpeta docs", "Open docs folder"), style="Small.TButton",
                       command=lambda: open_path(docs)).pack(anchor="w", pady=(8, 0))
        s = Card(left, T("Primeros pasos", "First steps"))
        s.pack(fill="x", pady=(0, 12))
        steps = [T("Copia TODO el kit junto a dbz3.exe (deben quedar «mod center hd» y «awo_tools» al lado).",
                   "Copy the WHOLE kit next to dbz3.exe ('mod center hd' and 'awo_tools' must sit beside it)."),
                 T("«Comprobar instalación» → «Instalar requisitos» (una sola vez).",
                   "'Check installation' → 'Install requirements' (only once)."),
                 T("Para personajes de otros juegos, pon TUS copias en ps2_games y usa «Importar personaje».",
                   "For characters from other games, put YOUR copies in ps2_games and use 'Import a character'."),
                 T("Abre el juego y pulsa JUGAR: los personajes nuevos se montan solos.",
                   "Open the game and press PLAY: new characters are built automatically."),
                 T("Si algo va mal, desactiva el mod en «Mis mods». Tus partidas guardadas no se tocan nunca.",
                   "If something goes wrong, disable the mod in 'My mods'. Your saves are never touched.")]
        for i, st in enumerate(steps, 1):
            row = tk.Frame(s.body, bg=C["card"])
            row.pack(fill="x", pady=3)
            tk.Label(row, text=str(i), bg=C["accent"], fg=C["on_accent"], font=F["bold"], width=2).pack(side="left",
                                                                                                       anchor="n")
            label(row, st, bg=C["card"], wrap=470).pack(side="left", fill="x", padx=10)
        cm = Card(right, T("Comunidad", "Community"), accent=True)
        cm.pack(fill="x", pady=(0, 12))
        label(cm.body, T("Comparte tus mods, pide ayuda y descubre los de los demás en el Discord de la comunidad.",
                         "Share your mods, ask for help and discover everyone else's on the community Discord."),
              bg=C["card"], wrap=320).pack(fill="x")
        if DISCORD_URL:
            ttk.Button(cm.body, text=T("Unirse al Discord", "Join the Discord"), style="Accent.TButton",
                       command=lambda: webbrowser.open(DISCORD_URL)).pack(anchor="w", pady=(10, 0))
            label(cm.body, DISCORD_URL, fg=C["blue"], bg=C["card"], font=F["small"]).pack(fill="x", pady=(4, 0))
        else:
            label(cm.body, T("Enlace de Discord: próximamente.", "Discord link: coming soon."), fg=C["warn"],
                  bg=C["card"], font=F["bold"]).pack(fill="x", pady=(10, 0))
        label(cm.body, T("Para compartir un mod: comprime su carpeta (mods\\<tu_mod>) y súbela. Nunca compartas "
                         "archivos de los juegos.", "To share a mod: zip its folder (mods\\<your_mod>) and upload "
                         "it. Never share game files."), fg=C["dim"], bg=C["card"], wrap=380,
              font=F["small"]).pack(fill="x", pady=(10, 0))
        ab = Card(right, T("Acerca de", "About"))
        ab.pack(fill="x", pady=(0, 12))
        for a, b in (("DBZ3 HD Mod Kit", MODKIT_VERSION), (T("Kit de modding", "Modding kit"), KIT_VERSION),
                     ("Python", "%d.%d.%d" % sys.version_info[:3]), ("Tk", str(tk.TkVersion))):
            row = tk.Frame(ab.body, bg=C["card"])
            row.pack(fill="x")
            label(row, a, fg=C["dim"], bg=C["card"], width=18).pack(side="left")
            label(row, b, bg=C["card"]).pack(side="left")
        label(ab.body, T("No incluye archivos de ningún juego: usa siempre los de tus copias.",
                         "Contains no game files: always use your own copies."), fg=C["dim"], bg=C["card"],
              font=F["small"], wrap=320).pack(fill="x", pady=(8, 0))


class DocViewer(tk.Toplevel):
    """Visor sencillo de Markdown / texto (titulos, codigo, listas y tablas)."""

    def __init__(self, app, path, title=""):
        super().__init__(app.root)
        if app.selftest:
            self.withdraw()
        self.title(title or os.path.basename(path))
        self.configure(bg=C["bg"])
        self.geometry("980x720")
        top = tk.Frame(self, bg=C["bg"])
        top.pack(fill="x", padx=14, pady=10)
        label(top, title or os.path.basename(path), font=F["h2"]).pack(side="left")
        ttk.Button(top, text=T("Abrir con mi programa", "Open with my app"), style="Small.TButton",
                   command=lambda: open_path(path)).pack(side="right")
        body = tk.Frame(self, bg=C["card"])
        body.pack(fill="both", expand=True, padx=14, pady=(0, 14))
        t = tk.Text(body, bg=C["card"], fg=C["text"], font=F["body"], relief="flat", bd=0, padx=18, pady=14,
                    wrap="word", spacing1=2, spacing3=2)
        vsb = ttk.Scrollbar(body, orient="vertical", command=t.yview)
        t.configure(yscrollcommand=vsb.set)
        t.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        t.tag_configure("h1", font=F["h1"], foreground=C["accent"], spacing1=10, spacing3=6)
        t.tag_configure("h2", font=F["h2"], foreground=C["accent"], spacing1=10, spacing3=4)
        t.tag_configure("h3", font=F["h3"], foreground=C["blue"], spacing1=8, spacing3=2)
        t.tag_configure("code", font=F["mono"], background=C["log_bg"], foreground=C["text"], lmargin1=14,
                        lmargin2=14)
        t.tag_configure("inline", font=F["mono"], foreground=C["blue"])
        t.tag_configure("bold", font=F["bold"])
        t.tag_configure("quote", foreground=C["dim"], lmargin1=18, lmargin2=18)
        t.tag_configure("bullet", lmargin1=12, lmargin2=28)
        t.tag_configure("table", font=F["mono"], foreground=C["text"])
        try:
            with open(path, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError as ex:
            text = str(ex)
        md = path.lower().endswith(".md")
        in_code = False
        for ln in text.splitlines():
            if not md:
                t.insert("end", ln + "\n")
                continue
            if ln.strip().startswith("```"):
                in_code = not in_code
                continue
            if in_code:
                t.insert("end", ln + "\n", "code")
            elif ln.startswith("### "):
                t.insert("end", ln[4:] + "\n", "h3")
            elif ln.startswith("## "):
                t.insert("end", ln[3:] + "\n", "h2")
            elif ln.startswith("# "):
                t.insert("end", ln[2:] + "\n", "h1")
            elif ln.lstrip().startswith("|"):
                if re.match(r"^\s*\|[\s:|-]+\|\s*$", ln):
                    continue
                t.insert("end", ln + "\n", "table")
            elif ln.startswith(">"):
                self._inline(t, ln.lstrip("> "), "quote")
            elif re.match(r"^\s*([-*]|\d+\.)\s", ln):
                self._inline(t, "  • " + re.sub(r"^\s*([-*]|\d+\.)\s", "", ln) if not re.match(r"^\s*\d+\.", ln)
                             else "  " + ln.strip(), "bullet")
            else:
                self._inline(t, ln, None)
        t.configure(state="disabled")

    @staticmethod
    def _inline(t, ln, base):
        parts = re.split(r"(`[^`]+`|\*\*[^*]+\*\*)", ln)
        for p in parts:
            if p.startswith("`") and p.endswith("`") and len(p) > 1:
                t.insert("end", p[1:-1], ("inline",) + ((base,) if base else ()))
            elif p.startswith("**") and p.endswith("**") and len(p) > 3:
                t.insert("end", p[2:-2], ("bold",) + ((base,) if base else ()))
            else:
                t.insert("end", p, (base,) if base else ())
        t.insert("end", "\n", (base,) if base else ())


# ================================================================================ marco principal
class LogPanel(tk.Frame):
    def __init__(self, master, app, expanded):
        super().__init__(master, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
        self.app = app
        bar = tk.Frame(self, bg=C["card_hi"])
        bar.pack(fill="x")
        label(bar, T("Registro", "Log"), font=F["bold"], bg=C["card_hi"]).pack(side="left", padx=12, pady=6)
        self.last = label(bar, "", fg=C["dim"], font=F["small"], bg=C["card_hi"])
        self.last.pack(side="left", fill="x", expand=True)
        self.toggle_btn = ttk.Button(bar, text="", style="Small.TButton", command=self.toggle)
        self.toggle_btn.pack(side="right", padx=(4, 8), pady=4)
        for txt, fn in ((T("Guardar…", "Save…"), self.save), (T("Copiar", "Copy"), self.copy),
                        (T("Limpiar", "Clear"), self.clear)):
            ttk.Button(bar, text=txt, style="Small.TButton", command=fn).pack(side="right", padx=2, pady=4)
        self.body = tk.Frame(self, bg=C["log_bg"])
        self.text = tk.Text(self.body, height=8, bg=C["log_bg"], fg="#C9CED8",
                            font=F["mono"], relief="flat", bd=0, padx=10, pady=6, wrap="word",
                            selectbackground=C["accent_dim"], insertbackground=C["text"])
        vsb = ttk.Scrollbar(self.body, orient="vertical", command=self.text.yview)
        self.text.configure(yscrollcommand=vsb.set, state="disabled")
        self.text.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        for tag, col in (("cmd", C["blue"]), ("err", C["err"]), ("ok", C["ok"]), ("warn", C["warn"]),
                         ("dim", C["dim"])):
            self.text.tag_configure(tag, foreground=col)
        self.expanded = None
        self.set_expanded(expanded)
        for chunk, tag in app.log_buffer:
            self._insert(chunk, tag)

    def set_expanded(self, on):
        if on == self.expanded:
            return
        self.expanded = on
        if on:
            self.body.pack(fill="both", expand=True)
        else:
            self.body.pack_forget()
        self.toggle_btn.configure(text=("▾ " + T("Ocultar", "Hide")) if on else ("▴ " + T("Ver detalles",
                                                                                         "Show details")))

    def toggle(self):
        self.set_expanded(not self.expanded)
        self.app.state["log_open_%s" % ("adv" if self.app.adv else "basic")] = self.expanded

    @staticmethod
    def classify(line):
        low = line.lower().strip()
        if line.startswith("$ "):
            return "cmd"
        if ("traceback" in low or low.startswith("error") or "error:" in low or low.startswith("!!") or
                low.startswith("[exit code") or "exception" in low or low.startswith("fallo")):
            return "err"
        if low.startswith(("aviso", "warning", "warn", "⚠")):
            return "warn"
        if low.startswith(("ok", "listo", "hecho", "✔")) or " listo:" in low:
            return "ok"
        return None

    def _insert(self, text, tag=None):
        self.text.configure(state="normal")
        for ln in text.splitlines(keepends=True):
            self.text.insert("end", ln, tag or self.classify(ln) or ())
        lines = int(self.text.index("end-1c").split(".")[0])
        if lines > 6000:
            self.text.delete("1.0", "%d.0" % (lines - 5000))
        self.text.configure(state="disabled")
        self.text.see("end")
        ll = last_line(text)
        if ll:
            self.last.configure(text="  " + ll[:160])

    def append(self, text, tag=None):
        self._insert(text, tag)

    def clear(self):
        self.app.log_buffer.clear()
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        self.text.configure(state="disabled")
        self.last.configure(text="")

    def copy(self):
        self.app.root.clipboard_clear()
        self.app.root.clipboard_append(self.text.get("1.0", "end-1c"))

    def save(self):
        p = filedialog.asksaveasfilename(defaultextension=".txt", initialfile="modkit_log.txt",
                                         filetypes=[(T("Texto", "Text"), "*.txt")])
        if p:
            try:
                with open(p, "w", encoding="utf-8") as fh:
                    fh.write(self.text.get("1.0", "end-1c"))
            except OSError as ex:
                self.app.info(str(ex))


class StatusBar(tk.Frame):
    def __init__(self, master, app):
        super().__init__(master, bg=C["card_hi"])
        self.app = app
        self.game = label(self, "", fg=C["dim"], font=F["small"], bg=C["card_hi"])
        self.game.pack(side="left", padx=12, pady=5)
        self.py = label(self, "", fg=C["dim"], font=F["small"], bg=C["card_hi"])
        self.py.pack(side="right", padx=12)
        self.cancel = ttk.Button(self, text="■  " + T("Cancelar", "Cancel"), style="Danger.TButton",
                                 command=app.jobs.cancel)
        self.pb = ttk.Progressbar(self, mode="indeterminate", length=150)
        self.job = label(self, "", font=F["bold_sm"], bg=C["card_hi"])
        self.job.pack(side="right", padx=8)
        self.refresh_env()
        if app.jobs.busy():
            self.job_started(app.jobs.job)

    def refresh_env(self):
        e = self.app.env
        self.game.configure(text=(T("Juego: %s", "Game: %s") % e.game) if e.game else
                            T("Juego: no encontrado (Comprobar instalación)", "Game: not found (Check installation)"),
                            fg=C["dim"] if e.game else C["warn"])
        self.py.configure(text="Python %d.%d.%d" % sys.version_info[:3])

    def job_started(self, job):
        self.job.configure(text=T("Trabajando: %s…", "Working: %s…") % job.title, fg=C["warn"])
        self.cancel.pack(side="right", padx=(4, 4), pady=3)
        self.pb.pack(side="right", padx=4)
        self.pb.start(12)

    def job_finished(self, job):
        self.pb.stop()
        self.pb.pack_forget()
        self.cancel.pack_forget()
        dt = time.time() - job.t0
        if job.cancelled:
            self.job.configure(text=T("%s: cancelado", "%s: cancelled") % job.title, fg=C["warn"])
        elif job.rc == 0:
            self.job.configure(text=T("%s: hecho (%.1f s)", "%s: done (%.1f s)") % (job.title, dt), fg=C["ok"])
        else:
            self.job.configure(text=T("%s: error (código %s)", "%s: error (code %s)") % (job.title, job.rc),
                               fg=C["err"])


ADV_TABS = [("setup", "Entorno", "Environment"), ("mods", "Mods", "Mods"), ("importer", "Importar", "Import"),
            ("characters", "Personajes", "Characters"), ("create", "Crear", "Create"),
            ("textures", "Texturas", "Textures"), ("swap", "Cambio de modelo", "Model swap"),
            ("tools", "Herramientas", "Tools"), ("hex", "Hex", "Hex"), ("diag", "Diagnóstico", "Diagnostics"),
            ("help", "Ayuda", "Help")]
PAGE_CLASSES = {"home": HomePage, "setup": SetupPage, "mods": ModsPage, "importer": ImporterPage,
                "characters": CharactersPage, "create": CreatePage, "textures": TexturesPage, "swap": SwapPage,
                "tools": ToolsPage, "hex": HexPage, "diag": DiagPage, "help": HelpPage}


class App:
    def __init__(self, root, lang=None, mode=None, selftest=False):
        global LANG
        self.root, self.selftest = root, selftest
        self.settings = load_settings()
        LANG = lang or self.settings.get("lang") or "es"
        self.adv = (mode or self.settings.get("mode") or "basic") == "advanced"
        self.env = Env(self.settings)
        self.jobs = JobRunner(self)
        self.cache, self.state, self.log_buffer, self.selftest_cmds = {}, {}, [], []
        self.pages = {}
        init_fonts(root)
        setup_style(root)
        root.configure(bg=C["bg"])
        root.title("DBZ3 HD Mod Kit")
        root.minsize(1020, 660)
        self.logo = dragonball_image(44, C["bg"])
        try:
            self.icon = dragonball_image(32)
            root.iconphoto(True, self.icon)
        except tk.TclError:
            pass
        root.bind_all("<MouseWheel>", self._wheel, add="+")
        root.bind("<F5>", lambda _e: self.visible_page() and self.visible_page().on_show())
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.build()
        if not selftest:
            root.after(400, self.prefetch)

    def prefetch(self):
        """Busca los juegos de ps2_games en segundo plano (importar.py fuentes, como el launcher)."""
        if "sources" in self.cache or not self.env.tools_installed() or self.jobs.busy():
            return

        def done(job):
            srcs = parse_sources(job.text)
            if srcs:
                self.cache["sources"] = srcs
                self.cache.pop("checks", None)
            page = self.visible_page()
            if page is not None and page.key in ("home", "setup", "importer"):
                try:
                    page.on_show()
                except tk.TclError:
                    pass
        self.run(cmd_importer(self.env, ["fuentes"]), T("Buscando juegos", "Looking for games"), done, quiet=True)

    def visible_page(self):
        if self.adv:
            try:
                return self.pages[ADV_TABS[self.nb.index(self.nb.select())][0]]
            except (tk.TclError, KeyError, IndexError, AttributeError):
                return None
        return getattr(self, "_basic_current", None)

    # ------------------------------------------------------------------ construccion
    def build(self):
        for w in self.root.winfo_children():
            if not isinstance(w, tk.Toplevel):
                w.destroy()
        self.pages = {}
        self._build_header()
        self.status = StatusBar(self.root, self)
        self.status.pack(side="bottom", fill="x")
        open_default = self.adv
        self.log = LogPanel(self.root, self, self.state.get("log_open_%s" % ("adv" if self.adv else "basic"),
                                                            open_default))
        self.log.pack(side="bottom", fill="x", padx=14, pady=(0, 8))
        self.content = tk.Frame(self.root, bg=C["bg"])
        self.content.pack(fill="both", expand=True)
        if self.adv:
            self.nb = ttk.Notebook(self.content)
            self.nb.pack(fill="both", expand=True, pady=(4, 0))
            for key, es, en in ADV_TABS:
                page = PAGE_CLASSES[key](self.nb, self, True)
                self.pages[key] = page
                self.nb.add(page, text=T(es, en))
            self.nb.bind("<<NotebookTabChanged>>", self._tab_changed)
            idx = self.state.get("adv_tab", 0)
            self.nb.select(min(idx, len(ADV_TABS) - 1))
            self._tab_changed()
        else:
            self.show_basic(self.state.get("basic_page", "home"))

    def _build_header(self):
        h = tk.Frame(self.root, bg=C["bg"])
        h.pack(fill="x", padx=20, pady=(12, 8))
        tk.Label(h, image=self.logo, bg=C["bg"]).pack(side="left")
        tf = tk.Frame(h, bg=C["bg"])
        tf.pack(side="left", padx=12)
        tr = tk.Frame(tf, bg=C["bg"])
        tr.pack(anchor="w")
        tk.Label(tr, text="DBZ3 HD", bg=C["bg"], fg=C["accent"], font=F["h1"]).pack(side="left")
        tk.Label(tr, text=" Mod Kit", bg=C["bg"], fg=C["text"], font=F["h1"]).pack(side="left")
        label(tf, T("Budokai 3 HD Collection · todas las herramientas de modding, sin consola",
                    "Budokai 3 HD Collection · every modding tool, no console needed"), fg=C["dim"]).pack(anchor="w")
        right = tk.Frame(h, bg=C["bg"])
        right.pack(side="right")
        for title, opts, val, cmd in ((T("Idioma", "Language"), [("es", "ES"), ("en", "EN")], LANG, self.set_lang),
                                      (T("Modo", "Mode"), [("basic", T("Básico", "Basic")),
                                                           ("advanced", T("Avanzado", "Advanced"))],
                                       "advanced" if self.adv else "basic", self.set_mode)):
            col = tk.Frame(right, bg=C["bg"])
            col.pack(side="right", padx=(14, 0))
            label(col, title, fg=C["dim"], font=F["small"]).pack(anchor="e")
            Segmented(col, opts, val, cmd).pack(anchor="e")
        tk.Frame(self.root, bg=C["accent"], height=2).pack(fill="x")

    def show_basic(self, key):
        if key not in PAGE_CLASSES or key == "tools":
            key = "home"
        if getattr(self, "_basic_current", None) is not None:
            try:
                self._basic_current.pack_forget()
            except tk.TclError:
                pass
        page = self.pages.get(key)
        if page is None:
            page = PAGE_CLASSES[key](self.content, self, False)
            self.pages[key] = page
        page.pack(fill="both", expand=True)
        self._basic_current = page
        self.state["basic_page"] = key
        page.on_show()
        return page

    def _tab_changed(self, _e=None):
        try:
            idx = self.nb.index(self.nb.select())
        except tk.TclError:
            return
        self.state["adv_tab"] = idx
        self.pages[ADV_TABS[idx][0]].on_show()

    def rebuild(self):
        global ADV_LABELS
        ADV_LABELS = self.adv
        self._basic_current = None
        self.build()

    def set_lang(self, lang):
        global LANG
        LANG = lang
        self.settings["lang"] = lang
        self.save()
        self.rebuild()

    def set_mode(self, mode):
        self.adv = mode == "advanced"
        self.settings["mode"] = mode
        self.save()
        self.rebuild()

    def open_studio(self, personaje=None):
        """Studio (ventana propia): mismas carpetas de mods/us que el Mod Kit."""
        cmd = [self.env.python, os.path.join(HERE, "studio", "studio_gui.py"), "--mods", self.env.mods,
               "--lang", LANG] + (["--us", self.env.us] if self.env.us else []) + (
            ["--personaje", str(personaje)] if personaje is not None else [])
        if self.selftest:
            self.selftest_cmds.append(("studio", [str(c) for c in cmd]))
            return
        try:
            subprocess.Popen([str(c) for c in cmd], cwd=self.env.cwd(), creationflags=NO_WINDOW)
            self.log_text(T("Studio abierto (ventana aparte).\n", "Studio opened (separate window).\n"), "ok")
        except OSError as ex:
            self.info(T("No se pudo abrir el Studio: %s", "Could not open the Studio: %s") % ex)

    def goto(self, key, **kw):
        if key == "studio":
            self.open_studio(kw.get("personaje"))
            return
        if key == "home":
            if self.adv:
                return
            self.show_basic("home")
            return
        if key == "tools" and not self.adv:
            self.set_mode("advanced")
        if self.adv:
            idx = next((i for i, (k, _, _) in enumerate(ADV_TABS) if k == key), 0)
            self.nb.select(idx)
            page = self.pages[key]
        else:
            page = self.show_basic(key)
        if kw and hasattr(page, "focus_item"):
            page.focus_item(**kw)

    def _wheel(self, e):
        try:
            w = self.root.winfo_containing(e.x_root, e.y_root)
        except (tk.TclError, KeyError):
            return
        while w is not None:
            if isinstance(w, (tk.Text, tk.Listbox, ttk.Treeview, ttk.Combobox, ttk.Spinbox)):
                return
            sf = getattr(w, "_scrollframe", None)
            if sf is not None:
                sf.scroll(e.delta)
                return
            w = getattr(w, "master", None)

    # ------------------------------------------------------------------ trabajos y registro
    def run(self, cmd, title, done=None, quiet=False, cwd=None):
        if self.selftest:
            self.selftest_cmds.append((title, [str(c) for c in cmd]))
            return False
        if self.jobs.busy():
            self.info(T("Ya hay una tarea en marcha. Espera a que termine o pulsa «Cancelar» abajo.",
                        "A task is already running. Wait for it to finish or press 'Cancel' below."))
            return False
        job = Job(cmd, title, done, cwd or self.env.cwd(), quiet)
        self.log_text("$ %s\n" % cmdline(job.cmd), "cmd")
        self.status.job_started(job)
        self.jobs.start(job)
        return True

    def job_finished(self, job):
        if job.quiet and job.rc != 0:
            self.log_text(job.text)
        if job.rc == 9009:
            self.log_text(T("No se pudo ejecutar Python (el alias de Microsoft Store no sirve). Instala Python "
                            "3.11+ de python.org.\n", "Python could not be started (the Microsoft Store alias does "
                            "not work). Install Python 3.11+ from python.org.\n"), "err")
        elif "No module named" in job.text:
            self.log_text(T("Falta una librería de Python (numpy / Pillow / scipy): «Comprobar instalación» → "
                            "«Instalar requisitos».\n", "A Python library is missing (numpy / Pillow / scipy): "
                            "'Check installation' → 'Install requirements'.\n"), "warn")
        dt = time.time() - job.t0
        if job.cancelled:
            self.log_text(T("[%s: cancelado]\n", "[%s: cancelled]\n") % job.title, "warn")
        elif job.rc == 0:
            if not job.quiet:
                self.log_text(T("[%s: hecho en %.1f s]\n", "[%s: done in %.1f s]\n") % (job.title, dt), "ok")
        else:
            self.log_text(T("[%s: falló, código %s]\n", "[%s: failed, code %s]\n") % (job.title, job.rc), "err")
            if not job.quiet:
                try:
                    self.log.set_expanded(True)
                except tk.TclError:
                    pass
        try:
            self.status.job_finished(job)
        except tk.TclError:
            pass
        if job.done:
            try:
                job.done(job)
            except tk.TclError:
                pass
            except Exception:  # noqa: BLE001
                self.log_text(traceback.format_exc(), "err")

    def outcome(self, job, ok_text=None):
        if job.cancelled:
            return "warn", T("Cancelado.", "Cancelled.")
        if job.rc == 0:
            return "ok", ok_text or T("Hecho.", "Done.")
        if "No module named" in job.text:
            return "err", T("Falta una librería de Python (numpy / Pillow / scipy). Ve a «Comprobar instalación» y "
                            "pulsa «Instalar requisitos».", "A Python library is missing (numpy / Pillow / scipy). "
                            "Go to 'Check installation' and press 'Install requirements'.")
        return "err", T("Algo falló (código %s): %s", "Something failed (code %s): %s") % (job.rc,
                                                                                        last_line(job.text)[:240])

    def log_text(self, text, tag=None):
        text = text.replace("\r\n", "\n").replace("\r", "\n")
        self.log_buffer.append((text, tag))
        if len(self.log_buffer) > 4000:
            del self.log_buffer[:1000]
        try:
            self.log.append(text, tag)
        except (tk.TclError, AttributeError):
            pass

    # ------------------------------------------------------------------ utilidades
    def checks(self, force=False):
        if force or "checks" not in self.cache:
            self.cache["checks"] = collect_checks(self)
        return self.cache["checks"]

    def pick_game(self):
        p = filedialog.askdirectory(title=T("Carpeta del juego (dbz3.exe)", "Game folder (dbz3.exe)"))
        if not p:
            return
        if not os.path.isfile(os.path.join(p, "dbz3.exe")) and not self.ask(
                T("No hay dbz3.exe en esa carpeta. ¿Usarla igualmente?", "There is no dbz3.exe in that folder. Use "
                  "it anyway?")):
            return
        self.settings["game_dir"] = os.path.normpath(p)
        self.save()
        self.env.refresh()
        self.cache.pop("checks", None)
        self.rebuild()

    def save(self):
        if not self.selftest:
            save_settings(self.settings)

    def info(self, msg):
        if self.selftest:
            self.selftest_cmds.append(("info", [msg]))
            return
        messagebox.showinfo("DBZ3 HD Mod Kit", msg, parent=self.root)

    def ask(self, msg, parent=None):
        if self.selftest:
            return False
        return messagebox.askyesno("DBZ3 HD Mod Kit", msg, parent=parent or self.root)

    def close(self):
        if self.jobs.busy():
            if not self.ask(T("Hay una tarea en marcha. ¿Cancelarla y salir?", "A task is running. Cancel it and "
                              "quit?")):
                return
            self.jobs.cancel()
        try:
            self.settings["geometry"] = self.root.winfo_geometry()
        except tk.TclError:
            pass
        self.save()
        self.root.destroy()


# ================================================================================ autotest
def selftest(lang=None):
    """Construye TODAS las pantallas (basico y avanzado, ES y EN) sin mostrar la ventana, genera los
    formularios de todas las herramientas y prueba el cableado real con `importar.py fuentes`."""
    global LANG
    t0 = time.time()
    ok = True
    out = []

    def say(msg):
        out.append(msg)
        print("[selftest] " + msg, flush=True)

    try:
        sys.stdout.reconfigure(errors="backslashreplace")   # consola cp1252: que una flecha no lo pare
    except (AttributeError, ValueError):
        pass

    root = tk.Tk()
    root.withdraw()
    try:
        app = App(root, lang=lang or "es", mode="basic", selftest=True)
        env = app.env
        say("python %s  (%s)" % (".".join(map(str, sys.version_info[:3])), env.python))
        say("kit:   %s" % env.kit)
        say("game:  %s" % (env.game or "(dbz3.exe not found)"))
        say("mods:  %s  (exists=%s)" % (env.mods, os.path.isdir(env.mods)))
        say("us:    %s  (pass --us/--afs: %s)" % (env.us or "(not found)", env.pass_us()))
        built = []
        for lg in ("es", "en"):
            for mode in ("basic", "advanced"):
                LANG = lg
                app.adv = mode == "advanced"
                app.rebuild()
                if mode == "basic":
                    for key, *_ in PAGE_DEFS:
                        app.goto(key)
                        root.update_idletasks()
                    app.goto("home")
                else:
                    for i, (key, _, _) in enumerate(ADV_TABS):
                        app.nb.select(i)
                        app._tab_changed()
                        root.update_idletasks()
                built.append("%s/%s:%d" % (lg, mode, len(app.pages)))
        say("screens built: " + ", ".join(built))
        LANG = lang or "es"
        # diagnostico: un log sintetico con 30 FPS sostenidos en una version antigua
        import diagnostico
        sample = "\n".join(
            ["[2026-10-04 17:51:47.887] [info] [core] [t1] dbz3: entorno os=10.0.26300 ram=65219MB "
             "dbz3.exe=1.3.0.0 rexgpu-xenos=1.3.0 rexruntime=1.3.0 amd_fidelityfx_dx12.dll=1.0.1.0"] +
            ["[2026-10-04 17:53:%02d.000] [info] [gpu] [t2] dbz3: perf fps=30.0 frames=150 window=5.00s "
             "max_frame_ms=33.5 fg=1 cfg=x" % i for i in range(4)])
        levels = [f[0] for f in diagnostico.analizar(sample)]
        if "err" not in levels or "warn" not in levels:
            ok = False
            say("DIAG FAIL: %s" % levels)
        app.goto("diag")
        app.pages["diag"].report = diagnostico.informe(sample)
        say("diagnostico: %d hallazgos (%s)" % (len(levels), ",".join(levels)))
        # todas las herramientas con su formulario
        tp = app.pages["tools"]
        tp.on_show()
        kinds = {}
        forms = failures = 0
        for t in tp.tools:
            kinds[t.kind] = kinds.get(t.kind, 0) + 1
            try:
                tp.show_tool(t)
                root.update_idletasks()
                if tp.form is not None:
                    forms += 1
                    try:
                        tp.build_cmd()
                    except ValueError:
                        pass      # faltan obligatorios: es lo esperado con el formulario vacio
            except Exception:  # noqa: BLE001
                failures += 1
                ok = False
                say("FORM FAIL %s\n%s" % (t.name, traceback.format_exc()))
        say("tools: %d scripts %s -> %d argparse forms built, %d failures" % (
            len(tp.tools), json.dumps(kinds, sort_keys=True), forms, failures))
        imp = next((t for t in tp.tools if t.name == "importar.py"), None)
        rb = next((t for t in tp.tools if t.name == "roster_build.py"), None)
        for t in (imp, rb):
            if t is None or t.kind != "argparse" or not t.spec.sub:
                ok = False
                say("FAIL: argparse spec of %s not read" % (t.name if t else "?"))
            else:
                say("%s subcommands: %s" % (t.name, ", ".join(t.spec.sub["parsers"])))
        # utilidades puras
        checks = [
            (mod_slug("b1", "Zarbon (Monster)") == "imp_b1_zarbon_monster", "mod_slug"),
            (split_args('a "b c" C:\\x\\y') == ["a", "b c", "C:\\x\\y"], "split_args"),
            (parse_sources("fuente\tb1\tBudokai 1\tlisto\tC:\\x.iso\tnota") == [
                {"id": "b1", "name": "Budokai 1", "state": "listo", "path": "C:\\x.iso", "note": "nota"}],
             "parse_sources"),
            (parse_entries("personaje\t13\tZarbon\t38\tb1\t4\t0\tZarbon B1")[0]["donor"] == 38, "parse_entries"),
            (assign_slots([("a", True, 25), ("b", True, 25), ("c", False, -1)]) == {"a": 25, "b": 22}, "assign_slots"),
            (cmd_roster_build(env)[2:4] == ["construir", "--mods"], "cmd_roster_build"),
            (cmd_swap(env, 327, 298)[2:8] == ["--origen", "327", "--dest", "298", "--mod", "swap_327_on_298"],
             "cmd_swap"),
            (cmd_tex_extract(env, 91, "tex_91")[2:6] == ["extract", "--bin", "91", "--mod"], "cmd_tex_extract"),
        ]
        for good, name in checks:
            if not good:
                ok = False
                say("FAIL: " + name)
        say("pure helpers: %d/%d OK" % (sum(1 for g, _ in checks if g), len(checks)))
        say("page queries queued while building (not run): %d" % len(app.selftest_cmds))
        # acciones de cada pagina con datos de ejemplo: el comando se registra, NO se ejecuta
        app.adv = True
        app.rebuild()
        n0 = len(app.selftest_cmds)
        imp_page = app.pages["importer"]
        app.cache["sources"] = [{"id": "b1", "name": "Budokai 1", "state": "listo", "path": "x.iso", "note": ""}]
        app.cache.setdefault("entries", {})["b1|"] = [{"key": "13", "name": "Zarbon", "donor": 38, "kind": "b1",
                                                       "count": 4, "port": False, "suggested": "Zarbon B1"}]
        imp_page.render_tiles()
        imp_page.pick_source("b1")
        imp_page.tree.selection_set("0")
        imp_page.pick_entry()
        imp_page.do_import()
        cp = app.pages["create"]
        cp.name.set("Selftest Hero")
        cp.models.insert("end", os.path.join(env.kit, "modelo_de_prueba.amb"))
        cp.create()
        cat = load_catalog(env)
        if cat:
            tx = app.pages["textures"]
            tx.src.set_char(cat[0])
            tx.extract()
            sw = app.pages["swap"]
            sw.src.set_char(cat[0])
            sw.dst.set_char(cat[-1])
            sw.swap()
        chp = app.pages["characters"]
        chp.reload()
        if chp.chars:
            chp.preview(chp.chars[0], ["--nombre", "Prueba", "--guardar", "--solo", "icono"])
            chp.caps(chp.chars[0], ["--anadir", "Prueba", "especial"])
        mp = app.pages["mods"]
        mp.refresh()
        if mp.mods:
            mp.tree.selection_set(mp.mods[0]["name"])
            mp.show_details()
            tomls = [m["toml"] for m in mp.mods if m["toml"]]
            dialogs = [NewModDialog(app, mp), DocViewer(app, __file__, "selftest")]
            if tomls:
                dialogs += [TomlFormDialog(app, tomls[0]), TextEditor(app, tomls[:2])]
            root.update_idletasks()
            for dlg in dialogs:
                dlg.destroy()
            say("dialogs built: %d (hidden, nothing saved)" % len(dialogs))
        acts = app.selftest_cmds[n0:]
        say("page actions -> commands (recorded, not run): %d" % len(acts))
        for title, cmd in acts:
            say("   %s: %s" % (title, cmdline(cmd[1:])[:230]))
        imp_cmd = next((c for t, c in acts if c[2:3] == ["importar"]), None)
        if not imp_cmd or imp_cmd[3:9] != ["b1", "13", "--mod", unique_mod_name(env.mods, "imp_b1_zarbon_b1"),
                                           "--nombre", "Zarbon B1"]:
            ok = False
            say("FAIL: importer page command %s" % imp_cmd)
        app.goto("studio", personaje=0)
        st_cmd = ([c for t, c in app.selftest_cmds if t == "studio"] or [None])[-1]
        if not st_cmd or not st_cmd[1].endswith("studio_gui.py") or st_cmd[-2:] != ["--personaje", "0"]:
            ok = False
            say("FAIL: studio command %s" % st_cmd)
        rc, text = run_sync([env.python, os.path.join(HERE, "studio", "studio_core.py"), "selftest", "--rapido"],
                            env.cwd(), timeout=300)
        say("studio_core selftest --rapido -> rc=%s, %s" % (rc, last_line(text)))
        if rc != 0:
            ok = False
            say(text[-800:])
        # cableado real: el mismo constructor y el mismo lanzador que usa la ventana
        if env.tools_installed():
            cmd = cmd_importer(env, ["fuentes"])
            say("wiring: %s" % cmdline(cmd))
            rc, text = run_sync(cmd, env.cwd(), timeout=120)
            srcs = parse_sources(text)
            for s in srcs:
                say("   %-5s %-42s %-10s %s" % (s["id"], s["name"], s["state"], s["path"]))
            if rc != 0 or not srcs:
                ok = False
                say("FAIL: importar.py fuentes rc=%s\n%s" % (rc, text[-800:]))
            else:
                say("importar.py fuentes -> rc=0, %d sources parsed" % len(srcs))
        else:
            ok = False
            say("FAIL: importar.py / roster_build.py not found next to the GUI")
        root.update_idletasks()
    except Exception:  # noqa: BLE001
        ok = False
        say("EXCEPTION\n" + traceback.format_exc())
    finally:
        try:
            root.destroy()
        except tk.TclError:
            pass
    say("RESULT: %s (%.1f s)" % ("OK" if ok else "FAILED", time.time() - t0))
    return 0 if ok else 1


# ================================================================================ main
def main(argv=None):
    ap = argparse.ArgumentParser(description="DBZ3 HD Mod Kit")
    ap.add_argument("--lang", choices=("es", "en"))
    ap.add_argument("--advanced", action="store_true", help="abrir en modo avanzado")
    ap.add_argument("--basic", action="store_true", help="abrir en modo basico")
    ap.add_argument("--selftest", action="store_true", help="construir todo sin mostrar la ventana y salir")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest(a.lang)
    if IS_WIN:
        try:
            import ctypes
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("DBZ3HD.ModKit")
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except (AttributeError, OSError):
            pass
    root = tk.Tk()
    root.withdraw()
    mode = "advanced" if a.advanced else "basic" if a.basic else None
    app = App(root, lang=a.lang, mode=mode)
    geo = app.settings.get("geometry")
    if geo and re.match(r"^\d+x\d+[+-]-?\d+[+-]-?\d+$", geo):
        root.geometry(geo)
    else:
        w, h = 1280, 840
        sw, sh = root.winfo_screenwidth(), root.winfo_screenheight()
        w, h = min(w, sw - 60), min(h, sh - 80)
        root.geometry("%dx%d+%d+%d" % (w, h, max(0, (sw - w) // 2), max(0, (sh - h) // 3)))
    root.deiconify()
    root.mainloop()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except Exception:  # noqa: BLE001
        err = traceback.format_exc()
        log = os.path.join(os.environ.get("TEMP") or HERE, "dbz3_modkit_error.log")
        try:
            with open(log, "w", encoding="utf-8") as fh:
                fh.write(err)
        except OSError:
            pass
        try:
            messagebox.showerror("DBZ3 HD Mod Kit", "Error al abrir / startup error:\n\n%s\n\n(%s)" % (
                err[-1500:], log))
        except Exception:  # noqa: BLE001
            print(err, file=sys.stderr)
        sys.exit(1)
