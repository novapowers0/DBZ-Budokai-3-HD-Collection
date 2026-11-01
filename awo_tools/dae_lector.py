#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Lector COLLADA (.dae) sin dependencias nuevas: entrega esqueleto y mallas con el mismo formato
que dan xeno_model.Esk / leer_emd, para reutilizar xeno_a_b3 (Retarget, pose_hand, build_model).

Solo lee: ningun fichero se modifica. No lleva arte. Pensado para rigs con nombres Xenoverse
(b_C_Base, b_L_Arm1, h_L_Index1...) como los de "Legends"; el mapa a B3 sale de xeno_rig.ALIASES.
El marco de salida es el de B3: Y arriba, la cara hacia +Z, izquierda +X, escala x10.
"""
import copy
import xml.etree.ElementTree as ET

import numpy as np

SCALE = 10.0


def _t(e):
    return e.tag.split("}")[-1]


def _hijos(e, nombre):
    return [c for c in e if _t(c) == nombre]


def _floats(e):
    return np.array(e.text.split(), float) if e is not None and e.text else np.zeros(0)


class DaeEsk:
    """Mismos campos que xeno_model.Esk que usa xeno_a_b3."""

    def __init__(self, names, parent, world_dae, C, ibm_error=None):
        self.names, self.parent = names, parent
        self.index = {n: i for i, n in enumerate(names)}
        Ci = np.linalg.inv(C)
        self.world_matrix = [C @ w @ Ci for w in world_dae]
        self.world = [m[:3, 3].copy() for m in self.world_matrix]
        self.inverse_bind_error = ibm_error


class Sub:
    pass


def _nodos(root):
    """JOINT del visual_scene en orden de documento: (nombre, padre, matriz local, mundo)."""
    names, parent, world = [], [], []

    def visita(n, p, W):
        M = np.array(_floats(_hijos(n, "matrix")[0]) if _hijos(n, "matrix") else np.eye(4).ravel()).reshape(4, 4)
        W2 = W @ M
        if n.get("type") == "JOINT":
            names.append(n.get("name") or n.get("id"))
            parent.append(p)
            world.append(W2)
            p = len(names) - 1
        for c in _hijos(n, "node"):
            visita(c, p, W2)

    vs = next(e for e in root.iter() if _t(e) == "visual_scene")
    for n in _hijos(vs, "node"):
        visita(n, -1, np.eye(4))
    return names, parent, world


def _marco(names, world):
    """Matriz 4x4 DAE -> B3 (con escala). Mide: arriba = cabeza-pies, izquierda = mano izq., adelante = punta del pie."""
    p = lambda n: world[names.index(n)][:3, 3]
    arriba = p("b_C_Head") - p("b_C_Base")
    izq = p("b_L_Hand") - p("b_R_Hand")
    adelante = p("b_L_Toe") - p("b_L_Foot")
    f = lambda v: int(np.argmax(np.abs(v)))
    ia, ii = f(arriba), f(izq)
    adelante = adelante.copy()
    adelante[[ia, ii]] = 0
    iad = f(adelante)
    if len({ia, ii, iad}) != 3:
        raise ValueError("DAE: no se pueden fijar los ejes del rig")
    R = np.zeros((3, 3))
    R[0, ii] = np.sign(izq[ii])
    R[1, ia] = np.sign(arriba[ia])
    R[2, iad] = np.sign(adelante[iad])
    C = np.eye(4)
    C[:3, :3] = R * SCALE
    return C, R


def _malla(g, ctrl, joints, R, C):
    mesh = _hijos(g, "mesh")[0]
    srcs = {s.get("id"): s for s in _hijos(mesh, "source")}
    vtx = _hijos(mesh, "vertices")[0]
    pos_src = next(srcs[i.get("source")[1:]] for i in _hijos(vtx, "input") if i.get("semantic") == "POSITION")
    stride = lambda s: int(_hijos(_hijos(s, "technique_common")[0], "accessor")[0].get("stride"))
    P = _floats(_hijos(pos_src, "float_array")[0]).reshape(-1, stride(pos_src))
    out = []
    for tri in mesh:
        if _t(tri) not in ("triangles", "polylist"):
            continue
        ins = _hijos(tri, "input")
        ofs = {i.get("semantic"): int(i.get("offset")) for i in ins}
        n_in = max(ofs.values()) + 1
        arr = {}
        for i in ins:
            if i.get("semantic") in ("NORMAL", "TEXCOORD"):
                s = srcs[i.get("source")[1:]]
                arr[i.get("semantic")] = _floats(_hijos(s, "float_array")[0]).reshape(-1, stride(s))
        p = np.array(_hijos(tri, "p")[0].text.split(), int).reshape(-1, n_in)
        if _t(tri) == "polylist":      # solo triangulos (Blender los exporta asi); lo demas se rechaza
            if any(int(v) != 3 for v in _hijos(tri, "vcount")[0].text.split()):
                raise ValueError("DAE: polylist con poligonos no triangulares")
        out.append((tri.get("material"), p, ofs, arr))
    # Pesos: por indice de posicion del XML.
    skin = _hijos(ctrl, "skin")[0]
    ssrc = {s.get("id"): s for s in _hijos(skin, "source")}
    vw = _hijos(skin, "vertex_weights")[0]
    wsrc = next(ssrc[i.get("source")[1:]] for i in _hijos(vw, "input") if i.get("semantic") == "WEIGHT")
    W = _floats(_hijos(wsrc, "float_array")[0])
    jsrc = next(ssrc[i.get("source")[1:]] for i in _hijos(_hijos(skin, "joints")[0], "input") if i.get("semantic") == "JOINT")
    jn = _hijos(jsrc, "Name_array")[0].text.split()
    bsm = _floats(_hijos(skin, "bind_shape_matrix")[0]).reshape(4, 4) if _hijos(skin, "bind_shape_matrix") else np.eye(4)
    vc = np.array(_hijos(vw, "vcount")[0].text.split(), int)
    v = np.array(_hijos(vw, "v")[0].text.split(), int).reshape(-1, 2)
    jofs, wofs = [int(i.get("offset")) for i in _hijos(vw, "input") if i.get("semantic") in ("JOINT", "WEIGHT")][:2]
    start = np.r_[0, np.cumsum(vc)]
    nv = len(vc)
    B, BW = np.zeros((nv, 4), int), np.zeros((nv, 4))
    for k in range(nv):
        rows = v[start[k]:start[k + 1]]
        ws = W[rows[:, 1]]
        top = np.argsort(-ws)[:4]
        sel = ws[top]
        if sel.sum() <= 0:
            continue
        B[k, :len(top)] = rows[top, 0]
        BW[k, :len(top)] = sel / sel.sum()
    Pm = (P[:, :3] @ bsm[:3, :3].T + bsm[:3, 3])
    return Pm, out, jn, B, BW


def leer_dae(ruta):
    """-> (esk, subs). Cada sub: name, material, pos, nrm, uv, bones, weights, bone_names, tris (formato leer_emd)."""
    root = ET.parse(ruta).getroot()
    names, parent, world = _nodos(root)
    C, R = _marco(names, world)
    esk = DaeEsk(names, parent, world, C)
    geoms = {g.get("id"): g for g in root.iter() if _t(g) == "geometry"}
    subs, err = [], 0.0
    for ctrl in (c for c in root.iter() if _t(c) == "controller"):
        g = geoms[_hijos(ctrl, "skin")[0].get("source")[1:]]
        Pm, partes, jn, B, BW = _malla(g, ctrl, names, R, C)
        # Coherencia bind: mundo(hueso) * inversaBind = identidad
        skin = _hijos(ctrl, "skin")[0]
        isrc = next(s for s in _hijos(skin, "source") if _hijos(s, "float_array") and len(_floats(_hijos(s, "float_array")[0])) == 16 * len(jn))
        ibm = _floats(_hijos(isrc, "float_array")[0]).reshape(-1, 4, 4)
        bsm = _floats(_hijos(skin, "bind_shape_matrix")[0]).reshape(4, 4)
        for j, n in enumerate(jn):
            if n in esk.index:
                err = max(err, float(np.abs(world[esk.index[n]] @ ibm[j] @ bsm - np.eye(4)).max()))
        for mat, p, ofs, arr in partes:
            keys = p[:, [ofs["VERTEX"]] + ([ofs["NORMAL"]] if "NORMAL" in arr else []) + ([ofs["TEXCOORD"]] if "TEXCOORD" in arr else [])]
            uniq, inv = np.unique(keys, axis=0, return_inverse=True)
            inv = inv.reshape(-1)
            sm = Sub()
            sm.name, sm.material, sm.geom = g.get("name"), mat, g.get("name")
            sm.pos = (Pm[uniq[:, 0]] @ R.T) * SCALE
            col = 1
            sm.nrm = (arr["NORMAL"][uniq[:, col]][:, :3] @ R.T) if "NORMAL" in arr else np.zeros((len(uniq), 3))
            col += "NORMAL" in arr
            uv = arr["TEXCOORD"][uniq[:, col]][:, :2] if "TEXCOORD" in arr else np.zeros((len(uniq), 2))
            sm.uv = np.c_[uv[:, 0], 1 - uv[:, 1]]      # DAE: v hacia arriba; DDS/B3: v hacia abajo
            sm.bone_names, sm.bones, sm.weights = list(jn), B[uniq[:, 0]], BW[uniq[:, 0]]
            tris = inv.reshape(-1, 3)
            sm.tris = tris[:, [0, 2, 1]] if np.linalg.det(R) < 0 else tris
            subs.append(sm)
    esk.inverse_bind_error = err
    return esk, subs


if __name__ == "__main__":
    import sys
    e, ss = leer_dae(sys.argv[1])
    print("huesos", len(e.names), "error bind %.2g" % e.inverse_bind_error)
    for s in ss:
        print("%-24s %-6s v=%5d t=%5d alto=%.2f..%.2f" % (s.name, s.material, len(s.pos), len(s.tris), s.pos[:, 1].min(), s.pos[:, 1].max()))
