"""Guardas del lector de contrato; no sustituyen una prueba visual del robot."""
from pathlib import Path
import struct
import unittest
from unittest.mock import mock_open, patch

import solb2x_robot_contrato as C


class ContratoRobot(unittest.TestCase):
    def test_acc_desde_palabras_capturadas(self):
        self.assertEqual(C.decodificar_acc(int.from_bytes(bytes.fromhex('1a180246'), 'little')),
                         dict(mnemonic='mula.s', fs=3, ft=2, destino='ACC'))
        self.assertEqual(C.decodificar_acc(int.from_bytes(bytes.fromhex('1ca00046'), 'little')),
                         dict(mnemonic='madd.s', fs=20, ft=0, destino='f0'))

    def test_no_confundir_orden_bytes_ni_otro_cop1(self):
        for palabra in (0x1a180246, 0x46021802, 0x4a02181a, 0x4602185a):
            with self.subTest(palabra=hex(palabra)), self.assertRaises(ValueError):
                C.decodificar_acc(palabra)

    @staticmethod
    def elf(*regiones):
        b = bytearray(256)
        b[:6] = b'\x7fELF\x01\x01'
        struct.pack_into('<I', b, 28, 52)
        struct.pack_into('<HH', b, 42, 32, len(regiones))
        for i, (off, base, size) in enumerate(regiones):
            struct.pack_into('<8I', b, 52+32*i, 1, off, base, base, size, size, 5, 4)
        b[192:200] = b'12345678'
        return bytes(b)

    def test_mapeo_elf_archivo_no_direccion(self):
        self.assertEqual(C.mapear_elf(self.elf((192, 0x1000, 8)), 0x1002, 4),
                         (194, b'3456'))

    def test_no_acepta_hueco_doble_mapeo_o_segmento_truncado(self):
        for elf, direccion, n in [(self.elf((192, 0x1000, 8)), 0x1006, 4),
                (self.elf((192, 0x1000, 8), (192, 0x1000, 8)), 0x1000, 4),
                (self.elf((254, 0x1000, 8)), 0x1000, 4)]:
            with self.subTest(direccion=direccion, n=n), self.assertRaises(ValueError):
                C.mapear_elf(elf, direccion, n)

    def test_rechaza_imagen_hd_no_acreditada(self):
        with patch.object(C, 'gp_elf', return_value=(0x3abcf0, [])):
            with self.assertRaisesRegex(ValueError, 'Imagen HD distinta'):
                C.auditar(b'elf', b'otra imagen')

    def test_callback_indirecto_no_desaparece_ni_se_confunde_con_retorno(self):
        # BCTRL presente en82139514; BLR no debe contarse como llamada.
        fuente = 'DEFINE_REX_FUNC(sub_82000000) {\n// bctrl\n// blr\n}'
        with patch.object(Path, 'read_text', return_value=fuente):
            cuerpo = C.cuerpo_hd(bytes.fromhex('4e8004214e800020'), '82000000', 'prueba.cpp')
        self.assertEqual(cuerpo['llamadas_directas'], [])
        self.assertEqual(cuerpo['llamadas_indirectas'],
                         [dict(pc='82000000', destino=None, registro='CTR')])

    def test_ejemplos_separan_factor_del_termino_original(self):
        constantes = {k: dict(valor=v) for k, v in
                      [('pal_min', .015), ('pal_max', .35), ('hd_min', .01), ('hd_max', .3)]}
        filas = C.ejemplos(constantes)
        self.assertEqual(len(filas), 12)
        # S=0, rama intermedia: el término bajo PAL queda sin factor incluso con P=2.
        fila = next(r for r in filas if r['rama'] == 'intermedia'
                    and r['factor'] == 2 and r['retorno_auxiliar'] == 0)
        self.assertEqual(fila['pal'], .015)
        self.assertEqual(fila['hd'], .02)
        cerca = next(r for r in filas if r['rama'] == 'cerca' and r['factor'] == 2)
        self.assertEqual(cerca['pal'], .03)

    def test_salida_exclusiva(self):
        abrir = mock_open()
        with patch.object(Path, 'open', abrir):
            C.guardar(Path('informe.json'), dict(aceptacion_escena=False))
        abrir.assert_called_once_with('x', encoding='utf-8')
        self.assertIn('"aceptacion_escena": false', abrir().write.call_args.args[0])

    def test_datos_invalidos_no_abren_salida(self):
        with patch.object(Path, 'open') as abrir, self.assertRaises(ValueError):
            C.guardar(Path('informe.json'), dict(valor=float('nan')))
        abrir.assert_not_called()


if __name__ == '__main__':
    unittest.main()
