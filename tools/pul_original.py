"""Referencia IW con PCSX2 privado pul y contador original validado."""
import ctypes
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import threading
import time

os.environ['DBZ3_AGENTE'] = 'solPUL'
os.environ['PYTHONUTF8'] = '1'
OUT = Path('D:/tmp/solPUL')


def main():
    import pcsx2_cuadros as C
    import pruebas as P
    import pcsx2_sonda as s
    from pcsx2_turnos import process_image
    def pids():
        r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq pcsx2.exe', '/FO', 'CSV', '/NH'],
                           capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, check=True)
        return [int(p[1]) for l in r.stdout.decode('mbcs', errors='replace').splitlines()
                if len(p := l.split('\",\"')) > 1]
    s._pids = pids
    s._exe_de = process_image
    destino = OUT / 'pcsx2_pul'
    fuente = Path(s.RAIZ) / 'pcsx2_kit'
    if not destino.exists():
        destino.mkdir()
        for f in fuente.iterdir():
            if f.is_file():
                shutil.copy2(f, destino / f.name)
        for nombre in ('bios', 'QtPlugins', 'resources', 'inis', 'memcards'):
            shutil.copytree(fuente / nombre, destino / nombre)
        (destino / 'sstates').mkdir()
        estado = 'SLES-55347 (335A5A1F).04.p2s'
        shutil.copy2(fuente / 'sstates' / estado, destino / 'sstates' / estado)
        ini = destino / 'inis/PCSX2.ini'
        texto = ini.read_text(encoding='utf-8-sig').replace('PINESlot = 28017', 'PINESlot = 28027')
        ini.write_text(texto, encoding='utf-8')
    s.INSTANCIAS['pul'] = (28027, str(destino), str(OUT / 'capturas_pul'), 'instancia_pul')
    s = C.conectar('pul')
    os.environ['DBZ3_PCSX2_CUADROS_TMP'] = str(OUT)
    os.environ['DBZ3_PCSX2_CUADROS_RESPALDO'] = 'D:/_PARA_BORRAR_CLAUDE/solPUL'
    reserva = P.Juego('sol', 'solPUL_original')
    while not reserva._tomar_plaza(False):
        time.sleep(5)
    c = None
    propio = preparado = False
    try:
        assert not s.nuestro_pid(), 'pul ocupada'
        pendiente = OUT / 'PCSX2_pul.ini.original'
        if pendiente.exists():
            shutil.copy2(pendiente, destino / 'inis/PCSX2.ini')
            archivo = Path('D:/_PARA_BORRAR_CLAUDE/solPUL')
            archivo.mkdir(parents=True, exist_ok=True)
            shutil.move(str(pendiente), str(archivo / f'{time.time_ns()}_PCSX2_pul_recuperada.ini'))
        C.preparar(s)
        preparado = True
        p = s.lanzar('iw', 4)
        propio = True
        p.s.close()
        c = C.Cuadros(s, 0x4A4D40)
        p = c.p
        P.U32.GetMonitorInfoW.argtypes = [P.wt.HMONITOR, ctypes.POINTER(P._MONINFO)]
        P._a_pantalla_secundaria(type('Proceso', (), {'pid': s.nuestro_pid(), 'poll': lambda _: None})())
        time.sleep(2.5)
        th = threading.Thread(target=lambda: s.teclas('right espera300 z espera800 down down z'))
        th.start()
        fin = time.monotonic() + 30
        while time.monotonic() < fin:
            if p.u32(0x5C0DC8) == 0x7D0:
                p.escribir(0x5C0DC4, struct.pack('<II', 8, 0x7D7))
                break
            time.sleep(.01)
        else:
            raise RuntimeError('sin seleccion de mision')
        th.join()
        filas = []
        inicio = None
        fin = time.monotonic() + 100
        while time.monotonic() < fin:
            mgr, cam, ui = p.lote([(0x49CA70, 4), (0x49CBBC, 4), (0x49CD8C, 4)])
            # MSN009 no publica su camara en 49CBBC (es cero en el estado
            # n20024); no usar ese puntero de los minijuegos a pie.
            if not (0x100000 < mgr < 0x2000000):
                time.sleep(.02)
                continue
            # MSN009 no usa el READY de los campos a pie (49CD8C+150).
            # Su AimStart usa la camara fija y las poses 3B6/384 del original.
            ojo_actual = None
            if p.u32(mgr + 0x28D0 + 0xB60) == 0x384 and p.u32(mgr + 0x800 + 0xB60) == 0x3B6:
                if inicio is None:
                    inicio = c.numero()
            if inicio is not None:
                actores = []
                for x in (mgr + 0x800, mgr + 0x28D0):
                    actores.append(dict(pos=struct.unpack('<3f', p.leer(x + 0x40, 12)),
                                        codigo=hex(p.u32(x + 0xB60))))
                filas.append(dict(n=c.numero(), actores=actores))
                if c.numero() - inicio >= 140:
                    c.pausa()
                    foto = c.captura(OUT / 'original_7D7_05.png')
                    datos = dict(n=c.numero(), foto=foto, filas=filas)
                    (OUT / 'original_7D7.json').write_text(json.dumps(datos, indent=1), encoding='utf-8')
                    print(json.dumps(filas[-1]), flush=True)
                    break
            time.sleep(.015)
        else:
            c.pausa()
            c.captura(OUT / 'original_sin_ready_05.png')
            print('DIAGNOSTICO', hex(mgr), hex(cam), flush=True)
            raise RuntimeError('sin READY del original')
    finally:
        if c:
            c.p.s.close()
        if propio:
            s.main(['--instancia', 'pul', 'cerrar'])
        if preparado and not s.nuestro_pid():
            respaldo = OUT / 'PCSX2_pul.ini.original'
            if respaldo.exists():
                shutil.copy2(respaldo, destino / 'inis/PCSX2.ini')
                archivo = Path('D:/_PARA_BORRAR_CLAUDE/solPUL')
                archivo.mkdir(parents=True, exist_ok=True)
                shutil.move(str(respaldo), str(archivo / f'{time.time_ns()}_PCSX2_pul.ini.original'))
        reserva._soltar_plaza()


if __name__ == '__main__':
    main()
