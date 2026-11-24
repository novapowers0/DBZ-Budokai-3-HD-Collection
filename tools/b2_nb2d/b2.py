"""Entrada única del banco B2 offline. Reutiliza las sondas instaladas NB2A/B/C."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

sys.dont_write_bytecode = True
import radar
import popo
HERE = Path(__file__).resolve().parent
WORK = Path('E:/DBZ3HD_trabajo')
DEFAULT_ISO = Path('D:/DBZ3HD/ps2_games/DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso')
LLVM = Path('C:/Program Files/LLVM/bin/clang++.exe')
FOCI = ('movimiento', 'pasos', 'saltos', 'aliados', 'radar', 'vinetas', 'transiciones', 'shenron')
BANKS = ('recursos', 'animacion', 'eventos', 'radar', 'popo')


def require(ok, msg):
    if not ok:
        raise ValueError(msg)


def path_repo():
    return HERE.parents[1]


def connect(repo):
    paths = [repo/'tools'/p for p in ('b2_nb2a', 'b2_nb2b', 'b2_nb2c', 'iw_mips', '')]
    for p in paths:
        if not p.is_dir():
            raise FileNotFoundError(p)
    sys.path[:0] = list(map(str, paths))


def save(path, value):
    with path.open('x', encoding='utf-8', newline='\n') as f:
        json.dump(value, f, ensure_ascii=True, allow_nan=False, indent=1)
        f.write('\n')


def create(out):
    if out is None:
        return Path(tempfile.mkdtemp(prefix='b2-nb2d-'))
    out = out.resolve()
    out.mkdir()  # nunca escribir sobre una corrida anterior
    return out


def rc_for(rows):
    return 1 if any(f[1] is False for f in rows) else 2 if not rows or any(f[1] is None for f in rows) else 0


def verdict(codes):
    return next((code for code in (3, 1, 2) if code in codes), 0)


def datos_censo(a):
    import censo
    import banco_cpp
    a = argparse.Namespace(repo=a.repo, estado=a.estado or a.estados/'fase1.p2s',
                           iso=a.iso, loc=a.loc, ghidra=a.ghidra,
                           fase=sorted(set(a.fase or ())) or None, src=None)
    data = censo.censo(a)
    src = a.repo / radar.SRC
    require(len(data['fases']) == len(a.fase or range(1,10)), 'faltan fases en el censo')
    return data, src, censo, banco_cpp


def capas(a, out):
    data, src, censo, banco_cpp = datos_censo(a)
    req, obs, controls = banco_cpp.ejecutar(src, data, out, 'selector', a.cxx)
    rows, summary = banco_cpp.evaluar(req, obs, data['registros_ama397'], censo.comparar)
    data['cotejo_cpp'] = dict(filas=rows, resumen=summary, controles=controls)
    save(out/'censo.json', data)
    with (out/'censo.md').open('x', encoding='utf-8', newline='\n') as f:
        f.write(censo.md(data))
    state = summary['on']
    rc = 1 if any(v for k,v in state.items() if k not in ('IGUAL', 'PENDIENTE')) else 2 if not rows or state.get('PENDIENTE') else 0
    return dict(rc=rc, alcance='Identidad y orden de capas T1/T8 con la opción de NB2A ON; OFF solo como control. Pantallas restantes: inventario.',
                fases=list(data['fases']), filas=len(rows), resumen=summary, evidencia='censo.json')


def animacion(a, out):
    import b2_anim
    # measure crea su destino nuevo. Conserva exactamente su unidad y sus códigos.
    kwargs = dict(raiz=a.repo, estado=a.estado or a.estados/'fase1.p2s', iso=a.iso, salida=out/'sonda',
                  src=None, focos=a.foco, aristas=a.aristas, cxx=a.cxx,
                  paso_4px=not a.anim_off, color_ama_pal=not a.anim_off)
    rc = b2_anim.measure(argparse.Namespace(**kwargs))
    path = kwargs['salida'] / 'cotejo.json'
    data = json.loads(path.read_text(encoding='utf-8'))
    return dict(rc=rc, alcance='Curvas/posiciones/sonidos dirigidos de NB2B; consultar faltantes por foco.',
                resumen=data['resumen'], pendientes=data['pendientes'], evidencia='sonda/cotejo.json')


def event_matrix(adapter, repo, rows, cxx, out, slow=False):
    """Mide una vez lo que no depende de la opción de Popo (NB2C/cpp.py)."""
    start = time.perf_counter()
    if slow:
        result = {opt: adapter.collect(repo, rows, opt, cxx, out) for opt in ('serie','off','on')}
        return result, dict(compilaciones=3, sondas=sum(len(d['casos']) for d in result.values()),
                            segundos=time.perf_counter()-start)
    (flag,) = re.findall(rf'REXCVAR_DEFINE_BOOL\(\s*{adapter.FLAG}\s*,\s*(true|false)\s*,',
                         (repo / adapter.SRC).read_text(encoding='utf-8-sig'))
    full = adapter.collect(repo, rows, 'serie', cxx, out)
    popo = {k:r for k,r in rows.items() if k.startswith('popo/')}
    default = 'on' if flag == 'true' else 'off'
    other = 'off' if default == 'on' else 'on'
    require(full['identidad']['popo_origen_pal'] == (default == 'on'), 'cambió la opción durante el banco')
    ids = dict(full['identidad'], ajuste=default, reutiliza='serie')
    result = {'serie': full, default: {**full, 'identidad': ids}}
    extra = adapter.collect(repo, popo, other, cxx, out) if popo else None
    if extra is not None:
        require(all(extra['identidad'][k] == full['identidad'][k] for k in ('src_sha256', 'nave_sha256')),
                'fuentes cambiaron entre las sondas de eventos')
        require(extra['casos'].keys() == popo.keys() and extra['casos'].keys() <= full['casos'].keys(),
                'cobertura Popo incompleta o dispar')
    result[other] = {**full, 'casos': {**full['casos'], **(extra['casos'] if extra else {})},
                     'identidad': {**full['identidad'], 'ajuste': other, 'popo_origen_pal': other == 'on',
                                   'sonda_no_popo': full['identidad']['sonda'],
                                   'sonda': extra['identidad']['sonda'] if extra else None,
                                   'reutiliza_no_popo': 'serie'}}
    return result, dict(compilaciones=1+(extra is not None),
                        sondas=len(full['casos'])+(len(extra['casos']) if extra else 0),
                        segundos=time.perf_counter()-start)


def eventos(a, out):
    import b2_eventos as judge
    import cpp
    import b2_juez
    pal = judge.collect(a.estado or a.estados/'fase9.p2s', a.caso or ())
    docs, fast = event_matrix(cpp, a.repo, pal['casos'], a.cxx, out)
    timings = dict(lote=fast)
    if a.medir_velocidad:
        slow, timings['separado'] = event_matrix(cpp, a.repo, pal['casos'], a.cxx, out, slow=True)
        for k, doc in docs.items():
            require(judge.canonical(doc['casos']) == judge.canonical(slow[k]['casos']),
                    f'matriz optimizada no equivale al banco aislado, ajuste {k}')
            require(judge.judge(pal, doc) == judge.judge(pal, slow[k]), f'veredicto distinto en {k}')
    rows = {k:judge.judge(pal, docs[k]) for k in ('serie','off','on')}
    counts = {k:Counter('igual' if ok is True else 'diferente' if ok is False else 'pendiente'
                       for _, ok, _ in r) for k,r in rows.items()}
    result = dict(PAL=pal, port=docs, filas=rows, firmas={k:b2_juez.firma(r) for k,r in rows.items()},
                  resumen=counts, rendimiento=timings)
    save(out/'eventos.json', result)
    return dict(rc=rc_for(rows['serie']), alcance='Se juzga el valor de serie por IDs alineados. OFF/ON comparan únicamente el efecto de la opción Popo.',
                resumen=counts, rendimiento=timings, evidencia='eventos.json')


def indicadores(a, out):
    result = radar.measure(a.repo, a.estado or a.estados/'fase1.p2s', out, a.cxx)
    save(out/'radar.json', result)
    return dict(rc=int(result['diferencias']['ON'] != 0), alcance=result['alcance'],
                filas=len(result['casos']), resumen=result['diferencias'], evidencia='radar.json')


def aterrizaje(a, out):
    result = popo.measure(a.repo, a.estado or a.estados/'fase9.p2s', out, a.cxx)
    save(out/'popo.json', result)
    return dict(rc=int(result['diferencias']['ON'] != 0), alcance=result['alcance'],
                filas=len(result['casos']), resumen=result['diferencias'], evidencia='popo.json')


RUN = dict(recursos=capas, animacion=animacion, eventos=eventos, radar=indicadores, popo=aterrizaje)


def check_args(a):
    require(0 <= a.aristas <= 512, 'aristas fuera de [0,512]')
    require(not a.foco or len(a.foco) == len(set(a.foco)), 'foco repetido')
    require(not a.caso or len(a.caso) == len(set(a.caso)), 'caso repetido')


def execute(s, a, out):
    start = time.perf_counter()
    try:
        result = RUN[s](a, out)
    except FileNotFoundError as e:
        result = dict(rc=2, pendiente=str(e))
    except (OSError, RuntimeError, IndexError, KeyError, ValueError, TypeError, AssertionError, ImportError,
            struct.error, zipfile.BadZipFile, subprocess.SubprocessError) as e:
        result = dict(rc=3, error=str(e))
    result.update(segundos=time.perf_counter()-start)
    return result


def selfcheck():
    import b2_anim
    import b2_eventos
    import b2_juez
    import censo
    censo.autoprueba()
    b2_anim.selfcheck()
    b2_eventos.autoprueba()
    require(b2_juez.autoprueba() == 0, 'autoprueba del juez PM-23')
    require(rc_for([]) == rc_for([('dato', None)]) == 2, 'vacío no es PASS')
    require(rc_for([('a', None), ('b', False)]) == verdict([0,2,1]) == 1, 'prioridad de divergencia')
    require(verdict([0,1,2,3]) == 3 and verdict([0]) == 0, 'prioridad de error')
    require(radar.equal([[17,0],[89,120]], [[17,0],[89,120.001]]), 'tolerancia de reloj radar')
    require(not radar.equal([[87,0],[88,0]], [[88,0],[87,0]]) and not radar.equal([[89,120]], []),
            'orden/cobertura radar')
    require(len({r['id'] for r in radar.cases()}) == 90, 'IDs únicos radar')
    print('NB2D autoprueba: 0')


def parser():
    p = argparse.ArgumentParser(description=__doc__, epilog='0: alcance cotejado sin diferencias; 1: hay diferencias; 2: falta evidencia; 3: error. En batería: error > diferencia > pendiente.')
    p.add_argument('--repo', type=Path, default=path_repo(), help='raíz local del proyecto (antes del subcomando)')
    p.add_argument('--self-check', '--autoprueba', action='store_true', help='controles sin ISO, estado, juego ni compilador')
    modes = p.add_subparsers(dest='modo')
    for key, help_ in [('recursos','censo PAL y capas T1/T8 OFF/ON'), ('animacion','curvas y tiempos NB2B'),
                       ('eventos','matriz de eventos de serie/OFF/ON'), ('radar','flechas y puntos OFF/ON del C++ actual'),
                       ('popo','huecos de aterrizaje OFF/ON del C++ actual'), ('bateria','un informe de los bancos seleccionados')]:
        s = modes.add_parser(key, help=help_)
        s.add_argument('--salida', type=Path, help='carpeta nueva, con padre existente (por defecto una carpeta temporal nueva)')
        s.add_argument('--estado', type=Path, help='estado PAL alternativo; en batería afecta a todos los bancos seleccionados')
        s.add_argument('--estados', type=Path, default=WORK/'A2/sstates', help='carpeta de estados (fase1 y fase9)')
        s.add_argument('--cxx', default=str(LLVM) if LLVM.is_file() else shutil.which('clang++') or 'clang++')
        s.set_defaults(aristas=12, fase=None, foco=None, caso=None)
        if key == 'bateria':
            s.add_argument('--banco', action='append', choices=BANKS, help='repetible, todos si no se indica')
        if key in ('recursos', 'animacion', 'bateria'):
            s.add_argument('--iso', type=Path, default=DEFAULT_ISO)
        if key in ('recursos', 'bateria'):
            s.add_argument('--fase', type=int, choices=range(1,10), action='append')
            s.add_argument('--loc', choices=[f'DATA_{x}.AFS' for x in ('SP','UK','FR','IT','DU')], default='DATA_SP.AFS')
            s.add_argument('--ghidra', type=Path, default=WORK/'ghidra/salida/b2.c')
        if key in ('animacion', 'bateria'):
            s.add_argument('--foco', action='append', choices=FOCI, help='repetible; todos si no se indica')
            s.add_argument('--aristas', type=int, default=12)
            s.add_argument('--anim-off', action='store_true', help='mide con los dos ajustes NB2B apagados')
        if key in ('eventos', 'bateria'):
            s.add_argument('--caso', action='append', help='ID exacto NB2C, repetible; por defecto todos')
            s.add_argument('--medir-velocidad', action='store_true', help='control: compara con tres ejecuciones independientes de NB2C')
    modes.add_parser('casos', help='lista los IDs NB2C para --caso')
    s = modes.add_parser('pm23', help='compara volcados b2-state-v1 con el juez histórico')
    s.add_argument('pal', type=Path)
    s.add_argument('port', type=Path)
    s.add_argument('--vblank', type=int, default=1)
    s.add_argument('--ordenes', type=int, default=2)
    return p


def main(argv=None):
    p = parser()
    a = p.parse_args(argv)
    if a.self_check and a.modo:
        p.error('--self-check no se combina con un subcomando')
    if not a.modo and not a.self_check:
        p.print_help()
        return 0
    a.repo = a.repo.resolve()
    try:
        connect(a.repo)
        if a.self_check:
            selfcheck()
            return 0
        if a.modo == 'casos':
            from pal import cases
            for key in sorted(cases()):
                print(key)
            return 0
        if a.modo == 'pm23':
            import b2_juez as j
            require(min(a.vblank, a.ordenes) >= 0, 'tolerancias negativas')
            return j.imprimir(j.juzgar(j.cargar(a.pal), j.cargar(a.port), a.vblank, a.ordenes))
        check_args(a)
        out = create(a.salida)
        selection = tuple(dict.fromkeys(a.banco or BANKS)) if a.modo == 'bateria' else (a.modo,)
        result = {}
        for name in selection:
            sub = out/name
            sub.mkdir()
            result[name] = execute(name, a, sub)
        rc = verdict([r['rc'] for r in result.values()])
        data = dict(formato='nb2d-bateria-v1', rc=rc, bancos=result,
                    limite='Los bancos tienen distinta autoridad y unidad. El resultado no acredita partida ni píxeles.')
        save(out/'resultado.json', data)
        print(json.dumps(data, ensure_ascii=True, indent=1))
        print(f'Evidencia: {out}')
        return rc
    except FileNotFoundError as e:
        print(f'PENDIENTE: {e}', file=sys.stderr)
        return 2
    except (OSError, RuntimeError, ImportError, ValueError, KeyError, TypeError, AssertionError) as e:
        print(f'ERROR: {e}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())
