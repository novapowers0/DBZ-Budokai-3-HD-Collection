"""Resume la sonda de intro por paso del guest; una pose fija no es por si sola un fallo.

Uso: python -B tools/iwx_intro_analizar.py CARPETA_PRUEBA
Con --censo CENSO_JSON resume bancos nativos y repeticiones de claves offline.
No escribe los originales. --salida guarda metadatos en una ruta nueva.
"""
import argparse
from bisect import bisect_right
import csv
import json
from itertools import groupby
from pathlib import Path

POSE = ('cam_ojo_x', 'cam_ojo_y', 'cam_ojo_z', 'cam_obj_x', 'cam_obj_y',
        'cam_obj_z', 'cam_roll', 'cam_fov')


def analizar(path):
    raw = list(csv.DictReader((path/'combate_sonda.csv').open(encoding='utf-8')))
    # El arnes copia el CSV con el proceso vivo: puede quedar la ultima fila
    # parcialmente escrita. Se declara la omision; nunca se repara el original.
    incomplete = [i+2 for i, r in enumerate(raw) if None in r or any(v is None for v in r.values())]
    if any(i != len(raw)+1 for i in incomplete):
        raise ValueError('fila incompleta interior; no se puede acreditar continuidad')
    rows = raw[:-1] if incomplete else raw
    if not rows or not set(POSE).issubset(rows[0]):
        raise ValueError('faltan filas/columnas DBZ3_TEST_INTRO_SONDA; medida no concluyente')
    groups = []
    key = lambda r: tuple(r[k] for k in ('combate', 'cam_modo', 'cam_clip', 'cam_acc', 'cam_limite'))
    # Un reinicio puede seleccionar otra pista con la misma duracion y ACC.
    segments = []
    segment = 0
    previous = None
    for r in rows:
        if previous is not None and (key(r) != key(previous)
                or int(r['cuadro']) != int(previous['cuadro'])+1
                or float(r['cam_reloj']) < float(previous['cam_reloj'])):
            segment += 1
        segments.append((segment, r))
        previous = r
    for _, it in groupby(segments, lambda sr: sr[0]):
        g = [r for _, r in it]
        signature = key(g[0])
        pauses = []
        for _, same in groupby(enumerate(g), lambda ir: tuple(ir[1][p] for p in POSE)):
            repeated = list(same)
            if len(repeated) < 3 or any(not repeated[0][1][p] for p in POSE):
                continue
            a, b = repeated[0][1], repeated[-1][1]
            pauses.append(dict(pasos=len(repeated), cuadro_inicio=int(a['cuadro']),
                               cuadro_fin=int(b['cuadro']), reloj_inicio=float(a['cam_reloj']),
                               reloj_fin=float(b['cam_reloj'])))
        steps = [float(b['cam_reloj'])-float(a['cam_reloj']) for a, b in zip(g, g[1:])]
        groups.append(dict(estado=int(signature[0]), modo=int(signature[1]),
                           campo_560=int(signature[2]),
                           acc=int(signature[3]), limite=float(signature[4]), pasos=len(g),
                           cuadro_inicio=int(g[0]['cuadro']), cuadro_fin=int(g[-1]['cuadro']),
                           reloj_inicio=float(g[0]['cam_reloj']), reloj_fin=float(g[-1]['cam_reloj']),
                            incrementos=sorted(set(steps)), poses_distintas=len({tuple(r[p] for p in POSE) for r in g}),
                            pasos_sin_pose=sum(any(not r[p] for p in POSE) for r in g),
                            pausas=pauses))
    return dict(prueba=str(path), filas=len(rows), filas_incompletas=incomplete, grupos=groups,
                cobertura='Pasos de sub_821100B0; pose seleccionada observada antes del paso. '
                          'La fila de READY reinicia cuadro. No acredita sonido ni todos los dibujos.')


def resumen_censo(path):
    data = json.loads(path.read_text(encoding='utf-8'))
    result = {}
    for name, detail in data['detalle'].items():
        banks = []
        for bank in detail['camaras']:
            clips = []
            for c in bank:
                tracks = []
                for t in c['pistas']:
                    tail = 0
                    for k in reversed(t):
                        if k[1:] != t[-1][1:]:
                            break
                        tail += 1
                    tracks.append(dict(claves=len(t), ultimo_cuadro=t[-1][0] if t else None,
                                       ultimas_claves_iguales=tail))
                clips.append(dict(clip=c['clip'], cuadros=c['cuadros'], pistas=tracks))
            banks.append(clips)
        result[name] = dict(donante=detail.get('spx_donante'), bancos=banks)
    return dict(mods=data['mods'], filas=data['filas'], detalle=result)


def resumen_vivo(path):
    data = json.loads(path.read_text(encoding='utf-8'))
    cases = []
    for c in data['casos']:
        intro = [g for g in c.get('medida', {}).get('grupos', []) if g['estado'] == 1 and g['modo'] == 7]
        cases.append(dict(entrada=c['entrada'], indice=c['indice'], prueba=c.get('prueba'),
                          exe=c.get('contexto', {}).get('exe_sha256'), error=c.get('error'),
                          intro=[{k: g[k] for k in ('cuadro_inicio', 'cuadro_fin', 'reloj_inicio',
                                 'reloj_fin', 'limite', 'pasos', 'poses_distintas', 'pasos_sin_pose', 'pausas')}
                                 for g in intro]))
    return dict(casos=cases)


def comparar_pose(path, reference):
    def intro(folder):
        with (folder/'combate_sonda.csv').open(encoding='utf-8') as fp:
            rows = list(csv.DictReader(fp))
        return {int(r['cuadro']): r for r in rows if r.get('combate') == '1'
                and r.get('cam_modo') == '7' and None not in r and all(v is not None for v in r.values())}
    left, right = intro(path), intro(reference)
    common = left.keys() & right.keys()
    fields = {}
    for field in ('cam_reloj', 'cam_paso', 'cam_limite', *POSE):
        pairs = [(left[k][field], right[k][field]) for k in common]
        valid = [(float(a), float(b)) for a, b in pairs if a != '' and b != '']
        fields[field] = dict(diferencias=sum(a != b for a, b in valid),
                             max_abs=max((abs(a-b) for a, b in valid), default=None),
                             sin_dato=len(pairs)-len(valid))
    return dict(prueba=str(path), referencia=str(reference), cuadros_comunes=len(common),
                solo_prueba=sorted(left.keys()-right.keys()), solo_referencia=sorted(right.keys()-left.keys()),
                 campos=fields, cobertura='Solo estado 1/modo 7; referencia es B3 nativo, no PS2.')


def contrastar_amc(path, source_path, field):
    """Evalua claves originales; no sustituye una captura del reproductor PS2."""
    source = json.loads(source_path.read_text(encoding='utf-8'))
    record, = [c for c in source['campos'] if c['campo'] == field]
    clips = record['original']
    with (path/'combate_sonda.csv').open(encoding='utf-8') as fp:
        rows = [r for r in csv.DictReader(fp) if r.get('combate') == '1'
                and r.get('cam_modo') == '7' and None not in r and all(v is not None for v in r.values())]
    groups = [g for g in analizar(path)['grupos'] if g['estado'] == 1 and g['modo'] == 7]

    def sample(keys, time):
        if not keys:
            return []
        i = max(0, bisect_right([k[0] for k in keys], time)-1)
        a, b = keys[i], keys[min(i+1, len(keys)-1)]
        blend = min(1, max(0, (time-a[0])/(b[0]-a[0]))) if b[0] != a[0] else 0
        return [left+(right-left)*blend for left, right in zip(a[1:], b[1:])]

    result = []
    for i, group in enumerate(groups):
        measured = [r for r in rows if group['cuadro_inicio'] <= int(r['cuadro']) <= group['cuadro_fin']]
        if i >= len(clips):
            result.append(dict(recorrido=i, estado='sin clip original propio; no se fuerza emparejamiento'))
            continue
        clip = clips[i]
        models = []
        # La sonda lee reloj y pose antes del paso; se conservan ambas hipotesis
        # de fase sin elegir retrospectivamente una como prueba del motor PS2.
        for delay in (0, 1):
            maxima = {p: 0 for p in POSE}
            missing, differing, pauses, error_frames = 0, 0, [], []
            previous, start, count = None, None, 0
            for r in measured:
                clock = max(0, min(clip['cuadros']-1, float(r['cam_reloj'])-delay))
                pose = [v for t in clip['pistas'] for v in sample(t, clock)]
                if len(pose) != len(POSE) or any(not r[p] for p in POSE):
                    missing += 1
                    continue
                errors = [abs(float(r[p])-v) for p, v in zip(POSE, pose)]
                for p, error in zip(POSE, errors):
                    maxima[p] = max(maxima[p], error)
                if any(e > 0.001 for e in errors):
                    differing += 1
                    if len(error_frames) < 12:
                        error_frames.append(int(r['cuadro']))
                signature = tuple(pose)
                if signature != previous:
                    if count >= 3:
                        pauses.append(dict(cuadro_inicio=start, pasos=count))
                    start, count = int(r['cuadro']), 0
                previous, count = signature, count+1
            if count >= 3:
                pauses.append(dict(cuadro_inicio=start, pasos=count))
            models.append(dict(retraso_reloj=delay, filas=len(measured), sin_dato=missing,
                               filas_error_mayor_0_001=differing, primeros_cuadros_error=error_frames,
                               max_abs=maxima, pausas_esperadas=pauses))
        result.append(dict(recorrido=i, modelos=models))
    return dict(prueba=str(path), fuente=str(source_path), campo=field, recorridos=result,
                cobertura='Claves AMC originales e interpolacion lineal calculada, tolerancia 0.001; '
                          'hipotesis de fase 0/1, no observacion del motor PS2 ni prueba de sonido/fondo.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('prueba', nargs='?', type=Path)
    p.add_argument('--censo', type=Path)
    p.add_argument('--censo-vivo', type=Path)
    p.add_argument('--referencia', type=Path)
    p.add_argument('--amc-original', type=Path, help='camaras_originales.json, comparacion derivada de claves')
    p.add_argument('--campo', type=int)
    p.add_argument('--salida', type=Path)
    a = p.parse_args()
    if sum(bool(v) for v in (a.prueba, a.censo, a.censo_vivo)) != 1:
        p.error('elegir prueba, --censo o --censo-vivo')
    if a.referencia and not a.prueba:
        p.error('--referencia requiere prueba')
    if (a.amc_original is None) != (a.campo is None) or (a.amc_original and not a.prueba):
        p.error('--amc-original y --campo requieren prueba y deben usarse juntos')
    result = (resumen_censo(a.censo) if a.censo else resumen_vivo(a.censo_vivo)
              if a.censo_vivo else analizar(a.prueba))
    if a.referencia:
        result['comparacion'] = comparar_pose(a.prueba, a.referencia)
    if a.amc_original:
        result['comparacion_amc'] = contrastar_amc(a.prueba, a.amc_original, a.campo)
    text = json.dumps(result, ensure_ascii=False, indent=2)
    if a.salida:
        with a.salida.open('x', encoding='utf-8') as f:
            f.write(text+'\n')
    print(text)


if __name__ == '__main__':
    main()
