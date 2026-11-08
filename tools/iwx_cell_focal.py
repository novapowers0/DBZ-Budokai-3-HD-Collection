"""Altura READY de Cell, antes/despues y camara realmente dibujada."""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import re
import time

import pruebas as P
from iwx_bubbles_focal import pantalla2,FUENTE,HOME


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--antes',action='store_true')
    a=ap.parse_args()
    os.environ['DBZ3_AGENTE']='solIWX'
    os.environ['PYTHONUTF8']='1'
    out=HOME/f'cell_altura_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA',out,flush=True)
    original=HOME/'original_cuadros_1793683951569622500/7DD/cuadros.jsonl'
    ult=json.loads(original.read_text(encoding='utf-8').splitlines()[-1])['seguimiento']
    medida=dict(antes=a.antes,fuente_original=str(original),original_ojo_y=ult['ojo'][1],
        original_objetivo_y=ult['objetivo'][1],paridad_global=False)
    pantalla2()
    def env(_):
        return dict(DBZ3_TEST_IWMINI='0x7DD',DBZ3_PS2_GAMES=FUENTE,
            DBZ3_TEST_IWMINI_CAMLOG='1',DBZ3_TEST_IWCAMTRAZA=str(out/'camara.csv'))
    def jugar(j,_):
        j.log(r'campo camara READY 110 ',190)
        texto=j.texto_log()
        filas=list(re.finditer(r'campo camara READY (\d+) ojo \(([^)]+)\) obj \(([^)]+)\) fov ([\d.]+)',texto))
        ready=filas[-1]
        ojo=[float(v) for v in ready[2].split(',')]
        obj=[float(v) for v in ready[3].split(',')]
        if not a.antes:
            if 'altura camara descriptor jugador=33 altura=10 leida=true' not in texto:
                raise P.Fallo('el ELF no proporciono la altura medida de Cell')
        # Medir tambien pies/ancla: la primera prueba atribuia el residual a
        # una cola temporal, pero esperar no lo eliminaba. No subir tolerancias.
        if not a.antes:
            muestras=re.findall(r'Cell READY pies_y=([^ ]+) seguido_y=([^ ]+) altura=([^\r\n]+)',texto)
            suelos=re.findall(r'Cell suelo pies_antes=([^ ]+) suelo=([^ ]+) delta=([^\r\n]+)',texto)
            if not muestras:
                raise P.Fallo('faltan pies y ancla READY de Cell')
            pies,seguido,altura=map(float,muestras[-1])
            medida['ready_pies_ancla']=dict(pies_y=pies,seguido_y=seguido,altura=altura)
            medida['correcciones_suelo']=[dict(zip(('pies_antes','suelo','delta'),map(float,r))) for r in suelos]
            if not all(math.isfinite(v) for v in (pies,seguido,altura)) or max(abs(pies),abs(seguido))>.001 or altura!=10:
                raise P.Fallo(f'pies/ancla READY distintos al original: {pies}/{seguido}/{altura}')
        P.dormir(1)
        with (out/'camara.csv').open(newline='') as f:
            traza=[r for r in csv.reader(f) if len(r)==19][-10:]
        if len(traza)<5 or not all(math.isfinite(float(v)) for r in traza for v in r[:17]):
            raise P.Fallo('traza render incompleta o no finita')
        espera=(24,14) if a.antes else (25,15)
        medida['ready_edad110']=dict(ojo=ojo,objetivo=obj)
        ojo=[float(v) for v in traza[-1][11:14]]
        obj=[float(v) for v in traza[-1][14:17]]
        error=max(abs(ojo[1]-espera[0]),abs(obj[1]-espera[1]))
        error_render=max(abs(float(r[2])-espera[0]) for r in traza)
        error_original=max(abs(ojo[1]-ult['ojo'][1]),abs(obj[1]-ult['objetivo'][1]))
        medida.update(edad=int(ready[1]),ojo=ojo,objetivo=obj,fov=float(ready[4]),
            error_altura=error,error_render=error_render,error_original_estacionario=error_original,
            alcance='altura READY estacionaria; no emparejamiento temporal completo')
        if error>.01 or error_render>.02 or (not a.antes and error_original>.01):
            raise P.Fallo(f'altura distinta: publicada={error}, render={error_render}, original={error_original}')
        P.dormir(1)
    key='solIWX_cell_altura_'+('antes' if a.antes else 'despues')
    P.ESCENARIOS[key]=('Cell: altura READY del descriptor original',env,jugar)
    r=P.correr('smoke_b',key,[],None,{'timeout':'350'},True)
    for nombre,datos in [('medida.json',medida),('resultado.json',r)]:
        with (out/nombre).open('x',encoding='utf-8') as f:
            json.dump(datos,f,default=str,indent=1,ensure_ascii=False,allow_nan=False)
    print(P.tabla([r]),flush=True)
    print(json.dumps(medida,indent=1),flush=True)
    return 0 if r['ok'] else 1


if __name__=='__main__':
    raise SystemExit(main())
