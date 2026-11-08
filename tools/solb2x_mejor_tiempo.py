"""eB2h: para cada cuadro HD de la victima, el reloj PAL cuya pose se parece mas (mide retrasos de reloj del clip). Uso: python tools/solb2x_mejor_tiempo.py <traza_pal.json> <carpeta_hd> <cuadro_ini> <cuadro_fin> (desde la raiz del repo)."""
import json,sys
import numpy as np
sys.path[:0]=['tools']
import solb2x_huesos_cmp3 as C, solb2x_huesos_cmp4 as M
from solb2x_escena_resumen import interp
pal,hd,c0,c1=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4])
P=[r for r in json.load(open(pal)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
pt=np.array([r['reloj'] for r in P]);H=C.hd_pasos(hd);com=[n for n in M.PAL_IDX if n in M.HD_IDX]
for x in H:
    c=round(x['cuadro'])
    if not c0<=c<=c1: continue
    Rh=[m[:3,:3].T for m in C.hd_mats(x['actores'][1])];best=(1e9,0)
    for t in np.arange(c-1-8,c-1+8,0.25):
        if not pt[0]<=t<=pt[-1]: continue
        Rp=interp(P,pt,t,1);e=np.mean([C.dd(Rp[M.PAL_IDX[n]],Rh[M.HD_IDX[n]]) for n in com])
        if e<best[0]:best=(e,t)
    print(c,x['actores'][1]['codigo'],'tiempo_modelo',round(x['actores'][1]['tiempo_modelo'],1),'cuadro_anim',round(x['actores'][1]['cuadro_anim'],1),'mejor PAL reloj %.2f (c-1=%d) err %.1f'%(best[1],c-1,best[0]))
