"""eB2h: lista de muestras de la victima con error medio >5 grados (PAL interpolado). Uso: python tools/solb2x_victima_mala.py <traza_pal.json> <carpeta_hd> <desfase> (desde la raiz del repo)."""
import json,sys
import numpy as np
sys.path[:0]=['tools']
import solb2x_huesos_cmp3 as C, solb2x_huesos_cmp4 as M
from solb2x_escena_resumen import interp
pal,hd,off=sys.argv[1],sys.argv[2],float(sys.argv[3])
P=[r for r in json.load(open(pal)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
pt=np.array([r['reloj'] for r in P]);H=C.hd_pasos(hd)
com=[n for n in M.PAL_IDX if n in M.HD_IDX];bad=[]
for x in H:
    t=x['cuadro']-off
    if not pt[0]<=t<=pt[-1]: continue
    Rp=interp(P,pt,t,1);Rh=[m[:3,:3].T for m in C.hd_mats(x['actores'][1])]
    e=np.mean([C.dd(Rp[M.PAL_IDX[n]],Rh[M.HD_IDX[n]]) for n in com])
    if e>5: bad.append((round(x['cuadro']),x['actores'][1]['codigo'],round(e,1)))
print(len(bad),bad)
