# -*- coding: utf-8 -*-
"""Texturas: sacar a PNG, editar y volver a montar.

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403


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
        self.galeria = vistas.Galeria(c2.body, nucleo.G, columnas=7, lado=104)
        self.galeria.pack(fill="x", pady=(8, 0))
        c4 = Card(body, T("Ver cómo queda una imagen tuya", "See how an image of yours looks"),
                  T("Fondos de saga y escenarios: se recorta igual que al montar el mod.",
                    "Saga backgrounds and stages: cropped the same way as when the mod is built."))
        c4.pack(fill="x", pady=(0, 12))
        vistas.VistaImagen(c4.body, nucleo.G).pack(fill="x")
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
        if ok_dir and not self.app.selftest:
            self.galeria.cargar(d, vistas.tamanos_de_meta(d))
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
