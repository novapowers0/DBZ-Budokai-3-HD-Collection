"""Preparacion de integracion AP: respalda fuentes y clasifica corpus ya medido."""
import collections
import json
from pathlib import Path
import struct
import argparse
import sys


def main():
    root=Path(__file__).resolve().parents[1];parent=Path('D:/tmp/solIWF/ap');assert parent.is_dir()
    out=parent/'integrado';out.mkdir(exist_ok=True)
    parser=argparse.ArgumentParser();parser.add_argument('--bundle',action='store_true');parser.add_argument('--referencia',action='store_true');args=parser.parse_args()
    if args.bundle or args.referencia:
        sys.path.insert(0,str(root/'tests'))
        from iw_ap_audio_prueba import entrada,hijo,U
        from campo_F_investigar import Elf
        e=Elf(Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)/SLES_553.47'))
        referencia=struct.unpack('<4f',e.bytes(0x44bb90,16))
        if args.referencia:
            print('referencia billboard',referencia);return
        b=entrada(root/'ps2_games/Infinite World (USA)/USR/DATA_CMN.AFS',813);rows=[]
        for i in range(U(b,16)):
            c=hijo(b,i);row=dict(hijo=i,magic=c[:4].decode('ascii',errors='replace'),bytes=len(c),
                flags=struct.unpack_from('<i',b,U(b,20)+16*i+8)[0])
            if i in (1,6):
                seen=set();todo=[U(c,12)];nodes=[]
                while todo:
                    p=todo.pop()
                    if p in seen or p<16 or p+0x50>len(c):continue
                    seen.add(p);nodes.append(dict(offset=p,tipo=hex(U(c,p+4)),flags=hex(U(c,p+8))))
                    todo.extend([U(c,p+0x24),U(c,p+0x28)])
                row['nodos']=nodes
            rows.append(row)
        result=dict(referencia_billboard=referencia,hijos=rows)
        (out/'bundle813.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2));return
    for name in ('anim_iw.cpp','anim_iw.h','iw_minijuegos.cpp','iw_minijuegos.h'):
        dest=out/(name+'.antes')
        if not dest.exists():dest.write_bytes((root/'src'/name).read_bytes())
    raw=(parent/'corpus.bin').read_bytes();pos=0;rows=[]
    u=lambda p:struct.unpack_from('<I',raw,p)[0]
    while pos<len(raw):
        cmn,hijo,cod=struct.unpack_from('<III',raw,pos);pos+=12;n=u(pos);pos+=4
        for _ in range(n):
            frame,t,v=struct.unpack_from('<fII',raw,pos);pos+=12
            if t in (0,4,5,8,9):rows.append(dict(cmn=cmn,hijo=hijo,codigo=cod,cuadro=frame,tipo=t,valor=v))
        for _ in range(10):n=u(pos);pos+=4+4*n
    resumen={t:dict(collections.Counter(r['valor'] for r in rows if r['tipo']==t)) for t in (0,4,5,8,9)}
    (out/'inventario.json').write_text(json.dumps(dict(resumen=resumen,eventos=rows),indent=2),encoding='utf-8')
    print(json.dumps(resumen,indent=2))


if __name__=='__main__':main()
