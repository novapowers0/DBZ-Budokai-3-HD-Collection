"""Bubbles: lector EE puro y captura propia SOLO cuando gp2 este libre.
No fuerza estados/clips/flags/seed. Las lecturas se delimitan por contador EE.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import zipfile

HOME=Path(__file__).resolve().parent
PROJECT=Path('C:/Users/javie/Desktop/PROYECTOS IA/DBZ Budokai 3 HD Collection')
EMU=Path('D:/tmp/solGP2/pcsx2_gp2')
CAM=Path('D:/tmp/solGP2/noche/camaras')
COUNT=0x4A4D40
PORT_CSV=Path('D:/tmp/solGP2/noche/bb_7D1_1793504663681255100/bb.csv')

def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,data):
    with Path(p).open('x',encoding='utf-8') as f:json.dump(data,f,indent=1,ensure_ascii=False,allow_nan=False)
def ptr(x):return 0x100000<=x<0x2000000
class Reader:
    def __init__(self,read):self.read=read
    def u(self,a):return struct.unpack('<I',self.read(a,4))[0]
    def h(self,a):return struct.unpack('<h',self.read(a,2))[0]
    def f(self,a):
        x=struct.unpack('<f',self.read(a,4))[0];return x if math.isfinite(x) else None
    def vec(self,a,n=3):return [self.f(a+4*i) for i in range(n)]

def leer(read,matrices=True):
    r=Reader(read);n=r.u(COUNT);roots={hex(a):r.u(a) for a in (0x49CA70,0x49CBC0,0x49CBB8,0x49CBA4,0x49CBBC)}
    mgr,o,g,ctx,cam=roots.values();d=dict(n=n,clave=r.u(0x5C0DC8),roots=roots,seed=r.u(0x49BE08),npc=None)
    if ptr(cam):d['camara']=dict(clip=r.u(cam+0x114),f=r.f(cam+0x118),activa=r.u(cam+0x12c),c0=r.vec(cam+0xc0))
    if ptr(ctx):d['tipo_mini']=r.u(ctx+4)
    if ptr(mgr) and ptr(o):
        x=mgr+0x28d0;w=o+0x260;model=r.u(x+0xb78);task=r.u(w+0xb8)
        a=dict(actor=x,work=w,estado=r.h(x+0xcbc),previo=r.h(x+0xcbe),cc4=r.h(x+0xcc4),
               codigo=r.u(x+0xb60),cuadro=r.f(x+0xc68),duracion=r.u(x+0xc6c),fin=r.u(x+0xc74),
               flags18e8=r.u(x+0x18e8),pos=r.vec(x+0x40),ancla=r.vec(x+0x640),work90=r.vec(w+0x90),
               euler=r.vec(w),flags=[r.u(w+y) for y in (0x210,0x214,0x218)],stick=r.vec(w+0x1f4,2),
               reloj=r.f(w+0x1a0),tareaB8=task,modelo=model,rootlocal=None,mat47_128=[])
        if ptr(model):
            idx=r.u(model+0x40c);a['rootindex']=idx
            node=r.u(model+8+4*idx) if idx<47 else 0
            if ptr(node):a['rootnode']=node;a['rootlocal']=r.vec(node+0x50);a['root128']=read(node,128).hex()
            if matrices:
                nodes=struct.unpack('<47I',read(model+8,47*4))
                for k,p in enumerate(nodes):
                    if not ptr(p):raise ValueError(f'Nodo BBR{k} fuera de EE: {p:x}')
                    a['mat47_128'].append(dict(i=k,p=p,raw=read(p,128).hex()))
        if ptr(task):a['tarea']=dict(callback=r.u(task+0x1c),modo=r.h(task+0x60),fase=r.h(task+0x62),
            tiempo=r.f(task+0x50),duracion=r.f(task+0x54),vel=r.vec(task+0x40),centro=r.vec(task+0x30),raw=read(task,0x70).hex())
        d['npc']=a
        p=mgr+0x800;d['jugador']=dict(estado=r.h(p+0xcbc),codigo=r.u(p+0xb60),cuadro=r.f(p+0xc68),pos=r.vec(p+0x40),ancla=r.vec(p+0x640))
    if ptr(g):d['contacto238']=dict(normal=r.vec(g+0x238),d=r.f(g+0x244),material=r.h(g+0x258),tipo=r.h(g+0x25a),raw=read(g+0x238,36).hex())
    d['n_despues']=r.u(COUNT);d['estable']=n==d['n_despues'] and all(r.u(int(k,16))==v for k,v in roots.items())
    return d

def states():
    found=[];seen=set()
    bases=[Path('D:/tmp/solGP2/camaras/respaldos'),CAM]
    for base in bases:
        for p in sorted(base.rglob('*.p2s')):
            try:
                raw=p.read_bytes();digest=sha(raw)
                if digest in seen:continue
                seen.add(digest)
                with zipfile.ZipFile(p) as z:ram=z.read('eeMemory.bin')
                def read(a,n):return ram[a:a+n]
                d=leer(read,False)
                if d.get('tipo_mini')==1 and d['npc'] and d.get('camara',{}).get('clip') in (650,0x296,0x28a):
                    found.append(dict(path=str(p),sha256=digest,n=d['n'],clave=d['clave'],npc_estado=d['npc']['estado'],
                        codigo=d['npc']['codigo'],camara=d.get('camara'),datos=d))
            except (OSError,KeyError,ValueError,struct.error,zipfile.BadZipFile):continue
    return found

def analizar_port(out):
    with PORT_CSV.open(encoding='utf-8') as f:rows=list(csv.DictReader(f))
    from collections import Counter
    states_=Counter(int(r['estado']) for r in rows);codes=Counter(hex(int(r['codigo'])) for r in rows)
    moves=[];trans=[];old=None;maxroot=0
    for r in rows:
        if old:
            dist=math.dist([float(r[k]) for k in ('x','y','z')],[float(old[k]) for k in ('x','y','z')]);moves.append(dist)
            if r['estado']!=old['estado'] or r['codigo']!=old['codigo']:trans.append({k:r[k] for k in ('tick','estado','codigo','cuadro','flags18e8_consumidos','tarea','x','y','z')})
        dx=float(r['anclax'])-float(r['x']);dz=float(r['anclaz'])-float(r['z']);rh=math.hypot(float(r['rootx']),float(r['rootz']))
        maxroot=max(maxroot,abs(math.hypot(dx,dz)-rh),abs(float(r['anclay'])-float(r['y'])-float(r['rooty'])))
        old=r
    result=dict(csv=str(PORT_CSV),sha256=sha(PORT_CSV.read_bytes()),filas=len(rows),estados=dict(states_),codigos=dict(codes),
        transiciones=trans,tareas=sum(int(r['tarea'])!=0 for r in rows),sweeps=sum(int(r['sweep_hit']) for r in rows),
        nueva5=sum(int(r['nueva5']) for r in rows),max_desplazamiento=max(moves,default=0),error_ancla_root_norma_max=maxroot,
        lectura='PASOS ejecutados; no confundir con clips cargados. No comparar trayectorias si IC/seed/inputs no iguales.')
    save(out/'port_resumen.json',result);print('PORT',json.dumps(result,ensure_ascii=False),flush=True)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--inventario',action='store_true');ap.add_argument('--capturar',action='store_true')
    ap.add_argument('--estado',type=Path);ap.add_argument('--esperar',type=float,default=0);ap.add_argument('--pasos',type=int,default=100)
    a=ap.parse_args();out=HOME/f'medida_{time.time_ns()}';out.mkdir();print('EVIDENCIA',out,flush=True)
    analizar_port(out)
    if a.inventario:
        ss=states();save(out/'savestates.json',ss)
        print('ESTADOS',json.dumps([{k:v for k,v in s.items() if k!='datos'} for s in ss],ensure_ascii=False),flush=True)
    if a.capturar:capturar(a,out)

def capturar(a,out):
    os.environ['DBZ3_AGENTE']='solGP2'
    if not a.estado:raise ValueError('--estado requiere snapshot existente de Bubbles')
    raw=a.estado.read_bytes()
    with zipfile.ZipFile(a.estado) as z:ram=z.read('eeMemory.bin')
    initial=leer(lambda p,n:ram[p:p+n],False)
    gameplay=initial.get('npc',{}).get('estado',99)<10 and initial.get('camara',{}).get('activa')==0
    if initial['clave']!=0x7D1 or initial.get('tipo_mini')!=1 or not(initial.get('camara',{}).get('clip')==650 or gameplay):
        raise ValueError('Snapshot no es clave7D1/camara650/tipo1: no lanzar')
    save(out/'snapshot_elegido.json',dict(path=str(a.estado),sha256=sha(raw),datos=initial))
    sys.path.insert(0,str(PROJECT/'tools'))
    spec=importlib.util.spec_from_file_location('_bb_gp2_privado',HOME.parent.parent/'camaras/iw_camaras_gp2.py')
    T=importlib.util.module_from_spec(spec);spec.loader.exec_module(T)
    import pcsx2_cuadros as C
    import pruebas as P
    s=T.sonda_privada()
    if os.path.normcase(os.path.abspath(s.EXE))!=os.path.normcase(str(EMU/'pcsx2.exe').replace('/','\\')) or s.PUERTO!=28033:
        raise RuntimeError('Ruta/puerto no son gp2 autorizada')
    reserve=object.__new__(P.Juego);reserve.nombre='gp2_bubbles_original';reserve.dest=EMU
    wait_end=time.monotonic()+a.esperar;owned_slot=False
    while True:
        pid=s.nuestro_pid()
        busy_backup=(CAM/'gp2_ini.original').exists() or (T.HOME/'PCSX2_gp2.ini.original').exists()
        if not pid and not busy_backup:
            try:T.puerto_libre()
            except OSError:pass
            else:
                if reserve._tomar_plaza(False):owned_slot=True;break
        if time.monotonic()>=wait_end:
            save(out/'sin_reserva.json',dict(gp2_ocupada=bool(pid),pid=pid,respaldo_ajeno=busy_backup,accion='ninguna conexion PINE ni cambio de configuracion'))
            print('SIN_RESERVA',pid,flush=True);return
        print('ESPERA_GP2 reservada',pid,'backup',busy_backup,flush=True);time.sleep(10)
    ini=EMU/'inis/PCSX2.ini';backup=out/'gp2_ini_antes.ini';slot=EMU/'sstates/SLES-55347 (335A5A1F).08.p2s'
    previous=out/'slot8_previo.p2s';procs=[];c=None;prepared=False;slot_prepared=False;held=False;hook=None;ini_hash=None
    slot_was_present=slot.exists();cleanup_errors=[]
    try:
        if s.nuestro_pid():raise RuntimeError('gp2 ocupada tras adquirir plaza; no adoptar')
        T.puerto_libre()
        shutil.copy2(ini,backup);ini_hash=sha(backup.read_bytes());prepared=True;T.configurar(ini)
        if hasattr(a,'configurar_ini'):a.configurar_ini(ini)
        slot_prepared=True
        if slot.exists():shutil.move(str(slot),str(previous))
        shutil.copy2(a.estado,slot)
        class Own:
            def __getattr__(self,name):return getattr(subprocess,name)
            def Popen(self,*args,**kw):
                p=subprocess.Popen(*args,**kw);procs.append(p);return p
        s.subprocess=Own()
        p=s.lanzar('iw',8);p.s.close();c=C.Cuadros(s,COUNT)
        save(out/'instancia_propia.json',dict(script_pid=os.getpid(),pid=procs[0].pid,exe=str(EMU/'pcsx2.exe'),pine=28033,slot=8,ini_sha256=ini_hash))
        if hasattr(a,'captura_personal'):
            c.pausa();a.captura_personal(c,s,out,initial,procs[0]);return
        c.pausa();hook=poner_hook_fin9(c.p)
        #No salto a estado: dejar650/intro/READY concluir por el juego.
        c.pulso(0x75)
        deadline=time.monotonic()+150
        while time.monotonic()<deadline:
            b=c.p.u32(0x49ca70);cam=c.p.u32(0x49cbbc)
            if ptr(b) and ptr(cam):
                vals=c.p.lote([(b+0x28d0+0xcbc,2),(cam+0x12c,4),(COUNT,4)])
                if vals[0]<10 and vals[1]==0:break
            time.sleep(.04)
        else:raise TimeoutError('No llego gameplay natural desde650; no se fuerza fase')
        c.pausa();os.environ['DBZ3_PCSX2_CUADROS_RESPALDO']=str(out/'respaldos')
        rows=[];first=leer(c.p.leer);save(out/'ic_gameplay.json',first);rows.append(first)
        print('GAMEPLAY',first['n'],first['npc']['estado'],first['jugador'],flush=True)
        for i in range(a.pasos):
            if i==20:c.tecla(0x49,True);held=True #LUp=Keyboard/I en configuracion existente.
            if i==80 and held:c.tecla(0x49,False);held=False
            c.paso();d=leer(c.p.leer)
            if not d['estable']:raise RuntimeError('Lectura original cruzada por contador')
            if (d['n']-rows[-1]['n'])&0xffffffff!=1:raise RuntimeError('No es un paso PAL consecutivo')
            d['input_I']=held;rows.append(d)
            print('BB_EE',d['n'],d['npc']['estado'],hex(d['npc']['codigo']),d['npc']['cuadro'],d['npc']['pos'],d['seed'],flush=True)
            if d['clave']!=0x7D1 or d['jugador']['estado']>=0x2c:break
        if held:c.tecla(0x49,False);held=False
        save(out/'serie_original.json',dict(schema='bubbles_EE_v1',filas=rows,IC=first,fuente_snapshot=sha(raw),
            inputs='I/LUp pulsado despues de20pasos hasta80; ningun estado/clip/flag/seed forzado',instrumentacion=hook))
        if hook:
            hraw=c.p.leer(0xE0000,0x500);save(out/'fin9_hook.json',dict(cuenta=int.from_bytes(hraw[:4],'little'),raw=hraw.hex(),
                capturado=int.from_bytes(hraw[:4],'little')>0,fuente='3187A8->2F5DA0->3187B0',sin_forzar_estado=True))
        save(out/'foto_final.json',c.captura(out/'original_final.png'))
    finally:
        if c:
            try:
                if held:c.tecla(0x49,False)
                if hook:quitar_hook_fin9(c.p,hook)
            except Exception as exc:cleanup_errors.append(str(exc))
            finally:c.p.s.close()
        try:
            for p in procs:
                if p.poll() is None:
                    p.terminate()
                    try:p.wait(timeout=20)
                    except subprocess.TimeoutExpired:p.kill();p.wait(timeout=10)
            if prepared and not s.nuestro_pid():
                shutil.move(str(ini),str(out/'gp2_ini_usado.ini'));shutil.copy2(backup,ini)
            if slot_prepared and not s.nuestro_pid():
                if slot.exists():shutil.move(str(slot),str(out/'slot8_usado.p2s'))
                if previous.exists():shutil.copy2(previous,slot)
            if procs:s.TURNO.liberar(procs[-1].pid)
            save(out/'cierre.json',dict(own_cerrados=all(p.poll() is not None for p in procs),
                ini_restaurado=prepared and sha(ini.read_bytes())==ini_hash,
                slot8_restaurado=(slot.exists() and previous.exists() and sha(slot.read_bytes())==sha(previous.read_bytes())) if slot_was_present else not slot.exists(),errores=cleanup_errors))
        finally:
            if owned_slot:reserve._soltar_plaza()

def I(op,rs,rt,imm):return (op<<26)|(rs<<21)|(rt<<16)|(imm&0xffff)
def poner_hook_fin9(p):
    site=0x3187a8;base=0xE0000;stub=base+0x800;old=p.u32(site)
    if old!=0x0c0bd768:raise RuntimeError('Fin9 no tiene jal2F5DA0 original')
    if any(p.leer(base,0x1800)):raise RuntimeError('Zona de traza no libre; no sobrescribir')
    t0,t1,t2,t3,t4,t5=8,9,10,11,12,13
    code=[I(9,29,29,-0x40),I(0x3f,29,31,0),I(0x3f,29,4,8),I(0x3f,29,5,16)]
    def copy_sample(offset):
        #Solo GPR: no toca VF/FPU ni argumentos de RootAndBones.
        c=[I(15,0,t0,base>>16),I(9,t0,t0,offset),I(0x23,29,t1,8),I(0x23,29,t5,16),I(0x2b,t0,t1,0),I(0x2b,t0,t5,4)]
        for a,dst in ((COUNT,8),(0x49be08,12)):
            c.extend([I(15,0,t2,(a+0x8000)>>16),I(0x23,t2,t2,a&0xffff),I(0x2b,t0,t2,dst)])
        for src,dst in ((0xcbc,24),(0xb60,28),(0xc68,32),(0xc74,36),(0xcc4,40),(0x18e8,44)):
            c.extend([I(0x23,t1,t2,src),I(0x2b,t0,t2,dst)])
        for src,dst in ((0x40,48),(0x640,64)):
            c.extend([I(0x1e,t1,t4,src),I(0x1f,t0,t4,dst)])
        c.extend([I(0x1e,t5,t4,0x90),I(0x1f,t0,t4,80),I(0x23,t5,t2,0xb8),I(0x2b,t0,t2,112)])
        c.extend([I(0x23,t1,t2,0xb78),I(0x23,t2,t3,0x40c),I(0x2b,t0,t3,16),
                  (t3<<16)|(t3<<11)|(2<<6), (t2<<21)|(t3<<16)|(t2<<11)|0x21,
                  I(0x23,t2,t2,8),I(0x2b,t0,t2,20)])
        for k in range(0,128,16):c.extend([I(0x1e,t2,t4,k),I(0x1f,t0,t4,128+k)])
        c.extend([I(15,0,t1,base>>16),I(0x23,t1,t2,0),I(9,t2,t2,1),I(0x2b,t1,t2,0)])
        return c
    code+=copy_sample(0x100);code+=[0x0c0bd768,0];code+=copy_sample(0x300)
    code+=[I(0x37,29,31,0),0x03e00008,I(9,29,29,0x40)]
    encoded=b''.join(struct.pack('<I',w) for w in code)
    if len(encoded)>0x1000:raise RuntimeError('Stub demasiado grande')
    p.escribir(stub,encoded);p.escribir(site,struct.pack('<I',0x0c000000|(stub>>2)))
    return dict(site=site,original=old,base=base,stub=stub,bytes=len(encoded),sha256=sha(encoded),limite='instrumentacion de observacion; sin cambiar clip/estado/RNG')

def quitar_hook_fin9(p,h):
    expected=0x0c000000|(h['stub']>>2)
    if p.u32(h['site'])!=expected:raise RuntimeError('Gancho no coincide: no restaurar bytes ajenos')
    p.escribir(h['site'],struct.pack('<I',h['original']))
if __name__=='__main__':main()
