"""Base comun de las herramientas del Kit (KIT-04): rutas, carpeta us/ del Budokai 3 HD, LZX e indice AFS.

Una sola copia de lo que antes repetian swap_b3, texture_b3 y swap_matrix (y el LZX de studio_core y
escenarios_convertir). Esos scripts mantienen sus nombres de siempre como alias de estos.

  RAIZ, HD                       raiz del proyecto (o de la release) y mod center hd/
  find_game(prefer)              carpeta con dbz3.exe
  find_us(prefer, roots, root)   us/ (o eu/) con el data_cmn.afs del Budokai 3 HD de Xbox 360
  lzx_compress / lzx_decompress  xbcompress /N:2048 (nunca /N:32) y xbdecompress, de fichero a fichero
  lzx_bytes(datos, comprimir)    lo mismo en memoria
  read_afs_index / extract_afs_entry / build_afs   leer y reescribir el indice AFS (tabla en offset 8)

Prueba: python "mod center hd/kit/prueba_base.py"
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

HD = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))     # mod center hd/
RAIZ = os.path.abspath(os.path.join(HD, '..'))                      # proyecto (o carpeta del juego en la release)

try:   # lectores binarios de tools/infra.py; la release no lo trae
    if os.path.join(RAIZ, 'tools') not in sys.path:
        sys.path.append(os.path.join(RAIZ, 'tools'))
    from infra import u32
except ImportError:
    def u32(b, o=0):
        return struct.unpack_from('<I', b, o)[0]


def first_existing(*paths):
    for p in paths:
        if os.path.exists(p):
            return p
    return paths[0]


# ------------------------------------------------ xbcompress / xbdecompress (XDK)
# junto al Kit/release (mod center hd/tools) o el XDK del proyecto
TOOLS_DIR = first_existing(
    os.path.join(HD, 'tools'),
    os.path.join(HD, '..', 'mod center',
                 'Xbox 360 Compression - Decompression tool '
                 'from the XBOX Development Kit'),
    os.path.join(HD, '..', 'tools'),
)
XBCOMPRESS = os.path.join(TOOLS_DIR, 'xbcompress.exe')
XBDECOMPRESS = os.path.join(TOOLS_DIR, 'xbdecompress.exe')
LZX_MAGIC = b'\x0f\xf5\x12\xee'


def clean_env(workdir):
    """TEMP/TMP a una ruta valida: el launcher hereda a veces un TEMP invalido y xbcompress/xbdecompress
    no pueden crear su propio temporal."""
    env = dict(os.environ)
    env['TEMP'] = workdir
    env['TMP'] = workdir
    return env


def _xb(args, dst, workdir, timeout, que):
    if os.path.exists(dst):
        os.remove(dst)
    r = subprocess.run(args, capture_output=True, text=True, env=clean_env(workdir) if workdir else None,
                       timeout=timeout)
    if r.returncode != 0:
        raise RuntimeError('%s fallo: %s' % (que, r.stdout + r.stderr))
    return os.path.getsize(dst)


def lzx_compress(src, dst, workdir=None, timeout=None):
    """Bins de 360: /N:2048 (con /N:32 el juego no los lee bien). Devuelve el tamano comprimido."""
    return _xb([XBCOMPRESS, '/N:2048', src, dst], dst, workdir, timeout, 'xbcompress')


def lzx_decompress(src, dst, workdir=None, timeout=None):
    return _xb([XBDECOMPRESS, src, dst], dst, workdir, timeout, 'xbdecompress')


def lzx_bytes(datos, comprimir=True, work=None, timeout=None):
    """LZX en memoria (carpeta temporal propia, dentro de `work` si se da)."""
    with tempfile.TemporaryDirectory(dir=work) as d:
        src, dst = os.path.join(d, 'a'), os.path.join(d, 'b')
        with open(src, 'wb') as fh:
            fh.write(datos)
        (lzx_compress if comprimir else lzx_decompress)(src, dst, d, timeout)
        with open(dst, 'rb') as fh:
            return fh.read()


# ------------------------------------------------ indice AFS
# El runtime (rexglue-sdk/src/filesystem/afs.cpp) lee la tabla en offset 8:
#   0: "AFS" + 1 byte | 4: count (u32) | 8: (addr u32, size u32) por entrada
# Leerla en 0x10 daba un desfase de 1 entrada (bins de la entrada equivocada y crashes al servir el override).
def read_afs_index(afs_path):
    with open(afs_path, 'rb') as f:
        head = f.read(0x10)
        if head[:3] != b'AFS':
            raise RuntimeError('no es un AFS: %r' % head[:4])
        count = u32(head, 4)
        f.seek(8)
        tabla = f.read(8 * count)
    if len(tabla) < 8 * count:
        raise struct.error('tabla AFS cortada')
    return [(u32(tabla, 8 * i), u32(tabla, 8 * i + 4)) for i in range(count)]


def extract_afs_entry(afs_path, idx):
    addr, size = read_afs_index(afs_path)[idx]
    with open(afs_path, 'rb') as f:
        f.seek(addr)
        return f.read(size)


def build_afs(afs_orig, idx, new_bin, out_path):
    """Reescribe el AFS con `new_bin` en la entrada idx (mid-insert, como awo_tools/build_afs.py):
    la entrada conserva su loc y crece en su sitio; las posteriores se desplazan (multiplo de 0x800)."""
    afs = open(afs_orig, 'rb').read()
    n = u32(afs, 4)
    base = 8
    first_data = u32(afs, base)
    old_loc, old_sz = u32(afs, base + idx * 8), u32(afs, base + idx * 8 + 4)
    delta_raw = len(new_bin) - old_sz
    delta = ((delta_raw + 0x7FF) & ~0x7FF) if delta_raw > 0 else 0
    print('AFS: %d entradas, header real=0x%X' % (n, first_data))
    print('Entrada %d: loc=0x%X size=%d -> nuevo bin %d bytes (delta %+d)' % (
        idx, old_loc, old_sz, len(new_bin), delta))
    table = bytearray()
    for i in range(n):
        loc, sz = u32(afs, base + i * 8), u32(afs, base + i * 8 + 4)
        if i == idx:
            sz = len(new_bin)
        elif i > idx and loc != 0:
            loc += delta
        table += struct.pack('<II', loc, sz)
    out = bytearray(afs[:base]) + table + afs[base + n * 8:first_data]
    assert len(out) == first_data, 'header size mismatch'
    out += afs[first_data:old_loc] + new_bin
    if delta_raw > 0:
        out += bytes(delta - delta_raw)
    out += afs[old_loc + old_sz:]
    with open(out_path, 'wb') as f:
        f.write(bytes(out))
    print('AFS reconstruido: %d bytes -> %s' % (len(out), out_path))


# ------------------------------------------------ carpeta del juego y us/ del Budokai 3 HD (Xbox 360)
# Antes valia el primer us/data_cmn.afs junto al Kit: con el de otro juego (IW: 1136 entradas; B1; uno cortado)
# la vista previa daba "IndexError". El de PS2 tambien tiene 3990 entradas, pero sin LZX: se mira la entrada 0.
CMN_ENTRIES = 3990          # US y EU: las mismas 3990 entradas
LAYOUTS = ('us', os.path.join('DBZ3', 'us'), os.path.join('assets', 'us'), os.path.join('assets', 'DBZ3', 'us'))


def find_game(prefer=None, root=None):
    """Carpeta con dbz3.exe: `prefer`, la raiz, su padre o el build de release; si no, ''."""
    if prefer and os.path.isdir(prefer):
        return os.path.abspath(prefer)
    root = root or RAIZ
    for d in (root, os.path.dirname(root), os.path.join(root, 'out', 'build', 'win-amd64-release')):
        if os.path.isfile(os.path.join(d, 'dbz3.exe')):
            return os.path.abspath(d)
    return ''


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
            f.seek(struct.unpack('<I', head[8:12])[0])
            first = f.read(4)
    except OSError:
        return 'missing', path
    if first != LZX_MAGIC:
        return 'ps2', path          # entradas sin comprimir: data_cmn de PS2 / otro juego
    return None


def launcher_cfg(roots):
    """(carpeta de datos elegida en el launcher, region) del primer dbz3_user.toml de `roots`."""
    for r in roots:
        try:
            with open(os.path.join(r, 'dbz3_user.toml'), encoding='utf-8', errors='replace') as fh:
                text = fh.read()
        except OSError:
            continue
        # "..." (barras dobladas) o '...' (literal: asi lo guarda el launcher)
        m = re.search(r'''(?m)^\s*dbz3_game_dir\s*=\s*(["'])(.*)\1\s*$''', text)
        reg = re.search(r'''(?m)^\s*dbz3_region\s*=\s*["'](\w+)["']''', text)
        game = '' if not m else m.group(2).replace('\\\\', '\\') if m.group(1) == '"' else m.group(2)
        return game, (reg.group(1) if reg else 'us')
    return '', 'us'


def find_us(prefer=None, roots=(), root=None):
    """Carpeta us/ (o eu/) con el data_cmn.afs del Budokai 3 HD, o None. Orden: `prefer`, la carpeta
    de datos del launcher (dbz3_game_dir), `roots` y la raiz (`root`, por defecto RAIZ), subiendo hasta
    4 niveles; en cada una us, DBZ3/us (volcado del disco), assets/us, assets/DBZ3/us y sus eu."""
    root = root or RAIZ
    exe_dirs = [r for r in roots if r] + [root, os.path.join(root, 'out', 'build', 'win-amd64-release')]
    # si la carpeta del juego no deja escribir, el launcher guarda dbz3_user.toml en Documentos\dbz3
    # (UserDataRoot: <juego>\user_data\dbz3 o la carpeta del usuario\dbz3)
    home = os.path.expanduser('~')
    game_dir, region = launcher_cfg(exe_dirs + [os.path.join(r, 'user_data', 'dbz3') for r in exe_dirs] +
                                    [os.path.join(home, 'Documents', 'dbz3'), os.path.join(home, 'dbz3')])
    eu = [l.replace('us', 'eu') for l in LAYOUTS]
    layouts = eu + list(LAYOUTS) if region == 'eu' else list(LAYOUTS) + eu
    seen = [prefer] if prefer else []
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
