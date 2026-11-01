#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Materiales EMM (XV1/XV2): nombres, shader y parametros tipados, sin convertir arte.

Layout contrastado con XV2-Tools, Xv2CoreLib/EMM/{Parser,EMM_File}.cs (MIT).
No evalua shaders ni aplica tintes: conserva tambien el tipo y los bits originales.
  python -B awo_tools/xeno_emm.py --selftest
  python -B awo_tools/xeno_emm.py --cpk data.cpk --entry data/chara/GOK/GOK_000_Bust.emm
"""
import struct
import sys

from xeno_binary import Reader, reader_main, rejects

TYPES = {0: "float", 1: "bool", 0x10000: "float2", 0x10001: "int"}


def leer_emm(data):
    """Lista de materiales (None = hueco); Float2 es un escalar f32, no un vec2."""
    r = Reader(data, "EMM")
    r.header(b"#EMM", 16)
    base = r.u32(12)
    count = r.u32(base)
    materials = []
    for index, entry in enumerate(r.array(base + 4, count, 4)):
        rel = r.u32(entry)
        if not rel:
            materials.append(None)
            continue
        offset = base + rel
        r.span(offset, 68)
        parameters = []
        for p in r.array(offset + 68, r.u16(offset + 64), 40):
            typ, raw = r.u32(p + 32), r.u32(p + 36)
            value = r.unpack("<f", p + 36)[0] if typ in (0, 0x10000) else r.i32(p + 36)
            if typ == 1 and value in (0, 1):
                value = bool(value)
            parameters.append(dict(name=r.text(p, 32), type=typ,
                                   type_name=TYPES.get(typ, "unknown"), value=value, raw=raw))
        materials.append(dict(index=index, name=r.text(offset, 32), shader=r.text(offset + 32, 32),
                              unknown_66=r.u16(offset + 66), parameters=parameters))
    return materials


def _fixture():
    data = bytearray(32 + 12 + 68 + 6 * 40)
    struct.pack_into("<4sHHII", data, 0, b"#EMM", 0xFFFE, 16, 16, 32)
    struct.pack_into("<III", data, 32, 2, 12, 0)
    data[44:48], data[76:81] = b"Body", b"TOON\0"
    struct.pack_into("<HH", data, 44 + 64, 6, 7)
    for i, (name, typ, value) in enumerate((("MatCol0R", 0, 0.25), ("Enabled", 1, 1),
                                           ("Index", 0x10001, -3), ("Scale", 0x10000, 1.5),
                                           ("Future", 99, -5), ("BoolRaw", 1, 2))):
        p = 112 + 40 * i
        data[p:p + len(name)] = name.encode("ascii")
        struct.pack_into("<I", data, p + 32, typ)
        struct.pack_into("<f" if typ in (0, 0x10000) else "<i", data, p + 36, value)
    return bytes(data)


def selftest():
    data = _fixture()
    m, hole = leer_emm(data)
    assert hole is None and m["name"] == "Body" and m["shader"] == "TOON"
    assert m["unknown_66"] == 7
    assert [p["value"] for p in m["parameters"]] == [0.25, True, -3, 1.5, -5, 2]
    assert m["parameters"][4]["raw"] == 0xFFFFFFFB
    empty = bytearray(20)
    struct.pack_into("<4sHHII", empty, 0, b"#EMM", 0xFFFE, 16, 0, 16)
    assert leer_emm(empty) == []
    for bad in (b"", b"FAIL" + data[4:], data[:4] + b"\xff\xfe" + data[6:], data[:-1]):
        rejects(lambda bad=bad: leer_emm(bad))
    bad = bytearray(data)
    struct.pack_into("<I", bad, 36, 0xFFFFFFFC)
    rejects(lambda: leer_emm(bad))
    print("xeno_emm: N0 OK (tipos, huecos, cabeceras 16/32, truncados, offsets)")


if __name__ == "__main__":
    sys.exit(reader_main(leer_emm, selftest, "EMM"))
