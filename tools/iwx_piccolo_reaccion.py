"""Tabla ELF, giro de una reaccion natural y guardas de la cola del motor."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from iwx_lava import Original
from iwx_piccolo_replay import extraer


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf',type=Path,required=True)
    p.add_argument('--captura',type=Path,required=True)
    p.add_argument('--salida',type=Path,required=True)
    a=p.parse_args();a.salida.mkdir(exist_ok=False)
    raiz=Path(__file__).resolve().parents[1]
    nativo=Original(a.elf);original=extraer(a.captura,a.elf)
    (a.salida/'original.json').write_text(json.dumps(original,indent=1,allow_nan=False),encoding='utf-8')
    pares=[(x,y) for x,y in zip(original['serie'],original['serie'][1:])
        if x['pose']['estado']==y['pose']['estado']==3 and y['i']==x['i']+1]
    if not pares:raise ValueError('Sin giro natural consecutivo')
    entrada=''.join(f"{x['pose']['tabla_indice']} {x['salud']} {x['salud_maxima']} "
        f"{x['azimut']} {x['inclinacion']} {y['contador']} {y['pose']['fin']}\n" for x,y in pares)
    exe=a.salida/'reaccion.exe'
    subprocess.run(['C:/Program Files/LLVM/bin/clang++.exe','-std=c++17','-ffp-contract=off',
        str(raiz/'tools/iwx_piccolo_reaccion_cpp.cpp'),'-o',str(exe)],check=True)
    r=subprocess.run([str(exe)],input=entrada,text=True,capture_output=True,check=True)
    (a.salida/'cpp.jsonl').write_text(r.stdout,encoding='utf-8')
    filas=[json.loads(l) for l in r.stdout.splitlines()]
    assert filas[0]['tabla']==nativo.bytes(0x499A60,22*16).hex(),'Tabla distinta del ELF'
    assert filas[0]['giros']==nativo.bytes(0x499BC0,48).hex(),'Giros distintos del ELF'
    assert filas[1]['fallos']==0
    assert len(filas[2:])==len(pares)
    errores=[]
    for (_,y),actual in zip(pares,filas[2:]):
        assert actual['error']==0 and actual['estado']==y['pose']['estado']
        errores.append(max(abs(actual[k]-y[k]) for k in ('azimut','inclinacion')))
    assert max(errores)<1e-8,errores
    medida=dict(tabla_22_y_giros_6_byteidenticos=True,guardas=filas[1],
        pares_naturales=len(pares),error_maximo=max(errores),
        impacto_original=original['resumen']['impactos'],
        elf_sha256=hashlib.sha256(nativo.elf).hexdigest(),
        normal=False,paridad_poses=False,paridad_scheduler=False,paridad_integral=False,
        limite='Giro natural observado, tablas y guardas; PAL avanza1.2 de animacion por campo. No equipara PAL50 con B360 por ordinal.')
    (a.salida/'medida.json').write_text(json.dumps(medida,indent=1,allow_nan=False),encoding='utf-8')
    print(json.dumps(medida,indent=1))


if __name__=='__main__':main()
