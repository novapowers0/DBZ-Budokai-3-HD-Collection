"""Exportador Blender 5.2 de los diseños originales de cac_nativas_diseno.

blender --background --factory-startup --python-exit-code 1 --python
tools/cac_nativas_blender.py -- --salida res/heroe/partes_nativas
"""
import argparse
import hashlib
import json
import math
import sys
from pathlib import Path


def main():
    import bpy
    from mathutils import Vector
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import cac_nativas_diseno as d
    parser = argparse.ArgumentParser()
    parser.add_argument('--salida', required=True, type=Path)
    parser.add_argument('--blend', required=True, type=Path)
    parser.add_argument('--render', action='store_true')
    parser.add_argument('--raza', choices=('freezer', 'namek', 'majin'), action='append')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if args.salida.exists() or args.blend.exists():
        raise FileExistsError('Las carpetas de salida deben ser nuevas')
    if not args.salida.parent.is_dir() or not args.blend.parent.is_dir():
        raise FileNotFoundError('Los directorios padre deben existir')
    args.salida.mkdir()
    args.blend.mkdir()
    records, rigchecks = [], {}
    cv = lambda p: (p[0], -p[2], p[1])
    parents = {'HEAD': 'NECK', 'NECK': 'CHEST', 'CHEST': 'STMC', 'STMC': 'WAIST'}
    for side in ('L', 'R'):
        parents.update({side+'CHN': 'CHEST', side+'ARM1': side+'CHN', side+'ARM2': side+'ARM1',
                        side+'HAND': side+'ARM2', side+'LEG1': 'WAIST', side+'LEG2': side+'LEG1',
                        side+'FOOT1': side+'LEG2', side+'FOOT2': side+'FOOT1'})
    for i in range(1, 6):
        parents['T_TAIL%d'%i] = 'WAIST' if i == 1 else 'T_TAIL%d'%(i-1)
    def write(path, mesh):
        payload = json.dumps(mesh, ensure_ascii=False, separators=(',', ':'), allow_nan=False).encode('utf-8')
        (args.salida/path).write_bytes(payload)
        records.append(dict(file=path, sha256=hashlib.sha256(payload).hexdigest(),
                            vertices=len(mesh['P']), triangles=len(mesh['tris'])))
    races = args.raza or ('freezer', 'namek', 'majin')
    for race in races:
        (args.salida/race).mkdir()
        bpy.ops.object.select_all(action='SELECT')
        bpy.ops.object.delete(use_global=False)
        arm = bpy.data.armatures.new('Maniqui_original_R16')
        rig = bpy.data.objects.new('Rig_original_'+race, arm)
        bpy.context.collection.objects.link(rig)
        bpy.context.view_layer.objects.active = rig
        rig.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT')
        for name, pos in d.BIND.items():
            bone = arm.edit_bones.new(name)
            bone.head = cv(pos)
            bone.tail = Vector(bone.head)+Vector((0, 0, .24))
        for name, parent in parents.items():
            arm.edit_bones[name].parent = arm.edit_bones[parent]
        bpy.ops.object.mode_set(mode='OBJECT')
        mats = []
        for k, rgb in enumerate(d.PALETAS[race]):
            mat = bpy.data.materials.new('Region_original_%d'%k)
            mat.diffuse_color = (*[int(rgb[i:i+2], 16)/255 for i in (0, 2, 4)], 1.)
            mats.append(mat)
        def obj(name, mesh, visible=False):
            data = bpy.data.meshes.new(name)
            data.from_pydata([cv(p) for p in mesh['P']], [], mesh['tris'])
            data.update()
            ob = bpy.data.objects.new(name, data)
            bpy.context.collection.objects.link(ob)
            for mat in mats:
                data.materials.append(mat)
            uv = data.uv_layers.new(name='Regiones_originales')
            for p, region in zip(data.polygons, mesh['regions']):
                p.material_index = region
                p.use_smooth = True
                for li in p.loop_indices:
                    uv.data[li].uv = ((region%4+.5)/4, (region//4+.5)/4)
            for name in set(mesh['bones']):
                ids = [i for i, b in enumerate(mesh['bones']) if b == name]
                ob.vertex_groups.new(name=name).add(ids, 1., 'REPLACE')
            mod = ob.modifiers.new('Skin_original', 'ARMATURE')
            mod.object = rig
            ob.parent = rig
            ob['procedencia'] = 'original, made for this project'
            ob.hide_render = not visible
            ob.hide_set(not visible)
            return ob
        objects = []
        for k in range(8):
            h = d.head(race, k)
            write(f'{race}/cabeza_{k:02}.json', h)
            for expression in (1, 2):
                write(f'{race}/cabeza_{k:02}_exp{expression}.json', d.head(race, k, expression))
            objects.append(obj('Cabeza_'+d.NOMBRES[race][k], h, k == 0))
        for k in range(4):
            b = d.body(race, k)
            write(f'{race}/cuerpo_{k:02}.json', b)
            objects.append(obj('Cuerpo_'+d.CUERPOS[k], b, k == 0))
        for side in ('L', 'R'):
            for pose in (0, 1):
                h = d.hand(race, side, pose)
                write(f'{race}/mano_{side}_{pose}.json', h)
                objects.append(obj('Mano_'+side+'_'+str(pose), h, pose == 0))
        checks = {}
        for bone_name, meshobj in [('HEAD', objects[0]), ('LARM2', objects[8])]:
            meshobj.hide_set(False)
            def evaluated():
                bpy.context.view_layer.update()
                evaluated_obj = meshobj.evaluated_get(bpy.context.evaluated_depsgraph_get())
                return [v.co.copy() for v in evaluated_obj.data.vertices]
            before = evaluated()
            pb = rig.pose.bones[bone_name]
            pb.rotation_mode = 'XYZ'
            pb.rotation_euler[0] = .25
            after = evaluated()
            moved = sum((a-b).length > 1e-5 for a, b in zip(before, after))
            if not moved:
                raise ValueError('Skin original inerte: '+bone_name)
            checks[bone_name] = moved
            pb.rotation_euler[0] = 0
        rigchecks[race] = checks
        scene = bpy.context.scene
        scene.render.engine = 'BLENDER_WORKBENCH'
        scene.display.shading.light = 'STUDIO'
        scene.display.shading.color_type = 'MATERIAL'
        scene.display.shading.show_cavity = True
        scene.display.shading.show_object_outline = True
        scene.render.resolution_x = 700
        scene.render.resolution_y = 900
        scene.render.resolution_percentage = 100
        camdata = bpy.data.cameras.new('Camera')
        cam = bpy.data.objects.new('Camera', camdata)
        bpy.context.collection.objects.link(cam)
        camdata.type = 'ORTHO'
        camdata.ortho_scale = 7.0
        scene.camera = cam
        for name, angle in [('frente', 0), ('perfil', math.pi/2), ('espalda', math.pi), ('trescuartos', .6)]:
            cam.location = (10*math.sin(angle), -10*math.cos(angle), 3.6)
            cam.rotation_euler = (Vector((0, 0, 3.1))-cam.location).to_track_quat('-Z', 'Y').to_euler()
            if args.render:
                scene.render.filepath = str((args.blend/(race+'_'+name+'.png')).resolve())
                bpy.ops.render.render(write_still=True)
        bpy.ops.wm.save_as_mainfile(filepath=str((args.blend/(race+'.blend')).resolve()))
        print('PASS Blender nativo '+race+': ocho cabezas, cuatro cuerpos, manos, rig '+str(checks))
    manifest = dict(version=1, original=True, license='original, made for this project',
                    source='tools/cac_nativas_diseno.py', bind=d.BIND, palette=d.PALETAS,
                     catalog=[r for r in d.catalog() if r['race'] in races], files=records, rig_checks=rigchecks,
                    acceptance='pendiente de revisión en el juego')
    (args.salida/'catalogo.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PASS exportacion original: %d archivos propios; cero lecturas de juegos'%len(records))


if __name__ == '__main__':
    main()
