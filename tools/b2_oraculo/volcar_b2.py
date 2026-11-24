# SPDX-License-Identifier: MIT
"""B2-2/B2-3: volcado del original de Budokai 2 con el oraculo (oraculo_b2.py). Gemelo de E:/DBZ3HD_trabajo/iw_volcado
(volcar.py + texturas.py), del que reutiliza la VRAM del GS (texturas.Vram) y el traductor (gs_primitivas).

  python tools/b2_oraculo/volcar_b2.py correr NOMBRE [--desde punto.pkl] [--pulsa ...] [--parar V] [--captura-desde V]
                                              [--guardar V=ruta] [--limite N]
      -> E:/DBZ3HD_trabajo/b2_volcado/NOMBRE/{gs.jsonl, sonido.jsonl, spx.jsonl, texto.txt, resumen.json} y _datos/<md5>.bin
  python tools/b2_oraculo/volcar_b2.py ver NOMBRE VBLANK[,VBLANK...]   -> NOMBRE/ver_<vblank>.png (sprites 2D del cuadro)
  python tools/b2_oraculo/volcar_b2.py tex NOMBRE                      -> NOMBRE/tex/<xxh3 texeles>_<xxh3 clut>.png + texturas.json
"""
import argparse
import hashlib
import json
import os
import struct
import sys
from collections import Counter
from pathlib import Path

AQUI = Path(__file__).parent
sys.path.insert(0, str(AQUI))
sys.path.insert(0, str(AQUI.parent / 'iw_mips'))
sys.path.insert(0, r'E:\DBZ3HD_trabajo\iw_volcado')      # texturas.py (Vram del GS validada contra PCSX2)
SAL = Path(r'E:\DBZ3HD_trabajo\b2_volcado')
DAT = SAL / '_datos'


def correr(a):
    import ee
    import gs_captura
    import oraculo as o
    import oraculo_b2 as b2
    out = SAL / a.nombre
    out.mkdir(parents=True, exist_ok=True)
    DAT.mkdir(parents=True, exist_ok=True)

    class Guarda:      # las subidas IMAGE y los MPG se guardan enteros (sin repetir) en _datos/<md5>.bin
        def __init__(s, b):
            s.b = bytes(b)
            s.h = hashlib.md5(s.b).hexdigest()
            p = DAT / (s.h + '.bin')
            if not p.exists():
                p.write_bytes(s.b)

        def hexdigest(s):
            return s.h
    gs_captura.hashlib = type('h', (), {'md5': staticmethod(Guarda)})

    estado = {'n': 0}
    sonido = (out / 'sonido.jsonl').open('w')
    call0 = o.Iop.call

    def call(s, c):
        cli, fno, env, tam = c.r[4] & ee.M32, c.r[5] & ee.M32, c.r[7] & ee.M32, c.r[8] & ee.M32
        sid = s.sid.get(cli, 0)
        if sid != 0x80000400 and estado['n'] >= a.captura_desde:
            datos = bytes(c.r8(env + i) for i in range(min(tam, 64))) if env else b''
            sonido.write(json.dumps({'n': estado['n'], 'sid': hex(sid), 'fno': hex(fno), 'tam': tam, 'env': datos.hex()}) + '\n')
        return call0(s, c)
    o.Iop.call = call
    vb0 = o.Reloj.vblank

    def vblank(s, c):
        estado['n'] = s.n
        return vb0(s, c)
    o.Reloj.vblank = vblank
    a.captura, a.spx = str(out / 'gs.jsonl'), str(out / 'spx.jsonl')
    fin = 'ok'
    try:
        b2.arranque(a)
    except BaseException as ex:
        fin = f'{type(ex).__name__}: {ex}'
    sonido.close()
    json.dump({'nombre': a.nombre, 'desde': a.desde, 'pulsa': a.pulsa, 'parar': a.parar, 'captura_desde': a.captura_desde,
               'fin': fin, 'ultimo_vblank': estado['n']}, (out / 'resumen.json').open('w'), indent=1)
    print('FIN', a.nombre, fin, estado['n'])


def cuadros(nombre):
    """Recorre gs.jsonl rehaciendo la VRAM: da (n, primitiva, vram) por cada primitiva nueva, con las subidas ya puestas."""
    import gs_primitivas as G
    import texturas
    vr = texturas.Vram()
    for l in (SAL / nombre / 'gs.jsonl').open(encoding='utf-8'):
        f = json.loads(l)
        t, hechas = G.Traductor(), 0
        for od in f['ordenes']:
            if od[0] == 'G':
                antes = len(t.prims)
                t.registro(od[1], od[2])
                if len(t.prims) > antes:
                    for b, tp, rg, d in t.subidas[hechas:]:
                        vr.subir(b, tp, rg, d)
                    hechas = len(t.subidas)
                    yield f['n'], t.prims[-1], vr
            elif od[0] == 'I':
                p = DAT / (od[2] + '.bin')
                t.imagen(p.read_bytes() if p.exists() else bytes(od[1] * 16))
        for b, tp, rg, d in t.subidas[hechas:]:
            vr.subir(b, tp, rg, d)
        yield f['n'], None, vr


def uv(p, i, w, h):
    v = p[17][i]
    if p[0] & 0x100:                                   # FST: UV en 1/16 de texel
        return (v[7] & 0x3FFF) / 16, ((v[7] >> 16) & 0x3FFF) / 16
    q = struct.unpack('<f', struct.pack('<I', v[5]))[0] or 1.0
    s, t = struct.unpack('<ff', struct.pack('<Q', v[6]))
    return s / q * w, t / q * h


def ver(a):
    """Pintor de comprobacion (no es el traductor del juego): sprites del cuadro, con textura si la llevan."""
    from PIL import Image
    pedir = sorted(int(x) for x in a.vblanks.split(','))
    lienzo, cache, cuenta, actual, marcos = None, {}, Counter(), None, {0}

    def cerrar():
        if lienzo is not None and actual in pedir:
            ruta = SAL / a.nombre / f'ver_{actual}.png'
            lienzo.convert('RGB').save(ruta)
            print(ruta, dict(cuenta))
    for n, p, vr in cuadros(a.nombre):
        if n != actual:
            cerrar()
            actual, cuenta = n, Counter()
            lienzo = Image.new('RGBA', (640, 512), (0, 0, 0, 255)) if n in pedir else None
            if n > pedir[-1]:
                return
        if p is None or lienzo is None:
            continue
        tipo = p[0] & 7
        marcos.add((p[8] & 0x1FF) * 32)
        al = p[4]                                      # ALPHA: (A - B) * C + D
        if p[0] & 0x40 and al & 3 == (al >> 2) & 3 and (al >> 6) & 3 == 1:
            cuenta['mezcla_nula'] += 1                 # A == B y D = destino: no cambia nada (fundido a 0)
            continue
        if p[0] & 0x10 and (p[1] & 0x3FFF) in marcos:
            cuenta['copia_de_pantalla'] += 1           # el juego dibuja en 512x512 (FBP 0x40) y lo copia a la pantalla
            continue
        cuenta[('sprite', 'linea', 'tri')[0 if tipo == 6 else 1 if tipo in (1, 2) else 2] + ('_tex' if p[0] & 0x10 else '')] += 1
        vs = p[17]
        ofx, ofy = (p[7] & 0xFFFF) / 16, ((p[7] >> 32) & 0xFFFF) / 16
        xs = [v[0] / 16 - ofx for v in vs]
        ys = [v[1] / 16 - ofy for v in vs]
        x0, x1, y0, y1 = round(min(xs)), round(max(xs)), round(min(ys)), round(max(ys))
        if x1 <= x0 or y1 <= y0 or x1 - x0 > 2048 or y1 - y0 > 2048 or x1 <= 0 or y1 <= 0 or x0 >= 640 or y0 >= 512:
            continue
        col = vs[-1][4]
        rgba = (col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, min(((col >> 24) & 0xFF) * 2, 255))
        if p[0] & 0x10:
            k = (p[1], p[16], vr.version)
            if k not in cache:
                r = vr.textura(p[1], p[16])
                cache[k] = Image.fromarray(r[0], 'RGBA') if r else None
            tex = cache[k]
            if tex is None:
                cuenta['sin_textura'] += 1
                continue
            us, ts = zip(*(uv(p, i, tex.width, tex.height) for i in range(len(vs))))
            caja = (max(min(us), 0), max(min(ts), 0), min(max(us), tex.width), min(max(ts), tex.height))
            if caja[2] - caja[0] < 0.01 or caja[3] - caja[1] < 0.01:
                continue
            trozo = tex.resize((x1 - x0, y1 - y0), Image.BILINEAR, box=caja)   # ponytail: sin volteos ni repeticion
            if tipo != 6:
                cuenta['tri_como_caja'] += 1
            if p[0] & 0x40:
                lienzo.alpha_composite(trozo, (max(x0, 0), max(y0, 0)), (max(-x0, 0), max(-y0, 0)))
            else:
                lienzo.paste(trozo, (x0, y0))
        elif tipo == 6 and (p[0] & 0x40 or (x1 - x0) * (y1 - y0) < 400 * 400):   # (los borrados de pantalla/Z no se pintan)
            capa = Image.new('RGBA', (x1 - x0, y1 - y0), rgba if p[0] & 0x40 else rgba[:3] + (255,))
            lienzo.alpha_composite(capa, (max(x0, 0), max(y0, 0)), (max(-x0, 0), max(-y0, 0)))
    cerrar()


def tex(a):
    """PNG de cada textura que dibuja una primitiva 2D por GIF (sprite o tira de triangulos) + su identificador XXH3
    (capas / pack HD). No entran las copias de pantalla (textura = un bufer de dibujo)."""
    from PIL import Image
    base = SAL / a.nombre
    (base / 'tex').mkdir(exist_ok=True)
    usos, hechas, marcos = {}, {}, {0}
    for n, p, vr in cuadros(a.nombre):
        if p is None:
            continue
        marcos.add((p[8] & 0x1FF) * 32)
        if p[0] & 7 not in (4, 5, 6) or not p[0] & 0x10 or (p[1] & 0x3FFF) in marcos:
            continue
        k = (p[1], vr.version)
        if k not in hechas:
            r = vr.textura(p[1], p[16])
            hechas[k] = None
            if r:
                hechas[k] = '%016X_%016X.png' % (r[1], r[2])
                if not (base / 'tex' / hechas[k]).exists():
                    Image.fromarray(r[0], 'RGBA').save(base / 'tex' / hechas[k])
        nombre = hechas[k]
        if not nombre:
            continue
        ofx, ofy = (p[7] & 0xFFFF) / 16, ((p[7] >> 32) & 0xFFFF) / 16
        xs = [round(v[0] / 16 - ofx) for v in p[17]]
        ys = [round(v[1] / 16 - ofy) for v in p[17]]
        u = usos.setdefault(nombre, {'tex0': hex(p[1]), 'psm': hex((p[1] >> 20) & 0x3F), 'sprites': 0, 'tiras': 0,
                                     'primero': n, 'ultimo': n, 'rect': [min(xs), min(ys), max(xs), max(ys)]})
        u['sprites' if p[0] & 7 == 6 else 'tiras'] += 1
        u['ultimo'] = n
        u['rect'] = [min(u['rect'][0], *xs), min(u['rect'][1], *ys), max(u['rect'][2], *xs), max(u['rect'][3], *ys)]
    json.dump(usos, (base / 'texturas.json').open('w'), indent=1)
    print(a.nombre, 'texturas', len(usos))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='que', required=True)
    c = sub.add_parser('correr')
    c.add_argument('nombre')
    c.add_argument('--limite', type=int, default=4_000_000_000)
    c.add_argument('--pulsa', default='')
    c.add_argument('--desde')
    c.add_argument('--guardar', default='')
    c.add_argument('--parar', type=int)
    c.add_argument('--captura-desde', type=int, default=0)
    v = sub.add_parser('ver')
    v.add_argument('nombre')
    v.add_argument('vblanks')
    t = sub.add_parser('tex')
    t.add_argument('nombre')
    a = ap.parse_args()
    {'correr': correr, 'ver': ver, 'tex': tex}[a.que](a)
