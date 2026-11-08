import json,struct,numpy as np,sys,re
import solb2x_acm as acm
hd=sys.argv[1]; pal='D:/tmp/solB2X/original_g8_e0_20261105_185240/traza.json'
B2N=acm.info(acm.load('D:/DBZ3HD/smoke_d/mods/Calle de la ciudad B2/transicion_b2.b2t',8))[3]
suf=lambda n:n.split('_',1)[1] if '_' in n else n
P=[r for r in json.load(open(pal)) if r.get('evento') and r['actores'][0].get('modelo_raw')]
pm=lambda raw:np.array([struct.unpack_from('<16f',raw,0x1480+0x40*j) for j in range(43)]).reshape(43,4,4)
H=[]
for l in open(hd+'/traza_hd.jsonl'):
    try:x=json.loads(l)
    except:continue
    if x.get('tipo')=='paso' and all('modelo_raw' in a for a in x['actores']):H.append(x)
names=None
def hdm(a):
    global names
    names=a['acm_nombres']
    w=a['modelo_raw'];b=struct.pack('<%dI'%len(w),*w)
    return np.array([struct.unpack_from('<16f',b,0x24b0+0x40*j) for j in range(len(names))]).reshape(-1,4,4)
def rel(M):  # rotaciones relativas al hueso 0 (entradas 3x3 fila-mayor)
    R0=M[0,:3,:3]
    return np.array([m[:3,:3]@R0.T for m in M]),R0
def mapa(hn):
    d={suf(n.lstrip('X') if False else n).lstrip('X'):i for i,n in enumerate(hn)}
    d2={}
    for i,n in enumerate(hn):d2[re.sub(r'^X?[A-Z0-9]*?_','',n,count=1)]=i
    out={}
    for i,n in enumerate(B2N):
        s=re.sub(r'^X?[A-Z0-9]*?_','',n,count=1) if '_' in n else n
        if s in d2:out[i]=d2[s]
    return out
def err(A,B,mp):
    Ra,_=rel(A);Rb,_=rel(B);e={}
    for i,j in mp.items():
        R=Rb[i]@Ra[j].T;e[i]=np.degrees(np.arccos(np.clip((np.trace(R)-1)/2,-1,1)))
    return e
if __name__=='__main__':
    k=1
    pv=pm(bytes.fromhex(P[int(sys.argv[2])]['actores'][k]['modelo_raw']))
    print('PAL idx',sys.argv[2],'t',P[int(sys.argv[2])]['actores'][k]['tiempo_reaccion'])
    for x in H:
        a=x['actores'][k];A=hdm(a);mp=mapa(names)
        e=err(A,pv,mp)
        v=np.array(list(e.values()))
        print(x['cuadro'],hex(a['clip_modelo']),round(a['tiempo_modelo'],1),'mapeados',len(mp),'media',round(v.mean(),1),'med',round(float(np.median(v)),1),'<=5:',int((v<=5).sum()))
