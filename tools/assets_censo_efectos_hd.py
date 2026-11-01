"""Localiza hojas DDS reales de efectos HD para Tactics; arte solo en D:/tmp.

Usa xbdecompress ya instalado y XXH3 existente, igual que native_ui.cpp.
No instala dependencias, no genera imagenes ni modifica bancos.
"""
from pathlib import Path
import argparse
import hashlib
import os
import re
import struct
import subprocess
import time
from assets_censo import ROOT, OUT, write_csv

PIECES=[('fx_impacto',504,0x40AF1EBA62CD4A73),('fx_anillo',504,0xC5C95EF10DC7667D),
        ('fx_nucleo',504,0xBC4107469032B90F),('fx_rayo',504,0xA85E604E0EA9A4C9),
        ('fx_halo',553,0x9E585F231E5CD258),('fx_brillo',553,0x0F42A9F23C423FC0),
        ('fx_chispas',531,0xC8611BC86718688B)]

def main():
    import xxhash
    from texture_b3 import XBDECOMPRESS, extract_afs_entry
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tmp',type=Path,default=Path('D:/tmp/solAST2'))
    ap.add_argument('--reuse',type=Path,help='Reutilizar solo los tres AMB ya descomprimidos por esta herramienta')
    a=ap.parse_args();a.tmp.mkdir(parents=True,exist_ok=True)
    work=a.reuse if a.reuse else a.tmp/('efectos_'+str(time.time_ns()))
    if not a.reuse:work.mkdir()
    env={**os.environ,'TEMP':str(work),'TMP':str(work),'DBZ3_AGENTE':'solAST2'}
    rows=[];banks=[]
    for entry in (504,531,553):
        src=work/f'{entry}.lzx';dst=work/f'{entry}.amb'
        compressed=extract_afs_entry(str(ROOT/'us/data_cmn.afs'),entry)
        if a.reuse:
            assert src.read_bytes()==compressed,'La copia HD ha cambiado; no reutilizar el AMB'
        else:
            src.write_bytes(compressed)
            r=subprocess.run([XBDECOMPRESS,str(src),str(dst)],capture_output=True,text=True,env=env)
            if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        b=dst.read_bytes()
        # Enumerar cada AZT, no asumir que un paquete solo contiene uno.
        leaves=[]
        for m in re.finditer(b'\x23AZT',b):
            z=m.start()
            if z+24>len(b):continue
            n,tab=struct.unpack_from('>II',b,z+16)
            if n>4096 or z+tab+n*4>len(b):continue
            for tex in range(n):
                off=struct.unpack_from('>I',b,z+tab+4*tex)[0]
                if z+off+24>len(b):continue
                dat=struct.unpack_from('>I',b,z+off+20)[0]
                leaves.append((z,tex,z+dat))
        for z,tex,off in leaves:
            if b[off:off+4]!=b'DDS ':continue
            h,w=struct.unpack_from('<II',b,off+12)
            four=b[off+84:off+88]
            if four not in (b'DXT1',b'DXT3',b'DXT5'):continue
            size=max(1,(w+3)//4)*max(1,(h+3)//4)*(8 if four==b'DXT1' else 16)
            pixels=b[off+128:off+128+size]
            assert len(pixels)==size
            digest=xxhash.xxh3_64_intdigest(pixels)
            for name,e,expected in PIECES:
                if e!=entry or digest!=expected:continue
                rows.append(dict(nombre=name,archivo='us/data_cmn.afs',entrada=entry,
                    hoja=f'AZT@{z:#x}/textura{tex}',dds_offset=hex(off),ancho=w,alto=h,
                    formato=four.decode(),xxh3=f'{digest:016X}',sha256_bitmap=hashlib.sha256(pixels).hexdigest(),
                    consumidor='src/ingame/native_ui.cpp kPieces -> src/tactics/entrada.cpp PintarFx',
                    uso='declarada, NO solicitada por PintarFx' if name in ('fx_rayo','fx_brillo') else 'solicitada por PintarFx',
                    limite='Hoja fisica verificada; textura aislada no equivale al guion/modelo/AME completo de una tecnica'))
        # Leer magics solo en limites de contenedor validos. Un barrido por
        # texturas comprimidas produce falsos tags ASCII con mucha facilidad.
        tags=set()
        def walk(off,size,depth=0):
            magic=b[off:off+4]
            if magic.startswith(b'#'):tags.add(magic.decode('ascii',errors='replace'))
            if magic!=b'#AMB' or size<32 or depth>=8:return
            n,tab=struct.unpack_from('>II',b,off+16)
            if n>4096 or tab+16*n>size:return
            for k in range(n):
                child,length=struct.unpack_from('>II',b,off+tab+16*k)
                if child+length<=size:walk(off+child,length,depth+1)
        walk(0,len(b))
        banks.append(dict(entrada=entry,bytes=len(b),sha256=hashlib.sha256(b).hexdigest(),
                          hojas_azt=len(leaves),magics=';'.join(sorted(tags)),
                          limite='Magics en limites AMB; no identifica tecnica completa sin seguir su consumidor'))
    assert {r['nombre'] for r in rows}=={n for n,_,_ in PIECES},'No se localizaron las siete piezas'
    write_csv(OUT/'EFECTOS_B3HD_TACTICS.csv',rows,list(rows[0]))
    write_csv(OUT/'BANCOS_EFECTOS_B3HD.csv',banks,list(banks[0]))
    print('EFECTOS HD OK:',len(rows),'hojas DDS con XXH3 del consumidor; temporales:',work)
    for r in rows:print(r['nombre'],r['entrada'],r['hoja'],f'{r["ancho"]}x{r["alto"]}',r['uso'])
    for r in banks:print('Banco',r['entrada'],r['magics'])

if __name__=='__main__':main()
