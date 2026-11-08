"""Una focal de seguimiento: fase/filtro, sin habilitar candidatos en normal."""
import argparse
import json
import os
from pathlib import Path
import time

import pruebas as P
from iwx_bubbles_focal import pantalla2,FUENTE,HOME
from iwx_seguimiento import analizar_port,recuperar_estados,analizar


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--clave',type=lambda x:int(x,0),choices=(0x7d0,0x7d1,0x7d2),default=0x7d2)
    ap.add_argument('--original',type=Path,help='Serie propia con estados VSync conservados')
    a=ap.parse_args()
    coeficiente=.3 if a.clave==0x7d2 else .12
    original=None
    if a.clave!=0x7d2 and not a.original:
        ap.error('Goku requiere su original propio autenticado')
    if a.original:
        filas,auditoria=recuperar_estados(a.original)
        if any(int(r['video']['clave'],0)!=a.clave for r in filas):
            ap.error('La clave original no coincide')
        original=analizar(filas,coeficiente)
        if max(r['error']['anterior_selector'] for r in original['filas'])>.001:
            ap.error('La hipotesis no reproduce el filtro original')
        original.pop('filas')
        original['auditoria_video']=auditoria
        original['auditoria_video'].pop('segmentos',None)
    os.environ['DBZ3_AGENTE']='solIWX'
    os.environ['PYTHONUTF8']='1'
    out=HOME/f'seguimiento_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA',out,flush=True)
    pantalla2()
    def env(_):
        e=dict(DBZ3_TEST_IWMINI=hex(a.clave),DBZ3_PS2_GAMES=FUENTE,
            TEST_IWX_SEGUIMIENTO=str(out/'seguimiento.csv'),
            DBZ3_TEST_IWCAMTRAZA=str(out/'camara.csv'),DBZ3_IWVUELO_PJ='0')
        if a.clave!=0x7d2:e['TEST_IWX_SEGUIMIENTO_PREVIO']='1'
        if a.clave==0x7d1:e['DBZ3_TEST_GP2_BUBBLES']='1'
        return e
    def jugar(j,_):
        if a.clave==0x7d1:
            j.log(r'gp2 bubbles READY terminado: mando y reloj liberados',190)
        else:
            j.log(r'entrada acabada: mando al jugador',190)
            # El cartel READY dura 140 ticks; no se fuerza ni se salta.
            P.dormir(3)
        j.tecla('W 2000',2200)
        P.dormir(1)
        j.tecla('D 1500',1700)
        P.dormir(1)
        if a.clave!=0x7d2:
            j.tecla('Backspace 100',300)
            P.dormir(2)
    key=f'solIWX_{a.clave:X}_seguimiento'
    P.ESCENARIOS[key]=('IW: referencia previa, carrera, frenada y dash',env,jugar)
    r=P.correr('smoke_b',key,[],None,{'timeout':'350'},True)
    try:
        medida=dict(port=analizar_port(out/'seguimiento.csv',coeficiente),original=original)
        with (out/'medida.json').open('x',encoding='utf-8') as f:
            json.dump(medida,f,indent=1,ensure_ascii=False,allow_nan=False)
    except (OSError,ValueError,KeyError) as e:
        r['ok']=False
        r['msg']=(r['msg']+'; ' if r['msg'] else '')+str(e)
    with (out/'resultado.json').open('x',encoding='utf-8') as f:
        json.dump(r,f,default=str,indent=1,ensure_ascii=False)
    print(P.tabla([r]),flush=True)
    return 0 if r['ok'] else 1


if __name__=='__main__':
    raise SystemExit(main())
