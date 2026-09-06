import struct
import os
import subprocess
import sys

AFS = r"us/data_cmn.afs"
TMP = r"C:\Users\javie\AppData\Local\Temp\opencode\afs_audit"
XBD = r"mod center\Xbox 360 Compression - Decompression tool from the XBOX Development Kit\xbdecompress.exe"

d = open(AFS, "rb").read()
count = struct.unpack("<I", d[4:8])[0]
entries = {}
for i in range(count):
    addr, size = struct.unpack("<II", d[8 + i * 8 : 16 + i * 8])
    entries[i] = (addr, size)

MAGICS = [b"#AWO", b"#AWG", b"#AZT", b"#ACM", b"#AWM", b"#AMO0", b"#AMB"]


def classify(entry):
    addr, size = entries[entry]
    lzx = os.path.join(TMP, f"s{entry}.lzx")
    out = os.path.join(TMP, f"s{entry}.bin")
    open(lzx, "wb").write(d[addr : addr + size])
    try:
        r = subprocess.run([XBD, lzx, out], capture_output=True, timeout=30)
    except Exception:
        return "ERR"
    if not os.path.exists(out) or os.path.getsize(out) == 0:
        return "ERR"
    b = open(out, "rb").read()
    counts = {m: 0 for m in MAGICS}
    for m in MAGICS:
        pos = 0
        while True:
            i = b.find(m, pos)
            if i < 0:
                break
            counts[m] += 1
            pos = i + 1
    tag = "".join(f"{m.decode()}{counts[m]}" for m in MAGICS if counts[m])
    if b[:4] == b"\x00\x00\x01\xba":
        tag = "MPEG"
    os.remove(lzx)
    os.remove(out)
    return f"len={len(b)} {tag}"


def main():
    ranges = []
    for a in sys.argv[1:]:
        if "-" in a:
            lo, hi = a.split("-")
            ranges.append(range(int(lo), int(hi) + 1))
        else:
            ranges.append([int(a)])
    out_lines = []
    for rng in ranges:
        for e in rng:
            if e not in entries:
                continue
            addr, size = entries[e]
            if size > 100 * 1024:  # solo bins >100KB comprimidos
                c = classify(e)
            else:
                c = "small"
            out_lines.append(f"{e:>5} {size:>9} {c}")
            print(f"{e:>5} {size:>9} {c}", flush=True)
    with open(r"C:\Users\javie\AppData\Local\Temp\opencode\afs_classified.txt", "a") as f:
        f.write("\n".join(out_lines) + "\n")


if __name__ == "__main__":
    main()