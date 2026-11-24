"""Compara mods/_roster de un ZIP canonico con copias de Windows y Linux."""

import argparse
from contextlib import ExitStack
import hashlib
from io import BytesIO
from pathlib import Path
import re
import stat
import sys
import tomllib
from zipfile import BadZipFile, ZipFile

PREFIX = "mods/_roster/"
REQUIRED = {"roster.toml", "manifest.txt", "build.hash"}
CHUNK = 1024 * 1024
RESERVED = re.compile(r"(?:con|prn|aux|nul|conin\$|conout\$|(?:com|lpt)[1-9¹²³])", re.I)


def portable_name(name):
    parts = name.split("/")
    if any(not part or part[-1] in " ." or RESERVED.fullmatch(part.split(".", 1)[0].rstrip())
           or re.search(r'[<>:"\\|?*\x00-\x1f]', part) for part in parts):
        raise ValueError(f"ruta no portable: {name!r}")


def zip_files(z):
    files = {}
    for member in z.infolist():
        path = member.orig_filename
        if path == PREFIX:
            if not member.is_dir():
                raise ValueError("raiz _roster no es un directorio ZIP")
            continue
        if not path.startswith(PREFIX):
            continue
        name = path[len(PREFIX):].removesuffix("/")
        portable_name(name)
        kind = stat.S_IFMT(member.external_attr >> 16)
        if kind not in (0, stat.S_IFDIR if member.is_dir() else stat.S_IFREG):
            raise ValueError(f"tipo no permitido en ZIP: {name}")
        if name.casefold() == ".disabled":
            raise ValueError("_roster desactivado")
        if member.is_dir():
            continue
        if name in files:
            raise ValueError(f"entrada ZIP duplicada: {name}")
        files[name] = member
    return files


def tree_files(root):
    def walk(folder):
        for path in folder.iterdir():
            name = path.relative_to(root).as_posix()
            portable_name(name)
            st = path.lstat()
            if (getattr(st, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
                    or stat.S_ISLNK(st.st_mode)):
                raise ValueError(f"enlace no permitido: {path}")
            if name.casefold() == ".disabled":
                raise ValueError(f"_roster desactivado: {root}")
            if stat.S_ISDIR(st.st_mode):
                yield from walk(path)
            elif stat.S_ISREG(st.st_mode):
                yield name, path
            else:
                raise ValueError(f"tipo no permitido: {path}")

    st = root.lstat()
    if not stat.S_ISDIR(st.st_mode) or (getattr(st, "st_file_attributes", 0)
                                       & stat.FILE_ATTRIBUTE_REPARSE_POINT):
        raise ValueError(f"se requiere un directorio _roster real: {root}")
    return dict(walk(root))


def validate(files, reader):
    missing = REQUIRED - files.keys()
    if missing:
        raise ValueError(f"faltan artefactos base: {', '.join(sorted(missing))}")
    names = {}
    for name in files:
        prefix = ""
        for part in name.split("/"):
            prefix += "/" + part
            other = names.setdefault(prefix.casefold(), prefix)
            if other != prefix:
                raise ValueError(f"colision Windows: {other} / {prefix}")
            if prefix[1:] in files and prefix[1:] != name:
                raise ValueError(f"archivo usado como directorio: {prefix[1:]}")
    if not tomllib.loads(reader(files["roster.toml"]).decode("utf-8")):
        raise ValueError("roster.toml vacio")
    if not reader(files["manifest.txt"]).strip():
        raise ValueError("manifest.txt vacio")
    if not re.fullmatch(rb"[0-9a-f]{40}", reader(files["build.hash"]).strip()):
        raise ValueError("build.hash invalido")


def compare(z, windows, linux):
    reference = zip_files(z)
    validate(reference, z.read)
    for label, files in (("Windows", windows), ("Linux", linux)):
        missing = sorted(reference.keys() - files.keys())
        extra = sorted(files.keys() - reference.keys())
        if missing or extra:
            raise ValueError(f"{label}: faltan {len(missing)} {missing[:5]}; sobran {len(extra)} {extra[:5]}")

    inventory = hashlib.sha256()
    total = 0
    for name, member in sorted(reference.items()):
        digest = hashlib.sha256()
        offset = 0
        with ExitStack() as stack:
            expected = stack.enter_context(z.open(member))
            win = stack.enter_context(windows[name].open("rb"))
            lin = stack.enter_context(linux[name].open("rb"))
            while True:
                block = expected.read(CHUNK)
                for label, stream in (("Windows", win), ("Linux", lin)):
                    if stream.read(CHUNK) != block:
                        raise ValueError(f"{label}: bytes distintos en {name} (bloque desde {offset})")
                if not block:
                    break
                digest.update(block)
                offset += len(block)
        total += offset
        inventory.update(name.encode("utf-8") + b"\0" + digest.digest())
    return len(reference), total, inventory.hexdigest()


def selftest():
    base = {"roster.toml": b"[[personaje]]\nid = 22\n", "manifest.txt": b"prueba\n",
            "build.hash": b"a" * 40, "us/data_cmn.afs/42": b"\x00\x01"}

    class MemoryFile:
        def __init__(self, data):
            self.data = data

        def open(self, mode):
            return BytesIO(self.data)

    archive = BytesIO()
    with ZipFile(archive, "w") as z:
        for name, data in base.items():
            z.writestr(PREFIX + name, data)
    with ZipFile(archive) as z:
        good = {name: MemoryFile(data) for name, data in base.items()}
        compare(z, good, good)
        for target, message, modified in (
            ("Windows", "bytes distintos", dict(good, **{"us/data_cmn.afs/42": MemoryFile(b"\x00\x02")})),
            ("Linux", "bytes distintos", dict(good, **{"build.hash": MemoryFile(b"b" * 40)})),
            ("Linux", "faltan 1", {k: v for k, v in good.items() if k != "manifest.txt"}),
            ("Linux", "sobran 1", dict(good, **{".disabled": MemoryFile(b"")})),
        ):
            try:
                compare(z, modified if target == "Windows" else good,
                        modified if target == "Linux" else good)
            except ValueError as err:
                if not str(err).startswith(target) or message not in str(err):
                    raise
            else:
                raise RuntimeError(f"control negativo no detectado: {target}, {message}")
    print("OK: igualdad y cuatro controles negativos (bytes, sello, ruta ausente, extra)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--autoprueba", action="store_true")
    parser.add_argument("--pack", type=Path, help="ZIP de Personajes de referencia")
    parser.add_argument("--windows", type=Path, help="directorio mods/_roster de Windows")
    parser.add_argument("--linux", type=Path, help="directorio mods/_roster de Linux")
    args = parser.parse_args()
    if args.autoprueba:
        if args.pack or args.windows or args.linux:
            parser.error("--autoprueba no admite rutas")
        selftest()
        return 0
    if not (args.pack and args.windows and args.linux):
        parser.error("se requieren --pack, --windows y --linux")
    try:
        if args.windows.samefile(args.linux):
            raise ValueError("se requieren dos directorios distintos")
        with ZipFile(args.pack) as z:
            count, size, digest = compare(z, tree_files(args.windows), tree_files(args.linux))
    except (OSError, ValueError, RuntimeError, BadZipFile) as err:
        print(f"FAIL: {err}", file=sys.stderr)
        return 1
    print(f"OK ZIP / Windows / Linux: {count} archivos, {size} bytes; SHA256 inventario {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
