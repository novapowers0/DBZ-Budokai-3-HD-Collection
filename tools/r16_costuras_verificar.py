"""Prueba Blender autocontenida: 4 parejas originales, dos movimientos, 24 poses."""
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_costuras import VERTICES, ajustar

out = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
if out.exists():
    raise FileExistsError(out)
out.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
data = bpy.data.armatures.new('rig_prueba')
rig = bpy.data.objects.new('rig_prueba', data)
bpy.context.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
rig.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
b = data.edit_bones.new('TEST_NECK')
b.head, b.tail = (0, 0, 0), (0, 0, 1)
bpy.ops.object.mode_set(mode='OBJECT')


def part(name, head, variant):
    # Cada pieza es geometria original de prueba, abierta solo en el cuello.
    verts, faces = [], []
    rings = [(0., .16), ((.30 if head else -.35), (.24 if variant else .18))]
    for z, radius in rings:
        for i in range(VERTICES):
            a = 2 * math.pi * i / VERTICES
            verts.append((radius * math.cos(a), radius * math.sin(a), z))
    for i in range(VERTICES):
        j = (i + 1) % VERTICES
        faces.append((i, j, VERTICES + j, VERTICES + i))
    faces.append(tuple(range(VERTICES, 2 * VERTICES)))
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    group = obj.vertex_groups.new(name='TEST_NECK')
    group.add(list(range(VERTICES, VERTICES * 2)), 1., 'REPLACE')
    order = ajustar(obj, rig, 'cuello', range(VERTICES))
    mod = obj.modifiers.new('deformacion', 'ARMATURE')
    mod.object = rig
    return obj, order


heads = [part('cabeza_%d' % i, True, i) for i in range(2)]
bodies = [part('cuerpo_%d' % i, False, i) for i in range(2)]
originals = heads + bodies
scene = bpy.context.scene
mat = bpy.data.materials.new('Original')
mat.diffuse_color = (.45, .75, .65, 1)
for obj, _ in originals:
    obj.data.materials.append(mat)

maxgap = 0.
for row, (head, body) in enumerate(( (heads[h], bodies[b]) for h in range(2) for b in range(2))):
    for col, angle in enumerate((0., .24, .48, -.1, -.3, -.5)):
        pb = rig.pose.bones['TEST_NECK']
        pb.rotation_mode = 'XYZ'
        pb.rotation_euler = (angle, 0, .12 * math.sin(col))
        bpy.context.view_layer.update()
        evaluated = []
        for obj, order in (head, body):
            ev = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
            points = [ev.matrix_world @ ev.data.vertices[i].co for i in order]
            evaluated.append(points)
            # Copia visual evaluada, sin modificar las piezas de prueba.
            mesh = bpy.data.meshes.new_from_object(ev)
            snap = bpy.data.objects.new('muestra_%d_%d' % (row, col), mesh)
            bpy.context.collection.objects.link(snap)
            snap.matrix_world = ev.matrix_world.copy()
            snap.location += Vector((col * .75, 0, -row * .9))
        maxgap = max(maxgap, *( (a-b).length for a, b in zip(*evaluated)))
if maxgap > 1e-6:
    raise AssertionError('Separacion de costura: %g' % maxgap)
for obj, _ in originals:
    obj.hide_render = True
camdata = bpy.data.cameras.new('camara')
cam = bpy.data.objects.new('camara', camdata)
bpy.context.collection.objects.link(cam)
cam.location = (1.875, -12, -1.35)
cam.rotation_euler = (Vector((1.875, 0, -1.35)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
camdata.type = 'ORTHO'
camdata.ortho_scale = 5.0
scene.camera = cam
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'MATERIAL'
scene.render.resolution_x = 1200
scene.render.resolution_y = 1000
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = str(out)
bpy.ops.render.render(write_still=True)
print('PASS costuras: 2 cabezas x 2 cuerpos x 6 poses, separacion maxima %.9g' % maxgap)
