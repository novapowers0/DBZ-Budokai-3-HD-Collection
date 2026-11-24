"""NMC: 29 piezas de autor, sin importar geometría ni imágenes de juegos.

Ejes propios X derecha, Y arriba, Z frente. La adaptación al anfitrión es
posterior al modelado; no se consulta su superficie para crear nuestras piezas.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path

import numpy as np

VERSION = 1
ROOT = Path(__file__).resolve().parents[2]
PALETA = {'piel': (.37, .64, .33, 1), 'musculo': (.76, .43, .48, 1),
          'tela': (.24, .32, .41, 1), 'ribete': (.75, .62, .35, 1),
          'tinta': (.055, .07, .08, 1), 'ojos': (.94, .94, .82, 1)}
NOMBRES = {
    'cabeza': ('Semilla', 'Aguja', 'Bastión', 'Estela', 'Cáliz', 'Guijarro', 'Órbita', 'Duna'),
    'antenas': ('Espiral', 'Brote', 'Tallo', 'Gemelas', 'Viento', 'Sauce'),
    'orejas': ('Lanza', 'Remo', 'Hoja', 'Muesca'),
    'mandibula': ('Serena', 'Firme', 'Proa', 'Velo'),
    'parches': ('Costillas', 'Rombos', 'Canales'),
    'ropa': ('Surco', 'Nómada', 'Umbral', 'Estrato'),
}
BASES = dict(zip(NOMBRES, (3600, 3620, 3640, 3660, 3680, 3700)))
# Posiciones de un rig dibujado desde cero, no medidas de un personaje comercial.
RIG = {'WAIST': ((0, 3.05, 0), None), 'STMC': ((0, 3.8, 0), 'WAIST'),
       'CHEST': ((0, 4.65, 0), 'STMC'), 'NECK': ((0, 5.35, 0), 'CHEST'),
       'HEAD': ((0, 5.72, 0), 'NECK')}
for side, s in (('L', 1), ('R', -1)):
    RIG.update({side+'ARM1': ((s*.76, 4.75, 0), 'CHEST'),
                side+'ARM2': ((s*1.65, 4.55, 0), side+'ARM1'),
                side+'HANDROT': ((s*2.43, 4.4, 0), side+'ARM2'),
                side+'LEG1': ((s*.36, 2.95, 0), 'WAIST'),
                side+'LEG2': ((s*.39, 1.65, .03), side+'LEG1'),
                side+'FOOT1': ((s*.4, .4, .02), side+'LEG2')})


def catalogo():
    return [dict(id=BASES[slot]+i, tipo=slot, nombre='Tipo %d · %s'%(i+1, name),
                 raza='namek', version=VERSION, licencia='CC0-1.0', original=True)
            for slot, names in NOMBRES.items() for i, name in enumerate(names)]


def catalogo_b3():
    zones = {'cabeza': 'torso', 'orejas': 'piernas', 'parches': 'guantes', 'ropa': 'botas'}
    return [dict(p, tipo='original_'+zones[p['tipo']], idx_afs=-1,
                 archivo='nmc:'+str(p['id']), cuerpos=[11])
            for p in catalogo() if p['tipo'] in zones]


def seleccion(ficha):
    active = any(3600 <= int(ficha.get('b3_'+z, -1)) < 3720
                 for z in ('torso', 'piernas', 'guantes', 'botas'))
    if not active:
        return {}
    if int(ficha.get('cuerpo', -1)) != 11:
        raise ValueError('Piezas NMC en cuerpo de otra raza')
    sel = {}
    for slot, zone in [('cabeza','torso'), ('orejas','piernas'), ('parches','guantes'), ('ropa','botas')]:
        ident = int(ficha.get('b3_'+zone, -1))
        if ident == -1:
            ident = BASES[slot]
        if not BASES[slot] <= ident < BASES[slot]+len(NOMBRES[slot]):
            raise ValueError('Pieza incompatible en '+slot)
        sel[slot] = ident-BASES[slot]
    for slot, field in [('antenas','pelo'), ('mandibula','cara')]:
        # ArgsFicha serializa Heroe::cara como «rostro», no como «cara».
        value = int(ficha.get('_nmc_'+slot, ficha.get('rostro', ficha.get(field, 0))
                             if slot == 'mandibula' else ficha.get(field, 0)))
        if not 0 <= value < len(NOMBRES[slot]):
            raise ValueError('Tipo NMC fuera de rango: '+slot)
        sel[slot] = value
    return sel


def ficha_sin_injertos(ficha):
    """Conserva antena/mandíbula y desactiva los injertos genéricos de pelo/cara."""
    sel = seleccion(ficha)
    if not sel:
        return ficha
    return dict(ficha, _nmc_antenas=sel['antenas'], _nmc_mandibula=sel['mandibula'], pelo=0, cara=0)


class Malla:
    def __init__(self):
        self.p, self.t, self.h, self.r, self.uv = [], [], [], [], []
        self.costuras = {}

    def v(self, p, bone, region, uv=(.5,.5)):
        self.p.append(tuple(float(x) for x in p)); self.h.append(bone)
        self.r.append(region); self.uv.append(uv)
        return len(self.p)-1

    def face(self, *vs):
        for j in range(1, len(vs)-1):
            self.t.append((vs[0],vs[j],vs[j+1]))

    def loft(self, rows, bone, region='piel', sides=16, caps=(True,True), crown=0):
        """Filas (x,y,z,radio_x,radio_z); UV cilíndrica propia."""
        rings = []
        for i, (x,y,z,rx,rz) in enumerate(rows):
            ring = []
            for j in range(sides):
                a = 2*math.pi*j/sides
                yy = y + crown * math.cos(2*a) * (i/max(len(rows)-1,1))**2
                ring.append(self.v((x+rx*math.cos(a),yy,z+rz*math.sin(a)),
                                   bone(i) if callable(bone) else bone,region,(j/sides,i/max(1,len(rows)-1))))
            if rings:
                for j in range(sides):
                    q = (j+1)%sides
                    self.face(rings[-1][q],rings[-1][j],ring[j],ring[q])
            rings.append(ring)
        for end, enabled in ((0,caps[0]),(-1,caps[1])):
            if enabled:
                c = self.v(rows[end][:3], self.h[rings[end][0]], region)
                for j in range(sides):
                    q=(j+1)%sides
                    self.face(c, rings[end][j if end==0 else q], rings[end][q if end==0 else j])
        return rings

    def tubo(self, points, radii, bone='HEAD', region='piel', sides=8):
        pts=np.asarray(points,float)
        tang=np.gradient(pts,axis=0)
        tang/=np.maximum(np.linalg.norm(tang,axis=1,keepdims=True),1e-9)
        rings=[]
        for i,(p,r) in enumerate(zip(pts,radii)):
            ref=np.array([0.,1.,0.]) if abs(tang[i,1])<.9 else np.array([1.,0.,0.])
            u=np.cross(tang[i],ref);u/=np.linalg.norm(u);v=np.cross(tang[i],u)
            ring=[self.v(p+r*(math.cos(2*math.pi*j/sides)*u+math.sin(2*math.pi*j/sides)*v),
                         bone(i) if callable(bone) else bone,region,(j/sides,i/max(1,len(pts)-1))) for j in range(sides)]
            if rings:
                for j in range(sides):
                    q=(j+1)%sides
                    self.face(rings[-1][j],rings[-1][q],ring[q],ring[j])
            rings.append(ring)
        for end in (0,-1):
            c=self.v(pts[end],self.h[rings[end][0]],region)
            for j in range(sides):
                q=(j+1)%sides
                self.face(c,rings[end][q if end==0 else j],rings[end][j if end==0 else q])

    def ell(self, c, r, bone='HEAD', region='piel', sides=12, rows=6):
        # Sin radios cero: polos explícitos para evitar triángulos degenerados.
        yvals=[math.pi*i/rows for i in range(1,rows)]
        coords=[(c[0],c[1]-r[1]*math.cos(a),c[2],r[0]*math.sin(a),r[2]*math.sin(a)) for a in yvals]
        rings=self.loft(coords,bone,region,sides,caps=(False,False))
        for end,sgn in ((0,-1),(-1,1)):
            v=self.v((c[0],c[1]+sgn*r[1],c[2]),bone,region)
            for j in range(sides):
                q=(j+1)%sides
                self.face(v,rings[end][j if end==0 else q],rings[end][q if end==0 else j])

    def fin(self, meta):
        p=np.asarray(self.p,float); t=np.asarray(self.t,int)
        n=np.zeros_like(p)
        fn=np.cross(p[t[:,1]]-p[t[:,0]],p[t[:,2]]-p[t[:,0]])
        if np.any(np.linalg.norm(fn,axis=1)<1e-10):
            raise ValueError('Triángulo degenerado: '+str(meta))
        for j in range(3): np.add.at(n,t[:,j],fn)
        mag=np.linalg.norm(n,axis=1)
        if np.any(mag<1e-10): raise ValueError('Normal indeterminada')
        n/=mag[:,None]
        for ring in self.costuras.values():
            center=p[ring].mean(0)
            radial=p[ring]-center; radial[:,1]=0
            radial/=np.linalg.norm(radial,axis=1,keepdims=True)
            n[ring]=radial
        return dict(meta,P=p.tolist(),N=n.tolist(),tris=t.tolist(),UV=self.uv,
                    huesos=self.h,regiones=self.r,colores=PALETA,costuras=self.costuras,
                    rig={k:dict(pos=v[0],parent=v[1]) for k,v in RIG.items()},
                    procedencia='original, made for this project',licencia='CC0-1.0',version=VERSION)


# Cada perfil usa filas/radios nuevos. No se escala ni deforma una cabeza B3.
PERFILES = (
    (.41,.35,6.30,.00,.00), (.31,.34,6.59,.01,.00),
    (.49,.35,6.26,.00,.00), (.36,.48,6.43,-.17,.00),
    (.46,.34,6.39,.00,.075), (.40,.32,6.17,.02,.00),
    (.34,.38,6.53,-.01,.00), (.40,.43,6.40,-.14,.035),
)


def cabeza(m,k):
    rx,rz,top,back,crown=PERFILES[k]
    rows=[(0,5.35,0,.12,.12),(0,5.52,.02,.29,.27),
          (0,5.74,.015,rx,rz),(0,5.98,back*.35,rx,rz*.97),
          (0,top-.08,back,rx*.65,rz*.70),(0,top,back,.045,.045)]
    rings=m.loft(rows,lambda i:'NECK' if i==0 else 'HEAD',caps=(False,True),crown=crown)
    m.costuras['cuello']=rings[0]
    for s in (-1,1):
        # Ojos sólidos, cejas en cuña de piel y pupilas que no dependen de atlas ajenos.
        # Anclajes derivados del perfil PROPIO para no enterrar ojos en cráneos profundos.
        face_z = back*.14+.009+rz*.988*math.sqrt(1-(.15/rx)**2)
        m.ell((s*.15,5.84,face_z+.018),(.115,.052,.033),region='ojos',rows=4)
        m.ell((s*.15,5.84,face_z+.05),(.025,.037,.009),region='tinta',sides=8,rows=4)
        brow=[(s*.035,5.91+(k%3)*.014,rz+back*.20+.02),
              (s*.15,5.96+(k%2)*.035,face_z+.022),
              (s*min(.31,rx*.85),5.92-(k%4)*.012,face_z-.07)]
        m.tubo(brow,[.043,.048,.014],sides=6)
    m.ell((0,5.76,rz+.01),(.055,.10,.085),sides=8,rows=4)
    # Relieve de surcos longitudinales o transversales, diferente por tipo.
    for j in range(1+k%3):
        x=(j-(k%3)/2)*.10
        m.tubo([(x,6.0,rz*.93),(x*.8,top-.12,rz*.63),(x*.7,top-.045,rz*.2+back)],
               [.009,.014,.006],region='musculo',sides=5)


def antenas(m,k):
    for s in (-1,1):
        curves=(
            [(.20,6.01,.25),(.32,6.37,.37),(.53,6.62,.35),(.66,6.55,.31),(.64,6.43,.32),(.54,6.44,.34)],
            [(.20,6.01,.25),(.23,6.17,.28),(.26,6.24,.27)],
            [(.20,6.01,.25),(.34,6.25,.29),(.48,6.43,.31)],
            [(.20,6.01,.25),(.32,6.27,.33),(.48,6.48,.35)],
            [(.20,6.01,.25),(.26,6.24,.03),(.30,6.25,-.33),(.35,6.15,-.57)],
            [(.20,6.01,.25),(.42,6.20,.28),(.63,6.06,.30),(.67,5.78,.32)],
        )
        pts=[(s*x,y,z) for x,y,z in curves[k]]
        rad=.075 if k==2 else .037
        m.tubo(pts,np.linspace(rad,.014,len(pts)),sides=8)
        if k==3:
            m.tubo([(s*.28,6.19,.31),(s*.24,6.41,.41),(s*.23,6.56,.46)], [.032,.026,.009],sides=7)


def orejas(m,k):
    shapes=(
        [(.33,5.76,.02),(.42,5.98,.02),(.91,6.12,-.04),(.69,5.77,.03),(.39,5.65,.04)],
        [(.33,5.76,0),(.41,5.97,-.02),(.69,6.00,-.59),(.65,5.77,-.46),(.39,5.65,.01)],
        [(.33,5.76,0),(.40,5.96,0),(.59,5.98,-.06),(.64,5.85,-.06),(.39,5.65,.02)],
        [(.33,5.76,0),(.42,5.98,0),(.88,6.10,-.07),(.72,5.94,-.03),(.78,5.89,-.04),(.60,5.85,0),(.39,5.65,.03)],
    )
    for s in (-1,1):
        pts=np.asarray(shapes[k],float);pts[:,0]*=s
        c=pts.mean(0)
        front=m.v(c+(0,0,.055),'HEAD','piel')
        back=m.v(c-(0,0,.035),'HEAD','piel')
        ring=[m.v(p,'HEAD','piel') for p in pts]
        for j in range(len(ring)):
            q=(j+1)%len(ring)
            # Triangulación en abanico conserva la muesca de la silueta.
            m.face(front,ring[j if s>0 else q],ring[q if s>0 else j])
            m.face(back,ring[q if s>0 else j],ring[j if s>0 else q])
        # La cara interior tiene región propia: ningún triángulo mezcla atlas.
        inner=[m.v(c+(p-c)*.63+(0,0,.025),'HEAD','musculo') for p in pts]
        mid=m.v(c+(0,0,.058),'HEAD','musculo')
        for j in range(len(inner)):
            q=(j+1)%len(inner)
            m.face(mid,inner[j if s>0 else q],inner[q if s>0 else j])


def mandibula(m,k):
    width=(.24,.32,.23,.31)[k]
    depth=(.31,.32,.43,.37)[k]
    m.ell((0,5.59,.13),(width,.14,depth-.10),region='tela' if k==3 else 'piel',rows=4)
    if k==3:
        for j in range(3):
            m.tubo([(-.27,5.67-j*.055,.31),(0,5.65-j*.055,.393),(.27,5.67-j*.055,.31)],
                   [.011,.014,.011],region='ribete',sides=5)
    else:
        m.tubo([(-width*.57,5.615,depth),(0,5.60+(k==1)*.012,depth+.012),
                (width*.57,5.615,depth)],[.009,.012,.009],region='tinta',sides=5)


def parches(m,k):
    for s,side in ((1,'L'),(-1,'R')):
        for bone,x,y in ((side+'ARM1',1.13,4.67),(side+'ARM2',1.98,4.48)):
            if k==0:
                for j in range(4):
                    m.ell((s*(x+(j-1.5)*.093),y,.24),(.035,.16,.025),bone,'musculo',sides=8,rows=4)
            elif k==1:
                for j in range(3):
                    m.ell((s*(x+(j-1)*.13),y+(j-1)*.065,.245),(.084,.12,.029),bone,'musculo',sides=4,rows=4)
            else:
                for j in (-1,1):
                    m.ell((s*x,y+j*.10,.242),(.22,.043,.025),bone,'musculo',sides=8,rows=4)


def cuerpo(m):
    rows=[(0,2.88,0,.51,.30),(0,3.15,0,.56,.32),(0,3.55,0,.47,.28),
          (0,4.0,0,.57,.31),(0,4.55,0,.73,.37),(0,4.82,0,.69,.33),
          (0,5.05,0,.31,.23),(0,5.35,0,.12,.12)]
    rings=m.loft(rows,lambda i:'WAIST' if i<2 else 'STMC' if i<4 else 'CHEST' if i<6 else 'NECK',caps=(True,False))
    m.costuras['cuello']=rings[-1]
    for side,s in (('L',1),('R',-1)):
        arm=[(s*.61,4.77,0),(s*.93,4.73,0),(s*1.33,4.62,0),
             (s*1.65,4.55,0),(s*1.90,4.49,0),(s*2.23,4.43,0),(s*2.43,4.4,0)]
        m.tubo(arm,[.25,.29,.25,.21,.25,.19,.12],lambda i:side+('ARM1' if i<=3 else 'ARM2'),sides=12)
        m.ell((s*2.58,4.38,.01),(.23,.12,.17),side+'HANDROT',rows=4)
        for f in range(4):
            z=(f-1.5)*.082
            m.tubo([(s*2.69,4.38,z),(s*(2.93-abs(f-1.5)*.045),4.36,z)], [.035,.025],side+'HANDROT',sides=6)
        m.tubo([(s*2.53,4.36,.13),(s*2.63,4.22,.23)], [.055,.028],side+'HANDROT',sides=7)
        leg=[(s*.34,2.98,0),(s*.37,2.57,0),(s*.39,2.03,.02),(s*.39,1.65,.03),
             (s*.40,1.27,.01),(s*.40,.77,0),(s*.40,.40,.02)]
        m.tubo(leg,[.30,.31,.27,.20,.24,.19,.13],lambda i:side+('LEG1' if i<=3 else 'LEG2'),sides=12)
        m.ell((s*.40,.24,.18),(.20,.16,.39),side+'FOOT1',rows=4)


def ropa(m,k):
    region='ribete' if k==3 else 'tela'
    # Cintura y pantalones comunes dibujados aquí, separados del cuerpo neutro.
    for s,side in ((1,'L'),(-1,'R')):
        m.tubo([(s*.35,2.99,0),(s*.38,2.5,0),(s*.39,1.68,.03)],
               [.335,.36 if k==0 else .32,.23],lambda i:side+('LEG1' if i<2 else 'LEG2'),'tela',sides=12)
        if k==3:
            m.ell((s*.40,.99,.17),(.22,.46,.11),side+'LEG2','ribete',sides=8,rows=4)
    if k==0:
        m.loft([(0,3.27,0,.61,.37),(0,3.7,0,.52,.34),(0,4.3,0,.70,.41),(0,4.75,0,.76,.38)],
               lambda i:'STMC' if i<2 else 'CHEST','tela',caps=(True,False))
        m.loft([(0,3.28,0,.62,.38),(0,3.49,0,.59,.37)],'STMC','ribete')
        m.tubo([(-.46,4.78,.30),(-.10,4.25,.43),(.37,3.52,.37)],[.055,.05,.04],'CHEST','ribete',sides=6)
        m.tubo([(.38,3.38,.34),(.49,3.03,.36),(.58,2.73,.39)],[.085,.07,.048],'WAIST','ribete',sides=6)
    elif k==1:
        m.loft([(0,3.88,0,.62,.37),(0,4.55,0,.77,.42),(0,4.91,0,.37,.28)],'CHEST','tela')
        m.loft([(0,5.02,0,.36,.28),(0,5.20,0,.32,.25)],'NECK','ribete')
        m.tubo([(.18,5.12,-.22),(.29,4.75,-.44),(.40,4.22,-.44)],[.13,.11,.08],'CHEST','ribete',sides=6)
    elif k==2:
        for s in (-1,1):
            m.ell((s*.53,3.64,-.04),(.21,.67,.39),'STMC','tela',sides=10,rows=6)
        # Capucha abierta delante: arco transversal, no turbante ni capa larga.
        points=[(-.47,5.35,-.09),(-.49,5.91,-.20),(-.31,6.44,-.24),
                (0,6.55,-.24),(.31,6.44,-.24),(.49,5.91,-.20),(.47,5.35,-.09)]
        m.tubo(points,[.09,.11,.13,.13,.13,.11,.09],region='tela',sides=8)
    else:
        for j in range(3):
            m.ell((0,4.60-j*.24,.31),(.70-j*.065,.15,.13),'CHEST','ribete',sides=10,rows=4)
        m.loft([(0,2.91,0,.65,.37),(0,3.24,0,.60,.36)],'WAIST','ribete')


def generar(ident, forma='base'):
    if forma not in ('base','f_nam_nail','f_nam_kami'):
        raise ValueError('Forma Namek desconocida')
    meta=next((p for p in catalogo() if p['id']==ident),None)
    if meta is None: raise ValueError('ID Namek desconocido')
    m=Malla();slot=meta['tipo'];k=ident-BASES[slot]
    globals()[slot](m,k)
    out=m.fin(dict(meta,forma=forma))
    # Potenciaciones conservan anatomía y selección, sin inventar otra especie.
    return out


def base_corporal():
    m=Malla();cuerpo(m)
    return m.fin(dict(id=3599,tipo='base',nombre='Base neutra NMC',forma='base'))


def combinar(piezas):
    out=dict(piezas[0]);out.update(P=[],N=[],tris=[],UV=[],huesos=[],regiones=[],costuras={})
    for part in piezas:
        offset=len(out['P'])
        for key in ('P','N','UV','huesos','regiones'):out[key].extend(part[key])
        out['tris'].extend([[v+offset for v in t] for t in part['tris']])
    return out


def recurso(ident, forma='base', juego=None):
    folder=(Path(juego) if juego else ROOT)/'res/heroe/namek_original'
    manifest=json.loads((folder/'catalogo.json').read_text(encoding='utf-8'))
    name='%s/%d.json'%(forma,ident)
    record=next((x for x in manifest['archivos'] if x['archivo']==name),None)
    if record is None:raise ValueError('Pieza ausente del manifiesto NMC: '+name)
    payload=(folder/name).read_bytes()
    if hashlib.sha256(payload).hexdigest()!=record['sha256']:
        raise ValueError('Pieza modificada sin regenerar manifiesto: '+name)
    mesh=json.loads(payload)
    if mesh['id']!=ident or mesh['forma']!=forma:raise ValueError('Identidad de recurso NMC incorrecta')
    return mesh


def colores_ficha(ficha):
    """Traduce los cuatro HSV persistentes, sin confundirlos con RGB."""
    import colorsys
    from vista_heroe import objetivo
    from toon_regiones import validar_colores
    colors=dict(PALETA)
    for i,region in enumerate(('piel','musculo','tela','ribete')):
        parts=ficha.get('partes',[])
        hsv=parts[i] if i<len(parts) else (0,0,0)
        if region=='piel' and any(ficha.get('piel',(0,0,0))):hsv=ficha['piel']
        if any(hsv):
            h,s,v=objetivo(hsv)
            colors[region]=(*colorsys.hsv_to_rgb(h/360,s,v),1.)
    if any(ficha.get('ojos',(0,0,0))):
        h,s,v=objetivo(ficha['ojos'])
        colors['ojos']=(*colorsys.hsv_to_rgb(h/360,s,v),1.)
    # Contrato de terraCAC_TOON; los HSV anteriores ya tienen guardado existente.
    keys={'piel':'piel','placas':'musculo','gemas':'ribete','ropa':'tela'}
    for key,rgb in validar_colores(ficha.get('colores_regiones',{})).items():
        colors[keys[key]]=(*(c/255 for c in rgb),1.)
    return colors


def recalzar(mesh, model):
    """Adaptación continua del rig propio, sin consultar la superficie B3.

    Un campo cúbico 2D interpola los anclajes anatómicos. Todos los vértices
    comparten el mismo campo, también en uniones con pesos rígidos distintos;
    recalzar cada hueso por separado abría hombros y cintura en bind-pose.
    """
    import partes_nativas as pn
    names=list(mesh['rig'])
    indices=[pn._resolve(name,model.labels) for name in names]
    if any(i is None for i in indices):raise ValueError('Rig NMC sin correspondencia completa')
    source=np.asarray([mesh['rig'][name]['pos'] for name in names],float)
    worlds=np.asarray(model.worlds(),float)
    target=worlds[indices,:3,3]
    scale=np.linalg.norm(target[names.index('HEAD')]-target[names.index('WAIST')])/np.linalg.norm(source[names.index('HEAD')]-source[names.index('WAIST')])
    if not np.isfinite(scale) or not .1<scale<100:raise ValueError('Escala NMC inválida')
    anchors=source[:,:2]
    distance=np.linalg.norm(anchors[:,None,:]-anchors[None,:,:],axis=2)
    affine=np.column_stack((np.ones(len(names)),anchors))
    system=np.block([[distance**3,affine],[affine.T,np.zeros((3,3))]])
    displacement=target-source*scale
    coefficients=np.linalg.solve(system,np.vstack((displacement,np.zeros((3,3)))))
    weights,linear=coefficients[:-3],coefficients[-3:]
    P,N=np.asarray(mesh['P'],float),np.asarray(mesh['N'],float)
    delta=P[:,None,:2]-anchors[None,:,:]
    radius=np.linalg.norm(delta,axis=2)
    Q=P*scale+(radius**3)@weights+np.column_stack((np.ones(len(P)),P[:,:2]))@linear
    derivative=3*radius[:,:,None]*delta
    J=np.tile(np.eye(3)*scale,(len(P),1,1))
    J[:,:,:2]+=np.einsum('nki,kj->nji',derivative,weights)+linear[1:].T
    determinant=np.linalg.det(J)
    if not np.isfinite(Q).all() or np.min(determinant)<=0:raise ValueError('Recalce NMC invertido o no finito')
    normals=np.linalg.solve(J.transpose(0,2,1),N[...,None])[...,0]
    normals/=np.linalg.norm(normals,axis=1,keepdims=True)
    # El atlas compartido recibe ya las coordenadas destino y no debe adaptarlas
    # otra vez: rig destino, escala 1 y anclaje rígido identidad por vértice.
    return dict(mesh,P=Q.tolist(),N=normals.tolist(),
                rig={name:dict(mesh['rig'][name],pos=target[i].tolist()) for i,name in enumerate(names)},
                bindings=[(name,name,0.) for name in mesh['huesos']])


def aplicar_b3(amb,ficha,juego=None):
    sel=seleccion(ficha)
    if not sel:return bytes(amb),{}
    import model_render as mr
    import partes_nativas as pn
    import injertar_ropa as ir
    from awg_vertex_buffer import AwgVertexBuffer
    from b3_gateway import DrawTable
    import texture_b3 as tb
    forma=ficha.get('_nmc_forma','base')
    parts=[recurso(3599,'base',juego)]+[recurso(BASES[s]+k,forma,juego) for s,k in sel.items()]
    mesh=combinar(parts);mesh['colores']=colores_ficha(ficha)
    model=mr.Model(bytes(amb))
    mesh=recalzar(mesh,model)
    data=pn._atlas(mesh,model,dict(ficha,partes=[]))
    hosts=[i for i,d in enumerate(model.table.draws) if d['prim']==5
           and not model.mats[d['material']]['env']
           and model.mats[d['material']]['tex']<len(model.tex)]
    if not hosts:raise ValueError('Sin draw compatible NMC')
    host=max(hosts,key=lambda i:model.table.draws[i]['B'][1])
    material=model.table.draws[host]['material'];tex=model.mats[material]['tex']
    # El alfa B3 selecciona sombreado, NO transparencia. Pillow/LANCZOS en RGBA
    # premultiplica y borra el RGB con alfa 0: redimensionar las celdas sin mezcla.
    height,width=model.tex[tex].shape[:2]
    atlas=data['rgba'];ah,aw=atlas.shape[:2]
    data['rgba']=atlas[(np.arange(height)*ah//height)[:,None],
                       (np.arange(width)*aw//width)[None,:]].copy()
    job=dict(data,host=host,quitar=np.ones(len(model.tris),bool),tex_destino=tex,zona='nmc_original_completo')
    result,report=ir.injertar_ropa_multi(bytes(amb),model,[job])
    avb=AwgVertexBuffer._parse(result);dt=DrawTable(avb)
    _,textures=tb.parse_azt(result)
    ramp=next((t['idx'] for t in textures if t['idx']!=tex and t['size']-128==t['w']*t['h']),None)
    if ramp is None:raise ValueError('Sin espacio para rampa de autor NMC')
    gray=np.zeros((64,64,4),np.uint8);gray[...,3]=255
    gray[:,:20,:3]=72;gray[:,20:39,:3]=34
    ir._escribir_textura(avb.data,ramp,gray)
    flags=dt.materials[material]['flags']
    dt.set_material(material,tex=tex,ramp=ramp,flags=(flags[0]&~0x40,flags[1]))
    result=bytes(avb.data)
    for awg in range(1,model.avb.n_awg):
        aux=AwgVertexBuffer._parse(result,awg=awg);table=DrawTable(aux)
        for d in table.draws:table.set_range(d,d['A'][0],d['A'][1],d['B'][0],0)
        result=bytes(aux.data)
    report.update(original=True,raza='namek',seleccion=sel,forma=forma,
                  vertices=len(mesh['P']),triangulos=len(mesh['tris']),
                  colores_rgb={k:[round(c*255) for c in v[:3]] for k,v in mesh['colores'].items()},
                  aviso='Aceptación visual, manos expresivas y combate real pendientes')
    return result,report


def formas_b3(afs,fid,ficha,juego,resumen,construir):
    if not seleccion(ficha):return None
    if int(ficha.get('donante',11))!=11:return []
    import roster_build as rb
    from variantes_formas import construir_formas, formas_raza
    # Las potenciaciones de Piccolo usan su MISMO modelo: no indexar modelos
    # de traje como si fuesen transformaciones. Verificar número en el donante.
    count=int(rb.IDS[11]['forms'])-1
    if rb.IDS[11].get('form_models') != [[11,0],[11,0],[11,0]]:
        raise ValueError('Cambió la relación forma/modelo del donante Namek')
    names=('f_nam_nail','f_nam_kami')
    # La copia del juego distribuye res/heroe completo, pero no todos los TOML
    # junto a los scripts. La proyección de reglas viaja con nuestras piezas.
    rules=(Path(juego) if juego else ROOT)/'res/heroe/namek_original/reglas_formas.toml'
    if formas_raza(2,rules)!=names:raise ValueError('Reglas de formas NMC incompatibles')
    if not 0<=count<=len(names):raise ValueError('Cadena Namek no reconocida')
    enlaces=[(name,i+1,fid) for i,name in enumerate(names[:count])]
    def build(blob,actual,root,source):
        # construir_formas conserva pelo/cara; jamás pierde nuestras selecciones.
        return construir(blob,dict(actual,_nmc_forma=names[build.index]),root,source)
    out=[]
    for j,link in enumerate(enlaces):
        build.index=j
        out.extend(construir_formas(afs,ficha,juego,resumen,build,2,[link],{},ruta=rules))
    return out
