"""Ensayo focalizado de reverb/salida IW. Sin juego ni compilacion del juego."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import wave
import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from iw_audio_F_reverb_original import Irx


def valores_mmio(result,core):
    regs={}
    for w in result['mmio']:
        addr=int(w['direccion'],16)
        if core:addr-=0x28 if 0x1f90078c<=addr<0x1f9007b0 else 0x400
        regs[addr]=w['valor']
    return regs


def referencia(entrada,reg):
    """Formula publicada, bancos MMIO del IRX; FIR por convolucion independiente."""
    def signed(x):return x if x<32768 else x-65536
    def clip(x):return max(-32768,min(32767,int(x)))
    def mul(a,b):return int(a)*int(b)>>15
    fir=np.array([-1,0,2,0,-10,0,35,0,-103,0,266,0,-616,0,1332,0,-2960,0,10246,16384,
                  10246,0,-2960,0,1332,0,-616,0,266,0,-103,0,35,0,-10,0,2,0,-1],dtype=np.int64)
    frames=len(entrada);down=np.zeros((frames,2),dtype=np.int64)
    for ch in range(2):down[1:,ch]=np.clip(np.convolve(entrada[:,ch].astype(np.int64),fir)[:frames-1]>>15,-32768,32767)
    addr=lambda r:(reg[r]<<16)|reg[r+2]
    p0=addr(0x1f9002e0);size=0x100000-p0
    ram=[0]*size;out=np.zeros((frames,2),dtype=np.int64)
    c=[signed(reg[0x1f900774+i*2]) for i in range(8)]
    offs=[addr(0x1f9002e4+i*4) for i in range(2)]+[addr(0x1f9002ec+i*4) for i in range(20)]
    inputs=[signed(reg[0x1f900784]),signed(reg[0x1f900786])]
    for frame in range(frames):
        ch=frame%2;pointer=(frame//2)%size
        at=lambda off:(pointer+off)%size
        get=lambda off:ram[at(off)]
        # offs[0..1]=APF displacement; offs[2..]=SAME,COMB1,COMB2,
        # SAMEsrc,DIFFdst,COMB3,COMB4,DIFFsrc,APF1dst,APF2dst.
        same=offs[2+ch];diff=offs[10+ch];a1=offs[18+ch];a2=offs[20+ch]
        signal=mul(down[frame,ch],inputs[ch])
        same_value=get(same-1)+mul(signal+mul(get(offs[8+ch]),c[5])-get(same-1),c[0])
        diff_value=get(diff-1)+mul(signal+mul(get(offs[16+(1-ch)]),c[5])-get(diff-1),c[0])
        comb=sum(mul(get(offs[k+ch]),c[j+1]) for j,k in enumerate((4,6,12,14)))
        saved1=get(a1-offs[0]);saved2=get(a2-offs[1])
        first=comb-mul(saved1,c[6]);middle=saved1+mul(first,c[6])
        second=middle-mul(saved2,c[7]);out[frame,ch]=clip(saved2+mul(second,c[7]))
        for address,value in ((same,same_value),(diff,diff_value),(a1,first),(a2,second)):ram[at(address)]=clip(value)
    upfir=np.clip(fir*2,-32768,32767)
    final=np.zeros((frames,2),dtype=np.int16)
    for ch in range(2):
        values=np.clip(np.convolve(out[:,ch],upfir)[:frames]>>15,-32768,32767)
        final[:,ch]=(values*signed(reg[0x1f900764+2*ch]))>>15
    return final,size


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--juego',type=Path,default=Path('ps2_games/Infinite World (USA)'))
    ap.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF/reverb'))
    ap.add_argument('--solo-dsp',action='store_true')
    ap.add_argument('--solo-salida',action='store_true',help='conserva el DSP ya verificado; prueba solo el receptor de salida')
    a=ap.parse_args();assert a.tmp.is_dir();root=Path(__file__).resolve().parents[1];comandos=[]
    def run(cmd,name):
        p=subprocess.run(list(map(str,cmd)),cwd=root,capture_output=True,text=True)
        (a.tmp/(name+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
        comandos.append(dict(comando=list(map(str,cmd)),exit=p.returncode));print(name,p.returncode,flush=True)
        if p.returncode:print(p.stdout+p.stderr)
        assert p.returncode==0,name
        return p.stdout
    clang='C:/Program Files/LLVM/bin/clang++.exe'
    cmd=[clang,'-std=c++23','-O1','-Wall','-Wextra','-fsanitize=undefined','-fsanitize-trap=all',root/'tests/iw_audio_F_reverb.cpp','-o',a.tmp/'reverb_prueba.exe']
    if a.solo_salida:
        anterior=json.loads((a.tmp/'reverb_verificado.json').read_text(encoding='utf-8'))
        assert anterior['fuentes']['iw_sonido.h']==hashlib.sha256((root/'src/iw_sonido.h').read_bytes()).hexdigest(),'DSP cambiado: ejecutar sin --solo-salida'
        text=(a.tmp/'reverb_dsp.log').read_text(encoding='utf-8')
    else:run(cmd,'reverb_compilar');text=run([a.tmp/'reverb_prueba.exe',a.tmp],'reverb_dsp')
    lib=Irx(a.juego/'MODULES/LIBSD.IRX');cores=[]
    if a.solo_salida:
        data=json.loads((a.tmp/'configuracion_irx.json').read_text(encoding='utf-8'));assert data['sha256']==lib.sha256
        cores=data['cores']
    else:
        for core in range(2):
            attr=struct.pack('<IIhhII',core,0x103,0x50a,0x50a,0,0)
            result=lib.interpretar(lib.exports['libsd'][23],[core,0x1fd000],attr)
            assert result['retorno']==0
            cores.append(result)
    (a.tmp/'configuracion_irx.json').write_text(json.dumps(dict(sha256=lib.sha256,cores=cores),indent=2),encoding='utf-8')
    regs=valores_mmio(cores[0],0);regs1=valores_mmio(cores[1],1)
    assert regs==regs1,'efecto distinto en core1'
    host={int(p[1]):int(p[2]) for l in text.splitlines() if (p:=l.split())[0]=='REG'}
    native={0:((regs[0x1f9002e4]<<16)|regs[0x1f9002e6])//4,
            1:((regs[0x1f9002e8]<<16)|regs[0x1f9002ea])//4}
    for i in range(2,10):native[i]=regs[0x1f900774+(i-2)*2]
    for i in range(10,30):
        reg=0x1f9002ec+(i-10)*4;native[i]=((regs[reg]<<16)|regs[reg+2])//4
    native[30]=regs[0x1f900784];native[31]=regs[0x1f900786]
    assert host==native,(host,native)
    # Ruta COMPACTA SESQ: tono+0D, no Smpl MIDI+29. Ejecutar el bloque
    # MODHSYN5D5C..5F2C con registros reales de mezcla a0/FFFFFFFF.
    hsyn=Irx(a.juego/'MODULES/MODHSYN.IRX');routing=[]
    direcciones=(0xc4f0,0xc4f4,0x10500,0x10504)
    for core in range(2):
        for previo in (0,0xffffffff):
            for flags in range(16):
                addrs=[p+core*8 for p in direcciones]
                extra=[(0x100040,core,1),(0x10100d,flags,1)]+[(p,previo,4) for p in addrs]
                result=hsyn.interpretar(0x5d5c,[],registros={16:0x100000,18:1<<5,19:0x101000},
                                       memoria_extra=extra,hasta=0x5f2c,leer_final=addrs)
                for bit,p in enumerate(addrs):
                    expected=(previo|(1<<5)) if flags&(1<<bit) else previo&~(1<<5)
                    assert result['memoria'][hex(p)]==expected,(core,previo,flags,bit,result)
                routing.append(dict(core=core,previo=previo,flags=flags,instrucciones=result['instrucciones']))
    cases=json.loads((a.tmp/'reverb_verificado.json').read_text(encoding='utf-8'))['casos'] if a.solo_salida else []
    for case in ([] if a.solo_salida else range(3)):
        entrada=np.fromfile(a.tmp/f'impulso_{case}_entrada.s16',dtype='<i2').reshape(-1,2)
        actual=np.fromfile(a.tmp/f'impulso_{case}_cola.s16',dtype='<i2').reshape(-1,2)
        expected,size=referencia(entrada,regs)
        if not np.array_equal(actual,expected):
            where=np.argwhere(actual!=expected);idx=tuple(where[0]);raise AssertionError((case,len(where),idx,actual[idx],expected[idx]))
        with wave.open(str(a.tmp/f'impulso_{case}_cola.wav'),'wb') as wav:
            wav.setnchannels(2);wav.setsampwidth(2);wav.setframerate(48000);wav.writeframes(actual.tobytes())
        cases.append(dict(caso=case,cuadros=len(actual),diferencias=0,area_palabras=size,sha256=hashlib.sha256(actual.tobytes()).hexdigest()))
        print('impulso/cola',case,len(actual),'cuadros identicos a formula con MMIO retail',flush=True)
    assert np.array_equal(np.fromfile(a.tmp/'mezcla_cola.s16',dtype='<i2'),np.fromfile(a.tmp/'impulso_0_cola.s16',dtype='<i2'))
    if not a.solo_dsp:
        for name in ('iw_iop','iw_adx'):
            txt=(root/'src'/f'{name}.h').read_text(encoding='utf-8')
            ns='namespace dbz3::iwiop {' if name=='iw_iop' else 'namespace dbz3::iwadx {'
            (a.tmp/(name+'_core.h')).write_text(txt[txt.index(ns):],encoding='utf-8')
        cmd=[clang,'-std=c++23','-O1','-Wall','-Wextra','-fsanitize=undefined','-fsanitize-trap=all',
             '-DDBZ3_IW_AUDIO_F_SALIDA',
             '-DDBZ3_IW_IOP_CORE="'+str((a.tmp/'iw_iop_core.h').resolve()).replace('\\','/')+'"',
             '-DDBZ3_IW_ADX_CORE="'+str((a.tmp/'iw_adx_core.h').resolve()).replace('\\','/')+'"',
             root/'tests/iw_audio_F_mezcla_salida.cpp','-o',a.tmp/'mezcla_salida.exe']
        run(cmd,'mezcla_salida_compilar');run([a.tmp/'mezcla_salida.exe',a.tmp],'mezcla_salida')
    result=dict(comandos=comandos,registros_retail=32,cores=2,libsd_sha256=lib.sha256,hsyn_sha256=hsyn.sha256,
                routing_original=routing,casos=cases,
                fuentes={n:hashlib.sha256((root/'src'/n).read_bytes()).hexdigest() for n in ('iw_iop.h','iw_adx.h','iw_sonido.h')},
                salida='no ejecutada' if a.solo_dsp else 'PASS receptor waveOut: un dispositivo, IOP+ADX, volumen/mute/pausa en vivo y cierre',
                limites=['Comparacion de salida contra formula hardware independiente con parametros ejecutados del IRX; no captura de una consola ni PCSX2',
                         'Routing compacto tono+0D contrastado; fase global y asignacion/prioridad de voces entre cores no certificadas',
                         'ADX32k se remuestrea en host; no se afirma equivalencia bit a bit con CRI/AutoDMA'])
    (a.tmp/'reverb_verificado.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('PASS reverb/salida',a.tmp/'reverb_verificado.json')


if __name__=='__main__':main()
