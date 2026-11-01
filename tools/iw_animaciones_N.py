"""Animaciones DURANTE IW: entrada por mando, RAM por contador; sin piloto.

original: instancia kit propia; port: copia fija del arnes (--copia). Conserva datos
crudos y huecos de muestreo: una transicion no observada NO cuenta como validada.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import struct
import sys
import threading
import time
import traceback

OUT = Path('D:/tmp/solIWN')
CLAVES = {i: 0x7D0 + i - (i > 4) for i in range(19) if i != 4}
# Duraciones en segundos; mismos botones PS2/XInput en ambas versiones.
PLAN = [('reposo', None, 2), ('andar', 'arriba', 2),
        ('frenar', None, 1), ('saltar', 'cruz', .15), ('caer', None, 3),
        ('correr', 'arriba', 3), ('dash', 'circulo', .15),
        ('recibir', None, 3), ('golpe', 'cuadrado', .15),
        ('disparar', 'circulo', 1), ('soltar', None, 4)]


def guardar(ruta, obj):
    if ruta.exists():
        ruta.rename(ruta.with_name(f'{ruta.stem}_anterior_{time.time_ns()}{ruta.suffix}'))
    ruta.write_text(json.dumps(obj, ensure_ascii=False, indent=1, default=str), encoding='utf-8')


def registrar(leer, poner, ruta):
    filas = []
    completa = False
    try:
        for accion, boton, segundos in PLAN:
            poner(boton, True, segundos)
            fin = time.monotonic() + segundos
            previo = None
            while time.monotonic() < fin:
                d = leer()
                if d and d['n'] != previo:
                    d['accion'] = accion
                    d['t'] = round(time.monotonic(), 5)
                    filas.append(d)
                    previo = d['n']
                time.sleep(.003)
            poner(boton, False, segundos)
        completa = True
    finally:
        guardar(ruta, dict(plan=PLAN, filas=filas, completa=completa))
    print(ruta.name, len(filas), 'muestras', flush=True)


def original(indices):
    import pcsx2_cuadros as C
    import pruebas as P
    s = C.conectar(os.environ.get('DBZ3_PCSX2_INSTANCIA', 'kit'))
    assert not s.nuestro_pid(), 'instancia ocupada'
    assert 'StandardVolume = 0' in (Path(s.EMU)/'inis/PCSX2.ini').read_text(encoding='utf-8-sig')
    reserva = P.Juego('sol', 'solIWN_original')
    while not reserva._tomar_plaza(False):
        time.sleep(5)
    propio = False
    p = None
    try:
        propio = True
        p = s.lanzar('iw', 4)
        h = s.ventana_juego(s.nuestro_pid())
        from ctypes import wintypes as wt
        s.u32dll.SetWindowPos.argtypes = [wt.HWND, wt.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wt.UINT]
        mon = P._pantalla_secundaria()
        if mon:
            s.u32dll.SetWindowPos(h, 1, mon[0]+20, mon[1]+30, 640, 510, 0x10)
        keys = {'arriba': 0x49, 'cruz': 0x5A, 'circulo': 0x58, 'cuadrado': 0x41}

        def poner(boton, abajo, segundos):
            if boton:
                s._post(h, keys[boton], abajo)

        def leer():
            if idx in (7,17):
                # El vuelo no usa actores de combate ca+800. EU340D70 y
                # EU2666C0: G+548 es CharModel, codigo G+554, reloj +270.
                g=p.u32(0x49CE3C)
                if not 0x100000<g<0x2000000:return None
                modelos=p.lote([(g+0x548,4),(g+0x7A0,4)])
                actores=[]
                for rol,m in enumerate(modelos):
                    if not 0x100000<m<0x2000000:
                        actores.append(dict(c='0x0',clip='0x0',f=0,dur=0,codigo_disponible=False))
                        continue
                    v=p.lote([(0x4A4D40,4),(g+0x554,4),(m+0x268,4),(m+0x270,4),(m+0x278,4)])
                    actores.append(dict(c=hex(v[1]) if rol==0 else '0x0',clip=hex(v[2]),
                        f=struct.unpack('<f',v[3].to_bytes(4,'little'))[0],dur=v[4],codigo_disponible=rol==0))
                return dict(n=v[0],actores=actores,adaptador='visor G+548/+7A0; rival sin codigo identificado') if actores and 'v' in locals() else None
            n, mgr = p.lote([(0x4A4D40, 4), (0x49CA70, 4)])
            if not 0x100000 < mgr < 0x2000000:
                return None
            pares = [(0x4A4D40, 4)]
            for x in (mgr+0x800, mgr+0x28D0):
                pares += [(x+0xB60, 4), (x+0xC64, 4), (x+0xC68, 4), (x+0xC6C, 4), (x+0xCBC, 2)]
            v = p.lote(pares)
            # El lote de actores ya incluye SU contador. PINE puede atender
            # cada peticion en un VSync distinto: comparar contra el lote del
            # puntero descartaba todos los cuadros del visor (0 muestras).
            n = v[0]
            actores = []
            for k in (1, 6):
                actores.append(dict(c=hex(v[k]), clip=hex(v[k+1]),
                    f=struct.unpack('<f', v[k+2].to_bytes(4,'little'))[0], dur=v[k+3], estado=v[k+4]))
            return dict(n=n, actores=actores)

        for idx in indices:
            clave = CLAVES[idx]
            print('ORIGINAL', hex(clave), flush=True)
            p.cargar(4)
            time.sleep(2.5)
            hilo = threading.Thread(target=lambda: s.teclas('right espera300 z espera800 down down z'))
            hilo.start()
            fin = time.monotonic()+30
            while time.monotonic() < fin:
                if p.u32(0x5C0DC8) == 0x7D0:
                    p.escribir(0x5C0DC4, struct.pack('<I', idx))
                    p.escribir(0x5C0DC8, struct.pack('<I', clave))
                    break
                time.sleep(.005)
            else:
                raise RuntimeError('no selecciona minijuego')
            hilo.join()
            # Esperar el comienzo real; no forzar estado/animacion del actor.
            fin = time.monotonic()+150
            ready = False
            while time.monotonic() < fin:
                ui, contexto = p.lote([(0x49CD8C,4),(0x49CBA4,4)])
                if idx in (7,17):
                    g=p.u32(0x49CE3C)
                    if 0x100000<g<0x2000000:
                        bandera,modo,bloqueado=p.lote([(g+0x724,4),(g+0x54C,4),(g+0x550,4)])
                        if bandera==0xFFFFFFFF and modo==1 and bloqueado==0:
                            ready=True
                            break
                elif idx in (3,8,9,15):
                    # GameState_SetFlag2 EU28A980: los visores no tienen el
                    # READY de campo en49CD8C. El bit2 se activa al dar mando.
                    if 0x100000<contexto<0x2000000 and p.u32(contexto+0x8C)&2:
                        ready=True
                        break
                elif 0x100000 < ui < 0x2000000 and p.u32(ui+0x150)&2:
                    ready = True
                    break
                time.sleep(.02)
            if not ready:
                guardar(OUT/f'original_{clave:X}_error.json', dict(error='READY no observado'))
                continue
            # READY dura140 cuadros60 (~2.34s). El original PAL avanza1.2.
            if idx not in (3,7,8,9,15,17):time.sleep(2.8)
            registrar(leer, poner, OUT/f'original_{clave:X}.json')
    finally:
        if p:
            p.s.close()
        if propio and s.nuestro_pid():
            s.main(['--instancia',s.INSTANCIA,'cerrar'])
        reserva._soltar_plaza()


def port(indices, copia='sol'):
    import pruebas as P
    # Guardar la causa del fallo del arnes antes de que correr la resuma.
    for nombre in ('preparar','_cache_key','respaldar'):
        fn=getattr(P,nombre)
        def con_traza(*args,_fn=fn,**kw):
            try:return _fn(*args,**kw)
            except Exception:
                traceback.print_exc()
                raise
        setattr(P,nombre,con_traza)
    for idx in indices:
        clave = CLAVES[idx]
        def env(_):
            return dict(DBZ3_TEST_IWMINI=hex(clave), DBZ3_PS2_GAMES=r'D:\DBZ3HD\ps2_games')

        def jugar(j, _):
            # Las familias publican READY con nombres distintos.
            j.log(r'(entrada acabada: mando al jugador|iwdisparo: juego .* empieza|r14: en juego|READY\? terminado; empieza el vuelo)', 150)
            P.dormir(3)
            keys = {'arriba': 'W', 'cruz': 'Semicolon', 'circulo': 'Quote', 'cuadrado': 'L'}
            def poner(boton, abajo, segundos):
                if boton and abajo:
                    # Entrada del arnes al mismo mando que el teclado normal.
                    j.tecla(f'{keys[boton]} {int(segundos*1000)}', 0)

            def leer():
                mgr, reloj = P._freecam_mem(j)
                if not mgr or reloj is None:
                    return None
                actores = []
                for x in (mgr+2096, mgr+2096+8240):
                    raw = P.leer(j.h, P.GBASE+x+0x650, 0x620)
                    if not raw: return None
                    u = lambda o: struct.unpack_from('>I', raw, o)[0]
                    actores.append(dict(c=hex(u(0x520)&0xFFF), clip=hex(u(0x604)),
                        f=struct.unpack_from('>f',raw,0x608)[0], dur=u(0x60C)))
                return dict(n=reloj, actores=actores)
            registrar(leer, poner, OUT/f'port_{clave:X}.json')
        nombre = f'solIWN_{clave:X}'
        P.ESCENARIOS[nombre] = ('animaciones durante partida', env, jugar)
        r = P.correr(copia, nombre, ['DBZ3_TEST_TURBO=1'], None, {'timeout':'500'}, True)
        guardar(OUT/f'port_{clave:X}_resultado.json',r)
        print(P.tabla([r]), flush=True)
        if not r['ok']:
            raise RuntimeError('Tanda detenida tras fallo; revisar causa antes de otra partida: '+r['msg'])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('modo', choices=['original','port','diagnostico'])
    ap.add_argument('--indices', default=','.join(map(str,CLAVES)))
    ap.add_argument('--copia', choices=['sol','opus','opus2','smoke_a','smoke_b','smoke_c'], default='sol')
    ap.add_argument('--faltantes', action='store_true')
    ap.add_argument('--esperar-pid', type=int)
    a = ap.parse_args()
    os.environ.setdefault('DBZ3_AGENTE', 'solIWN')
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')
    OUT.mkdir(exist_ok=True)
    if a.modo=='diagnostico':
        import pcsx2_cuadros as C
        s=C.conectar('kit');p=s.Pine()
        try:
            for _ in range(5):
                n,mgr,ctx=p.lote([(0x4A4D40,4),(0x49CA70,4),(0x49CBA4,4)])
                if 0x100000<mgr<0x2000000:
                    v=p.lote([(0x4A4D40,4),(mgr+0x800+0xB60,4),(mgr+0x800+0xC68,4),(mgr+0x800+0xC6C,4)])
                else:v=[]
                print(dict(n=n,mgr=hex(mgr),contexto=hex(ctx),segundo_lote=v),flush=True)
        finally:p.s.close()
        return
    if a.esperar_pid:
        k=ctypes.WinDLL('kernel32')
        k.OpenProcess.restype=ctypes.c_void_p
        k.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_ulong]
        k.CloseHandle.argtypes=[ctypes.c_void_p]
        h=k.OpenProcess(0x100000,False,a.esperar_pid)
        if h:
            print('Esperando fin de la tanda propia',a.esperar_pid,flush=True)
            k.WaitForSingleObject(h,0xFFFFFFFF)
            k.CloseHandle(h)
    indices = list(map(int,a.indices.split(',')))
    if a.faltantes:
        def falta(i):
            ruta=OUT/f'{a.modo}_{CLAVES[i]:X}.json'
            if not ruta.exists():return True
            datos=json.loads(ruta.read_text(encoding='utf-8'))
            filas=datos.get('filas',[])
            return not filas or not datos.get('completa',True) or filas[-1].get('accion')!='soltar' or filas[-1]['t']-filas[0]['t']<sum(s for _,_,s in PLAN)-.5
        indices=[i for i in indices if falta(i)]
        print('Casos sin medicion:',indices,flush=True)
    if not indices:return
    if a.modo=='original': original(indices)
    else: port(indices,a.copia)


if __name__ == '__main__':
    main()
