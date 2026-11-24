#!/usr/bin/env python3
"""PM-23 vía corta: empareja la matriz F8 de cierres con 0–6 Bolas y mide con E8."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
from stat import S_ISREG
import sys

import b2_cotejo_pm23 as e8


F8 = e8.BASE / 'fabrica/F8/TABLA_PM23.md'
REVISION = {'cuadro', 'port', 'mascara_pal', 'mascara_port', 'pal_sha256',
            'port_sha256', 'build', 'observacion', 'recorte_ref', 'recorte_port'}
CUADRO = re.compile(r'[0-9]{10}')


def matriz(texto):
    casos = []
    for linea in texto.splitlines():
        if not re.match(r'^\s*\|\s*[0-9]+\s*\|', linea):
            continue
        n, hexa, decimal, estrellas, _, epilogo, resumen = (
            celda.strip().strip('`') for celda in linea.strip().strip('|').split('|'))
        n, mascara = int(n), int(hexa, 16)
        if not (0 <= n <= 6 and mascara < 0x7f and mascara == int(decimal)
                and mascara.bit_count() == n
                and estrellas == (', '.join(str(i + 1) for i in range(7) if mascara & (1 << i)) or '—')
                and (epilogo, resumen) == (f'cierre11_bolas_{n:02}', f'cierre10d1_bolas_{n:02}')):
            raise ValueError(f'fila F8 incoherente: {linea}')
        for ident, escena in ((epilogo, '11 · Epílogo'), (resumen, '10 dato 1 · Show Results')):
            casos.append(dict(id=ident, bolas=n, mascara=f'0x{mascara:02x}', elemento=escena))
    if [c['bolas'] for c in casos] != [n for n in range(7) for _ in range(2)]:
        raise ValueError('F8 debe contener las siete filas 0–6, una vez y en orden')
    return casos


def sin_duplicados(pares):
    objeto = {}
    for clave, valor in pares:
        if clave in objeto:
            raise ValueError(f'clave duplicada en revisión: {clave}')
        objeto[clave] = valor
    return objeto


def leer_revision(path, casos):
    if path is None:
        return {}
    datos = json.loads(path.read_text(encoding='utf-8-sig'), object_pairs_hook=sin_duplicados)
    if not isinstance(datos, dict) or datos.keys() - {c['id'] for c in casos}:
        raise ValueError('la revisión debe ser un objeto con IDs de la matriz F8')
    for ident, dato in datos.items():
        if (not isinstance(dato, dict) or dato.keys() - REVISION
                or any(not isinstance(v, str) for v in dato.values())):
            raise ValueError(f'{ident}: campos de revisión desconocidos o valores no textuales')
    return datos


def preparar(caso, rev, ref, port):
    ident = caso['id']
    datos = dict(ref='', port='', motivos=[], candidatos_ref=[], port_esperado=f'{ident}.png')
    motivos = datos['motivos']
    try:
        carpeta = e8.ruta_bajo(ref, ident)
        candidatos = sorted(p.name for p in carpeta.glob('*') if CUADRO.fullmatch(p.name)
                            and p.is_dir() and any((p / f).is_file() for f in ('cuadro.png', 'referencia.json')))
        datos['candidatos_ref'] = candidatos
        cuadro = rev.get('cuadro') or (candidatos[0] if len(candidatos) == 1 else '')
        if cuadro:
            if not CUADRO.fullmatch(cuadro):
                raise ValueError('cuadro PAL debe tener diez dígitos')
            e8.ruta_bajo(ref, f'{ident}/{cuadro}')
            datos['ref'] = f'{ident}/{cuadro}'
        elif candidatos:
            motivos.append('varios cuadros PAL; indicar cuadro en la revisión')
    except e8.ERRORES_DATOS as exc:
        motivos.append(f'original: {exc}')
    try:
        relativa = rev.get('port') or datos['port_esperado']
        datos['port_esperado'] = relativa
        p = e8.ruta_bajo(port, relativa)
        if p.suffix.lower() != '.png':
            raise ValueError('se requiere una ruta PNG')
        try:
            st = p.stat()
        except FileNotFoundError:
            return datos
        if S_ISREG(st.st_mode):
            datos['port'] = Path(relativa).as_posix()
            datos['identidad_port'] = (st.st_dev, st.st_ino) if st.st_ino else p
    except e8.ERRORES_DATOS as exc:
        motivos.append(f'port: {exc}')
    return datos


def cotejar(casos, revision, ref, port):
    preparados = [preparar(c, revision.get(c['id'], {}), ref, port) for c in casos]
    usos_port = Counter(d['identidad_port'] for d in preparados if d['port'])
    resultados = []
    for caso, d in zip(casos, preparados):
        rev = revision.get(caso['id'], {})
        motivos = d['motivos']
        if d['port'] and usos_port[d['identidad_port']] > 1:
            motivos.append('el mismo archivo del port se ha asignado a varias filas')
        if d['ref'] and d['port']:
            faltan = [k for k in ('cuadro', 'mascara_pal', 'mascara_port', 'pal_sha256',
                                   'port_sha256', 'build', 'observacion') if not rev.get(k, '').strip()]
            if faltan:
                motivos.append('revisión incompleta: ' + ', '.join(faltan))
        for k in ('mascara_pal', 'mascara_port'):
            if rev.get(k) and rev[k].lower() != caso['mascara']:
                motivos.append(f'{k} no coincide con F8 ({caso["mascara"]})')
        fila = {**caso, 'ref': d['ref'], 'port': d['port'],
                'estado': 'confirmado' if d['ref'] and d['port'] and not motivos else 'diagnostico',
                'observacion': rev.get('observacion', ''),
                **{k: rev.get(k, '') for k in ('port_sha256', 'recorte_ref', 'recorte_port')}}
        r = e8.evaluar(fila, ref, port)
        if rev.get('pal_sha256') and r['evidencia_ref']:
            if rev['pal_sha256'].lower() != r['evidencia_ref']['sha256']:
                motivos.append('SHA-256 PAL distinto del cuadro revisado')
        r['motivos'] = motivos + r['motivos']
        if r['motivos']:
            r['veredicto'] = 'FAIL'
        r.update(candidatos_ref=d['candidatos_ref'], port_esperado=d['port_esperado'], revision=rev)
        resultados.append(r)
    return resultados


def tabla(filas):
    def celda(s):
        return ' '.join(s.split()).replace('|', r'\|')

    ok = sum(f['veredicto'] == 'PASS' for f in filas)
    medidos = sum(f['metricas'] is not None for f in filas)
    lineas = ['# G2 · PM-23 vía corta — matriz F8', '',
              f'{ok}/{len(filas)} PASS, {len(filas) - ok} FAIL; {medidos} pares con métricas E8.', '',
              'FAIL sin métricas no acredita una diferencia visual. La tabla no sustituye el juez de nueve fases.', '',
              '| ID F8 | Máscara | Cuadro PAL | PNG del port | Resultado | MAE RGB | % Δ>20 | Motivo |',
              '|---|---|---|---|---|---:|---:|---|']
    for f in filas:
        m = f['metricas']
        numeros = f"{m['mae_rgb']:.3f} | {m['pct_pixeles_distintos']:.3f}" if m else '— | —'
        ref = f['ref'] or ', '.join(f['candidatos_ref']) or '—'
        port = f['port'] or f"— (esperado: {f['port_esperado']})"
        motivo = celda('; '.join(f['motivos'])) or 'umbral cumplido'
        lineas.append(f"| {f['id']} | {f['mascara']} | {celda(ref)} | {celda(port)} | {f['veredicto']} | {numeros} | {motivo} |")
    return '\n'.join(lineas) + '\n'


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, epilog='0: 14 PASS; 1: algún FAIL; 2: error global.')
    p.add_argument('--f8', type=Path, default=F8, help='tabla Markdown de F8')
    p.add_argument('--ref', type=Path, default=e8.REF, help='raíz PAL de B2')
    p.add_argument('--port', type=Path, default=e8.PORT, help='raíz de PNG del port')
    p.add_argument('--revision', type=Path, help='JSON por ID F8 con las parejas revisadas')
    p.add_argument('--tabla', type=Path, help='guardar Markdown en un archivo nuevo')
    p.add_argument('--json', type=Path, help='guardar evidencias en un archivo nuevo')
    args = p.parse_args(argv)
    try:
        ref, port = args.ref.resolve(strict=True), args.port.resolve(strict=True)
        if ref == port or not (ref.is_dir() and port.is_dir()):
            raise ValueError('se requieren dos directorios raíz distintos')
        salidas = [p.resolve() for p in (args.tabla, args.json) if p is not None]
        if len(salidas) != len(set(salidas)) or any(p.exists() or not p.parent.is_dir() for p in salidas):
            raise ValueError('cada salida requiere un archivo nuevo en un directorio existente')
        f8 = args.f8.read_bytes()
        casos = matriz(f8.decode('utf-8-sig'))
        filas = cotejar(casos, leer_revision(args.revision, casos), ref, port)
        texto = tabla(filas)
        informe = dict(esquema='b2-pm23-f8-v1', f8=str(args.f8.resolve()),
                       f8_sha256=hashlib.sha256(f8).hexdigest(), raiz_ref=str(ref), raiz_port=str(port),
                       umbrales=e8.UMBRALES, filas=filas)
        for path, contenido in ((args.tabla, texto), (args.json, json.dumps(informe, ensure_ascii=False, indent=2) + '\n')):
            if path:
                with path.open('x', encoding='utf-8', newline='\n') as f:
                    f.write(contenido)
        print(texto, end='')
        return int(any(f['veredicto'] == 'FAIL' for f in filas))
    except e8.ERRORES_CLI as exc:
        print(f'Error: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
