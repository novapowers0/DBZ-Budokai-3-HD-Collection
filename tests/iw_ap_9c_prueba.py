"""Contraste IW9c: selector EE retail, corpus AP y no regresion del PCM anterior."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from iw_ap_audio_prueba import U, H


def selector_ee(elf,valor,japones,seed):
    def off(a):
        for i in range(H(elf,44)):
            p=U(elf,28)+i*H(elf,42)
            if U(elf,p)==1 and U(elf,p+8)<=a<U(elf,p+8)+U(elf,p+16):
                return U(elf,p+4)+a-U(elf,p+8)
        raise ValueError(hex(a))
    # El tramo de filtro de idioma se observa separado; la seleccion se ejecuta
    # desde22FDA0 hasta el primer consumidor de sonido22FEB8 (sin simularlo).
    v=valor
    if v>=2000:
        if japones:return 1,v,seed
        v-=2000
    elif v>=1000:
        if not japones:return 1,v,seed
        v-=1000
    r=[0]*32;r[16]=v;r[29]=0x10000000
    mem={};pc=0x22fda0;branch=None;call=False;hi=0
    def signed(v):return v if v<0x80000000 else v-0x100000000
    def read(a,n):
        if a>=0x10000000:return bytes(mem.get(a+i,0) for i in range(n))
        return elf[off(a):off(a)+n]
    for _ in range(400):
        if pc in (0x22feb8,0x22febc):return 3,r[16],seed
        if pc==0x22ffdc:return 1,v,seed
        w=U(elf,off(pc));op=w>>26;s=w>>21&31;t=w>>16&31;d=w>>11&31
        im=struct.unpack('<h',struct.pack('<H',w&65535))[0]
        old,oldcall=branch,call;branch=None;call=False
        if op==9:r[t]=(r[s]+im)&0xffffffff
        elif op==15:r[t]=(w&65535)<<16
        elif op in (0xa,0xb):r[t]=int((signed(r[s])<im) if op==0xa else (r[s]<(im&0xffffffff)))
        elif op in (4,5,0x14,0x15,7):
            take=signed(r[s])>0 if op==7 else (r[s]==r[t])==(op in (4,0x14))
            if take:branch=pc+4+4*im
            elif op in (0x14,0x15):pc+=4
        elif op in (0x20,0x21,0x23):
            n={0x20:1,0x21:2,0x23:4}[op]
            # lw del contexto en el delay slot anterior al punto de parada;
            # su valor ya no interviene en la seleccion.
            r[t]=0 if op==0x23 else int.from_bytes(read(r[s]+im,n),'little',signed=True)&0xffffffff
        elif op==0x29:
            for i,b in enumerate((r[t]&65535).to_bytes(2,'little')):mem[r[s]+im+i]=b
        elif op==3:
            assert (w&0x3ffffff)*4==0x127ca0
            branch=pc+8;call=True
        elif op==0:
            fn=w&63
            if fn==0:r[d]=(r[t]<<((w>>6)&31))&0xffffffff
            elif fn in (0x21,0x2d):r[d]=(r[s]+r[t])&0xffffffff
            elif fn==0x1b:hi=r[s]%r[t]
            elif fn==0x10:r[d]=hi
            elif fn==0x2b:r[d]=int(r[s]<r[t])
            else:raise AssertionError((hex(pc),hex(w)))
        else:raise AssertionError((hex(pc),hex(w)))
        r[0]=0
        if oldcall:
            assert r[4]==32
            for _ in range(r[4]):seed=((seed<<1)|(((seed>>31)^(seed>>4))&1))&0xffffffff
            r[2]=(seed-1)&0xffffffff
        pc=old if old is not None else pc+4
    raise AssertionError('no termino selector EE')


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIW9c'))
    ap.add_argument('--juego',type=Path,default=Path('ps2_games/Infinite World (USA)'))
    a=ap.parse_args();assert a.tmp.is_dir()
    root=Path(__file__).resolve().parents[1]
    def run(cmd,name):
        p=subprocess.run(list(map(str,cmd)),cwd=root,capture_output=True,text=True)
        (a.tmp/(name+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
        print(name,p.returncode,flush=True)
        if p.returncode:print(p.stdout+p.stderr)
        assert not p.returncode
        return p.stdout
    iop=(root/'src/iw_iop.h').read_text(encoding='utf-8')
    core=a.tmp/'iw_iop_9c_core.h'
    core.write_text(iop[iop.index('namespace dbz3::iwiop {'):],encoding='utf-8')
    exe=a.tmp/'iw_ap_9c.exe'
    cmd=['C:/Program Files/LLVM/bin/clang++.exe','-std=c++23','-O1','-Wall','-Wextra',
         '-DDBZ3_IW_IOP_CORE="'+str(core.resolve()).replace('\\','/')+'"',
         root/'tests/iw_ap_9c_prueba.cpp','-o',exe]
    run(cmd,'ap9c_compilar')
    text=run([exe,a.juego.resolve()],'ap9c_resultado')
    run(cmd[:-1]+[a.tmp/'iw_ap_9c_trap.exe','-fsanitize=undefined','-fsanitize-trap=all'],'ap9c_compilar_trap')
    assert run([a.tmp/'iw_ap_9c_trap.exe',a.juego.resolve()],'ap9c_trap')==text
    elf=(a.juego/'SLUS_218.42').read_bytes();checked=0;pcm=0
    for line in text.splitlines():
        p=line.split()
        if p[0]=='RNG':
            seed,jp,v,r,s,after=map(int,p[1:])
            expected=selector_ee(elf,v,bool(jp),seed)
            assert (r,s,after)==expected,(p,expected)
            checked+=1
        elif p[0]=='PCM9C':pcm+=1
    corpus=next(x for x in text.splitlines() if x.startswith('CORPUS9C '))
    result=dict(selector_ee_casos=checked,pcm_pares=pcm,corpus=corpus,
                elf_sha256=hashlib.sha256(elf).hexdigest(),
                limites=['common204/915 contrastado con actor real Goku; no todos los actores',
                         'selector EE probado con misma semilla; no RNG global compartido IW',
                         'sin certificacion waveOut/SPU2 ni efectos visuales'])
    (a.tmp/'ap9c_verificado.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('PASS',result)


if __name__=='__main__':main()
