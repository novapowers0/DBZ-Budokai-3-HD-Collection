"""Diseños originales R16. No abre modelos, imágenes ni archivos de juegos.

Unidades de autor, Y vertical, Z frontal. Los anclajes son un maniquí propio;
el adaptador traduce etiquetas y proporciones, nunca copia geometría B3.
"""
from __future__ import annotations

import math
import numpy as np

VERSION = 2
BIND = {'WAIST': (0, 2.85, 0), 'STMC': (0, 3.5, 0),
        'CHEST': (0, 4.25, 0), 'NECK': (0, 4.9, 0), 'HEAD': (0, 5.35, 0)}
for _s, _sign in [('L', 1), ('R', -1)]:
    for _k, _p in {'CHN': (.82, 4.52, 0), 'ARM1': (1.02, 4.43, 0),
                   'ARM2': (1.65, 3.98, 0), 'HAND': (2.24, 3.55, 0),
                   'LEG1': (.38, 2.65, 0), 'LEG2': (.43, 1.43, .04),
                   'FOOT1': (.43, .23, 0), 'FOOT2': (.43, .15, .40)}.items():
        BIND[_s+_k] = (_sign*_p[0], _p[1], _p[2])
for _i in range(1, 6):
    BIND['T_TAIL%d' % _i] = (0, 2.70-.18*_i, -.28-.42*_i)

NOMBRES = {
    'freezer': ('Arista', 'Marea', 'Cometa', 'Ónice', 'Astilla', 'Albor', 'Prisma', 'Cenit'),
    'namek': ('Junco', 'Sauce', 'Cauce', 'Brote', 'Vigía', 'Bruma', 'Loto', 'Senda'),
    'majin': ('Nimbo', 'Gota', 'Espiral', 'Vela', 'Brisa', 'Lazo', 'Chispa', 'Muelle'),
}
PALETAS = {
    'freezer': ['e5e1cd', '267c86', 'bd7445', '3d4e63', 'f4e8b9', '18202c', 'f8f1dc', 'ce693c'],
    'namek': ['71964b', 'ab6563', 'd9cba4', '354d66', 'a7b86e', '1d2830', 'f3efda', 'c68a41'],
    'majin': ['c579a1', '9c4e7d', 'dad5c7', '37475b', 'e6adbe', '20202c', 'faf0dd', 'd49e4f'],
}
CUERPOS = ('Caminante', 'Guardián', 'Explorador', 'Centinela')
SEAM = {'neck': dict(center=(0, 4.9, 0), radius=(.175, .155), sides=16),
        'wrist': dict(radius=(.115, .125), sides=10),
        'ankle': dict(radius=(.14, .16), sides=12)}


class Mesh:
    def __init__(self):
        self.p, self.faces, self.bones, self.binds, self.regions = [], [], [], [], []
        self.seams = {}

    def v(self, p, bone, a=None, b=None, t=0):
        self.p.append(tuple(map(float, p)))
        self.bones.append(bone)
        self.binds.append((a or bone, b or a or bone, float(t)))
        return len(self.p)-1

    def f(self, ids, region=0):
        for j in range(1, len(ids)-1):
            self.faces.append((ids[0], ids[j], ids[j+1]))
            self.regions.append(region)

    def bridge(self, a, b, region=0):
        assert len(a) == len(b)
        for j in range(len(a)):
            k = (j+1) % len(a)
            self.f((a[j], a[k], b[k], b[j]), region)

    def horizontal(self, center, rx, rz, bone, a=None, b=None, t=0, sides=16):
        c = np.asarray(center)
        return [self.v(c+(rx*math.sin(2*math.pi*j/sides), 0, rz*math.cos(2*math.pi*j/sides)),
                       bone, a, b, t) for j in range(sides)]

    def tube(self, centers, radii, bones, binds=None, region=0, sides=10, caps=True):
        centers = np.asarray(centers, float)
        tangent = np.gradient(centers, axis=0)
        tangent /= np.linalg.norm(tangent, axis=1)[:, None]
        rings = []
        lateral = np.cross(tangent[0], (0, 0, 1))
        if np.linalg.norm(lateral) < .1:
            lateral = np.cross(tangent[0], (0, 1, 0))
        for i, (c, r, bone) in enumerate(zip(centers, radii, bones)):
            lateral -= tangent[i]*np.dot(lateral, tangent[i])
            lateral /= np.linalg.norm(lateral)
            other = np.cross(tangent[i], lateral)
            rr = (r, r) if np.isscalar(r) else r
            aa, bb, tt = binds[i] if binds else (bone, bone, 0)
            ring = [self.v(c+rr[0]*math.cos(2*math.pi*j/sides)*lateral+
                           rr[1]*math.sin(2*math.pi*j/sides)*other, bone, aa, bb, tt)
                    for j in range(sides)]
            if rings:
                # tube uses the opposite orientation to a horizontal Y sweep.
                self.bridge(ring, rings[-1], region[i-1] if isinstance(region, list) else region)
            rings.append(ring)
        if caps:
            for ri, rev in [(0, False), (-1, True)]:
                aa, bb, tt = binds[ri] if binds else (bones[ri], bones[ri], 0)
                c = self.v(centers[ri], bones[ri], aa, bb, tt)
                for j in range(sides):
                    q = (c, rings[ri][j], rings[ri][(j+1) % sides])
                    self.f(tuple(reversed(q)) if rev else q,
                           region[ri] if isinstance(region, list) else region)
        return rings

    def ellipsoid(self, center, radii, bone='HEAD', region=0, sides=12, rows=6):
        c, r = np.asarray(center), np.asarray(radii)
        rings = []
        for i in range(1, rows):
            a = math.pi*i/rows
            ring = self.horizontal(c+(0, -math.cos(a)*r[1], 0),
                                   math.sin(a)*r[0], math.sin(a)*r[2], bone, sides=sides)
            if rings:
                self.bridge(rings[-1], ring, region)
            rings.append(ring)
        low = self.v(c-(0, r[1], 0), bone)
        high = self.v(c+(0, r[1], 0), bone)
        for j in range(sides):
            k = (j+1) % sides
            self.f((low, rings[0][k], rings[0][j]), region)
            self.f((high, rings[-1][j], rings[-1][k]), region)

    def finish(self, **meta):
        p = np.asarray(self.p, float)
        t = np.asarray(self.faces, int)
        n = normals(p, t)
        if np.any(np.linalg.norm(np.cross(p[t[:, 1]]-p[t[:, 0]], p[t[:, 2]]-p[t[:, 0]]), axis=1) < 1e-9):
            raise ValueError('triángulo degenerado en diseño original')
        return dict(version=VERSION, **meta, P=np.round(p, 7).tolist(), N=np.round(n, 7).tolist(),
                    tris=t.tolist(), bones=self.bones, bindings=self.binds, regions=self.regions,
                    seams=self.seams)


def normals(p, t):
    n = np.zeros_like(p, dtype=float)
    fn = np.cross(p[t[:, 1]]-p[t[:, 0]], p[t[:, 2]]-p[t[:, 0]])
    for j in range(3):
        np.add.at(n, t[:, j], fn)
    lengths = np.linalg.norm(n, axis=1)
    if np.any(lengths < 1e-10):
        raise ValueError('normal nula en diseño')
    return n/lengths[:, None]


def bezier(ctrl, samples=10):
    a, b, c, d = np.asarray(ctrl, float)
    return np.array([(1-t)**3*a+3*(1-t)**2*t*b+3*(1-t)*t*t*c+t**3*d
                     for t in np.linspace(0, 1, samples)])


def eye(m, x, y, z, sign, tilt=0, sleepy=False):
    # Ojo lenticular propio: una superficie de color y su contorno geométrico.
    outline = [(-.118, .012), (-.045, .062), (.104, .037), (.115, -.005), (.015, -.035), (-.092, -.020)]
    rings = []
    for scale, zz in [(1.12, z), (.90, z+.006)]:
        rings.append([m.v((x+sign*dx*scale, y+dy*scale+tilt*dx, zz-.06*abs(dx)), 'HEAD')
                      for dx, dy in outline])
    for j in range(6):
        k = (j+1) % 6
        q = (rings[0][j], rings[0][k], rings[1][k], rings[1][j])
        m.f(tuple(reversed(q)) if sign > 0 else q, 5)
    q = rings[1]
    m.f(tuple(reversed(q)) if sign > 0 else q, 5 if sleepy else 6)
    if not sleepy:
        m.ellipsoid((x-sign*.015, y+.010, z+.014), (.022, .037, .012), region=5, sides=8, rows=4)


def head(race, k, expression=0):
    if race == 'freezer':
        return freezer_head(k, expression)
    m = Mesh()
    hx, hy, hz = BIND['HEAD']
    width = [.96, 1.07, .91, 1.03, .98, 1.10, .94, 1.01][k]
    dome = [1.0, .95, 1.13, 1.02, .91, .98, 1.08, 1.12][k]
    # Couronne, tempes, pommettes et menton: profils indépendants de toute source.
    profile = [(-.45, .175, .155, 0), (-.29, .215, .215, .035),
               (-.18, .282, .268, .028), (-.04, .323, .288, .014),
               (.13, .357, .310, 0), (.30, .371, .322, -.008),
               (.46, .371, .320, -.028), (.61, .337, .290, -.042),
               (.73, .259, .230, -.048), (.80, .144, .135, -.05)]
    rings = []
    for row, (y, rx, rz, z) in enumerate(profile):
        if row == 0:
            ring = m.horizontal((0, hy+y, z), rx, rz, 'HEAD')
            m.seams['neck'] = ring
        else:
            yy = y if y < .3 else .3+(y-.3)*dome
            ring = m.horizontal((0, hy+yy, z), rx*width, rz, 'HEAD')
        if rings:
            region = 1 if race == 'freezer' and row >= 7 else 0
            m.bridge(rings[-1], ring, region)
        rings.append(ring)
    top = m.v((0, hy+.83*dome, -.05), 'HEAD')
    for j in range(16):
        m.f((top, rings[-1][j], rings[-1][(j+1) % 16]), 1 if race == 'freezer' else 0)
    # Nez et lèvres: petits volumes dessinés, pas un nez/une bouche tiré d'un jeu.
    nose = [(-.035, hy+.26, .300), (.035, hy+.26, .300),
            (.043, hy+.10, .315), (0, hy+.075, .357), (-.043, hy+.10, .315)]
    ids = [m.v(p, 'HEAD') for p in nose]
    for face in [(0, 1, 3), (1, 2, 3), (0, 3, 4)]:
        m.f(tuple(ids[i] for i in reversed(face)), 0)
    for s in (1, -1):
        eye(m, s*.169*width, hy+.32, .300, s, (k-3)*.025, sleepy=expression == 2)
        brow = [(s*.055, hy+.425, .312), (s*.12, hy+.455+(k%3)*.012, .312),
                (s*.22, hy+.453, .277), (s*.284, hy+.40, .233)]
        centers = bezier(brow, 6)
        m.tube(centers, [.020, .024, .024, .023, .019, .011], ['HEAD']*6,
               region=1 if race == 'freezer' else 0, sides=6)
    mouth = bezier([(-.103, hy-.036, .296), (-.04, hy-.056-expression*.008, .326),
                    (.04, hy-.056-expression*.008, .326), (.103, hy-.033, .296)], 8)
    m.tube(mouth, [.006]*8, ['HEAD']*8, region=5, sides=5)
    if expression == 1:
        m.ellipsoid((0, hy-.075, .306), (.055, .035, .010), region=5, sides=10, rows=4)
    if race == 'namek':
        for s in (1, -1):
            root = (s*.315*width, hy+.28, -.015)
            outline = [(root[0], hy+.10, .04), (s*(.51+.018*k), hy+.23, -.075),
                       (s*(.69+.012*k), hy+.45+(k%3)*.045, -.15),
                       (s*.42*width, hy+.43, -.06)]
            front = m.v((s*.445*width, hy+.29, .065), 'HEAD')
            back = m.v((s*.445*width, hy+.29, -.09), 'HEAD')
            rr = [m.v(p, 'HEAD') for p in outline]
            for j in range(4):
                q = (front, rr[j], rr[(j+1) % 4])
                m.f(tuple(reversed(q)) if s < 0 else q, 0)
                q = (back, rr[(j+1) % 4], rr[j])
                m.f(tuple(reversed(q)) if s < 0 else q, 0)
            m.ellipsoid((s*.442*width, hy+.29, .057), (.090, .075, .015), region=1, sides=8, rows=4)
            length = [.30, .43, .53, .24, .46, .38, .55, .33][k]
            ctrl = [(s*.165, hy+.64, .198), (s*.18, hy+.80, .30),
                    (s*(.16+.025*k), hy+.87+length*.2, .35+length*.6),
                    (s*(.18+.020*k), hy+.76+length*.3, .34+length)]
            cc = bezier(ctrl, 10)
            m.tube(cc, np.linspace(.035, .009, 10), ['HEAD']*10, region=0, sides=7)
    elif race == 'freezer':
        if k not in (5, 7):
            for s in (1, -1):
                length = [.32, .48, .42, .24, .35, 0, .29, 0][k]
                root = (s*.30*width, hy+.59, -.06)
                ctrl = [root, (s*(.34+length*.65), hy+.64, -.06),
                        (s*(.34+length), hy+.71+length*.4, -.16-length*.3),
                        (s*(.30+length*.75), hy+.75+length, -.22-length*.3)]
                m.tube(bezier(ctrl, 10), np.linspace(.080, .007, 10), ['HEAD']*10, region=3, sides=8)
        if k in (3, 6, 7):
            m.ellipsoid((0, hy+.76, -.045), (.085, .16+.025*k, .13), region=2, sides=10, rows=6)
    else:
        paths = [((0, .75, -.02), (0, 1.03, -.05), (.12, 1.11, -.31), (.22, .99, -.44)),
                 ((0, .75, -.02), (0, .97, -.09), (-.05, .98, -.48), (-.10, .69, -.57)),
                 ((0, .75, -.02), (.38, 1.02, -.04), (.46, .87, -.37), (.22, .83, -.42)),
                 ((0, .75, -.02), (0, 1.00, -.02), (0, 1.29, -.12), (.10, 1.31, -.19)),
                 ((0, .75, -.02), (-.29, .97, -.10), (-.42, 1.04, -.31), (-.26, 1.11, -.40)),
                 ((0, .75, -.02), (.27, 1.08, -.10), (.33, 1.02, -.48), (.03, .97, -.53)),
                 ((0, .75, -.02), (0, .96, -.04), (.30, 1.10, -.10), (.36, 1.18, -.16)),
                 ((0, .75, -.02), (-.10, 1.05, -.07), (-.17, 1.18, -.29), (.09, 1.13, -.36))]
        cc = bezier(np.asarray(paths[k])+(0, hy, 0), 14)
        m.tube(cc, np.linspace(.112, .019, 14), ['HEAD']*14, region=0, sides=10)
        for s in (1, -1):
            m.ellipsoid((s*.340*width, hy+.24, -.035), (.07, .115, .10), region=0, sides=10, rows=5)
            for y in (.49, .58):
                m.ellipsoid((s*.28, hy+y, .18), (.012, .017, .005), region=5, sides=6, rows=4)
    return m.finish(race=race, kind='head', variant=k, expression=expression)


def freezer_head(k, expression=0):
    """Cabeza angular de autor; cuello común, pómulos anchos y ojos dibujados."""
    m = Mesh()
    hy = BIND['HEAD'][1]
    width = (.99, 1.08, .93, 1.04, .96, 1.10, .97, 1.02)[k]
    dome = (1., .88, 1.14, .98, .92, .94, 1.06, 1.12)[k]
    profiles = [(-.45, .175, .155, 0), (-.29, .175, .155, 0),
                (-.22, .235, .22, .025), (-.12, .305, .265, .02),
                (.04, .357, .292, 0), (.23, .390, .310, -.012),
                (.40, .375, .303, -.025), (.53, .303, .252, -.04),
                (.62, .168, .145, -.045)]
    rings = []
    for row, (y, rx, rz, z) in enumerate(profiles):
        yy = y if y < .4 else .4+(y-.4)*dome
        ring = []
        for j in range(16):
            angle = 2*math.pi*j/16
            cs = math.cos(angle)
            # La mitad frontal no es una esfera: reserva un plano facial legible.
            zz = rz*math.copysign(abs(cs)**(.48 if cs > 0 else 1.), cs)
            ring.append(m.v((rx*(1 if row < 2 else width)*math.sin(angle), hy+yy, z+zz), 'HEAD'))
        if row == 0:
            m.seams['neck'] = ring
        if rings:
            m.bridge(rings[-1], ring, 1 if row >= 7 else 0)
        rings.append(ring)
    top = m.v((0, hy+.67*dome, -.045), 'HEAD')
    for j in range(16):
        m.f((top, rings[-1][j], rings[-1][(j+1)%16]), 1)
    for sign in (1, -1):
        outline = [(.054, .218), (.140, .228), (.295, .295), (.265, .201), (.095, .185)]
        outer, inner = [], []
        for x, y in outline:
            z = .315-.125*x
            outer.append(m.v((sign*x*width, hy+y, z), 'HEAD'))
            inner.append(m.v((sign*(.17+(x-.17)*.82)*width, hy+.225+(y-.225)*.72, z+.004), 'HEAD'))
        for j in range(5):
            face = (outer[j], outer[(j+1)%5], inner[(j+1)%5], inner[j])
            m.f(tuple(reversed(face)) if sign > 0 else face, 5)
        m.f(tuple(reversed(inner)) if sign > 0 else inner, 5 if expression == 2 else 6)
        if expression != 2:
            m.ellipsoid((sign*.145*width, hy+.220, .306), (.015, .024, .009),
                        region=7, sides=8, rows=4)
            m.ellipsoid((sign*.145*width, hy+.220, .314), (.006, .019, .004),
                        region=5, sides=6, rows=4)
        brow = [(sign*.054, hy+.258, .315), (sign*.135, hy+.288, .307),
                (sign*.257, hy+.332, .278), (sign*.32, hy+.304, .255)]
        m.tube(bezier(brow, 6), [.021, .026, .026, .023, .018, .009], ['HEAD']*6,
               region=0, sides=6)
        # Rebordes de mejilla, no discos pegados a la cara.
        m.tube(bezier([(sign*.27, hy+.15, .272), (sign*.30, hy+.10, .262),
                       (sign*.29, hy+.04, .263), (sign*.25, hy-.015, .278)], 5),
               [.009]*5, ['HEAD']*5, region=3, sides=5)
    ids = [m.v(p, 'HEAD') for p in [(-.027, hy+.15, .303), (.027, hy+.15, .303),
                                   (.035, hy+.04, .311), (0, hy+.015, .344), (-.035, hy+.04, .311)]]
    for face in ((0, 3, 1), (1, 3, 2), (0, 4, 3)):
        m.f(tuple(ids[i] for i in face), 0)
    mouth = bezier([(-.092, hy-.065, .288), (-.035, hy-.080, .303),
                    (.035, hy-.080, .303), (.092, hy-.065, .288)], 7)
    m.tube(mouth, [.005]*7, ['HEAD']*7, region=5, sides=5)
    if expression == 1:
        m.ellipsoid((0, hy-.095, .300), (.055, .035, .012), region=5, sides=10, rows=4)
    paths = [(.33, .53, .72, .82), (.30, .63, .60, .63),
             (.25, .45, .82, .95), (.34, .65, .58, .48),
             (.31, .48, .42, .68), None, (.26, .50, .73, .61), None]
    if paths[k]:
        rootx, outx, midy, endy = paths[k]
        for sign in (1, -1):
            cc = bezier([(sign*rootx, hy+.43, -.06), (sign*outx, hy+.46, -.10),
                         (sign*(outx+.08), hy+midy, -.23), (sign*(outx-.06), hy+endy, -.25)], 11)
            m.tube(cc, np.linspace(.076, .004, 11), ['HEAD']*11, region=3, sides=8)
    if k in (3, 6, 7):
        # Quilla posterior propia, baja y continua con la cúpula.
        m.ellipsoid((0, hy+.56*dome, -.14), (.065, .14+.012*k, .20), region=2, sides=8, rows=5)
    if k == 5:
        for sign in (-1, 1):
            m.ellipsoid((sign*.22, hy+.49, -.14), (.09, .12, .18), region=2, sides=8, rows=5)
    return m.finish(race='freezer', kind='head', variant=k, expression=expression)


def body(race, k):
    m = Mesh()
    bulk = [1.0, 1.13, .92, 1.07][k]
    depth = [1.0, 1.12, .94, 1.05][k]
    if race == 'majin':
        bulk *= [1.12, 1.24, 1.02, 1.15][k]
    # Continuous torso rings, shoulder transition and a common neck seam.
    profiles = [(2.57, .48, .30), (2.88, .56, .33), (3.16, .42, .27),
                (3.48, .48, .30), (3.82, .65, .36), (4.13, .78, .39),
                (4.40, .80, .34), (4.59, .57, .25), (4.75, .28, .19), (4.90, .175, .155)]
    rings = []
    for i, (y, rx, rz) in enumerate(profiles):
        if i == len(profiles)-1:
            ring = m.horizontal((0, y, 0), rx, rz, 'HEAD')
            m.seams['neck'] = ring
        else:
            a, b = ('WAIST', 'CHEST') if y <= 4.25 else ('CHEST', 'HEAD')
            tt = (y-BIND[a][1])/(BIND[b][1]-BIND[a][1])
            bone = 'WAIST' if y < 3.2 else ('STMC' if y < 3.75 else 'CHEST')
            ring = m.horizontal((0, y, 0), rx*(bulk if i < 8 else 1), rz*(depth if i < 8 else 1),
                                bone, a, b, tt)
        if rings:
            region = (2 if i < 3 else 0) if race != 'freezer' else (3 if i < 3 else (1 if 5 <= i <= 7 else 0))
            m.bridge(rings[-1], ring, region)
        rings.append(ring)
    # Crotch closes the abdomen; overlapping upper thighs are hidden inside pants.
    cc = m.v((0, 2.61, 0), 'WAIST')
    for j in range(16):
        m.f((cc, rings[0][(j+1) % 16], rings[0][j]), 2 if race != 'freezer' else 3)
    # Belt is geometry, no copyrighted emblem.
    rr = []
    for y in (3.02, 3.11, 3.20):
        r = m.horizontal((0, y, 0), .435*bulk, .29*depth, 'WAIST', 'WAIST', 'CHEST', (y-2.85)/1.4)
        if rr:
            m.bridge(rr[-1], r, 3 if race != 'freezer' else 2)
        rr.append(r)
    for s in ('L', 'R'):
        # A-pose limbs, tubular quad topology; joint rings remain shared.
        for limb in ('arm', 'leg'):
            if limb == 'arm':
                a, b, c = s+'ARM1', s+'ARM2', s+'HAND'
                rad = [.255, .28, .25, .19, .18, .22, .19, .135, .115]
                rad = [(x*bulk, x*depth) for x in rad]
                region = [0, 0, 0, 0, 0, 0, 3, 3] if race == 'freezer' else [0, 0, 0, 0, 0, 0, 3, 3]
                sides = 10
            else:
                a, b, c = s+'LEG1', s+'LEG2', s+'FOOT1'
                rad = [.31, .33, .29, .23, .22, .22, .19, .155, .14]
                rad = [(x*bulk, x*depth) for x in rad]
                region = [2]*6+[3, 3] if race != 'freezer' else [0]*6+[1, 1]
                sides = 12
            centers, weights, bones = [], [], []
            for j in range(9):
                if j <= 4:
                    aa, bb, t = a, b, j/4
                else:
                    aa, bb, t = b, c, (j-4)/4
                centers.append((1-t)*np.asarray(BIND[aa])+t*np.asarray(BIND[bb]))
                weights.append((aa, bb, t))
                bones.append(aa if t < .999 else bb)
            limb_rings = m.tube(centers, rad, bones, weights, region, sides=sides, caps=True)
            m.seams[s+('_wrist' if limb == 'arm' else '_ankle')] = limb_rings[-1]
            if limb == 'leg':
                sign = 1 if s == 'L' else -1
                # Rounded footwear, authored shape distinct from B3 boots.
                m.ellipsoid((sign*.43, .17, .22), (.205*bulk, .185, .43), s+'FOOT1',
                            3 if race != 'freezer' else 1, sides=12, rows=6)
        # Muscle/armour panels and side ribs are original closed details.
        sign = 1 if s == 'L' else -1
        if race == 'freezer':
            m.ellipsoid((sign*.81*bulk, 4.42, 0), (.35*bulk, .31, .31*depth),
                        s+'ARM1', 0, sides=12, rows=6)
            # Placas alargadas y someras; siguen el pecho y dejan visible el esternón.
            m.ellipsoid((sign*.345*bulk, 4.23, .337*depth), (.355*bulk, .13, .062),
                        'CHEST', 2 if k != 2 else 1, sides=12, rows=5)
            # Caderas y glúteos solapan el arranque de muslo, sin hueco en A-pose.
            m.ellipsoid((sign*.36, 2.72, -.025), (.34*bulk, .35, .315*depth),
                        s+'LEG1', 3, sides=12, rows=5)
            if k in (1, 3):
                m.ellipsoid((sign*.83*bulk, 4.49, -.015), (.39*bulk, .20, .325*depth),
                            s+'ARM1', 1 if k == 1 else 2, sides=12, rows=5)
        elif race == 'namek':
            for j in range(3):
                p = np.asarray(BIND[s+'ARM1'])*(.75-j*.10)+np.asarray(BIND[s+'ARM2'])*(.25+j*.10)
                p[2] += .23*depth
                m.ellipsoid(p, (.10, .055, .045), s+'ARM1', 1, sides=8, rows=4)
    if race == 'freezer':
        points = [np.array((0, 2.72, -.26))]+[np.array(BIND['T_TAIL%d'%i]) for i in range(1, 6)]
        m.tube(points, np.linspace(.20+.01*k, .045, 6), ['WAIST']+['T_TAIL%d'%i for i in range(1, 6)],
               region=[0, 0, 0, 1, 1], sides=10)
    return m.finish(race=race, kind='body', variant=k)


def hand(race, side, pose=0):
    m = Mesh()
    s = 1 if side == 'L' else -1
    bone = side+'HAND'
    root = np.array(BIND[bone])
    tangent = np.array((s*.8, -.6, 0))
    center = root+tangent*.16
    m.ellipsoid(center, (.18, .16, .12), bone, 0, sides=10, rows=5)
    for i in range(4):
        z = (i-1.5)*.060
        ctrl = [root+tangent*.16+(0, 0, z), root+tangent*.28+(0, 0, z),
                root+tangent*(.39 if pose == 0 else .29)+(0, -.02, z+.02),
                root+tangent*(.46 if pose == 0 else .24)+(0, -.04, z+.035)]
        m.tube(bezier(ctrl, 5), [.040, .040, .036, .033, .024], [bone]*5, region=0, sides=6)
    ctrl = [root+(s*.05, -.04, .08), root+(s*.15, -.12, .15),
            root+(s*.20, -.22, .15), root+(s*.22, -.23, .12)]
    m.tube(bezier(ctrl, 6), np.linspace(.05, .025, 6), [bone]*6, region=0, sides=6)
    return m.finish(race=race, kind='hand', side=side, pose=pose)


def catalog():
    rows = []
    for race, base in [('freezer', 3300), ('namek', 3400), ('majin', 3500)]:
        for k, name in enumerate(NOMBRES[race]):
            rows.append(dict(id=base+k, race=race, kind='head', variant=k, name=name,
                             file=f'{race}/cabeza_{k:02}.json', original=True))
        for k, name in enumerate(CUERPOS):
            rows.append(dict(id=base+40+k, race=race, kind='body', variant=k, name=name,
                             file=f'{race}/cuerpo_{k:02}.json', original=True))
    return rows
