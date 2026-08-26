#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""port_ps2_b3_verify.py - Paso 5 del pipeline port PS2 -> B3 HD.

Bucle de feedback: exporta el bin construido a OBJ (reusa awg0_export.py /
awg_to_obj_b3.py de awo_tools/) y reporta la salud de la geometria:
  - numero de vertices / triangulos
  - bounding box (min/max xyz) - plausible si ~[-2..3] (espacio local hueso)
  - conteo de NaN / inf

Uso:
  python port_ps2_b3_verify.py <bin.amb> [out.obj]
"""
import subprocess
import sys
import os
import math


def analyze_obj(path):
    verts, tris = 0, 0
    mins, maxs = [float('inf')] * 3, [float('-inf')] * 3
    n_nan = 0
    with open(path, 'r', errors='replace') as f:
        for line in f:
            if line.startswith('v '):
                v = [float(x) for x in line.split()[1:4]]
                verts += 1
                for i, c in enumerate(v):
                    if math.isnan(c) or math.isinf(c):
                        n_nan += 1
                        continue
                    mins[i] = min(mins[i], c)
                    maxs[i] = max(maxs[i], c)
            elif line.startswith('f '):
                tris += 1
    return verts, tris, mins, maxs, n_nan


def main():
    if len(sys.argv) < 2:
        print('Uso: port_ps2_b3_verify.py <bin.amb> [out.obj]')
        return
    binpath = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    awo_tools = os.path.normpath(os.path.join(here, '..', '..', 'awo_tools'))
    out = sys.argv[2] if len(sys.argv) > 2 else binpath + '.obj'
    # intentar awg0_export.py (AWG0 completo)
    exe = os.path.join(awo_tools, 'awg0_export.py')
    if not os.path.exists(exe):
        print('ERROR: no existe %s' % exe)
        return
    r = subprocess.run([sys.executable, exe, binpath, out], capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode != 0:
        print(r.stderr[-500:])
        return
    if not os.path.exists(out):
        print('no se genero OBJ')
        return
    verts, tris, mins, maxs, n_nan = analyze_obj(out)
    ok = n_nan == 0 and all(maxs[i] - mins[i] < 20 for i in range(3))
    print('OBJ: verts=%d tris=%d | bounds x[%.2f..%.2f] y[%.2f..%.2f] z[%.2f..%.2f] | NaN=%d' %
          (verts, tris, mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2], n_nan))
    print('VEREDICTO:', 'OK' if ok else 'REVISAR')


if __name__ == '__main__':
    main()