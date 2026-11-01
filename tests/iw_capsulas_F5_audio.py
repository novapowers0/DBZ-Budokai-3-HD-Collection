"""Solo contrato nuevo CapsulePick/API real; reutiliza oraculo F3 sin repetir DSP."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import iw_capsulas_F4_original as O


def main():
    p=argparse.ArgumentParser();p.add_argument('bancos',type=Path);p.add_argument('--tmp',type=Path,default=Path('D:/tmp/solIWF'));a=p.parse_args()
    assert a.tmp.is_dir() and a.bancos.is_file();out=a.tmp/('audio_real_F5_'+str(time.time_ns()));out.mkdir()
    stubs,cuerpos=O.F2.motor_sources(out);imgui=ROOT/'rexglue-sdk-0.10/thirdparty/imgui';exe=out/'audio_real.exe';commands=[]
    cmd=['C:/Program Files/LLVM/bin/clang++.exe','-std=c++23','-O1','-D_CRT_SECURE_NO_WARNINGS','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN',
         '-fsanitize=undefined','-fsanitize-trap=all','-I'+str(stubs),'-I'+str(imgui),
         '-DDBZ3_CAPSULAS_F2_CUERPOS="'+cuerpos.as_posix()+'"',ROOT/'tests/iw_capsulas_F5_audio.cpp',
         imgui/'imgui.cpp',imgui/'imgui_draw.cpp',imgui/'imgui_tables.cpp',imgui/'imgui_widgets.cpp','-lwinmm','-o',exe]
    result=0
    for name,command in [('compilar',cmd),('audio',[exe,a.bancos])]:
        r=subprocess.run(list(map(str,command)),cwd=ROOT,capture_output=True,text=True,env=dict(os.environ,TEMP=str(out),TMP=str(out),TMPDIR=str(out)))
        (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');print(r.stdout+r.stderr,flush=True)
        commands.append(dict(command=list(map(str,command)),returncode=r.returncode));result=r.returncode
        if result:break
    sources=['src/pasarela_iw.cpp','src/iw_sonido.h','src/iw_adx.h','src/iw_midi.h','tests/iw_capsulas_F5_audio.cpp',str(Path(__file__).relative_to(ROOT))]
    (out/'resultado.json').write_text(json.dumps(dict(status='PASS' if not result else 'FAIL',commands=commands,
        bancos=str(a.bancos),bancos_sha256=hashlib.sha256(a.bancos.read_bytes()).hexdigest(),
        sources={s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources},
        limites=['API reales sin overrides; PCM sin altavoces','no repite decoder/oraculo DSP previo','cuerpos CapsulePick de produccion extraidos; frontera SDK/AFS del arnes']),indent=2),encoding='utf-8')
    print('EVIDENCIA',out,flush=True);return result


if __name__=='__main__':sys.exit(main())
