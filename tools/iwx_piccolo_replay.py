"""Extrae una reaccion natural desde estados propios, autenticados por campo."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import zipfile

from iwx_piccolo import inventario, analizar


def extraer(captura,elf):
    inv=inventario(elf);resumen=analizar(captura,inv)
    filas=[json.loads(l) for l in captura.read_text(encoding='utf-8').splitlines()]
    indices=[i for i,f in enumerate(filas) if f['piccolo']['estado']==3]
    if not indices:raise ValueError('Captura sin reaccion natural')
    seleccion=set()
    for i in indices:seleccion.update(range(max(0,i-1),min(len(filas),i+2)))
    serie=[]
    for i in sorted(seleccion):
        f=filas[i];v=f['video'];raw=Path(v['ruta']).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=v['sha256']:raise ValueError('Estado alterado')
        with zipfile.ZipFile(io.BytesIO(raw)) as z:ram=z.read('eeMemory.bin')
        u=lambda a:struct.unpack_from('<I',ram,a)[0]
        fl=lambda a:struct.unpack_from('<f',ram,a)[0]
        actor=f['piccolo']['actor']
        visor=u(0x49CDF0);nivel=u(visor+0x28)
        if not 0<=nivel<6:raise ValueError('Dificultad del visor no valida')
        if u(0x4A4D40)!=f['n']:raise ValueError('Estado de otro campo')
        serie.append(dict(i=i,estado_sha256=v['sha256'],pose=f['piccolo'],
            azimut=fl(0x5D9940),inclinacion=fl(0x5D9944),
            expansion=fl(0x5D996C),fase=fl(0x5D9970),
            contador=fl(actor+0xCC8),cb4=fl(actor+0xCB4),cb8=fl(actor+0xCB8),
            salud=fl(visor+4),salud_maxima=fl(0x5D98B8+nivel*16),
            posicion=[fl(actor+0x40+k*4) for k in range(3)]))
    return dict(elf=inv,resumen=resumen,serie=serie,paridad_integral=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--captura',required=True,type=Path)
    p.add_argument('--elf',required=True,type=Path)
    p.add_argument('--salida',required=True,type=Path)
    a=p.parse_args();d=extraer(a.captura,a.elf)
    with a.salida.open('x',encoding='utf-8') as f:json.dump(d,f,indent=1,allow_nan=False)
    print(json.dumps([dict(i=r['i'],estado=r['pose']['estado'],codigo=hex(r['pose']['codigo']),
        cuadro=r['pose']['cuadro'],duracion=r['pose']['duracion'],fin=r['pose']['fin'],
        azimut=r['azimut'],inclinacion=r['inclinacion'],expansion=r['expansion'],fase=r['fase'],
        contador=r['contador'],cb4=r['cb4'],cb8=r['cb8']) for r in d['serie']],indent=1))


if __name__=='__main__':main()
