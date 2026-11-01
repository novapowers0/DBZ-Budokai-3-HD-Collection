"""Evidencia IW por contador; instancia zeta privada, sin cambiar sonda compartida.

original captura una presentación y READY por misión; cada imagen conserva
contador, código, cuadro de actor y cámara RAM. No usa el reloj de lanzamiento.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import threading
import time

OUT = Path('D:/tmp/solIWZ')
INDICES = [0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18]


def zeta():
    import pcsx2_cuadros as C
    sys.path.insert(0, r'E:\DBZ3HD_trabajo\R7\emu')
    import sonda as s
    from pcsx2_turnos import process_image
    def pids():
        raw = subprocess.run(['tasklist','/FI','IMAGENAME eq pcsx2.exe','/FO','CSV','/NH'],
            capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,check=True).stdout
        return [int(p[1]) for l in raw.decode('mbcs',errors='replace').splitlines()
                if len(p := l.split('\",\"')) > 1]
    s._pids = pids
    s._exe_de = process_image
    destino = OUT/'pcsx2_zeta'
    fuente = Path(s.RAIZ)/'pcsx2_kit'
    if not destino.exists():
        destino.mkdir()
        for f in fuente.iterdir():
            if f.is_file():
                shutil.copy2(f, destino/f.name)
        for nombre in ('bios', 'QtPlugins', 'resources', 'inis', 'memcards'):
            shutil.copytree(fuente/nombre, destino/nombre)
        (destino/'sstates').mkdir()
        shutil.copy2(fuente/'sstates/SLES-55347 (335A5A1F).04.p2s', destino/'sstates/SLES-55347 (335A5A1F).04.p2s')
        ini = destino/'inis/PCSX2.ini'
        texto = ini.read_text(encoding='utf-8-sig').replace('PINESlot = 28017', 'PINESlot = 28019')
        ini.write_text(texto, encoding='utf-8')
    s.INSTANCIAS['zeta'] = (28019, str(destino), str(OUT/'capturas_zeta'), 'instancia_zeta')
    s.seleccionar_instancia(['--instancia', 'zeta'])
    # La instancia solo existe en este helper; cada cierre pasa zeta explícito.
    return C.conectar('zeta')


def original(claves):
    import pcsx2_cuadros as C
    import pruebas as P
    s = zeta()
    assert not s.nuestro_pid(), 'zeta ocupada'
    os.environ['DBZ3_PCSX2_CUADROS_TMP'] = str(OUT)
    os.environ['DBZ3_PCSX2_CUADROS_RESPALDO'] = 'D:/_PARA_BORRAR_CLAUDE/solIWZ'
    reserva = P.Juego('sol', 'solIWZ_original')
    while not reserva._tomar_plaza(False):
        time.sleep(5)
    c = None
    propio = False
    preparado = False
    try:
        assert not s.nuestro_pid(), 'zeta ocupada mientras esperaba'
        pendiente = OUT/'PCSX2_zeta.ini.original'
        if pendiente.exists():
            shutil.copy2(pendiente, Path(s.EMU)/'inis/PCSX2.ini')
            archivo = Path('D:/_PARA_BORRAR_CLAUDE/solIWZ')
            archivo.mkdir(parents=True,exist_ok=True)
            shutil.move(str(pendiente), str(archivo/f'{time.time_ns()}_PCSX2_zeta_recuperada.ini'))
        C.preparar(s)
        preparado = True
        print('Lanzando instancia zeta propia', flush=True)
        p = s.lanzar('iw', 4)
        propio = True
        p.s.close()
        c = C.Cuadros(s, 0x4A4D40)
        p = c.p
        P.U32.GetMonitorInfoW.argtypes = [P.wt.HMONITOR, ctypes.POINTER(P._MONINFO)]
        P._a_pantalla_secundaria(type('Proceso', (), {'pid': s.nuestro_pid(), 'poll': lambda _: None})())
        for clave in claves:
            out = OUT/f'original_{clave:X}'
            if (out/'medida.json').exists():
                print(hex(clave), 'ya medido; omitido', flush=True)
                continue
            out.mkdir(exist_ok=True)
            if p.status() == 1:
                c.pulso(0x75)
            p.cargar(4)
            time.sleep(2.5)
            th = threading.Thread(target=lambda: s.teclas('right espera300 z espera800 down down z'))
            th.start()
            fin = time.monotonic()+30
            while time.monotonic() < fin:
                if p.u32(0x5C0DC8) == 0x7D0:
                    p.escribir(0x5C0DC4, struct.pack('<II', INDICES[clave-0x7D0], clave))
                    break
                time.sleep(.01)
            else:
                raise RuntimeError('sin selección')
            th.join()
            print(hex(clave), 'seleccionada; esperando estado/READY por PINE', flush=True)
            filas, fotos = [], []
            visto = set()
            fin = time.monotonic()+160
            n_ready = None
            anterior = None
            while time.monotonic() < fin:
                mgr, cam, ui = p.lote([(0x49CA70, 4), (0x49CBBC, 4), (0x49CD8C, 4)])
                if not (0x100000 < mgr < 0x2000000 and 0x100000 < cam < 0x2000000):
                    time.sleep(.02)
                    continue
                x = mgr+0x800
                v = p.lote([(0x4A4D40,4),(x+0xCBC,2),(x+0xB60,4),(x+0xC68,4),
                            (cam+0x114,4),(cam+0x118,4),(cam+0x12C,4)] +
                           [(cam+o,4) for o in (0x80,0x84,0x88,0x90,0x94,0x98)] +
                           [(x+o,4) for o in (0x40,0x44,0x48)])
                f = lambda u: struct.unpack('<f', u.to_bytes(4,'little'))[0]
                d = dict(n=v[0],estado=v[1],codigo=hex(v[2]),f=f(v[3]),
                         clip=hex(v[4]),cam_f=f(v[5]),cam_animada=v[6],ojo=list(map(f,v[7:10])),
                         objetivo=list(map(f,v[10:13])),pos=list(map(f,v[13:16])))
                filas.append(d)
                firma = (d['estado'], d['codigo'], d['clip'])
                if firma != anterior:
                    print(hex(clave), d, flush=True)
                    anterior = firma
                ready = 0x100000 < ui < 0x2000000 and p.u32(ui+0x150)&2
                if ready and n_ready is None:
                    n_ready = d['n']
                etiqueta = None
                if d['estado'] == 0x2C and d['f'] >= 60 and 'presentacion' not in visto:
                    etiqueta = 'presentacion'
                if n_ready is not None and d['n']-n_ready >= 50 and 'ready60' not in visto:
                    etiqueta = 'ready60'
                if etiqueta:
                    c.pausa()
                    # Guardar la RAM del cuadro detenido, no el sondeo previo.
                    datos = dict(n=c.numero(),estado=int.from_bytes(p.leer(x+0xCBC,2),'little'),
                        codigo=hex(p.u32(x+0xB60)),f=f(p.u32(x+0xC68)),
                        ojo=list(struct.unpack('<3f',p.leer(cam+0x80,12))),
                        objetivo=list(struct.unpack('<3f',p.leer(cam+0x90,12))),
                        pos=list(struct.unpack('<3f',p.leer(x+0x40,12))),
                        cam_f=f(p.u32(cam+0x118)),clip=hex(p.u32(cam+0x114)))
                    foto = c.captura(out/f'{etiqueta}_05.png')
                    fotos.append(dict(etiqueta=etiqueta,datos=datos,foto=foto))
                    visto.add(etiqueta)
                    c.pulso(0x75)
                if n_ready is not None and d['n']-n_ready >= 140:
                    break
                time.sleep(.01)
            (out/'medida.json').write_text(json.dumps(dict(clave=hex(clave),n_ready=n_ready,
                fotos=fotos,filas=filas),indent=1),encoding='utf-8')
            print(hex(clave), 'READY', n_ready, 'fotos', list(visto), flush=True)
            if n_ready is None:
                raise RuntimeError('READY no observado; diagnosticar familia antes de repetir')
    finally:
        if c:
            c.p.s.close()
        if propio:
            s.main(['--instancia','zeta','cerrar'])
        if preparado and not s.nuestro_pid():
            respaldo = OUT/'PCSX2_zeta.ini.original'
            if respaldo.exists():
                shutil.copy2(respaldo, Path(s.EMU)/'inis/PCSX2.ini')
                archivo = Path('D:/_PARA_BORRAR_CLAUDE/solIWZ')
                archivo.mkdir(parents=True,exist_ok=True)
                shutil.move(str(respaldo), str(archivo/f'{time.time_ns()}_PCSX2_zeta.ini.original'))
        reserva._soltar_plaza()


def port(claves, copia, etapa):
    import pruebas as P
    from PIL import Image
    for clave in claves:
        out = OUT/f'port_{clave:X}_{etapa}'
        out.mkdir(exist_ok=True)
        referencia = json.loads((OUT/f'original_{clave:X}/medida.json').read_text(encoding='utf-8'))
        objetivo_f = referencia['fotos'][0]['datos']['f']
        esperado = 0xA31 if clave == 0x7DD else 0xA25
        def env(_):
            return dict(DBZ3_TEST_IWMINI=hex(clave),DBZ3_PS2_GAMES='D:/DBZ3HD/ps2_games',
                DBZ3_TEST_IWMINI_CAMLOG='1',DBZ3_TEST_IWCAMTRAZA=(out/'camara.csv').as_posix())
        def jugar(j,_):
            j.log(r'iwmini: prueba: empieza',150)
            fotos, filas = [], []
            fin = time.monotonic()+120
            ready = None
            while time.monotonic()<fin:
                mgr,n = P._freecam_mem(j)
                if not mgr:
                    time.sleep(.01)
                    continue
                raw = P.leer(j.h,P.GBASE+mgr+2096+0x650,0x610)
                if not raw:continue
                codigo = struct.unpack_from('>I',raw,0x520)[0]&0xFFF
                f = struct.unpack_from('>f',raw,0x608)[0]
                d = dict(n=n,codigo=hex(codigo),f=f)
                filas.append(d)
                log = j.texto_log()
                if ready is None and 'entrada acabada: mando al jugador' in log:
                    ready = n
                etiqueta = None
                # El código real queda en el registro; la presentación se
                # identifica por el estado nombrado, no por un alias supuesto.
                if ready is None and 'estado entrada-2' in log and f>=objetivo_f and not fotos:
                    etiqueta='presentacion'
                if ready is not None and n-ready>=77 and not any(x['etiqueta']=='ready' for x in fotos):
                    etiqueta='ready'
                if etiqueta:
                    pp=j.captura(etiqueta)
                    with Image.open(pp) as im:
                        im.convert('RGB').resize((im.width//2,im.height//2)).save(out/f'{etiqueta}_05.png')
                    fotos.append(dict(etiqueta=etiqueta,antes=d,despues=P._freecam_mem(j)[1]))
                if ready is not None and n-ready>=150:break
                time.sleep(.01)
            (out/'medida.json').write_text(json.dumps(dict(fotos=fotos,filas=filas,run=str(j.run)),indent=1),encoding='utf-8')
            assert ready is not None,'sin READY'
            assert any(x['etiqueta']=='ready' for x in fotos),'sin foto READY'
        nombre=f'solIWZ_{clave:X}_{etapa}'
        P.ESCENARIOS[nombre]=('cámara por contador',env,jugar)
        resultado=P.correr(copia,nombre,[],None,{'timeout':'400'},True)
        print(P.tabla([resultado]),flush=True)
        if not resultado['ok']:raise RuntimeError(resultado['msg'])


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('modo', choices=['original','port','faltantes','curva','resumen'])
    ap.add_argument('--claves', default='0x7DD,0x7D2')
    ap.add_argument('--instancia', choices=['zeta'], default='zeta')
    ap.add_argument('--copia', default='sol')
    ap.add_argument('--etapa', default='antes')
    a = ap.parse_args()
    os.environ['DBZ3_AGENTE'] = 'solIWZ'
    OUT.mkdir(exist_ok=True)
    claves=[int(k,0) for k in a.claves.split(',')]
    if a.modo=='original':original(claves)
    elif a.modo=='port':port(claves,a.copia,a.etapa)
    elif a.modo=='faltantes':
        import iw_animaciones_N as N
        N.OUT=OUT/'animaciones'
        N.OUT.mkdir(exist_ok=True)
        for f in Path('D:/tmp/solIWN').glob('*.json'):
            if not (N.OUT/f.name).exists():shutil.copy2(f,N.OUT/f.name)
        sys.argv=[sys.argv[0],'port','--copia',a.copia,'--faltantes']
        N.main()
    elif a.modo=='curva':
        import iw_temblor as T
        from types import SimpleNamespace
        destino=OUT/'curva'
        destino.mkdir(exist_ok=True)
        T.port(SimpleNamespace(salida=destino,etapa='despues',claves=[0x7D2],
            reanudar=True,copia=a.copia,plan='WED ESE'.replace(' ','')))
    elif a.modo=='resumen':
        import iw_animaciones_N_resumen as N
        import iw_temblor_resumen as T
        N.OUT=OUT/'animaciones'
        sys.argv=[sys.argv[0],'--bancos']
        N.main()
        filas={}
        for etapa,ruta in [('original',Path('D:/tmp/solIWT/medidas/7D2_original.csv')),
            ('antes',Path('D:/tmp/solIWT/medidas/7D2_despues.csv')),
            ('solIWZ',OUT/'curva/7D2_despues.csv')]:
            n,e,c,err=T.cargar(ruta)
            filas[etapa]=T.medir(n,e,c)
        print('CURVA 7D2',json.dumps(filas),flush=True)
        (OUT/'curva_comparada.json').write_text(json.dumps(filas,indent=2),encoding='utf-8')
