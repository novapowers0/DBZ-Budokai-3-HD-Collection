"""swap_matrix.py - Swap-matrix generica (A1): mueve CUALQUIER blob de recurso
entre slots AFS y entre regiones, sin conocer su formato.

Generaliza swap_b3.py (que solo movia bins #AMB de modelo) a todos los tipos de
recurso: modelo, moveset (#ACM), retrato (#AZT), textura, aura, CAM/LIPS, voz...
El blob origen se extrae del AFS (o se da un bin ya construido por el pipeline
de port), se comprime LZX /N:2048, se paddea al to_read (o to_read_virtual con
mid-insert) y se instala como override por entrada en mods/<mod>/<us|eu>/.

Uso:
  python swap_matrix.py --afs us\\data_cmn.afs --from 3934 --to 3930 \\
      --type portrait --mod nappa_portrait_on_krillin
  python swap_matrix.py --afs us\\data_cmn.afs --from 332 --to 400 \\
      --type moveset --mod krillin_acm_on_tenshinhan
  python swap_matrix.py --bin <bins/nuevo.bin> --afs us\\data_cmn.afs \\
      --to 327 --type model --mod tien_on_krillin
  python swap_matrix.py --afs us\\data_cmn.afs --describe 332   # que hay en 332
  python swap_matrix.py --afs eu\\data_cmn.afs --from 327 --to 327 \\
      --type model --region eu --mod control_eu   # cruce de region

Tipos y magic esperado (--type):
  model    -> #AWO/#AMB   | moveset -> #ACM       | portrait -> #AZT
  texture  -> #AZT        | generic -> cualquiera
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))

LZX_MAGIC = b"\x0f\xf5\x12\xee"

TYPES = {
    "model": ("#AWO", "geom.bin"),
    "moveset": ("#ACM", "anm.bin"),
    "portrait": ("#AZT", "tex.bin"),
    "texture": ("#AZT", "tex.bin"),
    "aura": (None, "aura.bin"),
    "generic": (None, "data.bin"),
}


def _first_existing(*paths):
    for p in paths:
        if os.path.exists(p):
            return p
    return paths[0]


TOOLS_DIR = _first_existing(
    os.path.join(HERE, "tools"),
    os.path.join(HERE, "..", "mod center",
                 "Xbox 360 Compression - Decompression tool "
                 "from the XBOX Development Kit"),
    os.path.join(HERE, "..", "tools"),
)
XBCOMPRESS = os.path.join(TOOLS_DIR, "xbcompress.exe")
XBDECOMPRESS = os.path.join(TOOLS_DIR, "xbdecompress.exe")

MAGICS = [b"#AWO", b"#AWG", b"#AZT", b"#ACM", b"#AWM", b"#AMO0", b"#AMB",
          b"#SPX", b"#ACC", b"#AMT"]


def _clean_env(workdir):
    env = dict(os.environ)
    env["TEMP"] = workdir
    env["TMP"] = workdir
    return env


def lzx_compress(src, dst, workdir=None):
    if os.path.exists(dst):
        os.remove(dst)
    env = _clean_env(workdir) if workdir else None
    r = subprocess.run([XBCOMPRESS, "/N:2048", src, dst], capture_output=True,
                       text=True, env=env)
    if r.returncode != 0:
        raise RuntimeError("xbcompress fallo: %s" % (r.stdout + r.stderr))
    return os.path.getsize(dst)


def lzx_decompress(src, dst, workdir=None):
    if os.path.exists(dst):
        os.remove(dst)
    env = _clean_env(workdir) if workdir else None
    r = subprocess.run([XBDECOMPRESS, src, dst], capture_output=True, text=True,
                       env=env)
    if r.returncode != 0:
        raise RuntimeError("xbdecompress fallo: %s" % (r.stdout + r.stderr))
    return os.path.getsize(dst)


def read_afs_index(afs_path):
    with open(afs_path, "rb") as f:
        head = f.read(0x10)
    if head[:3] != b"AFS":
        raise RuntimeError("no es AFS: %r" % head[:4])
    count = struct.unpack("<I", head[4:8])[0]
    entries = []
    with open(afs_path, "rb") as f:
        f.seek(8)
        for _ in range(count):
            addr, size = struct.unpack("<II", f.read(8))
            entries.append((addr, size))
    return entries


def extract_entry(afs_path, idx):
    entries = read_afs_index(afs_path)
    addr, size = entries[idx]
    with open(afs_path, "rb") as f:
        f.seek(addr)
        return f.read(size)


def describe_bin(b):
    counts = {}
    for m in MAGICS:
        n = b.count(m)
        if n:
            counts[m.decode()] = n
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--afs", default=None, help="ruta del AFS origen/destino")
    ap.add_argument("--from", dest="from_idx", type=int, default=None,
                    help="entrada AFS origen (si --bin no se usa)")
    ap.add_argument("--bin", default=None, help="bin ya construido (pipeline port)")
    ap.add_argument("--to", dest="to_idx", type=int, default=None,
                    help="entrada AFS destino (slot)")
    ap.add_argument("--type", choices=list(TYPES), default="generic")
    ap.add_argument("--region", choices=["us", "eu"], default=None,
                    help="region donde instalar (default: inferida del --afs)")
    ap.add_argument("--mod", default=None)
    ap.add_argument("--out", default=None, help="raiz de mods")
    ap.add_argument("--file", default=None, help="nombre del archivo override")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--describe", type=int, default=None,
                    help="describe la entrada indicada y sale")
    args = ap.parse_args()

    if not os.path.exists(XBCOMPRESS):
        raise SystemExit("xbcompress no encontrado en %s" % TOOLS_DIR)

    if args.describe is not None:
        if not args.afs:
            raise SystemExit("--describe requiere --afs")
        raw = extract_entry(args.afs, args.describe)
        wd = os.path.join(tempfile.gettempdir(), "swap_matrix_desc")
        os.makedirs(wd, exist_ok=True)
        if raw[:4] == LZX_MAGIC:
            lzx = os.path.join(wd, "d.lzx")
            out = os.path.join(wd, "d.bin")
            open(lzx, "wb").write(raw)
            lzx_decompress(lzx, out, wd)
            b = open(out, "rb").read()
        else:
            b = raw
        print("entrada %d: size_fisico=%d size_bin=%d" % (args.describe, len(raw), len(b)))
        print("  magics:", describe_bin(b) or "ninguno HD")
        print("  firma:", b[:8].hex())
        return 0

    if not args.afs:
        raise SystemExit("se requiere --afs (o --describe)")
    if args.to_idx is None:
        raise SystemExit("se requiere --to (entrada destino)")
    if not args.from_idx and not args.bin:
        raise SystemExit("se requiere --from (entrada) o --bin (archivo)")

    entries = read_afs_index(args.afs)
    dest_sz = entries[args.to_idx][1]

    # 1. conseguir el bin origen
    workdir = os.path.join(ROOT, "out", "build", "win-amd64-release", ".swap_work")
    if not os.path.isdir(workdir):
        workdir = os.path.join(tempfile.gettempdir(), "dbz3_swap_work")
    os.makedirs(workdir, exist_ok=True)

    if args.bin:
        bin_path = args.bin
    else:
        raw = extract_entry(args.afs, args.from_idx)
        lzx = os.path.join(workdir, "src.lzx")
        out = os.path.join(workdir, "src.bin")
        open(lzx, "wb").write(raw)
        if raw[:4] == LZX_MAGIC:
            lzx_decompress(lzx, out, workdir)
        else:
            os.replace(lzx, out)
        bin_path = out
    amb = open(bin_path, "rb").read()

    # 2. validar tipo
    expected, default_file = TYPES[args.type]
    magics = describe_bin(amb)
    if expected and expected not in magics:
        print("AVISO: tipo %s pero el bin no contiene %s (magics=%s)" % (
            args.type, expected, magics or "ninguno"))
    print("bin origen: %d bytes magics=%s firma=%s" % (
        len(amb), magics or "ninguno", amb[:8].hex()))

    # 3. comprimir + padding (mid-insert virtual)
    new_lzx = os.path.join(workdir, "dst.lzx")
    lzx_compress(bin_path, new_lzx, workdir)
    new_data = open(new_lzx, "rb").read()
    to_read = (dest_sz + 0xFFF) & ~0xFFF
    to_read_virtual = (len(new_data) + 0xFFF) & ~0xFFF
    if len(new_data) > to_read:
        print("AVISO: bin %d > to_read %d del slot %d -> mid-insert virtual (pad a %d)" % (
            len(new_data), to_read, args.to_idx, to_read_virtual))
        to_read = to_read_virtual
    if len(new_data) < to_read:
        new_data = new_data + b"\x00" * (to_read - len(new_data))
    print("Override: entry=%d slot_size=%d to_read=%d bin_servido=%d" % (
        args.to_idx, dest_sz, to_read, len(new_data)))

    # 4. instalar override
    region = args.region
    if not region:
        afs_path = args.afs.replace("/", os.sep)
        region = "us" if os.sep + "us" + os.sep in os.sep + afs_path else "eu"
    if args.out:
        mods_root = args.out
    else:
        dev_mods = os.path.join(ROOT, "out", "build", "win-amd64-release", "mods")
        mods_root = dev_mods if os.path.isdir(os.path.dirname(dev_mods)) else os.path.join(ROOT, "mods")
    os.makedirs(mods_root, exist_ok=True)
    mod_name = args.mod or "swap_%s_on_%d" % (
        os.path.basename(bin_path).split(".")[0] if args.bin else args.from_idx,
        args.to_idx)
    afs_name = os.path.basename(args.afs)
    entry_dir = os.path.join(mods_root, mod_name, region, afs_name, str(args.to_idx))
    file_name = args.file or default_file
    geom_path = os.path.join(entry_dir, file_name)
    print("Instalar: %s" % geom_path)
    if not args.dry_run:
        os.makedirs(entry_dir, exist_ok=True)
        with open(geom_path, "wb") as f:
            f.write(new_data)
        with open(os.path.join(mods_root, mod_name, "manifest.txt"), "w",
                  encoding="utf-8") as f:
            f.write("name=%s\n" % mod_name)
            f.write("description=swap_matrix: tipo=%s entry=%d\n" % (args.type, args.to_idx))
            f.write("type=swap_matrix\n")
            f.write("source=%s\n" % (args.bin or args.from_idx))
            f.write("target=%d\n" % args.to_idx)
            f.write("region=%s\n" % region)
    print("DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())