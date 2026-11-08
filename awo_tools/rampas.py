#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""rampas.py - Rampas de sombreado de Budokai 2 -> estilo de B3 HD (frente Ports, 2026-10-15).

El sombreado "toon" de B3 HD resta la rampa a la textura base: color = base - rampa[u, v]
(memoria b3hd-toon-render; u = luz, v = banda de 4 filas elegida por el alfa DXT3 de la base).
En B3 la rampa guarda el color en RGB (piel nativa = rampa azul, ropa = rampa gris) con alfa 255.

Los modelos de Budokai 2 traen OTRA convencion: la base ya va coloreada y la rampa guarda la
oscuridad en el ALFA (51 = sombra, 0 = luz, 68-102 = linea del terminador) con el RGB a 0, salvo
la ultima columna, que es blanca (255). En B3 HD eso da:
  - piel "demasiado blanca": base - 0 = la base sin sombra (mas el brillo HD encima);
  - manchas negras: donde la luz cae en la ultima columna, base - 255 = negro (nuca de Tenshinhan).

Arreglo: para cada material con rampa "de alfa" se escribe una rampa nueva en RGB:
  rampa[fila, col] = color medio de la base en esa banda x (D_LUZ + alfa[fila, col] / 255 x PESOS)
de modo que base - rampa = base x (1 - D_LUZ - alfa x PESOS) en media: la sombra que pintaba B2, con su
tono, y el nivel de luz y la sombra calida de la piel nativa de B3 (calibrado con Tenshinhan).
Si dos materiales con bases distintas comparten rampa, el segundo recibe una copia propia (textura
nueva al final del #AZT). Los modelos nativos (rampa con RGB) no se tocan: deteccion por formato.

  python rampas.py ENTRADA SALIDA       (bin HD #AMB [#AWO, #AZT]; LZX o #AMB PS2 tambien)
  python rampas.py --info ENTRADA       (que rampas son de B2)

Escenarios: escena_rampas() hace lo mismo dentro de un bin #AMB con #ZDD anidado (escenarios de B2 de
escenarios_convertir.py, opcion --rampas-b2). En escena la sombra es neutra (pesos 1,1,1): en B2 la
rampa es un negro con alfa y oscurece sin cambiar el tono; el calibrado de piel es solo de personajes.
"""
import io
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
K_SOMBRA = 1.0          # 1.0 = la misma oscuridad que el alfa de B2
# Calibrado con Tenshinhan nativo (rampa azul de su piel): en B3 la luz ya oscurece ~10 % (piel nativa iluminada
# (225,195,146) frente a la base de B2 (248,205,168)) y la sombra es calida (oscurece mas el azul que el rojo:
# sombra nativa (201,149,99)). Con la rampa de B2 tal cual la piel salia casi blanca al sol.
D_LUZ = 0.10
PESOS = np.array([0.5, 1.0, 1.4])
MAX_TEX = 32            # texturas por modelo (nativos: hasta 26)


def _be(b, o):
    return struct.unpack_from(">I", b, o)[0]


def hijos(b, base=0):
    """[(magic, off_abs, size)] de un #AMB HD (big-endian)."""
    n, tbl = struct.unpack_from(">II", b, base + 0x10)
    out = []
    for k in range(n):
        off, size, _, _ = struct.unpack_from(">4I", b, base + tbl + 16 * k)
        if size:
            out.append((bytes(b[base + off:base + off + 4]), base + off, size, base + tbl + 16 * k))
    return out


def materiales(b, awo):
    """[(offset absoluto del material, tex base, rampa)] de todos los #AWG del #AWO."""
    n_awg = _be(b, awo + 0x18)
    tbl = awo + _be(b, awo + 0x1C)
    out = []
    for gi in range(n_awg):
        g0 = awo + _be(b, tbl + 4 * gi)
        if b[g0:g0 + 4] != b"#AWG":
            continue
        mats, nmat = _be(b, g0 + 0x20), _be(b, g0 + 0x24)
        if not mats:
            continue
        for m in range(nmat):
            mo = g0 + mats + 0x50 * m
            out.append((mo, _be(b, mo + 0x30), _be(b, mo + 0x34)))
    return out


def texturas(b, z):
    """{indice: (off entrada abs, off datos abs, tamano)} del #AZT en z."""
    n, idx = struct.unpack_from(">II", b, z + 0x10)
    out = {}
    for t in range(n):
        o = _be(b, z + idx + 4 * t)
        if not o:
            continue
        do, ds = struct.unpack_from(">II", b, z + o + 0x14)
        out[t] = (z + o, z + do, ds)
    return out


def leer_dds(b, t):
    from PIL import Image  # noqa: PLC0415
    _, do, ds = t
    return np.array(Image.open(io.BytesIO(bytes(b[do:do + ds]))).convert("RGBA"))


def es_rampa_b2(img):
    """Rampa de B2: RGB a 0 (salvo las ultimas columnas, blancas) y la sombra en el alfa."""
    if img.shape[1] < 8:
        return False
    claros = (img[..., :3].max(2) > 24).mean()
    a = img[..., 3]
    return claros <= 0.07 and int(a.max()) > 0 and int(a.min()) < 255


def rampa_nueva(ramp, bases, pesos=None):
    """Rampa RGB (alfa 255) a partir del alfa de la de B2 y del color medio de las bases por banda.

    pesos: vector RGB fijo para la sombra; None = el calibrado de piel (_pesos, personajes). Los escenarios
    usan (1,1,1): en B2 la rampa es un negro con alfa (oscurece sin cambiar el tono)."""
    h = ramp.shape[0]
    bandas = np.zeros((16, 3))
    cuenta = np.zeros(16)
    for base in bases:
        lv = np.minimum(15, (base[..., 3].astype(np.int32) + 8) // 17).reshape(-1)
        rgb = base[..., :3].reshape(-1, 3).astype(np.float64)
        for L in range(16):
            sel = lv == L
            n = int(sel.sum())
            if n:
                bandas[L] += rgb[sel].sum(0)
                cuenta[L] += n
    sombreadas = cuenta[:15].sum()
    media = bandas[:15].sum(0) / sombreadas if sombreadas else np.array([200.0, 200.0, 200.0])
    out = np.zeros_like(ramp)
    out[..., 3] = 255
    a = ramp[..., 3].astype(np.float64) / 255.0 * K_SOMBRA
    tinte = ramp[..., :3].astype(np.float64)
    coloreada = (ramp[..., :3].max(2) > 24).mean() > 0.5   # B2 "metal": base x (1 - alfa) + color x alfa (M dorada)
    for r in range(h):
        L = min(15, r * 16 // h)
        # una banda con cuatro pixeles sueltos no da color: la fila vecina se mezcla al muestrear (Bu con Freezer: blanco -> cara roja)
        c = bandas[L] / cuenta[L] if cuenta[L] >= 0.005 * cuenta.sum() else media
        if coloreada:                         # base - rampa = base x (1 - D_LUZ) - alfa x (base - color)
            fila = D_LUZ * c[None, :] + a[r][:, None] * (c[None, :] - tinte[r])
        else:
            w = _pesos(c) if pesos is None else np.asarray(pesos, np.float64)
            fila = np.clip(D_LUZ + a[r][:, None] * w[None, :], 0.0, 1.0) * c[None, :]
        out[r, :, :3] = np.clip(np.rint(fila), 0, 255).astype(np.uint8)
    return out


def _pesos(c):
    """La sombra calida (PESOS) es de la piel; en ropa con R ~ B (morado de Bu con Freezer) la volvia marron."""
    return PESOS if c[0] >= c[1] and c[0] > 1.3 * c[2] else np.ones(3)      # piel: R/B 1.5-1.7; morado: 1.0-1.2


def _dds(img, plantilla):
    sys.path.insert(0, HERE)
    from sdbh_model import encode_dxt3  # noqa: PLC0415
    hdr = bytes(plantilla[:128])
    if hdr[84:88] == b"DXT3":
        return hdr + encode_dxt3(img)
    return hdr + img[..., [2, 1, 0, 3]].astype(np.uint8).tobytes()   # A8R8G8B8


def _anadir(b, z, ref_t, data, n_actual):
    """Textura nueva al final del #AZT (que debe ser el ultimo hijo): copia la entrada de ref_t.
    -> indice nuevo."""
    ent_o, _, ds = ref_t
    ent = bytearray(b[ent_o:ent_o + 0x30])
    n, idx = struct.unpack_from(">II", b, z + 0x10)
    offs = [_be(b, z + idx + 4 * t) for t in range(n)]
    last = max(o for o in offs if o)
    vram = _be(b, z + last + 0x24)
    b += b"\0" * ((-len(b)) % 0x80)
    data_off = len(b) - z
    b += data
    struct.pack_into(">I", ent, 0, n)
    struct.pack_into(">II", ent, 0x14, data_off, len(data))
    struct.pack_into(">I", ent, 0x24, vram + ds // 0x20)
    b += b"\0" * ((-len(b)) % 0x10)
    ent_off = len(b) - z
    b += ent
    new_idx = len(b) - z
    b += struct.pack(">%dI" % (n + 1), *(offs + [ent_off]))
    b += b"\0" * ((-len(b)) % 0x10)
    struct.pack_into(">II", b, z + 0x10, n + 1, new_idx)
    return n


def convertir(hd, info=None):
    """Bin HD #AMB [#AWO, ..., #AZT] -> (bin, informe). Sin rampas de B2 devuelve el mismo bin."""
    b = bytearray(hd)
    if b[:4] != b"#AMB":
        return bytes(hd), "no es un #AMB"
    ks = hijos(b)
    awo = next((o for m, o, _, _ in ks if m == b"#AWO"), None)
    zs = [(o, s, e) for m, o, s, e in ks if m == b"#AZT"]
    if awo is None or not zs:
        return bytes(hd), "sin #AWO o #AZT"
    z, zsize, zent = zs[0]
    tex = texturas(b, z)
    mats = materiales(b, awo)
    cache = {}

    def img(t):
        if t not in cache:
            cache[t] = leer_dds(b, tex[t][:3]) if t in tex else None
        return cache[t]

    pares = {}
    for mo, base, ramp in mats:
        if ramp in tex and base in tex:
            r = img(ramp)
            # rampa coloreada de B2 (M dorada; RGB marron con alfa): sin el bit 0x2000 (solo B2) seria nativa, 4 de B3 traen alfa 0
            if r is not None and (es_rampa_b2(r) or (_be(b, mo + 0x38) & 0x2000 and int(r[..., 3].min()) < 255)):
                pares.setdefault(ramp, {}).setdefault(base, []).append(mo)
    if not pares:
        return bytes(hd), "sin rampas de Budokai 2"
    # texturas nuevas solo si el #AZT es el ultimo hijo y el modelo no pasa de MAX_TEX texturas (los nativos
    # llegan a 26); si no, una rampa por indice con el color medio de todas sus bases
    nuevas_hacen_falta = sum(len(v) - 1 for v in pares.values())
    ultimo = max(o for _, o, _, _ in ks) == z and len(tex) + nuevas_hacen_falta <= MAX_TEX
    nuevas = escritas = 0
    for ramp, por_base in sorted(pares.items()):
        r = img(ramp)
        grupos = list(por_base.items())
        if not ultimo:                       # sin sitio para texturas nuevas: una rampa para todas
            grupos = [(None, [mo for _, ms in grupos for mo in ms])]
        for k, (base, ms) in enumerate(grupos):
            bases = [img(base)] if base is not None else [img(x) for x in por_base]
            nueva = rampa_nueva(r, bases)
            data = _dds(nueva, b[tex[ramp][1]:tex[ramp][1] + 128])
            if k == 0:
                _, do, ds = tex[ramp]
                if len(data) != ds:
                    raise ValueError("rampa %d: tamano distinto (%d != %d)" % (ramp, len(data), ds))
                b[do:do + ds] = data
                escritas += 1
            else:
                t_new = _anadir(b, z, tex[ramp], data, None)
                for mo in ms:
                    struct.pack_into(">I", b, mo + 0x34, t_new)
                nuevas += 1
            for mo in ms:                     # vtype de B2 (0x21B5): el bit 0x2000 no existe en B3
                vt = _be(b, mo + 0x38)
                if vt & 0x2000:
                    struct.pack_into(">I", b, mo + 0x38, vt & ~0x2000)
    if nuevas:
        struct.pack_into(">I", b, zent + 4, len(b) - z)       # tamano del #AZT en la tabla del #AMB
        b += b"\0" * ((-len(b)) % 32)
    return bytes(b), "rampas de Budokai 2 convertidas: %d (%d nuevas)" % (escritas + nuevas, nuevas)


def _azts_escena(b):
    """#AZT de un bin de escenario (#AMB/#ZDD), con sus texturas DDS: [(off, {indice: (entrada, datos, tam, w, h)})]."""
    out, i = [], 0
    while True:
        i = bytes(b).find(b"#AZT", i)
        if i < 0:
            break
        z, i = i, i + 4
        try:
            n, idx = _be(b, z + 0x10), _be(b, z + 0x14)
            if not 1 <= n <= 512 or idx < 0x18 or z + idx + 4 * n > len(b):
                continue
            tex, vistas = {}, 0
            for t in range(n):
                e = _be(b, z + idx + 4 * t)
                if not e:
                    continue
                if e < 0x18 or z + e + 0x30 > len(b):
                    raise ValueError
                do, ds = _be(b, z + e + 0x14), _be(b, z + e + 0x18)
                if do < 0x18 or ds < 128 or z + do + ds > len(b) or bytes(b[z + do:z + do + 4]) != b"DDS ":
                    raise ValueError
                w, h = struct.unpack_from(">2H", b, z + e + 16)
                tex[t] = (z + e, z + do, ds, w, h)
                vistas += 1
            if vistas:
                out.append((z, tex))
        except (ValueError, struct.error):
            continue
    return out


def _materiales_escena(b):
    """Materiales de todos los #AWO del bin: [(descriptor, textura base, rampa)] (FFFF = sin textura/rampa)."""
    out, i = [], 0
    while True:
        i = bytes(b).find(b"#AWO", i)
        if i < 0:
            break
        awo, i = i, i + 4
        try:
            n, tbl = _be(b, awo + 0x18), _be(b, awo + 0x1C)
            if not 1 <= n <= 64 or tbl < 0x20 or awo + tbl + 4 * n > len(b):
                continue
            out += materiales(b, awo)
        except (ValueError, struct.error):
            continue
    return out


def escena_rampas(hd):
    """Rampas de sombreado de Budokai 2 dentro de un bin de ESCENARIO (#AMB con #ZDD anidado).

    En B3 HD el color es base - rampa (la rampa lleva la sombra en RGB); los escenarios de B2 traen
    la sombra en el ALFA de la rampa con el RGB a 0. Se reescribe cada rampa de alfa como rampa RGB
    con el color medio de las texturas base de sus materiales, igual que los ports de personaje
    (convertir()). Trabaja por #AZT y solo toca indices de rampa que use algun material de ese #AZT;
    sin rampas de B2 devuelve el mismo bin. -> (bytes, informe)."""
    b = bytearray(hd)
    if b[:4] != b"#AMB":
        return bytes(hd), "no es un #AMB"
    mats = _materiales_escena(b)
    if not mats:
        return bytes(hd), "sin materiales #AWO"
    usadas = {r for _, _, r in mats}
    hechas = []
    for z, tex in _azts_escena(b):
        cache = {}

        def img(t):
            if t not in cache:
                cache[t] = None
                if t in tex:
                    try:
                        cache[t] = leer_dds(b, tex[t][:3])
                    except (OSError, ValueError):
                        pass
            return cache[t]

        for r in sorted(usadas & set(tex)):
            ramp = img(r)
            if ramp is None or not es_rampa_b2(ramp):
                continue
            bases = sorted({t for _, t, rr in mats if rr == r and t in tex and t != r})
            imgs = [i for i in (img(t) for t in bases) if i is not None and not es_rampa_b2(i)]
            if not imgs:
                continue
            nueva = rampa_nueva(ramp, imgs, pesos=np.ones(3))
            ent_o, do, ds, _, _ = tex[r]
            dato = _dds(nueva, b[do:do + 128])
            if len(dato) != ds:
                continue
            b[do:do + ds] = dato
            hechas.append(dict(azt=z, indice=r, bases=bases))
    if not hechas:
        return bytes(hd), "sin rampas de Budokai 2 en escenario"
    return bytes(b), "rampas de B2 en escenario convertidas: %d %s" % (
        len(hechas), ", ".join("AZT%X/tex%d<-%s" % (h["azt"], h["indice"], h["bases"]) for h in hechas))


def es_b1(b, mats):
    """Modelo de Budokai 1 convertido: ningun material con rampa y tipo 0x1BD (los nativos: 0x1B5 / 0x29BD con
    rampa, 0x1B4 / 0x1B4 sin ella)."""
    sin = [mo for mo, _, r in mats if r == 0xFFFFFFFF and _be(b, mo + 0x38) & 0xFF == 0xBD
           and not _be(b, mo + 0x3C) & 0x2800]
    # los de Goku, Krilin, Vegeta... traen ademas 1-4 materiales 0x190/0x199 (ojos): con exigir TODOS 0x1BD se
    # quedaban blancos 40 de los 68 modelos de B1 (auditoria10/A); basta con que ninguno tenga rampa y la mitad sea 0x1BD
    libres = [mo for mo, _, r in mats if r == 0xFFFFFFFF]
    return bool(mats) and len(libres) == len(mats) and 2 * len(sin) >= len(mats)


def desde_b1(hd):
    """Budokai 1 (frente Ports, 2026-10-15): sus materiales no traen rampa y el tipo 0x1BD/0x1BD; en B3 HD el
    modelo salia blanco entero (Zarbon B1, normal y transformado). Se le da a cada textura base una rampa nueva
    al estilo de B3 (la de B2 hecha con un perfil de sombra fijo: sombra en 3/5 del ancho, linea del terminador y
    luz) y el tipo de los nativos con rampa (0x1B5 / 0x29BD, con el resto de bits). -> (bin, n rampas)."""
    b = bytearray(hd)
    if b[:4] != b"#AMB":
        return bytes(hd), 0
    ks = hijos(b)
    awo = next((o for m, o, _, _ in ks if m == b"#AWO"), None)
    zs = [(o, s, e) for m, o, s, e in ks if m == b"#AZT"]
    if awo is None or not zs:
        return bytes(hd), 0
    z, _, zent = zs[0]
    mats = materiales(b, awo)
    if not es_b1(b, mats) or max(o for _, o, _, _ in ks) != z:
        return bytes(hd), 0
    tex = texturas(b, z)
    bases = sorted({base for _, base, _ in mats if base in tex})
    if len(tex) + len(bases) > MAX_TEX:
        return bytes(hd), 0
    nueva = {}
    for t in bases:
        img = leer_dds(b, tex[t])
        h, w = img.shape[:2]
        perfil = np.zeros(w)                          # alfa de sombra por columna (u = luz), como las rampas de B2
        c = np.arange(w) / w
        perfil[c < 0.625] = 51
        perfil[(c >= 0.625) & (c < 0.6875)] = 68
        perfil[(c >= 0.6875) & (c < 0.75)] = 85
        perfil[(c >= 0.75) & (c < 0.8125)] = 34
        plantilla = np.zeros((h, w, 4), np.uint8)
        plantilla[..., 3] = perfil[None, :].astype(np.uint8)
        data = _dds(rampa_nueva(plantilla, [img]), b[tex[t][1]:tex[t][1] + 128])
        if len(data) != tex[t][2]:
            continue
        nueva[t] = _anadir(b, z, tex[t], data, None)
    for mo, base, _ in mats:
        if base in nueva and _be(b, mo + 0x38) & 0xFF == 0xBD:   # los 0x190/0x199 (ojos) se quedan como estan
            struct.pack_into(">I", b, mo + 0x34, nueva[base])
            struct.pack_into(">I", b, mo + 0x38, (_be(b, mo + 0x38) & ~0xFF) | 0xB5)
            struct.pack_into(">I", b, mo + 0x3C, _be(b, mo + 0x3C) | 0x2800)
    if nueva:
        struct.pack_into(">I", b, zent + 4, len(b) - z)       # tamano del #AZT en la tabla del #AMB
        b += b"\0" * ((-len(b)) % 32)
    return bytes(b), len(nueva)


def sombrear_planas(hd):
    """Texturas base con alfa 255 en toda la imagen (en B3 = sin sombrear: el pelo de Goku de SDBH salia gris claro
    con brillos) -> alfa 0 (banda sombreada, como las del juego). No toca las rampas. -> (bin, n)."""
    b = bytearray(hd)
    if b[:4] != b"#AMB":
        return bytes(hd), 0
    ks = hijos(b)
    awo = next((o for m, o, _, _ in ks if m == b"#AWO"), None)
    z = next((o for m, o, _, _ in ks if m == b"#AZT"), None)
    if awo is None or z is None:
        return bytes(hd), 0
    tex = texturas(b, z)
    mats = materiales(b, awo)
    ramps = {r for _, _, r in mats}
    n = 0
    for t in sorted({base for _, base, r in mats if r != 0xFFFFFFFF and base not in ramps}):
        if t not in tex:
            continue
        _, do, ds = tex[t]
        if bytes(b[do + 84:do + 88]) != b"DXT3":
            continue
        img = leer_dds(b, tex[t])
        if int(img[..., 3].min()) < 255:
            continue
        for k in range((ds - 128) // 16):     # bloque DXT3: 8 B de alfa (4 bits por pixel) + 8 B de color
            o = do + 128 + 16 * k
            b[o:o + 8] = bytes(8)
        n += 1
    return bytes(b), n


def _prueba():
    """python rampas.py --prueba: M dorada (no verde), morado sin marron, piel con sombra calida."""
    def banda(rgb):
        return np.dstack([np.full((16, 16, 3), rgb, np.uint8), np.zeros((16, 16, 1), np.uint8)])
    oro = np.dstack([np.full((64, 64, 3), (107, 44, 8), np.uint8), np.full((64, 64, 1), 102, np.uint8)])
    amarillo = np.array((255, 220, 80))
    final = amarillo - rampa_nueva(oro, [banda(amarillo)])[0, 0, :3].astype(int)
    assert final[0] > final[1] > final[2], final      # dorado: R > G > B (antes verde: G > R)
    negra = np.dstack([np.zeros((64, 64, 3), np.uint8), np.full((64, 64, 1), 136, np.uint8)])
    morado = np.array((155, 91, 131))
    final = morado - rampa_nueva(negra, [banda(morado)])[0, 0, :3].astype(int)
    assert final[2] > final[1], final                 # sigue morado
    piel = np.array((255, 146, 148))
    r = rampa_nueva(negra, [banda(piel)])[0, 0, :3].astype(float) / piel
    assert r[2] > r[0], r                             # la sombra de la piel come mas azul que rojo
    print("rampas: prueba OK")


def main(argv):
    if argv == ["--prueba"]:
        _prueba()
        return 0
    if len(argv) >= 2 and argv[0] == "--info":
        sys.path.insert(0, os.path.join(HERE, "..", "mod center hd"))
        import roster_build as rb  # noqa: PLC0415
        work = os.path.join(os.environ.get("TEMP", "."), "rampas_work")
        os.makedirs(work, exist_ok=True)
        b = rb.to_hd(open(argv[1], "rb").read(), work)
        ks = hijos(b)
        awo = next(o for m, o, _, _ in ks if m == b"#AWO")
        z = next(o for m, o, _, _ in ks if m == b"#AZT")
        tex = texturas(b, z)
        for mo, base, ramp in materiales(b, awo):
            r = leer_dds(b, tex[ramp]) if ramp in tex else None
            print("material %06X base %s rampa %s %s" % (mo, base, ramp,
                  "B2" if r is not None and es_rampa_b2(r) else "-"))
        return 0
    if len(argv) != 2:
        print(__doc__)
        return 1
    sys.path.insert(0, os.path.join(HERE, "..", "mod center hd"))
    import roster_build as rb  # noqa: PLC0415
    work = os.path.join(os.environ.get("TEMP", "."), "rampas_work")
    os.makedirs(work, exist_ok=True)
    out, rep = convertir(rb.to_hd(open(argv[0], "rb").read(), work))
    open(argv[1], "wb").write(out)
    print(rep)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
