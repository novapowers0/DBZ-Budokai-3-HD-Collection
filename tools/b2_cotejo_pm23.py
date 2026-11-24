#!/usr/bin/env python3
"""Cotejo visual B2 offline. Lee el TSV de T16 o un TSV revisado por pm-b2."""

import argparse
import csv
import hashlib
from io import BytesIO
import json
from pathlib import Path
import re
import sys

import numpy as np
from PIL import Image


BASE = Path('E:/DBZ3HD_trabajo')
REF = BASE / 'refs_1a1/b2'
PORT = Path('D:/DBZ3HD/_pruebas')
PARES = BASE / 'fabrica/T16/pares.tsv'
REQUERIDAS = {'id', 'elemento', 'ref', 'port', 'observacion'}
OPCIONALES = {'estado', 'port_sha256', 'recorte_ref', 'recorte_port'}
SHA256 = re.compile(r'[0-9a-fA-F]{64}')
ERRORES_DATOS = (OSError, ValueError, SyntaxError, Image.DecompressionBombError)
ERRORES_CLI = ERRORES_DATOS + (csv.Error,)
UMBRALES = {'mae_max': 3.0, 'pct_max': 2.0, 'delta_rgb': 20, 'aspecto_rel': 0.01}


def ruta_bajo(raiz, relativa):
    p = Path(relativa)
    if p.anchor or '..' in p.parts:
        raise ValueError(f'ruta no relativa a la raíz: {relativa}')
    p = (raiz / p).resolve()
    if not p.is_relative_to(raiz):
        raise ValueError(f'ruta fuera de la raíz: {relativa}')
    return p


def leer_pares(path, ref, port):
    filas, ids = [], set()
    with path.open(encoding='utf-8-sig', newline='') as f:
        lector = csv.DictReader(f, delimiter='\t', strict=True)
        campos = lector.fieldnames or []
        if (len(campos) != len(set(campos)) or not REQUERIDAS <= set(campos)
                or set(campos) - REQUERIDAS - OPCIONALES):
            raise ValueError('columnas TSV inválidas; se requieren id, elemento, ref, port, observacion')
        for fila in lector:
            if None in fila or None in fila.values():
                raise ValueError(f'TSV línea {lector.line_num}: número incorrecto de columnas')
            fila = {k: v.strip() for k, v in fila.items()}
            ident = fila['id']
            if not ident or ident in ids or ident.startswith('REF:') or not fila['elemento']:
                raise ValueError(f'TSV línea {lector.line_num}: ID repetido, reservado o vacío, o escena vacía')
            ids.add(ident)
            if (fila.get('estado') or 'diagnostico') not in ('diagnostico', 'confirmado'):
                raise ValueError(f'{ident}: estado debe ser diagnostico o confirmado')
            if fila['ref']:
                ruta_bajo(ref, fila['ref'])
                if len(Path(fila['ref']).parts) != 2 or not Path(fila['ref']).name.isascii() or not Path(fila['ref']).name.isdecimal():
                    raise ValueError(f'{ident}: ref debe ser ESCENA/CUADRO')
            if fila['port']:
                p = ruta_bajo(port, fila['port'])
                if p.suffix.lower() != '.png':
                    raise ValueError(f'{ident}: port debe indicar un PNG')
            filas.append(fila)
    if not filas:
        raise ValueError('TSV sin escenas')
    return filas


def lados_originales(raiz):
    return sorted({p.parent.relative_to(raiz).as_posix() for nombre in ('cuadro.png', 'referencia.json')
                   for p in raiz.glob(f'*/*/{nombre}') if p.is_file()})


def leer_png(path, recorte, esperado=''):
    datos = path.read_bytes()
    digest = hashlib.sha256(datos).hexdigest()
    if esperado and (not isinstance(esperado, str) or not SHA256.fullmatch(esperado)
                     or esperado.lower() != digest):
        raise ValueError('SHA-256 PNG no coincide o es inválido')
    with Image.open(BytesIO(datos), formats=['PNG']) as png:
        png.verify()
    with Image.open(BytesIO(datos), formats=['PNG']) as png:
        if getattr(png, 'n_frames', 1) != 1:
            raise ValueError('PNG animado')
        if png.convert('RGBA').getchannel('A').getextrema() != (255, 255):
            raise ValueError('PNG con transparencia')
        img = png.convert('RGB')
    ancho, alto = img.size
    caja = (0, 0, ancho, alto)
    if recorte:
        try:
            caja = tuple(int(x) for x in recorte.split(','))
            x0, y0, x1, y1 = caja
        except ValueError:
            raise ValueError('recorte inválido; usar x0,y0,x1,y1') from None
        if not (0 <= x0 < x1 <= ancho and 0 <= y0 < y1 <= alto):
            raise ValueError('recorte fuera del PNG')
        img = img.crop(caja)
    return img, {'ruta': str(path), 'sha256': digest, 'tamano': [ancho, alto], 'recorte': caja}


def leer_ref(raiz, fila):
    rel = Path(fila['ref'])
    meta = ruta_bajo(raiz, rel / 'referencia.json')
    m = json.loads(meta.read_text(encoding='utf-8-sig'))
    if not isinstance(m, dict):
        raise ValueError('referencia.json no contiene un objeto')
    t = m.get('toma')
    es_pal = (isinstance(t, dict) and t.get('juego') == 'b2' and t.get('region') == 'eu'
              and t.get('serial') == 'SLES-51839' and t.get('crc') == 'FE09A7CC'
              and t.get('id') == rel.parent.name and type(t.get('cuadro')) is int
              and t['cuadro'] == int(rel.name) and bool(m.get('png_sha256'))
              and not m.get('origen'))
    origen = 'PCSX2 PAL' if es_pal else 'vídeo' if m.get('origen') == 'youtube' else 'no acreditado'
    esperado = m.get('png_sha256', '')
    if 'png_sha256' in m and not (isinstance(esperado, str) and SHA256.fullmatch(esperado)):
        raise ValueError('referencia.json: png_sha256 inválido')
    img, dato = leer_png(ruta_bajo(raiz, rel / 'cuadro.png'), fila.get('recorte_ref'), esperado)
    dato.update(origen=origen, referencia_json=str(meta), hash_validado=bool(esperado),
                cuadro=t['cuadro'] if es_pal else None,
                segundo=m.get('segundo') if origen == 'vídeo' else None)
    return img, dato


def medir(a, b):
    ar, br = a.width / a.height, b.width / b.height
    if max(ar, br) / min(ar, br) - 1 > UMBRALES['aspecto_rel']:
        raise ValueError(f'aspectos distintos ({a.width}×{a.height} / {b.width}×{b.height}); falta recorte explícito')
    tam = min((a.size, b.size), key=lambda t: (t[0] * t[1], t))
    xa, xb = (np.asarray(img.resize(tam, Image.Resampling.LANCZOS), dtype=np.int16)
              for img in (a, b))
    dif = np.abs(xa - xb)
    return {'tamano': tam, 'mae_rgb': float(dif.mean()),
            'pct_pixeles_distintos': 100 * float((dif.max(axis=2) > UMBRALES['delta_rgb']).mean())}


def evaluar(fila, ref, port):
    r = {**fila, 'veredicto': 'FAIL', 'motivos': [], 'metricas': None,
         'evidencia_ref': None, 'evidencia_port': None}
    motivos = r['motivos']
    a = b = None
    confirmado = fila.get('estado') == 'confirmado'
    if not confirmado:
        motivos.append('sin sincronía acreditada')
    elif not fila['observacion']:
        motivos.append('falta ancla de sincronía en observacion')
    if fila['ref']:
        try:
            a, r['evidencia_ref'] = leer_ref(ref, fila)
            if r['evidencia_ref']['origen'] != 'PCSX2 PAL':
                motivos.append('referencia no acreditada como PCSX2 PAL')
        except ERRORES_DATOS as exc:
            motivos.append(f'original: {exc}')
    else:
        motivos.append('falta original')
    if fila['port']:
        esperado = fila.get('port_sha256', '')
        if confirmado and not esperado:
            motivos.append('falta port_sha256')
        try:
            b, r['evidencia_port'] = leer_png(ruta_bajo(port, fila['port']), fila.get('recorte_port'), esperado)
        except ERRORES_DATOS as exc:
            motivos.append(f'port: {exc}')
    else:
        motivos.append('falta port')
    if a is not None and b is not None:
        try:
            if Path(r['evidencia_ref']['ruta']).samefile(r['evidencia_port']['ruta']):
                raise ValueError('el original y el port apuntan al mismo archivo')
            r['metricas'] = m = medir(a, b)
            if m['mae_rgb'] > UMBRALES['mae_max'] or m['pct_pixeles_distintos'] > UMBRALES['pct_max']:
                motivos.append('supera el umbral RGB')
        except ERRORES_DATOS as exc:
            motivos.append(str(exc))
    if not motivos:
        r['veredicto'] = 'PASS'
    return r


def tabla(filas):
    def celda(s):
        return ' '.join(s.split()).replace('|', r'\|')

    ok = sum(f['veredicto'] == 'PASS' for f in filas)
    medidas = sum(f['metricas'] is not None for f in filas)
    lineas = ['# B2 / PM-23 — cotejo visual offline', '',
              f'{len(filas)} filas: {ok} PASS, {len(filas) - ok} FAIL; {medidas} con métricas RGB.', '',
              '| ID | Escena | Original | Veredicto | MAE RGB | % píxeles Δ>20 | Motivo y observación |',
              '|---|---|---|---|---:|---:|---|']
    for f in filas:
        m = f['metricas']
        origen = (f['evidencia_ref'] or {}).get('origen', '—')
        numeros = f"{m['mae_rgb']:.3f} | {m['pct_pixeles_distintos']:.3f}" if m else '— | —'
        motivo = celda('; '.join(f['motivos'])) or 'umbral cumplido'
        if f['observacion']:
            motivo += ' — ' + celda(f['observacion'])
        lineas.append(f"| {celda(f['id'])} | {celda(f['elemento'])} | {origen} | {f['veredicto']} | {numeros} | {motivo} |")
    return '\n'.join(lineas) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, epilog='Salida: 0 todas PASS; 1 alguna FAIL; 2 error global.')
    parser.add_argument('--pares', type=Path, default=PARES, help='TSV de parejas (por defecto, el de T16)')
    parser.add_argument('--ref', type=Path, default=REF, help='raíz de las referencias B2')
    parser.add_argument('--port', type=Path, default=PORT, help='raíz de los PNG del port')
    parser.add_argument('--inventario', action='store_true', help='añade las referencias sin pareja declarada')
    parser.add_argument('--tabla', type=Path, help='guardar Markdown en un archivo nuevo')
    parser.add_argument('--json', type=Path, help='guardar evidencias y motivos en un archivo nuevo')
    args = parser.parse_args(argv)
    try:
        ref, port = args.ref.resolve(strict=True), args.port.resolve(strict=True)
        if not (ref.is_dir() and port.is_dir()) or ref == port:
            raise ValueError('se requieren dos directorios raíz distintos')
        filas = leer_pares(args.pares, ref, port)
        inventario = lados_originales(ref) if args.inventario else None
        if inventario is not None:
            usadas = {ruta_bajo(ref, f['ref']) for f in filas if f['ref']}
            filas.extend(dict(id=f'REF:{p}', elemento=Path(p).parent.name, ref=p, port='',
                              observacion='Sin pareja declarada para esta referencia.')
                         for p in inventario if ruta_bajo(ref, p) not in usadas)
        salidas = [p.resolve() for p in (args.tabla, args.json) if p is not None]
        if len(set(salidas)) != len(salidas) or any(p.exists() or not p.parent.is_dir() for p in salidas):
            raise ValueError('cada salida debe ser un archivo nuevo y tener un directorio padre existente')
        resultados = [evaluar(f, ref, port) for f in filas]
        texto = tabla(resultados)
        informe = dict(esquema='b2-cotejo-pm23-v1', umbrales=UMBRALES, raiz_ref=str(ref), raiz_port=str(port),
                       manifiesto=str(args.pares.resolve()), inventario=inventario, filas=resultados)
        for path, contenido in ((args.tabla, texto), (args.json, json.dumps(informe, ensure_ascii=False, indent=2) + '\n')):
            if path:
                with path.open('x', encoding='utf-8', newline='\n') as f:
                    f.write(contenido)
        print(texto, end='')
        return int(any(f['veredicto'] == 'FAIL' for f in resultados))
    except ERRORES_CLI as exc:
        print(f'Error: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
