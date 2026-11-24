# -*- coding: utf-8 -*-
"""Historias y modos de reto (torre, supervivencia, kumite).

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403


# modos_reto.py: modo, opcion del numero de combates, por defecto, nombre es/en
RETO_MODOS = [("torre", "--pisos", 10, "Torre", "Tower"), ("supervivencia", "--oleadas", 8, "Supervivencia", "Survival"),
              ("kumite", "--jefes", 5, "Kumite", "Kumite")]


def parse_fighters(text):
    """'Goku, vegeta, 44, xx' -> ([0, 7, 44], ['xx']): nombres del juego (es/en) o IDs 0-63."""
    names = {n.lower(): did for did, es, en in DONORS for n in (es, en)}
    ids, bad = [], []
    for tok in (t.strip() for t in text.split(",")):
        if tok.isdigit() and int(tok) < 64:
            ids.append(int(tok))
        elif tok.lower() in names:
            ids.append(names[tok.lower()])
        elif tok:
            bad.append(tok)
    return ids, bad


def _reto_mode_name(tipo):
    return {m[0]: T(m[3], m[4]) for m in RETO_MODOS}.get(tipo, tipo)


def predefined_challenges():
    """Catalogo de retos_predefinidos.py ([{key, tipo, label}]), en el orden torre, supervivencia, kumite.
    Sin la herramienta (Kit antiguo) o con un .toml roto, la lista sale vacia o sin ese reto: nunca falla."""
    try:
        import retos_predefinidos as rp  # noqa: PLC0415
        cat = rp.catalogo()
    except Exception:  # noqa: BLE001
        return []
    out = []
    for tipo in rp.TIPOS:
        # primero los retos con nombre (los del catalogo), el de serie al final de su modo
        for key, (_, d) in sorted(cat.items(), key=lambda kv: ("nombre" not in kv[1][1], kv[0])):
            if d.get("tipo") != tipo or rp.errores(key, d):
                continue
            name = d.get("name" if nucleo.LANG == "en" else "nombre") or d.get("nombre") or T("de serie", "stock")
            out.append({"key": key, "tipo": tipo, "label": "%s · %s  (%d %s)" % (
                _reto_mode_name(tipo), name, len(d.get("combate", [])), T("combates", "fights"))})
    return out


def installed_challenges(game):
    """Que reto tiene puesto cada modo en ese juego: {tipo: nombre} (el de serie si no hay ninguno)."""
    try:
        import retos_predefinidos as rp  # noqa: PLC0415
        cat = rp.catalogo()
    except Exception:  # noqa: BLE001
        return {}
    out = {}
    for tipo in rp.TIPOS:
        key = rp.puesto(game, tipo, cat)
        if key in cat:
            d = cat[key][1]
            out[tipo] = d.get("name" if nucleo.LANG == "en" else "nombre") or d.get("nombre") or T("de serie", "stock")
        else:
            out[tipo] = T("de serie", "stock") if key == "(reto por defecto)" else T("uno tuyo", "your own")
    return out


def wheel_of(cfg):
    """[rueda] personajes de una campaña: lista en el .toml de origen, numero suelto en el campana.toml del mod."""
    v = cfg.get("rueda", {}).get("personajes", [])
    return set(v if isinstance(v, list) else [v])


def campaign_rivals(mods_dir, mod, wheel):
    """Otros mods de campaña ACTIVOS con un personaje de la rueda en comun (se pisan la casilla)."""
    out = []
    try:
        names = sorted(os.listdir(mods_dir), key=str.lower)
    except OSError:
        return out
    for name in names:
        d = os.path.join(mods_dir, name)
        if name == mod or name.endswith(".disabled") or os.path.exists(os.path.join(d, ".disabled")):
            continue
        ct = os.path.join(d, "campana.toml")
        if os.path.isfile(ct) and wheel & wheel_of(read_toml(ct)):
            out.append(name)
    return out


class StoriesPage(Page):
    """Historias: el editor de historias (editor_historias.py) incrustado en el Mod Kit, sin ventana propia."""
    key = "stories"

    def build(self):
        self.header(T("Historias", "Stories"),
                    T("Crea tu campaña: capítulos con sus combates, rivales y retratos. Cuando esté lista, pulsa "
                      "«Montar mod».",
                      "Create your campaign: chapters with their fights, rivals and portraits. When it is ready, "
                      "press 'Build mod'."))
        ttk.Button(self.header_right, text=T("Ventana aparte", "Separate window"), style="Small.TButton",
                   command=lambda: self.app.open_story_editor(self.editor.path if self.editor else None)).pack(
            anchor="e")
        # barra fija: modos de reto (modos_reto.py) y el resultado de «Montar mod» (campana.py)
        bar = Card(self, pad=10)
        bar.pack(fill="x", padx=22, pady=(0, 8))
        row = tk.Frame(bar.body, bg=C["card"])
        row.pack(fill="x")
        label(row, T("Modos de reto", "Challenge modes"), font=F["bold"], bg=C["card"]).pack(side="left")
        self.reto_cb = ttk.Combobox(row, values=[T(es, en) for _, _, _, es, en in RETO_MODOS], state="readonly",
                                    width=14)
        self.reto_cb.current(0)
        self.reto_cb.pack(side="left", padx=(10, 6))
        self.reto_n = tk.StringVar(value=str(RETO_MODOS[0][2]))
        self.reto_cb.bind("<<ComboboxSelected>>", lambda _e: self.reto_n.set(str(RETO_MODOS[self.reto_cb.current()][2])))
        ttk.Spinbox(row, from_=2, to=10, textvariable=self.reto_n, width=4).pack(side="left")
        label(row, T("combates", "fights"), fg=C["dim"], bg=C["card"]).pack(side="left", padx=(4, 10))
        ttk.Button(row, text=T("Crear y montar", "Create and build"), style="Accent.TButton",
                   command=self.make_reto).pack(side="left")
        ttk.Button(row, text=T("Poner en el HUB", "Use in the HUB"), command=self.make_nativo).pack(
            side="left", padx=(6, 0))
        hint(bar.body, T("«Crear y montar»: torre, supervivencia o kumite con Freezer en Dragon Universe (un mod; las "
                         "campañas de Freezer comparten su casilla: deja activa solo una). «Poner en el HUB»: ese "
                         "modo del HUB del juego con ese número de combates.",
                         "'Create and build': tower, survival or kumite with Frieza in Dragon Universe (a mod; Frieza "
                         "campaigns share his slot: keep only one enabled). 'Use in the HUB': that game HUB mode "
                         "with that number of fights."), wrap=900,
             bg=C["card"]).pack(fill="x", pady=(4, 0))
        # Torneo Mundial con los rivales elegidos (modos_reto.py torneo --participantes ... --juego)
        trow = tk.Frame(bar.body, bg=C["card"])
        trow.pack(fill="x", pady=(8, 0))
        label(trow, T("Torneo Mundial", "World Tournament"), font=F["bold"], bg=C["card"]).pack(side="left")
        self.torneo = tk.StringVar()
        ttk.Entry(trow, textvariable=self.torneo, width=46).pack(side="left", padx=(10, 6))
        ttk.Button(trow, text=T("Poner rivales", "Set rivals"), command=self.make_torneo).pack(side="left")
        hint(bar.body, T("Nombres o números separados por comas (p. ej. Goku, Vegeta, Cell, 44). Los que falten salen "
                         "al azar, como siempre.",
                         "Names or numbers separated by commas (e.g. Goku, Vegeta, Cell, 44). Missing ones are "
                         "random, as usual."), wrap=900, bg=C["card"]).pack(fill="x", pady=(2, 0))
        # Retos predefinidos (retos_predefinidos.py): el reto que juega cada modo del HUB (Torre, Supervivencia,
        # Kumite). Solo copia un .toml a user_data/dbz3/modos/ del juego; lo que sustituye va a _respaldo.
        tk.Frame(bar.body, bg=C["line"], height=1).pack(fill="x", pady=(10, 8))
        prow = tk.Frame(bar.body, bg=C["card"])
        prow.pack(fill="x")
        label(prow, T("Retos predefinidos", "Ready-made challenges"), font=F["bold"], bg=C["card"]).pack(side="left")
        self.pre = predefined_challenges()
        self.pre_cb = ttk.Combobox(prow, values=[p["label"] for p in self.pre], state="readonly", width=44)
        if self.pre:
            self.pre_cb.current(0)
        self.pre_cb.pack(side="left", padx=(10, 6))
        ttk.Button(prow, text=T("Poner", "Use it"), command=self.pre_install).pack(side="left")
        ttk.Button(prow, text=T("Quitar", "Remove"), command=self.pre_remove).pack(side="left", padx=(6, 0))
        ttk.Button(prow, text="★  " + T("Torre de la semana", "Tower of the week"), style="Accent.TButton",
                   command=self.pre_week).pack(side="left", padx=(12, 0))
        self.pre_now = label(bar.body, "", fg=C["dim"], bg=C["card"], wrap=900)
        self.pre_now.pack(fill="x", pady=(4, 0))
        hint(bar.body, T("Para los modos del HUB del juego (Torre, Supervivencia, Kumite): el reto elegido se juega "
                         "la próxima vez que entres en ese modo. «Torre de la semana» cambia cada semana; «Quitar» "
                         "vuelve al reto de serie. No se borra nada: lo anterior se guarda aparte.",
                         "For the game's HUB modes (Tower, Survival, Kumite): the chosen challenge is played the "
                         "next time you enter that mode. 'Tower of the week' changes every week; 'Remove' goes "
                         "back to the stock challenge. Nothing is deleted: the previous one is kept aside."),
             wrap=900, bg=C["card"]).pack(fill="x", pady=(2, 0))
        self.pre_refresh()
        srow = tk.Frame(bar.body, bg=C["card"])
        srow.pack(fill="x")
        self.st = self.status_label(srow)
        self.st.pack(side="left", fill="x", expand=True)
        self.after_bar = tk.Frame(srow, bg=C["card"])
        ttk.Button(self.after_bar, text=T("Abrir carpeta del mod", "Open mod folder"), style="Small.TButton",
                   command=lambda: open_path(self.last_mod)).pack(side="left", padx=(6, 0))
        self.solo_btn = ttk.Button(self.after_bar, text=T("Dejar solo esta activa", "Keep only this one enabled"),
                                   style="Small.TButton", command=self.keep_only)
        self.last_mod, self.rivals = "", []
        sf = ScrollFrame(self)               # ventana baja: la página se desplaza (no se recorta)
        sf.pack(fill="both", expand=True, padx=22, pady=(0, 10))
        host = sf.inner
        sf.canvas.bind("<Configure>", lambda e: sf.canvas.itemconfigure(
            sf._win, height=max(e.height, host.winfo_reqheight())), add="+")
        self.editor = None
        try:
            import editor_historias as ed
            ed.LANG = nucleo.LANG
            self.editor = ed.App(host, None, embedded=True, runner=self._run,
                                 log_fn=lambda msg: self.app.log_text(msg + "\n"))
        except Exception:  # noqa: BLE001 - se avisa en la pantalla y el error va al registro
            self.app.log_text(traceback.format_exc(), "err")
            label(host, T("No se pudo abrir el editor aquí. Puedes usar «Ventana aparte» (arriba).",
                          "The editor could not open here. You can use 'Separate window' (above)."),
                  fg=C["warn"], wrap=760).pack(anchor="w", pady=20)

    def focus_item(self, path=None, **_):
        if path and self.editor is not None and self.editor.confirm_discard():
            self.editor.open_path(path)

    def _run(self, cmd, title):
        """Montar / Probar: se ejecutan con el registro y la barra de tareas del Mod Kit. Montar va a la
        carpeta de mods del Mod Kit (campana.py --mods), no a la de por defecto."""
        if list(cmd[1:3]) == ["campana.py", "montar"]:
            return self.build_campaign(cmd[3], title)
        return self.app.run([self.env.python] + list(cmd[1:]), title, cwd=HERE)

    def make_reto(self):
        key, flag = RETO_MODOS[max(0, self.reto_cb.current())][:2]
        try:
            n = int(self.reto_n.get())
        except ValueError:
            n = 0
        if not 2 <= n <= 10:
            self.set_status(self.st, "warn", T("Elige entre 2 y 10 combates.", "Pick between 2 and 10 fights."))
            return
        toml = os.path.join(HERE, "campanas", "retos", "reto_%s.toml" % key)

        def done(job):
            if job.rc == 0 and not job.cancelled:
                self.build_campaign(toml, T("Modo de reto (2/2): montar", "Challenge mode (2/2): build"))
            elif self.alive:
                self.set_status(self.st, "err", T("No se pudo crear el modo: ", "Could not create the mode: ") +
                                self.app.outcome(job)[1])
        self.after_bar.pack_forget()
        self.run(cmd_script(self.env, "modos_reto.py", [key, flag, n, "--salida", toml]),
                 T("Modo de reto (1/2): crear", "Challenge mode (1/2): create"), done, self.st, cwd=HERE)

    def _hub_run(self, tipo, args, title, ok_text):
        """modos_reto.py ... --juego: lo que habia en user_data/dbz3/modos/<tipo>.toml va antes a _respaldo."""
        if not self.env.game:
            self.set_status(self.st, "warn", T("No encuentro la carpeta del juego: elígela en «Comprobar instalación».",
                                               "Game folder not found: pick it in 'Check installation'."))
            return
        if not self.app.selftest:
            try:
                import retos_predefinidos as rp  # noqa: PLC0415
                rp.mover_a_respaldo(rp.destino(self.env.game, tipo))
            except (ImportError, OSError) as ex:
                self.set_status(self.st, "err", T("No se pudo guardar aparte el reto anterior: %s",
                                                  "The previous challenge could not be kept aside: %s") % ex)
                return
        self.after_bar.pack_forget()
        self.run(cmd_script(self.env, "modos_reto.py", list(args) + ["--juego", self.env.game]), title,
                 lambda _job: self.pre_refresh() if self.alive else None, self.st, ok_text=ok_text, cwd=HERE)

    def make_nativo(self):
        tipo, _, _, es, en = RETO_MODOS[max(0, self.reto_cb.current())]
        try:
            n = int(self.reto_n.get())
        except ValueError:
            n = 0
        if not 2 <= n <= 10:
            self.set_status(self.st, "warn", T("Elige entre 2 y 10 combates.", "Pick between 2 and 10 fights."))
            return
        import modos_reto as mr  # noqa: PLC0415
        base = mr.NATIVO[tipo]["rivales"]     # los rivales de serie del juego, repetidos si hacen falta mas
        rivales = ",".join(str(base[i % len(base)]) for i in range(n))
        self._hub_run(tipo, ["nativo", tipo, "--rivales", rivales], T("Reto del HUB", "HUB challenge"),
                      T("Listo: %s con %d combates. Entra en HUB > %s.", "Done: %s with %d fights. Go to HUB > %s.") %
                      (T(es, en), n, T(es, en)))

    def make_torneo(self):
        ids, bad = parse_fighters(self.torneo.get())
        if bad or not ids:
            self.set_status(self.st, "warn", T("No conozco: %s. Escribe nombres del juego o números del 0 al 63.",
                                               "Unknown: %s. Type game names or numbers from 0 to 63.") %
                            (", ".join(bad) or "-"))
            return
        self._hub_run("torneo", ["torneo", "--participantes", ",".join(map(str, ids))],
                      T("Torneo Mundial", "World Tournament"),
                      T("Listo: %d rivales para el Torneo Mundial.", "Done: %d rivals for the World Tournament.") %
                      len(ids))

    def build_campaign(self, toml, title=None):
        """campana.py montar en la carpeta de mods del Mod Kit; al terminar: activo, carpeta y choques."""
        cfg = read_toml(toml)
        mod = cfg.get("campana", {}).get("mod") or os.path.splitext(os.path.basename(toml))[0]
        wheel = wheel_of(cfg)

        def done(job):
            self.app.cache.pop("mods", None)
            if not self.alive:
                return
            if job.rc != 0 or job.cancelled:
                self.set_status(self.st, "err", T("No se pudo montar: ", "Could not build: ") + self.app.outcome(job)[1])
                return
            set_mod_enabled(self.env.mods, mod, True)        # montar = instalar: queda activo
            self.last_mod = os.path.join(self.env.mods, mod)
            self.rivals = campaign_rivals(self.env.mods, mod, wheel)
            who = ", ".join(donor_name(i) for i in sorted(wheel)) or T("su personaje", "its character")
            msg = T("Montada en mods\\%s. En el juego: JUGAR > Dragon Universe > %s.",
                    "Built into mods\\%s. In game: PLAY > Dragon Universe > %s.") % (mod, who)
            if self.rivals:
                msg += "  " + T("Ojo: también están activas %s (misma casilla).",
                                "Careful: %s are also enabled (same slot).") % ", ".join(self.rivals)
                self.solo_btn.pack(side="left", padx=(6, 0))
            else:
                self.solo_btn.pack_forget()
            self.set_status(self.st, "warn" if self.rivals else "ok", msg)
            self.after_bar.pack(side="right")
        self.after_bar.pack_forget()
        return self.run(cmd_script(self.env, "campana.py", ["--mods", self.env.mods] + self.env.us_args() +
                                   ["montar", toml]), title or T("Montar campaña", "Build campaign"), done,
                        self.st, cwd=HERE)

    def keep_only(self):
        for m in self.rivals:
            set_mod_enabled(self.env.mods, m, False)       # reversible: «Mis mods» los vuelve a activar
        self.app.cache.pop("mods", None)
        self.set_status(self.st, "ok", T("Desactivadas: %s. Puedes volver a activarlas en «Mis mods».",
                                         "Disabled: %s. You can enable them again in 'My mods'.") % ", ".join(self.rivals))
        self.rivals = []
        self.solo_btn.pack_forget()

    # ------------------------------------------------------------ retos predefinidos (HUB)
    def pre_refresh(self):
        if not self.env.game:
            text = T("No encuentro la carpeta del juego: elígela en «Comprobar instalación».",
                     "Game folder not found: pick it in 'Check installation'.")
        elif not self.pre:
            text = T("No hay retos predefinidos en este Kit (falta retos_predefinidos.py o campanas/retos).",
                     "No ready-made challenges in this kit (retos_predefinidos.py or campanas/retos is missing).")
        else:
            now = installed_challenges(self.env.game)
            text = T("Ahora en el juego:", "Now in the game:") + "  " + "   ·   ".join(
                "%s: %s" % (_reto_mode_name(t), n) for t, n in now.items())
        try:
            self.pre_now.configure(text=text)
        except tk.TclError:
            pass

    def _pre_run(self, args, title, ok_text, done=None):
        if not self.env.game:
            self.set_status(self.st, "warn", T("No encuentro la carpeta del juego: elígela en «Comprobar instalación».",
                                               "Game folder not found: pick it in 'Check installation'."))
            return

        def _done(job):
            if done is not None:
                done(job)
            if self.alive:
                self.pre_refresh()
        self.after_bar.pack_forget()
        self.run(cmd_script(self.env, "retos_predefinidos.py", list(args) + ["--juego", self.env.game]), title,
                 _done, self.st, ok_text=ok_text, cwd=HERE)

    def pre_selected(self):
        i = self.pre_cb.current()
        return self.pre[i] if 0 <= i < len(self.pre) else None

    def pre_install(self):
        p = self.pre_selected()
        if p is None:
            return
        self._pre_run(["instalar", p["key"]], T("Reto predefinido: poner", "Ready-made challenge: use"),
                      T("Listo: %s. Entra en HUB > %s para jugarlo.", "Done: %s. Go to HUB > %s to play it.") % (
                          p["label"], _reto_mode_name(p["tipo"])))

    def pre_remove(self):
        p = self.pre_selected()
        if p is None:
            return
        mode = _reto_mode_name(p["tipo"])
        self._pre_run(["quitar", p["tipo"]], T("Reto predefinido: quitar", "Ready-made challenge: remove"),
                      T("%s vuelve a su reto de serie (el anterior queda guardado aparte).",
                        "%s goes back to its stock challenge (the previous one is kept aside).") % mode)

    def pre_week(self):
        def done(job):
            # el nombre de la torre puesta, en el idioma del Kit, en la linea de estado
            if job.rc == 0 and not job.cancelled and self.alive:
                name = installed_challenges(self.env.game).get("torre", "")
                self.set_status(self.st, "ok", T("Torre de la semana puesta: %s. Entra en HUB > Torre.",
                                                 "Tower of the week set: %s. Go to HUB > Tower.") % name)
        self._pre_run(["semana", "--tipo", "torre"], T("Torre de la semana", "Tower of the week"),
                      T("Torre de la semana puesta. Entra en HUB > Torre.", "Tower of the week set. Go to HUB > Tower."),
                      done)
