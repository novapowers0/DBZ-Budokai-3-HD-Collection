"""Modelo B3 Goku traje4: tabla/carga nativa y ausencia del aro superpuesto."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import time

import pruebas as P
from iwx_bubbles_focal import FUENTE, HOME, pantalla2


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--claves',default='0x7D1,0x7D2')
    a=ap.parse_args()
    claves=[int(x,0) for x in a.claves.split(',')]
    if not claves or any(x not in (0x7D1,0x7D2) for x in claves):
        ap.error('Solo las misiones con Goku muerto: 0x7D1 y 0x7D2')
    os.environ['DBZ3_AGENTE']='solIWX'
    os.environ['PYTHONUTF8']='1'
    out=HOME/f'halo_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA',out,flush=True)
    pantalla2()
    resultados=[]
    for clave in claves:
        medida=dict(clave=hex(clave),paridad_global=False)
        def env(_):
            return dict(DBZ3_TEST_IWMINI=hex(clave),DBZ3_TEST_IWMINI_HALO_NATIVO='1',
                DBZ3_PS2_GAMES=FUENTE)
        def jugar(j,_):
            j.log(r'halo nativo B3 traje=4 fid=282 overlay=false',150)
            j.log(r'entrada acabada: mando al jugador',190)
            P.dormir(2)
            h=P.K32.OpenProcess(0x0410,False,j.proc.pid)
            if not h:
                raise P.Fallo('No se puede leer el modelo cargado')
            try:
                def leer(addr,n):
                    b=P.leer(h,P.GBASE+addr,n)
                    if not b or len(b)!=n:
                        raise P.Fallo(f'Lectura incompleta de {addr:X}')
                    return b
                u32=lambda addr:struct.unpack('>I',leer(addr,4))[0]
                u16=lambda addr:struct.unpack('>H',leer(addr,2))[0]
                cab=leer(0x8234ABB8,16)
                trajes,por_traje=cab[4:6]
                modelos=struct.unpack_from('>I',cab,8)[0]
                cfg_traje=u16(0x8245EFC0+16+18)
                actual=modelos+12*(cfg_traje*por_traje)
                fid,cache=u32(actual),u32(actual+8)
                medida.update(trajes=trajes,modelos_por_traje=por_traje,
                    tabla=hex(modelos),cfg_traje=cfg_traje,fid=fid,cache=hex(cache))
                if cfg_traje!=4 or fid!=282 or not 0x40000000<=cache<0xA0000000:
                    raise P.Fallo(f'Traje/carga distintos del modelo con aureola: {medida}')
                medida['cabecera_cache']=leer(cache,32).hex()
            finally:
                P.K32.CloseHandle(ctypes.c_void_p(h))
            if 'aureola de IW (409/0 parte' in j.texto_log():
                raise P.Fallo('El aro superpuesto sigue cargado junto al nativo')
            j.captura('halo_nativo_ready')
            j.tecla('W 1800',2000)
            j.captura('halo_nativo_movimiento')
        key=f'solIWX_{clave:X}_halo'
        P.ESCENARIOS[key]=('Goku B3 con aureola propia, READY y movimiento',env,jugar)
        r=P.correr('smoke_b',key,[],None,{'timeout':'350'},True)
        medida.update(run=str(r['run']),resultado=r)
        medida['fotos']=[dict(ruta=str(f),sha256=hashlib.sha256(f.read_bytes()).hexdigest())
            for f in Path(r['run']).glob('halo_nativo_*.png')]
        with (out/f'{clave:X}.json').open('x',encoding='utf-8') as f:
            json.dump(medida,f,default=str,indent=1,ensure_ascii=False)
        resultados.append(r)
        if not r['ok']:
            break
    print(P.tabla(resultados),flush=True)
    return 0 if len(resultados)==len(claves) and all(r['ok'] for r in resultados) else 1


if __name__=='__main__':
    raise SystemExit(main())
