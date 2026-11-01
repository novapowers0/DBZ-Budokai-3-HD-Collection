#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BCS XV1/XV2: piezas/CaC, selectores de tinte, paletas, fisicas y escalas.

Layout contrastado con XV2-Tools, Xv2CoreLib/BCS/Parser.cs (MIT), no importa su codigo.
No resuelve ni exporta modelos: rutas vacias usan las convenciones del juego.
  python -B awo_tools/xeno_bcs.py --selftest
  python -B awo_tools/xeno_bcs.py --cpk data.cpk --entry data/chara/GOK/GOK.bcs
"""
import struct
import sys

from xeno_binary import Reader, reader_main, rejects

PART_TYPES = ("Face_base", "Face_forehead", "Face_eye", "Face_nose", "Face_ear",
              "Hair", "Bust", "Pants", "Rist", "Boots")


def leer_bcs(data):
    r = Reader(data, "BCS")
    r.span(0, 8)
    unsigned = data[:8] == b"\0" * 8
    if not unsigned:
        r.header(b"#BCS", 8)
    header = r.u16(6) if not unsigned else 76
    if header not in (72, 76):
        raise ValueError("BCS: version de cabecera desconocida (%d)" % header)
    r.span(0, header)
    xv2 = header == 76
    ns, nc, nb = r.unpack("<3H", 12)
    sets, colors, bodies = r.unpack("<3I", 24 if xv2 else 20)
    rg = 44 if xv2 else 32
    result = dict(version="XV2" if xv2 else "XV1", signature_missing=unsigned,
                  race=r.span(rg, 1)[0], gender=r.span(rg + 1, 1)[0],
                  header_values=list(r.unpack("<7f", 48 if xv2 else 36)),
                  part_sets=[], colors=[], bodies=[], skeletons=[])

    def physics(p):
        r.span(p, 72)
        return dict(model=r.i16(p), model2=r.i16(p + 2), texture=r.i16(p + 4),
                    flags=r.u32(p + 24), hide_flags=r.u32(p + 28), hide_materials=r.u32(p + 32),
                    chara=r.text(p + 36, 4),
                    paths={key: r.relative_text(p, field) for key, field in
                           (("emd", 40), ("emm", 44), ("emb", 48), ("ean", 52),
                            ("bone", 56), ("scd", 60))})

    def part(p, slot):
        r.span(p, 88 if xv2 else 80)
        ncol, nphys = r.u16(p + 18), r.u16(p + 74)
        selectors = [dict(group=r.u16(q), color=r.u16(q + 2)) for q in
                     r.array(p + r.u32(p + 20), ncol, 4)]
        phys = [physics(q) for q in r.array(p + r.u32(p + 76), nphys, 72)]
        unk = [list(r.unpack("<6h", q)) for q in
               r.array(p + r.u32(p + 84), r.u16(p + 82), 12)] if xv2 else []
        return dict(type=PART_TYPES[slot], model=r.i16(p), model2=r.i16(p + 2),
                    texture=r.i16(p + 4), shader=r.i16(p + 16), flags=r.u32(p + 24),
                    hide_flags=r.u32(p + 28), hide_materials=r.u32(p + 32),
                    values=list(r.unpack("<2f2i", p + 36)), chara=r.text(p + 52, 4),
                    paths={key: r.relative_text(p, field) for key, field in
                           (("emd", 56), ("emm", 60), ("emb", 64), ("ean", 68))},
                    color_selectors=selectors, physics=phys, unknown_3=unk)

    for i, q in enumerate(r.array(sets, ns, 4)):
        p = r.u32(q)
        if not p:
            continue
        r.span(p, 28)
        if r.u32(p + 20) != 10:
            raise ValueError("BCS: conjunto %d no tiene 10 partes" % i)
        table = r.array(p + r.u32(p + 24), 10, 4)
        result["part_sets"].append(dict(id=i, parts=[part(p + r.u32(t), slot) if r.u32(t) else None
                                                    for slot, t in enumerate(table)]))
    for i, q in enumerate(r.array(colors, nc, 4)):
        p = r.u32(q)
        if not p:
            continue
        r.span(p, 16)
        entries = []
        for color, c in enumerate(r.array(p + r.u32(p + 12), r.u16(p + 10), 80)):
            entries.append(dict(id=color, rgba=[list(r.unpack("<4f", c + k * 16)) for k in range(4)],
                                unknown=list(r.unpack("<4f", c + 64))))
        result["colors"].append(dict(id=i, name=r.relative_text(p, 0), entries=entries))
    for i, q in enumerate(r.array(bodies, nb, 4)):
        p = r.u32(q)
        if not p:
            continue
        r.span(p, 8)
        scales = [dict(bone=r.relative_text(c, 12), scale=list(r.unpack("<3f", c))) for c in
                  r.array(p + r.u32(p + 4), r.u16(p + 2), 16)]
        result["bodies"].append(dict(id=i, scales=scales))

    def skeleton(p, base):
        r.span(p, 8)
        bones = []
        for c in r.array(base + r.u32(p + 4), r.u16(p + 2), 52):
            bones.append(dict(bone=r.relative_text(c, 48 if xv2 else 12),
                              unknown=list(r.unpack("<2i", c)),
                              values=list(r.unpack("<9f", c + (12 if xv2 else 16)))))
        return dict(unknown=r.u16(p), bones=bones)

    if xv2:
        for field in (40, 36):
            table = r.u32(field)
            p = r.u32(table) if table else 0
            result["skeletons"].append(skeleton(p, p) if p else None)
    else:
        result["skeletons"].append(skeleton(64, 32))
    return result


def _fixture(xv2=True):
    b = bytearray(860)
    struct.pack_into("<4sHH", b, 0, b"#BCS", 0xFFFE, 76 if xv2 else 72)
    struct.pack_into("<3H", b, 12, 2, 2, 1)
    struct.pack_into("<3I", b, 24 if xv2 else 20, 80, 88, 96)
    b[44 if xv2 else 32] = 1
    struct.pack_into("<IIIII", b, 80, 0, 100, 0, 480, 600)
    struct.pack_into("<II", b, 120, 10, 28)
    struct.pack_into("<I", b, 128 + 6 * 4, 68)  # Bust en conjunto ID=1
    struct.pack_into("<3h", b, 168, 7, -1, 4)
    struct.pack_into("<hHI", b, 184, 2, 1, 88)
    b[220:224] = b"HUM\0"
    struct.pack_into("<I", b, 224, 180)
    struct.pack_into("<HI", b, 242, 1, 92)
    if xv2:
        struct.pack_into("<HI", b, 250, 1, 164)
        struct.pack_into("<6h", b, 332, 1, 2, 3, 4, 5, 6)
    struct.pack_into("<HH", b, 256, 1, 2)
    struct.pack_into("<3h", b, 260, 8, 9, 10)
    b[296:300] = b"HUM\0"
    struct.pack_into("<I", b, 316, 100)
    b[348:356], b[360:366] = b"HUM_Bust", b"b_C_00"
    struct.pack_into("<I", b, 480, 128)
    struct.pack_into("<HI", b, 490, 1, 16)
    struct.pack_into("<20f", b, 496, *([0.25, 0.5, 0.75, 1.0] * 5))
    struct.pack_into("<HI", b, 602, 1, 24)
    b[608:613] = b"SKIN\0"
    struct.pack_into("<3fI", b, 624, 1.0, 2.0, 3.0, 24)
    b[648:655] = b"b_C_00\0"
    if xv2:
        struct.pack_into("<II", b, 36, 696, 700)
        struct.pack_into("<II", b, 696, 704, 776)
        for p in (704, 776):
            struct.pack_into("<HHI", b, p, 3, 1, 8)
            struct.pack_into("<I", b, p + 8 + 48, 52)
            b[p + 60:p + 67] = b"b_C_00\0"
    return bytes(b)


def selftest():
    for xv2 in (False, True):
        b = _fixture(xv2)
        result = leer_bcs(b)
        assert result["version"] == ("XV2" if xv2 else "XV1")
        s = result["part_sets"][0]
        assert s["id"] == 1 and s["parts"][0] is None
        p = s["parts"][6]
        assert (p["type"], p["model"], p["model2"], p["chara"]) == ("Bust", 7, -1, "HUM")
        assert p["paths"]["emd"] == "HUM_Bust" and p["paths"]["emm"] == ""
        assert p["color_selectors"] == [dict(group=1, color=2)]
        assert p["physics"][0]["paths"]["bone"] == "b_C_00"
        assert result["colors"][0]["id"] == 1 and result["colors"][0]["name"] == "SKIN"
        assert result["colors"][0]["entries"][0]["rgba"][1] == [0.25, 0.5, 0.75, 1.0]
        assert result["bodies"][0]["scales"] == [dict(bone="b_C_00", scale=[1.0, 2.0, 3.0])]
        if xv2:
            assert p["unknown_3"] == [[1, 2, 3, 4, 5, 6]]
            assert all(s["bones"][0]["bone"] == "b_C_00" for s in result["skeletons"])
            assert leer_bcs(b"\0" * 8 + b[8:])["signature_missing"]
        rejects(lambda: leer_bcs(b[:160]))
        bad = bytearray(b)
        struct.pack_into("<I", bad, 120, 11)
        rejects(lambda: leer_bcs(bad))
        struct.pack_into("<I", bad, 120, 10)
        struct.pack_into("<I", bad, 224, 0xFFFFFF00)
        rejects(lambda: leer_bcs(bad))
    for bad in (b"", b"FAIL" + b[4:], b[:6] + b"\x50\0" + b[8:]):
        rejects(lambda bad=bad: leer_bcs(bad))
    print("xeno_bcs: N0 OK (XV1/XV2, huecos/IDs, piezas, tintes, fisicas, escalas, esqueletos, invalidos)")


if __name__ == "__main__":
    sys.exit(reader_main(leer_bcs, selftest, "BCS"))
