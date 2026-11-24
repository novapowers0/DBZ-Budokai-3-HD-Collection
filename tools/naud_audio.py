#!/usr/bin/env python3
"""Compara peticiones de audio NAUD por escena, evento y ocurrencia."""

import argparse
from collections import Counter
from copy import deepcopy
import json
import math
from pathlib import Path
import sys


FORMAT = 'naud-audio-v1'


def need(test, message):
    if not test:
        raise ValueError(message)


def indexed(trace):
    need(isinstance(trace, dict) and trace.get('formato') == FORMAT
         and isinstance(trace.get('autoridad'), str), 'versión/autoridad de traza inválida')
    need(isinstance(trace.get('eventos'), list), 'se esperaba eventos[]')
    index = {}
    for event in trace['eventos']:
        need(isinstance(event, dict), 'cada evento debe ser un objeto')
        key = tuple(event[k] for k in ('escena', 'evento', 'vez'))
        need(all(isinstance(x, str) and x for x in (*key[:2], event['bus'], event['prueba']))
             and type(key[2]) is int and key[2] > 0, 'clave, bus o prueba inválidos')
        for field in ('ms', 'ganancia'):
            n = event[field]
            need(n is None or type(n) in (float, int) and math.isfinite(n) and n >= 0,
                 f'{key}: {field} inválido')
        unit = event['unidad']
        need(unit is None and event['ganancia'] is None or isinstance(unit, str) and bool(unit),
             f'{key}: unidad inválida')
        resource = event['recurso']
        need(resource is None or isinstance(resource, str) and bool(resource), f'{key}: recurso inválido')
        need(key not in index, f'clave repetida: {key}')
        index[key] = event
    return index


def compare(original, port, ms=0.1, gain=0.001):
    need(math.isfinite(ms) and math.isfinite(gain) and ms >= 0 and gain >= 0, 'tolerancias inválidas')
    aa, bb = indexed(original), indexed(port)
    rows, counts = [], Counter()
    for key in sorted(aa.keys() | bb.keys()):
        x, y = aa.get(key), bb.get(key)
        delta = {}
        elapsed = None
        if x is None or y is None:
            delta['ocurrencia'] = 'FALTA' if y is None else 'EXTRA'
        else:
            if x['ms'] is not None and y['ms'] is not None:
                elapsed = y['ms'] - x['ms']
            for field in ('bus', 'recurso', 'ms', 'ganancia'):
                xx, yy = x[field], y[field]
                if xx is None or yy is None or field == 'ganancia' and x['unidad'] != y['unidad']:
                    delta[field] = 'SIN_DATO'
                elif field in ('ms', 'ganancia'):
                    if abs(xx - yy) > (ms if field == 'ms' else gain):
                        delta[field] = 'DIFIERE'
                elif xx != yy:
                    delta[field] = 'DIFIERE'
        verdict = 'OK' if not delta else 'PENDIENTE' if set(delta.values()) == {'SIN_DATO'} else 'DIFIERE'
        rows.append(dict(clave=key, original=x, port=y, estado=verdict, campos=delta, delta_ms=elapsed))
        counts[verdict] += 1
    return dict(autoridades=[original['autoridad'], port['autoridad']], tolerancias=dict(ms=ms, ganancia=gain),
                resumen=dict(counts), filas=rows, rc=1 if counts['DIFIERE'] else 2 if not rows or counts['PENDIENTE'] else 0)


def selfcheck():
    def trace(*events):
        return dict(formato=FORMAT, autoridad='fixture', eventos=list(events))

    row = dict(escena='X', evento='voz', vez=1, bus='voz', recurso='AFS/1', ms=30,
               ganancia=0.7, unidad='lineal', prueba='fixture')
    base = trace(row)
    need(compare(base, deepcopy(base))['rc'] == 0, 'control positivo')
    for key, value in [('recurso', 'AFS/2'), ('ms', 31), ('ganancia', 0.5), ('bus', 'efecto'), ('vez', 2)]:
        need(compare(base, trace(dict(row, **{key: value})))['rc'] == 1, f'control negativo: {key}')
    for key in ('ms', 'ganancia', 'recurso', 'unidad'):
        value = 'dB' if key == 'unidad' else None
        need(compare(trace(dict(row, **{key: value})), base)['rc'] == 2, f'control de ausencia/unidad: {key}')
    need(compare(trace(), trace())['rc'] == 2, 'las trazas vacías no pasan')
    need(compare(trace(), base)['filas'][0]['campos']['ocurrencia'] == 'EXTRA', 'control extra')
    need(compare(base, trace())['filas'][0]['campos']['ocurrencia'] == 'FALTA', 'control falta')
    for bad in (trace(row, row), trace(dict(row, ms=math.nan)), trace(None), trace(dict(row, unidad=None))):
        try:
            indexed(bad)
        except ValueError:
            continue
        raise ValueError('se aceptó una traza inválida')
    print('Autoprueba: 17 controles OK')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-check', action='store_true', help='ejecuta controles sintéticos sin datos del juego')
    sub = parser.add_subparsers(dest='command')
    judge = sub.add_parser('comparar', help='coteja dos archivos JSON naud-audio-v1')
    judge.add_argument('original', type=Path)
    judge.add_argument('port', type=Path)
    judge.add_argument('--ms', type=float, default=0.1, help='tolerancia temporal en ms (0.1)')
    judge.add_argument('--ganancia', type=float, default=0.001, help='tolerancia de ganancia en la misma unidad (0.001)')
    judge.add_argument('--salida', type=Path, help='guarda el JSON completo en un archivo nuevo')
    args = parser.parse_args(argv)
    if args.self_check and args.command:
        parser.error('--self-check no acepta subcomando')
    try:
        if args.self_check:
            selfcheck()
            return 0
        if args.command == 'comparar':
            traces = [json.loads(p.read_text(encoding='utf-8-sig')) for p in (args.original, args.port)]
            report = compare(*traces, args.ms, args.ganancia)
            if args.salida:
                with args.salida.open('x', encoding='utf-8') as f:
                    json.dump(report, f, ensure_ascii=False, allow_nan=False, indent=2)
                    f.write('\n')
            print(json.dumps(report['resumen'] if args.salida else report, ensure_ascii=False, indent=2))
            return report['rc']
    except (OSError, ValueError, KeyError, OverflowError) as exc:
        print(f'NAUD: {exc}', file=sys.stderr)
        return 3
    parser.print_help()
    return 2


if __name__ == '__main__':
    sys.exit(main())
