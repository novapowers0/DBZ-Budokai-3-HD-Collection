"""Genera PNG por pareja y un índice JSON. Rutas relativas al manifiesto TOML."""
import argparse
import json
import re
import tomllib
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw
from skimage.transform import warp

from comparar import alinear, metricas

COLORES = {'verde': '#267344', 'ambar': '#805500', 'rojo': '#99353b'}


def generar(manifiesto, destino):
    datos = tomllib.loads(manifiesto.read_text(encoding='utf-8'))
    verde, ambar = (datos.get('umbrales', {}).get(k, d) for k, d in
                    [('verde_ssim', .90), ('ambar_ssim', .65)])
    if not 0 <= ambar <= verde <= 1:
        raise ValueError('Umbrales SSIM inválidos')
    pares = datos['parejas']
    ids = [p['id'] for p in pares]
    if not ids or len(set(ids)) != len(ids) or any(not re.fullmatch(r'[a-z0-9][a-z0-9_-]*', i) for i in ids):
        raise ValueError('IDs vacíos, duplicados o inválidos')
    destino.mkdir(parents=True, exist_ok=False)
    filas = []
    for par in pares:
        if ('original' in par) == ('maqueta' in par):
            raise ValueError(f'{par["id"]}: indica exactamente original o maqueta')
        tipo = 'original' if 'original' in par else 'maqueta'
        a, b = ((manifiesto.parent / par[k]).resolve() for k in (tipo, 'port'))
        with Image.open(a) as ia, Image.open(b) as ib:
            org, port = ia.convert('RGB'), ib.convert('RGB')
        # Evita errores numéricos de registro con dos imágenes pixel a pixel idénticas.
        if org.size == port.size and not ImageChops.difference(org, port).getbbox():
            m = dict(ssim=1.0, phash_hamming=0, alineado='identico', puntos_comunes=None, zona_comun_pct=100.0)
            ali = org
        else:
            m = metricas(a, b)
            t, _ = alinear(org, port) if m['alineado'] == 'alineado' else (None, 0)
            ali = (Image.fromarray((warp(np.asarray(org), t, output_shape=(port.height, port.width)) * 255)
                                   .clip(0, 255).astype('uint8')) if t is not None else org.resize(port.size))
        color = 'verde' if m['ssim'] >= verde else 'ambar' if m['ssim'] >= ambar else 'rojo'
        w = min(640, port.width)
        h = max(1, round(port.height * w / port.width))
        hoja = Image.new('RGB', (w * 3, h + 44), '#141b24')
        d = ImageDraw.Draw(hoja)
        d.rectangle((0, 0, w * 3, 20), fill=COLORES[color])
        d.text((6, 3), f'{par["id"]} | {color.upper()} | SSIM {m["ssim"]:.4f} | {m["alineado"]}', fill='white')
        for i, (titulo, im) in enumerate(((tipo.title(), ali), ('Port', port), ('Diferencia RGB', ImageChops.difference(ali, port)))):
            d.text((i*w + 6, 25), titulo, fill='white')
            hoja.paste(im.resize((w, h), Image.Resampling.LANCZOS), (i*w, 44))
        nombre = par['id'] + '.png'
        hoja.save(destino / nombre)
        filas.append(dict(id=par['id'], referencia=str(a), tipo=tipo, port=str(b),
                          lamina=nombre, color=color, metricas=m, esperado=par.get('esperado')))
        print(par['id'], color, m['ssim'])
    (destino / 'lamina_lote.json').write_text(json.dumps(dict(umbrales=[verde, ambar], parejas=filas),
                                                         indent=2, ensure_ascii=False), encoding='utf-8')
    return int(any(f['esperado'] not in (None, f['color']) for f in filas))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('manifiesto', type=Path)
    p.add_argument('--salida', required=True, type=Path)
    a = p.parse_args()
    raise SystemExit(generar(a.manifiesto, a.salida))
