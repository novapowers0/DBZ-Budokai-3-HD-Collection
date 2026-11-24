# -*- coding: utf-8 -*-
"""Importar personaje (B1, B2, IW, comunidad y ports).

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403


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
        # preset de presets_importar.toml (respeta el juego de origen / solo el modelo) + opciones
        label(g, T("Preset", "Preset"), bg=C["card"]).grid(row=4, column=0, sticky="w", padx=(0, 12), pady=4)
        # la lista sale de presets_importar.toml (importar.py lista FUENTE): un preset nuevo aparece solo
        self.preset_keys = ["original", "solo_modelos"]
        self.preset = tk.StringVar(value=preset_label("original"))
        self.preset_cb = ttk.Combobox(g, textvariable=self.preset, values=[preset_label(k) for k in self.preset_keys],
                                      state="readonly", width=40)
        self.preset_cb.grid(row=4, column=1, sticky="w", pady=4)
        self.preset_cb.bind("<<ComboboxSelected>>", self.preset_sync)
        self.v_ground = tk.BooleanVar(value=True)
        self.v_pose = tk.BooleanVar(value=True)
        self.v_combo_tech = tk.BooleanVar(value=True)
        # fisica del pelo y la ropa: la del preset, la del donante o ninguna (--opcion fisica)
        self.physics_opts = [(T("Física: la del preset", "Physics: as the preset says"), None),
                             (T("Física del pelo y la ropa del donante", "Donor hair and cloth physics"), "donante"),
                             (T("Sin física (pelo y ropa rígidos)", "No physics (rigid hair and cloth)"), "ninguna")]
        self.physics_cb = ttk.Combobox(g, values=[x[0] for x in self.physics_opts], state="readonly", width=40)
        self.physics_cb.current(0)
        self.physics_cb.grid(row=5, column=1, sticky="w", pady=2)
        checks = tk.Frame(g, bg=C["card"])
        checks.grid(row=6, column=1, sticky="w")
        ttk.Checkbutton(checks, text=T("Pies al suelo", "Feet on the ground"), variable=self.v_ground).pack(side="left")
        ttk.Checkbutton(checks, text=T("Pose del select propia", "Own select pose"), variable=self.v_pose).pack(
            side="left", padx=(12, 0))
        ttk.Checkbutton(checks, text=T("Técnica al final de los combos", "Technique at combo end"),
                        variable=self.v_combo_tech).pack(side="left", padx=(12, 0))
        # golpes y voces: lo que diga el preset o cambiarlo aqui (--opcion golpes / voces de importar.py)
        self.moves_opts = [(T("Golpes: los del preset", "Moves: as the preset says"), None),
                           (T("Golpes: los del juego original", "Moves: the original game's"), "fuente"),
                           (T("Golpes: los del personaje base", "Moves: the base character's"), "donante")]
        self.voice_opts = [(T("Voces: las del preset", "Voices: as the preset says"), None),
                           (T("Voces: las del juego original", "Voices: the original game's"), "fuente"),
                           (T("Voces: las del personaje base", "Voices: the base character's"), "donante"),
                           (T("Sin voces", "No voices"), "ninguna")]
        self.moves_cb = ttk.Combobox(g, values=[x[0] for x in self.moves_opts], state="readonly", width=40)
        self.moves_cb.current(0)
        self.moves_cb.grid(row=7, column=1, sticky="w", pady=2)
        self.voice_cb = ttk.Combobox(g, values=[x[0] for x in self.voice_opts], state="readonly", width=40)
        self.voice_cb.current(0)
        self.voice_cb.grid(row=8, column=1, sticky="w", pady=2)
        r = 9
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

        # 4. ports del proyecto con su receta estandar (ports_oficiales.toml): se recrean con TUS discos
        c4 = Card(body, "4. " + T("Ports oficiales", "Official ports"))
        c4.pack(fill="x", pady=(0, 12))
        hint(c4.body, T("Los personajes portados del proyecto con su receta estándar, que respeta el juego de "
                        "origen. Se crean desde tus juegos de ps2_games; si ya lo tienes, el anterior va a "
                        "mods_respaldo.",
                        "The project's ported characters with their standard recipe, faithful to the source "
                        "game. They are built from your games in ps2_games; an existing one is moved to "
                        "mods_respaldo."), bg=C["card"]).pack(anchor="w")
        self.off_list, self.off_status = {}, {}
        self.off_list["oficiales"] = tk.Frame(c4.body, bg=C["card"])
        self.off_list["oficiales"].pack(fill="x", pady=(6, 0))
        self.off_status["oficiales"] = self.status_label(c4.body)
        self.off_status["oficiales"].pack(fill="x", pady=(6, 0))

        # 5. personajes de la vision (ports_vision.toml): propuestas, mismas recetas y mismos botones
        c5 = Card(body, "5. " + T("Personajes de la visión (beta)", "Vision characters (beta)"))
        c5.pack(fill="x", pady=(0, 12))
        hint(c5.body, T("Propuestas de personajes nuevos: trajes de otras épocas, fusiones de Budokai 2 y Goku de "
                        "Heroes. No son oficiales y pueden tener fallos. Se crean desde tus juegos de ps2_games.",
                        "Proposed new characters: costumes from other eras, Budokai 2 fusions and Heroes Goku. "
                        "They are not official and may have bugs. Built from your games in ps2_games."),
             bg=C["card"]).pack(anchor="w")
        self.off_list["vision"] = tk.Frame(c5.body, bg=C["card"])
        self.off_list["vision"].pack(fill="x", pady=(6, 0))
        self.off_status["vision"] = self.status_label(c5.body)
        self.off_status["vision"].pack(fill="x", pady=(6, 0))

    # catalogo -> --recetas de importar.py oficial(es) (None = ports_oficiales.toml)
    CATALOGS = {"oficiales": None, "vision": "ports_vision.toml"}

    def _recetas(self, cat):
        return ["--recetas", self.CATALOGS[cat]] if self.CATALOGS[cat] else []

    def load_official(self, force=False, cat="oficiales"):
        if not self.env.tools_installed():
            return
        nxt = "vision" if cat == "oficiales" else None       # las dos listas, una detras de otra
        if not force and cat in self.app.cache:
            self.render_official(cat)
            if nxt:
                self.load_official(False, nxt)
            return

        def done(job):
            rows = [ln.split("\t") for ln in job.text.splitlines() if ln.startswith("oficial\t")]
            self.app.cache[cat] = [dict(mod=r[1], name=r[2], src=r[3], ready=r[4] == "listo") for r in rows if len(r) >= 6]
            if self.alive:
                self.render_official(cat)
                if job.rc != 0:
                    self.set_status(self.off_status[cat], "err", T("No se pudo leer la lista: ", "Could not read the "
                                                                   "list: ") + last_line(job.text))
                if nxt and not force:
                    self.load_official(False, nxt)
        self.run(cmd_importer(self.env, ["oficiales"] + self._recetas(cat)),
                 T("Ports oficiales", "Official ports") if cat == "oficiales" else
                 T("Personajes de la visión", "Vision characters"), done, quiet=True)

    def render_official(self, cat="oficiales"):
        box = self.off_list[cat]
        for w in box.winfo_children():
            w.destroy()
        rows = self.app.cache.get(cat) or []
        names = {"iw": "Infinite World", "sb1": "Shin Budokai", "sb2": "Another Road", "b1": "Budokai 1",
                 "b2": "Budokai 2", "sdbh": "Heroes"}
        for i, r in enumerate(rows):
            have = os.path.isdir(os.path.join(self.env.mods, r["mod"]))    # en TU carpeta de mods
            label(box, r["name"], bg=C["card"]).grid(row=i, column=0, sticky="w", padx=(0, 12), pady=2)
            hint(box, names.get(r["src"], r["src"]) + ("" if r["ready"] else T("  (falta el juego)",
                                                                               "  (game missing)")),
                 bg=C["card"]).grid(row=i, column=1, sticky="w", padx=(0, 12))
            b = ttk.Button(box, text=T("Rehacer", "Rebuild") if have else T("Crear", "Create"),
                           command=lambda m=r["mod"]: self.make_official(m, cat))
            b.grid(row=i, column=2, sticky="w", pady=2)
            if not r["ready"]:
                b.state(["disabled"])
        if rows:
            ttk.Button(box, text=T("Crear todos", "Create all"), command=lambda: self.make_official(None, cat)).grid(
                row=len(rows), column=2, sticky="w", pady=(8, 0))

    def make_official(self, mod, cat="oficiales"):
        args = ["oficial"] + self._recetas(cat) + ([mod] if mod else ["--todos"]) + ["--mods", self.env.mods] + \
            self.env.us_args()

        def done(job):
            self.app.cache.pop("mods", None)
            if self.alive:
                lvl, txt = self.app.outcome(job, last_line(job.text) + T(
                    ".  Se añade al juego al pulsar JUGAR en el launcher.",
                    ".  It is added to the game when you press PLAY in the launcher."))
                self.set_status(self.off_status[cat], lvl, txt)
                self.render_official(cat)
        self.run(cmd_importer(self.env, args), T("Creando %s", "Creating %s") % (
            T("ports oficiales", "official ports") if cat == "oficiales" else
            T("personajes de la visión", "vision characters")), done, status=self.off_status[cat])

    def on_show(self):
        self.load_official()
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
            pres = [ln.split("	")[1] for ln in job.text.splitlines() if ln.startswith("preset	")]
            if job.rc == 0:
                self.app.cache.setdefault("entries", {})[key] = lst
                self.app.cache.setdefault("presets", {})[key] = pres
            if self.alive and self.src == sid:
                self.fill_entries()
                if not lst:
                    help_text = (T("B3 comunidad: el disco PS2 no alimenta esta lista. Pon modelos .amb/.amo en modding resources o elige su carpeta.",
                                   "B3 community: this list does not read the PS2 disc. Place .amb/.amo models in modding resources or choose their folder.")
                                 if sid == "b3" else
                                 T("IW: comprueba la carpeta USR o ISO y que el Kit incluya iw_modelos.json. Mira el detalle del error abajo.",
                                   "IW: check the USR folder or ISO and that the Kit includes iw_modelos.json. See the error details below.")
                                 if sid == "iw" else T("No hay personajes en la fuente elegida; revisa la carpeta.",
                                                                      "No characters in the selected source; check the folder."))
                    detail = last_line(job.text) if job.rc != 0 else ""
                    self.set_status(self.c2_status, "err", help_text + ("  " + detail if detail else ""))
        self.run(cmd_importer(self.env, args), T("Leyendo %s", "Reading %s") % s["name"], done, quiet=True)

    PRESET_CHECKS = (("v_ground", "altura"), ("v_pose", "pose_select"), ("v_combo_tech", "combos_tecnica"))

    def preset_sync(self, _event=None):
        """Las casillas toman los valores del preset elegido (presets_importar.toml; sin clave = si)."""
        i = max(0, self.preset_cb.current())
        key = self.preset_keys[i] if i < len(self.preset_keys) else ""
        vals = read_toml(os.path.join(HERE, "presets_importar.toml")).get(self.src or "", {}).get(key, {})
        for var, k in self.PRESET_CHECKS:
            getattr(self, var).set(vals.get(k, True) is not False)

    def _entries_key(self):
        extra = self.v_folder.get().strip() if self.adv and self.src == "b3" else ""
        return "%s|%s" % (self.src, extra)

    def fill_entries(self):
        pres = (self.app.cache.get("presets") or {}).get(self._entries_key()) or ["original"]
        if pres != self.preset_keys:
            self.preset_keys = pres
            self.preset_cb.configure(values=[preset_label(k) for k in pres])
            self.preset_cb.current(0)
            self.preset_sync()
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
        i = max(0, self.preset_cb.current())
        extra = ["--preset", self.preset_keys[i]] if i < len(self.preset_keys) else []
        for var, key in self.PRESET_CHECKS:   # siempre explicito: la casilla manda sobre el preset
            extra += ["--opcion", "%s=%s" % (key, "true" if getattr(self, var).get() else "false")]
        for cb, opts, key in ((self.moves_cb, self.moves_opts, "golpes"), (self.voice_cb, self.voice_opts, "voces"),
                              (self.physics_cb, self.physics_opts, "fisica")):
            v = opts[max(0, cb.current())][1]
            if v:
                extra += ["--opcion", "%s=%s" % (key, v)]
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
            ok = job.rc == 0 and "listo: " in job.text   # el texto solo no basta: un fallo tardio tambien lo imprime
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
