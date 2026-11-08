"""Evita atribuir una captura sin profundidad o de otro guest a la prueba Z."""
from copy import deepcopy
import unittest

from solb2x_solar_z import validar_metadatos


class AdquisicionZ(unittest.TestCase):
    def fotos(self):
        a=dict(guest_publicado=80,guest_image=[0,0,1280,720],quads=[dict(textura=8)],
               z_solicitado=True,z_estado_sdk=2,quads_emitidos=5,sin_solar=False)
        fotos=[]
        for i,(modo,z) in enumerate(((0,0),(2,.9995),(1,-1),(1,-1))):
            b=deepcopy(a);b.update(ui_frame=100+i,z_modo_foto=modo,z_d3d_normal=z)
            fotos.append(b)
        fotos[3].update(quads_emitidos=0,sin_solar=True)
        return fotos

    def test_admite_cuatro_adquisiciones_del_mismo_publicado(self):
        validar_metadatos(self.fotos(),True)

    def test_rechaza_recurso_de_profundidad_ausente(self):
        fotos=self.fotos();fotos[1]['z_estado_sdk']=1
        with self.assertRaisesRegex(ValueError,'sin profundidad real'):validar_metadatos(fotos,True)

    def test_rechaza_camara_geometria_o_guest_diferentes(self):
        for clave,valor in (('guest_publicado',81),('guest_image',[0,0,640,360]),('quads',[])):
            fotos=self.fotos();fotos[1][clave]=valor
            with self.subTest(clave=clave),self.assertRaisesRegex(ValueError,'contenido publicado distinto'):
                validar_metadatos(fotos,True)
        with self.assertRaisesRegex(ValueError,'guest cambió'):validar_metadatos(self.fotos(),False)

    def test_rechaza_orden_invertido_y_falsa_base(self):
        fotos=self.fotos();fotos[2]['ui_frame']=99
        with self.assertRaisesRegex(ValueError,'orden de capturas'):validar_metadatos(fotos,True)
        fotos=self.fotos();fotos[3]['sin_solar']=False
        with self.assertRaisesRegex(ValueError,'falta imagen base'):validar_metadatos(fotos,True)


if __name__=='__main__':unittest.main()
