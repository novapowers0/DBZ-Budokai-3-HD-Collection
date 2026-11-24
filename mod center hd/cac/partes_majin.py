"""Catálogo Majin de autor R16: piezas independientes, sin arte de discos.

El generador usa solamente primitivas matemáticas del pipeline previo. Cada
perfil, trayectoria, UV y color se define aquí; no llama a sus diseños previos.
Blender exporta los JSON originales y el rig. El juego solo consume esos JSON.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path

import numpy as np

VERSION = 1
ROOT = Path(__file__).resolve().parents[2]
ZONAS = ('torso', 'piernas', 'guantes', 'botas')
CUERPOS = (34, 35, 36)
TIPOS = (
    (4000, 'Antena', ('Estela', 'Caracola', 'Gemelas', 'Brote', 'Trenza', 'Horquilla', 'Vela', 'Cascada')),
    (4020, 'Rostro', ('Almendra', 'Bastión', 'Luna', 'Filo')),
    (4040, 'Cuerpo', ('Junco', 'Ánfora', 'Yunque')),
    (4060, 'Ropa', ('Chaleco Delta', 'Capa Cometa', 'Cinturón Órbita', 'Bombacho Nube')),
)
PALETA = dict(piel_majin=(.92, .60, .69, 1.), sombra_majin=(.60, .29, .40, 1.),
              tela=(.16, .34, .40, 1.), ribete=(.82, .55, .34, 1.),
              tinta=(.055, .065, .10, 1.), ojos=(.96, .91, .80, 1.))
# Maniquí propio. Los anclajes se recalzan al esqueleto anfitrión, nunca se
# extraen vértices ni proporciones del modelo nativo para crear las piezas.
RIG = {'WAIST': ((0, 2.9, 0), None), 'STMC': ((0, 3.65, 0), 'WAIST'),
       'CHEST': ((0, 4.45, 0), 'STMC'), 'NECK': ((0, 5.35, 0), 'CHEST'),
       'HEAD': ((0, 5.75, 0), 'NECK')}
for _side, _s in (('L', 1), ('R', -1)):
    for _n, _p, _parent in (
        ('ARM1', (.95, 4.6, 0), 'CHEST'), ('ARM2', (1.85, 4.6, 0), _side+'ARM1'),
        ('HAND', (2.65, 4.6, 0), _side+'ARM2'),
        ('LEG1', (.43, 2.8, 0), 'WAIST'), ('LEG2', (.43, 1.45, 0), _side+'LEG1'),
        ('FOOT1', (.43, .30, 0), _side+'LEG2'),
    ):
        RIG[_side+_n] = ((_p[0]*_s, _p[1], _p[2]), _parent)


def catalogo_b3():
    return [dict(tipo='original_'+ZONAS[z], id=base+k,
                 nombre='Tipo %d · %s' % (k+1, name), idx_afs=-1,
                 archivo='majin:%d' % (base+k), cuerpos=list(CUERPOS))
            for z, (base, _, names) in enumerate(TIPOS) for k, name in enumerate(names)]


def seleccion_b3(ficha):
    ids = [int(ficha.get('b3_'+z, -1)) for z in ZONAS]
    if not any(4000 <= x < 4100 for x in ids):
        return {}
    if int(ficha.get('cuerpo', -1)) not in CUERPOS:
        raise ValueError('Las piezas Majin requieren un cuerpo Majin')
    result = []
    for ident, (base, _, names) in zip(ids, TIPOS):
        if ident != -1 and not base <= ident < base+len(names):
            raise ValueError('Selección Majin ajena al slot: '+str(ident))
        result.append(ident if ident >= 0 else base)
    return dict(zip(ZONAS, result), ropa_activa=ids[3] >= 0)


def _curva(ctrl, steps=12):
    a, b, c, d = np.asarray(ctrl, float)
    return [(1-t)**3*a+3*(1-t)**2*t*b+3*(1-t)*t*t*c+t**3*d for t in np.linspace(0, 1, steps)]


def _acabado(m, ident, forma):
    base, slot, names = next(row for row in TIPOS if row[0] <= ident < row[0]+len(row[2]))
    out = m.finish(dict(id=ident, nombre=names[ident-base], slot=slot, forma=forma))
    out.update(kind='cac_majin_r16', colores=PALETA,
               rig={k: dict(pos=list(v[0]), padre=v[1]) for k, v in RIG.items()},
               licencia='original, made for this project', version=VERSION)
    keys = list(PALETA)
    out['palette'] = [''.join('%02x'%round(c*255) for c in PALETA[k][:3]) for k in keys]
    out['regions'] = [keys.index(out['regiones'][t[0]]) for t in out['tris']]
    out['UV'] = [((keys.index(r)%4+.5)/4, (keys.index(r)//4+.5)/4) for r in out['regiones']]
    # Un mismo borde y normal radial en cabeza y cuerpo evita un escalón de luz.
    for ids in out['costuras'].values():
        for i in ids:
            x, _, z = out['P'][i]
            radius = math.hypot(x, z)
            out['N'][i] = [x/radius, 0., z/radius]
    # Especificar bindings incluso para las superficies próximas a los anclajes:
    # el retarget no debe interpolar el cuello contra el origen de la cabeza.
    child = {s+a: s+b for s in ('L', 'R') for a, b in
             (('ARM1', 'ARM2'), ('ARM2', 'HAND'), ('LEG1', 'LEG2'), ('LEG2', 'FOOT1'))}
    binds = []
    for p, n in zip(out['P'], out['huesos']):
        b = child.get(n, n)
        a0, b0 = np.asarray(RIG[n][0]), np.asarray(RIG[b][0])
        t = float(np.clip(np.dot(np.asarray(p)-a0, b0-a0)/max(np.dot(b0-a0, b0-a0), 1e-9), 0, 1))
        binds.append((n, b, t))
    out['bindings'] = binds
    return out


def generar(ident, forma='f_maj_super', cuerpo_tipo=0):
    """Arte matemático nuevo. No abre juegos, archivos de modelo ni texturas."""
    from nativas_namek_majin import Malla
    from variantes_formas import formas_raza
    if forma not in formas_raza(4):
        raise ValueError('Forma Majin desconocida')
    m = Malla()
    skin = 'piel_majin'
    if 4000 <= ident < 4008:
        k = ident-4000
        # Las ocho trayectorias corresponden a las siluetas de DISENO_MAJ.
        if k == 0:
            paths = [_curva([(0, 6.30, 0), (.35, 7.30, -.15), (-.65, 7.25, -.65), (-.80, 6.85, -.90)])]
        elif k == 1:
            ts = np.linspace(0, 1.72*math.pi, 20)
            paths = [[(0, 6.30, 0)]+[(.34*math.sin(t), 6.75-.34*math.cos(t), -.16*t) for t in ts]]
        elif k == 2:
            paths = [_curva([(s*.17, 6.28, 0), (s*.24, 6.65, -.08), (s*.65, 6.70, -.25), (s*.78, 6.88, -.35)]) for s in (-1, 1)]
        elif k == 3:
            paths = [[(0, 6.30, 0), (-.05, 6.44, 0), (-.10, 6.55, .04)]]
        elif k == 4:
            paths = [[(s*.085*math.cos(t*5*math.pi), 6.30+t*.9,
                       -.08+s*.085*math.sin(t*5*math.pi)) for t in np.linspace(0, 1, 24)] for s in (-1, 1)]
        elif k == 5:
            paths = [[(0, 6.30, 0), (0, 6.55, -.08), (0, 6.76, -.10)]]
            paths += [_curva([(0, 6.72, -.10), (s*.25, 6.70, -.12), (s*.48, 6.88, -.08), (s*.50, 7.05, 0)]) for s in (-1, 1)]
        elif k == 6:
            paths = [_curva([(0, 6.30, -.12), (0, 6.65, -.85), (0, 7.25, -.30), (0, 7.40, .08)])]
        else:
            paths = [_curva([(s*.18, 6.28, -.08), (s*.40, 6.40, -.42), (s*.55, 6.0, -.68),
                             (s*.50, 5.60-.18*(s == 0), -.75)]) for s in (-1, 0, 1)]
        for path in paths:
            rr = [(max(.018, .115*(1-.90*t)), max(.012, (.05 if k == 6 else .115)*(1-.90*t)))
                  for t in np.linspace(0, 1, len(path))]
            m.tube(path, rr, 'HEAD', skin, sides=8)
    elif 4020 <= ident < 4024:
        k = ident-4020
        # Secciones independientes: mandíbula cuadrada, mejillas lunares o filo.
        profiles = (
            [(5.35, .12, .12), (5.48, .23, .24), (5.65, .36, .32), (5.92, .44, .38), (6.14, .38, .34), (6.32, .18, .16)],
            [(5.35, .12, .12), (5.44, .34, .29), (5.63, .43, .32), (6.04, .43, .33), (6.24, .35, .29), (6.32, .18, .17)],
            [(5.35, .12, .12), (5.45, .33, .29), (5.67, .52, .36), (5.86, .49, .39), (6.13, .42, .35), (6.32, .18, .16)],
            [(5.35, .12, .12), (5.44, .16, .30), (5.68, .41, .33), (5.85, .48, .34), (6.13, .35, .30), (6.32, .18, .16)],
        )
        rings = m.loft([(0, y, 0, rx, rz) for y, rx, rz in profiles[k]],
                       lambda t: 'NECK' if t == 0 else 'HEAD', skin, sides=16, caps=(False, True))
        m.costuras['cuello_cabeza'] = rings[0]
        for side in (-1, 1):
            eye = (.135, .055, .038) if k == 0 else ((.105, .045, .038) if k == 1 else ((.073, .085, .035) if k == 2 else (.13, .035, .036)))
            x = side*(.20 if k != 2 else .23)
            m.ellipsoid((x, 5.94, .34), eye, 'HEAD', 'ojos', rows=4, sides=8)
            m.ellipsoid((x, 5.94, .375), (.025, .045, .014), 'HEAD', 'tinta', rows=4, sides=6)
        mouth = [(-.14, 5.65, .317), (0, 5.62-(.055 if k == 2 else 0), .346), (.14, 5.65-(.03 if k == 3 else 0), .317)]
        m.tube(mouth, [.016]*3, 'HEAD', 'tinta', sides=6)
    elif 4040 <= ident < 4043:
        k = ident-4040
        wide, deep, arm = ((.57, .36, .19), (1.06, .76, .31), (.90, .48, .31))[k]
        rows = [(0, 2.78, 0, .52, .36), (0, 3.08, 0, .64 if k != 0 else .49, .42),
                (0, 3.65, .06 if k == 1 else 0, wide, deep),
                (0, 4.35, 0, .73 if k == 1 else wide, .45 if k == 1 else deep),
                (0, 4.68, 0, .65 if k == 0 else .83, .32),
                (0, 4.92, 0, .33, .24), (0, 5.35, 0, .12, .12)]
        rings = m.loft(rows, lambda t: 'WAIST' if t < .20 else ('STMC' if t < .5 else ('CHEST' if t < .95 else 'NECK')),
                       skin, sides=16, caps=(True, False))
        m.costuras['cuello_cuerpo'] = rings[-1]
        for s, side in ((1, 'L'), (-1, 'R')):
            armpts = [(s*x, 4.6, 0) for x in (.65, .95, 1.4, 1.85, 2.25, 2.65)]
            m.tube(armpts, [arm, arm*1.15, arm, arm*.85, arm*.8, .12],
                   lambda t: side+('ARM1' if t < .6 else 'ARM2'), skin, sides=10)
            m.ellipsoid((s*2.80, 4.59, .025), (.23, .12, .21), side+'HAND', skin, rows=4, sides=8)
            for finger in range(3):
                m.tube([(s*2.89, 4.60, (finger-1)*.12), (s*3.09, 4.58, (finger-1)*.13)],
                       [.064, .035], side+'HAND', skin, sides=6)
            m.tube([(s*2.75, 4.53, .14), (s*2.82, 4.40, .25)], [.08, .035], side+'HAND', skin, sides=6)
            legpts = [(s*.43, y, 0) for y in (2.9, 2.65, 2.05, 1.45, .9, .30)]
            radius = .24 if k == 0 else (.34 if k == 1 else .32)
            m.tube(legpts, [radius, radius*1.2, radius, radius*.80, radius*.82, .14],
                   lambda t: side+('LEG1' if t < .6 else 'LEG2'), skin, sides=10)
            m.ellipsoid((s*.43, .23, .19), (.21, .20, .42), side+'FOOT1', skin, rows=4, sides=10)
        # Cobertura básica de autor; la prenda elegida sigue siendo independiente.
        m.loft([(0, 2.91, 0, .55, .39), (0, 3.11, 0, .66 if k else .51, .44)],
               'WAIST', 'tela', sides=16)
    elif 4060 <= ident < 4064:
        k = ident-4060
        w, d = ((.57, .36), (1.06, .76), (.90, .48))[cuerpo_tipo]
        if k == 0:
            # Dos paneles cerrados, corte frontal diagonal y huecos de brazo.
            for s in (-1, 1):
                paths = [[(s*.22, 4.88, .27), (s*.63, 4.68, .38), (s*(w+.04), 4.12, d+.04), (s*.17, 3.82, d+.035)],
                         [(s*.25, 4.85, -.29), (s*.65, 4.62, -.35), (s*(w+.03), 4.12, -d-.035), (s*.18, 3.82, -d-.03)]]
                for p in paths:
                    m.tube(p, [(.18, .035)]*len(p), 'CHEST', 'tela', sides=4)
                    m.tube(p, [.025]*len(p), 'CHEST', 'ribete', sides=6)
        elif k == 1:
            # Cape remesh own grid, double-sided thin surface, no native cloth.
            for side in (-1, 1):
                rows = []
                for j in range(7):
                    t = j/6
                    ring = []
                    for i in range(5):
                        x = (-1+2*i/4)*(.43+.50*t)
                        y = 4.98-2.30*t + (.30 if i in (0, 4) else 0)*t
                        z = -.35-(d-.28)*math.sin(math.pi*t)-.42*t+side*.022
                        ring.append(m.v((x, y, z), 'CHEST', 'tela'))
                    if rows:
                        for i in range(4):
                            vs = (rows[-1][i], rows[-1][i+1], ring[i+1], ring[i])
                            m.face(*(vs if side == 1 else vs[::-1]))
                    rows.append(ring)
            m.tube([(-.43, 4.98, -.35), (0, 5.02, -.40), (.43, 4.98, -.35)], [.035]*3, 'CHEST', 'ribete', sides=6)
        elif k == 2:
            ww = (.52, .77, .72)[cuerpo_tipo]
            m.loft([(0, 3.10, 0, ww, .45), (0, 3.31, 0, max(ww, w)+.03, d+.045)], 'WAIST', 'ribete', sides=16)
            m.ellipsoid((.25, 3.20, d+.07), (.13, .13, .045), 'WAIST', 'tela', rows=4, sides=6)
        else:
            for s, side in ((1, 'L'), (-1, 'R')):
                r = .38 if cuerpo_tipo == 0 else .49
                m.loft([(s*.43, 1.00, 0, .19, .19), (s*.43, 1.25, 0, r*.85, r),
                        (s*.43, 1.85, 0, r*1.1, r*1.1), (s*.43, 2.6, 0, r, r),
                        (s*.43, 2.96, 0, .35, .38)],
                       lambda t: side+('LEG2' if t < .5 else 'LEG1'), 'tela', sides=12)
            m.loft([(0, 2.90, 0, .66 if cuerpo_tipo else .54, .45),
                    (0, 3.09, 0, .67 if cuerpo_tipo else .53, .45)], 'WAIST', 'ribete', sides=16)
    else:
        raise ValueError('ID Majin desconocido: '+str(ident))
    result = _acabado(m, ident, forma)
    result['cuerpo_tipo'] = cuerpo_tipo
    return result


def recurso(ident, forma, juego=None, cuerpo_tipo=0):
    root = (Path(juego) if juego else ROOT)/'res/heroe/partes_majin'
    manifest = json.loads((root/'catalogo.json').read_text(encoding='utf-8'))
    path = '%s/%d%s.json' % (forma, ident, '_c%d'%cuerpo_tipo if ident >= 4060 else '')
    record = next((r for r in manifest['files'] if r['file'] == path), None)
    if record is None:
        raise ValueError('Pieza Majin no exportada para la forma: '+path)
    data = (root/path).read_bytes()
    if hashlib.sha256(data).hexdigest() != record['sha256']:
        raise ValueError('Pieza Majin distinta de su manifiesto: '+path)
    mesh = json.loads(data)
    if mesh['kind'] != 'cac_majin_r16' or mesh['id'] != ident or mesh['forma'] != forma:
        raise ValueError('Identidad de pieza Majin incorrecta')
    return mesh


def aplicar_b3(amb, ficha, juego=None):
    selected = seleccion_b3(ficha)
    if not selected:
        return bytes(amb), {}
    import model_render as mr
    import partes_nativas as pn
    import injertar_ropa as ir
    import texture_b3 as tb
    from awg_vertex_buffer import AwgVertexBuffer
    from b3_gateway import DrawTable
    model = mr.Model(bytes(amb))
    forma = ficha.get('_majin_forma', 'f_maj_super')
    pieces = [recurso(selected[z], forma, juego, selected['guantes']-4040) for z in ZONAS
              if z != 'botas' or selected['ropa_activa']]
    # Un atlas de región único para las cuatro piezas, manteniendo UV propias.
    atlas_parts = [pn._atlas(p, model, ficha) for p in pieces]
    for part in atlas_parts:
        part['rgba'][16:32, :16, 3] = 255  # Tinta, quinta región de nuestra paleta.
    if any(not np.array_equal(p['rgba'], atlas_parts[0]['rgba']) for p in atlas_parts):
        raise ValueError('Atlas de colores Majin incompatibles')
    offsets = np.cumsum([0]+[len(p['P']) for p in atlas_parts])
    P, N, B, UV = (np.concatenate([p[key] for p in atlas_parts]) for key in ('P', 'N', 'bones', 'UV'))
    tris = np.concatenate([p['tris']+offsets[i] for i, p in enumerate(atlas_parts)])
    hosts = [i for i, d in enumerate(model.table.draws) if d['prim'] == 5 and
             not model.mats[d['material']]['env'] and model.mats[d['material']]['tex'] < len(model.tex)]
    if not hosts:
        raise ValueError('Sin anfitrión strip Majin')
    host = max(hosts, key=lambda i: model.table.draws[i]['B'][1])
    mat = model.table.draws[host]['material']
    tex = model.mats[mat]['tex']
    # Pillow redimensiona RGBA premultiplicando por alfa: aquí alfa NO es
    # transparencia, es la banda toon. Un texel con banda cero perdería su RGB
    # y toda la piel quedaría negra. Entregar el tamaño final por vecino próximo
    # conserva por separado los cuatro canales y los colores planos propios.
    _, textures = tb.parse_azt(amb)
    size = next(t for t in textures if t['idx'] == tex)
    atlas = imagen_tamano(atlas_parts[0]['rgba'], size['w'], size['h'])
    job = dict(host=host, quitar=np.ones(len(model.tris), bool), P=P, N=N, bones=B,
               UV=UV, tris=tris, rgba=atlas, tex_destino=tex, zona='majin_original')
    data, report = ir.injertar_ropa_multi(bytes(amb), model, [job])
    avb = AwgVertexBuffer._parse(data)
    dt = DrawTable(avb)
    _, textures = tb.parse_azt(data)
    ramp = next((t['idx'] for t in textures if t['idx'] != tex and t['size']-128 == t['w']*t['h']), None)
    if ramp is None:
        raise ValueError('Sin textura independiente para rampa Majin')
    gray = np.zeros((64, 64, 4), np.uint8)
    gray[..., 3] = 255
    gray[:, :20, :3], gray[:, 20:39, :3] = 72, 34
    size = next(t for t in textures if t['idx'] == ramp)
    ir._escribir_textura(avb.data, ramp, imagen_tamano(gray, size['w'], size['h']))
    flags = dt.materials[mat]['flags']
    dt.set_material(mat, tex=tex, ramp=ramp, flags=(flags[0] & ~0x40, flags[1]))
    data = bytes(avb.data)
    for awg in range(1, model.avb.n_awg):
        aux = AwgVertexBuffer._parse(data, awg=awg)
        table = DrawTable(aux)
        for d in table.draws:
            table.set_range(d, *d['A'], d['B'][0], 0)
        data = bytes(aux.data)
    report.update(ids=[selected[z] for z in ZONAS], forma=forma, original=True,
                  piezas=[p['nombre'] for p in pieces],
                  procedencia='original, made for this project',
                  aviso='Expresiones y poses de dedos aún no conectadas a AWG auxiliares')
    return data, report


def imagen_tamano(rgba, ancho, alto):
    """Escala un atlas de bandas sin interpretar alfa como transparencia."""
    rgba = np.asarray(rgba, np.uint8)
    ys = np.minimum(np.arange(alto)*rgba.shape[0]//alto, rgba.shape[0]-1)
    xs = np.minimum(np.arange(ancho)*rgba.shape[1]//ancho, rgba.shape[1]-1)
    return rgba[ys[:, None], xs[None, :]].copy()


def modelo_forma(cid, native_form):
    """Los ID de absorción se resuelven en la tabla form_ids del intérprete."""
    import roster_build as rb
    source = rb.DB.get('form_ids', {}).get(str(cid)) or rb.IDS.get(cid)
    if source is None or not 0 <= native_form < len(source['models']):
        raise ValueError('Modelo de forma Majin sin referencia: %s/%s' % (cid, native_form))
    return source['models'][native_form]


def formas_b3(afs, fid, ficha, juego, resumen, construir):
    if not seleccion_b3(ficha):
        return None
    import roster_build as rb
    # Los modelos de absorción de Super Buu son referencias explícitas del
    # intérprete, NO el campo forms (vale 1). No inventar una cadena Fat->Kid.
    if int(ficha.get('donante', ficha['cuerpo'])) != 35 or int(ficha['cuerpo']) != 35:
        return []
    from variantes_formas import construir_formas
    refs = rb.IDS[35]['form_models']
    enlaces = []
    for index, form in ((1, 'f_maj_absorcion'), (2, 'f_maj_absorbida')):
        cid, native_form = refs[index]
        target_fid = modelo_forma(cid, native_form)
        enlaces.append((form, index, target_fid))
    def build(raw, actual, root, afs):
        # construir_formas conserva los cuatro IDs; el token elige el recurso.
        return construir(raw, actual, root, afs)
    # Los enlaces se procesan uno a uno para declarar el token de arte propio.
    out = []
    for form, index, target in enlaces:
        actual = dict(ficha, _majin_forma=form, pelo=0, cara=0)
        out.extend(construir_formas(afs, actual, juego, resumen, build, 4,
                                   [(form, index, target)], {}))
    return out
