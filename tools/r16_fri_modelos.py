"""Arcosiano modular de autor: solamente primitivas y perfiles propios.

No abre discos ni modelos. Y vertical, Z frontal; unidades del maniquí propio.
Los ocho cascos tienen secciones diferentes; cuernos/cara/gema son independientes.
"""
import math
import numpy as np
from cac_nativas_diseno import Mesh, BIND, bezier, eye, normals
from r16_fri_bocetos import NAMES

PALETTE = ['dedcc5', '315e6a', 'd99b48', '38414b', '9aa9a0', '172028', 'fff4d8', 'da644b']
FORMS = ('base', 'f_fre_2', 'f_fre_3', 'f_fre_final', 'f_fre_100', 'f_fre_cooler')
HY = BIND['HEAD'][1]


def wedge(m, outline, depth=.025, bone='HEAD', region=1):
    """Placa biselada convexa; contorno XY, frente Z, centro propio elevado."""
    pts = np.asarray(outline, float)
    # Orden frontal CCW, independiente de cómo se escribió el boceto.
    if np.cross(pts[1]-pts[0], pts[2]-pts[0])[2] < 0:
        pts = pts[::-1]
    outer = [m.v(p, bone) for p in pts]
    c = pts.mean(axis=0)
    inner = [m.v(c+(p-c)*.78+(0, 0, depth), bone) for p in pts]
    middle = m.v(c+(0, 0, depth*1.35), bone)
    for i in range(len(pts)):
        j = (i+1) % len(pts)
        m.f((outer[i], outer[j], inner[j], inner[i]), region)
        m.f((middle, inner[i], inner[j]), region)
    m.f(tuple(reversed(outer)), region)


def loft(m, rows, bone='HEAD', regions=0, seam=None):
    prev = None
    for i, (y, rx, rz, z) in enumerate(rows):
        ring = m.horizontal((0, y, z), rx, rz, bone)
        if i == 0 and seam:
            m.seams[seam] = ring
        if prev:
            m.bridge(prev, ring, regions[i-1] if isinstance(regions, list) else regions)
        prev = ring
    peak = m.v((0, rows[-1][0]+.045, rows[-1][3]), bone)
    for j in range(16):
        m.f((peak, prev[j], prev[(j+1)%16]), regions[-1] if isinstance(regions, list) else regions)


def head(k):
    m = Mesh()
    # Mejillas/ojos neutros, sin cuernos ni mandíbula/máscara predeterminados.
    rows = [(4.90, .175, .155, 0), (5.03, .18, .17, .005),
            (5.17, .225, .22, .018), (5.34, .31, .265, 0),
            (5.51, .345, .28, -.012), (5.67, .34, .27, -.025)]
    crown = [
        [(5.82,.30,.25,-.03),(5.96,.20,.18,-.035),(6.015,.07,.07,-.04)],
        [(5.80,.34,.42,-.18),(5.87,.29,.49,-.24),(5.90,.15,.33,-.25)],
        [(5.82,.28,.27,-.045),(5.92,.15,.23,-.09),(5.97,.04,.10,-.10)],
        [(5.82,.30,.27,-.03),(5.93,.23,.22,-.05),(5.96,.09,.12,-.05)],
        [(5.77,.35,.31,-.07),(5.86,.29,.27,-.08),(5.91,.12,.14,-.08)],
        [(5.75,.36,.30,.005),(5.82,.26,.25,-.035),(5.86,.09,.10,-.035)],
        [(5.72,.48,.25,-.03),(5.85,.46,.23,-.055),(5.89,.16,.14,-.055)],
        [(5.83,.26,.24,-.05),(6.07,.15,.16,-.09),(6.26,.022,.025,-.14)],
    ][k]
    loft(m, rows+crown, regions=[0]*5+[1]*len(crown), seam='neck')
    if k in (2, 3):
        for x in ([0.] if k == 2 else [-.17, .17]):
            # Aleta tridimensional, no un cambio de escala del cráneo.
            centers = [(x,5.77,.02),(x,6.14,-.10),(x,6.17,-.39),(x,5.87,-.49)]
            m.tube(centers, [(.075,.08),(.055,.10),(.04,.085),(.018,.03)], ['HEAD']*4, region=1, sides=6)
    if k == 4:
        for y,z,rx in [(5.78,-.12,.33),(5.88,-.21,.27),(5.95,-.30,.18)]:
            m.tube([(-rx,y,z),(0,y+.025,z+.10),(rx,y,z)], [.028]*3, ['HEAD']*3, region=3, sides=6)
    if k == 5:
        wedge(m, [(-.35,5.69,.17),(-.23,5.83,.25),(0,5.86,.32),(.23,5.83,.25),(.35,5.69,.17),(0,5.64,.36)], .025)
    for s in (-1, 1):
        eye(m, s*.16, 5.57, .276, s, tilt=-.12)
    return m


def horns(k):
    m = Mesh()
    if k == 5:
        return m
    paths = [
        [(.30,5.73,-.04),(.48,5.74,-.06),(.77,5.75,-.10),(.94,5.79,-.14)],
        [(.30,5.73,-.04),(.64,5.83,-.08),(.67,6.19,-.10),(.40,6.36,-.13)],
        [(.30,5.72,-.07),(.45,5.76,-.32),(.50,5.82,-.65),(.36,5.89,-.86)],
        [(.30,5.73,-.05),(.83,5.69,-.10),(.93,6.10,-.20),(.71,6.25,-.22)],
        [(.30,5.73,-.06),(.37,5.76,-.07),(.42,5.82,-.08),(.43,5.87,-.09)],
    ][k]
    for s in (-1, 1):
        path = np.asarray(paths)*[s,1,1]
        cc = bezier(path, 9 if k != 4 else 5)
        radii = np.linspace(.092 if k != 3 else .12, .006 if k != 4 else .035, len(cc))
        m.tube(cc, radii, ['HEAD']*len(cc), region=3, sides=8)
        if k == 3:
            cc = bezier([(s*.71,6.25,-.22),(s*.61,6.37,-.23),(s*.59,6.52,-.24),(s*.66,6.62,-.25)], 6)
            m.tube(cc, np.linspace(.015,.003,6), ['HEAD']*6, region=3, sides=8)
    return m


def face(k, expression=0):
    m = Mesh()
    if k == 0:
        outline = [(-.235,5.35,.235),(.235,5.35,.235),(.13,5.15,.24),(0,5.10,.25),(-.13,5.15,.24)]
        wedge(m, outline, .03, region=0)
    elif k == 1:
        wedge(m, [(-.235,5.41,.25),(.235,5.41,.25),(.18,5.13,.25),(-.18,5.13,.25)], .085, region=1)
        for y,w in [(5.34,.14),(5.27,.115),(5.20,.085)]:
            m.tube([(-w,y,.348),(0,y,.37),(w,y,.348)], [.007]*3, ['HEAD']*3, region=5, sides=5)
    elif k == 2:
        wedge(m, [(-.27,5.36,.22),(.27,5.36,.22),(.24,5.10,.22),(-.24,5.10,.22)], .045, region=0)
        for s in (-1,1):
            wedge(m, [(s*.19,5.42,.24),(s*.35,5.48,.19),(s*.32,5.28,.22),(s*.23,5.23,.25)], .035, region=1)
    else:
        m.ellipsoid((0,5.19,.237), (.17,.12,.060), region=0, sides=12, rows=5)
        for s in (-1,1):
            cc = bezier([(s*.29,5.49,.22),(s*.38,5.40,.20),(s*.30,5.31,.27),(s*.21,5.29,.28)], 6)
            m.tube(cc, [.035,.04,.04,.032,.025,.014], ['HEAD']*6, region=1, sides=6)
    if k != 1:
        wedge(m, [(-.024,5.49,.282),(.024,5.49,.282),(.035,5.39,.30),(0,5.36,.335),(-.035,5.39,.30)], .007, region=0)
        cc = bezier([(-.09,5.27,.305),(-.035,5.25-expression*.01,.326),(.035,5.25-expression*.01,.326),(.09,5.27,.305)], 6)
        m.tube(cc, [.006]*6, ['HEAD']*6, region=5, sides=5)
        if expression == 1:
            m.ellipsoid((0,5.235,.32),(.045,.028,.009),region=5,sides=8,rows=4)
    return m


def body():
    m = Mesh()
    rows = [(2.65,.42,.27),(2.87,.46,.30),(3.15,.38,.25),(3.49,.44,.27),
            (3.86,.60,.32),(4.19,.73,.35),(4.43,.76,.30),(4.63,.51,.22),
            (4.79,.22,.165),(4.90,.175,.155)]
    prev = None
    for i,(y,rx,rz) in enumerate(rows):
        bone = 'WAIST' if y<3.3 else 'STMC' if y<3.8 else 'CHEST' if y<4.79 else 'HEAD'
        r = m.horizontal((0,y,0),rx,rz,bone)
        if prev:
            m.bridge(prev,r,0 if i>2 else 3)
        prev = r
    m.seams['neck'] = prev
    for s,sign in [('L',1),('R',-1)]:
        # Superposición interior corta en las articulaciones, sin huecos abiertos.
        for limb in ('ARM','LEG'):
            a,b,c = (s+'ARM1',s+'ARM2',s+'HAND') if limb=='ARM' else (s+'LEG1',s+'LEG2',s+'FOOT1')
            rad = [.24,.255,.22,.19,.18,.19,.17,.13,.115] if limb=='ARM' else [.275,.29,.26,.22,.215,.22,.19,.16,.14]
            centers, bones, binds = [],[],[]
            for j in range(9):
                aa,bb,t = (a,b,j/4) if j<=4 else (b,c,(j-4)/4)
                centers.append((1-t)*np.asarray(BIND[aa])+t*np.asarray(BIND[bb]))
                bones.append(aa if t<.999 else bb)
                binds.append((aa,bb,t))
            m.tube(centers,rad,bones,binds,region=0,sides=10 if limb=='ARM' else 12)
        m.ellipsoid((sign*.79,4.44,0),(.30,.27,.28),s+'ARM1',0,sides=12,rows=5)
        m.ellipsoid((sign*.36,2.74,-.015),(.29,.28,.27),s+'LEG1',0,sides=12,rows=5)
        m.ellipsoid((sign*.43,.17,.23),(.19,.17,.40),s+'FOOT1',3,sides=12,rows=5)
    return m


def plates(k):
    m = Mesh()
    for sign,s in [(-1,'R'),(1,'L')]:
        if k == 0:
            m.ellipsoid((sign*.34,4.24,.322),(.31,.15,.075),'CHEST',1,sides=12,rows=5)
            m.ellipsoid((sign*.86,4.49,0),(.30,.13,.30),s+'ARM1',1,sides=12,rows=5)
        elif k == 1:
            wedge(m, [(sign*.07,4.28,.35),(sign*.64,4.43,.26),(sign*.56,4.21,.32),(sign*.10,4.01,.32)], .045,'CHEST',1)
            wedge(m, [(sign*.69,4.52,.12),(sign*1.15,4.63,-.12),(sign*1.09,4.35,.16),(sign*.77,4.32,.22)], .035,s+'ARM1',1)
        elif k == 2:
            for j in range(3):
                y=4.39-j*.16
                wedge(m, [(sign*.11,y,.345),(sign*.58,y+.04,.30),(sign*.54,y-.10,.325),(sign*.13,y-.13,.355)],.025,'CHEST',1)
                m.ellipsoid((sign*(.78+.075*j),4.51-.05*j,-.03),(.14,.10,.27),s+'ARM1',1,sides=8,rows=4)
        else:
            wedge(m, [(sign*.64,4.54,.18),(sign*1.13,4.54,.12),(sign*1.12,4.27,.20),(sign*.71,4.25,.26)],.04,s+'ARM1',1)
    if k == 3:
        wedge(m, [(0,4.53,.30),(.57,4.26,.34),(0,3.89,.33),(-.57,4.26,.34)], .065,'CHEST',1)
    return m


def gem(k):
    m=Mesh()
    if k==0:
        m.ellipsoid((0,4.37,.423),(.075,.11,.035),'CHEST',2,sides=10,rows=5)
    elif k==1:
        wedge(m,[(0,4.52,.407),(.10,4.37,.407),(0,4.23,.407),(-.10,4.37,.407)],.05,'CHEST',2)
    else:
        for x,y in [(-.082,4.44),(.082,4.44),(0,4.30)]:
            m.ellipsoid((x,y,.427),(.052,.052,.03),'CHEST',2,sides=8,rows=4)
    return m


def tail(k):
    m=Mesh()
    centers = [(0,2.72,-.24)]+[BIND['T_TAIL%d'%i] for i in range(1,6)]
    bones = ['WAIST']+['T_TAIL%d'%i for i in range(1,6)]
    radii = [.18,.17,.145,.115,.08,.025] if k!=2 else [.18,.16,.14,.12,.10,.065]
    if k==1:
        radii=[(r*.72,r*1.24) for r in radii]
    m.tube(centers,radii,bones,region=[0,0,0,1,1],sides=10)
    if k==1:
        for i in (3,4,5):
            x,y,z=centers[i]
            m.ellipsoid((x,y+.13,z),(.025,.20,.22),bones[i],1,sides=6,rows=4)
    if k==2:
        m.ellipsoid(centers[-1],(.15,.14,.25),bones[-1],1,sides=10,rows=5)
        for i in (3,4):
            m.ellipsoid(centers[i],(.14,.14,.12),bones[i],3,sides=8,rows=4)
    return m


def hand(side):
    m=Mesh()
    sign=1 if side=='L' else -1
    bone=side+'HAND'
    root=np.asarray(BIND[bone])
    direction=np.array((sign*.8,-.6,0))
    m.ellipsoid(root+direction*.13,(.145,.13,.13),bone,0,sides=10,rows=5)
    for j in range(3):
        z=(j-1)*.085
        m.tube([root+direction*t+(0,0,z) for t in (.14,.24,.31,.34)],
               [.043,.042,.038,.024],[bone]*4,region=0,sides=6)
    m.tube([root+(sign*.01,-.04,.10),root+(sign*.11,-.16,.17),root+(sign*.18,-.20,.15)],
           [.052,.044,.026],[bone]*3,region=0,sides=6)
    return m


def build(slot,k=0,form='base',expression=0):
    makers={'cabeza':lambda:head(k),'cuernos':lambda:horns(k),'cara':lambda:face(k,expression),
            'placas':lambda:plates(k),'gema':lambda:gem(k),'cola':lambda:tail(k),
            'base':body,'mano_L':lambda:hand('L'),'mano_R':lambda:hand('R')}
    m=makers[slot]()
    if not m.p:
        return dict(P=[],N=[],tris=[],bones=[],bindings=[],regions=[],seams={},slot=slot,variant=k,form=form,original=True)
    # Sólo se transforman nuestros vértices, jamás un modelo de referencia.
    for i,(p,bone) in enumerate(zip(m.p,m.bones)):
        p=np.asarray(p,float)
        origin=np.asarray(BIND[bone])
        delta=p-origin
        if form=='f_fre_2':
            delta*=([1.15,1.1,1.1] if slot=='cuernos' else [1.06,1.02,1.04])
        elif form=='f_fre_3':
            if slot=='cabeza' and p[1]>5.68:
                delta[2]-=.22*min((p[1]-5.68)/.3,1)
            if slot=='placas': delta[0]*=.94
        elif form=='f_fre_final':
            if slot=='cuernos': delta*=.75
        elif form=='f_fre_100':
            if slot in ('base','placas'): delta*=[1.10,1.,1.10]
        elif form=='f_fre_cooler':
            if slot in ('placas','cuernos'): delta*=[1.13,1.10,1.12]
        q=origin+delta
        # El contrato de cuello no depende ni del tipo ni de la transformación.
        if i not in m.seams.get('neck',[]): m.p[i]=tuple(q)
    result=m.finish(slot=slot,variant=k,form=form,expression=expression,original=True,
                    procedencia='original, made for this project')
    if 'neck' in m.seams:
        # Normales compartidas en la unión: elimina salto de luz entre mitades.
        for i in m.seams['neck']:
            p=np.asarray(result['P'][i]); n=np.array([p[0]/.175,0,p[2]/.155]); n/=np.linalg.norm(n)
            result['N'][i]=n.tolist()
    return result
