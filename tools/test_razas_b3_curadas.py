"""Integridad real: las opciones normales conservan los bins del AFS local."""
import os
from pathlib import Path
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import patch

R = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(R / 'mod center hd/cac'), str(R / 'mod center hd'), str(R / 'awo_tools')]
import razas_b3 as rb
import vista_heroe as vh


class ConjuntosNativos(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(dir=r'E:\DBZ3HD_trabajo\respaldos_r18\solmAP_RAZAS')
        cls.afs = vh.rb.Afs(str(R / 'us/data_cmn.afs'), cls.work.name)

    @classmethod
    def tearDownClass(cls):
        # Los datos de esta verificacion se conservan como evidencia.
        cls.work._finalizer.detach()

    @patch.dict(os.environ, {'DBZ3_CAC_RAZAS_EXPERIMENTAL': '0'})
    def test_seis_opciones_conservan_bin_nativo_completo(self):
        for cuerpo in (27, 11, 34):
            raza = rb.FAMILIAS[cuerpo]
            for receta in rb.CURADAS[raza]:
                with self.subTest(raza=raza, receta=receta):
                    base = rb.INICIOS[raza][0]
                    ficha = dict(cuerpo=cuerpo, **{'b3_' + z: base + v for z, v in zip(rb.ZONAS, receta)})
                    fid = rb.CUERPOS[raza][receta[2]]
                    datos, resumen = rb.aplicar(self.afs.entry(vh.rb.IDS[cuerpo]['models'][0]), ficha, afs=self.afs)
                    self.assertEqual(datos, self.afs.entry(fid), 'Se alteraron geometria/pesos/expresiones nativos')
                    self.assertTrue(resumen['conjunto_nativo'])

    @patch.dict(os.environ, {'DBZ3_CAC_RAZAS_EXPERIMENTAL': '0'})
    def test_formas_conservan_cada_modelo_del_donante(self):
        for cuerpo in (27, 11, 35):
            ficha = rb.ficha_prueba(cuerpo)
            for donante, forma in vh.rb.IDS[cuerpo]['form_models'][1:]:
                row = vh.rb.IDS.get(donante) or vh.rb.DB['form_ids'][str(donante)]
                fid = row['models'][forma]
                with self.subTest(cuerpo=cuerpo, fid=fid):
                    datos, _ = rb.aplicar(self.afs.entry(fid), dict(ficha, _raza_modelo_forma=fid), afs=self.afs)
                    self.assertEqual(datos, self.afs.entry(fid))

    @patch.dict(os.environ, {'DBZ3_CAC_RAZAS_EXPERIMENTAL': '0'})
    def test_color_conserva_geometria_y_banda_alfa(self):
        import io
        from PIL import Image
        import numpy as np
        ficha = dict(rb.ficha_prueba(27), colores_regiones={'piel': [30, 180, 230], 'placas': [80, 220, 90]})
        original = self.afs.entry(181)
        datos, _ = rb.aplicar(original, ficha, afs=self.afs)
        z = original.find(b'#AZT')
        self.assertEqual(datos[:z], original[:z], 'El color altero el modelo/esqueleto')
        self.assertEqual(len(datos), len(original))
        self.assertNotEqual(datos, original, 'No se aplico el color solicitado')
        tex = rb._Texturas(original)
        for offset in tex.offsets:
            ent = z + offset
            at, size = tex.be32(original, ent + 0x14), tex.be32(original, ent + 0x18)
            a = np.asarray(Image.open(io.BytesIO(original[z + at:z + at + size])).convert('RGBA'))
            b = np.asarray(Image.open(io.BytesIO(datos[z + at:z + at + size])).convert('RGBA'))
            self.assertTrue(np.array_equal(a[..., 3], b[..., 3]), 'Se altero la banda de sombra nativa')

    @patch.dict(os.environ, {'DBZ3_CAC_RAZAS_EXPERIMENTAL': '0'})
    def test_ficha_antigua_no_expone_mezcla(self):
        ficha = rb.ficha_prueba(27, cabeza=6, cuerpo_tipo=1)
        self.assertEqual(rb.seleccion(ficha), ('freezer', list(rb.CURADAS['freezer'][0])))
        self.assertEqual(ficha['b3_torso'], 4006, 'No reescribir la ficha original')

    @patch.dict(os.environ, {'DBZ3_CAC_RAZAS_EXPERIMENTAL': '0'})
    def test_ficha_de_otro_proveedor_recupera_conjunto_seguro(self):
        ficha = dict(rb.ficha_prueba(27, cabeza=3), b3_piernas=2810)
        original = dict(ficha)
        self.assertEqual(rb.seleccion(ficha), ('freezer', list(rb.CURADAS['freezer'][1])))
        self.assertEqual(ficha, original, 'La recuperacion visual no modifica la partida')

    @patch.dict(os.environ, {'DBZ3_CAC_RAZAS_EXPERIMENTAL': '1'})
    def test_experimental_rechaza_mezcla_de_proveedores(self):
        with self.assertRaisesRegex(ValueError, 'otro proveedor'):
            rb.seleccion(dict(rb.ficha_prueba(27), b3_piernas=2810))

    @patch.dict(os.environ, {'DBZ3_CAC_RAZAS_EXPERIMENTAL': '1'})
    def test_bandera_conserva_catalogo_experimental(self):
        self.assertEqual(rb.seleccion(rb.ficha_prueba(27, 6, 1)), ('freezer', [6, 0, 1, 0]))

    def test_absorciones_no_se_recortan_como_ssj(self):
        ficha = dict(formas=8, modelos_por_traje=8, modelo_forma=list(range(8)), absorciones_nativas=True)
        n, modelos, lineas = vh.rb.forms_config(ficha, vh.rb.IDS[35], 8, 35, 'Majin propio')
        self.assertEqual(n, 8)
        self.assertEqual(modelos, list(range(8)))
        self.assertIn('absorciones_nativas = true', lineas)
        # Sin la declaracion expresa sigue vigente el limite seguro del donante.
        n, _, lineas = vh.rb.forms_config(dict(ficha, absorciones_nativas=False), vh.rb.IDS[35], 8, 35, 'Otro mod')
        self.assertEqual(n, 1)
        self.assertNotIn('absorciones_nativas = true', lineas)

    @patch.dict(os.environ, {'DBZ3_CAC_RAZAS_EXPERIMENTAL': '0'})
    def test_exportador_real_declara_absorciones_y_capsula(self):
        juego = Path(self.work.name) / 'exportador_majin'
        ficha = dict(rb.ficha_prueba(35, cabeza=1, cuerpo_tipo=1), nombre='Majin unidad')
        vh.escribir_mod_combate(str(juego), 1, ficha, 'cuerpo=35;nombre=Majin unidad;donante=35', self.afs)
        mod = juego / 'mods/heroe_propio'
        doc = tomllib.loads((mod / 'personaje.toml').read_text(encoding='utf-8'))['personaje']
        self.assertTrue(doc['absorciones_nativas'])
        self.assertEqual(doc['capsulas_nativas'], [123, 124])
        self.assertEqual(doc['fisica'], 'donante')
        _, _, lineas = vh.rb.forms_config(doc, vh.rb.IDS[35], 8, 35, 'Majin unidad')
        self.assertIn('fisica = 35', lineas)
        self.assertEqual(len(doc['modelos']), 8)
        self.assertEqual((mod / doc['modelos'][0]).read_bytes(), self.afs.entry(133))


if __name__ == '__main__':
    unittest.main(verbosity=2)
