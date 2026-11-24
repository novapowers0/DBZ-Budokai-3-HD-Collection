"""Exportación Blender 5.2 de las piezas diseñadas en DISENO_FRI.md."""
import argparse
import hashlib
import json
import math
import sys
from pathlib import Path


def main():
    import bpy
    from mathutils import Vector
    sys.path.insert(0,str(Path(__file__).resolve().parent))
    import r16_fri_modelos as d
    ap=argparse.ArgumentParser()
    ap.add_argument('--salida',type=Path,required=True)
    ap.add_argument('--laminas',action='store_true')
    args=ap.parse_args(sys.argv[sys.argv.index('--')+1:])
    if args.salida.exists() or not args.salida.parent.is_dir():
        raise ValueError('Se requiere carpeta nueva y padre existente')
    args.salida.mkdir()
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    cv=lambda p:(p[0],-p[2],p[1])
    inv=lambda p:[p[0],p[2],-p[1]]
    arm=bpy.data.armatures.new('Arcosiano_R16_original')
    rig=bpy.data.objects.new('Rig_original',arm)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active=rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for name,pos in d.BIND.items():
        b=arm.edit_bones.new(name); b.head=cv(pos); b.tail=Vector(b.head)+Vector((0,0,.2))
    parents={'STMC':'WAIST','CHEST':'STMC','NECK':'CHEST','HEAD':'NECK'}
    for s in ('L','R'):
        parents.update({s+'CHN':'CHEST',s+'ARM1':s+'CHN',s+'ARM2':s+'ARM1',s+'HAND':s+'ARM2',
                        s+'LEG1':'WAIST',s+'LEG2':s+'LEG1',s+'FOOT1':s+'LEG2',s+'FOOT2':s+'FOOT1'})
    for k in range(1,6): parents['T_TAIL%d'%k]='WAIST' if k==1 else 'T_TAIL%d'%(k-1)
    for n,p in parents.items():arm.edit_bones[n].parent=arm.edit_bones[p]
    bpy.ops.object.mode_set(mode='OBJECT')
    mats=[]
    for i,col in enumerate(d.PALETTE):
        m=bpy.data.materials.new('Region_%d_original'%i)
        m.diffuse_color=(*[int(col[j:j+2],16)/255 for j in (0,2,4)],1)
        mats.append(m)
    records=[]
    objects={}
    cases=[(s,k,f) for s,ns in d.NAMES.items() for k in range(len(ns)) for f in d.FORMS]
    cases += [(s,0,f) for s in ('base','mano_L','mano_R') for f in d.FORMS]
    cases += [('cara',k,f) for k in range(4) for f in d.FORMS]  # expresión de ataque separada
    for ordinal,(slot,k,form) in enumerate(cases):
        expression=1 if ordinal>=186 else 0
        mesh=d.build(slot,k,form,expression)
        name='%s_%02d_%s%s'%(slot,k,form,'_exp1' if expression else '')
        data=bpy.data.meshes.new(name)
        data.from_pydata([cv(p) for p in mesh['P']],[],mesh['tris'])
        data.update()
        ob=bpy.data.objects.new(name,data)
        bpy.context.collection.objects.link(ob)
        for mat in mats:data.materials.append(mat)
        uv=data.uv_layers.new(name='Regiones_de_autor')
        for p,r in zip(data.polygons,mesh['regions']):
            p.material_index=r; p.use_smooth=True
            for li in p.loop_indices: uv.data[li].uv=((r%4+.5)/4,(r//4+.5)/4)
        for bone in set(mesh['bones']):
            ob.vertex_groups.new(name=bone).add([i for i,b in enumerate(mesh['bones']) if b==bone],1.,'REPLACE')
        mod=ob.modifiers.new('Rig_original','ARMATURE');mod.object=rig
        ob.parent=rig
        ob['procedencia']='original, made for this project'
        ob.hide_render=not(form=='base' and k==0 and not expression)
        ob.hide_set(ob.hide_render)
        # Las posiciones distribuibles salen de la malla real de Blender.
        mesh['P']=[[round(x,7) for x in inv(v.co)] for v in data.vertices]
        mesh['tris']=[list(p.vertices) for p in data.polygons]
        mesh['blender']=bpy.app.version_string
        payload=json.dumps(mesh,ensure_ascii=False,separators=(',',':'),allow_nan=False).encode('utf-8')
        filename=name+'.json'
        (args.salida/filename).write_bytes(payload)
        records.append(dict(file=filename,slot=slot,variant=k,form=form,expression=expression,
                            sha256=hashlib.sha256(payload).hexdigest(),vertices=len(mesh['P']),triangles=len(mesh['tris'])))
        if form=='base' and not expression:objects[slot,k]=ob
    scene=bpy.context.scene
    scene.render.engine='BLENDER_WORKBENCH'
    scene.display.shading.light='STUDIO';scene.display.shading.color_type='MATERIAL'
    scene.display.shading.show_cavity=True;scene.display.shading.show_object_outline=True
    scene.display.shading.background_type='WORLD';scene.world.color=(.085,.10,.12)
    scene.render.image_settings.file_format='PNG'
    camdata=bpy.data.cameras.new('Camera_lamina')
    cam=bpy.data.objects.new('Camera_lamina',camdata)
    bpy.context.collection.objects.link(cam);scene.camera=cam;camdata.type='ORTHO'
    bpy.ops.wm.save_as_mainfile(filepath=str((args.salida/'arcosiano_original.blend').resolve()))
    manifest=dict(version=1,original=True,procedencia='original, made for this project',
                  blender=bpy.app.version_string,source='tools/r16_fri_modelos.py',bind=d.BIND,
                  palette=d.PALETTE,forms=d.FORMS,slots=d.NAMES,files=records,
                  acceptance='pendiente de revisión en combate')
    (args.salida/'catalogo.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    if args.laminas:
        for ob in list(bpy.data.objects):
            if ob.type=='MESH':ob.hide_render=True
        for slot,names in d.NAMES.items():
            copies=[]
            for k in range(len(names)):
                for view,angle in enumerate((0,math.pi/2,math.pi,-math.pi/2)):
                    for part,idx in [('base',0),('mano_L',0),('mano_R',0)]+[(s,k if s==slot else (5 if s=='cuernos' else 0)) for s in d.NAMES]:
                        source=objects[part,idx]
                        ob=bpy.data.objects.new('Lamina_%s_%d_%d_%s'%(slot,k,view,part),source.data)
                        bpy.context.collection.objects.link(ob)
                        ob.rotation_euler[2]=angle
                        ob.scale=(.5,.5,.5)
                        ob.location=(view*2.0,0,-k*3.65)
                        copies.append(ob)
            scene.render.resolution_x=1280;scene.render.resolution_y=len(names)*570
            scene.render.resolution_percentage=100
            center=Vector((3.,0,1.7-(len(names)-1)*3.65/2))
            cam.location=center+Vector((0,-60,0));cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
            camdata.ortho_scale=max(8.3,len(names)*3.65)
            scene.render.filepath=str((args.salida/('lamina_'+slot+'.png')).resolve())
            bpy.ops.render.render(write_still=True)
            # Conservar las piezas fuente; solamente ocultar los ejemplares de lámina.
            for ob in copies:ob.hide_render=True
    print('PASS Blender Arcosiano: %d recursos, 28 tipos, 6 estados, geometría propia'%len(records))


if __name__=='__main__':main()
