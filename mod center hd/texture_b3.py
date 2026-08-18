"""texture_b3.py - Mod de texturas B3 HD -> B3 HD desde el launcher.

Extrae las texturas (#AZT = DDS DXT3) de un personaje del data_cmn.afs como
PNG editables en mods/<mod>/textures/, y al reconstruir reinserta los PNG
editados (mismas dimensiones) re-codificando a DXT3 y manteniendo el bin del
mismo tamano (el AWO no cambia, solo los bitmaps de textura).

Uso:
  python texture_b3.py extract --bin <entrada_afs> [--mod <name>]
                               [--afs <data_cmn.afs>] [--out <mods_root>]
  python texture_b3.py build   --mod <name> [--afs <data_cmn.afs>]
                               [--out <mods_root>]

Requiere Pillow (DDS<->PNG) y el xbcompress/xbdecompress del XDK.
"""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))

TOOLS_DIR = os.path.join(ROOT, 'mod center',
                         'Xbox 360 Compression - Decompression tool '
                         'from the XBOX Development Kit')
XBCOMPRESS = os.path.join(TOOLS_DIR, 'xbcompress.exe')
XBDECOMPRESS = os.path.join(TOOLS_DIR, 'xbdecompress.exe')
DEFAULT_AFS = os.path.join(ROOT, 'us', 'data_cmn.afs')


def sanitize_name(name):
    """Quita los caracteres que Windows no permite en nombres de carpeta."""
    import re
    cleaned = re.sub(r'[<>:"/\\|?*\x00-\x1f]', '_', name)
    cleaned = cleaned.rstrip(' .')  # no puede terminar en espacio o punto
    return cleaned or 'tex'


def _clean_env(workdir):
    """Entorno para subprocesos externos con TEMP/TMP a una ruta valida.
    El launcher hereda a veces un TEMP invalido, lo que rompe xbcompress/
    xbdecompress (no pueden crear su propio temporal)."""
    env = dict(os.environ)
    env['TEMP'] = workdir
    env['TMP'] = workdir
    return env


def lzx_compress(src, dst, workdir=None):
    if os.path.exists(dst):
        os.remove(dst)
    env = _clean_env(workdir) if workdir else None
    r = subprocess.run([XBCOMPRESS, '/N:2048', src, dst],
                       capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise RuntimeError('xbcompress fallo: %s' % r.stdout + r.stderr)
    return os.path.getsize(dst)


def lzx_decompress(src, dst, workdir=None):
    if os.path.exists(dst):
        os.remove(dst)
    env = _clean_env(workdir) if workdir else None
    r = subprocess.run([XBDECOMPRESS, src, dst], capture_output=True, text=True,
                       env=env)
    if r.returncode != 0:
        raise RuntimeError('xbdecompress fallo: %s' % r.stdout + r.stderr)
    return os.path.getsize(dst)


def read_afs_index(afs_path):
    # El runtime (rexglue-sdk/src/filesystem/afs.cpp) lee la tabla en offset 8:
    #   offset 0: magic "AFS" (3B) + padding (1B)
    #   offset 4: count (uint32)
    #   offset 8: tabla de (addr uint32, size uint32) -- 8 bytes por entrada
    # Nota: leer la tabla en 0x10 producia un desfase de 1 entrada (off-by-one)
    # que extraia bins de la entrada equivocada y provocaba crashes al servir
    # el override (p.ej. tex_91 -> bin 92 en el slot 91). Corregido a offset 8.
    with open(afs_path, 'rb') as f:
        head = f.read(0x10)
    if head[:3] != b'AFS':
        raise RuntimeError('no es un AFS: %r' % head[:4])
    count = struct.unpack('<I', head[4:8])[0]
    entries = []
    with open(afs_path, 'rb') as f:
        f.seek(8)
        for _ in range(count):
            raw = f.read(8)
            addr, size = struct.unpack('<II', raw)
            entries.append((addr, size))
    return entries


def extract_afs_entry(afs_path, idx):
    entries = read_afs_index(afs_path)
    addr, size = entries[idx]
    with open(afs_path, 'rb') as f:
        f.seek(addr)
        return f.read(size)


def build_afs(afs_orig, idx, new_bin, out_path):
    afs = open(afs_orig, 'rb').read()
    n = struct.unpack('<I', afs[4:8])[0]
    base = 8  # tabla AFS en offset 8 (igual que el runtime)
    first_data = struct.unpack('<I', afs[base:base + 4])[0]
    old_loc = struct.unpack('<I', afs[base + idx * 8:base + idx * 8 + 4])[0]
    old_sz = struct.unpack('<I', afs[base + idx * 8 + 4:base + idx * 8 + 8])[0]

    delta_raw = len(new_bin) - old_sz
    delta = ((delta_raw + 0x7FF) & ~0x7FF) if delta_raw > 0 else 0

    table_a = bytearray()
    for i in range(n):
        loc = struct.unpack('<I', afs[base + i * 8:base + i * 8 + 4])[0]
        sz = struct.unpack('<I', afs[base + i * 8 + 4:base + i * 8 + 8])[0]
        if i == idx:
            sz = len(new_bin)
        elif i > idx and loc != 0:
            loc += delta
        table_a += struct.pack('<II', loc, sz)

    out_b = bytearray()
    out_b += afs[:base]
    out_b += table_a
    out_b += afs[base + n * 8:first_data]
    out_b += afs[first_data:old_loc]
    out_b += new_bin
    if delta_raw > 0:
        out_b += bytes(delta - delta_raw)
    out_b += afs[old_loc + old_sz:]
    with open(out_path, 'wb') as f:
        f.write(bytes(out_b))
    print('AFS reconstruido: %d bytes -> %s' % (len(out_b), out_path))


# ---------- #AZT parsing ----------

def parse_azt(bin_data):
    """Localiza el #AZT y devuelve la lista de texturas.

    Cada textura: dict con idx, off (rel AZT), w, h, data_off (rel AZT),
    bitmap_abs (offset absoluto en bin del bitmap DXT3), size.
    """
    i = bin_data.find(b'#AZT')
    if i < 0:
        raise RuntimeError('no se encontro #AZT en el bin')
    azt = bin_data[i:]
    azt_abs = i
    tex_am = struct.unpack('>I', azt[0x10:0x14])[0]
    index_loc = struct.unpack('>I', azt[0x14:0x18])[0]
    texs = []
    for n in range(tex_am):
        off = struct.unpack('>I', azt[index_loc + n * 4: index_loc + n * 4 + 4])[0]
        w = struct.unpack('>H', azt[off + 16:off + 18])[0]
        h = struct.unpack('>H', azt[off + 18:off + 20])[0]
        d = struct.unpack('>I', azt[off + 20:off + 24])[0]
        texs.append({'idx': n, 'off': off, 'w': w, 'h': h, 'data_off': d})
    # size of each = next data_off - current (or to end of AZT)
    for n in range(len(texs)):
        if n + 1 < len(texs):
            size = texs[n + 1]['data_off'] - texs[n]['data_off']
        else:
            size = len(azt) - texs[n]['data_off']
        texs[n]['size'] = size
        texs[n]['bitmap_abs'] = azt_abs + texs[n]['data_off'] + 128
    return azt_abs, texs


# ---------- DXT3 encoder (BC2) ----------

import numpy as np


def _to565(c):
    r, g, b = c
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _from565(v):
    r = ((v >> 11) & 0x1F) << 3
    g = ((v >> 5) & 0x3F) << 2
    b = (v & 0x1F) << 3
    return np.array([r, g, b], dtype=np.int32)


def _encode_block(block):
    alpha = block[:, :, 3].flatten()
    ab = bytearray(8)
    for i in range(0, 16, 2):
        a0 = min(15, int(alpha[i] / 16.0 + 0.5)) & 0xF
        a1 = min(15, int(alpha[i + 1] / 16.0 + 0.5)) & 0xF
        ab[i // 2] = (a1 << 4) | a0
    alpha_bytes = bytes(ab)

    rgb = block[:, :, :3].astype(np.int32).reshape(16, 3)
    cmin = rgb.min(axis=0)
    cmax = rgb.max(axis=0)
    if (cmin == cmax).all():
        c0 = _to565(cmax)
        c1 = c0
        return alpha_bytes + struct.pack('<HH', c0 & 0xFFFF, c1 & 0xFFFF) + bytes(4)

    c0 = _to565(cmax)
    c1 = _to565(cmin)
    if c0 < c1:
        c0, c1 = c1, c0
    p0 = _from565(c0)
    p1 = _from565(c1)
    p2 = (2 * p0 + p1) // 3
    p3 = (p0 + 2 * p1) // 3
    palette = np.stack([p0, p1, p2, p3])
    dist = ((rgb[:, None, :] - palette[None, :, :]) ** 2).sum(axis=2)
    idx = dist.argmin(axis=1).astype(np.uint8)
    indices = bytearray(4)
    for i in range(0, 16, 4):
        indices[i // 4] = (idx[i] | (idx[i + 1] << 2) |
                           (idx[i + 2] << 4) | (idx[i + 3] << 6)) & 0xFF
    return alpha_bytes + struct.pack('<HH', c0 & 0xFFFF, c1 & 0xFFFF) + bytes(indices)


def encode_dxt3(rgba):
    h, w = rgba.shape[:2]
    assert w % 4 == 0 and h % 4 == 0, "dimensiones deben ser multiplo de 4"
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            out += _encode_block(rgba[by:by + 4, bx:bx + 4])
    return bytes(out)


def decode_dxt3_bitmap(bitmap, w, h):
    """Decodifica un bitmap DXT3 (w*h bytes) a RGBA via Pillow (hack: construir
    un DDS temporal)."""
    header = (b'DDS \x7c\x00\x00\x00\x07\x10\x08\x00' +
              struct.pack('<IIII', h, w, h * w, 0) +
              bytes(44) +
              b'DXT3' + bytes(4) +
              struct.pack('<I', 32) + bytes(48) +
              struct.pack('<IIII', 0, 0, 0, 0) + bytes(8))
    dds = header + bitmap
    from PIL import Image
    import io
    im = Image.open(io.BytesIO(dds)).convert('RGBA')
    return np.array(im)


def cmd_extract(args):
    from PIL import Image
    import io
    # Carpeta de trabajo FIJA del proyecto (no depender de TEMP del entorno,
    # que puede estar invalido en el proceso del launcher).
    workdir = os.path.join(ROOT, 'out', 'build', 'win-amd64-release', '.tex_work')
    os.makedirs(workdir, exist_ok=True)

    mod_name = args.mod or ('tex_bin%d' % args.bin)
    mod_name = sanitize_name(mod_name)
    if args.out:
        mods_root = args.out
    else:
        mods_root = os.path.join(ROOT, 'out', 'build', 'win-amd64-release', 'mods')
    if args.dir:
        # Carpeta de texturas elegida por el usuario (p.ej. donde está
        # editando). El meta se guarda junto a los PNG. Si contiene caracteres
        # invalidos de Windows, ignorarla y usar la automatica.
        if any(c in args.dir for c in '<>:"|?*'):
            print('AVISO: la ruta de carpeta contiene caracteres invalidos; '
                  'usando la automatica: %s' % os.path.join(mods_root, mod_name, 'textures'))
            tex_dir = os.path.join(mods_root, mod_name, 'textures')
        else:
            tex_dir = args.dir
    else:
        tex_dir = os.path.join(mods_root, mod_name, 'textures')
    os.makedirs(tex_dir, exist_ok=True)

    # Extraer y descomprimir el bin
    raw = extract_afs_entry(args.afs, args.bin)
    lzx = os.path.join(workdir, 'bin.lzx')
    dec = os.path.join(workdir, 'bin.bin')
    open(lzx, 'wb').write(raw)
    try:
        lzx_decompress(lzx, dec, workdir)
    except RuntimeError as e:
        print('ERROR: no se pudo descomprimir bin %d: %s' % (args.bin, e))
        return 1
    bin_data = open(dec, 'rb').read()

    if not bin_data.startswith(b'#AMB'):
        print('ERROR: el bin %d no es un modelo #AMB (primero %r). Este slot '
              'probablemente no tiene modelo/texturas propios.' % (
                  args.bin, bin_data[:8]))
        return 1
    try:
        azt_abs, texs = parse_azt(bin_data)
    except RuntimeError as e:
        print('ERROR: %s. El personaje %d no tiene texturas #AZT propias '
              '(modelo parcial o variante que reutiliza las de otro bin).' % (e, args.bin))
        return 1
    # Guardar metadatos para reconstruir. Guardamos el header DDS original
    # (128B) de cada textura para poder reconstruir el DDS completo desde el
    # PNG editado (el build NO necesita el .dds en disco).
    import json
    tex_meta = []
    for t in texs:
        dds = bin_data[azt_abs + t['data_off']: azt_abs + t['data_off'] + t['size']]
        tex_meta.append({
            'idx': t['idx'], 'data_off': t['data_off'], 'size': t['size'],
            'w': t['w'], 'h': t['h'],
            'dds_header': list(dds[:128]),  # header DDS original para reconstruir
        })
    meta = {'bin_entry': args.bin, 'azt_abs': azt_abs, 'texs': tex_meta}
    with open(os.path.join(tex_dir, 'textures_meta.json'), 'w') as f:
        json.dump(meta, f, indent=1)

    print('Bin %d: %d texturas -> %s' % (args.bin, len(texs), tex_dir))
    for t in texs:
        # Solo generamos PNG editable. El DDS se reconstruye en el build a
        # partir del header guardado en el meta + el bitmap DXT3 del PNG.
        dds = bin_data[azt_abs + t['data_off']: azt_abs + t['data_off'] + t['size']]
        im = Image.open(io.BytesIO(dds)).convert('RGBA')
        png_path = os.path.join(tex_dir, 'tex%02d_%dx%d.png' % (t['idx'], t['w'], t['h']))
        im.save(png_path)
        print('  tex[%d] %dx%d -> %s' % (t['idx'], t['w'], t['h'], os.path.basename(png_path)))

    # manifest (solo cuando se usa la carpeta por defecto del mod; con --dir
    # el usuario esta editando en una carpeta propia, el mod se genera al build).
    if not args.dir:
        os.makedirs(os.path.join(mods_root, mod_name), exist_ok=True)
        with open(os.path.join(mods_root, mod_name, 'manifest.txt'), 'w', encoding='utf-8') as f:
            f.write('name=%s\n' % mod_name)
            f.write('description=Texturas del bin %d\n' % args.bin)
            f.write('type=data\n')
            f.write('source=%d\n' % args.bin)
    print('DONE. Edita los PNG en %s y ejecuta: texture_b3.py build --mod %s' % (
        tex_dir, mod_name))
    return 0


def cmd_build(args):
    import json
    mod_name = sanitize_name(args.mod)
    # Carpeta de trabajo FIJA del proyecto (no depender de TEMP del entorno).
    workdir = os.path.join(ROOT, 'out', 'build', 'win-amd64-release', '.tex_work')
    os.makedirs(workdir, exist_ok=True)
    if args.out:
        mods_root = args.out
    else:
        mods_root = os.path.join(ROOT, 'out', 'build', 'win-amd64-release', 'mods')
    if args.dir:
        # Carpeta de texturas custom (donde el usuario esta editando).
        if any(c in args.dir for c in '<>:"|?*'):
            print('AVISO: la ruta de carpeta contiene caracteres invalidos; '
                  'usando la automatica: %s' % os.path.join(mods_root, mod_name, 'textures'))
            tex_dir = os.path.join(mods_root, mod_name, 'textures')
        else:
            tex_dir = args.dir
    else:
        tex_dir = os.path.join(mods_root, mod_name, 'textures')
    meta_path = os.path.join(tex_dir, 'textures_meta.json')
    if not os.path.exists(meta_path):
        print('ERROR: no hay textures_meta.json en %s (ejecuta extract primero)' % tex_dir)
        return 1
    meta = json.load(open(meta_path))

    src_entry = meta['bin_entry']
    # Slot destino: por defecto el mismo bin del extract; con --slot se
    # aplican las texturas sobre el bin de OTRO personaje (compatible con
    # swaps de modelo: el bin del origen con sus texturas editadas se coloca
    # en el slot del destino).
    dest_entry = args.slot if args.slot is not None else src_entry
    print('Origen (texturas): bin %d -> Destino (slot): bin %d' % (src_entry, dest_entry))

    # Re-extraer el bin ORIGEN y descomprimir (para reinsertar sus texturas).
    raw = extract_afs_entry(args.afs, src_entry)
    lzx = os.path.join(workdir, 'bin.lzx')
    dec = os.path.join(workdir, 'bin.bin')
    open(lzx, 'wb').write(raw)
    lzx_decompress(lzx, dec, workdir)
    bin_data = bytearray(open(dec, 'rb').read())

    azt_abs = meta['azt_abs']
    changed = 0
    for t in meta['texs']:
        idx = t['idx']
        # buscar el PNG editado
        import glob
        cands = glob.glob(os.path.join(tex_dir, 'tex%02d_*.png' % idx))
        if not cands:
            continue
        png = cands[0]
        from PIL import Image
        im = Image.open(png).convert('RGBA')
        w, h = im.size
        if w != t['w'] or h != t['h']:
            print('WARN: tex[%d] tamano %dx%d != original %dx%d; reescalando' % (
                idx, w, h, t['w'], t['h']))
            im = im.resize((t['w'], t['h']))
        rgba = np.array(im)
        new_bitmap = encode_dxt3(rgba)
        if len(new_bitmap) != t['size'] - 128:
            print('ERROR: tex[%d] bitmap nuevo %d != %d' % (
                idx, len(new_bitmap), t['size'] - 128))
            return 1
        # Reconstruir el DDS completo: header original + bitmap nuevo.
        # Si el meta guarda el header (extract nuevo), lo usamos; si no
        # (meta viejo), lo leemos del bin original.
        if 'dds_header' in t and t['dds_header']:
            header = bytes(t['dds_header'])
        else:
            header = bytes(bin_data[azt_abs + t['data_off']: azt_abs + t['data_off'] + 128])
        new_dds = header + new_bitmap
        abs_off = azt_abs + t['data_off']
        bin_data[abs_off:abs_off + len(new_dds)] = new_dds
        changed += 1
        print('  tex[%d] %dx%d reemplazada' % (idx, w, h))

    if changed == 0:
        print('ERROR: ningun PNG editado en %s' % tex_dir)
        return 1
    print('%d texturas reemplazadas' % changed)

    # Comprimir y generar el mod como OVERRIDE POR ENTRADA (bajo peso).
    # En vez de reconstruir el data_cmn.afs completo (~279MB), colocamos solo
    # el bin LZX modificado en mods/<mod>/us/data_cmn.afs/<entry>/geom.bin.
    # El runtime (AfsFindModOverride en host_path_file.cpp) sirve ese archivo
    # en lugar de leer la entrada del AFS original.
    new_bin_path = os.path.join(workdir, 'bin_nuevo.bin')
    open(new_bin_path, 'wb').write(bytes(bin_data))
    new_lzx = os.path.join(workdir, 'bin_nuevo.lzx')
    lzx_compress(new_bin_path, new_lzx, workdir)
    new_data = open(new_lzx, 'rb').read()

    # Padding al to_read del guest: el guest lee el bin con un buffer de
    # tamaño = slot redondeado a 0x1000 (verificado: Krillin slot 104404 ->
    # to_read 106496). Si el bin del mod es mas corto, se paddea con ceros
    # para que el guest reciba todos los bytes esperados.
    entries = read_afs_index(args.afs)
    slot_sz = entries[dest_entry][1]
    to_read = ((slot_sz + 0xFFF) & ~0xFFF)  # ceil(slot / 0x1000) * 0x1000
    if len(new_data) < to_read:
        new_data = new_data + b'\x00' * (to_read - len(new_data))
    print('Override: entry %d slot=%d to_read=%d (bin comprimido %d -> %d)'
          % (dest_entry, slot_sz, to_read, len(open(new_lzx, 'rb').read()), len(new_data)))

    # Estructura: mods/<mod>/us/<afs_filename>/<entry_index>/geom.bin
    afs_name = os.path.basename(args.afs)  # p.ej. data_cmn.afs
    # Migracion: si el mod tenia un AFS completo viejo (mods/<mod>/us/xxx.afs
    # como ARCHIVO), borrarlo para poder crear el arbol de override por entrada
    # (mods/<mod>/us/xxx.afs/<entry>/geom.bin). El override pesa ~100KB vs
    # ~280MB del AFS completo.
    old_afs_file = os.path.join(mods_root, mod_name, 'us', afs_name)
    if os.path.isfile(old_afs_file):
        os.remove(old_afs_file)
        print('Migracion: eliminado AFS completo viejo (%s)' % old_afs_file)
    entry_dir = os.path.join(mods_root, mod_name, 'us', afs_name,
                             str(dest_entry))
    os.makedirs(entry_dir, exist_ok=True)
    geom_path = os.path.join(entry_dir, 'geom.bin')
    with open(geom_path, 'wb') as f:
        f.write(new_data)

    # Manifest para que el runtime lo registre como mod activo.
    with open(os.path.join(mods_root, mod_name, 'manifest.txt'),
              'w', encoding='utf-8') as f:
        f.write('name=%s\n' % mod_name)
        f.write('description=Texturas de bin %d aplicadas al slot %d\n'
                % (src_entry, dest_entry))
        f.write('type=data\n')
        f.write('source=%d\n' % src_entry)
        if dest_entry != src_entry:
            f.write('target=%d\n' % dest_entry)
    print('Mod generado (override por entrada, bajo peso): %s' % geom_path)
    print('DONE')
    return 0


def main():
    # Log de diagnostico del entorno (para depurar el error que solo ocurre
    # cuando el launcher lanza el script via _popen).
    try:
        log_path = os.path.join(ROOT, 'out', 'build', 'win-amd64-release',
                                'texture_b3_error.log')
        with open(log_path, 'a', encoding='utf-8') as _f:
            _f.write('\n===== INICIO =====\n')
            _f.write('cwd=%r\n' % os.getcwd())
            _f.write('argv=%r\n' % sys.argv)
            _f.write('TEMP=%r TMP=%r\n' % (os.environ.get('TEMP'),
                                           os.environ.get('TMP')))
            _f.write('HERE=%r ROOT=%r\n' % (HERE, ROOT))
            _f.write('gettempdir=%r\n' % __import__('tempfile').gettempdir())
    except Exception:
        pass
    ap = argparse.ArgumentParser(description='Mod de texturas B3 HD')
    sub = ap.add_subparsers(dest='cmd', required=True)

    pe = sub.add_parser('extract', help='extraer texturas a PNG')
    pe.add_argument('--bin', type=int, required=True, help='entrada AFS del personaje')
    pe.add_argument('--mod', default=None)
    pe.add_argument('--afs', default=DEFAULT_AFS)
    pe.add_argument('--out', default=None)
    pe.add_argument('--dir', default=None,
                    help='carpeta de salida de los PNG (default: mods/<mod>/textures). '
                         'Usar para guardar en una carpeta que elijas.')
    pe.set_defaults(func=cmd_extract)

    pb = sub.add_parser('build', help='reconstruir con PNG editados')
    pb.add_argument('--mod', required=True)
    pb.add_argument('--afs', default=DEFAULT_AFS)
    pb.add_argument('--out', default=None)
    pb.add_argument('--dir', default=None,
                    help='carpeta de los PNG editados (default: mods/<mod>/textures). '
                         'Usar si editaste en una carpeta custom.')
    pb.add_argument('--slot', type=int, default=None,
                    help='slot destino en el AFS (default: el mismo bin del extract). '
                         'Usar para aplicar las texturas sobre otro personaje (swap).')
    pb.set_defaults(func=cmd_build)

    args = ap.parse_args()
    if not os.path.exists(args.afs):
        print('ERROR: no se encontro %s' % args.afs)
        return 1
    if not os.path.exists(XBCOMPRESS):
        print('ERROR: no se encontro xbcompress en %s' % TOOLS_DIR)
        return 1
    return args.func(args)


if __name__ == '__main__':
    import traceback
    try:
        sys.exit(main())
    except SystemExit:
        raise
    except Exception:
        import io as _io
        print('\n=== TRACEBACK COMPLETO ===')
        traceback.print_exc()
        # Ademas, volcar el traceback a un archivo del proyecto para
        # diagnosticarlo sin depender del output del launcher.
        try:
            buf = _io.StringIO()
            traceback.print_exc(file=buf)
            log_path = os.path.join(ROOT, 'out', 'build', 'win-amd64-release',
                                    'texture_b3_error.log')
            with open(log_path, 'a', encoding='utf-8') as f:
                f.write('\n===== ERROR =====\n')
                f.write('TEMP=%r TMP=%r\n' % (os.environ.get('TEMP'),
                                              os.environ.get('TMP')))
                f.write('argv=%r\n' % sys.argv)
                f.write(buf.getvalue())
        except Exception:
            pass
        sys.exit(1)
