"""acm_parse.py - RE del formato #AMB/#ACM HD (moveset y stages).

El bin ANM/moveset HD es un contenedor #AMB big-endian con 3 bloques #ACM
(equivalente renombrado del PS2: AMC/BCM+BSK/AMM, SPX, segun IW_moveset_editing_notes).
Los stages tambien llevan #ACM y #SPX. Este script:

  1. Extrae un bin del AFS y lo descomprime LZX (o lee un .bin ya descomprimido).
  2. Parsea el contenedor #AMB (magic, n_files, address list 0x10/entrada).
  3. Lista sub-files con offset/size/magic.
  4. Para cada bloque #ACM: intenta decodificar la estructura PS2 (BCM/BSK/AMM/SPX)
     en big-endian: header, contadores, tablas de offsets.

Uso:
  python awo_tools/acm_parse.py --afs us\\data_cmn.afs --idx 333
  python awo_tools/acm_parse.py --bin <descomprimido.bin>
  python awo_tools/acm_parse.py --afs us\\data_cmn.afs --idx 333 --dump-sub all --out out\\analysis\\acm
"""
import argparse
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "mod center",
                     "Xbox 360 Compression - Decompression tool from the XBOX Development Kit")
XBDECOMPRESS = os.path.join(TOOLS, "xbdecompress.exe")

BE = ">"
MAGICS = [b"#AWO", b"#AWG", b"#AZT", b"#ACM", b"#AMB", b"#SPX", b"#ACC",
          b"#AWM", b"#AMO0", b"#AMG", b"#AMT", b"#BCM", b"#BSK", b"#AMM",
          b"#AMC", b"#BFC", b"#ASE", b"#AME", b"#AST"]


def read_afs_index(afs_path):
    with open(afs_path, "rb") as f:
        head = f.read(8)
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


def decompress(raw, workdir):
    lzx = os.path.join(workdir, "src.lzx")
    out = os.path.join(workdir, "src.bin")
    with open(lzx, "wb") as f:
        f.write(raw)
    if os.path.exists(out):
        os.remove(out)
    r = subprocess.run([XBDECOMPRESS, lzx, out], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        raise RuntimeError("xbdecompress fallo: %s" % r.stderr)
    with open(out, "rb") as f:
        return f.read()


def find_magics(b, start=0, end=None):
    end = end if end is not None else len(b)
    found = []
    for m in MAGICS:
        i = b.find(m, start, end)
        if i != -1:
            found.append((m, i))
    found.sort(key=lambda x: x[1])
    return found


def parse_amb(b):
    """Contenedor big-endian generico (misma cabecera para #AMB/#CSK/#ACM):
    +0x00 magic, +0x04 header_size, +0x0C n?, +0x10 n_sub, tabla en +0x14
    (descriptor de la propia tabla) y desde +0x20 entradas [off,size,type,pad]."""
    magic = b[:4]
    hdr = struct.unpack_from(BE + "IIII", b, 0x04) if len(b) >= 0x14 else None
    n_sub = struct.unpack_from(BE + "I", b, 0x10)[0] if len(b) >= 0x14 else 0
    subs = []
    for i in range(n_sub):
        off = 0x20 + i * 0x10
        if off + 0x10 > len(b):
            break
        soff, size, typ, unk = struct.unpack_from(BE + "IIII", b, off)
        subs.append((soff, size, typ, unk))
    return magic, hdr, n_sub, subs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--afs")
    ap.add_argument("--idx", type=int)
    ap.add_argument("--bin")
    ap.add_argument("--out", default=None, help="volcar sub-files")
    ap.add_argument("--dump-sub", default=None, help="indice de sub-file a volcar o 'all'")
    args = ap.parse_args()

    if args.bin:
        with open(args.bin, "rb") as f:
            b = f.read()
        print(f"bin: {args.bin} ({len(b)} B)")
    else:
        raw = extract_entry(args.afs, args.idx)
        print(f"entry {args.idx}: {len(raw)} B comprimido")
        workdir = os.path.join(ROOT, "out", "analysis", "acm", ".work")
        os.makedirs(workdir, exist_ok=True)
        if raw[:4] == b"\x0f\xf5\x12\xee":
            b = decompress(raw, workdir)
        else:
            b = raw
        print(f"descomprimido: {len(b)} B")
        outdir = os.path.join(ROOT, "out", "analysis", "acm")
        os.makedirs(outdir, exist_ok=True)
        with open(os.path.join(outdir, f"entry_{args.idx}.bin"), "wb") as f:
            f.write(b)

    print("magics globales:", [(m.decode(), i) for m, i in find_magics(b)[:20]])
    print("primeros 16 B:", b[:16].hex())

    if b[:4] == b"#AMB":
        magic, hdr, n, subs = parse_amb(b)
        print(f"\n#AMB container: +10 n_sub={n} hdr={hdr}")
        for i, (soff, size, typ, unk) in enumerate(subs):
            sub = b[soff:soff + min(size, 8)]
            print(f"  sub {i:2d}: off={soff:#x} size={size} type={typ:#x} magic={sub[:4]}")
            if args.out and args.dump_sub in ("all", str(i)):
                outdir = args.out or os.path.join(ROOT, "out", "analysis", "acm")
                os.makedirs(outdir, exist_ok=True)
                with open(os.path.join(outdir, f"sub{i}.bin"), "wb") as f:
                    f.write(b[soff:soff + size])
                print(f"     -> volcado sub{i}.bin")
    else:
        print("NO es #AMB; magics encontrados:", [(m.decode(), i) for m, i in find_magics(b)])


if __name__ == "__main__":
    main()