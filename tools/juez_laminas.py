"""Prepara una lámina y un prompt Haiku por pareja revisada, desde PNG locales."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import tomllib

from PIL import Image, ImageDraw

REFS = Path('E:/DBZ3HD_trabajo/refs_1a1')
PRUEBAS = Path('D:/DBZ3HD/_pruebas')
RUN = re.compile(r'\d{8}-\d{6}_.+')
ID = re.compile(r'[a-z0-9][a-z0-9_-]{0,63}', re.ASCII)
RESERVADO = re.compile(r'con|prn|aux|nul|com[1-9]|lpt[1-9]')
TIPOS = {'original': ('referencia original', 'ORIGINAL'),
         'maqueta': ('maqueta aprobada', 'MAQUETA APROBADA')}


def inventario(refs, pruebas):
    """Nombres exactos como pistas, sin seleccionar región, fase ni corrida."""
    capturas = {}
    for corrida in sorted(pruebas.iterdir()):
        if not RUN.fullmatch(corrida.name) or not corrida.is_dir():
            continue
        for png in sorted(corrida.glob('*.png')):
            if png.is_file() and not png.stem.lower().startswith('zz_'):
                capturas.setdefault(png.stem.casefold(), []).append(png.relative_to(pruebas).as_posix())
    filas = []
    for png in sorted(refs.glob('*/*/*/cuadro.png')):
        if not png.is_file():
            continue
        candidatas = capturas.get(png.parent.parent.name.casefold(), [])
        filas.append(dict(referencia=png.relative_to(refs).as_posix(), candidatas=candidatas,
                          estado=('sin_candidata' if not candidatas else
                                  'revisar' if len(candidatas) == 1 else 'ambiguo')))
    return dict(regla='ID de toma = nombre del PNG; una coincidencia no confirma equivalencia',
                pares_confirmados=0, referencias=filas)


def ruta_png(raiz, nombre):
    ruta = Path(nombre)
    if ruta.anchor or '..' in ruta.parts or ruta.suffix.lower() != '.png':
        raise ValueError(f'Se requiere un PNG relativo a {raiz}: {nombre}')
    ruta = (raiz / ruta).resolve(strict=True)
    if not ruta.is_relative_to(raiz) or not ruta.is_file():
        raise ValueError(f'PNG fuera de su raíz o no regular: {nombre}')
    return ruta


def cargar_pares(manifiesto, refs, pruebas):
    datos = tomllib.loads(manifiesto.read_text(encoding='utf-8-sig')).get('parejas')
    if not isinstance(datos, list) or not datos:
        raise ValueError('El manifiesto requiere al menos un bloque [[parejas]]')
    ids, pares = set(), []
    for i, fila in enumerate(datos, 1):
        campos = {'id', 'referencia', 'captura', 'tipo', 'foco'}
        if (not isinstance(fila, dict) or set(fila) != campos or
                any(not isinstance(v, str) or not v.strip() for v in fila.values())):
            raise ValueError(f'Pareja {i}: requiere {", ".join(sorted(campos))}')
        id_, tipo, foco = fila['id'], fila['tipo'], fila['foco']
        if not ID.fullmatch(id_) or RESERVADO.fullmatch(id_) or id_ in ids:
            raise ValueError(f'ID inválido o repetido: {id_}')
        if tipo not in TIPOS or foco.splitlines() != [foco] or len(foco) > 300:
            raise ValueError(f'{id_}: tipo original/maqueta y foco de una línea (máx. 300 caracteres)')
        ref = ruta_png(refs, fila['referencia'])
        cap = ruta_png(pruebas, fila['captura'])
        if ref.samefile(cap):
            raise ValueError(f'{id_}: se seleccionó el mismo archivo en ambos lados')
        ids.add(id_)
        pares.append((id_, tipo, foco, ref, cap))
    return pares


def leer_png(ruta):
    datos = ruta.read_bytes()
    with Image.open(io.BytesIO(datos), formats=['PNG']) as imagen:
        imagen.verify()
    with Image.open(io.BytesIO(datos), formats=['PNG']) as imagen:
        if getattr(imagen, 'n_frames', 1) != 1:
            raise ValueError(f'Se requiere un fotograma fijo: {ruta}')
        rgba = imagen.convert('RGBA')
        rgb = Image.new('RGB', imagen.size, '#151b24')
        rgb.paste(rgba, mask=rgba.getchannel('A'))
    return rgb, dict(ruta=str(ruta), sha256=hashlib.sha256(datos).hexdigest(), tamano=list(rgb.size))


def montar(ref, cap, rotulo):
    margen, y = 12, 36
    x = margen + max(160, ref.width) + margen
    lamina = Image.new('RGB', (x + max(160, cap.width) + margen,
                               y + max(ref.height, cap.height) + margen), '#151b24')
    dibujo = ImageDraw.Draw(lamina)
    for imagen, izquierda, titulo in ((ref, margen, rotulo), (cap, x, 'CAPTURA DEL PORT')):
        dibujo.text((izquierda, margen), titulo, fill='white')
        lamina.paste(imagen, (izquierda, y))
    return lamina


def generar(pares, salida):
    plantilla = Path(__file__).with_suffix('.md').read_text(encoding='utf-8')
    salida.mkdir(exist_ok=False)
    filas = []
    for id_, tipo, foco, ref, cap in pares:
        original, datos_ref = leer_png(ref)
        captura, datos_cap = leer_png(cap)
        texto, rotulo = TIPOS[tipo]
        lamina, prompt = f'{id_}.png', f'{id_}.md'
        montar(original, captura, rotulo).save(salida / lamina)
        (salida / prompt).write_text(plantilla.format(id=id_, tipo=texto, foco=foco), encoding='utf-8')
        filas.append(dict(id=id_, tipo=tipo, foco=foco, referencia=datos_ref, captura=datos_cap,
                          lamina=lamina, prompt=prompt))
    (salida / 'pares.json').write_text(json.dumps({'parejas': filas}, ensure_ascii=False, indent=2)
                                      + '\n', encoding='utf-8')
    print(f'{len(filas)} pareja(s) preparada(s) en {salida}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--refs', type=Path, default=REFS)
    parser.add_argument('--pruebas', type=Path, default=PRUEBAS)
    modo = parser.add_mutually_exclusive_group(required=True)
    modo.add_argument('--inventario', action='store_true', help='Pistas por nombre, JSON en consola')
    modo.add_argument('--pares', type=Path, help='Manifiesto TOML con las parejas seleccionadas')
    parser.add_argument('--salida', type=Path, help='Carpeta nueva; su padre debe existir')
    args = parser.parse_args()
    if bool(args.pares) != bool(args.salida):
        parser.error('--salida solo se usa con --pares y es obligatoria en ese modo')
    try:
        refs, pruebas = (raiz.resolve(strict=True) for raiz in (args.refs, args.pruebas))
        if not refs.is_dir() or not pruebas.is_dir():
            raise ValueError('--refs y --pruebas deben ser directorios')
        if args.inventario:
            print(json.dumps(inventario(refs, pruebas), ensure_ascii=True, indent=2))
        else:
            generar(cargar_pares(args.pares, refs, pruebas), args.salida)
    except (OSError, ValueError, SyntaxError, Image.DecompressionBombError) as exc:
        parser.exit(1, f'juez_laminas: {exc}\n')


if __name__ == '__main__':
    main()
