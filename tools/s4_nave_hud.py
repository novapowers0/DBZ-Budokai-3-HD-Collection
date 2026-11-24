# -*- coding: utf-8 -*-
"""Banco sin juego del carril S4 (B2-08/B2-09/B2-10/B2S): marcador por planta, etiquetas y reparto del resultado.

Lee la ISO de B2 del jugador (ps2_games) y su ELF: no abre el juego, ni PCSX2, ni la bateria.
  python tools\\s4_nave_hud.py datos                 # tablas del ELF (premios, pagos, limites) y nodos del HUD
  python tools\\s4_nave_hud.py marcador --png DIR    # compone I..IV con valores >0 y coteja con los dorados
  python tools\\s4_nave_hud.py resultado             # simula la pantalla de resultado (duracion y cifras)
  python tools\\s4_nave_hud.py todo --png DIR        # las tres cosas; exit 0 = PASS
Salidas: E:\\DBZ3HD_trabajo\\s4\\banco_nave_s4.json (o --json).
"""
import argparse, json, os, struct, sys

def _raiz():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def _buscar_iso(explicita=None):
    if explicita:
        return explicita
    d = os.environ.get('DBZ3_PS2_GAMES') or os.path.join(_raiz(), 'ps2_games')
    if os.path.isfile(d):
        return d
    for n in sorted(os.listdir(d)):
        if 'budokai 2' in n.lower() and n.lower().endswith('.iso'):
            return os.path.join(d, n)
    raise SystemExit('s4: no encuentro la ISO de Budokai 2 (usa --iso o DBZ3_PS2_GAMES)')

class Iso:
    def __init__(self, path):
        self.f = open(path, 'rb'); self.path = path
        self.f.seek(16 * 2048); pvd = self.f.read(2048)
        if pvd[1:6] != b'CD001':
            raise SystemExit('s4: la ISO no es ISO9660')
        self.raiz = (struct.unpack_from('<I', pvd, 156 + 2)[0], struct.unpack_from('<I', pvd, 156 + 10)[0])

    def _dir(self, lba, size):
        self.f.seek(lba * 2048); d = self.f.read(max(size, 2048)); i = 0
        while i < len(d) and d[i] >= 34:
            yield (struct.unpack_from('<I', d, i + 2)[0], struct.unpack_from('<I', d, i + 10)[0],
                   d[i + 25], d[i + 33:i + 33 + d[i + 32]])
            i += d[i]

    def find(self, nombre):
        cola = [self.raiz]
        for _ in range(4):
            nueva = []
            for lba, size in cola:
                for ext, sz, fl, nm in self._dir(lba, size):
                    n = nm.decode('latin1')
                    if fl == 2 and n not in ('\x00', '\x01'):
                        nueva.append((ext, sz))
                    elif fl == 0 and n.upper().split(';')[0] == nombre.upper():
                        return ext, sz
            cola = nueva
        return None

    def read(self, lba, size, off=0, n=None):
        self.f.seek(lba * 2048 + off)
        return self.f.read(size if n is None else min(n, size - off))

class Afs:
    def __init__(self, data):
        if data[:3] != b'AFS':
            raise SystemExit('s4: AFS invalido')
        n = struct.unpack_from('<I', data, 4)[0]
        self.tab = [struct.unpack_from('<II', data, 8 + 8 * k) for k in range(n)]
        self.data = data

    def __len__(self):
        return len(self.tab)

    def entry(self, k, n=0xFFFFFFFF):
        o, s = self.tab[k]
        return self.data[o:o + min(n, s)]

def u32(d, o):
    return struct.unpack_from('<I', d, o)[0] if o + 4 <= len(d) else 0

def u16(d, o):
    return struct.unpack_from('<H', d, o)[0] if o + 2 <= len(d) else 0

def f32(d, o):
    return struct.unpack_from('<f', d, o)[0] if o + 4 <= len(d) else 0.0

def hijo(amb, c):
    if len(amb) < 0x20 or amb[:4] != b'#AMB' or c >= u32(amb, 0x10):
        return b''
    t = u32(amb, 0x14)
    if t + 16 * (c + 1) > len(amb):
        return b''
    o, sz = u32(amb, t + 16 * c), u32(amb, t + 16 * c + 4)
    return amb[o:o + sz] if o + sz <= len(amb) else b''

class Anim:
    __slots__ = ('x0', 'y0', 'x1', 'y1', 'dur', 'k', 'p', 'pieza')

class Pz:
    __slots__ = ('t', 'tex', 'niebla', 'alpha', 'm', 'u0', 'v0', 'u1', 'v1')

def leer_ama(d, simple=False):
    """#AMA de B2: rect, claves y lineas de piezas. simple=True lee solo la primera pieza (resultado)."""
    out = []
    if len(d) < 16 or d[:4] != b'#AMA':
        return out
    n, base = u32(d, 8), u32(d, 12)
    for i in range(min(n, 1024)):
        o = base + 0x40 * i
        if o + 0x40 > len(d):
            break
        a = Anim()
        a.x0, a.y0, a.x1, a.y1 = f32(d, o), f32(d, o + 4), f32(d, o + 0x10), f32(d, o + 0x14)
        a.dur = float(max(struct.unpack_from('<i', d, o + 0x20)[0], struct.unpack_from('<i', d, o + 0x24)[0]))
        a.k, a.p, a.pieza = [], [], -1
        if not simple:
            na, oa = u32(d, o + 0x30), u32(d, o + 0x34)
            for c in range(min(na, 64)):
                q = oa + 0x20 * c
                if q + 0x20 > len(d):
                    break
                # El cuadro es un entero; leerlo como float colapsa las claves de la barra.
                a.k.append((float(u32(d, q)),) + tuple(f32(d, q + x) for x in (4, 8, 0xC, 0x10, 0x14, 0x18, 0x1C)))
        nb, ob = u32(d, o + 0x38), u32(d, o + 0x3C)
        for c in range(min(nb, 16)):
            q = ob + 0x30 * c
            if q + 0x30 > len(d):
                break
            p = Pz()
            p.t = float(u32(d, q)); p.tex = u32(d, q + 4); p.niebla = u32(d, q + 0xC)
            p.alpha = f32(d, q + 0x1C); p.m = (f32(d, q + 0x10), f32(d, q + 0x14), f32(d, q + 0x18))
            p.u0, p.v0 = f32(d, q + 0x20), f32(d, q + 0x24)
            p.u1, p.v1 = f32(d, q + 0x28) + 1, f32(d, q + 0x2C) + 1
            a.p.append(p)
            if c == 0:
                a.pieza = p.tex
        out.append(a)
    return out

class Amt:
    def __init__(self, d):
        self.d = d
        self.n = int(u32(d, 0x10)) if len(d) >= 0x14 and d[:4] == b'#AMT' else -1

    def info(self, j):
        d = self.d
        if j < 0 or j >= self.n:
            return None
        e = u32(d, u32(d, 0x14) + 4 * j)
        if not e or e + 0x30 > len(d):
            return None
        return dict(psm=u32(d, e + 8), w=u16(d, e + 0x10), h=u16(d, e + 0x12), data=u32(d, e + 0x14),
                    dsize=u32(d, e + 0x18), clut=u32(d, e + 0x24), csize=u32(d, e + 0x28))

    def rgba(self, j):
        d, i = self.d, self.info(j)
        if not i:
            return None
        w, h = i['w'], i['h']
        if not w or not h or w > 2048 or h > 2048 or i['psm'] not in (0x14, 0x13):
            return None
        data, dsize, clut = i['data'] + 0x20, i['dsize'], i['clut'] + 0x20
        nc = min(i['csize'] // 4, 256)
        if clut + 4 * nc > len(d):
            return None
        pal = []
        for k in range(nc):
            s = k
            if nc == 256:
                s = (k & ~0x18) | ((k & 0x08) << 1) | ((k & 0x10) >> 1)      # CSM1
            o = clut + 4 * s
            if o + 4 > len(d):
                return None
            pal.append((d[o], d[o + 1], d[o + 2], min(255, d[o + 3] * 2)))
        out = bytearray(w * h * 4)
        for p in range(w * h):
            if i['psm'] == 0x14:
                o = data + (p >> 1)
                v = (d[o] >> 4 if (p & 1) else d[o] & 0xF) if (p >> 1) < dsize and o < len(d) else 0
            else:
                v = d[data + p] if p < dsize and data + p < len(d) else 0
            out[p * 4:p * 4 + 4] = bytes(pal[v & 0xFF])
        return w, h, bytes(out)

# ---------------------------------------------------------------- tablas del ELF y del AFS
TABLA_ENTRADAS = 0x373C50      # AFS por modo; 6..9 = paquetes de las plantas I..IV
TABLA_CARAS = 0x373BA0
NODOS = {0: 0x373C90, 1: 0x373D64}
CAMPOS = (8, 16, 0x38, 0x3C, 0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74)
PAGOS = (25, 2, 4, 3)          # kPruebas[].pago (0x265AB0)
LIMITES = (-1, 600, 99, 99)    # segundos de combate (I sin reloj)

def va_a_off(d, va):
    ph = u32(d, 28); sz = u16(d, 42); n = u16(d, 44)
    for i in range(n):
        p = ph + i * sz
        if u32(d, p) != 1:
            continue
        off, base, size = u32(d, p + 4), u32(d, p + 8), u32(d, p + 16)
        if base <= va < base + size:
            return off + (va - base)
    return None

def premios_del_elf(elf):
    firma = bytes([0xE6, 0x01, 100, 0, 0xB4, 0, 0x40, 0x01])   # (486,100),(180,320)
    k = elf.find(firma)
    out = []
    while k >= 0 and k + 4 <= len(elf) and len(out) < 64:
        p = (elf[k] | elf[k + 1] << 8, elf[k + 2] | elf[k + 3] << 8)
        if not p[1]:
            break
        out.append(p); k += 4
    return out

class Banco:
    def __init__(self, iso_path=None, elf_path=None):
        self.iso = Iso(_buscar_iso(iso_path))
        lba, sz = self.iso.find('DATA_UK.AFS') or (0, 0)
        if not sz:
            raise SystemExit('s4: DATA_UK.AFS no esta en la ISO')
        self.uk = Afs(self.iso.read(lba, sz))
        if elf_path:
            self.elf = open(elf_path, 'rb').read()
        else:
            lba, sz = self.iso.find('SLES_518.39') or (0, 0)
            if not sz:
                raise SystemExit('s4: no encuentro el ELF de B2 en la ISO')
            self.elf = self.iso.read(lba, sz)
        self.hz = 50 if self.elf else 60                 # SLES = PAL
        self.entradas = [self._u32t(TABLA_ENTRADAS + 4 * (6 + i)) for i in range(4)]
        self.nodos = {l: {c: self._u32t(NODOS[l] + c) for c in CAMPOS} for l in (0, 1)}
        self.premios = premios_del_elf(self.elf)
        e = self.uk.entry(18)                            # comun: reloj (0/1) y cuerpo (3/4)
        self.reloj = (leer_ama(hijo(e, 0)), Amt(hijo(e, 1)))
        self.cuerpo = (leer_ama(hijo(e, 3)), Amt(hijo(e, 4)))
        self.plantas = [(leer_ama(hijo(self.uk.entry(k), 0)), Amt(hijo(self.uk.entry(k), 1))) for k in self.entradas]

    def _u32t(self, va):
        o = va_a_off(self.elf, va)
        return struct.unpack_from('<I', self.elf, o)[0] if o is not None and o + 4 <= len(self.elf) else 0xFFFFFFFF

    def cara(self, b2):
        e = self.uk.entry(self._u32t(TABLA_CARAS + 4 * b2))
        return (leer_ama(hijo(e, 0)), Amt(hijo(e, 1)))

# ---------------------------------------------------------------- composicion (espejo de iw_nave_hud_b2.inl)
def pieza(a, c=0.0):
    if not a.p:
        return None
    p = a.p[0]
    for x in a.p:
        if x.t > c:
            break
        p = x
    return p

def pose(a, c=0.0):
    """PoseAma2 de iw_nave_hud_b2.inl: claves (interpoladas) + linea de piezas (alfa/color) en el cuadro c."""
    if not a.k:
        return dict(x=0.0, y=0.0, z=0.0, sx=1.0, sy=1.0, rot=0.0, a=1.0, m=(1, 1, 1), niebla=0)
    kd = a.k[-1][0]; pd = a.p[-1].t if a.p else 0.0
    ck = min(float(c), kd); cp = min(float(c), pd)
    k0 = k1 = a.k[0]
    for i in range(1, len(a.k)):
        k1 = a.k[i]
        if ck < k1[0]:
            break
        k0 = k1
    f = max(0.0, min(1.0, (ck - k0[0]) / (k1[0] - k0[0]))) if k1[0] > k0[0] else 1.0
    q = dict(x=k0[4] + (k1[4] - k0[4]) * f, y=k0[5] + (k1[5] - k0[5]) * f, z=k0[6] + (k1[6] - k0[6]) * f,
             sx=k0[1] + (k1[1] - k0[1]) * f, sy=k0[2] + (k1[2] - k0[2]) * f,
             rot=k0[3] + (k1[3] - k0[3]) * f, a=k0[7] + (k1[7] - k0[7]) * f,
             m=(1.0, 1.0, 1.0), niebla=0)
    if a.p:
        pa = a.p[0].alpha; q['m'] = a.p[0].m; q['niebla'] = a.p[0].niebla
        for i in range(len(a.p)):
            if cp < a.p[i].t:
                break
            pa = a.p[i].alpha; q['m'] = a.p[i].m; q['niebla'] = a.p[i].niebla
            if i + 1 < len(a.p) and a.p[i + 1].t > a.p[i].t:
                g = max(0.0, min(1.0, (cp - a.p[i].t) / (a.p[i + 1].t - a.p[i].t)))
                pa += (a.p[i + 1].alpha - a.p[i].alpha) * g
                q['m'] = tuple(a.p[i].m[ch] + (a.p[i + 1].m[ch] - a.p[i].m[ch]) * g for ch in range(3))
        q['a'] *= pa
    return q

def _tex(amt, j, m=None, niebla=0):
    r = amt.rgba(j)
    if not r:
        return None
    w, h, raw = r
    if m is None or (m == (1, 1, 1) and not niebla):
        return (w, h, raw)
    px = bytearray(raw); f = (niebla >> 24) / 255.0
    for i in range(0, len(px), 4):
        for ch in range(3):
            px[i + ch] = max(0, min(255, int(px[i + ch] * m[ch] * (1 - f) + ((niebla >> (8 * ch)) & 255) * f)))
    return (w, h, bytes(px))

def poner(cola, lista, amt, nodo, c=0.0, tex=None, dx=0.0, cifra=-1, barra=False, bake=False):
    if nodo < 0 or nodo >= len(lista):
        return False
    src = lista[nodo]; pz = pieza(src, c)
    if not pz:
        return False
    t = pz.tex if tex is None else tex
    u0, v0, u1, v1 = pz.u0, pz.v0, pz.u1, pz.v1
    if cifra >= 0:                                   # atlas de cifras 5x2 (FUN_001BE260)
        ww, hh = pz.u1 - pz.u0, pz.v1 - pz.v0
        u0 = ww * (cifra % 5); u1 = u0 + ww
        v = 0.0 if cifra < 5 else hh
        v1 = v0 + v + hh; v0 = v0 + v
    if barra:                                        # FUN_001C44A0
        w = abs((src.x1 - src.x0) * pose(src, c)['sx'])
        u0, u1 = (max(0.0, w - 1), 1.0) if src.x0 < 0 else (0.0, w)
    im = _tex(amt, t, pz.m if bake else None, pz.niebla if bake else 0)
    if not im:
        return False
    q = pose(src, c); q['x'] += dx
    cola.append(dict(a=src, q=q, tex=im, u0=u0, v0=v0, u1=u1, v1=v1))
    return True

def numero(cola, lista, amt, ancla, cero, valor):
    if ancla < 0 or cero < 0 or ancla >= len(lista) or cero >= len(lista):
        return False
    a, z = lista[ancla], lista[cero]
    qa, qz = pose(a), pose(z)
    if not poner(cola, lista, amt, ancla):
        return False
    x = qa['x'] + abs(a.x1 - a.x0)
    for ch in str(max(0, min(999999, int(valor)))):
        if not poner(cola, lista, amt, cero, 0.0, None, x - qz['x'], int(ch)):
            return False
        cola[-1]['cifra'] = True
        x += abs(z.x1 - z.x0)
    return True

def componer(b, planta, vencidos=0, golpes=0, ondas=0, cuadros=0.0, limite=5940,
             vida=(3000, 3000), ki=(3000, 3000), ki_max=(7000, 7000), b2=(0, 0), cara=(0, 0)):
    """Marcador de combate de la planta `planta` (0..3), como lo pinta iw_nave_hud_b2.inl."""
    cola = []
    cuerpo, cuerpob = b.cuerpo
    reloj, relojb = b.reloj

    def retrato(lado):
        c = b.cara(b2[lado]); p = pieza(c[0][0], float(cara[lado]))
        return (p and poner(cola, cuerpo, cuerpob, b.nodos[lado][0x4C]) and
                poner(cola, cuerpo, c[1], b.nodos[lado][0x50], 0.0, p.tex) and
                poner(cola, cuerpo, cuerpob, b.nodos[lado][0x54]))

    def barras(lado):
        v = max(0.0, min(700.0, vida[lado] * 0.1)); k = max(0.0, min(700.0, ki[lado] * 0.1))
        if not (poner(cola, cuerpo, cuerpob, b.nodos[lado][8], v) and
                poner(cola, cuerpo, cuerpob, b.nodos[lado][16], v, None, 0.0, -1, True) and
                poner(cola, cuerpo, cuerpob, b.nodos[lado][0x5C])):
            return False
        if not poner(cola, cuerpo, cuerpob, b.nodos[lado][0x64]):
            return False
        cola[-1]['recorte'] = (int(k) % 100) / 100.0 if int(k) < int(ki_max[lado] * 0.1) else 1.0
        if int(k) < int(ki_max[lado] * 0.1):
            return poner(cola, cuerpo, relojb, b.nodos[lado][0x68], 0.0, None, 0.0, int(k) // 100)
        return poner(cola, cuerpo, relojb, b.nodos[lado][0x6C])

    if not retrato(0):
        return None
    if planta == 0 and not retrato(1):
        return None
    if planta <= 1 and not barras(0):
        return None
    if planta == 0 and not barras(1):
        return None
    p, pb = b.plantas[planta]
    if planta == 0:
        cr = b.cara(b2[1]); pz = pieza(cr[0][0], float(cara[1]))
        if not (poner(cola, p, cuerpob, 1) and poner(cola, p, pb, 6) and pz and
                poner(cola, p, cr[1], 2, 0.0, pz.tex) and numero(cola, p, relojb, 4, 5, vencidos)):
            return None
    elif planta == 1:
        seg = max(0, min(999, (int(cuadros) + 59) // 60))
        if not poner(cola, p, pb, 1):
            return None
        for i, div in enumerate((100, 10, 1)):
            if not poner(cola, p, relojb, 2 + i, 0.0, None, 0.0, (seg // div) % 10):
                return None
    else:
        cuenta = golpes if planta == 2 else ondas
        if not (poner(cola, p, pb, 1) and poner(cola, p, pb, 2) and
                numero(cola, p, relojb, 5 if planta == 2 else 3, 6 if planta == 2 else 4, cuenta)):
            return None
        seg = max(0, min(99, (max(0, limite - int(cuadros)) + 59) // 60))
        if not (poner(cola, reloj, relojb, 2, 0.0, None, 0.0, seg // 10) and
                poner(cola, reloj, relojb, 3, 0.0, None, 0.0, seg % 10)):
            return None
    return cola

def raster(cola, w=640, h=448):
    try:
        import numpy as np
    except ImportError:
        return None
    img = np.zeros((h, w, 4), np.uint8)
    for o in sorted(cola, key=lambda o: -o['q']['z']):
        a, q = o['a'], o['q']; tw, th, raw = o['tex']
        tex = np.frombuffer(raw, np.uint8).reshape(th, tw, 4)
        x0, y0 = a.x0 * q['sx'], a.y0 * q['sy']; x1, y1 = (a.x1 + 1) * q['sx'], (a.y1 + 1) * q['sy']
        if 'recorte' in o:
            x1 = x0 + (x1 - x0) * o['recorte']
        if x1 == x0 or y1 == y0:
            continue
        X0, X1 = int(q['x'] + x0), int(q['x'] + x1) + 1
        Y0, Y1 = int(q['y'] + y0), int(q['y'] + y1) + 1
        for Y in range(max(0, Y0), min(h, Y1)):
            for X in range(max(0, X0), min(w, X1)):
                u = o['u0'] + (o['u1'] - o['u0']) * ((X + 0.5 - q['x'] - x0) / (x1 - x0))
                v = o['v0'] + (o['v1'] - o['v0']) * ((Y + 0.5 - q['y'] - y0) / (y1 - y0))
                sr, sg, sb, sa = tex[min(th - 1, max(0, int(v))), min(tw - 1, max(0, int(u)))]
                if q['m'] != (1.0, 1.0, 1.0):                 # multiplicador de color de la pieza (0x10..0x18)
                    sr = min(255, int(sr * q['m'][0])); sg = min(255, int(sg * q['m'][1]))
                    sb = min(255, int(sb * q['m'][2]))
                al = (sa / 255.0) * q['a']
                if al <= 0:
                    continue
                dst = img[Y, X]
                img[Y, X, 0] = np.uint8(sr * al + dst[0] * (1 - al))
                img[Y, X, 1] = np.uint8(sg * al + dst[1] * (1 - al))
                img[Y, X, 2] = np.uint8(sb * al + dst[2] * (1 - al))
                img[Y, X, 3] = np.uint8(min(255, int(sa) + int(dst[3] * (1 - al))))
    return img

# ---------------------------------------------------------------- medidas
def datos(b, png_dir=None):
    r = {'elf': len(b.elf), 'hz': b.hz, 'afs_entradas': len(b.uk), 'premios': len(b.premios),
         'premios_kili': [p[1] for p in b.premios], 'entradas_planta': b.entradas,
         'nodos': b.nodos, 'paginas': {}, 'etiquetas': {}, 'fallos': []}
    for i in range(4):
        r['paginas'][str(i + 1)] = dict(pago=PAGOS[i], limite=LIMITES[i], entrada=b.entradas[i],
                                        nodos=len(b.plantas[i][0]), texturas=b.plantas[i][1].n)
    if len(b.premios) != 43:
        r['fallos'].append('premios %d != 43' % len(b.premios))
    if not all(0 < e < len(b.uk) for e in b.entradas):
        r['fallos'].append('entradas de planta fuera del AFS: %s' % b.entradas)
    try:
        lba, sz = b.iso.find('DATA_CMN.AFS')
        n = struct.unpack_from('<I', b.iso.read(lba, sz, 0, 8), 4)[0]
        tab = b.iso.read(lba, sz, 8, 8 * n)
        o, s = struct.unpack_from('<II', tab, 8 * 905)
        ama = leer_ama(b.iso.read(lba, sz, o, s), simple=True)
        amt = Amt(b.uk.entry(1131))
        tiras = []
        for k, nombre in ((92, 'wins'), (93, 'seconds'), (94, 'Hits'), (95, 'times')):
            a = ama[k]
            im = amt.rgba(a.pieza) if k < len(ama) and a.p else None
            r['etiquetas'][k] = dict(esperado=nombre, pieza=a.pieza if k < len(ama) else -1,
                                     textura=('{}x{}'.format(im[0], im[1]) if im else None))
            if not im:
                r['fallos'].append('etiqueta %d sin textura' % k)
        if png_dir:                       # hoja de las cuatro palabras (comprobacion a ojo, sin OCR)
            try:
                from PIL import Image
                for k in (92, 93, 94, 95):
                    a = ama[k]; im = amt.rgba(a.pieza)
                    if im:
                        tiras.append(Image.frombytes('RGBA', (im[0], im[1]), im[2]))
                if tiras:
                    os.makedirs(png_dir, exist_ok=True)
                    hoja = Image.new('RGBA', (sum(t.width + 8 for t in tiras), max(t.height for t in tiras) + 8),
                                     (25, 25, 25, 255))
                    x = 4
                    for t in tiras:
                        hoja.alpha_composite(t, (x, 4)); x += t.width + 8
                    hoja.save(os.path.join(png_dir, 'palabras.png'))
            except ImportError:
                pass
    except Exception as e:                       # noqa: BLE001 - banco: se informa, no se cae
        r['fallos'].append('etiquetas: %s' % e)
    r['ok'] = not r['fallos']
    return r

def marcador(b, png_dir=None):
    r = {'plantas': {}, 'contador_III': {}, 'cotejo': {}, 'fallos': []}
    casos = {0: dict(vencidos=1, cuadros=30, vida=(1124, 1000)), 1: dict(cuadros=50, vida=(1000, 3000)),
             2: dict(golpes=10, cuadros=53), 3: dict(ondas=0, cuadros=10)}
    for pl, kw in casos.items():
        cola = componer(b, pl, b2=(30, 0), **kw)
        if cola is None:
            r['fallos'].append('planta %d no compone' % (pl + 1)); continue
        r['plantas'][str(pl + 1)] = dict(quads=len(cola))
        if png_dir:
            try:
                from PIL import Image
                os.makedirs(png_dir, exist_ok=True)
                Image.fromarray(raster(cola), 'RGBA').save(os.path.join(png_dir, 'planta%d.png' % (pl + 1)))
            except ImportError:
                pass
    for g in (2, 10, 17):
        cola = componer(b, 2, golpes=g, cuadros=53, b2=(30, 0))
        n = sum(1 for q in cola if q.get('cifra') and q['q']['x'] > 150)
        r['contador_III'][str(g)] = n
        if n != len(str(g)):
            r['fallos'].append('contador III con %d: %d quads de cifra (esperado %d)' % (g, n, len(str(g))))
    fotos = {'III_golpes_10': (2, dict(golpes=10, cuadros=53)),
             'III_contador_2': (2, dict(golpes=2, cuadros=80)),
             'pre_p3_f2': (2, dict(golpes=0, cuadros=53)),
             'pre_p4_f2': (3, dict(ondas=0, cuadros=53))}
    base = r'E:\DBZ3HD_trabajo'
    rutas = {'III_golpes_10': os.path.join(base, 'dorados_0911', 'b2_nave_0911', 'evidencia', 'III_golpes_10.png'),
             'III_contador_2': os.path.join(base, 'dorados_0911', 'b2_nave_0911', 'evidencia', 'III_contador_2.png'),
             'pre_p3_f2': os.path.join(base, 'R7', 'emu', 'capturas_pre', 'pre_p3_f2.png'),
             'pre_p4_f2': os.path.join(base, 'R7', 'emu', 'capturas_pre', 'pre_p4_f2.png')}
    try:
        import cv2
        import numpy as np
        from PIL import Image
        for nom, (pl, kw) in fotos.items():
            if not os.path.exists(rutas[nom]):
                continue
            foto = np.asarray(Image.open(rutas[nom]).convert('RGB'))
            comp = raster(componer(b, pl, b2=(30, 0), **kw))
            al = comp[:, :, 3].astype('float32') / 255.0
            mejor = None
            for sx in (0.78, 0.79, 0.80, 0.81, 0.82):
                for sy in (1.0, 1.05, 1.06, 1.07, 1.0714, 1.08, 1.14):
                    M = np.float32([[sx, 0, 0], [0, sy, 0]])
                    w, h = int(round(640 * sx)), int(round(448 * sy))
                    rr = cv2.warpAffine(comp[:, :, :3].astype('float32'), M, (w, h))
                    mm = (cv2.warpAffine(al, M, (w, h)) > 0.5).astype('float32')
                    if h > foto.shape[0] or w > foto.shape[1] or mm.sum() < 100:
                        rr = rr[:foto.shape[0], :foto.shape[1]]; mm = mm[:foto.shape[0], :foto.shape[1]]
                    res = cv2.matchTemplate(foto.astype('float32'), rr, cv2.TM_CCORR_NORMED,
                                            mask=np.dstack([mm] * 3))
                    res = np.nan_to_num(res, nan=-1, posinf=-1, neginf=-1)
                    _, mx, _, ml = cv2.minMaxLoc(res)
                    if mejor is None or mx > mejor[0]:
                        mejor = (float(mx), sx, sy, ml[0], ml[1])
            r['cotejo'][nom] = dict(corr=round(mejor[0], 4), sx=mejor[1], sy=mejor[2], dx=mejor[3], dy=mejor[4])
            if mejor[0] < 0.75:
                r['fallos'].append('cotejo %s: corr %.3f' % (nom, mejor[0]))
            if abs(mejor[1] - 0.80) > 0.02 or abs(mejor[2] - 1.0714) > 0.03:
                r['fallos'].append('cotejo %s: encuadre %.3f/%.4f' % (nom, mejor[1], mejor[2]))
    except ImportError:
        r['cotejo'] = {'aviso': 'sin cv2/numpy/PIL: no se coteja con los dorados'}
    r['ok'] = not r['fallos']
    return r

def simular_resultado(cuenta, pago, hz=50):
    """Espejo de PasoResultadoCampo (0x263700/0x263670/0x262DD0/0x262CC0), sin pulsar nada."""
    total = cuenta * pago
    campos = 0
    if total > 0:
        campos += 1                      # el campo que ve ganado >= total
        campos += 3 * total              # fase 0: la cuenta sube 1 Kili cada 3 campos
        campos += 41                     # fase 1 (espera > 40)
        campos += 3 * total              # fase 2: la barra, 1 Kili cada 3 campos
        campos += 41                     # fase 4
    else:
        campos += 1 + 41                 # sin Kili acaba en la fase 1
    return dict(campos=campos, segundos=round(campos / float(hz), 2), kili=total)

def resultado(b):
    r = {'pantallas': {}, 'fallos': []}
    for pl, cuenta in ((2, 17), (2, 0), (3, 0), (1, 2)):
        r['pantallas']['planta%d_%dhits' % (pl + 1, cuenta)] = simular_resultado(cuenta, PAGOS[pl], b.hz)
    iii = r['pantallas']['planta3_17hits']; iv = r['pantallas']['planta4_0hits']
    if not (7.0 <= iii['segundos'] <= 10.0):
        r['fallos'].append('III con 17 golpes: %.2f s fuera de 7-10 s' % iii['segundos'])
    if not (0.5 <= iv['segundos'] <= 2.0):
        r['fallos'].append('IV con 0: %.2f s fuera de 0,5-2 s' % iv['segundos'])
    if iii['kili'] != 68:
        r['fallos'].append('III: %d Kili != 68' % iii['kili'])
    r['original_10_11'] = {'III_fin_torre_cuadros': 778, 'IV_fin_torre_cuadros': 771,
                           'segundos': [round(778 / 50.0, 1), round(771 / 50.0, 1)],
                           'nota': 'el dorado viejo (b2_nave.json) daba 528-697 cuadros; el del 10/11 manda'}
    r['ok'] = not r['fallos']
    return r

def main():
    ap = argparse.ArgumentParser(description='Banco sin juego del marcador de la Nave de B2 (carril S4)')
    ap.add_argument('que', nargs='?', default='todo', choices=['datos', 'marcador', 'resultado', 'todo'])
    ap.add_argument('--iso'); ap.add_argument('--elf'); ap.add_argument('--json'); ap.add_argument('--png')
    a = ap.parse_args()
    b = Banco(a.iso, a.elf)
    r = {}
    if a.que in ('datos', 'todo'):
        r['datos'] = datos(b, a.png)
    if a.que in ('marcador', 'todo'):
        r['marcador'] = marcador(b, a.png)
    if a.que in ('resultado', 'todo'):
        r['resultado'] = resultado(b)
    ok = all(v.get('ok', True) for v in r.values())
    if a.que == 'todo':
        d, m, s = r['datos'], r['marcador'], r['resultado']
        print('S4 nave: ELF %d B, %d premios; plantas %s; etiquetas %s' %
              (d['elf'], d['premios'], d['entradas_planta'], [v['esperado'] for v in d['etiquetas'].values()]))
        for k, v in s['pantallas'].items():
            print('  resultado %-16s %5.2f s  %d Kili' % (k, v['segundos'], v['kili']))
        for k, v in m.get('cotejo', {}).items():
            if 'corr' in v:
                print('  cotejo %-14s corr %.3f  sx %.3f  sy %.4f' % (k, v['corr'], v['sx'], v['sy']))
        for x in d['fallos'] + m['fallos'] + s['fallos']:
            print('  FALLO', x)
    out = a.json or r'E:\DBZ3HD_trabajo\s4\banco_nave_s4.json'
    try:
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, 'w', encoding='utf-8') as f:
            json.dump(r, f, indent=1, ensure_ascii=False)
        print('json:', out)
    except OSError as e:
        print('s4: no pude escribir %s (%s)' % (out, e))
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1

if __name__ == '__main__':
    sys.exit(main())
