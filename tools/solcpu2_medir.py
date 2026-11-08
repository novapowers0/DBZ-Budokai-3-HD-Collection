"""A/B de la espera con copias fijas de pruebas.py; 100 % CPU = un nucleo.

No mide latencia boton-pantalla ni certifica audio. La sonda de sueno mide
tiempo bloqueado real, no el instante en que cambia el contador del guest.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import time

import pruebas as P


K = P.K32
K.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
K.SetProcessAffinityMask.restype = P.wt.BOOL
K.GetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t),
                                   ctypes.POINTER(ctypes.c_size_t)]
K.GetProcessAffinityMask.restype = P.wt.BOOL


def cpu(h):
    valores = [P.wt.FILETIME() for _ in range(4)]
    if not K.GetProcessTimes(h, *(ctypes.byref(v) for v in valores)):
        raise ctypes.WinError(ctypes.get_last_error())
    return sum((v.dwHighDateTime << 32) | v.dwLowDateTime for v in valores[2:]) / 1e7


def medir(j, par):
    P._esperar_pelea(j, 120)
    mascara = int(par['mascara'])
    h = K.OpenProcess(0x0600, False, j.proc.pid)
    if not h:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        if not K.SetProcessAffinityMask(h, mascara):
            raise ctypes.WinError(ctypes.get_last_error())
        actual, sistema = ctypes.c_size_t(), ctypes.c_size_t()
        if not K.GetProcessAffinityMask(h, ctypes.byref(actual), ctypes.byref(sistema)):
            raise ctypes.WinError(ctypes.get_last_error())
        if actual.value != mascara:
            raise P.Fallo('La afinidad no coincide con la solicitada')
        time.sleep(5)
        previo = j.texto_log()
        c0, t0 = cpu(h), time.perf_counter()
        time.sleep(float(par['seg']))
        dur, consumido = time.perf_counter() - t0, cpu(h) - c0
        texto = j.texto_log()[len(previo):]
        perf = [dict(fps=float(a), p1=float(b), p99_ms=float(c), max_ms=float(d))
                for a, b, c, d in re.findall(
                    r'perf fps=([\d.]+) p1=([\d.]+) p99_ms=([\d.]+).*?max_frame_ms=([\d.]+)', texto)]
        if len(perf) < 3:
            raise P.Fallo('Menos de tres ventanas perf durante la medida')
        suenos = [dict(zip(('solicitado_us', 'n', 'total_us', 'max_us', 'mas1ms', 'mas5ms'), map(int, v)))
                  for v in re.findall(r'espera_sonda lr=[0-9A-F]+ solicitado_us=(\d+) n=(\d+) '
                                      r'total_us=(\d+) max_us=(\d+) mas1ms=(\d+) mas5ms=(\d+)', texto)]
        resultado = dict(mascara=mascara, dormir_us=int(par['dormir']), segundos=dur,
                         cpu_pct=100 * consumido / dur, ventanas=perf, suenos=suenos,
                         medianas={k: statistics.median(f[k] for f in perf) for k in perf[0]},
                         input_diag=[l for l in texto.splitlines() if 'input_diag' in l],
                         alcance='combate vivo; no latencia boton-pantalla ni equivalencia de sonido')
        (j.run / 'medida_cpu.json').write_text(json.dumps(resultado, indent=2), encoding='utf-8')
        j.info(f"CPU={resultado['cpu_pct']:.1f}% mascara={mascara} {resultado['medianas']}")
    finally:
        K.CloseHandle(h)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--salida', type=Path, required=True)
    ap.add_argument('--seg', type=float, default=35)
    ap.add_argument('--copia', default='smoke_b')
    ap.add_argument('--casos', default='5:0,5:100,1:0,1:100', help='mascara:microsegundos, separados por coma')
    ap.add_argument('--sonda', action='store_true', help='Medir el sueno real; altera el coste del camino activo')
    a = ap.parse_args()
    os.environ.update(DBZ3_AGENTE='solCPU2', PYTHONUTF8='1', DBZ3_PRUEBAS_LENTO='1')
    P.OVERRIDES.update(dbz3_perf_logging='true', frame_cap='60', dbz3_hd_textures='1')
    # Estas pruebas son numericas: no se piden capturas ni se crean PNG.
    P.Juego.captura = lambda self, *args, **kwargs: None
    a.salida.mkdir(parents=True, exist_ok=False)
    registros = []
    for mascara, dormir in (tuple(map(int, caso.split(':'))) for caso in a.casos.split(',')):
        clave = f'solCPU2_cpu_{mascara}_{dormir}'
        env = dict(DBZ3_TEST_BATTLE='0,1,0,-1,100000,100000', DBZ3_HUB_MODE='5',
                   DBZ3_TEST_OCULTO='1', DBZ3_TEST_TURBO='1',
                   REX_DBZ3_ESPERA_DORMIR_US=str(dormir),
                   DBZ3_TEST_ESPERA_SONDA='1' if dormir and a.sonda else '0')
        P.OVERRIDES['dbz3_espera_dormir_us'] = str(dormir)
        P.ESCENARIOS[clave] = ('solCPU2: combate fijo, CPU y cadencia', lambda _, e=env: e, medir)
        r = P.correr(a.copia, clave, [], None,
                     dict(mascara=str(mascara), dormir=str(dormir), seg=str(a.seg)), True)
        f = Path(r['run']) / 'medida_cpu.json'
        registros.append(dict(resultado=r, medida=json.loads(f.read_text()) if f.exists() else None))
        (a.salida / 'resultados.json').write_text(json.dumps(registros, indent=2, default=str), encoding='utf-8')
        if not r['ok']:
            return 1
    exe = Path(P.EXE)
    (a.salida / 'exe_sha256.txt').write_text(hashlib.sha256(exe.read_bytes()).hexdigest(), encoding='ascii')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
