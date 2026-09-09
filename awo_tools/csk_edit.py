#!/usr/bin/env python3
"""csk_edit.py - Editar el DAÑO de una habilidad en el #CSK HD (formato CORRECTO).

Cadena verificada 2026-09-08 (BSK_breakdown.pdf + dif contra BSK PS2):
  attack code -> bloque de animacion (lista de 4B en +0x14) -> AP addresses
  (types 0-7) -> AP type 1 (Hit properties) -> HR code (u16 en bytes 6-7 de
  cada linea) -> bloque HR en hr_off + code*128 (8 lineas x 16B):
    [damage u16][grunt u8][visual u8][stun_type u16][stun_code u16]
    [pushback f32][specific f32]

Lineas del bloque HR (situaciones de hit):
  1 normal | 2 counter | 3 juggle | 4 back | 5 grounded | 6 blocking |
  7 ?? | 8 stunned

Uso:
  python csk_edit.py --entry 333 --attack 3c --damage 80 --yes
      cambia el daño de TODAS las lineas de daño de los bloques HR usados por
      el attack code 0x3c (lineas 1,2,3,4,5,7,8).
  python csk_edit.py --entry 333 --attack 21b --damage 40 --line 1 2 --yes
      solo las lineas 1 y 2 (normal + counter).
  python csk_edit.py --entry 333 --hr 5c --damage 80 --yes
      parchea el bloque HR 0x5c directamente.
  python csk_edit.py --entry 333 --attack 3c --damage 80 --mod krillin_dmg --install
      instala como override mods/<mod>/us/data_cmn.afs/<entry>/geom.bin (LZX).
"""
import argparse
import os
import struct
import subprocess
import sys
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import csk_chain as cc

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XBC = os.path.join(
    ROOT,
    "mod center",
    "Xbox 360 Compression - Decompression tool from the XBOX Development Kit",
    "xbcompress.exe",
)
DAMAGE_LINES = (1, 2, 3, 4, 5, 7, 8)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--entry", type=int, required=True)
    ap.add_argument("--attack", type=lambda x: int(x, 16), default=None)
    ap.add_argument("--hr", type=lambda x: int(x, 16), default=None)
    ap.add_argument("--damage", type=int, required=True)
    ap.add_argument("--line", nargs="*", type=int, default=None)
    ap.add_argument("--out", default=None)
    ap.add_argument("--mod", default=None)
    ap.add_argument("--install", action="store_true")
    ap.add_argument("--yes", action="store_true")
    args = ap.parse_args()

    # extraer #CSK
    afs = os.path.join(ROOT, "us", "data_cmn.afs")
    with open(afs, "rb") as f:
        f.seek(4)
        count = struct.unpack("<I", f.read(4))[0]
        ents = [struct.unpack("<II", f.read(8)) for _ in range(count)]
        a, s = ents[args.entry]
        f.seek(a)
        raw = f.read(s)
    work = os.path.join(ROOT, "out", "analysis", "acm", ".work")
    os.makedirs(work, exist_ok=True)
    tok = uuid.uuid4().hex[:8]
    lzx = os.path.join(work, f"in_{tok}.lzx")
    bin_path = os.path.join(work, f"bin_{tok}.bin")
    open(lzx, "wb").write(raw)
    cc.lzx(work, cc.XBD, lzx, bin_path)
    data = open(bin_path, "rb").read()
    n_sub = struct.unpack_from(">I", data, 0x10)[0]
    csoff = cssize = None
    for i in range(n_sub):
        off, size, typ, _ = struct.unpack_from(">IIII", data, 0x20 + i * 0x10)
        if typ == 0xFFFFFFFF:
            csoff, cssize = off, size
            break
    if csoff is None:
        raise SystemExit("no #CSK")
    csk = bytearray(data[csoff : csoff + cssize])
    n_hr = struct.unpack_from(">I", csk, 0x18)[0]
    hr_off = struct.unpack_from(">I", csk, 0x1C)[0]
    print("entry %d: #CSK %d B | n_hr=%d hr@%#x" % (args.entry, len(csk), n_hr, hr_off))

    if args.hr is not None:
        hr_codes = [args.hr]
        src = "HR %#x" % args.hr
    elif args.attack is not None:
        aoff, hrs = cc.attack_hr_chain(csk, args.attack)
        if aoff is None:
            raise SystemExit("attack %#x: sin bloque de animacion" % args.attack)
        hr_codes = sorted({h[2] for h in hrs})
        if not hr_codes:
            raise SystemExit("attack %#x: sin AP type 1 (no es un ataque con hitbox)" % args.attack)
        src = "attack %#x (anim@%#x)" % (args.attack, aoff)
    else:
        raise SystemExit("necesito --attack o --hr")

    lines = args.line if args.line else list(DAMAGE_LINES)
    if not args.yes:
        print("HR codes afectados: %s" % [hex(c) for c in hr_codes])
        print("lineas: %s -> damage %d" % (lines, args.damage))
        r = input("Aplicar? [y/N] ")
        if r.lower() != "y":
            raise SystemExit("cancelado")

    patched_blocks = 0
    for code in hr_codes:
        blk = hr_off + code * 128
        if blk + 128 > len(csk):
            print("  HR %#x fuera de rango, saltado" % code)
            continue
        for ln in lines:
            if ln < 1 or ln > 8:
                continue
            o = blk + (ln - 1) * 16
            cur = struct.unpack_from(">H", csk, o)[0]
            struct.pack_into(">H", csk, o, args.damage)
            print("  HR %#x line%d dmg %d -> %d" % (code, ln, cur, args.damage))
        patched_blocks += 1
    print("bloques HR parcheados: %d (%s)" % (patched_blocks, src))

    if args.out:
        open(args.out, "wb").write(csk)
        print("#CSK patcheado -> %s" % args.out)

    if args.install:
        if not args.mod:
            raise SystemExit("--install necesita --mod")
        assert len(csk) == cssize
        patched = bytearray(data)
        patched[csoff : csoff + cssize] = csk
        dev_mods = os.path.join(ROOT, "out", "build", "win-amd64-release", "mods")
        mods_root = dev_mods if os.path.isdir(os.path.dirname(dev_mods)) else os.path.join(ROOT, "mods")
        moddir = os.path.join(mods_root, args.mod, "us", "data_cmn.afs", str(args.entry))
        os.makedirs(moddir, exist_ok=True)
        with open(os.path.join(mods_root, args.mod, "manifest.txt"), "w") as f:
            f.write("mod: %s\nentry: %d\nattack: %s\nhr: %s\ndamage: %d\nlines: %s\n"
                    % (args.mod, args.entry,
                       hex(args.attack) if args.attack is not None else "-",
                       [hex(c) for c in hr_codes], args.damage, lines))
        pbin = os.path.join(work, f"patched_{tok}.bin")
        open(pbin, "wb").write(patched)
        lzx_dst = os.path.join(moddir, "geom.bin.lzx")
        cc.lzx(work, XBC, pbin, lzx_dst)
        os.replace(lzx_dst, os.path.join(moddir, "geom.bin"))
        print("override instalado: %s/geom.bin" % os.path.relpath(moddir, ROOT))


if __name__ == "__main__":
    main()