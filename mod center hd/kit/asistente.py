# -*- coding: utf-8 -*-
"""Asistentes guiados del Mod Kit (fase 2): Personaje, Aspecto y aura, Traje e Instalar .zip.

`Asistente` es el componente reutilizable: pasos, Atras/Siguiente, un resumen final con «Cambiar» en cada linea
(preset y donante incluidos) y un solo boton «Montar y probar». Cada asistente solo dice sus pasos, como leer
las respuestas (`respuestas`) y que plan de kit/planes.py corresponde. `g` = modulo modkit_gui (como en portada.py)."""
from __future__ import annotations

import colorsys
import os
import queue
import sys
import threading

from kit import errores, musica, planes, probar, vistas


class Paso:
    """es/en: titulo; build(cuerpo): crea los widgets; check() -> texto de error o None;
    filas() -> [(etiqueta, valor)] para el resumen; entrar(): se llama al mostrarlo."""

    def __init__(self, es, en, build, check=None, filas=None, entrar=None):
        self.es, self.en, self.build, self.check, self.filas, self.entrar = es, en, build, check, filas, entrar


class Asistente:
    """Ventana con pasos. Las subclases rellenan `self.pasos`, `respuestas()` y `plan()`."""
    nombre = ("", "")
    intro = ("", "")

    def __init__(self, app, g):
        import tkinter as tk
        from tkinter import ttk
        self.app, self.g, self.tk, self.ttk = app, g, tk, ttk
        self.win = tk.Toplevel(app.root)
        if app.selftest:
            self.win.withdraw()
        self.win.title(g.T(*self.nombre))
        self.win.configure(bg=g.C["bg"])
        self.win.geometry("820x640")
        self.win.minsize(700, 520)
        self.idx, self.vuelve, self.hecho, self.lote, self.cancelar = 0, False, False, None, False
        self.cola, self.proc = queue.Queue(), None
        self.pasos = []
        self.construir_pasos()
        self._ui()
        self.ir(0)

    # ------------------------------------------------------------- lo que define cada asistente
    def construir_pasos(self):
        raise NotImplementedError

    def respuestas(self):
        raise NotImplementedError

    def plan(self):
        raise NotImplementedError

    # ------------------------------------------------------------- interfaz comun
    def _ui(self):
        g, tk, ttk = self.g, self.tk, self.ttk
        C, F, T = g.C, g.F, g.T
        top = tk.Frame(self.win, bg=C["bg"])
        top.pack(fill="x", padx=22, pady=(16, 6))
        g.label(top, T(*self.nombre), font=F["h1"]).pack(fill="x")
        g.label(top, T(*self.intro), fg=C["dim"], wrap=740).pack(fill="x", pady=(2, 0))
        self.lbl_paso = g.label(top, "", fg=C["accent"], font=F["bold"])
        self.lbl_paso.pack(fill="x", pady=(10, 0))
        self.barra = tk.Frame(top, bg=C["line"], height=4)
        self.barra.pack(fill="x", pady=(4, 0))
        self.barra_on = tk.Frame(self.barra, bg=C["accent"], height=4)
        self.barra_on.place(x=0, y=0, relheight=1, relwidth=0.0)
        foot = tk.Frame(self.win, bg=C["bg"])
        foot.pack(side="bottom", fill="x", padx=22, pady=(6, 16))
        self.lbl_err = g.label(foot, "", fg=C["err"], wrap=740)
        self.lbl_err.pack(fill="x", pady=(0, 6))
        bt = tk.Frame(foot, bg=C["bg"])
        bt.pack(fill="x")
        self.b_atras = ttk.Button(bt, text="←  " + T("Atrás", "Back"), command=self.atras)
        self.b_atras.pack(side="left")
        self.b_sig = ttk.Button(bt, text=T("Siguiente", "Next") + "  →", style="Accent.TButton", command=self.siguiente)
        self.b_sig.pack(side="right")
        ttk.Button(bt, text=T("Cerrar", "Close"), command=self.cerrar).pack(side="right", padx=8)
        self.cuerpo = tk.Frame(self.win, bg=C["bg"])
        self.cuerpo.pack(fill="both", expand=True, padx=22, pady=6)

    def _limpiar(self):
        for w in self.cuerpo.winfo_children():
            w.destroy()

    def ir(self, i):
        g = self.g
        self.idx = max(0, min(i, len(self.pasos)))
        self.lbl_err.configure(text="")
        self._limpiar()
        n = len(self.pasos)
        self.barra_on.place_configure(relwidth=min(1.0, self.idx / float(n)) if n else 0)
        if self.idx < n:
            p = self.pasos[self.idx]
            self.lbl_paso.configure(text=g.T("Paso %d de %d · %s", "Step %d of %d · %s") % (
                self.idx + 1, n, g.T(p.es, p.en)))
            card = g.Card(self.cuerpo)
            card.pack(fill="both", expand=True)
            if p.entrar:
                p.entrar()
            p.build(card.body)
            self.b_sig.configure(text=(g.T("Volver al resumen", "Back to summary") if self.vuelve else
                                       g.T("Siguiente", "Next")) + "  →", state="normal")
        else:
            self.lbl_paso.configure(text=g.T("Resumen · revisa y monta", "Summary · review and build"))
            self._resumen()
            self.b_sig.configure(state="disabled")
        self.b_atras.configure(state="normal" if self.idx > 0 and not self.vuelve else "disabled")

    def siguiente(self):
        if self.idx < len(self.pasos):
            p = self.pasos[self.idx]
            err = p.check() if p.check else None
            if err:
                self.lbl_err.configure(text=err)
                return False
        self.vuelve, destino = False, (len(self.pasos) if self.vuelve else self.idx + 1)
        self.ir(destino)
        return True

    def atras(self):
        if self.idx > 0:
            self.ir(self.idx - 1)

    def cambiar(self, i):
        self.vuelve = True
        self.ir(i)

    def filas(self):
        """[(indice de paso, etiqueta, valor)] de todo el resumen."""
        out = []
        for k, p in enumerate(self.pasos):
            for et, val in (p.filas() if p.filas else []):
                out.append((k, et, val))
        return out

    def _resumen(self):
        g, tk, ttk = self.g, self.tk, self.ttk
        C, F, T = g.C, g.F, g.T
        card = g.Card(self.cuerpo, T("Esto es lo que se va a hacer", "This is what will happen"),
                      T("Pulsa «Cambiar» en cualquier línea para corregirla.", "Press 'Change' on any line to fix it."))
        card.pack(fill="x")
        b = card.body
        b.columnconfigure(1, weight=1)
        for r, (k, et, val) in enumerate(self.filas()):
            g.label(b, et, fg=C["dim"], bg=C["card"]).grid(row=r, column=0, sticky="nw", padx=(0, 14), pady=3)
            g.label(b, val, bg=C["card"], font=F["bold"], wrap=420).grid(row=r, column=1, sticky="w", pady=3)
            ttk.Button(b, text=T("Cambiar", "Change"), style="Small.TButton",
                       command=lambda k=k: self.cambiar(k)).grid(row=r, column=2, padx=(10, 0), pady=3)
        acc = tk.Frame(self.cuerpo, bg=C["bg"])
        acc.pack(fill="x", pady=(14, 0))
        self.b_montar = ttk.Button(acc, text=T("Montar y probar", "Build and try"), style="Big.Accent.TButton",
                                   command=self.montar)
        self.b_montar.pack(side="left")
        self.b_deshacer = ttk.Button(acc, text=T("Deshacer", "Undo"), command=self.deshacer)
        self.b_juego = ttk.Button(acc, text=T("Abrir el juego", "Open the game"), command=self.abrir_juego)
        self.estado = g.label(self.cuerpo, "", wrap=740)
        self.estado.pack(fill="x", pady=(10, 0))
        if self.hecho:
            self._botones_fin()

    def estado_txt(self, nivel, texto):
        try:
            self.estado.configure(text=texto, fg={"ok": self.g.C["ok"], "err": self.g.C["err"],
                                                  "warn": self.g.C["warn"]}.get(nivel, self.g.C["dim"]))
        except self.tk.TclError:
            pass

    def _botones_fin(self):
        self.b_deshacer.pack(side="left", padx=10)
        if self.app.env.game and os.path.isfile(os.path.join(self.app.env.game, "dbz3.exe")):
            self.b_juego.pack(side="left")

    # ------------------------------------------------------------- montar / deshacer
    def validar_todo(self):
        for k, p in enumerate(self.pasos):
            err = p.check() if p.check else None
            if err:
                self.cambiar(k)
                self.lbl_err.configure(text=err)
                return False
        return True

    def montar(self):
        g = self.g
        if not self.validar_todo():
            return
        plan = self.plan()
        self.ultimo_plan = plan
        if self.app.selftest:
            for titulo, paso in plan.pasos:
                self.app.selftest_cmds.append(("asistente: " + titulo, [str(c) for c in paso]
                                               if isinstance(paso, list) else ["<funcion>"]))
            return
        if self.app.jobs.busy():
            self.estado_txt("warn", g.T("Ya hay otra tarea en marcha. Espera a que termine.",
                                        "Another task is already running. Wait for it to finish."))
            return
        self.b_montar.configure(state="disabled")
        self.estado_txt("warn", g.T("Trabajando… puede tardar unos minutos. El detalle sale en el registro.",
                                    "Working… this can take a few minutes. Details are in the log."))
        self.cancelar = False

        def hilo():
            res = planes.ejecutar(g, self.app.env, plan, lambda t: self.cola.put(("log", t)),
                                  lambda: self.cancelar, lambda p: setattr(self, "proc", p))
            self.prueba = None
            if res[0] and not self.cancelar:
                self.cola.put(("log", g.T("Probando el juego…", "Testing the game…")))
                try:
                    self.prueba = probar.probar(self.app.env.mods, self.app.env.game, plan.nuevos, g.LANG == "en",
                                                lambda t: self.cola.put(("log", t)))
                except Exception as ex:                      # noqa: BLE001 (la prueba nunca tumba el montaje)
                    self.cola.put(("log", "probar: %s" % ex))
            self.cola.put(("fin",) + res)
        threading.Thread(target=hilo, daemon=True).start()
        self.win.after(150, self._sondear)

    def _sondear(self):
        g = self.g
        try:
            while True:
                m = self.cola.get_nowait()
                if m[0] == "log":
                    self.app.log_text(m[1] + "\n")
                else:
                    _, ok, texto, lote = m
                    self.lote, self.hecho = lote, True
                    av = planes.avisos(texto) if ok else []
                    if ok:
                        extra = (g.T(" Ojo: %d aviso(s) en el registro.", " Note: %d warning(s) in the log.") % len(av)
                                 if av else "")
                        pr = getattr(self, "prueba", None)
                        if pr:
                            self.estado_txt({"ok": "ok", "aviso": "warn"}.get(pr[0], "err"), pr[1] + extra)
                        else:
                            self.estado_txt("warn" if av else "ok", g.T(
                                "Hecho. Abre el juego y pulsa JUGAR para probarlo.", "Done. Open the game and press "
                                "PLAY to try it.") + extra)
                    else:
                        que, hacer, _acc = errores.explicar(texto, g.LANG == "en")
                        self.estado_txt("err", "%s %s  %s" % (que, hacer, g.T(
                            "Si quieres, pulsa «Deshacer» para dejar todo como estaba.",
                            "If you prefer, press 'Undo' to leave everything as it was.")))
                    try:
                        self.b_montar.configure(state="normal")
                        self._botones_fin()
                    except self.tk.TclError:
                        pass
                    self.app.cache.pop("checks", None)
                    return
        except queue.Empty:
            pass
        try:
            self.win.after(150, self._sondear)
        except self.tk.TclError:
            pass

    def deshacer(self):
        g = self.g
        try:
            n = len(g.deshacer_ultimo(self.app.env))
        except OSError as ex:
            self.estado_txt("err", str(ex))
            return
        self.hecho = False
        self.b_deshacer.pack_forget()
        self.estado_txt("ok" if n else "warn", g.T("Deshecho: todo está como antes (la copia queda en modkit_backups).",
                                                   "Undone: everything is as before (the copy stays in "
                                                   "modkit_backups).") if n else g.T("No había nada que deshacer.",
                                                                                     "Nothing to undo."))

    def abrir_juego(self):
        import subprocess
        exe = os.path.join(self.app.env.game, "dbz3.exe")
        subprocess.Popen([exe], cwd=self.app.env.game)

    def cerrar(self):
        if self.proc is not None and self.proc.poll() is None:
            self.cancelar = True
            self.proc.kill()
        self.win.destroy()

    # ------------------------------------------------------------- piezas para los pasos
    def var(self, valor=""):
        return self.tk.StringVar(master=self.win, value=valor)

    def fila_combo(self, parent, texto, var, valores, fila, ancho=48, cuando=None):
        g = self.g
        g.label(parent, texto, bg=g.C["card"]).grid(row=fila, column=0, sticky="w", padx=(0, 12), pady=5)
        cb = self.ttk.Combobox(parent, textvariable=var, values=valores, state="readonly", width=ancho)
        cb.grid(row=fila, column=1, sticky="ew", pady=5)
        if cuando:
            cb.bind("<<ComboboxSelected>>", lambda _e: cuando())
        return cb


def color_hex(tono):
    return "#%02x%02x%02x" % tuple(int(255 * x) for x in colorsys.hsv_to_rgb(tono / 360.0, 0.85, 1.0))


def muestra_color(g, parent, nombre):
    """Cuadrado con el color del preset (solo para ver cual es)."""
    import tkinter as tk
    tono = next((t for k, _es, _en, t in g.COLOR_PRESETS if k == nombre), None)
    return tk.Frame(parent, bg=g.C["card"] if tono is None else color_hex(tono), width=26, height=26,
                    highlightthickness=1, highlightbackground=g.C["line"])


# ====================================================================================== 1. Personaje
class AsistentePersonaje(Asistente):
    nombre = ("Añadir un personaje", "Add a character")
    intro = ("Elige de qué juego sale, qué personaje es y cómo quieres que se comporte. Al final se monta solo.",
             "Pick which game it comes from, which character, and how it should behave. It builds itself at the end.")

    def construir_pasos(self):
        g = self.g
        self.v = {k: self.var() for k in ("fuente", "nombre", "preset", "donante", "buscar")}
        self.fuentes, self.entradas, self.clave = [], [], ""
        self.preset_keys = []
        self.pasos = [
            Paso("¿De qué juego?", "Which game?", self._p_fuente, self._c_fuente, self._f_fuente, self._buscar_fuentes),
            Paso("¿Qué personaje?", "Which character?", self._p_personaje, self._c_personaje, self._f_personaje,
                 self._buscar_lista),
            Paso("Nombre y ajustes", "Name and settings", self._p_ajustes, self._c_ajustes, self._f_ajustes)]

    # -- datos
    def _buscar_fuentes(self):
        app, g = self.app, self.g
        if "sources" not in app.cache and app.env.tools_installed():
            def done(job):
                srcs = g.parse_sources(job.text)
                if srcs:
                    app.cache["sources"] = srcs
                    if self.idx == 0:
                        self.ir(0)
            app.run(g.cmd_importer(app.env, ["fuentes"]), g.T("Buscando juegos", "Looking for games"), done, quiet=True)
        self.fuentes = [s for s in app.cache.get("sources", []) if s["state"] == "listo"]

    def _buscar_lista(self):
        app, g = self.app, self.g
        src = self.v["fuente"].get()
        clave = "lista_" + src
        self.entradas = app.cache.get(clave, [])
        if not self.entradas and src and app.env.tools_installed():
            def done(job):
                lst = g.parse_entries(job.text)
                if lst:
                    app.cache[clave] = lst
                    if self.idx == 1 and self.v["fuente"].get() == src:
                        self.ir(1)
            app.run(g.cmd_importer(app.env, ["lista", src]), g.T("Leyendo personajes", "Reading characters"), done,
                    quiet=True)

    # -- pasos
    def _p_fuente(self, b):
        g = self.g
        if not self.fuentes:
            g.label(b, g.T("Buscando tus juegos… (o no hay ninguno en la carpeta ps2_games)",
                           "Looking for your games… (or there are none in the ps2_games folder)"),
                    bg=g.C["card"], wrap=700).pack(fill="x")
        for s in self.fuentes:
            self.ttk.Radiobutton(b, text=s["name"], value=s["id"], variable=self.v["fuente"],
                                 style="Card.TRadiobutton").pack(anchor="w", pady=(6, 0))
            g.hint(b, g.source_note(s["id"]), wrap=680, bg=g.C["card"]).pack(anchor="w", padx=(24, 0))

    def _c_fuente(self):
        g = self.g
        if not self.v["fuente"].get():
            return g.T("Elige un juego de la lista.", "Pick a game from the list.")

    def _f_fuente(self):
        s = next((x for x in self.fuentes if x["id"] == self.v["fuente"].get()), None)
        return [(self.g.T("Juego", "Game"), s["name"] if s else self.v["fuente"].get())]

    def _p_personaje(self, b):
        g, tk = self.g, self.tk
        b.columnconfigure(0, weight=1)
        ent = self.ttk.Entry(b, textvariable=self.v["buscar"])
        ent.grid(row=0, column=0, sticky="ew")
        g.hint(b, g.T("Escribe para buscar.", "Type to search."), bg=g.C["card"]).grid(row=1, column=0, sticky="w")
        self.lista = tk.Listbox(b, height=14, exportselection=False, bg=g.C["bg"], fg=g.C["text"], bd=0,
                                highlightthickness=1, highlightbackground=g.C["line"], font=g.F["body"],
                                selectbackground=g.C["accent_dim"])
        self.lista.grid(row=2, column=0, sticky="nsew", pady=(4, 0))
        b.rowconfigure(2, weight=1)
        self.v["buscar"].trace_add("write", lambda *_: self._filtrar())
        self._filtrar()

    def _filtrar(self):
        try:
            t = self.v["buscar"].get().lower().strip()
            self.visibles = [e for e in self.entradas if t in e["name"].lower() or t in e["key"].lower()]
            self.lista.delete(0, "end")
            for e in self.visibles:
                self.lista.insert("end", e["name"] + ("  ★" if e["port"] else ""))
            for i, e in enumerate(self.visibles):
                if e["key"] == self.clave:
                    self.lista.selection_set(i)
        except self.tk.TclError:
            pass

    def _c_personaje(self):
        g = self.g
        sel = self.lista.curselection() if hasattr(self, "lista") and self.lista.winfo_exists() else ()
        if sel:
            self.elegir(self.visibles[sel[0]])
        if not self.clave:
            return g.T("Elige un personaje de la lista.", "Pick a character from the list.")

    def elegir(self, e):
        g = self.g
        if e["key"] != self.clave:
            self.clave, self.entrada = e["key"], e
            self.v["nombre"].set(e["suggested"] or e["name"])
            self.v["donante"].set(g.donor_values(self._auto())[g.donor_index(e["donor"], True)]
                                  if e["donor"] >= 0 else self._auto())

    def _f_personaje(self):
        e = getattr(self, "entrada", None)
        return [(self.g.T("Personaje", "Character"), e["name"] if e else self.clave)]

    def _auto(self):
        return self.g.T("Automático (el que mejor encaja)", "Automatic (best fit)")

    def _presets(self):
        src = self.v["fuente"].get()
        pre = self.g.read_toml(os.path.join(self.g.HERE, "presets_importar.toml")).get(src, {})
        return {k: v for k, v in pre.items() if isinstance(v, dict)}

    def _p_ajustes(self, b):
        g = self.g
        b.columnconfigure(1, weight=1)
        g.label(b, g.T("Nombre en el juego", "Name in the game"), bg=g.C["card"]).grid(row=0, column=0, sticky="w",
                                                                                      padx=(0, 12), pady=5)
        self.ttk.Entry(b, textvariable=self.v["nombre"], width=30).grid(row=0, column=1, sticky="w", pady=5)
        pres = self._presets()
        self.preset_keys = list(pres) or ["original"]
        if self.v["preset"].get() not in self.preset_keys:
            self.v["preset"].set(self.preset_keys[0])
        self.fila_combo(b, g.T("Ajuste", "Preset"), self.v["preset"], self.preset_keys, 1, 30, self._desc_preset)
        self.desc = g.hint(b, "", wrap=560, bg=g.C["card"])
        self.desc.grid(row=2, column=1, sticky="w")
        self.fila_combo(b, g.T("Personaje base (donante)", "Base character (donor)"), self.v["donante"],
                        g.donor_values(self._auto()), 3)
        g.hint(b, g.T("El personaje base aporta lo que el juego de origen no trae (técnicas, voz…). En "
                      "«Automático» el kit elige el que mejor encaja.",
                      "The base character provides what the source game lacks (techniques, voice…). 'Automatic' "
                      "lets the kit pick the best fit."), wrap=560, bg=g.C["card"]).grid(row=4, column=1, sticky="w")
        self._desc_preset()

    def _desc_preset(self):
        try:
            self.desc.configure(text=self._presets().get(self.v["preset"].get(), {}).get("descripcion", ""))
        except self.tk.TclError:
            pass

    def _c_ajustes(self):
        g = self.g
        n = self.v["nombre"].get().strip()
        if not n or len(n) > 24:
            return g.T("Pon un nombre de hasta 24 letras.", "Enter a name of up to 24 characters.")

    def _donante(self):
        vals = self.g.donor_values(self._auto())
        v = self.v["donante"].get()
        return self.g.donor_from_index(vals.index(v), True) if v in vals else -1

    def _f_ajustes(self):
        g = self.g
        d = self._donante()
        e = getattr(self, "entrada", None)
        auto = g.T("Automático", "Automatic") + (": " + g.donor_name(e["donor"]) if e and e["donor"] >= 0 else "")
        desc = self._presets().get(self.v["preset"].get(), {}).get("descripcion", "")
        return [(g.T("Nombre en el juego", "Name in the game"), self.v["nombre"].get().strip()),
                (g.T("Ajuste", "Preset"), self.v["preset"].get() + (" · " + desc if desc else "")),
                (g.T("Personaje base (donante)", "Base character (donor)"), g.donor_name(d) if d >= 0 else auto)]

    def respuestas(self):
        return {"fuente": self.v["fuente"].get(), "clave": self.clave, "nombre": self.v["nombre"].get().strip(),
                "preset": self.v["preset"].get(), "donante": self._donante()}

    def plan(self):
        return planes.plan_personaje(self.g, self.app.env, self.respuestas())


# ====================================================================================== 2. Aspecto y aura
class AsistenteAspecto(Asistente):
    nombre = ("Cambiar el aspecto y el aura", "Change the look and aura")
    intro = ("Elige un personaje del juego, el color de su aura y su ki y, si quieres, tus propias imágenes.",
             "Pick a game character, the colour of its aura and ki and, if you want, your own images.")

    def construir_pasos(self):
        g = self.g
        self.v = {k: self.var() for k in ("pj", "aura", "ki", "imagenes", "forma", "carpeta")}
        self.v["pj"].set(g.donor_values()[0])
        self.ninguno = g.T("(no cambiar)", "(leave as is)")
        self.v["aura"].set(self.ninguno)
        self.v["ki"].set(self.ninguno)
        self.v["imagenes"].set("no")
        self.mod_tex = None
        self.catalogo = g.load_catalog(self.app.env)
        self.pasos = [
            Paso("¿Qué personaje?", "Which character?", self._p_pj, None, self._f_pj),
            Paso("Aura y ki", "Aura and ki", self._p_color, self._c_color, self._f_color),
            Paso("Tus imágenes (opcional)", "Your images (optional)", self._p_img, self._c_img, self._f_img)]

    def _pid(self):
        vals = self.g.donor_values()
        v = self.v["pj"].get()
        return self.g.DONORS[vals.index(v)][0] if v in vals else 0

    def _pnombre(self):
        return self.g.donor_name(self._pid())

    def _p_pj(self, b):
        g = self.g
        b.columnconfigure(1, weight=1)
        self.fila_combo(b, g.T("Personaje", "Character"), self.v["pj"], g.donor_values(), 0, 40)

    def _f_pj(self):
        return [(self.g.T("Personaje", "Character"), self._pnombre())]

    def _colores(self):
        return [self.ninguno] + [self.g.T(es, en) for _k, es, en, _t in self.g.COLOR_PRESETS]

    def _clave_color(self, texto):
        for k, es, en, _t in self.g.COLOR_PRESETS:
            if texto == self.g.T(es, en):
                return k
        return None

    def _p_color(self, b):
        g = self.g
        b.columnconfigure(1, weight=1)
        vistas_mu = {}
        aura_vista = vistas.MuestraAura(b, g)
        aura_vista.grid(row=0, column=2, rowspan=4, sticky="n", padx=(16, 0))
        for r, (k, txt) in enumerate((("aura", g.T("Color del aura", "Aura colour")),
                                      ("ki", g.T("Color del ki (bolas de energía)", "Ki colour (energy blasts)")))):
            g.label(b, txt, bg=g.C["card"]).grid(row=r, column=0, sticky="w", padx=(0, 12), pady=5)
            box = self.tk.Frame(b, bg=g.C["card"])
            box.grid(row=r, column=1, sticky="w", pady=5)
            cb = self.ttk.Combobox(box, textvariable=self.v[k], values=self._colores(), state="readonly", width=28)
            cb.pack(side="left")
            mu = muestra_color(g, box, self._clave_color(self.v[k].get()))
            mu.pack(side="left", padx=(8, 0))
            vistas_mu[k] = mu

            def cambia(_e=None, k=k, mu=mu):
                tono = next((t for kk, _es, _en, t in g.COLOR_PRESETS if kk == self._clave_color(self.v[k].get())),
                            None)
                mu.configure(bg=g.C["card"] if tono is None else color_hex(tono))
                aura_vista.pintar(vistas_mu["aura"].cget("bg"), vistas_mu["ki"].cget("bg"))
            cb.bind("<<ComboboxSelected>>", cambia)
        aura_vista.pintar(vistas_mu["aura"].cget("bg"), vistas_mu["ki"].cget("bg"))
        g.hint(b, g.T("Si dejas los dos sin cambiar, solo se montarán tus imágenes (siguiente paso).",
                      "If you leave both unchanged, only your images (next step) will be built."), wrap=560,
               bg=g.C["card"]).grid(row=2, column=1, sticky="w")

    def _c_color(self):
        return None

    def _f_color(self):
        g = self.g
        return [(g.T("Color del aura", "Aura colour"), self.v["aura"].get()),
                (g.T("Color del ki", "Ki colour"), self.v["ki"].get())]

    def _formas(self):
        """Formas del personaje con su bin (catalog_b3.cat ∩ modelos del personaje en roster_db)."""
        import json
        db = json.load(open(os.path.join(self.g.HERE, "roster_db.json"), encoding="utf-8"))
        e = next((x for x in db["ids"] if x["id"] == self._pid()), {})
        bins = set(e.get("models", []))
        return [c for c in self.catalogo if c["bin"] in bins]

    def _p_img(self, b):
        g = self.g
        b.columnconfigure(1, weight=1)
        for val, txt in (("no", g.T("No cambiar las imágenes", "Do not change the images")),
                         ("si", g.T("Poner mis imágenes (PNG)", "Use my images (PNG)"))):
            self.ttk.Radiobutton(b, text=txt, value=val, variable=self.v["imagenes"],
                                 style="Card.TRadiobutton").grid(row=0 if val == "no" else 1, column=0,
                                                                 columnspan=2, sticky="w", pady=3)
        self.formas = self._formas()
        etiquetas = [g.char_label(c) for c in self.formas]
        if self.v["forma"].get() not in etiquetas:
            self.v["forma"].set(etiquetas[0] if etiquetas else "")
        self.fila_combo(b, g.T("Traje o forma", "Costume or form"), self.v["forma"], etiquetas, 2, 44)
        g.label(b, g.T("Carpeta con tus PNG", "Folder with your PNGs"), bg=g.C["card"]).grid(
            row=3, column=0, sticky="w", padx=(0, 12), pady=5)
        g.PathEntry(b, self.v["carpeta"], "dir", bg=g.C["card"]).grid(row=3, column=1, sticky="ew", pady=5)
        self.ttk.Button(b, text=g.T("Sacar las imágenes del personaje para editarlas", "Export the character's "
                                    "images so I can edit them"), command=self._sacar).grid(row=4, column=1,
                                                                                            sticky="w", pady=5)
        g.hint(b, g.T("Edita los PNG con tu programa, guarda con el mismo nombre y tamaño, y elige aquí su carpeta.",
                      "Edit the PNGs in your program, save with the same name and size, and pick their folder here."),
               wrap=560, bg=g.C["card"]).grid(row=5, column=1, sticky="w")

    def _forma(self):
        return next((c for c in getattr(self, "formas", self._formas()) if self.g.char_label(c) == self.v["forma"].get()),
                    None)

    def _sacar(self):
        g, app = self.g, self.app
        c = self._forma()
        if not c:
            self.lbl_err.configure(text=g.T("Este personaje no tiene imágenes que sacar.", "This character has no "
                                            "images to export."))
            return
        self.mod_tex = self.mod_tex or g.unique_mod_name(app.env.mods, "tex_" + g.slug(self._pnombre()))
        carpeta = os.path.join(app.env.mods, self.mod_tex, "textures")

        def done(job):
            if job.rc == 0:
                self.v["carpeta"].set(carpeta)
                self.v["imagenes"].set("si")
                g.open_path(carpeta)
            self.lbl_err.configure(text="" if job.rc == 0 else app.outcome(job)[1])
        app.run(g.cmd_tex_extract(app.env, c["bin"], self.mod_tex), g.T("Sacar imágenes", "Export images"), done)

    def _c_img(self):
        g = self.g
        if self.v["imagenes"].get() == "si":
            if not self._forma():
                return g.T("Elige el traje o forma.", "Pick the costume or form.")
            if not os.path.isdir(self.v["carpeta"].get().strip()):
                return g.T("Elige la carpeta con tus PNG.", "Pick the folder with your PNGs.")
        elif not self._clave_color(self.v["aura"].get()) and not self._clave_color(self.v["ki"].get()):
            return g.T("No has elegido nada que cambiar: pon un color o tus imágenes.", "You have not picked anything "
                       "to change: set a colour or your images.")

    def _f_img(self):
        g = self.g
        if self.v["imagenes"].get() != "si":
            return [(g.T("Imágenes", "Images"), g.T("No se cambian", "Not changed"))]
        return [(g.T("Imágenes", "Images"), "%s · %s" % (self.v["forma"].get(), self.v["carpeta"].get()))]

    def respuestas(self):
        r = {"id": self._pid(), "nombre": self._pnombre(), "aura": self._clave_color(self.v["aura"].get()),
             "ki": self._clave_color(self.v["ki"].get())}
        if self.v["imagenes"].get() == "si" and self._forma():
            r.update(bin=self._forma()["bin"], png_dir=self.v["carpeta"].get().strip(), mod_texturas=self.mod_tex)
        return r

    def plan(self):
        return planes.plan_aspecto(self.g, self.app.env, self.respuestas())


# ====================================================================================== 3. Traje
class AsistenteTraje(Asistente):
    nombre = ("Dar un traje a un personaje", "Give a character a costume")
    intro = ("Añade un traje extra a un personaje del juego, con un personaje que ya importaste o con tu propio modelo.",
             "Add an extra costume to a game character, using a character you already imported or your own model.")

    def construir_pasos(self):
        g = self.g
        self.v = {k: self.var() for k in ("pj", "origen", "port", "modelo", "nombre")}
        self.v["pj"].set(g.donor_values()[0])
        self.v["origen"].set("port")
        self.pasos = [
            Paso("¿A quién?", "Who gets it?", self._p_pj, None, lambda: [(g.T("Personaje", "Character"), self._pnombre())]),
            Paso("¿Qué modelo?", "Which model?", self._p_modelo, self._c_modelo, self._f_modelo),
            Paso("Nombre", "Name", self._p_nombre, self._c_nombre, self._f_nombre)]

    def _pid(self):
        vals = self.g.donor_values()
        v = self.v["pj"].get()
        return self.g.DONORS[vals.index(v)][0] if v in vals else 0

    def _pnombre(self):
        return self.g.donor_name(self._pid())

    def _p_pj(self, b):
        b.columnconfigure(1, weight=1)
        self.fila_combo(b, self.g.T("Personaje del juego", "Game character"), self.v["pj"], self.g.donor_values(), 0, 40)

    def _ports(self):
        return [m["name"] for m in self.g.scan_mods(self.app.env.mods)
                if m["type"] == "personaje" and not m["name"].startswith("traje_")]

    def _p_modelo(self, b):
        g = self.g
        b.columnconfigure(1, weight=1)
        ports = self._ports()
        self.ttk.Radiobutton(b, text=g.T("De un personaje que ya importé", "From a character I already imported"),
                             value="port", variable=self.v["origen"], style="Card.TRadiobutton").grid(
            row=0, column=0, columnspan=2, sticky="w", pady=3)
        self.fila_combo(b, g.T("Personaje importado", "Imported character"), self.v["port"], ports, 1, 40)
        if ports and self.v["port"].get() not in ports:
            self.v["port"].set(ports[0])
        if not ports:
            g.hint(b, g.T("Todavía no has importado ninguno: usa «Añadir un personaje» primero.",
                          "You have not imported any yet: use 'Add a character' first."),
                   bg=g.C["card"]).grid(row=2, column=1, sticky="w")
        self.ttk.Radiobutton(b, text=g.T("De mi propio modelo (.amb o .bin)", "From my own model (.amb or .bin)"),
                             value="modelo", variable=self.v["origen"], style="Card.TRadiobutton").grid(
            row=3, column=0, columnspan=2, sticky="w", pady=(10, 3))
        g.label(b, g.T("Archivo del modelo", "Model file"), bg=g.C["card"]).grid(row=4, column=0, sticky="w",
                                                                                padx=(0, 12))
        g.PathEntry(b, self.v["modelo"], "file", bg=g.C["card"]).grid(row=4, column=1, sticky="ew", pady=4)

    def _c_modelo(self):
        g = self.g
        if self.v["origen"].get() == "port" and not self.v["port"].get():
            return g.T("Elige un personaje importado o usa tu propio modelo.", "Pick an imported character or use "
                       "your own model.")
        if self.v["origen"].get() == "modelo" and not os.path.isfile(self.v["modelo"].get().strip()):
            return g.T("Elige el archivo del modelo.", "Pick the model file.")

    def _f_modelo(self):
        g = self.g
        if self.v["origen"].get() == "port":
            return [(g.T("Modelo", "Model"), g.T("Del personaje importado «%s»", "From imported character '%s'")
                     % self.v["port"].get())]
        return [(g.T("Modelo", "Model"), self.v["modelo"].get())]

    def _p_nombre(self, b):
        g = self.g
        if not self.v["nombre"].get():
            self.v["nombre"].set(g.T("Traje de %s", "%s costume") % self._pnombre())
        g.label(b, g.T("Nombre del traje", "Costume name"), bg=g.C["card"]).grid(row=0, column=0, sticky="w",
                                                                                padx=(0, 12))
        self.ttk.Entry(b, textvariable=self.v["nombre"], width=34).grid(row=0, column=1, sticky="w")

    def _c_nombre(self):
        if not self.v["nombre"].get().strip():
            return self.g.T("Pon un nombre.", "Enter a name.")

    def _f_nombre(self):
        return [(self.g.T("Nombre del traje", "Costume name"), self.v["nombre"].get().strip())]

    def respuestas(self):
        r = {"personaje": self._pid(), "nombre": self.v["nombre"].get().strip()}
        if self.v["origen"].get() == "port":
            r["port"] = self.v["port"].get()
        else:
            r["modelo"] = self.v["modelo"].get().strip()
        return r

    def plan(self):
        return planes.plan_traje(self.g, self.app.env, self.respuestas())


# ====================================================================================== 4. Instalar .zip
class AsistenteZip(Asistente):
    nombre = ("Instalar un mod (.zip)", "Install a mod (.zip)")
    intro = ("Elige el .zip del mod, mira lo que trae y se instala. Si ya tenías uno igual, se guarda una copia.",
             "Pick the mod's .zip, see what it contains, and install it. If you already had one, a copy is kept.")

    def construir_pasos(self):
        g = self.g
        self.v = {"zip": self.var(), "ejec": self.var("0")}
        self.info, self.marcas = {"mods": {}, "ejecutables": [], "error": ""}, {}
        self.pasos = [Paso("Elige el .zip", "Pick the .zip", self._p_zip, self._c_zip, lambda: [
            (g.T("Archivo", "File"), self.v["zip"].get())], self._leer),
            Paso("Qué trae", "What is inside", self._p_contenido, self._c_contenido, self._f_contenido)]

    def _p_zip(self, b):
        g = self.g
        b.columnconfigure(1, weight=1)
        g.label(b, g.T("Archivo .zip", ".zip file"), bg=g.C["card"]).grid(row=0, column=0, sticky="w", padx=(0, 12))
        g.PathEntry(b, self.v["zip"], "file", [("ZIP", "*.zip")], bg=g.C["card"]).grid(row=0, column=1, sticky="ew")

    def _leer(self):
        pass

    def _errores(self):
        g = self.g
        return {"no_zip": g.T("Ese archivo no es un .zip válido.", "That file is not a valid .zip."),
                "ruta_peligrosa": g.T("El .zip tiene rutas peligrosas: no se instala por seguridad.",
                                      "The .zip has unsafe paths: it will not be installed."),
                "enorme": g.T("El .zip es demasiado grande (más de 6 GB descomprimido).", "The .zip is too big "
                              "(over 6 GB uncompressed)."),
                "vacio": g.T("El .zip está vacío.", "The .zip is empty.")}

    def _c_zip(self):
        g = self.g
        z = self.v["zip"].get().strip()
        if not os.path.isfile(z):
            return g.T("Elige el archivo .zip.", "Pick the .zip file.")
        self.info = planes.inspeccionar_zip(z)
        if self.info["error"]:
            return self._errores().get(self.info["error"], self.info["error"])
        self.marcas = {m: self.var("1") for m in self.info["mods"]}

    def _p_contenido(self, b):
        g = self.g
        g.label(b, g.T("Mods que trae (quita la marca de los que no quieras):", "Mods inside (untick the ones you "
                       "do not want):"), bg=g.C["card"]).pack(anchor="w")
        for m, v in self.marcas.items():
            existe = os.path.exists(os.path.join(self.app.env.mods, m)) or os.path.exists(
                os.path.join(self.app.env.mods, m + ".disabled"))
            txt = m + ("   · " + g.T("ya lo tienes: se guardará una copia", "you already have it: a copy will be kept")
                       if existe else "")
            self.ttk.Checkbutton(b, text=txt, variable=v, style="Card.TCheckbutton").pack(anchor="w", pady=2)
        if self.info["ejecutables"]:
            g.label(b, g.T("Atención: este .zip trae archivos de programa (%s). Pueden hacer cualquier cosa en tu "
                           "PC. Por defecto NO se instalan; marca la casilla solo si confías en quien lo hizo.",
                           "Warning: this .zip has program files (%s). They can do anything on your PC. They are NOT "
                           "installed by default; tick the box only if you trust its author.")
                    % ", ".join(sorted({os.path.splitext(x)[1].lower() for x in self.info["ejecutables"]})),
                    fg=g.C["warn"], bg=g.C["card"], wrap=700).pack(anchor="w", pady=(10, 2))
            self.ttk.Checkbutton(b, text=g.T("Confío en su autor: instalar también esos archivos", "I trust its "
                                             "author: install those files too"), variable=self.v["ejec"],
                                 onvalue="1", offvalue="0", style="Card.TCheckbutton").pack(anchor="w")

    def _elegidos(self):
        return [m for m, v in self.marcas.items() if v.get() == "1"]

    def _c_contenido(self):
        if not self._elegidos():
            return self.g.T("Marca al menos un mod.", "Tick at least one mod.")

    def _f_contenido(self):
        g = self.g
        el = self._elegidos()
        ya = [m for m in el if os.path.exists(os.path.join(self.app.env.mods, m))]
        filas = [(g.T("Mods a instalar", "Mods to install"), ", ".join(el)),
                 (g.T("Se sustituyen (con copia)", "Replaced (with a copy)"), ", ".join(ya) or g.T("ninguno", "none"))]
        if self.info["ejecutables"]:
            filas.append((g.T("Archivos de programa", "Program files"), g.T("se instalan", "installed")
                          if self.v["ejec"].get() == "1" else g.T("NO se instalan", "NOT installed")))
        return filas

    def respuestas(self):
        return {"zip": self.v["zip"].get().strip(), "mods": self._elegidos(), "ejecutables": self.v["ejec"].get() == "1"}

    def plan(self):
        return planes.plan_zip(self.g, self.app.env, self.respuestas())


class AsistenteMusica(Asistente):
    nombre = ("Poner música", "Add music")
    intro = ("Elige una canción tuya (mp3, wav, ogg, flac o adx) y se prepara para sonar en los combates.",
             "Pick a song of yours (mp3, wav, ogg, flac or adx) and it is prepared to play during fights.")

    def construir_pasos(self):
        g = self.g
        self.v = {"archivo": self.var(), "nombre": self.var()}
        self.pasos = [Paso("Tu canción", "Your song", self._p_archivo, self._c_archivo, lambda: [
            (g.T("Archivo", "File"), self.v["archivo"].get().strip()),
            (g.T("Nombre", "Name"), self.v["nombre"].get().strip())])]

    def _p_archivo(self, b):
        g = self.g
        b.columnconfigure(1, weight=1)
        g.label(b, g.T("Archivo de audio", "Audio file"), bg=g.C["card"]).grid(row=0, column=0, sticky="w", padx=(0, 12))
        g.PathEntry(b, self.v["archivo"], "file", [("Audio", "*.mp3 *.wav *.ogg *.flac *.m4a *.adx")],
                    bg=g.C["card"]).grid(row=0, column=1, sticky="ew", pady=4)
        g.label(b, g.T("Nombre en el juego", "Name in game"), bg=g.C["card"]).grid(row=1, column=0, sticky="w",
                                                                                 padx=(0, 12))
        self.ttk.Entry(b, textvariable=self.v["nombre"], width=34).grid(row=1, column=1, sticky="w", pady=4)
        vistas.Oyente(b, g, lambda: self.v["archivo"].get().strip(), musica.ffmpeg).grid(row=2, column=1, sticky="w", pady=4)
        g.hint(b, g.T("Para elegirla: en el selector de combate pulsa L1/R1 (o [ ]) o usa F1 > Música de combate. "
                      "Recomendado: de 1 a 6 minutos.",
                      "To pick it: on the fight selector press L1/R1 (or [ ]) or use F1 > Fight music. "
                      "Recommended: 1 to 6 minutes."),
               bg=g.C["card"], wrap=400).grid(row=3, column=1, sticky="w", pady=(10, 0))

    def _c_archivo(self):
        g = self.g
        f = self.v["archivo"].get().strip()
        if not os.path.isfile(f):
            return g.T("Elige el archivo de audio.", "Pick the audio file.")
        if not self.v["nombre"].get().strip():
            self.v["nombre"].set(os.path.splitext(os.path.basename(f))[0])
        if not f.lower().endswith(".adx") and not musica.ffmpeg():
            return g.T("Falta ffmpeg para convertir ese audio: instálalo o elige un .adx.",
                       "ffmpeg is needed to convert that audio: install it or pick an .adx.")

    def respuestas(self):
        return {"archivo": self.v["archivo"].get().strip(), "nombre": self.v["nombre"].get().strip()}

    def plan(self):
        return planes.plan_musica(self.g, self.app.env, self.respuestas())


ASISTENTES = {"w_personaje": AsistentePersonaje, "w_aspecto": AsistenteAspecto, "w_traje": AsistenteTraje,
              "w_zip": AsistenteZip, "w_musica": AsistenteMusica}


def abrir(app, clave, g):
    """Abre el asistente `clave` (tarjetas de la portada). g = modulo modkit_gui."""
    return ASISTENTES[clave](app, g)
