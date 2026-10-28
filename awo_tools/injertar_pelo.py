#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""injertar_pelo.py - Injerta un peinado UT (pieza convertida) en el bin #AMB HD de un cuerpo, para el COMBATE.

El skinning de B3 HD es "un hueso por ventana" (campo `bone` = slot de la tabla de paleta). Aqui el peinado se
pega RIGIDO a la cabeza: todas sus ventanas llevan el slot del hueso *_HEAD y la posicion/normal en el espacio
local de ese hueso (inv(world_bind[HEAD]) . modelo), asi que sigue a la cabeza en cada animacion.

Pasos (todo sobre el AWG0, sin mover texturas):
  1. Los triangulos de pelo NATIVO (los que decide el que llama: la misma regla que la vista previa) se quitan del IB
     de su draw (se reescribe el IB de los draws tocados en una zona nueva, al final).
  2. El peinado nuevo (vertices ya ajustados al cuerpo, en espacio modelo) entra en el draw que mas pelo nativo tenia
     ("anfitrion", se queda con su material): sus UV apuntan a un texel del pelo nativo de ESA textura (que
     `combate_bin` ya tino con el color de pelo de la ficha), asi el color sale igual que el de la vista previa.
  3. Las ventanas que solo usaba el pelo nativo se reutilizan; lo que falte se anade (awg_vertex_buffer.grow, que
     mueve la tabla de paleta pegada al IB). Tope: WINDOW_BUDGET ventanas (combate tolera 3261; mas = cuerpo invisible):
     si no cabe, se agrupan vertices (decimacion por rejilla).

API: injertar(amb, model, pelo_mask, Q, QN, tris_ut, ...) -> (bytes, resumen). Lint: draw_oracle.py y
comprobar_assets.py (ver `python injertar_pelo.py --selftest <bin_cuerpo> <bin_pelo>`).
"""
import collections
import io
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (HERE, os.path.join(HERE, "..", "awo_tools"), os.path.join(HERE, "..", "mod center hd"),
           os.path.join(HERE, "..")):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.append(_p)

from awg_vertex_buffer import AwgVertexBuffer, WINDOW, minv_affine, set32  # noqa: E402
from b3_gateway import DrawTable, strip_tri, stripify, WINDOW_BUDGET  # noqa: E402
from skin_slots import SkinSlots  # noqa: E402


def triangulos_por_draw(model):
    """[(indice de draw, triangulo)] en el MISMO orden que model_render.Model.tris (misma regla de filtrado)."""
    avb, table = model.avb, model.table
    ib = avb.indices()
    nv = avb.n
    out = []
    for di, d in enumerate(table.draws):
        s, c = d["B"]
        mat = table.materials[d["material"]]
        if not c or (mat["tex"] >= len(model.tex) and mat["tex"] != 0xFFFFFFFF):
            continue
        seq = ib[s:s + c + 2] if d["prim"] == 5 else ib[s:s + 3 * c]
        tt = [strip_tri(seq, i) for i in range(len(seq) - 2)] if d["prim"] == 5 else \
            [tuple(seq[i:i + 3]) for i in range(0, len(seq) - 2, 3)]
        for t in tt:
            if len(set(t)) != 3 or max(t) >= nv:
                continue
            out.append((di, t))
    return out


def _rango_draw(avb, d):
    s, c = d["B"]
    ib = avb.indices()
    if not c:
        return []
    return ib[s:s + c + 2] if d["prim"] == 5 else ib[s:s + 3 * c]


def _hueso_cabeza(labels):
    """Indice del hueso <PFX>_HEAD (sin la X de los huesos auxiliares)."""
    for i, l in enumerate(labels):
        if l.endswith("_HEAD") and not l.startswith("X"):
            return i
    return None


def _texel_pelo(tx, uvs, objetivo=None, tris_uv=None):
    """(u, v) de un texel representativo del pelo nativo: de entorno liso (para que el filtrado bilineal/los bloques
    DXT no lo mezclen con un vecino de otro color) y, con `objetivo` (RGB 0..1), el mas parecido a ese color (el que
    `tenir` dejo en la textura); sin objetivo, de brillo medio. tris_uv: triangulos del pelo nativo en uv (rasteriza
    la zona de pelo de la textura: solo se eligen texeles de ahi)."""
    from PIL import Image, ImageDraw  # noqa: PLC0415
    h, w = tx.shape[:2]
    mask = None
    if tris_uv:
        im = Image.new("L", (w, h), 0)
        d = ImageDraw.Draw(im)
        for tri in tris_uv:
            d.polygon([(float((u % 1) * w), float((v % 1) * h)) for u, v in tri], fill=1)
        mask = np.asarray(im) > 0
        _texel_pelo.mask = mask
    cand = []
    pts = [(x, y) for y, x in zip(*np.nonzero(mask))] if mask is not None and mask.any() else         [(int(np.clip((u % 1) * w, 0, w - 1)), int(np.clip((v % 1) * h, 0, h - 1))) for u, v in uvs]
    for x, y in pts[::max(1, len(pts) // 4000)]:
        ys = [(y + k) % h for k in range(-2, 3)]
        xs = [(x + k) % w for k in range(-2, 3)]
        blk = tx[np.ix_(ys, xs)][..., :3]
        luma = float(blk[2, 2] @ np.array([0.299, 0.587, 0.114]))
        std = float(blk.std(axis=(0, 1)).sum())
        dist = float(np.abs(blk[2, 2] - np.array(objetivo)).sum()) if objetivo is not None else 0.0
        cand.append((dist, std, luma, (x + 0.5) / w, (y + 0.5) / h))
    if not cand:
        return 0.5, 0.5
    if objetivo is not None:
        best = min(cand, key=lambda c: c[0] + 0.7 * c[1])
        return best[3], best[4]
    lumas = sorted(c[2] for c in cand)
    lo, hi = lumas[len(lumas) // 4], lumas[(3 * len(lumas)) // 4]
    medio = [c for c in cand if lo <= c[2] <= hi] or cand
    best = min(medio, key=lambda c: c[1])
    return best[3], best[4]


def _bloque_libre(tx, mask, excluir, ancho=4):
    """Texel (u, v) del centro de un bloque 4x4 alineado, entero dentro de `mask`, distinto del bloque `excluir` (bx, by)."""
    h, w = tx.shape[:2]
    excl = excluir if isinstance(excluir, (set, frozenset)) else {excluir}
    for by in range(0, h // ancho):
        for bx in range(0, w // ancho):
            if (bx, by) not in excl and mask[by * ancho:(by + 1) * ancho, bx * ancho:(bx + 1) * ancho].all():
                return bx, by
    return None


def _pintar_bloque_dxt3(data, tex, bx, by, rgb, alfa=None):
    """Pone el bloque (bx, by) de la textura DXT3 `tex` del #AMB `data` (bytearray) a un color liso (conserva el alfa,
    salvo con `alfa` = nibble 0..15: es la banda de la rampa toon)."""
    import texture_b3 as tb  # noqa: PLC0415
    _, texs = tb.parse_azt(bytes(data))
    t = next(x for x in texs if x["idx"] == tex)
    w, h = t["w"], t["h"]
    if t["size"] - 128 != w * h:
        return False
    o = t["bitmap_abs"] + (by * (w // 4) + bx) * 16
    c = ((int(round(rgb[0] * 31)) << 11) | (int(round(rgb[1] * 63)) << 5) | int(round(rgb[2] * 31))) & 0xFFFF
    data[o + 8:o + 16] = struct.pack("<HHI", c, c, 0)
    if alfa is not None:
        data[o:o + 8] = bytes([(alfa << 4) | alfa]) * 8
    return True


def _bloques_calvo(model, tex_idx, tx, n):
    """Calvos (sin pelo nativo): `n` bloques 4x4 de la textura `tex_idx` que ningun triangulo del cuerpo cubre (ni a un bloque
    de distancia: el filtrado no los mezcla con la piel), y el nibble de alfa (banda toon) mas comun de los texeles cubiertos.
    Devuelve ([(bx, by)], alfa_nibble) (menos de n bloques si no hay sitio)."""
    from PIL import Image, ImageDraw  # noqa: PLC0415
    h, w = tx.shape[:2]
    im = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(im)
    for i, tri in enumerate(model.tris):
        if model.mats[model.tri_mat[i]]["tex"] == tex_idx:
            d.polygon([(float((u % 1) * w), float((v % 1) * h)) for u, v in model.uv[tri]], fill=1)
    cub = np.asarray(im) > 0
    bh, bw = h // 4, w // 4
    blk = cub[:bh * 4, :bw * 4].reshape(bh, 4, bw, 4).any(axis=(1, 3))
    gr = blk.copy()
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (-1, -1), (1, -1), (-1, 1)):
        gr |= np.roll(blk, (dy, dx), (0, 1))
    libres = [(int(x), int(y)) for y, x in zip(*np.nonzero(~gr))]
    libres = libres[::-1][:n]                       # los del final de la textura (suelen ser relleno)
    alfas = (tx[..., 3][cub] * 15 + 0.5).astype(int) if tx.shape[2] > 3 and cub.any() else np.array([8])
    return libres, int(np.bincount(alfas, minlength=16).argmax())


def _decimar(P, N, tris, limite):
    """Agrupa vertices por rejilla hasta que quepan en `limite` ventanas. Devuelve (P, N, tris)."""
    lo, hi = P.min(0), P.max(0)
    ext = float(max(hi - lo))
    for celdas in range(int(np.sqrt(limite) * 2.2), 4, -2):
        paso = ext / celdas
        clave = {}
        rep = np.zeros(len(P), int)
        for i, p in enumerate(P):
            k = tuple(np.floor((p - lo) / paso).astype(int))
            rep[i] = clave.setdefault(k, len(clave))
        if len(clave) <= limite:
            break
    nP = np.zeros((len(clave), 3))
    nN = np.zeros((len(clave), 3))
    cnt = np.zeros(len(clave))
    for i, r in enumerate(rep):
        nP[r] += P[i]
        nN[r] += N[i]
        cnt[r] += 1
    nP /= cnt[:, None]
    nN /= np.maximum(np.linalg.norm(nN, axis=1), 1e-9)[:, None]
    nt = [t for t in ((rep[a], rep[b], rep[c]) for a, b, c in tris) if len(set(t)) == 3]
    return nP, nN, nt


def _soldar(ex):
    """Suelda los vertices iguales (posicion + normal) de una pieza extra: una caja de 36 pasa a 24."""
    P, N, uniq, idx = np.asarray(ex["P"], float), np.asarray(ex["N"], float), {}, []
    for p_, n_ in zip(P, N):
        idx.append(uniq.setdefault((tuple(np.round(p_, 4)), tuple(np.round(n_, 2))), len(uniq)))
    nP, nN = np.zeros((len(uniq), 3)), np.zeros((len(uniq), 3))
    for i, r in enumerate(idx):
        nP[r], nN[r] = P[i], N[i]
    tris = [tuple(idx[v] for v in t) for t in ex["tris"]]
    return {"P": nP, "N": nN, "tris": tris, "rgb": ex["rgb"]}


def injertar(amb, model, pelo_mask, Q, QN, tris_ut, presupuesto=WINDOW_BUDGET, rgb1=None, rgb2=None, cara_mask=None,
             extras=None, uvs=None, tex_destino=None, tex_rgba=None):
    """amb: #AMB del cuerpo (bytes, con las texturas ya tenidas). model: model_render.Model del cuerpo (del bin
    ORIGINAL; fija el orden de triangulos de pelo_mask). pelo_mask: bool por triangulo de model.tris (True = pelo
    nativo, se quita). Q/QN: posiciones/normales (n x 3, espacio modelo del cuerpo) de los vertices de la pieza y
    tris_ut: sus triangulos (indices a Q). rgb1/rgb2: colores objetivo (0..1) del pelo 1 (base) y 2 (puntas, el 40 %
    de arriba) ya cocidos en la textura; None = el que haya. cara_mask: bool por triangulo (zona "cara"): si el cuerpo es
    CALVO (sin pelo nativo) el peinado va al draw de la cara y su color se pinta en un bloque libre de esa textura.
    extras: piezas rigidas a la cabeza ademas del pelo (scouter): [{"P", "N", "tris", "rgb"}] (espacio modelo, color liso
    en un bloque libre de la textura del anfitrion). Sin pieza de pelo (tris_ut vacio) no se quita ni se anade pelo.
    uvs (27/10 DS111): UV por vertice de Q (n x 2) en la textura del anfitrion: se usa tal cual en las ventanas
    (a trozos en las costuras de UV) en vez del texel unico del pelo. tex_destino/tex_rgba: textura del #AZT que
    recibe `rgba` (h,w,4 uint8, misma resolucion) y a la que se reapunta el material del anfitrion; sirve para llevar
    el ATLAS del pelo del donante con sus lineas oscuras (Trunks) sin pintar un bloque liso. Devuelve (bytes, resumen)."""
    import model_render as mr  # noqa: PLC0415
    td = triangulos_por_draw(model)
    assert len(td) == len(model.tris), "orden de triangulos distinto del Model (%d != %d)" % (len(td), len(model.tris))
    pelo_mask = np.asarray(pelo_mask, bool)
    if not len(tris_ut):
        pelo_mask = np.zeros_like(pelo_mask)   # sin peinado nuevo el nativo se queda
    avb = AwgVertexBuffer._parse(bytes(amb))
    dt = DrawTable(avb)
    ss = SkinSlots(avb)
    world, parents = avb.bind_worlds()
    invw = [minv_affine(w) for w in world]
    labels = avb.bone_labels()
    resumen = {}

    quitados = collections.defaultdict(list)    # draw -> triangulos de pelo nativo
    kept = collections.defaultdict(list)        # draw -> triangulos que se quedan
    for (di, t), es in zip(td, pelo_mask):
        (quitados if es else kept)[di].append(t)
    extras = [_soldar(ex) for ex in (extras or [])]
    n_extra = sum(len(ex["P"]) for ex in extras)
    calvo = False
    if not quitados:
        cuenta = collections.Counter(di for (di, _), es in zip(td, cara_mask if cara_mask is not None else []) if es)
        if not cuenta:
            return bytes(amb), {"pelo": "el cuerpo no tiene pelo propio ni cara: sin injerto en combate (calvo)"}
        calvo = True
        quitados[cuenta.most_common(1)[0][0]] = []
    host = max(quitados, key=lambda d: len(quitados[d])) if not calvo else next(iter(quitados))
    hd = dt.draws[host]

    # --- hueso y espacio local del injerto
    if hd["prim"] == 5 or hd["group_bone"] == 0:
        cab = _hueso_cabeza(labels)
        if cab is None:
            return bytes(amb), {"pelo": "sin hueso HEAD"}
        eff, seen = cab, set()
        while eff not in ss.slot_of and eff >= 0 and eff not in seen:   # sube al ancestro con slot
            seen.add(eff)
            eff = parents[eff]
        if eff not in ss.slot_of:
            return bytes(amb), {"pelo": "HEAD sin slot de paleta"}
        slot = ss.slot_of[eff]
    else:                                   # lista rigida de un hueso propio (pelo de Vegeta: XVGT_HAIR)
        eff, slot = hd["group_bone"], 0
    inv = invw[eff]
    resumen["anfitrion"] = "draw %d (%s, prim %d, hueso %s, slot %d)" % (host, labels[hd["group_bone"]], hd["prim"],
                                                                      labels[eff], slot)

    # --- UV: texel del pelo nativo de la textura del anfitrion (ya tenida)
    m2 = mr.Model(bytes(amb))
    mat = dt.materials[hd["material"]]
    u2 = v2 = None
    atlas = tex_rgba is not None and tex_destino is not None and uvs is not None and not calvo
    if atlas:
        # 27/10 DS111: UV por vertice (atlas del donante con sus lineas); el material del anfitrion pasa a la textura
        # nueva. No se pinta ningun bloque: los colores van cocidos en el atlas.
        u0 = v0 = 0.5
        resumen["atlas"] = "tex %d %dx%d" % (int(tex_destino), np.asarray(tex_rgba).shape[1], np.asarray(tex_rgba).shape[0])
    elif calvo:                      # sin pelo nativo: bloques libres de la textura de la cara, pintados lisos
        u0 = v0 = 0.5
        tx = m2.tex[mat["tex"]] if mat["tex"] < len(m2.tex) else None
        col1 = rgb1 or (0.13, 0.09, 0.06)
        if tx is not None:
            h_, w_ = tx.shape[:2]
            usadas = (2 if rgb2 is not None else 1) if len(tris_ut) else 0
            bls, alfa = _bloques_calvo(model, mat["tex"], tx, usadas)
            if not usadas:
                pass
            elif bls and _pintar_bloque_dxt3(avb.data, mat["tex"], bls[0][0], bls[0][1], col1, alfa):
                u0, v0 = (bls[0][0] * 4 + 2) / w_, (bls[0][1] * 4 + 2) / h_
                resumen["calvo"] = "bloque liso %s alfa %d" % (bls[0], alfa)
                if rgb2 is not None and len(bls) > 1 and _pintar_bloque_dxt3(avb.data, mat["tex"], bls[1][0], bls[1][1], rgb2, alfa):
                    u2, v2 = (bls[1][0] * 4 + 2) / w_, (bls[1][1] * 4 + 2) / h_
            else:
                resumen["calvo"] = "SIN bloque libre / textura no DXT3: color de la piel"
    elif mat["tex"] < len(m2.tex):
        tris_h = quitados[host]
        uvs = [tuple(model.uv[v]) for t in tris_h for v in t]
        tuv = [[tuple(model.uv[v]) for v in t] for t in tris_h]
        u0, v0 = _texel_pelo(m2.tex[mat["tex"]], uvs, rgb1, tuv)
        if rgb1 is not None:   # 25/10: la textura no tiene ese color (rubio SSJ o lila de Trunks sobre el negro de Goku): bloque liso
            tx = m2.tex[mat["tex"]]
            h_, w_ = tx.shape[:2]
            got = tx[min(int(v0 * h_), h_ - 1), min(int(u0 * w_), w_ - 1), :3]
            mask = getattr(_texel_pelo, "mask", None)
            # exacto: con un texel "parecido" el rubio SSJ salia blanco (brillo) o color piel (borde de la zona)
            if float(np.abs(got - np.array(rgb1)).sum()) > 0.06 and mask is not None:
                bl = _bloque_libre(tx, mask, set())
                if bl and _pintar_bloque_dxt3(avb.data, mat["tex"], bl[0], bl[1], rgb1):
                    u0, v0 = (bl[0] * 4 + 2) / w_, (bl[1] * 4 + 2) / h_
                    resumen["base_pintada"] = True
        if rgb2 is not None:
            u2, v2 = _texel_pelo(m2.tex[mat["tex"]], uvs, rgb2, tuv)
            tx = m2.tex[mat["tex"]]
            h_, w_ = tx.shape[:2]
            got = tx[min(int(v2 * h_), h_ - 1), min(int(u2 * w_), w_ - 1), :3]
            if float(np.abs(got - np.array(rgb2)).sum()) > 0.45:     # la textura no tiene ese color: se pinta un bloque
                mask = getattr(_texel_pelo, "mask", None)
                bl = _bloque_libre(tx, mask, (int(u0 * w_) // 4, int(v0 * h_) // 4)) if mask is not None else None
                if bl and _pintar_bloque_dxt3(avb.data, mat["tex"], bl[0], bl[1], rgb2):
                    u2, v2 = (bl[0] * 4 + 2) / w_, (bl[1] * 4 + 2) / h_
                    resumen["puntas_pintadas"] = True
    else:
        u0, v0 = 0.5, 0.5
    resumen["uv"] = tuple(round(float(x), 4) for x in ((u0, v0) + (() if u2 is None else (u2, v2))))
    uv_extra = []
    if atlas and extras:           # DS111: piezas extra (calva) -> bloques 4x4 en la banda reservada del ATLAS
        _a = np.asarray(tex_rgba, np.uint8)
        _lado = int(_a.shape[0])
        _med = np.median(_a[:_lado - 16, :, :3].reshape(-1, 3), 0)   # color medio del atlas (piel/pelo cocidos)
        for k in range(len(extras)):
            bx, by = 4 * k, _lado - 8
            col = np.clip(np.asarray(extras[k]["rgb"], float), 0.0, 1.0)
            if col.min() > 0.80:   # la piel de B3 es blanca y el tono lo ponía la rampa: en el atlas (banda 15) seria blanca
                col = np.clip(_med / 255.0, 0.0, 1.0)
            _a[by:by + 4, bx:bx + 4, :3] = np.clip(np.round(col * 255), 0, 255).astype(np.uint8)
            _a[by:by + 4, bx:bx + 4, 3] = 255
            uv_extra.append(((bx + 2) / _lado, (by + 2) / _lado))
    if extras and not atlas:       # color liso de cada pieza extra (scouter): bloques libres de la textura del anfitrion
        tx = m2.tex[mat["tex"]] if mat["tex"] < len(m2.tex) else None
        usadas = (2 if rgb2 is not None else 1) if (calvo and len(tris_ut)) else 0
        bls = []
        if tx is not None:
            h_, w_ = tx.shape[:2]
            todos, alfa = _bloques_calvo(model, mat["tex"], tx, usadas + len(extras))
            bls = todos[usadas:]
            mask = getattr(_texel_pelo, "mask", None)
            if len(bls) < len(extras) and mask is not None and not calvo:   # textura llena: bloques DENTRO del pelo nativo
                ocu = {(int(u0 * w_) // 4, int(v0 * h_) // 4)} | ({(int(u2 * w_) // 4, int(v2 * h_) // 4)} if u2 is not None else set())
                while len(bls) < len(extras):
                    b_ = _bloque_libre(tx, mask, ocu)
                    if not b_:
                        break
                    ocu.add(b_)
                    bls.append(b_)
                alfa = 0
        for k, ex in enumerate(extras):
            uv = (0.5, 0.5)
            if k < len(bls) and _pintar_bloque_dxt3(avb.data, mat["tex"], bls[k][0], bls[k][1], ex["rgb"], alfa):
                uv = ((bls[k][0] * 4 + 2) / w_, (bls[k][1] * 4 + 2) / h_)
            else:
                resumen["extra%d" % k] = "SIN bloque libre / no DXT3: color de la piel"
            uv_extra.append(uv)

    # --- vertices del injerto en el espacio local del hueso, soldados (pos + normal)
    Q = np.asarray(Q, float)
    QN = np.asarray(QN, float)
    usados = np.unique(np.asarray(tris_ut, int).reshape(-1))
    remap0 = {int(o): i for i, o in enumerate(usados)}
    Pu, Nu = Q[usados], QN[usados]
    tu = [tuple(remap0[int(v)] for v in t) for t in tris_ut]
    tu = [t for t in tu if len(set(t)) == 3]
    uv_u = np.asarray(uvs, float)[usados] if (atlas and uvs is not None) else None
    uniq, keys = {}, []
    rep = np.zeros(len(Pu), int)
    for i in range(len(Pu)):
        k = (tuple(np.round(Pu[i], 4)), tuple(np.round(Nu[i], 2)),
             (round(float(uv_u[i][0]), 5), round(float(uv_u[i][1]), 5)) if uv_u is not None else None)
        rep[i] = uniq.setdefault(k, len(uniq))
    nP = np.zeros((len(uniq), 3))
    nN = np.zeros((len(uniq), 3))
    nUV = np.zeros((len(uniq), 2)) if uv_u is not None else None
    for i, r in enumerate(rep):
        nP[r], nN[r] = Pu[i], Nu[i]
        if nUV is not None:
            nUV[r] = uv_u[i]
    tu = [t for t in ((rep[a], rep[b], rep[c]) for a, b, c in tu) if len(set(t)) == 3]

    # --- reparto de ventanas: refs tras quitar el pelo nativo
    ib_old = avb.indices()
    refs_final = set()
    for di, d in enumerate(dt.draws):
        if di in quitados:
            for t in kept[di]:
                refs_final.update(t)
        else:
            refs_final.update(v for v in _rango_draw(avb, d) if v != 0xFFFF)
    libres = sorted({v for di in quitados for t in quitados[di] for v in t} - refs_final)
    tope = max(presupuesto, avb.n)
    lim = tope - (avb.n - len(libres)) - n_extra      # ventanas de pelo que caben sin pasar el tope
    nP0, nN0, tu0 = nP, nN, tu
    for _ in range(8):   # decima y, si el reparto en dos colores (vertices duplicados) pasa el tope, vuelve a decimar
        nP, nN, tu = nP0, nN0, tu0
        if len(nP) > max(lim, 64):
            nP, nN, tu = _decimar(nP, nN, tu, max(lim, 64))
            resumen["decimado"] = len(nP)
        grupo = np.zeros(len(nP), int)       # 0 = pelo 1 (base), 1 = pelo 2 (puntas)
        if u2 is not None and len(nP):
            y = nP[:, 1]
            corte = y.min() + 0.6 * (y.max() - y.min())
            dup, nt2, nP2, nN2, g2 = {}, [], [], [], []
            for t in tu:
                g = 1 if np.mean([y[i] for i in t]) > corte else 0
                tri = []
                for i in t:
                    k = (i, g)
                    if k not in dup:
                        dup[k] = len(nP2)
                        nP2.append(nP[i]); nN2.append(nN[i]); g2.append(g)
                    tri.append(dup[k])
                nt2.append(tuple(tri))
            nP, nN, tu, grupo = np.array(nP2), np.array(nN2), nt2, np.array(g2)
        exceso = avb.n + max(0, len(nP) + n_extra - len(libres)) - tope
        if exceso <= 0 or lim <= 64:
            break
        lim -= exceso
    resumen['puntas'] = int(grupo.sum())
    for k, ex in enumerate(extras or []):      # piezas extra: grupo 2+k
        eP, eN = np.asarray(ex["P"], float), np.asarray(ex["N"], float)
        base = len(nP)
        nP = np.concatenate([nP, eP]) if len(nP) else eP
        nN = np.concatenate([nN, eN]) if len(nN) else eN
        tu = list(tu) + [tuple(int(v) + base for v in t) for t in ex["tris"]]
        grupo = np.concatenate([grupo, np.full(len(eP), 2 + k, int)])
        resumen["extra_ventanas"] = resumen.get("extra_ventanas", 0) + len(eP)
    # coordenadas locales
    R = np.array(inv)[:3, :3]
    loc = (nP @ R.T) + np.array(inv)[:3, 3]
    nl = nN @ R.T
    nl /= np.maximum(np.linalg.norm(nl, axis=1), 1e-9)[:, None]
    H = len(loc)
    ventanas = []
    for i in range(H):
        if nUV is not None and i < len(nUV):
            uv_i = (float(nUV[i][0]), float(nUV[i][1]))
        else:
            uv_i = (u0, v0) if grupo[i] == 0 else (u2, v2) if grupo[i] == 1 else uv_extra[grupo[i] - 2]
        ventanas.append({"pos": tuple(loc[i]), "w": 1.0, "bone": slot, "nrm": tuple(nl[i]), "uv": uv_i})
    destino = list(libres[:H])
    n_nuevas = max(0, H - len(destino))
    base_nueva = avb.n
    destino += list(range(base_nueva, base_nueva + n_nuevas))
    resumen["ventanas"] = {"pelo": H, "reutilizadas": min(H, len(libres)), "nuevas": n_nuevas,
                           "total": avb.n + n_nuevas}
    # --- IB nuevo para los draws tocados (al final)
    seqs = {}
    tri_pelo = [tuple(destino[i] for i in t) for t in tu]
    for di in sorted(quitados):
        tris = list(kept[di]) + (tri_pelo if di == host else [])
        d = dt.draws[di]
        if d["prim"] == 5:
            seq = stripify(tris) if tris else []
        else:
            seq = [i for t in tris for i in t]
        seqs[di] = seq
    add_ib = sum(len(s) for s in seqs.values())
    new_n = avb.n + n_nuevas
    new_nib = avb.n_ib + add_ib
    obj = avb.grow(new_n, new_nib) if (n_nuevas or add_ib) else avb
    dt2 = DrawTable(obj)
    b = obj.data
    if atlas:
        import injertar_ropa as _ir  # noqa: PLC0415
        wh = _ir._escribir_textura(b, int(tex_destino), np.asarray(tex_rgba, np.uint8))
        dt2.set_material(hd["material"], tex=int(tex_destino))
        resumen["atlas"] = "tex %d %dx%d (material %d)" % (int(tex_destino), wh[0], wh[1], hd["material"])
    for i, w in enumerate(ventanas):
        o = obj.vb0 + destino[i] * WINDOW
        b[o:o + WINDOW] = AwgVertexBuffer.pack_window(w)
    cur = avb.n_ib
    for di in sorted(seqs):
        seq = seqs[di]
        d = dt2.draws[di]
        for k, v in enumerate(seq):
            struct.pack_into(">H", b, obj.ib_abs + (cur + k) * 2, int(v) & 0xFFFF)
        n_prim = max(0, len(seq) - 2) if d["prim"] == 5 else len(seq) // 3
        a0, ac = d["A"]
        if di == host and seq:
            idx = [v for v in seq]
            lo, hi = min(idx), max(idx)
            a0, ac = min(a0, lo), max(a0 + ac, hi + 1) - min(a0, lo)
        dt2.set_range(d, a0, ac, cur, n_prim)
        cur += len(seq)
    # Rampa toon: la del pelo nativo de Goku es casi negra en sombra (el color 1 se ve negro menos en las puntas). Si el
    # material del anfitrion solo lo usan draws de pelo, pasa a la rampa mas usada por el resto del cuerpo (sombra suave).
    usan = [i for i, d in enumerate(dt.draws) if d["material"] == hd["material"]]
    if all(i in quitados for i in usan) and all(not kept[i] for i in usan if i != host):
        cuenta = collections.Counter(m["shader"] for i, m in enumerate(dt.materials)
                                     if i != hd["material"] and m["shader"] != 0xFFFFFFFF)
        # 25/10: solo rampas GRISES (las de la ropa). El toon RESTA la rampa a la base y la de la piel es azul: el pelo
        # lila de Trunks (y cualquier color claro) salia color carne y el heroe parecia calvo
        def gris(r):
            tx = m2.tex[r] if r < len(m2.tex) else None
            if tx is None:
                return False
            rgb = np.asarray(tx[..., :3], float)
            return float((rgb.max(-1) - rgb.min(-1)).mean()) < 0.06
        cuenta = collections.Counter({r: c for r, c in cuenta.items() if gris(r)}) or cuenta
        if cuenta:
            ramp = cuenta.most_common(1)[0][0]
            dt2.set_material(hd["material"], ramp=ramp)
            resumen["rampa"] = "material %d: rampa %d -> %d" % (hd["material"], mat["shader"], ramp)
    # el relleno de alineacion del IB (grow) ya es 0xFFFF
    resumen["triangulos_pelo"] = len(tri_pelo)
    resumen["quitados"] = sum(len(v) for v in quitados.values())
    return bytes(b), resumen


def _selftest(cuerpo, pelo, salida):
    import model_render as mr  # noqa: PLC0415
    import vista_heroe as vh  # noqa: PLC0415
    amb = open(cuerpo, "rb").read()
    out, r = vh.injertar_ficha(amb, pelo, mr.Model(amb))
    open(salida, "wb").write(out)
    print(r)


if __name__ == "__main__":
    if len(sys.argv) >= 5 and sys.argv[1] == "--selftest":
        _selftest(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        print(__doc__)
