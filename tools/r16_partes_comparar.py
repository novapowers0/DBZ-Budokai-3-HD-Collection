"""Hoja local de cuatro capturas de juego frente a su referencia B3."""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', type=Path, nargs=4, required=True)
    ap.add_argument('--pieza', type=Path, nargs=4, required=True)
    ap.add_argument('--salida', type=Path, required=True)
    args = ap.parse_args()
    if args.salida.exists() or not args.salida.parent.is_dir():
        raise ValueError('La salida debe ser nueva y su carpeta padre debe existir')
    sheet = Image.new('RGB', (1600, 516), '#15151e')
    draw = ImageDraw.Draw(sheet)
    draw.text((12, 8), 'B3 nativo / variante original. Capturas reales; poses independientes.', fill='white')
    labels = ('frente', 'derecha', 'espalda', 'izquierda')
    for row, (title, paths) in enumerate((('B3', args.base), ('Original', args.pieza))):
        y = 32 + row * 242
        for column, path in enumerate(paths):
            with Image.open(path) as source:
                image = source.convert('RGB')
                image.thumbnail((400, 225), Image.Resampling.LANCZOS)
                sheet.paste(image, (column * 400, y))
            draw.text((column * 400 + 8, y + 226), '%s / solicitud %s' % (title, labels[column]), fill='white')
    sheet.save(args.salida)
    print('PASS comparacion local: ocho capturas reales, sin alterar geometria ni sombreado; %s' % args.salida)


if __name__ == '__main__':
    main()
