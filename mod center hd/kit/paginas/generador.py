# -*- coding: utf-8 -*-
"""Generador de animaciones (KIT-09, maqueta studies 5/ENTREGAS/KIT_VISUAL/06_generador.png). Pestaña «Biblioteca»
del modo avanzado y pantalla a la que lleva «Nueva técnica › Movimiento». Logica sin interfaz: kit/generador.py;
fotogramas: kit/pelicula.py; receta y mod: biblioteca_anim.py. Todo lo pesado va en UN hilo de trabajo (el render
toon comparte el modelo cargado: dos hilos se pisarian) y la ventana nunca se congela."""
from __future__ import annotations

import os
import queue
import threading
import time

from kit import generador as G
from kit import nucleo, vistas
from kit.nucleo import C, F, Page, T, Tooltip, label, make_clickable, tk, ttk

pel = G.pel
VIS, ORIG, MINI = 280, 220, 84          # px del visor, del panel «Original» y de las miniaturas
MAX_TILES = 45
PASO_FANTASMA = 4


# ------------------------------------------------------------------------------------------ hilo de trabajo
class _Trabajo:
    """Un solo hilo para leer el juego y renderizar (prioridad: 0 lo que se ve, 1 afinado, 2 miniaturas)."""

    def __init__(self):
        self.q, self.fin, self.n, self.hilo = queue.PriorityQueue(), queue.Queue(), 0, None

    def pedir(self, prio, fn, fin):
        self.n += 1
        self.q.put((prio, self.n, fn, fin))
        if self.hilo is None or not self.hilo.is_alive():
            self.hilo = threading.Thread(target=self._bucle, daemon=True)
            self.hilo.start()

    def _bucle(self):
        while True:
            _p, _n, fn, fin = self.q.get()
            try:
                res = (True, fn())
            except Exception as ex:  # noqa: BLE001  (un clip roto no tumba el Kit)
                res = (False, ex)
            self.fin.put((fin, res))
            del fn, fin, res        # que las referencias a la pagina las suelte el hilo de Tk, no este

    def recoger(self):
        try:
            while True:
                fin, res = self.fin.get_nowait()
                try:
                    fin(*res)
                except tk.TclError:
                    pass
        except queue.Empty:
            pass


TRABAJO = _Trabajo()
_BIB = {}          # biblioteca cargada (una vez por sesion)


def bombear(widget):
    """Recoge los resultados del hilo mientras `widget` viva."""
    def tic():
        if widget.winfo_exists():
            TRABAJO.recoger()
            widget.after(60, tic)
    widget.after(60, tic)


def biblioteca(fin):
    if "clips" in _BIB:
        fin(True, _BIB["clips"])
        return

    def cargar():
        _BIB["clips"] = G.clips()
        return _BIB["clips"]
    TRABAJO.pedir(-1, cargar, fin)


def _sobre(im, bg, tam=None):
    from PIL import Image
    tam = tam or im.size
    base = Image.new("RGBA", tam, bg)
    base.alpha_composite(im.convert("RGBA"), ((tam[0] - im.width) // 2, (tam[1] - im.height) // 2))
    return base


def _fantasma(im):
    from PIL import Image
    r, g, b, a = im.split()
    oscuro = Image.merge("RGB", (r, g, b)).point(lambda v: int(v * 0.55))
    oscuro.putalpha(a.point(lambda v: int(v * 0.38)))
    return oscuro


def _hex_rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5)) + (255,)


# ------------------------------------------------------------------------------------------ visor
class Visor(tk.Frame):
    """Pelicula de un clip: reproducir/pausa, fotograma a fotograma, velocidad, 3 vistas, fantasmas, marcas de golpe
    y afinado al pausar. `esclavo`: sin controles, sigue la fraccion de tiempo de otro visor."""

    def __init__(self, master, px=VIS, controles=True, titulo="", al_mover=None, seguir=False):
        super().__init__(master, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
        self.px, self.controles, self.al_mover, self.seguir = px, controles, al_mover, seguir
        self.ims, self.fant, self.marcas, self.fino = [], [], [], None
        self.f, self.vel, self.play, self.gen, self.src, self.vista = 0.0, 1.0, False, 0, None, "lado"
        self.v_fant = tk.BooleanVar(master=self, value=True)
        self.v_marcas = tk.BooleanVar(master=self, value=True)
        top = tk.Frame(self, bg=C["card"])
        top.pack(fill="x", padx=10, pady=(8, 0))
        self.lbl_tit = tk.Label(top, text=titulo, bg=C["card"], fg=C["accent"], font=F["bold_sm"], anchor="w")
        self.lbl_tit.pack(side="left")
        if controles:
            self.seg_vista = nucleo.Segmented(top, [("frente", T("Frente", "Front")), ("lado", T("Lado", "Side")),
                                                    ("3/4", "3/4")], "lado", self._vista)
            self.seg_vista.pack(side="right")
            Tooltip(self.seg_vista, T("Desde dónde se mira (3 vistas fijas).", "Where it is seen from (3 fixed views)."))
        abajo2 = tk.Frame(self, bg=C["card"])
        abajo2.pack(side="bottom", fill="x", padx=10, pady=(0, 6))
        abajo = tk.Frame(self, bg=C["card"])
        abajo.pack(side="bottom", fill="x", padx=10, pady=(4, 2))
        self.barra = tk.Canvas(self, height=16, bg=C["card"], highlightthickness=0)
        self.barra.pack(side="bottom", fill="x", padx=10)
        caja = tk.Frame(self, bg=C["log_bg"], width=px, height=px)
        caja.pack(padx=10, pady=6, fill="both", expand=True)
        caja.pack_propagate(False)
        self.img = tk.Label(caja, bg=C["log_bg"], fg=C["dim"], font=F["small"], compound="center",
                            text=T("Elige un clip de la biblioteca", "Pick a clip from the library"))
        self.img.pack(fill="both", expand=True)
        self.carga = tk.Frame(self.img, bg=C["log_bg"])
        self.lbl_carga = tk.Label(self.carga, text="", bg=C["log_bg"], fg=C["dim"], font=F["small"])
        self.lbl_carga.pack()
        self.pb = ttk.Progressbar(self.carga, length=min(220, px - 40), mode="determinate")
        self.pb.pack(pady=4)
        self.barra.bind("<Button-1>", self._clic_barra)
        self.barra.bind("<B1-Motion>", self._clic_barra)
        self.barra.bind("<Configure>", lambda _e: self._dibujar_barra())
        self.lbl_f = tk.Label(abajo, text="", bg=C["card"], fg=C["dim"], font=F["small"])
        if controles:
            for txt, tip, fn in (("⏮", T("Fotograma anterior.", "Previous frame."), lambda: self.paso(-1)),
                                 ("⏯", T("Reproducir o pausar.", "Play or pause."), self.alternar),
                                 ("⏭", T("Fotograma siguiente.", "Next frame."), lambda: self.paso(1))):
                b = ttk.Button(abajo, text=txt, width=2, style="Small.TButton", command=fn)
                b.pack(side="left", padx=(0, 4))
                Tooltip(b, tip)
            self.lbl_f.pack(side="left", padx=6)
            for txt, var, tip in ((T("marcas de golpe", "hit marks"), self.v_marcas,
                                   T("Rayas rojas en la barra: los fotogramas en los que golpea.",
                                     "Red ticks on the bar: the frames where it hits.")),
                                  (T("fantasmas", "ghosts"), self.v_fant,
                                   T("El fotograma anterior y el siguiente, transparentes, para ver el movimiento.",
                                     "The previous and next frame, see-through, to read the motion."))):
                cb = ttk.Checkbutton(abajo2, text=txt, variable=var, style="Card.TCheckbutton", command=self._pintar)
                cb.pack(side="right", padx=(8, 0))
                Tooltip(cb, tip)
            self.seg_vel = nucleo.Segmented(abajo, [(0.5, "½"), (1.0, "1×"), (2.0, "2×")], 1.0,
                                            lambda v: setattr(self, "vel", v))
            self.seg_vel.pack(side="right")
            Tooltip(self.seg_vel, T("Velocidad de reproducción.", "Playback speed."))
        else:
            self.lbl_f.pack(side="left")
        self.lbl_nota = tk.Label(abajo2 if controles else abajo, text="", bg=C["card"], fg=C["warn"], font=F["small"])
        self.lbl_nota.pack(side="left", padx=6)

    # ---------------------------------------------------------------- carga
    def cargar(self, src, titulo=None):
        """src: dict(personaje, codigo, modelo=None, anm=None, frames=n, marcas_de=(personaje, codigo, anm))."""
        self.gen += 1
        gen, self.src, self.ims, self.fino, self.play = self.gen, dict(src), [], None, False
        if titulo is not None:
            self.lbl_tit.configure(text=titulo)
        self.lbl_nota.configure(text="")
        self.img.configure(image="", text="")
        self.img.foto = None
        self.lbl_carga.configure(text=T("Preparando la película…", "Preparing the film…"))
        self.pb.configure(value=0, maximum=max(1, src.get("frames") or 1))
        self.carga.place(relx=0.5, rely=0.5, anchor="center")
        prog = {}
        vista, px, seguir = self.vista, self.px, self.seguir

        def trabajo():
            raiz = pel._cache(None)
            prog["raiz"], prog["antes"] = raiz, set(os.listdir(raiz)) if os.path.isdir(raiz) else set()
            nota = ""
            try:
                rutas = pel.pelicula(src["personaje"], src["codigo"], vista, px, modelo=src.get("modelo"),
                                     anm=src.get("anm"), seguir=seguir)
            except Exception:  # noqa: BLE001  (otro esqueleto: se ve con su propio modelo)
                if src.get("modelo") is None:
                    raise
                nota = T("con su propio modelo (esqueleto distinto)", "with its own model (different skeleton)")
                rutas = pel.pelicula(src["personaje"], src["codigo"], vista, px, anm=src.get("anm"), seguir=seguir)
            from PIL import Image
            ims = [Image.open(r).convert("RGBA") for r in rutas]
            mp, mc, ma = src.get("marcas_de") or (src["personaje"], src["codigo"], src.get("anm"))
            return ims, [_fantasma(i) for i in ims], pel.marcas(mp, mc, ma), nota

        def fin(ok, res):
            if gen != self.gen or not self.winfo_exists():
                return
            self.carga.place_forget()
            if not ok:
                self.img.configure(text=T("No se pudo dibujar: %s", "Could not draw: %s") % res)
                return
            self.ims, self.fant, self.marcas, nota = res
            self.lbl_nota.configure(text=nota)
            self.f = 0.0
            self._pintar()
            if self.controles:
                self.alternar(True)

        def progreso():
            if gen != self.gen or self.ims or not self.winfo_exists():
                return
            try:
                nuevos = set(os.listdir(prog["raiz"])) - prog["antes"]
                hechos = max((len([x for x in os.listdir(os.path.join(prog["raiz"], d)) if x.endswith(".png")])
                              for d in nuevos), default=0)
                self.pb.configure(value=hechos)
                self.lbl_carga.configure(text=T("Preparando la película… %d/%d", "Preparing the film… %d/%d")
                                         % (hechos, src.get("frames") or 0))
            except (OSError, KeyError, tk.TclError):
                pass
            self.after(250, progreso)
        TRABAJO.pedir(0, trabajo, fin)
        self.after(250, progreso)

    def _vista(self, v):
        self.vista = v
        if self.src:
            self.cargar(self.src)

    # ---------------------------------------------------------------- reproduccion
    def frac(self):
        return self.f / max(1, len(self.ims) - 1) if self.ims else 0.0

    def poner_frac(self, fr):
        if self.ims:
            self.f = fr * (len(self.ims) - 1)
            self._pintar()

    def alternar(self, on=None):
        self.play = (not self.play) if on is None else on
        if self.play:
            self.fino = None
            self._tic()
        else:
            self._afinar()

    def paso(self, d):
        if not self.ims:
            return
        self.play = False
        self.f = (int(round(self.f)) + d) % len(self.ims)
        self._pintar()
        self._afinar()

    def _tic(self):
        if not self.play or not self.ims or not self.winfo_exists():
            return
        self.f = (self.f + 2 * self.vel) % len(self.ims)       # clips a 60 fps, 30 pasos por segundo
        self._pintar()
        self.after(33, self._tic)

    def _clic_barra(self, e):
        if self.ims:
            self.play = False
            self.f = max(0.0, min(1.0, e.x / max(1, self.barra.winfo_width()))) * (len(self.ims) - 1)
            self._pintar()
            self._afinar()

    def _afinar(self):
        """Pausado: el mismo fotograma a doble resolucion (fotograma_fino), si sigue pausado ahi."""
        if not self.ims or self.play or not self.src:
            return
        i, gen, src, vista, px = int(round(self.f)) % len(self.ims), self.gen, self.src, self.vista, self.px

        def trabajo():
            from PIL import Image
            r = pel.fotograma_fino(src["personaje"], src["codigo"], i, vista, px * 2, modelo=src.get("modelo"),
                                   anm=src.get("anm"), seguir=self.seguir)
            return Image.open(r).convert("RGBA").resize((px, px), Image.LANCZOS)

        def fin(ok, im):
            if ok and gen == self.gen and not self.play and int(round(self.f)) % len(self.ims) == i:
                self.fino = (i, im)
                self._pintar()
        TRABAJO.pedir(1, trabajo, fin)

    def _pintar(self):
        if not self.ims:
            return
        from PIL import ImageTk
        n = len(self.ims)
        i = int(round(self.f)) % n
        fondo = _hex_rgb(C["log_bg"])
        actual = self.fino[1] if self.fino and self.fino[0] == i else self.ims[i]
        if self.controles and self.v_fant.get():
            base = _sobre(self.fant[max(0, i - PASO_FANTASMA)], fondo)
            base.alpha_composite(self.fant[min(n - 1, i + PASO_FANTASMA)])
            base.alpha_composite(actual)
        else:
            base = _sobre(actual, fondo)
        foto = ImageTk.PhotoImage(base)
        self.img.configure(image=foto, text="")
        self.img.foto = foto
        self.lbl_f.configure(text=T("fotograma %d / %d", "frame %d / %d") % (i + 1, n))
        self._dibujar_barra()
        if self.al_mover:
            self.al_mover(self.frac())

    def _dibujar_barra(self):
        c = self.barra
        c.delete("all")
        w = max(1, c.winfo_width())
        c.create_rectangle(0, 6, w, 11, fill=C["frame"], outline="")
        if not self.ims:
            return
        n = len(self.ims)
        c.create_rectangle(0, 6, w * self.frac(), 11, fill=C["accent"], outline="")
        if not self.controles or self.v_marcas.get():
            for fr, tipo in self.marcas:
                x = w * min(fr, n - 1) / max(1, n - 1)
                c.create_rectangle(x - 1, 2, x + 1, 15, fill=C["err"] if tipo == "golpe" else C["warn"], outline="")
        x = w * self.frac()
        c.create_line(x, 0, x, 16, fill=C["text"], width=2)


def probar_mods(page, mods, lbl, pid):
    """«Probar en el juego» con kit/probar.py tal cual (arranca el juego con los mods y dice si cargaron)."""
    if page.app.selftest:
        page.app.selftest_cmds.append(("tecnica: probar", list(mods)))
        return
    page.set_status(lbl, "warn", T("Probando el juego… (el detalle sale en el registro)",
                                   "Testing the game… (details in the log)"))
    cola, env, ingles = queue.Queue(), page.env, nucleo.LANG == "en"

    def hilo():
        from kit import probar  # noqa: PLC0415
        try:
            cola.put(probar.probar(env.mods, env.game, mods, ingles, lambda t: cola.put(("log", t))))
        except Exception as ex:  # noqa: BLE001
            cola.put(("error", str(ex), ""))
    threading.Thread(target=hilo, daemon=True).start()

    def sondear():
        try:
            while True:
                m = cola.get_nowait()
                if m[0] == "log":
                    page.app.log_text(m[1] + "\n")
                    continue
                page.set_status(lbl, {"ok": "ok", "aviso": "warn"}.get(m[0], "err"), m[1] + T(
                    " Para ver la técnica: Entrenamiento con %s.", " To see the technique: Training with %s.")
                    % G.quien(pid, ingles))
                return
        except queue.Empty:
            pass
        if page.alive:
            page.after(300, sondear)
    page.after(300, sondear)


# ------------------------------------------------------------------------------------------ pagina
class GeneradorPage(Page):
    key = "biblioteca"

    def build(self):
        self.ingles = nucleo.LANG == "en"
        self.lista, self.vis, self.sel, self.tsel = None, [], None, None
        self.destino, self.tramos, self.pendiente = None, [], None
        self.tipo, self.origen, self.ult_mod = None, None, None
        self.miniaturas, self.gen_mini = {}, 0
        self.v_q = tk.StringVar(master=self, value="")
        self.v_q.trace_add("write", lambda *_a: self._programar(self._refrescar, 250))
        self.v_mezcla = tk.IntVar(master=self, value=6)
        self.v_dur = tk.IntVar(master=self, value=0)
        self.v_desde, self.v_hasta = tk.IntVar(master=self, value=0), tk.IntVar(master=self, value=0)
        self._pend = {}
        self.header(T("Generador de animaciones", "Animation generator"))
        hr = self.header_right
        self.b_probar = self._boton(hr, T("Probar en el juego", "Test in the game"), T(
            "Arranca el juego con este mod y te dice si ha cargado (la técnica la ves tú en Entrenamiento).",
            "Starts the game with this mod and tells you if it loaded (you check the technique in Training)."),
            self.probar, "Accent.TButton")
        self.b_guardar = self._boton(hr, T("Guardar como técnica", "Save as technique"), T(
            "Escribe el movimiento nuevo en un mod (con copia: «Deshacer» lo deja como estaba).",
            "Writes the new movement into a mod (with a copy: 'Undo' puts it back)."), self.guardar, "Blue.TButton")
        self.b_deshacer = self._boton(hr, T("Deshacer", "Undo"), T("Deja el mod como estaba antes de guardar.",
                                                                     "Puts the mod back as it was before saving."),
                                      self.deshacer)
        if self.adv:
            self._boton(hr, T("Guardar receta…", "Save recipe…"), T(
                "La receta en un .toml (python biblioteca_anim.py aplicar --receta … --mod …).",
                "The recipe as a .toml (python biblioteca_anim.py aplicar --receta … --mod …)."), self.guardar_receta)
        self._barra_destino()
        pie = tk.Frame(self, bg=C["bg"])
        pie.pack(side="bottom", fill="x", padx=22, pady=(2, 8))
        self.lbl_val = label(pie, "", fg=C["dim"], wrap=820)
        self.lbl_val.pack(side="left", fill="x", expand=True)
        self.lbl_estado = label(pie, T("Guardado con copia · puedes deshacer", "Saved with a copy · you can undo"),
                                fg=C["ok"], font=F["small"], wrap=420)
        self.lbl_estado.pack(side="right")
        self._linea_tiempo()
        cuerpo = tk.Frame(self, bg=C["bg"])
        cuerpo.pack(fill="both", expand=True, padx=22, pady=(0, 8))
        cuerpo.columnconfigure(1, weight=1)
        cuerpo.rowconfigure(0, weight=1)
        self._biblioteca(cuerpo)
        self.visor = Visor(cuerpo, VIS, True, T("Tu técnica", "Your technique"), self._sincronizar)
        self.visor.grid(row=0, column=1, sticky="nsew", padx=10)
        self.orig = Visor(cuerpo, ORIG, False, T("Original", "Original"), seguir=True)   # KIT-10: sigue al personaje
        self.orig.grid(row=0, column=2, sticky="nsew")
        if not self.app.selftest:
            bombear(self)

    def _boton(self, parent, texto, tip, fn, estilo=None):
        b = ttk.Button(parent, text=texto, command=fn, **({"style": estilo} if estilo else {}))
        b.pack(side="right", padx=(8, 0))
        Tooltip(b, tip)
        return b

    def _programar(self, fn, ms):
        if self._pend.get(fn.__name__):
            self.after_cancel(self._pend[fn.__name__])
        self._pend[fn.__name__] = self.after(ms, fn)

    def _nombre(self, c):
        n = G.nombre(c, self.ingles)
        return n + ("  · %#x" % c["codigo"] if self.adv else "")

    # ---------------------------------------------------------------- destino
    def _barra_destino(self):
        f = tk.Frame(self, bg=C["bg"])
        f.pack(fill="x", padx=22, pady=(0, 8))
        label(f, T("Movimiento que cambias:", "Move you change:"), font=F["bold"]).pack(side="left")
        self.cb_quien = ttk.Combobox(f, state="readonly", width=22)
        self.cb_quien.pack(side="left", padx=(8, 6))
        self.cb_quien.bind("<<ComboboxSelected>>", lambda _e: self._elegir_quien())
        self.cb_mov = ttk.Combobox(f, state="readonly", width=34)
        self.cb_mov.pack(side="left")
        self.cb_mov.bind("<<ComboboxSelected>>", lambda _e: self._elegir_mov())
        lim = label(f, "  ⓘ " + T("Qué no puede hacer aún", "What it cannot do yet"), fg=C["blue"], font=F["small"])
        lim.pack(side="left", padx=(12, 0))
        Tooltip(lim, T(*G.LIMITES) + T(" Tampoco se crea movimiento pose a pose ni se importan animaciones de Blender.",
                                      " It does not create movement pose by pose or import Blender animations either."))
        self.lbl_lim = label(f, T("Golpes y daño: los del movimiento que cambias.",
                                  "Hits and damage: those of the move you change."), fg=C["dim"], font=F["small"])
        self.lbl_lim.pack(side="left", padx=(8, 0))

    def _movs(self, pid):
        return sorted((c for c in self.lista if c["personaje"] == pid),
                      key=lambda c: ({"definitiva": 0, "tecnica": 1}.get(c["tipo"], 2), not c["oficial"], c["codigo"]))

    def _elegir_quien(self, codigo=None):
        pid = self.quienes[self.cb_quien.current()]
        self.movs = self._movs(pid)
        self.cb_mov.configure(values=["%s · %s" % (self._nombre(c), G.tipo_txt(c["tipo"], self.ingles))
                                      for c in self.movs])
        k = next((i for i, c in enumerate(self.movs) if c["codigo"] == codigo), 0)
        self.cb_mov.current(k)
        self._elegir_mov()

    def _elegir_mov(self):
        c = self.movs[self.cb_mov.current()]
        self.destino = c
        self.orig.cargar(dict(personaje=c["personaje"], codigo=c["codigo"], frames=c["frames"]),
                         T("Original · %s (lo que se sustituye)", "Original · %s (what gets replaced)")
                         % G.quien(c["personaje"], self.ingles))
        if not self.tramos:
            self._ver(c)
        self._redibujar()

    def focus_item(self, personaje=None, codigo=None, **_kw):
        """Desde «Nueva técnica»: abre con ese personaje y esa técnica como destino."""
        self.pendiente = (personaje, codigo)
        if self.lista is not None:
            self._aplicar_pendiente()

    def _aplicar_pendiente(self):
        if not self.pendiente or self.pendiente[0] not in self.quienes:
            return
        pid, cod = self.pendiente
        self.pendiente = None
        self.cb_quien.current(self.quienes.index(pid))
        self.tramos, self.tsel = [], None
        self._elegir_quien(cod)

    # ---------------------------------------------------------------- biblioteca
    def _biblioteca(self, cuerpo):
        card = tk.Frame(cuerpo, bg=C["card"], highlightthickness=1, highlightbackground=C["line"], width=330)
        card.grid(row=0, column=0, sticky="nsew")
        card.grid_propagate(False)
        card.pack_propagate(False)
        b = tk.Frame(card, bg=C["card"])
        b.pack(fill="both", expand=True, padx=10, pady=10)
        fb = tk.Frame(b, bg=C["card"])
        fb.pack(fill="x")
        label(fb, "🔍", fg=C["dim"], bg=C["card"]).pack(side="left", padx=(0, 4))
        ent = ttk.Entry(fb, textvariable=self.v_q)
        ent.pack(side="left", fill="x", expand=True)
        Tooltip(ent, T("Busca por nombre, personaje o tipo: patada, onda, agarre…",
                       "Search by name, character or type: kick, wave, throw…"))
        self.chips_origen = tk.Frame(b, bg=C["card"])
        self.chips_origen.pack(fill="x")
        chips = tk.Frame(b, bg=C["card"])
        chips.pack(fill="x", pady=(6, 0))
        self.chip = {}
        for i, (k, es, en) in enumerate([(None, "Todos", "All")] + [(k, es + "s", en + "s") for k, es, en in G.TIPOS]):
            lb = tk.Label(chips, text=T(es, en), font=F["small"], padx=7, pady=2,
                          cursor="hand2")
            lb.grid(row=i // 3, column=i % 3, sticky="ew", padx=(0, 4), pady=(0, 4))
            lb.bind("<Button-1>", lambda _e, t=k: self._tipo(t))
            self.chip[k] = lb
        fila = tk.Frame(b, bg=C["card"])
        fila.pack(fill="x", pady=(0, 6))
        self.cb_filtro = ttk.Combobox(fila, state="readonly", width=24)
        self.cb_filtro.pack(side="left", fill="x", expand=True)
        self.cb_filtro.bind("<<ComboboxSelected>>", lambda _e: self._refrescar())
        Tooltip(self.cb_filtro, T("Solo los clips de un personaje.", "Only one character's clips."))
        self.sf = nucleo.ScrollFrame(b, bg=C["card"])
        self.sf.pack(fill="both", expand=True)
        for c in range(3):
            self.sf.inner.columnconfigure(c, weight=1, uniform="mini")
        self.lbl_bib = label(b, T("Cargando la biblioteca del juego…", "Loading the game library…"), fg=C["dim"],
                             font=F["small"], bg=C["card"], wrap=270)
        self.lbl_bib.pack(fill="x", pady=(4, 0))
        self.b_anadir = ttk.Button(b, text="+  " + T("Añadir a la línea de tiempo", "Add to the timeline"),
                                   style="Small.TButton", command=self.anadir)
        self.b_anadir.pack(fill="x", pady=(4, 0))
        Tooltip(self.b_anadir, T("Pone el clip elegido al final de la pista de Movimiento (también con doble clic).",
                                 "Puts the picked clip at the end of the Movement track (double-click works too)."))
        self._tipo(None, refrescar=False)

    def on_show(self):
        if self.lista is None and not self.app.selftest:
            biblioteca(self._cargada)

    def _cargada(self, ok, lista):
        if not ok:
            self.lbl_bib.configure(text=T("No se pudo leer el juego: %s", "Could not read the game: %s") % lista,
                                   fg=C["err"])
            return
        self.lista = lista
        self.quienes = sorted({c["personaje"] for c in lista})
        nombres = [G.quien(p, self.ingles) for p in self.quienes]
        self.cb_quien.configure(values=nombres)
        self.cb_filtro.configure(values=[T("Todos los personajes", "All characters")] + nombres)
        self.cb_filtro.current(0)
        origenes = sorted({c["origen"] for c in lista})
        if len(origenes) > 1:       # B3/B1/B2/SB/IW: solo si hay clips de mas de un juego (no se pintan vacios)
            for o in [None] + origenes:
                lb = tk.Label(self.chips_origen, text=o or T("Todos", "All"), font=F["small"], padx=7, pady=2,
                              bg=C["frame"], fg=C["dim"], cursor="hand2")
                lb.pack(side="left", padx=(0, 4), pady=(6, 0))
                lb.bind("<Button-1>", lambda _e, x=o: (setattr(self, "origen", x), self._refrescar()))
        if self.pendiente is None:
            self.pendiente = (0, next((c["codigo"] for c in self._movs(0) if c["oficial"] == "Dragon Fist"), None))
        self._aplicar_pendiente()
        self._refrescar()

    def _tipo(self, t, refrescar=True):
        self.tipo = t
        for k, lb in self.chip.items():
            on = k == t
            lb.configure(bg=C["accent"] if on else C["frame"], fg=C["on_accent"] if on else C["dim"])
        if refrescar:
            self._refrescar()

    def _refrescar(self):
        if self.lista is None:
            return
        k = self.cb_filtro.current()
        pid = self.quienes[k - 1] if k > 0 else None
        self.vis = G.buscar(self.lista, self.v_q.get(), pid, self.tipo, self.origen, self.ingles)
        self.vis.sort(key=lambda c: (not c["oficial"], c["personaje"], c["codigo"]))
        self.gen_mini += 1
        for w in self.sf.inner.winfo_children():
            w.destroy()
        self.casillas = {}
        for i, c in enumerate(self.vis[:MAX_TILES]):
            self._casilla(i, c)
        self.sf.top()
        resto = len(self.vis) - MAX_TILES
        self.lbl_bib.configure(fg=C["dim"], text=(T("%d clips", "%d clips") % len(self.vis)) + (
            T(" · se ven %d: afina la búsqueda", " · %d shown: refine the search") % MAX_TILES if resto > 0 else ""))

    def _casilla(self, i, c):
        sel = self.sel is not None and (self.sel["personaje"], self.sel["codigo"]) == (c["personaje"], c["codigo"])
        f = tk.Frame(self.sf.inner, bg=C["card_hi"], highlightthickness=1,
                     highlightbackground=C["accent"] if sel else C["line"])
        f.grid(row=i // 3, column=i % 3, sticky="nsew", padx=2, pady=2)
        if not hasattr(self, "_vacia"):
            self._vacia = tk.PhotoImage(master=self, width=MINI, height=MINI)
        img = tk.Label(f, bg=C["log_bg"], image=self._vacia)
        img.pack(fill="x")
        tk.Label(f, text=self._nombre(c), bg=C["card_hi"], fg=C["text"], font=F["small"], anchor="w",
                 width=1).pack(fill="x", padx=3)
        tk.Label(f, text=G.quien(c["personaje"], self.ingles), bg=C["card_hi"], fg=C["dim"], font=F["small"],
                 anchor="w", width=1).pack(fill="x", padx=3, pady=(0, 2))
        clave = (c["personaje"], c["codigo"])
        self.casillas[clave] = (f, img)
        make_clickable(f, lambda x=c: self._elegir(x), C["card_hi"], C["frame"])
        for w in (f,) + tuple(f.winfo_children()):
            w.bind("<Double-Button-1>", lambda _e, x=c: (self._elegir(x), self.anadir()), add="+")
        if clave in self.miniaturas:
            img.configure(image=self.miniaturas[clave])
            return
        if self.app.selftest:
            return
        gen = self.gen_mini

        def trabajo():
            if gen != self.gen_mini:
                return None
            from PIL import Image
            r = pel.fotograma_fino(c["personaje"], c["codigo"], c["frames"] // 2, "3/4", MINI, seguir=True)
            return _sobre(Image.open(r), _hex_rgb(C["log_bg"]), (MINI, MINI))

        def fin(ok, im):
            if not ok or im is None:
                return
            from PIL import ImageTk
            self.miniaturas[clave] = ImageTk.PhotoImage(im)
            if clave in self.casillas and self.casillas[clave][1].winfo_exists():
                self.casillas[clave][1].configure(image=self.miniaturas[clave])
        TRABAJO.pedir(2, trabajo, fin)

    def _elegir(self, c):
        viejo = self.sel
        self.sel = c
        for clave in [x for x in (viejo, c) if x]:
            k = (clave["personaje"], clave["codigo"])
            if k in self.casillas:
                self.casillas[k][0].configure(highlightbackground=C["accent"] if clave is c else C["line"])
        self._ver(c)

    def _ver(self, c, titulo=None):
        """Visor: el clip `c` con el MODELO del personaje destino (si el esqueleto encaja)."""
        d = self.destino or c
        modelo = None if c["personaje"] == d["personaje"] else G.ba._persona(d["personaje"])["modelo"]
        tit = titulo or (T("Tu técnica · %s", "Your technique · %s") % G.quien(d["personaje"], self.ingles) +
                         "  ·  " + self._nombre(c) + (T(" (de %s)", " (from %s)") % G.quien(c["personaje"], self.ingles)
                                                      if modelo is not None else ""))
        self.visor.cargar(dict(personaje=c["personaje"], codigo=c["codigo"], modelo=modelo, frames=c["frames"]), tit)

    def _sincronizar(self, fr):
        self.orig.poner_frac(fr)
        self._cabezal(fr)

    # ---------------------------------------------------------------- linea de tiempo
    def _linea_tiempo(self):
        card = tk.Frame(self, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
        card.pack(side="bottom", fill="x", padx=22, pady=(0, 6))
        self.lt = tk.Canvas(card, height=128, bg=C["card"], highlightthickness=0)
        self.lt.pack(fill="x", padx=10, pady=(6, 0))
        self.lt.bind("<Configure>", lambda _e: self._redibujar(validar=False))
        self.lt.bind("<Button-1>", self._clic_lt)
        ctl = tk.Frame(card, bg=C["card"])
        ctl.pack(fill="x", padx=10, pady=(4, 8))
        self.lbl_tramo = label(ctl, T("Añade clips desde la biblioteca.", "Add clips from the library."),
                               font=F["bold_sm"], bg=C["card"])
        self.lbl_tramo.pack(side="left")
        self.tramo_ctl = tk.Frame(ctl, bg=C["card"])
        self.tramo_ctl.pack(side="left", padx=(8, 0))
        for txt, var, tip in ((T("desde", "from"), self.v_desde, T("Primer fotograma del tramo.",
                                                                 "First frame of the segment.")),
                              (T("hasta", "to"), self.v_hasta, T("Último fotograma del tramo.",
                                                               "Last frame of the segment."))):
            label(self.tramo_ctl, txt, fg=C["dim"], font=F["small"], bg=C["card"]).pack(side="left", padx=(6, 2))
            sp = ttk.Spinbox(self.tramo_ctl, from_=0, to=999, width=4, textvariable=var, command=self._recorte)
            sp.pack(side="left")
            sp.bind("<Return>", lambda _e: self._recorte())
            sp.bind("<FocusOut>", lambda _e: self._recorte())
            Tooltip(sp, tip)
        for txt, tip, fn in (("◀", T("Mueve el tramo antes.", "Moves the segment earlier."), lambda: self._mover(-1)),
                             ("▶", T("Mueve el tramo después.", "Moves the segment later."), lambda: self._mover(1)),
                             (T("Quitar", "Remove"), T("Quita el tramo de la línea.", "Removes the segment."),
                              self._quitar)):
            b = ttk.Button(self.tramo_ctl, text=txt, style="Small.TButton", command=fn, width=len(txt) + 1)
            b.pack(side="left", padx=(6, 0))
            Tooltip(b, tip)
        b = ttk.Button(ctl, text=T("Ver el resultado", "See the result"), style="Small.TButton", command=self.ver_resultado)
        b.pack(side="right")
        self.lbl_total = label(ctl, "", fg=C["dim"], font=F["small"], bg=C["card"])
        self.lbl_total.pack(side="right", padx=(0, 12))
        Tooltip(b, T("Dibuja el movimiento final (empalmado y con la duración elegida) con el personaje.",
                     "Draws the final movement (joined and with the chosen length) with the character."))
        for txt, var, de, a, tip in ((T("duración total", "total length"), self.v_dur, 0, 600,
                                      T("Fotogramas del resultado (60 = 1 s). 0 = la suma de los tramos.",
                                        "Frames of the result (60 = 1 s). 0 = the sum of the segments.")),
                                     (T("mezcla", "blend"), self.v_mezcla, 0, 30,
                                      T("Fotogramas en los que un tramo se funde con el siguiente (naranja).",
                                        "Frames where one segment blends into the next (orange)."))):
            sp = ttk.Spinbox(ctl, from_=de, to=a, width=4, textvariable=var, command=self._redibujar)
            sp.pack(side="right", padx=(2, 10))
            sp.bind("<Return>", lambda _e: self._redibujar())
            sp.bind("<FocusOut>", lambda _e: self._redibujar())
            Tooltip(sp, tip)
            label(ctl, txt, fg=C["dim"], font=F["small"], bg=C["card"]).pack(side="right")

    def _int(self, var, defecto=0):
        try:
            return int(var.get())
        except (tk.TclError, ValueError):
            return defecto

    def _geom(self):
        """[(x0, x1, fotograma de inicio)] de cada tramo en el lienzo, y px por fotograma."""
        w = max(100, self.lt.winfo_width()) - 110
        mezcla = self._int(self.v_mezcla)
        total = max(1, G.duracion_natural(self.tramos, mezcla))
        esc = w / total
        out, pos = [], 0
        for k, t in enumerate(self.tramos):
            if k:
                pos -= max(0, min(mezcla, pos, G.largo(t)))
            out.append((110 + pos * esc, 110 + (pos + G.largo(t)) * esc, pos))
            pos += G.largo(t)
        return out, esc, total

    def _redibujar(self, validar=True):
        c = self.lt
        c.delete("all")
        w = max(100, c.winfo_width())
        filas = ((T("Movimiento", "Movement"), 22, 62), (T("Cámara", "Camera"), 68, 94), (T("Efectos", "Effects"), 100, 126))
        for txt, y0, y1 in filas:
            c.create_text(4, (y0 + y1) / 2, text=txt, anchor="w", fill=C["text"], font=F["bold_sm"])
            c.create_rectangle(110, y0, w, y1, fill=C["log_bg"], outline="")
        geo, esc, total = self._geom()
        nat = total if self.tramos else 0
        dur = self._int(self.v_dur) or nat
        for s in range(0, int(nat / 60) + 2):           # regla en segundos (60 fps)
            x = 110 + s * 60 * esc
            if x < w:
                c.create_text(x + 2, 9, text="%d s" % s, anchor="w", fill=C["dim"], font=F["small"])
        colores = ("#2E4A75", "#2F6040", "#5A3B6E", "#6B4A1E")
        for k, ((x0, x1, _p), t) in enumerate(zip(geo, self.tramos)):
            c.create_rectangle(x0, 22, x1, 62, fill=colores[k % len(colores)], outline=C["accent"] if k == self.tsel
                               else "", width=2)
            tx = (min(x1, geo[k - 1][1]) if k else x0) + 6
            c.create_text(tx, 32, text=t["nombre"], anchor="w", fill=C["text"], font=F["bold_sm"])
            c.create_text(tx, 50, text="%d-%d · %d f" % (t["desde"], t["hasta"], G.largo(t)), anchor="w",
                          fill=C["dim"], font=F["small"])
            if k:
                c.create_rectangle(x0, 22, min(x1, geo[k - 1][1]), 62, fill=C["accent"], outline="")
        c.create_text(116, 81, anchor="w", fill=C["dim"], font=F["small"], text=T(
            "La de la técnica, sin cambios: se cambia en «Nueva técnica › Cámara» o en el Studio.",
            "The technique's own, unchanged: change it in 'New technique › Camera' or in the Studio."))
        c.create_text(116, 113, anchor="w", fill=C["dim"], font=F["small"], text=T(
            "Color de aura y ki: en «Nueva técnica › Color» (vale para todo el personaje, no por tramo).",
            "Aura and ki colour: in 'New technique › Colour' (it applies to the whole character, not per segment)."))
        self.lbl_total.configure(text=T("resultado: %d fotogramas (%.1f s)", "result: %d frames (%.1f s)")
                                 % (dur, dur / 60.0) if self.tramos else "", fg=C["accent"] if dur != nat else C["dim"])
        self._ctl_tramo()
        if validar:
            self._programar(self._validar, 400)

    def _cabezal(self, fr):
        c = self.lt
        c.delete("cabezal")
        if self.tramos and getattr(self, "viendo_resultado", False):
            w = max(100, c.winfo_width())
            x = 110 + fr * (w - 110)
            c.create_line(x, 16, x, 128, fill=C["accent"], width=2, tags="cabezal")

    def _clic_lt(self, e):
        for k, (x0, x1, _p) in enumerate(self._geom()[0]):
            if x0 <= e.x <= x1 and 22 <= e.y <= 62:
                self.tsel = k
                t = self.tramos[k]
                self._redibujar(validar=False)
                self._ver(t)
                return

    def _ctl_tramo(self):
        if self.tsel is None or self.tsel >= len(self.tramos):
            self.tsel = None
            self.lbl_tramo.configure(text=T("Pista de Movimiento: añade clips desde la biblioteca y pulsa un tramo para "
                                            "recortarlo.", "Movement track: add clips from the library and click a "
                                            "segment to trim it.") if not self.tramos else
                                     T("Pulsa un tramo para recortarlo o moverlo.", "Click a segment to trim or move it."))
            self.tramo_ctl.pack_forget()
            return
        t = self.tramos[self.tsel]
        self.lbl_tramo.configure(text=T("Tramo %d:", "Segment %d:") % (self.tsel + 1) + " " + t["nombre"])
        self.tramo_ctl.pack(side="left", padx=(8, 0))
        self.v_desde.set(t["desde"])
        self.v_hasta.set(t["hasta"])

    def anadir(self):
        c = self.sel
        if c is None:
            self.lbl_val.configure(text=T("Elige primero un clip de la biblioteca.", "Pick a library clip first."),
                                   fg=C["warn"])
            return
        nom = self._nombre(c) + ("" if self.destino and c["personaje"] == self.destino["personaje"] else
                                 " (%s)" % G.quien(c["personaje"], self.ingles))
        self.tramos.append(dict(personaje=c["personaje"], codigo=c["codigo"], frames=c["frames"], desde=0,
                                hasta=c["frames"] - 1, nombre=nom))
        self.tsel = len(self.tramos) - 1
        self.viendo_resultado = False
        self._redibujar()

    def _recorte(self):
        if self.tsel is None:
            return
        t = self.tramos[self.tsel]
        a = max(0, min(self._int(self.v_desde, t["desde"]), t["frames"] - 1))
        b = max(0, min(self._int(self.v_hasta, t["hasta"]), t["frames"] - 1))
        if (a, b) != (t["desde"], t["hasta"]):
            t["desde"], t["hasta"] = a, b
            self._redibujar()

    def _mover(self, d):
        k = self.tsel
        if k is None or not 0 <= k + d < len(self.tramos):
            return
        self.tramos[k], self.tramos[k + d] = self.tramos[k + d], self.tramos[k]
        self.tsel = k + d
        self._redibujar()

    def _quitar(self):
        if self.tsel is not None:
            del self.tramos[self.tsel]
            self.tsel = None
            self._redibujar()

    # ---------------------------------------------------------------- receta, validacion y guardado
    def receta(self):
        if not self.destino or not self.tramos:
            return None
        return G.receta(self.destino, self.tramos, self._int(self.v_mezcla), self._int(self.v_dur))

    def _validar(self):
        r = self.receta()
        if r is None:
            self.lbl_val.configure(text=T("Añade al menos un clip a la pista de Movimiento.",
                                          "Add at least one clip to the Movement track."), fg=C["dim"])
            return
        if self.app.selftest:
            return
        clave = repr(r)
        self._val_clave = clave
        ingles = self.ingles

        def fin(ok, av):
            if clave != self._val_clave or not self.winfo_exists():
                return
            if not ok:
                av = [("err", str(av))]
            if not av:
                self.lbl_val.configure(text=T("✓ huesos compatibles · ✓ duración · ✓ los golpes caen dentro del clip",
                                              "✓ bones match · ✓ length · ✓ hits land inside the clip"), fg=C["ok"])
            else:
                peor = "err" if any(n == "err" for n, _t in av) else "warn"
                self.lbl_val.configure(text="  ".join(("✗ " if n == "err" else "⚠ ") + t for n, t in av),
                                       fg=C[peor])
            self.b_guardar.configure(state="disabled" if any(n == "err" for n, _t in av) else "normal")
        TRABAJO.pedir(0, lambda: G.avisos(r, ingles=ingles), fin)

    def ver_resultado(self):
        r = self.receta()
        if r is None:
            self._validar()
            return
        if self.app.selftest:
            self.app.selftest_cmds.append(("generador: ver resultado", [repr(r)]))
            return
        carpeta = os.path.join(vistas.cache_dir(), "generador_resultado", time.strftime("%Y%m%d-%H%M%S"))
        d = self.destino
        n = self._int(self.v_dur) or G.duracion_natural(self.tramos, self._int(self.v_mezcla))

        def trabajo():
            return G.ba.aplicar_receta(r, carpeta)["ruta"]

        def fin(ok, ruta):
            if not ok:
                self.lbl_val.configure(text="✗ " + str(ruta), fg=C["err"])
                return
            self.viendo_resultado = True
            self.visor.cargar(dict(personaje=d["personaje"], codigo=d["codigo"], anm=ruta, frames=n),
                              T("Resultado · %s", "Result · %s") % G.quien(d["personaje"], self.ingles) + "  ·  " +
                              self._nombre(d))
        TRABAJO.pedir(0, trabajo, fin)

    def _mod(self):
        return os.path.join(self.env.mods, G.mod_de(self.destino))

    def guardar(self):
        r = self.receta()
        if r is None:
            self._validar()
            return
        mod = self._mod()
        if self.app.selftest:
            self.app.selftest_cmds.append(("generador: guardar", [mod, repr(r)]))
            return
        self.b_guardar.configure(state="disabled")
        self.set_status(self.lbl_estado, "warn", T("Guardando…", "Saving…"))
        env = self.env

        def fin(ok, res):
            self.b_guardar.configure(state="normal")
            if not ok:
                self.set_status(self.lbl_estado, "err", T("No se pudo guardar: %s", "Could not save: %s") % res)
                return
            self.ult_mod = os.path.basename(mod)
            self.app.log_text("%s -> %s\n" % (res["informe"], res["ruta"]), "ok")
            self.set_status(self.lbl_estado, "ok", T("Guardado en tus mods («%s») · puedes deshacer",
                                                     "Saved in your mods ('%s') · you can undo") % self.ult_mod)
        TRABAJO.pedir(0, lambda: G.guardar(env, r, mod, nucleo.anotar_indice), fin)

    def deshacer(self):
        try:
            n = len(nucleo.deshacer_ultimo(self.env))
        except OSError as ex:
            self.set_status(self.lbl_estado, "err", str(ex))
            return
        self.set_status(self.lbl_estado, "ok" if n else "warn", T(
            "Deshecho: el mod vuelve a como estaba (la copia queda en modkit_backups).",
            "Undone: the mod is back as it was (the copy stays in modkit_backups).") if n else
                        T("No había nada que deshacer.", "Nothing to undo."))

    def probar(self):
        mods = [self.ult_mod] if self.ult_mod else ([G.mod_de(self.destino)] if self.destino else [])
        if not mods or not os.path.isdir(os.path.join(self.env.mods, mods[0])):
            self.set_status(self.lbl_estado, "warn", T("Primero «Guardar como técnica».", "First 'Save as technique'."))
            return
        probar_mods(self, mods, self.lbl_estado, self.destino["personaje"])

    def guardar_receta(self):
        r = self.receta()
        if r is None:
            self._validar()
            return
        from tkinter import filedialog
        ruta = filedialog.asksaveasfilename(defaultextension=".toml", initialfile="receta.toml",
                                            filetypes=[(T("Receta", "Recipe"), "*.toml")])
        if ruta:
            G.receta_toml(r, ruta)
            self.set_status(self.lbl_estado, "ok", T("Receta guardada: %s", "Recipe saved: %s") % ruta)
