"""Selector completo1B1130 contrastado con133770 del ELF (sin juego ni SDK)."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import iw_capsulas_F4_original as O


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--oraculo',type=Path);ap.add_argument('--solo-callbacks',action='store_true');a=ap.parse_args()
    parent=Path('D:/tmp/solIWF');assert parent.is_dir();out=parent/('shader_F5_'+str(time.time_ns()));out.mkdir()
    elf=O.ELF.read_bytes();m=O.EE(elf);cases=[];code={}
    flags=sorted({mask<<22|low|deco for mask in range(16) for low in range(8) for deco in (0,8,0x20,0x80,0x88,0xa0,0xa8)})
    flags=sorted(set(flags+[0x36,0x80000836,0x800008b6,0x1b5,0x29bd]))
    if a.oraculo:
        anterior=json.loads(a.oraculo.read_text());assert anterior['elf_sha256']==hashlib.sha256(elf).hexdigest()
        cases,code=anterior['cases'],anterior['code'];flags=[]
    if a.solo_callbacks:cases=[];flags=[]
    for mode in (0,1):
        for flag in flags:
            m.store(0x49c66c,4,mode);m.store(0x49c644,4,0xffffffff);m.store(0x4ab670,4,0x1b1130)
            m.store(0x900000,4,0x901000);m.mem[0x901000:0x901010]=bytes(16);m.instructions=[]
            result=O.call(m,0x133770,(0x900000,7,flag));code.update({hex(pc):hex(w) for pc,w in m.instructions})
            draw=result!=0xffffffff;record=[m.load(0x901000+4*k,4) for k in range(4)]
            if draw:assert result==1 and m.load(0x900000,4)==0x901010
            cases.append(dict(flags=flag,mode=mode,draw=draw,key=m.load(0x49c644,4),callback=m.load(0x4ab674,4),unico=bool(m.load(0x49c6b0,4)),record=record))
    callbacks=[]
    if a.solo_callbacks:
        ids=[0,0x1b09d0,0x1b0a40,0x1b0a50,0x1b0ac0,0x1b0b30,0x1b0ba0,0x1b0c10,0x1b0c30,0x1b0c50,0x1b0c70,0x1b0c90,0x1b0ca0,0x1b0cc0]
        for expansion in (.025,.1):
            for cb in ids:
                globals={va:O.FB((((va>>2)&31)-16)/31) for lo,hi in ((0x4c4e20,0x4c4ea0),(0x4c4fc0,0x4c5000),(0x4c5120,0x4c5160)) for va in range(lo,hi,4)}
                globals.update({0x49be7c:O.FB(expansion),0x49c9b0:O.FB(.0625)})
                for va,value in globals.items():m.store(va,4,value)
                before=b''.join(struct.pack('<f',(i+1)/7) for i in range(80));m.mem[0x900000:0x900140]=before;m.instructions=[]
                if cb:O.call(m,cb,(0x900000,0,0))
                code.update({hex(pc):hex(w) for pc,w in m.instructions})
                callbacks.append(dict(callback=cb,expansion=expansion,globals=globals,before=before.hex(),after=m.mem[0x900000:0x900140].hex()))
    oracle=out/'original.json';oracle.write_text(json.dumps(dict(elf_sha256=hashlib.sha256(elf).hexdigest(),cases=cases,callbacks=callbacks,code=code),indent=2),encoding='utf-8')
    b=bytearray()
    def word(v):b.extend(struct.pack('<I',int(v)&0xffffffff))
    word(0x46355348);word(len(elf));b.extend(elf);word(len(cases))
    for c in cases:
        for k in ('flags','mode','draw','key','callback','unico'):word(c[k])
        for v in c['record']:word(v)
    word(len(callbacks))
    for c in callbacks:
        word(c['callback']);word(len(c['globals']))
        for addr,value in c['globals'].items():word(addr);word(value)
        before,after=bytes.fromhex(c['before']),bytes.fromhex(c['after']);word(len(before));b.extend(before);b.extend(after)
    fixture=out/'fixture.bin';fixture.write_bytes(b);exe=out/'shader.exe';commands=[]
    import capstone
    dis=capstone.Cs(capstone.CS_ARCH_MIPS,capstone.CS_MODE_MIPS64|capstone.CS_MODE_LITTLE_ENDIAN);dis.skipdata=True
    raw=elf[0x1b09d0-0x100000+0x80:0x1b1130-0x100000+0x80]
    (out/'callbacks.txt').write_text('\n'.join(f'{i.address:08x}: {i.mnemonic} {i.op_str}' for i in dis.disasm(raw,0x1b09d0)),encoding='utf-8')
    print('CALLBACKS',list(map(hex,sorted(set(c['callback'] for c in cases if c['callback'])))),flush=True)
    for name,cmd in [('compilar',['C:/Program Files/LLVM/bin/clang++.exe','-std=c++23','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-trap=all',ROOT/'tests/iw_capsulas_F5_shader.cpp','-o',exe]),('shader',[exe,fixture])]:
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out)))
        (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');print(r.stdout+r.stderr,flush=True);commands.append(dict(command=list(map(str,cmd)),returncode=r.returncode))
        if r.returncode:break
    (out/'resultado.json').write_text(json.dumps(dict(status='PASS' if not r.returncode else 'FAIL',commands=commands,oracle=str(oracle),
        sources={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in ('src/iw_capsulas_render.h','tests/iw_capsulas_F5_shader.cpp','tests/iw_capsulas_F5_shader.py')}),indent=2),encoding='utf-8')
    print('EVIDENCIA',out,flush=True);return r.returncode


if __name__=='__main__':sys.exit(main())
