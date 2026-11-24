#!/usr/bin/env python3
"""Piezas originales incluidas: arte propio -> esqueleto anfitrión B3.

No adquiere arte de discos. El #AMB que entrega el exportador es solamente el
contenedor de animación/materiales del motor; se sustituye toda su superficie.
Una selección de cabeza o cuerpo monta la otra mitad original por defecto.
"""
from __future__ import annotations

import copy
import hashlib
import json
import re
from pathlib import Path

import numpy as np

VERSION = 2
COMPAT = {'freezer': (27, 38), 'namek': (11,), 'majin': (34, 35, 36)}
RAZA = {c: r for r, cs in COMPAT.items() for c in cs}
BASE = {'freezer': 3300, 'namek': 3400, 'majin': 3500}
ROOT = Path(__file__).resolve().parents[2]


def catalogo_b3():
    # El selector no depende de que haya una instalación de Blender.
    nombres = ('Arista', 'Marea', 'Cometa', 'Ónice', 'Astilla', 'Albor', 'Prisma', 'Cenit')
    cuerpos = ('Caminante', 'Guardián', 'Explorador', 'Centinela')
    rows = [dict(tipo='original_'+zona, id=3300+offset+k,
                 nombre='Nativa: '+n, idx_afs=-1, archivo='nativa:freezer/%s_%02d.json'%(kind, k))
            for zona, offset, kind, names in [('torso', 0, 'cabeza', nombres),
                                              ('guantes', 40, 'cuerpo', cuerpos)]
            for k, n in enumerate(names)]
    import nativas_namek_majin as nm
    rows.extend(dict(p, nombre='Nativa: '+p['nombre']) for p in nm.catalogo_b3())
    return rows


def seleccion_b3(ficha):
    import arcosiano_original
    modular = arcosiano_original.seleccion(ficha)
    if modular:
        return dict(raza='arcosiano', **modular)
    race = RAZA.get(int(ficha.get('cuerpo', -1)))
    ids = [int(ficha.get(k, -1)) for k in ('b3_torso', 'b3_guantes')]
    active = any(3300 <= x < 3600 for x in ids)
    if not active:
        return {}
    if race is None:
        raise ValueError('Piezas nativas de raza en un cuerpo incompatible')
    base = BASE[race]
    for ident, lo, count in ((ids[0], base, 8), (ids[1], base+40, 4)):
        if ident >= 0 and not lo <= ident < lo+count:
            raise ValueError('No se mezclan recetas antiguas ni piezas de otra raza con un cuerpo nativo completo')
    # Los dos selectores conservan sus campos persistentes anteriores.
    return dict(raza=race, cabeza=ids[0] if ids[0] >= 0 else base,
                cuerpo=ids[1] if ids[1] >= 0 else base+40)


def _read_verified(root, relative, manifest):
    record = next((r for r in manifest['files'] if r['file'] == relative), None)
    if record is None:
        raise ValueError('Recurso original ausente del manifiesto: '+relative)
    payload = (root/relative).read_bytes()
    if hashlib.sha256(payload).hexdigest() != record['sha256']:
        raise ValueError('Recurso original modificado sin regenerar su manifiesto: '+relative)
    return json.loads(payload)


def recurso(ident, juego=None, expression=0):
    """Sólo lee los JSON propios distribuidos, sin generadores ni juegos extra."""
    root = Path(juego) if juego else ROOT
    if 3300 <= ident < 3400:
        folder = root/'res/heroe/partes_nativas_freezer'
        manifest = json.loads((folder/'catalogo.json').read_text(encoding='utf-8'))
        kind, k = ('cabeza', ident-3300) if ident < 3340 else ('cuerpo', ident-3340)
        path = 'freezer/%s_%02d%s.json'%(kind, k, '_exp%d'%expression if kind == 'cabeza' and expression else '')
        mesh = _read_verified(folder, path, manifest)
        mesh['rig'] = {n: {'pos': v} for n, v in manifest['bind'].items()}
        mesh['palette'] = manifest['palette']['freezer']
        return mesh
    folder = root/'res/heroe/partes_nativas/namek_majin'
    path = folder/('%d.json'%ident)
    if not path.is_file():
        raise FileNotFoundError('Pieza original aún no exportada: '+str(path))
    mesh = json.loads(path.read_text(encoding='utf-8'))
    if mesh.get('id') != ident or mesh.get('kind') != 'cac_original_nm':
        raise ValueError('Recurso original de identidad incorrecta')
    return mesh


def _suffix(label):
    return label.lstrip('X').split('_', 1)[-1]


def _resolve(suffix, labels):
    keys = {_suffix(n): i for i, n in enumerate(labels)}
    if suffix in keys:
        return keys[suffix]
    if suffix in ('LHAND', 'RHAND'):
        side = suffix[0]
        for alternative in ('L00_'+side+'HAND', side+'HANDROT', side+'ARM2'):
            if alternative in keys:
                return keys[alternative]
    for prefix in ('L00_LHAND', 'L00_RHAND'):
        if suffix == prefix:
            return _resolve(suffix[4:], labels)
    if 'HAIR' in suffix:
        return keys.get('HEAD')
    if suffix.startswith(('MANT', 'SPAT')):
        return keys.get('CHEST')
    if suffix.endswith('FOOT2'):
        return keys.get(suffix[0]+'FOOT1')
    if suffix in ('LCHN', 'RCHN'):
        return keys.get(suffix[0]+'ARM1')
    if suffix == 'NECK':
        return keys.get('HEAD')
    return None


def _rotation(a, b):
    a, b = np.asarray(a, float), np.asarray(b, float)
    a /= max(np.linalg.norm(a), 1e-9)
    b /= max(np.linalg.norm(b), 1e-9)
    v, c = np.cross(a, b), float(np.dot(a, b))
    if c < -.99999:
        axis = np.cross(a, (0., 0., 1.))
        if np.linalg.norm(axis) < .1:
            axis = np.cross(a, (0., 1., 0.))
        axis /= np.linalg.norm(axis)
        return 2*np.outer(axis, axis)-np.eye(3)
    k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3)+k+k@k/max(1+c, 1e-9)


def retarget(mesh, model):
    """Recalza los anclajes propios, conservando radios y topología de autor."""
    P, N = np.asarray(mesh['P'], float), np.asarray(mesh['N'], float)
    labels = mesh.get('bones', mesh.get('huesos'))
    rig = {n: np.asarray(v['pos'], float) for n, v in mesh['rig'].items()}
    worlds = np.asarray(model.worlds(), float)
    targets, indices = {}, {}
    for name in rig:
        b = _resolve(name, model.labels)
        if b is not None:
            targets[name], indices[name] = worlds[b, :3, 3].copy(), b
    if not {'HEAD', 'WAIST'} <= targets.keys():
        raise ValueError('Esqueleto sin HEAD/WAIST')
    scale = np.linalg.norm(targets['HEAD']-targets['WAIST'])/np.linalg.norm(rig['HEAD']-rig['WAIST'])
    if not np.isfinite(scale) or not .1 < scale < 100:
        raise ValueError('Proporción del esqueleto inválida')
    child = {}
    for side in ('L', 'R'):
        child.update({side+'ARM1': side+'ARM2', side+'ARM2': side+('HANDROT' if side+'HANDROT' in rig else 'HAND'),
                      side+'LEG1': side+'LEG2', side+'LEG2': side+'FOOT1'})
    bindings = mesh.get('bindings')
    # El proveedor NM tiene pesos rígidos; preservamos las transiciones longitudinales.
    if bindings is None:
        bindings = []
        for p, name in zip(P, labels):
            other = child.get(name)
            if other in rig:
                d = rig[other]-rig[name]
                t = float(np.clip(np.dot(p-rig[name], d)/np.dot(d, d), 0, 1))
                bindings.append((name, other, t))
            else:
                bindings.append((name, name, 0.))
    Q, NN, bones = np.empty_like(P), np.empty_like(N), []
    for i, (p, normal, name, bind) in enumerate(zip(P, N, labels, bindings)):
        a, b, t = bind
        if name not in indices:
            raise ValueError('Hueso propio sin correspondencia: '+name)
        if a not in targets or b not in targets:
            a = b = name
            t = 0.
        source = rig[a]*(1-t)+rig[b]*t
        dest = targets[a]*(1-t)+targets[b]*t
        end = b if a != b else child.get(a)
        R = np.eye(3)
        if end in targets and end in rig and np.linalg.norm(rig[end]-rig[a]) > 1e-6:
            R = _rotation(rig[end]-rig[a], targets[end]-targets[a])
        Q[i] = dest+R@(p-source)*scale
        NN[i] = R@normal
        bones.append(indices[name])
    NN /= np.maximum(np.linalg.norm(NN, axis=1, keepdims=True), 1e-9)
    if not np.isfinite(Q).all() or not np.isfinite(NN).all():
        raise ValueError('Retarget no finito')
    return Q, NN, np.asarray(bones, int)


def _atlas(mesh, model, ficha):
    """Atlas original deduplicado por (vértice, región): no triplica cada cara."""
    P, N, bones = retarget(mesh, model)
    tris = np.asarray(mesh['tris'], int)
    if 'palette' in mesh:
        colors = [tuple(int(c[i:i+2], 16) for i in (0, 2, 4)) for c in mesh['palette']]
        regions = list(range(len(colors)))
        face_regions = mesh['regions']
    else:
        regions = sorted(set(mesh['regiones']))
        colors = [tuple(round(c*255) for c in mesh['colores'][r][:3]) for r in regions]
        face_regions = [mesh['regiones'][int(t[0])] for t in tris]
        if any(any(mesh['regiones'][int(v)] != r for v in t) for t, r in zip(tris, face_regions)):
            raise ValueError('Triángulo propio cruza dos regiones de color')
    # Reutiliza los cuatro colores YA guardados por el creador.
    parts = ficha.get('partes', [])
    color_keys = {'piel': 0, 'piel_majin': 0, 'musculo': 1, 'sombra_majin': 1,
                  'tela': 2, 'ribete': 3}
    if 'palette' in mesh:
        color_keys = {0: 0, 1: 1, 2: 2, 3: 3}
    for i, region in enumerate(regions):
        slot = color_keys.get(region)
        if slot is not None and len(parts) > slot and any(parts[slot]):
            colors[i] = tuple(parts[slot])
    side = 4
    rgba = np.zeros((64, 64, 4), np.uint8)
    for i, rgb in enumerate(colors):
        x, y = i % side, i//side
        rgba[y*16:(y+1)*16, x*16:(x+1)*16, :3] = rgb
        # Tinta y ojos conservan contraste, el resto usa la rampa gris original.
        rgba[y*16:(y+1)*16, x*16:(x+1)*16, 3] = 255 if regions[i] in (5, 6, 7, 'tinta', 'ojos') else 0
    mapping, used, uv, triangles = {}, [], [], []
    for tri, region in zip(tris, face_regions):
        ri = regions.index(region)
        face = []
        for old in tri:
            key = (int(old), ri)
            if key not in mapping:
                mapping[key] = len(used)
                used.append(int(old))
                uv.append(((ri % side+.5)/side, (ri//side+.5)/side))
            face.append(mapping[key])
        triangles.append(face)
    used = np.asarray(used, int)
    return dict(P=P[used], N=N[used], bones=bones[used], UV=np.asarray(uv),
                tris=np.asarray(triangles), rgba=rgba)


def _freezer_hand(side, pose, juego):
    root = (Path(juego) if juego else ROOT)/'res/heroe/partes_nativas_freezer'
    manifest = json.loads((root/'catalogo.json').read_text(encoding='utf-8'))
    mesh = _read_verified(root, 'freezer/mano_%s_%d.json'%(side, pose), manifest)
    mesh['rig'] = {n: {'pos': v} for n, v in manifest['bind'].items()}
    mesh['palette'] = manifest['palette']['freezer']
    return mesh


def aplicar_b3(amb, ficha, juego=None):
    sel = seleccion_b3(ficha)
    if not sel:
        return bytes(amb), {}
    import model_render as mr
    import injertar_ropa as ir
    from awg_vertex_buffer import AwgVertexBuffer
    from b3_gateway import DrawTable
    model = mr.Model(bytes(amb))
    if sel['raza'] == 'arcosiano':
        import arcosiano_original
        meshes = arcosiano_original.piezas(ficha, juego)
        pieces = [_atlas(m, model, ficha) for m in meshes if m['tris']]
    else:
        head = recurso(sel['cabeza'], juego)
        body = recurso(sel['cuerpo'], juego)
        pieces = [_atlas(head, model, ficha), _atlas(body, model, ficha)]
        if sel['raza'] == 'freezer':
            pieces.extend(_atlas(_freezer_hand(s, 0, juego), model, ficha) for s in ('L', 'R'))
    # Cabeza y cuerpo comparten atlas por raza; NM paletas completas, no subpaletas.
    # Combinar sus atlas en una sola cuadrícula evita consumir materiales extra.
    grid = int(np.ceil(np.sqrt(len(pieces))))
    atlas_side = 1 << int(np.ceil(np.log2(grid*64)))
    atlas = np.zeros((atlas_side, atlas_side, 4), np.uint8)
    allp, alln, allb, alluv, allt = [], [], [], [], []
    offset = 0
    for i, p in enumerate(pieces):
        x, y = i % grid, i//grid
        atlas[y*64:(y+1)*64, x*64:(x+1)*64] = p['rgba']
        allp.append(p['P']); alln.append(p['N']); allb.append(p['bones'])
        alluv.append((p['UV']+(x, y))*64/atlas_side)
        allt.append(p['tris']+offset)
        offset += len(p['P'])
    P, N, bones, UV, tris = (np.concatenate(x) for x in (allp, alln, allb, alluv, allt))
    candidates = [i for i, d in enumerate(model.table.draws)
                  if d['prim'] == 5 and not model.mats[d['material']]['env']
                  and model.mats[d['material']]['tex'] < len(model.tex)]
    if not candidates:
        raise ValueError('Sin anfitrión strip para modelo original')
    host = max(candidates, key=lambda i: model.table.draws[i]['B'][1])
    material = model.table.draws[host]['material']
    tex = model.mats[material]['tex']
    # Se reemplaza TODA la malla base. No quedan caras/cuerpos nativos escondidos.
    job = dict(host=host, quitar=np.ones(len(model.tris), bool), P=P, N=N,
               bones=bones, UV=UV, tris=tris, rgba=atlas, tex_destino=tex,
               zona='modelo_original_completo')
    data, summary = ir.injertar_ropa_multi(bytes(amb), model, [job])
    avb = AwgVertexBuffer._parse(data)
    table = DrawTable(avb)
    # Rampa de autor, sin muestrear colores ni UV de ningún donante.
    # Elegimos otra entrada DXT3 de textura: todas las superficies activas son propias.
    import texture_b3 as tb
    _, textures = tb.parse_azt(data)
    ramp = next((t['idx'] for t in textures if t['idx'] != tex and t['size']-128 == t['w']*t['h']), None)
    if ramp is None:
        raise ValueError('Contenedor sin segunda textura para rampa propia')
    gray = np.zeros((64, 64, 4), np.uint8)
    gray[..., 3] = 255
    gray[:, :20, :3] = 72
    gray[:, 20:39, :3] = 34
    gray[:, 39:55, :3] = 0
    gray[:, 55:, :3] = 0
    ir._escribir_textura(avb.data, ramp, gray)
    flags = table.materials[material]['flags']
    table.set_material(material, tex=tex, ramp=ramp, flags=(flags[0] & ~0x40, flags[1]))
    data = bytes(avb.data)
    # Las variantes auxiliares se vacían para no superponer arte ajeno al original.
    # Las manos/expresiones propias se conectarán a sus poses antes de aceptación.
    disabled = []
    for awg in range(1, model.avb.n_awg):
        aux = AwgVertexBuffer._parse(data, awg=awg)
        dt = DrawTable(aux)
        for d in dt.draws:
            dt.set_range(d, d['A'][0], d['A'][1], d['B'][0], 0)
        data = bytes(aux.data)
        disabled.append(awg)
    summary.update(ids={k:v for k,v in sel.items() if k != 'raza'}, raza=sel['raza'],
                   original=True, procedencia='original, made for this project',
                   auxiliares_sin_arte_nativo=disabled, rampa_propia=ramp,
                   aviso='Prototipo: poses de mano y expresiones por conectar; aceptación visual pendiente')
    return data, summary


def formas_b3(afs, fid, ficha, juego, resumen, construir):
    sel = seleccion_b3(ficha)
    if not sel:
        return None
    if sel['raza'] == 'arcosiano':
        import arcosiano_original
        return arcosiano_original.construir_formas(afs, fid, ficha, juego, resumen, construir)
    import roster_build as rb
    body = int(ficha['cuerpo'])
    if int(ficha.get('donante', body)) != body:
        return []
    source = rb.IDS[body]
    costume = min(max(int(ficha.get('traje', 0)), 0), int(source['costumes'])-1)
    per = int(source['models_per_costume'])
    out = []
    for form in range(1, int(source['forms'])):
        idx = costume*per+form
        # Piccolo usa un único modelo para sus potenciaciones.
        model_fid = source['models'][idx] if idx < len(source['models']) else fid
        data, report = construir(afs.entry(model_fid), dict(ficha, pelo=0, cara=0, _nativa_forma=form), juego, afs)
        out.append((data, form))
        resumen['nativa_forma%d'%form] = dict(fid=model_fid, originales=report.get('piezas_b3', {}))
    return out
