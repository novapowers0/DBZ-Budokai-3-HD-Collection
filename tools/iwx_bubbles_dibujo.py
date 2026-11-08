"""Lee IWPB1 del dibujo real; compara LIVE por codigo y cuadro PAL exacto.

La coincidencia de codigo/cuadro no prueba IC, camara, piel ni paridad visual.
No modifica el juego, las capturas originales ni sus tolerancias.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct


def leer(path,vertices=False):
    with path.open('rb') as f:
        ordinal=0
        while True:
            cab=f.read(32)
            if not cab:
                return
            if len(cab)!=32:
                raise ValueError('Cabecera IWPB truncada')
            magic,version,i,fase,codigo,estado,tick,nt=struct.unpack('<8I',cab)
            if magic!=0x42505749 or version!=1 or i!=ordinal or nt>4096:
                raise ValueError('Contrato IWPB1 incompatible')
            n=64+47*128+8*128+64+20+nt*72
            raw=f.read(n)
            if len(raw)!=n:
                raise ValueError('Registro IWPB incompleto')
            meta=struct.unpack_from('<16f',raw)
            cuerpo=raw[64:64+47*128]
            cara=raw[64+47*128:64+55*128]
            camara=struct.unpack_from('<21f',raw,64+55*128)
            verts=list(struct.iter_unpack('<6f',raw[64+55*128+84:]))
            if not all(math.isfinite(x) for x in meta+camara) or not all(
                    math.isfinite(x) for v in verts for x in v):
                raise ValueError('Dibujo IWPB no finito')
            for live in (cuerpo,cara):
                for off in range(0,len(live),128):
                    if not all(math.isfinite(x) for x in struct.unpack_from('<28f',live,off)):
                        raise ValueError('LIVE IWPB no finito')
            registro=dict(i=i,fase=fase,codigo=codigo,estado=estado,tick=tick,
                cuadro=meta[0],u=meta[1],rate=meta[2],actor=meta[3:6],raiz=meta[6:9],
                dibujo=meta[9:12],yaw=meta[12],centro=meta[13:16],
                cuerpo=cuerpo,cara=cara,camara=camara,triangulos=nt,
                bounds=[[min(v[k] for v in verts),max(v[k] for v in verts)] for k in range(3)]
                    if verts else [],
                 vertices_sha256=hashlib.sha256(raw[64+55*128+84:]).hexdigest())
            if vertices:
                registro['vertices']=verts
            yield registro
            ordinal+=1


def clave(codigo,cuadro):
    return codigo,struct.pack('<f',cuadro)


def original(path):
    indice={}
    with path.open(encoding='utf-8') as f:
        for line in f:
            r=json.loads(line)
            n=r['bubbles']['npc']
            fase={15:0,10:1,0:2}.get(n['estado'])
            if fase is not None:
                indice.setdefault((fase,clave(n['codigo'],r['bubbles_animacion']['frame'])),[]).append(r)
    return indice


def comparar(port,ref):
    esperado=b''.join(bytes.fromhex(x['raw']) for x in ref['bubbles']['npc']['mat47_128'])
    if len(esperado)!=47*128:
        raise ValueError('Original sin LIVE47 completo')
    facetas={}
    for nombre,off,n in [('matriz',0,16),('q',64,4),('p',80,3),('s',96,3)]:
        errores=[]
        for i in range(47):
            a=struct.unpack_from('<'+'f'*n,port['cuerpo'],128*i+off)
            b=struct.unpack_from('<'+'f'*n,esperado,128*i+off)
            errores.append(max(abs(x-y) for x,y in zip(a,b)))
        facetas[nombre]=dict(maximo=max(errores),huesos_distintos=sum(e!=0 for e in errores),
            peor_hueso=errores.index(max(errores)))
    n=ref['bubbles']['npc']
    return dict(i=port['i'],EE=ref['n'],fase=port['fase'],codigo=hex(port['codigo']),
        cuadro=port['cuadro'],facetas=facetas,
        actor_error=max(abs(a-b) for a,b in zip(port['actor'] if port['fase']>=2 else (0,0,0),n['pos'][:3])),
        actor_maquina=port['actor'],
        u_error=abs(port['u']-ref['bubbles_animacion']['cb4']))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('dibujo',type=Path)
    p.add_argument('--original',type=Path,required=True)
    p.add_argument('--salida',type=Path,required=True)
    p.add_argument('--primera-entrada',action='store_true',help='En fase2 compara solo la primera entrada D2; descarta historia posterior')
    a=p.parse_args()
    indice=original(a.original)
    registros=[];pares=[];ambiguos=0;salio_d2=False;historia_descartada=0
    for r in leer(a.dibujo):
        candidatos=indice.get((r['fase'],clave(r['codigo'],r['cuadro'])),[])
        if a.primera_entrada and r['fase']==2:
            if r['codigo']!=0xD2:
                salio_d2=True
            if salio_d2:
                historia_descartada+=bool(candidatos)
                candidatos=[]
        if len(candidatos)==1:
            pares.append(comparar(r,candidatos[0]))
        elif len(candidatos)>1:
            ambiguos+=1
        registros.append({k:v for k,v in r.items() if k not in ('cuerpo','cara')})
    if not registros:
        raise ValueError('No se registro el dibujo Bubbles')
    resumen=dict(dibujo=str(a.dibujo),original=str(a.original),registros=len(registros),
        fases=dict(Counter(r['fase'] for r in registros)),codigos=dict(Counter(hex(r['codigo']) for r in registros)),
        pares=len(pares),ambiguos=ambiguos,comparaciones=pares,dibujos=registros,
        primera_entrada=a.primera_entrada,historia_descartada=historia_descartada,
        paridad_visual=False,normal_activado=False,
        limite='Pareja exacta codigo/cuadro, no prueba igualdad de historia, camara, IC ni vertices originales')
    with a.salida.open('x',encoding='utf-8') as f:
        json.dump(resumen,f,indent=1,ensure_ascii=False)
    print(json.dumps({k:v for k,v in resumen.items() if k not in ('dibujos','comparaciones')},ensure_ascii=False))
    for fase in sorted(set(x['fase'] for x in pares)):
        xs=[x for x in pares if x['fase']==fase]
        print('FASE',fase,'pares',len(xs),'max_matriz',max(x['facetas']['matriz']['maximo'] for x in xs),
            'max_actor',max(x['actor_error'] for x in xs),'max_u',max(x['u_error'] for x in xs))
    return 0


if __name__=='__main__':
    raise SystemExit(main())
