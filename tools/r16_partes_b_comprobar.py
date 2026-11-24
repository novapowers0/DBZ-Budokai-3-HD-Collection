#!/usr/bin/env python3
"""Comprueba topología, orientación, anclajes y continuidad de recetas propias.

Estas pruebas offline NO sustituyen la aceptación visual/combate del encargo.
"""
from collections import Counter, defaultdict
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "mod center hd" / "cac"))
import partes_namek_majin as piezas


def comprobar_malla(pieza):
    p, n, t = pieza["P"], pieza["N"], pieza["tris"]
    assert np.isfinite(p).all() and np.isfinite(n).all()
    assert p.shape == n.shape and p.shape[1] == 3
    assert len(pieza["huesos"]) == len(pieza["pesos"]) == len(pieza["regiones"]) == len(p)
    for h, peso, region in zip(pieza["huesos"], pieza["pesos"], pieza["regiones"]):
        assert peso == [(h, 1.0)]
        assert region in pieza["colores"]
    if not len(t):
        assert pieza["tipo"] == "cuerpo" and pieza["deformacion"] == pieza["clave"]
        return 0
    assert t.min() >= 0 and t.max() < len(p)
    assert np.allclose(np.linalg.norm(n, axis=1), 1)
    edges = Counter()
    directed = Counter()
    adj = defaultdict(set)
    for tri in t:
        assert len(set(tri)) == 3
        for a, b in zip(tri, np.roll(tri, -1)):
            edges[tuple(sorted((int(a), int(b))))] += 1
            directed[int(a), int(b)] += 1
            adj[int(a)].add(int(b))
            adj[int(b)].add(int(a))
    assert set(edges.values()) == {2}, "superficie abierta o no-manifold"
    assert all(v == directed[b, a] for (a, b), v in directed.items()), "winding inconsistente"
    componentes = []
    unseen = set(range(len(p)))
    while unseen:
        seed = unseen.pop()
        component, stack = {seed}, [seed]
        while stack:
            for b in adj[stack.pop()]:
                if b in unseen:
                    unseen.remove(b)
                    component.add(b)
                    stack.append(b)
        componentes.append(component)
    # Volumen por componente, no total: un relieve invertido no puede quedar
    # oculto por el volumen positivo de la antena vecina.
    for component in componentes:
        tt = t[[int(x) in component for x in t[:, 0]]]
        pp = p - p[list(component)].mean(axis=0)
        volume = np.einsum("ij,ij->i", pp[tt[:, 0]], np.cross(pp[tt[:, 1]], pp[tt[:, 2]])).sum()/6
        assert volume > 1e-10, f"componente invertido o plano: {volume}"
    return len(componentes)


def main():
    ids = [x["clave"] for x in piezas.catalogo()]
    assert len(set(ids)) == len(ids) == 23
    for id_ in ids:
        a, b = piezas.generar(id_), piezas.generar(id_)
        count = comprobar_malla(a)
        assert np.array_equal(a["P"], b["P"]) and np.array_equal(a["tris"], b["tris"])
        print(f"PASS {id_}: {len(a['P'])} vértices, {len(a['tris'])} triángulos, {count} volúmenes orientados")
    labels = ["PIC_HEAD", "XPIC_LHAIR1", "XPIC_LHAIR2", "PIC_CHEST"]
    assert piezas.resolver_hueso("LHAIR2", labels) == 2
    assert piezas.resolver_hueso("LHAIR3", labels) == 2
    assert piezas.resolver_hueso("RHAIR2", labels) == 0
    assert piezas.resolver_hueso("MANT2", labels) == 3
    assert piezas.resolver_hueso("HAIR3", ["BUU_HEAD", "XBUU_HAIR1"]) == 1
    assert piezas.resolver_hueso("HEAD", ["KBU_HEAD", "KBU_HEAD_END"]) == 0
    print("PASS anclajes por etiqueta y alternativas HEAD/CHEST")
    # Dos ventanas de la misma posición con huesos distintos deben coincidir.
    p = np.array([[1, 1, .5], [1, 1, .5], [1, 4, .5], [-1, 1, .5]], float)
    outs = []
    for i in range(1, 4):
        o = piezas.deformar_cuerpo(p, [0, 1, 0, 1], labels, f"mj_cuerpo_{i:02}", [0, 0, 0], 10)
        assert np.isfinite(o).all() and np.array_equal(o[0], o[1])
        assert np.array_equal(o[2], p[2]), "la cabeza debe quedar intacta"
        assert np.allclose(o[3], [-o[0, 0], o[0, 1], o[0, 2]])
        outs.append(o)
    assert all(not np.array_equal(outs[i], outs[j]) for i in range(3) for j in range(i))
    print("PASS complexiones distintas, continuas, simétricas y cabeza intacta")
    rng = np.random.default_rng(604)
    points = rng.uniform([-2, -.5, -1], [2, 3.8, 1], (100, 3))
    normals = rng.normal(size=(100, 3))
    normals /= np.linalg.norm(normals, axis=1, keepdims=True)
    tangent = np.cross(normals, np.array([.37, .21, 1]))
    tangent /= np.linalg.norm(tangent, axis=1, keepdims=True)
    for i in range(1, 4):
        clave = f"mj_cuerpo_{i:02}"
        pp, nn = piezas.deformar_cuerpo_con_normales(points, normals, clave, [0, 0, 0], 10)
        d = 1e-5
        a = piezas.deformar_cuerpo(points+tangent*d, None, None, clave, [0, 0, 0], 10)
        b = piezas.deformar_cuerpo(points-tangent*d, None, None, clave, [0, 0, 0], 10)
        assert np.allclose(np.linalg.norm(nn, axis=1), 1)
        assert np.max(np.abs(np.einsum("ij,ij->i", nn, (a-b)/(2*d)))) < 1e-6
    print("PASS normales de complexiones perpendiculares a la superficie deformada")
    print("PASS partes B: 23 recetas; topología, orientación, pesos y continuidad")


if __name__ == "__main__":
    main()
