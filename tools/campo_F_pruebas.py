"""Campo F: pruebas offline de proyeccion/sombra; medida visual intacta."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
from campo_F_investigar import Elf


def funcion(texto,marca):
    p=texto.index(marca);ini=texto.index('{',p);n=1;i=ini+1
    while n:
        if texto[i]=='{':n+=1
        elif texto[i]=='}':n-=1
        i+=1
    return texto[p:i]


def direccion_getter(imagen,pc):
    r=[0]*32
    for _ in range(4):
        w=struct.unpack_from('>I',imagen,pc-0x82000000)[0];op=w>>26;t=w>>21&31;s=w>>16&31
        imm=w&65535;imm=imm if imm<32768 else imm-65536
        if op==15:r[t]=(imm<<16)&0xffffffff
        elif op==14:r[t]=(r[s]+imm)&0xffffffff
        elif w==0x4e800020:return r[3]
        else:raise AssertionError(hex(w))
        pc+=4
    raise AssertionError('getter sin retorno')


def sombra_original(elf):
    # Ejecutar2636DC..263700: la rama default REAL, sin reinterpretar el decomp.
    regs=[0]*32;mem={}
    for pc in range(0x2636dc,0x263704,4):
        w=elf.u32(pc);op=w>>26;s=w>>21&31;t=w>>16&31;k=w&65535;imm=k if k<32768 else k-65536
        if op==15:regs[t]=(k<<16)&0xffffffff
        elif op==0x2b:mem[(regs[s]+imm)&0xffffffff]=regs[t]
        else:raise AssertionError((hex(pc),hex(w)))
    return [mem[0x458e90+4*i] for i in range(4)]


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF/campo'))
    ap.add_argument('--elf',type=Path,default=Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47'))
    a=ap.parse_args();assert a.tmp.is_dir();root=Path(__file__).resolve().parents[1];comandos=[]
    def run(cmd,nombre):
        p=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,cwd=root)
        (a.tmp/(nombre+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
        comandos.append(dict(comando=list(map(str,cmd)),exit=p.returncode));print(nombre,p.returncode,flush=True)
        if p.returncode:print(p.stdout+p.stderr)
        assert p.returncode==0,nombre
        return p.stdout
    # Instrucciones originales: parametro480 en298174 y division en126F98.
    e=Elf(a.elf);w=e.u32(0x298174);assert w>>26==15
    base=struct.unpack('<f',struct.pack('<I',(w&65535)<<16))[0];assert base==480
    assert e.u32(0x126f98)==0x4600a003 # div.s f0,f20,f0
    assert e.u32(0x2f666c)==0x0c0477be and e.u32(0x2f6670)==0x24060068 # memset(context,0,68h)
    screen=e.bytes(0x420300,48);width,height=struct.unpack_from('<HH',screen,10)
    assert (width,height)==(600,450)
    abajo=sombra_original(e);assert abajo==[0,0xbf800000,0,0x3f800000]
    getter={}
    for nombre,pc in [('us',0x82139760),('eu',0x82139868)]:
        raw=Path(f'D:/DBZ3HD/work/eu/{nombre}_img.bin').read_bytes()
        getter[nombre]=direccion_getter(raw,pc)
    assert getter['us']==getter['eu']==0x8234d3d0
    resultados={}
    for etapa in ('antes','despues'):
        texto=(a.tmp/'iw_minijuegos.cpp.antes' if etapa=='antes' else root/'src/iw_minijuegos.cpp').read_text(encoding='utf-8')
        core='namespace dbz3::iwmini {\n'+funcion(texto,'static void FovIW(')+'\n'+funcion(texto,'bool CamaraIw(')+'\n}\n'
        core+=funcion(texto,'DBZ3_HOOK(sub_82139578,dbz3eu_sub_82139680)')+'\n'
        inc=a.tmp/f'campo_{etapa}_core.h';inc.write_text(core,encoding='utf-8')
        exe=a.tmp/f'campo_{etapa}.exe'
        cmd=['C:/Program Files/LLVM/bin/clang++.exe','-std=c++23','-O1','-Wall','-Wextra',
             '-fsanitize=undefined','-fsanitize-trap=all',
             '-DCAMPO_F_CORE="'+str(inc.resolve()).replace('\\','/')+'"',
             root/'tests/campo_F_prueba.cpp','-o',exe]
        if etapa=='antes':cmd.insert(1,'-DCAMPO_F_ANTES')
        run(cmd,etapa+'_compilar');log=run([exe],etapa+'_prueba')
        casos=[];sombras={};clip=None
        for line in log.splitlines():
            p=line.split()
            if p[0]=='FOV':
                deg,fov=int(p[1]),float(p[2]);original=base/(2*math.tan(math.radians(deg)/2))
                port=height/(2*math.tan(fov/2));error=abs(port-original)/original
                if etapa=='despues':assert error<3e-7,(deg,fov,error)
                casos.append(dict(grados=deg,vertical=fov,focal_original=original,focal_port=port,error_relativo=error))
            elif p[0]=='SOMBRA':
                bits=list(map(int,p[2:]));sombras[p[1]]=bits
                if etapa=='despues':assert bits==abajo
                else:assert bits!=abajo
            elif p[0]=='CLIP':clip=float(p[1])
        if etapa=='despues':assert abs(height/(2*math.tan(clip/2))-base/(2*math.tan(math.radians(40)/2)))<.0001
        resultados[etapa]=dict(casos_fov=casos,sombras=sombras,clip=clip)
    # Reproducir SOLO la medida ANTERIOR con sus imagenes intactas, sin recorte,
    # registro ni imagen sintetica que pueda hacerse pasar por una corrida nueva.
    from PIL import Image
    import numpy as np
    from skimage.metrics import structural_similarity
    import comparar
    meta=json.loads(Path('D:/tmp/solIW9c/iw9_visual_cierre9e_0x7D0.json').read_text(encoding='utf-8'))
    runpath=Path(meta['run']);medida=json.loads((runpath/'medidas9.json').read_text(encoding='utf-8'))
    org=Path(medida['imagenes'][0]['original']);port=Path(medida['imagenes'][0]['port'])
    aa=Image.open(org).convert('RGB').resize((320,240));bb=Image.open(port).convert('RGB').resize((320,240))
    ssim=float(structural_similarity(np.asarray(aa.convert('L')),np.asarray(bb.convert('L')),data_range=255))
    ph=comparar.hamming(comparar.phash(aa),comparar.phash(bb))
    assert abs(ssim-medida['imagenes'][0]['ssim'])<1e-12 and ph==22
    result=dict(comandos=comandos,elf_sha256=hashlib.sha256(e.b).hexdigest(),lienzo=[width,height],base_proyeccion=base,
                getters_sombra=getter,direccion_original=abajo,resultados=resultados,
                fuentes={n:hashlib.sha256((root/'src'/n).read_bytes()).hexdigest() for n in ('iw_minijuegos.cpp','iw_minijuegos.h')},
                medida_anterior=dict(ssim=ssim,phash=ph,umbral_ssim=.45,umbral_phash=18,
                                     original=str(org),port=str(port),verde=ssim>=.45 and ph<=18),
                medida_despues=None,limites=['Sin build/juego; el efecto en SSIM/phash queda para la corrida agrupada',
                                            'Sombra vertical original default; no se altera la luz del personaje ni la geometria',
                                            'La prueba de gancho simula RAM/consumidor; no certifica por si sola salida GPU'])
    (a.tmp/'campo_F_verificado.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('PASS offline: 81 FOV+clips, 12 contextos sombra US/EU y restauracion; ANTES',ssim,ph,'FAIL intacto; DESPUES pendiente')


if __name__=='__main__':main()
