# SPDX-License-Identifier: MIT
"""TRAD/2-3: gemelo de referencia de src/iw_gs_traductor.cpp (ordenes GS -> primitivas con su contexto) y prueba P3.

  python tools/iw_mips/gs_primitivas.py p3 CAPTURA.jsonl --exe prueba_traductor.exe   # oraculo (ordenes G/I)
  python tools/iw_mips/gs_primitivas.py p3 VOLCADO.gs.zst --exe prueba_traductor.exe  # PCSX2 (flujo GIF crudo)

La P3 da a los dos traductores (este y el C++ compilado suelto con -DIWTRAD_PRUEBA) la misma entrada cuadro a cuadro y
compara su texto. Pasa con 0 diferencias; si no, imprime el primer cuadro y la primera linea distinta.
"""
import argparse
import json
import subprocess
import sys

M32 = 0xFFFFFFFF
NECESITA = (1, 2, 2, 3, 3, 3, 2, 0)


class Traductor:
    def __init__(s):
        s.g = [0] * 0x64
        s.g[0x1A] = 1
        s.q = 0x3F800000
        s.cola, s.prims, s.subidas = [], [], []

    def registro(s, r, v):
        if not 0 <= r < 0x64:
            return
        s.g[r] = v
        if r == 0:
            s.cola = []
        elif r in (4, 5, 0xC, 0xD):
            s.vertice(v, r in (4, 0xC), r in (4, 5))
        elif r == 0x53 and v & 3 == 0:
            s.subidas.append([s.g[0x50], s.g[0x51], s.g[0x52], bytearray()])

    def imagen(s, d):
        if s.subidas:
            s.subidas[-1][3] += d

    def vertice(s, v, f, kick):
        g = s.g
        if f:
            z, fo = (v >> 32) & 0xFFFFFF, v >> 56
        else:
            z, fo = v >> 32, (g[0xA] >> 56) & 0xFF
        x = (v & 0xFFFF, (v >> 16) & 0xFFFF, z, fo, g[1] & M32, g[1] >> 32, g[2], g[3] & 0x3FFF3FFF)
        tipo = g[0] & 7
        need = NECESITA[tipo]
        if not need:
            return
        s.cola.append(x)
        if len(s.cola) < need:
            return
        if kick:
            attr = g[0] if g[0x1A] & 1 else g[0x1B]
            c = (attr >> 9) & 1
            s.prims.append(((tipo | (attr & 0x7F8)), g[6 + c], g[0x14 + c], g[8 + c], g[0x42 + c], g[0x47 + c],
                            g[0x40 + c], g[0x18 + c], g[0x4C + c], g[0x4E + c], g[0x4A + c], g[0x49], g[0x44], g[0x45],
                            g[0x46], g[0x3D], g[0x3B], list(s.cola)))
        if tipo == 2:
            s.cola = s.cola[1:]
        elif tipo == 4:
            s.cola = s.cola[1:]
        elif tipo == 5:
            s.cola = [s.cola[0], s.cola[2]]
        else:
            s.cola = []

    def empaquetado(s, r, a, b):
        if r == 0:
            s.registro(0, a & 0x7FF)
        elif r == 1:
            s.registro(1, (a & 0xFF) | ((a >> 32) & 0xFF) << 8 | (b & 0xFF) << 16 | ((b >> 32) & 0xFF) << 24 | s.q << 32)
        elif r == 2:
            s.q = b & M32
            s.registro(2, a)
        elif r == 3:
            s.registro(3, (a & 0x3FFF) | ((a >> 32) & 0x3FFF) << 16)
        elif r == 4:
            s.registro(0xC if (b >> 47) & 1 else 4,
                       (a & 0xFFFF) | ((a >> 32) & 0xFFFF) << 16 | ((b >> 4) & 0xFFFFFF) << 32 | ((b >> 36) & 0xFF) << 56)
        elif r == 5:
            s.registro(0xD if (b >> 47) & 1 else 5, (a & 0xFFFF) | ((a >> 32) & 0xFFFF) << 16 | (b & M32) << 32)
        elif r == 14:
            s.registro(b & 0xFF, a)
        elif r != 15:
            s.registro(r, a)

    def gif(s, d):
        import struct
        p, n = 0, len(d)
        while p + 16 <= n:
            lo, hi = struct.unpack_from('<QQ', d, p)
            p += 16
            nloop, pre, prim, flg, nreg = lo & 0x7FFF, (lo >> 46) & 1, (lo >> 47) & 0x7FF, (lo >> 58) & 3, (lo >> 60) & 15
            nreg = nreg or 16
            if pre and flg != 2:
                s.registro(0, prim)
            if flg == 0:
                for _ in range(nloop):
                    for i in range(nreg):
                        if p + 16 > n:
                            return
                        a, b = struct.unpack_from('<QQ', d, p)
                        s.empaquetado((hi >> (4 * i)) & 15, a, b)
                        p += 16
            elif flg == 1:
                for k in range(nloop * nreg):
                    if p + 8 > n:
                        return
                    r = (hi >> (4 * (k % nreg))) & 15
                    if r != 15:
                        s.registro(r, struct.unpack_from('<Q', d, p)[0])
                    p += 8
                p = (p + 15) & ~15
            else:
                tam = nloop * 16
                s.imagen(d[p:p + tam])
                p += tam

    def texto(s):
        out = []
        for p in s.prims:
            l = 'P ' + ' '.join('%X' % x for x in p[:17])
            for v in p[17]:
                l += ' | ' + ' '.join('%X' % x for x in v)
            out.append(l)
        for b, t, r, d in s.subidas:
            h = 0xCBF29CE484222325
            for x in d:
                h = ((h ^ x) * 0x100000001B3) & ((1 << 64) - 1)
            out.append('I %X %X %X %X %X' % (b, t, r, len(d), h))
        return out

    def fin_cuadro(s):
        s.prims, s.subidas = [], []


def leer_gs(ruta):
    """Volcado GS de PCSX2 v2.x (.gs o .gs.zst, cabecera v9) -> lista de cuadros {path: bytes}."""
    import collections, io, struct
    raw = open(ruta, 'rb').read()
    if ruta.endswith('.zst'):
        import zstandard
        raw = zstandard.ZstdDecompressor().stream_reader(io.BytesIO(raw)).read()
    crc, = struct.unpack_from('<I', raw, 0)
    if crc == 0xFFFFFFFF:          # cabecera nueva: el tamano del estado va dentro de ella (+8)
        hs, ss = struct.unpack_from('<I', raw, 4)[0], struct.unpack_from('<I', raw, 12)[0]
        p = 8 + hs + ss
    else:
        ss, = struct.unpack_from('<I', raw, 4)
        p = 8 + ss
    p += 8192                      # registros privilegiados
    cuadros, act = [], collections.defaultdict(bytearray)
    while p < len(raw):
        t = raw[p]
        p += 1
        if t == 0:                 # Transfer(path u8, tam u32, datos)
            path, n = raw[p], struct.unpack_from('<I', raw, p + 1)[0]
            act[path] += raw[p + 5:p + 5 + n]
            p += 5 + n
        elif t == 1:               # VSync
            p += 1
            cuadros.append(act)
            act = collections.defaultdict(bytearray)
        elif t == 2:               # ReadFIFO2
            p += 4
        elif t == 3:               # Registers
            p += 8192
        else:
            raise ValueError('paquete GS desconocido %d en %d' % (t, p))
    if any(act.values()):
        cuadros.append(act)
    return cuadros


def entradas(ruta):
    """(nombre_cuadro, [lineas de entrada del C++], funcion que alimenta al gemelo)."""
    if ruta.endswith('.jsonl'):
        for l in open(ruta, encoding='utf-8'):
            f = json.loads(l)
            os_ = [o for o in f['ordenes'] if o[0] in 'GI']
            lineas = ['G %X %X' % (o[1], o[2]) if o[0] == 'G' else 'I %d' % (o[1] * 16) for o in os_]
            def alimentar(t, os_=os_):
                for o in os_:
                    if o[0] == 'G':
                        t.registro(o[1], o[2])
                    else:
                        t.imagen(bytes(o[1] * 16))
            yield str(f['n']), lineas, alimentar
    else:
        for i, c in enumerate(leer_gs(ruta)):
            d = bytes(b''.join(bytes(v) for _, v in sorted(c.items())))
            yield 'pcsx2_%d' % i, ['B ' + d.hex()], lambda t, d=d: t.gif(d)


def p3(ruta, exe):
    cuadros = list(entradas(ruta))
    entrada = '\n'.join(l for n, ls, _ in cuadros for l in ls + ['F ' + n]) + '\n'
    r = subprocess.run([exe], input=entrada.encode(), capture_output=True, check=True)
    cpp, bloque = {}, []
    for l in r.stdout.decode().splitlines():
        if l.startswith('F '):
            cpp[l[2:]] = bloque
            bloque = []
        else:
            bloque.append(l)
    t, prims, difs = Traductor(), 0, 0
    for n, _, alimentar in cuadros:
        alimentar(t)
        py = t.texto()
        t.fin_cuadro()
        prims += len(py)
        c = cpp.get(n, [])
        if py != c:
            difs += 1
            if difs == 1:
                k = next((i for i, (a, b) in enumerate(zip(py, c)) if a != b), min(len(py), len(c)))
                print('PRIMERA DIFERENCIA cuadro', n, 'linea', k, '\n  py :', py[k:k + 1], '\n  c++:', c[k:k + 1],
                      '\n  lineas py/c++:', len(py), len(c))
    print('P3 %s: %d cuadros, %d primitivas+subidas, %d cuadros con diferencias' % (ruta, len(cuadros), prims, difs))
    return difs


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('que', choices=['p3'])
    ap.add_argument('ruta')
    ap.add_argument('--exe', required=True)
    o = ap.parse_args()
    sys.exit(1 if p3(o.ruta, o.exe) else 0)
