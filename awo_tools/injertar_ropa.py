#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""injertar_ropa.py - Injerto MULTI-HUESO de una prenda (studies 9b, "fase A" del creador por piezas).

Como injertar_pelo.py (misma maquinaria: ventanas de 44 B, tabla de paleta intacta via grow(), IB nuevo para el draw
anfitrion, tope de ventanas) pero:
  * cada vertice lleva SU hueso B3 (el de mayor peso de la fuente, ya traducido por nombre a un hueso del cuerpo): slot de
    paleta = SkinSlots.slot_for(hueso) (sube al ancestro con slot) y posicion local = inv(world[hueso efectivo]) . p.
    El skinning de B3 HD es "un hueso por ventana" (sin mezclar pesos): las costuras entre huesos son duras, la prenda se
    dobla como las piezas rigidas de B3.
  * la prenda REEMPLAZA la geometria nativa de una zona (mascara de triangulos de UN draw anfitrion) y su textura
    REEMPLAZA la del anfitrion (misma resolucion, DXT3): el material/la rampa toon del anfitrion se quedan. Solo es seguro
    si esa textura la usa solo ese draw (el que llama lo comprueba con `textura_exclusiva`).
API: injertar_ropa(amb, model, quitar, host, P, N, UV, bones, tris, rgba) -> (bytes, resumen)
"""
import collections
import io
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (HERE, os.path.join(HERE, "..", "mod center hd")):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.append(_p)

from awg_vertex_buffer import AwgVertexBuffer, WINDOW, minv_affine  # noqa: E402
from b3_gateway import DrawTable, stripify, WINDOW_BUDGET  # noqa: E402
from skin_slots import SkinSlots  # noqa: E402
from injertar_pelo import triangulos_por_draw, _rango_draw  # noqa: E402


def textura_exclusiva(model, draws_de_tri, host_draw):
    """True si la textura del draw anfitrion solo la usan triangulos de ese draw."""
    tex = model.mats[model.tri_mat[next(i for i, d in enumerate(draws_de_tri) if d == host_draw)]]["tex"]
    return all(d == host_draw for i, d in enumerate(draws_de_tri) if model.mats[model.tri_mat[i]]["tex"] == tex)


def _escribir_textura(data, tex_idx, rgba):
    """Pone `rgba` (h, w, 4 uint8) como la textura DXT3 `tex_idx` del #AMB `data` (bytearray). Misma resolucion."""
    import texture_b3 as tb  # noqa: PLC0415
    _, texs = tb.parse_azt(bytes(data))
    t = next(x for x in texs if x["idx"] == tex_idx)
    w, h = t["w"], t["h"]
    if t["size"] - 128 != w * h:
        raise ValueError("la textura %d del anfitrion no es DXT3 (%d x %d)" % (tex_idx, w, h))
    if rgba.shape[:2] != (h, w):
        from PIL import Image  # noqa: PLC0415
        rgba = np.asarray(Image.fromarray(rgba).resize((w, h), Image.LANCZOS))
    nuevo = tb.encode_dxt3(np.ascontiguousarray(rgba, np.uint8))
    assert len(nuevo) == w * h
    a = t["bitmap_abs"]
    data[a:a + w * h] = nuevo
    return w, h


def injertar_ropa(amb, model, quitar, host, P, N, UV, bones, tris, rgba, presupuesto=WINDOW_BUDGET):
    """amb: #AMB del cuerpo. model: model_render.Model de ese cuerpo. quitar: bool por triangulo de model.tris (los de
    `host` que se quitan). host: indice del draw anfitrion. P/N/UV: vertices de la prenda (espacio de bind del cuerpo,
    UV 0..1 de `rgba`), bones: indice de hueso B3 por vertice, tris: (m,3). rgba: textura nueva (uint8)."""
    td = triangulos_por_draw(model)
    assert len(td) == len(model.tris)
    quitar = np.asarray(quitar, bool)
    avb = AwgVertexBuffer._parse(bytes(amb))
    dt = DrawTable(avb)
    ss = SkinSlots(avb)
    world, parents = avb.bind_worlds()
    invw = [np.array(minv_affine(w)) for w in world]
    labels = avb.bone_labels()
    resumen = {}
    quitados, kept = collections.defaultdict(list), collections.defaultdict(list)
    for (di, t), q in zip(td, quitar):
        (quitados if q else kept)[di].append(t)
    hd = dt.draws[host]
    if hd["prim"] != 5 and hd["group_bone"] != 0:
        raise ValueError("el draw anfitrion es una lista rigida de un hueso: no admite huesos por ventana")
    quitados.setdefault(host, [])
    resumen["anfitrion"] = "draw %d (prim %d, quitados %d)" % (host, hd["prim"], len(quitados[host]))

    # --- ventanas de la prenda: un hueso efectivo y su marco local por vertice
    P, N, UV, bones = np.asarray(P, float), np.asarray(N, float), np.asarray(UV, float), np.asarray(bones, int)
    usados = np.unique(np.asarray(tris, int).reshape(-1))
    remap = {int(o): i for i, o in enumerate(usados)}
    tu = [tuple(remap[int(v)] for v in t) for t in np.asarray(tris, int)]
    Pu, Nu, Uu, Bu = P[usados], N[usados], UV[usados], bones[usados]
    eff = np.array([ss.effective_bone(int(b)) for b in Bu])
    loc, nl, slots = np.zeros_like(Pu), np.zeros_like(Pu), np.zeros(len(Pu), int)
    for i in range(len(Pu)):
        m = invw[eff[i]]
        loc[i] = m[:3, :3] @ Pu[i] + m[:3, 3]
        nl[i] = m[:3, :3] @ Nu[i]
        slots[i] = ss.slot_of[int(eff[i])]
    nl /= np.maximum(np.linalg.norm(nl, axis=1), 1e-9)[:, None]
    resumen["huesos"] = dict(collections.Counter(labels[int(e)] for e in eff).most_common(12))

    # --- reparto de ventanas (como injertar_pelo)
    refs_final = set()
    for di, d in enumerate(dt.draws):
        if di in quitados:
            for t in kept[di]:
                refs_final.update(t)
        else:
            refs_final.update(v for v in _rango_draw(avb, d) if v != 0xFFFF)
    libres = sorted({v for di in quitados for t in quitados[di] for v in t} - refs_final)
    tope = max(presupuesto, avb.n)
    H = len(Pu)
    if avb.n + max(0, H - len(libres)) > tope:
        raise ValueError("la prenda no cabe en el tope de ventanas (%d + %d - %d libres > %d)" % (avb.n, H, len(libres), tope))
    destino = list(libres[:H])
    n_nuevas = max(0, H - len(destino))
    destino += list(range(avb.n, avb.n + n_nuevas))
    resumen["ventanas"] = {"prenda": H, "reutilizadas": min(H, len(libres)), "nuevas": n_nuevas, "total": avb.n + n_nuevas}

    seqs = {}
    tri_new = [tuple(destino[i] for i in t) for t in tu]
    for di in sorted(quitados):
        ts = list(kept[di]) + (tri_new if di == host else [])
        seqs[di] = (stripify(ts) if ts else []) if dt.draws[di]["prim"] == 5 else [i for t in ts for i in t]
    add_ib = sum(len(s) for s in seqs.values())
    obj = avb.grow(avb.n + n_nuevas, avb.n_ib + add_ib) if (n_nuevas or add_ib) else avb
    dt2 = DrawTable(obj)
    b = obj.data
    for i in range(H):
        w = {"pos": tuple(loc[i]), "w": 1.0, "bone": int(slots[i]), "nrm": tuple(nl[i]),
             "uv": (float(Uu[i][0]), float(Uu[i][1]))}
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
            lo, hi = min(seq), max(seq)
            a0, ac = min(a0, lo), max(a0 + ac, hi + 1) - min(a0, lo)
        dt2.set_range(d, a0, ac, cur, n_prim)
        cur += len(seq)

    # --- textura nueva en el sitio de la del anfitrion
    mat = dt.materials[hd["material"]]
    wh = _escribir_textura(b, mat["tex"], rgba)
    resumen["textura"] = "tex %d %dx%d" % (mat["tex"], wh[0], wh[1])
    resumen["triangulos"] = len(tri_new)
    resumen["quitados"] = sum(len(v) for v in quitados.values())
    return bytes(b), resumen


def injertar_ropa_multi(amb, model, piezas, presupuesto=WINDOW_BUDGET):
    """Sustituye varias zonas/draws en UNA reconstruccion de la tabla y el pool de ventanas.

    Cada pieza es un dict con `quitar`, `host`, `P`, `N`, `UV`, `bones`, `tris`, `rgba` y `zona`.
    Evita que un injerto anterior cambie la clasificacion por huesos que se usa para preparar el siguiente.
    """
    if not piezas:
        return bytes(amb), {}
    td = triangulos_por_draw(model)
    if len(td) != len(model.tris):
        raise ValueError("DrawTable no coincide con los triangulos del modelo base")
    avb = AwgVertexBuffer._parse(bytes(amb))
    dt = DrawTable(avb)
    ss = SkinSlots(avb)
    world, _ = avb.bind_worlds()
    invw = [np.array(minv_affine(w)) for w in world]
    labels = avb.bone_labels()

    hosts = set()
    textures = set()
    remove_all = np.zeros(len(model.tris), bool)
    prepared = []
    for pieza in piezas:
        host = int(pieza["host"])
        if host in hosts:
            raise ValueError("dos piezas usan el mismo draw anfitrion %d" % host)
        hosts.add(host)
        quitar = np.asarray(pieza["quitar"], bool)
        if len(quitar) != len(model.tris):
            raise ValueError("mascara de triangulos incorrecta en %s" % pieza.get("zona", "?"))
        remove_all |= quitar
        d = dt.draws[host]
        if d["prim"] != 5 and d["group_bone"] != 0:
            raise ValueError("draw anfitrion %d no admite huesos por ventana" % host)
        material = dt.materials[d["material"]]
        tex = int(material["tex"])
        if tex in textures:
            raise ValueError("dos zonas comparten la textura anfitriona %d" % tex)
        textures.add(tex)

        P, N = np.asarray(pieza["P"], float), np.asarray(pieza["N"], float)
        UV, bones = np.asarray(pieza["UV"], float), np.asarray(pieza["bones"], int)
        tris = np.asarray(pieza["tris"], int)
        usados = np.unique(tris.reshape(-1))
        remap = {int(o): i for i, o in enumerate(usados)}
        tris_local = [tuple(remap[int(v)] for v in t) for t in tris]
        Pu, Nu, Uu, Bu = P[usados], N[usados], UV[usados], bones[usados]
        eff = np.array([ss.effective_bone(int(b)) for b in Bu])
        loc, nl, slots = np.zeros_like(Pu), np.zeros_like(Pu), np.zeros(len(Pu), int)
        for i in range(len(Pu)):
            m = invw[eff[i]]
            loc[i] = m[:3, :3] @ Pu[i] + m[:3, 3]
            nl[i] = m[:3, :3] @ Nu[i]
            slots[i] = ss.slot_of[int(eff[i])]
        nl /= np.maximum(np.linalg.norm(nl, axis=1), 1e-9)[:, None]
        prepared.append({"host": host, "loc": loc, "nrm": nl, "uv": Uu, "slots": slots,
                         "tris": tris_local, "rgba": pieza["rgba"], "zona": pieza.get("zona", "?"),
                         "placement": pieza.get("placement", {})})

    removed, kept = collections.defaultdict(list), collections.defaultdict(list)
    for (di, tri), remove in zip(td, remove_all):
        (removed if remove else kept)[di].append(tri)
    refs_final = set()
    for di, d in enumerate(dt.draws):
        if di in removed:
            for tri in kept[di]:
                refs_final.update(tri)
        else:
            refs_final.update(v for v in _rango_draw(avb, d) if v != 0xFFFF)
    libres = sorted({v for di in removed for tri in removed[di] for v in tri} - refs_final)
    n_piezas = sum(len(p["loc"]) for p in prepared)
    tope = max(presupuesto, avb.n)
    if avb.n + max(0, n_piezas - len(libres)) > tope:
        raise ValueError("las piezas no caben en el tope de ventanas (%d + %d - %d libres > %d)"
                         % (avb.n, n_piezas, len(libres), tope))
    destinos = list(libres[:n_piezas])
    n_nuevas = max(0, n_piezas - len(destinos))
    destinos += list(range(avb.n, avb.n + n_nuevas))
    seqs = {}
    tri_new = collections.defaultdict(list)
    offset = 0
    for p in prepared:
        verts = destinos[offset:offset + len(p["loc"])]
        for t in p["tris"]:
            tri_new[p["host"]].append(tuple(verts[v] for v in t))
        offset += len(p["loc"])
    changed = set(removed) | set(tri_new)
    for di in sorted(changed):
        ts = list(kept[di]) + tri_new.get(di, [])
        seqs[di] = stripify(ts) if ts and dt.draws[di]["prim"] == 5 else [v for tri in ts for v in tri]
    add_ib = sum(len(s) for s in seqs.values())
    obj = avb.grow(avb.n + n_nuevas, avb.n_ib + add_ib) if (n_nuevas or add_ib) else avb
    dt2 = DrawTable(obj)
    b = obj.data
    offset = 0
    for p in prepared:
        for i in range(len(p["loc"])):
            v = {"pos": tuple(p["loc"][i]), "w": 1.0, "bone": int(p["slots"][i]),
                 "nrm": tuple(p["nrm"][i]), "uv": (float(p["uv"][i][0]), float(p["uv"][i][1]))}
            at = obj.vb0 + destinos[offset + i] * WINDOW
            b[at:at + WINDOW] = AwgVertexBuffer.pack_window(v)
        offset += len(p["loc"])
    cur = avb.n_ib
    for di in sorted(seqs):
        seq = seqs[di]
        d = dt2.draws[di]
        for k, v in enumerate(seq):
            struct.pack_into(">H", b, obj.ib_abs + (cur + k) * 2, int(v) & 0xFFFF)
        n_prim = max(0, len(seq) - 2) if d["prim"] == 5 else len(seq) // 3
        a0, ac = d["A"]
        if di in hosts and seq:
            lo, hi = min(seq), max(seq)
            a0, ac = min(a0, lo), max(a0 + ac, hi + 1) - min(a0, lo)
        dt2.set_range(d, a0, ac, cur, n_prim)
        cur += len(seq)

    resumen = {"zonas": {}, "ventanas": {"piezas": n_piezas, "reutilizadas": min(n_piezas, len(libres)),
                                           "nuevas": n_nuevas, "total": avb.n + n_nuevas}}
    for p in prepared:
        mat = dt2.materials[dt2.draws[p["host"]]["material"]]
        wh = _escribir_textura(b, mat["tex"], p["rgba"])
        resumen["zonas"][p["zona"]] = {"anfitrion": p["host"], "triangulos": len(p["tris"]), "textura": wh,
                                         "placement": p["placement"]}
    return bytes(b), resumen
