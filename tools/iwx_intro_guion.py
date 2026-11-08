"""Inventaria llamadas del SPX donante sin ejecutarlo ni cambiar recursos."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import time

import iwx_escenarios as E


def llamadas(spx):
    if not spx.startswith(b'#SPX') or len(spx)<24:
        raise ValueError('No es un SPX con cabecera completa')
    inicio=struct.unpack_from('<I',spx,20)[0]
    if not 24<=inicio<len(spx):
        raise ValueError('Inicio de bytecode fuera del SPX')
    tokens=E.EC._spx_tokens(spx,inicio)
    out=[];prefijo=[]
    for off,t in tokens:
        if t[0]=='SEP':
            prefijo=[]
        elif t[0]=='CALL':
            out.append(dict(offset=off,llamada=hex(t[1]),
                tokens_previos=[dict(offset=o,token=v) for o,v in prefijo],
                bytes_previos=spx[max(inicio,off-24):off+12].hex()))
            prefijo.append((off,t))
        else:
            prefijo.append((off,t))
    return dict(sha256=hashlib.sha256(spx).hexdigest(),bytes=len(spx),inicio=inicio,
        llamadas_observadas=out,
        limite='Tokenizador parcial: literales y offsets observados; no traduce control de flujo ni todo el guion')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mods',type=Path,default=Path('D:/DBZ3HD/smoke_b/mods'))
    p.add_argument('--campo',type=int,default=884)
    a=p.parse_args()
    out=Path('D:/tmp/solIWX')/f'guion_intro_{time.time_ns()}'
    out.mkdir()
    os.environ['TMP']=os.environ['TEMP']=str(out)
    datos={}
    for i in (44,48,54):
        amb=E.unpack(E.EC.entrada_afs(E.ROOT/'us/data_cmn.afs',i),out,'nativo'+str(i))
        datos['nativo'+str(i)]=llamadas(E.split(amb)[3])
    ruta=a.mods/('Campo IW '+str(a.campo))/'us/data_cmn.afs'/str(E.CI.ENTRADAS[a.campo])
    if ruta.is_dir():
        archivos=[f for f in ruta.iterdir() if f.is_file()]
        if len(archivos)!=1:
            raise ValueError('Override ambiguo: se necesita exactamente un archivo')
        ruta=archivos[0]
    raw=ruta.read_bytes()
    stage=E.split(E.unpack(raw,out,'campo'+str(a.campo)))
    datos['instalado']=dict(ruta=str(ruta),sha256_archivo=hashlib.sha256(raw).hexdigest(),
        camaras=E.cameras(stage[0]),spx=llamadas(stage[3]))
    datos['instalado']['spx_donantes_identicos']=[k for k,v in datos.items()
        if k!='instalado' and v['sha256']==datos['instalado']['spx']['sha256']]
    with (out/'guion.json').open('x',encoding='utf-8') as f:
        json.dump(datos,f,indent=1,ensure_ascii=False)
    print(json.dumps(dict(salida=str(out),
        nativos={k:dict(sha256=v['sha256'],llamadas=len(v['llamadas_observadas']),
            intro=[dict(offset=c['offset'],llamada=c['llamada'],
                literales=[t['token'][1] for t in c['tokens_previos'] if t['token'][0]=='PUSH'],
                bytes_previos=c['bytes_previos'])
                for c in v['llamadas_observadas']
                if c['offset']<370 and c['llamada'] in ('0x106','0x1d8','0xe4')])
            for k,v in datos.items() if k!='instalado'},
        instalado=dict(ruta=str(ruta),sha256_spx=datos['instalado']['spx']['sha256'],
            spx_donantes_identicos=datos['instalado']['spx_donantes_identicos'],
            duraciones=[[c['cuadros'] for c in banco] for banco in datos['instalado']['camaras']])),
        indent=1,ensure_ascii=False))
    return 0


if __name__=='__main__':
    raise SystemExit(main())
