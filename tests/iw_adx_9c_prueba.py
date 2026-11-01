"""AP7: tablas del ELF y PCM comparado con FFmpeg instalado, no sonido real."""
import json
from pathlib import Path
import struct
import subprocess
from iw_ap_audio_prueba import U,H,entrada


def main():
    root=Path(__file__).resolve().parents[1];out=Path('D:/tmp/solIW9c');assert out.is_dir()
    game=root/'ps2_games/Infinite World (USA)'
    core=out/'iw_iop_adx_core.h'
    s=(root/'src/iw_iop.h').read_text(encoding='utf-8');core.write_text(s[s.index('namespace dbz3::iwiop {'):],encoding='utf-8')
    def run(cmd,name):
        p=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,cwd=root)
        (out/(name+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
        print(name,p.returncode,flush=True)
        if p.returncode:print(p.stdout+p.stderr)
        assert not p.returncode
        return p.stdout
    exe=out/'iw_adx_9c.exe'
    cmd=['C:/Program Files/LLVM/bin/clang++.exe','-std=c++23','-O1','-Wall','-Wextra',
         '-DDBZ3_IW_IOP_CORE="'+str(core).replace('\\','/')+'"',root/'tests/iw_adx_9c_prueba.cpp','-o',exe]
    run(cmd,'adx9c_compilar');text=run([exe,game],'adx9c_resultado')
    run(cmd[:-1]+[out/'iw_adx_9c_trap.exe','-fsanitize=undefined','-fsanitize-trap=all'],'adx9c_compilar_trap')
    assert run([out/'iw_adx_9c_trap.exe',game],'adx9c_trap')==text
    elf=(game/'SLUS_218.42').read_bytes()
    def va(addr):
        for i in range(H(elf,44)):
            p=U(elf,28)+i*H(elf,42)
            if U(elf,p)==1 and U(elf,p+8)<=addr<U(elf,p+8)+U(elf,p+16):return U(elf,p+4)+addr-U(elf,p+8)
        raise ValueError(hex(addr))
    def fnv(b):
        h=14695981039346656037
        for x in b:h=((h^x)*1099511628211)&((1<<64)-1)
        return h
    # Argumentos del controlador AP7 ejecutable: v>=2000 resta2000 y
    # llama AudioFiles;1000..1999 llama al pausador del slot (no a SESQ).
    assert U(elf,va(0x22fc78))==0x2666f830
    assert U(elf,va(0x22fc84))==0x0c000000|(0x1fd080>>2)
    assert U(elf,va(0x22fcc0))==0x0c000000|(0x1fd020>>2)
    actores={922:0,921:2,920:3,919:4,923:6,931:7,928:8,927:9,924:10,925:11,929:12,932:13,926:14,930:15,914:33}
    def perfil_ee(actor):
        r=[0]*32;r[4]=actor;pc=0x2980f0;branch=None
        for _ in range(200):
            if pc==0x2982e4:return r[2]
            w=U(elf,va(pc));op=w>>26;s=w>>21&31;t=w>>16&31;d=w>>11&31
            im=struct.unpack('<h',struct.pack('<H',w&65535))[0];old,branch=branch,None
            if op==9:r[t]=(r[s]+im)&0xffffffff
            elif op==0:
                if w&63==0:r[d]=(r[t]<<((w>>6)&31))&0xffffffff
                elif w&63 in (0x21,0x2d):r[d]=(r[s]+r[t])&0xffffffff
                else:raise AssertionError(hex(w))
            elif op in (4,0x14):
                if r[s]==r[t]:branch=pc+4+4*im
                elif op==0x14:pc+=4
            else:raise AssertionError(hex(w))
            r[0]=0;pc=old if old is not None else pc+4
        raise AssertionError('perfil EE no termino')
    cache={};casos=0;tablas=0
    perfiles={}
    for line in text.splitlines():
        p=line.split()
        if p[0]=='PERFIL':
            cmn,perfil=map(int,p[1:]);assert 0<=perfil<20
            assert perfil==perfil_ee(actores[cmn]),(cmn,perfil)
            perfiles[cmn]=perfil
            n=U(elf,va(0x47fc50+4*perfil));addr=U(elf,va(0x47fc00+4*perfil))
            assert 0<n<500 and va(addr+n*4-4)<len(elf);tablas+=1
        elif p[0]=='ADX':
            cmn,v=map(int,p[1:3]);afs=p[3];ent,hz,n,h=map(int,p[4:])
            perfil=perfiles[cmn];ptr=U(elf,va(0x47fc00+4*perfil))
            assert ent==U(elf,va(ptr+4*v)),(cmn,v,ent)
            key=(afs,ent)
            if key not in cache:
                raw=entrada(game/'USR'/afs,ent);src=out/(afs+'_'+str(ent)+'.adx')
                if not src.exists():src.write_bytes(raw)
                f=subprocess.run(['ffmpeg','-v','error','-threads','1','-filter_threads','1','-i',str(src),'-f','s16le','-acodec','pcm_s16le','-'],capture_output=True)
                assert f.returncode==0,(key,f.stderr)
                # FFmpeg entrega el bloque ADPCM final entero. El ADX declara
                # su longitud exacta en12; el IOP no reproduce ese relleno.
                esperado=struct.unpack_from('>I',raw,12)[0]*2
                pcm=f.stdout
                if raw[7]==1:pcm=b''.join(pcm[i:i+2]*2 for i in range(0,len(pcm),2))
                pcm=pcm[:esperado*2]
                cache[key]=(len(pcm)//2,fnv(pcm))
            assert cache[key]==(n,h),(p,cache[key])
            casos+=1
    corpus=next(l for l in text.splitlines() if l.startswith('CORPUS_ADX '))
    version=subprocess.run(['ffmpeg','-version'],capture_output=True,text=True).stdout.splitlines()[0]
    result=dict(corpus=corpus,pcm_ffmpeg=casos,pares_unicos=len(cache),perfiles_assembly_ee=tablas,ffmpeg=version,
                limites=['transicion de forma pendiente; controles1000/2000 contrastados contra instrucciones retail',
                         'sin salida sonora ni certificacion waveOut/pausa'])
    (out/'adx9c_verificado.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('PASS',result)


if __name__=='__main__':main()
