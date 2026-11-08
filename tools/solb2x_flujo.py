"""eB2h: flujo de estados de la victima PAL frente a HD con el error medio de pose por tramo de estado. Uso: python tools/solb2x_flujo.py <traza_pal.json> <carpeta_hd> <desfase=1.0> (desde la raiz del repo)."""
import json,sys
import numpy as np
sys.path[:0]=['tools']
import solb2x_huesos_cmp3 as C, solb2x_huesos_cmp4 as M
from solb2x_escena_resumen import interp
pal,hd,off=sys.argv[1],sys.argv[2],float(sys.argv[3])
P=[r for r in json.load(open(pal)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
pt=np.array([r['reloj'] for r in P]);H=C.hd_pasos(hd)
print('PAL victima')
last=None
for r in P:
    a=r['actores'][1];s=(a['codigo'],hex(a['clip']))
    if s!=last:print(' ',round(r['reloj'],1),s);last=s
print('HD victima + error por tramo')
com=[n for n in M.PAL_IDX if n in M.HD_IDX];last=None;acc=[]
def flush():
    if acc:print('   -> n',len(acc),'media %.1f max %.1f'%(np.mean(acc),max(acc)))
for x in H:
    a=x['actores'][1];s=(a['codigo'],hex(a['clip_modelo']),a.get('accion'))
    t=x['cuadro']-off
    if s!=last:
        flush();acc=[];print(' ',round(x['cuadro']),s,round(a['tiempo_modelo'],1),[round(v,1) for v in a['pos']]);last=s
    if pt[0]<=t<=pt[-1]:
        Rp=interp(P,pt,t,1);Rh=[m[:3,:3].T for m in C.hd_mats(a)]
        acc.append(np.mean([C.dd(Rp[M.PAL_IDX[n]],Rh[M.HD_IDX[n]]) for n in com]))
flush()
