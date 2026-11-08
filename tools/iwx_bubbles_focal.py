"""Una focal Bubbles candidato; CSV por tick, fuente EU explicita, sin habilitar normal."""
import argparse
from collections import Counter
import csv
import ctypes
import json
import math
import os
from pathlib import Path
import subprocess
import time

import pruebas as P

HOME=Path('D:/tmp/solIWX')
FUENTE='E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021'


def analizar_csv(path):
    """Solo un CSV cerrado: continuidad y cambios observados, no paridad IW."""
    with path.open(encoding='utf-8',newline='') as f:
        filas=list(csv.DictReader(f))
    if not filas or any(None in r or any(v is None for v in r.values()) for r in filas):
        raise ValueError('CSV Bubbles vacio/incompleto')
    if any(not math.isfinite(float(v)) for r in filas for v in r.values()):
        raise ValueError('CSV Bubbles con valor no finito')
    if not all(int(b['tick'])==int(a['tick'])+1 and b['sesion']==a['sesion']
               for a,b in zip(filas,filas[1:])):
        raise ValueError('CSV Bubbles no consecutivo o cruza sesiones')
    transiciones=[dict(tick=int(b['tick']),desde=int(a['estado']),hasta=int(b['estado']),
        codigo_antes=hex(int(a['codigo'])),codigo=hex(int(b['codigo'])),cuadro=float(b['cuadro']))
        for a,b in zip(filas,filas[1:]) if a['estado']!=b['estado'] or a['codigo']!=b['codigo']]
    return dict(csv=str(path),filas=len(filas),consecutivos=True,
        estados=dict(Counter(r['estado'] for r in filas)),
        codigos=dict(Counter(hex(int(r['codigo'])) for r in filas)),transiciones=transiciones,
        primero=filas[0],ultimo=filas[-1],
        desplazamiento_npc_max=max(math.dist([float(filas[0][k]) for k in ('x','y','z')],
            [float(r[k]) for k in ('x','y','z')]) for r in filas),
        transicion_y_continuidad=bool(transiciones) and int(filas[-1]['tick'])>transiciones[0]['tick'],
        paridad_original=False,normal_activado=False,
        limite='IC/RNG distintos: no equiparar trayectorias por ordinal; este CSV no prueba TARGET/LIVE47/fin9 ni paridad visual')


def pantalla2():
    P.U32.GetMonitorInfoW.argtypes=[P.wt.HMONITOR,ctypes.POINTER(P._MONINFO)]
    if P._pantalla_secundaria():
        return
    raw=subprocess.run(['powershell','-NoProfile','-Command',
        'Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Screen]::AllScreens | '
        'Select-Object DeviceName,WorkingArea | ConvertTo-Json -Compress'],capture_output=True,check=True).stdout
    monitores=json.loads(raw.decode('utf-8-sig'))
    if isinstance(monitores,dict):
        monitores=[monitores]
    elegido=next((m for m in monitores if m['DeviceName'].endswith('DISPLAY2')),None)
    if elegido is None:
        raise RuntimeError('Falta pantalla2 para la focal')
    b=elegido['WorkingArea']
    P._pantalla_secundaria=lambda:(b['X'],b['Y'],b['Right'],b['Bottom'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--analizar',type=Path,help='CSV cerrado existente: no lanzar el juego')
    p.add_argument('--salida',type=Path,help='JSON nuevo para el analisis offline')
    p.add_argument('--presentacion',action='store_true',help='Solo intro/READY y dibujo: no exige una decision de IA')
    a=p.parse_args()
    if a.analizar:
        resumen=analizar_csv(a.analizar)
        if a.salida:
            with a.salida.open('x',encoding='utf-8') as f:
                json.dump(resumen,f,indent=1,ensure_ascii=False)
        print(json.dumps(resumen,indent=1,ensure_ascii=False))
        return 0 if resumen['transicion_y_continuidad'] else 1
    if a.salida:
        p.error('--salida requiere --analizar')
    os.environ['DBZ3_AGENTE']='solIWX'
    os.environ['PYTHONUTF8']='1'
    out=HOME/f'bubbles_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA',out,flush=True)
    pantalla2()
    def env(_):
        return dict(DBZ3_TEST_IWMINI='0x7D1',DBZ3_TEST_GP2_BUBBLES='1',
            DBZ3_PS2_GAMES=FUENTE,DBZ3_TEST_TURBO='1',TEST_GP2_BB_CSV=str(out/'bubbles.csv'),
            TEST_GP2_BB_POSE=str(out/'dibujo.iwpb'))
    def jugar(j,_):
        j.log(r'gp2 bubbles prueba banco=true',150)
        j.log(r'gp2 bubbles LIVE47\+facial8 recursos=118/912/120 LUT=ELF intro=11D',10)
        j.log(r'estado 0x2D de IW',190)
        j.captura('a_bubbles_intro')
        j.log(r'entrada acabada: mando al jugador',190)
        P.dormir(.4)
        j.captura('a_bubbles_juego')
        if a.presentacion:
            return
        # "entrada acabada" inicia READY: aun no acepta caminar. La captura
        # original natural EE17796 entra0->7/D5 (lejos), no0->1/D3.
        j.log(r'gp2 bubbles READY terminado: mando y reloj liberados',15)
        j.log(r'gp2 bubbles tick=\d+ fase=UPDATE 0->7 codigo=D5',10)
        # EU31A940 crea la tarea4 cuando el jugador esta en reposo1. Correr
        # durante este intervalo cambia la entrada de la IA a3 y no lo prueba.
        j.log(r'gp2 bubbles tick=\d+ fase=UPDATE 0->3 codigo=DD',45)
        j.tecla('W 2500',2700)
        P.dormir(.4)
        j.captura('b_bubbles_juego')
    key='solIWX_bubbles_presentacion' if a.presentacion else 'solIWX_bubbles_juego'
    P.ESCENARIOS[key]=('Bubbles candidato: intro/READY y dibujo' if a.presentacion else
        'Bubbles candidato: juego real y CSV continuo con fuente EU',env,jugar)
    r=P.correr('smoke_b',key,[],None,{'timeout':'350'},True)
    # correr() ya cerro el proceso: no perder las filas escritas durante su cierre.
    try:
        resumen=analizar_csv(out/'bubbles.csv')
        resumen.update(run=str(r['run']),fuente=FUENTE,csv_cerrado=True,solo_presentacion=a.presentacion)
        if not a.presentacion and not resumen['transicion_y_continuidad']:
            raise ValueError('Sin transicion y continuidad posterior observadas')
        if not a.presentacion:
            with (out/'bubbles.csv').open(encoding='utf-8',newline='') as f:
                filas=list(csv.DictReader(f))
            if any(k not in filas[0] for k in ('jugadorx','jugadory','jugadorz','estado_jugador')):
                raise ValueError('Sin registro de la entrada del jugador')
            movido=max(math.dist([float(filas[0][k]) for k in ('jugadorx','jugadorz')],
                [float(r[k]) for k in ('jugadorx','jugadorz')]) for r in filas)
            resumen['desplazamiento_jugador_xz']=movido
            if movido<=4 or not any(r['estado_jugador']=='3' for r in filas):
                raise ValueError('No se observo correr del jugador tras READY')
        with (out/'recogida.json').open('x',encoding='utf-8') as f:
            json.dump(resumen,f,indent=1,ensure_ascii=False)
    except (OSError,ValueError,KeyError) as e:
        r['ok']=False
        r['msg']=(r['msg']+'; ' if r['msg'] else '')+str(e)
    with (out/'resultado.json').open('x',encoding='utf-8') as f:
        json.dump(r,f,default=str,indent=1)
    print(P.tabla([r]),flush=True)
    return 0 if r['ok'] else 1


if __name__=='__main__':
    raise SystemExit(main())
