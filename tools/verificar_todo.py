"""Puerta R23 sin juego: ocho verificaciones en serie y registros por corrida."""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from collections import Counter
from functools import cache
from hashlib import sha256
from pathlib import Path

AQUI = Path(__file__).resolve().parent
RAIZ = AQUI.parent
SALIDA = Path(r'E:\DBZ3HD_trabajo\fabrica\Q1\salida')
DATOS_PRUEBA = {
    'docs/14_frentes/escenarios_pruebas/modos_datos/v7_progreso_roto.toml',
}

# F7: instantánea Q1 20261122-082822.37200, confirmada en la corrida F7.
# Incluye ruta relativa, aviso íntegro y multiplicidad; cualquier cambio requiere revisión.
PREFLIGHT_F7 = (390, '3dc9fe9cb1d49c8f436402a37d02b7b617f646067809833045b0f11c10191ae5')


def buscador_preflight(pruebas):
    """Misma búsqueda que pruebas._esta_en_src; evita recorrer todo src por cada regex."""
    def contiene(lit, texto):
        literal = lit.encode('utf-8')
        if literal in texto:
            return True
        partes = pruebas._TOKEN.split(lit)
        patron = re.compile(''.join(re.escape(x) if i % 2 == 0 else
                                    f'(?:{pruebas._PH}|{re.escape(x)})'
                                    for i, x in enumerate(partes)).encode('utf-8'))
        ancla = max((x.encode('utf-8') for x in partes[::2]), key=len)
        if not ancla:
            if patron.search(texto):
                return True
        else:
            # Toda coincidencia contiene el ancla y mide como máximo len(literal).
            # Buscar sobre ventanas del texto original: no juntar líneas no contiguas.
            pos = texto.find(ancla)
            while pos != -1:
                if patron.search(texto, max(0, pos-len(literal)), pos+len(ancla)+len(literal)):
                    return True
                pos = texto.find(ancla, pos+1)
        pal = lit.rsplit(None, 1)
        if len(pal) == 2 and all(x.encode('utf-8') in texto for x in pal):
            return True
        pw = lit.split()
        return any(' '.join(pw[:i] + [pruebas._PH] + pw[i+n:]).encode('utf-8') in texto
                   for i in range(len(pw)) for n in (1, 2) if i or n < len(pw))

    @cache
    def fuentes_utf8():
        return pruebas._texto_fuentes().encode('utf-8')

    # La clave de caché es solo el literal para no hashear todo src por consulta.
    @cache
    def en_fuentes(lit):
        return contiene(lit, fuentes_utf8())

    def buscar(lit, texto):
        if texto is pruebas._texto_fuentes():
            return en_fuentes(lit)
        return contiene(lit, texto.encode('utf-8'))

    return buscar


def firma_preflight(avisos):
    datos = json.dumps(sorted(avisos), ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    return len(avisos), sha256(datos).hexdigest()


def informar_preflight(avisos, ruta_base, informe):
    error = None
    try:
        with ruta_base.open(encoding='utf-8') as f:
            filas = [json.loads(linea) for linea in f]
        if any(not isinstance(fila, list) or len(fila) != 2
               or not all(isinstance(x, str) for x in fila) for fila in filas):
            raise ValueError('se esperaba una pareja [ruta, aviso] por línea')
        if firma_preflight(filas) != PREFLIGHT_F7:
            raise ValueError('la lista no coincide con el inventario aprobado de F7')
        restantes = Counter(map(tuple, filas))
    except (OSError, ValueError) as e:
        error = f'Base de preflight inválida ({ruta_base}): {e}'
        restantes = Counter()

    heredados = nuevos = 0
    with informe.open('x', encoding='utf-8') as f:
        if error:
            print(error, file=f)
        for clave in avisos:
            if restantes[clave] > 0:
                restantes[clave] -= 1
                heredados += 1
            else:
                ruta, aviso = clave
                print(f'NUEVO {ruta}: {aviso}', file=f)
                nuevos += 1
        print(f'preflight: {nuevos} nuevos, {heredados} heredados, '
              f'{sum(restantes.values())} de la base no observados; registro completo: preflight.log', file=f)
    return 1 if nuevos or error else (3 if heredados else 0)


def preflight(inexistente=None, *, esperados_f7=False, ruta_base=None, informe=None):
    import pruebas

    pruebas._esta_en_src = buscador_preflight(pruebas)
    guiones = []
    for base in ('docs', 'studies 5', 'tools'):
        for p in (RAIZ / base).rglob('*.toml'):
            rel = p.relative_to(RAIZ)
            dirs = [x.lower() for x in rel.parts[1:-1]]
            if not any(x.startswith(('escenarios', 'pruebas_')) for x in dirs):
                continue
            if (any(x.startswith('fixture') for x in dirs) or rel.as_posix() in DATOS_PRUEBA
                    or '.local.inc' in p.name.lower()):
                print(f'DATO  {rel.as_posix()} (no es guion)')
            else:
                guiones.append(p)
    if not guiones:
        raise SystemExit('No se encontraron guiones TOML')
    guiones.sort()
    if inexistente is not None:
        guiones.insert(0, Path(inexistente))
    avisos = []
    obsoletos = {}
    for p in guiones:
        lista = pruebas.preflight_uno(p, obsoletos=obsoletos)
        ruta = p.relative_to(RAIZ).as_posix() if p.is_relative_to(RAIZ) else str(p)
        avisos.extend((ruta, aviso) for aviso in lista)
        if p in obsoletos:
            print(f'OBSOLETO {ruta}: {obsoletos[p]}')
        if lista or p not in obsoletos:
            print(f'{"AVISO" if lista else "ok   "} {ruta}')
        for aviso in lista:
            print(f'      - {aviso}')
    print(f'preflight: {len(guiones)} guiones, {len(avisos)} avisos ({len(obsoletos)} obsoletos)', flush=True)
    if ruta_base is not None:
        return informar_preflight(avisos, ruta_base, informe)
    if esperados_f7 and avisos and inexistente is None:
        cantidad, firma = firma_preflight(avisos)
        print(f'Firma de avisos: {firma}', flush=True)
        if (cantidad, firma) == PREFLIGHT_F7:
            return 3  # resultado interno: auditoría completa, deuda exacta de F7
    return int(bool(avisos))


def dorados():
    import comparar_modo as cm

    cm.autoprueba()
    iw = sorted(cm.DORADOS.glob('iw_0x7D*.json'))
    b2 = sorted(cm.DORADOS.glob('b2_fase*.json'))
    esperados = {f'iw_0x{i:X}.json' for i in range(0x7D0, 0x7E0)} | {
        f'b2_fase{i}.json' for i in range(1, 10)}
    faltan = esperados - {p.name for p in iw + b2}
    if faltan:
        print('Faltan dorados:', ', '.join(sorted(faltan)))
    fallos = bool(faltan)
    for p in iw + b2:
        data = json.loads(p.read_text(encoding='utf-8'))
        modo, escena = p.stem.split('_', 1)
        if p.name == 'b2_fase1_lista.json' and data.get('secuencia') == []:
            # Referencia auxiliar de la lista de aliados: solo tiene capturas.
            capturas = data.get('capturas', [])
            errores = [f'captura {i}: falta el PNG de referencia o la etiqueta del port'
                       for i, c in enumerate(capturas, 1)
                       if not c.get('original') or not Path(c['original']).is_file() or not c.get('port')]
            if not capturas:
                errores.append('sin capturas')
            print(f'{p.name}: referencia visual auxiliar; {len(capturas)} capturas declaradas')
        else:
            errores = cm.validar_original(modo, escena, data)
        if errores:
            print(p.name, errores)
            fallos = True
    return int(fallos)


def comprobar_cac():
    import cac_nativas_verificar as cac
    import vista_heroe as vh

    # El hijo exige que --salida no exista. Crear su scratch al abrir el AFS,
    # cuando el hijo ya ha creado la salida; el comprobador conserva sus asserts.
    abrir_afs = vh.rb.Afs

    def afs_con_scratch(origen, trabajo):
        Path(trabajo).mkdir(exist_ok=True)
        return abrir_afs(origen, trabajo)

    vh.rb.Afs = afs_con_scratch
    try:
        cac.main()
    finally:
        vh.rb.Afs = abrir_afs


def main():
    inicio = time.monotonic()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--salida', type=Path, default=SALIDA)
    ap.add_argument('--provocar-fallo', action='store_true', help='Añade un guion inexistente a preflight')
    ap.add_argument('--esperados-f7', action='store_true',
                    help='Acepta solo los avisos heredados del preflight F7; no equivale a PASS')
    ap.add_argument('--preflight-base', type=Path, metavar='JSONL',
                    help='Muestra solo avisos nuevos de preflight; conserva todos en preflight.log')
    args = ap.parse_args()
    dest = (args.salida / f'{time.strftime("%Y%m%d-%H%M%S")}.{os.getpid()}').resolve()
    dest.mkdir(parents=True)
    temp = dest / 'tmp'
    temp.mkdir()
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONUTF8='1', TMP=str(temp), TEMP=str(temp),
               TMPDIR=str(temp), DBZ3_TMP=str(temp))
    env.pop('PYTHONOPTIMIZE', None)
    env['DBZ3_AISLAMIENTO_DIR'] = str(temp / 'aisl')
    env['DBZ3_COLA_DIR'] = str(temp / 'cola')
    fallo = [str(dest / '__guion_inexistente_r23__.toml')] if args.provocar_fallo else []
    informe = dest / 'preflight_nuevos.log'
    if args.preflight_base is not None:
        argv_preflight = ['-c', 'import sys; from pathlib import Path; from verificar_todo import preflight; '
                          'sys.exit(preflight(*sys.argv[3:], ruta_base=Path(sys.argv[1]), informe=Path(sys.argv[2])))',
                          str(args.preflight_base.resolve()), str(informe), *fallo]
    else:
        argv_preflight = ['-c', 'import sys; from verificar_todo import preflight; '
                          'sys.exit(preflight(*sys.argv[2:], esperados_f7=sys.argv[1] == "1"))',
                          str(int(args.esperados_f7)), *fallo]
    checks = [
        ('eu_map', 'eu_map.py', ['--comprobar']),
        ('b2_oraculo', 'b2_oraculo/banco.py', ['--autoprueba', '--salida', str(dest / 'oraculo.json')]),
        ('b2_juez_bateria', 'b2_juez_bateria.py', ['--salida', str(dest / 'juez')]),
        ('preflight', 'pruebas.py', argv_preflight),
        ('comparar_modo', 'comparar_modo.py', ['-c', 'from verificar_todo import dorados; raise SystemExit(dorados())']),
        ('test_pruebas', 'test_pruebas.py', []),
        ('tanda', 'tanda.py', ['--autoprueba']),
        ('cac_nativas_verificar', 'cac_nativas_verificar.py', ['-c', 'from verificar_todo import comprobar_cac; comprobar_cac()',
                                                             '--salida', str(dest / 'cac')]),
    ]
    print(f'Registros: {dest}', flush=True)
    print(f'{"Verificación":25} {"Estado":7} {"s":>7}  Detalle', flush=True)
    fallos = ejecutados = pendientes = 0
    for i, (nombre, archivo, argv) in enumerate(checks):
        t = time.monotonic()
        ruta = AQUI / archivo
        log = dest / f'{nombre}.log'
        if not ruta.is_file():
            estado, detalle = 'FAIL', f'Falta verificación obligatoria: {ruta}'
        else:
            ejecutados += 1
            comando = argv if argv[:1] == ['-c'] else [str(ruta), *argv]
            try:
                # Reserva para que un preflight lento no se coma las demás comprobaciones.
                restante = 118 - (t - inicio) - 3 * (len(checks) - i - 1)
                if restante <= 0:
                    raise subprocess.TimeoutExpired(comando, 118)
                with log.open('xb') as f:
                    # Preflight escribe cientos de líneas: agrupar E/S en lugar de volcar cada línea.
                    modo_io = [] if nombre == 'preflight' else ['-u']
                    r = subprocess.run([sys.executable, '-B', *modo_io, *comando], stdout=f,
                                       stderr=subprocess.STDOUT, cwd=RAIZ, timeout=restante,
                                       env=dict(env, PYTHONPATH=str(AQUI)))
                estado, detalle = ('OK' if r.returncode == 0 else 'FAIL'), f'código {r.returncode}'
                if nombre == 'b2_juez_bateria':
                    match = re.search(r'^RESUMEN: (\d+) fases OK, (\d+) con FAIL, (\d+) PENDIENTE_DATO',
                                      log.read_text(encoding='utf-8', errors='replace'), re.M)
                    if match:
                        ok, mal, pendiente = match.groups()
                        detalle = f'{ok}/9 OK, {mal} FAIL, {pendiente} PENDIENTE_DATO (código {r.returncode})'
                        if match.groups() != ('9', '0', '0'):
                            estado = 'FAIL'
                    elif r.returncode == 0:
                        estado, detalle = 'FAIL', 'El juez no emitió un resumen reconocible'
                if args.preflight_base is not None and nombre == 'preflight':
                    if informe.is_file():
                        print(informe.read_text(encoding='utf-8'), end='', flush=True)
                        if r.returncode == 3:
                            estado, detalle = 'ESPERADO', 'Solo avisos heredados de la base F7 (no PASS)'
                            pendientes += 1
                    else:
                        estado, detalle = 'FAIL', 'Sin informe de preflight; consultar preflight.log'
                elif args.esperados_f7 and nombre == 'preflight' and r.returncode == 3:
                    estado, detalle = 'ESPERADO', '390 avisos heredados coinciden por ruta, texto y multiplicidad'
                    pendientes += 1
            except subprocess.TimeoutExpired:
                estado, detalle = 'FAIL', 'Sin veredicto: tiempo agotado (presupuesto de 118 s para la serie)'
            except OSError as e:
                estado, detalle = 'FAIL', f'Sin veredicto: {e}'
        fallos += estado == 'FAIL'
        print(f'{nombre:25} {estado:7} {time.monotonic()-t:7.2f}  {detalle}', flush=True)
    print(f'Total: {time.monotonic()-inicio:.2f} s; {fallos} fallos; {pendientes} esperados (no PASS); {len(checks)-ejecutados} no ejecutadas por archivo ausente')
    return int(bool(fallos))


if __name__ == '__main__':
    sys.exit(main())
