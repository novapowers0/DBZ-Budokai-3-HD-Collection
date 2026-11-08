"""Sonda WinMM silenciosa: tasa real con la misma cola del mezclador IW."""
import ctypes as C
import json
from pathlib import Path
import time

class Formato(C.Structure):
    _pack_=2
    _fields_=[('tipo',C.c_uint16),('canales',C.c_uint16),('hz',C.c_uint32),('bytes_s',C.c_uint32),('bloque',C.c_uint16),('bits',C.c_uint16),('extra',C.c_uint16)]
class Cabecera(C.Structure):
    _fields_=[('datos',C.c_void_p),('longitud',C.c_uint32),('grabado',C.c_uint32),('usuario',C.c_size_t),('flags',C.c_uint32),('bucles',C.c_uint32),('siguiente',C.c_void_p),('reserva',C.c_size_t)]
class Reloj(C.Structure):
    _fields_=[('tipo',C.c_uint32),('valor',C.c_uint32*2)]

def medir(cuadros):
    w=C.WinDLL('winmm');k=C.WinDLL('kernel32');h=C.c_void_p()
    k.CreateEventW.argtypes=[C.c_void_p,C.c_bool,C.c_bool,C.c_wchar_p];k.CreateEventW.restype=C.c_void_p
    k.WaitForSingleObject.argtypes=[C.c_void_p,C.c_uint32];k.CloseHandle.argtypes=[C.c_void_p]
    ev=k.CreateEventW(None,False,False,None);assert ev
    w.waveOutOpen.argtypes=[C.POINTER(C.c_void_p),C.c_uint32,C.POINTER(Formato),C.c_size_t,C.c_size_t,C.c_uint32]
    for nombre in ('waveOutPrepareHeader','waveOutUnprepareHeader','waveOutWrite'):
        getattr(w,nombre).argtypes=[C.c_void_p,C.POINTER(Cabecera),C.c_uint32]
    w.waveOutGetPosition.argtypes=[C.c_void_p,C.POINTER(Reloj),C.c_uint32]
    w.waveOutReset.argtypes=[C.c_void_p];w.waveOutClose.argtypes=[C.c_void_p]
    f=Formato(1,2,48000,192000,4,16,0)
    assert not w.waveOutOpen(C.byref(h),0xffffffff,C.byref(f),ev,0,0x50000)
    bloques=[];preparados=[]
    try:
        for _ in range(3):
            pcm=(C.c_int16*(cuadros*2))();cab=Cabecera();cab.datos=C.cast(pcm,C.c_void_p);cab.longitud=C.sizeof(pcm)
            assert not w.waveOutPrepareHeader(h,C.byref(cab),C.sizeof(cab));preparados.append(cab);bloques.append((pcm,cab))
        enviados=set();n=0;inicio=time.perf_counter();lecturas=[]
        while time.perf_counter()-inicio<3:
            trabajo=False
            for i,(_,cab) in enumerate(bloques):
                if i in enviados and not cab.flags&1:continue
                assert not w.waveOutWrite(h,C.byref(cab),C.sizeof(cab));enviados.add(i);n+=cuadros;trabajo=True
            t=Reloj();t.tipo=2
            assert not w.waveOutGetPosition(h,C.byref(t),C.sizeof(t))
            lecturas.append([time.perf_counter()-inicio,t.tipo,t.valor[0]])
            if not trabajo:k.WaitForSingleObject(ev,20)
        dur=time.perf_counter()-inicio
        r=dict(bloque_cuadros=cuadros,cola_ms=3*cuadros/48,segundos=dur,enviados=n,
               hz_envio=(n-3*cuadros)/dur,ultima_posicion=lecturas[-1],lecturas=lecturas)
        print({x:y for x,y in r.items() if x!='lecturas'},flush=True)
        return r
    finally:
        w.waveOutReset(h)
        for cab in preparados:w.waveOutUnprepareHeader(h,C.byref(cab),C.sizeof(cab))
        w.waveOutClose(h);k.CloseHandle(ev)

if __name__=='__main__':
    resultados=[medir(n) for n in (256,512,1024)]
    Path('D:/tmp/astraIW/audio_backend.json').write_text(json.dumps(resultados,indent=2),encoding='utf-8')
