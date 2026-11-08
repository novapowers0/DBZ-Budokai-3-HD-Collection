"""Contrasta STAGED47 con RAM original y ejecuta C++ en una salida propia nueva.

No arranca juegos, no regenera fixtures ni escribe en las evidencias de GP2.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zipfile


ORIGEN = Path('D:/tmp/solGP2/noche/bubbles')
CLANG = Path('C:/Program Files/LLVM/bin/clang++.exe')
HUELLAS = {
    'pose47.h': '6fe06c75106d5e3bfb5a3a2cba7206de0bd7f33e65fc5e4440688a8fe04f2dd2',
    'test.cpp': '6ec0d0ad7e6f8a431614e8cbe31c1c7761eee1efb0d0e36753da6f5d57112e18',
}


def sha(datos):
    return hashlib.sha256(datos).hexdigest()


def exigir(condicion, mensaje):
    if not condicion:
        raise ValueError(mensaje)


def u(datos, offset):
    return struct.unpack_from('<I', datos, offset)[0]


def comprobar(origen):
    staged = origen / 'staged47'
    manifiesto = json.loads((staged / 'provenance.json').read_text(encoding='utf-8'))
    verificadas = {}
    for nombre, esperado in {**manifiesto['files'], **HUELLAS}.items():
        exigir(Path(nombre).name == nombre, 'Nombre fuera de la carpeta STAGED47')
        contenido = (staged / nombre).read_bytes()
        exigir(sha(contenido) == esperado, f'Ha cambiado {nombre}')
        verificadas[nombre] = dict(bytes=len(contenido), sha256=esperado)

    snapshots = []
    for caso in (c for c in manifiesto['cases'] if 'i' in c):
        path = Path(caso['source'])
        with zipfile.ZipFile(path) as z:
            ram = z.read('eeMemory.bin')
        actor = u(ram, 0x49ca70) + 0x28d0
        modelo = u(ram, actor + 0xb78)
        packed = u(ram, modelo + 0x41c)
        exigir(packed >> 16 == 4 and u(ram, modelo + 0x418) == 47,
               'Snapshot fuera del banco BBR47')
        target = u(ram, modelo + 0x42c)
        mapa = modelo + 0x4b0 + ((packed >> 16) - 1) * 0x200
        fixture = ram[modelo + 0x41c:modelo + 0x420] + ram[modelo + 0x424:modelo + 0x428]
        fixture += ram[mapa:mapa + 94] + ram[target:target + 47 * 64]
        exigir(fixture == (staged / f"target{caso['i']}.bin").read_bytes(),
               'TARGET no coincide byte a byte con su snapshot')
        exigir(u(ram, 0x4a4d40) == caso['EE'], 'Contador de snapshot distinto')
        if caso['i'] == 0:
            seno = u(ram, 0x49be0c) & 0x1fffffff
            exigir(ram[seno:seno + 16385 * 4] == (staged / 'lut.bin').read_bytes(),
                   'LUT distinta de la RAM original')
        snapshots.append(dict(path=str(path), sha256=sha(path.read_bytes()),
                              EE=caso['EE'], packed=hex(packed), target_raw_identico=True))

    serie = origen / 'medida_1793507666924748000/serie_original.json'
    filas = json.loads(serie.read_text(encoding='utf-8'))['filas']
    pareja = next((a, b) for a, b in zip(filas, filas[1:])
                  if a['npc']['estado'] == 3 and b['npc']['estado'] == 7)
    exigir(pareja[1]['n'] == pareja[0]['n'] + 1, 'LIVE original no consecutivo')
    for fila, nombre in zip(pareja, ('oldlive.bin', 'expectedlive.bin')):
        original = b''.join(bytes.fromhex(m['raw']) for m in fila['npc']['mat47_128'])
        exigir(len(original) == 47 * 128 and original == (staged / nombre).read_bytes(),
               'LIVE no coincide con la serie original')

    carpeta_fin = origen / 'fin9_natural_1793514102821066300'
    meta = json.loads((carpeta_fin / 'fin9_buffers.json').read_text(encoding='utf-8'))
    raw = (carpeta_fin / 'fin9_buffers.raw').read_bytes()
    exigir(sha(raw) == meta['sha256'] and len(raw) == 0x7000, 'RAW fin9 modificado/truncado')
    exigir(u(raw, 0) == meta['count'] == 2, 'Fin9 no tiene el par antes/despues')
    exigir(meta['layout']['schema'] == 'meta100/300,target1000/2000,live3000/5000',
           'Layout fin9 desconocido')
    for nombre, contenido in (
        ('fin_before.bin', raw[0x3000:0x3000 + 47 * 128]),
        ('fin_after.bin', raw[0x5000:0x5000 + 47 * 128]),
        ('fin_meta.bin', raw[0x100:0x200] + raw[0x300:0x400]),
    ):
        exigir(contenido == (staged / nombre).read_bytes(), 'Fixture fin9 distinta del hook original')
    exigir(raw[0x1000:0x1000 + 47 * 64] == raw[0x2000:0x2000 + 47 * 64],
           'TARGET ha cambiado dentro de fin9')
    antes, despues = raw[0x100:0x200], raw[0x300:0x400]
    exigir(u(antes, 8) == u(despues, 8) == 19761, 'Fin9 cruza contadores')
    exigir(u(antes, 12) == u(despues, 12), 'Fin9 cambia RNG')
    exigir(u(antes, 24) & 65535 == u(despues, 24) & 65535 == 9, 'Callback no pertenece al estado9')
    exigir(u(antes, 28) == u(despues, 28) == 0xe2, 'Callback no pertenece a E2')
    exigir(u(antes, 36) == u(despues, 36) == 1, 'Callback sin fin de animacion')
    exigir(u(antes, 44) == 1 and u(despues, 44) == 0, 'Consumo de flags no observado')
    exigir(u(antes, 108) == u(despues, 108), 'TARGET remuestreado dentro del callback')
    frame = struct.unpack_from('<f', antes, 108)[0]
    exigir(35 < frame < 36 and u(antes, 32) == 0, 'Fin9 no conserva la fase fraccional esperada')
    return dict(
        staged=str(staged), archivos=verificadas, snapshots=snapshots,
        live_original=dict(path=str(serie), sha256=sha(serie.read_bytes()),
                           EE=[f['n'] for f in pareja], raw_identico=True),
        fin9=dict(path=str(carpeta_fin), sha256=sha(raw), EE=19761,
                  count=2, estado=9, flags=[1, 0], reloj_actor=0,
                  cuadro_target=frame, target_inalterado=True, rng_inalterado=True,
                  limite='Callback de consumo; no certifica todo el cambio9->0 ni el renderer'),
        normal_activado=False,
    )


def ejecutar(cmd, cwd, salida):
    r = subprocess.run([str(c) for c in cmd], cwd=cwd, capture_output=True,
                       text=True, errors='replace', timeout=120)
    registro = dict(comando=[str(c) for c in cmd], cwd=str(cwd), codigo=r.returncode,
                    stdout=r.stdout, stderr=r.stderr)
    with salida.open('x', encoding='utf-8') as f:
        json.dump(registro, f, indent=1, ensure_ascii=False)
    exigir(r.returncode == 0, f'Fallo de C++: {salida}')
    return registro


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--origen', type=Path, default=ORIGEN)
    p.add_argument('--salida', type=Path, required=True, help='Carpeta nueva, con padre existente')
    p.add_argument('--fin9', action='store_true', help='Ademas reproduce el limite de Paso del proyecto')
    p.add_argument('--matrices', action='store_true', help='Diagnostica matrices LIVE47 sin aprobar el renderer')
    a = p.parse_args()
    exigir(a.salida.parent.is_dir() and not a.salida.exists(), 'La salida debe ser nueva y su padre existir')
    exigir(not a.salida.resolve().is_relative_to(a.origen.resolve()), 'No escribir en evidencia original')
    a.salida.mkdir()
    resultado = dict(ok=False, normal_activado=False)
    try:
        resultado.update(comprobar(a.origen))
        staged = a.origen / 'staged47'
        exe = a.salida.resolve() / 'staged47_verificado.exe'
        ejecutar([CLANG, '-std=c++17', '-O2', '-ffp-contract=off', staged / 'test.cpp', '-o', exe],
                 a.salida, a.salida / 'compilar_staged.json')
        resultado['cpp_staged'] = ejecutar([exe], staged, a.salida / 'cpp_staged.json')
        lineas = resultado['cpp_staged']['stdout'].splitlines()
        exigir(len(lineas) == 8 and sum(' PASS' in l for l in lineas) == 7
               and lineas[-1] == 'INIT_DESCONOCIDO BLOCKED normal=false', 'C++ no ejecuto los ocho casos esperados')
        print(resultado['cpp_staged']['stdout'], end='')
        if a.fin9:
            fuente = Path(__file__).with_name('iwx_bubbles_fin9.cpp').resolve()
            raiz = fuente.parent.parent
            resultado['codigo_proyecto'] = {
                nombre: sha((raiz / 'src' / nombre).read_bytes())
                for nombre in ('iw_bubbles_gp2.h', 'iw_bubbles_gp2.inl', 'iw_bubbles_root.h')
            }
            exe_fin = a.salida.resolve() / 'fin9_proyecto.exe'
            ejecutar([CLANG, '-std=c++17', '-O2', '-ffp-contract=off', '-I', staged,
                      fuente, '-o', exe_fin], a.salida, a.salida / 'compilar_fin9.json')
            resultado['fuente_fin9_sha256'] = sha(fuente.read_bytes())
            resultado['cpp_fin9'] = ejecutar([exe_fin], staged, a.salida / 'cpp_fin9.json')
            print(resultado['cpp_fin9']['stdout'], end='')
        if a.matrices:
            fuente = Path(__file__).with_name('iwx_bubbles_matrices.cpp').resolve()
            exe_matrices = a.salida.resolve() / 'matrices_proyecto.exe'
            ejecutar([CLANG, '-std=c++17', '-O2', '-ffp-contract=off', '-I', staged,
                      fuente, '-o', exe_matrices], a.salida, a.salida / 'compilar_matrices.json')
            resultado['fuente_matrices_sha256'] = sha(fuente.read_bytes())
            resultado['cpp_matrices'] = ejecutar([exe_matrices], staged, a.salida / 'cpp_matrices.json')
            resultado['matrices_aceptadas'] = False
            for linea in resultado['cpp_matrices']['stdout'].splitlines():
                if linea.startswith('RESUMEN '):
                    print(linea)
        resultado['ok'] = True
    except (OSError, ValueError, KeyError, StopIteration, subprocess.SubprocessError) as e:
        resultado['error'] = str(e)
        print('FAIL', e)
    finally:
        with (a.salida / 'resultado.json').open('x', encoding='utf-8') as f:
            json.dump(resultado, f, indent=1, ensure_ascii=False)
        print('EVIDENCIA', a.salida)
    return 0 if resultado['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
