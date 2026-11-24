"""Caché de escenas PAL EN, a partir de la ISO del jugador y la VM Opus R23."""
import argparse
from collections import Counter, defaultdict
from functools import cache, lru_cache
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'mod center hd')]
from historia_b1_1910 import TRANSLATED, adx_info, pal_elf_bytes, tracks
from iso import Iso

EXTRA = {0x01, 0x0b, 0xa2, 0x126, 0xd1}
HOOKS = {0x01: '0x00156160', 0x0b: '0x0015ef10', 0xa2: '0x0015db40'}
RATE = 60


def iso_digest(path):
    # Lectura secuencial grande: en Windows evita miles de lecturas pequeñas.
    digest = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(8 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


class Afs:
    def __init__(self, iso, name):
        self.iso = iso
        self.path = iso.find(name)
        if not self.path:
            raise ValueError(f'Falta {name}')
        _, self.size = iso.files()[self.path]
        magic, count = struct.unpack('<4sI', iso.read(self.path, 0, 8))
        self.head = 8 + 8 * count
        if magic != b'AFS\0' or self.head > self.size or count > 100_000:
            raise ValueError(f'Cabecera inválida de {name}')
        self.entries = list(struct.iter_unpack('<II', iso.read(self.path, 8, 8 * count)))

    def read(self, index, n=None):
        if not 0 <= index < len(self.entries):
            raise ValueError(f'{self.path}:{index} fuera de rango')
        off, size = self.entries[index]
        if off < self.head or not 0 < size <= 64 << 20 or off + size > self.size:
            raise ValueError(f'{self.path}:{index} rango inválido')
        length = size if n is None else min(n, size)
        data = self.iso.read(self.path, off, length)
        if len(data) != length:
            raise ValueError(f'{self.path}:{index} lectura corta')
        return data


def children(data):
    if len(data) < 0x20 or data[:4] != b'#AMB':
        raise ValueError('Cabecera AMB inválida')
    count, table = struct.unpack_from('<II', data, 0x10)
    if table < 0x20 or count > 4096 or table + count * 16 > len(data):
        raise ValueError('Tabla AMB inválida')
    out = []
    for k in range(count):
        off, size, ty, slot, sub = struct.unpack_from('<IIIHH', data, table + k * 16)
        if size < 4 or off + size > len(data):
            raise ValueError(f'Hijo AMB inválido: {k}')
        magic = data[off:off + 4]
        row = dict(hijo=k, off=off, tam=size, tipo=ty, ranura=slot, sub=sub, magic=magic.decode('latin1'))
        if magic in (b'#AMM', b'#AMP', b'#AMC', b'#AML'):
            if size < 0x18:
                raise ValueError(f'Cabecera de animación truncada: {k}')
            n, t = struct.unpack_from('<II', data, off + 0x10)
            if n > 4096 or t + 16 * n > size:
                raise ValueError(f'Tabla de clips inválida: {k}')
            row['clips'] = [struct.unpack_from('<I', data, off + t + 16 * i + 8)[0] for i in range(n)]
        out.append(row)
    return out


def handler_evidence(elf, c_path):
    group = struct.unpack('<19I', pal_elf_bytes(elf, 0x335e00, 19 * 4))
    lines = {}
    with c_path.open(encoding='utf-8', errors='replace') as f:
        for line, text in enumerate(f, 1):
            if text.startswith('// ==== '):
                lines[int(text.split()[2], 16)] = line

    @cache
    def evidence(i):
        va = None
        if i is not None and 0 <= i < 0x130 and group[i >> 4]:
            va, = struct.unpack('<I', pal_elf_bytes(elf, group[i >> 4] + 4 * (i & 15), 4))
        return dict(handler=f'{va:#010x}' if va else None, b1_c_line=lines.get(va))

    return evidence


def engine(tx, packages, voice):
    class Scene(tx.Traductor):
        def __init__(self, script):
            self.hijos, self.ev, self.voz = [], [], {}
            self.idioma, self.cuenta, self.sin = 3, Counter(), Counter()
            self.loaded, self.bound, self.sprites = [], {}, {}
            self.diagnostics, self.unknown = Counter(), {}
            self.hooks, self.hook_calls = {}, Counter()
            self.m = tx.V.Maquina(script, self.stub)

        def e(self, h, tipo, **props):
            for name, value in props.items():
                vals = value if isinstance(value, list) else [value]
                if any(isinstance(x, float) and not math.isfinite(x) for x in vals):
                    self.diagnostics[f'{tipo}.{name}: valor no finito'] += 1
                    props[name] = [None if isinstance(x, float) and not math.isfinite(x) else x for x in vals] if isinstance(value, list) else None
            super().e(h, tipo, cuadro=self.m.cuadro, **props)

        def stub(self, m, h, i, a):
            if i in EXTRA:
                self.cuenta[i] += 1
                if i in HOOKS:
                    self.hook_calls[key(i)] += 1
                    # 0x01 escribe tarea+0x14. 0x0B y 0xA2 comparten tarea+0x0C.
                    hook_slot = (h, 0x14 if i == 0x01 else 0x0c)
                    if i == 0x0b:
                        if a[2] == 0 and self.hooks.get(hook_slot) == 0x0b:
                            self.hooks.pop(hook_slot)
                        elif a[2] != 0:
                            self.hooks[hook_slot] = i
                        self.e(h, 'desplazamiento_uv', builtin=i, callback=HOOKS[i],
                               cuadros=a[2], du=tx.f32(a[1]), dv=tx.f32(a[0]))
                    elif self.hooks.get(hook_slot) != i:
                        self.hooks[hook_slot] = i
                        if i == 0xa2:
                            # b1.c:57037–57086, 39600–39629. Entradas a la transformación Z.
                            self.e(h, 'gancho_profundidad', builtin=i, callback=HOOKS[i],
                                   parametros=[tx.f32(x) for x in a[:4][::-1]])
                        else:
                            self.e(h, 'gancho_render', builtin=i, callback=HOOKS[i])
                    return 1
                if i == 0xd1:
                    self.e(h, 'destino_alternativo', recurso=a[0], pantalla=a[1])
                    return 1
                return tx.V.stub_b1(m, h, i, a)
            if i == 9:
                entry = a[0]
                self.loaded.append(entry)
                for row in packages(entry):
                    self.bound[row['tipo'], row['ranura'], row['sub']] = (entry, row)
                    if row['tipo'] == 2 and row['sub']:
                        self.sprites[row['ranura']] = (entry, row)
                self.hijos = [row for _, row in self.bound.values()]
            if i == 0x41:
                self.cuenta[i] += 1
                bound = self.sprites.get(a[2])
                self.e(h, 'texto_imagen', sprite=a[2], imagen=a[1], celda=a[0],
                       amb=bound[0] if bound else None, hijo=bound[1]['hijo'] if bound else None)
                return 1
            if i == 0x8a:
                self.e(h, 'preparar_stream', canal=a[1], adx_jp=a[0])
            if i == 0x84:
                self.cuenta[i] += 1
                entry = self.voz.get(a[0])
                info = voice(entry) if entry is not None else None
                self.e(h, 'musica' if a[0] == 0 else 'voz', canal=a[0], adx_jp=entry,
                       audio=info, segundos=info['seconds'] if info else None)
                return 1
            if i in (0x33, 0x3a, 0x50, 0x60):
                self.cuenta[i] += 1
                ty, slot, sub = ((3 if i == 0x33 else 4), a[3], a[2]) if i in (0x33, 0x3a) else (5 if i == 0x50 else 6, a[2], 0)
                bound = self.bound.get((ty, slot, sub))
                clips = bound[1].get('clips', []) if bound else []
                if not 0 <= a[1] < len(clips):
                    self.diagnostics[f'clip {i:#x} ranura={slot} sub={sub} clip={a[1]} sin resolver'] += 1
                    length = None
                else:
                    length = clips[a[1]]
                extra = dict(actor=slot, banco=sub) if ty in (3, 4) else dict(canal=slot)
                self.e(h, {3: 'animacion', 4: 'animacion_amp', 5: 'camara', 6: 'luz'}[ty],
                       clip=a[1], vueltas=a[0], largo=length, amb=bound[0] if bound else None,
                       hijo=bound[1]['hijo'] if bound else None, **extra)
                return self.esperar(h, length, a[0]) if length is not None else 1
            if i not in TRANSLATED:
                self.unknown.setdefault(i, dict(pc_siguiente=m.traza[-1]['pc'], cuadro=m.cuadro, pila=a))
            return super().stub(m, h, i, a)

    return Scene


def ratio(a, b):
    return f'{100*a/b:.2f}%' if b else '—'


def key(i):
    return 'indirecta' if i is None else f'{i:#04x}'


def extract(iso_path, out, opus, c_path):
    sys.path.insert(0, str(opus.resolve()))
    import spx_a_escena as tx
    before = iso_path.stat()
    iso = Iso(iso_path)
    boot = iso.boot_elf()
    if not boot or Path(boot).name.lower() != 'sles_512.33':
        raise ValueError('Se requiere la ISO PAL SLES_512.33')
    en, jp = Afs(iso, 'DATA_EN.AFS'), Afs(iso, 'ADX_JP.AFS')
    evidence = handler_evidence(iso.read(boot), c_path)
    indices = defaultdict(list)
    for n in range(1909, 1997):
        indices[en.read(n, 4)].append(n)
    if {k: len(v) for k, v in indices.items()} != {b'#SPX': 38, b'#AMB': 50}:
        raise ValueError('El censo no es 38 SPX + 50 AMB')
    read = lru_cache(maxsize=3)(en.read)

    @cache
    def packages(n):
        return children(read(n))

    @cache
    def voice(n):
        return adx_info(jp.read(n, 16))

    @lru_cache(maxsize=128)
    def camera(n, child, clip):
        h = packages(n)[child]
        data = read(n)[h['off']:h['off'] + h['tam']]
        result = tracks(data, clip)
        table, = struct.unpack_from('<I', data, 0x14)
        offset, = struct.unpack_from('<I', data, table + 16 * clip + 12)
        ptr, = struct.unpack_from('<I', data, offset + 8) if offset else (0,)
        count, = struct.unpack_from('<I', data, ptr + 8) if ptr else (0,)
        if count > 8192 or ptr and ptr + 12 + count * 8 > len(data):
            raise ValueError('Pista de giro AMC inválida')
        result['cam_roll'] = [struct.unpack_from('<If', data, ptr + 12 + k * 8) for k in range(count)]
        return result

    print('38 SPX y 50 AMB. Calculando huella de la ISO PAL...', flush=True)
    digest = iso_digest(iso_path)
    Scene = engine(tx, packages, voice)
    out.mkdir()
    for n in indices[b'#SPX']:
        script = read(n)
        _, _, static = tx.V.censo_estatico(script)
        s = Scene(script)
        try:
            s.correr(10000)
            reason = 'fin_escena' if hasattr(s.m, 'terminada') else 'sin_hilos' if not s.m.vivo() else 'limite_cuadros'
        except (ValueError, IndexError, OverflowError, struct.error) as exc:
            reason = 'error_vm'
            s.diagnostics[f'{type(exc).__name__}: {exc}'] += 1
        if getattr(s.m, 'perdidos', 0):
            s.diagnostics['hilos sin ranura'] += s.m.perdidos
        unknown_static = Counter(i for _, i in static if i not in TRANSLATED | EXTRA)
        unknown = {key(i): dict(**evidence(i), estatica=unknown_static[i], ejecutada=s.sin[i], ejemplo=s.unknown.get(i))
                   for i in sorted(unknown_static.keys() | s.sin.keys(), key=lambda i: -1 if i is None else i)}
        actors = defaultdict(lambda: dict(recursos=[], cargas=[]))
        shots = []
        for amb in dict.fromkeys(s.loaded):
            for h in packages(amb):
                if h['tipo'] in (1, 3, 4) or h['tipo'] == 2 and h['sub'] == 0:
                    actors[h['ranura']]['recursos'].append(dict(amb=amb, **h))
        for event in s.ev:
            if event['tipo'] in ('carga_modelo', 'carga_textura'):
                actors[event['actor']]['cargas'].append(event)
            if event['tipo'] == 'camara' and event['largo'] is not None:
                shots.append(dict(t=event['t'], canal=event['canal'], amb=event['amb'], hijo=event['hijo'],
                                  **camera(event['amb'], event['hijo'], event['clip'])))
        counts = dict(estaticas=len(static), estaticas_traducidas=len(static)-sum(unknown_static.values()),
                      ejecutadas=sum(s.cuenta.values()), ejecutadas_traducidas=sum(s.cuenta.values())-sum(s.sin.values()))
        result = dict(schema='b1c2/pal-en/1', spx=n, spx_sha256=hashlib.sha256(script).hexdigest(),
                      iso_sha256=digest, idioma=3, unidades_por_segundo=RATE, unidades_por_cuadro_pal=1.2,
                      duracion_unidades=round(s.m.reloj, 1), cuadros=s.m.cuadro, cierre=reason,
                      paquetes=list(dict.fromkeys(s.loaded)), cobertura=counts, desconocidos=unknown,
                      diagnosticos=dict(s.diagnostics), actores=[dict(ranura=i, **a) for i, a in sorted(actors.items())],
                      camaras=shots, eventos=s.ev, llamadas_ganchos=dict(s.hook_calls))
        write_json(out / f'escena_{n}.json', result)
        print(n, result['paquetes'], reason, counts['ejecutadas'], len(actors), len(shots), dict(s.diagnostics), flush=True)
    after = iso_path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError('La ISO cambió durante la lectura')
    write_json(out / 'indice.json', dict(iso_sha256=digest, spx=indices[b'#SPX'],
                                        amb={i: packages(i) for i in indices[b'#AMB']}))


def write_json(path, value):
    text = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(',', ':'))
    with path.open('x', encoding='utf-8', newline='\n') as f:
        f.write(text + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--salida', type=Path, required=True)
    parser.add_argument('--opus', type=Path, required=True)
    parser.add_argument('--decomp', type=Path, required=True)
    args = parser.parse_args()
    out = args.salida.resolve()
    if out.is_relative_to(ROOT) or not out.parent.is_dir() or out.exists():
        parser.error('La salida debe ser un directorio nuevo fuera del proyecto, con padre existente')
    extract(args.iso.resolve(), out, args.opus, args.decomp)


if __name__ == '__main__':
    main()
