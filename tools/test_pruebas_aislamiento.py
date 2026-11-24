"""Regresion R16: python tools/test_pruebas_aislamiento.py (sin abrir juegos).

Conserva todos sus datos bajo el respaldo del encargo; no usa limpieza destructiva.
"""
import json
import os
import subprocess
import sys
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from unittest.mock import patch
from uuid import uuid4

import pruebas_aislamiento as A


class AislamientoTest(unittest.TestCase):
    def setUp(self):
        self.raiz = A.RESPALDOS.parent / "unitarios" / uuid4().hex
        self.copia = self.raiz / "copia con espacios"
        self.backup = self.raiz / "respaldos"
        self.copia.mkdir(parents=True)
        for rel, contenido in {
            "mods/historia_freezer/historia.toml": b"casilla=22\n",
            "mods/otro/.disabled": b"marcador original",
            "mods/heroe_exportado/modelos/base.bin": b"modelo previo",
            "user_data/dbz3/partida.sav": b"partida original",
            "user_data/dbz3/cac.toml": b"raza=3\n",
            "saves/perfil.sav": b"save",
            "exports/personaje/receta.toml": b"nombre=previo",
            "recetas_heroe/receta.toml": b"receta previa",
            "dbz3_user.toml": b"# ajustes originales\r\nvolumen = 30\r\n",
            "dbz3_user.test_keys": b"clave_original\n",
            "dbz3_cam.req": b"camara previa",
        }.items():
            p = self.copia / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(contenido)
        (self.copia / "mods/vacio").mkdir()
        self.antes = A.inventario(self.copia)

    def ensuciar(self):
        (self.copia / "mods/historia_freezer/.disabled").write_bytes(b"desactivado por otra prueba")
        (self.copia / "mods/historia_freezer/historia.toml").write_bytes(b"casilla=13\n")
        (self.copia / "mods/otro/.disabled").rename(self.raiz / "marcador_apartado")
        (self.copia / "mods/heroe_exportado/modelos/base.bin").write_bytes(b"exportado obsoleto")
        (self.copia / "user_data").rename(self.raiz / "partida_apartada")
        (self.copia / "user_data/dbz3").mkdir(parents=True)
        (self.copia / "user_data/dbz3/partida.sav").write_bytes(b"otra partida")
        (self.copia / "saves/perfil.sav").write_bytes(b"otro save")
        (self.copia / "exports/nuevo").mkdir()
        (self.copia / "exports/nuevo/modelo.bin").write_bytes(b"exportacion nueva")
        (self.copia / "dbz3_user.toml").write_bytes(b"volumen=0\n")
        (self.copia / "dbz3_cam.req").write_bytes(b"otra camara")
        (self.copia / "nuevo.toml.bak").write_bytes(b"config nueva")
        (self.copia / "logs").mkdir(exist_ok=True)
        (self.copia / "logs/prueba.log").write_bytes(b"evidencia que debe quedar")

    def verificar(self, estado):
        self.assertEqual(A.inventario(self.copia), self.antes)
        self.assertFalse(estado.pendiente.exists())
        recibo = json.loads((estado.carpeta / "restauracion.json").read_text(encoding="utf-8"))
        self.assertTrue(recibo["ok"])
        despues = Path(recibo["conservados_en"])
        self.assertEqual((despues / "mods/historia_freezer/.disabled").read_bytes(), b"desactivado por otra prueba")
        self.assertEqual((despues / "mods/heroe_exportado/modelos/base.bin").read_bytes(), b"exportado obsoleto")
        self.assertEqual((despues / "exports/nuevo/modelo.bin").read_bytes(), b"exportacion nueva")
        self.assertTrue((self.copia / "logs/prueba.log").exists())

    def test_exito_mods_partidas_config_y_exports(self):
        with A.aislar(self.copia, lambda _: None, self.backup) as estado:
            self.ensuciar()
        self.verificar(estado)

    def test_fallo_y_ctrl_c_siempre_restauran(self):
        for error in (RuntimeError("fallo del escenario"), SystemExit(3), KeyboardInterrupt()):
            with self.subTest(error=type(error).__name__):
                # Cada intento arranca con el mismo estado, como el reintento del ejecutor.
                for p in (self.raiz / "marcador_apartado", self.raiz / "partida_apartada"):
                    if p.exists():
                        p.rename(p.with_name(p.name + uuid4().hex))
                with self.assertRaises(type(error)):
                    with A.aislar(self.copia, lambda _: None, self.backup) as estado:
                        self.ensuciar()
                        raise error
                self.verificar(estado)

    def test_muerte_del_ejecutor_se_recupera_en_siguiente_entrada(self):
        codigo = """import os, sys
from pathlib import Path
import pruebas_aislamiento as A
s = A.Estado(Path(sys.argv[1]), Path(sys.argv[2]))
s.guardar()
(s.dest / 'mods/historia_freezer/.disabled').write_bytes(b'interrupcion')
(s.dest / 'dbz3_user.toml').write_bytes(b'config rota')
os._exit(9)
"""
        p = subprocess.run([sys.executable, "-c", codigo, str(self.copia), str(self.backup)],
                           cwd=str(Path(__file__).parent), capture_output=True, text=True)
        self.assertEqual(p.returncode, 9, p.stderr)
        self.assertNotEqual(A.inventario(self.copia), self.antes)
        with A.aislar(self.copia, lambda _: None, self.backup):
            self.assertEqual(A.inventario(self.copia), self.antes)
        self.assertEqual(A.inventario(self.copia), self.antes)

    def test_juego_aun_abierto_deja_pendiente_y_no_restaura_en_caliente(self):
        llamadas = []

        def comprobar(_):
            llamadas.append(1)
            if len(llamadas) > 1:
                raise RuntimeError("juego aun abierto")

        with self.assertRaisesRegex(RuntimeError, "aun abierto"):
            with A.aislar(self.copia, comprobar, self.backup) as estado:
                self.ensuciar()
        self.assertTrue(estado.pendiente.exists())
        self.assertNotEqual(A.inventario(self.copia), self.antes)
        recuperado = A.Estado(self.copia, self.backup)
        recuperado.recuperar()
        self.verificar(recuperado)

    def test_cambio_archivo_por_carpeta_y_carpeta_por_archivo(self):
        with A.aislar(self.copia, lambda _: None, self.backup):
            (self.copia / "mods/vacio").rename(self.raiz / "vacio_apartado")
            (self.copia / "mods/vacio").write_bytes(b"antes era una carpeta")
            p = self.copia / "saves/perfil.sav"
            p.rename(self.raiz / "save_apartado")
            p.mkdir()
            (p / "nuevo").write_bytes(b"antes era un archivo")
        self.assertEqual(A.inventario(self.copia), self.antes)

    def test_copias_distintas_en_paralelo(self):
        otra = self.raiz / "otra"
        otra.mkdir()

        def correr(dest):
            with A.aislar(dest, lambda _: None, self.backup) as e:
                (dest / "dbz3_user.toml").write_bytes(b"config de prueba")
            return e.pendiente

        with ThreadPoolExecutor(2) as pool:
            recibos = list(pool.map(correr, (self.copia, otra)))
        self.assertNotEqual(*recibos)
        self.assertEqual(A.inventario(self.copia), self.antes)
        self.assertEqual(A.inventario(otra), {})

    def test_respaldo_incompleto_no_publica_transaccion(self):
        with patch.object(A.shutil, "copy2", side_effect=OSError("disco lleno")):
            with self.assertRaisesRegex(OSError, "disco lleno"):
                with A.aislar(self.copia, lambda _: None, self.backup):
                    self.fail("no debe empezar el escenario")
        self.assertEqual(A.inventario(self.copia), self.antes)
        self.assertFalse(A.Estado(self.copia, self.backup).pendiente.exists())

    def test_respaldo_danado_impide_dar_restauracion_por_buena(self):
        estado = A.Estado(self.copia, self.backup)
        estado.guardar()
        (self.copia / "dbz3_user.toml").write_bytes(b"config prueba")
        (estado.carpeta / "original/dbz3_user.toml").write_bytes(b"respaldo danado")
        with self.assertRaisesRegex(RuntimeError, "respaldo dañado"):
            estado.restaurar()
        self.assertTrue(estado.pendiente.exists())
        self.assertFalse((estado.carpeta / "restauracion.json").exists())
        self.assertEqual(next(estado.carpeta.glob("despues_*/dbz3_user.toml")).read_bytes(), b"config prueba")


if __name__ == "__main__":
    unittest.main(verbosity=2)
