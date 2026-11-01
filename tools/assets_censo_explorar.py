"""Inspeccion offline acotada: decomp existente y bytes MIPS del ELF del jugador."""
import argparse
import os
os.environ.setdefault('DBZ3_AGENTE', 'solAST2')
from pathlib import Path
from assets_censo import Game, decomp

EU = Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)')
DEC = Path('E:/DBZ3HD_trabajo/ghidra/salida')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('addresses', nargs='*')
    ap.add_argument('--game', default='iw')
    ap.add_argument('--asm', action='store_true')
    ap.add_argument('--full', action='store_true')
    ap.add_argument('--scan', action='store_true')
    ap.add_argument('--table', action='store_true')
    args = ap.parse_args()
    funcs = decomp(DEC / {'iw':'iw_sles55347.c','b2':'b2.c','b1':'b1.c'}[args.game])
    if args.table:
        g=Game('iw',EU)
        import struct
        b=g.elf;sh=struct.unpack_from('<I',b,32)[0];ents,n=struct.unpack_from('<HH',b,46)
        for i in range(n):
            typ=struct.unpack_from('<I',b,sh+i*ents+4)[0]
            if typ==0x70000006:
                off=struct.unpack_from('<I',b,sh+i*ents+16)[0]
                gp=struct.unpack_from('<I',b,off+20)[0]
                print('GP',hex(gp))
        for arg in args.addresses:
            a,n=arg.split(':')
            print(a, g.va(int(a,16),int(n)))
        return
    if args.scan:
        from assets_censo_mips import recover
        from assets_censo import OUT, write_csv
        g=Game('iw',EU)
        rows=recover(g,funcs,{0x14c960,0x310560})
        write_csv(OUT/'ORIGINAL_IW_MIPS_CARGAS.csv',rows,list(rows[0]))
        for r in rows:
            if 0x280000<=int(r['funcion'].split()[0],16)<0x384000:
                print(r['funcion'],r['pc'],r['a0'],r['a1'])
        return
    if args.asm:
        import capstone
        g = Game('iw', EU)
        cs = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS64 | capstone.CS_MODE_LITTLE_ENDIAN)
    for adr in args.addresses:
        a = int(adr,16)
        f = funcs[format(a,'08x')]
        print('\n', adr, f['name'], f['line'])
        if args.asm:
            end = next((int(k,16) for k in funcs if int(k,16)>a), a+256)
            raw = b''.join(v.to_bytes(4,'little') for v in g.va(a,(end-a)//4))
            # PS2 MMI/VU instructions are not all supported. Decode each word independently.
            lines=[]
            for off in range(0,len(raw),4):
                ins=next(cs.disasm(raw[off:off+4],a+off),None)
                lines.append(f'{a+off:08x}: {ins.mnemonic} {ins.op_str}' if ins else f'{a+off:08x}: MMI/VU {raw[off:off+4].hex()}')
            for i,line in enumerate(lines):
                if args.full:
                    print(line)
                    continue
                if ': jal ' in line or ': jalr ' in line:
                    print('\n'.join(lines[max(0,i-6):i+2]))
        else:
            import re
            for n,s in f['lines']:
                if re.search(r'\w+\(|DAT_|PTR_|0x[0-9a-f]{5,}',s) and not s.lstrip().startswith(('/*','//')):
                    print(n,s.strip())

if __name__=='__main__': main()
