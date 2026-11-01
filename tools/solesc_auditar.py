"""Inventario reproducible de transiciones B2 desde la ISO del jugador.

No modifica mods ni juego. Los tiempos son sumas estáticas de esperas del SPX,
no una medida de ejecución: conserva llamadas y offsets para contrastarlos.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "mod center hd"))
import escenarios_convertir as E


def sentencias(spx):
    base = E.le(spx, 20)
    actual = []
    for off, token in E._spx_tokens(spx, base):
        if token[0] == "SEP" and actual:
            yield actual
            actual = []
        actual.append((off, token))
        # Varios llamados pueden compartir SEP: conservarlos todos.
        if token[0] == "END":
            yield actual
            actual = []
    if actual:
        yield actual


def auditar(fuente, grupo):
    mdd, _, _, spx = fuente.piezas(grupo)
    filas = []
    base = E.le(spx, 20)
    for sentencia in sentencias(spx):
        args, llamada, tipo = [], None, None
        for off, token in sentencia:
            if token[0] == "PUSH":
                args.append(token[1])
            elif token[0] == "PUSHF":
                args.append(struct.unpack_from("<f", spx, off + 2)[0])
            elif token[0] == "CALL":
                llamada = token[1]
            elif token[0] == "END":
                tipo = token[1]
        filas.append(dict(offset=sentencia[0][0] - base, llamada=llamada, tipo=tipo, args=args))
    cambios = [f for f in filas if f["tipo"] == 128 and f["llamada"] == 64]
    clips = []
    for cam in E.camaras_transicion(mdd):
        n, tab = E.le(cam, 0x10), E.le(cam, 0x14)
        clips.append([E.le(cam, tab + 16 * i + 8) for i in range(n)])
    return dict(grupo=grupo, entrada=5000 + grupo, variantes=E.le(mdd, 4),
                spx_sha256=hashlib.sha256(spx).hexdigest(), cambios=cambios,
                clips=clips, sentencias=filas,
                subrutinas_frecuentes=Counter(f["llamada"] for f in filas
                    if f["tipo"] == 115 and len(f["args"]) == 1).most_common(3))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--iso", required=True)
    p.add_argument("--salida", type=Path, required=True)
    a = p.parse_args()
    f = E.IsoB2(a.iso)
    datos = [auditar(f, g) for g in range(len(f.escenarios()))]
    a.salida.parent.mkdir(parents=True, exist_ok=True)
    a.salida.write_text(json.dumps(dict(iso=str(f.ruta), escenarios=datos),
                                   indent=2, ensure_ascii=False), encoding="utf-8")
    for d in datos:
        print(f"grupo {d['grupo']:2}: variantes={d['variantes']} "
              f"cambios_0x40={len(d['cambios'])} clips={d['clips']}")


if __name__ == "__main__":
    main()
