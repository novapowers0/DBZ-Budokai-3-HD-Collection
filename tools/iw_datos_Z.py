"""Oráculo offline del disco del jugador: códigos/AMM y tabla de Piccolo."""
from pathlib import Path
import struct
import json
import sys
import csv

OUT=Path('D:/tmp/solIWZ')
AFS=Path('D:/DBZ3HD/ps2_games/Infinite World (USA)/USR/DATA_CMN.AFS')


def u(b,o): return struct.unpack_from('<I',b,o)[0]
def hijo(b,i):
    assert b[:4]==b'#AMB' and i<u(b,16)
    o,n=struct.unpack_from('<II',b,u(b,20)+16*i)
    return b[o:o+n]
def entrada(i,afs=AFS):
    with afs.open('rb') as f:
        f.seek(8+8*i);o,n=struct.unpack('<II',f.read(8));f.seek(o);return f.read(n)


def main():
    if '--assets' in sys.argv:
        ruta=Path(__file__).resolve().parent.parent/'studies 5/ESTUDIO_ASSETS/EVENTOS_IW_BSK.csv'
        with ruta.open(encoding='utf-8-sig',newline='') as f:
            filas=[r for r in csv.DictReader(f) if r['tipo'] in ('4','8')]
        res={t:[r for r in filas if r['tipo']==t] for t in ('4','8')}
        (OUT/'assets_ap.json').write_text(json.dumps(res,indent=1,ensure_ascii=False),encoding='utf-8')
        for tipo,rs in res.items():
            print('AP'+tipo,len(rs),'eventos;',len({(r['asset'],r['codigo_bsk']) for r in rs}),'codigos/banco')
            print('seleccionados',[(r['asset'].split(':')[-1],r['codigo_bsk'],r['valor_iw']) for r in rs if r['selector_literal_en_port']=='si'])
        return
    if '--hit' in sys.argv:
        b=hijo(entrada(5,AFS.with_name('DATA_USA.AFS')),0)
        raiz=u(b,u(b,20)+4*5);pend=[raiz];vistos=set()
        while pend:
            nodo=pend.pop()
            if not nodo or nodo in vistos:continue
            vistos.add(nodo);seg=u(b,nodo+16);segs=[]
            while seg:
                pista=u(b,seg+4);pos=u(b,pista+0x1C) if pista else 0
                segs.append((hex(seg),u(b,seg+28),struct.unpack_from('<4f',b,pos) if pos else None));seg=u(b,seg+24)
            print('HIT AMA5/0 objeto5 nodo',hex(nodo),'segmentos',segs)
            pend.extend((u(b,nodo+4),u(b,nodo+8)))
        return
    if '--fade' in sys.argv:
        elf=Path('D:/DBZ3HD/ps2_games/Infinite World (USA)/SLUS_218.42').read_bytes()
        ph=u(elf,28);sz,num=struct.unpack_from('<HH',elf,42)
        for a,n in [(0x480C54-0x7B0,6),(0x480AF0-0x7B0,6)]:
            for k in range(num):
                tipo,o,va,_,tam=struct.unpack_from('<5I',elf,ph+k*sz)
                if tipo==1 and va<=a and a+4*n<=va+tam:
                    b=elf[o+a-va:o+a-va+4*n]
                    print(hex(a),'u32',struct.unpack('<'+str(n)+'I',b),'f32',struct.unpack('<'+str(n)+'f',b))
                    break
        return
    if '--atlas' in sys.argv:
        from PIL import Image,ImageDraw
        sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'awo_tools'))
        import amt_ps2
        b=hijo(entrada(9,AFS.with_name('DATA_USA.AFS')),1)
        ts=amt_ps2.entries(b,keep_empty=True)
        im=Image.new('RGB',(750,((len(ts)+4)//5)*110),(50,50,50));d=ImageDraw.Draw(im)
        for i,t in enumerate(ts):
            if t is None:continue
            if i in (19,20,24):print(i,t['w'],t['h'])
            pic=Image.fromarray(amt_ps2.decode(b,t));pic.thumbnail((140,82))
            x,y=(i%5)*150,(i//5)*110
            im.paste(pic,(x,y+20),pic);d.text((x+2,y+2),str(i),fill='white')
        im.save(OUT/'atlas9.png');return
    if '--hud' in sys.argv:
        b=hijo(entrada(9,AFS.with_name('DATA_USA.AFS')),0)
        assert b[:4]==b'#AMA'
        for i in range(u(b,16)):
            nodo=u(b,u(b,20)+4*i);seg=u(b,nodo+0x10)
            if not seg:continue
            pista=u(b,seg+4)
            key=u(b,pista+0x14) if pista else 0
            pos=u(b,pista+0x1C) if pista else 0
            print(i,'rect',struct.unpack_from('<4f',b,seg+0x20),
                'escala',struct.unpack_from('<2f',b,key) if key else None,
                'pos',struct.unpack_from('<4f',b,pos) if pos else None)
        return
    global_=hijo(entrada(204),0)
    filas=[]
    for banco,codigos in [(915,[2,3,4]),(914,[2,3,4]),(918,[0x393]),(925,list(range(0x3F2,0x3FA)))]:
        amb=entrada(banco);bsk=hijo(amb,3 if banco==915 else 0)
        for codigo in codigos:
            blk=u(bsk,u(bsk,20)+4*codigo) if codigo<u(bsk,16) else 0
            if not blk:
                filas.append(dict(banco=banco,codigo=hex(codigo),ausente=True));continue
            indice,alm=struct.unpack_from('<HH',bsk,blk)
            amm=hijo(amb,0) if banco==915 else global_ if alm==0 else hijo(amb,1)
            assert amm[:4]==b'#AMM' and indice<u(amm,16)
            filas.append(dict(banco=banco,codigo=hex(codigo),indice=indice,almacen=alm,duracion=u(amm,u(amm,20)+16*indice+8)))
    # Son escrituras inmediatas de3279F0, no offsets del ELF por inferir.
    tabla=dict(umbral_quieto=0.25,umbral_variante=0.4,
        fuente='EU3279F0, lineas417006 y417017 del decomp existente')
    res=dict(codigos=filas,tabla=tabla)
    OUT.mkdir(exist_ok=True)
    (OUT/'datos_originales.json').write_text(json.dumps(res,indent=2),encoding='utf-8')
    print(json.dumps(res,indent=2))


if __name__=='__main__':main()
