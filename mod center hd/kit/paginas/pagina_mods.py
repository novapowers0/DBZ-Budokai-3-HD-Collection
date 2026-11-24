# -*- coding: utf-8 -*-
"""Mis mods (KIT-10, maquetas studies 5/ENTREGAS/KIT_VISUAL/07_mis_mods.png y 08_avanzado.png).

Modo fácil: lista con interruptor (el marcador .disabled de siempre, con Deshacer), tipo legible, estado («visto en el
juego» / «sin probar» / choque), Abrir · Compartir · Deshacer, «Volver al juego limpio», «Instalar .zip», «Probar en el
juego» (kit/probar.py tal cual) y la última prueba guardada.
Modo avanzado: la lista de antes (tabla) y, al abrir un mod, su árbol de contenido, la sección elegida con los
editores que ya existen, «Validar mod» (kit/validar.py) y la orden de consola de cada acción, copiable.
La lógica sin interfaz está en kit/mis_mods.py, kit/choques.py y kit/validar.py."""
from __future__ import annotations

import json
import queue
import threading

from kit import choques, mis_mods, nucleo, validar
from kit import probar as prueba_juego
from kit.nucleo import *  # noqa: F401,F403
from kit.paginas.dialogos import NewModDialog, TextEditor, TomlFormDialog  # noqa: E402


# --------------------------------------------------------------------------------- mods
TYPE_NAMES = {k: v for k, v in mis_mods.TIPOS.items()}
TYPE_NAMES.update({"generado": ("Generado (no tocar)", "Generated (do not edit)")})


def type_name(t):
    es, en = TYPE_NAMES.get(t, (t or "?", t or "?"))
    return T(es, en)


PILL = {"visto": ("#1E3A26", C["ok"]), "sin_probar": ("#3A3217", C["warn"]), "choque": ("#3E1E1E", C["err"]),
        "apagado": (C["frame"], C["dim"])}
MARCA = {"error": ("✕", C["err"]), "aviso": ("⚠", C["warn"]), "ok": ("✓", C["ok"])}


def _enlace(parent, text, tip, command, bg, fg=None):
    """Acción como enlace de texto (Abrir · Compartir · Deshacer de la maqueta)."""
    b = tk.Button(parent, text=text, command=command, bg=bg, relief="flat", bd=0, fg=fg or C["dim"],
                  activebackground=bg, activeforeground=C["text"], font=F["small"], cursor="hand2", padx=3, pady=0)
    Tooltip(b, tip)
    return b


def _b(parent, text, tip, command, style=None, **kw):
    b = ttk.Button(parent, text=text, command=command, **({"style": style} if style else {}), **kw)
    Tooltip(b, tip)
    return b


class Interruptor(tk.Canvas):
    """Interruptor encendido / apagado (dibujado: tkinter no trae uno)."""

    def __init__(self, master, on, command, bg):
        super().__init__(master, width=44, height=24, bg=bg, highlightthickness=0, bd=0, cursor="hand2")
        col = C["accent"] if on else C["active"]
        for x in (2, 22):
            self.create_oval(x, 2, x + 20, 22, fill=col, outline=col)
        self.create_rectangle(12, 2, 32, 22, fill=col, outline=col)
        k = 24 if on else 4
        self.create_oval(k, 4, k + 16, 20, fill="#FFFFFF" if on else C["dim"], outline="")
        self.bind("<Button-1>", lambda _e: command())


class ModsPage(Page):
    key = "mods"

    def build(self):
        self.mods, self.images, self.lista_choques, self.prueba = [], {}, [], None
        self.abierto, self.seccion, self.validacion, self.ocupado = None, "datos", {}, False
        self.search = tk.StringVar(master=self)
        self.search.trace_add("write", lambda *_: self.fill() if getattr(self, "tree", None) else None)
        self.orden = tk.StringVar(master=self, value="")
        self.cola = queue.Queue()
        self.status = None

    def on_show(self):
        self.refresh()

    def focus_item(self, mod=None, abrir=False, **_):
        if abrir and mod:
            self.abierto, self.seccion = mod, "datos"
        self.refresh()

    # ------------------------------------------------------------------ datos
    def refresh(self):
        self.mods = scan_mods(self.env.mods)
        for m in self.mods:
            m["tipo"] = mis_mods.tipo(m["dir"], m["type"])
        self.lista_choques = choques.choques(self.env.mods, nucleo.LANG == "en")
        self.prueba = mis_mods.leer_prueba(self.env)
        self.dibujar()

    def mod(self, name):
        return next((m for m in self.mods if m["name"] == name), None)

    def dibujar(self):
        for w in self.winfo_children():
            if not isinstance(w, tk.Toplevel):
                w.destroy()
        self.tree = None
        m = self.mod(self.abierto) if self.abierto else None
        if m is not None and self.adv:
            self._detalle(m)
        elif self.adv:
            self.abierto = None
            self._lista_avanzada()
        else:
            self._lista_facil()

    def _orden(self, cmd):
        """La orden de consola equivalente: en la caja copiable y en el Registro (como app.run)."""
        txt = cmd if isinstance(cmd, str) else cmdline(cmd)
        self.orden.set("$ " + txt)
        return txt

    def run(self, cmd, title, done=None, status=None, ok_text=None, quiet=False, cwd=None):
        self._orden(cmd)
        return super().run(cmd, title, done, status, ok_text, quiet, cwd)

    def _log_orden(self, script, args):
        cmd = [self.env.python, os.path.join(HERE, "kit", script)] + [str(a) for a in args]
        self.app.log_text("$ %s\n" % self._orden(cmd), "cmd")

    def _caja_orden(self, parent, bg):
        f = tk.Frame(parent, bg=C["log_bg"], highlightthickness=1, highlightbackground=C["line"])
        e = tk.Entry(f, textvariable=self.orden, font=F["mono"], bg=C["log_bg"], fg="#C9CED8", relief="flat",
                     readonlybackground=C["log_bg"], state="readonly", bd=0)
        e.pack(side="left", fill="x", expand=True, padx=10, pady=8)
        _b(f, T("Copiar", "Copy"), T("Copia la orden para repetirla en una consola o en un lote.",
                                     "Copies the command to repeat it in a console or a batch."),
           lambda: (self.clipboard_clear(), self.clipboard_append(self.orden.get().lstrip("$ "))),
           "Small.TButton").pack(side="right", padx=6)
        if not self.orden.get():
            self.orden.set(T("(aquí sale la orden de consola de la última acción)",
                             "(the console command of the last action shows here)"))
        hint(parent, T("Cada acción del Kit enseña su orden de consola (se copia con un clic), para repetirla en lote.",
                       "Every Kit action shows its console command (one click copies it), to repeat it in a batch."),
             bg=bg).pack(side="bottom", fill="x", pady=(4, 0))
        f.pack(side="bottom", fill="x")
        return f

    # ------------------------------------------------------------------ modo fácil (maqueta 07)
    def _lista_facil(self):
        on = sum(1 for m in self.mods if m["enabled"])
        self.header(T("Mis mods", "My mods"), T("%d instalados · %d activos", "%d installed · %d on") % (len(self.mods), on))
        hr = self.header_right
        _b(hr, "▷  " + T("Probar en el juego", "Test in the game"),
           T("Monta tus mods, abre el juego hasta el menú y te dice en palabras llanas qué se ha cargado.",
             "Builds your mods, opens the game up to the menu and tells you in plain words what loaded."),
           self.probar_juego, "Accent.TButton").pack(side="right", padx=(8, 0))
        _b(hr, T("Instalar .zip", "Install .zip"), T("Instala un mod que te han pasado en un .zip (sin pisar nada).",
                                                     "Installs a mod you were given as a .zip (nothing is overwritten)."),
           lambda: self.app.goto("w_zip")).pack(side="right", padx=(8, 0))
        _b(hr, T("Volver al juego limpio", "Back to the clean game"),
           T("Apaga todos los mods de una vez. No borra nada: «Deshacer» los vuelve a encender.",
             "Turns every mod off at once. Nothing is deleted: 'Undo' turns them back on."),
           self.juego_limpio).pack(side="right", padx=(8, 0))
        main = tk.Frame(self, bg=C["bg"])
        main.pack(fill="both", expand=True, padx=22, pady=(0, 10))
        der = tk.Frame(main, bg=C["bg"], width=440)
        der.pack(side="right", fill="y", padx=(14, 0))
        der.pack_propagate(False)
        izq = tk.Frame(main, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
        izq.pack(side="left", fill="both", expand=True)
        pie = tk.Frame(izq, bg=C["card"])
        pie.pack(side="bottom", fill="x", padx=12, pady=(4, 10))
        self.status = self.status_label(pie)
        self.status.pack(side="left", fill="x", expand=True)
        _b(pie, "↶  " + T("Deshacer último paso", "Undo last step"),
           T("Devuelve lo último que cambió el Kit (también «Volver al juego limpio»).",
             "Reverts the last thing the Kit changed (also 'Back to the clean game')."), self.undo).pack(side="right")
        for c in [c for c in self.lista_choques if c["nivel"] == "error"][:3]:
            self._caja_choque(izq, c)
        sf = ScrollFrame(izq, bg=C["card"])
        sf.pack(fill="both", expand=True, padx=(12, 4), pady=12)
        if not self.mods:
            label(sf.inner, T("Aún no tienes mods. Crea uno desde el Inicio o instala un .zip.",
                              "You have no mods yet. Make one from Home or install a .zip.") if os.path.isdir(self.env.mods)
                  else T("No hay carpeta mods todavía: %s", "No mods folder yet: %s") % self.env.mods,
                  fg=C["dim"], bg=C["card"]).pack(fill="x", pady=10)
        for m in self.mods:
            self._fila(sf.inner, m)
        self._ultima_prueba(der)

    def _fila(self, parent, m):
        est, txt = mis_mods.estado(m, self.prueba, self.lista_choques, nucleo.LANG == "en")
        f = tk.Frame(parent, bg=C["card_hi"], highlightthickness=1,
                     highlightbackground=C["err"] if est == "choque" else C["line"])
        f.pack(fill="x", pady=(0, 8), padx=(0, 6))
        Interruptor(f, m["enabled"], lambda m=m: self.toggle(m), C["card_hi"]).pack(side="left", padx=(12, 10), pady=12)
        acc = tk.Frame(f, bg=C["card_hi"])
        acc.pack(side="right", padx=(6, 10))
        for texto, tip, fn in ((T("Abrir", "Open"), T("Abre el mod con todas sus partes (modo avanzado).",
                                                       "Opens the mod with all its parts (advanced mode)."), self.abrir),
                               (T("Compartir", "Share"), T("Crea un .zip del mod para pasárselo a otra persona.",
                                                           "Makes a .zip of the mod to give to someone else."),
                                self.compartir),
                               ("↶ " + T("Deshacer", "Undo"), T("Deshace el último cambio del Kit en este mod.",
                                                                "Undoes the Kit's last change to this mod."),
                                self.deshacer_mod)):
            if acc.winfo_children():
                tk.Label(acc, text="·", bg=C["card_hi"], fg=C["dim"], font=F["small"]).pack(side="left")
            _enlace(acc, texto, tip, lambda m=m, fn=fn: fn(m), C["card_hi"],
                    C["blue"] if fn == self.deshacer_mod else None).pack(side="left")
        bgp, fgp = PILL[est]
        tk.Label(f, text=txt, bg=bgp, fg=fgp, font=F["bold_sm"], padx=8, pady=2).pack(side="right", padx=8)
        tf = tk.Frame(f, bg=C["card_hi"])
        tf.pack(side="left", fill="x", expand=True, pady=8)
        label(tf, m["display"] or m["name"], font=F["bold"], bg=C["card_hi"],
              fg=C["text"] if m["enabled"] else C["dim"]).pack(fill="x")
        label(tf, type_name(m["tipo"]), fg=C["dim"], font=F["small"], bg=C["card_hi"]).pack(fill="x")

    def _caja_choque(self, parent, c):
        f = tk.Frame(parent, bg=C["card"], highlightthickness=1, highlightbackground=C["err"])
        f.pack(side="bottom", fill="x", padx=12, pady=(0, 8))
        tit = {"textura": T("Dos mods cambian las mismas imágenes", "Two mods change the same images"),
               "receta": T("Dos mods cambian el mismo movimiento", "Two mods change the same move"),
               "plaza": T("Dos personajes piden la misma plaza", "Two characters ask for the same slot")}.get(
            c["tipo"], T("Dos mods cambian lo mismo", "Two mods change the same thing"))
        if len(c["pierden"]) > 1:
            tit = tit.replace(T("Dos", "Two"), T("Varios", "Several"), 1)
        label(f, tit, fg=C["err"], font=F["bold"], bg=C["card"]).pack(fill="x", padx=12, pady=(8, 0))
        label(f, c["texto"], bg=C["card"], wrap=640).pack(fill="x", padx=12)
        b = tk.Frame(f, bg=C["card"])
        b.pack(fill="x", padx=8, pady=(0, 6))
        _enlace(b, T("Ver cuáles", "See which"), T("Lista lo que cambian los dos.", "Lists what both change."),
                lambda: self.app.info("\n".join(c["claves"][:40]) + ("\n…" if len(c["claves"]) > 40 else "")),
                C["card"], C["accent"]).pack(side="left")
        for p in c["pierden"][:1] + [c["gana"]]:
            tk.Label(b, text="·", bg=C["card"], fg=C["dim"], font=F["small"]).pack(side="left")
            _enlace(b, T("Apagar «%s»", "Turn off '%s'") % p,
                    T("El juego no tiene un orden de prioridad que se pueda cambiar: se resuelve apagando uno.",
                      "The game has no priority order you can change: it is solved by turning one off."),
                    lambda p=p: self.toggle(self.mod(p)), C["card"], C["accent"]).pack(side="left")

    def _ultima_prueba(self, der):
        card = Card(der, T("Última prueba en el juego", "Last test in the game"))
        card.pack(fill="x")
        b = card.body
        p = self.prueba
        if not p:
            label(b, T("Aún no has probado tus mods en el juego. Pulsa «Probar en el juego»: el Kit lo abre hasta el "
                       "menú y te dice qué se ha cargado.", "You have not tested your mods in the game yet. Press 'Test "
                       "in the game': the Kit opens it up to the menu and tells you what loaded."),
                  fg=C["dim"], bg=C["card"], wrap=390).pack(fill="x")
            return
        col = {"ok": C["ok"], "aviso": C["warn"]}.get(p.get("estado"), C["err"])
        label(b, ("✓ " if p.get("estado") == "ok" else "⚠ ") + p.get("frase", ""), fg=col, bg=C["card"],
              wrap=390).pack(fill="x", pady=(0, 4))
        for ln in p.get("lineas", []):
            label(b, ln, fg=C["err"] if ln.startswith("✕") else C["text"], bg=C["card"], wrap=390).pack(fill="x")
        label(b, T("Probado el %s", "Tested on %s") % p.get("fecha", "?"), fg=C["dim"], font=F["small"],
              bg=C["card"]).pack(fill="x", pady=(6, 0))

    # ------------------------------------------------------------------ modo avanzado: lista (la de antes)
    def _lista_avanzada(self):
        self.header(T("Mis mods", "My mods"),
                    T("Un mod está activo si su casilla está marcada. Desactivar no borra nada: solo deja de "
                      "cargarse. «Abrir» enseña todo el mod, su validación y la orden de consola de cada acción.",
                      "A mod is active when ticked. Disabling deletes nothing: it just stops loading. 'Open' shows "
                      "the whole mod, its validation and the console command of every action."))
        hr = self.header_right
        _b(hr, "⟳  " + T("Refrescar", "Refresh"), T("Vuelve a leer la carpeta mods.", "Reads the mods folder again."),
           self.refresh).pack(side="right")
        _b(hr, T("Abrir carpeta mods", "Open mods folder"), T("Abre la carpeta mods en el explorador.",
                                                             "Opens the mods folder in Explorer."),
           lambda: open_path(self.env.mods if os.path.isdir(self.env.mods) else self.env.game)).pack(side="right", padx=6)
        _b(hr, "✚  " + T("Nuevo mod…", "New mod…"), T("Crea una carpeta de mod vacía con su ficha.",
                                                     "Creates an empty mod folder with its card."),
           lambda: NewModDialog(self.app, self)).pack(side="right")
        _b(hr, T("Aplicar a varios mods…", "Apply to several mods…"),
           T("Validar, montar o empaquetar varios mods de una vez.", "Validate, build or pack several mods at once."),
           self.varios).pack(side="right", padx=6)
        main = tk.Frame(self, bg=C["bg"])
        main.pack(fill="both", expand=True, padx=22, pady=(0, 12))
        left = tk.Frame(main, bg=C["bg"])
        left.pack(side="left", fill="both", expand=True)
        sr = tk.Frame(left, bg=C["bg"])
        sr.pack(fill="x", pady=(0, 6))
        label(sr, "⌕", fg=C["dim"], font=F["h3"]).pack(side="left")
        ttk.Entry(sr, textvariable=self.search, width=30).pack(side="left", padx=6)
        self.count = label(sr, "", fg=C["dim"])
        self.count.pack(side="left", padx=8)
        hint(sr, T("Clic en «Estado» (o barra espaciadora) = activar / desactivar · doble clic = abrir",
                   "Click 'Status' (or space) = enable / disable · double click = open"), bg=C["bg"]).pack(side="right")
        bottom = tk.Frame(left, bg=C["bg"])
        bottom.pack(side="bottom", fill="x", pady=(8, 0))
        self._caja_orden(left, C["bg"]).pack_configure(pady=(8, 0))
        cols = ("estado", "tipo", "carpeta", "autor", "version")
        tf = tk.Frame(left, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
        tf.pack(fill="both", expand=True)
        self.tree = ttk.Treeview(tf, columns=cols, show="tree headings", style="Icons.Treeview", selectmode="browse")
        self.tree.heading("#0", text=T("Mod", "Mod"))
        self.tree.column("#0", width=260, minwidth=160, stretch=True)
        heads = {"estado": (T("Estado", "Status"), 140), "tipo": (T("Tipo", "Type"), 120),
                 "carpeta": (T("Carpeta", "Folder"), 150), "autor": (T("Autor", "Author"), 110),
                 "version": (T("Versión", "Version"), 60)}
        for c in cols:
            self.tree.heading(c, text=heads[c][0])
            self.tree.column(c, width=heads[c][1], minwidth=50, stretch=c in ("carpeta", "autor", "tipo"))
        vsb = ttk.Scrollbar(tf, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        self.tree.tag_configure("off", foreground=C["dim"])
        self.tree.tag_configure("gen", foreground=C["blue"])
        self.tree.tag_configure("choque", foreground=C["err"])
        self.tree.bind("<<TreeviewSelect>>", lambda _e: self.show_details())
        self.tree.bind("<ButtonRelease-1>", self._click)
        self.tree.bind("<Double-1>", lambda _e: self.selected() and self.abrir(self.selected()))
        self.tree.bind("<space>", lambda _e: self.toggle())
        _b(bottom, "⟳  " + T("Reconstruir personajes nuevos", "Rebuild new characters"),
           T("Monta otra vez los personajes y trajes nuevos (lo mismo que JUGAR).",
             "Builds the new characters and costumes again (the same as PLAY)."), self.rebuild).pack(side="left")
        _b(bottom, "↶  " + T("Deshacer último paso", "Undo last step"), T("Devuelve lo último que cambió el Kit.",
                                                                        "Reverts the last thing the Kit changed."),
           self.undo).pack(side="left", padx=(8, 0))
        _b(bottom, T("Volver al juego limpio", "Back to the clean game"),
           T("Apaga todos los mods de una vez (reversible con «Deshacer»).",
             "Turns every mod off at once (reversible with 'Undo')."), self.juego_limpio).pack(side="left", padx=(8, 0))
        _b(bottom, T("Instalar .zip", "Install .zip"), T("Instala un mod en .zip sin pisar nada.",
                                                         "Installs a .zip mod without overwriting anything."),
           lambda: self.app.goto("w_zip")).pack(side="left", padx=(8, 0))
        self.status = self.status_label(bottom)
        self.status.configure(bg=C["bg"])
        self.status.pack(side="left", padx=10, fill="x", expand=True)
        self.side = Card(main, pad=16)
        self.side.configure(width=340)
        self.side.pack(side="right", fill="y", padx=(14, 0))
        self.side.pack_propagate(False)
        self.fill()

    def fill(self):
        if self.tree is None:
            return
        sel = self.selected_name()
        self.tree.delete(*self.tree.get_children())
        needle = self.search.get().lower().strip()
        shown = 0
        for m in self.mods:
            title = m["display"] or m["name"]
            if needle and needle not in (title + " " + m["name"] + " " + m["tipo"]).lower():
                continue
            img = self.images.get(m["name"])
            if img is None and m["icon"]:
                img = load_png(m["icon"], 42, 42)
                self.images[m["name"]] = img
            est, txt = mis_mods.estado(m, self.prueba, self.lista_choques, nucleo.LANG == "en")
            vals = [txt, type_name(m["tipo"]), m["name"], m["author"], m["version"]]
            tags = ("off",) if not m["enabled"] else ("choque",) if est == "choque" else (
                ("gen",) if m["tipo"] == "generado" else ())
            kw = {"image": img} if img else {}
            self.tree.insert("", "end", iid=m["name"], text="  " + title, values=vals, tags=tags, **kw)
            shown += 1
        self.count.configure(text=T("%d de %d mods", "%d of %d mods") % (shown, len(self.mods)))
        if not os.path.isdir(self.env.mods):
            self.count.configure(text=T("No hay carpeta mods todavía: %s", "No mods folder yet: %s") % self.env.mods)
        if sel and self.tree.exists(sel):
            self.tree.selection_set(sel)
            self.tree.see(sel)
        self.show_details()

    def _click(self, e):
        """Un clic en la columna Estado activa / desactiva ese mod."""
        if self.tree.identify_region(e.x, e.y) != "cell" or self.tree.identify_column(e.x) != "#1":
            return
        row = self.tree.identify_row(e.y)
        if row:
            self.tree.selection_set(row)
            self.toggle()

    def selected_name(self):
        if self.tree is None:
            return None
        s = self.tree.selection()
        return s[0] if s else None

    def selected(self):
        return self.mod(self.selected_name())

    def show_details(self):
        if self.tree is None:
            return
        for w in self.side.body.winfo_children():
            w.destroy()
        m = self.selected()
        b = self.side.body
        if not m:
            label(b, T("Elige un mod de la lista para ver qué es y activarlo o desactivarlo.",
                       "Pick a mod from the list to see what it is and enable or disable it."), fg=C["dim"],
                  bg=C["card"], wrap=300).pack(fill="x")
            return
        img = self.images.get(m["name"])
        if img:
            tk.Label(b, image=img, bg=C["card"]).pack(anchor="w")
        label(b, m["display"] or m["name"], font=F["h2"], bg=C["card"], wrap=300).pack(fill="x")
        label(b, T("Carpeta: ", "Folder: ") + m["raw"], fg=C["dim"], font=F["small"], bg=C["card"]).pack(fill="x")
        est, txt = mis_mods.estado(m, self.prueba, self.lista_choques, nucleo.LANG == "en")
        bgp, fgp = PILL[est]
        tk.Label(b, text=txt, bg=bgp, fg=fgp, font=F["bold_sm"], padx=10, pady=3).pack(anchor="w", pady=(8, 8))
        label(b, T("Tipo: ", "Type: ") + type_name(m["tipo"]), bg=C["card"]).pack(fill="x")
        if m["author"] or m["version"]:
            label(b, T("Autor: ", "Author: ") + (m["author"] or "-") + ("   v" + m["version"] if m["version"] else ""),
                  bg=C["card"]).pack(fill="x")
        if m["desc"]:
            label(b, m["desc"], fg=C["dim"], bg=C["card"], wrap=300).pack(fill="x", pady=(6, 0))
        if m["tipo"] == "generado":
            label(b, T("Lo genera «Reconstruir personajes nuevos» a partir de tus personajes: no hace falta "
                       "tocarlo.", "Built by 'Rebuild new characters' from your characters: no need to touch it."),
                  fg=C["blue"], bg=C["card"], wrap=300).pack(fill="x", pady=(6, 0))
        n, size = mod_stats(m["dir"])
        label(b, T("%d archivos, %s", "%d files, %s") % (n, human_size(size)), fg=C["dim"], font=F["small"],
              bg=C["card"]).pack(fill="x", pady=(6, 0))
        bf = tk.Frame(b, bg=C["card"])
        bf.pack(fill="x", pady=(14, 0))
        _b(bf, T("Abrir", "Open"), T("Árbol del mod, validación y órdenes de consola.",
                                     "Mod tree, validation and console commands."),
           lambda: self.abrir(m), "Accent.TButton").pack(fill="x")
        for texto, tip, fn in (
                (T("Desactivar", "Disable") if m["enabled"] else T("Activar", "Enable"),
                 T("Enciende o apaga el mod (reversible).", "Turns the mod on or off (reversible)."), self.toggle),
                (T("Abrir carpeta", "Open folder"), T("Abre la carpeta del mod.", "Opens the mod folder."),
                 lambda: open_path(m["dir"])),
                (T("Validar mod", "Validate mod"), T("Revisa todo el mod de una vez.", "Checks the whole mod at once."),
                 lambda: self.abrir(m)),
                (T("Compartir (.zip)", "Share (.zip)"), T("Crea un .zip del mod.", "Makes a .zip of the mod."),
                 lambda: self.compartir(m))):
            _b(bf, texto, tip, fn).pack(fill="x", pady=(6, 0))
        if m["tipo"] == "personaje":
            _b(bf, T("Editar personaje", "Edit character"), T("Formas y transformación en la pestaña Personajes.",
                                                              "Forms and transformation in the Characters tab."),
               lambda: self.app.goto("characters", mod=m["name"])).pack(fill="x", pady=(6, 0))
            # feedback de jugadores: "¿se podria importar como TRAJE (Vegeta GT de IW como traje de Vegeta)?"
            _b(bf, T("Convertir en traje de su personaje base", "Make it a costume of its base character"),
               T("Crea un traje del personaje base con el modelo de este port.",
                 "Makes a costume of the base character with this port's model."),
               lambda: self.as_costume(m["name"])).pack(fill="x", pady=(6, 0))
        if m["toml"]:
            _b(bf, T("Editar ajustes (formulario)", "Edit settings (form)"), T("Edita la ficha con un formulario.",
                                                                               "Edits the card with a form."),
               lambda: TomlFormDialog(self.app, m["toml"], self.refresh)).pack(fill="x", pady=(6, 0))
        files = [f for f in ("personaje.toml", "traje.toml", "stage.toml", "manifest.txt", "README.md", "roster.toml")
                 if os.path.isfile(os.path.join(m["dir"], f))]
        if files:
            _b(bf, T("Editar texto…", "Edit text…"), T("Edita los ficheros de texto del mod.",
                                                      "Edits the mod's text files."),
               lambda: TextEditor(self.app, [os.path.join(m["dir"], f) for f in files], self.refresh)).pack(
                fill="x", pady=(6, 0))

    # ------------------------------------------------------------------ modo avanzado: un mod (maqueta 08)
    def _secciones(self, m):
        """[(iid, padre, texto)] del árbol de contenido según lo que trae el mod."""
        d, out = m["dir"], []
        datos = read_toml(m["toml"]) if m["toml"] else {}
        if m["tipo"] == "personaje":
            caps = len(datos.get("capsula", []))
            out += [("ficha", "", T("Ficha del personaje", "Character card")), ("datos", "ficha", T("Datos", "Data")),
                    ("formas", "ficha", T("Formas y ki", "Forms and ki")),
                    ("modelos", "ficha", T("Modelos por forma", "Models per form")),
                    ("tecnicas", "", T("Técnicas (%d cápsulas)", "Techniques (%d capsules)") % caps),
                    ("movimientos", "tecnicas", T("Movimientos", "Moves")), ("camaras", "tecnicas", T("Cámaras", "Cameras")),
                    ("aura", "tecnicas", T("Aura y ki", "Aura and ki")), ("voces", "", T("Voces", "Voices")),
                    ("imagenes", "", T("Imágenes del selector", "Selector images"))]
        elif m["tipo"] == "traje":
            out += [("ficha", "", T("Ficha del traje", "Costume card")), ("datos", "ficha", T("Datos", "Data")),
                    ("formas", "ficha", T("Modelos por forma", "Models per form"))]
        elif m["tipo"] == "escenario":
            out += [("datos", "", T("Escenario (stage.toml)", "Stage (stage.toml)"))]
        elif m["tipo"] == "tecnica":
            out += [("movimientos", "", T("Movimientos", "Moves")), ("datos", "", T("Datos", "Data"))]
        else:
            out += [("datos", "", T("Datos", "Data"))]
        n = sum(len(fs) for _dp, _ds, fs in os.walk(d))
        out += [("archivos", "", T("Archivos (%d)", "Files (%d)") % n), ("hex", "", "Hex")]
        return out

    def _detalle(self, m):
        h = tk.Frame(self, bg=C["bg"])
        h.pack(fill="x", padx=22, pady=(12, 8))
        label(h, m["display"] or m["name"], font=F["h1"]).pack(side="left")
        tk.Label(h, text=type_name(m["tipo"]), bg=C["frame"], fg=C["dim"], font=F["bold_sm"], padx=8, pady=2).pack(
            side="left", padx=12)
        label(h, "mods/" + m["raw"], fg=C["dim"]).pack(side="left")
        _b(h, T("Montar y probar", "Build and test"), T("Monta este mod, abre el juego hasta el menú y dice qué cargó.",
                                                        "Builds this mod, opens the game to the menu and says what "
                                                        "loaded."), lambda: self.probar_juego([m["name"]]),
           "Accent.TButton").pack(side="right", padx=(8, 0))
        _b(h, T("Validar mod", "Validate mod"), T("Revisa todo de una vez: claves, archivos, huesos, límites y choques.",
                                                  "Checks everything at once: keys, files, bones, limits and clashes."),
           lambda: self.validar(m, True), "Blue.TButton").pack(side="right", padx=(8, 0))
        _b(h, T("Abrir en modo fácil", "Open in easy mode"), T("El mismo mod en la vista sencilla.",
                                                               "The same mod in the simple view."),
           lambda: self.modo_facil(m)).pack(side="right", padx=(8, 0))
        _b(h, "←  " + T("Lista", "List"), T("Vuelve a la lista de mods.", "Back to the mods list."),
           self.cerrar, "Ghost.TButton").pack(side="right")
        cuerpo = tk.Frame(self, bg=C["bg"])
        cuerpo.pack(fill="both", expand=True, padx=22, pady=(0, 10))
        arbol = Card(cuerpo, T("Contenido del mod", "Mod contents"), pad=12)
        arbol.configure(width=240)
        arbol.pack(side="left", fill="y")
        arbol.pack_propagate(False)
        self.arbol = ttk.Treeview(arbol.body, show="tree", selectmode="browse")
        self.arbol.pack(fill="both", expand=True)
        secs = self._secciones(m)
        for iid, padre, texto in secs:
            self.arbol.insert(padre, "end", iid=iid, text=texto, open=True)
        if self.seccion not in [s[0] for s in secs]:
            self.seccion = "datos"
        self.arbol.selection_set(self.seccion)
        self.arbol.bind("<<TreeviewSelect>>", lambda _e: self._ir(m))
        der = tk.Frame(cuerpo, bg=C["bg"], width=330)
        der.pack(side="right", fill="y", padx=(14, 0))
        der.pack_propagate(False)
        img = next((os.path.join(m["dir"], "ui", "_vista", f) for f in ("retrato_p1.png", "icono.png")
                    if os.path.isfile(os.path.join(m["dir"], "ui", "_vista", f))), "")
        ph = load_png(img, 300, 220) if img else None
        if ph is not None:
            fc = tk.Frame(der, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
            fc.pack(fill="x", pady=(0, 12))
            lb = tk.Label(fc, image=ph, bg=C["card"])
            lb.image = ph
            lb.pack(pady=8)
        self.panel_val = Card(der, pad=12)
        self.panel_val.pack(fill="both", expand=True)
        self.centro = Card(cuerpo, pad=16)
        self.centro.pack(side="left", fill="both", expand=True, padx=(14, 0))
        self._caja_orden(self.centro.body, C["card"])
        self.seccion_f = tk.Frame(self.centro.body, bg=C["card"])
        self.seccion_f.pack(fill="both", expand=True)
        self._seccion(m)
        if m["name"] not in self.validacion:
            self.validar(m, False)
        else:
            self._pintar_validacion(m)

    def _ir(self, m):
        s = self.arbol.selection()
        if s:
            self.seccion = s[0]
            self._seccion(m)

    def _tabla(self, parent, cabecera, filas):
        g = tk.Frame(parent, bg=C["card"])
        g.pack(fill="x", pady=(4, 10))
        for j, c in enumerate(cabecera):
            label(g, c, fg=C["dim"], font=F["bold_sm"], bg=C["card"]).grid(row=0, column=j, sticky="w", padx=(0, 18))
        for i, fila in enumerate(filas, 1):
            for j, v in enumerate(fila):
                lb = label(g, str(v), bg=C["frame"] if j else C["card"], font=F["bold"] if not j else F["body"],
                           padx=6 if j else 0, pady=2)
                lb.grid(row=i, column=j, sticky="w", padx=(0, 18), pady=3)
        return g

    def _editar(self, parent, m, fichero):
        ruta = os.path.join(m["dir"], fichero)
        if not os.path.isfile(ruta):
            return
        _b(parent, T("Editar %s", "Edit %s") % fichero, T("Lo abre en tu editor de texto (se guarda tal cual).",
                                                          "Opens it in your text editor (saved as is)."),
           lambda: open_path(ruta)).pack(side="left", padx=(0, 8))

    def _seccion(self, m):
        for w in self.seccion_f.winfo_children():
            w.destroy()
        p, s = self.seccion_f, self.seccion
        datos = read_toml(m["toml"]) if m["toml"] else {}
        ficha = datos.get("personaje" if m["tipo"] == "personaje" else "traje", {})
        ficha = (ficha[0] if ficha else {}) if isinstance(ficha, list) else ficha
        tit = dict((i, t) for i, _p, t in self._secciones(m)).get(s, "")
        label(p, tit, font=F["h3"], bg=C["card"]).pack(fill="x", pady=(0, 6))
        acc = tk.Frame(p, bg=C["card"])
        if s == "datos":
            if m["tipo"] == "escenario":
                st = os.path.join(m["dir"], "stage.toml")
                with open(st, encoding="utf-8", errors="replace") as fh:
                    kv = [ln.split("#")[0].split("=", 1) for ln in fh if "=" in ln and not ln.lstrip().startswith("#")]
                self._tabla(p, [T("Clave", "Key"), T("Valor", "Value")], [(k.strip(), v.strip()[:60]) for k, v in kv][:24])
            elif m["tipo"] == "tecnica" or not ficha:
                man = read_manifest(m["dir"])
                self._tabla(p, [T("Clave", "Key"), T("Valor", "Value")], list(man.items())[:20] or [("-", "-")])
            else:
                filas = [(k, json.dumps(v, ensure_ascii=False)[:70]) for k, v in ficha.items()][:22]
                self._tabla(p, [T("Clave", "Key"), T("Valor", "Value")], filas)
                _b(acc, T("Editar (formulario)", "Edit (form)"), T("Edita la ficha con un formulario.",
                                                                  "Edits the card with a form."),
                   lambda: TomlFormDialog(self.app, m["toml"], self.refresh)).pack(side="left", padx=(0, 8))
            for f in ("personaje.toml", "traje.toml", "stage.toml", "manifest.txt"):
                self._editar(acc, m, f)
        elif s in ("formas", "modelos"):
            filas = self._formas(m, ficha)
            if m["tipo"] == "personaje":
                self._tabla(p, [T("Forma", "Form"), T("Modelo", "Model"), T("Ki base", "Base ki"), "Aura"], filas)
                _b(acc, T("Abrir en Personajes", "Open in Characters"), T("Formas y transformación (editor del Kit).",
                                                                        "Forms and transformation (Kit editor)."),
                   lambda: self.app.goto("characters", mod=m["name"])).pack(side="left", padx=(0, 8))
            else:
                self._tabla(p, [T("Forma", "Form"), T("Modelo", "Model")], filas)
                _b(acc, T("Cambio de modelo", "Model swap"), T("Herramienta de cambio de modelo.", "Model swap tool."),
                   lambda: self.app.goto("swap")).pack(side="left", padx=(0, 8))
            self._editar(acc, m, "personaje.toml" if m["tipo"] == "personaje" else "traje.toml")
        elif s == "tecnicas":
            caps = datos.get("capsula", [])
            self._tabla(p, [T("Cápsula", "Capsule"), T("Tipo", "Type"), T("Forma", "Form")],
                        [(c.get("nombre", "?"), c.get("tipo", ""), c.get("forma", "")) for c in caps] or [("-", "", "")])
            _b(acc, T("Cápsulas desde el port", "Capsules from the port"),
               T("Crea las cápsulas con los nombres del juego de origen.", "Makes the capsules with the source game's "
                 "names."), lambda: self.run(cmd_capsules(self.env, m["name"], ["--importar", "auto"]),
                                             T("Cápsulas", "Capsules"), lambda _j: self.refresh(), self.status)).pack(
                side="left", padx=(0, 8))
            self._editar(acc, m, "personaje.toml")
        elif s == "movimientos":
            try:
                with open(os.path.join(m["dir"], "animaciones.json"), encoding="utf-8") as fh:
                    rs = json.load(fh)
            except (OSError, ValueError):
                rs = []
            self._tabla(p, [T("Personaje", "Character"), T("Movimiento", "Move"), T("Sale de", "Comes from")],
                        [(r["destino"]["personaje"], "%#x" % int(r["destino"]["codigo"]),
                          "%s / %#x" % (r.get("fuente", {}).get("personaje", "?"), int(r.get("fuente", {}).get("codigo", 0))))
                         for r in rs if isinstance(r, dict) and "destino" in r] or [("-", "", "")])
            _b(acc, T("Abrir el generador", "Open the generator"), T("Generador de animaciones (Biblioteca).",
                                                                     "Animation generator (Library)."),
               lambda: self.app.goto("biblioteca")).pack(side="left", padx=(0, 8))
        elif s == "camaras":
            self._tabla(p, [T("Clave", "Key"), T("Valor", "Value")], [("camara", ficha.get("camara", T("la del donante",
                                                                                                    "the donor's")))])
            _b(acc, T("Abrir el Studio", "Open the Studio"), T("Cámaras de técnicas y definitivas.",
                                                               "Technique and ultimate cameras."),
               lambda: self.app.open_studio()).pack(side="left", padx=(0, 8))
        elif s == "aura":
            self._tabla(p, [T("Clave", "Key"), T("Valor", "Value")],
                        [(k, ficha.get(k, "-")) for k in ("aura_color", "ki_color", "aura_de", "aura_formas")])
            self._editar(acc, m, "personaje.toml")
        elif s == "voces":
            self._tabla(p, [T("Clave", "Key"), T("Valor", "Value")], [(k, ficha.get(k, "-")) for k in ("voces", "gritos")])
            self._editar(acc, m, "personaje.toml")
        elif s == "imagenes":
            ui = os.path.join(m["dir"], "ui")
            fs = sorted(f for f in os.listdir(ui) if f.lower().endswith(".png")) if os.path.isdir(ui) else []
            self._tabla(p, [T("Imagen", "Image")], [(f,) for f in fs[:20]] or [(T("(las pinta el Kit)", "(the Kit draws them)"),)])
            _b(acc, T("Poner tus imágenes", "Add your images"), T("Asistente de imágenes.", "Images wizard."),
               lambda: self.app.goto("arte")).pack(side="left", padx=(0, 8))
        elif s == "archivos":
            fs = []
            for dp, _ds, ff in os.walk(m["dir"]):
                for f in ff:
                    q = os.path.join(dp, f)
                    fs.append((os.path.relpath(q, m["dir"]).replace(os.sep, "/"), human_size(os.path.getsize(q))))
            self._tabla(p, [T("Archivo", "File"), T("Tamaño", "Size")], fs[:18] + ([("… +%d" % (len(fs) - 18), "")]
                                                                                  if len(fs) > 18 else []))
            _b(acc, T("Abrir carpeta", "Open folder"), T("Abre la carpeta del mod.", "Opens the mod folder."),
               lambda: open_path(m["dir"])).pack(side="left", padx=(0, 8))
        elif s == "hex":
            label(p, T("Editor hexadecimal de los modelos y datos del mod.", "Hex editor for the mod's models and data."),
                  fg=C["dim"], bg=C["card"]).pack(fill="x")
            _b(acc, T("Abrir Hex", "Open Hex"), T("Pestaña Hex del Kit.", "The Kit's Hex tab."),
               lambda: self.app.goto("hex")).pack(side="left", padx=(0, 8))
        else:
            label(p, T("Elige una parte del mod a la izquierda.", "Pick a part of the mod on the left."), fg=C["dim"],
                  bg=C["card"]).pack(fill="x")
        _b(acc, T("Aplicar a varios mods…", "Apply to several mods…"),
           T("Validar, montar o empaquetar varios mods de una vez.", "Validate, build or pack several mods at once."),
           lambda: self.varios([m["name"]])).pack(side="left", padx=(0, 8))
        acc.pack(fill="x", pady=(4, 10))

    def _formas(self, m, ficha):
        if m["tipo"] == "traje":
            try:
                from kit import traje  # noqa: PLC0415
                fs = traje.formas(int(ficha.get("personaje", -1)))
            except (ValueError, TypeError, OSError):
                fs = []
            mods_ = ficha.get("modelos", [])
            n = max(len(fs), len(mods_))
            return [((fs[i]["en" if nucleo.LANG == "en" else "es"] if i < len(fs) else "%d" % (i + 1)),
                     (mods_[min(i, len(mods_) - 1)] if mods_ else "-")) for i in range(n)]
        modelos = ficha.get("modelos", [])
        ki, aura = ficha.get("ki_base", []), ficha.get("aura_formas", [])
        mf = ficha.get("modelo_forma", [])
        n = max([len(ki), len(aura), len(mf), ficha.get("formas", 0) if isinstance(ficha.get("formas"), int) else 0,
                 min(len(modelos), int(ficha.get("modelos_por_traje", 1) or 1)) or 1])
        out = []
        for i in range(n):
            j = mf[i] if i < len(mf) else i
            out.append((T("Forma %d", "Form %d") % (i + 1), modelos[j] if j < len(modelos) else (modelos[-1] if modelos
                                                                                                else "-"),
                        ki[i] if i < len(ki) else "—", aura[i] if i < len(aura) else T("del donante", "donor's")))
        return out

    def validar(self, m, completo):
        """Rápida al abrir (sin huesos, recetas ni plazas) y completa en segundo plano."""
        args = [m["dir"], "--mods", self.env.mods] + (["--rapido"] if not completo else [])
        if completo:
            self._log_orden("validar.py", args)
        else:
            self._orden([self.env.python, os.path.join(HERE, "kit", "validar.py")] + args)
        try:
            self.validacion[m["name"]] = validar.validar(m["dir"], self.env.mods, None, nucleo.LANG == "en", False)
        except Exception as ex:  # noqa: BLE001  (un mod raro no tumba la pantalla)
            self.validacion[m["name"]] = [("aviso", T("No se pudo validar: %s", "Could not validate: %s") % ex, "")]
        self._pintar_validacion(m, completo and not self.app.selftest)
        if not completo or self.app.selftest:
            return
        env, ing = self.env, nucleo.LANG == "en"
        self._hilo(lambda log: validar.validar(m["dir"], env.mods, env.us, ing, True),
                   lambda r: (self.validacion.__setitem__(m["name"], r), self.alive and self.abierto == m["name"]
                              and self._pintar_validacion(m)))

    def _pintar_validacion(self, m, trabajando=False):
        b = self.panel_val.body
        for w in b.winfo_children():
            w.destroy()
        r = self.validacion.get(m["name"], [])
        top = tk.Frame(b, bg=C["card"])
        top.pack(fill="x", pady=(0, 6))
        label(top, T("Validación", "Validation"), font=F["h3"], bg=C["card"]).pack(side="left")
        for nivel, (es, en), (bg, fg) in (("ok", ("%d bien", "%d fine"), ("#1E3A26", C["ok"])),
                                          ("aviso", ("%d avisos", "%d warnings"), ("#3A3217", C["warn"])),
                                          ("error", ("%d errores", "%d errors"), ("#3E1E1E", C["err"]))):
            tk.Label(top, text=T(es, en) % sum(1 for x in r if x[0] == nivel), bg=bg, fg=fg, font=F["bold_sm"], padx=6,
                     pady=1).pack(side="left", padx=(6, 0))
        if trabajando:
            label(b, T("Comprobando huesos, movimientos y plazas…", "Checking bones, moves and slots…"), fg=C["warn"],
                  font=F["small"], bg=C["card"]).pack(fill="x")
        sf = ScrollFrame(b, bg=C["card"])
        sf.pack(fill="both", expand=True)
        for nivel, msg, det in r:
            s, col = MARCA.get(nivel, ("·", C["dim"]))
            lb = label(sf.inner, "%s %s" % (s, msg), fg=C["text"], bg=C["card"], wrap=280)
            lb.configure(fg=col if nivel != "ok" else C["text"])
            lb.pack(fill="x", pady=1)
            if det:
                Tooltip(lb, det)

    def _hilo(self, trabajo, fin):
        """trabajo(log) en un hilo; fin(resultado) en la ventana. El registro va al Registro del Kit."""
        cola = queue.Queue()

        def correr():
            try:
                cola.put(("fin", trabajo(lambda t: cola.put(("log", t)))))
            except Exception as ex:  # noqa: BLE001
                cola.put(("log", "ERROR: %s" % ex))
                cola.put(("fin", None))
        threading.Thread(target=correr, daemon=True).start()

        def sondear():
            try:
                while True:
                    k, v = cola.get_nowait()
                    if k == "log":
                        self.app.log_text(str(v).rstrip("\n") + "\n")
                    else:
                        fin(v)
                        return
            except queue.Empty:
                pass
            except tk.TclError:
                return
            self.after(200, sondear)
        self.after(200, sondear)

    # ------------------------------------------------------------------ acciones
    def abrir(self, m):
        if self.adv:
            self.abierto, self.seccion = m["name"], "datos"
            self.dibujar()
            return
        self.app.set_mode("advanced")
        self.app.goto("mods", mod=m["name"], abrir=True)

    def modo_facil(self, m):
        self.abierto = None
        self.app.set_mode("basic")
        self.app.goto("mods", mod=m["name"])

    def cerrar(self):
        self.abierto = None
        self.dibujar()

    def toggle(self, m=None):
        m = m or self.selected()
        if not m:
            return
        if m["tipo"] == "generado" and m["enabled"]:
            if not self.app.ask(T("Este mod es la lista de tus personajes nuevos (la crea el kit). Si lo desactivas, "
                                  "ninguno aparecerá en el juego. ¿Desactivar?",
                                  "This mod is the list of your new characters (the kit makes it). If you disable it "
                                  "none of them will show up in game. Disable?")):
                return
        try:
            mis_mods.poner(self.env, [m["name"]], not m["enabled"], anotar_indice)
        except OSError as ex:
            self.set_status(self.status, "err", str(ex))
            return
        self._log_orden("mis_mods.py", ["desactivar" if m["enabled"] else "activar", self.env.mods, m["name"]])
        self.app.log_text("%s: %s\n" % (m["name"], T("activado", "enabled") if not m["enabled"] else
                                        T("desactivado", "disabled")), "ok")
        self.refresh()
        if m["tipo"] in ("personaje", "traje"):
            self.set_status(self.status, "ok", T("Hecho. Los personajes se actualizan al pulsar JUGAR (o con "
                                                 "«Reconstruir personajes nuevos»). «Deshacer» lo devuelve.",
                                                 "Done. Characters update when you press PLAY (or with 'Rebuild "
                                                 "new characters'). 'Undo' reverts it."))
        else:
            self.set_status(self.status, "ok", T("Hecho. «Deshacer» lo devuelve.", "Done. 'Undo' reverts it."))

    def juego_limpio(self):
        on = [m for m in self.mods if m["enabled"]]
        if not on:
            self.set_status(self.status, "warn", T("Ya no hay ningún mod activo.", "No mod is on already."))
            return
        if not self.app.ask(T("Se apagan los %d mods activos y el juego queda como de serie. No se borra nada: "
                              "«Deshacer último paso» los vuelve a encender. ¿Seguir?",
                              "The %d active mods are turned off and the game is back to stock. Nothing is deleted: "
                              "'Undo last step' turns them back on. Continue?") % len(on)):
            return
        try:
            hechos = mis_mods.juego_limpio(self.env, anotar_indice)
        except OSError as ex:
            self.set_status(self.status, "err", str(ex))
            return
        self._log_orden("mis_mods.py", ["limpio", self.env.mods])
        self.refresh()
        self.set_status(self.status, "ok", T("Juego limpio: %d mods apagados. «Deshacer último paso» los vuelve a "
                                             "encender.", "Clean game: %d mods off. 'Undo last step' turns them back "
                                             "on.") % len(hechos))

    def compartir(self, m):
        try:
            z = mis_mods.compartir(m["dir"])
        except OSError as ex:
            self.set_status(self.status, "err", str(ex))
            return
        self._log_orden("mis_mods.py", ["compartir", m["dir"], z])
        self.set_status(self.status, "ok", T("Listo para compartir: %s", "Ready to share: %s") % z)
        if not self.app.selftest:
            open_path(os.path.dirname(z))

    def deshacer_mod(self, m):
        try:
            hechos = deshacer_ultimo(self.env, m["dir"])
        except OSError as ex:
            self.set_status(self.status, "err", str(ex))
            return
        if not hechos:
            self.set_status(self.status, "warn", T("No hay nada que deshacer en «%s».", "Nothing to undo in '%s'.") %
                            (m["display"] or m["name"]))
            return
        self.app.log_text(T("Deshecho: %s\n", "Undone: %s\n") % ", ".join(hechos), "ok")
        self.refresh()
        self.set_status(self.status, "ok", T("Deshecho el último cambio de «%s» (%d fichero/s). Lo anterior queda en "
                                             "modkit_backups.", "Undid the last change to '%s' (%d file/s). The previous "
                                             "state stays in modkit_backups.") % (m["display"] or m["name"], len(hechos)))

    def probar_juego(self, mods=None):
        """Monta (roster_build construir) y prueba con kit/probar.py tal cual; guarda el resumen para el panel."""
        env, ing = self.env, nucleo.LANG == "en"
        mods = mods or [m["name"] for m in self.mods if m["enabled"] and m["tipo"] not in ("texturas", "generado")
                        and not m["name"].startswith("_")]
        cmd = cmd_roster_build(env, True)
        self._orden(cmd)
        if self.app.selftest:
            self.app.selftest_cmds.append((T("Probar en el juego", "Test in the game"), [str(c) for c in cmd]))
            return
        if self.ocupado or self.app.jobs.busy():
            self.set_status(self.status, "warn", T("Ya hay otra tarea en marcha. Espera a que termine.",
                                                   "Another task is already running. Wait for it to finish."))
            return
        self.ocupado = True
        self.app.log_text("$ %s\n" % cmdline(cmd), "cmd")
        self.set_status(self.status, "warn", T("Montando y abriendo el juego… tarda unos minutos.",
                                               "Building and opening the game… it takes a few minutes."))

        def trabajo(log):
            rc, texto = run_sync(cmd, env.cwd(), timeout=1800)
            log("\n".join(texto.splitlines()[-25:]))
            if rc != 0:
                que, hacer, _a = errores.explicar(texto, ing)
                return "error", "%s %s" % (que, hacer), [], mods
            est, frase, _modo = prueba_juego.probar(env.mods, env.game, mods, ing, log)
            return est, frase, prueba_juego.resumen(texto, env.mods, mods, ing), mods

        def fin(r):
            self.ocupado = False
            if r is None:
                self.set_status(self.status, "err", T("La prueba falló: mira el registro.", "The test failed: check the log."))
                return
            mis_mods.guardar_prueba(env, *r)
            if self.alive:
                self.refresh()
                self.set_status(self.status, {"ok": "ok", "aviso": "warn"}.get(r[0], "err"), r[1])
        self._hilo(trabajo, fin)

    def varios(self, preelegidos=()):
        """«Aplicar a varios mods…»: validar, montar o empaquetar una selección (con sus órdenes de consola)."""
        w = tk.Toplevel(self)
        w.title(T("Aplicar a varios mods", "Apply to several mods"))
        w.configure(bg=C["bg"])
        w.geometry("560x560")
        label(w, T("Elige los mods y la acción. Abajo salen las órdenes de consola equivalentes.",
                   "Pick the mods and the action. The equivalent console commands show below."),
              fg=C["dim"], wrap=520).pack(fill="x", padx=16, pady=(12, 6))
        sf = ScrollFrame(w)
        sf.pack(fill="both", expand=True, padx=16)
        vs = {}
        for m in self.mods:
            v = tk.BooleanVar(master=w, value=m["name"] in preelegidos)
            cb = ttk.Checkbutton(sf.inner, text="%s  (%s)" % (m["display"] or m["name"], type_name(m["tipo"])), variable=v)
            Tooltip(cb, m["dir"])
            cb.pack(anchor="w")
            vs[m["name"]] = v
        accion = tk.StringVar(master=w, value="validar")
        fa = tk.Frame(w, bg=C["bg"])
        fa.pack(fill="x", padx=16, pady=6)
        for k, es, en in (("validar", "Validar", "Validate"), ("montar", "Montar", "Build"),
                          ("empaquetar", "Empaquetar (.zip)", "Pack (.zip)")):
            rb = ttk.Radiobutton(fa, text=T(es, en), value=k, variable=accion)
            Tooltip(rb, T(es, en))
            rb.pack(side="left", padx=(0, 12))
        txt = tk.Text(w, height=7, bg=C["log_bg"], fg="#C9CED8", font=F["mono"], relief="flat", wrap="none")
        txt.pack(fill="x", padx=16, pady=(0, 6))

        def ordenes():
            sel = [n for n, v in vs.items() if v.get()]
            a = accion.get()
            if a == "montar":
                cmds = [cmd_roster_build(self.env, True)]
            elif a == "validar":
                cmds = [[self.env.python, os.path.join(HERE, "kit", "validar.py"), self.mod(n)["dir"], "--mods",
                         self.env.mods] for n in sel]
            else:
                cmds = [[self.env.python, os.path.join(HERE, "kit", "mis_mods.py"), "compartir", self.mod(n)["dir"]]
                        for n in sel]
            txt.delete("1.0", "end")
            txt.insert("end", "\n".join(cmdline(c) for c in cmds))
            return sel, a, cmds

        def aplicar():
            sel, a, cmds = ordenes()
            if not sel and a != "montar":
                return
            for c in cmds:
                self.app.log_text("$ %s\n" % cmdline(c), "cmd")
            if a == "montar":
                self.run(cmds[0], T("Montar", "Build"), lambda _j: self.refresh(), self.status)
                return
            env, ing = self.env, nucleo.LANG == "en"

            def trabajo(log):
                malos = 0
                for n in sel:
                    d = self.mod(n)["dir"]
                    if a == "validar":
                        r = validar.validar(d, env.mods, env.us, ing, True)
                        e = sum(x[0] == "error" for x in r)
                        malos += bool(e)
                        log("%s: %d errores, %d avisos" % (n, e, sum(x[0] == "aviso" for x in r)))
                        for x in r:
                            if x[0] != "ok":
                                log("   %s %s" % (MARCA[x[0]][0], x[1]))
                    else:
                        log("%s -> %s" % (n, mis_mods.compartir(d)))
                return malos
            self._hilo(trabajo, lambda malos: self.set_status(self.status, "warn" if malos else "ok", T(
                "Hecho: %d mod(s) (detalle en el registro).", "Done: %d mod(s) (details in the log).") % len(sel)))
        accion.trace_add("write", lambda *_: ordenes())
        for v in vs.values():
            v.trace_add("write", lambda *_: ordenes())
        ordenes()
        fb = tk.Frame(w, bg=C["bg"])
        fb.pack(fill="x", padx=16, pady=(0, 12))
        _b(fb, T("Aplicar", "Apply"), T("Lo hace con todos los elegidos.", "Does it for all the picked ones."),
           aplicar, "Accent.TButton").pack(side="right")
        _b(fb, T("Copiar órdenes", "Copy commands"), T("Copia las órdenes para un lote (.bat).",
                                                       "Copies the commands for a batch (.bat)."),
           lambda: (w.clipboard_clear(), w.clipboard_append(txt.get("1.0", "end").strip()))).pack(side="right", padx=8)
        return w

    def revisar_mod(self, m):
        self.abrir(m)

    def as_costume(self, mod):
        """importar.py traje: el port como traje extra de su donante (mod nuevo traje_<mod>; el port no cambia)."""
        golpes_base = self.app.ask(T("Se crea un traje extra del personaje base con el modelo de este port (el port "
                                     "no se toca).\n\n¿Con los golpes del personaje base? (Sí = sus golpes y "
                                     "cápsulas; No = los del port)",
                                     "An extra costume of the base character is made with this port's model (the port "
                                     "is not touched).\n\nUse the base character's moves? (Yes = its moves and "
                                     "capsules; No = the port's)"))
        args = ["traje", mod, "--mods", self.env.mods] + (["--sin-enlace"] if golpes_base else [])
        self.run(cmd_script(self.env, "importar.py", args), T("Traje desde ", "Costume from ") + mod,
                 done=lambda job: self.refresh(), status=self.status,
                 ok_text=T("Traje creado (mod traje_%s). Se ve al pulsar JUGAR." % mod,
                           "Costume made (mod traje_%s). It shows up when you press PLAY." % mod))

    def rebuild(self):
        self.run(cmd_roster_build(self.env, True), T("Reconstruir personajes nuevos", "Rebuild new characters"),
                 lambda job: self.alive and self.refresh(), self.status,
                 T("Personajes nuevos montados.", "New characters built."))

    def undo(self):
        try:
            restaurados = deshacer_ultimo(self.env)
        except OSError as ex:
            self.set_status(self.status, "err", str(ex))
            return
        if not restaurados:
            self.set_status(self.status, "warn", T("No hay nada que deshacer.", "Nothing to undo."))
            return
        self.app.log_text(T("Deshecho: %s\n", "Undone: %s\n") % ", ".join(restaurados), "ok")
        self.refresh()
        self.set_status(self.status, "ok", T("Deshecho el último paso (%d fichero/s). Lo anterior queda en modkit_backups.",
                                             "Last step undone (%d file/s). The previous state is kept in modkit_backups.")
                        % len(restaurados))
