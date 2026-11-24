"""NB2I: regenera el cotejo de eventos, prepara capturas cortas y separa cierre dirigido de orden 42."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tomllib

sys.dont_write_bytecode = True
from sondas import construir, medir_premio

REPO = Path(__file__).resolve().parents[2]

PENDIENTES = {
    'combate/salida-jugador': 'Falta medir la casilla destino de +0x0A tras el desplazamiento del jugador y el despacho a HD con el mismo contacto/T4. El fixture PAL 10/12 hace distintos los dos destinos; no equivale a r.casilla en el port.',
    'combate/salida-rival': 'Falta medir el destino de +0x0A tras EmpezarPaso(rival) y el despacho a HD con T4 sin guion. El modo 7 PAL y el retorno 2 del port pertenecen a interfaces distintas.',
    'dende/4.banda_vida': 'PAL escribe 2 al recalcular +0x1F; el port no conserva un byte equivalente. El consumidor de vida rival deriva banda de vida_max, con otra semántica.',
    'dende/5.banda_vida': 'Con 5 barras PAL también recalcula +0x1F=2; saturar barras no preserva ni sustituye esa banda. El port no tiene el byte.',
    'dende/4.vida_raw': 'PAL conserva 2501; CurarDende fija vida=0, vida_b2=-1 (vida llena, sin override). Falta validar su efecto en el siguiente combate con entradas equivalentes.',
    'dende/5.vida_raw': 'PAL conserva 2501 incluso a 5 barras; el port también borra su override en esta rama. Falta verificar la vida efectiva del consumidor HD al reentrar.',
    'dialogo/abierto': 'PAL entra con panel en estado 2; en C++ un mensaje en cola bloquea el siguiente opcode. Forzar Orden(38) con cola ocupada omite la guarda. Falta emparejar la ventana realmente abierta con un siguiente 38 alcanzable.',
    'dialogo/abre.pasos': 'Los pasos 0/1/2 PAL dependen de disponibilidad y estado de panel; la cola C++ usa un reloj y D_CLEAR. Hacen falta hitos equivalentes, no comparar numeración interna.',
    'dialogo/abre.termina': 'El fin de tarea PAL está puenteado; el port retira la cola según animación/entrada y puede esperar una orden 39. Falta traza de retiro y reanudación del mismo hilo.',
}
PREMIO = ('grupo', 'mascara', 'stock', 'termina', 'ventana')


def cargar_modulos(repo):
    if not (repo / 'tools/b2.py').is_file():
        raise ValueError('pasa --proyecto con la raíz del proyecto')
    sys.path[:0] = [str(repo / 'tools'), str(repo / 'tools/b2_nb2c')]


def exigir(valor, mensaje):
    if not valor:
        raise ValueError(mensaje)


def conteo(filas):
    c = Counter(f[1] for f in filas)
    return dict(ok=c[True], fail=c[False], pendiente=c[None])


def juzgar_cierre(txt, exigir_alfa=False):
    import b2_cierre11 as cierre
    import b2_juez as juez
    d = cierre.juzgar(txt, exigir_alfa)
    jsonl = list(re.finditer(r'dbz3 b2 state: (\{[^\r\n]+\})[ \t]*$', txt, re.M))
    # El prefijo de log es del port. El campo op puede seguir siendo 42 en
    # escena11 sin que ESA fila sea un despacho de la orden.
    docs = [(m.start(), json.loads(m[1])) for m in jsonl]
    exigir(txt.count('dbz3 b2 state: {') == len(docs), 'línea de estado truncada o ilegible')
    exigir(all(x.get('formato') == 'b2-state-v1' and x.get('fuente') == 'port' for _, x in docs),
           'volcado de cierre con formato o fuente inesperados')
    cortes = list(re.finditer(r'tablero b2: CIERRES escena 11\b(?! ->)', txt))
    ordenes = list(re.finditer(r'tablero b2: script 303: orden 42\b', txt))
    saltos = list(re.finditer(r'tablero b2: prueba CIERRES dirigida: 11\b', txt))
    naturalezas = []
    for pos, doc in docs:
        if (doc.get('fase') == 9 and doc.get('hito') == 'cierre' and doc.get('script', {}).get('op') == 42):
            exigir(not juez.validar_volcado(doc), 'volcado de orden 42 inválido')
            if doc['script'].get('id') == 303:
                naturalezas.append((pos, doc))
    # No se certifica un orden natural: aun con el hito, EVENTO=303 puede ser inyectado.
    firma = 'sin_atribucion'
    if len(cortes) == 1:
        if (len(naturalezas) == len(ordenes) == 1 and not saltos
                and ordenes[0].start() < naturalezas[0][0] < cortes[0].start()):
            firma = 'orden_42_observada'
        elif len(saltos) == 1 and saltos[0].start() < cortes[0].start() and not (naturalezas or ordenes):
            firma = 'salto_dirigido'
    return dict(**d, clasificacion=firma, estados=len(docs), despachos_42=[doc for _, doc in naturalezas])


def rutas(repo, salida, ps2, fases, snapshot):
    import b2_escenario as esc
    import b2_cierre11 as cierre
    from b2_eventos import save
    from b2_oraculo.original import sha
    salida.mkdir()
    rutas = []

    def escribir(nombre, d):
        texto = esc.texto(d)
        exigir(tomllib.loads(texto) == d and d['turbo'] is False, f'guion inválido: {nombre}')
        ruta = salida / f'{nombre}.toml'
        ruta.write_text(texto, encoding='utf-8', newline='\n')
        rutas.append(ruta)

    for fase in fases:
        escribir(f'pm23_carga_f{fase}', esc.guion('pm23', fase, ps2))
    dirigido = cierre.guion(ps2, True)
    dirigido['env'] += ['DBZ3_TEST_B2_STATE=1', 'DBZ3_TEST_B2_ENTRADA=S9_NB2I_salto11']
    dirigido['pasos'].insert(3, r'log "dbz3 b2 state: .*\x22hito\x22:\x22escena11\x22" 45')
    escribir('cierre11_dirigido', dirigido)

    evento = cierre.guion(ps2, True)
    evento['descripcion'] = 'NB2I: guion 303 inyectado sobre snapshot NB2G, valida el despacho de la orden 42'
    evento['env'] = [e for e in evento['env'] if not e.startswith('DBZ3_TEST_B2_CIERRE=')]
    evento['env'] = [e.replace('1,auto,reset,desde=8', '1,auto,desde=8') for e in evento['env']]
    evento['env'] += ['DBZ3_TEST_B2_EVENTO=303', 'DBZ3_TEST_B2_STATE=1', 'DBZ3_TEST_B2_ENTRADA=S9_NB2I_op42']
    evento['pasos'][1:1] = ['log "tablero b2: partida restaurada: fase 9" 90',
                              'log "tablero b2: script 303: orden 42" 90',
                              r'log "dbz3 b2 state: .*\x22hito\x22:\x22cierre\x22.*\x22op\x22:42" 15']
    escribir('cierre11_orden42', evento)
    (salida/'progreso_orden42.toml').write_bytes(snapshot.read_bytes())
    save(salida/'snapshot.json', dict(origen=str(snapshot), sha256=sha(salida/'progreso_orden42.toml'),
                                      alcance='instantánea dirigida NB2G; no acredita una partida natural'))

    dende = esc.guion('pm23', 9, ps2)
    dende['descripcion'] = 'NB2I: Dende dirigido IX, volcados antes y después de solicitar el movimiento'
    dende['env'] = [e for e in dende['env'] if not e.startswith('DBZ3_TEST_B2_ENTRADA=')]
    dende['env'] += ['DBZ3_TEST_B2_DENDE=1', 'DBZ3_TEST_B2_ENTRADA=S9_NB2I_dende']
    dende['pasos'] += [r'log "dbz3 b2 state: .*prueba_dende_antes" 90',
                       r'log "dbz3 b2 state: .*prueba_dende_despues" 15', 'captura dende']
    escribir('dende_f9', dende)

    premio = esc.guion('pm23', 1, ps2)
    premio['descripcion'] = 'NB2I: inyecta T6/62 (orden 8); premio real sin forzar ID'
    premio['env'] = [e for e in premio['env'] if not e.startswith('DBZ3_TEST_B2_ENTRADA=')]
    premio['env'] += ['DBZ3_TEST_B2_EVENTO=62', 'DBZ3_TEST_B2_ORDENES=1', 'DBZ3_TEST_B2_ENTRADA=S1_NB2I_premio']
    premio['pasos'] += ['log "tablero b2: script 62: orden 8 \\(grupo 35," 90',
                        'log "tablero b2: prueba: premio visible" 30', 'captura premio',
                        'log "tablero b2: orden 8/70: ventana cerrada" 30']
    escribir('premio8_f1', premio)

    salida_pre = subprocess.run([sys.executable, '-B', '-X', 'utf8', str(repo/'tools/pruebas.py'),
                                'preflight', *map(str, rutas)], cwd=repo, capture_output=True,
                                encoding='utf-8', timeout=90)
    texto = salida_pre.stdout + salida_pre.stderr
    (salida/'preflight.log').write_text(texto, encoding='utf-8')
    print(texto, end='')
    exigir(salida_pre.returncode == 0 and f'preflight: {len(rutas)} guiones, 0 avisos' in texto,
           'preflight no limpio')


def medir(repo, salida, estado, compilador, casos):
    import b2_juez as juez
    import b2_eventos as ev
    from pal import cases, o
    from cpp import collect as cpp, fragment, run
    nuevos = {k for k in cases() if k.startswith('premio/')}
    exigir(set(casos) <= cases().keys(), 'hay un identificador de caso desconocido')
    salida.mkdir()
    pal = ev.collect(estado, casos)
    base = cpp(repo, pal['casos'], 'serie', compilador, salida)
    filas_base = ev.judge(pal, base, casos)
    ev.save(salida/'PAL.json', pal)
    ev.save(salida/'CPP-base.json', base)
    from copy import deepcopy
    port = deepcopy(base)
    activos = sorted(nuevos & pal['casos'].keys())
    if activos:
        b2txt, navetxt = [(repo / f'src/{file}.cpp').read_text(encoding='utf-8-sig')
                           for file in ('pasarela_b2', 'pasarela_nave')]
        sonda = salida/'sonda_premio.cpp'
        sonda.write_text(construir(b2txt, navetxt, fragment), encoding='utf-8')
        exe = salida/'sonda_premio.exe'
        run([compilador, '-std=c++20', str(sonda), '-o', str(exe)], salida)
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith('DBZ3_TEST_')}

        def ejecutar(args):
            p = subprocess.run([str(exe), *map(str, args)], cwd=salida, env=env, capture_output=True,
                               check=True, encoding='utf-8', timeout=10)
            nums = list(map(int, p.stdout.split()))
            exigir(len(nums) == 9, 'formato de la sonda de premio inesperado')
            return nums

        ref = o.Original(estado)
        port['metodo'] = 'fragmentos_cpp_nb2c_y_nb2i'
        port['identidad']['sonda_premio_sha256'] = o.sha(sonda)
        for k in activos:
            row = medir_premio(o, ref, pal['casos'][k], ejecutar)
            exigir(base['casos'][k]['valor']['stock_final'] == row['valor']['stock_final'],
                   'el arnés extendido contradice el stock final de NB2C')
            port['casos'][k] = row
        # Negativo: retirar el bit de Goku impide seleccionar la ficha 17 incluso con grupo correcto.
        negativo = ejecutar((70, 35, 17, 0, 2, 1 << 34, 0, 1, 1))
        exigir(negativo[:3] == [35, 0, 0] and negativo[5:] == [0, 0, 1, 0],
               'el selector no excluyó al personaje bloqueado')
        ev.save(salida/'negativo_premio.json', negativo)
    ev.save(salida/'CPP-extendido.json', port)
    filas = ev.judge(pal, port, casos)
    actuales = {k: (ok, d) for k, ok, d in filas}
    seguimiento = []
    for key, ok, _ in filas_base:
        if ok is not None:
            continue
        valor, detalle = actuales[key]
        if valor is None:
            exigir(key in PENDIENTES, f'pendiente nuevo o sin diagnóstico: {key}')
            motivo = PENDIENTES[key]
        else:
            exigir(any(key == f'{k}.{p}' for k in activos for p in PREMIO), f'fila resuelta no autorizada: {key}')
            motivo = 'Cotejo focal con #SKA e inventario de banco, ventana lógica C++ y servicios aislados; no prueba visual/persistencia.'
        seguimiento.append(dict(id=key, veredicto=valor, detalle=detalle, alcance=motivo))
    ev.save(salida/'matriz.json', dict(base=conteo(filas_base), nuevo=conteo(filas),
           firma_base=juez.firma(filas_base), firma=juez.firma(filas), filas=filas, seguimiento=seguimiento))
    print(f'NB2C de serie: {conteo(filas_base)} -> NB2I focal: {conteo(filas)}', flush=True)
    return juez.imprimir(filas)


def self_check():
    import b2_escenario
    import b2_cierre11
    import b2_eventos
    import b2_juez
    b2_escenario.selfcheck()
    b2_cierre11.self_check()
    b2_eventos.autoprueba()
    exigir(b2_juez.autoprueba() == 0, 'falló b2_juez')
    exigir(juzgar_cierre('CIERRES escena 11')['clasificacion'] == 'sin_atribucion', 'falso cierre')
    dirigido = 'tablero b2: prueba CIERRES dirigida: 11\ntablero b2: CIERRES escena 11 (orden 42)'
    exigir(juzgar_cierre(dirigido)['clasificacion'] == 'salto_dirigido', 'el rótulo de escena no prueba el opcode')
    escena = 'dbz3 tablero b2: CIERRES escena 11 (orden 42)'
    despacho = 'dbz3 tablero b2: script 303: orden 42, fase 9 completada'
    doc = dict(formato='b2-state-v1', fuente='port', fase=9, evento=10, hito='cierre', piezas=[],
               script=dict(id=303, op=42))
    estado = 'dbz3 b2 state: ' + json.dumps(doc)
    exigir(juzgar_cierre('\n'.join((despacho, estado, escena)))['clasificacion'] == 'orden_42_observada',
           'no reconoció el despacho observado en orden')
    exigir(juzgar_cierre('\n'.join((despacho, escena, estado)))['clasificacion'] == 'sin_atribucion',
           'aceptó un estado posterior a la escena')
    print('NB2I: autoprueba OK')


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, epilog='0 OK en el alcance; 1 diferencias; 2 pendiente; 3 error.')
    p.add_argument('--proyecto', type=Path, default=REPO)
    p.add_argument('--self-check', action='store_true')
    sub = p.add_subparsers(dest='cmd')
    m = sub.add_parser('medir', help='ejecuta A=ELF PAL y B=C++ en un banco focal')
    m.add_argument('--salida', required=True, type=Path, help='carpeta nueva')
    m.add_argument('--estado', required=True, type=Path, help='savestate PAL IX compatible')
    m.add_argument('--compilador', default='clang++')
    m.add_argument('--caso', action='append', default=[], help='ID NB2C exacto; repetible')
    r = sub.add_parser('rutas', help='genera TOML a x1 y preflight; jamás ejecuta el juego')
    r.add_argument('--salida', required=True, type=Path, help='carpeta nueva')
    r.add_argument('--ps2-games', type=Path, default=Path('D:/DBZ3HD/ps2_games'))
    r.add_argument('--snapshot', required=True, type=Path, help='progreso IX validado por NB2G')
    r.add_argument('--fase', type=int, choices=range(1, 10), action='append', help='PM-23: repetible; por defecto las nueve')
    j = sub.add_parser('cierre', help='coteja un dbz3.log ya existente y clasifica su origen')
    j.add_argument('--log', required=True, type=Path)
    j.add_argument('--exigir-alfa-pal', action='store_true')
    j.add_argument('--salida', type=Path)
    a = p.parse_args(argv)
    if a.self_check and a.cmd:
        p.error('--self-check no admite subcomando')
    try:
        repo = a.proyecto.resolve()
        cargar_modulos(repo)
        if a.self_check:
            self_check()
        elif a.cmd == 'medir':
            return medir(repo, a.salida.resolve(), a.estado.resolve(), a.compilador, a.caso)
        elif a.cmd == 'rutas':
            exigir(not any(ord(c) < 32 or ord(c) == 127 for c in str(a.ps2_games)), 'ruta PS2 inválida')
            rutas(repo, a.salida.resolve(), a.ps2_games, sorted(set(a.fase or range(1, 10))), a.snapshot.resolve())
        elif a.cmd == 'cierre':
            from b2_eventos import save
            v = juzgar_cierre(a.log.read_text(encoding='utf-8-sig', errors='replace'), a.exigir_alfa_pal)
            save(a.salida, v)
            return v['rc']
        else:
            p.print_help()
            return 2
        return 0
    except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f'NB2I: {exc}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    sys.exit(main())
