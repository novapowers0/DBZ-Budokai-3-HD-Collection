"""Ejecutor secuencial del carril R16; todas las pruebas pasan por pruebas.py."""
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    fallos = []
    for fase in ('lamina', 'guardar', 'forma'):
        for raza in ('freezer', 'namekiano', 'majin'):
            cmd = [sys.executable, '-u', 'tools/pruebas.py', 'correr', 'smoke_e', 'razas_b3',
                   '--par', 'raza='+raza, '--par', 'fase='+fase]
            print('\nR16 INICIO', fase, raza, time.strftime('%H:%M:%S'), flush=True)
            rc = subprocess.run(cmd, cwd=ROOT).returncode
            print('R16 FIN', fase, raza, 'codigo', rc, flush=True)
            if rc:
                fallos.append((fase, raza, rc))
    cmd = [sys.executable, '-u', 'tools/pruebas.py', 'todo', '--max', '3']
    print('\nR16 BATERIA', flush=True)
    rc = subprocess.run(cmd, cwd=ROOT).returncode
    print('R16 RESULTADO', dict(fallos=fallos, bateria=rc), flush=True)
    return int(bool(fallos) or bool(rc))


if __name__ == '__main__':
    sys.exit(main())
