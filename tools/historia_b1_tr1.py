"""TR1: traductor PAL EN con censo estructural y evidencia por sitio SPX."""
import argparse
from collections import Counter, defaultdict
from functools import lru_cache
from fractions import Fraction
import hashlib
import math
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import historia_b1_escenas as base


BRANCH = {0x70: 0, 0x71: 1, 0x72: 1, 0x73: 0, 0x74: 1, 0x75: 1,
          0x90: 0, 0x91: 0, 0x96: 0, 0x98: 0, 0x99: 0}
TERMINAL = {0x70, 0x76, 0x77, 0x94, 0x95, 0x96, 0x97, 0x99}
ELF_SHA = 'e0cbc2cac7244409e1b4c910a7a55004bb0f0f5a4715b78a3563b0daf00d3f38'
ISO_SHA = 'a3c64545d4d32446e36bf264dd356d8f6144cfecff75332a6af859eb5cbfd3db'
FACTORY = Path('E:/DBZ3HD_trabajo/fabrica/TR1')

# El cuerpo de estos handlers queda sin función propia en b1.c. Se enlaza
# el consumidor cuando existe, y siempre el dispatch y la VA del ELF PAL.
C_REFS = {
    1: '53607–53614', 2: '95193–95197', 3: '95193–95197',
    0x30: '53654–53753', 0x31: '53756–53817', 0x36: '53736–53738',
    0x37: '53704–53713', 0x3d: '53884–53945', 0x3e: '53884–53945',
    0x3f: '53682–53685', 0x40: '54230–54309', 0x41: '54230–54309',
    0x43: '54312–54413', 0x44: '54230–54309', 0x45: '54230–54309',
    0x46: '54230–54309', 0x63: '45185–45258', 0x65: '54799–54817',
    0x66: '45185–45258', 0xc7: '51663–51737', 0xe0: '123405–123410',
    0xe1: '123405–123410', 0xe4: '141535–141583', 0xea: '141227–141249',
    0x128: '54058–54066, 54178–54184',
}


def census(vm, script):
    start, count = struct.unpack_from('<II', script, 0x14)
    if not 0x20 + count * 4 <= start < len(script):
        raise ValueError('Cabecera SPX inválida')
    roots = [x[0] for x in struct.iter_unpack('<I', script[0x20:0x20 + count * 4])
             if x[0] != 0xffffffff]
    unknown = (None,) * 8
    pending = [(pc, unknown) for pc in roots]
    seen, own, calls, gaps = set(), {}, defaultdict(set), set()
    while pending:
        pc, inputs = pending.pop()
        if (pc, inputs) in seen:
            continue
        seen.add((pc, inputs))
        if len(seen) > 500_000:
            raise ValueError('Límite del censo SPX')
        if not 0 <= pc < len(script) - start:
            gaps.add((pc, 'fuera de SPX'))
            continue
        dec = vm.decodificar(script, start + pc)
        if dec is None or dec[2] > len(script):
            gaps.add((pc, 'instrucción inválida'))
            continue
        op, args, end = dec
        for byte in range(pc, end - start):
            if own.setdefault(byte, pc) != pc:
                gaps.add((pc, 'instrucciones solapadas'))
                break

        def val(operand):
            mode, reg, value = operand
            return value if mode in vm.IMM else inputs[reg] if mode == 0 and reg < len(inputs) else None

        regs = list(inputs)
        for kind, (_, (mode, reg, _)) in zip(vm.FORMA[op], args):
            if kind == 'D' and mode == 0 and reg < len(regs):
                regs[reg] = val(args[0][1]) if op == 1 else 0 if op == 2 else None
        if op in (0x50, 0x51, 0x52, 0x53, 0x80):
            regs[0] = None
        if op in BRANCH or op in (0x80, 0x77, 0x79, 0x7b):
            value = val(args[BRANCH.get(op, 0)][1])
            if op != 0x80 and op not in BRANCH:
                # Destino absoluto de ret con PC explícito: no inferir de su operando de condición.
                gaps.add((pc, f'retorno explícito {op:#x}'))
            elif op == 0x80:
                calls[pc].add(value)
            elif value is None:
                gaps.add((pc, f'destino indirecto {op:#x}'))
            else:
                pending.append((value, unknown if op in (0x90, 0x91, 0x96, 0x98, 0x99) else tuple(regs)))
        if op not in TERMINAL:
            pending.append((end - start, unknown if op in (0x73, 0x74, 0x75) else tuple(regs)))
    for pc, variants in calls.items():
        if len(variants) != 1 or None in variants:
            gaps.add((pc, 'builtin no resuelto de forma única'))
    return {pc: next(iter(v)) if len(v) == 1 else None for pc, v in calls.items()}, sorted(gaps)


def source(iso_path, opus, decomp):
    sys.path.insert(0, str(opus.resolve()))
    import spx_a_escena as tx
    iso = base.Iso(iso_path)
    boot = iso.boot_elf()
    if not boot or Path(boot).name.lower() != 'sles_512.33':
        raise ValueError('Se requiere PAL SLES_512.33')
    en, jp = base.Afs(iso, 'DATA_EN.AFS'), base.Afs(iso, 'ADX_JP.AFS')
    elf = iso.read(boot)
    if hashlib.sha256(elf).hexdigest() != ELF_SHA:
        raise ValueError('ELF PAL distinto del b1.c contrastado')
    evidence = base.handler_evidence(elf, decomp)
    ids = defaultdict(list)
    for n in range(1909, 1997):
        ids[en.read(n, 4)].append(n)
    if {k: len(v) for k, v in ids.items()} != {b'#SPX': 38, b'#AMB': 50}:
        raise ValueError('Se requieren 38 SPX y 50 AMB')
    return tx, en, jp, elf, evidence, ids


def f32(n):
    v = struct.unpack('<f', struct.pack('<I', n & 0xffffffff))[0]
    return v if math.isfinite(v) else None


def keys(data, ptr, fmt):
    if ptr == 0:
        return []
    row = struct.Struct('<' + fmt)
    count, = struct.unpack_from('<I', data, ptr + 8)
    end = ptr + 12 + row.size * count
    if not 0 < count <= 65536 or end > len(data):
        raise ValueError(f'Pista inválida {ptr:#x}/{count}')
    result = list(row.iter_unpack(data[ptr + 12:end]))
    if any(not all(map(math.isfinite, key)) for key in result):
        raise ValueError('Clave no finita')
    return result


def bank(data, magic):
    """Pistas AML (b1.c:44969–45181) y selectores AMP (50345–50368)."""
    count, table = struct.unpack_from('<II', data, 0x10)
    track_count, names = struct.unpack_from('<II', data, 0x18) if magic == '#AMP' else (0, 0)
    if track_count > 4096 or names + track_count * 32 > len(data):
        raise ValueError('Tabla de nombres AMP inválida')
    result = []
    for i in range(count):
        typ, flags, frames, off = struct.unpack_from('<4I', data, table + 16 * i)
        row = dict(clip=i, tipo=typ, campo_04=flags, cuadros=frames, offset=off)
        if magic == '#AML' and typ in (9, 10, 0x29):
            p, c = struct.unpack_from('<2I', data, off)
            row.update(vector=keys(data, p, 'I3f'), color=keys(data, c, 'I3f'),
                       vector_destino='posicion' if typ & 15 == 9 else 'direccion')
            if typ == 0x29:
                row['atenuacion'] = keys(data, struct.unpack_from('<I', data, off + 8)[0], 'I2f')
        elif magic == '#AMP':
            if off + track_count * 4 > len(data):
                raise ValueError('Tabla de pistas AMP inválida')
            row['selectores'] = []
            for j in range(track_count):
                ptr, = struct.unpack_from('<I', data, off + j * 4)
                if ptr:
                    name = cstring(data, names + j * 32, 32)
                    row['selectores'].append(dict(pista=j, nodo=name, claves=keys(data, ptr, 'hh')))
        result.append(row)
    return result


def cstring(data, off, limit):
    return data[off:off + limit].split(b'\0', 1)[0].decode('latin1')


def model_info(data):
    """Nombre de raíz y nodos AMO (+0x24), no deducidos del número de actor."""
    count, = struct.unpack_from('<I', data, 0x10)
    offset, = struct.unpack_from('<I', data, 0x24)
    if not 0 < count < 4096 or not 0 < offset <= len(data) - count * 32:
        raise ValueError('Tabla de nodos AMO inválida')
    nodes = [cstring(data, offset + 32 * i, 32) for i in range(count)]
    return dict(raiz=nodes[0], nodos=nodes, cielo=nodes[0].upper().startswith('SKY'))


def fade_ticks():
    """Número nominal de aumentos del fade PAL desde cero, en binary32."""
    alpha, ticks = 0.0, 0
    delta = f32(0x3c23d70a)
    while alpha < 1.0:
        alpha, = struct.unpack('<f', struct.pack('<f', alpha + delta))
        ticks += 1
    return ticks


def generator(tx, packages, voice, elf):
    cls = base.engine(tx, packages, voice)
    e2 = struct.unpack('<3i', base.pal_elf_bytes(elf, 0x335ee0, 12))

    class Scene(cls):
        def __init__(self, script):
            super().__init__(script)
            self.flags = defaultdict(int)
            self.context = None

        def e(self, h, tipo, **props):
            if self.context:
                builtin, pc = self.context
                props.setdefault('builtin', builtin)
                props['pc_siguiente'] = pc
            super().e(h, tipo, **props)

        def stub(self, m, h, i, a):
            self.context = i, m.traza[-1]['pc']
            try:
                return self.translate(m, h, i, a)
            finally:
                self.context = None

        def translate(self, m, h, i, a):
            x, y, z, w, p, q = a
            stack = lambda n: [m.r32(h.r[11] + 4 * j) for j in range(n)]
            floats = lambda n: [f32(v) for v in reversed(a[:n])]
            if i not in NUEVOS:
                ret = super().stub(m, h, i, a)
                if i in (0xd3, 0xd7):
                    self.ev[-1].pop('fundido' if i == 0xd3 else 'cuadros')
                    self.ev[-1].update(selector=x, delta_f32=f32(0x3c23d70a),
                                       cuadros_pal_desde_extremo=fade_ticks(), es_parada_stream=False)
                if i == 0x38:
                    self.ev[-1]['tipo'] = 'selector_nodo'
                if i == 0x63:
                    self.ev[-1].update(tipo='luz_activar_bits', mascara=x & 0xffff,
                                       limpiar_30_antes=bool(x & 0x30))
                if i == 0x47:
                    self.ev[-1]['rgba'] = floats(4)
                return ret
            self.cuenta[i] += 1
            if i == 0x0a:
                self.e(h, 'reinicio_estado_grafico', modo_53c20=2)
            elif i == 0x16:
                self.e(h, 'liberar_luz', canal=x)
                for key in list(self.bound):
                    if key[0] == 6 and (x == -1 or key[1] == x):
                        del self.bound[key]
            elif i == 0x39:
                lo, hi = f32(y), f32(x)
                bound = self.bound.get((3, p, w))
                self.e(h, 'animacion_segmento', actor=p, banco=w, clip=z, desde=lo, hasta=hi,
                       amb=bound[0] if bound else None, hijo=bound[1]['hijo'] if bound else None)
                if lo is None or hi is None or hi < lo:
                    self.diagnostics['segmento AMM inválido'] += 1
                else:
                    return self.esperar(h, round(hi + 1 - (lo - 1.2)), 1)
            elif i == 0x3c:
                self.e(h, 'objeto_procedural', actor=p, forma=w, resolucion=x, parametros=floats(3)[:2],
                       libera_anterior=True, asigna=w in range(4), opacidad_inicial=0.5 if w in range(4) else None)
            elif i in (0x3f, 0x128):
                self.e(h, 'actor_flush_al_dibujar' if i == 0x3f else 'actor_cuantizar_animacion', actor=y, valor=x & 0xff)
            elif i == 0x44:
                self.e(h, 'sprite_dimensiones', sprite=z, ancho=f32(y), alto=f32(x))
            elif i == 0x46:
                self.e(h, 'sprite_anclaje', sprite=y, flags=x, centrado=bool(x & 1))
            elif i == 0x52:
                if not 0 <= x <= 32:
                    raise ValueError(f'Número de rectángulos no admitido: {x}')
                data = stack(2 * x + 3)
                self.e(h, 'composicion_camara_2d', n=x, rectangulos=[data[k:k+2][::-1] for k in range(1, 2*x+1, 2)],
                       offset_gs=[data[-1]+1728, data[-2]*256/448+1920])
            elif i == 0x64:
                self.e(h, 'luz_base', activa=bool(x), callback='0x00159dd0')
            elif i == 0x65:
                self.e(h, 'luz_base_color', rgba=floats(4))
            elif i == 0x66:
                self.e(h, 'luz_limpiar_bits', canal=y, mascara=x & 0xffff)
            elif i == 0xa1:
                self.e(h, 'velo_rgba', rgba=floats(4))
            elif i == 0xa3:
                self.e(h, 'gancho_composicion_gs', callback='0x0015dbf0')
            elif i == 0xa4:
                data = stack(11) if x else None
                self.e(h, 'efecto_particulas', activo=bool(x), ranura=data[10] if data else None,
                       cantidad=data[8] if data else None, perfil=data[9] if data else None,
                       palabras=data[1:10] if data else None)
            elif i == 0xa5:
                rgb = list(struct.unpack('<3f', base.pal_elf_bytes(elf, 0x335ec0, 12)))
                self.e(h, 'fundido_velo', cuadros_pal=x, rgb=rgb, curva='(n/(n+1))^3')
                h.acum = (int(m.reloj + 0.5) if getattr(h, 'acum', None) is None else h.acum) + x
                elapsed = 0

                def finish(mm, hh):
                    nonlocal elapsed
                    elapsed += 1
                    return 1 if elapsed >= max(0, x - 1) else 3

                if x <= 0:
                    return 1
                h.pend = finish
                return 3
            elif i == 0xc3:
                self.e(h, 'orden_iop_sonido', orden=x)
            elif i in (0xc5, 0xc6):
                self.e(h, 'registrar_banco_sonido', banco=x, variante=i)
            elif i == 0xc7:
                self.e(h, 'barrera_iop_sonido', disponibilidad='inmediata_modelada')
            elif i in (0xd2, 0xd6):
                self.e(h, 'limpieza_transicion', fase='solicita_cambio' if i == 0xd2 else 'espera_bandera',
                       orden_al_completar='parar_stream_todos', continuacion_t=None)
                # D2 cede incondicionalmente una vez. D6 depende del gestor
                # exterior; aquí se evalúa con bandera ya disponible.
                if i == 0xd2:
                    h.pend = lambda mm, hh: 1
                    return 3
            elif i in (0xe0, 0xe1, 0xe4):
                bit = 1 << (x & 31)
                if i == 0xe0:
                    self.flags[y] |= bit
                elif i == 0xe1:
                    h.r[0] = int(bool(self.flags[y] & bit))
                self.e(h, 'flag_historia', tabla=y, bit=x & 31, operacion={0xe0:'set',0xe1:'get',0xe4:'marca_si_activo'}[i],
                       activo=bool(self.flags[y] & bit))
            elif i == 0xe2:
                self.e(h, 'indice_historia', valor=x + e2[y] if 0 <= y < len(e2) else None, base_index=y, suma=x)
            elif i == 0xf4:
                self.e(h, 'reinicio_subsistema_338420')
            return 1

    return Scene


NUEVOS = {0x0a, 0x16, 0x39, 0x3c, 0x3f, 0x44, 0x46, 0x52, 0x64, 0x65, 0x66,
          0xa1, 0xa3, 0xa4, 0xa5, 0xc3, 0xc5, 0xc6, 0xc7, 0xd2, 0xd6, 0xe0, 0xe1,
          0xe2, 0xe4, 0xf4, 0x128}


def audio_windows(events):
    """Ventana SPX; un cierre visual no se convierte en un stop ADX ficticio."""
    rows = []
    for k, e in enumerate(events):
        if e['tipo'] != 'voz':
            continue
        ch, info = e['canal'], e['audio']
        later = next(x for x in events[k + 1:] if x['tipo'] == 'fin_escena' or
                     x['tipo'] in ('voz', 'preparar_stream', 'parar_stream') and x.get('canal') in (ch, -1))
        natural = Fraction(info['samples'] * 60, info['sample_rate'])
        delta = Fraction(str(later['t'])) - Fraction(str(e['t']))
        over = natural - delta
        reason = later['tipo']
        status = ('dentro' if over <= 1 else 'cierre_handle_anterior' if reason == 'preparar_stream'
                  else 'cola_transicion' if reason == 'fin_escena' else 'parada_o_otra_voz')
        tail = dict(t_fin_fundido_nominal=None, fin_nativo_confirmado=False)
        if status == 'cola_transicion':
            nominal = Fraction(fade_ticks() * 6, 5)
            tail.update(t_fin_fundido_nominal=float(Fraction(str(later['t'])) + nominal),
                        exceso_sobre_fundido_nominal_u=float(over - nominal))
        rows.append(dict(adx=e['adx_jp'], canal=ch, inicio=e['t'], limite_spx=later['t'], exceso_u=float(over),
                         razon=reason, estado=status, muestras=info['samples'], hz=info['sample_rate'],
                         pc_inicio=e['pc_siguiente'], pc_limite=later['pc_siguiente'], **tail))
    return rows


def generate(args, tx, en, jp, elf, evidence, ids):
    out = args.salida.resolve()
    if not out.is_relative_to(FACTORY) or out.exists() or not out.parent.is_dir():
        raise ValueError('Usar directorio nuevo dentro de fabrica/TR1 con padre existente')
    stat = args.iso.stat()
    digest = base.iso_digest(args.iso)
    if digest != ISO_SHA:
        raise ValueError('ISO distinta del corpus PAL EN contrastado')
    read = lru_cache(maxsize=4)(en.read)
    packages = lru_cache(None)(lambda n: base.children(read(n)))
    voice = lru_cache(None)(lambda n: base.adx_info(jp.read(n, 16)))

    @lru_cache(None)
    def model_entry(n):
        data = en.read(n)
        return model_info(data) if data.startswith(b'#AMO0') else None

    @lru_cache(None)
    def camera(n, child_index, clip):
        child = packages(n)[child_index]
        data = read(n)[child['off']:child['off'] + child['tam']]
        result = base.tracks(data, clip)
        pos, = struct.unpack_from('<I', data, 0x14)
        off, = struct.unpack_from('<I', data, pos + 16 * clip + 12)
        roll, = struct.unpack_from('<I', data, off + 8) if off else (0,)
        return dict(cam_roll=keys(data, roll, 'If'), **result)

    Scene = generator(tx, packages, voice, elf)
    out.mkdir()
    table, summary, count, all_audio = {}, [], Counter(), []
    for n in ids[b'#SPX']:
        script = read(n)
        calls, gaps = census(tx.V, script)
        scene = Scene(script)
        scene.correr(10000)
        if not hasattr(scene.m, 'terminada') or getattr(scene.m, 'perdidos', 0):
            gaps.append((None, 'cierre o ranuras VM'))
        for i in sorted(set(calls.values()) | scene.cuenta.keys(), key=lambda i: -1 if i is None else i):
            ev = evidence(i)
            if i is None or not ev['handler'] or i not in base.TRANSLATED | base.EXTRA | NUEVOS or not (ev['b1_c_line'] or i in C_REFS):
                continue
            table[i] = dict(**ev, ref_lectura=C_REFS.get(i), dispatch='95193–95197',
                            modo='C' if ev['b1_c_line'] else 'ELF + ancla C')
        if scene.sin:
            gaps.append((None, 'builtins dinámicos sin traducir'))
        sites = [[pc, i] for pc, i in sorted(calls.items())]
        start, = struct.unpack_from('<I', script, 0x14)
        static_next = {tx.V.decodificar(script, start + pc)[2] - start: i for pc, i in sites}
        if any(static_next.get(t['pc']) != t['id'] for t in scene.m.traza):
            gaps.append((None, 'traza no reconciliada con censo'))
        actors, shots = defaultdict(lambda: dict(recursos=[], cargas=[])), []
        skies = []
        for amb in dict.fromkeys(scene.loaded):
            for child in packages(amb):
                if child['tipo'] in (0, 1, 3, 4) or child['tipo'] == 2 and child['sub'] == 0:
                    item = dict(amb=amb, **child)
                    if child['tipo'] in (0, 1) and child['magic'] == '#AMO':
                        item.update(model_info(read(amb)[child['off']:child['off']+child['tam']]))
                        if item['cielo']:
                            skies.append(dict(amb=amb, hijo=child['hijo'], actor=child['ranura'], raiz=item['raiz']))
                    actors[child['ranura']]['recursos'].append(item)
        for event in scene.ev:
            if event['tipo'] in ('carga_modelo', 'carga_textura'):
                actors[event['actor']]['cargas'].append(event)
                if event['tipo'] == 'carga_modelo' and event['entrada'] is not None:
                    model = model_entry(event['entrada'])
                    if model is not None:
                        event.update(model)
                        if event['cielo']:
                            skies.append(dict(entrada=event['entrada'], actor=event['actor'], raiz=event['raiz']))
            if event['tipo'] == 'camara' and event['largo'] is not None:
                shots.append(dict(t=event['t'], canal=event['canal'], amb=event['amb'], hijo=event['hijo'],
                                  **camera(event['amb'], event['hijo'], event['clip'])))
        audio = audio_windows(scene.ev)
        all_audio.extend(dict(spx=n, **row) for row in audio)
        count.update(row['estado'] for row in audio)
        stat_n = sum(i in table for i in calls.values())
        dyn_n = sum(v for i, v in scene.cuenta.items() if i in table and i not in scene.sin)
        coverage = dict(estaticas=len(calls), estaticas_traducidas=stat_n,
                        ejecutadas=sum(scene.cuenta.values()), ejecutadas_traducidas=dyn_n)
        row = dict(spx=n, **coverage, voces=len(audio), diagnosticos=dict(scene.diagnostics), huecos=gaps)
        summary.append(row)
        base.write_json(out / f'escena_{n}.json', dict(schema='tr1/pal-en/1', spx=n, iso_sha256=digest,
                        spx_sha256=hashlib.sha256(script).hexdigest(), idioma=3, unidades_por_segundo=60,
                        unidades_por_cuadro_pal=1.2, duracion_unidades=round(scene.m.reloj, 1), cuadros=scene.m.cuadro,
                        cierre='fin_escena' if hasattr(scene.m,'terminada') else 'sin_cierre', paquetes=list(dict.fromkeys(scene.loaded)),
                        cobertura=coverage, sitios=sites, huecos=gaps, diagnosticos=dict(scene.diagnostics),
                        actores=[dict(ranura=i, **a) for i,a in sorted(actors.items())], cielos=skies, camaras=shots,
                        eventos=scene.ev, voces_tr1=audio, llamadas_ganchos=dict(scene.hook_calls)))
        print(n, coverage, dict(scene.diagnostics), gaps, flush=True)
    ambs = {}
    for n in ids[b'#AMB']:
        data = read(n)
        rows = []
        for item in packages(n):
            row = dict(item)
            child = data[row['off']:row['off']+row['tam']]
            row['sha256'] = hashlib.sha256(child).hexdigest()
            if row['magic'] in ('#AML', '#AMP'):
                row['pistas'] = bank(child, row['magic'])
            rows.append(row)
        ambs[n] = rows
    after = args.iso.stat()
    if (stat.st_size, stat.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError('ISO modificada durante la lectura')
    base.write_json(out / 'indice.json', dict(iso_sha256=digest, elf_sha256=ELF_SHA,
                    b1_c_sha256=hashlib.sha256(args.decomp.read_bytes()).hexdigest(), spx=ids[b'#SPX'], amb=ambs,
                    builtins=table, resumen=summary))
    base.write_json(out / 'audio.json', dict(resumen=dict(count), voces=all_audio))
    print(dict(count))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', required=True, type=Path)
    parser.add_argument('--opus', default=FACTORY.parents[1] / 'estudio_b1/opus', type=Path)
    parser.add_argument('--decomp', default=FACTORY.parents[1] / 'ghidra/salida/b1.c', type=Path)
    parser.add_argument('--salida', required=True, type=Path)
    args = parser.parse_args()
    generate(args, *source(args.iso, args.opus, args.decomp))


if __name__ == '__main__':
    main()
