"""Interprete EE acotado para pruebas AP: ejecuta instrucciones retail.

No emula un juego ni perifericos. Opcode/call no soportado provoca error.
"""
import math
import struct

MASK=(1<<64)-1
def s32(v):return (v&0x7fffffff)-(v&0x80000000)
def s64(v):return (v&0x7fffffffffffffff)-(v&0x8000000000000000)
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]


class EE:
    def __init__(self,elf):
        self.m=bytearray(32*1024*1024)
        u=lambda p:struct.unpack_from('<I',elf,p)[0]
        h=lambda p:struct.unpack_from('<H',elf,p)[0]
        for i in range(h(44)):
            p=u(28)+i*h(42)
            if u(p)==1:self.m[u(p+8):u(p+8)+u(p+16)]=elf[u(p+4):u(p+4)+u(p+16)]
        self.reset()
    def reset(self):
        self.r=[0]*32;self.f=[0]*32;self.hi=self.lo=0;self.cond=False;self.acc=0.0
        self.r[28]=0x4a3df0;self.r[29]=0x1ff0000;self.r[31]=0x1fff000
        self.calls=[]
        self.watch=set();self.accesos=[]
    def read(self,a,n=4):
        a&=0x1fffffff;assert a+n<=len(self.m),hex(a)
        if a in self.watch:self.accesos.append((a,n))
        return int.from_bytes(self.m[a:a+n],'little')
    def write(self,a,v,n=4):
        a&=0x1fffffff;assert a+n<=len(self.m),hex(a)
        self.m[a:a+n]=(int(v)&((1<<(8*n))-1)).to_bytes(n,'little')
    def rf(self,n):return struct.unpack('<f',struct.pack('<I',self.f[n]))[0]
    def wf(self,n,v):self.f[n]=struct.unpack('<I',struct.pack('<f',v))[0]
    def float(self,a):return struct.unpack_from('<f',self.m,a)[0]
    def putf(self,a,v):struct.pack_into('<f',self.m,a,v)
    def run(self,pc,stop=None,hooks=None):
        stop={0x1fff000} if stop is None else set(stop);hooks=hooks or {};delay=None
        for _ in range(300000):
            if pc in stop:return pc
            if pc in hooks:
                self.calls.append((pc,self.r[4:8]));hooks[pc](self);pc=self.r[31];continue
            w=self.read(pc);op=w>>26;s=w>>21&31;t=w>>16&31;d=w>>11&31;sa=w>>6&31;k=w&65535;im=k if k<32768 else k-65536
            old,delay=delay,None;r=self.r;skip=False
            if op in (8,9):r[t]=s32(r[s]+im)&MASK
            elif op==0x19:r[t]=(r[s]+im)&MASK
            elif op==0xa:r[t]=int(s64(r[s])<im)
            elif op==0xb:r[t]=int(r[s]<(im&MASK))
            elif op==0xc:r[t]=r[s]&k
            elif op==0xd:r[t]=r[s]|k
            elif op==0xe:r[t]=r[s]^k
            elif op==0xf:r[t]=s32(k<<16)&MASK
            elif op in (0x20,0x21,0x23,0x24,0x25,0x27,0x37,0x1e):
                n={0x20:1,0x21:2,0x23:4,0x24:1,0x25:2,0x27:4,0x37:8,0x1e:16}[op]
                v=self.read((r[s]+im)&0xffffffff,n)
                if op in (0x20,0x21,0x23) and v>>(n*8-1):v|=MASK^((1<<(n*8))-1)
                r[t]=v
            elif op in (0x28,0x29,0x2b,0x3f,0x1f):self.write((r[s]+im)&0xffffffff,r[t],{0x28:1,0x29:2,0x2b:4,0x3f:8,0x1f:16}[op])
            elif op==0x31:self.f[t]=self.read((r[s]+im)&0xffffffff)
            elif op==0x39:self.write((r[s]+im)&0xffffffff,self.f[t])
            elif op in (2,3):
                delay=((pc+4)&0xf0000000)|((w&0x3ffffff)<<2)
                if op==3:r[31]=pc+8
            elif op in (1,4,5,6,7,0x14,0x15,0x16,0x17):
                typ=op if op<0x14 else op-0x10;likely=op>=0x14 or (op==1 and t in (2,3))
                take=(s64(r[s])>=0 if t&1 else s64(r[s])<0) if typ==1 else r[s]==r[t] if typ==4 else r[s]!=r[t] if typ==5 else s64(r[s])<=0 if typ==6 else s64(r[s])>0
                if take:delay=pc+4+4*im
                elif likely:skip=True
            elif op==0:
                fn=w&63
                if fn==0:r[d]=s32((r[t]&0xffffffff)<<sa)&MASK
                elif fn==2:r[d]=s32((r[t]&0xffffffff)>>sa)&MASK
                elif fn==3:r[d]=(s32(r[t])>>sa)&MASK
                elif fn==4:r[d]=s32(r[t]<<(r[s]&31))&MASK
                elif fn==6:r[d]=s32((r[t]&0xffffffff)>>(r[s]&31))&MASK
                elif fn==8:delay=r[s]&0xffffffff
                elif fn==9:delay=r[s]&0xffffffff;r[d]=pc+8
                elif fn in (0x20,0x21):r[d]=s32(r[s]+r[t])&MASK
                elif fn in (0x22,0x23):r[d]=s32(r[s]-r[t])&MASK
                elif fn==0x24:r[d]=r[s]&r[t]
                elif fn==0x25:r[d]=r[s]|r[t]
                elif fn==0x26:r[d]=r[s]^r[t]
                elif fn==0x27:r[d]=~(r[s]|r[t])&MASK
                elif fn==0x2a:r[d]=int(s64(r[s])<s64(r[t]))
                elif fn==0x2b:r[d]=int(r[s]<r[t])
                elif fn in (0x2c,0x2d):r[d]=(r[s]+r[t])&MASK
                elif fn in (0x2e,0x2f):r[d]=(r[s]-r[t])&MASK
                elif fn in (0x18,0x19):
                    a,b=(s32(r[s]),s32(r[t])) if fn==0x18 else (r[s]&0xffffffff,r[t]&0xffffffff)
                    value=a*b;self.hi=s32(value>>32)&MASK;self.lo=s32(value)&MASK
                    if d:r[d]=self.lo
                elif fn in (0x1a,0x1b):
                    a,b=(s32(r[s]),s32(r[t])) if fn==0x1a else (r[s]&0xffffffff,r[t]&0xffffffff)
                    assert b;quo=abs(a)//abs(b);quo=quo if (a<0)==(b<0) else -quo
                    self.lo=s32(quo)&MASK;self.hi=s32(a-quo*b)&MASK
                elif fn==0x10:r[d]=self.hi
                elif fn==0x12:r[d]=self.lo
                elif fn in (0x38,0x3c):r[d]=(r[t]<<(sa+(32 if fn==0x3c else 0)))&MASK
                elif fn in (0x3a,0x3e):r[d]=(r[t]&MASK)>>(sa+(32 if fn==0x3e else 0))
                elif fn in (0x3b,0x3f):r[d]=(s64(r[t])>>(sa+(32 if fn==0x3f else 0)))&MASK
                else:raise AssertionError((hex(pc),hex(w),'SPECIAL'))
            elif op==0x11:
                fs=d;fd=sa;ft=t;fn=w&63
                if s==0:r[t]=s32(self.f[fs])&MASK
                elif s==4:self.f[fs]=r[t]&0xffffffff
                elif s==8:
                    take=self.cond if t&1 else not self.cond
                    if take:delay=pc+4+im*4
                    elif t&2:skip=True
                elif s in (16,20):
                    a=self.rf(fs);b=self.rf(ft)
                    if fn==0:self.wf(fd,a+b)
                    elif fn==1:self.wf(fd,a-b)
                    elif fn==2:self.wf(fd,a*b)
                    elif fn==3:self.wf(fd,a/b)
                    elif fn==4:self.wf(fd,math.sqrt(a))
                    elif fn==5:self.wf(fd,abs(a))
                    elif fn==6:self.f[fd]=self.f[fs]
                    elif fn==7:self.wf(fd,-a)
                    elif fn==0x18:self.acc=f32(a+b)
                    elif fn==0x19:self.acc=f32(a-b)
                    elif fn==0x1a:self.acc=f32(a*b)
                    elif fn==0x1c:self.wf(fd,self.acc+f32(a*b))
                    elif fn==0x1d:self.wf(fd,self.acc-f32(a*b))
                    elif fn==0x1e:self.acc=f32(self.acc+f32(a*b))
                    elif fn==0x1f:self.acc=f32(self.acc-f32(a*b))
                    elif fn==0x20 and s==20:self.wf(fd,s32(self.f[fs]))
                    elif fn>=0x30:self.cond=bool((fn&4 and a<b) or (fn&2 and a==b) or (fn&1 and (math.isnan(a) or math.isnan(b))))
                    else:raise AssertionError((hex(pc),hex(w),'FPU'))
                else:raise AssertionError((hex(pc),hex(w),'COP1'))
            else:raise AssertionError((hex(pc),hex(w),'opcode'))
            r[0]=0;pc=old if old is not None else pc+(8 if skip else 4)
        raise AssertionError('limite de instrucciones EE')
