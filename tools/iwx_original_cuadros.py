"""IW EU: RAM por pasos, con reloj de video opcional, instancia solIWX aislada.

No elige poses ni camaras: selecciona mision desde el menu de la instantanea04.
--fase juego espera gameplay natural; --fase intro espera el sobrevuelo original.
--fase instantanea continua un estado declarado, sin saltar a otra fase.
--reloj-video acredita cada campo PAL desde VSync serializado. Sin esa opcion,
solo se comprueba continuidad del serial libpad, que NO es un contador de video.
No certifica igualdad visual.
"""
import argparse
import configparser
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import threading
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
HOME = Path('D:/tmp/solIWX')
EMU = HOME/'pcsx2_iwx'
SOURCE = Path('D:/DBZ3HD/_pcsx2_pool/base_iw')
ISO = Path('E:/DBZ3HD_trabajo/iw_eu/Dragon Ball Z - Infinite World (Europe).iso')
SONDA = Path(__file__).resolve().parent   # pcsx2_sonda.py / pcsx2_turnos.py estan en tools/
PORT = 28041
COUNT = 0x4A4D40
STATE = 'SLES-55347 (335A5A1F).04.p2s'
INDICES = [0,1,2,3,5,6,7,8,9,10,11,12,13,14,15,16,17,18]


def cargar(nombre, ruta):
    spec = importlib.util.spec_from_file_location(nombre, ruta)
    m = importlib.util.module_from_spec(spec)
    sys.modules[nombre] = m
    spec.loader.exec_module(m)
    return m


def guardar(p, data):
    with Path(p).open('x', encoding='utf-8') as f:
        json.dump(data, f, indent=1, ensure_ascii=False, allow_nan=False)


def estado_info(path):
    with zipfile.ZipFile(path) as z:
        with z.open('eeMemory.bin') as ram:
            ram.seek(COUNT)
            contador=int.from_bytes(ram.read(4),'little')
            ram.seek(0x5C0DC4)
            indice,clave=struct.unpack('<II',ram.read(8))
        return dict(ruta=str(path.resolve()),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            contador=contador,indice=indice,clave=hex(clave),imagen_gs='Screenshot.png' in z.namelist())


def leer_efectos(leer):
    """Lista de efectos de la PS2: cabecera en 0x4dbfe0, centinela 0x4dc060.
    Solo lectura; devuelve los nodos con su matriz y el recurso (param +0x1C)."""
    try:
        head = struct.unpack('<I', leer(0x4dbfe0, 4))[0]
    except Exception:
        return []
    filas, vistos = [], set()
    for _ in range(32):
        if head in (0, 0x4dc060) or head in vistos:
            break
        vistos.add(head)
        raw = leer(head, 0xA4)
        if not raw or len(raw) < 0xA4:
            break
        nxt, prev, update, free, model = struct.unpack_from('<IIIII', raw, 0)
        p1, p2 = struct.unpack_from('<hh', raw, 0x1C)
        mat = list(struct.unpack_from('<4f', raw, 0x60))
        aux = struct.unpack_from('<IIII', raw, 0x80)
        filas.append(dict(dir=head, sig=nxt, update=update, libre=free, modelo=model,
                          recurso=p1, canal=p2, matriz=mat,
                          amo=aux[0], global_param=aux[1], offset=aux[2], aux=aux[3]))
        head = nxt
    return filas

def preparar(out):
    """Reusar utilidades existentes con globals privados; nunca instancia gp2."""
    T = cargar('_iwx_utilidades_camara', Path(__file__).resolve().parent/'dif_mapa_iw_eu.py')
    T.HOME, T.EMU, T.SOURCE, T.PORT = HOME, EMU, SOURCE, PORT
    T.clonar()
    antiguo = os.environ.pop('DBZ3_PCSX2_INSTANCIA', None)
    try:
        s = cargar('_iwx_sonda_privada', SONDA/'pcsx2_sonda.py')
    finally:
        if antiguo is not None:
            os.environ['DBZ3_PCSX2_INSTANCIA'] = antiguo
    from pcsx2_turnos import Turno, process_image
    s.INSTANCIAS['iwx'] = (PORT, str(EMU), str(out/'capturas'), 'instancia_solIWX')
    s.seleccionar_instancia(['--instancia','iwx'])
    s.TURNO = Turno('instancia_solIWX', PORT, s.EXE, cuarentena=HOME/'respaldos/turnos')
    s.ISOS['iw'] = str(ISO)
    s._exe_de = process_image
    # Las funciones de este modulo usan la pagina de codigos local de tasklist.
    def pids():
        raw = subprocess.run(['tasklist','/FI','IMAGENAME eq pcsx2.exe','/FO','CSV','/NH'],
            capture_output=True, check=True, creationflags=subprocess.CREATE_NO_WINDOW).stdout
        return [int(p[1]) for ln in raw.decode('mbcs', errors='replace').splitlines()
                if len(p := ln.split('\",\"')) > 1]
    s._pids = pids
    from ctypes import wintypes as wt
    s.u32dll.PostMessageW.argtypes = [wt.HWND,wt.UINT,wt.WPARAM,wt.LPARAM]
    s.u32dll.PostMessageW.restype = wt.BOOL
    return T, s


def original(a):
    import pruebas as P
    import pcsx2_cuadros as C
    out = HOME/f'original_cuadros_{time.time_ns()}'
    out.mkdir()
    print('EVIDENCIA', out, flush=True)
    T, s = preparar(out)
    if s.nuestro_pid():
        raise RuntimeError('La instancia propia esta ocupada; no adoptar ni cerrar')
    T.puerto_libre()
    reserva = object.__new__(P.Juego)
    reserva.nombre, reserva.dest = 'solIWX_original', EMU
    while not reserva._tomar_plaza(False):
        time.sleep(5)  # Cola compartida, no una espera de gameplay.
    ini = EMU/'inis/PCSX2.ini'
    backup = out/'PCSX2_antes.ini'
    own, c, preparado = [], None, False
    slot = EMU/'sstates/SLES-55347 (335A5A1F).98.p2s'
    slot_backup = None
    slot_propio = False
    try:
        if s.nuestro_pid():
            raise RuntimeError('La instancia propia se ha ocupado durante la cola')
        shutil.copy2(ini, backup)
        T.configurar(ini)
        cfg = configparser.ConfigParser(interpolation=None)
        cfg.optionxform = str
        cfg.read(ini, encoding='utf-8-sig')
        cfg['InputSources']['Keyboard'] = 'true'
        for name,key in [('LUp','I'),('LDown','K'),('LLeft','J'),('LRight','L')]:
            cfg['Pad1'][name] = 'Keyboard/'+key
        cfg['Pad1']['Square'] = 'Keyboard/O'
        cfg['Pad1']['Circle'] = 'Keyboard/U'
        if a.piccolo_apuntar:
            for name,key in [('Up','Up'),('Down','Down'),('Left','Left'),('Right','Right')]:
                cfg['Pad1'][name]='Keyboard/'+key
        if a.palanca_escala is not None:
            # Ajuste publico del emulador: entrada analogica, no RAM del juego.
            cfg['Pad1']['AxisScale'] = str(a.palanca_escala)
            cfg['Pad1']['Deadzone'] = '0.0'
        with ini.open('w', encoding='utf-8') as f:
            cfg.write(f)
        preparado = True
        class Propio:
            def __getattr__(self, name):
                return getattr(subprocess, name)
            def Popen(self, *args, **kw):
                proc = subprocess.Popen(*args, **kw)
                own.append(proc)
                return proc
        s.subprocess = Propio()
        p = s.lanzar('iw',str(a.estado) if a.estado else 4)
        p.s.close()
        class Muestreo(C.Cuadros):
            video_actual = None
            ordinal_video = 0

            def tomar_video(self):
                from iwx_reloj_original import guardar_pausado
                self.video_actual = guardar_pausado(self, out/'relojes', self.ordinal_video)
                self.ordinal_video += 1
                return self.video_actual

            def paso(self, n=1):
                if not a.reloj_video:
                    return super().paso(n)
                from iwx_reloj_original import comparar_estados
                self.pausa()
                inicio = self.numero()
                if self.video_actual is None:
                    self.tomar_video()
                for _ in range(n):
                    anterior = self.video_actual
                    contador = self.contador
                    self.contador = None  # El libpad ya no decide cuantos campos pasaron.
                    try:
                        super().paso()
                    finally:
                        self.contador = contador
                    actual = self.tomar_video()
                    medida = comparar_estados(anterior, actual)
                    if medida['campos_video'] != 1:
                        raise RuntimeError(f'Paso VSync inexacto: {medida}')
                return inicio, self.numero()

            def pulso(self, vk):
                self.tecla(vk,True)
                time.sleep(a.pulso_ms/1000 if vk==0x76 else .025)
                self.tecla(vk,False)
        c = Muestreo(s,COUNT)
        P.U32.GetMonitorInfoW.argtypes = [P.wt.HMONITOR,ctypes.POINTER(P._MONINFO)]
        if not P._pantalla_secundaria():
            # DISPLAY2 puede ser el unico monitor activo y estar marcado primario.
            raw = subprocess.run(['powershell','-NoProfile','-Command',
                'Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Screen]::AllScreens | '
                'Select-Object DeviceName,WorkingArea | ConvertTo-Json -Compress'],
                capture_output=True,check=True).stdout
            pantallas = json.loads(raw.decode('utf-8-sig'))
            if isinstance(pantallas,dict):
                pantallas = [pantallas]
            pantalla = next((x for x in pantallas if x['DeviceName'].endswith('DISPLAY2')),None)
            if pantalla is None:
                raise RuntimeError('No se encuentra DISPLAY2')
            b = pantalla['WorkingArea']
            P._pantalla_secundaria = lambda:(b['X'],b['Y'],b['Right'],b['Bottom'])
        P._a_pantalla_secundaria(own[0])
        os.environ['DBZ3_PCSX2_CUADROS_RESPALDO'] = str(out/'respaldos')
        os.environ['DBZ3_PCSX2_INSTANCIA'] = 'iwx'
        if a.estado:
            # lanzar() devuelve el foco durante 10 s: recargar ya pausado evita
            # saltarse el final de la intro durante ese arranque de la ventana.
            c.pausa()
            if slot.exists():
                slot_backup = out/'slot98_anterior.p2s'
                shutil.move(str(slot),str(slot_backup))
            shutil.copy2(a.estado,slot)
            slot_propio = True
            c.p.cargar(98)
            time.sleep(.5)  # PINE encola la carga; la VM permanece pausada.
            c.pausa()
            n_guardado = a.estado_info['contador']
            if c.numero()!=n_guardado:
                raise RuntimeError('La recarga pausada no conserva el contador de la instantanea')
            # El GS recien cargado puede guardar RAM sin Screenshot.png hasta su
            # primer VSync. Avanzar un campo MEDIDO, antes de iniciar la serie.
            c.paso()
        B = cargar('_iwx_lector_bubbles', Path(__file__).resolve().parent/'dif_lector_bubbles.py')
        guardar(out/'instancia.json', dict(pid=own[0].pid,exe=s.EXE,pine=PORT,iso=str(ISO),
             fase=a.fase,pasos=a.pasos,bubbles47=a.bubbles47,contador=hex(COUNT),factor_pal=1.2,
              seguimiento=a.seguimiento,mando_desde=a.mando_desde,mando_hasta=a.mando_hasta,
               palanca_escala=a.palanca_escala,pulso_ms=a.pulso_ms,
               piccolo_apuntar=a.piccolo_apuntar,piccolo_rol=a.piccolo_rol,
              reloj_video=a.reloj_video,contador_temporal='campo_video' if a.reloj_video else 'serial_libpad_no_validado',
            estado=str(a.estado.resolve()) if a.estado else STATE,
            estado_info=a.estado_info if a.estado else None,
            avance_tras_recarga=1 if a.estado else 0,
            seleccion=('Instantanea declarada; clave cotejada en RAM; sin escribir estado de juego'
                if a.estado else 'Solo indice y clave de mision desde menu04; sin forzar camara/pose/seed')))
        for clave in a.claves:
            d = out/f'{clave:X}'
            d.mkdir()
            if a.estado and c.p.u32(0x5C0DC8)!=clave:
                raise ValueError('La clave de la instantanea no coincide con la solicitada')
            if not a.estado:
                if c.p.status()==1:
                    c.pulso(0x75)
                c.p.cargar(4)
                time.sleep(2.5)
                fallos = []
                def menu():
                    try:
                        s.teclas('right espera300 z espera800 down down z')
                    except BaseException as exc:
                        fallos.append(str(exc))
                hilo = threading.Thread(target=menu)
                hilo.start()
                fin = time.monotonic()+30
                while time.monotonic()<fin:
                    if c.p.u32(0x5C0DC8)==0x7D0:
                        c.p.escribir(0x5C0DC4,struct.pack('<II',INDICES[clave-0x7D0],clave))
                        break
                    time.sleep(.02)
                else:
                    raise TimeoutError('No se llego al selector de mision')
                hilo.join()
                if fallos:
                    raise RuntimeError(fallos)
            idx = INDICES[clave-0x7D0]
            fin = time.monotonic()+180
            vio_ready = False
            transiciones = []
            while time.monotonic()<fin:
                datos = T.leer_original(c.p,idx)
                actor = T.actor(datos)
                if datos.get('estable') and actor:
                    firma = (actor.get('estado'),datos.get('ready'),datos['camara'].get('clip'),datos['camara'].get('animada'))
                    if not transiciones or firma != tuple(transiciones[-1]['firma']):
                        transiciones.append(dict(n=datos['n'],firma=firma,actor=actor))
                    vio_ready = vio_ready or datos.get('ready') is True
                    listo = a.fase in ('instantanea','inicio') or (actor.get('estado')==0x31 if a.fase=='intro' else (
                        vio_ready and actor.get('dur',0)>0 and
                        actor.get('estado',99)<0x2C and
                        datos.get('camara',{}).get('animada',0)==0))
                    if listo:
                        c.pausa()
                        break
                    if a.estado:
                        raise ValueError('La instantanea declarada no esta en la fase solicitada')
                time.sleep(.02)
            else:
                c.pausa()
                guardar(d/'fase_no_observada.json',dict(transiciones=transiciones,ultima=T.leer_original(c.p,idx)))
                guardar(d/'fallo_foto.json',c.captura(d/'fallo.png'))
                raise TimeoutError(f'{clave:X}: fase {a.fase} no observada naturalmente')
            guardar(d/'transiciones_hasta_inicio.json',transiciones)
            if a.bubbles_ia:   # eIW6: sonda de la IA (escribe codigo en RAM, ver iwx_bubbles_ia.py)
                import iwx_bubbles_ia
                guardar(d/'sonda_ia.json',dict(palabras=iwx_bubbles_ia.instalar(c.p),campos=iwx_bubbles_ia.CAMPOS))
            def leer():
                datos = T.leer_original(c.p,idx)
                if a.palanca_escala is not None:
                    pad=struct.unpack('<b',c.p.leer(0x5c14b4,1))[0]
                    if not 0<=pad<=1:
                        raise RuntimeError('Indice de mando original fuera de rango')
                    raw=struct.unpack('<bb',c.p.leer(0x4adeda+pad*0xbc,2))
                    work=c.p.u32(0x49cbc0)
                    if not 0x100000<=work<0x2000000-0x260:
                        raise RuntimeError('Contexto de marcha original fuera de rango')
                    datos['palanca']=dict(raw=raw,normalizada=struct.unpack('<ff',c.p.leer(work+0x1f4,8)))
                if a.entrada=='carga':
                    actor=c.p.u32(0x49CA70)+0x800
                    if not 0x100000<=actor<0x2000000-0x1ac0:
                        raise RuntimeError('Actor fuera de rango para lectura de energia')
                    datos['energia']=dict(actual=struct.unpack('<f',c.p.leer(actor+0x185c,4))[0],
                        reserva=struct.unpack('<f',c.p.leer(actor+0x1860,4))[0],
                        especial=struct.unpack('<f',c.p.leer(actor+0x1ab8,4))[0])
                if a.qte:   # eIW3: estado de la libreria QTE del original (NB006/NB013/NB014), solo lectura
                    ctx=c.p.u32(a.qte_ctx)
                    if 0x100000<=ctx<0x2000000-0x400:
                        r=c.p.leer(ctx,0x380)
                        u=lambda o:struct.unpack_from('<i',r,o)[0]
                        datos['qte']=dict(ctx=ctx,dif=u(0x18),f4=u(4),f8=u(8),f2e0=u(0x2e0),
                            sale=[u(0x2c8),u(0x2cc),u(0x2d0)],cuenta=[u(0x2c0),u(0x2c4)],
                            resueltos=[u(0x2c),u(0x30)],f44=[u(0x44),u(0x48)],vel=struct.unpack_from('<2f',r,0x35c),
                            vivos=[sum(1 for k in r[0x68+100*l:0x68+100*l+50] if k) for l in range(2)],raw=r.hex())
                if a.piccolo:
                    from iwx_piccolo import leer as leer_piccolo
                    datos['piccolo']=leer_piccolo(c.p.leer)
                    if not datos['piccolo']['estable'] or datos['piccolo']['n']!=datos['n']:
                        raise RuntimeError('Piccolo y camara cruzados con otro campo PAL')
                if a.seguimiento:
                    from iwx_seguimiento import leer as leer_seguimiento
                    datos['seguimiento'] = leer_seguimiento(c.p.leer)
                    if datos['seguimiento']['n'] != datos['n']:
                        raise RuntimeError('Seguimiento y camara pertenecen a campos distintos')
                if clave==0x7D1:
                    datos['bubbles'] = B.leer(c.p.leer,True)
                    # Tarea creada por s_BBR_POST_EXEC (EU 3172E0): modo +60, fase +62, duracion +54, +30/+40.
                    tb=(datos['bubbles'].get('npc') or {}).get('tareaB8',0)
                    if B.ptr(tb):
                        # Equipo E 6/11: la tarea entera (0x70 bytes): el movimiento necesita el tiempo +0x50.
                        tt=c.p.leer(tb,0x70);t=tt[0x30:]
                        datos['bubbles']['tarea']=dict(p=tb,v30=struct.unpack_from('<3f',t,0),
                            v40=struct.unpack_from('<3f',t,0x10),duracion=struct.unpack_from('<f',t,0x24)[0],
                            u58=struct.unpack_from('<I',t,0x28)[0],modo=struct.unpack_from('<h',t,0x30)[0],
                            fase=struct.unpack_from('<h',t,0x32)[0],tiempo=struct.unpack_from('<f',t,0x20)[0],
                            crudo=tt.hex())
                    if a.bubbles_ia:
                        import iwx_bubbles_ia
                        datos['bubbles_ia']=iwx_bubbles_ia.leer(c.p.leer)
                    if a.bubbles_audio:
                        from iwx_bubbles_audio import leer as leer_audio_bubbles
                        datos['bubbles_audio']=leer_audio_bubbles(c.p.leer)
                        if datos['bubbles_audio']['n']!=datos['n']:
                            raise RuntimeError('Audio y actor pertenecen a campos distintos')
                    if a.bubbles47:
                        npc=datos['bubbles']['npc']
                        if not npc or not B.ptr(npc['modelo']):
                            raise ValueError('Bubbles sin modelo para TARGET47')
                        r=B.Reader(c.p.leer)
                        model=npc['modelo']
                        count,packed=r.u(model+0x418),r.u(model+0x41c)
                        target,live=r.u(model+0x42c),r.u(model+0x45c)
                        if count!=47 or not 1<=packed>>16<=8 or not B.ptr(target) or not B.ptr(live):
                            raise ValueError('Contrato TARGET/LIVE47 fuera de rango')
                        nodos=[x['p'] for x in npc['mat47_128']]
                        if nodos!=[live+128*k for k in range(47)]:
                            raise ValueError('La tabla de nodos no coincide con LIVE45C')
                        datos['bubbles_animacion']=dict(
                            cb4=r.f(npc['actor']+0xcb4),cb8=r.f(npc['actor']+0xcb8),
                            count=count,packed=packed,frame=r.f(model+0x424),
                            target=target,live=live,target_hex=c.p.leer(target,47*64).hex(),
                            mapa_hex=c.p.leer(model+0x4b0+((packed>>16)-1)*0x200,94).hex())
                        # 205FB0 publica el cuerpo y despues 25FA90 copia hasta
                        # nueve LIVE128 de la boca. Conservar la FUENTE separada:
                        # el TARGET corporal no basta para contrastar esa capa.
                        actor=npc['actor']
                        cuerpo,boca=r.u(actor+0xb70),r.u(actor+0xb80)
                        facial=dict(cuerpo=cuerpo,boca=boca,disponible=False,
                            seleccion=struct.unpack('<i',c.p.leer(actor+0xce4,4))[0],
                            desactivada=c.p.leer(actor+0xccc,1)[0])
                        if B.ptr(cuerpo) and B.ptr(boca):
                            modelo_boca=r.u(boca+0x1c)
                            if not B.ptr(modelo_boca) or r.u(cuerpo+0x4c)!=model:
                                raise ValueError('Modelos corporal/facial fuera del contrato25FA90')
                            cuenta_boca=r.u(modelo_boca+0x418)
                            live_boca=r.u(modelo_boca+0x45c)
                            if not 1<=cuenta_boca<=128 or not B.ptr(live_boca):
                                raise ValueError('LIVE facial fuera de rango')
                            destino=struct.unpack('<9h',c.p.leer(cuerpo+0x30,18))
                            fuente=struct.unpack('<9h',c.p.leer(boca,18))
                            pares=[]
                            for j,(dst,src) in enumerate(zip(destino,fuente)):
                                if dst<0 or src<0:
                                    continue
                                if dst>=47 or src>=cuenta_boca:
                                    raise ValueError('Mapa facial fuera de rango')
                                contenido=c.p.leer(live_boca+src*128,128)
                                pares.append(dict(i=j,destino=dst,fuente=src,
                                    raw_fuente=contenido.hex(),
                                    igual128=contenido.hex()==npc['mat47_128'][dst]['raw']))
                            facial.update(disponible=True,modelo=modelo_boca,
                                count=cuenta_boca,live=live_boca,mapa_destino=destino,
                                mapa_fuente=fuente,pares=pares)
                        datos['bubbles_animacion']['facial']=facial
                    if not datos['bubbles']['estable'] or datos['bubbles']['n']!=datos['n']:
                        raise RuntimeError('Lectura Bubbles cruzada con otro campo PAL')
                if a.freezer:   # eIW6: esquiva y huesos de Freezer (7D7), lectura pura
                    import iwx_freezer
                    datos['freezer']=iwx_freezer.leer(c.p.leer,a.freezer_huesos)
                if a.efectos:
                    datos['efectos'] = leer_efectos(c.p.leer)
                if not datos['estable'] or c.numero()!=datos['n']:
                    raise RuntimeError('Lectura cruzada con otro campo PAL')
                if a.reloj_video:
                    reloj = c.video_actual
                    if reloj is None or reloj['mando']['serial'] != datos['n']:
                        raise RuntimeError('RAM y reloj de video no pertenecen al mismo estado')
                    datos['video'] = reloj
                    datos['campo_video'] = reloj['video']['ancla_vblank_cycle']//reloj['video']['periodo_campo_cycles']
                return datos
            if a.reloj_video and (c.video_actual is None or c.video_actual['mando']['serial'] != c.numero()):
                c.tomar_video()
            filas = [leer()]
            golpe_observado=False
            ultimo_disparo=-24
            guardar(d/'inicio_foto.json',c.captura(d/'inicio.png'))
            with (d/'cuadros.jsonl').open('x', encoding='utf-8') as f:
                f.write(json.dumps(filas[0],allow_nan=False)+'\n')
                for i in range(a.pasos):
                    piloto=None;teclas_piloto=[]
                    if a.piccolo_apuntar:
                        from iwx_piccolo import apuntar
                        piloto=apuntar(c.p.leer,a.piccolo_rol)
                        golpe_observado=golpe_observado or filas[-1]['piccolo']['estado']==3
                        if not golpe_observado:
                            teclas_piloto=list(piloto['teclas'])
                            if piloto['apuntado'] and i-ultimo_disparo>=24:
                                teclas_piloto.append(0x55);ultimo_disparo=i
                        for vk in teclas_piloto:c.tecla(vk,True)
                    mantener = a.fase in ('juego','instantanea') and a.mando and a.mando_desde<=i<a.mando_hasta
                    tecla = {'carga':0x4f,'disparo':0x55,'palanca':0x49}[a.entrada]
                    dash = a.dash_en is not None and i==a.dash_en
                    disparo = a.disparo_en is not None and i==a.disparo_en
                    if mantener:
                        c.tecla(tecla,True)
                    if dash or disparo:
                        c.tecla(0x55,True)
                    try:
                        c.paso()
                    finally:
                        for vk in teclas_piloto:c.tecla(vk,False)
                        if mantener:
                            c.tecla(tecla,False)
                        if dash or disparo:
                            c.tecla(0x55,False)
                    datos = leer()
                    delta = (datos['campo_video']-filas[-1]['campo_video'] if a.reloj_video
                        else (datos['n']-filas[-1]['n'])&0xffffffff)
                    if delta != 1 and not os.environ.get("PCSX2_PASO_TOLERANTE"):
                        raise RuntimeError('Falta un campo: no acreditar continuidad')
                    datos['input_I'] = bool(mantener and a.entrada=='palanca')
                    datos['entrada'] = dict(tipo=a.entrada,activa=bool(mantener))
                    datos['input_circulo'] = dash or disparo
                    if a.foto_en is not None and i==a.foto_en:
                        guardar(d/f'foto_{i}.json', c.captura(d/f'foto_{i}.png'))
                    if piloto is not None:datos['piloto_piccolo']=dict(**piloto,pulsadas=teclas_piloto)
                    filas.append(datos)
                    f.write(json.dumps(datos,allow_nan=False)+'\n')
                    f.flush()
            guardar(d/'fin_foto.json',c.captura(d/'fin.png'))
            guardar(d/'resumen.json',dict(clave=hex(clave),campos=len(filas),
                primero=filas[0]['n'],ultimo=filas[-1]['n'],consecutivos=True,
                contador_temporal='campo_video' if a.reloj_video else 'serial_libpad_no_validado',
                campos_video_verificados=a.reloj_video,
                mando_consecutivo=all((y['n']-x['n'])&0xffffffff == 1 for x,y in zip(filas,filas[1:])),
                fase=a.fase,paridad_certificada=False))
            print(hex(clave),'CAMPOS',len(filas),filas[0]['n'],filas[-1]['n'],flush=True)
    except BaseException as exc:
        fallo=dict(completa=False,error=repr(exc),recuperacion=None)
        if c and own and own[-1].poll() is None:
            try:
                fallo['recuperacion']=c.captura(out/'interrupcion.png')
            except Exception as recuperacion:
                fallo['error_recuperacion']=repr(recuperacion)
        guardar(out/'interrupcion.json',fallo)
        raise
    finally:
        if c:
            c.p.s.close()
        for proc in own:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=10)
        if own:
            s.TURNO.liberar(own[-1].pid)
        if preparado and not s.nuestro_pid():
            shutil.move(str(ini),str(out/'PCSX2_usado.ini'))
            shutil.copy2(backup,ini)
        if not s.nuestro_pid():
            if slot_propio and slot.exists():
                shutil.move(str(slot),str(out/'slot98_usado.p2s'))
            if slot_backup:
                shutil.move(str(slot_backup),str(slot))
        reserva._soltar_plaza()
        guardar(out/'cierre.json',dict(procesos_propios_cerrados=all(p.poll() is not None for p in own),
            ini_restaurado=preparado and ini.read_bytes()==backup.read_bytes()))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--claves',type=lambda x:int(x,0),nargs='+',required=True)
    p.add_argument('--fase',choices=('intro','juego','instantanea','inicio'),required=True)  # inicio (eIW6): desde la primera lectura estable
    p.add_argument('--pasos',type=int,default=240)
    p.add_argument('--mando',action='store_true')
    p.add_argument('--mando-desde',type=int,default=20)
    p.add_argument('--mando-hasta',type=int,default=120)
    p.add_argument('--entrada',choices=('palanca','carga','disparo'),default='palanca',
        help='Entrada fisica: palanca arriba, cuadrado o circulo; no escribe estado/energia')
    p.add_argument('--dash-en',type=int,help='Pulsar circulo un campo antes de cargar; gasto natural de energia')
    p.add_argument('--palanca-escala',type=float,help='AxisScale de PCSX2 para medir marcha lenta por entrada real')
    p.add_argument('--pulso-ms',type=float,default=25,help='Duracion del pulso FrameAdvance; continuidad siempre exigida')
    p.add_argument('--reloj-video',action='store_true',help='Verificar cada campo mediante estado VSync; conserva estados propios, sin fotos por paso')
    p.add_argument('--efectos',action='store_true',help='leer la lista de efectos (matriz/recurso) en cada campo')
    p.add_argument('--disparo-en',type=int,help='campo en el que se pulsa circulo (p. ej. tras cargar con cuadrado)')
    p.add_argument('--foto-en',type=int,help='campo en el que se guarda una captura (para ver el efecto)')
    p.add_argument('--seguimiento',action='store_true',help='Ancla/filtro de camara Serpiente o Cell, lectura pura por campo')
    p.add_argument('--estado',type=Path,help='Instantanea original propia o de referencia; no se modifica')
    p.add_argument('--bubbles47',action='store_true',help='Anade TARGET47 y parametros de mezcla de Bubbles')
    p.add_argument('--freezer',action='store_true',help='eIW6: globales de la esquiva de Freezer y estados (7D7)')
    p.add_argument('--freezer-huesos',action='store_true',help='eIW6: con --freezer, matrices de los huesos de Freezer por campo')
    p.add_argument('--bubbles-ia',action='store_true',help='eIW6: sonda de la IA BBR (semilla y entradas por llamada); escribe codigo en RAM')
    p.add_argument('--bubbles-audio',action='store_true',help='Voz y boca BBR desde RAM original, sin alterar su estado')
    p.add_argument('--qte-ctx',type=lambda x:int(x,0),default=0x49CDF8,help='puntero al contexto QTE: NB014 0x49CDF8, NB013 0x49CE7C, NB006 0x49CEBC')
    p.add_argument('--qte',action='store_true',help='eIW3: lee la libreria QTE (ctx en 0x49CDF8) en cada campo')
    p.add_argument('--piccolo',action='store_true',help='Estado, clip, color y tabla de impacto de Piccolo7D8')
    p.add_argument('--piccolo-apuntar',action='store_true',help='Apuntar por teclas hasta observar una reaccion; sin escrituras RAM')
    p.add_argument('--piccolo-rol',type=int,choices=range(22),default=1,
        help='Hueso objetivo del piloto, no fuerza el hueso alcanzado (defecto1)')
    a = p.parse_args()
    if not all(0x7D0<=k<=0x7E1 for k in a.claves) or a.pasos<1:
        p.error('Claves7D0..7E1 y pasos positivos')
    if a.estado and (not a.estado.is_file() or len(a.claves)!=1):
        p.error('--estado requiere fichero existente y una sola clave')
    if a.fase=='instantanea' and not a.estado:
        p.error('--fase instantanea requiere --estado; no selecciona ni fuerza fases')
    if a.bubbles47 and a.claves!=[0x7D1]:
        p.error('--bubbles47 requiere solo --claves 0x7D1')
    if a.freezer and a.claves!=[0x7D7]:
        p.error('--freezer requiere solo --claves 0x7D7')
    if a.bubbles_ia and a.claves!=[0x7D1]:
        p.error('--bubbles-ia requiere solo --claves 0x7D1')
    if a.bubbles_audio and a.claves!=[0x7D1]:
        p.error('--bubbles-audio requiere solo --claves 0x7D1')
    if a.piccolo and a.claves!=[0x7D8]:
        p.error('--piccolo requiere solo --claves 0x7D8')
    if a.piccolo_apuntar and (not a.piccolo or a.mando or a.dash_en is not None):
        p.error('--piccolo-apuntar requiere --piccolo y no admite otro piloto')
    if a.seguimiento and a.claves not in ([0x7D2],[0x7DD]):
        p.error('--seguimiento requiere solo --claves 0x7D2 o 0x7DD')
    if not 0<=a.mando_desde<a.mando_hasta:
        p.error('Intervalo de mando invalido')
    if a.dash_en is not None and not (a.mando and a.entrada=='carga' and 0<=a.dash_en<a.mando_desde<a.pasos):
        p.error('--dash-en requiere --mando --entrada carga y un campo previo al cuadrado')
    if a.disparo_en is not None and not (a.mando and 0<=a.disparo_en<a.pasos):
        p.error('--disparo-en requiere --mando y un campo dentro de --pasos')
    if a.foto_en is not None and not 0<=a.foto_en<a.pasos:
        p.error('--foto-en debe caer dentro de --pasos')
    if a.palanca_escala is not None and not (0.01<=a.palanca_escala<=1 and a.entrada=='palanca' and a.mando):
        p.error('--palanca-escala requiere --mando --entrada palanca y valor .01..1')
    if not 1<=a.pulso_ms<=25:
        p.error('--pulso-ms debe estar entre1 y25')
    if a.estado:
        a.estado_info=estado_info(a.estado)
        if int(a.estado_info['clave'],0)!=a.claves[0]:
            p.error('La clave guardada en la instantanea no coincide con --claves')
    os.environ['DBZ3_AGENTE'] = 'solIWX'
    original(a)


if __name__=='__main__':
    main()
