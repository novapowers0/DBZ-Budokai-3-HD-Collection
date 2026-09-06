#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""stage_analyze.py - Analizar bins candidatos a STAGE del data_cmn.afs.

Para cada bin: descomprime, localiza TODOS los bloques #AWO y, por cada uno,
reutiliza el parser validado de awg_to_obj_b3 (parse_awg0/build_mesh) para
calcular el AABB world de su geometria. Un ENTORNO tiene cientos de miles de
vertices repartidos en un area grande; un personaje es humano y centrado.

Uso:
  python stage_analyze.py <entry> [<entry> ...]
"""
import struct
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import awg_to_obj_b3 as awg

AFS = r"us/data_cmn.afs"
TMP = r"C:\Users\javie\AppData\Local\Temp\opencode\afs_audit"
XBD = r"mod center\Xbox 360 Compression - Decompression tool from the XBOX Development Kit\xbdecompress.exe"

d = open(AFS, "rb").read()
count = struct.unpack("<I", d[4:8])[0]
entries = {}
for i in range(count):
    addr, size = struct.unpack("<II", d[8 + i * 8 : 16 + i * 8])
    entries[i] = (addr, size)


def get_bin(entry):
    addr, size = entries[entry]
    os.makedirs(TMP, exist_ok=True)
    lzx = os.path.join(TMP, f"st{entry}.lzx")
    out = os.path.join(TMP, f"st{entry}.bin")
    open(lzx, "wb").write(d[addr : addr + size])
    r = subprocess.run([XBD, lzx, out], capture_output=True, timeout=60)
    if not os.path.exists(out) or os.path.getsize(out) == 0:
        return None
    return open(out, "rb").read()


def find_awo_blocks(b):
    pos = 0
    out = []
    while True:
        i = b.find(b"#AWO", pos)
        if i < 0:
            break
        out.append(i)
        pos = i + 1
    return out


def aabb(verts, cap=1e6):
    import math
    xs, ys, zs = [], [], []
    for v in verts:
        x, y, z = v[0], v[1], v[2]
        if not (math.isfinite(x) and math.isfinite(y) and math.isfinite(z)):
            continue
        if abs(x) > cap or abs(y) > cap or abs(z) > cap:
            continue
        xs.append(x)
        ys.append(y)
        zs.append(z)
    if not xs:
        return None
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def analyze(entry):
    b = get_bin(entry)
    if b is None:
        return None, "ERR decompress"
    awos = find_awo_blocks(b)
    n_bad = 0
    total_verts = 0
    total_tris = 0
    bounds = None
    per = []
    for awo in awos:
        try:
            st = awg.parse_awg0(b, awo)
            # sanity: punteros dentro del archivo
            if st["sec_abs"] < 0 or st["sec_abs"] >= len(b) or st["n_sec"] < 0 or st["n_sec"] > 2_000_000:
                n_bad += 1
                per.append((awo, "bad"))
                continue
            if st["n_vb2"] < 0 or st["n_vb2"] > 2_000_000 or st["n_ib"] < 0 or st["n_ib"] > 4_000_000:
                n_bad += 1
                per.append((awo, "bad"))
                continue
            verts, tris, _oob = awg.build_mesh(b, st)
            bb = aabb(verts)
            total_verts += len(verts)
            total_tris += len(tris)
            if bb is None:
                per.append((awo, f"{len(verts)}v/{len(tris)}t OOB"))
                continue
            if bounds is None:
                bounds = bb
            else:
                bounds = ((min(bounds[0][0], bb[0][0]), min(bounds[0][1], bb[0][1]), min(bounds[0][2], bb[0][2])),
                          (max(bounds[1][0], bb[1][0]), max(bounds[1][1], bb[1][1]), max(bounds[1][2], bb[1][2])))
            per.append((awo, f"{len(verts)}v/{len(tris)}t span={bb[1][0]-bb[0][0]:.1f}"))
        except Exception as e:
            n_bad += 1
            per.append((awo, f"ERR {e}"))
    sz = max(0.0, (bounds[1][0] - bounds[0][0])) if bounds else 0
    return {
        "entry": entry,
        "compressed": entries[entry][1],
        "decompressed": len(b),
        "n_awo": len(awos),
        "n_bad": n_bad,
        "verts": total_verts,
        "tris": total_tris,
        "bounds": bounds,
        "span_x": sz,
        "per": per,
    }


def main():
    for e in [int(x) for x in sys.argv[1:]]:
        r = analyze(e)
        if r is None:
            print(f"entry {e}: ERR decompress")
            continue
        b = r["bounds"]
        print(f"entry {e:>5}: comp={r['compressed']:>8} ({r['compressed']/1024:>6.1f}KB) "
              f"dec={r['decompressed']/1024:>7.1f}KB | AWO={r['n_awo']} (bad={r['n_bad']}) "
              f"| {r['verts']}v {r['tris']}t | span_x={r['span_x']:>9.1f}")
        if b:
            print(f"          bounds min=({b[0][0]:.1f},{b[0][1]:.1f},{b[0][2]:.1f}) "
                  f"max=({b[1][0]:.1f},{b[1][1]:.1f},{b[1][2]:.1f})")
        for awo, tag in r["per"][:8]:
            print(f"          AWO@{awo:06x}: {tag}")


if __name__ == "__main__":
    main()