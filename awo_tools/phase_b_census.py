import struct, sys, re, json

def be32(b, o): return struct.unpack(">I", b[o:o+4])[0]
def bef(b, o):  return struct.unpack(">f", b[o:o+4])[0]

BODY_VTYPES = {0xB5, 0xB6, 0xF5, 0xBD, 0xFD, 0x3D}

def parse_awg(b, awg0, n_files):
    hdr = {
        "n_bones":  be32(b, awg0+0x10),
        "axes_rel": be32(b, awg0+0x14),
        "groups":   be32(b, awg0+0x18),
        "mg_off":   be32(b, awg0+0x20),
        "mg_size":  be32(b, awg0+0x28),
        "vb2_rel":  be32(b, awg0+0x2C),
        "ib_rel":   be32(b, awg0+0x30),
        "sec_rel":  be32(b, awg0+0x34),
        "end_rel":  be32(b, awg0+0x38),
    }
    hdr["n_sec"] = (hdr["vb2_rel"] - hdr["sec_rel"] - 2)//44
    hdr["n_vb2"] = (hdr["ib_rel"] - hdr["vb2_rel"])//44
    hdr["n_ib"]  = (hdr["end_rel"] - hdr["ib_rel"])//2
    return hdr

def descriptors(b, awg0, hdr):
    mg_start = awg0 + hdr["mg_off"]
    zone_end = awg0 + hdr["sec_rel"]
    zone = bytes(b[mg_start:zone_end])
    out = []
    for m in re.finditer(rb"max \d+ m", zone):
        dd = mg_start + (m.start() - 0x18)
        if be32(b, dd+0x44) != 0x2C00:
            continue
        if be32(b, dd+0x40) == 0:
            continue
        label = bytes(b[dd:dd+0x10]).split(b"\x00")[0].decode("latin1", "replace")
        A_s = be32(b, dd+0x50) >> 8
        A_c = be32(b, dd+0x54) >> 8
        B_s = be32(b, dd+0x58) >> 8
        B_c = be32(b, dd+0x5C) >> 8
        out.append((dd - awg0, label, A_s, A_c, B_s, B_c))
    return out

def arms(b, awg0, hdr):
    axes = awg0 + hdr["axes_rel"]
    rows = []
    for i in range(hdr["n_bones"]):
        e = axes + i*80
        arm = be32(b, e+0x34)
        sello = be32(b, e+0x30)
        if arm == 0:
            rows.append((i, sello, 0, None)); continue
        ao = awg0 + arm
        vals = [be32(b, ao+k*4) for k in range(5)]
        rows.append((i, sello, arm, vals))
    return rows

def classify(v, hdr, awg_size):
    if v == 0: return "zero"
    if 0 <= v < hdr["n_sec"]: return "POOL"
    if hdr["n_sec"] <= v < hdr["n_sec"]+hdr["n_vb2"]: return "VB2"
    if 0 <= v < hdr["n_ib"]: return "IBidx"
    if 0 <= v < 2*hdr["n_ib"]: return "IBbyte"
    if 0 <= v < hdr["n_bones"]: return "bone?"
    if 0 <= v < awg_size: return "relAWG"
    return "other"

def scan_indexlist(b, awg0, hdr, awg_size, lo, hi, minrun=4):
    """busca runs contiguos de u32 dentro de [lo,hi) que sean todos indices de pool"""
    runs = []
    run_start = None
    for off in range(lo, hi-4, 4):
        v = be32(b, awg0+off)
        ok = (hdr["n_sec"] <= v < hdr["n_sec"]+hdr["n_vb2"])
        if ok and run_start is None:
            run_start = off
        elif not ok and run_start is not None:
            n = (off - run_start)//4
            if n >= minrun: runs.append((run_start, n))
            run_start = None
    if run_start is not None:
        n = (hi - run_start)//4
        if n >= minrun: runs.append((run_start, n))
    return runs

def main():
    path = sys.argv[1]
    b = open(path, "rb").read()
    awo = 0x40
    assert b[:4] in (b"#AMB", b"#AWO"), b[:4]
    n_bones = be32(b, awo+0x10)
    n_awg = be32(b, awo+0x18)
    awg_tbl = awo + be32(b, awo+0x1C)
    label = "%s" % path.split("\\")[-1]
    print("="*78)
    print("%s  size=%d  n_bones=%d n_awg=%d" % (label, len(b), n_bones, n_awg))
    offs = [awo + be32(b, awg_tbl+i*4) for i in range(n_awg)]
    awg0 = offs[0]
    hdr = parse_awg(b, awg0, len(b))
    print("AWG0 @0x%X  axes=0x%X mg=0x%X(+0x%X) groups=%d" % (
        awg0, hdr["axes_rel"], hdr["mg_off"], hdr["mg_size"], hdr["groups"]))
    print("  vb2=+0x%X(rel) sec=+0x%X ib=+0x%X end=+0x%X" % (
        hdr["vb2_rel"], hdr["sec_rel"], hdr["ib_rel"], hdr["end_rel"]))
    print("  n_sec=%d n_vb2=%d n_ib=%d  (sec+2 align, first marker=%08X)" % (
        hdr["n_sec"], hdr["n_vb2"], hdr["n_ib"], be32(b, awg0+hdr["sec_rel"]+2)))
    awg_size = hdr["end_rel"]

    # --- BONES usados por el pool (posicion +28) ---
    used = {}
    sec_real = awg0 + hdr["sec_rel"] + 2
    for i in range(hdr["n_sec"]):
        bn = be32(b, sec_real + i*44 + 28)
        used[bn] = used.get(bn, 0) + 1
    print("  bones en pool: %d distintos; rango %d..%d" % (
        len(used), min(used), max(used)))

    # --- DESCRIPTORES ---
    descs = descriptors(b, awg0, hdr)
    print("  descriptores=%d" % len(descs))
    badA = 0
    ib = [struct.unpack(">H", b[awg0+hdr["ib_rel"]+2*k:awg0+hdr["ib_rel"]+2*k+2])[0]
          for k in range(hdr["n_ib"])]
    for (do, lab, As, Ac, Bs, Bc) in descs:
        idxs = [ib[x] for x in range(Bs, min(Bs+Bc, len(ib))) if ib[x] != 0xFFFF]
        if idxs:
            mn, mx = min(idxs), max(idxs)
            ok = (mn >= As and mx < As+Ac)
            if not ok: badA += 1
        else:
            ok = False
    print("  descriptor A/B: indices de B dentro de A -> %d/%d OK" % (len(descs)-badA, len(descs)))

    # --- ARMS ---
    rows = arms(b, awg0, hdr)
    print("  --- arms (5 u32 por hueso, ptr rel AWG0) ---")
    big = [r for r in rows if r[3] and (r[3][1] or r[3][3])]
    print("  arms con campos !=0: %d de %d" % (len(big), len(rows)))
    prev = None
    for (i, sello, arm, vals) in rows:
        if not vals: continue
        if vals[1] or vals[3]:
            cls = [classify(v, hdr, awg_size) for v in vals]
            print("   bone %2d arm=0x%05X sello=%08X vals=%s %s" % (
                i, arm, sello, vals, cls))
    # periodicidad de vals[3]
    v3 = [r[3][3] for r in rows if r[3] and r[3][3]]
    if len(v3) >= 2:
        d = sorted(set(v3[k+1]-v3[k] for k in range(len(v3)-1) if v3[k+1] > v3[k]))
        print("  deltas de arm[+12] (campo4): %s" % d[:10])

    # --- CENSO: runs de indices de pool fuera de sec34/vb2/ib ---
    print("  --- census: runs de indices de pool (>=4 u32) fuera de buffers ---")
    regions = [("mg", hdr["mg_off"], hdr["sec_rel"]),
               ("tail", hdr["end_rel"], awg_size)]
    total = 0
    for (nm, lo, hi) in regions:
        if hi <= lo: continue
        runs = scan_indexlist(b, awg0, hdr, awg_size, lo, hi)
        for (off, n) in runs:
            print("   [%s] @+0x%05X  n=%d  first=%d..%d" % (
                nm, off, n, be32(b, awg0+off), be32(b, awg0+off+(n-1)*4)))
            total += n
    print("  total indices de pool en runs fuera de buffers: %d" % total)

if __name__ == "__main__":
    main()
