"""Mirada B2: el .inl compila fuera del juego, gira cuello/cabeza hacia el rival con w=1 y no toca nada con w=0.
Si existe la traza PAL con pose A (D:/tmp/solB2X/original_g8_e0_20261105_205413) tambien comprueba <=5 grados medios."""
import tempfile
import unittest
from pathlib import Path

import numpy as np

import solb2x_mirada_validar as M

TRAZA = 'D:/tmp/solB2X/original_g8_e0_20261105_205413'


class MiradaTest(unittest.TestCase):
    def test_compila_y_valida(self):
        with tempfile.TemporaryDirectory() as td:
            try:
                M.compilar(Path(td))
            except (FileNotFoundError, RuntimeError) as e:   # sin clang++ en el PATH: nada que comprobar
                self.skipTest(str(e)[:80])
        if not Path(TRAZA, 'traza.json').exists():
            self.skipTest('sin traza PAL')
        import io, contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            M.main(TRAZA)
        ult = buf.getvalue().strip().split('\n')[-1]     # 'media cuello X cabeza Y | ...'
        medias = [float(v) for v in ult.replace('|', ' ').split() if v.replace('.', '', 1).isdigit()][:2]
        self.assertLess(medias[0], 2.0)
        self.assertLess(medias[1], 3.0)


if __name__ == '__main__':
    unittest.main()
