"""Una focal del inicio de voz BBR: recursos, fase y solicitud unica."""
import argparse
import json
import os
from pathlib import Path
import re
import time

import pruebas as P
from iwx_bubbles_focal import FUENTE, HOME, pantalla2


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revisar',type=Path,help='Revisar el log cerrado sin lanzar otra partida')
    args=parser.parse_args()
    os.environ['DBZ3_AGENTE']='solIWX'
    os.environ['PYTHONUTF8']='1'
    out=HOME/f'bubbles_audio_focal_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA',out,flush=True)
    def env(_):
        return dict(DBZ3_TEST_IWMINI='0x7D1',DBZ3_TEST_GP2_BUBBLES='1',
            DBZ3_PS2_GAMES=FUENTE,DBZ3_TEST_TURBO='1')
    def jugar(j,_):
        j.log(r'gp2 bubbles prueba banco=true',150)
        j.log(r'gp2 bubbles voz_inicio fase=1 codigo=11D cuadro=1\.2 ',190)
        j.log(r'gp2 bubbles READY terminado: mando y reloj liberados',190)
    key='solIWX_bubbles_audio'
    P.ESCENARIOS[key]=('Bubbles voz inicial: solicitud unica tras entrar en 11D',env,jugar)
    if args.revisar:
        r=dict(run=str(args.revisar),ok=True,msg='',revision_offline=True)
    else:
        pantalla2()
        r=P.correr('smoke_b',key,[],None,{'timeout':'350'},True)
    try:
        texto=(Path(r['run'])/'dbz3.log').read_text(encoding='utf-8',errors='replace')
        lineas=[x for x in texto.splitlines() if 'gp2 bubbles voz' in x]
        inicios=[x for x in lineas if 'voz_inicio' in x]
        if len(inicios)!=1:
            raise ValueError(f'Se esperaba una solicitud de voz, hay {len(inicios)}')
        m=re.search(r'fase=1 codigo=11D cuadro=1\.2 perfil=16 linea=0 entrada=927 idioma=(USA|JPN) hz=24000 muestras=(\d+) boca=10048 handle=(\d+) normal=false',inicios[0])
        if not m or int(m[3])==0:
            raise ValueError('Solicitud distinta del contrato original o handle nulo')
        if int(m[2])!={'USA':34328,'JPN':45209}[m[1]]:
            raise ValueError('PCM no coincide con el recurso original decodificado')
        datos=dict(run=str(r['run']),fuente=FUENTE,solicitudes=len(inicios),
            idioma=m[1],muestras=int(m[2]),handle=int(m[3]),lineas=lineas,
            paridad_global=False,normal_activado=False,
            limite='Acredita solicitud y PCM cargado; no captura salida del dispositivo ni certifica ganancia, reverb o sincronismo audible.')
        with (out/'audio.json').open('x',encoding='utf-8') as f:
            json.dump(datos,f,indent=1,ensure_ascii=False)
    except (OSError,ValueError,KeyError) as e:
        r['ok']=False
        r['msg']=(r['msg']+'; ' if r['msg'] else '')+str(e)
    with (out/'resultado.json').open('x',encoding='utf-8') as f:
        json.dump(r,f,default=str,indent=1)
    print(json.dumps(r,default=str,ensure_ascii=False) if args.revisar else P.tabla([r]),flush=True)
    return 0 if r['ok'] else 1


if __name__=='__main__':
    raise SystemExit(main())
