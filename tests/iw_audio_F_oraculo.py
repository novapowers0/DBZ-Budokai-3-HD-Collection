"""Oraculos separados: instrucciones retail y ROM documentada, sin fuente host."""
import array
import math
import re
import struct
from iw_ap_audio_prueba import U,H,hijo,F


class Mips:
    """Subconjunto minimo que ejecuta Memory, guardia SESQ y periodo SESQ."""
    def __init__(self, code):
        self.code=code
        self.r=[0]*32
        self.mem={}
        self.hi=self.lo=0
    def write(self,addr,value,n=4):
        for i in range(n):self.mem[addr+i]=value>>(8*i)&255
    def read(self,addr,n=4):
        return sum(self.mem.get(addr+i,0)<<(i*8) for i in range(n))
    def ejecutar(self,pc,fin):
        delay=None
        for _ in range(1000):
            if pc in fin:return pc
            w=self.code(pc);op=w>>26;s=w>>21&31;t=w>>16&31;d=w>>11&31
            k=w&65535;imm=k if k<32768 else k-65536
            old,delay=delay,None;r=self.r
            if op in (8,9):r[t]=(r[s]+imm)&0xffffffff
            elif op==0xc:r[t]=r[s]&k
            elif op==0xd:r[t]=r[s]|k
            elif op==0xf:r[t]=k<<16
            elif op==0x23:r[t]=self.read((r[s]+imm)&0xffffffff)
            elif op==0x25:r[t]=self.read((r[s]+imm)&0xffffffff,2)
            elif op==0x2b:self.write((r[s]+imm)&0xffffffff,r[t])
            elif op==2:delay=(pc+4)&0xf0000000|(w&0x3ffffff)*4
            elif op in (4,5,1):
                signed=r[s] if r[s]<0x80000000 else r[s]-0x100000000
                take=(r[s]==r[t]) if op==4 else (r[s]!=r[t]) if op==5 else signed>=0
                if take:delay=pc+4+imm*4
            elif op==0:
                fn=w&63
                if fn==0:r[d]=(r[t]<<((w>>6)&31))&0xffffffff
                elif fn==2:r[d]=r[t]>>((w>>6)&31)
                elif fn==8:delay=r[s]
                elif fn in (0x21,0x2d):r[d]=(r[s]+r[t])&0xffffffff
                elif fn==0x23:r[d]=(r[s]-r[t])&0xffffffff
                elif fn==0x25:r[d]=r[s]|r[t]
                elif fn==0x26:r[d]=r[s]^r[t]
                elif fn==0x2b:r[d]=int(r[s]<r[t])
                elif fn in (0x18,0x19):
                    product=r[s]*r[t];self.hi=product>>32&0xffffffff;self.lo=product&0xffffffff
                elif fn==0x1b:self.lo,self.hi=divmod(r[s],r[t])
                elif fn==0x10:r[d]=self.hi
                elif fn==0x12:r[d]=self.lo
                else:raise AssertionError((hex(pc),hex(w)))
            else:raise AssertionError((hex(pc),hex(w)))
            r[0]=0;pc=old if old is not None else pc+4
        raise AssertionError('limite de instrucciones retail')


def code_elf(elf):
    segments=[]
    for i in range(H(elf,44)):
        p=U(elf,28)+i*H(elf,42)
        if U(elf,p)==1:segments.append((U(elf,p+8),U(elf,p+16),U(elf,p+4)))
    def code(pc):
        for va,n,off in segments:
            if va<=pc<va+n:return U(elf,off+pc-va)
        raise ValueError(hex(pc))
    return code


def rom_documentada(text):
    table=text.split('The Gauss table contains the following values (in hex):',1)[1].split('The PSX table is',1)[0]
    table='\n'.join(line.split(';')[0] for line in table.splitlines())
    values=[int(x,16) for x in re.findall(r'(-?[0-9A-Fa-f]{3,4})h',table)]
    assert len(values)==512,len(values)
    return values


def pcm_original(amb,sound,rom):
    body,ind,sq=[hijo(amb,k) for k in range(3)]
    sc=sq.index(b'IECSqseS');group=sc+U(sq,sc+32);tab=group+4+U(sq,group+4)
    if sound>U(sq,group):return b'',dict(estado='fuera_de_banco',maximo=U(sq,group))
    rec=sc+U(sq,tab+4*sound);p=rec+U(sq,rec);ev=sq[p:p+U(sq,rec+8)]
    assert ev[:2]==b'\0\x90' and len(ev) in (7,13)
    if ev[4]==0 or sq[rec+4]==0:return b'',dict(estado='volumen_cero')
    sb=ind.index(b'IECSbteS');vi=ind.index(b'IECSigaV')
    prog=sb+U(ind,sb+U(ind,sb+16)+4+ev[2]*4)
    tone=prog+U(ind,prog)+ind[prog+4]*(ev[3]-ind[prog+5])
    sample=H(ind,tone);vr=vi+U(ind,vi+16+4*sample)
    start=U(ind,vr);hz=H(ind,vr+4);pitch=min(16383,4096*int(hz*65536/48000)>>16)
    gain=F(ev[4]*ind[tone+3]/16384)
    adsr1,adsr2=H(ind,tone+14),H(ind,tone+16)
    # Decodificador independiente por nibbles signados, con predictor en bloques.
    coeff=[(0,0),(60,0),(115,-52),(98,-55),(122,-60)]
    mono=[];h0=h1=0
    for off in range(start,len(body)-15,16):
        c0,c1=coeff[body[off]>>4];shift=body[off]&15
        for j in range(28):
            nib=(body[off+2+j//2]>>((j%2)*4))&15
            if nib>7:nib-=16
            x=max(-32768,min(32767,(nib*4096>>shift)+(c0*h0+c1*h1+32>>6)))
            mono.append(x);h1,h0=h0,x
        if body[off+1]&1:break
    keyoff=None if len(ev)==7 else (((ev[5]&127)*128+ev[6])*48)
    # Tasas ADSR descritas en la especificacion; contadores enteros de hardware.
    stage=0;level=clock=0;out=array.array('h');phase=0;frame=0
    while phase//4096<len(mono):
        if keyoff is not None and frame>=keyoff and stage!=3:stage=3;clock=0
        if stage==0:rate=(adsr1>>8)&127;decrease=False;exp=bool(adsr1&0x8000)
        elif stage==1:rate=((adsr1>>4)&15)*4;decrease=exp=True
        elif stage==2:rate=(adsr2>>6)&127;decrease=bool(adsr2&0x4000);exp=bool(adsr2&0x8000)
        else:rate=(adsr2&31)*4;decrease=True;exp=bool(adsr2&32)
        shift=rate//4;step=7-rate%4
        if decrease:step=~step
        step*=2**max(0,11-shift);inc=32768>>max(0,shift-11)
        if exp:
            if decrease:step=step*level>>15
            elif level>24576:
                if shift<10:step//=4
                elif shift==10:step//=2;inc//=2
                else:inc//=4
        frozen=(stage in (0,2) and rate==127) or (stage==3 and rate==124)
        clock+=inc if frozen else max(1,inc)
        if clock>=32768:clock=0;level=max(0,min(32767,level+step))
        if stage==0 and level==32767:stage=1
        elif stage==1 and level<=((adsr1&15)+1)*2048:stage=2
        index=phase//4096;fraction=(phase%4096)//16
        taps=[mono[index-3+k] if index-3+k>=0 else 0 for k in range(4)]
        co=[rom[255-fraction],rom[511-fraction],rom[256+fraction],rom[fraction]]
        interpolated=max(-32768,min(32767,sum((x*c)>>15 for x,c in zip(taps,co))))
        x=int(F(F(interpolated*gain)*F(level/32768)))
        out.extend([x,x]);frame+=1;phase+=pitch
        if stage==3 and level==0:break
    return out.tobytes(),dict(estado='pcm',muestra=sample,pitch=pitch,adsr1=adsr1,adsr2=adsr2,rutas=ind[tone+13]&15,
                             cuadros=frame,apagado=keyoff,hz=48000)
