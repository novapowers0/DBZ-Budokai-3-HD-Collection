# SPDX-License-Identifier: MIT
"""Cotejo IW: bancos PAL vs C++ compilado, con acceso a los modos de HIWB."""
import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
RAIZ = Path(__file__).resolve().parents[2]
AJUSTES = {'serie': -1, 'on': 1, 'off': 0}


def conectar(raiz):
    if not (raiz / 'tools' / 'iw_cotejo.py').is_file():
        raise ValueError('--raiz debe apuntar al árbol del proyecto')
    sys.path[:0] = [str(raiz / 'tools'), str(raiz / 'tools' / 'iw_mips')]
    import iw_cotejo as legado
    import _focos as focos
    return legado, focos


def medir(raiz, elf, cxx, focos, elegidos, ajustes):
    from _sonda_cpp import abrir
    sha = focos.hiwb.imagen(elf)[1]
    resultados = []
    with ExitStack() as dirs:
        sonda = abrir(raiz, cxx, dirs)
        for ajuste in ajustes:
            for foco, (cursos, productor) in focos.FOCOS.items():
                activos = [c for c in cursos if (c, foco) in elegidos]
                if not activos:
                    continue
                # Un banco compartido no se ejecuta ni se cuenta varias veces.
                r = productor(activos[0], elf, sonda, AJUSTES[ajuste])
                resultados.append(dict(cursos=activos, foco=foco, ajuste=ajuste, region='PAL', elf_sha256=sha,
                                        cpp_sha256=sonda.sha, sonda_sha256=sonda.codigo_sha256,
                                        valores_de_serie=sonda.defectos, **r))
    return resultados


def autoprueba(legado, focos):
    legado.autoprueba()
    entrada = [dict(boton=14)]
    for b, esperado in (([(1,)], True), ([(0,)], False)):
        r = focos.comparar(entrada, [(1,)], b, ('acierto',))['comparacion']
        if r['ok'] != esperado or (not esperado and r['primer_tick'] != 0):
            raise RuntimeError('control del comparador')
    for b in ([], [()], [(None,)], [(1,), (1,)]):
        try:
            focos.comparar(entrada, [(1,)], b, ('acierto',))
        except ValueError:
            continue
        raise RuntimeError('se aceptó cobertura vacía, desalineada o incompleta')
    print('NIWB: autoprueba OK')


def main():
    p = argparse.ArgumentParser(description=__doc__, epilog='0: coincide; 1: difiere; 2: faltan datos/uso; 3: error.')
    grupo = p.add_mutually_exclusive_group(required=True)
    grupo.add_argument('curso', nargs='?', help='ej. 7D5, un curso del lote')
    grupo.add_argument('--todos', action='store_true', help='ejecuta todos los focos NIWB')
    grupo.add_argument('--listar', action='store_true', help='enumera cursos y focos nuevos')
    grupo.add_argument('--autoprueba', '--self-check', dest='autoprueba', action='store_true', help='controles del comparador sin ELF ni compilador')
    grupo.add_argument('--legado', nargs=argparse.REMAINDER, metavar='ARG', help='pasa el resto de argumentos a iw_cotejo.py de HIWB')
    p.add_argument('--foco', help='restringe a un foco de --listar')
    p.add_argument('--ajuste', choices=(*AJUSTES, 'todos'), default='serie',
                   help='usa el valor declarado en C++ o fuerza el ajuste del foco; todos incluye los tres casos')
    p.add_argument('--raiz', type=Path, default=RAIZ, help='raíz del proyecto (por defecto: donde está esta herramienta)')
    p.add_argument('--elf-eu', type=Path, help='SLES PAL con el SHA-256 del banco HIWB')
    p.add_argument('--cxx', default='g++', help='compilador de las sondas; por defecto g++')
    p.add_argument('--json', type=Path, help='ruta nueva para la evidencia JSON')
    a = p.parse_args()
    try:
        if a.legado is not None:
            raiz = a.raiz.resolve()
            return subprocess.run([sys.executable, '-B', '-X', 'utf8', str(raiz / 'tools' / 'iw_cotejo.py'),
                                   *a.legado], cwd=raiz, timeout=240).returncode
        legado, focos = conectar(a.raiz.resolve())
        if a.autoprueba:
            autoprueba(legado, focos)
            return 0
        if a.listar:
            for nombre, (cursos, _) in focos.FOCOS.items():
                print(f'{nombre}: {", ".join(cursos)}')
            return 0
        curso = legado.normalizar(a.curso) if a.curso else None
        elegidos = {(c, n) for n, (cs, _) in focos.FOCOS.items() for c in cs
                    if (not curso or c == curso) and (not a.foco or n == a.foco)}
        if not elegidos:
            p.error('sin foco NIWB compatible; consulte --listar o --legado --help')
        ajustes = tuple(AJUSTES) if a.ajuste == 'todos' else (a.ajuste,)
        res = medir(a.raiz.resolve(), a.elf_eu or focos.banco.ELF_EU, a.cxx, focos, elegidos, ajustes)
        for r in res:
            print(focos.cmp.informe(r['comparacion'], f'{r["ajuste"]} {r["foco"]} {"/".join(r["cursos"])}'))
        if a.json:
            with a.json.open('x', encoding='utf-8') as f:
                json.dump(dict(formato='iw-niwb-v2', resultados=res), f, ensure_ascii=False, allow_nan=False, indent=2)
                f.write('\n')
        return int(not all(r['comparacion']['ok'] for r in res))
    except (FileNotFoundError, ValueError, argparse.ArgumentTypeError) as exc:
        print(f'NIWB sin datos/uso: {exc}', file=sys.stderr)
        return 2
    except (RuntimeError, OSError, subprocess.SubprocessError) as exc:
        print(f'NIWB error: {exc}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())
