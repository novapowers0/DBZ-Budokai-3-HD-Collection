#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pies.py - Pies de los personajes importados (B1/B2/SB) frente al ajuste de pies de B3 (eANI3/eANI4, 6/11).

RE (sub_8211B1F0): el IK de pies de B3 mide los NULOS XNNN_NLF/NRF (no el tobillo). Con r8 = [[X+2996]+8] (la tabla de
cuerpo del donante; ~ altura del nulo de un nativo en reposo + 0,08), si el nulo queda a mas de 0,1 de r8 el IK sube o
baja el pie y dobla la pierna (importados: LEGROT fuera de pista en el 45-70 % de los cuadros; nativos 1-2 %).
Dos causas medidas en los importados:
  1. en los modelos PS2 de B1/B2 el nulo va en la planta (Goku B1 y local 0,83 en FOOT1; el nativo 0,22):
     `nulos` lo pone en la misma fraccion del pie (FOOT1 -> FOOT2) que el donante;
  2. el cuerpo entero queda bajo: `dy_cadera` mide, fuera de linea (cinematica directa, sin ejecutar el juego), la
     altura del nulo del modelo con su reposo y la del donante con el suyo, y `altura.lift_waist` sube la cadera del bind
     la diferencia (valores iguales a los medidos en juego: Goku B1 +0,252 / +0,25, Freezer B2 +0,979 / +0,98).
     Reposo: codigo 0 del moveset si es del #AMM propio; si no (Freezer: banco global) la animacion 0 del #AMM global
     de data_cmn (entrada 165, 46 huesos GOK_).
Idempotente: con el modelo ya corregido, dy ~ 0 y los nulos ya estan en la fraccion del donante.
Interruptor: `pies = false` en el personaje.toml del mod, o DBZ3_SIN_PIES=1 (roster_build lo respeta).

uso: python pies.py <modelo_mod> <donante_modelo> [anm_mod anm_donante]   (muestra nulos y dy; no escribe)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import altura  # noqa: E402

GLOBAL_AMM = 165          # data_cmn: #AMB con el #AMM global (banco 0 de B3), el mismo indice en PS2 y HD


def _amg(model):
    """(offset del #AMG, n huesos, offset de nombres) de un #AMB PS2 o #AMO suelto; None si no es PS2."""
    amo = 0
    if model[:4] == b"#AMB":
        if not altura._is_ps2(model):
            return None
        n, t = struct.unpack("<II", model[0x10:0x18])
        amo = next((o for o, s, _, _ in (struct.unpack_from("<4I", model, t + 16 * k) for k in range(n))
                    if model[o:o + 4] == b"#AMO"), None)
        if amo is None:
            return None
    elif model[:4] != b"#AMO":
        return None
    g = model.find(b"#AMG", amo)
    return g, struct.unpack_from("<I", model, g + 0x10)[0], g + struct.unpack_from("<I", model, g + 0x1C)[0]


def _ejes(model):
    """{sufijo: offset de la posicion local (3 floats) del eje de 80 B}."""
    g, nb, na = _amg(model)
    out = {}
    for i in range(nb):
        nm = bytes(model[na + 32 * i:na + 32 * i + 32]).split(b"\0")[0].decode("latin1")
        out.setdefault(altura.suffix(nm), g + 0x20 + 80 * i + 16)
    return out


def nulos(model, donor):
    """Modelo PS2 con XNNN_NLF/NRF en la misma fraccion del pie (FOOT1 -> FOOT2, local) que el donante.
    -> (modelo, {nulo: (antes, despues)}); sin cambios si no es PS2 o faltan huesos."""
    if not _amg(model):
        return model, {}
    sk_d = {altura.suffix(n): p for n, q, p, par in altura.skeleton(donor)}
    ej = _ejes(model)
    out = bytearray(model)
    hecho = {}
    for lado in "LR":
        nulo, toe = "N%sF" % lado, "%sFOOT2" % lado
        if nulo not in ej or toe not in ej or nulo not in sk_d or toe not in sk_d:
            continue
        nd, td = sk_d[nulo], sk_d[toe]
        tm = struct.unpack_from("<3f", out, ej[toe])
        antes = struct.unpack_from("<3f", out, ej[nulo])
        nuevo = tuple(nd[k] * (tm[k] / td[k]) if abs(td[k]) > 1e-4 else antes[k] for k in range(3))
        struct.pack_into("<3f", out, ej[nulo], *nuevo)
        hecho[nulo] = (tuple(round(x, 3) for x in antes), tuple(round(x, 3) for x in nuevo))
    return bytes(out), hecho


def global_idle(amb165):
    """Pistas de la animacion 0 del #AMM global de data_cmn (entrada 165, PS2 o HD); None si no cuadra."""
    try:
        e, kids = altura.amb_kids(amb165)
        o, s = kids[0][0], kids[0][1]
        if amb165[o:o + 4] not in (b"#AMM", b"#ACM"):
            return None
        return altura.anim_tracks(amb165[o:o + s], 0, e)
    except (struct.error, IndexError):
        return None


def reposo(anm, glob=None):
    """Pistas del reposo de un moveset: codigo 0 si es del #AMM propio; si no el reposo global `glob`."""
    try:
        return altura.idle_tracks(anm, 0) or glob
    except (struct.error, IndexError):
        return glob


def nulo_y(skel, tracks):
    """Altura media de los nulos NLF/NRF con `tracks` (cinematica directa); None si faltan."""
    if not tracks:
        return None
    P = altura.pose(skel, tracks)
    ys = [P[k][1] for k in ("NLF", "NRF") if k in P]
    return sum(ys) / len(ys) if ys else None


def corregir(model, donor, tracks, d_tracks, lift=True, tope=4.0):
    """Modelo PS2 con los nulos como el donante y, si `lift` y hay reposos, la cadera subida lo que falte para que el
    nulo en reposo quede a la altura del nulo del donante en el suyo. -> (modelo, informe str | None)."""
    if not _amg(model):
        return model, None
    new, hecho = nulos(model, donor)
    if not hecho:
        return model, None
    info = "nulos NLF/NRF como el donante"
    a, b = nulo_y(altura.skeleton(new), tracks), nulo_y(altura.skeleton(donor), d_tracks)
    if lift and a is not None and b is not None:
        dy = b - a
        if abs(dy) > tope:
            return new, info + "; cadera sin ajustar (dy %.2f fuera de rango)" % dy
        if abs(dy) >= 0.02:
            fixed = altura.lift_waist(new, dy)
            if fixed:
                new = fixed
                info += "; cadera %+.3f" % dy
    return new, info


def var_como_donante(anm, d_anm, valor=10):
    """ANM de un port de SB con el campo `var` de la cabecera de sus animaciones (16 B por animacion del #AMM propio:
    flags, var, cuadros, datos) a `valor` en los codigos donde el donante lo tiene y el port no. Medido (eANI4): el
    reposo de Krillin SB (codigo 0, var 0 en el port y 10 en el nativo) quedaba 1,95 por debajo de r8 en combate y con
    var 10 en -0,07 (nativo -0,08); sbport copiaba el var de unos codigos (0x280...) y no de otros (0, 2, 0x3C8-0x3CF).
    Una animacion compartida con un codigo que el donante no tiene en `valor` se deja. -> (anm, [codigos])."""
    def codigos(b):
        e, kids = altura.amb_kids(b)
        bsk = b[kids[0][0]:kids[0][0] + kids[0][1]]
        n, lst = struct.unpack(e + "II", bsk[0x10:0x18])
        out = {}
        for c in range(n):
            a0 = struct.unpack_from(e + "I", bsk, lst + 4 * c)[0]
            if a0:
                an, pool = struct.unpack_from(e + "HH", bsk, a0)
                if pool == 3:
                    out[c] = an
        return e, kids, out
    try:
        e, kids, mine = codigos(anm)
        de, dk, theirs = codigos(d_anm)
        if len(kids) < 2 or len(dk) < 2:
            return anm, []
        o, od = kids[1][0], dk[1][0]
        t, td = struct.unpack_from(e + "I", anm, o + 0x14)[0], struct.unpack_from(de + "I", d_anm, od + 0x14)[0]
        dvar = {c: struct.unpack_from(de + "I", d_anm, od + td + 16 * a + 4)[0] for c, a in theirs.items()}
        users = {}
        for c, a in mine.items():
            users.setdefault(a, set()).add(c)
        out, hecho = bytearray(anm), []
        for c, a in sorted(mine.items()):
            if dvar.get(c) != valor or struct.unpack_from(e + "I", out, o + t + 16 * a + 4)[0] == valor:
                continue
            if any(dvar.get(u) != valor for u in users[a]):
                continue
            struct.pack_into(e + "I", out, o + t + 16 * a + 4, valor)
            hecho.append(c)
        return bytes(out), hecho
    except (struct.error, IndexError):
        return anm, []


def main(a):
    m, d = open(a[0], "rb").read(), open(a[1], "rb").read()
    tr = altura.idle_tracks(open(a[2], "rb").read()) if len(a) > 2 else None
    dt = altura.idle_tracks(open(a[3], "rb").read()) if len(a) > 3 else None
    new, info = corregir(m, d, tr, dt)
    print(info, "| nulo antes", nulo_y(altura.skeleton(m), tr), "despues", nulo_y(altura.skeleton(new), tr),
          "donante", nulo_y(altura.skeleton(d), dt))


if __name__ == "__main__":
    main(sys.argv[1:])
