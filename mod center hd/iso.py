#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""iso.py - Lectura directa de imagenes ISO 9660 (PS2 y PSP), sin extraerlas.

Sirve para que el importador trabaje con la ISO tal cual la tiene el jugador:

    iso = Iso("DragonBall Z - Budokai (Europe).iso")
    iso.files()                     -> {"/SLES_515.05": (lba, tamano), ...}
    iso.read("/USR/DATA_CMN.AFS", offset, n)
    iso.open("/USR/DATA_CMN.AFS")   -> objeto tipo fichero (seek/read) para el lector de AFS

`python iso.py <iso>` lista su contenido.
"""
import io
import os
import struct
import sys

SECTOR = 2048


class Iso:
    def __init__(self, path):
        self.path = path
        self._f = open(path, "rb")
        pvd = self._sector(16)
        if pvd[1:6] != b"CD001":
            raise ValueError("no es una imagen ISO 9660: %s" % path)
        self.label = pvd[40:72].decode("ascii", "replace").strip()
        root = pvd[156:156 + 34]
        self._files = {}
        self._walk(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "")

    def _sector(self, lba, n=1):
        self._f.seek(lba * SECTOR)
        return self._f.read(n * SECTOR)

    def _walk(self, lba, size, prefix, depth=0):
        if depth > 16:
            return
        data = self._sector(lba, (size + SECTOR - 1) // SECTOR)
        o = 0
        while o < len(data):
            ln = data[o]
            if ln == 0:              # resto del sector vacio
                o = (o // SECTOR + 1) * SECTOR
                continue
            rec = data[o:o + ln]
            o += ln
            elba, esize = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
            flags, nlen = rec[25], rec[32]
            name = rec[33:33 + nlen]
            if name in (b"\x00", b"\x01"):
                continue
            name = name.decode("ascii", "replace").split(";")[0]
            full = prefix + "/" + name
            if flags & 2:
                self._walk(elba, esize, full, depth + 1)
            else:
                self._files[full] = (elba, esize)

    def files(self):
        return dict(self._files)

    def find(self, name):
        """Ruta de un fichero sin distinguir mayusculas; `name` puede ser solo el nombre."""
        low = name.lower().lstrip("/")
        for p in self._files:
            if p.lower().lstrip("/") == low or p.lower().rsplit("/", 1)[-1] == low:
                return p
        return None

    def find_region(self, name):
        """Como find, pero si el fichero lleva sufijo de region (data_btl_voice_us.afs) y esta
        copia es de otra region, el mismo fichero con su sufijo (_eu, _en...; nunca _jp/_cmn)."""
        import re  # noqa: PLC0415
        hit = self.find(name)
        if hit:
            return hit
        stem, ext = os.path.splitext(os.path.basename(name).lower())
        base = re.sub(r"_(us|usa)$", "", stem)
        for p in sorted(self._files):
            m = re.fullmatch(re.escape(base) + r"_([a-z]{2,3})" + re.escape(ext), p.rsplit("/", 1)[-1].lower())
            if m and m.group(1) not in ("jp", "jpn", "cmn"):
                return p
        return None

    def read(self, path, offset=0, n=None):
        lba, size = self._files[path]
        n = size - offset if n is None else min(n, size - offset)
        self._f.seek(lba * SECTOR + offset)
        return self._f.read(max(0, n))

    def open(self, path):
        return _Sub(self.path, *self._files[path])

    def boot_elf(self):
        """Ruta del ejecutable de arranque (SYSTEM.CNF BOOT2): /SLUS_218.42, /SLES_512.33...
        Asi vale cualquier region (USA / Europa) sin nombres fijos."""
        return boot_elf_name(self.read(self.find("SYSTEM.CNF") or "/SYSTEM.CNF"), self.find)


# juegos de origen en ps2_games, de cualquier region: '(USA)', '(Europe)', '(En,Fr,De,Es,It)'...
GAME_RE = {"b1": r"budokai \(", "b2": r"budokai 2", "sb1": r"shin budokai(?!.*another)", "sb2": r"another road"}
GAME_SKIP = {"b1": "shin"}


def find_game(game, root=None):
    """ISO de un juego de origen en ps2_games (o None): por patron, no por el nombre exacto."""
    import glob  # noqa: PLC0415
    import re  # noqa: PLC0415
    root = root or os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "ps2_games")
    for p in sorted(glob.glob(os.path.join(root, "*.iso"))):
        low = os.path.basename(p).lower()
        if re.search(GAME_RE[game], low) and not (GAME_SKIP.get(game) and GAME_SKIP[game] in low):
            return p
    return None


def boot_elf_name(cnf, find):
    """Nombre del ELF de un SYSTEM.CNF de PS2 (BOOT2 = cdrom0:\\SLES_512.33;1) resuelto con
    find(nombre) (Iso.find o una busqueda en carpeta)."""
    for ln in cnf.decode("latin1", "replace").splitlines():
        if ln.strip().upper().startswith("BOOT2"):
            name = ln.split("=", 1)[1].strip().replace("cdrom0:", "").strip("\\/").split(";")[0]
            return find(name)
    return None


def folder_elf(folder):
    """Igual que Iso.boot_elf para un juego extraido en una carpeta (ruta completa o None)."""
    names = {fn.lower(): os.path.join(folder, fn) for fn in os.listdir(folder)}
    cnf = names.get("system.cnf")
    if cnf:
        with open(cnf, "rb") as fh:
            hit = boot_elf_name(fh.read(), lambda n: names.get(n.lower()))
        if hit:
            return hit
    return next((p for n, p in sorted(names.items()) if n[:4] in ("slus", "sles", "sces", "scus", "slps")), None)


class _Sub(io.RawIOBase):
    """Fichero dentro de la ISO como objeto de solo lectura (seek/read/tell)."""

    def __init__(self, path, lba, size):
        self._f = open(path, "rb")
        self._base, self._size, self._pos = lba * SECTOR, size, 0

    def readable(self):
        return True

    def seekable(self):
        return True

    def seek(self, off, whence=0):
        self._pos = off if whence == 0 else (self._pos + off if whence == 1 else self._size + off)
        return self._pos

    def tell(self):
        return self._pos

    def read(self, n=-1):
        if n is None or n < 0:
            n = self._size - self._pos
        n = max(0, min(n, self._size - self._pos))
        self._f.seek(self._base + self._pos)
        b = self._f.read(n)
        self._pos += len(b)
        return b

    def close(self):
        self._f.close()
        super().close()


def prueba():
    """Region: SYSTEM.CNF de PS2 y ficheros con sufijo de region (sin ISO real)."""
    files = {"SLES_512.33": "/SLES_512.33", "SLUS_218.42": "/SLUS_218.42"}
    assert boot_elf_name(b"BOOT2 = cdrom0:\\SLES_512.33;1\r\nVER = 1.00\r\n", files.get) == "/SLES_512.33"
    assert boot_elf_name(b"BOOT2 = cdrom0:\\SLUS_218.42;1\n", files.get) == "/SLUS_218.42"
    img = Iso.__new__(Iso)
    img._files = {"/U/data_btl_voice.afs": 0, "/U/data_btl_voice_jp.afs": 0, "/U/data_btl_voice_eu.afs": 0,
                  "/U/data_btl_eu.afs": 0, "/U/data_btl_cmn.afs": 0}
    assert img.find_region("data_btl_voice_us.afs") == "/U/data_btl_voice_eu.afs"
    assert img.find_region("data_btl_us.afs") == "/U/data_btl_eu.afs"
    assert img.find_region("data_btl_voice_jp.afs") == "/U/data_btl_voice_jp.afs"
    print("iso.py: prueba OK")


if __name__ == "__main__":
    if sys.argv[1:] == ["prueba"]:
        prueba()
        sys.exit(0)
    iso = Iso(sys.argv[1])
    print(iso.label)
    for p, (lba, size) in sorted(iso.files().items()):
        print("%12d  %s" % (size, p))
