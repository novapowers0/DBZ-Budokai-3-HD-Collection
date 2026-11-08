"""Medidas originales IW en PCSX2 astra2, privada; sin parches al ELF.

Se reutiliza el guardado de misión existente. Preparar una copia privada del
emulador instalado evita tocar configuración, estados o ventanas de otra sesión.
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

OUT = Path('D:/tmp/astraIW')
EMU = OUT/'pcsx2_astra2'
PORT = 28032


def conectar():
    import pcsx2_cuadros as C
    previo = os.environ.pop('DBZ3_PCSX2_INSTANCIA', None)
    import pcsx2_sonda as s
    os.environ['DBZ3_PCSX2_INSTANCIA'] = previo or 'astra2'
    from pcsx2_turnos import process_image
    fuente = Path(s.RAIZ)/'pcsx2_kit'
    if not EMU.exists():
        EMU.mkdir(parents=True)
        for f in fuente.iterdir():
            if f.is_file(): shutil.copy2(f, EMU/f.name)
        for n in ('bios','QtPlugins','resources','inis','memcards'):
            shutil.copytree(fuente/n, EMU/n)
        (EMU/'sstates').mkdir()
        shutil.copy2(fuente/'sstates/SLES-55347 (335A5A1F).04.p2s', EMU/'sstates/SLES-55347 (335A5A1F).04.p2s')
        import configparser
        ini = EMU/'inis/PCSX2.ini'
        cfg = configparser.ConfigParser(interpolation=None)
        cfg.optionxform = str
        cfg.read(ini, encoding='utf-8-sig')
        for section in cfg.sections():
            if 'PINESlot' in cfg[section]: cfg[section]['PINESlot'] = str(PORT)
            if 'StandardVolume' in cfg[section]: cfg[section]['StandardVolume'] = '0'
        cfg['Hotkeys']['TogglePause'] = 'Keyboard/F6'
        cfg['Hotkeys']['FrameAdvance'] = 'Keyboard/F7'
        cfg['Hotkeys']['Screenshot'] = 'Keyboard/F8'
        with ini.open('w', encoding='utf-8') as f: cfg.write(f)
    s.INSTANCIAS['astra2'] = (PORT,str(EMU),str(OUT/'capturas_astra2'),'instancia_astra2')
    s._exe_de = process_image
    def pids():
        raw = subprocess.run(['tasklist','/FI','IMAGENAME eq pcsx2.exe','/FO','CSV','/NH'],
                             capture_output=True,check=True).stdout.decode('mbcs',errors='replace')
        return [int(p[1]) for l in raw.splitlines() if len(p:=l.split('\",\"'))>1]
    s._pids = pids
    return C.conectar('astra2')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--clave', type=lambda x:int(x,0), default=0x7D0)
    ap.add_argument('--cuadros',type=int,default=100)
    ap.add_argument('--borde', choices=['x','z','esquina'],default='x')
    ap.add_argument('--tecla',default='J')
    ap.add_argument('--reanudar',action='store_true')
    ap.add_argument('--paso',type=float,default=0,help='desplazamiento X/Z por campo PAL;0=mando')
    a=ap.parse_args()
    os.environ['DBZ3_AGENTE']='astraIW'
    os.environ['DBZ3_PCSX2_CUADROS_RESPALDO']='D:/_PARA_BORRAR_CLAUDE/astraIW'
    import pcsx2_cuadros as C
    import pruebas as P
    s=conectar()
    assert not s.nuestro_pid(), 'astra2 ocupada'
    destino=OUT/(time.strftime('%Y%m%d-%H%M%S')+f'_original_{a.clave:X}_{a.borde}')
    destino.mkdir()
    reserva=P.Juego('sol','astraIW_original')
    c=None; propio=False
    while not reserva._tomar_plaza(False): time.sleep(5)
    try:
        p=s.lanzar('iw',8 if a.reanudar else 4); propio=True; p.s.close()
        c=C.Cuadros(s,0x4A4D40); p=c.p
        P.U32.GetMonitorInfoW.argtypes=[P.wt.HMONITOR,ctypes.POINTER(P._MONINFO)]
        P._a_pantalla_secundaria(type('Proceso',(),{'pid':s.nuestro_pid(),'poll':lambda _:None})())
        hilo=threading.Thread(target=lambda:s.teclas('right espera300 z espera800 down down z') if not a.reanudar else None)
        hilo.start()
        fin=time.monotonic()+35
        while time.monotonic()<fin:
            if a.reanudar or p.u32(0x5C0DC8)==0x7D0:
                if a.reanudar: break
                indice=a.clave-0x7D0+(a.clave>=0x7D4)
                p.escribir(0x5C0DC4,struct.pack('<II',indice,a.clave)); break
            time.sleep(.02)
        else: raise RuntimeError('selección no localizada')
        hilo.join()
        fin=time.monotonic()+180
        while time.monotonic()<fin:
            mgr,cam,ui=p.lote([(0x49CA70,4),(0x49CBBC,4),(0x49CD8C,4)])
            if all(0x100000<v<0x2000000 for v in (mgr,cam,ui)) and p.u32(ui+0x150)&2:
                break
            time.sleep(.03)
        else: raise RuntimeError('sin READY')
        # Esperar a que termine START por contador, no comparar el reloj host.
        n=p.u32(0x4A4D40)
        while p.u32(0x4A4D40)-n<180: time.sleep(.02)
        c.pausa()
        if not a.reanudar:
            guardado=EMU/'sstates/SLES-55347 (335A5A1F).08.p2s'
            if guardado.exists():
                respaldo=Path('D:/_PARA_BORRAR_CLAUDE/astraIW');respaldo.mkdir(parents=True,exist_ok=True)
                shutil.move(str(guardado),str(respaldo/(str(time.time_ns())+'_'+guardado.name)))
            p.guardar(8)
        x=mgr+0x800; rej=p.u32(0x49CDB8)
        def vec(addr): return list(struct.unpack('<3f',p.leer(addr,12)))
        ex,ez=struct.unpack('<2f',p.leer(rej+0xAD4,8))
        nx,nz=struct.unpack('<2I',p.leer(rej+0xADC,8))
        origen=vec(rej+0xAF0)
        limites=[int(origen[0]-ex*.5/nx),int(origen[2]-ez*.5/nz)]
        limites += [int(limites[0]+ex),int(limites[1]+ez)]
        anterior=vec(x+0x40)
        nuevo=[limites[2]-12 if a.borde in ('x','esquina') else 0., anterior[1],
               limites[3]-12 if a.borde in ('z','esquina') else 0.]
        delta=[nuevo[i]-anterior[i] for i in range(3)]
        # Recolocar una sola vez; el cruce lo provoca después el mando normal.
        for o in (0x30,0x40,0x50): p.escribir(x+o,struct.pack('<3f',*nuevo))
        for o in (0x80,0x90):
            v=vec(cam+o); p.escribir(cam+o,struct.pack('<3f',*(v[i]+delta[i] for i in range(3))))
        # EU299A70: el punto seguido esta en el controlador49CBC0+10;
        # mover solo la salida49CBBC no recoloca su historia.
        seguir=p.u32(0x49CBC0)
        v=vec(seguir+0x10)
        p.escribir(seguir+0x10,struct.pack('<3f',*(v[i]+delta[i] for i in range(3))))
        c.paso(40)
        def observar():
            return dict(n=c.numero(),pos=vec(x+0x40),ojo=vec(cam+0x80),obj=vec(cam+0x90),
                        estado=int.from_bytes(p.leer(x+0xCBC,2),'little'),codigo=p.u32(x+0xB60),
                        nodos=p.u32(rej+0x97B4))
        datos=dict(clave=a.clave,rejilla=dict(ptr=hex(rej),flags=p.u32(rej+0x630),
                    periodo=[ex,ez],dimensiones=[nx,nz],limites=limites),filas=[],fotos=[])
        print('Rejilla original',datos['rejilla'],flush=True)
        # Flecha derecha en el pad del kit = L; arriba = I.
        teclas=[] if a.paso else [ord(k) for k in a.tecla.upper()]
        for vk in teclas: c.tecla(vk,True)
        try:
            for i in range(a.cuadros):
                if a.paso:
                    sitio=vec(x+0x40)
                    if a.borde in ('x','esquina'):sitio[0]+=a.paso
                    if a.borde in ('z','esquina'):sitio[2]+=a.paso
                    p.escribir(x+0x40,struct.pack('<3f',*sitio))
                c.paso(); d=observar(); datos['filas'].append(d)
                salto=i and max(abs(d['pos'][j]-datos['filas'][-2]['pos'][j]) for j in (0,2))>1000
                if i in (0,8,a.cuadros-1) or salto:
                    foto=c.captura(destino/f'{i:03d}.png'); datos['fotos'].append(dict(datos=d,foto=foto))
                    print('foto',i,d,flush=True)
                (destino/'medida.json').write_text(json.dumps(datos,indent=2),encoding='utf-8')
        finally:
            for vk in teclas:c.tecla(vk,False)
        print(destino,flush=True)
    finally:
        if c:c.p.s.close()
        if propio:s.main(['--instancia','astra2','cerrar'])
        reserva._soltar_plaza()


if __name__=='__main__':main()
