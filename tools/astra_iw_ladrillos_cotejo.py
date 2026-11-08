"""Original/PAL y port con dtPAL de prueba: mismo tramo y mismo contador."""
import argparse
import json
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('original',type=Path);p.add_argument('port',type=Path);p.add_argument('--mensaje',action='store_true');a=p.parse_args()
    o=json.loads((a.original/'medida.json').read_text(encoding='utf-8'))['filas']
    filas=[list(map(float,l.split(','))) for l in (a.port/'visor.csv').read_text().splitlines()]
    por_t={int(f[0]):f for f in filas}
    if a.mensaje:
        anterior=None;cambios=[]
        for i,f in enumerate(o):
            firma=(f['callback'],f['mensaje']['callback'],f['mensaje']['cuenta'])
            if firma!=anterior:cambios.append(dict(campo=i,n=f['n'],estado=firma));anterior=firma
        activos=[f for f in filas if int(f[11])>=0]
        r=dict(original=cambios,port_campos_activos=len(activos),port_ultimo_t=activos[-1][12] if activos else None)
        voz=[f for f in filas if len(f)>15 and f[15] and f[13]>0]
        if len(voz)>1:
            x,y=voz[0],voz[-1]
            r['audio_medido']=dict(muestras=y[13]-x[13],segundos_host=y[14]-x[14],
                                  hz_consumidos=(y[13]-x[13])/(y[14]-x[14]))
        print(json.dumps(r,indent=2));(a.port/'mensaje_original.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
        return
    campos=[('progreso','0x508',1),('blanco_x','0x4f4',2),('blanco_y','0x4f8',3),('profundidad','0x528',4),
            ('mira_x','0x4ec',5),('mira_y','0x4f0',6),('iman','0x550',8)]
    errores={n:0.0 for n,_,_ in campos};fijacion=0
    for i,d in enumerate(o,1):
        f=por_t[i]
        for nombre,clave,k in campos:errores[nombre]=max(errores[nombre],abs(d['floats'][clave]-f[k]))
        fijacion+=int(d['enteros']['0x4e4']!=int(f[7]))
    resultado=dict(campos=len(o),errores_max=errores,fijacion_distinta=fijacion,
                   nota='dt1,2 solicitado solo al arnes; partida normal sigue60Hz/dt1')
    (a.port/'cotejo_original.json').write_text(json.dumps(resultado,indent=2),encoding='utf-8')
    print(json.dumps(resultado,indent=2))
    assert not fijacion and max(errores.values())<.001,'divergencia original/port'

if __name__=='__main__':main()
