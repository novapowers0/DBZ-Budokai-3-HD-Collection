"""eB2f: valida el IK de suelo de B2 (src/escenarios_b2_ik.inl) contra una traza PAL con pose A.

El PS2 (FUN_001f2c50, justo antes de 1F68C0) sube el cuello a suelo+2,0-y_actor girando el abuelo del
cuello (STMC) y empuja manos (0,7) y pies (0,8) con un CCD sobre 4 ancestros. Eso es el giro extra del STMC
que se midio en tumbado/levantarse/aterrizaje (eB2d). Este script extrae las funciones C++ del .inl,
las compila en un ejecutable independiente (clang++ del PATH) y lo alimenta con la pose A (antes del IK) de
PAL; compara con las matrices finales de PAL.
uso: python tools/solb2x_ik_validar.py <traza_pal.json con --huesos --huesos-bytes 0x3600> [paso_ini paso_fin salto]
Hace falta una traza de solb2x_original.py con --huesos --huesos-bytes 0x3600 (campos pose_a, reposo, esqueleto_cab).
Medido (g8, tumbado): error maximo 14,3 grados sin IK -> 0,3-1,6 con el C++ del .inl."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

RAIZ = Path(__file__).resolve().parent.parent
N = 42   # filas del esqueleto PAL (sk+0x10)


def q2R(q):
    x, y, z, w = q
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])


def dd(A, B):
    return float(np.degrees(np.arccos(np.clip((np.trace(A @ B.T)-1)/2, -1, 1))))


DRIVER_MAIN = r'''
int main(int argc,char** argv){
  FILE* f=std::fopen(argv[1],"r");int n;double y,suelo;
  std::fscanf(f,"%d %lf %lf",&n,&y,&suelo);
  Esq e;e.n=n;e.par.assign(n,-1);
  for(int i=0;i<n;++i)std::fscanf(f,"%d",&e.par[i]);
  std::fscanf(f,"%d %d %d %d %d",&e.cuello,&e.nra,&e.nla,&e.nrf,&e.nlf);
  std::vector<M16> W(n);
  for(int i=0;i<n;++i)for(int k=0;k<16;++k)std::fscanf(f,"%lf",&W[i][k]);
  e.sub.assign(n,{});
  for(int j=0;j<n;++j){e.sub[j].push_back(j);for(int d=0;d<n;++d){if(d==j)continue;
    for(int a=e.par[d],g=0;a>0&&g<64;a=e.par[a],++g)if(a==j){e.sub[j].push_back(d);break;}}}
  Sucias().assign(n,0);
  bool h=Cabeza(W,e,y,suelo);bool l=false;
  l|=Extremidad(W,e,e.nlf,0.8,y,suelo);l|=Extremidad(W,e,e.nrf,0.8,y,suelo);
  l|=Extremidad(W,e,e.nla,0.7,y,suelo);l|=Extremidad(W,e,e.nra,0.7,y,suelo);
  std::printf("%d %d ",int(h),int(l));std::puts("");
  for(int i=0;i<n;++i){for(int k=0;k<16;++k)std::printf("%.9g ",W[i][k]);std::puts("");}
}
'''


def compilar_driver(destino: Path) -> Path:
    s = (RAIZ/'src/escenarios_b2_ik.inl').read_text(encoding='utf-8')
    cuerpo = s[s.index('struct Esq {'):s.index('inline bool Sufijo')]
    cabecera = ('#include <array>\n#include <vector>\n#include <cmath>\n#include <cstdio>\n#include <cstdlib>\n'
                '#include <string>\n#include <cstring>\nnamespace t{\n')
    cpp = destino/'ik_drv.cpp'
    cpp.write_text(cabecera+cuerpo+'\n}\nusing namespace t;\n'+DRIVER_MAIN, encoding='utf-8', newline='\n')
    exe = destino/'ik_drv.exe'
    r = subprocess.run(['clang++', '-O1', '-std=c++20', str(cpp), '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr[-2000:])
    return exe


def esqueleto(rec):
    rest = bytes.fromhex(rec['reposo'])
    base = struct.unpack_from('<I', bytes.fromhex(rec['esqueleto_cab']), 0x14)[0]
    par = []
    for b in range(N):
        pp = struct.unpack_from('<I', rest, 0x50*b+0x40)[0]
        par.append((pp-base)//0x50 if pp else -1)
    rt = [np.array(struct.unpack_from('<3f', rest, 0x50*b+0x10)) for b in range(N)]
    return par, rt


def pre_ik(rec, par, rt):
    pa = bytes.fromhex(rec['pose_a'])
    W = [np.eye(4) for _ in range(N)]
    for b in range(1, N):
        q = np.array(struct.unpack_from('<4f', pa, 0x40*b))
        t = np.array(struct.unpack_from('<3f', pa, 0x40*b+0x10))
        L = np.eye(4)
        L[:3, :3] = q2R(q).T
        L[3, :3] = t+rt[b]
        W[b] = L if par[b] <= 0 else L @ W[par[b]]
    return W


def indices(rec):
    """eB2i: cuello, NRA, NLA, NRF, NLF de la tabla actor+0x9AC (FUN_001f2c50 usa +4, +0x1E/+0x1C manos, +0x1A/+0x18 pies).
    El orden de huesos cambia entre actores (victima 11 10 28 39 33; atacante 17 16 34 7 39)."""
    t = rec.get('ik', {}).get('tabla_huesos') if isinstance(rec, dict) else None
    if not t:
        return '11 10 28 39 33'
    h = struct.unpack('<16h', bytes.fromhex(t)[:32])
    return '%d %d %d %d %d' % (h[2], h[15], h[14], h[13], h[12])


def escribir_entrada(ruta, W, par, y, idx='11 10 28 39 33'):
    txt = ['%d %.9g 0' % (N, y), ' '.join(str(p if p >= 0 else -1) for p in par), idx]
    txt += [' '.join('%.9g' % v for v in W[k].flatten()) for k in range(N)]
    ruta.write_text('\n'.join(txt), encoding='ascii')


def ejecutar(exe, ruta):
    out = subprocess.run([str(exe), str(ruta)], capture_output=True, text=True).stdout.split('\n')
    return out[0].strip(), [np.array([float(v) for v in out[1+k].split()]).reshape(4, 4) for k in range(N)]


def main(traza, ini=0, fin=10**9, salto=4):
    P = [r for r in json.load(open(traza)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        exe = compilar_driver(td)
        par, rt = esqueleto(P[0]['actores'][1])
        tot = []
        for i in range(ini, min(fin, len(P)), salto):
            a = P[i]['actores'][1]
            if not a.get('pose_a'):
                continue
            W = pre_ik(a, par, rt)
            raw = bytes.fromhex(a['modelo_raw'])
            F = [np.array(struct.unpack_from('<16f', raw, 0x1480+0x40*b)).reshape(4, 4) for b in range(N)]
            escribir_entrada(td/'in.txt', W, par, a['pos'][1], indices(a))
            flags, W2 = ejecutar(exe, td/'in.txt')
            e0 = max(dd(W[k][:3, :3].T, F[k][:3, :3].T) for k in range(1, N))
            e1 = [dd(W2[k][:3, :3].T, F[k][:3, :3].T) for k in range(1, N)]
            tot.append((i, a['codigo'], flags, e0, max(e1), float(np.mean(e1))))
            print('paso %d codigo %d (cabeza,extremidades)=%s  max sin IK %.1f | con IK max %.1f media %.2f' % tot[-1])
        return tot


if __name__ == '__main__':
    a = sys.argv
    main(a[1], int(a[2]) if len(a) > 2 else 0, int(a[3]) if len(a) > 3 else 10**9, int(a[4]) if len(a) > 4 else 4)
