"""AP solIWF: fuentes retail, aliases y recursos de boca; solo lectura del juego."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from iw_ap_audio_prueba import U,H,hijo,entrada


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF/ap'))
    ap.add_argument('--juego',type=Path,default=Path('ps2_games/Infinite World (USA)'))
    ap.add_argument('--decomp',type=Path,default=Path('E:/DBZ3HD_trabajo/ghidra/salida/iw_sles55347.c'))
    ap.add_argument('--asm',nargs=2,type=lambda v:int(v,0))
    ap.add_argument('--elf',type=Path,default=Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47'))
    a=ap.parse_args();assert a.tmp.parent.is_dir();a.tmp.mkdir(exist_ok=True)
    if a.asm:
        import capstone
        from campo_F_investigar import Elf
        e=Elf(a.elf);lo,hi=a.asm;d=capstone.Cs(capstone.CS_ARCH_MIPS,capstone.CS_MODE_MIPS64|capstone.CS_MODE_LITTLE_ENDIAN);d.skipdata=True
        lines=[f'{i.address:08x}: {i.mnemonic} {i.op_str}' for i in d.disasm(e.bytes(lo,hi-lo),lo)]
        (a.tmp/f'original_{lo:x}_{hi:x}.asm.txt').write_text('\n'.join(lines),encoding='utf-8');print('\n'.join(lines));return
    root=Path(__file__).resolve().parents[1]
    for n in ('iw_ap_eventos.h','iw_eventos.h'):
        dest=a.tmp/(n+'.antes')
        if not dest.exists():dest.write_bytes((root/'src'/n).read_bytes())
    texto=a.decomp.read_text(encoding='utf-8',errors='replace')
    matches=[];funcion='';alias=re.compile(r'\+ (0x(?:4dc|514|b1c|b20|b54|20c|210|244))\b')
    for n,line in enumerate(texto.splitlines(),1):
        if line.startswith('// ===='):funcion=line
        if alias.search(line):matches.append(dict(linea=n,funcion=funcion,texto=line))
    (a.tmp/'aliases_decomp.json').write_text(json.dumps(matches,indent=2),encoding='utf-8')
    banks={}
    for afs in ('ADX_USA.AFS','ADX_JPN.AFS'):
        for bank in (1471,1475):
            b=entrada(a.juego/'USR'/afs,bank)
            rows=[]
            if b[:4]==b'#AMB':
                for k in range(U(b,16)):
                    c=hijo(b,k);rows.append(dict(hijo=k,tamano=len(c),magic=c[:4].decode('ascii',errors='replace'),cabecera=c[:64].hex()))
                    if c[:4]==b'#AMK':
                        (a.tmp/f'{afs}_{bank}_{k}.amk').write_bytes(c)
            else:
                rows.append(dict(tamano=len(b),magic=b[:4].decode('ascii',errors='replace'),cabecera=b[:64].hex()))
                if b[:4]==b'#AMK':(a.tmp/f'{afs}_{bank}.amk').write_bytes(b)
            banks[f'{afs}/{bank}']=rows
    result=dict(fuentes=dict(decomp_sha256=hashlib.sha256(a.decomp.read_bytes()).hexdigest()),bancos=banks,alias_coincidencias=len(matches))
    (a.tmp/'inventario.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
