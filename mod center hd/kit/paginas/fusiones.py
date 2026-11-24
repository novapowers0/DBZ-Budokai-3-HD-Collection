# -*- coding: utf-8 -*-
"""«Fusiones» (KIT-07b, issue #73; maqueta studies 5/ENTREGAS/KIT_VISUAL/05b_fusiones.png): a la izquierda cuanto dura
una fusion (para todas y una a una; en modo Avanzado, segundos exactos); a la derecha los fusionados como personajes
sueltos: Gogeta ya se puede activar (KIT-17, mod fusion_gogeta_suelto); el resto, «sin probar en el juego». La logica esta en kit/fusiones.py; guardar
pasa por planes.ejecutar y deja copia del dbz3_user.toml para «Deshacer». Clave de la tarjeta: obra_fusiones."""
from __future__ import annotations

from kit import fusiones as FU
from kit import nucleo, planes
from kit.nucleo import C, F, Page, T, Tooltip, label, tk, ttk

ETIQUETAS = [(k, es, en) for k, es, en, _v in FU.NIVELES]


def _boton(parent, texto, tip, command, style=None):
    b = ttk.Button(parent, text=texto, command=command, **({"style": style} if style else {}))
    Tooltip(b, tip)
    return b


class FusionesPage(Page):
    key = "obra_fusiones"

    def build(self):
        self.ingles = nucleo.LANG == "en"
        general, sg, por, seg = FU.desde_texto(FU.leer_ajuste(self.env.game))
        self.v_general = tk.IntVar(master=self, value=[k for k, *_ in ETIQUETAS].index(general))
        self.v_gogeta = tk.BooleanVar(master=self, value=FU.activo(self.env.mods))
        self.v_sg = tk.IntVar(master=self, value=sg)
        self.v_por = {c: tk.StringVar(master=self, value=self._texto_nivel(por.get(c, FU.COMO_TODAS)))
                      for c, *_ in FU.FUSIONES}
        self.v_seg = {c: tk.IntVar(master=self, value=seg.get(c, 0)) for c, *_ in FU.FUSIONES}
        self.header(T("Fusiones", "Fusions"),
                    T("Dos cosas sencillas: cuánto dura una fusión y tener a los fusionados como personajes sueltos "
                      "en la rueda.", "Two simple things: how long a fusion lasts and having fused fighters as "
                      "standalone characters on the wheel."))
        pie = tk.Frame(self, bg=C["bg"])
        pie.pack(side="bottom", fill="x", padx=22, pady=(4, 12))
        self.estado = label(pie, "", wrap=640)
        self.estado.pack(side="left", fill="x", expand=True)
        self.b_guardar = _boton(pie, T("Guardar", "Save") + "  →",
                                T("Guarda la duración en los ajustes del juego (con copia para deshacer).",
                                  "Saves the duration in the game settings (with a copy to undo)."),
                                self.guardar, "Accent.TButton")
        self.b_guardar.pack(side="right")
        _boton(pie, T("Deshacer cambios", "Undo changes"), T("Deja los ajustes como estaban antes de guardar.",
                                                             "Puts the settings back as they were before saving."),
               self.deshacer).pack(side="right", padx=8)
        cuerpo = tk.Frame(self, bg=C["bg"])
        cuerpo.pack(fill="both", expand=True, padx=22)
        cuerpo.columnconfigure(0, weight=1, uniform="fu")
        cuerpo.columnconfigure(1, weight=1, uniform="fu")
        cuerpo.rowconfigure(0, weight=1)
        izq = nucleo.Card(cuerpo, T("¿Cuánto dura la fusión?", "How long does the fusion last?"))
        izq.grid(row=0, column=0, sticky="nsew", padx=(0, 7))
        der = nucleo.Card(cuerpo, T("Fusiones como personajes sueltos", "Fusions as standalone characters"))
        der.grid(row=0, column=1, sticky="nsew", padx=(7, 0))
        self._duracion(izq.body)
        self._sueltos(der.body)
        self._pintar_niveles()

    # ------------------------------------------------------------------ duracion
    def _texto_nivel(self, k):
        if k == FU.COMO_TODAS:
            return T("Como todas", "Like all")
        return next(T(es, en) for kk, es, en in ETIQUETAS if kk == k)

    def _nivel_de_texto(self, t):
        if t == T("Como todas", "Like all"):
            return FU.COMO_TODAS
        return next((k for k, es, en in ETIQUETAS if T(es, en) == t), FU.COMO_TODAS)

    def _duracion(self, b):
        label(b, T("Se aplica a todas. Abajo puedes cambiar una por una.", "It applies to all. Below you can change "
                   "them one by one."), fg=C["dim"], bg=C["card"]).pack(fill="x", pady=(0, 10))
        esc = ttk.Scale(b, from_=0, to=3, orient="horizontal", variable=self.v_general,
                        command=lambda _v: self._pintar_niveles(True))
        esc.pack(fill="x", padx=10)
        Tooltip(esc, T("Corta = la mitad; Larga = el doble; Sin límite = no se deshace nunca.",
                       "Short = half; Long = double; No limit = it never ends."))
        fila = tk.Frame(b, bg=C["card"])
        fila.pack(fill="x", pady=(4, 12))
        self.lbl_niveles = []
        for i, (k, es, en) in enumerate(ETIQUETAS):
            fila.columnconfigure(i, weight=1)
            lb = tk.Label(fila, text=T(es, en) + (" ∞" if k == "sin" else ""), bg=C["card"], font=F["small"],
                          cursor="hand2", anchor=("w", "center", "center", "e")[i])
            lb.grid(row=0, column=i, sticky="ew")
            lb.bind("<Button-1>", lambda _e, n=i: (self.v_general.set(n), self._pintar_niveles()))
            self.lbl_niveles.append(lb)
        if self.adv:
            sg = tk.Frame(b, bg=C["card"])
            sg.pack(fill="x", pady=(0, 10))
            label(sg, T("Segundos exactos para todas (0 = usar la barra)", "Exact seconds for all (0 = use the bar)"),
                  bg=C["card"]).pack(side="left")
            ttk.Spinbox(sg, from_=0, to=3600, width=6, textvariable=self.v_sg).pack(side="left", padx=8)
        valores = [T("Como todas", "Like all")] + [T(es, en) for _k, es, en in ETIQUETAS]
        for clave, es, en, fichas, (a, c) in FU.FUSIONES:
            f = tk.Frame(b, bg=C["card_hi"], highlightthickness=1, highlightbackground=C["line"])
            f.pack(fill="x", pady=4)
            tk.Label(f, text="%s + %s" % (a, c), bg=C["card_hi"], fg=C["dim"], font=F["small"],
                     width=22, anchor="w").pack(side="left", padx=(12, 6), pady=10)
            tk.Label(f, text=T(es, en) + (("  (ID %s)" % "/".join(map(str, fichas))) if self.adv else ""),
                     bg=C["card_hi"], fg=C["text"], font=F["bold"], anchor="w").pack(side="left", fill="x", expand=True)
            if self.adv:
                ttk.Spinbox(f, from_=0, to=3600, width=5, textvariable=self.v_seg[clave]).pack(side="right", padx=(4, 12))
                tk.Label(f, text=T("s", "s"), bg=C["card_hi"], fg=C["dim"]).pack(side="right")
            cb = ttk.Combobox(f, values=valores, state="readonly", width=17, textvariable=self.v_por[clave])
            cb.pack(side="right", padx=12 if not self.adv else 4)
        label(b, T("Las fusiones de mods (Gokule, Yamhan…) siguen a Vegetto o a Gogeta, cuya fila usan. En las "
                   "partidas online no cambia nada.", "Fusions from mods (Gokule, Yamhan…) follow Vegito or Gogeta, "
                   "whose row they use. Nothing changes in online matches."), fg=C["dim"], font=F["small"],
              bg=C["card"], wrap=560).pack(fill="x", pady=(10, 0))
        if not self.adv:
            label(b, T("En modo avanzado: el tiempo exacto en segundos, por fusión.",
                       "In Advanced mode: the exact time in seconds, per fusion."), fg=C["dim"], font=F["small"],
                  bg=C["card"]).pack(side="bottom", fill="x", pady=(8, 0))

    def _pintar_niveles(self, arrastre=False):
        i = int(round(float(self.v_general.get())))
        if arrastre:
            self.v_general.set(i)
        for k, lb in enumerate(self.lbl_niveles):
            lb.configure(fg=C["accent"] if k == i else C["dim"], font=F["bold_sm"] if k == i else F["small"])

    # ------------------------------------------------------------------ sueltos
    def _sueltos(self, b):
        label(b, T("Los eliges en la rueda como a cualquier luchador, sin fusionarte en el combate.",
                   "You pick them on the wheel like any fighter, without fusing in battle."), fg=C["dim"],
              bg=C["card"]).pack(fill="x", pady=(0, 10))
        for clave, nombre, ficha, forma, probado in FU.SUELTOS:
            f = tk.Frame(b, bg=C["card_hi"], highlightthickness=1, highlightbackground=C["line"])
            f.pack(fill="x", pady=4)
            tf = tk.Frame(f, bg=C["card_hi"])
            if probado:
                cb = ttk.Checkbutton(f, variable=self.v_gogeta)
                cb.pack(side="left", padx=(12, 6), pady=4)
                Tooltip(cb, T("Enciende para crear a %s como personaje suelto al guardar; apaga para quitarlo." % nombre,
                              "Turn on to create %s as a standalone character when saving; off to remove it." % nombre))
                tk.Label(tf, text=nombre + ("  (ID %d, form %d)" % (ficha, forma) if self.adv and self.ingles else
                                            "  (ficha %d, forma %d)" % (ficha, forma) if self.adv else ""),
                         bg=C["card_hi"], fg=C["text"], font=F["bold"], anchor="w").pack(fill="x")
                tk.Label(tf, text=T("en pruebas", "in testing"), bg=C["card_hi"], fg=C["dim"], font=F["small"],
                         anchor="w").pack(fill="x")
            else:
                tk.Label(f, text="◯", bg=C["card_hi"], fg=C["dim"], font=F["bold"]).pack(side="left", padx=(12, 10), pady=4)
                tk.Label(tf, text=nombre, bg=C["card_hi"], fg=C["dim"], font=F["bold"], anchor="w").pack(fill="x")
                tk.Label(tf, text=T(*FU.SIN_PROBAR), bg=C["card_hi"], fg=C["dim"], font=F["small"], anchor="w").pack(fill="x")
            tf.pack(side="left", fill="x", expand=True)
        label(b, FU.AVISO_PLAZA[1 if self.ingles else 0], fg=C["warn"], bg=C["card"], wrap=560).pack(fill="x", pady=(8, 0))
        caja = tk.Frame(b, bg=C["frame"], highlightthickness=1, highlightbackground=C["line"])
        caja.pack(fill="both", expand=True, pady=(10, 0))
        tk.Label(caja, text=T("Cómo funciona", "How it works"),
                 bg=C["card_hi"], fg=C["text"], font=F["bold_sm"], padx=8, pady=3).pack(anchor="w", padx=10, pady=8)
        label(caja, T("Gogeta sale en la rueda como un luchador más y empieza ya fusionado. Vegetto, Gotenks y Gokule "
                      "todavía no se han probado en el juego, por eso no se pueden activar.",
                      "Gogeta shows on the wheel as one more fighter and starts already fused. Vegito, Gotenks and "
                      "Gokule have not been tested in the game yet, so they cannot be turned on."),
              fg=C["dim"], bg=C["frame"], font=F["small"], wrap=480).pack(fill="x", padx=14, pady=(0, 4))
        label(caja, T("Las demás fusiones, en combate: equipa la cápsula de fusión, entra en modo hiper y pulsa P+K+G+E.",
                      "The other fusions, in battle: equip the fusion capsule, enter hyper mode and press P+K+G+E."),
              fg=C["dim"], bg=C["frame"], font=F["small"], wrap=480).pack(fill="x", padx=14, pady=(0, 10))

    # ------------------------------------------------------------------ guardar / deshacer
    def valor(self):
        general = ETIQUETAS[int(round(float(self.v_general.get())))][0]

        def entero(v):
            try:
                return max(0, int(v.get())) if self.adv else 0
            except (tk.TclError, ValueError):
                return 0
        return FU.componer(general, {c: self._nivel_de_texto(v.get()) for c, v in self.v_por.items()},
                           entero(self.v_sg), {c: entero(v) for c, v in self.v_seg.items()})

    def guardar(self):
        valor = self.valor()
        quiere, hay = bool(self.v_gogeta.get()), FU.activo(self.env.mods)
        if self.app.selftest:
            self.app.selftest_cmds.append(("fusiones: guardar", [FU.CVAR, valor] + (["gogeta"] if quiere else [])))
            return
        pasos = [(T("Duración de las fusiones", "Fusion duration"),
                  lambda: FU.guardar(self.env, valor, nucleo.anotar_indice))]
        nuevos, aviso = [], ""
        if quiere and not hay:
            if FU.plazas_libres(self.env.mods) == 0:     # sin plaza no se crea (nada que el juego pueda cerrar)
                aviso = FU.motivo_sin_plaza(self.ingles)
                self.v_gogeta.set(False)
            else:
                pasos.append((T("Crear a Gogeta suelto", "Create standalone Gogeta"),
                              lambda: FU.crear_gogeta(self.env, libres=-1)))
                nuevos = [FU.MOD_GOGETA]
        elif hay and not quiere:
            pasos.append((T("Quitar a Gogeta suelto", "Remove standalone Gogeta"),
                          lambda: FU.quitar_gogeta(self.env, nucleo.anotar_indice)))
        ok, texto, _lote = planes.ejecutar(nucleo, self.env, planes.Plan("fusiones", pasos, nuevos),
                                           lambda t: self.app.log_text(t + "\n"))
        if ok:
            extra = (T("✓ Gogeta suelto: se monta al pulsar «Montar y probar».",
                       "✓ Standalone Gogeta: it is built when you press Build and try.") + "\n") if nuevos else ""
            self.set_status(self.estado, "warn" if aviso else "ok", "%s%s%s %s" % (
                extra, (aviso + "\n") if aviso else "",
                "\n".join(FU.resumen(self.env.game, self.ingles)) or T("✓ fusiones: como el original",
                                                                     "✓ fusions: like the original"),
                T("· Se nota al jugar fuera de línea (hace falta la versión del juego que trae este ajuste).",
                  "· You will see it offline (it needs the game version that includes this setting).")))
        else:
            self.set_status(self.estado, "err", T("No se pudo guardar: ", "Could not save: ") + texto.strip()[-200:])

    def deshacer(self):
        try:
            n = len(nucleo.deshacer_ultimo(self.env))
        except OSError as ex:
            self.set_status(self.estado, "err", str(ex))
            return
        self.set_status(self.estado, "ok" if n else "warn", T("Deshecho: los ajustes vuelven a como estaban.",
                                                              "Undone: the settings are back as they were.")
                        if n else T("No hay nada que deshacer.", "There is nothing to undo."))
        if n:
            general, sg, por, seg = FU.desde_texto(FU.leer_ajuste(self.env.game))
            self.v_general.set([k for k, *_ in ETIQUETAS].index(general))
            self.v_sg.set(sg)
            self.v_gogeta.set(FU.activo(self.env.mods))
            for c, v in self.v_por.items():
                v.set(self._texto_nivel(por.get(c, FU.COMO_TODAS)))
                self.v_seg[c].set(seg.get(c, 0))
            self._pintar_niveles()
