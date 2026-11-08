"""Focal MSN019: errores reales de mando y observacion de los clips instalados."""
import argparse
import json
import os
from pathlib import Path
import struct
import time
import pruebas as P

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--auto',action='store_true');ap.add_argument('--segundos',type=float,default=15);a=ap.parse_args()
    os.environ['DBZ3_AGENTE']='astraIW'
    out=Path('D:/tmp/astraIW')/(time.strftime('%Y%m%d-%H%M%S')+'_port_msn019');out.mkdir()
    def env(_):
        return dict(DBZ3_TEST_IWMINI='0x7E1'+(',auto' if a.auto else ''),DBZ3_TEST_MSN019_ESTADOS='1',DBZ3_PS2_GAMES='D:/DBZ3HD/ps2_games')
    def jugar(j,_):
        j.log('MSN019 estados: (actor=0 |candidato detenido)',120)
        assert 'candidato detenido' not in j.texto_log(),'candidato bloqueado; ver log'
        if not a.auto:j.tecla('Quote',100)
        filas=[];inicio=time.monotonic();foto=False
        while time.monotonic()-inicio<a.segundos:
            mgr,_=P._freecam_mem(j)
            if not mgr:time.sleep(.01);continue
            x=mgr+2096
            n=int.from_bytes(P.leer(j.h,P.GBASE+0x8238D6B4,4),'big')
            alias=int.from_bytes(P.leer(j.h,P.GBASE+x+0x650+0x520,4),'big')&0xfff
            frame,dur=struct.unpack('>2f',P.leer(j.h,P.GBASE+x+0x650+0x608,8))
            filas.append(dict(n=n,alias=hex(alias),frame=frame,dur=dur))
            if not foto and time.monotonic()-inicio>3:
                from PIL import Image
                f=j.captura('recuperacion')
                with Image.open(f) as im:im.resize((im.width//2,im.height//2)).save(out/'recuperacion.png')
                foto=True
            if 'candidato detenido' in j.texto_log():raise RuntimeError('candidato detenido durante el juego')
            time.sleep(.01)
        (out/'medida.json').write_text(json.dumps(dict(run=str(j.run),filas=filas),indent=2),encoding='utf-8')
        print('Aliases observados',sorted({f['alias'] for f in filas}),flush=True)
    P.ESCENARIOS['astraIW_msn019']=('MSN019: estados originales sobre modeloB3',env,jugar)
    r=P.correr('opus2','astraIW_msn019',[],None,{'timeout':str(180+int(a.segundos))},True)
    print(P.tabla([r]),out,flush=True)
    if not r['ok']:raise SystemExit(1)

if __name__=='__main__':main()
