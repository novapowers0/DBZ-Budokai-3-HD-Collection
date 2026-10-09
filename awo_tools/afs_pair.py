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

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
def _ps2_source(pattern, default):
    """Juego de PS2 en ps2_games de cualquier region: su carpeta USR (disco extraido) o su ISO
    (se lee sin extraer). La carpeta tiene prioridad."""
    import glob  # noqa: PLC0415
    hits = sorted(glob.glob(os.path.join(ROOT, "ps2_games", pattern)), key=lambda p: not os.path.isdir(p))
    for p in hits:
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
CACHE = os.path.join(tempfile.gettempdir(), "afs_pair_cache")
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


def ps2(n, afs="data_cmn.afs", game=PS2_GH):
    return entry(os.path.join(game, afs), n)


def iw(n, afs="DATA_CMN.AFS"):
    return entry(os.path.join(PS2_IW, afs), n)


def hd(n, afs="data_cmn.afs", region=HD_US):
    return decompress(entry(os.path.join(region, afs), n), "%s_%d" % (afs, n))


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
