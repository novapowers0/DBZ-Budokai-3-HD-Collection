"""IK de suelo B2: el .inl compila fuera del juego y sube el cuello a su umbral sin deformar las matrices."""
import subprocess
import tempfile
import unittest
from pathlib import Path

import numpy as np

import solb2x_ik_validar as V


class IkSueloTest(unittest.TestCase):
    def test_cadena_sintetica(self):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            try:
                exe = V.compilar_driver(td)
            except (FileNotFoundError, RuntimeError) as e:   # sin clang++ en el PATH: nada que comprobar
                self.skipTest(str(e)[:80])
            par = [-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 3] + list(range(11, 11+30))
            W = []
            for b in range(V.N):
                m = np.eye(4)
                m[3, :3] = [0.0, 0.5*b if b < 12 else 6.0, 0.0]
                W.append(m)
            W[11][3, :3] = [0.0, 1.0, -5.0]      # cuello por debajo de 2.0 (y_actor=0, suelo=0); STMC en el origen
            W[2][3, :3] = [0.0, 0.0, 0.0]
            V.escribir_entrada(td/'in.txt', W, par[:V.N], 0.0)
            flags, W2 = V.ejecutar(exe, td/'in.txt')
            self.assertEqual(flags.split()[0], '1')                    # la cabeza se corrige
            self.assertGreater(W2[11][3, 1], 1.8)                      # el cuello sube hacia 2.0 (el giro conserva la distancia al STMC)
            for m in W2[1:12]:                                         # sigue siendo una rotacion
                self.assertLess(np.abs(m[:3, :3] @ m[:3, :3].T - np.eye(3)).max(), 1e-6)


if __name__ == '__main__':
    unittest.main()
