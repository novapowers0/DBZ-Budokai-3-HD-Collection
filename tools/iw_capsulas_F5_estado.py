"""Analiza estados ORIGINALES guardados por el arnes, sin ejecutar otra VM."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile
import time


def analizar(path,micros):
    meta=json.loads(path.read_text());result=dict(origen=str(path),capturas=[],cierre=meta.get('cierre'),pid_final=meta.get('pid_final'))
    anterior=None;bases={}
    for shot in meta['capturas']:
        with zipfile.ZipFile(shot['estado']) as z:
            ram=z.read('eeMemory.bin');vu=z.read('vu1MicroMem.bin');data=z.read('vu1Memory.bin')
        def u(a):return struct.unpack_from('<I',ram,a)[0]
        def f(a):return struct.unpack_from('<f',ram,a)[0]
        effect=u(0x5b5af0);nodes=[];p=u(effect+0x40);base=bases.get(effect);vistos=set()
        while p:
            assert p not in vistos;vistos.add(p);n=u(p+8);raw=u(n+0x14)
            if base is None:
                base=raw-16
                print('CABECERA runtime',hex(effect),hex(p),hex(n),hex(raw),ram[base:base+32].hex(),flush=True)
                # El cargador nativo marca los recursos procesados con !AME;
                # no es el magic #AME de la entrada AFS sin resolver.
                assert u(base) in (0x454d4123,0x454d4121) and u(base+4)==2 and u(base+8)==101,'cabecera runtime AME no identificada'
                bases[effect]=base
            kind=u(raw+4);particles=[]
            if kind>>16==2:
                q=u(n+0x3c)
                while q!=n+0x44:
                    color=0x50 if kind==0x20003 else 0x30;uv=0x60 if kind==0x20003 else 0x40;size=0x70 if kind==0x20003 else 0x50
                    particles.append(dict(age=f(q+8),flags=u(q+12),pos=[f(q+0x10+4*k) for k in range(3)],vel=[f(q+0x20+4*k) for k in range(3)],
                        color=[f(q+color+4*k) for k in range(4)],uv=[f(q+uv+4*k) for k in range(4)],size=f(q+size),scale=f(q+size+4),
                        angle=f(q+0x58) if kind==0x20001 else 0,quat=[f(q+0x30+4*k) for k in range(4)] if kind==0x20003 else [0,0,0,1]))
                    q=u(q)
                    assert len(particles)<2000
            nodes.append(dict(offset=raw-base,kind=kind,particles=particles,emisor_edad=f(u(n+0x20)+8) if kind>>16==1 else None));p=u(p)
        matches=[]
        if micros:
            for code in micros.glob('micro_*.bin'):
                b=code.read_bytes()
                if b and vu[:len(b)]==b:matches.append(dict(registro=int(code.stem.split('_')[1]),bytes=len(b),sha256=hashlib.sha256(b).hexdigest()))
        row=dict(cuadro=shot['cuadro'],effect=effect,base_ame=base,nodos=u(effect+0x48),rng=u(0x49be08),nodes=nodes,
            matrices_globales=[list(struct.unpack_from('<4f',ram,0x485f10+16*k)) for k in range(28)],
            ultimo_shader=hex(u(0x49c644)),vu_micro_sha256=hashlib.sha256(vu).hexdigest(),vu_matches=matches,
            vu_datos_sha256=hashlib.sha256(data).hexdigest(),paso=f(0x5509e8),dt=f(0x5509ec),efecto_cabecera=ram[effect:effect+0x50].hex(),
            paletas={hex(a):ram[a:a+n].hex() for a,n in ((0x4c4e20,0x80),(0x4c4fc0,0x40),(0x4c5120,64))})
        from iw_capsulas_F_datos import entrada
        work=u(0x49cb94);row['guiones']={}
        for campo in (0xd8,0xdc):
            ptr=u(work+campo);coincide=[]
            for id in (993,994):
                src=entrada(id)
                if ram[ptr+4:ptr+len(src)]==src[4:]:coincide.append(id)
            row['guiones'][hex(campo)]=dict(puntero=ptr,magic=ram[ptr:ptr+4].hex(),igual_AFS_salvo_magic=coincide)
        if anterior:
            seed=anterior['rng'];salto=None
            for bits in range(4097):
                if seed==row['rng']:salto=bits;break
                seed=((seed<<1)|(((seed>>31)^(seed>>4))&1))&0xffffffff
            row['rng_bits_desde_anterior']=salto
        anterior=row;result['capturas'].append(row)
        print('ESTADO ORIGINAL',row['cuadro'],'nodos',row['nodos'],'particulas',sum(len(n['particles']) for n in nodes),'shader',row['ultimo_shader'],'micro_matches',matches,'RNGbits',row.get('rng_bits_desde_anterior'),flush=True)
        print('EDADES',sorted(set(p['age'] for n in nodes for p in n['particles'])),flush=True)
        print('GUIONES',row['guiones'],flush=True)
    out=path.with_name('estado_detallado_'+str(time.time_ns())+'.json');out.write_text(json.dumps(result,indent=2),encoding='utf-8');print('EVIDENCIA',out,flush=True)


def main():
    p=argparse.ArgumentParser();p.add_argument('medida',type=Path);p.add_argument('--micros',type=Path);a=p.parse_args();analizar(a.medida,a.micros)


if __name__=='__main__':main()
