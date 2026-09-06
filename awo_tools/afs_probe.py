import struct
import os
import subprocess
import sys

AFS = r"us/data_cmn.afs"
TMP = r"C:\Users\javie\AppData\Local\Temp\opencode\afs_audit"
XBD = r"mod center\Xbox 360 Compression - Decompression tool from the XBOX Development Kit\xbdecompress.exe"

os.makedirs(TMP, exist_ok=True)

d = open(AFS, "rb").read()
count = struct.unpack("<I", d[4:8])[0]
entries = {}
for i in range(count):
    addr, size = struct.unpack("<II", d[8 + i * 8 : 16 + i * 8])
    entries[i] = (addr, size)


def decompress(entry):
    addr, size = entries[entry]
    lzx = os.path.join(TMP, f"e{entry}.lzx")
    out = os.path.join(TMP, f"e{entry}.bin")
    open(lzx, "wb").write(d[addr : addr + size])
    r = subprocess.run([XBD, lzx, out], capture_output=True, text=True)
    if not os.path.exists(out) or os.path.getsize(out) == 0:
        return None, r.stdout + r.stderr
    return open(out, "rb").read(), None


def describe(b, entry):
    if b is None:
        return "ERR"
    head = b[:8]
    s = []
    # buscar magics conocidos en los primeros 64KB
    for magic in [b"#AMB", b"#AWO", b"#AWG", b"#AZT", b"#AMO0", b"#AMG", b"#AMT",
                  b"#AWM", b"#AWN", b"#AWT", b"#AWP", b"#AWT", b"#AWE", b"#AWF"]:
        pos = b.find(magic, 0, 0x20000)
        if pos >= 0:
            s.append(f"{magic.decode() if isinstance(magic, bytes) else magic}@{hex(pos)}")
    if b[:4] == b"\x00\x00\x01\xba":
        s.append("MPEG")
    return f"len={len(b)} ({len(b)/1024:.0f}KB) first8={head.hex()} " + " ".join(s)


def main():
    entries_to_check = [int(x) for x in sys.argv[1:]]
    for e in entries_to_check:
        b, err = decompress(e)
        print(f"entry {e:>5}: {describe(b, e)}" + (f"  [{err}]" if err else ""))


if __name__ == "__main__":
    main()