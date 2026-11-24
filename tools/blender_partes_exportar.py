#!/usr/bin/env python3
"""Blender --background pieza.blend --python tools/blender_partes_exportar.py -- salida.json"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_partes_b3 import exportar_blend

if __name__ == '__main__':
    exportar_blend(Path(sys.argv[sys.argv.index('--') + 1]))
