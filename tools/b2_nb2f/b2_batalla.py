"""NB2F: censo y cotejos focales de los combates de Mundo Dragón, sin abrir el juego."""
import argparse
from collections import Counter
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from inventario import BANCOS, dependencias, exigir, inventario, sha
from bancos import banco

REPO = Path(__file__).resolve().parents[2]
TRABAJO = Path('E:/DBZ3HD_trabajo')
ISO = Path('D:/DBZ3HD/ps2_games/DragonBall Z - Budokai 2 (Europe) (En,Fr,De,Es,It).iso')
ARCHIVOS = ('src/iw_gs_traductor.h', 'src/iw_trad_bucle.cpp', 'src/pasarela_b2.cpp')


def json_leer(path):
    def pares(rows):
        out = dict(rows)
        exigir(len(out) == len(rows), 'clave JSON repetida')
        return out
    return json.loads(path.read_text(encoding='utf-8-sig'), object_pairs_hook=pares,
                      parse_constant=lambda x: exigir(False, f'Número JSON no válido: {x}'))


def juzgar(a, b):
    """Comparador con claves de hito/entrada explícitas. None = pendiente."""
    def index(doc):
        exigir(doc['formato'] == 'nb2f-traza-v1' and isinstance(doc['casos'], list), 'formato de traza incorrecto')
        exigir(isinstance(doc['fuente'], str) and doc['fuente'].strip(), 'falta fuente de la traza')
        out = {}
        for r in doc['casos']:
            exigir(isinstance(r['id'], str) and r['id'] and r['id'] not in out, 'ID vacío o repetido')
            exigir(isinstance(r['entrada'], dict) and r['entrada'] and isinstance(r['valor'], dict) and r['valor'],
                   f'{r["id"]}: faltan entrada o campos')
            out[r['id']] = r
        return out
    def canon(v):
        return json.dumps(v, sort_keys=True, allow_nan=False, ensure_ascii=True)
    A, B = index(a), index(b)
    rows = []
    for key in sorted(A.keys() | B.keys()):
        if key not in A or key not in B or canon(A[key]['entrada']) != canon(B[key]['entrada']):
            rows.append(dict(id=key, estado='PENDIENTE', motivo='caso sin pareja o entrada desalineada'))
            continue
        va, vb = A[key]['valor'], B[key]['valor']
        for field in sorted(va.keys() | vb.keys()):
            x, y = va.get(field), vb.get(field)
            rows.append(dict(id=key, campo=field, a=x, b=y, estado='PENDIENTE' if x is None or y is None
                             else 'IGUAL' if canon(x) == canon(y) else 'DIF'))
    resumen = Counter(row['estado'] for row in rows)
    rc = 1 if resumen['DIF'] else 2 if resumen['PENDIENTE'] or not rows else 0
    return rc, dict(fuentes=[a['fuente'], b['fuente']], resumen=dict(resumen), filas=rows)


def selfcheck():
    def fixture(value, tick=1, has=True):
        return dict(formato='nb2f-traza-v1', fuente='control', casos=[dict(id='fase1/contacto',
                    entrada={'tick': tick}, valor={'sonido': value})] if has else [])
    a = fixture(7)
    for b, rc in ((fixture(7),0), (fixture(8),1), (fixture(True),1), (fixture(None),2),
                  (fixture(7,2),2), (fixture(7,has=False),2)):
        exigir(juzgar(a,b)[0]==rc, f'autoprueba esperaba {rc}')
    b = fixture(7)
    b['casos'] *= 2
    try:
        juzgar(a,b)
    except ValueError:
        return 7
    raise ValueError('el comparador admite IDs repetidos')


def salida(path):
    if path:
        path = path.resolve()
        path.mkdir()
        return path
    return Path(tempfile.mkdtemp(prefix='nb2f-'))


def guarda(path, data):
    with path.open('x', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, indent=2, ensure_ascii=False, allow_nan=False)
        f.write('\n')


def huellas(raiz):
    return {f: sha((raiz/f).read_bytes()) for f in ARCHIVOS}


def argumentos():
    p = argparse.ArgumentParser(description=__doc__, epilog='Salidas: 0 sin diferencias en el foco; 1 DIF; 2 pendiente; 3 error.')
    p.add_argument('--repo', type=Path, default=REPO, help='árbol del port integrado')
    p.add_argument('--iso', type=Path, default=ISO)
    p.add_argument('--estado', type=Path, default=TRABAJO/'A2/sstates/fase1.p2s')
    p.add_argument('--ghidra', type=Path, default=TRABAJO/'ghidra/salida/b2.c')
    p.add_argument('--idioma', choices=BANCOS, default='es')
    p.add_argument('--fase', type=int, choices=range(1,10), action='append')
    llvm = Path('C:/Program Files/LLVM/bin/clang++.exe')
    p.add_argument('--cxx', default=str(llvm) if llvm.is_file() else shutil.which('clang++') or 'clang++')
    p.add_argument('--salida', type=Path, help='carpeta nueva con padre existente; por defecto una carpeta temporal')
    p.add_argument('--self-check', '--autoprueba', dest='self_check', action='store_true', help='7 controles de contrato sin ISO, compilador ni juego')
    sub = p.add_subparsers(dest='modo')
    sub.add_parser('censo', help='inventariar datos T1/T2, Battle!, audio y rutas estáticas')
    c = sub.add_parser('cotejo', help='medir PAL y fragmentos del C++ previo e integrado; juzga la integración')
    c.add_argument('--base', type=Path, required=True, help='respaldo anterior con las tres fuentes bajo src/')
    j = sub.add_parser('juzgar', help='comparar dos trazas nb2f-traza-v1 por caso, entrada y campo')
    j.add_argument('original', type=Path)
    j.add_argument('port', type=Path)
    return p


def main():
    p = argumentos()
    a = p.parse_args()
    try:
        if a.self_check:
            print(f'Autoprueba: {selfcheck()}/7'); return 0
        if a.modo == 'juzgar':
            rc, d = juzgar(json_leer(a.original), json_leer(a.port))
            print(json.dumps(d, ensure_ascii=False, indent=2)); return rc
        if a.modo is None:
            p.print_help(); return 0
        a.repo = a.repo.resolve()
        a.fase = sorted(set(a.fase)) if a.fase else None
        a.anterior, deps = dependencias(a.repo)
        work = salida(a.salida)
        if a.modo == 'censo':
            d = inventario(a, deps)
            guarda(work/'censo.json', d)
            print(f'{work}: {sum(len(f["casillas"]) for f in d["fases"])} casillas, {len(d["pistas_adx"])} pistas ADX')
            return 0
        a.base = a.base.resolve()
        fuentes = dict(base=huellas(a.base), integrado=huellas(a.repo))
        for lado in ('base','parche'):
            (work/lado).mkdir()
        d = banco(a, deps, a.repo, work)
        exigir(fuentes == dict(base=huellas(a.base), integrado=huellas(a.repo)), 'cambiaron las fuentes durante el cotejo')
        d['fuentes_sha256'] = fuentes
        guarda(work/'cotejo.json', d)
        print(work)
        for lado in ('base','parche'):
            print(lado, dict(Counter(row[lado+'_juicio'] for row in d['casos'])))
        for bank in d['ama']['bancos']:
            print(f'CMN {bank["cmn"]}, curva (DIF/medidas):', [(x['clip'], x['cotejo']['distintas'], x['cotejo']['muestras'])
                                                              for x in bank['clips']])
        print('Centro del texto, diferencias:', {k:v['distintas'] for k,v in d['tarjeta']['cotejo'].items()})
        print('Cámara, fundidos y sonido: referencia PAL focal; port sincronizado pendiente.')
        states = {row['parche_juicio'] for row in d['casos']}
        ama_dif = any(x['cotejo']['distintas'] for b in d['ama']['bancos'] for x in b['clips'])
        return 1 if 'DIF' in states or ama_dif or d['tarjeta']['cotejo']['on']['distintas'] else 2 if 'PENDIENTE' in states else 0
    except (OSError, ValueError, RuntimeError, KeyError, TypeError, ImportError, subprocess.SubprocessError) as exc:
        print(f'NB2F: {exc}', file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())
