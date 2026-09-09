#!/usr/bin/env python3
"""corpus_scan.py - Parse-all corpus builder para AFS de DBZ Budokai 3 HD.

Semilla de la Content Database (acelerador A3 de HOJA_DE_RUTA_ACELERADA.md):
parsea TODAS las entradas de un AFS, descomprime LZX, clasifica por magics
internos y extrae metadata de estructura AWO (n_awg, labels). Produce:

  corpus_<afs>.json      - por entrada: idx, size_fisico, size_bin,
                           magics{..}, cluster, n_awg, label, anomalia
  formats_summary.json   - clusters de formato con conteos/rangos
  anomalias.txt          - entradas que no parsean o sin magic conocido
  corpus_all.db          - SQLite con la misma informacion (consulta SQL)

Uso:
  python corpus_scan.py <afs> [-o outdir] [-r 0-3990] [--quick]
                             [--workers 8] [--keep-bin] [--out-json name]

  --quick        no parsea estructura AWO (solo magics) -> mucho mas rapido
  -r 0-3990      rango de entradas (por defecto todas)
  --workers N    procesos paralelos de descompresion
  --keep-bin     conserva los bins descomprimidos en <outdir>/bins/
  --xbd          ruta alternativa a xbdecompress.exe

Regla operativa: la tabla AFS se lee en OFFSET 8 (no 0x10), igual que el
runtime rexglue. El magic LZX es 0F F5 12 EE.
"""
import argparse
import json
import os
import sqlite3
import struct
import subprocess
import sys
import tempfile
import uuid
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor

MAGICS = [b"#AWO", b"#AWG", b"#AZT", b"#ACM", b"#AWM", b"#AMO0", b"#AMB", b"#AMT",
          b"#SPX", b"#ACC", b"#AFL", b"#SLX", b"#SLXS", b"#CLL", b"#BDX", b"#AMV",
          b"#CSK", b"#BFC", b"#BCM", b"#BSK", b"#AMM", b"#AMC", b"#AST", b"#ASE",
          b"#AME", b"#AWA", b"#AWB", b"#AWBK", b"#ZDD", b"#CAD", b"#CAS", b"#ACE",
          b"#AML", b"#AMG", b"#MTC"]
LZX_MAGIC = b"\x0f\xf5\x12\xee"

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))


def _first_existing(*paths):
    for p in paths:
        if os.path.exists(p):
            return p
    return paths[0]


XBD = _first_existing(
    os.path.join(ROOT, "mod center",
                 "Xbox 360 Compression - Decompression tool "
                 "from the XBOX Development Kit", "xbdecompress.exe"),
    os.path.join(ROOT, "tools", "xbdecompress.exe"),
    os.path.join(ROOT, "mod center hd", "tools", "xbdecompress.exe"),
)


def afs_table(path):
    with open(path, "rb") as f:
        head = f.read(8)
    if head[:3] != b"AFS":
        raise SystemExit("no es AFS: %r" % head[:4])
    count = struct.unpack("<I", head[4:8])[0]
    entries = []
    with open(path, "rb") as f:
        f.seek(8)
        for i in range(count):
            addr, size = struct.unpack("<II", f.read(8))
            entries.append((i, addr, size))
    return entries


def decompress(data, workdir):
    """Devuelve (bin_bytes, error_str). data puede ser raw o LZX."""
    if data[:4] == LZX_MAGIC:
        token = uuid.uuid4().hex[:12]
        lzx = os.path.join(workdir, "in_%s.lzx" % token)
        out = os.path.join(workdir, "out_%s.bin" % token)
        with open(lzx, "wb") as f:
            f.write(data)
        if os.path.exists(out):
            os.remove(out)
        env = dict(os.environ)
        env["TEMP"] = workdir
        env["TMP"] = workdir
        try:
            r = subprocess.run([XBD, lzx, out], capture_output=True,
                               timeout=30, env=env)
        except Exception as e:
            return None, "xbd_exc:%s" % e
        if r.returncode != 0 or not os.path.exists(out) or os.path.getsize(out) == 0:
            return None, "xbd_rc:%d" % r.returncode
        with open(out, "rb") as f:
            result = f.read()
        # Limpieza: los temporales in_*.lzx/out_*.bin se borran tras usar. Sin
        # esto cada scan acumulaba ~16 GB en .work (una entrada por bin). La
        # copia permanente solo existe si --keep-bin (scan_entry).
        try:
            os.remove(lzx)
            os.remove(out)
        except OSError:
            pass
        return result, None
    return data, None


def count_magics(b):
    counts = {}
    for m in MAGICS:
        n = 0
        pos = 0
        while True:
            i = b.find(m, pos)
            if i < 0:
                break
            n += 1
            pos = i + 1
        if n:
            counts[m.decode()] = n
    return counts


def parse_awo(b):
    """Extrae metadata minima del primer #AWO (estructura por doc 03_formatos)."""
    i = b.find(b"#AWO")
    if i < 0:
        return None
    awo = b[i:]
    if len(awo) < 0x28:
        return None
    n_awg = struct.unpack(">I", awo[0x18:0x1C])[0]
    tbl = struct.unpack(">I", awo[0x1C:0x20])[0]
    label = None
    try:
        if tbl + 4 <= len(awo):
            off = struct.unpack(">I", awo[tbl:tbl + 4])[0]
            if off + 0x40 <= len(awo):
                no = struct.unpack(">I", awo[off + 0x1C:off + 0x20])[0]
                if off + no + 16 <= len(awo):
                    label = awo[off + no:off + no + 16].split(b"\x00")[0].decode("latin1", "ignore") or None
    except Exception:
        pass
    return {"n_awg": n_awg, "label": label}


def cluster_of(counts, has_mpeg, anomalia):
    if has_mpeg:
        return "audio_mpeg"
    if anomalia:
        return "err"
    if counts.get("#ZDD") or (counts.get("#CAD") and counts.get("#CAS")):
        return "stage"
    if counts.get("#AWO") or counts.get("#AMO0"):
        return "model"
    if counts.get("#ACM"):
        return "moveset"
    if counts.get("#AZT") and not (counts.get("#AWO") or counts.get("#ACM")):
        return "texture"
    if counts.get("#AMB"):
        return "amb"
    if not counts:
        return "no_magic"
    return "other"


def scan_entry(idx, addr, size, data_bytes, outdir, keep_bin, quick):
    """data_bytes = slice crudo del AFS para la entrada. Devuelve dict corpus."""
    rec = {"idx": idx, "size_fisico": size, "size_bin": None,
           "magics": {}, "cluster": None, "n_awg": None, "label": None,
           "signature": None, "anomalia": None}
    if size > 0x400000:
        rec["anomalia"] = "entry_masiva"
        return rec
    bin_bytes, err = decompress(data_bytes, outdir)
    if err:
        rec["anomalia"] = "decompress:" + err
        return rec
    rec["size_bin"] = len(bin_bytes)
    rec["signature"] = bin_bytes[:8].hex()
    if bin_bytes[:4] == b"\x00\x00\x01\xba":
        rec["cluster"] = "audio_mpeg"
        return rec
    rec["magics"] = count_magics(bin_bytes)
    if not rec["magics"]:
        rec["anomalia"] = "sin_magic_HD"
        rec["cluster"] = "no_magic"
        return rec
    awo = None if quick else parse_awo(bin_bytes)
    if awo:
        rec["n_awg"] = awo["n_awg"]
        rec["label"] = awo["label"]
    rec["cluster"] = cluster_of(rec["magics"], False, None)
    if keep_bin:
        bdir = os.path.join(outdir, "bins")
        os.makedirs(bdir, exist_ok=True)
        with open(os.path.join(bdir, "e%04d.bin" % idx), "wb") as f:
            f.write(bin_bytes)
    return rec


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("afs")
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("-r", "--range", default=None,
                    help="rango de entradas, p.ej. 0-3990 o 44-69,400,401")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--keep-bin", action="store_true")
    ap.add_argument("--xbd", default=None)
    ap.add_argument("--json-name", default=None)
    args = ap.parse_args()

    global XBD
    if args.xbd:
        XBD = args.xbd
    if not os.path.exists(XBD):
        raise SystemExit("xbdecompress no encontrado en %s (usa --xbd)" % XBD)

    entries = afs_table(args.afs)
    ranges = []
    if args.range:
        for a in args.range.split(","):
            if "-" in a:
                lo, hi = a.split("-")
                ranges.extend(range(int(lo), int(hi) + 1))
            else:
                ranges.append(int(a))
    else:
        ranges = [e[0] for e in entries]

    outdir = args.out or os.path.join(
        ROOT, "out", "analysis", "corpus")
    os.makedirs(outdir, exist_ok=True)

    with open(args.afs, "rb") as f:
        afs_data = f.read()

    def work(idx):
        addr, size = entries[idx][1], entries[idx][2]
        data = afs_data[addr:addr + size]
        wd = os.path.join(outdir, ".work")
        os.makedirs(wd, exist_ok=True)
        return scan_entry(idx, addr, size, data, wd, args.keep_bin, args.quick)

    results = []
    if args.workers > 1 and len(ranges) > 1:
        with ThreadPoolExecutor(max_workers=args.workers) as ex:
            for rec in ex.map(work, ranges):
                results.append(rec)
                if len(results) % 500 == 0:
                    print("... %d/%d" % (len(results), len(ranges)), flush=True)
    else:
        for idx in ranges:
            results.append(work(idx))

    results.sort(key=lambda r: r["idx"])

    base = os.path.basename(args.afs).replace(".afs", "")
    json_name = args.json_name or ("corpus_%s.json" % base)
    with open(os.path.join(outdir, json_name), "w", encoding="utf-8") as f:
        json.dump(results, f, ensure_ascii=False, indent=0)

    # summary de clusters
    clusters = defaultdict(list)
    for r in results:
        clusters[r["cluster"] or "?"].append(r["idx"])
    summary = {}
    for k, v in sorted(clusters.items()):
        summary[k] = {"count": len(v), "min_size": min(
            (r["size_bin"] or 0) for r in results if (r["cluster"] or "?") == k),
            "max_size": max((r["size_bin"] or 0) for r in results if (r["cluster"] or "?") == k),
            "entries": v[:50]}
    # desglose de no_magic por firma (8 primeros bytes)
    nomagic = defaultdict(list)
    for r in results:
        if r["cluster"] == "no_magic":
            nomagic[r["signature"] or "?"].append(r["idx"])
    summary["_no_magic_por_firma"] = {
        sig: {"count": len(v), "entries": v[:20]} for sig, v in
        sorted(nomagic.items(), key=lambda kv: -len(kv[1]))}
    with open(os.path.join(outdir, "formats_summary.json"), "w", encoding="utf-8") as f:
        json.dump({"afs": args.afs, "total": len(results), "clusters": summary},
                  f, ensure_ascii=False, indent=1)

    # anomalias
    anom = [r for r in results if r.get("anomalia")]
    with open(os.path.join(outdir, "anomalias.txt"), "w", encoding="utf-8") as f:
        for r in anom:
            f.write("e%d size=%d anomalia=%s magics=%s\n" % (
                r["idx"], r["size_fisico"], r["anomalia"], r["magics"]))

    # sqlite (acumula todos los AFS en una sola DB, columna afs)
    dbp = os.path.join(outdir, "corpus_all.db")
    conn = sqlite3.connect(dbp)
    conn.execute("CREATE TABLE IF NOT EXISTS corpus (afs TEXT, idx INTEGER, "
                 "size_fisico INTEGER, size_bin INTEGER, cluster TEXT, "
                 "n_awg INTEGER, label TEXT, anomalia TEXT, magics TEXT, signature TEXT, "
                 "PRIMARY KEY (afs, idx))")
    for r in results:
        conn.execute("INSERT OR REPLACE INTO corpus VALUES (?,?,?,?,?,?,?,?,?,?)", (
            base, r["idx"], r["size_fisico"], r["size_bin"], r["cluster"],
            r["n_awg"], r["label"], r["anomalia"],
            json.dumps(r["magics"]), r["signature"]))
    conn.commit()
    conn.close()

    # informe consola
    print("== %s == entradas escaneadas: %d" % (base, len(results)))
    for k, v in sorted(clusters.items()):
        print("  %-12s %5d  (min=%d max=%d)" % (
            k, len(v), summary[k]["min_size"], summary[k]["max_size"]))
    print("anomalias: %d -> %s" % (len(anom), os.path.join(outdir, "anomalias.txt")))
    print("db: %s" % dbp)


if __name__ == "__main__":
    main()