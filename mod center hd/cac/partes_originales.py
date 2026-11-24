#!/usr/bin/env python3
"""Variantes originales por deformacion local del B3 del jugador.

Solo viajan parametros. El #AMB, las texturas y los .blend derivados se generan
localmente. Se conservan TODOS los AWG, indices, slots, pesos y materiales.
Las coordenadas se deforman en bind y vuelven al marco original de cada hueso.
"""
import json
import re
import struct
import hashlib
from pathlib import Path

import numpy as np

VERSION = 1
ZONAS = ('torso', 'piernas', 'guantes', 'botas')
ROTULOS = ('Cabeza original', 'Mandibula original', 'Placas originales', 'Cola original')
# Estos IDs no son entradas AFS. Se guardan en los cuatro selectores ya persistentes.
CATALOGO = (
    (3000, 0, 'Cresta dorsal', 'cresta'),
    (3001, 0, 'Cupula alta', 'cupula'),
    (3002, 0, 'Doble quilla', 'quilla'),
    (3003, 0, 'Corona lateral', 'corona'),
    (3004, 0, 'Casco afilado', 'afilado'),
    (3005, 0, 'Casco ancho', 'ancho'),
    (3020, 1, 'Mandibula ancha', 'mandibula_ancha'),
    (3021, 1, 'Menton proyectado', 'menton'),
    (3022, 1, 'Rostro estrecho', 'rostro_estrecho'),
    (3040, 2, 'Placa pectoral', 'pectoral'),
    (3041, 2, 'Placas laterales', 'laterales'),
    (3060, 3, 'Cola acorazada', 'acorazada'),
    (3061, 3, 'Cola de quilla', 'cola_quilla'),
)
POR_ID = {i: (zona, nombre, receta) for i, zona, nombre, receta in CATALOGO}
CUERPOS = (27, 38)


def catalogo():
    out = [dict(tipo='original_' + ZONAS[z], id=i, nombre=n,
                idx_afs=-1, archivo='original:' + receta) for i, z, n, receta in CATALOGO]
    provider = _proveedor()
    if provider is not None and hasattr(provider, 'catalogo_b3'):
        out.extend(provider.catalogo_b3())
    import arcosiano_original
    out.extend(arcosiano_original.catalogo())
    # Registro LOCAL: nunca se distribuyen sus bins ni geometria del jugador.
    for p in _locales().get('piezas', []):
        out.append(dict(tipo='original_torso', id=p['id'], nombre=p['nombre'], idx_afs=-1,
                        archivo='local:%d:%s' % (p['cuerpo'], ','.join(map(str, p.get('trajes', [0]))))))
    return out


def _locales(juego=None):
    root = Path(juego) if juego else Path(__file__).resolve().parents[2]
    path = root / 'user_data' / 'dbz3' / 'partes_blender' / 'registro.json'
    return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {}


def huella_geometria(amb):
    """Identidad de ventanas, indices y skin; independiente de los tintes AZT."""
    import model_render as mr
    model = mr.Model(bytes(amb))
    h = hashlib.sha256()
    for awg in range(model.avb.n_awg):
        m = model if awg == 0 else mr.Model(bytes(amb), awg=awg)
        h.update(bytes(amb[m.avb.vb0:m.avb.vb0 + len(m.pos) * 44]))
        indices = m.avb.indices()
        h.update(struct.pack('>%dH' % len(indices), *indices))
    h.update(repr((model.labels, np.asarray(model.worlds()).tolist())).encode('utf-8'))
    return h.hexdigest()


def _aplicar_local(amb, ficha, juego):
    ident = int(ficha.get('b3_torso', -1))
    if not 3080 <= ident < 3100:
        return None
    reg = _locales(juego)
    item = next((p for p in reg.get('piezas', []) if p['id'] == ident and p['cuerpo'] == int(ficha['cuerpo'])), None)
    if item is None:
        raise ValueError('Pieza Blender local no instalada para este cuerpo')
    key = huella_geometria(amb)
    form = next((f for f in item['modelos'] if f['geometria'] == key), None)
    if form is None:
        raise ValueError('Pieza Blender sin modelo para esta forma/traje; exporta todas sus formas')
    root = Path(juego) if juego else Path(__file__).resolve().parents[2]
    binary = (root / 'user_data' / 'dbz3' / 'partes_blender' / form['archivo']).read_bytes()
    if hashlib.sha256(binary).hexdigest() != form['sha256']:
        raise ValueError('El bin local Blender ha cambiado sin volver a registrarse')
    import model_render as mr
    base, edited = mr.Model(bytes(amb)), mr.Model(binary)
    if len(binary) != len(amb) or edited.avb.n_awg != base.avb.n_awg:
        raise ValueError('Bin Blender de otro modelo')
    out = bytearray(amb)
    for awg in range(base.avb.n_awg):
        m = base if awg == 0 else mr.Model(bytes(amb), awg=awg)
        n = edited if awg == 0 else mr.Model(binary, awg=awg)
        if m.avb.vb0 != n.avb.vb0 or len(m.pos) != len(n.pos):
            raise ValueError('Ventanas Blender incompatibles')
        for i in range(len(m.pos)):
            at = m.avb.vb0 + 44 * i
            out[at:at+12] = binary[at:at+12]
            out[at+20:at+32] = binary[at+20:at+32]
    return bytes(out), dict(piezas=[item['nombre']], blender_local=True, fid=form['fid'],
                            skin='pesos y slots nativos conservados', materiales='toon y borde nativos conservados')


def _proveedor():
    import importlib.util
    if importlib.util.find_spec('partes_namek_majin') is None:
        return None
    import partes_namek_majin
    return partes_namek_majin


def seleccion(ficha):
    """Valida por zona y cuerpo: un ID ajeno nunca se convierte en indice AFS."""
    out = {}
    for z in range(4):   # CAC-11: orig1..4 (cabeza, mandibula, placa, cola) viajan en su propio campo y valen en cualquier cuerpo
        i = int(ficha.get('orig%d' % (z + 1), -1))
        if i in POR_ID and POR_ID[i][0] == z:
            out[z] = i
    if out:
        return out
    if int(ficha.get('cuerpo', -1)) not in CUERPOS:
        return {}
    for z, zona in enumerate(ZONAS):
        i = int(ficha.get('b3_' + zona, -1))
        if i in POR_ID and POR_ID[i][0] == z:
            out[z] = i
    return out


def _smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0., 1.)
    return t * t * (3. - 2. * t)


def _canon(label):
    return re.sub(r'_L\d\d_', '_L00_', label)


def _contexto(base):
    W = np.asarray(base.worlds(), float)
    head = next(i for i, n in enumerate(base.labels) if n.endswith('_HEAD'))
    chest = next(i for i, n in enumerate(base.labels) if n.endswith('_CHEST'))
    P, _ = base.posed()
    hv = P[base.vbone == head]
    unit = max(float(hv[:, 1].max() - W[head, 1, 3]), .5)
    tail = [i for i, n in enumerate(base.labels) if 'T_TAIL' in n]
    tv = P[np.isin(base.vbone, tail)] if tail else None   # CAC-11: la cola es opcional (Vegeta no la tiene)
    return dict(head=W[head, :3, 3], chest=W[chest, :3, 3], unit=unit,
                tail_y=W[tail[0], 1, 3] if tail else 0., tail_start=float(tv[:, 2].max()) if tail else 0.,
                tail_end=float(tv[:, 2].min()) if tail else 0., sin_cola=not tail, worlds=W,
                labels={_canon(n): i for i, n in enumerate(base.labels)})


def _deformar(P, names, sel, ctx):
    Q = P.copy()
    u = ctx['unit']
    H = ctx['head']
    x = P[:, 0] - H[0]
    y = (P[:, 1] - H[1]) / u
    skull = np.asarray([n.endswith('_HEAD') or '_M_' in n or 'FACE' in n or 'HAIR' in n
                        for n in names], bool)
    cap = _smooth(.32, .92, y) * skull
    head = POR_ID[sel[0]][2] if 0 in sel else ''
    mid = np.exp(-(x / (.22 * u)) ** 2)
    if head == 'cresta':
        Q[:, 1] += .38 * u * cap * mid
        Q[:, 2] -= .16 * u * cap * mid
    elif head == 'cupula':
        Q[:, 1] += .26 * u * cap
        Q[:, 0] += .10 * x * cap
    elif head == 'quilla':
        ridge = np.exp(-((np.abs(x) / u - .24) / .12) ** 2)
        Q[:, 1] += .36 * u * cap * ridge
    elif head == 'corona':
        side = _smooth(.16, .43, np.abs(x) / u)
        Q[:, 0] += .32 * u * np.sign(x) * cap * side
        Q[:, 1] += .28 * u * cap * side
    elif head == 'afilado':
        Q[:, 0] -= .24 * x * cap
        Q[:, 2] -= .32 * u * cap
        Q[:, 1] += .12 * u * cap * mid
    elif head == 'ancho':
        Q[:, 0] += .28 * x * cap
        Q[:, 1] -= .10 * u * cap
    # Misma funcion espacial sobre dientes y TODAS las expresiones de la cara.
    jaw = POR_ID[sel[1]][2] if 1 in sel else ''
    lower = (1. - _smooth(.22, .55, y)) * _smooth(-.28, -.05, y) * skull
    if jaw == 'mandibula_ancha':
        Q[:, 0] += .22 * x * lower
    elif jaw == 'menton':
        Q[:, 2] += .13 * u * lower * np.exp(-(x / (.35 * u)) ** 2)
        Q[:, 1] -= .06 * u * lower
    elif jaw == 'rostro_estrecho':
        Q[:, 0] -= .18 * x * lower
    plate = POR_ID[sel[2]][2] if 2 in sel else ''
    torso = np.asarray([n.endswith(('_CHEST', '_STMC', '_LCHN', '_RCHN', '_LARMROT', '_RARMROT'))
                        for n in names], bool)
    C = ctx['chest']
    sc = bool(ctx.get('sin_cola'))   # CAC-11: Vegeta y demas: el pecho esta mas arriba y mas ancho que en Freezer
    band = np.exp(-((P[:, 1] - C[1] - (.95 if sc else .18) * u) / ((.75 if sc else .42) * u)) ** 4) * torso
    front = _smooth(.1, .6, (P[:, 2] - C[2]) / u) if sc else _smooth(-.04, .24, (P[:, 2] - C[2]) / u)
    if plate == 'pectoral':
        k = 4.5 if ctx.get('sin_cola') else 1.   # CAC-11: en cuerpos sin armadura Freezer (Vegeta...) el abultamiento a escala 1 no se ve
        Q[:, 2] += .14 * k * u * band * front * np.exp(-(P[:, 0] / ((1.0 if sc else .62) * u)) ** 4)
    elif plate == 'laterales':
        side = np.exp(-((np.abs(P[:, 0]) / u - .55) / .24) ** 2)
        Q[:, 0] += np.sign(P[:, 0]) * .14 * u * band * side
        Q[:, 2] += .08 * u * band * side * front
    tail = POR_ID[sel[3]][2] if 3 in sel and not ctx.get('sin_cola') else ''   # sin cola no hay nada que deformar
    chain = np.asarray(['T_TAIL' in n for n in names], bool)
    t = np.clip((ctx['tail_start'] - P[:, 2]) / max(ctx['tail_start'] - ctx['tail_end'], 1e-6), 0, 1)
    gate = _smooth(0, .12, t) * chain
    if tail == 'acorazada':
        radial = .38 * gate * (1. - .65 * t)
        Q[:, 0] += P[:, 0] * radial
        Q[:, 1] += (P[:, 1] - ctx['tail_y']) * radial
    elif tail == 'cola_quilla':
        fin = gate * np.sin(np.pi * t) ** 2
        Q[:, 0] -= .25 * P[:, 0] * fin
        Q[:, 1] += (P[:, 1] - ctx['tail_y']) * 1.25 * fin
    return Q


def _marcos(model, base, ctx, awg):
    if awg == 0:
        return ctx['worlds'][model.vbone]
    # Las variantes L01/L02 de la cara son locales al L00 del cuerpo, no al BODY.
    label = bytes(model.data[model.avb.awg0 + 0x40:model.avb.awg0 + 0x60]).split(b'\0')[0].decode('latin1')
    root = ctx['labels'].get(_canon(label))
    if root is None:
        raise ValueError('AWG auxiliar sin hueso equivalente: ' + label)
    return np.repeat(ctx['worlds'][root][None], len(model.pos), axis=0)


def aplicar(amb, ficha, juego=None):
    """Misma entrada para previa y combate; no toca topologia, texturas ni pesos."""
    import arcosiano_original
    if arcosiano_original.seleccion(ficha):
        import partes_nativas
        return partes_nativas.aplicar_b3(amb, ficha, juego)
    local = _aplicar_local(amb, ficha, juego)
    if local is not None:
        return local
    sel = seleccion(ficha)
    if not sel:
        provider = _proveedor()
        if provider is not None and hasattr(provider, 'aplicar_b3'):
            return provider.aplicar_b3(amb, ficha)
        return bytes(amb), {}
    import model_render as mr
    base = mr.Model(bytes(amb))
    ctx = _contexto(base)
    out = bytearray(amb)
    counts = []
    for awg in range(base.avb.n_awg):
        m = base if awg == 0 else mr.Model(bytes(amb), awg=awg)
        W = _marcos(m, base, ctx, awg)
        P = np.einsum('nij,nj->ni', W[:, :3, :3], m.pos) + W[:, :3, 3]
        N = np.einsum('nij,nj->ni', W[:, :3, :3], m.nrm)
        if awg == 0:
            names = [m.labels[b] for b in m.vbone]
        else:
            label = bytes(m.data[m.avb.awg0 + 0x40:m.avb.awg0 + 0x60]).split(b'\0')[0].decode('latin1')
            names = [label] * len(P)
        Q = _deformar(P, names, sel, ctx)
        changed = np.linalg.norm(Q - P, axis=1) > 1e-8
        if changed.any():
            # Inversa-transpuesta del Jacobiano: mantiene la luz suave, tambien en matcaps.
            eps = ctx['unit'] * 1e-4
            J = np.empty((len(P), 3, 3))
            for axis in range(3):
                d = np.zeros_like(P)
                d[:, axis] = eps
                J[:, :, axis] = (_deformar(P + d, names, sel, ctx) - _deformar(P - d, names, sel, ctx)) / (2 * eps)
            det = np.linalg.det(J[changed])
            if (det <= .05).any():
                raise ValueError('Deformacion plegada o invertida; no se publica el modelo')
            NN = np.linalg.solve(J[changed].transpose(0, 2, 1), N[changed, :, None])[..., 0]
            NN /= np.maximum(np.linalg.norm(NN, axis=1), 1e-9)[:, None]
            local = np.linalg.solve(W[changed, :3, :3], (Q[changed] - W[changed, :3, 3])[..., None])[..., 0]
            normal = np.linalg.solve(W[changed, :3, :3], NN[..., None])[..., 0]
            if not np.isfinite(local).all() or not np.isfinite(normal).all():
                raise ValueError('Coordenadas no finitas')
            for i, p, n in zip(np.flatnonzero(changed), local, normal):
                off = m.avb.vb0 + int(i) * 44
                struct.pack_into('>3f', out, off, *p)
                struct.pack_into('>3f', out, off + 20, *n)
        counts.append(int(changed.sum()))
    return bytes(out), dict(piezas=[POR_ID[i][1] for i in sel.values()], ventanas_por_awg=counts,
                           skin='pesos y slots nativos conservados', materiales='toon y borde nativos conservados')


def formas(afs, fid, ficha, juego, resumen, construir):
    """Modelos nativos por forma, con la misma seleccion original en todos ellos."""
    import arcosiano_original
    if arcosiano_original.seleccion(ficha):
        import partes_nativas
        return partes_nativas.formas_b3(afs, fid, ficha, juego, resumen, construir)
    local = 3080 <= int(ficha.get('b3_torso', -1)) < 3100 and int(ficha.get('cuerpo', -1)) in CUERPOS
    if int(ficha.get('cuerpo', -1)) not in CUERPOS and not local:
        provider = _proveedor()
        if provider is not None and hasattr(provider, 'formas_b3'):
            return provider.formas_b3(afs, fid, ficha, juego, resumen, construir)
        return None  # no intercepta las otras razas
    import roster_build as rb
    body = int(ficha['cuerpo'])
    donor = rb.IDS.get(int(ficha.get('donante', body))) or {}
    source = rb.IDS[body]
    if donor.get('id') != body:
        return []  # nunca asociar un esqueleto Freezer al moveset de Goku
    models = source['models']
    per = source['models_per_costume']
    costume = min(max(int(ficha.get('traje', 0)), 0), source['costumes'] - 1)
    out = []
    for form in range(1, source['forms']):
        model_fid = models[costume * per + form]
        data, r = construir(afs.entry(model_fid), dict(ficha, pelo=0, cara=0), juego, afs)
        out.append((data, form))
        resumen['original_forma%d' % form] = dict(fid=model_fid, piezas=r.get('piezas_b3', {}).get('originales', {}))
    return out


def ficha_de_receta(receta):
    """Contrato Blender TOML: cuerpo y cuatro IDs, sin depender de XV2."""
    ficha = dict(cuerpo=int(receta.get('cuerpo', 27)))
    for zona in ZONAS:
        ficha['b3_' + zona] = int(receta.get('b3_' + zona, -1))
    if not seleccion(ficha):
        raise ValueError('Receta sin piezas originales validas para Freezer/Cooler')
    return ficha
