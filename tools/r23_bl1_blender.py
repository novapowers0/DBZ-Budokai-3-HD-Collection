"""Trabajo Blender BL1: normales partidas; salida con el exportador B3 del Kit."""
import json
import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_partes_b3 import exportar_blend


def cv(p):
    return (p[0], -p[2], p[1])


def run(scene_path, output, save=None):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    source = json.loads(scene_path.read_text(encoding='utf-8'))
    scene = bpy.context.scene
    scene['b3_escena_local'] = str(scene_path)
    for row in source['meshes']:
        mesh = bpy.data.meshes.new('AWG_%02d' % row['awg'])
        mesh.from_pydata([cv(p) for p in row['pos']], [], row['tris'])
        mesh.update()
        obj = bpy.data.objects.new(mesh.name, mesh)
        obj['awg'] = row['awg']
        scene.collection.objects.link(obj)
        obj.hide_viewport = obj.hide_render = row['awg'] != 0
        uv = mesh.uv_layers.new(name='B3_UV')
        for loop in mesh.loops:
            uv.data[loop.index].uv = row['uv'][loop.vertex_index]
        mesh.polygons.foreach_set('use_smooth', [True] * len(mesh.polygons))
        normals = [Vector(cv(p)).normalized() for p in row['nrm']]
        for ids in row['groups']:
            avg = sum((normals[i] for i in ids), Vector()).normalized()
            for i in ids:
                normals[i] = avg
        mesh.normals_split_custom_set_from_vertices(normals)
        mesh.update()
    if save:
        bpy.ops.wm.save_as_mainfile(filepath=str(save))
    exportar_blend(output)


if __name__ == '__main__':
    mode, plan, dest = sys.argv[sys.argv.index('--') + 1:]
    for job in json.loads(Path(plan).read_text(encoding='utf-8')):
        p = Path(job['scene'])
        out = Path(dest) / (job['key'] + '.json')
        if mode == 'crear':
            run(p, out, p.with_suffix('.blend'))
        elif mode == 'reexportar':
            bpy.ops.wm.open_mainfile(filepath=str(p.with_suffix('.blend')), load_ui=False)
            exportar_blend(out)
        else:
            raise ValueError(mode)
