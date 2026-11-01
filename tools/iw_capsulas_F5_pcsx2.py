"""Referencia CapsulePick: instancia propia, arnes de cuadros y estados existentes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import time
import zipfile
import io
import subprocess
import sys
import ctypes
import pcsx2_cuadros as C


def inventario(s,out):
    from PIL import Image,ImageDraw
    files=sorted((Path(s.EMU)/'sstates').glob('SLES-55347 (*.p2s'))
    if not files:files=sorted((Path(s.EMU)/'sstates').glob('SLES-55347*.p2s'))
    sheet=Image.new('RGB',(640,248*((len(files)+1)//2)),(20,20,20));draw=ImageDraw.Draw(sheet);rows=[]
    for i,p in enumerate(files):
        with zipfile.ZipFile(p) as z:
            ram=z.read('eeMemory.bin');png=z.read('Screenshot.png')
        def u(a):return struct.unpack_from('<I',ram,a)[0]
        image=Image.open(io.BytesIO(png)).convert('RGB');image.thumbnail((320,224));x=(i%2)*320;y=(i//2)*248
        sheet.paste(image,(x,y+24));draw.text((x+4,y+4),p.name,fill='white')
        row=dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),
            contador=u(0x4a4d40),globals={hex(a):hex(u(a)) for a in (0x49ca70,0x49cbc0,0x5c0dc8,0x49cb28,0x49cb70,0x5b5af0)})
        rows.append(row);print(json.dumps(row),flush=True)
    sheet.save(out/'estados.jpg');(out/'inventario.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')


def medir(s,out,estado,calentar):
    import pruebas as P
    assert s.INSTANCIA!='principal' and not s.nuestro_pid(),'instancia ocupada; no se adopta proceso ajeno'
    ini=(Path(s.EMU)/'inis/PCSX2.ini').read_text(encoding='utf-8-sig')
    assert 'StandardVolume = 0' in ini and 'Backend = Null' in ini and 'OutputMuted = true' in ini
    os.environ['DBZ3_PCSX2_CUADROS_TMP']=str(out)
    os.environ['DBZ3_PCSX2_CUADROS_RESPALDO']=str(out/'respaldo')
    C.limitar_port(P);reserva=P.Juego('sol','solIWF_capsulas_original');preparado=False;propio=False;c=None;datos=dict(instancia=s.INSTANCIA,puerto=s.PUERTO,sonido='Null/muted/0',filas=[],capturas=[])
    try:
        print('COLA de plaza propia',flush=True)
        limite=time.monotonic()+1800
        while not reserva._tomar_plaza(False):
            if time.monotonic()>limite:raise RuntimeError('plaza no disponible tras1800s; ninguna instancia abierta')
            time.sleep(5)
        C.preparar(s);preparado=True
        try:p=s.lanzar('iw',estado)
        finally:
            pid=s.nuestro_pid()
            if pid:
                parent=subprocess.run(['powershell','-NoProfile','-Command',f'(Get-CimInstance Win32_Process -Filter "ProcessId={pid}").ParentProcessId'],capture_output=True,text=True,check=True)
                propio=parent.stdout.strip()==str(os.getpid());datos['pid']=pid
        assert propio,'no se adopta una instancia de otro proceso';p.s.close()
        c=C.Cuadros(s,0x4a4d40);p=c.p
        h=s.ventana_juego(pid);P.U32.GetMonitorInfoW.argtypes=[P.wt.HMONITOR,ctypes.POINTER(P._MONINFO)]
        pantallas=subprocess.run(['powershell','-NoProfile','-Command',
            'Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Screen]::AllScreens | ForEach-Object { if ($_.DeviceName.EndsWith("DISPLAY2")) { @($_.Bounds.Left,$_.Bounds.Top,$_.Bounds.Right,$_.Bounds.Bottom) | ConvertTo-Json -Compress } }'],capture_output=True,text=True,check=True)
        assert pantallas.stdout.strip(),'no existe DISPLAY2';m=json.loads(pantallas.stdout)
        s.u32dll.SetWindowPos.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_uint]
        assert s.u32dll.SetWindowPos(h,1,m[0]+20,m[1]+35,816,639,0x10),'no se pudo colocar la ventana'
        c.pausa()
        datos.update(pantalla2=m,version=p.version(),serial=p.id(),contador_inicial=c.numero(),estado_base=estado)
        assert datos['serial']=='SLES-55347'
        def row():
            effect=p.u32(0x5b5af0);work=p.u32(0x49cb94)
            r=dict(contador=c.numero(),destino=p.u32(0x49cbe4),escena=p.u32(0x5c1490),work=work,efecto=effect,rng=p.u32(0x49be08),
                salida_lista=p.u32(0x49cbe0),carga_lista=p.u32(0x49cbdc),
                paso=struct.unpack('<f',p.leer(0x5509e8,4))[0],dt=struct.unpack('<f',p.leer(0x5509ec,4))[0])
            if 0x100000<effect<0x1fff000:r['nodos']=p.u32(effect+0x48)
            if 0x100000<work<0x1fff000:
                task=p.u32(work);r['task']=task
                if 0x100000<task<0x1fff000:r['callback']=p.u32(task+0x1c)
                for nombre,off in (('enlace',0xbc),('script',0xd4)):
                    handle=p.u32(work+off)
                    if 0x100000<handle<0x1fff000:r[nombre+'_carga']=int.from_bytes(p.leer(handle+4,2),'little')
            return r
        datos['antes_peticion']=row()
        # Salida de Transition_ApplyQueuedParams2ADDE0: destino13, parametro0
        # y callback de fin listo. Es preparacion RAM de la transicion; la
        # primera prueba solo escribia el destino y quedaba esperando CBE0.
        # Cleanup,45cargas,281D20 y guiones se ejecutan nativos. Sin parchear
        # codigo, llamar al renderer ni instalar un SPX/pose artificial.
        previo=p.u32(0x49cbe4);assert previo==0xffffffff,'el original ya tenia una transicion pendiente'
        peticion=[]
        for addr,valor in ((0x49cbe4,13),(0x49cbec,0),(0x49cbe0,1)):
            peticion.append(dict(direccion=hex(addr),antes=p.u32(addr),despues=valor));p.escribir(addr,struct.pack('<I',valor))
        datos['peticion_RAM']=peticion
        print('ORIGINAL peticion CapsulePick',datos['antes_peticion'],flush=True)
        if calentar:
            antes=c.numero();c.pulso(0x75);fin=time.monotonic()+10
            while p.status()!=0:
                if time.monotonic()>fin:raise RuntimeError('no reanudo la VM para completar las cargas nativas')
                time.sleep(.05)
            # Estimulo temporal acotado: permite avanzar CDVD/IOP y cargas de
            #45archivos sin congelarlos entre cada campo. La medida exacta
            # posterior vuelve a FrameAdvance con contador y VM pausada.
            time.sleep(calentar);c.pausa();datos['carga_en_marcha']=dict(segundos=calentar,antes=antes,despues=c.numero())
            print('CARGA NATIVA',datos['carga_en_marcha'],row(),flush=True)
        encontrado=False
        for k in range(240):
            c.paso();r=row();datos['filas'].append(r)
            if r.get('nodos',0)>0 and r.get('nodos',0)<512 and r['work']:
                encontrado=True;datos['primero']=r;break
            if k in (0,29,89,179):print('FASE',k,r,flush=True)
        assert encontrado,'no aparecio AME CapsulePick; consultar filas, sin alterar mas datos'
        print('CAPSULEPICK ORIGINAL',datos['primero'],flush=True)
        for avance in (0,1,24):
            if avance:c.paso(avance)
            r=row();tag=str(r['contador']);before=set((out/'respaldo/captura_estados').glob('*.p2s'))
            shot=c.captura(out/('capsulas_'+tag+'_05.png'))
            after=set((out/'respaldo/captura_estados').glob('*.p2s'));nuevo=after-before;assert len(nuevo)==1
            shot.update(ram=r,estado=str(nuevo.pop()));datos['capturas'].append(shot)
            with zipfile.ZipFile(shot['estado']) as z:
                shot['componentes']=z.namelist();ram=z.read('eeMemory.bin')
            from iw_capsulas_F_datos import ELF
            elf=ELF.read_bytes();codigo={}
            for va,n in ((0x1b09d0,0xd10),(0x133770,0x2c0),(0x277150,0x15e0)):
                original=elf[va-0x100000+0x80:va-0x100000+0x80+n]
                codigo[hex(va)]=dict(bytes=n,sha256=hashlib.sha256(ram[va:va+n]).hexdigest(),igual_ELF=ram[va:va+n]==original)
            shot['codigo']=codigo;assert all(c['igual_ELF'] for c in codigo.values()),'codigo AME/GS original difiere del ELF de referencia'
            print('CAPTURA',json.dumps(shot),flush=True)
        datos['status']='PASS_REFERENCIA';print('REFERENCIA ORIGINAL GUARDADA',out,flush=True)
    except BaseException as error:
        datos.update(status='FAIL',error=repr(error))
        if c:
            try:
                if c.p.status()==1:datos['captura_limite']=c.captura(out/'limite_05.png')
            except Exception as captura:datos['error_captura_limite']=repr(captura)
        raise
    finally:
        if c:c.p.s.close()
        # Sonda comprueba ruta/PID/cerrojo. Solo cerrar la que lanzo ESTE script.
        if propio or (preparado and not s.nuestro_pid()):
            result=subprocess.run([sys.executable,str(Path(__file__).with_name('pcsx2_cuadros.py')),'--instancia',s.INSTANCIA,'cerrar'],capture_output=True,text=True)
            datos['cierre']=dict(returncode=result.returncode,log=result.stdout+result.stderr)
            print(result.stdout+result.stderr,flush=True)
        datos['pid_final']=s.nuestro_pid();reserva._soltar_plaza()
        (out/'medida.json').write_text(json.dumps(datos,indent=2),encoding='utf-8')


def main():
    p=argparse.ArgumentParser();p.add_argument('--instancia',default='kit');p.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF'));p.add_argument('--estado',type=int,default=8);p.add_argument('--calentar',type=float,default=6.);p.add_argument('--fondo',action='store_true');p.add_argument('--out',type=Path);p.add_argument('accion',choices=['inventario','medir']);a=p.parse_args()
    assert a.instancia!='principal' and a.tmp.is_dir();os.environ['DBZ3_PCSX2_INSTANCIA']=a.instancia
    out=a.out or a.tmp/('pcsx2_F5_'+str(time.time_ns()))
    if not a.out:out.mkdir()
    if a.fondo:
        with (out/'ejecucion.log').open('w',encoding='utf-8') as log:
            proc=subprocess.Popen([sys.executable,'-B',str(Path(__file__).resolve()),'--instancia',a.instancia,'--estado',str(a.estado),'--calentar',str(a.calentar),'--out',str(out),a.accion],stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        print('TRABAJO',proc.pid,'EVIDENCIA',out,flush=True);return
    s=C.conectar(a.instancia)
    if a.accion=='inventario':inventario(s,out)
    else:medir(s,out,a.estado,a.calentar)
    print('EVIDENCIA',out,flush=True)


if __name__=='__main__':main()
