# -*- coding: utf-8 -*-
"""Cambio de modelo (swap).

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403


# --------------------------------------------------------------------------------- swap
class SwapPage(Page):
    key = "swap"

    def build(self):
        self.header(T("Cambio de modelo (B3 → B3)", "Model swap (B3 → B3)"),
                    T("Pone el modelo HD de un personaje en el sitio de otro. El mod generado se activa solo y "
                      "aparece en «Mis mods».", "Puts one character's HD model in another's slot. The generated "
                      "mod activates itself and shows up in 'My mods'."))
        sf, body = self.scroll_body()
        self.chars = load_catalog(self.env)
        c = Card(body)
        c.pack(fill="x", pady=(0, 12))
        g = c.body
        g.columnconfigure(1, weight=1)
        label(g, T("Personaje HD (origen)", "HD character (source)"), bg=C["card"]).grid(row=0, column=0, sticky="w",
                                                                                       padx=(0, 12), pady=4)
        self.src = CharPicker(g, self.chars, on_change=self.update_preview)
        self.src.grid(row=0, column=1, sticky="w", pady=4)
        label(g, T("Slot destino", "Destination slot"), bg=C["card"]).grid(row=1, column=0, sticky="w", padx=(0, 12),
                                                                          pady=4)
        self.dst = CharPicker(g, self.chars, on_change=self.update_preview)
        self.dst.grid(row=1, column=1, sticky="w", pady=4)
        r = 2
        self.mod = tk.StringVar()
        self.verify = tk.BooleanVar(value=False)
        if self.adv:
            label(g, "--mod", bg=C["card"]).grid(row=r, column=0, sticky="w", padx=(0, 12), pady=4)
            ttk.Entry(g, textvariable=self.mod, width=30).grid(row=r, column=1, sticky="w", pady=4)
            r += 1
            hint(g, T("Vacío = swap_<origen>_on_<destino> (como el launcher).",
                      "Empty = swap_<source>_on_<target> (like the launcher)."), bg=C["card"]).grid(row=r, column=1,
                                                                                                   sticky="w")
            r += 1
            ttk.Checkbutton(g, text="--verify  " + T("(comprueba el override con MD5 al terminar)",
                                                     "(checks the override with MD5 at the end)"),
                            variable=self.verify, style="Card.TCheckbutton").grid(row=r, column=1, sticky="w", pady=4)
            r += 1
        self.preview = tk.Frame(g, bg=C["frame"], highlightthickness=1, highlightbackground=C["line"])
        self.preview.grid(row=r, column=0, columnspan=2, sticky="ew", pady=(10, 4))
        r += 1
        bf = tk.Frame(g, bg=C["card"])
        bf.grid(row=r, column=0, columnspan=2, sticky="w", pady=(8, 0))
        self.b_swap = ttk.Button(bf, text="⇄  " + T("Cambiar B3 → B3", "Swap B3 → B3"), style="Big.Accent.TButton",
                                 command=self.swap)
        self.b_swap.pack(side="left")
        if self.adv:
            ttk.Button(bf, text=T("Catálogo (--list)", "Catalog (--list)"),
                       command=lambda: self.run(cmd_script(self.env, "swap_b3.py", ["--list"]), "swap_b3 --list",
                                                None, self.status)).pack(side="left", padx=6)
            ttk.Button(bf, text="swap_matrix.py", command=lambda: self.app.goto("tools", tool="swap_matrix.py")).pack(
                side="left")
        self.status = self.status_label(g)
        self.status.grid(row=r + 1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        self.update_preview()

    def update_preview(self):
        if not hasattr(self, "preview"):
            return
        for w in self.preview.winfo_children():
            w.destroy()
        s, d = self.src.get(), self.dst.get()
        label(self.preview, T("Vista previa", "Preview"), fg=C["accent"], font=F["bold"], bg=C["frame"]).pack(
            fill="x", padx=12, pady=(8, 2))
        for who, c, kind in ((T("Origen:", "Source:"), s, "bin"), (T("Destino:", "Target:"), d, "slot")):
            txt = "%s  %s" % (who, (c["name"] + (" (%s)" % c["variant"] if c["variant"] else "") + "   %s %d" % (
                kind, c["bin"]) + ("" if c["playable"] else "   " + T("(no jugable)", "(not playable)"))) if c else "—")
            label(self.preview, txt, bg=C["frame"]).pack(fill="x", padx=12)
        same = s and d and s["bin"] == d["bin"]
        if same:
            label(self.preview, T("Origen y destino son el mismo personaje.", "Source and destination are the same "
                                  "character."), fg=C["warn"], bg=C["frame"]).pack(fill="x", padx=12)
        fotos = tk.Frame(self.preview, bg=C["frame"])
        fotos.pack(fill="x", padx=12, pady=(6, 0))
        for c in (s, d):
            if c:
                self._cuerpo(fotos, c)
        tk.Frame(self.preview, bg=C["frame"], height=8).pack()
        self.b_swap.state(["!disabled"] if s and d and not same else ["disabled"])

    def _cuerpo(self, parent, c):
        """Cuerpo del personaje con el render toon del juego; la primera vez se genera en segundo plano."""
        if not self.env.us or self.app.selftest:
            return
        png = vistas.png_modelo(self.env.us, c["bin"])
        if os.path.isfile(png):
            w = vistas.foto_fija(nucleo.G, parent, png, 180)
            if w:
                w.pack(side="left", padx=(0, 12))
            return
        label(parent, T("Dibujando…", "Drawing…"), fg=C["dim"], bg=C["frame"]).pack(side="left", padx=(0, 12))
        self.app.run(vistas.cmd_modelo(self.env.python, self.env.us, c["bin"], png), T("Vista previa", "Preview"),
                     lambda _j: self.alive and os.path.isfile(png) and self.update_preview(), quiet=True)

    def swap(self):
        s, d = self.src.get(), self.dst.get()
        if not s or not d or s["bin"] == d["bin"]:
            return
        extra = ["--verify"] if self.verify.get() else []
        mod = self.mod.get().strip() or None
        self.run(cmd_swap(self.env, s["bin"], d["bin"], mod, extra), T("Cambio de modelo %d → %d",
                                                                       "Model swap %d → %d") % (s["bin"], d["bin"]),
                 None, self.status, T("Hecho. El mod está activo en «Mis mods».", "Done. The mod is active in "
                                                                                  "'My mods'."))
