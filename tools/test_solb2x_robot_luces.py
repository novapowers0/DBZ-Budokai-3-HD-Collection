"""Contrato del lector HD, con memoria ficticia; sin ISO, juego ni escrituras."""
import hashlib
import struct
import unittest

from solb2x_robot_luces import luces_hd


class LectorLucesHD(unittest.TestCase):
    def setUp(self):
        self.memoria = {}
        self.g = 0x823a3d80
        # Direcciones derivadas de lis/addi originales, no de las del lector.
        self.cabeza = ((-32199 & 0xffff) << 16)-23356
        self.ambiental = ((-32199 & 0xffff) << 16)-23376
        self.perfiles = ((-32206 & 0xffff) << 16)+11040
        self.celda = 0x82323760+16*92+28+32
        self.luz = 0x83000100
        self.poner(self.g, struct.pack('>I', 16))
        self.poner(self.g+64, struct.pack('>I', 1))
        self.poner(self.cabeza, struct.pack('>I', self.luz))
        self.poner(self.celda, struct.pack('>i', 6)+bytes(28))
        self.perfil = struct.pack('>12f', .8, .8, .8, 0, .58, -.8, -.17, 0,
                                  .4, .4, .4, .07)+bytes(32)
        self.poner(self.perfiles+6*80, self.perfil)
        b = bytearray(80)
        struct.pack_into('>12f', b, 0, 0, 0, 0, 1, .58, -.8, -.17, 0,
                         .4, .4, .4, .07)
        struct.pack_into('>hH', b, 48, 0, 0x103)
        self.poner(self.luz, b)
        self.poner(self.ambiental, struct.pack('>4f', .8, .8, .8, 0))
        self.poner(0x8201ed04, struct.pack('>I', 0x82085358))

    def poner(self, p, b):
        self.memoria.update((p+i, v) for i, v in enumerate(b))

    def leer(self, p, n):
        return bytes(self.memoria[p+i] for i in range(n))

    def test_layout_be_y_cabeceras_derivadas_de_ppc(self):
        r = luces_hd(self.leer)
        self.assertEqual((r['stage'], r['zona'], r['indice']), (16, 1, 6))
        self.assertEqual(r['celda'], self.celda)
        self.assertEqual(r['perfil_hex'], self.perfil.hex())
        self.assertEqual(r['perfil_sha256'], hashlib.sha256(self.perfil).hexdigest())
        self.assertEqual(r['callback'], 0x82085358)
        self.assertEqual(r['primera'], self.luz)
        self.assertEqual(r['luces'][0]['flags'], 0x103)
        self.assertTrue(r['luces'][0]['admite_0100'])
        self.assertEqual(r['luces'][0]['color'], list(struct.unpack('>4f', self.perfil[32:48])))
        self.assertIn('no dibujo del robot', r['cobertura'])

    def test_filtros_hd_sin_confundir_1000_con_0100(self):
        for flags, esperado in ((0x1003, False), (0x103, True), (3, True)):
            with self.subTest(flags=flags):
                self.poner(self.luz+50, struct.pack('>H', flags))
                self.assertEqual(luces_hd(self.leer)['luces'][0]['admite_0100'], esperado)

    def test_lista_vacia_valida(self):
        self.poner(self.cabeza, bytes(4))
        self.assertEqual(luces_hd(self.leer)['luces'], [])

    def test_lista_ciclica_rechazada(self):
        self.poner(self.luz+76, struct.pack('>I', self.luz))
        with self.assertRaisesRegex(ValueError, 'cíclica'):
            luces_hd(self.leer)

    def test_lectura_incompleta_rechazada(self):
        def leer(p, n):
            b = self.leer(p, n)
            return b[:-1] if p == self.luz else b
        with self.assertRaisesRegex(ValueError, 'incompleta'):
            luces_hd(leer)

    def test_puntero_invalido_rechazado(self):
        self.poner(self.cabeza, struct.pack('>I', 0xfffffff0))
        with self.assertRaisesRegex(ValueError, 'espacio guest'):
            luces_hd(self.leer)

    def test_vector_no_finito_rechazado(self):
        self.poner(self.luz+16, struct.pack('>f', float('nan')))
        with self.assertRaisesRegex(ValueError, 'no finito'):
            luces_hd(self.leer)

    def test_indice_ausente_y_fuera_del_limite_rechazados(self):
        for indice in (-1, 256):
            with self.subTest(indice=indice):
                self.poner(self.celda, struct.pack('>i', indice))
                with self.assertRaisesRegex(ValueError, 'índice HD'):
                    luces_hd(self.leer)

    def test_stage_y_variante_fuera_de_tabla_rechazados(self):
        for stage, zona in ((17, 0), (0, 2)):
            with self.subTest(stage=stage, zona=zona):
                self.poner(self.g, struct.pack('>I', stage))
                self.poner(self.g+64, struct.pack('>I', zona))
                with self.assertRaisesRegex(ValueError, 'stage/zona'):
                    luces_hd(self.leer)

    def test_cabecera_cambiada_no_se_presenta_como_muestra_estable(self):
        llamadas = 0
        def leer(p, n):
            nonlocal llamadas
            if p == self.cabeza:
                llamadas += 1
                if llamadas == 2:
                    return bytes(4)
            return self.leer(p, n)
        with self.assertRaisesRegex(ValueError, 'cabecera HD'):
            luces_hd(leer)

    def preparar_controles(self, tipo=5):
        self.mgr = 0x83100000
        self.control = 0x83200000
        global_mgr = ((-32195 & 0xffff) << 16)+27892
        self.poner(global_mgr, struct.pack('>I', self.mgr))
        self.poner(self.mgr+35096, bytes(4))
        self.poner(self.mgr+616, struct.pack('>2I', self.control, self.luz))
        b = struct.pack('>Ii', self.luz, tipo)+bytes(48)
        self.poner(self.control, b)
        for base, destino in ((0x8201fa84, 0x82136b58), (0x8201fa9c, 0x82136bf8)):
            self.poner(base+5*4, struct.pack('>I', destino))
        return b

    def test_tipo_control_no_es_tipo_luz(self):
        b = self.preparar_controles()
        r = luces_hd(self.leer, con_controles=True)
        fila = r['controles']['registros'][0]
        self.assertEqual(r['luces'][0]['tipo'], 0)
        self.assertEqual(fila['tipo_control'], 5)
        self.assertEqual(fila['destinos'], {'inicializacion': 0x82136b58,
                                            'actualizacion': 0x82136bf8})
        self.assertTrue(fila['coincide_luz'])
        self.assertTrue(fila['luz_en_lista'])
        self.assertEqual(fila['bytes_hex'], b.hex())
        self.assertEqual(fila['sha256'], hashlib.sha256(b).hexdigest())

    def test_control_inactivo_no_consulta_destino_negativo(self):
        self.preparar_controles(-1)
        fila = luces_hd(self.leer, con_controles=True)['controles']['registros'][0]
        self.assertEqual(fila['tipo_control'], -1)
        self.assertEqual(fila['destinos'], {})

    def test_control_nulo_se_conserva_como_ausente(self):
        self.preparar_controles()
        self.poner(self.mgr+616, bytes(4))
        fila = luces_hd(self.leer, con_controles=True)['controles']['registros'][0]
        self.assertEqual(fila['control'], 0)
        self.assertNotIn('tipo_control', fila)

    def test_enlace_luz_discordante_no_se_presenta_como_coincidencia(self):
        self.preparar_controles()
        self.poner(self.control, struct.pack('>I', self.luz+80))
        fila = luces_hd(self.leer, con_controles=True)['controles']['registros'][0]
        self.assertFalse(fila['coincide_luz'])

    def test_cantidad_y_tipo_de_control_fuera_de_limite(self):
        self.preparar_controles(6)
        with self.assertRaisesRegex(ValueError, 'tipo HD'):
            luces_hd(self.leer, con_controles=True)
        self.poner(self.mgr+35096, struct.pack('>I', 17))
        with self.assertRaisesRegex(ValueError, 'cantidad HD'):
            luces_hd(self.leer, con_controles=True)

    def test_tabla_de_control_no_acepta_destino_nulo_o_desalineado(self):
        self.preparar_controles()
        for destino in (0, 0x82136b59):
            with self.subTest(destino=destino):
                self.poner(0x8201fa84+5*4, struct.pack('>I', destino))
                with self.assertRaisesRegex(ValueError, 'destino HD'):
                    luces_hd(self.leer, con_controles=True)

    def test_controles_incompletos_rechazados(self):
        self.preparar_controles()
        def leer(p, n):
            b = self.leer(p, n)
            return b[:-1] if p == self.control else b
        with self.assertRaisesRegex(ValueError, 'incompleta'):
            luces_hd(leer, con_controles=True)

    def test_tabla_de_controles_cambiante_rechazada(self):
        self.preparar_controles()
        llamadas = 0
        def leer(p, n):
            nonlocal llamadas
            b = self.leer(p, n)
            if p == self.mgr+616:
                llamadas += 1
                if llamadas == 2:
                    return bytes(n)
            return b
        with self.assertRaisesRegex(ValueError, 'cambió la tabla HD'):
            luces_hd(leer, con_controles=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
