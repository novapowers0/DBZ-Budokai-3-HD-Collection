import json,struct,sys,math
import numpy as np
exec(open('D:/tmp/eB2a_huesos2.py').read().split("out=[]")[0].replace("hd,pal=sys.argv[1],sys.argv[2]; shift=float(sys.argv[3]) if len(sys.argv)>3 else 0","hd,pal,shift=sys.argv[1],sys.argv[2],0"))
acc=np.zeros((43,45));n=0
for l in open(hd+'/traza_hd.jsonl'):
    try:x=json.loads(l)
    except:continue
    if x.get('tipo')!='paso' or 'modelo_raw' not in x['actores'][0]:continue
    t=x['cuadro'];pr=min(Pm,key=lambda r:abs(r[0]-t))
    hp=x['actores'][0]['pos'];k=0 if math.dist(hp,pr[2][0])<math.dist(hp,pr[2][1]) else 1
    w=x['actores'][0]['modelo_raw'];b=struct.pack('<%dI'%len(w),*w)
    A=np.array([struct.unpack_from('<16f',b,0x24b0+0x40*j) for j in range(45)]).reshape(45,4,4);B=pr[1][k]
    R=np.einsum('jab,icb->jiac',B[:,:3,:3],A[:,:3,:3])  # B_j * A_i^T
    c=np.clip((np.einsum('jiaa->ji',R)-1)/2,-1,1);acc+=np.degrees(np.arccos(c));n+=1
acc/=n;print('muestras',n)
best=acc.argmin(1);print([ (j,int(best[j]),round(float(acc[j,best[j]]),1)) for j in range(43)])
print('huesos con error medio<8g:',int((acc.min(1)<8).sum()),'identidad:',int((best==np.arange(43)).sum()))
