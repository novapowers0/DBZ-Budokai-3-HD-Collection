"""NB2J: cotejos offline de combate, Dende PAL y diálogo en B2; nunca lanza el juego."""
import argparse
from collections import Counter
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SOURCE = Path('src/pasarela_b2.cpp')
SONDA_TPL = HERE / 'sonda.cpp'
GRUPOS = ('combate', 'dende', 'dialogo')
PENDIENTES_NB2I = frozenset({
    'combate/salida-jugador',
    'combate/salida-rival',
    'dende/4.banda_vida',
    'dende/4.vida_raw',
    'dende/5.banda_vida',
    'dende/5.vida_raw',
    'dialogo/abre.pasos',
    'dialogo/abre.termina',
    'dialogo/abierto',
})


def exigir(test, msg):
    if not test:
        raise ValueError(msg)


def cargar(repo):
    exigir((repo / 'tools/b2.py').is_file(), 'pasa --proyecto con la raíz del port')
    sys.path[:0] = [str(HERE), str(repo / 'tools'), str(repo / 'tools/b2_nb2c'), str(repo / 'tools/b2_nb2i')]


def ejecutar(args, cwd, log=None, timeout=90):
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('DBZ3_TEST_')}
    r = subprocess.run(list(map(str, args)), cwd=cwd, capture_output=True, env=env,
                       text=True, encoding='utf-8', errors='replace', timeout=timeout)
    if log is not None:
        with log.open('x', encoding='utf-8') as f:
            f.write(f'cwd: {cwd}\n{list(map(str, args))!r}\nexit={r.returncode}\n{r.stdout}{r.stderr}')
    return r


def checked(args, cwd, log=None):
    r = ejecutar(args, cwd, log)
    exigir(r.returncode == 0, f'{args[0]} devolvió {r.returncode}:\n{r.stdout}{r.stderr}')
    return r.stdout


def sonda(texto, work, compiler, patched, name=None):
    texto = texto.replace('\r\n', '\n')
    from cpp import fragment
    from sondas import bloque

    def frag(s):
        return fragment(texto, s)

    def blk(a, b):
        return bloque(texto, a, b)

    def rama(a):
        return blk(a, '\n    }') + '\n    }'

    hilo, = re.findall(r'^(struct Hilo \{\n[^}]+\};)\nstd::vector<Hilo> g_hilos;', texto, re.M)
    tipos = '\n'.join((frag('struct Ficha {') + ';', hilo, frag('struct CombateB2 {') + ';'))
    logica = [frag('void RecalcularSlots() {')]
    for n in ('PasosHasta', 'PasosContacto', 'TiempoPaso', 'Activa'):
        line, = re.findall(r'^(?:int|float|bool) ' + n + r'\([^\n]+$', texto, re.M)
        logica.append(line)
    logica += [frag(s) for s in (
        'int FichaEnHueco0(int casilla, bool rival) {',
        'void EmpezarPaso(Ficha& f, int dest, float ahora) {',
        'int PersonajeDe(int n) {',
        'void SucesosDeContacto(const Ficha& a, const Ficha& b) {',
        'void FundirPiezas(float objetivo, int excepto) {',
        'void PonerPose(Ficha& f, int anim, int estado) {',
        'void Contacto(int j, int r) {',
        'float IrAPunto(Ficha& f, int orden, float x, float y, float ahora) {',
        'void CurarDende(Ficha& f) {',
        'void ComponerVidaKi(const Ficha& j,',
        'void GuardarPiezasFase(',
        'bool LeerPiezasFase() {',
    )]
    logica.insert(0, blk('constexpr int Ficha::* kCamposPieza[] = {', '\nvoid GuardarPiezasFase('))
    logica += [
        'int BandaDende(const Ficha& f) { return ' + ('f.banda_dende' if patched else '-1') + '; }',
        'void PasoJugadorContacto(int i,int dest) {\n' + blk(
            '  Ficha& f = g_f[size_t(i)];\n  --f.movs;',
            '\n  // studies 8 (E): encuentro con un aliado.') + '\n}',
        'void LlegadaJugadorContacto() {\n' + blk(
            '  if (g_cont_j >= 0 && g_ahora >= g_cont_t) {',
            '\n  if (g_estado == kJuego && g_menu == 0 && !g_vuelta') + '\n}',
        'void PasoRivalContacto() {\n  auto& r=g_f[2];\n' + blk(
            '    if (g_rival_paso == 1) {',
            '\n  }\n}\n// Ordenes del guion que cambian') + '\n}',
        'int Despacho(float t,uint16_t pressed,int out[14]) { ImDrawList d; auto* dl=&d; S::Area a; bool auto_on=false;\n' +
        blk('  // combate pendiente: el "!!" se ve un momento y se pide el combate',
            '\n  if (g_pend_j >= 0) return 0;') + '\nreturn 0; }',
        'float Orden(int k,const std::array<int,6>& o) { float espera=0;\n' + rama('    if (o[1] == 38) {') + '\n' +
        rama('    if (o[1] == 39) {') + '\nassert(o[1]==35); ++marcador; return espera; }',
        frag('void CorrerHilos() {'),
        'void PanelPort(float t,uint16_t pressed) { ImDrawList d; auto* dl=&d; S::Area a; bool prepara=false, auto_on=false;\n' +
        blk('  if (!prepara && !g_mensajes.empty()) {   // ventana de mensaje',
            '\n  if (!prepara && g_mensajes.empty() && g_premio_id > 0') + '\n}',
    ]
    plantilla = SONDA_TPL.read_text(encoding='utf-8')
    salida = plantilla.replace('// @TIPOS@', tipos).replace('// @LOGICA@', '\n\n'.join(logica))
    stem = name or ('integrada' if patched else 'base')
    (work / f'{stem}.cpp').write_text(salida, encoding='utf-8')
    exe = work / f'{stem}.exe'
    checked([compiler, '-std=c++20', work / f'{stem}.cpp', '-o', exe], work, work / f'clang_{stem}.log')
    return exe


def numeros(exe, args, width):
    raw = checked([exe, *args], exe.parent)
    rows = [list(map(int, line.split())) for line in raw.splitlines()]
    exigir(rows and all(len(r) == width for r in rows), 'salida de sonda C++ inesperada')
    return rows


def medir(repo, work, estado, compilador, grupos, base_cpp=None, historico=None):
    import b2_eventos as ev
    import pal_nb2j as pal

    raw = (repo / SOURCE).read_bytes()
    texto = raw.decode('utf-8-sig')
    exigir(texto.count('REXCVAR_DEFINE_BOOL(dbz3_b2_dende_vida_pal, false,') == 1,
           'falta dbz3_b2_dende_vida_pal apagada de serie en src/pasarela_b2.cpp')
    base = pal.o.Original(estado)
    integrada = sonda(texto, work, compilador, True, 'integrada')
    previa = None
    if base_cpp is not None:
        previa = sonda(base_cpp.read_text(encoding='utf-8-sig'), work, compilador, False, 'base')
    datos, filas = {}, {}
    if 'combate' in grupos:
        for lado, rival in [('jugador', 0), ('rival', 1)]:
            key = 'combate/salida-' + lado
            pruebas = []
            for origen, dest, cond, otra in ((10, 12, 1, 2), (12, 10, 2, 1), (10, 12, 0, 1)):
                a = pal.combate(base, bool(rival), origen, dest, cond, otra)
                b, = numeros(integrada, ('combate', rival, origen, dest, cond, otra), 13)
                bv = dict(fichas=b[:2], casilla=b[2], vida=b[3:5], ki=b[5:7], pide_combate=b[7] == 2,
                          casilla_movil=b[8], casilla_quieta=b[9], tarjeta_cond=b[10], combate_cond=b[11], t4=b[12])
                campos = ('fichas', 'casilla', 'vida', 'ki', 'pide_combate')
                iguales = all(ev.canonical(a[k]) == ev.canonical(bv[k]) for k in campos)
                iguales &= a['destinos'] == [dest, dest] and b[9:] == [dest, cond, cond, -1] and b[8] == (dest if rival else origen)
                pruebas.append(dict(entrada=[origen, dest, cond, otra], pal=a, port=bv, iguales=iguales))
            datos[key] = pruebas
            filas[key] = 'igual_banco' if all(p['iguales'] for p in pruebas) else 'difiere'
    if 'dende' in grupos:
        for barras in (4, 5):
            pruebas = []
            for hp, cond, override in ((2501, 0, 1), (2501, 1, 1), (2501, 2, 1), (999, 1, 1), (2501, 1, 0), (500, 0, 1)):
                a = pal.dende(base, barras, hp, cond)
                args = ('dende', barras, hp, cond, 3000, override, 1900)

                def mide(exe):
                    return {f'{sw}/{n}': dict(zip(('barras', 'vida', 'override', 'banda_recargada', 'hp_peticion',
                                                   'hp_tras_54', 'banda_vida'), r, strict=True))
                            for sw, n, *r in numeros(exe, args, 9)}

                b = mide(integrada)
                off = mide(previa) if previa is not None else None
                if off is not None:
                    exigir(b['0/1'] == off['0/1'] and b['0/2'] == off['0/2'], 'cambio de serie inesperado respecto a --base')
                for n, lado in enumerate(('jugador', 'rival'), 1):
                    v = b[f'1/{n}']
                    orig = a[lado]
                    exigir(v['barras'] == orig['barras'] and v['vida'] == hp
                           and v['banda_vida'] == orig['banda_vida'] == v['banda_recargada'],
                           f'Dende: no conserva barra/HP/banda {barras}/{hp}/{cond}/{lado}')
                    if override or n == 1:
                        exigir(v['hp_peticion'] == orig['hp_peticion'], 'HP efectivo divergente tras Dende')
                    exigir(v['hp_tras_54'] == orig['hp_tras_54'], 'la banda post-Dende no se usa al preparar el siguiente HP')
                exigir(b['0/1']['vida'] == 0 and b['0/1']['override'] == -1 and b['0/1']['banda_vida'] == -1,
                       'el control con ajuste OFF no reprodujo el reset')
                fila = dict(entrada=args[1:], pal=a, integrado=b)
                if off is not None:
                    fila['base'] = off
                pruebas.append(fila)
            key = f'dende/{barras}'
            datos[key] = pruebas
            filas[key + '.vida_raw'] = filas[key + '.banda_vida'] = 'igual_banco_optativo'
    if 'dialogo' in grupos:
        cols = ('clip', 'cola', 'cerrando', 'siguiente', 'cursor', 'texto_a', 'texto_b')
        a, abre = (pal.dialogo(base, x) for x in (True, False))
        b, mismo, distinto = ([dict(zip(cols, r, strict=True)) for r in numeros(integrada, ('dialogo', x), 7)]
                              for x in (0, 1, 2))
        sec = pal.secuencia_301(base)
        native = sec['filas_rebasadas']
        nat_a = pal.dialogo(base, False, native)
        nat_b = [dict(zip(cols, r, strict=True))
                 for r in numeros(integrada, ('dialogo', 3, *(v for row in native for v in row)), 7)]
        exigir([x['panel'] for x in abre['frames']] == [0, 0, 1, 1, 1, 2, 2] and b[0]['clip'] == 0x33 and b[2]['clip'] == 0x34,
               'no se observaron los hitos de apertura esperados')
        exigir(abre['frames'][-1]['siguiente'] and b[3]['siguiente'] == 0 and b[7]['siguiente'] == 1,
               'no se reprodujo la diferencia de liberación')
        exigir(a['frames'][0]['panel'] == 3 and all(x['textos'] == [1, 2] for x in a['frames'])
               and mismo[0]['cola'] == 2 and distinto[0]['cola'] == distinto[3]['cola'] == 1 and distinto[3]['cursor'] == 1,
               'faltan controles de ventana abierta/mismo paso')
        solicitar = {row['hito']: row for row in nat_a['frames']}
        exigir(solicitar['39_sin_cruz']['lanzadas'] == 2 and solicitar['cruz_despacha_38']['panel'] == 2
               and solicitar['segundo_38_con_panel_abierto']['panel'] == 3 and nat_a['frames'][-1]['panel'] == 0
               and all(r['textos'] == native[0][2:4] for r in nat_a['frames'][2:])
               and [nat_b[7][k] for k in ('texto_a', 'texto_b')] == native[2][2:4],
               'no se reprodujo el contraste del segundo 38 nativo')
        datos['dialogo/abre'] = dict(pal=abre, port=b, tiempos_port=[1, 1.017, 1.05, 1.1, 1.2, 1.22, 1.25, 1.5, 1.6])
        datos['dialogo/abierto'] = dict(pal=a, port_mismo_paso=mismo, port_distinto_paso=distinto,
                                        secuencia=sec, pal_301=nat_a, port_301=nat_b)
        filas.update({'dialogo/abre.pasos': 'igual_hitos_apertura', 'dialogo/abre.termina': 'difiere',
                      'dialogo/abierto': 'difiere'})
    esperados = set(PENDIENTES_NB2I)
    meta_hist = None
    if historico is not None:
        hist = ev.read(historico)
        pendientes = {k for k, valor, _ in hist['filas'] if valor is None}
        exigir(hist['firma'] == 'eece4b41730f' and pendientes == esperados, 'IDs no corresponden a los pendientes NB2I')
        meta_hist = pal.o.sha(historico)
    exigir(set(filas) == {k for k in esperados if k.split('/')[0] in grupos}, 'IDs inesperados para los grupos pedidos')
    doc = dict(estado_pal=base.identity, fuente_cpp_sha256=hashlib.sha256(raw).hexdigest(),
               conteo=dict(Counter(filas.values())), filas=filas, evidencia=datos)
    if meta_hist is not None:
        doc['historico_sha256'] = meta_hist
    if base_cpp is not None:
        doc['base_cpp_sha256'] = pal.o.sha(base_cpp)
    ev.save(work / 'banco.json', doc)
    for k, v in sorted(filas.items()):
        print(f'{k}: {v}')
    return doc


def relevo(repo, work, ps2, log=None, ruta_cierre11=None):
    from nb2i import juzgar_cierre
    from b2_eventos import save
    from b2_oraculo.original import sha
    import tomllib
    from b2_escenario import texto

    py = [sys.executable, '-B', '-X', 'utf8']
    b2 = py + [repo / 'tools/b2.py']
    guion = work / 'NB2J_dende_ix.toml'
    checked(b2 + ['escenario', 'pm23', '--fase', '9', '--ps2-games', ps2, '--salida', guion], repo, work / 'saltador.log')
    d = tomllib.loads(guion.read_text(encoding='utf-8'))
    d['descripcion'] = 'NB2J: llegada y fin real del efecto de Dende IX. Fixture de juego: 500 HP / 4 barras.'
    d['env'] += ['DBZ3_TEST_B2_DENDE=1']
    cp = 'dbz3_b2_dende_espera=true;dbz3_b2_dende_vida_pal=true'
    d['env'] = [e + ';' + cp if e.startswith('DBZ3_TEST_CFG=') else e for e in d['env']]
    d['pasos'] += [r'log "dbz3 b2 state: .*prueba_dende_antes" 60',
                   'log "Dende termina y cura a" 20', 'esperar 250', 'captura dende_termina']
    final = work / 'NB2J_dende_efecto.toml'
    with final.open('x', encoding='utf-8') as f:
        f.write(texto(d))
    guiones = [final] + ([ruta_cierre11] if ruta_cierre11 is not None else [])
    out = checked(py + [repo / 'tools/pruebas.py', 'preflight', *guiones], repo, work / 'preflight.log')
    exigir(f'{len(guiones)} guiones, 0 avisos (0 obsoletos)' in out or
           (len(guiones) == 1 and '1 guion, 0 avisos (0 obsoletos)' in out) or
           (len(guiones) == 1 and '1 guiones, 0 avisos (0 obsoletos)' in out),
           'preflight no limpio')
    if log is None:
        print(f'preflight {len(guiones)}/{len(guiones)} sin avisos; guion en {final}')
        return None
    checked(b2 + ['cierre11', 'juzgar', '--log', log, '--exigir-alfa-pal'], repo, work / 'cierre11.log')
    r = juzgar_cierre(log.read_text(encoding='utf-8', errors='strict'), True)
    save(work / 'atribucion.json', dict(log_sha256=sha(log), **r))
    print(f'preflight {len(guiones)}/{len(guiones)} sin avisos; cierre11 rc=0:', r['clasificacion'], f"({r['estados']} estados)")
    return r


def juzgar_dende(log):
    """El log del efecto no basta: exige los valores de la instrumentación de Dende PAL."""
    filas = re.findall(r'Dende postefecto: pieza (\d+) barras (-?\d+) vida_b2 (-?\d+) banda (-?\d+)\b', log)
    if not filas:
        return 2, 'falta el diagnóstico postefecto del parche'
    if len(filas) != 1:
        return 2, 'la prueba breve debe tener exactamente un postefecto'
    pieza, barras, hp, banda = map(int, filas[0])
    return int(pieza < 1 or (barras, hp, banda) != (5, 500, 0)), f'pieza={pieza} barras={barras} vida_b2={hp} banda={banda}'


def autoprueba(repo):
    from b2_eventos import autoprueba as juez

    exigir(SONDA_TPL.is_file(), 'falta sonda.cpp junto a nb2j.py')
    exigir(len(PENDIENTES_NB2I) == 9, 'lista de IDs pendientes incompleta')
    juez()
    py = [sys.executable, '-B', '-X', 'utf8', repo / 'tools/b2.py']
    for cmd in ('escenario', 'cierre11'):
        print(checked(py + [cmd, '--self-check'], repo).strip())
    exigir(juzgar_dende('Dende termina y cura a Goku')[0] == 2, 'un efecto sin muestra pasó como HP medido')
    exigir(juzgar_dende('Dende postefecto: pieza 1 barras 5 vida_b2 -1 banda -1')[0] == 1, 'reset no detectado')
    exigir(juzgar_dende('Dende postefecto: pieza 1 barras 5 vida_b2 500 banda 0')[0] == 0, 'el control bueno no pasa')
    print('NB2J: autoprueba OK')


def main(argv=None):
    p = argparse.ArgumentParser(
        description=__doc__,
        epilog='Medir: 0 medición terminada (consulta las filas). Juzgar-dende: 0 igual, 1 diferencia, 2 faltan datos. 3 error.',
    )
    p.add_argument('--proyecto', type=Path, default=REPO)
    p.add_argument('--self-check', '--autoprueba', dest='self_check', action='store_true',
                   help='controles del juez y de las herramientas reutilizadas (sin abrir el juego)')
    sub = p.add_subparsers(dest='modo')

    m = sub.add_parser('medir', help='ejecutar banco PAL/port sobre el árbol integrado')
    m.add_argument('--estado', type=Path, required=True, help='savestate PAL fase9.p2s')
    m.add_argument('--salida', type=Path, help='carpeta nueva (por defecto crea temporal)')
    m.add_argument('--grupo', action='append', choices=GRUPOS, help='repetible; por defecto los tres')
    m.add_argument('--compilador', default='clang++')
    m.add_argument('--base', type=Path, help='src/pasarela_b2.cpp previo opcional para comprobar OFF == base')
    m.add_argument('--historico', type=Path, help='matriz.json NB2I opcional para comprobar firma e IDs')

    r = sub.add_parser('relevo', help='generar guion breve de Dende y opcionalmente juzgar un log de cierre 11')
    r.add_argument('--salida', type=Path, required=True, help='carpeta nueva')
    r.add_argument('--ps2-games', type=Path, default=Path('D:/DBZ3HD/ps2_games'))
    r.add_argument('--log', type=Path, help='dbz3.log opcional de cierre 11 para volver a juzgar')
    r.add_argument('--ruta-cierre11', type=Path, help='guion .toml de cierre 11 opcional para preflight conjunto')

    j = sub.add_parser('juzgar-dende', help='comprobar en un dbz3.log la vida y banda al terminar Dende')
    j.add_argument('--log', type=Path, required=True)

    a = p.parse_args(argv)
    if a.self_check == (a.modo is not None):
        p.error('elige --self-check o un comando')
    repo = a.proyecto.resolve()
    try:
        cargar(repo)
        if a.self_check:
            autoprueba(repo)
            return 0
        if a.modo == 'juzgar-dende':
            rc, msg = juzgar_dende(a.log.read_text(encoding='utf-8-sig'))
            print(msg)
            return rc
        if a.salida:
            work = a.salida.resolve()
            work.mkdir()
        else:
            work = Path(tempfile.mkdtemp(prefix='nb2j_'))
        print(work, flush=True)
        if a.modo == 'medir':
            medir(repo, work, a.estado.resolve(), a.compilador, a.grupo or GRUPOS,
                  a.base.resolve() if a.base else None,
                  a.historico.resolve() if a.historico else None)
        else:
            relevo(repo, work, a.ps2_games,
                   a.log.resolve() if a.log else None,
                   a.ruta_cierre11.resolve() if a.ruta_cierre11 else None)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as e:
        print(f'NB2J: {e}', file=sys.stderr)
        return 3
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
