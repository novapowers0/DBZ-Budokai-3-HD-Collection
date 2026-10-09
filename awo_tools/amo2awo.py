#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""amo2awo.py - Conversor GENERICO de modelos PS2 #AMO0 (little-endian) a HD #AWO (big-endian).

Sin plantilla: el #AWO se construye desde el propio #AMO con su esqueleto, sus grupos de
malla y su piel, con el layout de la HD (RE 2026-10-03 contra los 440 pares nativos
PS2 GH n <-> HD n, ver amo2awo_validate.py).

Equivalencias (PS2 -> HD):
  #AMO0 cab 0x30 [nb, huesos, nAMG, tabla AMG, nL, nombres]  -> #AWO cab 0x30 (mismos campos)
    registros de hueso 0x20 [i, lista, hijo, hermano, padre]  -> en 0x30, punteros reubicados
    tabla AWG, nombres (nb x 32), AWGs alineados a 32, listas por hueso (nL x 16 B) al final
  #AMG cab 0x20 [nb, ejes, ntex, nombres]                     -> #AWG cab 0x40
    [nb, ejes, ntex, nombres, materiales, nmat, vb0, vb_tam, ib, ib_tam, paleta, npal]
    nombres (nb x 32) | materiales (nmat x 0x50) | ejes (nb x 80, punteros reubicados)
    | arms (nb x 0x14: [i, grupo, 0, caja, 0]) | cajas (0x40, copia) | grupos
    | ventanas (44 B) | IB (u16) | paleta (slot -> hueso, [0] = FFFFFFFF)
  arm PS2 16 B [i, grupo, piel, caja]; grupo = [n, 0x10, 0, 0, offs[n]] -> partes de 0xA0 B
    + tiras VIF (UNPACK V4-32: el nº de qwords da el stride 16/32/48 = pos/+uv/+nrm+uv)
  piel PS2 (por hueso): trozos [peso, n32, loc32, n16, loc16]; cada entrada lleva la posicion
    y normal LOCALES al hueso + el offset del vertice  ==>  ventana HD
    [pos, peso, slot de paleta, normal, FFFFFFFF, uv del vertice]
  Draw HD (0x60) por parte: centro/radio de la parte, material (u16 alto), 0x1158, 44,
    prim 5 (tira skinneada) / 4 (lista rigida en espacio del hueso del grupo),
    A = rango de ventanas, B = primitivas, etiqueta {flag, nombre del hueso}.
  Material HD (0x50) = color, escala, CLAMP_1/2 (GS 0x08/0x09), tex, shader, vtype, vflags,
    ALPHA_1/2 (GS 0x42/0x43).
Diferencias con el nativo aceptadas: la IB skinneada se re-encadena (mismos triangulos) y las
normales se renormalizan en float32 (difieren en el ultimo bit en ~13 %).

Uso:
  python amo2awo.py <entrada> <salida>
    entrada: #AMO0 suelto o #AMB PS2 (se convierte el contenedor entero con ps2hd.py,
             incluidas las texturas #AMT -> #AZT)
"""
import struct
import sys

import numpy as np

# ---------------------------------------------------------------------------
# utilidades
# ---------------------------------------------------------------------------


def le(b, o):
    return struct.unpack_from("<I", b, o)[0]


def lef(b, o):
    return struct.unpack_from("<f", b, o)[0]


def align(x, a):
    return (x + a - 1) // a * a


def swap_words(raw):
    n = len(raw) // 4
    return struct.pack(">%dI" % n, *struct.unpack("<%dI" % n, raw[:4 * n]))


def norm32(n):
    """Normal renormalizada en float32 (como la HD, salvo el ultimo bit en algunos casos)."""
    v = np.array(n, dtype=np.float32)
    d = np.float32(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if d == 0:
        return [float(x) for x in v]
    r = np.float32(1) / np.sqrt(d)
    return [float(x) for x in (v * r)]


class ConvError(Exception):
    pass


# ---------------------------------------------------------------------------
# lectura PS2
# ---------------------------------------------------------------------------
class Part:
    pass


class PS2AMG:
    def __init__(self, b, a):
        self.b, self.a = b, a
        if b[a:a + 4] != b"#AMG":
            raise ConvError("AMG sin magic en 0x%x" % a)
        self.word0c = le(b, a + 0xC)
        self.nb = le(b, a + 0x10)
        self.axes_rel = le(b, a + 0x14)
        self.ntex = le(b, a + 0x18)
        self.names_rel = le(b, a + 0x1C)
        self.arms = []
        for i in range(self.nb):
            arm = le(b, a + self.axes_rel + 80 * i + 0x34)
            w = struct.unpack_from("<4I", b, a + arm) if arm else None
            self.arms.append((arm, w))
        self.parts = []
        self.vert_of = {}
        self.hidden_verts = set()              # vertices de partes ocultas (no se exportan)
        self.groups = []                       # (bone, [part idx])
        self.all_parts = []                    # todas, tambien las ocultas (editor hex del Mod Kit)
        for i, (arm, w) in enumerate(self.arms):
            if not w or not w[1]:
                continue
            g = a + w[1]
            n, tbl = le(b, g), le(b, g + 4)
            idx = []
            for k in range(n):
                p = self._part(i, a + w[1] + le(b, g + tbl + 4 * k))
                self.all_parts.append(p)
                if p.hidden:
                    continue
                idx.append(len(self.parts))
                self.parts.append(p)
            if idx:
                self.groups.append((i, idx))
        self.skin = []   # (bone, peso, pos, nrm|None, vert_off)
        for i, (arm, w) in enumerate(self.arms):
            if not w or not w[2]:
                continue
            s = a + w[2]
            for c in range(le(b, s + 12)):
                ch = s + 16 + 32 * c
                wt = lef(b, ch)
                n32, l32, n16, l16 = (le(b, ch + 4), le(b, ch + 8), le(b, ch + 12), le(b, ch + 16))
                for k in range(n32):
                    e = a + l32 + 32 * k
                    self.skin.append((i, wt, [lef(b, e + 4 * j) for j in range(3)],
                                      [lef(b, e + 16 + 4 * j) for j in range(3)], le(b, e + 12)))
                for k in range(n16):
                    e = a + l16 + 16 * k
                    self.skin.append((i, wt, [lef(b, e + 4 * j) for j in range(3)], None,
                                      le(b, e + 12)))
        self.skin = [s for s in self.skin if s[4] not in self.hidden_verts]

    def _part(self, bone, po):
        b, a = self.b, self.a
        p = Part()
        p.bone = bone
        p.off = po                             # en el #AMO: cabecera de la parte (vtype +0, vflags +4)
        p.hdr = bytes(b[po:po + 0xA0])
        p.vtype, p.vflags, p.tex, p.shader = struct.unpack_from("<4I", b, po)
        # Truco de modders para trajes alternativos (aureola, cola...): vtype/vflags = FFFFFF/FFFF
        # oculta la parte en el juego. Se omite (sus vertices se apartan para filtrar la piel).
        p.hidden = (p.vtype & 0xFFFF) == 0xFFFF
        p.center = [lef(b, po + 0x10 + 4 * j) for j in range(3)]
        p.radius = lef(b, po + 0x1C)
        size = le(b, po + 0x90)
        p.size = (size - 0x60000000) * 16 if 0x60000000 <= size < 0x70000000 else 0
        p.regs = {}
        for o in range(0x40, 0x90, 16):
            v, r = struct.unpack_from("<QQ", b, po + o)
            p.regs[r] = v
        pos, end = po + 0xA0, po + 0xA0 + p.size
        p.strips, p.verts = [], []
        pi = len(self.parts)
        while pos + 0x20 <= end:
            vif = le(b, pos + 0xC)
            ft, cnt = le(b, pos + 0x10), le(b, pos + 0x14)
            num = (vif >> 16) & 0xFF
            if cnt == 0 or cnt > 0xFFFF or (vif >> 24) & 0x7F != 0x6C or num < 1:
                break
            stride = (num - 1) * 16 // cnt
            vp = pos + 0x20
            strip = []
            for x in range(cnt):
                o = vp + x * stride
                if p.hidden:
                    self.hidden_verts.add(o - a)
                    continue
                xyz = [lef(b, o + 4 * j) for j in range(3)]
                nrm, col, uv = None, None, None
                q = o + 16
                if p.vtype & 1:
                    nrm = [lef(b, q + 4 * j) for j in range(3)]
                    q += 16
                if p.vtype & 2:
                    col = [lef(b, q + 4 * j) for j in range(4)]
                    q += 16
                if p.vtype & 4:
                    uv = [lef(b, q), lef(b, q + 4)]
                    q += 16
                if q - o != stride:
                    raise ConvError("vtype 0x%x con stride %d" % (p.vtype, stride))
                self.vert_of[o - a] = (pi, len(p.verts))
                strip.append(len(p.verts))
                p.verts.append((o - a, xyz, nrm, uv, col))
            p.strips.append((ft, strip))
            pos = vp + cnt * stride
        return p


class PS2AMO:
    def __init__(self, b):
        if b[:4] != b"#AMO":
            raise ConvError("no es #AMO0")
        self.b = b
        self.nb, self.bones, self.namg, self.amg_tbl, self.nlist, self.names = \
            struct.unpack_from("<6I", b, 0x10)
        self.amg_off = [le(b, self.amg_tbl + 4 * i) for i in range(self.namg)]
        self.amgs = [PS2AMG(b, o) for o in self.amg_off]


# ---------------------------------------------------------------------------
# escritura HD
# ---------------------------------------------------------------------------
class Out:
    def __init__(self):
        self.b = bytearray()

    def pad(self, a):
        self.b += bytes(align(len(self.b), a) - len(self.b))

    def u32(self, *v):
        self.b += struct.pack(">%dI" % len(v), *v)

    def put32(self, o, v):
        struct.pack_into(">I", self.b, o, v)


def tris_of(strips):
    out = []
    for ft, idx in strips:
        if ft == 1:
            for i in range(len(idx) - 2):
                t = (idx[i], idx[i + 1], idx[i + 2]) if i % 2 == 0 else (idx[i + 1], idx[i], idx[i + 2])
                if len(set(t)) == 3:
                    out.append(t)
        else:
            for i in range(0, len(idx) - 2, 3):
                t = tuple(idx[i:i + 3])
                if len(set(t)) == 3:
                    out.append(t)
    return out


def strip_tri(seq, i):
    a, b, c = seq[i], seq[i + 1], seq[i + 2]
    return (a, b, c) if i % 2 == 0 else (b, a, c)


def stripify(tris):
    """Triangulos orientados -> UNA tira (degenerados de union) con el winding conservado.

    Voraz: arranca en el triangulo libre con menos vecinos libres, prueba sus 3 rotaciones y
    extiende hacia delante mientras haya un triangulo libre con la arista dirigida que toca
    (posicion par: s[-2]->s[-1]; impar: s[-1]->s[-2])."""
    tris = [tuple(t) for t in tris if len(set(t)) == 3]
    n = len(tris)
    if not n:
        return []
    edge = {}
    for ti, (a, b, c) in enumerate(tris):
        for e in ((a, b), (b, c), (c, a)):
            edge.setdefault(e, []).append(ti)
    used = [False] * n

    def third(t, u, v):
        a, b, c = t
        if (a, b) == (u, v):
            return c
        if (b, c) == (u, v):
            return a
        return b

    def free_neighbors(ti):
        a, b, c = tris[ti]
        k = 0
        for e in ((b, a), (c, b), (a, c)):
            k += sum(1 for x in edge.get(e, ()) if not used[x])
        return k

    def grow(seq, mark):
        taken = []
        while True:
            i = len(seq) - 2
            u, v = (seq[-2], seq[-1]) if i % 2 == 0 else (seq[-1], seq[-2])
            nxt = next((x for x in edge.get((u, v), ()) if not used[x] and x not in taken), None)
            if nxt is None:
                break
            taken.append(nxt)
            seq.append(third(tris[nxt], u, v))
        if mark:
            for x in taken:
                used[x] = True
        return seq

    out = []
    remaining = n
    cursor = 0
    while remaining:
        while used[cursor]:
            cursor += 1
        start = cursor
        a, b, c = tris[start]
        used[start] = True
        best = None
        for rot in ((a, b, c), (b, c, a), (c, a, b)):
            s = grow(list(rot), False)
            if best is None or len(s) > len(best):
                best = s
                best_rot = rot
        seq = grow(list(best_rot), True)
        remaining -= 1 + (len(seq) - 3)
        if out:
            out += [out[-1], seq[0]]
            if len(out) % 2 == 1:
                out.append(seq[0])
        out += seq
    return out


def win_bytes(pos, w, slot, nrm, col, uv):
    """Ventana HD de 44 B: pos, peso, slot, normal (o 0), color ARGB8 (o FFFFFFFF), uv."""
    if col is not None:
        c = [max(0, min(255, int(round(x * 255.0)))) for x in col]
        argb = (c[3] << 24) | (c[0] << 16) | (c[1] << 8) | c[2]
    else:
        argb = 0xFFFFFFFF
    return struct.pack(">4fI3fI2f", pos[0], pos[1], pos[2], w, slot,
                       *(norm32(nrm) if nrm else [0.0, 0.0, 0.0]), argb, *(uv or [0.0, 0.0]))


def label_bytes(flag, name, nmax):
    """Etiqueta del draw (+0x38..+0x5F): flag (1 = skinneado) + nombre del hueso del grupo; si
    cabe en 15 se anotan size/cap (como un std::string con buffer de 16) y el texto de depuracion
    'max N mi' recortado a 8 caracteres (N = mayor hueso de la piel del draw)."""
    nb = name.encode("latin1")[:38]
    r = bytearray(40)
    r[0] = flag
    r[1:1 + len(nb)] = nb
    if len(nb) <= 15:
        struct.pack_into("<II", r, 17, len(nb), 15)
        txt = ("max %d mi" % nmax).encode()[:8]
        r[26:26 + len(txt)] = txt
    return bytes(r)


def build_awg(m, names):
    """PS2AMG -> bytes del #AWG (offsets relativos al propio AWG)."""
    b = m.b
    nb = m.nb
    # ---- materiales y draws -------------------------------------------------
    mat_bytes, mat_of_part = [], []
    for p in m.parts:
        col = struct.unpack_from("<4I", p.hdr, 0x30)
        scl = struct.unpack_from("<4I", p.hdr, 0x20)
        mb = struct.pack(">20I", col[0], col[1], col[2], 0, scl[0], scl[1], scl[2], 0,
                         0, p.regs.get(8, 0) & 0xFFFFFFFF, 0, p.regs.get(9, 0) & 0xFFFFFFFF,
                         p.tex, p.shader, p.vtype, p.vflags,
                         p.regs.get(0x42, 0) & 0xFFFFFFFF, p.regs.get(0x43, 0) & 0xFFFFFFFF, 0, 0)
        if mb not in mat_bytes:
            mat_bytes.append(mb)
        mat_of_part.append(mat_bytes.index(mb))
    skinned_bones = sorted({s[0] for s in m.skin})
    skinned_parts = {m.vert_of[s[4]][0] for s in m.skin if s[4] in m.vert_of}
    # [0] = FFFFFFFF (matriz del hueso del grupo) solo si hay partes rigidas
    pal = ([0xFFFFFFFF] if len(skinned_parts) < len(m.parts) else []) + skinned_bones
    slot = {bn: pal.index(bn) for bn in skinned_bones}
    skin_by_part = {}
    for s in m.skin:
        pv = m.vert_of.get(s[4])
        if pv is None:
            raise ConvError("entrada de piel a un vertice desconocido 0x%x" % s[4])
        skin_by_part.setdefault(pv[0], []).append((s, pv[1]))
    windows, ib, draws = [], [], []
    for pi, p in enumerate(m.parts):
        a0 = len(windows)
        uniq, remap = {}, {}
        skinned = pi in skin_by_part
        if skinned:
            ent = {vi: sv for sv, vi in skin_by_part[pi]}
            if len(ent) != len(p.verts):
                raise ConvError("parte %d: %d vertices sin piel" % (pi, len(p.verts) - len(ent)))
        keys = []
        for vi, (_, xyz, nrm, uv, col) in enumerate(p.verts):
            if skinned:
                bone, wt, pos, snrm, _ = ent[vi]
                keys.append(win_bytes(pos, wt, slot[bone], snrm, col, uv))
            else:
                keys.append(win_bytes(xyz, 1.0, 0, nrm, col, uv))
        # la HD descarta los triangulos con dos vertices de igual posicion+uv (aunque difieran
        # en normal) y las ventanas que solo usaban ellos
        loc = {}
        for vi, k in enumerate(keys):
            loc.setdefault(k, len(loc))
        ltris = [t for t in tris_of([(ft, [loc[keys[i]] for i in idx]) for ft, idx in p.strips])]
        kpos = {loc[k]: k[:12] + k[36:] for k in keys}
        ltris = [t for t in ltris if len({kpos[t[0]], kpos[t[1]], kpos[t[2]]}) == 3]
        used = {i for t in ltris for i in t}
        vwin = {}
        for vi, k in enumerate(keys):
            li = loc[k]
            if li not in used:
                continue
            if k not in uniq:
                uniq[k] = len(uniq)
                windows.append(k)
            vwin[li] = a0 + uniq[k]
        tris = [(vwin[a], vwin[b], vwin[c]) for a, b, c in ltris]
        prim = 4
        seq = [i for t in tris for i in t]
        nprim = len(tris)
        if skinned:
            st = stripify(tris)
            if len(st) < len(seq):
                prim, seq, nprim = 5, st, max(0, len(st) - 2)
        nmax = max(sv[0][0] for sv in skin_by_part[pi]) if skinned else 0
        draws.append({"part": p, "mat": mat_of_part[pi], "prim": prim,
                      "A": (a0, len(windows) - a0), "B": (len(ib), nprim), "nmax": nmax,
                      "skinned": skinned})
        ib += seq
    if len(windows) > 0xFFFF:
        raise ConvError("demasiadas ventanas (%d) para una IB de 16 bits" % len(windows))

    # ---- layout ------------------------------------------------------------
    o = Out()
    o.b += bytes(0x40)
    names_off = len(o.b)
    o.b += names
    mats_off = len(o.b) if mat_bytes else 0
    for mb in mat_bytes:
        o.b += mb
    axes_off = len(o.b)
    o.b += bytes(80 * nb)
    arms_off = len(o.b)
    o.b += bytes(0x14 * nb)
    o.pad(16)
    # cajas
    box_off = {}
    for i, (arm, w) in enumerate(m.arms):
        if w and w[3]:
            box_off[i] = len(o.b)
            o.b += swap_words(m.b[m.a + w[3]:m.a + w[3] + 0x3C]) + bytes(4)
    # grupos
    grp_off = {}
    di = 0
    for bone, idx in m.groups:
        grp_off[bone] = len(o.b)
        o.u32(len(idx), 0, 0, 0)
        for pi in idx:
            d = draws[pi]
            p = d["part"]
            rec = bytearray(0x60)
            struct.pack_into(">4f", rec, 0, p.center[0], p.center[1], p.center[2], 1.0)
            struct.pack_into(">fIII", rec, 0x10, p.radius, d["mat"] << 16, 0x1158, 44)
            struct.pack_into(">6I", rec, 0x20, d["prim"], 0, d["A"][0], d["A"][1], d["B"][0], d["B"][1])
            flag = 1 if d["skinned"] else 0
            rec[0x38:0x60] = label_bytes(flag, names_list(names)[bone], d["nmax"])
            o.b += rec
            di += 1
    vb0 = len(o.b) if windows else 0
    for w in windows:
        o.b += w
    ib_off = len(o.b) if ib else 0
    for x in ib:
        o.b += struct.pack(">H", x)
    o.pad(4)
    pal_off = len(o.b) if windows else 0
    if windows:
        o.u32(*pal)
    o.pad(16)
    # ejes
    ax_ps2 = m.a + m.axes_rel

    def rel_axes(ptr):
        if not ptr:
            return 0
        k, r = divmod(ptr - m.axes_rel, 80)
        if r or not 0 <= k < nb:
            raise ConvError("puntero de eje inesperado 0x%x" % ptr)
        return axes_off + 80 * k
    for i in range(nb):
        raw = bytearray(swap_words(m.b[ax_ps2 + 80 * i:ax_ps2 + 80 * i + 80]))
        struct.pack_into(">I", raw, 0x34, arms_off + 0x14 * i)
        for f in (0x38, 0x3C, 0x40):
            struct.pack_into(">I", raw, f, rel_axes(le(m.b, ax_ps2 + 80 * i + f)))
        o.b[axes_off + 80 * i:axes_off + 80 * i + 80] = raw
        w = m.arms[i][1]
        struct.pack_into(">5I", o.b, arms_off + 0x14 * i, w[0] if w else i, grp_off.get(i, 0), 0,
                         box_off.get(i, 0), 0)
    hdr = struct.pack(">4s15I", b"#AWG", 0x40, 0, m.word0c, nb, axes_off, m.ntex, names_off,
                      mats_off, len(mat_bytes), vb0, 44 * len(windows), ib_off, 2 * len(ib),
                      pal_off, len(pal) if windows else 0)
    o.b[0:0x40] = hdr
    return bytes(o.b), {"windows": len(windows), "ib": len(ib), "draws": len(draws),
                        "materials": len(mat_bytes), "palette": len(pal) if windows else 0}


def names_list(names):
    return [names[i:i + 32].split(b"\0")[0].decode("latin1") for i in range(0, len(names), 32)]


def convert_amo(b, report=None):
    """bytes #AMO0 (PS2, LE) -> bytes #AWO (HD, BE)."""
    a = PS2AMO(b)
    nb = a.nb
    o = Out()
    o.b += bytes(0x30)
    bones_off = 0x30
    o.b += bytes(0x20 * nb)
    tbl_off = len(o.b)
    o.b += bytes(4 * a.namg)
    names_off = len(o.b)
    o.b += b[a.names:a.names + 32 * nb]
    awg_abs = []
    stats = []
    for k, m in enumerate(a.amgs):
        o.pad(32)
        awg_abs.append(len(o.b))
        names = b[m.a + m.names_rel:m.a + m.names_rel + 32 * m.nb]
        raw, st = build_awg(m, names)
        stats.append(st)
        o.b += raw
    o.pad(32)
    # listas por hueso (nlist entradas de 16 B: [amg, hueso, eje abs, 0])
    list_off = []
    for i in range(nb):
        lp = le(b, a.bones + 0x20 * i + 4)
        list_off.append(len(o.b) if lp else 0)
        if not lp:
            continue
        for e in range(a.nlist):
            ga, gb, gp, g3 = struct.unpack_from("<4I", b, lp + 16 * e)
            hp = 0
            if gp:
                m = a.amgs[ga]
                k, r = divmod(gp - (m.a + m.axes_rel), 80)
                if r or not 0 <= k < m.nb:
                    raise ConvError("lista de hueso con eje inesperado 0x%x" % gp)
                hp = awg_abs[ga] + le_awg_axes(o.b, awg_abs[ga]) + 80 * k
            o.u32(ga, gb, hp, g3)
    # cabecera y registros de hueso
    for i in range(nb):
        w = struct.unpack_from("<8I", b, a.bones + 0x20 * i)

        def rb(ptr):
            if not ptr:
                return 0
            k, r = divmod(ptr - a.bones, 0x20)
            if r or not 0 <= k < nb:
                raise ConvError("puntero de hueso inesperado 0x%x" % ptr)
            return bones_off + 0x20 * k
        struct.pack_into(">8I", o.b, bones_off + 0x20 * i, w[0], list_off[i], rb(w[2]), rb(w[3]),
                         rb(w[4]), w[5], w[6], w[7])
    for k, x in enumerate(awg_abs):
        struct.pack_into(">I", o.b, tbl_off + 4 * k, x)
    struct.pack_into(">4s11I", o.b, 0, b"#AWO", 0x30, 0, 0, nb, bones_off, a.namg, tbl_off,
                     a.nlist, names_off, le(b, 0x28), le(b, 0x2C))
    o.pad(32)
    if report is not None:
        report["awgs"] = stats
    return bytes(o.b)


def le_awg_axes(buf, awg):
    return struct.unpack_from(">I", buf, awg + 0x14)[0]


def convert_file(src, dst):
    b = open(src, "rb").read()
    if b[:4] == b"#AMB":
        sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
        import ps2hd
        out = ps2hd.convert_block(b)
    else:
        out = convert_amo(b)
    open(dst, "wb").write(out)
    return out


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    rep = {}
    out = convert_file(sys.argv[1], sys.argv[2])
    print("OK -> %s (%d bytes)" % (sys.argv[2], len(out)))
