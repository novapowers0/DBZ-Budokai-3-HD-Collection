#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xeno_cpk.py - catalogo de assets de Dragon Ball Xenoverse (CPK de CRI), solo lectura.

Xenoverse guarda TODO en `data.cpk` (+ data2/datap1..3, movie*): 36 276 entradas en `data.cpk`
(medido). Dentro estan los personajes y el creador (CaC):
  data/chara/HUM|HUF|NMC|FRI|MAM|MAF/...   piezas del CaC por raza (cuerpo, pelo, cara, ropa)
  data/chara/APL/...                        piezas del avatar (protagonista)
  data/chara/<CLAVE>/...                    personajes (GOK, VGT, ...)
y los formatos son los MISMOS que los de SDBH Heroes (familia #EMD/#ESK/#EMM/#EMB/#EAN):
  .emd geometria | .esk esqueleto | .emm material | .emb texturas | .ean animacion
(lo confirma LibXenoverse: https://github.com/DarioSamo/LibXenoverse).

OJO: los ficheros del CPK pueden ir comprimidos con CRILAYLA. `extraer` conserva el blob
tal cual y lo avisa; `extraer --descomprimir` usa el lector de xeno_model.

  python xeno_cpk.py info <cpk...>
  python xeno_cpk.py catalogo <cpk> <salida.tsv>
  python xeno_cpk.py buscar <cpk> <regex>
  python xeno_cpk.py extraer <cpk> <ruta interna> <salida>
  python -B xeno_cpk.py --selftest
"""
import argparse
import collections
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
try:
    import cri  # noqa: E402
except ImportError:                     # prueba desde otra carpeta
    sys.path.insert(0, os.environ.get("DBZ3_AWO_TOOLS", HERE))
    import cri  # noqa: E402,F811


def _leer(cpk, nombre, size, off):
    if size < 0 or off < 0:
        raise ValueError("CPK: rango invalido para %s" % nombre)
    with open(cpk, "rb") as f:
        f.seek(off)
        data = f.read(size)
    if len(data) != size:
        raise ValueError("CPK: entrada truncada: %s" % nombre)
    return data


def cmd_info(cpks):
    for cpk in cpks:
        rows = cri.cpk_toc(cpk)
        ext = collections.Counter(n.rsplit(".", 1)[-1].lower() for n, s, o in rows)
        chara = collections.Counter()
        for n, s, o in rows:
            m = re.match(r"data/chara/([^/]+)/", n, re.I)
            if m:
                chara[m.group(1).upper()] += 1
        print("%s: %d entradas" % (os.path.basename(cpk), len(rows)))
        print("   extensiones: " + ", ".join("%s=%d" % (k, v) for k, v in ext.most_common(12)))
        print("   chara: " + ", ".join("%s=%d" % (k, v) for k, v in chara.most_common(12)))
        for pat in ("_Body", "_Hair", "_Face", "_Boots", "_Bust"):
            hit = [n for n, s, o in rows if pat.lower() in n.lower()]
            if hit:
                print("   %s: %d (p.ej. %s)" % (pat, len(hit), hit[0]))


def cmd_catalogo(cpk, out):
    rows = cri.cpk_toc(cpk)
    with open(out, "x", encoding="utf-8") as fh:
        fh.write("ruta\ttamano\toffset\tcomprimido\n")
        for n, s, o in rows:
            comp = _leer(cpk, n, 8, o) == b"CRILAYLA"
            fh.write("%s\t%d\t%d\t%s\n" % (n, s, o, "si" if comp else "no"))
    print("OK -> %s (%d entradas)" % (out, len(rows)))


def cmd_buscar(cpk, patron):
    rx = re.compile(patron, re.I)
    n = 0
    for ruta, s, o in cri.cpk_toc(cpk):
        if rx.search(ruta):
            print("%-70s %9d" % (ruta, s))
            n += 1
    print("%d coincidencias" % n)


def cmd_extraer(cpk, nombre, out, descomprimir=False):
    rows = cri.cpk_toc(cpk)
    matches = [(n, s, o) for n, s, o in rows if n.lower() == nombre.lower()]
    if not matches:
        matches = [(n, s, o) for n, s, o in rows if n.rsplit("/", 1)[-1].lower() == nombre.lower()]
    if len(matches) != 1:
        print("%s: %s" % ("nombre ambiguo; indica ruta completa" if matches else "no encontrado", nombre))
        return 1
    ruta, size, offset = matches[0]
    data = _leer(cpk, ruta, size, offset)
    if descomprimir:
        from xeno_model import descomprimir as decode
        data = decode(data)
    with open(out, "xb") as target:
        target.write(data)
    comp = " CRILAYLA (hay que descomprimir)" if data[:8] == b"CRILAYLA" else ""
    print("OK -> %s (%d B) %s%s" % (out, len(data), data[:4], comp))
    return 0


def selftest():
    import contextlib
    import io
    from unittest.mock import patch
    from xeno_binary import _test_cpk, _test_utf, rejects
    blob = _test_cpk()
    with patch("builtins.open", side_effect=lambda *a, **kw: io.BytesIO(blob)):
        rows = cri.cpk_toc("synthetic.cpk")
        assert rows == [("data/chara/TST/test.emm", 5, 0x800), ("raw.dat", 3, 0x808)]
        assert _leer("synthetic.cpk", rows[0][0], 5, 0x800) == b"#EMM!"
        rejects(lambda: _leer("synthetic.cpk", "bad", 16, 0x808))
        with contextlib.redirect_stdout(io.StringIO()) as output:
            cmd_buscar("synthetic.cpk", r"TST/.*\.emm$")
        assert "1 coincidencias" in output.getvalue()
        with contextlib.redirect_stdout(io.StringIO()) as output:
            cmd_info(["synthetic.cpk"])
        assert "2 entradas" in output.getvalue() and "TST=1" in output.getvalue()
    table = _test_utf("N0", [("n", 2), ("s", 10)], [(7, "uno"), (9, "dos")])
    expected = ("N0", [dict(n=7, s="uno"), dict(n=9, s="dos")])
    assert cri.utf(table) == expected and cri.utf(cri._xor(table)) == expected
    with patch("builtins.open", side_effect=lambda *a, **kw: io.BytesIO(b"not a CPK")):
        rejects(lambda: cri.cpk_toc("invalid.cpk"))
    print("xeno_cpk: N0 OK (@UTF claro/cifrado, TOC/rutas/offsets, busqueda, resumen, truncado)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    a = sub.add_parser("info")
    a.add_argument("cpks", nargs="+")
    a = sub.add_parser("catalogo")
    a.add_argument("cpk")
    a.add_argument("salida")
    a = sub.add_parser("buscar")
    a.add_argument("cpk")
    a.add_argument("patron")
    a = sub.add_parser("extraer")
    a.add_argument("cpk")
    a.add_argument("nombre")
    a.add_argument("salida")
    a.add_argument("--descomprimir", action="store_true")
    x = ap.parse_args(argv)
    if x.selftest:
        selftest()
        return 0
    if x.cmd == "info":
        cmd_info(x.cpks)
    elif x.cmd == "catalogo":
        cmd_catalogo(x.cpk, x.salida)
    elif x.cmd == "buscar":
        cmd_buscar(x.cpk, x.patron)
    elif x.cmd == "extraer":
        return cmd_extraer(x.cpk, x.nombre, x.salida, x.descomprimir)
    else:
        ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
