"""PELO-03: banco optativo de recetas, sobre modelos B3 HD del usuario."""
from functools import lru_cache
from pathlib import Path
import os
from struct import pack_into, unpack_from
import tomllib

import numpy as np

_CATALOGO = Path(__file__).with_name('peinados_r17.toml')
_BASES = {264, 416, 235, 390, 298, 447}
_HOSTS = {0, 3, 7, 8}  # corpus R17, traje 0 (264/235/416/390)


def solicitada(ficha):
    return (os.environ.get('DBZ3_CAC_PELO_R17') == '1' and bool(ficha.get('pelo_r17'))
            and ficha.get('cuerpo', 0) in _HOSTS and ficha.get('traje', 0) == 0)


@lru_cache(maxsize=2)
def _catalogo(raw):
    d = tomllib.loads(raw.decode('utf-8'))
    if (d['version'], d['ranura'], d['operacion'], d['raza_ssj'], d['fisica']) != (1, 'pelo', 'sustituir', 'saiyan', 'rigido'):
        raise ValueError('contrato de catálogo R17 desconocido')
    if not (d['fid_paleta_ssj'] == 266 and d['ssj3'] == {'fid_largo_referencia': 268, 'ocultar_cejas': True}
            and 1.20 <= d['ssj1']['altura'] <= 1.30 and 0 < d['ssj1']['recoge_frente'] < 1
            and 0 < d['ssj2']['caida'] < 1):
        raise ValueError('reglas de forma no verificadas')
    recetas = {r['id']: r for r in d['receta']}
    if len(recetas) != len(d['receta']) or {r['fid_base'] for r in recetas.values()} != _BASES or len(recetas) != len(_BASES):
        raise ValueError('catálogo R17 incompleto o ambiguo')
    for r in recetas.values():
        if not (-0.15 <= r['alto_base'] <= 0.15 and r['lado_flequillo'] in (-1, 1) and r['fid_melena'] in (268, 360)):
            raise ValueError('receta R17 fuera del banco')
    return d, recetas


def aplicar(vh, amb, ficha, afs):
    """El llamador conserva intacto el camino anterior si se rechaza el montaje."""
    raw = _CATALOGO.read_bytes()
    d, recetas = _catalogo(raw)
    id_ = ficha['pelo_r17']
    r = recetas[id_]
    nivel = int(ficha.get('_ssj', 0))
    if nivel not in (0, 1, 2, 3):
        raise ValueError('forma sin receta R17')
    fid = r['fid_melena'] if nivel == 3 else r['fid_base']
    # No resolver donantes mediante overrides de los mods ni aceptar FIDs de la ficha.
    fids = {fid}
    if nivel and fid != 268:
        fids.add(266)
    if nivel == 3:
        fids.add(268)
    entradas = {k: bytes(afs.entry(k)) for k in fids}
    ssj_nativa = bool(ficha.get('_ssj_nativa'))
    data, quitados, insertados, ventanas, slot = _montar(vh, bytes(amb), raw, id_, nivel, tuple(sorted(entradas.items())), ssj_nativa)
    return data, dict(receta=id_, forma=nivel, fid=fid, quitados=quitados, insertados=insertados,
                      atlas=slot, ventanas={'total': ventanas, 'tope': 3261})


@lru_cache(maxsize=4)
def _montar(vh, amb, raw, id_, nivel, entradas, ssj_nativa=False):
    import injertar_pelo as ip
    from pelo_nativos03 import atlas03
    import texture_b3 as tb

    d, recetas = _catalogo(raw)
    r = recetas[id_]
    entradas = dict(entradas)
    m = vh.mr.Model(amb)
    P, _ = m.posed(vh.pose_de(m))
    # Sustitucion completa tambien en anfitriones SSJ con pelo de color repartido en varias texturas (Trunks 391).
    zonas = vh.zonas_ficha(m, P, pelo_color=True)
    mask = [z == 'pelo' for z in zonas]
    cara = [z == 'cara' for z in zonas]
    if not any(mask) or not any(cara) or not vh._host_pelo_exclusivo(m, zonas)[1]:
        raise ValueError('sin máscara de pelo exclusiva')
    L, W, inv = vh._local_head(m, P)
    head = W[m.bone('HEAD')]
    marcas = vh._marcas_cara(m, L, vh._tris_cara(m), W, inv)
    if marcas is None:
        raise ValueError('sin anclas de ojos')
    ojos = np.mean(marcas[0], axis=0)
    cara_y = L[np.unique(m.tris[cara])][:, 1].max()
    ojos_x = abs(marcas[0][0][0] - marcas[0][1][0]) / 2
    if not (cara_y > ojos[1] and ojos_x > 0 and ojos[2] > 0):
        raise ValueError('anclas de ojos incoherentes')
    # No sólo albedo del BODY: Gohan 235 comparte slots con rampas de ropa
    # y AWG auxiliares (gestos/manos). No se puede sobrescribir ninguno.
    ocupadas = ip._tex_usadas(amb)
    ocupadas.update(vh.ATLAS_PELO_RESERVADAS)
    faciales = {}
    if nivel and (not ssj_nativa or nivel == 3):
        # En un cuerpo SSJ nativo (266/267/417/391) los ojos ya son turquesa: repintar el iris
        # degradaba >50 % de los pixeles azules. En SSJ3 nativo solo se ocultan las cejas.
        faciales = vh.texturas_rostro(amb, iris_rgb=None if ssj_nativa else vh.SSJ_OJOS,
                                     ceja_rgb=None if nivel == 3 else vh.SSJ_CEJAS, sin_cejas=nivel == 3)
        if not faciales:
            raise ValueError('sin texturas de rostro SSJ')
        ocupadas.update(faciales)
    z, texs = tb.parse_azt(amb)
    candidatos = [(t['w'], -t['idx']) for t in texs if t['idx'] not in ocupadas
                  and 64 <= t['w'] == t['h'] <= 256 and t['size'] - 128 == t['w'] ** 2]
    if candidatos:
        lado, negativo = max(candidatos)
        slot = -negativo
    else:
        amb, slot, lado = _extender_azt(amb, z, texs)

    @lru_cache(maxsize=3)
    def atlas(fid):
        don = vh._pelo_donante(m, P, zonas, entradas[fid])
        if don is None:
            raise ValueError('donante sin pelo: %d' % fid)
        dm, ht, dvs, Q, QN, tris, _ = don
        a = atlas03(dm, ht, dvs, tris, Q, QN, lado=lado)
        if a is None:
            raise ValueError('donante sin atlas: %d' % fid)
        Q, N, tris, uv, rgba, _ = a
        return Q @ inv[:3, :3].T + inv[:3, 3], N, tris, uv, rgba

    fid = r['fid_melena'] if nivel == 3 else r['fid_base']
    X, N, tris, uv, rgba = atlas(fid)
    x, y, z = X.T
    h = max(y.max() - cara_y, cara_y - ojos[1])
    u = np.clip((y - cara_y) / h, 0, 1)
    frente = np.clip(z / ojos[2], 0, 1)
    Y = y + r['alto_base'] * h * u
    if nivel in (1, 2):
        Y += (d['ssj1']['altura'] - 1) * h * u
        Y += d['ssj1']['recoge_frente'] * frente * np.maximum(cara_y - y, 0)
        if nivel == 2:
            t = np.clip(r['lado_flequillo'] * (x - ojos[0]) / ojos_x, 0, 1)
            Y -= np.minimum(d['ssj2']['caida'] * h * 4 * t * (1 - t) * frente, np.maximum(Y - ojos[1], 0))
    if nivel == 3:
        ref = atlas(268)[0]
        i = np.argmin(np.where(z < 0, y, np.inf))
        bajo, ref_bajo = y[i], ref[ref[:, 2] < 0, 1].min()
        if not (z[i] < 0 and bajo < ojos[1] and ref_bajo < ojos[1]):
            raise ValueError('melena sin caída posterior')
        posterior = np.clip(-z / abs(z[i]), 0, 1)
        Y += (ref_bajo - bajo) * posterior * np.clip((ojos[1] - y) / (ojos[1] - bajo), 0, 1)
    Q = np.column_stack((x, Y, z)) @ head[:3, :3].T + head[:3, 3]
    if not (np.isfinite(Q).all() and np.isfinite(N).all() and len(tris)):
        raise ValueError('geometría no válida')
    if nivel and fid != 268:
        # R17: dos tonos del 266. En donantes negros, #AWG usa ese negro como
        # masa de base, no como tinta. Llevarlo al oro de sombra; el SSJ3 268
        # mantiene su albedo auténtico y sus trazos internos sin recolor.
        _, _, _, uv_ref, oro = atlas(266)
        iy, ix = np.clip((uv_ref[:, ::-1] * lado).astype(int), 0, lado - 1).T
        muestras = oro[iy, ix, :3]
        muestras = muestras[(muestras[:, 0] > muestras[:, 1]) & (muestras[:, 1] > muestras[:, 2])]
        sombra, luz = np.percentile(muestras, [25, 85], axis=0)
        rgba = rgba.copy()
        tono = rgba[..., :3].max(axis=-1).astype(float)
        positivos = tono[tono > 0]
        factor = np.clip(tono / np.percentile(positivos, 85), 0, 1) if positivos.size else np.ones_like(tono)
        rgba[..., :3] = np.rint(sombra + factor[..., None] * (luz - sombra))
    data, info = ip.injertar(amb, m, mask, Q, N, tris, cara_mask=cara, uvs=uv, tex_destino=slot, tex_rgba=rgba)
    if info.get('quitados') != sum(mask) or 'decimado' in info or not info.get('atlas'):
        raise ValueError('sustitución de pelo incompleta')
    ventanas = ip.AwgVertexBuffer._parse(data).n
    if ventanas > ip.WINDOW_BUDGET or ventanas != info['ventanas']['total']:
        raise ValueError('presupuesto excedido o incoherente')
    if faciales:
        from cac17_materiales import reempacar

        data = bytearray(data)
        z, finales = tb.parse_azt(data)
        for ti, im in faciales.items():
            tx = finales[ti]
            a, n = tx['bitmap_abs'], tx['w'] * tx['h']
            # parse_azt estima tamaños por diferencia. En el DDS que ahora
            # precede al nuevo atlas, hay relleno y la tabla nueva en medio.
            ndds = unpack_from('>I', data, z + tx['off'] + 0x18)[0]
            if ndds != 128 + n or data[a-44:a-40] != b'DXT3' or im.shape[:2] != (tx['h'], tx['w']):
                raise ValueError('textura facial no DXT3 o tamaño incoherente')
            rgba = np.rint(np.clip(im, 0, 1) * 255).astype(np.uint8)
            original = np.rint(np.clip(m.tex[ti], 0, 1) * 255).astype(np.uint8)
            data[a:a+n] = reempacar(data[a:a+n], original, rgba)
    return bytes(data), info['quitados'], info['triangulos_pelo'], ventanas, slot


def _extender_azt(amb, z, texs):
    """Un slot DXT3 adicional. Conserva los descriptores y DDS originales.

    Basado en el esquema de sb_tecnicas.azt_append y b3_gateway.replace_azt:
    índice nuevo al final; para el grabador de pelo el DDS nuevo debe cerrar AZT.
    """
    def u32(b, p):
        return unpack_from('>I', b, p)[0]

    n, tabla = unpack_from('>II', amb, 0x10)
    kids = [tabla + 16 * j for j in range(n)]
    e = next((k for k in kids if u32(amb, k) == z), None)
    if e is None or amb[0x40:0x44] != b'#AWO' or any(u32(amb, k) > z for k in kids):
        raise ValueError('#AZT no es el último hijo')
    opciones = []
    for t in texs:
        o = z + t['data_off']
        ndds = u32(amb, z + t['off'] + 0x18)
        if 64 <= t['w'] == t['h'] <= 256 and ndds == 128 + t['w'] ** 2 and amb[o+84:o+88] == b'DXT3':
            opciones.append(t)
    if not opciones:
        raise ValueError('sin plantilla DXT3 para ampliar #AZT')
    tpl = max(opciones, key=lambda t: (t['w'], -t['idx']))
    slot = len(texs)
    out = bytearray(amb)
    out += bytes((-len(out)) % 32)
    ent = len(out)
    out += amb[z + tpl['off']:z + tpl['off'] + 0x30]
    idx = len(out)
    for t in texs:
        out += t['off'].to_bytes(4, 'big')
    out += (ent - z).to_bytes(4, 'big')
    out += bytes((-len(out)) % 128)
    pack_into('>I', out, ent, slot)
    pack_into('>I', out, ent + 0x14, len(out) - z)
    vram = max(u32(amb, z + t['off'] + 0x24) + (u32(amb, z + t['off'] + 0x18) + 31) // 32 for t in texs)
    pack_into('>I', out, ent + 0x24, vram)
    o = z + tpl['data_off']
    out += amb[o:o + 128 + tpl['w'] ** 2]
    pack_into('>II', out, z + 0x10, slot + 1, idx - z)
    pack_into('>I', out, e + 4, len(out) - z)
    awgs, tbl = unpack_from('>II', amb, 0x40 + 0x18)
    for k in range(awgs):
        a = 0x40 + u32(amb, 0x40 + tbl + k * 4)
        if amb[a:a+4] != b'#AWG':
            raise ValueError('tabla #AWG no reconocida')
        pack_into('>I', out, a + 0x18, slot + 1)
    return bytes(out), slot, tpl['w']
