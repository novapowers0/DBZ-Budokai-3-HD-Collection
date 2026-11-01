"""LIBSD/AMSOUND del jugador: parametros de reverb mediante MIPS original.

No arranca juego/dispositivo. Interprete R3000 acotado a la configuracion:
ejecuta las instrucciones del IRX y registra MMIO; IRQ/vaciado DMA se anotan.
Todos los resultados se conservan en --tmp; nunca se reparte el IRX.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from iw_ap_audio_prueba import U,H


class Irx:
    def __init__(self,path):
        self.path=path;self.raw=path.read_bytes()
        r=self.raw
        self.sections=[struct.unpack_from('<10I',r,U(r,32)+i*H(r,46)) for i in range(H(r,48))]
        self.imports={};self.exports={}
        for p in range(0,len(r)-20,4):
            magic=U(r,p)
            if magic not in (0x41e00000,0x41c00000):continue
            name=r[p+12:p+20].split(b'\0')[0].decode('ascii',errors='replace')
            if magic==0x41e00000:
                q=p+20
                while q+8<=len(r) and U(r,q)==0x03e00008:
                    sec=next(s for s in self.sections if s[4]<=q<s[4]+s[5] and s[2]&2)
                    self.imports[sec[3]+q-sec[4]]=(name,U(r,q+4)&65535);q+=8
            else:
                # Los ordinales0..3 pueden apuntar a0; no son terminador.
                vals=[];q=p+20
                while q+4<=len(r):
                    val=U(r,q)
                    if len(vals)>3 and not val:break
                    vals.append(val);q+=4
                self.exports[name]=vals
        self.sha256=hashlib.sha256(r).hexdigest()

    def interpretar(self,pc,args,attr=None,registros=None,memoria_extra=(),hasta=0x1fe000,leer_final=()):
        """R3000 sin JIT: ejecuta configuracion; kernel y DMA se registran aparte."""
        from iw_audio_F_oraculo import Mips
        mem={}
        for sec in self.sections:
            if sec[2]&2 and sec[1]!=8:
                mem.update(enumerate(self.raw[sec[4]:sec[4]+sec[5]],sec[3]))
        class Cpu(Mips):
            def write(self,addr,value,n=4):
                addr&=0x1fffffff
                if 0x1f900000<=addr<0x1f910000:writes.append(dict(direccion=hex(addr),bytes=n,valor=value&((1<<(8*n))-1)))
                super().write(addr,value,n)
            def read(self,addr,n=4):return super().read(addr&0x1fffffff,n)
        c=Cpu(lambda pc:sum(mem.get(pc+i,0)<<(8*i) for i in range(4)));c.mem=mem
        c.r[29]=0x1ff000;c.r[31]=0x1fe000
        for i,v in enumerate(args):c.r[4+i]=v
        if registros:
            for reg,value in registros.items():c.r[reg]=value
        if attr is not None:
            for i,b in enumerate(attr):mem[0x1fd000+i]=b
        for addr,value,n in memoria_extra:
            for i in range(n):mem[addr+i]=(value>>(8*i))&255
        # Fin del area SPU2 usado por libsd (registro EEA, no RAM del juego).
        for core in (0,1):
            mem[0x1f90033c+core*0x400]=0xf
            mem[0x1f90033e+core*0x400]=0xff;mem[0x1f90033f+core*0x400]=0xff
        writes=[];calls=[];delay=None
        def signed(v):return v if v<0x80000000 else v-0x100000000
        def ret(value=0):c.r[2]=value;return c.r[31]
        for count in range(50000):
            if pc==hasta:return dict(retorno=c.r[2],instrucciones=count,llamadas=calls,mmio=writes,
                                    memoria={hex(addr):c.read(addr) for addr in leer_final})
            if pc in self.imports or pc==0x660:
                name,ordinal=self.imports.get(pc,('libsd',25))
                calls.append(dict(pc=hex(pc),lib=name,ordinal=ordinal,args=c.r[4:8]))
                assert name in ('intrman','libsd'),(name,ordinal)
                pc=ret();continue # IRQ y vaciado DMA, sin sustituir aritmetica/parametros
            w=c.code(pc);op=w>>26;s=w>>21&31;t=w>>16&31;d=w>>11&31
            k=w&65535;im=k if k<32768 else k-65536;r=c.r
            old,delay=delay,None
            if op in (8,9):r[t]=(r[s]+im)&0xffffffff
            elif op==0xa:r[t]=int(signed(r[s])<im)
            elif op==0xb:r[t]=int(r[s]<(im&0xffffffff))
            elif op==0xc:r[t]=r[s]&k
            elif op==0xd:r[t]=r[s]|k
            elif op==0xf:r[t]=k<<16
            elif op in (0x20,0x21,0x23,0x24,0x25):
                n={0x20:1,0x21:2,0x23:4,0x24:1,0x25:2}[op];v=c.read((r[s]+im)&0xffffffff,n)
                if op in (0x20,0x21) and v>>(n*8-1):v|=0xffffffff<<n*8
                r[t]=v&0xffffffff
            elif op in (0x28,0x29,0x2b):c.write((r[s]+im)&0xffffffff,r[t],{0x28:1,0x29:2,0x2b:4}[op])
            elif op in (2,3):
                delay=(pc+4)&0xf0000000|(w&0x3ffffff)*4
                if op==3:r[31]=pc+8
            elif op in (1,4,5,6,7):
                take=(signed(r[s])>=0 if t==1 else signed(r[s])<0) if op==1 else r[s]==r[t] if op==4 else r[s]!=r[t] if op==5 else signed(r[s])<=0 if op==6 else signed(r[s])>0
                if take:delay=pc+4+im*4
            elif op==0:
                fn=w&63;shift=w>>6&31
                if fn==0:r[d]=(r[t]<<shift)&0xffffffff
                elif fn==2:r[d]=r[t]>>shift
                elif fn==3:r[d]=(signed(r[t])>>shift)&0xffffffff
                elif fn==4:r[d]=(r[t]<<(r[s]&31))&0xffffffff
                elif fn==6:r[d]=r[t]>>(r[s]&31)
                elif fn==8:delay=r[s]
                elif fn in (0x21,0x2d):r[d]=(r[s]+r[t])&0xffffffff
                elif fn==0x23:r[d]=(r[s]-r[t])&0xffffffff
                elif fn==0x24:r[d]=r[s]&r[t]
                elif fn==0x25:r[d]=r[s]|r[t]
                elif fn==0x26:r[d]=r[s]^r[t]
                elif fn==0x27:r[d]=~(r[s]|r[t])&0xffffffff
                elif fn==0x2a:r[d]=int(signed(r[s])<signed(r[t]))
                elif fn==0x2b:r[d]=int(r[s]<r[t])
                else:raise AssertionError((hex(pc),hex(w)))
            else:raise AssertionError((hex(pc),hex(w)))
            r[0]=0;pc=old if old is not None else pc+4
        raise AssertionError('limite de instrucciones de LIBSD')

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--juego',type=Path,default=Path('ps2_games/Infinite World (USA)'))
    ap.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF/reverb'))
    ap.add_argument('--inventario',action='store_true')
    ap.add_argument('--respaldo',action='store_true')
    a=ap.parse_args();assert a.tmp.parent.is_dir();a.tmp.mkdir(exist_ok=True)
    if a.respaldo:
        root=Path(__file__).resolve().parents[1]
        for name in ('iw_iop.h','iw_adx.h'):
            dest=a.tmp/(name+'.pre_reverb')
            if not dest.exists():dest.write_bytes((root/'src'/name).read_bytes())
        print('Respaldo conservado:',a.tmp);return
    lib=Irx(a.juego/'MODULES/LIBSD.IRX')
    hs=Irx(a.juego/'MODULES/MODHSYN.IRX')
    inventory=dict(libsd_exports=lib.exports,hsyn_exports=hs.exports,
                   mapa_modos_hsyn=[U(hs.raw,next(s[4]+0xc1e8-s[3] for s in hs.sections if s[3]<=0xc1e8<s[3]+s[5] and s[1]!=8)+4*i) for i in range(10)],
                   fuentes={str(p.path):p.sha256 for p in (lib,hs)})
    print(json.dumps(inventory,indent=2))
    if a.inventario:return
    # sceSdEffectAttr {core,mode,depthL,depthR,delay,feedback}; RPC22 modo3|100.
    pc=next(v[23] for k,v in lib.exports.items() if k=='libsd')
    attr=struct.pack('<IIhhII',0,0x103,0x50a,0x50a,0,0)
    result=lib.interpretar(pc,[0,0x1fd000],attr)
    inventory['efecto3']=result
    (a.tmp/'efecto3_original.json').write_text(json.dumps(inventory,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
