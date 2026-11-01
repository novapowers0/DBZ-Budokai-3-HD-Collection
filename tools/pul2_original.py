"""Cotejo controlado de los avisos C/D originales; reutiliza copia privada sin crear otra."""
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

os.environ['DBZ3_AGENTE'] = 'solPUL2'
os.environ['PYTHONUTF8'] = '1'
OUT = Path('D:/tmp/solPUL2/cotejo_ready')


def main():
    import pcsx2_cuadros as C
    import pruebas as P
    sys.path.insert(0, r'E:\DBZ3HD_trabajo\R7\emu')
    import sonda as s
    from pcsx2_turnos import process_image

    def pids():
        r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq pcsx2.exe', '/FO', 'CSV', '/NH'],
                           capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, check=True)
        return [int(p[1]) for l in r.stdout.decode('mbcs', errors='replace').splitlines()
                if len(p := l.split('\",\"')) > 1]

    s._pids, s._exe_de = pids, process_image
    destino = Path('D:/tmp/solPUL/pcsx2_pul')
    s.INSTANCIAS['pul2'] = (28029, str(destino), str(OUT / 'capturas'), 'instancia_pul2')
    s = C.conectar('pul2')
    os.environ['DBZ3_PCSX2_CUADROS_TMP'] = str(OUT)
    os.environ['DBZ3_PCSX2_CUADROS_RESPALDO'] = 'D:/_PARA_BORRAR_CLAUDE/solPUL2'
    if '--restaurar' in sys.argv:
        assert not s.nuestro_pid(), 'copia privada ocupada; no restaurar en marcha'
        respaldo = OUT / 'PCSX2_pul2.ini.original'
        assert respaldo.exists(), 'sin configuracion pendiente'
        shutil.copy2(respaldo, destino / 'inis/PCSX2.ini')
        assert respaldo.read_bytes() == (destino / 'inis/PCSX2.ini').read_bytes()
        archivo = Path('D:/_PARA_BORRAR_CLAUDE/solPUL2')
        archivo.mkdir(parents=True, exist_ok=True)
        shutil.move(str(respaldo), str(archivo / f'{time.time_ns()}_PCSX2_pul2_recuperada.ini'))
        print('Configuracion original restaurada y verificada; copia privada cerrada.', flush=True)
        return
    reserva = P.Juego('sol', 'solPUL2_original')
    while not reserva._tomar_plaza(False):
        time.sleep(5)
    c = None
    propio = preparado = False
    try:
        assert not s.nuestro_pid(), 'copia privada ocupada'
        C.preparar(s)
        preparado = True
        ini = destino / 'inis/PCSX2.ini'
        texto = ini.read_text(encoding='utf-8-sig')
        import re
        texto, n = re.subn(r'PINESlot\s*=\s*\d+', 'PINESlot = 28029', texto)
        assert n == 1, 'PINE ambiguo'
        ini.write_text(texto, encoding='utf-8')
        p = s.lanzar('iw', 4)
        propio = True
        p.s.close()
        c = C.Cuadros(s, 0x4A4D40)
        p = c.p
        P.U32.GetMonitorInfoW.argtypes = [P.wt.HMONITOR, ctypes.POINTER(P._MONINFO)]
        P._a_pantalla_secundaria(type('Proceso', (), {'pid': s.nuestro_pid(), 'poll': lambda _: None})())
        th = threading.Thread(target=lambda: s.teclas('right espera300 z espera800 down down z'))
        th.start()
        fin = time.monotonic() + 30
        while time.monotonic() < fin:
            if p.u32(0x5C0DC8) == 0x7D0:
                p.escribir(0x5C0DC4, struct.pack('<II', 16, 0x7DF))
                break
            time.sleep(.02)
        else:
            raise RuntimeError('sin seleccion de mision')
        th.join()
        fin = time.monotonic() + 100
        while time.monotonic() < fin:
            mgr, obj, ficha, ui = p.lote([(0x49CA70, 4), (0x49CBC0, 4), (0x49CBA4, 4), (0x49CD8C, 4)])
            if all(0x100000 < a < 0x2000000 for a in (mgr, obj, ficha, ui)):
                estado = int.from_bytes(p.leer(mgr + 0x14BC, 2), 'little')
                if p.u32(ficha + 4) == 16 and estado == 0 and p.u32(ui + 0x150) & 2 and p.u32(obj + 0x248) == 0 and p.u32(obj + 0x180) == 0:
                    break
            time.sleep(.03)
        else:
            raise RuntimeError('sin juego activo MSNMW')
        resultados = []
        for letra, off in [('C', 0x1D8), ('D', 0x1DC)]:
            c.pausa()
            # Disparador controlado: se modifica solo el progreso pendiente,
            # nunca las texturas, retratos, voz ni rutina que los selecciona.
            p.escribir(obj + off, struct.pack('<I', 1))
            c.pulso(0x75)
            time.sleep(.9)
            c.pausa()
            dato = dict(letra=letra, objeto=hex(obj), flag=p.u32(obj + off),
                        temporizador=p.u32(obj + 0x17C),
                        foto=c.captura(OUT / ('original_' + letra + '_05.png')))
            resultados.append(dato)
            print(json.dumps(dato), flush=True)
            assert dato['flag'] == 0 and dato['temporizador'] != 0, 'aviso no consumido: no aprobar foto'
            c.pulso(0x75)
            time.sleep(4.2)
        (OUT / 'original.json').write_text(json.dumps(resultados, indent=2), encoding='utf-8')
    finally:
        if c:
            c.p.s.close()
        if propio:
            s.main(['--instancia', 'pul2', 'cerrar'])
        if preparado and not s.nuestro_pid():
            respaldo = OUT / 'PCSX2_pul2.ini.original'
            if respaldo.exists():
                shutil.copy2(respaldo, destino / 'inis/PCSX2.ini')
                archivo = Path('D:/_PARA_BORRAR_CLAUDE/solPUL2')
                archivo.mkdir(parents=True, exist_ok=True)
                shutil.move(str(respaldo), str(archivo / f'{time.time_ns()}_PCSX2_pul2.ini.original'))
        reserva._soltar_plaza()


if __name__ == '__main__':
    main()
