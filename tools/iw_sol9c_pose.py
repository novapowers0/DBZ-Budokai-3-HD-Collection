"""Oraculo de puntos de cuerpo: RAM EE de estados originales conservados."""
import argparse
import json
from pathlib import Path
import struct
import zipfile


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--original',type=Path,default=Path('D:/tmp/solIW9c/original_campo_044234'))
    ap.add_argument('--port',type=Path)
    a=ap.parse_args()
    meta=json.loads((a.original/'medida.json').read_text(encoding='utf-8'))
    fotos={f['cuadro']:f['nombre'] for f in meta['fotos']}
    datos={}
    for path in Path('D:/_PARA_BORRAR_CLAUDE/solIW/captura_estados').glob('*.p2s'):
        with zipfile.ZipFile(path) as z:
            with z.open('eeMemory.bin') as ram:
                ram.seek(0x4a4d40);contador=int.from_bytes(ram.read(4),'little')
                if contador not in fotos:continue
            ram=z.read('eeMemory.bin')
        u=lambda ad:struct.unpack_from('<I',ram,ad)[0]
        f=lambda ad,n=1:struct.unpack_from('<'+str(n)+'f',ram,ad)
        x=u(0x49ca70)+0x800
        cam=u(0x49cbbc)
        datos[fotos[contador]]=dict(estado=str(path),contador=contador,actor=hex(x),
            codigo=hex(u(x+0xb60)),cuadro=f(x+0xc68)[0],
            modo_audio=u(u(0x49ca70)),voz_base=u(x+0xb8c),idioma_audio=ram[0x5c54b3],
            transform=f(x,16),puntos=[f(x+0x640+16*i,4) for i in range(16)],
            camara=hex(cam),cam_raw=ram[cam:cam+0x240].hex() if cam else '')
    out=Path('D:/tmp/solIW9c/pose_original.json')
    out.write_text(json.dumps(datos,indent=2),encoding='utf-8')
    for n,d in datos.items():print(n,'codigo',d['codigo'],'cuadro',d['cuadro'],'puntos',len(d['puntos']),
                                 'modo_audio',d['modo_audio'],'voz_base',d['voz_base'],'idioma_audio',d['idioma_audio'])
    assert 'fracaso10' in datos
    if a.port:
        import numpy as np
        p=json.loads(a.port.read_text(encoding='utf-8'));o=datos['fracaso10']
        pp=np.array(p['puntos'])[:,:3];po=np.array(o['puntos'])[:,:3]
        m=np.array(p['transform']).reshape(4,4)
        local=(pp-m[3,:3])@m[:3,:3].T
        diff=np.linalg.norm(local-po,axis=1)
        comparacion=dict(original=datos['fracaso10']['estado'],port=str(a.port),
            cuadro_original=o['cuadro'],cuadro_port=p['cuadro'],error_por_punto=diff.tolist(),
            media=float(diff.mean()),maximo=float(diff.max()),
            metodo='puntos de cuerpo a coordenadas del modelo mediante la inversa de SU matriz de mundo; sin ajustar la pose',
            limites=['RAM original11.999999 frente port12; Screenshot GS original puede ir un campo atras',
                     'no usar capturas rebobinadas054151/054549: sus caches de claves no se reiniciaron'])
        Path('D:/tmp/solIW9c/pose_oraculo9c.json').write_text(json.dumps(comparacion,indent=2),encoding='utf-8')
        print('PUNTOS LOCALES error',diff.tolist(),'media',diff.mean(),'max',diff.max())
        for i in range(16):print(i,'EE',po[i].round(3).tolist(),'port',local[i].round(3).tolist())


if __name__=='__main__':main()
