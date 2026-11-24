"""Entrada única al juez focal NB2C y al juez PM-23 existente."""
import argparse
from copy import deepcopy
import json
import subprocess
import sys
from pathlib import Path

DEFAULT = Path('E:/DBZ3HD_trabajo/A2/sstates/fase9.p2s')
ROOT = Path(__file__).resolve().parents[2]
FMT = 'b2-eventos-nb2c-v1'


def project(root):
    if not (root / 'tools/b2_juez.py').is_file():
        raise ValueError('ejecuta desde la raíz del proyecto o pasa --proyecto')
    sys.path.insert(0, str(root / 'tools'))
    import b2_juez
    return b2_juez


def collect(state, names):
    from pal import cases, o
    available = cases()
    if names and set(names) - available.keys():
        raise ValueError(f'casos desconocidos: {sorted(set(names) - available.keys())}')
    base = o.Original(state)
    rows = {}
    for case, (fn, kw) in available.items():
        if not names or case in names:
            value, trace = fn(base, **kw)
            rows[case] = dict(entrada=kw, valor=value, traza=trace)
    return dict(formato=FMT, lado='pal', metodo='elf_ee', identidad=base.identity, casos=rows)


def canonical(value):
    # Evita tratar True == 1, o 1 == 1.0, al cotejar JSON.
    return json.dumps(value, sort_keys=True, allow_nan=False, ensure_ascii=True)


def unique(pairs):
    d = {}
    for k, v in pairs:
        if k in d:
            raise ValueError(f'clave JSON duplicada: {k}')
        d[k] = v
    return d


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'), object_pairs_hook=unique)


def validate(doc, side):
    if not isinstance(doc, dict) or doc.get('formato') != FMT or doc.get('lado') != side:
        raise ValueError(f'se esperaba {FMT}, lado={side}')
    if not isinstance(doc.get('identidad'), dict) or not doc['identidad'] or not doc.get('metodo'):
        raise ValueError(f'{side}: identidad o método ausente')
    if not isinstance(doc.get('casos'), dict):
        raise ValueError(f'{side}: falta el objeto casos')
    for key, row in doc['casos'].items():
        if (not isinstance(row, dict) or not isinstance(row.get('entrada'), dict)
                or not isinstance(row.get('valor'), dict)):
            raise ValueError(f'{side}/{key}: se requieren objetos entrada y valor')
    canonical(doc)  # también rechaza NaN/Infinity de un lector JSON permisivo


def judge(a, b, names=()):
    validate(a, 'pal')
    validate(b, 'port')
    aa, bb = a['casos'], b['casos']
    results = []
    for key in sorted(set(names) or (aa.keys() | bb.keys())):
        x, y = aa.get(key), bb.get(key)
        if x is None or y is None:
            results.append((key, None, 'falta caso en ' + ('A y B' if x is y else 'A' if x is None else 'B')))
            continue
        if canonical(x['entrada']) != canonical(y['entrada']):
            results.append((key, None, 'entradas distintas: no son un par'))
            continue
        av, bv = x['valor'], y['valor']
        if not av:
            results.append((key, None, 'A sin campos observados'))
        for field, expected in sorted(av.items()):
            actual = bv.get(field)
            if expected is None or actual is None:
                results.append((f'{key}.{field}', None, 'falta dato comparable'))
            else:
                results.append((f'{key}.{field}', canonical(expected) == canonical(actual),
                                f'A={canonical(expected)} B={canonical(actual)}'))
    return results or [('cobertura', None, 'ningún caso en A ni B')]


def save(path, data):
    if path is None:
        print(json.dumps(data, indent=2, ensure_ascii=True, allow_nan=False))
    else:
        # Exclusivo: no pisar una corrida anterior ni otra entrada.
        with path.open('x', encoding='utf-8', newline='\n') as f:
            json.dump(data, f, indent=2, ensure_ascii=True, allow_nan=False)
            f.write('\n')


def autoprueba():
    key = 'popo/5-a-24'
    a = dict(formato=FMT, lado='pal', metodo='fixture', identidad={'fixture': True}, casos={
        key: dict(entrada={'llegada': 5, 'destino': 24}, valor={'desde': 24, 'slot': 0})})
    b = deepcopy(a)
    b['lado'] = 'port'
    assert [ok for _, ok, _ in judge(a, b)] == [True, True]
    b['casos'][key]['valor']['slot'] = False
    assert [ok for _, ok, _ in judge(a, b)] == [True, False]
    b['casos'][key]['entrada']['llegada'] = 6
    assert judge(a, b)[0][1] is None and judge(a, b, ('ausente',))[0][1] is None
    try:
        unique([('caso', 1), ('caso', 2)])
    except ValueError:
        pass
    else:
        raise AssertionError('clave duplicada aceptada')
    print('NB2C: autoprueba del comparador OK')


def cli(argv=None):
    p = argparse.ArgumentParser(description=__doc__, epilog='Cotejo: 0 igual; 1 diferencia; 2 pendiente; 3 error de datos/ejecución.')
    p.add_argument('--proyecto', type=Path, default=ROOT)
    p.add_argument('--autoprueba', '--self-check', dest='autoprueba', action='store_true', help='comprobar el comparador sin datos del juego')
    sub = p.add_subparsers(dest='modo')
    sub.add_parser('listar', help='listar los contratos PAL disponibles')
    def filters(s):
        s.add_argument('--caso', action='append', help='ID exacto, repetible; por defecto todos')
        s.add_argument('--salida', type=Path, help='resultado JSON en un archivo nuevo')
    sp = sub.add_parser('pal', help='ejecutar el ELF sobre RAM privada')
    sp.add_argument('--estado', type=Path, default=DEFAULT)
    filters(sp)
    sc = sub.add_parser('cpp', help='compilar y medir fragmentos literales del port para los casos disponibles')
    sc.add_argument('pal', type=Path)
    sc.add_argument('--ajuste', choices=('serie', 'on', 'off'), default='serie',
                    help='valor de dbz3_b2_popo_origen_pal solo en la sonda')
    sc.add_argument('--compilador', default='clang++')
    sc.add_argument('--trabajo', type=Path, help='carpeta existente para guardar las sondas (por defecto, TEMP)')
    sc.add_argument('--salida', type=Path)
    sj = sub.add_parser('juzgar', help='cotejar los casos PAL y port por ID y contrato')
    sj.add_argument('pal', type=Path)
    sj.add_argument('port', type=Path)
    filters(sj)
    sl = sub.add_parser('pm23', help='reusar el juez de volcados b2-state-v1 (PM-23)')
    sl.add_argument('pal', type=Path)
    sl.add_argument('port', type=Path)
    sl.add_argument('--vblank', type=int, default=1)
    sl.add_argument('--ordenes', type=int, default=2)
    a = p.parse_args(argv)
    if a.autoprueba and a.modo:
        p.error('--autoprueba no se combina con un subcomando')
    if not a.modo and not a.autoprueba:
        p.print_help()
        return 2
    root = a.proyecto.resolve()
    try:
        old = project(root)
        if a.autoprueba:
            autoprueba()
        elif a.modo == 'listar':
            from pal import cases
            for name, (_, kw) in cases().items():
                print(name, canonical(kw))
        elif a.modo == 'pal':
            save(a.salida, collect(a.estado, a.caso or ()))
        elif a.modo == 'cpp':
            from cpp import collect as collect_cpp
            ref = read(a.pal)
            validate(ref, 'pal')
            save(a.salida, collect_cpp(root, ref['casos'], a.ajuste, a.compilador, a.trabajo))
        elif a.modo == 'juzgar':
            pa, po = read(a.pal), read(a.port)
            rows = judge(pa, po, a.caso or ())
            if a.salida:
                save(a.salida, dict(metodos=[pa['metodo'], po['metodo']], firma=old.firma(rows), filas=rows))
            return old.imprimir(rows)
        elif a.modo == 'pm23':
            return old.imprimir(old.juzgar(old.cargar(a.pal), old.cargar(a.port), a.vblank, a.ordenes))
        return 0
    except (AssertionError, ImportError, OSError, ValueError, RuntimeError, KeyError, TypeError,
            subprocess.SubprocessError) as exc:
        print(f'NB2C ERROR: {exc}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(cli())
