"""Contrasta la vista publicada en los cortes, sin confundirla con el reloj nuevo.

Lee evidencia ya cerrada y autenticada por SHA256. No modifica el juego ni las
tolerancias: conserva todas las filas, incluidos hold y ultimo paso con clamp.
La inferencia de tiempo evaluado queda separada del contador observado.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

from iwx_intro_analizar import analizar as cobertura_port
from iwx_original_analizar import muestra

POSE=('cam_ojo_x','cam_ojo_y','cam_ojo_z','cam_obj_x','cam_obj_y','cam_obj_z')


def grupos(rows, clave, reloj):
    salida=[]
    for r in rows:
        if not salida or clave(r)!=clave(salida[-1][-1]) or reloj(r)<reloj(salida[-1][-1])-.001:
            salida.append([])
        salida[-1].append(r)
    return salida


def contrastar(original, port, clips):
    if not original or not original[0]['estable']:
        raise ValueError('Original vacio/no estable')
    if not all(b['n']==a['n']+1 and b['estable'] for a,b in zip(original,original[1:])):
        raise ValueError('Original no consecutivo/estable')
    if not port or any(not all(math.isfinite(float(r[k])) for k in (*POSE,'cam_reloj','cam_limite')) for r in port):
        raise ValueError('Port vacio/no finito')
    todos_og=grupos(original,lambda r:r['camara']['clip'],lambda r:r['camara']['f'])
    og=todos_og[:3]
    pg=grupos(port,lambda r:(r['cam_acc'],r['cam_limite']),lambda r:float(r['cam_reloj']))
    if [g[0]['camara']['clip'] for g in og]!=[0,1,2] or len(pg)!=3:
        raise ValueError('No son los tres planos de Serpiente esperados')
    if [float(g[0]['cam_limite']) for g in pg]!=[199,299,359]:
        raise ValueError('Duraciones ajenas al candidato medido')
    puntos=[]
    for i,g in enumerate(pg):
        if not all(int(b['cuadro'])==int(a['cuadro'])+1 for a,b in zip(g,g[1:])):
            raise ValueError('Huecos interiores en port')
        ps=[[float(r['cam_reloj'])-1,*[float(r[k]) for k in POSE]]
            for r in g if float(r['cam_reloj'])>=1]
        if i+1<len(pg):
            siguiente=pg[i+1][0]
            if float(siguiente['cam_reloj'])!=0 or int(siguiente['cuadro'])!=int(g[-1]['cuadro'])+1:
                raise ValueError('Falta la vista publicada durante el cambio de pista')
            # El reloj se acaba de reiniciar; la vista sigue siendo la evaluacion
            # del ultimo reloj de la pista anterior (sonda anterior al paso).
            ps.append([float(g[-1]['cam_reloj']),*[float(siguiente[k]) for k in POSE]])
        if len(ps)<2 or any(b[0]<=a[0] for a,b in zip(ps,ps[1:])):
            raise ValueError('Tiempo port ambiguo/no creciente')
        puntos.append(ps)
    publicaciones=[]
    for i,ps in enumerate(puntos):
        for punto in ps:
            esperado=[v for track in clips[i]['pistas'][:2] for v in muestra(track,punto[0])]
            publicaciones.append(dict(clip=i,tiempo_evaluado_inferido=punto[0],
                error=max(abs(a-b) for a,b in zip(punto[1:],esperado))))
    filas=[];anterior=None;tiempo_anterior=None;clip_anterior=None
    for i,g in enumerate(og):
        for j,r in enumerate(g):
            c=r['camara'];tipo='paso';efectivo=i
            if j==0 and anterior is not None:
                efectivo=clip_anterior;t=tiempo_anterior;tipo='vista_anterior_durante_cambio'
            elif anterior is not None and anterior['camara']['clip']==c['clip']:
                t=anterior['camara']['f']
                if c['f']<anterior['camara']['f']:
                    tipo='evaluacion_anterior_al_clamp'
            else:
                t=max(0,c['f']-1.2)
            ps=puntos[efectivo];pose=c['ojo']+c['objetivo']
            if len(pose)!=6 or not all(math.isfinite(v) for v in pose):
                raise ValueError('Vista original incompleta/no finita')
            metodo='tiempo_evaluado_inferido'
            if ps[0][0]<=t<=ps[-1][0]:
                evaluada=muestra(ps,t)
            elif t>ps[-1][0] and i==2:
                # El siguiente estado ya publica otra camara: NO usar su vista
                # como si aun perteneciera a este plano. Solo contrastar la pose
                # terminal si todas las claves restantes son constantes y hay
                # al menos tres publicaciones reales iguales en el port.
                for track in clips[i]['pistas'][:2]:
                    final=track[-1][1:]
                    if muestra(track,ps[-1][0])!=final or any(
                            k[1:]!=final for k in track if k[0]>=ps[-1][0]):
                        raise ValueError('La cola del AMC no es constante: falta cobertura temporal')
                if not ps[-1][1:]==ps[-2][1:]==ps[-3][1:]:
                    raise ValueError('La pose terminal no esta observada repetida en port')
                evaluada=ps[-1][1:];metodo='pose_terminal_constante_sin_pareja_temporal'
            else:
                raise ValueError(f'Sin cobertura port en EE{r["n"]}: no descartar la fila')
            amc=[v for p in clips[efectivo]['pistas'][:2] for v in muestra(p,t)]
            fila=dict(n=r['n'],clip_observado=i,cuadro_observado=c['f'],
                clip_evaluado_inferido=efectivo,tiempo_evaluado_inferido=t,tipo=tipo,metodo=metodo,
                error_port=max(abs(a-b) for a,b in zip(pose,evaluada)),
                error_amc=max(abs(a-b) for a,b in zip(pose,amc)))
            filas.append(fila)
            # Al cambiar pista, la vista conservada no es la primera evaluacion
            # de la pista nueva; para el siguiente campo se usa su reloj actual.
            anterior=r;tiempo_anterior=t;clip_anterior=efectivo
    return dict(filas=filas,pares=len(filas),filas_descartadas=0,
        filas_original_recibidas=len(original),filas_original_analizadas=len(filas),
        filas_original_fuera_del_alcance=len(original)-len(filas),
        clips_original_fuera_del_alcance=[g[0]['camara']['clip'] for g in todos_og[3:]],
        publicaciones_port=len(publicaciones),max_error_publicaciones=max(r['error'] for r in publicaciones),
        publicaciones_port_fuera_tolerancia=[r for r in publicaciones if r['error']>.001],
        pares_temporales=sum(r['metodo']=='tiempo_evaluado_inferido' for r in filas),
        poses_terminales_sin_pareja_temporal=sum(r['metodo']!='tiempo_evaluado_inferido' for r in filas),
        max_error_port=max(r['error_port'] for r in filas),
        max_error_amc=max(r['error_amc'] for r in filas),
        fuera_tolerancia=[r for r in filas if max(r['error_port'],r['error_amc'])>.001],
        cortes=[r for r in filas if r['tipo']!='paso'],tolerancia=.001,
        paridad_global=False,normal_activado=False,
        limite='Tiempo evaluado inferido del orden de publicacion, no medido en un campo independiente. '
                'Solo las tres primeras pistas originales0/1/2; filas posteriores declaradas fuera del alcance. '
                'Parejas de ojo/objetivo; no certifica FOV, fondo, sonido, guion completo ni transicion de juego.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--comparacion',type=Path,required=True)
    p.add_argument('--amc',type=Path,required=True)
    p.add_argument('--salida',type=Path,required=True)
    a=p.parse_args();meta=json.loads(a.comparacion.read_text(encoding='utf-8'))
    original=[]
    for fuente in meta['fuentes']:
        raw=(Path(fuente['ruta'])/'cuadros.jsonl').read_bytes()
        if hashlib.sha256(raw).hexdigest()!=fuente['sha256']:
            raise ValueError('Original distinto al registrado')
        original.extend(json.loads(l) for l in raw.decode('utf-8').splitlines())
    run=Path(meta['port']['ruta']);cobertura=cobertura_port(run)
    with (run/'combate_sonda.csv').open(encoding='utf-8',newline='') as f:
        completas=[r for r in csv.DictReader(f) if None not in r and all(v is not None for v in r.values())]
    port=[r for r in completas if r.get('combate')=='1' and r.get('cam_modo')=='7']
    fuente=json.loads(a.amc.read_text(encoding='utf-8'))
    campo,=[c for c in fuente['campos'] if c['campo']==884]
    resultado=contrastar(original,port,campo['original'])
    resultado.update(comparacion=str(a.comparacion),port=str(run),amc=str(a.amc),
        amc_sha256=hashlib.sha256(a.amc.read_bytes()).hexdigest(),
        filas_port_incompletas=cobertura['filas_incompletas'],
        csv_port_sha256=hashlib.sha256((run/'combate_sonda.csv').read_bytes()).hexdigest())
    with a.salida.open('x',encoding='utf-8') as f:
        json.dump(resultado,f,indent=1,ensure_ascii=False)
    print(json.dumps({k:v for k,v in resultado.items() if k!='filas'},indent=1,ensure_ascii=False))
    return 1 if resultado['fuera_tolerancia'] or resultado['publicaciones_port_fuera_tolerancia'] else 0


if __name__=='__main__':
    raise SystemExit(main())
