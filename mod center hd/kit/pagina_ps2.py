# -*- coding: utf-8 -*-
"""Modo PS2 del Mod Kit: crea mods para el Budokai 3 de PS2 (motor en ps2_b3.py) y monta una ISO nueva."""
from __future__ import annotations

import glob
import os
import re
import tkinter as tk
from tkinter import ttk

from kit import musica, vistas

# Nombres: modding resources/adx_complete_bin_list_b3.docx (comunidad); la 142 tambien medida en combate.
_T = ("Move Forward Fearlessly", "Challengers", "The Battle With All My Force", "Warrior From An Unknown Land",
      "The Man Called C", "Fortitude Indomitable Spirit", "Wild Soul", "Impulse To Victory", "Do It At All Risks",
      "I'm Gonna Get Over", "Greatest Tactics In The Universe", "MISSION", "Archipelago Theme", "Sky Battle", "Ultimatum",
      "Out Of Control", "Night Of The Tempest", "Evil Power", "Powerfull Man From The Darkness", "Twist Of Fate",
      "Legend Of Hercule", "Asian Spirit", "I Wont Forgive You", "Running To Victory", "Ore Wa Tokoton Tomarana!!")
PISTAS = {122 + i: (t, t) for i, t in enumerate(_T)}
PISTAS.update({n: ("Dragon Universe", "Dragon Universe") for n in range(4187, 4216)})
PISTAS.update({
    4267: ("Menú principal", "Main menu"), 4268: ("Menú de cargar o nueva partida", "Load/New game menu"),
    4269: ("Elegir personaje", "Character select"), 4270: ("Tienda de habilidades", "Skill shop"),
    4271: ("Torneo Mundial", "World Tournament"), 4272: ("Dragon Arena", "Dragon Arena"),
    4273: ("Dragon Universe (inserción)", "Dragon Universe insert"), 4274: ("Opciones", "Options menu"),
    4275: ("Vídeo de introducción", "Intro film"), 4276: ("Pantalla de inicio", "Splash screen")})

# Lo que el modo HD hace y aqui no: (es, en, por que es, por que en)
NO_DISPONIBLE = [
    ("Personajes nuevos en la rueda", "New characters on the wheel",
     "En HD los añade nuestro motor al arrancar; en PS2 corre el juego original y la rueda es fija. Aquí un personaje cambia el aspecto de otro.",
     "In HD our engine adds them at start-up; PS2 runs the original game with a fixed wheel. Here a character changes another's look."),
    ("Cápsulas, colores de aura y formas", "Capsules, aura colours and forms",
     "Necesitan el motor del port: en PS2 están en el ejecutable original, sin estudiar para editar.",
     "They need the port's engine: on PS2 they live in the original executable, not studied for editing."),
    ("Historias, retos y HUB", "Stories, challenges and HUB",
     "Son modos del port, no existen en el juego de PS2.", "They are port modes; they do not exist in the PS2 game."),
    ("Studio de cámaras y packs HD", "Camera Studio and HD packs",
     "Usan el motor y el formato HD.", "They use the HD engine and format."),
    ("Música extra elegible en combate", "Extra selectable fight music",
     "En PS2 solo se puede sustituir una pista; no se pueden añadir.", "On PS2 a track can only be replaced, not added."),
]


def nombre_mod(texto):
    return re.sub(r"[^a-z0-9_]+", "_", texto.lower()).strip("_")[:40]


class PaginaPs2:
    """Se construye dentro de una Page de modkit_gui: g = modulo, page = la Page."""

    def __init__(self, page, g):
        self.page, self.g, self.app = page, g, page.app
        self.mods = os.path.join(g.HERE, "mods_ps2")
        self.tmp = os.path.join(vistas.cache_dir(), "ps2_texturas")
        self.copia_v = tk.StringVar(value=self.app.settings.get("ps2_copia", "") or self._copia_en_ps2_games())
        self.salida_v = tk.StringVar(value=self.app.settings.get("ps2_salida", ""))
        self.copia_ok = False
        self.construir()

    def _copia_en_ps2_games(self):
        for p in sorted(glob.glob(os.path.join(self.app.env.ps2, "*Budokai 3*"))):
            if os.path.isdir(p) or p.lower().endswith(".iso"):
                return p
        return ""

    def _py(self, *args):
        return [self.app.env.python, os.path.join(self.g.HERE, "ps2_b3.py")] + list(args)

    def construir(self):
        g, T = self.g, self.g.T
        self.page.header(T("Mods para Budokai 3 de PS2", "Mods for PS2 Budokai 3"),
                         T("Crea mods para tu copia de PS2 y genera una ISO nueva para el emulador. Tu copia nunca se toca.",
                           "Create mods for your PS2 copy and build a new ISO for the emulator. Your copy is never touched."))
        sf, body = self.page.scroll_body()
        self.sf = sf

        c = g.Card(body, T("1. Tu copia", "1. Your copy"))
        c.pack(fill="x", pady=(0, 12))
        b = c.body
        b.columnconfigure(1, weight=1)
        g.label(b, T("Tu copia de Budokai 3 (PS2)", "Your Budokai 3 copy (PS2)"), bg=g.C["card"]).grid(row=0, column=0, sticky="w", padx=(0, 12))
        g.PathEntry(b, self.copia_v, "file", [("ISO", "*.iso"), ("*", "*.*")], bg=g.C["card"]).grid(row=0, column=1, sticky="ew", pady=4)
        ttk.Button(b, text=T("Usar una carpeta", "Use a folder"), style="Small.TButton", command=self._elegir_carpeta).grid(row=0, column=2, padx=(6, 0))
        self.copia_lbl = g.hint(b, "", bg=g.C["card"], wrap=860)
        self.copia_lbl.grid(row=1, column=0, columnspan=3, sticky="w", pady=(4, 0))
        self.copia_v.trace_add("write", lambda *_: self._comprobar())

        c = g.Card(body, T("2. Crea un mod", "2. Create a mod"))
        c.pack(fill="x", pady=(0, 12))
        nb = ttk.Notebook(c.body)
        nb.pack(fill="x")
        self._pest_textura(nb)
        self._pest_personaje(nb)
        self._pest_musica(nb)

        c = g.Card(body, T("3. Tus mods de PS2", "3. Your PS2 mods"))
        c.pack(fill="x", pady=(0, 12))
        self.lista = ttk.Treeview(c.body, columns=("estado", "contenido"), show="tree headings", height=5, selectmode="browse")
        self.lista.heading("#0", text=T("Mod", "Mod"))
        self.lista.heading("estado", text=T("Estado", "State"))
        self.lista.heading("contenido", text=T("Cambia", "Changes"))
        self.lista.column("estado", width=90, anchor="center")
        self.lista.column("contenido", width=420)
        self.lista.pack(fill="x")
        bf = tk.Frame(c.body, bg=g.C["card"])
        bf.pack(fill="x", pady=(6, 0))
        ttk.Button(bf, text=T("Activar / desactivar", "Enable / disable"), command=self._alternar).pack(side="left")
        ttk.Button(bf, text=T("Abrir carpeta", "Open folder"), command=lambda: g.open_path(self.mods)).pack(side="left", padx=6)
        self.aviso_mods = g.hint(c.body, "", bg=g.C["card"], wrap=860)
        self.aviso_mods.pack(fill="x", pady=(6, 0))

        c = g.Card(body, T("4. Monta la ISO", "4. Build the ISO"))
        c.pack(fill="x", pady=(0, 12))
        b = c.body
        b.columnconfigure(1, weight=1)
        g.label(b, T("ISO que se creará", "ISO to create"), bg=g.C["card"]).grid(row=0, column=0, sticky="w", padx=(0, 12))
        g.PathEntry(b, self.salida_v, "save", [("ISO", "*.iso")], bg=g.C["card"]).grid(row=0, column=1, sticky="ew", pady=4)
        self.b_montar = ttk.Button(b, text="⟳  " + T("Montar ISO", "Build ISO"), style="Accent.TButton", command=self._montar)
        self.b_montar.grid(row=1, column=1, sticky="w", pady=(6, 0))
        self.b_montar._con_tip = True
        g.Tooltip(self.b_montar, T("Crea la ISO con los mods encendidos. Unos segundos y 2,4 GB de disco.",
                                   "Creates the ISO with the enabled mods. A few seconds and 2.4 GB of disk."))
        self.estado = self.page.status_label(b)
        self.estado.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(6, 0))
        g.hint(b, T("Después, en PCSX2: Sistema > Arrancar ISO y elige la nueva. Probado en PCSX2 2.9; en consola real no se ha podido probar.",
                    "Then in PCSX2: System > Boot ISO and pick the new one. Tested on PCSX2 2.9; real consoles could not be tested."),
               bg=g.C["card"], wrap=860).grid(row=3, column=0, columnspan=2, sticky="w", pady=(6, 0))

        c = g.Card(body, T("Lo que no está en PS2 y por qué", "What is not on PS2 and why"))
        c.pack(fill="x", pady=(0, 12))
        for es, en, pes, pen in NO_DISPONIBLE:
            f = tk.Frame(c.body, bg=g.C["card"])
            f.pack(fill="x", pady=2)
            g.label(f, "✕  " + (es if g.LANG == "es" else en), fg=g.C["dim"], font=g.F["bold"], bg=g.C["card"]).pack(anchor="w")
            g.label(f, pes if g.LANG == "es" else pen, fg=g.C["dim"], bg=g.C["card"], wrap=860).pack(anchor="w", padx=(22, 0))
        self.refrescar()

    # ---------------------------------------------------------------- pestañas
    def _pest_textura(self, nb):
        g, T = self.g, self.g.T
        p = tk.Frame(nb, bg=g.C["card"])
        nb.add(p, text=T("Textura", "Texture"))
        f = tk.Frame(p, bg=g.C["card"])
        f.pack(fill="x", pady=(8, 4))
        self.t_quien = tk.StringVar()
        g.label(f, T("Personaje", "Character"), bg=g.C["card"]).pack(side="left", padx=(0, 8))
        self.t_cb = ttk.Combobox(f, textvariable=self.t_quien, state="readonly", width=26, values=self._personajes())
        self.t_cb.pack(side="left")
        g.label(f, T("Forma", "Form"), bg=g.C["card"]).pack(side="left", padx=(10, 6))
        self.t_forma = tk.IntVar(value=1)
        ttk.Spinbox(f, from_=1, to=13, width=4, textvariable=self.t_forma).pack(side="left")
        ttk.Button(f, text="⇩  " + T("Sacar texturas", "Extract textures"), command=self._sacar).pack(side="left", padx=8)
        ttk.Button(f, text=T("Abrir carpeta de texturas", "Open textures folder"), command=lambda: g.open_path(self.t_dir())).pack(side="left")
        f = tk.Frame(p, bg=g.C["card"])
        f.pack(fill="x", pady=4)
        self.t_mod = tk.StringVar()
        g.label(f, T("Nombre del mod", "Mod name"), bg=g.C["card"]).pack(side="left", padx=(0, 8))
        ttk.Entry(f, textvariable=self.t_mod, width=24).pack(side="left")
        ttk.Button(f, text="✚  " + T("Crear mod con mis cambios", "Create mod with my changes"), style="Accent.TButton", command=self._hacer_textura).pack(side="left", padx=8)
        g.hint(p, T("La PS2 usa paletas de 16 o 256 colores: al guardar se reducen los colores de tu imagen. Mantén el tamaño. La transparencia marca zonas de sombra.",
                    "PS2 uses 16 or 256-colour palettes: your image's colours are reduced on save. Keep the size. Transparency marks shading areas."),
               bg=g.C["card"], wrap=860).pack(fill="x")
        self.galeria = vistas.Galeria(p, g, columnas=7, lado=104)
        self.galeria.pack(fill="x", pady=(6, 8))

    def _pest_personaje(self, nb):
        g, T = self.g, self.g.T
        p = tk.Frame(nb, bg=g.C["card"])
        nb.add(p, text=T("Personaje", "Character"))
        f = tk.Frame(p, bg=g.C["card"])
        f.pack(fill="x", pady=(8, 4))
        self.p_quien, self.p_por = tk.StringVar(), tk.StringVar()
        g.label(f, T("En el sitio de", "In the slot of"), bg=g.C["card"]).pack(side="left", padx=(0, 8))
        q = ttk.Combobox(f, textvariable=self.p_quien, state="readonly", width=22, values=self._personajes())
        q.pack(side="left")
        q.bind("<<ComboboxSelected>>", lambda _e: self._ver_cuerpos())
        g.label(f, T("ponle el aspecto de", "give it the look of"), bg=g.C["card"]).pack(side="left", padx=8)
        q = ttk.Combobox(f, textvariable=self.p_por, state="readonly", width=22, values=self._personajes())
        q.pack(side="left")
        q.bind("<<ComboboxSelected>>", lambda _e: self._ver_cuerpos())
        ttk.Button(f, text="✚  " + T("Crear mod", "Create mod"), style="Accent.TButton", command=self._hacer_personaje).pack(side="left", padx=8)
        g.hint(p, T("Conserva el nombre, los golpes y el icono del primero: solo cambia el modelo y sus texturas. Elige cuerpos parecidos. Medido en combate: Vegeta en el sitio de Krillin.",
                    "Keeps the first one's name, moves and icon: only the model and its textures change. Pick similar bodies. Measured in a fight: Vegeta in Krillin's slot."),
               bg=g.C["card"], wrap=860).pack(fill="x", pady=(0, 4))
        self.cuerpos = tk.Frame(p, bg=g.C["card"])
        self.cuerpos.pack(fill="x", pady=(0, 8))

    def _ver_cuerpos(self):
        """Los dos modelos dibujados desde tu Budokai 3 HD (misma numeracion). Sin HD no hay dibujo: lo dice."""
        import ps2_b3
        g, T = self.g, self.g.T
        for w in self.cuerpos.winfo_children():
            w.destroy()
        env = self.app.env
        if not env.us:
            g.hint(self.cuerpos, T("Sin vista previa: necesita tu Budokai 3 HD para dibujar los modelos.",
                                   "No preview: it needs your Budokai 3 HD to draw the models."), bg=g.C["card"]).pack(anchor="w")
            return
        for quien in (self.p_quien.get(), self.p_por.get()):
            if not quien:
                continue
            n = ps2_b3.PERSONAJES[quien][0][0]
            png = vistas.png_modelo(env.us, n)
            caja = tk.Frame(self.cuerpos, bg=g.C["card"])
            caja.pack(side="left", padx=(0, 14))
            w = vistas.foto_fija(g, caja, png, 160) if os.path.isfile(png) else None
            if w:
                w.pack()
            elif not self.app.selftest:
                self.app.run(vistas.cmd_modelo(env.python, env.us, n, png), T("Vista previa", "Preview"),
                             lambda _j: self.page.alive and os.path.isfile(png) and self._ver_cuerpos(), quiet=True)
            g.label(caja, quien if w else T("Dibujando %s…", "Drawing %s…") % quien, fg=g.C["dim"], bg=g.C["card"]).pack()
        g.hint(self.cuerpos, T("Dibujado con el modelo de la versión HD; en PS2 tiene las texturas de PS2.",
                               "Drawn with the HD model; on PS2 it has the PS2 textures."), bg=g.C["card"], wrap=300).pack(side="left")

    def _pest_musica(self, nb):
        g, T = self.g, self.g.T
        p = tk.Frame(nb, bg=g.C["card"])
        nb.add(p, text=T("Música", "Music"))
        f = tk.Frame(p, bg=g.C["card"])
        f.pack(fill="x", pady=(8, 4))
        self.m_audio, self.m_pista, self.m_mod = tk.StringVar(), tk.StringVar(), tk.StringVar()
        f.columnconfigure(1, weight=1)
        g.label(f, T("Archivo de audio", "Audio file"), bg=g.C["card"]).grid(row=0, column=0, sticky="w", padx=(0, 8))
        g.PathEntry(f, self.m_audio, "file", [("Audio", "*.mp3 *.wav *.ogg *.flac *.m4a *.adx")], bg=g.C["card"]).grid(row=0, column=1, sticky="ew")
        vistas.Oyente(f, g, self.m_audio.get, musica.ffmpeg).grid(row=1, column=1, sticky="w", pady=4)
        g.label(f, T("Pista que sustituye", "Track to replace"), bg=g.C["card"]).grid(row=2, column=0, sticky="w", padx=(0, 8))
        self.m_cb = ttk.Combobox(f, textvariable=self.m_pista, width=38)
        self.m_cb.grid(row=2, column=1, sticky="w", pady=4)
        vistas.Oyente(f, g, self._pista_original, musica.ffmpeg).grid(row=2, column=2, sticky="w", padx=(8, 0))
        g.label(f, T("Nombre del mod", "Mod name"), bg=g.C["card"]).grid(row=3, column=0, sticky="w", padx=(0, 8))
        ttk.Entry(f, textvariable=self.m_mod, width=24).grid(row=3, column=1, sticky="w", pady=4)
        ttk.Button(f, text="✚  " + T("Crear mod", "Create mod"), style="Accent.TButton", command=self._hacer_musica).grid(row=4, column=1, sticky="w", pady=(2, 8))
        g.hint(p, T("Sustituye la pista en inglés y en japonés, y se repite al acabar. Usa solo audio que tengas derecho a usar.",
                    "Replaces the track in English and Japanese, and repeats when it ends. Use only audio you have the right to use."),
               bg=g.C["card"], wrap=860).pack(fill="x", pady=(0, 8))

    # ---------------------------------------------------------------- datos
    def _personajes(self):
        import ps2_b3
        return sorted(ps2_b3.PERSONAJES)

    def _elegir_carpeta(self):
        from tkinter import filedialog
        d = filedialog.askdirectory(parent=self.app.root)
        if d:
            self.copia_v.set(d)

    def _comprobar(self):
        import ps2_b3
        g, T, ruta = self.g, self.g.T, self.copia_v.get().strip()
        self.copia_ok = False
        if not ruta:
            self.copia_lbl.configure(text=T("Vale una .iso o la carpeta del disco. Si tienes un .chd, conviértelo antes (chdman).",
                                            "An .iso or the disc folder work. If you have a .chd, convert it first (chdman)."), fg=g.C["dim"])
            return
        try:
            c = ps2_b3.Copia(ruta)
        except ps2_b3.CopiaNoValida as e:
            self.copia_lbl.configure(text=str(e), fg=g.C["err"])
            return
        self.copia_ok = True
        self.app.settings["ps2_copia"] = ruta
        self.app.save()
        self.copia_lbl.configure(text=T("Copia correcta: Budokai 3 Greatest Hits USA (SLUS-20998).",
                                        "Valid copy: Budokai 3 Greatest Hits USA (SLUS-20998)."), fg=g.C["ok"])
        if not self.m_cb.cget("values"):
            self.m_cb.configure(values=self._lista_pistas(c))
        c.cerrar()
        if not self.salida_v.get().strip():
            base = os.path.dirname(os.path.abspath(ruta.rstrip("/\\")))
            self.salida_v.set(os.path.join(base, "Budokai 3 con mods.iso"))

    def _lista_pistas(self, copia):
        g, salen = self.g, []
        for n, seg in copia.pistas():
            nom = PISTAS.get(n)
            salen.append("%d  %d:%02d%s" % (n, seg // 60, seg % 60, "  " + nom[0 if g.LANG == "es" else 1] if nom else ""))
        return salen

    def _pista_original(self):
        """Saca a la cache la pista elegida para oirla antes de sustituirla (de la copia del jugador)."""
        import ps2_b3
        n = self.m_pista.get().strip().split(" ")[0]
        if not n.isdigit() or not self.copia_ok:
            return None
        ruta = os.path.join(vistas.cache_dir(), "pista_%s.adx" % n)
        if not os.path.exists(ruta):
            c = ps2_b3.Copia(self.copia_v.get().strip())
            with open(ruta, "wb") as f:
                f.write(c.entrada("adx_usa.afs", int(n)))
            c.cerrar()
        return ruta

    def refrescar(self):
        import ps2_b3
        g, T = self.g, self.g.T
        self.lista.delete(*self.lista.get_children())
        dup = {}
        for nom, on, tabla in ps2_b3.lista_mods(self.mods):
            partes = []
            for afs, ent in sorted(tabla.items()):
                partes.append("%s: %s" % (afs, ", ".join(map(str, sorted(ent)[:6])) + (" …" if len(ent) > 6 else "")))
                if on:
                    for n in ent:
                        dup.setdefault((afs, n), []).append(nom)
            self.lista.insert("", "end", iid=nom, text=nom, values=(T("encendido", "on") if on else T("apagado", "off"), "; ".join(partes)))
        choques = sorted({m for v in dup.values() if len(v) > 1 for m in v})
        self.aviso_mods.configure(
            text=(T("Estos mods cambian lo mismo; gana el último por orden alfabético: %s", "These mods change the same thing; the last alphabetically wins: %s") % ", ".join(choques)) if choques else "",
            fg=g.C["warn"])
        self._comprobar()

    def _alternar(self):
        sel = self.lista.selection()
        if not sel:
            return
        marca = os.path.join(self.mods, sel[0], ".disabled")
        if os.path.exists(marca):
            os.remove(marca)
        else:
            open(marca, "w").close()
        self.refrescar()

    def _pide_copia(self):
        if self.copia_ok:
            return True
        self.app.info(self.g.T("Elige primero tu copia del juego (paso 1).", "Pick your game copy first (step 1)."))
        return False

    def t_dir(self):
        return os.path.join(self.tmp, nombre_mod(self.t_quien.get() or "x"), "forma%d" % self.t_forma.get())

    def _t_entrada(self):
        import ps2_b3
        formas = ps2_b3.PERSONAJES[self.t_quien.get()][0]
        return formas[min(max(self.t_forma.get(), 1), len(formas)) - 1]

    def _sacar(self):
        T = self.g.T
        if not self._pide_copia():
            return
        if not self.t_quien.get():
            return self.app.info(T("Elige un personaje.", "Pick a character."))
        d = self.t_dir()
        if os.path.isdir(d) and os.listdir(d):
            if not self.app.ask(T("Ya sacaste estas texturas. ¿Volver a sacarlas? Perderías tus cambios en esa carpeta.",
                                  "You already extracted these textures. Extract again? Your changes in that folder would be lost.")):
                self._ver_galeria()
                return
        os.makedirs(d, exist_ok=True)
        self.page.run(self._py("texturas", self.copia_v.get().strip(), str(self._t_entrada()), d),
                      T("Sacar texturas", "Extract textures"), lambda j: self._ver_galeria(), self.estado,
                      T("Texturas listas: edítalas y crea el mod.", "Textures ready: edit them and create the mod."))

    def _ver_galeria(self):
        self.galeria.cargar(self.t_dir())

    def _hacer_textura(self):
        T = self.g.T
        if not self._pide_copia() or not self.t_quien.get():
            return
        mod = nombre_mod(self.t_mod.get()) or "textura_" + nombre_mod(self.t_quien.get())
        self.page.run(self._py("textura", self.copia_v.get().strip(), str(self._t_entrada()), self.t_dir(), mod),
                      T("Crear mod de textura", "Create texture mod"), lambda j: self.refrescar(), self.estado,
                      T("Mod creado. Enciéndelo y monta la ISO.", "Mod created. Enable it and build the ISO."))

    def _hacer_personaje(self):
        T = self.g.T
        a, b = self.p_quien.get(), self.p_por.get()
        if not self._pide_copia() or not a or not b:
            return
        if a == b:
            return self.app.info(T("Elige dos personajes distintos.", "Pick two different characters."))
        self.page.run(self._py("personaje", self.copia_v.get().strip(), a, b, nombre_mod("%s_como_%s" % (a, b))),
                      T("Crear mod de personaje", "Create character mod"), lambda j: self.refrescar(), self.estado,
                      T("Mod creado. Enciéndelo y monta la ISO.", "Mod created. Enable it and build the ISO."))

    def _hacer_musica(self):
        T = self.g.T
        audio, pista = self.m_audio.get().strip(), self.m_pista.get().strip().split(" ")[0]
        if not os.path.isfile(audio):
            return self.app.info(T("Elige el archivo de audio.", "Pick the audio file."))
        if not pista.isdigit():
            return self.app.info(T("Elige la pista o escribe su número.", "Pick the track or type its number."))
        mod = nombre_mod(self.m_mod.get()) or "musica_" + pista
        self.page.run(self._py("musica", audio, pista, mod), T("Crear mod de música", "Create music mod"),
                      lambda j: self.refrescar(), self.estado, T("Mod creado. Enciéndelo y monta la ISO.", "Mod created. Enable it and build the ISO."))

    def _montar(self):
        g, T = self.g, self.g.T
        salida = self.salida_v.get().strip()
        if not self._pide_copia():
            return
        if not salida.lower().endswith(".iso"):
            return self.app.info(T("Elige dónde crear la ISO (debe acabar en .iso).", "Choose where to create the ISO (must end in .iso)."))
        if os.path.exists(salida) and not self.app.ask(T("Ya existe y se sustituirá: %s", "It exists and will be replaced: %s") % salida):
            return
        self.app.settings["ps2_salida"] = salida
        self.app.save()
        self.page.run(self._py("montar", self.copia_v.get().strip(), salida), T("Montar ISO", "Build ISO"),
                      None, self.estado, T("ISO lista. Ábrela en PCSX2.", "ISO ready. Open it in PCSX2."))
