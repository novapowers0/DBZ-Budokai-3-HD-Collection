#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""studio_core.py - Nucleo del Studio (sin interfaz): camaras de tecnicas de B3 HD.

Lee y escribe el #ACC (#AMC en PS2) de un CAM bin, entiende su guion #SPX (que clip de
camara va con que tecnica y cuanto espera), plantillas de camara, validacion, puente glTF
(Blender u otro programa 3D, sin add-on) y guardado como mod con respaldo.

Formato (RE 2026-10-06, docs/03_formatos/CAMARA_ACC.md):
  #ACC: +0x10 n_clips  +0x14 tabla (0x20)  +0x18 1  +0x1C 0
        tabla: n x [flags 0x1D, variante, n_frames, off]
        clip:  4 punteros -> pistas ojo | objetivo | roll | fov
        pista: [u32 0][u32 1][u32 n_claves] + claves
               ojo/objetivo [u32 frame][f32 x y z]   roll/fov [u32 frame][f32 rad]
        la ultima clave esta en n_frames-1; interpolacion lineal; 60 fps
  #SPX (little-endian tambien en la HD): builtin 0xA7 = reproducir clip, "push N; call sub 0"
        = esperar N frames. El indice del clip suele ser g[0x60]+K (base por tecnica).

Salidas (nunca toca us/*.afs):
  personaje nativo -> mods/studio_<personaje>/us/data_cmn.afs/<fid CAM>/geom.bin
                      (LZX /N:2048 + relleno a un tamano reservado: recarga sin reiniciar)
  port (mod fuente) -> su camara.bin; la version anterior va a <mod>/respaldo/<fecha>/

  python studio_core.py selftest [--rapido]       ida y vuelta byte a byte de TODOS los clips
  python studio_core.py info 0                    clips y guiones de Goku (ID 0, o ruta de un bin)
  python studio_core.py exportar-glb 0 --clip 24 --out goku.glb [--modelo] [--anim 16]
  python studio_core.py importar-glb goku.glb 0 --clip 24 --out cam.bin
"""
import argparse
import datetime
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MCH = os.path.dirname(HERE)                       # mod center hd
ROOT = os.path.dirname(MCH)
AWO = os.path.join(ROOT, "awo_tools")
sys.path[:0] = [MCH, AWO]

TRACKS = ("ojo", "objetivo", "roll", "fov")
VEC = ("ojo", "objetivo")
FPS = 60
CAM_KID, SPX_KID = 5, 8
LZX_MAGIC = b"\x0f\xf5\x12\xee"
MARGIN = 0x10000          # margen reservado en el override para editar sin reiniciar el juego
FOV_MIN, FOV_MAX = 0.05, 2.5


# ================================================================== contenedor #AMB
def endian(amb):
    """'<' PS2 (LE) | '>' HD 360 (BE)."""
    return "<" if struct.unpack("<I", amb[4:8])[0] == 0x20 else ">"


def amb_kids(amb, e):
    n, t = struct.unpack(e + "II", amb[0x10:0x18])
    return [struct.unpack_from(e + "4I", amb, t + 16 * k) for k in range(n)]


def amb_rebuild(amb, e, idx, child):
    """#AMB con el hijo `idx` sustituido (mismo orden y tipos; alineacion 32 HD / 16 PS2)."""
    al = 32 if e == ">" else 16
    t = struct.unpack_from(e + "I", amb, 0x14)[0]
    ents = [list(x) for x in amb_kids(amb, e)]
    datas = [child if k == idx else amb[o:o + s] for k, (o, s, _ty, _r) in enumerate(ents)]
    start = struct.unpack_from(e + "I", amb, 0x18)[0]
    tail = len(amb) - max((o + s for o, s, _t, _r in ents), default=len(amb))   # la PS2 no rellena el final
    out = bytearray(amb[:start])
    for k, d in enumerate(datas):
        out += bytes((-len(out)) % al)
        ents[k][0], ents[k][1] = (len(out) if d else 0), len(d)
        out += d
        struct.pack_into(e + "4I", out, t + 16 * k, *ents[k])
    if tail:
        out += bytes((-len(out)) % al)
    return bytes(out)


def unlzx(data, key="studio"):
    if data[:4] != LZX_MAGIC:
        return bytes(data)
    import afs_pair  # noqa: PLC0415
    return bytes(afs_pair.decompress(data, "%s_%d_%08x" % (key, len(data), hash(bytes(data[:4096])) & 0xFFFFFFFF)))


def load_cam_file(path):
    """camara.bin de un mod (HD, PS2 #AMB o LZX) -> bytes tal cual (endianness original)."""
    return unlzx(open(path, "rb").read(), "studio_" + re.sub(r"\W", "_", os.path.basename(path)))


# ================================================================== pistas y clips
def _fmt(e, kind):
    return (e + "I3f", 16) if kind in VEC else (e + "If", 8)


def read_track(b, p, e, name):
    a, ty, nk = struct.unpack_from(e + "3I", b, p)
    fmt, ks = _fmt(e, name)
    return (a, ty), [list(struct.unpack_from(fmt, b, p + 12 + ks * k)) for k in range(nk)]


def write_track(head, keys, e, name):
    fmt = _fmt(e, name)[0]
    return struct.pack(e + "3I", head[0], head[1], len(keys)) + b"".join(
        struct.pack(fmt, int(k[0]), *[float(v) for v in k[1:]]) for k in keys)


class Clip:
    """Un clip de camara: n frames y 4 pistas de claves [[frame, valores...]]."""

    def __init__(self, frames, keys, flags=0x1D, variante=0, heads=None):
        self.frames, self.keys, self.flags, self.variante = int(frames), keys, flags, variante
        self.heads = heads or {t: (0, 1) for t in TRACKS}

    def copy(self):
        return Clip(self.frames, {t: [list(k) for k in self.keys[t]] for t in TRACKS}, self.flags, self.variante,
                    dict(self.heads))

    def to_json(self):
        return dict(frames=self.frames, flags=self.flags, variante=self.variante,
                    cabeceras={t: list(self.heads[t]) for t in TRACKS}, claves=self.keys)

    @staticmethod
    def from_json(d):
        return Clip(d["frames"], {t: [list(k) for k in d["claves"][t]] for t in TRACKS}, d.get("flags", 0x1D),
                    d.get("variante", 0), {t: tuple(d.get("cabeceras", {}).get(t, (0, 1))) for t in TRACKS})

    def at(self, track, f):
        return sample(self.keys[track], f)

    def baked(self):
        """1 clave por frame en las 4 pistas (lo que editan las herramientas)."""
        c = self.copy()
        for t in TRACKS:
            c.keys[t] = [[f] + list(sample(self.keys[t], f)) for f in range(self.frames)]
        return c

    def reduced(self, tol=1e-4):
        """Quita las claves que la interpolacion lineal reproduce (como b1port.reduce_track)."""
        c = self.copy()
        for t in TRACKS:
            c.keys[t] = reduce_keys(self.keys[t], tol)
        return c


def sample(keys, f):
    """Valor interpolado (lineal) de una pista [[frame, v...]] en el frame f."""
    if f <= keys[0][0]:
        return list(keys[0][1:])
    for a, b in zip(keys, keys[1:]):
        if a[0] <= f <= b[0]:
            t = (f - a[0]) / ((b[0] - a[0]) or 1)
            return [x + (y - x) * t for x, y in zip(a[1:], b[1:])]
    return list(keys[-1][1:])


def reduce_keys(keys, tol=1e-4):
    if len(keys) <= 2:
        return [list(k) for k in keys]
    out = [list(keys[0])]
    for i in range(1, len(keys) - 1):
        a, b = out[-1], keys[i + 1]
        t = (keys[i][0] - a[0]) / ((b[0] - a[0]) or 1)
        if any(abs(a[j] + (b[j] - a[j]) * t - keys[i][j]) > tol for j in range(1, len(a))):
            out.append(list(keys[i]))
    out.append(list(keys[-1]))
    return out


# ================================================================== CAM bin
class CamBin:
    """CAM bin (#AMB [#ACC, #ACL, #CCM, #SPX]) de B3, HD o PS2. Los clips se editan en
    `self.clips`; build() devuelve el bin (identico al original si no cambio nada)."""

    def __init__(self, data):
        self.raw = bytes(data)
        if self.raw[:4] != b"#AMB":
            raise ValueError("no es un #AMB (CAM bin)")
        self.e = e = endian(self.raw)
        self.ents = amb_kids(self.raw, e)
        self.acc_idx = next((i for i, x in enumerate(self.ents) if x[2] == CAM_KID), None)
        if self.acc_idx is None:
            raise ValueError("el bin no tiene #ACC/#AMC (no es un CAM bin)")
        o, s = self.ents[self.acc_idx][:2]
        self.acc = self.raw[o:o + s]
        self.clips = decode_acc(self.acc, e)
        self.n_orig = len(self.clips)

    def kid(self, ty):
        x = next((x for x in self.ents if x[2] == ty), None)
        return None if x is None else self.raw[x[0]:x[0] + x[1]]

    def spx(self):
        return self.kid(SPX_KID)

    def build(self):
        acc = encode_acc(self.acc, self.clips, self.e)
        return self.raw if acc == self.acc else amb_rebuild(self.raw, self.e, self.acc_idx, acc)

    def hd(self):
        """Bin HD (BE) aunque el original sea PS2."""
        b = self.build()
        if self.e == "<":
            import ps2hd  # noqa: PLC0415
            b = bytes(ps2hd.convert_block(b))
        return b


def decode_acc(acc, e):
    if len(acc) < 0x20:            # stub vacio (Gohan del Futuro de SB2: 0 clips)
        return []
    n, tbl = struct.unpack_from(e + "2I", acc, 0x10)
    out = []
    for k in range(n):
        flags, var, nf, off = struct.unpack_from(e + "4I", acc, tbl + 16 * k)
        ptr = struct.unpack_from(e + "4I", acc, off)
        keys, heads = {}, {}
        for name, p in zip(TRACKS, ptr):
            heads[name], keys[name] = read_track(acc, p, e, name)
        out.append(Clip(nf, keys, flags, var, heads))
    return out


def encode_acc(orig, clips, e):
    """#ACC completo: cabecera original, tabla y clips contiguos alineados a 8 (como los oficiales)."""
    if not clips and len(orig) < 0x20:
        return orig
    head = bytearray(orig[:0x20]) if len(orig) >= 0x20 else bytearray(
        b"#ACC" + struct.pack(e + "7I", 0x20, 0, 2, 0, 0x20, 1, 0))
    if e == "<":
        head[:4] = b"#AMC"
    struct.pack_into(e + "2I", head, 0x10, len(clips), 0x20)
    out = head + bytes(16 * len(clips))
    for k, c in enumerate(clips):
        out += bytes((-len(out)) % 8)
        off = len(out)
        body, ptr = b"", []
        for t in TRACKS:
            ptr.append(off + 16 + len(body))
            body += write_track(c.heads[t], c.keys[t], e, t)
        out += struct.pack(e + "4I", *ptr) + body
        struct.pack_into(e + "4I", out, 0x20 + 16 * k, c.flags, c.variante, c.frames, off)
    out += bytes((-len(out)) % 8)
    return bytes(out)


# ================================================================== validacion
def validate_clip(c):
    """Lista de problemas (vacia = valido). Reglas del informe 05_studio §4.3."""
    errs = []
    if c.frames < 2:
        errs.append("el clip necesita al menos 2 frames")
    for t in TRACKS:
        ks = c.keys[t]
        if not ks:
            errs.append("%s: sin claves" % t)
            continue
        fr = [k[0] for k in ks]
        if fr[0] < 0 or any(b <= a for a, b in zip(fr, fr[1:])):
            errs.append("%s: los frames de las claves tienen que crecer" % t)
        if fr[-1] != c.frames - 1:
            errs.append("%s: la ultima clave tiene que estar en el frame %d" % (t, c.frames - 1))
        if any(not math.isfinite(v) for k in ks for v in k[1:]):
            errs.append("%s: hay valores no numericos" % t)
            continue
        if t == "fov" and any(not FOV_MIN <= k[1] <= FOV_MAX for k in ks):
            errs.append("fov fuera de %.0f-%.0f grados" % (math.degrees(FOV_MIN), math.degrees(FOV_MAX)))
        if t == "roll" and any(abs(k[1]) > math.pi + 1e-6 for k in ks):
            errs.append("roll fuera de -180..180 grados")
    if not errs:
        near = min(math.dist(c.at("ojo", f), c.at("objetivo", f)) for f in range(c.frames))
        if near <= 0.1:
            errs.append("el ojo y el objetivo estan demasiado juntos (%.3f)" % near)
    return errs


def validate_cam(cam):
    errs = []
    if len(cam.clips) < cam.n_orig:
        errs.append("no se pueden borrar clips: el guion los pide por numero")
    for k, c in enumerate(cam.clips):
        errs += ["clip %d: %s" % (k, x) for x in validate_clip(c)]
    return errs


# ================================================================== plantillas
def _smooth(t):
    return t * t * (3 - 2 * t)


def tpl_orbita(n, centro, radio, altura, ang0, barrido, fov_deg, objetivo_y=None):
    """Orbita alrededor de `centro` (grados): el ojo gira `barrido` grados en n frames."""
    ty = centro[1] if objetivo_y is None else objetivo_y
    ojo, obj = [], []
    for f in range(n):
        a = math.radians(ang0 + barrido * _smooth(f / max(1, n - 1)))
        ojo.append([f, centro[0] + radio * math.sin(a), centro[1] + altura, centro[2] + radio * math.cos(a)])
        obj.append([f, centro[0], ty, centro[2]])
    return Clip(n, dict(ojo=ojo, objetivo=obj, roll=[[0, 0.0], [n - 1, 0.0]],
                        fov=[[0, math.radians(fov_deg)], [n - 1, math.radians(fov_deg)]]))


def tpl_travelling(n, ojo0, ojo1, obj0, obj1, fov0, fov1, suave=True):
    """Travelling / acercamiento: ojo, objetivo y fov (grados) de A a B."""
    def lerp(a, b, f):
        t = f / max(1, n - 1)
        t = _smooth(t) if suave else t
        return [x + (y - x) * t for x, y in zip(a, b)]
    return Clip(n, dict(ojo=[[f] + lerp(ojo0, ojo1, f) for f in range(n)],
                        objetivo=[[f] + lerp(obj0, obj1, f) for f in range(n)],
                        roll=[[0, 0.0], [n - 1, 0.0]],
                        fov=[[f, math.radians(lerp([fov0], [fov1], f)[0])] for f in range(n)]))


def tpl_temblor(clip, amp, hz, f0=0, f1=None, semilla=1):
    """Temblor (golpe, explosion): ruido suave sumado a ojo y objetivo entre f0 y f1."""
    c = clip.baked()
    f1 = c.frames - 1 if f1 is None else min(f1, c.frames - 1)
    ph = [(semilla * 12.9898 + i * 78.233) % (2 * math.pi) for i in range(6)]
    for f in range(max(0, f0), f1 + 1):
        u = (f - f0) / max(1, f1 - f0)
        env = math.sin(math.pi * u) if f1 > f0 else 1.0
        t = f / FPS * hz * 2 * math.pi
        d = [amp * env * (0.6 * math.sin(t + ph[i]) + 0.4 * math.sin(2.3 * t + ph[i + 3])) for i in range(3)]
        for tr in VEC:
            k = c.keys[tr][f]
            c.keys[tr][f] = [k[0], k[1] + d[0], k[2] + d[1], k[3] + d[2]]
    return c


def tpl_giro(clip, grados0, grados1, f0=0, f1=None):
    """Giro de camara (roll) sumado, de grados0 a grados1 entre f0 y f1."""
    c = clip.baked()
    f1 = c.frames - 1 if f1 is None else min(f1, c.frames - 1)
    for f in range(c.frames):
        u = min(1.0, max(0.0, (f - f0) / max(1, f1 - f0)))
        r = c.keys["roll"][f][1] + math.radians(grados0 + (grados1 - grados0) * _smooth(u))
        c.keys["roll"][f][1] = (r + math.pi) % (2 * math.pi) - math.pi
    return c


def brush(clip, track, f, delta, radio=8):
    """Mueve la clave del frame f en `delta` (lista) y arrastra las vecinas con caida suave."""
    c = clip.baked()
    for g in range(max(0, f - radio), min(c.frames, f + radio + 1)):
        w = 1.0 if radio == 0 else 0.5 * (1 + math.cos(math.pi * abs(g - f) / (radio + 1)))
        k = c.keys[track][g]
        c.keys[track][g] = [k[0]] + [v + w * d for v, d in zip(k[1:], delta)]
    return c


def shift(clip, track, delta):
    """Mueve toda la trayectoria de una pista (sin cambiar su forma)."""
    c = clip.copy()
    c.keys[track] = [[k[0]] + [v + d for v, d in zip(k[1:], delta)] for k in c.keys[track]]
    return c


def retime(clip, n):
    """Estira o encoge el clip a n frames (sin cambiar la forma de la trayectoria)."""
    c = Clip(n, {}, clip.flags, clip.variante, dict(clip.heads))
    for t in TRACKS:
        c.keys[t] = [[f] + sample(clip.keys[t], f * (clip.frames - 1) / max(1, n - 1)) for f in range(n)]
    return c


# ================================================================== guion #SPX
_PUSHV = (0x5b, 0x5c, 0x5e, 0x4b, 0x4c)


def spx_tokens(spx):
    """Recorrido lineal APROXIMADO de la VM (solo lectura): [(pos, op, valor)] por sentencia."""
    base = struct.unpack_from("<I", spx, 0x14)[0]
    i, stmt, out = base, [], []
    n = len(spx)
    while i < n:
        b = spx[i]
        b1 = spx[i + 1] if i + 1 < n else -1
        if b == 0x08 and b1 in (0x10, 0x20, 0x30):
            k = {0x10: 1, 0x20: 2, 0x30: 4}[b1]
            stmt.append((i, "push", int.from_bytes(spx[i + 2:i + 2 + k], "little")))
            i += 2 + k
        elif b == 0x09 and b1 == 0x30:
            stmt.append((i, "pushf", struct.unpack_from("<f", spx, i + 2)[0]))
            i += 6
        elif b == 0x08 and b1 in _PUSHV:
            stmt.append((i, "pushv", (b1, spx[i + 2] if i + 2 < n else 0)))
            i += 3
        elif b == 0x01 and b1 in (0x10, 0x20, 0x30):
            k = {0x10: 1, 0x20: 2, 0x30: 4}[b1]
            v = int.from_bytes(spx[i + 2:i + 2 + k], "little")
            tail = spx[i + 2 + k:i + 5 + k]
            if tail == b"\x02\x80\x02":
                stmt.append((i, "builtin", v))
                i += 5 + k
            elif tail == b"\x02\x73\x02":
                stmt.append((i, "sub", base + v))
                i += 5 + k
            else:
                stmt.append((i, "acc", v))
                i += 2 + k
        elif b == 0x12 and b1 == 0x10:
            i += 3
        elif b == 0x0b:
            if stmt:
                out.append(stmt)
            stmt = []
            i += 1
        else:
            i += 1
    if stmt:
        out.append(stmt)
    return out, base


def _cam_arg(stmt):
    """Indice de clip de una llamada 0xA7: ('rel', K) = g[0x60]+K, ('abs', K) o ('var', None)."""
    toks = [t for t in stmt if t[1] in ("push", "pushv", "pushf")]
    head = []
    for t in toks:
        if t[1] == "pushf":
            break
        head.append(t)
    for j, t in enumerate(head):
        if t[1] == "pushv":
            if t[2] == (0x5c, 0x60) and j + 1 < len(head) and head[j + 1][1] == "push":
                return ("rel", head[j + 1][2])
            return ("var", None)
    ints = [t[2] for t in head if t[1] == "push"]
    return ("abs", ints[1]) if len(ints) >= 2 else ("var", None)


def spx_sequences(spx):
    """Guiones con camara: [{sub, ranuras, pasos: [{clip: (tipo, K), espera, codigos}], codigos}]."""
    if not spx or spx[:4] != b"#SPX":
        return []
    stmts, base = spx_tokens(spx)
    ns = struct.unpack_from("<I", spx, 0x18)[0]
    slots = {i: base + x for i, x in enumerate(struct.unpack_from("<%dI" % ns, spx, 0x20)) if x != 0xFFFFFFFF}
    sites = [(t[0], t[2]) for s in stmts for t in s if t[1] == "sub"]
    starts = sorted({a for _, a in sites if a != base} | set(slots.values()))

    def owner(pos):
        return max((s for s in starts if s <= pos), default=None)

    callers = {}
    for pos, tgt in sites:
        callers.setdefault(tgt, set()).add(owner(pos))

    def roots(f, seen=()):
        hit = {k for k, v in slots.items() if v == f}
        if hit:
            return hit
        out = set()
        for c in callers.get(f, ()):
            if c is not None and c not in seen:
                out |= roots(c, seen + (f,))
        return out

    seqs = {}
    for s in stmts:
        own = owner(s[0][0])
        if own is None:
            continue
        q = seqs.setdefault(own, {"sub": own, "pasos": [], "codigos": [], "t": 0})
        codes = [t[2] for t in s if t[1] == "push" and 0x200 <= t[2] < 0x500]
        for c in codes:
            q["codigos"].append((c, q["t"]))
        if any(t[1] == "builtin" and t[2] == 0xA7 for t in s):
            q["pasos"].append({"clip": _cam_arg(s), "espera": 0, "inicio": q["t"]})
        elif len(s) >= 2 and s[-1][1] == "sub" and s[-1][2] == base and s[-2][1] == "push":
            q["t"] += s[-2][2]
            if q["pasos"]:
                q["pasos"][-1]["espera"] += s[-2][2]
    out = []
    for own, q in sorted(seqs.items()):
        if q["pasos"]:
            q["ranuras"] = sorted(roots(own))
            del q["t"]
            out.append(q)
    return out


def guess_base(seq, frames):
    """Base g[0x60] mas probable: la que mejor casa la duracion de cada clip con su espera."""
    rel = [(p["clip"][1], p["espera"]) for p in seq["pasos"] if p["clip"][0] == "rel" and p["espera"]]
    if not rel:
        return 0
    best = None
    for b in range(0, max(1, len(frames))):
        if any(b + k >= len(frames) for k, _ in rel):
            continue
        score = sum(abs(frames[b + k] - w) for k, w in rel)
        if best is None or score < best[0]:
            best = (score, b)
    return best[1] if best else 0


def seq_label(seq, i):
    names = {0: "ranura 0 (modo hiper / definitiva)", 20: "ranura 20 (agarre)", 10: "ranura 10"}
    sl = ", ".join(names.get(s, "ranura %d" % s) for s in seq["ranuras"]) or "sin ranura"
    return "Guion %d: %d clips, %s" % (i + 1, len(seq["pasos"]), sl)


# ================================================================== moveset: marcas de golpe
def csk_of(anm):
    """#CSK (HD) / #BSK (PS2) de un ANM bin -> (bytes, endian)."""
    e = endian(anm)
    for o, s, _t, _r in amb_kids(anm, e):
        if anm[o:o + 4] in (b"#CSK", b"#BSK"):
            return anm[o:o + s], e
    return None, e


def code_info(csk, e, code):
    """(anim, banco, [(frame, tipo AP)]) del primer sub-bloque de un codigo de ataque."""
    n, lst, _nhr, hr = struct.unpack_from(e + "4I", csk, 0x10)
    if code >= n:
        return None
    a = struct.unpack_from(e + "I", csk, lst + 4 * code)[0]
    if not a or a + 48 > len(csk):
        return None
    anim, pool = struct.unpack_from(e + "HH", csk, a)
    _pad, nap, apo = struct.unpack_from(e + "3I", csk, a + 0x24)
    marks = []
    for k in range(min(nap, 64)):
        t, nl, do = struct.unpack_from(e + "HHI", csk, apo + 8 * k)
        if t in (1, 7):
            marks += [(struct.unpack_from(e + "H", csk, do + 16 * ln)[0], t) for ln in range(min(nl, 256))]
    return anim, pool, marks


# ================================================================== glTF (Blender sin add-on)
def look_quat(eye, tgt, roll):
    import numpy as np  # noqa: PLC0415
    f = np.subtract(tgt, eye)
    f = f / (np.linalg.norm(f) + 1e-12)
    r = np.cross(f, (0.0, 1.0, 0.0))
    r = r / (np.linalg.norm(r) + 1e-12)
    u = np.cross(r, f)
    c, s = math.cos(roll), math.sin(roll)
    r, u = r * c + u * s, u * c - r * s
    m = np.column_stack([r, u, -f])                # camara glTF: mira a -Z, Y arriba
    w = math.sqrt(max(0.0, 1 + m[0, 0] + m[1, 1] + m[2, 2])) / 2
    x = math.copysign(math.sqrt(max(0.0, 1 + m[0, 0] - m[1, 1] - m[2, 2])) / 2, m[2, 1] - m[1, 2])
    y = math.copysign(math.sqrt(max(0.0, 1 - m[0, 0] + m[1, 1] - m[2, 2])) / 2, m[0, 2] - m[2, 0])
    z = math.copysign(math.sqrt(max(0.0, 1 - m[0, 0] - m[1, 1] + m[2, 2])) / 2, m[1, 0] - m[0, 1])
    return [x, y, z, w]


class Glb:
    def __init__(self):
        self.bin = bytearray()
        self.j = dict(asset=dict(version="2.0", generator="DBZ3 HD Studio"), buffers=[], bufferViews=[],
                      accessors=[], nodes=[], meshes=[], skins=[], animations=[], cameras=[],
                      scenes=[dict(nodes=[])], scene=0)

    def acc(self, arr, typ, comp=5126, target=None, minmax=False):
        import numpy as np  # noqa: PLC0415
        arr = np.ascontiguousarray(arr, np.float32 if comp == 5126 else (np.uint16 if comp == 5123 else np.uint32))
        self.bin += bytes((-len(self.bin)) % 4)
        bv = dict(buffer=0, byteOffset=len(self.bin), byteLength=arr.nbytes)
        if target:
            bv["target"] = target
        self.bin += arr.tobytes()
        self.j["bufferViews"].append(bv)
        n = {"SCALAR": 1, "VEC3": 3, "VEC4": 4, "MAT4": 16}[typ]
        a = dict(bufferView=len(self.j["bufferViews"]) - 1, componentType=comp, count=arr.size // n, type=typ)
        if minmax:
            a["min"] = arr.reshape(-1, n).min(0).tolist()
            a["max"] = arr.reshape(-1, n).max(0).tolist()
        self.j["accessors"].append(a)
        return len(self.j["accessors"]) - 1

    def save(self, path):
        self.bin += bytes((-len(self.bin)) % 4)
        self.j["buffers"] = [dict(byteLength=len(self.bin))]
        for k in ("meshes", "skins", "cameras", "animations"):
            if not self.j[k]:
                del self.j[k]
        js = json.dumps(self.j, separators=(",", ":")).encode()
        js += b" " * ((-len(js)) % 4)
        with open(path, "wb") as f:
            f.write(struct.pack("<3I", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(self.bin)))
            f.write(struct.pack("<I4s", len(js), b"JSON") + js)
            f.write(struct.pack("<I4s", len(self.bin), b"BIN\0") + bytes(self.bin))


def anim_pool(anm):
    """Primer #ACM/#AMM (banco 3, el propio) de un ANM bin -> (bytes, endian)."""
    e = endian(anm)
    for o, s, t, _r in amb_kids(anm, e):
        if t == 3:
            return anm[o:o + s], e
    return None, e


def anim_count(acm, e):
    return struct.unpack_from(e + "I", acm, 0x10)[0] if acm else 0


def anim_locals(skel, acm, e, anim, f):
    """Matrices locales de la pose del frame f (reglas de altura.py: giro sustituye, pos suma)."""
    import numpy as np  # noqa: PLC0415
    import altura  # noqa: PLC0415
    from awg_vertex_buffer import qmat  # noqa: PLC0415
    n, tbl, nb, names = struct.unpack_from(e + "4I", acm, 0x10)
    flags, _v, nf, off = struct.unpack_from(e + "4I", acm, tbl + 16 * anim)
    per = 3 if flags & 0x10 else 2
    f = f % max(1, nf)
    bones = [acm[names + 32 * i:names + 32 * i + 32].split(b"\0")[0].decode("latin1") for i in range(nb)]
    by = {altura.suffix(nm): i for i, (nm, *_r) in enumerate(skel)}
    loc = [np.array(qmat(*q, *p), float) for _nm, q, p, _par in skel]
    if not off:
        return loc
    for j in range(nb):
        i = by.get(altura.suffix(bones[j]))
        if i is None:
            continue
        ps = struct.unpack_from(e + "%dI" % per, acm, off + 4 * per * j)
        q, p = skel[i][1], np.array(skel[i][2])
        if ps[0]:
            a, ty, nk = struct.unpack_from(e + "3I", acm, ps[0])
            keys = [list(struct.unpack_from(e + "4H", acm, ps[0] + 12 + 8 * k)) for k in range(nk)]
            q = altura.euler(_euler_lerp(keys, f))
        if ps[1]:
            a, ty, nk = struct.unpack_from(e + "3I", acm, ps[1])
            keys = [list(struct.unpack_from(e + "I3f", acm, ps[1] + 12 + 16 * k)) for k in range(nk)]
            p = p + sample(keys, f)
        loc[i] = np.array(qmat(*q, *p), float)
    return loc


def _euler_lerp(keys, f):
    if f <= keys[0][0]:
        return keys[0][1:]
    for a, b in zip(keys, keys[1:]):
        if a[0] <= f <= b[0]:
            t = (f - a[0]) / ((b[0] - a[0]) or 1)
            return [x + (((y - x + 32768) % 65536) - 32768) * t for x, y in zip(a[1:], b[1:])]
    return keys[-1][1:]


def export_glb(path, clip, model=None, anm=None, anim=0, name="camara"):
    """.glb con la camara animada (+ el personaje con su animacion si se da modelo/anm)."""
    import numpy as np  # noqa: PLC0415
    g = Glb()
    samplers, channels = [], []
    nf = clip.frames
    if model is not None:
        import altura  # noqa: PLC0415
        from model_render import Model  # noqa: PLC0415
        m, skel = Model(model), altura.skeleton(model)
        base = 0
        for nm, q, p, _par in skel:
            g.j["nodes"].append(dict(name=nm, rotation=list(q), translation=list(p)))
        roots = []
        for i, (_nm, _q, _p, par) in enumerate(skel):
            if par < 0 or par == i:
                roots.append(base + i)
            else:
                g.j["nodes"][base + par].setdefault("children", []).append(base + i)
        W = np.array(m.worlds())
        ibm = np.linalg.inv(W).transpose(0, 2, 1).reshape(-1)
        P, N = m.posed()
        prim = dict(attributes=dict(
            POSITION=g.acc(P, "VEC3", target=34962, minmax=True), NORMAL=g.acc(N, "VEC3", target=34962),
            JOINTS_0=g.acc(np.column_stack([m.vbone, np.zeros((len(P), 3), int)]), "VEC4", 5123, 34962),
            WEIGHTS_0=g.acc(np.column_stack([np.ones(len(P)), np.zeros((len(P), 3))]), "VEC4", target=34962)),
            indices=g.acc(m.tris.astype(np.uint32).reshape(-1), "SCALAR", 5125, 34963))
        g.j["meshes"].append(dict(name="cuerpo", primitives=[prim]))
        g.j["skins"].append(dict(joints=list(range(len(skel))), inverseBindMatrices=g.acc(ibm, "MAT4")))
        g.j["nodes"].append(dict(name="modelo", mesh=0, skin=0))
        g.j["nodes"].append(dict(name="personaje", children=roots + [len(g.j["nodes"]) - 1]))
        g.j["scenes"][0]["nodes"].append(len(g.j["nodes"]) - 1)
        acm, ea = anim_pool(anm) if anm is not None else (None, ">")
        if acm is not None and 0 <= anim < anim_count(acm, ea):
            import altura as _al  # noqa: PLC0415
            n, tbl, nb, names = struct.unpack_from(ea + "4I", acm, 0x10)
            flags, _v, af, off = struct.unpack_from(ea + "4I", acm, tbl + 16 * anim)
            per = 3 if flags & 0x10 else 2
            by = {_al.suffix(nm): i for i, (nm, *_r) in enumerate(skel)}
            tacc = g.acc(np.arange(nf, dtype=np.float32) / FPS, "SCALAR", minmax=True)
            for j in range(nb if off else 0):
                bn = acm[names + 32 * j:names + 32 * j + 32].split(b"\0")[0].decode("latin1")
                i = by.get(_al.suffix(bn))
                if i is None:
                    continue
                ps = struct.unpack_from(ea + "%dI" % per, acm, off + 4 * per * j)
                if ps[0]:
                    _a, _t, nk = struct.unpack_from(ea + "3I", acm, ps[0])
                    keys = [list(struct.unpack_from(ea + "4H", acm, ps[0] + 12 + 8 * k)) for k in range(nk)]
                    q = [_al.euler(_euler_lerp(keys, f % af)) for f in range(nf)]
                    samplers.append(dict(input=tacc, output=g.acc(q, "VEC4"), interpolation="LINEAR"))
                    channels.append(dict(sampler=len(samplers) - 1, target=dict(node=i, path="rotation")))
                if ps[1]:
                    _a, _t, nk = struct.unpack_from(ea + "3I", acm, ps[1])
                    keys = [list(struct.unpack_from(ea + "I3f", acm, ps[1] + 12 + 16 * k)) for k in range(nk)]
                    t = [np.array(skel[i][2]) + sample(keys, f % af) for f in range(nf)]
                    samplers.append(dict(input=tacc, output=g.acc(t, "VEC3"), interpolation="LINEAR"))
                    channels.append(dict(sampler=len(samplers) - 1, target=dict(node=i, path="translation")))
    ct = g.acc(np.arange(nf, dtype=np.float32) / FPS, "SCALAR", minmax=True)
    eye = [clip.at("ojo", f) for f in range(nf)]
    rot = [look_quat(eye[f], clip.at("objetivo", f), clip.at("roll", f)[0]) for f in range(nf)]
    fovs = [clip.at("fov", f)[0] for f in range(nf)]
    g.j["cameras"].append(dict(type="perspective", perspective=dict(yfov=fovs[0], znear=0.5, zfar=5000.0,
                                                                     aspectRatio=16 / 9)))
    cam = len(g.j["nodes"])
    g.j["nodes"].append(dict(name=name, camera=0, translation=list(eye[0]), rotation=rot[0],
                             extras=dict(fov_rad_por_frame=fovs)))
    g.j["scenes"][0]["nodes"].append(cam)
    k = len(samplers)
    samplers += [dict(input=ct, output=g.acc(eye, "VEC3"), interpolation="LINEAR"),
                 dict(input=ct, output=g.acc(rot, "VEC4"), interpolation="LINEAR"),
                 dict(input=ct, output=g.acc(fovs, "SCALAR"), interpolation="LINEAR")]
    channels += [dict(sampler=k, target=dict(node=cam, path="translation")),
                 dict(sampler=k + 1, target=dict(node=cam, path="rotation")),
                 dict(sampler=k + 2, target=dict(path="pointer", extensions=dict(
                     KHR_animation_pointer=dict(pointer="/cameras/0/perspective/yfov"))))]
    g.j["extensionsUsed"] = ["KHR_animation_pointer"]
    g.j["animations"].append(dict(name="toma", samplers=samplers, channels=channels))
    g.save(path)
    return path


def read_glb(path):
    b = open(path, "rb").read()
    if b[:4] != b"glTF":
        raise ValueError("no es un .glb")
    jl = struct.unpack_from("<I", b, 12)[0]
    j = json.loads(b[20:20 + jl])
    bl = struct.unpack_from("<I", b, 20 + jl)[0] if len(b) > 28 + jl else 0
    return j, b[28 + jl:28 + jl + bl]


def _accessor(j, bin_, i):
    import numpy as np  # noqa: PLC0415
    a = j["accessors"][i]
    bv = j["bufferViews"][a["bufferView"]]
    n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
    dt = {5126: np.float32, 5123: np.uint16, 5125: np.uint32, 5121: np.uint8}[a["componentType"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride")
    if stride and stride != n * np.dtype(dt).itemsize:
        raw = np.frombuffer(bin_, np.uint8, stride * a["count"], off).reshape(a["count"], stride)
        return raw[:, :n * np.dtype(dt).itemsize].copy().view(dt).reshape(a["count"], n)
    return np.frombuffer(bin_, dt, a["count"] * n, off).reshape(a["count"], n)


def glb_camera_track(path):
    """[(frame, ojo, R3x3 mundo, yfov)] de la primera camara del .glb, a 60 fps."""
    import numpy as np  # noqa: PLC0415
    j, b = read_glb(path)
    nodes = j["nodes"]
    parent = {c: i for i, n in enumerate(nodes) for c in n.get("children", [])}
    cam = next((i for i, n in enumerate(nodes) if "camera" in n), None)
    if cam is None:
        raise ValueError("el .glb no tiene camara")
    chain = {cam}
    p = cam
    while p in parent:
        p = parent[p]
        chain.add(p)
    ch, fov_ch, tmax = {}, None, 0.0

    def mat(q):
        x, y, z, w = q
        return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])

    def samp(t_in, vals, t):
        if t <= t_in[0]:
            return vals[0]
        if t >= t_in[-1]:
            return vals[-1]
        k = int(np.searchsorted(t_in, t)) - 1
        u = (t - t_in[k]) / (t_in[k + 1] - t_in[k])
        v = vals[k] * (1 - u) + vals[k + 1] * u
        return v / np.linalg.norm(v) if len(v) == 4 else v

    for an in j.get("animations", []):
        for c in an["channels"]:
            s = an["samplers"][c["sampler"]]
            tg = c["target"]
            ti, vo = _accessor(j, b, s["input"])[:, 0], _accessor(j, b, s["output"])
            if tg.get("path") == "pointer":
                ptr = tg.get("extensions", {}).get("KHR_animation_pointer", {}).get("pointer", "")
                if ptr.endswith("/perspective/yfov"):
                    fov_ch = (ti, vo)
                    tmax = max(tmax, float(ti[-1]))
            elif tg.get("node") in chain:
                ch[(tg["node"], tg["path"])] = (ti, vo)
                tmax = max(tmax, float(ti[-1]))

    def world(i, t):
        n = nodes[i]
        if "matrix" in n:
            M = np.array(n["matrix"], float).reshape(4, 4).T
        else:
            T = np.array(n.get("translation", [0, 0, 0]), float)
            Q = np.array(n.get("rotation", [0, 0, 0, 1]), float)
            S = np.array(n.get("scale", [1, 1, 1]), float)
            if (i, "translation") in ch:
                T = samp(*ch[(i, "translation")], t)
            if (i, "rotation") in ch:
                Q = samp(*ch[(i, "rotation")], t)
            if (i, "scale") in ch:
                S = samp(*ch[(i, "scale")], t)
            M = np.eye(4)
            M[:3, :3] = mat(Q) * S
            M[:3, 3] = T
        return world(parent[i], t) @ M if i in parent else M

    yfov0 = j["cameras"][nodes[cam]["camera"]]["perspective"]["yfov"]
    out = []
    for f in range(int(round(tmax * FPS)) + 1):
        M = world(cam, f / FPS)
        fov = float(samp(*fov_ch, f / FPS)[0]) if fov_ch else yfov0
        R = M[:3, :3] / np.linalg.norm(M[:3, :3], axis=0)
        out.append((f, M[:3, 3].copy(), R, fov))
    return out


def clip_from_track(track, ref=None, dist_default=30.0):
    """Clip a partir de la camara del .glb. El objetivo = ojo + vista * distancia del clip de
    referencia (glTF no guarda el punto de mira)."""
    import numpy as np  # noqa: PLC0415
    n = len(track)
    dist = [math.dist(ref.at("ojo", f), ref.at("objetivo", f)) if ref else dist_default for f in range(
        ref.frames if ref else 1)]
    keys = {t: [] for t in TRACKS}
    for f, eye, R, yfov in track:
        fwd, up = -R[:, 2], R[:, 1]
        r0 = np.cross(fwd, (0.0, 1.0, 0.0))
        r0 /= np.linalg.norm(r0) + 1e-12
        u0 = np.cross(r0, fwd)
        ang = math.atan2(-float(np.dot(up, r0)), float(np.dot(up, u0)))
        tgt = eye + fwd * dist[min(f, len(dist) - 1)]
        keys["ojo"].append([f, *map(float, eye)])
        keys["objetivo"].append([f, *map(float, tgt)])
        keys["roll"].append([f, ang])
        keys["fov"].append([f, float(yfov)])
    heads = dict(ref.heads) if ref else None
    return Clip(n, keys, ref.flags if ref else 0x1D, ref.variante if ref else 0, heads)


# ================================================================== Blender
def find_blender(hint=None):
    cands = [hint] if hint else []
    cands += sorted(__import__("glob").glob(r"C:\Program Files\Blender Foundation\Blender*\blender.exe"),
                    reverse=True)
    cands.append(shutil.which("blender"))
    return next((c for c in cands if c and os.path.isfile(c)), None)


def blender_open(blender, glb, blend):
    """Abre Blender (con ventana) con el .glb importado; el usuario edita y guarda el .blend."""
    script = os.path.join(HERE, "blender_puente.py")
    return subprocess.Popen([blender, "--python", script, "--", "abrir", glb, blend])


def blender_export(blender, blend, glb, timeout=300):
    """Blender en segundo plano: .blend -> .glb (sin add-on)."""
    script = os.path.join(HERE, "blender_puente.py")
    r = subprocess.run([blender, "-b", blend, "--python", script, "--", "exportar", glb],
                       capture_output=True, text=True, timeout=timeout,
                       creationflags=0x08000000 if os.name == "nt" else 0)
    if r.returncode != 0 or not os.path.isfile(glb):
        raise RuntimeError("Blender no pudo exportar:\n" + (r.stdout + r.stderr)[-1500:])
    return glb


# ================================================================== personajes
def load_db():
    with open(os.path.join(MCH, "roster_db.json"), encoding="utf-8") as fh:
        return json.load(fh)


def natives():
    """[{id, nombre, cam, modelo, anm}] de los personajes de B3 con CAM bin."""
    out = []
    for e in load_db()["ids"]:
        cam = e.get("cam")
        if not cam or cam >= 0xFFFFFFFF:
            continue
        anm = next((a for a in e.get("anm") or [] if a < 0xFFFFFFFF), None)
        out.append(dict(id=e["id"], nombre=e["name"].title(), cam=cam, modelo=(e.get("models") or [None])[0],
                        anm=anm))
    return out


def us_dir(hint=None):
    for d in (hint, os.path.join(ROOT, "us"), os.path.join(ROOT, "assets", "us")):
        if d and os.path.isfile(os.path.join(d, "data_cmn.afs")):
            return d
    return hint or os.path.join(ROOT, "us")


def hd_entry(fid, us=None):
    """Entrada HD descomprimida de us/data_cmn.afs (lectura; nunca se escribe)."""
    import afs_pair  # noqa: PLC0415
    return bytes(afs_pair.hd(int(fid), region=us_dir(us)))


def afs_slot(fid, us=None):
    import afs_pair  # noqa: PLC0415
    return afs_pair.table(os.path.join(us_dir(us), "data_cmn.afs"))[int(fid)][1]


def ports(mods):
    """Mods fuente de personaje con camara propia: [{mod, nombre, camara, modelo, anm}]."""
    out = []
    if not mods or not os.path.isdir(mods):
        return out
    try:
        import tomllib  # noqa: PLC0415
    except ImportError:
        return out
    for d in sorted(os.listdir(mods)):
        toml = os.path.join(mods, d, "personaje.toml")
        if d.startswith("_") or not os.path.isfile(toml):
            continue
        try:
            with open(toml, "rb") as fh:
                c = tomllib.load(fh).get("personaje", {})
        except (OSError, ValueError):
            continue
        cam = c.get("camara")
        if not cam or not os.path.isfile(os.path.join(mods, d, cam)):
            continue
        mv = c.get("moveset")
        mv = mv[0] if isinstance(mv, list) and mv else mv
        md = c.get("modelos") or c.get("modelo")
        md = md[0] if isinstance(md, list) and md else md
        out.append(dict(mod=d, nombre=str(c.get("nombre", d)), camara=cam, donante=c.get("donante"),
                        modelo=os.path.join(mods, d, md) if md else None,
                        anm=os.path.join(mods, d, mv) if mv else None))
    return out


def load_model_bin(path_or_fid, us=None):
    """Modelo o ANM HD (fid nativo o fichero de un mod: LZX/PS2 se convierten)."""
    if path_or_fid is None:
        return None
    if isinstance(path_or_fid, int):
        return hd_entry(path_or_fid, us)
    b = unlzx(open(path_or_fid, "rb").read(), "studio_" + re.sub(r"\W", "_", os.path.basename(path_or_fid)))
    if b[:4] == b"#AMB" and endian(b) == "<":
        import ps2hd  # noqa: PLC0415
        b = bytes(ps2hd.convert_block(b))
    return b


def slug(name):
    s = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
    return s or "personaje"


# ================================================================== guardar (mods)
def stamp():
    return datetime.datetime.now().strftime("%Y%m%d_%H%M%S")


def backup(mod_dir, files, when=None):
    """Copia los ficheros existentes a <mod>/respaldo/<fecha>/ (misma ruta relativa) antes de
    reescribirlos. Nunca se borra nada."""
    when = when or stamp()
    moved = []
    for f in files:
        if os.path.isfile(f):
            rel = os.path.relpath(f, mod_dir)
            dst = os.path.join(mod_dir, "respaldo", when, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(f, dst)
            moved.append(dst)
    return moved


def atomic_write(path, data, tries=8):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".studio_tmp"
    with open(tmp, "wb") as fh:
        fh.write(data)
    for k in range(tries):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:           # el juego puede tener el fichero abierto un instante
            if k == tries - 1:
                raise
            time.sleep(0.25 * (k + 1))


def lzx(data, work=None):
    """LZX /N:2048 (kit/base.py, el de swap_b3 / swap_matrix) -> bytes comprimidos."""
    from kit import base  # noqa: PLC0415
    return base.lzx_bytes(data, True, work)


def conflicts(mods, fid, own):
    """Otros mods activos que sustituyen la misma entrada (gana el primero alfabetico)."""
    out = []
    if not mods or not os.path.isdir(mods):
        return out
    for d in sorted(os.listdir(mods)):
        if d == own or d.startswith("_") or d.endswith(".disabled"):
            continue
        if os.path.exists(os.path.join(mods, d, "us", "data_cmn.afs", str(fid))):
            out.append(d)
    return out


def save_native(cam, mods, nombre, fid, project, us=None):
    """Mod mods/studio_<slug>/us/data_cmn.afs/<fid>/geom.bin (+ studio.json, manifest.txt).
    Devuelve dict(mod, ruta, tamano, reservado, reiniciar, respaldo, conflictos)."""
    errs = validate_cam(cam)
    if errs:
        raise ValueError("\n".join(errs))
    data = cam.hd()
    CamBin(data)                                   # se vuelve a leer: tiene que decodificar
    mod = "studio_" + slug(nombre)
    mdir = os.path.join(mods, mod)
    geom = os.path.join(mdir, "us", "data_cmn.afs", str(fid), "geom.bin")
    pj = os.path.join(mdir, "studio.json")
    old = {}
    if os.path.isfile(pj):
        with open(pj, encoding="utf-8") as fh:
            old = json.load(fh)
    comp = lzx(data)
    slot = afs_slot(fid, us)
    to_read = (slot + 0xFFF) & ~0xFFF
    reserve = int(old.get("reservado") or 0)
    restart = not os.path.isfile(geom)
    if len(comp) > reserve:
        restart = restart or reserve > 0
        reserve = (max(to_read, len(comp) + MARGIN) + 0xFFF) & ~0xFFF
    when = stamp()
    bk = backup(mdir, [geom, pj, os.path.join(mdir, "manifest.txt")], when)
    atomic_write(geom, comp + bytes(reserve - len(comp)))
    with open(geom, "rb") as fh:                  # comprobacion: lo escrito descomprime al bin
        got = fh.read()
    if unlzx(got[:len(comp)], "studio_chk_%s" % when) != data:
        raise RuntimeError("la comprobacion del override fallo (LZX)")
    project = dict(project, tipo="studio", personaje=nombre, fid_cam=int(fid), reservado=reserve,
                   slot_original=slot, guardado=when)
    atomic_write(pj, json.dumps(project, indent=1).encode("utf-8"))
    atomic_write(os.path.join(mdir, "manifest.txt"), (
        "name=%s\ndescription=Studio: camaras de %s\ntype=studio\nsource=CAM %d\ntarget=%d\nregion=us\n" % (
            mod, nombre, fid, fid)).encode("utf-8"))
    return dict(mod=mod, ruta=geom, tamano=len(comp), reservado=reserve, reiniciar=restart, respaldo=bk,
                conflictos=conflicts(mods, fid, mod))


def save_port(cam, mod_dir, rel, project):
    """Reescribe el camara.bin de un mod fuente (HD sin comprimir; roster_build lo monta)."""
    errs = validate_cam(cam)
    if errs:
        raise ValueError("\n".join(errs))
    data = cam.hd()
    CamBin(data)
    path = os.path.join(mod_dir, rel)
    when = stamp()
    pj = os.path.join(mod_dir, "studio.json")
    bk = backup(mod_dir, [path, pj], when)
    atomic_write(path, data)
    atomic_write(pj, json.dumps(dict(project, tipo="studio_port", camara=rel, guardado=when), indent=1).encode())
    return dict(mod=os.path.basename(mod_dir), ruta=path, tamano=len(data), respaldo=bk, reiniciar=False,
                conflictos=[])


def load_project(mods, nombre):
    p = os.path.join(mods or "", "studio_" + slug(nombre), "studio.json")
    if os.path.isfile(p):
        with open(p, encoding="utf-8") as fh:
            return json.load(fh)
    return None


def apply_project(cam, project):
    """Aplica los clips editados de un studio.json sobre el CAM original."""
    for k, d in sorted((int(k), v) for k, v in (project or {}).get("clips", {}).items()):
        c = Clip.from_json(d)
        while len(cam.clips) < k:
            cam.clips.append(cam.clips[-1].copy())
        if k < len(cam.clips):
            cam.clips[k] = c
        else:
            cam.clips.append(c)
    return cam


# ================================================================== autocomprobacion
def selftest(fast=False, mods=None):
    import numpy as np  # noqa: PLC0415
    import afs_pair  # noqa: PLC0415
    t0 = time.time()
    ok = True

    def say(m):
        print("[studio] " + m, flush=True)

    natv = natives()
    if fast:
        natv = [n for n in natv if n["id"] in (0, 4, 20)]
    nclip = nbin = nseq = 0
    for n in natv:
        for src, data in (("HD", hd_entry(n["cam"])),) + ((("PS2", bytes(afs_pair.ps2(n["cam"]))),) if os.path.isfile(
                os.path.join(afs_pair.PS2_GH, "data_cmn.afs")) else ()):
            try:
                cam = CamBin(data)
            except ValueError as ex:
                say("aviso: ID %d (%s) CAM %d %s: %s" % (n["id"], n["nombre"], n["cam"], src, ex))
                continue
            if encode_acc(cam.acc, cam.clips, cam.e) != cam.acc:
                ok = False
                say("FALLO %s CAM %d: el #ACC no sale byte a byte" % (src, n["cam"]))
                continue
            if cam.clips:                         # bin completo: forzar la reconstruccion del #AMB
                full = amb_rebuild(cam.raw, cam.e, cam.acc_idx, encode_acc(cam.acc, cam.clips, cam.e))
                if full != cam.raw:
                    ok = False
                    say("FALLO %s CAM %d: el #AMB reconstruido difiere" % (src, n["cam"]))
            for k, c in enumerate(cam.clips):
                errs = validate_clip(c)
                if errs:
                    ok = False
                    say("FALLO %s CAM %d clip %d: %s" % (src, n["cam"], k, errs))
            nclip += len(cam.clips)
            nbin += 1
            if src == "HD":
                nseq += len(spx_sequences(cam.spx()))
    say("ida y vuelta byte a byte: %d bins, %d clips (HD + PS2), %d guiones con camara" % (nbin, nclip, nseq))
    # guion de Goku: el patron conocido (clip g[0x60]+0 con espera 0x2e)
    goku = CamBin(hd_entry(288))
    seqs = spx_sequences(goku.spx())
    hit = [s for s in seqs if any(p["clip"] == ("rel", 0) and p["espera"] == 0x2E for p in s["pasos"])]
    if not hit:
        ok = False
        say("FALLO: no encuentro el guion de Goku (clip +0, espera 0x2e)")
    else:
        say("guion de Goku: %s, base estimada %d" % (seq_label(hit[0], seqs.index(hit[0])),
                                                     guess_base(hit[0], [c.frames for c in goku.clips])))
    # moveset: mismas marcas en HD y PS2
    ci = code_info(*csk_of(hd_entry(292)), 0x259)
    if not ci or ci[0] != 63 or ci[1] != 3 or (30, 1) not in ci[2]:
        ok = False
        say("FALLO: marcas de golpe del codigo 0x259 de Goku: %s" % (ci,))
    # plantillas y validacion
    c0 = goku.clips[0]
    tpls = [tpl_orbita(90, (0, 10.5, 0), 40, 5, 0, 120, 38), tpl_travelling(60, (0, 12, 60), (0, 11, 25), (0, 10, 0),
                                                                             (0, 11, 0), 40, 30),
            tpl_temblor(c0, 0.8, 9, 10, 40), tpl_giro(c0, 0, 25, 0, 30), brush(c0, "ojo", 20, [3, 0, 0], 6),
            retime(c0, 120)]
    for t in tpls:
        e = validate_clip(t)
        if e:
            ok = False
            say("FALLO plantilla: %s" % e)
    bad = c0.copy()
    bad.keys["fov"][-1][1] = 3.5
    if not validate_clip(bad):
        ok = False
        say("FALLO: la validacion no detecta un fov imposible")
    red = tpls[0].reduced()
    if max(abs(a - b) for f in range(90) for a, b in zip(red.at("ojo", f), tpls[0].at("ojo", f))) > 1e-3:
        ok = False
        say("FALLO: reduccion de claves")
    # edicion -> bin -> lectura: los demas clips intactos
    cam = CamBin(goku.raw)
    cam.clips[3] = tpls[0]
    cam.clips.append(tpls[1])
    new = CamBin(cam.build())
    same = all(new.clips[k].keys == goku.clips[k].keys for k in range(len(goku.clips)) if k != 3)
    if not same or new.kid(SPX_KID) != goku.kid(SPX_KID) or len(new.clips) != len(goku.clips) + 1:
        ok = False
        say("FALLO: la edicion toca otros clips o el guion")
    if max(abs(a - b) for f in range(90) for a, b in zip(new.clips[3].at("ojo", f), tpls[0].at("ojo", f))) > 1e-3:
        ok = False
        say("FALLO: el clip editado no se lee igual")
    # glTF de ida y vuelta (sin Blender)
    work = tempfile.mkdtemp(prefix="studio_selftest_")
    glb = export_glb(os.path.join(work, "c.glb"), goku.clips[24])
    back = clip_from_track(glb_camera_track(glb), goku.clips[24])
    err = max(np.max(np.abs(np.subtract(back.at(t, f), goku.clips[24].at(t, f)))) for t in TRACKS
              for f in range(goku.clips[24].frames))
    if err > 1e-3 or back.frames != goku.clips[24].frames:
        ok = False
        say("FALLO glTF: error %.6f" % err)
    say("glTF ida y vuelta (clip 24 de Goku): %d frames, error max %.6f" % (back.frames, err))
    # guardar como mod en una carpeta de pruebas (LZX + relleno + respaldo)
    tm = mods or os.path.join(work, "mods")
    os.makedirs(tm, exist_ok=True)
    try:
        r1 = save_native(cam, tm, "Selftest Goku", 288, dict(clips={"3": tpls[0].to_json()}))
        r2 = save_native(cam, tm, "Selftest Goku", 288, dict(clips={"3": tpls[0].to_json()}))
        sz = os.path.getsize(r2["ruta"])
        if sz != r1["reservado"] or r2["reservado"] != r1["reservado"] or not r2["respaldo"] or r2["reiniciar"]:
            ok = False
            say("FALLO guardar: %s / %s" % (r1, r2))
        say("mod de prueba: %s, LZX %d B, reservado %d B (slot original %d B), respaldo %d ficheros" % (
            r2["mod"], r2["tamano"], r2["reservado"], afs_slot(288), len(r2["respaldo"])))
        again = apply_project(CamBin(goku.raw), load_project(tm, "Selftest Goku"))
        if again.clips[3].keys != tpls[0].keys or len(again.clips) != len(goku.clips):
            ok = False
            say("FALLO: studio.json no regenera el clip")
    except FileNotFoundError as ex:
        say("aviso: sin xbcompress (%s): no se prueba el guardado LZX" % ex)
    say("RESULTADO: %s (%.1f s)" % ("OK" if ok else "FALLO", time.time() - t0))
    return 0 if ok else 1


# ================================================================== CLI
def _cam_source(arg, mods=None):
    if str(arg).isdigit():
        n = next((x for x in natives() if x["id"] == int(arg)), None)
        if n is None:
            raise SystemExit("ID %s sin CAM bin" % arg)
        return CamBin(hd_entry(n["cam"])), n
    return CamBin(load_cam_file(arg)), None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("selftest", help="ida y vuelta byte a byte de todos los clips + plantillas + glTF")
    s.add_argument("--rapido", action="store_true", help="solo Goku, Gohan adulto y Ginyu")
    s.add_argument("--mods", help="carpeta de mods de pruebas (por defecto una temporal)")
    i = sub.add_parser("info", help="clips y guiones de camara de un personaje (ID nativo o camara.bin)")
    i.add_argument("personaje")
    x = sub.add_parser("exportar-glb", help="clip -> .glb (Blender u otro programa 3D)")
    x.add_argument("personaje")
    x.add_argument("--clip", type=int, required=True)
    x.add_argument("--out", required=True)
    x.add_argument("--modelo", action="store_true", help="incluir el personaje (nativos)")
    x.add_argument("--anim", type=int, default=0)
    m = sub.add_parser("importar-glb", help=".glb -> clip dentro de una COPIA del CAM bin")
    m.add_argument("glb")
    m.add_argument("personaje")
    m.add_argument("--clip", type=int, required=True)
    m.add_argument("--out", required=True)
    a = ap.parse_args(argv)
    if a.cmd == "selftest":
        return selftest(a.rapido, a.mods)
    cam, nat = _cam_source(a.personaje)
    if a.cmd == "info":
        print("%s: %d clips (%s)" % (nat["nombre"] if nat else a.personaje, len(cam.clips),
                                     "HD" if cam.e == ">" else "PS2"))
        for k, c in enumerate(cam.clips):
            print("  clip %2d: %3d frames, claves %s, fov %.1f grados" % (
                k, c.frames, "/".join(str(len(c.keys[t])) for t in TRACKS), math.degrees(c.keys["fov"][0][1])))
        seqs = spx_sequences(cam.spx())
        for q, sq in enumerate(seqs):
            b = guess_base(sq, [c.frames for c in cam.clips])
            print("  %s (base estimada %d)" % (seq_label(sq, q), b))
            for p in sq["pasos"]:
                kind, kk = p["clip"]
                print("     clip %-8s espera %3d" % ("+%d" % kk if kind == "rel" else str(kk) if kind == "abs"
                                                    else "variable", p["espera"]))
        return 0
    if a.cmd == "exportar-glb":
        model = anm = None
        if a.modelo and nat:
            model, anm = hd_entry(nat["modelo"]), hd_entry(nat["anm"])
        export_glb(a.out, cam.clips[a.clip], model, anm, a.anim, "camara_clip%d" % a.clip)
        print("glb ->", a.out)
        return 0
    ref = cam.clips[a.clip] if a.clip < len(cam.clips) else None
    new = clip_from_track(glb_camera_track(a.glb), ref)
    if a.clip < len(cam.clips):
        cam.clips[a.clip] = new
    else:
        cam.clips.append(new)
    errs = validate_cam(cam)
    if errs:
        raise SystemExit("ERROR:\n" + "\n".join(errs))
    with open(a.out, "wb") as fh:
        fh.write(cam.build())
    print("clip %d: %d frames desde %s -> %s" % (a.clip, new.frames, a.glb, a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
