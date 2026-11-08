"""Un impacto 7D8: colores reales publicados al consumidor y foto a media escala."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import time

from PIL import Image
import pruebas as P
from iwx_bubbles_focal import FUENTE, HOME, pantalla2
from iwx_chr_fade import inventario


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf',type=Path,required=True)
    a=ap.parse_args();os.environ['DBZ3_AGENTE']='solIWX'
    out=HOME/f'chr_fade_focal_{time.time_ns()}';out.mkdir()
    print('EVIDENCIA',out,flush=True);pantalla2()
    original=inventario(a.elf);color=f"{original['color_be']:08X}"
    patron=r'CHRFADE9: consumidor dibujo actor (\d) intensidad ([\d.]+) cuadrado ([\d.]+) colores ([0-9A-F]{8})/([0-9A-F]{8})'
    medida={}
    def env(_):
        return dict(DBZ3_TEST_IWMINI='0x7D8,auto',DBZ3_PS2_GAMES=FUENTE)
    def jugar(j,_):
        j.log(r'iwdisparo: juego .* empieza',190)
        j.log(patron,30)
        foto=j.captura('piccolo_impacto')
        with Image.open(foto) as im:
            im.resize((im.width//2,im.height//2)).save(out/'piccolo_impacto_05.png')
        P.dormir(1)
        text=j.texto_log()
        with (out/'impacto.log').open('x',encoding='utf-8') as f:f.write(text)
        filas=[dict(rol=int(rol),intensidad=float(v),cuadrado=float(q),luz=l,niebla=n)
            for rol,v,q,l,n in re.findall(patron,text)]
        fallos=[r for r in filas if r['rol']!=1 or r['luz']!=color or r['niebla']!=color
            or not 0<=r['intensidad']<=.95 or not math.isfinite(r['cuadrado'])
            or abs(r['cuadrado']-r['intensidad']**2)>2e-6]
        medida.update(filas=len(filas),impactos=len(re.findall('impacto hueso',text)),
            color_esperado=color,colores_vistos=sorted(set(r['luz'] for r in filas)),
            muestra=filas[:5],fallos=fallos,run=str(j.run),foto=str(foto),
            elf_sha256=original['elf_sha256'],paridad_visual=False,
            paridad_duracion=False,paridad_reaccion=False,paridad_integral=False)
        assert medida['impactos']>0,'Sin impactos reales'
        assert filas and not fallos,'Colores del consumidor distintos del original'
    key='solIWX_piccolo_tinte'
    P.ESCENARIOS[key]=('IW: color corporal de impacto de Piccolo',env,jugar)
    resultado=P.correr('smoke_b',key,[],None,{'timeout':'350'},True)
    for nombre,datos in (('medida.json',medida),('resultado.json',resultado)):
        with (out/nombre).open('x',encoding='utf-8') as f:json.dump(datos,f,default=str,indent=1,ensure_ascii=False,allow_nan=False)
    print(P.tabla([resultado]),flush=True);print(json.dumps(medida,indent=1),flush=True)
    return 0 if resultado['ok'] else 1


if __name__=='__main__':raise SystemExit(main())
