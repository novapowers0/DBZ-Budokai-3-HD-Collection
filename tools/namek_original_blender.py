"""Fuente Blender y recursos NMC originales. No abre archivos de juegos.

blender --background --factory-startup --python-exit-code 1 --python
tools/namek_original_blender.py -- --salida res/heroe/namek_original --render
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
    sys.path[:0] = [str(ROOT/'mod center hd/cac'), str(ROOT/'tools')]
    import namek_original as nm
    import blender_costuras as seam
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--salida', type=Path, required=True)
    ap.add_argument('--render', action='store_true')
    args = ap.parse_args(sys.argv[sys.argv.index('--')+1:])
    out = args.salida.resolve()
    if not out.parent.is_dir():
        raise FileNotFoundError('No existe el directorio padre')
    if out.exists():
        raise FileExistsError('La salida debe ser nueva; conservar la anterior en respaldo')
    out.mkdir()
    # Proyección del contrato compartido: el recurso es autosuficiente en las
    # copias del juego, donde solo se distribuyen los .py de cac/.
    import tomllib
    rules=tomllib.loads((ROOT/'mod center hd/cac/razas_reglas.toml').read_text(encoding='utf-8'))
    namek=next(r for r in rules['raza'] if r['id']==2)
    if namek['formas']!=['f_nam_nail','f_nam_kami']:
        raise ValueError('Revisar adaptación NMC: cambió el contrato de formas')
    (out/'reglas_formas.toml').write_text(
        '# Proyección Namek de cac/razas_reglas.toml.\nversion = 1\n[[raza]]\nid = 2\n'
        'nombre = "Namekiano"\nformas = '+json.dumps(namek['formas'])+'\ntecnicas = []\n',encoding='utf-8')
    for form in ('base', 'f_nam_nail', 'f_nam_kami'):
        (out/form).mkdir()
    (out/'laminas').mkdir()
    cv = lambda p: (p[0], -p[2], p[1])
    bpy.ops.object.select_all(action='SELECT')
    # Solamente limpia la escena temporal --factory-startup.
    bpy.ops.object.delete(use_global=False)
    arm = bpy.data.armatures.new('Rig_original_NMC')
    rig = bpy.data.objects.new('Rig_original_NMC', arm)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for name, (pos, parent) in nm.RIG.items():
        b = arm.edit_bones.new('NMC_'+name)
        b.head = cv(pos)
        b.tail = Vector(b.head)+Vector((0, .18, 0))
    for name, (pos, parent) in nm.RIG.items():
        if parent:
            arm.edit_bones['NMC_'+name].parent = arm.edit_bones['NMC_'+parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    mats = []
    for region, rgba in nm.PALETA.items():
        m = bpy.data.materials.new('NMC_'+region)
        m.diffuse_color = rgba
        mats.append(m)
    allmeshes = [nm.base_corporal()]+[nm.generar(x['id']) for x in nm.catalogo()]
    objects, meshes, files = {}, {}, []
    for mesh in allmeshes:
        ident = mesh['id']
        data = bpy.data.meshes.new('NMC_%d'%ident)
        data.from_pydata([cv(x) for x in mesh['P']], [], mesh['tris'])
        data.update()
        obj = bpy.data.objects.new(data.name, data)
        bpy.context.collection.objects.link(obj)
        for m in mats:
            data.materials.append(m)
        uv = data.uv_layers.new(name='UV_original_NMC')
        regions = list(nm.PALETA)
        for poly in data.polygons:
            r = mesh['regiones'][poly.vertices[0]]
            if any(mesh['regiones'][v] != r for v in poly.vertices):
                raise ValueError('Cara cruza regiones')
            poly.material_index = regions.index(r)
            poly.use_smooth = True
            for li in poly.loop_indices:
                uv.data[li].uv = mesh['UV'][data.loops[li].vertex_index]
        for bone in set(mesh['huesos']):
            ids = [i for i, b in enumerate(mesh['huesos']) if b == bone]
            obj.vertex_groups.new(name='NMC_'+bone).add(ids, 1., 'REPLACE')
        obj.modifiers.new('Un_peso_B3', 'ARMATURE').object = rig
        obj.parent = rig
        obj['procedencia'] = mesh['procedencia']
        obj['licencia'] = mesh['licencia']
        obj['id_cac'] = ident
        if mesh['costuras']:
            order = seam.ajustar(obj, rig, 'cuello', mesh['costuras']['cuello'])
            for v in order:
                c = data.vertices[v].co
                mesh['P'][v] = [float(c.x), float(c.z), float(-c.y)]
                mesh['huesos'][v] = 'NECK'
                n = Vector((c.x, c.y, 0)).normalized()
                mesh['N'][v] = [float(n.x), 0., float(-n.y)]
            mesh['costuras']['cuello'] = order
        data.normals_split_custom_set_from_vertices([cv(n) for n in mesh['N']])
        objects[ident], meshes[ident] = obj, mesh
        forms = ('base',) if ident == 3599 else ('base', 'f_nam_nail', 'f_nam_kami')
        for form in forms:
            actual = dict(mesh, forma=form)
            payload = json.dumps(actual, ensure_ascii=False, separators=(',', ':'), allow_nan=False).encode('utf-8')
            rel = '%s/%d.json'%(form, ident)
            (out/rel).write_bytes(payload)
            files.append(dict(id=ident, forma=form, archivo=rel,
                              sha256=hashlib.sha256(payload).hexdigest(),
                              vertices=len(mesh['P']), triangulos=len(mesh['tris'])))
        obj.hide_render = True
        obj.hide_set(True)
    # Comprobación sobre el modificador real: todas las cabezas, seis poses.
    errors = []
    def evaluated(obj, ids):
        bpy.context.view_layer.update()
        ev = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        return [ev.data.vertices[i].co.copy() for i in ids]
    base = objects[3599]
    base.hide_set(False)
    for i in range(8):
        head = objects[3600+i]
        head.hide_set(False)
        for neck, body_angle in ((0,0),(.5,0),(-.5,0),(0,.3),(.4,.2),(-.3,-.2)):
            for name, angle in (('NECK',neck),('STMC',body_angle)):
                pb=rig.pose.bones['NMC_'+name]
                pb.rotation_mode='XYZ'
                pb.rotation_euler.x=angle
            hp=evaluated(head,meshes[3600+i]['costuras']['cuello'])
            bp=evaluated(base,meshes[3599]['costuras']['cuello'])
            errors.append(max(min((u-v).length for v in bp) for u in hp))
        head.hide_set(True)
    for pb in rig.pose.bones:
        pb.rotation_euler=(0,0,0)
    bpy.context.view_layer.update()
    if max(errors)>1e-6:
        raise ValueError('Separación de cuello: '+str(max(errors)))
    print('PASS NMC costuras: 8 cabezas x 6 poses, max=',max(errors),flush=True)
    # Confirmar que el rig mueve las extremidades, no sólo que existen grupos.
    moved={}
    for bone in ('HEAD','LARM2','LLEG2'):
        obj=objects[3600] if bone=='HEAD' else base
        obj.hide_set(False)
        before=evaluated(obj,range(len(obj.data.vertices)))
        pb=rig.pose.bones['NMC_'+bone]
        pb.rotation_mode='XYZ';pb.rotation_euler.x=.4
        after=evaluated(obj,range(len(obj.data.vertices)))
        moved[bone]=sum((a-b).length>1e-5 for a,b in zip(before,after))
        pb.rotation_euler.x=0
        obj.hide_set(True)
        if moved[bone]==0:
            raise ValueError('Rig sin movimiento: '+bone)
    scene=bpy.context.scene
    scene.render.engine='BLENDER_WORKBENCH'
    scene.display.shading.light='STUDIO'
    scene.display.shading.color_type='MATERIAL'
    scene.display.shading.show_shadows=True
    scene.display.shading.show_cavity=True
    scene.display.shading.show_object_outline=True
    scene.world.color=(.13,.15,.17)
    scene.view_settings.view_transform='Standard'
    camera=bpy.data.cameras.new('Camara_laminas_NMC')
    cam=bpy.data.objects.new('Camara_laminas_NMC',camera)
    bpy.context.collection.objects.link(cam);scene.camera=cam
    camera.type='ORTHO'
    labels=('frente','derecha','espalda','izquierda')
    if args.render:
        for slot,names in nm.NOMBRES.items():
            copies=[]
            full=slot in ('ropa','parches')
            spacing=6.7 if full else 2.4
            center=(len(names)-1)*spacing/2
            for i in range(len(names)):
                chosen={s:0 for s in nm.NOMBRES};chosen[slot]=i
                ids=[3599]+[nm.BASES[s]+k for s,k in chosen.items()]
                for ident in ids:
                    ob=bpy.data.objects.new('Lamina_%s_%d_%d'%(slot,i,ident),objects[ident].data)
                    bpy.context.collection.objects.link(ob)
                    ob.location.x=i*spacing-center
                    copies.append(ob)
            scene.render.resolution_x=max(960,380*len(names))
            scene.render.resolution_y=480 if full else 460
            scene.render.resolution_percentage=100
            camera.ortho_scale=len(names)*spacing+.3
            height=3.4 if full else 5.92
            for view,label in enumerate(labels):
                # Las columnas siguen siendo tipos: girar cada instancia en su
                # propio centro, sin apilar todas al mirar el conjunto de lado.
                for ob in copies:
                    ob.rotation_euler.z=-view*math.pi/2
                cam.location=(0,-30,height)
                cam.rotation_euler=(Vector((0,0,height))-cam.location).to_track_quat('-Z','Y').to_euler()
                scene.render.filepath=str(out/'laminas'/('%s_%s.png'%(slot,label)))
                bpy.ops.render.render(write_still=True)
            for ob in copies:
                bpy.data.objects.remove(ob,do_unlink=True)
    for ident,obj in objects.items():
        visible=ident==3599 or ident in nm.BASES.values()
        obj.hide_render=not visible;obj.hide_set(not visible)
    camera.ortho_scale=8
    scene.render.resolution_x=900;scene.render.resolution_y=900
    cam.location=(0,-15,3.4)
    cam.rotation_euler=(Vector((0,0,3.4))-cam.location).to_track_quat('-Z','Y').to_euler()
    bpy.ops.wm.save_as_mainfile(filepath=str(out/'namek_original.blend'))
    manifest=dict(version=nm.VERSION, original=True, licencia='CC0-1.0',
                  generador='mod center hd/cac/namek_original.py', catalogo=nm.catalogo(), archivos=files,
                  pruebas_blender=dict(costura_max=max(errors),costura_poses=len(errors),vertices_movidos=moved),
                  aprobacion_artistica='pendiente de cotejo en combate; láminas de taller')
    (out/'catalogo.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    print('PASS NMC Blender: 29 piezas + cuerpo neutro; 88 recursos; 48 poses; fuente .blend',flush=True)


if __name__=='__main__':
    main()
