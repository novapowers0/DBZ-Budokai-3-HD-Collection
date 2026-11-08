"""Una partida: frenada con ancla corporal -> carga con pies, sin forzar RAM."""
import csv
import json
import os
import time
import pruebas as P
from iwx_bubbles_focal import FUENTE, HOME, pantalla2
from iwx_seguimiento import analizar_port


def analizar(path):
    with path.open(encoding='utf-8',newline='') as f:
        filas=list(csv.DictReader(f))
    cargas=[r for r in filas if int(r['estado'])==5]
    finales=[r for r in filas if int(r['estado'])==6]
    if len(cargas)<10 or not finales:
        raise ValueError('No se registraron carga y cierre naturales')
    if any(int(r['raiz']) for r in cargas+finales):
        raise ValueError('Carga/cierre conservan indebidamente el ancla corporal')
    transiciones=[dict(tick=int(b['tick']),antes=int(a['estado']),despues=int(b['estado']),
        raiz_antes=int(a['raiz']),raiz_despues=int(b['raiz']))
        for a,b in zip(filas,filas[1:]) if a['estado']!=b['estado']]
    if not any(t['antes']==4 and t['despues']==5 and t['raiz_antes']==1 and t['raiz_despues']==0
               for t in transiciones):
        raise ValueError('Falta transicion directa frenada/ancla -> carga/pies')
    return dict(carga=len(cargas),cierre=len(finales),transiciones=transiciones,
        filtro=analizar_port(path,.12),paridad_global=False,paridad_trayectoria=False)


def main():
    os.environ['DBZ3_AGENTE']='solIWX'
    out=HOME/f'estados_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA',out,flush=True)
    pantalla2()
    def env(_):
        return dict(DBZ3_TEST_IWMINI='0x7D0',DBZ3_PS2_GAMES=FUENTE,
            TEST_IWX_SEGUIMIENTO=str(out/'seguimiento.csv'),TEST_IWX_SEGUIMIENTO_PREVIO='1')
    def jugar(j,_):
        j.log(r'entrada acabada: mando al jugador',190)
        P.dormir(3)
        # Gastar energia jugando y dejar terminar el dash antes de la marcha.
        j.tecla('Backspace 100',300)
        j.log(r'iwmini: scatto \(energia ',10)
        P.dormir(1.5)
        j.tecla('W 1100',1100)
        # La frenada dura16 cuadros; cuadrado durante ella ejercita el cambio.
        # tecla() espera un tiempo total, no un margen desde soltar: el piloto
        # anterior sumaba280ms y ya habia pasado un cuadro de reposo.
        j.tecla('L 3000',3200)
        P.dormir(.5)
    key='solIWX_estados_freno_carga'
    P.ESCENARIOS[key]=('IW: cambio real de ancla al cargar tras frenada',env,jugar)
    resultado=P.correr('smoke_b',key,[],None,{'timeout':'350'},True)
    medida={}
    try:
        medida=analizar(out/'seguimiento.csv')
    except (OSError,ValueError,KeyError) as exc:
        resultado['ok']=False
        resultado['msg']=(resultado['msg']+'; ' if resultado['msg'] else '')+str(exc)
    for nombre,datos in (('medida.json',medida),('resultado.json',resultado)):
        with (out/nombre).open('x',encoding='utf-8') as f:
            json.dump(datos,f,default=str,indent=1,ensure_ascii=False,allow_nan=False)
    print(P.tabla([resultado]),flush=True)
    print(json.dumps(medida,indent=1),flush=True)
    return 0 if resultado['ok'] else 1


if __name__=='__main__':
    raise SystemExit(main())
