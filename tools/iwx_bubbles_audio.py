"""Lectura pura de voz/boca BBR; no confunde pedir ADX con oirlo en la salida."""
import math
import struct


def leer(read):
    def raw(a,n):
        if not 0x100000<=a<0x2000000 or a+n>0x2000000:
            raise ValueError('Lectura de audio fuera de EE')
        b=read(a,n)
        if len(b)!=n:
            raise ValueError('Lectura de audio incompleta')
        return b
    def u(a):return struct.unpack('<I',raw(a,4))[0]
    def f(a):
        v=struct.unpack('<f',raw(a,4))[0]
        if not math.isfinite(v):raise ValueError('Reloj de voz/boca no finito')
        return v
    n=u(0x4a4d40)
    if u(0x5c0dc8)!=0x7d1:raise ValueError('Audio BBR fuera de 7D1')
    mgr=u(0x49ca70);actor=mgr+0x28d0;lado=u(actor+0x1380)
    if lado!=1:raise ValueError('BBR no es el actor secundario')
    slot=mgr+(lado+3)*0x20+0x5a4
    voz=struct.unpack('<8I',raw(slot,32))
    d=dict(n=n,actor=actor,estado=struct.unpack('<H',raw(actor+0xcbc,2))[0],
        codigo=u(actor+0xb60),cuadro=f(actor+0xc68),perfil=u(actor+0xb8c),
        forma=u(actor+0xb94),forma_siguiente=u(actor+0xb98),
        boca_id=u(actor+0xca0),boca_cuadro=f(actor+0xca4),boca_modo=u(actor+0xca8),
        boca_ce4=u(actor+0xce4),boca_desactivada=raw(actor+0xccc,1)[0],
        voz_handle=voz[2],voz_estado=voz[3],voz_flags=voz[4],
        voz_perfil=voz[5]&65535,voz_linea=voz[5]>>16)
    if n!=u(0x4a4d40):raise ValueError('Audio cruza campos PAL')
    return d


def analizar(path,continuacion=False):
    import hashlib,json,zipfile
    from iwx_reloj_original import auditar_serie
    raw=path.read_bytes()
    auditoria=auditar_serie(path)
    rows=[json.loads(x) for x in raw.splitlines() if x.strip()]
    cambios=[]
    anterior=None
    for r in rows:
        a=r['bubbles_audio']
        with zipfile.ZipFile(r['video']['ruta']) as z:
            with z.open('eeMemory.bin') as ram:
                def read(addr,n):
                    ram.seek(addr);return ram.read(n)
                if leer(read)!=a:raise ValueError('El estado retenido no autentica voz/boca')
        if not r['estable'] or a['n']!=r['n'] or a['perfil']!=0x39 or a['forma']!=a['forma_siguiente']:
            raise ValueError('Audio inestable o identidad distinta de BBR')
        firma=tuple(a[k] for k in ('estado','codigo','voz_estado','voz_perfil','voz_linea','boca_id'))
        if firma!=anterior:
            cambios.append(dict(campo_video=r['campo_video'],**a));anterior=firma
    pedidos=[r for r in cambios if r['voz_perfil']==0x39 and r['voz_linea']==0 and r['voz_flags']&1]
    inicio=pedidos[0] if len(pedidos)==1 else None
    if not continuacion and (inicio is None or inicio['estado']!=10 or inicio['codigo']!=0x11d or inicio['boca_id']!=0x10048):
        raise ValueError('No se observo un unico inicio de voz0/boca48 en11D')
    fases=[]
    for r in rows:
        a=r['bubbles_audio']
        if not fases or fases[-1]['estado']!=a['estado']:
            fases.append(dict(estado=a['estado'],primero=a['n'],muestras=0,boca_min=a['boca_cuadro'],boca_max=a['boca_cuadro']))
        s=fases[-1];s['muestras']+=1;s['ultimo']=a['n']
        s['boca_min']=min(s['boca_min'],a['boca_cuadro']);s['boca_max']=max(s['boca_max'],a['boca_cuadro'])
    return dict(serie=str(path.resolve()),sha256=hashlib.sha256(raw).hexdigest(),
        auditoria=auditoria,muestras=len(rows),cambios=cambios,inicio=inicio,fases=fases,
        paridad_port=False,limites=['RAM acredita seleccion y fase, no PCM audible ni paridad del port',
            'No acredita fin/corte de voz ni retorno facial durante el juego'])


if __name__=='__main__':
    import argparse,json
    from pathlib import Path
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serie',type=Path,required=True)
    p.add_argument('--salida',type=Path,required=True)
    p.add_argument('--continuacion',action='store_true',help='La serie empieza con la voz ya solicitada')
    a=p.parse_args();r=analizar(a.serie,a.continuacion)
    with a.salida.open('x',encoding='utf-8') as f:json.dump(r,f,indent=1,ensure_ascii=False,allow_nan=False)
    print('MUESTRAS',r['muestras'],'INICIO',json.dumps(r['inicio']),'EVIDENCIA',a.salida)
    print(json.dumps(dict(cambios=r['cambios'],fases=r['fases']),ensure_ascii=False))
