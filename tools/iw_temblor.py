"""Curvas de camara IW por cuadro, con entrada de mando (sin piloto).

original: PINE de una instancia propia; port: exclusivamente por pruebas.py.
Las series conservan n y los huecos: nunca se inventan cuadros por reloj.
"""
import argparse
import csv
import ctypes
import json
import os
from pathlib import Path
import struct
import sys
import threading
import time

CLAVES = [0x7D0 + i for i in range(18)]
INDICES = [0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18]
CONTADOR = 0x4A4D40


def original(a):
    import pcsx2_sonda as s
    from forzar_mini_iw import B
    s.seleccionar_instancia(['--instancia', a.instancia])
    if s.INSTANCIA == 'principal' or s.nuestro_pid():
        raise RuntimeError('Se necesita una instancia propia, cerrada')
    import pruebas as P
    reserva = P.Juego(a.copia, 'solIWT_original')
    while not reserva._tomar_plaza(False):
        time.sleep(5)
    p = None
    lanzados = []
    try:
        # La instancia puede haberse ocupado mientras esperabamos plaza.
        # Retener el Popen que NOSOTROS creamos: nunca cerrar por nombre/ruta.
        if s.nuestro_pid():
            raise RuntimeError('Instancia ocupada durante la espera; no se toca')
        popen = s.subprocess.Popen

        def lanzar_propio(*args, **kwargs):
            proc = popen(*args, **kwargs)
            lanzados.append(proc)
            return proc

        s.subprocess.Popen = lanzar_propio
        try:
            p = s.lanzar('iw', os.path.join(s.EMU, 'sstates', 'SLES-55347 (335A5A1F).04.p2s'))
        finally:
            s.subprocess.Popen = popen
        # Prototipos HWND completos en Windows x64.
        from ctypes import wintypes as w
        s.u32dll.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
        s.u32dll.PostMessageW.restype = w.BOOL
        for clave in a.claves:
            previo = a.salida / f'{clave:X}_original.csv'
            if a.reanudar and previo.exists() and previo.with_suffix('.json').exists():
                print(f'{clave:X}: curva ya guardada; no se repite', flush=True)
                continue
            if a.reanudar and previo.exists():
                import shutil
                respaldo = a.salida / '_incompletas'
                respaldo.mkdir(exist_ok=True)
                shutil.move(str(previo), str(respaldo / f'{time.time_ns()}_{previo.name}'))
            idx = INDICES[CLAVES.index(clave)]
            # PINE atiende un socket a la vez: no abrir el de forzar() mientras
            # sigue conectado el devuelto por lanzar().
            p.cargar(4)
            time.sleep(2.5)
            th = threading.Thread(target=lambda: s.teclas('right espera300 z espera800 down down z'))
            th.start()
            seleccionado = False
            limite = time.monotonic() + 25
            while time.monotonic() < limite:
                if p.u32(B + 8) == 0x7D0:
                    p.escribir(B + 4, struct.pack('<II', idx, clave))
                    seleccionado = True
                    break
                time.sleep(.001)
            th.join()
            if not seleccionado:
                raise RuntimeError('No se ha seleccionado el minijuego')
            destino = a.salida / f'{clave:X}_original.csv'
            meta = {'clave': hex(clave), 'indice': idx, 'contador': hex(CONTADOR),
                    'vista': '*(0x49CA70)+0x70', 'pal': True, 'plan': a.plan}
            ultimo = None
            n_ready = None
            n_inicio = None
            tlim = time.monotonic() + 110
            with destino.open('x', newline='', encoding='utf-8') as f:
                wr = csv.writer(f)
                wr.writerow(['n', 'estado', 'anim', 'f', 'px', 'py', 'pz'] +
                            [f'm{i}' for i in range(16)] + [f'cam{i}' for i in range(16)])
                h = s.ventana_juego(s.nuestro_pid())
                paso = -1
                tecla = None
                while time.monotonic() < tlim:
                    ca = p.u32(0x49CA70)
                    if not 0x100000 <= ca < 0x2000000:
                        time.sleep(.01)
                        continue
                    # Contador antes/despues: descarta muestras cruzadas entre cuadros.
                    n = p.u32(CONTADOR)
                    if n == ultimo:
                        time.sleep(.001)
                        continue
                    raw = p.leer(ca, 0xC0)
                    ch = p.leer(ca + 0x800, 0xCD0)
                    cam = p.u32(0x49CBBC)
                    cr = p.leer(cam + 0x80, 64) if 0x100000 <= cam < 0x2000000 else bytes(64)
                    if n != p.u32(CONTADOR):
                        continue
                    ultimo = n
                    st = struct.unpack_from('<H', ch, 0xCBC)[0]
                    ui = p.u32(0x49CD8C)
                    ready = 0x100000 < ui < 0x2000000 and p.u32(ui + 0x150) & 2
                    if ready and n_ready is None:
                        n_ready = n
                    # El READY es comun a las familias; el estado 0x2C del
                    # actor no existe en todas. 140 cuadros / 1,2 PAL.
                    if n_ready is not None and n - n_ready >= 117 and n_inicio is None:
                        n_inicio = n
                        print(f'{clave:X}: mando n={n}', flush=True)
                    if n_inicio is not None:
                        rel = (n - n_inicio) & 0xffffffff
                        # PAL: cada n es un campo (1,2 cuadros logicos).
                        nuevo = min(len(a.plan) - 1, rel // 100)
                        if nuevo != paso:
                            if tecla:
                                s._post(h, ord(tecla), False)
                            paso = nuevo
                            tecla = {'W': 'I', 'A': 'J', 'S': 'K', 'D': 'L', 'Q': 'X', 'E': None}[a.plan[paso]]
                            if tecla:
                                s._post(h, ord(tecla), True)
                        if rel >= len(a.plan) * 100:
                            break
                    wr.writerow([n, st, struct.unpack_from('<I', ch, 0xB60)[0] & 0xfff,
                                 struct.unpack_from('<f', ch, 0xC68)[0],
                                 *struct.unpack_from('<3f', ch, 0x40),
                                 *struct.unpack_from('<16f', raw, 0x70), *struct.unpack('<16f', cr)])
                if tecla:
                    s._post(h, ord(tecla), False)
            meta['n_inicio'] = n_inicio
            destino.with_suffix('.json').write_text(json.dumps(meta, indent=2), encoding='utf-8')
            print(clave, destino, 'entrada', n_inicio, flush=True)
            if n_inicio is None:
                print(f'{clave:X}: READY no observado; curva guardada, comparacion pendiente', flush=True)
    finally:
        if p is not None:
            p.s.close()
        for proc in lanzados:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=15)
        reserva._soltar_plaza()


def port(a):
    import pruebas as P
    import hashlib
    import shutil
    import traceback
    preparar = P.preparar

    def preparar_con_causa(*args, **kwargs):
        try:
            return preparar(*args, **kwargs)
        except OSError as exc:
            print('PREPARAR:', repr(exc), 'archivo=', exc.filename, flush=True)
            traceback.print_exc()
            raise

    P.preparar = preparar_con_causa
    # Fija el binario de esta tanda aunque otro encargo compile mientras espera.
    ejecutable = a.salida / f'{a.etapa}.exe'
    if not ejecutable.exists():
        shutil.copy2(P.EXE, ejecutable)
    os.environ['DBZ3_PRUEBAS_EXE'] = str(ejecutable.resolve())
    (a.salida / f'{a.etapa}_exe.json').write_text(json.dumps({'original': str(P.EXE),
        'sha256': hashlib.file_digest(ejecutable.open('rb'), 'sha256').hexdigest()}, indent=2), encoding='utf-8')
    resultados = []
    for clave in a.claves:
        destino = a.salida / f'{clave:X}_{a.etapa}.csv'
        if destino.exists():
            if a.reanudar and destino.with_suffix('.json').exists():
                print(f'{clave:X}: curva ya guardada; no se repite', flush=True)
                continue
            if not a.reanudar:
                raise FileExistsError(destino)
            respaldo = a.salida / '_incompletas'
            respaldo.mkdir(exist_ok=True)
            shutil.move(str(destino), str(respaldo / f'{time.time_ns()}_{destino.name}'))

        def env(_):
            return {'DBZ3_TEST_IWMINI': hex(clave), 'DBZ3_PS2_GAMES': r'D:\DBZ3HD\ps2_games',
                    'DBZ3_TEST_IWCAMTRAZA': destino.as_posix(), 'DBZ3_IWVUELO_PJ': '0'}

        def jugar(j, _):
            j.log(r'entrada acabada: mando al jugador|iwdisparo: juego .* empieza|r14: en juego|READY\? terminado; empieza el vuelo', 150)
            marcas = []
            for k in a.plan:
                marcas.append([len(destino.read_text().splitlines()) if destino.exists() else 0, k])
                if k == 'E':
                    P.dormir(2)
                else:
                    j.tecla(f'{"Quote" if k == "Q" else k} 2000', 2000)
            destino.with_suffix('.json').write_text(json.dumps({'clave': hex(clave),
                'plan': a.plan, 'marcas_fila': marcas, 'run': str(j.run)}, indent=2), encoding='utf-8')

        nombre = f'solIWT_{clave:X}_{a.etapa}'
        P.ESCENARIOS[nombre] = ('curva de camara con mando', env, jugar)
        r = P.correr(a.copia, nombre, [], None, {'timeout': '300'}, True)
        resultados.append(r)
        print(P.tabla([r]), flush=True)
        if not r['ok']:
            break
    return all(r['ok'] for r in resultados)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('modo', choices=['original', 'port', 'evidencia'])
    ap.add_argument('--claves', type=lambda s: [int(k, 0) for k in s.split(',')], default=CLAVES)
    ap.add_argument('--salida', type=Path, required=True)
    ap.add_argument('--instancia', default='esc')
    ap.add_argument('--copia', default='sol')
    ap.add_argument('--etapa', default='antes')
    ap.add_argument('--plan', default='WEDES E'.replace(' ', ''))
    ap.add_argument('--estado', type=Path)
    ap.add_argument('--direccion', type=lambda s: int(s, 0), default=0x22EBB0)
    ap.add_argument('--reanudar', action='store_true', help='omite curvas ya cerradas, sin repetir partidas')
    a = ap.parse_args()
    os.environ['DBZ3_AGENTE'] = 'solIWT'
    a.salida.mkdir(parents=True, exist_ok=True)
    if a.modo == 'evidencia':
        import zipfile
        import capstone
        with zipfile.ZipFile(a.estado) as z:
            with z.open('eeMemory.bin') as f:
                f.seek(a.direccion)
                codigo = f.read(0x100)
        md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 | capstone.CS_MODE_LITTLE_ENDIAN)
        texto = []
        for i in range(0, len(codigo), 4):
            ins = list(md.disasm(codigo[i:i+4], a.direccion+i))
            texto.append(f'{a.direccion+i:08X}: ' + (ins[0].mnemonic+' '+ins[0].op_str if ins else codigo[i:i+4].hex()))
        (a.salida / f'evidencia_{a.direccion:X}.txt').write_text('\n'.join(texto), encoding='utf-8')
        print('\n'.join(texto))
    elif a.modo == 'original':
        original(a)
    else:
        raise SystemExit(0 if port(a) else 1)


if __name__ == '__main__':
    main()
