"""Muestrea la cámara y las poses HD de 1910 a reloj fijo, sin juego."""
import argparse
from bisect import bisect_right
import json
import math
from pathlib import Path
import struct
import tomllib

import numpy as np


def sample(track, f):
    i = max(0, bisect_right(track, f, key=lambda k: k[0]) - 1)
    a, b = track[i], track[min(i + 1, len(track) - 1)]
    t = min(1., max(0., (f - a[0]) / (b[0] - a[0]))) if a[0] != b[0] else 0.
    return np.asarray(a[1:]) * (1. - t) + np.asarray(b[1:]) * t


def read_skin(path, mesh=False):
    data = path.read_bytes()
    magic, textures, tri, n, _ = struct.unpack_from('<4sIIIf', data)
    if magic != b'HMS3' or not 0 < n <= 256 or textures > 4096 or tri > 250000:
        raise ValueError(f'Cabecera HMS3 invalida: {path.name}')
    off = 20
    for _ in range(textures):
        w, h = struct.unpack_from('<II', data, off)
        off += 8 + 4 * w * h
    bones = []
    for i in range(n):
        parent, label, *bind = struct.unpack_from('<i24s12f', data, off + 116 * i)
        if not -1 <= parent <= i or not all(map(math.isfinite, bind)):
            raise ValueError(f'Hueso HMS3 invalido: {path.name}:{i}')
        bones.append((parent, label.split(b'\0', 1)[0], np.asarray(bind).reshape(3, 4)))
    if not mesh:
        return bones, None
    vertex = np.dtype([('p', '<f4', 3), ('other', '<f4', 5), ('bone', '<u4')])
    dtype = np.dtype([('v', vertex, 3), ('tex', '<i4'), ('flags', '<u4')])
    triangles = np.frombuffer(data, dtype, tri, off + 116 * n)['v']
    if (triangles['bone'] >= n).any() or not np.isfinite(triangles['p']).all():
        raise ValueError(f'Malla HMS3 inválida: {path.name}')
    return bones, triangles


def read_clip(path, frames, bones):
    data = path.read_bytes()
    if (struct.unpack_from('<4sII', data) != (b'B1M1', frames, bones)
            or not 0 < frames <= 4096 or len(data) != 12 + frames * bones * 48):
        raise ValueError(f'Dimensiones B1M1 invalidas: {path.name}')
    local = np.frombuffer(data, '<f4', offset=12).reshape(frames, bones, 3, 4)
    if not np.isfinite(local).all():
        raise ValueError(f'Matriz B1M1 no finita: {path.name}')
    return local


def compose(bones, local):
    world = []
    for i, (parent, _, _) in enumerate(bones):
        x = local[i].astype(float)
        if 0 <= parent < i:
            p = world[parent]
            x = p[:, :3] @ x
            x[:, 3] += p[:, 3]
        world.append(x)
    return world


def camera(shot, t):
    f = min(max(t - shot['t'], 0.), max(shot['frames'] - 1, 0))
    eye = sample(shot['cam_ojo'], f)
    target = sample(shot['cam_obj'], f)
    fov, = sample(shot['cam_fov'], f)
    if not 0. < fov < math.pi or np.linalg.norm(target - eye) < 1e-6:
        raise ValueError(f'Cámara degenerada: {shot["clip"]} a {t}')
    return dict(clip=shot['clip'], eye=eye.tolist(), target=target.tolist(), fov=float(fov))


def basis(view):
    eye = np.asarray(view['eye'])
    forward = np.asarray(view['target']) - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, [0, 1, 0])
    norm = np.linalg.norm(right)
    if norm < 1e-6:
        return None
    right /= norm
    return eye, right, np.cross(right, forward), forward


def project(pos, view, aspect=640 / 448):
    """Sonda aproximada en marco B1; eje arriba +Y, sin roll ni zoom de B3."""
    axes = basis(view)
    if axes is None:
        return None
    eye, right, up, forward = axes
    diff = pos - eye
    z = float(diff @ forward)
    if z <= 0.001:
        return dict(depth=z, ndc=None)
    h = z * math.tan(view['fov'] / 2)
    return dict(depth=z, ndc=[float(diff @ right / (h * aspect)), float(diff @ up / h)])


def preview(cells, output):
    from PIL import Image, ImageDraw
    size = (240, 168)
    sheet = Image.new('RGB', (4 * size[0], 4 * (size[1] + 18)), '#111820')
    label = ImageDraw.Draw(sheet)
    for number, (t, view, meshes) in enumerate(cells):
        pic = Image.new('RGB', size, '#293038')
        draw = ImageDraw.Draw(pic)
        axes = basis(view)
        faces = []
        if axes is not None:
            eye, right, up, forward = axes
            focus = size[1] / 2 / math.tan(view['fov'] / 2)
            for actor, triangles, posed in meshes:
                world = np.asarray(posed)[triangles['bone']]
                delta = (world[..., :3] @ triangles['p'][..., None])[..., 0] + world[..., 3] - eye
                z = delta @ forward
                front = (z > .05).all(axis=1)
                xyz, depths = delta[front], z[front]
                if not len(depths):
                    continue
                xy = np.stack([size[0] / 2 + focus * (xyz @ right) / depths,
                               size[1] / 2 - focus * (xyz @ up) / depths], axis=-1)
                visible = ((xy.max(axis=1) >= 0) & (xy.min(axis=1) < size)).all(axis=1)
                normal = np.cross(xyz[:, 1] - xyz[:, 0], xyz[:, 2] - xyz[:, 0])
                norm = np.maximum(np.linalg.norm(normal, axis=1), 1e-9)
                light = .4 + .6 * np.abs(normal @ forward) / norm
                color = ((231, 138, 57), (93, 210, 116), (166, 122, 220), (88, 193, 228))[actor % 4]
                faces.extend((float(depth), polygon, tuple(int(c * lit) for c in color))
                             for depth, polygon, lit in zip(depths.mean(axis=1)[visible], xy[visible], light[visible]))
        for _, polygon, color in sorted(faces, key=lambda face: -face[0]):
            draw.polygon([tuple(xy) for xy in polygon], fill=color)
        x, y = (number % 4) * size[0], (number // 4) * (size[1] + 18)
        sheet.paste(pic, (x, y))
        label.text((x + 4, y + size[1] + 3), f'{view["clip"]:02}: {t / 60:5.2f}s | {len(faces)} tris HD', fill='white')
    with output.open('xb') as out:
        sheet.save(out, format='PNG')


def inspect(base, sheet=None):
    doc = tomllib.loads((base / 'escena_1910.toml').read_text(encoding='utf-8'))
    if doc['schema'] != 1 or doc['spx'] != 1910:
        raise ValueError('Se necesita la cache B1 de la escena 1910')

    def asset(name):
        path = (base / name).resolve()
        if path.parent != base:
            raise ValueError(f'Ruta de recurso fuera de la cache: {name}')
        return path

    actors = {a['slot']: read_skin(asset(a['hd_skin']), mesh=sheet is not None)
              for a in doc['actores'] if 'hd_skin' in a}
    clips = {}
    for r in doc['retarget']:
        actor = r['actor']
        key = actor, r['bank'], r['source_clip']
        if key in clips or r['bones'] != len(actors[actor][0]):
            raise ValueError(f'Clip duplicado o incompatible: {key}')
        clips[key] = read_clip(asset(r['matrices']), r['frames'], r['bones'])
    events = sorted(doc['eventos'], key=lambda e: e['t'])  # estable en empates
    shots = doc['camaras']
    if any(a['t'] >= b['t'] for a, b in zip(shots, shots[1:])):
        raise ValueError('Cortes de cámara fuera de orden')
    shown, active, used = set(), {}, set()
    rows, cells = [], []
    event_index = 0
    # Probar inicio/mitad/final de cada toma y cambios de visibilidad o animación.
    times, middle = {doc['duration_units']}, set()
    for a, b in zip(shots, shots[1:] + [dict(t=doc['duration_units'])]):
        mid = (a['t'] + b['t']) / 2
        middle.add(mid)
        times.update((a['t'], mid, max(a['t'], b['t'] - .1)))
    times.update(e['t'] for e in events if e['tipo'] in ('animacion', 'actor_dibujo') and e['t'] >= shots[0]['t'])
    for t in sorted(times):
        while event_index < len(events) and events[event_index]['t'] <= t:
            e = events[event_index]
            kind = e['tipo']
            if kind == 'actor_dibujo':
                (shown.add if e['activo'] else shown.discard)(e['actor'])
            elif kind == 'animacion' and e['actor'] in actors:
                key = e['actor'], e['banco'], e['clip']
                if e['vueltas'] != 1 or len(clips[key]) != e['largo']:
                    raise ValueError(f'No se confirmó la duración de {key}')
                active[e['actor']] = e['t'], key
                used.add(key)
            elif kind == 'liberar_actor':
                if e['actor'] == -1:
                    shown.clear()
                    active.clear()
                else:
                    shown.discard(e['actor'])
                    active.pop(e['actor'], None)
            event_index += 1
        shot = shots[bisect_right(shots, t, key=lambda s: s['t']) - 1]
        view = camera(shot, t)
        pose, meshes = [], []
        for actor in sorted(shown & actors.keys()):
            bones, triangles = actors[actor]
            start, key = active.get(actor, (None, None))
            f = min(int(t - start), len(clips[key]) - 1) if key else None
            local = clips[key][f] if key else [b for _, _, b in bones]
            world = compose(bones, local)
            waist, = [i for i, (_, name, _) in enumerate(bones) if name == b'WAIST']
            p = world[waist][:, 3]
            pose.append(dict(actor=actor, clip=key, frame=f, waist=p.tolist(), waist_projection=project(p, view)))
            if sheet and t in middle:
                meshes.append((actor, triangles, world))
        if sheet and t in middle:
            cells.append((t, view, meshes))
        rows.append(dict(time_units=t, camera=view, actors_hd=pose,
                         actors_other=sorted(shown - actors.keys())))

    if used != clips.keys() or len({row['camera']['clip'] for row in rows}) != len(shots):
        raise ValueError('Hay clips sin resolver en la línea de tiempo')
    if sheet:
        preview(cells, sheet)
    print(f'{len(rows)} muestras, {len(shots)} tomas, {len(used)} clips HD y {len(actors)} ranuras HD')
    return dict(iso_sha256=doc['iso_sha256'], runtime_verified=False, samples=rows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cache', type=Path)
    parser.add_argument('--salida', type=Path)
    parser.add_argument('--hoja', type=Path, help='malla HD sin texturas: una muestra por corte')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    for output in (args.salida, args.hoja):
        if output and output.resolve().is_relative_to(root):
            parser.error('La salida debe estar fuera del proyecto')
    result = inspect(args.cache.resolve(), args.hoja)
    if args.salida:
        with args.salida.open('x', encoding='utf-8') as stream:
            json.dump(result, stream, indent=2, allow_nan=False)
