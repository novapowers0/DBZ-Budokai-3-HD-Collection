"""eB2g: valida la mirada procedimental de B2 (src/escenarios_b2_ik.inl, funcion Mirada) contra una traza PAL con pose A.

El PS2 (FUN_001F6D80) gira cuello y cabeza del luchador hacia la cabeza del rival cuando el clip trae el flag 0x20.
Este script compila el C++ del .inl, lo alimenta con la pose A (antes de la mirada) de PAL y compara la rotacion
LOCAL de cuello/cabeza con las matrices finales de PAL. Referencia: g8 Ciudad (actor 0, atacante).
uso: python tools/solb2x_mirada_validar.py <carpeta_traza_pal con pose_a> [carpeta_con_reposo]"""
import json, math, struct, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import solb2x_ik_validar as V

RAIZ = Path(__file__).resolve().parent.parent
N = V.N
DRIVER = r'''
int main(int argc,char** argv){
  FILE* f=std::fopen(argv[1],"r");int n,s,c,h,nf;
  std::fscanf(f,"%d %d %d %d %d",&n,&s,&c,&h,&nf);
  Esq e;e.n=n;e.par.assign(n,-1);e.stmc=s;e.cuello=c;e.cabeza=h;e.rt.assign(n,{0,0,0});
  for(int i=0;i<n;++i)std::fscanf(f,"%d",&e.par[i]);
  for(int i=0;i<n;++i)std::fscanf(f,"%lf %lf %lf",&e.rt[i][0],&e.rt[i][1],&e.rt[i][2]);
  e.sub.assign(n,{});
  for(int j=0;j<n;++j){e.sub[j].push_back(j);for(int d=0;d<n;++d){if(d==j)continue;
    for(int a=e.par[d],g=0;a>0&&g<64;a=e.par[a],++g)if(a==j){e.sub[j].push_back(d);break;}}}
  MiradaEst st;
  for(int fr=0;fr<nf;++fr){
    R3 Mx;V3 pos,T;double k;int flag;
    for(int i=0;i<9;++i)std::fscanf(f,"%lf",&Mx[i]);
    std::fscanf(f,"%lf %lf %lf %lf %lf %lf %lf %d",&pos.x,&pos.y,&pos.z,&T.x,&T.y,&T.z,&k,&flag);
    std::vector<M16> W(n);
    for(int i=0;i<n;++i)for(int q=0;q<16;++q)std::fscanf(f,"%lf",&W[i][q]);
    Sucias().assign(n,0);
    bool m=Mirada(W,e,Mx,pos,T,k,st,flag,false);
    std::printf("%d %.9g\n",int(m),st.w);
    for(int i=0;i<n;++i){for(int q=0;q<16;++q)std::printf("%.9g ",W[i][q]);std::puts("");}
  }
}
'''
def compilar(dest):
    s = (RAIZ/'src/escenarios_b2_ik.inl').read_text(encoding='utf-8')
    cuerpo = s[s.index('struct Esq {'):s.index('inline bool Sufijo')]
    cab = '#include <array>\n#include <vector>\n#include <cmath>\n#include <cstdio>\n#include <cstdlib>\n#include <string>\n#include <cstring>\n#include <algorithm>\nnamespace t{\n'
    cpp = dest/'mir.cpp'
    cpp.write_text(cab+cuerpo+'\n}\nusing namespace t;\n'+DRIVER, encoding='utf-8', newline='\n')
    exe = dest/'mir.exe'
    r = subprocess.run(['clang++', '-O1', '-std=c++20', str(cpp), '-o', str(exe)], capture_output=True, text=True)
    if r.returncode: raise RuntimeError(r.stderr[-3000:])
    return exe
def roty(t):
    c, s = math.cos(t), math.sin(t); return np.array([[c,0,-s],[0,1,0],[s,0,c]])
def theta(g): return ((((g+0x8000)&0xffff)*2-0xffff)/65535.0)*math.pi
def final(a):
    raw = bytes.fromhex(a['modelo_raw'])
    return [np.array(struct.unpack_from('<16f', raw, 0x1480+0x40*b)).reshape(4,4) for b in range(N)]
def load(path):
    return [r for r in json.load(open(path+'/traza.json')) if r.get('evento') and r['actores'][0].get('modelo_raw')]
def main(pal, ref=None):
    P = load(pal)
    R0 = next(r for r in load(ref or pal) if r['actores'][0].get('reposo'))
    par, rt = V.esqueleto(R0['actores'][0])
    ac = struct.unpack('<16h', bytes.fromhex(R0['actores'][0]['ik']['tabla_huesos'])[:32])
    s, c, h = ac[1], ac[2], ac[3]
    with tempfile.TemporaryDirectory() as td:
        td = Path(td); exe = compilar(td)
        L = []; idx = []
        for i, r in enumerate(P):
            a, b = r['actores'][0], r['actores'][1]
            if not a.get('pose_a'): continue
            W = V.pre_ik(a, par, rt)
            Pl = roty(theta(a['giro'])); Mx = Pl.T
            Fb = final(b); T = Fb[12][3,:3] @ roty(theta(b['giro'])) + np.array(b['pos'])
            flag = (1 if r['reloj'] >= 15.6 else 0) if a['codigo'] in (573, 0) else 2
            L.append(' '.join('%.9g' % v for v in Mx.flatten())+' '+' '.join('%.9g' % v for v in a['pos'])+' '+
                     ' '.join('%.9g' % v for v in T)+' 1.2 %d' % flag)
            L += [' '.join('%.9g' % v for v in W[k].flatten()) for k in range(N)]
            idx.append(i)
        hdr = ['%d %d %d %d %d' % (N, s, c, h, len(idx)), ' '.join(str(p) for p in par)]
        hdr += [' '.join('%.9g' % v for v in rt[k]) for k in range(N)]
        (td/'in.txt').write_text('\n'.join(hdr+L), encoding='ascii')
        out = subprocess.run([str(exe), str(td/'in.txt')], capture_output=True, text=True).stdout.split('\n')
    def loc(M, b): return M[b][:3,:3] @ np.linalg.inv(M[par[b]][:3,:3])
    ln = 0; err = []
    for i in idx:
        head = out[ln].split(); ln += 1
        W2 = [np.array([float(v) for v in out[ln+k].split()]).reshape(4,4) for k in range(N)]; ln += N
        a = P[i]['actores'][0]; F = final(a)
        err.append((i, a['codigo'], float(head[1]), V.dd(loc(W2,c).T, loc(F,c).T), V.dd(loc(W2,h).T, loc(F,h).T)))
    for e in err[::8]: print('paso %3d codigo %4d w %.2f | cuello %.1f cabeza %.1f' % e)
    arr = np.array([[e[3], e[4]] for e in err])
    print('media cuello %.2f cabeza %.2f | max cuello %.1f cabeza %.1f | pasos con ambos <=5: %d de %d' % (
        arr[:,0].mean(), arr[:,1].mean(), arr[:,0].max(), arr[:,1].max(), int(((arr<=5).all(1)).sum()), len(arr)))
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
