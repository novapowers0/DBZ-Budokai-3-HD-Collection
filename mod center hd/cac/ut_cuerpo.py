#!/usr/bin/env python3
"""CaC con geometría COMPLETA de UT: UV/skin SCNE originales, materiales separados.

Solo se toma de B3 el contrato del esqueleto/animación; ninguna superficie B3 se
incluye en la salida. Construye un #AWO nuevo con el escritor ya usado por SDBH.
Los recursos se leen de la copia de UT del jugador, nunca se distribuyen.

  python ut_cuerpo.py --fuente <carpeta extraída UT> --familia a0 --apariencia 1 \
      --plantilla <AMB B3> --salida <AMB CaC> [--informe <json>]
"""
import argparse
import collections
import json
import os
from pathlib import Path
import struct
import sys

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent), str(HERE.parents[1] / "awo_tools")]
import ut_formato as uf
import ut_textura as utx
import ut_color as uc
import sdbh_model as hd
import amo2awo
from awg_vertex_buffer import AwgVertexBuffer
from skin_slots import SkinSlots
from texture_b3 import encode_dxt3


def huesos(spr):
    ent, = spr.seccion(b"BONE")
    b = spr.bloque(ent["data_offset"], ent["data_size"])
    n, start = struct.unpack_from(">I", b, 4)[0], struct.unpack_from(">I", b, 12)[0]
    out = []
    for i in range(n):
        p = start + i * 0xAC
        idx, nr, nc, cr, _, mw, _ = struct.unpack_from(">7I", b, p)
        if idx != i:
            raise ValueError("ID de hueso UT fuera de orden")
        nombre = b[nr:b.index(b"\0", nr)].decode("utf-8")
        kids = [(struct.unpack_from(">I", b, cr + k * 4)[0] - start) // 0xAC for k in range(nc)]
        world = np.array(struct.unpack_from(">16f", b, mw)).reshape(4, 4).T
        out.append(dict(nombre=nombre, parent=-1, children=kids, world=world))
    for i, bone in enumerate(out):
        for child in bone["children"]:
            out[child]["parent"] = i
    return out


def escenas(spr):
    root, = spr.seccion(b"SCNE")
    nodes, = (c for c in root["hijos"] if c["nombre"] == "[NODES]")
    out, by_vbuf = {}, {}
    shapes = {e["nombre"]: e for e in spr.seccion(b"SHAP")}
    for e in nodes["hijos"]:
        b = spr.bloque(e["data_offset"], e["data_size"])
        flags, ty, res, _, parent = struct.unpack_from(">5I", b)
        info = dict(flags=flags, tipo=spr.cstr(ty), recurso=spr.cstr(res), parent=spr.cstr(parent))
        for mat in e["hijos"]:
            if mat["nombre"] != "[MATERIAL]":
                continue
            mb = spr.bloque(mat["data_offset"], mat["data_size"])
            mr, _, count = struct.unpack_from(">3I", mb)
            info["material"] = spr.cstr(mr)
            info["canales"] = {spr.cstr(a): spr.cstr(b) for a, b, _ in
                               (struct.unpack_from(">3I", mb, 12 + k * 12) for k in range(count))}
        out[e["nombre"]] = info
    for name, info in out.items():
        if info["tipo"] != "mesh":
            continue
        chain, cur = [], name
        while cur:
            if cur in [n for n, _ in chain]:
                raise ValueError("ciclo SCNE")
            chain.append((cur, out[cur]))
            cur = out[cur]["parent"]
        shape = next((x for _, x in chain if x["tipo"] == "shape"), None)
        sf = 1
        if shape:
            for c in shapes[shape["recurso"]]["hijos"]:
                if c["nombre"] == "DbzShapeInfo":
                    sf = struct.unpack_from(">I", spr.bloque(c["data_offset"], c["data_size"]))[0]
        neutro = not any(t in name for t in ("|BEFORE|", "|AFTER_", "|scouter_on|", "|equipment01|", "|face_anim|"))
        info["activo"] = neutro and bool(sf & 1) and not any(x["flags"] & 2 for _, x in chain)
        info["node"] = name
        by_vbuf[info["recurso"]] = info
    return by_vbuf


def fuente(familia, apariencia, carpeta=None, juego=None):
    if familia not in ("a0", "a1", "b0", "b1", "c0", "c1") or not 1 <= apariencia <= 17:
        raise ValueError("cuerpo UT fuera de catálogo")
    roots = [carpeta, os.environ.get("DBZ3_CAC_UT"),
             str(Path(juego) / "cac_piezas" / "fuente") if juego else None,
             r"E:\DBZ3HD_trabajo\ut\extraido"]
    for root in roots:
        if not root:
            continue
        matches = sorted(Path(root).glob("*_avatar_%s_%02dp_X360.spr" % (familia, apariencia)))
        if matches:
            return matches[0]
    raise FileNotFoundError("faltan los recursos originales UT; indicar DBZ3_CAC_UT o --fuente")


def remap_bones(ut, tpl):
    suffix = {hd.bone_suffix(l): i for i, l in enumerate(tpl.labels)}
    exact = {"NULL": "WAIST", "RESERVE": "WAIST", "WAIST": "WAIST", "SPINE1": "STMC",
             "SPINE2": "CHEST", "SPINE3": "CHEST", "NECK": "NECK", "HEAD": "HEAD", "FACE": "HEAD"}
    for side in "LR":
        for src, dst in (("CLAVICLE", "CHN"), ("SHOULDER", "ARM1"), ("ELBOW", "ARM2"),
                         ("ELBOWROLL", "ARM2"), ("WRIST", "HANDROT"), ("HIP", "LEGROT"),
                         ("THIGH", "LEG1"), ("CLANK", "LEG2"), ("TOE1", "FOOT1"), ("TOE2", "FOOT2")):
            exact[src + "_" + side] = side + dst
    mapped = {}
    for i, b in enumerate(ut):
        cur = i
        while cur >= 0:
            nm = ut[cur]["nombre"]
            if nm in exact and exact[nm] in suffix:
                mapped[i] = suffix[exact[nm]]
                break
            cur = ut[cur]["parent"]
        else:
            raise ValueError("hueso UT sin equivalente: " + b["nombre"])
    return mapped


def construir(plantilla, spr_path, salida, colores=None, cap=256, informe=None, pelo=0):
    spr_path = Path(spr_path)
    spr = uf.cargar(str(spr_path), str(spr_path.with_suffix(".ioram")))
    vram = spr_path.with_suffix(".vram").read_bytes()
    ut = huesos(spr)
    scene = escenas(spr)
    material_info = uc.mapa_materiales(spr)
    hsv_path = str(spr_path).replace("_X360.spr", "_hsv_001.dat")
    hsv = uc.leer_hsv(hsv_path)
    tpl = hd.Template(str(plantilla))
    W = tpl.world()
    inv = [np.linalg.inv(m) for m in W]
    mapping = remap_bones(ut, tpl)
    # UT L = -X, B3 L = +X. Giro 180° con determinante +1, sin invertir winding.
    R = np.diag([-1.0, 1.0, -1.0])
    by_name = {b["nombre"]: i for i, b in enumerate(ut)}
    waist, head = by_name["WAIST"], by_name["HEAD"]
    scale = (np.linalg.norm(W[mapping[head]][:3, 3] - W[mapping[waist]][:3, 3]) /
             np.linalg.norm(ut[head]["world"][:3, 3] - ut[waist]["world"][:3, 3]))
    slots = {b: s for s, b in enumerate(tpl.palette) if s > 0}
    def effective(b):
        while b not in slots and tpl.parent[b] >= 0:
            b = tpl.parent[b]
        if b not in slots:
            raise ValueError("hueso B3 sin slot")
        return b
    items, mats, parts = [], [], []
    stats = dict(fuente=str(spr_path), escala=float(scale), geometria_b3=0,
                 uv_originales=True, grupos=[], reduccion_skin="influencia dominante por vértice")
    tex_names = {t["nombre"] for t in uf.texturas(spr)}
    # Rampa negra (como las nativas de B3). La rampa gris 75->0 apagaba el color y
    # convertia el oro de las armaduras en oliva.
    ramp = np.zeros((64, 64, 4), np.uint8)
    ramp[..., 3] = 255
    items.append((64, 64, 0x80000001, hd.dds_dxt3(64, 64) + encode_dxt3(ramp)))
    cache_tex = {}
    packed_by_material = collections.defaultdict(list)
    sources = [(g, spr, vram, ut, scene.get(g.nombre), material_info, hsv, False) for g in uf.grupos(spr)]
    if pelo > 0:
        names = ("000_01", "000_02", "002_01", "003_01", "004_01", "005_01", "006_01", "007_01", "008_01")
        hp = spr_path.parent / ("avatar_hair_%s_X360.spr" % names[pelo - 1])
        hs = uf.cargar(str(hp), str(hp.with_suffix(".ioram")))
        hv, hb, hsc, hm = hp.with_suffix(".vram").read_bytes(), huesos(hs), escenas(hs), uc.mapa_materiales(hs)
        # El .spr del pelo es un rig LOCAL: su HEAD esta en el origen, no en la
        # cabeza del cuerpo. Restarle el origen de cada hueso lo dejaba sobre el
        # pecho (y a escala de torso). Se coloca como pieza del hueso HEAD.
        sources.extend((g, hs, hv, hb, hsc.get(g.nombre), hm, {}, True) for g in uf.grupos(hs))
    for g, spr, vram, ut, sc, material_info, hsv, es_local in sources:
        if not sc or not sc["activo"]:
            continue
        mat = sc["material"]
        if "H_HAIR_SHADOW" in mat:
            continue   # sombra proyectada de UT: no es superficie visible del pelo
        mapped = {u: effective(b) for u, b in remap_bones(ut, tpl).items()}
        tex_names = {t["nombre"] for t in uf.texturas(spr)}
        tex = material_info.get(mat, {})
        slot = next((s for s in ("COLORMAP0", "COLORMAP", "COLORMAP1") if tex.get(s) in tex_names), None)
        if slot is None:
            raise ValueError("material sin textura original: " + mat)
        uv_name = sc["canales"].get(slot, "map1")
        uv_decl = next((d for d in g.decls if d["usage"] == 5 and d["nombre"] == uv_name), None)
        if uv_decl is None:
            raise ValueError("canal UV no resuelto: " + g.nombre)
        img = utx.extraer(spr, vram, tex[slot])
        if img is None:
            raise ValueError("textura UT no soportada: " + tex[slot])
        zona = uc.zona_de(mat)
        # C_ARM_LEG agrupa mangas Y perneras: la zona depende del hueso dominante,
        # no del nombre (antes las mangas se teñian con el color de abajo).
        if mat.upper().startswith("C_ARM_LEG"):
            arm = any("ARM" in ut[bi]["nombre"].upper()
                      for inf in g.influencias() for bi, _w in inf)
            zona = "arriba" if arm else "abajo"
        color = (colores or {}).get(zona)
        if zona == "pelo" and color is None:
            color = (0.16, 0.11, 0.08)   # pelo por defecto oscuro: la textura UT es verde-oliva
        # El pelo, las cejas y la ropa usan el color de su zona. Ojos, logo,
        # panel y accesorios conservan su textura original (no se tiñen).
        no_tint = mat.upper().startswith(("A_EYE", "E_EYE", "T_", "LOGO", "PANEL", "G_SCOUTER"))
        img = uc.tintar_textura(img, None if no_tint else hsv.get(mat), None if no_tint else color)
        tex_key = (mat, tex[slot])
        if tex_key not in cache_tex:
            w, h = img.size
            ratio = min(1.0, cap / max(w, h))
            w, h = max(4, int(w * ratio) // 4 * 4), max(4, int(h * ratio) // 4 * 4)
            a = np.array(img.resize((w, h), Image.Resampling.LANCZOS))
            a[..., 3] = 0
            ti = len(items)
            items.append((w, h, 0x21, hd.dds_dxt3(w, h) + encode_dxt3(a)))
            cache_tex[tex_key] = len(mats)
            mats.append(hd.material(True, ti, 0))
        mi = cache_tex[tex_key]
        rows = g.vertices()
        skin = g.influencias()
        verts, pts = [], []
        max_bone = 0
        for i, row in enumerate(rows):
            p, normal = np.array(row[:3]), np.array(row[3:6])
            influences = skin[i]
            if not influences:
                raise ValueError("vértice UT sin skin: " + g.nombre)
            # Reposicionar los pivotes al contrato del motor; no se toma geometría B3.
            q = np.zeros(3)
            total = sum(w for _, w in influences)
            weights = collections.defaultdict(float)
            for bi, weight in influences:
                b = mapped[bi]
                if es_local:
                    # Rig local (pelo): p ya esta en el espacio del hueso HEAD.
                    q += (weight / total) * (p @ R.T * scale + W[b][:3, 3])
                else:
                    q += (weight / total) * ((p - ut[bi]["world"][:3, 3]) @ R.T * scale + W[b][:3, 3])
                weights[b] += weight
            bone = max(weights, key=weights.get)
            local_pos = inv[bone][:3, :3] @ q + inv[bone][:3, 3]
            n = inv[bone][:3, :3] @ R @ normal
            n /= max(np.linalg.norm(n), 1e-12)
            uv = struct.unpack_from(">2f", g.vertex_data, uv_decl["offset"] + i * uv_decl["stride"])
            verts.append(amo2awo.win_bytes(local_pos.tolist(), 1.0, slots[bone], n.tolist(), None, list(uv)))
            pts.append(q)
            max_bone = max(max_bone, bone)
        tris = g.triangulos()
        packed_by_material[mi].append((verts, tris, np.array(pts), max_bone))
        stats["grupos"].append(dict(nombre=g.nombre, material=mat, zona=zona, textura=tex[slot],
                                    canal=uv_name, vertices=len(verts), triangulos=len(tris)))
    groups = {0: []}
    for mi, subs in sorted(packed_by_material.items()):
        win, tris, pts, nmax = hd._merge(subs)
        groups[0].append(dict(skinned=True, mat=mi, win=win, tris=tris, pts=pts,
                              label=tpl.labels[0], nmax=nmax))
    raw, axes, metrics = hd.awg_bytes(tpl.names, tpl.axes, tpl.links, tpl.word0c, 0, groups, mats, tpl.palette)
    lists = [[(0, i, i)] * tpl.nlist for i in range(tpl.nb)]
    awo = hd.awo_bytes(tpl, [(raw, axes)], lists, tpl.nlist)
    azt = hd.build_azt(items)
    data = hd.amb_pack([(awo, tpl.kid_types.get(b"#AWO", 1)), (azt, tpl.kid_types.get(b"#AZT", 2))])
    Path(salida).write_bytes(data)
    errors = hd.verify(str(salida))
    stats.update(metrics, materiales=len(mats), texturas=len(items), bytes=len(data), azt=len(azt), errores=errors)
    if informe:
        Path(informe).write_text(json.dumps(stats, ensure_ascii=False, indent=2), encoding="utf-8")
    if errors:
        raise ValueError("UT convertido no válido: " + "; ".join(errors))
    return data, stats


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--fuente")
    p.add_argument("--familia", default="a0")
    p.add_argument("--apariencia", type=int, default=1)
    p.add_argument("--plantilla", required=True)
    p.add_argument("--salida", required=True)
    p.add_argument("--informe")
    p.add_argument("--cap", type=int, default=256)
    p.add_argument("--pelo", type=int, choices=range(10), default=0)
    a = p.parse_args()
    _, info = construir(a.plantilla, fuente(a.familia, a.apariencia, a.fuente), a.salida,
                         cap=a.cap, informe=a.informe, pelo=a.pelo)
    print(json.dumps({k: v for k, v in info.items() if k != "grupos"}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
