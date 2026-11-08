#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""hdps2.py - Inverso de ps2hd.py para lo que los importadores leen del donante en formato PS2
(#AMB de animaciones y de camaras: #ACM, #CSK, #CCM, #ACC, #ACL, #SPX), sacado de la HD del jugador.

Asi importar B1/B2 no necesita el Budokai 3 de PS2 (#72): afs_pair.ps2(n) cae aqui si no lo hay.
Autoprueba: HD -> PS2 -> HD da el mismo contenido en todos los donantes, y frente a la PS2 real solo
difieren las animaciones que la propia HD retoco (sale la version HD, la que el jugador ve en su juego).

  python hdps2.py --autoprueba
"""
import struct
import sys

import ps2hd

MAGIC = {hd: ps2 for ps2, hd in ps2hd.MAGIC.items()}


def align(x, a):
    return (x + a - 1) // a * a


def swap32_all(b, start=4):
    n = (len(b) - start) // 4
    out = bytearray(b)
    if n:
        out[start:start + 4 * n] = struct.pack("<%dI" % n, *struct.unpack(">%dI" % n, b[start:start + 4 * n]))
    return out


def apply_kinds(b, spans):
    """Inverso de ps2hd.apply_kinds: spans en las mismas posiciones (el layout no cambia)."""
    out = bytearray(swap32_all(b, 0))
    for a, z, k in spans:
        z = min(z, len(b))
        if k == 2:
            n = (z - a) // 2
            out[a:a + 2 * n] = struct.pack("<%dH" % n, *struct.unpack(">%dH" % n, b[a:a + 2 * n]))
        elif k == 1:
            out[a:z] = b[a:z]
    return out


def rev_container(b):
    n, tbl = struct.unpack(">II", b[0x10:0x18])
    kids = [struct.unpack(">4I", b[tbl + 16 * k:tbl + 16 * k + 16]) for k in range(n)]
    data_start = align(0x20 + 16 * n, 16)
    body, entries, pos = bytearray(), [], data_start
    for off, size, typ, _ in kids:
        child = rev_block(b[off:off + size]) if size else b""
        pos = align(pos, 16)
        entries.append((pos, len(child), typ, 0))
        body += bytes(pos - data_start - len(body)) + child
        pos += len(child)
    hdr = bytearray(b"#AMB" + struct.pack("<7I", 0x20, 0, 3, n, 0x20, data_start, 0))
    for e in entries:
        hdr += struct.pack("<4I", *e)
    return bytes(hdr + bytes(data_start - len(hdr)) + body)


def rev_copy(b):
    return MAGIC.get(bytes(b[:4]), bytes(b[:4])) + bytes(b[4:])


def rev_u32(b):
    out = swap32_all(b)
    out[:4] = MAGIC[bytes(b[:4])]
    return bytes(out)


def rev_amm(b):
    """#ACM -> #AMM. La PS2 pone cada bloque por hueso JUSTO antes de sus pistas (6305/6305 medidos) y acaba
    en la tabla de nombres (tamano = +0x1C + 32 x huesos, 114/114): con eso el layout PS2 sale exacto."""
    n_anim, tbl, n_bones, names = struct.unpack(">4I", b[0x10:0x20])
    te = tbl + 16 * n_anim
    entries = [struct.unpack(">4I", b[tbl + 16 * k:tbl + 16 * k + 16]) for k in range(n_anim)]
    per_of = {}
    for flags, _, _, off in entries:
        if off:
            per_of.setdefault(off, 3 if flags & 0x10 else 2)
    order = list(per_of)
    size = {o: 4 * per_of[o] * n_bones for o in order}
    data_base = te + sum(size.values())
    ptrs = {o: struct.unpack(">%dI" % (per_of[o] * n_bones), b[o:o + size[o]]) for o in order}
    dp = {o: min(v for v in ptrs[o] if v) - data_base for o in order}   # donde empiezan sus pistas
    by_dp = sorted(order, key=lambda o: dp[o])
    ps2_off, extra = {}, 0
    for o in by_dp:
        ps2_off[o] = te + dp[o] + extra
        extra += size[o]

    def unmap(v):                                      # puntero HD -> PS2 (el bloque va antes de su pista)
        x = v - data_base
        return te + x + sum(size[o] for o in by_dp if dp[o] <= x)

    total = names + 32 * n_bones
    data_len = total - te - sum(size.values())
    data = bytes(b[data_base:data_base + data_len])
    kinds = {}                                         # tipos de las pistas, como ps2hd.conv_amm
    for o in order:
        per = per_of[o]
        for j in range(n_bones):
            for i in range(per):
                v = ptrs[o][per * j + i]
                if v:
                    kinds.setdefault(v - data_base, 2 if i == 0 else 4)
    nm = names - te - sum(size[o] for o in order if ps2_off[o] + size[o] <= names)
    starts = sorted(set(kinds) | {nm, data_len})
    dspans = [(nm, data_len, 1)] if nm < data_len else []
    for a, z in zip(starts, starts[1:]):
        if kinds.get(a) == 2:
            dspans.append((a + 12, z, 2))
    pdata = apply_kinds(data, dspans)
    out = bytearray(swap32_all(b[:te], 0))
    out[:4] = b"#AMM"
    for k, (flags, var, nfr, off) in enumerate(entries):
        struct.pack_into("<4I", out, tbl + 16 * k, flags, var, nfr, ps2_off[off] if off else 0)
    cut = 0
    for o in by_dp:
        out += pdata[cut:dp[o]]
        out += struct.pack("<%dI" % len(ptrs[o]), *[unmap(v) if v else 0 for v in ptrs[o]])
        cut = dp[o]
    out += pdata[cut:]
    return bytes(out)


def rev_bsk(b):
    """#CSK -> #BSK: mismo recorrido que ps2hd.conv_bsk leyendo en BE (las palabras leidas no cambian de tipo)."""
    n, lst, n_hr, hr = struct.unpack(">4I", b[0x10:0x20])
    spans = []
    addrs = sorted({a for a in struct.unpack(">%dI" % n, b[lst:lst + 4 * n]) if a})
    aset = set(addrs)
    aps = set()
    for a in addrs:
        s = a
        while s + 48 <= hr:
            pad, n_ap, ap_off = struct.unpack(">3I", b[s + 0x24:s + 0x30])
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
            t, nl, do = struct.unpack(">HHI", b[e:e + 8])
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
            if lo + 6 <= len(b) and struct.unpack(">H", b[lo + 4:lo + 6])[0] == 2:
                spans.append((lo + 12, lo + 16, 2))
    out = apply_kinds(b, spans)
    out[:4] = b"#BSK"
    return bytes(out)


def rev_bcm(b):
    spans = [(0x0C, 0x50, 2)]
    n = struct.unpack(">H", b[0x1E:0x20])[0]
    todo = [struct.unpack(">I", b[0x50 + 4 * k:0x54 + 4 * k])[0] for k in range(n)]
    seen = set()
    while todo:
        b0 = todo.pop()
        if b0 in seen or not (0x50 <= b0 <= len(b) - 0x40):
            continue
        seen.add(b0)
        for w, kind in ps2hd.BCM_BLOCK.items():
            spans += ps2hd.word_spans(b0 + w, kind)
        nb = struct.unpack(">H", b[b0 + 0x0E:b0 + 0x10])[0]
        for k in range(min(nb, 64)):
            q = b0 + 0x40 + 4 * k
            if q + 4 <= len(b):
                todo.append(struct.unpack(">I", b[q:q + 4])[0])
    out = apply_kinds(b, spans)
    out[:4] = b"#BCM"
    return bytes(out)


HANDLERS = {b"#AMB": rev_container, b"#ACM": rev_amm, b"#CSK": rev_bsk, b"#CCM": rev_bcm,
            b"#ACC": rev_u32, b"#ACL": rev_copy, b"#SPX": rev_copy}


def rev_block(b):
    h = HANDLERS.get(bytes(b[:4]))
    if h is None:
        raise ps2hd.Unsupported(bytes(b[:4]))
    return h(b)


def hd_a_ps2(hd):
    return rev_block(bytes(hd))


def _hijos(b, e):
    n, t = struct.unpack(e + "II", b[0x10:0x18])
    return [bytes(b[o:o + s]) for o, s, _, _ in
            (struct.unpack(e + "4I", b[t + 16 * k:t + 16 * k + 16]) for k in range(n)) if s]


def canon_amm(b, e):
    """Contenido de un #AMM/#ACM sin su layout: por animacion (flags, variante, cuadros) y, por hueso, los bytes
    de cada pista (en el orden de bytes `e`). La HD a veces ordena los bloques distinto que ps2hd: esto lo ignora."""
    n_anim, tbl, n_bones, names = struct.unpack(e + "4I", b[0x10:0x20])
    ents = [struct.unpack(e + "4I", b[tbl + 16 * k:tbl + 16 * k + 16]) for k in range(n_anim)]
    ptr = {}
    for flags, _, _, off in ents:
        if off and off not in ptr:
            per = 3 if flags & 0x10 else 2
            ptr[off] = struct.unpack(e + "%dI" % (per * n_bones), b[off:off + 4 * per * n_bones])
    fin = len(b) if b[:4] == b"#AMM" else len(b.rstrip(bytes(1)))   # la HD rellena a 32 con ceros
    starts = sorted({v for vs in ptr.values() for v in vs if v} | set(ptr) | {fin})
    nxt = dict(zip(starts, starts[1:]))
    return [(f, v, nf, tuple(b[x:nxt[x]] if x else None for x in ptr[o]) if o else None) for f, v, nf, o in ents]


def autoprueba():
    import os  # noqa: PLC0415
    sys.path.append(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mod center hd"))
    import afs_pair  # noqa: PLC0415
    import roster_build as rb  # noqa: PLC0415
    files = sorted({d["anm"][0] for d in rb.DB["ids"] if d.get("anm")} |
                   {d["cam"] for d in rb.DB["ids"] if d.get("cam") is not None})
    hay_ps2 = os.path.exists(afs_pair.PS2_GH)
    n = hijos = iguales = anims = anims_iguales = 0
    otros = []
    for f in files:
        h = bytes(afs_pair.hd(f))
        try:
            p = hd_a_ps2(h)
        except ps2hd.Unsupported:   # entradas de la tabla que no son animaciones/camaras (modelos)
            otros.append(f)
            continue
        n += 1
        vuelta = ps2hd.convert_block(p)   # ida y vuelta: mismo contenido que la HD
        for a, b in zip(_hijos(vuelta, ">"), _hijos(h, ">")):
            igual = canon_amm(a, ">") == canon_amm(b, ">") if a[:4] == b"#ACM" else a == b
            assert igual, "ida y vuelta distinta en %d %s" % (f, a[:4])
        if not hay_ps2:
            continue
        ps2 = bytes(afs_pair.ps2(f, real=True))
        for a, b in zip(_hijos(p, "<"), _hijos(ps2, "<")):
            hijos += 1
            if a[:4] == b"#AMM":
                ca, cb = canon_amm(a, "<"), canon_amm(b, "<")
                anims += len(cb)
                anims_iguales += sum(x == y for x, y in zip(ca, cb))
                iguales += ca == cb
            else:
                assert a == b, "PS2 distinta en %d %s" % (f, a[:4])
                iguales += 1
    print("OK: %d ficheros, ida y vuelta igual en todos; frente a la PS2 real: %d/%d piezas iguales, "
          "animaciones %d/%d (las distintas son retoques de la HD); sin tratar (no son del donante): %s"
          % (n, iguales, hijos, anims_iguales, anims, otros))


if __name__ == "__main__":
    if "--autoprueba" in sys.argv:
        autoprueba()
    else:
        print(__doc__)
