"""Datos SHOOT del disco: identidad del tramo observado en PCSX2."""
import json
import struct
from pathlib import Path
from astra_iw_auditoria import entrada_afs,hijo,u,OUT

def main():
    afs=Path('D:/DBZ3HD/ps2_games/Infinite World (USA)/USR/DATA_CMN.AFS')
    m=hijo(entrada_afs(afs,890),4)
    f=lambda o:struct.unpack_from('<f',m,o)[0]
    b=u(m,0x2c+2*4);tramos=[]
    for i in range(u(m,b+0x60)):
        v=list(struct.unpack_from('<7f',m,b+0x64+i*28));tramos.append(v)
        if v==[1.5,-60.,200.,200.,660.,200.,200.]:print('Tramo observado original: indice',i)
        if v==[1.,280.,110.,1000.,-10.,95.,50.]:print('Tramo profundidad variable observado: indice',i)
    d=dict(dificultad=2,cuantos=u(m,b),duracion=f(b+12),velocidad_mira=f(16),tramos=tramos)
    (OUT/'ladrillos_datos.json').write_text(json.dumps(d,indent=2),encoding='utf-8')
    print('SHOOT',d['cuantos'],d['duracion'],d['velocidad_mira'],len(tramos))
    for idioma in ('USA','JPN'):
        b=entrada_afs(afs.with_name(f'ADX_{idioma}.AFS'),1120)
        hz,n=struct.unpack_from('>II',b,8)
        print('Voz1120',idioma,hz,n,n*60/hz)
    eu=Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/USR/ADX_USA.AFS')
    b=entrada_afs(eu,1120);hz,n=struct.unpack_from('>II',b,8)
    print('Voz1120 EU ingles',hz,n,n*60/hz)
    for entrada in (5,7,9):
        paquete=entrada_afs(afs.with_name('DATA_USA.AFS'),entrada)
        print('UI',entrada,[(i,hijo(paquete,i)[:4].decode('ascii',errors='replace'),len(hijo(paquete,i)),u(hijo(paquete,i),16) if len(hijo(paquete,i))>20 else 0) for i in range(u(paquete,16))])

if __name__=='__main__':main()
