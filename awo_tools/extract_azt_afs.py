"""Extract standalone #AZT entries from an Xbox 360 AFS archive.

This is an analysis helper for data_cmn.afs entries that start with #AZT
instead of the #AMB container expected by texture_b3.py.
"""
import argparse
import io
import json
import os
import struct
import subprocess

from PIL import Image


def read_entries(path):
    data = open(path, "rb").read()
    if data[:3] != b"AFS":
        raise RuntimeError("not an AFS archive")
    count = struct.unpack_from("<I", data, 4)[0]
    return data, [struct.unpack_from("<II", data, 8 + i * 8)
                  for i in range(count)]


def decompress(raw, tool, workdir, entry):
    os.makedirs(workdir, exist_ok=True)
    src = os.path.join(workdir, "entry_%d.lzx" % entry)
    dst = os.path.join(workdir, "entry_%d.bin" % entry)
    with open(src, "wb") as f:
        f.write(raw)
    result = subprocess.run([tool, src, dst], capture_output=True, text=True)
    if result.returncode != 0 or not os.path.exists(dst):
        raise RuntimeError("decompression failed for entry %d: %s%s" %
                           (entry, result.stdout, result.stderr))
    with open(dst, "rb") as f:
        return f.read()


def parse_azt(data):
    if not data.startswith(b"#AZT"):
        raise RuntimeError("entry starts with %r, not #AZT" % data[:4])
    tex_count = struct.unpack_from(">I", data, 0x10)[0]
    index_loc = struct.unpack_from(">I", data, 0x14)[0]
    textures = []
    for index in range(tex_count):
        offset = struct.unpack_from(">I", data, index_loc + index * 4)[0]
        width = struct.unpack_from(">H", data, offset + 16)[0]
        height = struct.unpack_from(">H", data, offset + 18)[0]
        data_offset = struct.unpack_from(">I", data, offset + 20)[0]
        next_offset = (struct.unpack_from(">I", data, index_loc + (index + 1) * 4)[0]
                       if index + 1 < tex_count else len(data))
        blob_size = next_offset - data_offset
        if data_offset + blob_size > len(data) or blob_size < 128:
            raise RuntimeError("invalid texture %d bounds" % index)
        textures.append({
            "index": index,
            "width": width,
            "height": height,
            "data_offset": data_offset,
            "size": blob_size,
        })
    return textures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("afs")
    parser.add_argument("--tool", required=True)
    parser.add_argument("--entries", nargs="+", type=int, required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--work", required=True)
    args = parser.parse_args()

    archive, entries = read_entries(args.afs)
    os.makedirs(args.out, exist_ok=True)
    inventory = []
    for entry in args.entries:
        address, size = entries[entry]
        decoded = decompress(archive[address:address + size], args.tool,
                             args.work, entry)
        textures = parse_azt(decoded)
        entry_dir = os.path.join(args.out, str(entry))
        os.makedirs(entry_dir, exist_ok=True)
        for texture in textures:
            offset = texture["data_offset"]
            dds = decoded[offset:offset + texture["size"]]
            image = Image.open(io.BytesIO(dds)).convert("RGBA")
            filename = "tex%02d_%dx%d.png" % (
                texture["index"], texture["width"], texture["height"])
            image.save(os.path.join(entry_dir, filename))
        inventory.append({"entry": entry, "decoded_size": len(decoded),
                          "textures": textures})
        print("entry %d: %d textures" % (entry, len(textures)))
    with open(os.path.join(args.out, "inventory.json"), "w") as f:
        json.dump(inventory, f, indent=2)


if __name__ == "__main__":
    main()
