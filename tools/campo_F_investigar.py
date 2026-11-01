"""Campo solIWF: lectura de ELF/decomp/imagenes; nunca arranca ni compila juego."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


class Elf:
    def __init__(self,path):
        self.path=path;self.b=path.read_bytes()
        self.u=lambda o:struct.unpack_from('<I',self.b,o)[0]
        self.h=lambda o:struct.unpack_from('<H',self.b,o)[0]
    def off(self,va,n=4):
        for i in range(self.h(44)):
            p=self.u(28)+i*self.h(42)
            if self.u(p)==1 and self.u(p+8)<=va and va+n<=self.u(p+8)+self.u(p+16):return self.u(p+4)+va-self.u(p+8)
        raise ValueError(hex(va))
    def bytes(self,va,n):return self.b[self.off(va,n):self.off(va,n)+n]
    def u32(self,va):return struct.unpack('<I',self.bytes(va,4))[0]
    def f32(self,va):return struct.unpack('<f',self.bytes(va,4))[0]


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF/campo'))
    ap.add_argument('--elf',type=Path,default=Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47'))
    ap.add_argument('--assembly',nargs=2,type=lambda v:int(v,0))
    ap.add_argument('--ppc',nargs=2,type=lambda v:int(v,0))
    ap.add_argument('--imagen',type=Path,default=Path('D:/DBZ3HD/work/eu/us_img.bin'))
    ap.add_argument('--datos',nargs=2,type=lambda v:int(v,0))
    a=ap.parse_args();assert a.tmp.parent.is_dir();a.tmp.mkdir(exist_ok=True)
    e=Elf(a.elf)
    if a.datos:
        lo,hi=a.datos;b=e.bytes(lo,hi-lo)
        print(hex(lo),b.hex());print('u16',struct.unpack('<'+str(len(b)//2)+'H',b));return
    if a.ppc:
        import capstone
        lo,hi=a.ppc;b=a.imagen.read_bytes();d=capstone.Cs(capstone.CS_ARCH_PPC,capstone.CS_MODE_32|capstone.CS_MODE_BIG_ENDIAN)
        lines=[f'{i.address:08x}: {i.mnemonic} {i.op_str}' for i in d.disasm(b[lo-0x82000000:hi-0x82000000],lo)]
        (a.tmp/f'ppc_{lo:x}_{hi:x}.asm.txt').write_text('\n'.join(lines),encoding='utf-8')
        print('\n'.join(lines));return
    if a.assembly:
        import capstone
        lo,hi=a.assembly;d=capstone.Cs(capstone.CS_ARCH_MIPS,capstone.CS_MODE_MIPS64|capstone.CS_MODE_LITTLE_ENDIAN);d.skipdata=True
        lines=[f'{i.address:08x}: {i.mnemonic} {i.op_str}' for i in d.disasm(e.bytes(lo,hi-lo),lo)]
        (a.tmp/f'original_{lo:x}_{hi:x}.asm.txt').write_text('\n'.join(lines),encoding='utf-8')
        print('\n'.join(lines));return
    root=Path(__file__).resolve().parents[1]
    for name in ('suelo_iw.cpp','suelo_iw.h','iw_minijuegos.cpp','iw_minijuegos.h'):
        dest=a.tmp/(name+'.antes')
        if not dest.exists():dest.write_bytes((root/'src'/name).read_bytes())
    fields={}
    for addr in range(0x49c82c,0x49c88c+1,4):
        try:fields[hex(addr)]=dict(u32=e.u32(addr),f32=e.f32(addr))
        except ValueError:fields[hex(addr)]=dict(region='BSS/runtime, sin valor inicial en ELF')
    result=dict(elf=str(a.elf),sha256=hashlib.sha256(e.b).hexdigest(),iniciales=fields)
    (a.tmp/'original_globales.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
