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

HERE = os.path.dirname(os.path.abspath(__file__))          # .../mod center hd
sys.path.insert(0, HERE)
# KIT-05: el cuerpo vive en kit/nucleo.py (comun) y kit/paginas/ (una pagina por fichero). Aqui quedan el arranque,
# el marco (registro, pie), el registro de paginas, App y la autoprueba. Todo se reexporta: `modkit_gui.X` sigue valiendo.
from kit import nucleo  # noqa: E402
from kit.nucleo import *  # noqa: E402,F401,F403
from kit import asistente, ayudas, errores, portada, prueba, revisar, vistas  # noqa: E402,F401
if nucleo.G is None:          # el `g` de las piezas de kit/ = este modulo (como __main__ o importado)
    nucleo.G = sys.modules[__name__]
from kit.paginas.dialogos import *  # noqa: E402,F401,F403
from kit.paginas.pagina_inicio import *  # noqa: E402,F401,F403
from kit.paginas.pagina_entorno import *  # noqa: E402,F401,F403
from kit.paginas.pagina_mods import *  # noqa: E402,F401,F403
from kit.paginas.pagina_importar import *  # noqa: E402,F401,F403
from kit.paginas.pagina_personajes import *  # noqa: E402,F401,F403
from kit.paginas.pagina_crear import *  # noqa: E402,F401,F403
from kit.paginas.pagina_texturas import *  # noqa: E402,F401,F403
from kit.paginas.pagina_cambio_modelo import *  # noqa: E402,F401,F403
from kit.paginas.pagina_herramientas import *  # noqa: E402,F401,F403
from kit.paginas.pagina_historias import *  # noqa: E402,F401,F403
from kit.paginas.pagina_hex import *  # noqa: E402,F401,F403
from kit.paginas.pagina_diagnostico import *  # noqa: E402,F401,F403
from kit.paginas.pagina_ayuda import *  # noqa: E402,F401,F403
from kit.paginas.pagina_arte_opcional import *  # noqa: E402,F401,F403
from kit.paginas.pagina_juego_ps2 import *  # noqa: E402,F401,F403
from kit.paginas.pagina_construccion import *  # noqa: E402,F401,F403
from kit.paginas.pagina_historias import _reto_mode_name  # noqa: E402,F401


def __getattr__(name):
    """g.LANG / g.ADV_LABELS: siempre el valor vivo de kit.nucleo (asistente, ayudas, pagina_ps2... los leen asi)."""
    if name in ("LANG", "ADV_LABELS"):
        return getattr(nucleo, name)
    raise AttributeError(name)


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
        self.title = job.title
        self.job.configure(text=T("Trabajando: %s…", "Working: %s…") % job.title, fg=C["warn"])

    def job_progress(self, line):
        if line and getattr(self, "title", None):
            self.job.configure(text=T("Trabajando: %s…", "Working: %s…") % self.title + "  ·  " + line[:90])
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
            ("stories", "Historias", "Stories"),
            ("textures", "Texturas", "Textures"), ("swap", "Cambio de modelo", "Model swap"),
            ("biblioteca", "Biblioteca", "Library"),
            ("tools", "Experto", "Expert"), ("hex", "Hex", "Hex"), ("diag", "Diagnóstico", "Diagnostics"),
            ("help", "Ayuda", "Help")]


PAGE_CLASSES = {"ps2": Ps2Page, "arte": ArtePage, "home": HomePage, "setup": SetupPage, "mods": ModsPage, "importer": ImporterPage,
                "characters": CharactersPage, "create": CreatePage, "stories": StoriesPage,
                "textures": TexturesPage, "swap": SwapPage,
                "tools": ToolsPage, "hex": HexPage, "diag": DiagPage, "help": HelpPage}
PAGE_CLASSES.update(OBRA_CLASES)     # KIT-05: tarjetas de la portada aun sin asistente ("En construccion")
from kit.paginas.escenario import EscenarioPage  # noqa: E402  KIT-07: «Nuevo escenario» ya tiene asistente
PAGE_CLASSES["obra_escenario"] = EscenarioPage
from kit.paginas.fusiones import FusionesPage  # noqa: E402  KIT-07b: «Fusiones» ya tiene asistente
PAGE_CLASSES["obra_fusiones"] = FusionesPage
try:   # KIT-09: la pestaña Biblioteca es el generador de animaciones; «Nueva técnica» ya tiene asistente
    from kit.paginas.generador import GeneradorPage  # noqa: E402
    from kit.paginas.tecnica import TecnicaPage  # noqa: E402
    PAGE_CLASSES["biblioteca"] = GeneradorPage
    PAGE_CLASSES["obra_tecnica"] = TecnicaPage
except Exception:  # noqa: BLE001  sin numpy/PIL: la biblioteca vieja (R6 1.5.1) o nada
    try:
        from pagina_biblioteca import BibliotecaPage  # noqa: E402
        PAGE_CLASSES["biblioteca"] = BibliotecaPage
    except Exception:  # noqa: BLE001
        ADV_TABS = [t for t in ADV_TABS if t[0] != "biblioteca"]


class App:
    def __init__(self, root, lang=None, mode=None, selftest=False):
        self.root, self.selftest = root, selftest
        self.settings = load_settings()
        self.env = Env(self.settings)
        nucleo.LANG = lang or self.settings.get("lang") or detect_lang(self.env.game)
        self.adv = (mode or self.settings.get("mode") or "basic") == "advanced"
        self.juego = self.settings.get("juego", "hd")      # "hd" o "ps2" (kit/pagina_ps2.py)
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
        ayudas.instalar(root, sys.modules[__name__])
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
        if self.adv and self.juego != "ps2":
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
        if self.juego == "ps2":
            self._basic_current = None
            self.show_basic("ps2")
        elif self.adv:
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
        for title, opts, val, cmd in ((T("Juego", "Game"), [("hd", "B3 HD"), ("ps2", "B3 PS2")], self.juego,
                                       self.set_juego),
                                      (T("Idioma", "Language"), [("es", "ES"), ("en", "EN")], nucleo.LANG, self.set_lang),
                                      (T("Modo", "Mode"), [("basic", T("Fácil", "Easy")),
                                                           ("advanced", T("Avanzado", "Advanced"))],
                                       "advanced" if self.adv else "basic", self.set_mode)):
            col = tk.Frame(right, bg=C["bg"])
            col.pack(side="right", padx=(14, 0))
            label(col, title, fg=C["dim"], font=F["small"]).pack(anchor="e")
            Segmented(col, opts, val, cmd).pack(anchor="e")
            if cmd == self.set_juego:
                Tooltip(col, T("B3 HD: el port con todas las herramientas. B3 PS2: crea mods para tu copia de PS2 y una ISO "
                               "para el emulador; ahí se explica qué no está disponible.",
                               "B3 HD: the port with every tool. B3 PS2: creates mods for your PS2 copy and an ISO for the "
                               "emulator; it explains what is not available there."))
        tk.Frame(self.root, bg=C["accent"], height=2).pack(fill="x")

    def show_basic(self, key):
        if key not in PAGE_CLASSES or key == "tools" or (key == "ps2" and self.juego != "ps2"):
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
        nucleo.ADV_LABELS = self.adv
        self._basic_current = None
        self.build()

    def set_lang(self, lang):
        nucleo.LANG = lang
        self.settings["lang"] = lang
        self.save()
        self.rebuild()

    def set_juego(self, juego):
        self.juego = juego
        self.settings["juego"] = juego
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
               "--lang", nucleo.LANG] + (["--us", self.env.us] if self.env.us else []) + (
            ["--personaje", str(personaje)] if personaje is not None else [])
        if self.selftest:
            self.selftest_cmds.append(("studio", [str(c) for c in cmd]))
            return
        try:
            subprocess.Popen([str(c) for c in cmd], cwd=self.env.cwd(), creationflags=NO_WINDOW)
            self.log_text(T("Studio abierto (ventana aparte).\n", "Studio opened (separate window).\n"), "ok")
        except OSError as ex:
            self.info(T("No se pudo abrir el Studio: %s", "Could not open the Studio: %s") % ex)

    def open_story_editor(self, path=None):
        """Editor de historias (ventana propia), en el idioma del Mod Kit."""
        cmd = [self.env.python, os.path.join(HERE, "editor_historias.py"), "--lang", nucleo.LANG] + ([path] if path else [])
        if self.selftest:
            self.selftest_cmds.append(("editor", [str(c) for c in cmd]))
            return
        try:
            subprocess.Popen([str(c) for c in cmd], cwd=self.env.cwd(), creationflags=NO_WINDOW)
            self.log_text(T("Editor de historias abierto (ventana aparte).\n",
                            "Story editor opened (separate window).\n"), "ok")
        except OSError as ex:
            self.info(T("No se pudo abrir el editor: %s", "Could not open the editor: %s") % ex)

    def goto(self, key, **kw):
        if self.juego == "ps2":
            self.show_basic("ps2")
            return
        if key in asistente.ASISTENTES:               # asistentes guiados (ventana aparte, ambos modos)
            return asistente.abrir(self, key, sys.modules[__name__])
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
            # consulta de fondo (listas), o lo que pulsa el jugador mientras corre una de ellas (p. ej. la
            # busqueda de juegos del arranque): espera su turno en vez de un aviso emergente
            if quiet or (self.jobs.job is not None and self.jobs.job.quiet):
                self.root.after(500, lambda: self.run(cmd, title, done, quiet, cwd))
                return not quiet
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
                            "«Instalar librerías».\n", "A Python library is missing (numpy / Pillow / scipy): "
                            "'Check installation' → 'Install libraries'.\n"), "warn")
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
        que, hacer, _acc = errores.explicar(job.text, nucleo.LANG == "en")
        return "err", "%s %s" % (que, hacer)

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
def probar_deshacer(say):
    """Copia + indice + Deshacer en una carpeta temporal (no toca el juego ni los mods reales)."""
    import tempfile  # noqa: PLC0415
    tmp = tempfile.mkdtemp(prefix="s7_deshacer_")
    try:
        class _E:
            pass
        env = _E()
        env.mods = os.path.join(tmp, "mods")
        env.backups = os.path.join(tmp, "modkit_backups")
        f = os.path.join(env.mods, "mod_prueba", "personaje.toml")
        os.makedirs(os.path.dirname(f))
        with open(f, "w", encoding="utf-8") as fh:
            fh.write("v1\n")
        backup_file(f, env)
        with open(f, "w", encoding="utf-8") as fh:
            fh.write("v2\n")
        restaurados = deshacer_ultimo(env)
        with open(f, encoding="utf-8") as fh:
            volvio = fh.read() == "v1\n"
        vuelta = deshacer_ultimo(env)          # ya deshecho: no debe tocar nada mas
        guardada = any(n.endswith("_personaje.toml") for _, _, fs in os.walk(env.backups) for n in fs)
        ok = volvio and len(restaurados) == 1 and vuelta == [] and guardada
        say(("OK" if ok else "FAIL") + ": Deshacer ultimo paso (restaura, conserva lo anterior, no repite)")
        return ok
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def selftest(lang=None):
    """Construye TODAS las pantallas (basico y avanzado, ES y EN) sin mostrar la ventana, genera los
    formularios de todas las herramientas y prueba el cableado real con `importar.py fuentes`."""
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
                nucleo.LANG = lg
                app.adv = mode == "advanced"
                app.rebuild()
                if mode == "basic":
                    for key in [k for k, *_ in PAGE_DEFS] + list(OBRA_CLASES):
                        app.goto(key)
                        root.update_idletasks()
                    app.goto("home")
                else:
                    for i, (key, _, _) in enumerate(ADV_TABS):
                        app.nb.select(i)
                        app._tab_changed()
                        root.update_idletasks()
                built.append("%s/%s:%d" % (lg, mode, len(app.pages)))
                sp = app.pages.get("stories")          # Historias: el editor embebido debe existir y hablar el idioma
                if sp is None or sp.editor is None:
                    ok = False
                    say("FAIL: Historias: el editor no se construye (%s/%s)" % (lg, mode))
                elif sp.editor.nb.tab(0, "text") != ("1. Your story" if lg == "en" else "1. Tu historia"):
                    ok = False
                    say("FAIL: Historias: pestañas en otro idioma (%s/%s)" % (lg, mode))
        say("screens built: " + ", ".join(built))
        for lg in ("es", "en"):        # ayudas: cada boton de cada pantalla tiene la suya; modo PS2 construye
            nucleo.LANG = lg
            app.adv = False
            app.rebuild()
            faltan = set()
            for key in [k for k, *_ in PAGE_DEFS] + list(OBRA_CLASES):
                app.goto(key)
                root.update_idletasks()
                if app.pages.get(key) is not None:
                    faltan |= set(ayudas.sin_tip(app.pages[key], lg))
            if faltan:
                ok = False
                say("FAIL: botones sin ayuda (%s): %s" % (lg, sorted(faltan)))
            else:
                say("OK: ayudas en todos los botones (%s)" % lg)
            app.juego = "ps2"
            app.rebuild()
            root.update_idletasks()
            if app.visible_page().key != "ps2" or ayudas.sin_tip(app.visible_page(), lg):
                ok = False
                say("FAIL: modo PS2 (%s): %s" % (lg, ayudas.sin_tip(app.visible_page(), lg)))
            else:
                say("OK: modo PS2 (%s)" % lg)
            app.juego = "hd"
        app.adv = True
        app.rebuild()
        import ps2_b3
        ps2_b3.prueba()
        say("OK: ps2_b3 prueba")
        from PIL import Image as _I
        png = os.path.join(vistas.cache_dir(), "prueba_formato.png")
        _I.new("RGB", (1000, 300), (200, 30, 30)).save(png)
        tam = [[(i.width, i.height) for i, _e, _n in vistas.imagenes_formato(png, f)] for f in ("saga", "escenario")]
        if tam != [[(660, 420)], [(228, 228), (512, 372)]]:
            ok = False
            say("FAIL: vistas.imagenes_formato %s" % tam)
        rojo = vistas.foto_aura("#ff0000", "#00ff00").getpixel((75, 22))
        if rojo[0] <= rojo[1] or vistas.foto_aura("#ff0000").size != (150, 190):
            ok = False
            say("FAIL: vistas.foto_aura %s" % (rojo,))
        import wave
        wav = os.path.join(vistas.cache_dir(), "prueba_picos.wav")
        with wave.open(wav, "wb") as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(44100)
            w.writeframes(b"\x00\x40" * 88200)
        picos, seg = vistas.picos_wav(wav)
        if abs(seg - 1) > 0.01 or abs(max(picos) - 0.5) > 0.01:
            ok = False
            say("FAIL: vistas.picos_wav %s %s" % (seg, max(picos)))
        say("tabs basic: " + ", ".join(k for k, *_ in PAGE_DEFS))
        say("tabs advanced: " + ", ".join(k for k, _, _ in ADV_TABS))
        nucleo.LANG = lang or "es"
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
        # errores en llano: trazas reales de los docs (si estan) + ejemplos; ninguna debe dar "Traceback"
        extra = []
        for doc in ("qol.md", "modkit.md"):
            dp = os.path.join(KIT, "docs", "14_frentes", doc)
            if os.path.isfile(dp):
                extra += re.findall(r"\b\w+Error\b[^`\n|]*", open(dp, encoding="utf-8", errors="replace").read())
        try:
            say("errores en llano: %d textos sin 'Traceback' (%d de los docs)" % (errores.prueba(extra), len(extra)))
        except AssertionError as ex:
            ok = False
            say("ERRORES FAIL: %s" % (ex,))
        if "Mod Kit" not in errores.informe(app):
            ok = False
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
        imp_page.make_official("vis_b2_fat_gotenks", "vision")        # tarjeta 5: personajes de la vision
        stp = app.pages["stories"]
        stp.make_reto()                                                # modos de reto: crear (1/2)
        stp.pre_week()                                                 # retos predefinidos: torre de la semana
        stp.make_nativo()                                              # modos de reto: poner en el HUB (torre 10)
        stp.torneo.set("Goku, vegeta, 44")
        stp.make_torneo()                                              # torneo mundial con rivales elegidos
        if stp.pre:
            stp.pre_cb.current(len(stp.pre) - 1)
            stp.pre_install()                                          # retos predefinidos: poner (un kumite)
            stp.pre_remove()
        stp.build_campaign(os.path.join(HERE, "campanas", "historia_freezer.toml"))   # montar en env.mods
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
            vistos = set()          # KIT-10: la vista de un mod (arbol, cada seccion, validacion) de cada tipo
            for m in mp.mods:
                if m["tipo"] in vistos:
                    continue
                vistos.add(m["tipo"])
                mp.focus_item(mod=m["name"], abrir=True)
                for iid, _p, _t in mp._secciones(m):
                    mp.seccion = iid
                    mp._seccion(m)
                root.update_idletasks()
            say("OK: Mis mods avanzado, vista de un mod por tipo: %s" % ", ".join(sorted(vistos)))
            mp.cerrar()
            mp.varios([mp.mods[0]["name"]]).destroy()
            mp.tree.selection_set(mp.mods[0]["name"])
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
        want = {"vision": lambda c: c[2:6] == ["oficial", "--recetas", "ports_vision.toml", "vis_b2_fat_gotenks"]
                and c[6:8] == ["--mods", env.mods],
                "retos": lambda c: c[1].endswith("modos_reto.py") and c[2:5] == ["torre", "--pisos", "10"],
                "semana": lambda c: not env.game or (c[1].endswith("retos_predefinidos.py") and
                                                     c[2:5] == ["semana", "--tipo", "torre"] and
                                                     c[5:7] == ["--juego", env.game]),
                "predefinido": lambda c: not env.game or (c[1].endswith("retos_predefinidos.py") and
                                                          c[2] == "instalar" and c[3].startswith("nativo_kumite")),
                "quitar": lambda c: not env.game or (c[1].endswith("retos_predefinidos.py") and
                                                     c[2:4] == ["quitar", "kumite"]),
                "hub": lambda c: not env.game or (c[1].endswith("modos_reto.py") and c[2:5] == ["nativo", "torre",
                                                  "--rivales"] and len(c[5].split(",")) == 10 and
                                                  c[6:8] == ["--juego", env.game]),
                "torneo": lambda c: not env.game or (c[1].endswith("modos_reto.py") and
                                                     c[2:4] == ["torneo", "--participantes"] and c[4] == "0,7,44"),
                "campana": lambda c: c[1].endswith("campana.py") and c[2:4] == ["--mods", env.mods] and
                c[-2] == "montar"}
        for name, test in want.items():
            if not any(test(c) for _, c in acts):
                ok = False
                say("FAIL: %s command not recorded" % name)
        if parse_fighters("Goku, vegeta, Frieza, 63, xx, ") != ([0, 7, 27, 63], ["xx"]):
            ok = False
            say("FAIL: parse_fighters %s" % (parse_fighters("Goku, vegeta, Frieza, 63, xx, "),))
        if len(stp.pre) < 12:
            ok = False
            say("FAIL: ready-made challenges: %d in the list (expected 12+)" % len(stp.pre))
        else:
            say("ready-made challenges: %d (%s ... %s)" % (len(stp.pre), stp.pre[0]["label"], stp.pre[-1]["label"]))
        # choques de campañas: misma casilla, solo las activas
        import tempfile  # noqa: PLC0415
        tmp = tempfile.mkdtemp(prefix="modkit_selftest_")
        for nm, txt, off in (("a", "[rueda]\npersonajes = 27\n", False), ("b", "[rueda]\npersonajes = 27\n", True),
                             ("c", "[rueda]\npersonajes = 7\n", False), ("d", "", False)):
            os.makedirs(os.path.join(tmp, nm))
            if txt:
                with open(os.path.join(tmp, nm, "campana.toml"), "w", encoding="utf-8") as fh:
                    fh.write(txt)
            if off:
                set_mod_enabled(tmp, nm, False)
        rivals = campaign_rivals(tmp, "x", {27})
        shutil.rmtree(tmp, ignore_errors=True)      # carpeta temporal propia del autotest
        if rivals != ["a"] or wheel_of({"rueda": {"personajes": [27, 7]}}) != {27, 7}:
            ok = False
            say("FAIL: campaign_rivals %s" % rivals)
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
            # Heroes: la lista trae TODOS los modelos (antes 24) y Xeno Janemba con su nombre
            if any(s["id"] == "sdbh" and s["state"] == "listo" for s in srcs):
                rc, text = run_sync(cmd_importer(env, ["lista", "sdbh"]), env.cwd(), timeout=120)
                lst = parse_entries(text)
                if rc != 0 or len(lst) < 300 or not any(e["key"] == "JNO" and e["name"] == "Xeno Janemba" for e in lst):
                    ok = False
                    say("FAIL: importar.py lista sdbh rc=%s, %d personajes" % (rc, len(lst)))
                else:
                    say("importar.py lista sdbh -> %d characters (Xeno Janemba included)" % len(lst))
        else:
            ok = False
            say("FAIL: importar.py / roster_build.py not found next to the GUI")
        ok = probar_deshacer(say) and ok
        ok = prueba.rapido(app, sys.modules[__name__], say) and ok
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
    ap.add_argument("--selftest-asistentes", nargs=2, metavar=("US", "CARPETA"),
                    help="prueba completa de los 4 asistentes sin ventanas, con US = COPIA de los datos del juego")
    a = ap.parse_args(argv)
    if a.selftest_asistentes:
        us, base = a.selftest_asistentes
        okk = prueba.real(sys.modules[__name__], os.path.abspath(us), os.path.abspath(base),
                          lambda m: print("[asistentes] " + m, flush=True))
        print("[asistentes] RESULT: %s" % ("OK" if okk else "FAILED"))
        return 0 if okk else 1
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
            messagebox.showerror("DBZ3 HD Mod Kit", (
                "El Mod Kit no ha podido abrirse. Prueba a descomprimir otra vez el Kit completo junto a dbz3.exe. "
                "Si sigue fallando, comparte este archivo en el Discord de la comunidad:\n"
                "The Mod Kit could not start. Try unzipping the full kit again next to dbz3.exe. If it keeps "
                "failing, share this file on the community Discord:\n\n%s\n\n%s") % (log, err[-1200:]))
        except Exception:  # noqa: BLE001
            print(err, file=sys.stderr)
        sys.exit(1)
