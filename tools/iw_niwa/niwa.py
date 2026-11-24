"""NIWA: flujo M11 sin emulador y cotejo de un log del port."""
import argparse
import datetime as dt
import hashlib
import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True


def montar(repo):
    tools = repo / 'tools'
    if not (tools / 'iw_mips/ee.py').is_file():
        raise FileNotFoundError('ejecutar desde la raíz del proyecto o indicar --repo')
    sys.path.insert(0, str(tools / 'iw_mips'))
    import original
    return original


PATRONES = {
    'mapa': r'espejo listo \(saga (\d+), .*aparicion en (\d+) cuadros',
    'globo': r'globo de Bulma abierto \(reloj ([\d.]+), casilla (\d+)\)',
    'desbloqueo': r'iwmips mapa: fila (\d+) desbloqueada',
    'ficha': r'iw: ficha de la mision (\d+)\b',
    'entrada': r'iw: MISSION START mision (\d+)\b',
    'resultado': r'iw: mision (\d+) superada: \d+ s, rango (\d+) \(record \d+\), (\d+) Zenie',
    'cierre_resultado': r'dbz3 niwa resultado cerrado mision=(\d+)\b',
    'vuelta': r'vuelta de combate: ScreenTransition_PushCommands ([0-9A-Fa-fx/]+) \(\d+ ordenes\)',
    'pose': r'Story_MapResultDelayTick 60 cuadros: .*\(pose (\d+)\)',
    'recarga': r'Story_MapReloadAfterDelayTick 100 superado',
}


def hitos(lines, mision=11, fila=6):
    """Una vuelta por log: conserva precedencia real y rechaza otro intento intercalado."""
    encontrados = {}
    for i, linea in enumerate(lines, 1):
        for nombre, reg in PATRONES.items():
            m = re.search(reg, linea)
            if not m:
                continue
            g = m.groups()
            if nombre == 'mapa' and 'mapa' in encontrados:
                nombre = 'iconos'
                if 'resultado' not in encontrados:
                    continue
            if nombre in ('ficha', 'entrada', 'resultado', 'cierre_resultado') and int(g[0]) != mision:
                if 'ficha' in encontrados and 'recarga' not in encontrados:
                    raise ValueError(f'misión distinta intercalada en línea {i}')
                continue
            if nombre in ('globo', 'desbloqueo') and (int(g[-1]) != fila or 'ficha' in encontrados):
                continue
            if nombre in ('vuelta', 'pose', 'recarga', 'iconos') and 'entrada' not in encontrados:
                continue
            if nombre in encontrados:
                if nombre in ('ficha', 'entrada', 'resultado', 'vuelta'):
                    raise ValueError(f'dos {nombre} en el mismo log: proporcionar una sola vuelta')
                continue
            encontrados[nombre] = dict(linea=i, valores=list(g), instante=linea[1:24] if linea.startswith('[') else None)
    return encontrados


def cotejo_hitos(a, lines):
    esperado = {e['evento']: e for e in a['eventos']}
    b = hitos(lines, esperado['ficha']['mision'], esperado['ficha']['fila'])
    comparables = dict(
        mapa=[esperado['mapa']['saga'], esperado['mapa']['cuadros_aparicion']],
        globo=[esperado['globo']['reloj'], esperado['globo']['fila']],
        desbloqueo=[esperado['desbloqueo']['fila']], ficha=[esperado['ficha']['mision']],
        entrada=[esperado['entrada']['mision']],
        resultado=[esperado['resultado'][k] for k in ('mision', 'rango', 'zeni')],
        vuelta=[[c for c, _ in esperado['vuelta']['cola']]], pose=[esperado['pose']['anim']],
        iconos=[esperado['mapa']['saga'], esperado['iconos']['cuadros_aparicion']],
    )
    filas, dif = [], []
    for nombre, valores in comparables.items():
        d = b.get(nombre)
        v = None
        if d:
            g = d['valores']
            if nombre == 'vuelta':
                v = [[int(x, 0) for x in g[0].split('/')]]
            elif nombre == 'globo':
                v = [float(g[0]), int(g[1])]
            else:
                v = list(map(int, g))
        estado = 'SIN_DATO' if v is None else 'IGUAL' if v == valores else 'DIF'
        filas.append(dict(evento=nombre, original=valores, port=v, estado=estado, linea=d and d['linea']))

    # A la salida del resultado (0x285F30, estado 2) se encola la vuelta, no al entrar en el panel.
    pares = [('globo', 'ficha'), ('ficha', 'entrada'), ('entrada', 'resultado'),
             ('resultado', 'cierre_resultado'), ('cierre_resultado', 'vuelta'), ('vuelta', 'pose'), ('pose', 'recarga')]
    pendientes = []
    for x, y in pares:
        if x not in b or y not in b:
            pendientes.append([x, y])
        elif b[x]['linea'] >= b[y]['linea']:
            dif.append([x, y, b[x]['linea'], b[y]['linea']])
    wall = None
    if all(k in b and b[k]['instante'] for k in ('pose', 'recarga')):
        try:
            wall = (dt.datetime.fromisoformat(b['recarga']['instante']) -
                    dt.datetime.fromisoformat(b['pose']['instante'])).total_seconds()
        except ValueError:
            pass
    return dict(hitos=filas, orden_distinto=dif, orden_sin_dato=pendientes, segundos_pose_a_recarga=wall)


def autoprueba(original):
    ntsc_step = original.traza()
    pal = original.traza(1.2)
    bloqueada = original.traza(con_mini=False)
    tope = original.traza(saldo=999899)
    for r, apar, ficha, pose, rec in ((ntsc_step, 210, 41, 60, 161), (pal, 170, 34, 51, 135)):
        e = {x['evento']: x for x in r['eventos']}
        assert len(e) == 12 and (e['ficha']['mision'], e['entrada']['clave']) == (11, 0x7EC)
        assert (e['mapa']['cuadros_aparicion'], e['ficha_lista']['tick']) == (apar, ficha)
        assert (e['pose']['tick'], e['recarga']['tick'], e['iconos']['cuadros_aparicion']) == (pose, rec, 0)
        assert r['cuadros'][0]['x'] > 890 and r['cuadros'][0]['fila_globo'] == -1 and e['globo']['tick'] == 2
    assert not bloqueada['eventos'][0]['en_cola'] and bloqueada['eventos'][0]['capsula'] == 0
    assert [x['evento'] for x in bloqueada['eventos']] == ['mapa', 'movimiento']
    assert next(x for x in tope['eventos'] if x['evento'] == 'resultado')['saldo'] == 999900
    return {'paso_1': ntsc_step, 'paso_1_2': pal, 'sin_7d0': bloqueada, 'tope': tope}


def autoprueba_offline():
    """Controles rapidos de hitos() y cotejo_hitos() sin ELF PAL ni DATA_CMN.AFS."""
    muestra = [
        '[2026-11-24 03:20:00.000] espejo listo (saga 1, aparicion en 210 cuadros)',
        '[2026-11-24 03:20:01.000] iwmips mapa: fila 6 desbloqueada',
        '[2026-11-24 03:20:02.000] globo de Bulma abierto (reloj 59.0, casilla 6)',
        '[2026-11-24 03:20:03.000] iw: ficha de la mision 11',
        '[2026-11-24 03:20:04.000] iw: MISSION START mision 11',
        '[2026-11-24 03:20:10.000] iw: mision 11 superada: 12 s, rango 2 (record 2), 4200 Zenie',
        '[2026-11-24 03:20:11.000] dbz3 niwa resultado cerrado mision=11',
        '[2026-11-24 03:20:11.100] vuelta de combate: ScreenTransition_PushCommands 5/14/10/4/1/8/13 (7 ordenes)',
        '[2026-11-24 03:20:11.200] espejo listo (saga 1, aparicion en 0 cuadros)',
        '[2026-11-24 03:20:12.000] Story_MapResultDelayTick 60 cuadros: fin (pose 8)',
        '[2026-11-24 03:20:13.667] Story_MapReloadAfterDelayTick 100 superado',
    ]
    ref = dict(eventos=[
        dict(evento='mapa', saga=1, cuadros_aparicion=210),
        dict(evento='globo', reloj=59.0, fila=6),
        dict(evento='desbloqueo', fila=6),
        dict(evento='ficha', mision=11, fila=6),
        dict(evento='entrada', mision=11),
        dict(evento='resultado', mision=11, rango=2, zeni=4200),
        dict(evento='vuelta', cola=[(5, 0), (14, 1), (10, 1), (4, 0), (1, 0), (8, 0), (13, 0)]),
        dict(evento='pose', anim=8),
        dict(evento='iconos', cuadros_aparicion=0),
    ])
    ok = cotejo_hitos(ref, muestra)
    if any(x['estado'] != 'IGUAL' for x in ok['hitos']) or ok['orden_distinto'] or ok['orden_sin_dato']:
        raise RuntimeError(f'control positivo NIWA fallo: {ok}')
    # Control negativo 1: cierre de resultado despues de encolar vuelta -> orden_distinto
    desorden = list(muestra)
    desorden[6], desorden[7] = desorden[7], desorden[6]
    if not cotejo_hitos(ref, desorden)['orden_distinto']:
        raise RuntimeError('control negativo de orden NIWA no detectado')
    # Control negativo 2: premio alterado -> DIF
    mal_premio = [l.replace('4200 Zenie', '9999 Zenie') for l in muestra]
    if not any(x['evento'] == 'resultado' and x['estado'] == 'DIF' for x in cotejo_hitos(ref, mal_premio)['hitos']):
        raise RuntimeError('control negativo de premio NIWA no detectado')
    # Control negativo 3: mision intercalada -> ValueError
    intercalada = muestra[:4] + ['[2026-11-24 03:20:03.500] iw: ficha de la mision 12'] + muestra[4:]
    try:
        hitos(intercalada)
    except ValueError:
        pass
    else:
        raise RuntimeError('control negativo de mision intercalada NIWA no detectado')
    print('NIWA autoprueba offline: OK (9 hitos, orden, premio y rechazo de mision intercalada)')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', type=Path, default=Path.cwd())
    p.add_argument('--self-check', '--autoprueba', dest='self_check', action='store_true',
                   help='controles offline de hitos() y cotejo_hitos() sin ELF PAL ni AFS')
    modos = p.add_subparsers(dest='modo', required=False)
    modos.add_parser('autoprueba', help='cuatro controles ejecutables sobre ELF PAL')
    mapa = modos.add_parser('mapa-log', help='repetir los cuadros libres de un log por separado')
    mapa.add_argument('ruta', type=Path)
    mapa.add_argument('--afs', type=Path, help='DATA_CMN.AFS PAL; por defecto junto al ELF')
    for nombre, detalle in [('original', 'simular fronteras de M11'), ('log', 'comparar hitos de una vuelta de M11')]:
        s = modos.add_parser(nombre, help=detalle)
        if nombre == 'log':
            s.add_argument('ruta', type=Path)
        else:
            s.add_argument('--sin-7d0', action='store_true')
        s.add_argument('--paso', type=float, choices=(1.0, 1.2), default=1.0)
        s.add_argument('--dificultad', type=int, choices=range(6), default=2)
        s.add_argument('--rango', type=int, choices=range(5), default=2, help='rango inyectado, no inferido')
        s.add_argument('--afs', type=Path, help='DATA_CMN.AFS PAL; por defecto junto al ELF')
    args = p.parse_args()
    if args.self_check:
        try:
            autoprueba_offline()
            return 0
        except (OSError, ValueError, RuntimeError, AssertionError, KeyError) as err:
            p.exit(3, f'NIWA: {err}\n')
    if not args.modo:
        p.error('indique un subcomando (autoprueba, original, log, mapa-log) o --self-check')
    rc = 0
    try:
        if not __debug__:
            raise ValueError('ejecutar sin -O')
        original = montar(args.repo)
        if args.modo == 'autoprueba':
            r = autoprueba(original)
        elif args.modo == 'mapa-log':
            from replay import cotejar_mapa
            raw = args.ruta.read_bytes()
            r = cotejar_mapa(raw.decode('utf-8-sig', errors='replace').splitlines(), args.afs)
            r['sha256_log'] = hashlib.sha256(raw).hexdigest()
            rc = 1 if r['diferencias'] else 0 if r['completo'] else 2
        else:
            r = original.traza(args.paso, not getattr(args, 'sin_7d0', False), args.afs, args.dificultad, args.rango)
            if args.modo == 'log':
                raw = args.ruta.read_bytes()
                lines = raw.decode('utf-8-sig', errors='replace').splitlines()
                h = cotejo_hitos(r, lines)
                from replay import cotejar_mapa
                h['libre'] = cotejar_mapa(lines, args.afs)
                r = dict(referencia=r, port=h, sha256_log=hashlib.sha256(raw).hexdigest())
                estados = {item['estado'] for item in h['hitos']}
                rc = 1 if 'DIF' in estados or h['orden_distinto'] or h['libre']['diferencias'] else (
                    2 if 'SIN_DATO' in estados or h['orden_sin_dato'] or not h['libre']['completo'] else 0)
        print(json.dumps(r, indent=2, ensure_ascii=True, allow_nan=False))
        return rc
    except (OSError, ValueError, RuntimeError, AssertionError, KeyError) as err:
        p.exit(3, f'NIWA: {err}\n')


if __name__ == '__main__':
    sys.exit(main())
