"""Genera la caché local de Saiyan 1 (PAL) desde la ISO del jugador y la VM Opus R23.

Requiere Python 3.11+, numpy y Pillow. Véase ESTUDIO_HISTORIA_B1, Avance Sol.
La salida es una carpeta nueva fuera del árbol del proyecto. El TOML es un
intermedio para adaptar a cinematica::Paso, no un formato que B3 lea hoy.
"""
import argparse
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tomllib
from collections import Counter


ROOT = Path(__file__).resolve().parents[1]
SPX, AMB = 1910, 1909
RATE = 60  # hipótesis PAL: 50 Hz × 1,2 unidades por cuadro
TRANSLATED = frozenset(int(x, 16) for x in (
    '125 120 121 90 EA 09 08 28 2A 20 22 B1 B2 B3 B5 B6 B7 13 30 31 3E 36 37 3D '
    '38 33 3A 50 60 62 63 70 71 72 73 40 41 43 45 47 8A 84 83 85 D7 D3 D0 02 03'
).split())


def afs_entry(iso, name, index, max_size=64 << 20):
    path = iso.find(name)
    if path is None:
        raise ValueError(f'Falta {name} en la ISO')
    _, size = iso.files()[path]
    magic, count = struct.unpack('<4sI', iso.read(path, 0, 8))
    if magic != b'AFS\0' or not 0 <= index < count or 8 + 8 * count > size:
        raise ValueError(f'Índice {index} no válido en {name}')
    offset, length = struct.unpack('<II', iso.read(path, 8 + 8 * index, 8))
    if not 0 < length <= max_size or offset < 8 + 8 * count or offset + length > size:
        raise ValueError(f'Rango inválido para {name}:{index}')
    data = iso.read(path, offset, length)
    if len(data) != length:
        raise ValueError(f'Lectura incompleta de {name}:{index}')
    return data


def adx_info(data):
    if len(data) < 16 or data[:2] != b'\x80\x00':
        raise ValueError('Cabecera ADX inválida')
    rate, samples = struct.unpack_from('>II', data, 8)
    if not rate or not samples:
        raise ValueError('ADX sin frecuencia o sin muestras')
    return {'sample_rate': rate, 'samples': samples, 'seconds': samples / rate}


def tracks(amc, clip):
    count, table = struct.unpack_from('<II', amc, 0x10)
    if amc[:4] != b'#AMC' or not 0 <= clip < count:
        raise ValueError('Clip de cámara inválido')
    frames, offset = struct.unpack_from('<II', amc, table + 16 * clip + 8)
    result = {'clip': clip, 'frames': frames}
    # Mismo formato que LeerCamaraCruda; se conserva el marco de B1 y el FOV original.
    for idx, dim, name in [(0, 3, 'cam_ojo'), (1, 3, 'cam_obj'), (3, 1, 'cam_fov')]:
        ptr, = struct.unpack_from('<I', amc, offset + 4 * idx) if offset else (0,)
        n, = struct.unpack_from('<I', amc, ptr + 8) if ptr else (0,)
        stride = 4 * (1 + dim)
        if n > 8192 or ptr and ptr + 12 + n * stride > len(amc):
            raise ValueError('Pista #AMC fuera de límites')
        keys = [struct.unpack_from('<I' + 'f' * dim, amc, ptr + 12 + stride * k) for k in range(n)]
        if any(not all(map(math.isfinite, key)) for key in keys):
            raise ValueError('Pista #AMC no finita')
        result[name] = keys
    return result


def pal_elf_bytes(elf, va, size):
    """Traduce por PT_LOAD (ELF32 MIPS LE), no por un desplazamiento supuesto."""
    if elf[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('ELF de B1 no reconocido')
    table, = struct.unpack_from('<I', elf, 0x1c)
    stride, count = struct.unpack_from('<HH', elf, 0x2a)
    for n in range(count):
        typ, off, base, _, file_size = struct.unpack_from('<5I', elf, table + n * stride)
        if typ == 1 and base <= va and va + size <= base + file_size:
            pos = off + va - base
            if pos + size <= len(elf):
                return elf[pos:pos + size]
    raise ValueError(f'VA de B1 no respaldada por fichero: {va:#x}')


def census(elf, read):
    ids = struct.unpack('<29H', pal_elf_bytes(elf, 0x34f420, 58))
    if len(set(ids)) != len(ids) or any(read(i)[:4] != b'#SPX' for i in ids):
        raise ValueError('Tabla PAL Contents no reconocida')
    records = pal_elf_bytes(elf, 0x35ca40, 43 * 128)
    fights = []
    for index in range(43):
        r = records[index * 128:(index + 1) * 128]
        if r[0x0f] != 0x3f or r[0x70] not in (0, 6, 7):
            raise ValueError(f'Registro PAL no reconocido: {index}')
        fights.append(dict(index=index, player=r[0x20], chapter=struct.unpack_from('<H', r, 0x28)[0],
                           opponent=r[0x50], opponent_form=r[0x52], arena=r[0x71], mode=r[0x70]))
    return [dict(chapter=i + 1, spx=n) for i, n in enumerate(ids)], fights


def first_fights(chapters, fights, tx, read, scene):
    """Censo de caminos observados en la VM; el combate, el mando y los stubs no son el B1 real."""
    starts = {ch['spx'] for ch in chapters}
    cache = {}

    def trace(index):
        if index not in cache:
            if index == SPX:
                probe = scene
            else:
                probe = tx.Traductor(index, None, idioma=3)
                probe.correr(2)
                packages = {r['a'][0] for r in probe.m.traza if r['id'] == 9}
                amb = next(iter(packages)) if len(packages) == 1 else None
                if amb is not None and read(amb)[:4] != b'#AMB':
                    raise ValueError('Recurso previo al combate no es #AMB')
                probe = tx.Traductor(index, amb, idioma=3)
                probe.m.lanzar(0)
                while probe.m.vivo() and probe.m.cuadro < 4200 and getattr(probe.m, 'terminada', None) is None:
                    prev = len(probe.ev)
                    probe.m.cuadro_()
                    # El primer lote que encola una pantalla no autoriza
                    # simular el resto como si la transición no existiera.
                    if any(e['tipo'] == 'siguiente' and e['pantalla'] in (8, 9) for e in probe.ev[prev:]):
                        break
            edges = list(dict.fromkeys((e['pantalla'], e['recurso']) for e in probe.ev if e['tipo'] == 'siguiente'))
            loaded = [row['a'][0] for row in probe.m.traza if row['id'] == 0x2c and row['a'][2] == 0]
            cache[index] = edges, loaded
        return cache[index]

    results = []
    for ch in chapters:
        index = ch['spx']
        path = []
        result = dict(chapter=ch['chapter'], path=path, status='depth_limit')
        results.append(result)
        for _ in range(6):
            path.append(index)
            try:
                edges, loaded = trace(index)
            except (IndexError, KeyError, ValueError, struct.error, OverflowError, ZeroDivisionError, RuntimeError) as err:
                result.update(status='vm_unsupported', diagnostic=str(err))
                break
            if index == 2457 + ch['chapter'] - 1 and len(loaded) == 1:
                if read(loaded[0])[:4] == b'#AMT':
                    ch.update(title_spx=index, title_amt=loaded[0])
            combat = {n for screen, n in edges if screen == 9}
            if len(combat) == 1:
                argument = next(iter(combat))
                # FUN_00196140, modo historia: cuando es negativo toma
                # VA 0x35dac0 - (arg + 3)*0x80, es decir, fila 30 - arg.
                record = argument if argument >= 0 else 30 - argument
                if 0 <= record < len(fights):
                    result.update(status='vm_candidate', argument=argument, fight=record,
                                  player=fights[record]['player'], opponent=fights[record]['opponent'])
                    break
            following = {n for screen, n in edges if screen == 8 and n > 0}
            if combat or len(following) != 1:
                result['status'] = 'ambiguous' if combat or len(following) > 1 else 'unresolved'
                break
            index = next(iter(following))
            if index in starts or index in path:
                result['status'] = 'reaches_menu_chapter_or_cycle'
                break
    return results


def toml_records(metadata, groups):
    # Sólo escalares/listas JSON permitidos también en TOML; los None quedan ausentes.
    def fields(row):
        for key, value in row.items():
            if value is not None:
                yield f'{key} = {json.dumps(value, ensure_ascii=False, allow_nan=False)}'

    lines = list(fields(metadata))
    for name, rows in groups.items():
        for row in rows:
            lines += ['', f'[[{name}]]', *fields(row)]
    result = '\n'.join(lines) + '\n'
    tomllib.loads(result)
    return result


def generate(iso_path, out, opus, hd_afs, audit_chapters, samples):
    for base in (opus, ROOT / 'mod center hd', ROOT / 'awo_tools', ROOT / 'tools/b3hd_conv'):
        sys.path.insert(0, str(base))
    from iso import Iso
    import spx_a_escena as tx
    import amt_ps2
    import retarget as rt
    from PIL import Image, ImageDraw

    iso = Iso(iso_path)
    boot = iso.boot_elf()
    if not boot or Path(boot).name.lower() != 'sles_512.33':
        raise ValueError('Sólo está comprobada la edición PAL SLES_512.33')

    @lru_cache(maxsize=30)
    def en(index):
        return afs_entry(iso, 'DATA_EN.AFS', index)

    def voice(index):
        return afs_entry(iso, 'ADX_JP.AFS', index)

    before = iso_path.stat()
    script, package = en(SPX), en(AMB)
    if script[:4] != b'#SPX' or package[:4] != b'#AMB':
        raise ValueError('La ISO no contiene la pareja de escena esperada')
    chapters, fights = census(iso.read(boot), en)
    audio = {}

    def duration(index):
        if index not in audio:
            audio[index] = adx_info(voice(index))
        return round(audio[index]['seconds'], 3)

    # La versión R23 tiene rutas fijas: sustituimos únicamente sus lectores dentro
    # de este proceso y los restauramos al acabar. No se escribe sobre Opus.
    read0, duration0 = tx.V.leer_entrada, tx.duracion_adx
    try:
        tx.V.leer_entrada, tx.duracion_adx = en, duration
        scene = tx.Traductor(SPX, AMB, idioma=3)
        scene.correr()
        # 1910 -> título 2457 -> introducción 949 -> combate. Sólo el
        # prólogo de los dos guiones siguientes: no simulamos el combate.
        title = tx.Traductor(2457, None, idioma=3)
        title.correr(2)
        pre_fight = tx.Traductor(949, None, idioma=3)
        pre_fight.correr(2)
        chap_audit = first_fights(chapters, fights, tx, en, scene) if audit_chapters else []
    finally:
        tx.V.leer_entrada, tx.duracion_adx = read0, duration0
    if getattr(scene.m, 'terminada', None) is None:
        raise ValueError('El traductor no alcanzó el fin de la escena')
    def next_screens(trace):
        return [(e['pantalla'], e['recurso']) for e in trace.ev if e['tipo'] == 'siguiente']

    if (8, 2457) not in next_screens(scene) or (8, 949) not in next_screens(title) or (9, 1) not in next_screens(pre_fight):
        raise ValueError('No se confirmó la cadena PAL del primer combate')
    title_calls = [row['a'][0] for row in title.m.traza if row['id'] == 0x2c and row['a'][2] == 0]
    if title_calls != [2376] or (fights[1]['player'], fights[1]['opponent']) != (0, 9):
        raise ValueError('No se confirmó el título o el primer combate PAL')
    chapters[0].update(title_spx=2457, title_amt=title_calls[0], pre_fight_spx=949, first_fight=1)
    title_amt = en(title_calls[0])
    _, _, static = tx.V.censo_estatico(script)
    coverage = dict(static=len(static), static_translated=sum(i in TRANSLATED for _, i in static),
                    executed=sum(scene.cuenta.values()), untranslated=sum(scene.sin.values()))

    digest = hashlib.sha256()
    with iso_path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 << 20), b''):
            digest.update(block)
    after = iso_path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError('Cambió la ISO durante la lectura')

    children = {(h['tipo'], h['ranura'], h['sub']): h for h in scene.hijos}

    def child(ty, slot, sub=0):
        h = children[ty, slot, sub]
        if h['off'] + h['tam'] > len(package):
            raise ValueError('Hijo #AMB fuera de límites')
        return package[h['off']:h['off'] + h['tam']]

    external = {field: {e['actor']: e['entrada'] for e in scene.ev if e['tipo'] == 'carga_' + field
                        and e['entrada'] is not None} for field in ('modelo', 'textura')}
    slots = sorted({slot for ty, slot, _ in children if ty == 1} | set(external['modelo']))
    amc = child(5, 0)
    shots = [dict(t=e['t'], **tracks(amc, e['clip'])) for e in scene.ev if e['tipo'] == 'camara']
    voices = [dict(t=e['t'], channel=e['canal'], entry=e['adx_jp'], **audio[e['adx_jp']])
              for e in scene.ev if e['tipo'] == 'voz']
    if [s['clip'] for s in shots] != list(range(14)) or [v['entry'] for v in voices] != list(range(699, 706)):
        raise ValueError('Cámaras o voces de la escena no coinciden con la secuencia comprobada')
    for i, v in enumerate(voices):
        end = voices[i + 1]['t'] if i + 1 < len(voices) else scene.m.reloj
        v['gap_seconds'] = round((end - v['t']) / RATE - v['seconds'], 4)

    out.mkdir()  # no mezcla resultados parciales ni pisa otro trabajo
    summary = dict(schema=1, spx=SPX, amb=AMB, iso_sha256=digest.hexdigest(), iso_size=after.st_size,
                   units_per_second=RATE, duration_units=round(scene.m.reloj, 1), runtime_verified=False)
    actors, clips = [], []
    for slot in slots:
        model_entry, texture_entry = (external[f].get(slot) for f in ('modelo', 'textura'))
        model = en(model_entry) if model_entry is not None else child(1, slot)
        texture = en(texture_entry) if texture_entry is not None else child(2, slot)
        name = f'actor_{slot:02}'
        (out / (name + '.amo')).write_bytes(model)
        (out / (name + '.amt')).write_bytes(texture)
        bones = rt.esqueleto(model)
        actor = dict(slot=slot, model_entry=model_entry, texture_entry=texture_entry,
                     body=bones[0][0] if bones else '', model=name + '.amo', texture=name + '.amt')
        actors.append(actor)
        for ty, index, sub in sorted(children):
            if index != slot or ty not in (3, 4):
                continue
            ext = 'amm' if ty == 3 else 'amp'
            filename = f'{name}_{sub}.{ext}'
            (out / filename).write_bytes(child(ty, slot, sub))
            clips.append(dict(actor=slot, bank=sub, kind=ext, file=filename,
                              frames=children[ty, slot, sub]['clips']))

    def decode(amt, tex):
        return Image.fromarray(amt_ps2.decode(amt, tex))

    decode(title_amt, amt_ps2.entries(title_amt)[0]).save(out / 'titulo.png')
    def sheet(label, values):
        image = Image.new('RGB', (600, 135 * ((len(values) + 4) // 5)), '#303039')
        draw = ImageDraw.Draw(image)
        for n, (text, img) in enumerate(values):
            x, y = 120 * (n % 5), 135 * (n // 5)
            img.thumbnail((116, 111))
            image.paste(img, (x, y), img)
            draw.text((x, y + 111), text, fill='white')
        image.save(out / label)

    for a in actors:
        if a['texture_entry'] is not None:
            amt = (out / a['texture']).read_bytes()
            textures = [(str(i), decode(amt, tex)) for i, tex in enumerate(amt_ps2.entries(amt, keep_empty=True))
                        if tex and min(tex['w'], tex['h']) >= 16]
            if textures:
                sheet(f"hoja_{a['slot']:02}.png", textures)

    # FUN_0015ce70 -> FUN_0015b990 instala tipo 2/sub!=0 en la tabla de sprites.
    # 0x41 indexa la tabla de ese AMT (b1.elf:0x158218–0x15826c).
    subtitle_amt = child(2, 0, 1)
    textures = amt_ps2.entries(subtitle_amt, keep_empty=True)
    subtitles = []
    for e in scene.ev:
        if e['tipo'] != 'texto_imagen' or e['sprite'] != 0:
            continue
        idx = e['imagen']
        if not 0 <= idx < len(textures) or textures[idx] is None:
            raise ValueError(f'Subtítulo fuera de la hoja: {idx}/{len(textures)}')
        filename = f'subtitulo_{idx:02}.png'
        if not (out / filename).exists():
            decode(subtitle_amt, textures[idx]).save(out / filename)
        subtitles.append(dict(t=e['t'], image=idx, draw_argument=e['celda'], file=filename))

    retargeted = export_hd(hd_afs, out, actors, clips, scene.ev, rt, samples) if hd_afs else []
    (out / 'camaras.amc').write_bytes(amc)
    events = Counter(e['tipo'] for e in scene.ev)
    payload = dict(**summary, coverage=coverage, counts=events, events=scene.ev,
                   unsupported={hex(k): v for k, v in scene.sin.items()}, audio=audio)
    (out / 'escena_1910.json').write_text(json.dumps(payload, indent=2, allow_nan=False), encoding='utf-8')
    (out / 'escena_1910.toml').write_text(toml_records(summary, dict(
        actores=actors, clips=clips, camaras=shots, voces=voices, subtitulos=subtitles, retarget=retargeted,
        eventos=scene.ev)),
        encoding='utf-8')
    (out / 'capitulos_pal.toml').write_text(toml_records(summary, dict(
        capitulos=chapters, combates=fights, rutas_vm=chap_audit)), encoding='utf-8')
    print(f'{len(actors)} actores, {len(shots)} cámaras, {len(voices)} voces, {len(subtitles)} cambios de subtítulo, '
          f'{len(retargeted)} clips HD; {coverage}; {sum(v["seconds"] for v in voices):.3f} s de voz')
    if chap_audit:
        print('Candidatos por capítulo:', dict(Counter(r['status'] for r in chap_audit)))


def export_hd(hd_afs, out, actors, clips, events, rt, samples):
    import afs_pair as afs
    from awg_vertex_buffer import qmat
    sys.path.insert(0, str(ROOT / 'mod center hd/cac'))
    import vista_b2 as hd_view
    # Bin de data_cmn (no ID de luchador). Elegido por identidad y atuendo B1.
    targets = {6: 264, 7: 352, 8: 264, 9: 246, 10: 360, 11: 352, 12: 327, 13: 110}
    retargeted = []
    decoded = {}
    for a in actors:
        slot = a['slot']
        if slot not in targets:
            continue
        bin_id = targets[slot]
        if bin_id not in decoded:
            raw = afs.entry(str(hd_afs), bin_id)
            digest = hashlib.sha256(raw).hexdigest()
            if raw.startswith(afs.LZX_MAGIC):
                work = out / 'fuentes_hd'
                work.mkdir(exist_ok=True)
                compressed, decompressed = work / f'{bin_id}.lzx', work / f'{bin_id}.amb'
                compressed.write_bytes(raw)
                subprocess.run([afs.XBDEC, str(compressed), str(decompressed)], check=True,
                               capture_output=True, timeout=30)
                raw = decompressed.read_bytes()
            dst = rt.esqueleto(raw)
            mesh = raw
            if raw[:4] == b'#AMB':
                awo, = struct.unpack_from('>I', raw, 0x20)
                if awo > 0x40:
                    mesh = raw[awo - 0x40:]
            model = hd_view.vh.mr.Model(mesh)
            if tuple(model.parent) != tuple(bone[3] for bone in dst) or [
                n.lstrip('X') for n in model.labels
            ] != [bone[0].lstrip('X') for bone in dst]:
                raise ValueError(f'Orden de huesos distinto entre retarget y malla de HD {bin_id}')
            skin = f'sk_{bin_id}.sk'
            hd_view.exportar_sk(model, str(out / skin))
            decoded[bin_id] = dst, digest, skin, mesh
        src = rt.esqueleto((out / a['model']).read_bytes())
        dst, digest, skin, mesh = decoded[bin_id]
        a.update(hd_bin=bin_id, hd_skin=skin)
        # Conserva los nombres originales para leer las claves y escribir el ACM.
        # Sólo la comparación ignora el prefijo de personaje, incluso XGOK_* vs GOK_*.
        def suffix_view(skeleton):
            return [(name.partition('_')[2] or name, *rest) for name, *rest in skeleton]

        mapping, _ = rt.mapa_auto(suffix_view(src), suffix_view(dst))
        waist = next((j for j in mapping if dst[j][0].endswith('_WAIST')), None)
        if waist is None or not any(dst[j][0].endswith('_HEAD') for j in mapping):
            raise ValueError(f'Actor {slot}: no hay cadera/cabeza compatibles en HD {bin_id}')
        base = rt.locales(dst, {}, 0, False)
        played = sorted({(e['banco'], e['clip']) for e in events if e['tipo'] == 'animacion' and e['actor'] == slot})
        for bank, clip in played:
            info = next(c for c in clips if (c['actor'], c['bank'], c['kind']) == (slot, bank, 'amm'))
            anim, nf = rt.leer_anim((out / info['file']).read_bytes(), clip)
            if not 0 < nf <= 4096:
                raise ValueError(f'Duración anómala del clip {slot}/{bank}/{clip}')
            frames, _ = rt.retarget(src, anim, nf, dst, base, mapping, raiz=waist)
            if samples and (bank, clip) == played[0]:
                normalized = [(name.lstrip('X'), *rest) for name, *rest in dst]
                rt.render_destino(mesh, normalized, frames, [0, nf // 2], str(out / f'pose_hd_{slot:02}.png'), lado=180)
            data = rt.escribir_acm(dst, frames)
            _, checked = rt.leer_anim(data, 0)
            if checked != nf:
                raise ValueError('El #ACM generado no conserva la duración')
            filename = f'actor_{slot:02}_{bank}_{clip}.acm'
            (out / filename).write_bytes(data)
            # Para ps2::M34 / ps2::Escena: cuadro-major, en el orden del HMS3.
            # Evita que el reproductor tenga que volver a leer ángulos del #ACM.
            matrices = f'actor_{slot:02}_{bank}_{clip}.mat'
            packed = bytearray(struct.pack('<4sII', b'B1M1', nf, len(dst)))
            for frame in frames:
                for q, p in frame:
                    values = tuple(v for row in qmat(*q, *p)[:3] for v in row)
                    if not all(map(math.isfinite, values)):
                        raise ValueError(f'Matriz no finita: {slot}/{bank}/{clip}')
                    packed.extend(struct.pack('<12f', *values))
            (out / matrices).write_bytes(packed)
            retargeted.append(dict(actor=slot, bank=bank, source_clip=clip, hd_bin=bin_id, frames=nf,
                                   mapped=len(mapping), bones=len(dst), file=filename, matrices=matrices,
                                   hd_entry_sha256=digest))
    return retargeted


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iso', required=True, type=Path)
    p.add_argument('--opus', required=True, type=Path)
    p.add_argument('--salida', required=True, type=Path)
    p.add_argument('--hd-afs', type=Path, help='data_cmn.afs de la copia de B3 HD')
    p.add_argument('--censo-rutas', action='store_true', help='rastrear el primer combate de los 29 capítulos')
    p.add_argument('--muestras-hd', action='store_true', help='render estático de dos poses del primer clip por actor HD')
    args = p.parse_args()
    dest = args.salida.resolve()
    if dest.is_relative_to(ROOT) or dest.exists() or not dest.parent.is_dir():
        p.error('--salida debe ser una carpeta nueva fuera del proyecto, con padre existente')
    if args.muestras_hd and not args.hd_afs:
        p.error('--muestras-hd necesita --hd-afs')
    generate(args.iso.resolve(), dest, args.opus.resolve(), args.hd_afs, args.censo_rutas, args.muestras_hd)
