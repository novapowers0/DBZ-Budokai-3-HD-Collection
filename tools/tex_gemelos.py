"""Gemelos nativos: para cada textura que falta en el pack PV, busca en el PROPIO B3 HD la misma imagen
a mayor tamano (x2/x4/x8/x16, mismas proporciones). Si al reducirla queda igual que la pequena, la grande
es su version HD nativa: se escribe <hash_pequena>_<W>x<H>_<fmt>.png con los pixeles de la grande.
Tambien cuenta "alias": misma imagen al mismo tamano con otro hash ya cubierto por el pack PV
(no se copian: serian arte de PV).

  python tools/tex_gemelos.py E:/DBZ3HD_trabajo/tex/faltantes.csv --salida D:/tmp/tex_gemelos [--limite 2]
Solo usa los zips de originales nativos (release_packs). Prueba interna: --prueba.
"""
import argparse, csv, io, os, re, sys, zipfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
RP = os.path.join(os.path.dirname(HERE), "release_packs")
NAME = re.compile(r"([0-9A-F]{16})_(\d+)x(\d+)_(\w+)\.png$", re.I)
SIG = 8


def sig(im):
    return np.asarray(im.convert("RGBA").resize((SIG, SIG), Image.BOX), np.float32).ravel()


def small(im, w, h):
    return np.asarray(im.convert("RGBA").resize((w, h), Image.BOX), np.float32)


def opaque(a):
    # alfa todo a 0 = textura que no usa transparencia (mods de PS2): su color es lo que se ve
    if a[..., 3].max() < 8:
        a = a.copy()
        a[..., 3] = 255
    return a


def rmse(a, b):
    # el canal alfa cuenta; el color donde alfa=0 no se ve: se pondera por alfa
    a, b = opaque(a), opaque(b)
    al = np.maximum(a[..., 3:], b[..., 3:]) / 255.0
    d = ((a[..., :3] - b[..., :3]) * al) ** 2
    da = (a[..., 3] - b[..., 3]) ** 2
    return float(np.sqrt((d.sum() / 3 + da.sum()) / (a.shape[0] * a.shape[1] * 2)))


def index(zips):
    """{hash: (zip, nombre, w, h, fmt)} de todos los PNG nativos."""
    out = {}
    for z in zips:
        for n in zipfile.ZipFile(z).namelist():
            m = NAME.search(n)
            if m:
                out[m.group(1).upper()] = (z, n, int(m.group(2)), int(m.group(3)), m.group(4))
    return out


_zf = {}


def load(z, n):
    if z is None:   # PNG suelto (texturas de mods sacadas por tex_faltantes --png-mods)
        return Image.open(n)
    if z not in _zf:
        _zf[z] = zipfile.ZipFile(z)
    return Image.open(io.BytesIO(_zf[z].read(n)))


def find(missing, natives, limit, out_dir, covered, extra=None, cache=None):
    # firmas 8x8 de todos los nativos, una vez (y guardadas en `cache` para la siguiente corrida)
    sigs = {}
    if cache and os.path.exists(cache):
        d = np.load(cache)
        sigs = dict(zip(d["h"].tolist(), d["s"]))
    falta = [h for h in natives if h not in sigs]
    for i, h in enumerate(falta):
        try:
            sigs[h] = sig(load(*natives[h][:2]))
        except Exception:  # noqa: BLE001 - PNG roto: sin firma
            sigs[h] = np.full(SIG * SIG * 4, -1e6, np.float32)
        if i % 2000 == 0:
            print("  firmas %d/%d" % (i, len(falta)), flush=True)
    if cache and falta:
        np.savez(cache, h=np.array(list(sigs)), s=np.stack(list(sigs.values())))
    by_size = {}
    for h, (z, n, w, hh, f) in natives.items():
        by_size.setdefault((w, hh), []).append(h)
    arr = {k: (v, np.stack([sigs[h] for h in v])) for k, v in by_size.items()}

    def best_of(size, ms, thr, skip, only=None):
        if size not in arr:
            return []
        hs, S = arr[size]
        d = np.abs(S - ms).mean(1)
        out = []
        for i in np.argsort(d):
            if d[i] > thr or len(out) == 3:
                break
            if hs[i] != skip and (only is None or hs[i] in only):
                out.append(hs[i])
        return out

    rows = []
    for j, (h, (w, hh, fmt)) in enumerate(missing.items()):
        if j % 2000 == 0:
            print("  faltantes %d/%d, encontrados %d" % (j, len(missing), len(rows)), flush=True)
        src = natives.get(h) or (extra or {}).get(h)
        if src is None:
            continue   # sin PNG del original
        try:
            im = load(*src[:2]).convert("RGBA")
        except Exception:  # noqa: BLE001
            continue
        a = np.asarray(im, np.float32)
        if a[..., :3].std() < 4 and a[..., 3].std() < 4:
            continue   # lisa: cualquier cosa "encaja"; no aporta HD
        ms = sigs.get(h)
        if ms is None:
            ms = sig(im)
        found = None
        for k in (16, 8, 4, 2):
            for c in best_of((w * k, hh * k), ms, 12, h):
                e = rmse(small(load(*natives[c][:2]), w, hh), a)
                if e <= limit:
                    found = (c, k, e)
                    break
            if found:
                break
        if found:
            c, k, e = found
            if out_dir:
                load(*natives[c][:2]).convert("RGBA").save(os.path.join(out_dir, "%s_%dx%d_%s.png" % (h, w, hh, fmt)))
            rows.append([h, w, hh, fmt, "gemelo_x%d" % k, c, "%.2f" % e])
            continue
        for c in best_of((w, hh), ms, 6, h, covered):   # alias: misma imagen, otro hash cubierto por PV
            if True:
                e = rmse(np.asarray(load(*natives[c][:2]).convert("RGBA"), np.float32), a)
                if e <= limit:
                    rows.append([h, w, hh, fmt, "alias_pv", c, "%.2f" % e])
                    break
    return rows


def prueba():
    import tempfile
    rng = np.random.default_rng(1)
    big = Image.fromarray(rng.integers(0, 255, (64, 64, 4), np.uint8).repeat(4, 0).repeat(4, 1))
    sm = big.resize((64, 64), Image.BOX)
    d = tempfile.mkdtemp()
    z = os.path.join(d, "t.zip")
    with zipfile.ZipFile(z, "w") as zf:
        for name, im in (("A" * 16 + "_256x256_RGBA8.png", big), ("B" * 16 + "_64x64_RGBA8.png", sm)):
            b = io.BytesIO()
            im.save(b, "PNG")
            zf.writestr(name, b.getvalue())
    nat = index([z])
    rows = find({"B" * 16: (64, 64, "RGBA8")}, nat, 2.0, d, set())
    assert rows and rows[0][4] == "gemelo_x4" and rows[0][5] == "A" * 16, rows
    assert Image.open(os.path.join(d, "B" * 16 + "_64x64_RGBA8.png")).size == (256, 256)
    z0 = np.zeros((8, 8, 4), np.float32)
    z1 = z0.copy()
    z1[..., 0] = 200
    assert rmse(z0, z1) > 50, "alfa 0: el color tiene que contar"
    print("prueba OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="?")
    ap.add_argument("--salida")
    ap.add_argument("--limite", type=float, default=2.0, help="RMSE maximo (0-255) para aceptar el gemelo")
    ap.add_argument("--informe", help="CSV con lo encontrado")
    ap.add_argument("--pngs", help="carpeta con PNG de texturas que faltan (las de mods)")
    ap.add_argument("--prueba", action="store_true")
    a = ap.parse_args()
    if a.prueba:
        return prueba()
    orig = os.path.join(RP, "DBZ3HD-Textures-Originals.zip")
    natives = index([orig, os.path.join(RP, "DBZ3HD-Textures-Originals-Languages.zip")])
    covered = set(index([orig]))
    missing = {}
    with open(a.csv, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            missing[r["hash"]] = (int(r["ancho"]), int(r["alto"]), r["formato"])
    if a.salida:
        os.makedirs(a.salida, exist_ok=True)
    extra = {}
    if a.pngs:
        for f in os.listdir(a.pngs):
            m = NAME.search(f)
            if m:
                extra[m.group(1).upper()] = (None, os.path.join(a.pngs, f))
    rows = find(missing, natives, a.limite, a.salida, covered, extra, "E:/DBZ3HD_trabajo/tex/firmas.npz")
    kinds = {}
    for r in rows:
        kinds[r[4]] = kinds.get(r[4], 0) + 1
    print("faltan %d; encontrados: %s" % (len(missing), kinds))
    if a.informe:
        with open(a.informe, "w", newline="", encoding="utf-8") as f:
            csv.writer(f).writerows([["hash", "ancho", "alto", "formato", "tipo", "fuente", "rmse"]] + rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
