"""Visor SHOOT original: contexto49CEAC, no actores de combate49CA70."""
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import shutil
import struct
import threading
import time
import astra_iw_original as A
import pcsx2_cuadros as C
import pruebas as P

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--reanudar',action='store_true');ap.add_argument('--reiniciar-tramo',action='store_true');ap.add_argument('--tramo',type=int);ap.add_argument('--aviso',type=int,default=-1);ap.add_argument('--cuadros',type=int,default=35);ap.add_argument('--final',choices=['bien','mal']);args=ap.parse_args()
    os.environ['DBZ3_AGENTE']='astraIW'
    os.environ['DBZ3_PCSX2_CUADROS_RESPALDO']='D:/_PARA_BORRAR_CLAUDE/astraIW'
    s=A.conectar();assert not s.nuestro_pid(),'astra2 ocupada'
    out=A.OUT/(time.strftime('%Y%m%d-%H%M%S')+'_original_ladrillos');out.mkdir()
    reserva=P.Juego('sol','astraIW_ladrillos_original');c=None;propio=False
    while not reserva._tomar_plaza(False):time.sleep(5)
    try:
        p=s.lanzar('iw',9 if args.reanudar else 4);propio=True;p.s.close()
        c=C.Cuadros(s,0x4A4D40);p=c.p
        P.U32.GetMonitorInfoW.argtypes=[P.wt.HMONITOR,ctypes.POINTER(P._MONINFO)]
        P._a_pantalla_secundaria(type('Proceso',(),{'pid':s.nuestro_pid(),'poll':lambda _:None})())
        if not args.reanudar:
            h=threading.Thread(target=lambda:s.teclas('right espera300 z espera800 down down z'));h.start()
            fin=time.monotonic()+35
            while time.monotonic()<fin:
                if p.u32(0x5C0DC8)==0x7D0:p.escribir(0x5C0DC4,struct.pack('<II',3,0x7D3));break
                time.sleep(.02)
            else:raise RuntimeError('sin seleccion')
            h.join()
        def f(o):return struct.unpack('<f',p.leer(o,4))[0]
        fin=time.monotonic()+200
        while time.monotonic()<fin:
            g=p.u32(0x49CEAC)
            if 0x100000<g<0x1f00000:
                dur=f(g+0x50c)
                if 0<dur<10000 and 0<=p.u32(g+0x4fc)<1000 and 0<f(g+0x504)<dur:break
            time.sleep(.02)
        else:raise RuntimeError('sin tramo de vuelo; revisar G49CEAC')
        c.pausa()
        if args.reiniciar_tramo:
            # Condicion inicial controlada, sin cambiar instrucciones ni tareas.
            if args.tramo is not None:
                bloque=p.u32(g+0x4c4)
                assert 0<=args.tramo<p.u32(bloque+0x60)
                tr=struct.unpack('<7f',p.leer(bloque+0x64+28*args.tramo,28))
                for off,x,y,z in ((0x530,*tr[1:4]),(0x540,*tr[4:7])):
                    p.escribir(g+off,struct.pack('<4f',z*(x-300)/512,z*(y-225)/512,z,1))
                p.escribir(g+0x500,struct.pack('<f',tr[0]))
                p.escribir(g+0x4fc,struct.pack('<I',0))
            inicio=struct.unpack('<3f',p.leer(g+0x530,12))
            p.escribir(g+0x520,struct.pack('<3f',*inicio))
            for off,v in ((0x504,0.),(0x508,0.),(0x550,0.),(0x4ec,300.),(0x4f0,225.),
                          (0x4f4,300.+512.*inicio[0]/inicio[2]),(0x4f8,225.+512.*inicio[1]/inicio[2])):
                p.escribir(g+off,struct.pack('<f',v))
            p.escribir(g+0x4e4,struct.pack('<I',0))
        if args.aviso>=0:
            total=p.u32(p.u32(g+0x4c4))
            p.escribir(g+0x4cc,struct.pack('<I',(2*total)//3 if args.aviso==0 else total//3))
            p.escribir(g+0x508,struct.pack('<f',f(g+0x50c)))
        if args.final:
            p.escribir(g+0x4cc,struct.pack('<I',0));p.escribir(g+0x4d0,struct.pack('<I',0))
            p.escribir(g+0x4dc,struct.pack('<I',7 if args.final=='bien' else 0))
            p.escribir(g+0x508,struct.pack('<f',f(g+0x50c)))
        if not args.reanudar:
            guardado=A.EMU/'sstates/SLES-55347 (335A5A1F).09.p2s'
            if guardado.exists():
                respaldo=Path('D:/_PARA_BORRAR_CLAUDE/astraIW');respaldo.mkdir(parents=True,exist_ok=True)
                shutil.move(str(guardado),str(respaldo/(str(time.time_ns())+'_'+guardado.name)))
            p.guardar(9)
        def leer():
            tarea=p.u32(g+0x40)
            mensaje=p.u32(g+0xbc)
            return dict(n=c.numero(),g=hex(g),callback=hex(p.u32(tarea+0x1c)) if 0x100000<tarea<0x2000000 else '0x0',
                        mensaje=dict(cuenta=p.u32(g+0x6f4),estado=p.u32(g+0x6f8),
                                     callback=hex(p.u32(mensaje+0x1c)) if mensaje else None,
                                     reloj=f(mensaje+0x30) if mensaje else None),
                        enteros={hex(o):p.u32(g+o) for o in (0x4c4,0x4c8,0x4cc,0x4d0,0x4d4,0x4dc,0x4e4,0x4e8,0x4fc)},
                        floats={hex(o):f(g+o) for o in range(0x4ec,0x554,4)},
                        lienzo=[f(0x49C84C),f(0x49C850)],focal=f(0x4AB64C),dt=f(0x49C7DC))
        datos=dict(filas=[],fotos=[])
        anterior=None
        for i in range(args.cuadros):
            c.paso();d=leer();datos['filas'].append(d)
            cambio=args.final and d['callback']!=anterior
            if i in (0,8,16,34,80,args.cuadros-1) or cambio:
                foto=c.captura(out/f'{i:03d}.png');datos['fotos'].append(dict(datos=d,foto=foto))
                print('visor',i,d if args.aviso<0 and not args.final else {k:d[k] for k in ('n','callback','mensaje')},flush=True)
            (out/'medida.json').write_text(json.dumps(datos,indent=2),encoding='utf-8')
            anterior=d['callback']
            if args.final and p.u32(0x49CEAC)!=g:break
        print(out,flush=True)
    finally:
        if c:c.p.s.close()
        if propio:s.main(['--instancia','astra2','cerrar'])
        reserva._soltar_plaza()

if __name__=='__main__':main()
