# -*- coding: utf-8 -*-
"""Experto: formulario de cualquier herramienta a partir de su argparse.

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403


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
        self.header(T("Modo experto: todas las herramientas", "Expert mode: all tools"),
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
        if t.name == "editor_historias.py":     # editor con ventana propia: un boton, sin formulario
            ttk.Button(c.body, text=T("Abrir el editor de historias", "Open the story editor"),
                       style="Accent.TButton", command=self.app.open_story_editor).pack(anchor="w", pady=(12, 0))
            hint(c.body, T("Se abre en su propia ventana, en el idioma del Mod Kit.",
                           "It opens in its own window, in the Mod Kit's language."), bg=C["card"]).pack(
                fill="x", pady=(6, 0))
            return
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
