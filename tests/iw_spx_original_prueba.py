"""Ejecuta codigo MIPS retail EU y lo contrasta con iw_spx.h black-box.

Solo libreria estandar. No usa pseudocodigo como oraculo. No arranca PCSX2.
python -B tests/iw_spx_original_prueba.py --tmp D:/tmp/solIWV
La RAM/contexto y los callbacks de estimulo son fixtures, NO una partida grabada.
Los opcodes SPX, sus lectores y LoadWaitSpxScript salen del ELF sin traducirlos
a Python. Cada instruccion MIPS no soportada detiene el caso con PC/word.
"""
import argparse
import collections
import hashlib
import json
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
from datetime import datetime

MASK = (1 << 64) - 1
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
S32 = lambda v: (v & 0x7fffffff) - (v & 0x80000000)
F32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
FB = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]
BF = lambda v: struct.unpack('<f', struct.pack('<I', v & 0xffffffff))[0]
SX = lambda v, n: (v & ((1 << (n-1))-1)) - (v & (1 << (n-1)))
REG, SCRIPT, BASE = 0x920000, 0x800000, 0x800024
TEMP, STACK, INSTANCE, TASK = 0x900000, 0x910000, 0xA00000, 0xA01000
END, PRE, POST, POLL, NAT0, NAT1, CREATE, FINISH, THREAD_END = (
    0x1FFF000, 0xB00000, 0xB00080, 0xB00100, 0xB00180, 0xB00200,
    0xB00280, 0xB00300, 0xB00380)
CONTROL, TABLE = 0xB01000, 0xA04000
START = 0xB00400


class Unsupported(Exception):
    pass


class Mips:
    """Subconjunto ISA R5900: escalares64, delay/likely, LQ/SQ128, COP1 finito.

    No contiene switch SPX ni funciones equivalente a Valor/Op/Ejecutar.
    No simula VU, kernels, temporizadores hardware o floats EE excepcionales.
    """
    def __init__(self, elf):
        self.mem = bytearray(32 * 1024 * 1024)
        self.exec_ranges = []
        for i in range(struct.unpack_from('<H', elf, 44)[0]):
            p = U32(elf, 28) + i * struct.unpack_from('<H', elf, 42)[0]
            typ, off, va, _, size, total, flags, _ = struct.unpack_from('<8I', elf, p)
            if typ == 1:
                assert va + total <= len(self.mem)
                self.mem[va:va+size] = elf[off:off+size]
                if flags & 1:
                    self.exec_ranges.append((va, va+size))
        self.r, self.fp = [0]*32, [0]*32
        self.hi = self.lo = 0
        self.cond = False
        self.pc = 0
        self.pending = None
        self.hooks, self.observe = {}, None
        self.instructions, self.external, self.boundaries = [], [], []

    def load(self, a, n):
        if a < 0 or a+n > len(self.mem):
            raise Unsupported(f'RAM {a:#x}+{n}')
        return int.from_bytes(self.mem[a:a+n], 'little')

    def store(self, a, n, v):
        if a < 0 or a+n > len(self.mem):
            raise Unsupported(f'RAM write {a:#x}+{n}')
        self.mem[a:a+n] = (v & ((1 << (n*8))-1)).to_bytes(n, 'little')

    def low(self, i, v):
        if i:
            self.r[i] = (self.r[i] & ~MASK) | (v & MASK)

    def word(self, i, v):
        self.low(i, SX(v & 0xffffffff, 32))

    def call(self, pc, a0, max_steps=100000):
        self.r = [0]*32
        self.r[4], self.r[28], self.r[29], self.r[31] = a0, 0x4A3DF0, 0x1F00000, END
        self.pc, self.pending = pc, None
        for _ in range(max_steps):
            if self.pc == END:
                return
            self.step()
        raise Unsupported(f'limite instrucciones en {self.pc:#x}')

    def step(self):
        pc = self.pc
        if self.observe:
            self.observe(pc)
        if pc in self.hooks:
            if self.pending is not None:
                raise Unsupported('hook en delay slot')
            self.external.append(pc)
            args=[v & MASK for v in self.r[4:10]]
            self.hooks[pc]()
            self.boundaries.append(dict(pc=pc,args=args,v0=self.r[2] & MASK))
            self.pc = self.r[31] & 0xffffffff
            return
        w = self.load(pc, 4)
        self.instructions.append((pc, w))
        old, self.pending = self.pending, None
        op, s, t, d, sh, fn = w>>26, w>>21 & 31, w>>16 & 31, w>>11 & 31, w>>6 & 31, w & 63
        a, b = self.r[s] & MASK, self.r[t] & MASK
        imm, sim = w & 65535, SX(w & 65535, 16)
        nxt = pc+4

        def branch(take, target, likely=False):
            nonlocal nxt
            if old is not None:
                raise Unsupported('branch dentro de delay slot')
            if take:
                self.pending = target & 0xffffffff
            elif likely:
                nxt += 4

        if op == 0:
            if fn in (0,2,3):
                val = (b & 0xffffffff) << sh if fn == 0 else (b & 0xffffffff) >> sh if fn == 2 else SX(b & 0xffffffff,32) >> sh
                self.word(d,val)
            elif fn in (4,6,7):
                n = a & 31
                val = b << n if fn == 4 else (b & 0xffffffff) >> n if fn == 6 else SX(b & 0xffffffff,32) >> n
                self.word(d,val)
            elif fn in (8,9):
                target = a
                if fn == 9: self.low(d,pc+8)
                branch(True,target)
            elif fn in (10,11):
                if (b == 0) == (fn == 10): self.low(d,a)
            elif fn in (0x21,0x23): self.word(d, a+b if fn == 0x21 else a-b)
            elif fn in (0x2d,0x2f): self.low(d,a+b if fn == 0x2d else a-b)
            elif fn == 0x24: self.low(d,a & b)
            elif fn == 0x25: self.low(d,a | b)
            elif fn == 0x26: self.low(d,a ^ b)
            elif fn == 0x27: self.low(d,~(a | b))
            elif fn in (0x2a,0x2b): self.low(d,int(SX(a,64)<SX(b,64) if fn == 0x2a else a<b))
            elif fn in (0x18,0x19):
                z = SX(a & 0xffffffff,32)*SX(b & 0xffffffff,32) if fn == 0x18 else (a & 0xffffffff)*(b & 0xffffffff)
                self.lo, self.hi = SX(z & 0xffffffff,32) & MASK, SX((z>>32)&0xffffffff,32) & MASK
                self.low(d,self.lo)  # R5900 MULT rd,rs,rt (rd puede ser distinto de0).
            elif fn in (0x1a,0x1b):
                av,bv = (SX(a&0xffffffff,32),SX(b&0xffffffff,32)) if fn==0x1a else (a&0xffffffff,b&0xffffffff)
                if not bv: raise Unsupported('DIV cero EE no modelado')
                q=abs(av)//abs(bv); q=-q if (av<0)!=(bv<0) else q
                self.lo,self.hi=SX(q&0xffffffff,32)&MASK,SX((av-q*bv)&0xffffffff,32)&MASK
            elif fn == 0x10: self.low(d,self.hi)
            elif fn == 0x12: self.low(d,self.lo)
            elif fn == 0x11: self.hi=a
            elif fn == 0x13: self.lo=a
            elif fn in (0x38,0x3a,0x3b,0x3c,0x3e,0x3f):
                n=sh+(32 if fn>=0x3c else 0)
                val=b<<n if fn in (0x38,0x3c) else b>>n if fn in (0x3a,0x3e) else SX(b,64)>>n
                self.low(d,val)
            else: raise Unsupported(f'SPECIAL {fn:#x} pc={pc:#x} word={w:#x}')
        elif op in (9,0x19):
            (self.word if op==9 else self.low)(t,a+sim)
        elif op in (0xa,0xb): self.low(t,int(SX(a,64)<sim if op==0xa else a<(sim & MASK)))
        elif op == 0xc: self.low(t,a & imm)
        elif op == 0xd: self.low(t,a | imm)
        elif op == 0xe: self.low(t,a ^ imm)
        elif op == 0xf: self.word(t,imm<<16)
        elif op in (2,3):
            if op == 3: self.low(31,pc+8)
            branch(True,((pc+4)&0xf0000000)|((w&0x3ffffff)<<2))
        elif op in (4,5,6,7,0x14,0x15,0x16,0x17):
            k=op & 15
            take=(a==b if k==4 else a!=b if k==5 else SX(a,64)<=0 if k==6 else SX(a,64)>0)
            branch(take,pc+4+4*sim,op>=0x14)
        elif op == 1:
            if t not in (0,1,2,3,16,17): raise Unsupported(f'REGIMM {t}')
            take=(SX(a,64)<0)==(t in (0,2,16))
            if t>=16: self.low(31,pc+8)
            branch(take,pc+4+4*sim,t in (2,3))
        elif op in (0x20,0x21,0x23,0x24,0x25,0x27,0x37,0x1e):
            n={0x20:1,0x24:1,0x21:2,0x25:2,0x23:4,0x27:4,0x37:8,0x1e:16}[op]
            value=self.load((a+sim)&0xffffffff,n)
            if op==0x1e:
                if t: self.r[t]=value
            else: self.low(t,SX(value,n*8) if op in (0x20,0x21,0x23) else value)
        elif op in (0x28,0x29,0x2b,0x3f,0x1f):
            n={0x28:1,0x29:2,0x2b:4,0x3f:8,0x1f:16}[op]
            self.store((a+sim)&0xffffffff,n,self.r[t])
        elif op in (0x31,0x39):
            addr=(a+sim)&0xffffffff
            if op==0x31: self.fp[t]=self.load(addr,4)
            else: self.store(addr,4,self.fp[t])
        elif op == 0x11:
            if s==0: self.word(t,self.fp[d])
            elif s==4: self.fp[d]=b&0xffffffff
            elif s==8:
                branch(self.cond==bool(t&1),pc+4+4*sim,bool(t&2))
            elif s in (16,20):
                ft,fs,fd=t,d,sh
                x,y=BF(self.fp[fs]),BF(self.fp[ft])
                if fn in (0,1,2,3,4,5,6,7):
                    if fn==0: z=x+y
                    elif fn==1: z=x-y
                    elif fn==2: z=x*y
                    elif fn==3: z=x/y
                    elif fn==4: z=math.sqrt(x)
                    elif fn==5: z=abs(x)
                    elif fn==6: z=x
                    else: z=-x
                    self.fp[fd]=FB(F32(z))
                elif fn in (0x0d,0x24):
                    if not math.isfinite(x) or not -2147483648<=x<2147483648: raise Unsupported('conversion COP1 excepcional')
                    self.fp[fd]=int(x)&0xffffffff
                elif fn==0x20 and s==20: self.fp[fd]=FB(float(S32(self.fp[fs])))
                elif fn in (0x32,0x34,0x36,0x3c,0x3e): self.cond=x==y if fn==0x32 else x<y if fn in (0x34,0x3c) else x<=y
                else: raise Unsupported(f'COP1 {s}/{fn:#x} en {pc:#x}')
            else: raise Unsupported(f'COP1 rs={s} en {pc:#x}')
        else:
            raise Unsupported(f'ISA op={op:#x} en {pc:#x}, word={w:#x}')
        self.r[0]=0
        self.pc = old if old is not None else nxt


def u(v): return struct.pack('<I',v & 0xffffffff)
def imm(v): return b'\x30'+u(v)
def mov(v,r=0): return b'\x01'+imm(v)+bytes([r])
def native(i=0): return b'\x80'+imm(i)
def spx(code):
    b=bytearray(36); b[:4]=b'#SPX'; b[16:28]=u(32)+u(36)+u(1)
    return bytes(b)+code


def corpus():
    out=[]
    def add(name,code,**kw):
        c=dict(name=name,bytes=spx(code),ticks=1,options=0,ret0=1,ret1=1,ri=[],fi=[],mem=[],poll=[],pause=[])
        c.update(kw); c['poll']=c['poll'] or [1]*c['ticks']; c['pause']=c['pause'] or [0]*c['ticks']
        out.append(c)
    # Mismos estimulos, no resultados esperados: los produce el ELF retail.
    for op in (1,3,6,7,8,9):
        for mode in range(8):
            for sign in (0,1):
                flt=op in (3,7,9)
                value=FB(-2.75 if sign else 2.75) if flt else 0xffffff83 if sign else 0x12345678
                ri=[(2,0x950010),(3,4)]; fi=[(2,value)]
                if mode==0:
                    operand=b'\x02'; ri += [] if flt else [(2,value)]
                elif mode<=3:
                    n=1<<(mode-1); operand=bytes([mode<<4])+(value&((1<<(8*n))-1)).to_bytes(n,'little')
                elif mode==4: operand=b'\x42'
                elif mode==5: operand=b'\x52\xff'
                elif mode==6: operand=b'\x62\x03\x00'
                else: operand=b'\x72\x30'
                m=[(0x950000,bytes((i*37+11)&255 for i in range(64)))]
                # Datos finitos para todos los modos de memoria COP1.
                offset={4:16,5:15,6:19,7:20}.get(mode)
                if offset is not None: m.append((0x950000+offset,u(value)))
                code=bytes([op])+operand+(b'\x00' if op not in (8,9) else b'')+b'\x92'
                add(f'operand_{op:02x}_{mode}_{sign}',code,ri=ri,fi=fi,mem=m)
    for op in (2,4,5,0x10,0x11,0x17,0x33):
        for dst in (b'\x00',b'\x42',b'\x52\xff',b'\x62\x03\x00',b'\x72\x30'):
            add(f'dst_{op:02x}_{dst.hex()}',bytes([op])+dst+b'\x92',ri=[(0,0xffffff),(2,0x950010),(3,4)],mem=[(0x950000,bytes([255])*64)])
    rng=random.Random(0x145CD0)
    for op in (0x12,0x13,0x14,0x15,0x16,0x30,0x31,0x32,0x60,0x61,0x62,0x63,0x64):
        for i in range(5):
            a=rng.getrandbits(32); b=[0,1,31,0xffffffff,rng.getrandbits(32)][i]
            add(f'int_{op:02x}_{i}',bytes([op])+imm(b)+b'\x00\x92',ri=[(0,a)])
    for op in (0x63,0x64):
        for cantidad in (-65,-33,32,33,65):
            add(f'rotate_{op:02x}_{cantidad}',bytes([op])+imm(cantidad)+b'\x00\x92',ri=[(0,0xAB1234CD)])
    for op in (0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x29,0x50,0x51,0x52,0x53):
        for i,a in enumerate((2.75,-3.25,0.0)):
            if op in (0x20,0x21,0x27,0x29): code=bytes([op,0])+b'\x92'
            elif op in (0x50,0x52): code=bytes([op])+imm(FB(1.5) if op==0x52 else 1)+b'\x00\x92'
            elif op in (0x51,0x53): code=bytes([op,0])+b'\x92'
            else: code=bytes([op])+imm(FB(2.0))+b'\x00\x92'
            if op==0x29 and a<0: continue
            add(f'num_{op:02x}_{i}',code,ri=[(0,int(a)&0xffffffff)],fi=[(0,FB(a))])
    for op in (0xa,0xb):
        for dst in (0,11):
            add(f'pop_{op:02x}_{dst}',bytes([op,dst,0x92]),ri=[(11,STACK+0xff0)],mem=[(STACK+0xff0,u(STACK)+u(123))])
    # Saltos/call/ret con banderas altas: se observa el PC real siguiente.
    for op in (0x70,0x71,0x72,0x73,0x74,0x75):
        for cond in (0,1):
            operand=(imm(cond) if op in (0x71,0x72,0x74,0x75) else b'')
            n=1+len(operand)+5
            target=n+len(mov(10))+1
            code=bytes([op])+operand+imm(0xf0000000|target)+mov(10)+b'\x92'+mov(20)+b'\x92'
            add(f'branch_{op:02x}_{cond}',code)
    for op in range(0x76,0x7c):
        for cond in (0,1):
            args=(imm(cond) if op>=0x78 else b'')+(imm(0xe0000020) if op in (0x77,0x79,0x7b) else b'')
            code=bytes([op])+args+b'\x92'
            code=code.ljust(32,b'\x00')+mov(27)+b'\x92'
            for empty in (False,True):
                add(f'return_{op:02x}_{cond}_{int(empty)}',code,
                    ri=[] if empty else [(11,STACK+0xffc)],mem=[] if empty else [(STACK+0xffc,u(BASE+32))])
    for ret in (1,2,3,0x203,4,5,6,0x107,8):
        code=native()+mov(42)+b'\x92'+b'\x95'
        add(f'native_return_{ret}',code,ticks=1 if ret in (4,5,6) else 4,ret0=ret,ret1=3,options=3,
            ri=[(0,BASE+len(code)-1),(1,0xffffffff)] if ret==5 else [])
    for n in (0,1,2,3,0x80000000,0xffffffff,65537):
        add(f'wait_{n}',b'\x93'+imm(n)+mov(5)+b'\x92\x95',ticks=5,options=3)
    add('pause_wait',b'\x93'+imm(3)+mov(13)+b'\x92\x95',ticks=6,options=3,pause=[0,1,0,0,0,0])
    add('poll_before_pause',mov(14)+b'\x92\x95',ticks=4,options=7,poll=[3,1,1,1],pause=[1,1,0,0])
    add('poll_restart',mov(15)+b'\x92',ticks=2,options=7,poll=[2,1])
    for op in (0x91,0x98,0x96,0x99):
        code=bytes([op])+imm(0xf0000020)+(imm(0xffffffff) if op in (0x98,0x99) else b'')+b'\x92'
        code=code.ljust(32,b'\x00')+b'\x92'
        add(f'create_{op:02x}',code,options=3,ri=[(11,STACK+0xff0)],mem=[(STACK+0xff4,u(0xffffffff))])
    return out


def emit_fixture(path,cases):
    def blob(b): return u(len(b))+b
    out=u(0x5350584f)+u(len(cases))
    for c in cases:
        out+=blob(c['name'].encode())+blob(c['bytes'])+u(c['ticks'])+u(c['options'])+u(c['ret0'])+u(c['ret1'])
        out+=b''.join(u(x) for x in c['poll']+c['pause'])
        for k in ('ri','fi'):
            out+=u(len(c[k]))+b''.join(u(i)+u(v) for i,v in c[k])
        out+=u(len(c['mem']))+b''.join(u(a)+blob(b) for a,b in c['mem'])
    path.write_bytes(out)


class Retail:
    def __init__(self,elf,c):
        self.cpu=Mips(elf); self.c=c; self.tick=0; self.events=[]; self.callbacks=[]
        self.current=TASK; self.tasks=[]; self.serial=0
        m=self.cpu
        m.mem[SCRIPT:SCRIPT+len(c['bytes'])]=c['bytes']
        regs=[0]*32
        for i,v in {8:BASE,9:BASE,10:BASE,11:STACK+0x1000,12:0x930000,13:0x940000,14:TEMP,15:STACK}.items(): regs[i]=v
        for i,v in enumerate(regs): m.store(REG+4*i,4,v)
        for off,v in {4:SCRIPT,8:BASE,12:1,16:0x3000,20:1,24:1,32:0,36:0x930000,40:0x940000,44:TEMP,48:STACK,52:REG,60:0x400,64:0x1000,68:TABLE,72:START,76:FINISH,80:CREATE,84:THREAD_END,88:INSTANCE+0x100}.items(): m.store(INSTANCE+off,4,v)
        for i in range(32): m.store(INSTANCE+0x100+i,1,i)
        m.store(0x49C8F4,4,INSTANCE)
        m.store(0x4AD558,4,0x4AD570)
        m.store(0x4AD570+0x28,4,0x4AD570)
        m.store(TABLE,4,TABLE+0x100); m.store(TABLE+0x100,4,NAT0); m.store(TABLE+0x104,4,NAT1)
        self.fixture_code()
        # Solo los servicios no-VM: un bloque del pool de tasks y memset de C.
        m.hooks[0x140300]=self.allocate
        m.hooks[0x11DEF8]=self.memset
        self.opframes=[]
        self.pending_native=None
        self.callback_returns=[]
        self.op_targets={m.load(m.load(0x4202d0+4*(op>>4),4)+4*(op&15),4):op
                         for op in list(range(12))+list(range(0x10,0x19))+list(range(0x20,0x2a))+list(range(0x30,0x34))+list(range(0x40,0x47))+list(range(0x50,0x54))+list(range(0x60,0x65))+list(range(0x70,0x7c))+[0x80]+list(range(0x90,0x9a))}
        m.observe=self.observe
        # Arranque retail COMPLETO145C10 ->1490B0; no se copia la logica C++.
        m.call(0x145c10,0)
        self.events.append(dict(case=c['name'],kind='initial',state=m.load(INSTANCE+12,4),
            live=m.load(INSTANCE+32,4),priority=m.load(self.current+0x18,4),header=m.load(SCRIPT+0x1c,4),
            **self.snapshot(REG),callbacks=self.callbacks[:]))
        self.callbacks=[]
        for i,v in c['ri']: m.store(REG+4*i,4,v)
        for i,v in c['fi']: m.store(REG+64+4*i,4,v)
        for a,b in c['mem']: m.mem[a:a+len(b)]=b

    def task(self,a,priority):
        m=self.cpu
        for off,v in {0x10:1,0x14:1,0x18:priority,0x1c:0x145cd0,0x20:0x145fa0,0x28:0x4ad570,0x30:INSTANCE,
                      0x3c:PRE if self.c['options']&1 else 0,0x40:POLL if self.c['options']&4 else 0,0x44:POST if self.c['options']&2 else 0}.items(): m.store(a+off,4,v)

    def fixture_code(self):
        m=self.cpu
        def asm(a,words):
            for i,v in enumerate(words): m.store(a+i*4,4,v)
        # Callbacks escritos como instrucciones, no hooks que salten la VM.
        def inc(a,reg): asm(a,[0x8ca80000+4*reg,0x25080001,0xaca80000+4*reg,0x03e00008,0])
        inc(PRE,6); inc(POST,7)
        asm(POLL,[0x8ca80014,0x25080001,0xaca80014,0x3c0800b0,0x8d021008,
                  0x304900ff,0x240a0001,0x152a0002,0,0xac800010,0x03e00008,0])
        asm(NAT0,[0x3c0800b0,0x8d021000,0x03e00008,0])
        asm(NAT1,[0x3c0800b0,0x8d021004,0x03e00008,0])
        words=[]
        for off,addr,bit in ((12,PRE,1),(16,POLL,4),(20,POST,2)):
            if self.c['options']&bit: words += [0x3c080000|(addr>>16),0x35080000|(addr&65535),0xac880000|off]
        asm(CREATE,words+[0x03e00008,0])
        asm(FINISH,[0x03e00008,0]); asm(THREAD_END,[0x03e00008,0])
        asm(START,[0x8c880004,0x24090055,0xad09001c,0x03e00008,0])

    def allocate(self):
        m=self.cpu; a=0xA10000+self.serial*0x1000; self.serial+=1
        m.mem[a:a+0x100]=bytes(0x100)
        self.task(a,m.r[6]&0xffffffff); self.tasks.append(a); self.current=a
        m.store(a+0x28,4,0x4ad570)
        if self.serial==1: m.store(0x4ad558,4,a)
        m.low(2,a)

    def memset(self):
        m=self.cpu; a,v,n=m.r[4]&0xffffffff,m.r[5]&255,m.r[6]&0xffffffff
        if a+n>len(m.mem): raise Unsupported('memset fuera de RAM')
        m.mem[a:a+n]=bytes([v])*n; m.low(2,a)

    def snapshot(self,regs):
        m=self.cpu; rr=[m.load(regs+4*i,4) for i in range(32)]
        st=rr[15]+0x1000-32
        return dict(r=rr[:16],f=rr[16:],mem=list(m.mem[0x950000:0x950040]),stack=list(m.mem[st:st+32]))

    def observe(self,pc):
        m=self.cpu
        if pc in self.op_targets:
            regs=m.r[5]&0xffffffff; p=m.r[6]&0xffffffff
            self.opframes.append((m.r[31]&0xffffffff,m.load(p,4)-1,regs,self.op_targets[pc]))
        if self.opframes and pc==self.opframes[-1][0]:
            _,pos,regs,op=self.opframes.pop()
            self.events.append(dict(case=self.c['name'],kind='op',tick=self.tick,slot=(regs-REG)//128,pc=pos,code=op,ret=m.r[2]&0xffffffff,**self.snapshot(regs)))
        labels={PRE:'pre',POST:'post',POLL:'poll',NAT0:'nativo0',NAT1:'nativo1',FINISH:'fin',THREAD_END:'terminar',START:'inicio'}
        if pc in labels: self.callbacks.append(labels[pc])
        if pc==CREATE:
            self.callbacks.append('crear:'+str(m.load(m.r[5]&0xffffffff,4)))
        if pc in (NAT0,NAT1):
            self.pending_native=(m.r[31]&0xffffffff,m.r[5]&0xffffffff,0 if pc==NAT0 else 1)
        if self.pending_native and pc==self.pending_native[0]:
            _,regs,i=self.pending_native; self.pending_native=None
            self.events.append(dict(case=self.c['name'],kind='native',tick=self.tick,slot=(regs-REG)//128,pc=m.load(regs+36,4),code=i,ret=m.r[2]&0xffffffff,**self.snapshot(regs)))

    def run(self):
        m=self.cpu; c=self.c
        for self.tick in range(1,c['ticks']+1):
            self.callbacks=[]
            count=m.load(INSTANCE+32,4)
            m.store(INSTANCE+12,4,3 if c['pause'][self.tick-1] else 2 if count else 0)
            m.store(CONTROL,4,c['ret0']); m.store(CONTROL+4,4,c['ret1']); m.store(CONTROL+8,4,c['poll'][self.tick-1])
            if m.load(self.current+0x1c,4)!=0xffffffff:
                m.call(0x145cd0,self.current)
            children=[]
            for slot in range(1,2):
                task=next((a for a in self.tasks if m.load(a+0x34,4)==REG+128*slot),None)
                children.append(dict(active=int(task is not None and m.load(task+0x1c,4)!=0xffffffff),priority=m.load(task+0x18,4) if task else 0,r=[m.load(REG+128*slot+4*i,4) for i in range(16)]))
            self.events.append(dict(case=c['name'],kind='tick',tick=self.tick,ok=1,state=m.load(INSTANCE+12,4),live=m.load(INSTANCE+32,4),wait=SX(m.load(self.current+0x38,2),16),**self.snapshot(REG),callbacks=self.callbacks[:],children=children))
        return self.events


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--tmp',type=Path,required=True)
    p.add_argument('--elf',type=Path,default=Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47'))
    p.add_argument('--antes',type=Path,help='evidencia.json anterior; conservar antes/despues sin reconstruir el oraculo')
    args=p.parse_args(); assert args.tmp.is_dir(),'verificar/crear antes el padre temporal'
    root=Path(__file__).resolve().parents[1]
    out=args.tmp/('spx_original_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f')); out.mkdir()
    elf=args.elf.read_bytes()
    assert elf[:6]==b'\x7fELF\x01\x01','ELF32 LE requerido'
    cases=corpus(); fixture=out/'fixtures.bin'; emit_fixture(fixture,cases)
    commands=[]
    def run(cmd,name):
        entorno=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out))
        ret=subprocess.run(list(map(str,cmd)),cwd=root,text=True,capture_output=True,env=entorno)
        (out/(name+'.log')).write_text(ret.stdout+ret.stderr,encoding='utf-8')
        commands.append(dict(command=list(map(str,cmd)),returncode=ret.returncode,log=name+'.log'))
        if ret.returncode: raise RuntimeError(name+': '+ret.stderr)
        return ret.stdout
    exe=out/'iw_spx_original_prueba.exe'
    run(['C:/Program Files/LLVM/bin/clang++.exe','--version'],'compilador_version')
    run(['C:/Program Files/LLVM/bin/clang++.exe','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=undefined','-fsanitize-trap=all',root/'tests/iw_spx_original_prueba.cpp','-o',exe],'compilar')
    result=run([exe,fixture],'cpp'); cpp=collections.defaultdict(list)
    for line in result.splitlines():
        d=json.loads(line); cpp[d['case']].append(d)
    regresion=out/'iw_spx_regresion.exe'
    run(['C:/Program Files/LLVM/bin/clang++.exe','-std=c++17','-O1','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=undefined','-fsanitize-trap=all',root/'tests/iw_spx_prueba.cpp','-o',regresion],'regresion_compilar')
    regression_output=run([regresion],'regresion')
    mismatches=[]; unsupported=[]; passed=[]; opcodes=set(); instruction_count=0; fixture_count=0; partial_count=0; external=collections.Counter()
    with (out/'retail.jsonl').open('w',encoding='utf-8') as logs,(out/'instrucciones.jsonl').open('w',encoding='utf-8') as machine:
        for c in cases:
            r=Retail(elf,c)
            try:
                actual=r.run()
                for d in actual: logs.write(json.dumps(d,separators=(',',':'))+'\n')
                ins=r.cpu.instructions
                retail_n=sum(any(a<=pc<b for a,b in r.cpu.exec_ranges) for pc,_ in ins)
                instruction_count+=retail_n; fixture_count+=len(ins)-retail_n; external.update(r.cpu.external)
                machine.write(json.dumps(dict(case=c['name'],instructions=ins,external_calls=r.cpu.boundaries),separators=(',',':'))+'\n')
                opcodes.update(e['code'] for e in actual if e['kind']=='op')
                if actual!=cpp[c['name']]:
                    i=next((i for i,(a,b) in enumerate(zip(actual,cpp[c['name']])) if a!=b),min(len(actual),len(cpp[c['name']])))
                    a=actual[i] if i<len(actual) else None; b=cpp[c['name']][i] if i<len(cpp[c['name']]) else None
                    keys=[k for k in set(a or {})|set(b or {}) if (a or {}).get(k)!=(b or {}).get(k)]
                    mismatches.append(dict(case=c['name'],event=i,keys=keys,original=a,cpp=b))
                else: passed.append(c['name'])
            except (Unsupported,ZeroDivisionError,OverflowError) as e:
                unsupported.append(dict(case=c['name'],reason=str(e),pc=r.cpu.pc))
                partial_count+=sum(any(a<=pc<b for a,b in r.cpu.exec_ranges) for pc,_ in r.cpu.instructions)
                machine.write(json.dumps(dict(case=c['name'],partial=True,reason=str(e),instructions=r.cpu.instructions,external_calls=r.cpu.boundaries),separators=(',',':'))+'\n')
    evidence=dict(invocacion=[sys.executable]+sys.argv,python=sys.version,elf=str(args.elf),elf_sha256=hashlib.sha256(elf).hexdigest(),
                  core_sha256=hashlib.sha256((root/'src/iw_spx.h').read_bytes()).hexdigest(),
                  driver_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  test_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (Path(__file__),root/'tests/iw_spx_original_prueba.cpp',root/'tests/iw_spx_prueba.cpp')},
                  fixtures_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),commands=commands,
                  cases=len(cases),passed=passed,mismatches=mismatches,unsupported=unsupported,
                  status='FAIL' if mismatches else 'PARTIAL' if unsupported else 'PASS',regression=regression_output.strip(),
                  retail_instructions=instruction_count,fixture_callback_instructions=fixture_count,
                  partial_retail_instructions=partial_count,
                  fixture_code_addresses={name:hex(a) for name,a in dict(pre=PRE,post=POST,poll=POLL,native0=NAT0,native1=NAT1,create=CREATE,finish=FINISH,thread_end=THREAD_END,start=START).items()},
                  opcodes=sorted(opcodes),external_hooks={hex(k):v for k,v in external.items()},
                  limits=['contexto RAM controlado; sin savestate ni ejecucion de una partida',
                          'callbacks de estimulo MIPS sinteticos, no builtins de juego',
                          '140300 pool de tasks y11DEF8 memset son servicios sustituidos explicitamente',
                          'no se ejecuta el planificador global140200: hijos con prioridad anterior se difieren',
                          'COP1 finito; no se certifican NaN, subnormales, VU ni libreria trigonometrica'])
    if args.antes:
        previo=args.antes.read_bytes(); anterior=json.loads(previo)
        assert anterior['elf_sha256']==evidence['elf_sha256'],'el antes debe usar el mismo ELF'
        evidence['antes']=dict(path=str(args.antes),sha256=hashlib.sha256(previo).hexdigest(),
            core_sha256=anterior['core_sha256'],cases=anterior['cases'],
            mismatches=[dict(case=d['case'],keys=d['keys']) for d in anterior['mismatches']])
    for name in ('retail.jsonl','cpp.log','instrucciones.jsonl','regresion.log','regresion_compilar.log','compilar.log','compilador_version.log'):
        evidence.setdefault('logs_sha256',{})[name]=hashlib.sha256((out/name).read_bytes()).hexdigest()
    (out/'evidencia.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
    print('EVIDENCIA',out)
    print('ELF SHA256',evidence['elf_sha256'])
    print('CASOS',len(cases),'PASS',len(passed),'DIFERENCIAS',len(mismatches),'NO_SOPORTADOS',len(unsupported),'INSTRUCCIONES_RETAIL',instruction_count,'OPCODES',len(opcodes))
    print('REGRESION',regression_output.strip())
    for d in mismatches[:20]: print('DIFF',d['case'],d['event'],d['keys'])
    for d in unsupported[:10]: print('NO_SOPORTADO',d)
    # Un timeout declarado es cobertura parcial. Una ISA inesperada NO pasa
    # como si fuera un test correcto, ni aunque los demas casos coincidan.
    if mismatches: return 1
    return 2 if any(not d['reason'].startswith('limite instrucciones') for d in unsupported) else 0


if __name__=='__main__':
    sys.exit(main())
