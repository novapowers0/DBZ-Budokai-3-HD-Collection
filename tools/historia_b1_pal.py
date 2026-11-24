"""Audita desde la ISO PAL los prólogos, títulos y primeros combates por idioma.

Salida JSON opcional fuera del proyecto. La VM modela un camino sin mando ni
combate real; un primer combate encontrado sigue siendo un candidato.
"""
import argparse
from collections import Counter
from functools import lru_cache
from hashlib import sha256
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from historia_b1_1910 import (ROOT, TRANSLATED, adx_info, afs_entry, census,
                              first_fights, pal_elf_bytes)


def script_queue(source, index, lang):
    scene = source(index, None, lang)
    scene.correr(6)
    return dict(spx=index, frames=scene.m.cuadro,
                queued=[r['a'][1::-1] for r in scene.m.traza if r['id'] == 0xd0],
                alternative=[r['a'][1::-1] for r in scene.m.traza if r['id'] == 0xd1])


def fight_queue(route, lang, source):
    """Only model the initial queue in the script that starts the fight."""
    if route['status'] != 'vm_candidate':
        return None
    queue = script_queue(source, route['path'][-1], lang)
    # D0 appends to the queue (FUN_002171d0); D1 sets its abort/retry target
    # independently of it (FUN_00217260). See FUN_002173e0, case 1.
    expected = [9, route['argument']]
    return dict(status='observed_prefix' if queue['queued'][:1] == [expected] else 'unresolved', **queue)


def follow_queue(first, read, probe, fights, starts):
    """Walk D0 prefixes, assuming each combat and interlude advances the queue.

    This does not simulate a victory, the alternative target, or the real
    screen tasks. Stop at menu/join/loop instead of consuming another chapter.
    """
    steps, scripts, records = [], [], []
    if not first or first['status'] != 'observed_prefix':
        return dict(status='no_prefix', steps=steps, scripts=scripts, records=records)
    pending = list(first['queued'])
    seen = {first['spx']}
    status = 'empty_prefix'
    for _ in range(32):
        if not pending:
            break
        screen, resource = pending.pop(0)
        steps.append([screen, resource])
        if screen == 9:
            record = resource if resource >= 0 else 30 - resource
            if not 0 <= record < len(fights):
                status = 'invalid_fight'
                break
            records.append(record)
        elif screen == 8:
            if resource in starts or resource in seen:
                status = 'chapter_or_cycle'
                break
            seen.add(resource)
            if read(resource)[:4] != b'#SPX':
                status = 'not_spx'
                break
            part = probe(resource)
            scripts.append(part)
            pending.extend(part['queued'])
        elif screen in (6, 5):
            status = 'menu_or_return'
            break
        elif screen not in (10, 0x21):
            status = 'unsupported_screen'
            break
    else:
        status = 'step_limit'
    return dict(status=status, steps=steps, scripts=scripts, records=records)


def audit(iso_path, opus):
    sys.path[:0] = [str(opus), str(ROOT / 'mod center hd'), str(ROOT / 'awo_tools')]
    from iso import Iso
    from amt_ps2 import decode, entries
    import spx_a_escena as tx

    iso = Iso(iso_path)
    boot = iso.boot_elf()
    if not boot or Path(boot).name.lower() != 'sles_512.33':
        raise ValueError('Solo PAL SLES_512.33')
    elf = iso.read(boot)
    # FUN_00217090 elige DATA por el índice de idioma, no por el orden del menú.
    ptrs = struct.unpack('<8I', pal_elf_bytes(elf, 0x34ff40, 32))
    names = {}
    for lang, ptr in enumerate(ptrs):
        if not ptr:
            continue
        name = pal_elf_bytes(elf, ptr, 32).split(b'\0', 1)[0].decode('ascii').rsplit('/', 1)[-1].upper()
        if name.startswith('DATA_') and name.endswith('.AFS') and iso.find(name):
            names[lang] = name
    if set(names.values()) != {f'DATA_{lang}.AFS' for lang in ('EN', 'SP', 'FR', 'GE', 'IT')}:
        raise ValueError(f'Tabla PAL de idiomas inesperada: {names}')

    def texture_hash(amt, tex):
        img = decode(amt, tex)
        return sha256(struct.pack('<II', tex['w'], tex['h']) + img.tobytes()).hexdigest()

    @lru_cache(maxsize=128)
    def duration(index):
        return round(adx_info(afs_entry(iso, 'ADX_JP.AFS', index))['seconds'], 3)

    results = {}
    for lang, bank in names.items():
        @lru_cache(maxsize=24)
        def read(index):
            return afs_entry(iso, bank, index)

        chapters, fights = census(elf, read)
        original = tx.V.leer_entrada, tx.duracion_adx
        tx.V.leer_entrada, tx.duracion_adx = read, duration
        source = tx.Traductor
        # El censo anterior fija idioma 3. Fijar aquí el índice que selecciona
        # este banco en el ELF, incluso en los guiones que consultan 0xEA.
        tx.Traductor = lambda spx, amb, idioma: source(spx, amb, lang)
        try:
            scene = tx.Traductor(1910, 1909, lang)
            scene.correr(4200)
            if getattr(scene.m, 'terminada', None) is None:
                raise ValueError(f'Sin fin de 1910 en {bank}')
            routes = first_fights(chapters, fights, tx, read, scene)
            queues = [fight_queue(route, lang, source) for route in routes]
            probe = lru_cache(maxsize=128)(lambda index: script_queue(source, index, lang))
            starts = {ch['spx'] for ch in chapters}
            follow = [follow_queue(queue, read, probe, fights, starts) for queue in queues]
        finally:
            tx.V.leer_entrada, tx.duracion_adx = original
            tx.Traductor = source

        rows = []
        for chapter, path, queue, continued in zip(chapters, routes, queues, follow, strict=True):
            _, _, calls = tx.V.censo_estatico(read(chapter['spx']))
            row = dict(**chapter, route=path, first_combat_queue=queue, continuation_candidate=continued, static=len(calls),
                       static_translated=sum(i in TRANSLATED for _, i in calls))
            if 'title_amt' in chapter:
                if chapter['title_amt'] != 2375 + chapter['chapter']:
                    raise ValueError(f'Orden de títulos no comprobado en {bank}')
                amt = read(chapter['title_amt'])
                textures = entries(amt, keep_empty=True)
                if len(textures) != 1 or textures[0] is None:
                    raise ValueError(f'Título mal formado {bank}:{chapter["title_amt"]}')
                row['title_sha256_rgba'] = texture_hash(amt, textures[0])
            rows.append(row)

        sprite = [h for h in scene.hijos if (h['tipo'], h['ranura'], h['sub']) == (2, 0, 1)]
        if len(sprite) != 1:
            raise ValueError(f'Sprite 0 ambiguo en {bank}')
        offset, size = sprite[0]['off'], sprite[0]['tam']
        amt = read(1909)[offset:offset + size]
        textures = entries(amt, keep_empty=True)
        subs = [dict(t=e['t'], id=e['imagen'], sha256_rgba=texture_hash(amt, textures[e['imagen']]))
                for e in scene.ev if e['tipo'] == 'texto_imagen' and e['sprite'] == 0]
        results[bank] = dict(language_index=lang, chapters=rows, subtitles_1910=subs,
                             executed_1910=sum(scene.cuenta.values()),
                             untranslated_1910=sum(scene.sin.values()))
        read.cache_clear()

    en = results['DATA_EN.AFS']
    for bank, outcome in results.items():
        rows = outcome['chapters']
        print(bank, 'idioma', outcome['language_index'],
              dict(Counter(r['route']['status'] for r in rows)),
              'rutas iguales EN', sum(a['route'] == b['route'] for a, b in zip(rows, en['chapters'])),
              'colas iguales EN', sum(a['first_combat_queue'] == b['first_combat_queue']
                                      for a, b in zip(rows, en['chapters'])),
              'continuaciones iguales EN', sum(a['continuation_candidate'] == b['continuation_candidate']
                                               for a, b in zip(rows, en['chapters'])),
              'finales candidatos', dict(Counter(r['continuation_candidate']['status'] for r in rows)),
              'títulos', sum('title_sha256_rgba' in r for r in rows),
              'títulos distintos EN', sum(a.get('title_sha256_rgba') != b.get('title_sha256_rgba')
                                         for a, b in zip(rows, en['chapters'])),
              'subtítulos 1910', len(outcome['subtitles_1910']),
              'subtítulos distintos EN', sum(a['sha256_rgba'] != b['sha256_rgba'] for a, b in
                                            zip(outcome['subtitles_1910'], en['subtitles_1910'])),
              'estáticas', sum(r['static_translated'] for r in rows), '/', sum(r['static'] for r in rows))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--opus', type=Path, required=True)
    parser.add_argument('--salida', type=Path)
    args = parser.parse_args()
    if args.salida and args.salida.resolve().is_relative_to(ROOT):
        parser.error('La salida debe estar fuera del proyecto')
    result = audit(args.iso, args.opus)
    if args.salida:
        with args.salida.open('x', encoding='utf-8') as stream:
            json.dump(result, stream, indent=2, allow_nan=False)


if __name__ == '__main__':
    main()
