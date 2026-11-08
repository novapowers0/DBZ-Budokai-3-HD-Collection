#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""afs_pair.py - Lectura de entradas AFS de la PS2 (GH/IW, crudas LE) y de la HD 360
(LZX `0F F5 12 EE` -> xbdecompress) con cache. La HD usa la numeracion de data_cmn de
la PS2 Greatest Hits, asi que (ps2(n), hd(n)) es un par del MISMO contenido en LE/BE:
el oraculo para validar conversores PS2 -> HD.

  from afs_pair import entry, ps2, hd
  raw = ps2(333)            # bytes de la entrada 333 de la PS2 GH data_cmn.afs
  dec = hd(333)             # bytes DESCOMPRIMIDOS de la entrada 333 HD (us/)
  python afs_pair.py 333    # resumen de ambos
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
def _ruta(p):
    try:
        with open(p, encoding="utf-8-sig") as f:
            d = f.read().strip()
    except OSError:
        return None
    return d if d and os.path.exists(d) else None


def _ps2_source(pattern, default):
    """Juego de PS2 en ps2_games de cualquier region: su carpeta USR (disco extraido) o su ISO
    (se lee sin extraer). La carpeta tiene prioridad."""
    import glob  # noqa: PLC0415
    hits = sorted(glob.glob(os.path.join(ROOT, "ps2_games", pattern)), key=lambda p: not os.path.isdir(p))
    # #72: 'Elegir ISO' del launcher deja un .ruta (la ISO se queda donde estaba): se sigue a su destino
    hits = [_ruta(p) if p.lower().endswith(".ruta") else p for p in hits]
    for p in filter(None, hits):
        if os.path.isdir(p):
            usr = next((os.path.join(p, d) for d in ("USR", "usr") if os.path.isdir(os.path.join(p, d))), None)
            if usr:
                return usr
        elif p.lower().endswith(".iso"):
            return p
    return default


PS2_GH = _ps2_source("*Budokai 3*", os.path.join(ROOT, "ps2_games", "Budokai 3 Greatest Hits (USA)", "USR"))
PS2_IW = _ps2_source("*Infinite World*", os.path.join(ROOT, "ps2_games", "Infinite World (USA)", "USR"))
HD_US = os.path.join(ROOT, "us")
XBDEC = next((p for p in (
    os.path.join(ROOT, "mod center", "Xbox 360 Compression - Decompression tool from the XBOX Development Kit",
                 "xbdecompress.exe"),
    os.path.join(ROOT, "mod center hd", "tools", "xbdecompress.exe"),   # kit de modding (release)
) if os.path.exists(p)), os.path.join(ROOT, "mod center hd", "tools", "xbdecompress.exe"))
# fuera de C: si hay sitio de trabajo (mismo orden que roster_build.work_dir); si no, el TEMP de siempre
CACHE = os.path.join(next((c for c in (os.environ.get("DBZ3_TMP"), "E:/DBZ3HD_trabajo/tmp", "D:/tmp")
                          if c and os.path.isdir(c)), tempfile.gettempdir()), "afs_pair_cache")
LZX_MAGIC = b"\x0f\xf5\x12\xee"

_tables = {}


def table(path):
    """[(off, size)] de un AFS (tabla en +8: magic 'AFS\\0' + count u32 LE)."""
    if path not in _tables:
        with open(path, "rb") as f:
            hdr = f.read(8)
            assert hdr[:3] == b"AFS", path
            n = struct.unpack("<I", hdr[4:8])[0]
            _tables[path] = [struct.unpack("<II", f.read(8)) for _ in range(n)]
    return _tables[path]


_maps = {}


_isos = {}


def _iso_entry(iso_path, afs, n):
    """Entrada n de /USR/<afs> dentro de una ISO de PS2."""
    import sys  # noqa: PLC0415
    sys.path.insert(0, os.path.join(ROOT, "mod center hd"))
    import iso  # noqa: PLC0415
    key = (iso_path, afs.lower())
    if key not in _isos:
        img = iso.Iso(iso_path)
        inner = img.find("USR/" + afs) or img.find(afs)
        hdr = img.read(inner, 0, 8)
        cnt = struct.unpack("<I", hdr[4:8])[0]
        t = img.read(inner, 8, 8 * cnt)
        _isos[key] = (img, inner, [struct.unpack_from("<II", t, 8 * k) for k in range(cnt)])
    img, inner, tab = _isos[key]
    off, size = tab[n]
    return img.read(inner, off, size)


def entry(path, n):
    if os.path.isfile(os.path.dirname(path)) and os.path.dirname(path).lower().endswith(".iso"):
        return _iso_entry(os.path.dirname(path), os.path.basename(path), n)
    off, size = table(path)[n]
    if path not in _maps:
        import mmap
        f = open(path, "rb")
        _maps[path] = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    return _maps[path][off:off + size]


def decompress(raw, key):
    if raw[:4] != LZX_MAGIC:
        return raw
    os.makedirs(CACHE, exist_ok=True)
    out = os.path.join(CACHE, key + ".dec")
    if not os.path.exists(out):
        src = os.path.join(CACHE, key + ".lzx")
        with open(src, "wb") as f:
            f.write(raw)
        subprocess.run([XBDEC, src, out], capture_output=True,
                       env=dict(os.environ, MSYS_NO_PATHCONV="1"))
        os.remove(src)
    return open(out, "rb").read()


def ps2(n, afs="data_cmn.afs", game=PS2_GH, real=False):
    # #72: sin el Budokai 3 de PS2 (lo normal en casa del jugador) se reconstruye desde su HD con hdps2
    if game == PS2_GH and afs == "data_cmn.afs" and not real and not os.path.exists(os.path.join(game, afs)) \
            and not os.path.isfile(game):
        import hdps2  # noqa: PLC0415
        try:
            return hdps2.hd_a_ps2(hd(n))
        except hdps2.ps2hd.Unsupported as ex:   # solo animaciones y camaras tienen vuelta (modelos, BSP: no)
            raise SystemExit("ERROR: esta parte necesita el Budokai 3 de PS2 en ps2_games (entrada %d, %s)" % (n, ex))
    return entry(os.path.join(game, afs), n)


def iw(n, afs="DATA_CMN.AFS"):
    return entry(os.path.join(PS2_IW, afs), n)


def hd(n, afs="data_cmn.afs", region=HD_US):
    # swap_b3.b3_afs: si `region` no es la us/ del Budokai 3 HD (otro juego, cortada, o no hay us/ junto al Kit)
    # se usa la buena que encuentre (carpeta del launcher, assets/us, DBZ3/us...) o sale un error claro. Antes un
    # data_cmn.afs ajeno daba plantillas basura (Heroes sin imagen 3D) y quedaban en la cache: la clave lleva
    # ahora el fichero de origen.
    mch = os.path.join(ROOT, "mod center hd")
    if mch not in sys.path:
        sys.path.append(mch)
    import swap_b3  # noqa: PLC0415
    path = swap_b3.b3_afs(os.path.join(region, afs))
    st = os.stat(path)
    src = "%08x" % (zlib.crc32(("%s|%d|%d" % (os.path.abspath(path), st.st_size, st.st_mtime_ns)).encode()))
    return decompress(entry(path, n), "%s_%d_%s" % (afs, n, src))


def magics(b, limit=64):
    """Magics '#XXX' / 'XXX\\0' a 16 B de alineacion (vista rapida)."""
    out = []
    for o in range(0, min(len(b), 1 << 22), 16):
        m = b[o:o + 4]
        if (m[:1] == b"#" and m[1:].isalpha()) or (m[:3].isalpha() and m[3:4] in (b"\0", b"0", b"1")):
            out.append((o, m.decode("latin1")))
            if len(out) >= limit:
                break
    return out


if __name__ == "__main__":
    for a in sys.argv[1:]:
        n = int(a)
        p, h = ps2(n), hd(n)
        print("entrada %d: PS2 %d B %s | HD %d B %s" % (n, len(p), magics(p, 6), len(h), magics(h, 6)))
