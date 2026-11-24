"""Controles experimentales para mallas originales en coordenadas locales.

Contrato independiente del creador: la malla completa se pasa en espacio local
normalizado, con su eje longitudinal Y y su unión en el origen. La integración
debe convertir desde y hacia ese marco y conservar el anclaje y los pesos.
Los rangos son provisionales: razas_reglas.toml aún no define dimensiones.
"""
from __future__ import annotations

import math
import numpy as np

# Los valores son porcentajes enteros, con 50 como geometría original.
CONTROLES = {
    "freezer": ("cuerno_largo", "cuerno_curva", "cupula_altura", "mandibula_ancho", "cola_largo"),
    "namek": ("antena_largo",),
    "majin": ("antena_largo", "cuerpo_masa"),
}
TIPOS = {
    "cuerno": ("cuerno_largo", "cuerno_curva"),
    "antena": ("antena_largo",),
    "cupula": ("cupula_altura",),
    "mandibula": ("mandibula_ancho",),
    "cuerpo": ("cuerpo_masa",),
    "cola": ("cola_largo",),
}


def normalizar(raza, valores):
    """Solo controles de la raza; valores finitos limitados a 0..100."""
    if raza not in CONTROLES:
        raise ValueError("raza desconocida")
    resultado = {}
    for nombre in CONTROLES[raza]:
        v = float(valores.get(nombre, 50))
        if not math.isfinite(v):
            raise ValueError("valor no finito: " + nombre)
        resultado[nombre] = min(100.0, max(0.0, v))
    return resultado


def deformar(raza, tipo, puntos, triangulos, valores):
    """Devuelve posiciones y normales recalculadas sin alterar la entrada.

    Triángulos, UV y pesos permanecen intactos; se rechaza una malla degenerada.
    La pieza debe contener solamente la región indicada por `tipo`.
    """
    if tipo not in TIPOS:
        raise ValueError("tipo de pieza desconocido")
    v = normalizar(raza, valores)
    if any(k not in v for k in TIPOS[tipo]):
        raise ValueError("pieza incompatible con la raza")
    p = np.asarray(puntos, dtype=float)
    t = np.asarray(triangulos)
    if p.ndim != 2 or p.shape[1] != 3 or not len(p) or not np.isfinite(p).all():
        raise ValueError("posiciones inválidas")
    if t.ndim != 2 or t.shape[1] != 3 or not len(t) or t.dtype.kind not in "iu" or np.any(t < 0) or np.any(t >= len(p)):
        raise ValueError("triángulos inválidos")
    q = p.copy()
    def factor(nombre, amplitud):
        return 1 + amplitud * (v[nombre] - 50) / 50
    if tipo in ("cuerno", "antena", "cola"):
        nombre = {"cuerno": "cuerno_largo", "antena": "antena_largo", "cola": "cola_largo"}[tipo]
        q[:, 1] *= factor(nombre, .3)
        if tipo == "cuerno":
            # Desplazamiento suave nulo en la raíz, en unidades del artista.
            q[:, 0] += .2 * (v["cuerno_curva"] - 50) / 50 * p[:, 1] ** 2
    elif tipo == "cupula":
        q[:, 1] *= factor("cupula_altura", .25)
    elif tipo == "mandibula":
        q[:, 0] *= factor("mandibula_ancho", .2)
    else:
        f = factor("cuerpo_masa", .2)
        q[:, 0] *= f
        q[:, 2] *= f
    fn = np.cross(q[t[:, 1]] - q[t[:, 0]], q[t[:, 2]] - q[t[:, 0]])
    if np.any(np.linalg.norm(fn, axis=1) < 1e-10):
        raise ValueError("triángulo degenerado")
    n = np.zeros_like(q)
    for j in range(3):
        np.add.at(n, t[:, j], fn)
    mag = np.linalg.norm(n, axis=1)
    if np.any(mag < 1e-10):
        raise ValueError("normal indeterminada")
    return q, n / mag[:, None]
