"""eB2h: lista de muestras del atacante con mas de 2 huesos >5 grados (PAL interpolado). Uso: python tools/solb2x_atacante_mala.py <traza_pal.json> <carpeta_hd> <desfase> (desde la raiz del repo)."""
import json,sys
import numpy as np
sys.path[:0]=['tools']
import solb2x_huesos_cmp3 as C, solb2x_huesos_cmp5 as C5
from solb2x_escena_resumen import interp
pal,hd,off=sys.argv[1],sys.argv[2],float(sys.argv[3])
P=[r for r in json.load(open(pal)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
pt=np.array([r['reloj'] for r in P]);H=C.hd_pasos(hd)
mp=C5.mapa_atacante(H[0]['actores'][0]['acm_nombres'])
tr=[];
for x in H:
    t=x['cuadro']-off
    if not pt[0]<=t<=pt[-1]: continue
    Rp=interp(P,pt,t,0);Rh=[m[:3,:3].T for m in C.hd_mats(x['actores'][0])]
    e={n:C.dd(Rp[a],Rh[b]) for n,(a,b) in mp.items()}
    bad=[n for n,v in e.items() if v>5]
    a=x['actores'][0]
    if len(bad)>2: tr.append((round(x['cuadro']),a['codigo'],hex(a['clip_modelo']),round(a['tiempo_modelo'],1),len(bad),sorted(((round(v,0),n[4:]) for n,v in e.items() if v>5),reverse=True)[:4]))
# compress
last=None
for t in tr:
    if last and t[0]-last[0]<=1 and t[1:3]==last[1:3]: last=t;continue
    print(t);last=t
print([t[0] for t in tr]);print(len(tr),'frames con >2 malos')
