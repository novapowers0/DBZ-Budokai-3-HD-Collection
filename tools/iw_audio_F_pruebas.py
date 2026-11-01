"""Pruebas solIWF focalizadas. No compila ni arranca el juego.

Conserva el antes, contrasta salida seca con oraculo separado y escribe WAV
solo en D:/tmp/solIWF. Las llamadas waveOut se prueban con receptor sin sonido.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tests'))
from iw_ap_audio_prueba import U,H,hijo,entrada
from iw_audio_F_oraculo import Mips,code_elf,rom_documentada,pcm_original


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF'))
    ap.add_argument('--juego',type=Path,default=Path('ps2_games/Infinite World (USA)'))
    ap.add_argument('--especificacion',type=Path,help='texto de psx-spx/soundprocessingunitspu, guardado localmente')
    ap.add_argument('--elf-eu',type=Path,help='ELF EU del jugador, opcional para probar localizacion de GP/semilla')
    ap.add_argument('--solo-verificar',action='store_true',help='contrastar logs/PCM ya generados sin repetir compilaciones/corpus')
    ap.add_argument('--solo-nucleo',action='store_true',help='no repetir la salida comun/reverb ya verificada')
    a=ap.parse_args();assert a.tmp.is_dir()
    root=Path(__file__).resolve().parents[1];commands=[]
    def run(cmd,name):
        p=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,cwd=root)
        (a.tmp/(name+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
        commands.append(dict(comando=list(map(str,cmd)),exit=p.returncode))
        print(name,p.returncode,flush=True)
        if p.returncode:print(p.stdout+p.stderr)
        assert p.returncode==0,name
        return p.stdout
    compiler='C:/Program Files/LLVM/bin/clang++.exe'
    texts={}
    for stage in ('antes','despues'):
        log=a.tmp/(stage+'_resultado.log')
        if a.solo_verificar or (stage=='antes' and log.exists()):
            texts[stage]=log.read_text(encoding='utf-8');continue
        out=a.tmp/stage
        assert out.parent.is_dir();out.mkdir(exist_ok=True)
        source=(a.tmp/'iw_iop.h.antes') if stage=='antes' else root/'src/iw_iop.h'
        text=source.read_text(encoding='utf-8')
        core=out/'iop_core.h';core.write_text(text[text.index('namespace dbz3::iwiop {'):],encoding='utf-8')
        cmd=[compiler,'-std=c++23','-O1','-Wall','-Wextra',
             '-DDBZ3_IW_IOP_CORE="'+str(core.resolve()).replace('\\','/')+'"',
             root/'tests/iw_audio_F_prueba.cpp','-o',out/'prueba.exe']
        if stage=='despues':cmd.insert(1,'-DDBZ3_IW_AUDIO_F_NUEVO')
        run(cmd,stage+'_compilar')
        args=[a.juego.resolve(),out]+([a.elf_eu] if a.elf_eu else [])
        texts[stage]=run([out/'prueba.exe',*args],stage+'_resultado')
        if stage=='despues':
            run(cmd+['-fsanitize=undefined','-fsanitize-trap=all','-o',out/'trap.exe'],'sanitizador_compilar')
            assert run([out/'trap.exe',*args],'sanitizador_resultado')==texts[stage]
    spec=a.tmp/'spu_especificacion.txt'
    if a.especificacion and a.especificacion.resolve()!=spec.resolve():spec.write_bytes(a.especificacion.read_bytes())
    rom=rom_documentada(spec.read_text(encoding='utf-8'))
    elf=(a.juego/'SLUS_218.42').read_bytes();ee=code_elf(elf)
    irx=(a.juego/'MODULES/MODSESQ.IRX').read_bytes()
    # .text del IRX original (no una traduccion del algoritmo C++).
    sh=[struct.unpack_from('<10I',irx,U(irx,32)+n*H(irx,46)) for n in range(H(irx,48))]
    text_sec=next(s for s in sh if s[2]&4)
    iop=lambda pc:U(irx,text_sec[4]+pc-text_sec[3])
    rng_checks=filter_checks=pcm_checks=range_checks=0;cases=[];events=[];seeds={};rutas={}
    for line in texts['despues'].splitlines():
        p=line.split()
        if p[0] in ('SEMILLA','SEMILLA_EU'):
            raw_elf=elf if p[0]=='SEMILLA' else a.elf_eu.read_bytes()
            sections=[struct.unpack_from('<10I',raw_elf,U(raw_elf,32)+i*H(raw_elf,46)) for i in range(H(raw_elf,48))]
            reg=next(s for s in sections if s[1]==0x70000006);gp=U(raw_elf,reg[4]+20);addr=gp-0x7fe8
            data=next(s for s in sections if s[1]!=8 and s[3]<=addr<s[3]+s[5])
            actual=U(raw_elf,data[4]+addr-data[3]);assert actual==int(p[1])
            seeds[p[0]]=dict(gp=hex(gp),direccion=hex(addr),semilla=hex(actual),elf_sha256=hashlib.sha256(raw_elf).hexdigest())
        elif p[0]=='RUTA':rutas[(int(p[1]),int(p[2]))]=int(p[3])
        elif p[0]=='MEMORY':
            seed,n,value,state=map(int,p[1:]);vm=Mips(ee);vm.r[28]=0x20000000;vm.r[4]=n&0xffffffff;vm.r[31]=0xdead0000
            addr=(vm.r[28]-0x7fe8)&0xffffffff;vm.write(addr,seed)
            vm.ejecutar(0x127ca0,{0xdead0000});assert (vm.r[2],vm.read(addr))==(value,state)
            rng_checks+=1
        elif p[0]=='FILTRO':
            phase,tap,value=map(int,p[1:]);co=[rom[255-phase],rom[511-phase],rom[256+phase],rom[phase]]
            assert value==(32767*co[tap])>>15,(p,co)
            filter_checks+=1
        elif p[0]=='EVENTO':events.append(dict(zip(('cmn','codigo','tipo','valor','banco','sonido','estado'),map(int,p[1:]))))
        elif p[0]=='PCM':
            afs,bank,snd=p[1],int(p[2]),int(p[3]);amb=entrada(a.juego/'USR'/afs,bank)
            expected,detail=pcm_original(amb,snd,rom)
            actual=(a.tmp/'despues'/p[-1]).read_bytes()
            assert actual==expected,(bank,snd,detail,len(actual),len(expected),next((i for i,(x,y) in enumerate(zip(actual,expected)) if x!=y),None))
            assert bool(int(p[4]))==(detail['estado']!='pcm'),(p,detail)
            if expected:
                assert rutas[(bank,snd)]==detail['rutas']
                assert tuple(map(int,p[5:9]))==(detail['muestra'],detail['pitch'],detail['adsr1'],detail['adsr2'])
                with wave.open(str(a.tmp/('salida_seca_'+str(bank)+'_'+str(snd)+'.wav')),'wb') as w:
                    w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000);w.writeframes(actual)
            cases.append(dict(banco=bank,sonido=snd,**detail,sha256=hashlib.sha256(actual).hexdigest()))
            pcm_checks+=1
    # Ejecutar las instrucciones de guardia con TODOS los IDs de8bits de cinco
    # bancos: ni suplantar un sonido ausente ni esconder un error de lectura.
    for afs,bank in [('DATA_CMN.AFS',937),('DATA_CMN.AFS',938),('DATA_CMN.AFS',954),('ADX_USA.AFS',1472),('ADX_USA.AFS',1476)]:
        seq=hijo(entrada(a.juego/'USR'/afs,bank),2);sc=seq.index(b'IECSqseS');g=sc+U(seq,sc+32);maximum=U(seq,g)
        for snd in range(256):
            vm=Mips(iop);vm.r[7]=0x10000000;vm.r[20]=snd;vm.write(0x10000000,maximum)
            stop=vm.ejecutar(0x2308,{0x231c,0x2484})
            assert (stop==0x2484)==(snd>maximum);range_checks+=1
    # Periodo exacto del secuenciador con los seis factores1000 del banco.
    vm=Mips(iop);vm.r[4]=0x10000000;vm.r[5]=vm.r[6]=1000;vm.r[31]=0xdead0000
    for off in (0x24,0x26,0x28,0x2a):vm.write(0x10000000+off,1000,2)
    vm.ejecutar(0x7f4,{0xdead0000});assert vm.read(0x10000034)==128000
    result=dict(comandos=commands,antes=next(s for s in texts['antes'].splitlines() if s.startswith('CORPUS')),
                despues=next(s for s in texts['despues'].splitlines() if s.startswith('CORPUS')),
                rng_instrucciones=rng_checks,semillas_elf=seeds,filtro_impulsos=filter_checks,pcm_pares=pcm_checks,
                guardia_ids_retail=range_checks,periodo_sesq=128000,eventos=events,pares=cases,
                fuentes=dict(elf_sha256=hashlib.sha256(elf).hexdigest(),modsesq_sha256=hashlib.sha256(irx).hexdigest(),
                             rom_documento_sha256=hashlib.sha256(spec.read_bytes()).hexdigest(),
                             iop_sha256=hashlib.sha256((root/'src/iw_iop.h').read_bytes()).hexdigest(),
                             adx_sha256=hashlib.sha256((root/'src/iw_adx.h').read_bytes()).hexdigest()),
                limites=['RNG compartido requiere integracion; semilla/consumos globales no certificados',
                          'Este corpus contrasta PCM seco; reverb/salida comun se verifican en reverb/reverb_verificado.json',
                         'note-off en tiempo logico; fase de callback IOP4167us no sincronizada con host',
                         'filtro matematico contrastado, no captura de hardware ni altavoz'])
    # ADX de la tabla real perfil0/indice0 -> FFmpeg independiente.
    def off(addr):
        for n in range(H(elf,44)):
            p=U(elf,28)+n*H(elf,42)
            if U(elf,p)==1 and U(elf,p+8)<=addr<U(elf,p+8)+U(elf,p+16):return U(elf,p+4)+addr-U(elf,p+8)
        raise ValueError(hex(addr))
    ptr=U(elf,off(0x47fc00));entry=U(elf,off(ptr));raw=entrada(a.juego/'USR/ADX_USA.AFS',entry)
    (a.tmp/'original.adx').write_bytes(raw)
    proc=subprocess.run(['ffmpeg','-v','error','-threads','1','-i',str(a.tmp/'original.adx'),'-f','s16le','-'],capture_output=True)
    assert proc.returncode==0,proc.stderr
    ref=proc.stdout
    if raw[7]==1:ref=b''.join(ref[i:i+2]*2 for i in range(0,len(ref),2))
    ref=ref[:struct.unpack_from('>I',raw,12)[0]*4]
    (a.tmp/'adx_original.s16').write_bytes(ref)
    if not a.solo_nucleo:
        run([sys.executable,'-B',root/'tools/iw_audio_F_reverb_pruebas.py','--juego',a.juego,
             '--tmp',a.tmp/'reverb'],'salida_comun_reverb')
    result['salida']=dict(adx_entrada=entry,evidencia=str(a.tmp/'reverb/reverb_verificado.json'),
                         ejecutada_en_esta_tanda=not a.solo_nucleo)
    (a.tmp/'resultado_F.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('PASS',result['antes'],'->',result['despues'],
          'RNG',rng_checks,'filtro',filter_checks,'pares',pcm_checks,'guardias',range_checks,
          'evidencia',a.tmp/'resultado_F.json')


if __name__=='__main__':main()
