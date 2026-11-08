import json,sys,math
D,P=sys.argv[1],sys.argv[2]
L=[json.loads(l) for l in open(D+'/traza_hd.jsonl')]
H=[x for x in L if x['tipo']=='paso']
pal=[r for r in json.load(open(P)) if r.get('evento') and r.get('escena_original')]
def cam(r):
    c=r['escena_original']['camaras']['aplicada'];return c['ojo'],c['objetivo'],c['fov']
def err(h,p):
    o,t,f=cam(p);l=h['logica']
    return max(math.dist(o,l['ojo']),math.dist(t,l['objetivo']))
for scale in (1.0,1.2,1/1.2):
    es=[]
    for h in H[::5]:
        t=h['cuadro']*scale
        p=min(pal,key=lambda r:abs(r['reloj']-t));es.append(err(h,p))
    print('escala',round(scale,3),'max',round(max(es),1),'media',round(sum(es)/len(es),1))
# mejor alineacion libre por paso
es=[min(err(h,p) for p in pal) for h in H[::5]];print('libre max',round(max(es),1))
for h in H[::40]:
    p=min(pal,key=lambda r:err(h,r));print(h['cuadro'],h['tiempo_camara'],round(p['reloj'],1),round(err(h,p),1))
print('---')
r=[]
for h in H:
    p=min(pal,key=lambda r:abs(r['reloj']-h['cuadro']));r.append((round(err(h,p),1),h['cuadro'],h['tiempo_camara'],h['indice_camara']))
r.sort(reverse=True);print(r[:8]);print(sum(1 for x in r if x[0]<=15),len(r))
print('===')
e=[]
for h in H:
    if h['indice_camara']<0: continue
    c=[err(h,p) for p in pal if abs(p['reloj']-h['cuadro'])<=2.5]
    e.append((min(c),h['cuadro']))
e.sort(reverse=True);print(len(e),e[:5],'<=1:',sum(1 for x in e if x[0]<=1),'<=15:',sum(1 for x in e if x[0]<=15))
