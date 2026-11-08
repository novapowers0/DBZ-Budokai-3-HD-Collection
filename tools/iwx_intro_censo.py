"""Recoge intros de las altas censadas usando solo la copia fija y solcam_pruebas.

El log de una pasada anterior proporciona indices runtime; no son entradas AFS.
PASS del arnes solo acredita recogida. El JSON conserva curvas, pausas y limites.
No mide audio ni acredita equivalencia visual con PS2. No altera mods ni guiones.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from iwx_intro_analizar import analizar


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--copia', required=True, choices=('smoke_a', 'smoke_b', 'smoke_c'))
    parser.add_argument('--censo', required=True, type=Path)
    parser.add_argument('--registro', required=True, type=Path)
    parser.add_argument('--salida', required=True, type=Path)
    parser.add_argument('--entradas', help='entradas AFS separadas por coma; omitido = todas las instaladas')
    args = parser.parse_args()
    censo = json.loads(args.censo.read_text(encoding='utf-8'))
    source_log = args.registro.read_text(encoding='utf-8', errors='replace')
    matches = re.findall(r"dbz3 escenarios: alta (\d+) = '([^']*)'.*?entrada (\d+)\)", source_log)
    indices = {}
    for index, name, entry in matches:
        entry, index = int(entry), int(index)
        if entry in indices and indices[entry] != (index, name):
            raise ValueError(f'alta ambigua para entrada {entry}')
        indices[entry] = (index, name)
    selected = {int(e) for e in args.entradas.split(',')} if args.entradas else None
    cases = [r for r in censo['filas'] if r['instalada'] and r['activa']
             and (selected is None or r['entrada'] in selected)]
    if not cases or (selected is not None and selected != {r['entrada'] for r in cases}):
        raise ValueError('seleccion vacia o alguna entrada no esta instalada/activa en el censo')
    if any(r['entrada'] not in indices for r in cases):
        raise ValueError('el registro no identifica todas las altas seleccionadas')
    args.salida.mkdir(exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    env = dict(os.environ, DBZ3_AGENTE='solIWX', PYTHONUTF8='1',
               DBZ3_PRUEBAS_ENV='DBZ3_TEST_INTRO_SONDA=1;DBZ3_HUB_MODE=1')
    result = dict(censo=str(args.censo), registro=str(args.registro), copia=args.copia,
                  cobertura='Recogida por pasos guest; no acredita fondo/sonido ni equivalencia PS2.', casos=[])
    for case in sorted(cases, key=lambda r: indices[r['entrada']][0]):
        index, name = indices[case['entrada']]
        command = [sys.executable, '-B', 'tools/solcam_pruebas.py', 'correr', args.copia,
                   'solcam', '--par', 'modo=observar', '--par', f'ids=0,7,{index}',
                   '--par', 'seg=2', '--forzar']
        print(f"{case['entrada']} / indice {index}: {name}", flush=True)
        run = subprocess.run(command, cwd=root, env=env, text=True, encoding='utf-8',
                             errors='replace', stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (args.salida/f"{case['entrada']}_recogida.txt").write_text(run.stdout, encoding='utf-8')
        evidence = re.search(r'evidencia y resultados:\s*(.+)', run.stdout)
        record = dict(entrada=case['entrada'], indice=index, nombre=name, origen=case['origen'],
                      comando=command, codigo=run.returncode)
        if run.returncode or not evidence:
            record['error'] = 'recogida fallida; consultar salida retenida'
        else:
            path = Path(evidence[1].strip())
            record['prueba'] = str(path)
            actual_log = (path/'dbz3.log').read_text(encoding='utf-8', errors='replace')
            expected = f"alta {index} '{name}'"
            record['alta_registrada'] = expected in actual_log
            record['contexto'] = json.loads((path/'contexto.json').read_text(encoding='utf-8'))
            try:
                record['medida'] = analizar(path)
            except (ValueError, OSError) as exc:
                record['error'] = str(exc)
        result['casos'].append(record)
        (args.salida/'censo_vivo.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        if record.get('error') or not record.get('alta_registrada', False):
            print(f"DETENIDO: {record.get('error', 'alta no confirmada')}", flush=True)
            return 1
        intro = [g for g in record['medida']['grupos'] if g['estado'] == 1 and g['modo'] == 7]
        pauses = [p for g in intro for p in g['pausas']]
        print(f"  {len(intro)} recorridos; repeticiones >=3 pasos: {len(pauses)}; {path}", flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
