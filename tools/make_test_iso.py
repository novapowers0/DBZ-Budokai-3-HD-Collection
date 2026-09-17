#!/usr/bin/env python3
"""Build a synthetic Xbox 360 XDVDFS disc image from a folder tree.

The runtime's DiscImageDevice reads raw XDVDFS images ("GDFX"): the volume
descriptor with the MICROSOFT*XBOX*MEDIA magic lives at sector 32 (offset
0x10000 when the image starts at sector 0) and points at the root directory; the
directory entries are 14-byte records plus the name, linked as a BST whose
pointers are counted in 4-byte units.

This tool is for TESTING disc mode (ISO) without a real disc: point it at a
folder that mimics a retail HD Collection disc and it writes an image the
launcher can mount. It packs files you provide; it never contains game data.

    python tools/make_test_iso.py <out.iso> <source folder> [--quiet]

Layout it produces (mirrors the retail disc used by the reports):

    <out.iso>/
      default.xex                 <- whatever you put in the source folder
      DBZ3/
        yae3_xenon.xex
        us/ (and/or eu/)

Verification: run dbz3.exe with the image next to it and check the log for
"ISO xex source 'DBZ3/yae3_xenon.xex'" and no "No function registered".
"""

import argparse
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
ATTR_DIRECTORY = 0x10
ATTR_NORMAL = 0x80


def align_up(value: int, boundary: int) -> int:
    return (value + boundary - 1) // boundary * boundary


class Dir:
    def __init__(self, name: str):
        self.name = name
        self.dirs = []
        self.files = []  # (name, host_path, size)
        self.blob_sector = 0
        self.blob_size = 0


def scan(path: str, name: str) -> Dir:
    node = Dir(name)
    for entry in sorted(os.scandir(path), key=lambda e: e.name.lower()):
        if entry.is_dir():
            node.dirs.append(scan(entry.path, entry.name))
        elif entry.is_file():
            node.files.append((entry.name, entry.path, entry.stat().st_size))
    return node


def walk_dirs(node: Dir):
    yield node
    for child in node.dirs:
        yield from walk_dirs(child)


def build_entry_bytes(entries, dir_blobs_first_ordinal):
    """entries: list of dicts with name/sector/length/attrs. Returns blob bytes.

    Each record is 14 bytes + name, and the ordinals (used by the BST links) are
    counted in 4-byte units, so the record is padded to the next 4-byte unit.
    The reader walks node_l -> record -> node_r, so a simple node_r chain with
    node_l = 0 makes every entry reachable (order does not matter for lookups:
    GetChild scans linearly and case-insensitively).
    """
    # First pass: ordinal of each record.
    ordinal = 0
    ordinals = []
    for e in entries:
        ordinals.append(ordinal)
        size = 14 + len(e["name"].encode("utf-8"))
        ordinal += (size + 3) // 4
    blob = bytearray(ordinal * 4)
    for i, e in enumerate(entries):
        name = e["name"].encode("utf-8")
        node_l = 0
        node_r = ordinals[i + 1] if i + 1 < len(entries) else 0
        off = ordinals[i] * 4
        struct.pack_into("<HHIIBB", blob, off, node_l, node_r, e["sector"], e["length"],
                         e["attrs"], len(name))
        blob[off + 14:off + 14 + len(name)] = name
    return bytes(blob)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("output", help="path of the .iso to write")
    ap.add_argument("source", help="folder to pack (its contents become the disc root)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not os.path.isdir(args.source):
        print(f"error: '{args.source}' is not a folder", file=sys.stderr)
        return 2

    log = (lambda *a: None) if args.quiet else print
    root = scan(args.source, "")

    # --- layout: sector 32 = volume descriptor, then the directory blobs, then
    # the file data, each aligned to a sector (the reader indexes by sector).
    #
    # Three phases, because a directory blob stores the sector/size of its
    # children and the entries are only final once every sector is assigned:
    #   1) reserve one sector per directory and compute its blob size,
    #   2) assign the file data sectors,
    #   3) build the blobs with the final values and write everything.
    dirs = list(walk_dirs(root))
    next_sector = 33
    for d in dirs:
        d.blob_sector = next_sector
        next_sector += 1
        entries = []
        for sub in d.dirs:
            entries.append({"name": sub.name, "sector": 0, "length": 1, "attrs": ATTR_DIRECTORY,
                            "child": sub})
        for name, host_path, size in d.files:
            entries.append({"name": name, "host_path": host_path, "sector": 0, "length": size,
                            "attrs": ATTR_NORMAL})
        d.entries = entries
        d.blob_size = len(build_entry_bytes(entries, 0))
        if d.blob_size > SECTOR:
            print(f"error: directory '{d.name or '<root>'}' needs "
                  f"{d.blob_size} bytes (> one sector); reduce the entry count",
                  file=sys.stderr)
            return 3

    file_sector = next_sector
    for d in dirs:
        for e in d.entries:
            if e["attrs"] == ATTR_DIRECTORY:
                continue
            e["sector"] = file_sector
            file_sector += (e["length"] + SECTOR - 1) // SECTOR
    total_sectors = max(file_sector, 34)

    # Fill in the directory entries now that every sector is known, and build the
    # blobs that will be written.
    for d in dirs:
        for e in d.entries:
            if e["attrs"] == ATTR_DIRECTORY:
                e["sector"] = e["child"].blob_sector
                # The reader descends into the child only when length != 0; real
                # images store the child's record size here.
                e["length"] = max(e["child"].blob_size, 13)
        d.blob = build_entry_bytes(d.entries, 0)

    log(f"sectors: {total_sectors} ({total_sectors * SECTOR / 1048576:.1f} MB)")
    for d in dirs:
        log(f"  dir '{d.name or '<root>'}' blob at sector {d.blob_sector}")

    # --- write ---
    with open(args.output, "wb") as f:
        f.truncate(total_sectors * SECTOR)

        def write_at(offset: int, data: bytes):
            f.seek(offset)
            f.write(data)

        # Volume descriptor (sector 32): magic, root dir sector/size at +20/+24.
        vd = bytearray(SECTOR)
        vd[0:len(MAGIC)] = MAGIC
        struct.pack_into("<II", vd, 20, root.blob_sector, max(root.blob_size, 13))
        # Real images pad the volume descriptor with 0xFF.
        for i in range(28, SECTOR):
            vd[i] = 0xFF
        write_at(32 * SECTOR, bytes(vd))

        # Directory blobs.
        for d in dirs:
            write_at(d.blob_sector * SECTOR, d.blob)

        # File data.
        for d in dirs:
            for e in d.entries:
                if e["attrs"] == ATTR_DIRECTORY:
                    continue
                src = os.path.getsize(e["host_path"])
                if src != e["length"]:
                    print(f"error: '{e['host_path']}' changed size while packing",
                          file=sys.stderr)
                    return 4
                f.seek(e["sector"] * SECTOR)
                remaining = src
                with open(e["host_path"], "rb") as srcf:
                    while remaining > 0:
                        chunk = srcf.read(min(1 << 20, remaining))
                        if not chunk:
                            break
                        f.write(chunk)
                        remaining -= len(chunk)
                # Pad the last sector with zeros (the reader slices by length).
                pad = align_up(src, SECTOR) - src
                if pad:
                    f.write(b"\0" * pad)

    log(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
