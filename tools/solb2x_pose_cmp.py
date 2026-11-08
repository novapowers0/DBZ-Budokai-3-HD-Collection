import json,sys,math
hd,pal=sys.argv[1],sys.argv[2]
H=[]
for l in open(hd+'/traza_hd.jsonl'):
    try:x=json.loads(l)
    except:continue
    if x['tipo']=='paso':H.append(x)
P=[r for r in json.load(open(pal)) if r.get('evento') and r.get('actores')]
print('PAL evento',len(P),P[0]['reloj'],P[-1]['reloj'],'HD',len(H),H[0]['cuadro'],H[-1]['cuadro'])
d=math.dist
def err(h,p):
    a=[x['pos'] for x in h['actores']];b=[x['pos'] for x in p['actores']]
    return min(max(d(a[0],b[0]),d(a[1],b[1])),max(d(a[0],b[1]),d(a[1],b[0])))
def run(s):
    e=[]
    for h in H:
        p=min(P,key=lambda r:abs(r['reloj']-(h['cuadro']+s)))
        e.append((err(h,p),h['cuadro']))
    return e
best=min((sum(x for x,_ in run(s))/len(H),s) for s in [x/2 for x in range(-40,41)])
e=run(best[1]);es=sorted(x for x,_ in e)
print('desfase',best[1],'media',round(best[0],1),'mediana',round(es[len(es)//2],1),'p90',round(es[int(len(es)*.9)],1),'max',round(es[-1],1),'<=5',sum(x<=5 for x in es),'<=15',sum(x<=15 for x in es),'<=30',sum(x<=30 for x in es),'/',len(es))
print('peores',sorted(e,reverse=True)[:6])
