"""IW9c: campo y fracaso retail, instancia propia y contador PAL por PINE."""
import argparse
import ctypes
import json
import os
import shutil
from pathlib import Path
import struct
import subprocess
import sys
import threading
import time
import pcsx2_cuadros as C
import pruebas as P


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--instancia',default='luna')
    ap.add_argument('--pose',type=float)
    ap.add_argument('--exito',action='store_true')
    a=ap.parse_args();assert a.instancia!='principal'
    os.environ['DBZ3_PCSX2_INSTANCIA']=a.instancia
    os.environ['DBZ3_PCSX2_CUADROS_TMP']='D:/tmp/solIW9c'
    os.environ['DBZ3_PCSX2_CUADROS_RESPALDO']='D:/_PARA_BORRAR_CLAUDE/solIW'
    out=Path('D:/tmp/solIW9c')/(('original_pose_' if a.pose is not None else 'original_campo_')+time.strftime('%H%M%S'))
    out.mkdir()
    s=C.conectar(a.instancia)
    assert not s.nuestro_pid(),'instancia ocupada por otro trabajo'
    # Sonda hereda volumen0 de esta copia; no tocar configuracion de otra.
    assert 'StandardVolume = 0' in (Path(s.EMU)/'inis/PCSX2.ini').read_text(encoding='utf-8-sig')
    propio=False;preparado=False;c=None;filas=[];fotos=[];datos={};scratch=None
    reserva=P.Juego('sol','iw9c_original')
    fin=time.monotonic()+600
    while not reserva._tomar_plaza(False):
        if time.monotonic()>fin:raise RuntimeError('sin plaza de prueba')
        time.sleep(5)
    try:
        C.preparar(s);preparado=True
        estado=4
        if a.pose is not None:
            anterior=json.loads(Path('D:/tmp/solIW9c/pose_original.json').read_text(encoding='utf-8'))
            base_estado='ready40' if a.exito else 'fracaso0'
            estado=anterior[base_estado]['estado']
        try:p=s.lanzar('iw',estado)
        finally:
            pid=s.nuestro_pid()
            if pid:
                q=subprocess.run(['powershell','-NoProfile','-Command',
                    f'(Get-CimInstance Win32_Process -Filter "ProcessId={pid}").ParentProcessId'],capture_output=True,text=True,check=True)
                propio=q.stdout.strip()==str(os.getpid())
        assert propio
        p.s.close();time.sleep(6)
        c=C.Cuadros(s,0x4A4D40);p=c.p
        h=s.ventana_juego(pid)
        s.u32dll.SetWindowPos.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_uint]
        P.U32.GetMonitorInfoW.argtypes=[P.wt.HMONITOR,ctypes.POINTER(P._MONINFO)]
        m=P._pantalla_secundaria()
        if not m:
            # DISPLAY2 puede ser la unica pantalla conectada y la principal.
            # Comprobar su identidad fisica, no confundirla con "no primaria".
            q=subprocess.run(['powershell','-NoProfile','-Command',
                'Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Screen]::AllScreens | ForEach-Object { if ($_.DeviceName.EndsWith("DISPLAY2")) { @($_.Bounds.Left,$_.Bounds.Top,$_.Bounds.Right,$_.Bounds.Bottom) | ConvertTo-Json -Compress } }'],
                capture_output=True,text=True,check=True)
            assert q.stdout.strip(),'sin DISPLAY2'
            m=json.loads(q.stdout)
        s.u32dll.SetWindowPos(h,1,m[0]+20,m[1]+35,816,639,0x10)
        if a.pose is not None:
            c.pausa()
            # sonda.lanzar devuelve tras unos10s y el estado ya habria corrido
            # al final. Recargar NUESTRA captura0 estando pausado, en un slot
            # temporal libre; el archivo se conserva en respaldo al terminar.
            estados=Path(s.EMU)/'sstates'
            prefijo=Path(estado).name.split('_',1)[-1].rsplit('.',2)[0]
            slot=next(i for i in range(98,9,-1) if not(estados/f'{prefijo}.{i:02d}.p2s').exists())
            scratch=estados/f'{prefijo}.{slot:02d}.p2s'
            shutil.copy2(estado,scratch);p.cargar(slot);time.sleep(2)
            c.pausa();assert c.numero()==anterior[base_estado]['contador'],'la recarga avanzo'
            g=p.u32(0x49ca70);x=g+0x800
            f=lambda ad:struct.unpack('<f',p.leer(ad,4))[0]
            if a.exito:
                work=p.u32(0x49cbc0)
                p.escribir(work+0x1a8,struct.pack('<I',0x2e))
                p.escribir(work+0x1b0,struct.pack('<I',1)) # como tocar la meta, EU31F280
                previo=None;capturado=False
                for k in range(550):
                    d=dict(contador=c.numero(),codigo=hex(p.u32(x+0xb60)),cuadro=f(x+0xc68),
                        estado=int.from_bytes(p.leer(x+0xcbc,2),'little'),recompensa=f(0x5c0ff0))
                    firma=(d['codigo'],d['estado'])
                    if firma!=previo:print('EXITO FASE',d,flush=True);previo=firma
                    if k==100 and d['codigo'] not in ('0x52','0xf0'):raise RuntimeError('la peticion2E no fue consumida; no esperar en un estado incorrecto')
                    filas.append(d)
                    if d['recompensa']>0 and not capturado:
                        datos['entrada_panel']=d;capturado=True
                        fotos.append(c.captura(out/'exito_panel0_05.png'))
                    if capturado and d['recompensa']>=90:
                        datos['panel90']=d;fotos.append(c.captura(out/'exito_panel90_05.png'));break
                    c.paso()
                assert capturado and 'panel90' in datos,'sin panel de exito'
                print('EVIDENCIA EXITO',out,datos,flush=True)
                return
            while f(x+0xc68)<a.pose-.01:c.paso()
            datos.update(codigo=hex(p.u32(x+0xb60)),cuadro=f(x+0xc68),contador=c.numero(),
                transform=struct.unpack('<16f',p.leer(x,64)),
                puntos=[struct.unpack('<4f',p.leer(x+0x640+16*i,16)) for i in range(16)])
            fotos.append(c.captura(out/'pose_05.png'))
            print('POSE ORIGINAL',out,datos,flush=True)
            return
        hilo=threading.Thread(target=lambda:s.teclas('right espera300 z espera800 down down z'));hilo.start()
        fin=time.monotonic()+30
        while time.monotonic()<fin:
            if p.u32(0x5C0DC8)==0x7D0:break
            time.sleep(.01)
        else:raise RuntimeError('sin seleccion original')
        hilo.join()
        fin=time.monotonic()+180
        while time.monotonic()<fin:
            ui=p.u32(0x49CD8C)
            if 0x100000<ui<0x2000000 and p.u32(ui+0x150)&2:break
            time.sleep(.01)
        else:raise RuntimeError('sin READY campo')
        c.pausa();g=p.u32(0x49CA70);x=g+0x800;work=p.u32(0x49CBC0)
        f=lambda ad:struct.unpack('<f',p.leer(ad,4))[0]
        def sample():
            return dict(contador=c.numero(),codigo=hex(p.u32(x+0xB60)),cuadro=f(x+0xC68),
                        estado=int.from_bytes(p.leer(x+0xCBC,2),'little'),
                        recompensa=f(0x5C0FF0),transform=struct.unpack('<16f',p.leer(x,64)))
        c.paso(40)
        datos.update(actor=hex(x),work=hex(work),ready=sample(),
            stage_id=p.u32(0x4DB280),stage_var=p.u32(0x4DB2C0))
        li=p.u32(0x44ABCC+datos['stage_id']*92+datos['stage_var']*32)
        datos['luz_id']=li
        datos['luz']=list(struct.unpack('<20f',p.leer(0x449FA0+li*80,80)))
        datos['luz_raw']=p.leer(0x449FA0+li*80,80).hex()
        fotos.append(dict(nombre='ready40',**c.captura(out/'ready40_05.png')))
        print('READY',datos,flush=True)
        for _ in range(200):
            if not(p.u32(ui+0x150)&2):break
            c.paso()
        # Mismo camino de estados que TABLA_OPUS: solicita0x30, sin llamar a
        # rutinas de dibujo ni escoger una pose a mano.
        p.escribir(work+0x1A8,struct.pack('<I',0x30))
        for _ in range(10):
            c.paso()
            if p.u32(x+0xB60)==0x53:break
        else:raise RuntimeError('no entro fracaso53')
        inicial=c.numero();visto=False;targets=[90,320,420]
        for k in range(620):
            d=sample();d['desde_fracaso_pal']=c.numero()-inicial;filas.append(d)
            if k in (0,10,50,100,150,199):
                fotos.append(dict(nombre='fracaso'+str(k),**c.captura(out/f'fracaso{k:03}_05.png')))
            reloj=d['recompensa']
            if reloj>0 and not visto:
                datos['panel_primero']=d;visto=True
                fotos.append(dict(nombre='panel_entrada',**c.captura(out/'panel_entrada_05.png')))
                print('PANEL',d,flush=True)
            if visto and targets and reloj>=targets[0]:
                edad=targets.pop(0)
                fotos.append(dict(nombre='panel'+str(edad),edad_real=reloj,**c.captura(out/f'panel{edad:03}_05.png')))
            if visto and not targets:break
            c.paso()
        assert visto and not targets,'sin edades de panel originales'
    finally:
        (out/'medida.json').write_text(json.dumps(dict(datos=datos,filas=filas,fotos=fotos),indent=2),encoding='utf-8')
        if scratch and scratch.exists():
            shutil.move(str(scratch),str(Path('D:/_PARA_BORRAR_CLAUDE/solIW')/(str(time.time_ns())+'_pose_recarga_'+scratch.name)))
        if propio:
            subprocess.run([sys.executable,str(Path(__file__).with_name('pcsx2_cuadros.py')),'--instancia',a.instancia,'cerrar'],check=True)
        elif preparado and not s.nuestro_pid():
            subprocess.run([sys.executable,str(Path(__file__).with_name('pcsx2_cuadros.py')),'--instancia',a.instancia,'cerrar'],check=True)
        reserva._soltar_plaza()
    print('EVIDENCIA',out,flush=True)


if __name__=='__main__':main()
