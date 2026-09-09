#!/usr/bin/env python3
"""csk_chain.py - Cadena correcta #CSK: attack code -> anim block -> AP -> HR code -> HR block.

Formato verificado 2026-09-08 contra BSK_breakdown.pdf (IW/B3 PS2, gemelo LE):
  Header 0x20: +0x10 n_entries, +0x14 off lista direcciones (4B/attack code),
                +0x18 n_hr_blocks, +0x1C off seccion HR.
  Bloque de animacion: [anim u16][AMM u16] + params + sentinel FF*12 +
                [pad u32][n_ap u32][ap_addrs_off u32]  (puede haber varios sub-bloques).
  AP addresses: [type u16][n_lines u16][data_off u32] x n_ap.
  AP type 1 (Hit properties): lineas 16B: [frame u16][ID u16][activation u8][HR_code u8]
                [attack_props u16][body u8][radius u8][pos x i8][pos y i8][pos z i8][pad u8].
  Seccion HR: bloques de 8 lineas x 16B indexados por HR code:
                [damage u16][grunt u8][visual u8][stun_type u16][stun_code u16]
                [pushback f32][specific f32].

Uso:
  python csk_chain.py --entry 333 --scan        # lista attack codes con AP1/HR
  python csk_chain.py --entry 333 --code 21b    # cadena de un attack code
  python csk_chain.py --entry 333 --hr 3        # muestra bloque HR 3
"""
import argparse
import os
import struct
import subprocess
import sys
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XBD = os.path.join(
    ROOT,
    "mod center",
    "Xbox 360 Compression - Decompression tool from the XBOX Development Kit",
    "xbdecompress.exe",
)


def lzx(work, cmd, src, dst):
    r = subprocess.run([cmd, src, dst], capture_output=True, timeout=120)
    if not os.path.exists(dst) or os.path.getsize(dst) == 0:
        raise RuntimeError("%s falló: %d" % (os.path.basename(cmd), r.returncode))
    return os.path.getsize(dst)


def get_csk(entry):
    afs = os.path.join(ROOT, "us", "data_cmn.afs")
    with open(afs, "rb") as f:
        f.seek(4)
        count = struct.unpack("<I", f.read(4))[0]
        ents = [struct.unpack("<II", f.read(8)) for _ in range(count)]
        a, s = ents[entry]
        f.seek(a)
        raw = f.read(s)
    work = os.path.join(ROOT, "out", "analysis", "acm", ".work")
    os.makedirs(work, exist_ok=True)
    tok = uuid.uuid4().hex[:8]
    lzx = os.path.join(work, f"in_{tok}.lzx")
    out = os.path.join(work, f"bin_{tok}.bin")
    open(lzx, "wb").write(raw)
    subprocess.run([XBD, lzx, out], capture_output=True, timeout=120)
    data = open(out, "rb").read()
    n = struct.unpack_from(">I", data, 0x10)[0]
    for i in range(n):
        off, size, typ, _ = struct.unpack_from(">IIII", data, 0x20 + i * 0x10)
        if typ == 0xFFFFFFFF:
            return data[off : off + size]
    raise RuntimeError("no #CSK")


def anim_blocks(csk, aoff):
    """Devuelve lista de (anim, amm, n_ap, ap_off) sub-bloques del bloque de animacion."""
    res = []
    p = aoff
    while p + 0x20 < len(csk):
        if csk[p] == 0xFF:
            break
        anim, amm = struct.unpack_from(">HH", csk, p)
        j = csk[p : p + 0x40].find(b"\xff" * 12)
        if j < 0:
            break
        tail = p + j + 12
        if tail + 12 > len(csk):
            break
        _, n_ap, ap_off = struct.unpack_from(">III", csk, tail)
        res.append((anim, amm, n_ap, ap_off))
        # avanzar al siguiente sub-bloque: fin del actual (sentinel+tail) + margen
        p = tail + 12
    return res


def ap_blocks(csk, ap_off, n_ap):
    """Devuelve lista de (type, n_lines, data_off)."""
    res = []
    for i in range(n_ap):
        t, nl, do = struct.unpack_from(">HHI", csk, ap_off + i * 8)
        res.append((t, nl, do))
    return res


def ap1_hr_codes(csk, do, n_lines):
    """De AP type 1 (Hit properties): extrae (frame, ID, act, HR code) de las
    lineas de ventana de golpe. Layout linea 16B:
      [frame u16][ID u16][act u8][pad u8][HR u16][props u16][body u8][radius u8][pos xyz][pad]
    Las lineas de cierre tienen HR=0xFFFF."""
    codes = []
    for i in range(n_lines):
        o = do + i * 16
        if o + 16 > len(csk):
            break
        frame, lid = struct.unpack_from(">HH", csk, o)
        act = csk[o + 4]
        hr = struct.unpack_from(">H", csk, o + 6)[0]
        if hr != 0xFFFF:
            codes.append((frame, lid, act, hr))
    return codes


def hr_block(csk, hr_off, code):
    o = hr_off + code * 128
    if o + 128 > len(csk):
        return None
    lines = []
    for i in range(8):
        l = csk[o + i * 16 : o + i * 16 + 16]
        dmg, grunt, vis = struct.unpack_from(">HBB", l, 0)
        st, sc = struct.unpack_from(">HH", l, 4)
        push, spec = struct.unpack_from(">ff", l, 8)
        lines.append((dmg, grunt, vis, st, sc, push, spec, l.hex()))
    return lines


def attack_hr_chain(csk, code):
    """attack code -> lista de (ap_type, hr_code, frame) con AP type 1."""
    list_off = struct.unpack_from(">I", csk, 0x14)[0]
    aoff = struct.unpack_from(">I", csk, list_off + code * 4)[0]
    if aoff == 0 or aoff >= len(csk):
        return None, None
    hrs = []
    blocks = anim_blocks(csk, aoff)
    for anim, amm, n_ap, ap_off in blocks:
        for t, nl, do in ap_blocks(csk, ap_off, n_ap):
            if t == 1:
                for frame, lid, act, hr in ap1_hr_codes(csk, do, nl):
                    hrs.append((frame, act, hr))
    return aoff, hrs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--entry", type=int, required=True)
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("--code", type=lambda x: int(x, 16), default=None)
    ap.add_argument("--hr", type=lambda x: int(x, 16), default=None)
    args = ap.parse_args()

    csk = get_csk(args.entry)
    n = struct.unpack_from(">I", csk, 0x10)[0]
    list_off = struct.unpack_from(">I", csk, 0x14)[0]
    n_hr = struct.unpack_from(">I", csk, 0x18)[0]
    hr_off = struct.unpack_from(">I", csk, 0x1C)[0]
    print("entry %d: #CSK %d B | n_attack=%d list@%#x | n_hr=%d hr@%#x"
          % (args.entry, len(csk), n, list_off, n_hr, hr_off))

    if args.hr is not None:
        lines = hr_block(csk, hr_off, args.hr)
        if not lines:
            print("HR block %d fuera de rango" % args.hr)
            return
        names = ["normal", "counter", "juggle", "back", "grounded", "blocking", "??", "stunned"]
        print("== HR block %d ==" % args.hr)
        for i, (dmg, g, v, st, sc, push, spec, hx) in enumerate(lines):
            print("  line%d %-9s dmg=%-4d grunt=%02x vis=%02x type=%02x code=%04x push=%6.2f spec=%7.2f"
                  % (i + 1, names[i], dmg, g, v, st, sc, push, spec))
        return

    if args.code is not None:
        aoff, hrs = attack_hr_chain(csk, args.code)
        if aoff is None:
            print("attack %#x: sin bloque de animacion" % args.code)
            return
        print("== attack %#x: anim block @%#x ==" % (args.code, aoff))
        for anim, amm, n_ap, ap_off in anim_blocks(csk, aoff):
            print("  sub-bloque anim=%d amm=%d n_ap=%d ap@%#x" % (anim, amm, n_ap, ap_off))
            for t, nl, do in ap_blocks(csk, ap_off, n_ap):
                tag = {0: "headtrack", 1: "HIT", 2: "airborne", 3: "turnaround",
                       4: "speed", 5: "limb", 6: "hands", 7: "misc"}.get(t, "?")
                print("    AP type %d (%s) %d lineas @%#x" % (t, tag, nl, do))
        if hrs:
            print("  HR codes (linea activa):")
            for frame, act, hr in hrs:
                lines = hr_block(csk, hr_off, hr)
                if lines:
                    d0 = lines[0][0]
                    print("    frame=%d act=%d HR=%d (linea normal dmg=%d)" % (frame, act, hr, d0))
        else:
            print("  (sin AP type 1 -> no es un ataque con hitbox)")
        return

    if args.scan:
        print("== Attack codes con AP type 1 (ataques reales) ==")
        found = 0
        for code in range(n):
            aoff = struct.unpack_from(">I", csk, list_off + code * 4)[0]
            if aoff == 0 or aoff >= len(csk):
                continue
            hrs = attack_hr_chain(csk, code)[1]
            if hrs:
                hrset = sorted({h[2] for h in hrs})
                print("  code %#04x (%3d): HR=%s" % (code, code, hrset))
                found += 1
        print("total con hitbox: %d" % found)


if __name__ == "__main__":
    main()