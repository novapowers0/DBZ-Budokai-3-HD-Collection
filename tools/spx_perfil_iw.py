"""Auditoria offline IW PAL (SLES_553.47). Emite solo metadatos; no ejecuta nativos.

Perfil de entrada: Dragon Mission (SFD) y censo diagnostico #SPX (otros usos).
El porcentaje por ID de B1 es una coincidencia numerica, no una traduccion.
"""
import argparse
import ast
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
WORK = Path('E:/DBZ3HD_trabajo')
ISO = WORK / 'iw_eu/Dragon Ball Z - Infinite World (Europe).iso'
OPUS = WORK / 'estudio_b1/opus'
CMN = '/USR/DATA_CMN.AFS'
DATA = [CMN] + [f'/USR/DATA_{lang}.AFS' for lang in ('ENG', 'FRA', 'GER', 'ITA', 'SPN')]
T_CAP, T_BTL = 0x484930, 0x4582C0
PROBES = {(50, 3): (2552, 7), (993,): (13, 0x45), (994,): (7, 0x71)}


def check(ok, message):
    if not ok:
        raise ValueError(message)


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def u32(data, off):
    return struct.unpack_from('<I', data, off)[0]


class Elf:
    def __init__(self, data):
        self.data = data
        check(data[:6] == b'\x7fELF\x01\x01' and struct.unpack_from('<H', data, 18)[0] == 8,
              'Se esperaba ELF32 MIPS LE')
        off = u32(data, 28)
        stride, n = struct.unpack_from('<HH', data, 42)
        check(stride >= 32 and off + stride * n <= len(data), 'Program headers ELF invalidos')
        self.segments = []
        for k in range(n):
            t, p, va, _, size, _, flags, _ = struct.unpack_from('<8I', data, off + stride * k)
            if t == 1:
                check(p + size <= len(data), 'Segmento ELF truncado')
                self.segments.append((va, p, size, flags))

    def read(self, va, size, flags=0):
        for base, off, length, perm in self.segments:
            if base <= va and va + size <= base + length and perm & flags == flags:
                start = off + va - base
                return self.data[start:start + size]
        raise ValueError(f'VA sin bytes ELF: {va:#x}')

    def ptr(self, va):
        return u32(self.read(va, 4), 0)

    def builtin(self, table, i):
        if i is None or not 0 <= i <= 0xffff:
            return None
        try:
            group = self.ptr(table + 4 * (i >> 4))
            fn = self.ptr(group + 4 * (i & 15))
            self.read(fn, 4, 1)
            return f'0x{fn:06X}' if not fn % 4 else None
        except ValueError:
            return None


def bank(iso, path):
    with iso.open(path) as f:
        extent = iso.files()[path][1]

        def read(off, size):
            check(0 <= off <= off + size <= extent, f'{path}: rango {off:#x}+{size:#x}')
            f.seek(off)
            result = f.read(size)
            check(len(result) == size, f'{path}: lectura truncada')
            return result

        header = read(0, 8)
        check(header[:4] == b'AFS\0', f'{path}: no es AFS')
        count = u32(header, 4)
        top = read(8, 8 * count)
        nodes = 0
        hits = {}
        dmd = []

        def scan(off, size, ids):
            nonlocal nodes
            nodes += 1
            check(len(ids) <= 16, f'{path}:{ids}: #AMB demasiado profundo')
            magic = read(off, min(size, 4))
            if magic == b'#SPX':
                hits[ids] = read(off, size)
            elif magic == b'#DMD' and path == CMN and ids[0] == 1093:
                dmd.append(read(off, size))
            elif magic == b'#AMB':
                check(size >= 0x18, f'{path}:{ids}: #AMB truncado')
                n, table = struct.unpack('<II', read(off + 0x10, 8))
                check(0x18 <= table <= table + 16 * n <= size, f'{path}:{ids}: tabla #AMB')
                for k, (rel, child) in enumerate(struct.iter_unpack('<II8x', read(off + table, 16 * n))):
                    if child:
                        check(0 < rel <= rel + child <= size, f'{path}:{ids}/{k}: hijo fuera de #AMB')
                        scan(off + rel, child, (*ids, k))

        for i, (off, size) in enumerate(struct.iter_unpack('<II', top)):
            scan(off, size, (i,))
        return count, nodes, hits, dmd


def scenes(iso, elf, dmd):
    """Une las casillas tipo 2 con la tabla EU del reproductor y el fichero de ISO."""
    out = []
    count = u32(dmd, 8)
    check(0x10 + 4 * count <= len(dmd), 'Tabla #DMD truncada')
    for row in range(count):
        off = u32(dmd, 0x10 + 4 * row)
        kind, index, key = struct.unpack_from('<III', dmd, off + 0x18)
        if kind != 2:
            continue
        check(0 <= index < 22, f'#DMD: indice de pelicula {index} fuera de tabla PAL')
        raw = elf.read(0x481960 + 0x100 * index, 0x100)
        name = raw.split(b'\0', 1)[0].decode('ascii').replace('\\', '/')
        path = iso.find(name)
        check(path and path.upper().endswith('.SFD'), f'Escena {key:#x}: falta SFD {name}')
        h = iso.read(path, 0, 256 * 1024)
        check(h.startswith(b'\0\0\x01\xba') and b'\0\0\x01\xb3' in h,
              f'{path}: no se encontro pack MPEG y cabecera de secuencia de video')
        out.append(dict(index=index, key=f'{key:#x}', row=row, path=path,
                        size=iso.files()[path][1], kind='sfd',
                        prefix_sha256=hashlib.sha256(h).hexdigest()))
    return sorted(out, key=lambda item: item['index'])


def b1_ids(opus):
    # Lee exactamente el numerador estatico de B1 sin importar su generador de cache.
    tree = ast.parse((opus / 'spx_a_escena.py').read_text(encoding='utf-8-sig'))
    main = next(f for f in tree.body if isinstance(f, ast.FunctionDef) and f.name == 'main')
    return next(ast.literal_eval(a.value) for a in main.body if isinstance(a, ast.Assign)
                and any(isinstance(t, ast.Name) and t.id == 'trad' for t in a.targets))


def header_spx(g):
    check(g.startswith(b'#SPX') and len(g) >= 0x24, 'Cabecera #SPX invalida')
    base, count = struct.unpack_from('<II', g, 0x14)
    check(count > 0 and 0x20 + 4 * count <= base < len(g), 'Tabla SPX fuera de rango')
    return base, struct.unpack_from(f'<{count}I', g, 0x20)


def censo_b1(vm, g):
    base, entries = header_spx(g)
    _, decoded, calls = vm.censo_estatico(g)
    # Guarda los sitios concretos; los IDs de registro son inferencias de B1,
    # no propagacion de valores sensible al flujo.
    imm = sum(decoded[p][1][0][1][0] in vm.IMM for p, _ in calls)
    return dict(base=base, entries=len(entries), masked_offsets=sum(e != 0xffffffff and e >> 28 != 0 for e in entries),
                decoded=sum(d is not None for d in decoded.values()), invalid=sum(d is None for d in decoded.values()),
                immediate=imm, calls=Counter(i for _, i in calls), first_sites={
                    i: min(p for p, j in calls if j == i) for i in {j for _, j in calls}})


def probe(vm, g, cap=2000):
    """Corre el núcleo B1 hasta el primer builtin; no lo simula ni lo etiqueta."""
    base, entries = header_spx(g)
    check(entries[0] < len(g) - base, 'Entrada cero no valida para la VM B1')
    m = vm.Maquina(g)
    m.lanzar()
    h = m.hilos[0]
    for _ in range(cap):
        pc = h.r[9] - vm.CODIGO
        if not base <= pc < len(g):
            break
        raw = vm.decodificar(g, pc)
        if raw is None or raw[2] > len(g):
            break
        rv = m.paso(h)
        if rv & 0xff == 7:
            return pc - base, rv >> 8
        # Caso no lineal respecto del hilo inicial: no seguir adivinando.
        if rv != 1 or len(m.hilos) != 1:
            break
    return None


def audit(iso, opus):
    boot = iso.boot_elf()
    check(boot and boot.upper() == '/SLES_553.47', f'Se esperaba IW PAL, BOOT2={boot}')
    data = iso.read(boot)
    elf = Elf(data)
    vm = load(opus / 'spx_vm.py', 'spx_b1_oraculo')
    translated = b1_ids(opus)
    inventory, scripts, all_hits = {}, {}, {}
    movie = []
    for path in DATA:
        count, nodes, hits, dmd = bank(iso, path)
        inventory[path] = dict(top=count, nodes=nodes, spx=len(hits))
        check(path == CMN or not hits, f'{path}: #SPX nuevos, revisar su contexto')
        if path == CMN:
            check(len(dmd) == 1, 'Se esperaba un #DMD en DATA_CMN 1093')
            movie = scenes(iso, elf, dmd[0])
            scripts = hits

    groups = {}
    for ids, g in scripts.items():
        profile = 'capsulas' if ids in ((993,), (994,)) else 'anidados'
        row = censo_b1(vm, g)
        group = groups.setdefault(profile, Counter())
        group.update(row['calls'])
        all_hits['/'.join(map(str, ids))] = dict(profile=profile, sha256=hashlib.sha256(g).hexdigest(), **row)

    def stats(group):
        total = sum(group.values())
        match = sum(n for i, n in group.items() if i in translated)
        return dict(total=total, match_b1_id=match, pct_b1_id=100 * match / total if total else None,
                    unresolved=group[None], ids=len(group) - (None in group), ids_detail=[dict(
                        id=i, count=n, b1_id=i in translated, iw_capsulas_va=elf.builtin(T_CAP, i),
                        iw_combate_va=elf.builtin(T_BTL, i))
                        for i, n in sorted(group.items(), key=lambda item: -1 if item[0] is None else item[0])])

    combined = Counter()
    for group in groups.values():
        combined.update(group)
    controls = {'/'.join(map(str, ids)): probe(vm, scripts[ids]) for ids in PROBES}
    report = dict(boot=boot, elf_sha256=hashlib.sha256(data).hexdigest(), inventory=inventory,
                  iw_opcode_80_va=elf.builtin(0x4202D0, 0x80),
                  movies=movie, story_spx_calls=0, story_spx_coverage=None, controls=controls,
                  groups={name: stats(group) for name, group in groups.items()}, all_spx=stats(combined),
                  scripts=all_hits)
    return report


def self_test(result):
    movies = result['movies']
    check([s['index'] for s in movies] == list(range(22)), 'Faltan escenas Dragon Mission o hay duplicados')
    check(len({s['path'] for s in movies}) == 22, 'Peliculas repetidas en la tabla')
    for index, expected in [(0, 'FD01A.SFD'), (2, 'FD02.SFD'), (21, 'FD20.SFD')]:
        scene = movies[index]
        check(scene['key'] == f'{0x850 + index:#x}' and scene['path'].endswith('/' + expected),
              f'No coincide el ancla de escena {index}')
    check(len(result['scripts']) == 66 and all(s['decoded'] for s in result['scripts'].values()),
          'Cambio en corpus SPX PAL o guion sin decodificar')
    check(result['iw_opcode_80_va'] == '0x148630', 'No coincide el handler de opcode 0x80 de IW PAL')
    check(result['all_spx']['total'] == 21125 and result['all_spx']['unresolved'] == 0,
          'Cambio en el censo de sitios SPX PAL')
    for ids, expected in PROBES.items():
        key = '/'.join(map(str, ids))
        value = result['controls'][key]
        check(value == expected, f'{key}: VM B1 primer builtin {value} != {expected}')
        pc, i = value
        check(i in result['scripts'][key]['calls'], f'{key}:{pc:#x}: builtin fuera del censo B1')
    caps = result['groups']['capsulas']['ids_detail']
    check(any(i['id'] == 0x75 and i['iw_capsulas_va'] == '0x283130' for i in caps),
          'No se reprodujo la colision 0x75 de CapsulePick')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso', type=Path, default=ISO)
    p.add_argument('--opus', type=Path, default=OPUS)
    p.add_argument('--self-test', action='store_true', help='Comprueba las 22 escenas SFD y tres SPX de control')
    p.add_argument('--json', action='store_true', help='Escribe solo metadatos por stdout')
    args = p.parse_args()
    try:
        reader = load(ROOT / 'mod center hd/iso.py', 'lector_iw_iso')
        iso = reader.Iso(args.iso)
        try:
            result = audit(iso, args.opus)
        finally:
            iso._f.close()
        if args.self_test:
            self_test(result)
        if args.json:
            print(json.dumps(result, indent=2))
        else:
            s = result['all_spx']
            print(f"Historia IW: {len(result['movies'])} SFD; cobertura de builtins SPX: N/A (0/0)")
            print(f"Control AFS: {len(result['scripts'])} #SPX; {s['match_b1_id']}/{s['total']} IDs coincidentes "
                  f"({s['pct_b1_id']:.2f} %, sin afirmar equivalencia)")
            print('Sondas B1 (PC relativo, primer ID):', result['controls'])
            if args.self_test:
                print('Autoprueba PAL: OK')
        return 0
    except (ValueError, OSError, struct.error, KeyError, IndexError, ArithmeticError) as exc:
        print(f'B1C3: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
