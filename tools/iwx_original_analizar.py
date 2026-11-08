"""Resume series EE consecutivas y contrasta AMC/port sin ocultar cortes."""
import argparse
from collections import Counter
import csv
import hashlib
import json
import math
from pathlib import Path


def muestra(claves, t):
    i=0
    while i+1<len(claves) and claves[i+1][0]<=t:
        i+=1
    a,b=claves[i],claves[min(i+1,len(claves)-1)]
    w=max(0,min(1,(t-a[0])/(b[0]-a[0]))) if b[0]!=a[0] else 0
    return [x+(y-x)*w for x,y in zip(a[1:],b[1:])]


def analizar(a):
    filas=[];fuentes=[]
    for carpeta in [a.original,*getattr(a,'continuacion',[])]:
        raw=(carpeta/'cuadros.jsonl').read_bytes()
        parte=[json.loads(l) for l in raw.decode('utf-8').splitlines()]
        if not parte:
            raise ValueError('Una parte de la serie esta vacia')
        fuentes.append(dict(ruta=str(carpeta),sha256=hashlib.sha256(raw).hexdigest(),
            filas=len(parte),primero=parte[0]['n'],ultimo=parte[-1]['n']))
        filas.extend(parte)
    continuidad=all(((b['n']-c['n'])&0xffffffff)==1 for c,b in zip(filas,filas[1:]))
    if not filas or not continuidad or not all(r['estable'] for r in filas):
        raise ValueError('Serie no consecutiva/estable: no acreditar comparacion por campo')
    result=dict(original=str(a.original),filas=len(filas),consecutivos=continuidad,
                primero=filas[0]['n'],ultimo=filas[-1]['n'],fuentes=fuentes,paridad_global=False)
    result['cambios_camara']=[dict(n=r['n'],**{k:r['camara'].get(k) for k in ('clip','f','animada')})
        for i,r in enumerate(filas) if i==0 or
        (r['camara'].get('clip'),r['camara'].get('animada'))!=
        (filas[i-1]['camara'].get('clip'),filas[i-1]['camara'].get('animada'))]
    if 'bubbles' in filas[0]:
        bb=[r['bubbles'] for r in filas]
        if not all(r['estable'] and r['n']==f['n'] for r,f in zip(bb,filas)):
            raise ValueError('El lector Bubbles cruzo campos')
        result['bubbles']=dict(estados=dict(Counter(str(r['npc']['estado']) for r in bb)),
            codigos=dict(Counter(hex(r['npc']['codigo']) for r in bb)),
            campos_mando_solicitado=sum(r.get('input_I',False) for r in filas),
            desplazamiento_jugador_max=max(math.dist(bb[0]['jugador']['pos'],r['jugador']['pos']) for r in bb),
            desplazamiento_bubbles_max=max(math.dist(bb[0]['npc']['pos'],r['npc']['pos']) for r in bb),
            matrices47_completas=sum(len(r['npc']['mat47_128'])==47 for r in bb),
            limite='Mando solicitado no demuestra mando efectivo; no alinear RNG/IC distintos por ordinal')
    if a.amc:
        fuente=json.loads(a.amc.read_text(encoding='utf-8'))
        campo,=[c for c in fuente['campos'] if c['campo']==a.campo]
        clips=campo['original']
        grupos=[]
        for r in filas:
            c=r['camara']
            if not c.get('animada') or c.get('clip') is None:
                continue
            # El clamp final PAL puede retroceder ~3e-5 sin reiniciar el clip.
            if (not grupos or grupos[-1]['clip']!=c['clip'] or
                    c['f']<grupos[-1]['filas'][-1]['camara']['f']-.001):
                grupos.append(dict(clip=c['clip'],filas=[]))
            grupos[-1]['filas'].append(r)
        result['camaras']=[]
        for g in grupos:
            rs=g['filas'];idx=g['clip']
            dato=dict(clip=idx,campos=len(rs),primero=rs[0]['n'],ultimo=rs[-1]['n'],
                      f_primero=rs[0]['camara']['f'],f_ultimo=rs[-1]['camara']['f'])
            if not 0<=idx<len(clips):
                dato['sin_clip_original']=True
                result['camaras'].append(dato)
                continue
            clip=clips[idx]
            variantes=[]
            for retraso in (0,1.2):
                errores=[];constantes=[];inicio=None;anterior=None
                for r in rs:
                    c=r['camara'];t=max(0,min(clip['cuadros']-1,c['f']-retraso))
                    pose=[v for pista in clip['pistas'][:2] for v in muestra(pista,t)]
                    actual=c['ojo']+c['objetivo']
                    if len(pose)!=6 or len(actual)!=6:
                        raise ValueError('Faltan componentes ojo/objetivo originales')
                    errores.append(dict(n=r['n'],f=c['f'],t=t,error=max(abs(x-y) for x,y in zip(pose,actual))))
                    sig=tuple(actual)
                    if sig!=anterior:
                        if inicio and inicio['campos']>=3:
                            constantes.append(inicio)
                        inicio=dict(n=r['n'],f=c['f'],campos=0)
                    inicio['campos']+=1;anterior=sig
                if inicio and inicio['campos']>=3:
                    constantes.append(inicio)
                variantes.append(dict(retraso=retraso,max_abs=max(e['error'] for e in errores),
                    errores_mayor_0_001=[e for e in errores if e['error']>.001],
                    constantes_observadas=constantes))
            dato['hipotesis_fase']=variantes
            result['camaras'].append(dato)
    if a.port:
        from iwx_intro_analizar import analizar as analizar_port
        cobertura=analizar_port(a.port)  # Rechaza huecos interiores; declara fila final incompleta.
        with (a.port/'combate_sonda.csv').open(encoding='utf-8') as f:
            port=[r for r in csv.DictReader(f) if r.get('combate')=='1' and r.get('cam_modo')=='7'
                  and None not in r and all(v is not None for v in r.values())]
        # Recurso compartido y tiempo comun, nunca contador guest==contador PAL.
        campos=('cam_ojo_x','cam_ojo_y','cam_ojo_z','cam_obj_x','cam_obj_y','cam_obj_z')
        result['port']=dict(ruta=str(a.port),filas_intro=len(port),columnas=campos,
            filas_incompletas=cobertura['filas_incompletas'],comparaciones=[])
        grupos_port=[]
        for r in port:
            if (not grupos_port or float(r['cam_reloj'])<float(grupos_port[-1][-1]['cam_reloj'])
                    or r['cam_acc']!=grupos_port[-1][-1]['cam_acc']
                    or r['cam_limite']!=grupos_port[-1][-1]['cam_limite']):
                grupos_port.append([])
            grupos_port[-1].append(r)
        if not a.amc:
            raise ValueError('--port requiere --amc y --campo para anclar las pistas')
        for g in grupos:
            idx=g['clip']
            if not 0<=idx<len(grupos_port):
                continue
            ps=grupos_port[idx]
            # Fases observadas respecto de AMC: PS2 lee tras avanzar1.2; sonda B3
            # lee antes del paso. Los cortes quedan incluidos y listados aparte.
            puntos=[[max(0,float(r['cam_reloj'])-1),*[float(r[k]) for k in campos]]
                    for r in ps if float(r['cam_reloj'])>=1]
            if len(puntos)<2:
                raise ValueError('Port sin suficientes muestras de la pista')
            pares=[];fuera=[]
            for r in g['filas']:
                c=r['camara'];t=max(0,c['f']-1.2)
                if not puntos[0][0]<=t<=puntos[-1][0]:
                    fuera.append(dict(n=r['n'],f=c['f'],t=t))
                    continue
                pc=muestra(puntos,t);oc=c['ojo']+c['objetivo']
                pares.append(dict(n=r['n'],f=c['f'],t=t,
                    error=max(abs(x-y) for x,y in zip(pc,oc))))
            result['port']['comparaciones'].append(dict(clip=idx,pares=len(pares),
                fase_original=1.2,fase_port=1,metodo='Interpolacion de muestras port al tiempo logico PS2',
                rango_port=[puntos[0][0],puntos[-1][0]],
                max_abs=max((r['error'] for r in pares),default=None),
                fuera_tolerancia=[r for r in pares if r['error']>.001],
                sin_cobertura_port=fuera,
                cortes_port=[dict(cuadro=r['cuadro'],reloj=r['cam_reloj']) for r in ps if float(r['cam_reloj'])==0],
                limite='Pareja por indice secuencial de pista; no acredita duracion/guion/fondo/sonido completos'))
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--original',type=Path,required=True)
    p.add_argument('--continuacion',type=Path,nargs='+',default=[],
        help='Series posteriores en orden; se exige continuidad exacta tambien entre archivos')
    p.add_argument('--amc',type=Path)
    p.add_argument('--campo',type=int)
    p.add_argument('--port',type=Path)
    p.add_argument('--salida',type=Path,required=True)
    a=p.parse_args();r=analizar(a)
    with a.salida.open('x',encoding='utf-8') as f:
        json.dump(r,f,indent=1,ensure_ascii=False)
    breve={k:v for k,v in r.items() if k!='camaras'}
    if 'port' in r:
        breve['port']={k:v for k,v in r['port'].items() if k!='comparaciones'}
        breve['port']['comparaciones']=[{**{k:v for k,v in g.items()
            if k not in ('fuera_tolerancia','sin_cobertura_port')},
            'fuera_tolerancia':len(g['fuera_tolerancia']),
            'sin_cobertura_port':len(g['sin_cobertura_port'])}
            for g in r['port']['comparaciones']]
    if 'camaras' in r:
        breve['camaras']=[{**{k:v for k,v in g.items() if k!='hipotesis_fase'},
            'fases':[dict(retraso=h['retraso'],max_abs=h['max_abs'],fuera=len(h['errores_mayor_0_001']),
                           constantes=h['constantes_observadas']) for h in g.get('hipotesis_fase',[])]}
                         for g in r['camaras']]
    print(json.dumps(breve,ensure_ascii=False,indent=1))


if __name__=='__main__':
    main()
