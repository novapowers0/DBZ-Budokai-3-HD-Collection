"""Goku/Cell: marcha lenta con palanca -64, codigo/tiempo y salida al soltar.

La comparacion de duracion/cadencia no certifica poses, colision ni camara.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import struct
import time

import pruebas as P
from iwx_bubbles_focal import pantalla2,FUENTE,HOME


def analizar_original(path):
    rows=[json.loads(s) for s in path.read_text(encoding='utf-8').splitlines()]
    if not (path.parent/'resumen.json').is_file():
        raise ValueError('La captura original no esta completa')
    temporal='campo_video' if all('campo_video' in r for r in rows) else 'n'
    for i,r in enumerate(rows):
        if not r['estable'] or (i and r[temporal]!=rows[i-1][temporal]+1):
            raise ValueError('Original inestable o sin continuidad en su eje temporal')
    auditoria=None
    if temporal=='campo_video':
        from iwx_reloj_original import auditar_serie
        validacion=auditar_serie(path)
        auditoria={k:validacion[k] for k in ('estados_autenticados','pares_vsync_verificados','anomalias_libpad')}
    codigo=0xfe if int(path.parent.name,16)==0x7d1 else 0x15
    walk=[r for r in rows if r.get('entrada',{}).get('activa')]
    if len(walk)<60 or any(r['actores'][0]['codigo']!=codigo or r['actores'][0]['estado']!=2 for r in walk):
        raise ValueError(f'No se provoco marcha lenta2/{codigo:X} en el original')
    if any(r['palanca']['raw']!=[-1,-64] for r in walk):
        raise ValueError('La palanca original no es -64')
    return dict(fuente=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        campos=len(rows),campos_marcha=len(walk),codigo=codigo,duracion=walk[0]['actores'][0]['dur'],
        contador_temporal=temporal,auditoria_video=auditoria,
        palanca=walk[0]['palanca'],cadencia=statistics.median(
            b['actores'][0]['f']-a['actores'][0]['f'] for a,b in zip(walk,walk[1:])
            if b['actores'][0]['f']>a['actores'][0]['f']),
        fin=rows[-1]['actores'][0]['codigo'],paridad_global=False)


def evaluar_port(rows,original,clave):
    cell=clave==0x7dd
    # anim_iw.cpp: {0x00,915,0x8F1}; LocoDe().ready, no kQuieto (otro clip).
    andar,quieto=(0xc01,0xc00) if cell else (0x973,0x970) if clave==0x7d1 else (0x815,0x8f1)
    carrera=(0xc02,0x992,0xc0a) if cell else (0x974,0x975,0x976) if clave==0x7d1 else (0x816,0x817,0x811)
    walk=[r for r in rows if r['codigo']==andar]
    pasos=[b['f']-a['f'] for a,b in zip(walk,walk[1:]) if b['n']==a['n']+1 and b['f']>a['f']]
    if len(walk)<60 or len(pasos)<30:
        raise P.Fallo('Muestras insuficientes de la marcha lenta real')
    cadencia=statistics.median(pasos)
    medida=dict(muestras=len(rows),marcha=len(walk),pares_consecutivos=len(pasos),
        duraciones=sorted({r['duracion'] for r in walk}),cadencia=cadencia,
        fin=rows[-1],huecos=sum(b['n']!=a['n']+1 for a,b in zip(rows,rows[1:])))
    if any(r['duracion']!=original['duracion'] for r in walk) or abs(cadencia-1)>.0001 or abs(original['cadencia']-1.2)>.0001:
        raise P.Fallo('Duracion/cadencia distintas a IW')
    if rows[-1]['codigo']!=quieto or any(r['codigo'] in carrera for r in rows):
        raise P.Fallo('Entra carrera/freno al andar lento, o no vuelve al reposo')
    return medida


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--original',type=Path,required=True)
    ap.add_argument('--clave',type=lambda x:int(x,0),choices=(0x7d0,0x7d1,0x7dd),default=0x7dd)
    ap.add_argument('--revisar',type=Path,help='Reevaluar RAM retenida sin abrir ni repetir el juego')
    a=ap.parse_args()
    if int(json.loads((a.original.parent/'resumen.json').read_text(encoding='utf-8'))['clave'],0)!=a.clave:
        ap.error('La mision original no coincide con --clave')
    cell=a.clave==0x7dd
    nombre='cell' if cell else 'goku_bubbles' if a.clave==0x7d1 else 'goku'
    original=analizar_original(a.original)
    if a.revisar:
        port=a.revisar/'port.json'
        previa=json.loads((a.revisar/'medida.json').read_text(encoding='utf-8'))
        if previa['original']['sha256']!=original['sha256']:
            ap.error('La captura original no coincide con la declarada en la focal')
        revision=dict(ok=True,original=original,port=evaluar_port(json.loads(port.read_text(encoding='utf-8')),original,a.clave),
            sha256_port=hashlib.sha256(port.read_bytes()).hexdigest(),paridad_global=False,
            motivo='Alias READY de Goku: original0 -> 8F1/915, no kQuieto800; RAM conservada, sin nueva partida')
        destino=a.revisar/f'revision_{time.time_ns()}.json'
        with destino.open('x',encoding='utf-8') as f:json.dump(revision,f,indent=1,allow_nan=False)
        print(destino,flush=True)
        print(json.dumps(revision,indent=1),flush=True)
        return 0
    os.environ['DBZ3_AGENTE']='solIWX'
    out=HOME/f'{nombre}_marcha_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA',out,flush=True)
    pantalla2()
    rows=[]
    medida=dict(original=original,paridad_global=False)
    def env(_):
        variables=dict(DBZ3_TEST_IWMINI=hex(a.clave),DBZ3_PS2_GAMES=FUENTE,DBZ3_TEST_IW_PALANCA='64')
        if a.clave==0x7d1:
            variables['DBZ3_TEST_GP2_BUBBLES']='1'
        return variables
    def jugar(j,_):
        j.log(r'entrada acabada: mando al jugador',190)
        if a.clave==0x7d1:
            j.log(r'gp2 bubbles READY terminado: mando y reloj liberados',15)
        else:
            P.dormir(3)
        # Es el limitador de entrada ya existente; no cambia estados ni poses.
        j.tecla('W 1600',0)
        limite=time.monotonic()+2.5
        while time.monotonic()<limite:
            mgr,n=P._freecam_mem(j)
            if mgr and n is not None:
                x=mgr+2096
                raw=P.leer(j.h,P.GBASE+x+0x650,0x620)
                _,n2=P._freecam_mem(j)
                if raw and len(raw)==0x620 and n==n2 and (not rows or rows[-1]['n']!=n):
                    u=lambda o:struct.unpack_from('>I',raw,o)[0]
                    f=struct.unpack_from('>f',raw,0x608)[0]
                    if not math.isfinite(f):raise P.Fallo('Reloj de pose no finito')
                    rows.append(dict(n=n,codigo=u(0x520)&0xfff,clip=u(0x604),f=f,duracion=u(0x60c)))
            time.sleep(.003)
        medida['port']=evaluar_port(rows,original,a.clave)
    key=f'solIWX_{nombre}_marcha'
    P.ESCENARIOS[key]=(f'{nombre}: marcha lenta original y reposo al soltar',env,jugar)
    r=P.correr('smoke_b',key,[],None,{'timeout':'350'},True)
    for name,obj in [('port.json',rows),('medida.json',medida),('resultado.json',r)]:
        with (out/name).open('x',encoding='utf-8') as f:json.dump(obj,f,default=str,indent=1,allow_nan=False)
    print(P.tabla([r]),flush=True)
    print(json.dumps(medida,indent=1),flush=True)
    return 0 if r['ok'] else 1


if __name__=='__main__':
    raise SystemExit(main())
