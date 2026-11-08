"""Valida adquisición CPU y pérdidas de lectura; no simula ejecución GPU."""
import hashlib
import struct
import unittest

from solb2x_robot_materiales import capturar


class MaterialesRobot(unittest.TestCase):
    def setUp(self):
        self.memoria = {}
        self.pedidos = []

    def poner(self, p, b):
        for i, x in enumerate(b):
            self.memoria[p+i] = x

    def u(self, p, x):
        self.poner(p, struct.pack('>I', x))

    def leer(self, p, n):
        self.pedidos.append((p, n))
        try:
            return bytes(self.memoria[p+i] for i in range(n))
        except KeyError as e:
            raise OSError('memoria no disponible') from e

    def programa(self, *, tipo=6, codigo=0xf9591000):
        dispositivo, registro, objeto, recurso = 0x40100130, 0x40200000, 0x40300000, 0x40400000
        self.u(0x823958a4, dispositivo)
        self.u(dispositivo+412, registro)
        self.u(dispositivo+416, registro+20)
        self.poner(registro, struct.pack('>5I', 14, 23, 5, objeto, 0x40500000))
        self.poner(objeto, bytes(128))
        self.u(objeto+8, recurso)
        self.poner(recurso, bytes(4096))
        self.u(recurso, 0x00100000 | tipo)
        self.u(recurso+(32 if tipo == 6 else 24), codigo)
        self.poner(codigo, b'D'*4096)
        alias = 0xa0000000+codigo-0xe0000000+0x1000
        self.poner(alias, b'A'*4096)
        return dispositivo, registro, objeto, alias

    @staticmethod
    def log_programa():
        return 'dbz3 B2 robot programa: pasada=1 dibujo=1 arg4=14 arg5=23 arg6=5'

    def test_material_endian_indice_y_sha_sin_cambiar_memoria(self):
        grupo, parte, tabla = 0x45377600, 0x45379030, 0x45377a40
        descriptor = bytearray(96)
        struct.pack_into('>H', descriptor, 20, 4)
        self.poner(parte, descriptor)
        self.u(grupo+32, tabla)
        material = bytes(range(80))
        self.poner(tabla+4*80, material)
        previo = self.memoria.copy()
        log = ('dbz3 B2 robot material: pasada=2 dibujo=9 '
               'modelo=401099F0 grupo=45377600 parte=45379030')
        r = capturar(self.leer, log, 300)
        m = r['partes'][0]
        self.assertEqual((m['pasada'], m['dibujo'], m['indice_material']), (2, 9, 4))
        self.assertEqual(m['material']['direccion'], f'{tabla+320:08X}')
        self.assertEqual(m['material']['sha256'], hashlib.sha256(material).hexdigest())
        self.assertEqual(self.memoria, previo)
        self.assertIsNone(r['dispositivo'])
        self.assertFalse(r['escena_aceptada'])

    def test_programa_conserva_lectura_bruta_y_alias_con_pagina_sdk(self):
        _, _, _, alias = self.programa()
        r = capturar(self.leer, self.log_programa(), 300)
        p = r['programas'][0]
        self.assertTrue(p['registros_vertex_disponibles'])
        c = p['candidatos_vertex'][0]['objeto']['candidato_codigo_recurso']
        self.assertEqual(c['offset'], 32)
        self.assertFalse(c['lectura_directa_ajusta_ventana'])
        self.assertEqual(c['alias_fisico_sdk']['direccion'], f'{alias:08X}')
        self.assertEqual(bytes.fromhex(c['lectura_directa']['hex']), b'D'*4096)
        self.assertEqual(bytes.fromhex(c['alias_fisico_sdk']['hex']), b'A'*4096)
        self.assertFalse(p['pixel_disponible'])

    def test_recurso_pixel_usa_offset24_y_no_inventa_codigo_por_tipo(self):
        for tipo in (7, 9):
            self.memoria.clear()
            self.programa(tipo=tipo)
            p = capturar(self.leer, self.log_programa(), 300)['programas'][0]
            objeto = p['candidatos_vertex'][0]['objeto']
            if tipo == 7:
                self.assertEqual(objeto['candidato_codigo_recurso']['offset'], 24)
            else:
                self.assertNotIn('candidato_codigo_recurso', objeto)

    def test_tabla_o_vector_cortos_se_declaran_no_disponibles(self):
        _, registro, _, _ = self.programa()
        def corta(p, n):
            return self.leer(p, n)[:-1] if p == registro else self.leer(p, n)
        p = capturar(corta, self.log_programa(), 300)['programas'][0]
        self.assertFalse(p['registros_vertex_disponibles'])
        self.assertEqual(p['candidatos_vertex'], [])
        self.poner(0x45379030, bytes(96))
        log = ('dbz3 B2 robot material: pasada=1 dibujo=1 '
               'modelo=401099F0 grupo=45377600 parte=45379030')
        r = capturar(self.leer, log, 300)
        self.assertFalse(r['partes'][0]['tabla_materiales_disponible'])

    def test_no_lee_punteros_invalidos_ni_bloques_sin_limite(self):
        _, _, objeto, _ = self.programa()
        self.u(objeto+52, 0xfffffff0)
        self.u(objeto+60, 0x10000)
        self.pedidos.clear()
        p = capturar(self.leer, self.log_programa(), 300)['programas'][0]
        self.assertFalse(p['candidatos_vertex'][0]['objeto']['buffer_52_60']['disponible'])
        self.assertNotIn((0xfffffff0, 0x10000), self.pedidos)
        self.assertTrue(all(0 < n <= 65536 and p+n <= 0x100000000 for p, n in self.pedidos))


if __name__ == '__main__':
    unittest.main()
