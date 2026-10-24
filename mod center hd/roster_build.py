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
    aura_formas = [0, 3, 4, 0]     # opcional: tipo de aura de cada forma (0..5; Gohan adulto:
                                   # 0 normal, 3 SSJ, 4 SSJ2 con rayos). Sin la clave, la del donante
    bocas = ["modelos/bocas.bin"]  # opcional: bocas propias (si no, las del donante re-etiquetadas)
    icono_fuente = "modelo"        # modelo (render cel-shading del 1er modelo, por defecto) |
                                   # imagen (ui/cara.png con fondo y aro oficiales) |
                                   # terminado (ui/icono.png tal cual)
    retrato_fuente = "modelo"      # modelo | imagen (ui/retrato.png) | terminado (ui/retrato_p1/p2.png)
    icono_ajuste = [1.0, 0, 0, 12]     # zoom, dx, dy (fraccion), giro (grados)
    retrato_ajuste = [1.0, 0, 0, 50]
    hud_ajuste = [1.0, 0, 0, 0]    # cara de la barra de vida (render del modelo, una por forma;
    hud_centrado_ps2 = true        # centra la cara en 64/128 px de PS2; false conserva el encuadre 360 antiguo
                                   # o ui/hud.png / ui/hud_1.png... terminadas de 256x128)
    capsulas_nativas = [6, 7]      # opcional: capsulas de B3 que pide el moveset de un port de B3
    voces = "iw:JANENBA"           # opcional: voces de combate (iw:NOMBRE | b3:ID | donante | ninguna)
    gritos = "iw:JANENBA"          # opcional ("wav:gritos" = los .wav de esa carpeta del mod): gritos de los golpes (por defecto, la fuente de `voces`;
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
                             # SSJ3, SSJ4); si faltan, se repite el ultimo. "juego:418" = el
                             # modelo del juego tal cual (forma sin modelo propio en el traje)
    (importar.py traje <mod> --personaje ID lo crea desde un port importado)

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
from hornada_support import TreePublish, retire_tree, validate_roster
import hashlib
import io
import json
import re
import os
import shutil
import struct
import sys
import tempfile
import threading
import time
import tomllib
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))


def toml_file(path):
    """TOML de un mod: tolera el BOM del Bloc de notas (tomllib lo rechaza); un archivo que no es
    UTF-8 da TOMLDecodeError como cualquier TOML roto (los except ya lo tratan)."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data.startswith(b"\xef\xbb\xbf"):
        data = data[3:]
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise tomllib.TOMLDecodeError("no es UTF-8 (%s)" % exc, "", 0) from exc
    return tomllib.loads(text)
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
TOURN_ENTRY = 2047             # data_usi: texturas del Torneo (#AZT): tex 14-21 = tiras de 5 iconos 280x56, 28-64 = rotulos x32
TOURN_STRIP_TEX = 14           # plantilla de las tiras de iconos (DDS 512x128 A8R8G8B8)
# El juego lee data_usi (ingles USA) o, con otro idioma, data_eng/spn/fra/ger/ita: las
# cinco tienen la misma estructura (2709 entradas, mismas texturas del select y catalogo).
# Todo lo que se escribe en data_usi se repite en ellas (sin esto, en espanol las casillas
# nuevas salian sin icono y el juego se cerraba al pasar por ellas).
DATA_LANGS = ("eng", "spn", "fra", "ger", "ita")
SELECT_POSES = (3881, 3882)    # data_cmn: poses del select (#ACM, una por ID; #CSK codigo = ID)
ICON_TEX = 11                  # textura 1024x1024 del marco de la rueda (45 % libre)
FONT = "C:/Windows/Fonts/comicbd.ttf"
PORTRAIT_SCALE = 1.3


def work_dir(prefix):
    """Carpeta de trabajo temporal: fuera de C: si existe alguna de DBZ3_TMP, E:/DBZ3HD_trabajo/tmp o D:/tmp
    (C: va lleno; antes quedaban carpetas importar_* de cientos de MB en %TEMP%) y se borra al salir del proceso."""
    import atexit  # noqa: PLC0415
    base = next((c for c in (os.environ.get("DBZ3_TMP"), "E:/DBZ3HD_trabajo/tmp", "D:/tmp")
                 if c and os.path.isdir(c)), None)
    d = tempfile.mkdtemp(prefix=prefix, dir=base)
    atexit.register(shutil.rmtree, d, True)
    return d


AVISOS = []   # (carpeta, texto) de cada "!! carpeta: texto": el launcher los ensena en ese mod


def log(*a):
    txt = " ".join(str(x) for x in a)
    if txt.startswith("!! ") and ": " in txt:
        AVISOS.append(tuple(txt[3:].split(": ", 1)))
    print(*a, flush=True)


def progress(k, n, what):
    """Linea que el launcher convierte en barra de carga ("Preparando personajes")."""
    log("@progreso %d/%d %s" % (k, n, what))


def default_mods():
    for p in (os.path.join(ROOT, "mods"), os.path.join(ROOT, "out", "build", "win-amd64-release", "mods")):
        if os.path.isdir(p):
            return p
    return os.path.join(ROOT, "mods")


def default_us():
    return os.path.dirname(swap_b3.DEFAULT_AFS)


def us_dir(us=None):
    """La carpeta us/ del Budokai 3 HD: `us` si su data_cmn.afs es el bueno; si no, la que encuentre
    swap_b3.find_us (carpeta del launcher, DBZ3/us...). Sin ninguna: swap_b3.NoGameData (mensaje claro)."""
    return os.path.dirname(swap_b3.b3_afs(os.path.join(us or default_us(), "data_cmn.afs")))


# ---------------------------------------------------------------- AFS / LZX
class Afs:
    def __init__(self, path, work):
        # todos los lectores pasan por aqui: un data_cmn.afs de otro juego (IW, B1, PS2) o cortado se
        # cambia por el bueno o da un error claro (antes: IndexError en la vista previa)
        self.path, self.work = swap_b3.b3_afs(path), work
        self.index = swap_b3.read_afs_index(self.path)

    def raw(self, n):
        if not 0 <= n < len(self.index):
            raise IndexError("%s: no tiene la entrada %d (%d entradas)" % (self.path, n, len(self.index)))
        a, s = self.index[n]
        with open(self.path, "rb") as f:
            f.seek(a)
            return f.read(s)

    def entry(self, n):
        return unlzx(self.raw(n), self.work)


# ------------------------------------------------ cache de contenido (LZX / modelos)
# Todo lo pesado y reproducible del montaje (descomprimir LZX, comprimir LZX, convertir
# PS2->HD) se recuerda por CONTENIDO (sha256 de lo que entra): una reconstruccion que no
# cambia un bin no vuelve a llamar a xbcompress/xbdecompress ni a convertir. La clave lleva
# la firma de la herramienta que produce el dato (si cambia, la cache vieja no se usa) y la
# carpeta la pone construir (<juego>/mods_cache, junto a la de retratos).
CACHE_DIR = None
_MEM = {}
_MEM_BYTES = [0]
_MEM_MAX = 384 * 1024 * 1024        # tope de la cache en memoria (lo demas, en disco)
_PRUNE = [0]
_TAGS = {}


def _file_tag(path):
    """sha256 corto de `path`: la firma de la herramienta que produce el dato cacheado."""
    tag = _TAGS.get(path)
    if tag is None:
        try:
            with open(path, "rb") as fh:
                tag = hashlib.sha256(fh.read()).hexdigest()[:16]
        except OSError:
            tag = "sin-fichero"
        _TAGS[path] = tag
    return tag


def _content_key(prefix, data):
    return "%s-%s" % (prefix, hashlib.sha256(data).hexdigest())


def _mem_store(folder, key, data):
    if len(data) > 8 * 1024 * 1024 or _MEM_BYTES[0] > _MEM_MAX:
        return
    if (folder, key) not in _MEM:
        _MEM[(folder, key)] = data
        _MEM_BYTES[0] += len(data)


def _prune(folder, cap_mb):
    """Poda la cache en disco (si paso del tope) por fecha: el dato se regenera si vuelve a hacer falta."""
    _PRUNE[0] += 1
    if _PRUNE[0] % 64 or not CACHE_DIR:
        return
    d = os.path.join(CACHE_DIR, folder)
    try:
        files = sorted(((e.stat().st_mtime, e.path, e.stat().st_size) for e in os.scandir(d)),
                       key=lambda x: x[0])
    except OSError:
        return
    total = sum(s for _, _, s in files)
    cap = cap_mb * 1024 * 1024
    for _, path, size in files:
        if total <= cap * 8 // 10:
            break
        try:
            os.remove(path)
            total -= size
        except OSError:
            pass


def _cached(folder, key, make, cap_mb=512):
    """Valor de `make()` recordado por contenido: memoria y (si construir dio carpeta) disco."""
    hit = _MEM.get((folder, key))
    if hit is not None:
        return hit
    path = os.path.join(CACHE_DIR, folder, key) if CACHE_DIR else None
    if path:
        try:
            with open(path, "rb") as fh:
                data = fh.read()
            _mem_store(folder, key, data)
            return data
        except OSError:
            pass
    data = make()
    _mem_store(folder, key, data)
    if path:
        try:
            d = path.rsplit(os.sep, 1)[0]
            os.makedirs(d, exist_ok=True)
            tmp = "%s.%d.%d.tmp" % (path, os.getpid(), threading.get_ident())
            with open(tmp, "wb") as fh:
                fh.write(data)
            os.replace(tmp, path)
        except OSError:
            pass
        _prune(folder, cap_mb)
    return data


def unlzx(b, work):
    if b[:4] != LZX:
        return bytes(b)
    return _cached("lzx_d", _content_key(_file_tag(swap_b3.XBDECOMPRESS), b),
                   lambda: _unlzx_raw(b, work))


def _unlzx_raw(b, work):
    src, dst = os.path.join(work, "in.lzx"), os.path.join(work, "out.bin")
    open(src, "wb").write(b)
    swap_b3.lzx_decompress(src, dst, work)
    return open(dst, "rb").read()


# Compresion LZX en paralelo durante "construir" (antes, un xbcompress tras otro: el montaje
# tardaba ~10 min). Fuera de construir, write_entry sigue siendo sincrono.
_POOL = None
_PENDING = {}
_SEQ = [0]


def _compress(raw, dst, work):
    with open(raw, "rb") as fh:
        data = fh.read()

    def comprimir():
        swap_b3.lzx_compress(raw, dst, work)
        with open(dst, "rb") as fh:
            return fh.read()

    blob = _cached("lzx_c", _content_key(_file_tag(swap_b3.XBCOMPRESS), data), comprimir, cap_mb=256)
    with open(dst, "wb") as fh:
        fh.write(blob)
    os.remove(raw)


def write_entry(mod_dir, afs, n, data, work):
    d = os.path.join(mod_dir, "us", afs, str(n))
    os.makedirs(d, exist_ok=True)
    dst = os.path.join(d, "geom.bin")
    prev = _PENDING.pop(dst, None)
    if prev is not None:
        prev.result()                  # la misma entrada otra vez: primero la anterior
    _SEQ[0] += 1
    raw = os.path.join(work, "raw_%d.bin" % _SEQ[0])
    open(raw, "wb").write(data)
    if _POOL is None:
        _compress(raw, dst, work)
    else:
        _PENDING[dst] = _POOL.submit(_compress, raw, dst, work)


def flush_entries():
    """Espera a que terminen las compresiones pendientes (y relanza su error si lo hubo)."""
    while _PENDING:
        _, f = _PENDING.popitem()
        f.result()


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


_COMMON_FX = None


def common_fx(cmn, min_chars=10):
    """Efectos de banco (AP7 clase 4) que usan >= min_chars personajes nativos: los comunes,
    cargados siempre (0, 4, 5, 16, 300...). Los demas van por grupos (352 Cell/Cooler/Freeza)."""
    global _COMMON_FX
    if _COMMON_FX is None:
        who = {}
        for d in IDS.values():
            for f in d.get("anm", []):
                if not 0 <= f < len(cmn.index):
                    continue
                b = cmn.entry(f)
                cc = capsulas.csk_child(b)
                if cc:
                    for v in capsulas.csk_fx_values(b[cc[1]:cc[1] + cc[2]]):
                        who.setdefault(v, set()).add(d["id"])
        _COMMON_FX = {v for v, ids in who.items() if len(ids) >= min_chars}
    return _COMMON_FX


# ---------------------------------------------------------------- altura
def forms_height_fixed(raws, per, anm, cmn, dn, name):
    """Moveset propio hecho para el modelo de la forma 1 y una forma mucho mas grande o
    pequena (Goku GT nino -> SSJ4 adulto se hundia en el suelo): su cadera en bind sube o
    baja para que el tobillo en reposo quede como el de un cuerpo de su talla (el del donante
    si tiene sus piernas; si no, el de la forma 1 a escala). Modelos PS2. -> raws."""
    import altura  # noqa: PLC0415
    idle = altura.any_tracks(anm)
    if not idle:
        return raws
    sk0 = [altura.skeleton(raws[c0]) for c0 in range(0, len(raws), per)]
    dsk = altura.skeleton(cmn.entry(dn["models"][0])) if dn.get("models") else None
    d_leg = altura.leg(dsk) if dsk else None
    d_ank = altura.ankle(dsk, altura.any_tracks(cmn.entry(dn["anm"][0]))) if d_leg else None
    out = list(raws)
    for i, raw in enumerate(raws):
        if i % per == 0:
            continue
        base = sk0[i // per]
        sk = altura.skeleton(raw)
        l0, l1 = altura.leg(base), altura.leg(sk)
        if not (l0 and l1) or abs(l1 / l0 - 1) < 0.1:
            continue
        now = altura.ankle(sk, idle)
        target = d_ank if d_leg and abs(l1 / d_leg - 1) < 0.03 else altura.ankle(base, idle) * l1 / l0
        dy = target - now
        fixed = altura.lift_waist(raw, dy) if abs(dy) > 0.15 else None
        if fixed:
            out[i] = fixed
            log("   %s: altura: modelo %d (pierna %.2f, la forma 1 %.2f) cadera %+.2f" % (name, i + 1, l1, l0, dy))
    return out


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


def donor_cam_scaled(cmn, dn, model_hd, out_dir, next_fid, work, name):
    """solo_modelos (#36): la camara #ACC del donante esta encuadrada para sus piernas; con el modelo nuevo
    (moveset del donante) la definitiva -Big Bang de Vegeta- pierde al luchador. Se escala igual que la
    cadera: x/z * s e y * s + dy en las pistas ojo/objetivo, sin tocar roll, fov ni los cuadros.
    -> (fid, next_fid) o (None, next_fid) si no hace falta / falla."""
    if not dn.get("cam") or not dn.get("models") or not dn.get("anm") or dn["anm"][0] >= 0xFFFFFFFF:
        return None, next_fid
    try:
        import altura  # noqa: PLC0415
        s, dy = altura.correccion(altura.skeleton(model_hd), altura.skeleton(cmn.entry(dn["models"][0])),
                                  altura.any_tracks(cmn.entry(dn["anm"][0])))
        if abs(s - 1.0) < 0.03 and abs(dy) < 0.15:
            return None, next_fid
        sys.path.insert(0, os.path.join(HERE, "studio"))
        import studio_core  # noqa: PLC0415
        cam = studio_core.CamBin(cmn.entry(dn["cam"]))
        n = 0
        for clip in cam.clips:
            for t in studio_core.VEC:
                for k in clip.keys.get(t, []):
                    if len(k) >= 4:
                        k[1] = k[1] * s
                        k[2] = k[2] * s + dy
                        k[3] = k[3] * s
            n += 1
        write_entry(out_dir, "data_cmn.afs", next_fid, cam.build(), work)
        log("   %s: camara del donante escalada x%.3f %+.2f (%d clips; #36)" % (name, s, dy, n))
        return next_fid, next_fid + 1
    except Exception as e:  # noqa: BLE001
        log("   %s: aviso: camara del donante sin escalar (%s)" % (name, e))
        return None, next_fid


# ---------------------------------------------------------------- modelos
def _amo_hd(b):
    """#AMB PS2 -> #AMB HD (ps2hd), recordado por contenido (la conversion es lo lento
    de un port: un bin que no cambia no se vuelve a convertir)."""
    import ps2hd  # noqa: PLC0415
    tag = _file_tag(ps2hd.__file__) + _file_tag(os.path.join(ROOT, "awo_tools", "amo2awo.py"))
    return _cached("hd", _content_key(tag, b), lambda: bytes(ps2hd.convert_block(b)), cap_mb=512)


def to_hd(b, work):
    """Bin del modelo -> #AMB HD descomprimido (acepta LZX, #AMB HD o #AMB PS2)."""
    b = unlzx(b, work)
    if b[:4] != b"#AMB":
        raise ValueError("no es un #AMB (%r)" % b[:4])
    if struct.unpack("<I", b[0xC:0x10])[0] == 3:      # PS2 (little-endian, version 3)
        return _amo_hd(b)
    if struct.unpack(">I", b[0xC:0x10])[0] == 2:      # HD
        return bytes(b)
    raise ValueError("version de #AMB desconocida")


def bsp_cst_records(bsp):
    """Offsets de los registros #CST (0xF0 B) de un BSP HD."""
    out = []

    def walk(o):
        mg = bsp[o:o + 4]
        if mg == b"#AMB":
            n, tbl = struct.unpack_from(">II", bsp, o + 0x10)
            for k in range(n):
                off, size = struct.unpack_from(">II", bsp, o + tbl + 16 * k)
                if size:
                    walk(o + off)
        elif mg == b"#CST":
            n = struct.unpack_from(">I", bsp, o + 8)[0]
            out.extend(o + 16 + 0xF0 * r for r in range(n))
    try:
        walk(0)
    except struct.error:
        return []
    return out


CST_BANK_FIELDS = (32, 48, 88, 172, 174)   # tipo (+32) y bancos de los efectos de un #CST; 6 = el de aura de SSJ


def bsp_bank6_fix(bsp, donor_bsp):
    """Ports de IW: registros #CST con efectos del banco 6 (lo usan los nativos de aura Super Saiyan: Goku,
    Vegeta, Gohan...; Yamcha de IW lo pide en sus rafagas 360/361) cuando el donante no lo usa: el juego pedia
    el efecto 12 que no existe y se cerraba al lanzar la tecnica (Yamcha amarillo con capsulas, 2026-10-15).
    Esos registros toman todo (salvo nombre y codigo, bytes 0..20) del primer registro del BSP sin banco 6: con
    solo los efectos el tipo (+32 = 6) seguia pidiendo el banco y el juego se cerraba igual. -> (bsp, n)."""
    def usa6(b, r):
        return any(struct.unpack_from(">h", b, r + o)[0] == 6 for o in CST_BANK_FIELDS)
    if not bsp or any(usa6(donor_bsp, r) for r in bsp_cst_records(donor_bsp)):
        return bsp, 0
    recs = bsp_cst_records(bsp)
    base = next((r for r in recs if not usa6(bsp, r)), None)
    if base is None:
        return bsp, 0
    out, n = bytearray(bsp), 0
    for r in recs:
        if usa6(bsp, r):
            out[r + 20:r + 0xF0] = bsp[base + 20:base + 0xF0]
            n += 1
    return bytes(out), n


def cam_pad_clips(cam, donor_cam):
    """La presentacion del combate (modo 1 del Duelo) pide clips de camara por indice del donante: Vegeta saga
    Saiyan de IW tiene 30 clips y la entrada pedia el 30 (Vegeta tiene 35) -> clip nulo y cierre del juego al
    acabar el vuelo por el escenario (2026-10-15). Los clips que faltan se copian del donante. -> (cam, n)."""
    try:
        sys.path.insert(0, os.path.join(HERE, "studio"))
        import studio_core  # noqa: PLC0415
        mine, theirs = studio_core.CamBin(cam), studio_core.CamBin(donor_cam)
    except Exception:  # noqa: BLE001
        return cam, 0
    n = len(theirs.clips) - len(mine.clips)
    if n <= 0 or mine.e != theirs.e:
        return cam, 0
    mine.clips += theirs.clips[len(mine.clips):]
    return mine.build(), n


IW_CINE = range(0x480, 0x500)
# ports de IW cuyo BSP de tecnicas no prepara los efectos de la definitiva del donante (guion del donante): Baby
# (definitiva de Vegeta: la orden de efecto 13/banco 1 encontraba su objeto sin preparar, +148 = 0, y el juego se
# cerraba en lr 0x820CFF1C; con el BSP de Vegeta la definitiva va entera, R9 2026-10-16)
IW_TECNICAS_DONANTE = {"c:super baby vegeta 2"}      # codigos de agarre/definitiva que piden los guiones del donante


def iw_guion_donante(cam, anm_bins, dcam, danm):
    """Ports de IW: guiones (#SPX) y camaras (#ACC, #ACL) del donante, como en los ports de B1 (R9, 2026-10-16).
    El #SPX de IW usa ordenes que B3 no tiene (filas de su tabla mas largas: 0x35-0x37, 0x86-0x88, 0x99-0x9A...):
    la definitiva de Goku GT cerraba el juego o se quedaba colgada. Con el guion del donante, su definitiva
    (ranura 0: sub(capsula, 0x4A0, ...)) y el agarre (20) piden los codigos 0x480-0x4FF del moveset: los que el de
    IW tiene se quedan con su animacion de IW (Goku GT: los mismos codigos y duraciones que Goku); los que faltan se
    injertan del donante con su animacion. Los golpes con guion de una ranura que el donante no tiene (IW 1 y 50)
    pasan a la 0 (la definitiva). Las animaciones de la victima (2o y 3er #ACM) son las del donante, como sus
    guiones. -> (cam, anm_bins, notas)."""
    def kid(b, tag):
        return next((k for k, e in enumerate(capsulas.amb_children(b)) if e[1] and b[e[0]:e[0] + 4] == tag), None)

    def data(b, k):
        e = capsulas.amb_children(b)[k]
        return b[e[0]:e[0] + e[1]]
    rep = []
    swap = {}
    for tag in (b"#ACC", b"#ACL", b"#SPX"):
        k, dk = kid(cam, tag), kid(dcam, tag)
        if k is not None and dk is not None:
            swap[k] = data(dcam, dk)
    cam = capsulas.amb_rebuild(cam, swap)
    spx = swap.get(kid(cam, b"#SPX"))
    ns = struct.unpack_from("<I", spx, 0x18)[0]
    slots = {i for i, x in enumerate(struct.unpack_from("<%dI" % ns, spx, 0x20)) if x != 0xFFFFFFFF}
    rep.append("guiones y camaras del donante (ranuras %s)" % sorted(slots))
    _, dso, dss = capsulas.csk_child(danm)
    dcsk = danm[dso:dso + dss]
    d_acm = [k for k, e in enumerate(capsulas.amb_children(danm)) if e[2] == 3]
    _, d_anims = acm_read(data(danm, d_acm[0]))
    out = []
    for anm in anm_bins:
        k, so, ss = capsulas.csk_child(anm)
        csk = bytearray(anm[so:so + ss])
        n, lst = struct.unpack_from(">II", csk, 0x10)
        nhr, ho = struct.unpack_from(">II", csk, 0x18)
        moved = set()
        for h in range(nhr):
            for ln in range(8):
                st, sl = struct.unpack_from(">HH", csk, ho + 128 * h + 16 * ln + 4)
                if st == 3 and sl not in slots:
                    struct.pack_into(">H", csk, ho + 128 * h + 16 * ln + 6, 0)
                    moved.add(sl)
        acm_k = [i for i, e in enumerate(capsulas.amb_children(anm)) if e[2] == 3]
        if not acm_k:            # forma que solo cambia golpes (#CSK sin #ACM): usa los de la forma 1
            out.append(capsulas.amb_rebuild(anm, {k: bytes(csk)}))
            continue
        acm = data(anm, acm_k[0])
        n_own = len(acm_read(acm)[1])
        new, grafted = [], []
        for code in IW_CINE:
            so_d = capsulas.csk_block_at(dcsk, code)
            if not so_d or capsulas.csk_block_at(csk, code):
                continue
            a, bank = struct.unpack_from(">HH", dcsk, so_d)
            csk = bytearray(capsulas.csk_graft(bytes(csk), code, dcsk, code))
            if bank == 3 and a < len(d_anims) and d_anims[a][3] is not None:
                struct.pack_into(">H", csk, capsulas.csk_block_at(csk, code), n_own + len(new))
                new.append(d_anims[a])
            grafted.append(code)
        repl = {k: bytes(csk)}
        if new:
            repl[acm_k[0]] = acm_append(acm, new)
        for i, j in zip(acm_k[1:], d_acm[1:]):          # victima (2o y 3er #ACM): los del donante
            repl[i] = data(danm, j)
        out.append(capsulas.amb_rebuild(anm, repl))
        if moved:
            rep.append("golpes con guion de las ranuras %s (que el donante no tiene) -> ranura 0" % sorted(moved))
        if grafted:
            rep.append("%d codigos de agarre/definitiva del donante injertados (%s..%s)" % (
                len(grafted), hex(grafted[0]), hex(grafted[-1])))
    return cam, out, rep


def bsp_resources(bsp):
    """Recursos que el juego apunta al procesar un BSP de tecnicas (sub_82126490): hijos de [0] y de [1] y hijos
    de la raiz desde el 2. La ranura tiene sitio para 64."""
    def kids(o):
        n, tbl = struct.unpack_from(">II", bsp, o + 0x10)
        return [struct.unpack_from(">I", bsp, o + tbl + 16 * k)[0] for k in range(n)]
    try:
        top = kids(0)
        sub = sum(len(kids(top[g])) for g in (0, 1) if bsp[top[g]:top[g] + 4] == b"#AMB")
        return sub + max(0, len(top) - 2)
    except (struct.error, IndexError):
        return 0


def b3_ramps(hdb, name, c=None):
    """Rampas de sombreado de Budokai 2 (sombra en el alfa, RGB negro y ultima columna blanca) -> estilo B3
    (awo_tools/rampas.py). Sin esto la piel salia sin sombra (muy blanca) y con manchas negras (Tenshinhan B2).
    Los modelos nativos, de IW, SB y SDBH no cambian (se detecta por el formato de la rampa)."""
    try:
        import rampas  # noqa: PLC0415
        out, rep = rampas.convertir(hdb)
    except Exception as e:  # noqa: BLE001
        log("   %s: aviso: rampas sin convertir (%s)" % (name, e))
        return hdb
    if "convertidas" in rep:
        log("   %s: %s" % (name, rep))
    out, n_b1 = rampas.desde_b1(out)          # B1: sin rampas, salia blanco entero (Zarbon B1)
    if n_b1:
        log("   %s: modelo de Budokai 1: %d rampas de sombreado nuevas" % (name, n_b1))
        out, n = rampas.sombrear_planas(out)
        if n:
            log("   %s: %d texturas planas pasan a sombreadas" % (name, n))
    if c is not None and c.get("origen_fuente") == "sdbh":
        out, n = rampas.sombrear_planas(out)      # SDBH: texturas sin sombrear (pelo de Goku gris claro)
        if n:
            log("   %s: %d texturas de SDBH pasan a sombreadas (pelo)" % (name, n))
    return out


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


def _dds_rgba(blob):
    """DDS SIN comprimir de 32 bpp con alfa (el formato del select, los retratos y los
    rotulos) -> (alto, ancho, 4) RGBA, EL MISMO resultado byte a byte que Pillow (lo
    comprueba datos/test_igualdad.py sobre las texturas del juego), sin pasar por su
    decodificador (que para 1024x1024 tarda ~1,2 s). None si el DDS no es exactamente ese
    formato (comprimido, mips, cubemap...): entonces lo decodifica Pillow como siempre."""
    if blob[:4] != b"DDS " or len(blob) < 128:
        return None
    size, flags, h, w, pitch, depth, mips = struct.unpack("<7I", blob[4:32])
    pf_flags, fourcc, rgb_bits = struct.unpack("<3I", blob[80:92])
    if (size != 124 or not h or not w or mips not in (0, 1) or fourcc != 0
            or rgb_bits != 32 or pf_flags & 0x41 != 0x41 or pitch not in (0, w * 4)):
        return None
    masks = struct.unpack("<4I", blob[92:108])
    order = {(0xFF0000, 0xFF00, 0xFF, 0xFF000000): (2, 1, 0, 3),
             (0xFF, 0xFF00, 0xFF0000, 0xFF000000): (0, 1, 2, 3)}.get(masks)
    if order is None or struct.unpack("<I", blob[112:116])[0] & 0x200:
        return None
    if len(blob) - 128 < w * h * 4:
        return None
    a = np.frombuffer(blob, np.uint8, count=w * h * 4, offset=128).reshape(h, w, 4)
    return a[..., order] if order != (0, 1, 2, 3) else a.copy()


def azt_read(b, t):
    blob = bytes(b[t["at"]:t["at"] + t["size"]])
    img = _dds_rgba(blob)
    if img is not None:
        return img
    return np.array(Image.open(io.BytesIO(blob)).convert("RGBA"))


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


# ---------------------------------------------------------------- franja de ojos (bandeja Ability de Edit Skills)
# data_usi 2064 (#AZT de 10 texturas): las texturas 2 y 3 son atlas de 20 franjas de ojos de 168x40 (logico; 252x60 HD).
# El juego pide la franja por el indice de sprite de la lista 2063 [tabla u8 0x82373D38[ID]]; los personajes nuevos
# usaban la del donante. Aqui se anaden atlas propios (20 franjas por textura) y roster.toml lleva `ojos = [tex, x0,
# y0, x1, y1]`; el gancho de sub_821B8FC8 (roster_ext.cpp) clona el descriptor del donante con esa textura y rect.
OJOS_ENTRY = 2064
OJOS_POR_TEX = 20
OJOS_LOGICO = (168, 40)
OJOS_HD = (252, 60)


def eye_strip(img):
    """Retrato (PIL o ndarray RGBA) -> franja de ojos RGBA de OJOS_HD (alto x ancho): recorte de la banda de los ojos
    (ojos a ~42% del alto y 60% del ancho, 72% del ancho del retrato) con la proporcion 168:40."""
    im = img if isinstance(img, Image.Image) else Image.fromarray(np.asarray(img))
    im = im.convert("RGBA")
    W, H = im.size
    cw = 0.66 * W
    ch = cw * OJOS_LOGICO[1] / OJOS_LOGICO[0]
    cx, cy = 0.56 * W, 0.44 * H
    box = (int(cx - cw / 2), int(cy - ch / 2), int(cx + cw / 2), int(cy + ch / 2))
    out = im.crop(box).resize(OJOS_HD, Image.LANCZOS)
    a = np.array(out)
    a[..., 3] = 255
    return a


def ojos_toml(toml, tex, n):
    """Cambia los marcadores [@oK] de las lineas `ojos` por [textura, x0, y0, x1, y1] (rect logico); sin textura, las quita."""
    if tex is None:
        return [ln for ln in toml if not ln.startswith("ojos = ")]
    out = []
    for ln in toml:
        for k in range(n if ln.startswith("ojos = ") else 0):
            y = OJOS_LOGICO[1] * (k % OJOS_POR_TEX)
            ln = ln.replace("[@o%d]" % k, "[%d, 0, %d, %d, %d]" % (tex + k // OJOS_POR_TEX, y, OJOS_LOGICO[0], y + OJOS_LOGICO[1]))
        out.append(ln)
    return out


def ojos_append(afs, out_dir, afs_name, strips, work):
    """Anade a la entrada 2064 los atlas con las franjas (20 por textura, plantilla = la textura 2 si cuadra) y la
    escribe. -> indice de la primera textura nueva o None (sin franjas, o 2064 distinta de la esperada)."""
    if not strips:
        return None
    try:
        b = bytearray(afs.entry(OJOS_ENTRY))
        if len(azt_textures(b)) != 10:
            return None
        items = []
        for k in range(0, len(strips), OJOS_POR_TEX):
            canvas = np.zeros((2048, 256, 4), np.uint8)   # textura real de las nativas (256 x 2048); el contenido es 252 x 1200
            for j, st in enumerate(strips[k:k + OJOS_POR_TEX]):
                canvas[OJOS_HD[1] * j:OJOS_HD[1] * (j + 1), :OJOS_HD[0]] = st
            t = azt_template(b, 2, 256, 2048)
            if t is None:
                return None
            items.append((canvas, (OJOS_LOGICO[0], OJOS_LOGICO[1] * OJOS_POR_TEX),
                          (OJOS_HD[0], OJOS_HD[1] * OJOS_POR_TEX), t))
        b, idx = azt_append(b, items)
        write_entry(out_dir, afs_name, OJOS_ENTRY, bytes(b), work)
        return idx[0]
    except Exception as e:  # noqa: BLE001 (sin franja propia: la del donante, como antes)
        log("aviso: franjas de ojos (%s): %s" % (afs_name, e))
        return None


def tornament_append(afs, out_dir, afs_name, icons, name_items, work):
    """Torneo (S5-I1, issue #33): anade al #AZT 2047 las tiras con los iconos de las celdas nuevas (5 por tira, plantilla
    la tex 14) y sus rotulos de nombre, y escribe la entrada. -> (indices de tiras, indices de rotulos) o None."""
    if not icons:
        return None
    try:
        tz = bytearray(afs.entry(TOURN_ENTRY))
        if len(azt_textures(tz)) < 40:
            return None
        items = []
        for st in range((len(icons) + 4) // 5):
            canvas = np.zeros((128, 512, 4), np.uint8)
            for j, ic in enumerate(icons[5 * st:5 * st + 5]):
                canvas[:ic.shape[0], 84 * j:84 * j + ic.shape[1]] = ic
            items.append((canvas, (280, 56), (420, 84), TOURN_STRIP_TEX))
        nst = len(items)
        for im, lg, hd, tp in name_items:
            t = azt_template(tz, tp, im.shape[1], im.shape[0])
            if t is None:
                return None
            items.append((im, lg, hd, t))
        tz, idx = azt_append(tz, items)
        write_entry(out_dir, afs_name, TOURN_ENTRY, bytes(tz), work)
        return (idx[:nst], idx[nst:])
    except Exception as e:  # noqa: BLE001 (sin Torneo propio: el cuadro enseña el icono del anfitrion, como antes)
        log("aviso: Torneo (%s): %s" % (afs_name, e))
        return None


def torneo_indices(ln, tor_idx):
    """Cambia los marcadores [@sN, / [@tN, de las lineas torneo_* por los indices reales de textura."""
    if ln.startswith("torneo_icono"):
        for i, t in enumerate(tor_idx[0]):
            ln = ln.replace("[@s%d," % i, "[%d," % t)
    elif ln.startswith("torneo_rotulo"):
        for i, t in enumerate(tor_idx[1]):
            ln = ln.replace("[@t%d," % i, "[%d," % t)
    return ln


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
#   icono_ajuste / retrato_ajuste = [zoom, dx, dy, giro[, roll]] (dx/dy fraccion; roll en grados)
ICON_ADJ = [1.0, 0.0, 0.0, 3.0, 0.0]      # zoom, dx, dy, giro, inclinacion del plano
PORTRAIT_ADJ = [1.0, 0.0, 0.0, 35.0, 0.0]


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
        for i, x in enumerate(v[:5]):
            base[i] = float(x)
    return base


# aura de cada transformacion (personaje.toml aura_tipos_forma): la fila de tipo de aura (0x82321A38) es la misma
# en todos los personajes para la misma clase de forma (medido: el SSJ y el SSJ2 de Gohan adulto = los de Goku),
# asi que cada nombre es la forma de Goku (ID 0) que se copia; "ninguna" deja la forma sin aura.
AURA_TIPOS = {"base": -3, "ninguna": -1, "normal": 0, "kaioken": 1, "ssj": 2, "ssj2": 3, "ssj3": 4, "ssj4": 5}

CACHE_IMAGENES = None   # carpeta de la cache de iconos/retratos (la pone construir: <juego>/mods_cache/retratos)


def select_images(c, ui, hd_model, portrait_size, want=("icono", "retrato"), fetch=None):
    """select_images con cache en disco: el render de iconos y retratos era casi todo el tiempo de un montaje
    completo (12 min con ~25 personajes) y se repetia en cada actualizacion del Kit aunque nada del personaje
    cambiase. La clave son solo las entradas del render (modelo, imagenes del mod, ajustes, model_render.py)."""
    if not CACHE_IMAGENES:
        return _select_images(c, ui, hd_model, portrait_size, want, fetch)
    import pickle  # noqa: PLC0415
    h = hashlib.sha1(open(os.path.join(HERE, "model_render.py"), "rb").read())
    h.update(bytes(hd_model))
    h.update(json.dumps([image_source(c, ui, "icono"), image_source(c, ui, "retrato"), adjust(c, "icono"),
                         adjust(c, "retrato"), list(portrait_size), list(want), fetch is not None]).encode())
    if os.path.isdir(ui):
        for fn in sorted(os.listdir(ui)):
            p = os.path.join(ui, fn)
            if os.path.isfile(p):
                h.update(fn.encode() + open(p, "rb").read())
    f = os.path.join(CACHE_IMAGENES, h.hexdigest() + ".pkl")
    try:
        with open(f, "rb") as fh:
            return pickle.load(fh)
    except Exception:  # noqa: BLE001  (sin cache o cache rota: se renderiza)
        pass
    r = _select_images(c, ui, hd_model, portrait_size, want, fetch)
    try:
        os.makedirs(CACHE_IMAGENES, exist_ok=True)
        with open(f + ".tmp", "wb") as fh:
            pickle.dump(r, fh)
        os.replace(f + ".tmp", f)
    except OSError:
        pass
    return r


def _select_images(c, ui, hd_model, portrait_size, want=("icono", "retrato"), fetch=None):
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
    z, dx, dy, yaw, roll = adjust(c, "icono")
    icon = None
    if src == "-":
        icon = np.zeros((mr.ICON_PX, mr.ICON_PX, 4), np.uint8)
    if src == "terminado":
        icon = np.array(Image.open(os.path.join(ui, "icono.png")).convert("RGBA").resize(
            (mr.ICON_PX, mr.ICON_PX), Image.LANCZOS))
    if icon is None and src == "modelo":
        try:
            icon = mr.make_icon(get_model(), yaw=yaw, roll=roll, zoom=z, dx=dx, dy=dy)
            # en algunos PCs el render sale vacio sin dar error (casillas con el circulo en blanco en la seleccion):
            # casi sin pixeles opacos = fallido, y se usa la imagen como si el render hubiera fallado
            if float((np.asarray(icon)[..., 3] > 32).mean()) < 0.02:
                icon = None
                notes.append("icono: el render del modelo salio vacio; se usa ui/cara.png")
        except Exception as e:  # noqa: BLE001
            notes.append("icono: el modelo no se pudo renderizar (%s); se usa ui/cara.png" % e)
    if icon is None:
        art = os.path.join(ui, "cara.png")
        if not os.path.exists(art):     # con la causa: antes solo "falta ui/cara.png" y no se sabia por que
            raise RuntimeError("falta ui/cara.png para el icono" + ("" if not notes else " (%s)" % notes[-1]))
        icon = mr.make_icon(None, zoom=z, dx=dx, dy=dy, art=Image.open(art))
    # retratos
    src = image_source(c, ui, "retrato") if "retrato" in want else "-"
    z, dx, dy, yaw, roll = adjust(c, "retrato")
    pors = None
    if src == "-":
        return icon, None, notes
    if src == "terminado":
        pors = [None, None]
    if pors is None and src == "modelo":
        try:
            pors = mr.make_portraits(get_model(), portrait_size, yaw=yaw, roll=roll, zoom=z, dx=dx, dy=dy)
            if pors and all(p is not None and float((np.asarray(p)[..., 3] > 32).mean()) < 0.01 for p in pors):
                pors = None   # render vacio (ver el icono)
                notes.append("retrato: el render del modelo salio vacio; se usa la imagen")
        except Exception as e:  # noqa: BLE001
            notes.append("retrato: el modelo no se pudo renderizar (%s); se usa la imagen" % e)
    if pors is None:
        art = next((os.path.join(ui, f) for f in ("retrato.png", "cara.png")
                    if os.path.exists(os.path.join(ui, f))), None)
        if art is None:
            raise RuntimeError("falta ui/retrato.png para los retratos" + ("" if not notes else " (%s)" % notes[-1]))
        pors = mr.make_portraits(None, portrait_size, zoom=z, dx=dx, dy=dy, art=Image.open(art))
    return icon, pors, notes


def load_bin(path, work):
    """Fichero de datos del personaje -> bin HD (LZX se descomprime; #AMB PS2 se convierte)."""
    b = unlzx(open(path, "rb").read(), work)
    if b[:4] == b"#AMB" and struct.unpack("<I", b[0xC:0x10])[0] == 3:
        b = _amo_hd(b)
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
    blob = bytes(h[do:do + ds])
    tpl = _dds_rgba(blob)
    if tpl is None:
        tpl = np.array(Image.open(io.BytesIO(blob)).convert("RGBA"))
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


def hud_dy(m):
    """Bajada automatica de la cara de la barra de vida (fraccion del alto) para cabezas muy
    altas: las oficiales dejan el hueso HEAD a 12-29 px sobre el menton (camara fija); Fat
    Gotenks (45) con el menton en su sitio dejaba los ojos fuera de la barra y solo se veian
    boca y barbilla. Se baja lo que pase de 28 px (las oficiales y los ports de antes, igual)."""
    import model_render as mr  # noqa: PLC0415
    try:
        pose = m.arms_down()
        pose.update(m.mouth_closed())
        lo, _, hb = mr.head_info(m, pose, core=True)
        up = (m.worlds(pose)[hb][1, 3] - lo[1]) * mr.HUD_PX_PER_UNIT if hb is not None else 0.0
    except Exception:  # noqa: BLE001 (modelo raro: sin ajuste)
        return 0.0
    return float(np.clip(up - 28.0, 0.0, 30.0)) / mr.HUD_SIZE[1]


def hud_images(c, ui, form_models, fetch=None):
    """Caras de la barra de vida, una por forma: ui/hud.png (o hud_1.png, hud_2.png... por
    forma) terminadas de 256x128, o render de cada modelo (hud_ajuste = [zoom, dx, dy, giro]).
    Con cache en disco (como los iconos/retratos): el render se repite en cada montaje."""
    if CACHE_IMAGENES:
        import pickle  # noqa: PLC0415
        h = hashlib.sha1(open(os.path.join(HERE, "model_render.py"), "rb").read())
        h.update(b"".join(form_models))
        h.update(json.dumps([c.get("hud_ajuste"), c.get("hud_centrado_ps2", True), fetch is not None]).encode())
        if os.path.isdir(ui):
            for fn in sorted(os.listdir(ui)):
                p = os.path.join(ui, fn)
                if os.path.isfile(p) and fn.startswith("hud"):
                    h.update(fn.encode() + open(p, "rb").read())
        f = os.path.join(CACHE_IMAGENES, "hud_" + h.hexdigest()[:24] + ".pkl")
        try:
            with open(f, "rb") as fh:
                return pickle.load(fh)
        except Exception:  # noqa: BLE001  (sin cache o cache rota: se renderiza)
            pass
    else:
        f = None
    out = _hud_images(c, ui, form_models, fetch)
    if f:
        try:
            os.makedirs(CACHE_IMAGENES, exist_ok=True)
            with open(f + ".tmp", "wb") as fh:
                pickle.dump(out, fh)
            os.replace(f + ".tmp", f)
        except OSError:
            pass
    return out


def _hud_images(c, ui, form_models, fetch=None):
    import model_render as mr  # noqa: PLC0415
    z, dx, dy, yaw = 1.0, 0.0, 0.0, 0.0
    v = c.get("hud_ajuste")
    if isinstance(v, list):
        z, dx, dy, yaw = ([float(x) for x in v] + [1.0, 0.0, 0.0, 0.0][len(v):])[:4]
    out, cache = [], {}
    center_ps2 = c.get("hud_centrado_ps2", True) is not False
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
            cache[id(hd)] = mr.make_hud(m, yaw=yaw, zoom=z, dx=dx,
                                       dy=dy if isinstance(v, list) else hud_dy(m), center_ps2=center_ps2)
        out.append(cache[id(hd)])
    return out


# ---------------------------------------------------------------- trajes extra
def mod_off(mods_dir, name):
    """Igual que ListMods y el runtime: marcador .disabled, carpeta X.disabled o .carpeta
    (zip a medio instalar, copias internas)."""
    return (name.startswith(".") or name.endswith(".disabled") or
            os.path.exists(os.path.join(mods_dir, name, ".disabled")))


def traje_mods(mods_dir):
    out = []
    for name in sorted(os.listdir(mods_dir)):
        d = os.path.join(mods_dir, name)
        f = os.path.join(d, "traje.toml")
        if name == OUT_MOD or not os.path.isfile(f) or mod_off(mods_dir, name):
            continue
        try:
            cfg = dict(toml_file(f).get("traje", {}))
        except (OSError, ValueError, TypeError) as exc:
            log("!! %s: traje.toml no valido (%s): omitido" % (name, exc))
            continue
        out.append((name, d, cfg))
    return out


def build_trajes(trajes, cmn, out_dir, next_fid, work, toml, manifest, ids=None):
    """Trajes extra: un modelo por forma (en el orden de las formas del personaje). El
    runtime coloca cada uno donde lo busca el juego (incluidas las formas que son otro
    ID, como Goku SSJ4 = ID 91) y suma el traje al select."""
    extra_n = {}
    for name, d, c in trajes:
        mod_char = isinstance(c.get("personaje"), str)    # personaje = "<carpeta del mod>": un port/mod como personaje propio
        if mod_char:
            cid = (ids or {}).get(c["personaje"])
            if cid is None:
                log("!! %s: traje para el personaje de mod %r, que no esta montado: omitido" % (name, c["personaje"]))
                continue
            try:
                pc = dict(toml_file(os.path.join(os.path.dirname(d), c["personaje"], "personaje.toml")).get("personaje", {}))
                dn = {"name": str(pc.get("nombre", c["personaje"])).upper(),
                      "costumes": max(1, len(pc.get("modelos", [])) // max(1, int(pc.get("modelos_por_traje", 1))))}
            except (OSError, ValueError, TypeError) as exc:
                log("!! %s: no leo el personaje.toml de %r (%s): omitido" % (name, c["personaje"], exc))
                continue
        else:
            cid = int(c.get("personaje", -1))
            if cid not in IDS or not IDS[cid].get("models"):
                log("!! %s: traje para el personaje %r, que no es del juego original: omitido "
                    "(los trajes extra solo van sobre personajes del juego o de mods por carpeta)" % (name, cid))
                continue
            dn = IDS[cid]
        files = c.get("modelos", [])
        if not files:
            log("!! %s: sin modelos" % name)
            continue
        # Preparar TODO el traje antes de publicar modelos o referencias TOML.
        preparados, cache = [], {}
        try:
            for rel in files:
                nat = rel if isinstance(rel, int) else (int(rel[6:]) if str(rel).startswith("juego:") else None)
                tono, regs = c.get("tono"), c.get("colores")      # colores = [[traje.colores]]: HSV por region (recolorear.py)
                if nat is not None and tono is None and not regs:
                    preparados.append((None, nat))
                    continue
                key = ((("colores", nat, json.dumps(regs, sort_keys=True)) if regs else ("tono", nat, float(tono)))
                       if nat is not None else ("archivo", str(rel), json.dumps(regs, sort_keys=True)))
                if key not in cache:
                    if nat is not None:
                        import colores  # noqa: PLC0415
                        hd, _ = (colores.recolor_regiones(cmn.entry(nat), regs) if regs
                                 else colores.retint_modelo(cmn.entry(nat), float(tono)))
                    else:
                        with open(os.path.join(d, rel), "rb") as modelo:
                            hd = b3_ramps(to_hd(modelo.read(), work), name)
                        if regs:     # recolor por regiones de un modelo de archivo (port/mod)
                            import colores  # noqa: PLC0415
                            hd, _ = colores.recolor_regiones(hd, regs)
                    cache[key] = hd
                preparados.append((key, None))
        except Exception as exc:  # noqa: BLE001 -- error del mod, no fallo de escritura
            log("!! %s: traje omitido ANTES de publicar referencias (%s)" % (name, exc))
            continue
        fids, publicados = [], {}
        for key, nat in preparados:
            if key is None:
                fids.append(nat)
                continue
            if key not in publicados:
                # Si falla escribir, aborta TreePublish: nunca se publica un roster parcial.
                write_entry(out_dir, "data_cmn.afs", next_fid, cache[key], work)
                publicados[key] = next_fid
                next_fid += 1
            fids.append(publicados[key])
        numero = dn.get("costumes", 1) + extra_n.get(cid, 0)     # numero del traje en el select (0 = el 1.o)
        extra_n[cid] = extra_n.get(cid, 0) + 1
        link = []
        enl = c.get("enlazado")
        if enl:
            # traje de epoca con golpes, capsulas y color propios: al combatir con este traje, el juego carga el
            # personaje del mod enlazado (src/ports_guard.cpp, peticion de la escena 25)
            tid = (ids or {}).get(enl)
            if tid is None:
                log("!! %s: enlazado = %r no es un personaje montado: traje sin enlace" % (name, enl))
            else:
                link = ["traje_enlazado = [%d, %d]" % (numero, tid)]
                log("   %s: traje %d de %s enlazado al personaje %d (%s)" % (name, numero + 1, dn["name"], tid, enl))
        toml += ["[[personaje]]", "# %s: traje extra (mod %s)" % (dn["name"], name), "id = %d" % cid,
                 "traje_formas = %s" % toml_list(fids)] + link + [""]
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
        if name == OUT_MOD or not os.path.isfile(f) or mod_off(mods_dir, name):
            continue
        try:
            doc = toml_file(f)
            cfg = dict(doc.get("personaje", {}))
        except (OSError, ValueError, TypeError) as exc:
            log("!! %s: personaje.toml no valido (%s): omitido" % (name, exc))
            continue
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


# ---------------------------------------------------------------- reparto persistente de IDs
# mods/roster_ids.toml (fuera de _roster, que se regenera): carpeta -> ID de cada personaje nuevo
# y orden de los trajes extra. Reglas (INFORME R7/slots):
#  1. un mod ya apuntado conserva su ID aunque se anadan o quiten otros;
#  2. un `id` pedido en personaje.toml se respeta si nadie activo lo tiene (nunca desplaza a otro);
#  3. un mod nuevo toma el primer ID libre que no este reservado;
#  4. un mod desactivado o quitado RESERVA su ID (vuelve con el mismo). La reserva solo se libera
#     cuando no queda ningun ID libre: primero la de mods cuya carpeta ya no existe, luego la de
#     desactivados (con aviso). Sin archivo (primera vez) se toma el reparto del _roster actual.
IDS_FILE = "roster_ids.toml"
AVISOS_FILE = "roster_avisos.txt"
MAX_CELDAS = 24        # casillas nuevas de la rueda (src/roster_ext.h kMaxVirtualCells = 64 - 40)


def ids_del_roster(mods_dir):
    """Migracion: carpeta -> ID del _roster ya montado (lineas '# Nombre (mod X)' + 'id = N')."""
    out, mod = {}, None
    try:
        lines = open(os.path.join(mods_dir, OUT_MOD, "roster.toml"), encoding="utf-8").read().splitlines()
    except OSError:
        return out
    for ln in lines:
        if ln.startswith("# ") and ln.endswith(")") and "(mod " in ln and "traje extra" not in ln:
            mod = ln[ln.rindex("(mod ") + 5:-1]
        elif mod and ln.startswith("id = "):
            out.setdefault(mod, int(ln[5:]))
            mod = None
    return out


def plan_ids(mods_dir, srcs):
    """(ids {carpeta activa: ID o None}, mapa nuevo {carpeta: ID} en orden de llegada, orden de trajes)."""
    path = os.path.join(mods_dir, IDS_FILE)
    doc = None
    if os.path.isfile(path):
        try:
            doc = toml_file(path)
        except (OSError, ValueError) as exc:
            log("aviso: %s no valido (%s): se rehace desde el _roster actual" % (IDS_FILE, exc))
    if doc is None:
        mapa = ids_del_roster(mods_dir)
        trajes = {}
    else:
        mapa = {str(k): v for k, v in doc.get("ids", {}).items() if isinstance(v, int)}
        trajes = {str(k): v for k, v in doc.get("trajes", {}).items() if isinstance(v, int)}
    mapa = {k: v for k, v in mapa.items() if v in ALL_IDS}
    activos = [n for n, _, _ in srcs]
    req = {n: c.get("id") for n, _, c in srcs if c.get("id") in ALL_IDS}
    ids, dueno = {}, {}
    for n in sorted(activos, key=lambda n: list(mapa).index(n) if n in mapa else len(mapa)):
        i = mapa.get(n)                                  # 1. su ID de siempre
        if i is not None and req.get(n, i) == i and i not in dueno:
            ids[n], dueno[i] = i, n
    for n in activos:                                    # 2. ID pedido por el mod
        r = req.get(n)
        if n in ids or r is None:
            continue
        if r in dueno:
            log("!! %s: pide el ID %d, que ya usa %s: se le da otro libre" % (n, r, dueno[r]))
        else:
            ids[n], dueno[r] = r, n
    reserva = {i: n for n, i in mapa.items() if n not in activos and i not in dueno}
    for n in sorted(activos, key=lambda n: n not in mapa):   # 3. su ID o el primer libre; 4. reservas
        if n in ids:
            continue
        i = mapa.get(n)
        if i is None or i in dueno:
            i = next((i for i in ALL_IDS if i not in dueno and i not in reserva), None)
        if i is None and reserva and len(dueno) < MAX_CELDAS:   # sin casilla no vale la pena liberar
            i = min(reserva, key=lambda i: (os.path.isdir(os.path.join(mods_dir, reserva[i])) or
                                            os.path.isdir(os.path.join(mods_dir, reserva[i] + ".disabled")), i))
            log("   %s: ID %d liberado de %s (quitado o desactivado) por falta de IDs" % (n, i, reserva[i]))
            del mapa[reserva.pop(i)]
        if i is None:
            log("!! %s: no quedan plazas (maximo %d personajes nuevos a la vez; los mods quitados o "
                "desactivados guardan la suya): desactiva otro personaje para jugar con este" % (n, MAX_CELDAS))
            ids[n] = None
            continue
        ids[n], dueno[i] = i, n
    nuevo = {k: (ids[k] if ids.get(k) is not None else v) for k, v in mapa.items()
             if ids.get(k) is not None or (k not in ids and v not in dueno)}
    for n in activos:
        if ids[n] is not None and n not in nuevo:
            nuevo[n] = ids[n]
    # rueda: 24 casillas nuevas; si hay mas, se quedan fuera los ultimos en llegar
    con_id = [n for n in nuevo if ids.get(n) is not None]
    for n in con_id[MAX_CELDAS:]:
        log("!! %s: no cabe en la rueda del select (maximo %d personajes nuevos a la vez): "
            "desactiva otro personaje para jugar con este" % (n, MAX_CELDAS))
        ids[n] = None
    return ids, nuevo, trajes


def ordenar_trajes(trajes, orden):
    """Trajes extra en orden de llegada (antes alfabetico: un mod nuevo movia el numero de los otros)."""
    orden = dict(orden)
    for name, _, _ in sorted(trajes, key=lambda t: t[0]):
        if name not in orden:
            orden[name] = max(orden.values(), default=0) + 1
    return sorted(trajes, key=lambda t: (orden[t[0]], t[0])), orden


def guardar_reparto(mods_dir, mapa, orden):
    txt = "\n".join(["# GENERADO por roster_build.py: ID fijo de cada personaje nuevo y orden de los trajes extra.",
                     "# No cambia al anadir o quitar mods (un mod quitado reserva su ID). Editar a mano solo",
                     "# para mover a alguien a proposito (con una copia de la partida guardada).",
                     "version = 1", "", "[ids]"] +
                    ["%s = %d" % (json.dumps(k), v) for k, v in mapa.items()] + ["", "[trajes]"] +
                    ["%s = %d" % (json.dumps(k), v) for k, v in orden.items()]) + "\n"
    path = os.path.join(mods_dir, IDS_FILE)
    try:
        if open(path, encoding="utf-8").read() == txt:
            return
    except OSError:
        pass
    open(path + ".tmp", "w", encoding="utf-8").write(txt)
    os.replace(path + ".tmp", path)


def guardar_avisos(mods_dir):
    """mods/roster_avisos.txt: 'carpeta<TAB>texto' (src/mods.cpp lo ensena como problema del mod)."""
    open(os.path.join(mods_dir, AVISOS_FILE), "w", encoding="utf-8").write(
        "".join("%s\t%s\n" % a for a in dict.fromkeys(AVISOS)))


def all_source_mods(mods_dir):
    """Todos los mods fuente (activos o no): [(carpeta, ruta, cfg, activo)]."""
    out = []
    for name in sorted(os.listdir(mods_dir)):
        d = os.path.join(mods_dir, name)
        f = os.path.join(d, "personaje.toml")
        if name == OUT_MOD or name.startswith(".") or not os.path.isfile(f):
            continue
        try:
            cfg = toml_file(f).get("personaje", {})
        except (OSError, tomllib.TOMLDecodeError):
            cfg = {}
        out.append((name, d, cfg, not mod_off(mods_dir, name)))
    return out


def status(a):
    """Estado de las plazas (IDs libres): libre / ocupada por que mod (JSON)."""
    mods = a.mods or default_mods()
    allm = all_source_mods(mods)
    import contextlib  # noqa: PLC0415
    import io  # noqa: PLC0415
    with contextlib.redirect_stdout(io.StringIO()):    # sus avisos no van en la salida JSON
        ids, mapa, _ = plan_ids(mods, [(n, d, c) for n, d, c, on in allm if on])   # = construir
    by_id = {v: k for k, v in mapa.items()}           # reservadas (mod quitado o desactivado) tambien
    by_id.update({v: k for k, v in ids.items() if v is not None})
    names = {n: c.get("nombre", n) for n, _, c, _ in allm}
    out = {"max": len(ALL_IDS), "plazas": [], "personajes": []}
    for i in ALL_IDS:
        m = by_id.get(i)
        out["plazas"].append({"id": i, "original": slot_name(i), "estado": ("ocupada" if ids.get(m) == i else "reservada") if m else "libre",
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
                                        "%s por %s (%s)" % (p["estado"], p["mod"], p["nombre"]) if p["mod"] else "libre"))
        for p in out["personajes"]:
            if p["conflicto"] or p["sin_plaza"]:
                log("!! %s pide la plaza %s: %s" % (p["mod"], p["id_pedido"],
                                                     "sin plaza libre" if p["sin_plaza"] else "ocupada; usa %s" % p["id"]))
    return 0


def set_toml_keys(path, values):
    """Escribe/actualiza claves `k = v` en la seccion [personaje] (v ya en sintaxis TOML;
    None borra la clave)."""
    original = open(path, encoding="utf-8").read()
    lines = original.splitlines()
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
    text = "\n".join(lines) + "\n"
    if text == original:
        return
    # copia del anterior en mods_respaldo (son ajustes del usuario: nunca se pierden)
    mod_dir = os.path.dirname(os.path.abspath(path))
    bak_dir = os.path.join(os.path.dirname(os.path.dirname(mod_dir)), "mods_respaldo")
    os.makedirs(bak_dir, exist_ok=True)
    import uuid  # noqa: PLC0415
    shutil.copyfile(path, os.path.join(bak_dir, "%s_%s_%s_%s" % (
        os.path.basename(mod_dir), time.strftime("%Y%m%d_%H%M%S"), uuid.uuid4().hex[:8], os.path.basename(path))))
    open(path, "w", encoding="utf-8").write(text)


def write_rgba(path, img):
    """Imagen cruda para el launcher: u32 ancho, u32 alto (LE) + RGBA8."""
    h, w = img.shape[:2]
    with open(path, "wb") as f:
        f.write(struct.pack("<II", w, h))
        f.write(np.ascontiguousarray(img[..., :4], dtype=np.uint8).tobytes())


class VistaError(RuntimeError):
    """Vista previa fallida, con un mensaje claro (main lo deja como ultima linea, sin traceback suelto)."""


def preview(a):
    """Vista previa de icono, rotulo y retratos de un mod fuente (sin construir nada)
    en <mod>/ui/_vista/ (PNG + .rgba). --guardar escribe los ajustes en personaje.toml."""
    mods = a.mods or default_mods()
    d = os.path.join(mods, a.mod)
    tf = os.path.join(d, "personaje.toml")
    c = toml_file(tf).get("personaje", {})
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
    work = work_dir("roster_v_")
    try:
        return _preview(a, c, d, ui, work)
    except swap_b3.NoGameData as e:      # sin el data_cmn.afs bueno (el icono y el rotulo ya estan hechos)
        raise VistaError(str(e)) from None
    except Exception as e:  # noqa: BLE001  (el detalle en el registro y una ultima linea clara)
        import traceback  # noqa: PLC0415
        traceback.print_exc()
        raise VistaError("ERROR: vista previa: %s: %s" % (type(e).__name__, e)) from None
    finally:
        shutil.rmtree(work, ignore_errors=True)


def _preview(a, c, d, ui, work):
    """Cuerpo de preview() (con la carpeta temporal ya creada)."""
    # sin los datos del juego (data_cmn.afs) no hay plantillas de retrato ni accesorios nativos,
    # pero el icono y el rotulo salen igual; los retratos dan el mensaje claro de swap_b3.
    missing = None
    try:
        cmn = Afs(os.path.join(a.us or default_us(), "data_cmn.afs"), work)
    except swap_b3.NoGameData as e:
        cmn, missing = None, e
        if a.solo == "icono":
            log("aviso: %s" % str(e).replace("ERROR: ", "", 1))
    dn = IDS[int(c.get("donante", 21))]
    tpl_fids = DB["data_cmn_portrait_by_slot"][dn["slot"] if dn["slot"] < 39 else 21]
    tpls = [cmn.entry(tpl_fids[v] & 0xFFFF) for v in (0, 1)] if cmn else None
    fetch = cmn.entry if cmn else None
    files = c.get("modelos", [])
    out = os.path.join(ui, "_vista")
    os.makedirs(out, exist_ok=True)
    hd = None
    if files:   # cache del modelo convertido (la conversion PS2->HD es lo mas lento)
        src = os.path.join(d, files[0])
        st = os.stat(src)
        key = "%s|%d|%d|rampas3" % (files[0], st.st_size, int(st.st_mtime))   # rampas3: B2 y SDBH con sombra
        cache, ckey = os.path.join(out, ".modelo_hd.bin"), os.path.join(out, ".modelo_hd.key")
        if os.path.exists(cache) and os.path.exists(ckey) and open(ckey, encoding="utf-8").read() == key:
            hd = open(cache, "rb").read()
        else:
            hd = b3_ramps(to_hd(open(src, "rb").read(), work), a.mod, c)
            # H8 24/10: caché con .tmp + os.replace (si se corta a mitad no queda un .bin roto)
            open(cache + ".tmp", "wb").write(hd)
            os.replace(cache + ".tmp", cache)
            open(ckey + ".tmp", "w", encoding="utf-8").write(key)
            os.replace(ckey + ".tmp", ckey)
    want = ("icono", "retrato") if not a.solo else (a.solo,)
    if tpls is None:
        want = tuple(w for w in want if w != "retrato")
    size = portrait_size(tpls[0]) if tpls else (0, 0)
    ic, pors, notes = select_images(c, ui, hd, size, want, fetch)
    imgs = {"rotulo": np.array(make_name(c.get("nombre", a.mod), 48, 504))}
    if "icono" in want:
        imgs["icono"] = ic
    cw, ch = size
    for v in ((0, 1) if "retrato" in want else ()):
        ready = os.path.join(ui, "retrato_p%d.png" % (v + 1))
        raw = portrait_ready(tpls[v], Image.open(ready)) if pors[v] is None else portrait_bin(tpls[v], pors[v])
        o = struct.unpack(">I", raw[0x20:0x24])[0]
        e = struct.unpack(">3I2H2H7I", raw[o:o + 0x30])
        blob = bytes(raw[e[7]:e[7] + e[8]])
        full = _dds_rgba(blob)
        if full is None:
            full = np.array(Image.open(io.BytesIO(blob)).convert("RGBA"))
        imgs["retrato_p%d" % (v + 1)] = full[:ch, :cw]
    if not a.solo and (hd is not None or os.path.exists(os.path.join(ui, "hud.png"))):
        try:                  # cara de la barra de vida (forma base)
            imgs["hud"] = hud_images(c, ui, [hd], fetch)[0][:mr_hud_h(), :mr_hud_w()]
        except Exception as e:  # noqa: BLE001
            notes.append("cara de la barra de vida: %s" % e)
    for k_, img in imgs.items():
        Image.fromarray(img).save(os.path.join(out, k_ + ".png"))
        write_rgba(os.path.join(out, k_ + ".rgba"), img)
    for n_ in notes:
        log("aviso: " + n_)
    log("vista previa -> %s" % out)
    if missing is not None and a.solo != "icono":
        raise missing           # icono y rotulo hechos; los retratos necesitan el juego
    return 0


def inputs_hash(srcs):
    h = hashlib.sha1(("v%d" % VERSION).encode())
    h.update(open(os.path.join(HERE, "roster_db.json"), "rb").read())
    h.update(open(os.path.abspath(__file__), "rb").read())
    h.update(open(os.path.join(HERE, "model_render.py"), "rb").read())
    h.update(open(os.path.join(HERE, "voces.py"), "rb").read())
    h.update(open(os.path.join(HERE, "gritos.py"), "rb").read())
    h.update(open(os.path.join(HERE, "capsulas.py"), "rb").read())
    for extra in (os.path.join(HERE, "colores.py"), os.path.join(ROOT, "awo_tools", "altura.py"),
                  os.path.join(ROOT, "awo_tools", "rampas.py")):
        if os.path.isfile(extra):        # tambien cambian el resultado (efectos, pies al suelo)
            h.update(open(extra, "rb").read())
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
        self.grafts = {}              # forma -> {codigo: codigo de su variante} (IW, 0x1000)
        self.nforms = 1
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

    def graft_hyper(self, anm, donor, cmn, anim_from=None):
        """Injerta en el moveset (forma 1) el bloque del modo hiper del donante en los codigos
        que usa la entrada hiper convertida. anim_from = {codigo: codigo propio} hace que el
        bloque reproduzca la animacion propia (el aura burst de IW) y no la del donante."""
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
        own = bytes(csk)
        for dst_code, src_code in zip(self.hyper, src_codes):
            csk = capsulas.csk_graft(csk, dst_code, src, src_code)
        csk = bytearray(csk)
        for dst_code, own_code in (anim_from or {}).items():
            # con la animacion propia (aura burst de IW, 60 fotogramas) los eventos del bloque
            # de B3 (120) se comprimen a su duracion: los de f88/f100 terminan de entrar en
            # modo hiper y sin ellos se quedaba en la pantalla oscura (Goku GT, 2026-10-10)
            if capsulas.csk_set_anim(csk, dst_code, own, own_code):
                capsulas.csk_fit_frames(csk, dst_code, capsulas.csk_last_frame(own, own_code))
        return capsulas.amb_rebuild(anm, {k: bytes(csk)})

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
        self.grafts = {}
        notes, lines = [], []
        own = []
        last_tr = 0
        dforms = dn.get("form_caps", [])
        if c.get("formas_donante"):     # formas elegidas del donante (Goku GT: sin Kaioken ni SSJ2)
            dforms = [dforms[int(i)] if 0 <= int(i) < len(dforms) else 0 for i in c["formas_donante"]]
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
                            equipada=bool(cc.get("equipada", True)), reemplaza=cc.get("reemplaza"), iw=cc.get("iw"),
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
                    # cada tecnica de IW con su capsula: por su clave (iw = ...) o, en los
                    # personajes importados antes, por orden
                    skill_caps = {}
                    key_map = {}
                    for kind in ("especial", "definitiva"):
                        mine = [o for o in own if o["tipo"] == kind]
                        keys = [sk["clave"] for sk in capsulas.iw_skills(ccm) if sk["tipo"] == kind]
                        for k, o in enumerate(mine):
                            bcm_key = keys[k] if k < len(keys) else None
                            # la clave declarada (iw = ...) manda; sin ella, la del BCM
                            real = int(o["iw"]) if o.get("iw") is not None else bcm_key
                            if real is None:
                                continue
                            if bcm_key is not None and bcm_key != real:
                                key_map[bcm_key] = real
                            skill_caps.setdefault(int(real), o["id"])
                    ccm, self.grafts, rep = capsulas.adapt_iw_bcm(ccm, skill_caps, self.nforms, key_map)
                    notes += ["BCM de Infinite World adaptado: " + r for r in rep]
                    dsrc = cmn.entry(dn["cam"])
                    dat = capsulas.ccm_child(dsrc)
                    ccm = bytearray(ccm)
                    if dat and capsulas.hyper_tail_from(ccm, bytearray(dsrc[dat[0]:dat[0] + dat[1]])):
                        notes.append("modo hiper: parametros de la entrada del donante")
                    ccm = bytes(ccm)
                    self.hyper = capsulas.hyper_codes(ccm)
                    cam = capsulas.amb_rebuild(cam, {next(k for k, e in enumerate(capsulas.amb_children(cam))
                                                          if e[0] == at[0] and e[1] == at[1]): ccm})
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
            # con una sola forma (formas = 1: Fat Gotenks, Ghost Gotenks, sin modelo de Super Saiyan) las
            # transformaciones del donante no se heredan: se podian equipar y "transformaban" sin cambiar de modelo
            one_form = int(c.get("formas") or 0) == 1
            trans = {x for x in dn.get("form_caps", []) if x} if one_form else set()
            inherit = [x for x in inherit if x not in trans]
            lines.append("hereda = %s" % toml_list(inherit))
            if nativas or trans:
                dflt = [x for x in (nativas + [x for x in dn.get("caps", []) if x not in nativas]) if x not in trans][:7]
                lines.append("capsulas = %s" % toml_list(dflt))
            if trans:
                lines.append("capsulas_forma = [0]")
                notes.append("sin las transformaciones del donante %s (una sola forma)" % sorted(trans))
            if dn.get("skills"):        # su ficha de habilidades (pausa y rotulos en combate)
                lines.append("habilidades_indice = %d" % dn["skills"]["indice"])
            notes.append("capsulas: las del donante (%s)" % dn["name"])
            return lines, cam, notes
        # solo caben 7: primero transformaciones y definitivas (Goku GT: 3 + 2 + 3 especiales;
        # sin esto la Spirit Bomb, la unica definitiva en SSJ4, quedaba fuera)
        prio = {"transformacion": 0, "definitiva": 1, "especial": 2}
        defaults = [o["id"] for o in sorted((o for o in own if o["equipada"]), key=lambda o: prio[o["tipo"]])] +             [x for x in nativas]
        if not iw:
            defaults += [repl.get(x, x) for x in dn.get("caps", []) if repl.get(x, x) not in defaults]
        defaults = [x for x in defaults if x not in dmoves or x in {o["id"] for o in own}][:7]
        fcaps = [repl.get(x, x) for x in dforms] or [0]
        fcaps[0] = 0                # la forma inicial no exige capsula (Gogeta: el SSJ del donante)
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


def csk_copy_codes(anm, moves):
    """Moveset con el bloque de cada codigo viejo copiado tambien en el nuevo {nuevo: viejo}
    (si el moveset lo tiene: las formas solo traen los que cambian)."""
    k, do, ds = capsulas.csk_child(anm)
    csk = anm[do:do + ds]
    n, lst = struct.unpack(">II", csk[0x10:0x18])
    src = bytes(csk)
    for new, old in sorted(moves.items()):
        if old < n and struct.unpack_from(">I", src, lst + 4 * old)[0]:
            csk = capsulas.csk_graft(csk, new, src, old)
    return capsulas.amb_rebuild(anm, {k: csk})


def form_variants(anm_rels, uniq, anm_bins, grafts):
    """Injerta en el moveset de cada forma (copia propia si lo comparte con otra) los codigos
    de sus variantes {forma: {codigo: codigo origen}}, con el bloque de la forma 1 (completa).
    -> (anm_rels, uniq, anm_bins, informe)."""
    anm_rels = list(anm_rels) or [uniq[0]]
    base = anm_bins[uniq.index(anm_rels[0])]
    _, so, ss = capsulas.csk_child(base)
    src = base[so:so + ss]
    rep = []
    for f, pairs in sorted(grafts.items()):
        while len(anm_rels) <= f:
            anm_rels.append(anm_rels[0])
        rel = anm_rels[f]
        b = anm_bins[uniq.index(rel)]
        k, do, ds = capsulas.csk_child(b)
        csk = b[do:do + ds]
        for dst, sc in sorted(pairs.items()):
            csk = capsulas.csk_graft(csk, dst, src, sc)
        nrel = "%s#forma%d" % (rel, f + 1)
        uniq.append(nrel)
        anm_bins.append(capsulas.amb_rebuild(b, {k: csk}))
        anm_rels[f] = nrel
        rep.append("forma %d: %d golpes con su variante de IW (%s)" % (
            f + 1, len(pairs), ", ".join("%#x<-%#x" % x for x in sorted(pairs.items()))))
    return anm_rels, uniq, anm_bins, rep


def forms_config(c, dn, per, donor, name):
    """Formas de un personaje nuevo -> (n de formas, modelo de cada forma, lineas de roster.toml).
      formas = N            no mas que las del donante (el runtime copia sus registros por forma)
      formas_donante = [..] que formas de combate del donante (indices), en que orden
      modelo_forma = [..]   modelo DENTRO del traje de cada forma (0 = el 1o). Por defecto, con
                            tantos modelos por traje como formas, cada forma el suyo; si no, el
                            reparto del donante (Gohan adulto: [0, 1, 1, 2], el SSJ2 = modelo SSJ)
      ki_base = [..]        nivel de ki en barras al que tiende cada forma (por defecto el del donante)
      aura_formas = [..]    tipo de aura de cada forma (0..5; por defecto la del donante)
      fisica = "donante"    cadenas de fisica (pelo, colas del cinturon) del donante, o = ID de
                            otro personaje; sin la clave, ninguna (el modelo queda rigido)"""
    nforms = max(1, int(dn.get("forms") or 1))
    fdon = [int(x) for x in c.get("formas_donante", [])][:8]
    fde = [int(x) for x in c.get("formas_de", [])][:16]
    if fde:       # Tag Team: formas_de = [ID, forma, ID, forma...]: cada forma de un personaje
        nforms = len(fde) // 2
        lines = ["formas_de = %s" % toml_list(fde),          # "formas = N" lo pone el bucle (formas en el toml)
                 "modelo_forma = %s" % toml_list(list(range(nforms)))]
        af = [max(0, min(5, int(x))) for x in c.get("aura_formas", [])][:nforms]
        if af:
            lines.append("aura_formas = %s" % toml_list(af))
        if c.get("equipo"):
            lines.append("equipo = %s" % toml_list([int(x) for x in c["equipo"]]))
        return nforms, list(range(nforms)), lines
    if fdon:      # formas_donante = [0, 2, 4, 5]: las formas de combate del donante, en ese orden
        bad = [x for x in fdon if not 0 <= x < len(dn.get("form_caps") or [0])]
        if bad:
            log("   %s: aviso: formas_donante %s: %s no tiene esas formas" % (name, bad, dn["name"]))
        nforms = len(fdon)
    elif c.get("formas"):
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
    if fdon:
        lines.append("formas_donante = %s" % toml_list(fdon))
    kib = [max(0, min(7, int(x))) for x in c.get("ki_base", [])][:nforms]
    if kib:
        lines.append("ki_base = %s" % toml_list(kib))
    af = [max(0, min(5, int(x))) for x in c.get("aura_formas", [])][:nforms]
    if af:
        lines.append("aura_formas = %s" % toml_list(af))
    fis = c.get("fisica")
    fid = donor if str(fis).lower() == "donante" else fis if type(fis) is int else None
    if fid is not None and (fid not in IDS or not IDS[fid].get("models")):
        log("   %s: aviso: fisica = %r: personaje sin modelos, sin fisica" % (name, fis))
        fid = None
    if fid is not None:
        lines.append("fisica = %d" % fid)
    return nforms, mforma, lines


def build(a):
    global CACHE_IMAGENES, CACHE_DIR
    mods = a.mods or default_mods()
    us = a.us or default_us()
    out_dir = os.path.join(mods, OUT_MOD)
    CACHE_IMAGENES = os.path.join(os.path.dirname(os.path.abspath(mods)), "mods_cache", "retratos")
    CACHE_DIR = os.path.join(os.path.dirname(os.path.abspath(mods)), "mods_cache")
    gritos.PACKETS_DIR = os.path.join(CACHE_DIR, "gritos")
    srcs = source_mods(mods)
    trajes = traje_mods(mods)
    if not srcs and not trajes:
        # Fuentes presentes pero ilegibles: no retirar el roster que funcionaba.
        if any(not mod_off(mods, n) and
               any(os.path.isfile(os.path.join(mods, n, f)) for f in
                   ("personaje.toml", "traje.toml")) for n in os.listdir(mods) if n != OUT_MOD):
            raise ValueError("ninguna fuente valida: _roster anterior conservado")
        if os.path.isdir(out_dir):
            if not os.path.isfile(os.path.join(mods, IDS_FILE)):   # reservas antes de apartar el _roster
                guardar_reparto(mods, ids_del_roster(mods), {})
            retire_tree(out_dir)
            log("sin personajes nuevos activos: _roster apartado en mods_respaldo")
        else:
            log("sin personajes nuevos activos")
        guardar_avisos(mods)
        return 0
    ids, mapa, orden = plan_ids(mods, srcs)
    trajes, orden = ordenar_trajes(trajes, orden)
    digest = inputs_hash(srcs + [(n, d, None) for n, d, _ in trajes])
    # el reparto entra en el sello: si cambia un ID u orden de trajes, se vuelve a montar
    digest = hashlib.sha1((digest + json.dumps([ids, [t[0] for t in trajes]], sort_keys=True)).encode()).hexdigest()
    stamp = os.path.join(out_dir, "build.hash")
    if not a.force and os.path.isfile(stamp) and open(stamp).read().strip() == digest:
        guardar_reparto(mods, mapa, orden)
        log("_roster al dia (%d personajes)" % len(srcs))
        return 0
    target_dir = out_dir
    txn = None
    work = work_dir("roster_")
    global _POOL
    from concurrent.futures import ThreadPoolExecutor  # noqa: PLC0415
    _POOL = ThreadPoolExecutor(max_workers=max(2, (os.cpu_count() or 4)))
    try:
        us = us_dir(us)     # aqui y no arriba: sin personajes no hace falta el juego
        cmn = Afs(os.path.join(us, "data_cmn.afs"), work)
        # EU/PAL no trae data_usi.afs (su ingles es data_eng, misma estructura): se parte de
        # data_eng; lo escrito en data_usi no lo lee el juego EU (inofensivo).
        usi_path = os.path.join(us, "data_usi.afs")
        usi = Afs(usi_path if os.path.isfile(usi_path) else os.path.join(us, "data_eng.afs"), work)
        txn = TreePublish(target_dir)
        out_dir = str(txn.stage)
        stamp = os.path.join(out_dir, "build.hash")
        next_fid = len(cmn.index)
        sel = bytearray(usi.entry(SELECT_ENTRY))
        texs = azt_textures(sel)
        icon_tex = texs[ICON_TEX]
        icon_img = azt_read(sel, icon_tex)
        icon_orig = icon_img.copy()
        icon_k = icon_tex["hd"][0] / icon_tex["logical"][0]
        icons = Packer(icon_tex, 56, 56, free_regions(icon_tex))
        name_items = []
        ojos_strips = []        # franjas de ojos 252x60 por personaje (ojos = [@oK] en roster.toml)
        tor_icons = []          # iconos 84x84 de las celdas nuevas: tiras del Torneo (S5-I1, issue #33)
        free = ALL_IDS
        toml = ["# GENERADO por mod center hd/roster_build.py (no editar: se regenera).", ""]
        manifest = []
        caps = Capsules(usi, cmn)
        own_caps, first_new = {}, caps.next_id
        voice_out = voces.VoiceWriter(us, out_dir)
        iw_voices = None
        b1_cache = {}
        langs = {k: Afs(os.path.join(us, "lang_%s.afs" % k), work) for k in ("usa", "jpn")}
        poses = []
        next_lang = max(len(x.index) for x in langs.values())
        steps = len(srcs) + 2
        for k_src, (name, d, c) in enumerate(srcs):
            nombre = c.get("nombre", name)
            progress(k_src, steps, nombre)
            cid = ids[name]
            if cid is None:
                continue          # sin ID o sin casilla: plan_ids ya dio el aviso
            try:
                donor = int(c.get("donante", 21))
                dn = IDS[donor]
                if not dn.get("models"):
                    raise ValueError("donante sin modelos")
            except (ValueError, TypeError, KeyError) as exc:
                log("!! %s: donante no valido (%s): omitido" % (name, exc))
                continue
            # modelos
            files = c.get("modelos", [])
            if not files:
                log("!! %s: sin modelos" % name)
                continue
            try:
                per = int(c.get("modelos_por_traje", 1))
                if not 1 <= per <= 8:
                    raise ValueError("modelos_por_traje debe ser 1..8")
                nforms, mforma, form_lines = forms_config(c, dn, per, donor, name)
            except (ValueError, TypeError, KeyError) as exc:
                log("!! %s: formas no validas (%s): omitido" % (name, exc))
                continue
            model_fids, heights, prefixes, hds = [], [], [], []
            first_hd = None
            hdbs = []
            try:              # un modelo que no se puede leer no tumba el montaje de los demas
                raws = []
                for rel in files:
                    with open(os.path.join(d, rel), "rb") as fh:
                        raws.append(fh.read())
                if c.get("moveset") and c.get("altura", True):
                    try:
                        raws = forms_height_fixed(raws, per, load_bin(os.path.join(d, c["moveset"][0]), work),
                                                  cmn, dn, name)
                    except Exception as e:  # noqa: BLE001
                        log("   %s: aviso: altura de las formas sin ajustar (%s)" % (name, e))
                for rel, raw in zip(files, raws):
                    hdbs.append(b3_ramps(to_hd(raw, work), name, c))
            except Exception as e:  # noqa: BLE001
                log("!! %s: modelo no valido (%s): personaje omitido" % (name, e))
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
            missing_por = [v + 1 for v in (0, 1) if pors[v] is None and not
                           os.path.isfile(os.path.join(ui, "retrato_p%d.png" % (v + 1)))]
            if missing_por:
                log("!! %s: faltan retratos terminados %s: omitido" % (name, missing_por))
                continue
            for n_ in notes:
                log("   %s: %s" % (name, n_))
            ix, iy = icons.take()
            px, py = int(round(ix * icon_k)), int(round(iy * icon_k))
            icon_img[py:py + ic.shape[0], px:px + ic.shape[1]] = ic
            # <= 256 px: la plantilla ancha (512) no se ve en la rueda (nombres largos sin rotulo, visto 2026-10-18);
            # un nombre largo se estrecha para caber en la de 256
            nm = np.array(make_name(nombre, 48, 256))
            tpl = NAME_TEX_TEMPLATES[0] if nm.shape[1] <= 256 else NAME_TEX_TEMPLATES[1]
            canvas = np.zeros((64, 256 if tpl == NAME_TEX_TEMPLATES[0] else 512, 4), np.uint8)
            canvas[:nm.shape[0], :nm.shape[1]] = nm
            nw = int(np.ceil(nm.shape[1] / 1.5))
            name_items.append((canvas, (nw, 32), (nm.shape[1], 48), tpl))
            name_slot = len(name_items) - 1
            tor_icons.append(ic)
            tor_k = len(tor_icons) - 1
            # retratos P1/P2 (plantilla: los del slot del donante)
            por = []
            for v in (0, 1):
                ready = os.path.join(ui, "retrato_p%d.png" % (v + 1))   # retrato ya hecho (512x512)
                data = (portrait_ready(tpls[v], Image.open(ready)) if pors[v] is None
                        else portrait_bin(tpls[v], pors[v]))
                write_entry(out_dir, "data_cmn.afs", next_fid, data, work)
                por.append(next_fid)
                next_fid += 1
            try:   # franja de ojos de la bandeja Ability: recorte del retrato propio
                ojos_strips.append(eye_strip(pors[0] if pors[0] is not None else Image.open(os.path.join(ui, "retrato_p1.png"))))
                ojos_k = len(ojos_strips) - 1
            except Exception as e:  # noqa: BLE001
                ojos_k = None
                log("   %s: sin franja de ojos propia (%s)" % (name, e))
            # cara de la barra de vida: una por forma (render de su modelo o ui/hud*.png)
            hud_tpl = cmn.entry(dn.get("hud_face") or IDS[0]["hud_face"])
            nf = nforms if c.get("formas") or c.get("formas_donante") else struct.unpack(">I", hud_tpl[0x10:0x14])[0]
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
            cam_fid_esc = None
            cam = load_bin(os.path.join(d, c["camara"]), work) if c.get("camara") else None
            if cam is not None and dn.get("cam") is not None:
                cam, npad = cam_pad_clips(cam, cmn.entry(dn["cam"]))
                if npad:
                    log("   %s: camara: %d clips del donante anadidos al final (tenia menos que el)" % (name, npad))
            n_new = len(caps.new)
            caps.nforms = nforms
            cap_lines, cam, notes = caps.character(name, cid, donor, c, cam, cmn)
            own_caps[cid] = {x[0] for x in caps.new[n_new:]}
            # moveset por forma; el mismo fichero en varias formas se escribe una vez (con uno
            # solo, las demas formas usan el de la forma 1, como el SSJ de Gohan adulto)
            anm_rels = list(c.get("moveset", []))
            uniq = list(dict.fromkeys(anm_rels))
            anm_bins = [load_bin(os.path.join(d, rel), work) for rel in uniq]
            if caps.grafts.get(-1) and anm_bins:     # golpes de IW que estaban en 0x258/0x358
                moves = caps.grafts.pop(-1)
                anm_bins = [csk_copy_codes(b, moves) for b in anm_bins]
            try:
                # tecnicas = "donante": las del donante aunque el moveset sea de la fuente (Yamcha amarillo de IW: su
                # BSP de IW pedia efectos que no existen en la definitiva y el juego se cerraba; sus tres tecnicas son
                # las mismas que las del Yamcha del juego)
                bsp = (load_bin(os.path.join(d, c["tecnicas"]), work)
                       if c.get("tecnicas") and c["tecnicas"] != "donante" else None)
                if bsp is not None and c.get("origen_fuente") == "iw" and c.get("origen_clave") in IW_TECNICAS_DONANTE:
                    bsp = None
                    log("   %s: tecnicas del donante (con su BSP de IW la definitiva del donante cerraba el juego)" % name)
            except Exception as e:  # noqa: BLE001 (un BSP ilegible no tumba el montaje de todos)
                log("   %s: aviso: tecnicas propias ilegibles (%s): las del donante" % (name, e))
                bsp = None
            if bsp:
                bsp, n6 = bsp_bank6_fix(bsp, cmn.entry(dn["bsp"]))
                if n6:
                    log("   %s: tecnicas: %d efectos del banco 6 (que su donante no carga) con los del primer rayo"
                        % (name, n6))
                nres = bsp_resources(bsp)
                if nres > 64:
                    # la ranura de tecnicas del juego guarda 64 recursos (los nativos llegan a 52): con mas, el
                    # ultimo pisa la lista de instancias y al acabar el combate el juego liberaba un bloque del
                    # fichero y se cerraba (Vegeta GT de IW: 65). src/ports_guard.cpp lo evita desde 2026-10-15.
                    log("   %s: aviso: tecnicas con %d recursos (el juego admite 64); necesita el exe con la "
                        "guarda de ports (2026-10-15 o posterior)" % (name, nres))
            if anm_bins and c.get("moveset"):
                # efectos del banco de grupo (AP7 clase 4 >= 300) que el donante no carga: puntero
                # nulo y el juego se cerraba (Final Flash de Vegeta GT pedia 352, del banco de
                # Freeza). Si el rayo esta en su BSP pasa a un codigo propio libre (< 300, se busca
                # en su BSP); si no, se quita. Los < 300 se quedan: van a su BSP.
                try:
                    loaded = set(common_fx(cmn))              # los que cargan todos (polvo, ki...)
                    for f in dn.get("anm", []):
                        if 0 <= f < len(cmn.index):           # formas que el donante no tiene en el disco
                            _, so, ss = capsulas.csk_child(cmn.entry(f))
                            loaded |= capsulas.csk_fx_values(cmn.entry(f)[so:so + ss])
                    used = set()
                    for b in anm_bins:
                        _, so, ss = capsulas.csk_child(b)
                        used |= capsulas.csk_fx_values(b[so:so + ss])
                    own_cst, own_cse = capsulas.bsp_fx_codes(bsp) if bsp else (set(), set())
                    bank = sorted(v for v in used if v >= capsulas.FX_GROUP_BANK and v not in loaded)
                    taken = used | loaded | own_cst | own_cse
                    free = (v for v in range(1, capsulas.FX_GROUP_BANK) if v not in taken)
                    remap = {v: next(free) for v in bank if v in own_cst}
                    drop = set(bank) - set(remap)
                    fixed = []
                    for b in anm_bins:
                        ck, so, ss = capsulas.csk_child(b)
                        csk = bytearray(b[so:so + ss])
                        capsulas.csk_remap_fx(csk, remap)
                        capsulas.csk_drop_fx(csk, used - drop | set(remap.values()))
                        fixed.append(capsulas.amb_rebuild(b, {ck: bytes(csk)}))
                    anm_bins = fixed
                    if remap:
                        bsp = capsulas.bsp_rename_cst(bsp, remap)
                        notes.append("rayos del banco de otro grupo pasados a su BSP: %s" % ", ".join(
                            "%d->%d" % kv for kv in sorted(remap.items())))
                    if drop:
                        notes.append("efectos que %s no carga, quitados (cerraban el juego): %s" % (
                            dn["name"], ", ".join(str(v) for v in sorted(drop))))
                except Exception as e:  # noqa: BLE001
                    notes.append("aviso: efectos sin revisar (%s)" % e)
                # moverse por el suelo: animaciones de IW/SB que suben la cadera (el port "flotaba"
                # al andar) bajan hasta el tobillo del reposo, si el donante las tiene apoyadas
                if c.get("altura", True) and first_hd:
                    try:
                        import altura  # noqa: PLC0415
                        sk = altura.skeleton(first_hd)
                        ref = (altura.skeleton(cmn.entry(dn["models"][0])), cmn.entry(dn["anm"][0]))
                        fixed, low = [], {}
                        # ports de IW: todos los codigos en que el donante pisa el suelo (Goku GT flotaba en 185 de
                        # ellos, auditoria10/A); ground_moves ya exige al donante apoyado en ese codigo
                        codes = range(0x400) if c.get("origen_fuente") == "iw" else altura.GROUND_CODES
                        for b in anm_bins:
                            b, rep = altura.ground_moves(b, sk, ref, codes=codes)
                            fixed.append(b)
                            low.update(rep)
                        anm_bins = fixed
                        if low:
                            notes.append("pies al suelo al moverse: cadera bajada en los codigos %s" % (
                                ", ".join("%d (%.2f)" % kv for kv in sorted(low.items()))))
                    except Exception as e:  # noqa: BLE001
                        notes.append("aviso: apoyo al moverse sin revisar (%s)" % e)
            if caps.hyper and anm_bins:
                try:
                    anim_from = caps.grafts.pop(-2, None)       # IW: su animacion de aura burst
                    anm_bins = [caps.graft_hyper(b, donor, cmn, anim_from) for b in anm_bins]   # todas las formas
                    notes.append("modo hiper: animacion y efecto de Budokai 3 (los de %s)" % dn["name"])
                except Exception as e:  # noqa: BLE001
                    notes.append("aviso: sin injerto del modo hiper (%s)" % e)
            # IW: golpes con otra animacion en algunas formas (Kamehameha x10 del SSJ4 de Goku
            # GT): en el moveset de esa forma, el codigo de la entrada comun con la de su variante
            if caps.grafts and anm_bins:
                try:
                    anm_rels, uniq, anm_bins, rep = form_variants(anm_rels, uniq, anm_bins, caps.grafts)
                    notes += rep
                except Exception as e:  # noqa: BLE001
                    notes.append("aviso: variantes por forma sin injertar (%s)" % e)
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
            auto_tr = (nforms > 1 and cam is not None and not capsulas.has_transform(cam)
                       and not c.get("equipo"))   # Tag Team: el relevo es SELECT, sin P+K+G
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
            # IW: guiones del donante (sus ordenes de IW colgaban o cerraban la definitiva); guion = "propio" lo evita
            if (c.get("origen_fuente") == "iw" and cam is not None and anm_bins
                    and str(c.get("guion", "donante")).lower() != "propio"):
                try:
                    cam, anm_bins, rep = iw_guion_donante(cam, anm_bins, cmn.entry(dn["cam"]), cmn.entry(dn["anm"][0]))
                    notes += list(dict.fromkeys(rep))
                except Exception as e:  # noqa: BLE001
                    notes.append("aviso: guion de IW sin cambiar por el del donante (%s)" % e)
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
                    if cam is None:   # solo_modelos: la camara del donante, a la altura del modelo (#36)
                        cam_fid_esc, next_fid = donor_cam_scaled(cmn, dn, first_hd, out_dir, next_fid, work, name)
                except Exception as e:  # noqa: BLE001
                    log("   %s: aviso: altura del donante sin ajustar (%s)" % (name, e))
                    anm = []
            if anm:
                extra.append("anm = %s" % toml_list(anm))
            if c.get("formas") or c.get("formas_donante"):
                extra.append("formas = %d" % nforms)
            extra += form_lines
            # efectos de tecnicas propios (bsp, leido arriba) y/o color del ki (tono sobre los del donante)
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
                    if str(c["aura_de"]).strip().isdigit():   # tambien sus tipos de aura por forma (roster_ext.cpp)
                        extra.append("aura_tipos = %d" % int(c["aura_de"]))
            except Exception as e:  # noqa: BLE001
                log("   %s: aviso: aura del donante (%s)" % (name, e))
                aura_fid = dn["aura"]
            # aura de cada transformacion (Kit): nombre -> forma de Goku cuya fila de tipo se copia (-1 = sin aura)
            if c.get("aura_tipos_forma"):
                idx = [AURA_TIPOS.get(str(x).strip().lower(), -2) for x in c["aura_tipos_forma"]][:8]
                if -2 in idx:
                    log("   %s: aviso: aura_tipos_forma con nombres desconocidos (validos: %s)"
                        % (name, ", ".join(AURA_TIPOS)))
                extra.append("aura_tipos_forma = %s" % toml_list([-3 if i == -2 else i for i in idx]))
            if c.get("aura_color") is not None:
                try:
                    aura, cnt = colores.retint(cmn.entry(aura_fid), colores.tono(c["aura_color"]))
                    write_entry(out_dir, "data_cmn.afs", next_fid, aura, work)
                    extra.append("aura = %d" % next_fid)
                    next_fid += 1
                    log("   %s: color del aura %s (%d texturas)" % (name, c["aura_color"], cnt["texturas"]))
                    if not any(cnt.values()):
                        log("   %s: aviso: el color del aura %s no cambio nada (aura clara o gris)"
                            % (name, c["aura_color"]))
                except Exception as e:  # noqa: BLE001
                    log("   %s: aviso: color del aura sin cambiar (%s)" % (name, e))
                    if aura_fid is not None and 0 <= int(aura_fid) < 3990:
                        extra.append("aura = %d" % aura_fid)
            elif aura_fid is not None and 0 <= int(aura_fid) < 3990:
                # siempre explicito: el fid del aura queda en el roster.toml y no depende de que
                # el runtime copie la tabla por ID (frente 12: causa comun de "sin aura base").
                extra.append("aura = %d" % aura_fid)
                log("   %s: aura fid %d%s" % (name, aura_fid,
                    "" if aura_fid == dn.get("aura") else " (no la del donante)"))
            if cam is not None:
                write_entry(out_dir, "data_cmn.afs", next_fid, cam, work)
                extra.append("cam = %d" % next_fid)
                next_fid += 1
            elif cam_fid_esc is not None:      # camara del donante ya escrita por donor_cam_scaled (arreglo E3: antes se pasaba el fid como bytes)
                extra.append("cam = %d" % cam_fid_esc)
            extra += cap_lines
            # voces de combate: las de su juego (port), las de otro personaje o las del donante
            try:
                spec = c.get("voces", "donante")
                if isinstance(spec, str) and "iw:" in spec.lower() and iw_voices is None:
                    try:
                        iw_voices = voces.IwVoices(getattr(a, "iw", None))
                    except Exception:  # noqa: BLE001  (sin Infinite World: la alternativa tras "|", si la hay)
                        if "|" not in spec:
                            raise
                v_lines, v_note = voces.resolve(spec, voice_out, iw_voices, donor=donor, base=d)
                extra += v_lines
                log("   %s: %s" % (name, v_note))
            except Exception as e:  # noqa: BLE001
                log("   %s: aviso: voces del donante (%s)" % (name, e))
            # gritos de los golpes: banco propio (lang_usa / lang_jpn) hecho sobre el del donante
            # "iw:JANENBA|ninguno": alternativas en orden (la primera que se pueda montar; sin ninguna, las del donante)
            gspec = c.get("gritos")
            if gspec is None:
                vs = c.get("voces", "donante")
                gspec = vs if isinstance(vs, str) and vs.lower().startswith(("iw:", "sb1:", "sb2:", "t3:")) else "donante"
            g_err = None
            for gspec in gspec.split("|"):
                try:
                    gl = gspec.strip().lower()
                    if gl not in ("donante", "", "auto"):
                        dl = dn["lang"][0]           # char372 +0x24: banco de gritos
                        built = {}
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
                            elif gl.startswith("wav:"):
                                # WAV propios del mod (p. ej. las voces de Heroes que saca importar.py), por duracion
                                sounds = gritos.assign(lens, gritos.wav_pool(os.path.join(d, gspec[4:].strip())))
                            elif gl.startswith("sdbh:"):
                                # voces de Super Dragon Ball Heroes World Mission (su instalacion, como importar.py):
                                # no se copian al mod; sin el juego, gritos del donante
                                if b1_cache.get(gl) is None:
                                    import cri  # noqa: PLC0415
                                    import importar  # noqa: PLC0415
                                    pool = [x for _, x in cri.voces_sdbh(importar.sdbh_cpks(), gspec[5:].strip())]
                                    if not pool:
                                        raise ValueError("sin voces de Heroes para %s" % gspec)
                                    b1_cache[gl] = gritos.norm_pool(pool)
                                sounds = gritos.assign(lens, b1_cache[gl])
                            elif gl.startswith("t3:"):
                                # voces de Budokai Tenkaichi 3 (su ISO: DBZ3_T3_ISO o ps2_games), por duracion.
                                # El banco depende del idioma (japones/ingles): cache por clave + idioma
                                clave = gl + ":" + lk
                                if b1_cache.get(clave) is None:
                                    b1_cache[clave] = gritos.t3_pool(gspec[3:].strip(), lk)
                                sounds = gritos.assign(lens, b1_cache[clave])
                            elif gl.startswith(("sb1:", "sb2:")):
                                import sb_voces  # noqa: PLC0415  (Shin Budokai: tonos PPHD -> huecos)
                                sounds = sb_voces.gritos_sounds(gspec, lens, lk, entry[bank[0]:bank[0] + bank[1]])
                            else:
                                raise ValueError("gritos desconocidos: %r" % gspec)
                            built[lk] = gritos.build_lang(entry, sounds)
                        for lk, data in built.items():   # solo si todos los idiomas salieron
                            write_entry(out_dir, "lang_%s.afs" % lk, next_lang, data, work)
                        extra.append("idioma = %s" % toml_list([next_lang, dn["lang"][1]]))
                        log("   %s: gritos propios (%s, %d huecos)" % (name, gspec, len(lens)))
                        next_lang += 1
                    g_err = None
                    break
                except Exception as e:  # noqa: BLE001
                    g_err = e
            if g_err is not None:
                log("   %s: aviso: gritos del donante (%s)" % (name, g_err))
            # encuadre del select: escala segun la altura (ajuste sobre los 38 nativos)
            h = max(heights)
            scale = min(1.24, max(0.88, 1.4228 - 0.0201 * h))
            s = list(dn["select"])
            s[3] = s[10] = round(scale, 3)
            toml += (["[[personaje]]", "# %s (mod %s)" % (nombre, name), "id = %d" % cid, "donante = %d" % donor,
                     'nombre = "%s"' % "".join(ch for ch in nombre.upper() if 32 <= ord(ch) < 127 and ch != '"'),
                     "modelos = %s" % toml_list(model_fids), "bocas = %s" % toml_list(lips_fids),
                     "modelos_por_traje = %d" % per] +
                    (["anfitrion = %d" % dn["slot"]] if c.get("celda") is False else   # el Heroe: sin celda en ninguna rueda
                     ["celda = true", "tras_casilla = %d" % next(sl for sl in (IDS[int(c.get("despues_de", donor))]["slot"],
                                                                               dn["slot"], 0) if sl < 38)]) + [
                     "icono = %s" % toml_list([ICON_TEX, ix, iy, ix + 56, iy + 56]),
                     "rotulo = %s" % toml_list(["@%d" % name_slot, 0, 0, nw, 32]),
                     "torneo_icono = %s" % toml_list(["@s%d" % (tor_k // 5), 56 * (tor_k % 5), 0, 56 * (tor_k % 5) + 56, 56]),
                     "torneo_rotulo = %s" % toml_list(["@t%d" % name_slot, 0, 0, nw, 32]),
                     "retrato = %s" % toml_list(por)] +
                    (["ojos = [@o%d]" % ojos_k] if ojos_k is not None else []) + [
                     "select = %s" % toml_list([float(x) if i % 7 < 4 else int(x) for i, x in enumerate(s)])])
            toml += extra + [""]
            manifest.append("%s: id %d, donante %d (%s), modelos %s, bocas %s, retratos %s, altura %.1f -> escala %.2f"
                            % (nombre, cid, donor, dn["name"], model_fids, sorted(set(lips_fids)), por, h, scale))
            log("+ " + manifest[-1])
        progress(len(srcs), steps, "trajes")
        next_fid = build_trajes(trajes, cmn, out_dir, next_fid, work, toml, manifest, ids)
        azt_write(sel, icon_tex, icon_img)
        # plantilla que cuadre en ESTE #AZT (con data_usi es la fija; con data_eng de base, EU, otra)
        sel, name_idx = azt_append(sel, [(im, lg, hd, azt_template(sel, tp, im.shape[1], im.shape[0]))
                                         for im, lg, hd, tp in name_items])
        for i, t in enumerate(name_idx):   # textura real de cada rotulo
            toml = [ln.replace("[@%d," % i, "[%d," % t) if ln.startswith("rotulo") else ln for ln in toml]
        write_entry(out_dir, "data_usi.afs", SELECT_ENTRY, bytes(sel), work)
        ojos_tex = ojos_append(usi, out_dir, "data_usi.afs", ojos_strips, work)
        toml = ojos_toml(toml, ojos_tex, len(ojos_strips))
        tor_idx = tornament_append(usi, out_dir, "data_usi.afs", tor_icons, name_items, work)
        toml = ([torneo_indices(ln, tor_idx) for ln in toml] if tor_idx
                else [ln for ln in toml if not ln.startswith("torneo_")])
        data_langs = []
        drawn = (icon_img != icon_orig).any(-1)
        for lk in DATA_LANGS:
            path = os.path.join(us, "data_%s.afs" % lk)
            if not os.path.isfile(path):
                continue
            la = Afs(path, work)
            if ojos_append(la, out_dir, "data_%s.afs" % lk, ojos_strips, work) != ojos_tex:
                log("aviso: data_%s.afs: franjas de ojos en otro indice (el juego protege la lectura)" % lk)
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
            if tor_idx and tornament_append(la, out_dir, "data_%s.afs" % lk, tor_icons, name_items, work) != tor_idx:
                log("aviso: data_%s.afs: Torneo con otros indices (iconos/rotulos del cuadro del anfitrion)" % lk)
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
        progress(len(srcs) + 1, steps, "capsulas")
        toml += caps.finish(out_dir, work, data_langs)
        flush_entries()
        open(os.path.join(out_dir, "roster.toml"), "w", encoding="utf-8").write("\n".join(toml))
        open(os.path.join(out_dir, "manifest.txt"), "w", encoding="utf-8").write(
            "Personajes nuevos (generado por roster_build.py)\n" + "\n".join(manifest) + "\n")
        open(stamp, "w").write(digest)
        txn.commit(validate_roster)
        guardar_reparto(mods, mapa, orden)     # solo con el _roster publicado (si falla, nada cambia)
        guardar_avisos(mods)
        try:   # el _roster ya esta publicado: un fallo aqui es un aviso, no un montaje fallido
            clean_custom_lists(mods, first_new, own_caps)
        except Exception as exc:  # noqa: BLE001
            log("aviso: listas Custom sin revisar (%s)" % exc)
        log("_roster listo: %d personajes, data_cmn %d..%d" % (len(manifest), len(cmn.index), next_fid - 1))
        return 0
    finally:
        try:
            flush_entries()
        finally:
            if _POOL is not None:
                _POOL.shutdown(wait=True)
                _POOL = None
            if txn is not None:
                txn.finish()
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
        # copia unica (A4/C7): dos montajes en el mismo segundo ya no se pisan la copia
        import uuid  # noqa: PLC0415
        shutil.copyfile(path, path + time.strftime(".%Y%m%d_%H%M%S.") + uuid.uuid4().hex[:8] + ".bak")
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
CAP_KEYS = ("nombre", "tipo", "forma", "formas", "ki", "equipada", "reemplaza", "iw", "quien", "descripcion",
            "botones", "nota")


def read_caps(path):
    return [dict(c) for c in toml_file(path).get("capsula", [])]


def write_caps(path, caps):
    """Reescribe las secciones [[capsula]] de personaje.toml (el resto se conserva; la version
    anterior queda en <mod>/respaldo/: los comentarios de esas secciones no se reescriben)."""
    text = open(path, encoding="utf-8").read()
    bk = os.path.join(os.path.dirname(path), "respaldo")
    os.makedirs(bk, exist_ok=True)
    # H8 24/10: uuid en el nombre de la copia para que dos guardados en el mismo segundo no se pisen
    import uuid  # noqa: PLC0415
    with open(os.path.join(bk, "personaje.toml.%s.%s" % (time.strftime("%Y%m%d_%H%M%S"), uuid.uuid4().hex[:8])),
              "w", encoding="utf-8") as fh:
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
    # H8 24/10: escritura a .tmp + os.replace para no dejar personaje.toml a medias si falla
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write(chr(10).join(out) + chr(10))
    os.replace(tmp, path)


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


def _xlsx_rows(path, sheet=1):
    """Filas (dict columna -> texto) de una hoja (1 = la 1a) de un .xlsx (sin dependencias)."""
    import html  # noqa: PLC0415
    import re  # noqa: PLC0415
    import zipfile  # noqa: PLC0415
    z = zipfile.ZipFile(path)
    strs = []
    if "xl/sharedStrings.xml" in z.namelist():
        ss = z.read("xl/sharedStrings.xml").decode("utf-8")
        strs = [html.unescape("".join(re.findall(r"<t[^>]*>([^<]*)</t>", si)))
                for si in re.findall(r"<si>(.*?)</si>", ss, re.S)]
    x = z.read("xl/worksheets/sheet%d.xml" % sheet).decode("utf-8")
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


def _norm(t):
    return re.sub(r"[^a-z0-9]", "", str(t).lower())


IW_USER_ALIAS = {"baby": "superbaby2", "babyvegeta": "superbaby2", "superbabyvegeta": "superbaby2",
                 "superbabyvegeta2": "superbaby2", "vegetababy": "superbaby2", "gokugt": "gokugt",
                 "vegetagt": "vegetagt", "super17": "super17", "superandroid17": "super17",
                 "omegashenron": "synshenron", "frieza": "frieza", "freeza": "frieza", "hercule": "hercule",
                 "mrsatan": "hercule", "tien": "tien", "tienshinhan": "tien", "pikkon": "pikkon"}


def iw_skill_names(who, skills, path=None):
    """{clave IW: nombre} de las tecnicas de un BCM de IW (capsulas.iw_skills) con la hoja de
    capsulas de IW (Skill List + Null Capsules). Los IDs de la hoja no son los del juego pero,
    por personaje y tipo, van a una distancia fija: se elige la que mas tecnicas hace coincidir
    con el primer nivel de una de la hoja (las fusiones no son definitivas del BCM)."""
    path = path or os.path.join(RESOURCES, NAME_LISTS["iw"])
    if not os.path.isfile(path):
        return {}
    rows = []
    for sh in (1, 3):
        try:
            rr = _xlsx_rows(path, sh)
        except KeyError:
            continue
        head = {}
        for r in rr[:3]:
            for k, v in r.items():
                v = str(v or "").upper()
                if v in ("ID", "TYPE", "USER") or v.startswith("NAME"):
                    head.setdefault(v.split(" ")[0], k)
            if len(head) >= 4:
                break
        for r in rr:
            try:
                i = int(str(r.get(head.get("ID"))).replace(",", ""))
            except ValueError:
                continue
            rows.append((i, _clean(r.get(head.get("NAME")) or ""), str(r.get(head.get("TYPE")) or ""),
                         _clean(r.get(head.get("USER")) or "")))
    users = {_norm(u) for _, _, _, u in rows if u}
    key = _norm(re.sub(r"\((alt colou?r pall?ete|battle damaged[^)]*|halo|default)\)", "", who, flags=re.I))
    key = IW_USER_ALIAS.get(key, key)
    if key not in users:
        cand = [u for u in users if u and (u in key or key in u)]
        key = max(cand, key=len) if cand else key
    out = {}
    for kind, tname in (("especial", "Super"), ("definitiva", "Ultimate")):
        mine = sorted((i, n) for i, n, t, u in rows if _norm(u) == key and t == tname and n
                      and not re.match(r"(fusion|potara)", n, re.I))
        first = {}
        for i, n in mine:             # primer nivel de cada tecnica (3 filas seguidas)
            if not any(n == m and 0 < i - j <= 2 for j, m in first.items()):
                first[i] = n
        sk = [x["clave"] for x in skills if x["tipo"] == kind]
        best = (0, 0)
        for k in sk:
            for i in first:
                off = k - i
                hit = sum(1 for q in sk if q - off in first)
                if hit > best[0] or (hit == best[0] and best[0] and abs(off - 511) < abs(best[1] - 511)):
                    best = (hit, off)
        if best[0]:
            for q in sk:
                if q - best[1] in first:
                    out[q] = first[q - best[1]]
    return out


def iw_skill_ids(who, path=None):
    """{nombre de la hoja IW: ID de su primer nivel} de las tecnicas (Super/Ultimate) del
    personaje. Para escribir en `iw` la clave real (Janemba: Hell Gate 364, Rakshasa's Claw
    367, Spike Hell 583) en vez del w11 del BCM, que en ports de la comunidad puede ser el ID
    de un objeto (884 Senzu Bean, 887 Super Holy Water)."""
    path = path or os.path.join(RESOURCES, NAME_LISTS["iw"])
    if not os.path.isfile(path):
        return {}
    rows = []
    for sh in (1, 3):
        try:
            rr = _xlsx_rows(path, sh)
        except KeyError:
            continue
        head = {}
        for r in rr[:3]:
            for k, v in r.items():
                v = str(v or "").upper()
                if v in ("ID", "TYPE", "USER") or v.startswith("NAME"):
                    head.setdefault(v.split(" ")[0], k)
            if len(head) >= 4:
                break
        for r in rr:
            try:
                i = int(str(r.get(head.get("ID"))).replace(",", ""))
            except ValueError:
                continue
            rows.append((i, _clean(r.get(head.get("NAME")) or ""), str(r.get(head.get("TYPE")) or ""),
                         _clean(r.get(head.get("USER")) or "")))
    users = {_norm(u) for _, _, _, u in rows if u}
    key = _norm(re.sub(r"\((alt colou?r pall?ete|battle damaged[^)]*|halo|default)\)", "", who, flags=re.I))
    key = IW_USER_ALIAS.get(key, key)
    if key not in users:
        cand = [u for u in users if u and (u in key or key in u)]
        key = max(cand, key=len) if cand else key
    out = {}
    for i, n, t, u in sorted(rows):
        if _norm(u) != key or t not in ("Super", "Ultimate") or not n:
            continue
        if re.match(r"(fusion|potara)", n, re.I):
            continue
        out.setdefault(n, i)     # el primer nivel de cada tecnica
    return out


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
    work = work_dir("caps_")
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
    if game == "iw":            # cada tecnica de IW: su capsula, con su nombre, formas y ki
        skills = capsulas.iw_skills(ccm)
        names = iw_skill_names(c.get("nombre_iw") or c.get("nombre", ""), skills, a.lista)
        ids = iw_skill_ids(c.get("nombre_iw") or c.get("nombre", ""), a.lista)
        caps, used = [], {"especial": 0, "definitiva": 0}
        for sk in skills:
            used[sk["tipo"]] += 1
            nm = names.get(sk["clave"]) or {"especial": "Special %d", "definitiva": "Ultimate %d"}[sk["tipo"]] % used[sk["tipo"]]
            # clave real de la hoja de IW (no el w11 del BCM, que en ports de la comunidad puede
            # ser el ID de un objeto: Janemba 884 Senzu Bean / 887 Super Holy Water)
            e = {"nombre": nm, "tipo": sk["tipo"], "iw": ids.get(nm, sk["clave"])}
            if sk["formas"]:
                e["formas"] = [f + 1 for f in range(8) if sk["formas"] >> f & 1]
            if sk["ki"]:
                e["ki"] = min(7, sk["ki"])
            caps.append(e)
            log("  IW %d -> %s (%s%s%s)" % (sk["clave"], nm, sk["tipo"], ", formas %s" % e["formas"] if "formas" in e else "",
                                          ", %d variantes" % sk["variantes"] if sk["variantes"] > 1 else ""))
        return caps
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


# nombre de cada personaje en los rotulos de los equipos (los internos son GOKOU, KULILIN...)
TEAM_NAMES = {0: "Goku", 1: "Kid Goku", 2: "Kid Gohan", 3: "Teen Gohan", 4: "Gohan", 5: "Great Saiyaman",
              6: "Goten", 7: "Vegeta", 8: "Trunks", 9: "Kid Trunks", 10: "Krillin", 11: "Piccolo", 12: "Tien",
              13: "Yamcha", 14: "Hercule", 15: "Videl", 16: "Supreme Kai", 17: "Uub", 18: "Raditz",
              19: "Nappa", 20: "Ginyu", 21: "Recoome", 27: "Frieza", 28: "Android 16", 29: "Android 17",
              30: "Android 18", 32: "Dr. Gero", 33: "Cell", 34: "Majin Buu", 35: "Super Buu", 36: "Kid Buu",
              37: "Dabura", 38: "Cooler", 39: "Bardock", 40: "Broly", 41: "Omega Shenron", 42: "Saibaman",
              43: "Cell Jr."}


def make_team(a):
    """Tag Team: mod fuente de un equipo de 2-4 personajes de B3. Es un personaje nuevo cuyas
    formas son los miembros (forma k = el miembro k en su forma normal): su modelo, sus bocas, su
    moveset y sus golpes solo en su forma (ccm_merge_forms). SELECT = relevo (el runtime elige al
    compañero vivo y guarda la vida de cada uno; ver src/tagteam.cpp)."""
    mods = a.mods or default_mods()
    us = us_dir(a.us)
    members = [int(x) for x in a.miembros][:4]
    if len(members) < 2 or any(m not in IDS or not IDS[m].get("models") or not IDS[m].get("cam")
                               for m in members):
        raise SystemExit("ERROR: equipo de 2 a 4 personajes de B3 con modelo y moveset (IDs 0-43)")
    work = work_dir("equipo_")
    try:
        cmn = Afs(os.path.join(us, "data_cmn.afs"), work)
        anm_by_id = {m: next((x for x in IDS[m].get("anm", []) if 0 <= x < len(cmn.index)), None)
                     for m in members}
        if any(v is None for v in anm_by_id.values()):
            raise SystemExit("ERROR: equipo con miembro sin animacion valida en este AFS")
        mod = a.mod or "equipo_" + "_".join(re.sub(r"[^a-z0-9]", "", IDS[m]["name"].lower()) for m in members)
        d = os.path.join(mods, mod)
        for sub in ("modelos", "bocas", "moveset", "ui"):
            os.makedirs(os.path.join(d, sub), exist_ok=True)
        ccms, cams = [], []
        for k, m in enumerate(members):
            dm = IDS[m]
            open(os.path.join(d, "modelos", "forma%d.amb" % (k + 1)), "wb").write(cmn.entry(dm["models"][0]))
            open(os.path.join(d, "bocas", "forma%d.bin" % (k + 1)), "wb").write(cmn.entry(dm["lips"][0]))
            open(os.path.join(d, "moveset", "anm_forma%d.bin" % (k + 1)), "wb").write(
                cmn.entry(anm_by_id[m]))
            cam = cmn.entry(dm["cam"])
            at = capsulas.ccm_child(cam)
            cams.append((cam, at))
            ccms.append(cam[at[0]:at[0] + at[1]])
        cam0, at0 = cams[0]
        k0 = next(k for k, e in enumerate(capsulas.amb_children(cam0)) if e[0] == at0[0] and e[1] == at0[1])
        open(os.path.join(d, "moveset", "camara.bin"), "wb").write(
            capsulas.amb_rebuild(cam0, {k0: capsulas.ccm_merge_forms(ccms)}))
        # tecnicas de cada uno (sin sus transformaciones: en el equipo cada miembro va en su forma normal)
        tech = []
        for m in members:
            dm = IDS[m]
            tech += [x for x in dm.get("caps", []) if x not in dm.get("form_caps", []) and x not in tech]
        n = len(members)
        nombre = a.nombre or " & ".join(TEAM_NAMES.get(m, IDS[m]["name"].title()) for m in members)
        toml = ['[personaje]', 'nombre = %s' % json.dumps(nombre),
                'donante = %d' % members[0],
                'equipo = %s' % toml_list(members),
                'modelos = %s' % json.dumps(["modelos/forma%d.amb" % (k + 1) for k in range(n)]),
                'modelos_por_traje = %d' % n,
                'bocas = %s' % json.dumps(["bocas/forma%d.bin" % (k + 1) for k in range(n)]),
                'formas = %d' % n,
                'formas_de = %s' % toml_list([v for m in members for v in (m, 0)]),
                'moveset = %s' % json.dumps(["moveset/anm_forma%d.bin" % (k + 1) for k in range(n)]),
                'camara = "moveset/camara.bin"',
                'capsulas_nativas = %s' % toml_list(tech[:7]),
                'combos_tecnica = false',
                'altura = false', '']
        open(os.path.join(d, "personaje.toml"), "w", encoding="utf-8").write("\n".join(toml))
        log("equipo creado: mods/%s (%s); se monta al pulsar JUGAR o Reconstruir ahora" % (mod, nombre))
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


def caps_cmd(a):
    """Lista / anade / quita / cambia las capsulas propias de un personaje (personaje.toml)."""
    mods = a.mods or default_mods()
    path = os.path.join(mods, a.mod, "personaje.toml")
    caps = read_caps(path)
    changed = False
    if a.importar:
        c = toml_file(path).get("personaje", {})
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
    q = sub.add_parser("equipo", help="Tag Team: personaje nuevo con 2-4 miembros (formas) y relevo P+K+G")
    q.add_argument("--mods")
    q.add_argument("--us")
    q.add_argument("--miembros", nargs="+", required=True, help="IDs de B3 (0 Goku, 7 Vegeta...)")
    q.add_argument("--nombre")
    q.add_argument("--mod")
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
    try:
        return {"construir": build, "nuevo": new_char, "equipo": make_team, "estado": status, "vista": preview,
                "capsulas": caps_cmd}[a.cmd](a)
    except VistaError as e:
        raise SystemExit(str(e)) from None


if __name__ == "__main__":
    sys.exit(main())
