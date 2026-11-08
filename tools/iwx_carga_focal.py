"""Carga de Goku/Cell: cuadrado con energia llena y, despues, gastada jugando."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import threading
import time
import pruebas as P
from iwx_bubbles_focal import pantalla2,FUENTE,HOME


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--antes',action='store_true',help='Registrar el defecto del ejecutable anterior')
    ap.add_argument('--clave',type=lambda x:int(x,0),choices=(0x7d0,0x7d1,0x7dd),default=0x7dd)
    ap.add_argument('--original',type=Path,help='RAM original; admite prefijo consecutivo, sin certificar su final')
    a=ap.parse_args()
    nombre={0x7dd:'cell',0x7d0:'goku',0x7d1:'goku_bubbles'}[a.clave]
    esperado={'8D3','8D4','8D5'} if a.clave==0x7dd else {'8D0','8D1','8D2'}
    original=None
    if a.original:
        datos=[json.loads(s) for s in a.original.read_text(encoding='utf-8').splitlines()]
        instancia=json.loads((a.original.parent.parent/'instancia.json').read_text(encoding='utf-8'))
        estado=instancia.get('estado_info')
        if int(a.original.parent.name,16)!=a.clave or (estado and int(estado['clave'],0)!=a.clave):
            ap.error('La mision declarada no coincide con la original')
        temporal='campo_video' if all('campo_video' in r for r in datos) else 'n'
        if any(not r['estable'] or (i and r[temporal]!=datos[i-1][temporal]+1) for i,r in enumerate(datos)):
            ap.error('Original inestable, de otra mision o sin continuidad')
        auditoria=None
        if temporal=='campo_video':
            from iwx_reloj_original import auditar_serie
            validacion=auditar_serie(a.original)
            auditoria={k:validacion[k] for k in ('estados_autenticados','pares_vsync_verificados','anomalias_libpad')}
        duraciones={c:sorted({r['actores'][0]['dur'] for r in datos if r['actores'][0]['codigo']==c}) for c in (2,3,4)}
        if any(len(ds)!=1 for ds in duraciones.values()):
            ap.error('Falta alguna duracion original de carga2/3/4')
        original=dict(fuente=str(a.original),sha256=hashlib.sha256(a.original.read_bytes()).hexdigest(),
            captura_completa=(a.original.parent/'resumen.json').is_file(),campos=len(datos),duraciones=duraciones,
            contador_temporal=temporal,auditoria_video=auditoria)
    os.environ['DBZ3_AGENTE']='solIWX'
    out=HOME/f'{nombre}_carga_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA',out,flush=True)
    pantalla2()
    medida=dict(antes=a.antes,original=original,paridad_global=False)
    rows=[]
    def env(_):
        variables=dict(DBZ3_TEST_IWMINI=hex(a.clave),DBZ3_PS2_GAMES=FUENTE)
        if a.clave==0x7d1:
            variables['DBZ3_TEST_GP2_BUBBLES']='1'
        return variables
    def jugar(j,_):
        j.log(r'entrada acabada: mando al jugador',190)
        if a.clave==0x7d1:
            j.log(r'gp2 bubbles READY terminado: mando y reloj liberados',15)
        else:
            P.dormir(3)
        inicio=len(j.texto_log())
        j.tecla('L 1200',1400)
        P.dormir(.3)
        texto=j.texto_log()[inicio:]
        if a.clave==0x7d1:
            resbalones=len(re.findall(r'iwmini: platano: resbala',j.texto_log()))
            medida['resbalones_inicio']=resbalones
            if resbalones:
                raise P.Fallo('Goku resbala al empezar sin moverse; no es un ensayo con energia llena')
        codigos=re.findall(r'iwmini: anim (carga(?:-bucle|-fin)?) codigo ([0-9A-F]+)',texto)
        medida['energia_llena']=dict(entrada='L 1200',pedidos=codigos)
        if a.antes:
            if not codigos:
                raise P.Fallo('no se reprodujo la carga incorrecta con energia llena')
            return
        if codigos:
            raise P.Fallo('el original no carga con energia llena, pero el port si')
        inicio=len(j.texto_log())
        parar=threading.Event()
        errores=[]
        def muestrear():
            try:
                while not parar.is_set():
                    mgr,n=P._freecam_mem(j)
                    if mgr and n is not None:
                        raw=P.leer(j.h,P.GBASE+mgr+2096+0x650,0x620)
                        _,n2=P._freecam_mem(j)
                        if raw and len(raw)==0x620 and n==n2 and (not rows or rows[-1]['n']!=n):
                            u=lambda o:struct.unpack_from('>I',raw,o)[0]
                            f=struct.unpack_from('>f',raw,0x608)[0]
                            if not math.isfinite(f):raise ValueError('Cuadro de pose no finito')
                            rows.append(dict(n=n,codigo=u(0x520)&0xfff,clip=u(0x604),f=f,duracion=u(0x60c)))
                    parar.wait(.003)
            except Exception as exc:
                errores.append(repr(exc))
        hilo=threading.Thread(target=muestrear)
        hilo.start()
        try:
            j.tecla('Backspace 100',300)
            j.log(r'iwmini: scatto \(energia ',10)
            P.dormir(2)
            j.tecla('L 1800',2000)
        finally:
            parar.set()
            hilo.join()
        if errores:raise P.Fallo('Fallo del lector: '+str(errores))
        texto=j.texto_log()[inicio:]
        scatto=re.findall(r'iwmini: scatto \(energia ([0-9.]+)',texto)
        codigos=re.findall(r'iwmini: anim (carga(?:-bucle|-fin)?) codigo ([0-9A-F]+)',texto)
        medida['energia_gastada']=dict(scatto=scatto,entrada='L 1800',pedidos=codigos)
        if not scatto or not esperado<={codigo for _,codigo in codigos}:
            raise P.Fallo('sin carga 2/3/4 despues de gastar energia')
        if original:
            base=0x8d3 if a.clave==0x7dd else 0x8d0
            lectura={c:sorted({r['duracion'] for r in rows if r['codigo']==base+c-2}) for c in (2,3,4)}
            medida['duraciones_ram']=lectura
            medida['muestras_ram']=len(rows)
            if lectura!=original['duraciones']:
                raise P.Fallo('La RAM del port no reproduce las duraciones originales2/3/4')
    key=f'solIWX_{nombre}_carga_'+('antes' if a.antes else 'despues')
    P.ESCENARIOS[key]=(f'{nombre}: cuadrado con energia llena/gastada',env,jugar)
    r=P.correr('smoke_b',key,[],None,{'timeout':'350'},True)
    for name,obj in [('medida.json',medida),('port.json',rows),('resultado.json',r)]:
        with (out/name).open('x',encoding='utf-8') as f:
            json.dump(obj,f,default=str,indent=1,ensure_ascii=False,allow_nan=False)
    print(P.tabla([r]),flush=True)
    print(json.dumps(medida,indent=1),flush=True)
    return 0 if r['ok'] else 1


if __name__=='__main__':
    raise SystemExit(main())
