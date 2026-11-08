#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""altura.py - Altura de los pies de un personaje con una animacion (cinematica directa).

RE 2026-10-04 (38 personajes de B3): con la pose de reposo (codigo 0, frame 0) los
tobillos/puntas de los nativos quedan a ras de suelo (0.0-0.1) si:
  - el giro de cada pista es Euler u16 (65536 = 360 grados) compuesto q = qz * qy * qx
    y SUSTITUYE al giro de reposo del hueso;
  - la posicion de una pista se SUMA a la de reposo (Goku y Cooler tienen la cadera de
    reposo en y = -0.97 / -0.78 y solo asi caen al suelo);
  - el padre de cada hueso es el enlace +0x40 de su eje de 80 B (#AMG PS2 / AWG HD).
Un port que reutiliza animaciones de otro esqueleto (Guldo con las de Recoome, piernas
de 4 frente a 10 unidades) flota o se hunde: `correccion` da la escala de las posiciones
de la cadera (largo de pierna) y el desplazamiento vertical que dejan el tobillo a la
misma altura relativa que en el esqueleto original.

Uso:  python altura.py <modelo> <anm> [<modelo_origen> <anm_origen>]
"""
import math
import struct
import sys


# ---------------------------------------------------------------- cuaterniones
def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def qrot(q, v):
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), (-q[0], -q[1], -q[2], q[3]))
    return p[:3]


def euler(v):
    q = (0.0, 0.0, 0.0, 1.0)
    for ax in (2, 1, 0):
        a = v[ax] * math.pi / 32768
        s = [0.0, 0.0, 0.0]
        s[ax] = math.sin(a / 2)
        q = qmul(q, (s[0], s[1], s[2], math.cos(a / 2)))
    return q


def suffix(name):
    return name.split("_", 1)[1] if "_" in name else name


# ---------------------------------------------------------------- esqueletos
def _is_ps2(b):
    """#AMB little-endian (PS2: B3 version 3 y B1); el HD es big-endian (cabecera 0x20 en BE)."""
    return b[:4] == b"#AMB" and struct.unpack("<I", b[4:8])[0] == 0x20


def skeleton(model):
    """[(nombre, quat, pos, padre)] del cuerpo de un modelo (#AMB PS2 o HD)."""
    if model[:4] == b"#AMO" or _is_ps2(model):
        amo = model                               # B1: #AMO suelto
        if model[:4] == b"#AMB":
            n, t = struct.unpack("<II", model[0x10:0x18])
            for k in range(n):
                o, s, _, _ = struct.unpack_from("<4I", model, t + 16 * k)
                if model[o:o + 4] == b"#AMO":
                    amo = model[o:o + s]
                    break
        g = amo.find(b"#AMG")
        e, nb, na = "<", struct.unpack_from("<I", amo, g + 0x10)[0], struct.unpack_from("<I", amo, g + 0x1C)[0]
        base, axes, names, data = g, g + 0x20, g + na, amo
    else:
        from awg_vertex_buffer import AwgVertexBuffer, be32  # noqa: PLC0415
        a = AwgVertexBuffer._parse(model)
        e, nb, data = ">", a.bone_count(), a.data
        base, axes, names = a.awg0, a.awg0 + be32(a.data, a.awg0 + 0x14), a.awg0 + 0x40
    out = []
    for i in range(nb):
        o = axes + 80 * i
        f = struct.unpack_from(e + "7f", data, o)
        par = struct.unpack_from(e + "I", data, o + 0x40)[0]
        nm = bytes(data[names + 32 * i:names + 32 * i + 32]).split(b"\0")[0].decode("latin1")
        out.append((nm, f[0:4], f[4:7], (base + par - axes) // 80 if par else -1))
    return out


# ---------------------------------------------------------------- animaciones
def amb_kids(b):
    e = "<" if _is_ps2(b) else ">"
    n, t = struct.unpack(e + "II", b[0x10:0x18])
    return e, [struct.unpack_from(e + "4I", b, t + 16 * k) for k in range(n)]


def idle_tracks(anm, code=0):
    """{sufijo: (giro frame 0 | None, pos frame 0 | None)} de la animacion del codigo
    `code` del moveset (ANM PS2 o HD); None si usa una animacion global."""
    e, kids = amb_kids(anm)
    bsk = anm[kids[0][0]:kids[0][0] + kids[0][1]]
    amm = anm[kids[1][0]:kids[1][0] + kids[1][1]]
    n, lst = struct.unpack(e + "II", bsk[0x10:0x18])
    a0 = struct.unpack_from(e + "I", bsk, lst + 4 * code)[0]
    if not a0:
        return None
    anim, pool = struct.unpack_from(e + "HH", bsk, a0)
    if pool != 3:
        return None
    return anim_tracks(amm, anim, e)


def anim_tracks(amm, anim, e):
    n, t, nb, no = struct.unpack(e + "4I", amm[0x10:0x20])
    flags, var, nf, off = struct.unpack_from(e + "4I", amm, t + 16 * anim)
    if not off:
        return None
    per = 3 if flags & 0x10 else 2
    out = {}
    for j in range(nb):
        nm = amm[no + 32 * j:no + 32 * j + 32].split(b"\0")[0].decode("latin1")
        rp, pp = struct.unpack_from(e + "2I", amm, off + 4 * per * j)
        rot = struct.unpack_from(e + "3H", amm, rp + 14) if rp and struct.unpack_from(e + "I", amm, rp + 8)[0] else None
        pos = struct.unpack_from(e + "3f", amm, pp + 16) if pp and struct.unpack_from(e + "I", amm, pp + 8)[0] else None
        out.setdefault(suffix(nm), (rot, pos))
    return out


def pose(skel, tracks, hip=(1.0, 0.0)):
    """{sufijo: posicion global}; hip = (escala, desplazamiento y) de la pista de cadera."""
    tracks = tracks or {}
    G = {}

    def get(i):
        if i in G:
            return G[i]
        nm, q, p, par = skel[i]
        rot, pos = tracks.get(suffix(nm), (None, None))
        if rot:
            q = euler(rot)
        if pos:
            if suffix(nm) == "WAIST":
                pos = (pos[0] * hip[0], pos[1] * hip[0] + hip[1], pos[2] * hip[0])
            p = tuple(a + b for a, b in zip(p, pos))
        if par < 0 or par == i:
            G[i] = (q, p)
        else:
            pq, pp = get(par)
            v = qrot(pq, p)
            G[i] = (qmul(pq, q), tuple(pp[k] + v[k] for k in range(3)))
        return G[i]

    for i in range(len(skel)):
        get(i)
    return {suffix(skel[i][0]): G[i][1] for i in G}


def any_tracks(anm):
    """Reposo (codigo 0) o, si usa la animacion global, la primera con cadera."""
    t = idle_tracks(anm)
    if t:
        return t
    e, kids = amb_kids(anm)
    amm = anm[kids[1][0]:kids[1][0] + kids[1][1]]
    for k in range(struct.unpack(e + "I", amm[0x10:0x14])[0]):
        t = anim_tracks(amm, k, e)
        if t and t.get("WAIST", (None, None))[1]:
            return t
    return None


def scale_hips(anm, s, dy):
    """ANM (PS2 o HD) con las pistas de posicion de la cadera escaladas (x s) y subidas
    (+dy) en los #ACM/#AMM con el esqueleto del personaje (hijo 1 y los que tengan sus
    mismos nombres de hueso; el hijo 2, generico GOK_ de los agarres, no se toca)."""
    e, kids = amb_kids(anm)
    out = bytearray(anm)
    names = None
    for k, (o, size, _, _) in enumerate(kids):
        if k == 0 or not size:
            continue
        n, t, nb, no = struct.unpack_from(e + "4I", out, o + 0x10)
        nm = [bytes(out[o + no + 32 * j:o + no + 32 * j + 32]).split(b"\0")[0] for j in range(nb)]
        names = names or nm
        if nm != names:
            continue
        w = [j for j in range(nb) if suffix(nm[j].decode("latin1")) == "WAIST"]
        done = set()
        for a in range(n):
            flags, var, nf, off = struct.unpack_from(e + "4I", out, o + t + 16 * a)
            per = 3 if flags & 0x10 else 2
            for j in w if off else ():
                pp = struct.unpack_from(e + "I", out, o + off + 4 * (per * j + 1))[0]
                if not pp or pp in done:
                    continue
                done.add(pp)
                for q in range(struct.unpack_from(e + "I", out, o + pp + 8)[0]):
                    at = o + pp + 12 + 16 * q + 4
                    x, y, z = struct.unpack_from(e + "3f", out, at)
                    struct.pack_into(e + "3f", out, at, x * s, y * s + dy, z * s)
    return bytes(out)


def code_anim(anm, code):
    """Indice de la animacion del moveset (#ACM hijo 1) que usa el codigo, o None."""
    e, kids = amb_kids(anm)
    bsk = anm[kids[0][0]:kids[0][0] + kids[0][1]]
    n, lst = struct.unpack(e + "II", bsk[0x10:0x18])
    a0 = struct.unpack_from(e + "I", bsk, lst + 4 * code)[0] if code < n else 0
    if not a0:
        return None
    anim, pool = struct.unpack_from(e + "HH", bsk, a0)
    return anim if pool == 3 else None


# Codigos de moverse por el suelo con los pies apoyados en casi todos los nativos (RE
# 2026-10-10: 2 y 15-17 en el 80-100 % de los 34); en IW algunos levantan la cadera y el port
# "flota" al andar.
GROUND_CODES = (2, 15, 16, 17)


def ground_moves(anm, skel, ref=None, codes=GROUND_CODES, tol=0.06):
    """ANM con la cadera bajada en las animaciones de `codes` cuyo tobillo (fotograma 0) queda por
    encima del de reposo mas de tol * pierna; `ref` = (esqueleto, anm) del donante: solo los
    codigos que el tiene apoyados. -> (anm, {codigo: bajada})."""
    if len(amb_kids(anm)[1]) < 2:
        return anm, {}                        # forma que solo trae #BSK: usa las animaciones de la 1a
    L = leg(skel) or 0
    idle = idle_tracks(anm, 0)
    base = ankle(skel, idle) if idle else None
    if not L or base is None:
        return anm, {}
    if ref:
        r_idle = idle_tracks(ref[1], 0)
        r_base = ankle(ref[0], r_idle) if r_idle else None
        r_leg = leg(ref[0]) or 1
    fix = {}
    for code in codes:
        a = code_anim(anm, code)
        tr = idle_tracks(anm, code)
        if a is None or not tr or "WAIST" not in tr:
            continue
        if ref and r_base is not None:
            rt = idle_tracks(ref[1], code)
            ra = ankle(ref[0], rt) if rt else None
            if ra is None or abs(ra - r_base) > tol * r_leg:
                continue                      # el donante tampoco lo tiene apoyado
        up = (ankle(skel, tr) or base) - base
        if up > tol * L and a not in fix.values():
            fix[code] = (a, up)
    if not fix:
        return anm, {}
    e, kids = amb_kids(anm)
    o = kids[1][0]
    out = bytearray(anm)
    n, t, nb, no = struct.unpack_from(e + "4I", out, o + 0x10)
    w = [j for j in range(nb) if suffix(bytes(out[o + no + 32 * j:o + no + 32 * j + 32]).split(b"\0")[0]
                                       .decode("latin1")) == "WAIST"]
    users = {}                                # pista de posicion -> animaciones que la usan
    for a in range(n):
        flags, var, nf, off = struct.unpack_from(e + "4I", out, o + t + 16 * a)
        per = 3 if flags & 0x10 else 2
        for j in w if off else ():
            pp = struct.unpack_from(e + "I", out, o + off + 4 * (per * j + 1))[0]
            if pp:
                users.setdefault(pp, set()).add(a)
    done = {}
    for code, (a, up) in fix.items():
        flags, var, nf, off = struct.unpack_from(e + "4I", out, o + t + 16 * a)
        per = 3 if flags & 0x10 else 2
        for j in w if off else ():
            pp = struct.unpack_from(e + "I", out, o + off + 4 * (per * j + 1))[0]
            if not pp or users[pp] - {x for x, _ in fix.values()}:
                continue                      # pista compartida con otra animacion (reposo...)
            if pp in done:
                continue
            done[pp] = code
            for q in range(struct.unpack_from(e + "I", out, o + pp + 8)[0]):
                at = o + pp + 12 + 16 * q + 4
                x, y, z = struct.unpack_from(e + "3f", out, at)
                struct.pack_into(e + "3f", out, at, x, y - up, z)
    return bytes(out), {c: round(fix[c][1], 2) for c in set(done.values())}


ANKLES = ("LFOOT1", "RFOOT1")


def ankle(skel, tracks, hip=(1.0, 0.0)):
    P = pose(skel, tracks, hip)
    ys = [P[k][1] for k in ANKLES if k in P]
    return min(ys) if ys else None


def leg(skel):
    """Largo de pierna en reposo (cadera -> tobillo, vertical)."""
    P = pose(skel, None)
    if "WAIST" not in P or not any(k in P for k in ANKLES):
        return None
    return P["WAIST"][1] - min(P[k][1] for k in ANKLES if k in P)


def correccion(skel_dst, skel_src, idle, ref=None):
    """(escala, desplazamiento y) de la cadera para que `idle` (animacion hecha para
    skel_src) deje los tobillos de skel_dst a la altura relativa que tenian en skel_src.
    eANI2 (6/11): con `ref` = (esqueleto, reposo) de un nativo de B3 (el donante), el tobillo va a la altura de
    reposo de ESE nativo escalada al largo de pierna de skel_dst: la convencion de suelo de B3. Con la del juego de
    origen los importados quedaban hundidos (reposo en combate, tobillo: Goku B1 0,41, Freezer B2 -0,2/-0,7, Krillin
    SB -1,5, frente a 0,95-1,14 de los nativos) y el IK de suelo de B3 les doblaba las piernas en el 32-46 % de los
    cuadros (nativos 1-11 %). MEDIDO 6/11 con Goku B1 rehecho: el reposo sube 0,41 -> 0,88 pero el IK salta MAS (63 %):
    el IK no es "pie bajo el suelo" sino una banda junto al suelo, y el reposo de combate (clip 0x40000) no es el
    codigo 0 que mide `idle`. Por eso va APAGADO: DBZ3_ALTURA_B3=1 lo enciende para medir."""
    import os  # noqa: PLC0415
    l_dst, l_src = leg(skel_dst), leg(skel_src)
    if not (l_dst and l_src and idle and "WAIST" in idle and idle["WAIST"][1]):
        return 1.0, 0.0
    s = l_dst / l_src
    target = ankle(skel_src, idle) * s
    if ref and os.environ.get("DBZ3_ALTURA_B3"):
        l_ref, a_ref = leg(ref[0]), ankle(ref[0], ref[1]) if ref[1] else None
        if l_ref and a_ref is not None:
            target = a_ref * l_dst / l_ref
    now = ankle(skel_dst, idle, (s, 0.0))
    return s, target - now


def lift_waist(model, dy):
    """#AMB PS2 (o #AMO suelto) con la cadera (WAIST) del esqueleto en bind subida dy: las
    pistas de posicion se SUMAN al bind y los vertices van en el espacio de cada hueso, asi
    que todo el cuerpo sube dy en cada animacion (SSJ4 adulto de Goku GT con los golpes de
    nino: se hundia 3.4). None si no es un modelo PS2."""
    out = bytearray(model)
    amo = 0
    if model[:4] == b"#AMB":
        if not _is_ps2(model):
            return None
        n, t = struct.unpack("<II", model[0x10:0x18])
        amo = next((o for o, s, _, _ in (struct.unpack_from("<4I", model, t + 16 * k) for k in range(n))
                    if model[o:o + 4] == b"#AMO"), None)
        if amo is None:
            return None
    elif model[:4] != b"#AMO":
        return None
    g = model.find(b"#AMG", amo)
    nb, na = struct.unpack_from("<I", model, g + 0x10)[0], struct.unpack_from("<I", model, g + 0x1C)[0]
    for i in range(nb):
        nm = bytes(model[g + na + 32 * i:g + na + 32 * i + 32]).split(b"\0")[0].decode("latin1")
        if suffix(nm) == "WAIST":
            o = g + 0x20 + 80 * i + 20
            struct.pack_into("<f", out, o, struct.unpack_from("<f", out, o)[0] + dy)
            return bytes(out)
    return None


def hang_tails(model, deg=80.0, roots=("LOBI1", "ROBI1")):
    """Colas del cinturon (B1/B2/IW) colgadas en el bind: giro de `deg` en el eje X local de
    su raiz. Sin pista suya en el moveset (donante sin cinturon; B3 no aplica la fisica a los
    modelos nuevos) se quedaban en horizontal: dos hojas blancas junto a la mano (Fat Gotenks
    con los golpes de Trunks). Si el moveset si las anima, su pista sustituye este giro.
    Modelo PS2 (#AMB/#AMO) o HD; None si no tiene esas raices."""
    if model[:4] == b"#AMO" or _is_ps2(model):
        amo = 0
        if model[:4] == b"#AMB":
            n, t = struct.unpack("<II", model[0x10:0x18])
            amo = next((o for o, s, _, _ in (struct.unpack_from("<4I", model, t + 16 * k) for k in range(n))
                        if model[o:o + 4] == b"#AMO"), None)
            if amo is None:
                return None
        g = model.find(b"#AMG", amo)
        e, axes = "<", g + 0x20
        nb, names = struct.unpack_from("<I", model, g + 0x10)[0], g + struct.unpack_from("<I", model, g + 0x1C)[0]
    else:
        from awg_vertex_buffer import AwgVertexBuffer, be32  # noqa: PLC0415
        a = AwgVertexBuffer._parse(model)
        e, axes, nb, names = ">", a.awg0 + be32(a.data, a.awg0 + 0x14), a.bone_count(), a.awg0 + 0x40
    out, hit = bytearray(model), 0
    qx = (math.sin(math.radians(deg) / 2), 0.0, 0.0, math.cos(math.radians(deg) / 2))
    for i in range(nb):
        nm = bytes(model[names + 32 * i:names + 32 * i + 32]).split(b"\0")[0].decode("latin1")
        if suffix(nm) in roots:
            o = axes + 80 * i
            struct.pack_into(e + "4f", out, o, *qmul(struct.unpack_from(e + "4f", model, o), qx))
            hit += 1
    return bytes(out) if hit else None


if __name__ == "__main__":
    a = sys.argv[1:]
    m, anm = open(a[0], "rb").read(), open(a[1], "rb").read()
    sk, idle = skeleton(m), idle_tracks(anm)
    print("pierna %.2f  tobillo en reposo %.2f" % (leg(sk), ankle(sk, idle)))
    if len(a) >= 4:
        src = skeleton(open(a[2], "rb").read())
        idle_src = idle_tracks(open(a[3], "rb").read()) or idle
        print("origen: pierna %.2f tobillo %.2f -> correccion escala %.3f, y %+.2f" % (
            leg(src), ankle(src, idle_src), *correccion(sk, src, idle)))
