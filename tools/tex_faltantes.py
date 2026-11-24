"""Texturas del juego que NO cubre el pack HD de Prince Vegeta (V2 = mismos hashes que el zip de originales).

Fuentes que cruza:
  - cubiertas: nombres del zip de originales (release_packs/DBZ3HD-Textures-Originals.zip), 12345 hashes;
  - idiomas: zip -Languages (textos de menu es/fr/de/it/en_gb, no estan en el pack);
  - contenido anadido por mods (escenarios B1/B2/IW, ports, UI nueva): DDS dentro de los ficheros de mods/;
  - opcional --update: los .dds que el juego volco en <pack>/update/ (faltantes vistos en partida).
Salida: CSV (hash, ancho, alto, formato, familia, origen) y resumen por familia.

  python tools/tex_faltantes.py --mods D:/DBZ3HD/sol/mods --csv E:/DBZ3HD_trabajo/tex/faltantes.csv
"""
import argparse, collections, csv, os, re, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "awo_tools"))
sys.path.insert(0, os.path.join(ROOT, "mod center hd"))
NAME = re.compile(r"([0-9A-F]{16})_(\d+)x(\d+)_(\w+)\.(?:png|dds)$", re.I)


def zip_hashes(path):
    out = {}
    for n in zipfile.ZipFile(path).namelist():
        m = NAME.search(n)
        if m:
            out[m.group(1).upper()] = (int(m.group(2)), int(m.group(3)), m.group(4), n)
    return out


def mod_hashes(mods, png_dir=None):
    import xxhash
    import afs_pair
    from texturas_originales import dds_textures, to_png
    out = {}
    for dp, _, fs in os.walk(mods):
        for f in fs:
            p = os.path.join(dp, f)
            try:
                raw = open(p, "rb").read()
                b = afs_pair.decompress(raw, "texfalt_" + re.sub(r"\W", "_", os.path.relpath(p, mods)))
            except Exception:  # noqa: BLE001 - ficheros raros: se saltan
                continue
            fam = "mods/" + os.path.relpath(p, mods).replace("\\", "/").split("/")[0]
            for off, w, h, fmt, base in dds_textures(b):
                hx = "%016X" % xxhash.xxh3_64_intdigest(base)
                if hx in out:
                    continue
                out[hx] = (w, h, fmt, fam)
                if png_dir:   # PNG del original (para buscarle gemelo nativo); las cubiertas sobran, pero pesan poco
                    dst = os.path.join(png_dir, "%s_%dx%d_%s.png" % (hx, w, h, fmt))
                    if not os.path.exists(dst):
                        try:
                            open(dst, "wb").write(to_png(b, off))
                        except Exception:  # noqa: BLE001 - DDS que PIL no abre: queda sin PNG
                            pass
    return out


def afs_hashes(path, tag, png_dir=None):
    """Texturas de un .afs del juego (p. ej. eu/data_cmn.afs, us/data_jpn.afs) que el zip de originales no escaneo."""
    import xxhash
    import afs_pair
    from texturas_originales import Labels, dds_textures, to_png
    labels, out, afs = Labels(), {}, os.path.basename(path).lower()
    for n in range(len(afs_pair.table(path))):
        try:
            b = afs_pair.decompress(afs_pair.entry(path, n), "texfalt_%s_%s_%d" % (tag, afs, n))
        except Exception:  # noqa: BLE001 - video/audio
            continue
        for off, w, h, fmt, base in dds_textures(b):
            hx = "%016X" % xxhash.xxh3_64_intdigest(base)
            if hx in out:
                continue
            out[hx] = (w, h, fmt, "%s/%s" % (tag, labels.folder(afs, n)))
            if png_dir:
                dst = os.path.join(png_dir, "%s_%dx%d_%s.png" % (hx, w, h, fmt))
                if not os.path.exists(dst):
                    try:
                        open(dst, "wb").write(to_png(b, off))
                    except Exception:  # noqa: BLE001
                        pass
    return out


# Mods que no son contenido del juego: pruebas, XV2 (nunca se reparte) y respaldos.
FUERA = ("xv2", "xvcara", "prueba", "_respaldo", "heroe_propio")
LEEME = """DBZ BUDOKAI 3 HD COLLECTION - HD PACK UPDATE (ORIGINAL TEXTURES MISSING FROM THE HD PACK)
=========================================================================================

Only the textures the current HD pack does NOT have yet ({n} PNG, original size). Nothing that is
already upscaled is here. Menu text in other languages is in the separate "Languages" zip.

Folders = where the texture is used: B1/B2/IW stages, Hero mode and story scenes, loading screens.
Same rules as the main pack: keep each file name (<hash>_<width>x<height>_<format>.png), upscale
x2-x4 with the same aspect ratio, 32-bit RGBA PNG. index.csv lists the folder of every image.
Install: drop the files into the HD pack (any subfolder) or zip this folder and drag it onto the launcher.
"""


def zip_update(path, missing, png_dir, update_dirs):
    from PIL import Image
    rows = []
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for h, (w, hh, fmt, fam, src) in sorted(missing.items(), key=lambda x: (x[1][3], x[0])):
            if src == "idiomas" or any(f in fam.lower() for f in FUERA):
                continue
            name = "%s_%dx%d_%s" % (h, w, hh, fmt)
            if src == "partida":   # volcado por el juego (.dds); 16:9 = imagen de pantalla, no textura
                if w * 9 == hh * 16:
                    continue
                dds = next((os.path.join(u, name + ".dds") for u in update_dirs
                            if os.path.exists(os.path.join(u, name + ".dds"))), None)
                if dds is None:
                    continue
                import io
                b = io.BytesIO()
                Image.open(dds).convert("RGBA").save(b, "PNG")
                data, folder = b.getvalue(), "seen_in_game"
            else:
                pf = os.path.join(png_dir or "", name + ".png")
                if not os.path.exists(pf):
                    continue
                data, folder = open(pf, "rb").read(), fam.split("/", 1)[1].replace(" ", "_")
            rel = "%s/%s.png" % (folder, name)
            z.writestr("HD Textures - Update/" + rel, data)
            rows.append(rel)
        z.writestr("HD Textures - Update/README.txt", LEEME.format(n=len(rows)))
        z.writestr("HD Textures - Update/manifest.txt",
                   "name=HD Textures - Update\nauthor=\nversion=1.0\ndescription=Textures missing from the HD pack\n")
        z.writestr("HD Textures - Update/index.csv", "png\n" + "\n".join(rows) + "\n")
    return len(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    rp = os.path.join(ROOT, "release_packs")
    ap.add_argument("--originales", default=os.path.join(rp, "DBZ3HD-Textures-Originals.zip"))
    ap.add_argument("--idiomas", default=os.path.join(rp, "DBZ3HD-Textures-Originals-Languages.zip"))
    ap.add_argument("--mods", action="append", default=[])
    ap.add_argument("--afs", action="append", default=[], metavar="ETIQUETA=RUTA",
                    help="afs extra del juego, p. ej. eu=eu/data_cmn.afs o ja=us/data_jpn.afs")
    ap.add_argument("--update", action="append", default=[], help="carpeta update/ volcada por el juego")
    ap.add_argument("--csv")
    ap.add_argument("--png-mods", help="carpeta donde dejar el PNG original de cada textura de mods")
    ap.add_argument("--zip-update", help="zip para el autor del pack: SOLO las que faltan y no son de idioma")
    a = ap.parse_args()

    covered = zip_hashes(a.originales)
    if a.png_mods:
        os.makedirs(a.png_mods, exist_ok=True)
    missing = {}
    for h, (w, hh, fmt, n) in zip_hashes(a.idiomas).items():
        missing.setdefault(h, (w, hh, fmt, "/".join(n.split("/")[1:3]), "idiomas"))
    for spec in a.afs:
        tag, path = spec.split("=", 1)
        for h, (w, hh, fmt, fam) in afs_hashes(path, tag, a.png_mods).items():
            missing.setdefault(h, (w, hh, fmt, fam, "b3hd"))
    for m in a.mods:
        for h, (w, hh, fmt, fam) in mod_hashes(m, a.png_mods).items():
            missing.setdefault(h, (w, hh, fmt, fam, "mods"))
    for u in a.update:
        for f in os.listdir(u):
            mm = NAME.search(f)
            if mm:
                missing.setdefault(mm.group(1).upper(), (int(mm.group(2)), int(mm.group(3)), mm.group(4),
                                                         "update", "partida"))
    missing = {h: v for h, v in missing.items() if h not in covered}
    print("cubiertas por el pack PV: %d; faltan: %d" % (len(covered), len(missing)))
    fams = collections.Counter((v[4], v[3]) for v in missing.values())
    for (src, fam), n in sorted(fams.items(), key=lambda x: -x[1]):
        print("  %-8s %-48s %6d" % (src, fam, n))
    if a.zip_update:
        n = zip_update(a.zip_update, missing, a.png_mods, a.update)
        print("zip update: %d texturas -> %s" % (n, a.zip_update))
    if a.csv:
        os.makedirs(os.path.dirname(os.path.abspath(a.csv)), exist_ok=True)
        with open(a.csv, "w", newline="", encoding="utf-8") as f:
            wr = csv.writer(f)
            wr.writerow(["hash", "ancho", "alto", "formato", "familia", "origen"])
            for h, (w, hh, fmt, fam, src) in sorted(missing.items(), key=lambda x: (x[1][4], x[1][3], x[0])):
                wr.writerow([h, w, hh, fmt, fam, src])
    return 0


if __name__ == "__main__":
    sys.exit(main())
