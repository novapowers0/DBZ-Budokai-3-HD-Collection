# -*- coding: utf-8 -*-
"""«Nuevo escenario» (KIT-07, maqueta studies 5/ENTREGAS/KIT_VISUAL/04_escenario.png): 1 elige el escenario por su
foto (sacada de la ISO del jugador), 2 nombre y ambiente, 3 guardar (con copia y «Deshacer»). La logica sin
interfaz esta en kit/escenarios.py; la conversion, en escenarios_convertir.py. Se registra en modkit_gui con la
clave de la tarjeta de la portada (obra_escenario)."""
from __future__ import annotations

import os
import queue
import subprocess
import threading
import tkinter.font as tkfont

from kit import escenarios as ES
from kit import nucleo, planes, vistas
from kit.nucleo import C, F, Page, T, Tooltip, label, make_clickable, tk, ttk

TILE = (262, 148)          # foto de cada casilla de la rejilla
GRANDE = (420, 244)        # vista previa
_FOTOS_EN_MARCHA = set()   # (juego) con su hilo de fotos vivo en esta sesion


def _boton(parent, texto, tip, command, style=None):
    b = ttk.Button(parent, text=texto, command=command, **({"style": style} if style else {}))
    Tooltip(b, tip)
    return b


def _foto(path, caja, apagada=False, girada=False):
    """PhotoImage recortado a la proporcion de `caja` (centrado) o None; `apagada`: gris y oscura; `girada`: 180 grados
    (foto_girada de escenarios_estado.toml)."""
    try:
        from PIL import Image, ImageTk
        with Image.open(path) as im:
            im = im.convert("RGB")
        if girada:
            im = im.rotate(180)
        w, h = im.size
        r = caja[0] / caja[1]
        if w / h > r:
            nw = int(h * r)
            im = im.crop(((w - nw) // 2, 0, (w + nw) // 2, h))
        else:
            nh = int(w / r)
            im = im.crop((0, (h - nh) // 2, w, (h + nh) // 2))
        im = im.resize(caja, Image.LANCZOS)
        if apagada:
            im = Image.blend(im.convert("L").convert("RGB"), Image.new("RGB", caja, (17, 19, 24)), 0.55)
        return ImageTk.PhotoImage(im)
    except Exception:  # noqa: BLE001 (sin PIL o png roto: casilla sin foto)
        return None


class EscenarioPage(Page):
    key = "obra_escenario"

    def build(self):
        self.paso, self.juego, self.grupo = 0, self.app.state.get("esc_juego", "b2"), None
        self.v_nombre = tk.StringVar(master=self, value="")
        self.tema, self.fotos, self.lote, self.cola = 16, {}, None, queue.Queue()
        self.ingles = nucleo.LANG == "en"
        self.header(T("Nuevo escenario", "New stage"),
                    T("Un escenario de tu Budokai 2 o Budokai 1 de PS2 como casilla nueva del selector. No sustituye "
                      "ninguno de los de serie.", "A stage from your PS2 Budokai 2 or Budokai 1 as a new box in the "
                      "stage selector. It does not replace any of the original ones."))
        chips = tk.Frame(self, bg=C["bg"])
        chips.pack(fill="x", padx=22, pady=(0, 8))
        self.chips = []
        for i, (es, en) in enumerate((("Elige el escenario", "Pick the stage"), ("Nombre y ambiente", "Name and mood"),
                                      ("Guardar", "Save"))):
            lb = tk.Label(chips, text="%d · %s" % (i + 1, T(es, en)), font=F["bold_sm"], padx=12, pady=4)
            lb.pack(side="left", padx=(0, 8))
            self.chips.append(lb)
        pie = tk.Frame(self, bg=C["bg"])
        pie.pack(side="bottom", fill="x", padx=22, pady=(4, 12))
        self.lbl_err = label(pie, "", fg=C["err"], wrap=900)
        self.lbl_err.pack(side="left", fill="x", expand=True)
        self.b_sig = _boton(pie, T("Siguiente", "Next") + "  →", T("Pasa al paso siguiente.", "Go to the next step."),
                            self.siguiente, "Accent.TButton")
        self.b_sig.pack(side="right")
        self.b_atras = _boton(pie, "←  " + T("Atrás", "Back"), T("Vuelve al paso anterior.", "Back to the previous step."),
                              self.atras)
        self.b_atras.pack(side="right", padx=8)
        self.cuerpo = tk.Frame(self, bg=C["bg"])
        self.cuerpo.pack(fill="both", expand=True, padx=22)
        self.ir(0)

    # ------------------------------------------------------------------ navegacion
    def ir(self, i):
        self.paso = i
        for k, lb in enumerate(self.chips):
            on = k == i
            lb.configure(bg=C["accent"] if on else C["frame"], fg=C["on_accent"] if on else C["dim"])
        self.lbl_err.configure(text="")
        for w in self.cuerpo.winfo_children():
            w.destroy()
        (self._paso1, self._paso2, self._paso3)[i]()
        self.b_atras.configure(state="normal" if i else "disabled")
        self.b_sig.configure(state="normal" if i < 2 else "disabled")

    def siguiente(self):
        err = self.error_paso()
        if err:
            self.lbl_err.configure(text=err)
            return
        self.ir(self.paso + 1)

    def atras(self):
        if self.paso:
            self.ir(self.paso - 1)

    def error_paso(self):
        if self.paso == 0:
            e = ES.estado(self.juego, self.grupo) if self.grupo is not None else None
            if e is None:
                return T("Elige un escenario pulsando su foto.", "Pick a stage by clicking its photo.")
            if e["estado"] == "no_funciona":
                return T("Ese escenario aún no funciona. Elige otro.", "That stage does not work yet. Pick another.")
            return None
        return ES.validar(self.v_nombre.get(), self.juego, self.grupo, self.tema, self.env.mods, ingles=self.ingles)

    # ------------------------------------------------------------------ paso 1
    def _paso1(self):
        izq = nucleo.Card(self.cuerpo)
        izq.pack(side="left", fill="both", expand=True)
        der = tk.Frame(self.cuerpo, bg=C["bg"], width=440)
        der.pack(side="right", fill="y", padx=(14, 0))
        der.pack_propagate(False)
        top = tk.Frame(izq.body, bg=C["card"])
        top.pack(fill="x", pady=(0, 8))
        for juego, es, en in (("b2", "Budokai 2", "Budokai 2"), ("b1", "Budokai 1", "Budokai 1")):
            n = len(ES.estados(juego))
            lb = tk.Label(top, text="%s (%d)" % (T(es, en), n), font=F["bold_sm"], padx=14, pady=6, cursor="hand2",
                          bg=C["accent"] if juego == self.juego else C["frame"],
                          fg=C["on_accent"] if juego == self.juego else C["dim"])
            lb.pack(side="left")
            lb.bind("<Button-1>", lambda _e, j=juego: self._cambiar_juego(j))
        label(top, "   " + T("Fotos sacadas de tu copia del juego", "Photos taken from your copy of the game"),
              fg=C["dim"], font=F["small"], bg=C["card"]).pack(side="left")
        iso = ES.iso_de(self.juego, self.env.ps2)
        if not iso:
            label(izq.body, ES.falta_iso(self.juego, self.env.ps2, self.ingles), fg=C["warn"], bg=C["card"],
                  wrap=760).pack(fill="x", pady=20)
        else:
            sf = nucleo.ScrollFrame(izq.body, bg=C["card"])
            sf.pack(fill="both", expand=True)
            grid = sf.inner
            for c in range(3):
                grid.columnconfigure(c, weight=1, uniform="esc")
            self.casillas = {}
            for i, e in enumerate(ES.estados(self.juego)):
                self._casilla(grid, i, e, iso)
            self._pedir_fotos(iso)
        label(izq.body, T("Los escenarios marcados «aún no funciona» cierran hoy el juego: salen apagados y no se "
                          "pueden elegir.", "Stages marked 'not working yet' close the game today: they are greyed "
                          "out and cannot be picked."), fg=C["dim"], font=F["small"], bg=C["card"],
              wrap=760).pack(fill="x", pady=(8, 0))
        self._panel(der, iso)

    def _cambiar_juego(self, j):
        if j != self.juego:
            self.juego, self.grupo = j, None
            self.app.state["esc_juego"] = j
            self.ir(0)

    def _etiqueta(self, e):
        return {"probado": (T("✓ probado", "✓ tested"), C["ok"]),
                "sin_probar": (T("sin probar", "untested"), C["warn"]),
                "no_funciona": (T("aún no funciona", "not working yet"), C["err"])}[e["estado"]]

    def _casilla(self, grid, i, e, iso):
        apagada = e["estado"] == "no_funciona"
        sel = e["grupo"] == self.grupo
        f = tk.Frame(grid, bg=C["card_hi"], highlightthickness=2,
                     highlightbackground=C["accent"] if sel else C["line"])
        f.grid(row=i // 3, column=i % 3, sticky="nsew", padx=5, pady=5)
        img = tk.Label(f, bg=C["frame"], width=TILE[0], height=TILE[1], text=T("sacando la foto…", "taking the photo…"),
                       fg=C["dim"], font=F["small"], compound="center")
        img.pack(fill="x")
        fila = tk.Frame(f, bg=C["card_hi"])
        fila.pack(fill="x", padx=8, pady=6)
        txt, col = self._etiqueta(e)
        tk.Label(fila, text=txt, bg=C["card_hi"], fg=col, font=F["small"]).pack(side="right")
        sitio = TILE[0] - 24 - tkfont.Font(font=F["small"]).measure(txt)     # nombre largo: en dos lineas, no cortado
        tk.Label(fila, text=e["es"] if not self.ingles else e["en"], bg=C["card_hi"], wraplength=max(80, sitio),
                 justify="left", fg=C["dim"] if apagada else C["text"], font=F["bold"], anchor="w").pack(side="left",
                                                                                                     fill="x")
        self.casillas[e["grupo"]] = (img, apagada)
        self._poner_foto(e["grupo"], iso)
        if not apagada:
            make_clickable(f, lambda g=e["grupo"]: self._elegir(g), C["card_hi"], C["frame"])

    def _poner_foto(self, grupo, iso):
        img, apagada = self.casillas[grupo]
        png = ES.foto_png(vistas.cache_dir(), iso, self.juego, grupo)
        if os.path.isfile(png):
            ph = _foto(png, TILE, apagada, bool((ES.estado(self.juego, grupo) or {}).get("foto_girada")))
            if ph is not None:
                img.configure(image=ph, text="", width=TILE[0], height=TILE[1])
                img.foto = ph
                return True
        elif os.path.isfile(png + ".fallo"):
            img.configure(text=T("sin foto", "no photo"))
            return True
        return False

    def _pedir_fotos(self, iso):
        """Fotos que falten: en un hilo (orden escenarios.py fotos) y se recogen cada segundo."""
        if self.app.selftest:
            return
        faltan = [g for g in self.casillas if not os.path.isfile(ES.foto_png(vistas.cache_dir(), iso, self.juego, g))
                  and not os.path.isfile(ES.foto_png(vistas.cache_dir(), iso, self.juego, g) + ".fallo")]
        if not faltan:
            return
        if self.juego not in _FOTOS_EN_MARCHA:
            _FOTOS_EN_MARCHA.add(self.juego)
            cmd = ES.cmd_fotos(self.env.python, self.env.us, self.env.ps2, self.juego, vistas.cache_dir())

            def hilo(j=self.juego):
                try:
                    subprocess.run(cmd, capture_output=True, timeout=3600,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                except (OSError, subprocess.SubprocessError):
                    pass
                _FOTOS_EN_MARCHA.discard(j)
            threading.Thread(target=hilo, daemon=True).start()
        juego = self.juego

        def recoger():
            if not self.alive or self.paso != 0 or self.juego != juego:
                return
            try:
                pend = [g for g in faltan if not self._poner_foto(g, iso)]
            except tk.TclError:
                return
            if self.grupo is not None and self.grupo in faltan and self.grupo not in pend:
                self.ir(0)
                return
            if pend and juego in _FOTOS_EN_MARCHA:
                self.after(1000, recoger)
        self.after(1000, recoger)

    def _elegir(self, g):
        e = ES.estado(self.juego, g)
        if self.grupo != g:
            self.v_nombre.set(ES.propuesto(e, self.ingles))
        self.grupo = g
        self.ir(0)

    def _panel(self, der, iso):
        """Columna derecha: foto grande y lo que se sabe del escenario elegido."""
        card = nucleo.Card(der)
        card.pack(fill="both", expand=True)
        e = ES.estado(self.juego, self.grupo) if self.grupo is not None else None
        if e is None:
            label(card.body, T("Pulsa la foto de un escenario para elegirlo.", "Click a stage photo to pick it."),
                  fg=C["dim"], bg=C["card"], wrap=380).pack(fill="x")
            return
        self._grande(card.body, iso, e)
        label(card.body, e["en"] if self.ingles else e["es"], font=F["h3"], bg=C["card"]).pack(fill="x", pady=(10, 2))
        txt, col = self._etiqueta(e)
        label(card.body, {"probado": T("Probado en el juego: selector, combate y presentación.",
                                       "Tested in the game: selector, fight and intro."),
                          "sin_probar": T("Se convierte bien, pero nadie lo ha probado aún en el juego. Si algo va "
                                          "mal, «Deshacer» lo quita.", "It converts fine, but nobody has tried it in "
                                          "the game yet. If something goes wrong, 'Undo' removes it.")}.get(
            e["estado"], txt), fg=col, bg=C["card"], wrap=380).pack(fill="x")
        self._nota_zona(card.body, e)

    def _grande(self, parent, iso, e):
        png = ES.foto_png(vistas.cache_dir(), iso, self.juego, e["grupo"]) if iso else ""
        ph = _foto(png, GRANDE, girada=bool(e.get("foto_girada"))) if png and os.path.isfile(png) else None
        lb = tk.Label(parent, bg=C["frame"], image=ph, text="" if ph else T("(sin foto todavía)", "(no photo yet)"),
                      fg=C["dim"], font=F["small"])
        if ph is None:
            lb.configure(width=50, height=12)
        lb.foto = ph
        lb.pack(fill="x")

    def _nota_zona(self, parent, e):
        if e.get("zona_b"):
            label(parent, T("Tiene una segunda zona: se añadirá sola, y en el combate se pasa a ella al romper el borde "
                            "del escenario. Para verla, activa en los ajustes del juego «zonas B de los escenarios "
                            "nuevos»%s." % self._cvar(),
                            "It has a second area: it is added automatically, and in a fight you move to it by breaking "
                            "the stage edge. To see it, turn on 'second areas of new stages' in the game settings"
                            "%s." % self._cvar()), fg=C["dim"], font=F["small"], bg=C["card"],
                  wrap=380).pack(fill="x", pady=(10, 0))

    def _cvar(self):
        return " (%s)" % ES.CVAR_ZONAS if self.adv else ""      # el nombre del ajuste, solo en modo Avanzado

    # ------------------------------------------------------------------ paso 2
    def _paso2(self):
        e = ES.estado(self.juego, self.grupo)
        iso = ES.iso_de(self.juego, self.env.ps2)
        izq = nucleo.Card(self.cuerpo)
        izq.pack(side="left", fill="both", expand=True)
        label(izq.body, (e["en"] if self.ingles else e["es"]) + T(" · así se verá", " · how it will look"),
              font=F["h3"], bg=C["card"]).pack(fill="x", pady=(0, 8))
        self._grande(izq.body, iso, e)
        self._nota_zona(izq.body, e)
        der = nucleo.Card(self.cuerpo)
        der.pack(side="right", fill="both", padx=(14, 0))
        b = der.body
        label(b, T("Nombre en el selector", "Name in the selector"), font=F["h3"], bg=C["card"]).pack(fill="x")
        fila = tk.Frame(b, bg=C["card"])
        fila.pack(fill="x", pady=(4, 12))
        ent = ttk.Entry(fila, textvariable=self.v_nombre, width=30)
        ent.pack(side="left", fill="x", expand=True)
        self.lbl_cont = tk.Label(fila, bg=C["card"], font=F["small"])
        self.lbl_cont.pack(side="left", padx=(10, 0))
        self.v_nombre.trace_add("write", lambda *_a: self._contador())
        self._contador()
        label(b, T("Ambiente", "Mood"), font=F["h3"], bg=C["card"]).pack(fill="x")
        label(b, T("Luz y cielo prestados de un escenario del juego. La neutra es la probada.",
                   "Light and sky borrowed from a game stage. The neutral one is the tested one."),
              fg=C["dim"], font=F["small"], bg=C["card"], wrap=400).pack(fill="x", pady=(0, 6))
        ambs = ES.ambientes()
        botones = tk.Frame(b, bg=C["card"])
        botones.pack(fill="x")
        self.amb_lbl = {}
        principales = [a for a in ambs if a.get("principal")]
        for i, a in enumerate(principales):
            lb = tk.Label(botones, text=a["en"] if self.ingles else a["es"], font=F["bold_sm"], padx=10, pady=8,
                          cursor="hand2")
            lb.grid(row=i // 3, column=i % 3, sticky="nsew", padx=(0, 6), pady=(0, 6))
            lb.bind("<Button-1>", lambda _e, t=a["tema"]: self._tema(t))
            self.amb_lbl[a["tema"]] = lb
        resto = [a for a in ambs if not a.get("principal")]
        mas = tk.Frame(b, bg=C["card"])
        mas.pack(fill="x", pady=(4, 12))
        label(mas, T("Más…", "More…"), fg=C["dim"], bg=C["card"]).pack(side="left", padx=(0, 8))
        self.v_mas = tk.StringVar(master=self, value="")
        self._mas_valores = {(a["en"] if self.ingles else a["es"]): a["tema"] for a in resto}
        cb = ttk.Combobox(mas, textvariable=self.v_mas, values=list(self._mas_valores), state="readonly", width=30)
        cb.pack(side="left", fill="x", expand=True)
        cb.bind("<<ComboboxSelected>>", lambda _e: self._tema(self._mas_valores[self.v_mas.get()]))
        self._tema(self.tema)
        label(b, T("Música y sonido", "Music and sound"), font=F["h3"], bg=C["card"]).pack(fill="x")
        label(b, T("Los del escenario del juego que presta el sonido de fondo (de momento no se pueden cambiar "
                   "desde aquí).", "Those of the game stage that lends the background sound (they cannot be "
                   "changed from here yet)."), fg=C["dim"], bg=C["card"], wrap=400).pack(fill="x", pady=(2, 0))

    def _contador(self):
        n = len(self.v_nombre.get().strip())
        if n > ES.LIMITE:
            self.lbl_cont.configure(text=T("%d/%d · sobra %d letra%s", "%d/%d · %d letter%s too many") % (
                n, ES.LIMITE, n - ES.LIMITE, "" if n - ES.LIMITE == 1 else "s"), fg=C["err"])
        else:
            self.lbl_cont.configure(text="%d/%d" % (n, ES.LIMITE), fg=C["dim"])

    def _tema(self, t):
        self.tema = t
        for k, lb in self.amb_lbl.items():
            lb.configure(bg=C["accent"] if k == t else C["frame"], fg=C["on_accent"] if k == t else C["text"])
        if t not in self.amb_lbl:
            nombre = next((n for n, v in self._mas_valores.items() if v == t), "")
            self.v_mas.set(nombre)
        else:
            self.v_mas.set("")

    # ------------------------------------------------------------------ paso 3
    def _paso3(self):
        e = ES.estado(self.juego, self.grupo)
        card = nucleo.Card(self.cuerpo, T("Esto es lo que se va a guardar", "This is what will be saved"))
        card.pack(fill="x")
        amb = next((a for a in ES.ambientes() if a["tema"] == self.tema), {"es": "?", "en": "?"})
        filas = [(T("Escenario", "Stage"), "%s (%s)" % (e["en"] if self.ingles else e["es"],
                                                        "Budokai 2" if self.juego == "b2" else "Budokai 1")),
                 (T("Nombre en el selector", "Name in the selector"), self.v_nombre.get().strip()),
                 (T("Ambiente", "Mood"), amb["en"] if self.ingles else amb["es"]),
                 (T("Dónde", "Where"), T("Un mod nuevo en tu carpeta de mods (se puede apagar o deshacer).",
                                         "A new mod in your mods folder (it can be turned off or undone)."))]
        if e.get("zona_b"):
            filas.append((T("Segunda zona", "Second area"), T("Sí, se añade sola (oculta en el selector).",
                                                              "Yes, added automatically (hidden in the selector).")))
        b = card.body
        b.columnconfigure(1, weight=1)
        for r, (et, val) in enumerate(filas):
            label(b, et, fg=C["dim"], bg=C["card"]).grid(row=r, column=0, sticky="nw", padx=(0, 14), pady=3)
            label(b, val, bg=C["card"], font=F["bold"], wrap=640).grid(row=r, column=1, sticky="w", pady=3)
        acc = tk.Frame(self.cuerpo, bg=C["bg"])
        acc.pack(fill="x", pady=(14, 0))
        self.b_guardar = _boton(acc, T("Guardar", "Save"), T("Crea el mod del escenario en tu carpeta de mods.",
                                                             "Creates the stage mod in your mods folder."),
                                self.guardar, "Big.Accent.TButton")
        self.b_guardar.pack(side="left")
        self.b_deshacer = _boton(acc, T("Deshacer", "Undo"), T("Quita lo que se acaba de guardar (queda una copia).",
                                                               "Removes what was just saved (a copy is kept)."),
                                 self.deshacer)
        self.estado = label(self.cuerpo, "", wrap=900)
        self.estado.pack(fill="x", pady=(10, 0))

    def _estado(self, nivel, texto):
        self.set_status(self.estado, nivel, texto)

    def plan(self):
        """planes.Plan con las ordenes del conversor (zona A y, si la hay, zona B)."""
        env = self.env
        ords = ES.ordenes(env.python, env.us, env.ps2, env.mods, self.juego, self.grupo, self.v_nombre.get(),
                          self.tema)
        return planes.Plan("escenario", [(T(es, en), cmd) for es, en, cmd, _c in ords], [c for *_x, c in ords], False)

    def guardar(self):
        err = ES.validar(self.v_nombre.get(), self.juego, self.grupo, self.tema, self.env.mods, ingles=self.ingles)
        if err:
            self._estado("err", err)
            return
        plan = self.plan()
        if self.app.selftest:
            for titulo, paso in plan.pasos:
                self.app.selftest_cmds.append(("escenario: " + titulo, [str(c) for c in paso]))
            return
        if self.app.jobs.busy():
            self._estado("warn", T("Ya hay otra tarea en marcha. Espera a que termine.",
                                   "Another task is already running. Wait for it to finish."))
            return
        self.b_guardar.configure(state="disabled")
        self._estado("warn", T("Trabajando… tarda uno o dos minutos. El detalle sale en el registro.",
                               "Working… it takes a minute or two. Details are in the log."))

        def hilo():
            res = planes.ejecutar(nucleo, self.env, plan, lambda t: self.cola.put(("log", t)))
            self.cola.put(("fin",) + res)
        threading.Thread(target=hilo, daemon=True).start()
        self.after(200, self._sondear)

    def _sondear(self):
        try:
            while True:
                m = self.cola.get_nowait()
                if m[0] == "log":
                    self.app.log_text(m[1] + "\n")
                    continue
                _, ok, _texto, lote = m
                self.lote = lote
                if ok:
                    lineas = []
                    for c in (lote.extra if lote else []):
                        lineas += ES.resumen(os.path.join(self.env.mods, c), self.ingles)
                    self._estado("ok", "\n".join(lineas + [T(
                        "Abre el juego, ve a Duelo y busca la casilla nueva en el selector de escenarios.",
                        "Open the game, go to Duel and look for the new box in the stage selector.")]))
                else:
                    self._estado("err", T("No se pudo crear el escenario. Mira el registro; «Deshacer» deja todo "
                                          "como estaba.", "The stage could not be created. Check the log; 'Undo' "
                                          "leaves everything as it was."))
                self.b_guardar.configure(state="normal")
                self.b_deshacer.pack(side="left", padx=10)
                return
        except queue.Empty:
            pass
        except tk.TclError:
            return
        if self.alive:
            self.after(200, self._sondear)

    def deshacer(self):
        try:
            n = len(nucleo.deshacer_ultimo(self.env))
        except OSError as ex:
            self._estado("err", str(ex))
            return
        self.b_deshacer.pack_forget()
        self._estado("ok" if n else "warn", T("Deshecho: el escenario ya no está en tus mods (la copia queda en "
                                              "modkit_backups).", "Undone: the stage is no longer in your mods (the "
                                              "copy stays in modkit_backups).") if n else
                     T("No había nada que deshacer.", "Nothing to undo."))
