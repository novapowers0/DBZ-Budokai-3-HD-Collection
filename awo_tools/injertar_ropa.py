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

from awg_vertex_buffer import AwgVertexBuffer, WINDOW, be32, minv_affine, set32  # noqa: E402
from b3_gateway import DrawTable, stripify, WINDOW_BUDGET  # noqa: E402
from skin_slots import SkinSlots  # noqa: E402
from injertar_pelo import triangulos_por_draw, _rango_draw  # noqa: E402


def _marco_rigido(draw, prim_destino=None):
    """27/10 DS166: hueso de referencia de una LISTA rigida (prim 4), o None si es tira.

    El guest dibuja las listas rigidas (prim 4) con `group_bone` (ver b3_gateway: `sk =
    bone_of[slot] if prim == 5 else group_bone`), NO con el slot de paleta de la ventana.
    Los huesos L00 de mano/cara NO tienen slot (SkinSlots sube al ancestro: la mano acaba
    en RARM2/LARM2), asi que escribir la ventana en el marco de ese ancestro y con su slot
    desplazaba la malla justo la distancia ancestro->hueso (medido en 447/99: ~2,4 u de
    una mano de largo, la mano suelta de las poses de golpe/caida). La ventana de estas
    listas va en el marco local de `group_bone` y con bone=0 (como las nativas)."""
    prim = int(prim_destino) if prim_destino else int(draw["prim"])
    if prim != 4:
        return None
    gb = int(draw.get("group_bone", 0))
    # group_bone == 0 en una lista = "usa los slots" (p. ej. el draw 2 XGOK_BODY del cuerpo,
    # con slots 20-23); NO se toca.
    return gb if gb > 0 else None


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


def injertar_ropa(amb, model, quitar, host, P, N, UV, bones, tris, rgba, presupuesto=WINDOW_BUDGET,
                  tex_destino=None, prim_destino=None):
    """amb: #AMB del cuerpo. model: model_render.Model de ese cuerpo. quitar: bool por triangulo de model.tris (los de
    `host` que se quitan). host: indice del draw anfitrion. P/N/UV: vertices de la prenda (espacio de bind del cuerpo,
    UV 0..1 de `rgba`), bones: indice de hueso B3 por vertice, tris: (m,3). rgba: textura nueva (uint8).
    tex_destino / prim_destino: ver injertar_ropa_multi (textura libre del #AZT y draw a tira)."""
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
    if hd["prim"] != 5 and hd["group_bone"] != 0 and not prim_destino:
        raise ValueError("el draw anfitrion es una lista rigida de un hueso: no admite huesos por ventana")
    quitados.setdefault(host, [])
    resumen["anfitrion"] = "draw %d (prim %d, quitados %d)" % (host, hd["prim"], len(quitados[host]))

    # --- ventanas de la prenda: un hueso efectivo y su marco local por vertice
    P, N, UV, bones = np.asarray(P, float), np.asarray(N, float), np.asarray(UV, float), np.asarray(bones, int)
    usados = np.unique(np.asarray(tris, int).reshape(-1))
    remap = {int(o): i for i, o in enumerate(usados)}
    tu = [tuple(remap[int(v)] for v in t) for t in np.asarray(tris, int)]
    Pu, Nu, Uu, Bu = P[usados], N[usados], UV[usados], bones[usados]
    rigido = _marco_rigido(hd, prim_destino)
    if rigido is not None:
        # lista rigida: TODO va en el marco de group_bone y bone=0 (nativo)
        eff = np.full(len(Pu), rigido, int)
        slots = np.zeros(len(Pu), int)
    else:
        eff = np.array([ss.effective_bone(int(b)) for b in Bu])
        slots = np.array([ss.slot_of[int(e)] for e in eff], int)
    loc, nl = np.zeros_like(Pu), np.zeros_like(Pu)
    for i in range(len(Pu)):
        m = invw[eff[i]]
        loc[i] = m[:3, :3] @ Pu[i] + m[:3, 3]
        nl[i] = m[:3, :3] @ Nu[i]
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
    prim_final = {di: int(dt.draws[di]["prim"]) for di in quitados}
    if prim_destino:
        prim_final[host] = int(prim_destino)      # 27/10 DS166: el prim destino se aplica de verdad
    for di in sorted(quitados):
        ts = list(kept[di]) + (tri_new if di == host else [])
        seqs[di] = (stripify(ts) if ts else []) if prim_final[di] == 5 else [i for t in ts for i in t]
    add_ib = sum(len(s) for s in seqs.values())
    obj = avb.grow(avb.n + n_nuevas, avb.n_ib + add_ib) if (n_nuevas or add_ib) else avb
    dt2 = DrawTable(obj)
    b = obj.data
    if prim_destino and int(prim_destino) != int(dt2.draws[host]["prim"]):
        set32(b, dt2.draws[host]["abs"] + 0x30, int(prim_destino))
        dt2.draws[host]["prim"] = int(prim_destino)
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

    # --- textura nueva en el sitio de la del anfitrion (o en la textura libre indicada)
    mi = dt2.draws[host]["material"]
    mat = dt.materials[hd["material"]]
    tex = mat["tex"] if tex_destino is None else int(tex_destino)
    if tex != mat["tex"]:
        dt2.set_material(mi, tex=tex)
    wh = _escribir_textura(b, tex, rgba)
    resumen["textura"] = "tex %d %dx%d" % (tex, wh[0], wh[1])
    resumen["triangulos"] = len(tri_new)
    resumen["quitados"] = sum(len(v) for v in quitados.values())
    return bytes(b), resumen


def injertar_ropa_multi(amb, model, piezas, presupuesto=WINDOW_BUDGET):
    """Sustituye varias zonas/draws en UNA reconstruccion de la tabla y el pool de ventanas.

    Cada pieza es un dict con `quitar`, `host`, `P`, `N`, `UV`, `bones`, `tris`, `rgba` y `zona`.
    Evita que un injerto anterior cambie la clasificacion por huesos que se usa para preparar el siguiente.

    25/10 DS35 (guantes): dos claves opcionales por pieza.
      * `tex_destino`: indice de una textura LIBRE del #AZT que recibe el atlas. El material del anfitrion se reapunta a
        ella (el anfitrion puede ser un draw rigido de la mano con material propio de esa zona, p.ej. las manos de Goku,
        mat 11 -> tex 13: la textura de las manos se queda para las variantes de mano de los AWG auxiliares). Con
        `tex_destino` no se exige que la textura del anfitrion sea exclusiva de la zona (no se repinta la del anfitrion).
      * `prim_destino`: primitivas que debe tener el draw anfitrion tras el injerto (5 = tira). Las manos de B3 se
        dibujan como LISTAS rigidas (prim 4, group_bone = hueso de la mano) y no admiten huesos por ventana: para
        meter mano+antebrazo (huesos distintos) en el mismo draw hay que pasarlo a tira.
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
    textures = {}   # textura destino -> rgba (dos piezas pueden compartir textura si el atlas es el mismo)
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
        prim_dest = pieza.get("prim_destino")
        if d["prim"] != 5 and d["group_bone"] != 0 and not prim_dest:
            raise ValueError("draw anfitrion %d no admite huesos por ventana" % host)
        material = dt.materials[d["material"]]
        tex_dest = pieza.get("tex_destino")
        tex = int(material["tex"]) if tex_dest is None else int(tex_dest)
        if tex in textures:
            previa = textures[tex]
            if tex_dest is None or not np.array_equal(np.asarray(previa, np.uint8),
                                                      np.asarray(pieza["rgba"], np.uint8)):
                raise ValueError("dos zonas comparten la textura anfitriona %d" % tex)
        else:
            textures[tex] = np.asarray(pieza["rgba"], np.uint8)

        P, N = np.asarray(pieza["P"], float), np.asarray(pieza["N"], float)
        UV, bones = np.asarray(pieza["UV"], float), np.asarray(pieza["bones"], int)
        tris = np.asarray(pieza["tris"], int)
        usados = np.unique(tris.reshape(-1))
        remap = {int(o): i for i, o in enumerate(usados)}
        tris_local = [tuple(remap[int(v)] for v in t) for t in tris]
        Pu, Nu, Uu, Bu = P[usados], N[usados], UV[usados], bones[usados]
        rigido = _marco_rigido(d, prim_dest)
        if rigido is not None:
            # 27/10 DS166: lista rigida (prim 4). El guest la dibuja con `group_bone`
            # (b3_gateway: prim 4 -> group_bone), no con el slot de la ventana; la mano
            # no tiene slot y `effective_bone` subia a RARM2/LARM2 -> la mano salia
            # suelta ~2,4 u en las poses que dibujan el draw L00 (golpe/caida).
            eff = np.full(len(Pu), rigido, int)
            slots = np.zeros(len(Pu), int)
        else:
            eff = np.array([ss.effective_bone(int(b)) for b in Bu])
            slots = np.array([ss.slot_of[int(e)] for e in eff], int)
        loc, nl = np.zeros_like(Pu), np.zeros_like(Pu)
        for i in range(len(Pu)):
            m = invw[eff[i]]
            loc[i] = m[:3, :3] @ Pu[i] + m[:3, 3]
            nl[i] = m[:3, :3] @ Nu[i]
        nl /= np.maximum(np.linalg.norm(nl, axis=1), 1e-9)[:, None]
        prepared.append({"host": host, "loc": loc, "nrm": nl, "uv": Uu, "slots": slots,
                         "tris": tris_local, "rgba": pieza["rgba"], "zona": pieza.get("zona", "?"),
                         "placement": pieza.get("placement", {}), "rampa_gris": pieza.get("rampa_gris", False),
                         "tex_destino": tex_dest, "prim_destino": prim_dest, "rigido": rigido,
                         "uv_remap": pieza.get("uv_remap") or {}})

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
    prim_de = {di: dt.draws[di]["prim"] for di in range(len(dt.draws))}
    for p in prepared:
        if p["prim_destino"]:
            prim_de[p["host"]] = int(p["prim_destino"])
    changed = set(removed) | set(tri_new)
    for di in sorted(changed):
        ts = list(kept[di]) + tri_new.get(di, [])
        seqs[di] = stripify(ts) if ts and prim_de[di] == 5 else [v for tri in ts for v in tri]
    add_ib = sum(len(s) for s in seqs.values())
    obj = avb.grow(avb.n + n_nuevas, avb.n_ib + add_ib) if (n_nuevas or add_ib) else avb
    dt2 = DrawTable(obj)
    b = obj.data
    for di, pr in prim_de.items():   # primitivas del anfitrion (5 = tira)
        if pr != dt2.draws[di]["prim"]:
            set32(b, dt2.draws[di]["abs"] + 0x30, pr)
            dt2.draws[di]["prim"] = pr
    offset = 0
    for p in prepared:
        for i in range(len(p["loc"])):
            v = {"pos": tuple(p["loc"][i]), "w": 1.0, "bone": int(p["slots"][i]),
                 "nrm": tuple(p["nrm"][i]), "uv": (float(p["uv"][i][0]), float(p["uv"][i][1]))}
            at = obj.vb0 + destinos[offset + i] * WINDOW
            b[at:at + WINDOW] = AwgVertexBuffer.pack_window(v)
        offset += len(p["loc"])
    # 27/10 DS157: UVs nuevas de ventanas que YA existian (el atlas del anfitrion
    # se rehace para meter la manga/guante: los triangulos que se quedan del draw
    # anfitrion tienen que apuntar al atlas nuevo). No se tocan las ventanas que
    # acaban de escribir las piezas.
    usadas_nuevas = set(destinos)
    for p in prepared:
        for w, uv in p.get("uv_remap", {}).items():
            if int(w) in usadas_nuevas:
                continue
            at = obj.vb0 + int(w) * WINDOW
            struct.pack_into(">2f", b, at + 36, float(uv[0]), float(uv[1]))
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
    escritas = {}
    for p in prepared:
        mi = dt2.draws[p["host"]]["material"]
        mat = dt2.materials[mi]
        tex = mat["tex"] if p["tex_destino"] is None else int(p["tex_destino"])
        if tex != mat["tex"]:
            dt2.set_material(mi, tex=tex)   # el anfitrion pasa a dibujar con la textura nueva
        if tex in escritas:
            wh = escritas[tex]
        else:
            wh = escritas[tex] = _escribir_textura(b, tex, p["rgba"])
        resumen["zonas"][p["zona"]] = {"anfitrion": p["host"], "triangulos": len(p["tris"]), "textura": wh,
                                         "placement": p["placement"],
                                         "tex": tex, "prim": dt2.draws[p["host"]]["prim"]}
        if p.get("rigido") is not None:
            resumen["zonas"][p["zona"]]["marco"] = "%s (group_bone %d)" % (
                labels[int(p["rigido"])] if 0 <= int(p["rigido"]) < len(labels) else "?", int(p["rigido"]))
        # 25/10: prenda de ropa (rampa gris en su personaje) sobre un material con la rampa de la piel (azul, el toon la
        # resta: guantes blancos oscuros) -> la rampa gris mas usada del cuerpo, si ese material solo lo usa la prenda
        usan = [i for i, d in enumerate(dt.draws) if d["material"] == dt.draws[p["host"]]["material"]]
        if p["rampa_gris"] and all(i in hosts or not kept[i] for i in usan):
            m2 = model.__class__(bytes(amb))
            gris = lambda r: r < len(m2.tex) and float(np.ptp(np.asarray(m2.tex[r][..., :3], float), -1).mean()) < 0.06  # noqa: E731
            if not gris(mat["shader"]):
                cuenta = collections.Counter(m["shader"] for m in dt.materials if m["shader"] != 0xFFFFFFFF and gris(m["shader"]))
                if cuenta:
                    ramp = cuenta.most_common(1)[0][0]
                    dt2.set_material(dt2.draws[p["host"]]["material"], ramp=ramp)
                    resumen["zonas"][p["zona"]]["rampa"] = "%d -> %d" % (mat["shader"], ramp)
    return bytes(b), resumen


def injertar_variantes_mano(amb, variantes):
    """27/10 DS157: mete la geometria de la MANO/guante en las variantes de mano (AWG auxiliares).

    Las variantes (GOK_L01_LHAND, ...) son AWG independientes con UNA lista rigida de un hueso;
    el combate elige una u otra segun la pose, por eso el draw de la mano L00 del AWG0 no se
    pinta en pelea (DS155). Cada variante se sustituye entera por la geometria de la variante
    equivalente del donante (misma etiqueta; si no la tiene, la mano L00 del donante del mismo
    lado), con las UV ya remapeadas al atlas comun.

    variantes: lista de dicts {awg, P, N, UV, tris, tex, ramp}. P/N/UV en el marco LOCAL del
    hueso de la mano (el mismo que el AWG; medido: variante y cuerpo comparten marco)."""
    data = bytearray(amb)
    hechas = []
    conservadas = []
    for v in sorted(variantes, key=lambda x: int(x["awg"])):
        if v.get("saltar"):
            # 27/10 DS161: pose que el donante no trae -> la variante se deja NATIVA (mano del
            # heroe, siempre pegada a la muneca). Antes se copiaba la mano L00 del donante y el
            # juego la dibujaba con la pose del hueso de la mano de esa etiqueta: salia suelta
            # (99/447 en reposo). Ver INFORME DS161.
            conservadas.append({"awg": int(v["awg"]), "motivo": v.get("fuente", "sin variante en el donante")})
            continue
        P = np.asarray(v["P"], float)
        N = np.asarray(v["N"], float)
        UV = np.asarray(v["UV"], float)
        tris = np.asarray(v["tris"], int)
        if not len(tris):
            continue
        awg = int(v["awg"])
        avb = AwgVertexBuffer._parse(bytes(data), awg=awg)
        dt = DrawTable(avb)
        if len(dt.draws) != 1:
            raise ValueError("la variante AWG %d no tiene un solo draw (%d)" % (awg, len(dt.draws)))
        n_new = len(P)
        d0 = dt.draws[0]
        if int(d0["prim"]) != 4:
            raise ValueError("la variante AWG %d no es una lista (prim %d)" % (awg, int(d0["prim"])))
        # ojo: en las LISTAS (prim 4) el campo B cuenta TRIANGULOS, no indices.
        b_count = int(len(tris))
        nib_new = int(tris.size)          # capacidad de IB (indices u16)
        if n_new > avb.n or nib_new > avb.n_ib:
            avb = avb.grow(max(n_new, avb.n), max(nib_new, avb.n_ib))
            dt = DrawTable(avb)
            d0 = dt.draws[0]
        # AVISO: se escribe SIEMPRE en avb.data (bytearray del objeto parseado). Antes se
        # escribia en `data` y los set_range/set_material en `avb.data` (una COPIA): la
        # ventana nueva se quedaba con n/A viejos y solo se veian las poses parciales.
        b = avb.data
        bone = be32(b, avb.vb0 + 16) if avb.n else 0   # las variantes nativas usan el slot 0
        for i in range(n_new):
            w = {"pos": tuple(P[i]), "w": 1.0, "bone": int(bone), "nrm": tuple(N[i]),
                 "uv": (float(UV[i, 0]), float(UV[i, 1]))}
            o = avb.vb0 + i * WINDOW
            b[o:o + WINDOW] = AwgVertexBuffer.pack_window(w)
        bstart = int(d0["B"][0])
        for k, idx in enumerate(tris.reshape(-1)):
            struct.pack_into(">H", b, avb.ib_abs + (bstart + k) * 2, int(idx) & 0xFFFF)
        dt.set_range(d0, int(d0["A"][0]), n_new, bstart, b_count)
        mi = int(d0["material"])
        kw = {"tex": int(v["tex"])}
        if v.get("ramp") is not None:
            kw["ramp"] = int(v["ramp"])
        dt.set_material(mi, **kw)
        hechas.append({"awg": awg, "label": d0["label"], "triangulos": int(len(tris)),
                       "ventanas": int(n_new)})
        data = b
    return bytes(data), {"variantes": hechas, "conservadas": conservadas}
