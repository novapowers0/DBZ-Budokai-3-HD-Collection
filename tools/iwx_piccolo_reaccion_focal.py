"""Piccolo7D8 opt-in: animacion corporal original, reinicio y retorno en el motor."""
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
from iwx_bubbles_focal import FUENTE, HOME, pantalla2
from iwx_piccolo import esqueleto


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--original',type=Path,required=True,
        help='Inventario original con original.esqueleto, producido por iwx_piccolo.py')
    ap.add_argument('--rol',type=int,choices=range(22),default=1,
        help='Objetivo del piloto, sin forzar el hueso finalmente alcanzado')
    ap.add_argument('--acuse',action='store_true',
        help='Candidato nuevo: exigir identidad del reinicio aplicado, incluido reimpacto igual')
    a=ap.parse_args()
    original=json.loads(a.original.read_text(encoding='utf-8'))
    roles_original=original['original']['esqueleto']['roles']
    os.environ['DBZ3_AGENTE']='solIWX'
    os.environ['PYTHONUTF8']='1'
    out=HOME/f'piccolo_reaccion_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA',out,flush=True)
    pantalla2()
    medida=dict(normal=False,paridad_integral=False,paridad_scheduler=False,
        alcance='Entrada, avance y retorno del clip corporal; no igualdad visual ni temporal PS2/B3',
        original=str(a.original),original_sha256=hashlib.sha256(a.original.read_bytes()).hexdigest(),
        rol_objetivo=a.rol,acuse_reinicio=a.acuse)
    muestras,errores=[],[]

    def env(_):
        e=dict(DBZ3_TEST_IWMINI='0x7D8,auto',
            DBZ3_TEST_IWX_PICCOLO_REACCION='1',DBZ3_TEST_IWX_PICCOLO_PUNTO=str(a.rol),
            DBZ3_PS2_GAMES=FUENTE)
        if a.acuse:
            e['DBZ3_TEST_IWX_PICCOLO_ACUSE']='1'
        return e

    def jugar(j,_):
        j.log(r'reaccion Piccolo candidata; normal=false',150)
        j.log(r'iwdisparo: juego .* empieza',180)
        stop=threading.Event()

        def leer(addr,n):
            b=P.leer(j.h,P.GBASE+addr,n)
            if b is None or len(b)!=n:
                raise P.Fallo(f'Cuerpo rival incompleto {addr:X}/{n}')
            return b

        def sample():
            try:
                while not stop.is_set():
                    mgr,n=P._freecam_mem(j)
                    if mgr and n is not None and (not muestras or muestras[-1]['n']!=n):
                        b=leer(mgr+2096+8240+0x650,0x614)
                        codigo=struct.unpack_from('>I',b,0x520)[0]&0xFFF
                        clip=struct.unpack_from('>I',b,0x604)[0]
                        cuadro=struct.unpack_from('>f',b,0x608)[0]
                        duracion=struct.unpack_from('>I',b,0x60C)[0]
                        posicion=struct.unpack('>3f',leer(mgr+2096+8240+48,12))
                        _,despues=P._freecam_mem(j)
                        if n==despues:
                            if not all(math.isfinite(v) for v in (cuadro,*posicion)):
                                raise P.Fallo('Cuadro corporal no finito')
                            muestras.append(dict(n=n,codigo=codigo,clip=clip,
                                cuadro=cuadro,duracion=duracion,posicion=posicion,
                                distancia_ojo=math.dist(posicion,(0,150,37))))
                    stop.wait(.002)
            except Exception as exc:
                errores.append(repr(exc))

        t=threading.Thread(target=sample)
        t.start()
        try:
            j.log(r'Piccolo reaccion entra gen ',35)
            j.captura('piccolo_reaccion')
            j.log(r'Piccolo reaccion fin gen ',12)
            P.dormir(4 if a.acuse else 2)
            j.captura('piccolo_tras_reaccion')
            mgr,_=P._freecam_mem(j)
            medida['esqueleto_b3']=esqueleto(leer,mgr+2096+8240,hd=True)
        finally:
            stop.set()
            t.join()
        if errores:
            raise P.Fallo('Lectura corporal: '+str(errores))
        nombres=lambda rs:[(r['rol'],r['nombre']) for r in rs]
        medida['roles_byteidenticos_nombre']=nombres(roles_original)==nombres(medida['esqueleto_b3']['roles'])
        if not medida['roles_byteidenticos_nombre']:
            raise P.Fallo('Los22roles B3 no se corresponden con los nombres originales')
        texto=j.texto_log()
        patron=r'Piccolo reaccion entra gen (\d+) rol (\d+) codigo ([0-9A-Fa-f]+) alias ([0-9A-Fa-f]+) previo (\d+) repeticion (\d+)'
        entradas=[dict(generacion=int(g),rol=int(r),codigo=int(c,16),alias=int(a,16),
            previo=int(p),repeticion=int(v)) for g,r,c,a,p,v in re.findall(patron,texto)]
        finales=[int(g) for g in re.findall(r'Piccolo reaccion fin gen (\d+)',texto)]
        medida.update(entradas=entradas,finales=finales,muestras=len(muestras),errores=errores)
        radios=[m['distancia_ojo'] for m in muestras]
        medida['radio_desde_ojo']=[min(radios),max(radios)] if radios else None
        if not radios or any(not 59.99<=r<=70.01 for r in radios):
            raise P.Fallo('La orbita candidata no esta a60..70unidades del ojo del visor')
        if not entradas or not finales:
            raise P.Fallo('Faltan entrada y retorno de la reaccion')
        grupos=(1,1,0,0,4,5,4,5,2,3,2,3,4,5,2,3,2,3,1,0,1,0)
        for e in entradas:
            if not 0<=e['rol']<22 or e['codigo']!=0x406+grupos[e['rol']] or e['alias']!=0xFC8+grupos[e['rol']]:
                raise P.Fallo('Rol/reaccion/alias distintos de la tabla original')
        vistos=[m for m in muestras if 0xFC8<=m['codigo']<=0xFCD]
        medida['reacciones_observadas']=len(vistos)
        medida['codigos_observados']=sorted({hex(m['codigo']) for m in vistos})
        if len(vistos)<10 or any(m['duracion']!=30 or not 0<=m['cuadro']<=29 for m in vistos):
            raise P.Fallo('No se han observado suficientes muestras validas de la reaccion corporal de30cuadros')
        if not any(m['codigo']==0xFB0 for m in muestras):
            raise P.Fallo('No se ha observado la vuelta a3E8/FB0')
        # detalle::Entrar(1) inicia otra generacion: el log de fin contiene
        # el estado resultante, no el identificador del clip3 ya terminado.
        if not set(finales).issubset({e['generacion']+1 for e in entradas}):
            raise P.Fallo('Fin de una generacion no solicitada')
        # El reloj del cuerpo y la frecuencia del controlador son observaciones
        # distintas. Conservar repeticiones/saltos, sin fabricar un paso+1.
        pasos=r'Piccolo reaccion paso gen (\d+) codigo ([0-9A-Fa-f]+) cuadro ([\d.]+) duracion (\d+) giro ([-\d.]+)/([-\d.]+)'
        traza=[dict(generacion=int(g),codigo=int(c,16),cuadro=float(f),duracion=int(d),
            giro=[float(h),float(v)]) for g,c,f,d,h,v in re.findall(pasos,texto)]
        anteriores={};repetidos=saltos=0
        for paso in traza:
            g=paso['generacion'];f=paso['cuadro']
            if g in anteriores:
                repetidos+=f==anteriores[g]
                saltos+=f>anteriores[g]+1
            anteriores[g]=f
        reimpactos=[]
        for anterior,actual in zip(entradas,entradas[1:]):
            if actual['previo']==3:
                reimpactos.append(dict(generacion=actual['generacion'],
                    mismo_codigo=anterior['codigo']==actual['codigo'],repeticion=actual['repeticion']))
        medida.update(reimpactos=reimpactos,cadencia=dict(despachos=len(traza),
            cuadros_repetidos=repetidos,saltos_de_cuadro=saltos,paridad_original=False),
            traza_motor=traza)
        if a.acuse:
            pedidos=[dict(generacion=int(g),orden=int(o),vinculado=v=='true') for g,o,v in re.findall(
                r'Piccolo reinicio pedido gen (\d+) orden (\d+) vinculado (true|false)',texto)]
            aplicados=[dict(orden=int(o),codigo=int(c,16),valido=v=='true',
                codigo_a=int(ca,16),clip_a=int(cla,16),cuadro_a=float(fa),duracion_a=int(da),
                codigo_b=int(cb,16),clip_b=int(clb,16),cuadro_b=float(fb),duracion_b=int(db))
                for o,c,v,ca,cla,fa,da,cb,clb,fb,db in re.findall(
                    r'reinicio aplicado orden (\d+) codigo ([0-9A-Fa-f]+) valido (true|false) '
                    r'A ([0-9A-Fa-f]+)/([0-9A-Fa-f]+)/([-\d.]+)/(\d+) '
                    r'B ([0-9A-Fa-f]+)/([0-9A-Fa-f]+)/([-\d.]+)/(\d+)',texto)]
            observados=[dict(generacion=int(g),orden=int(o),codigo=int(c,16),
                cuadro=float(f),duracion=int(d),fin=v=='true') for g,o,c,f,d,v in re.findall(
                    r'Piccolo acuse observado gen (\d+) orden (\d+) codigo ([0-9A-Fa-f]+) '
                    r'cuadro ([-\d.]+) duracion (\d+) fin (true|false)',texto)]
            medida.update(pedidos_reinicio=pedidos,reinicios_aplicados=aplicados,acuses_observados=observados)
            por_generacion={p['generacion']:p['orden'] for p in pedidos}
            por_orden={p['orden']:p for p in aplicados}
            if not pedidos or len(por_generacion)!=len(pedidos) or not all(p['vinculado'] for p in pedidos):
                raise P.Fallo('Identidad de reinicio ausente, repetida o sin vinculo')
            if not observados or not aplicados or not all(p['valido'] for p in aplicados):
                raise P.Fallo('El motor no acredita el reinicio en cuadro0 de A/B')
            for p in aplicados:
                if p['codigo_a']!=p['codigo'] or p['cuadro_a']!=0 or p['duracion_a']!=30:
                    raise P.Fallo('Acuse A no corresponde al reinicio solicitado')
                if p['duracion_b'] and (p['codigo_b']!=p['codigo'] or p['cuadro_b']!=0 or p['duracion_b']!=30):
                    raise P.Fallo('Acuse B no corresponde al reinicio solicitado')
            for p in observados:
                gen=p['generacion']-1 if p['fin'] else p['generacion']
                if por_generacion.get(gen)!=p['orden'] or p['orden'] not in por_orden:
                    raise P.Fallo('Un cuadro/fin se atribuyo a una orden distinta de la aplicada')
                if por_orden[p['orden']]['codigo']!=p['codigo']:
                    raise P.Fallo('El acuse observado cambio de clip')
            iguales=[r for r in reimpactos if r['mismo_codigo']]
            ordenes_observadas={p['orden'] for p in observados}
            iguales_confirmados=[r for r in iguales if por_generacion.get(r['generacion']) in ordenes_observadas]
            medida['reimpactos_iguales_confirmados']=iguales_confirmados
            if not iguales_confirmados:
                raise P.Fallo('No hubo reimpacto natural del mismo codigo con reinicio confirmado')

    key='solIWX_piccolo_acuse' if a.acuse else 'solIWX_piccolo_reaccion'
    P.ESCENARIOS[key]=('Piccolo: reaccion opt-in con animacion original y retorno',env,jugar)
    r=P.correr('smoke_b',key,[],None,{'timeout':'350'},True)
    for nombre,datos in [('medida.json',medida),('resultado.json',r),('cuerpo.json',muestras)]:
        with (out/nombre).open('x',encoding='utf-8') as f:
            json.dump(datos,f,default=str,indent=1,ensure_ascii=False,allow_nan=False)
    print(P.tabla([r]),flush=True)
    print(json.dumps({k:v for k,v in medida.items() if k not in ('esqueleto_b3','traza_motor','acuses_observados')},indent=1),flush=True)
    return 0 if r['ok'] else 1


if __name__=='__main__':
    raise SystemExit(main())
