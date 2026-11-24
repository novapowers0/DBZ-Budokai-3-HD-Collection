"""Crea las 24 fuentes Blender originales y JSON sin abrir recursos de juegos.

blender --background --factory-startup --python-exit-code 1 --python
tools/cac_nm_blender.py -- --salida CARPETA_NUEVA [--render]
"""
import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    import bpy
    from mathutils import Vector
    sys.path[:0] = [str(ROOT / 'mod center hd' / 'cac'), str(ROOT / 'tools')]
    import nativas_namek_majin as nm
    import blender_costuras as seam
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--salida', type=Path, required=True)
    p.add_argument('--render', action='store_true')
    a = p.parse_args(sys.argv[sys.argv.index('--')+1:])
    # PROCEDENCIA.md puede existir; nunca reemplazar recursos/modelos anteriores.
    if not a.salida.parent.is_dir():
        raise FileNotFoundError('No existe el directorio padre')
    if a.salida.exists() and any(x.name != 'PROCEDENCIA.md' for x in a.salida.iterdir()):
        raise FileExistsError('Elegir salida nueva o mover la anterior al respaldo')
    a.salida.mkdir(exist_ok=True)
    cv = lambda p: (p[0], -p[2], p[1])
    records, checks = [], {}
    for race in ('namek', 'majin'):
        bpy.ops.object.select_all(action='SELECT')
        bpy.ops.object.delete(use_global=False)
        arm = bpy.data.armatures.new('Rig_original_'+race)
        rig = bpy.data.objects.new('Rig_original_'+race, arm)
        bpy.context.collection.objects.link(rig)
        bpy.context.view_layer.objects.active = rig
        rig.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT')
        for name, (pos, parent) in nm.RIG.items():
            bone = arm.edit_bones.new('NM_'+name)
            bone.head = cv(pos)
            # Basis identity: local XY is horizontal, as seam helper requires.
            # The bone's drawing direction need not follow the anatomical limb.
            bone.tail = Vector(bone.head)+Vector((0, .18, 0))
        for name, (pos, parent) in nm.RIG.items():
            if parent:
                arm.edit_bones['NM_'+name].parent = arm.edit_bones['NM_'+parent]
        bpy.ops.object.mode_set(mode='OBJECT')
        materials = []
        for name, rgba in nm.PALETA.items():
            mat = bpy.data.materials.new('Original_'+name)
            mat.diffuse_color = rgba
            materials.append(mat)
        objects, meshes = [], []
        for meta in [x for x in nm.catalogo() if x['raza'] == race]:
            mesh = nm.generar(meta['id'])
            data = bpy.data.meshes.new('%d_%s' % (meta['id'], meta['nombre']))
            data.from_pydata([cv(x) for x in mesh['P']], [], mesh['tris'])
            data.update()
            obj = bpy.data.objects.new(data.name, data)
            bpy.context.collection.objects.link(obj)
            for mat in materials:
                data.materials.append(mat)
            uv = data.uv_layers.new(name='UV_originales')
            region_names = list(nm.PALETA)
            for poly in data.polygons:
                reg = mesh['regiones'][poly.vertices[0]]
                if any(mesh['regiones'][v] != reg for v in poly.vertices):
                    raise ValueError('Triángulo cruza regiones de color')
                poly.material_index = region_names.index(reg)
                poly.use_smooth = True
                for li in poly.loop_indices:
                    uv.data[li].uv = mesh['UV'][data.loops[li].vertex_index]
            for bone in set(mesh['huesos']):
                ids = [i for i, b in enumerate(mesh['huesos']) if b == bone]
                obj.vertex_groups.new(name='NM_'+bone).add(ids, 1., 'REPLACE')
            obj.modifiers.new('Skin_original_1_peso', 'ARMATURE').object = rig
            obj.parent = rig
            obj['id_cac'] = meta['id']
            obj['procedencia'] = mesh['procedencia']
            obj['licencia'] = mesh['licencia']
            # Se utiliza el ayudante de terraCAC_SEAM de verdad en ambos bordes.
            zone = 'cuello_cabeza' if meta['tipo'] == 'cabeza' else 'cuello_cuerpo'
            order = seam.ajustar(obj, rig, 'cuello', mesh['costuras'][zone])
            for v in order:
                co = data.vertices[v].co
                mesh['P'][v] = [float(co.x), float(co.z), float(-co.y)]
                mesh['huesos'][v] = 'NECK'
            # El intercambio de coordenadas puede invertir el orden canónico;
            # guardar la correspondencia realmente aplicada, no una declaración.
            mesh['costuras'][zone] = order
            data.update()
            normals = [cv(x) for x in mesh['N']]
            # Normales comunes, radiales y horizontales en la unión compartida.
            for v in order:
                co = data.vertices[v].co
                n = Vector((co.x, co.y, 0)).normalized()
                normals[v] = n
                mesh['N'][v] = [float(n.x), float(n.z), float(-n.y)]
            data.normals_split_custom_set_from_vertices(normals)
            raw = json.dumps(mesh, ensure_ascii=False, separators=(',', ':'), allow_nan=False).encode('utf-8')
            path = a.salida / ('%d.json' % meta['id'])
            with path.open('xb') as f:
                f.write(raw)
            records.append(dict(id=meta['id'], archivo=path.name,
                                sha256=hashlib.sha256(raw).hexdigest(),
                                vertices=len(mesh['P']), triangulos=len(mesh['tris'])))
            visible = meta['id'] % 100 in (0, 40)
            obj.hide_render = not visible
            obj.hide_set(not visible)
            objects.append(obj)
            meshes.append(mesh)
        # Rotación medible, no solo existencia de grupos.
        for bone, obj in [('HEAD',objects[0]), ('LARM2',objects[8]), ('LLEG2',objects[8])]:
            def sample():
                bpy.context.view_layer.update()
                evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
                return [v.co.copy() for v in evaluated.data.vertices]
            before = sample()
            pb = rig.pose.bones['NM_'+bone]
            pb.rotation_mode = 'XYZ'
            pb.rotation_euler[0] = .30
            after = sample()
            moved = sum((u-v).length > 1e-5 for u,v in zip(before,after))
            pb.rotation_euler[0] = 0
            if moved == 0:
                raise ValueError('Armature no mueve '+bone)
            checks[race+'_'+bone] = moved
        bpy.context.view_layer.update()
        # Comprobar los 32 cruces de cabeza/cuerpo de cada raza en dos poses.
        seam_errors = []
        for head, hm in zip(objects[:8],meshes[:8]):
            for body, bm in zip(objects[8:],meshes[8:]):
                for angle in (0, .5):
                    rig.pose.bones['NM_NECK'].rotation_mode='XYZ'
                    rig.pose.bones['NM_NECK'].rotation_euler[0]=angle
                    bpy.context.view_layer.update()
                    dg=bpy.context.evaluated_depsgraph_get()
                    hh=head.evaluated_get(dg).data
                    bb=body.evaluated_get(dg).data
                    hp=[hh.vertices[v].co for v in hm['costuras']['cuello_cabeza']]
                    bp=[bb.vertices[v].co for v in bm['costuras']['cuello_cuerpo']]
                    err=max(min((u-v).length for v in bp) for u in hp)
                    seam_errors.append(err)
        rig.pose.bones['NM_NECK'].rotation_euler[0]=0
        if max(seam_errors) > 1e-6:
            raise ValueError('Cuello separado en pose: '+str(max(seam_errors)))
        checks[race+'_costura_max'] = max(seam_errors)
        checks[race+'_combinaciones_poses'] = len(seam_errors)
        scene=bpy.context.scene
        scene.render.engine='BLENDER_WORKBENCH'
        scene.display.shading.light='STUDIO'
        scene.display.shading.color_type='MATERIAL'
        scene.display.shading.show_shadows=True
        scene.display.shading.show_cavity=True
        scene.display.shading.show_object_outline=True
        scene.world.color=(.12,.12,.12)
        scene.view_settings.view_transform='Standard'
        scene.render.resolution_x=900
        scene.render.resolution_y=900
        scene.render.resolution_percentage=100
        camera=bpy.data.cameras.new('Camera')
        cam=bpy.data.objects.new('Camera',camera)
        bpy.context.collection.objects.link(cam)
        scene.camera=cam
        camera.type='ORTHO'
        camera.ortho_scale=7.8
        for label,angle in [('frente',0),('derecha',math.pi/2),('espalda',math.pi),('izquierda',-math.pi/2)]:
            cam.location=(12*math.sin(angle),-12*math.cos(angle),3.6)
            cam.rotation_euler=(Vector((0,0,3.5))-cam.location).to_track_quat('-Z','Y').to_euler()
            if a.render:
                scene.render.filepath=str((a.salida/(race+'_'+label+'.png')).resolve())
                bpy.ops.render.render(write_still=True)
        bpy.ops.wm.save_as_mainfile(filepath=str((a.salida/(race+'.blend')).resolve()))
        print('PASS Blender %s: 8 cabezas, 4 cuerpos, rig medido, 64 poses de costura sin huecos'%race, flush=True)
    manifest=dict(version=1, original=True, licencia='CC0-1.0',
                  generador='mod center hd/cac/nativas_namek_majin.py',
                  catalogo=nm.catalogo(), archivos=records, pruebas_blender=checks,
                  aprobacion_artistica='pendiente de comparación real en B3')
    with (a.salida/'catalogo_nm.json').open('x',encoding='utf-8') as f:
        json.dump(manifest,f,ensure_ascii=False,indent=2)
    print('PASS exportación NM: 24 modelos originales, 2 fuentes Blender, cero lecturas de juegos',flush=True)


if __name__=='__main__':
    main()
