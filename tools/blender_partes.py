#!/usr/bin/env python3
"""Piezas originales paramétricas para Blender; HMV1 es solo vista estática."""
import argparse
import math
import struct
import sys
import tomllib
from pathlib import Path

import bpy
from mathutils import Vector


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('receta', type=Path)
    parser.add_argument('--salida', required=True, type=Path)
    parser.add_argument('--sin-render', action='store_true', help='solo B3: guardar rig/exportacion sin PNG')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    with args.receta.open('rb') as f:
        r = tomllib.load(f)
    kind = r['kind']
    if kind == 'original_b3':
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from blender_partes_b3 import blender_main
        return blender_main(r, args)
    if kind not in ('horn', 'crest', 'antenna', 'ear', 'tail', 'plate'):
        raise ValueError('kind desconocido')
    size = float(r.get('size', 1.0))
    curve = float(r.get('curve', 0.2))
    seg = int(r.get('segments', 8))
    sides = int(r.get('sides', 8))
    color = r.get('colour', [0.7, 0.7, 0.7])
    region = r.get('colour_region', 'principal')
    bone = r.get('bone', 'HEAD' if kind not in ('tail',) else 'TAIL')
    if not (0 < size <= 10 and -3 <= curve <= 3 and 3 <= seg <= 64 and 3 <= sides <= 32):
        raise ValueError('size, curve o segmentos fuera de rango')
    if len(color) != 3 or any(not 0 <= float(c) <= 1 for c in color):
        raise ValueError('colour debe contener tres valores entre 0 y 1')
    args.salida.mkdir(parents=True, exist_ok=True)
    outputs = [args.salida / (args.receta.stem + ext) for ext in ('.malla', '.blend', '.png')]
    if any(p.exists() for p in outputs):
        raise FileExistsError('La salida ya existe; elija otra carpeta')
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    verts, faces = [], []
    for i in range(seg + 1):
        t = i / seg
        radius = size * (0.14 * (1 - t) + 0.008)
        if kind in ('ear', 'crest', 'plate'):
            radius = size * (0.06 + 0.16 * math.sin(math.pi * t)) * (1 - 0.7 * t)
        for j in range(sides):
            a = 2 * math.pi * j / sides
            x = size * curve * t * t + radius * math.cos(a)
            y = radius * math.sin(a) * (0.32 if kind in ('ear', 'crest', 'plate') else 1)
            z = size * t
            verts.append((x, y, z))
        if i:
            for j in range(sides):
                a = (i - 1) * sides + j
                b = (i - 1) * sides + (j + 1) % sides
                c = i * sides + (j + 1) % sides
                d = i * sides + j
                faces.extend(((a, b, c), (a, c, d)))
    faces.append(tuple(reversed(range(sides))))
    faces.append(tuple(seg * sides + j for j in range(sides)))
    mesh = bpy.data.meshes.new('Pieza_original')
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new('Pieza_original', mesh)
    bpy.context.collection.objects.link(obj)
    obj['colour_region'] = region
    obj['bone_sugerido'] = bone
    group = obj.vertex_groups.new(name=bone)
    group.add(list(range(len(verts))), 1.0, 'REPLACE')
    uv = mesh.uv_layers.new(name='Color_plano')
    for loop in uv.data:
        loop.uv = (0.5, 0.5)
    mat = bpy.data.materials.new('Color_' + str(region))
    mat.diffuse_color = (*map(float, color), 1.0)
    obj.data.materials.append(mat)
    mesh.calc_loop_triangles()
    # HMV1: geometría estática; no contiene esqueleto ni permite injerto de combate.
    triangles = list(mesh.loop_triangles)
    payload = bytearray(b'HMV1' + struct.pack('<IIfII', 1, len(triangles), size, 1, 1))
    payload.extend(bytes([round(float(c) * 255) for c in color] + [255]))
    for tri in triangles:
        for vi in tri.vertices:
            v = mesh.vertices[vi]
            # Blender Z vertical -> visor B3 Y vertical.
            payload.extend(struct.pack('<8f', v.co.x, v.co.z, v.co.y,
                                       v.normal.x, v.normal.z, v.normal.y, .5, .5))
        payload.extend(struct.pack('<iI', 0, 0))
    outputs[0].write_bytes(payload)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.wm.save_as_mainfile(filepath=str(outputs[1].resolve()))
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'MATERIAL'
    scene.display.shading.show_shadows = True
    scene.render.resolution_x = 512
    scene.render.resolution_y = 512
    scene.render.resolution_percentage = 50
    camera = bpy.data.cameras.new('Camera')
    cam = bpy.data.objects.new('Camera', camera)
    bpy.context.collection.objects.link(cam)
    cam.location = (size * 2.8, -size * 3.2, size * 2.2)
    cam.rotation_euler = (Vector((size * curve * .4, 0, size * .5)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    camera.type = 'ORTHO'
    camera.ortho_scale = size * 1.5
    scene.camera = cam
    scene.render.image_settings.file_format = 'PNG'
    scene.render.filepath = str(outputs[2].resolve())
    bpy.ops.render.render(write_still=True)
    print('PASS pieza %s: %d vertices, %d triangulos, HMV1 estatico, %s' % (kind, len(verts), len(triangles), outputs[0]))


if __name__ == '__main__':
    main()
