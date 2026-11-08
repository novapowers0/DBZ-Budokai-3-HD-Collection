"""Prueba focal de la costura del campo; usa siempre el arnes pruebas.py."""
import argparse
import ctypes
import json
import os
import re
from pathlib import Path
import struct
import time
import pruebas as P

OUT=Path('D:/tmp/astraIW')

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--antes',action='store_true')
    ap.add_argument('--copia',default='sol')
    ap.add_argument('--quieto',action='store_true')
    ap.add_argument('--sin-depth',action='store_true')
    ap.add_argument('--clave',type=lambda x:int(x,0),default=0x7D1)
    ap.add_argument('--punto',default='888,0')
    ap.add_argument('--altura',type=float,default=0)
    ap.add_argument('--direccion',default='1,0')
    ap.add_argument('--nativa',action='store_true')
    ap.add_argument('--solo-modelos',action='store_true')
    a=ap.parse_args()
    os.environ['DBZ3_AGENTE']='astraIW'
    destino=OUT/(time.strftime('%Y%m%d-%H%M%S')+('_port_antes' if a.antes else '_port_rejilla'))
    destino.mkdir()
    px,pz=map(float,a.punto.split(','));dx,dz=map(float,a.direccion.split(','))
    def env(_):
        e=dict(DBZ3_TEST_IWMINI=hex(a.clave),DBZ3_PS2_GAMES='D:/DBZ3HD/ps2_games',
               DBZ3_SUELO_PRUEBA=f'{px},{pz};{px+52*dx},{pz+52*dz}',DBZ3_SUELO_PRUEBA_PASO='0.75',
               DBZ3_TEST_IWCAMTRAZA=str(destino/'camara.csv'))
        e['DBZ3_TEST_IW_REJILLA_REGS']='1'
        e['DBZ3_TEST_IW_REJILLA_ARMAR']=str(destino/'iniciar.req')
        e['REX_DBZ3_PERF_LOGGING']='true'
        if a.antes:e['DBZ3_IW_REJILLA']='0'
        if a.quieto:e['DBZ3_TEST_IW_REJILLA_PARAR']='1'
        if a.nativa:e['DBZ3_TEST_IW_REJILLA_NATIVA']='1'
        if a.sin_depth:e['DBZ3_SIN_ZBUF']='1'
        return e
    def jugar(j,_):
        if a.solo_modelos:
            j.log('suelo_iw: AWO nativo',70)
            print('\n'.join(l for l in j.texto_log().splitlines() if 'AWO nativo' in l),flush=True)
            return
        j.log('iwmini: entrada acabada',100)
        time.sleep(3)
        mgr,_=P._freecam_mem(j)
        h=P.K32.OpenProcess(0x0438,False,j.proc.pid)
        if not h:raise RuntimeError('sin escritura de la copia de prueba')
        try:
            for actor in (0,1):
                for o in (48,64,80):
                    b=struct.pack('>3f',px+actor*40,a.altura,pz)
                    n=ctypes.c_size_t()
                    assert P.K32.WriteProcessMemory(ctypes.c_void_p(h),ctypes.c_void_p(P.GBASE+mgr+2096+actor*8240+o),
                                                   b,len(b),ctypes.byref(n)) and n.value==len(b)
        finally:P.K32.CloseHandle(ctypes.c_void_p(h))
        (destino/'iniciar.req').write_text('1',encoding='ascii')
        datos=dict(run=str(j.run),filas=[],fotos=[])
        fin=time.monotonic()+75
        listo=False;cruce=None;ultimo=None;n_inicio=None
        while time.monotonic()<fin:
            mgr,n=P._freecam_mem(j)
            if not mgr:time.sleep(.01);continue
            x=mgr+2096
            def vec(addr):
                b=P.leer(j.h,P.GBASE+addr,12)
                return list(struct.unpack('>3f',b)) if b else [0,0,0]
            pos=vec(x+64)
            if not listo and abs(pos[0]-px)<5 and abs(pos[2]-pz)<5:listo=True;n_inicio=n
            if not listo:time.sleep(.01);continue
            m=struct.unpack('>16f',P.leer(j.h,P.GBASE+mgr+0x60,64))
            ojo=[-sum(m[12+i]*m[k*4+i] for i in range(3)) for k in range(3)]
            d=dict(n=n,pos=pos,ojo=ojo)
            periodo=1800 if a.clave==0x7D1 else 3000
            if ultimo and any(abs(abs(pos[k]-ultimo['pos'][k])-periodo)<5 for k in (0,2)) and cruce is None:
                cruce=n
            datos['filas'].append(d)
            etiqueta=None
            if not datos['fotos'] and n-n_inicio>=240:etiqueta='antes'
            if cruce is not None and not any(f['etiqueta']=='cruce' for f in datos['fotos']):etiqueta='cruce'
            if cruce is not None and n-cruce>(120 if a.quieto else 30) and not any(f['etiqueta']=='despues' for f in datos['fotos']):etiqueta='despues'
            if etiqueta:
                ptr=re.search(r'GPU registros host ([0-9A-F]+)',j.texto_log())
                if ptr:
                    host=int(ptr[1],16)
                    d['gpu_host']={hex(reg):int.from_bytes(P.leer(j.h,host+reg*4,4),'little')
                                   for reg in (0x210f,0x2110,0x2111,0x2112,0x2206,0x2302)}
                d['matriz']=list(m)
                d['camara_base']=list(struct.unpack('>24f',P.leer(j.h,P.GBASE+mgr,96)))
                d['proyecciones']={hex(addr):list(struct.unpack('>16f',P.leer(j.h,P.GBASE+addr,64)))
                                   for addr in (0x82389320,0x82389360)}
                print('medida',etiqueta,{k:d[k] for k in ('n','pos','ojo')},flush=True)
                from PIL import Image
                pp=j.captura(etiqueta)
                with Image.open(pp) as im:im.resize((im.width//2,im.height//2)).save(destino/(etiqueta+'.png'))
                datos['fotos'].append(dict(etiqueta=etiqueta,antes=d,despues=P._freecam_mem(j)[1]))
            ultimo=d
            if cruce is not None and n-cruce>(140 if a.quieto else 50):break
            time.sleep(.005)
        (destino/'medida.json').write_text(json.dumps(datos,indent=2),encoding='utf-8')
        assert listo,'no llego a la costura'
        assert cruce is not None,'sin cruce observado'
    nombre='astraIW_rejilla_antes' if a.antes else 'astraIW_rejilla'
    P.ESCENARIOS[nombre]=('costura IW y copias vecinas',env,jugar)
    r=P.correr(a.copia,nombre,[],None,{'timeout':'220'},True)
    print(P.tabla([r]),flush=True)
    print(destino,flush=True)
    if not r['ok']:raise SystemExit(1)

if __name__=='__main__':main()
