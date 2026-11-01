"""Comprueba AME con los estados RAM reales y la cadencia PAL capturada."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from iw_capsulas_F_datos import entrada


def main():
    p=argparse.ArgumentParser();p.add_argument('referencia',type=Path);a=p.parse_args();ref=json.loads(a.referencia.read_text());snap=ref['capturas'];start,end=snap[1],snap[2]
    assert all(not n['particles'] for n in start['nodes']);ages={p['age'] for n in end['nodes'] for p in n['particles']};assert len(ages)==1
    # Los hijos directos se actualizan tambien en el cuadro de nacimiento:
    # la primera edad visible es paso, no cero (orden277150/277830).
    ticks=round(ages.pop()/end['paso']);campos=end['cuadro']-start['cuadro'];primero=campos-ticks+1;assert 0<primero<=campos
    parent=Path('D:/tmp/solIWF');assert parent.is_dir();out=parent/('runtime_F5_'+str(time.time_ns()));out.mkdir();b=bytearray()
    def u(v):b.extend(struct.pack('<I',int(v)&0xffffffff))
    def fl(v):b.extend(struct.pack('<f',v))
    raw=entrada(972);u(0x46355254);u(len(raw));b.extend(raw);u(start['rng']);u(campos);u(primero);fl(end['paso']);fl(end['dt']);u(end['nodos']);u(end['rng']);u(len(end['nodes']))
    for node in end['nodes']:
        u(node['offset']);u(node['kind']);u(len(node['particles']))
        for p in node['particles']:
            fl(p['age']);u(p['flags'])
            for k in ('pos','vel','color','uv'):
                for v in p[k]:fl(v)
            for k in ('size','scale','angle'):fl(p[k])
            for v in p['quat']:fl(v)
    fixture=out/'fixture.bin';fixture.write_bytes(b);exe=out/'runtime.exe';commands=[]
    for name,cmd in [('compilar',['C:/Program Files/LLVM/bin/clang++.exe','-std=c++23','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-trap=all',ROOT/'tests/iw_capsulas_F5_runtime.cpp','-o',exe]),('runtime',[exe,fixture])]:
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out)))
        (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');print(r.stdout+r.stderr,flush=True);commands.append(dict(command=list(map(str,cmd)),returncode=r.returncode))
        if r.returncode:break
    (out/'resultado.json').write_text(json.dumps(dict(status='PASS' if not r.returncode else 'FAIL',commands=commands,referencia=str(a.referencia),
        referencia_sha256=hashlib.sha256(a.referencia.read_bytes()).hexdigest(),campos=campos,ticks=ticks,primer_D5=primero,
        files={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in ('src/iw_capsulas_render.h','tests/iw_capsulas_F5_runtime.cpp','tests/iw_capsulas_F5_runtime.py')}),indent=2),encoding='utf-8');print('EVIDENCIA',out,flush=True);return r.returncode


if __name__=='__main__':sys.exit(main())
