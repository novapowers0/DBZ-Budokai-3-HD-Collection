#!/usr/bin/env python3
"""Texturas originales del B3 HD listas para remasterizar (pack de texturas HD).

Saca de los AFS del juego TODAS las texturas que un pack puede sustituir (DXT1/3/5 y
RGBA8), las pasa a PNG en colores reales y las ordena por carpetas (personajes,
escenarios, menus...). Cada PNG se llama como lo busca el juego:

    <hash>_<ancho>x<alto>_<formato>.png      (hash = XXH3-64 del bitmap original)

El artista solo tiene que agrandar la imagen (x2, x3 o x4, sin cambiar el nombre ni la
proporcion) y devolver la carpeta: se instala tal cual en mods/ como pack de texturas.

Uso:
    python texturas_originales.py <carpeta_salida> [--juego <carpeta us/>] [--zip <fichero.zip>]
    python texturas_originales.py --prueba        (autocomprobacion)

Necesita el modulo xxhash (pip install xxhash) para calcular el mismo hash que el juego.
"""
import argparse
import io
import json
import os
import re
import struct
import sys
import zipfile
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "awo_tools"))

# Orden: lo comun primero; de los idiomas solo sale lo que no esta ya en ingles.
AFS = [("data_cmn.afs", ""), ("data_usi.afs", ""), ("data_yah.afs", ""), ("data_eng.afs", "languages/en_gb"),
       ("data_spn.afs", "languages/es"), ("data_fra.afs", "languages/fr"), ("data_ger.afs", "languages/de"),
       ("data_ita.afs", "languages/it")]
# Las carpetas van en ingles (el pack lo hacen artistas de fuera): palabra a palabra.
EN = {"personajes": "characters", "escenarios": "stages", "escenario": "stage", "efectos": "effects",
      "efecto": "effect", "capsulas": "capsules", "capsula": "capsule", "seleccion": "select_screen",
      "otros": "other", "retrato": "portrait", "retratos": "portraits", "labios": "lips",
      "camara": "camera", "nombres": "names", "nombre": "name", "habilidades": "skills",
      "habilidad": "skill", "cara": "face", "caras": "faces", "ficha": "sheet", "fichas": "sheets",
      "nino": "kid", "adulto": "adult", "futuro": "future", "modelo": "model", "entrada": "entry",
      "compartida": "shared", "gordo": "fat", "animacion": "animation", "textura": "texture",
      "texturas": "textures", "descripcion": "description", "cartas": "cards", "sin": "no",
      "catalogo": "catalog", "menu": "menu", "videos": "videos", "mesh": "mesh"}
PHRASES = (("ficha_habilidades", "skill_sheet"), ("fichas_habilidades", "skill_sheets"),
           ("nombres_capsulas", "capsule_names"), ("descripcion_capsulas", "capsule_descriptions"),
           ("cartas_habilidad", "skill_cards"), ("retratos_select", "select_portraits"),
           ("texturas_efectos_hud", "hud_effect_textures"), ("personajes_sin_nombre", "unnamed_characters"),
           ("efectos_personajes", "character_effects"), ("escenarios_mesh", "stage_meshes"),
           ("caras_hud", "hud_faces"), ("catalogo_capsulas", "capsule_catalog"))
LABELS = os.path.join(HERE, "texturas_etiquetas.json")


def dds_textures(b):
    """[(offset, ancho, alto, formato, bytes del nivel base)] de cada DDS de un bin.
    Solo los formatos que el juego deja sustituir: DXT1/3/5 y 32 bits (RGBA8)."""
    out = []
    i = b.find(b"DDS |")
    while i >= 0:
        if i + 128 <= len(b):
            h, w = struct.unpack_from("<II", b, i + 12)
            flags, four, bits = struct.unpack_from("<I4sI", b, i + 80)
            if four in (b"DXT1", b"DXT3", b"DXT5"):
                size = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * (8 if four == b"DXT1" else 16)
                fmt = four.decode()
            elif not flags & 4 and bits == 32:
                size, fmt = w * h * 4, "RGBA8"
            else:
                size = 0
            if size and 0 < w <= 4096 and 0 < h <= 4096 and i + 128 + size <= len(b):
                out.append((i, w, h, fmt, b[i + 128:i + 128 + size]))
        i = b.find(b"DDS |", i + 4)
    return out


def to_png(b, off):
    """PNG (bytes) en colores reales de la DDS en b[off]: solo el nivel base."""
    from PIL import Image
    hdr = bytearray(b[off:off + 128])
    struct.pack_into("<I", hdr, 8, struct.unpack_from("<I", hdr, 8)[0] & ~0x20000)   # sin DDSD_MIPMAPCOUNT
    struct.pack_into("<I", hdr, 28, 0)
    im = Image.open(io.BytesIO(bytes(hdr) + b[off + 128:])).convert("RGBA")
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=False, compress_level=6)
    return buf.getvalue()


def safe(name):
    name = re.sub(r"[^a-z0-9_\-]+", "_", str(name).lower()).strip("_")
    for a, b in PHRASES:
        name = name.replace(a, b)
    words = name.split("_")
    return "_".join(EN.get(w, w) for w in words) or "x"


class Labels:
    def __init__(self, path=LABELS):
        try:
            self.d = json.load(open(path, encoding="utf-8"))
        except OSError:
            self.d = {}

    def folder(self, afs, entry):
        stem = afs.replace(".afs", "")
        per = self.d.get("data_usi.afs" if afs != "data_cmn.afs" and afs != "data_yah.afs" else afs, {})
        e = per.get(str(entry))
        if e:
            return "%s/%s" % (safe(e["cat"]), safe(e["name"]))
        for r in self.d.get("_ranges", []):
            if r["afs"] == afs and r["from"] <= entry <= r["to"]:
                return "%s/%s/%04d" % (safe(r["cat"]), safe(r["name"]), entry)
        cat = "menus" if afs != "data_cmn.afs" else "other"
        return "%s/%s_%04d" % (cat, stem, entry)


def scan(game):
    """Fase 1: hash de cada textura -> primera aparicion. {(afs, entrada): [(off, nombre)]}."""
    import xxhash
    import afs_pair
    labels = Labels()
    seen, work, total = set(), {}, 0
    for afs, prefix in AFS:
        path = os.path.join(game, afs)
        if not os.path.isfile(path):
            continue
        for n in range(len(afs_pair.table(path))):
            try:
                b = afs_pair.decompress(afs_pair.entry(path, n), "%s_%d" % (afs, n))
            except Exception:  # noqa: BLE001 - entradas raras (video/audio): se saltan
                continue
            for off, w, h, fmt, base in dds_textures(b):
                hh = xxhash.xxh3_64_intdigest(base)
                total += 1
                if hh in seen:
                    continue
                seen.add(hh)
                folder = labels.folder(afs, n)
                if prefix:
                    folder = prefix + "/" + folder
                work.setdefault((path, afs, n), []).append((off, "%s/%016X_%dx%d_%s.png" % (folder, hh, w, h, fmt)))
        print("  %s: %d texturas unicas hasta ahora" % (afs, len(seen)), flush=True)
    return work, total


def _write(args):
    path, afs, n, items, out = args
    import afs_pair
    b = afs_pair.decompress(afs_pair.entry(path, n), "%s_%d" % (afs, n))
    rows = []
    for off, rel in items:
        dst = os.path.join(out, rel)
        if not os.path.exists(dst):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as f:
                f.write(to_png(b, off))
        rows.append("%s,%s,%d" % (rel, afs, n))
    return rows


README = """DBZ BUDOKAI 3 HD COLLECTION - ORIGINAL TEXTURES FOR AN HD TEXTURE PACK
=========================================================================

This folder has the game's ORIGINAL textures ({count} PNG images) that the game can replace,
sorted into folders: characters, stages, effects, menus, capsules, select_screen,
dragon_universe... {extra}

HOW TO MAKE THE HD PACK
1. Upscale the images you want: x2, x3 or x4 (x2 or x4 recommended).
   - Do NOT rename the files: the game finds each texture by its file name.
   - Keep the aspect ratio: 256x128 becomes 512x256 at x2, 1024x512 at x4.
   - Keep the transparency (alpha channel): save as 32-bit RGBA PNG.
   - Colors are the real in-game colors. Max x4, exact multiples only.
2. Images you do not upscale can be deleted (the game then uses its own); keeping
   them also works, it just makes the pack bigger.
3. Keep the folder layout and keep "manifest.txt" (it names the mod in the launcher;
   you can edit the name, author and description in it).

HOW TO DELIVER AND INSTALL IT
- Zip this whole folder ("{folder}") as it is and send the zip.
- To install: drag the .zip onto the launcher window (Mods tab), or extract it into the
  "mods" folder next to dbz3.exe so that it ends up as mods/{folder}/... Then start the
  game. To turn it off, disable the mod in the launcher.

NOTES
- File name = <hash>_<width>x<height>_<format>.png. Width and height are the ORIGINAL
  size; no need to change them when upscaling (the game reads the real image size).
- index.csv lists the game file and entry each image comes from.
"""

LEEME = """DBZ BUDOKAI 3 HD COLLECTION - TEXTURAS ORIGINALES PARA UN PACK DE TEXTURAS HD
==========================================================================

Esta carpeta tiene las texturas ORIGINALES del juego que se pueden sustituir ({count} PNG),
ordenadas en carpetas (characters = personajes, stages = escenarios, effects = efectos,
menus, capsules = capsulas, select_screen = seleccion...). {extra_es}

COMO HACER EL PACK HD
1. Agranda las que quieras: x2, x3 o x4 (recomendado x2 o x4).
   - NO cambies el nombre del archivo: el juego reconoce cada textura por su nombre.
   - Manten la proporcion (256x128 -> 512x256 a x2, 1024x512 a x4) y la transparencia
     (PNG RGBA de 32 bits). Colores reales del juego. Maximo x4, multiplos exactos.
2. Las que no agrandes se pueden borrar (el juego usa las suyas).
3. Manten las carpetas y "manifest.txt" (es el nombre del mod en el launcher).

COMO ENTREGARLO E INSTALARLO
- Comprime esta carpeta entera ("{folder}") en un zip tal cual.
- Para instalar: arrastra el .zip a la ventana del launcher (pestana Mods), o descomprimelo
  dentro de "mods" (junto a dbz3.exe) para que quede mods/{folder}/... y abre el juego.

NOTAS
- Nombre = <hash>_<ancho>x<alto>_<formato>.png, con el tamano ORIGINAL; no hace falta
  cambiarlo al agrandar. index.csv dice de que archivo y entrada sale cada imagen.
"""

# Carpeta (y mod) de cada zip: el principal y el de los textos de cada idioma.
PACK_MAIN = "HD Textures"
PACK_LANGS = "HD Textures - Languages"
EXTRA = ("Text that changes with the game language (mostly Dragon Universe text) is in the "
         "separate \"Languages\" zip.",
         "The \"languages\" folder holds the text that changes with the game language "
         "(es, fr, de, it, en_gb): only needed if you also remaster text.")
EXTRA_ES = ("Los textos que cambian segun el idioma van en el zip aparte \"Languages\".",
            "La carpeta \"languages\" tiene los textos de cada idioma (es, fr, de, it, en_gb).")


def write_docs(dst, folder, count, langs):
    """README (ingles), LEEME (espanol) y manifest.txt de una carpeta de pack."""
    k = 1 if langs else 0
    with open(os.path.join(dst, "README.txt"), "w", encoding="utf-8") as f:
        f.write(README.format(count=count, folder=folder, extra=EXTRA[k]))
    with open(os.path.join(dst, "LEEME.txt"), "w", encoding="utf-8") as f:
        f.write(LEEME.format(count=count, folder=folder, extra_es=EXTRA_ES[k]))
    with open(os.path.join(dst, "manifest.txt"), "w", encoding="utf-8") as f:
        f.write("name=%s\nauthor=\nversion=1.0\ndescription=%s\n" % (
            folder, "HD texture pack (language text)" if langs else "HD texture pack"))


def build(out, game, zip_path=None, workers=8):
    work, total = scan(game)
    jobs = [(path, afs, n, items, out) for (path, afs, n), items in work.items()]
    count = sum(len(i) for i in work.values())
    print("Texturas: %d (de %d apariciones). Escribiendo PNG..." % (count, total), flush=True)
    rows = []
    with ProcessPoolExecutor(workers) as ex:
        for k, r in enumerate(ex.map(_write, jobs, chunksize=4)):
            rows += r
            if k % 500 == 0:
                print("  %d/%d bins" % (k, len(jobs)), flush=True)
    with open(os.path.join(out, "index.csv"), "w", encoding="utf-8") as f:
        f.write("png,afs,entry\n" + "\n".join(sorted(rows)) + "\n")
    if zip_path:
        make_zips(out, zip_path)
    print("Listo: %d PNG en %s" % (count, out))


def make_zips(out, zip_path):
    """Dos zips listos para mods/: <zip> con la carpeta "HD Textures/" (todo menos
    languages/) y <zip>-Languages.zip con "HD Textures - Languages/" (textos de cada idioma).
    Cada uno lleva su README/LEEME/manifest; arrastrados al launcher se instalan solos."""
    import tempfile
    langs_zip = os.path.splitext(zip_path)[0] + "-Languages.zip"
    groups = {False: [], True: []}
    for d, _, files in os.walk(out):
        for fn in files:
            rel = os.path.relpath(os.path.join(d, fn), out).replace("\\", "/")
            if fn.lower().endswith(".png"):
                groups[rel.startswith("languages/")].append(rel)
    index = open(os.path.join(out, "index.csv"), encoding="utf-8").read().splitlines()
    for langs, zpath, folder in ((False, zip_path, PACK_MAIN), (True, langs_zip, PACK_LANGS)):
        rels = sorted(groups[langs])
        docs = tempfile.mkdtemp()
        write_docs(docs, folder, len(rels), langs)
        keep = set(rels)
        with open(os.path.join(docs, "index.csv"), "w", encoding="utf-8") as f:
            f.write("\n".join([index[0]] + [r for r in index[1:] if r.split(",")[0] in keep]) + "\n")
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_STORED) as z:   # PNG ya va comprimido
            for fn in ("README.txt", "LEEME.txt", "manifest.txt", "index.csv"):
                z.write(os.path.join(docs, fn), folder + "/" + fn)
            for rel in rels:
                z.write(os.path.join(out, rel), folder + "/" + rel)
        print("ZIP: %s (%d PNG, %.1f MB)" % (zpath, len(rels), os.path.getsize(zpath) / 1e6))


def _selftest():
    """Una DDS RGBA8 (BGRA en memoria) y una DXT1 sinteticas: deteccion, tamaño y colores."""
    from PIL import Image

    def dds(w, h, four=b"", bits=0, masks=(0, 0, 0, 0)):
        hd = bytearray(128)
        hd[0:4] = b"DDS "
        struct.pack_into("<IIII", hd, 4, 124, 0x1007, h, w)
        struct.pack_into("<II4sI", hd, 76, 32, 4 if four else 0x41, four or b"\0\0\0\0", bits)
        struct.pack_into("<IIII", hd, 92, *masks)
        return bytes(hd)
    px = bytes([10, 20, 200, 255]) * 4                      # B,G,R,A: rojo intenso
    rgba = dds(2, 2, bits=32, masks=(0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)) + px
    dxt1 = dds(4, 4, b"DXT1") + bytes(8)
    blob = b"junk" + rgba + b"pad!" + dxt1
    t = dds_textures(blob)
    assert [(w, h, f) for _, w, h, f, _ in t] == [(2, 2, "RGBA8"), (4, 4, "DXT1")], t
    assert t[0][4] == px and len(t[1][4]) == 8
    im = Image.open(io.BytesIO(to_png(blob, t[0][0])))
    assert im.size == (2, 2) and im.getpixel((0, 0)) == (200, 20, 10, 255), im.getpixel((0, 0))
    assert safe("ficha_habilidades_goku") == "skill_sheet_goku" and safe("Escenario Namek") == "stage_namek"
    print("autocomprobacion OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("salida", nargs="?")
    ap.add_argument("--juego", default=os.path.join(ROOT, "us"), help="carpeta con los data_*.afs (us/)")
    ap.add_argument("--zip", help="crear tambien este .zip (y <nombre>-Languages.zip con los textos de cada idioma)")
    ap.add_argument("--solo-zip", action="store_true", help="no extraer: solo crear los zips de la carpeta")
    ap.add_argument("--hilos", type=int, default=max(1, min(8, (os.cpu_count() or 2) - 1)))
    ap.add_argument("--prueba", action="store_true")
    a = ap.parse_args()
    if a.prueba:
        _selftest()
        return 0
    if not a.salida:
        ap.error("falta la carpeta de salida")
    try:
        import xxhash  # noqa: F401
    except ImportError:
        print("ERROR: falta el modulo xxhash (pip install xxhash)")
        return 1
    if a.solo_zip and a.zip:
        make_zips(a.salida, a.zip)
        return 0
    os.makedirs(a.salida, exist_ok=True)
    build(a.salida, a.juego, a.zip, a.hilos)
    return 0


if __name__ == "__main__":
    sys.exit(main())
