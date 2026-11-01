"""Ejecuta spawn/update AME retail en RAM controlada; no arranca ningun juego."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from iw_capsulas_F_datos import ELF,entrada,u
spec=importlib.util.spec_from_file_location('F2',ROOT/'tests/iw_capsulas_F2_original.py')
F2=importlib.util.module_from_spec(spec);spec.loader.exec_module(F2)
F=F2.F;BF,FB=F.BF,F.FB


def f32_chop(value):
    """Perfil finito COP2/MUL COP1 contrastado con RAM PCSX2 F5."""
    r=F.retail.F32(value)
    if math.isfinite(r) and ((value>0 and r>value) or (value<0 and r<value)):
        return BF(FB(r)-1)
    return r


class EE(F2.MipsF2):
    def __init__(self,b):
        super().__init__(b);self.vf=[[0]*4 for _ in range(32)];self.vf[0][3]=FB(1)
        self.acc=[0.]*4;self.q=1.;self.random=[];self.fpacc=0.
    def step(self):
        pc=self.pc;w=self.load(pc,4);op=w>>26;s,t,d,fd,fn=(w>>21)&31,(w>>16)&31,(w>>11)&31,(w>>6)&31,w&63
        if pc in self.hooks:return super().step()
        handled=True
        if op==0x11 and s==16 and fn==2:
            x,y=BF(self.fp[d]),BF(self.fp[t]);self.fp[fd]=FB(f32_chop(x*y))
        elif op==0x11 and s==16 and fn in (0x18,0x19,0x1a,0x1c,0x1d,0x1e,0x1f):
            # MADD.S2747FC tambien trunca su producto, no solo MUL.S.
            # El acumulador/suma mantienen su paso F32 separado.
            x,y=BF(self.fp[d]),BF(self.fp[t]);product=f32_chop(x*y)
            if fn==0x18:self.fpacc=F.retail.F32(x+y)
            elif fn==0x19:self.fpacc=F.retail.F32(x-y)
            elif fn==0x1a:self.fpacc=product
            elif fn in (0x1e,0x1f):self.fpacc=F.retail.F32(self.fpacc+(product if fn==0x1e else -product))
            else:self.fp[fd]=FB(F.retail.F32(self.fpacc+(product if fn==0x1c else -product)))
        elif op in (0x36,0x3e):
            addr=(self.r[s]+F.retail.SX(w&65535,16))&0xffffffff
            if op==0x36:self.vf[t]=[self.load(addr+j*4,4) for j in range(4)]
            else:
                for j,v in enumerate(self.vf[t]):self.store(addr+4*j,4,v)
        elif op==0x12:
            if s==1:self.r[t]=sum(v<<(j*32) for j,v in enumerate(self.vf[d]))
            elif s==5:self.vf[d]=[(self.r[t]>>(j*32))&0xffffffff for j in range(4)]
            elif s>=16:
                mask=s&15;fs,ft=d,t
                def vec(kind,bc=None,acc=False,scalar=None):
                    for j in range(4):
                        if not(mask&(8>>j)):continue
                        a=BF(self.vf[fs][j]);b=scalar if scalar is not None else BF(self.vf[ft][bc if bc is not None else j])
                        z=(a+b,a-b,self.acc[j]+a*b,self.acc[j]-a*b,max(a,b),min(a,b),a*b)[kind]
                        z=f32_chop(z)
                        if acc:self.acc[j]=z
                        else:self.vf[fd][j]=FB(z)
                if fn<0x1c:vec(fn>>2,fn&3)
                elif fn in (0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2f):vec({0x28:0,0x29:2,0x2a:6,0x2b:4,0x2c:1,0x2d:3,0x2f:5}[fn])
                elif fn==0x1c:vec(6,scalar=self.q)
                elif fn>=0x3c:
                    ext=(fd<<2)|(fn&3)
                    if ext<16:vec(ext>>2,ext&3,True)
                    elif 0x18<=ext<=0x1b:vec(6,ext&3,True)
                    elif ext==0x30:
                        src=self.vf[fs][:]
                        for j in range(4):
                            if mask&(8>>j):self.vf[ft][j]=src[j]
                    elif ext==0x31:
                        src=self.vf[fs][:]
                        for j in range(4):
                            if mask&(8>>j):self.vf[ft][j]=src[(j+1)%4]
                    elif ext in (0x38,0x39,0x3a):
                        a,b=BF(self.vf[fs][(w>>21)&3]),BF(self.vf[ft][(w>>23)&3])
                        self.q=F.retail.F32(a/b if ext==0x38 else math.sqrt(abs(b)) if ext==0x39 else a/math.sqrt(abs(b)))
                    elif ext==0x3b:pass
                    else:raise F.retail.Unsupported(f'VU0 especial{ext:#x} pc={pc:#x} w={w:#x}')
                else:raise F.retail.Unsupported(f'VU0 fn{fn:#x} pc={pc:#x}')
            else:raise F.retail.Unsupported(f'COP2 rs{s}')
        else:handled=False
        if not handled:return super().step()
        self.instructions.append((pc,w));old,self.pending=self.pending,None;self.pc=old if old is not None else pc+4
        self.vf[0]=[0,0,0,FB(1)];self.r[0]=0


def call(m,pc,args=()):return F.call(m,pc,args)


def oracle(frames=310,paso=1.):
    elf=ELF.read_bytes();m=EE(elf);b=entrada(972);base=0xC00000;m.mem[base:base+len(b)]=b
    visited=set()
    def relocate(p):
        while p:
            assert p not in visited;visited.add(p);child,nxt=u(b,p+0x24),u(b,p+0x28)
            if child:relocate(child)
            for off,v in ((0x24,child),(0x28,nxt)):m.store(base+p+off,4,base+v if v else 0)
            p=nxt
    relocate(u(b,12))
    for table,n,addr,stride in ((0x553250,128,0x900000,0x50),(0x554450,256,0xA00000,0x10),
                                (0x559850,256,0xA10000,0x80),(0x559c50,1920,0xA20000,0x100)):
        for i in range(n):m.store(table+4*i,4,addr+stride*i)
    for p in range(0x49cb40,0x49cb74,4):m.store(p,4,0)
    m.store(0x5b5a50,4,0x5b5aa0);m.store(0x5b5aa0+4,4,0x5b5a50)
    paso=BF(FB(paso));dt=BF(FB(paso/60))
    m.store(0x5509e0,4,1);m.store(0x5509e4,4,1);m.store(0x5509e8,4,FB(paso));m.store(0x5509ec,4,FB(dt))
    def memset():
        dst,n=m.r[4]&0xffffffff,m.r[6]&0xffffffff;m.mem[dst:dst+n]=bytes([m.r[5]&255])*n;m.low(2,dst)
    m.hooks[0x11def8]=memset
    def observe(pc):
        if pc==0x127ca0:m.random.append(m.r[4]&0xffffffff)
    m.observe=observe
    effect=call(m,0x277ea0,(base,0x10001,0));assert effect==0x900000,hex(effect)
    def snapshot(frame):
        ns=[];p=m.load(effect+0x40,4)
        while p:
            n=m.load(p+8,4);raw=m.load(n+0x14,4)-base;kind=u(b,raw+4);ps=[]
            if kind>>16==2:
                q=m.load(n+0x3c,4)
                while q!=n+0x44:
                    color=0x50 if kind==0x20003 else 0x30;uv=0x60 if kind==0x20003 else 0x40
                    size=0x70 if kind==0x20003 else 0x50
                    ps.append(dict(age=m.load(q+8,4),flags=m.load(q+12,4),
                        pos=[m.load(q+0x10+j*4,4) for j in range(3)],vel=[m.load(q+0x20+j*4,4) for j in range(3)],
                        color=[m.load(q+color+j*4,4) for j in range(4)],uv=[m.load(q+uv+j*4,4) for j in range(4)],
                        size=m.load(q+size,4),scale=m.load(q+size+4,4),
                        angle=m.load(q+0x58,4) if kind==0x20001 else 0,
                        quat=[m.load(q+0x30+j*4,4) for j in range(4)] if kind==0x20003 else [0,0,0,FB(1)]))
                    q=m.load(q,4)
            ns.append(dict(offset=raw,kind=kind,particles=ps));p=m.load(p,4)
        return dict(frame=frame,nodos=m.load(effect+0x48,4),rng=m.load(0x49be08,4),nodes=ns)
    states=[snapshot(0)]
    for frame in range(1,frames+1):
        call(m,0x277830,(effect,))
        if frame in (1,2,3,10,60,293,294,295,296,306,307,308,310) or frame==frames or (frame>310 and m.load(effect+0x48,4)!=states[-1]['nodos']):states.append(snapshot(frame))
        if m.load(effect+0x48,4)==0xffffffff:break
    return dict(elf_sha256=hashlib.sha256(elf).hexdigest(),ame_sha256=hashlib.sha256(b).hexdigest(),states=states,paso=paso,dt=dt,
        random=m.random,code={hex(pc):hex(w) for pc,w in m.instructions},instructions=len(m.instructions),hooks=m.boundaries)


def main():
    p=argparse.ArgumentParser();p.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF'));p.add_argument('--frames',type=int,default=310);p.add_argument('--paso',type=float,default=1.);a=p.parse_args()
    assert a.tmp.is_dir();out=a.tmp/('ame_F4_'+str(time.time_ns()));out.mkdir()
    result=oracle(a.frames,a.paso);(out/'original_ame.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    for s in result['states']:print('AME original',s['frame'],'nodos',s['nodos'],'particulas',sum(len(n['particles']) for n in s['nodes']),'rng',hex(s['rng']))
    print('Memory argumentos',sorted(set(result['random'])),'instrucciones',result['instructions']);print('ORACULO',out)


if __name__=='__main__':main()
