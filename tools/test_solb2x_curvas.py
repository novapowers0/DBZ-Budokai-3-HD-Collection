"""Contratos del emparejamiento temporal y de papeles observados."""
import unittest

from solb2x_curvas import comparar


class CurvasTest(unittest.TestCase):
    def muestras(self):
        cam = dict(ojo=[0, 0, 0], objetivo=[0, 0, 1], arriba=[0, 1, 0], giro=0, fov=.7)
        atacante = dict(pos=[10, 178, -37], codigo=1290)
        victima = dict(pos=[427, 652, -63], codigo=1288)
        pal = dict(evento=1, campo=18000, reloj=449.99, zona=1,
                   escena_original=dict(camaras=dict(aplicada=cam)), actores=[victima, atacante])
        hd = dict(tipo='antes_dibujo', guest=3000, cuadro=450, zona=1, aplicada=cam,
                  tiempo_camara=1, indice_camara=0, actores=[atacante, victima])
        return pal, hd

    def test_papeles_invertidos_se_declaran_sin_buscar_la_mejor_distancia(self):
        pal, hd = self.muestras()
        correcto = comparar([pal], [hd], .03, (1, 0))
        self.assertEqual(correcto['pares_observados'][0]['error_pos'], [0, 0])
        self.assertEqual(correcto['papeles_pal_por_lado_hd'], [1, 0])
        sin_declarar = comparar([pal], [hd], .03)
        self.assertGreater(sin_declarar['pares_observados'][0]['error_pos'][0], 600)
        self.assertFalse(correcto['aceptacion_escena'])

    def test_no_ajusta_fase_para_conseguir_un_par(self):
        pal, hd = self.muestras()
        pal['reloj'] = 449.8
        with self.assertRaisesRegex(ValueError, 'no hay relojes comparables'):
            comparar([pal], [hd], .03, (1, 0))

    def test_rechaza_guest_duplicado_y_papel_repetido(self):
        pal, hd = self.muestras()
        with self.assertRaisesRegex(ValueError, 'mismo guest'):
            comparar([pal], [hd, hd], .03)
        with self.assertRaisesRegex(ValueError, 'permutación'):
            comparar([pal], [hd], .03, (1, 1))


if __name__ == '__main__':
    unittest.main()
