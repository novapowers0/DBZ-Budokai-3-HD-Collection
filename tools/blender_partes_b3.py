#!/usr/bin/env python3
"""Puente local B3 -> Blender armature -> B3, sin arte distribuido.

Preparar/integrar se ejecutan con Python del proyecto; Blender solo lee JSON y
PNG locales. No requiere instalar Pillow/numpy dentro de Blender.
"""
import argparse
import hashlib
import json
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _modulos():
    sys.path[:0] = [str(ROOT / 'mod center hd' / 'cac'), str(ROOT / 'mod center hd'), str(ROOT / 'awo_tools')]
    import partes_originales as po
    import vista_heroe as vh
    return po, vh


def preparar(receta, us, salida, modelo=None):
    import tempfile
    import numpy as np
    from PIL import Image
    po, vh = _modulos()
    r = tomllib.loads(receta.read_text(encoding='utf-8'))
    ficha = po.ficha_de_receta(r)
    fid = int(modelo if modelo is not None else r.get('modelo', 181 if ficha['cuerpo'] == 27 else 166))
    if fid not in vh.rb.IDS[ficha['cuerpo']]['models']:
        raise ValueError('Modelo ajeno al cuerpo de la receta')
    if salida.exists():
        raise FileExistsError('La salida debe ser nueva: ' + str(salida))
    if not salida.parent.is_dir():
        raise FileNotFoundError('No existe la carpeta padre de salida')
    afs = vh.rb.Afs(str(us / 'data_cmn.afs'), tempfile.mkdtemp(prefix='blender_partes_'))
    original = afs.entry(fid)
    amb, summary = po.aplicar(original, ficha)
    base = vh.mr.Model(original)
    ctx = po._contexto(base)
    W = ctx['worlds']
    # Este formato es LOCAL: contiene la geometria derivada del juego del usuario.
    scene = dict(version=1, kind='b3_skin_local', fid=fid, ficha=ficha,
                 source_sha256=hashlib.sha256(original).hexdigest(),
                 rig=dict(labels=base.labels, parents=list(map(int, base.parent)), worlds=W.tolist()),
                 meshes=[], resumen=summary)
    salida.mkdir()
    (salida / 'fuente.amb').write_bytes(original)
    for awg in range(base.avb.n_awg):
        m = vh.mr.Model(amb, awg=awg)
        F = po._marcos(m, base, ctx, awg)
        P = np.einsum('nij,nj->ni', F[:, :3, :3], m.pos) + F[:, :3, 3]
        N = np.einsum('nij,nj->ni', F[:, :3, :3], m.nrm)
        if awg == 0:
            bones = m.vbone.tolist()
        else:
            label = amb[m.avb.awg0 + 0x40:m.avb.awg0 + 0x60].split(b'\0')[0].decode('latin1')
            bones = [ctx['labels'][po._canon(label)]] * len(P)
        mats = []
        for k, mat in enumerate(m.mats):
            tex = vh._textura_cocida(m, mat)
            name = 'awg%d_mat%d.png' % (awg, k)
            Image.fromarray((np.clip(tex, 0, 1) * 255 + .5).astype(np.uint8)).save(salida / name)
            # Colores/umbrales de la rampa real; referencias de shader para el roundtrip.
            ramp = vh.tramos_rampa(m, mat)
            shades = [(float(l), np.asarray(c)[..., :3].mean(axis=(0, 1)).tolist()) for l, c in ramp]
            mats.append(dict(tex=name, env=bool(mat['env']), shader=int(mat['shader']),
                             diffuse=mat['diffuse'].tolist(), rampa=shades))
        scene['meshes'].append(dict(awg=awg, pos=P.tolist(), nrm=N.tolist(), uv=m.uv.tolist(),
                                    tris=m.tris.tolist(), bones=bones, materials=mats,
                                    tri_mat=m.tri_mat.tolist()))
    (salida / 'escena.json').write_text(json.dumps(scene), encoding='utf-8')
    (salida / 'blender.toml').write_text('kind = "original_b3"\nescena = "escena.json"\n', encoding='utf-8')
    print('PASS preparar: modelo %d, %d AWG con huesos/pesos; fuente local %s' % (fid, len(scene['meshes']), salida))


def blender_main(receta, args):
    import bpy
    from mathutils import Matrix, Vector
    scene_path = (args.receta.parent / receta['escena']).resolve()
    source = json.loads(scene_path.read_text(encoding='utf-8'))
    if source.get('kind') != 'b3_skin_local' or source.get('version') != 1:
        raise ValueError('Escena B3 local no compatible')
    if args.salida.exists() and any(args.salida.iterdir()):
        raise FileExistsError('La carpeta Blender debe estar vacia o ser nueva')
    args.salida.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    S = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
    Si = S.inverted()
    cv = lambda p: (p[0], -p[2], p[1])
    invcv = lambda p: (p[0], p[2], -p[1])
    rigdata = source['rig']
    arm = bpy.data.armatures.new('B3_bind_original')
    rig = bpy.data.objects.new('B3_rig_original', arm)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for label, world in zip(rigdata['labels'], rigdata['worlds']):
        b = arm.edit_bones.new(label)
        b.matrix = S @ Matrix(world) @ Si
        b.length = .35
    for k, parent in enumerate(rigdata['parents']):
        if parent >= 0 and parent != k:
            arm.edit_bones[k].parent = arm.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    objects = []
    for data in source['meshes']:
        mesh = bpy.data.meshes.new('AWG_%02d' % data['awg'])
        mesh.from_pydata([cv(p) for p in data['pos']], [], data['tris'])
        mesh.update()
        for p in mesh.polygons:
            p.use_smooth = True
        mesh.normals_split_custom_set_from_vertices([cv(n) for n in data['nrm']])
        obj = bpy.data.objects.new(mesh.name, mesh)
        obj['awg'] = data['awg']
        obj['source_sha256'] = source['source_sha256']
        bpy.context.collection.objects.link(obj)
        for k, label in enumerate(rigdata['labels']):
            ids = [i for i, bone in enumerate(data['bones']) if bone == k]
            if ids:
                obj.vertex_groups.new(name=label).add(ids, 1., 'REPLACE')
        modifier = obj.modifiers.new('Skin_B3_real', 'ARMATURE')
        modifier.object = rig
        obj.parent = rig
        uv = mesh.uv_layers.new(name='B3_UV')
        for loop in mesh.loops:
            uv.data[loop.index].uv = data['uv'][loop.vertex_index]
        for k, meta in enumerate(data['materials']):
            mat = bpy.data.materials.new('%s_mat_%d' % (mesh.name, k))
            mat['b3_shader'] = str(meta['shader'])  # FFFFFFFF no cabe en IDProperty int32
            mat['b3_env'] = meta['env']
            mat.use_nodes = True
            tree = mat.node_tree
            tree.nodes.clear()
            output = tree.nodes.new('ShaderNodeOutputMaterial')
            emission = tree.nodes.new('ShaderNodeEmission')
            texture = tree.nodes.new('ShaderNodeTexImage')
            texture.image = bpy.data.images.load(str(scene_path.parent / meta['tex']), check_existing=True)
            texture.image.pack()
            diffuse = tree.nodes.new('ShaderNodeBsdfDiffuse')
            s2rgb = tree.nodes.new('ShaderNodeShaderToRGB')
            ramp = tree.nodes.new('ShaderNodeValToRGB')
            ramp.color_ramp.interpolation = 'CONSTANT'
            # El shader nativo sigue intacto en el bin. Esta rampa es para authoring EEVEE.
            shades = meta['rampa'] or [(0., [.40] * 3), (.45, [.75] * 3), (.75, [1.] * 3)]
            while len(ramp.color_ramp.elements) > 1:
                ramp.color_ramp.elements.remove(ramp.color_ramp.elements[-1])
            for j, (level, rgb) in enumerate(shades):
                e = ramp.color_ramp.elements[0] if j == 0 else ramp.color_ramp.elements.new(max(0., min(1., level)))
                e.position = max(0., min(1., level))
                e.color = (*rgb, 1.)
            multiply = tree.nodes.new('ShaderNodeMixRGB')
            multiply.blend_type = 'MULTIPLY'
            multiply.inputs[0].default_value = 1.
            tree.links.new(diffuse.outputs[0], s2rgb.inputs[0])
            tree.links.new(s2rgb.outputs[0], ramp.inputs[0])
            tree.links.new(texture.outputs['Color'], multiply.inputs[1])
            tree.links.new(ramp.outputs[0], multiply.inputs[2])
            tree.links.new(multiply.outputs[0], emission.inputs[0])
            tree.links.new(emission.outputs[0], output.inputs['Surface'])
            mesh.materials.append(mat)
        for polygon, material in zip(mesh.polygons, data['tri_mat']):
            polygon.material_index = material
        if data['awg']:
            obj.hide_render = True  # expresiones alternativas, no superponerlas al L00
            obj.hide_set(True)
        objects.append(obj)
    # Medir que el modificador realmente mueve vertices (cabeza Y cadena de cola).
    check = {}
    meshobj = objects[0]
    def evaluated():
        bpy.context.view_layer.update()
        evaluated_obj = meshobj.evaluated_get(bpy.context.evaluated_depsgraph_get())
        return [v.co.copy() for v in evaluated_obj.data.vertices]
    before = evaluated()
    for ending in ('_HEAD', '_T_TAIL4'):
        name = next(n for n in rigdata['labels'] if n.endswith(ending))
        pb = rig.pose.bones[name]
        pb.rotation_mode = 'XYZ'
        pb.rotation_euler[0] = .18
        after = evaluated()
        moved = sum((a-b).length > 1e-5 for a, b in zip(after, before))
        pb.rotation_euler[0] = 0.
        bpy.context.view_layer.update()
        if moved == 0:
            raise RuntimeError('El rig no deforma ' + name)
        check[name] = moved
    bpy.context.scene['b3_escena_local'] = str(scene_path)
    exportar_blend(args.salida / 'pieza.b3mesh.json', check)
    scene = bpy.context.scene
    # Blender 5 utiliza BLENDER_EEVEE; resolver mediante asignacion sin depender de introspeccion RNA.
    for engine in ('BLENDER_EEVEE', 'BLENDER_EEVEE_NEXT'):
        try:
            scene.render.engine = engine
            break
        except TypeError:
            continue
    scene.render.resolution_x = 640
    scene.render.resolution_y = 640
    scene.render.resolution_percentage = 50
    camera = bpy.data.cameras.new('Camera')
    cam = bpy.data.objects.new('Camera', camera)
    bpy.context.collection.objects.link(cam)
    z = max(p[1] for p in source['meshes'][0]['pos'])
    cam.location = (z*1.4, -z*2.8, z*1.1)
    target = Vector((0, 0, z*.45))
    cam.rotation_euler = (target - cam.location).to_track_quat('-Z', 'Y').to_euler()
    camera.type = 'ORTHO'
    camera.ortho_scale = z*2.2
    scene.camera = cam
    light = bpy.data.lights.new('Luz', 'AREA')
    lamp = bpy.data.objects.new('Luz', light)
    bpy.context.collection.objects.link(lamp)
    lamp.location = (z, -z, z*2)
    light.energy = 1500
    light.size = z
    scene.render.filepath = str((args.salida / 'pieza.png').resolve())
    bpy.ops.wm.save_as_mainfile(filepath=str((args.salida / 'pieza.blend').resolve()))
    if not args.sin_render:
        bpy.ops.render.render(write_still=True)
    print('PASS Blender B3: armature real, pesos 1.0, deformacion medida ' + json.dumps(check))


def exportar_blend(salida, check=None):
    """Reexportar un .blend editado en bind: conserva ventanas, UV y huesos."""
    import bpy
    from mathutils import Vector
    if salida.exists():
        raise FileExistsError(salida)
    scene_path = Path(bpy.context.scene['b3_escena_local'])
    source = json.loads(scene_path.read_text(encoding='utf-8'))
    objects = {int(o['awg']): o for o in bpy.data.objects if o.type == 'MESH' and 'awg' in o}
    if len(objects) != len(source['meshes']):
        raise ValueError('Faltan AWG/expresiones; no los borres del .blend')
    out = dict(version=1, kind='b3_skin_roundtrip', escena=str(scene_path),
               source_sha256=source['source_sha256'], meshes=[], skin_check=check or {})
    invcv = lambda p: (p[0], p[2], -p[1])
    for data in source['meshes']:
        obj = objects[data['awg']]
        mesh = obj.data
        if len(mesh.vertices) != len(data['pos']) or [list(p.vertices) for p in mesh.polygons] != data['tris']:
            raise ValueError('No cambies la topologia ni el orden de ventanas')
        if any(abs(float(mesh.uv_layers['B3_UV'].data[l.index].uv[k]) - data['uv'][l.vertex_index][k]) > 1e-5
               for l in mesh.loops for k in range(2)):
            raise ValueError('Este recorrido conserva las UV del material B3')
        if any(abs(obj.matrix_basis[r][c] - (1. if r == c else 0.)) > 1e-6 for r in range(4) for c in range(4)):
            raise ValueError('Edita vertices en modo Edicion, no el transform del objeto')
        normals = [Vector((0, 0, 0)) for _ in mesh.vertices]
        for loop, normal in zip(mesh.loops, mesh.corner_normals):
            normals[loop.vertex_index] += normal.vector
        out['meshes'].append(dict(awg=data['awg'], pos=[invcv(v.co) for v in mesh.vertices],
                                 nrm=[invcv(n.normalized()) if n.length else data['nrm'][i] for i, n in enumerate(normals)],
                                 pesos=[[(obj.vertex_groups[g.group].name, g.weight) for g in v.groups] for v in mesh.vertices]))
    salida.write_text(json.dumps(out), encoding='utf-8')
    print('PASS reexportar: %d AWG editables, topologia/UV conservadas' % len(out['meshes']))


def registrar(entradas, juego, ident, nombre):
    """Instala un conjunto Blender LOCAL completo; el catalogo guarda solo su ID."""
    import shutil
    from datetime import datetime
    po, vh = _modulos()
    if not 3080 <= ident < 3100:
        raise ValueError('IDs Blender locales reservados: 3080..3099')
    cat = juego / 'mod center hd' / 'cac' / 'piezas_cac.toml'
    if not cat.is_file():
        raise FileNotFoundError('Falta el catalogo del creador: ' + str(cat))
    root = juego / 'user_data' / 'dbz3' / 'partes_blender'
    root.mkdir(parents=True, exist_ok=True)
    models, body = [], None
    stamp = datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    for i, entrada in enumerate(entradas):
        bundle = json.loads(entrada.read_text(encoding='utf-8'))
        sc = json.loads(Path(bundle['escena']).read_text(encoding='utf-8'))
        b = int(sc['ficha']['cuerpo'])
        if body is not None and body != b:
            raise ValueError('No mezclar cuerpos en un conjunto Blender')
        body = b
        fn = '%d_%s_%d.bin' % (ident, stamp, i)
        integrar(entrada, root / fn)
        raw = (Path(bundle['escena']).parent / 'fuente.amb').read_bytes()
        models.append(dict(fid=sc['fid'], geometria=po.huella_geometria(raw), archivo=fn,
                           sha256=hashlib.sha256((root / fn).read_bytes()).hexdigest()))
    if body not in po.CUERPOS:
        raise ValueError('Cuerpo Blender no admitido')
    donor = vh.rb.IDS[body]
    # Las formas pueden ser compartidas entre trajes: al menos una cadena COMPLETA.
    fids = {f['fid'] for f in models}
    per = donor['models_per_costume']
    costumes = [c for c in range(donor['costumes'])
                if set(donor['models'][c*per:c*per+donor['forms']]) <= fids]
    if not costumes:
        raise ValueError('Faltan formas: registra juntas todas las exportaciones del mismo traje')
    path = root / 'registro.json'
    reg = po._locales(juego)
    items = [p for p in reg.get('piezas', []) if p['id'] != ident]
    items.append(dict(id=ident, cuerpo=body, nombre=nombre, modelos=models, trajes=costumes))
    if path.exists():
        shutil.copy2(path, root / ('registro.%s.bak' % stamp))
    path.write_text(json.dumps(dict(version=1, piezas=items), ensure_ascii=False, indent=2), encoding='utf-8')
    text = cat.read_text(encoding='utf-8')
    # Cambiar de nombre, cuerpo o ampliar trajes actualiza SOLO esta entrada.
    import re
    blocks = re.split(r'(?m)^(?=\[\[pieza\]\]\s*$)', text)
    keep = []
    for block in blocks:
        entries = tomllib.loads(block).get('pieza', []) if block.strip() else []
        if not entries or entries[0].get('id') != ident:
            keep.append(block)
    entry = '\n[[pieza]]\ntipo = "original_torso"\nid = %d\nnombre = %s\nidx_afs = -1\narchivo = "local:%d:%s"\n' % (
        ident, json.dumps(nombre, ensure_ascii=False), body, ','.join(map(str, costumes)))
    shutil.copy2(cat, root / ('catalogo.%s.bak' % stamp))
    cat.write_text(''.join(keep) + entry, encoding='utf-8')
    print('PASS registrar: conjunto local %d, cuerpo %d, %d modelos y todas sus formas' % (ident, body, len(models)))


def integrar(entrada, salida):
    import struct
    import numpy as np
    po, vh = _modulos()
    bundle = json.loads(entrada.read_text(encoding='utf-8'))
    if bundle.get('kind') != 'b3_skin_roundtrip' or bundle.get('version') != 1:
        raise ValueError('Exportacion Blender desconocida')
    scene_path = Path(bundle['escena'])
    scene = json.loads(scene_path.read_text(encoding='utf-8'))
    original = (scene_path.parent / 'fuente.amb').read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    if digest != bundle['source_sha256'] or digest != scene['source_sha256']:
        raise ValueError('La exportacion no pertenece a este modelo fuente')
    amb, _ = po.aplicar(original, scene['ficha'])
    base = vh.mr.Model(original)
    ctx = po._contexto(base)
    if len(bundle['meshes']) != len(scene['meshes']):
        raise ValueError('Faltan expresiones AWG')
    out = bytearray(amb)
    for expected, data in zip(scene['meshes'], bundle['meshes']):
        if data['awg'] != expected['awg'] or len(data['pos']) != len(expected['pos']):
            raise ValueError('Topologia modificada: este recorrido conserva ventanas B3')
        model = vh.mr.Model(amb, awg=data['awg'])
        P, N = np.asarray(data['pos'], float), np.asarray(data['nrm'], float)
        if P.shape != model.pos.shape or N.shape != model.nrm.shape or not np.isfinite(P).all() or not np.isfinite(N).all():
            raise ValueError('Posiciones/normales invalidas')
        if len(data['pesos']) != len(P):
            raise ValueError('Faltan pesos reales')
        for weights, bone in zip(data['pesos'], expected['bones']):
            if len(weights) != 1 or weights[0][0] != base.labels[bone] or abs(weights[0][1] - 1.) > 1e-6:
                raise ValueError('B3 necesita el mismo hueso efectivo con peso 1 por ventana')
        W = po._marcos(model, base, ctx, data['awg'])
        local = np.linalg.solve(W[:, :3, :3], (P - W[:, :3, 3])[..., None])[..., 0]
        normal = np.linalg.solve(W[:, :3, :3], N[..., None])[..., 0]
        for i, (p, n) in enumerate(zip(local, normal)):
            at = model.avb.vb0 + 44*i
            struct.pack_into('>3f', out, at, *p)
            struct.pack_into('>3f', out, at + 20, *n)
    if salida.exists():
        raise FileExistsError(salida)
    salida.write_bytes(out)
    print('PASS integrar: %d AWG, huesos/pesos y materiales B3 conservados -> %s' % (len(bundle['meshes']), salida))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('preparar')
    p.add_argument('receta', type=Path)
    p.add_argument('--us', type=Path, required=True)
    p.add_argument('--salida', type=Path, required=True)
    p.add_argument('--modelo', type=int)
    p = sub.add_parser('integrar')
    p.add_argument('entrada', type=Path)
    p.add_argument('--salida', type=Path, required=True)
    p = sub.add_parser('registrar')
    p.add_argument('entradas', type=Path, nargs='+')
    p.add_argument('--juego', type=Path, required=True)
    p.add_argument('--id', type=int, default=3080)
    p.add_argument('--nombre', default='Conjunto Blender local')
    args = ap.parse_args()
    if args.cmd == 'preparar':
        preparar(args.receta, args.us, args.salida, args.modelo)
    elif args.cmd == 'integrar':
        integrar(args.entrada, args.salida)
    else:
        registrar(args.entradas, args.juego, args.id, args.nombre)


if __name__ == '__main__':
    main()
