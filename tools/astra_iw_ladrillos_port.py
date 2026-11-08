"""SHOOT sin piloto: reproducir el tramo42 medido en PCSX2 y guardar el reloj lógico."""
import json
import argparse
import os
from pathlib import Path
import time
import pruebas as P

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tramo',type=int,default=42);ap.add_argument('--pal',action='store_true');ap.add_argument('--aviso',type=int,default=-1);ap.add_argument('--sin-foto',action='store_true');ap.add_argument('--copia',default='opus2');args=ap.parse_args()
    os.environ['DBZ3_AGENTE']='astraIW'
    out=Path('D:/tmp/astraIW')/(time.strftime('%Y%m%d-%H%M%S')+'_port_ladrillos');out.mkdir()
    traza=out/'visor.csv'
    def env(_):
        e=dict(DBZ3_TEST_IWMINI='0x7D3',DBZ3_TEST_IWMINI_DIFICULTAD='2',
                    DBZ3_PS2_GAMES='D:/DBZ3HD/ps2_games',DBZ3_TEST_LADRILLOS_TRAMO=str(args.tramo),
                    DBZ3_TEST_LADRILLOS_TRAZA=str(traza))
        if args.pal:e['DBZ3_TEST_LADRILLOS_PAL']='1'
        if args.aviso>=0:e['DBZ3_TEST_LADRILLOS_AVISO']=str(args.aviso)
        return e
    def jugar(j,_):
        j.log('iwdisparo: juego 0x7D3 empieza',120)
        fin=time.monotonic()+25
        while time.monotonic()<fin:
            lineas=traza.read_text().splitlines() if traza.exists() else []
            if len(lineas)>=42:break
            time.sleep(.01)
        else:raise RuntimeError('sin42cuadros de visor')
        from PIL import Image
        n0=len(lineas)
        if not args.sin_foto:
            foto=j.captura('visor')
            with Image.open(foto) as im:im.resize((im.width//2,im.height//2)).save(out/'visor.png')
        filas=[list(map(float,l.split(','))) for l in traza.read_text().splitlines()]
        assert filas[0][0]==1,'no se capturo el primer cuadro'
        if args.aviso>=0:
            assert any(int(f[11])==args.aviso for f in filas),'sin aviso'
            j.log('aviso Kaio terminado',20)
            time.sleep(1.5)
            filas=[list(map(float,l.split(','))) for l in traza.read_text().splitlines()]
        elif args.tramo==42:assert any(f[7] for f in filas),'no se adquirio fijacion'
        (out/'medida.json').write_text(json.dumps(dict(run=str(j.run),antes=n0,despues=len(filas),filas=filas),indent=2),encoding='utf-8')
    P.ESCENARIOS['astraIW_ladrillos']=('SHOOT: trayectoria y magnetismo originales',env,jugar)
    r=P.correr(args.copia,'astraIW_ladrillos',[],None,{'timeout':'220'},True)
    print(P.tabla([r]),out,flush=True)
    if not r['ok']:raise SystemExit(1)

if __name__=='__main__':main()
