#!/usr/bin/env python3
"""Dos influencias B3 en la vista XV2, usando matrices ANI1 ya admitidas.

Los nodos de dibujo son combinaciones de matrices, no huesos del juego. No se
modifica el bin convertido ni el esqueleto que usa el combate.
"""
import numpy as np


class Vista:
    def __init__(self, model):
        self.model = model
        self.bind = np.asarray(model.worlds(), float)
        self.inv_bind = np.linalg.inv(self.bind)
        self.bones = np.asarray(model.vbone, int)
        self.parents = np.asarray(model.parent, int)[self.bones]
        self.weights = np.clip(np.asarray([v["w"] for v in model.avb.vertices()]), 0, 1)
        self.weights[self.parents < 0] = 1
        self.parents = np.maximum(self.parents, 0)
        # ANI1 tiene indices u8. Cuantizar SOLO la vista, con el mayor numero de
        # niveles que cabe, manteniendo el modelo de combate sin cambio alguno.
        nb = len(self.bind)
        for levels in (64, 32, 16, 8, 4, 2, 1):
            q = np.rint(self.weights * levels).astype(int)
            keys = sorted(set((int(b), int(p), int(w)) for b, p, w in zip(self.bones, self.parents, q)
                              if w < levels and b != p))
            if nb + len(keys) <= 255:
                break
        else:
            raise ValueError("vista XV2: no caben las mezclas en ANI1")
        self.levels = levels
        self.keys = keys
        self.count = nb + len(keys)
        ids = {key: nb + i for i, key in enumerate(keys)}
        self.indices = np.asarray([ids.get((int(b), int(p), int(w)), int(b))
                                   for b, p, w in zip(self.bones, self.parents, q)], int)
        self.quant_error = float(np.max(np.abs(q / levels - self.weights), initial=0))
        self.P = np.einsum("vij,vj->vi", self.bind[self.bones, :3, :3], model.pos) + self.bind[self.bones, :3, 3]
        self.N = np.einsum("vij,vj->vi", self.bind[self.bones, :3, :3], model.nrm)

    def matrices(self, worlds):
        base = np.asarray(worlds) @ self.inv_bind
        extra = [base[b] * (q / self.levels) + base[p] * (1 - q / self.levels)
                 for b, p, q in self.keys]
        return np.concatenate([base, np.asarray(extra)]) if extra else base

    def posed(self, pose=None):
        M = self.matrices(self.model.worlds(pose))[self.indices]
        P = np.einsum("vij,vj->vi", M[:, :3, :3], self.P) + M[:, :3, 3]
        N = np.einsum("vij,vj->vi", M[:, :3, :3], self.N)
        N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-12)
        return P, N

    def delta(self, worlds, pose):
        return self.matrices(worlds) @ np.linalg.inv(self.matrices(self.model.worlds(pose)))


def habilitar(model):
    vista = Vista(model)
    model.cuerpo_vista = vista
    model.posed = vista.posed
    print("vista_heroe: XV2 dos pesos, %d nodos ANI1, %d niveles, error peso %.6f" %
          (vista.count, vista.levels, vista.quant_error))
    return vista


def selftest():
    from types import SimpleNamespace
    bind = np.tile(np.eye(4), (2, 1, 1))
    current = bind.copy()
    current[1, :2, :2] = [[0, -1], [1, 0]]
    model = SimpleNamespace(worlds=lambda pose=None: current if pose else bind,
                            vbone=np.array([1, 1]), parent=[-1, 0],
                            avb=SimpleNamespace(vertices=lambda: [{"w": .5}, {"w": 1.}]),
                            pos=np.array([[1., 0, 0], [1., 0, 0]]),
                            nrm=np.tile([1., 0, 0], (2, 1)))
    v = Vista(model)
    np.testing.assert_allclose(v.posed()[0], [[1, 0, 0], [1, 0, 0]], atol=1e-12)
    np.testing.assert_allclose(v.posed(True)[0], [[.5, .5, 0], [0, 1, 0]], atol=1e-12)
    P = v.posed(True)[0]
    D = v.delta(bind, True)[v.indices]
    back = np.einsum("vij,vj->vi", D[:, :3, :3], P) + D[:, :3, 3]
    np.testing.assert_allclose(back, model.pos, atol=1e-12)
    assert v.count <= 255 and v.quant_error == 0
    print("PASS xv2_cuerpo_vista: mezcla 50/50, giro 90 grados, ANI1 y vuelta a reposo")


if __name__ == "__main__":
    selftest()
