import struct, sys, os

AFS = r"C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\us\data_cmn.afs"
OUT = r"C:\Users\javie\AppData\Local\Temp\opencode\phaseb"

def read_table(path):
    with open(path, "rb") as f:
        head = f.read(8)
        magic = head[:4]
        count = struct.unpack("<I", head[4:8])[0]
        tbl = f.read(count * 8)
    entries = []
    for i in range(count):
        addr, size = struct.unpack("<II", tbl[i*8:i*8+8])
        entries.append((addr, size))
    return magic, count, entries

def main():
    idxs = [int(x) for x in sys.argv[1:]] or [327]
    magic, count, entries = read_table(AFS)
    print("magic=%r count=%d" % (magic, count))
    with open(AFS, "rb") as f:
        for idx in idxs:
            if idx >= count:
                print("entry %d out of range" % idx)
                continue
            addr, size = entries[idx]
            f.seek(addr)
            data = f.read(size)
            magic4 = data[:4].hex()
            out = os.path.join(OUT, "e%d.comp" % idx)
            open(out, "wb").write(data)
            print("entry %d: addr=0x%X size=%d magic=%s -> %s" % (idx, addr, size, magic4, out))

if __name__ == "__main__":
    main()
