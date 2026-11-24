# -*- coding: utf-8 -*-
"""Crear con mis modelos.

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403


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
