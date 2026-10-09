#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ps2hd.py - Conversor de bins PS2 (Budokai 3 GH / ports comunitarios, little-endian)
al formato HD 360 (big-endian) bloque a bloque.

La HD usa la numeracion de data_cmn de la PS2 GH: (PS2 n, HD n) son el MISMO contenido.
Cada manejador se valida byte a byte contra esos pares (ps2hd_validate.py).

Contenedor #AMB (PS2 y HD comparten layout, cambia endianness/version/alineacion):
  +0 magic +4 0x20 +8 0 +0xC version (PS2 3, HD 2) +0x10 n +0x14 0x20 (tabla)
  +0x18 inicio de datos (PS2 align 16, HD align 32) ; tabla n x (off, size, type, 0)
  hijos alineados a 16 (PS2) / 32 (HD).

Uso:  python ps2hd.py <in_ps2.bin> <out_hd.bin>
"""
import struct
import sys

# magic PS2 -> magic HD (renombrados en la HD)
MAGIC = {
    b"#AMB": b"#AMB", b"#AMO": b"#AWO", b"#AMG": b"#AWG", b"#AMT": b"#AZT",
    b"#AMM": b"#ACM", b"#BSK": b"#CSK", b"#BCM": b"#CCM", b"#AMC": b"#ACC",
    b"#AML": b"#ACL", b"#AME": b"#ACE", b"#AST": b"#CST", b"#SPX": b"#SPX",
    b"#AMP": b"#ACP",        # BSP de Goku 533 y Dabura 523 (y el de Dabura de IW): todo u32
}


class Unsupported(Exception):
    pass


def align(x, a):
    return (x + a - 1) // a * a


def swap32_all(b, start=4):
    """Todas las palabras u32/f32 a BE desde `start` (los bytes de cola < 4 se copian)."""
    n = (len(b) - start) // 4
    out = bytearray(b)
    if n:
        out[start:start + 4 * n] = struct.pack(">%dI" % n, *struct.unpack("<%dI" % n, b[start:start + 4 * n]))
    return out


def conv_container(b):
    n, tbl = struct.unpack("<II", b[0x10:0x18])
    kids = [list(struct.unpack("<4I", b[tbl + 16 * k:tbl + 16 * k + 16])) for k in range(n)]
    # Algunos ports comunitarios traen un TAMANO falso (mayor que el hueco hasta el
    # siguiente hijo; la PS2 solo usa los offsets): se recorta al siguiente offset.
    offs = sorted({k[0] for k in kids if k[1]} | {len(b)})
    for k in kids:
        if k[1]:
            nxt = next(o for o in offs if o > k[0])
            k[1] = min(k[1], nxt - k[0])
    data_start = align(0x20 + 16 * n, 32)
    body = bytearray()
    entries = []
    pos = data_start
    for off, size, typ, _ in kids:
        child = convert_block(b[off:off + size]) if size else b""
        pos = align(pos, 32)
        entries.append((pos, len(child), typ, 0))
        body += bytes(pos - data_start - len(body)) + child
        pos += len(child)
    hdr = bytearray(b"#AMB" + struct.pack(">7I", 0x20, 0, 2, n, 0x20, data_start, 0))
    for e in entries:
        hdr += struct.pack(">4I", *e)
    hdr += bytes(data_start - len(hdr))
    out = hdr + body
    # el HD alinea el final del contenedor a 32
    out += bytes(align(len(out), 32) - len(out))
    return out


def conv_copy(b):
    return MAGIC.get(b[:4], b[:4]) + b[4:]


def conv_u32(b):
    out = swap32_all(b)
    out[:4] = MAGIC[b[:4]]
    return bytes(out)


NAME_CHARS = set(b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ -.")


def name_len(b, o):
    """Si en o (alineado a 16) empieza un nombre ASCII terminado en NUL, devuelve los
    bytes a copiar tal cual (hasta el NUL, redondeado a 4); si no, 0. Un "nombre"
    de <= 4 letras cuya palabra es un float LE plausible ('fffA' = 14.4f) no cuenta."""
    if not (65 <= b[o] <= 90 or 97 <= b[o] <= 122):
        return 0
    z = b.find(b"\0", o, o + 64)
    if z < 0 or z - o < 3 or not all(x in NAME_CHARS for x in b[o:z]):
        return 0
    if z - o <= 4 and 1e-6 < abs(struct.unpack("<f", b[o:o + 4])[0]) < 1e6:
        return 0
    return align(z - o + 1, 4)


def conv_u32_names(b):
    """u32/f32 BE salvo los nombres ASCII (empiezan alineados a 16) que se copian."""
    out = swap32_all(b)
    out[:4] = MAGIC[b[:4]]
    for o in range(16, len(b) - 3, 16):
        n = name_len(b, o)
        if n:
            out[o:o + n] = b[o:o + n]
    return bytes(out)


def apply_kinds(b, spans):
    """spans: [(ini, fin, kind)] kind 4 = u32/f32, 2 = u16, 1 = bytes. Resto: u32."""
    out = bytearray(swap32_all(b, 0))
    for a, z, k in spans:
        z = min(z, len(b))
        if k == 2:
            n = (z - a) // 2
            out[a:a + 2 * n] = struct.pack(">%dH" % n, *struct.unpack("<%dH" % n, b[a:a + 2 * n]))
        elif k == 1:
            out[a:z] = b[a:z]
    return out


def conv_amm(b):
    """#AMM (PS2) -> #ACM (HD).
    PS2: [cabecera][tabla n_anim x (flags, variante, frames, off)] y despues, intercalados,
    los bloques por hueso de cada animacion (n_bones x [ptr_ang, ptr_pos(, ptr_esc)]) y
    sus pistas. HD: los bloques por hueso van TODOS seguidos tras la tabla (en orden de
    animacion) y luego las pistas en el mismo orden y con las mismas distancias que en la
    PS2 (se quitan los huecos de los bloques). Pistas angulares (frame u16 + 3 x u16) en
    u16; posicionales (frame u32 + 3 f32) y el resto en u32. Se alinea a 32."""
    n_anim, tbl, n_bones, names_off = struct.unpack("<4I", b[0x10:0x20])
    tbl_end = tbl + 16 * n_anim
    entries = [struct.unpack("<4I", b[tbl + 16 * k:tbl + 16 * k + 16]) for k in range(n_anim)]
    blocks = {}                                   # off PS2 -> (per, tamano)
    for flags, _, _, off in entries:
        if off and off not in blocks:
            per = 3 if flags & 0x10 else 2
            blocks[off] = (per, 4 * per * n_bones)
    order = [off for _, _, _, off in entries if off]
    order = list(dict.fromkeys(order))
    new_block = {}
    pos = tbl_end
    for off in order:
        new_block[off] = pos
        pos += blocks[off][1]
    data_base = pos
    spans = sorted((off, off + blocks[off][1]) for off in blocks)

    def remap(p):                                  # offset de datos PS2 -> HD
        removed = sum(z - a for a, z in spans if z <= p)
        return data_base + (p - tbl_end - removed)

    kinds = {}                                     # inicio de pista PS2 -> 2 (u16) | 4
    for off in order:
        per = blocks[off][0]
        for j in range(n_bones):
            for i in range(per):
                p = struct.unpack("<I", b[off + 4 * (per * j + i):off + 4 * (per * j + i) + 4])[0]
                if p:
                    kinds.setdefault(p, 2 if i == 0 else 4)
    # flujo de datos sin los bloques
    data = bytearray()
    cut = tbl_end
    for a, z in spans:
        data += b[cut:a]
        cut = z
    data += b[cut:]
    # +0x1C = inicio de la tabla final de nombres de hueso (n_bones x 32 B, texto plano)
    names = names_off if tbl_end <= names_off < len(b) else len(b)
    starts = sorted(set(kinds) | {names, len(b)})
    dspans = []
    if names < len(b):
        dspans.append((remap(names) - data_base, len(data), 1))
    for a, z in zip(starts, starts[1:]):
        if kinds.get(a) == 2:
            # pista = [u32 0, u32 tipo, u32 n_claves] + claves (u16 en las angulares)
            dspans.append((remap(a) - data_base + 12,
                           remap(z) - data_base if z < len(b) else len(data), 2))
    out = bytearray(swap32_all(b[:tbl_end], 0))
    out[:4] = b"#ACM"
    for k, (flags, var, nfr, off) in enumerate(entries):
        struct.pack_into(">4I", out, tbl + 16 * k, flags, var, nfr, new_block[off] if off else 0)
    for off in order:
        per = blocks[off][0]
        vals = struct.unpack("<%dI" % (per * n_bones), b[off:off + blocks[off][1]])
        out += struct.pack(">%dI" % len(vals), *[remap(v) if v else 0 for v in vals])
    out += apply_kinds(bytes(data), dspans)
    return bytes(out) + bytes(align(len(out), 32) - len(out))


def conv_bsk(b):
    """#BSK (PS2) -> #CSK (HD). Esquema inferido por votacion sobre el corpus (bsk_infer):
    cabecera/lista de direcciones u32; sub-bloque de animacion 48 B = [anim u16][amm u16]
    + 11 u32/f32; AP addresses [tipo u16][n u16][off u32]; linea AP 16 B =
    [frame u16][ID u8][act u8] + u32 (+ en tipo 1: [u16][u16] y 4 bytes de hitbox);
    bloque HR 8 x [u16 dano][u16 grunt/visual][u16 tipo][u16 codigo][f32][f32]."""
    n, lst, n_hr, hr = struct.unpack("<4I", b[0x10:0x20])
    spans = []
    addrs = sorted({a for a in struct.unpack("<%dI" % n, b[lst:lst + 4 * n]) if a})
    aset = set(addrs)
    aps = set()
    for a in addrs:
        s = a
        while s + 48 <= hr:
            pad, n_ap, ap_off = struct.unpack("<3I", b[s + 0x24:s + 0x30])
            if pad != 0 or not (0 <= n_ap < 64) or (n_ap and not (hr > ap_off >= 0x20)):
                break
            spans.append((s, s + 4, 2))
            if n_ap:
                aps.add((ap_off, n_ap))
            s += 48
            if s in aset:
                break
    for ap_off, n_ap in aps:
        for k in range(n_ap):
            e = ap_off + 8 * k
            t, nl, do = struct.unpack("<HHI", b[e:e + 8])
            spans.append((e, e + 4, 2))
            if nl > 512 or do + 16 * nl > len(b):
                continue
            for ln in range(nl):
                lo = do + 16 * ln
                spans.append((lo, lo + 2, 2))
                spans.append((lo + 2, lo + 4, 1))
                if t == 1:
                    spans.append((lo + 8, lo + 12, 2))
                    spans.append((lo + 12, lo + 16, 1))
    for k in range(n_hr):
        for ln in range(8):
            lo = hr + 128 * k + 16 * ln
            spans.append((lo, lo + 8, 2))
            # "especifico": f32, salvo knockaway (tipo 2) = 2 x s16 (direccion)
            if lo + 6 <= len(b) and struct.unpack("<H", b[lo + 4:lo + 6])[0] == 2:
                spans.append((lo + 12, lo + 16, 2))
    out = apply_kinds(b, spans)
    out[:4] = b"#CSK"
    return bytes(out)


# tipos por palabra del bloque de movimiento BCM (0x40 B), inferidos en el corpus:
# '22' = 2 x u16, '4' = u32, '11.2' = 2 bytes + u16, '11' = 4 bytes
BCM_BLOCK = {0: "22", 4: "22", 8: "4", 12: "22", 16: "22", 20: "22", 24: "22", 28: "22",
             32: "4", 36: "22", 40: "22", 44: "11.2", 48: "22", 52: "22", 56: "11", 60: "4"}


def word_spans(o, kind):
    if kind == "22":
        return [(o, o + 4, 2)]
    if kind == "11":
        return [(o, o + 4, 1)]
    if kind == "11.2":
        return [(o, o + 2, 1), (o + 2, o + 4, 2)]
    if kind == "2.11":
        return [(o, o + 2, 2), (o + 2, o + 4, 1)]
    return []


def conv_bcm(b):
    """#BCM (PS2) -> #CCM (HD): cabecera 0x50 (u32 en +4/+8, u16 el resto; n starters
    u16 en +0x1E), lista de starters u32 en +0x50 y arbol de bloques de 0x40 B
    (BCM_BLOCK) seguidos de n_ramas (u16 en +0x0E) offsets u32 de los movimientos
    siguientes."""
    spans = [(0x0C, 0x50, 2)]
    n = struct.unpack("<H", b[0x1E:0x20])[0]
    todo = [struct.unpack("<I", b[0x50 + 4 * k:0x54 + 4 * k])[0] for k in range(n)]
    seen = set()
    while todo:
        b0 = todo.pop()
        if b0 in seen or not (0x50 <= b0 <= len(b) - 0x40):
            continue
        seen.add(b0)
        for w, kind in BCM_BLOCK.items():
            spans += word_spans(b0 + w, kind)
        nb = struct.unpack("<H", b[b0 + 0x0E:b0 + 0x10])[0]
        for k in range(min(nb, 64)):
            q = b0 + 0x40 + 4 * k
            if q + 4 <= len(b):
                todo.append(struct.unpack("<I", b[q:q + 4])[0])
    out = apply_kinds(b, spans)
    out[:4] = b"#CCM"
    return bytes(out)


def conv_amt(b):
    """#AMT (PS2, paletizadas 4/8 bpp con cabecera GIF) -> #AZT (HD, DDS + DXT3)."""
    import amt_ps2
    for t in amt_ps2.entries(b):
        if t["psm"] not in (0x13, 0x14):
            raise Unsupported(b"#AMT psm=%#x" % t["psm"])
    return amt_ps2.to_azt(b)


# #AST (ataques de energia del BSP: bloques "wk" de 0xF0) y #ASE (bloques de 0xD0):
# cabecera [magic][codigo de personaje, 4 B texto][n u32][inicio u32]; tipos por posicion
# del bloque inferidos por votacion sobre los 45/46 pares nativos (sin conflictos).
# Posiciones no listadas: u32 (en el corpus son ceros o simetricas).
AST_WORDS = {0: "11", 4: "11", 8: "11", 12: "11"}
AST_WORDS.update({o: "22" for o in (
    0x10, 0x18, 0x20, 0x30, 0x34, 0x38, 0x48, 0x58, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74, 0x78,
    0x7C, 0x80, 0x84, 0x8C, 0x90, 0x94, 0x98, 0x9C, 0xA0, 0xAC, 0xB0, 0xB4, 0xB8, 0xC0, 0xDC, 0xE0)})
ASE_WORDS = {0: "11", 4: "11", 8: "11", 12: "11"}
ASE_WORDS.update({o: "22" for o in (0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74,
                                     0xA8, 0xAC, 0xB0)})


def conv_wk_table(b, words, magic_hd):
    """#AST/#ASE: cabecera + n bloques de tamano fijo ((len - inicio) / n)."""
    n, start = struct.unpack("<II", b[8:16])
    spans = [(4, 8, 1)]                       # codigo del personaje ("16G\0"): texto
    if n:
        bs = (len(b) - start) // n
        for k in range(n):
            o0 = start + k * bs
            for w, kind in words.items():
                if w < bs:
                    spans += word_spans(o0 + w, kind)
    out = apply_kinds(b, spans)
    out[:4] = magic_hd
    return bytes(out)


# #ATR (bloques de 0xB0): +0x20..+0x2C son punteros PS2 de runtime que la HD copia tal cual
ATR_WORDS = {o: "11" for o in (0, 4, 8, 12, 0x20, 0x24, 0x28, 0x2C)}
ATR_WORDS.update({o: "22" for o in (0x80, 0x84, 0x88, 0x8C, 0x90, 0x94, 0x98, 0xA0)})


def conv_atr(b):
    return conv_wk_table(b, ATR_WORDS, b"#CTR")


def conv_ast(b):
    return conv_wk_table(b, AST_WORDS, b"#CST")


def conv_ase(b):
    return conv_wk_table(b, ASE_WORDS, b"#CSE")


# #AWV (bloques 0xB0, en el #AMB de los #ASE de casi todos los BSP; tambien en los ports IW de la
# comunidad): nombre "wk0" y u16 en +0x50..+0x5F; el resto u32/f32. Medido en los 28 nativos.
AWV_WORDS = {0: "11", 0x50: "22", 0x54: "22", 0x58: "22", 0x5C: "22"}


def conv_awv(b):
    return conv_wk_table(b, AWV_WORDS, b"#CWV")


def conv_amp(b):
    """#AMP -> #ACP (BSP de Goku 533 y Dabura 523; el de Dabura de IW es el mismo). Cabecera:
    +0x10 grupos, +0x1C tabla de nombres; grupo (16 B en 0x20+) = [.., .., .., lista de offsets];
    cada offset apunta a una entrada de 20 B cuya ultima palabra son dos u16; los nombres
    (ASCII) van tal cual; el resto, u32."""
    out = swap32_all(b)
    out[:4] = b"#ACP"
    ng, names = struct.unpack_from("<I", b, 0x10)[0], struct.unpack_from("<I", b, 0x1C)[0]
    entries = set()
    for g in range(min(ng, 64)):
        pos = struct.unpack_from("<I", b, 0x2C + 16 * g)[0]
        stop = len(b)
        while 0 < pos < min(stop, len(b) - 3):      # la lista acaba donde empieza la 1a entrada
            v = struct.unpack_from("<I", b, pos)[0]
            if v and v + 20 <= len(b):
                entries.add(v)
                stop = min(stop, v)
            pos += 4
    for e in entries:
        out[e + 16:e + 20] = struct.pack(">2H", *struct.unpack_from("<2H", b, e + 16))
    o = names
    while 0 < o < len(b):                           # nombres: hasta su NUL, redondeado a 4
        if 65 <= b[o] <= 90:
            z = b.find(b"\0", o)
            z = len(b) if z < 0 else z
            end = min(len(b), (z + 4) & ~3)
            out[o:end] = b[o:end]
            o = end
        else:
            o += 4
    return bytes(out)


HANDLERS = {
    b"#AST": conv_ast,
    b"#ATR": conv_atr,
    b"#ASE": conv_ase,
    b"#AWV": conv_awv,
    b"#AMT": conv_amt,
    b"#BCM": conv_bcm,
    b"#BSK": conv_bsk,
    b"#AMM": conv_amm,
    b"#AMB": conv_container,
    b"#SPX": conv_copy,
    b"#AML": conv_copy,
    b"#AMC": conv_u32,
    b"#AMP": conv_amp,
    b"#AME": conv_u32_names,
}


def conv_amo(b):
    import amo2awo  # noqa: PLC0415 (import tardio: amo2awo importa numpy)
    return amo2awo.convert_amo(bytes(b))


HANDLERS[b"#AMO"] = conv_amo


def convert_block(b):
    h = HANDLERS.get(b[:4])
    if h is None:
        # relleno de algunos ports de la comunidad (hijo de 5 B a cero): no hay nada que girar
        if len(b) < 16 and not any(b):
            return bytes(b)
        raise Unsupported(b[:4])
    return h(b)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src = open(sys.argv[1], "rb").read()
    out = convert_block(src)
    open(sys.argv[2], "wb").write(out)
    print("OK %s -> %s (%d -> %d B)" % (sys.argv[1], sys.argv[2], len(src), len(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
