"""Pruebas focales por contador: carga real y pilotos que deben acertar."""
import argparse
import json
import os
from pathlib import Path
import struct
import shutil
import time
import pruebas as P
from PIL import Image

OUT=Path('D:/tmp/solIWZ')


def caso(clave,copia):
    out=OUT/f'estados_{clave:X}_{time.strftime("%H%M%S")}'
    out.mkdir()
    def env(_):
        return dict(DBZ3_TEST_IWMINI=hex(clave)+('' if clave==0x7D0 else ',auto'),
                    DBZ3_PS2_GAMES='D:/DBZ3HD/ps2_games')
    def jugar(j,_):
        j.log(r'entrada acabada: mando al jugador|iwdisparo: juego .* empieza|r14: en juego',150)
        P.dormir(2.5 if clave==0x7D0 else .1)
        if clave==0x7D0:j.tecla('L 1200',0)
        filas=[];vistos=set();fotos=[]
        t0=time.monotonic()
        fin=time.monotonic()+(3 if clave==0x7D0 else 22)
        while time.monotonic()<fin:
            mgr,n=P._freecam_mem(j)
            if not mgr:continue
            x=mgr+2096+(8240 if clave in (0x7D7,0x7D8) else 0)
            raw=P.leer(j.h,P.GBASE+x+0x650,0x610)
            if not raw:continue
            cod=struct.unpack_from('>I',raw,0x520)[0]&0xFFF
            d=dict(n=n,codigo=hex(cod),f=struct.unpack_from('>f',raw,0x608)[0],
                   duracion=struct.unpack_from('>I',raw,0x60C)[0])
            filas.append(d);vistos.add(cod)
            tag=None
            # No bloquear16cuadros de carga2 con una captura: al estar llena
            # la energia,3 solo aparece un cuadro antes de4 en el original.
            if clave==0x7D0 and cod==0x8D1 and cod not in fotos:tag=f'carga_{cod:X}'
            if clave==0x7D8 and 'impacto hueso' in j.texto_log() and 'impacto' not in fotos:tag='impacto'
            if clave in (0x7D3,0x7D7) and time.monotonic()-t0>1 and 'hud' not in fotos:tag='hud'
            if clave==0x7DA and 'ronda 2 (fila' in j.texto_log() and 'ronda2' not in fotos:tag='ronda2'
            if tag:
                foto=j.captura(tag)
                with Image.open(foto) as im:im.resize((im.width//2,im.height//2)).save(out/f'{tag}_05.png')
                fotos.append(cod if clave==0x7D0 else tag)
            time.sleep(.006)
        (out/'medida.json').write_text(json.dumps(dict(filas=filas,codigos=list(map(hex,sorted(vistos))),
            fotos=fotos,run=str(j.run)),indent=1),encoding='utf-8')
        print('CODIGOS',list(map(hex,sorted(vistos))),'FOTOS',fotos,flush=True)
        if clave==0x7D0:assert {0x8D0,0x8D1}<=vistos,'carga2->3 no observada'
        if clave==0x7D8:assert 'impacto hueso' in j.texto_log(),'piloto sin impactos; analizar huesos/cámara'
        if clave==0x7D7:assert 0xFB9 in vistos,'esquiva393 no observada'
        if clave==0x7DA:assert 'ronda 2 (fila' in j.texto_log(),'ronda2 no observada'
    nom=f'solIWZ_estados_{clave:X}'
    P.ESCENARIOS[nom]=('carga/esquiva/impacto medidos',env,jugar)
    r=P.correr(copia,nom,[],None,{'timeout':'400'},True)
    print(P.tabla([r]),flush=True)
    (out/'resultado.json').write_text(json.dumps(r,indent=1,default=str),encoding='utf-8')


if __name__=='__main__':
    ap=argparse.ArgumentParser()
    ap.add_argument('--claves',default='0x7D0,0x7D8,0x7D7')
    ap.add_argument('--copia',default='sol')
    a=ap.parse_args()
    os.environ['DBZ3_AGENTE']='solIWZ'
    fijo=OUT/f'estados_{time.strftime("%H%M%S")}.exe'
    shutil.copy2(P.EXE,fijo)
    P.EXE=fijo # otra compilacion puede retirar el exe dual entre dos casos
    for clave in a.claves.split(','):caso(int(clave,0),a.copia)
