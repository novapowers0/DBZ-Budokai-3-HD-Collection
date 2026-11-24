"""Ejecutar: python -B tools/b2_oraculo/banco.py --ayuda.

0 = sin diferencias; 1 = diferencias observadas; 2 = banco incompleto/error.
--autoprueba verifica los controles y contraejemplos conocidos, devuelve 0 si
el propio banco responde como se espera. No equivale a que el port sea igual.
"""
import argparse
import hashlib
import json
import random
import sys
import traceback
from collections import Counter, defaultdict
from datetime import datetime, timezone
from importlib.metadata import version
from pathlib import Path

import fuentes
import modelo_port as port
import original as orig

DEFAULT_STATE = Path('E:/DBZ3HD_trabajo/grabaciones_original/b2_tablero_f4_mov/m00.p2s')


def compare(real, model, evidence, **extra):
    return dict(original=real, modelo_port=model, iguales=real == model, traza=evidence, **extra)


def ally_case(base, pieces=None):
    inputs, real, tr = orig.allies(base, pieces)
    return compare(real, port.allies(inputs), tr, t2=inputs)


def phase_case(base, order, following, current):
    real, tr = orig.phase(base, order, following, current)
    return compare(real, port.phase(order, following, current), tr,
                   normalizacion='fase persistida, solicitud guardar y destino 9/10/11; no cuadros de UI')


def rival_case(base, neighbors, seed):
    word, nextseed = orig.lfsr(seed)
    real, tr = orig.rivals(base, neighbors, seed)
    # rnd del port se inyecta solo en su MODELO y dentro del rango real de rand.
    result = compare(real['paso'], port.rivals(neighbors, word & 3), tr,
                     azar_port=word & 3, detalle_original=real,
                     contrato='misma direccion sorteada; no mismo generador ni reloj')
    if real['semilla_final'] != nextseed:
        raise AssertionError('LFSR del modelo difiere del ELF')
    return result


def rival_order_case(base, pieces=None):
    inputs, real, tr = orig.rival_order(base, pieces)
    return compare(real, port.rival_order(inputs), tr, t2=inputs,
                   contrato='sin azar al elegir actor; acciones consumidas sin mover ni combatir')


def rival_clock_case(base):
    neighbors = [2, 0, 0, 0]
    real, tr = orig.rivals(base, neighbors, 1)
    return compare(real['paso'], port.rivals(neighbors, 1), tr,
                   contrato='FRONTERA: reloj_port&3=1; no hay semilla comun del algoritmo')


def capsule_case(base, group, mask, seed, records=None, inventory=None):
    inputs, real, tr = orig.capsules(base, group, mask, seed, records, inventory)
    word, _ = orig.lfsr(seed)
    # El residuo cabe en rand() (bolsa <=520), sin fingir que rand devuelve u32.
    a, b = [], []
    for i, (m, g, maximum, inv) in enumerate(inputs):
        if g == group and (m == 0 or m & mask):
            if inv < maximum:
                a.append(i)
            if m:
                b.append(i)
    pool = a or b
    token = word % len(pool) if pool else 0
    expected_state = orig.lfsr(seed)[1] if pool else seed
    if tr['estado_rng_final'] != expected_state:
        raise AssertionError('Consumo del RNG de capsulas incorrecto')
    return compare(real, port.capsules(inputs, group, mask, token), tr,
                   azar_port=token, bolsa_a=len(a), bolsa_b=len(b),
                   tabla_inventario_sha256=hashlib.sha256(json.dumps(inputs).encode()).hexdigest(),
                   contrato='mismo rango de seleccion; inventario y mascara ya resueltos')


def wish_case(base, flags, seed):
    real, pools, tr = orig.wishes(base, flags, seed)
    model, final_seed = port.wishes(set(flags), seed, detail=True)
    if tr['estado_rng_final_u64'] != final_seed:
        raise AssertionError('Consumo del RNG de deseos difiere')
    return compare(real, model, tr, bolsas=pools,
                   contrato='mismas banderas efectivas y semilla u64; no persistencia ni semilla de reloj')


def wish_inventory_case(base, enabled):
    real, pools, tr = orig.wishes(base, [461], 1)
    flags = port.effective_wish_flags([], {461: 1}, enabled)
    return compare(real, port.wishes(flags, 1), tr, bolsas=pools,
                   contrato='original SAVE[0x984+461]=1; port nave_b2_461=1, sin tablero_b2_dw461; no se ejecuta guardar')


def cases(base):
    for current in (0, 3, 8):
        for following in range(9):
            inp = dict(orden=41, siguiente=following, fase_actual=current)
            yield f'fase/41/{current}/{following}', 'logica', inp, lambda x=inp: phase_case(base, x['orden'], x['siguiente'], x['fase_actual'])
        for order in (42, 43):
            inp = dict(orden=order, siguiente=0, fase_actual=current)
            yield f'fase/{order}/{current}', 'logica', inp, lambda x=inp: phase_case(base, x['orden'], x['siguiente'], x['fase_actual'])
    for following in (-1, 9, 255, 256):
        yield f'fase/limite/{following}', 'robustez_fuera_dominio', dict(orden=41, siguiente=following, fase_actual=3), lambda x=following: phase_case(base, 41, x, 3)

    yield 'aliados/capturado', 'logica', dict(estado=str(base.path), reiniciar_registro_desde_t2=True), lambda: ally_case(base)
    for char in port.ALLY_ORDER:
        for unlocked in (0, 1):
            piece = [dict(personaje=char, bando=0, abierto=unlocked)]
            yield f'aliados/id{char}/abierto{unlocked}', 'logica', dict(piezas=piece), lambda x=piece: ally_case(base, x)
    for bando in (2, 4, 6):
        piece = [dict(personaje=28, bando=bando, abierto=1)]
        yield f'aliados/bando{bando}', 'logica', dict(piezas=piece), lambda x=piece: ally_case(base, x)
    for chars in ([28, 26, 6], list(reversed(port.ALLY_ORDER))):
        piece = [dict(personaje=x, bando=0, abierto=1) for x in chars]
        yield f'aliados/orden{len(chars)}', 'logica', dict(piezas=piece), lambda x=piece: ally_case(base, x)

    for neighbors in ([2, 3, 4, 5], [2, 0, 0, 0], [0, 0, 0, 0], [0, 3, 4, 0]):
        label = ''.join('1' if x else '0' for x in neighbors)
        for seed in (1, 2, 3, 4, 0x12345678, 0x7B39638A, 0xFFFFFFFF, 0):
            yield f'rivales/mov3/{label}/{seed}', 'logica_con_azar_acoplado', dict(vecinas=neighbors, semilla=seed), lambda n=neighbors, s=seed: rival_case(base, n, s)
    yield 'rivales/reloj_distinto', 'frontera_azar', dict(vecinas=[2, 0, 0, 0], semilla_original=1, reloj_port_bits=1), lambda: rival_clock_case(base)
    yield 'rivales/orden_capturado', 'logica', dict(reiniciar_t2=True), lambda: rival_order_case(base)
    for count in (0, 1, 2, 3):
        for bando in (0, 2, 4, 6):
            pieces = [dict(personaje=25, bando=bando, acciones_turno=count),
                      dict(personaje=16, bando=6, acciones_turno=2),
                      dict(personaje=22, bando=6, acciones_turno=1)]
            yield f'rivales/orden/{bando}/{count}', 'logica', dict(piezas=pieces), lambda p=pieces: rival_order_case(base, p)

    for group in (0, 1, 2, 3, 4, 5, 35):
        for mask in (0, 1, 1 << 28, 1 << 35, (1 << 36) - 1):
            for seed in (1, 0x12345678):
                yield f'capsulas/capturado/{group}/{mask}/{seed}', 'logica_con_azar_acoplado', dict(grupo=group, mascara=mask, semilla=seed), lambda g=group, m=mask, s=seed: capsule_case(base, g, m, s)
    layouts = {
        'generica': [dict(id=1, mascara=0)],
        'dos': [dict(id=1, mascara=1), dict(id=3, mascara=1)],
        'bit35': [dict(id=1, mascara=1), dict(id=3, mascara=1 << 35)],
        'sin_mascara_llena': [dict(id=1, mascara=0)],
        'vacia': [],
    }
    for label, records in layouts.items():
        for full in (False, True):
            inv = {row['id']: int(full) for row in records}
            for seed in (1, 2, 0x12345678):
                mask = 1 << 35 if label == 'bit35' else 1
                inp = dict(grupo=9, mascara=mask, semilla=seed, registros=records, inventario=inv)
                yield f'capsulas/sintetico/{label}/{int(full)}/{seed}', 'logica_con_azar_acoplado', inp, lambda x=inp: capsule_case(base, x['grupo'], x['mascara'], x['semilla'], x['registros'], x['inventario'])

    flag_sets = [[], [461], [461, 462], [461, 462, 463],
                 [503, 461, 462, 314, 253], [314, 253],
                 [323, 324, 220], [318, 253, 314],
                 list(range(314, 343)), list(range(314, 343)) + [237, 266, 279, 290, 301]]
    fixed_flags = [f for f, _ in port.FIXED]
    for n in range(1, 7):
        flag_sets.append(fixed_flags[:n])
    flag_sets.append(fixed_flags + list(range(314, 343)) + [237, 266, 279, 290, 301])
    rng = random.Random(0xB2)
    universe = sorted(set(fixed_flags + list(range(314, 343)) + list(range(461, 490))
                          + [237, 266, 279, 290, 301, 220, 253]))
    for _ in range(32):
        flag_sets.append([f for f in universe if rng.randrange(2)])
    for i, flags in enumerate(flag_sets):
        for seed in (1, 2, 0x123456789ABCDEF0):
            yield f'deseos/flags{i}/{seed}', 'logica', dict(banderas=flags, semilla=seed), lambda f=flags, s=seed: wish_case(base, f, s)
    for enabled in (False, True):
        yield f'deseos/inventario/{int(enabled)}', 'integracion_modelada', dict(inventario={461: 1}, banderas_dw=[], requisitos_inventario=enabled, semilla=1), lambda e=enabled: wish_inventory_case(base, e)


def selftest(base):
    """Controles reales: rechazo estricto, argumentos u64 y contraejemplo minimo."""
    checks = []
    c = base.cpu()
    try:
        orig.invoke(c, 0x124EA8)
    except RuntimeError as e:
        assert 'llamada sin puente 0x11da00' in str(e)
        checks.append('llamada_no_admitida_rechazada')
    else:
        raise AssertionError('El modo estricto no rechazo una llamada sin contrato')
    c = base.cpu()
    c.w32(0x12E4E0, c.r32(0x12E4E0) ^ 1)
    try:
        orig.invoke(c, 0x12E4E0, 32)
    except ValueError as e:
        assert 'Instruccion distinta del ELF' in str(e)
        checks.append('codigo_alterado_rechazado')
    else:
        raise AssertionError('Se ejecuto codigo distinto del ELF')
    old = fuentes.REVIEWED['aliados']
    try:
        fuentes.REVIEWED['aliados'] = 'sello_obsoleto'
        try:
            fuentes.validate()
        except ValueError as e:
            assert 'aliados' in str(e)
            checks.append('modelo_obsoleto_rechazado')
        else:
            raise AssertionError('Se admitio un modelo con sello obsoleto')
    finally:
        fuentes.REVIEWED['aliados'] = old
    for seed in (0, 1, 2, 3, 4, 0x12345678, 0x7B39638A, 0xFFFFFFFF):
        c = base.cpu()
        c.w32(c.gp - 0x7FE8, seed)
        real = orig.invoke(c, 0x12E4E0, 32)
        word, nextseed = orig.lfsr(seed)
        assert (real, c.r32(c.gp - 0x7FE8)) == (word, nextseed)
    checks.append('rng_lfsr_8_vectores')
    for order in (41, 42, 43):
        assert phase_case(base, order, 4, 3)['iguales']
    checks.append('fase_3_ordenes')
    assert ally_case(base, [dict(personaje=28, abierto=1)])['iguales']
    delta = ally_case(base, [dict(personaje=28, abierto=0)])
    assert delta['original'] == [] and delta['modelo_port'] == [1]
    checks.append('aliados_control_y_diferencia_minima')
    assert rival_case(base, [2, 0, 0, 0], 1)['iguales']
    checks.append('rival_mov3_azar_acoplado')
    assert rival_order_case(base, [dict(personaje=25, bando=6, acciones_turno=2)])['iguales']
    assert not rival_clock_case(base)['iguales']
    checks.append('rivales_orden_y_frontera_reloj')
    cap = capsule_case(base, 9, 1 << 35, 1,
                       [dict(id=1, mascara=1), dict(id=3, mascara=1 << 35)], {})
    assert cap['original'] == cap['modelo_port'] == 3
    checks.append('capsulas_argumento_mascara_u64')
    assert wish_case(base, [], 1)['iguales']
    assert wish_case(base, [461, 462], 0x123456789ABCDEF0)['iguales']
    checks.append('deseos_productor_y_sorteo_2_entradas')
    assert wish_inventory_case(base, True)['iguales']
    assert not wish_inventory_case(base, False)['iguales']
    checks.append('deseos_inventario_on_off')
    return checks


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--ayuda', action='help', help='Mostrar esta ayuda')
    ap.add_argument('--estado', type=Path, default=DEFAULT_STATE)
    ap.add_argument('--area', choices=['fase', 'rivales', 'aliados', 'capsulas', 'deseos'])
    ap.add_argument('--caso', help='Identificador exacto del caso; sin coincidencia se rechaza')
    ap.add_argument('--todos-estados', action='store_true', help='Agregar aliados en todos los .p2s hermanos')
    ap.add_argument('--autoprueba', action='store_true', help='Comprobar el banco, incluidos fallos conocidos')
    ap.add_argument('--salida', type=Path, help='JSON nuevo; su carpeta padre debe existir; no sobrescribe')
    args = ap.parse_args()
    if args.autoprueba and (args.area or args.caso or args.todos_estados):
        ap.error('--autoprueba se ejecuta sin filtros ni --todos-estados')
    if args.salida and (not args.salida.parent.is_dir() or args.salida.exists()):
        ap.error('--salida requiere un archivo ausente y un padre existente')
    sources = fuentes.validate()
    before = {str(p): orig.sha(p) for p in (args.estado, orig.PERFILES['b2']['elf'])}
    engines = (orig.TOOLS / 'mips_perfil.py', orig.TOOLS / 'iw_mips' / 'ee.py')
    engine_hashes = {str(p): orig.sha(p) for p in engines}
    before.update(engine_hashes)
    base = orig.Original(args.estado)
    start = datetime.now(timezone.utc).isoformat()
    report = dict(inicio_utc=start, frontera='ELF original ejecutado vs modelo Python revisado del C++; NO binario del port',
                  identidad=base.identity, fuentes=sources, casos=[], estados_adicionales=[],
                  motor=dict(python=sys.version, rabbitizer=version('rabbitizer'),
                             archivos_sha256=engine_hashes))
    if args.autoprueba:
        report['controles'] = selftest(base)
        print('AUTOPRUEBA OK:', ', '.join(report['controles']))
    else:
        selected = 0
        for name, kind, inputs, action in cases(base):
            if args.area and not name.startswith(args.area + '/'):
                continue
            if args.caso and args.caso != name:
                continue
            selected += 1
            row = dict(id=name, tipo=kind, entrada=inputs)
            try:
                row.update(action())
                row['estado'] = 'IGUAL' if row['iguales'] else 'DIFERENCIA'
            except Exception as e:
                row.update(estado='ERROR_BANCO', error=f'{type(e).__name__}: {e}')
                print(name, row['error'], file=sys.stderr)
            report['casos'].append(row)
            if args.caso:
                print(json.dumps(row, ensure_ascii=False, indent=2))
        if not selected:
            ap.error('No hay casos que coincidan con --area/--caso')
        if args.todos_estados and not args.caso and args.area in (None, 'aliados'):
            for state in sorted(args.estado.parent.glob('*.p2s')):
                if state.resolve() == args.estado.resolve():
                    continue
                before[str(state)] = orig.sha(state)
                other = orig.Original(state)
                row = dict(id='aliados/estado/' + state.stem, tipo='logica', entrada=dict(estado=str(state)))
                row.update(ally_case(other))
                row['estado'] = 'IGUAL' if row['iguales'] else 'DIFERENCIA'
                report['casos'].append(row)
                report['estados_adicionales'].append(other.identity)
        counts = defaultdict(Counter)
        for row in report['casos']:
            counts[row['id'].split('/')[0]][row['estado']] += 1
        report['resumen'] = dict(counts)
        print(json.dumps(report['resumen'], ensure_ascii=False, sort_keys=True))
    # Cambios concurrentes de fuente, ELF o estado invalidan la corrida.
    if fuentes.validate() != sources or any(orig.sha(p) != h for p, h in before.items()):
        raise RuntimeError('Una entrada cambio durante el banco; repetir sobre una copia estable')
    report['entradas_sin_cambios_al_final'] = True
    report['fin_utc'] = datetime.now(timezone.utc).isoformat()
    report['herramientas_sha256'] = {p.name: orig.sha(p) for p in Path(__file__).parent.glob('*.py')}
    if args.salida:
        with args.salida.open('x', encoding='utf-8') as out:
            json.dump(report, out, ensure_ascii=False, indent=2)
        print('Evidencia:', args.salida)
    states = {row['estado'] for row in report['casos']}
    return 2 if 'ERROR_BANCO' in states else 1 if 'DIFERENCIA' in states else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception:
        traceback.print_exc()
        sys.exit(2)
