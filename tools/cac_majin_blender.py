"""Exporta el catálogo Majin original con Blender 5.2, rig y láminas propias."""
import argparse
import hashlib
import json
import math
import sys
from pathlib import Path


def main():
    import bpy
    import numpy as np
    from mathutils import Vector
    root = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(root/'mod center hd/cac'), str(root/'tools')]
    import partes_majin as design
    import blender_costuras as seams
    from variantes_formas import formas_raza
    parser = argparse.ArgumentParser()
    parser.add_argument('--salida', required=True, type=Path)
    parser.add_argument('--fuentes', required=True, type=Path)
    parser.add_argument('--render', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if args.salida.exists() or args.fuentes.exists():
        raise FileExistsError('La exportación requiere dos carpetas nuevas')
    for path in (args.salida, args.fuentes):
        if not path.parent.is_dir():
            raise FileNotFoundError(path.parent)
        path.mkdir()
    cv = lambda p: (p[0], -p[2], p[1])
    icv = lambda p: [float(p[0]), float(p[2]), float(-p[1])]
    records, checks = [], {}
    keys = list(design.PALETA)
    for form in formas_raza(4):
        (args.salida/form).mkdir()
        # Reinicia datos propios en memoria; no abre ni elimina archivos.
        bpy.ops.wm.read_factory_settings(use_empty=True)
        arm = bpy.data.armatures.new('Rig_Majin_original')
        rig = bpy.data.objects.new('Rig_Majin_original', arm)
        bpy.context.collection.objects.link(rig)
        bpy.context.view_layer.objects.active = rig
        rig.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT')
        for name, (pos, _) in design.RIG.items():
            b = arm.edit_bones.new('MJ_'+name)
            b.head = cv(pos)
            # La plantilla de costura trabaja en el XY local: Z local arriba.
            b.tail = Vector(b.head)+Vector((0, .24, 0))
        for name, (_, parent) in design.RIG.items():
            if parent:
                arm.edit_bones['MJ_'+name].parent = arm.edit_bones['MJ_'+parent]
        bpy.ops.object.mode_set(mode='OBJECT')
        mats = []
        for name, rgba in design.PALETA.items():
            mat = bpy.data.materials.new('Majin_'+name)
            mat.diffuse_color = rgba
            mats.append(mat)
        objects, meshes = {}, {}
        for row in design.catalogo_b3():
            ident = row['id']
            for ctype in (range(3) if ident >= 4060 else (0,)):
                mesh = design.generar(ident, form, ctype)
                # Variante de autor por forma, sin cambiar el anillo compartido.
                factor = {'f_maj_super': 1., 'f_maj_absorcion': 1.04,
                          'f_maj_absorbida': 1.08, 'f_maj_kid': .96, 'f_maj_kid2': .92}[form]
                seam_ids = set(sum(mesh['costuras'].values(), []))
                for i, p in enumerate(mesh['P']):
                    if i not in seam_ids:
                        p[0] *= factor
                        p[2] *= factor
                tag = str(ident)+('_c%d'%ctype if ident >= 4060 else '')
                data = bpy.data.meshes.new(tag)
                data.from_pydata([cv(p) for p in mesh['P']], [], mesh['tris'])
                data.update()
                ob = bpy.data.objects.new(row['nombre']+'_'+tag, data)
                bpy.context.collection.objects.link(ob)
                for mat in mats:
                    data.materials.append(mat)
                uv = data.uv_layers.new(name='UV_original')
                for poly, region in zip(data.polygons, mesh['regions']):
                    poly.material_index = region
                    poly.use_smooth = True
                    for li in poly.loop_indices:
                        uv.data[li].uv = mesh['UV'][data.loops[li].vertex_index]
                for name in set(mesh['huesos']):
                    ids = [i for i, b in enumerate(mesh['huesos']) if b == name]
                    ob.vertex_groups.new(name='MJ_'+name).add(ids, 1., 'REPLACE')
                mod = ob.modifiers.new('Skin_Majin_original', 'ARMATURE')
                mod.object = rig
                ob.parent = rig
                for border in mesh['costuras'].values():
                    seams.ajustar(ob, rig, 'cuello', border)
                mesh['P'] = [icv(v.co) for v in data.vertices]
                mesh['N'] = [icv(v.normal) for v in data.vertices]
                # Normal radial idéntica en las dos piezas de cuello.
                for i in seam_ids:
                    x, _, z = mesh['P'][i]
                    r = math.hypot(x, z)
                    mesh['N'][i] = [x/r, 0., z/r]
                mesh['fuente_blender'] = form+'.blend:'+ob.name
                mesh['variante_radio'] = factor
                payload = json.dumps(mesh, ensure_ascii=False, separators=(',', ':'), allow_nan=False).encode('utf-8')
                relative = form+'/'+tag+'.json'
                (args.salida/relative).write_bytes(payload)
                records.append(dict(file=relative, sha256=hashlib.sha256(payload).hexdigest(),
                                    vertices=len(mesh['P']), triangles=len(mesh['tris'])))
                objects[tag], meshes[tag] = ob, mesh
                ob.hide_render = True
                ob.hide_set(True)
        maxgap = 0.
        for hid in range(4020, 4024):
            h = meshes[str(hid)]
            hp = np.asarray(h['P'])[h['costuras']['cuello_cabeza']]
            for bid in range(4040, 4043):
                b = meshes[str(bid)]
                bp = np.asarray(b['P'])[b['costuras']['cuello_cuerpo']]
                maxgap = max(maxgap, float(np.min(np.linalg.norm(hp[:, None]-bp[None], axis=2), axis=1).max()))
        if maxgap > 1e-6:
            raise ValueError('Costuras incompatibles: '+str(maxgap))
        # Ejercitar el armature real exportado: cabeza y antebrazo deben mover piel.
        moved = {}
        for name, target in (('HEAD', '4020'), ('LARM2', '4040')):
            obj = objects[target]
            obj.hide_set(False)
            bpy.context.view_layer.update()
            before = [v.co.copy() for v in obj.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices]
            pb = rig.pose.bones['MJ_'+name]
            pb.rotation_mode = 'XYZ'
            pb.rotation_euler[0] = .25
            bpy.context.view_layer.update()
            after = [v.co.copy() for v in obj.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices]
            moved[name] = sum((a-b).length > 1e-5 for a, b in zip(before, after))
            if not moved[name]:
                raise ValueError('Rig inerte: '+name)
            pb.rotation_euler[0] = 0
            obj.hide_set(True)
        checks[form] = dict(cuello_gap=maxgap, rig_vertices_movidos=moved)
        for tag in ('4000', '4020', '4040', '4060_c0'):
            objects[tag].hide_render = False
            objects[tag].hide_set(False)
        scene = bpy.context.scene
        scene.render.engine = 'BLENDER_WORKBENCH'
        scene.display.shading.color_type = 'MATERIAL'
        scene.display.shading.light = 'STUDIO'
        scene.display.shading.show_cavity = True
        scene.display.shading.show_object_outline = True
        camdata = bpy.data.cameras.new('Camara')
        camdata.type = 'ORTHO'
        camdata.ortho_scale = 8.
        camera = bpy.data.objects.new('Camara', camdata)
        bpy.context.collection.objects.link(camera)
        scene.camera = camera
        scene.render.resolution_x, scene.render.resolution_y = 720, 900
        for name, angle in (('frente', 0), ('perfil', math.pi/2), ('espalda', math.pi), ('trescuartos', .6)):
            camera.location = (10*math.sin(angle), -10*math.cos(angle), 3.8)
            camera.rotation_euler = (Vector((0, 0, 3.7))-camera.location).to_track_quat('-Z', 'Y').to_euler()
            if args.render:
                scene.render.filepath = str((args.fuentes/(form+'_'+name+'.png')).resolve())
                bpy.ops.render.render(write_still=True)
        bpy.ops.wm.save_as_mainfile(filepath=str((args.fuentes/(form+'.blend')).resolve()))
        print('PASS Majin Blender '+form+': 27 recursos; cuello gap='+str(maxgap)+'; rig='+str(moved), flush=True)
    manifest = dict(version=design.VERSION, catalogo=design.catalogo_b3(), files=records,
                    controles=checks, procedencia='original, made for this project',
                    aceptacion_visual_en_juego='pendiente', fuentes=str(args.fuentes))
    (args.salida/'catalogo.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PASS exportacion Majin: %d recursos; 8 antenas, 4 rostros, 3 cuerpos, 4 prendas'%len(records), flush=True)


if __name__ == '__main__':
    main()
