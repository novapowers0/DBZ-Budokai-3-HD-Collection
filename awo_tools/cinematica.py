#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""cinematica.py - Definitivas con animaciones propias sobre la cinematica de B3 del donante.

La definitiva de B3 (P+K+G+E en modo hiper -> 0x259 -> #SPX ranura 0) empuja codigos de ataque
0x4A0+ cuyo sub-bloque #CSK apunta a una animacion del #ACM propio (banco 3). El guion fija
cuanto dura cada codigo (espera) y la camara; la posicion de la cadera de cada animacion
coloca al personaje respecto al rival. Aqui NO se toca el guion: cada codigo elegido recibe
una animacion NUEVA hecha con un tramo de otra animacion (la de Shin Budokai, ya portada por
sbport con su numero) re-temporizada a la duracion del codigo, con:
  - giros de los huesos: los del tramo (la pose de SB);
  - posicion de la cadera: la de la animacion original del codigo (su sitio en la cinematica);
  - huesos raiz y de anclaje de efectos (BODY, NW, NRA...): los del original (helper()).
"sostener" congela la ultima pose: solo para tramos que acaban en reposo (la rafaga de SB a una
mano acaba torcida: en 0x4AF el modelo se veia roto 2 segundos, prueba en juego 2026-10-09).
Los codigos que no se nombran (p. ej. la version en la que el rival se defiende) quedan igual.

Receta (JSON o dict): {"0x4a0": [anim, desde, hasta], ...}; "hasta" < "desde" = al reves;
[anim, desde, hasta, "sostener"] = el tramo a su velocidad y despues se mantiene la ultima pose;
[[anim, desde, hasta], [anim2, desde2, hasta2], ...] = varios tramos seguidos en ese codigo (la
rafaga de golpes de una definitiva de Budokai 1: b1port.receta_definitiva la genera sola).

  python cinematica.py aplicar anm.bin receta.json --out anm2.bin      (HD o LZX)
  python cinematica.py prueba
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "mod center hd")]

ROT, POS = 8, 16          # bytes por clave: giro [frame u16][x y z u16] | posicion [frame u32][x y z f32]


def _keys(tr, kind):
    if not tr:
        return None, []
    a, ty, n = struct.unpack_from(">3I", tr, 0)
    fmt, ks = (">4H", ROT) if kind == "rot" else (">I3f", POS)
    return (a, ty), [list(struct.unpack_from(fmt, tr, 12 + ks * k)) for k in range(n)]


def _track(head, keys, kind):
    fmt = ">4H" if kind == "rot" else ">I3f"
    return struct.pack(">3I", head[0], head[1], len(keys)) + b"".join(struct.pack(fmt, *k) for k in keys)


def _rot_at(keys, f):
    """Euler u16 en el frame f (vuelta mas corta por componente)."""
    if f <= keys[0][0]:
        return keys[0][1:]
    for a, b in zip(keys, keys[1:]):
        if a[0] <= f <= b[0]:
            t = (f - a[0]) / ((b[0] - a[0]) or 1)
            return [int(round(x + (((y - x + 32768) % 65536) - 32768) * t)) % 65536 for x, y in zip(a[1:], b[1:])]
    return keys[-1][1:]


def _src_frame(d, n, a, b, hold):
    if hold:
        return min(a + d, b) if b >= a else max(a - d, b)
    return a + (b - a) * d / max(1, n - 1)


def helper(bone):
    """Hueso raiz o de anclaje de efectos (BODY, NW, NRA, NLA, NRF, NLF...): el guion de la
    cinematica lanza la energia desde ellos (con la pista de SB el Burning Attack del final se
    quedaba en las manos, prueba en juego 2026-10-09)."""
    return bone == "BODY" or (bone[:1] == "N" and bone != "NECK" and len(bone) <= 4)


def tramo(src, dst, a, b, hold=False):
    """Animacion nueva (formato de roster_build.acm_read) de dst['frames'] frames: giros de src
    entre los frames a..b, posiciones de dst."""
    fl_s, _, nf_s, bones_s = src
    fl_d, var_d, n, bones_d = dst
    a, b = max(0, min(a, nf_s - 1)), max(0, min(b, nf_s - 1))
    out = {}
    for bone in set(bones_s or {}) | set(bones_d or {}):
        rs = (bones_s or {}).get(bone) or [None, None]
        rd = (bones_d or {}).get(bone) or [None, None]
        if helper(bone):                    # raiz y puntos de anclaje: los del original
            out[bone] = list(rd)
            continue
        rot = None
        head, keys = _keys(rs[0], "rot")
        if keys:
            rot = _track(head, [[d] + _rot_at(keys, _src_frame(d, n, a, b, hold)) for d in range(n)], "rot")
        elif rd[0]:
            rot = rd[0]
        out[bone] = [rot, rd[1]]            # posicion: la del codigo original (su sitio)
    return (fl_d & ~0x10, var_d, n, out)


def tramo_multi(segs, dst):
    """Como tramo, pero con varios tramos seguidos [(src, a, b), ...] (p. ej. la rafaga de 16
    golpes de una definitiva de Budokai 1) re-temporizados juntos a los frames de dst."""
    fl_d, var_d, n, bones_d = dst
    segs = [(s, max(0, min(a, s[2] - 1)), max(0, min(b, s[2] - 1))) for s, a, b in segs]
    lens = [abs(b - a) + 1 for _, a, b in segs]
    total = sum(lens)
    place = []                       # frame destino -> (tramo, frame de su origen)
    for d in range(n):
        v = d * (total - 1) / max(1, n - 1)
        k = 0
        while k < len(lens) - 1 and v >= lens[k]:
            v -= lens[k]
            k += 1
        _, a, b = segs[k]
        place.append((k, a + v if b >= a else a - v))
    names = set(bones_d or {})
    for s, _, _ in segs:
        names |= set(s[3] or {})
    out = {}
    for bone in names:
        rd = (bones_d or {}).get(bone) or [None, None]
        if helper(bone):
            out[bone] = list(rd)
            continue
        per = [_keys(((s[3] or {}).get(bone) or [None])[0], "rot") for s, _, _ in segs]
        if not any(keys for _, keys in per):
            out[bone] = [rd[0], rd[1]]
            continue
        _, dkeys = _keys(rd[0], "rot")
        head = next(h for h, keys in per if keys)
        keys = []
        for d, (k, f) in enumerate(place):
            src = per[k][1] or dkeys          # hueso que ese tramo no mueve: el del original
            keys.append([d] + (_rot_at(src, f) if src else [0, 0, 0]))
        out[bone] = [_track(head, keys, "rot"), rd[1]]
    return (fl_d & ~0x10, var_d, n, out)


def aplicar(anm, receta):
    """anm HD (#AMB [#CSK, #ACM x3]) -> anm HD con los codigos de la receta re-animados.
    -> (bin, informe)."""
    import roster_build as rb  # noqa: PLC0415
    from sb_tecnicas import amb_hd, kids  # noqa: PLC0415
    ks = list(kids(anm, ">"))
    ci = next(i for i, (x, _) in enumerate(ks) if x[:4] == b"#CSK")
    ai = next(i for i, (x, t) in enumerate(ks) if t == 3)
    csk, acm = bytearray(ks[ci][0]), ks[ai][0]
    _, anims = rb.acm_read(acm)
    n_codes, lst = struct.unpack_from(">II", csk, 0x10)
    new, rep = [], []
    receta = {k: v for k, v in receta.items() if not str(k).startswith("_")}     # "_": notas
    for code_s, spec in sorted(receta.items(), key=lambda kv: int(str(kv[0]), 0)):
        code = int(str(code_s), 0)
        multi = isinstance(spec[0], (list, tuple))           # [[anim, desde, hasta], ...]
        segs = [tuple(int(x) for x in sp[:3]) for sp in spec] if multi else [tuple(int(x) for x in spec[:3])]
        hold = not multi and len(spec) > 3 and spec[3] == "sostener"
        ad = struct.unpack_from(">I", csk, lst + 4 * code)[0] if code < n_codes else 0
        if not ad:
            raise ValueError("codigo %#x sin sub-bloque en el #CSK" % code)
        cur, bank = struct.unpack_from(">HH", csk, ad)
        if bank != 3 or cur >= len(anims) or anims[cur][3] is None:
            raise ValueError("codigo %#x: animacion %d del banco %d (solo el propio, 3)" % (code, cur, bank))
        for src, _, _ in segs:
            if src >= len(anims) or anims[src][3] is None:
                raise ValueError("animacion de origen %d inexistente" % src)
        if multi:
            new.append(tramo_multi([(anims[s], a, b) for s, a, b in segs], anims[cur]))
        else:
            src, a, b = segs[0]
            new.append(tramo(anims[src], anims[cur], a, b, hold))
        struct.pack_into(">H", csk, ad, len(anims) + len(new) - 1)
        rep.append("%#x: anim %d (%d f) -> %s%s" % (
            code, cur, anims[cur][2], ", ".join("tramo %d-%d de la anim %d" % (a, b, s) for s, a, b in segs),
            " (y se sostiene)" if hold else ""))
    ks[ci] = (bytes(csk), ks[ci][1])
    ks[ai] = (bytes(rb.acm_append(acm, new)), ks[ai][1])
    return amb_hd(ks), rep


def prueba():
    """Sobre el moveset de Trunks de B3 (ID 8, data_cmn 399): un codigo re-animado con otra
    animacion conserva su duracion y su cadera, y el resto de codigos no cambia."""
    import afs_pair  # noqa: PLC0415
    import roster_build as rb  # noqa: PLC0415
    from sb_tecnicas import kids  # noqa: PLC0415
    anm = afs_pair.decompress(afs_pair.entry(os.path.join(ROOT, "us", "data_cmn.afs"), 399), "hd399")
    out, rep = aplicar(anm, {"0x4a0": [71, 0, 97], "0x4af": [72, 0, 40, "sostener"],
                             "0x4a1": [[71, 0, 20], [72, 10, 30], [73, 0, 5]]})

    def info(b):
        ks = kids(b, ">")
        csk = next(x for x, _ in ks if x[:4] == b"#CSK")
        _, an = rb.acm_read(next(x for x, t in ks if t == 3))
        lst = struct.unpack_from(">I", csk, 0x14)[0]
        return {c: an[struct.unpack_from(">H", csk, struct.unpack_from(">I", csk, lst + 4 * c)[0])[0]]
                for c in (0x4a0, 0x4a1, 0x4a2, 0x4af)}
    o, n = info(anm), info(out)
    assert n[0x4a0][2] == o[0x4a0][2] == 44 and n[0x4af][2] == o[0x4af][2]
    assert n[0x4a0][3]["WAIST"][1] == o[0x4a0][3]["WAIST"][1]          # cadera en su sitio
    assert n[0x4a0][3]["CHEST"][0] != o[0x4a0][3]["CHEST"][0]          # pose nueva
    assert n[0x4a1][2] == o[0x4a1][2] and n[0x4a1][3]["CHEST"][0] != o[0x4a1][3]["CHEST"][0]   # varios tramos
    assert n[0x4a1][3]["WAIST"][1] == o[0x4a1][3]["WAIST"][1]
    assert n[0x4a2] == o[0x4a2]                                          # lo demas igual
    print("prueba OK:", "; ".join(rep))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("aplicar")
    p.add_argument("anm")
    p.add_argument("receta")
    p.add_argument("--out", required=True)
    sub.add_parser("prueba")
    a = ap.parse_args()
    if a.cmd == "prueba":
        return prueba()
    import tempfile  # noqa: PLC0415
    import roster_build as rb  # noqa: PLC0415
    anm = rb.load_bin(a.anm, tempfile.mkdtemp())
    out, rep = aplicar(anm, json.load(open(a.receta, encoding="utf-8")))
    open(a.out, "wb").write(out)
    print("\n".join(rep))
    return None


if __name__ == "__main__":
    main()
