#!/usr/bin/env python3
"""Importa un volcado de texturas de dbz3 (dbz3_texture_dump) y lo organiza.

El runtime (rexgpu-xenos.dll) escribe, al cargar cada textura unica, un DDS con
el bitmap ORIGINAL (linealizado y en little-endian, identico al del #AZT) mas
una linea en `index.jsonl`. Formatos: DXT1/DXT3/DXT5 (comprimidos) y los SIN
comprimir que usa el HUD/UI (RGBA8, RGB565, RGB5A1, RGB655, RGBA4, L8, L8A8,
RGBA1010102). Este importador:

  1. convierte cada DDS a PNG (Pillow), y
  2. si se le pasa el AFS + el catalogo, identifica a que personaje y a que
     textura pertenece cada volcado comparando el bitmap con el #AZT de cada bin
     (mismo dato, byte a byte), y lo coloca en
         <out>/<personaje>/<texNN>_<WxH>.png
     Los que no casen van a <out>/_unknown/.

Uso:
    python texture_dump_import.py <dump_dir> <out_dir>
        [--afs us/data_cmn.afs] [--catalog "mod center hd/catalog_b3.cat"]
        [--bins 70-90] [--no-match] [--opaque-alpha] [--limit N] [--max-bins N]

Sin --afs/--catalog (o con --no-match) solo convierte y agrupa por WxH.

Requiere Pillow y los xbcompress/xbdecompress del XDK (para descomprimir los
bins del AFS), reutilizando las funciones de `mod center hd/texture_b3.py`.
"""

import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def load_texture_b3():
    """Carga `mod center hd/texture_b3.py` como modulo (la ruta tiene espacios)."""
    path = os.path.join(ROOT, 'mod center hd', 'texture_b3.py')
    spec = importlib.util.spec_from_file_location('texture_b3', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_index(dump_dir):
    entries = []
    index_path = os.path.join(dump_dir, 'index.jsonl')
    with open(index_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line:
                entries.append(json.loads(line))
    return entries


def dds_bitmap(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'DDS ':
        raise RuntimeError('no es DDS: %s' % path)
    return data[128:]


def load_catalog(path):
    """catalog_b3.cat: bin|nombre|label|variante|jugable."""
    rows = []
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split('|')
            if len(parts) < 2:
                continue
            rows.append((int(parts[0]), parts[1]))
    return rows


def build_bin_index(texture_b3, afs_path, catalog_path, bins_filter, max_bins):
    """md5(bitmap #AZT) -> (personaje, tex_index, w, h)."""
    import tempfile
    workdir = os.path.join(tempfile.gettempdir(), 'dbz3_texdump_import')
    os.makedirs(workdir, exist_ok=True)
    index = {}
    rows = load_catalog(catalog_path)
    if bins_filter is not None:
        rows = [r for r in rows if bins_filter[0] <= r[0] <= bins_filter[1]]
    if max_bins:
        rows = rows[:max_bins]
    for n, (bin_idx, name) in enumerate(rows):
        try:
            raw = texture_b3.extract_afs_entry(afs_path, bin_idx)
            comp = os.path.join(workdir, 'bin%04d.lzx' % bin_idx)
            dec = os.path.join(workdir, 'bin%04d.bin' % bin_idx)
            with open(comp, 'wb') as f:
                f.write(raw)
            texture_b3.lzx_decompress(comp, dec, workdir=workdir)
            with open(dec, 'rb') as f:
                bin_data = f.read()
            _, texs = texture_b3.parse_azt(bin_data)
        except Exception as exc:  # bin sin #AZT o no descomprimible
            print('  bin %d (%s): omitido (%s)' % (bin_idx, name, exc))
            continue
        for t in texs:
            size = t['w'] * t['h']
            bmp = bin_data[t['bitmap_abs']:t['bitmap_abs'] + size]
            if len(bmp) != size:
                continue
            index[hashlib.md5(bmp).hexdigest()] = (name, t['idx'], t['w'], t['h'])
        if (n + 1) % 10 == 0:
            print('  ... %d/%d bins' % (n + 1, len(rows)))
    return index


def sanitize(name):
    for ch in '<>:"/\\|?*':
        name = name.replace(ch, '_')
    return name.strip(' .') or 'tex'


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dump_dir')
    ap.add_argument('out_dir')
    ap.add_argument('--afs', default=None)
    ap.add_argument('--catalog', default=os.path.join(ROOT, 'mod center hd', 'catalog_b3.cat'))
    ap.add_argument('--bins', default=None, help='rango de bins, p.ej. 70-95')
    ap.add_argument('--no-match', action='store_true')
    ap.add_argument('--opaque-alpha', action='store_true',
                    help='si el alpha del DDS esta TODO a cero, escribe el PNG opaco. Son '
                         'texturas que el juego dibuja ignorando el alpha de la textura, pero '
                         'que un visor muestra como un cuadrado negro (y transparente).')
    ap.add_argument('--limit', type=int, default=0, help='procesar solo N volcados')
    ap.add_argument('--max-bins', type=int, default=0, help='limitar bins escaneados')
    args = ap.parse_args()

    from PIL import Image

    entries = read_index(args.dump_dir)
    if args.limit:
        entries = entries[:args.limit]
    print('Volcados: %d' % len(entries))

    bins_filter = None
    if args.bins:
        a, b = args.bins.split('-')
        bins_filter = (int(a), int(b))

    index = {}
    if not args.no_match and args.afs and os.path.exists(args.afs):
        texture_b3 = load_texture_b3()
        print('Escaneando bins para identificar texturas...')
        index = build_bin_index(texture_b3, args.afs, args.catalog, bins_filter, args.max_bins)
        print('Texturas indexadas: %d' % len(index))
    elif not args.no_match:
        print('Sin --afs: solo conversion (agrupado por WxH).')

    os.makedirs(args.out_dir, exist_ok=True)
    manifest = []
    matched = 0
    for e in entries:
        dds_path = os.path.join(args.dump_dir, e['file'])
        if not os.path.exists(dds_path):
            continue
        bitmap = dds_bitmap(dds_path)
        key = hashlib.md5(bitmap).hexdigest()
        info = index.get(key)
        if info:
            name, tex_idx, w, h = info
            folder = sanitize(name)
            png_name = 'tex%02d_%dx%d.png' % (tex_idx, w, h)
            matched += 1
        else:
            folder = '_unknown' if index else ('%dx%d' % (e['width'], e['height']))
            png_name = '%s_%dx%d.png' % (e['hash'], e['width'], e['height'])
        out_folder = os.path.join(args.out_dir, folder)
        os.makedirs(out_folder, exist_ok=True)
        out_png = os.path.join(out_folder, png_name)
        alpha_all_zero = False
        try:
            im = Image.open(dds_path)
            if args.opaque_alpha and im.mode in ('RGBA', 'LA', 'PA'):
                im = im.convert('RGBA')
                alpha_all_zero = im.getchannel('A').getextrema() == (0, 0)
                if alpha_all_zero:
                    im.putalpha(255)
            im.save(out_png)
        except Exception as exc:
            print('  %s: fallo al convertir (%s)' % (e['file'], exc))
            continue
        manifest.append({
            'dump': e['file'],
            'png': os.path.relpath(out_png, args.out_dir).replace('\\', '/'),
            'character': info[0] if info else None,
            'texture_index': info[1] if info else None,
            'width': e['width'],
            'height': e['height'],
            'format': e['format'],
            'dds': e.get('dds'),
            'alpha_all_zero': alpha_all_zero,
        })

    with open(os.path.join(args.out_dir, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    print('Listo: %d PNG (%d identificados) en %s' % (len(manifest), matched, args.out_dir))
    return 0


if __name__ == '__main__':
    sys.exit(main())
