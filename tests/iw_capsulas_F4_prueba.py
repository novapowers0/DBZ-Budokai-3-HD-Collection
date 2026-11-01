"""Solo pruebas nuevas F4: AME original y ejecucion VU->GIF->GS."""
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
import iw_capsulas_F4_original as O


def main():
    p=argparse.ArgumentParser();p.add_argument('oraculo',type=Path);p.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF'));p.add_argument('--solo-vu',action='store_true');p.add_argument('--solo-ame',action='store_true');p.add_argument('--vu-indice',type=int,default=5);p.add_argument('--puente',type=Path);a=p.parse_args()
    assert a.tmp.is_dir();out=a.tmp/('f4_prueba_'+str(time.time_ns()));out.mkdir()
    b=bytearray()
    def word(v):b.extend(struct.pack('<I',v&0xffffffff))
    def blob(v):word(len(v));b.extend(v)
    elf=O.ELF.read_bytes();ame=O.entrada(972);e=json.loads(a.oraculo.read_text())
    assert hashlib.sha256(elf).hexdigest()==e['elf_sha256'],'ELF distinto al oraculo'
    assert hashlib.sha256(ame).hexdigest()==e['ame_sha256'],'AME distinta al oraculo'
    word(0x46345249);blob(elf);blob(ame);b.extend(struct.pack('<ff',e.get('paso',1.),e.get('dt',1/60)));word(len(e['states']))
    for s in e['states']:
        for k in ('frame','nodos','rng'):word(s[k])
        word(len(s['nodes']))
        for n in s['nodes']:
            word(n['offset']);word(n['kind']);word(len(n['particles']))
            for p in n['particles']:
                if not a.solo_vu and not a.puente:assert all(k in p for k in ('size','scale','angle','quat')),'usar el oraculo ampliado F4'
                word(p['age']);word(p['flags'])
                for key in ('pos','vel','color','uv'):
                    for v in p[key]:word(v)
                for key in ('size','scale','angle'):word(p.get(key,0))
                for v in p.get('quat',[0,0,0,0x3f800000]):word(v)
    fixture=out/'fixture.bin';fixture.write_bytes(b);commands=[]
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True,env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out)))
        (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');print(r.stdout+r.stderr)
        commands.append(dict(command=list(map(str,cmd)),returncode=r.returncode));return r.returncode
    compiler='C:/Program Files/LLVM/bin/clang++.exe'
    if a.puente:
        stubs,cuerpos=O.F2.motor_sources(out);imgui=ROOT/'rexglue-sdk-0.10/thirdparty/imgui';exe=out/'f4_motor.exe'
        result=run([compiler,'-std=c++23','-O1','-D_CRT_SECURE_NO_WARNINGS','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN',
            '-fsanitize=undefined','-fsanitize-trap=all','-I'+str(stubs),'-I'+str(imgui),
            '-DDBZ3_CAPSULAS_F2_CUERPOS="'+cuerpos.as_posix()+'"',ROOT/'tests/iw_capsulas_F4_motor.cpp',
            imgui/'imgui.cpp',imgui/'imgui_draw.cpp',imgui/'imgui_tables.cpp',imgui/'imgui_widgets.cpp','-lwinmm','-o',exe],'compilar_puente')
        if not result:result=run([exe,a.puente],'puente')
    else:
        exe=out/'f4.exe';result=run([compiler,'-std=c++23','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
            '-fsanitize=undefined','-fsanitize-trap=all',ROOT/'tests/iw_capsulas_F4_prueba.cpp','-o',exe],'compilar')
        if not result:result=run([exe,fixture]+(['--solo-vu',str(a.vu_indice)] if a.solo_vu else ['--solo-ame'] if a.solo_ame else []),'prueba')
    fuentes=[ROOT/'src/iw_capsulas_render.h',Path(__file__),ROOT/'tests/iw_capsulas_F4_prueba.cpp']
    if a.puente:fuentes.extend([ROOT/'src/pasarela_iw.cpp',ROOT/'src/iw_azar.h',ROOT/'tests/iw_capsulas_F4_motor.cpp',ROOT/'tests/iw_capsulas_F2_original.py'])
    (out/'resultado.json').write_text(json.dumps(dict(status='PASS' if not result else 'FAIL',commands=commands,oracle=str(a.oraculo),
        oraculo_sha256=hashlib.sha256(a.oraculo.read_bytes()).hexdigest(),
        alcance='puente AME' if a.puente else 'VU funcional '+str(a.vu_indice) if a.solo_vu else 'AME' if a.solo_ame else 'AME+VU funcional5',
        fuentes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in fuentes}),indent=2),encoding='utf-8')
    print('EVIDENCIA',out);return result


if __name__=='__main__':sys.exit(main())
