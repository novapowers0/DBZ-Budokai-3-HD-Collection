#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""sb_voces.py - Voces de Shin Budokai (PSP: SB1 y SB2/Another Road) para los ports a B3 HD.

Se leen las ISOs de PSP en su sitio (iso.py, sin extraer):

    data_btl_voice_us.afs / _jp (SB1: data_btl_voice.afs = japones), AFS con tabla de nombres
      BC<XXX>SND.amb   #AMB [PPHD (cabecera), cuerpo PS-ADPCM]: la voz de combate
                       PPTN = "tonos" de 0x60 B (u32 +0 = ID de VAG; PPVA +0x10/+0x14 = primer y
                       ultimo ID), PPVA = fichas de 16 B (desplazamiento, frecuencia, tamano, -1).
                       El numero de TONO es la ranura semantica (igual en todos los personajes y en
                       los dos idiomas); el orden de los VAG cambia entre idiomas.
      ZP<n><XXX>A0     4 frases RIFF ATRAC3plus (ffmpeg las decodifica)
    data_sys_voice_us.afs / _jp (solo SB2): ZP<n>_<XXX>_1nn.at3, 15 frases por personaje:
      101-102 seleccion, 103-109 empezar, 110-114 victoria, 115-121 derrota.

Tonos de SB (RE 2026-10-06, transcritos con whisper en 6 personajes, US y JP):
    0-7 golpe recibido, 8 golpe fuerte, 9 vacio, 10-19 gritos de ataque, 20-25 esquiva/frases
    cortas, 26 carga de ki, 27-29 gritos fuertes, 30-31 sorpresa, 32-33 empezar, 34-35 victoria,
    36 derrota, 37-38 nombre de tecnica + "ha", 42 grito de tecnica, 47-48 / 56-57 frases de
    tecnicas, 55 burla. Los efectos (39-41, 43-45, 49-54, 58-64 en casi todos) son iguales en
    ingles y en japones: asi se distinguen de las voces.

En B3 HD (gritos.py): huecos 0-17 golpe recibido (motor), 18-27 gritos de ataque (k3 del
moveset 0x12-0x1b), 28-36 esquiva y frases (motor), 37-40 / 42 / 44 / 54 k3 del moveset
(39 carga de ki, 41 TRANSFORMACION: la linea k3 0x29 de los codigos 0x2E0/0x3E0), 43/45/50/51
efectos del donante. La tabla de voces (voces.py, 50 situaciones con ADX) lleva las frases.

En personaje.toml:
    voces = "sb2:GHF"      # frases (tabla de voces) del personaje GHF de Another Road
    gritos = "sb2:GHF"     # banco de gritos (por defecto, la fuente de `voces`)

Uso:
    python sb_voces.py --test                                   autocomprobacion (sin ISO)
    python sb_voces.py --lista sb2                              personajes con voz
    python sb_voces.py sb2:GHF --donante 4 --salida DIR         banco, ADX, WAV y tabla
"""
import argparse
import os
import shutil
import struct
import subprocess
import tempfile
import wave

import numpy as np

import gritos
import iso as isomod

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
import iso as _iso  # noqa: E402
# la ISO de PSP de cualquier region (ruta completa; join con ps2_games la deja igual)
ISOS = {"sb1": _iso.find_game("sb1") or "Dragonball Z Shin Budokai (USA).iso",
        "sb2": _iso.find_game("sb2") or "Dragonball Z Shin Budokai Another Road (USA).iso"}
BTL = {"sb1": {"usa": "data_btl_voice_us.afs", "jpn": "data_btl_voice.afs"},
       "sb2": {"usa": "data_btl_voice_us.afs", "jpn": "data_btl_voice_jp.afs"}}
SYS = {"sb2": {"usa": "data_sys_voice_us.afs", "jpn": "data_sys_voice_jp.afs"}}
ADX_RATE = 24000
FREE_MAX = 25600               # huecos libres del donante (46-49): hasta 1.6 s

# hueco de B3 -> tonos de SB candidatos (el primero que exista y sea voz). None = el del donante.
SLOT_TONES = (
    [0], [1], [2], [3], [4], [5], [6], [7],                          # 0-7 golpe recibido
    [8], [28], [29], [2, 8], [27, 28], [8, 29], [28, 8], [31, 30], [29, 28], [8, 28],  # 8-17 fuerte
    [10], [11], [12], [13], [14], [15], [16], [17], [18], [19],      # 18-27 gritos de ataque
    [20], [23], [21], [25, 20],                                      # 28-31 esquiva / teletransporte
    [21, 20], [22], [55, 22], [24, 18], [17, 24],                    # 32-36 frases de esquiva/bloqueo
    [38, 14], [25], [26], [27], [15, 28],                            # 37, 38, 39 carga, 40, 41 transf.
    [37], None, [42], None,                                          # 42 tecnica, 43 efecto, 44, 45 efecto
    [56], [57], None, None,                                          # 46-47 tecnica ("Kamehame" + "HA", si libres);
    None, None, None, None,                                          # 48-49 (0x30/0x31) no los usa ningun nativo; 50-53 efectos
    [55, 36],                                                        # 54 burla
)
CONCAT = {41}                  # transformacion: "FULL POWER!" + grito, seguidos
FREE = {46, 47}            # solo k3 <= 0x2f: los nativos nunca piden 0x30/0x31        # solo si el donante los tiene vacios

# k3 (clase 3 de las lineas AP tipo 7) de un moveset de SB -> hueco del banco de B3 con ESE tono.
# Solo valores del banco del personaje (< 0x40); los de >= 0x40 son del banco comun.
K3 = dict([(t, t) for t in range(9)] + [(t, t + 8) for t in range(10, 20)] + [
    (20, 28), (21, 30), (22, 33), (23, 29), (24, 35), (25, 38), (26, 39), (27, 40), (28, 9),
    (29, 10), (31, 15), (37, 42), (38, 37), (42, 44), (55, 54), (56, 46), (57, 47)])

# situacion de la tabla de voces (voces.py) -> fuente: ("t", tono) o (bolsa, n). "donante" = la suya.
SITUACIONES = {
    0: ("derrota", -1), 1: "donante", 2: ("t", 30), 3: ("t", 31), 4: ("t", 26), 5: ("t", 23),
    6: ("t", 55), 7: ("t", 22), 8: ("t", 15), 9: ("t", 24),
    20: ("empezar", 0), 21: ("empezar", 1), 22: ("victoria", 0), 23: ("victoria", 1),
    24: ("derrota", 0), 25: ("empezar", 2), 26: ("empezar", 3), 27: ("victoria", 2),
    28: ("victoria", 2), 29: ("victoria", 3), 30: ("empezar", 4), 31: ("victoria", 4),
    40: ("t", 47), 41: ("t", 35), 42: ("t", 32), 43: ("t", 15), 44: ("t", 17),
}
MASK = [0, 1, 2, 3, 4, 20, 21, 22, 23, 24, 25, 26, 28, 40, 41, 42, 44]   # sin donante conocido


# ---------------------------------------------------------------- lectura
class Afs:
    """AFS de PSP dentro de la ISO, con su tabla de nombres (48 B por entrada)."""

    def __init__(self, iso, name):
        path = iso.find_region(name)
        if not path:
            raise FileNotFoundError("falta %s en %s" % (name, iso.path))
        self.f = iso.open(path)
        head = self.f.read(8)
        if head[:3] != b"AFS":
            raise ValueError("no es un AFS: %s" % name)
        n = struct.unpack("<I", head[4:8])[0]
        self.tab = [struct.unpack("<II", self.f.read(8)) for _ in range(n)]
        noff, nsz = struct.unpack("<II", self.f.read(8))
        self.names = [""] * n
        if noff and nsz >= 48 * n:
            self.f.seek(noff)
            blob = self.f.read(48 * n)
            self.names = [blob[48 * i:48 * i + 32].split(b"\0")[0].decode("ascii", "replace") for i in range(n)]

    def entry(self, i):
        o, s = self.tab[i]
        self.f.seek(o)
        return self.f.read(s)

    def find(self, name):
        return [i for i, nm in enumerate(self.names) if nm.upper() == name.upper()]


def pphd(amb):
    """BC<XXX>SND.amb -> (tonos: indice de VAG o -1, VAG: [(int16, frecuencia) | None])."""
    n, tbl = struct.unpack_from("<II", amb, 0x10)
    kids = [struct.unpack_from("<4I", amb, tbl + 16 * k) for k in range(n)]
    hd = amb[kids[0][0]:kids[0][0] + kids[0][1]]
    bd = amb[kids[1][0]:kids[1][0] + kids[1][1]]
    i, j = hd.find(b"PPTN"), hd.find(b"PPVA")
    if i < 0 or j < 0:
        raise ValueError("no es un banco PPHD")
    t0, t1 = struct.unpack_from("<II", hd, i + 0x10)
    base, last = struct.unpack_from("<II", hd, j + 0x10)
    tones = []
    for t in range(t1 - t0 + 1):
        v = struct.unpack_from("<I", hd, i + 0x20 + 0x60 * t)[0]
        tones.append(v - base if base <= v <= last else -1)
    vags = []
    for v in range(last - base + 1):
        off, rate, size, _ = struct.unpack_from("<4I", hd, j + 0x20 + 16 * v)
        ok = off != 0xFFFFFFFF and 0x30 < size and off < len(bd)
        vags.append((gritos.vag_decode(bd, off, min(len(bd), off + size)), rate) if ok else None)
    return tones, vags


def ffmpeg():
    """ffmpeg del sistema o el de easy-whisper (decodifica ATRAC3plus y XMA)."""
    p = shutil.which("ffmpeg")
    alt = os.path.join(os.environ.get("APPDATA", ""), "easy-whisper-electron", "whisper-workspace",
                       "bin", "ffmpeg.exe")
    return p or (alt if os.path.exists(alt) else None)


def ff_pcm(data, rate, suffix=".wav"):
    """Fichero de audio (RIFF ATRAC3plus/XMA...) -> int16 mono a `rate` con ffmpeg."""
    exe = ffmpeg()
    if not exe:
        raise FileNotFoundError("hace falta ffmpeg (ATRAC3plus / XMA)")
    fd, path = tempfile.mkstemp(suffix=suffix)
    with os.fdopen(fd, "wb") as f:
        f.write(data)
    try:
        r = subprocess.run([exe, "-v", "error", "-i", path, "-f", "s16le", "-ac", "1", "-ar", str(rate), "-"],
                           capture_output=True, check=True)
    finally:
        os.remove(path)
    return np.frombuffer(r.stdout, np.int16).copy()


def xma_clip(bank, i):
    """Sonido i (XMA2) de un banco lang_* de B3 -> int16 a 16 kHz (para no mezclar formatos)."""
    start = struct.unpack(">I", bank[0x10:0x14])[0]
    o = gritos.records(bank)[i]
    r = bank[o:o + 0x100]
    size, off = struct.unpack(">II", r[0x14:0x1C])
    f = r[0xBC:0xBC + 52]
    fmt = (struct.pack("<HHIIHHH", 0x166, *struct.unpack(">HIIHH", f[2:16]), 34)
           + struct.pack("<HIIIIIIIBBH", *struct.unpack(">HIIIIIIIBBH", f[18:52])))
    data = bank[start + off:start + off + size]
    riff = (b"RIFF" + struct.pack("<I", 20 + len(fmt) + len(data)) + b"WAVEfmt " + struct.pack("<I", len(fmt))
            + fmt + b"data" + struct.pack("<I", len(data)) + data)
    x = ff_pcm(riff, gritos.RATE)
    return x[:struct.unpack(">I", r[0xE0:0xE4])[0]]


def trim(x, thr=0.02, pad=160):
    """Quita el silencio de los extremos (deja 10 ms)."""
    a = np.abs(x.astype(np.int32))
    if not len(a) or not a.max():
        return x
    k = np.nonzero(a > thr * a.max())[0]
    return x[max(0, k[0] - pad):k[-1] + pad + 1]


def fit_len(x, lim):
    """Corta a `lim` muestras con un fundido corto (el banco no debe crecer: memoria del juego)."""
    if len(x) <= lim:
        return x
    y = x[:lim].astype(np.float64)
    f = min(800, lim // 4)
    y[-f:] *= np.linspace(1, 0, f)
    return y.astype(np.int16)


def snr(x, y):
    x = x.astype(np.float64)
    e = np.sum((x - y) ** 2)
    return 99.0 if e == 0 else float(10 * np.log10(max(np.sum(x ** 2), 1e-9) / e))


def ncc(a, b):
    n = 1 << int(np.ceil(np.log2(len(a) + len(b))))
    a = a.astype(np.float64) - a.mean()
    b = b.astype(np.float64) - b.mean()
    c = np.fft.irfft(np.fft.rfft(a, n) * np.conj(np.fft.rfft(b, n)), n)
    return float(np.abs(c).max() / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-9))


class SbVoices:
    """Voces de un personaje de Shin Budokai ("sb2:GHF", "sb1:GOK")."""

    def __init__(self, spec, iso_path=None):
        game, _, code = str(spec).partition(":")
        self.game, self.code = game.strip().lower(), code.strip().upper()
        if self.game not in ISOS or not self.code:
            raise ValueError("voces de Shin Budokai: sb1:COD o sb2:COD (p. ej. sb2:GHF), no %r" % spec)
        self.iso = isomod.Iso(iso_path or os.path.join(ROOT, "ps2_games", ISOS[self.game]))
        self._afs, self._tones, self._pools = {}, {}, {}
        self._voice = None

    def afs(self, name):
        if name not in self._afs:
            self._afs[name] = Afs(self.iso, name)
        return self._afs[name]

    def tones(self, lang):
        """[int16 a 16 kHz | None] por tono (lang = "usa" | "jpn")."""
        if lang not in self._tones:
            a = self.afs(BTL[self.game][lang])
            k = a.find("BC%sSND.AMB" % self.code)
            if not k:
                raise KeyError("%s no tiene voces en %s (BC%sSND.amb)" % (self.code, self.game, self.code))
            tones, vags = pphd(a.entry(k[0]))
            self._tones[lang] = [None if v < 0 or vags[v] is None else trim(gritos.resample(*vags[v]))
                                 for v in tones]
        return self._tones[lang]

    def voices(self):
        """Tonos que son voz: distintos en ingles y en japones (los efectos son identicos)."""
        if self._voice is None:
            us, jp = self.tones("usa"), self.tones("jpn")
            self._voice = set()
            for t, a in enumerate(us):
                b = jp[t] if t < len(jp) else None
                if a is None:
                    continue
                same = b is not None and abs(len(a) - len(b)) <= 0.02 * len(a) and ncc(a, b) > 0.9
                if not same:
                    self._voice.add(t)
        return self._voice

    def tone(self, lang, t):
        cl = self.tones(lang)
        return cl[t] if 0 <= t < len(cl) and t in self.voices() else None

    def pools(self, lang, rate=ADX_RATE):
        """Frases por categoria (empezar / victoria / derrota) y tonos de voz, a `rate`."""
        if (lang, rate) in self._pools:
            return self._pools[(lang, rate)]
        sysl, zp = {}, []
        if ffmpeg():
            if self.game in SYS:
                try:
                    a = self.afs(SYS[self.game][lang])
                    for i, nm in enumerate(a.names):
                        p = nm.upper().split("_")
                        if len(p) == 3 and p[0][:2] == "ZP" and p[1] == self.code:
                            sysl[int(p[2].split(".")[0])] = trim(ff_pcm(a.entry(i), rate))
                except FileNotFoundError:
                    pass
            a = self.afs(BTL[self.game][lang])
            zp = [trim(ff_pcm(a.entry(i), rate)) for i, nm in enumerate(a.names)
                  if nm.upper().startswith("ZP") and nm.upper()[3:6] == self.code]
        t = {k: gritos.resample(x, gritos.RATE, rate) for k in self.voices()
             for x in [self.tones(lang)[k]] if x is not None}
        sel = lambda lo, hi: [sysl[k] for k in sorted(sysl) if lo <= k <= hi]  # noqa: E731
        out = {"t": t,
               "empezar": sel(103, 109) + [t[k] for k in (32, 33) if k in t],
               "victoria": [t[k] for k in (34,) if k in t] + sel(110, 114) + zp + [t[k] for k in (35,) if k in t],
               "derrota": sel(115, 121) or [t[k] for k in (36,) if k in t]}
        self._pools[(lang, rate)] = out
        return out


def characters(game, iso_path=None):
    a = Afs(isomod.Iso(iso_path or os.path.join(ROOT, "ps2_games", ISOS[game])), BTL[game]["usa"])
    return sorted(nm[2:-7].upper() for nm in a.names if nm.upper().endswith("SND.AMB"))


# ---------------------------------------------------------------- gritos (banco lang_*)
def gritos_sounds(spec, lens, lang, donor_bank=None, report=None):
    """Sonidos para gritos.build_lang: un int16 a 16 kHz por hueco del donante, o None (se
    queda el suyo). Con `donor_bank` y ffmpeg, los que se quedan se recodifican tambien (todo el
    banco en RXADPC, como el de Janemba, sin mezclar formatos)."""
    sb = spec if isinstance(spec, SbVoices) else SbVoices(spec)
    out = []
    for slot, ln in enumerate(lens):
        cand = SLOT_TONES[slot] if slot < len(SLOT_TONES) else None
        free = slot in FREE and ln <= gritos.SILENT_MAX
        clips = [(t, sb.tone(lang, t)) for t in (cand or [])]
        clips = [(t, x) for t, x in clips if x is not None]
        if slot in CONCAT and len(clips) > 1:
            gap = np.zeros(gritos.RATE // 20, np.int16)
            clips = [("+".join(str(t) for t, _ in clips), np.concatenate([clips[0][1], gap, clips[1][1]]))]
        src = None
        if clips and (ln > gritos.SILENT_MAX or free):
            src, x = clips[0]
            x = fit_len(x, FREE_MAX if free else int(ln * 1.1))
        elif ln <= gritos.SILENT_MAX:
            x = np.zeros(max(ln, 1), np.int16)
        else:
            x = None
            if donor_bank is not None and ffmpeg():
                try:
                    x = xma_clip(donor_bank, slot)
                except (OSError, subprocess.CalledProcessError, struct.error):
                    x = None
        out.append(x)
        if report is not None:
            report.append((slot, ln, src, 0 if x is None else len(x)))
    return out


# ---------------------------------------------------------------- tabla de voces (ADX)
def adx_encode(x, rate=ADX_RATE, cut=500):
    """int16 mono -> ADX de CRI como los de B3 (cabecera v4 de 0x24, filtro 500 Hz, tramas de
    18 B = 32 muestras y trama de fin 0x8001)."""
    z = np.cos(2 * np.pi * cut / rate)
    a, b = np.sqrt(2) - z, np.sqrt(2) - 1
    c = (a - np.sqrt((a + b) * (a - b))) / b
    c1, c2 = int(np.floor(c * 8192)), int(np.floor(c * c * -4096))
    n = len(x)
    out = bytearray(struct.pack(">HHBBBBIIHBB", 0x8000, 0x24, 3, 18, 4, 1, rate, n, cut, 4, 0))
    out += bytes(0x22 - len(out)) + b"(c)CRI"
    xs = np.concatenate([x.astype(np.int32), np.zeros((-n) % 32, np.int32)])
    h1 = h2 = 0
    for f in range(0, len(xs), 32):
        blk = xs[f:f + 32]
        # escala: el mayor error de prediccion (con la historia real) entre 7
        p1, p2, peak = h1, h2, 0
        for s in blk:
            peak = max(peak, abs(int(s) - ((c1 * p1 + c2 * p2) >> 12)))
            p2, p1 = p1, int(s)
        scale = max(1, -(-peak // 7))
        nib = []
        for s in blk:
            pred = (c1 * h1 + c2 * h2) >> 12
            q = max(-8, min(7, int(round((int(s) - pred) / scale))))
            v = max(-32768, min(32767, q * scale + pred))
            h2, h1 = h1, v
            nib.append(q & 15)
        out += struct.pack(">H", scale - 1) + bytes((nib[i] << 4) | nib[i + 1] for i in range(0, 32, 2))
    return bytes(out + b"\x80\x01\x00\x0e" + bytes(14))


def voice_block(spec, writer, donor_block=None, log=print):
    """-> bloque de 50 indices ADX (voces.py) con las frases del personaje de SB. Solo se rellenan
    las situaciones que tiene el donante (su moveset y sus cinematicas son las que las piden)."""
    sb = spec if isinstance(spec, SbVoices) else SbVoices(spec)
    if not ffmpeg():
        log("   aviso: sin ffmpeg solo hay frases del banco de combate (no las ATRAC3plus)")
    pools = {lk: sb.pools(lk) for lk in ("usa", "jpn")}
    mask = [i for i, v in enumerate(donor_block or []) if v >= 0] or MASK
    block, used = [-1] * 50, []
    for s in mask:
        if s >= 50:
            continue
        src = SITUACIONES.get(s, "donante")
        clips = {}
        if src != "donante":
            for lk in pools:
                pool = pools[lk][src[0]]
                k = src[1]
                clips[lk] = pool.get(k) if isinstance(pool, dict) else (pool[k] if -len(pool) <= k < len(pool) else None)
        if clips.get("usa") is None:
            block[s] = donor_block[s] if donor_block else -1
            used.append((s, "donante" if donor_block else "sin voz"))
            continue
        adx = {lk: adx_encode(x) for lk, x in clips.items() if x is not None}
        for lk, data in adx.items():      # ida y vuelta: el ADX da la misma frase
            y, _ = gritos.adx_decode(data)
            assert len(y) == len(clips[lk]) and snr(clips[lk], y) > 15, (s, lk)
        block[s] = writer.add(adx["usa"], adx.get("jpn"))
        used.append((s, "%s %s (%.2f s)" % (src[0], src[1], len(clips["usa"]) / ADX_RATE)))
    return block, used


# ---------------------------------------------------------------- herramientas
def write_wav(path, x, rate=gritos.RATE):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(np.asarray(x, "<i2").tobytes())


class FileWriter:
    """VoiceWriter de prueba: deja los ADX en DIR/adx/<idioma>/sNN.adx."""

    def __init__(self, out_dir):
        self.out_dir, self.next, self.files = out_dir, 0, []

    def add(self, usa, jpn):
        n = self.next
        for lk, data in (("usa", usa), ("jpn", jpn or usa)):
            d = os.path.join(self.out_dir, "adx", lk)
            os.makedirs(d, exist_ok=True)
            open(os.path.join(d, "%03d.adx" % n), "wb").write(data)
        self.next += 1
        return n


def selftest():
    # tablas
    assert len(SLOT_TONES) == 55 and CONCAT <= set(range(55)) and FREE <= set(range(55))
    for t, s in K3.items():
        assert SLOT_TONES[s] and SLOT_TONES[s][0] == t, (t, s)
    assert len(set(K3.values())) == len(K3), "dos tonos al mismo hueco"
    # PPHD minimo: 2 tonos -> VAG 1 (un bloque PS-ADPCM con un escalon) y VAG 0 (vacio)
    hd = bytearray(0x200)
    hd[0x40:0x44], hd[0x100:0x104] = b"PPTN", b"PPVA"
    struct.pack_into("<II", hd, 0x50, 0, 1)
    struct.pack_into("<I", hd, 0x60, 11)                       # tono 0 -> VAG 11 - 10 = 1
    struct.pack_into("<I", hd, 0xC0, 10)                       # tono 1 -> VAG 0 (vacio)
    struct.pack_into("<II", hd, 0x110, 10, 11)
    struct.pack_into("<4I", hd, 0x120, 0, 4000, 0x30, 0xFFFFFFFF)
    struct.pack_into("<4I", hd, 0x130, 0x30, 16000, 0x40, 0xFFFFFFFF)
    bd = bytearray(0x70)
    bd[0x30:0x40] = bytes([0x0C, 0x00]) + bytes([0x11] * 14)  # desplazamiento 12, filtro 0, nibble 1
    bd[0x60:0x70] = bytes([0x0C, 0x01]) + bytes(14)
    amb = bytearray(0x40) + hd + bd
    struct.pack_into("<II", amb, 0x10, 2, 0x20)
    struct.pack_into("<4I", amb, 0x20, 0x40, len(hd), 0, 0)
    struct.pack_into("<4I", amb, 0x30, 0x40 + len(hd), len(bd), 0, 0)
    tones, vags = pphd(bytes(amb))
    assert tones == [1, 0] and vags[0] is None and vags[1][1] == 16000
    assert list(vags[1][0][:28]) == [1] * 28, vags[1][0][:4]   # 1 << 12 >> 12
    # ADX: la cabecera de B3 y la ida y vuelta con el decodificador del kit
    t = np.arange(12000)
    sig = (9000 * np.sin(2 * np.pi * 330 * t / ADX_RATE) * np.exp(-t / 6000)).astype(np.int16)
    adx = adx_encode(sig)
    assert adx[:12] == bytes.fromhex("800000240312040100005dc0") and adx[0x22:0x28] == b"(c)CRI"
    assert len(adx) == 0x28 + 18 * (len(sig) // 32 + (len(sig) % 32 > 0) + 1)
    back, rate = gritos.adx_decode(adx)
    snr = 10 * np.log10(np.sum(sig.astype(float) ** 2) / np.sum((sig.astype(float) - back[:len(sig)]) ** 2))
    assert rate == ADX_RATE and len(back) == len(sig) and snr > 20, snr

    # gritos: hueco k3 con su tono, recorte a 1.1x, huecos libres, efectos del donante
    class Fake(SbVoices):
        def __init__(self):
            self._voice = set(range(65)) - {39, 40, 43}
            self._tones = {"usa": [np.full(3000 + 100 * k, k + 1, np.int16) for k in range(65)]}
    lens = [4000] * 55
    lens[46] = 1116
    rep = []
    s = gritos_sounds(Fake(), lens, "usa", report=rep)
    assert all(s[K3[t]][0] == t + 1 for t in K3 if K3[t] != 46), "k3 -> tono"
    assert s[46][0] == 57 and s[48] is None and s[43] is None and s[50] is None
    assert len(s[41]) == 3000 + 1500 + 800 + 3000 + 2800 or len(s[41]) == int(4000 * 1.1)
    assert max(len(x) for x in s if x is not None and len(x) != len(s[46])) <= 4400
    print("sb_voces.py: autocomprobacion OK (tablas, PPHD, ADX %.1f dB, reparto de gritos)" % snr)


def build(spec, donor, out_dir, previews=True, log=print):
    """Todo lo de un personaje en `out_dir`: lang_usa.bin / lang_jpn.bin (entradas #AMB sin
    comprimir), ADX de la tabla de voces, WAV de comprobacion y las tablas en texto."""
    import json  # noqa: PLC0415
    import swap_b3  # noqa: PLC0415
    import voces  # noqa: PLC0415
    os.makedirs(out_dir, exist_ok=True)
    us = os.path.dirname(swap_b3.DEFAULT_AFS)
    db = json.load(open(os.path.join(HERE, "roster_db.json")))["ids"]
    fid = db[donor]["lang"][0]
    sb = SbVoices(spec)
    work = tempfile.mkdtemp(prefix="sb_voces_")
    lines = ["# Gritos de %s sobre el banco de %s (lang %d)" % (spec, db[donor]["name"], fid), "",
             "| hueco | donante (muestras) | tono SB | muestras | SNR dB (runtime) |", "|---|---|---|---|---|"]
    for lk in ("usa", "jpn"):
        path = os.path.join(us, "lang_%s.afs" % lk)
        idx = swap_b3.read_afs_index(path)
        with open(path, "rb") as f:
            f.seek(idx[fid][0])
            raw = f.read(idx[fid][1])
        if raw[:4] == b"\x0f\xf5\x12\xee":
            open(os.path.join(work, "in.lzx"), "wb").write(raw)
            swap_b3.lzx_decompress(os.path.join(work, "in.lzx"), os.path.join(work, "out.bin"), work)
            raw = open(os.path.join(work, "out.bin"), "rb").read()
        kid = gritos.amb_children(raw)[0]
        bank = raw[kid[0]:kid[0] + kid[1]]
        lens = gritos.bank_lengths(bank)
        rep = []
        sounds = gritos_sounds(sb, lens, lk, donor_bank=bank, report=rep)
        entry = gritos.build_lang(raw, sounds)
        open(os.path.join(out_dir, "lang_%s.bin" % lk), "wb").write(entry)
        # comprobacion: el banco nuevo, leido con la logica del runtime, da lo que se metio
        k2 = gritos.amb_children(entry)
        nb = entry[k2[0][0]:k2[0][0] + k2[0][1]]
        assert nb == entry[k2[1][0]:k2[1][0] + k2[1][1]], "los dos bancos deben ser iguales"
        back = gritos.bank_sounds(nb)
        snrs = []
        for i, (x, y) in enumerate(zip(sounds, back)):
            if x is None or y is None:
                assert x is None and y is None, i
                snrs.append(None)
                continue
            assert len(y) == max(1, len(x)), (i, len(x), len(y))
            snrs.append(snr(x, y) if np.any(x) else None)
        worst = min(v for v in snrs if v is not None)
        log("lang_%s: %d huecos, banco %d B (donante %d B), SNR minimo %.1f dB" % (lk, len(lens), len(nb), len(bank), worst))
        if lk == "usa":
            lines += ["| %d | %d | %s | %d | %s |" % (i, ln, "donante (XMA->RXADPC)" if s is None and ln > gritos.SILENT_MAX
                                                       else ("silencio" if s is None else s), n,
                                                       "-" if v is None else "%.1f" % v)
                      for (i, ln, s, n), v in zip(rep, snrs)]
        if previews:
            d = os.path.join(out_dir, "wav_gritos_%s" % lk)
            os.makedirs(d, exist_ok=True)
            for i, y in enumerate(back):
                if y is not None and len(y) > gritos.SILENT_MAX:
                    write_wav(os.path.join(d, "hueco_%02d.wav" % i), y)
    # tabla de voces: bloque del donante (B3 de PS2) y frases nuevas
    donor_block = voces.b3_block(donor)
    w = FileWriter(out_dir)
    block, used = voice_block(sb, w, donor_block, log=log)
    lines += ["", "# Tabla de voces (situacion -> fuente)", "", "| situacion | ADX donante | fuente |", "|---|---|---|"]
    lines += ["| %d | %s | %s |" % (s, donor_block[s] if donor_block else "-", u) for s, u in used]
    if previews:
        for lk in ("usa", "jpn"):
            d = os.path.join(out_dir, "adx", lk)
            for s, u in used:
                p = os.path.join(d, "%03d.adx" % block[s]) if "donante" not in u and block[s] >= 0 else None
                if p and os.path.exists(p):
                    x, r = gritos.adx_decode(open(p, "rb").read())
                    write_wav(os.path.join(out_dir, "wav_voces_%s_s%02d.wav" % (lk, s)), x, r)
    open(os.path.join(out_dir, "tablas.md"), "w", encoding="utf-8").write("\n".join(lines) + "\n")
    log("tabla de voces: %d situaciones propias, %d del donante" % (
        sum("donante" not in u for _, u in used), sum("donante" in u for _, u in used)))
    return block


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("spec", nargs="?", help="sb2:GHF, sb1:GOK...")
    ap.add_argument("--donante", type=int, default=4)
    ap.add_argument("--salida")
    ap.add_argument("--lista", choices=sorted(ISOS))
    ap.add_argument("--test", action="store_true")
    a = ap.parse_args()
    if a.test or not (a.spec or a.lista):
        selftest()
    if a.lista:
        print(" ".join(characters(a.lista)))
    if a.spec:
        build(a.spec, a.donante, a.salida or os.path.join(tempfile.gettempdir(), "sb_voces"))
