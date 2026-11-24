"""Revisa un pack HD contra los originales: escala de cada textura y si su transparencia (alfa) coincide.

Lee el zip aunque venga partido (.zip.001, .002...) sin unirlo. Para cada PNG del pack:
  escala = ancho_pack / ancho_original (x1 = sin ampliar), modo PNG (RGB = sin canal alfa),
  acuerdo = fraccion de pixeles donde (alfa<128) coincide con el original (reducido al tamano original).
Salida CSV ordenada de peor a mejor acuerdo.

  python tools/tex_revisar_pack.py E:/DBZ3HD_trabajo/pack_v2/texturas_hd_pv_v2.zip.001 --csv E:/DBZ3HD_trabajo/tex/revision_v2.csv
"""
import argparse, csv, glob, io, os, re, sys, zipfile
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None
HERE = os.path.dirname(os.path.abspath(__file__))
ORIG = os.path.join(os.path.dirname(HERE), "release_packs", "DBZ3HD-Textures-Originals.zip")
NAME = re.compile(r"([0-9A-F]{16})_(\d+)x(\d+)_(\w+)\.png$", re.I)


class Partes(io.RawIOBase):
    """Un fichero de solo lectura hecho de varios trozos seguidos."""

    def __init__(self, paths):
        self.f = [open(p, "rb") for p in paths]
        self.sz = [os.path.getsize(p) for p in paths]
        self.total, self.pos = sum(self.sz), 0

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self.pos

    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.total + off
        return self.pos

    def readinto(self, b):
        n, base = 0, 0
        for f, s in zip(self.f, self.sz):
            if self.pos < base + s and n < len(b):
                f.seek(self.pos - base)
                d = f.read(min(len(b) - n, base + s - self.pos))
                b[n:n + len(d)] = d
                n += len(d)
                self.pos += len(d)
            base += s
        return n


def abrir(path):
    if re.search(r"\.\d{3}$", path):
        parts = sorted(glob.glob(path[:-3] + "[0-9][0-9][0-9]"))
        return zipfile.ZipFile(io.BufferedReader(Partes(parts), 1 << 20))
    return zipfile.ZipFile(path)


def alfa_bin(im):
    a = np.asarray(im.convert("RGBA"))[..., 3]
    return a < 128


def revisar(args):
    pack, items = args
    zp, zo = abrir(pack), zipfile.ZipFile(ORIG)
    out = []
    for name, oname in items:
        try:
            hd = Image.open(io.BytesIO(zp.read(name)))
            hd.load()
            o = Image.open(io.BytesIO(zo.read(oname)))
            o.load()
        except Exception as e:  # noqa: BLE001
            out.append([name, "", "", "", "", "", f"error en pareja con {oname}: {e}"])
            continue
        escala = hd.width / o.width
        mo = alfa_bin(o)
        if mo.all() or not mo.any():
            acuerdo = ""   # original sin alfa util (todo 0 o todo 255): no se compara
        else:
            mh = alfa_bin(hd.convert("RGBA").resize(o.size, Image.BOX))
            acuerdo = "%.3f" % (mh == mo).mean()
        # color: el HD reducido al tamano original tiene que parecerse (tablas de sombreado, rampas...)
        a = np.asarray(o.convert("RGB"), np.float32)
        b = np.asarray(hd.convert("RGB").resize(o.size, Image.BOX), np.float32)
        color = "%.1f" % float(np.sqrt(((a - b) ** 2).mean()))
        out.append([name, "%g" % escala, hd.mode, acuerdo, "%.3f" % mo.mean(), color, ""])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pack")
    ap.add_argument("--csv", required=True)
    ap.add_argument("--hilos", type=int, default=max(1, min(6, (os.cpu_count() or 2) - 2)))
    ap.add_argument("--solo", help="regex sobre la ruta original (p. ej. 'broly|world_tour')")
    a = ap.parse_args()
    orig = {NAME.search(n).group(1).upper(): n for n in zipfile.ZipFile(ORIG).namelist() if NAME.search(n)}
    items = []
    for n in abrir(a.pack).namelist():
        m = NAME.search(n)
        if m and m.group(1).upper() in orig and (not a.solo or re.search(a.solo, orig[m.group(1).upper()], re.I)):
            items.append((n, orig[m.group(1).upper()]))
    print("texturas a revisar: %d" % len(items), flush=True)
    lotes = [(a.pack, items[i::a.hilos * 8]) for i in range(a.hilos * 8)]
    rows = []
    with ProcessPoolExecutor(a.hilos) as ex:
        for k, r in enumerate(ex.map(revisar, lotes)):
            rows += r
            print("  lote %d/%d" % (k + 1, len(lotes)), flush=True)
    rows.sort(key=lambda r: -float(r[5] or 0))
    with open(a.csv, "w", newline="", encoding="utf-8") as f:
        csv.writer(f).writerows([["png", "escala", "modo", "acuerdo_alfa", "frac_transparente_orig", "color_rmse", "error"]] + rows)
    malos = [r for r in rows if r[3] and float(r[3]) < 0.9]
    print("x1: %d | RGB sin alfa: %d | alfa distinto (<90%%): %d | color distinto (rmse>25): %d" % (
        sum(r[1] == "1" for r in rows), sum(r[2] == "RGB" for r in rows), len(malos),
        sum(r[5] and float(r[5]) > 25 for r in rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
