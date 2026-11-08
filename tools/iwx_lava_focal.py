"""Una partida Namek: contacto registrado sin forzar posicion, suelo o vida."""
import argparse
from collections import Counter
import csv
import json
import math
from pathlib import Path
import os
import time

import pruebas as P
from iwx_bubbles_focal import FUENTE, HOME, pantalla2
from iwx_lava import Original


def analizar(path,elf):
    original=Original(elf)
    with path.open(encoding='utf-8',newline='') as f:filas=list(csv.DictReader(f))
    if not filas or any(None in r or any(v is None for v in r.values()) for r in filas):
        raise ValueError('CSV vacio o incompleto')
    if any(not math.isfinite(float(v)) for r in filas for v in r.values()):
        raise ValueError('Valor no finito en CSV')
    if not all(int(b['tick'])==int(a['tick'])+1 for a,b in zip(filas,filas[1:])):
        raise ValueError('Registro no consecutivo')
    fallos=[]
    for row in filas:
        c={k:int(row[k]) for k in ('estado','pies','cuerpo','aire')}
        c.update(y=float(row['y']),suelo=float(row['suelo']))
        esperado=(original.ejecutar(0x2A5F40,c),original.ejecutar(0x2A5DE0,c))
        actual=(int(row['tres']),int(row['cuatro']))
        if esperado!=actual:fallos.append(dict(tick=row['tick'],original=esperado,port=actual))
    lava=[r for r in filas if int(r['pies'])==3]
    aceptadas=[r for r in lava if int(r['tres'])]
    separadas=[r for r in lava if abs(float(r['y'])-float(r['suelo']))>.01001]
    diferencias=[r for r in filas if r['tres']!=r['anterior']]
    vidas=[float(r['vida']) for r in filas if r.get('vida') not in (None,'')]
    caidas=[round(a-b,3) for a,b in zip(vidas,vidas[1:]) if b < a-0.001]
    estados=[int(r['estado']) for r in filas]
    i2a=estados.index(0x2A) if 0x2A in estados else -1
    i2b=estados.index(0x2B) if 0x2B in estados else -1
    secuencia_muerte = i2a>=0 and i2b>i2a
    medida=dict(filas=len(filas),consecutivos=True,tipos=dict(Counter(r['pies'] for r in filas)),
        lava=len(lava),aceptadas=len(aceptadas),separadas=len(separadas),
        decisiones_distintas_anterior=len(diferencias),ejemplos=diferencias[:8],fallos=fallos,
        vida_inicial=vidas[0] if vidas else None,vida_min=min(vidas) if vidas else None,
        caidas=caidas,estados_2A=estados.count(0x2A),estados_2B=estados.count(0x2B),
        secuencia_muerte=secuencia_muerte,
        paridad_colision=False,paridad_trayectoria=False,paridad_integral=False,
        limite='Predicados y vida sobre entradas registradas del port; la muerte 2A/2B se observa, '
               'no se sincroniza con una partida PS2.')
    if fallos:raise ValueError(f'{len(fallos)} decisiones contradicen el ELF')
    if not lava or not aceptadas:raise ValueError('El recorrido no ejercito contacto aceptado con lava3')
    return medida


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf',type=Path,required=True)
    ap.add_argument('--esperar',type=float,default=0,help='segundos quieto tras el recorrido (para la muerte)')
    a=ap.parse_args();os.environ['DBZ3_AGENTE']='tIW'
    out=HOME/f'lava_focal_{time.time_ns()}';out.mkdir()
    print('EVIDENCIA',out,flush=True);pantalla2()
    def env(_):
        return dict(DBZ3_TEST_IWMINI='0x7D9',DBZ3_PS2_GAMES=FUENTE,TEST_IWX_LAVA=str(out/'lava.csv'))
    def jugar(j,_):
        j.log(r'entrada acabada: mando al jugador',190)
        P.dormir(.3)
        j.captura('namek_ready')
        csvp=out/'lava.csv'
        def toca():
            try:
                with csvp.open(encoding='utf-8',errors='replace') as f:
                    return any(r.get('pies')=='3' for r in csv.DictReader(f))
            except OSError:
                return False
        # Dirigido por la senal de contacto del propio juego: avanza a tramos
        # cortos y para en cuanto el registro dice pies==3 (lava). Si no sale,
        # prueba un lateral para rodear el obstaculo.
        toco=False
        for i in range(16):
            j.tecla('W 450',520)
            if toca():
                toco=True; break
        if not toco:
            j.tecla('D 1200',1400)
            for i in range(12):
                j.tecla('W 450',520)
                if toca():
                    toco=True; break
        j.captura('namek_contacto')
        if a.esperar>0:
            P.dormir(a.esperar)
        P.dormir(.5)
        j.captura('namek_final')
    key='solIWX_lava_contacto'
    P.ESCENARIOS[key]=('IW: contacto real con lava de Namek',env,jugar)
    resultado=P.correr('smoke_b',key,[],None,{'timeout':'350'},True)
    medida={}
    try:medida=analizar(out/'lava.csv',a.elf)
    except (OSError,ValueError,KeyError) as exc:
        resultado['ok']=False;resultado['msg']=(resultado['msg']+'; ' if resultado['msg'] else '')+str(exc)
    for nombre,datos in (('medida.json',medida),('resultado.json',resultado)):
        with (out/nombre).open('x',encoding='utf-8') as f:json.dump(datos,f,default=str,indent=1,ensure_ascii=False,allow_nan=False)
    if resultado.get('run'):
        texto=(Path(resultado['run'])/'dbz3.log').read_text(encoding='utf-8',errors='replace')
        medida['muerte_log_2A']=texto.count('estado muerte iw combate-2A')
        medida['muerte_log_2B']=texto.count('estado muerte iw combate-2B')
        i2a=texto.find('estado muerte iw combate-2A');i2b=texto.find('estado muerte iw combate-2B')
        medida['secuencia_muerte']=i2a>=0 and i2b>i2a
        with (out/'medida.json').open('w',encoding='utf-8') as f:json.dump(medida,f,default=str,indent=1,ensure_ascii=False,allow_nan=False)
    print(P.tabla([resultado]),flush=True);print(json.dumps(medida,indent=1),flush=True)
    return 0 if resultado['ok'] else 1


if __name__=='__main__':raise SystemExit(main())
