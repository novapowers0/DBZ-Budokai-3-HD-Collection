"""IW: 16 casos pendientes (18 con --todos), RAM y cámara por cuadro lógico.

--validar y --help son offline: no importan sonda/pruebas ni abren aplicaciones.
original usa exclusivamente gp2/PINE28033 y slot4; port usa pruebas.py/copia sol.
No cambia clips del original. Las esperas de reloj son SOLO plazos de I/O/menú.
"""
import argparse
import configparser
import ctypes
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time

ROOT = Path(r'C:/Users/javie/Desktop/PROYECTOS IA/DBZ Budokai 3 HD Collection')
HOME = Path('D:/tmp/solGP2/camaras')
EMU = Path('D:/tmp/solGP2/pcsx2_gp2')
SOURCE = Path('D:/DBZ3HD/_pcsx2_pool/base_iw')
SONDA = Path(__file__).resolve().parent   # sonda.py vive ahora en tools/pcsx2_sonda.py
PORT = 28033
COUNT = 0x4A4D40
INDICES = [0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18]
CASOS = dict(zip(range(0x7D0, 0x7E2), INDICES))
PENDIENTES = [k for k in CASOS if k not in (0x7D2, 0x7DD)]
STATE = 'SLES-55347 (335A5A1F).04.p2s'
READY_LOG = re.compile(r'campo READY tras presentacion|entrada acabada: mando al jugador|'
    r'iwdisparo: juego .* empieza|r14: en juego|READY\? terminado; empieza el vuelo')


def guardar(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    # Cada ejecución tiene carpeta exclusiva; no reemplazar evidencia anterior.
    with path.open('x', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=1, allow_nan=False)


def valido(x):
    return 0x100000 <= x < 0x2000000


def flotante(x):
    f = struct.unpack('<f', x.to_bytes(4, 'little'))[0]
    return f if math.isfinite(f) else None


def floats(raw, endian='<'):
    return [x if math.isfinite(x) else None for x in struct.unpack(endian + str(len(raw)//4) + 'f', raw)]


def delta(n, inicio):
    return (n - inicio) & 0xFFFFFFFF


def familia(idx):
    return 'vuelo' if idx in (7, 17) else 'visor' if idx in (3, 8, 9, 15) else 'ui'


def recuperar():
    """Recuperación única autorizada por PID y CommandLine, jamás por imagen."""
    raw=subprocess.run(['powershell','-NoProfile','-Command',
        "Get-CimInstance Win32_Process -Filter 'ProcessId=19144 OR ProcessId=31328' | Select-Object ProcessId,ExecutablePath,CommandLine | ConvertTo-Json -Compress"],
        capture_output=True,check=True).stdout.decode('utf-8',errors='replace')
    rows=json.loads(raw)
    rows=rows if isinstance(rows,list) else [rows]
    by={x['ProcessId']:x for x in rows}
    assert by[19144]['CommandLine']=='C:\\Python314\\python.exe -B D:/tmp/solGP2/camaras/iw_camaras_gp2.py original --continuar',by
    assert os.path.normcase(by[31328]['ExecutablePath'])==os.path.normcase(str(EMU/'pcsx2.exe').replace('/','\\')),by
    assert '-statefile' in by[31328]['CommandLine'] and STATE in by[31328]['CommandLine']
    import pcsx2_cuadros as C
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32]
    k.OpenProcess.restype=ctypes.c_void_p
    k.TerminateProcess.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
    k.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
    k.CloseHandle.argtypes=[ctypes.c_void_p]
    def stop(pid):
        h=k.OpenProcess(0x100001,False,pid)
        if not h:raise ctypes.WinError(ctypes.get_last_error())
        try:
            if not k.TerminateProcess(h,0):raise ctypes.WinError(ctypes.get_last_error())
            if k.WaitForSingleObject(h,15000)!=0:raise RuntimeError('No terminó PID '+str(pid))
        finally:k.CloseHandle(h)
    out=HOME/f'recuperacion_{time.time_ns()}'
    out.mkdir()
    guardar(out/'procesos_verificados.json',rows)
    stop(19144)
    s=sonda_privada()
    c=None
    try:
        os.environ['DBZ3_PCSX2_CUADROS_RESPALDO']=str(HOME/'respaldos')
        c=C.Cuadros(s,COUNT)
        foto=foto_original(c,0,out,'cuadro_recuperado',None)
        guardar(out/'medida.json',dict(origen='interrupción avance9000, sin atribuir f60',foto=foto))
        print('RECUPERADO',out,foto['antes']['n'],flush=True)
    finally:
        if c:c.p.s.close()
        stop(31328)
        s.TURNO.liberar(31328)
        backup=HOME/'PCSX2_gp2.ini.original'
        if backup.exists():
            shutil.move(str(EMU/'inis/PCSX2.ini'),str(out/'PCSX2_interrumpido.ini'))
            shutil.move(str(backup),str(EMU/'inis/PCSX2.ini'))


def leer_original(p, idx):
    """Un lote de punteros y UN lote de datos con contador antes/después."""
    roots=(0x49CA70,0x49CBBC,0x49CD8C,0x49CBA4,0x49CE3C)
    ptrs=p.lote([(x,4) for x in roots])
    mgr,cam,ui,ctx,g=ptrs
    req=[(COUNT,4)]
    def add(addr,size=4):
        req.append((addr,size))
    if valido(mgr):
        for o in range(0x70,0xB0,4):add(mgr+o)
    modelos=[]
    if idx in (7,17) and valido(g):
        modelos=p.lote([(g+0x548,4),(g+0x7A0,4)])
        for o in (0x724,0x54C,0x550,0x554):add(g+o)
        for m in modelos:
            if valido(m):
                for o in (0x268,0x270,0x278):add(m+o)
    elif idx not in (7,17):
        base,off=(ctx,0x8C) if idx in (3,8,9,15) else (ui,0x150)
        if valido(base):add(base+off)
        if valido(mgr):
            for x in (mgr+0x800,mgr+0x28D0):
                for o in (0xB60,0xC64,0xC68,0xC6C,0x40,0x44,0x48):add(x+o)
                add(x+0xCBC,2)
    if valido(cam) and idx in (0,1,2,10,14,16):
        for o in (0x114,0x118,0x12C,0x80,0x84,0x88,0x90,0x94,0x98):add(cam+o)
    for addr in roots:add(addr)
    add(COUNT)
    vals=p.lote(req)
    mem={a:v for (a,_),v in zip(req[1:-1],vals[1:-1])}
    n0,n1=vals[0],vals[-1]
    u=lambda a:mem[a]
    ff=lambda a:flotante(u(a))
    vec=lambda a,n:[ff(a+i*4) for i in range(n)]
    d = dict(n=n0, familia=familia(idx), actores=[], ready=None,
             punteros=dict(mgr=mgr, campo=cam, ui=ui, contexto=ctx, vuelo=g))
    if idx in (7, 17):
        d['ready_fuente'] = '*(49CE3C): +724==FFFFFFFF,+54C==1,+550==0 (juego)'
        if valido(g):
            flags = [u(g+o) for o in (0x724, 0x54C, 0x550)]
            d['ready_flags'] = flags
            d['ready'] = flags == [0xFFFFFFFF, 1, 0]
            for rol,m in enumerate(modelos):
                if not valido(m):
                    d['actores'].append(None)
                    continue
                d['actores'].append(dict(codigo=u(g+0x554) if rol == 0 else None,
                    clip=u(m+0x268), f=ff(m+0x270), dur=u(m+0x278), modelo=m))
    else:
        base, off = (ctx, 0x8C) if idx in (3, 8, 9, 15) else (ui, 0x150)
        d['ready_fuente'] = 'contexto49CBA4+8C&2' if idx in (3, 8, 9, 15) else 'UI49CD8C+150&2'
        if valido(base):
            flags = u(base+off)
            d.update(ready=bool(flags & 2), ready_flags=flags)
        if valido(mgr):
            for x in (mgr+0x800, mgr+0x28D0):
                d['actores'].append(dict(codigo=u(x+0xB60),clip=u(x+0xC64),f=ff(x+0xC68),
                    dur=u(x+0xC6C),estado=u(x+0xCBC),pos=vec(x+0x40,3)))
    # La matriz común existe también con cámara de campo nula. No es un
    # sustituto inventado de ojo/objetivo; se conserva como canal independiente.
    d['camara'] = dict(matriz_comun=vec(mgr+0x70,16) if valido(mgr) else None,
        matriz_fuente='*(49CA70)+70', ojo=None, objetivo=None, fov=None,
        limite_fov='offset FOV PS2 no validado; no se inventa')
    if valido(cam) and idx in (0, 1, 2, 10, 14, 16):
        d['camara'].update(ojo=vec(cam+0x80,3),
            objetivo=vec(cam+0x90,3), clip=u(cam+0x114), f=ff(cam+0x118), animada=u(cam+0x12C),
            fuente='campo *(49CBBC), ojo+80/objetivo+90')
    d['n_despues'] = n1
    d['estable'] = n0==n1 and [u(x) for x in roots]==ptrs
    return d


def actor(d):
    return (d.get('actores') or [None])[0]


def foto_original(c, idx, out, etiqueta, objetivo, referencia=None):
    c.pausa()
    antes = leer_original(c.p, idx)
    meta = c.captura(out / (etiqueta+'.png'))
    despues = leer_original(c.p, idx)
    exacta = (antes['estable'] and despues['estable'] and
        antes['n'] == despues['n'] == meta['cuadro'] == meta['contador_en_estado'])
    return dict(etiqueta=etiqueta, objetivo_logico=objetivo, referencia=referencia,
                antes=antes, despues=despues, foto=meta, exacta=exacta)


def puerto_libre():
    with socket.socket() as s:
        if os.name == 'nt':
            s.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        s.bind(('127.0.0.1', PORT))


def clonar():
    """Solo crea gp2 al ejecutarse original. BIOS y tarjetas nunca se copian."""
    if not (SOURCE/'pcsx2.exe').is_file() or not (SOURCE/'sstates'/STATE).is_file():
        raise RuntimeError('Falta el clon pul o el slot4 IW de kit')
    EMU.mkdir(parents=True, exist_ok=True)
    for src in SOURCE.iterdir():
        dst = EMU/src.name
        if src.is_file() and not dst.exists():
            shutil.copy2(src, dst)
    for name in ('QtPlugins', 'resources'):
        dst = EMU/name
        if not dst.exists():
            subprocess.run(['cmd', '/c', 'mklink', '/J', str(dst), str(SOURCE/name)],
                           check=True, capture_output=True)
    for name in ('inis','sstates','snaps','logs','cache','gamesettings','inputprofiles'):
        (EMU/name).mkdir(exist_ok=True)
    if not (EMU/'inis/PCSX2.ini').exists():
        shutil.copy2(SOURCE/'inis/PCSX2.ini', EMU/'inis/PCSX2.ini')
    if not (EMU/'sstates'/STATE).exists():
        shutil.copy2(SOURCE/'sstates'/STATE, EMU/'sstates'/STATE)


def sonda_privada():
    # Módulo propio: no se reconfigura el import global compartido de sonda.
    sys.path.insert(0, str(SONDA))
    spec = importlib.util.spec_from_file_location('_solGP2_sonda', SONDA/'pcsx2_sonda.py')
    s = importlib.util.module_from_spec(spec)
    old = os.environ.pop('DBZ3_PCSX2_INSTANCIA', None)
    try:
        spec.loader.exec_module(s)
    finally:
        if old is not None:
            os.environ['DBZ3_PCSX2_INSTANCIA'] = old
    from pcsx2_turnos import Turno, process_image
    s.INSTANCIAS['gp2'] = (PORT, str(EMU), str(HOME/'capturas'), 'instancia_gp2')
    s.seleccionar_instancia(['--instancia','gp2'])
    s.TURNO = Turno('instancia_gp2', PORT, s.EXE, cuarentena=HOME/'respaldos/turnos')
    s._exe_de = process_image
    # tasklist emite la pagina local de Windows aunque PYTHONUTF8=1.
    # No modificar sonda.py ni el modulo global compartido.
    def pids_locales():
        raw = subprocess.run(['tasklist','/FI','IMAGENAME eq pcsx2.exe','/FO','CSV','/NH'],
            capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW, check=True).stdout
        return [int(p[1]) for ln in raw.decode('mbcs', errors='replace').splitlines()
                if len(p := ln.split('\",\"')) > 1]
    s._pids = pids_locales
    from ctypes import wintypes as wt
    s.u32dll.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
    s.u32dll.PostMessageW.restype = wt.BOOL
    return s


def configurar(ini):
    cfg = configparser.ConfigParser(interpolation=None)
    cfg.optionxform = str
    cfg.read(ini, encoding='utf-8-sig')
    values = {'EmuCore': dict(EnablePINE='true', PINESlot=str(PORT), SaveStateOnShutdown='false'),
        'SPU2/Output': dict(Backend='Null', OutputMuted='true', StandardVolume='0', FastForwardVolume='0'),
        'Hotkeys': dict(TogglePause='Keyboard/F6', FrameAdvance='Keyboard/F7', Screenshot='Keyboard/F8'),
        'UI': dict(PauseOnFocusLoss='false', StartFullscreen='false', ConfirmShutdown='false'),
        'MemoryCards': dict(Slot1_Enable='false', Slot2_Enable='false'),
        'Folders': dict(Bios=str(SOURCE/'bios'), MemoryCards=str(SOURCE/'memcards'),
                        Savestates='sstates', Snapshots='snaps', Logs='logs', Cache='cache')}
    for section, vals in values.items():
        if not cfg.has_section(section):
            cfg.add_section(section)
        cfg[section].update(vals)
    with ini.open('w', encoding='utf-8') as f:
        cfg.write(f)


def tomar_realtime(c,idx,out,a,data,limite):
    p=c.p
    anterior=None
    armado=False
    prev_intro=None
    last_sig=None
    ultimo_false=None
    while time.monotonic()<limite:
        d=leer_original(p,idx)
        if not d['estable']:
            data['descartadas']=data.get('descartadas',0)+1
            time.sleep(.006)
            continue
        ac=actor(d)
        if anterior and d['n']==anterior['n']:
            time.sleep(.008)
            continue
        data['filas'].append(d)
        if 'primero' not in data:data['primero']=d
        data['ultimo']=d
        sig=(ac.get('estado'),ac.get('codigo'),d['ready']) if ac else (None,None,d['ready'])
        if sig!=last_sig:
            print(f"{data['clave']:X} n={d['n']} estado/codigo/ready={sig} f={ac.get('f') if ac else None}",flush=True)
            last_sig=sig
        if d['ready'] is False:
            armado=True
            ultimo_false=d
        if d['ready'] and armado:
            if getattr(a,'solo_memoria',False):
                data['ready']=dict(n=d['n'],fuente=d['ready_fuente'],
                    n_ultimo_false=ultimo_false['n'] if ultimo_false else None,
                    incertidumbre_campos=delta(d['n'],ultimo_false['n']) if ultimo_false else None,
                    ultimo_false=ultimo_false,primero_true=d,
                    limite='Flanco acotado por sondeos; RAM en ejecución, sin foto')
                guardar(out/'ready_actual.json',dict(datos=d,ancla=data['ready']))
                print(f"{data['clave']:X} READY RAM n={d['n']} false={data['ready']['n_ultimo_false']}; esperando50campos realtime",flush=True)
                while time.monotonic()<limite:
                    m=leer_original(p,idx)
                    if m['estable'] and delta(m['n'],d['n'])>=50:
                        edad=delta(m['n'],d['n'])*1.2
                        sample=dict(datos=m,edad_logica=edad,desfase_logico=edad-60,
                            exacta_por_contador=True,foto=False,ancla=data['ready'])
                        data['memoria_ready60']=sample
                        data['ultimo']=m
                        data['filas'].append(m)
                        guardar(out/'ready_juego60_memoria.json',sample)
                        print(f"{data['clave']:X} READY+60 RAM n={m['n']} edad={edad} desfase={edad-60}",flush=True)
                        return
                    time.sleep(.006)
                raise TimeoutError('Plazo por clave esperando muestra RAM READY+60')
            c.pausa()
            detenido=leer_original(p,idx)
            if not detenido['estable'] or not detenido['ready']:
                raise RuntimeError('Flanco READY desapareció antes de pausa; no se inventa ancla')
            data['ready']=dict(n=detenido['n'],fuente=detenido['ready_fuente'],
                n_detectado=d['n'],n_ultimo_false=anterior['n'] if anterior else None,
                incertidumbre_campos=delta(detenido['n'],anterior['n']) if anterior else None,
                limite='Ancla real pausada; el inicio exacto del flanco está dentro del intervalo observado')
            guardar(out/'ready_actual.json',dict(datos=detenido,ancla=data['ready']))
            print(f"{data['clave']:X} READY pausado n={detenido['n']}; SOLO50campos",flush=True)
            start=detenido['n']
            for _ in range(50):
                if time.monotonic()>=limite:raise TimeoutError('Plazo por clave durante50campos')
                c.paso()
            foto=foto_original(c,idx,out,'ready_juego60',60,data['ready'])
            edad=delta(foto['antes']['n'],start)*1.2
            foto.update(edad_logica=edad,desfase_logico=edad-60,
                limite_edad='60 desde READY observado y pausado; no desde flanco no observado')
            data['fotos'].append(foto)
            guardar(out/'ready_juego60.json',foto)
            return
        eligible=(ac and ac.get('dur',0)>0 and ac.get('f') is not None and
                  ac.get('codigo') is not None and not d['ready'])
        if idx in (0,1,2,10,14,16):
            eligible=eligible and ac.get('estado')==0x2C
        if eligible:
            sig_intro=(ac['codigo'],ac.get('clip'))
            crossed=(prev_intro and prev_intro[0]==sig_intro and prev_intro[1]<60<=ac['f'])
            if crossed and not data['fotos']:
                c.pausa()
                foto=foto_original(c,idx,out,'presentacion',60)
                real=actor(foto['antes'])
                foto.update(codigo=real['codigo'],clip=real.get('clip'),f_real=real['f'],
                    desfase_logico=real['f']-60,cruce_observado=dict(n=d['n'],f=ac['f']),
                    exacta_f60=False,limite='Foto exacta por contador ACTUAL, no se afirma cuadro60 exacto')
                data['fotos'].append(foto)
                guardar(out/'presentacion.json',foto)
                print(f"{data['clave']:X} PRESENTACION n={foto['antes']['n']} fREAL={real['f']}",flush=True)
                c.pulso(0x75)
            prev_intro=(sig_intro,ac['f'])
        else:
            prev_intro=None
        anterior=d
        time.sleep(.008)
    raise TimeoutError('Plazo realtime por clave: READY/presentación no observados; sin reintento')


def original(a, claves, run):
    import pruebas as P
    import pcsx2_cuadros as C
    s = sonda_privada()
    if s.nuestro_pid() and not a.adoptar_gp2:
        raise RuntimeError('gp2 ya ocupada; no se conecta ni se cierra')
    # Solo plaza general de emulador; no inicializar Juego('sol') ni bloquear copia sol.
    reserva = object.__new__(P.Juego)
    reserva.nombre='solGP2_original'
    reserva.dest=EMU
    while not reserva._tomar_plaza(False):
        time.sleep(5)
    own = []
    c = p = None
    ini = EMU/'inis/PCSX2.ini'
    backup = HOME/'PCSX2_gp2.ini.original'
    preparado = False
    try:
        if s.nuestro_pid() and not a.adoptar_gp2:
            raise RuntimeError('gp2 ocupada durante la espera')
        if not a.adoptar_gp2:
            puerto_libre()
            clonar()
            if backup.exists():
                raise RuntimeError('Configuración pendiente: restaurar primero '+str(backup))
            shutil.copy2(ini, backup)
        elif not backup.exists():
            raise RuntimeError('Adopción propia sin respaldo de configuración')
        preparado = True
        if not a.adoptar_gp2:configurar(ini)
        ini_original_sha256=hashlib.sha256(backup.read_bytes()).hexdigest()
        guardar(run/'instancia.json', dict(instancia='gp2', pine=PORT, fuente=str(SOURCE),
            exe=str(EMU/'pcsx2.exe'), estado=str(EMU/'sstates'/STATE), copia_reserva='plaza general gp2',
            claves=[hex(k) for k in claves], timeout_por_clave=a.timeout, solo_memoria=a.solo_memoria,
            plazo_total=a.plazo_total, ini_original_sha256=ini_original_sha256,
            bios_tarjetas='rutas compartidas; tarjetas desactivadas; sin duplicación'))
        # Proxy SOLO dentro del módulo privado, nunca monkeypatch subprocess global.
        class SubprocessPropio:
            def __getattr__(self, k):
                return getattr(subprocess, k)
            def Popen(self, *args, **kw):
                proc = subprocess.Popen(*args, **kw)
                own.append(proc)
                return proc
        s.subprocess = SubprocessPropio()
        if a.adoptar_gp2:
            if a.adoptar_gp2!=34076 or s.nuestro_pid()!=34076:
                raise RuntimeError('Adopción limitada al Popen propio34076 verificado')
            from pcsx2_turnos import process_image
            if os.path.normcase(process_image(34076))!=os.path.normcase(str(EMU/'pcsx2.exe')):
                raise RuntimeError('Ruta de VM propia cambió')
            class PropioAdoptado:
                pid=34076
                def __init__(self):
                    self.k=ctypes.WinDLL('kernel32',use_last_error=True)
                    self.k.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32]
                    self.k.OpenProcess.restype=ctypes.c_void_p
                    self.k.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
                    self.k.TerminateProcess.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
                    self.h=self.k.OpenProcess(0x100001,False,self.pid)
                    if not self.h:raise ctypes.WinError(ctypes.get_last_error())
                def poll(self):return None if self.k.WaitForSingleObject(self.h,0)==258 else 0
                def terminate(self):self.k.TerminateProcess(self.h,0)
                kill=terminate
                def wait(self,timeout):
                    if self.k.WaitForSingleObject(self.h,int(timeout*1000))!=0:
                        raise subprocess.TimeoutExpired('gp2',timeout)
            own.append(PropioAdoptado())
        else:
            p = s.lanzar('iw', 4)
            p.s.close()
        c = C.Cuadros(s, COUNT)
        p = c.p
        P.U32.GetMonitorInfoW.argtypes = [P.wt.HMONITOR, ctypes.POINTER(P._MONINFO)]
        if not P._pantalla_secundaria():
            raise RuntimeError('No se encuentra pantalla2')
        P._a_pantalla_secundaria(own[0])
        os.environ['DBZ3_PCSX2_CUADROS_RESPALDO'] = str(HOME/'respaldos')
        limite_total=time.monotonic()+a.plazo_total if a.plazo_total>0 else math.inf
        guardar(run/'proceso_propio.json',dict(script_pid=os.getpid(),vm_pid=own[0].pid,
            exe=str(EMU/'pcsx2.exe'),pine=PORT))
        for clave in claves:
            out = run/f'original_{clave:X}'
            out.mkdir()
            idx = CASOS[clave]
            filas, fotos, errores = [], [], []
            data = dict(clave=clave, indice=idx, filas=filas, fotos=fotos, errores=errores,
                factor_pal=1.2, ready=None, completa=False)
            try:
                limite=min(limite_total,time.monotonic()+a.timeout)
                if time.monotonic()>=limite:raise TimeoutError('Plazo total original agotado')
                if p.status() == 1:
                    c.pulso(0x75)
                p.cargar(4)
                time.sleep(2.5)  # restauración del menú, nunca emparejamiento
                fallos_hilo = []
                def menu():
                    try:
                        s.teclas('right espera300 z espera800 down down z')
                    except Exception as exc:
                        fallos_hilo.append(str(exc))
                th = threading.Thread(target=menu)
                th.start()
                selected = False
                try:
                    fin = min(limite,time.monotonic()+20)
                    while time.monotonic() < fin:
                        if p.u32(0x5C0DC8) == 0x7D0:
                            # Solo índice y clave: jamás escribir código/clip de actor.
                            p.escribir(0x5C0DC4, struct.pack('<II',idx,clave))
                            selected = True
                            break
                        time.sleep(.015)
                finally:
                    th.join()
                if not selected or fallos_hilo:
                    raise RuntimeError('Selección fallida: '+repr(fallos_hilo))
                tomar_realtime(c,idx,out,a,data,limite)
                if data['ready'] is None:
                    errores.append('No observado READY/juego dentro del límite de campos')
                if not any(x['etiqueta']=='presentacion' for x in fotos):
                    errores.append('Sin presentación identificable por código/cuadro; no se eligió un clip artificial')
                data['completa'] = (len(fotos)==2 or (len(fotos)==1 and 'memoria_ready60' in data)) and all(x['exacta'] for x in fotos) and not errores
            except Exception as exc:
                errores.append(str(exc))
            finally:
                guardar(out/'medida.json',data)
            print(f'{clave:X}: completa={data["completa"]} n_primero={data.get("primero",{}).get("n")} n_ultimo={data.get("ultimo",{}).get("n")} fotos={len(fotos)} errores={errores}', flush=True)
            if errores and not a.continuar:
                break
    finally:
        try:
            if p:
                p.s.close()
        finally:
            for proc in own:
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait(timeout=10)
            try:
                if own and all(x.poll() is not None for x in own):
                    s.TURNO.liberar(own[-1].pid)
            finally:
                try:
                    if preparado and all(x.poll() is not None for x in own):
                        archive = HOME/'respaldos'
                        archive.mkdir(exist_ok=True)
                        shutil.move(str(ini),str(archive/f'PCSX2_usado_{time.time_ns()}.ini'))
                        shutil.move(str(backup),str(ini))
                finally:
                    reserva._soltar_plaza()
                    if preparado and all(x.poll() is not None for x in own):
                        sha=hashlib.sha256(ini.read_bytes()).hexdigest()
                        guardar(run/'cierre.json',dict(vm_pids=[x.pid for x in own],
                            procesos_cerrados=True,plaza_liberada=getattr(reserva,'plaza',None) is None,
                            configuracion_restaurada=sha==ini_original_sha256,
                            ini_sha256=sha,ini_original_sha256=ini_original_sha256))


def leer_port(P, j):
    mgr, n = P._freecam_mem(j)
    if not mgr or n is None or not math.isfinite(n):
        return None
    def read(addr, size):
        b = P.leer(j.h, P.GBASE+addr, size)
        if not b or len(b)!=size:
            raise RuntimeError(f'Lectura guest incompleta {addr:X}')
        return b
    d = dict(n=n, actores=[])
    for x in (mgr+2096,mgr+2096+8240):
        raw = read(x+0x650,0x610)
        d['actores'].append(dict(codigo=struct.unpack_from('>I',raw,0x520)[0]&0xFFF,
            clip=struct.unpack_from('>I',raw,0x604)[0], f=floats(raw[0x608:0x60C],'>')[0]))
    cam = int.from_bytes(read(mgr+0x264,4),'big')
    d['camara'] = dict(matriz_comun=floats(read(mgr+0x60,64),'>'),
        matriz_fuente='guest mgr+60 (vista render)', ojo=None,objetivo=None,fov=None)
    if 0x82000000 <= cam < 0x83000000:
        raw = read(cam,0x3C)
        d['camara'].update(ojo=floats(raw[:12],'>'),objetivo=floats(raw[0x10:0x1C],'>'),
            fov=floats(raw[0x34:0x38],'>')[0], fuente='guest *(mgr+264), +0/+10/+34')
    mgr2, n2 = P._freecam_mem(j)
    d.update(n_despues=n2,estable=mgr==mgr2 and n==n2)
    return d


def aliases():
    """Extrae solo entradas literales de la tabla real; conserva fuente/banco.

    Expresiones simbólicas y otros bancos NO se adivinan: se usa fase/edad.
    """
    path = ROOT/'src/anim_iw.cpp'
    text = path.read_text(encoding='utf-8-sig')
    block = text.split('constexpr Extra kExtras[] =',1)[1].split('};',1)[0]
    block = re.sub(r'//[^\n]*','',block)
    out = {}
    for code, bank, alias in re.findall(r'\{\s*(0x[\dA-Fa-f]+|\d+)\s*,\s*(\d+)\s*(?:,\s*(0x[\dA-Fa-f]+|\d+)\s*)?\}',block):
        code, bank = int(code,0),int(bank)
        alias = int(alias,0) if alias else code
        out.setdefault(alias,[]).append(dict(codigo_iw=code,banco=bank))
    return out


def port_run(a, claves, run):
    import pruebas as P
    mapa = aliases()
    guardar(run/'traduccion.json',dict(fuente=str(ROOT/'src/anim_iw.cpp'),tabla=mapa,
        limite='Solo literales kExtras; banco PS2 no identificado => fase/edad READY; ningún A31 genérico'))
    for clave in claves:
        refpath = a.referencia/f'original_{clave:X}/medida.json'
        if not refpath.exists():
            raise RuntimeError('Falta referencia '+str(refpath))
        ref = json.loads(refpath.read_text(encoding='utf-8'))
        out = run/f'port_{clave:X}'
        out.mkdir()
        def env(_):
            return dict(DBZ3_TEST_IWMINI=hex(clave), DBZ3_TEST_TURBO='1',
                DBZ3_PS2_GAMES=os.environ.get('DBZ3_PS2_GAMES','D:/DBZ3HD/ps2_games'),
                DBZ3_TEST_IWMINI_CAMLOG='1', DBZ3_TEST_IWCAMTRAZA=str(out/'camara.csv'))
        def jugar(j,_):
            filas, fotos, errores = [], [], []
            ready = None
            anterior = None
            ultimo = None
            limite = time.monotonic()+a.timeout
            data = dict(clave=clave, filas=filas,fotos=fotos,errores=errores, ready=None,
                run=str(j.run),referencia=str(refpath),congelacion_tick=False,completa=False)
            try:
                while time.monotonic()<limite:
                    d = leer_port(P,j)
                    if not d or not d['estable'] or d['n']==ultimo:
                        if d and not d['estable']:
                            data['lecturas_cruzadas_descartadas']=data.get('lecturas_cruzadas_descartadas',0)+1
                        time.sleep(.002)
                        continue
                    d['salto_logico']=d['n']-ultimo if ultimo is not None else None
                    if ultimo is not None and d['n']<ultimo:
                        errores.append(f'El contador guest retrocedió: {ultimo} -> {d["n"]}; se pierde ancla READY')
                        ready=None
                    ultimo=d['n']
                    texto=j.texto_log()
                    match=READY_LOG.search(texto)
                    if match and ready is None:
                        ready=dict(n=d['n'], n_anterior=anterior['n'] if anterior else None,
                            fuente=match.group(0), limite='Flanco observado por log; no tick exacto',
                            incertidumbre_log_no_acotada=True)
                        data['ready']=ready
                    d['fase']='ready_juego' if ready else 'presentacion'
                    d['edad_ready']=d['n']-ready['n'] if ready else None
                    d['traduccion_candidata']=mapa.get(actor(d)['codigo'],[])
                    filas.append(d)
                    if a.capturar_port and ready and d['edad_ready']>=a.cuadro and not fotos:
                        antes=leer_port(P,j)
                        path=j.captura('gp2_ready_juego60')
                        despues=leer_port(P,j)
                        fotos.append(dict(etiqueta='ready_juego60',archivo=str(path),antes=antes,despues=despues,
                            amplitud_cuadros=despues['n']-antes['n'],exacta=False,
                            limite='Tick no congelado: ilustrativa; >1 cuadro nunca exacta'))
                    if ready and d['edad_ready']>=a.cuadro+30:
                        data['completa']=True
                        break
                    anterior=d
                    time.sleep(.002)
                if ready is None:
                    errores.append('No se observó READY/juego; no se empareja por reloj')
                if not data['completa']:
                    errores.append('No se alcanzó la edad lógica pedida')
            finally:
                guardar(out/'medida.json',data)
        nombre=f'solGP2_camara_{clave:X}'
        P.ESCENARIOS[nombre]=('IW cámara por contador, copia sol fija',env,jugar)
        result=P.correr('sol',nombre,[],None,{'timeout':str(a.timeout+200)},True)
        guardar(out/'resultado.json',result)
        print(P.tabla([result]),flush=True)
        if not result['ok'] and not a.continuar:
            break


def comparar(a, claves):
    """Sin interpolar huecos, sin convertir milisegundos CSV en cuadros."""
    reporte=[]
    for clave in claves:
        row=dict(clave=f'{clave:X}',pares=[],errores=[])
        try:
            orig=json.loads((a.referencia/f'original_{clave:X}/medida.json').read_text(encoding='utf-8'))
            port=json.loads((a.port/f'port_{clave:X}/medida.json').read_text(encoding='utf-8'))
            ready=port.get('ready')
            for foto in orig['fotos']:
                od=foto['antes']
                candidatos=[]
                if ready:
                    edad=(delta(od['n'],orig['ready']['n'])*1.2 if foto['etiqueta']=='ready_juego60'
                          else -delta(orig['ready']['n'],od['n'])*1.2)
                    for pd in port['filas']:
                        pa=pd['n']-ready['n']
                        if (edad>=0)==(pa>=0):
                            candidatos.append((abs(pa-edad),pd,pa-edad))
                if not candidatos:
                    row['errores'].append(foto['etiqueta']+': sin pareja de fase/edad READY')
                    continue
                _,pd,desfase=min(candidatos,key=lambda t:t[0])
                par=dict(etiqueta=foto['etiqueta'],metodo='fase/edad por contador, READY relativo',
                    original_n=od['n'],port_n=pd['n'],desfase_logico=desfase,
                    original_codigo=actor(od),port_codigo=actor(pd),
                    exacta=False,limite='READY port observado por log; banco original no identificado; no certifica igualdad de clip')
                for key in ('ojo','objetivo','fov','matriz_comun'):
                    u,v=od['camara'].get(key),pd['camara'].get(key)
                    if u is None or v is None:
                        par['error_'+key]=None
                        continue
                    us,vs=(u,v) if isinstance(u,list) else ([u],[v])
                    par['error_'+key]=(math.dist(us,vs) if len(us)==len(vs) and all(x is not None for x in us+vs) else None)
                par['limite_matriz']='Norma cruda de matrices; convenciones PS2/B3 no certificadas, no error visual'
                row['pares'].append(par)
            row['errores']+=orig.get('errores',[])+port.get('errores',[])
        except (OSError,KeyError,TypeError,ValueError) as exc:
            row['errores'].append(str(exc))
        reporte.append(row)
    print(json.dumps(reporte,ensure_ascii=False,indent=1))
    if a.informe:
        guardar(a.informe,reporte)


def validar():
    # Pruebas del lector real con PINE falso: cero procesos, cero escrituras.
    class Fake:
        def __init__(self,idx):
            self.idx=idx
        def u32(self,a):
            ptr={COUNT:100,0x49CA70:0x100000,0x49CBBC:0,
                 0x49CD8C:0x200000,0x49CBA4:0x300000,0x49CE3C:0x400000}
            if a in ptr:return ptr[a]
            if a in (0x200150,0x30008C):return 2
            if a==0x400724:return 0xFFFFFFFF
            if a==0x40054C:return 1
            if a in (0x400548,0x4007A0):return 0x500000
            return 0
        def lote(self,pares):return [self.u32(x) for x,_ in pares]
        def leer(self,a,n):return bytes(n)
    assert len(CASOS)==18 and len(PENDIENTES)==16
    assert CASOS[0x7D6]==7 and CASOS[0x7E0]==17
    assert delta(3,0xFFFFFFFF)==4
    for idx in INDICES:
        d=leer_original(Fake(idx),idx)
        assert d['estable'] and d['ready'] and len(d['actores'])==2
        assert d['camara']['matriz_comun'] is not None
        assert d['camara']['ojo'] is None  # campo nulo no impide medir matriz
    deps=[ROOT/'tools'/f for f in ('pruebas.py','pcsx2_cuadros.py','iw_revision_Z.py','iw_animaciones_N.py','iw_temblor.py')]
    deps += [SONDA/'pcsx2_sonda.py',SONDA/'pcsx2_turnos.py',SOURCE/'pcsx2.exe',SOURCE/'sstates'/STATE]
    missing=[str(p) for p in deps if not p.is_file()]
    if missing:raise RuntimeError('Faltan dependencias: '+repr(missing))
    mapa=aliases()
    assert any(x['codigo_iw']==0x3B6 and x['banco']==924 for x in mapa[0xFA6])
    print('VALIDAR OK: 18 claves / 16 pendientes; familias READY, vuelo, cámara nula, contador y tabla fuente/banco.')
    print('Sin apps ni conexión PINE. Puerto28033 se comprobará al lanzar; no se declara reservado ahora.')


def main():
    for stream in (sys.stdout,sys.stderr):
        if hasattr(stream,'reconfigure'):
            stream.reconfigure(encoding='utf-8')
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('modo',nargs='?',choices=['enumerar','original','port','comparar','recuperar'],default='enumerar')
    ap.add_argument('--validar',action='store_true',help='pruebas offline de estructura/lector, sin apps')
    ap.add_argument('--todos',action='store_true',help='18, incluidos 7D2/7DD; por defecto solo16')
    ap.add_argument('--claves',help='lista hexadecimal, por ejemplo 0x7D0,0x7D1')
    ap.add_argument('--cuadro',type=float,default=60,help='cuadro lógico objetivo; PAL contador*1.2')
    ap.add_argument('--max-campos',type=int,default=9000,help='límite por original; avance exacto de uno en uno')
    ap.add_argument('--timeout',type=float,default=220,help='plazo por clave en segundos; no empareja por tiempo')
    ap.add_argument('--plazo-total',type=float,default=0,help='máximo segundos tanda tras abrir VM; 0 sin límite global')
    ap.add_argument('--adoptar-gp2',type=int,help=argparse.SUPPRESS)
    ap.add_argument('--solo-memoria',action='store_true',help='READY+60 por lote estable, sin foto ni FrameAdvance')
    ap.add_argument('--referencia',type=Path,help='carpeta de ejecución original')
    ap.add_argument('--port',type=Path,help='carpeta ejecución port para comparar')
    ap.add_argument('--informe',type=Path,help='JSON nuevo para comparación')
    ap.add_argument('--capturar-port',action='store_true',help='fotos ilustrativas con contadores antes/después; no exactas')
    ap.add_argument('--continuar',action='store_true',help='seguir con otras claves tras un fallo documentado')
    a=ap.parse_args()
    if a.modo=='recuperar':
        sys.path.insert(0,str(ROOT/'tools'))
        recuperar()
        return
    if a.validar:
        validar()
        return
    claves=[int(k,0) for k in a.claves.split(',')] if a.claves else list(CASOS) if a.todos else PENDIENTES
    if not claves or any(k not in CASOS for k in claves) or len(set(claves))!=len(claves):
        ap.error('claves inválidas o repetidas')
    if a.cuadro<=0 or a.max_campos<1 or a.timeout<=0 or a.plazo_total<0:
        ap.error('cuadro, max-campos y timeout deben ser positivos')
    if a.modo=='enumerar':
        print('\n'.join(f'0x{k:X} indice={CASOS[k]} ready={familia(CASOS[k])}' for k in claves))
        return
    if a.modo in ('port','comparar') and not a.referencia:
        ap.error('--referencia es obligatorio')
    if a.modo=='comparar':
        if not a.port:ap.error('--port es obligatorio')
        comparar(a,claves)
        return
    os.environ['DBZ3_AGENTE']='solGP2'
    os.environ['PYTHONUTF8']='1'
    sys.path.insert(0,str(ROOT/'tools'))
    run=HOME/f'{a.modo}_{time.time_ns()}'
    run.mkdir(parents=True)
    print('EVIDENCIA:',run,flush=True)
    if a.modo=='original':original(a,claves,run)
    else:port_run(a,claves,run)


if __name__=='__main__':
    main()
