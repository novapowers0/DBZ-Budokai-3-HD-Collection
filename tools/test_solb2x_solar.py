"""Protege la adquisición de evidencia: no prueba fidelidad visual ni simula EE."""
import struct
import math
import unittest

from solb2x_solar import camara_solar_original, comprobar_par_capas, solar_hd, solar_original


class LectorSolar(unittest.TestCase):
    def hd(self, *, indice=3, direccion=None, callbacks=(0x820c8068, 0x820c7c68), marca=0x4232534f):
        p = 0x82398d40 + 176 * indice
        tabla = bytearray(1024)
        struct.pack_into('>I', tabla, 4 * indice, p if direccion is None else direccion)
        tarea = bytearray(176)
        struct.pack_into('>2I', tarea, 20, *callbacks)
        struct.pack_into('>2I', tarea, 28, 0x90000000, 4)
        struct.pack_into('>11f', tarea, 48, -820, 450, -650, 1, 35, -24, 965, 1, 1766, .15, .8)
        struct.pack_into('>5I', tarea, 156, 17, 16, 5, 1, marca)
        memoria = {0x82398940: bytes(tabla), p: bytes(tarea),
                   0x100060: struct.pack('>16f', *range(16)),
                   0x1001c4: struct.pack('>f', .785)}
        return lambda direccion, cantidad: memoria[direccion][:cantidad]

    def test_conserva_endian_contadores_y_estado_sin_reconstruir(self):
        r = solar_hd(self.hd(), 0x100000)
        t = r['tareas'][0]
        self.assertEqual((t['pasos'], t['dibujos'], t['quads_generados'], t['transicion']), (17, 16, 5, True))
        self.assertEqual(t['estado'][:4], [-820, 450, -650, 1])
        self.assertEqual((t['prioridad'], t['flags']), (0x90000000, 4))
        self.assertEqual(r['matriz_hd'], list(range(16)))

    def test_no_atribuye_tareas_ajenas_al_solar(self):
        self.assertEqual(solar_hd(self.hd(marca=0), 0x100000)['tareas'], [])

    def test_rechaza_marca_propia_con_callbacks_ajenos(self):
        with self.assertRaisesRegex(ValueError, 'callbacks diferentes'):
            solar_hd(self.hd(callbacks=(0, 0)), 0x100000)

    def test_rechaza_puntero_que_no_corresponde_al_indice_del_pool(self):
        with self.assertRaisesRegex(ValueError, 'fuera del pool'):
            solar_hd(self.hd(direccion=0x82398d40), 0x100000)

    def test_rechaza_lectura_incompleta(self):
        with self.assertRaisesRegex(ValueError, 'incompleta'):
            solar_hd(lambda p, n: bytes(n - 1), 0x100000)
        with self.assertRaisesRegex(ValueError, 'incompleta'):
            solar_original(lambda p, n: bytes(n - 1))

    def test_rechaza_punteros_pal_fuera_de_ram(self):
        tabla = struct.pack('<256I', 0x1fffff0, *([0] * 255))
        with self.assertRaisesRegex(ValueError, 'Rango solar PAL inválido'):
            solar_original(lambda p, n: tabla if p == 0x3dce50 else bytes(n))


class ParCapasSolar(unittest.TestCase):
    def par(self):
        from copy import deepcopy
        con=dict(guest_publicado=100,ui_frame=300,sin_solar=False,quads_emitidos=5,
                 guest_image=[0,0,1280,720],quads=[dict(textura=9,rgba_u32=123,xy=[[0,0]]*4,uv=[[0,0]]*4)]*5)
        sin=deepcopy(con);sin.update(ui_frame=330,sin_solar=True,quads_emitidos=0)
        return con,sin

    def test_admite_solo_par_estable_y_no_acepta_pal(self):
        con,sin=self.par()
        self.assertFalse(comprobar_par_capas(con,sin,True)['aceptacion_pal'])
        with self.assertRaisesRegex(ValueError,'mismo guest'):
            comprobar_par_capas(con,sin,False)

    def test_rechaza_cambios_de_contenido_y_emision(self):
        con,sin=self.par();sin['quads'][0]['rgba_u32']=124
        with self.assertRaisesRegex(ValueError,'geometría'):
            comprobar_par_capas(con,sin,True)
        con,sin=self.par();sin['quads_emitidos']=1
        with self.assertRaisesRegex(ValueError,'cinco quads'):
            comprobar_par_capas(con,sin,True)


class CamaraSolar(unittest.TestCase):
    def referencia(self):
        return dict(evento=0, stage=0, zona=0, solar=dict(
            matriz=[.8197306394577026,0,-.5727491974830627,0,
                    0,-1,0,0,-.5727491974830627,0,-.8197306394577026,0,
                    -10.145946502685547,12,105.1466293334961,1],
            proyeccion=[0]*15+[579.4153442382812]))

    def test_recupera_la_vista_dibujada_y_su_foco(self):
        r=self.referencia();m=r['solar']['matriz'];c=camara_solar_original(r)
        # El origen debe ir al centro de la vista; no usar la cámara activa
        # PAL, que puede describir el siguiente campo en vez del ya dibujado.
        for j in range(3):
            self.assertAlmostEqual(sum(m[4*i+j]*c[i] for i in range(3))+m[12+j],0,places=4)
        for j in range(3):
            self.assertAlmostEqual((c[j+3]-c[j])/100,m[4*j+2])
        self.assertAlmostEqual(240/math.tan(c[7]/2),579.4153442382812)

    def test_rechaza_vista_deformada_no_finita_y_campo_ajeno(self):
        for indice,valor in ((0,2),(0,float('nan')),(15,0),(5,1)):
            r=self.referencia();r['solar']['matriz'][indice]=valor
            with self.assertRaises(ValueError):camara_solar_original(r)
        r=self.referencia();r['evento']=1
        with self.assertRaises(ValueError):camara_solar_original(r)


if __name__ == '__main__':
    unittest.main()
