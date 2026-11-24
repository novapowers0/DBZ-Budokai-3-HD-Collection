# -*- coding: utf-8 -*-
"""Ventanas compartidas: editor de texto, ficha .toml, mod nuevo y visor de documentos.

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403


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
            "voces": T("iw:NOMBRE | sb2:COD | b3:ID | wav:voces (tus frases: voces/07.wav...) | donante | ninguna",
                       "iw:NAME | sb2:CODE | b3:ID | wav:voces (your lines: voces/07.wav...) | donante (= the "
                       "donor's) | ninguna (= none)"),
            "gritos": T("iw:NOMBRE | b1:N | sb2:COD | wav:gritos (tus .wav en gritos/) | donante | ninguno",
                        "iw:NAME | b1:N | sb2:CODE | wav:gritos (your .wav in gritos/) | donante (= the donor's) | "
                        "ninguno (= none)"),
            "formas": T("número de formas (vacío = las del donante; con menos modelos que formas, "
                        "las formas comparten el último modelo)",
                        "number of forms (empty = the donor's; with fewer models than forms, forms "
                        "share the last model)"),
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
