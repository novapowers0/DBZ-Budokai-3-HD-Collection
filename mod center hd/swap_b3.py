"""swap_b3.py - Swap nativo B3 HD -> B3 HD desde el launcher.

Intercambia el bin #AMB COMPLETO de un personaje HD del B3 por el de otro
personaje HD del mismo juego (swap nativo, validado con Goten->Krillin).

El bin #AMB completo (AWO+AZT) del personaje ORIGEN se extrae del
data_cmn.afs, se comprime LZX (/N:2048) y se coloca en el slot del personaje
DESTINO (entrada AFS) como OVERRIDE POR ENTRADA.

En vez de reconstruir el data_cmn.afs completo (~279MB por mod), el mod solo
contiene el bin modificado en mods/<mod>/us/data_cmn.afs/<entry>/geom.bin.
El runtime (AfsFindModOverride en rexglue-sdk/src/filesystem/afs.cpp) sirve
ese archivo en lugar de leer la entrada del AFS original -> el mod pesa
~100KB y pueden coexistir VARIOS mods activos (cada uno toca entradas
distintas del mismo AFS).

Restricciones (documentadas en AGENTS.md):
  - El bin comprimido DEBE caber en el slot destino: el guest lee el bin con
    to_read = ceil(slot / 0x1000) * 0x1000 (p.ej. slot 105296 -> 106496).
    Con override por entrada NO hay mid-insert: si el bin excede to_read se
    avisa y se aborta (usar otro slot mas grande o decimar la geometria).
  - Si el bin es mas corto que to_read se paddea con ceros (igual que
    texture_b3.py) para que el guest reciba todos los bytes esperados.

Uso:
  python swap_b3.py --origen <bin_origen> --dest <slot_destino> [--mod <name>]
                    [--afs <data_cmn.afs>] [--out <mods_root>]

  swap_b3.py --list            lista bins de personajes (catalogo)
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))


def _first_existing(*paths):
    for p in paths:
        if os.path.exists(p):
            return p
    return paths[0]


# Ruta del xbcompress / xbdecompress (XDK). Se busca en varios sitios para que
# el script funcione tanto desde el repo de desarrollo como desde el paquete de
# release standalone (donde las tools viven junto al script).
TOOLS_DIR = _first_existing(
    os.path.join(HERE, 'tools'),
    os.path.join(HERE, '..', 'mod center',
                 'Xbox 360 Compression - Decompression tool '
                 'from the XBOX Development Kit'),
    os.path.join(HERE, '..', 'tools'),
)
XBCOMPRESS = os.path.join(TOOLS_DIR, 'xbcompress.exe')
XBDECOMPRESS = os.path.join(TOOLS_DIR, 'xbdecompress.exe')

# AFS del B3 (modelos). Deteccion por prioridad:
#   1) junto al exe (paquete release): <exe>/assets/us/data_cmn.afs o <exe>/us/data_cmn.afs
#   2) proyecto de desarrollo: <root>/us/data_cmn.afs
# En el paquete de release "mod center hd/" vive DENTRO de la carpeta del exe,
# asi que ROOT (padre de la carpeta del script) ES el directorio del exe.
DEFAULT_AFS = _first_existing(
    os.path.join(ROOT, 'assets', 'us', 'data_cmn.afs'),
    os.path.join(ROOT, 'us', 'data_cmn.afs'),
)


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
    """Reconstruye el AFS con el bin nuevo en el slot idx (mid-insert).

    Replica build_afs.py del awo_tools (mismo metodo validado):
      - entrada idx mantiene su loc, el bin crece en su lugar
      - entradas posteriores se desplazan por delta (multiplo 0x800)
    """
    afs = open(afs_orig, 'rb').read()
    n = struct.unpack('<I', afs[4:8])[0]
    base = 8  # tabla AFS en offset 8 (igual que el runtime)
    first_data = struct.unpack('<I', afs[base:base + 4])[0]
    old_loc = struct.unpack('<I', afs[base + idx * 8:base + idx * 8 + 4])[0]
    old_sz = struct.unpack('<I', afs[base + idx * 8 + 4:base + idx * 8 + 8])[0]

    delta_raw = len(new_bin) - old_sz
    delta = ((delta_raw + 0x7FF) & ~0x7FF) if delta_raw > 0 else 0
    print('AFS: %d entradas, header real=0x%X' % (n, first_data))
    print('Entrada %d: loc=0x%X size=%d -> nuevo bin %d bytes (delta %+d)' % (
        idx, old_loc, old_sz, len(new_bin), delta))

    # Tabla A ajustada.
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
    assert len(out_b) == first_data, 'header size mismatch'

    out_b += afs[first_data:old_loc]
    out_b += new_bin
    if delta_raw > 0:
        out_b += bytes(delta - delta_raw)
    out_b += afs[old_loc + old_sz:]

    with open(out_path, 'wb') as f:
        f.write(bytes(out_b))
    print('AFS reconstruido: %d bytes -> %s' % (len(out_b), out_path))


def labels_of_amb(amb):
    """Label raiz del primer AWG del #AWO contenido en un #AMB (o None)."""
    i = amb.find(b'#AWO')
    if i < 0:
        return None
    awo = amb[i:]
    if len(awo) < 0x20:
        return None
    n = struct.unpack('>I', awo[0x18:0x1C])[0]
    tbl = struct.unpack('>I', awo[0x1C:0x20])[0]
    if not n or tbl + 4 > len(awo):
        return None
    off = struct.unpack('>I', awo[tbl:tbl + 4])[0]
    if off + 0x40 > len(awo):
        return None
    no = struct.unpack('>I', awo[off + 0x1C:off + 0x20])[0]
    if off + no + 16 > len(awo):
        return None
    lab = awo[off + no:off + no + 16].split(b'\x00')[0].decode('latin1', 'ignore')
    return lab or None


def main():
    ap = argparse.ArgumentParser(description='Swap nativo B3->B3')
    ap.add_argument('--origen', type=int, help='bin origen (entrada AFS)')
    ap.add_argument('--dest', type=int, help='slot destino (entrada AFS)')
    ap.add_argument('--mod', default=None)
    ap.add_argument('--afs', default=DEFAULT_AFS)
    ap.add_argument('--out', default=None, help='raiz de mods (default <root>/out/.../mods)')
    args = ap.parse_args()

    if not os.path.exists(args.afs):
        print('ERROR: no se encontro %s' % args.afs)
        return 1
    if not os.path.exists(XBCOMPRESS):
        print('ERROR: no se encontro xbcompress en %s' % TOOLS_DIR)
        return 1
    if args.origen is None or args.dest is None:
        ap.print_help()
        return 1

    # Carpeta de trabajo (no depender del TEMP del entorno, que puede estar
    # invalido en el proceso del launcher). En el repo de desarrollo se usa la
    # fija del build; en el paquete de release se usa el TEMP corregido.
    workdir = os.path.join(ROOT, 'out', 'build', 'win-amd64-release', '.swap_work')
    if not os.path.isdir(workdir):
        workdir = os.path.join(tempfile.gettempdir(), 'dbz3_swap_work')
    os.makedirs(workdir, exist_ok=True)

    # 1. Extraer el bin origen.
    print('Extraer bin origen %d...' % args.origen)
    orig_data = extract_afs_entry(args.afs, args.origen)
    orig_lzx = os.path.join(workdir, 'origen.lzx')
    orig_bin = os.path.join(workdir, 'origen.bin')
    open(orig_lzx, 'wb').write(orig_data)
    try:
        lzx_decompress(orig_lzx, orig_bin, workdir)
    except RuntimeError as e:
        print('ERROR: no se pudo descomprimir bin origen: %s' % e)
        return 1
    amb = open(orig_bin, 'rb').read()
    if amb[:4] != b'#AMB':
        print('ERROR: el bin origen no es #AMB (magic %r)' % amb[:4])
        return 1
    lab = labels_of_amb(amb)
    print('Bin origen %d: %s (label %s)' % (args.origen, orig_bin, lab))

    # 2. Comprimir LZX.
    new_lzx = os.path.join(workdir, 'destino.lzx')
    lzx_compress(orig_bin, new_lzx, workdir)
    new_data = open(new_lzx, 'rb').read()
    print('Bin comprimido: %d bytes' % len(new_data))

    # 3. Padding al to_read del guest (igual que texture_b3.py).
    # El guest lee el bin con un buffer de tamano = slot redondeado a 0x1000.
    # El runtime (desde 2026-08-18) aplica MID-INSERT VIRTUAL: si el bin
    # comprimido excede el to_read del slot, la tabla AFS virtual hace crecer la
    # entrada in-place y desplaza las posteriores (como un AFS reconstruido).
    # Por eso un bin puede superar el to_read (p.ej. Goten 107006 > Krillin
    # 106496): se paddea al to_read VIRTUAL = ceil(bin/0x1000)*0x1000.
    entries = read_afs_index(args.afs)
    dest_sz = entries[args.dest][1]
    to_read = ((dest_sz + 0xFFF) & ~0xFFF)  # ceil(slot / 0x1000) * 0x1000
    to_read_virtual = ((len(new_data) + 0xFFF) & ~0xFFF)  # ceil(bin / 0x1000) * 0x1000
    if len(new_data) > to_read:
        print('AVISO: bin comprimido %d > to_read %d del slot %d.' % (
            len(new_data), to_read, args.dest))
        print('  El runtime (mid-insert virtual) autoriza bins mayores:')
        print('  la entrada crece in-place y las posteriores se desplazan.')
        print('  Padding al to_read virtual %d.' % to_read_virtual)
        to_read = to_read_virtual
    if len(new_data) < to_read:
        new_data = new_data + b'\x00' * (to_read - len(new_data))
    print('Override: entry %d slot=%d to_read=%d (bin comprimido -> %d)'
          % (args.dest, dest_sz, to_read, len(new_data)))

    # 5. Generar el mod como OVERRIDE POR ENTRADA (bajo peso).
    # Estructura: mods/<mod>/us/<afs_filename>/<entry_index>/geom.bin
    mod_name = args.mod or 'swap_%d_on_%d' % (args.origen, args.dest)
    if args.out:
        mods_root = args.out
    else:
        # En el repo de desarrollo los mods van al build; en el paquete de
        # release, junto al exe (donde el runtime los sirve: exe_dir/mods).
        dev_mods = os.path.join(ROOT, 'out', 'build', 'win-amd64-release', 'mods')
        mods_root = dev_mods if os.path.isdir(os.path.dirname(dev_mods)) else os.path.join(ROOT, 'mods')
    os.makedirs(mods_root, exist_ok=True)
    afs_name = os.path.basename(args.afs)  # p.ej. data_cmn.afs
    # Migracion: si el mod tenia un AFS completo viejo (ARCHIVO), borrarlo
    # para poder crear el arbol de override por entrada (~100KB vs ~280MB).
    old_afs_file = os.path.join(mods_root, mod_name, 'us', afs_name)
    if os.path.isfile(old_afs_file):
        os.remove(old_afs_file)
        print('Migracion: eliminado AFS completo viejo (%s)' % old_afs_file)
    entry_dir = os.path.join(mods_root, mod_name, 'us', afs_name, str(args.dest))
    os.makedirs(entry_dir, exist_ok=True)
    geom_path = os.path.join(entry_dir, 'geom.bin')
    with open(geom_path, 'wb') as f:
        f.write(new_data)

    # manifest.txt (formato del launcher).
    manifest = os.path.join(mods_root, mod_name, 'manifest.txt')
    with open(manifest, 'w', encoding='utf-8') as f:
        f.write('name=%s\n' % mod_name)
        f.write('description=Swap nativo B3: bin %d -> slot %d\n' % (args.origen, args.dest))
        f.write('type=swap_b3\n')
        f.write('source=%d\n' % args.origen)
        f.write('target=%d\n' % args.dest)
    print('Mod generado (override por entrada, bajo peso): %s' % geom_path)
    print('DONE')
    return 0


if __name__ == '__main__':
    sys.exit(main())