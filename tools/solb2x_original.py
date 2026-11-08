"""AdquisiciÃ³n B2 PAL en PCSX2 propio; entradas reales, escena nunca forzada.

Prepara selecciÃ³n/posiciones/orientaciÃ³n, despuÃ©s avanza un campo medido.
Conserva muestras de todos los campos y estados para reanudar sin repetir escenas.
"""
import argparse
import configparser
import ctypes
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

import pruebas as P
import pcsx2_cuadros as C
from solb2x_lanzamiento import pared_original
from solb2x_estados import ampliar_estado
from solb2x_solar import posiciones_solar, solar_original


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--grupo',type=int,choices=[0,2,4,7,8],required=True)
    ap.add_argument('--entrada',type=int,default=0,help='entrada SPX de la superficie de preparaciÃ³n')
    ap.add_argument('--altura',type=float,default=9)
    ap.add_argument('--estado',type=Path,help='estado propio para reanudar adquisiciÃ³n')
    ap.add_argument('--estado-pausado',action='store_true',help='reanuda --estado sin avanzar: comprueba el contador guardado antes de medir')
    ap.add_argument('--gs-software',action='store_true',help='GS software en el PCSX2 propio: conservar VRAM sin depender del readback hardware')
    ap.add_argument('--gs-dump',action='store_true',help='solicita un volcado GS de un fotograma en la primera foto; registra transferencias, no solo la VRAM final')
    ap.add_argument('--campos',type=int,default=850)
    ap.add_argument('--huesos',action='store_true',help='guarda 0x2000 bytes crudos del objeto de modelo de cada actor por muestra (buscar las matrices de huesos)')
    ap.add_argument('--huesos-bytes',type=lambda x:int(x,0),default=0x2000,help='bytes del objeto de modelo con --huesos (0x3600 incluye cuaternios locales por hueso)')
    ap.add_argument('--fotos',default='30,120,300,720')
    ap.add_argument('--solar',action='store_true',help='lee proyecciÃ³n y tarea solar original')
    ap.add_argument('--solar-previo',type=int,default=0,help='campos observados antes del ataque, sin forzar el solar (0..120)')
    ap.add_argument('--solo-solar',action='store_true',help='preparaciÃ³n terrestre comÃºn, sin ataque ni transiciÃ³n; requiere grupo4 y --solar-previo')
    ap.add_argument('--tras-fin',type=int,default=0,help='campos adicionales observados tras el fin (0..120)')
    ap.add_argument('--esperar-neutros',type=int,default=0,
                    help='mÃ¡ximo de campos antes de preparar: dos lecturas consecutivas de ambos en reposo')
    ap.add_argument('--pool',type=int,choices=[1,2,3],help='dia1B: se engancha al PCSX2 ya lanzado en esa plaza de la granja (no lanza el propio)')
    ap.add_argument('--slot-libre',type=int,help='dia1B con --pool: slot del savestate de la plaza en combate libre (hace la preparacion normal, sin elegir escenario)')
    a=ap.parse_args()
    if a.pool and a.slot_libre is None:ap.error('--pool requiere --slot-libre')
    if os.getenv('DBZ3_AGENTE') not in ('solB2X','nB2','eB2','eB2a','eB2c','eB2d','eB2f','eB2g','eB2h','eB2i','eB2j','dia1B'):ap.error('requiere DBZ3_AGENTE=solB2X o nB2')
    if a.estado_pausado and not a.estado:ap.error('--estado-pausado requiere --estado')
    if not 0 <= a.tras_fin <= 120:ap.error('--tras-fin admite0..120')
    if not 0 <= a.solar_previo <= 120 or (a.solar_previo and (not a.solar or a.estado)):
        ap.error('--solar-previo admite0..120 y requiere --solar sin --estado')
    if a.solo_solar and (a.grupo!=4 or not a.solar_previo or a.altura!=9):
        ap.error('--solo-solar requiere grupo4, --solar-previo y altura predeterminada9')
    if not 0 <= a.esperar_neutros <= 600 or (a.estado and a.esperar_neutros):
        ap.error('--esperar-neutros admite0..600 y solo la preparaciÃ³n de una partida nueva')
    root=Path(__file__).resolve().parent.parent
    out=Path(os.getenv('DBZ3_SALIDA_PAL') or ('D:/tmp/dia1B/pal' if a.pool else 'D:/tmp/solB2X'))   # N3 7/11: salida propia por agente
    out.mkdir(parents=True,exist_ok=True)
    sys.path.insert(0,str(Path(__file__).resolve().parent))
    # La sonda antigua tiene una lista cerrada. Registrar endpoint propio en memoria.
    os.environ.pop('DBZ3_PCSX2_INSTANCIA',None)
    import pcsx2_sonda as s
    from pcsx2_turnos import Turno
    from ctypes import wintypes as W
    s.u32dll.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
    s.u32dll.PostMessageW.restype=W.BOOL
    if a.pool:
        import pcsx2_pool as PL
        PL.registrar_en_sonda(s,a.pool);perfil=PL.perfil(a.pool);origen=perfil
    else:
        perfil=out/'pcsx2'
        origen=Path(s.RAIZ)/'pcsx2_esc'
        perfil.mkdir(exist_ok=True)
    for p in ([] if a.pool else origen.iterdir()):
        if p.suffix.lower() in ('.dll','.exe') or p.name in ('qt.conf','portable.ini'):
            if not (perfil/p.name).exists():shutil.copy2(p,perfil/p.name)
    for n in ('resources','QtPlugins'):
        if not a.pool and not (perfil/n).exists():shutil.copytree(origen/n,perfil/n)
    if not a.pool:
        for n in ('inis','snaps','sstates','memcards','logs','cache'):(perfil/n).mkdir(exist_ok=True)
        s.INSTANCIA,s.EMU,s.EXE,s.PUERTO='solB2X',str(perfil),str(perfil/'pcsx2.exe'),28041
        s.TURNO=Turno('solB2X',s.PUERTO,s.EXE,cuarentena=str(out/'turnos_archivados'))
        s.TURNO.adquirir(os.getpid(),sys.executable)
        os.environ['DBZ3_PCSX2_INSTANCIA']='solB2X'
    os.environ['DBZ3_PCSX2_CUADROS_RESPALDO']=str(out/'estados')
    stage={0:4,2:6,4:0,7:5,8:1}[a.grupo]
    run=out/f'original_g{a.grupo}_e{a.entrada}_{time.strftime("%Y%m%d_%H%M%S")}'
    run.mkdir()
    j=P.Juego('smoke_c',f'solB2X_original_g{a.grupo}')
    proc=p=None
    filas=[]
    dumps_antes={q.name for q in (perfil/'snaps').iterdir() if q.is_file() and '.gs' in q.name}
    dump_solicitado=None
    try:
        cfg=configparser.ConfigParser(interpolation=None);cfg.optionxform=str
        cfg.read(origen/'inis/PCSX2.ini',encoding='utf-8-sig')
        if a.estado_pausado:cfg['UI']['StartPaused']='true'
        cfg['EmuCore']['PINESlot']=str(s.PUERTO)
        cfg['MemoryCards']['Slot1_Enable']='false';cfg['MemoryCards']['Slot2_Enable']='false'
        if a.gs_software:cfg['EmuCore/GS']['Renderer']='13'
        cfg['SPU2/Output']['OutputModule']='nullout'
        cfg['Folders']['Bios']=str(origen/'bios')
        for n,v in [('Snapshots','snaps'),('Savestates','sstates'),('MemoryCards','memcards'),('Logs','logs'),('Cache','cache')]:cfg['Folders'][n]=str(perfil/v)
        cfg['Hotkeys']['TogglePause']='Keyboard/F6';cfg['Hotkeys']['FrameAdvance']='Keyboard/F7'
        if a.gs_dump:cfg['Hotkeys']['GSDumpSingleFrame']='Keyboard/F9'
        if not a.pool:
            with (perfil/'inis/PCSX2.ini').open('w',encoding='utf-8') as f:cfg.write(f)
        (run/'adquisicion.json').write_text(json.dumps(dict(gs_software_solicitado=a.gs_software,
            estado_pausado_solicitado=a.estado_pausado,
            gs_dump_solicitado=a.gs_dump,
            renderer_configurado=cfg['EmuCore/GS']['Renderer'],
            alcance='configuraciÃ³n solicitada; confirmar renderizador efectivo en el log del emulador'),indent=2),encoding='utf-8')
        estado=a.estado or Path(s.RAIZ)/'pcsx2/sstates/SLES-51839 (FE09A7CC).05.p2s'
        nuevo=(not a.estado) or bool(a.pool)
        # Solo la copia propia: necesario para el prefijo CRC de capturas PINE.
        destino=perfil/'sstates'/'SLES-51839 (FE09A7CC).05.p2s'
        if not a.pool and not destino.exists():shutil.copy2(estado,destino)
        while not a.pool and not j._tomar_plaza():
            j.info('esperando plaza compartida para PCSX2 propio');time.sleep(15)
        if a.pool:
            p=s.Pine(espera=60);assert p.id()=='SLES-51839','Original PAL no iniciado en la plaza'
            p.cargar(a.slot_libre);time.sleep(4)
        else:
            env=dict(os.environ,SDL_JOYSTICK_WGI='0',SDL_JOYSTICK_RAWINPUT='0',SDL_JOYSTICK_HIDAPI='0',SDL_JOYSTICK_DIRECTINPUT='0',SDL_XINPUT_ENABLED='0')
            proc=subprocess.Popen([s.EXE,'-portable','-nogui','-fastboot','-statefile',str(estado),'--',s.ISOS['b2']],cwd=perfil,env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            s.TURNO.adoptar(proc.pid)
            s.nuestro_pid=lambda:proc.pid if proc.poll() is None else None
            p=s.Pine(espera=180)
            fin=time.monotonic()+120
            while p.status()==2 and time.monotonic()<fin:time.sleep(.2)
            assert p.status()!=2 and p.id()=='SLES-51839','Original PAL no iniciado'
            P._a_pantalla_secundaria(proc)
        c=object.__new__(C.Cuadros);c.s,c.p,c.contador,c.exacto=s,p,0x371550,True
        p.s.settimeout(30)
        u16=lambda addr:struct.unpack('<H',p.leer(addr,2))[0]
        fl=lambda addr:struct.unpack('<f',p.leer(addr,4))[0]
        vec=lambda addr:struct.unpack('<3f',p.leer(addr,12))
        # PINE puede responder antes de aplicar -statefile tambiÃ©n al reanudar.
        # La pausa prematura llegÃ³ a leer stage0 antes de restaurar el stage4.
        time.sleep(2)
        if nuevo:
            print('ESTADO CARGADO',p.u32(0x371550),hex(p.u32(0x3a44b4)),p.u32(0x373460),flush=True)
            if not a.pool:
                p.escribir(0x699391,bytes([stage]))
                s.teclas('start espera1000 start')
            fin=time.monotonic()+90
            while time.monotonic()<fin:
                mgr=p.u32(0x3a44b4)
                if mgr and (a.pool or p.u32(0x373460)==stage) and u16(mgr+0x14304) in (2,3) and not u16(mgr+0x19c5c):break
                time.sleep(.1)
            else:
                c.captura(run/'fallo_carga.png')
                raise RuntimeError(('No llegÃ³ a combate libre',hex(mgr),p.u32(0x373460),u16(mgr+0x14304) if mgr else None,u16(mgr+0x19c5c) if mgr else None))
            c.pausa()
            neutros=[]
            if a.esperar_neutros:
                racha=0
                for paso in range(a.esperar_neutros+1):
                    actores=[]
                    for k in range(2):
                        x=mgr+0x210+0x9f50*k
                        actores.append(dict(codigo=p.u32(x+0x990),estado=p.leer(x+0xa04,1)[0],
                                            pendiente=struct.unpack('<h',p.leer(x+0x9c26,2))[0]))
                    r=dict(campo=p.u32(0x371550),evento=u16(mgr+0x19c5c),actores=actores)
                    neutros.append(r)
                    racha=racha+1 if not r['evento'] and all(
                        x['codigo']==0 and x['estado']==0 and x['pendiente']==-1 for x in actores) else 0
                    if racha>=2:break
                    if paso==a.esperar_neutros:
                        (run/'espera_neutros.json').write_text(json.dumps(neutros,indent=2))
                        raise RuntimeError('No llegaron ambos a reposo; no se prepara ni se fuerza el ataque')
                    c.paso()
                (run/'espera_neutros.json').write_text(json.dumps(neutros,indent=2))
                print('REPOSO MEDIDO',json.dumps(dict(campos_avanzados=paso,ultimo=neutros[-1])),flush=True)
            pared=None if a.solo_solar else pared_original(a.grupo,a.altura,a.entrada)
            if pared:
                n=pared['normal'];hit=pared['punto']
                posiciones=[[hit[t]+d*n[t] for t in range(3)] for d in (27.,15.)]
                for pos in posiciones:pos[1]=max(0,a.altura-9)
            else:
                posiciones=posiciones_solar()
            for k,pos in enumerate(posiciones):
                x=mgr+0x210+0x9f50*k
                p.escribir(x+0x40,struct.pack('<3f',*pos))
                dx,dz=(posiciones[1-k][q]-pos[q] for q in (0,2))
                giro=round(math.atan2(dx,dz)*65536/(2*math.pi))&65535
                p.escribir(x+0xfac,struct.pack('<I',giro))
            (run/'preparacion.json').write_text(json.dumps(dict(stage=stage,grupo=a.grupo,pared=pared,
                posiciones=posiciones,solo_solar=a.solo_solar,
                reposo_medido=neutros[-2:] if neutros else None,
                escrituras='selecciÃ³n, posiciÃ³n y orientaciÃ³n; nunca evento/zona/acciÃ³n ni relojes de reacciÃ³n'),indent=2))
            c.captura(run/'antes.png')
        else:
            c.pausa();mgr=p.u32(0x3a44b4)
            assert p.u32(0x373460)==stage,'Estado de otro escenario'
            if a.estado_pausado:
                # StartPaused es una opciÃ³n del host PCSX2, no una escritura al guest.
                # Una restauraciÃ³n no equivale al instante pedido si avanzÃ³ al cargar.
                with zipfile.ZipFile(estado) as z:
                    esperado=struct.unpack_from('<I',z.read('eeMemory.bin'),0x371550)[0]
                observado=p.u32(0x371550)
                comprobacion=dict(estado=str(estado),campo_guardado=esperado,
                    campo_observado=observado,pcsx2_pausado=p.status()==1)
                (run/'restauracion_pausada.json').write_text(json.dumps(comprobacion,indent=2))
                assert comprobacion['pcsx2_pausado'] and observado==esperado,'La restauraciÃ³n avanzÃ³; no medir como instante equivalente'
        def muestra():
            r=dict(campo=p.u32(0x371550),stage=p.u32(0x373460),zona=p.u32(0x3734ac),evento=u16(mgr+0x19c5c),reloj=fl(mgr+0x142fc),actores=[])
            for k in range(2):
                x=mgr+0x210+0x9f50*k;mod=p.u32(x+0x9a4)
                r['actores'].append(dict(pos=vec(x+0x40),colision=vec(x+0x4b0),vel=vec(x+0x60),codigo=p.u32(x+0x990),clip=p.u32(mod+0x41c) if mod else None,giro=p.u32(x+0xfac),tiempo_reaccion=fl(x+0xa00),umbral_reaccion=fl(x+0x9c14),estado=p.leer(x+0xa04,1)[0],pendiente=struct.unpack('<h',p.leer(x+0x9c26,2))[0],tray=p.u32(x+0x9c34)))
            if a.huesos and not (run/'banco3_a0.bin').exists():
                # eB2i: banco 3 del atacante (mod+0x464; capa superpuesta del descriptor, actor+0x9d8), una vez por captura
                m0=p.u32(mgr+0x210+0x9a4);b3=p.u32(m0+0x464) if m0 else 0
                if 0x100000<=b3<0x2000000:(run/'banco3_a0.bin').write_bytes(p.leer(b3,0x100000))
            if a.huesos:
                for k in range(2):
                    m=p.u32(mgr+0x210+0x9f50*k+0x9a4)
                    r['actores'][k]['modelo_ptr']=m
                    r['actores'][k]['modelo_raw']=p.leer(m,a.huesos_bytes).hex() if m else None
                    if a.huesos_bytes>0x2000:
                        ax=mgr+0x210+0x9f50*k
                        tb=p.u32(ax+0x9ac)
                        r['actores'][k]['ik']=dict(actor_754=p.leer(ax+0x700,0x80).hex(),actor_990=p.leer(ax+0x990,0x1c0).hex(),
                            actor_4b0=p.leer(ax+0x4b0,0x100).hex(),
                            tabla_huesos=p.leer(tb,0x40).hex() if 0x100000<=tb<0x2000000 else None,
                            t380=p.leer(0x389380,0x200).hex(),t460=p.leer(0x389460,0x200).hex())
                    if m and a.huesos_bytes>0x2000:
                        sk=p.u32(m+4)
                        if 0x100000<=sk<0x2000000:
                            rp=p.u32(sk+0x14)
                            r['actores'][k]['esqueleto_cab']=p.leer(sk,0x40).hex()
                            r['actores'][k]['reposo']=p.leer(rp,43*0x50).hex() if 0x100000<=rp<0x2000000 else None
                        # eB2f: buffers de pose del animador (A en +0x42C, B en +0x444), 43 huesos x 0x40
                        for nom,off in (('pose_a',0x42c),('pose_b',0x444)):
                            q=p.u32(m+off)
                            r['actores'][k][nom]=p.leer(q,43*0x40).hex() if 0x100000<=q<0x2000000 else None
            r['contacto']=dict(registro=p.u32(0x373500),punto=vec(0x3734e0),normal=vec(0x3734f0))
            # 1CDED0 elige1F0; 1CDC80 conserva en1F4 la cÃ¡mara aplicada.
            # Sin recorrer256 tareas por campo: ese censo se recupera de estados.
            r['escena_original']=ampliar_estado(p.leer,mgr,con_tareas=False)
            if a.solar:r['solar']=solar_original(p.leer)
            return r
        fotos=set(map(int,filter(None,a.fotos.split(','))));hechas=set();activo=False;cerrado=False;fin_en=None
        previas=[];solar_fotografiado=False
        for _ in range(a.solar_previo):
            r=muestra();previas.append(r)
            visible=any(t['estado'][9]>=.95 and t['estado'][10]>=0 and t['estado'][6]>0 and
                        0<=t['estado'][4]<=512 and 0<=t['estado'][5]<=512 for t in r['solar']['tareas'])
            if visible and not r['evento'] and not solar_fotografiado:
                c.captura(run/'solar_previo.png');solar_fotografiado=True
                (run/'solar_previo_visible.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
                if P.Image:
                    with P.Image.open(run/'solar_previo.png') as im:
                        im.resize((im.width//2,im.height//2)).save(run/'solar_previo_mitad.png')
            c.paso()
        if previas:
            (run/'solar_previo.json').write_text(json.dumps(previas,indent=2),encoding='utf-8')
            print('SOLAR PREVIO',json.dumps(dict(muestras=len(previas),visible=solar_fotografiado)),flush=True)
        if a.solo_solar:
            filas.extend(previas)
            print('RESULTADO SOLAR',json.dumps(dict(ruta=str(run),visible=solar_fotografiado,
                muestras=len(previas),ultimo=previas[-1])),flush=True)
            return 0 if solar_fotografiado else 4
        if a.estado and a.gs_dump:
            # TambiÃ©n sirve para un solar sin VM activa. El volcado comienza
            # al avanzar, no se atribuye al frame anterior por pedirlo aquÃ­.
            r=muestra()
            # Sin ningÃºn frame presentado desde la carga, PINE SaveState no
            # puede producir Screenshot.png. No avanzar para fabricar esa foto.
            if not a.estado_pausado:c.captura(run/'gs_previo.png')
            else:r['captura_no_disponible_sin_avanzar']=True
            (run/'gs_previo.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
            dump_solicitado=dict(campo=r['campo'],reloj=r['reloj'],foto='gs_previo',
                alcance='solicitud F9 con el estado detenido; el volcado registra los campos siguientes')
            c.pulso(0x78)
        # Un Ãºnico ataque P+K. El estado de reacciÃ³n y la selecciÃ³n del guion son originales.
        if nuevo:
            c.tecla(s.VK['a'],True);c.tecla(s.VK['s'],True)
        for i in range(a.campos):
            if i==25 and nuevo:
                c.tecla(s.VK['a'],False);c.tecla(s.VK['s'],False)
            r=muestra();filas.append(r)
            if r['evento'] and not activo:
                activo=True;c.captura(run/'inicio.png');print('INICIO',json.dumps(r),flush=True)
            for objetivo in sorted(fotos-hechas):
                if activo and r['reloj']>=objetivo:
                    hechas.add(objetivo);c.captura(run/f'cuadro_{objetivo}.png')
                    (run/f'cuadro_{objetivo}.json').write_text(json.dumps(r,indent=2))
                    if a.gs_dump and dump_solicitado is None:
                        dump_solicitado=dict(campo=r['campo'],reloj=r['reloj'],foto=objetivo,
                            alcance='solicitud F9; identificar despuÃ©s los campos/transferencias efectivos en el volcado')
                        c.pulso(0x78) # F9: GSDumpSingleFrame, sin pasos ocultos
            if activo and not r['evento'] and fin_en is None:
                cerrado=True;fin_en=i;c.captura(run/'fin.png');print('FIN',json.dumps(r),flush=True)
            if fin_en is not None and i-fin_en>=a.tras_fin:
                if a.tras_fin:c.captura(run/'tras_fin.png')
                break
            c.paso()
        print('RESULTADO',json.dumps(dict(ruta=str(run),inicio=activo,fin=cerrado,muestras=len(filas),ultimo=filas[-1])),flush=True)
        return 0 if cerrado else 2
    finally:
        (run/'traza.json').write_text(json.dumps(filas,indent=2),encoding='utf-8')
        if p:p.s.close()
        if proc:
            P.cerrar_pid(proc.pid);proc.wait(timeout=20);s.TURNO.liberar(proc.pid)
        elif not a.pool:s.TURNO.liberar(os.getpid(),fuerza=True)
        if not a.pool:j._soltar_plaza()
        if a.gs_dump:
            dumps=[]
            for q in (perfil/'snaps').iterdir():
                if q.is_file() and '.gs' in q.name and q.name not in dumps_antes:
                    shutil.copy2(q,run/q.name);dumps.append(dict(archivo=q.name,bytes=q.stat().st_size))
            (run/'gs_dump.json').write_text(json.dumps(dict(solicitud=dump_solicitado,
                archivos=dumps,aceptacion_escena=False),indent=2),encoding='utf-8')
        if a.gs_dump or a.gs_software:
            log=perfil/'logs/emulog.txt'
            if log.exists():shutil.copy2(log,run/'emulog.txt')


if __name__=='__main__':sys.exit(main())
