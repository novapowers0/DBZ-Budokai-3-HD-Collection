# -*- coding: utf-8 -*-
"""Portada del Mod Kit (pagina de inicio): lista de tarjetas y su construccion.

Movido tal cual desde modkit_gui.py (fase 0). `g` es el modulo modkit_gui (widgets, T, C, F...);
se pasa como argumento para no importar modkit_gui desde aqui (evita el ciclo y la doble carga
cuando se ejecuta como script).
"""
from __future__ import annotations

import glob
import os
import subprocess
import threading

from kit import vistas

PAGE_DEFS = [
    ("setup", "Comprobar instalación", "Check installation",
     "Python, librerías, carpeta del juego y tus juegos de PS2.", "Python, libraries, game folder and your PS2 games."),
    ("mods", "Mis mods", "My mods",
     "Activa, desactiva y revisa los mods instalados.", "Enable, disable and inspect installed mods."),
    ("importer", "Importar personaje", "Import a character",
     "Trae personajes de Budokai 1, 2, Infinite World o de la comunidad, y los ports del proyecto.",
     "Bring characters from Budokai 1, 2, Infinite World or the community, and the project's ports."),
    ("characters", "Personajes nuevos", "New characters",
     "Nombre, plaza en la rueda, imágenes y cápsulas de tus personajes.",
     "Name, wheel slot, images and capsules of your characters."),
    ("w_musica", "Poner música", "Add music",
     "Guiado: tu canción (mp3, wav...) lista para elegirla en los combates.",
     "Guided: your song (mp3, wav...) ready to pick in fights."),
    ("create", "Crear con mis modelos", "Create from my models",
     "Un personaje nuevo a partir de tus propios archivos de modelo.",
     "A new character from your own model files."),
    ("stories", "Historias", "Stories",
     "Crea tu campaña o un modo de reto (torre, supervivencia, kumite) y móntalo en tus mods.",
     "Create your campaign or a challenge mode (tower, survival, kumite) and build it into your mods."),
    ("textures", "Texturas", "Textures",
     "Saca las texturas de un personaje a PNG, edítalas y vuelve a montarlas.",
     "Export a character's textures to PNG, edit them and rebuild."),
    ("swap", "Cambio de modelo", "Model swap",
     "Pon el modelo de un personaje en el sitio de otro.", "Put one character's model in another's slot."),
    ("arte", "Arte opcional", "Optional art",
     "Tus imágenes para el Dragon HUB y el modo Héroe: dice cuál sirve para qué y avisa si algo va mal.",
     "Your images for the Dragon HUB and Hero mode: says what each one is for and warns if something is wrong."),
    ("studio", "Studio de cámaras", "Camera Studio",
     "Rehaz las cámaras de las técnicas: línea de tiempo, plantillas, vista previa y Blender.",
     "Redo technique cameras: timeline, templates, preview and Blender."),
    ("diag", "Diagnóstico", "Diagnostics",
     "¿Va lento o se cierra? Analiza el registro del juego y te dice qué hacer.",
     "Slow or crashing? Analyse the game log and get what to do."),
    ("help", "Ayuda y comunidad", "Help & community",
     "Guías, primeros pasos y el Discord de la comunidad.", "Guides, first steps and the community Discord."),
]

# «¿Qué quieres hacer?»: tareas en lenguaje de jugador; cada una abre la pestana que ya existe.
TAREAS = [
    ("w_personaje", "Añadir un personaje", "Add a character",
     "Guiado: de Budokai 1, 2, Infinite World o la comunidad, hasta dejarlo montado.",
     "Guided: from Budokai 1, 2, Infinite World or the community, until it is built."),
    ("w_aspecto", "Cambiar aspecto y aura", "Change look and aura",
     "Guiado: color de aura y ki, y tus propias imágenes de un personaje.",
     "Guided: aura and ki colour, and your own images for a character."),
    ("w_traje", "Dar un traje a alguien", "Give someone a costume",
     "Guiado: un traje extra con un personaje que ya importaste o con tu modelo.",
     "Guided: an extra costume from a character you imported or your own model."),
    ("w_zip", "Instalar un mod (.zip)", "Install a mod (.zip)",
     "Guiado: mira lo que trae, instálalo y, si hace falta, móntalo. Con copia y «Deshacer».",
     "Guided: see what is inside, install it and build it if needed. With a copy and 'Undo'."),
    ("create", "Crear con mis modelos", "Create from my models",
     "Un personaje nuevo con tus propios archivos de modelo.", "A new character from your own model files."),
    ("stories", "Hacer una historia o un reto", "Make a story or challenge",
     "Tu campaña, torre, supervivencia o kumite.", "Your campaign, tower, survival or kumite."),
    ("diag", "Algo no va bien", "Something is wrong",
     "Analiza el registro del juego y te dice qué hacer.", "Reads the game log and tells you what to do."),
    ("setup", "Comprobar e instalar librerías", "Check and install libraries",
     "Python, librerías y carpeta del juego, con botón para instalar lo que falte.",
     "Python, libraries and game folder, with a button to install what is missing."),
]


def _rejilla(parent, g, items, self, cols=4):
    T, C, F, icon, make_clickable, tk = g.T, g.C, g.F, g.icon, g.make_clickable, g.tk
    grid = tk.Frame(parent, bg=C["bg"])
    grid.pack(fill="both", expand=True)
    for c in range(cols):
        grid.columnconfigure(c, weight=1, uniform="cards")
    for i, (key, es, en, des, den) in enumerate(items):
        card = tk.Frame(grid, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
        card.grid(row=i // cols, column=i % cols, sticky="nsew", padx=(0 if i % cols == 0 else 7, 0 if
                                                                       i % cols == cols - 1 else 7), pady=7)
        tk.Label(card, text=icon(key), bg=C["card"], fg=C["accent"], font=F["icon"]).pack(anchor="w", padx=18,
                                                                                        pady=(18, 6))
        tk.Label(card, text=T(es, en), bg=C["card"], fg=C["text"], font=F["h3"], anchor="w",
                 justify="left", wraplength=200).pack(fill="x", padx=18)
        tk.Label(card, text=T(des, den), bg=C["card"], fg=C["dim"], font=F["small"], anchor="nw",
                 justify="left", wraplength=200).pack(fill="both", expand=True, padx=18, pady=(4, 18))
        make_clickable(card, lambda k=key: self.app.goto(k), C["card"], C["card_hi"])


def copiar_informe(self, g):
    from kit import errores
    self.app.root.clipboard_clear()
    self.app.root.clipboard_append(errores.informe(self.app))
    self.app.info(g.T("Informe copiado. Pégalo en el Discord de la comunidad para que te ayuden.",
                      "Report copied. Paste it on the community Discord to get help."))




# KIT-05 (maqueta KIT_VISUAL/01_portada.png): 6 tarjetas grandes con imagen + 6 accesos pequenos + «Más…».
# (clave que abre, titulo es, en, texto es, en, entrada de data_cmn.afs para la imagen o None)
GRANDES = [
    ("w_traje", "Cambiar traje", "Change costume",
     "Dale un aspecto nuevo a un personaje. Lo ves forma por forma antes de guardarlo.",
     "Give a character a new look. You see it form by form before saving.", 266),
    ("w_personaje", "Añadir personaje", "Add a character",
     "Trae un luchador de otro Budokai, de la comunidad o con tus modelos.",
     "Bring a fighter from another Budokai, from the community or from your models.", 416),
    ("obra_tecnica", "Nueva técnica", "New technique",
     "Movimiento, cámara y color de una técnica, con vista previa animada.",
     "Movement, camera and colour of a technique, with an animated preview.", 276),
    ("obra_escenario", "Nuevo escenario", "New stage",
     "Elige un escenario por su foto y ponle nombre y ambiente.",
     "Pick a stage by its photo and give it a name and mood.", None),
    ("textures", "Pack de texturas", "Texture pack",
     "Cambia las imágenes del juego por las tuyas, con el antes y el después.",
     "Swap the game's images for yours, with the before and after.", 264),
    ("obra_fusiones", "Fusiones", "Fusions",
     "Cuánto dura una fusión (o que no se acabe) y fusiones como personajes sueltos.",
     "How long a fusion lasts (or that it never ends) and fusions as standalone characters.", 217),
]
PEQUENOS = [
    ("mods", "Mis mods", "My mods", "Activa, apaga y deshaz", "Turn on, off and undo"),
    ("w_zip", "Instalar un mod (.zip)", "Install a mod (.zip)", "Con copia y «Deshacer»", "With a copy and 'Undo'"),
    ("stories", "Historia o reto", "Story or challenge", "Tu campaña o torre", "Your campaign or tower"),
    ("w_musica", "Música", "Music", "Tus canciones", "Your songs"),
    ("obra_probar", "Probar en el juego", "Test in the game", "Con tus mods", "With your mods"),
    ("diag", "Algo no va bien", "Something is wrong", "Te dice qué hacer", "Tells you what to do"),
]
# lo que tenia la portada y no cabe arriba: va a «Más…» (nada se pierde)
MAS = [t for t in TAREAS if t[0] not in {k for k, *_ in GRANDES + PEQUENOS}]

# imagenes de las tarjetas: render toon del juego DEL JUGADOR (vistas.cmd_modelo), en segundo plano y en la cache del
# Kit (vistas.cache_dir). Sin juego o si falla, la tarjeta sale sin imagen: el Kit no lleva arte.
_DIBUJANDO, _HECHOS = set(), set()


def _dibujar(python, us, trabajos):
    for entrada, png in trabajos:
        try:
            subprocess.run(vistas.cmd_modelo(python, us, entrada, png), capture_output=True, timeout=300,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except (OSError, subprocess.SubprocessError):
            pass
        _HECHOS.add(png)
        _DIBUJANDO.discard(png)


def _imagenes(self, g):
    """Pone en cada tarjeta su imagen si ya esta en la cache; las que faltan se dibujan en un hilo y se recogen."""
    env = self.app.env
    if not env.us or not os.path.isdir(env.us):
        return
    faltan, nuevos = {}, []
    for key, entrada in self._fotos_de.items():
        png = vistas.png_modelo(env.us, entrada)
        if os.path.isfile(png) and png not in _DIBUJANDO:
            _poner(self, g, key, png)
        elif png not in _HECHOS:       # ya fallo una vez en esta sesion: no se repite
            faltan[key] = png
            if png not in _DIBUJANDO and not self.app.selftest:
                _DIBUJANDO.add(png)
                nuevos.append((entrada, png))
    if nuevos:
        threading.Thread(target=_dibujar, args=(env.python, env.us, nuevos), daemon=True).start()
    if faltan and not self.app.selftest:
        self.after(700, lambda: self.alive and _imagenes(self, g))


def _poner(self, g, key, png):
    lbl = self._foto_lbl.get(key)
    if lbl is None or getattr(lbl, "foto", None) is not None:
        return
    try:
        from PIL import Image, ImageTk
        with Image.open(png) as im:
            base = im.convert("RGBA")
        caja = base.getbbox()          # recorta el aire alrededor del cuerpo
        if caja:
            base = base.crop(caja)
        base.thumbnail((170, 168))
        lbl.foto = ImageTk.PhotoImage(base)
        lbl.configure(image=lbl.foto)
    except (OSError, ImportError, g.tk.TclError):
        pass


def _tarjeta(self, g, grid, i, item):
    T, C, F, tk = g.T, g.C, g.F, g.tk
    key, es, en, des, den, entrada = item
    card = tk.Frame(grid, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
    card.grid(row=i // 3, column=i % 3, sticky="nsew", padx=(0 if i % 3 == 0 else 7, 0 if i % 3 == 2 else 7), pady=7)
    card.columnconfigure(0, weight=1)
    card.rowconfigure(3, weight=1)
    tk.Label(card, text=g.icon(key), bg=C["card"], fg=C["accent"], font=F["icon"]).grid(row=0, column=0, sticky="w",
                                                                                      padx=20, pady=(16, 6))
    tk.Label(card, text=T(es, en), bg=C["card"], fg=C["text"], font=F["h2"], anchor="w", justify="left",
             wraplength=210).grid(row=1, column=0, sticky="ew", padx=20)
    tk.Label(card, text=T(des, den), bg=C["card"], fg=C["dim"], font=F["body"], anchor="nw", justify="left",
             wraplength=210).grid(row=2, column=0, sticky="new", padx=20, pady=(6, 16))
    foto = tk.Label(card, bg=C["card"], bd=0)
    foto.foto = None
    foto.grid(row=0, column=1, rowspan=4, sticky="e", padx=(0, 16), pady=10)
    self._foto_lbl[key] = foto
    if entrada is not None:
        self._fotos_de[key] = entrada
    g.make_clickable(card, lambda k=key: self.app.goto(k), C["card"], C["card_hi"])


def _acceso(self, g, fila, i, item):
    T, C, F, tk = g.T, g.C, g.F, g.tk
    key, es, en, des, den = item
    b = tk.Frame(fila, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
    b.grid(row=0, column=i, sticky="nsew", padx=(0 if i == 0 else 6, 0 if i == len(PEQUENOS) - 1 else 6))
    tk.Label(b, text=g.icon(key), bg=C["card"], fg=C["accent"], font=F["icon_sm"]).pack(side="left", padx=(10, 6),
                                                                                       pady=12)
    tf = tk.Frame(b, bg=C["card"])
    tf.pack(side="left", fill="x", expand=True, pady=9, padx=(0, 4))
    tk.Label(tf, text=T(es, en), bg=C["card"], fg=C["text"], font=F["bold"], anchor="w").pack(fill="x")
    sub = tk.Label(tf, text=T(des, den), bg=C["card"], fg=C["dim"], font=F["small"], anchor="w")
    sub.pack(fill="x")
    if key == "mods":
        self.mis_mods_sub = sub
    g.make_clickable(b, lambda k=key: self.app.goto(k), C["card"], C["card_hi"])


def _mas(self, g, abrir=None):
    """«Más…»: lo que no cabe en las tarjetas (y todas las herramientas). Recuerda si estaba abierto."""
    T, C = g.T, g.C
    if abrir is None:
        abrir = not self.app.state.get("portada_mas", False)
    self.app.state["portada_mas"] = abrir
    self.mas_lbl.configure(text=("▾  " if abrir else "▸  ") + T("Más…", "More…"))
    for w in self.mas_caja.winfo_children():
        w.destroy()
    if not abrir:
        self.mas_caja.pack_forget()
        return
    self.mas_caja.pack(fill="x", after=self.mas_lbl)
    _rejilla(self.mas_caja, g, MAS, self)
    g.label(self.mas_caja, T("Todas las herramientas", "All tools"), font=g.F["h2"]).pack(fill="x", pady=(14, 2))
    _rejilla(self.mas_caja, g, PAGE_DEFS, self)
    g.hint(self.mas_caja, T("¿Ya sabes modear? Cambia arriba a «Avanzado»: todas las herramientas del kit con todas "
                            "sus opciones, el registro completo y las rutas.",
                            "Already a modder? Switch to 'Advanced' at the top: every kit tool with all its options, "
                            "the full log and raw paths."), wrap=860).pack(fill="x", pady=(10, 6))


def construir(self, g):
    T, C, F, label, tk = g.T, g.C, g.F, g.label, g.tk
    sf, body = self.scroll_body()
    self.sf = sf
    self._foto_lbl, self._fotos_de, self.mis_mods_sub = {}, {}, None
    label(body, T("¿Qué quieres hacer?", "What do you want to do?"), font=F["h1"]).pack(fill="x", pady=(8, 0))
    label(body, T("Elige una tarjeta. Cada paso te enseña cómo va a quedar antes de tocar nada, y todo se puede "
                  "deshacer.", "Pick a card. Every step shows you how it will look before touching anything, and "
                  "everything can be undone."), fg=C["dim"]).pack(fill="x", pady=(2, 4))
    row = tk.Frame(body, bg=C["bg"])          # estado del Kit (antes en el recuadro de bienvenida)
    row.pack(fill="x", pady=(0, 6))
    self.dot = tk.Label(row, text="●", bg=C["bg"], fg=C["dim"], font=F["bold"])
    self.dot.pack(side="left")
    self.summary = label(row, "", font=F["small"], bg=C["bg"], fg=C["dim"])
    self.summary.pack(side="left", padx=(6, 12))
    g.ttk.Button(row, text=T("Ver detalles", "Details"), style="Small.TButton",
                 command=lambda: self.app.goto("setup")).pack(side="left")
    g.ttk.Button(row, text=T("Copiar informe", "Copy report"), style="Small.TButton",
                 command=lambda: copiar_informe(self, g)).pack(side="left", padx=(8, 0))
    grid = tk.Frame(body, bg=C["bg"])
    grid.pack(fill="x")
    for c in range(3):
        grid.columnconfigure(c, weight=1, uniform="grandes")
    for r in range(2):
        grid.rowconfigure(r, minsize=200, uniform="grandes")
    for i, item in enumerate(GRANDES):
        _tarjeta(self, g, grid, i, item)
    fila = tk.Frame(body, bg=C["bg"])
    fila.pack(fill="x", pady=(8, 4))
    for c in range(len(PEQUENOS)):
        fila.columnconfigure(c, weight=1, uniform="peq")
    for i, item in enumerate(PEQUENOS):
        _acceso(self, g, fila, i, item)
    self.mas_lbl = tk.Label(body, text="", bg=C["bg"], fg=C["accent"], font=F["bold"], cursor="hand2", anchor="w")
    self.mas_lbl.pack(fill="x", pady=(12, 4))
    self.mas_lbl.bind("<Button-1>", lambda _e: _mas(self, g))
    self.mas_caja = tk.Frame(body, bg=C["bg"])
    _mas(self, g, self.app.state.get("portada_mas", False))
    _imagenes(self, g)
    fotos = sorted(glob.glob(os.path.join(vistas.cache_dir(), "escenario_b2_g7_*.png")) +   # KIT-07: foto de la cache
                   glob.glob(os.path.join(vistas.cache_dir(), "escenario_*.png")))
    if fotos:
        _poner(self, g, "obra_escenario", fotos[0])


def al_mostrar(self, g):
    T, C = g.T, g.C
    checks = self.app.checks()
    bad = [c for c in checks if c["level"] == "err"]
    warn = [c for c in checks if c["level"] == "warn"]
    if bad:
        self.dot.configure(fg=C["err"])
        self.summary.configure(text=T("Falta algo: %s", "Something is missing: %s") % ", ".join(
            c["title"] for c in bad))
    elif warn:
        self.dot.configure(fg=C["warn"])
        self.summary.configure(text=T("Casi listo. Revisa: %s", "Almost ready. Check: %s") % ", ".join(
            c["title"] for c in warn))
    else:
        self.dot.configure(fg=C["ok"])
        self.summary.configure(text=T("Todo listo para modear.", "All set for modding."))
    if self.mis_mods_sub is not None:
        mods = g.scan_mods(self.app.env.mods)
        self.mis_mods_sub.configure(text=T("%d instalados · %d activos", "%d installed · %d on") % (
            len(mods), sum(1 for m in mods if m["enabled"])))