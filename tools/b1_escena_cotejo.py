"""Lámina Original | Traducido y diferencia RGB media por corte (0 = idénticos)."""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]


def read_plan(path):
    spec = json.loads(path.read_bytes())
    cuts = spec['cortes']
    if (spec['schema'], spec['spx'], spec['resolucion']) != ('b1-tr3-v1', 1910, [640, 448]):
        raise ValueError('Manifiesto TR3 desconocido')
    if len(cuts) != 14 or any(c['corte'] != i or c['png'] != f'1910_corte_{i:02}.png'
                             for i, c in enumerate(cuts)):
        raise ValueError('Se necesitan los 14 cortes ordenados y sin duplicar')
    return spec


def read_png(path, size):
    with Image.open(path) as im:
        if im.format != 'PNG' or im.size != size or getattr(im, 'is_animated', False):
            raise ValueError(f'{path}: debe ser PNG fijo de {size[0]}x{size[1]}')
        rgba = im.convert('RGBA')
        if rgba.getchannel('A').getextrema() != (255, 255):
            raise ValueError(f'{path}: imagen no opaca')
        return rgba.convert('RGB')


def compare(manifest, original, translated, output, allow_missing=False):
    spec = read_plan(manifest)
    size = tuple(spec['resolucion'])
    output = output.resolve()
    if output.is_relative_to(ROOT):
        raise ValueError('La lámina con recursos debe quedar fuera del repositorio')
    w, h = size[0]//2, size[1]//2
    pad, title, header = 12, 25, 39
    sheet = Image.new('RGB', (2*w+3*pad, header+14*(h+title+pad)), '#151a22')
    draw = ImageDraw.Draw(sheet)
    draw.text((pad, 11), 'B1 PAL / 1910  |  ORIGINAL', fill='#e7eaf0')
    draw.text((2*pad+w, 11), 'TRADUCIDO / BLENDER', fill='#e7eaf0')
    results = []
    for cut in spec['cortes']:
        path = original / cut['png']
        a = read_png(path, size) if path.exists() or not allow_missing else None
        b = read_png(translated / cut['png'], size)
        score, ratio = None, None
        if a is not None:
            diff = ImageChops.difference(a, b)
            score = 100 * sum((i % 256) * n for i, n in enumerate(diff.histogram())) / (255*3*size[0]*size[1])
            r, g, bl = diff.split()
            ratio = 100 * (1 - ImageChops.lighter(ImageChops.lighter(r, g), bl).histogram()[0] / (size[0]*size[1]))
        i = cut['corte']
        y = header+i*(h+title+pad)
        label = 'Sin referencia' if score is None else f'Error RGB: {score:.3f}/100'
        draw.text((pad, y+4), f'Corte {i:02}  |  {cut["segundos"]:.2f} s  |  {label}', fill='#e7eaf0')
        for j, img in enumerate((a, b)):
            x, top = pad+j*(w+pad), y+title
            if img is None:
                draw.rectangle((x, top, x+w-1, top+h-1), fill='#212833')
                draw.text((x+15, top+h//2), 'Captura PCSX2 pendiente', fill='#abb6c5')
            else:
                sheet.paste(img.resize((w,h), Image.Resampling.LANCZOS), (x, top))
        results.append(dict(corte=i, png=cut['png'], cuadro_pal=cut['cuadro_pal'], error_rgb=score,
                            pixeles_distintos_pct=ratio))
    valid = [r['error_rgb'] for r in results if r['error_rgb'] is not None]
    report = dict(escena=1910, metrica='100 * MAE(RGB8) / 255', comparados=len(valid), esperado=14,
                  error_rgb=sum(valid)/14 if len(valid) == 14 else None, cortes=results)
    output.mkdir()
    sheet.save(output / 'lamina.png')
    with (output / 'puntuacion.json').open('x', encoding='utf-8') as f:
        f.write(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for arg in ('manifiesto', 'original', 'traducido', 'salida'):
        p.add_argument(arg, type=Path)
    p.add_argument('--admitir-faltantes', action='store_true', help='Solo vista previa; sin puntuación global')
    args = p.parse_args()
    r = compare(args.manifiesto, args.original, args.traducido, args.salida, args.admitir_faltantes)
    print(f"{r['comparados']}/14 cortes cotejados; error RGB: {r['error_rgb']}")
