"""Combinaciones de geometria B3 leida del AFS local del jugador.

Los IDs 4000..4047 y 4080..4087 son recetas, nunca indices AFS. Caben en el limite 4095
de la ficha existente. No hay geometria ni
texturas serializadas en este modulo. Se conserva el esqueleto anfitrion,
incluidas sus tablas de variantes; cada material y DDS procede del donante.
"""
from __future__ import annotations

from collections import defaultdict
from pathlib import Path
import os
import sys
import re
import struct

import numpy as np

ZONAS = ('torso', 'piernas', 'guantes', 'botas')
FAMILIAS = {11: 'namek', 27: 'freezer', 38: 'freezer',
            34: 'majin', 35: 'majin', 36: 'majin'}
CABEZAS = {'freezer': (181, 182, 183, 184, 185, 186, 166, 167),
           'namek': (350, 351, 352, 353, 350, 351, 352, 353),
           'majin': (128, 133, 141, 116, 107, 104, 129, 134)}
CUERPOS = {'freezer': (181, 184, 185, 167), 'namek': (350, 351, 352, 353),
           'majin': (128, 133, 141, 104)}
INICIOS = {'freezer': (4000,) * 4, 'namek': (4080,) * 4, 'majin': (4080,) * 4}
# Conjuntos completos: nunca se cruzan cabeza, cuerpo y cola en la lista normal.
# El catalogo antiguo sigue disponible expresamente para investigacion.
CURADAS = {'freezer': ((0, 0, 0, 0), (3, 0, 1, 1)),
           'namek': ((0, 0, 0, 0), (3, 0, 3, 0)),
           'majin': ((0, 0, 0, 0), (1, 0, 1, 0))}


def experimental():
    return os.environ.get('DBZ3_CAC_RAZAS_EXPERIMENTAL') == '1'


def explicita(ficha):
    """CAC-02: el jugador eligio piezas de raza (cabeza/cuerpo/cola B3) en el creador. Sin ellas, el heroe es el cuerpo
    y el TRAJE elegidos tal cual (antes: siempre el conjunto curado 0 = Freezer 181 / Buu gordo 128 / Piccolo 350, y
    Cooler, Super Buu, Buu nino o los trajes 2-4 de Piccolo salian como otro personaje)."""
    raza = FAMILIAS.get(int(ficha.get('cuerpo', -1)))
    if raza is None:
        return False
    limites = (48, 24, 4, 4) if raza == 'freezer' else (8, 4, 4, 4)
    ids = [int(ficha.get('b3_' + z, -1)) for z in ZONAS]
    return any(b <= i < b + n for i, b, n in zip(ids, INICIOS[raza], limites))


def seleccion(ficha):
    raza = FAMILIAS.get(int(ficha.get('cuerpo', -1)))
    if raza is None:
        return None
    ids = [int(ficha.get('b3_' + z, -1)) for z in ZONAS]
    inicios = INICIOS[raza]
    limites = (48, 24, 4, 4) if raza == 'freezer' else (8, 4, 4, 4)
    if not any(b <= i < b + n for i, b, n in zip(ids, inicios, limites)):
        return None if experimental() else (raza, list(CURADAS[raza][0]))
    if experimental() and any(i >= 0 and not b <= i < b + n for i, b, n in zip(ids, inicios, limites)):
        raise ValueError('Receta B3 mezclada con piezas de otro proveedor')
    valores = [i - b if i >= b else 0 for i, b in zip(ids, inicios)]
    if not experimental():
        # Una ficha antigua conserva su fichero; su vista utiliza un conjunto
        # completo, sin volver a mostrar una mezcla rechazada.
        valores = list(next((p for p in CURADAS[raza] if p[0] == valores[0]), CURADAS[raza][0]))
    return raza, valores


def ficha_prueba(cuerpo, cabeza=0, cuerpo_tipo=0):
    raza = FAMILIAS[cuerpo]
    bases = INICIOS[raza]
    valores = (cabeza, 0, cuerpo_tipo, 0)
    return dict(cuerpo=cuerpo, traje=0, pelo=0, cara=0, donante=cuerpo,
                **{'b3_' + z: b + v for z, b, v in zip(ZONAS, bases, valores)})


def _clave(label):
    from b3_gateway import bone_suffix
    return re.sub(r'^L\d\d_(?:S\d\d_)?', 'L00_', bone_suffix(label))


def _cabeza(clave):
    return (clave == 'HEAD' or clave.startswith('M_') or 'FACE' in clave
            or 'HAIR' in clave or 'CHEEK' in clave or clave.startswith('HOLE'))


def _region(model):
    claves = [_clave(model.labels[b]) for b in model.vbone]
    # Mayoritaria por cara: una union no puede pertenecer a dos piezas.
    regiones = np.array(['cabeza' if _cabeza(c) else 'cola' if 'T_TAIL' in c
                         else 'cuerpo' for c in claves])
    return np.array([max(set(r), key=lambda x: (int(np.count_nonzero(r == x)),
                                               x == 'cabeza'))
                     for r in regiones[model.tris]])


def _retarget(donor, host, region):
    from b3_gateway import suffix_bonemap
    nombres = suffix_bonemap(donor.labels, donor.parent, host.labels)
    by_name = {n: i for i, n in enumerate(host.labels)}
    mapa = np.array([by_name[nombres[n]] for n in donor.labels], int)
    P, N = donor.posed()
    dw, hw = np.asarray(donor.worlds()), np.asarray(host.worlds())
    B = mapa[donor.vbone]
    if region == 'cabeza':
        dh, hh = donor.bone('HEAD'), host.bone('HEAD')
        T = hw[hh] @ np.linalg.inv(dw[dh])
        P = P @ T[:3, :3].T + T[:3, 3]
        N = N @ T[:3, :3].T
    else:
        # Traslacion/rotacion del marco, no vecino mas cercano ni indices crudos.
        T = hw[B] @ np.linalg.inv(dw[donor.vbone])
        P = np.einsum('nij,nj->ni', T[:, :3, :3], P) + T[:, :3, 3]
        N = np.einsum('nij,nj->ni', T[:, :3, :3], N)
    N /= np.maximum(np.linalg.norm(N, axis=1), 1e-12)[:, None]
    return P, N, B


class _Texturas:
    def __init__(self, base, ficha=None, raza=None):
        from awg_vertex_buffer import be32
        self.be32 = be32
        z = base.find(b'#AZT')
        if z < 0:
            raise ValueError('Modelo sin AZT')
        n, tab = be32(base, z + 0x10), be32(base, z + 0x14)
        self.z = bytearray(base[z:])
        self.offsets = [be32(base, z + tab + 4 * i) for i in range(n)]
        self.cache = {}
        self.ficha, self.raza = ficha or {}, raza
        self.vram = max(be32(base, z + o + 0x24) +
                        (be32(base, z + o + 0x18) + 31) // 32 for o in self.offsets)

    def traer(self, modelo, indice, base=False, gema=False):
        if indice == 0xFFFFFFFF:
            return indice
        key = (modelo.data, indice, base, gema)
        if key in self.cache:
            return self.cache[key]
        b, read = modelo.data, self.be32
        z = b.find(b'#AZT')
        n, tab = read(b, z + 0x10), read(b, z + 0x14)
        if not 0 <= indice < n:
            raise ValueError('Material donante con textura fuera de rango')
        o = z + read(b, z + tab + 4 * indice)
        ent = bytearray(b[o:o + 0x30])
        at, size = read(ent, 0x14), read(ent, 0x18)
        dds = b[z + at:z + at + size]
        if len(dds) != size or not dds.startswith(b'DDS '):
            raise ValueError('DDS donante incompleto')
        if base:
            dds = self._color(dds, gema)
        self.z += bytes((-len(self.z)) % 128)
        at = len(self.z)
        self.z += dds
        nuevo = len(self.offsets)
        struct.pack_into('>I', ent, 0, nuevo)
        struct.pack_into('>II', ent, 0x14, at, size)
        struct.pack_into('>I', ent, 0x24, self.vram)
        self.vram += (size + 31) // 32
        self.z += bytes((-len(self.z)) % 16)
        self.offsets.append(len(self.z))
        self.z += ent
        self.cache[key] = nuevo
        return nuevo

    def _color(self, dds, gema):
        """Modula el dibujo original; nunca sustituye la textura por color plano."""
        import toon_regiones as toon
        import vista_heroe as vh
        colores = toon.validar_colores(self.ficha.get(toon.CAMPO, {}))
        partes = self.ficha.get('partes', [])
        hsv = {r: partes[i] for i, r in enumerate(toon.REGIONES) if i < len(partes)
               and tuple(partes[i]) != (0, 0, 0)}
        if tuple(self.ficha.get('piel', (0, 0, 0))) != (0, 0, 0):
            hsv['piel'] = self.ficha['piel']
        if not colores and not hsv:
            return dds
        import io
        from PIL import Image
        import texture_b3 as tb
        rgba = np.asarray(Image.open(io.BytesIO(dds)).convert('RGBA'), np.float32) / 255
        rgb = rgba[..., :3]
        red, green, blue = rgb[..., 0], rgb[..., 1], rgb[..., 2]
        region = np.full(rgb.shape[:2], 'ropa', dtype='<U6')
        if gema:
            region[:] = 'gemas'
        elif self.raza == 'namek':
            region[(green > red * 1.08) & (green > blue * 1.05)] = 'piel'
            region[(red > green * 1.18) & (red > blue * 1.05)] = 'placas'
        elif self.raza == 'majin':
            region[(red > green * 1.12) & (blue > green * 1.05)] = 'piel'
        else:
            region[np.max(rgb, axis=2) - np.min(rgb, axis=2) < .18] = 'placas'
            region[(red > green * 1.15) & (blue > green * 1.15)] = 'piel'
        out = rgba.copy()
        for r in toon.REGIONES:
            mask = region == r
            if r in colores:
                target = np.asarray(colores[r], float) / 255
                lum = rgb @ np.array([.2126, .7152, .0722])
                ref = max(float(np.percentile(lum[mask], 85)), .1) if mask.any() else 1
                out[mask, :3] = np.clip(lum[mask, None] / ref * target, 0, 1)
            elif r in hsv:
                out[mask] = vh.tenir(rgba, tuple(hsv[r]))[mask]
        out[..., 3] = rgba[..., 3]  # alfa = banda toon, no transparencia
        height, width = rgba.shape[:2]
        if len(dds) != 128 + width * height or dds[84:88] != b'DXT3':
            raise ValueError('Recoloreado requiere DDS DXT3 nativo')
        import cac17_materiales as materiales
        antes_u8 = (np.clip(rgba, 0, 1) * 255 + .5).astype(np.uint8)
        desp_u8 = (np.clip(out, 0, 1) * 255 + .5).astype(np.uint8)
        desp_u8[..., 3] = antes_u8[..., 3]
        return dds[:128] + materiales.reempacar(dds[128:], antes_u8, desp_u8)

    def terminar(self):
        self.z += bytes((-len(self.z)) % 16)
        at = len(self.z)
        self.z += struct.pack('>%dI' % len(self.offsets), *self.offsets)
        struct.pack_into('>II', self.z, 0x10, len(self.offsets), at)
        return bytes(self.z)


def _grupos(modelo, host, mascara, region, texturas):
    import injertar_pelo as ip
    P, N, B = _retarget(modelo, host, region)
    td = np.array([d for d, _ in ip.triangulos_por_draw(modelo)])
    grupos = []
    for di, d in enumerate(modelo.table.draws):
        tris = modelo.tris[mascara & (td == di)]
        if not len(tris):
            continue
        mi = d['material']
        mat = bytearray(modelo.data[modelo.table.mat_base + mi * 0x50:
                                   modelo.table.mat_base + (mi + 1) * 0x50])
        info = modelo.mats[mi]
        struct.pack_into('>II', mat, 0x30, texturas.traer(modelo, info['tex'], True, info['env']),
                         texturas.traer(modelo, info['shader']))
        rigido = 0
        if d['prim'] == 4 and d['group_bone']:
            clave = _clave(modelo.labels[d['group_bone']])
            destino = next((i for i, label in enumerate(host.labels) if _clave(label) == clave), None)
            if destino in {g['group_bone'] for g in host.table.draws}:
                rigido = destino
        grupos.append(dict(P=P, N=N, B=B, UV=modelo.uv, tris=tris, material=bytes(mat),
                           rigido=rigido, region=region, origen=modelo))
    return grupos


def _variantes_grupos(donor, dm, host, etiqueta, texturas, hm=None):
    """Una expresion/pose se convierte desde SU L00, no desde el BODY."""
    clave = _clave(etiqueta)
    db = next((i for i, n in enumerate(donor.labels) if _clave(n) == clave), None)
    hb = next((i for i, n in enumerate(host.labels) if _clave(n) == clave), None)
    if db is None or hb is None:
        raise ValueError('Variante sin anclaje L00: ' + etiqueta)
    dw, hw = np.asarray(donor.worlds()), np.asarray(host.worlds())
    if 'FACE' in etiqueta:
        T = hw[host.bone('HEAD')] @ np.linalg.inv(dw[donor.bone('HEAD')]) @ dw[db]
    else:
        T = hw[hb]
    P = dm.pos @ T[:3, :3].T + T[:3, 3]
    N = dm.nrm @ T[:3, :3].T
    if hm is not None:
        # El emisor del AWG auxiliar trabaja en su marco local (habitualmente I).
        T2 = np.asarray(hm.worlds())[0] @ np.linalg.inv(hw[hb])
        P = P @ T2[:3, :3].T + T2[:3, 3]
        N = N @ T2[:3, :3].T
    grupos = []
    import injertar_pelo as ip
    td = np.array([d for d, _ in ip.triangulos_por_draw(dm)])
    for di, d in enumerate(dm.table.draws):
        tris = dm.tris[td == di]
        if not len(tris):
            continue
        mi = d['material']
        mat = bytearray(dm.data[dm.table.mat_base + mi * 0x50:dm.table.mat_base + (mi + 1) * 0x50])
        info = dm.mats[mi]
        struct.pack_into('>II', mat, 0x30, texturas.traer(dm, info['tex'], True, info['env']), texturas.traer(dm, info['shader']))
        grupos.append(dict(P=P, N=N, B=np.full(len(P), hb), UV=dm.uv, tris=tris,
                           material=bytes(mat), rigido=0 if hm else hb, region='expresion', origen=dm))
    return grupos


def _anillo_cuello(model):
    """Borde REAL entre cabeza/cuerpo; no usa radios genericos de otra raza."""
    regiones = _region(model)
    P, _ = model.posed()
    lados = []
    for region in ('cabeza', 'cuerpo'):
        vv = np.unique(model.tris[regiones == region])
        lados.append({tuple(np.round(P[v], 4)): int(v) for v in vv})
    comunes = lados[0].keys() & lados[1].keys()
    return np.asarray([lados[0][p] for p in sorted(comunes)], int)


def _soldar_cuello(grupos, head, body):
    a, b = _anillo_cuello(head), _anillo_cuello(body)
    gh = next((g for g in grupos if g['region'] == 'cabeza'), None)
    gb = next((g for g in grupos if g['region'] == 'cuerpo'), None)
    if not len(a) or not len(b) or gh is None or gb is None:
        return dict(cabeza=len(a), cuerpo=len(b), soldados=0)
    target = gb['P'][b]
    center = target.mean(0)
    ang = np.arctan2(target[:, 2] - center[2], target[:, 0] - center[0])
    orden = np.argsort(ang)
    angles, ring = ang[orden], target[orden]
    source = gh['P'][a]
    sc = source.mean(0)
    sa = np.arctan2(source[:, 2] - sc[2], source[:, 0] - sc[0])
    angles = np.r_[angles[-1] - 2*np.pi, angles, angles[0] + 2*np.pi]
    ring = np.vstack([ring[-1], ring, ring[0]])
    dest = np.column_stack([np.interp(sa, angles, ring[:, i]) for i in range(3)])
    # Incluye duplicados UV/normales del mismo punto: no deja una segunda piel.
    hp, _ = head.posed()
    keys = {tuple(np.round(hp[v], 4)): dest[i] for i, v in enumerate(a)}
    edits = {id(g['P']): g['P'] for g in grupos if g['region'] == 'cabeza'}
    count = 0
    for points in edits.values():
        for v in np.unique(head.tris[_region(head) == 'cabeza']):
            p = keys.get(tuple(np.round(hp[v], 4)))
            if p is not None:
                points[v] = p
                count += 1
    return dict(cabeza=len(a), cuerpo=len(b), soldados=count)


def _mascara_rasgo(model, rasgo):
    claves = np.asarray([_clave(model.labels[b]) for b in model.vbone])
    head = _region(model) == 'cabeza'
    if rasgo == 'antena':
        return head & np.any(np.char.find(claves[model.tris], 'HAIR') >= 0, axis=1)
    if rasgo == 'cara':
        # El AWG de expresion sustituye SOLO L00_FACE (Model.use_variant).
        # Mandibula, dientes y labios M_* siguen animados en el AWG principal.
        # Retirarlos junto a FACE dejaba huecos y una boca sin mandibula.
        face = claves == 'L00_FACE'
        return head & np.all(face[model.tris], axis=1)
    if rasgo == 'gema':
        return head & np.array([model.mats[i]['env'] for i in model.tri_mat], bool)
    if rasgo == 'cuerno':
        P, _ = model.posed()
        center = np.asarray(model.worlds())[model.bone('HEAD'), :3, 3]
        face = np.array(['FACE' in c for c in claves])
        width = max(float(np.max(np.abs(P[face, 0] - center[0]))), .1)
        # El corte se apoya en la anchura de la mascara nativa, no en una
        # coordenada absoluta comun a Freezer pequeño/Cooler gigante.
        side = (np.abs(P[:, 0] - center[0]) > width * 1.02) & (P[:, 1] > center[1] + .25)
        return head & np.any(side[model.tris], axis=1)
    raise ValueError('Rasgo desconocido: ' + rasgo)


def _antenas(grupos, host, tipo):
    """Dobla solo las antenas nativas en su raiz; no escala la cabeza."""
    if not tipo:
        return
    hechas = set()
    for g in grupos:
        if g['region'] != 'antena':
            continue
        for side in ('LHAIR', 'RHAIR', 'HAIR'):
            vv = np.unique(g['tris'])
            vv = np.array([v for v in vv if _clave(host.labels[int(g['B'][v])]).startswith(side)
                           and (id(g['P']), int(v)) not in hechas], int)
            if not len(vv):
                continue
            bone = next((i for i, label in enumerate(host.labels) if _clave(label) == side + '1'), None)
            if bone is None:
                bone = int(g['B'][vv[0]])
            root = np.asarray(host.worlds())[bone, :3, 3]
            angle = np.deg2rad((0, -22, 22, 40)[tipo])
            c, s = np.cos(angle), np.sin(angle)
            R = np.array([[1., 0, 0], [0, c, -s], [0, s, c]])
            g['P'][vv] = (g['P'][vv] - root) @ R.T + root
            g['N'][vv] = g['N'][vv] @ R.T
            hechas.update((id(g['P']), int(v)) for v in vv)


def _reducir_interiores(grupos, objetivo):
    """Colapsa aristas cortas interiores, conservando bordes UV, huesos y materiales.

    Los puntos, UV y normales que quedan son los originales del donante. No
    agrupa por rejilla ni mueve un borde de costura para encajar el presupuesto.
    """
    hechos, distancia = 0, 0.0
    for _ in range(32):
        antes = hechos
        candidatos = []
        for gi, g in enumerate(grupos):
            t = np.asarray(g['tris'], int)
            edges = np.sort(np.concatenate((t[:, :2], t[:, 1:], t[:, ::2])), axis=1)
            edges, counts = np.unique(edges, axis=0, return_counts=True)
            frontera = set(edges[counts != 2].ravel())
            vecinos = defaultdict(set)
            for a, b in edges:
                vecinos[int(a)].add(int(b))
                vecinos[int(b)].add(int(a))
            for a, b in edges[counts == 2]:
                if a in frontera or b in frontera or g['B'][a] != g['B'][b]:
                    continue
                largo = float(np.linalg.norm(g['P'][a] - g['P'][b]))
                if largo > .75 or float(g['N'][a] @ g['N'][b]) < .95:
                    continue
                if len(vecinos[int(a)] & vecinos[int(b)]) != 2:
                    continue
                candidatos.append((largo, gi, int(a), int(b), vecinos))
        ocupados = defaultdict(set)
        for largo, gi, a, b, vecinos in sorted(candidatos, key=lambda x: x[:4]):
            if a in ocupados[gi] or b in ocupados[gi]:
                continue
            g = grupos[gi]
            t = g['tris']
            afectadas = np.any(t == b, axis=1)
            anteriores = t[afectadas]
            nuevas = np.where(anteriores == b, a, anteriores)
            buenas = np.array([len(set(x)) == 3 for x in nuevas])
            if buenas.any():
                P = g['P']
                po, pn = P[anteriores[buenas]], P[nuevas[buenas]]
                no = np.cross(po[:, 1] - po[:, 0], po[:, 2] - po[:, 0])
                nn = np.cross(pn[:, 1] - pn[:, 0], pn[:, 2] - pn[:, 0])
                norma = np.linalg.norm(no, axis=1) * np.linalg.norm(nn, axis=1)
                if np.any(norma < 1e-12) or np.any(np.sum(no * nn, axis=1) < .8 * norma):
                    continue
            g['tris'] = np.vstack((t[~afectadas], nuevas[buenas]))
            ocupados[gi].update({a, b} | vecinos[a] | vecinos[b])
            hechos += 1
            distancia = max(distancia, largo)
            if hechos >= objetivo:
                return dict(aristas=hechos, distancia_max=distancia)
        if hechos == antes:
            break
    return dict(aristas=hechos, distancia_max=distancia)


def _emitir(amb, host, grupos, n_tex, awg=0, reducciones=None):
    from awg_vertex_buffer import AwgVertexBuffer, be32, set32
    from b3_gateway import DrawTable, stripify, WINDOW_BUDGET
    from skin_slots import SkinSlots
    avb = AwgVertexBuffer._parse(bytes(amb), awg=awg)
    table = DrawTable(avb)
    ss = SkinSlots(avb)
    world, _ = avb.bind_worlds()
    inv = np.linalg.inv(np.asarray(world))
    vertices, compartidas, indices, draws, mats = [], {}, [], defaultdict(list), []
    for g in grupos:
        mi = len(mats)
        mats.append(g['material'])
        local = {}
        for v in np.unique(g['tris']):
            bone = int(g['B'][v])
            effective = 0 if awg else g['rigido'] or ss.effective_bone(bone)
            slot = 0 if awg or g['rigido'] else ss.slot_of[effective]
            p = inv[effective, :3, :3] @ g['P'][v] + inv[effective, :3, 3]
            n = inv[effective, :3, :3] @ g['N'][v]
            raw = AwgVertexBuffer.pack_window(dict(pos=p, nrm=n, uv=g['UV'][v], bone=slot, w=1.0))
            key = (effective, raw)
            if key not in compartidas:
                compartidas[key] = len(vertices)
                vertices.append(raw)
            local[int(v)] = compartidas[key]
        tris = [tuple(local[int(v)] for v in t) for t in g['tris']]
        lista = bool(awg or g['rigido'])
        seq = [v for t in tris for v in t] if lista else stripify(tris)
        first = len(indices)
        indices.extend(seq)
        draws[g['rigido']].append((mi, first, len(tris) if lista else len(seq) - 2, seq))
    if awg == 0 and len(vertices) > WINDOW_BUDGET:
        reducciones = list(reducciones or [])
        cambio = _reducir_interiores(grupos, len(vertices) - WINDOW_BUDGET)
        if cambio['aristas'] and len(reducciones) < 8:
            reducciones.append(cambio)
            return _emitir(amb, host, grupos, n_tex, awg, reducciones)
    if not vertices or (awg == 0 and len(vertices) > WINDOW_BUDGET):
        raise ValueError('Combinacion fuera del presupuesto B3: %d ventanas' % len(vertices))
    # grow(pre=...) ya reubica paleta, AWGs, AZT y tablas de expresiones.
    pre = (len(mats) * 0x50 + sum(map(len, draws.values())) * 0x60 + 15) & ~15
    obj = avb.grow(max(avb.n, len(vertices)), max(avb.n_ib, len(indices)), pre=pre)
    b, a = obj.data, obj.awg0
    cursor = avb.vb0
    set32(b, a + 0x20, cursor - a)
    set32(b, a + 0x24, len(mats))
    set32(b, a + 0x18, n_tex)
    for mat in mats:
        b[cursor:cursor + 0x50] = mat
        cursor += 0x50
    axes = a + be32(b, a + 0x14)
    disponibles = {d['group_bone'] for d in table.draws}
    if not set(draws) <= disponibles:
        raise ValueError('Donante requiere un grupo rigido ausente en el anfitrion')
    prototipos = {d['group_bone']: bytes(avb.data[d['abs']:d['abs'] + 0x60])
                  for d in reversed(table.draws)}
    for bone in disponibles:
        arm = a + be32(b, axes + bone * 80 + 0x34)
        lista = draws.get(bone, [])
        set32(b, arm + 4, cursor - a if lista else 0)
        for k, (mi, first, count, seq) in enumerate(lista):
            b[cursor:cursor + 0x60] = prototipos[bone]
            set32(b, cursor, len(lista) if k == 0 else 0)
            set32(b, cursor + 0x24, mi << 16)
            set32(b, cursor + 0x30, 4 if awg or bone else 5)
            set32(b, cursor + 0x38, min(seq))
            set32(b, cursor + 0x3C, max(seq) + 1 - min(seq))
            set32(b, cursor + 0x40, first)
            set32(b, cursor + 0x44, count)
            # Tras el terminador del nombre hay datos binarios nativos. No
            # rellenar 0x49..0x60 con ceros ni usar el descriptor BODY en manos/cara.
            cursor += 0x60
    for i in range(obj.n):
        at = obj.vb0 + 44 * i
        b[at:at + 44] = vertices[i] if i < len(vertices) else vertices[0]
    for i in range(obj.n_ib):
        struct.pack_into('>H', b, obj.ib_abs + 2 * i, indices[i] if i < len(indices) else 0xFFFF)
    return bytes(b), dict(ventanas=len(vertices), triangulos=sum(len(g['tris']) for g in grupos),
                          materiales=len(mats), draws=sum(map(len, draws.values())),
                          ajuste_presupuesto=reducciones or [])


def aplicar(amb, ficha, juego=None, afs=None):
    sel = seleccion(ficha)
    if sel is None:
        return bytes(amb), {}
    if afs is None:
        raise ValueError('Las piezas B3 necesitan el AFS del jugador')
    import model_render as mr
    from b3_gateway import replace_azt, clamp_awg_textures
    raza, valores = sel
    if not experimental():
        # Mantener el bin nativo entero conserva pesos, paleta, cadena de cola,
        # manos y expresiones. Reemitirlo con peso 1 rigidizaba la cola de Freezer.
        if '_raza_modelo_forma' in ficha:
            fid = int(ficha['_raza_modelo_forma'])
            raw = bytearray(afs.entry(fid))
        elif not explicita(ficha):   # CAC-02: sin piezas de raza, el modelo del cuerpo y traje elegidos (amb)
            fid = None
            raw = bytearray(amb)
        else:
            fid = CUERPOS[raza][valores[2]]
            raw = bytearray(afs.entry(fid))
        modelo = mr.Model(bytes(raw))
        tex = _Texturas(bytes(raw), ficha, raza)
        bases = {m['tex']: m['env'] for m in modelo.mats if m['tex'] != 0xFFFFFFFF}
        z = raw.find(b'#AZT')
        for indice, gema in bases.items():
            ent = z + tex.offsets[indice]
            at, size = tex.be32(raw, ent + 0x14), tex.be32(raw, ent + 0x18)
            raw[z + at:z + at + size] = tex._color(bytes(raw[z + at:z + at + size]), gema)
        return bytes(raw), dict(raza=raza, cabeza=fid, cuerpo=fid,
                                cola=fid if raza == 'freezer' else None,
                                tipo_cabeza=valores[0] + 1, tipo_cuerpo=valores[2] + 1,
                                conjunto_nativo=True, variantes_nativas=True,
                                procedencia='AFS local; bin nativo completo, sin reemitir geometria')
    cabeza = valores[0] % 8
    fhead = CABEZAS[raza][cabeza]
    fbody = CUERPOS[raza][valores[2]]
    # La cola tiene su propia cadena; nunca se transfiere a una raza sin cola.
    ftail = CUERPOS['freezer'][valores[3]] if raza == 'freezer' else fbody
    host = mr.Model(bytes(amb))
    cuerno = valores[0] // 8 if raza == 'freezer' else 0
    gema = valores[1] // 8 if raza == 'freezer' else 0
    cara = valores[1] % 8 if raza == 'freezer' else valores[3]
    if raza == 'namek':
        # Los trajes con turbante no tienen antenas. La segunda serie de
        # cabezas usa otra expresion NATIVA, tambien bajo el turbante.
        cara = (cara + cabeza // 4) % 4
    # Type 1 = el propio rasgo de la cabeza. Los otros proceden de su
    # donante declarado, no de un .blend ni de un json de geometria.
    fface = fhead if cara == 0 or raza != 'freezer' else CABEZAS[raza][cara]
    fhorn = (fhead, 181, 182, 183, 167, None)[cuerno] if raza == 'freezer' else fhead
    fgem = (fhead, 185, 167)[gema] if raza == 'freezer' else fhead
    fantenna = (fhead, 128, 133, 141)[valores[1]] if raza == 'majin' else fhead
    modelos = {fid: mr.Model(afs.entry(fid)) for fid in {fhead, fbody, ftail, fface, fhorn, fgem, fantenna}
               if fid is not None}
    tex = _Texturas(bytes(amb), ficha, raza)
    grupos = []
    for region, fid in (('cabeza', fhead), ('cuerpo', fbody), ('cola', ftail)):
        m = modelos[fid]
        mask = _region(m) == region
        if region == 'cabeza':
            for rasgo, changed in (('cuerno', cuerno), ('gema', gema), ('cara', cara and raza == 'freezer'),
                                   ('antena', raza != 'freezer')):
                if changed:
                    mask &= ~_mascara_rasgo(m, rasgo)
        grupos.extend(_grupos(m, host, mask, region, tex))
    for rasgo, fid, changed in (('cuerno', fhorn, cuerno), ('gema', fgem, gema),
                                ('cara', fface, cara and raza == 'freezer'),
                                ('antena', fantenna, raza != 'freezer')):
        if changed and fid is not None:
            m = modelos[fid]
            # Todos estos rasgos estan en el marco de la cabeza.
            nuevos = _grupos(m, host, _mascara_rasgo(m, rasgo), 'cabeza', tex)
            for g in nuevos:
                g['region'] = rasgo
            grupos.extend(nuevos)
    seam = _soldar_cuello(grupos, modelos[fhead], modelos[fbody])
    if raza == 'namek':
        # Ocho combinaciones reales: cuatro cabezas/trajes B3, dos disposiciones
        # de sus antenas. El segundo selector añade cuatro poses independientes.
        _antenas(grupos, host, (valores[1] + cabeza // 4) % 4)
    elif raza == 'majin':
        _antenas(grupos, host, int(ficha.get('antena_pose', 0)) % 4)
    if cara and raza != 'freezer':
        donor = modelos[fhead]
        labels = [(lab, k) for lab, k in donor.variants().items() if 'FACE' in lab]
        if not labels:
            raise ValueError('Cabeza sin expresiones nativas')
        lab, k = labels[(cara - 1) % len(labels)]
        # Retira la cara L00 antes de poner la expresion: nunca dos caras.
        face_mask = _mascara_rasgo(donor, 'cara')
        for g in grupos:
            if g['region'] == 'cabeza':
                selected = {tuple(t) for t in donor.tris[face_mask]}
                g['tris'] = np.asarray([t for t in g['tris'] if tuple(t) not in selected], int).reshape(-1, 3)
        grupos = [g for g in grupos if len(g['tris'])]
        grupos.extend(_variantes_grupos(donor, mr.Model(donor.data, awg=k), host, lab, tex))
    data, resumen = _emitir(bytes(amb), host, grupos, len(tex.offsets))
    # Las expresiones y manos se seleccionan por etiqueta, nunca por orden AWG.
    variantes = []
    for etiqueta, k in host.variants().items():
        es_cara = 'FACE' in etiqueta
        donor = modelos[fface if es_cara else fbody]
        suffix = etiqueta.lstrip('X').split('_', 1)[-1]
        dk = next((i for lab, i in donor.variants().items()
                    if lab.lstrip('X').split('_', 1)[-1] == suffix), None)
        if es_cara and raza == 'namek' and cabeza >= 4:
            caras = [i for lab, i in donor.variants().items() if 'FACE' in lab]
            if dk in caras:
                dk = caras[(caras.index(dk) + 1) % len(caras)]
        if dk is None:
            variantes.append(dict(awg=k, conservada=True))
            continue
        dm = mr.Model(donor.data, awg=dk)
        hm = mr.Model(data, awg=k)
        # Una variante es rigida y sus coordenadas YA son locales al grupo.
        piezas = _variantes_grupos(donor, dm, host, etiqueta, tex, hm)
        data, detalle = _emitir(data, hm, piezas, len(tex.offsets), awg=k)
        variantes.append(dict(awg=k, donante=dk, **detalle))
    data = replace_azt(data, tex.terminar())
    clamp_awg_textures(data, len(tex.offsets))
    resumen.update(raza=raza, cabeza=fhead, cuerpo=fbody, cola=ftail if raza == 'freezer' else None,
                   cuernos=fhorn if raza == 'freezer' else None, gema=fgem if raza == 'freezer' else None,
                   cara=fface, antena=fantenna if raza != 'freezer' else None, costura=seam,
                   tipo_cabeza=cabeza + 1, tipo_cuerpo=valores[2] + 1, variantes=variantes,
                   procedencia='AFS local; DDS, materiales, toon y matcap nativos')
    return bytes(data), resumen


def formas(afs, fid, ficha, juego, resumen, construir):
    if seleccion(ficha) is None:
        return None
    import roster_build as rb
    body = int(ficha['cuerpo'])
    row = rb.IDS[body]
    if int(ficha.get('donante', body)) != body:
        return []
    traje = max(int(ficha.get('traje', 0)), 0)
    costume = min(traje, row['costumes'] - 1)
    # CAC-02: trajes extra de mods (sus modelos por forma)
    vh = sys.modules.get('vista_heroe')   # vista_heroe se registra con ese nombre aunque corra como script
    extra = vh.EXTRA_FORMAS.get((body, traje)) if vh else None
    out = []
    for indice, (donante, forma) in enumerate(row.get('form_models', [])[1:], 1):
        source = rb.IDS.get(donante) or rb.DB['form_ids'].get(str(donante))
        if not source:
            raise ValueError('Forma B3 sin modelo declarado: %s' % donante)
        per = source.get('models_per_costume', 1)
        tr = min(costume, len(source['models']) // per - 1)
        model_fid = extra[min(indice, len(extra) - 1)] if extra and donante == body else source['models'][tr * per + forma]
        fk = dict(ficha, pelo=0, cara=0)
        if not experimental():
            fk['_raza_modelo_forma'] = model_fid
        data, detalle = construir(afs.entry(model_fid), fk, juego, afs)
        out.append((data, indice))
        resumen['raza_b3_forma%d' % indice] = dict(fid=model_fid, detalle=detalle)
    return out
