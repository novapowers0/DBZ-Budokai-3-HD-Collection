"""Hojas de evidencia ya capturada; no confunde fase con mismo fotograma."""
from pathlib import Path
from PIL import Image,ImageDraw
import json
import sys
import shutil
import hashlib

ROOT=Path(__file__).resolve().parent.parent
OUT=Path('D:/tmp/solIWZ')
MODEL=ROOT/'studies 5/ENTREGAS/solIW/modelos'

if '--preparar' in sys.argv:
    exe=ROOT/'out/build/win-amd64-dual/dbz3.exe'
    fijo=OUT/'entrega.exe'
    if fijo.exists():raise FileExistsError(fijo)
    shutil.copy2(exe,fijo)
    (OUT/'entrega_exe.json').write_text(json.dumps(dict(origen=str(exe),
        sha256=hashlib.file_digest(fijo.open('rb'),'sha256').hexdigest(),bytes=fijo.stat().st_size),indent=2),encoding='utf-8')
    print('Ejecutable final fijado:',fijo)


def hoja(nombre,original,port,limite):
    lienzo=Image.new('RGB',(960,430),(24,24,24));d=ImageDraw.Draw(lienzo)
    for i,im in enumerate((original,port)):
        im=im.convert('RGB');im.thumbnail((470,350))
        lienzo.paste(im,(i*480+(480-im.width)//2,30+(350-im.height)//2))
    d.text((10,8),'ORIGINAL IW',fill='white');d.text((490,8),'PORT B3 HD + animaciones IW',fill='white')
    d.text((10,390),limite,fill='yellow')
    lienzo.save(OUT/(nombre+'.jpg'),quality=85)


hoja('camara_7D2',Image.open(OUT/'original_7D2/ready60_05.png'),
    Image.open(OUT/'port_7D2_despues/ready_05.png'),
    'READY: vector ojo-objetivo medido (0,10,70) en ambos. Rotulo/capturas NO son mismo fotograma.')
hoja('impacto_7D8',Image.open(MODEL/'freezer_foto_usuario.jpg').crop((0,0,480,360)),
    Image.open(OUT/'estados_7D8_211741/impacto_05.png'),
    'HIT y fundido vistos; original con tinte rojo aun distinto. Referencia del usuario; no sincronia temporal.')
for clave,carpeta in [('7D3','estados_7D3_211200'),('7D7','estados_7D7_211536')]:
    rect=(850,0,1150,225) if clave=='7D3' else (450,0,750,225)
    hoja('hud_'+clave,Image.open(MODEL/f'0x{clave}.jpg').crop(rect),
        Image.open(OUT/carpeta/'hud_05.png'),
        'HUD: posiciones AMA originales; capturas de fases/contador distintos, no igualdad global.')
