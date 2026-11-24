"""Verifica el censo PAL, las referencias de caché y el ajuste temporal de voces."""
import argparse
from collections import Counter
from fractions import Fraction
from functools import cache
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from historia_b1_escenas import Afs, Iso, ROOT, children, iso_digest, ratio


def load(path):
    def reject(x):
        raise ValueError(f'JSON no finito: {x}')
    return json.loads(path.read_bytes(), parse_constant=reject)


def check(cache_dir, iso_path):
    errors, rows, cuts = [], [], []
    iso = Iso(iso_path)
    en, jp = Afs(iso, 'DATA_EN.AFS'), Afs(iso, 'ADX_JP.AFS')
    print('Cotejando huella y entradas con la ISO PAL...', flush=True)
    digest = iso_digest(iso_path)

    @cache
    def samples(entry):
        magic, hz, n = struct.unpack('>H6xII', jp.read(entry, 16))
        if magic != 0x8000 or not hz or not n:
            raise ValueError(f'ADX_JP:{entry} inválido')
        return hz, n

    index = load(cache_dir / 'indice.json')
    spx = [i for i in range(1909, 1997) if en.read(i, 4) == b'#SPX']
    amb = {str(i) for i in range(1909, 1997) if en.read(i, 4) == b'#AMB'}
    if (len(spx), len(amb)) != (38, 50) or index['spx'] != spx or index['amb'].keys() != amb or index['iso_sha256'] != digest:
        errors.append('Censo/ISO no coincide')
    if {p.name for p in cache_dir.glob('escena_*.json')} != {f'escena_{i}.json' for i in spx}:
        errors.append('Ficheros inesperados o faltantes')
    for a in sorted(amb):
        if index['amb'].get(a) != children(en.read(int(a))):
            errors.append(f'AMB {a}: inventario distinto de la ISO')
    totals = Counter()
    used = set()
    for i in spx:
        s = load(cache_dir / f'escena_{i}.json')
        rows.append(s)
        if (s['spx'] != i or s['iso_sha256'] != digest or s['idioma'] != 3 or
                s['spx_sha256'] != hashlib.sha256(en.read(i)).hexdigest() or s['unidades_por_segundo'] != 60):
            errors.append(f'{i}: identidad o reloj incorrectos')
        e = s['eventos']
        cover = s['cobertura']
        totals.update(cover)
        if s['cierre'] != 'fin_escena' or not e or e[-1]['tipo'] != 'fin_escena' or e[-1]['t'] != s['duracion_unidades'] or s['diagnosticos']:
            errors.append(f'{i}: escena incompleta o diagnósticos')
        if any(a['t'] > b['t'] for a, b in zip(e, e[1:])) or any(x['t'] < 0 for x in e):
            errors.append(f'{i}: eventos desordenados')
        for kind, field in [('estaticas', 'estatica'), ('ejecutadas', 'ejecutada')]:
            a, b = cover[kind + '_traducidas'], cover[kind]
            if b <= 0 or a * 10 < b * 9 or a + sum(x[field] for x in s['desconocidos'].values()) != b:
                errors.append(f'{i}: cobertura {kind} por debajo del 90% o incoherente')
        if set(s['paquetes']) != {x['entrada'] for x in e if x['tipo'] == 'carga_amb' and x['entrada'] is not None}:
            errors.append(f'{i}: cargas AMB y paquetes no coinciden')
        used.update(s['paquetes'])
        actors = {x['ranura'] for x in s['actores']}
        loaded = {}
        prepared = {}
        for x in e:
            if x['tipo'] == 'carga_amb' and x['entrada'] is not None:
                for h in index['amb'][str(x['entrada'])]:
                    loaded[h['tipo'], h['ranura'], h['sub']] = (x['entrada'], h['hijo'])
            if x['tipo'] == 'preparar_stream':
                prepared[x['canal']] = x['adx_jp']
            if x['tipo'] in ('voz', 'musica') and prepared.get(x['canal']) != x['adx_jp']:
                errors.append(f'{i}: reproducción no coincide con preparación')
            if x['tipo'] == 'texto_imagen' and x.get('amb') is not None:
                h = index['amb'][str(x['amb'])][x['hijo']]
                if ((h['tipo'], h['ranura'], h['magic']) != (2, x['sprite'], '#AMT') or not h['sub'] or
                        loaded.get((2, h['ranura'], h['sub'])) != (x['amb'], x['hijo'])):
                    errors.append(f'{i}: referencia de sprite incoherente')
            if x['tipo'] in ('animacion', 'animacion_amp', 'camara', 'luz'):
                h = index['amb'][str(x['amb'])][x['hijo']]
                slot = x['actor'] if 'actor' in x else x['canal']
                ty = {'animacion': 3, 'animacion_amp': 4, 'camara': 5, 'luz': 6}[x['tipo']]
                if (h['tipo'], h['ranura'], h['sub'], h['clips'][x['clip']]) != (ty, slot, x.get('banco', 0), x['largo']):
                    errors.append(f'{i}: referencia a clip no coincide')
                if loaded.get((ty, slot, x.get('banco', 0))) != (x['amb'], x['hijo']):
                    errors.append(f'{i}: clip de banco no activo')
                if ty in (3, 4) and slot not in actors:
                    errors.append(f'{i}: actor de animación ausente')
        expected = [(x['t'], x['amb'], x['hijo'], x['clip'], x['canal'], x['largo']) for x in e if x['tipo'] == 'camara']
        actual = [tuple(x[k] for k in ('t', 'amb', 'hijo', 'clip', 'canal', 'frames')) for x in s['camaras']]
        if actual != expected or not expected:
            errors.append(f'{i}: cámaras ausentes/incoherentes')
        for shot in s['camaras']:
            for field, width in (('cam_ojo', 4), ('cam_obj', 4), ('cam_roll', 2), ('cam_fov', 2)):
                keys = shot[field]
                if any(len(k) != width or not all(map(math.isfinite, k)) for k in keys) or any(
                        a[0] > b[0] for a, b in zip(keys, keys[1:])):
                    errors.append(f'{i}: {field} de cámara inválida')
        voice_count = 0
        for k, x in enumerate(e):
            if x['tipo'] != 'voz':
                continue
            voice_count += 1
            entry, ch = x['adx_jp'], x['canal']
            hz, n = samples(entry)
            if (x['audio']['sample_rate'], x['audio']['samples']) != (hz, n) or not math.isclose(x['segundos'], n/hz, abs_tol=1e-12):
                errors.append(f'{i}: duración ADX {entry} no coincide')
            following = next(y for y in e[k+1:] if y['tipo'] == 'fin_escena' or
                             y['tipo'] in ('voz', 'preparar_stream', 'parar_stream') and y.get('canal') in (ch, -1))
            end = Fraction(str(x['t'])) + Fraction(60*n, hz)
            delta = end - Fraction(str(following['t']))
            if delta > 1:
                cuts.append((i, entry, ch, x['t'], float(delta), following['tipo'], following.get('fundido')))
        s['voces_verificadas'] = voice_count
        if i == 1910 and ([x['adx_jp'] for x in e if x['tipo'] == 'voz'] != list(range(699, 706)) or
                          [x['clip'] for x in s['camaras']] != list(range(14)) or len(actors) != 25):
            errors.append('1910: no reproduce el censo de referencia')
    if used != {int(a) for a in amb}:
        errors.append(f'No se enlazan todos los AMB: {sorted({int(a) for a in amb} - used)}')
    return rows, totals, cuts, errors


def report(path, rows, totals, cuts, errors):
    evidence = {}
    per_scene = Counter(x[0] for x in cuts)
    with path.open('x', encoding='utf-8', newline='\n') as f:
        def line(*args):
            print(*args, file=f)

        line('# B1C2 — Cobertura de escenas PAL EN\n')
        line(f'ISO SHA-256: `{rows[0]["iso_sha256"]}`. 38 SPX y 50 AMB de `DATA_EN.AFS:1909–1996`.')
        line(f'Cobertura total: {totals["estaticas_traducidas"]}/{totals["estaticas"]} estáticas; '
             f'{totals["ejecutadas_traducidas"]}/{totals["ejecutadas"]} ejecutadas.\n')
        line('Estática = sitios hallados por `censo_estatico`; ejecutada = invocaciones en el recorrido EN sin mando de la VM Opus. '
             'Las traducciones incluyen control y eventos, no garantizan paridad audiovisual. '
             'Las ranuras son de B1; las cámaras conservan sus claves en el sistema original.\n')
        line('| SPX | AMB cargados | Estática | Ejecutada | Ranuras actor | Tomas | Voces ADX | Excesos | IDs pendientes |')
        line('|---:|---|---:|---:|---|---:|---:|---:|---|')
        for s in rows:
            c = s['cobertura']
            i = s['spx']
            actors = ','.join(str(x['ranura']) for x in s['actores'])
            slots = ','.join(map(str, s['paquetes']))
            unknowns = s['desconocidos']
            evidence.update(unknowns)
            line(f'| {i} | {slots} | {ratio(c["estaticas_traducidas"], c["estaticas"])} '
                 f'| {ratio(c["ejecutadas_traducidas"], c["ejecutadas"])} | {len(s["actores"])}: {actors} '
                 f'| {len(s["camaras"])} | {s["voces_verificadas"]} | {per_scene[i]} | {", ".join(unknowns)} |')

        line('\n## Builtins pendientes\n')
        line('El handler se lee de la tabla `0x335E00` del ELF de la ISO (`b1.c:95193–95197`). '
             'Cada JSON registra los recuentos por escena y el primer PC, cuadro y argumentos de los pendientes ejecutados. '
             '«No separado» indica ausencia de bloque propio en `ghidra/salida/b1.c`; no se inventa una línea.\n')
        line('| ID | Handler | Línea de `b1.c` | Sitios | Invocaciones |')
        line('|---|---|---:|---:|---:|')
        for k, item in sorted(evidence.items()):
            counts = [sum(s['desconocidos'].get(k, {}).get(field, 0) for s in rows) for field in ('estatica', 'ejecutada')]
            line(f'| {k} | {item["handler"] or "indeterminado"} | {item["b1_c_line"] or "no separado"} | {counts[0]} | {counts[1]} |')

        line('\n## Voces y reloj\n')
        line('Duración de cada ADX cotejada con muestras/frecuencia de la ISO. '
             'La prueba mide si la voz completa cabe hasta el siguiente uso/parada de su canal o la orden de fin. '
             'Permite pausas y canales paralelos, con una tolerancia de 1 unidad (1/60 s). '
             'La orden de fin inicia un fundido; no está demostrado que el audio se corte ahí. '
             'Se preservan tiempos y duraciones originales, sin recortar ni estirar para forzar el resultado.\n')
        line('| SPX | ADX | Canal | Inicio (u) | Exceso (u) | Límite medido |')
        line('|---:|---:|---:|---:|---:|---|')
        for i, entry, ch, start, excess, boundary, fade in cuts:
            line(f'| {i} | {entry} | {ch} | {start:g} | {excess:.3f} | {boundary}' +
                 (f' (parámetro de fundido {fade})' if fade is not None else '') + ' |')
        if errors:
            line('\nIncidencias adicionales:', '; '.join(errors))
        line(f'\n**Verificador: código {int(bool(cuts or errors))}.**')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--informe', type=Path)
    args = parser.parse_args()
    try:
        rows, totals, cuts, errors = check(args.cache, args.iso)
        if args.informe:
            path = args.informe.resolve()
            if path.is_relative_to(ROOT):
                raise ValueError('El informe con datos de la ISO debe quedar fuera del proyecto')
            report(path, rows, totals, cuts, errors)
    except (ValueError, OSError, TypeError, KeyError, IndexError, struct.error) as exc:
        print(f'FAIL: {exc}', file=sys.stderr)
        return 1
    print(f'{len(rows)}/38, voces {sum(s["voces_verificadas"] for s in rows)}; cobertura '
          f'{ratio(totals["estaticas_traducidas"], totals["estaticas"])} estática; '
          f'{ratio(totals["ejecutadas_traducidas"], totals["ejecutadas"])} ejecutada')
    print(f'{len(cuts)} voces exceden el siguiente límite del mismo canal o fin de escena:')
    for i, entry, ch, start, excess, boundary, fade in cuts:
        print(f'{i} ADX={entry} canal={ch} t={start:g} +{excess:.3f}u {boundary} fundido={fade}')
    for error in errors:
        print(f'FAIL: {error}')
    return int(bool(cuts or errors))


if __name__ == '__main__':
    sys.exit(main())
