#!/usr/bin/env python3
"""24 piezas completas de autor. No se lee ni se modifica arte de otro juego.

Marco de diseño propio: X derecha, Y arriba, Z delante; cabeza=1 unidad.
Las etiquetas del rig son semánticas, no índices de paleta B3. El adaptador
resuelve sus marcos reales; los JSON originales son distribuibles sin disco.
"""
from __future__ import annotations

import math
import numpy as np

VERSION = 1
PALETA = {
    'piel': (.36, .64, .24, 1.), 'musculo': (.76, .46, .46, 1.),
    'piel_majin': (.91, .56, .67, 1.), 'sombra_majin': (.67, .30, .43, 1.),
    'tela': (.24, .19, .38, 1.), 'claro': (.88, .86, .76, 1.),
    'ribete': (.24, .55, .53, 1.), 'dorado': (.85, .62, .25, 1.),
    'tinta': (.055, .045, .075, 1.), 'ojos': (.94, .91, .77, 1.),
}

# Diseño de proporciones, no valores extraídos de B3 ni de XV2.
RIG = {
    'WAIST': ((0, 3.0, 0), None), 'STMC': ((0, 3.65, 0), 'WAIST'),
    'CHEST': ((0, 4.55, 0), 'STMC'), 'NECK': ((0, 5.5, 0), 'CHEST'),
    'HEAD': ((0, 5.78, 0), 'NECK'),
    'HAIR1': ((0, 6.40, -.05), 'HEAD'),
    'HAIR2': ((0, 6.70, -.22), 'HAIR1'),
    'HAIR3': ((0, 6.77, -.48), 'HAIR2'),
    'LHAIR1': ((.15, 6.25, .27), 'HEAD'),
    'LHAIR2': ((.21, 6.42, .43), 'LHAIR1'),
    'RHAIR1': ((-.15, 6.25, .27), 'HEAD'),
    'RHAIR2': ((-.21, 6.42, .43), 'RHAIR1'),
    'MANT': ((0, 4.95, -.32), 'CHEST'),
    'MANT1': ((0, 4.23, -.42), 'MANT'),
    'MANT2': ((0, 3.51, -.51), 'MANT1'),
}
for _side, _s in (('L', 1), ('R', -1)):
    for _name, _p, _parent in (
        ('CHN', (1.00, 4.78, 0), 'CHEST'),
        ('ARM1', (1.10, 4.78, 0), _side+'CHN'),
        ('ARM2', (1.98, 4.78, 0), _side+'ARM1'),
        ('HANDROT', (2.70, 4.78, 0), _side+'ARM2'),
        ('L00_'+_side+'HAND', (2.86, 4.78, 0), _side+'HANDROT'),
        ('LEG1', (.43, 2.80, 0), 'WAIST'),
        ('LEG2', (.43, 1.53, 0), _side+'LEG1'),
        ('FOOT1', (.43, .32, 0), _side+'LEG2'),
    ):
        _key = _name if _name.startswith('L00_') else _side+_name
        RIG[_key] = ((_p[0]*_s, _p[1], _p[2]), _parent)

NAMEK = (
    # nombre, ancho, mandíbula, ceño, oreja, antena, apertura, curva, turbante
    ('Centinela', .80, .48, .060, .26, .38, .10, .04, False),
    ('Anciano sereno', .84, .46, .035, .30, .28, .17, -.04, False),
    ('Guardia severo', .86, .54, .080, .23, .34, .19, .06, False),
    ('Explorador', .76, .42, .040, .31, .46, .23, .12, False),
    ('Místico del alba', .82, .44, .050, .24, .32, .07, .05, True),
    ('Veterano', .84, .50, .070, .28, .40, -.02, -.16, False),
    ('Oráculo del río', .78, .43, .045, .33, .48, -.08, .14, False),
    ('Guardián de piedra', .90, .58, .085, .21, .30, .14, -.02, False),
)
MAJIN = (
    # nombre, ancho, mandíbula, mejilla, apéndice, boca
    ('Gota serena', .86, .53, .03, 'gota', .018),
    ('Llama de ámbar', .78, .46, .00, 'llama', -.015),
    ('Gancho travieso', .82, .49, .02, 'gancho', .035),
    ('Lóbulos gemelos', .93, .58, .06, 'doble', .025),
    ('Cinta de bruma', .76, .43, .00, 'cinta', -.010),
    ('Espiral abierta', .86, .52, .02, 'espiral', .030),
    ('Tríada risueña', .91, .57, .05, 'triada', .048),
    ('Bifurca firme', .84, .54, .01, 'bifurca', -.025),
)
CUERPOS_NAMEK = ('Luchador de gi', 'Viajero de capa partida',
                 'Sabio de túnica', 'Guardián de brazales')
CUERPOS_MAJIN = ('Veloz de chaleco', 'Redondo de faja ancha',
                 'Atlético de dos paños', 'Viajero de capa corta')


def catalogo():
    out = []
    for raza, cabezas, cuerpos, base, compat in (
        ('namek', NAMEK, CUERPOS_NAMEK, 3400, [11]),
        ('majin', MAJIN, CUERPOS_MAJIN, 3500, [34, 35, 36]),
    ):
        for i, row in enumerate(cabezas):
            out.append(dict(id=base+i, nombre=row[0], raza=raza, tipo='cabeza',
                            campo='b3_torso', cuerpos=compat,
                            archivo='namek_majin/%d.json' % (base+i)))
        for i, nombre in enumerate(cuerpos):
            out.append(dict(id=base+40+i, nombre=nombre, raza=raza, tipo='cuerpo',
                            campo='b3_guantes', cuerpos=compat,
                            archivo='namek_majin/%d.json' % (base+40+i)))
    return out


class Malla:
    def __init__(self):
        self.p, self.f, self.b, self.r, self.uv = [], [], [], [], []
        self.costuras = {}

    def v(self, p, bone, region, uv=(.5, .5)):
        self.p.append(tuple(map(float, p)))
        self.b.append(bone)
        self.r.append(region)
        self.uv.append(tuple(map(float, uv)))
        return len(self.p)-1

    def face(self, *vs):
        for j in range(1, len(vs)-1):
            self.f.append((vs[0], vs[j], vs[j+1]))

    def loft(self, rows, bone, region, sides=16, caps=(True, True), seam=None):
        """Secciones Y: (x,y,z,radio_x,radio_z); anillos sin vértice duplicado."""
        rings = []
        for i, (x, y, z, rx, rz) in enumerate(rows):
            rr = region(i/(len(rows)-1)) if callable(region) else region
            bb = bone(i/(len(rows)-1)) if callable(bone) else bone
            ids = []
            for j in range(sides):
                a = 2*math.pi*j/sides
                ids.append(self.v((x+rx*math.cos(a), y, z+rz*math.sin(a)), bb, rr,
                                  (j/sides, i/(len(rows)-1))))
            if rings:
                for j in range(sides):
                    k = (j+1) % sides
                    self.face(rings[-1][j], ids[j], ids[k], rings[-1][k])
            rings.append(ids)
        if caps[0]:
            c = self.v(rows[0][:3], self.b[rings[0][0]], self.r[rings[0][0]])
            for j in range(sides):
                self.face(c, rings[0][j], rings[0][(j+1) % sides])
        if caps[1]:
            c = self.v(rows[-1][:3], self.b[rings[-1][0]], self.r[rings[-1][0]])
            for j in range(sides):
                self.face(c, rings[-1][(j+1) % sides], rings[-1][j])
        if seam:
            self.costuras[seam] = rings[0] if seam == 'cuello_cabeza' else rings[-1]
        return rings

    def ellipsoid(self, c, r, bone, region, rows=6, sides=12):
        c, r = np.asarray(c, float), np.asarray(r, float)
        a = self.v(c+(0, r[1], 0), bone, region, (.5, 1))
        b = self.v(c-(0, r[1], 0), bone, region, (.5, 0))
        rings = []
        for i in range(1, rows):
            t = math.pi*i/rows
            ring = [self.v(c+r*np.array((math.sin(t)*math.cos(2*math.pi*j/sides),
                                         math.cos(t), math.sin(t)*math.sin(2*math.pi*j/sides))),
                           bone, region, (j/sides, 1-i/rows)) for j in range(sides)]
            if rings:
                for j in range(sides):
                    k = (j+1) % sides
                    self.face(rings[-1][k], ring[k], ring[j], rings[-1][j])
            rings.append(ring)
        for j in range(sides):
            k = (j+1) % sides
            self.face(a, rings[0][k], rings[0][j])
            self.face(b, rings[-1][j], rings[-1][k])

    def tube(self, centers, radii, bone, region, sides=8):
        centers = np.asarray(centers, float)
        tangents = np.gradient(centers, axis=0)
        tangents /= np.linalg.norm(tangents, axis=1)[:, None]
        lateral = np.cross(tangents[0], (0, 0, 1))
        if np.linalg.norm(lateral) < .1:
            lateral = np.cross(tangents[0], (0, 1, 0))
        rings = []
        for i, (c, t, rad) in enumerate(zip(centers, tangents, radii)):
            lateral -= t*np.dot(lateral, t)
            lateral /= np.linalg.norm(lateral)
            other = np.cross(t, lateral)
            bb = bone(i/(len(centers)-1)) if callable(bone) else bone
            rr = region(i/(len(centers)-1)) if callable(region) else region
            rad = np.broadcast_to(rad, (2,))
            ring = [self.v(c+rad[0]*math.cos(2*math.pi*j/sides)*lateral+
                           rad[1]*math.sin(2*math.pi*j/sides)*other, bb, rr,
                           (j/sides, i/(len(centers)-1))) for j in range(sides)]
            if rings:
                for j in range(sides):
                    k = (j+1) % sides
                    self.face(rings[-1][j], rings[-1][k], ring[k], ring[j])
            rings.append(ring)
        for i in (0, len(rings)-1):
            v = self.v(centers[i], self.b[rings[i][0]], self.r[rings[i][0]])
            for j in range(sides):
                k = (j+1) % sides
                self.face(v, *( (rings[i][k], rings[i][j]) if i == 0 else (rings[i][j], rings[i][k]) ))
        return rings

    def ribbon(self, points, halfwidth, bone, region):
        # Una tira cerrada muy fina, no un trazo 2D ajeno al modelo.
        centers = np.asarray(points, float)
        return self.tube(centers, [(halfwidth, halfwidth*.30)]*len(centers), bone, region, sides=4)

    def finish(self, meta):
        p, f = np.asarray(self.p), np.asarray(self.f)
        n = np.zeros_like(p)
        fn = np.cross(p[f[:, 1]]-p[f[:, 0]], p[f[:, 2]]-p[f[:, 0]])
        if np.any(np.linalg.norm(fn, axis=1) < 1e-10):
            raise ValueError('Triángulos degenerados en %s' % meta['nombre'])
        for j in range(3):
            np.add.at(n, f[:, j], fn)
        mag = np.linalg.norm(n, axis=1)
        if np.any(mag < 1e-10):
            raise ValueError('Normal sin definir')
        n /= mag[:, None]
        # Atlas propio por región, con margen de texel y UV locales no prestadas.
        keys = list(PALETA)
        # Cortar UV en límites de color. Un triángulo nunca interpola a través
        # de varias franjas del atlas, incluso al cambiar de piel a tela.
        mapping, pp, nn, uv, bb, rr, ff = {}, [], [], [], [], [], []
        for face in f:
            face_regions = [self.r[v] for v in face]
            region = max(dict.fromkeys(face_regions), key=face_regions.count)
            new = []
            for old in face:
                key = (int(old), region)
                if key not in mapping:
                    mapping[key] = len(pp)
                    pp.append(p[old].tolist())
                    nn.append(n[old].tolist())
                    u, v = self.uv[old]
                    uv.append(((keys.index(region)+.15+.70*u)/len(keys), .15+.70*v))
                    bb.append(self.b[old])
                    rr.append(region)
                new.append(mapping[key])
            ff.append(new)
        seams = {zone: [mapping[(int(v), self.r[v])] for v in ids]
                 for zone, ids in self.costuras.items()}
        return dict(meta, version=VERSION, kind='cac_original_nm',
                    P=pp, N=nn, UV=uv, tris=ff,
                    huesos=bb, pesos=[1.]*len(pp), regiones=rr,
                    colores=PALETA, costuras=seams,
                    rig={k: dict(pos=list(v[0]), padre=v[1]) for k, v in RIG.items()},
                    procedencia='Diseño matemático de autor, sin geometría, UV ni texturas de juegos',
                    licencia='CC0-1.0')


def _curva(ctrl, n=12):
    a, b, c, d = np.asarray(ctrl, float)
    return [(1-t)**3*a+3*(1-t)**2*t*b+3*(1-t)*t*t*c+t**3*d for t in np.linspace(0, 1, n)]


def _cabeza(m, raza, k):
    is_nm = raza == 'namek'
    if is_nm:
        _, width, jaw, brow, ear, antenna, spread, curve, turban = NAMEK[k]
        cheek, smile = .01, -.005
    else:
        _, width, jaw, cheek, style, smile = MAJIN[k]
        brow, ear, turban = .028+.004*(k % 3), .13, False
    skin = 'piel' if is_nm else 'piel_majin'
    # Mentón, mandíbula, pómulos, sienes, bóveda : un único cráneo continuo.
    profile = [(5.50, .120, .120, 0), (5.59, .155, .145, .015),
               (5.67, jaw*.34, .192, .040), (5.77, jaw*.50, .255, .030),
               (5.88, width*.47+cheek, .307, .018),
               (6.02, width*.50, .335, 0), (6.17, width*.49, .325, -.008),
               (6.31, width*.43, .283, -.018), (6.43, width*.29, .195, -.025),
               (6.49, width*.11, .085, -.028)]
    rings = m.loft([(0,y,z,rx,rz) for y,rx,rz,z in profile],
                   lambda t: 'NECK' if t < .08 else 'HEAD', skin, sides=16,
                   caps=(False, True), seam='cuello_cabeza')
    # Frente propia con superficie evaluable; todo el arte facial sale de aquí.
    def front(x, y):
        ys = [r[0] for r in profile]
        rx = np.interp(y, ys, [r[1] for r in profile])
        rz = np.interp(y, ys, [r[2] for r in profile])
        cz = np.interp(y, ys, [r[3] for r in profile])
        return float(cz+rz*math.sqrt(max(.02, 1-(x/rx)**2)))

    for s in (-1, 1):
        # Ojos almendrados, párpado superior y pupila se modelan en la superficie.
        outline = [(s*.055, 6.045), (s*.13, 6.096-brow*.35),
                   (s*.265, 6.091+brow*.15), (s*.240, 6.023), (s*.135, 6.012)]
        if s < 0:
            outline.reverse()
        center = np.mean(outline, axis=0)
        c = m.v((center[0], center[1], front(*center)+.009), 'HEAD', 'ojos')
        rim = [m.v((x,y,front(x,y)+.010), 'HEAD', 'ojos') for x,y in outline]
        for j in range(len(rim)):
            m.face(c, rim[(j+1) % len(rim)], rim[j])
        x, y = s*(.155+.005*(k % 3)), 6.047
        m.ellipsoid((x, y, front(x,y)+.014), (.026,.039,.008), 'HEAD', 'tinta', rows=4, sides=8)
        points = [(s*x,y,front(s*x,y)+.015) for x,y in
                  ((.05,6.052),(.12,6.105),(.22,6.112+brow*.35),(.28,6.09))]
        m.ribbon(points, .010 if is_nm else .008, 'HEAD', 'tinta')
        ridge = [(s*x,y,front(s*x,y)+.009) for x,y in
                 ((.05,6.115),(.12,6.15),(.23,6.153+brow*.4),(.30,6.115))]
        m.tube(ridge, [.013,brow*.55,brow*.50,.009], 'HEAD', skin, sides=6)
        # Oreille en cuenco : pourtour extérieur et fond réellement concave.
        root = width*.46
        outline3 = [(s*root,5.93,.015), (s*(root+.045),6.13,-.025),
                    (s*(root+ear),6.13+.05*(k % 3),-.11),
                    (s*(root+ear*.67),5.99,-.06), (s*(root+.025),5.86,.005)]
        if s < 0:
            outline3.reverse()
        cen = np.mean(outline3, axis=0)
        back = m.v(cen+(0,0,-.040), 'HEAD', skin)
        center_ear = m.v(cen+(0,0,.004), 'HEAD', 'musculo' if is_nm else 'sombra_majin')
        outer = [m.v(p, 'HEAD', skin) for p in outline3]
        inner = [m.v(cen+(np.asarray(p)-cen)*.68+(0,0,.027), 'HEAD', skin) for p in outline3]
        for j in range(5):
            q = (j+1) % 5
            m.face(outer[j], outer[q], inner[q], inner[j])
            m.face(center_ear, inner[j], inner[q])
            m.face(back, outer[q], outer[j])
    # Nariz modelada, labio y sonrisa propios, sin atlas de rostro ajeno.
    m.ellipsoid((0,5.958,front(0,5.958)+.017), (.044,.073,.043), 'HEAD', skin, rows=5, sides=8)
    mouth = [(x,5.811+smile*(abs(x)/.115)**1.5, front(x,5.815)+.009)
             for x in np.linspace(-.115,.115,7)]
    m.ribbon(mouth, .009, 'HEAD', 'tinta')
    for s in (-1,1):
        m.ribbon([(s*.045,5.906,front(s*.045,5.906)+.008),
                  (s*.066,5.912,front(s*.066,5.912)+.008)], .004, 'HEAD', 'tinta')
    if is_nm:
        if turban:
            # Diseño de bandas cerradas : ninguna antena atraviesa el turbante.
            for band in range(3):
                pts = [(math.cos(a)*(.402-.030*band),6.31+.065*band+.025*math.sin(2*a+band),
                        -.02+math.sin(a)*(.325-.020*band)) for a in np.linspace(0,2*math.pi,33)]
                m.tube(pts, [.043]*len(pts), 'HEAD', 'claro', sides=6)
            m.ellipsoid((0,6.435,-.02),(.335,.11,.28),'HEAD','claro',rows=5,sides=16)
        else:
            for s,side in ((1,'L'),(-1,'R')):
                pts = _curva(((s*.145,6.26,.285), (s*(.17+spread*.30),6.41,.38),
                              (s*(.20+spread),6.39+antenna*.35,.38+antenna*.5),
                              (s*(.18+spread),6.25+curve,.30+antenna)))
                m.tube(pts, np.linspace(.035,.010,len(pts)), 'HEAD', skin, sides=7)
        if k in (1,5,7):
            for s in (-1,1):
                m.ribbon([(s*x,y,front(s*x,y)+.008) for x,y in
                          ((.25,5.94),(.28,5.90),(.29,5.865))], .004, 'HEAD','tinta')
    else:
        _tentaculos(m, style)
        for s in (-1,1):
            for y in (6.22,6.29):
                x = s*.23
                m.ellipsoid((x,y,front(x,y)+.005),(.018,.026,.005), 'HEAD', 'sombra_majin', rows=3,sides=6)


def _tentaculos(m, style):
    paths = {
        'gota': [((0,6.40,-.03),(0,6.78,-.05),(0,6.76,-.45),(0,6.43,-.60))],
        'llama': [((0,6.40,-.03),(0,6.72,-.05),(.11,7.01,-.16),(.17,7.10,-.29))],
        'gancho': [((0,6.40,-.03),(.29,6.76,-.04),(.48,6.72,-.21),(.37,6.50,-.24))],
        'doble': [((s*.13,6.39,-.03),(s*.30,6.65,-.07),(s*.43,6.55,-.22),(s*.40,6.37,-.35)) for s in (-1,1)],
        'cinta': [((0,6.40,-.03),(-.32,6.91,-.12),(.36,6.90,-.50),(.10,6.33,-.73))],
        'espiral': [((0,6.40,-.03),(-.48,6.87,-.18),(.43,7.04,-.30),(.14,6.65,-.38))],
        'triada': [((s*.15,6.38,-.05),(s*.25,6.67,-.11),(s*.32,6.69,-.23),(s*.29,6.52,-.34)) for s in (-1,0,1)],
        'bifurca': [((0,6.40,-.03),(0,6.71,-.12),(s*.19,6.86,-.29),(s*.28,6.79,-.43)) for s in (-1,1)],
    }
    for ctrl in paths[style]:
        pts = _curva(ctrl,14)
        radii = [.075*(1-t)**.65+.010 for t in np.linspace(0,1,len(pts))]
        m.tube(pts,radii,'HEAD','piel_majin',sides=8)


def _capa(m, skin, short=False):
    # Dos faldones, pliegue a escala grande; cada paño es cerrado y de autor.
    for s in (-1,1):
        grid = []
        for layer in (-1,1):
            rows=[]
            for j in range(6):
                t=j/5
                y=4.92-t*(1.35 if short else 2.05)
                row=[]
                for i in range(5):
                    u=i/4
                    x=s*(.075+u*(.91+.18*t))
                    z=-.40-.20*t-.055*math.sin(3*math.pi*u)*t+layer*.014
                    row.append(m.v((x,y+.05*t*math.sin(math.pi*u),z),
                                   'CHEST', 'claro' if not short else 'tela', (u,t)))
                rows.append(row)
            grid.append(rows)
        for layer in (0,1):
            for j in range(5):
                for i in range(4):
                    vs=(grid[layer][j][i],grid[layer][j][i+1],grid[layer][j+1][i+1],grid[layer][j+1][i])
                    m.face(*(vs if (layer == 1) == (s == 1) else vs[::-1]))
        edge=[(0,i) for i in range(5)]+[(j,4) for j in range(1,6)]+[(5,i) for i in range(3,-1,-1)]+[(j,0) for j in range(4,0,-1)]
        for (a,b),(c,d) in zip(edge,edge[1:]+edge[:1]):
            vs=(grid[0][a][b],grid[1][a][b],grid[1][c][d],grid[0][c][d])
            m.face(*(vs if s == 1 else vs[::-1]))


def _cuerpo(m, raza, k):
    nm = raza == 'namek'
    skin = 'piel' if nm else 'piel_majin'
    # Variación real de silueta y construcción de prenda, no escalas de donante.
    widths = ((.57,.59,.96),(.54,.60,.87),(.50,.53,.80),(.62,.65,1.04)) if nm else (
        (.50,.51,.78),(.78,1.02,.97),(.55,.61,1.02),(.59,.66,.87))
    hip, belly, shoulder = widths[k]
    depth = .49 if (not nm and k == 1) else (.32 if nm else .33)
    rows = [(0,2.76,0,hip*.77,.27),(0,3.00,0,hip,.33),
            (0,3.38,.01,belly,depth),(0,3.78,.015,belly*.98,depth*1.03),
            (0,4.17,0,shoulder*.79,.35),(0,4.55,-.005,shoulder,.36),
            (0,4.80,-.01,shoulder*.95,.33),(0,5.00,-.01,shoulder*.71,.26),
            (0,5.25,0,.27,.17),(0,5.50,0,.12,.12)]
    def torso_bone(t):
        return 'WAIST' if t < .23 else ('STMC' if t < .49 else ('CHEST' if t < .90 else 'NECK'))
    def torso_region(t):
        if t > .81:
            return skin
        if not nm and k in (1,2):
            return skin
        return 'tela'
    rings=m.loft(rows,torso_bone,torso_region,16,(True,False),'cuello_cuerpo')
    # Faja de tela amplia, cierre oval original sin emblemas de personajes.
    m.loft([(0,3.00,.005,hip*1.025,.345),(0,3.10,.005,hip*1.025,.345),
            (0,3.22,.005,max(hip,belly)*1.02,max(.35,depth))], 'WAIST',
           'ribete' if nm else 'dorado',16)
    m.ellipsoid((0,3.11,.36),(.105,.085,.025),'WAIST','dorado',rows=4,sides=10)
    for s,side in ((1,'L'),(-1,'R')):
        # Bras continus : épaules, biceps, coude, avant-bras et poignets.
        bulk = (1.12 if nm and k==3 else (1.22 if not nm and k==2 else 1.))
        pts=[(s*x,4.78,0) for x in (.74,1.00,1.17,1.43,1.73,1.98,2.12,2.36,2.58,2.70)]
        rs=[(.22*bulk,.23),(.26*bulk,.28),(.24*bulk,.26),(.23*bulk,.24),(.19,.20),
            (.165,.17),(.20*bulk,.21),(.18*bulk,.19),(.12,.13),(.095,.10)]
        m.tube(pts,rs,lambda t:side+('ARM1' if t < .55 else 'ARM2'),skin,sides=12)
        # Paume et doigts propres, trois volumes de lecture plus pouce.
        hand=side+'HANDROT'
        m.ellipsoid((s*2.83,4.78,.01),(.19,.115,.135),hand,skin,rows=5,sides=10)
        for finger in range(4):
            z=-.085+finger*.056
            y=4.785
            length=(.18,.23,.24,.19)[finger]
            m.tube([(s*2.91,y,z),(s*(3.00+length*.3),y-.004,z),(s*(3.00+length),y-.025,z)],
                   [.037,.034,.020],hand,skin,sides=6)
        m.tube([(s*2.78,4.73,.10),(s*2.84,4.63,.19),(s*2.95,4.62,.22)],
               [.06,.05,.026],hand,skin,sides=7)
        # Patches musculaires comme surface de peau segmentée, pas armure.
        if nm:
            for x,rx,bone in ((1.43,.24,side+'ARM1'),(2.26,.22,side+'ARM2')):
                m.ellipsoid((s*x,4.78,.212),(rx,.135,.045),bone,'musculo',rows=5,sides=10)
                for d in (-.11,0,.11):
                    m.ribbon([(s*(x+d),4.69,.238),(s*(x+d+.018),4.78,.259),
                              (s*(x+d),4.87,.238)],.008,bone,'piel')
        elif k in (0,3):
            # Deux pans de gilet en surépaisseur : bords contrastés et poitrine libre.
            x=s*.45
            m.ellipsoid((x,4.35,.305),(.17,.55,.075),'CHEST','claro',rows=6,sides=10)
            m.ribbon([(s*.28,3.89,.36),(s*.30,4.33,.39),(s*.30,4.80,.28)],.022,'CHEST','ribete')
        # Jambières continues ; pantalons bouffants segmentés aux genoux.
        baggy=(.34 if nm else (.43 if k==1 else .37))
        pts=[(s*.43,y,0) for y in (2.92,2.65,2.30,1.90,1.53,1.20,.85,.50,.32)]
        rr=[(.31,.31),(baggy,baggy*.92),(baggy*.94,baggy*.91),(.27,.28),(.24,.26),
            (.27,.27),(.23,.23),(.14,.16),(.12,.14)]
        m.tube(pts,rr,lambda t:side+('LEG1' if t < .5 else 'LEG2'),
               lambda t:'tela' if t < .80 else 'ribete',sides=12)
        m.ellipsoid((s*.43,.23,.17),(.21,.22,.43),side+'FOOT1','ribete' if nm else 'claro',rows=5,sides=12)
        if nm and k==3:
            m.tube([(s*2.40,4.78,0),(s*2.53,4.78,0),(s*2.64,4.78,0)],
                   [(.18,.185),(.17,.18),(.13,.14)],side+'ARM2','claro',sides=12)
    if nm and k in (1,2):
        _capa(m,skin,short=k==2)
    if not nm and k==3:
        _capa(m,skin,short=True)
    if nm and k==2:
        # Tunique originale fendue, deux pans séparés pour libérer les hanches.
        for s,side in ((1,'L'),(-1,'R')):
            m.loft([(s*.34,2.35,.01,.31,.36),(s*.34,2.75,.01,.32,.37),
                    (s*.30,3.14,.01,.31,.36)],side+'LEG1','claro',12)


def generar(ident):
    meta = next((x for x in catalogo() if x['id'] == int(ident)), None)
    if meta is None:
        raise ValueError('ID de pieza original desconocido: %s' % ident)
    m=Malla()
    k=int(ident) % 100
    if meta['tipo']=='cabeza':
        _cabeza(m,meta['raza'],k)
    else:
        _cuerpo(m,meta['raza'],k-40)
    return m.finish(meta)


def catalogo_b3():
    """Metadatos para el selector común; no activa automáticamente injertos."""
    return [dict(tipo='original_'+('torso' if p['tipo']=='cabeza' else 'guantes'),
                 id=p['id'], nombre=p['nombre'], cuerpos=p['cuerpos'], idx_afs=-1,
                 archivo='nativa:'+p['archivo']) for p in catalogo()]
