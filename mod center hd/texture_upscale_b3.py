#!/usr/bin/env python3
"""Upscale de texturas del B3 HD (#AZT) con IA + reconstruccion del bin.

Reescala las texturas de un bin de personaje (formato #AMB + #AZT) x2/x3/x4/x5,
las re-codifica a DXT3/BC2 y RECONSTRUYE el #AZT (descriptores + offsets +
tamanos) y la tabla #AMB, de forma que el juego lea las texturas grandes por la
misma ruta de siempre => coste de rendimiento en runtime CERO.

El #AZT es el ULTIMO bloque del bin y termina justo en EOF (verificado), asi
que crecer solo cambia el size de la entrada 1 de la tabla #AMB: no hay que
reubicar nada mas.

Formato verificado del descriptor (48 B, big-endian):
    +0x00 idx            +0x04 type (0x21=[T] 0x01=[B] 0x80000001=[S])
    +0x08 (w<<16)|h      +0x0C (log2 w<<16)|log2 h
    +0x10 w(u16),h(u16)  +0x14 data_off (rel AZT)
    +0x18 blob_bytes = w*h + 128 (cabecera DDS)   +0x1C = 1
El blob es: cabecera DDS de 128 B (little-endian) + bitmap DXT3 (w*h bytes).

Uso:
    python texture_upscale_b3.py --bin 327 --scale 4
    python texture_upscale_b3.py --bin 327 --scale 4 --skip-ai   (Lanczos, test)
    python texture_upscale_b3.py --bin 327 --list
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import texture_b3 as tb  # noqa: E402

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

DESC_SIZE = 48
DDS_HEADER = 128
DEFAULT_MODEL = 'realesrgan-x4plus-anime'
UPSCALER_CANDIDATES = [
    os.path.join(os.environ.get('TEMP', ''), 'opencode', 'realesrgan',
                 'realesrgan-ncnn-vulkan.exe'),
    os.path.join(HERE, 'tools', 'realesrgan', 'realesrgan-ncnn-vulkan.exe'),
]


def be32(b, o):
    return struct.unpack('>I', b[o:o + 4])[0]


def u16(b, o):
    return struct.unpack('>H', b[o:o + 2])[0]


def find_upscaler(explicit):
    if explicit:
        if os.path.isfile(explicit):
            return explicit
        raise SystemExit('ERROR: upscaler no encontrado: %s' % explicit)
    for p in UPSCALER_CANDIDATES:
        if os.path.isfile(p):
            return p
    raise SystemExit(
        'ERROR: falta realesrgan-ncnn-vulkan.exe.\n'
        '  Descarga: https://github.com/xinntao/Real-ESRGAN/releases\n'
        '  (realesrgan-ncnn-vulkan-20220424-windows.zip) y descomprimelo en\n'
        '  %s\n' % os.path.dirname(UPSCALER_CANDIDATES[0]))


# ---------------------------------------------------------------- AZT parse

def parse_azt_full(bin_data):
    """Devuelve dict con todo lo necesario para reconstruir el AZT."""
    a = bin_data.find(b'#AZT')
    if a < 0:
        raise SystemExit('ERROR: no hay #AZT en el bin')
    azt = bin_data[a:]
    tex_am = be32(azt, 0x10)
    index_loc = be32(azt, 0x14)
    descs = [be32(azt, index_loc + n * 4) for n in range(tex_am)]
    if not (0 < tex_am < 4000 and index_loc + tex_am * 4 <= len(azt)):
        raise SystemExit('ERROR: cabecera AZT invalida')
    texs = []
    for n, off in enumerate(descs):
        if off + DESC_SIZE > len(azt):
            raise SystemExit('ERROR: descriptor %d fuera del AZT' % n)
        w, h = u16(azt, off + 0x10), u16(azt, off + 0x12)
        d = be32(azt, off + 0x14)
        blob = be32(azt, off + 0x18)
        if not (0 < w <= 4096 and 0 < h <= 4096 and w % 4 == 0 and h % 4 == 0):
            raise SystemExit('ERROR: dimensiones raras en tex %d (%dx%d)' % (n, w, h))
        texs.append({'idx': n, 'desc_off': off, 'w': w, 'h': h,
                     'data_off': d, 'blob': blob})
    datas = sorted(t['data_off'] for t in texs)
    if datas != [t['data_off'] for t in texs]:
        raise SystemExit('ERROR: data_offs no ordenados (no soportado)')
    max_desc = max(t['desc_off'] for t in texs) + DESC_SIZE
    first_data = datas[0]
    if max_desc > first_data:
        raise SystemExit('ERROR: descriptores solapados con datos')
    # cada blob debe medir 128 + w*h y ser contiguo con el siguiente
    for i, t in enumerate(texs):
        expect = DDS_HEADER + t['w'] * t['h']
        if t['blob'] != expect:
            raise SystemExit('ERROR: blob %d = %d, esperado %d (w=%d h=%d)'
                             % (t['idx'], t['blob'], expect, t['w'], t['h']))
        end = t['data_off'] + t['blob']
        nxt = texs[i + 1]['data_off'] if i + 1 < len(texs) else len(azt)
        if end != nxt:
            raise SystemExit('ERROR: hueco/solape tras tex %d (%d != %d)'
                             % (t['idx'], end, nxt))
    return {'azt_abs': a, 'azt': azt, 'tex_am': tex_am, 'index_loc': index_loc,
            'texs': texs, 'first_data': first_data}


def validate_amb(bin_data, azt_abs, azt_len):
    if bin_data[:4] != b'#AMB':
        raise SystemExit('ERROR: (de momento) solo se soportan bins #AMB')
    count = be32(bin_data, 0x0C)
    table = 0x20
    blocks = []
    for i in range(count):
        loc = be32(bin_data, table + i * 16)
        size = be32(bin_data, table + i * 16 + 4)
        blocks.append((loc, size))
    hit = [i for i, (loc, _) in enumerate(blocks) if loc == azt_abs]
    if len(hit) != 1:
        raise SystemExit('ERROR: el AZT no esta en la tabla #AMB')
    last = max(blocks, key=lambda x: x[0])
    if last[0] != azt_abs:
        raise SystemExit('ERROR: el #AZT no es el ultimo bloque (no soportado)')
    if azt_abs + azt_len != len(bin_data):
        raise SystemExit('ERROR: el AZT no termina en EOF (no soportado)')
    return {'table': table, 'entry': hit[0], 'count': count,
            'entry_off': table + hit[0] * 16}


# ---------------------------------------------------------------- imagenes

def dxt3_to_rgba(azt, t):
    """Decodifica usando la cabecera DDS REAL del blob (128 B; la del helper de
    texture_b3 tiene 156 B y desalinea el bitmap)."""
    import io
    blob = azt[t['data_off']:t['data_off'] + t['blob']]
    return np.array(Image.open(io.BytesIO(blob)).convert('RGBA'))


def fill_transparent(rgba):
    """Rellena el RGB de los pixeles transparentes con el color opaco vecino
    (evita que la IA invente colores y los sangre en los bordes)."""
    rgb = rgba[:, :, :3].astype(np.float32)
    a = rgba[:, :, 3]
    opaque = a > 8
    if opaque.all():
        return rgba[:, :, :3].copy()
    if not opaque.any():
        # alfa 0 en TODA la textura: el juego la usa como opaca (ignora el
        # alfa) => el RGB es el contenido real, NO se toca.
        return rgba[:, :, :3].copy()
    out = rgb.copy()
    mean_color = rgb[opaque].mean(axis=0)
    for _ in range(64):
        if opaque.all():
            break
        acc = np.zeros_like(out)
        cnt = np.zeros(a.shape, dtype=np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sr = np.roll(out, (dy, dx), (0, 1))
            so = np.roll(opaque, (dy, dx), (0, 1))
            acc += sr * so[:, :, None]
            cnt += so
        newly = (~opaque) & (cnt > 0)
        out[newly] = acc[newly] / cnt[newly, None]
        opaque = opaque | newly
    out[~opaque] = mean_color  # lo que no se alcanzo: color neutro, no basura
    return out.astype(np.uint8)


def run_upscaler(exe, indir, outdir, model, ai_scale, tile, gpu=None):
    cmd = [exe, '-i', indir, '-o', outdir, '-s', str(ai_scale), '-n', model]
    if tile:
        cmd += ['-t', str(tile)]
    if gpu is not None:
        cmd += ['-g', str(gpu)]
    print('  $ %s' % ' '.join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=os.path.dirname(exe))
    if r.returncode != 0:
        sys.stdout.write(r.stdout[-3000:])
        sys.stderr.write(r.stderr[-3000:])
        raise SystemExit('ERROR: realesrgan fallo (rc=%d)' % r.returncode)


# ---------------------------------------------------------------- rebuild

def patch_descriptor(desc, w, h, data_off):
    d = bytearray(desc)
    struct.pack_into('>I', d, 0x08, ((w & 0xFFFF) << 16) | (h & 0xFFFF))
    lw = w.bit_length() - 1
    lh = h.bit_length() - 1
    if (1 << lw) != w or (1 << lh) != h:
        raise SystemExit('ERROR: %dx%d no es potencia de 2 (log2 no valido)' % (w, h))
    struct.pack_into('>I', d, 0x0C, ((lw & 0xFFFF) << 16) | (lh & 0xFFFF))
    struct.pack_into('>H', d, 0x10, w)
    struct.pack_into('>H', d, 0x12, h)
    struct.pack_into('>I', d, 0x14, data_off)
    struct.pack_into('>I', d, 0x18, w * h + DDS_HEADER)
    return bytes(d)


def build_new_bin(bin_data, info, scale, new_rgba):
    """new_rgba: lista de arrays RGBA al tamano nuevo (mismo orden que texs)."""
    azt = info['azt']
    body = bytearray(azt[:info['first_data']])
    data = bytearray()
    for t, rgba in zip(info['texs'], new_rgba):
        w, h = t['w'] * scale, t['h'] * scale
        bitmap = tb.encode_dxt3(rgba)
        if len(bitmap) != w * h:
            raise SystemExit('ERROR: DXT3 inesperado %d != %d' % (len(bitmap), w * h))
        header = bytearray(azt[t['data_off']:t['data_off'] + DDS_HEADER])
        if bytes(header[:4]) != b'DDS ':
            raise SystemExit('ERROR: la cabecera DDS de tex %d no es DDS' % t['idx'])
        struct.pack_into('<I', header, 0x0C, h)
        struct.pack_into('<I', header, 0x10, w)
        struct.pack_into('<I', header, 0x14, w * h)
        new_off = len(body) + len(data)
        d = patch_descriptor(azt[t['desc_off']:t['desc_off'] + DESC_SIZE],
                             w, h, new_off)
        body[t['desc_off']:t['desc_off'] + DESC_SIZE] = d
        data += bytes(header) + bitmap
    new_azt = bytes(body) + bytes(data)
    new_bin = bytearray(bin_data[:info['azt_abs']] + new_azt)
    # size de la entrada del AZT en la tabla #AMB
    struct.pack_into('>I', new_bin, info['amb']['entry_off'] + 4, len(new_azt))
    return bytes(new_bin)


def verify_new_bin(new_bin, old_info, scale, verbose=True):
    info = parse_azt_full(new_bin)
    info['amb'] = validate_amb(new_bin, info['azt_abs'], len(info['azt']))
    if len(info['texs']) != len(old_info['texs']):
        raise SystemExit('ERROR verify: cambia el numero de texturas')
    for a, b in zip(old_info['texs'], info['texs']):
        if (b['w'], b['h']) != (a['w'] * scale, a['h'] * scale):
            raise SystemExit('ERROR verify: dims tex %d' % a['idx'])
    if verbose:
        print('  verify OK: %d texturas, %d -> %d B'
              % (len(info['texs']), len(old_info['azt']), len(info['azt'])))
    return info


# ---------------------------------------------------------------- install

def install(new_data, mod_name, afs_path, dest_entry, mods_root, info):
    afs_name = os.path.basename(afs_path)
    old = os.path.join(mods_root, mod_name, 'us', afs_name)
    if os.path.isfile(old):
        os.remove(old)
    entry_dir = os.path.join(mods_root, mod_name, 'us', afs_name, str(dest_entry))
    os.makedirs(entry_dir, exist_ok=True)
    with open(os.path.join(entry_dir, 'geom.bin'), 'wb') as f:
        f.write(new_data)
    with open(os.path.join(mods_root, mod_name, 'manifest.txt'), 'w',
              encoding='utf-8') as f:
        f.write('name=%s\n' % mod_name)
        f.write('description=Texturas x%d con IA (%d texturas)\n'
                % (info['scale'], len(info['texs'])))
        f.write('type=texture_hd\n')
        f.write('source=%d\n' % info['src'])
        f.write('target=%d\n' % dest_entry)
        f.write('scale=%d\n' % info['scale'])
    return entry_dir


def main():
    ap = argparse.ArgumentParser(description='Upscale IA de texturas B3 HD')
    ap.add_argument('--bin', type=int, default=327, help='entrada AFS origen')
    ap.add_argument('--slot', type=int, default=None, help='entrada destino')
    ap.add_argument('--scale', type=int, default=4, choices=[1, 2, 3, 4, 5])
    ap.add_argument('--model', default=DEFAULT_MODEL)
    ap.add_argument('--upscaler', default=None)
    ap.add_argument('--mod', default=None)
    ap.add_argument('--out', default=None, help='raiz de mods')
    ap.add_argument('--afs', default=None)
    ap.add_argument('--tile', type=int, default=0)
    ap.add_argument('--gpu', type=int, default=None)
    ap.add_argument('--skip-ai', action='store_true',
                    help='Lanczos en vez de IA (test estructural)')
    ap.add_argument('--bin-out', default=None, help='escribir el bin nuevo (debug)')
    ap.add_argument('--no-install', action='store_true')
    ap.add_argument('--list', action='store_true', help='solo listar texturas')
    args = ap.parse_args()

    afs = args.afs or tb.DEFAULT_AFS
    if not afs or not os.path.isfile(afs):
        raise SystemExit('ERROR: AFS no encontrado (%s)' % afs)
    dest = args.slot if args.slot is not None else args.bin
    mod_name = tb.sanitize_name(args.mod or ('texai%d_bin%d' % (args.scale, args.bin)))

    work = os.path.join(ROOT, 'out', 'build', 'win-amd64-release', '.tex_upscale')
    if not os.path.isdir(os.path.dirname(os.path.dirname(work))):
        work = os.path.join(os.environ.get('TEMP', '.'), 'dbz3_tex_upscale')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)

    # 1) cargar y validar
    raw = tb.extract_afs_entry(afs, args.bin)
    lzx = os.path.join(work, 'bin.lzx')
    dec = os.path.join(work, 'bin.bin')
    with open(lzx, 'wb') as f:
        f.write(raw)
    try:
        tb.lzx_decompress(lzx, dec, workdir=work)
        with open(dec, 'rb') as f:
            bin_data = f.read()
    except RuntimeError:
        bin_data = raw
    info = parse_azt_full(bin_data)
    info['amb'] = validate_amb(bin_data, info['azt_abs'], len(info['azt']))
    info['src'] = args.bin
    info['scale'] = args.scale
    tot = sum(t['w'] * t['h'] for t in info['texs'])
    print('bin %d: %d texturas, %.1f MB de bitmap; escalado x%d -> %.1f MB'
          % (args.bin, len(info['texs']), tot / 1e6, args.scale,
             tot * args.scale * args.scale / 1e6))
    if args.list:
        for t in info['texs']:
            print('  tex%02d %4dx%-4d %6d B  type=0x%08x'
                  % (t['idx'], t['w'], t['h'], t['blob'],
                     be32(info['azt'], t['desc_off'] + 4)))
        return 0

    # 2) extraer a PNG (RGB rellenado + alfa)
    indir = os.path.join(work, 'in')
    outdir = os.path.join(work, 'out')
    os.makedirs(indir, exist_ok=True)
    os.makedirs(outdir, exist_ok=True)
    alphas = {}
    for t in info['texs']:
        rgba = dxt3_to_rgba(info['azt'], t)
        rgb = fill_transparent(rgba)
        name = 'tex%02d.png' % t['idx']
        Image.fromarray(rgb).save(os.path.join(indir, name))
        alphas[t['idx']] = np.array(Image.fromarray(rgba[:, :, 3]))
    print('  extraidas %d texturas a %s' % (len(info['texs']), indir))

    # 3) upscale
    if args.skip_ai:
        print('  modo --skip-ai: Lanczos')
    else:
        exe = find_upscaler(args.upscaler)
        # La IA corre a su escala nativa (4x del modelo) y luego se reduce con
        # Lanczos al factor pedido: mas calidad que pedirle al ncnn un -s 2/3.
        run_upscaler(exe, indir, outdir, args.model, 4, args.tile, args.gpu)

    # 4) recomponer RGBA al tamano nuevo
    new_rgba = []
    for t in info['texs']:
        W, H = t['w'] * args.scale, t['h'] * args.scale
        src = os.path.join(outdir, 'tex%02d.png' % t['idx'])
        if args.skip_ai or not os.path.isfile(src):
            im = Image.fromarray(
                np.dstack([np.array(Image.open(os.path.join(indir, 'tex%02d.png'
                                                            % t['idx'])).convert('RGB')),
                           alphas[t['idx']]])).resize((W, H), Image.LANCZOS)
            new_rgba.append(np.array(im))
            continue
        rgb = Image.open(src).convert('RGB')
        if rgb.size != (W, H):
            rgb = rgb.resize((W, H), Image.LANCZOS)
        alpha = Image.fromarray(alphas[t['idx']]).resize((W, H), Image.LANCZOS)
        new_rgba.append(np.dstack([np.array(rgb), np.array(alpha)]))

    # 5) reconstruir y verificar
    new_bin = build_new_bin(bin_data, info, args.scale, new_rgba)
    verify_new_bin(new_bin, info, args.scale)

    # 6) salvar muestras de comparacion
    cmp_dir = os.path.join(work, 'compare')
    os.makedirs(cmp_dir, exist_ok=True)
    for t in info['texs'][:6]:
        Image.fromarray(dxt3_to_rgba(info['azt'], t)).save(
            os.path.join(cmp_dir, 'tex%02d_x1.png' % t['idx']))
        Image.fromarray(new_rgba[t['idx']]).save(
            os.path.join(cmp_dir, 'tex%02d_x%d.png' % (t['idx'], args.scale)))

    if args.bin_out:
        with open(args.bin_out, 'wb') as f:
            f.write(new_bin)
        print('  bin nuevo: %s' % args.bin_out)

    # 7) LZX + padding + instalar
    if args.no_install:
        print('--no-install: fin (workdir %s)' % work)
        return 0
    new_raw = os.path.join(work, 'new_raw.bin')
    with open(new_raw, 'wb') as f:
        f.write(new_bin)
    new_lzx = os.path.join(work, 'new.lzx')
    tb.lzx_compress(new_raw, new_lzx, work)
    data = open(new_lzx, 'rb').read()
    entries = tb.read_afs_index(afs)
    slot_sz = entries[dest][1]
    to_read = ((slot_sz + 0xFFF) & ~0xFFF)
    comp = len(data)
    if len(data) < to_read:
        data += b'\x00' * (to_read - len(data))
    mods_root = args.out
    if not mods_root:
        dev = os.path.join(ROOT, 'out', 'build', 'win-amd64-release', 'mods')
        mods_root = dev if os.path.isdir(os.path.dirname(dev)) else os.path.join(ROOT, 'mods')
    os.makedirs(mods_root, exist_ok=True)
    entry_dir = install(data, mod_name, afs, dest, mods_root, info)
    print('  comprimido %d -> %d B (slot %d, to_read %d)%s'
          % (len(new_bin), comp, slot_sz, to_read,
             '  [mid-insert virtual]' if comp > to_read else ''))
    print('  muestras: %s' % cmp_dir)
    print('MOD: %s' % entry_dir)
    print('DONE (mod=%s)' % mod_name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
