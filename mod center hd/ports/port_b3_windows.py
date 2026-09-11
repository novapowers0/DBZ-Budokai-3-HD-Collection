#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""port_b3_windows.py - Vía B: porta una geometría PS2 a un bin HD B3 usando el
modelo REAL del vertex buffer (ventanas de 44 B + IB), no el modelo "sec34/A-B".

Convierte el `extract.json` de un modelo PS2 (port_ps2_b3_extract.py) al pool de
ventanas + IB de la plantilla HD, conservando la plantilla (ejes/arms/mesh-ref/
descriptores). Mapea huesos PS2->HD por LABEL.

Semántica (verificada 2026-09-12, `awo_tools/awg_vertex_buffer.py`):
  ventana.pos = inv(world[hd_bone])·model        (bone-local, orden natural)
  ventana.nrm = inv(world[hd_bone]).R · model_nrm
  ventana.bone = hd_bone ; ventana.w = peso PS2 ; +32 = FFFFFFFF ; uv directo
  IB = lista de triángulos (prim=4, kTriangleList), índices de ventana.

Uso:
  python port_b3_windows.py <ps2_extract.json> <plantilla.bin> <salida.amb>
      [--fit]       # decima (cluster) para caber en la plantilla (sin grow)
      [--no-grow]   # falla si no cabe
"""
import os
import sys
import json
import math
import struct

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "awo_tools"))
from awg_vertex_buffer import AwgVertexBuffer, minv_affine, mvec  # noqa: E402


def build_bone_map(ps2_labels, hd_labels):
    idx = {}
    for i, lab in enumerate(hd_labels):
        idx.setdefault(lab, i)
    m, miss = {}, []
    for i, lab in enumerate(ps2_labels):
        if lab in idx:
            m[i] = idx[lab]
        else:
            miss.append((i, lab))
    return m, miss


def load_model(ex, bmap):
    skin = {int(k): v for k, v in ex["skin"].items()}
    vmap, V = {}, []
    part_loc = []
    for p in ex["parts"]:
        pb = p.get("bone", 0)
        loc = []
        for v in p["verts"]:
            oa = v[0]
            gi = vmap.get(oa)
            if gi is None:
                sv = skin.get(oa)
                ps2b = int(sv[0]) if sv else int(pb)
                if sv:
                    weight = struct.unpack("<f", struct.pack("<I", int(sv[1]) & 0xFFFFFFFF))[0]
                else:
                    weight = 1.0
                hb = bmap.get(ps2b, bmap.get(int(pb), 0))
                gi = len(V)
                vmap[oa] = gi
                V.append({"pos": (v[1], v[2], v[3]), "nrm": (v[4], v[5], v[6]),
                          "uv": (v[7], v[8]), "bone": hb, "w": weight})
            loc.append(gi)
        part_loc.append(loc)
    tris = []
    for pi, p in enumerate(ex["parts"]):
        loc = part_loc[pi]
        for tri in p["tris"]:
            try:
                tris.append((loc[tri[0]], loc[tri[1]], loc[tri[2]]))
            except IndexError:
                pass
    return V, tris


def _closest_on_tri(p, a, b, c):
    """Punto mas cercano de p al triangulo (a,b,c) + baricentricas (u,v,w)."""
    import numpy as _np
    ab = b - a; ac = c - a; ap = p - a
    d1 = ab.dot(ap); d2 = ac.dot(ap)
    if d1 <= 0 and d2 <= 0:
        return a, (1.0, 0.0, 0.0)
    bp = p - b; d3 = ab.dot(bp); d4 = ac.dot(bp)
    if d3 >= 0 and d4 <= d3:
        return b, (0.0, 1.0, 0.0)
    vc = d1 * d4 - d3 * d2
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1 / (d1 - d3) if (d1 - d3) else 0.0
        return a + ab * v, (1.0 - v, v, 0.0)
    cp = p - c; d5 = ab.dot(cp); d6 = ac.dot(cp)
    if d6 >= 0 and d5 <= d6:
        return c, (0.0, 0.0, 1.0)
    vb = d5 * d2 - d1 * d6
    if vb <= 0 and d2 >= 0 and d6 <= 0:
        w = d2 / (d2 - d6) if (d2 - d6) else 0.0
        return a + ac * w, (1.0 - w, 0.0, w)
    va = d3 * d6 - d5 * d4
    if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
        den = (d4 - d3) + (d5 - d6)
        w = (d4 - d3) / den if den else 0.0
        return b + (c - b) * w, (0.0, 1.0 - w, w)
    den = va + vb + vc
    if abs(den) < 1e-20:
        return a, (1.0, 0.0, 0.0)
    v = vb / den; w = vc / den
    return a + ab * v + ac * w, (1.0 - v - w, v, w)


def transfer_skin_surface(V, tpl, world, force_w=None):
    """Asigna hueso (y peso) a cada vertice del port por el punto mas cercano de
    la SUPERFICIE de la plantilla HD (triangulos en model-space), no por vertice
    mas cercano. El hueso = el vertice de mayor baricentrica del triangulo. Si
    force_w no es None, se fija ese peso (p.ej. 1.0)."""
    import numpy as np
    from scipy.spatial import cKDTree

    tl = tpl.vertices()
    n_hd = len(tl)
    hd = np.zeros((n_hd, 3)); hb = np.zeros(n_hd, int); hw = np.zeros(n_hd)
    for i, w in enumerate(tl):
        m = mvec(world[w["bone"]], [w["pos"][0], w["pos"][1], w["pos"][2], 1.0])
        hd[i] = m[:3]; hb[i] = w["bone"]; hw[i] = w["w"]

    ib = tpl.indices()
    nv = len(ib)
    for k, x in enumerate(ib):
        if x == 0xFFFF or x >= n_hd:
            nv = k; break
    ib = ib[:nv - (nv % 3)]
    tris = [(ib[i], ib[i + 1], ib[i + 2]) for i in range(0, len(ib) - 2, 3)]
    tris = [t for t in tris if max(t) < n_hd]
    if not tris:
        print("[!] surface-skin: plantilla sin triangulos utilizables")
        return
    cen = np.array([(hd[a] + hd[b] + hd[c]) / 3.0 for a, b, c in tris])
    tree = cKDTree(cen)

    P = np.array([v["pos"] for v in V])
    kq = min(12, len(tris))
    _, cand = tree.query(P, k=kq)
    if kq == 1:
        cand = cand[:, None]
    moved = 0
    for i, v in enumerate(V):
        p = P[i]
        best_d = 1e18; best_bone = v["bone"]; best_w = v["w"]
        for ci in cand[i]:
            a, b, c = tris[ci]
            q, (ua, ub, uc) = _closest_on_tri(p, hd[a], hd[b], hd[c])
            d = float(((q - p) ** 2).sum())
            if d < best_d:
                best_d = d
                j = (a, b, c)[int(np.argmax((ua, ub, uc)))]
                best_bone = hb[j]; best_w = hw[j]
        if v["bone"] != best_bone:
            moved += 1
        v["bone"] = best_bone
        v["w"] = float(force_w) if force_w is not None else best_w
        v["_sd"] = best_d ** 0.5
    dss = np.array([v.get("_sd", 0.0) for v in V])
    print("[*] surface-skin: huesos re-asignados=%d/%d  dist media=%.3f max=%.3f"
          % (moved, len(V), dss.mean(), dss.max()))


def cluster_fit(V, tris, target_n, target_tris):
    cell = 0.02
    for _ in range(40):
        key2i, V2, remap = {}, [], {}
        for i, v in enumerate(V):
            k = (v["bone"],
                 int(math.floor(v["pos"][0] / cell)),
                 int(math.floor(v["pos"][1] / cell)),
                 int(math.floor(v["pos"][2] / cell)),
                 int(round(v["uv"][0] * 16)), int(round(v["uv"][1] * 16)))
            j = key2i.get(k)
            if j is None:
                j = len(V2)
                key2i[k] = j
                V2.append({"pos": v["pos"], "nrm": v["nrm"], "uv": v["uv"],
                           "bone": v["bone"], "w": v["w"], "_n": 1})
            else:
                a = V2[j]
                a["_n"] += 1
                for f in ("pos", "nrm", "uv"):
                    n = 3 if f in ("pos", "nrm") else 2
                    a[f] = tuple((a[f][t] * (a["_n"] - 1) + v[f][t]) / a["_n"]
                                 for t in range(n))
            remap[i] = j
        T2 = []
        for a, b, c in tris:
            ra, rb, rc = remap[a], remap[b], remap[c]
            if ra != rb and rb != rc and ra != rc:
                T2.append((ra, rb, rc))
        if len(V2) <= target_n and len(T2) <= target_tris:
            break
        cell *= 1.35
    if len(T2) > target_tris:
        def area(t):
            a, b, c = t
            pa, pb, pc = V2[a]["pos"], V2[b]["pos"], V2[c]["pos"]
            u = [pb[k] - pa[k] for k in range(3)]
            w = [pc[k] - pa[k] for k in range(3)]
            cr = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2],
                  u[0] * w[1] - u[1] * w[0])
            return cr[0] ** 2 + cr[1] ** 2 + cr[2] ** 2
        T2.sort(key=area, reverse=True)
        T2 = T2[:target_tris]
    for v in V2:
        v.pop("_n", None)
    return V2, T2


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) < 3:
        print(__doc__)
        return 1
    fit = "--fit" in sys.argv
    no_grow = "--no-grow" in sys.argv or fit
    ex = json.load(open(args[0]))
    tpl = AwgVertexBuffer.load(args[1])
    bmap, miss = build_bone_map(ex["labels"], tpl.bone_labels())
    print("huesos PS2=%d HD=%d mapeados=%d sin_map=%d"
          % (len(ex["labels"]), tpl.bone_count(), len(bmap), len(miss)))
    if miss:
        print("  [!] sin mapear:", miss[:8])

    V, tris = load_model(ex, bmap)
    print("PS2: verts=%d tris=%d | plantilla: N=%d n_ib=%d (tris~%d)"
          % (len(V), len(tris), tpl.n, tpl.n_ib, tpl.n_ib // 3))

    need = len(V) > tpl.n or len(tris) * 3 > tpl.n_ib
    if need and fit:
        V, tris = cluster_fit(V, tris, tpl.n, tpl.n_ib // 3)
        print("[*] fit: verts=%d tris=%d" % (len(V), len(tris)))
    elif need and no_grow:
        print("[X] no cabe y --no-grow: usa --fit o quita --no-grow")
        return 2

    world, _ = tpl.bind_worlds()
    invw = [minv_affine(w) for w in world]

    if "--hd-skin" in sys.argv:
        # Transferir el skin de la plantilla HD a la geometria del port (hueso y
        # peso del vertice HD mas cercano en model-space). Asi la animacion HD
        # (autorada para el rig HD) casa con la topologia PS2.
        tl = tpl.vertices()
        tpts = []
        for w in tl:
            m = mvec(world[w["bone"]], [w["pos"][0], w["pos"][1], w["pos"][2], 1.0])
            tpts.append(((m[0], m[1], m[2]), w["bone"], w["w"]))
        cell = 0.5
        grid = {}
        for pt, b, ww in tpts:
            grid.setdefault((int(pt[0] // cell), int(pt[1] // cell),
                             int(pt[2] // cell)), []).append((pt, b, ww))
        moved = 0
        for v in V:
            p = v["pos"]
            ki = (int(p[0] // cell), int(p[1] // cell), int(p[2] // cell))
            best = None
            bd = 1e18
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        for q in grid.get((ki[0] + dx, ki[1] + dy, ki[2] + dz), []):
                            d = (p[0] - q[0][0]) ** 2 + (p[1] - q[0][1]) ** 2 + \
                                (p[2] - q[0][2]) ** 2
                            if d < bd:
                                bd = d
                                best = q
            if best is not None:
                if v["bone"] != best[1]:
                    moved += 1
                v["bone"] = best[1]
                v["w"] = best[2]
        print("[*] hd-skin: huesos re-asignados=%d/%d" % (moved, len(V)))

    if "--surface-skin" in sys.argv:
        fw = None
        for a in sys.argv:
            if a.startswith("--surface-skin="):
                fw = float(a.split("=", 1)[1])
        transfer_skin_surface(V, tpl, world, force_w=fw)

    windows = [AwgVertexBuffer.window_from_model(v["pos"], v["nrm"], v["bone"],
                                                 v["w"], v["uv"], invw[v["bone"]])
               for v in V]
    ib = [i for t in tris for i in t]

    obj = tpl
    if len(windows) > tpl.n or len(ib) > tpl.n_ib:
        obj = tpl.grow(max(len(windows), tpl.n), max(len(ib), tpl.n_ib))
        print("[*] grow: N %d->%d n_ib %d->%d (EXPERIMENTAL, validar en juego)"
              % (tpl.n, obj.n, tpl.n_ib, obj.n_ib))

    obj.emit(args[2], vertices=windows, indices=ib)
    print("OK -> %s (ventanas=%d, IB=%d)" % (args[2], len(windows), len(ib)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
