# -*- coding: utf-8 -*-
"""Ayuda y comunidad: guias y documentos.

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403
from kit.paginas.dialogos import DocViewer  # noqa: E402


# --------------------------------------------------------------------------------- ayuda
DOCS = [
    ("Léeme del kit", "Kit readme", "Instalación y contenido del kit.", "Kit install and contents.",
     ["LEEME_KIT.txt", "tools/modpacks/LEEME_KIT.txt"]),
    ("Cómo hacer mods", "How to make mods", "Tipos de mod, override por entrada, activar/desactivar.",
     "Mod types, per-entry override, enable/disable.", ["docs/COMO_HACER_MODS.md", "docs/02_mods/COMO_HACER_MODS.md"]),
    ("Cambio de modelo", "Model swap", "Cómo funciona el swap de modelos B3 → B3.", "How B3 → B3 model swaps work.",
     ["docs/MODEL_SWAP.md", "docs/02_mods/MODEL_SWAP.md"]),
    ("Mod de texturas", "Texture mod", "Extraer, editar y reconstruir texturas.", "Extract, edit and rebuild textures.",
     ["docs/TEXTURAS_MOD.md", "docs/02_mods/TEXTURAS_MOD.md"]),
    ("Packs de texturas", "Texture packs", "Packs estilo PCSX2 (reemplazo por hash).",
     "PCSX2-style packs (replacement by hash).", ["docs/PACKS_DE_TEXTURAS.md", "docs/02_mods/PACKS_DE_TEXTURAS.md"]),
    ("Juegos de PS2 (ps2_games)", "PS2 games (ps2_games)", "Qué nombres deben tener tus copias.",
     "Which names your copies must have.", ["ps2_games/LEEME.txt", "tools/modpacks/LEEME_PS2_GAMES.txt"]),
    ("Cápsulas de B3", "B3 capsules", "Formato de las cápsulas (habilidades).", "Capsule (skill) format.",
     ["docs/formatos/CAPSULAS_B3.md", "docs/03_formatos/CAPSULAS_B3.md"]),
    ("Mapa del roster HD", "HD roster map", "IDs, entradas AFS y tablas por personaje.",
     "IDs, AFS entries and per-character tables.", ["docs/formatos/MAPA_ROSTER_HD.md",
                                                    "docs/03_formatos/MAPA_ROSTER_HD.md"]),
    ("Formato #ACM (moveset)", "#ACM format (moveset)", "Animaciones y moveset HD.", "HD animations and moveset.",
     ["docs/formatos/ACM_FORMAT.md", "docs/03_formatos/ACM_FORMAT.md"]),
    ("Formato AMO / AWO", "AMO / AWO format", "Modelos PS2 y HD.", "PS2 and HD models.",
     ["docs/formatos/AMO_AWO.md", "docs/03_formatos/AMO_AWO.md"]),
    ("Estructura de un bin", "Bin layout", "Contenedor #AMB HD.", "HD #AMB container.",
     ["docs/formatos/BIN_LAYOUT.md", "docs/03_formatos/BIN_LAYOUT.md"]),
    ("Escenarios", "Stages", "Formato de los escenarios.", "Stage format.",
     ["docs/formatos/STAGES_FORMAT.md", "docs/03_formatos/STAGES_FORMAT.md"]),
    ("Guía de swaps y ports", "Swaps & ports guide", "Principios de los swaps y ports entre juegos.",
     "Principles of cross-game swaps and ports (technical notes, Spanish only).", ["mod center hd/GUIA_SWAPS_Y_PORTS.md"]),
    ("Inventario de herramientas", "Tools inventory", "Todas las herramientas del proyecto.",
     "Every tool in the project (technical list, Spanish only).", ["docs/04_herramientas/TOOLS.md"]),
]

class HelpPage(Page):
    key = "help"

    def build(self):
        self.header(T("Ayuda y comunidad", "Help & community"),
                    T("Guías incluidas en el kit, primeros pasos y dónde pedir ayuda.",
                      "Guides shipped with the kit, first steps and where to ask for help."))
        sf, body = self.scroll_body()
        cols = tk.Frame(body, bg=C["bg"])
        cols.pack(fill="both", expand=True)
        cols.columnconfigure(0, weight=3, uniform="h")
        cols.columnconfigure(1, weight=2, uniform="h")
        left = tk.Frame(cols, bg=C["bg"])
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 12))
        right = tk.Frame(cols, bg=C["bg"])
        right.grid(row=0, column=1, sticky="nsew")
        g = Card(left, T("Guías", "Guides"), T("Se abren aquí mismo. «Abrir» las abre con tu programa.",
                                              "They open right here. 'Open' uses your own program."))
        g.pack(fill="x", pady=(0, 12))
        found = 0
        for es, en, des, den, cands in DOCS:
            if nucleo.LANG == "en":    # X.md -> X_EN.md / X.txt -> X_EN.txt (guia traducida, si existe)
                cands = [re.sub(r"(\.\w+)$", r"_EN\1", c) for c in cands] + cands
            p = first_existing(*[os.path.join(KIT, *c.split("/")) for c in cands])
            if not p:
                continue
            found += 1
            row = tk.Frame(g.body, bg=C["card"])
            row.pack(fill="x", pady=3)
            tf = tk.Frame(row, bg=C["card"])
            tf.pack(side="left", fill="x", expand=True)
            label(tf, T(es, en), font=F["bold"], bg=C["card"]).pack(fill="x")
            label(tf, T(des, den), fg=C["dim"], font=F["small"], bg=C["card"]).pack(fill="x")
            ttk.Button(row, text=T("Abrir", "Open"), style="Small.TButton",
                       command=lambda q=p: open_path(q)).pack(side="right")
            ttk.Button(row, text=T("Leer", "Read"), style="Small.TButton",
                       command=lambda q=p, t=T(es, en): DocViewer(self.app, q, t)).pack(side="right", padx=6)
        if not found:
            label(g.body, T("No hay guías junto al kit.", "No guides next to the kit."), fg=C["dim"],
                  bg=C["card"]).pack(fill="x")
        docs = first_existing(os.path.join(KIT, "docs"))
        if docs:
            ttk.Button(g.body, text=T("Abrir carpeta docs", "Open docs folder"), style="Small.TButton",
                       command=lambda: open_path(docs)).pack(anchor="w", pady=(8, 0))
        s = Card(left, T("Primeros pasos", "First steps"))
        s.pack(fill="x", pady=(0, 12))
        steps = [T("Copia TODO el kit junto a dbz3.exe (deben quedar «mod center hd» y «awo_tools» al lado).",
                   "Copy the WHOLE kit next to dbz3.exe ('mod center hd' and 'awo_tools' must sit beside it)."),
                 T("«Comprobar instalación» → «Instalar librerías» (una sola vez).",
                   "'Check installation' → 'Install libraries' (only once)."),
                 T("Para personajes de otros juegos, pon TUS copias en ps2_games y usa «Importar personaje».",
                   "For characters from other games, put YOUR copies in ps2_games and use 'Import a character'."),
                 T("Abre el juego y pulsa JUGAR: los personajes nuevos se montan solos.",
                   "Open the game and press PLAY: new characters are built automatically."),
                 T("Si algo va mal, desactiva el mod en «Mis mods». Tus partidas guardadas no se tocan nunca.",
                   "If something goes wrong, disable the mod in 'My mods'. Your saves are never touched.")]
        for i, st in enumerate(steps, 1):
            row = tk.Frame(s.body, bg=C["card"])
            row.pack(fill="x", pady=3)
            tk.Label(row, text=str(i), bg=C["accent"], fg=C["on_accent"], font=F["bold"], width=2).pack(side="left",
                                                                                                       anchor="n")
            label(row, st, bg=C["card"], wrap=470).pack(side="left", fill="x", padx=10)
        cm = Card(right, T("Comunidad", "Community"), accent=True)
        cm.pack(fill="x", pady=(0, 12))
        label(cm.body, T("Comparte tus mods, pide ayuda y descubre los de los demás en el Discord de la comunidad.",
                         "Share your mods, ask for help and discover everyone else's on the community Discord."),
              bg=C["card"], wrap=320).pack(fill="x")
        if DISCORD_URL:
            ttk.Button(cm.body, text=T("Unirse al Discord", "Join the Discord"), style="Accent.TButton",
                       command=lambda: webbrowser.open(DISCORD_URL)).pack(anchor="w", pady=(10, 0))
            label(cm.body, DISCORD_URL, fg=C["blue"], bg=C["card"], font=F["small"]).pack(fill="x", pady=(4, 0))
        else:
            label(cm.body, T("Enlace de Discord: próximamente.", "Discord link: coming soon."), fg=C["warn"],
                  bg=C["card"], font=F["bold"]).pack(fill="x", pady=(10, 0))
        label(cm.body, T("Para compartir un mod: comprime su carpeta (mods\\<tu_mod>) y súbela. Nunca compartas "
                         "archivos de los juegos.", "To share a mod: zip its folder (mods\\<your_mod>) and upload "
                         "it. Never share game files."), fg=C["dim"], bg=C["card"], wrap=380,
              font=F["small"]).pack(fill="x", pady=(10, 0))
        ab = Card(right, T("Acerca de", "About"))
        ab.pack(fill="x", pady=(0, 12))
        for a, b in (("DBZ3 HD Mod Kit", MODKIT_VERSION), (T("Kit de modding", "Modding kit"), KIT_VERSION),
                     ("Python", "%d.%d.%d" % sys.version_info[:3]), ("Tk", str(tk.TkVersion))):
            row = tk.Frame(ab.body, bg=C["card"])
            row.pack(fill="x")
            label(row, a, fg=C["dim"], bg=C["card"], width=18).pack(side="left")
            label(row, b, bg=C["card"]).pack(side="left")
        label(ab.body, T("No incluye archivos de ningún juego: usa siempre los de tus copias.",
                         "Contains no game files: always use your own copies."), fg=C["dim"], bg=C["card"],
              font=F["small"], wrap=320).pack(fill="x", pady=(8, 0))
