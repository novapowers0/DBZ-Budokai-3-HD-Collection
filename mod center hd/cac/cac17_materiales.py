"""Materiales CAC-17 del B3 HD: editar el AZT ya montado, sin tocar AWO ni añadir capas."""
import hashlib
import io
import re
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.spatial import cKDTree

import texture_b3 as tb

ZONAS = ("pelo", "cara", 0, 1, 2, 3)
MAX_LIENZO = 256


def ajustes(ficha):
    sombras, brillos = {}, {}
    for item in str(ficha.get('material17', '')).split('|')[:6]:
        if not re.fullmatch(r'[0-9]{1,3}(,[0-9]{1,3}){4}', item):
            continue
        z, r, g, b, brillo = map(int, item.split(','))
        if z >= len(ZONAS) or max(r, g, b) > 255 or brillo > 3:
            continue
        if r or g or b:
            sombras[ZONAS[z]] = np.array([r, g, b], dtype=np.float32) / 255
        if brillo and ficha.get('_ssj', 0):
            brillos[ZONAS[z]] = brillo
    return sombras, brillos


def dxt3(data):
    out = {}
    for t in tb.parse_azt(data)[1]:
        a, w, h = t['bitmap_abs'], t['w'], t['h']
        if (0 < w <= MAX_LIENZO and 0 < h <= MAX_LIENZO and not (w % 4 or h % 4)
                and t['size'] == 128 + w * h and 128 <= a <= len(data) - w * h
                and data[a-44:a-40] == b'DXT3'):
            out[t['idx']] = t
    return out


def leer(data, t):
    a = t['bitmap_abs']
    return np.array(Image.open(io.BytesIO(data[a-128:a+t['w']*t['h']])).convert('RGBA'))


def huella(data, t):
    a = t['bitmap_abs']
    return hashlib.sha256(data[a-128:a+t['w']*t['h']]).hexdigest()


def reempacar(original, antes, despues):
    """Solo sustituir mitades RGB de los bloques modificados. Alfa = nibbles BC2, sin error de redondeo."""
    h, w = antes.shape[:2]
    bloques = np.frombuffer(original, dtype=np.uint8).reshape(-1, 16).copy()
    def agrupar(a):
        return a.reshape(h//4, 4, w//4, 4).transpose(0, 2, 1, 3).reshape(-1, 16)
    rgb = agrupar(np.any(antes[..., :3] != despues[..., :3], axis=2)).any(axis=1)
    if rgb.any():
        nuevos = np.frombuffer(tb.encode_dxt3(despues), dtype=np.uint8).reshape(-1, 16)
        bloques[rgb, 8:] = nuevos[rgb, 8:]
    alfa = agrupar(antes[..., 3] != despues[..., 3])
    if alfa.any():
        n = agrupar(((despues[..., 3].astype(np.uint16) + 8) // 17).astype(np.uint8))
        b = bloques[:, :8]
        b[:] = np.where(alfa[:, ::2], (b & 240) | n[:, ::2], b)
        b[:] = np.where(alfa[:, 1::2], (b & 15) | (n[:, 1::2] << 4), b)
    return bloques.tobytes()


def mascaras(data, v, texturas, expresiones=True):
    """Máscaras por textura GLOBAL, incluso al montar un AWG facial (attach duplica la tabla en RAM)."""
    m = v.mr.Model(data)
    n0 = len(m.tex)
    grupos = {}
    def agregar(modelo, zonas, indices):
        tris = {}
        for i in indices:
            mi = modelo.mats[modelo.tri_mat[i]]
            if not 0 <= mi['tex'] < len(modelo.tex):
                continue
            ti = mi['tex'] % n0
            if ti in texturas:
                tris.setdefault((ti, zonas[i], bool(mi['blend'])), []).append(i)
        for (ti, z, blend), ids in tris.items():
            t = texturas[ti]
            r = v._rasterizar_uv(modelo, ids, t['w'], t['h'])
            if r is not None:
                y, x = r[:2]
                grupos.setdefault(ti, {}).setdefault((z, blend), np.zeros((t['h'], t['w']), bool))[y, x] = True
    p, _ = m.posed(v.pose_de(m))
    agregar(m, v.zonas_ficha(m, p), range(len(m.tris)))
    if expresiones:
        for label in sorted(k for k in m.variants() if 'FACE' in k):
            alt = v.mr.Model(data)
            if alt.use_variant(label):
                ids = [i for i, t in enumerate(alt.tri_mat) if t >= len(m.mats)]
                agregar(alt, ['cara'] * len(alt.tris), ids)
    return grupos


def exclusiva(grupos, z):
    mask = grupos[z, False].copy()
    for k, rival in grupos.items():
        if k != (z, False):
            mask &= ~rival
    return mask


def capas(ficha, fuente, texturas):
    ruta, _, valores = str(ficha.get('pintura17', '')).partition(':')
    if not re.fullmatch(r'(?:[a-f0-9]{2}){1,4096}', ruta):
        return {}
    try:
        carpeta = Path(bytes.fromhex(ruta).decode('utf-8'))
    except (ValueError, UnicodeError):
        return {}
    out = {}
    for par in valores.split('|')[:8]:
        m = re.fullmatch(r'([0-9]{1,4}),([a-f0-9]{16,32})', par)
        if not m or int(m[1]) not in texturas:
            continue
        ti = int(m[1]); t = texturas[ti]
        n = t['w'] * t['h'] * 4
        try:
            with (carpeta / (m[2] + '.rgba')).open('rb') as f:
                raw = f.read(64 + n + 1)
            if len(raw) == 64 + n and raw[:64] == huella(fuente, t).encode('ascii'):
                out[ti] = np.frombuffer(raw[64:], dtype=np.uint8).reshape(t['h'], t['w'], 4)
            else:
                print('CAC-17: pintura no coincide con textura fuente', ti)
        except OSError:
            print('CAC-17: falta pintura local', ti)
    return out


def proyectar(directas, muestras, permitidas, umbral=0.025):
    """Un único color por texel: trazo directo > muestra 3D más cercana; nunca apilar expresiones."""
    posiciones, colores = [], []
    for ti, c in sorted(directas.items()):
        if ti not in permitidas:
            continue
        for (y, x), p in muestras.get(ti, {}).items():
            if c[y, x, 3] and permitidas[ti][y, x]:
                posiciones.append(p)
                colores.append(c[y, x])
    if not posiciones:
        return directas
    arbol, colores = cKDTree(posiciones), np.asarray(colores)
    out = dict(directas)
    for ti, muestras_t in muestras.items():
        if ti not in permitidas or not muestras_t:
            continue
        pares = [(yx, p) for yx, p in muestras_t.items() if permitidas[ti][yx]]
        if not pares:
            continue
        coord, pt = zip(*pares)
        y, x = np.asarray(coord).T
        d, k = arbol.query(pt, distance_upper_bound=umbral)
        c = directas[ti].copy() if ti in directas else np.zeros((*permitidas[ti].shape, 4), np.uint8)
        sel = np.isfinite(d) & (c[y, x, 3] == 0)
        c[y[sel], x[sel]] = colores[k[sel]]
        out[ti] = c
    return out


def aplicar(data, fuente, ficha, v):
    sombras, brillos = ajustes(ficha)
    pintura = ficha.get('pintura17')
    if not (sombras or brillos or pintura):
        return data, []
    tt = dxt3(data)
    masks = mascaras(data, v, tt)
    origen = dxt3(fuente) if pintura else {}
    dm = capas(ficha, fuente, origen) if pintura else {}
    # Un cambio de traje/forma nunca reproduce un trazo por simple coincidencia de índice/tamaño.
    dm = {ti: c for ti, c in dm.items() if ti in masks and c.shape[:2] == (tt[ti]['h'], tt[ti]['w'])}
    if dm and any(('cara', False) in masks[ti] for ti in dm):
        base = v.mr.Model(data)
        muestras = {}
        for lab in [None] + sorted(k for k in base.variants() if 'FACE' in k):
            m = base if lab is None else v.mr.Model(data)
            if not lab or m.use_variant(lab):
                p, _ = m.posed(v.pose_de(m))
                v._muestras_cara(m, p, len(base.tex), muestras)
        faces = {t: exclusiva(g, 'cara') for t, g in masks.items() if ('cara', False) in g}
        dm = proyectar(dm, {t: p for t, p in muestras.items() if t in faces}, faces)
    out, editadas = bytearray(data), []
    for ti, grupos in masks.items():
        if ti not in dm and not any((z, False) in grupos for z in sombras.keys() | brillos.keys()):
            continue
        t = tt[ti]; a = t['bitmap_abs']; fin = a + t['w'] * t['h']
        antes = leer(data, t)
        img = antes.copy()
        for z in sombras.keys() | brillos.keys():
            if (z, False) not in grupos:
                continue
            mask = exclusiva(grupos, z)
            # 5F27AACEB38B1088: base menos rampa; alfa DXT3 selecciona banda,
            # 15 deja solo la base. La textura de sombra respeta el dibujo oscuro.
            tono = antes[..., :3].max(axis=2) / 255
            if z in sombras:
                sel = mask & (antes[..., 3] <= 7 * 17) & (tono >= .15)
                img[sel, :3] = np.rint(255 * tono[sel, None] * sombras[z]).astype(np.uint8)
            if z in brillos:
                nivel = brillos[z]; sel = mask & (tono >= .15)
                img[sel, 3] = 255
                if nivel > 1:
                    f = .175 * (nivel - 1)
                    img[sel, :3] = np.rint((1-f)*img[sel, :3] + f*255).astype(np.uint8)
        if ti in dm:
            c = dm[ti]; opacas = np.zeros(img.shape[:2], bool); otras = opacas.copy()
            for (_, blend), g in grupos.items():
                (otras if blend else opacas).__ior__(g)
            sel = (c[..., 3] != 0) & opacas & ~otras
            f = c[sel, 3, None] / 255
            img[sel, :3] = np.rint(img[sel, :3]*(1-f) + c[sel, :3]*f).astype(np.uint8)
        if np.any(img != antes):
            nuevo = reempacar(data[a:fin], antes, img)
            if nuevo != data[a:fin]:
                out[a:fin] = nuevo
                editadas.append(ti)
    return bytes(out), editadas


def lienzo(data, ti, dst, v):
    t = dxt3(data).get(ti)
    if not t:
        raise ValueError('CAC-17: textura no editable (requiere DXT3)')
    grupos = mascaras(data, v, {ti: t}).get(ti, {})
    mask = np.zeros((t['h'], t['w']), bool)
    for (_, blend), g in grupos.items():
        if not blend:
            mask |= g
    for (_, blend), g in grupos.items():
        if blend:
            mask &= ~g
    if not mask.any():
        raise ValueError('CAC-17: textura sin islas editables')
    img = leer(data, t)
    img[..., 3] = 255  # el alfa del lienzo no es el selector de banda de B3
    dst = Path(dst)
    tmp = dst.with_suffix('.tmp')
    tmp.write_bytes(huella(data, t).encode('ascii') + img.tobytes() + mask.astype(np.uint8).tobytes())
    tmp.replace(dst)
