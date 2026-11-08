"""Cotejo del color de impacto: tabla y consumidores EU; no simula el rasterizador."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from iwx_lava import Original


def inventario(path):
    elf=Original(path)
    tabla=elf.bytes(0x480C54,24)
    funciones=[]
    for inicio,fin in ((0x32B2A0,0x32B490),(0x2F2D70,0x2F2DF0),(0x2F4450,0x2F4840)):
        raw=elf.bytes(inicio,fin-inicio)
        instrucciones=[]
        for off in range(0,len(raw),4):
            w,=struct.unpack_from('<I',raw,off)
            op,rs,rt,im=w>>26,(w>>21)&31,(w>>16)&31,w&65535
            if (op in (0x24,0x31,0x39,0x2B) and 0x18F4<=im<=0x1902) or inicio==0x2F2D70:
                instrucciones.append(dict(pc=hex(inicio+off),word=f'{w:08X}',op=op,rs=rs,rt=rt,imm=im))
        funciones.append(dict(inicio=hex(inicio),fin_exclusivo=hex(fin),sha256=hashlib.sha256(raw).hexdigest(),instrucciones=instrucciones))
    return dict(elf_sha256=hashlib.sha256(elf.elf).hexdigest(),tabla_direccion='0x480C54',
        tabla_hex=tabla.hex(),tabla_u32=list(struct.unpack('<6I',tabla)),
        tabla_f32=list(struct.unpack('<6f',tabla)),color_bytes=list(tabla[16:20]),
        color_be=int.from_bytes(tabla[16:20],'big'),funciones=funciones,
        paridad_visual=False)


def probar_cpp(elf,inventario,salida):
    raiz=Path(__file__).resolve().parents[1]
    # Consumidores originales y B3 leen bytes consecutivos, no el entero
    # empaquetado. Exigir las seis cargas antes de aceptar la conversion.
    for offset in (0x18FC,0x18FD,0x18FE,0x1900,0x1901,0x1902):
        assert any(x['op']==0x24 and x['rs']==20 and x['imm']==offset
            for x in inventario['funciones'][2]['instrucciones']),hex(offset)
    texto=(raiz/'generated/dbz3_recomp.9.cpp').read_text(encoding='utf-8')
    cuerpo=texto.split('DEFINE_REX_FUNC(sub_82109D90) {',1)[1]
    cuerpo=cuerpo.split('DEFINE_REX_FUNC(',1)[0]
    for offset in (6268,6269,6270,6272,6273,6274):
        assert f'REX_LOAD_U8(ctx.r30.u32 + {offset})' in cuerpo,offset
    exe=salida.with_suffix('.exe')
    subprocess.run(['C:/Program Files/LLVM/bin/clang++.exe','-std=c++17',
        str(raiz/'tools/iwx_chr_fade_cpp.cpp'),'-o',str(exe)],check=True)
    colores=[0,0xFFFFFFFF,0x003C90DC,0x12345678]
    colores += [n << (8*canal) for canal in range(4) for n in range(256)]
    esperado=[int.from_bytes(struct.pack('<I',c),'big') for c in colores]
    r=subprocess.run([str(exe)],input=''.join(f'{c:08X}\n' for c in colores),
        text=True,capture_output=True,check=True)
    actual=[int(x,16) for x in r.stdout.splitlines()]
    assert len(actual)==len(esperado),'Resultado C++ incompleto'
    fallos=[dict(ps2=f'{c:08X}',esperado=f'{e:08X}',actual=f'{p:08X}')
        for c,e,p in zip(colores,esperado,actual) if e!=p]
    assert not fallos,fallos
    impacto=int.from_bytes(Original(elf).bytes(0x480C64,4),'big')
    assert impacto==inventario['color_be']==0xDC903C00
    return dict(casos=len(colores),fallos=fallos,color_b3=f'{impacto:08X}',
        consumidores_ps2=True,consumidor_b3=True,
        fuente_b3_sha256=hashlib.sha256(texto.encode('utf-8')).hexdigest(),
        limite='Conversion y orden de bytes; no mide intensidad, duracion, reaccion o imagen final.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf',type=Path,required=True)
    p.add_argument('--salida',type=Path,required=True)
    p.add_argument('--probar-cpp',action='store_true')
    a=p.parse_args()
    datos=inventario(a.elf)
    if a.probar_cpp:datos['prueba_cpp']=probar_cpp(a.elf,datos,a.salida)
    with a.salida.open('x',encoding='utf-8') as f:json.dump(datos,f,indent=1,allow_nan=False)
    print(json.dumps(dict(salida=str(a.salida),color=hex(datos['color_be']),
        prueba_cpp=datos.get('prueba_cpp')),indent=1,allow_nan=False))


if __name__=='__main__':main()
