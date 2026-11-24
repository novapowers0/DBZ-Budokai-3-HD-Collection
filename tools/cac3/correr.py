"""Ejecuta un escenario CAC3 en una copia descartable; --preparar-solo no lanza el juego."""
import argparse
import os
from pathlib import Path
import shutil
import sys
import tempfile

SALIDA = Path(r'E:\DBZ3HD_trabajo\fabrica\CAC3')
PERFIL = 'B13EBABEBABEBABE'
SEMILLA = '''version = 3
activo = 0

[[heroe]]
id = "hcac202"
nombre = "Prueba CAC2"
raza = 0
cuerpo = 0
traje = 0
estilo = 0
progreso = 2
pelo = 0
receta_nombres = ["Tinte de prueba", "", "", ""]
receta_fichas = ["cuerpo=0;traje=0;partes=120,25,15|0,0,0|0,0,0|0,0,0", "", "", ""]
'''


def provisionar(api, dest, perfil):
    """Crea tanto la semilla legible como la instantánea real del arnés dentro de la copia."""
    ud = dest / 'user_data' / 'dbz3'
    ud.mkdir(parents=True)
    semilla = dest / 'semilla_cac.toml'
    semilla.write_bytes(SEMILLA.encode('utf-8'))
    shutil.copyfile(semilla, ud / 'cac.toml')
    shutil.copytree(perfil, ud / PERFIL)
    api.guardar_instantanea(dest.name, dest.name)


def main(argv=None):
    raiz = Path.cwd()
    escenarios = Path(__file__).resolve().parent / 'escenarios'
    casos = {p.stem: p for p in escenarios.glob('0[1-7]-*.toml')}
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('caso', choices=sorted(casos))
    parser.add_argument('--exe', type=Path, help='dbz3.exe dual ya compilado; por defecto, el de out/build/win-amd64-dual')
    parser.add_argument('--perfil', type=Path, help=f'carpeta {PERFIL} de un perfil nativo de pruebas; se lee, nunca se modifica')
    parser.add_argument('--preparar-solo', action='store_true', help='crear semilla e instantánea, validar el guion y salir sin juego')
    a = parser.parse_args(argv)
    if not (raiz / 'tools' / 'pruebas.py').is_file():
        parser.error('ejecutar desde la raíz del proyecto')
    exe = (a.exe or raiz / 'out/build/win-amd64-dual/dbz3.exe').resolve()
    if not a.preparar_solo:
        if not exe.is_file():
            parser.error(f'no existe {exe}; hace falta la compilación dual integrada')
        if not (raiz / 'us/data_cmn.afs').is_file():
            parser.error('falta el data_cmn.afs original de US en la raíz del proyecto')
    perfil = a.perfil
    if perfil is None:
        biblioteca = Path(r'D:\DBZ3HD\_instantaneas')
        perfiles = [biblioteca / nombre / 'user_data/dbz3' / PERFIL for nombre in ('perfil_nuevo', 'freezer_cap2')]
        perfil = next((p for p in perfiles if p.is_dir() and any(p.iterdir())), None)
    if perfil is None or not perfil.is_dir() or not any(perfil.iterdir()):
        parser.error(f'perfil nativo de pruebas ausente; pasar --perfil "ruta\\{PERFIL}"')
    if not SALIDA.is_dir():
        parser.error(f'no existe la carpeta de entrega {SALIDA}')
    dest = Path(tempfile.mkdtemp(prefix=f'cac3_{a.caso[:2]}_', dir=SALIDA))
    print(f'copia desechable: {dest}', flush=True)

    os.environ['DBZ3_AISLAMIENTO_DIR'] = str(dest / '_aislamiento')
    # La semilla y cac.toml tienen el mismo hash. El ZIP evita una carrera entre
    # los dos hilos que intentarían publicar el mismo objeto en Windows.
    os.environ['DBZ3_AISLAMIENTO_ARCHIVO'] = '1'
    os.environ['DBZ3_AISLAMIENTO_PODAR'] = '0'
    os.environ['DBZ3_PRUEBAS_REINTENTO'] = '0'
    for k in ('DBZ3_PRUEBAS_GUARDAR_INST', 'DBZ3_PRUEBAS_ENV', 'DBZ3_TEST_PANTALLA_EU', 'DBZ3_TEST_EU_COPIAS'):
        os.environ.pop(k, None)
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(raiz / 'tools'))
    import pruebas as api

    # Estado por corrida. Se conservan el candado de arranque y la cola globales del arnés.
    api.BASE = dest.parent
    api.INST = dest / '_instantaneas'
    api.RESPALDO = api.INST / '_respaldo'
    api.PRUEBAS = dest / '_pruebas'
    api.REINTENTOS = api.PRUEBAS / '_reintentos.jsonl'
    guion = str(casos[a.caso])
    api.cargar_escenario(guion, {})  # falla antes de tocar la copia de partida si el guion es inválido
    provisionar(api, dest, perfil)
    if a.preparar_solo:
        return 0

    # El arnés completa recursos, TOML limpio y DLL canónicas; no necesita clonar du_test.
    binario = dest / 'dbz3.exe'
    shutil.copy2(exe, binario)
    for n in ('amd_fidelityfx_dx12.dll', 'gamecontrollerdb.txt'):
        if (exe.parent / n).is_file():
            shutil.copy2(exe.parent / n, dest / n)
    # 23/11: the harness does not bring mod center hd/tools (xbdecompress.exe); without it vista_heroe.py cannot
    # decompress the model and the creator shows "Could not build the model".
    shutil.copytree(raiz / 'mod center hd' / 'tools', dest / 'mod center hd' / 'tools', dirs_exist_ok=True)
    os.environ['DBZ3_PRUEBAS_EXE'] = str(binario)
    api.EXE = binario  # preparar() no puede sustituir el binario seleccionado por el último build congelado
    api.PRIO = api.COLA.prio_de('correr')
    resultado = api.correr(dest.name, guion, ('DBZ3_TEST_PREPARAR_CAMPOS=0', 'DBZ3_TEST_TURBO=1',
                                            f'DBZ3_PYTHON={sys.executable}'),
                           instantanea=dest.name, forzar=True)
    return 0 if resultado['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
