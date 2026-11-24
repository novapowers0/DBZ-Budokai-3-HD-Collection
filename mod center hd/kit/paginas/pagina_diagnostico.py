# -*- coding: utf-8 -*-
"""Diagnostico: lee el log del juego y lo explica.

Sacado tal cual de modkit_gui.py (KIT-05). Los nombres comunes (C, F, T, Page, widgets, cmd_*...) vienen de
kit/nucleo.py; el idioma vivo es nucleo.LANG y el modulo del Kit (el `g` de las piezas de kit/) es nucleo.G."""
from __future__ import annotations

from kit import nucleo
from kit.nucleo import *  # noqa: F401,F403


class DiagPage(Page):
    """Lee el log del juego y explica en sencillo que pasa (diagnostico.py), y revisa que el mod de
    personajes nuevos tenga todos los idiomas (en espanol/aleman/... el juego lee otros ficheros)."""
    key = "diag"

    def build(self):
        self.header(T("Diagnóstico", "Diagnostics"),
                    T("¿Va lento, se cierra o no salen los personajes? Analiza el registro del juego y te dice "
                      "qué pasa y qué hacer.",
                      "Slow, crashing or new characters missing? Analyse the game log and get what is wrong "
                      "and what to do."))
        sf, body = self.scroll_body()
        c = Card(body, T("Registro del juego", "Game log"),
                 T("El juego guarda uno por partida en la carpeta logs (dbz3_XXX.log). También vale el log "
                   "que te pase otro jugador.",
                   "The game writes one per session in the logs folder (dbz3_XXX.log). A log from another "
                   "player works too."), accent=True)
        c.pack(fill="x", pady=(12, 12))
        row = tk.Frame(c.body, bg=C["card"])
        row.pack(fill="x")
        ttk.Button(row, text=T("Analizar mi último registro", "Analyse my latest log"), style="Accent.TButton",
                   command=self.latest).pack(side="left")
        ttk.Button(row, text=T("Abrir otro registro…", "Open another log…"), style="Small.TButton",
                   command=self.pick).pack(side="left", padx=8)
        ttk.Button(row, text=T("Copiar informe", "Copy report"), style="Small.TButton",
                   command=self.copy).pack(side="left")
        self.src = label(c.body, "", fg=C["dim"], bg=C["card"], font=F["small"])
        self.src.pack(fill="x", pady=(8, 0))
        self.out = tk.Frame(body, bg=C["bg"])
        self.out.pack(fill="x")
        self.report = ""
        m = Card(body, T("Mod de personajes nuevos", "New characters mod"),
                 T("Comprueba que esté montado para todos los idiomas del juego.",
                   "Checks it is built for every game language."))
        m.pack(fill="x", pady=(12, 12))
        self.mods_lbl = label(m.body, "", bg=C["card"], wrap=820)
        self.mods_lbl.pack(fill="x")

    def on_show(self):
        roster = os.path.join(self.env.mods, "_roster", "us")
        if not os.path.isdir(roster):
            self.mods_lbl.configure(text=T("Aún no hay personajes nuevos montados (se montan al pulsar JUGAR).",
                                           "No new characters built yet (they are built when you press PLAY)."),
                                    fg=C["dim"])
            return
        need = ["data_usi.afs"] + ["data_%s.afs" % k for k in ("eng", "spn", "fra", "ger", "ita")]
        miss = [n for n in need if not os.path.exists(os.path.join(roster, n))]
        if miss:
            self.mods_lbl.configure(text=T("Faltan idiomas: %s. Con el juego en esos idiomas las casillas nuevas "
                                           "salen vacías. Pulsa «Reconstruir» en «Personajes nuevos».",
                                           "Missing languages: %s. With the game in those languages the new "
                                           "cells are blank. Press 'Rebuild' in 'New characters'.")
                                    % ", ".join(miss), fg=C["err"])
        else:
            self.mods_lbl.configure(text=T("Correcto: montado para inglés, español, francés, alemán e italiano.",
                                           "OK: built for English, Spanish, French, German and Italian."),
                                    fg=C["ok"])

    def latest(self):
        logs = os.path.join(self.env.game, "logs") if self.env.game else ""
        cands = []
        if logs and os.path.isdir(logs):
            cands = [os.path.join(logs, n) for n in os.listdir(logs) if n.startswith("dbz3_") and n.endswith(".log")]
        if not cands:
            messagebox.showinfo(T("Diagnóstico", "Diagnostics"),
                                T("No encuentro registros del juego. Juega una partida o elige uno con «Abrir otro "
                                  "registro…».", "No game logs found. Play once or pick one with 'Open another "
                                  "log…'."))
            return
        self.analyse(max(cands, key=os.path.getmtime))

    def pick(self):
        p = filedialog.askopenfilename(filetypes=[(T("Registros", "Logs"), "*.log *.txt"), (T("Todos", "All"), "*.*")])
        if p:
            self.analyse(p)

    def analyse(self, path):
        import diagnostico
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError as e:
            messagebox.showerror(T("Diagnóstico", "Diagnostics"), T(
                "No se pudo leer ese registro (puede que el juego aún lo esté usando: ciérralo y vuelve a "
                "intentarlo).\n\n%s", "That log could not be read (the game may still be using it: close it and "
                "try again).\n\n%s") % e)
            return
        findings = diagnostico.analizar(text, en=nucleo.LANG == "en")
        self.report = diagnostico.informe(text, en=nucleo.LANG == "en")
        self.src.configure(text=path)
        for w in self.out.winfo_children():
            w.destroy()
        color = {"ok": C["ok"], "info": C["blue"], "warn": C["warn"], "err": C["err"]}
        mark = {"ok": "✔", "info": "ℹ", "warn": "!", "err": "✖"}
        for level, title, detail in findings:
            card = tk.Frame(self.out, bg=C["card"], highlightthickness=1, highlightbackground=C["line"])
            card.pack(fill="x", pady=4)
            tk.Label(card, text=mark[level], bg=C["card"], fg=color[level], font=F["h3"], width=2).pack(
                side="left", anchor="n", padx=(10, 4), pady=8)
            tf = tk.Frame(card, bg=C["card"])
            tf.pack(side="left", fill="x", expand=True, pady=8, padx=(0, 12))
            label(tf, title, font=F["bold"], bg=C["card"], fg=color[level]).pack(fill="x")
            if detail:
                label(tf, detail, bg=C["card"], wrap=820).pack(fill="x", pady=(2, 0))
        self.app.log_text(self.report + "\n")

    def copy(self):
        if not self.report:
            return
        self.app.root.clipboard_clear()
        self.app.root.clipboard_append(self.report)
        self.src.configure(text=T("Informe copiado: pégalo en Discord al pedir ayuda.",
                                  "Report copied: paste it on Discord when asking for help."))
