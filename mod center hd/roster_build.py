#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""roster_build.py - Personajes NUEVOS para B3 HD (US): casillas extra en el select.

Un personaje nuevo es un mod "fuente" con `personaje.toml` (sin numeros de entrada):

    mods/<mod>/personaje.toml
    mods/<mod>/modelos/*.bin|*.amb   (uno por traje: #AMB PS2 de B3, #AMB HD o LZX)
    mods/<mod>/ui/                   (opcional: arte propio, ver *_fuente)

    [personaje]
    nombre = "Guldo"         # rotulo del select
    id = 22                  # opcional: plaza (ID libre: 22-26 y 31 = recortados de fabrica,
                             # 44-63 = plazas extra)
    donante = 21             # moveset, tecnicas, aura, voz y bocas de partida
    modelos = ["modelos/traje1.amb", "modelos/traje2.amb"]
    despues_de = 21          # opcional: ID tras el que aparece en la rueda (por defecto el donante)
    moveset = ["moveset/anm.bin"]  # opcional: moveset propio por forma (si no, el del donante);
                                   # con uno solo, todas las formas usan ese
    formas = 1                     # opcional: numero de formas (si no, las del donante; no mas)
    modelos_por_traje = 4          # opcional: modelos de cada traje (uno por forma: traje1 =
                                   # modelos 1..4, traje2 = 5..8...)
    modelo_forma = [0, 1, 2, 3]    # opcional: modelo (dentro del traje) de cada forma; por
                                   # defecto, si hay tantos modelos por traje como formas, cada
                                   # forma el suyo; si no, el reparto del donante
    ki_base = [3, 4, 4, 5]         # opcional: barras de ki a las que tiende cada forma
    fisica = "donante"             # opcional: pelo y colas del cinturon con la fisica de cadenas
                                   # del donante (o = ID de otro personaje); sin ella, rigidos
    transformacion = "donante"     # opcional: P+K+G y animaciones de transformarse del donante
                                   # (quita la de transformarse que traiga un port)
    camara = "moveset/cam.bin"     # opcional: camara/animaciones especiales propias
    tecnicas = "moveset/bsp.bin"   # opcional: efectos de tecnicas propios
    combos_tecnica = true          # opcional (por defecto si): tecnicas al final de los combos
                                   # (P,P,P,P y E...) como en el donante, para movesets propios
    definitiva_animaciones = "moveset/definitiva.json"  # opcional: la definitiva (cinematica del
                                   # donante) con animaciones propias: {"0x4a0": [anim, desde, hasta]}
                                   # por codigo de la cinematica (awo_tools/cinematica.py)
    aura_color = "morado"          # opcional: color del aura (nombre, "#RRGGBB" o tono 0-359;
                                   # solo cambia el tono: lo blanco o gris se queda igual)
    ki_color = "rojo"              # opcional: color de los efectos de sus tecnicas (ki, rayos)
    aura_de = "gogeta"             # opcional: el aura de otro personaje (ID 0-43 o gogeta,
                                   # gogeta_ssj4, vegito); se puede combinar con aura_color
    bocas = ["modelos/bocas.bin"]  # opcional: bocas propias (si no, las del donante re-etiquetadas)
    icono_fuente = "modelo"        # modelo (render cel-shading del 1er modelo, por defecto) |
                                   # imagen (ui/cara.png con fondo y aro oficiales) |
                                   # terminado (ui/icono.png tal cual)
    retrato_fuente = "modelo"      # modelo | imagen (ui/retrato.png) | terminado (ui/retrato_p1/p2.png)
    icono_ajuste = [1.0, 0, 0, 12]     # zoom, dx, dy (fraccion), giro (grados)
    retrato_ajuste = [1.0, 0, 0, 50]
    hud_ajuste = [1.0, 0, 0, 0]    # cara de la barra de vida (render del modelo, una por forma;
                                   # o ui/hud.png / ui/hud_1.png... terminadas de 256x128)
    capsulas_nativas = [6, 7]      # opcional: capsulas de B3 que pide el moveset de un port de B3
    voces = "iw:JANENBA"           # opcional: voces de combate (iw:NOMBRE | b3:ID | donante | ninguna)
    gritos = "iw:JANENBA"          # opcional: gritos de los golpes (por defecto, la fuente de `voces`;
                                   # "b1:13" = banco de Budokai 1 del personaje 13 del ELF)

    [[capsula]]                    # opcional, varias: capsulas propias (ver Capsules mas abajo)
    nombre = "Hell Gate"
    tipo = "especial"              # especial | definitiva | transformacion (+ forma = 1)

Un traje EXTRA para un personaje que ya existe es otro mod fuente, con `traje.toml`:

    [traje]
    personaje = 0            # ID del personaje (0 = Goku)
    nombre = "Armadura Saiyan"
    modelos = ["modelos/base.amb", "modelos/kaioken.amb", ...]  # uno por forma, en el
                             # orden de sus transformaciones (Goku: base, Kaioken, SSJ, SSJ2,
                             # SSJ3, SSJ4); si faltan, se repite el ultimo

Los trajes se anaden detras de los del juego (el select los ofrece como uno mas). Las
formas que el juego guarda como otro personaje (Goku SSJ4) reciben su modelo alli.

`construir` junta TODOS los mods fuente activos en un unico mod generado `_roster`:
asigna IDs libres y entradas nuevas de data_cmn (al final del AFS), convierte los
modelos PS2->HD (ps2hd/amo2awo), copia y re-etiqueta las bocas del donante, pinta
iconos y rotulos en el espacio libre de las texturas 11/12 del select (data_usi
#2027), genera los retratos P1/P2 y calcula el encuadre del select con la altura del
esqueleto. El runtime (src/roster_ext.cpp + select_ext.cpp) lee `_roster/roster.toml`.

  python roster_build.py construir [--mods DIR] [--us DIR] [--force]
  python roster_build.py estado [--json]          # plazas libres / ocupadas y por que mod
  python roster_build.py vista --mod cut_guldo [--icono-ajuste 1.1,0,0.02,12] [--guardar]
  python roster_build.py capsulas --mod cut_janemba [--anadir "Hell Gate" especial] [--quitar 0]
  python roster_build.py capsulas --mod cut_janemba --importar [auto|iw|b1|b2|b3] [--lista nombres]
                                  # capsulas desde el moveset del port (nombres del juego de origen)
  python roster_build.py nuevo --mod cut_guldo --nombre Guldo --donante 21 \\
         --modelo a.amb --modelo b.amb [--cara cara.png] [--retrato retrato.png] [--id 22]
         [--captura preview.png]   # collage 3x2 de capturas: cara (celda 1) y retrato (0)

Necesita el runtime con entradas AFS anadidas (AfsVirtualSize, handback 2026-10-03).
"""
import argparse
import hashlib
import io
import json
import os
import shutil
import struct
import sys
import tempfile
import time
import tomllib
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
for p in (os.path.join(HERE, "lib"), os.path.join(ROOT, "awo_tools")):
    if os.path.isdir(p) and p not in sys.path:
        sys.path.append(p)
sys.path.insert(0, HERE)
import swap_b3  # noqa: E402  (LZX + AFS)
import capsulas  # noqa: E402  (catalogo de capsulas, BCM, nombres)
import voces  # noqa: E402  (voces de combate: tabla por ID + ADX nuevos)
import gritos  # noqa: E402  (gritos de los golpes: banco lang_* por personaje)
import colores  # noqa: E402  (color del aura y del ki)

VERSION = 1
OUT_MOD = "_roster"
DB = json.load(open(os.path.join(HERE, "roster_db.json"), encoding="utf-8"))
IDS = {e["id"]: e for e in DB["ids"]}
# Plazas: los 6 IDs recortados de fabrica (22-26, 31) y los 20 IDs extra (44-63), vacios en
# todas las tablas por ID del juego (char96/char372 tienen 105 entradas; 44-63 sin usar).
ALL_IDS = list(DB["free_ids"]) + list(DB.get("extra_ids", []))
LZX = b"\x0f\xf5\x12\xee"
SELECT_ENTRY = 2027            # data_usi: texturas del select (#AZT)
# El juego lee data_usi (ingles USA) o, con otro idioma, data_eng/spn/fra/ger/ita: las
# cinco tienen la misma estructura (2709 entradas, mismas texturas del select y catalogo).
# Todo lo que se escribe en data_usi se repite en ellas (sin esto, en espanol las casillas
# nuevas salian sin icono y el juego se cerraba al pasar por ellas).
DATA_LANGS = ("eng", "spn", "fra", "ger", "ita")
SELECT_POSES = (3881, 3882)    # data_cmn: poses del select (#ACM, una por ID; #CSK codigo = ID)
ICON_TEX = 11                  # textura 1024x1024 del marco de la rueda (45 % libre)
FONT = "C:/Windows/Fonts/comicbd.ttf"
PORTRAIT_SCALE = 1.3


def log(*a):
    print(*a, flush=True)


def default_mods():
    for p in (os.path.join(ROOT, "mods"), os.path.join(ROOT, "out", "build", "win-amd64-release", "mods")):
        if os.path.isdir(p):
            return p
    return os.path.join(ROOT, "mods")


def default_us():
    return os.path.dirname(swap_b3.DEFAULT_AFS)


# ---------------------------------------------------------------- AFS / LZX
class Afs:
    def __init__(self, path, work):
        self.path, self.work = path, work
        self.index = swap_b3.read_afs_index(path)

    def raw(self, n):
        a, s = self.index[n]
        with open(self.path, "rb") as f:
            f.seek(a)
            return f.read(s)

    def entry(self, n):
        return unlzx(self.raw(n), self.work)


def unlzx(b, work):
    if b[:4] != LZX:
        return bytes(b)
    src, dst = os.path.join(work, "in.lzx"), os.path.join(work, "out.bin")
    open(src, "wb").write(b)
    swap_b3.lzx_decompress(src, dst, work)
    return open(dst, "rb").read()


def write_entry(mod_dir, afs, n, data, work):
    d = os.path.join(mod_dir, "us", afs, str(n))
    os.makedirs(d, exist_ok=True)
    raw = os.path.join(work, "raw.bin")
    open(raw, "wb").write(data)
    swap_b3.lzx_compress(raw, os.path.join(d, "geom.bin"), work)


# ---------------------------------------------------------------- poses del select
def _suffix(name):
    return name.split("_", 1)[1] if "_" in name else name


def acm_read(b):
    """#ACM HD -> (nombres, [(flags, var, frames, {sufijo: [pista giro, pista pos]} | None)])."""
    n, t, nb, no = struct.unpack(">4I", b[0x10:0x20])
    names = [b[no + 32 * i:no + 32 * i + 32].split(b"\0")[0].decode("latin1") for i in range(nb)]
    anims = []
    for k in range(n):
        flags, var, nf, off = struct.unpack_from(">4I", b, t + 16 * k)
        if not off:
            anims.append((flags, var, nf, None))
            continue
        per = 3 if flags & 0x10 else 2
        bones = {}
        for j in range(nb):
            tr = []
            for i in range(2):
                p = struct.unpack_from(">I", b, off + 4 * (per * j + i))[0]
                nk = struct.unpack_from(">I", b, p + 8)[0] if p else 0
                tr.append(b[p:p + 12 + (8 if i == 0 else 16) * nk] if p else None)
            bones[_suffix(names[j])] = tr
        anims.append((flags, var, nf, bones))
    return names, anims


def acm_append(b, new):
    """#ACM HD con las animaciones `new` [(flags, var, frames, {sufijo: pistas})] anadidas al
    final (las pistas se asignan a los huesos de la tabla por sufijo; sin escala)."""
    n, t, nb, no = struct.unpack(">4I", b[0x10:0x20])
    names = [b[no + 32 * i:no + 32 * i + 32].split(b"\0")[0].decode("latin1") for i in range(nb)]
    old_start = t + 16 * n
    start = (t + 16 * (n + len(new)) + 15) // 16 * 16
    shift = start - (old_start + 15) // 16 * 16
    out = bytearray(b[:t])
    for k in range(n):
        flags, var, nf, off = struct.unpack_from(">4I", b, t + 16 * k)
        out += struct.pack(">4I", flags, var, nf, off + shift if off else 0)
    out += bytes(16 * len(new))
    out += bytes(start - len(out))
    data = bytearray(b[(old_start + 15) // 16 * 16:no])
    for k in range(n):
        flags, var, nf, off = struct.unpack_from(">4I", b, t + 16 * k)
        if not off:
            continue
        per = 3 if flags & 0x10 else 2
        base = off - (old_start + 15) // 16 * 16
        for q in range(nb * per):
            p = struct.unpack_from(">I", data, base + 4 * q)[0]
            if p:
                struct.pack_into(">I", data, base + 4 * q, p + shift)
    out += data
    for k, (flags, var, nf, bones) in enumerate(new):
        out += bytes((-len(out)) % 16)
        blk = len(out)
        out += bytes(8 * nb)
        for j, nm in enumerate(names):
            for i, tr in enumerate((bones.get(_suffix(nm)) or [None, None])[:2]):
                if tr:
                    out += bytes((-len(out)) % 4)
                    struct.pack_into(">I", out, blk + 4 * (2 * j + i), len(out))
                    out += tr
        struct.pack_into(">4I", out, t + 16 * (n + k), flags & ~0x10, var, nf, blk)
    out += bytes((-len(out)) % 16)
    struct.pack_into(">2I", out, 0x10, n + len(new), t)
    struct.pack_into(">I", out, 0x1C, len(out))
    for nm in names:
        out += nm.encode("latin1")[:31].ljust(32, b"\0")
    return bytes(out)


def select_poses(cmn, poses):
    """Poses del select de los IDs nuevos: el reposo (codigo 0) de su moveset se anade al
    banco comun (#ACM 3881) y el #CSK 3882 apunta el codigo = ID a un bloque como el de Goku.
    Sin pose propia el runtime usa la del donante. poses: [(ID, anm HD)] -> (acm, csk)."""
    acm, csk = cmn.entry(SELECT_POSES[0]), bytearray(cmn.entry(SELECT_POSES[1]))
    n_acm = struct.unpack(">I", acm[0x10:0x14])[0]
    n, lst, n_hr, hr = struct.unpack(">4I", csk[0x10:0x20])
    tpl = csk[struct.unpack_from(">I", csk, lst)[0]:][:48]
    new, out = [], bytearray(csk[:hr])
    for cid, anm in poses:
        kids = capsulas.amb_children(anm)
        bsk = anm[kids[0][0]:kids[0][0] + kids[0][1]]
        own = anm[kids[1][0]:kids[1][0] + kids[1][1]]
        bn, blst = struct.unpack(">II", bsk[0x10:0x18])
        a0 = struct.unpack_from(">I", bsk, blst)[0]
        anim, pool = struct.unpack_from(">HH", bsk, a0)
        if cid >= n or not a0 or pool != 3:
            continue
        flags, var, nf, bones = acm_read(own)[1][anim]
        if bones is None:
            continue
        new.append((flags, cid, nf, bones))
        blk = bytearray(tpl)
        struct.pack_into(">H", blk, 0, n_acm + len(new) - 1)
        # sin tabla AP: el juego reubica cada tabla una vez por bloque que la usa, y una
        # compartida con la de Goku acababa apuntando a basura (cierre al entrar al select)
        struct.pack_into(">3I", blk, 0x24, 0, 0, 0)
        out += bytes((-len(out)) % 16)
        struct.pack_into(">I", out, lst + 4 * cid, len(out))
        out += blk
    if not new:
        return None, None
    out += bytes((-len(out)) % 16)
    struct.pack_into(">I", out, 0x1C, len(out))
    return acm_append(acm, new), bytes(out + csk[hr:])


# ---------------------------------------------------------------- altura
def donor_moveset_fixed(cmn, dn, model_hd, out_dir, next_fid, work, name):
    """Movesets del donante (uno por forma) con la cadera escalada al largo de pierna del
    modelo nuevo (awo_tools/altura.py) -> (fids por forma, next_fid, primero); ([], .., None)
    si ya cuadra (diferencia < 3 % / 0.15)."""
    import altura  # noqa: PLC0415
    fids = [f for f in dn["anm"] if 0 < f < 0xFFFFFFFF]
    if not fids or not dn.get("models"):
        return [], next_fid, None
    base = cmn.entry(fids[0])
    s, dy = altura.correccion(altura.skeleton(model_hd), altura.skeleton(cmn.entry(dn["models"][0])),
                              altura.any_tracks(base))
    if abs(s - 1.0) < 0.03 and abs(dy) < 0.15:
        log("   %s: altura: el moveset del donante ya cuadra (x%.3f %+.2f)" % (name, s, dy))
        return [], next_fid, None
    new, first = {}, None
    for f in fids:
        if f not in new:
            fixed = altura.scale_hips(cmn.entry(f), s, dy)
            first = first or fixed
            write_entry(out_dir, "data_cmn.afs", next_fid, fixed, work)
            new[f] = next_fid
            next_fid += 1
    log("   %s: altura: cadera de las animaciones del donante x%.3f %+.2f" % (name, s, dy))
    return [new.get(f, 0xFFFFFFFF) for f in dn["anm"]], next_fid, first


# ---------------------------------------------------------------- modelos
def to_hd(b, work):
    """Bin del modelo -> #AMB HD descomprimido (acepta LZX, #AMB HD o #AMB PS2)."""
    b = unlzx(b, work)
    if b[:4] != b"#AMB":
        raise ValueError("no es un #AMB (%r)" % b[:4])
    if struct.unpack("<I", b[0xC:0x10])[0] == 3:      # PS2 (little-endian, version 3)
        import ps2hd  # noqa: PLC0415
        return bytes(ps2hd.convert_block(b))
    if struct.unpack(">I", b[0xC:0x10])[0] == 2:      # HD
        return bytes(b)
    raise ValueError("version de #AMB desconocida")


def skeleton(hd_bin, work):
    """(etiquetas, altura) del esqueleto en bind (traslaciones de los huesos)."""
    from awg_vertex_buffer import AwgVertexBuffer  # noqa: PLC0415
    p = os.path.join(work, "skel.bin")
    open(p, "wb").write(hd_bin)
    a = AwgVertexBuffer.load(p)
    w, _ = a.bind_worlds()
    ys = [m[1][3] for m in w]
    return a.bone_labels(), max(ys) - min(ys)


def prefix(labels):
    for lab in labels:
        s = lab.decode("latin1") if isinstance(lab, bytes) else str(lab)
        s = s.lstrip("X")
        if "_" in s:
            return s.split("_")[0]
    return None


# ---------------------------------------------------------------- #AZT
def azt_textures(b):
    z = b.find(b"#AZT")
    n, idx = struct.unpack(">II", b[z + 0x10:z + 0x18])
    out = {}
    for t in range(n):
        o = struct.unpack(">I", b[z + idx + 4 * t:z + idx + 4 * t + 4])[0]
        e = struct.unpack(">IIHHHHHH", b[z + o:z + o + 20])
        do, ds = struct.unpack(">II", b[z + o + 20:z + o + 28])
        out[t] = dict(at=z + do, size=ds, hd=(e[2], e[3]), logical=(e[6], e[7]))
    return out


def azt_read(b, t):
    return np.array(Image.open(io.BytesIO(bytes(b[t["at"]:t["at"] + t["size"]]))).convert("RGBA"))


def azt_write(b, t, img):
    hdr = bytes(b[t["at"]:t["at"] + 128])
    if struct.unpack("<I", hdr[84:88])[0] != 0:
        raise ValueError("textura comprimida: no soportada")
    data = img[..., [2, 1, 0, 3]].astype(np.uint8).tobytes()
    assert len(data) == t["size"] - 128
    b[t["at"] + 128:t["at"] + t["size"]] = data


NAME_TEX_TEMPLATES = (34, 25)   # rotulos nativos: DDS 256x64 y 512x64 (A8R8G8B8)


def azt_append(b, items):
    """Anade texturas al #AZT SIN desplazar nada: datos DDS, entradas de 0x30 B y una
    tabla indice nueva (n+K) al final; la cabecera (+0x10 n, +0x14 indice) apunta a
    ella. items = [(rgba (H,W,4), (ancho_logico, alto_logico), (ancho_hd, alto_hd), plantilla)].
    Devuelve (bin, [indices nuevos])."""
    z = b.find(b"#AZT")
    n, idx = struct.unpack(">II", b[z + 0x10:z + 0x18])
    offs = [struct.unpack(">I", b[z + idx + 4 * t:z + idx + 4 * t + 4])[0] for t in range(n)]
    out = bytearray(b)
    last = offs[-1]
    vram = struct.unpack(">I", b[z + last + 0x24:z + last + 0x28])[0]
    new = []
    for img, (lw, lh), (cw, ch), tpl in items:
        to = offs[tpl]
        tdo, tds = struct.unpack(">II", b[z + to + 0x14:z + to + 0x1C])
        hdr = bytearray(b[z + tdo:z + tdo + 128])
        H, W = struct.unpack("<II", hdr[12:20])
        assert img.shape[:2] == (H, W)
        out += b"\0" * ((-len(out)) % 0x80)
        data_off = len(out) - z
        out += hdr + img[..., [2, 1, 0, 3]].astype(np.uint8).tobytes()
        ent = bytearray(b[z + to:z + to + 0x30])
        vram += tds // 0x20
        log2 = lambda v: max(0, int(np.ceil(np.log2(max(1, v)))))
        struct.pack_into(">IIHHHHHHII", ent, 0, n + len(new), struct.unpack(">I", ent[4:8])[0],
                         cw, ch, log2(lw), log2(lh), lw, lh, data_off, len(hdr) + img.nbytes)
        struct.pack_into(">I", ent, 0x24, vram)
        new.append(bytes(ent))
    out += b"\0" * ((-len(out)) % 0x10)
    ent_offs = []
    for ent in new:
        ent_offs.append(len(out) - z)
        out += ent
    new_idx = len(out) - z
    out += struct.pack(">%dI" % (n + len(new)), *(offs + ent_offs))
    out += b"\0" * ((-len(out)) % 0x10)
    struct.pack_into(">II", out, z + 0x10, n + len(new), new_idx)
    return out, list(range(n, n + len(new)))


def azt_template(b, tpl, w, h):
    """Textura del #AZT con DDS sin comprimir de w x h: tpl si cuadra, si no la primera que
    cuadre (los rotulos nativos cambian de ancho segun el idioma). None si no hay."""
    z = b.find(b"#AZT")
    n, idx = struct.unpack(">II", b[z + 0x10:z + 0x18])

    def fits(t):
        to = struct.unpack(">I", b[z + idx + 4 * t:z + idx + 4 * t + 4])[0]
        tdo = struct.unpack(">I", b[z + to + 0x14:z + to + 0x18])[0]
        hdr = b[z + tdo:z + tdo + 128]
        return struct.unpack("<II", hdr[12:20]) == (h, w) and struct.unpack("<I", hdr[84:88])[0] == 0
    return next((t for t in [tpl] + list(range(n)) if fits(t)), None)


class Packer:
    """Rectangulos LOGICOS libres de una textura (fuera del contenido usado)."""

    def __init__(self, tex, cell_w, cell_h, regions):
        self.k = tex["hd"][0] / tex["logical"][0]     # px HD por unidad logica (1.5)
        self.slots = []
        for x0, y0, x1, y1 in regions:
            y = y0
            while y + cell_h <= y1:
                x = x0
                while x + cell_w <= x1:
                    self.slots.append((x, y))
                    x += cell_w + 2
                y += cell_h + 2

    def take(self):
        if not self.slots:
            raise RuntimeError("no queda espacio libre en la textura del select")
        return self.slots.pop(0)


def free_regions(tex):
    lw, lh = tex["logical"]
    k = tex["hd"][0] / lw
    full = int(1024 / k)             # textura de 1024 px
    return [(lw + 4, 2, full - 2, full - 2), (2, lh + 4, lw, full - 2)]


# ---------------------------------------------------------------- imagenes
def crop_frac(img, cx, cy, side_h, aspect=1.0):
    """Recorte centrado en (cx, cy) (fracciones) de alto side_h*alto y ancho alto*aspect."""
    w, h = img.size
    hh = side_h * h
    ww = hh * aspect
    x0, y0 = cx * w - ww / 2, cy * h - hh / 2
    return img.crop((int(x0), int(y0), int(x0 + ww), int(y0 + hh)))


NAME_MARGIN = 13      # margen izquierdo de los rotulos oficiales (px HD)


def make_name(text, px_h, max_px_w):
    f = capsulas.load_font(FONT, 64)
    tmp = Image.new("RGBA", (64 * len(text) + 60, 120), (0, 0, 0, 0))
    ImageDraw.Draw(tmp).text((15, 15), text, font=f, fill=(255, 255, 255, 255),
                             stroke_width=7, stroke_fill=(0, 0, 0, 255))
    a = np.array(tmp)
    ys, xs = np.where(a[..., 3] > 20)
    crop = tmp.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
    th = int(px_h * 0.78)
    tw = min(max_px_w - NAME_MARGIN - 4, int(crop.width * th / crop.height))
    crop = crop.resize((tw, th), Image.LANCZOS)
    out = Image.new("RGBA", (tw + NAME_MARGIN + 4, px_h), (0, 0, 0, 0))
    out.alpha_composite(crop, (NAME_MARGIN, (px_h - th) // 2))
    return out


# ---------------------------------------------------------------- imagenes del select
# Fuente de cada imagen (personaje.toml):
#   icono_fuente   = "modelo" (render del propio modelo, por defecto) | "imagen" (ui/cara.png:
#                    arte del modder, mejor con fondo transparente; se le pone el fondo y el
#                    aro oficiales) | "terminado" (ui/icono.png tal cual)
#   retrato_fuente = "modelo" | "imagen" (ui/retrato.png) | "terminado" (ui/retrato_p1.png y
#                    ui/retrato_p2.png tal cual)
#   icono_ajuste / retrato_ajuste = [zoom, dx, dy, giro]  (dx/dy en fraccion del ancho/alto)
ICON_ADJ = [1.0, 0.0, 0.0, 3.0]      # zoom, dx, dy, giro (relativos al encuadre calibrado)
PORTRAIT_ADJ = [1.0, 0.0, 0.0, 35.0]


def image_source(c, ui, kind):
    """Fuente de la imagen: la elegida en personaje.toml o, si no hay, las imagenes propias
    del mod (p.ej. las de un port de Infinite World) y si no trae, el render del modelo."""
    src = c.get(kind + "_fuente")
    if src in ("modelo", "imagen", "terminado"):
        return src
    orig = original_sources(ui)
    return orig[0 if kind == "icono" else 1] if orig else "modelo"


def original_sources(ui):
    """Fuentes para «conservar las imagenes originales» de un port (p.ej. Infinite World):
    (icono, retrato) o None si el mod no trae imagenes propias."""
    has = lambda f: os.path.exists(os.path.join(ui, f))  # noqa: E731
    # solo cuentan las imagenes TERMINADAS (icono.png / retrato_p1.png): cara.png y
    # retrato.png sueltos son arte de reserva para cuando el modelo no se puede renderizar
    if not (has("icono.png") or has("retrato_p1.png")):
        return None
    ic = "terminado" if has("icono.png") else "imagen" if has("cara.png") else "modelo"
    por = "terminado" if has("retrato_p1.png") else "imagen" if has("retrato.png") else "modelo"
    return ic, por


def adjust(c, kind):
    base = list(ICON_ADJ if kind == "icono" else PORTRAIT_ADJ)
    v = c.get(kind + "_ajuste")
    if isinstance(v, list):
        for i, x in enumerate(v[:4]):
            base[i] = float(x)
    return base


def select_images(c, ui, hd_model, portrait_size, want=("icono", "retrato"), fetch=None):
    """-> (icono RGBA 84x84, [retrato P1, retrato P2] RGBA alto x ancho o None si
    'terminado' de 512x512, avisos). `fetch(entrada)` lee data_cmn (rastreador de los
    cuerpos nativos y afro de Mr. Satan, que van en un bin aparte)."""
    import model_render as mr  # noqa: PLC0415
    notes = []
    model = None

    def get_model():
        nonlocal model
        if model is None:
            model = mr.Model(hd_model)
            acc = mr.native_accessory(model)
            if acc is not None and fetch is not None:
                model.attach(fetch(acc[0]), acc[1])
        return model
    # icono
    src = image_source(c, ui, "icono") if "icono" in want else "-"
    z, dx, dy, yaw = adjust(c, "icono")
    icon = None
    if src == "-":
        icon = np.zeros((mr.ICON_PX, mr.ICON_PX, 4), np.uint8)
    if src == "terminado":
        icon = np.array(Image.open(os.path.join(ui, "icono.png")).convert("RGBA").resize(
            (mr.ICON_PX, mr.ICON_PX), Image.LANCZOS))
    if icon is None and src == "modelo":
        try:
            icon = mr.make_icon(get_model(), yaw=yaw, zoom=z, dx=dx, dy=dy)
        except Exception as e:  # noqa: BLE001
            notes.append("icono: el modelo no se pudo renderizar (%s); se usa ui/cara.png" % e)
    if icon is None:
        art = os.path.join(ui, "cara.png")
        if not os.path.exists(art):
            raise RuntimeError("falta ui/cara.png para el icono")
        icon = mr.make_icon(None, zoom=z, dx=dx, dy=dy, art=Image.open(art))
    # retratos
    src = image_source(c, ui, "retrato") if "retrato" in want else "-"
    z, dx, dy, yaw = adjust(c, "retrato")
    pors = None
    if src == "-":
        return icon, None, notes
    if src == "terminado":
        pors = [None, None]
    if pors is None and src == "modelo":
        try:
            pors = mr.make_portraits(get_model(), portrait_size, yaw=yaw, zoom=z, dx=dx, dy=dy)
        except Exception as e:  # noqa: BLE001
            notes.append("retrato: el modelo no se pudo renderizar (%s); se usa la imagen" % e)
    if pors is None:
        art = next((os.path.join(ui, f) for f in ("retrato.png", "cara.png")
                    if os.path.exists(os.path.join(ui, f))), None)
        if art is None:
            raise RuntimeError("falta ui/retrato.png para los retratos")
        pors = mr.make_portraits(None, portrait_size, zoom=z, dx=dx, dy=dy, art=Image.open(art))
    return icon, pors, notes


def load_bin(path, work):
    """Fichero de datos del personaje -> bin HD (LZX se descomprime; #AMB PS2 se convierte)."""
    b = unlzx(open(path, "rb").read(), work)
    if b[:4] == b"#AMB" and struct.unpack("<I", b[0xC:0x10])[0] == 3:
        import ps2hd  # noqa: PLC0415
        b = bytes(ps2hd.convert_block(b))
    return b


def portrait_ready(template, img):
    """Bin HD de retrato con una imagen ya terminada del tamano de la textura."""
    h = bytearray(template)
    o = struct.unpack(">I", h[0x20:0x24])[0]
    e = struct.unpack(">3I2H2H7I", h[o:o + 0x30])
    do, ds = e[7], e[8]
    W, H = struct.unpack("<II", h[do + 12:do + 20])[::-1]
    a = np.array(img.convert("RGBA").resize((W, H), Image.LANCZOS))
    px = a[..., [2, 1, 0, 3]].tobytes()
    assert len(px) == ds - 128
    h[do + 128:do + ds] = px
    return bytes(h)


def portrait_size(template):
    """(ancho, alto) en px HD de la zona util del retrato (logico x 1,3)."""
    o = struct.unpack(">I", template[0x20:0x24])[0]
    e = struct.unpack(">3I2H2H7I", template[o:o + 0x30])
    return int(round(e[5] * PORTRAIT_SCALE)), int(round(e[6] * PORTRAIT_SCALE))


def portrait_bin(template, content):
    """Bin HD de retrato: plantilla nativa (mascara de medio disco) + contenido RGBA
    (alto x ancho de la zona util, ya con fondo; de model_render.make_portraits)."""
    h = bytearray(template)
    o = struct.unpack(">I", h[0x20:0x24])[0]
    e = struct.unpack(">3I2H2H7I", h[o:o + 0x30])
    do, ds = e[7], e[8]
    tpl = np.array(Image.open(io.BytesIO(bytes(h[do:do + ds]))).convert("RGBA"))
    H, W = tpl.shape[:2]
    ch, cw = content.shape[:2]
    img = np.zeros((H, W, 4), np.uint8)
    img[:min(ch, H), :min(cw, W)] = content[:H, :W]
    img[..., 3] = tpl[..., 3]                  # misma mascara que el retrato nativo
    img[img[..., 3] == 0] = 0
    px = img[..., [2, 1, 0, 3]].tobytes()
    assert len(px) == ds - 128
    h[do + 128:do + ds] = px
    return bytes(h)


def hud_bin(template, imgs):
    """Caras de la barra de vida (*_HUD.amt: #AZT con una textura por forma, DDS 256x128
    A8R8G8B8) con la plantilla nativa del donante: misma entrada para cada forma."""
    n0, idx0 = struct.unpack(">II", template[0x10:0x18])
    e0 = struct.unpack(">I", template[idx0:idx0 + 4])[0]
    ent = bytearray(template[e0:e0 + 0x30])
    do, ds = struct.unpack(">II", ent[0x14:0x1C])
    dds = bytes(template[do:do + 128])
    H, W = struct.unpack("<II", dds[12:20])
    n = len(imgs)
    first = (0x20 + 4 * n + 0xF) // 0x10 * 0x10
    data = (first + 0x30 * n + 0x7F) // 0x80 * 0x80
    out = bytearray(template[:0x20]) + bytes(data - 0x20)
    struct.pack_into(">II", out, 0x10, n, 0x20)
    vram = struct.unpack(">I", ent[0x24:0x28])[0]
    body_at = len(out)
    for k, img in enumerate(imgs):
        a = np.zeros((H, W, 4), np.uint8)
        a[:min(H, img.shape[0]), :min(W, img.shape[1])] = img[:H, :W]
        blob = dds + a[..., [2, 1, 0, 3]].tobytes()
        e = bytearray(ent)
        struct.pack_into(">I", e, 0, k)
        struct.pack_into(">II", e, 0x14, len(out), len(blob))
        struct.pack_into(">I", e, 0x24, vram + k * (len(blob) // 0x20))
        out[first + 0x30 * k:first + 0x30 * k + 0x30] = e
        struct.pack_into(">I", out, 0x20 + 4 * k, first + 0x30 * k)
        out += blob + bytes((-len(blob)) % 0x80)
    struct.pack_into(">I", out, 0x1C, zlib.crc32(bytes(out[body_at:])))
    return bytes(out)


def mr_hud_w():
    import model_render as mr  # noqa: PLC0415
    return mr.HUD_SIZE[0] + 2 * int(mr.HUD_GLOW_SIGMA)


def mr_hud_h():
    import model_render as mr  # noqa: PLC0415
    return mr.HUD_SIZE[1]


def hud_images(c, ui, form_models, fetch=None):
    """Caras de la barra de vida, una por forma: ui/hud.png (o hud_1.png, hud_2.png... por
    forma) terminadas de 256x128, o render de cada modelo (hud_ajuste = [zoom, dx, dy, giro])."""
    import model_render as mr  # noqa: PLC0415
    z, dx, dy, yaw = 1.0, 0.0, 0.0, 0.0
    v = c.get("hud_ajuste")
    if isinstance(v, list):
        z, dx, dy, yaw = ([float(x) for x in v] + [1.0, 0.0, 0.0, 0.0][len(v):])[:4]
    out, cache = [], {}
    for f, hd in enumerate(form_models):
        ready = next((os.path.join(ui, n) for n in ("hud_%d.png" % (f + 1), "hud.png")
                      if os.path.exists(os.path.join(ui, n))), None)
        if ready:
            out.append(np.array(Image.open(ready).convert("RGBA").resize(mr.HUD_CANVAS, Image.LANCZOS)))
            continue
        if id(hd) not in cache:
            m = mr.Model(hd)
            acc = mr.native_accessory(m)
            if acc is not None and fetch is not None:
                m.attach(fetch(acc[0]), acc[1])
            cache[id(hd)] = mr.make_hud(m, yaw=yaw, zoom=z, dx=dx, dy=dy)
        out.append(cache[id(hd)])
    return out


# ---------------------------------------------------------------- trajes extra
def traje_mods(mods_dir):
    out = []
    for name in sorted(os.listdir(mods_dir)):
        d = os.path.join(mods_dir, name)
        f = os.path.join(d, "traje.toml")
        if name == OUT_MOD or not os.path.isfile(f) or os.path.exists(os.path.join(d, ".disabled")):
            continue
        with open(f, "rb") as fh:
            out.append((name, d, dict(tomllib.load(fh).get("traje", {}))))
    return out


def build_trajes(trajes, cmn, out_dir, next_fid, work, toml, manifest):
    """Trajes extra: un modelo por forma (en el orden de las formas del personaje). El
    runtime coloca cada uno donde lo busca el juego (incluidas las formas que son otro
    ID, como Goku SSJ4 = ID 91) y suma el traje al select."""
    for name, d, c in trajes:
        cid = int(c.get("personaje", -1))
        if cid not in IDS or not IDS[cid].get("models"):
            log("!! %s: personaje %r sin modelos en el juego" % (name, cid))
            continue
        files = c.get("modelos", [])
        if not files:
            log("!! %s: sin modelos" % name)
            continue
        fids, cache = [], {}
        for rel in files:
            if rel not in cache:
                write_entry(out_dir, "data_cmn.afs", next_fid,
                            to_hd(open(os.path.join(d, rel), "rb").read(), work), work)
                cache[rel] = next_fid
                next_fid += 1
            fids.append(cache[rel])
        dn = IDS[cid]
        toml += ["[[personaje]]", "# %s: traje extra (mod %s)" % (dn["name"], name), "id = %d" % cid,
                 "traje_formas = %s" % toml_list(fids), ""]
        manifest.append("%s (id %d): traje extra %s, modelos por forma %s"
                        % (dn["name"], cid, c.get("nombre", name), fids))
        log("+ " + manifest[-1])
    return next_fid


# ---------------------------------------------------------------- construir
def source_mods(mods_dir):
    out = []
    for name in sorted(os.listdir(mods_dir)):
        d = os.path.join(mods_dir, name)
        f = os.path.join(d, "personaje.toml")
        if name == OUT_MOD or not os.path.isfile(f) or os.path.exists(os.path.join(d, ".disabled")):
            continue
        with open(f, "rb") as fh:
            doc = tomllib.load(fh)
        cfg = dict(doc.get("personaje", {}))
        cfg["_capsulas"] = doc.get("capsula", [])
        out.append((name, d, cfg))
    return out


FREE_NAMES = {22: "Guldo", 23: "Jeice", 24: "Burter", 25: "Zarbon", 26: "Dodoria", 31: "Androide 19"}


def slot_name(i):
    return FREE_NAMES.get(i, "extra %d" % (i - 43))


def assign_ids(srcs):
    """Plaza (ID libre) de cada mod fuente activo: primero las pedidas (en orden de
    carpeta; si dos piden la misma, gana la primera), luego la primera libre (antes las
    6 de los recortados, despues las 20 extra)."""
    free = ALL_IDS
    used, ids = set(), {}
    for name, _, c in srcs:
        if c.get("id") in free and c.get("id") not in used:
            ids[name] = c["id"]
            used.add(c["id"])
    for name, _, c in srcs:
        if name not in ids:
            ids[name] = next((i for i in free if i not in used), None)
            used.add(ids[name])
    return ids


def all_source_mods(mods_dir):
    """Todos los mods fuente (activos o no): [(carpeta, ruta, cfg, activo)]."""
    out = []
    for name in sorted(os.listdir(mods_dir)):
        d = os.path.join(mods_dir, name)
        f = os.path.join(d, "personaje.toml")
        if name == OUT_MOD or not os.path.isfile(f):
            continue
        try:
            with open(f, "rb") as fh:
                cfg = tomllib.load(fh).get("personaje", {})
        except (OSError, tomllib.TOMLDecodeError):
            cfg = {}
        out.append((name, d, cfg, not os.path.exists(os.path.join(d, ".disabled"))))
    return out


def status(a):
    """Estado de las plazas (IDs libres): libre / ocupada por que mod (JSON)."""
    mods = a.mods or default_mods()
    allm = all_source_mods(mods)
    ids = assign_ids([(n, d, c) for n, d, c, on in allm if on])
    by_id = {v: k for k, v in ids.items() if v is not None}
    names = {n: c.get("nombre", n) for n, _, c, _ in allm}
    out = {"max": len(ALL_IDS), "plazas": [], "personajes": []}
    for i in ALL_IDS:
        m = by_id.get(i)
        out["plazas"].append({"id": i, "original": slot_name(i), "estado": "ocupada" if m else "libre",
                              "mod": m or "", "nombre": names.get(m, "") if m else ""})
    for n, _, c, on in allm:
        req = c.get("id")
        got = ids.get(n) if on else None
        out["personajes"].append({"mod": n, "nombre": c.get("nombre", n), "activo": on,
                                  "id_pedido": req if isinstance(req, int) else None, "id": got,
                                  "conflicto": bool(on and isinstance(req, int) and got != req),
                                  "sin_plaza": bool(on and got is None)})
    txt = json.dumps(out, ensure_ascii=False, indent=1)
    if a.json:
        print(txt)
    else:
        for p in out["plazas"]:
            log("plaza %2d (%s): %s" % (p["id"], p["original"],
                                        "ocupada por %s (%s)" % (p["mod"], p["nombre"]) if p["mod"] else "libre"))
        for p in out["personajes"]:
            if p["conflicto"] or p["sin_plaza"]:
                log("!! %s pide la plaza %s: %s" % (p["mod"], p["id_pedido"],
                                                     "sin plaza libre" if p["sin_plaza"] else "ocupada; usa %s" % p["id"]))
    return 0


def set_toml_keys(path, values):
    """Escribe/actualiza claves `k = v` en la seccion [personaje] (v ya en sintaxis TOML;
    None borra la clave)."""
    lines = open(path, encoding="utf-8").read().splitlines()
    sec, end = None, len(lines)
    for i, ln in enumerate(lines):
        s = ln.strip()
        if s.startswith("["):
            if s == "[personaje]":
                sec = i
            elif sec is not None and end == len(lines):
                end = i
    if sec is None:
        lines.insert(0, "[personaje]")
        sec, end = 0, end + 1
    for k, v in values.items():
        hit = None
        for i in range(sec + 1, end):
            if lines[i].split("=")[0].strip() == k:
                hit = i
                break
        if v is None:
            if hit is not None:
                del lines[hit]
                end -= 1
        elif hit is not None:
            lines[hit] = "%s = %s" % (k, v)
        else:
            lines.insert(end, "%s = %s" % (k, v))
            end += 1
    open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")


def write_rgba(path, img):
    """Imagen cruda para el launcher: u32 ancho, u32 alto (LE) + RGBA8."""
    h, w = img.shape[:2]
    with open(path, "wb") as f:
        f.write(struct.pack("<II", w, h))
        f.write(np.ascontiguousarray(img[..., :4], dtype=np.uint8).tobytes())


def preview(a):
    """Vista previa de icono, rotulo y retratos de un mod fuente (sin construir nada)
    en <mod>/ui/_vista/ (PNG + .rgba). --guardar escribe los ajustes en personaje.toml."""
    mods = a.mods or default_mods()
    d = os.path.join(mods, a.mod)
    tf = os.path.join(d, "personaje.toml")
    with open(tf, "rb") as fh:
        c = tomllib.load(fh).get("personaje", {})
    edits = {}
    ui = os.path.join(d, "ui")
    os.makedirs(ui, exist_ok=True)
    # importar arte propio (lo copia a ui/ y cambia la fuente de esa imagen)
    for src, dst, key, val in ((a.cara, "cara.png", "icono_fuente", "imagen"),
                               (a.icono, "icono.png", "icono_fuente", "terminado"),
                               (a.retrato, "retrato.png", "retrato_fuente", "imagen")):
        if src:
            Image.open(src).convert("RGBA").save(os.path.join(ui, dst))
            setattr(a, key, getattr(a, key) or val)
    if getattr(a, "imagenes", None):     # originales del port / modelo 3D (estilo B3)
        orig = original_sources(ui) if a.imagenes == "originales" else None
        if a.imagenes == "originales" and orig is None:
            raise SystemExit("el mod no trae imagenes originales en ui/")
        a.icono_fuente, a.retrato_fuente = orig or ("modelo", "modelo")
    for key, val in (("icono_fuente", a.icono_fuente), ("retrato_fuente", a.retrato_fuente)):
        if val:
            c[key] = val
            edits[key] = '"%s"' % val
    for key, val in (("icono_ajuste", a.icono_ajuste), ("retrato_ajuste", a.retrato_ajuste)):
        if val:
            v = [float(x) for x in val.split(",")]
            c[key] = v
            edits[key] = toml_list(v)
    if a.id is not None:
        c["id"] = a.id
        edits["id"] = str(a.id) if a.id in ALL_IDS else None
    if a.nombre:
        c["nombre"] = a.nombre
        edits["nombre"] = '"%s"' % a.nombre.replace('"', "")
    if a.despues_de is not None:
        edits["despues_de"] = str(a.despues_de) if a.despues_de >= 0 else None
    if a.guardar and edits:
        set_toml_keys(tf, edits)
    work = tempfile.mkdtemp(prefix="roster_v_")
    try:
        cmn = Afs(os.path.join(a.us or default_us(), "data_cmn.afs"), work)
        dn = IDS[int(c.get("donante", 21))]
        tpl_fids = DB["data_cmn_portrait_by_slot"][dn["slot"] if dn["slot"] < 39 else 21]
        tpls = [cmn.entry(tpl_fids[v] & 0xFFFF) for v in (0, 1)]
        files = c.get("modelos", [])
        out = os.path.join(ui, "_vista")
        os.makedirs(out, exist_ok=True)
        hd = None
        if files:   # cache del modelo convertido (la conversion PS2->HD es lo mas lento)
            src = os.path.join(d, files[0])
            st = os.stat(src)
            key = "%s|%d|%d" % (files[0], st.st_size, int(st.st_mtime))
            cache, ckey = os.path.join(out, ".modelo_hd.bin"), os.path.join(out, ".modelo_hd.key")
            if os.path.exists(cache) and os.path.exists(ckey) and open(ckey).read() == key:
                hd = open(cache, "rb").read()
            else:
                hd = to_hd(open(src, "rb").read(), work)
                open(cache, "wb").write(hd)
                open(ckey, "w").write(key)
        want = ("icono", "retrato") if not a.solo else (a.solo,)
        ic, pors, notes = select_images(c, ui, hd, portrait_size(tpls[0]), want, cmn.entry)
        imgs = {"rotulo": np.array(make_name(c.get("nombre", a.mod), 48, 504))}
        if "icono" in want:
            imgs["icono"] = ic
        cw, ch = portrait_size(tpls[0])
        for v in ((0, 1) if "retrato" in want else ()):
            ready = os.path.join(ui, "retrato_p%d.png" % (v + 1))
            raw = portrait_ready(tpls[v], Image.open(ready)) if pors[v] is None else portrait_bin(tpls[v], pors[v])
            o = struct.unpack(">I", raw[0x20:0x24])[0]
            e = struct.unpack(">3I2H2H7I", raw[o:o + 0x30])
            full = np.array(Image.open(io.BytesIO(raw[e[7]:e[7] + e[8]])).convert("RGBA"))
            imgs["retrato_p%d" % (v + 1)] = full[:ch, :cw]
        if not a.solo and (hd is not None or os.path.exists(os.path.join(ui, "hud.png"))):
            try:                  # cara de la barra de vida (forma base)
                imgs["hud"] = hud_images(c, ui, [hd], cmn.entry)[0][:mr_hud_h(), :mr_hud_w()]
            except Exception as e:  # noqa: BLE001
                notes.append("cara de la barra de vida: %s" % e)
        for k_, img in imgs.items():
            Image.fromarray(img).save(os.path.join(out, k_ + ".png"))
            write_rgba(os.path.join(out, k_ + ".rgba"), img)
        for n_ in notes:
            log("aviso: " + n_)
        log("vista previa -> %s" % out)
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


def inputs_hash(srcs):
    h = hashlib.sha1(("v%d" % VERSION).encode())
    h.update(open(os.path.join(HERE, "roster_db.json"), "rb").read())
    h.update(open(os.path.abspath(__file__), "rb").read())
    h.update(open(os.path.join(HERE, "model_render.py"), "rb").read())
    h.update(open(os.path.join(HERE, "voces.py"), "rb").read())
    h.update(open(os.path.join(HERE, "gritos.py"), "rb").read())
    h.update(open(os.path.join(HERE, "capsulas.py"), "rb").read())
    for name, d, _ in srcs:
        for base, _, files in sorted(os.walk(d)):
            if "_vista" in os.path.relpath(base, d).split(os.sep):
                continue              # vistas previas: no cambian el mod generado
            for fn in sorted(files):
                p = os.path.join(base, fn)
                st = os.stat(p)
                h.update(("%s|%d|%d" % (os.path.relpath(p, d), st.st_size, int(st.st_mtime))).encode())
    return h.hexdigest()


def toml_list(v):
    return "[" + ", ".join(("%.4g" % x) if isinstance(x, float) else str(x) for x in v) + "]"


# ---------------------------------------------------------------- capsulas
# [[capsula]] en personaje.toml (opcional; sin ninguna, el personaje usa las del donante):
#   nombre = "Hell Gate"      # texto en combate y en los menus
#   tipo = "especial"         # especial | definitiva | transformacion
#   forma = 1                 # (transformacion) forma a la que lleva: 1 = la primera
#   ki = 4                    # (transformacion) barras EXIGIDAS, 0..7 (no se gastan; por defecto
#                             # las de la capsula nativa del donante para esa forma, si no 5).
#                             # Cada transformacion exige la anterior de la lista (SSJ2 pide SSJ)
#   equipada = true           # entra en su lista por defecto ("Original"), maximo 7
#   reemplaza = 0x8D          # opcional: capsula del donante que sustituye en su moveset
#   descripcion = "..."       # opcional: panel de descripcion de "Edit Skills" (tambien
#   botones = "..."           #   quien / nota); sin ellos se escribe al estilo de las nativas
# Un port de Infinite World (BCM sin modo hiper) se adapta a la mecanica de B3: su
# "aura burst" pasa a ser el modo hiper (LT/L2: Dragon Rush y definitivos) y sus
# especiales/definitivos se ligan, en orden, a sus capsulas de tipo especial/definitiva.
CAP_TEMPLATES = {"especial": 13, "definitiva": 10, "transformacion": 140}
EVERYONE = 0xFFFFFFFFFFF      # objetos comunes: bits de los IDs 0..43


class Capsules:
    """IDs nuevos (596+), registros del catalogo, bancos de nombres y lineas de roster.toml."""

    def __init__(self, usi, cmn=None):
        self.usi = usi
        self.cmn = cmn
        self.recs = capsulas.skc_records(usi.entry(capsulas.SKC_ENTRY))
        self.next_id = len(self.recs)
        self.new = []                 # [(id, registro, nombre)]
        self.next_usi = len(usi.index)
        self.banks = []               # [(fid data_usi, bytes | f(afs, idioma) -> bytes)]
        self._names = {}
        self.hyper = None             # (suelo, aire) del modo hiper convertido de un port IW
        self.desc = {}                # id -> fid data_usi del panel de descripcion

    @staticmethod
    def _bank_ids(b):
        out = {}
        for k, o in enumerate(capsulas.azt_entries(b)):
            if o:
                out[struct.unpack(">I", b[o:o + 4])[0]] = k
        return out

    def _native_name(self, cap, banks):
        for b, ids in banks:
            if cap in ids:
                return capsulas.azt_read(b, ids[cap])
        return None

    def graft_hyper(self, anm, donor, cmn):
        """Injerta en el moveset (forma 1) el bloque del modo hiper del donante en los codigos
        que usa la entrada hiper convertida."""
        dn = IDS[donor]
        dcam = cmn.entry(dn["cam"])
        at = capsulas.ccm_child(dcam)
        src_codes = capsulas.hyper_codes(bytearray(dcam[at[0]:at[0] + at[1]]))
        if not src_codes:
            raise ValueError("el donante no tiene modo hiper")
        damn = cmn.entry(dn["anm"][0])
        _, so, ss = capsulas.csk_child(damn)
        src = damn[so:so + ss]
        k, do, ds = capsulas.csk_child(anm)
        csk = anm[do:do + ds]
        for dst_code, src_code in zip(self.hyper, src_codes):
            csk = capsulas.csk_graft(csk, dst_code, src, src_code)
        return capsulas.amb_rebuild(anm, {k: csk})

    @staticmethod
    def graft_transform(cam, anm_bins, donor, cmn):
        """transformacion = "donante": la entrada P+K+G del donante en el BCM propio (quita la
        de transformarse que traiga el port) y sus animaciones 0x2E0/0x3E0 (con su empujon)
        en cada moveset propio. -> (cam, anm_bins, notas)."""
        dn = IDS[donor]
        rep = []
        if cam is not None:             # sin BCM propio, el del donante ya la tiene
            dcam = cmn.entry(dn["cam"])
            at, dat = capsulas.ccm_child(cam), capsulas.ccm_child(dcam)
            ccm, rep = capsulas.add_b3_transform(cam[at[0]:at[0] + at[1]], dcam[dat[0]:dat[0] + dat[1]])
            cam = capsulas.amb_rebuild(cam, {next(k for k, e in enumerate(capsulas.amb_children(cam))
                                                  if e[0] == at[0] and e[1] == at[1]): ccm})
        damn = cmn.entry(dn["anm"][0])
        _, so, ss = capsulas.csk_child(damn)
        src = damn[so:so + ss]
        out = []
        for anm in anm_bins:
            k, do, ds = capsulas.csk_child(anm)
            csk = anm[do:do + ds]
            for code in capsulas.TRANSFORM_CODES:
                csk = capsulas.csk_graft(csk, code, src, code)
            out.append(capsulas.amb_rebuild(anm, {k: csk}))
        if out:
            rep.append("animaciones %s del donante en %d moveset(s)" % (
                "/".join("%#x" % x for x in capsulas.TRANSFORM_CODES), len(out)))
        return cam, out, rep

    def character(self, mod, cid, donor, c, cam, cmn):
        """-> (lineas de roster.toml, bin de camara (propio o None), notas)."""
        dn = IDS[donor]
        self.hyper = None
        notes, lines = [], []
        own = []
        last_tr = 0
        dforms = dn.get("form_caps", [])
        for cc in c.get("_capsulas", []):
            kind = cc.get("tipo", "especial")
            if kind not in CAP_TEMPLATES:
                notes.append("capsula '%s': tipo desconocido (%s)" % (cc.get("nombre"), kind))
                continue
            nid = self.next_id
            self.next_id += 1
            req = last_tr if kind == "transformacion" else 0
            forma = int(cc.get("forma", 1))
            tpl = CAP_TEMPLATES[kind]
            if kind == "transformacion" and 0 < forma < len(dforms) and 0 < dforms[forma] < len(self.recs):
                tpl = dforms[forma]               # la nativa del donante para esa forma (rareza, ki)
            rec = bytearray(capsulas.make_record(self.recs[tpl], [cid], req))
            ki = None
            if kind == "transformacion":
                rec[20:28] = bytes(8)             # +20 sin RE (Freeza/Cooler 0x0b, Trunks 0x0e...):
                #                                   0 como Goku, Gohan, Vegeta y la plantilla 140
                rec[14] = (1 << forma) - 1        # formas desde las que se puede usar
                if cc.get("ki") is not None:      # barras EXIGIDAS (no se gastan), 0..7
                    rec[15] = 10 * max(0, min(7, int(cc["ki"])))
                ki = (rec[15] + 5) // 10
                last_tr = nid
            else:
                # formas que pueden usarla (1 = normal) y ki que GASTA, como las nativas
                fs = cc.get("formas")
                rec[14] = sum(1 << (int(f) - 1) for f in fs) & 0xFF if fs else 0xFF
                if cc.get("ki") is not None:
                    rec[15] = 10 * max(0, min(7, int(cc["ki"])))
                    ki = int(cc["ki"])
            own.append(dict(id=nid, tipo=kind, nombre=str(cc.get("nombre", "?"))[:40], forma=forma, ki=ki,
                            equipada=bool(cc.get("equipada", True)), reemplaza=cc.get("reemplaza"),
                            textos={k: cc[k] for k in ("quien", "descripcion", "botones", "nota") if cc.get(k)}))
            self.new.append((nid, bytes(rec), own[-1]["nombre"]))
        # moveset: BCM propio (port) o el del donante
        iw = False
        if cam is not None:
            at = capsulas.ccm_child(cam)
            if at:
                ccm = bytearray(cam[at[0]:at[0] + at[1]])
                summ = capsulas.bcm_summary(ccm)
                if summ["aura_iw"] and not summ["hiper"]:
                    iw = True
                    rep = capsulas.adapt_iw_bcm(ccm, [o["id"] for o in own if o["tipo"] == "especial"],
                                                [o["id"] for o in own if o["tipo"] == "definitiva"])
                    notes += ["BCM de Infinite World adaptado: " + r for r in rep]
                    self.hyper = capsulas.hyper_codes(ccm)
                    cam = bytearray(cam)
                    cam[at[0]:at[0] + at[1]] = ccm
                    cam = bytes(cam)
        repl = {int(o["reemplaza"]): o["id"] for o in own if o.get("reemplaza") is not None}
        for o in own:      # su transformacion sustituye a la del donante en esa forma
            if o["tipo"] == "transformacion" and 0 < o["forma"] < len(dforms) and dforms[o["forma"]]:
                repl.setdefault(dforms[o["forma"]], o["id"])
        if repl:
            src = cam if cam is not None else (cmn.entry(dn["cam"]) if dn.get("cam") is not None else None)
            at = capsulas.ccm_child(src) if src is not None else None
            if at:
                ccm = bytearray(src[at[0]:at[0] + at[1]])
                k = capsulas.remap_caps(ccm, repl)
                if k:          # copia propia del moveset solo si cambia algun golpe
                    cam = bytearray(src)
                    cam[at[0]:at[0] + at[1]] = ccm
                    cam = bytes(cam)
                    notes.append("%d golpes ligados a sus capsulas propias" % k)
        # capsulas del donante que tambien son suyas (si usa su moveset)
        inherit = []
        if not iw:
            inherit = [i for i, r in enumerate(self.recs)
                       if capsulas.skc_owner(r) >> donor & 1 and bin(capsulas.skc_owner(r)).count("1") < 40
                       and i not in repl]
        # con moveset propio, los golpes y transformaciones del donante ya no existen: no
        # se heredan sus capsulas (se podian equipar pero no hacian nada)
        dmoves = set()
        if c.get("camara") and dn.get("cam") is not None:
            dsrc = cmn.entry(dn["cam"])
            dat = capsulas.ccm_child(dsrc)
            if dat:
                dccm = dsrc[dat[0]:dat[0] + dat[1]]
                dmoves = {capsulas.w(dccm, o, 8) for o, _ in capsulas.ccm_blocks(dccm)}
                dmoves |= set(dn.get("form_caps", []))
                dmoves -= {0}
                # las que su moveset propio sigue usando (la definitiva del donante que b1port
                # injerta) si se heredan: sin ellas no se podria equipar
                oat = capsulas.ccm_child(cam) if cam is not None else None
                if oat:
                    occm = cam[oat[0]:oat[0] + oat[1]]
                    dmoves -= {capsulas.w(occm, o, 8) for o, _ in capsulas.ccm_blocks(occm)}
                dropped = [i for i in inherit if i in dmoves]
                inherit = [i for i in inherit if i not in dmoves]
                if dropped:
                    notes.append("no hereda las tecnicas del donante %s" % dropped)
        # capsulas de B3 que pide el moveset de un port de B3 (capsulas --importar b3)
        nativas = [int(x) for x in c.get("capsulas_nativas", []) if 0 < int(x) < len(self.recs)]
        inherit += [x for x in nativas if x not in inherit]
        if not own and not iw:
            lines.append("hereda = %s" % toml_list(inherit))
            if nativas:
                dflt = (nativas + [x for x in dn.get("caps", []) if x not in nativas])[:7]
                lines.append("capsulas = %s" % toml_list(dflt))
            if dn.get("skills"):        # su ficha de habilidades (pausa y rotulos en combate)
                lines.append("habilidades_indice = %d" % dn["skills"]["indice"])
            notes.append("capsulas: las del donante (%s)" % dn["name"])
            return lines, cam, notes
        defaults = [o["id"] for o in own if o["equipada"]] + [x for x in nativas]
        if not iw:
            defaults += [repl.get(x, x) for x in dn.get("caps", []) if repl.get(x, x) not in defaults]
        defaults = [x for x in defaults if x not in dmoves or x in {o["id"] for o in own}][:7]
        fcaps = [repl.get(x, x) for x in dn.get("form_caps", [])] or [0]
        if c.get("formas"):
            fcaps = fcaps[:max(1, int(c["formas"]))]
        for o in own:
            if o["tipo"] == "transformacion" and o["forma"] >= 1:
                while len(fcaps) <= o["forma"]:
                    fcaps.append(0)
                fcaps[o["forma"]] = o["id"]
        lines += ["capsulas = %s" % toml_list(defaults), "capsulas_forma = %s" % toml_list(fcaps)]
        if inherit:
            lines.append("hereda = %s" % toml_list(inherit))
        # banco de nombres propio: [base .. id mas alto] con todo lo que puede llevar
        mine = set(inherit) | {o["id"] for o in own}
        for i, r in enumerate(self.recs):
            m = capsulas.skc_owner(r)
            if m >> cid & 1 or (m & EVERYONE) == EVERYONE:
                mine.add(i)
        mine.discard(0)
        base = min(mine)
        own_imgs = {}
        for o in own:
            own_imgs[o["id"]] = capsulas.render_name(o["nombre"])
            self._names[o["id"]] = own_imgs[o["id"]]

        def hud_bank(afs, lang, mine=sorted(mine), bank=dn.get("bank", 0xFFFF), base=base, own_imgs=own_imgs):
            # los rotulos de las capsulas nativas, del data del MISMO idioma (antes, siempre
            # los ingleses de data_usi en todos los idiomas)
            srcb = []
            if bank != 0xFFFF:
                b = afs.entry(bank)
                srcb.append((b, self._bank_ids(b)))
            b = afs.entry(capsulas.NAMES_SHORT)
            srcb.append((b, self._bank_ids(b)))
            items = {}
            for i in mine:
                if i < len(self.recs):
                    img = self._native_name(i, srcb)
                    if img is not None:
                        items[i] = img
            items.update(own_imgs)
            return capsulas.build_bank(base, items)
        fid = self.next_usi
        self.next_usi += 1
        self.banks.append((fid, hud_bank))
        lines.append("hud = %s" % toml_list([fid >> 8, fid & 0xFF]))
        lines += self._skills(dn, own, iw, repl, fcaps, cam, dmoves)
        # panel de descripcion de "Edit Skills" (textos propios o al estilo de las nativas)
        who = str(c.get("nombre") or mod)
        for o in own:
            ki = o["ki"] if o["ki"] is not None else (
                capsulas.KI.get(o["id"]) or {"especial": 1, "definitiva": 4, "transformacion": 3}[o["tipo"]])
            fid = self.next_usi
            self.next_usi += 1
            self.banks.append((fid, lambda afs, lang, o=o, ki=ki: capsulas.build_desc(
                o["nombre"], *capsulas.desc_texts(o["tipo"], o["nombre"], who, ki, o["textos"], lang))))
            self.desc[o["id"]] = fid
        for o in own:
            notes.append("capsula %d: %s (%s%s)" % (o["id"], o["nombre"], o["tipo"],
                                                     " forma %d" % o["forma"] if o["tipo"] == "transformacion" else ""))
        return lines, cam, notes

    def _skills(self, dn, own, iw, repl, fcaps, cam, dmoves=()):
        """Ficha de habilidades propia (data_usi nueva): nombres + condicion y botones,
        copiando del donante lo equivalente (mismo tipo y orden)."""
        sk = dn.get("skills")
        if not sk:
            return []
        dimgs, drows = capsulas.scm_parts(self.usi.entry(sk["scm"]))
        dorder = list(sk["transformaciones"]) + list(sk["ataques"])
        dult = set()
        dcam = cam if cam is not None else (None if dn.get("cam") is None else self.cmn.entry(dn["cam"]))
        if dcam is not None and not iw:
            at = capsulas.ccm_child(dcam)
            if at:
                dult = set(capsulas.bcm_summary(bytearray(dcam[at[0]:at[0] + at[1]]))["definitivos"])
        if iw:
            dcam2 = self.cmn.entry(dn["cam"])
            at = capsulas.ccm_child(dcam2)
            dult = set(capsulas.bcm_summary(bytearray(dcam2[at[0]:at[0] + at[1]]))["definitivos"])
        dkind = {c: ("transformacion" if c in sk["transformaciones"] else
                     "definitiva" if c in dult else "especial") for c in dorder}
        trans = [c for c in fcaps[1:] if c]
        attacks = [o["id"] for o in own if o["tipo"] == "especial"] +                   [o["id"] for o in own if o["tipo"] == "definitiva"]
        if not iw:
            # las del donante solo si su moveset las tiene (con BCM propio, no)
            attacks += [repl.get(c, c) for c in sk["ataques"] if repl.get(c, c) not in attacks and c not in dmoves]
        ownby = {o["id"]: o for o in own}
        used = {"transformacion": 0, "especial": 0, "definitiva": 0}
        plan, rows = [], []           # plan: (capsula, tipo, indice en la ficha del donante)
        for c in trans + attacks:
            kind = ownby[c]["tipo"] if c in ownby else (
                "transformacion" if c in trans else dkind.get(c, "especial"))
            if c in dorder:                       # capsula del donante: tal cual
                src = c
            else:                                 # propia: la equivalente del donante
                cands = [x for x in dorder if dkind[x] == kind]
                src = cands[min(used[kind], len(cands) - 1)] if cands else None
                used[kind] += 1
            i = dorder.index(src) if src in dorder else None
            plan.append((c, kind, i))
            # botones: los de su propio BCM (como las fichas nativas); si no hay ruta, los del donante
            own_rows = capsulas.skill_rows(cam, c) if c in ownby and cam is not None else []
            rows += own_rows
            for r in drows if not own_rows else ():
                rc = struct.unpack(">I", r[:4])[0]
                if src is not None and rc == src:
                    rows.append(struct.pack(">I", c) + r[4:])
        if trans:
            rows = [r for r in drows if r[:4] == bytes([255] * 4)] + rows
        if not plan:                  # ninguna capsula (un port de IW sin especiales): la del donante
            return []

        def scm(afs, lang, plan=plan, rows=rows, scm_entry=sk["scm"]):
            # rotulos de la ficha en el idioma de ese data (los del donante salen de su propio
            # data; los textos de coste de las propias, traducidos)
            dimgs = capsulas.scm_parts(afs.entry(scm_entry))[0]
            blue = (150, 205, 255, 255)
            imgs = []
            for c, kind, i in plan:
                if c in ownby:
                    name = capsulas.render_name(ownby[c]["nombre"], 24)
                elif i is not None and 2 * i < len(dimgs):
                    name = dimgs[2 * i]
                else:   # la ficha del donante tiene menos rotulos que capsulas (Androide 18)
                    name = capsulas.render_name(b3_cap_names().get(c, "?"), 24)
                if c in ownby:                        # propia: su coste, sin notas del donante
                    kk = ownby[c]["ki"] if ownby[c]["ki"] is not None else (
                        capsulas.KI.get(c) or {"especial": 1, "definitiva": 4}.get(kind, 3))
                    txt = capsulas.text("over", lang, kk) if kind == "transformacion" else capsulas.ki_text(kk, lang)
                    cond = capsulas.render_name(txt, 24, blue)
                else:
                    cond = dimgs[2 * i + 1] if i is not None and 2 * i + 1 < len(dimgs) else \
                        capsulas.render_name(capsulas.ki_text(1, lang), 24, blue)
                imgs += [name, cond]
            return capsulas.build_scm(imgs, rows)
        fid = self.next_usi
        self.next_usi += 1
        self.banks.append((fid, scm))
        return ["habilidades = %d" % fid, "habilidades_ataques = %s" % toml_list(attacks),
                "habilidades_transformaciones = %s" % toml_list(trans)]

    def finish(self, out_dir, work, data_langs=()):
        """Bancos de nombres, nombres en los menus y registros del catalogo.
        data_langs: [(nombre del afs, Afs)] de los otros idiomas, que reciben lo mismo."""
        for afs_name, afs in [("data_usi.afs", self.usi)] + list(data_langs):
            lang = capsulas.lang_of(afs_name)
            for fid, b in self.banks:      # bytes, o una funcion (afs, idioma) -> bytes
                write_entry(out_dir, afs_name, fid, b(afs, lang) if callable(b) else b, work)
            if self._names:
                for e in (capsulas.NAMES_SHORT, capsulas.NAMES_LONG):
                    write_entry(out_dir, afs_name, e,
                                capsulas.extend_bank(afs.entry(e), self._names), work)
        out = []
        for nid, rec, nm in self.new:
            out += ["[[capsula]]", "# %s" % nm, "id = %d" % nid, 'registro = "%s"' % rec.hex()]
            if nid in self.desc:
                out.append("descripcion = %d" % self.desc[nid])
            out.append("")
        return out


def forms_config(c, dn, per, donor, name):
    """Formas de un personaje nuevo -> (n de formas, modelo de cada forma, lineas de roster.toml).
      formas = N            no mas que las del donante (el runtime copia sus registros por forma)
      modelo_forma = [..]   modelo DENTRO del traje de cada forma (0 = el 1o). Por defecto, con
                            tantos modelos por traje como formas, cada forma el suyo; si no, el
                            reparto del donante (Gohan adulto: [0, 1, 1, 2], el SSJ2 = modelo SSJ)
      ki_base = [..]        nivel de ki en barras al que tiende cada forma (por defecto el del donante)
      fisica = "donante"    cadenas de fisica (pelo, colas del cinturon) del donante, o = ID de
                            otro personaje; sin la clave, ninguna (el modelo queda rigido)"""
    nforms = max(1, int(dn.get("forms") or 1))
    if c.get("formas"):
        want = max(1, int(c["formas"]))
        if want > nforms:
            log("   %s: aviso: formas = %d pero %s solo tiene %d" % (name, want, dn["name"], nforms))
        nforms = min(want, nforms)
    mforma = [int(x) for x in c.get("modelo_forma", [])][:nforms]
    if not mforma and per == nforms > 1:
        mforma = list(range(nforms))
    if any(not 0 <= m < per for m in mforma):
        log("   %s: aviso: modelo_forma %s fuera de 0..%d (modelos_por_traje)" % (name, mforma, per - 1))
        mforma = [max(0, min(per - 1, m)) for m in mforma]
    lines = ["modelo_forma = %s" % toml_list(mforma)] if mforma else []
    kib = [max(0, min(7, int(x))) for x in c.get("ki_base", [])][:nforms]
    if kib:
        lines.append("ki_base = %s" % toml_list(kib))
    fis = c.get("fisica")
    fid = donor if str(fis).lower() == "donante" else fis if type(fis) is int else None
    if fid is not None and (fid not in IDS or not IDS[fid].get("models")):
        log("   %s: aviso: fisica = %r: personaje sin modelos, sin fisica" % (name, fis))
        fid = None
    if fid is not None:
        lines.append("fisica = %d" % fid)
    return nforms, mforma, lines


def build(a):
    mods = a.mods or default_mods()
    us = a.us or default_us()
    out_dir = os.path.join(mods, OUT_MOD)
    srcs = source_mods(mods)
    trajes = traje_mods(mods)
    if not srcs and not trajes:
        if os.path.isdir(out_dir):
            shutil.rmtree(out_dir)
            log("sin personajes nuevos activos: _roster eliminado")
        else:
            log("sin personajes nuevos activos")
        return 0
    digest = inputs_hash(srcs + [(n, d, None) for n, d, _ in trajes])
    stamp = os.path.join(out_dir, "build.hash")
    if not a.force and os.path.isfile(stamp) and open(stamp).read().strip() == digest:
        log("_roster al dia (%d personajes)" % len(srcs))
        return 0
    work = tempfile.mkdtemp(prefix="roster_")
    try:
        cmn = Afs(os.path.join(us, "data_cmn.afs"), work)
        # EU/PAL no trae data_usi.afs (su ingles es data_eng, misma estructura): se parte de
        # data_eng; lo escrito en data_usi no lo lee el juego EU (inofensivo).
        usi_path = os.path.join(us, "data_usi.afs")
        usi = Afs(usi_path if os.path.isfile(usi_path) else os.path.join(us, "data_eng.afs"), work)
        if os.path.isdir(out_dir):
            shutil.rmtree(out_dir)
        os.makedirs(out_dir)
        next_fid = len(cmn.index)
        sel = bytearray(usi.entry(SELECT_ENTRY))
        texs = azt_textures(sel)
        icon_tex = texs[ICON_TEX]
        icon_img = azt_read(sel, icon_tex)
        icon_orig = icon_img.copy()
        k = icon_tex["hd"][0] / icon_tex["logical"][0]
        icons = Packer(icon_tex, 56, 56, free_regions(icon_tex))
        name_items = []
        free = ALL_IDS
        toml = ["# GENERADO por mod center hd/roster_build.py (no editar: se regenera).", ""]
        manifest = []
        ids = assign_ids(srcs)
        caps = Capsules(usi, cmn)
        own_caps, first_new = {}, caps.next_id
        voice_out = voces.VoiceWriter(us, out_dir)
        iw_voices = None
        b1_cache = {}
        langs = {k: Afs(os.path.join(us, "lang_%s.afs" % k), work) for k in ("usa", "jpn")}
        poses = []
        next_lang = max(len(x.index) for x in langs.values())
        for name, d, c in srcs:
            nombre = c.get("nombre", name)
            cid = ids[name]
            if cid is None:
                log("!! %s: no quedan IDs libres (maximo %d personajes nuevos)" % (name, len(free)))
                continue
            donor = int(c.get("donante", 21))
            dn = IDS[donor]
            # modelos
            files = c.get("modelos", [])
            if not files:
                log("!! %s: sin modelos" % name)
                continue
            per = int(c.get("modelos_por_traje", 1))
            nforms, mforma, form_lines = forms_config(c, dn, per, donor, name)
            model_fids, heights, prefixes, hds = [], [], [], []
            first_hd = None
            hdbs = []
            try:              # un modelo que no se puede leer no tumba el montaje de los demas
                for rel in files:
                    hdbs.append(to_hd(open(os.path.join(d, rel), "rb").read(), work))
            except Exception as e:  # noqa: BLE001
                log("!! %s: modelo %s no valido (%s): personaje omitido" % (name, rel, e))
                continue
            for hdb in hdbs:
                first_hd = first_hd or hdb
                hds.append(hdb)
                labels, height = skeleton(hdb, work)
                prefixes.append(prefix(labels))
                heights.append(height)
                write_entry(out_dir, "data_cmn.afs", next_fid, hdb, work)
                model_fids.append(next_fid)
                next_fid += 1
            # bocas: misma estructura que el donante (trajes x bocas por traje); cada una
            # re-etiquetada con el prefijo de huesos de su forma (RCM_M_JAW -> GRD_M_JAW)
            ncos = max(1, len(files) // per)
            lpc = max(1, dn.get("lips_per_costume", 1))
            own_lips = c.get("bocas", [])
            dper = max(1, dn["models_per_costume"])
            lips_fids, lips_cache = [], {}
            if own_lips:                # bocas propias: una por forma, repetidas por traje
                own = []
                for rel in own_lips:
                    write_entry(out_dir, "data_cmn.afs", next_fid, load_bin(os.path.join(d, rel), work), work)
                    own.append(next_fid)
                    next_fid += 1
                lips_fids = [own[min(kk, len(own) - 1)] for _ in range(ncos) for kk in range(lpc)]
            for cidx in range(0 if own_lips else ncos):
                for kk in range(lpc):
                    mi = cidx * per + min(kk, per - 1)
                    src = dn["lips"][kk]
                    key = (src, prefixes[mi])
                    if key not in lips_cache:
                        lips = cmn.entry(src)
                        dpfx = prefix(skeleton(cmn.entry(dn["models"][min(kk, dper - 1)]), work)[0])
                        if prefixes[mi] and dpfx and len(prefixes[mi]) == len(dpfx):
                            lips = lips.replace((dpfx + "_").encode(), (prefixes[mi] + "_").encode())
                        write_entry(out_dir, "data_cmn.afs", next_fid, lips, work)
                        lips_cache[key] = next_fid
                        next_fid += 1
                    lips_fids.append(lips_cache[key])
            heights = heights[::per]   # altura de la forma base
            # icono, rotulo y retratos (render del modelo, arte del modder o terminados)
            ui = os.path.join(d, "ui")
            tpl_fids = DB["data_cmn_portrait_by_slot"][dn["slot"] if dn["slot"] < 39 else 21]
            tpls = [cmn.entry(tpl_fids[v] & 0xFFFF) for v in (0, 1)]
            try:
                ic, pors, notes = select_images(c, ui, first_hd, portrait_size(tpls[0]), fetch=cmn.entry)
            except Exception as e:  # noqa: BLE001
                log("!! %s: %s" % (name, e))
                continue
            for n_ in notes:
                log("   %s: %s" % (name, n_))
            ix, iy = icons.take()
            px, py = int(round(ix * k)), int(round(iy * k))
            icon_img[py:py + ic.shape[0], px:px + ic.shape[1]] = ic
            nm = np.array(make_name(nombre, 48, 504))
            tpl = NAME_TEX_TEMPLATES[0] if nm.shape[1] <= 256 else NAME_TEX_TEMPLATES[1]
            canvas = np.zeros((64, 256 if tpl == NAME_TEX_TEMPLATES[0] else 512, 4), np.uint8)
            canvas[:nm.shape[0], :nm.shape[1]] = nm
            nw = int(np.ceil(nm.shape[1] / 1.5))
            name_items.append((canvas, (nw, 32), (nm.shape[1], 48), tpl))
            name_slot = len(name_items) - 1
            # retratos P1/P2 (plantilla: los del slot del donante)
            por = []
            for v in (0, 1):
                ready = os.path.join(ui, "retrato_p%d.png" % (v + 1))   # retrato ya hecho (512x512)
                data = (portrait_ready(tpls[v], Image.open(ready)) if pors[v] is None
                        else portrait_bin(tpls[v], pors[v]))
                write_entry(out_dir, "data_cmn.afs", next_fid, data, work)
                por.append(next_fid)
                next_fid += 1
            # cara de la barra de vida: una por forma (render de su modelo o ui/hud*.png)
            hud_tpl = cmn.entry(dn.get("hud_face") or IDS[0]["hud_face"])
            nf = nforms if c.get("formas") else struct.unpack(">I", hud_tpl[0x10:0x14])[0]
            try:
                huds = hud_images(c, ui, [hds[min(len(hds) - 1, mforma[f] if f < len(mforma) else min(f, per - 1))]
                                          for f in range(nf)], fetch=cmn.entry)
                write_entry(out_dir, "data_cmn.afs", next_fid, hud_bin(hud_tpl, huds), work)
                hud_line = ["hud_cara = %d" % next_fid]
                next_fid += 1
            except Exception as e:  # noqa: BLE001
                log("   %s: sin cara de la barra de vida (%s)" % (name, e))
                hud_line = []
            # moveset / camara / tecnicas propios (si no, los del donante)
            extra = list(hud_line)
            # capsulas: propias ([[capsula]]), las del donante o las de un port de IW
            cam = load_bin(os.path.join(d, c["camara"]), work) if c.get("camara") else None
            n_new = len(caps.new)
            cap_lines, cam, notes = caps.character(name, cid, donor, c, cam, cmn)
            own_caps[cid] = {x[0] for x in caps.new[n_new:]}
            # moveset por forma; el mismo fichero en varias formas se escribe una vez (con uno
            # solo, las demas formas usan el de la forma 1, como el SSJ de Gohan adulto)
            anm_rels = list(c.get("moveset", []))
            uniq = list(dict.fromkeys(anm_rels))
            anm_bins = [load_bin(os.path.join(d, rel), work) for rel in uniq]
            if caps.hyper and anm_bins:
                try:
                    anm_bins = [caps.graft_hyper(b, donor, cmn) for b in anm_bins]   # todas las formas
                    notes.append("modo hiper: animacion y efecto de Budokai 3 (los de %s)" % dn["name"])
                except Exception as e:  # noqa: BLE001
                    notes.append("aviso: sin injerto del modo hiper (%s)" % e)
            # tecnicas tras combo (P,P,P,P y E...) como las del donante: los ports no las traen
            if cam is not None and c.get("combos_tecnica", True):
                try:
                    dcam = cmn.entry(dn["cam"])
                    at, dat = capsulas.ccm_child(cam), capsulas.ccm_child(dcam)
                    ccm, rep = capsulas.add_combo_specials(cam[at[0]:at[0] + at[1]], dcam[dat[0]:dat[0] + dat[1]])
                    if rep:
                        cam = capsulas.amb_rebuild(cam, {next(k for k, e in enumerate(capsulas.amb_children(cam))
                                                              if e[0] == at[0] and e[1] == at[1]): ccm})
                        notes += rep
                except Exception as e:  # noqa: BLE001
                    notes.append("aviso: sin tecnicas tras combo (%s)" % e)
            # con formas pero sin P+K+G en su moveset (ports IW de Goku GT, Janemba...) no podria
            # transformarse: se pone la del donante, como con transformacion = "donante"
            auto_tr = nforms > 1 and cam is not None and not capsulas.has_transform(cam)
            if auto_tr:
                notes.append("%d formas sin P+K+G en su moveset: transformacion del donante" % nforms)
            if str(c.get("transformacion", "")).lower() == "donante" or auto_tr:
                try:
                    cam, anm_bins, rep = caps.graft_transform(cam, anm_bins, donor, cmn)
                    notes += ["transformacion del donante: " + r for r in rep]
                except Exception as e:  # noqa: BLE001
                    notes.append("aviso: sin transformacion del donante (%s)" % e)
            # definitiva con animaciones propias sobre la cinematica del donante (cinematica.py)
            if c.get("definitiva_animaciones") and anm_bins:
                try:
                    import cinematica  # noqa: PLC0415
                    with open(os.path.join(d, c["definitiva_animaciones"]), encoding="utf-8") as fh:
                        receta = json.load(fh)
                    done = [cinematica.aplicar(b, receta) for b in anm_bins]
                    anm_bins = [b for b, _ in done]
                    notes.append("definitiva con animaciones propias: %d codigos de la cinematica" % len(done[0][1]))
                except Exception as e:  # noqa: BLE001
                    notes.append("aviso: definitiva con las animaciones del donante (%s)" % e)
            # todos (ports de IW incluidos) deben poder entrar en modo hiper y lanzar su definitiva
            try:
                fcam = cam if cam is not None else cmn.entry(dn["cam"])
                at = capsulas.ccm_child(fcam)
                for p in capsulas.hyper_check(fcam[at[0]:at[0] + at[1]], anm_bins or [cmn.entry(dn["anm"][0])]):
                    notes.append(("" if p.startswith("sin definitiva") else "aviso: ") + p)
            except Exception as e:  # noqa: BLE001
                notes.append("aviso: modo hiper sin comprobar (%s)" % e)
            for n_ in notes:
                log("   %s: %s" % (name, n_))
            anm = []
            # pose del select desde su moveset; pose_select = false la deja en la del donante
            # (el reposo de Gohan del Futuro, port de Shin Budokai, cerraba el juego en el select)
            if anm_bins and c.get("pose_select", True):
                poses.append((cid, anm_bins[0]))
            fid_of = {}
            for rel, ab in zip(uniq, anm_bins):
                write_entry(out_dir, "data_cmn.afs", next_fid, ab, work)
                fid_of[rel] = next_fid
                next_fid += 1
            anm = [fid_of[rel] for rel in anm_rels]
            if not anm and c.get("altura", True):
                # moveset del donante: la cadera de sus animaciones esta hecha para sus piernas
                # (Guldo con las de Recoome flotaba); copia con la cadera ajustada al modelo
                try:
                    anm, next_fid, first = donor_moveset_fixed(cmn, dn, first_hd, out_dir, next_fid, work, name)
                    if first:
                        poses.append((cid, first))
                except Exception as e:  # noqa: BLE001
                    log("   %s: aviso: altura del donante sin ajustar (%s)" % (name, e))
                    anm = []
            if anm:
                extra.append("anm = %s" % toml_list(anm))
            if c.get("formas"):
                extra.append("formas = %d" % nforms)
            extra += form_lines
            # efectos de tecnicas propios y/o color del ki (tono sobre los del donante)
            try:
                bsp = load_bin(os.path.join(d, c["tecnicas"]), work) if c.get("tecnicas") else None
            except Exception as e:  # noqa: BLE001 (un BSP ilegible no tumba el montaje de todos)
                log("   %s: aviso: tecnicas propias ilegibles (%s): las del donante" % (name, e))
                bsp = None
            if c.get("ki_color") is not None:
                try:
                    bsp, cnt = colores.retint(bsp if bsp else cmn.entry(dn["bsp"]),
                                              colores.tono(c["ki_color"]))
                    log("   %s: color del ki %s (%d texturas, %d particulas)" % (
                        name, c["ki_color"], cnt["texturas"], cnt["particulas"]))
                except Exception as e:  # noqa: BLE001
                    log("   %s: aviso: color del ki sin cambiar (%s)" % (name, e))
            if bsp is not None:
                write_entry(out_dir, "data_cmn.afs", next_fid, bsp, work)
                extra.append("tecnicas = %d" % next_fid)
                next_fid += 1
            # aura: la de otro personaje (aura_de) y/o de otro color (aura_color)
            try:
                aura_fid = colores.aura_fid(c["aura_de"]) if c.get("aura_de") is not None else dn["aura"]
                if c.get("aura_de") is not None:
                    log("   %s: aura de %s" % (name, c["aura_de"]))
            except Exception as e:  # noqa: BLE001
                log("   %s: aviso: aura del donante (%s)" % (name, e))
                aura_fid = dn["aura"]
            if c.get("aura_color") is not None:
                try:
                    aura, cnt = colores.retint(cmn.entry(aura_fid), colores.tono(c["aura_color"]))
                    write_entry(out_dir, "data_cmn.afs", next_fid, aura, work)
                    extra.append("aura = %d" % next_fid)
                    next_fid += 1
                    log("   %s: color del aura %s (%d texturas)" % (name, c["aura_color"], cnt["texturas"]))
                except Exception as e:  # noqa: BLE001
                    log("   %s: aviso: color del aura sin cambiar (%s)" % (name, e))
                    if aura_fid != dn["aura"]:
                        extra.append("aura = %d" % aura_fid)
            elif aura_fid != dn["aura"]:
                extra.append("aura = %d" % aura_fid)
            if cam is not None:
                write_entry(out_dir, "data_cmn.afs", next_fid, cam, work)
                extra.append("cam = %d" % next_fid)
                next_fid += 1
            extra += cap_lines
            # voces de combate: las de su juego (port), las de otro personaje o las del donante
            try:
                spec = c.get("voces", "donante")
                if isinstance(spec, str) and spec.lower().startswith("iw:") and iw_voices is None:
                    iw_voices = voces.IwVoices(getattr(a, "iw", None))
                v_lines, v_note = voces.resolve(spec, voice_out, iw_voices, donor=donor)
                extra += v_lines
                log("   %s: %s" % (name, v_note))
            except Exception as e:  # noqa: BLE001
                log("   %s: aviso: voces del donante (%s)" % (name, e))
            # gritos de los golpes: banco propio (lang_usa / lang_jpn) hecho sobre el del donante
            try:
                gspec = c.get("gritos")
                if gspec is None:
                    vs = c.get("voces", "donante")
                    gspec = vs if isinstance(vs, str) and vs.lower().startswith(("iw:", "sb1:", "sb2:")) else "donante"
                gl = gspec.strip().lower()
                if gl not in ("donante", "", "auto"):
                    dl = dn["lang"][0]           # char372 +0x24: banco de gritos
                    for lk, afs in langs.items():
                        entry = afs.entry(dl)
                        bank = gritos.amb_children(entry)[0]
                        lens = gritos.bank_lengths(entry[bank[0]:bank[0] + bank[1]])
                        if gl in ("ninguno", "ninguna", "no"):
                            sounds = [gritos.np.zeros(max(n, 1), gritos.np.int16) for n in lens]
                        elif gl.startswith("iw:"):
                            if iw_voices is None:
                                iw_voices = voces.IwVoices(getattr(a, "iw", None))
                            sounds = gritos.assign(lens, gritos.iw_pool(iw_voices, gspec[3:].strip(), lk))
                        elif gl.startswith("b1:"):
                            # banco de Budokai 1 (55 sonidos, mismo orden que los huecos)
                            if b1_cache.get(gl) is None:
                                # copia dentro del mod (moveset/gritos_b1.npz): asi el mod funciona
                                # en equipos sin la ISO de Budokai 1 (paquete de personajes)
                                saved = os.path.join(d, "moveset", "gritos_b1.npz")
                                try:
                                    import b1port  # noqa: PLC0415
                                    b1 = b1port.B1()
                                    rec = b1.record(int(gl[3:]))
                                    bank_b1 = gritos.scei_bank(b1.entry(rec["snd"][1]), b1.entry(rec["snd"][0]))
                                    b1_cache[gl] = [gritos.resample(x, r) for x, r in bank_b1]
                                    if not os.path.exists(saved):
                                        os.makedirs(os.path.dirname(saved), exist_ok=True)
                                        gritos.np.savez_compressed(saved, *b1_cache[gl])
                                except Exception:  # noqa: BLE001
                                    if not os.path.exists(saved):
                                        raise
                                    with gritos.np.load(saved) as z:
                                        b1_cache[gl] = [z["arr_%d" % i] for i in range(len(z.files))]
                            sounds = gritos.fit(lens, b1_cache[gl])
                        elif gl.startswith(("sb1:", "sb2:")):
                            import sb_voces  # noqa: PLC0415  (Shin Budokai: tonos PPHD -> huecos)
                            sounds = sb_voces.gritos_sounds(gspec, lens, lk, entry[bank[0]:bank[0] + bank[1]])
                        else:
                            raise ValueError("gritos desconocidos: %r" % gspec)
                        write_entry(out_dir, "lang_%s.afs" % lk, next_lang, gritos.build_lang(entry, sounds), work)
                    extra.append("idioma = %s" % toml_list([next_lang, dn["lang"][1]]))
                    log("   %s: gritos propios (%s, %d huecos)" % (name, gspec, len(lens)))
                    next_lang += 1
            except Exception as e:  # noqa: BLE001
                log("   %s: aviso: gritos del donante (%s)" % (name, e))
            # encuadre del select: escala segun la altura (ajuste sobre los 38 nativos)
            h = max(heights)
            scale = min(1.24, max(0.88, 1.4228 - 0.0201 * h))
            s = list(dn["select"])
            s[3] = s[10] = round(scale, 3)
            toml += ["[[personaje]]", "# %s (mod %s)" % (nombre, name), "id = %d" % cid, "donante = %d" % donor,
                     'nombre = "%s"' % "".join(ch for ch in nombre.upper() if 32 <= ord(ch) < 127 and ch != '"'),
                     "modelos = %s" % toml_list(model_fids), "bocas = %s" % toml_list(lips_fids),
                     "modelos_por_traje = %d" % per, "celda = true",
                     "tras_casilla = %d" % next(sl for sl in (IDS[int(c.get("despues_de", donor))]["slot"],
                                                              dn["slot"], 0) if sl < 38),
                     "icono = %s" % toml_list([ICON_TEX, ix, iy, ix + 56, iy + 56]),
                     "rotulo = %s" % toml_list(["@%d" % name_slot, 0, 0, nw, 32]),
                     "retrato = %s" % toml_list(por),
                     "select = %s" % toml_list([float(x) if i % 7 < 4 else int(x) for i, x in enumerate(s)])]
            toml += extra + [""]
            manifest.append("%s: id %d, donante %d (%s), modelos %s, bocas %s, retratos %s, altura %.1f -> escala %.2f"
                            % (nombre, cid, donor, dn["name"], model_fids, sorted(set(lips_fids)), por, h, scale))
            log("+ " + manifest[-1])
        next_fid = build_trajes(trajes, cmn, out_dir, next_fid, work, toml, manifest)
        azt_write(sel, icon_tex, icon_img)
        # plantilla que cuadre en ESTE #AZT (con data_usi es la fija; con data_eng de base, EU, otra)
        sel, name_idx = azt_append(sel, [(im, lg, hd, azt_template(sel, tp, im.shape[1], im.shape[0]))
                                         for im, lg, hd, tp in name_items])
        for i, t in enumerate(name_idx):   # textura real de cada rotulo
            toml = [ln.replace("[@%d," % i, "[%d," % t) if ln.startswith("rotulo") else ln for ln in toml]
        write_entry(out_dir, "data_usi.afs", SELECT_ENTRY, bytes(sel), work)
        data_langs = []
        drawn = (icon_img != icon_orig).any(-1)
        for lk in DATA_LANGS:
            path = os.path.join(us, "data_%s.afs" % lk)
            if not os.path.isfile(path):
                continue
            la = Afs(path, work)
            s2 = bytearray(la.entry(SELECT_ENTRY))
            t2 = azt_textures(s2)
            if len(t2) != len(texs) or t2[ICON_TEX]["size"] != icon_tex["size"]:
                log("aviso: data_%s.afs: texturas del select distintas, idioma sin casillas nuevas" % lk)
                continue
            img2 = azt_read(s2, t2[ICON_TEX])
            img2[drawn] = icon_img[drawn]
            azt_write(s2, t2[ICON_TEX], img2)
            items2 = [(im, lg, hd, azt_template(s2, tp, im.shape[1], im.shape[0]))
                      for im, lg, hd, tp in name_items]
            if any(it[3] is None for it in items2):
                log("aviso: data_%s.afs: sin plantilla para los rotulos, idioma sin casillas nuevas" % lk)
                continue
            s2, idx2 = azt_append(s2, items2)
            if idx2 != name_idx:
                log("aviso: data_%s.afs: rotulos en otras texturas, idioma sin casillas nuevas" % lk)
                continue
            write_entry(out_dir, "data_%s.afs" % lk, SELECT_ENTRY, bytes(s2), work)
            data_langs.append(("data_%s.afs" % lk, la))
        log("idiomas del select: usi + %s" % " ".join(n[5:8] for n, _ in data_langs))
        try:
            acm, csk = select_poses(cmn, poses)
            if acm:
                write_entry(out_dir, "data_cmn.afs", SELECT_POSES[0], acm, work)
                write_entry(out_dir, "data_cmn.afs", SELECT_POSES[1], csk, work)
                log("poses del select propias: IDs %s" % [c for c, _ in poses])
        except Exception as e:  # noqa: BLE001
            log("aviso: poses del select de los donantes (%s)" % e)
        toml += caps.finish(out_dir, work, data_langs)
        open(os.path.join(out_dir, "roster.toml"), "w", encoding="utf-8").write("\n".join(toml))
        open(os.path.join(out_dir, "manifest.txt"), "w", encoding="utf-8").write(
            "Personajes nuevos (generado por roster_build.py)\n" + "\n".join(manifest) + "\n")
        open(stamp, "w").write(digest)
        clean_custom_lists(mods, first_new, own_caps)
        log("_roster listo: %d personajes, data_cmn %d..%d" % (len(manifest), len(cmn.index), next_fid - 1))
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


def clean_custom_lists(mods, first_new, own_caps):
    """mods/capsulas_custom.txt (listas Custom que guarda el juego): si un personaje nuevo cambio de
    capsulas (reimportado, tecnicas que ahora evolucionan...), su lista vieja apunta a IDs nuevos
    que ya son de otro o no existen -> se quita su linea (vuelve a su lista Normal), con copia."""
    path = os.path.join(mods, "capsulas_custom.txt")
    if not os.path.isfile(path):
        return []
    lines = open(path, encoding="utf-8").read().splitlines()
    keep, gone = [], []
    for ln in lines:
        head, _, rest = ln.partition(":")
        try:
            cid, ids = int(head), [int(x) for x in rest.split()]
        except ValueError:
            keep.append(ln)
            continue
        if cid in own_caps and any(first_new <= x < 0xFFFF and x not in own_caps[cid] for x in ids):
            gone.append(cid)
        else:
            keep.append(ln)
    if gone:
        shutil.copyfile(path, path + time.strftime(".%Y%m%d_%H%M%S.bak"))
        open(path, "w", encoding="utf-8").write("\n".join(keep) + "\n")
        log("capsulas Custom de los IDs %s: sus capsulas cambiaron, vuelven a la lista Normal "
            "(copia: capsulas_custom.txt.*.bak)" % gone)
    return gone


# ---------------------------------------------------------------- nuevo
def new_char(a):
    mods = a.mods or default_mods()
    d = os.path.join(mods, a.mod)
    os.makedirs(os.path.join(d, "modelos"), exist_ok=True)
    os.makedirs(os.path.join(d, "ui"), exist_ok=True)
    rels = []
    for i, m in enumerate(a.modelo):
        rel = "modelos/traje%d%s" % (i + 1, os.path.splitext(m)[1].lower() or ".bin")
        shutil.copyfile(m, os.path.join(d, rel))
        rels.append(rel)
    if a.captura:
        im = Image.open(a.captura).convert("RGBA")
        w, h = im.size[0] // 3, im.size[1] // 2
        crop_frac(im.crop((w, 0, 2 * w, h)), 0.5, 0.47, 0.98).save(os.path.join(d, "ui", "cara.png"))
        crop_frac(im.crop((0, 0, w, h)), 0.40, 0.42, 0.84, aspect=288 / 352).save(os.path.join(d, "ui", "retrato.png"))
    extra = []
    if a.cara:              # arte propio de la cara -> icono con el fondo y aro oficiales
        Image.open(a.cara).convert("RGBA").save(os.path.join(d, "ui", "cara.png"))
        extra.append('icono_fuente = "imagen"')
    if a.icono:             # icono terminado (se usa tal cual)
        Image.open(a.icono).convert("RGBA").save(os.path.join(d, "ui", "icono.png"))
        extra.append('icono_fuente = "terminado"')
    if a.retrato:           # arte propio para los retratos (se encaja y se tine P1/P2)
        Image.open(a.retrato).convert("RGBA").save(os.path.join(d, "ui", "retrato.png"))
        extra.append('retrato_fuente = "imagen"')
    if a.retrato_p1 and a.retrato_p2:   # retratos terminados (512x512)
        for v, f in ((1, a.retrato_p1), (2, a.retrato_p2)):
            Image.open(f).convert("RGBA").save(os.path.join(d, "ui", "retrato_p%d.png" % v))
        extra.append('retrato_fuente = "terminado"')
    lines = ["[personaje]", 'nombre = "%s"' % a.nombre.replace('"', ""), "donante = %d" % a.donante,
             "modelos = [%s]" % ", ".join('"%s"' % r for r in rels)]
    if a.id is not None and a.id in ALL_IDS:
        lines.insert(2, "id = %d" % a.id)
    if a.por_traje > 1:
        lines.append("modelos_por_traje = %d" % a.por_traje)
    if a.despues_de is not None:
        lines.append("despues_de = %d" % a.despues_de)
    lines += list(dict.fromkeys(extra))
    open(os.path.join(d, "personaje.toml"), "w", encoding="utf-8").write("\n".join(lines) + "\n")
    log("mod fuente creado: %s" % d)
    try:                    # vista previa inicial (icono, rotulo y retratos)
        preview(argparse.Namespace(mods=mods, us=a.us, mod=a.mod, icono_fuente=None, retrato_fuente=None,
                                   icono_ajuste=None, retrato_ajuste=None, id=None, nombre=None,
                                   despues_de=None, guardar=False, solo=None, cara=None, icono=None,
                                   retrato=None))
    except Exception as e:  # noqa: BLE001
        log("aviso: sin vista previa (%s)" % e)
    return 0


# ---------------------------------------------------------------- capsulas (editor)
CAP_KEYS = ("nombre", "tipo", "forma", "formas", "ki", "equipada", "reemplaza", "quien", "descripcion", "botones", "nota")


def read_caps(path):
    with open(path, "rb") as fh:
        return [dict(c) for c in tomllib.load(fh).get("capsula", [])]


def write_caps(path, caps):
    """Reescribe las secciones [[capsula]] de personaje.toml (el resto se conserva; la version
    anterior queda en <mod>/respaldo/: los comentarios de esas secciones no se reescriben)."""
    text = open(path, encoding="utf-8").read()
    bk = os.path.join(os.path.dirname(path), "respaldo")
    os.makedirs(bk, exist_ok=True)
    with open(os.path.join(bk, "personaje.toml.%s" % time.strftime("%Y%m%d_%H%M%S")), "w", encoding="utf-8") as fh:
        fh.write(text)
    lines = text.splitlines()
    out, skip = [], False
    for ln in lines:
        s = ln.strip()
        if s.startswith("["):
            skip = s == "[[capsula]]"
        if not skip:
            out.append(ln)
    while out and not out[-1].strip():
        out.pop()
    for c in caps:
        out += ["", "[[capsula]]"]
        for k in CAP_KEYS:
            v = c.get(k)
            if v is None or (k == "equipada" and v is True) or (
                    k == "forma" and c.get("tipo") != "transformacion"):
                continue
            if isinstance(v, (list, tuple)):
                out.append("%s = [%s]" % (k, ", ".join(str(int(x)) for x in v)))
            elif isinstance(v, bool):
                out.append("%s = %s" % (k, "true" if v else "false"))
            elif isinstance(v, int):
                out.append("%s = %d" % (k, v))
            else:      # cadena TOML (las descripciones llevan saltos de linea)
                out.append("%s = %s" % (k, json.dumps(str(v), ensure_ascii=False)))
    open(path, "w", encoding="utf-8").write(chr(10).join(out) + chr(10))


# ---------------------------------------------------------------- capsulas (importar)
# Un port trae el moveset (BCM) de su juego: sus golpes piden capsulas de ESE juego. El
# importador las lee del BCM, deduce el tipo (especial / definitiva / transformacion; en B1/B2
# tambien por el catalogo #SKA) y el nombre (lista del juego: la de Infinite World por
# personaje, la de B3 o una propia "ID: Nombre") y las escribe como [[capsula]] con
# `reemplaza` = ID original (el build cambia esos IDs del BCM por los nuevos). Las de un port
# de B3 son capsulas que ya existen: van en `capsulas_nativas` (se las queda tal cual).
RESOURCES = os.path.join(ROOT, "modding resources")
NAME_LISTS = {"iw": "Dragon Ball Z Infinite World Capsule List.xlsx", "b3": "Budokai_3_Capsules_IDs.txt",
              "b1": os.path.join(HERE, "b1_capsulas.txt")}       # nombres oficiales de B1 (va con el kit)


def b1_cap_name(cap, default="?"):
    """Nombre oficial de una capsula de Budokai 1 (b1_capsulas.txt, "ID: nombre  # notas")."""
    try:
        by_id, _ = read_name_list(NAME_LISTS["b1"])
    except OSError:
        return default
    return (by_id.get(cap) or default).replace("(?)", "").strip()
SKA_FILES = {"b1": os.path.join("Budokai 1 and Budokai 2 Capsule Data", "B1 Capsules"),
             "b2": os.path.join("Budokai 1 and Budokai 2 Capsule Data", "B2 Capsules")}
GAMES = ("auto", "iw", "b1", "b2", "b3")


def _xlsx_rows(path):
    """Filas (dict columna -> texto) de la 1a hoja de un .xlsx (sin dependencias)."""
    import html  # noqa: PLC0415
    import re  # noqa: PLC0415
    import zipfile  # noqa: PLC0415
    z = zipfile.ZipFile(path)
    strs = []
    if "xl/sharedStrings.xml" in z.namelist():
        ss = z.read("xl/sharedStrings.xml").decode("utf-8")
        strs = [html.unescape("".join(re.findall(r"<t[^>]*>([^<]*)</t>", si)))
                for si in re.findall(r"<si>(.*?)</si>", ss, re.S)]
    x = z.read("xl/worksheets/sheet1.xml").decode("utf-8")
    rows = []
    for r in re.findall(r"<row[^>]*>(.*?)</row>", x, re.S):
        row = {}
        for m in re.finditer(r'<c r="([A-Z]+)\d+"([^>]*?)(?:/>|>(.*?)</c>)', r, re.S):
            col, attrs, body = m.groups()
            v = None
            if body:
                mv = re.search(r"<v>(.*?)</v>", body)
                v = mv.group(1) if mv else None
                if 't="s"' in attrs and v is not None:
                    v = strs[int(v)]
                mi = re.search(r"<is>.*?<t[^>]*>(.*?)</t>", body, re.S)
                if mi:
                    v = html.unescape(mi.group(1))
            row[col] = v
        rows.append(row)
    return rows


def _clean(t):
    return "".join(ch if 32 <= ord(ch) < 127 else "'" for ch in str(t)).strip()[:40]


def b3_cap_names():
    """{id: nombre} de la lista de capsulas de B3 de "modding resources" (o {} si no esta)."""
    if not hasattr(b3_cap_names, "cache"):
        p = os.path.join(RESOURCES, NAME_LISTS["b3"])
        try:
            b3_cap_names.cache = read_name_list(p)[0] if os.path.isfile(p) else {}
        except Exception:  # noqa: BLE001
            b3_cap_names.cache = {}
    return b3_cap_names.cache


def read_name_list(path):
    """-> ({id: nombre}, {personaje: [(tipo, nombre)]}). Admite la hoja de IW (.xlsx con
    columnas ID / NAME / TYPE / USER), la lista de B3 ("0A 00: Nombre - (Red)", ID en hex
    little-endian) y listas propias: "123: Nombre", "123<tab>Nombre" o "123,Nombre"."""
    import re  # noqa: PLC0415
    by_id, by_user = {}, {}
    if path.lower().endswith(".xlsx"):
        rows = _xlsx_rows(path)
        head = {v: k for k, v in rows[0].items() if v} if rows else {}
        ci, cn, ct, cu = (head.get(k) for k in ("ID", "NAME", "TYPE", "USER"))
        for r in rows[1:]:
            try:
                i = int(str(r.get(ci)).replace(",", ""))
            except ValueError:
                continue
            nm = _clean(r.get(cn) or "")
            if not nm:
                continue
            by_id.setdefault(i, nm)
            kind = {"Super": "especial", "Ultimate": "definitiva"}.get(r.get(ct))
            user = _clean(r.get(cu) or "").lower()
            if kind and user:
                lst = by_user.setdefault(user, [])
                if (kind, nm) not in lst:
                    lst.append((kind, nm))
        return by_id, by_user
    user = ""
    for ln in open(path, encoding="utf-8", errors="replace"):
        ln = ln.strip()
        m = re.match(r"^([0-9A-Fa-f]{2}) ([0-9A-Fa-f]{2}):\s*(.+?)(?:\s+-\s+\(.*\))?$", ln)
        if m:
            i = int(m.group(2) + m.group(1), 16)
        else:
            m = re.match(r"^(\d+)\s*[:\t,;]\s*(.+)$", ln)
            if not m:
                if ln and not ln.startswith("-") and ":" not in ln:
                    user = _clean(ln).lower()
                continue
            i = int(m.group(1))
        nm = _clean(m.groups()[-1])
        by_id.setdefault(i, nm)
        if user:
            by_user.setdefault(user, []).append(("especial", nm))
    return by_id, by_user


def ska_classes(path):
    """Catalogo #SKA de B1/B2 (PS2, LE, registros de 28 B): {id: clase}."""
    b = open(path, "rb").read()
    if b[:4] != b"#SKA":
        raise ValueError("%s no es un catalogo #SKA" % path)
    n, st = struct.unpack("<II", b[0x10:0x18])
    size = 28 if (len(b) - st) // max(n, 1) < 40 else 40
    cls_at = 4 if size == 28 else 8
    return {i: b[st + size * i + cls_at] for i in range(n) if st + size * (i + 1) <= len(b)}


def bcm_capsules(ccm, iw):
    """Capsulas que piden los golpes del BCM, en orden: [(id, tipo)]. En IW el definitivo
    no lleva capsula (id 0)."""
    out = []
    for o, st in capsulas.ccm_blocks(ccm):
        c1, cap = capsulas.w(ccm, o, capsulas.COND), capsulas.w(ccm, o, 8)
        if iw:
            kind = ("definitiva" if c1 & 0x0008 and c1 & 0x8000 else
                    "especial" if c1 & 0x8000 and cap else
                    "transformacion" if c1 & 0x0004 and cap else None)
        else:
            kind = ("definitiva" if c1 & 0x0008 and cap else
                    "transformacion" if c1 & 0x0004 and cap else
                    "especial" if c1 & 0x0002 and cap else None)
        if not kind or (cap, kind) in out:
            continue
        if kind == "definitiva" and not cap and any(k == "definitiva" for _, k in out):
            continue
        out.append((cap, kind))
    return out


def import_caps(a, path, c, d):
    """capsulas --importar: [[capsula]] desde el moveset del port. Devuelve la lista nueva o
    None (y explica por que)."""
    if not c.get("camara"):
        log("!! este personaje usa el moveset del donante: ya lleva sus capsulas (no hay nada que importar)")
        return None
    work = tempfile.mkdtemp(prefix="caps_")
    try:
        cam = load_bin(os.path.join(d, c["camara"]), work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    at = capsulas.ccm_child(cam)
    if not at:
        log("!! %s no tiene #CCM (moveset)" % c["camara"])
        return None
    ccm = bytearray(cam[at[0]:at[0] + at[1]])
    summ = capsulas.bcm_summary(ccm)
    iw = summ["aura_iw"] and not summ["hiper"]
    game = a.importar if a.importar != "auto" else ("iw" if iw else "b3")
    refs = bcm_capsules(ccm, game == "iw")
    rank = {"especial": 0, "definitiva": 1, "transformacion": 2}
    refs.sort(key=lambda r: rank[r[1]])
    if not refs:
        log("el moveset no pide ninguna capsula")
        return []
    if game == "b3":
        nat = sorted({cap for cap, _ in refs if 0 < cap < capsulas.N_NATIVE})
        log("port de Budokai 3: %d capsulas del juego (se quedan tal cual): %s" % (len(nat), nat))
        set_toml_keys(path, {"capsulas_nativas": toml_list(nat)})
        return read_caps(path)
    lst = a.lista or (os.path.join(RESOURCES, NAME_LISTS[game]) if game in NAME_LISTS else None)
    by_id, by_user = {}, {}
    if lst and os.path.exists(lst):
        by_id, by_user = read_name_list(lst)
        log("nombres: %s" % os.path.basename(lst))
    elif a.lista:
        log("!! no existe la lista %s" % a.lista)
    classes = {}
    ska = a.catalogo or (os.path.join(RESOURCES, SKA_FILES[game]) if game in SKA_FILES else None)
    if ska and os.path.exists(ska):
        classes = ska_classes(ska)
    # nombres de IW por personaje (los IDs del BCM de IW no son los de la lista)
    who = _clean(c.get("nombre", "")).lower()
    per_user = {k: [n for t, n in by_user.get(who, []) if t == k] for k in ("especial", "definitiva")}
    used = {"especial": 0, "definitiva": 0, "transformacion": 0}
    caps = []
    for cap, kind in refs:
        if classes.get(cap) == 0x11:
            kind = "transformacion"
        if game == "b1" and kind == "definitiva" and 0 < cap < capsulas.N_NATIVE:
            log("  definitiva del donante (capsula %d del juego, se hereda)" % cap)
            continue                  # B1 no tiene la de B3: b1port injerta la del donante
        k = used[kind]
        used[kind] += 1
        nm = by_id.get(cap) if game != "iw" else None
        if not nm and per_user.get(kind) and k < len(per_user[kind]):
            nm = per_user[kind][k]
        if not nm:
            nm = {"especial": "Special %d", "definitiva": "Ultimate %d",
                  "transformacion": "Transformation %d"}[kind] % (k + 1)
        e = {"nombre": nm, "tipo": kind}
        if kind == "transformacion":
            e["forma"] = k + 1
        if game != "iw" and cap:
            e["reemplaza"] = cap
        caps.append(e)
        log("  %s %d -> %s (%s)" % (game.upper(), cap, nm, kind))
    return caps


def caps_cmd(a):
    """Lista / anade / quita / cambia las capsulas propias de un personaje (personaje.toml)."""
    mods = a.mods or default_mods()
    path = os.path.join(mods, a.mod, "personaje.toml")
    caps = read_caps(path)
    changed = False
    if a.importar:
        with open(path, "rb") as fh:
            c = tomllib.load(fh).get("personaje", {})
        new = import_caps(a, path, c, os.path.join(mods, a.mod))
        if new is None:
            return 1
        if new != caps:
            if caps:              # copia de seguridad de las que habia (nunca se pierden)
                shutil.copy2(path, path + ".antes_de_importar")
            caps = new
            changed = True
    if a.anadir:
        name, kind = a.anadir[0][:40], a.anadir[1]
        if kind not in CAP_TEMPLATES:
            log("!! tipo desconocido: %s (especial, definitiva o transformacion)" % kind)
            return 1
        c = {"nombre": name, "tipo": kind}
        if kind == "transformacion":
            c["forma"] = int(a.anadir[2]) if len(a.anadir) > 2 else 1
        caps.append(c)
        changed = True
    if a.quitar is not None and 0 <= a.quitar < len(caps):
        caps.pop(a.quitar)
        changed = True
    if a.renombrar:
        i = int(a.renombrar[0])
        if 0 <= i < len(caps):
            caps[i]["nombre"] = a.renombrar[1][:40]
            changed = True
    if a.ki:                  # barras exigidas por una transformacion (0..7)
        i = int(a.ki[0])
        if 0 <= i < len(caps):
            caps[i]["ki"] = max(0, min(7, int(a.ki[1])))
            changed = True
    if a.subir is not None and 0 < a.subir < len(caps):
        caps[a.subir - 1], caps[a.subir] = caps[a.subir], caps[a.subir - 1]
        changed = True
    if changed:
        write_caps(path, caps)
        log("capsulas de %s: %d" % (a.mod, len(caps)))
    if a.json:
        print(json.dumps(caps, ensure_ascii=False))
    else:
        for i, c in enumerate(caps):
            log("%d. %s (%s%s)" % (i, c.get("nombre"), c.get("tipo"),
                                   " forma %s%s" % (c.get("forma", 1), ", ki %s" % c["ki"] if "ki" in c else "")
                                   if c.get("tipo") == "transformacion" else ""))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("construir")
    b.add_argument("--mods")
    b.add_argument("--us", help="carpeta con data_cmn.afs y data_usi.afs (us/)")
    b.add_argument("--force", action="store_true")
    b.add_argument("--iw", help="carpeta de Infinite World (para sus voces); por defecto ps2_games/")
    n = sub.add_parser("nuevo")
    n.add_argument("--mods")
    n.add_argument("--us", help="carpeta con data_cmn.afs (para la vista previa)")
    n.add_argument("--mod", required=True)
    n.add_argument("--nombre", required=True)
    n.add_argument("--donante", type=int, default=21)
    n.add_argument("--id", type=int)
    n.add_argument("--modelo", action="append", required=True)
    n.add_argument("--cara", help="arte de la cara para el icono (mejor PNG con fondo transparente)")
    n.add_argument("--icono", help="icono terminado (se usa tal cual)")
    n.add_argument("--retrato", help="arte para los retratos P1/P2")
    n.add_argument("--retrato-p1", help="retrato P1 terminado (512x512)")
    n.add_argument("--retrato-p2", help="retrato P2 terminado (512x512)")
    n.add_argument("--captura", help="collage 3x2 de capturas (reserva si el modelo no se puede renderizar)")
    n.add_argument("--despues-de", type=int, help="ID tras el que aparece su casilla en la rueda")
    n.add_argument("--por-traje", type=int, default=1, help="modelos (formas) por traje, en orden traje1: forma1, forma2...")
    e = sub.add_parser("estado", help="plazas libres / ocupadas")
    e.add_argument("--mods")
    e.add_argument("--json", action="store_true")
    v = sub.add_parser("vista", help="vista previa de icono, rotulo y retratos de un mod fuente")
    v.add_argument("--mods")
    v.add_argument("--us")
    v.add_argument("--mod", required=True)
    v.add_argument("--icono-fuente", choices=("modelo", "imagen", "terminado"))
    v.add_argument("--retrato-fuente", choices=("modelo", "imagen", "terminado"))
    v.add_argument("--imagenes", choices=("originales", "modelo"),
                   help="atajo para ports con imagenes propias (p.ej. Infinite World): conservar las "
                        "originales o generar icono y retratos desde el modelo 3D como el resto")
    v.add_argument("--icono-ajuste", help="zoom,dx,dy,giro")
    v.add_argument("--retrato-ajuste", help="zoom,dx,dy,giro")
    v.add_argument("--id", type=int, help="plaza pedida (22-26, 31 o 44-63; -1 = automatica)")
    v.add_argument("--nombre")
    v.add_argument("--despues-de", type=int, help="ID tras el que va la casilla (-1 = el donante)")
    v.add_argument("--guardar", action="store_true", help="escribe los cambios en personaje.toml")
    v.add_argument("--solo", choices=("icono", "retrato"), help="regenera solo esa imagen (mas rapido)")
    v.add_argument("--cara", help="importa arte de la cara (icono_fuente = imagen)")
    v.add_argument("--icono", help="importa un icono terminado (icono_fuente = terminado)")
    v.add_argument("--retrato", help="importa arte para los retratos (retrato_fuente = imagen)")
    k = sub.add_parser("capsulas", help="capsulas propias de un personaje: listar / anadir / quitar")
    k.add_argument("--mods")
    k.add_argument("--mod", required=True)
    k.add_argument("--anadir", nargs="+", metavar=("NOMBRE", "TIPO"),
                   help="NOMBRE TIPO [FORMA]: especial | definitiva | transformacion")
    k.add_argument("--quitar", type=int, help="indice (0 = la primera)")
    k.add_argument("--renombrar", nargs=2, metavar=("INDICE", "NOMBRE"))
    k.add_argument("--subir", type=int, help="sube una posicion la capsula INDICE (cambia su orden)")
    k.add_argument("--ki", nargs=2, metavar=("INDICE", "BARRAS"),
                   help="transformacion: barras de ki EXIGIDAS (no se gastan), 0..7")
    k.add_argument("--json", action="store_true")
    k.add_argument("--importar", nargs="?", const="auto", choices=GAMES,
                   help="crea las capsulas desde el moveset del port (juego: auto, iw, b1, b2, b3)")
    k.add_argument("--lista", help="nombres de las capsulas: .xlsx de IW, lista de B3 o 'ID: Nombre'")
    k.add_argument("--catalogo", help="catalogo #SKA del juego de origen (B1/B2: tipo de cada capsula)")
    a = ap.parse_args()
    return {"construir": build, "nuevo": new_char, "estado": status, "vista": preview,
            "capsulas": caps_cmd}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
