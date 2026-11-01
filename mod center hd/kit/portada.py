# -*- coding: utf-8 -*-
"""Portada del Mod Kit (pagina de inicio): lista de tarjetas y su construccion.

Movido tal cual desde modkit_gui.py (fase 0). `g` es el modulo modkit_gui (widgets, T, C, F...);
se pasa como argumento para no importar modkit_gui desde aqui (evita el ciclo y la doble carga
cuando se ejecuta como script).
"""
from __future__ import annotations

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


def construir(self, g):
    T, C, F, Card, label, hint = g.T, g.C, g.F, g.Card, g.label, g.hint
    icon, make_clickable, ttk, tk = g.icon, g.make_clickable, g.ttk, g.tk
    sf, body = self.scroll_body()
    self.sf = sf
    top = Card(body, accent=True)
    top.pack(fill="x", pady=(12, 14))
    label(top.body, T("¡Hola! ¿Qué quieres hacer hoy?", "Hi! What do you want to do today?"),
          font=F["h1"]).pack(fill="x")
    label(top.body, T("Elige una tarjeta. Cada paso explica lo que hace y no toca tus partidas guardadas. "
                      "Los personajes nuevos se añaden al juego al pulsar JUGAR en el launcher.",
                      "Pick a card. Every step explains what it does and never touches your saves. New "
                      "characters are added to the game when you press PLAY in the launcher."),
          fg=C["dim"], wrap=860).pack(fill="x", pady=(4, 10))
    row = tk.Frame(top.body, bg=C["card"])
    row.pack(fill="x")
    self.dot = tk.Label(row, text="●", bg=C["card"], fg=C["dim"], font=F["h3"])
    self.dot.pack(side="left")
    self.summary = label(row, "", font=F["bold"], bg=C["card"])
    self.summary.pack(side="left", padx=(6, 12))
    ttk.Button(row, text=T("Ver detalles", "Details"), style="Small.TButton",
               command=lambda: self.app.goto("setup")).pack(side="left")
    ttk.Button(row, text=T("Copiar informe", "Copy report"), style="Small.TButton",
               command=lambda: copiar_informe(self, g)).pack(side="left", padx=(8, 0))
    label(body, T("¿Qué quieres hacer?", "What do you want to do?"), font=F["h2"]).pack(fill="x", pady=(0, 2))
    _rejilla(body, g, TAREAS, self)
    label(body, T("Todas las herramientas", "All tools"), font=F["h2"]).pack(fill="x", pady=(14, 2))
    _rejilla(body, g, PAGE_DEFS, self)
    tip = tk.Frame(body, bg=C["bg"])
    tip.pack(fill="x", pady=(10, 6))
    hint(tip, T("¿Ya sabes modear? Cambia arriba a «Avanzado»: todas las herramientas del kit con todas sus "
                "opciones, el registro completo y las rutas.",
                "Already a modder? Switch to 'Advanced' at the top: every kit tool with all its options, the "
                "full log and raw paths."), wrap=860).pack(fill="x")


def al_mostrar(self, g):
    T, C, F, Card, label, hint = g.T, g.C, g.F, g.Card, g.label, g.hint
    icon, make_clickable, ttk, tk = g.icon, g.make_clickable, g.ttk, g.tk
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
