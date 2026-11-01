"""Capsulas: datos del jugador y ejecucion de MODMIDI retail, solo lectura.

RAM y puertos de salida controlados; no sustituye el parser por Python.
No abre dispositivos ni escribe assets en el proyecto.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from iw_capsulas_F_datos import ELF,CMN,entrada,u
spec=importlib.util.spec_from_file_location('F2',ROOT/'tests/iw_capsulas_F2_original.py')
F2=importlib.util.module_from_spec(spec);spec.loader.exec_module(F2)
F=F2.F


def load_irx(m,path):
    b=path.read_bytes()
    for i in range(struct.unpack_from('<H',b,48)[0]):
        h=struct.unpack_from('<10I',b,u(b,32)+i*struct.unpack_from('<H',b,46)[0])
        if (h[2]&2) and h[5]:
            m.mem[h[3]:h[3]+h[5]]=bytes(h[5]) if h[1]==8 else b[h[4]:h[4]+h[5]]
    return b


def call(m,pc,args=()):
    for i,v in enumerate(args[4:]):m.store(0x1f00000+16+4*i,4,v)
    return F.call(m,pc,args[:4])


def pulso_retail():
    m=F2.MipsF2(ELF.read_bytes());raw=load_irx(m,CMN.parent.parent/'MODULES/AMSOUND.IRX');args=[]
    def init():args.append(tuple(v&0xffffffff for v in m.r[4:6]));m.low(2,0)
    m.hooks[0x4448]=init;call(m,0x31f8)
    assert len(args)==1
    return dict(us=args[0][1],args=args,instructions=m.instructions,sha256=hashlib.sha256(raw).hexdigest())


def volumen_retail(valor):
    ee=F2.MipsF2(ELF.read_bytes());ee.store(0x49c740,4,0xA00000);request=[]
    ee.hooks[0x106070]=lambda:ee.low(2,0)
    def copy():
        dst,src,n=[v&0xffffffff for v in ee.r[4:7]];ee.mem[dst:dst+n]=ee.mem[src:src+n]
    ee.hooks[0x11dd40]=copy
    def rpc():
        request.append(dict(command=ee.r[5]&0xffffffff,mode=ee.r[6]&0xffffffff,
            value=ee.load(ee.r[7]&0xffffffff,2),size=ee.r[8]&0xffffffff));ee.low(2,0)
    ee.hooks[0x105e70]=rpc;F.call(ee,0x13fbd0,(0,),valor)
    req,=request;assert req['command']==0x1d
    m=F2.MipsF2(ELF.read_bytes());raw=load_irx(m,CMN.parent.parent/'MODULES/AMSOUND.IRX');writes=[]
    def sd():writes.append([v&0xffffffff for v in m.r[4:6]]);m.low(2,0)
    m.hooks[0x4304]=sd;m.store(0x900000,2,req['value']);call(m,0x2a7c,(0x900000,4))
    return dict(value=valor,request=req,registers=writes,ee_instructions=ee.instructions,iop_instructions=m.instructions,
        irx_sha256=hashlib.sha256(raw).hexdigest())


def hsyn_retail(id,program=0,velocity=80):
    m=F2.MipsF2(ELF.read_bytes());raw=load_irx(m,CMN.parent.parent/'MODULES/MODHSYN.IRX')
    pitch_cpu=F2.MipsF2(ELF.read_bytes());lib=load_irx(pitch_cpu,CMN.parent.parent/'MODULES/LIBSD.IRX')
    exp=next(p for p in range(0,len(lib)-80,4) if u(lib,p)==0x41c00000 and lib[p+12:p+17]==b'libsd')
    pitch_pc=u(lib,exp+20+13*4)
    root,table,entries,state,buf,data=0x900000,0x900100,0x900200,0x910000,0x920000,0xA00000
    ind=entrada(id);m.mem[data:data+len(ind)]=ind
    for a,v in ((root,2),(root+4,table),(table,2),(table+4,entries),(entries+4,buf),(entries+12,state),(buf,65536)):
        m.store(a,4,v)
    writes=[];voices=[]
    m.observe=lambda pc:voices.append(m.r[16]&0xffffffff) if pc==0x700c else None
    for pc in range(0xbd24,0xbdbc,8):
        def hook(pc=pc):writes.append(dict(pc=hex(pc),args=[v&0xffffffff for v in m.r[4:8]]));m.low(2,0)
        m.hooks[pc]=hook
    def pitch():
        args=tuple(v&0xffffffff for v in m.r[4:8]);ret=call(pitch_cpu,pitch_pc,args)
        writes.append(dict(pc='LIBSD Note2Pitch',args=args,result=ret));m.low(2,ret)
    m.hooks[0xbd64]=pitch
    m.hooks[0xbdd8]=lambda:m.store(m.r[4]&0xffffffff,4,0)
    m.hooks[0xbde0]=lambda:None
    m.hooks[0xbe28]=lambda:None
    m.hooks[0xbe30]=lambda:m.low(2,0)
    init=call(m,0x50,(root,pulso_retail()['us']));loaded=call(m,0x39c,(root,0,0x5010,data,0))
    assert init==0 and loaded==0,(init,loaded)
    writes.clear()
    events=bytes([0xc0,program,0xb0,7,127,0xb0,10,64,0xe0,0,64,0x90,60,velocity])
    m.mem[buf+8:buf+8+len(events)]=events;m.store(buf+4,4,len(events))
    call(m,0x1890,(root,))
    return dict(id=id,program=program,velocity=velocity,registers=writes,
        voices=[dict(address=hex(p),bytes=m.mem[p:p+0x130].hex()) for p in voices],
        pitch_irx_sha256=hashlib.sha256(lib).hexdigest(),pitch_code={hex(pc):hex(w) for pc,w in pitch_cpu.instructions},
        irx_sha256=hashlib.sha256(raw).hexdigest(),retail_instruction_count=len(m.instructions),
        code={hex(pc):hex(w) for pc,w in m.instructions})


def midi_retail(id,ms=5000):
    m=F2.MipsF2(ELF.read_bytes());irx=CMN.parent.parent/'MODULES/MODMIDI.IRX';raw=load_irx(m,irx)
    root,table,entries,state,port,buf,data=0x900000,0x900100,0x900200,0x910000,0x920000,0x921000,0xA00000
    seq=entrada(id);m.mem[data:data+len(seq)]=seq
    for a,v in ((root,2),(root+4,table),(table,2),(table+4,entries),(entries+4,data),(entries+12,state),
                (table+8,1),(table+12,port),(port+4,buf),(buf,65536)):
        m.store(a,4,v)
    setup=[]
    quantum=pulso_retail()
    setup.append(F.call(m,0x140,(root,quantum['us'])))
    setup.append(F.call(m,0x1d50,(root,0,0)))
    setup.append(F.call(m,0x1bb4,(root,0,1)))
    assert setup==[0,0,0],setup
    events=[];frames=[]
    for tick in range(ms):
        F.call(m,0x6b0,(root,))
        n=m.load(buf+4,4)
        if n:
            b=bytes(m.mem[buf+8:buf+8+n]);events.append(dict(pulso=tick,us=tick*quantum['us'],
                muestra=tick*quantum['us']*48000//1000000,bytes=b.hex(),midi_tick=m.load(state+8,4)))
            m.store(buf+4,4,0)
        if tick in (0,1,10,100,500,1000,2000,3000,4000):
            frames.append(dict(pulso=tick,tick=m.load(state+8,4),next=m.load(state+0x5c+0x18,4),
                period=m.load(state+0x5c+0x20,4),remainder=m.load(state+0x5c+0x1c,4),flags=m.load(state+12,4)))
        if not(m.load(state+12,4)&2):break
    instructions=m.instructions
    return dict(id=id,setup=setup,events=events,frames=frames,end_pulse=tick,quantum=quantum,flags=m.load(state+12,4),
        retail_instruction_count=len(instructions),code={hex(pc):hex(w) for pc,w in instructions},
        irx_sha256=hashlib.sha256(raw).hexdigest(),sequence_sha256=hashlib.sha256(seq).hexdigest())


def bank(id):
    b=entrada(id)
    if b'IECSbteS' not in b:
        chunks=[dict(offset=i,magic=b[i:i+8].decode('ascii'),bytes=b[i:i+48].hex()) for i in range(0,len(b)-8,4) if b[i:i+4]==b'IECS']
        records={}
        for chunk in chunks:
            q=chunk['offset']
            if chunk['magic'] in ('IECSigaV','IECSlpmS','IECStesS','IECSgorP'):
                offsets=[u(b,q+16+4*i) for i in range(u(b,q+12)+1)]
                records[chunk['magic']]=[b[q+off:q+(offsets[i+1] if i+1<len(offsets) else u(b,q+8))].hex() for i,off in enumerate(offsets)]
        body=entrada(id-1);waves=[]
        for i,rec in enumerate(records.get('IECSigaV',[])):
            r=bytes.fromhex(rec);start=u(r,0);flags=[]
            for off in range(start,len(body)-15,16):
                if body[off+1]&7:flags.append([off-start,body[off+1]])
                if body[off+1]&1:break
            waves.append(dict(id=i,rate=int.from_bytes(r[4:6],'little'),frames=(off-start+16)//16*28,flags=flags))
        return dict(id=id,hex=b.hex(),chunks=chunks,records=records,programs=[],waves=waves)
    sb=b.index(b'IECSbteS');vi=b.index(b'IECSigaV');p=sb+u(b,sb+16)
    programs=[]
    for j in range(u(b,p)+1):
        off=u(b,p+4+4*j)
        if off==0xffffffff:continue
        q=sb+off
        if q+8>len(b):break
        tones=[]
        for key in range(b[q+5],b[q+6]+1):
            t=q+u(b,q)+b[q+4]*(key-b[q+5])
            if t+50<=len(b):tones.append(dict(key=key,offset=t,bytes=b[t:t+50].hex()))
        programs.append(dict(id=j,header=b[q:q+8].hex(),tones=tones))
    waves=[]
    for j in range(u(b,vi+12)+1):
        p=vi+u(b,vi+16+4*j);waves.append(dict(id=j,offset=u(b,p),rate=u(b,p+4)&65535,record=b[p:p+16].hex()))
    return dict(id=id,programs=programs,waves=waves)


def main():
    p=argparse.ArgumentParser();p.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF'));p.add_argument('--inventario',action='store_true');p.add_argument('--sintesis',action='store_true');a=p.parse_args()
    assert a.tmp.is_dir();out=a.tmp/('midi_F3_datos_'+str(time.time_ns()));out.mkdir()
    if a.sintesis:
        result=hsyn_retail(956)
        (out/'sintesis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print('SINTESIS',out,'instrucciones',result['retail_instruction_count'])
        for r in result['registers']:print(r)
        print('VOCES',result['voices'])
        return
    result=dict(retail=[] if a.inventario else [midi_retail(id,15000) for id in (957,960)],banks=[bank(id) for id in (956,959)])
    result['exports']={}
    for name in ('MODMIDI','MODHSYN'):
        b=(CMN.parent.parent/('MODULES/'+name+'.IRX')).read_bytes();key=name.lower().encode();pos=b.find(key)
        result['exports'][name]=dict(offset=pos,words=[hex(u(b,pos+i)) for i in range(-12,136,4)])
        print('LIB',name,result['exports'][name])
    (out/'datos.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    for s in result['retail']:
        print('MIDI',s['id'],'fin_pulso',s['end_pulse'],'pulso_us',s['quantum']['us'],'flags',s['flags'],'instrucciones',s['retail_instruction_count'])
        for e in s['events']:print(e)
    for b in result['banks']:
        print('BANCO',b['id'],'waves',b['waves'])
        if 'chunks' in b:print('CHUNKS',b['chunks'])
        for chunk,rows in b.get('records',{}).items():
            for i,row in enumerate(rows):print(chunk,i,row)
        for p in b['programs']:print('PROGRAMA',p['id'],p['header'],'tonos',len(p['tones']),p['tones'][:2])
    print('DATOS',out)


if __name__=='__main__':main()
