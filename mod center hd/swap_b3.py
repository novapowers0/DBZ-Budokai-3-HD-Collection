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
  - El bin comprimido puede superar el to_read del slot: el runtime aplica
    MID-INSERT VIRTUAL (la entrada crece in-place y las posteriores se desplazan
    en memoria). El script paddea al to_read virtual = ceil(bin/0x1000)*0x1000.
    Históricamente el guest lee con to_read = ceil(slot/0x1000)*0x1000; si el bin
    es mas corto se paddea con ceros (igual que texture_b3.py).

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

# ------------------------------------------------ data_cmn.afs del Budokai 3 HD (Xbox 360)
# Antes se cogia el primer us/data_cmn.afs que existiera junto al Kit, fuera cual fuera: un jugador
# con otro data_cmn.afs (Infinite World: 1136 entradas; Budokai 1; uno cortado) veia
# "IndexError: list index out of range" en la vista previa. El de PS2 (Budokai 3 GH) tambien tiene
# 3990 entradas, pero sin LZX (#AMB de PS2): se distingue por la entrada 0.
CMN_ENTRIES = 3990          # US y EU: las mismas 3990 entradas
LZX_MAGIC = b'\x0f\xf5\x12\xee'
_LAYOUTS = ('us', os.path.join('DBZ3', 'us'), os.path.join('assets', 'us'), os.path.join('assets', 'DBZ3', 'us'))
B3_AFS_NAMES = ('data_cmn.afs', 'data_usi.afs', 'data_eng.afs', 'data_spn.afs', 'data_fra.afs', 'data_ger.afs',
                'data_ita.afs', 'data_yah.afs', 'lang_usa.afs', 'lang_jpn.afs', 'adx_usa.afs', 'adx_jpn.afs')


def cmn_problem(path):
    """None si `path` es el data_cmn.afs del Budokai 3 HD; si no, (clave del motivo, detalle)."""
    try:
        with open(path, 'rb') as f:
            head = f.read(16)
            if head[:3] != b'AFS' or len(head) < 16:
                return 'not_afs', path
            count = struct.unpack('<I', head[4:8])[0]
            if count < CMN_ENTRIES:
                return 'few', '%s (%d / %d)' % (path, count, CMN_ENTRIES)
            addr = struct.unpack('<I', head[8:12])[0]
            f.seek(addr)
            first = f.read(4)
    except OSError:
        return 'missing', path
    if first != LZX_MAGIC:
        return 'ps2', path          # entradas sin comprimir: data_cmn de PS2 / otro juego
    return None


def _launcher_cfg(roots):
    """(carpeta de datos elegida en el launcher, region) de dbz3_user.toml junto al exe."""
    for r in roots:
        try:
            with open(os.path.join(r, 'dbz3_user.toml'), encoding='utf-8', errors='replace') as fh:
                text = fh.read()
        except OSError:
            continue
        import re  # noqa: PLC0415
        # "..." (barras dobladas) o '...' (literal: asi lo guarda el launcher)
        m = re.search(r'''(?m)^\s*dbz3_game_dir\s*=\s*(["'])(.*)\1\s*$''', text)
        reg = re.search(r'''(?m)^\s*dbz3_region\s*=\s*["'](\w+)["']''', text)
        game = '' if not m else m.group(2).replace('\\\\', '\\') if m.group(1) == '"' else m.group(2)
        return game, (reg.group(1) if reg else 'us')
    return '', 'us'


def find_us(prefer=None, roots=()):
    """Carpeta us/ (o eu/) con el data_cmn.afs del Budokai 3 HD, o None. Orden: `prefer`, la carpeta
    de datos del launcher (dbz3_game_dir), `roots` y la carpeta del Kit, subiendo hasta 4 niveles;
    en cada una us, DBZ3/us (volcado del disco), assets/us, assets/DBZ3/us y sus eu."""
    exe_dirs = [r for r in roots if r] + [ROOT, os.path.join(ROOT, 'out', 'build', 'win-amd64-release')]
    # si la carpeta del juego no deja escribir, el launcher guarda dbz3_user.toml en Documentos\dbz3 (UserSettingsPath)
    # (UserDataRoot: <juego>\user_data\dbz3 o la carpeta del usuario\dbz3)
    home = os.path.expanduser('~')
    game_dir, region = _launcher_cfg(exe_dirs + [os.path.join(r, 'user_data', 'dbz3') for r in exe_dirs] +
                                     [os.path.join(home, 'Documents', 'dbz3'), os.path.join(home, 'dbz3')])
    eu = [l.replace('us', 'eu') for l in _LAYOUTS]
    layouts = eu + list(_LAYOUTS) if region == 'eu' else list(_LAYOUTS) + eu
    seen = []
    if prefer:
        seen.append(prefer)
    for r in [game_dir] + exe_dirs:
        probe = r
        for _ in range(4):
            if not probe:
                break
            seen += [os.path.join(probe, l) for l in layouts]
            parent = os.path.dirname(probe)
            if parent == probe:
                break
            probe = parent
    for d in seen:
        if cmn_problem(os.path.join(d, 'data_cmn.afs')) is None:
            return os.path.abspath(d)
    return None


_MSG = {
    'es': ('No encuentro el data_cmn.afs del Budokai 3 HD de Xbox 360 (%s). Elige la carpeta del juego en el '
           'launcher o en el Mod Kit («Comprobar instalación»): la que tiene us\\data_cmn.afs (o DBZ3\\us). '
           'No vale el de Budokai 1, Infinite World ni PS2, y desde una .iso no se puede: extrae el disco a una '
           'carpeta.'),
    'en': ('The Budokai 3 HD (Xbox 360) data_cmn.afs was not found (%s). Choose the game folder in the launcher '
           'or in the Mod Kit ("Check installation"): the one with us\\data_cmn.afs (or DBZ3\\us). The one from '
           'Budokai 1, Infinite World or PS2 does not work, and an .iso cannot be read: extract the disc to a '
           'folder.'),
    'fr': ('Le data_cmn.afs de Budokai 3 HD (Xbox 360) est introuvable (%s). Choisissez le dossier du jeu dans le '
           'lanceur ou dans le Mod Kit (« Vérifier l\'installation ») : celui qui contient us\\data_cmn.afs (ou '
           'DBZ3\\us). Celui de Budokai 1, Infinite World ou PS2 ne convient pas, et une .iso ne peut pas être '
           'lue : extrayez le disque dans un dossier.'),
    'de': ('Die data_cmn.afs von Budokai 3 HD (Xbox 360) wurde nicht gefunden (%s). Wähle den Spielordner im '
           'Launcher oder im Mod Kit („Installation prüfen“): den mit us\\data_cmn.afs (oder DBZ3\\us). Die von '
           'Budokai 1, Infinite World oder PS2 passt nicht, und eine .iso kann nicht gelesen werden: entpacke die '
           'Disc in einen Ordner.'),
    'it': ('Non trovo il data_cmn.afs di Budokai 3 HD (Xbox 360) (%s). Scegli la cartella del gioco nel launcher '
           'o nel Mod Kit («Controlla installazione»): quella con us\\data_cmn.afs (o DBZ3\\us). Quello di '
           'Budokai 1, Infinite World o PS2 non va bene e una .iso non si può leggere: estrai il disco in una '
           'cartella.'),
    'pt': ('Não encontro o data_cmn.afs do Budokai 3 HD de Xbox 360 (%s). Escolha a pasta do jogo no launcher ou '
           'no Mod Kit ("Verificar instalação"): a que tem us\\data_cmn.afs (ou DBZ3\\us). O de Budokai 1, '
           'Infinite World ou PS2 não serve, e uma .iso não pode ser lida: extraia o disco para uma pasta.'),
}
_WHY = {'missing': {'es': 'no existe', 'en': 'missing', 'fr': 'absent', 'de': 'fehlt', 'it': 'assente',
                    'pt': 'não existe'},
        'not_afs': {'es': 'no es un AFS', 'en': 'not an AFS', 'fr': 'pas un AFS', 'de': 'kein AFS',
                    'it': 'non è un AFS', 'pt': 'não é um AFS'},
        'few': {'es': 'faltan entradas', 'en': 'too few entries', 'fr': 'entrées manquantes',
                'de': 'zu wenige Einträge', 'it': 'mancano voci', 'pt': 'faltam entradas'},
        'ps2': {'es': 'es de PS2 u otro juego', 'en': 'it is from PS2 or another game', 'fr': 'PS2 ou autre jeu',
                'de': 'PS2 oder anderes Spiel', 'it': 'è di PS2 o di un altro gioco',
                'pt': 'é de PS2 ou de outro jogo'}}


def ui_lang():
    """Idioma de los avisos: DBZ3_LANG (lo pone el Mod Kit), dbz3_language del launcher
    (1 en, 3 de, 4 fr, 5 es, 6 it), el de Windows; si no, ingles."""
    lang = (os.environ.get('DBZ3_LANG') or '')[:2].lower()
    if lang in _MSG:
        return lang
    try:
        import re  # noqa: PLC0415
        with open(os.path.join(ROOT, 'dbz3_user.toml'), encoding='utf-8', errors='replace') as fh:
            m = re.search(r'(?m)^\s*dbz3_language\s*=\s*(\d+)', fh.read())
        if m and m.group(1) in ('1', '3', '4', '5', '6'):
            return {'1': 'en', '3': 'de', '4': 'fr', '5': 'es', '6': 'it'}[m.group(1)]
    except OSError:
        pass
    try:
        import ctypes  # noqa: PLC0415
        prim = ctypes.windll.kernel32.GetUserDefaultUILanguage() & 0x3FF
        return {0x0A: 'es', 0x0C: 'fr', 0x07: 'de', 0x10: 'it', 0x16: 'pt'}.get(prim, 'en')
    except (AttributeError, OSError):
        return 'en'


class NoGameData(SystemExit):
    """Sin data_cmn.afs valido: un mensaje claro (en el idioma del jugador), nunca un traceback."""


def no_cmn_error(path, lang=None):
    why, detail = cmn_problem(path) or ('missing', path)
    lang = lang if lang in _MSG else ui_lang()
    return NoGameData('ERROR: ' + _MSG[lang] % ('%s: %s' % (_WHY[why][lang], detail)))


def b3_afs(path):
    """`path` si su carpeta es la us/ del Budokai 3 HD; si no (otro juego, cortado, PS2), el mismo AFS en
    la carpeta buena que encuentre find_us (con un aviso). Sin ninguna buena: NoGameData. Solo mira
    los AFS de us/ del juego (data_*, lang_*, adx_*): cualquier otro AFS pasa tal cual."""
    name = os.path.basename(path).lower()
    if name not in B3_AFS_NAMES:
        return path
    d = os.path.dirname(os.path.abspath(path))
    cmn = os.path.join(d, 'data_cmn.afs')
    if cmn_problem(cmn) is None:
        return path
    good = find_us(roots=(os.path.dirname(d),))
    if good is None:
        raise no_cmn_error(cmn)
    if d not in _WARNED:
        _WARNED.add(d)
        print('aviso: %s no sirve (%s); uso %s' % (cmn, cmn_problem(cmn)[0], good), flush=True)
    return os.path.join(good, os.path.basename(path))


_WARNED = set()


# AFS del B3 (modelos): el data_cmn.afs bueno que encuentre find_us (junto al exe en la release,
# us/ del proyecto en desarrollo, la carpeta del launcher, DBZ3/us de un volcado del disco). Si no
# hay ninguno, la ruta de siempre (los que la usen sin comprobar daran "no existe").
DEFAULT_AFS = os.path.join(find_us() or _first_existing(
    os.path.join(ROOT, 'assets', 'us'), os.path.join(ROOT, 'us'),
    os.path.join(ROOT, 'assets', 'eu'), os.path.join(ROOT, 'eu')), 'data_cmn.afs')


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


def load_catalog():
    """Devuelve [(bin:int, nombre, label, variante, jugable)] del catalogo."""
    cat = os.path.join(HERE, 'catalog_b3.cat')
    out = []
    if not os.path.exists(cat):
        return out
    for line in open(cat, encoding='utf-8', errors='replace'):
        line = line.rstrip('\n')
        if not line.strip() or line.startswith('#'):
            continue
        p = line.split('|')
        if len(p) < 3 or not p[0].strip().isdigit():
            continue
        out.append((int(p[0]), p[1], p[2], p[3] if len(p) > 3 else '',
                    p[4] if len(p) > 4 else ''))
    return out


def resolve_bin(spec, cat):
    """Acepta '147', 'Cell' o 'Cell:Forma 2'. Devuelve (bin, descripcion)."""
    if spec is None:
        return None, None
    s = str(spec).strip()
    if s.isdigit():
        n = int(s)
        for (b, nom, lab, var, jug) in cat:
            if b == n:
                return n, '%s %s' % (nom, var)
        return n, None
    # por nombre / 'Nombre:Variante' (case-insensitive)
    if ':' in s:
        name, var = [x.strip().lower() for x in s.split(':', 1)]
    else:
        name, var = s.lower(), None
    hits = [c for c in cat
            if name in c[1].lower() and (var is None or var in c[3].lower())]
    if var is not None:
        exact = [c for c in hits if c[3].strip().lower() == var]
        if len(exact) == 1:
            hits = exact
    if len(hits) == 1:
        b, nom, lab, vv, jug = hits[0]
        return b, '%s %s' % (nom, vv)
    if not hits:
        raise SystemExit("ERROR: no hay bin que coincida con '%s'" % s)
    print("AMBIGUO '%s':" % s)
    for (b, nom, lab, vv, jug) in hits[:30]:
        print("   %-5d %-16s %s" % (b, nom, vv))
    raise SystemExit("   usa el bin explicito o 'Nombre:Variante'")


def main():
    ap = argparse.ArgumentParser(description='Swap nativo B3->B3')
    ap.add_argument('--origen', help='bin origen (numero, nombre o Nombre:Variante)')
    ap.add_argument('--dest', help='slot destino (numero, nombre o Nombre:Variante)')
    ap.add_argument('--mod', default=None)
    ap.add_argument('--afs', default=DEFAULT_AFS)
    ap.add_argument('--out', default=None, help='raiz de mods (default <root>/out/.../mods)')
    ap.add_argument('--list', action='store_true', help='lista el catalogo de bins')
    ap.add_argument('--verify', action='store_true',
                    help='descomprime el override y comprueba MD5 vs bin origen')
    args = ap.parse_args()

    if args.list:
        cat = os.path.join(HERE, 'catalog_b3.cat')
        if not os.path.exists(cat):
            print('ERROR: no se encontro %s' % cat)
            return 1
        print('%-6s %-16s %-10s %s' % ('bin', 'nombre', 'label', 'variante'))
        for (b, nom, lab, var, jug) in load_catalog():
            print('%-6d %-16s %-10s %s' % (b, nom, lab, var))
        return 0

    args.afs = b3_afs(args.afs)     # otro data_cmn.afs (IW, B1, PS2, cortado): el bueno o un error claro
    if not os.path.exists(XBCOMPRESS):
        print('ERROR: no se encontro xbcompress en %s' % TOOLS_DIR)
        return 1
    if args.origen is None or args.dest is None:
        ap.print_help()
        return 1

    cat = load_catalog()
    origen, odesc = resolve_bin(args.origen, cat)
    dest, ddesc = resolve_bin(args.dest, cat)
    print('Origen: bin %s%s | Destino: slot %s%s' % (
        origen, ' (%s)' % odesc if odesc else '',
        dest, ' (%s)' % ddesc if ddesc else ''))

    # Carpeta de trabajo (no depender del TEMP del entorno, que puede estar
    # invalido en el proceso del launcher). En el repo de desarrollo se usa la
    # fija del build; en el paquete de release se usa el TEMP corregido.
    workdir = os.path.join(ROOT, 'out', 'build', 'win-amd64-release', '.swap_work')
    if not os.path.isdir(workdir):
        workdir = os.path.join(tempfile.gettempdir(), 'dbz3_swap_work')
    os.makedirs(workdir, exist_ok=True)

    # 1. Extraer el bin origen.
    print('Extraer bin origen %d...' % origen)
    orig_data = extract_afs_entry(args.afs, origen)
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
    print('Bin origen %d: %s (label %s)' % (origen, orig_bin, lab))

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
    dest_sz = entries[dest][1]
    to_read = ((dest_sz + 0xFFF) & ~0xFFF)  # ceil(slot / 0x1000) * 0x1000
    to_read_virtual = ((len(new_data) + 0xFFF) & ~0xFFF)  # ceil(bin / 0x1000) * 0x1000
    if len(new_data) > to_read:
        print('AVISO: bin comprimido %d > to_read %d del slot %d.' % (
            len(new_data), to_read, dest))
        print('  El runtime (mid-insert virtual) autoriza bins mayores:')
        print('  la entrada crece in-place y las posteriores se desplazan.')
        print('  Padding al to_read virtual %d.' % to_read_virtual)
        to_read = to_read_virtual
    if len(new_data) < to_read:
        new_data = new_data + b'\x00' * (to_read - len(new_data))
    print('Override: entry %d slot=%d to_read=%d (bin comprimido -> %d)'
          % (dest, dest_sz, to_read, len(new_data)))

    # 5. Generar el mod como OVERRIDE POR ENTRADA (bajo peso).
    # Estructura: mods/<mod>/us/<afs_filename>/<entry_index>/geom.bin
    mod_name = args.mod or 'swap_%d_on_%d' % (origen, dest)
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
    entry_dir = os.path.join(mods_root, mod_name, 'us', afs_name, str(dest))
    os.makedirs(entry_dir, exist_ok=True)
    geom_path = os.path.join(entry_dir, 'geom.bin')
    with open(geom_path, 'wb') as f:
        f.write(new_data)

    # manifest.txt (formato del launcher). Usa los nombres del catalogo (ASCII)
    # para que el mod se vea legible en la pestana Mods.
    manifest = os.path.join(mods_root, mod_name, 'manifest.txt')
    src_name = (odesc or 'bin %d' % origen).strip()
    dst_name = (ddesc or 'slot %d' % dest).strip()
    with open(manifest, 'w', encoding='utf-8') as f:
        f.write('name=%s en %s\n' % (src_name, dst_name))
        f.write('description=Swap nativo B3 HD: %s (bin %d) en el slot de %s (%d)\n'
                % (src_name, origen, dst_name, dest))
        f.write('type=swap_b3\n')
        f.write('source=%s (bin %d)\n' % (src_name, origen))
        f.write('target=%s (slot %d)\n' % (dst_name, dest))
    print('Mod generado (override por entrada, bajo peso): %s' % geom_path)

    # 6. Verificacion (opcional): descomprimir el override y comparar con el bin.
    if args.verify:
        chk = os.path.join(workdir, 'verify.bin')
        try:
            lzx_decompress(geom_path, chk, workdir)
        except RuntimeError as e:
            print('VERIFY: FALLO al descomprimir el override: %s' % e)
            return 1
        a = open(orig_bin, 'rb').read()
        bb = open(chk, 'rb').read()
        if a == bb:
            print('VERIFY: OK (override == bin origen, %d bytes)' % len(a))
        else:
            print('VERIFY: DIFERENTE (origen %d, override %d)' % (len(a), len(bb)))
            return 1
    print('DONE')
    return 0


def selftest():
    """Deteccion del data_cmn.afs: IW/cortado/PS2 rechazados, DBZ3/us de un volcado y la carpeta del
    launcher encontrados, b3_afs redirige, mensaje claro en 6 idiomas. python swap_b3.py --selftest"""
    global ROOT
    import shutil  # noqa: PLC0415
    tmp = tempfile.mkdtemp(prefix='swapb3_test_')

    def afs(d, n, first):
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, 'data_cmn.afs'), 'wb') as f:
            f.write(b'AFS\0' + struct.pack('<II', n, 12 + 8 * n) + b'\0' * (8 * n) + first)
    old_root = ROOT
    try:
        kit = os.path.join(tmp, 'kit')
        ROOT = kit                                   # sin el us/ del proyecto de por medio
        afs(os.path.join(kit, 'us'), 1136, LZX_MAGIC)                     # Infinite World / cortado
        afs(os.path.join(tmp, 'ps2', 'us'), CMN_ENTRIES, b'#AMB')         # PS2: sin LZX
        assert cmn_problem(os.path.join(kit, 'us', 'data_cmn.afs'))[0] == 'few'
        assert cmn_problem(os.path.join(tmp, 'ps2', 'us', 'data_cmn.afs'))[0] == 'ps2'
        assert cmn_problem(os.path.join(tmp, 'nada.afs'))[0] == 'missing'
        assert find_us() is None
        err = no_cmn_error(os.path.join(kit, 'us', 'data_cmn.afs'), 'en')
        assert isinstance(err, SystemExit) and '1136 / 3990' in str(err), err
        assert all(no_cmn_error(os.path.join(kit, 'us', 'data_cmn.afs'), k) for k in _MSG) and len(_MSG) == 6
        try:
            b3_afs(os.path.join(kit, 'us', 'data_usi.afs'))
            raise AssertionError('b3_afs sin datos buenos no fallo')
        except NoGameData:
            pass
        assert b3_afs(os.path.join(tmp, 'otro.afs')) == os.path.join(tmp, 'otro.afs')   # no es del juego
        afs(os.path.join(kit, 'DBZ3', 'us'), CMN_ENTRIES, LZX_MAGIC)     # volcado retail: DBZ3/us
        assert find_us() == os.path.join(kit, 'DBZ3', 'us')
        assert b3_afs(os.path.join(kit, 'us', 'data_usi.afs')) == os.path.join(kit, 'DBZ3', 'us', 'data_usi.afs')
        game = os.path.join(tmp, 'juego')                                # carpeta elegida en el launcher
        afs(os.path.join(game, 'eu'), CMN_ENTRIES, LZX_MAGIC)
        with open(os.path.join(kit, 'dbz3_user.toml'), 'w', encoding='utf-8') as fh:
            fh.write('dbz3_region = "eu"\ndbz3_game_dir = "%s"\n' % game.replace('\\', '\\\\'))
        assert find_us() == os.path.join(game, 'eu'), find_us()
        with open(os.path.join(kit, 'dbz3_user.toml'), 'w', encoding='utf-8') as fh:
            fh.write("dbz3_game_dir = '%s'\n" % game)                    # literal, como el launcher
        assert find_us() == os.path.join(game, 'eu'), find_us()
        assert find_us(prefer=os.path.join(kit, 'DBZ3', 'us')) == os.path.join(kit, 'DBZ3', 'us')
    finally:
        ROOT = old_root
        shutil.rmtree(tmp, ignore_errors=True)
    print('swap_b3 selftest: OK')
    return 0


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        sys.exit(selftest())
    sys.exit(main())