import json,struct,sys,math
import numpy as np
hd,pal=sys.argv[1],sys.argv[2]; shift=float(sys.argv[3]) if len(sys.argv)>3 else 0
P=[r for r in json.load(open(pal)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
pm=lambda raw:np.array([struct.unpack_from('<16f',raw,0x1480+0x40*j) for j in range(43)]).reshape(43,4,4)
Pm=[(r['reloj'],[pm(bytes.fromhex(a['modelo_raw'])) for a in r['actores']],[a['pos'] for a in r['actores']]) for r in P]
def ang(A,B):
    R=A[:,:3,:3]@np.transpose(B[:,:3,:3],(0,2,1));c=np.clip((np.trace(R,axis1=1,axis2=2)-1)/2,-1,1);return np.degrees(np.arccos(c))
out=[]
for l in open(hd+'/traza_hd.jsonl'):
    try:x=json.loads(l)
    except:continue
    if x.get('tipo')!='paso' or not any('modelo_raw' in a for a in x['actores']):continue
    t=x['cuadro']+shift;pr=min(Pm,key=lambda r:abs(r[0]-t))
    hp=[a['pos'] for a in x['actores']]
    perm=(0,1) if math.dist(hp[0],pr[2][0])+math.dist(hp[1],pr[2][1])<=math.dist(hp[0],pr[2][1])+math.dist(hp[1],pr[2][0]) else (1,0)
    for k in (0,1):
        if 'modelo_raw' not in x['actores'][k]:continue
        w=x['actores'][k]['modelo_raw'];b=struct.pack('<%dI'%len(w),*w)
        A=np.array([struct.unpack_from('<16f',b,0x24b0+0x40*j) for j in range(43)]).reshape(43,4,4)
        B=pr[1][perm[k]];a=ang(A,B);tr=np.linalg.norm(A[:,3,:3]-B[:,3,:3],axis=1)
        out.append((x['cuadro'],k,float(a.mean()),float(np.median(a)),float(tr.mean()),int((a<=5).sum())))
print('muestras',len(out))
for o in out[::6]:print(o[0],o[1],'giro medio',round(o[2],1),'mediana',round(o[3],1),'trasl media',round(o[4],2),'huesos<=5g',o[5],'/43')
