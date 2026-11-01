#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Utilidades de lectura acotada de XV1/XV2; nunca escriben en los originales."""
import argparse
import json
import struct


class Reader:
    def __init__(self, data, label):
        self.data, self.label = bytes(data), label

    def span(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError("%s: rango fuera del fichero (0x%X, %d B, total %d)" %
                             (self.label, offset, size, len(self.data)))
        return self.data[offset:offset + size]

    def unpack(self, fmt, offset):
        return struct.unpack(fmt, self.span(offset, struct.calcsize(fmt)))

    def u16(self, offset):
        return self.unpack("<H", offset)[0]

    def i16(self, offset):
        return self.unpack("<h", offset)[0]

    def u32(self, offset):
        return self.unpack("<I", offset)[0]

    def i32(self, offset):
        return self.unpack("<i", offset)[0]

    def text(self, offset, size=None):
        if size is not None:
            return self.span(offset, size).split(b"\0", 1)[0].decode("latin1")
        self.span(offset, 1)
        end = self.data.find(b"\0", offset)
        if end < 0:
            raise ValueError("%s: cadena sin terminador en 0x%X" % (self.label, offset))
        return self.data[offset:end].decode("latin1")

    def relative_text(self, base, field):
        offset = self.u32(base + field)
        return self.text(base + offset) if offset else ""

    def array(self, base, count, stride):
        if count and not base:
            raise ValueError("%s: tabla nula con %d entradas" % (self.label, count))
        self.span(base, count * stride)
        return range(base, base + count * stride, stride)

    def header(self, magic, size):
        self.span(0, size)
        if self.data[:4] != magic:
            raise ValueError("%s: firma incorrecta" % self.label)
        if self.data[4:6] != b"\xfe\xff":
            raise ValueError("%s: solo se admite little-endian (PC)" % self.label)


def rejects(call):
    """Los selftests negativos deben dar ValueError, no errores accidentales."""
    try:
        call()
    except ValueError:
        return
    raise AssertionError("se aceptaron datos invalidos")


def _test_utf(name, columns, rows):
    """Constructor N0 @UTF: solo datos inventados, columnas por fila."""
    strings = bytearray(b"\0")

    def text(value):
        off = len(strings)
        strings.extend(value.encode("utf-8") + b"\0")
        return off

    name_offset = text(name)
    schema = b"".join(bytes([0x50 | typ]) + struct.pack(">I", text(key)) for key, typ in columns)
    formats = {2: ">H", 4: ">I", 6: ">Q", 10: ">I"}
    values = bytearray()
    for row in rows:
        for (key, typ), value in zip(columns, row):
            values.extend(struct.pack(formats[typ], text(value) if typ == 10 else value))
    width = sum(struct.calcsize(formats[typ]) for key, typ in columns)
    str_off = 24 + len(schema) + len(values)
    body = struct.pack(">HHIIIHHI", 0, 24 + len(schema), str_off, str_off + len(strings),
                       name_offset, len(columns), width, len(rows)) + schema + values + strings
    return b"@UTF" + struct.pack(">I", len(body)) + body


def _test_cpk():
    header = _test_utf("CpkHeader", [("TocOffset", 6), ("ContentOffset", 6)], [(0x200, 0x800)])
    toc = _test_utf("CpkTocInfo", [("DirName", 10), ("FileName", 10), ("FileSize", 4),
                                   ("FileOffset", 6)],
                    [("data/chara/TST", "test.emm", 5, 0x600), ("", "raw.dat", 3, 0x608)])
    data = bytearray(0x80B)
    data[:16] = struct.pack("<4sIQ", b"CPK ", 0, len(header))
    data[16:16 + len(header)] = header
    data[0x200:0x210] = struct.pack("<4sIQ", b"TOC ", 0, len(toc))
    data[0x210:0x210 + len(toc)] = toc
    data[0x800:0x805], data[0x808:0x80B] = b"#EMM!", b"raw"
    return bytes(data)


def _test_crilayla():
    """3 literales + backref (offset 3, longitud 3), lectura de bits hacia atras."""
    bits = "".join("0" + format(c, "08b") for c in b"CBA")
    bits += "1" + "0" * 13 + "00"
    bits += "0" * (-len(bits) % 8)
    payload = bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))[::-1]
    prefix = bytes(range(256))
    return b"CRILAYLA" + struct.pack("<II", 6, len(payload)) + payload + prefix, prefix + b"ABCABC"


def write_json(path, value):
    # 'x': no se sobreescribe ninguna salida previa ni un fichero del jugador.
    with open(path, "x", encoding="utf-8") as out:
        json.dump(value, out, ensure_ascii=False, indent=2, allow_nan=False)
        out.write("\n")


def reader_main(parser, selftest, label, argv=None):
    ap = argparse.ArgumentParser(description="Lector %s de XV1/XV2, solo lectura" % label)
    ap.add_argument("archivo", nargs="?")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--cpk", help="CPK local del jugador")
    ap.add_argument("--entry", help="ruta completa dentro del CPK")
    ap.add_argument("--salida", help="JSON nuevo; nunca sobreescribe")
    args = ap.parse_args(argv)
    if args.selftest:
        selftest()
        return 0
    if bool(args.cpk) != bool(args.entry) or bool(args.archivo) == bool(args.cpk):
        ap.error("indica archivo, o --cpk con --entry")
    try:
        if args.cpk:
            from xeno_model import Cpk
            with Cpk(args.cpk) as cpk:
                data = cpk.get(args.entry)
        else:
            with open(args.archivo, "rb") as source:
                data = source.read()
        result = parser(data)
        if args.salida:
            write_json(args.salida, result)
        if isinstance(result, list):
            print("%s: %d materiales, %d parametros" %
                  (label, sum(m is not None for m in result),
                   sum(len(m["parameters"]) for m in result if m)))
            for material in result:
                if material:
                    print("  %s: %s (%d parametros)" %
                          (material["name"], material["shader"], len(material["parameters"])))
        else:
            print("%s %s: %d conjuntos, %d piezas, %d paletas, %d cuerpos" %
                  (label, result["version"], len(result["part_sets"]),
                   sum(sum(p is not None for p in s["parts"]) for s in result["part_sets"]),
                   len(result["colors"]), len(result["bodies"])))
        return 0
    except (OSError, ValueError, KeyError) as error:
        print("%s: %s" % (label, error))
        return 1
