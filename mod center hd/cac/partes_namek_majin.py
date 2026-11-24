#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Diseños propios namekianos/majin para el pipeline de partes_originales.

No contiene ni lee arte de B3/XV2. Las coordenadas están en un marco HEAD
normalizado: X derecha, Y arriba, Z delante, ancho de cráneo = 1. El adaptador
compartido ajusta estas recetas al cuerpo local del jugador y las injerta.
B3 usa un hueso por ventana: cada vértice declara un peso real 1.0; no se
prometen mezclas de pesos que el formato no puede representar.
"""
from __future__ import annotations

from dataclasses import dataclass, field
import math

import numpy as np


VERSION = 1
REGIONES = {
    "piel": (0.34, 0.64, 0.22),
    "musculo": (0.80, 0.47, 0.49),
    "tela": (0.88, 0.87, 0.82),
    "ribete": (0.40, 0.32, 0.57),
    "piel_majin": (0.94, 0.59, 0.69),
}

# Sólo dimensiones/curvas de autor. No se han extraído de modelos comerciales.
CABEZAS_NAMEK = (
    ("Brote", .48, .04, .02, .050, .00),
    ("Vigía", .78, .14, -.08, .045, -.05),
    ("Arco", .64, -.16, .24, .052, .07),
    ("Loto", .72, .29, -.15, .044, .10),
    ("Senda", .58, -.26, -.22, .057, -.09),
    ("Brisa", .86, .05, .33, .041, .04),
)
OREJAS_NAMEK = (("Hoja", .40, .08), ("Lanza", .57, .18), ("Aleta", .33, -.12))
TENTACULOS_MAJIN = (
    ("Gota", ((0, .76, -.02), (0, 1.01, -.06), (0, 1.02, -.40), (0, .78, -.58)), .095),
    ("Llama", ((0, .76, -.02), (0, 1.16, -.04), (.10, 1.36, -.18), (.22, 1.42, -.29)), .085),
    ("Cometa", ((0, .76, -.02), (0, .98, -.14), (0, 1.02, -.68), (0, .58, -.91)), .090),
    ("Ola", ((0, .76, -.02), (.20, 1.16, -.06), (.43, .93, -.32), (.27, .77, -.47)), .090),
    ("Lazo", ((0, .76, -.02), (-.24, 1.12, -.06), (-.37, 1.00, -.42), (-.11, .92, -.58)), .080),
    ("Bifurca", ((0, .76, -.02), (0, 1.08, -.08), (.24, 1.23, -.34), (.39, 1.13, -.45)), .075),
)


def catalogo():
    """Metadatos estables; las claves se guardan sin depender de su orden."""
    filas = []
    for k, (nombre, *_) in enumerate(CABEZAS_NAMEK, 1):
        filas.append(dict(clave=f"nm_cabeza_{k:02}", raza="namek", tipo="cabeza",
                          nombre=f"Namek · {nombre}", sustituye="antenas", marco="HEAD"))
    for k, (nombre, *_) in enumerate(OREJAS_NAMEK, 1):
        filas.append(dict(clave=f"nm_orejas_{k:02}", raza="namek", tipo="orejas",
                          nombre=f"Orejas · {nombre}", sustituye="orejas", marco="HEAD"))
    filas += [dict(clave="nm_capa", raza="namek", tipo="ropa", nombre="Capa de viaje",
                   sustituye="capa", marco="HEAD"),
              dict(clave="nm_turbante", raza="namek", tipo="ropa", nombre="Turbante de travesía",
                   sustituye="turbante", marco="HEAD")]
    for k, (nombre, *_) in enumerate(TENTACULOS_MAJIN, 1):
        filas.append(dict(clave=f"mj_antena_{k:02}", raza="majin", tipo="cabeza",
                          nombre=f"Majin · {nombre}", sustituye="antenas", marco="HEAD"))
    for k, nombre in enumerate(("Sereno", "Firme", "Pícaro"), 1):
        filas.append(dict(clave=f"mj_rostro_{k:02}", raza="majin", tipo="rostro",
                          nombre=f"Rostro · {nombre}", sustituye="", marco="HEAD"))
    for k, nombre in enumerate(("Delgada", "Robusta", "Musculosa"), 1):
        filas.append(dict(clave=f"mj_cuerpo_{k:02}", raza="majin", tipo="cuerpo",
                          nombre=f"Complexión · {nombre}", sustituye="", marco="BODY"))
    return filas


@dataclass
class _Malla:
    puntos: list = field(default_factory=list)
    triangulos: list = field(default_factory=list)
    huesos: list = field(default_factory=list)
    regiones: list = field(default_factory=list)

    def vertice(self, p, hueso="HEAD", region="piel"):
        self.puntos.append(tuple(map(float, p)))
        self.huesos.append(str(hueso))
        self.regiones.append(region)
        return len(self.puntos) - 1

    def cara(self, *indices):
        for k in range(1, len(indices) - 1):
            self.triangulos.append((indices[0], indices[k], indices[k + 1]))

    def terminar(self, clave):
        p = np.asarray(self.puntos, float).reshape(-1, 3)
        t = np.asarray(self.triangulos, int).reshape(-1, 3)
        n = np.zeros_like(p)
        if len(t):
            fn = np.cross(p[t[:, 1]] - p[t[:, 0]], p[t[:, 2]] - p[t[:, 0]])
            if np.any(np.linalg.norm(fn, axis=1) < 1e-10):
                raise ValueError(f"{clave}: triángulo degenerado")
            for j in range(3):
                np.add.at(n, t[:, j], fn)
            mag = np.linalg.norm(n, axis=1)
            if np.any(mag < 1e-10):
                raise ValueError(f"{clave}: normal indeterminada")
            n /= mag[:, None]
        meta = next(x for x in catalogo() if x["clave"] == clave)
        return dict(meta, P=p, N=n, tris=t, UV=np.full((len(p), 2), .5),
                    regiones=list(self.regiones), huesos=list(self.huesos),
                    pesos=[[(h, 1.0)] for h in self.huesos],
                    colores=dict(REGIONES), version=VERSION)


def _bezier(controles, t):
    a, b, c, d = np.asarray(controles, float)
    return (1-t)**3*a + 3*(1-t)**2*t*b + 3*(1-t)*t*t*c + t**3*d


def _tubo(m, controles, radio, hueso, region="piel", segmentos=8, lados=7, plano=1.0):
    """Barrido cerrado con extremo redondeado, sin anillos de radio cero."""
    centros = np.array([_bezier(controles, i / segmentos) for i in range(segmentos + 1)])
    tangentes = np.gradient(centros, axis=0)
    tangentes /= np.linalg.norm(tangentes, axis=1)[:, None]
    lateral = np.cross(tangentes[0], (0, 0, 1))
    if np.linalg.norm(lateral) < .1:
        lateral = np.cross(tangentes[0], (0, 1, 0))
    anillos = []
    for i in range(segmentos):
        t = i / segmentos
        # Transporte del marco: evita giros arbitrarios de las secciones.
        lateral -= tangentes[i] * np.dot(lateral, tangentes[i])
        lateral /= np.linalg.norm(lateral)
        otro = np.cross(tangentes[i], lateral)
        r = radio * (.96 * (1-t)**.72 + .04)
        h = hueso(t) if callable(hueso) else hueso
        ring = []
        for j in range(lados):
            a = 2*math.pi*j/lados
            ring.append(m.vertice(centros[i] + r*(math.cos(a)*lateral + plano*math.sin(a)*otro), h, region))
        anillos.append(ring)
        if i:
            for j in range(lados):
                k = (j+1) % lados
                m.cara(anillos[i-1][j], anillos[i-1][k], ring[k], ring[j])
    raiz = m.vertice(centros[0] - tangentes[0]*radio*.04,
                     hueso(0) if callable(hueso) else hueso, region)
    punta = m.vertice(centros[-1], hueso(1) if callable(hueso) else hueso, region)
    for j in range(lados):
        k = (j+1) % lados
        m.cara(raiz, anillos[0][k], anillos[0][j])
        m.cara(anillos[-1][j], anillos[-1][k], punta)


def _elipsoide(m, centro, radios, hueso="HEAD", region="piel", filas=4, lados=8):
    c, r = np.array(centro), np.array(radios)
    arriba = m.vertice(c + (0, r[1], 0), hueso, region)
    abajo = m.vertice(c - (0, r[1], 0), hueso, region)
    rings = []
    for i in range(1, filas):
        a = math.pi*i/filas
        ring = [m.vertice(c + r*np.array([math.sin(a)*math.cos(2*math.pi*j/lados),
                                         math.cos(a), math.sin(a)*math.sin(2*math.pi*j/lados)]),
                          hueso, region) for j in range(lados)]
        if rings:
            for j in range(lados):
                k = (j+1) % lados
                m.cara(rings[-1][k], ring[k], ring[j], rings[-1][j])
        rings.append(ring)
    for j in range(lados):
        k = (j+1) % lados
        m.cara(arriba, rings[0][k], rings[0][j])
        m.cara(abajo, rings[-1][j], rings[-1][k])


def _cabeza_namek(m, k):
    _, largo, apertura, curva, radio, ceno = CABEZAS_NAMEK[k]
    for signo, lado in ((1, "L"), (-1, "R")):
        c = ((signo*.19, .52, .56),
             (signo*(.20+apertura*.35), .64+largo*.12, .64),
             (signo*(.24+apertura), .56+largo*.38, .56+largo*.65),
             (signo*(.26+apertura), .52+largo*.22+curva, .56+largo))
        _tubo(m, c, radio, lambda t, s=lado: s+"HAIR1" if t < .36 else s+"HAIR2")
        # Ceño como relieve fino de piel, unido visualmente a la frente nativa.
        _tubo(m, ((signo*.055, .38+ceno, .615), (signo*.13, .41+ceno, .63),
                  (signo*.24, .42, .59), (signo*.32, .39-ceno*.2, .52)),
              .026 + .004*k, "HEAD", segmentos=4, lados=6, plano=.50)


def _orejas_namek(m, k):
    _, largo, subida = OREJAS_NAMEK[k]
    for s in (1, -1):
        # Volumen lenticular; la membrana interior es otro relieve cerrado.
        outline = [(s*.42, .22, .08), (s*.47, .46, .02),
                   (s*(.46+largo), .43+subida, -.10),
                   (s*(.61+largo*.15), .24, -.015), (s*.46, .16, .07)]
        if s < 0:
            outline.reverse()
        centro = np.array(outline).mean(0)
        front = m.vertice(centro + (0, 0, .065), "HEAD", "piel")
        back = m.vertice(centro - (0, 0, .065), "HEAD", "piel")
        ring = [m.vertice(p, "HEAD", "piel") for p in outline]
        for j in range(len(ring)):
            q = (j+1) % len(ring)
            m.cara(front, ring[q], ring[j])
            m.cara(back, ring[j], ring[q])
        _elipsoide(m, centro + (0, 0, .057), (largo*.22, .055, .013),
                   region="musculo", filas=3, lados=6)


def _capa(m):
    # Paño estrecho de viaje con pliegues asimétricos de autor; no reproduce
    # la capa/hombreras de Piccolo. Dos superficies y todos los bordes cerrados.
    nx, ny = 8, 6
    capas = []
    for cara in (1, -1):
        grid = []
        for j in range(ny + 1):
            t = j/ny
            fila = []
            for i in range(nx + 1):
                u = 2*i/nx-1
                x = u*(.82+.22*t)
                y = -.45-2.10*t+.08*t*math.cos(3*math.pi*u)
                z = -.38-.26*t-.085*math.cos(4*math.pi*u)*t + cara*.012
                # La base Piccolo con capa tiene una cadena MANT. En un traje
                # sin ella se resuelve a CHEST, sin inventar huesos.
                bone = "MANT" if t < .30 else ("MANT1" if t < .62 else "MANT2")
                fila.append(m.vertice((x, y, z), bone, "tela" if t < .84 else "ribete"))
            grid.append(fila)
        capas.append(grid)
    for side, grid in enumerate(capas):
        for j in range(ny):
            for i in range(nx):
                q = (grid[j][i], grid[j][i+1], grid[j+1][i+1], grid[j+1][i])
                m.cara(*(tuple(reversed(q)) if side == 0 else q))
    borde = ([(0, i) for i in range(nx+1)] + [(j, nx) for j in range(1, ny+1)] +
             [(ny, i) for i in range(nx-1, -1, -1)] + [(j, 0) for j in range(ny-1, 0, -1)])
    for a, b in zip(borde, borde[1:] + borde[:1]):
        m.cara(capas[0][b[0]][b[1]], capas[1][b[0]][b[1]],
               capas[1][a[0]][a[1]], capas[0][a[0]][a[1]])


def _turbante(m):
    # Cuatro bandas independientes cruzadas; la banda baja se hunde bajo la
    # frente para que no quede un aro flotando sobre la cabeza del jugador.
    for j in range(4):
        # Cada banda se hace como tubo cerrado alrededor de una elipse.
        n, lados = 16, 5
        rings = []
        for i in range(n):
            a = 2*math.pi*i/n
            y = .65+j*.075+.045*math.sin(a+.8*j)
            r = .46*(1-.075*j)
            centro = np.array([r*math.cos(a), y, .04+r*.95*math.sin(a)])
            radial = np.array([math.cos(a), 0, math.sin(a)])
            ring = [m.vertice(centro + .046*(math.cos(2*math.pi*k/lados)*radial +
                                            np.array([0, math.sin(2*math.pi*k/lados), 0])),
                              "HEAD", "tela") for k in range(lados)]
            rings.append(ring)
        for i in range(n):
            for k in range(lados):
                m.cara(rings[i][(k+1)%lados], rings[(i+1)%n][(k+1)%lados], rings[(i+1)%n][k], rings[i][k])
    _elipsoide(m, (.28, .70, -.34), (.13, .105, .07), region="ribete")
    _elipsoide(m, (0, .855, .04), (.385, .14, .36), region="tela", filas=5, lados=12)


def _antena_majin(m, k):
    _, puntos, radio = TENTACULOS_MAJIN[k]
    # La animación inclina HAIR1 alrededor de un pivote exterior al cráneo.
    # La raíz debe seguir HEAD; si todo el tubo empieza en HAIR1, flota al
    # agacharse Buu. Los anillos posteriores sí heredan su cadena flexible.
    chain = lambda t: "HEAD" if t < .20 else ("HAIR1" if t < .40 else ("HAIR2" if t < .70 else "HAIR3"))
    _tubo(m, puntos, radio, chain, "piel_majin", segmentos=10, lados=8)
    if k == 5:
        _tubo(m, [(-p[0], p[1], p[2]) for p in puntos], radio*.88, chain,
              "piel_majin", segmentos=10, lados=8)


def _rostro_majin(m, k):
    # Relieves de frente y mejillas; ojos, boca y expresiones originales del
    # jugador permanecen. El hueco central no bloquea labios/mandíbula.
    for s in (1, -1):
        y, slant = ((.29, -.035), (.31, .055), (.295, .025))[k]
        _tubo(m, ((s*.055, y-slant, .55), (s*.13, y, .575),
                  (s*.23, y+slant, .54), (s*.31, y+.01, .47)),
              (.025, .037, .030)[k], "HEAD", "piel_majin", segmentos=4, lados=6, plano=.5)
        if k != 0:
            _elipsoide(m, (s*.31, .08, .46), (.080, .055 if k == 1 else .036, .025),
                       region="piel_majin", filas=3, lados=6)


def resolver_hueso(sufijo, etiquetas):
    """Resuelve por nombre exacto (incluye auxiliares X); jamás por índice."""
    def buscar(s):
        return next((i for i, x in enumerate(etiquetas)
                     if x == s or x.lstrip("X").split("_", 1)[-1] == s), None)
    candidatos = [sufijo]
    if sufijo.startswith(("LHAIR", "RHAIR")) or sufijo.startswith("HAIR"):
        # Si no existe el eslabón distal, sube al anterior antes de HEAD.
        if sufijo[-1:].isdigit():
            candidatos += [sufijo[:-1]+str(i) for i in range(int(sufijo[-1])-1, 0, -1)]
        candidatos += ["HEAD"]
    elif sufijo.startswith("MANT"):
        candidatos += ["MANT", "CHEST", "STMC"]
    else:
        candidatos += ["HEAD"]
    for s in candidatos:
        b = buscar(s)
        if b is not None:
            return b
    raise ValueError(f"sin hueso de anclaje para {sufijo}")


def _factores_cuerpo(y, clave):
    belly = np.exp(-((y-.10)/.17)**4)
    shoulders = np.exp(-((y-.25)/.12)**4)
    fade = np.clip((.39-y)/.09, 0, 1)
    if clave == "mj_cuerpo_01":
        return 1-.16*belly*fade, 1-.18*belly*fade
    if clave == "mj_cuerpo_02":
        return 1+.19*belly*fade, 1+.22*belly*fade
    if clave == "mj_cuerpo_03":
        return 1+.15*shoulders*fade-.05*belly*fade, 1+.10*shoulders*fade
    raise ValueError("complexión desconocida")


def deformar_cuerpo(puntos, huesos, etiquetas, clave, centro_cadera, altura):
    """Complexiones locales del cuerpo del jugador; costuras no se separan.

    Opera en espacio modelo, con una función continua única para TODOS los
    vértices coincidentes (nunca por draw ni por hueso). No modifica la cabeza.
    El pipeline devuelve luego el resultado al marco local de cada hueso y
    transforma las normales con la inversa transpuesta del Jacobiano.
    """
    p = np.asarray(puntos, float).copy()
    if altura <= 0 or not np.isfinite(altura):
        raise ValueError("altura inválida")
    c = np.asarray(centro_cadera, float)
    y = (p[:, 1]-c[1])/altura
    # Los tres perfiles se desvanecen antes del cuello y en piernas/cadera.
    sx, sz = _factores_cuerpo(y, clave)
    p[:, 0] = c[0]+(p[:, 0]-c[0])*sx
    p[:, 2] = c[2]+(p[:, 2]-c[2])*sz
    return p


def deformar_cuerpo_con_normales(puntos, normales, clave, centro_cadera, altura):
    """Devuelve (P,N) con la normal transformada, no sólo renormalizada."""
    p, n = np.asarray(puntos, float), np.asarray(normales, float)
    if p.shape != n.shape or p.ndim != 2 or p.shape[1] != 3:
        raise ValueError("posiciones/normales incompatibles")
    out = deformar_cuerpo(p, None, None, clave, centro_cadera, altura)
    c = np.asarray(centro_cadera, float)
    y = (p[:, 1]-c[1])/altura
    sx, sz = _factores_cuerpo(y, clave)
    # Diferencias en la función ESCALAR continua, sin mezclar normales de
    # ventanas ni borrar las aristas duras que dibujó el artista de B3.
    e = 1e-5
    xp, zp = _factores_cuerpo(y+e, clave)
    xm, zm = _factores_cuerpo(y-e, clave)
    dxdy, dzdy = (xp-xm)/(2*e*altura), (zp-zm)/(2*e*altura)
    nn = np.column_stack((n[:, 0]/sx,
                          n[:, 1] - (p[:, 0]-c[0])*dxdy*n[:, 0]/sx
                          - (p[:, 2]-c[2])*dzdy*n[:, 2]/sz,
                          n[:, 2]/sz))
    nn /= np.maximum(np.linalg.norm(nn, axis=1, keepdims=True), 1e-12)
    return out, nn


def _ajustar_cadena_majin(receta, model, w, inv, escala):
    """Asienta los cambios de hueso en SUS pivotes, no en el arco de diseño.

    Un tubo rígido por anillos que cruza la articulación lejos de su pivote se
    pliega y se vuelve del revés al animar. Las curvas de autor aportan volumen,
    curvatura y punta; las uniones conservan los pivotes del esqueleto local.
    """
    k = int(receta['clave'][-2:])-1
    controles = np.asarray(TENTACULOS_MAJIN[k][1], float)
    root = lambda suffix: inv[:3, :3] @ w[resolver_hueso(suffix, model.labels), :3, 3] + inv[:3, 3]
    pivotes = np.asarray([root(s) for s in ('HAIR1', 'HAIR2', 'HAIR3')])
    tiempos = np.array([0., .2, .4, .7, 1.])
    # El primer anillo entra en la cabeza; los otros cambios de hueso quedan
    # centrados en su articulación. La punta se prolonga desde el último hueso.
    eje = pivotes[-1]-pivotes[-2]
    if np.linalg.norm(eje) < .02*escala:
        eje = pivotes[1]-pivotes[0]
    if np.linalg.norm(eje) < .02*escala:
        eje = np.array([0., 0., -.25*escala])
    punta = pivotes[-1]+eje*(.30+.10*k)
    out = []
    for rama in range(2 if k == 5 else 1):
        c = controles.copy()
        if rama:
            c[:, 0] *= -1
        fin = punta+(c[-1]-c[0])*escala*.35
        centros = np.vstack((pivotes[0]+[0, -.08*escala, 0], pivotes, fin))
        curva = []
        for i in range(11):
            t = i/10
            tramo = min(int(np.searchsorted(tiempos, t, side='right'))-1, 3)
            a, b = tiempos[tramo:tramo+2]
            u = (t-a)/(b-a)
            center = centros[tramo]*(1-u)+centros[tramo+1]*u
            # Perturbación de autor nula en cada pivote: no lo desplaza.
            arco = _bezier(c, t)-((1-u)*_bezier(c, a)+u*_bezier(c, b))
            center += arco*escala
            curva.append(center)
        curva = np.asarray(curva)
        tangentes = np.gradient(curva, axis=0)
        tangentes /= np.maximum(np.linalg.norm(tangentes, axis=1, keepdims=True), 1e-12)
        lateral = np.cross(tangentes[0], (0, 0, 1))
        if np.linalg.norm(lateral) < .1:
            lateral = np.cross(tangentes[0], (0, 1, 0))
        radio = TENTACULOS_MAJIN[k][2]*escala*(.88 if rama else 1)
        for i in range(10):
            t = i/10
            lateral -= tangentes[i]*np.dot(lateral, tangentes[i])
            lateral /= max(np.linalg.norm(lateral), 1e-12)
            otro = np.cross(tangentes[i], lateral)
            r = radio*(.96*(1-t)**.72+.04)*(1+.55*max(0, 1-t/.2))
            for j in range(8):
                a = 2*math.pi*j/8
                out.append(curva[i]+r*(math.cos(a)*lateral+math.sin(a)*otro))
        out.append(curva[0]-tangentes[0]*radio*.04)
        out.append(curva[-1])
    p = np.asarray(out)
    # Recalcular normales después de acomodar los centros a la cadena real.
    n = np.zeros_like(p)
    tri = receta['tris']
    fn = np.cross(p[tri[:, 1]]-p[tri[:, 0]], p[tri[:, 2]]-p[tri[:, 0]])
    for j in range(3):
        np.add.at(n, tri[:, j], fn)
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    return p, n


def _asentar_relieve(p, tris, superficie, caras, escala):
    """Acomoda los relieves propios sobre la piel real, con su base enterrada.

    El cráneo no es un elipsoide común a los tres Buu: una profundidad HEAD
    constante dejaba las mejillas suspendidas. Se consulta la superficie local
    del jugador y se conserva un relieve pequeño, cerrado y sin tocarla.
    """
    out = p.copy()
    tt = superficie[caras]
    a, e1, e2 = tt[:, 0], tt[:, 1]-tt[:, 0], tt[:, 2]-tt[:, 0]
    det = e1[:, 0]*e2[:, 1]-e1[:, 1]*e2[:, 0]
    valid = np.abs(det) > 1e-10
    a, e1, e2, det = a[valid], e1[valid], e2[valid], det[valid]
    vecinos = [set() for _ in p]
    for t in tris:
        for v in t:
            vecinos[v].update(map(int, t))
    pendientes = set(range(len(p)))
    while pendientes:
        comp, pila = set(), [pendientes.pop()]
        while pila:
            v = pila.pop()
            comp.add(v)
            for u in vecinos[v] & pendientes:
                pendientes.remove(u)
                pila.append(u)
        ids = np.asarray(sorted(comp), int)
        centro = p[ids].mean(axis=0)
        d = centro-a
        u = (d[:, 0]*e2[:, 1]-d[:, 1]*e2[:, 0])/det
        v = (e1[:, 0]*d[:, 1]-e1[:, 1]*d[:, 0])/det
        dentro = (u >= -1e-7) & (v >= -1e-7) & (u+v <= 1+1e-7)
        if not np.any(dentro):
            raise ValueError("relieve fuera de la superficie de la cabeza")
        zs = a[:, 2]+u*e1[:, 2]+v*e2[:, 2]
        j = np.flatnonzero(dentro)[np.argmax(zs[dentro])]
        grad_x = (e1[j, 2]*e2[j, 1]-e1[j, 1]*e2[j, 2])/det[j]
        grad_y = (e1[j, 0]*e2[j, 2]-e1[j, 2]*e2[j, 0])/det[j]
        z = p[ids, 2]
        # Un único plano tangente por volumen evita proyectar cada cara contra
        # distintas capas de ojos/boca. Es una transformación afín con escala
        # positiva: conserva orientación y grosor, sin caras plegadas.
        dz = -.012*escala+(z-z.min())/max(float(np.ptp(z)), 1e-10)*.025*escala
        out[ids, 2] = zs[j]+grad_x*(p[ids, 0]-centro[0])+grad_y*(p[ids, 1]-centro[1])+dz
    n = np.zeros_like(out)
    fn = np.cross(out[tris[:, 1]]-out[tris[:, 0]], out[tris[:, 2]]-out[tris[:, 0]])
    for j in range(3):
        np.add.at(n, tris[:, j], fn)
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
    return out, n


def ajustar(pieza, model, pose=None):
    """Adaptador geométrico para el pipeline común; NO injerta ni guarda.

    Recibe una receta o su clave. Devuelve P/N model-space y vbone resuelto.
    Para antenas existentes, la raíz se asienta en HAIR1 del traje exacto; las
    bases sin esos huesos conservan la forma y se anclan en HEAD.
    """
    receta = generar(pieza) if isinstance(pieza, str) else pieza
    if receta["tipo"] == "cuerpo":
        raise ValueError("la complexión se aplica con deformar_cuerpo_con_normales")
    head = resolver_hueso("HEAD", model.labels)
    w = np.asarray(model.worlds(pose))
    inv = np.linalg.inv(w[head])
    pp, _ = model.posed(pose)
    local = pp @ inv[:3, :3].T + inv[:3, 3]
    q = local[model.vbone == head]
    if not len(q):
        raise ValueError("cabeza sin geometría para ajustar la pieza")
    namek = receta["raza"] == "namek"
    # Percentil sin orejas; si existen raíces de antena, son una regla más
    # estable que el bbox de una cabeza con apéndices de distinto tamaño.
    escala = float(np.quantile(np.abs(q[:, 0]), .90)*2) * (1.25 if namek else 1)
    if not np.isfinite(escala) or escala < 1e-5:
        raise ValueError("escala de cabeza inválida")
    roots = {}
    for suffix in (("LHAIR1", "RHAIR1") if namek else ("HAIR1",)):
        b = model.bone(suffix)
        if b is not None:
            roots[suffix] = inv[:3, :3] @ w[b, :3, 3] + inv[:3, 3]
    if namek and len(roots) == 2:
        escala = float(np.linalg.norm(roots["LHAIR1"]-roots["RHAIR1"])/.38)
    lp = receta["P"]*escala
    ln = receta["N"].copy()
    hs = receta["huesos"]
    if receta["clave"].startswith("nm_cabeza_"):
        for s, lado in ((1, "L"), (-1, "R")):
            if lado+"HAIR1" in roots:
                sel = np.array([x.startswith(lado+"HAIR") for x in hs])
                lp[sel] += roots[lado+"HAIR1"] - np.array([s*.19, .52, .56])*escala
    elif receta["clave"].startswith("mj_antena_") and "HAIR1" in roots:
        lp, ln = _ajustar_cadena_majin(receta, model, w, inv, escala)
    elif receta["clave"].startswith("mj_rostro_"):
        tri = np.asarray(model.tris, int)
        caras = tri[np.all(model.vbone[tri] == head, axis=1)]
        lp, ln = _asentar_relieve(lp, receta["tris"], local, caras, escala)
    elif receta["clave"] == "nm_turbante":
        # El techo propio del turbante debe envolver la calva, no atravesarla.
        lp[:, 1] += float(q[:, 1].max()) + .025*escala - .93*escala
    out = dict(receta)
    out.update(P=lp @ w[head, :3, :3].T + w[head, :3, 3],
               N=ln @ w[head, :3, :3].T,
               vbone=np.array([resolver_hueso(x, model.labels) for x in hs]),
               escala=escala)
    out["N"] /= np.maximum(np.linalg.norm(out["N"], axis=1, keepdims=True), 1e-12)
    return out


def generar(clave):
    """Malla propia y pesos por vértice; complexiones devuelven sólo receta."""
    meta = next((x for x in catalogo() if x["clave"] == clave), None)
    if meta is None:
        raise ValueError(f"pieza desconocida: {clave}")
    m = _Malla()
    if clave.startswith("nm_cabeza_"):
        _cabeza_namek(m, int(clave[-2:])-1)
    elif clave.startswith("nm_orejas_"):
        _orejas_namek(m, int(clave[-2:])-1)
    elif clave == "nm_capa":
        _capa(m)
    elif clave == "nm_turbante":
        _turbante(m)
    elif clave.startswith("mj_antena_"):
        _antena_majin(m, int(clave[-2:])-1)
    elif clave.startswith("mj_rostro_"):
        _rostro_majin(m, int(clave[-2:])-1)
    out = m.terminar(clave)
    if meta["tipo"] == "cuerpo":
        out["deformacion"] = clave
    return out


# IDs de receta del contrato común. Nunca se pasan a Afs.entry(). Las cuatro
# columnas ya forman parte de la ficha guardada, compartida y exportada.
_ZONAS = ("torso", "piernas", "guantes", "botas")
_B3 = (
    [(3100+i, 0, f"nm_cabeza_{i+1:02}", (11,)) for i in range(6)] +
    [(3120+i, 1, f"nm_orejas_{i+1:02}", (11,)) for i in range(3)] +
    [(3140, 2, "nm_capa", (11,)), (3160, 3, "nm_turbante", (11,))] +
    [(3200+i, 0, f"mj_antena_{i+1:02}", (34, 35, 36)) for i in range(6)] +
    [(3220+i, 1, f"mj_rostro_{i+1:02}", (34, 35, 36)) for i in range(3)] +
    [(3240+i, 2, f"mj_cuerpo_{i+1:02}", (34, 35, 36)) for i in range(3)]
)


def catalogo_b3():
    import namek_original
    import ia_namek
    nombres = {x["clave"]: x["nombre"] for x in catalogo()}
    return [dict(tipo="original_"+_ZONAS[z], id=i, nombre=nombres[clave],
                 idx_afs=-1, archivo="original:"+clave, cuerpos=list(cuerpos))
            for i, z, clave, cuerpos in _B3] + namek_original.catalogo_b3() + ia_namek.catalogo_b3()


def seleccion_b3(ficha):
    import ia_namek
    cuerpo = int(ficha.get("cuerpo", -1))
    return [(i, z, clave) for i, z, clave, cuerpos in _B3
            if cuerpo in cuerpos and int(ficha.get("b3_"+_ZONAS[z], -1)) == i] + ia_namek.seleccion(ficha)


def _complexion_b3(amb, clave):
    """Una sola deformación espacial para cuerpo y expresiones, sin costuras."""
    import struct
    import re
    import model_render as mr
    from partes_originales import _marcos
    base = mr.Model(amb)
    worlds = np.asarray(base.worlds(), float)
    centro = worlds[base.bone("WAIST"), :3, 3]
    pp, _ = base.posed()
    altura = float(np.ptp(pp[:, 1]))
    canon = lambda label: re.sub(r"_L\d\d_", "_L00_", label)
    ctx = dict(worlds=worlds, labels={canon(s): i for i, s in enumerate(base.labels)})
    data, counts = bytearray(amb), []
    for awg in range(base.avb.n_awg):
        m = base if awg == 0 else mr.Model(amb, awg=awg)
        W = _marcos(m, base, ctx, awg)
        P = np.einsum("nij,nj->ni", W[:, :3, :3], m.pos) + W[:, :3, 3]
        N = np.einsum("nij,nj->ni", W[:, :3, :3], m.nrm)
        Q, NN = deformar_cuerpo_con_normales(P, N, clave, centro, altura)
        changed = np.linalg.norm(Q-P, axis=1) > 1e-8
        local = np.linalg.solve(W[changed, :3, :3], (Q[changed]-W[changed, :3, 3])[..., None])[..., 0]
        normal = np.linalg.solve(W[changed, :3, :3], NN[changed, :, None])[..., 0]
        if not np.isfinite(local).all() or not np.isfinite(normal).all():
            raise ValueError("complexión con coordenadas no finitas")
        for i, p, n in zip(np.flatnonzero(changed), local, normal):
            at = m.avb.vb0 + int(i)*44
            struct.pack_into(">3f", data, at, *p)
            struct.pack_into(">3f", data, at+20, *n)
        counts.append(int(changed.sum()))
    return bytes(data), counts


def _uv_region(tex, region):
    """Muestra estable del color LOCAL: no se repinta la textura compartida."""
    rgb = np.asarray(tex[..., :3], float)
    # Las bases B3 guardan la luz en la textura y restan su rampa toon propia.
    # Se prioriza un parche liso opaco para no llevar líneas/ojos a las antenas.
    # En B3 el alfa elige la banda de la rampa; NO indica transparencia.
    # La piel majin es una base casi blanca coloreada por su rampa rosa.
    # Buscar un rosa opaco llevaba al interior de la boca (y, en Piccolo con
    # turbante, al negro de las líneas) en lugar de a la superficie de piel.
    target = np.asarray({'piel': (.68, .86, .39),
                         'piel_majin': (.98, .98, .98),
                         'tela': (.94, .94, .94)}.get(region, REGIONES[region]))
    cost = np.sum((rgb-target)**2, axis=2)
    cost += (np.max(rgb, axis=2)-np.min(rgb, axis=2) < .05) * (0.5 if region in ("piel", "musculo", "piel_majin") else 0)
    cost += (tex[..., 3] > .8)*2   # banda sin toon reservada a tinta/ojos
    for axis in (0, 1):
        cost += np.linalg.norm(rgb-np.roll(rgb, 1, axis=axis), axis=2)*2
        cost += np.linalg.norm(rgb-np.roll(rgb, -1, axis=axis), axis=2)*2
    y, x = np.unravel_index(np.argmin(cost), cost.shape)
    # Un centro exacto del primer texel puede interpolarse a un epsilon negativo
    # en el retrato: numpy.mod(-epsilon, ancho) redondea a ancho. Mantener una
    # milésima de texel de margen conserva el color y evita esa discontinuidad.
    return ((x+.501)/tex.shape[1], (y+.501)/tex.shape[0])


def _preparar_b3(model, claves):
    from injertar_pelo import triangulos_por_draw
    td = triangulos_por_draw(model)
    if len(td) != len(model.tris):
        raise ValueError("draws y triángulos de la base no coinciden")
    tris = np.asarray(model.tris, int)
    bones = model.vbone[tris]
    head = model.bone("HEAD")
    P, _ = model.posed()
    W = np.asarray(model.worlds())
    inv = np.linalg.inv(W[head])
    local = P @ inv[:3, :3].T + inv[:3, 3]
    dib = np.asarray([di for di, _ in td])
    candidatos = []
    for di, draw in enumerate(model.table.draws):
        mat = model.mats[draw["material"]]
        if draw["prim"] != 5 or mat["env"] or mat["shader"] == 0xFFFFFFFF or mat["tex"] >= len(model.tex):
            continue
        cuenta = int(np.sum((dib == di) & np.any(bones == head, axis=1)))
        candidatos.append((cuenta, di))
    if not candidatos:
        raise ValueError("sin draw toon compatible con piezas animadas")
    host_piel = max(candidatos)[1]
    def gris(di):
        mat = model.mats[model.table.draws[di]["material"]]
        ramp = model.tex[mat["shader"]][..., :3]
        return float(np.mean(np.ptp(ramp, axis=2)))
    # La prenda toma una rampa gris nativa, sin cambiar la de la piel.
    host_tela = min((di for _, di in candidatos), key=lambda d: (gris(d), -int(np.sum(dib == d))))
    grupos = {}
    for clave in claves:
        pieza = ajustar(clave, model)
        quitar = np.zeros(len(tris), bool)
        host = host_tela if clave in ("nm_capa", "nm_turbante") else host_piel
        escala = pieza["escala"]
        if pieza["sustituye"] == "antenas":
            ids = [i for i, s in enumerate(model.labels) if "HAIR" in s]
            quitar |= np.any(np.isin(bones, ids), axis=1)
        elif pieza["sustituye"] == "orejas":
            # Solo las puntas laterales, detrás de la frente: no ojos ni mandíbula.
            q = local[tris]
            lateral = np.all(np.abs(q[..., 0]) > .36*escala, axis=1)
            quitar |= lateral & np.all(bones == head, axis=1) & (q[..., 2].mean(axis=1) < .30*escala)
        elif clave == "nm_capa":
            ids = [i for i, s in enumerate(model.labels) if "MANT" in s or "SPAT" in s]
            quitar |= np.any(np.isin(bones, ids), axis=1)
            # La capa de Piccolo no vive sólo en SPAT: sus paños centrales
            # comparten rampa con las hombreras, pero siguen CHEST/CHN/ARMROT.
            # Seleccionar esa rampa a partir de SPAT evita borrar el gi o el
            # cinturón (STMC), aunque también estén ligados al torso.
            rampas = {model.mats[model.table.draws[di]["material"]]["shader"]
                      for di in set(dib[quitar])}
            tela = [i for i, s in enumerate(model.labels)
                    if any(k in s for k in ("MANT", "SPAT", "CHEST", "LCHN", "RCHN", "LARMROT", "RARMROT"))]
            for di, draw in enumerate(model.table.draws):
                mat_capa = model.mats[draw["material"]]
                if mat_capa["shader"] in rampas and not mat_capa["env"]:
                    quitar |= (dib == di) & np.all(np.isin(bones, tela), axis=1)
        elif clave == "nm_turbante":
            # TA es la pieza rígida del turbante nativo; no es HEAD ni FACE.
            # Dejarla intacta superponía el turbante original al de autor.
            ta = [i for i, s in enumerate(model.labels) if s.lstrip("X").endswith("_TA")]
            quitar |= np.any(np.isin(bones, ta), axis=1)
            # El traje con turbante separa la tela del HEAD en otro material.
            for cuenta, di in candidatos:
                if di != host_piel and cuenta and gris(di) < .02:
                    quitar |= (dib == di) & np.all(bones == head, axis=1) & (local[tris, 1].mean(axis=1) > .30*escala)
        mat = model.mats[model.table.draws[host]["material"]]
        tex = int(mat["tex"])
        # Un anfitrión por textura. Se conservan los bytes del AZT íntegros tras
        # usar el injertador común: ni los otros draws ni las caras se repintan.
        group = grupos.setdefault(tex, dict(host=host, piezas=[], quitar=quitar.copy()))
        group["quitar"] |= quitar
        muestras = {r: _uv_region(model.tex[tex], r) for r in set(pieza["regiones"])}
        uv = np.asarray([muestras[r] for r in pieza["regiones"]])
        group["piezas"].append(dict(pieza, UV=uv))
    out = []
    for tex, group in grupos.items():
        ps = group["piezas"]
        offset, ts = 0, []
        for p in ps:
            ts.append(p["tris"]+offset)
            offset += len(p["P"])
        out.append(dict(host=group["host"], quitar=group["quitar"],
                        P=np.concatenate([p["P"] for p in ps]), N=np.concatenate([p["N"] for p in ps]),
                        UV=np.concatenate([p["UV"] for p in ps]), bones=np.concatenate([p["vbone"] for p in ps]),
                        tris=np.concatenate(ts), rgba=np.rint(model.tex[tex]*255).astype(np.uint8),
                        tex_destino=tex, zona="+".join(p["clave"] for p in ps)))
    return out


def aplicar_b3(amb, ficha):
    """Adaptador del único pipeline: receta persistida → #AMB del jugador."""
    import ia_namek
    if ia_namek.seleccion(ficha):
        return ia_namek.aplicar_b3(amb, ficha)
    import namek_original
    if namek_original.seleccion(ficha):
        return namek_original.aplicar_b3(amb, ficha)
    sel = seleccion_b3(ficha)
    if not sel:
        return bytes(amb), {}
    import model_render as mr
    import injertar_ropa as ir
    import texture_b3 as tb
    actual = bytes(amb)
    cuerpo = int(ficha["cuerpo"])
    m = mr.Model(actual)
    prefijo = "PIC_" if cuerpo == 11 else ("BUL_", "BUM_", "BUS_")
    head = m.labels[resolver_hueso("HEAD", m.labels)]
    if not head.lstrip("X").startswith(prefijo):
        raise ValueError("la receta no corresponde al esqueleto de la base")
    counts = []
    claves = []
    for _, _, clave in sel:
        if clave.startswith("mj_cuerpo_"):
            actual, counts = _complexion_b3(actual, clave)
        else:
            claves.append(clave)
    resumen = {}
    if claves:
        m = mr.Model(actual)
        preparados = _preparar_b3(m, claves)
        nuevo, resumen = ir.injertar_ropa_multi(actual, m, preparados)
        # El injertador escribe una imagen incluso al reutilizar el atlas. Aquí
        # NO hay atlas nuevo: todos los UV muestrean colores existentes. Recuperar
        # los DDS exactos evita recomprimir o alterar texturas compartidas/AWG.
        _, antes = tb.parse_azt(actual)
        _, despues = tb.parse_azt(nuevo)
        data = bytearray(nuevo)
        for a, b in zip(antes, despues):
            if a["idx"] != b["idx"] or a["size"] != b["size"]:
                raise ValueError("el injerto ha cambiado la identidad de una textura")
            na, nb = a["bitmap_abs"]-128, b["bitmap_abs"]-128
            data[nb:nb+b["size"]] = actual[na:na+a["size"]]
        actual = bytes(data)
    resumen.update(piezas=[next(x["nombre"] for x in catalogo() if x["clave"] == clave)
                           for _, _, clave in sel],
                   ids=[i for i, _, _ in sel], ventanas_por_awg=counts,
                   materiales="toon, borde y DDS nativos conservados")
    return actual, resumen


def formas_b3(afs, fid, ficha, juego, resumen, construir):
    """Piccolo potencia su mismo cuerpo: no indexar modelos como si fueran formas."""
    if not seleccion_b3(ficha):
        return None
    import roster_build as rb
    cuerpo = int(ficha["cuerpo"])
    donante = int(ficha.get("donante", cuerpo))
    if donante != cuerpo:
        return []
    source = rb.IDS[cuerpo]
    out = []
    # Buu gordo/niño no tienen formas nativas. Las absorciones de Super Buu
    # remiten a cuerpos diferentes: no inventar enlaces al esqueleto inicial.
    if cuerpo != 11:
        return out
    for form in range(1, int(source["forms"])):
        data, r = construir(afs.entry(fid), dict(ficha, pelo=0, cara=0), juego, afs)
        out.append((data, form))
        resumen["original_forma%d" % form] = dict(fid=fid, piezas=r.get("piezas_b3", {}).get("originales", {}))
    return out
