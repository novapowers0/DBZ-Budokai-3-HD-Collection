# -*- coding: utf-8 -*-
"""Comprobar instalacion: Python, librerias, carpeta del juego y juegos de PS2.

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403
from kit.paginas.dialogos import DocViewer  # noqa: E402


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
        # Validador de modelos (opcional): comprobar_assets.py del frente 01 (studies 6). Debajo de
        # los juegos de PS2; no bloquea nada si falta.
        self.val = Card(body, T("Validador de modelos (opcional)", "Model validator (optional)"),
                        T("Revisa que los modelos de tus mods cumplen las reglas del motor antes de montarlos "
                          "(ventana de 44 B, bone en +16, rampa por material, mipmaps=0, pesos PS2 de 32 B).",
                          "Checks that your mods' models follow the engine rules before you build them "
                          "(44-byte window, bone at +16, per-material ramp, mipmaps=0, 32-byte PS2 weights)."))
        self.val.pack(fill="x", pady=(0, 12))
        vb = tk.Frame(self.val.body, bg=C["card"])
        vb.pack(fill="x")
        self.val_btn = ttk.Button(vb, text=T("Revisar los modelos de mis mods", "Check my mods' models"),
                                  command=lambda: run_asset_check(self.app))
        self.val_btn.pack(side="left")
        self.val_status = self.status_label(self.val.body)
        self.val_status.pack(fill="x", pady=(6, 0))

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
        self.render_validator()
        if force or "sources" not in self.app.cache:
            self.query_sources()

    def render_validator(self):
        p = asset_checker_path(self.env)
        try:
            if p:
                self.val_btn.configure(state="normal")
                self.set_status(self.val_status, "dim", T("Listo: %s", "Ready: %s") % p)
            else:
                self.val_btn.configure(state="disabled")
                self.set_status(self.val_status, "dim",
                                T("No encontrado (opcional): viene con el Kit de modding.",
                                  "Not found (optional): it ships with the modding kit."))
        except tk.TclError:
            pass

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
    """Boton «Instalar librerias»: pip en segundo plano con el Python del kit (sin terminal)."""
    if not app.ask(T("Se descargarán e instalarán numpy, Pillow y scipy desde internet (solo para tu usuario). "
                     "Tarda un par de minutos. ¿Continuar?",
                     "numpy, Pillow and scipy will be downloaded and installed from the internet (for your user "
                     "only). It takes a couple of minutes. Continue?")):
        return
    cmd = [app.env.python, "-m", "pip", "install", "--user", "--disable-pip-version-check",
           "numpy", "Pillow", "scipy"]
    app.run(cmd, T("Instalar librerías", "Install libraries"),
            done=lambda job: (app.info(T("Librerías instaladas. Pulsa «Comprobar de nuevo».",
                                         "Libraries installed. Press 'Check again'.")) if job.rc == 0 else None))


# --- Validador de modelos (opcional): usa awo_tools/comprobar_assets.py del frente 01 ------------
# Verifica un bin (#AMB/#AWO/#AZT HD o #AMO0 PS2) contra las reglas del motor (ventana de 44 B,
# bone en +16, rampa por material, mipmaps=0, listas de pesos PS2 de 32 B...). El fichero puede
# vivir junto al Kit (mod center hd/), en la raiz del Kit o en Kit/datos/. Si no esta, el Kit
# sigue funcionando: solo se avisa y se explica de donde sacarlo.
def asset_checker_path(env):
    for p in (env.script("comprobar_assets.py"),
              os.path.join(KIT, "comprobar_assets.py"),
              os.path.join(KIT, "datos", "comprobar_assets.py"),
              os.path.join(env.kit, "datos", "comprobar_assets.py")):
        if p and os.path.isfile(p):
            return p
    return ""


def run_asset_check(app):
    """Revisa los bins de la carpeta de mods del jugador con comprobar_assets.py."""
    env = app.env
    p = asset_checker_path(env)
    if not p:
        app.info(T("No encuentro el validador de modelos (comprobar_assets.py) junto al Kit. Si lo tienes "
                   "(viene con el Kit de modding), copialo en «mod center hd» y vuelve a intentarlo.",
                   "I cannot find the model validator (comprobar_assets.py) next to the Kit. If you have it "
                   "(it ships with the modding kit), copy it into 'mod center hd' and try again."))
        return
    if not os.path.isdir(env.mods):
        app.info(T("Todavia no hay carpeta de mods donde revisar modelos.",
                   "There is no mods folder to check models in yet."))
        return

    def done(job):
        app.cache.pop("asset_check", None)

    app.run(cmd_script(env, p, ["--sumario", env.mods, "--patron", "*.bin"]),
            T("Revisando modelos", "Checking models"), done)


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
                T("Faltan: %s. Pulsa «Instalar librerías» (una sola vez).",
                  "Missing: %s. Press 'Install libraries' (only once).") % ", ".join(missing),
                "action": (T("Instalar librerías", "Install libraries"), lambda: install_requirements(app))})
    out.append({"key": "game", "title": T("Carpeta del juego", "Game folder"),
                "level": "ok" if env.game else "err",
                "detail": env.game if env.game else T(
                    "No encuentro dbz3.exe. Copia el kit junto a dbz3.exe o elige la carpeta del juego.",
                    "dbz3.exe not found. Copy the kit next to dbz3.exe or choose the game folder."),
                "action": None if env.game else (T("Elegir carpeta…", "Choose folder…"), app.pick_game)})
    afs = env.afs
    bad = afs and os.path.isfile(afs) and swap_b3.cmn_problem(afs)
    out.append({"key": "data", "title": T("Datos del juego", "Game data"),
                "level": "err" if bad else "ok" if afs and os.path.isfile(afs) else "warn",
                "detail": str(swap_b3.no_cmn_error(afs, nucleo.LANG))[len("ERROR: "):] if bad else
                afs if afs and os.path.isfile(afs) else T(
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
                                               ("mod center hd\\tools\\xbcompress.exe", xdk_ok)) if not ok) +
                            T(". Descomprime otra vez el Kit de modding completo junto a dbz3.exe.",
                              ". Unzip the full modding kit again next to dbz3.exe."))})
    checker = asset_checker_path(env)
    out.append({"key": "validator", "title": T("Validador de modelos (opcional)",
                                               "Model validator (optional)"),
                "level": "ok" if checker else "info",
                "detail": (T("Listo: %s. Revisa tus bins antes de montarlos.", "Ready: %s. Checks your bins "
                            "before building them.") % checker) if checker else
                          T("No esta (opcional). Revisa que un modelo nuevo cumple las reglas del motor: ventana de "
                            "44 B, bone en +16, rampa por material, mipmaps=0 y listas de pesos PS2 de 32 B. Viene "
                            "con el Kit de modding (comprobar_assets.py).",
                            "Not present (optional). Verifies a new model follows the engine rules: 44-byte window, "
                            "bone at +16, per-material ramp, mipmaps=0 and 32-byte PS2 weight lists. Ships with the "
                            "modding kit (comprobar_assets.py)."),
                "action": (T("Revisar modelos", "Check models"), lambda: run_asset_check(app)) if checker else None})
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
