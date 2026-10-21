#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""voces.py - Voces de combate de los personajes nuevos (B3 HD US).

Budokai 3 (HD y PS2) elige las voces de combate con dos tablas por ID de personaje
(RE 2026-10-04):

    0x823280B0  u32[122]  -> bloque de s32 (uno por "situacion": golpe, dano, victoria...)
    0x82328298  s32[122]  numero de situaciones (50; Goku 56)

Cada valor del bloque es el indice de un ADX de adx_usa.afs / adx_jpn.afs (los dos tienen
los mismos indices) o -1 si esa situacion no tiene voz. Los IDs 44-63 tienen 0 situaciones y
los recortados de fabrica (22-26, 31) un bloque todo a -1: por eso no hablan.

Infinite World usa el MISMO esquema (tabla de punteros + numeros en su SLUS) y las mismas
situaciones en el mismo orden (comprobado: los ADX de Krillin de IW son identicos a los de B3
y ocupan las mismas posiciones; IW solo deja vacias las 40-44). Las voces de un port de IW se
copian como ADX nuevos (indices >= los del AFS) y el bloque propio va en roster.toml:
`voces = [50 indices]`.

En personaje.toml (fuente):
    voces = "iw:JANENBA"     # las de un personaje de Infinite World (nombre interno o ID)
    voces = "sb2:GHF"        # las de un personaje de Shin Budokai (sb1/sb2 + codigo; sb_voces.py)
    voces = "t3:BABY_V"      # las de Budokai Tenkaichi 3 (su ISO: DBZ3_T3_ISO o ps2_games), el clip
                             # de duracion mas parecida en cada situacion del donante; "#1"/"#2"
                             # fija el juego de bancos ([H] 1 = japones, 2 = ingles)
    voces = "b3:9"           # las de un personaje de Budokai 3 (su ID)
    voces = "wav:voces"      # frases propias: <mod>/voces/NN*.wav (NN = situacion 0-49, p. ej. 07_victoria.wav);
                             # las que falten, las del donante
    voces = "donante"        # las del donante (por defecto)
    voces = "ninguna"        # sin voz
    voces = "iw:JANENBA|ninguna"   # alternativas en orden: la primera que se pueda leer
"""
import os
import struct

import iso

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
SLOTS = 50                         # situaciones de un bloque de B3
VOICE_AFS = ("adx_usa.afs", "adx_jpn.afs")


def iw_source():
    """Infinite World en ps2_games (DBZ3_PS2_GAMES): '.ruta' elegido por el jugador, carpeta extraida o ISO, de
    cualquier region. Misma resolucion que importar.iw_dir e iso.find_game (B-02/B-03, studies 2)."""
    return iso.find_game("iw", carpetas=True) or os.path.join(iso.ps2_root(), "Infinite World (USA)")


class GameFiles:
    """Ficheros de un juego de PS2 en su carpeta extraida o dentro de su ISO (sin extraer)."""

    def __init__(self, src):
        self.src = src
        self.iso = None
        if os.path.isfile(src):
            import iso  # noqa: PLC0415
            self.iso = iso.Iso(src)

    def find(self, name):
        if self.iso:
            return self.iso.find(name)
        return find_file(self.src, name)

    def names(self):
        if self.iso:
            return [p.rsplit("/", 1)[-1] for p in self.iso.files()]
        out = []
        for base in (self.src, os.path.join(self.src, "USR"), os.path.join(self.src, "usr")):
            if os.path.isdir(base):
                out += os.listdir(base)
        return out

    def elf(self):
        """Ruta del ejecutable (SYSTEM.CNF), sea SLUS (USA) o SLES (Europa)."""
        import iso  # noqa: PLC0415
        return self.iso.boot_elf() if self.iso else iso.folder_elf(self.src)

    def read(self, key, offset=0, n=None):
        if self.iso:
            return self.iso.read(key, offset, n)
        with open(key, "rb") as f:
            f.seek(offset)
            return f.read() if n is None else f.read(n)


def afs_index(path):
    with open(path, "rb") as f:
        head = f.read(8)
        if head[:3] != b"AFS":
            raise ValueError("no es un AFS: %s" % path)
        n = struct.unpack("<I", head[4:8])[0]
        t = struct.unpack("<%dI" % (2 * n), f.read(8 * n))
    return [(t[2 * i], t[2 * i + 1]) for i in range(n)]


def afs_read(path, index, n):
    a, s = index[n]
    with open(path, "rb") as f:
        f.seek(a)
        return f.read(s)


def find_file(folder, name):
    """Fichero de un juego de PS2 sin distinguir mayusculas (USR/ADX_USA.AFS...)."""
    for base in (folder, os.path.join(folder, "USR"), os.path.join(folder, "usr")):
        if not os.path.isdir(base):
            continue
        for fn in os.listdir(base):
            if fn.lower() == name.lower():
                return os.path.join(base, fn)
    return None


def find_elf(folder):
    for fn in os.listdir(folder):
        if fn[:4].upper() in ("SLUS", "SLES", "SLPS", "SCUS", "SCES") and "." in fn:
            return os.path.join(folder, fn)
    return None


class IwVoices:
    """Tabla de voces de Infinite World (SLUS de PS2, little-endian)."""

    def __init__(self, folder=None):
        self.folder = folder or iw_source()
        self.files = GameFiles(self.folder)
        elf = self.files.elf()
        if not elf:
            raise FileNotFoundError("no encuentro el ejecutable de Infinite World en %s" % self.folder)
        self.elf = self.files.read(elf)
        ph = struct.unpack_from("<I", self.elf, 0x1C)[0]
        _, off, va = struct.unpack_from("<3I", self.elf, ph)[:3]
        self._seg = (off, va)
        self.names = self._names()
        self.ptr_off, self.n = self._table()

    def v2o(self, v):
        return v - self._seg[1] + self._seg[0]

    def _names(self):
        """Registros de 32 B (nombre interno, u32 ID) -> {nombre: ID}."""
        d = self.elf
        at = d.find(b"KULILIN\x00")
        while at >= 0 and struct.unpack_from("<I", d, at + 28)[0] != 10:
            at = d.find(b"KULILIN\x00", at + 1)
        if at < 0:
            raise ValueError("tabla de nombres de IW no encontrada")
        out = {}
        for step in (-32, 32):
            o = at
            empty = 0
            while 0 <= o < len(d) - 32 and empty < 12:
                nm = d[o:o + 28].split(b"\x00")[0]
                cid = struct.unpack_from("<I", d, o + 28)[0]
                if nm and all(32 <= ch < 127 for ch in nm) and cid < 128:
                    out.setdefault(nm.decode("ascii"), cid)
                    empty = 0
                elif nm:
                    break
                else:
                    empty += 1
                o += step
        return out

    def _table(self):
        """Tabla de punteros a bloques + tabla de numeros justo detras (+2 palabras)."""
        d = self.elf
        lo, hi = self._seg[1], self._seg[1] + len(d)
        n = 110
        for p in range(0, len(d) - 4 * (2 * n + 2), 4):
            k0 = struct.unpack_from("<I", d, p + 40)[0]          # ID 10 = Krillin
            if not (lo <= k0 < hi):
                continue
            ptrs = struct.unpack_from("<%dI" % n, d, p)
            cnts = struct.unpack_from("<%di" % n, d, p + 4 * (n + 2))
            if cnts[10] != 48 or cnts[0] < 50:
                continue
            # (un ID sin bloque puede tener numero: el 72 de IW USA)
            if all(0 <= c <= 64 and (q == 0 or (c > 0 and lo <= q < hi))
                   for q, c in zip(ptrs, cnts)):
                return p, n
        raise ValueError("tabla de voces de IW no encontrada")

    def char_id(self, who):
        if isinstance(who, int) or str(who).isdigit():
            return int(who)
        key = str(who).strip().upper()
        for nm, cid in self.names.items():
            if nm.upper() == key:
                return cid
        raise KeyError("personaje de IW desconocido: %s (hay: %s)" % (who, ", ".join(sorted(self.names))))

    def slots(self, who):
        """-> lista de SLOTS indices ADX de IW (-1 = sin voz) en el orden de B3."""
        cid = self.char_id(who)
        if not 0 <= cid < self.n:
            raise KeyError("ID de IW fuera de la tabla: %d" % cid)
        d = self.elf
        ptr = struct.unpack_from("<I", d, self.ptr_off + 4 * cid)[0]
        cnt = struct.unpack_from("<i", d, self.ptr_off + 4 * (self.n + 2 + cid))[0]
        if not ptr or cnt <= 0:
            return []
        blk = list(struct.unpack_from("<%di" % cnt, d, self.v2o(ptr)))
        out = blk[:SLOTS] + [-1] * (SLOTS - min(cnt, SLOTS))
        return [v if v >= 0 else -1 for v in out]

    def adx(self, lang, idx):
        """ADX de IW (lang = 'usa' | 'jpn'). 'usa' = el banco en ingles (ADX_USA en la americana;
        en otras regiones el ADX_*.AFS que no sea JPN ni CMN)."""
        path = self.files.find("ADX_%s.AFS" % lang.upper())
        if not path and lang.lower() != "jpn":
            other = sorted(n for n in self.files.names() if n.upper().startswith("ADX_") and
                           n.upper().endswith(".AFS") and n.upper()[4:7] not in ("JPN", "CMN"))
            path = self.files.find(other[0]) if other else None
        if not path:
            raise FileNotFoundError("falta ADX_%s.AFS de Infinite World" % lang.upper())
        if not hasattr(self, "_idx"):
            self._idx = {}
        if path not in self._idx:
            head = self.files.read(path, 0, 8)
            n = struct.unpack("<I", head[4:8])[0]
            t = struct.unpack("<%dI" % (2 * n), self.files.read(path, 8, 8 * n))
            self._idx[path] = [(t[2 * i], t[2 * i + 1]) for i in range(n)]
        a, sz = self._idx[path][idx]
        return self.files.read(path, a, sz)


B3_PS2_DIR = os.path.join(ROOT, "ps2_games", "Budokai 3 Greatest Hits (USA)")


def b3_block(cid, folder=B3_PS2_DIR):
    """Bloque de voces de un ID de B3 HD leido del SLUS de B3 de PS2 (misma tabla y mismos ADX;
    en PS2 el indice es ID - 2 desde el 3 y el 0 es Goku). None si no hay SLUS o es el 1/2."""
    elf = find_elf(folder) if os.path.isdir(folder) else None
    if not elf or cid in (1, 2):
        return None
    d = open(elf, "rb").read()
    ph = struct.unpack_from("<I", d, 0x1C)[0]
    _, off, va = struct.unpack_from("<3I", d, ph)[:3]
    n = 122
    for p in range(0, len(d) - 8 * n, 4):
        cnt = struct.unpack_from("<%di" % n, d, p + 4 * n)
        if cnt[0] != 56 or cnt[8] != 50:          # Goku 56 situaciones, Krillin (PS2 8) 50
            continue
        ptr = struct.unpack_from("<%dI" % n, d, p)
        if all(0 <= c <= 64 and (q == 0 or va <= q < va + len(d)) for q, c in zip(ptr, cnt)):
            k = cid if cid == 0 else cid - 2
            if not ptr[k] or cnt[k] <= 0:
                return [-1] * SLOTS
            blk = list(struct.unpack_from("<%di" % cnt[k], d, ptr[k] - va + off))[:SLOTS]
            return [v if v > 0 else -1 for v in blk] + [-1] * (SLOTS - len(blk))
    return None


T3_AFS = "/DATA/PZS3EU2.AFS"       # voces de Budokai Tenkaichi 3 (ADX mono 24 kHz)


def t3_source():
    """ISO de Tenkaichi 3 (PAL): DBZ3_T3_ISO o un *.iso de ps2_games con 'tenkaichi' en el nombre."""
    import glob  # noqa: PLC0415
    p = os.environ.get("DBZ3_T3_ISO")
    if p and os.path.isfile(p):
        return p
    return next((q for q in sorted(glob.glob(os.path.join(ROOT, "ps2_games", "*.iso")))
                 if "tenkaichi" in os.path.basename(q).lower()), None)


class T3Voices:
    """Voces de Budokai Tenkaichi 3: PZS3EU2.AFS por nombre de banco (Goku_0..Goku_7, Baby_V...).

    Cada personaje tiene 1..8 bancos de 100 en 100 clips con el mismo nombre; hay dos juegos
    completos de bancos (32774..48873 y 48874..64973) con los mismos nombres [H]: el 1.º
    japones y el 2.º ingles (el 1.º dura de media 1,76 s y el 2.º 1,46 s). Mapa y conteos:
    studies 4\\ENTREGAS\\08_tenkaichi3\\datos\\mapa_voces_t3.json. Los grupos VIC-JP-B-/
    VIC-US-B- son el doblaje de la historia (indice = 300*banco + sub; ver
    mapa_clip_frase_t3.json)."""

    JUEGOS = {1: (32774, 48873), 2: (48874, 64973)}
    VACIO = 202                    # ficha ADX vacia (marcador)

    def __init__(self, src=None):
        import iso  # noqa: PLC0415  (mod center hd, como GameFiles)
        self.src = src or t3_source()
        if not self.src:
            raise FileNotFoundError("sin ISO de Tenkaichi 3 (define DBZ3_T3_ISO o ponla en ps2_games)")
        self.iso = iso.Iso(self.src)
        head = self.iso.read(T3_AFS, 0, 8)
        if head[:3] != b"AFS":
            raise ValueError("no es un AFS: %s" % T3_AFS)
        n = struct.unpack("<I", head[4:8])[0]
        tab = self.iso.read(T3_AFS, 8, (n + 1) * 8)
        self.ent = [struct.unpack_from("<II", tab, 8 * i) for i in range(n)]
        no, ns = struct.unpack_from("<II", tab, 8 * n)
        raw = self.iso.read(T3_AFS, no, ns)
        self.nombres = [raw[48 * i:48 * i + 32].split(b"\x00")[0].decode("ascii", "replace")
                        for i in range(n)]

    def adx(self, i):
        """ADX completo de la entrada i."""
        return self.iso.read(T3_AFS, self.ent[i][0], self.ent[i][1])

    def segundos(self, i):
        """Duracion del clip i (cabecera ADX); 0.0 si esta vacio."""
        if self.ent[i][1] <= self.VACIO:
            return 0.0
        d = self.iso.read(T3_AFS, self.ent[i][0], 18)
        if d[:1] != b"\x80":
            return 0.0
        tasa, muestras = struct.unpack(">II", d[8:16])
        return muestras / tasa if tasa else 0.0

    def clips(self, who, lang=None, juego=None):
        """Indices AFS con audio del personaje `who` (clave del mapa: GOKU, GT_GOK, BABY_V...).
        En 'usa' mira el juego 2 y en 'jpn' el 1 ([H]); `juego` 1|2 lo fija a mano y el sufijo
        "#1"/"#2" de `who` tambien (t3:BABY_V#2)."""
        import re  # noqa: PLC0415
        who, _, cual = who.partition("#")
        j = int(cual) if cual in ("1", "2") else (juego or (2 if (lang or "usa").lower() != "jpn" else 1))
        a, b = self.JUEGOS[j]
        clave = re.sub(r"_\d+$", "", who.strip()).upper()
        return [i for i in range(a, b + 1)
                if self.ent[i][1] > self.VACIO and
                re.sub(r"_\d+$", "", self.nombres[i]).upper() == clave]

    def clips_tiempos(self, who, lang=None, juego=None):
        """[(indice AFS, segundos)] de los clips con audio del personaje."""
        return [(i, d) for i, d in ((i, self.segundos(i)) for i in self.clips(who, lang, juego)) if d > 0]


def donor_durations(donor, writer):
    """Segundos de cada ADX del bloque de voces del donante (us/adx_usa.afs), por situacion."""
    base = b3_block(donor) if donor is not None else None
    us_dir = getattr(writer, "us_dir", None)
    if not base or not us_dir:
        return None
    try:
        ruta = os.path.join(us_dir, VOICE_AFS[0])
        idx = afs_index(ruta)
        out = []
        with open(ruta, "rb") as f:
            for v in base:
                if not (0 <= v < len(idx)):
                    out.append(0.0)
                    continue
                f.seek(idx[v][0])
                d = f.read(18)
                if d[:1] != b"\x80":
                    out.append(0.0)
                    continue
                tasa, muestras = struct.unpack(">II", d[8:16])
                out.append(muestras / tasa if tasa else 0.0)
        return out
    except OSError:
        return None


class VoiceWriter:
    """Anade ADX a adx_usa/adx_jpn del mod generado (entradas nuevas, sin LZX)."""

    def __init__(self, us_dir, out_dir):
        self.us_dir = us_dir
        self.out_dir = out_dir
        self.next = max(len(afs_index(os.path.join(us_dir, a))) for a in VOICE_AFS)
        self.first = self.next

    def add(self, usa, jpn):
        n = self.next
        for afs, data in zip(VOICE_AFS, (usa, jpn or usa)):
            d = os.path.join(self.out_dir, "us", afs, str(n))
            os.makedirs(d, exist_ok=True)
            open(os.path.join(d, "voz.adx"), "wb").write(data)
        self.next += 1
        return n


def leer_wav(path):
    """WAV PCM de 8/16/24/32 bits (mono o estereo, cualquier frecuencia) -> int16 mono a 24 kHz (los ADX de B3)."""
    import wave  # noqa: PLC0415
    import numpy as np  # noqa: PLC0415
    import gritos  # noqa: PLC0415
    import sb_voces  # noqa: PLC0415
    with wave.open(path, "rb") as w:
        sw, ch, rate = w.getsampwidth(), w.getnchannels(), w.getframerate()
        raw = w.readframes(w.getnframes())
    if sw == 1:
        x = (np.frombuffer(raw, np.uint8).astype(np.float64) - 128) * 256
    elif sw == 2:
        x = np.frombuffer(raw, "<i2").astype(np.float64)
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        x = np.where(v & 0x800000, v - 0x1000000, v).astype(np.float64) / 256
    elif sw == 4:
        x = np.frombuffer(raw, "<i4").astype(np.float64) / 65536
    else:
        raise ValueError("%s: %d bytes por muestra" % (path, sw))
    x = np.clip(np.round(x.reshape(-1, ch).mean(axis=1)), -32768, 32767).astype(np.int16)
    return gritos.resample(x, rate, sb_voces.ADX_RATE)


def wav_block(folder, writer, donor=None, log=print):
    """Bloque de 50 con las frases de `folder` (NN*.wav: NN = situacion) y el resto las del donante (o mudas)."""
    import re  # noqa: PLC0415
    import sb_voces  # noqa: PLC0415
    base = b3_block(donor) if donor is not None else None
    block = list(base) if base else [-1] * SLOTS
    propias = 0
    for f in sorted(os.listdir(folder)):
        m = re.match(r"(\d+)", f)
        if not f.lower().endswith(".wav") or not m or int(m.group(1)) >= SLOTS:
            if f.lower().endswith(".wav"):
                log("   aviso: %s no empieza por un numero de frase (0-%d): no se usa" % (f, SLOTS - 1))
            continue
        try:
            adx = sb_voces.adx_encode(leer_wav(os.path.join(folder, f)))
        except Exception as e:  # noqa: BLE001
            log("   aviso: %s no se puede leer (%s): no se usa" % (f, e))
            continue
        block[int(m.group(1))] = writer.add(adx, adx)
        propias += 1
    if not propias:
        raise ValueError("sin frases .wav (NN_nombre.wav) en %s" % folder)
    return block, propias


def resolve(spec, writer, iw=None, log=print, donor=None, base=None):
    """voces de personaje.toml -> (lineas de roster.toml, nota). `donor` (ID) solo hace falta
    para las de Shin Budokai: las situaciones sin frase propia se quedan con las suyas."""
    spec = (spec or "donante").strip() if isinstance(spec, str) else spec
    if isinstance(spec, str) and "|" in spec:
        # "iw:JANENBA|ninguna": la primera alternativa que se pueda leer (sin Infinite World, mejor mudo que el donante)
        err = None
        for alt in spec.split("|"):
            try:
                lines, note = resolve(alt, writer, iw, log, donor, base)
            except Exception as e:  # noqa: BLE001
                err = e
                continue
            if lines or alt.strip().lower() in ("donante", "", "auto"):
                return lines, note
        raise ValueError("ninguna alternativa de voces sirve (%s): %s" % (spec, err))
    if isinstance(spec, list):
        vals = [int(v) for v in spec][:SLOTS]
        return ["voces = [%s]" % ", ".join(map(str, vals))], "voces propias (%d)" % sum(v >= 0 for v in vals)
    low = spec.lower()
    if low in ("donante", "", "auto"):
        return [], "voces del donante"
    if low in ("ninguna", "sin voz", "no"):
        return ["voces = [%s]" % ", ".join(["-1"] * SLOTS)], "sin voz"
    game, _, who = spec.partition(":")
    game = game.strip().lower()
    if game == "b3":
        return ["voces_de = %d" % int(who)], "voces de Budokai 3 (ID %s)" % who
    if game == "wav":   # `base`: la carpeta del mod (roster_build)
        folder = who.strip() if os.path.isabs(who.strip()) else os.path.join(base or os.getcwd(), who.strip())
        block, propias = wav_block(folder, writer, donor, log)
        return (["voces = [%s]" % ", ".join(map(str, block))],
                "voces propias de WAV: %d frases (el resto, del donante)" % propias)
    if game == "iw":
        iw = iw or IwVoices()
        src = iw.slots(who)
        if not src:
            return [], "aviso: %s no tiene voces en Infinite World (se usan las del donante)" % who
        cache, out = {}, []
        for v in src:
            if v < 0:
                out.append(-1)
                continue
            if v not in cache:
                cache[v] = writer.add(iw.adx("usa", v), iw.adx("jpn", v))
            out.append(cache[v])
        return (["voces = [%s]" % ", ".join(map(str, out))],
                "voces de Infinite World (%s): %d grabaciones" % (who, len(cache)))
    if game == "t3":
        import math  # noqa: PLC0415
        t3 = T3Voices()
        who2, _, cual = who.partition("#")
        if not who2.strip():
            raise ValueError("voces t3: sin personaje (t3:BABY_V; #1/#2 fija el juego de bancos)")
        juegos = {"usa": int(cual) if cual in ("1", "2") else 2,
                  "jpn": int(cual) if cual in ("1", "2") else 1}
        base = b3_block(donor) if donor is not None else None
        durs = donor_durations(donor, writer)
        if base is None or not durs:
            raise ValueError("t3: hace falta el bloque de voces del donante y sus ADX "
                             "(ps2_games/%s)" % os.path.basename(B3_PS2_DIR))
        pools = {lk: t3.clips_tiempos(who2, lk, juegos[lk]) for lk in ("usa", "jpn")}
        if not pools["usa"] and not pools["jpn"]:
            raise KeyError("personaje de Tenkaichi 3 desconocido: %s" % who2)
        usos, ultimo, n = {}, -1, 0
        block = [-1] * SLOTS
        for s, v in enumerate(base):
            if v < 0 or s >= len(durs) or durs[s] <= 0.0:
                continue
            def coste(cand, ln=durs[s]):
                i, d = cand
                return (abs(math.log((d + 0.05) / (ln + 0.05))) + 0.5 * usos.get(i, 0)
                        + (1.5 if i == ultimo else 0))
            elegir = {lk: min(pool, key=coste)[0] for lk, pool in pools.items() if pool}
            i_usa = elegir.get("usa", elegir.get("jpn"))
            i_jpn = elegir.get("jpn", elegir.get("usa"))
            usos[i_usa] = usos.get(i_usa, 0) + 1
            ultimo = i_usa
            block[s] = writer.add(t3.adx(i_usa), t3.adx(i_jpn))
            n += 1
        return (["voces = [%s]" % ", ".join(map(str, block))],
                "voces de Tenkaichi 3 (%s; juego %d en usa, %d en jpn): %d frases por duracion"
                % (who2, juegos["usa"], juegos["jpn"], n))
    if game in ("sb1", "sb2"):
        import sb_voces  # noqa: PLC0415
        block, used = sb_voces.voice_block(spec, writer, b3_block(donor) if donor is not None else None, log)
        own = sum(1 for _, u in used if "donante" not in u and "sin voz" not in u)
        return (["voces = [%s]" % ", ".join(map(str, block))],
                "voces de Shin Budokai (%s): %d frases propias, %d del donante" % (who, own, len(used) - own))
    raise ValueError("voces desconocidas: %r (iw:NOMBRE, b3:ID, sb2:COD, wav:CARPETA, donante, ninguna)" % spec)


def prueba_wav():
    """python voces.py --prueba-wav: dos frases de 16 y 24 bits -> ADX que se decodifica a lo mismo."""
    import tempfile  # noqa: PLC0415
    import wave  # noqa: PLC0415
    import numpy as np  # noqa: PLC0415
    import gritos  # noqa: PLC0415
    d = tempfile.mkdtemp(prefix="voces_wav_")
    t = np.arange(16000) / 16000.0
    tono = (np.sin(2 * np.pi * 440 * t) * 8000).astype(np.int16)
    with wave.open(os.path.join(d, "07_victoria.wav"), "wb") as w:
        w.setnchannels(1), w.setsampwidth(2), w.setframerate(16000), w.writeframes(tono.tobytes())
    v24 = (tono.astype(np.int32) << 8).astype("<i4").tobytes()
    with wave.open(os.path.join(d, "12.wav"), "wb") as w:
        w.setnchannels(1), w.setsampwidth(3), w.setframerate(16000)
        w.writeframes(b"".join(v24[i:i + 3] for i in range(0, len(v24), 4)))
    open(os.path.join(d, "notas.wav"), "wb").close()   # sin numero: aviso y fuera

    class Escritor:
        datos = []

        def add(self, usa, jpn):
            self.datos.append(usa)
            return 9000 + len(self.datos) - 1
    e = Escritor()
    lines, note = resolve("wav:" + d, e, donor=None)
    block = eval(lines[0].split("=", 1)[1])
    assert block[7] == 9000 and block[12] == 9001 and block.count(-1) == SLOTS - 2, block
    for data in e.datos:
        y, _ = gritos.adx_decode(data)
        assert abs(len(y) - 24000) < 64 and np.abs(y.astype(np.float64)).max() > 4000
    # alternativas: una fuente que no sirve (nombre que IW no tiene, o sin IW) pasa a la siguiente, no al donante
    assert resolve("iw:NO_EXISTE|ninguna", e)[1] == "sin voz"
    print("prueba OK:", note)


if __name__ == "__main__":
    import sys
    if "--prueba-wav" in sys.argv:
        prueba_wav()
        sys.exit(0)
    if "--prueba-t3" in sys.argv:
        t3 = T3Voices()
        print("T3 %s: BABY_V tiene %d clips con audio en el juego 1 y %d en el 2"
              % (t3.src, len(t3.clips("BABY_V", juego=1)), len(t3.clips("BABY_V", juego=2))))
        sys.exit(0)
    iw = IwVoices(sys.argv[1] if len(sys.argv) > 1 else None)
    print("tabla en 0x%X, %d IDs; %d nombres" % (iw.ptr_off, iw.n, len(iw.names)))
    for nm, cid in sorted(iw.names.items(), key=lambda x: x[1]):
        s = iw.slots(cid)
        print("%3d %-24s %2d voces" % (cid, nm, sum(v >= 0 for v in s)))
