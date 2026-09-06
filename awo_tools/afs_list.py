import struct
import sys


def afs_table(path):
    d = open(path, "rb").read()
    assert d[:3] == b"AFS", "no es AFS"
    count = struct.unpack("<I", d[4:8])[0]
    entries = []
    for i in range(count):
        addr, size = struct.unpack("<II", d[8 + i * 8 : 16 + i * 8])
        entries.append((i, addr, size))
    return d, entries


if __name__ == "__main__":
    path = sys.argv[1]
    d, entries = afs_table(path)
    n_comp = 0
    n_amb = 0
    n_other = 0
    for i, addr, size in entries:
        head = d[addr : addr + 4]
        if head == b"\x0f\xf5\x12\xee":
            n_comp += 1
        elif head[:4] == b"#AMB":
            n_amb += 1
        else:
            n_other += 1
    print(f"entradas: {len(entries)} | LZX comprimidos: {n_comp} | raw #AMB: {n_amb} | otro: {n_other}")
    print(f"{'idx':>5} {'size':>9} {'KB':>8}  head")
    for i, addr, size in entries:
        head = d[addr : addr + 4].hex()
        print(f"{i:>5} {size:>9} {size/1024:>8.1f}  {head}")