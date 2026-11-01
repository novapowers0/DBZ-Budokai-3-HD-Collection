"""Contraste AP contra instrucciones EE y bancos retail. Sin juego/build de juego."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from iw_ap_F_original import EE,f32,s32,MASK
from iw_ap_F_cola6 import comprobar as comprobar_cola6
from iw_ap_audio_prueba import entrada
from campo_F_investigar import Elf

CTX=0x9002d0;G=0x800000;MODEL=0x900000;ACTOR=0xa00000
OFF={0:(0x1c8,0x1ca),1:(0x1dc,0x1de),2:(0x1ec,0x1ee),3:(0x1d8,0x1da),
     4:(0x1fc,0x1fe),5:(0x20c,0x210),6:(0x22c,0x22e),7:(0x23c,0x23e),8:(0x240,0x242)}


def parser_ee(c,records):
    c.reset();c.m[CTX:CTX+0x248]=bytes(0x248);c.write(0x49ca98,CTX)
    c.write(0xa04002,len(records),2);c.write(0xa04004,0xa05000)
    for i,(frame,typ,val) in enumerate(records):
        p=0xa05000+16*i;c.write(p,int(frame),2);c.write(p+4,typ);c.write(p+8,val)
    c.r[4]=0xa04000;c.wf(12,-1);c.wf(13,100000);c.run(0x23f520)
    cols=[]
    for typ in range(10):
        if typ==9:
            values=[v for _,t,v in records if t==9]
            if values:assert c.read(CTX+0x244)==struct.unpack('<I',struct.pack('<f',f32(f32(s32(values[-1]))/10)))[0]
            cols.append(values[-1:]);continue
        cnt,arr=OFF[typ];n=c.read(CTX+cnt,4 if typ==5 else 2)
        cols.append([c.read(CTX+arr+(4 if typ==5 else 2)*i,4 if typ==5 else 2) for i in range(n)])
    return cols


def packet(data,p=0):
    u=lambda n:struct.unpack_from('<I',data,n)[0]
    n=u(p);p+=4;records=[]
    for _ in range(n):records.append(struct.unpack_from('<fII',data,p));p+=12
    cols=[]
    for t in range(10):
        n=u(p);p+=4;cols.append([u(p+4*i) for i in range(n)]);p+=n*4
    return records,cols,p


def orden_original(c,escena):
    c.reset();c.r[17]=G;c.r[18]=CTX;c.r[19]=ACTOR;c.r[20]=MODEL
    c.write(0x49ca70,G);c.write(G+0x8b8c,escena);c.write(G+0x8bd8,0,2)
    c.write(ACTOR+0x1380,0);c.write(ACTOR+0xcbc,0,2);c.write(MODEL+0x68c,0,1)
    mouth=0xb00000;bank=0xc00000;c.write(MODEL+0x540,mouth);c.write(MODEL+0x530,0xb01000)
    c.write(mouth+0x2c,bank);c.write(bank+0x14,3);c.write(bank+0x18,bank+0x20);c.write(bank+0x20+16+4,1)
    calls=[]
    def add(t,get):
        def cb(vm):calls.append((t,get(vm)));vm.r[2]=0
        return cb
    hooks={0x205660:add(0,lambda v:v.r[5]&65535),
           0x22fc50:lambda v:calls.append((1 if v.r[5]==1 else 2,v.r[6]&65535)),
           0x22fea0:add(3,lambda v:v.r[5]&65535),0x22fd20:add(7,lambda v:v.r[5]&65535),
           0x1bf610:add(4,lambda v:v.r[4]&65535),
           0x1e0f90:add(6,lambda v:v.read(v.r[5],2)),0x25fa90:add(8,lambda v:v.read(CTX+0x242,2))}
    c.run(0x207538,{0x2079b0},hooks);return calls


def instalar_amk(c,raw,base=0x1000000):
    c.m[base:base+len(raw)]=raw
    n=struct.unpack_from('<I',raw,20)[0];tab=struct.unpack_from('<I',raw,24)[0]
    c.write(base+24,base+tab)
    for i in range(n):
        p=base+tab+16*i;off=c.read(p+12)
        if off:c.write(p+12,base+off)
    return base


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF/ap'))
    ap.add_argument('--juego',type=Path,default=Path('ps2_games/Infinite World (USA)'))
    ap.add_argument('--elf-eu',type=Path,default=Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47'))
    a=ap.parse_args();assert a.tmp.is_dir();root=Path(__file__).resolve().parents[1];commands=[]
    def run(cmd,name):
        p=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,cwd=root)
        (a.tmp/(name+'.log')).write_text(p.stdout+p.stderr,encoding='utf-8');commands.append(dict(comando=list(map(str,cmd)),exit=p.returncode))
        print(name,p.returncode,flush=True)
        if p.returncode:print(p.stdout+p.stderr)
        assert p.returncode==0,name
        return p.stdout
    old=(root/'tests/iw_ama_prueba.h').read_text(encoding='utf-8')
    seg=old[old.index('  std::vector<eventos::Registro> registros='):old.index('  REXLOG_INFO("dbz3 solIWA AP:')]
    previous='bool PruebasPrevias(){int bien=0,total=0;auto comprobar=[&](bool ok,const char*){++total;if(ok)++bien;};\n'+seg+'return bien==total;}\n'
    path=a.tmp/'regresion_previa.h';path.write_text(previous,encoding='utf-8')
    cmd=['C:/Program Files/LLVM/bin/clang++.exe','-std=c++23','-O1','-Wall','-Wextra','-ffp-contract=off','-fsanitize=undefined','-fsanitize-trap=all',
         '-DIW_AP_F_PREVIAS="'+str(path.resolve()).replace('\\','/')+'"',root/'tests/iw_ap_F_prueba.cpp','-o',a.tmp/'ap_F_prueba.exe']
    run(cmd,'compilar');text=run([a.tmp/'ap_F_prueba.exe',a.juego.resolve(),a.tmp],'prueba')
    elf=a.elf_eu.read_bytes();c=EE(elf);us=Elf(a.juego/'SLUS_218.42');decomp=Path('E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c').read_text(encoding='utf-8',errors='replace')
    labels={m[2]:int(m[1],16) for m in re.finditer(r'^// ==== ([0-9a-f]+) (.+)$',decomp,re.M)}
    records,cppcols,_=packet((a.tmp/'sintetico.bin').read_bytes());native=parser_ee(c,records)
    for t in range(10):assert native[t]==[v if t in (5,9) else v&65535 for v in cppcols[t]],(t,native[t],cppcols[t])
    cpporder=[tuple(map(int,l.split()[1:])) for l in text.splitlines() if l.startswith('ORDEN ')]
    sync=[(t,v&65535) for t,v in cpporder if t not in (5,9)]
    for escena in (0,1,2,3,4,5,6,7):
        calls=orden_original(c,escena)
        assert calls==[(t,v) for t,v in sync if t!=0 or escena in (2,3,4,7)],(escena,calls,sync)
    assert [t for t,_ in cpporder][-1]==9 and sum(t==9 for t,_ in cpporder)==1
    assert [(t,v) for t,v in cpporder if t==6]==[(6,7)]
    c.reset();c.r[16]=ACTOR;c.write(ACTOR+0xb1c,len(native[5]))
    for i,v in enumerate(native[5]):c.write(ACTOR+0xb20+4*i,v)
    lectura5=[]
    for _ in range(2):
        c.r[16]=ACTOR;c.run(0x22f180,{0x22f1b8},{0x1fbe00:lambda vm:lectura5.append(vm.r[4]&0xffffffff)})
    assert lectura5==[int(l.split()[1]) for l in text.splitlines() if l.startswith('LECTOR5 ')]
    corpus=(a.tmp/'corpus.bin').read_bytes();p=0;clips=events=0
    while p<len(corpus):
        cmn,hijo,codigo=struct.unpack_from('<III',corpus,p);p+=12
        regs,expected,p=packet(corpus,p);actual=parser_ee(c,regs)
        for t in range(10):assert actual[t]==[v if t in (5,9) else v&65535 for v in expected[t]],(cmn,hijo,codigo,t)
        clips+=1;events+=len(regs)
    print('corpus original',clips,events,flush=True)
    counts=dict(ap0=0,ap4=0,ap5=0,ap9=0,amk=0,reloj8=0,lector5=len(lectura5))
    banks={}
    for line in text.splitlines():
        p=line.split()
        if p[0]=='VELOCIDAD':
            value,bits,speed=map(int,p[1:]);parser_ee(c,[(0,9,value&0xffffffff)])
            assert c.read(CTX+0x244)==bits
            c.reset();c.r[19]=0xa08000;c.r[20]=ACTOR;c.write(0x49ca70,G);c.putf(G+0x8b80,.5);c.putf(ACTOR+0xcac,1.25);c.write(ACTOR+0xb54,bits);c.wf(22,20)
            c.run(0x299f34,{0x299f8c},{0x28a9d0:lambda vm:vm.r.__setitem__(2,0)})
            assert c.f[22]==speed,(value,c.rf(22),struct.unpack('<f',struct.pack('<I',speed))[0]);counts['ap9']+=1
        elif p[0]=='AP0':
            escena,v,resultado,c29,c2b,flag,oculto,frame,speed=map(int,p[1:]);c.reset()
            c.write(MODEL+0x580,ACTOR);c.write(ACTOR+0x20ac,2,2);c.write(ACTOR+0x20ae,3,2);c.write(ACTOR+0x206e,0,1);c.write(ACTOR+0xccd,0,1)
            c.putf(ACTOR+0x18e4,7.5);c.putf(ACTOR+0xc68,0);c.putf(ACTOR+0xcb4,1)
            if escena in (2,3,4,7):
                c.r[4]=MODEL;c.r[5]=v;c.run(0x205660)
            assert (c.read(ACTOR+0x20ac,2),c.read(ACTOR+0x20ae,2),c.read(ACTOR+0x206e,1),c.read(ACTOR+0xccd,1),c.read(ACTOR+0xc68),c.read(ACTOR+0xcb4))==(c29,c2b,flag,oculto,frame,speed)
            counts['ap0']+=1
        elif p[0]=='AP4':
            id,op,ruta,a24,a48,b24,b48,paquete=map(int,p[1:]);c.reset();rival=ACTOR+0x20d0;base=0xb00000
            c.write(0x49be88,base)
            for actor,side,codigo in ((ACTOR,0,111),(rival,1,222)):
                c.write(actor+0x1380,side);c.write(actor+0x1384,rival if actor==ACTOR else ACTOR);c.write(actor+0xb60,codigo)
                c.write(base+side*0x640+0x24,-1);c.write(base+side*0x640+0x48,-1)
            c.write(0xa04000,ACTOR);c.r[4]=id&MASK;c.r[5]=0xa04000;seen=[]
            funcs=[labels['Battle_SpawnProjectilesForId'],labels['Battle_TriggerEffectsForEvent'],labels['Battle_HandleEffectCommand']]
            def hook(route):
                def cb(vm):seen.append((route,vm.r[5]&0xffffffff))
                return cb
            c.run(0x1bec60,hooks={addr:hook(i) for i,addr in enumerate(funcs)})
            assert seen==[(ruta,0xa04000 if paquete else rival if op else ACTOR)],(p,seen)
            assert [s32(c.read(base+off)) for off in (0x24,0x48,0x640+0x24,0x640+0x48)]==[a24,a48,b24,b48]
            counts['ap4']+=1
        elif p[0]=='AP5':
            value,ok,typ,own,other,invert=map(int,p[1:7]);values=list(map(float,p[7:]));c.reset();c.r[4]=value&MASK;c.r[5]=ACTOR
            c.write(0x49be88,0xb00000);c.run(0x1fbe00,{0x1fbec4})
            if ok:
                assert (s32(c.r[16]),s32(c.r[5]),s32(c.r[3]),c.r[2])==(typ,own,other,invert),p
                aa=lambda i:[float(i),float(i*2),float(i*3)]
                bb=lambda i:[float(i+20),float(i*4+10),float(i*5+30)]
                pa=aa(own);pb=bb(other) if other>=0 else bb(0)
                pos=[pa[k]+f32(pb[k]-pa[k])*.5 for k in range(3)] if other>=0 else pa
                dr=[pb[k]-pa[k] for k in range(3)] if other>=0 else [bb(0)[k]-aa(0)[k] for k in range(3)]
                if invert:dr=[-x for x in dr]
                length=math.sqrt(sum(x*x for x in dr));dire=[x/length for x in dr]
                assert all(abs(x-y)<2e-6 for x,y in zip(values,pos+dire)),(p,pos,dire)
            counts['ap5']+=1
        elif p[0]=='AMK':
            afs,bank,id=p[1],int(p[2]),int(p[3]);frame=float(p[4]);key=(afs,bank)
            if key not in banks:banks[key]=entrada(a.juego/'USR'/afs,bank)
            c.reset();ptr=instalar_amk(c,banks[key]);work=0xb00020
            c.write(work+0xc,ptr);c.write(work+8,2);c.write(work+0x58,0x10000|id);c.putf(work+0x54,-10000);c.putf(work+0x5c,frame);c.write(work+0x50,-1)
            c.r[4]=work;c.run(0x1ba850)
            vis,var=c.read(work+0x4f,1),c.read(work+0x4d,1)
            assert (vis,var,var*6+vis)==tuple(map(int,p[5:])),p
            counts['amk']+=1
        elif p[0]=='FUNC5':
            typ,ptr=map(int,p[1:]);expected=us.u32(0x4939e0+typ*4)
            assert ptr==expected and 0x100000<=ptr<0x3d0000
        elif p[0]=='RELOJ8':
            frame,step=map(float,p[1:3]);pausa,forzar,congelado,id_out,bits,force_out=map(int,p[3:])
            c.reset();amk=instalar_amk(c,banks[('ADX_USA.AFS',1471)]);ctrl=0xb10000;mouth=0xb00000
            c.write(0x49ca70,G);c.write(G+0x8b7c,pausa);c.putf(G+0x8b80,step)
            c.write(ctrl+0x540,mouth);c.write(mouth+0x2c,amk);c.write(ctrl+0x660,0x10001)
            c.putf(ctrl+0x664,frame);c.write(ctrl+0x668,forzar);c.write(ctrl+0x68c,congelado,1);c.r[4]=ctrl
            c.run(0x206e20)
            assert (c.read(ctrl+0x660),c.read(ctrl+0x664),c.read(ctrl+0x668))==(id_out,bits,force_out),p
            counts['reloj8']+=1
    # Copia REAL25FA90: registros128B, indices s16 y los dos sentidos.
    ia=[0,2,4,-1,8,10,12,14,16];ib=[17,15,13,11,9,-1,5,3,1]
    for modo in range(3):
        c.reset();ha=0xb00000;person=0xb01000;ma=0xb02000;mb=0xb03000;src=0xc00000;dst=0xc10000
        c.write(ha+0x1c,ma);c.write(person+0x4c,mb);c.write(ma+0x45c,src);c.write(mb+0x45c,dst)
        aa,bb=(list(range(9)),list(range(9,18))) if modo==2 else (ia,ib)
        for i in range(9):c.write(ha+2*i,aa[i],2);c.write(person+0x30+2*i,bb[i],2)
        for i in range(20):
            for k in range(32):c.write(src+i*128+k*4,0x12300000+i*256+k);c.write(dst+i*128+k*4,0x45600000+i*256+k)
        c.r[4]=ha;c.r[5]=person;c.r[6]=1 if modo==1 else 0;c.run(0x25fa90)
        address=src if modo==1 else dst
        assert bytes(c.m[address:address+20*128])==(a.tmp/f'matrices_{modo}.bin').read_bytes()
    # Quaternion nlerp REAL13CEA0; normalizacion independiente, luego matriz
    # REAL13C7A0. No sustituir la mezcla por interpolacion de matrices4x4.
    c.reset();x=0xc00000;y=0xc00100;z=0xc00200
    for i,v in enumerate((0,0,0,1)):c.putf(x+i*4,v)
    for i,v in enumerate((0,.6,0,.8)):c.putf(y+i*4,v)
    c.r[4]=z;c.r[5]=x;c.r[6]=y;c.wf(12,.75);c.run(0x13cea0)
    quat=[c.float(z+4*i) for i in range(4)];norm=f32(1/math.sqrt(f32(sum(v*v for v in quat))))
    quat=[f32(v*norm) for v in quat]
    for i,v in enumerate(quat):c.putf(x+4*i,v)
    for i,v in enumerate((7.5,0,0,1)):c.putf(y+4*i,v)
    c.reset();c.r[4]=z;c.r[5]=x;c.r[6]=y;c.run(0x13c7a0)
    actual=struct.unpack('<32I',(a.tmp/'mezcla_matriz.bin').read_bytes())
    for i in range(16):
        av=struct.unpack('<f',struct.pack('<I',actual[i]))[0]
        assert abs(av-c.float(z+4*i))<2e-6,(i,av,c.float(z+4*i))
    assert actual[28]==0xaabbccdd
    # Estado original completo de boca206EE0/1BA850/1BA720. Solo se sustituyen
    # el evaluador AMM y el paso de matriz ya contrastado; se conserva la
    # seleccion/cache y el factor REAL que el consumidor pide a136A60.
    c.reset();raw=banks[('ADX_USA.AFS',1471)];amk=instalar_amk(c,raw)
    mouth=0xb00000;work=mouth+0x20;ctrl=0xb10000;model=0xb20000;amm=0xb40000
    c.m[mouth:mouth+0xb0]=bytes(0xb0);c.m[ctrl:ctrl+0x700]=bytes(0x700)
    c.write(ctrl+0x540,mouth);c.write(ctrl+0x660,0x10001);c.write(work,model);c.write(work+4,0);c.write(work+8,2);c.write(work+0xc,amk)
    c.write(model+0x46c,amm);c.write(amm+0x10,12);c.write(amm+0x14,amm+0x100)
    for i in range(12):c.write(amm+0x100+16*i+8,1)
    for off in (0x6c,0x6d,0x6e,0x6f):c.write(mouth+off,255,1)
    c.write(mouth+0x70,-1);c.putf(mouth+0x74,-1);c.write(mouth+0x78,-1)
    table=struct.unpack_from('<I',raw,24)[0];record=table+16;data=struct.unpack_from('<I',raw,record+12)[0]
    second=struct.unpack_from('<h',raw,data+4)[0] if struct.unpack_from('<I',raw,record+4)[0]>1 else 0
    indices=[17,15,13,11,9,7,5,3,1];xs=list(map(float,indices));motor=[]
    for step,time in enumerate((0,0,second)):
        c.putf(ctrl+0x664,time);c.r=[0]*32;c.f=[0]*32;c.r[28]=0x4a3df0;c.r[29]=0x1ff0000;c.r[31]=0x1fff000;c.r[4]=ctrl
        factors=[]
        c.run(0x206ee0,hooks={0x136a00:lambda vm:None,0x136a60:lambda vm:factors.append(vm.rf(13))})
        assert len(factors)==1,(step,factors)
        factor=factors[0];pose=c.read(model+0x41c)&65535
        for i in range(9):xs[i]=f32(xs[i]+f32(factor*f32(float(pose*10+i)-xs[i])))
        words=list(struct.unpack('<640I',(a.tmp/f'motor_{step}.bin').read_bytes()))
        for i,index in enumerate(indices):
            off=index*32;assert words[off+20]==struct.unpack('<I',struct.pack('<f',xs[i]))[0],(step,i,factor,pose,xs[i])
            assert words[off+28]==0x12340000+index
        motor.append(dict(paso=step,cuadro=time,pose=pose,factor=factor,matrices=9))
    cola6=comprobar_cola6(elf)
    result=dict(comandos=commands,original_elf_sha256=hashlib.sha256(elf).hexdigest(),corpus=dict(clips=clips,eventos=events),casos=counts,lector6=cola6,
                orden='0,1,2,3,7,4,6,8; 5 externo; 9 ultima escritura',matrices_copiadas=23,motor_boca=motor,
                fuentes={n:hashlib.sha256((root/'src'/n).read_bytes()).hexdigest() for n in ('iw_eventos.h','iw_ap_eventos.h','iw_ap_consumidores.h','iw_ap_boca.h')},
                limites=['Sin integracion en anim_iw ni salida GPU; paleta AMM de boca y enlace de huesos los aporta el cargador',
                         'AP4 conserva ruta/estado pero sus generadores IW/capsulas no se dan por renderizados',
                         'AP5 geometria/lector reales; tabla de funciones no equivale a un renderer de sus veinte subtipos',
                         'AP0 solo estados puros enumerados; otros pedidos siguen pendientes fuera de escenas suprimidas'])
    (a.tmp/'verificado.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('PASS AP offline',counts,'corpus',clips,events,'evidencia',a.tmp/'verificado.json')


if __name__=='__main__':main()
