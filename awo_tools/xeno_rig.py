#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Informe ESK XV2 -> huesos B3: candidatos semanticos, NO retarget ni port jugable.

Solo coincidencias anatomicas explicitas; dedos/fisica/cara no se inventan.
Los no casados pueden informar del ancestro casado, separado de los matches.
Lee XV2 en memoria y B3 de su AFS; deja temporales B3 solo en --trabajo.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

from xeno_binary import Reader, rejects, write_json
from xeno_model import Cpk, Esk

ALIASES = {"b_C_Base": "BODY", "b_C_Pelvis": "WAIST", "b_C_Spine1": "STMC",
           "b_C_Chest": "CHEST", "b_C_Neck1": "NECK", "b_C_Head": "HEAD", "f_C_Jaw": "M_JAW"}
for _side in ("L", "R"):
    for _source, _target in (("Shoulder", "CHN"), ("Arm1", "ARM1"), ("Arm2", "ARM2"),
                              ("Hand", "HAND"), ("Leg1", "LEG1"), ("Leg2", "LEG2"),
                              ("Foot", "FOOT1"), ("Toe", "FOOT2")):
        ALIASES["b_%s_%s" % (_side, _source)] = ("L00_%sHAND" % _side if _source == "Hand"
                                                   else _side + _target)


def leer_b3_labels(data):
    r = Reader(data, "B3 #AMB/#AWO")
    if r.span(0, 4) != b"#AMB":
        raise ValueError("B3: hace falta #AMB descomprimido")
    u32 = lambda p: r.unpack(">I", p)[0]
    for entry in r.array(u32(20), u32(16), 16):
        offset, size = r.unpack(">II", entry)
        block = r.span(offset, size)
        if block[:4] != b"#AWO":
            continue
        r.span(offset, 0x28)
        table = offset + u32(offset + 0x1C)
        g = offset + u32(table)
        if r.span(g, 4) != b"#AWG":
            raise ValueError("B3: AWG principal invalido")
        names = g + u32(g + 0x1C)
        return [r.text(p, 32) for p in r.array(names, u32(g + 0x10), 32)]
    raise ValueError("B3: no hay #AWO en el #AMB")


def mapear(names, parents, labels):
    if len(names) != len(parents):
        raise ValueError("ESK: nombres/padres incompatibles")
    by_suffix = {}
    for i, label in enumerate(labels):
        suffix = label.split("_", 1)[-1]
        by_suffix.setdefault(suffix, []).append(i)
    direct = {}
    for i, name in enumerate(names):
        candidates = by_suffix.get(ALIASES.get(name, ""), [])
        if len(candidates) == 1:
            direct[i] = candidates[0]
    matches, unmatched = [], []
    for i, name in enumerate(names):
        if i in direct:
            matches.append(dict(source_index=i, source=name, target_index=direct[i], target=labels[direct[i]],
                                basis="[H] anatomical_alias_candidate"))
            continue
        visited, p = {i}, parents[i]
        while p != -1 and p not in direct:
            if p < 0 or p >= len(names) or p in visited:
                raise ValueError("ESK: padre invalido o ciclo")
            visited.add(p)
            p = parents[p]
        unmatched.append(dict(source_index=i, source=name,
                              ancestor=names[p] if p in direct else None,
                              ancestor_target=labels[direct[p]] if p in direct else None))
    used_targets = set(direct.values())
    return dict(status="semantic_candidates_not_retarget", source_bones=len(names), target_bones=len(labels),
                matched=len(matches), unmatched=len(unmatched), matches=matches, missing=unmatched,
                target_unmatched=[label for i, label in enumerate(labels) if i not in used_targets])


def extraer_b3(afs, index, decoder, work):
    with open(afs, "rb") as source:
        head = source.read(8)
        if len(head) != 8 or head[:4] != b"AFS\0":
            raise ValueError("B3: cabecera AFS invalida")
        count = struct.unpack_from("<I", head, 4)[0]
        if not 0 <= index < count:
            raise ValueError("B3: entrada AFS fuera de rango")
        source.seek(8 + 8 * index)
        entry = source.read(8)
        if len(entry) != 8:
            raise ValueError("B3: tabla AFS truncada")
        offset, size = struct.unpack("<II", entry)
        source.seek(offset)
        data = source.read(size)
        if len(data) != size:
            raise ValueError("B3: entrada AFS truncada")
    if data[:4] == b"#AMB":
        return data
    run = tempfile.mkdtemp(prefix="b3_%d_" % index, dir=work)
    src, dst = os.path.join(run, "source.lzx"), os.path.join(run, "model.amb")
    with open(src, "xb") as out:
        out.write(data)
    subprocess.run([decoder, src, dst], capture_output=True, check=True)
    with open(dst, "rb") as source:
        return source.read()


def selftest():
    names = ["EXP_TST", "b_C_Base", "b_L_Hand", "h_L_Index1", "unknown"]
    result = mapear(names, [-1, 0, 1, 2, 0], ["XTST_BODY", "TST_L00_LHAND", "TST_HEAD"])
    assert result["matched"] == 2 and result["unmatched"] == 3
    assert result["missing"][1]["source"] == "h_L_Index1"
    assert result["missing"][1]["ancestor_target"] == "TST_L00_LHAND"
    assert result["missing"][2]["ancestor_target"] is None
    assert result["target_unmatched"] == ["TST_HEAD"]
    assert mapear(["b_C_Base"], [-1], ["A_BODY", "B_BODY"])["matched"] == 0
    rejects(lambda: mapear(["unknown", "other"], [1, 0], []))
    # #AMB con #AWO en 0x40 y AWG en 0x80, 2 nombres a partir de 0xC0.
    data = bytearray(256)
    data[:4], data[64:68], data[128:132] = b"#AMB", b"#AWO", b"#AWG"
    struct.pack_into(">II", data, 16, 1, 32)
    struct.pack_into(">II", data, 32, 64, 192)
    struct.pack_into(">I", data, 64 + 28, 48)
    struct.pack_into(">I", data, 112, 64)
    struct.pack_into(">I", data, 128 + 16, 2)
    struct.pack_into(">I", data, 128 + 28, 64)
    data[192:201], data[224:232] = b"XTST_BODY", b"TST_HEAD"
    assert leer_b3_labels(data) == ["XTST_BODY", "TST_HEAD"]
    rejects(lambda: leer_b3_labels(data[:200]))
    print("xeno_rig: N0 OK (alias, ancestros separados, ambiguos, ciclos, labels B3 sinteticos)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--cpk")
    ap.add_argument("--b3-afs")
    ap.add_argument("--xbdecompress")
    ap.add_argument("--trabajo", default=r"D:\tmp\xv")
    ap.add_argument("--personajes", nargs="+", default=["GOK=264", "VGT=416", "PIC=350"])
    ap.add_argument("--salida")
    args = ap.parse_args(argv)
    if args.selftest:
        selftest()
        return 0
    if not all((args.cpk, args.b3_afs, args.xbdecompress)):
        ap.error("hacen falta --cpk, --b3-afs y --xbdecompress")
    try:
        results = []
        with Cpk(args.cpk) as cpk:
            for spec in args.personajes:
                code, index = spec.split("=")
                code, index = code.upper(), int(index)
                path = "data/chara/%s/%s_000.esk" % (code, code)
                esk = Esk(cpk.get(path))
                labels = leer_b3_labels(extraer_b3(args.b3_afs, index, args.xbdecompress, args.trabajo))
                result = dict(character=code, esk=path, b3_entry=index,
                              inverse_bind_error=esk.inverse_bind_error, **mapear(esk.names, esk.parent, labels))
                results.append(result)
                print("%s: %d/%d candidatos; %d sin casar; B3=%d huesos; error bind=%s" %
                      (code, result["matched"], result["source_bones"], result["unmatched"],
                       result["target_bones"], result["inverse_bind_error"]))
        if args.salida:
            write_json(args.salida, results)
        return 0
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print("xeno_rig: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
