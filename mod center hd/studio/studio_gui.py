#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""studio_gui.py - DBZ3 HD Studio: editor de camaras de tecnicas (ventana propia del Mod Kit).

Elige personaje y tecnica, mira sus clips de camara en la linea de tiempo (esperas del guion
y golpes), edita la trayectoria en vistas 2D (arrastrar), aplica plantillas (orbita,
travelling, temblor, giro), mira la vista previa toon y guarda como mod con respaldo.
Blender es opcional: "Abrir en Blender" / "Traer de Blender" (glTF, sin add-on).
No necesita el juego abierto: lee us/data_cmn.afs y escribe solo en mods/.

  python studio_gui.py [--personaje ID|carpeta_de_mod] [--lang en] [--mods DIR] [--us DIR]
  python studio_gui.py --selftest            construye todo sin mostrar la ventana y sale
  python studio_gui.py --captura out.png     abre, carga Goku, guarda una captura y sale
"""
import argparse
import math
import os
import queue
import sys
import tempfile
import threading
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.dirname(HERE)]
import studio_core as sc  # noqa: E402

import tkinter as tk  # noqa: E402
from tkinter import filedialog, messagebox, ttk  # noqa: E402

import modkit_gui as mk  # noqa: E402  (paleta, estilos, Env y T del Mod Kit)
from modkit_gui import C, F  # noqa: E402

WORK = os.path.join(HERE, "trabajo")
VIEW_W, VIEW_H = 300, 230
PREV_W, PREV_H = 352, 198


def T(es, en):
    return mk.T(es, en)


def char_name(cid):
    return next((T(es, en) for i, es, en in mk.DONORS if i == cid), None)


class StudioApp:
    def __init__(self, root, env, start=None, selftest=False):
        self.root, self.env, self.selftest = root, env, selftest
        self.mods = env.mods
        self.us = env.us or None
        self.q = queue.Queue()
        self.cam = self.orig = None
        self.src = None
        self.edited = set()
        self.dirty = False            # cambios sin guardar (para avisar al cerrar)
        self.k = 0
        self.f = 0
        self.seqs, self.seq = [], None
        self.base = 0
        self.model = self.skel = self.acm = self.csk = None
        self.drag = None
        self.preview_img = None
        self.playing = False
        self._render_job = None
        mk.init_fonts(root)
        mk.setup_style(root)
        root.configure(bg=C["bg"])
        root.title("DBZ3 HD Studio")
        root.minsize(1180, 760)
        self.chars = self.load_chars()
        self.build()
        root.after(100, self.poll)
        if self.chars:
            idx = 0
            if start is not None:
                idx = next((i for i, c in enumerate(self.chars) if str(c.get("id", c.get("mod"))) == str(start)), 0)
            self.char_cb.current(idx)
            self.pick_char()

    # ------------------------------------------------------------------ datos
    def load_chars(self):
        out = []
        en = {i: e for i, _es, e in mk.DONORS}
        for n in sc.natives():
            n["nombre"] = en.get(n["id"], n["nombre"])        # carpeta del mod: studio_<nombre en ingles>
            out.append(dict(n, kind="nativo", label="%s  (ID %d)" % (char_name(n["id"]) or n["nombre"], n["id"])))
        for p in sc.ports(self.mods):
            out.append(dict(p, kind="port", label="%s  (%s)" % (p["nombre"], T("mod %s", "mod %s") % p["mod"])))
        return out

    # ------------------------------------------------------------------ interfaz
    def build(self):
        r = self.root
        h = tk.Frame(r, bg=C["bg"])
        h.pack(fill="x", padx=16, pady=(10, 6))
        tk.Label(h, text="DBZ3 HD", bg=C["bg"], fg=C["accent"], font=F["h1"]).pack(side="left")
        tk.Label(h, text=" Studio", bg=C["bg"], fg=C["text"], font=F["h1"]).pack(side="left")
        mk.label(h, "   " + T("Cámaras de técnicas · no hace falta el juego abierto",
                              "Technique cameras · the game does not need to be open"), fg=C["dim"]).pack(
            side="left", pady=(10, 0))
        tk.Frame(r, bg=C["accent"], height=2).pack(fill="x")
        bar = tk.Frame(r, bg=C["bg"])
        bar.pack(fill="x", padx=16, pady=8)
        ttk.Button(bar, text=T("Guardar como mod", "Save as mod"), style="Accent.TButton",
                   command=self.save).pack(side="right")
        for txt, cmd in ((T("Traer de Blender", "Bring from Blender"), self.from_blender),
                         (T("Abrir en Blender", "Open in Blender"), self.to_blender)):
            ttk.Button(bar, text=txt, style="Blue.TButton", command=cmd).pack(side="right", padx=(0, 6))
        guide = os.path.join(sc.ROOT, "docs", "02_mods", "STUDIO_CAMARAS.md")
        if os.path.isfile(guide):
            ttk.Button(bar, text="?", width=2, style="Ghost.TButton",
                       command=lambda: mk.open_path(guide)).pack(side="right", padx=(0, 6))
        mk.label(bar, T("Personaje", "Character")).pack(side="left")
        self.char_cb = ttk.Combobox(bar, values=[c["label"] for c in self.chars], state="readonly", width=24)
        self.char_cb.pack(side="left", padx=(6, 14))
        self.char_cb.bind("<<ComboboxSelected>>", lambda _e: self.pick_char())
        mk.label(bar, T("Técnica", "Technique")).pack(side="left")
        self.seq_cb = ttk.Combobox(bar, state="readonly", width=34)
        self.seq_cb.pack(side="left", padx=(6, 6))
        self.seq_cb.bind("<<ComboboxSelected>>", lambda _e: self.pick_seq())
        mk.label(bar, T("primer clip", "first clip"), fg=C["dim"], font=F["small"]).pack(side="left")
        self.base_var = tk.StringVar(value="0")
        sp = ttk.Spinbox(bar, from_=0, to=200, width=4, textvariable=self.base_var, command=self.base_changed)
        sp.pack(side="left", padx=4)
        sp.bind("<Return>", lambda _e: self.base_changed())
        mk.Tooltip(sp, T("El guion pide los clips como «base + K». La base se estima comparando la duración de "
                         "cada clip con su espera; cámbiala si la técnica no encaja.",
                         "The script asks for clips as 'base + K'. The base is estimated by matching each clip's "
                         "length with its wait; change it if the technique does not fit."))
        body = tk.Frame(r, bg=C["bg"])
        body.pack(fill="both", expand=True, padx=16)
        left = tk.Frame(body, bg=C["bg"], width=340)
        left.pack(side="left", fill="y")
        left.pack_propagate(False)
        right = tk.Frame(body, bg=C["bg"], width=PREV_W + 30)
        right.pack(side="right", fill="y")
        right.pack_propagate(False)
        mid = tk.Frame(body, bg=C["bg"])
        mid.pack(side="left", fill="both", expand=True, padx=12)
        self._build_left(left)
        self._build_mid(mid)
        self._build_right(right)
        self.status = mk.label(r, "", fg=C["dim"], font=F["small"])
        self.status.pack(fill="x", padx=16, pady=(4, 8))

    def _build_left(self, p):
        c = mk.Card(p, T("Clips de cámara", "Camera clips"))
        c.pack(fill="both", expand=True)
        tf = tk.Frame(c.body, bg=C["card"])
        tf.pack(fill="both", expand=True)
        self.tree = ttk.Treeview(tf, columns=("fr", "st"), show="tree headings", selectmode="browse", height=7)
        self.tree.heading("#0", text=T("Clip", "Clip"))
        self.tree.heading("fr", text=T("Frames", "Frames"))
        self.tree.heading("st", text=T("Estado", "State"))
        self.tree.column("#0", width=90)
        self.tree.column("fr", width=60, anchor="center")
        self.tree.column("st", width=100, anchor="center")
        vsb = ttk.Scrollbar(tf, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        self.tree.tag_configure("ed", foreground=C["accent"])
        self.tree.tag_configure("seq", foreground=C["blue"])
        self.tree.bind("<<TreeviewSelect>>", lambda _e: self._tree_pick())
        bf = tk.Frame(c.body, bg=C["card"])
        bf.pack(fill="x", pady=(8, 0))
        ttk.Button(bf, text=T("Deshacer", "Undo"), style="Small.TButton",
                   command=self.undo_clip).pack(side="left")
        ttk.Button(bf, text=T("Añadir", "Add"), style="Small.TButton",
                   command=self.add_clip).pack(side="left", padx=4)
        ttk.Button(bf, text=T("Copiar…", "Copy…"), style="Small.TButton",
                   command=self.copy_from).pack(side="left")
        rf = tk.Frame(c.body, bg=C["card"])
        rf.pack(fill="x", pady=(6, 0))
        mk.label(rf, T("Duración", "Length"), bg=C["card"]).pack(side="left")
        self.len_var = tk.StringVar()
        ttk.Spinbox(rf, from_=2, to=2000, width=6, textvariable=self.len_var).pack(side="left", padx=4)
        ttk.Button(rf, text=T("Retemporizar", "Retime"), style="Small.TButton", command=self.retime).pack(side="left")
        t = mk.Card(p, T("Plantillas", "Templates"))
        t.pack(fill="x", pady=(10, 0))
        self.tpl_names = [("orbita", T("Órbita", "Orbit")), ("travelling", T("Travelling / acercamiento",
                                                                              "Dolly / zoom")),
                          ("temblor", T("Temblor", "Shake")), ("giro", T("Giro (roll)", "Roll"))]
        self.tpl_cb = ttk.Combobox(t.body, values=[n for _, n in self.tpl_names], state="readonly", width=30)
        self.tpl_cb.current(0)
        self.tpl_cb.pack(fill="x")
        self.tpl_cb.bind("<<ComboboxSelected>>", lambda _e: self._tpl_fields())
        self.tpl_frame = tk.Frame(t.body, bg=C["card"])
        self.tpl_frame.pack(fill="x", pady=6)
        ttk.Button(t.body, text=T("Aplicar al clip", "Apply to clip"), style="Small.TButton",
                   command=self.apply_tpl).pack(anchor="w")
        self.tpl_vars = {}
        self._tpl_fields()

    TPL_FIELDS = {
        "orbita": [("radio", "Radio", "Radius", "40"), ("altura", "Altura", "Height", "6"),
                   ("ang0", "Ángulo inicial (°)", "Start angle (°)", "0"),
                   ("barrido", "Barrido (°)", "Sweep (°)", "120"), ("fov", "Fov (°)", "Fov (°)", "38")],
        "travelling": [("acercar", "Acercar (%)", "Move in (%)", "50"), ("subir", "Subir", "Raise", "0"),
                       ("fov1", "Fov final (°)", "End fov (°)", "30")],
        "temblor": [("amp", "Amplitud", "Amplitude", "0.8"), ("hz", "Frecuencia (Hz)", "Frequency (Hz)", "9"),
                    ("f0", "Desde frame", "From frame", "0"), ("f1", "Hasta frame", "To frame", "30")],
        "giro": [("g0", "Grados al inicio", "Start degrees", "0"), ("g1", "Grados al final", "End degrees", "25"),
                 ("f0", "Desde frame", "From frame", "0"), ("f1", "Hasta frame", "To frame", "60")],
    }

    def _tpl_fields(self):
        for w in self.tpl_frame.winfo_children():
            w.destroy()
        key = self.tpl_names[max(0, self.tpl_cb.current())][0]
        self.tpl_vars = {}
        for i, (k, es, en, dv) in enumerate(self.TPL_FIELDS[key]):
            r, col = i // 2, 2 * (i % 2)
            mk.label(self.tpl_frame, T(es, en), bg=C["card"], font=F["small"], wrap=90).grid(
                row=r, column=col, sticky="w")
            v = tk.StringVar(value=dv)
            ttk.Entry(self.tpl_frame, textvariable=v, width=6).grid(row=r, column=col + 1, sticky="w", padx=(4, 10),
                                                                    pady=1)
            self.tpl_vars[k] = v

    def _build_mid(self, p):
        tl = mk.Card(p, T("Línea de tiempo (guion)", "Timeline (script)"))
        tl.pack(fill="x")
        self.tl = tk.Canvas(tl.body, height=86, bg=C["frame"], highlightthickness=0)
        self.tl.pack(fill="x")
        self.tl.bind("<Button-1>", self._tl_click)
        self._tl_blocks = []
        self.tl.bind("<Configure>", lambda _e: self.draw_timeline())
        vw = tk.Frame(p, bg=C["bg"])
        vw.pack(fill="x", pady=(10, 0))
        self.views = {}
        for key, es, en in (("top", "Desde arriba (X / Z)", "From above (X / Z)"),
                            ("side", "De lado (Z / Y)", "From the side (Z / Y)")):
            cd = mk.Card(vw, T(es, en), pad=8)
            cd.pack(side="left", padx=(0, 10))
            cv = tk.Canvas(cd.body, width=VIEW_W, height=VIEW_H, bg=C["log_bg"], highlightthickness=0)
            cv.pack()
            cv.bind("<ButtonPress-1>", lambda e, k=key: self._press(k, e))
            cv.bind("<B1-Motion>", lambda e, k=key: self._motion(k, e))
            cv.bind("<ButtonRelease-1>", lambda e, k=key: self._release(k, e))
            self.views[key] = cv
        opt = tk.Frame(p, bg=C["bg"])
        opt.pack(fill="x", pady=(6, 0))
        mk.label(opt, "● " + T("ojo", "eye"), fg=C["accent"]).pack(side="left")
        mk.label(opt, "   ● " + T("objetivo", "target"), fg=C["blue"]).pack(side="left")
        mk.label(opt, "   " + T("Arrastra un punto para moverlo.", "Drag a dot to move it."), fg=C["dim"],
                 font=F["small"]).pack(side="left")
        opt2 = tk.Frame(p, bg=C["bg"])
        opt2.pack(fill="x", pady=(2, 0))
        mk.label(opt2, T("Suavizado ±frames", "Smoothing ±frames"), fg=C["dim"], font=F["small"]).pack(side="left")
        self.rad_var = tk.StringVar(value="10")
        ttk.Spinbox(opt2, from_=0, to=200, width=4, textvariable=self.rad_var).pack(side="left", padx=6)
        self.all_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(opt2, text=T("mover toda la trayectoria", "move the whole path"),
                        variable=self.all_var).pack(side="left", padx=10)
        cv = mk.Card(p, T("Giro y fov", "Roll and fov"), pad=8)
        cv.pack(fill="x", pady=(8, 0))
        self.curves = tk.Canvas(cv.body, height=92, bg=C["log_bg"], highlightthickness=0)
        self.curves.pack(fill="x")
        self.curves.bind("<Button-1>", self._curve_click)
        self.curves.bind("<B1-Motion>", self._curve_click)
        fr = tk.Frame(p, bg=C["bg"])
        fr.pack(fill="x", pady=(8, 0))
        self.play_btn = ttk.Button(fr, text="▶", width=3, style="Small.TButton", command=self.toggle_play)
        self.play_btn.pack(side="left")
        self.frame_var = tk.IntVar(value=0)
        self.scale = ttk.Scale(fr, from_=0, to=1, orient="horizontal", command=self._scale)
        self.scale.pack(side="left", fill="x", expand=True, padx=8)
        self.frame_lbl = mk.label(fr, "0 / 0", fg=C["dim"])
        self.frame_lbl.pack(side="left")

    def _build_right(self, p):
        pv = mk.Card(p, T("Vista previa (toon)", "Preview (toon)"), pad=8)
        pv.pack(fill="x")
        self.prev_lbl = tk.Canvas(pv.body, bg=C["frame"], width=PREV_W, height=PREV_H, highlightthickness=0)
        self.prev_lbl.pack()
        self.anim_var = tk.StringVar(value="0")
        af = tk.Frame(pv.body, bg=C["card"])
        af.pack(fill="x", pady=(6, 0))
        mk.label(af, T("Animación del personaje", "Character animation"), bg=C["card"], font=F["small"]).pack(
            side="left")
        sp = ttk.Spinbox(af, from_=0, to=999, width=5, textvariable=self.anim_var, command=self.render_later)
        sp.pack(side="left", padx=6)
        sp.bind("<Return>", lambda _e: self.render_later())
        ttk.Button(af, text="GIF", style="Small.TButton", command=self.gif).pack(side="right")
        mk.hint(pv.body, T("La pose es orientativa: en el juego el guion decide qué animación suena en cada clip.",
                           "The pose is a guide: in game the script decides which animation plays in each clip."),
                wrap=PREV_W, bg=C["card"]).pack(fill="x", pady=(4, 0))
        nc = mk.Card(p, T("Valores en este frame", "Values at this frame"), pad=10)
        nc.pack(fill="x", pady=(10, 0))
        g = nc.body
        self.num = {}
        rows = (("ojo", T("Ojo X Y Z", "Eye X Y Z"), 3), ("objetivo", T("Objetivo X Y Z", "Target X Y Z"), 3),
                ("roll", T("Giro (°)", "Roll (°)"), 1), ("fov", T("Fov (°)", "Fov (°)"), 1))
        for r, (key, text, n) in enumerate(rows):
            mk.label(g, text, bg=C["card"], font=F["small"]).grid(row=r, column=0, sticky="w", pady=2)
            vs = []
            for j in range(n):
                v = tk.StringVar()
                ttk.Entry(g, textvariable=v, width=8).grid(row=r, column=1 + j, padx=2)
                vs.append(v)
            self.num[key] = vs
        ttk.Button(g, text=T("Aplicar en este frame", "Apply at this frame"), style="Small.TButton",
                   command=self.apply_numbers).grid(row=4, column=0, columnspan=4, sticky="w", pady=(6, 0))
        gl = mk.Card(p, "glTF", pad=10)
        gl.pack(fill="x", pady=(10, 0))
        bf = tk.Frame(gl.body, bg=C["card"])
        bf.pack(fill="x")
        ttk.Button(bf, text=T("Importar .glb…", "Import .glb…"), style="Small.TButton",
                   command=self.import_glb).pack(side="left")
        ttk.Button(bf, text=T("Exportar .glb…", "Export .glb…"), style="Small.TButton",
                   command=self.export_glb).pack(side="left", padx=6)
        mk.hint(gl.body, T("Para Maya, 3ds Max, Cascadeur… La cámara viaja con su fov; el punto de mira se "
                           "reconstruye a la distancia del clip.", "For Maya, 3ds Max, Cascadeur… The camera "
                           "travels with its fov; the look-at point is rebuilt at the clip's distance."),
                wrap=PREV_W, bg=C["card"]).pack(fill="x", pady=(4, 0))

    # ------------------------------------------------------------------ carga
    def say(self, text, level="dim"):
        self.status.configure(text=text, fg=C.get(level, C["dim"]))

    def pick_char(self):
        i = self.char_cb.current()
        if i < 0:
            return
        if self.edited and self.src and not self.ask(T("Hay cambios sin guardar en %s. ¿Descartarlos?",
                                                       "There are unsaved changes in %s. Discard them?") %
                                                     self.src["label"]):
            self.char_cb.current(self.chars.index(self.src))
            return
        prev = (self.src, getattr(self, "orig", None), self.cam, set(self.edited))
        self.src = c = self.chars[i]
        self.root.config(cursor="watch")
        self.root.update_idletasks()
        try:
            if c["kind"] == "nativo":
                raw = sc.hd_entry(c["cam"], self.us)
                self.orig = sc.CamBin(raw)
                self.cam = sc.CamBin(raw)
                proj = sc.load_project(self.mods, c["nombre"])
                sc.apply_project(self.cam, proj)
                self.edited = {int(k) for k in (proj or {}).get("clips", {})}
                mdl, anm = c["modelo"], c["anm"]
            else:
                raw = sc.load_cam_file(os.path.join(self.mods, c["mod"], c["camara"]))
                self.orig, self.cam = sc.CamBin(raw), sc.CamBin(raw)
                self.edited = set()
                mdl, anm = c.get("modelo"), c.get("anm")
            self.model = self.skel = self.acm = self.csk = None
            try:
                mb = sc.load_model_bin(mdl, self.us) if mdl is not None else None
                ab = sc.load_model_bin(anm, self.us) if anm is not None else None
                if mb is not None:
                    import altura  # noqa: PLC0415
                    from model_render import Model  # noqa: PLC0415
                    self.model, self.skel = Model(mb), altura.skeleton(mb)
                if ab is not None:
                    self.acm, self.acm_e = sc.anim_pool(ab)
                    self.csk, self.csk_e = sc.csk_of(ab)
            except Exception as ex:  # noqa: BLE001
                self.say(T("Sin vista previa: %s", "No preview: %s") % ex, "warn")
            self.seqs = sc.spx_sequences(self.cam.spx())
            labels = [T("Todos los clips", "All clips")] + [self._seq_label(s, j) for j, s in enumerate(self.seqs)]
            self.seq_cb.configure(values=labels)
            best = max(range(len(self.seqs)), key=lambda j: sum(1 for p in self.seqs[j]["pasos"]
                                                                if p["clip"][0] != "var"), default=-1)
            self.seq_cb.current(best + 1)
            self.k, self.f = 0, 0
            self.pick_seq()
            n = len(self.cam.clips)
            msg = T("%s: %d clips de cámara, %d guiones con cámara.", "%s: %d camera clips, %d scripts with camera.") \
                % (c["label"], n, len(self.seqs))
            if not n:
                msg += "  " + T("No tiene cámaras propias: «Añadir clip» o «Copiar de…» para crear la primera.",
                                "It has no own cameras: 'Add clip' or 'Copy from…' to create the first one.")
            if self.edited:
                msg += "  " + T("Cambios del mod Studio cargados: %s.", "Studio mod changes loaded: %s.") % sorted(
                    self.edited)
            self.say(msg, "text")
            self.dirty = False
        except Exception as ex:  # noqa: BLE001
            # se vuelve al anterior: si no, "Guardar" escribiria sus camaras en este personaje
            self.src, self.orig, self.cam, self.edited = prev
            if self.src in self.chars:
                self.char_cb.current(self.chars.index(self.src))
            self.say(T("No se pudo abrir: %s", "Could not open: %s") % ex, "err")
            traceback.print_exc()
        finally:
            self.root.config(cursor="")

    def _seq_label(self, s, j):
        txt = sc.seq_label(s, j)
        if mk.LANG == "en":
            txt = txt.replace("sin ranura", "no slot").replace("Guion", "Script").replace("ranura", "slot")
            txt = txt.replace("modo hiper / definitiva", "hyper mode / ultimate").replace("agarre", "grab")
        return txt

    def pick_seq(self):
        i = self.seq_cb.current()
        self.seq = self.seqs[i - 1] if i > 0 and i - 1 < len(self.seqs) else None
        if self.seq:
            self.base = sc.guess_base(self.seq, [c.frames for c in self.cam.clips])
            self.base_var.set(str(self.base))
            ks = [self.clip_of(p) for p in self.seq["pasos"]]
            self.k = next((k for k in ks if k is not None), self.k)
            code = next((c for c, _t in self.seq["codigos"] if self.csk and sc.code_info(self.csk, self.csk_e, c)
                         and sc.code_info(self.csk, self.csk_e, c)[1] == 3), None)
            if code is not None:
                self.anim_var.set(str(sc.code_info(self.csk, self.csk_e, code)[0]))
        self.refresh_tree()
        self.select_clip(min(self.k, max(0, len(self.cam.clips) - 1)))

    def base_changed(self):
        try:
            self.base = max(0, int(self.base_var.get()))
        except ValueError:
            return
        self.refresh_tree()
        self.draw_timeline()

    def clip_of(self, step):
        kind, kk = step["clip"]
        if kind == "rel":
            k = self.base + kk
        elif kind == "abs":
            k = kk
        else:
            return None
        return k if 0 <= k < len(self.cam.clips) else None

    def refresh_tree(self):
        self.tree.delete(*self.tree.get_children())
        in_seq = {self.clip_of(p) for p in self.seq["pasos"]} if self.seq else set()
        for k, c in enumerate(self.cam.clips):
            st = T("nuevo", "new") if k >= self.orig_n() else T("editado", "edited") if k in self.edited else ""
            tags = ("ed",) if k in self.edited else ("seq",) if k in in_seq else ()
            mark = "▸ " if k in in_seq else "   "
            self.tree.insert("", "end", iid=str(k), text="%s%d" % (mark, k), values=(c.frames, st), tags=tags)

    def orig_n(self):
        return len(self.orig.clips) if self.orig else 0

    def _tree_pick(self):
        s = self.tree.selection()
        if s and int(s[0]) != self.k:
            self.select_clip(int(s[0]))

    def select_clip(self, k):
        self.k = k
        if self.cam and self.cam.clips:
            c = self.cam.clips[k]
            self.f = min(self.f, c.frames - 1)
            self.scale.configure(to=max(1, c.frames - 1))
            self.scale.set(self.f)
            self.len_var.set(str(c.frames))
            if self.tree.exists(str(k)) and self.tree.selection() != (str(k),):
                self.tree.selection_set(str(k))
                self.tree.see(str(k))
        self.redraw()

    # ------------------------------------------------------------------ dibujo
    def clip(self):
        return self.cam.clips[self.k] if self.cam and self.cam.clips and self.k < len(self.cam.clips) else None

    def redraw(self):
        self.draw_timeline()
        self.draw_views()
        self.draw_curves()
        self.fill_numbers()
        c = self.clip()
        self.frame_lbl.configure(text="%d / %d" % (self.f, (c.frames - 1) if c else 0))
        self.render_later()

    def draw_timeline(self):
        cv = self.tl
        cv.delete("all")
        W = max(200, cv.winfo_width())
        if not self.cam:
            return
        if self.seq:
            blocks = []
            for p in self.seq["pasos"]:
                k = self.clip_of(p)
                fr = self.cam.clips[k].frames if k is not None else 30
                # una espera muy larga suele sumar ramas del guion: se dibuja acotada
                blocks.append((k, min(p["espera"], 2 * fr) if p["espera"] else fr, p["inicio"]))
        else:
            t = 0
            blocks = []
            for k, c in enumerate(self.cam.clips):
                blocks.append((k, c.frames, t))
                t += c.frames
        total = max(1, sum(b[1] for b in blocks))
        self._tl_blocks = []
        x = 4.0
        sx = (W - 8) / total
        for k, dur, _t0 in blocks:
            w = max(3.0, dur * sx)
            sel = k == self.k
            col = C["accent_soft"] if sel else C["card_hi"]
            cv.create_rectangle(x, 6, x + w - 2, 46, fill=col, outline=C["accent"] if sel else C["line"])
            clipf = self.cam.clips[k].frames if k is not None else 0
            if k is not None and clipf < dur:          # el clip acaba antes que la espera: se mantiene
                cv.create_rectangle(x + clipf * sx, 40, x + w - 2, 46, fill=C["line"], outline="")
            if w > 26:
                cv.create_text(x + 5, 16, anchor="w", fill=C["accent"] if k in self.edited else C["text"],
                               text=("%d" % k) if k is not None else "?", font=F["bold_sm"])
                if w > 60:
                    cv.create_text(x + 5, 32, anchor="w", fill=C["dim"], font=F["small"],
                                   text="%df" % dur)
            self._tl_blocks.append((x, x + w, k))
            x += w
        if self.seq and self.csk is not None:                # golpes (aprox.): codigos que empuja el guion
            spans = [(t0, dur, x0) for (k, dur, t0), (x0, _x1, _k) in zip(blocks, self._tl_blocks)]
            for code, t in self.seq["codigos"]:
                ci = sc.code_info(self.csk, self.csk_e, code)
                for fr, ty in (ci[2] if ci else []):
                    span = next(((t0, dur, x0) for t0, dur, x0 in spans if t0 <= t + fr < t0 + dur), None)
                    if ty == 1 and span:
                        px = span[2] + (t + fr - span[0]) * sx
                        cv.create_line(px, 48, px, 60, fill=C["err"], width=2)
        secs = total / sc.FPS
        cv.create_text(W - 6, 74, anchor="e", fill=C["dim"], font=F["small"],
                       text=T("%.1f s  ·  rojo = golpes (aprox.)", "%.1f s  ·  red = hits (approx.)") % secs
                       if self.seq else T("%.1f s (clips seguidos)", "%.1f s (clips in a row)") % secs)

    def _tl_click(self, e):
        k = next((k for x0, x1, k in self._tl_blocks if x0 <= e.x < x1), None)
        if k is not None:
            self.select_clip(k)

    def _project(self, key, v):
        return (v[0], v[2]) if key == "top" else (v[2], v[1])

    def _fit(self, key, pts):
        xs = [p[0] for p in pts] or [0]
        ys = [p[1] for p in pts] or [0]
        x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
        span = max(x1 - x0, y1 - y0, 10.0) * 1.15
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        s = min(VIEW_W, VIEW_H) / span
        return lambda p: (VIEW_W / 2 + (p[0] - cx) * s, VIEW_H / 2 - (p[1] - cy) * s), s

    def skeleton_now(self):
        if self.model is None or self.acm is None:
            return None
        try:
            anim = int(self.anim_var.get())
        except ValueError:
            anim = 0
        if not 0 <= anim < sc.anim_count(self.acm, self.acm_e):
            anim = 0
        self.model.local = sc.anim_locals(self.skel, self.acm, self.acm_e, anim, self.f)
        W = self.model.worlds()
        return [w[:3, 3] for w in W], self.model.parent

    def draw_views(self):
        c = self.clip()
        bones = self.skeleton_now()
        self._view_tf = {}
        for key, cv in self.views.items():
            cv.delete("all")
            if c is None:
                cv.create_text(VIEW_W / 2, VIEW_H / 2, text=T("Sin clips", "No clips"), fill=C["dim"])
                continue
            eye = [self._project(key, c.at("ojo", f)) for f in range(c.frames)]
            tgt = [self._project(key, c.at("objetivo", f)) for f in range(c.frames)]
            sk = [self._project(key, p) for p in bones[0]] if bones else [(0, 0), (0, 15) if key == "side" else (0, 0)]
            tf, s = self._fit(key, eye + tgt + sk)
            self._view_tf[key] = (tf, s)
            o = tf((0, 0))
            cv.create_line(o[0] - 8, o[1], o[0] + 8, o[1], fill=C["line"])
            cv.create_line(o[0], o[1] - 8, o[0], o[1] + 8, fill=C["line"])
            if bones:
                for i, par in enumerate(bones[1]):
                    if 0 <= par < len(sk) and par != i:
                        a, b = tf(sk[i]), tf(sk[par])
                        cv.create_line(a[0], a[1], b[0], b[1], fill="#5A6070", width=2)
            for path, col in ((tgt, C["blue_dim"]), (eye, C["accent_dim"])):
                pts = [v for p in path for v in tf(p)]
                if len(pts) >= 4:
                    cv.create_line(*pts, fill=col, width=2)
            for path, col in ((tgt, C["blue"]), (eye, C["accent"])):
                a = tf(path[0])
                cv.create_oval(a[0] - 2, a[1] - 2, a[0] + 2, a[1] + 2, outline=col)
            e1, t1 = tf(eye[self.f]), tf(tgt[self.f])
            cv.create_line(e1[0], e1[1], t1[0], t1[1], fill=C["dim"], dash=(3, 3))
            for p, col in ((t1, C["blue"]), (e1, C["accent"])):
                cv.create_oval(p[0] - 6, p[1] - 6, p[0] + 6, p[1] + 6, fill=col, outline=C["text"])
            if self.drag and self.drag["key"] == key:
                d = self.drag
                cv.create_oval(d["x"] - 7, d["y"] - 7, d["x"] + 7, d["y"] + 7, outline=C["warn"], width=2)
            lab = ("X", "Z") if key == "top" else ("Z", "Y")
            cv.create_text(VIEW_W - 6, VIEW_H - 6, anchor="se", fill=C["dim"], font=F["small"],
                           text="→ %s   ↑ %s" % lab)

    def draw_curves(self):
        cv = self.curves
        cv.delete("all")
        c = self.clip()
        W = max(200, cv.winfo_width())
        H = 92
        if c is None:
            return
        sx = (W - 110) / max(1, c.frames - 1)
        for tr, col, lo, hi, y0, y1 in (("fov", C["ok"], 10, 60, 4, H / 2 - 2), ("roll", C["warn"], -45, 45,
                                                                                 H / 2 + 2, H - 4)):
            vals = [math.degrees(c.at(tr, f)[0]) for f in range(c.frames)]
            lo, hi = min(lo, min(vals)), max(hi, max(vals))
            cv.create_line(100, y1, W - 6, y1, fill=C["line"])
            pts = []
            for f, v in enumerate(vals):
                pts += [100 + f * sx, y1 - (v - lo) / (hi - lo) * (y1 - y0)]
            if len(pts) >= 4:
                cv.create_line(*pts, fill=col, width=2)
            cv.create_text(6, (y0 + y1) / 2, anchor="w", fill=col, font=F["small"],
                           text="%s %.1f°" % (T("fov", "fov") if tr == "fov" else T("giro", "roll"), vals[self.f]))
        x = 100 + self.f * sx
        cv.create_line(x, 2, x, H - 2, fill=C["text"])

    def _curve_click(self, e):
        c = self.clip()
        if c is None:
            return
        W = max(200, self.curves.winfo_width())
        f = round((e.x - 100) / ((W - 110) / max(1, c.frames - 1)))
        self.set_frame(max(0, min(c.frames - 1, f)))

    def fill_numbers(self):
        c = self.clip()
        for key, vs in self.num.items():
            if c is None:
                for v in vs:
                    v.set("")
                continue
            val = c.at(key, self.f)
            if key in ("roll", "fov"):
                val = [math.degrees(val[0])]
            for v, x in zip(vs, val):
                v.set("%.3f" % x)

    # ------------------------------------------------------------------ edicion
    def commit(self, new, msg):
        errs = sc.validate_clip(new)
        if errs:
            self.say(T("No se aplica: %s", "Not applied: %s") % "; ".join(errs), "err")
            return False
        self.cam.clips[self.k] = new
        self.edited.add(self.k)
        self.dirty = True
        self.refresh_tree()
        self.select_clip(self.k)
        self.say(msg, "ok")
        return True

    def radius(self):
        try:
            return max(0, int(self.rad_var.get()))
        except ValueError:
            return 10

    def _press(self, key, e):
        c = self.clip()
        if c is None or key not in getattr(self, "_view_tf", {}):
            return
        tf, s = self._view_tf[key]
        best = None
        for tr in ("ojo", "objetivo"):
            p = tf(self._project(key, c.at(tr, self.f)))
            d = math.hypot(p[0] - e.x, p[1] - e.y)
            if d < 14 and (best is None or d < best[0]):
                best = (d, tr, p)
        if best:
            self.drag = dict(key=key, track=best[1], x0=e.x, y0=e.y, x=e.x, y=e.y, s=s)

    def _motion(self, key, e):
        if self.drag and self.drag["key"] == key:
            self.drag["x"], self.drag["y"] = e.x, e.y
            self.draw_views()

    def _release(self, key, e):
        d, self.drag = self.drag, None
        if not d or d["key"] != key:
            return
        dx, dy = (e.x - d["x0"]) / d["s"], -(e.y - d["y0"]) / d["s"]
        if abs(dx) + abs(dy) < 1e-6:
            self.draw_views()
            return
        delta = [dx, 0.0, dy] if key == "top" else [0.0, dy, dx]
        c = self.clip()
        new = sc.shift(c, d["track"], delta) if self.all_var.get() else sc.brush(c, d["track"], self.f, delta,
                                                                                    self.radius())
        self.commit(new, T("Movido %s (%.2f, %.2f, %.2f).", "Moved %s (%.2f, %.2f, %.2f).") % (
            d["track"], *delta))

    def apply_numbers(self):
        c = self.clip()
        if c is None:
            return
        new = c
        try:
            for key, vs in self.num.items():
                want = [float(v.get().replace(",", ".")) for v in vs]
                if key in ("roll", "fov"):
                    want = [math.radians(want[0])]
                cur = new.at(key, self.f)
                delta = [w - x for w, x in zip(want, cur)]
                if any(abs(x) > 1e-6 for x in delta):
                    new = sc.brush(new, key, self.f, delta, self.radius())
        except ValueError:
            self.say(T("Escribe números (con punto o coma).", "Type numbers."), "err")
            return
        self.commit(new, T("Valores aplicados en el frame %d.", "Values applied at frame %d.") % self.f)

    def apply_tpl(self):
        c = self.clip()
        if c is None:
            return
        key = self.tpl_names[max(0, self.tpl_cb.current())][0]
        try:
            v = {k: float(x.get().replace(",", ".")) for k, x in self.tpl_vars.items()}
        except ValueError:
            self.say(T("Escribe números en la plantilla.", "Type numbers in the template."), "err")
            return
        n = c.frames
        tgt0, eye0 = c.at("objetivo", 0), c.at("ojo", 0)
        if key == "orbita":
            new = sc.tpl_orbita(n, tgt0, v["radio"], v["altura"], v["ang0"], v["barrido"], v["fov"])
        elif key == "travelling":
            p = v["acercar"] / 100.0
            eye1 = [t + (e - t) * (1 - p) for e, t in zip(eye0, tgt0)]
            eye1[1] += v["subir"]
            new = sc.tpl_travelling(n, eye0, eye1, tgt0, c.at("objetivo", n - 1),
                                    math.degrees(c.at("fov", 0)[0]), v["fov1"])
        elif key == "temblor":
            new = sc.tpl_temblor(c, v["amp"], v["hz"], int(v["f0"]), int(v["f1"]))
        else:
            new = sc.tpl_giro(c, v["g0"], v["g1"], int(v["f0"]), int(v["f1"]))
        new.flags, new.variante, new.heads = c.flags, c.variante, dict(c.heads)
        self.commit(new, T("Plantilla aplicada al clip %d.", "Template applied to clip %d.") % self.k)

    def retime(self):
        c = self.clip()
        try:
            n = int(self.len_var.get())
        except ValueError:
            return
        if c is None or n < 2 or n == c.frames:
            return
        if self.commit(sc.retime(c, n), T("Clip %d: %d frames.", "Clip %d: %d frames.") % (self.k, n)):
            self.say(T("Clip %d: %d frames. Ojo: el guion sigue esperando lo mismo (si el clip acaba antes, la "
                       "cámara se queda quieta; si dura más, se corta).", "Clip %d: %d frames. Note: the script "
                       "still waits the same (shorter clips hold the last frame; longer ones are cut).") % (
                self.k, n), "warn")

    def undo_clip(self):
        if self.k >= self.orig_n():
            self.say(T("Los clips nuevos no se borran (el guion los pide por número): edítalos o déjalos.",
                       "New clips are not deleted (the script asks for them by number): edit or keep them."),
                     "warn")
            return
        self.cam.clips[self.k] = self.orig.clips[self.k].copy()
        self.edited.discard(self.k)
        self.dirty = True
        self.refresh_tree()
        self.select_clip(self.k)
        self.say(T("Clip %d como el original.", "Clip %d back to the original.") % self.k, "ok")

    def add_clip(self):
        if not self.cam:
            return
        base = self.clip()
        new = base.copy() if base else sc.tpl_orbita(90, (0, 10.5, 0), 40, 6, 0, 90, 38)
        self.cam.clips.append(new)
        k = len(self.cam.clips) - 1
        self.edited.add(k)
        self.dirty = True
        self.refresh_tree()
        self.select_clip(k)
        self.say(T("Clip %d añadido al final. El guion no lo usará hasta que una técnica lo pida (o "
                   "sustituye un clip existente).", "Clip %d added at the end. The script will not use it until a "
                   "technique asks for it (or replace an existing clip).") % k, "warn")

    def copy_from(self):
        dlg = tk.Toplevel(self.root)
        dlg.title(T("Copiar clip de otro personaje", "Copy a clip from another character"))
        dlg.configure(bg=C["bg"])
        card = mk.Card(dlg, T("Copiar clip", "Copy clip"), T("Sustituye el clip actual (o crea el primero).",
                                                              "Replaces the current clip (or creates the first)."))
        card.pack(fill="both", expand=True, padx=12, pady=12)
        cb = ttk.Combobox(card.body, values=[c["label"] for c in self.chars], state="readonly", width=40)
        cb.pack(fill="x")
        cb.current(0)
        kv = tk.StringVar(value="0")
        f = tk.Frame(card.body, bg=C["card"])
        f.pack(fill="x", pady=6)
        mk.label(f, "Clip", bg=C["card"]).pack(side="left")
        ttk.Spinbox(f, from_=0, to=300, width=5, textvariable=kv).pack(side="left", padx=6)

        def go():
            src = self.chars[cb.current()]
            try:
                raw = sc.hd_entry(src["cam"], self.us) if src["kind"] == "nativo" else sc.load_cam_file(
                    os.path.join(self.mods, src["mod"], src["camara"]))
                other = sc.CamBin(raw)
                c = other.clips[int(kv.get())].copy()
            except (ValueError, IndexError, OSError) as ex:
                self.say(str(ex), "err")
                return
            dlg.destroy()
            if not self.cam.clips:
                self.cam.clips.append(c)
                self.k = 0
            self.commit(c, T("Clip copiado de %s.", "Clip copied from %s.") % src["label"])
        ttk.Button(card.body, text=T("Copiar", "Copy"), style="Accent.TButton", command=go).pack(anchor="w")
        if self.selftest:
            dlg.withdraw()
        return dlg

    # ------------------------------------------------------------------ frames y vista previa
    def _scale(self, v):
        self.set_frame(int(float(v)), from_scale=True)

    def set_frame(self, f, from_scale=False):
        c = self.clip()
        if c is None:
            return
        f = max(0, min(c.frames - 1, f))
        if f == self.f and from_scale:
            return
        self.f = f
        if not from_scale:
            self.scale.set(f)
        self.frame_lbl.configure(text="%d / %d" % (f, c.frames - 1))
        self.draw_views()
        self.draw_curves()
        self.fill_numbers()
        self.render_later()

    def toggle_play(self):
        self.playing = not self.playing
        self.play_btn.configure(text="■" if self.playing else "▶")
        if self.playing:
            self._play()

    def _play(self):
        c = self.clip()
        if not self.playing or c is None:
            return
        self.set_frame((self.f + 4) % c.frames)
        self.root.after(66, self._play)

    def render_later(self):
        if self._render_job:
            self.root.after_cancel(self._render_job)
        self._render_job = self.root.after(40, self.render_now)

    def frame_image(self, f, size=(PREV_W, PREV_H)):
        from PIL import Image  # noqa: PLC0415
        from model_render import render  # noqa: PLC0415
        import numpy as np  # noqa: PLC0415
        c = self.clip()
        W, H = size
        bg = Image.new("RGBA", (W, H), (150, 190, 230, 255))
        if c is None or self.model is None:
            return bg
        old = self.f
        self.f = f
        self.skeleton_now()
        self.f = old
        eye, tgt = c.at("ojo", f), c.at("objetivo", f)
        fw = np.subtract(tgt, eye)
        d = float(np.linalg.norm(fw))
        fw /= d
        r = np.cross(fw, (0.0, 1.0, 0.0))
        r /= np.linalg.norm(r) + 1e-12
        u = np.cross(r, fw)
        roll = c.at("roll", f)[0]
        cs, sn = math.cos(roll), math.sin(roll)
        r, u = r * cs + u * sn, u * cs - r * sn
        fov = c.at("fov", f)[0]
        img = render(self.model, size=(W, H), view=np.array([r, u, -fw]), center=tgt,
                     scale=(H / 2) / (d * math.tan(fov / 2)), persp=d, ss=1, outline=1.2)
        return Image.alpha_composite(bg, Image.fromarray(img))

    def render_now(self):
        self._render_job = None
        try:
            from PIL import ImageTk  # noqa: PLC0415
            im = self.frame_image(self.f)
            self.preview_img = ImageTk.PhotoImage(im)
            self.prev_lbl.delete("all")
            self.prev_lbl.create_image(0, 0, anchor="nw", image=self.preview_img)
            if self.model is None:
                self.prev_lbl.create_text(PREV_W / 2, PREV_H / 2, fill=C["on_accent"], width=PREV_W - 20,
                                          text=T("Sin modelo: solo la cámara.", "No model: camera only."))
        except Exception as ex:  # noqa: BLE001
            self.prev_lbl.delete("all")
            self.prev_lbl.create_text(PREV_W / 2, PREV_H / 2, fill=C["err"], width=PREV_W - 20, text=str(ex)[:160])

    def gif(self):
        c = self.clip()
        if c is None:
            return
        os.makedirs(WORK, exist_ok=True)
        out = os.path.join(WORK, "%s_clip%d.gif" % (self.work_name(), self.k))
        fr = [self.frame_image(f, (320, 180)).convert("RGB") for f in range(0, c.frames, 2)]
        fr[0].save(out, save_all=True, append_images=fr[1:], duration=33, loop=0)
        self.say(T("GIF guardado: %s", "GIF saved: %s") % out, "ok")
        if not self.selftest:
            mk.open_path(out)
        return out

    # ------------------------------------------------------------------ guardar
    def project(self):
        return dict(clips={str(k): self.cam.clips[k].to_json() for k in sorted(self.edited)
                           if k < len(self.cam.clips)}, personaje_id=self.src.get("id"), mod=self.src.get("mod"))

    def save(self):
        if not self.cam or not self.edited:
            self.say(T("No hay cambios que guardar.", "Nothing to save."), "warn")
            return None
        errs = sc.validate_cam(self.cam)
        if errs:
            self.say(T("No se guarda: %s", "Not saved: %s") % "; ".join(errs[:3]), "err")
            return None
        c = self.src
        try:
            if c["kind"] == "nativo":
                r = sc.save_native(self.cam, self.mods, c["nombre"], c["cam"], self.project(), self.us)
            else:
                r = sc.save_port(self.cam, os.path.join(self.mods, c["mod"]), c["camara"], self.project())
        except Exception as ex:  # noqa: BLE001
            self.say(T("No se pudo guardar: %s", "Could not save: %s") % ex, "err")
            traceback.print_exc()
            return None
        self.dirty = False
        lines = [T("Guardado en el mod «%s».", "Saved in mod '%s'.") % r["mod"]]
        if r["respaldo"]:
            lines.append(T("La versión anterior está en %s.", "The previous version is in %s.") % os.path.dirname(
                r["respaldo"][0]))
        if c["kind"] == "nativo":
            lines.append(T("Con el juego abierto: Pausa → «Reelegir personajes» y repite la técnica.",
                           "With the game open: Pause → 'Reselect characters' and repeat the technique.")
                         if not r["reiniciar"] else
                         T("Reinicia el juego para verlo (mod nuevo o tamaño reservado ampliado).",
                           "Restart the game to see it (new mod or bigger reserved size)."))
        else:
            lines.append(T("Se monta al pulsar JUGAR o «Reconstruir ahora» en el Mod Kit.",
                           "It is built on PLAY or 'Rebuild now' in the Mod Kit."))
        if r["conflictos"]:
            lines.append(T("Aviso: estos mods también cambian esa cámara y pueden ganar: %s",
                           "Warning: these mods also change that camera and may win: %s") % ", ".join(
                r["conflictos"]))
        self.say("  ".join(lines), "warn" if r["conflictos"] or r["reiniciar"] else "ok")
        if not self.selftest:
            messagebox.showinfo("DBZ3 HD Studio", "\n\n".join(lines), parent=self.root)
        return r

    # ------------------------------------------------------------------ glTF / Blender
    def work_name(self):
        return sc.slug(self.src.get("mod") or self.src["nombre"]) if self.src else "studio"

    def _glb_for_clip(self, path):
        c = self.clip()
        mb = ab = None
        try:
            if self.src["kind"] == "nativo":
                mb, ab = sc.hd_entry(self.src["modelo"], self.us), sc.hd_entry(self.src["anm"], self.us)
            else:
                mb, ab = sc.load_model_bin(self.src.get("modelo")), sc.load_model_bin(self.src.get("anm"))
        except Exception:  # noqa: BLE001
            mb = ab = None
        try:
            anim = int(self.anim_var.get())
        except ValueError:
            anim = 0
        return sc.export_glb(path, c, mb, ab, anim, "camara_clip%d" % self.k)

    def export_glb(self):
        if self.clip() is None:
            return
        p = filedialog.asksaveasfilename(parent=self.root, defaultextension=".glb", filetypes=[("glTF", "*.glb")],
                                         initialfile="%s_clip%d.glb" % (self.work_name(), self.k))
        if p:
            self._glb_for_clip(p)
            self.say(T("Exportado: %s", "Exported: %s") % p, "ok")

    def import_glb(self, path=None):
        path = path or filedialog.askopenfilename(parent=self.root, filetypes=[("glTF", "*.glb")])
        if not path:
            return
        try:
            new = sc.clip_from_track(sc.glb_camera_track(path), self.clip())
        except Exception as ex:  # noqa: BLE001
            self.say(T("No se pudo leer el .glb: %s", "Could not read the .glb: %s") % ex, "err")
            return
        if not self.cam.clips:
            self.cam.clips.append(new)
            self.k = 0
        was = self.clip().frames
        ok = self.commit(new, T("Cámara importada (%d frames).", "Camera imported (%d frames).") % new.frames)
        if ok and new.frames != was:
            self.say(T("Cámara importada con %d frames (antes %d): el guion espera lo mismo.",
                       "Camera imported with %d frames (was %d): the script waits the same.") % (new.frames, was),
                     "warn")

    def _paths(self):
        os.makedirs(WORK, exist_ok=True)
        base = os.path.join(WORK, "%s_clip%d" % (self.work_name(), self.k))
        return base + ".glb", base + ".blend", base + "_vuelta.glb"

    def to_blender(self):
        if self.clip() is None:
            return
        blender = sc.find_blender(self.env.settings.get("blender"))
        glb, blend, _ = self._paths()
        self._glb_for_clip(glb)
        if not blender:
            self.say(T("No encuentro Blender. Exportado %s: ábrelo en tu programa 3D y usa «Importar .glb…» "
                       "al volver.", "Blender not found. Exported %s: open it in your 3D app and use 'Import "
                       ".glb…' when done.") % glb, "warn")
            return
        sc.blender_open(blender, glb, blend)
        self.say(T("Blender se abre con la toma (60 fps, frame 0 = inicio). Mueve la cámara, guarda con Ctrl+S y "
                   "pulsa «Traer de Blender».", "Blender opens with the shot (60 fps, frame 0 = start). Move the "
                   "camera, save with Ctrl+S and press 'Bring from Blender'."), "ok")

    def from_blender(self):
        if self.clip() is None:
            return
        glb, blend, back = self._paths()
        blender = sc.find_blender(self.env.settings.get("blender"))
        if not (blender and os.path.isfile(blend)):
            self.say(T("No hay .blend de este clip: pulsa antes «Abrir en Blender» (o usa «Importar .glb…»).",
                       "No .blend for this clip: press 'Open in Blender' first (or use 'Import .glb…')."), "warn")
            return
        self.say(T("Blender exporta en segundo plano…", "Blender is exporting in the background…"))
        k = self.k

        def work():
            try:
                sc.blender_export(blender, blend, back)
                self.q.put(("glb", k, back))
            except Exception as ex:  # noqa: BLE001
                self.q.put(("err", k, str(ex)))
        threading.Thread(target=work, daemon=True).start()

    def poll(self):
        try:
            while True:
                kind, k, val = self.q.get_nowait()
                if k != self.k:
                    self.select_clip(k)
                if kind == "glb":
                    self.import_glb(val)
                else:
                    self.say(val, "err")
        except queue.Empty:
            pass
        except Exception as ex:  # noqa: BLE001  (un error aqui paraba el sondeo para siempre)
            self.say(T("Error: %s", "Error: %s") % ex, "err")
            traceback.print_exc()
        finally:
            self.root.after(150, self.poll)

    def ask(self, msg):
        return True if self.selftest else messagebox.askyesno("DBZ3 HD Studio", msg, parent=self.root)

    def on_close(self):
        """Cerrar la ventana: con cambios sin guardar, se pregunta (antes se perdian sin aviso)."""
        if self.dirty and self.src and not self.ask(T("Hay cambios sin guardar en %s. ¿Cerrar y descartarlos?",
                                                      "There are unsaved changes in %s. Close and discard them?")
                                                    % self.src["label"]):
            return
        self.root.destroy()


def report_error(*exc):
    """Errores de los botones (Tk los tragaba sin consola): ventana con el motivo + .log."""
    err = "".join(traceback.format_exception(*exc))
    log = os.path.join(os.environ.get("TEMP") or HERE, "dbz3_studio_error.log")
    try:
        with open(log, "a", encoding="utf-8") as fh:
            fh.write(err + "\n")
    except OSError:
        pass
    messagebox.showerror("DBZ3 HD Studio", T("Algo ha fallado: %s\n\n(detalles en %s)",
                                             "Something went wrong: %s\n\n(details in %s)") % (exc[1], log))


# ================================================================================ autotest
def selftest(env, lang):
    """Construye la ventana oculta, carga Goku, edita con cada herramienta y guarda en mods de prueba."""
    ok = True
    t0 = time.time()

    def say(m):
        print("[studio_gui] " + m, flush=True)
    root = tk.Tk()
    root.withdraw()
    global WORK
    mods = tempfile.mkdtemp(prefix="studio_gui_mods_")
    env.mods = mods
    WORK = os.path.join(mods, "trabajo")
    try:
        top = tk.Toplevel(root)
        top.withdraw()
        app = StudioApp(top, env, start=0, selftest=True)
        root.update_idletasks()
        say("personajes: %d (nativos %d), clips de Goku %d, guiones %d" % (
            len(app.chars), sum(1 for c in app.chars if c["kind"] == "nativo"), len(app.cam.clips), len(app.seqs)))
        for i in range(len(app.seqs) + 1):
            app.seq_cb.current(i)
            app.pick_seq()
            root.update_idletasks()
        for j in range(len(app.tpl_names)):
            app.tpl_cb.current(j)
            app._tpl_fields()
            app.apply_tpl()
        app.set_frame(10)
        tf, s = app._view_tf["top"]
        p = tf(app._project("top", app.clip().at("ojo", 10)))
        before = app.clip().at("ojo", 10)
        app._press("top", type("E", (), dict(x=p[0], y=p[1]))())
        app._release("top", type("E", (), dict(x=p[0] + 20, y=p[1]))())
        after = app.clip().at("ojo", 10)
        if abs((after[0] - before[0]) - 20 / s) > 1e-3:
            ok = False
            say("FALLO arrastre: %s -> %s" % (before, after))
        app.num["fov"][0].set("33")
        app.apply_numbers()
        if abs(math.degrees(app.clip().at("fov", 10)[0]) - 33) > 1e-3:
            ok = False
            say("FALLO valores numericos")
        app.len_var.set(str(app.clip().frames + 10))
        app.retime()
        app.add_clip()
        app.copy_from().destroy()
        app.render_now()
        if app.preview_img is None:
            ok = False
            say("FALLO: sin vista previa")
        gif = app.gif()
        glb = os.path.join(mods, "x.glb")
        app._glb_for_clip(glb)
        app.import_glb(glb)
        r = app.save()
        if not r or not os.path.isfile(r["ruta"]):
            ok = False
            say("FALLO guardar: %s" % (r,))
        else:
            say("guardado: %s (%d B, reservado %d) editados %s" % (r["mod"], r["tamano"], r["reservado"],
                                                                   sorted(app.edited)))
        top2 = tk.Toplevel(root)
        top2.withdraw()
        app2 = StudioApp(top2, env, start=0, selftest=True)       # vuelve a abrir: carga studio.json
        if app2.edited != app.edited:
            ok = False
            say("FALLO: al reabrir no se cargan los cambios (%s vs %s)" % (app2.edited, app.edited))
        say("gif %s, glb %s" % (os.path.basename(gif), os.path.basename(glb)))
        say("estado: %s" % app.status.cget("text")[:200])
    except Exception:  # noqa: BLE001
        ok = False
        say("EXCEPCION\n" + traceback.format_exc())
    finally:
        root.destroy()
    say("RESULTADO: %s (%.1f s)" % ("OK" if ok else "FALLO", time.time() - t0))
    return 0 if ok else 1


def capture(app, out, steps):
    """Captura de la ventana (para el informe): PIL.ImageGrab del rectangulo de la ventana."""
    from PIL import ImageGrab  # noqa: PLC0415
    for fn in steps:
        fn()
        app.root.update()
        time.sleep(0.4)
        app.root.update()
    x, y = app.root.winfo_rootx(), app.root.winfo_rooty()
    w, h = app.root.winfo_width(), app.root.winfo_height()
    ImageGrab.grab((x, y, x + w, y + h)).save(out)
    print("captura", out, w, h)


def main(argv=None):
    ap = argparse.ArgumentParser(description="DBZ3 HD Studio")
    ap.add_argument("--personaje", help="ID nativo o carpeta del mod")
    ap.add_argument("--lang", choices=("es", "en"))
    ap.add_argument("--mods")
    ap.add_argument("--us")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--captura", help="PNG: abre, captura la ventana y sale")
    a = ap.parse_args(argv)
    settings = mk.load_settings()
    mk.LANG = a.lang or settings.get("lang") or "es"
    env = mk.Env(settings)
    if a.mods:
        env.mods = a.mods
    if a.us:
        env.us = a.us
    if a.selftest:
        return selftest(env, mk.LANG)
    if os.name == "nt":
        try:
            import ctypes  # noqa: PLC0415
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("DBZ3HD.Studio")
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except (AttributeError, OSError):
            pass
    root = tk.Tk()
    try:
        root.iconphoto(True, mk.dragonball_image(32))
    except tk.TclError:
        pass
    sw, sh = root.winfo_screenwidth(), root.winfo_screenheight()
    w, h = min(1440, sw - 40), min(900, sh - 60)
    root.geometry("%dx%d+%d+%d" % (w, h, max(0, (sw - w) // 2), max(0, (sh - h) // 3)))
    root.report_callback_exception = report_error
    app = StudioApp(root, env, start=a.personaje)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    if a.captura:
        def go():
            capture(app, a.captura, [lambda: app.set_frame(30)])
            root.destroy()
        root.after(1500, go)
    root.mainloop()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except Exception:  # noqa: BLE001  (sin consola: el error se ve en una ventana y en un .log)
        err = traceback.format_exc()
        log = os.path.join(os.environ.get("TEMP") or HERE, "dbz3_studio_error.log")
        try:
            with open(log, "w", encoding="utf-8") as fh:
                fh.write(err)
            messagebox.showerror("DBZ3 HD Studio", "Error:\n\n%s\n\n(%s)" % (err[-1500:], log))
        except Exception:  # noqa: BLE001
            print(err, file=sys.stderr)
        sys.exit(1)
