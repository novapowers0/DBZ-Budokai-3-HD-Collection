#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""sb_amm.py - Animaciones de Shin Budokai (PSP, #AMM comprimido) -> #AMM de Budokai 3 (PS2).

RE 2026-10-04 (Gohan del Futuro, Another Road; validado posando el modelo convertido):

  Shin Budokai  cabecera igual (+0x10 n_anim, +0x14 tabla, +0x18 n_huesos, +0x1C nombres)
                tabla de 8 B por animacion: [u16 fotogramas][u16 opciones][u32 huesos]
                huesos: n_huesos x [u32 n_pistas][u32 ptr]
                 pista 16 B: [u16 canal (0 giro, 1 posicion, 2 escala)][u16 ancho]
                             [u16 n_claves][u16 tipo][u32 ptr fotogramas][u32 ptr valores]
                 ancho 0/1/2 = fotogramas u8/u16/u32 (RE SB2 2026-11-03:
                 consumidor 0x13c78c, tabla 0x26c5e4, lectores 0x13b8a8/0x13b9ac/0x13bac0).
                 SB1: consumidor 0x1023c, tabla 0x12f5b4, lectores 0xf358/0xf45c/0xf570.
                 La lectura ampliada es experimental y requiere wide_frames=True;
                 por defecto se conserva la conversion anterior, pendiente de aceptacion en juego.
                valores: giro 3 x s16 (65536 = 360 grados, orden X-Y-Z), posicion/escala 3 x f32
  Budokai 3     tabla de 16 B: [flags (9, o 25 con escala)][0][fotogramas][ptr bloque]
                bloque: n_huesos x [giro, posicion(, escala)] punteros a pistas
                pista: [u32 0][u32 tipo][u32 n] + claves (giro: u16 fotograma + 3 x u16;
                posicion/escala: u32 fotograma + 3 x f32)
  Las posiciones son, en los dos, un desplazamiento sobre la del hueso en reposo.

El resultado se pasa a HD con ps2hd.conv_amm (validado byte a byte con los nativos).
Uso:  python sb_amm.py <ANM de Shin Budokai (#AMB)> <salida ANM PS2 de B3>
"""
import struct
import sys


def is_sb(b):
    """#AMM de Shin Budokai: la tabla es de 8 B (los huesos de la 1a animacion empiezan justo
    detras) en vez de 16."""
    na, t = struct.unpack("<II", b[0x10:0x18])
    if na == 0:
        return False
    first = [struct.unpack_from("<HHI", b, t + 8 * a)[2] for a in range(na)]
    nz = [x for x in first if x]
    return bool(nz) and min(nz) == t + 8 * na


def convert(b, *, wide_frames=False):
    na, t, nbo, noff = struct.unpack("<4I", b[0x10:0x20])
    names = b[noff:noff + 32 * nbo]
    anims = []
    for a in range(na):
        nf, opt, tab = struct.unpack_from("<HHI", b, t + 8 * a)
        bones = []
        scale = False
        for j in range(nbo):
            tr = [None, None, None]
            if tab:
                cnt, ptr = struct.unpack_from("<II", b, tab + 8 * j)
                for r in range(cnt):
                    cls, nk, typ, fp, vp = struct.unpack_from("<IHHII", b, ptr + 16 * r)
                    if wide_frames and nk:
                        width, cls = cls >> 16, cls & 0xFFFF
                        if cls > 2 or width > 2:
                            raise ValueError("AMM PSP: canal/ancho no soportado: %d/%d" % (cls, width))
                    if cls > 2 or not nk:
                        continue
                    if wide_frames:
                        if fp + nk * (1 << width) > len(b) or vp + nk * (6 if cls == 0 else 12) > len(b):
                            raise ValueError("AMM PSP: claves fuera del archivo")
                        if tr[cls] is not None:
                            raise ValueError("AMM PSP: canal repetido en un hueso")
                        fr = struct.unpack_from("<%d%s" % (nk, "BHI"[width]), b, fp)
                    else:
                        fr = b[fp:fp + nk]
                    if cls == 0:
                        keys = [struct.pack("<H", fr[k]) + b[vp + 6 * k:vp + 6 * k + 6] for k in range(nk)]
                    else:
                        keys = [struct.pack("<I", fr[k]) + b[vp + 12 * k:vp + 12 * k + 12] for k in range(nk)]
                    tr[cls] = struct.pack("<3I", 0, typ, nk) + b"".join(keys)
                    scale |= cls == 2
            bones.append(tr)
        anims.append((nf, scale, bones, bool(tab)))
    out = bytearray(b[:0x20])
    out += bytes(16 * na)
    for a, (nf, scale, bones, present) in enumerate(anims):
        if not present:
            continue
        per = 3 if scale else 2
        while len(out) % 16:
            out += b"\0"
        blk = len(out)
        out += bytes(4 * per * nbo)
        for j, tr in enumerate(bones):
            for i in range(per):
                if tr[i] is None:
                    continue
                while len(out) % 4:
                    out += b"\0"
                struct.pack_into("<I", out, blk + 4 * (per * j + i), len(out))
                out += tr[i]
        struct.pack_into("<4I", out, 0x20 + 16 * a, 0x19 if scale else 0x9, 0, nf, blk)
    while len(out) % 16:
        out += b"\0"
    struct.pack_into("<I", out, 0x14, 0x20)
    struct.pack_into("<I", out, 0x1C, len(out))
    out += names
    return bytes(out)


def convert_anm(d, *, wide_frames=False):
    """#AMB de animaciones (BSK + AMMs): convierte los AMM de Shin Budokai."""
    n, tbl = struct.unpack("<II", d[0x10:0x18])
    kids = [list(struct.unpack("<4I", d[tbl + 16 * k:tbl + 16 * k + 16])) for k in range(n)]
    parts = []
    for off, size, typ, z in kids:
        blk = d[off:off + size] if size else b""
        if blk[:4] == b"#AMM" and is_sb(blk):
            blk = convert(blk, wide_frames=wide_frames)
        parts.append((blk, typ, z))
    out = bytearray(d[:0x20])
    start = (0x20 + 16 * n + 15) // 16 * 16
    out += bytes(start - len(out))
    ents = []
    for blk, typ, z in parts:
        while len(out) % 16:
            out += b"\0"
        ents.append((len(out) if blk else 0, len(blk), typ, z))
        out += blk
    for k, e in enumerate(ents):
        struct.pack_into("<4I", out, tbl + 16 * k, *e)
    struct.pack_into("<I", out, 0x18, start)
    return bytes(out)


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("origen")
    ap.add_argument("salida")
    ap.add_argument("--fotogramas-amplios", action="store_true",
                    help="experimental: conserva pistas con fotogramas u16/u32; apagado por defecto")
    args = ap.parse_args()
    open(args.salida, "wb").write(convert_anm(open(args.origen, "rb").read(),
                                            wide_frames=args.fotogramas_amplios))
    print("ok", args.salida)
