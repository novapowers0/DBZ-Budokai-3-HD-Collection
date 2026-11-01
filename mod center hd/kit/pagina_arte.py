# -*- coding: utf-8 -*-
"""Pantalla «Arte opcional»: elige carpeta, mira qué imagen sirve para qué y la instala como mod (motor: kit/arte.py)."""
from __future__ import annotations

import os
import tkinter as tk
from tkinter import filedialog, ttk

from kit import arte


class PaginaArte:
    """Se construye dentro de una Page de modkit_gui: g = modulo, page = la Page."""

    def __init__(self, page, g):
        self.page, self.g, self.filas = page, g, []
        T, Card, label = g.T, g.Card, g.label
        page.header(T("Arte opcional", "Optional art"),
                    T("Pon tus imágenes en el Dragon HUB y el modo Héroe. Es opcional: sin ellas el juego se ve igual que siempre.",
                      "Put your images in the Dragon HUB and Hero mode. It is optional: without them the game looks as always."))
        _sf, body = page.scroll_body()
        top = Card(body, accent=True)
        top.pack(fill="x", pady=(0, 12))
        fila = tk.Frame(top.body, bg=g.C["card"])
        fila.pack(fill="x")
        ttk.Button(fila, text=T("Elegir carpeta", "Choose folder"), command=self.elegir).pack(side="left")
        self.instalar_b = ttk.Button(fila, text=T("Instalar en mis mods", "Install into my mods"), command=self.instalar)
        self.instalar_b.pack(side="left", padx=8)
        self.ruta = label(top.body, "", fg=g.C["dim"], wrap=820)
        self.ruta.pack(fill="x", pady=(8, 0))
        self.lista = tk.Frame(body, bg=g.C["bg"])
        self.lista.pack(fill="both", expand=True)
        self.refrescar()

    def elegir(self):
        p = filedialog.askdirectory(initialdir=self.page.app.settings.get("arte_dir") or None, mustexist=True)
        if p:
            self.page.app.settings["arte_dir"] = os.path.normpath(p)
            self.refrescar()

    def refrescar(self):
        g, T = self.g, self.g.T
        carpeta = self.page.app.settings.get("arte_dir", "")
        self.filas, raras = arte.analizar(carpeta, g.LANG != "es") if carpeta else ([], [])
        self.filas = self.filas or [(p, None, []) for p in arte.PIEZAS]
        hay = sum(1 for f in self.filas if f[1])
        self.ruta.configure(text=(T("%s: %d imágenes válidas.", "%s: %d usable images.") % (carpeta, hay)) if carpeta else
                            T("Elige la carpeta donde tienes tus PNG. Abajo está lo que admite cada pieza.",
                              "Choose the folder with your PNGs. Below is what each piece accepts."))
        self.instalar_b.configure(state="normal" if hay else "disabled")
        for w in self.lista.winfo_children():
            w.destroy()
        if raras:
            caja = g.Card(self.lista, title=T("Con un nombre que el juego no conoce", "With a name the game does not know"))
            caja.pack(fill="x", pady=(0, 12))
            for f, parecido in raras:
                g.label(caja.body, "%s  →  %s" % (f, (T("renómbrala a %s.png", "rename it to %s.png") % parecido) if parecido else
                                                 T("no se usará", "will not be used")), fg=g.C["warn"], wrap=820).pack(fill="x")
        caja = g.Card(self.lista, title=T("Piezas", "Pieces"))
        caja.pack(fill="x")
        for pieza, ruta, avisos in self.filas:
            datos = arte.PIEZAS.get(pieza)
            uso = (datos[1] if g.LANG == "es" else datos[2]) if datos else pieza
            color = g.C["warn"] if avisos else g.C["ok"] if ruta else g.C["dim"]
            r = tk.Frame(caja.body, bg=g.C["card"])
            r.pack(fill="x", pady=1)
            tk.Label(r, text="●", bg=g.C["card"], fg=color, font=g.F["small"]).pack(side="left")
            g.label(r, "%s.png  ·  %s" % (pieza, uso), bg=g.C["card"], fg=g.C["text"] if ruta else g.C["dim"], wrap=780).pack(side="left", padx=6)
            for a in avisos:
                g.label(caja.body, "     " + a, fg=g.C["warn"], wrap=800).pack(fill="x")

    def instalar(self):
        g, T = self.g, self.g.T
        n = arte.instalar(self.page.env, self.filas, g.anotar_indice)
        self.page.app.info(T("Instaladas %d imágenes en el mod «%s». Si no te gusta, apágalo en «Mis mods» o pulsa «Deshacer».",
                             "%d images installed into the '%s' mod. If you do not like it, turn it off in 'My mods' or press 'Undo'.") % (n, arte.MOD))
