#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""hexedit.py - Editor hexadecimal para modders expertos (pestaña «Hex» del Mod Kit avanzado).

Como HxD, pero sabiendo de los formatos del juego:
  - vista hex + ASCII de ficheros de cualquier tamaño (se lee en su sitio, sin cargarlo entero)
  - edición sobrescribiendo (teclea dígitos hex en la columna hex o texto en la ASCII), deshacer
  - ir a offset, buscar bytes hex o texto (siguiente con F3)
  - inspector en el cursor: u8/u16/u32/f32 en little endian (PS2/PSP) y big endian (HD 360)
  - estructura: árbol de los #AMB (hijos con offset/tamaño/magia, en los dos endian) y, en los #AMO
    de PS2, sus partes con vtype/vflags y «Ocultar/Mostrar parte» (el truco de los trajes
    alternativos: vtype/vflags = FFFFFF00 FFFF0000; amo2awo omite esas partes)
  - al guardar: copia de seguridad (<fichero>.<fecha>.bak) o, si pasa de 256 MB, un .hexundo.json
    con los bytes originales (botón «Restaurar»)

  python hexedit.py [fichero]
"""
import json
import mmap
import os
import struct
import sys
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (os.path.join(os.path.dirname(HERE), "awo_tools"), HERE):
    if _p not in sys.path:
        sys.path.append(_p)

ROW = 16
BIG = 256 * 1024 * 1024
HIDE = bytes.fromhex("FFFFFF00FFFF0000")
COLORS = {"bg": "#111318", "card": "#1A1D24", "text": "#EDF0F5", "dim": "#8F96A6", "accent": "#F58C1C",
          "err": "#FF6B5C", "sel": "#3D414E", "line": "#323641"}


def _tr(es, en):
    return es


# ------------------------------------------------------------------ formatos
def amb_children(buf, base, size, depth=0):
    """[(nivel, offset absoluto, tamaño, magia, endian)] del #AMB en buf[base:base+size] (recursivo)."""
    out = []
    if depth > 6 or size < 0x20 or buf[base:base + 4] != b"#AMB":
        return out
    for e in ("<", ">"):
        n, tab = struct.unpack_from(e + "II", buf, base + 0x10)
        if 0 < n < 4096 and 0x10 <= tab < size and tab + 16 * n <= size:
            break
    else:
        return out
    for k in range(n):
        off, sz, _ty, _ = struct.unpack_from(e + "4I", buf, base + tab + 16 * k)
        if not sz or off + sz > size:
            continue
        a = base + off
        magic = bytes(buf[a:a + 4]).decode("latin1")
        out.append((depth, a, sz, magic if magic.isprintable() else "?", "LE" if e == "<" else "BE"))
        if buf[a:a + 4] == b"#AMB":
            out += amb_children(buf, a, sz, depth + 1)
    return out


def amo_parts(buf, a, size):
    """Partes de un #AMO de PS2: [(offset absoluto de la cabecera, AMG, hueso, vtype, vflags, oculta)]."""
    import amo2awo  # noqa: PLC0415
    m = amo2awo.PS2AMO(bytes(buf[a:a + size]))
    out = []
    for gi, g in enumerate(m.amgs):
        for p in g.all_parts:
            out.append((a + p.off, gi, p.bone, p.vtype, p.vflags, p.hidden))
    return out


# ------------------------------------------------------------------ editor
class HexEditor(tk.Frame):
    def __init__(self, master, colors=None, fonts=None, tr=None):
        self.C = dict(COLORS, **(colors or {}))
        super().__init__(master, bg=self.C["bg"])
        self.T = tr or _tr
        mono = (fonts or {}).get("mono", ("Consolas", 10))
        self.font = (mono[0], 10)
        self.path, self.f, self.mm, self.size = None, None, None, 0
        self.patch, self.undo = {}, []        # {offset: byte}; [(offset, byte anterior | None)]
        self.top, self.cur, self.nib, self.side = 0, 0, 0, "hex"
        self.last_find = None
        self._build()

    # ---------------------------------------------------------------- UI
    def _build(self):
        T, C = self.T, self.C
        bar = tk.Frame(self, bg=C["bg"])
        bar.pack(fill="x", pady=(0, 6))
        for txt, cmd in ((T("Abrir…", "Open…"), self.ask_open), (T("Guardar", "Save"), self.save),
                         (T("Deshacer", "Undo"), self.undo_last), (T("Ir a…", "Go to…"), self.ask_goto),
                         (T("Buscar…", "Find…"), self.ask_find), (T("Restaurar copia", "Restore backup"), self.restore)):
            ttk.Button(bar, text=txt, command=cmd).pack(side="left", padx=(0, 6))
        self.info = tk.Label(bar, text=T("Abre un fichero (.amb, .bin, .afs…)", "Open a file (.amb, .bin, .afs…)"),
                             bg=C["bg"], fg=C["dim"], anchor="w")
        self.info.pack(side="left", fill="x", expand=True, padx=8)
        pw = ttk.PanedWindow(self, orient="horizontal")
        pw.pack(fill="both", expand=True)
        left = tk.Frame(pw, bg=C["bg"])
        self.tree = ttk.Treeview(left, columns=("off", "size", "info"), show="tree headings", height=20)
        for c, w, t in (("#0", 150, T("Bloque", "Block")), ("off", 80, "Offset"), ("size", 70, T("Tamaño", "Size")),
                        ("info", 150, "Info")):
            self.tree.heading(c, text=t)
            self.tree.column(c, width=w, stretch=c == "info")
        self.tree.pack(fill="both", expand=True)
        self.tree.bind("<Double-1>", self._tree_go)
        tb = tk.Frame(left, bg=C["bg"])
        tb.pack(fill="x", pady=(4, 0))
        ttk.Button(tb, text=T("Ocultar / mostrar parte", "Hide / show part"), command=self.toggle_part).pack(side="left")
        pw.add(left, weight=1)
        mid = tk.Frame(pw, bg=C["bg"])
        self.text = tk.Text(mid, font=self.font, bg=C["card"], fg=C["text"], insertbackground=C["text"],
                            relief="flat", wrap="none", width=78, cursor="xterm", undo=False)
        self.sb = ttk.Scrollbar(mid, orient="vertical", command=self._scroll)
        self.text.pack(side="left", fill="both", expand=True)
        self.sb.pack(side="right", fill="y")
        self.text.tag_configure("off", foreground=C["dim"])
        self.text.tag_configure("mod", foreground=C["err"])
        self.text.tag_configure("cur", background=C["accent"], foreground="#16120C")
        self.text.bind("<Button-1>", self._click)
        self.text.bind("<Key>", self._key)
        self.text.bind("<MouseWheel>", lambda e: (self._scroll("scroll", -e.delta // 40, "units"), "break")[1])
        self.text.bind("<Configure>", lambda e: self.render())
        pw.add(mid, weight=3)
        self.insp = tk.Label(self, text="", bg=C["bg"], fg=C["text"], font=self.font, anchor="w", justify="left")
        self.insp.pack(fill="x", pady=(6, 0))
        self.bind_all("<F3>", lambda e: self.find_next(), add="+")

    def lines(self):
        try:
            return max(4, int(self.text.winfo_height() / max(1, self.text.tk.call("font", "metrics", self.font,
                                                                                    "-linespace"))) - 1)
        except tk.TclError:
            return 30

    # ---------------------------------------------------------------- datos
    def byte(self, o):
        return self.patch[o] if o in self.patch else self.mm[o]

    def get(self, o, n):
        n = max(0, min(n, self.size - o))
        b = bytearray(self.mm[o:o + n]) if self.mm is not None else bytearray()
        for k in range(n):
            if o + k in self.patch:
                b[k] = self.patch[o + k]
        return bytes(b)

    def open(self, path):
        if self.patch and not messagebox.askyesno("Hex", self.T("Hay cambios sin guardar. ¿Descartarlos?",
                                                                "Unsaved changes. Discard them?")):
            return
        self.close()
        self.path, self.size = path, os.path.getsize(path)
        self.f = open(path, "rb")
        self.mm = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ) if self.size else b""
        self.top = self.cur = 0
        self.fill_tree()
        self.render()

    def close(self):
        if self.mm is not None and hasattr(self.mm, "close"):
            self.mm.close()
        if self.f:
            self.f.close()
        self.f, self.mm, self.patch, self.undo = None, None, {}, []

    def ask_open(self):
        p = filedialog.askopenfilename(title=self.T("Abrir fichero", "Open file"))
        if p:
            self.open(p)

    # ---------------------------------------------------------------- estructura
    def fill_tree(self):
        self.tree.delete(*self.tree.get_children())
        if not self.size or self.size > BIG:
            return
        head = bytes(self.mm[:4])
        if head != b"#AMB" and head != b"#AMO":
            return
        parents = {-1: ""}
        kids = [(0, 0, self.size, head.decode(), "")] if head == b"#AMO" else \
            [(d + 1, a, s, m, e) for d, a, s, m, e in amb_children(self.mm, 0, self.size)]
        if head == b"#AMB":
            parents[0] = self.tree.insert("", "end", text="#AMB", values=("0x0", self.size, ""), open=True)
        for d, a, s, m, e in kids:
            it = self.tree.insert(parents.get(d - 1, ""), "end", text=m, values=("0x%X" % a, s, e), open=d < 2)
            parents[d] = it
            if m == "#AMO":
                try:
                    for po, gi, bone, vt, vf, hid in amo_parts(self.mm, a, s):
                        self.tree.insert(it, "end", text=self.T("parte", "part"), values=(
                            "0x%X" % po, "", "AMG%d hueso %d vtype %X vflags %X%s" % (
                                gi, bone, vt, vf, self.T("  OCULTA", "  HIDDEN") if hid else "")))
                except Exception as ex:  # noqa: BLE001
                    self.tree.insert(it, "end", text="?", values=("", "", str(ex)[:60]))

    def _tree_go(self, _e=None):
        sel = self.tree.selection()
        if sel:
            v = self.tree.item(sel[0], "values")
            if v and v[0].startswith("0x"):
                self.goto(int(v[0], 16))

    def toggle_part(self):
        """Parte de #AMO elegida: vtype/vflags = FFFFFF00 FFFF0000 (oculta) o los de antes."""
        sel = self.tree.selection()
        if not sel or self.tree.item(sel[0], "text") != self.T("parte", "part"):
            messagebox.showinfo("Hex", self.T("Elige una «parte» de un #AMO en el árbol.",
                                              "Pick a 'part' of an #AMO in the tree."))
            return
        po = int(self.tree.item(sel[0], "values")[0], 16)
        cur = self.get(po, 8)
        if cur == HIDE:
            old = self._saved_vtype.get(po) if hasattr(self, "_saved_vtype") else None
            if old is None:
                messagebox.showinfo("Hex", self.T("No sé su vtype original: escríbelo a mano (p. ej. B5 01 00 00 "
                                                  "BD 29 00 00, el de otra parte igual).", "Original vtype unknown: "
                                                  "type it (e.g. B5 01 00 00 BD 29 00 00, from a similar part)."))
                self.goto(po)
                return
            new = old
        else:
            self._saved_vtype = getattr(self, "_saved_vtype", {})
            self._saved_vtype[po] = cur
            new = HIDE
        for k, b in enumerate(new):
            self.set_byte(po + k, b)
        self.goto(po)

    # ---------------------------------------------------------------- vista
    def _scroll(self, *a):
        rows = (self.size + ROW - 1) // ROW
        if a[0] == "moveto":
            self.top = int(float(a[1]) * rows)
        elif a[0] == "scroll":
            self.top += int(a[1]) * (self.lines() if a[2] == "pages" else 1)
        self.top = max(0, min(self.top, max(0, rows - self.lines())))
        self.render()

    def render(self):
        t = self.text
        t.configure(state="normal")
        t.delete("1.0", "end")
        if self.mm is None:
            return
        n = self.lines()
        rows = max(1, (self.size + ROW - 1) // ROW)
        data = self.get(self.top * ROW, n * ROW)
        for r in range(n):
            o = (self.top + r) * ROW
            if o >= self.size and self.size:
                break
            chunk = data[r * ROW:(r + 1) * ROW]
            t.insert("end", "%08X  " % o, "off")
            for k in range(ROW):
                if k < len(chunk):
                    tags = ("mod",) if o + k in self.patch else ()
                    tags += ("cur",) if o + k == self.cur and self.side == "hex" else ()
                    t.insert("end", "%02X" % chunk[k], tags)
                else:
                    t.insert("end", "  ")
                t.insert("end", "  " if k == 7 else " ")
            t.insert("end", " ")
            for k, b in enumerate(chunk):
                tags = ("mod",) if o + k in self.patch else ()
                tags += ("cur",) if o + k == self.cur and self.side == "asc" else ()
                t.insert("end", chr(b) if 32 <= b < 127 else ".", tags)
            t.insert("end", "\n")
        self.sb.set(self.top / rows, min(1.0, (self.top + n) / rows))
        self.inspect()
        mod = self.T("  ·  %d bytes cambiados", "  ·  %d bytes changed") % len(self.patch) if self.patch else ""
        self.info.configure(text="%s  ·  %d bytes  ·  0x%X%s" % (os.path.basename(self.path or ""), self.size,
                                                                  self.cur, mod))

    def inspect(self):
        b = self.get(self.cur, 8)
        if not b:
            self.insp.configure(text="")
            return
        b8 = b.ljust(8, b"\0")
        u16l, u16b = struct.unpack("<H", b8[:2])[0], struct.unpack(">H", b8[:2])[0]
        u32l, u32b = struct.unpack("<I", b8[:4])[0], struct.unpack(">I", b8[:4])[0]
        f32l, f32b = struct.unpack("<f", b8[:4])[0], struct.unpack(">f", b8[:4])[0]
        self.insp.configure(text=(
            "0x%08X   u8 %d   i8 %d\n"
            "LE (PS2/PSP)  u16 %d  u32 %d (0x%X)  f32 %.6g\n"
            "BE (HD 360)   u16 %d  u32 %d (0x%X)  f32 %.6g") % (
            self.cur, b[0], b[0] - 256 * (b[0] > 127), u16l, u32l, u32l, f32l, u16b, u32b, u32b, f32b))

    def goto(self, o):
        if not self.size:
            return
        self.cur, self.nib = max(0, min(o, self.size - 1)), 0
        row, n = self.cur // ROW, self.lines()
        if not self.top <= row < self.top + n:
            self.top = max(0, row - n // 3)
        self.render()

    def _click(self, e):
        idx = self.text.index("@%d,%d" % (e.x, e.y))
        ln, col = (int(x) for x in idx.split("."))
        row = self.top + ln - 1
        hex0, asc0 = 10, 10 + ROW * 3 + 2
        if hex0 <= col < asc0 - 1:
            c = col - hex0 - (1 if col - hex0 >= 8 * 3 + 1 else 0)
            k, self.side = min(ROW - 1, c // 3), "hex"
        elif col >= asc0:
            k, self.side = min(ROW - 1, col - asc0), "asc"
        else:
            return "break"
        self.text.focus_set()
        self.goto(row * ROW + k)
        return "break"

    def _key(self, e):
        moves = {"Left": -1, "Right": 1, "Up": -ROW, "Down": ROW, "Prior": -ROW * self.lines(),
                 "Next": ROW * self.lines()}
        if e.keysym in moves:
            self.goto(self.cur + moves[e.keysym])
        elif e.keysym == "Home" and e.state & 4:
            self.goto(0)
        elif e.keysym == "End" and e.state & 4:
            self.goto(self.size - 1)
        elif e.keysym.lower() == "z" and e.state & 4:
            self.undo_last()
        elif e.keysym.lower() == "s" and e.state & 4:
            self.save()
        elif e.keysym.lower() == "g" and e.state & 4:
            self.ask_goto()
        elif e.keysym.lower() == "f" and e.state & 4:
            self.ask_find()
        elif self.mm is not None and self.size and e.char and not e.state & 4:
            if self.side == "hex" and e.char.lower() in "0123456789abcdef":
                v = int(e.char, 16)
                old = self.byte(self.cur)
                new = (v << 4 | old & 0xF) if self.nib == 0 else (old & 0xF0 | v)
                self.set_byte(self.cur, new)
                if self.nib == 0:
                    self.nib = 1
                    self.render()
                else:
                    self.goto(self.cur + 1)
            elif self.side == "asc" and 32 <= ord(e.char) < 127:
                self.set_byte(self.cur, ord(e.char))
                self.goto(self.cur + 1)
        return "break"

    def set_byte(self, o, v):
        if self.byte(o) == v:
            return
        self.undo.append((o, self.patch.get(o)))
        if v == self.mm[o]:
            self.patch.pop(o, None)
        else:
            self.patch[o] = v

    def undo_last(self):
        if not self.undo:
            return
        o, prev = self.undo.pop()
        if prev is None:
            self.patch.pop(o, None)
        else:
            self.patch[o] = prev
        self.goto(o)

    # ---------------------------------------------------------------- ir / buscar
    def ask_goto(self):
        from tkinter import simpledialog  # noqa: PLC0415
        s = simpledialog.askstring("Hex", self.T("Offset (hex, p. ej. 552C0; o decimal con #):",
                                                 "Offset (hex, e.g. 552C0; or decimal with #):"), parent=self)
        if s:
            try:
                self.goto(int(s[1:]) if s.startswith("#") else int(s.replace("0x", ""), 16))
            except ValueError:
                messagebox.showerror("Hex", self.T("Offset no válido", "Invalid offset"))

    def ask_find(self):
        from tkinter import simpledialog  # noqa: PLC0415
        s = simpledialog.askstring("Hex", self.T("Bytes hex (FF FF FF 00) o texto entre comillas (\"#AMO\"):",
                                                 "Hex bytes (FF FF FF 00) or quoted text (\"#AMO\"):"), parent=self)
        if not s:
            return
        try:
            pat = s.strip('"').encode("latin1") if s.startswith('"') else bytes.fromhex(s.replace(" ", ""))
        except ValueError:
            messagebox.showerror("Hex", self.T("Patrón no válido", "Invalid pattern"))
            return
        self.last_find = pat
        self.find_next(start=self.cur)

    def find_next(self, start=None):
        if not self.last_find or self.mm is None:
            return
        o = self.mm.find(self.last_find, (self.cur + 1) if start is None else start)
        if o < 0:
            o = self.mm.find(self.last_find, 0)
        if o < 0:
            messagebox.showinfo("Hex", self.T("No encontrado (busca en el fichero guardado).",
                                              "Not found (searches the saved file)."))
            return
        self.goto(o)

    # ---------------------------------------------------------------- guardar
    def save(self):
        if not self.patch or not self.path:
            return
        stamp = time.strftime("%Y%m%d_%H%M%S")
        try:
            if self.size <= BIG:
                import shutil  # noqa: PLC0415
                bak = "%s.%s.bak" % (self.path, stamp)
                shutil.copyfile(self.path, bak)
            else:
                bak = "%s.%s.hexundo.json" % (self.path, stamp)
                with open(bak, "w", encoding="utf-8") as fh:
                    json.dump({str(o): self.mm[o] for o in self.patch}, fh)
            path, patch = self.path, dict(self.patch)
            self.patch = {}
            self.close()
            with open(path, "r+b") as fh:
                for o in sorted(patch):
                    fh.seek(o)
                    fh.write(bytes([patch[o]]))
            cur, top = self.cur, self.top
            self.open(path)
            self.cur, self.top = cur, top
            self.render()
            self.info.configure(text=self.info.cget("text") + self.T("  ·  guardado (copia: %s)", "  ·  saved (backup: %s)")
                                % os.path.basename(bak))
        except OSError as ex:
            messagebox.showerror("Hex", str(ex))

    def restore(self):
        """Vuelve a una copia .bak o aplica un .hexundo.json de este fichero."""
        if not self.path:
            return
        p = filedialog.askopenfilename(title=self.T("Copia a restaurar", "Backup to restore"),
                                       initialdir=os.path.dirname(self.path),
                                       filetypes=[(self.T("Copias", "Backups"), "*.bak *.hexundo.json")])
        if not p or not messagebox.askyesno("Hex", self.T("¿Restaurar %s sobre %s?", "Restore %s over %s?") % (
                os.path.basename(p), os.path.basename(self.path))):
            return
        path = self.path
        self.patch = {}
        self.close()
        import shutil  # noqa: PLC0415
        keep = "%s.%s.antes_de_restaurar.bak" % (path, time.strftime("%Y%m%d_%H%M%S"))
        if os.path.getsize(path) <= BIG:
            shutil.copyfile(path, keep)
        if p.endswith(".hexundo.json"):
            with open(p, encoding="utf-8") as fh, open(path, "r+b") as out:
                for o, v in json.load(fh).items():
                    out.seek(int(o))
                    out.write(bytes([v]))
        else:
            shutil.copyfile(p, path)
        self.open(path)


def selftest():
    """Sin ventana: estructura de un #AMB sintético (LE y BE)."""
    for e in ("<", ">"):
        kid = b"#AMO" + bytes(28)
        amb = bytearray(b"#AMB" + bytes(12) + struct.pack(e + "II", 1, 0x20) + bytes(8))
        amb += struct.pack(e + "4I", 0x40, len(kid), 1, 0) + bytes(16) + kid
        ch = amb_children(amb, 0, len(amb))
        assert ch == [(0, 0x40, 32, "#AMO", "LE" if e == "<" else "BE")], ch
    print("hexedit: OK")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
        sys.exit(0)
    root = tk.Tk()
    root.title("DBZ3 HD Hex")
    root.geometry("1180x720")
    root.configure(bg=COLORS["bg"])
    ed = HexEditor(root)
    ed.pack(fill="both", expand=True, padx=10, pady=10)
    if len(sys.argv) > 1:
        root.after(100, lambda: ed.open(sys.argv[1]))
    root.mainloop()
