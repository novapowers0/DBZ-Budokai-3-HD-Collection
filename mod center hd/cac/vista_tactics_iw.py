#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Horno Tactics/Survivors con andar IW. Misma CLI y TKB2 que vista_tactics.py.

Sin copia de IW usa los clips B3 del manifiesto. No copia datos IW al mod kit.
--iw admite una carpeta USR o una ISO; --exigir-iw impide el respaldo en pruebas.
--correr hornea el bucle de carrera en lugar del andar (no el arranque 0x16).
"""
import argparse
import sys
from pathlib import Path

sys.path[:0] = [str(Path(__file__).resolve().parent), str(Path(__file__).resolve().parent.parent)]
import vista_tactics as vt
import tactics_andar_iw as iw


def main():
    # Los argumentos comunes los valida el horno existente. El adaptador cambia
    # solo su productor de TKB2; conserva CLI, poses de select y escritura atomica.
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--iw")
    parser.add_argument("--exigir-iw", action="store_true")
    parser.add_argument("--correr", action="store_true")
    parser.add_argument("--juego")
    a, resto = parser.parse_known_args()
    fuente = None
    try:
        fuente = iw.fuente_iw(a.juego, a.iw)
    except FileNotFoundError:
        if a.exigir_iw or a.iw:
            raise
    original = vt.hornear_unidad

    def producir(afs, uni, cid):
        # Restaurar la referencia durante la llamada evita recursiones: el
        # auxiliar obtiene primero los siete clips B3 que tiene que conservar.
        vt.hornear_unidad = original
        try:
            return iw.hornear_unidad(afs, uni, cid, fuente, a.correr, a.exigir_iw)
        finally:
            vt.hornear_unidad = producir

    sys.argv[1:] = resto + (["--juego", a.juego] if a.juego else [])
    vt.hornear_unidad = producir
    try:
        return vt.main()
    finally:
        vt.hornear_unidad = original


if __name__ == "__main__":
    sys.exit(main())
