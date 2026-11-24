# SPDX-License-Identifier: MIT
"""Coteja focos de Infinite World sin lanzar el juego ni PCSX2."""
import argparse
import contextlib
import io
import json
import math
import re
import subprocess
import sys
import tempfile
from functools import lru_cache
from pathlib import Path
from types import SimpleNamespace

from iw_censo import AFS, CURSOS, RAIZ

import comparar as cmp
import disparo_banco as banco
import persecucion as p7dd

import _iw_cotejo_focos as focos
import _iw_cotejo_neon as neon

AUTO = tuple(k for k, v in CURSOS.items() if v[3] in {'focal', 'regresion', 'diagnostico'})
ESTADOS = {'coincide': 0, 'difiere': 1, 'sin_datos': 2, 'error': 3}


def normalizar(s):
    s = s.strip().upper()
    clave = s if s in ('MAPA', 'ROAD') else '0x' + s.removeprefix('0X')
    if clave not in CURSOS:
        raise argparse.ArgumentTypeError(f'curso desconocido: {s!r}')
    return clave


def cotejar_filas(o, p):
    """No permitir éxito por ticks, campos, o entradas ausentes en comparar.primer_dif."""
    if not o or not p:
        raise ValueError('traza vacía')
    claves = set(o[0]) if isinstance(o[0], dict) else set()
    salidas = {k for k in claves if k != 'i' and not k.startswith('in_')}
    if 'i' not in claves or not salidas or not any(k.startswith('in_') for k in claves):
        raise ValueError('JSONL requiere i, al menos un in_* y al menos un campo de salida')
    for lado in (o, p):
        if any(not isinstance(r, dict) or set(r) != claves or type(r['i']) is not int for r in lado):
            raise ValueError('esquema o tick incompatible')
        if len({r['i'] for r in lado}) != len(lado):
            raise ValueError('ticks duplicados')
        if any(type(v) not in (bool, int, float, str) or (type(v) is float and not math.isfinite(v))
               for r in lado for v in r.values()):
            raise ValueError('solo se admiten escalares finitos')
    a, b = ({r['i']: r for r in lado} for lado in (o, p))
    if set(a) != set(b):
        raise ValueError('las dos trazas deben contener exactamente los mismos ticks')
    if any(a[i][k] != b[i][k] for i in a for k in claves if k.startswith('in_')):
        raise ValueError('las trazas no tienen las mismas entradas')
    return cmp.primer_dif([a[i] for i in sorted(a)], [b[i] for i in sorted(b)])


@lru_cache(maxsize=1)
def rangos(elf_eu, elf_us):
    for path, r in ((elf_eu, 'eu'), (elf_us, 'us')):
        if banco.sha256(path) != banco.ELF_HASH[r]:
            raise ValueError(f'ELF {r} incompatible con el banco de rangos: {path}')
    with contextlib.redirect_stdout(io.StringIO()):
        return banco.rangos(SimpleNamespace(elf_eu=elf_eu, elf_us=elf_us))['perfiles']


def externo(script, *args):
    return subprocess.run([sys.executable, '-B', '-X', 'utf8', str(RAIZ / 'tools' / script), *map(str, args)],
                          capture_output=True, encoding='utf-8', timeout=180)


def ladrillos(orig, port):
    # Astra guarda el informe en la carpeta port. Se le da una copia temporal del único CSV que lee.
    with tempfile.TemporaryDirectory(prefix='iw_ladrillos_') as tmp:
        import shutil
        shutil.copyfile(port / 'visor.csv', Path(tmp) / 'visor.csv')
        r = externo('astra_iw_ladrillos_cotejo.py', orig.resolve(), tmp)
    if r.returncode and 'AssertionError: divergencia original/port' not in r.stderr:
        raise RuntimeError(r.stderr.strip() or f'Astra rc={r.returncode}')
    d = json.loads(r.stdout)
    if d['campos'] <= 0:
        raise ValueError('Astra no cotejó ningún cuadro')
    return dict(alcance='Subconjunto visor: PAL y port capturado con dtPAL de prueba.', coincide=not r.returncode, astra=d)


def ejecutar(curso, a):
    if a.trazas:
        x, y = a.trazas
        return dict(alcance='JSONL aportados por el usuario; la identidad del curso/región no se deduce de los datos.',
                    comparacion=cotejar_filas(cmp.leer_jsonl(x), cmp.leer_jsonl(y)), orig=str(x), port=str(y))
    if a.gs:
        r = externo('iw_mips/comparar_trad.py', *a.gs, *(['--datos', a.gs_datos] if a.gs_datos else []))
        coincidencias = re.search(r'cuadros: oraculo y juego en comun (\d+)', r.stdout)
        if not coincidencias or not int(coincidencias[1]):
            raise ValueError(r.stderr.strip() or 'GS sin cuadros comparables')
        if r.returncode not in (0, 1) or not re.search(r'VEREDICTO (PASS|FAIL)', r.stdout):
            raise RuntimeError(r.stderr.strip() or 'comparar_trad no terminó con veredicto')
        return dict(alcance='GS/VU1 exclusivamente', coincide=r.returncode == 0, salida=r.stdout)
    if a.log_7dd:
        tot, malas, _ = p7dd.prueba_juego(a.log_7dd)
        if not tot.get('llamadas'):
            raise ValueError('log sin llamadas 7DD al espejo')
        return dict(alcance='Log del espejo 7DD en el host vs EE; no compara la rama de respaldo C++.',
                    coincide=not tot.get('DIF'), resumen=dict(tot), primeras=malas[:6])
    if curso == '0x7D3' and a.ladrillos:
        return ladrillos(*a.ladrillos)
    if curso == '0x7DD':
        return neon.persecucion(a.cuadros, a.wcf, a.rayo)
    if curso == '0x7DE':
        return neon.cell(a.elf_eu)
    if curso in ('MAPA', 'ROAD'):
        return neon.mapa(curso == 'ROAD', a.afs)
    if curso == '0x7D1':
        o, p, kw = cmp.reg_7d1()
        return dict(alcance='Regresión de IA Bubbles: dorado PCSX2 vs EE; no rama C++.', comparacion=cmp.primer_dif(o, p, **kw))
    if curso in ('0x7D7', '0x7D8'):
        r = [v for v in rangos(a.elf_eu, a.elf_us) if v['clave'] == curso[2:]]
        if not r or any(v['iguales'] != v['total'] for v in r):
            raise RuntimeError('banco de rangos no satisface su contrato EE')
        return dict(alcance='Clasificador de rango EU/USA en seis dificultades (no esquiva ni escena).',
                    coincide=all(v['port_iguales'] == v['total'] for v in r),
                    perfiles=[{k: v for k, v in x.items() if k != 'casos'} for x in r])
    if CURSOS[curso][3] == 'focal':
        return focos.cotejar(curso, a.elf_eu, a.afs)
    raise ValueError('requiere --ladrillos, --trazas o --gs' if curso == '0x7D3' else 'requiere --trazas o --gs')


def medir(curso, a):
    try:
        if curso == '0x7DD' and not (a.trazas or a.gs or a.log_7dd):
            for path in (a.wcf, a.rayo):
                if not path.is_file():
                    raise FileNotFoundError(path)
        r = ejecutar(curso, a)
        ok = r.get('coincide', r.get('cumple', r.get('comparacion', {}).get('ok')))
        if type(ok) is not bool:
            raise RuntimeError('sin veredicto contrastable')
        estado = 'coincide' if ok else 'difiere'
    except (FileNotFoundError, ValueError) as exc:
        r, estado = {'motivo': str(exc)}, 'sin_datos'
    except (OSError, RuntimeError, KeyError, TypeError, subprocess.SubprocessError) as exc:
        r, estado = {'motivo': str(exc)}, 'error'
    return dict(curso=curso, modo='trazas' if a.trazas else 'gs' if a.gs else 'log_7dd' if a.log_7dd else CURSOS[curso][3],
                estado=estado, **r)


def autoprueba():
    x = [{'i': 0, 'in_mando': 0, 'rango': 1}]
    if not cotejar_filas(x, x)['ok'] or cotejar_filas(x, [dict(x[0], rango=2)])['ok']:
        raise RuntimeError('control de coincidencia/divergencia falló')
    for y in ([], x * 2, [{'i': 0, 'in_mando': 0}], [dict(x[0], i=1)], [dict(x[0], in_mando=2)]):
        try:
            cotejar_filas(x, y)
        except ValueError:
            continue
        raise RuntimeError(f'traza inválida aceptada: {y}')
    print('iw_cotejo: autoprueba OK')


def main():
    p = argparse.ArgumentParser(description=__doc__, epilog='Salida: 0 coincide, 1 difiere, 2 sin datos/uso, 3 error. '
                                'Un diagnóstico o cotejo focal solo certifica el alcance indicado.')
    sel = p.add_mutually_exclusive_group(required=True)
    sel.add_argument('curso', nargs='?', type=normalizar, help='0x7D0..0x7E1, MAPA o ROAD')
    sel.add_argument('--listar', action='store_true', help='mostrar alcance por clave')
    sel.add_argument('--todos', action='store_true', help='ejecutar los bancos automáticos')
    sel.add_argument('--autoprueba', '--self-check', dest='autoprueba', action='store_true', help='verificar el comparador sin binarios')
    tipo = p.add_mutually_exclusive_group()
    tipo.add_argument('--trazas', nargs=2, type=Path, metavar=('ORIG_JSONL', 'PORT_JSONL'))
    tipo.add_argument('--gs', nargs=2, type=Path, metavar=('ORACULO', 'VOLCADO'))
    tipo.add_argument('--ladrillos', nargs=2, type=Path, metavar=('DIR_ORIG', 'DIR_PORT'))
    tipo.add_argument('--log-7dd', type=Path, help='comparar las llamadas al espejo en un log 7DD')
    p.add_argument('--json', type=Path, help='guardar salida estructurada')
    p.add_argument('--gs-datos', type=Path, help='datos de GS, para --gs')
    p.add_argument('--elf-eu', type=Path, default=banco.ELF_EU, help='SLES PAL para los focos de botones, vuelo y disparo')
    p.add_argument('--elf-us', type=Path, default=banco.ELF_US, help='SLUS USA para los clasificadores 7D7/7D8')
    p.add_argument('--afs', type=Path, default=RAIZ / AFS, help='DATA_CMN.AFS para mapas y perfiles de vuelo')
    p.add_argument('--wcf', type=Path, default=p7dd.WCF, help='colisión 907/0 para 7DD')
    p.add_argument('--rayo', type=Path, default=p7dd.RAYO_EXE, help='colisionador local rayo7dd para 7DD')
    p.add_argument('--cuadros', type=int, default=120, help='número de pasos PAL en el foco 7DD (1..2000)')
    a = p.parse_args()
    if (any((a.trazas, a.gs, a.ladrillos, a.log_7dd)) and not a.curso or
            a.ladrillos and a.curso != '0x7D3' or a.log_7dd and a.curso != '0x7DD' or a.gs_datos and not a.gs):
        p.error('modo e inputs incompatibles')
    if not 1 <= a.cuadros <= 2000:
        p.error('--cuadros debe estar entre 1 y 2000')
    if a.autoprueba:
        return autoprueba()
    if a.listar:
        for clave, (nombre, _, _, cobertura) in CURSOS.items():
            print(f'{clave:5} {cobertura:12} {nombre}')
        return 0
    resultados = [medir(c, a) for c in (AUTO if a.todos else (a.curso,))]
    for r in resultados:
        print(f"{r['curso']} [{r['modo']}] {r['estado']}: {r.get('alcance') or r['motivo']}")
        if 'comparacion' in r:
            print(cmp.informe(r['comparacion']))
    if a.json:
        a.json.write_text(json.dumps(resultados if a.todos else resultados[0], ensure_ascii=False,
                                    allow_nan=False, indent=2) + '\n', encoding='utf-8')
    return max(ESTADOS[r['estado']] for r in resultados)


if __name__ == '__main__':
    raise SystemExit(main())
