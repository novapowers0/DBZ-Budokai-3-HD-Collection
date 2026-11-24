import unittest

import variantes_formas as v


class FormasTest(unittest.TestCase):
    def test_dos_razas_y_seleccion_inmutable(self):
        for raza, forma in ((3, "f_fre_2"), (4, "f_maj_super")):
            ficha = {"b3_torso": 3300, "b3_guantes": 3340}
            actual = v.ficha_forma(ficha, raza, forma, {3300: {forma: 3301}})
            self.assertEqual(actual, {"b3_torso": 3301, "b3_guantes": 3340})
            self.assertEqual(ficha["b3_torso"], 3300)

    def test_rechaza_errores(self):
        with self.assertRaises(ValueError):
            v.ficha_forma({"b3_torso": 1}, 3, "f_maj_super", {})
        with self.assertRaises(ValueError):
            v.ficha_forma({"b3_torso": 1}, 3, "f_fre_2", {1: {}})
        with self.assertRaises(ValueError):
            v.construir_formas(None, {}, None, {}, None, 3,
                               [("f_fre_2", 1, 3), ("f_fre_3", 1, 4)], {})

    def test_construccion_usa_indice_explicito(self):
        class Afs:
            def entry(self, fid):
                return bytes([fid])

        def construir(datos, ficha, juego, afs):
            return datos + bytes([ficha["b3_torso"]]), {}

        resumen = {}
        resultado = v.construir_formas(Afs(), {"b3_torso": 1}, None, resumen, construir,
                                       4, [("f_maj_super", 2, 9)], {1: {"f_maj_super": 7}})
        self.assertEqual(resultado, [(b"\x09\x07", 2)])
        self.assertEqual(resumen["variante_f_maj_super"]["indice_donante"], 2)


if __name__ == "__main__":
    unittest.main()
