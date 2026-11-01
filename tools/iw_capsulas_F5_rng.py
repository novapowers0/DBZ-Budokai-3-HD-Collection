"""Sitios originales Memory(32); solo lectura del ELF, sin cambiar RNG ajeno."""
import hashlib
import json
from pathlib import Path
import struct
import time
import capstone
from iw_capsulas_F_datos import ELF


def main():
    b=ELF.read_bytes();out=Path('D:/tmp/solIWF')/('rng_F5_'+str(time.time_ns()));assert out.parent.is_dir();out.mkdir()
    jal=0x0c000000|(0x127ca0>>2);candidatos=[];dis=capstone.Cs(capstone.CS_ARCH_MIPS,capstone.CS_MODE_MIPS64|capstone.CS_MODE_LITTLE_ENDIAN)
    for off in range(0x80,len(b)-4,4):
        if struct.unpack_from('<I',b,off)[0]!=jal:continue
        cerca=[struct.unpack_from('<I',b,off+d)[0] for d in (-4,4)]
        if 0x24040020 not in cerca:continue
        va=off-0x80+0x100000
        asm=[f'{i.address:08x}: {i.mnemonic} {i.op_str}' for i in dis.disasm(b[off-32:off+20],va-32)]
        candidatos.append(dict(jal=hex(va),asm=asm))
        if va<0x180000:print('\n'.join(asm),flush=True)
    (out/'Memory32.json').write_text(json.dumps(dict(elf=str(ELF),sha256=hashlib.sha256(b).hexdigest(),calls=candidatos),indent=2),encoding='utf-8');print('Sitios totales',len(candidatos),'EVIDENCIA',out,flush=True)


if __name__=='__main__':main()
