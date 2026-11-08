"""MSN019 original: error real del mando, CC8 y cadena776/777/778."""
import ctypes
import json
import os
import struct
import threading
import time
import astra_iw_original as A
import pcsx2_cuadros as C
import pruebas as P

def main():
    os.environ['DBZ3_AGENTE']='astraIW';os.environ['DBZ3_PCSX2_CUADROS_RESPALDO']='D:/_PARA_BORRAR_CLAUDE/astraIW'
    s=A.conectar();assert not s.nuestro_pid(),'astra2 ocupada'
    out=A.OUT/(time.strftime('%Y%m%d-%H%M%S')+'_original_msn019');out.mkdir()
    reserva=P.Juego('opus2','astraIW_msn019_original');c=None;propio=False
    while not reserva._tomar_plaza(False):time.sleep(5)
    try:
        p=s.lanzar('iw',4);propio=True;p.s.close();c=C.Cuadros(s,0x4a4d40);p=c.p
        P.U32.GetMonitorInfoW.argtypes=[P.wt.HMONITOR,ctypes.POINTER(P._MONINFO)]
        P._a_pantalla_secundaria(type('Proceso',(),{'pid':s.nuestro_pid(),'poll':lambda _:None})())
        h=threading.Thread(target=lambda:s.teclas('right espera300 z espera800 down down z'));h.start()
        fin=time.monotonic()+35
        while time.monotonic()<fin:
            if p.u32(0x5c0dc8)==0x7d0:p.escribir(0x5c0dc4,struct.pack('<II',18,0x7e1));break
            time.sleep(.02)
        else:raise RuntimeError('sin seleccion')
        h.join();fin=time.monotonic()+180
        while time.monotonic()<fin:
            mgr,trabajo=p.lote([(0x49ca70,4),(0x49cd68,4)])
            if 0x100000<mgr<0x1f00000 and 0x100000<trabajo<0x1f00000:
                x=mgr+0x800
                if p.u32(x+0xb60)==0x76c:break
            time.sleep(.02)
        else:raise RuntimeError('sin estado76C')
        n=p.u32(0x4a4d40)
        while p.u32(0x4a4d40)-n<150:time.sleep(.02)
        c.pausa()
        def leer():
            f=lambda o:struct.unpack('<f',p.leer(x+o,4))[0]
            s16=lambda o:int.from_bytes(p.leer(x+o,2),'little',signed=True)
            return dict(n=c.numero(),codigo=hex(p.u32(x+0xb60)),estado=s16(0xcbc),anterior=s16(0xcbe),
                        repeticion=s16(0xcc0),destino=s16(0xcc4),cc8=f(0xcc8),cac=f(0xcac),
                        cuadro=f(0xc68),dur=f(0xc6c),fin=p.u32(x+0xc74),
                        fila=int.from_bytes(p.leer(trabajo+0x68,2),'little'),
                        intentos=int.from_bytes(p.leer(trabajo+0x6e,2),'little'))
        datos=dict(filas=[],fotos=[],intervencion='pulsaciones reales; no se escribe actor ni reloj')
        for vk in (0x58,0x41,0x53,0x5a):
            c.tecla(vk,True);c.paso();c.tecla(vk,False)
            if p.u32(x+0xb60)==0x776:break
            c.paso(4)
        else:raise RuntimeError('no se provoco error776')
        anterior=None
        for i in range(140):
            d=leer();datos['filas'].append(d)
            firma=(d['estado'],d['codigo'],d['repeticion'])
            if firma!=anterior:
                datos['fotos'].append(dict(datos=d,foto=c.captura(out/f'{i:03d}.png')))
                print(d,flush=True)
            anterior=firma
            (out/'medida.json').write_text(json.dumps(datos,indent=2),encoding='utf-8')
            c.paso()
        assert {f['codigo'] for f in datos['filas']} >= {'0x776','0x777','0x778','0x76c'}
        print(out,flush=True)
    finally:
        if c:c.p.s.close()
        if propio:s.main(['--instancia','astra2','cerrar'])
        reserva._soltar_plaza()

if __name__=='__main__':main()
