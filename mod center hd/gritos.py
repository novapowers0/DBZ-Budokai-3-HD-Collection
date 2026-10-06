#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gritos.py - Gritos de combate propios de los personajes nuevos (B3 HD).

Budokai 3 HD no saca los gritos de los golpes de la tabla de voces (voces.py), sino de
un banco por personaje (RE 2026-10-04):

    lang_usa.afs / lang_jpn.afs, entrada char372 +0x24 (roster_db "lang"[0]; la +0x28 es
    un #ACK pequeno aparte)
    #AMB HD: hijos 0 y 1 = banco de sonidos (los dos iguales), hijo 2 = secuencias (PS2 SQ)
    banco:   +0x08 ID, +0x0C n sonidos (55-56), +0x10 inicio de datos, +0x14 tamano
             +0x18 -> tabla de u32 con el desplazamiento de cada ficha (0x100 + 0x100*i; en
             Goku, Goku nino, Piccolo y Buu M, 0x110 + 0x100*i)
             ficha "xma": +0x14 tamano, +0x18 desplazamiento (paquetes de
             2048 B), +0xBC XMA2WAVEFORMATEX (16 kHz mono; +0xD4 muestras codificadas,
             +0xE0 duracion en muestras)

El formato de Xbox (XMA) no se puede crear aqui, asi que el runtime (xma_context.cpp, sdk
de la casa) acepta tambien paquetes "RXADPC": 8 B de cabecera y hasta 7 bloques IMA ADPCM
de 260 B (512 muestras cada uno). Este modulo fabrica el banco de un personaje nuevo a
partir del de su donante (mismas secuencias y huecos) con sus propios sonidos:

    gritos = "iw:JANENBA"    # sus grabaciones de Infinite World, repartidas por duracion
    gritos = "sb2:GHF"       # su banco de Shin Budokai, tono a hueco (sb_voces.py)
    gritos = "donante"       # los del donante (por defecto si no hay fuente)
    gritos = "ninguno"       # sin gritos (silencio)
"""
import struct

import numpy as np

RATE = 16000
SPF = 512                      # muestras por bloque
BLOCK = 260                    # bytes por bloque IMA (4 de estado + 256 de nibbles)
PER_PACKET = 7                 # bloques por paquete de 2048 B
PACKET = 2048
MAGIC = b"RXADPC\x01"
SILENT_MAX = 1200              # huecos "vacios" del banco (el de Krillin: 1116 muestras)

IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
            60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
            337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
            1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
            5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
            20350, 22385, 24623, 27086, 29794, 32767]
IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


# ---------------------------------------------------------------- audio
def adx_decode(b):
    """ADX de CRI (tipo 3/4, mono o estereo) -> (int16 mono, frecuencia)."""
    if b[0] != 0x80:
        raise ValueError("no es un ADX")
    off = struct.unpack(">H", b[2:4])[0] + 4
    bs, ch = b[5], b[7]
    rate, total = struct.unpack(">II", b[8:16])
    cut = struct.unpack(">H", b[16:18])[0]
    z = np.cos(2 * np.pi * cut / rate)
    a = np.sqrt(2) - z
    bb = np.sqrt(2) - 1
    c = (a - np.sqrt((a + bb) * (a - bb))) / bb
    c1, c2 = int(np.floor(c * 8192)), int(np.floor(c * c * -4096))
    spb = (bs - 2) * 2
    out = [[] for _ in range(ch)]
    hist = [[0, 0] for _ in range(ch)]
    o, n = off, 0
    while n < total and o + bs * ch <= len(b):
        for k in range(ch):
            fr = b[o + k * bs:o + (k + 1) * bs]
            scale = struct.unpack(">H", fr[:2])[0] + 1
            h1, h2 = hist[k]
            for i in range(spb):
                v = fr[2 + i // 2]
                nib = (v >> 4) if i % 2 == 0 else (v & 15)
                if nib >= 8:
                    nib -= 16
                s = nib * scale + ((c1 * h1 + c2 * h2) >> 12)
                s = max(-32768, min(32767, s))
                out[k].append(s)
                h2, h1 = h1, s
            hist[k] = [h1, h2]
        o += bs * ch
        n += spb
    x = np.array(out[0][:total], np.float64)
    if ch > 1:
        x = (x + np.array(out[1][:total], np.float64)) / 2
    return x.astype(np.int16), rate


VAG_F = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


def vag_decode(bd, start, end=None):
    """PS-ADPCM (VAG de PS2, mono) desde `start` hasta la marca de fin (flag 1)."""
    out, h1, h2 = [], 0, 0
    o = start
    end = len(bd) if end is None else end
    while o + 16 <= end:
        pf, flags = bd[o], bd[o + 1]
        f0, f1 = VAG_F[min(pf >> 4, 4)]
        sh = pf & 15
        for i in range(28):
            v = bd[o + 2 + i // 2]
            nib = (v & 15) if i % 2 == 0 else (v >> 4)
            if nib >= 8:
                nib -= 16
            sample = (nib << 12) >> sh
            sample += (h1 * f0 + h2 * f1 + 32) >> 6
            sample = max(-32768, min(32767, sample))
            out.append(sample)
            h2, h1 = h1, sample
        o += 16
        if flags & 1 and o > start + 16:
            break
    return np.array(out, np.int16)


def scei_bank(hd, bd):
    """Banco de sonido PS2 (cabecera IECS 'Vagi' + datos VAG) -> [(int16, frecuencia)]."""
    i = hd.find(b"IECSigaV")
    if i < 0:
        raise ValueError("sin bloque Vagi")
    mx = struct.unpack_from("<I", hd, i + 12)[0]
    offs = struct.unpack_from("<%dI" % (mx + 1), hd, i + 16)
    params = [struct.unpack_from("<IH", hd, i + o) for o in offs]
    starts = sorted(set(p[0] for p in params)) + [len(bd)]
    out = []
    for vo, rate in params:
        nxt = next(x for x in starts if x > vo)
        out.append((vag_decode(bd, vo, nxt), rate or 22050))
    return out


def fit(lengths, sounds):
    """Sonido k -> hueco k (mismo orden), a 16 kHz y sin pasar de 1.1 x el hueco."""
    out = []
    for k, ln in enumerate(lengths):
        if k >= len(sounds) or ln <= SILENT_MAX:
            out.append(np.zeros(max(ln, 1), np.int16))
            continue
        s = sounds[k]
        lim = int(ln * 1.1)
        if len(s) > lim:
            s = s[:lim].astype(np.float64)
            f = min(800, lim // 4)
            s[-f:] *= np.linspace(1, 0, f)
            s = s.astype(np.int16)
        out.append(s)
    return out


def resample(x, src, dst=RATE):
    if src == dst:
        return x.astype(np.int16)
    from math import gcd  # noqa: PLC0415
    from scipy.signal import resample_poly  # noqa: PLC0415
    g = gcd(src, dst)
    y = resample_poly(x.astype(np.float64), dst // g, src // g)
    return np.clip(np.round(y), -32768, 32767).astype(np.int16)


def ima_encode(x):
    """int16 mono -> bloques IMA de 260 B (el formato que decodifica el runtime)."""
    n = (len(x) + SPF - 1) // SPF
    x = np.concatenate([x.astype(np.int32), np.zeros(n * SPF - len(x), np.int32)])
    pred, idx = int(x[0]) if len(x) else 0, 0
    blocks = []
    for k in range(n):
        blk = bytearray(struct.pack(">hBB", pred, idx, 0))
        nibs = []
        for s in x[k * SPF:(k + 1) * SPF]:
            step = IMA_STEP[idx]
            diff = int(s) - pred
            nib = 0
            if diff < 0:
                nib, diff = 8, -diff
            if diff >= step:
                nib |= 4
                diff -= step
            if diff >= step >> 1:
                nib |= 2
                diff -= step >> 1
            if diff >= step >> 2:
                nib |= 1
            # reconstruye como el decodificador
            d = step >> 3
            if nib & 4:
                d += step
            if nib & 2:
                d += step >> 1
            if nib & 1:
                d += step >> 2
            pred = max(-32768, min(32767, pred - d if nib & 8 else pred + d))
            idx = max(0, min(88, idx + IMA_INDEX[nib]))
            nibs.append(nib)
        for i in range(0, SPF, 2):
            blk.append((nibs[i] << 4) | nibs[i + 1])
        blocks.append(bytes(blk))
    return blocks


def packets(x):
    """Sonido -> datos RXADPC (paquetes de 2048 B) y numero de bloques.

    El juego calcula el comienzo leyendo la cabecera XMA del primer paquete; la firma
    "RXAD" se lee como "primera trama en el bit 19240" = paquete 1, bloque 1. Por eso el
    paquete 0 va vacio y el 1 empieza con un bloque mudo: arranque en 32 o en 19240 da el
    sonido completo (antes: sin paquete 1 -> contexto atascado y el juego colgado)."""
    blocks = ima_encode(x) or ima_encode(np.zeros(SPF, np.int16))
    out = bytearray(MAGIC + bytes([0]) + bytes(PACKET - len(MAGIC) - 1))
    nb = len(blocks)
    blocks = ima_encode(np.zeros(SPF, np.int16)) + blocks
    for k in range(0, len(blocks), PER_PACKET):
        grp = blocks[k:k + PER_PACKET]
        p = bytearray(MAGIC + bytes([len(grp)]))
        for g in grp:
            p += g
        out += p + bytes(PACKET - len(p))
    return bytes(out), nb


def rx_decode(blob, length=None):
    """Datos RXADPC -> int16, con la misma logica que el runtime (xma_context.cpp):
    paquete a paquete, min(byte 7, 7) bloques de 260 B. Se quita el bloque mudo del
    arranque (paquete 1, bloque 0) y se corta a `length` (la duracion de la ficha)."""
    out = []
    for p in range(0, len(blob) - PACKET + 1, PACKET):
        pk = blob[p:p + PACKET]
        if pk[:7] != MAGIC:
            raise ValueError("paquete %d sin firma RXADPC" % (p // PACKET))
        for f in range(min(pk[7], PER_PACKET)):
            fr = pk[8 + BLOCK * f:8 + BLOCK * (f + 1)]
            pred, idx = struct.unpack(">h", fr[:2])[0], min(fr[2], 88)
            for i in range(SPF):
                nib = fr[4 + i // 2] & 15 if i & 1 else fr[4 + i // 2] >> 4
                step = IMA_STEP[idx]
                d = step >> 3
                if nib & 4:
                    d += step
                if nib & 2:
                    d += step >> 1
                if nib & 1:
                    d += step >> 2
                pred = max(-32768, min(32767, pred - d if nib & 8 else pred + d))
                idx = max(0, min(88, idx + IMA_INDEX[nib]))
                out.append(pred)
    x = np.array(out[SPF:], np.int16)
    return x if length is None else x[:length]


def records(bank):
    """Desplazamiento de la ficha de cada sonido (tabla apuntada por +0x18)."""
    n, tbl = struct.unpack(">I", bank[0x0C:0x10])[0], struct.unpack(">I", bank[0x18:0x1C])[0]
    return list(struct.unpack(">%dI" % n, bank[tbl:tbl + 4 * n]))


def bank_sounds(bank):
    """Sonidos RXADPC de un banco ya construido (None = hueco con datos XMA)."""
    start = struct.unpack(">I", bank[0x10:0x14])[0]
    out = []
    for r in records(bank):
        size, off = struct.unpack(">II", bank[r + 0x14:r + 0x1C])
        blob = bank[start + off:start + off + size]
        ln = struct.unpack(">I", bank[r + 0xE0:r + 0xE4])[0]
        out.append(rx_decode(blob, ln) if blob[:7] == MAGIC else None)
    return out


# ---------------------------------------------------------------- banco
def amb_children(b):
    n, tbl = struct.unpack(">II", b[0x10:0x18])
    return [list(struct.unpack(">4I", b[tbl + 16 * k:tbl + 16 * k + 16])) for k in range(n)]


def bank_lengths(bank):
    """Duracion (muestras) de cada hueco del banco."""
    return [struct.unpack(">I", bank[r + 0xE0:r + 0xE4])[0] for r in records(bank)]


def build_bank(bank, sounds):
    """Banco del donante con los sonidos `sounds` (int16 a 16 kHz, o None = se queda el
    del donante) en sus huecos."""
    start = struct.unpack(">I", bank[0x10:0x14])[0]
    head = bytearray(bank[:start])
    data = bytearray()
    recs = records(bank)
    loud = [r for r in recs if struct.unpack(">I", bank[r + 0xE0:r + 0xE4])[0] > SILENT_MAX]
    for i, r in enumerate(recs):
        size, off = struct.unpack(">II", head[r + 0x14:r + 0x1C])
        s = sounds[i] if i < len(sounds) else None
        if s is None:
            blob = bytes(bank[start + off:start + off + size])
        else:
            if len(s) > SILENT_MAX and struct.unpack(">I", bank[r + 0xE0:r + 0xE4])[0] <= SILENT_MAX and loud:
                # hueco vacio del donante: volumen -99 dB (+0x34), +0x60 y -100 dB (+0x6C);
                # se copian del ultimo hueco con sonido anterior (o el primero) para que se oiga
                ref = max((x for x in loud if x < r), default=loud[0])
                for o in (0x34, 0x60, 0x6C):
                    head[r + o:r + o + 4] = bank[ref + o:ref + o + 4]
            blob, nb = packets(s)
            struct.pack_into(">I", head, r + 0xD4, nb * SPF)       # muestras codificadas
            struct.pack_into(">I", head, r + 0xDC, 0)              # inicio
            struct.pack_into(">I", head, r + 0xE0, max(1, len(s))) # duracion
            struct.pack_into(">II", head, r + 0xE4, 0, 0)          # sin bucle
        struct.pack_into(">II", head, r + 0x14, len(blob), len(data))
        data += blob
    struct.pack_into(">I", head, 0x14, len(data))
    return bytes(head + data)


def sq_enable(sq, sounds):
    """SQ del banco: la secuencia de cada sonido (u32 0x009000XX) lleva en -0x0C un 0x7F si
    suena y 0x00 si el donante tenia el hueco vacio (Gohan adulto: 46-49). Se enciende en los
    huecos que ahora llevan sonido; si no, el grito existe pero el juego no lo reproduce."""
    out = bytearray(sq)
    for i, s in enumerate(sounds):
        if s is not None and len(s) > SILENT_MAX:
            at = out.find(bytes((0, 0x90, 0, i)))
            if at >= 0x0C:
                out[at - 0x0C] = 0x7F
    return bytes(out)


def build_lang(entry, sounds):
    """Entrada lang_*.afs (#AMB HD descomprimido) con los dos bancos sustituidos."""
    kids = amb_children(entry)
    tbl = struct.unpack(">I", entry[0x14:0x18])[0]
    out = bytearray(entry[:(tbl + 16 * len(kids) + 0x1F) // 0x20 * 0x20])
    for k, (off, size, typ, z) in enumerate(kids):
        part = entry[off:off + size]
        if k < 2 and size:
            part = build_bank(part, sounds)
        elif k == 2 and size:
            part = sq_enable(part, sounds)
        out += bytes((-len(out)) % 0x20)
        kids[k] = [len(out), len(part), typ, z]
        out += part
    out += bytes((-len(out)) % 0x20)
    for k, e in enumerate(kids):
        struct.pack_into(">4I", out, tbl + 16 * k, *e)
    return bytes(out)


def assign(lengths, pool, seed=0):
    """Reparte las grabaciones `pool` (int16) entre los huecos por duracion parecida,
    sin repetir la misma en huecos seguidos. Los huecos vacios quedan en silencio."""
    out, uses, last = [], [0] * len(pool), -1
    for ln in lengths:
        if ln <= SILENT_MAX or not pool:
            out.append(np.zeros(max(ln, 1), np.int16))
            continue
        best = None
        for k, s in enumerate(pool):
            cost = abs(np.log((len(s) + 1) / (ln + 1))) + 0.35 * uses[k] + (2.0 if k == last else 0)
            if best is None or cost < best[0]:
                best = (cost, k)
        k = best[1]
        uses[k] += 1
        last = k
        s = pool[k]
        lim = int(ln * 1.1)                   # el banco no debe crecer: memoria del juego
        if len(s) > lim:                       # recorta con un fundido corto
            s = s[:lim].astype(np.float64)
            f = min(800, lim // 4)
            s[-f:] *= np.linspace(1, 0, f)
            s = s.astype(np.int16)
        out.append(s)
    return out


def iw_pool(iw, who, lang):
    """Grabaciones (sin repetir) de un personaje de Infinite World a 16 kHz."""
    seen, pool = set(), []
    for v in iw.slots(who):
        if v < 0 or v in seen:
            continue
        seen.add(v)
        x, r = adx_decode(iw.adx(lang, v))
        pool.append(resample(x, r))
    return pool


if __name__ == "__main__":
    # autocomprobacion: IMA -> RXADPC -> decodificador del runtime = la misma senal
    t = np.arange(5000)
    sig = (12000 * np.sin(2 * np.pi * 440 * t / RATE) * np.exp(-t / 3000)).astype(np.int16)
    blob, nb = packets(sig)
    assert nb == (len(sig) + SPF - 1) // SPF and len(blob) % PACKET == 0
    back = rx_decode(blob, len(sig))
    snr = 10 * np.log10(np.sum(sig.astype(float) ** 2) / np.sum((sig.astype(float) - back) ** 2))
    assert len(back) == len(sig) and snr > 20, snr
    print("gritos.py: RXADPC ida y vuelta OK (%d muestras, SNR %.1f dB)" % (len(back), snr))
    # SQ: un hueco vacio del donante (0x00) se enciende al llenarlo; los demas no se tocan
    ent = lambda on, i: bytes.fromhex("10000000") + bytes((on, 0x40, 0xE8, 3)) + bytes(8) + bytes((0, 0x90, 0, i)) + bytes(4)
    sq = sq_enable(ent(0, 0) + ent(0, 1), [sig, None])
    assert sq == ent(0x7F, 0) + ent(0, 1)
    print("gritos.py: SQ de huecos nuevos encendida OK")
    # ficha: un hueco vacio (-99 dB) que se llena toma el volumen de uno con sonido
    bank = bytearray(0x230)                     # cabecera + tabla en 0x20 + 2 fichas de 0x100
    struct.pack_into(">I", bank, 0x0C, 2)
    struct.pack_into(">II", bank, 0x10, 0x230, 0)
    struct.pack_into(">I", bank, 0x18, 0x20)
    struct.pack_into(">II", bank, 0x20, 0x30, 0x130)
    for r, vol, ln in ((0x30, -6.0, 9000), (0x130, -99.0, 1116)):
        struct.pack_into(">f", bank, r + 0x34, vol)
        struct.pack_into(">I", bank, r + 0xE0, ln)
    out = build_bank(bytes(bank), [None, sig])
    assert struct.unpack_from(">f", out, 0x130 + 0x34)[0] == -6.0
    print("gritos.py: volumen de huecos vacios OK")
