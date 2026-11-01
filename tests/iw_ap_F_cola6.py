"""Prueba acotada del lector6 real: B3E se lee, B40 no, sin VFX/audio."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from iw_ap_F_original import EE


def comprobar(elf):
    text=Path('E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c').read_text(encoding='utf-8',errors='replace')
    labels={m[2]:int(m[1],16) for m in re.finditer(r'^// ==== ([0-9a-f]+) (.+)$',text,re.M)}
    c=EE(elf);actor=0xa00000;c.r[4]=actor;c.write(actor+0x1380,0)
    c.write(actor+0xb3c,2,2);c.write(actor+0xb3e,7,2);c.write(actor+0xb40,5,2)
    hooks={}
    for name,ptr in [('LoadWaitMinigameObjectsFlag',0xc80000),('MiniGame_GetWork',0xc81000),('LoadWaitMinigamesFlag',0xc83000)]:
        hooks[labels[name]]=lambda vm,p=ptr:vm.r.__setitem__(2,p)
    def memset(vm):
        p,n=vm.r[4]&0xffffffff,vm.r[6]&0xffffffff
        assert n<0x1000;c.m[p:p+n]=bytes([vm.r[5]&255])*n;vm.r[2]=p
    hooks[0x11def8]=memset;c.watch={actor+0xb3e,actor+0xb40}
    c.run(0x2a8940,hooks=hooks)
    assert c.accesos==[(actor+0xb3e,2)],c.accesos
    return dict(funcion='EU2A8940',cola=[7,5],lecturas=[dict(direccion=hex(p),bytes=n) for p,n in c.accesos],
                resultado='solo el primer evento; segundo5 no se consume',elf_sha256=hashlib.sha256(elf).hexdigest())


if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--elf',type=Path,default=Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47'))
    ap.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF/ap'));a=ap.parse_args();assert a.tmp.is_dir()
    result=comprobar(a.elf.read_bytes());(a.tmp/'cola6_original.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('PASS cola6 original:',result)
