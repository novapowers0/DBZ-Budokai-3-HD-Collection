"""NIWC: inventario de activos IW y sondas de su ELF PAL, sin juego ni PCSX2."""
import argparse
from collections import Counter
from functools import lru_cache
from hashlib import sha256
import json
from pathlib import Path
import re
import struct
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
EU = Path('E:/DBZ3HD_trabajo/_bancos_archivados/ps2_eu_iw_20261021/Infinite World (Europe)')
US = Path('ps2_games/Infinite World (USA)')
CLASES = ('modelo', 'textura', 'hud', 'icono', 'barra', 'fuente')
ESTADOS = ('nativo', 'mixto', 'respaldo_manual', 'optativo', 'pendiente')
GRUPOS = {'G': 'D0 D1 D2 D9 DD DF', 'A': 'D4 DB DC', 'B': 'D5 DA E1',
          'V': 'D6 E0', 'S': 'D3 D7 D8 DE', 'MINIS': ' '.join(f'{n:X}' for n in range(0xD0, 0xE2))}
GRUPOS = {k: tuple('0x7' + x for x in v.split()) for k, v in GRUPOS.items()}
BLOQUES = {'0x7D0': (887, 903), '0x7D1': (888, 897), '0x7D2': (889, 884),
           '0x7D9': (893, 900), '0x7DD': (894, 906), '0x7DF': (896, 909)}


def need(ok, msg):
    if not ok:
        raise ValueError(msg)


def setup(root):
    for p in ('tools', 'tools/iw_mips', 'awo_tools'):
        sys.path.insert(0, str(root / p))


def digest(data):
    return sha256(data).hexdigest()


@lru_cache(maxsize=16)
def entry(afs, index):
    from astra_iw_auditoria import entrada_afs
    size = afs.stat().st_size
    with afs.open('rb') as f:
        magic, n = struct.unpack('<4sI', f.read(8))
        need(magic == b'AFS\0' and 0 <= index < n and 8 + 8*n <= size, f'AFS/índice inválido {afs}:{index}')
        f.seek(8+8*index)
        offset, count = struct.unpack('<II', f.read(8))
        need(0 < count <= 64 << 20 and 8+8*n <= offset <= size-count, f'Rango AFS inválido {afs}:{index}')
    b = entrada_afs(afs, index)
    need(len(b) == count, f'Lectura AFS incompleta {afs}:{index}')
    return b


def blob(afs, indices):
    from astra_iw_auditoria import hijo, u
    b = entry(afs, indices[0])
    for k in indices[1:]:
        need(b.startswith(b'#AMB') and len(b) >= 24 and 0 <= k < u(b, 16), f'Hijo fuera de #AMB: {indices}')
        b = hijo(b, k)
    return b


def amt_rows(b):
    from amt_ps2 import entries
    need(b.startswith(b'#AMT') and len(b) >= 24, 'No es #AMT')
    n, idx = struct.unpack_from('<II', b, 16)
    need(24 <= idx <= len(b) and n <= min(4096, (len(b)-idx)//4), 'Tabla #AMT incompleta')
    return entries(b, keep_empty=True)


def texture(b, t):
    from amt_ps2 import GIF_HDR
    w, h, psm = t['w'], t['h'], t['psm']
    wanted = {0: w*h*4, 19: w*h, 20: (w*h+1)//2}.get(psm)
    off = t['data']+GIF_HDR
    valid = wanted is not None and w > 0 and h > 0 and wanted <= t['dsize'] and off+t['dsize'] <= len(b)
    if psm in (19, 20):
        valid = valid and t['csize'] == (1024 if psm == 19 else 64) and t['clut']+GIF_HDR+t['csize'] <= len(b)
    return {'w': w, 'h': h, 'psm': psm, 'rango_esperado_valido': valid,
            'sha256_pixeles_crudos': digest(b[off:off+wanted]) if valid else None}


@lru_cache(maxsize=2048)
def resource(afs, indices, slot, expected):
    b = blob(afs, indices)
    need(b[:4] == expected.encode('ascii'), f'{afs.name}:{indices}: {b[:4]!r} != {expected}')
    out = dict(afs=afs.name, indices=indices, slot=slot, magic=expected, bytes=len(b), sha256=digest(b))
    if expected == '#AMT':
        bank = amt_rows(b)
        out.update(slots=len(bank), por_psm=dict(sorted(Counter(t['psm'] for t in bank if t).items())),
                   huecos=sum(t is None for t in bank))
        if slot is not None:
            need(0 <= slot < len(bank) and bank[slot] is not None, f'{afs.name}:{indices}@{slot} no presente')
            out['textura'] = texture(b, bank[slot])
    return out


def resolve(text, archives):
    m = re.fullmatch(r'([CL]):([0-9/]+)(?:@(\d+))?=(#[A-Z0-9]{3})', text)
    need(m is not None, f'Referencia inválida: {text}')
    alias, indices, slot, expected = m.groups()
    return resource(archives[alias], tuple(map(int, indices.split('/'))), int(slot) if slot is not None else None, expected)


@lru_cache(maxsize=32)
def source(root, name):
    need(re.fullmatch(r'[\w-]+\.(cpp|h|inl)', name) is not None, f'Fuente fuera del alcance: {name}')
    b = (root / 'src' / name).read_bytes()
    return digest(b), b.decode('utf-8-sig', errors='replace').splitlines()


def anchors(root, refs):
    if refs == '–':
        return []
    out = []
    for ref in refs.split(';'):
        name, token = ref.split('::', 1)
        sha, lines = source(root, name)
        matches = [i for i, line in enumerate(lines, 1) if token in line]
        out.append(dict(path='src/' + name, literal=token, lineas=matches, sha256=sha))
    return out


def variants(root, pal, lang):
    dirs = [('USA', root/US, 'USA'), ('PAL-' + lang, pal, lang)]
    return {label: {'C': base/'USR/DATA_CMN.AFS', 'L': base/f'USR/DATA_{suffix}.AFS'}
            for label, base, suffix in dirs}


def census(root, copies, catalog, course=None):
    from iw_censo import CURSOS, leer_dmd
    data = {label: leer_dmd(files['C']) for label, files in copies.items()}
    rows, ids = [], set()
    for line in catalog.read_text(encoding='utf-8').splitlines():
        if not line or line.startswith('#'):
            continue
        key, scope, category, state, refs, cpp, note = line.split('|')
        need(key not in ids and category in CLASES and state in ESTADOS, f'Fila inválida: {line}')
        ids.add(key)
        keys = dict.fromkeys(k for x in scope.split(',') for k in (tuple(CURSOS) if x == '*' else GRUPOS.get(x, (x,))))
        need(set(keys) <= CURSOS.keys(), f'Curso desconocido en {key}')
        src = anchors(root, cpp)
        for k in keys:
            if course is not None and k != course:
                continue
            pack, field = BLOQUES.get(k, (None, None))
            args = dict(pack=pack, campo=field, wcf=field+1 if field else None,
                        titulo=335+int(k,16)-0x7D0 if k.startswith('0x') else None, fx='890/6' if k == '0x7D3' else '935')
            finds, errors = {}, []
            for label, afs in copies.items():
                finds[label] = []
                for ref in ([] if refs == '-' else refs.format(**args).split()):
                    try:
                        finds[label].append(resolve(ref, afs))
                    except (OSError, ValueError, AssertionError, struct.error) as e:
                        errors.append(f'{label} {ref}: {e}')
            missing = [a['literal'] for a in src if not a['lineas']]
            rows.append(dict(id=key, curso=k, clase=category, estado=state if not (errors or missing) else 'revisar',
                             estado_catalogo=state, nota=note, recursos=finds, errores=errors, anclas_sin_resultado=missing, fuente=src))
    return dict(schema='niwc-activos-v1', fuentes=copies, dmd=data, filas=rows,
                resumen=dict(Counter(r['estado'] for r in rows)))


def effect_evidence(copies):
    from astra_iw_auditoria import hijo, u
    out = []
    for version, afs in copies.items():
        for path in [(935,), (890, 6)]:
            pkg = blob(afs['C'], path)
            amt = hijo(pkg, 0)
            bank = amt_rows(amt)
            direct = {i: texture(amt, t) for i, t in enumerate(bank) if t and t['psm'] == 0}
            refs = []
            for i in range(3, u(pkg, 16)):
                b = hijo(pkg, i)
                if not b.startswith(b'#AME'):
                    continue
                # Sondeo de referencias estáticas: árbol serializado, no predice cuadros visibles.
                todo, seen = [u(b, 12)], set()
                while todo:
                    o = todo.pop()
                    if not o or o in seen:
                        continue
                    need(16 <= o <= len(b)-0x2c, f'AME inválido: {path}/{i} +{o:#x}')
                    seen.add(o)
                    ty = u(b, o+4)
                    todo.extend((u(b,o+0x28), u(b,o+0x24)))
                    if ty >> 16 == 2 and ty & 0xffff in (0, 1, 2, 3):
                        slot = u(b, o+(0xc4 if ty&0xffff == 2 else 0xa4))
                        if slot in direct:
                            refs.append(dict(hijo=i, offset=o, tipo=ty & 0xffff, slot=slot, sha256_ame=digest(b)))
            out.append(dict(version=version, banco=path+(0,), sha256_amt=digest(amt), slots=len(bank),
                            directas=direct, refs=sorted(refs, key=lambda r: (r['hijo'],r['offset']))))
    return out


def ee_probe(elf):
    from _iw_cotejo_focos import cpu_eu, llamar, CTX
    selector, load_flag, help_fn, opener, cd = 0x30D9F0, 0x28A9D0, 0x1B21F0, 0x348110, 0x14C960
    c, sha = cpu_eu(elf, [selector, load_flag, help_fn, opener])
    c.w32(0x49CBA4, CTX)
    c.w32(0x49CE74, CTX+0x2000)
    requests = []
    def open_afs(cpu):
        requests.append([cpu.r[j] & 0xffffffff for j in (4, 5, 6)])
        cpu.r[2] = CTX+0x3000   # handle de prueba, no abre nada
    c.hooks[cd] = open_afs
    effects = []
    for mode in (3, 8, 9, 15, 4, 14):
        c.w32(CTX+4, mode)
        c.w32(CTX+0x2200, 0)
        requests.clear()
        before = c.n
        ret = llamar(c, opener)
        effects.append(dict(modo=mode, llamadas=list(requests), retorno=ret, pasos=c.n-before))

    # Usamos HelpSelectMenuText real sobre una tabla de ayuda inyectada; la salida
    # y las UV centinela prueban qué entrada selecciona la rutina, no sus UV reales.
    ui, res, table, dst = (CTX+x for x in (0x4000,0x4800,0x5000,0x6000))
    c.w32(0x49CDC0, ui)
    c.w32(ui+8, ui+0x100)
    c.w32(ui+0x118, 128)
    c.w32(ui+0x11c, ui+0x200)
    c.w32(res+8, res+0x100)
    c.w32(res+0x114, table)
    for i in range(128):
        c.w32(ui+0x200+4*i, i)
    for i in range(10):
        for word in (0,4,5,6,7):
            c.w32(table+32*i+4*word, 0x5600+16*i+word)
    buttons = []
    for mode, item, sprite in [(1,i,0x5c) for i in (0,3,10,7,8,11,13,12,33)]+[(16,0,0x2f),(10,0,0x37),(14,0,0x2f),(1,3,0x20)]:
        c.w32(CTX+4, mode)
        c.w16(CTX+0x10, item)
        before = c.n
        for word in range(8):
            c.w32(dst+4*word, 0xDEADBEEF)
        c.r[10] = dst
        c.w32(ui+0x200+4*sprite, res)
        llamar(c, selector, 0, res)
        c.w32(ui+0x200+4*sprite, sprite)
        v = c.r32(dst)
        row = (v - 0x5600)//16 if v != 0xDEADBEEF else None
        if row is not None:
            need([c.r32(dst+4*n) for n in (0,4,5,6,7)] == [0x5600+16*row+n for n in (0,4,5,6,7)], 'Salida de selector parcial')
        buttons.append(dict(modo=mode, item=item, sprite=sprite, fila=row, pasos=c.n-before))
    return dict(elf=str(elf), sha256=sha, efectos=effects, selector=buttons)


def selfcheck():
    from iw_censo import CURSOS
    need(set().union(*(set(GRUPOS[g]) for g in 'GABVS')) == set(CURSOS)-{'MAPA','ROAD'}, 'Cobertura de familias')
    try:
        amt_rows(b'#AMT'+bytes(19))
    except ValueError:
        pass
    else:
        raise ValueError('Se aceptó un #AMT truncado')
    need(texture(bytes(40), dict(w=1,h=2,psm=0,data=4,dsize=8))['rango_esperado_valido'] is False,
         'Se aceptaron píxeles truncados')


def dump(obj, dest):
    text = json.dumps(obj, indent=2, default=str, ensure_ascii=False)
    if dest:
        with dest.open('x', encoding='utf-8', newline='\n') as f:
            f.write(text+'\n')
    else:
        print(text)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raiz', type=Path, default=HERE.parents[1], help='Raíz del árbol C++ y de la copia USA')
    p.add_argument('--pal', type=Path, default=EU, help='Directorio PAL extraído con USR/ y SLES_553.47')
    p.add_argument('--idioma', choices=['ENG','SPN','FRA','GER','ITA'], default='SPN')
    p.add_argument('--catalogo', type=Path, default=HERE/'catalogo.txt')
    p.add_argument('--salida', type=Path, help='JSON nuevo; nunca sobrescribe')
    p.add_argument('--autoprueba', '--self-check', dest='autoprueba', action='store_true', help='Ejecutar controles sin AFS ni ELF')
    p.add_argument('accion', choices=['censo','original'], nargs='?', default='censo')
    p.add_argument('curso', nargs='?', help='Filtro de censo, p.ej. 0x7DE, MAPA o ROAD')
    a = p.parse_args(argv)
    root = a.raiz.resolve()
    setup(root)
    try:
        if a.autoprueba:
            selfcheck()
            print('NIWC autoprueba: OK')
            return 0
        copies = variants(root, a.pal, a.idioma)
        if a.accion == 'original':
            need(a.curso is None, 'El filtro curso solo se admite en censo')
            dump(dict(ee=ee_probe(a.pal/'SLES_553.47'), efectos=effect_evidence(copies)), a.salida)
            return 0
        from iw_censo import CURSOS
        course = a.curso.upper().replace('0X','0x') if a.curso else None
        need(course is None or course in CURSOS, f'Curso desconocido: {a.curso}')
        result = census(root, copies, a.catalogo, course)
        dump(result, a.salida)
        return 2 if any(r['filas'] is None for r in result['dmd'].values()) or 'revisar' in result['resumen'] else 0
    except (OSError, ValueError, AssertionError, RuntimeError, struct.error) as e:
        print(f'NIWC: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
