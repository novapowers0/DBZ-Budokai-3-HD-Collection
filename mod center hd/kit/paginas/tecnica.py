# -*- coding: utf-8 -*-
"""«Nueva técnica» (KIT-09, maqueta studies 5/ENTREGAS/KIT_VISUAL/05_tecnica.png): a quién → qué técnica (por su nombre
oficial) → pestañas Movimiento (abre el generador), Cámara (plantillas del Studio con vista previa y «Abrir en el
Studio»), Color de aura y ki (colores.py) y Nombre (cápsula). Reutiliza el Studio (studio_core) y colores.py; la logica
sin interfaz esta en kit/generador.py. Se registra en modkit_gui con la clave de la tarjeta (obra_tecnica)."""
from __future__ import annotations

import functools
import math
import os

from kit import generador as G
from kit import nucleo, vistas
from kit.nucleo import C, F, Page, T, Tooltip, label, make_clickable, tk, ttk
from kit.paginas.generador import TRABAJO, Visor, bombear, probar_mods, _hex_rgb, _sobre

PREV = 300
N_CAM = 120
# (clave, es, en, descripcion es, en): las plantillas del Studio (studio_core.tpl_*) con sus valores por defecto
ESTILOS = [("orbita", "Órbita", "Orbit", "gira alrededor del luchador", "circles around the fighter"),
           ("acercamiento", "Acercamiento", "Zoom in", "entra hasta la cara", "moves in to the face"),
           ("travelling", "Travelling", "Tracking", "acompaña el golpe de lado", "follows the hit sideways"),
           ("temblor", "Temblor", "Shake", "sacude al impactar", "shakes on impact"),
           ("giro", "Giro", "Roll", "la imagen se inclina", "the picture tilts"),
           ("original", "La del original", "The original one", "la que trae la técnica", "the one the technique has")]
PESTANAS = (("mov", "Movimiento", "Movement"), ("cam", "Cámara", "Camera"),
            ("color", "Color de aura y ki", "Aura and ki colour"), ("nombre", "Nombre", "Name"))


@functools.lru_cache(None)
def clip_estilo(clave, n=N_CAM):
    """Clip de camara de la plantilla del Studio (studio_core) con los valores por defecto del Studio, o None."""
    sc = G.sc
    obj, ojo = (0.0, 10.5, 0.0), (0.0, 12.0, 40.0)
    fijo = sc.tpl_travelling(n, ojo, ojo, obj, obj, 38, 38)
    if clave == "orbita":
        return sc.tpl_orbita(n, obj, 40, 6, 0, 120, 38)
    if clave == "acercamiento":
        return sc.tpl_travelling(n, ojo, [o + (e - o) * 0.4 for e, o in zip(ojo, obj)], obj, (0, 12.5, 0), 38, 30)
    if clave == "travelling":
        return sc.tpl_travelling(n, (-30, 12, 32), (30, 12, 32), (-6, 10.5, 0), (6, 10.5, 0), 38, 38)
    if clave == "temblor":
        return sc.tpl_temblor(fijo, 0.8, 9, n // 3, n - 1)
    if clave == "giro":
        return sc.tpl_giro(fijo, 0, 25)
    return None


class TecnicaPage(Page):
    key = "obra_tecnica"

    def build(self):
        self.ingles = nucleo.LANG == "en"
        self.pid, self.tec, self.pest, self.estilo = None, None, "mov", "orbita"
        self.aura, self.ki, self.fotos, self.f_cam = None, None, {}, 0
        self.header(T("Nueva técnica", "New technique"),
                    T("Elige a quién y qué técnica; cambia su movimiento, su cámara y su color.",
                      "Pick who and which technique; change its movement, its camera and its colour."))
        chips = tk.Frame(self, bg=C["bg"])
        chips.pack(fill="x", padx=22, pady=(0, 8))
        self.chips = []
        for es, en in (("¿A quién?", "Who?"), ("¿Qué técnica?", "Which technique?"),
                       ("Movimiento, cámara y color", "Movement, camera and colour"), ("Probar", "Test")):
            lb = tk.Label(chips, text="", font=F["bold_sm"], padx=12, pady=4)
            lb.pack(side="left", padx=(0, 8))
            self.chips.append((lb, T(es, en)))
        pie = tk.Frame(self, bg=C["bg"])
        pie.pack(side="bottom", fill="x", padx=22, pady=(4, 10))
        self.lbl_estado = label(pie, "", wrap=640)
        self.lbl_estado.pack(side="left", fill="x", expand=True)
        b = ttk.Button(pie, text=T("Probar en el juego", "Test in the game") + "  →", style="Accent.TButton",
                       command=self.probar)
        b.pack(side="right")
        Tooltip(b, T("Arranca el juego con los mods de esta técnica y te dice si han cargado.",
                     "Starts the game with this technique's mods and tells you if they loaded."))
        b = ttk.Button(pie, text=T("Abrir el generador de animaciones", "Open the animation generator"),
                       style="Blue.TButton", command=self.generador)
        b.pack(side="right", padx=8)
        Tooltip(b, T("Cambia el movimiento de esta técnica con clips del juego.",
                     "Change this technique's movement with game clips."))
        cuerpo = tk.Frame(self, bg=C["bg"])
        cuerpo.pack(fill="both", expand=True, padx=22)
        self._izquierda(cuerpo)
        centro = tk.Frame(cuerpo, bg=C["bg"])
        centro.pack(side="left", fill="both", expand=True, padx=(14, 0))
        tabs = tk.Frame(centro, bg=C["bg"])
        tabs.pack(fill="x")
        self.tabs = {}
        for k, es, en in PESTANAS:
            lb = tk.Label(tabs, text=T(es, en), font=F["bold"], padx=16, pady=7, cursor="hand2")
            lb.pack(side="left", padx=(0, 2))
            lb.bind("<Button-1>", lambda _e, x=k: self.pestana(x))
            self.tabs[k] = lb
        self.contenido = tk.Frame(centro, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
        self.contenido.pack(fill="both", expand=True)
        if not self.app.selftest:
            bombear(self)
        self.elegir_quien(0)

    # ---------------------------------------------------------------- 1 y 2: a quien, que tecnica
    def _izquierda(self, cuerpo):
        card = tk.Frame(cuerpo, bg=C["card"], highlightthickness=1, highlightbackground=C["line"], width=270)
        card.pack(side="left", fill="y")
        card.pack_propagate(False)
        b = tk.Frame(card, bg=C["card"])
        b.pack(fill="both", expand=True, padx=12, pady=12)
        label(b, T("¿A quién?", "Who?"), font=F["h3"], bg=C["card"]).pack(fill="x")
        ids = {p["id"] for p in G.ba.personajes()}
        self.quienes = [i for i, _es, _en in nucleo.DONORS if i in ids]
        self.cb_quien = ttk.Combobox(b, state="readonly", values=[G.quien(i, self.ingles) for i in self.quienes])
        self.cb_quien.pack(fill="x", pady=(4, 12))
        self.cb_quien.bind("<<ComboboxSelected>>", lambda _e: self.elegir_quien(self.quienes[self.cb_quien.current()]))
        Tooltip(self.cb_quien, T("El personaje del juego cuya técnica cambias.", "The game character whose technique "
                                 "you change."))
        label(b, T("¿Qué técnica?", "Which technique?"), font=F["h3"], bg=C["card"]).pack(fill="x")
        self.lista = tk.Frame(b, bg=C["card"])
        self.lista.pack(fill="both", expand=True, pady=(4, 0))
        label(b, T("Nombres oficiales de las cápsulas. Las técnicas sin cápsula (golpes, agarres) se eligen en el "
                   "generador.", "Official capsule names. Moves without a capsule (strikes, throws) are picked in the "
                   "generator."), fg=C["dim"], font=F["small"], bg=C["card"], wrap=240).pack(fill="x")

    def elegir_quien(self, pid):
        self.pid = pid
        self.cb_quien.current(self.quienes.index(pid))
        for w in self.lista.winfo_children():
            w.destroy()
        try:
            tec = G.tecnicas(pid)
        except Exception:  # noqa: BLE001  (sin juego: lista vacia)
            tec = {}
        vistos, self.tecs = set(), []
        for code in sorted(tec):           # una fila por capsula: su primer codigo (el de suelo)
            cap, nom, tipo = tec[code]
            if cap not in vistos and code < 0x300:
                vistos.add(cap)
                self.tecs.append(dict(codigo=code, capsula=cap, nombre=nom or G.tipo_txt(tipo, self.ingles) +
                                      " %d" % cap, tipo=tipo))
        self.tecs.sort(key=lambda t: (t["tipo"] == "definitiva", t["codigo"]))
        self.filas = []
        for t in self.tecs:
            f = tk.Frame(self.lista, bg=C["card_hi"], highlightthickness=1, highlightbackground=C["line"])
            f.pack(fill="x", pady=3)
            tk.Label(f, text=t["nombre"] + ("  · %#x" % t["codigo"] if self.adv else ""), bg=C["card_hi"],
                     fg=C["text"], font=F["bold"], anchor="w").pack(side="left", padx=10, pady=7)
            tk.Label(f, text=G.tipo_txt(t["tipo"], self.ingles).lower(), bg=C["card_hi"], fg=C["dim"],
                     font=F["small"]).pack(side="right", padx=10)
            make_clickable(f, lambda x=t: self.elegir_tecnica(x), C["card_hi"], C["frame"])
            self.filas.append((f, t))
        if not self.tecs:
            label(self.lista, T("No encuentro sus técnicas (¿está el juego?).", "I cannot find its techniques (is the "
                                "game there?)."), fg=C["warn"], bg=C["card"], wrap=240).pack(fill="x")
        self.elegir_tecnica(self.tecs[0] if self.tecs else None)

    def elegir_tecnica(self, t):
        self.tec = t
        for f, x in self.filas:
            f.configure(highlightbackground=C["accent"] if x is t else C["line"])
        for k, (lb, txt) in enumerate(self.chips):
            hecho = (k == 0 and self.pid is not None) or (k == 1 and t is not None)
            actual = k == 2 and t is not None
            lb.configure(text=("✓ " if hecho else "") + "%d · %s" % (k + 1, txt),
                         bg=C["accent"] if actual else C["frame"],
                         fg=C["on_accent"] if actual else (C["ok"] if hecho else C["dim"]))
        self.pestana(self.pest)

    # ---------------------------------------------------------------- pestañas
    def pestana(self, k):
        self.pest = k
        for x, lb in self.tabs.items():
            lb.configure(bg=C["accent"] if x == k else C["frame"], fg=C["on_accent"] if x == k else C["text"])
        for w in self.contenido.winfo_children():
            w.destroy()
        if self.tec is None:
            label(self.contenido, T("Elige una técnica a la izquierda.", "Pick a technique on the left."), fg=C["dim"],
                  bg=C["card"]).pack(padx=16, pady=16, anchor="w")
            return
        {"mov": self._mov, "cam": self._cam, "color": self._color, "nombre": self._nombre}[k]()

    def _titulo(self, parent, extra=""):
        label(parent, "%s · %s%s" % (G.quien(self.pid, self.ingles), self.tec["nombre"], extra), font=F["h3"],
              bg=C["card"]).pack(fill="x", padx=16, pady=(12, 6))

    def _mov(self):
        self._titulo(self.contenido)
        fila = tk.Frame(self.contenido, bg=C["card"])
        fila.pack(fill="both", expand=True, padx=16, pady=(0, 12))
        v = Visor(fila, PREV, True, T("Así es hoy", "How it is today"))
        v.pack(side="left", fill="y")
        if not self.app.selftest:
            v.cargar(dict(personaje=self.pid, codigo=self.tec["codigo"], frames=self._frames()))
        der = tk.Frame(fila, bg=C["card"])
        der.pack(side="left", fill="both", expand=True, padx=(16, 0))
        label(der, T("Cambiar el movimiento", "Change the movement"), font=F["bold"], bg=C["card"]).pack(fill="x")
        label(der, T("En el generador eliges clips de cualquier personaje del juego, los recortas, los empalmas con "
                     "una mezcla suave y ajustas la duración. Lo ves con %s antes de guardar." % G.quien(self.pid),
                     "In the generator you pick clips from any game character, trim them, join them with a smooth "
                     "blend and set the length. You see it with %s before saving." % G.quien(self.pid, True)),
              fg=C["dim"], bg=C["card"], wrap=360).pack(fill="x", pady=(4, 10))
        b = ttk.Button(der, text=T("Abrir el generador con esta técnica", "Open the generator with this technique"),
                       style="Blue.TButton", command=self.generador)
        b.pack(anchor="w")
        Tooltip(b, T("Abre el generador con %s y esta técnica como movimiento a cambiar." % G.quien(self.pid),
                     "Opens the generator with %s and this technique as the move to change." % G.quien(self.pid, True)))
        label(der, T(*G.LIMITES), fg=C["warn"], font=F["small"], bg=C["card"], wrap=360).pack(fill="x", pady=(14, 0))

    def _frames(self):
        try:
            return G.ba.clip_de(dict(personaje=self.pid, codigo=self.tec["codigo"]))[2]
        except Exception:  # noqa: BLE001
            return 0

    # ---------------------------------------------------------------- camara
    def _cam(self):
        self._titulo(self.contenido, " · " + T("cámara «%s»", "camera '%s'") % self._estilo_txt())
        fila = tk.Frame(self.contenido, bg=C["card"])
        fila.pack(fill="both", expand=True, padx=16, pady=(0, 12))
        izq = tk.Frame(fila, bg=C["card"])
        izq.pack(side="left", fill="y")
        self._vacia = tk.PhotoImage(master=self, width=PREV, height=PREV)
        self.lienzo = tk.Label(izq, bg=C["log_bg"], image=self._vacia)
        self.lienzo.pack()
        label(izq, T("Vista previa orientativa de la plantilla del Studio (encuadre, zoom, giro y temblor).",
                     "Indicative preview of the Studio template (framing, zoom, roll and shake)."), fg=C["dim"],
              font=F["small"], bg=C["card"], wrap=PREV).pack(fill="x", pady=(4, 0))
        der = tk.Frame(fila, bg=C["card"])
        der.pack(side="left", fill="both", expand=True, padx=(16, 0))
        label(der, T("Estilo de cámara", "Camera style"), font=F["bold"], bg=C["card"]).pack(fill="x", pady=(0, 6))
        self.filas_est = []
        for k, es, en, des, den in ESTILOS:
            f = tk.Frame(der, bg=C["card_hi"], highlightthickness=1,
                         highlightbackground=C["accent"] if k == self.estilo else C["line"])
            f.pack(fill="x", pady=3)
            tk.Label(f, text=T(es, en), bg=C["card_hi"], fg=C["text"], font=F["bold"], anchor="w").pack(
                fill="x", padx=10, pady=(3, 0))
            tk.Label(f, text=T(des, den), bg=C["card_hi"], fg=C["dim"], font=F["small"], anchor="w").pack(
                fill="x", padx=10, pady=(0, 3))
            make_clickable(f, lambda x=k: self._estilo(x), C["card_hi"], C["frame"])
            self.filas_est.append((f, k))
        label(izq, T("Se aplica en el Studio (plantilla y «Guardar como mod»); allí también se mueve punto a punto.",
                     "It is applied in the Studio (template and 'Save as mod'); there it also moves point by point."),
              fg=C["dim"], font=F["small"], bg=C["card"], wrap=PREV).pack(fill="x", pady=(8, 0))
        b = ttk.Button(izq, text=T("Abrir en el Studio", "Open in the Studio") + "  →", style="Ghost.TButton",
                       command=lambda: self.app.goto("studio", personaje=self.pid))
        b.pack(anchor="w")
        Tooltip(b, T("Abre el Studio de cámaras con este personaje.", "Opens the camera Studio with this character."))
        self._fotos_camara()
        self.gen_cam = getattr(self, "gen_cam", 0) + 1
        if not self.app.selftest:
            self._animar_camara(self.gen_cam)

    def _estilo_txt(self):
        return next(T(es, en) for k, es, en, _d, _e in ESTILOS if k == self.estilo)

    def _estilo(self, k):
        self.estilo = k
        self.pestana("cam")

    def _fotos_camara(self):
        """Los 3 fotogramas fijos (frente, 3/4, lado) del golpe central de la técnica, en el hilo de trabajo."""
        if self.app.selftest:
            return
        pid, code, n = self.pid, self.tec["codigo"], max(1, self._frames())
        if (pid, code) in self.fotos:
            return
        self.fotos[(pid, code)] = None

        def trabajo():
            from PIL import Image
            out = {}
            for v in ("frente", "3/4", "lado"):     # recortado al luchador: el encuadre del clip entero es mas ancho
                im = Image.open(G.pel.fotograma_fino(pid, code, n // 2, v, PREV * 2)).convert("RGBA")
                im = im.crop(im.getbbox() or (0, 0, im.width, im.height))
                k = PREV * 0.62 / max(im.size)
                out[v] = _sobre(im.resize((max(1, int(im.width * k)), max(1, int(im.height * k))), Image.LANCZOS),
                                (0, 0, 0, 0), (PREV, PREV))
            return out

        def fin(ok, res):
            self.fotos[(pid, code)] = res if ok else None
        TRABAJO.pedir(0, trabajo, fin)

    def _animar_camara(self, gen):
        if gen != self.gen_cam or self.pest != "cam" or not self.lienzo.winfo_exists():
            return
        from PIL import Image, ImageDraw, ImageTk
        fotos = self.fotos.get((self.pid, self.tec["codigo"]))
        clip = clip_estilo(self.estilo)
        self.f_cam = (self.f_cam + 1) % N_CAM
        lienzo = Image.new("RGBA", (PREV, PREV), _hex_rgb(C["log_bg"]))
        if fotos:
            if clip is None:
                im, esc, roll, dx, dy = fotos["3/4"], 1.0, 0.0, 0, 0
            else:
                ojo, obj = clip.at("ojo", self.f_cam), clip.at("objetivo", self.f_cam)
                d = math.dist(ojo, obj) or 1.0
                fov = clip.at("fov", self.f_cam)[0]
                esc = (40.0 / d) * math.tan(math.radians(19)) / max(0.05, math.tan(fov / 2))
                ang = math.degrees(math.atan2(ojo[0] - obj[0], ojo[2] - obj[2]))
                im = fotos["frente" if abs(ang) < 22.5 else "3/4" if abs(ang) < 67.5 else "lado"]
                if ang < 0:
                    im = im.transpose(Image.FLIP_LEFT_RIGHT)
                roll = math.degrees(clip.at("roll", self.f_cam)[0])
                dx, dy = -(obj[0]) * PREV / 40.0 * esc, (obj[1] - 10.5) * PREV / 40.0 * esc
            t = max(8, int(PREV * esc))
            im = im.resize((t, t), Image.LANCZOS).rotate(-roll, resample=Image.BICUBIC, expand=False)
            lienzo.alpha_composite(im, (int((PREV - t) / 2 + dx), int((PREV - t) / 2 + dy)))
        if clip is not None:                     # mapa visto desde arriba: camino del ojo y donde esta ahora
            dib = ImageDraw.Draw(lienzo)
            m, r = 46, 0.45
            dib.rectangle((PREV - 2 * m - 8, 6, PREV - 6, 2 * m + 8), fill=(26, 29, 36, 255))
            cx, cy = PREV - m - 7, m + 7
            pts = [(cx + clip.at("ojo", f)[0] * r, cy + clip.at("ojo", f)[2] * r) for f in range(0, N_CAM, 4)]
            dib.line(pts, fill=(120, 130, 150, 255), width=1)
            dib.ellipse((cx - 3, cy - 3, cx + 3, cy + 3), fill=_hex_rgb(C["accent"]))
            o = clip.at("ojo", self.f_cam)
            dib.ellipse((cx + o[0] * r - 4, cy + o[2] * r - 4, cx + o[0] * r + 4, cy + o[2] * r + 4),
                        fill=_hex_rgb(C["blue"]))
        elif not fotos:
            ImageDraw.Draw(lienzo).text((12, 12), T("preparando…", "preparing…"), fill=(140, 150, 166, 255))
        foto = ImageTk.PhotoImage(lienzo)
        self.lienzo.configure(image=foto)
        self.lienzo.foto = foto
        self.after(50, lambda: self._animar_camara(gen))

    # ---------------------------------------------------------------- color
    def _color(self):
        self._titulo(self.contenido)
        fila = tk.Frame(self.contenido, bg=C["card"])
        fila.pack(fill="both", expand=True, padx=16, pady=(0, 12))
        izq = tk.Frame(fila, bg=C["card"])
        izq.pack(side="left", fill="both", expand=True)
        self.chips_color = {}
        for que, es, en in (("aura", "Color del aura", "Aura colour"), ("ki", "Color del ki (sus técnicas)",
                                                                       "Ki colour (its techniques)")):
            label(izq, T(es, en), font=F["bold"], bg=C["card"]).pack(fill="x", pady=(6, 4))
            rej = tk.Frame(izq, bg=C["card"])
            rej.pack(fill="x")
            for i, (val, ces, cen, tono) in enumerate([(None, "Como en el juego", "As in the game", None)] +
                                                      list(nucleo.COLOR_PRESETS)):
                sw = "#3a3f4b" if tono is None else self._hex(tono)
                lb = tk.Label(rej, text=T(ces, cen), font=F["small"], padx=6, pady=4, cursor="hand2",
                              bg=sw, fg="#ffffff" if tono is None else "#111318")
                lb.grid(row=i // 5, column=i % 5, sticky="ew", padx=2, pady=2)
                lb.bind("<Button-1>", lambda _e, q=que, v=val: self._poner_color(q, v))
                self.chips_color[(que, val)] = lb
        acc = tk.Frame(izq, bg=C["card"])
        acc.pack(fill="x", pady=(12, 0))
        b = ttk.Button(acc, text=T("Guardar colores", "Save colours"), style="Accent.TButton", command=self.guardar_colores)
        b.pack(side="left")
        Tooltip(b, T("Crea (o actualiza) el mod de colores de este personaje, con copia para «Deshacer».",
                     "Creates (or updates) this character's colour mod, with a copy for 'Undo'."))
        b = ttk.Button(acc, text=T("Deshacer", "Undo"), command=self.deshacer)
        b.pack(side="left", padx=8)
        Tooltip(b, T("Deja tus mods como antes del último guardado.", "Puts your mods back as before the last save."))
        label(izq, T("El color vale para todo el personaje (aura y todas sus técnicas de ki), no solo para esta "
                     "técnica. Solo cambia el tono: lo blanco y lo gris se quedan igual.",
                     "The colour applies to the whole character (aura and all its ki techniques), not only this "
                     "technique. Only the hue changes: white and grey stay the same."), fg=C["dim"], font=F["small"],
              bg=C["card"], wrap=430).pack(fill="x", pady=(10, 0))
        der = tk.Frame(fila, bg=C["card"])
        der.pack(side="left", padx=(16, 0), anchor="n")
        self.muestra = vistas.MuestraAura(der, nucleo)
        self.muestra.pack(anchor="n", pady=(6, 0))
        self._pintar_color()

    @staticmethod
    def _hex(tono):
        import colorsys
        r, g, b = colorsys.hsv_to_rgb(tono / 360.0, 0.75, 1.0)
        return "#%02x%02x%02x" % (int(r * 255), int(g * 255), int(b * 255))

    def _poner_color(self, que, val):
        setattr(self, que, val)
        if self.pest == "color":
            self._pintar_color()

    def _pintar_color(self):
        for (que, val), lb in self.chips_color.items():
            on = getattr(self, que) == val
            lb.configure(relief="solid" if on else "flat", bd=2 if on else 0,
                         highlightthickness=0, text=("✓ " if on else "") + lb.cget("text").lstrip("✓ "))
        tono = dict((v, t) for v, _e, _n, t in nucleo.COLOR_PRESETS)
        self.muestra.pintar(self._hex(tono[self.aura]) if self.aura else None,
                            self._hex(tono[self.ki]) if self.ki else None)

    def guardar_colores(self):
        if self.aura is None and self.ki is None:
            self.set_status(self.lbl_estado, "warn", T("Elige un color de aura o de ki.", "Pick an aura or ki colour."))
            return
        if self.app.selftest:
            self.app.selftest_cmds.append(("tecnica: colores", [str(self.pid), str(self.aura), str(self.ki)]))
            return
        self.set_status(self.lbl_estado, "warn", T("Guardando los colores… (tarda unos segundos)",
                                                   "Saving the colours… (it takes a few seconds)"))
        env, pid, aura, ki = self.env, self.pid, self.aura, self.ki

        def fin(ok, res):
            if not ok:
                self.set_status(self.lbl_estado, "err", T("No se pudo: %s", "It failed: %s") % res)
                return
            mod, rep = res
            self.app.log_text("colores %s: %s\n" % (mod, "; ".join(rep)), "ok")
            self.set_status(self.lbl_estado, "ok", T("Colores guardados en tus mods («%s»). Puedes deshacer.",
                                                     "Colours saved in your mods ('%s'). You can undo.") % mod)
        TRABAJO.pedir(0, lambda: G.guardar_colores(env, pid, aura, ki, nucleo.anotar_indice), fin)

    def deshacer(self):
        try:
            n = len(nucleo.deshacer_ultimo(self.env))
        except OSError as ex:
            self.set_status(self.lbl_estado, "err", str(ex))
            return
        self.set_status(self.lbl_estado, "ok" if n else "warn", T(
            "Deshecho (la copia queda en modkit_backups).", "Undone (the copy stays in modkit_backups).") if n else
                        T("No había nada que deshacer.", "Nothing to undo."))

    # ---------------------------------------------------------------- nombre
    def _nombre(self):
        self._titulo(self.contenido)
        b = tk.Frame(self.contenido, bg=C["card"])
        b.pack(fill="both", expand=True, padx=16, pady=(0, 12))
        ficha = vistas.FichaCapsula(b, nucleo)
        ficha.pack(fill="x")
        ficha.mostrar(dict(nombre=self.tec["nombre"], tipo="definitiva" if self.tec["tipo"] == "definitiva" else
                           "especial"), G.tipo_txt(self.tec["tipo"], self.ingles))
        label(b, T("En los personajes del juego el nombre de la cápsula es una imagen del propio juego: cambiarlo aún "
                   "no se puede desde el Kit. En los personajes que importas sí: es la ficha [[capsula]] de su "
                   "personaje.toml (modo Avanzado › Personajes).",
                   "For game characters the capsule name is an image inside the game: it cannot be changed from the "
                   "Kit yet. For characters you import it can: it is the [[capsula]] entry of their personaje.toml "
                   "(Advanced mode › Characters)."), fg=C["dim"], bg=C["card"], wrap=620).pack(fill="x", pady=(12, 0))

    # ---------------------------------------------------------------- acciones
    def generador(self):
        if self.tec is None:
            return
        self.app.goto("biblioteca", personaje=self.pid, codigo=self.tec["codigo"])

    def probar(self):
        mods = [m for m in (G.mod_de(dict(personaje=self.pid)), G.mod_color(self.pid))
                if os.path.isdir(os.path.join(self.env.mods, m))]
        if not mods:
            self.set_status(self.lbl_estado, "warn", T(
                "Aún no hay nada guardado para %s: guarda un movimiento (generador) o unos colores."
                % G.quien(self.pid), "Nothing saved for %s yet: save a movement (generator) or some colours."
                % G.quien(self.pid, True)))
            return
        probar_mods(self, mods, self.lbl_estado, self.pid)
