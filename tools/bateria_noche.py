"""bateria_noche.py: bateria nocturna SECUENCIAL (un juego cada vez), reanudable, con registro por paso.

  python tools/bateria_noche.py --listar                 pasos, grupos, timeouts
  python tools/bateria_noche.py --simular [--solo iw]    imprime los comandos, no ejecuta nada
  python tools/bateria_noche.py [--solo offline|iw|b2|regresion|cierre] [--desde PASO] [--hasta-hora 09:45] [--rehacer]
  python tools/bateria_noche.py --autoprueba             test del parser y del fichero de estado (sin juego)

Salida en E:\\DBZ3HD_trabajo\\tmp\\noche\\<fecha>\\ : <paso>.log, estado.json, resumen.md. "Fecha" = dia de la noche (hora-12 h), asi
una noche que cruza la medianoche reutiliza la misma carpeta. Un paso PASS en estado.json se salta SOLO si se midio con el
mismo exe (sha12 de out\\build\\win-amd64-release\\dbz3.exe, guardado en cada entrada); con otro exe se repite. --rehacer repite todo.
Reglas: no corre si listo_noche.py da 2; --forzar solo en el grupo cierre; un juego a la vez; al agotar el tiempo mata
SOLO el arbol de procesos que lanzo (taskkill /T de su propio PID). Estados: PASS, FAIL, DIF (la comparacion sale 1 =
hay diferencia con el original, no es una regresion), COLA (se agoto el tiempo esperando turno de juego), SALTADO.
Reparto de la noche: N5/orquestador usan offline, regresion (04:25) y cierre (09:00). Los grupos iw y b2 usan las copias
smoke_d/smoke_e de N4/N3: solo si N4 no arranca. Esta herramienta no reserva slots de la granja (adquirir no sirve: su dueno
seria el shell de la llamada, que muere).
"""
import argparse
import datetime as dt
import json
import os
import hashlib
import re
import subprocess
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
PY = sys.executable
GRUPOS = ('offline', 'iw', 'b2', 'regresion', 'cierre')
CLAVES = ['0x%X' % c for c in range(0x7D0, 0x7E2)]   # 18 minijuegos IW
BASE = Path(os.environ.get('DBZ3_NOCHE_DIR', r'E:\DBZ3HD_trabajo\tmp\noche'))
LOG_B2 = Path(r'E:\DBZ3HD_trabajo\R7\emu\logs\b2_traza_ordenes.log')
CMP_B2 = RAIZ / 'studies 5' / 'ENTREGAS' / 'A2' / 'herramientas' / 'comparar_b2.py'   # comparar_modo.py solo sabe b2 fase1
EXE = RAIZ / 'out' / 'build' / 'win-amd64-release' / 'dbz3.exe'


def P(*a):
    return [PY] + list(a)


# Familia de cada clave: iw_minijuego solo da PASS con aros (espera 'aro N cogido'); Bubbles/Piccolo piden npc=1;
# el disparo (0x7D3/7D7/7D8/7DE) lo redirige el propio iw_minijuego; botones (iw_mini_botones.cpp) y vuelo
# (iw_mini_vuelo.cpp) tienen su escenario en pruebas.py.
BOTONES = {'0x7D4', '0x7D5', '0x7DA', '0x7DB', '0x7DC', '0x7E1'}
VUELO = {'0x7D6', '0x7E0'}
NPC = {'0x7D1', '0x7DD'}


def args_iw(c):
    if c in BOTONES:
        return ['_r14_botones', '--par', 'clave=' + c] + (['--par', 'seg=420'] if c == '0x7DA' else [])   # NB012: 10 rondas
    if c in VUELO:
        return ['_r12_vuelo', '--par', 'clave=' + c]
    return ['iw_minijuego', '--par', 'clave=' + c] + (['--par', 'npc=1'] if c in NPC else [])


def paso(id, grupo, cmd, timeout, ok=None, juego=False, requiere=None, dif=False):
    """ok = regex que debe aparecer en la salida ademas del codigo 0; requiere = ruta que debe existir o el paso se salta;
    dif = comparacion cuyo codigo 1 significa 'hay diferencia' (estado DIF, no FAIL)."""
    return dict(id=id, grupo=grupo, cmd=cmd, timeout=timeout, ok=ok, juego=juego, requiere=requiere, dif=dif)


def pasos():
    t = 'tools/'
    pr = t + 'pruebas.py'
    L = []
    # --- offline: ni juego ni PCSX2 (comprobado: bubbles/freezer/diferencial solo leen ficheros y ejecutan ee.py)
    L += [paso('off_listo', 'offline', P(t + 'listo_noche.py', '--autoprueba'), 60, 'AUTOPRUEBA listo_noche OK'),
          paso('off_pool', 'offline', P(t + 'pcsx2_pool.py', '--autoprueba'), 120, 'AUTOPRUEBA pcsx2_pool OK'),
          paso('off_diferencial', 'offline', P(t + 'diferencial.py', '--autoprueba'), 300, 'AUTOPRUEBA OK'),
          paso('off_freezer', 'offline', P(t + 'iw_mips/freezer.py', 'dorado'), 300, r'"llamadas"'),
          paso('off_bubbles', 'offline', P(t + 'iw_mips/bubbles.py', 'comprobar'), 1800, r'autocomprobacion OK'),
          paso('off_b2_log', 'offline', P(t + 'comparar_modo.py', 'b2', 'fase1', '--log', str(LOG_B2)), 300, requiere=LOG_B2, dif=True)]
    # --- iw: 18 minijuegos x EU (copia fija EU, region=eu) y USA (copia N1 smoke_d)
    # 7/11: _r12_vuelo ya tiene _prep_iw_minijuego, asi que el vuelo EU vuelve a la lista (18 + 18).
    for c in CLAVES:
        L.append(paso('iw_eu_' + c[2:], 'iw', P(pr, 'correr', 'eu_test2', *args_iw(c), '--par', 'region=eu'), 900, juego=True))
    for c in CLAVES:
        L.append(paso('iw_usa_' + c[2:], 'iw', P(pr, 'correr', 'smoke_d', *args_iw(c)), 900, juego=True))
    for c in ('0x7D1', '0x7D7'):
        L.append(paso('iw_cmp_' + c[2:], 'iw', P(t + 'comparar_modo.py', 'iw', c, '--copia', 'smoke_d'), 900, juego=True, dif=True))
    # --- b2: tablero (dorados fase1..9), nave de Babidi (no existe comparar_nave: se usa r2_nave), zona B
    # fase1: comparar_modo.py; fase2..9: ESCENA_PORT de comparar_modo.py solo tiene fase1 (N4 lo ampliara) -> comparar_b2.py port N
    L.append(paso('b2_cmp_fase1', 'b2', P(t + 'comparar_modo.py', 'b2', 'fase1', '--copia', 'smoke_e'), 1200, juego=True, dif=True))
    for n in range(2, 10):
        L.append(paso('b2_cmp_fase%d' % n, 'b2', P(str(CMP_B2), 'port', str(n), '--copia', 'smoke_e'), 1200, juego=True, dif=True))
    for n in range(4):
        L.append(paso('b2_nave_%d' % n, 'b2', P(pr, 'correr', 'smoke_e', 'r2_nave', '--par', 'prueba=%d' % n), 1200, juego=True))
    L.append(paso('b2_zonaB', 'b2', P(pr, 'escena', 'b2:fase1|b2:fase3|b2:fase5|b2:fase9', '--copias', 'smoke_e'), 2400, juego=True))
    # --- regresion: una pregunta por prueba, sin --forzar (cache)
    for id, args in (('tag_swap', ['smoke_a', 'tag_swap', '--par', 'rafaga=4', '--instantanea', 'freezer_cap2']),
                     ('tactics', ['smoke_a', 'tactics_partida', '--par', 'n=1']),
                     ('iw_mapa', ['smoke_a', 'iw_mapa']),
                     ('b1_satan_r1', None),
                     ('import_fusion', ['smoke_f', 'fusion_nativa', '--par', 'ids=13,7,2', '--par', 'caps=597']),
                     ('import_bu', ['smoke_f', 'bu_absorbe']),
                     ('satan', ['smoke_f', 'satan']),
                     ('heroe', ['smoke_a', 'heroe']),
                     ('menu', ['smoke_a', 'menu']),
                     ('batalla', ['smoke_a', 'batalla', '--instantanea', 'freezer_cap2'])):
        cmd = P(pr, 'escena', 'satan:ronda1', '--copias', 'smoke_a') if args is None else P(pr, 'correr', *args)   # B1: escena de Mr. Satan (r1_historia es de IW)
        L.append(paso('reg_' + id, 'regresion', cmd, 1200, juego=True))
    # --- cierre: lo unico que usa --forzar; ultimo
    L.append(paso('cierre_todo', 'cierre', P(pr, 'todo', '--max', '3', '--forzar'), 3000, juego=True))
    L.append(paso('cierre_online', 'cierre', P(pr, 'online-rb', '--copias', 'rb_a,rb_b', '--seg', '40', '--combates', '1', '--lockstep'), 4500, juego=True))
    return L


def seleccionar(L, solo=None, desde=None):
    if solo:
        L = [p for p in L if p['grupo'] == solo]
    if desde:
        ids = [p['id'] for p in L]
        if desde not in ids:
            raise SystemExit('paso desconocido: %s (usa --listar)' % desde)
        L = L[ids.index(desde):]
    return L


def clasificar(codigo, salida, ok=None, dif=False):
    """PASS/FAIL (o DIF si dif y sale 1 sin traceback) a partir del codigo de salida y, si se pide, de una regex que debe aparecer."""
    if codigo != 0 and 'HERRAMIENTA:' in salida:
        return 'HERRAMIENTA'          # T1B: la grabacion del original o el banco fallaron; no es un fallo del juego
    if dif and codigo == 1 and 'Traceback (most recent call last)' not in salida:
        return 'DIF'
    if codigo != 0 or 'Traceback (most recent call last)' in salida:
        return 'FAIL'
    if ok and not re.search(ok, salida):
        return 'FAIL'
    return 'PASS'


def exe_sha12():
    """sha256[:12] del exe de release (vacio si no existe): una entrada PASS solo vale con el mismo exe."""
    try:
        h = hashlib.sha256()
        with open(EXE, 'rb') as fh:
            for b in iter(lambda: fh.read(1 << 22), b''):
                h.update(b)
        return h.hexdigest()[:12]
    except OSError:
        return ''


def vale_pass(e, exe):
    """True si la entrada de estado.json es un PASS medido con este mismo exe."""
    return e.get('estado') == 'PASS' and e.get('exe', '') == exe


def calcular_limite(hhmm, ahora=None):
    """datetime de la proxima ocurrencia de HH:MM (si ya paso hoy, manana: asi 23:50 + 09:45 apunta al dia siguiente)."""
    ahora = ahora or dt.datetime.now()
    h, m = map(int, hhmm.split(':'))
    lim = ahora.replace(hour=h, minute=m, second=0, microsecond=0)
    return lim + dt.timedelta(days=1) if lim <= ahora else lim


def carpeta_noche(fecha=None):
    return BASE / (fecha or (dt.datetime.now() - dt.timedelta(hours=12)).strftime('%Y%m%d'))


def leer_estado(d):
    try:
        return json.loads((d / 'estado.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return {}


def guardar_estado(d, est):
    d.mkdir(parents=True, exist_ok=True)
    tmp = d / 'estado.json.tmp'
    tmp.write_text(json.dumps(est, ensure_ascii=False, indent=1), encoding='utf-8')
    os.replace(tmp, d / 'estado.json')


def resumen(d, L, est):
    ln = ['# Bateria nocturna %s' % d.name, '', 'Actualizado %s' % dt.datetime.now().strftime('%d/%m %H:%M'), '',
          '| Paso | Grupo | Estado | Seg | Salida |', '|---|---|---|---|---|']
    for p in L:
        e = est.get(p['id'], {})
        ln.append('| %s | %s | %s | %s | %s.log |' % (p['id'], p['grupo'], e.get('estado', 'pendiente'), e.get('seg', ''), p['id']))
    c = {}
    for e in est.values():
        c[e.get('estado')] = c.get(e.get('estado'), 0) + 1
    ln += ['', 'Totales: ' + ', '.join('%s %d' % kv for kv in sorted(c.items(), key=lambda x: str(x[0])))]
    (d / 'resumen.md').write_text('\n'.join(ln) + '\n', encoding='utf-8')


def matar_arbol(pid):
    """Mata SOLO el proceso que lanzamos y sus hijos."""
    subprocess.run(['taskkill', '/F', '/T', '/PID', str(pid)], capture_output=True)


def ejecutar_paso(p, d, tiempo=None):
    """tiempo = segundos que quedan hasta --hasta-hora (recorta el limite del paso)."""
    if p['requiere'] and not Path(p['requiere']).exists():
        return 'SALTADO', 0, 'falta ' + str(p['requiere'])
    log = d / (p['id'] + '.log')
    t0 = time.time()
    limite = p['timeout'] if tiempo is None else max(60, min(p['timeout'], int(tiempo)))
    env = dict(os.environ, PYTHONIOENCODING='utf-8', DBZ3_AGENTE='bateria_noche')
    with open(log, 'w', encoding='utf-8', errors='replace') as fh:
        fh.write('$ %s\n# inicio %s timeout %ds\n\n' % (' '.join(p['cmd']), dt.datetime.now().isoformat(timespec='seconds'), limite))
        fh.flush()
        pr = subprocess.Popen(p['cmd'], cwd=str(RAIZ), stdout=fh, stderr=subprocess.STDOUT, env=env)
        try:
            codigo = pr.wait(timeout=limite)
        except subprocess.TimeoutExpired:
            if pr.poll() is None:            # carrera: si acaba de terminar, su PID podria reutilizarse
                matar_arbol(pr.pid)
            pr.wait()
            fh.write('\n# TIMEOUT: arbol del PID %d terminado\n' % pr.pid)
            fh.flush()
            txt = log.read_text(encoding='utf-8', errors='replace')
            en_cola = bool(re.search(r'en cola', txt)) and p['juego']
            return ('COLA' if en_cola else 'FAIL'), int(time.time() - t0), 'timeout' + (' esperando turno de juego (no es un fallo de la prueba)' if en_cola else '')
    txt = log.read_text(encoding='utf-8', errors='replace')
    return clasificar(codigo, txt, p['ok'], p['dif']), int(time.time() - t0), 'exit %d' % codigo


def listo():
    return subprocess.run(P(str(RAIZ / 'tools' / 'listo_noche.py'), '--json'), capture_output=True, text=True, cwd=str(RAIZ)).returncode


def autoprueba():
    L = pasos()
    ids = [p['id'] for p in L]
    assert len(ids) == len(set(ids)), 'ids repetidos'
    assert {p['grupo'] for p in L} == set(GRUPOS)
    assert sum(p['id'].startswith('iw_eu_') for p in L) == 18 and sum(p['id'].startswith('iw_usa_') for p in L) == 18
    assert len(BOTONES | VUELO | NPC) == 10 and set(CLAVES) >= BOTONES | VUELO | NPC
    cmd = {p['id']: p['cmd'] for p in L}
    assert 'npc=1' in cmd['iw_usa_7D1'] and 'npc=1' in cmd['iw_eu_7DD'] and 'npc=1' not in cmd['iw_usa_7D0']
    assert '_r14_botones' in cmd['iw_usa_7E1'] and 'seg=420' in cmd['iw_eu_7DA'] and 'region=eu' in cmd['iw_eu_7DB']
    assert '_r12_vuelo' in cmd['iw_usa_7D6'] and 'iw_minijuego' in cmd['iw_usa_7D7']
    assert '_r12_vuelo' in cmd['iw_eu_7D6'] and 'region=eu' in cmd['iw_eu_7E0']
    assert all('--forzar' not in p['cmd'] for p in L if p['grupo'] != 'cierre'), '--forzar fuera de cierre'
    assert sum('--forzar' in p['cmd'] for p in L) == 1
    assert sum(p['juego'] for p in L if p['grupo'] == 'offline') == 0
    assert [p['id'] for p in seleccionar(L, 'cierre')] == ['cierre_todo', 'cierre_online']
    sub = seleccionar(L, 'iw', 'iw_usa_7D0')
    assert sub[0]['id'] == 'iw_usa_7D0' and all(p['grupo'] == 'iw' for p in sub)
    assert clasificar(0, 'ok', None) == 'PASS' and clasificar(1, 'ok') == 'FAIL'
    assert clasificar(0, 'x Traceback (most recent call last)') == 'FAIL' and clasificar(0, 'nada', 'OK') == 'FAIL' and clasificar(0, 'bien OK', 'OK') == 'PASS'
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        d = Path(td) / 'n'
        assert leer_estado(d) == {}
        est = {'off_listo': {'estado': 'PASS', 'seg': 3}, 'iw_eu_7D0': {'estado': 'FAIL', 'seg': 9}}
        guardar_estado(d, est)
        assert leer_estado(d) == est
        resumen(d, L, est)
        r = (d / 'resumen.md').read_text(encoding='utf-8')
        assert '| off_listo | offline | PASS |' in r and 'PASS 1' in r and 'FAIL 1' in r
    assert carpeta_noche('20261107').name == '20261107'
    assert clasificar(2, 'HERRAMIENTA: grabacion no valida', None, True) == 'HERRAMIENTA'
    assert clasificar(1, 'hay diferencia', None, True) == 'DIF' and clasificar(1, 'x', None, False) == 'FAIL'
    assert clasificar(1, 'Traceback (most recent call last)', None, True) == 'FAIL'
    assert vale_pass({'estado': 'PASS', 'exe': 'aaa'}, 'aaa') and not vale_pass({'estado': 'PASS', 'exe': 'aaa'}, 'bbb') and not vale_pass({'estado': 'PASS'}, 'aaa')
    n = dt.datetime(2026, 11, 6, 23, 50)
    assert calcular_limite('09:45', n) == dt.datetime(2026, 11, 7, 9, 45) and calcular_limite('23:59', n) == dt.datetime(2026, 11, 6, 23, 59)
    assert all(not p['cmd'][-1].endswith('r1_historia') for p in L) and not any(p['id'] == 'reg_b1_historia' for p in L)
    assert sum(p['id'].startswith('b2_cmp_fase') for p in L) == 9
    print('AUTOPRUEBA bateria_noche OK (%d pasos)' % len(L))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--listar', action='store_true')
    ap.add_argument('--simular', action='store_true')
    ap.add_argument('--autoprueba', action='store_true')
    ap.add_argument('--solo', choices=GRUPOS)
    ap.add_argument('--desde')
    ap.add_argument('--hasta-hora', help='HH:MM: no empieza pasos nuevos a partir de esa hora')
    ap.add_argument('--rehacer', action='store_true', help='repite tambien los pasos ya PASS (sin esto, un PASS solo se salta si el exe es el mismo)')
    ap.add_argument('--fecha', help='AAAAMMDD de la carpeta de la noche')
    a = ap.parse_args()
    if a.autoprueba:
        return autoprueba()
    L = seleccionar(pasos(), a.solo, a.desde)
    if a.listar:
        for p in L:
            print('%-18s %-10s %5ds %s %s' % (p['id'], p['grupo'], p['timeout'], 'juego' if p['juego'] else 'offline', 'dif' if p['dif'] else ''))
        return print('%d pasos' % len(L))
    if a.simular:
        for p in L:
            print('[%s] %s' % (p['id'], ' '.join(p['cmd'])))
        return print('(simulacion: no se ha ejecutado nada)')
    if listo() == 2:
        raise SystemExit('listo_noche.py da FALLO (codigo 2): no se ejecuta la bateria. Revisa: python tools/listo_noche.py')
    d = carpeta_noche(a.fecha)
    d.mkdir(parents=True, exist_ok=True)
    est = leer_estado(d)
    limite = calcular_limite(a.hasta_hora) if a.hasta_hora else None
    exe = exe_sha12()
    for p in L:
        if not a.rehacer and vale_pass(est.get(p['id'], {}), exe):
            print('[%s] ya PASS con este exe (%s), se salta' % (p['id'], exe))
            continue
        if limite and dt.datetime.now() >= limite:
            print('hora limite alcanzada: se para antes de', p['id'])
            break
        print('[%s] %s' % (p['id'], ' '.join(p['cmd'])), flush=True)
        e, seg, nota = ejecutar_paso(p, d, (limite - dt.datetime.now()).total_seconds() if limite else None)
        if e == 'HERRAMIENTA':   # una sola repeticion automatica de los fallos de herramienta
            print('   -> HERRAMIENTA, se repite una vez', flush=True)
            e, seg, nota = ejecutar_paso(p, d, (limite - dt.datetime.now()).total_seconds() if limite else None)
            nota += ' (repetido 1 vez)'
        est[p['id']] = dict(estado=e, seg=seg, nota=nota, exe=exe, fin=dt.datetime.now().isoformat(timespec='seconds'))
        guardar_estado(d, est)
        resumen(d, L, est)
        print('   -> %s (%d s, %s)' % (e, seg, nota), flush=True)
    sys.exit(0 if all(est.get(p['id'], {}).get('estado') in ('PASS', 'SALTADO') for p in L) else 1)


if __name__ == '__main__':
    main()
