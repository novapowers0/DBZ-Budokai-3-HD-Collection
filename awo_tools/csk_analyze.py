#!/usr/bin/env python3
# ⚠️ DESACTUALIZADO 2026-09-08: este parser recorre la cadena AP (frames) y
# confunde los bloques AP con "datos HR". El formato CORRECTO (attack → AP type
# 1 → HR code → bloque HR → damage u16) está en csk_chain.py / csk_edit.py.
# No usar para editar daño (causó crash en combate).
"""csk_analyze.py - RE del bloque #CSK HD (moveset): cadena de edicion de habilidades.

El #CSK (equivalente BE del BSK PS2) indexa por attack code:
  entrada[attack_code] -> bloque de animacion -> lista HR -> dato HR
Los campos HR son pares [damage u16][code u16] empaquetados BE:
  u32 = (damage << 16) | code.

Uso:
  python csk_analyze.py --bin out/analysis/acm/krillin333/sub0.bin [--code 0 14 15 58]
  python csk_analyze.py --bin out/analysis/acm/krillin333/sub0.bin --dump-hr 0x95fc

  --bin     bloque #CSK ya extraido (o --entry 333 para extraerlo del AFS)
  --code    lista de attack codes a seguir la cadena
  --all     recorrer todos los attack codes usados
  --dump    volcar los datos HR en offsets literales (hex)
"""
import argparse
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def parse_csk(b):
    n = struct.unpack_from(">I", b, 0x10)[0]
    used = {}
    for i in range(n):
        e = struct.unpack_from(">IIII", b, 0x20 + i * 0x10)
        for off in (e[0], e[2]):
            if off and off < len(b):
                used[i] = e
                break
    return n, used


def anim_block(b, off):
    """Devuelve (anim_pairs, hr_list_off, n_ap) del bloque de animacion en off."""
    blk = b[off:off + 0x80]
    j = blk.find(b"\xff" * 12)
    if j < 0:
        return [], None, None
    # parejas anim antes del sentinel
    pairs = []
    p = 0
    while p + 4 < j:
        a, m = struct.unpack_from(">HH", blk, p)
        if a == 0 and m == 0:
            break
        pairs.append((a, m))
        p += 4
    pad, n_ap, hr_off = struct.unpack_from(">III", blk, j + 12)
    return pairs, hr_off, n_ap


def hr_list(b, hr_off):
    pairs = []
    p = hr_off
    while p + 8 <= len(b) and p < hr_off + 0x400:
        c = struct.unpack_from(">H", b, p)[0]
        t = struct.unpack_from(">H", b, p + 2)[0]
        o = struct.unpack_from(">I", b, p + 4)[0]
        if o == 0 or o >= len(b):
            break
        pairs.append((c, t, o))
        p += 8
    return pairs


def fmt_hr(b, o):
    d = b[o:o + 0x20]
    if len(d) < 0x20:
        return "  (fuera de rango)"
    vals = struct.unpack_from(">8I", d, 0)
    out = []
    for i, v in enumerate(vals):
        f = struct.unpack_from(">f", d, i * 4)[0]
        hi, lo = v >> 16, v & 0xFFFF
        if (hi and lo) and hi < 0x8000:
            out.append(f"damage={hi} code={lo:#06x}")
        elif 0.0001 < abs(f) < 1e6:
            out.append(f"float={f:+.4f}")
        else:
            out.append(f"0x{v:08x}")
    return "  " + " | ".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=None)
    ap.add_argument("--entry", type=int, default=None, help="extraer del AFS us")
    ap.add_argument("--code", nargs="*", type=int, default=None)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--dump", type=lambda x: int(x, 16), default=None)
    args = ap.parse_args()

    if args.bin:
        b = open(args.bin, "rb").read()
    elif args.entry is not None:
        import subprocess, uuid, tempfile
        from pathlib import Path
        afs = os.path.join(ROOT, "us", "data_cmn.afs")
        with open(afs, "rb") as f:
            f.seek(8)
            ents = [struct.unpack("<II", f.read(8)) for _ in range(
                struct.unpack("<I", f.read(4))[0])]
        # (cuidado: el seek consume; releemos)
        with open(afs, "rb") as f:
            f.seek(8)
            count = struct.unpack("<I", f.read(4))[0]
            ents = [struct.unpack("<II", f.read(8)) for _ in range(count)]
            a, s = ents[args.entry]
            f.seek(a)
            raw = f.read(s)
        workdir = os.path.join(ROOT, "out", "analysis", "acm", ".work")
        os.makedirs(workdir, exist_ok=True)
        xbd = os.path.join(ROOT, "mod center",
                           "Xbox 360 Compression - Decompression tool "
                           "from the XBOX Development Kit", "xbdecompress.exe")
        tok = uuid.uuid4().hex[:8]
        lzx = os.path.join(workdir, "in_%s.lzx" % tok)
        out = os.path.join(workdir, "out_%s.bin" % tok)
        open(lzx, "wb").write(raw)
        subprocess.run([xbd, lzx, out], capture_output=True)
        data = open(out, "rb").read()
        # localizar el sub-bloque #CSK (type=0xFFFFFFFF)
        n_sub = struct.unpack(">I", data, 0x10)[0]
        b = None
        for i in range(n_sub):
            off, size, typ, _ = struct.unpack_from(">IIII", data, 0x20 + i * 0x10)
            if typ == 0xFFFFFFFF:
                b = data[off:off + size]
                break
        if b is None:
            raise SystemExit("no #CSK en entry %d" % args.entry)
        print("entry %d -> #CSK %d B" % (args.entry, len(b)))
    else:
        raise SystemExit("necesito --bin o --entry")

    n, used = parse_csk(b)
    print("n_entradas=%d, attack codes usados=%d" % (n, len(used)))

    if args.dump is not None:
        print("\n== HR data @0x%x ==" % args.dump)
        print(fmt_hr(b, args.dump))
        return

    codes = args.code if args.code else sorted(used)[:6]
    if args.all:
        codes = sorted(used)
    for code in codes:
        if code not in used:
            print("\n[attack %d] sin entrada" % code)
            continue
        e = used[code]
        print("\n===== attack %d: entradas 0x%x / 0x%x =====" % (code, e[0], e[2]))
        for off in (e[0], e[2]):
            if not off or off >= len(b):
                continue
            pairs, hr_off, n_ap = anim_block(b, off)
            print("  bloque anim @0x%x: parejas=%s" % (off, [(hex(a), m) for a, m in pairs]))
            if not hr_off:
                continue
            print("  lista HR @0x%x (n_ap=%d):" % (hr_off, n_ap))
            for c, t, o in hr_list(b, hr_off)[:10]:
                print("    (code=%#04x type=%#04x) -> @0x%x" % (c, t, o))
                print(fmt_hr(b, o))


if __name__ == "__main__":
    main()