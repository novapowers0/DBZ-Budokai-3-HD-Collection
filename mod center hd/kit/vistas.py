# -*- coding: utf-8 -*-
"""Vistas previas del Mod Kit: galeria de PNG, cuerpo del personaje (render toon del juego) y escuchar audio.

Nada de esto se reparte: las imagenes salen de los datos del jugador y se guardan en la carpeta temporal."""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import threading
import tkinter as tk
from tkinter import ttk

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def cache_dir():
    d = os.path.join(os.environ.get("DBZ3_TMP") or tempfile.gettempdir(), "dbz3_kit_vistas")
    os.makedirs(d, exist_ok=True)
    return d


class Galeria(tk.Frame):
    """Miniaturas de los PNG de una carpeta, cargadas poco a poco para no congelar la ventana."""

    def __init__(self, master, g, columnas=6, lado=112, tamanos=None):
        super().__init__(master, bg=master.cget("bg"))
        self.g, self.columnas, self.lado, self.fotos, self.carpeta, self.firma = g, columnas, lado, [], "", None
        self.tamanos = tamanos or {}

    def cargar(self, carpeta, tamanos=None):
        """`tamanos` = {nombre.png: (w, h)} esperado; una imagen con otro tamano sale avisada en rojo."""
        pngs = sorted(f for f in os.listdir(carpeta) if f.lower().endswith(".png")) if os.path.isdir(carpeta) else []
        firma = (carpeta, tuple((f, os.path.getmtime(os.path.join(carpeta, f))) for f in pngs))
        if firma == self.firma:
            return len(pngs)
        self.firma, self.carpeta, self.tamanos = firma, carpeta, tamanos or {}
        for w in self.winfo_children():
            w.destroy()
        self.fotos = []
        self._seguir(pngs, 0, firma)
        return len(pngs)

    def _seguir(self, pngs, i, firma):
        if firma != self.firma or i >= len(pngs) or not self.winfo_exists():
            return
        self._celda(pngs[i], i)
        self.after(1, lambda: self._seguir(pngs, i + 1, firma))

    def _celda(self, nombre, i):
        from PIL import Image, ImageTk
        g, ruta = self.g, os.path.join(self.carpeta, nombre)
        marco = tk.Frame(self, bg=g.C["frame"], highlightthickness=1, highlightbackground=g.C["line"])
        marco.grid(row=i // self.columnas, column=i % self.columnas, padx=3, pady=3, sticky="n")
        try:
            with Image.open(ruta) as im:
                w, h = im.size
                base = im.convert("RGBA")
        except OSError:
            g.label(marco, g.T("No se puede leer", "Cannot be read"), fg=g.C["err"], bg=g.C["frame"]).pack()
            return
        base.thumbnail((self.lado, self.lado))
        fondo = damero(base.size)
        if sum(base.getchannel("A").getdata()) < 40 * base.width * base.height:
            base.putalpha(255)    # en estas texturas el alfa elige la sombra, no la transparencia: se ve opaca
        fondo.alpha_composite(base)
        foto = ImageTk.PhotoImage(fondo)
        self.fotos.append(foto)
        tk.Label(marco, image=foto, bg=g.C["frame"], cursor="hand2").pack(padx=3, pady=(3, 0))
        marco.winfo_children()[-1].bind("<Button-1>", lambda _e: g.open_path(ruta))
        espera = self.tamanos.get(nombre)
        mal = espera and tuple(espera) != (w, h)
        texto = "%s\n%dx%d" % (os.path.splitext(nombre)[0][:16], w, h)
        if mal:
            texto += g.T("\ndebe ser %dx%d", "\nmust be %dx%d") % tuple(espera)
            marco.configure(highlightbackground=g.C["err"], highlightthickness=2)
        g.label(marco, texto, fg=g.C["err"] if mal else g.C["dim"], font=g.F["small"], bg=g.C["frame"]).pack(pady=(0, 3))


def damero(size, paso=8):
    """Fondo de cuadros para distinguir la transparencia, como en cualquier editor de imagen."""
    from PIL import Image
    import numpy as np
    w, h = size
    y, x = np.indices((h, w))
    v = np.where(((x // paso) + (y // paso)) % 2, 96, 64).astype(np.uint8)
    a = np.dstack((v, v, v, np.full_like(v, 255)))
    return Image.fromarray(a, "RGBA")


def tamanos_de_meta(carpeta):
    """{png: (w, h)} de un textures_meta.json (texture_b3 extract). Vacio si no hay."""
    try:
        with open(os.path.join(carpeta, "textures_meta.json"), encoding="utf-8") as f:
            meta = json.load(f)
    except (OSError, ValueError):
        return {}
    return {"tex%02d_%dx%d.png" % (t["idx"], t["w"], t["h"]): (t["w"], t["h"]) for t in meta.get("texs", [])}


# ---------------------------------------------------------------- modelo
def png_modelo(us, entrada):
    """Ruta en cache del cuerpo renderizado de la entrada `entrada` de `us/data_cmn.afs`."""
    clave = hashlib.sha1(("%s|%d" % (os.path.abspath(us), entrada)).encode()).hexdigest()[:12]
    return os.path.join(cache_dir(), "cuerpo_%d_%s.png" % (entrada, clave))


def cmd_modelo(python, us, entrada, salida):
    return [python, os.path.abspath(__file__), "modelo", us, str(entrada), salida]


def _render_modelo(us, entrada, salida):
    """Render toon del juego (model_render.py, calibrado contra los iconos oficiales) del bin HD del jugador."""
    sys.path.insert(0, HERE)
    sys.path.insert(0, os.path.join(HERE, "..", "awo_tools"))
    sys.path.insert(0, os.path.join(HERE, "awo_tools"))
    import numpy as np
    from PIL import Image
    import afs_pair
    import model_render as mr
    bin_tmp = salida + ".bin"
    with open(bin_tmp, "wb") as f:
        f.write(afs_pair.hd(entrada, region=us))
    try:
        m = mr.Model(bin_tmp)
        pose = m.arms_down()
        P, _ = m.posed(pose)
        lo, hi = P.min(0), P.max(0)
        tam = 256
        sc = tam * 0.92 / max(hi[1] - lo[1], hi[0] - lo[0], 1e-6)
        img = mr.render(m, (tam, tam), mr.rot_y(20), (lo + hi) / 2, sc, pose=pose)
        Image.fromarray(np.asarray(img)).save(salida)
    finally:
        os.replace(bin_tmp, salida + ".bin.viejo")
        os.remove(salida + ".bin.viejo")


def foto_fija(g, parent, ruta, lado=200):
    """Label con una imagen ajustada; guarda la referencia. None si no se puede."""
    from PIL import Image, ImageTk
    try:
        with Image.open(ruta) as im:
            base = im.convert("RGBA")
    except OSError:
        return None
    base.thumbnail((lado, lado))
    foto = ImageTk.PhotoImage(base)
    w = tk.Label(parent, image=foto, bg=parent.cget("bg"))
    w.foto = foto
    return w


# ---------------------------------------------------------------- imagen propia al formato del juego
FORMATOS = [("saga", "Panel de una saga (660x420)", "Saga panel (660x420)"),
            ("escenario", "Escenario: círculo y vista del selector", "Stage: selector circle and view")]


def imagenes_formato(ruta, formato):
    """[(RGBA PIL, pie es, pie en)] con la imagen ya recortada como la deja el Kit (mismas funciones que al montar)."""
    from PIL import Image
    sys.path.insert(0, HERE)
    with Image.open(ruta) as im:
        im = im.convert("RGBA")
    if formato == "saga":
        import png2pic
        return [(Image.fromarray(png2pic.fit(im, (660, 420))), "Así se recorta", "How it is cropped")]
    import escenarios_convertir
    circulo, vista = escenarios_convertir.vistas(im)[:2]
    return [(circulo, "Círculo", "Circle"), (vista, "Vista ancha", "Wide view")]


class VistaImagen(tk.Frame):
    """Tu imagen tal como se vera: panel de saga o escenario del selector."""

    def __init__(self, master, g):
        super().__init__(master, bg=master.cget("bg"))
        self.g, self.ruta, self.tipo, self.fotos = g, tk.StringVar(), tk.StringVar(), []
        fila = tk.Frame(self, bg=master.cget("bg"))
        fila.pack(fill="x")
        fila.columnconfigure(1, weight=1)
        g.label(fila, g.T("Imagen", "Image"), bg=master.cget("bg")).grid(row=0, column=0, sticky="w", padx=(0, 8))
        g.PathEntry(fila, self.ruta, "file", [("Imagen / Image", "*.png *.webp *.jpg *.jpeg")], bg=master.cget("bg")).grid(row=0, column=1, sticky="ew")
        g.label(fila, g.T("Se usará como", "It will be used as"), bg=master.cget("bg")).grid(row=1, column=0, sticky="w", padx=(0, 8))
        self.nombres = [g.T(es, en) for _k, es, en in FORMATOS]
        self.tipo.set(self.nombres[0])
        cb = ttk.Combobox(fila, textvariable=self.tipo, values=self.nombres, state="readonly", width=40)
        cb.grid(row=1, column=1, sticky="w", pady=4)
        cb.bind("<<ComboboxSelected>>", lambda _e: self.pintar())
        self.ruta.trace_add("write", lambda *_: self.pintar())
        self.caja = tk.Frame(self, bg=master.cget("bg"))
        self.caja.pack(fill="x", pady=(6, 0))

    def pintar(self):
        from PIL import ImageTk
        g = self.g
        for w in self.caja.winfo_children():
            w.destroy()
        self.fotos = []
        ruta = self.ruta.get().strip()
        if not os.path.isfile(ruta):
            return
        clave = FORMATOS[self.nombres.index(self.tipo.get())][0]
        try:
            salen = imagenes_formato(ruta, clave)
        except (OSError, ValueError) as e:
            g.label(self.caja, g.T("No se puede leer esa imagen: %s", "That image cannot be read: %s") % e, fg=g.C["err"], bg=self.cget("bg")).pack(anchor="w")
            return
        for im, es, en in salen:
            col = tk.Frame(self.caja, bg=self.cget("bg"))
            col.pack(side="left", padx=(0, 12), anchor="n")
            pre = im.copy()
            pre.thumbnail((330, 240))
            fondo = damero(pre.size)
            fondo.alpha_composite(pre.convert("RGBA"))
            foto = ImageTk.PhotoImage(fondo)
            self.fotos.append(foto)
            tk.Label(col, image=foto, bg=self.cget("bg")).pack()
            g.label(col, "%s  %dx%d" % (g.T(es, en), im.width, im.height), fg=g.C["dim"], font=g.F["small"], bg=self.cget("bg")).pack()


# ---------------------------------------------------------------- capsula
COLOR_TIPO = {"especial": "#3f8fe0", "definitiva": "#e0493f", "transformacion": "#e0a93f"}


class FichaCapsula(tk.Frame):
    """Ficha visual de una capsula: dibujo orientativo con el color de su tipo y sus datos."""

    def __init__(self, master, g):
        super().__init__(master, bg=g.C["frame"], highlightthickness=1, highlightbackground=g.C["line"])
        self.g = g
        self.dibujo = tk.Canvas(self, width=92, height=46, bg=g.C["frame"], highlightthickness=0)
        self.dibujo.pack(side="left", padx=10, pady=8)
        self.txt = tk.Frame(self, bg=g.C["frame"])
        self.txt.pack(side="left", fill="x", expand=True, pady=6)
        self.mostrar(None, "")

    def mostrar(self, cap, tipo_txt):
        g, c = self.g, self.dibujo
        c.delete("all")
        for w in self.txt.winfo_children():
            w.destroy()
        if not cap:
            g.label(self.txt, g.T("Elige una cápsula para ver su ficha.", "Pick a capsule to see its card."), fg=g.C["dim"], bg=g.C["frame"]).pack(anchor="w")
            return
        color = COLOR_TIPO.get(cap.get("tipo", "especial"), COLOR_TIPO["especial"])
        c.create_arc(4, 6, 40, 42, start=90, extent=180, fill=color, outline=color)
        c.create_rectangle(22, 6, 46, 42, fill=color, outline=color)
        c.create_rectangle(46, 6, 70, 42, fill="#e8e8e8", outline="#e8e8e8")
        c.create_arc(52, 6, 88, 42, start=270, extent=180, fill="#e8e8e8", outline="#e8e8e8")
        c.create_oval(39, 17, 53, 31, fill="#ffffff", outline="#555555")
        g.label(self.txt, cap.get("nombre", "?"), font=g.F["bold"], bg=g.C["frame"]).pack(anchor="w")
        datos = [tipo_txt]
        if cap.get("coste") is not None:
            datos.append(g.T("coste %s", "cost %s") % cap["coste"])
        g.label(self.txt, "  ·  ".join(datos), fg=g.C["dim"], bg=g.C["frame"]).pack(anchor="w")
        if cap.get("descripcion"):
            g.label(self.txt, cap["descripcion"], fg=g.C["dim"], bg=g.C["frame"], wrap=520).pack(anchor="w")
        g.label(self.txt, g.T("Dibujo orientativo: en el juego usa el icono de cápsula de su tipo.",
                              "Indicative drawing: in game it uses the capsule icon of its type."), fg=g.C["dim"], font=g.F["small"], bg=g.C["frame"]).pack(anchor="w")


# ---------------------------------------------------------------- aura
def _silueta(tam):
    """Figura generica en L (0..255): cabeza, tronco, brazos y piernas."""
    from PIL import Image, ImageDraw
    w, h = tam
    m = Image.new("L", tam, 0)
    d = ImageDraw.Draw(m)
    cx = w // 2
    d.ellipse((cx - h * .07, h * .14, cx + h * .07, h * .30), fill=255)
    d.polygon([(cx - h * .13, h * .32), (cx + h * .13, h * .32), (cx + h * .08, h * .60), (cx - h * .08, h * .60)], fill=255)
    for s in (-1, 1):
        d.line([(cx + s * h * .13, h * .34), (cx + s * h * .20, h * .55)], fill=255, width=int(h * .055))
        d.line([(cx + s * h * .05, h * .60), (cx + s * h * .09, h * .86)], fill=255, width=int(h * .07))
    return m


def foto_aura(aura_hex, ki_hex=None, tam=(150, 190)):
    """PIL RGB: silueta oscura con un halo del color del aura y una bola del color del ki. Orientativa."""
    from PIL import Image, ImageChops, ImageDraw, ImageFilter
    w, h = tam
    forma = _silueta(tam)
    color = tuple(int(aura_hex[i:i + 2], 16) for i in (1, 3, 5))
    salida = Image.new("RGB", tam, (20, 22, 32))
    ancho = forma.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(9))
    salida.paste(Image.new("RGB", tam, color), (0, 0), ImageChops.multiply(ancho, Image.new("L", tam, 225)))
    salida.paste(Image.new("RGB", tam, (28, 28, 38)), (0, 0), forma)
    if ki_hex:
        d = ImageDraw.Draw(salida)
        d.ellipse((w - 34, 10, w - 10, 34), fill=ki_hex, outline="#ffffff")
    return salida


class MuestraAura(tk.Frame):
    def __init__(self, master, g):
        super().__init__(master, bg=master.cget("bg"))
        self.g, self.foto = g, None
        self.img = tk.Label(self, bg=master.cget("bg"))
        self.img.pack(anchor="w")
        g.label(self, g.T("Orientativa: en el juego el aura se mueve y brilla más.", "Indicative: in game the aura moves and glows more."),
                fg=g.C["dim"], font=g.F["small"], bg=master.cget("bg"), wrap=320).pack(anchor="w")

    def pintar(self, aura_hex, ki_hex=None):
        from PIL import ImageTk
        # sin color elegido (como su personaje base): halo neutro
        def valido(h):
            return h if isinstance(h, str) and len(h) == 7 and h.startswith("#") else None
        self.foto = ImageTk.PhotoImage(foto_aura(valido(aura_hex) or "#8a8fa0", valido(ki_hex)))
        self.img.configure(image=self.foto)


# ---------------------------------------------------------------- audio
class Oyente(tk.Frame):
    """Escuchar un audio antes de usarlo y ver su forma de onda y duracion."""
    SEGUNDOS = 45

    def __init__(self, master, g, ruta_fn, ffmpeg_fn):
        super().__init__(master, bg=master.cget("bg"))
        self.g, self.ruta_fn, self.ffmpeg_fn = g, ruta_fn, ffmpeg_fn
        self.b = ttk.Button(self, text="▶  " + g.T("Escuchar", "Listen"), style="Small.TButton", command=self.tocar)
        self.b.pack(side="left")
        self.b._con_tip = True
        g.Tooltip(self.b, g.T("Escucha los primeros %d segundos. Vuelve a pulsar para parar.",
                              "Plays the first %d seconds. Press again to stop.") % self.SEGUNDOS)
        self.onda = tk.Canvas(self, width=360, height=44, bg=g.C["frame"], highlightthickness=1,
                              highlightbackground=g.C["line"])
        self.onda.pack(side="left", padx=8)
        self.info = g.label(self, "", fg=g.C["dim"], bg=master.cget("bg"))
        self.info.pack(side="left")
        self.sonando = False

    def tocar(self):
        if self.sonando:
            return self.parar()
        ruta = self.ruta_fn()
        if not ruta or not os.path.isfile(ruta):
            self.info.configure(text=self.g.T("Elige primero el archivo.", "Pick the file first."))
            return
        self.info.configure(text=self.g.T("Preparando…", "Preparing…"))
        threading.Thread(target=self._preparar, args=(ruta,), daemon=True).start()

    def _preparar(self, ruta):
        try:
            wav = a_wav(ruta, self.ffmpeg_fn(), self.SEGUNDOS)
            picos, seg = picos_wav(wav)
        except (RuntimeError, OSError, ValueError) as e:
            self.after(0, lambda m=str(e): self.info.configure(text=m))
            return
        self.after(0, lambda: self._sonar(wav, picos, seg))

    def _sonar(self, wav, picos, seg):
        if not self.winfo_exists():
            return
        self.pintar(picos)
        self.info.configure(text="%d:%02d" % (seg // 60, seg % 60))
        try:
            import winsound
            winsound.PlaySound(wav, winsound.SND_FILENAME | winsound.SND_ASYNC)
        except (ImportError, RuntimeError):
            return     # sin audio en este sistema: queda la forma de onda
        self.sonando = True
        self.b.configure(text="■  " + self.g.T("Parar", "Stop"))
        self.after(int(min(seg, self.SEGUNDOS) * 1000) + 200, self.parar)

    def parar(self):
        self.sonando = False
        try:
            import winsound
            winsound.PlaySound(None, 0)
        except (ImportError, RuntimeError):
            pass
        try:
            self.b.configure(text="▶  " + self.g.T("Escuchar", "Listen"))
        except tk.TclError:
            pass

    def pintar(self, picos):
        self.onda.delete("all")
        h = 44
        for i, p in enumerate(picos):
            a = max(1, int(p * (h / 2 - 2)))
            self.onda.create_line(i, h / 2 - a, i, h / 2 + a, fill=self.g.C["accent"])


def a_wav(ruta, exe, segundos):
    """WAV PCM de los primeros `segundos` en la cache. Lanza RuntimeError con una frase llana."""
    if not exe:
        raise RuntimeError("Falta ffmpeg para escuchar este archivo.")
    st = os.stat(ruta)
    clave = hashlib.sha1(("%s|%d|%d" % (ruta, st.st_size, st.st_mtime_ns)).encode()).hexdigest()[:12]
    wav = os.path.join(cache_dir(), "oir_%s.wav" % clave)
    if not os.path.exists(wav):
        r = subprocess.run([exe, "-v", "error", "-y", "-i", ruta, "-vn", "-ac", "2", "-ar", "44100", "-t", str(segundos),
                            wav + ".wav"], capture_output=True, text=True, errors="replace",
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        if r.returncode:
            raise RuntimeError("No se puede leer ese audio: %s" % r.stderr.strip()[-120:])
        os.replace(wav + ".wav", wav)
    return wav


def picos_wav(wav, n=360):
    """(picos 0..1 por columna, segundos)."""
    import wave
    import numpy as np
    with wave.open(wav) as w:
        hz, can = w.getframerate(), w.getnchannels()
        datos = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    if not datos.size:
        raise ValueError("Audio vacío.")
    mono = np.abs(datos.reshape(-1, can)).max(1)
    trozos = np.array_split(mono, n)
    return [float(t.max()) / 32768 if t.size else 0.0 for t in trozos], len(mono) / hz


if __name__ == "__main__":
    if sys.argv[1:2] == ["modelo"]:
        _render_modelo(sys.argv[2], int(sys.argv[3]), sys.argv[4])
