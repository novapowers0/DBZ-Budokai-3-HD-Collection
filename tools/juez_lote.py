"""Agrupa hasta seis láminas por hoja al 50 % y prepara un único encargo de revisión."""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw

COLORES = ('verde', 'ambar', 'rojo')
POR_HOJA = 6
MARGEN = 12
ROTULO = 24


def leer_laminas(entradas, color):
    filas = []
    for ruta in entradas:
        ext = ruta.suffix.lower()
        if ext == '.json':
            datos = json.loads(ruta.read_text(encoding='utf-8-sig'))
            if not isinstance(datos, dict) or not isinstance(datos.get('parejas'), list):
                raise ValueError(f'{ruta}: se esperaba un índice lamina_lote.json con parejas')
            for fila in datos['parejas']:
                if (not isinstance(fila, dict)
                        or any(not isinstance(fila.get(k), str) or not fila[k]
                               for k in ('id', 'lamina', 'color'))
                        or fila['color'] not in COLORES):
                    raise ValueError(f'{ruta}: pareja sin id, lámina o color válido')
                if color is None or fila['color'] == color:
                    filas.append((fila['id'], ruta.parent / fila['lamina']))
        elif ext == '.png' and color is None:
            filas.append((ruta.stem, ruta))
        else:
            raise ValueError(f'{ruta}: usa un índice JSON o un PNG (PNG sin filtro de color)')
    laminas = [(id_, ruta.resolve(strict=True)) for id_, ruta in filas]
    if not laminas:
        raise ValueError('No hay láminas que revisar con esta selección')
    if len({ruta for _, ruta in laminas}) != len(laminas):
        raise ValueError('Hay láminas repetidas en la selección')
    for _, ruta in laminas:
        with Image.open(ruta, formats=['PNG']) as im:
            im.verify()
    return laminas


def montar(grupo, anterior):
    miniaturas = []
    for _, ruta in grupo:
        with Image.open(ruta, formats=['PNG']) as im:
            mitad = tuple(max(1, medida // 2) for medida in im.size)
            miniaturas.append(im.convert('RGB').resize(mitad, Image.Resampling.LANCZOS))
    filas = [miniaturas[i:i + 2] for i in range(0, len(miniaturas), 2)]
    anchos = [max(im.width for im in miniaturas[i::2]) for i in range(min(2, len(grupo)))]
    altos = [max(im.height for im in fila) + ROTULO for fila in filas]
    hoja = Image.new('RGB', (sum(anchos) + (len(anchos) + 1) * MARGEN,
                             sum(altos) + (len(altos) + 1) * MARGEN), '#151b24')
    dibujo = ImageDraw.Draw(hoja)
    y, registros = MARGEN, []
    for fila, alto in zip(filas, altos):
        x = MARGEN
        for im, ancho in zip(fila, anchos):
            i = len(registros)
            numero = anterior + i + 1
            id_, ruta = grupo[i]
            dibujo.text((x, y + 3), f'Lamina {numero:03d}', fill='white')
            hoja.paste(im, (x, y + ROTULO))
            registros.append(dict(numero=numero, id=id_, origen=str(ruta),
                                  caja=[x, y + ROTULO, x + im.width, y + ROTULO + im.height]))
            x += ancho + MARGEN
        y += alto + MARGEN
    return hoja, registros


def encargo(n_laminas, n_hojas):
    return f"""# Encargo único: revisión visual del port

Revisa las {n_laminas} láminas numeradas de las {n_hojas} hojas PNG, en el orden de
sus archivos `hoja_*.png`. Dentro de cada lámina, la referencia (original o
maqueta) va a la izquierda y el port a su derecha. Si hay un tercer panel,
es el mapa de diferencias: úsalo para localizar cambios, no como prueba por sí
solo. Ignora el color y las cifras de la comparación automática al decidir.

Da un veredicto por lámina:
- **SE PARECE**: conserva el contenido y la presentación relevantes; las
  diferencias visibles son menores.
- **CASI**: se reconoce la misma escena o pantalla, pero hay diferencias
  relevantes que se pueden describir y corregir.
- **NO**: falta un elemento esencial o las diferencias impiden reconocer
  la misma presentación. Si las capturas no permiten cotejarla, indica por qué.

Compara composición, posición y tamaño de elementos, textos, iconos y color.
Separa las diferencias del port de las debidas al encuadre, idioma, momento de
captura o resolución. Si algo no es legible a esta escala, indícalo sin adivinar.

Devuelve una única respuesta con este formato para cada número impreso:

### Lámina 001 — SE PARECE / CASI / NO (elige uno)
1. Zona: qué se ve en la referencia, qué se ve en el port y su impacto.
2. Siguiente diferencia, si la hay.

Enumera solo diferencias observables, de mayor a menor impacto. Si no ves
ninguna relevante, escribe «Sin diferencias relevantes».
"""


def generar(entradas, salida, color=None):
    laminas = leer_laminas(entradas, color)
    salida.mkdir(parents=True, exist_ok=False)
    hojas = []
    for i in range(0, len(laminas), POR_HOJA):
        imagen, registros = montar(laminas[i:i + POR_HOJA], i)
        nombre = f'hoja_{len(hojas) + 1:03d}.png'
        imagen.save(salida / nombre)
        hojas.append(dict(hoja=nombre, laminas=registros))
    (salida / 'juez_lote.json').write_text(json.dumps({'escala': 0.5, 'hojas': hojas},
                                                      ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    texto = encargo(len(laminas), len(hojas))
    (salida / 'encargo.md').write_text(texto, encoding='utf-8')
    print(f'{len(laminas)} laminas en {len(hojas)} hojas; encargo de {len(texto.split())} palabras.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('entradas', type=Path, nargs='+', help='Índices lamina_lote.json o láminas PNG en orden')
    p.add_argument('--salida', type=Path, required=True, help='Carpeta nueva para las hojas y encargo.md')
    p.add_argument('--color', choices=COLORES, help='Filtra por color; solo con índices JSON')
    args = p.parse_args()
    try:
        generar(args.entradas, args.salida, args.color)
    except (OSError, ValueError, SyntaxError) as exc:
        p.exit(1, f'juez_lote: {exc}\n')


if __name__ == '__main__':
    main()
