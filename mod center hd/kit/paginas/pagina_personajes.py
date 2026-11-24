# -*- coding: utf-8 -*-
"""Personajes nuevos: nombre, plaza, imagenes, capsulas, formas y aura.

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403
from kit.paginas.dialogos import TomlFormDialog  # noqa: E402


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
        # sin icono: buscar ese simbolo privado en las fuentes del sistema costaba 1-3 s al abrir la pantalla
        sb = ttk.Button(top, text=T("Cámaras (Studio)", "Cameras (Studio)"),
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
            ficha = vistas.FichaCapsula(g, nucleo.G)
            ficha.pack(fill="x", pady=(0, 6))
            ct.bind("<<TreeviewSelect>>", lambda _e: ct.selection() and ficha.mostrar(
                caps[int(ct.selection()[0])], ct.item(ct.selection()[0], "values")[0]))
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
            "Vacío = las del donante. Si hay menos modelos que formas, las formas comparten el último "
            "modelo; con un solo modelo y sin «modelo por forma», 1 forma.",
            "Empty = the donor's. With fewer models than forms, forms share the last model; with a "
            "single model and no per-form model, 1 form."))
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
                 T("vacío = las de su personaje base (%s); con menos modelos que formas, comparten el "
                   "último", "empty = its base character's (%s); with fewer models than forms, they "
                   "share the last") % donor_name(d.get("donante", 21))),
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
        aura_vista = vistas.MuestraAura(g, nucleo.G)
        aura_vista.grid(row=50, column=0, columnspan=3, sticky="w", pady=(8, 0))
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
                aura_vista.pintar(swatch_hex(chosen.get("aura_color")), swatch_hex(chosen.get("ki_color")))
            cb.bind("<<ComboboxSelected>>", picked)

        aura_vista.pintar(swatch_hex(chosen.get("aura_color")), swatch_hex(chosen.get("ki_color")))
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

        # aura de cada transformacion (aura_tipos_forma, roster_build.AURA_TIPOS): que aura enciende cada forma
        kinds = [("", T("La de su personaje base", "Its base character's")), ("ninguna", T("Sin aura", "No aura")),
                 ("normal", T("Normal (solo al cargar ki)", "Normal (only when charging ki)")),
                 ("kaioken", T("Kaioken (roja)", "Kaioken (red)")),
                 ("ssj", T("Super Saiyan (siempre encendida)", "Super Saiyan (always on)")),
                 ("ssj2", T("Super Saiyan 2 (con rayos)", "Super Saiyan 2 (with sparks)")),
                 ("ssj3", T("Super Saiyan 3", "Super Saiyan 3")), ("ssj4", T("Super Saiyan 4", "Super Saiyan 4"))]
        # mismas formas que construye roster_build: sin clave, las del donante (con 1 modelo y sin
        # modelo_forma, 1); con clave, recortada a las del donante
        try:
            db = json.load(open(os.path.join(HERE, "roster_db.json"),
                                encoding="utf-8"))
            donor_forms = next((x["forms"] for x in db["ids"] if x["id"] == d.get("donante", 21)), 1)
        except (OSError, ValueError, KeyError):
            donor_forms = 1
        nforms = d.get("formas")
        if d.get("formas_de"):
            nforms = max(1, len(d["formas_de"]) // 2)
        elif d.get("formas_donante"):
            nforms = max(1, len(d["formas_donante"]))
        elif isinstance(nforms, int):
            nforms = min(max(1, nforms), donor_forms)
        else:
            nforms = donor_forms
            if (d.get("modelos_por_traje") or 1) <= 1 and len(d.get("modelo_forma") or []) < 2:
                nforms = 1
        cur_t = list(d.get("aura_tipos_forma") or [])
        fbox = tk.Frame(g, bg=C["card"])
        fbox.grid(row=3, column=0, columnspan=3, sticky="w", pady=(6, 0))
        label(fbox, T("Aura de cada transformación", "Aura of each transformation"), bg=C["card"],
              font=F["bold"]).grid(row=0, column=0, columnspan=2, sticky="w")
        form_cbs = []
        for f in range(max(1, min(8, nforms))):
            label(fbox, T("Forma %d", "Form %d") % (f + 1) if f else T("Base", "Base"), bg=C["card"],
                  width=16).grid(row=f + 1, column=0, sticky="w", pady=1)
            v0 = cur_t[f] if f < len(cur_t) else ""
            cb_f = ttk.Combobox(fbox, values=[k[1] for k in kinds], state="readonly", width=34)
            cb_f.current(next((i for i, k in enumerate(kinds) if k[0] == v0), 0))
            cb_f.grid(row=f + 1, column=1, sticky="w")
            form_cbs.append(cb_f)

        def forms_picked(_e=None):
            sel = [kinds[cb_f.current()][0] for cb_f in form_cbs]
            chosen["aura_tipos_forma"] = None if not any(sel) else [x or "base" for x in sel]
        for cb_f in form_cbs:
            cb_f.bind("<<ComboboxSelected>>", forms_picked)
        hint(fbox, T("p. ej. Super Saiyan 2 con rayos en la forma 2; «la de su personaje base» deja la que hereda",
                     "e.g. Super Saiyan 2 with sparks on form 2; \"its base character's\" keeps the inherited one"),
             bg=C["card"], wrap=300).grid(row=1, column=2, rowspan=4, sticky="nw", padx=8)

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
            row=4, column=0, sticky="w", pady=(8, 0))
        st.grid(row=5, column=0, columnspan=3, sticky="ew")

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
