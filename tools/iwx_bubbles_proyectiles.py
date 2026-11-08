"""Contrasta 31A2F0 antes del dibujo contra un candidato C++ sin integrar.

Ejecuta saltos, prioridad caducidad/contacto/pausa, movimiento y giro del ELF.
Distancia2 y servicios del gestor son entradas interceptadas; no prueba anclas,
wrap final, renderer, contacto real, audio ni toda la aritmetica de PS2.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
from types import SimpleNamespace
from iwx_bubbles_eventos import cargar_elf, memoria_elf, llamadas, ELF_SHA

sys.dont_write_bytecode=True
INICIO, LIMITE_DIBUJO, FIN=0x31A2F0,0x31A4E4,0x31A570


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf',type=Path,required=True)
    ap.add_argument('--oraculo',type=Path,required=True)
    ap.add_argument('--salida',type=Path,required=True)
    ap.add_argument('--cpp',type=Path,required=True)
    args=ap.parse_args()
    data=cargar_elf(args.elf)
    spec=importlib.util.spec_from_file_location('iwx_proyectil_ee',args.oraculo)
    oracle=importlib.util.module_from_spec(spec);spec.loader.exec_module(oracle)
    obj,work,mini,mgr,ref=0x800000,0x810000,0x820000,0x830000,0x840000
    servicios={0x29A5D0,0x28A9D0,0x140460,0x142470}

    class ProyectilEE(oracle.EE):
        def __init__(self,distancia):
            super().__init__(SimpleNamespace(elf=data))
            self.distancia=distancia;self.eliminados=[];self.dibuja=False

        def step(self,pc,in_delay=False):
            if pc==LIMITE_DIBUJO:
                self.dibuja=True
                return 0x1FFFFF0,False,False
            # MULT EE con destino rd: 31A374 calcula slot*0x20D0.
            # Extension local; no modificar el interprete compartido GP2.
            w=self.get(pc)
            if pc==0x31A374 and w==0x00621018:
                rs,rt,rd=(w>>21)&31,(w>>16)&31,(w>>11)&31
                producto=oracle.S(self.r[rs])*oracle.S(self.r[rt])
                if rd:self.r[rd]=producto&0xFFFFFFFF
                self.r[0]=0
                return None,False,False
            result=super().step(pc,in_delay)
            if w>>26==3 and ((w&0x3FFFFFF)<<2) in (0x142520,0x1424A0):
                # Ejecutar estos helpers del ELF: el hook generico escala W
                # y suma Z/X/Y; estas funciones conservan W y suman X/Y/Z.
                return result[0],result[1],False
            if w>>26==3 and ((w&0x3FFFFFF)<<2) in servicios:
                return result[0],result[1],True
            return result

        def hook(self,addr):
            if addr==0x29A5D0:self.r[2]=work-0x260
            elif addr==0x28A9D0:self.r[2]=mini
            elif addr==0x140460:self.eliminados.append(self.r[4])
            elif addr==0x142470:self.f[0]=oracle.U(self.distancia)
            else:super().hook(addr)

    args.salida.mkdir(exist_ok=False)
    cases=[];raw=bytearray(b'BBOBJ001'+bytes(4))
    # Limites vecinos float32: NO tolerancia ampliada en el contraste.
    distancias=[oracle.V(oracle.U(80.)-1),80.,oracle.V(oracle.U(80.)+1)]
    relojes=[oracle.V(oracle.U(100.)-1),100.,oracle.V(oracle.U(100.)+1)]
    velocidades=[[0.,0.,0.,0.],[1.,0.,0.,1.],
                 [oracle.F(1/.9),0.,0.,1.],[1.12,0.,0.,1.],
                 [3.,-2.,4.,.25]]
    for tipo in range(6):
        for pausa in (0,1):
            for distancia in distancias:
                for tiempo in relojes:
                    for velocidad in velocidades:
                        ee=ProyectilEE(distancia);ee.r[4]=obj
                        ee.put(0x49CA70,mgr);ee.put(mgr+0x20D0+0x1B84,ref-0x6B0)
                        ee.put(mgr+0x8B7C,pausa);ee.pf(0x49C7DC,1.2)
                        ee.put(obj+0x58,1);ee.put(obj+0x64,47,2);ee.put(obj+0x62,tipo,2)
                        ee.put(work+0xBC+47*4,obj)
                        posicion=[10.,7.,-20.,1.]
                        ee.pv(obj+0x30,posicion);ee.pv(obj+0x40,velocidad)
                        ee.pf(obj+0x50,tiempo);ee.pf(obj+0x54,100.)
                        ee.put(obj+0x5C,0xFFFFF000)
                        count=ee.run(INICIO)
                        contacto=ee.get(work-0x260+0x1D0)!=0
                        salida=0 if ee.dibuja else 2 if contacto else 1
                        # Las salidas destructivas tienen que limpiar SU slot.
                        if (ee.eliminados==[obj]) != (salida!=0):
                            raise AssertionError(('eliminacion',tipo,salida,ee.eliminados))
                        if ee.get(work+0xBC+47*4)!=(obj if salida==0 else 0):
                            raise AssertionError('slot no limpiado')
                        words=[ee.get(obj+p) for p in range(0x30,0x58,4)]+[ee.get(obj+0x5C)]
                        raw+=struct.pack('<2I12fI',tipo,pausa,distancia,oracle.F(1.2),
                                         *posicion,*velocidad,tiempo,100.,0xFFFFF000)
                        raw+=struct.pack('<12I',salida,*words)
                        cases.append(dict(tipo=tipo,pausa=pausa,distancia2=distancia,
                                          tiempo=tiempo,velocidad=velocidad,salida=salida,
                                          instrucciones=count,estado_bits=words))
    struct.pack_into('<I',raw,8,len(cases))
    fixture=args.salida/'original.bin';fixture.write_bytes(raw)
    proc=subprocess.run([str(args.cpp),str(fixture)],capture_output=True,text=True,check=False)
    report=dict(elf_sha256=ELF_SHA,inicio=hex(INICIO),limite_antes_dibujo=hex(LIMITE_DIBUJO),
                sha256=hashlib.sha256(memoria_elf(data,INICIO,FIN-INICIO)).hexdigest(),
                llamadas=llamadas(data,INICIO,FIN),
                oraculo_sha256=hashlib.sha256(args.oraculo.read_bytes()).hexdigest(),
                cpp_sha256=hashlib.sha256(args.cpp.read_bytes()).hexdigest(),
                fixture_sha256=hashlib.sha256(raw).hexdigest(),casos=cases,
                cpp=dict(codigo=proc.returncode,salida=proc.stdout,error=proc.stderr),
                normal=False,integrado=False,
                alcance='fase anterior al dibujo; distancia2 externa y helpers vectoriales acotados; sin anclas, wrap, GS, contacto natural ni audio')
    (args.salida/'resultado.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(dict(casos=len(cases),cpp=report['cpp'],salida=str(args.salida),normal=False,integrado=False),indent=2))
    return proc.returncode


if __name__=='__main__':
    raise SystemExit(main())
