"""Validar y resumir una evidencia local; no vuelve a ejecutar el original.

Codigo 0: evidencia coherente y entradas actuales coincidentes (puede contener
diferencias del port). Codigo 2: incoherencia, entrada modificada o error del banco.
"""
import argparse
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path

import fuentes


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(path):
    report = json.loads(path.read_text(encoding='utf-8'))
    require(report['entradas_sin_cambios_al_final'] is True, 'Corrida con entradas inestables')
    for name, expected in report['herramientas_sha256'].items():
        require(sha(Path(__file__).parent / name) == expected, f'Herramienta modificada: {name}')
    require(report['fuentes'] == fuentes.validate(), 'Fuentes distintas de las de la corrida')
    for item in [report['identidad']] + report['estados_adicionales']:
        require(sha(item['estado']) == item['estado_sha256'], 'Savestate modificado: ' + item['estado'])
        require(sha(item['elf']) == item['elf_sha256'], 'ELF modificado: ' + item['elf'])
    for name, expected in report['motor']['archivos_sha256'].items():
        require(sha(name) == expected, 'Motor modificado: ' + name)
    counts = defaultdict(Counter)
    differences = Counter()
    seen = set()
    for row in report['casos']:
        require(row['id'] not in seen, 'Caso duplicado: ' + row['id'])
        seen.add(row['id'])
        require(row['estado'] in ('IGUAL', 'DIFERENCIA'), 'Caso incompleto: ' + row['id'])
        equal = row['original'] == row['modelo_port']
        require(row['iguales'] == equal, 'Comparacion inconsistente: ' + row['id'])
        require(row['estado'] == ('IGUAL' if equal else 'DIFERENCIA'), 'Estado inconsistente: ' + row['id'])
        trace = row['traza']
        for field in ('instrucciones_desconocidas', 'llamadas_sin_puente', 'io'):
            require(not trace[field], f'Traza incompleta ({field}): ' + row['id'])
        require(trace['palabras_codigo_verificadas'] > 0, 'Sin codigo original: ' + row['id'])
        counts[row['id'].split('/')[0]][row['estado']] += 1
        if not equal:
            differences[row['tipo']] += 1
    if report['casos']:
        require(dict(counts) == report['resumen'], 'Resumen distinto de los casos retenidos')
    else:
        require(len(report.get('controles', [])) == 11, 'Autoprueba incompleta')
    totals = Counter()
    for values in counts.values():
        totals.update(values)
    return dict(evidencia_sha256=sha(path), casos=len(seen), resultados=dict(totals),
                por_area=dict(counts), diferencias_por_tipo=dict(differences),
                controles=report.get('controles', []), entradas_actuales_coinciden=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidencia', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.evidencia), ensure_ascii=False, indent=2))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as error:
        print('EVIDENCIA NO VALIDA:', error)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
