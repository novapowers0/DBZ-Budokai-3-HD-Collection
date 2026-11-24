#!/usr/bin/env python3
"""Atlas de colores propios para piezas CaC; compatible con el injertador B3.

No contiene arte de terceros. La banda alfa 0 pide sombreado a la rampa del
material anfitrión; 255 indicaría color sin sombra. La rampa y el contorno los
aporta el renderizador existente, no este atlas.
"""
from __future__ import annotations

import copy
import math

import numpy as np

REGIONES = ("piel", "placas", "gemas", "ropa")
CAMPO = "colores_regiones"


def validar_colores(colores):
    """Copia y valida RGB de 0 a 255, con nombres estables en la ficha."""
    if not isinstance(colores, dict):
        raise ValueError("colores_regiones debe ser un diccionario")
    extra = set(colores) - set(REGIONES)
    if extra:
        raise ValueError("Regiones desconocidas: " + ", ".join(sorted(extra)))
    salida = {}
    for region, rgb in colores.items():
        if (not isinstance(rgb, (tuple, list)) or len(rgb) != 3 or
                any(isinstance(c, bool) or not isinstance(c, int) or not 0 <= c <= 255 for c in rgb)):
            raise ValueError("%s necesita tres enteros RGB entre 0 y 255" % region)
        salida[region] = list(rgb)
    return salida


def ficha_con_colores(ficha, colores):
    """Devuelve una ficha independiente; el llamador debe persistir este campo."""
    salida = copy.deepcopy(ficha)
    salida[CAMPO] = validar_colores(colores)
    return salida


def atlas_por_caras(puntos, normales, huesos, triangulos, regiones, colores,
                    tam_celda=16, banda=0):
    """Crea P/N/bones/tris/UV/rgba para injertar_ropa_multi.

    regiones contiene una etiqueta por triángulo. Se separan las ventanas de
    cada cara para que ninguna UV interpole entre regiones de color diferentes.
    El atlas entero se rellena por bloques, sin bordes transparentes ni huecos.
    """
    paleta = validar_colores(colores)
    p, n, b, t = (np.asarray(puntos), np.asarray(normales), np.asarray(huesos),
                  np.asarray(triangulos))
    if p.ndim != 2 or p.shape[1] != 3 or n.shape != p.shape or b.shape != (len(p),):
        raise ValueError("P, N y bones incompatibles")
    if not np.isfinite(p).all() or not np.isfinite(n).all():
        raise ValueError("P o N no finitos")
    if t.ndim != 2 or t.shape[1] != 3 or not np.issubdtype(t.dtype, np.integer):
        raise ValueError("tris debe contener ternas de índices enteros")
    if not len(t) or t.min() < 0 or t.max() >= len(p) or len(regiones) != len(t):
        raise ValueError("Índices o regiones de caras incompatibles")
    if isinstance(tam_celda, bool) or not isinstance(tam_celda, int) or tam_celda < 4 or tam_celda % 4:
        raise ValueError("tam_celda debe ser múltiplo de 4 y al menos 4")
    if isinstance(banda, bool) or not isinstance(banda, int) or not 0 <= banda <= 14:
        raise ValueError("banda debe estar entre 0 y 14")
    orden = sorted(set(regiones))
    if not set(orden) <= set(paleta):
        raise ValueError("Faltan colores para las regiones de la malla")
    lado = 2 ** math.ceil(math.log2(max(1, math.ceil(math.sqrt(len(orden))))))
    size = lado * tam_celda
    atlas = np.zeros((size, size, 4), dtype=np.uint8)
    atlas[:, :, 3] = banda * 17
    # Los bloques no usados también reciben un color válido para evitar halos.
    atlas[:, :, :3] = paleta[orden[0]]
    centros = {}
    for i, nombre in enumerate(orden):
        x, y = i % lado, i // lado
        atlas[y*tam_celda:(y+1)*tam_celda, x*tam_celda:(x+1)*tam_celda, :3] = paleta[nombre]
        centros[nombre] = ((x + .5) / lado, (y + .5) / lado)
    ids = t.reshape(-1)
    uv = np.repeat(np.asarray([centros[r] for r in regiones], dtype=np.float32), 3, axis=0)
    return dict(P=p[ids].copy(), N=n[ids].copy(), bones=b[ids].copy(), UV=uv,
                tris=np.arange(len(ids), dtype=np.int32).reshape(-1, 3), rgba=atlas)


def tramos_para_atlas(rgba, rampa):
    """Usa exactamente el mismo cálculo de bandas que vista_heroe/HMS3.

    rampa es una textura RGBA float del material B3 cargada localmente; esta
    función no la guarda ni la distribuye. Devuelve (luz, textura RGBA float).
    """
    import vista_heroe as vh

    base = np.asarray(rgba, dtype=np.float32) / 255.0
    ramp = np.asarray(rampa, dtype=np.float32)
    if base.ndim != 3 or base.shape[2] != 4 or ramp.ndim != 3 or ramp.shape[2] != 4:
        raise ValueError("Se esperan imágenes RGBA")
    if not np.isfinite(ramp).all() or ramp.min() < 0 or ramp.max() > 1:
        raise ValueError("La rampa debe estar entre 0 y 1")
    class Modelo:
        tex = [base, ramp]
    mat = dict(tex=0, shader=1, env=False, diffuse=np.ones(3), blend=False)
    return vh.tramos_rampa(Modelo(), mat)
