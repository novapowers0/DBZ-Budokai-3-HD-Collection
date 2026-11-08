"""Desensamble local acotado del ELF EU existente; sin modificarlo."""
import argparse
import hashlib
import struct
from pathlib import Path
import capstone

def main():
    ap=argparse.ArgumentParser();ap.add_argument('direccion',type=lambda x:int(x,0));ap.add_argument('tamano',type=lambda x:int(x,0));ap.add_argument('--datos',action='store_true');ap.add_argument('--elf',type=Path,default=Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47'));a=ap.parse_args()
    elf=a.elf
    b=elf.read_bytes();ph=struct.unpack_from('<I',b,28)[0];ps,pn=struct.unpack_from('<HH',b,42);o=-1
    for i in range(pn):
        tipo,off,va,_,tam=struct.unpack_from('<5I',b,ph+i*ps)
        if tipo==1 and va<=a.direccion and a.direccion+a.tamano<=va+tam:o=off+a.direccion-va;break
    assert 0<=o and o+a.tamano<=len(b)
    md=capstone.Cs(capstone.CS_ARCH_MIPS,capstone.CS_MODE_MIPS64+capstone.CS_MODE_LITTLE_ENDIAN);md.skipdata=True
    print('ELF bytes',len(b),'SHA256',hashlib.sha256(b).hexdigest())
    if a.datos:
        for i in range(0,a.tamano,4):print(f'{a.direccion+i:08X}: {struct.unpack_from("<I",b,o+i)[0]:08X}')
        return
    for i in md.disasm(b[o:o+a.tamano],a.direccion):print(f'{i.address:08X}: {i.mnemonic:10} {i.op_str}')

if __name__=='__main__':main()
