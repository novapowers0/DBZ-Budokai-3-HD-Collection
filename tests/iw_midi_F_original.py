"""MIDI CapsulePick: parser/HSYN/LIBSD retail + PCM/mixer real sin altavoces.

Usa la ROM documentada ya disponible, sin instalaciones ni descargas. Los
imports LIBSD de acceso hardware se observan; Note2Pitch si se ejecuta retail.
"""
import argparse
import array
import hashlib
import importlib.util
import json
from pathlib import Path
import os
import struct
import subprocess
import sys
import time
import wave

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import iw_midi_F_datos as O
from iw_audio_F_oraculo import rom_documentada


def native_parameters(bank,program,vel):
    result=O.hsyn_retail(bank,program,vel)
    params={r['args'][0]>>8:r['args'][1] for r in result['registers'] if r['pc']=='0xbd2c'}
    assert params[0]==params[1]
    route=0
    for r in result['registers']:
        if r['pc']=='0xbd3c' and r['args'][1]:
            reg=r['args'][0]&0xff00
            if reg in (0x1800,0x1900,0x1a00,0x1b00):route|={0x1800:1,0x1a00:2,0x1900:4,0x1b00:8}[reg]
    assert route==15 and params[2]>0,result['registers']
    index=O.entrada(bank);vi=index.index(b'IECSigaV');wave=vi+O.u(index,vi+16+4*program)
    addresses=[r['args'][1] for r in result['registers'] if r['pc']=='0xbd4c' and r['args'][0]&0xff00==0x2000]
    assert addresses==[0x5010+O.u(index,wave)],addresses
    result['parameters']=dict(wave=program,pitch=params[2],adsr1=params[3],adsr2=params[4],volume=params[0],routes=route,core=0)
    return result


def decode(body,index,program):
    # Tabla VAG, independiente del lector C++. Estos programas del banco
    # original seleccionan una sola onda por programa, comprobado por HSYN.
    vi=index.index(b'IECSigaV');record=vi+O.u(index,vi+16+4*program);start=O.u(index,record)
    pcm=[];h0=h1=0;coeff=((0,0),(60,0),(115,-52),(98,-55),(122,-60))
    for p in range(start,len(body)-15,16):
        shift=body[p]&15;a,b=coeff[body[p]>>4]
        for i in range(28):
            nib=(body[p+2+i//2]>>((i%2)*4))&15
            if nib>=8:nib-=16
            sample=max(-32768,min(32767,(nib*4096>>shift)+(h0*a+h1*b+32>>6)))
            pcm.append(sample);h1,h0=h0,sample
        if body[p+1]&1:return pcm
    raise ValueError('VAG sin fin original')


def env(v):
    a,b=v['adsr1'],v['adsr2'];stage=v['stage'];level=v['level']
    if stage==0:rate=(a>>8)&127;down=False;exp=bool(a&32768)
    elif stage==1:rate=((a>>4)&15)*4;down=exp=True
    elif stage==2:rate=(b>>6)&127;down=bool(b&16384);exp=bool(b&32768)
    else:rate=(b&31)*4;down=True;exp=bool(b&32)
    shift=rate//4;step=7-rate%4
    if down:step=~step
    step*=2**max(0,11-shift);increment=32768>>max(0,shift-11)
    if exp:
        if down:step=((step*level)>>15);step=((step+32768)&65535)-32768
        elif level>24576:
            if shift<10:step//=4
            elif shift==10:step//=2;increment//=2
            else:increment//=4
    frozen=(stage in (0,2) and rate==127) or (stage==3 and rate==124)
    v['clock']+=increment if frozen else max(1,increment)
    if v['clock']>=32768:v['clock']=0;level=max(0,min(32767,level+step))
    if stage==0 and level==32767:stage=1
    elif stage==1 and level<=((a&15)+1)*2048:stage=2
    v['level'],v['stage']=level,stage


def independent_pcm(case,body,index,native,rom,frames=72000,stop=48000):
    # Todo lo que dirige esta salida viene del puerto MIDImidi retail y de
    # los registros HSYN reales; nunca de los eventos/nota del header host.
    events=[]
    for e in case['events']:
        p=bytes.fromhex(e['bytes']);i=0
        while i<len(p):
            op=p[i];size=2 if (op&0xf0) in (0x80,0xc0,0xd0) else 3
            events.append((e['muestra'],p[i:i+size]));i+=size
    params={(x['program'],x['velocity']):x['parameters'] for x in native}
    waves={p:decode(body,index,p) for p,_ in params}
    programs=[0]*16;voices=[];out=array.array('h');i=0
    for f in range(frames):
        if f==stop:
            i=len(events)
            for v in voices:v['stage']=3;v['clock']=0
        while i<len(events) and events[i][0]<=f:
            data=events[i][1];i+=1;op=data[0]&0xf0;c=data[0]&15
            if op==0xc0:programs[c]=data[1]
            elif op==0x90 and data[2]:
                param=params[programs[c],data[2]]
                voices.append(dict(param,program=programs[c],channel=c,key=data[1],phase=0,stage=0,level=0,clock=0))
            elif op==0x80 or (op==0x90 and not data[2]):
                for v in voices:
                    if v['channel']==c and v['key']==data[1]:v['stage']=3;v['clock']=0
            elif op==0xb0 and data[1]==123:
                for v in voices:
                    if v['channel']==c:v['stage']=3;v['clock']=0
        total=0
        for v in voices:
            pcm=waves[v['program']];idx=v['phase']//4096
            if idx>=len(pcm):continue
            env(v);fraction=(v['phase']>>4)&255
            taps=[pcm[idx-3+k] if idx-3+k>=0 else 0 for k in range(4)]
            cof=[rom[255-fraction],rom[511-fraction],rom[256+fraction],rom[fraction]]
            sample=max(-32768,min(32767,sum((x*c)>>15 for x,c in zip(taps,cof))))
            sample=(sample*v['level'])>>15;total+=(sample*v['volume'])>>14
            v['phase']+=v['pitch']
        total=max(-32768,min(32767,total))
        if f>=stop:total=max(-32768,min(32767,total+1)) # segundo actor real del test
        out.extend((total,total))
    return out[:stop*2].tobytes(),out[stop*2:].tobytes()


def main():
    p=argparse.ArgumentParser();p.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF'));p.add_argument('--oraculo',type=Path);a=p.parse_args()
    assert a.tmp.is_dir();out=a.tmp/('midi_F3_prueba_'+str(time.time_ns()));out.mkdir()
    rom_path=a.tmp/'spu_especificacion.txt';rom=rom_documentada(rom_path.read_text(encoding='utf-8'))
    if a.oraculo:
        cases=json.loads(a.oraculo.read_text())['cases']
        assert all(c['retail'].get('quantum',{}).get('us')==0x1047 for c in cases),'el oraculo debe usar el pulso AMSOUND original4167us'
        for c in cases:
            assert c['retail']['sequence_sha256']==hashlib.sha256(O.entrada(c['seq'])).hexdigest(),'secuencia distinta del oraculo'
            assert c['retail']['irx_sha256']==hashlib.sha256((O.CMN.parent.parent/'MODULES/MODMIDI.IRX').read_bytes()).hexdigest(),'MODMIDI distinto'
            for s in c['synth']:
                assert s['irx_sha256']==hashlib.sha256((O.CMN.parent.parent/'MODULES/MODHSYN.IRX').read_bytes()).hexdigest(),'MODHSYN distinto'
    else:
        cases=[]
        for seq,bank in ((957,956),(960,959)):
            retail=O.midi_retail(seq,60000)
            assert not(retail['flags']&2),'no se alcanzo EOT en el original'
            notes=set();programs=[0]*16
            for event in retail['events']:
                b=bytes.fromhex(event['bytes']);i=0
                while i<len(b):
                    op,c=b[i]&0xf0,b[i]&15;size=2 if op in (0x80,0xc0,0xd0) else 3
                    if op==0xc0:programs[c]=b[i+1]
                    if op==0x90 and b[i+2]:notes.add((programs[c],b[i+2]))
                    i+=size
            synth=[native_parameters(bank,prog,vel) for prog,vel in sorted(notes)]
            cases.append(dict(seq=seq,bank=bank,retail=retail,synth=synth))
    volumen=[O.volumen_retail(i/8) for i in range(9)]
    oracle=dict(cases=cases,volumen=volumen,rom_sha256=hashlib.sha256(rom_path.read_bytes()).hexdigest())
    oracle_path=out/'original_midi.json';oracle_path.write_text(json.dumps(oracle,indent=2),encoding='utf-8')
    b=bytearray()
    def word(v):b.extend(struct.pack('<I',v&0xffffffff))
    def qword(v):b.extend(struct.pack('<Q',v))
    def blob(v):word(len(v));b.extend(v)
    word(0x4d494433);word(len(cases))
    for c in cases:
        body,index,seq=[O.entrada(id) for id in (c['bank']-1,c['bank'],c['seq'])]
        word(c['seq']);blob(body);blob(index);blob(seq);word(len(c['retail']['events']))
        for e in c['retail']['events']:qword(e['muestra']);blob(bytes.fromhex(e['bytes']))
        word(len(c['synth']))
        for s in c['synth']:
            word(s['program']);word(s['velocity'])
            for key in ('wave','pitch','adsr1','adsr2','volume','routes','core'):word(s['parameters'][key])
        inicio,fin=independent_pcm(c['retail'],body,index,c['synth'],rom);blob(inicio);blob(fin)
    fixture=out/'fixture_midi.bin';fixture.write_bytes(b)
    stubs,cuerpos=O.F2.motor_sources(out);imgui=ROOT/'rexglue-sdk-0.10/thirdparty/imgui'
    commands=[]
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,
            env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out)))
        (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');print(r.stdout+r.stderr)
        commands.append(dict(command=list(map(str,cmd)),returncode=r.returncode,log=name+'.log'))
        if r.returncode:raise RuntimeError(name)
    exe=out/'iw_midi_F_prueba.exe';compiler='C:/Program Files/LLVM/bin/clang++.exe'
    run([compiler,'-std=c++23','-O1','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-Wno-misleading-indentation',
        '-D_CRT_SECURE_NO_WARNINGS','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN','-fsanitize=undefined','-fsanitize-trap=all',
        '-I'+str(stubs),'-I'+str(imgui),ROOT/'tests/iw_midi_F_prueba.cpp','-lwinmm','-o',exe],'compilar_midi')
    run([exe,fixture,out],'midi')
    bancos=bytearray()
    def bw(v):bancos.extend(struct.pack('<I',v&0xffffffff))
    def bb(v):bw(len(v));bancos.extend(v)
    elf=O.ELF.read_bytes();bb(elf);ids=sorted(set(O.u(elf,0x472f40-0x100000+0x80+4*i) for i in range(45)));bw(len(ids))
    for id in ids:bw(id);bb(O.entrada(id))
    bw(len(volumen))
    for v in volumen:
        bw(O.F.FB(v['value']));bw(v['request']['value']);bw(len(v['registers']))
        for entry,value in v['registers']:bw(entry);bw(value)
    fixture_motor=out/'bancos_midi.bin';fixture_motor.write_bytes(bancos);motor=out/'iw_midi_F_motor.exe'
    run([compiler,'-std=c++23','-O1','-D_CRT_SECURE_NO_WARNINGS','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN',
         '-fsanitize=undefined','-fsanitize-trap=all','-I'+str(stubs),'-I'+str(imgui),
         '-DDBZ3_CAPSULAS_F2_CUERPOS="'+cuerpos.as_posix()+'"',ROOT/'tests/iw_midi_F_motor.cpp',
         imgui/'imgui.cpp',imgui/'imgui_draw.cpp',imgui/'imgui_tables.cpp',imgui/'imgui_widgets.cpp','-lwinmm','-o',motor],'compilar_puente')
    run([motor,fixture_motor],'puente')
    archivos=[]
    for c in cases:
        for etapa in ('inicio','corte'):
            raw=out/('midi_'+str(c['seq'])+'_'+etapa+'.s16');dest=raw.with_suffix('.wav')
            pcm=raw.read_bytes()
            with wave.open(str(dest),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000);w.writeframes(pcm)
            archivos.append(dict(path=str(dest),sha256=hashlib.sha256(dest.read_bytes()).hexdigest(),frames=len(pcm)//4))
    evidence=dict(status='PASS',commands=commands,oracle=str(oracle_path),oracle_sha256=hashlib.sha256(oracle_path.read_bytes()).hexdigest(),
        wav=archivos,
        sources={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/'src/iw_midi.h',ROOT/'src/iw_iop.h',ROOT/'src/iw_sonido.h',ROOT/'src/pasarela_iw.cpp',Path(__file__),ROOT/'tests/iw_midi_F_prueba.cpp',ROOT/'tests/iw_midi_F_motor.cpp')},
        limits=['RAM/puerto controlados en MODMIDI/MODHSYN; accesos hardware LIBSD observados; Note2Pitch ejecutado retail',
            'compara TODO el flujo MIDI hasta EOT y parametros de cada nota original; PCM1s y cola contra DSP independiente',
            'salida real via Reproductor.Procesar/Mezclador sin dispositivo; no certifica escucha/loopback del SO',
            'pulso original4167us medido en AMSOUND31F8; origen del primer callback controlado a0',
            'RPC14 usa cursor contado por el callback real del arnes; falta API cursor del mezclador para la UI',
            'host10 emite quad/MVOL/stop ADX real en contexto de un core1; faltan MVOL de ambos cores y registro ADX heredado en UI'])
    (out/'evidencia_midi.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8');print('EVIDENCIA',out)


if __name__=='__main__':main()
