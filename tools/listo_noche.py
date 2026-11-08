"""listo_noche.py: comprobacion de SOLO LECTURA de que todo esta listo para la bateria nocturna.

No lanza juego ni PCSX2, no copia, no borra, no quita cerrojos. Tabla OK / AVISO / FALLO en español.
Salida: 0 todo OK, 1 hay AVISO, 2 hay FALLO (bateria_noche.py se niega a correr con 2).
  python tools/listo_noche.py [--json]      python tools/listo_noche.py --autoprueba
"""
import csv
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / 'tools'))
D = Path(os.environ.get('DBZ3_PRUEBAS_DIR', r'D:\DBZ3HD'))
POOL = Path(os.environ.get('DBZ3_POOL_DIR', r'D:\DBZ3HD\_pcsx2_pool'))
ENTREGAS = RAIZ / 'studies 5' / 'ENTREGAS'
DLLS = ('rexruntime.dll', 'rexgpu-xenos.dll', 'amd_fidelityfx_dx12.dll')
BUILDS = ('win-amd64-dual', 'win-amd64-release')
COPIAS = ('smoke_a', 'smoke_b', 'smoke_c', 'smoke_d', 'smoke_e', 'smoke_f', 'rb_a', 'rb_b', 'opus', 'opus2', 'sol', 'eu_test2')
REGRESION = ('smoke_a', 'smoke_b', 'smoke_c', 'smoke_d', 'smoke_e', 'smoke_f', 'rb_a', 'rb_b', 'opus2', 'eu_test2')   # deben tener los valores de serie
COORD = Path(r'E:\DBZ3HD_trabajo\coord\compilando')
UMBRAL_GB = {'C': 15, 'D': 40, 'E': 20}   # AVISO por debajo
filas = []


def f(estado, tema, detalle):
    filas.append((estado, tema, detalle))


def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as fh:
        for b in iter(lambda: fh.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def comprobar_basico():
    f('OK' if sys.version_info >= (3, 8) else 'FALLO', 'Python', sys.version.split()[0])
    falta = [n for n in ('pruebas.py', 'pcsx2_pool.py', 'pcsx2_turnos.py', 'compilar.py', 'diferencial.py', 'bateria_noche.py',
                         'iw_mips/bubbles.py', 'iw_mips/freezer.py', 'verify_release.ps1') if not (RAIZ / 'tools' / n).exists()]
    f('FALLO' if falta else 'OK', 'Herramientas tools/', ('faltan: ' + ', '.join(falta)) if falta else 'todas presentes')


def comprobar_build():
    base = RAIZ / 'rexglue-sdk-0.10' / 'out' / 'win-amd64-baseline'
    esperados = {}
    try:
        txt = (RAIZ / 'tools' / 'verify_release.ps1').read_text(encoding='utf-8', errors='replace')
        for m in re.finditer(r'"([\w\-]+\.dll)"\s*=\s*(\d+)', txt):
            esperados[m.group(1)] = int(m.group(2))
        ver = re.search(r'\$canonVersion\s*=\s*"([^"]+)"', txt)
        f('OK', 'Version canonica (verify_release)', ver.group(1) if ver else '?')
    except OSError as e:
        f('FALLO', 'verify_release.ps1', str(e))
    hb = {}
    for d in DLLS:
        p = base / d
        if not p.exists():
            f('FALLO', 'DLL canonica ' + d, 'falta en baseline del SDK')
            continue
        hb[d] = sha(p)
        ok = esperados.get(d) in (None, p.stat().st_size)
        f('OK' if ok else 'AVISO', 'Tamano canonico ' + d, '%d B (esperado %s)' % (p.stat().st_size, esperados.get(d, '?')) + ('' if ok else ' - baseline recompilada tras verify_release; actualizar canonSizes'))
    exes = {}
    for b in BUILDS:
        carpeta = RAIZ / 'out' / 'build' / b
        exe = carpeta / 'dbz3.exe'
        if not exe.exists():
            f('FALLO', 'dbz3.exe ' + b, 'no existe')
            continue
        st = exe.stat()
        exes[b] = st.st_mtime
        f('OK', 'dbz3.exe ' + b, '%.1f MB, %s' % (st.st_size / 1e6, time.strftime('%d/%m %H:%M', time.localtime(st.st_mtime))))
        mal = [d for d in DLLS if d in hb and (not (carpeta / d).exists() or sha(carpeta / d) != hb[d])]
        f('FALLO' if mal else 'OK', 'DLL canonicas en ' + b, ('distintas/faltan: ' + ', '.join(mal)) if mal else 'SHA256 = baseline')
    if len(exes) == 2 and abs(exes[BUILDS[0]] - exes[BUILDS[1]]) > 3600:
        f('AVISO', 'Exe dual vs release', 'fechas distintas (>1 h): el release no sale del ultimo dual')
    if (ENTREGAS / '_compilando').exists():
        f('AVISO', 'Compilacion en curso', 'existe _compilando (tope de juegos baja a 1)')
    if COORD.exists():
        edad = (time.time() - COORD.stat().st_mtime) / 60
        f('AVISO', 'Cerrojo de compilacion', 'existe coord\\compilando (%.0f min; a los 20 se da por muerto)' % edad)


def comprobar_datos():
    pg = RAIZ / 'ps2_games'
    f('OK' if pg.exists() else 'FALLO', 'ps2_games (junction)', ('%s -> %s' % (pg, os.path.realpath(pg))) if pg.exists() else 'no resuelve')
    for ruta, tema in ((r'E:\DBZ3HD_trabajo\iw_eu\Dragon Ball Z - Infinite World (Europe).iso', 'ISO IW EU'),
                       (r'E:\DBZ3HD_trabajo\iw_usa\iw_usa.bin', 'ISO IW USA'),
                       (str(pg / 'Infinite World (USA)' / 'USR'), 'IW USA carpeta USR'),
                       (str(pg / 'DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso'), 'ISO B2 EU'),
                       (r'E:\DBZ3HD_trabajo\R7\emu\pcsx2_esc', 'Plantilla PCSX2'),
                       (r'E:\DBZ3HD_trabajo\dorados_noche', 'Capturas IW (dorados de freezer/piccolo/msn019/bubbles; D:\tmp se borro el 7/11)')):
        f('OK' if Path(ruta).exists() else 'FALLO', tema, ruta)


def comprobar_pool():
    try:
        import pcsx2_pool
    except Exception as e:  # noqa
        f('FALLO', 'pcsx2_pool', 'no importa: %s' % e)
        return
    for n in (1, 2, 3):
        p = POOL / str(n)
        if not (p / 'pcsx2.exe').exists():
            f('FALLO', 'Perfil pool %d' % n, 'sin pcsx2.exe en ' + str(p))
            continue
        tarj = [t for t in ('iw_usa100.ps2', 'b2_100.ps2') if not (p / 'memcards' / t).exists()]
        f('FALLO' if tarj else 'OK', 'Perfil pool %d' % n, ('faltan tarjetas: ' + ', '.join(tarj)) if tarj else 'pcsx2.exe + tarjetas iw_usa100 / b2_100')
        try:
            est = pcsx2_pool.estado_instancia(n)
        except Exception as e:  # noqa
            est = 'error ' + str(e)
        f('OK' if est == 'libre' else 'AVISO', 'Cerrojo pool_%d' % n, est + (' (huerfano: se aparta al reservar, no se quita aqui)' if 'muerto' in est else ''))
    for nombre, clave in (('iw_estados.json', 'estados'), ('b2_estados.json', 'escenas')):
        try:
            d = json.loads((RAIZ / 'tools' / 'dorados' / nombre).read_text(encoding='utf-8'))
        except Exception as e:  # noqa
            f('FALLO', 'dorados/' + nombre, str(e))
            continue
        faltan = [k for k, v in d.get(clave, {}).items() if isinstance(v, dict) and 'ruta' in v and not Path(v['ruta']).exists()]
        f('FALLO' if faltan else 'OK', 'Savestates ' + nombre, ('faltan %d: %s' % (len(faltan), ', '.join(faltan[:8]))) if faltan else '%d presentes' % len(d.get(clave, {})))
    j = ENTREGAS / '_juego'
    if j.exists():
        det = []
        for x in sorted(j.glob('plaza*')):
            try:
                det.append(x.name + ': ' + (x / 'quien.txt').read_text(encoding='utf-8', errors='replace').split('\n')[0][:70])
            except OSError:
                det.append(x.name + ': sin quien.txt (huerfana)')
        f('AVISO' if det else 'OK', 'Plazas de juego ocupadas', '; '.join(det) if det else 'ninguna')
    t = ENTREGAS / '_juego_tope.txt'
    f('OK', 'Tope de juegos', ('_juego_tope.txt: ' + t.read_text(encoding='utf-8', errors='replace').strip()[:30]) if t.exists() else 'por defecto (sin _juego_tope.txt)')


def comprobar_disco():
    for l, umbral in UMBRAL_GB.items():
        try:
            gb = shutil.disk_usage(l + ':\\').free / 2**30
        except OSError as e:
            f('FALLO', 'Disco ' + l, str(e))
            continue
        f('OK' if gb >= umbral else 'AVISO', 'Disco %s: libre' % l, '%.1f GB (aviso < %d GB)' % (gb, umbral))


def procesos():
    """(nombre, pid) de procesos vivos via tasklist (solo lectura)."""
    try:
        out = subprocess.run(['tasklist', '/FO', 'CSV', '/NH'], capture_output=True, text=True, timeout=30).stdout
    except Exception:  # noqa
        return None
    return [(r[0], r[1]) for r in csv.reader(io.StringIO(out)) if len(r) > 1]


def comprobar_procesos():
    ps = procesos()
    if ps is None:
        f('AVISO', 'Procesos', 'no se pudo ejecutar tasklist')
        return
    for nombre in ('dbz3.exe', 'pcsx2.exe', 'pcsx2-qt.exe', 'dbz3_bootstrap.exe'):
        pids = [p for n, p in ps if n.lower() == nombre]
        f('OK' if not pids else 'AVISO', 'Proceso ' + nombre, 'ninguno' if not pids else 'vivos: ' + ', '.join(pids) + ' (solo informe, no se tocan)')


def comprobar_copias():
    falta = [c for c in COPIAS if not (D / c).is_dir()]
    f('FALLO' if falta else 'OK', 'Copias fijas de pruebas', ('faltan: ' + ', '.join(falta)) if falta else '%d presentes en %s' % (len(COPIAS), D))
    uso = [x.name for x in D.glob('_en_uso_*')] if D.exists() else []
    if uso:
        f('AVISO', 'Copias marcadas _en_uso_', ', '.join(uso))
    for c in COPIAS[:6]:
        if (D / c).is_dir() and not (D / c / 'dbz3.exe').exists():
            f('AVISO', 'Copia %s' % c, 'sin dbz3.exe (hace falta preparar)')


def serie():
    """Valores de serie (los del codigo) de los dos interruptores que las copias de regresion no deben alterar."""
    r = {}
    try:
        s = (RAIZ / 'src' / 'launcher' / 'settings.cpp').read_text(encoding='utf-8', errors='replace')
        m = re.search(r'REXCVAR_DEFINE_STRING\(\s*dbz3_original_on\s*,\s*"([^"]*)"', s)
        r['dbz3_original_on'] = m.group(1) if m else None
        s = (RAIZ / 'src' / 'iw_mips.cpp').read_text(encoding='utf-8', errors='replace')
        m = re.search(r'REXCVAR_DEFINE_BOOL\(\s*dbz3_iw_logica_original\s*,\s*(true|false)', s)
        r['dbz3_iw_logica_original'] = m.group(1) if m else None
    except OSError:
        pass
    return r


def valor_toml(txt, clave):
    m = re.search(r'^\s*' + clave + r'\s*=\s*("([^"]*)"|[^\s#]+)', txt, re.M)
    return None if not m else (m.group(2) if m.group(2) is not None else m.group(1))


def comprobar_toml():
    """Las caches de pruebas no ven dbz3_user.toml: una copia con tokens o la logica original encendida falsea las pruebas."""
    s = serie()
    if None in s.values() or not s:
        f('AVISO', 'Valores de serie', 'no se pudieron leer de settings.cpp / iw_mips.cpp: ' + str(s))
        return
    f('OK', 'Valores de serie (codigo)', ', '.join('%s=%s' % kv for kv in sorted(s.items())))
    mal = []
    for c in REGRESION:
        tm = D / c / 'dbz3_user.toml'
        if not tm.exists():
            continue
        txt = tm.read_text(encoding='utf-8', errors='replace')
        for k, ser in s.items():
            v = valor_toml(txt, k)
            if v is not None and v != ser:
                mal.append('%s: %s=%s (serie %s)' % (c, k, v or '""', ser or '""'))
    f('FALLO' if mal else 'OK', 'toml de copias = valores de serie',
      ('distintos (reponer con DBZ3_TEST_CFG explicito, no a mano): ' + '; '.join(mal)) if mal else 'ninguna copia de regresion altera los interruptores')


def ejecutar():
    del filas[:]
    for fn in (comprobar_basico, comprobar_build, comprobar_datos, comprobar_pool, comprobar_disco, comprobar_procesos, comprobar_copias, comprobar_toml):
        try:
            fn()
        except Exception as e:  # noqa
            f('FALLO', fn.__name__, 'excepcion: %r' % e)
    return codigo(filas)


def codigo(fs):
    es = {x[0] for x in fs}
    return 2 if 'FALLO' in es else 1 if 'AVISO' in es else 0


def autoprueba():
    assert codigo([('OK', 'a', '')]) == 0 and codigo([('OK', 'a', ''), ('AVISO', 'b', '')]) == 1
    assert codigo([('AVISO', 'a', ''), ('FALLO', 'b', '')]) == 2
    assert len(sha(__file__)) == 64
    assert valor_toml('a = 1\ndbz3_original_on = "x,y"\n', 'dbz3_original_on') == 'x,y' and valor_toml('dbz3_iw_logica_original = false\n', 'dbz3_iw_logica_original') == 'false'
    assert valor_toml('nada = 1\n', 'dbz3_original_on') is None and valor_toml('dbz3_original_on = ""\n', 'dbz3_original_on') == ''
    print('AUTOPRUEBA listo_noche OK')


def main():
    if '--autoprueba' in sys.argv:
        return autoprueba()
    cod = ejecutar()
    if '--json' in sys.argv:
        print(json.dumps({'codigo': cod, 'filas': filas}, ensure_ascii=False))
    else:
        for e, t, d in filas:
            print('%-6s %-38s %s' % (e, t, d))
        print('\nResultado: %s (codigo %d)' % ({0: 'TODO OK', 1: 'CON AVISOS', 2: 'CON FALLOS'}[cod], cod))
    sys.exit(cod)


if __name__ == '__main__':
    main()
