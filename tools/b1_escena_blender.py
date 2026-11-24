"""Backend del render TR3; se ejecuta en el Python de Blender."""
import json
import math
from pathlib import Path
import struct
import sys

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Matrix, Vector
import numpy as np


TRI = np.dtype([('tex', '<i4'), ('v', '<f4', (3, 10))])


def texture(path):
    raw = path.read_bytes()
    w, h = struct.unpack_from('<II', raw)
    if not (1 <= w <= 4096 and 1 <= h <= 4096 and len(raw) == 8 + 4*w*h):
        raise ValueError(f'Textura truncada: {path}')
    img = bpy.data.images.new(path.stem, w, h, alpha=True)
    img.colorspace_settings.name = 'Non-Color'
    img.alpha_mode = 'STRAIGHT'
    px = np.frombuffer(raw, 'u1', offset=8).reshape(h, w, 4)[::-1]
    img.pixels.foreach_set(np.ascontiguousarray(px, dtype='f4').ravel()/np.float32(255))
    img.update()
    return img


def material(work, slot, tex, fog, images):
    name = f'{slot}_{tex}'
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    new, link = nodes.new, links.new
    rgba = new('ShaderNodeVertexColor')
    rgba.layer_name = 'tinte'
    amount = new('ShaderNodeAttribute')
    amount.attribute_name = 'niebla'
    color, alpha = rgba.outputs['Color'], rgba.outputs['Alpha']
    if tex >= 0:
        img = new('ShaderNodeTexImage')
        if name not in images:
            images[name] = texture(work / f'tex_{name}.rgba')
        img.image = images[name]
        mult = new('ShaderNodeMixRGB')
        mult.blend_type = 'MULTIPLY'
        mult.inputs[0].default_value = 1
        link(color, mult.inputs[1]); link(img.outputs['Color'], mult.inputs[2])
        color = mult.outputs[0]
        mul_alpha = new('ShaderNodeMath')
        mul_alpha.operation = 'MULTIPLY'
        link(alpha, mul_alpha.inputs[0]); link(img.outputs['Alpha'], mul_alpha.inputs[1])
        alpha = mul_alpha.outputs[0]
    mix = new('ShaderNodeMixRGB')
    mix.inputs[2].default_value = (*fog, 1)
    link(amount.outputs['Fac'], mix.inputs[0]); link(color, mix.inputs[1])
    emission = new('ShaderNodeEmission')
    link(mix.outputs[0], emission.inputs['Color'])
    blend = new('ShaderNodeMixShader')
    link(alpha, blend.inputs[0])
    link(new('ShaderNodeBsdfTransparent').outputs[0], blend.inputs[1])
    link(emission.outputs[0], blend.inputs[2])
    link(blend.outputs[0], new('ShaderNodeOutputMaterial').inputs['Surface'])
    return mat


def geometry(scene, work, slot, triangles, eye, fog, images):
    v = triangles['v'].reshape(-1, 10)
    if not np.isfinite(v).all():
        raise ValueError(f'Geometría no finita en actor {slot}')
    xyz = v[:, :3].copy()
    # SKY00 se dibuja como fondo; desplazar en los rayos conserva su proyección.
    if slot == 4:
        at = np.asarray(eye, dtype='f4')
        xyz = at + (xyz - at) * 16
    mesh = bpy.data.meshes.new(f'actor_{slot:02}')
    mesh.from_pydata(xyz.tolist(), [], np.arange(len(v)).reshape(-1, 3).tolist())
    obj = bpy.data.objects.new(mesh.name, mesh)
    scene.collection.objects.link(obj)
    texs, ids = np.unique(triangles['tex'], return_inverse=True)
    for tex in texs:
        mesh.materials.append(material(work, slot, int(tex), fog, images))
    mesh.polygons.foreach_set('material_index', ids.astype('i4'))
    uv = mesh.uv_layers.new(name='UVMap')
    uv.data.foreach_set('uv', np.ascontiguousarray(v[:, 3:5]).ravel())
    col = mesh.color_attributes.new(name='tinte', type='FLOAT_COLOR', domain='CORNER')
    col.data.foreach_set('color', np.ascontiguousarray(v[:, 5:9]).ravel())
    fog_attr = mesh.attributes.new(name='niebla', type='FLOAT', domain='CORNER')
    fog_attr.data.foreach_set('value', v[:, 9].copy())
    mesh.update()
    return dict(slot=slot, triangulos=len(triangles), texturas=[int(t) for t in texs if t >= 0])


def main(out, samples):
    spec = json.loads((out / 'cortes.json').read_bytes())
    work = out / 'intermedios'
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.world.use_nodes = True
    scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value = 0
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = samples
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = False
    scene.cycles.transparent_max_bounces = 32
    scene.cycles.max_bounces = 0
    scene.render.resolution_x, scene.render.resolution_y = spec['resolucion']
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.view_settings.view_transform = 'Raw'
    scene.render.dither_intensity = 0
    camera = bpy.data.cameras.new('AMC')
    obj = bpy.data.objects.new('AMC', camera)
    scene.collection.objects.link(obj)
    scene.camera = obj
    camera.sensor_fit = 'VERTICAL'
    camera.clip_start, camera.clip_end = .1, 65535
    images, inventory = {}, []
    for cut in spec['cortes']:
        i = cut['corte']
        raw = (work / f'corte_{i}.tri').read_bytes()
        if raw[:4] != b'TR31':
            raise ValueError('Intermedio TR3 inválido')
        data = struct.unpack_from('<22fi2f', raw, 4)
        view = Matrix(tuple(data[j:16:4] for j in range(4)))
        obj.matrix_world = view.inverted()
        fov, fx, fy = data[16:19]
        camera.lens = camera.sensor_height / (2 * math.tan(fov/2))
        scene.render.pixel_aspect_x = 1
        scene.render.pixel_aspect_y = fx/fy
        bpy.context.view_layer.update()
        # Contrasta con la fórmula de proyección B1, incluidos roll y focal X/Y.
        point = Vector((.25, .5, -10))
        ndc = world_to_camera_view(scene, obj, obj.matrix_world @ point)
        focus = 1 / (10 * math.tan(fov/2))
        x = (ndc.x-.5)*scene.render.resolution_x
        y = (ndc.y-.5)*scene.render.resolution_y
        if max(abs(x-point.x*fx*focus), abs(y-point.y*fy*focus)) > .025:
            raise ValueError(f'Proyección Blender distinta de AMC, corte {i}: {x}, {y}')
        at, mesh_info = 104, []
        while True:
            slot, = struct.unpack_from('<i', raw, at)
            at += 4
            if slot == -1:
                break
            n, = struct.unpack_from('<I', raw, at)
            at += 4
            tris = np.frombuffer(raw, TRI, count=n, offset=at)
            mesh_info.append(geometry(scene, work, slot, tris, obj.location, data[19:22], images))
            at += n * TRI.itemsize
        if at != len(raw) or not mesh_info or not any(a['texturas'] for a in mesh_info):
            raise ValueError(f'Corte {i}: geometría o texturas incompletas')
        scene.render.filepath = str(out / cut['png'])
        bpy.ops.render.render(write_still=True)
        inventory.append(dict(corte=i, t_u=cut['t_u'], actores=mesh_info, fov=fov,
                              subtitulo=data[22], sub_xy=data[23:25]))
        for mesh_obj in tuple(scene.objects):
            if mesh_obj.type == 'MESH':
                mesh = mesh_obj.data
                bpy.data.objects.remove(mesh_obj, do_unlink=True)
                bpy.data.meshes.remove(mesh)
        for mat in tuple(bpy.data.materials):
            if mat.users == 0:
                bpy.data.materials.remove(mat)
    (out / 'geometria.json').write_text(json.dumps(inventory, indent=2), encoding='utf-8')


if __name__ == '__main__':
    directory, n = sys.argv[sys.argv.index('--') + 1:]
    main(Path(directory), int(n))
