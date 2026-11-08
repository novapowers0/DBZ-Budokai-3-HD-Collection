"""Pruebas del lector binario de94; no prueban una escena ni emulan la FPU."""
import struct
import unittest
from unittest.mock import patch

import solb2x_control94 as C


class Control94(unittest.TestCase):
    def test_jal_usa_pc_siguiente_incluso_al_cruzar_region(self):
        # Programa sintético: JAL con PC+4 en la región siguiente.
        codigo = struct.pack('<I', (3 << 26) | 0x123)
        with patch.object(C, 'mapear_elf', return_value=(64, codigo)):
            cuerpo = C.cuerpo_pal(b'elf', 0x0ffffffc, 0x10000000)
        self.assertEqual(cuerpo['llamadas_jal'],
                         [dict(pc='0FFFFFFC', destino='1000048C')])
        self.assertEqual(cuerpo['offset'], 64)

    def test_salto_sin_enlace_no_se_confunde_con_jal(self):
        codigo = struct.pack('<3I', (2 << 26) | 0x123, 0, 0x03e00008)
        with patch.object(C, 'mapear_elf', return_value=(64, codigo)):
            cuerpo = C.cuerpo_pal(b'elf', 0x1000, 0x100c)
        self.assertEqual(cuerpo['llamadas_jal'], [])
        self.assertEqual(len(cuerpo['palabras']), 3)
        self.assertEqual(cuerpo['palabras'][-1]['palabra'], '03E00008')

    def test_rechaza_rangos_que_cortan_instrucciones(self):
        for inicio, fin in [(1, 4), (0, 5), (4, 4), (8, 4)]:
            with self.subTest(inicio=inicio, fin=fin), self.assertRaises(ValueError):
                C.cuerpo_pal(b'elf', inicio, fin)

    def test_rechaza_imagen_hd_antes_de_adquirir_cuerpos(self):
        with patch.object(C, 'gp_elf', return_value=(0x3abcf0, [])):
            with patch.object(C, 'mapear_elf') as adquirir:
                with self.assertRaisesRegex(ValueError, 'Imagen HD distinta'):
                    C.auditar(b'elf', b'imagen ajena')
                adquirir.assert_not_called()


if __name__ == '__main__':
    unittest.main()
