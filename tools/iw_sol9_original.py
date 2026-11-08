"""IW9: medir el original por campos, con la reserva PCSX2 y sin contar ventanas ajenas."""
import argparse
import json
import os
import shutil
from pathlib import Path
import struct
import subprocess
import sys
import threading
import time
import iw_sol9 as I
import pcsx2_cuadros as C
import pruebas as P


def rescatar(pid,padre,clave):
    """Conservar y cerrar SOLO el PCSX2 que quedo huerfano de nuestro helper."""
    os.environ['DBZ3_PCSX2_CUADROS_TMP']=str(I.BASE)
    os.environ['DBZ3_PCSX2_CUADROS_RESPALDO']=str(I.RESPALDO)
    s=C.conectar('principal')
    assert s.nuestro_pid()==pid,'la instancia ya no es el proceso indicado'
    q=subprocess.run(['powershell','-NoProfile','-Command',
        f'(Get-CimInstance Win32_Process -Filter "ProcessId={pid}").ParentProcessId'],
        capture_output=True,text=True,check=True)
    assert q.stdout.strip()==str(padre),'el proceso no pertenece al helper identificado'
    assert s.TURNO.validar()==pid,'la reserva no coincide'
    out=I.BASE/('original_panel_'+clave+'_rescate_'+str(time.time_ns()));out.mkdir()
    d=dict(pid=pid,padre=padre,clave=clave,filas=[],fotos=[])
    try:
        c=C.Cuadros(s,0x4A4D40);p=c.p;c.pausa()
        f=lambda a:struct.unpack('<f',p.leer(a,4))[0]
        x=p.u32(0x49CECC if clave=='0x7D4' else 0x49CE08)
        d.update(contador=c.numero(),reloj=f(0x5C0FF0),estado=p.u32(0x5C0F54),actor=hex(x))
        if 0x100000<x<0x2000000:d['cuerpo']=dict(codigo=hex(p.u32(x+0xB60)),cuadro=f(x+0xC68))
        foto=c.captura(out/'panel_actual_05.png')
        # Solo se etiqueta como420 si el reloj real lo acredita.
        d['fotos'].append(dict(edad=420 if 420<=d['reloj']<422 else -1,reloj=d['reloj'],**foto))
        print(json.dumps(d,indent=2),flush=True)
    finally:
        (out/'medida.json').write_text(json.dumps(d,indent=2),encoding='utf-8')
        if s.nuestro_pid()==pid:
            subprocess.run([sys.executable,str(I.ROOT/'tools/pcsx2_cuadros.py'),'--instancia','principal','cerrar'],check=True)


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('caso',choices=['vuelo','panel','rescate','intro','meta'])
    ap.add_argument('--clave',default='0x7D6')
    ap.add_argument('--meta-x',type=float,default=6500.0)
    ap.add_argument('--meta-z',type=float,default=-6000.0)
    ap.add_argument('--pid',type=int)
    ap.add_argument('--padre',type=int)
    a=ap.parse_args()
    if a.caso=='rescate':
        assert a.pid and a.padre
        return rescatar(a.pid,a.padre,a.clave)
    clave=int(a.clave,0)
    indice={0x7D4:5,0x7DC:13,0x7D6:7,0x7E0:17}[clave]
    os.environ['DBZ3_PCSX2_CUADROS_TMP']=str(I.BASE)
    os.environ['DBZ3_PCSX2_CUADROS_RESPALDO']=str(I.RESPALDO)
    out=I.BASE/('original_'+a.caso+'_'+a.clave+'_'+str(time.time_ns()));out.mkdir()
    reserva=P.Juego('sol','iw9_original_'+a.caso)
    while not reserva._tomar_plaza(False): time.sleep(5)
    s=C.conectar('principal');propio=False;preparado=False;filas=[];fotos=[];c=None
    PineBase=s.Pine
    class PineArranque(PineBase):
        def __init__(self,espera=180):
            super().__init__(espera=max(espera,360))
    s.Pine=PineArranque
    try:
        # Espera por evento a la instancia ajena; nunca la cierra.
        while (pid:=s.nuestro_pid()): I.esperar(pid)
        C.preparar(s)
        preparado=True
        try:
            p=s.lanzar('iw',4)
        except Exception:
            # lanzar puede haber creado PCSX2 antes de fallar al conectar PINE.
            # Solo adoptar el proceso cuyo PADRE es este helper.
            pid=s.nuestro_pid()
            if pid:
                q=subprocess.run(['powershell','-NoProfile','-Command',
                    f'(Get-CimInstance Win32_Process -Filter "ProcessId={pid}").ParentProcessId'],
                    capture_output=True,text=True,check=True)
                propio=q.stdout.strip()==str(os.getpid())
            raise
        propio=True;p.s.close();time.sleep(6)
        c=C.Cuadros(s,0x4A4D40);p=c.p
        hilo=threading.Thread(target=lambda:s.teclas('right espera300 z espera800 down down z'));hilo.start()
        fin=time.monotonic()+30
        while time.monotonic()<fin:
            if p.u32(0x5C0DC8)==0x7D0:
                p.escribir(0x5C0DC4,struct.pack('<II',indice,clave));break
            time.sleep(.01)
        else: raise RuntimeError('sin seleccion original')
        hilo.join()
        fin=time.monotonic()+240
        fs=lambda addr:struct.unpack('<f',p.leer(addr,4))[0]
        if a.caso=='intro':   # eIW8: linea de tiempo de la presentacion del vuelo (G=*(0x49CE3C)): desde que existe hasta soltar la puerta +0x550
            while time.monotonic()<fin:
                g=p.u32(0x49CE3C)
                if 0x100000<g<0x2000000:
                    c.pausa();break
                time.sleep(.005)
            else:raise RuntimeError('sin viewer de vuelo')
            visto=False
            for k in range(2600):
                filas.append(dict(campo=k,contador=c.numero(),puerta=p.u32(g+0x550),codigo=hex(p.u32(g+0x554)),
                    estado=p.u32(g+0x570),tiempo_ms=fs(g+0x518),listo=fs(g+0x52C),f54c=p.u32(g+0x54C),
                    mgr_clip=p.u32(0x5C0F54)))
                if k in (0,300,460,1080,1170):fotos.append(c.captura(out/f'intro_{k:04}_05.png'))
                visto=visto or filas[-1]['puerta']!=0
                if visto and k>6 and filas[-1]['puerta']==0 and filas[-6]['puerta']==0:break
                c.paso()
        elif a.caso=='meta':   # eIW8: coloca el viewer EN la meta (x,z del #MSF) tras READY y registra el cierre campo a campo
            while time.monotonic()<fin:
                g=p.u32(0x49CE3C)
                if 0x100000<g<0x2000000 and p.u32(g+0x724)==0xFFFFFFFF and p.u32(g+0x54C)==1 and p.u32(g+0x554)==0x320:
                    c.pausa();break
                time.sleep(.01)
            else:raise RuntimeError('sin entrada de vuelo')
            for _ in range(240):
                if p.u32(g+0x550)==0:break
                c.paso()
            else:raise RuntimeError('READY no libera G+550')
            c.tecla(0x49,True);c.tecla(0x57,True)   # palanca adelante: sin entrada el viewer no corre
            y=fs(g+0x5C4)
            p.escribir(g+0x5C0,struct.pack('<3f',a.meta_x,y,a.meta_z))
            ult=None
            for k in range(2400):
                fila=dict(campo=k,contador=c.numero(),puerta=p.u32(g+0x550),codigo=hex(p.u32(g+0x554)),estado=p.u32(g+0x570),
                    tiempo_ms=fs(g+0x518),f54c=p.u32(g+0x54C),posicion=struct.unpack('<3f',p.leer(g+0x5C0,12)),
                    yaw=p.u32(g+0x55C),vel=fs(g+0x560),mgr_estado=p.u32(0x5C0F54))
                filas.append(fila)
                firma=(fila['estado'],fila['codigo'],fila['f54c'],fila['puerta'])
                if firma!=ult:
                    fotos.append(dict(campo=k,**c.captura(out/f'meta_{k:04}_{fila["estado"]}_{fila["codigo"]}.png')));ult=firma
                if len(fotos)>40:break
                c.paso()
        elif a.caso=='vuelo':
            while time.monotonic()<fin:
                g=p.u32(0x49CE3C)
                if 0x100000<g<0x2000000 and p.u32(g+0x724)==0xFFFFFFFF and p.u32(g+0x54C)==1 and p.u32(g+0x554)==0x320:
                    c.pausa();break
                time.sleep(.01)
            else:raise RuntimeError('sin entrada de vuelo')
            for _ in range(240):
                if p.u32(g+0x550)==0:break
                c.paso()
            else:raise RuntimeError('READY no libera G+550')
            c.tecla(0x49,True);c.tecla(0x57,True)
            for k in range(51):
                filas.append(dict(campo=k,contador=c.numero(),bloqueado=p.u32(g+0x550),
                    tiempo_ms=fs(g+0x518),estado=p.u32(g+0x570),codigo=hex(p.u32(g+0x554)),
                    posicion=struct.unpack('<3f',p.leer(g+0x5C0,12))))
                if k in (0,25,50):fotos.append(c.captura(out/f'vuelo_{k:03}_05.png'))
                if k<50:c.paso()
        else:
            terminal=0x2AA if clave==0x7D4 else 0x5CA
            while time.monotonic()<fin:
                # EU377A00: NB006 usa49CECC;49CE08 corresponde a NB014.
                x=p.u32(0x49CECC if clave==0x7D4 else 0x49CE08)
                if 0x100000<x<0x2000000 and p.u32(x+0xB60)==terminal and fs(x+0xC68)>=238 and fs(0x5C0FF0)>0:
                    c.pausa();break
                time.sleep(.01)
            else:raise RuntimeError('sin recompensa del fracaso original')
            targets=[90,320,420]
            for k in range(450):
                reloj=fs(0x5C0FF0)
                filas.append(dict(campo=k,contador=c.numero(),reloj=reloj,estado=p.u32(0x5C0F54)))
                if targets and reloj>=targets[0]-.01:
                    edad=targets.pop(0)
                    fotos.append(dict(edad=edad,reloj=reloj,**c.captura(out/f'panel_{edad:03}_05.png')))
                if not targets:break
                c.paso()
    finally:
        emulog=Path(s.EMU)/'logs/emulog.txt'
        if preparado and emulog.exists():
            try:shutil.copy2(emulog,out/'emulog.txt')
            except OSError as e:print('No se pudo conservar emulog:',e,flush=True)
        (out/'medida.json').write_text(json.dumps(dict(clave=a.clave,caso=a.caso,filas=filas,fotos=fotos,
            contador='0x4A4D40',reloj='G+518 ms; G+550 gate' if a.caso=='vuelo' else '0x5C0FF0'),indent=2),encoding='utf-8')
        if propio:
            if c and a.caso in ('vuelo','meta'):
                c.tecla(0x49,False);c.tecla(0x57,False)
            subprocess.run([sys.executable,str(I.ROOT/'tools/pcsx2_cuadros.py'),'--instancia','principal','cerrar'],check=True)
        elif preparado and not s.nuestro_pid():
            subprocess.run([sys.executable,str(I.ROOT/'tools/pcsx2_cuadros.py'),'--instancia','principal','cerrar'],check=True)
        reserva._soltar_plaza()
    print('EVIDENCIA',out,flush=True)


if __name__=='__main__':main()
