#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""escenarios_convertir.py - Escenarios de Budokai 2 y Budokai 1 (PS2) -> Budokai 3 HD como ENTRADA NUEVA.

Los datos salen de la ISO del jugador (ps2_games/); el resultado es un mod opcional que anade una casilla
nueva al selector de escenarios (src/escenarios.cpp lee su stage.toml). No sustituye ningun escenario de serie
ni reparte geometria de otro juego.

Uso (desde la carpeta del proyecto):
  python tools\escenarios_convertir.py lista [--juego b1]          # escenarios de la ISO (B2 por defecto)
  python tools\escenarios_convertir.py mod 7 --nombre "Montanas nevadas" --rotulo "Montañas nevadas"
  python tools\escenarios_convertir.py mod 7 --zona b --nombre "Montanas nevadas B"
      la variante B (la otra zona del escenario; solo los grupos con 2 variantes: 0, 4, 7 y 8). El mod
      instalado lleva 'zona_b = 5007' en su stage.toml: con la cvar dbz3_escenarios_zonas_b activada
      (src/escenarios.cpp), al caer por el borde el juego pasa a esta zona.
  python tools\escenarios_convertir.py mod 3 --juego b1 --nombre "Paramo al atardecer"
      crea mods\<nombre>: entrada 5000+grupo (B1: 5020+grupo; B2 zona B: 5040+grupo) y sus imagenes del
      selector en entrada+100
      (--mods otra carpeta mods, --entrada N, --bin cache del bin convertido, --foto captura para la casilla)
  python tools\escenarios_convertir.py comprobar                   # autocomprobacion con los 13 pares B3 PS2/HD

Formato (medido contra los 13 pares nativos B3 PS2 GH <-> HD; detalle en docs/14_frentes/escenarios.md):
  bin de escenario = #AMB[#ZDD, #CAD, #CAS, #SPX, #AMB de 641 casillas]
  #ZDD = cabecera 0x40 + bloques (#AZT, #ACC, #AWO, #ACM) + grafo de escena. El grafo se copia estructura a
  estructura con el esquema de punteros del reubicador del juego (sub_820CBC90 / sub_820CB950): solo se
  traducen los campos que el juego reubica; el resto se copia tal cual (LE -> BE).
  Colision: cada modelo de escenario lleva, colgando de la lista de un hueso (4a palabra), un bloque de
  colision (cabeceras + nodos + vertices + triangulos) que amo2awo (conversor de personajes) no conoce; los
  registros rec20/rec30 del grafo apuntan DENTRO de ese bloque. Se convierte aqui (identico byte a byte en los
  20 modelos con colision de los 13 pares) y se traducen esos punteros.
"""
import argparse
import bisect
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(RAIZ / "awo_tools"), str(RAIZ / "mod center hd")]   # ps2hd, amo2awo, iso, model_render
# Carpetas del jugador: las pone el launcher (--us, --ps2: campos_iw.py auto); por defecto, las del proyecto.
US = RAIZ / "us"              # data_cmn.afs / data_eng.afs de la HD
PS2 = RAIZ / "ps2_games"      # ISOs de PS2

import amo2awo  # noqa: E402
import iso  # noqa: E402
import ps2hd  # noqa: E402

import swap_b3  # noqa: E402  (xbcompress: "mod center hd/tools" en el Kit y la release, el del XDK en el proyecto)
XBC, XBD = Path(swap_b3.XBCOMPRESS), Path(swap_b3.XBDECOMPRESS)
ENTRADA_BASE = 5000          # data_cmn.afs 5000-5199: reservado para escenarios (no pisa nada)
REGISTRO_N = 641


def align(x, a):
    return (x + a - 1) // a * a


def le(b, o):
    return struct.unpack_from("<I", b, o)[0]


# ---------------------------------------------------------------------------------------------------------
# Modelo: amo2awo + bloque de colision
# ---------------------------------------------------------------------------------------------------------
def conv_colision(b, g3s, base, escala=1.0):
    """Bloques de colision de un #AMO0 (offsets relativos al modelo, en orden de lista) -> bytes HD + mapa.

    PS2 y HD guardan lo mismo; la HD reordena: cabeceras de todos los bloques (4 filas de 16 B
    [FFFFFFFF,0,0,FFFFFFFF->0] + [4, nodos, primer nodo, FFFFFFFF->0]) y luego, por bloque, sus nodos
    (0x30 B + 0x20 de ceros: [siguiente, vertices, triangulos, 0, esfera...]) y los datos de cada nodo
    (vertices [0,0,0,n]+n x 16 B, triangulos [0,0,0,n]+n x 32 B). Devuelve (bytes, {off PS2: off HD}).
    escala != 1 estira la CAJA en x/z (no toca y) alrededor del origen del modelo: la usa cmd_mod
    (--caja-escala) para dar mas sitio a un escenario cuyo suelo es mas ancho que su colision."""
    R = lambda o: le(b, o)  # noqa: E731
    mp, pos, heads = {}, base, []
    for g3 in g3s:
        n4, nn, first, _ = (R(g3 + 0x40 + 4 * j) for j in range(4))
        if n4 != 4 and R(g3 + 0x4C) < 0x100:
            # s9b: B1 (Cell Ring, g11) cuelga un objeto "BBSEL_POL1" (seleccion de poligonos de B1, cabecera con un tipo 0xB/0xC): B3 no lo
            # usa; se deja una cabecera de colision VACIA (0 nodos) para que los rec20/rec30 del grafo que lo apuntan resuelvan
            n4, nn, first = 4, 0, 0
        if n4 != 4:
            raise ValueError("cabecera de colision inesperada en +0x%X" % g3)
        nodes, p = [], first
        while p and len(nodes) < nn:
            nodes.append(p)
            p = R(p)
        if len(nodes) != nn or p:
            raise ValueError("cadena de nodos de colision rota en +0x%X" % g3)
        for k in range(5):
            mp[g3 + 16 * k] = pos + 16 * k
        heads.append((nn, first, nodes))
        pos += 0x50
    datos = []
    for nn, first, nodes in heads:
        for q in nodes:
            mp[q] = pos
            pos += 0x50
        d = []
        for q in nodes:
            va, ta = R(q + 4), R(q + 8)
            nv, nt = R(va + 12), R(ta + 12)
            mp[va] = pos
            d.append(("v", va, 16 + 16 * nv))
            pos += 16 + 16 * nv
            mp[ta] = pos
            for t in range(nt):
                mp[ta + 16 + 32 * t] = pos + 16 + 32 * t
            d.append(("t", ta, 16 + 32 * nt))
            pos += 16 + 32 * nt
        datos.append(d)
    out = bytearray()

    def w(*v):
        out.extend(struct.pack(">%dI" % len(v), *v))
    for nn, first, nodes in heads:
        for _ in range(4):
            w(0xFFFFFFFF, 0, 0, 0)
        w(4, nn, mp[first] if first else 0, 0)
    for (nn, first, nodes), d in zip(heads, datos):
        for q in nodes:
            v = [R(q + 4 * j) for j in range(12)]
            v[0] = mp[v[0]] if v[0] else 0
            v[1], v[2] = mp[v[1]], mp[v[2]]
            w(*v)
            out.extend(bytes(0x20))
        for tipo, src, sz in d:
            if tipo == "v" and escala != 1.0:
                vals = list(struct.unpack_from("<%dI" % (sz // 4), b, src))
                for v in range((sz - 16) // 16):
                    k = 4 + 4 * v
                    x = struct.unpack("<f", struct.pack("<I", vals[k]))[0]
                    z = struct.unpack("<f", struct.pack("<I", vals[k + 2]))[0]
                    vals[k] = struct.unpack("<I", struct.pack("<f", x * escala))[0]
                    vals[k + 2] = struct.unpack("<I", struct.pack("<f", z * escala))[0]
                out.extend(struct.pack(">%dI" % (sz // 4), *vals))
            else:
                out.extend(struct.pack(">%dI" % (sz // 4), *struct.unpack_from("<%dI" % (sz // 4), b, src)))
    return bytes(out), mp


def amo_a_awo(amo, escala=1.0):
    """#AMO0 PS2 -> (#AWO HD, {off PS2: off HD} relativos al modelo: base, huesos, listas, colision).
    amo2awo deja las listas de hueso al final; el bloque de colision va justo delante (como el nativo)."""
    awo = bytearray(amo2awo.convert_amo(bytes(amo)))
    nb, bones, namg, tbl, nlist = (le(amo, 0x10 + 4 * j) for j in range(5))
    H = lambda o: struct.unpack_from(">I", awo, o)[0]  # noqa: E731
    hb = H(0x14)
    mp = {0: 0}
    listas, g3s = [], []
    for i in range(nb):
        mp[bones + 0x20 * i] = hb + 0x20 * i
        lp, lh = le(amo, bones + 0x20 * i + 4), H(hb + 0x20 * i + 4)
        if lp:
            listas.append((i, lp, lh))
            for k in range(nlist):
                g3 = le(amo, lp + 16 * k + 12)
                if g3 and g3 != 0xFFFFFFFF:
                    g3s.append(g3)
    if not g3s:
        for i, lp, lh in listas:
            for k in range(nlist):
                mp[lp + 16 * k] = lh + 16 * k
        return bytes(awo), mp
    ins = min(lh for _, _, lh in listas)
    col, cmp_ = conv_colision(amo, g3s, ins, escala)
    mp.update(cmp_)
    awo[ins:ins] = col
    for i, lp, lh in listas:
        nuevo = lh + len(col)
        struct.pack_into(">I", awo, hb + 0x20 * i + 4, nuevo)
        for k in range(nlist):
            mp[lp + 16 * k] = nuevo + 16 * k
            g3 = le(amo, lp + 16 * k + 12)
            struct.pack_into(">I", awo, nuevo + 16 * k + 12, cmp_[g3] if g3 and g3 != 0xFFFFFFFF else 0)
    awo += bytes(align(len(awo), 32) - len(awo))
    return bytes(awo), mp


# ---------------------------------------------------------------------------------------------------------
# #MDD (PS2) -> #ZDD (HD)
# ---------------------------------------------------------------------------------------------------------
MAGICS_MDD = (b"#AMT", b"#AMO", b"#AME", b"#AMC", b"#AMM", b"#AMP", b"#AML")
ORDEN_ZDD = {b"#AZT": 0, b"#ACC": 1, b"#AWO": 2, b"#ACM": 3, b"#ACL": 4}


def bloques_mdd(mdd):
    """Bloques de nivel superior de un #MDD PS2 en orden fisico (tamano = hasta el siguiente magic)."""
    ini = le(mdd, 0x1C)
    hits, pos = [], 4
    while True:
        best = min((i for i in (mdd.find(m, pos) for m in MAGICS_MDD) if i >= 0), default=-1)
        if best < 0:
            break
        hits.append(best)
        pos = best + 4
    hits = [h for h in hits if h >= ini - 4]
    return [(h, (hits[k + 1] if k + 1 < len(hits) else len(mdd)) - h) for k, h in enumerate(hits)]


class Grafo:
    """Copia el grafo de escena PS2 -> HD siguiendo el esquema de punteros del reubicador del juego.

    Cabecera: +0x20 lista de objetos (n +4), +0x24 lista de raices (n +8).
    Objeto 0xE0: +0xA4 textura, +0xA8 sub-mallas (n +0x90), +0xAC/+0xB4 rec20 (n +0x94/+0x9C, 5 punteros),
      +0xB0/+0xB8 rec30 (n +0x98/+0xA0, 8 punteros), +0xBC/+0xC4/+0xCC arboles st50, +0xC0/+0xC8/+0xD0 nodo
      st50, +0xD8 bloques (n +0xD4).
    Sub-malla/raiz 0x20: +8 modelo, +0xC/+0x10 arrays (n +0/+4) de 16 B [?, bloque, puntero, ?].
    st50 0x50: +0x34..+0x44 punteros st50, +0x48/+0x4C arrays de punteros (n +0x2C/+0x30) a rec20/rec30.
    rec20/rec30: punteros DENTRO del modelo de colision (cabecera, nodo, triangulo, lista, hueso, modelo)."""

    def __init__(self, mdd, bloque_hd, interno, base):
        self.m, self.bloque_hd, self.interno, self.base = mdd, bloque_hd, interno, base
        self.out = bytearray()
        self.nuevo = {}            # off PS2 de estructura -> off HD (relativo al #ZDD)
        self.regiones = []         # (src, tam, dst): copias contiguas (punteros a mitad de array)
        self.fix = []              # (off en out, destino PS2)

    def R(self, o):
        return le(self.m, o)

    def copia(self, src, tam, punteros=(), ceros=()):
        if src in self.nuevo:
            return self.nuevo[src]
        o = len(self.out)
        for i in range(0, tam, 4):
            v = self.R(src + i)
            if v == 0xFCFCFCFC or i in ceros:          # relleno PS2 -> 0 (como el nativo)
                v = 0
            self.out += struct.pack(">I", v)
        self.out += bytes(align(len(self.out), 16) - len(self.out))
        self.nuevo[src] = self.base + o
        self.regiones.append((src, tam, self.base + o))
        for i in punteros:
            if i not in ceros and self.R(src + i) != 0xFCFCFCFC:   # relleno: el objeto de B1 acaba en +0xD4
                self.fix.append((o + i, self.R(src + i), src + i))
        return self.base + o

    def lista(self, src, n, fn):
        o = self.copia(src, 4 * n, range(0, 4 * n, 4))
        for k in range(n):
            fn(self.R(src + 4 * k))
        return o

    def arrays16(self, s):
        # entradas [tipo, bloque (#AMM), parametros, relleno]; parametros = 16 B sin punteros [0, 1.0, 1.0, 0]
        for cf, af in ((0, 12), (4, 16)):
            n, a = self.R(s + cf), self.R(s + af)
            if n and a:
                dst = self.copia(a, 16 * n, [16 * k + j for k in range(n) for j in (4, 8)]) - self.base
                for k in range(n):
                    e = a + 16 * k
                    if self.R(e + 4) == 0xFCFCFCFC:   # B1: entrada = [bloque] -> [1, bloque, parametros, 0]
                        struct.pack_into(">I", self.out, dst + 16 * k, 1)
                        self.fix.append((dst + 16 * k + 4, self.R(e), e))
                    pp = self.R(e + 8)
                    if pp == 0xFCFCFCFC:          # B1 no los lleva: los de siempre
                        struct.pack_into(">I", self.out, dst + 16 * k + 8, self.base + len(self.out))
                        self.out += struct.pack(">4f", 0, 1, 1, 0)
                    elif pp:
                        self.copia(pp, 16)

    def submalla(self, s):
        self.copia(s, 0x20, (8, 12, 16))
        self.arrays16(s)

    raiz = submalla       # mismo formato (la raiz del escenario 46 lleva un array de animacion)

    def st50(self, s):
        if not s or s in self.nuevo:
            return
        self.copia(s, 0x50, (0x34, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C))
        for af, cf in ((0x48, 0x2C), (0x4C, 0x30)):
            n, a = self.R(s + cf), self.R(s + af)
            if n and a:
                self.copia(a, 4 * n, range(0, 4 * n, 4))
        for f in (0x34, 0x38, 0x3C, 0x40, 0x44):
            self.st50(self.R(s + f))

    def objeto(self, ob):
        self.copia(ob, 0xE0, (0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD8))
        n, a = self.R(ob + 0x90), self.R(ob + 0xA8)
        if n and a:
            self.lista(a, n, self.submalla)
        for pf, cf, tam, np_ in ((0xAC, 0x94, 0x20, 5), (0xB0, 0x98, 0x30, 8),
                                 (0xB4, 0x9C, 0x20, 5), (0xB8, 0xA0, 0x30, 8)):
            n, a = self.R(ob + cf), self.R(ob + pf)
            if n and a:
                self.copia(a, tam * n, [tam * k + 4 * j for k in range(n) for j in range(np_)])
        for f in (0xBC, 0xC4, 0xCC, 0xC0, 0xC8, 0xD0):
            self.st50(self.R(ob + f))
        n, a = self.R(ob + 0xD4), self.R(ob + 0xD8)
        if n and a and n != 0xFCFCFCFC:
            self.copia(a, 4 * n, range(0, 4 * n, 4))

    def resolver(self, v):
        if v == 0:
            return 0
        for tabla in (self.bloque_hd, self.nuevo, self.interno):
            if v in tabla:
                return tabla[v]
        i = bisect.bisect_right(self.rs, v) - 1
        if i >= 0:
            s, tam, d = self.regiones[i]
            if s < v < s + tam:
                return d + v - s
        raise ValueError("puntero del grafo sin destino conocido: 0x%X" % v)

    def convertir(self):
        n_obj, n_raiz, ol, rl = self.R(4), self.R(8), self.R(0x20), self.R(0x24)
        o_ol = self.lista(ol, n_obj, self.objeto) if (ol and n_obj) else 0
        o_rl = self.lista(rl, n_raiz, self.raiz) if (rl and n_raiz) else 0
        self.regiones.sort()
        self.rs = [r[0] for r in self.regiones]
        for o, v, campo in self.fix:
            try:
                struct.pack_into(">I", self.out, o, self.resolver(v))
            except ValueError as e:
                raise ValueError("%s (campo en +0x%X del #MDD)" % (e, campo)) from None
        return bytes(self.out), o_ol, o_rl, n_obj, n_raiz


def amc_pistas(amcs, minimo):
    """Une las pistas de camara de varios #AMC PS2 (LE) en uno con al menos `minimo` pistas (repite las
    ultimas). La presentacion del combate (script #SPX del escenario donante) pide las pistas 0..2 del primer
    #AMC del objeto 0 (B3: 1 #AMC de 3 pistas; B2: 2 #AMC de 1-7). Pista = registro de 16 B [tipo 0x1D/0x0D,
    0, fotogramas, datos] y datos = 4 (o 3) desplazamientos a sus claves; nada mas se reubica (sub_82080408)."""
    pistas = []
    for a in amcs:
        n, t = le(a, 0x10), le(a, 0x14)
        recs = [struct.unpack_from("<4I", a, t + 16 * i) for i in range(n)]
        inicios = sorted(r[3] for r in recs) + [len(a)]
        for r in recs:
            fin = next(x for x in inicios if x > r[3])
            pistas.append((r, a, r[3], fin))
    while len(pistas) < minimo:
        pistas.append(pistas[len(pistas) % max(1, len(pistas))])
    cab = bytearray(amcs[0][:0x20])
    struct.pack_into("<I", cab, 0x10, len(pistas))
    struct.pack_into("<I", cab, 0x14, 0x20)
    out = cab + bytes(16 * len(pistas))
    for i, (r, a, ini, fin) in enumerate(pistas):
        out += bytes(align(len(out), 16) - len(out))
        base = len(out)
        bloque = bytearray(a[ini:fin])
        for j in range(4 if r[0] == 0x1D else 3):
            v = le(bloque, 4 * j)
            if v:
                struct.pack_into("<I", bloque, 4 * j, v - ini + base)
        out += bloque
        struct.pack_into("<4I", out, 0x20 + 16 * i, r[0], r[1], r[2], base)
    return bytes(out)


def camaras_hd(zdd):
    """Bloques de camara (#ACC, ya HD) del objeto 0 de un #ZDD nativo: los de la presentacion del combate."""
    R = lambda o: struct.unpack_from(">I", zdd, o)[0]  # noqa: E731
    ol = R(0x20)
    ob = R(ol)
    out = []
    for k in range(R(ob + 0xD4)):
        o = R(R(ob + 0xD8) + 4 * k)
        sig = [i for i in (zdd.find(m, o + 4) for m in (b"#AZT", b"#ACC", b"#AWO", b"#ACM", b"#ACL")) if 0 <= i < ol]
        out.append(zdd[o:min(sig + [ol])])        # hasta el siguiente bloque o el grafo
    return out


def mdd_a_zdd(mdd, pistas_min=0, camaras=(), escala=1.0):
    """#MDD PS2 -> #ZDD HD: cabecera 0x40, bloques convertidos (orden HD, alineados a 32), grafo.
    pistas_min: el primer #AMC del objeto 0 (camaras de la presentacion) con al menos esas pistas.
    camaras: bloques #ACC HD para el objeto 0 si no trae ninguno (B1: su objeto acaba en +0xD4, sin camaras)."""
    conv = []
    sustituto = {}
    ob = le(mdd, le(mdd, 0x20)) if le(mdd, 4) else 0
    n_d8 = le(mdd, ob + 0xD4) if ob else 0
    n_d8 = 0 if n_d8 == 0xFCFCFCFC else n_d8              # relleno PS2: objeto de B1 (0xD4 B)
    if pistas_min and n_d8:
        d8 = [le(mdd, le(mdd, ob + 0xD8) + 4 * i) for i in range(n_d8)]
        tamanos = dict(bloques_mdd(mdd))
        amcs = [mdd[o:o + tamanos[o]] for o in d8 if mdd[o:o + 4] == b"#AMC" and o in tamanos]
        if amcs and le(amcs[0], 0x10) < pistas_min:
            sustituto[d8[0]] = amc_pistas(amcs, pistas_min)
    extra = list(camaras) if ob and not n_d8 else []
    for k, hd in enumerate(extra):
        conv.append((hd[:4], hd, ("camara", k), {}))
    for off, tam in bloques_mdd(mdd):
        datos = sustituto.get(off, mdd[off:off + tam])
        if datos[:4] == b"#AMO":
            hd, mp = amo_a_awo(datos, escala)
        else:
            try:
                hd, mp = ps2hd.convert_block(datos), {}
            except ps2hd.Unsupported:
                continue
        conv.append((ps2hd.MAGIC.get(datos[:4], datos[:4]), hd, off, mp))
    conv.sort(key=lambda t: ORDEN_ZDD.get(t[0], 9))
    cuerpo = bytearray()
    bloque_hd, interno = {}, {}
    for magic, hd, off, mp in conv:
        cuerpo += bytes(align(len(cuerpo), 32) - len(cuerpo))
        pos = 0x40 + len(cuerpo)
        bloque_hd[off] = pos
        for k, v in mp.items():
            if k:
                interno[off + k] = pos + v
        cuerpo += hd
    cuerpo += bytes(align(len(cuerpo), 32) - len(cuerpo))
    g = Grafo(mdd, bloque_hd, interno, 0x40 + len(cuerpo))
    grafo, ol, rl, n_obj, n_raiz = g.convertir()
    cuerpo += grafo
    if extra:   # lista de camaras del objeto 0 (n +0xD4, lista +0xD8)
        cuerpo += bytes(align(len(cuerpo), 16) - len(cuerpo))
        lista = 0x40 + len(cuerpo)
        cuerpo += struct.pack(">%dI" % len(extra), *(bloque_hd[("camara", k)] for k in range(len(extra))))
        struct.pack_into(">II", cuerpo, g.nuevo[ob] - 0x40 + 0xD4, len(extra), lista)
    hdr = b"#ZDD" + struct.pack(">15I", n_obj, n_raiz, 0, 0, 0, 0xFFFFFFFF, 0x40, ol, rl, 0, 0, 0, 0, 0, 0)
    out = hdr + cuerpo
    return out + bytes(align(len(out), 32) - len(out))


def camara(b, magic):
    """#MAD/#MAS (PS2) -> #CAD/#CAS: swap32 + renombrar + relleno FCFCFCFC -> 0 (medido en el par 44)."""
    out = bytearray(ps2hd.swap32_all(b, 4))
    out[:4] = magic
    for i in range(4, len(out) - 3, 4):
        if out[i:i + 4] == b"\xfc\xfc\xfc\xfc":
            out[i:i + 4] = bytes(4)
    return bytes(out)


def amb_hd(hijos):
    """#AMB BE: hijos = [(bytes o None, tipo)]; vacios con offset 0 (como el nativo), alineados a 32."""
    n = len(hijos)
    ini = align(0x20 + 16 * n, 32)
    tabla, cuerpo = bytearray(), bytearray()
    for d, ty in hijos:
        if d:
            pos = align(ini + len(cuerpo), 32)
            cuerpo += bytes(pos - ini - len(cuerpo)) + d
            tabla += struct.pack(">4I", pos, len(d), ty, 0)
        else:
            tabla += struct.pack(">4I", 0, 0, 0, 0)
    out = b"#AMB" + struct.pack(">7I", 0x20, 0, 2, n, 0x20, ini, 0) + tabla
    out += bytes(ini - len(out)) + cuerpo
    return out + bytes(align(len(out), 32) - len(out))


def hijos_amb(b, en):
    n, t = struct.unpack_from(en + "2I", b, 0x10)
    return [struct.unpack_from(en + "4I", b, t + 16 * k) for k in range(n)]


# ---------------------------------------------------------------------------------------------------------
# Fuentes
# ---------------------------------------------------------------------------------------------------------
JUEGOS = {"b2": ("Budokai 2", "/USR/DATA_CMN.AFS"), "b1": ("Budokai", "/USR/DATA_EN.AFS")}


class IsoB2:
    """Budokai 2 / Budokai 1 (PS2): cada escenario son entradas seguidas del AFS de datos: #MAD #MAS #MDD #SPX
    (B2: USR/DATA_CMN.AFS; B1: USR/DATA_EN.AFS, el mismo en los 5 idiomas)."""

    def __init__(self, ruta=None, juego="b2"):
        self.juego, afs = JUEGOS[juego]
        self.ruta = ruta or iso.find_game(juego, root=str(PS2))
        if not self.ruta:
            raise SystemExit("no encuentro la ISO de %s en ps2_games/" % self.juego)
        self.iso = iso.Iso(self.ruta)
        self.f = self.iso.open(self.iso.find(afs) or afs)
        self.f.seek(4)
        n = struct.unpack("<I", self.f.read(4))[0]
        self.tab = struct.unpack("<%dI" % (2 * n), self.f.read(8 * n))
        self.n = n

    def entrada(self, i):
        self.f.seek(self.tab[2 * i])
        return self.f.read(self.tab[2 * i + 1])

    def magic(self, i):
        if not self.tab[2 * i + 1]:
            return b""
        self.f.seek(self.tab[2 * i])
        return self.f.read(4)

    def escenarios(self):
        out = []
        for i in range(self.n - 2):
            if (self.magic(i), self.magic(i + 1), self.magic(i + 2)) == (b"#MAD", b"#MAS", b"#MDD"):
                out.append(i)
        return out

    def piezas(self, grupo):
        i = self.escenarios()[grupo]
        spx = next((self.entrada(j) for j in range(i + 3, i + 8) if self.magic(j) == b"#SPX"), None)
        return self.entrada(i + 2), self.entrada(i), self.entrada(i + 1), spx


def entrada_afs(ruta, i):
    with open(ruta, "rb") as f:
        f.seek(8 + 8 * i)
        a, s = struct.unpack("<II", f.read(8))
        f.seek(a)
        return f.read(s)


def lzx(datos, comprimir):
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, "a"), os.path.join(d, "b")
        open(src, "wb").write(datos)
        args = [str(XBC), "/N:2048", src, dst] if comprimir else [str(XBD), src, dst]
        subprocess.run(args, capture_output=True, timeout=600)
        if not os.path.exists(dst):
            raise RuntimeError("xbcompress/xbdecompress fallo")
        return open(dst, "rb").read()


def hd_nativo(i):
    """Entrada i del data_cmn de la HD del jugador (descomprimida)."""
    d = entrada_afs(US / "data_cmn.afs", i)
    return lzx(d, False) if d[:4] == b"\x0f\xf5\x12\xee" else d


# ---------------------------------------------------------------------------------------------------------
# Montaje
# ---------------------------------------------------------------------------------------------------------
SUCESO_B2 = ("KEMURI", "BAKU", "NULL", "HAKAI", "BOM", "KAMINARI")   # humo, explosion, cascotes, rotura, bombas, rayo


def _raiz_amm_y_adornos(mdd, s):
    """(tiene #AMM, entradas activas que NO son #AMM) de una raiz. Las raices de B2 pueden llevar en el
    segundo par un #AMP (particula) en vez de un #AMM; B3 no tiene manejador para ese bloque en la lista de
    nodos (cierre al dibujar, lr 0x8208E5AC)."""
    tiene_amm, adornos = False, 0
    for cf, af in ((0, 12), (4, 16)):
        cnt, arr = le(mdd, s + cf), le(mdd, s + af)
        for q in range(cnt if (cnt and arr) else 0):
            flag, blk = struct.unpack_from("<II", mdd, arr + 16 * q)
            if not blk or blk + 4 > len(mdd):
                continue
            if mdd[blk:blk + 4] == b"#AMM":
                tiene_amm = True
            elif flag:
                adornos += 1
    return tiene_amm, adornos


def _quietar_raiz(mdd, out, s):
    """Todas las claves de las pistas #AMM de la raiz `s` = la del cuadro 0 (la raiz queda quieta, como en raices_zona_b)."""
    for j in range(le(mdd, s)):
        blk = le(mdd, le(mdd, s + 12) + 16 * j + 4)
        if mdd[blk:blk + 4] != b"#AMM":
            continue
        n_anim, t, nb, no = struct.unpack_from("<4I", mdd, blk + 0x10)
        for a in range(n_anim):
            fl, _v, _nf, off = struct.unpack_from("<4I", mdd, blk + t + 16 * a)
            per = 3 if fl & 0x10 else 2
            for b in range(nb if off else 0):
                for i in range(per):
                    pista = le(mdd, blk + off + 4 * (per * b + i))
                    if not pista:
                        continue
                    q, n_c = blk + pista, le(mdd, blk + pista + 8)
                    paso, ini = (8, 2) if i == 0 else (16, 4)
                    v0 = mdd[q + 12 + ini:q + 12 + paso]
                    for c in range(1, n_c):
                        out[q + 12 + paso * c + ini:q + 12 + paso * (c + 1)] = v0


def raices_quietas_b2(mdd, todas=False):
    """B2: las animaciones de las raices del escenario (#MDD +0x08 n, +0x24 lista; raiz +0 n, +0xC entradas de 16 B
    [tipo, #AMM, parametros, 0]) son TODAS de destruccion (medido en los 9 grupos por los nombres de hueso: TOWER y
    NADARE en la nieve, HAKAI, BAKUHATU, KEMURI, HBILL...). B2 las dispara en un suceso del combate; B3 repite en bucle
    desde el principio toda animacion de raiz (las suyas son adornos ciclicos): la torre de la nieve caia sin parar.
    Sin ese suceso en B3: las raices del estado destruido (BB<esc>B..) y las de humo/cascotes (SUCESO_B2) se quitan de
    la lista; las demas se quedan en su cuadro 0, antes del suceso (todas las claves = la del cuadro 0; la torre en pie:
    su giro x pasa de 0 a ~92 grados entre los cuadros 153 y 256). Con cuadros -> 1 el juego la pintaba caida. Devuelve (mdd, quitadas, quietas)."""
    import re  # noqa: PLC0415
    out = bytearray(mdd)
    rl, n_raiz = le(mdd, 0x24), le(mdd, 8)
    if not rl:
        return mdd, 0, 0
    quedan, quietas, suceso_raices = [], 0, []
    for k in range(n_raiz):
        s = le(mdd, rl + 4 * k)
        nombres = []
        for j in range(le(mdd, s)):
            blk = le(mdd, le(mdd, s + 12) + 16 * j + 4)
            if mdd[blk:blk + 4] != b"#AMM":
                continue
            n_anim, t, nb, no = struct.unpack_from("<4I", mdd, blk + 0x10)
            nombres += [mdd[blk + no + 32 * b:blk + no + 32 * b + 32].split(b"\0")[0].decode("latin1") for b in range(nb)]
            for a in range(n_anim):   # cada pista (giro: claves de 8 B [cuadro u16, 3 u16]; posicion y escala:
                fl, _v, _nf, off = struct.unpack_from("<4I", mdd, blk + t + 16 * a)   # 16 B [cuadro u32, 3 f32])
                per = 3 if fl & 0x10 else 2
                for b in range(nb if off else 0):
                    for i in range(per):
                        pista = le(mdd, blk + off + 4 * (per * b + i))
                        if not pista:
                            continue
                        q, n_c = blk + pista, le(mdd, blk + pista + 8)
                        paso, ini = (8, 2) if i == 0 else (16, 4)
                        v0 = mdd[q + 12 + ini:q + 12 + paso]   # valores de la clave del cuadro 0
                        for c in range(1, n_c):
                            out[q + 12 + paso * c + ini:q + 12 + paso * (c + 1)] = v0
        tiene_amm, adornos = _raiz_amm_y_adornos(mdd, s)
        if adornos and not tiene_amm:   # grupo 1 (BUDO): raiz solo #AMP; B3 se cierra al dibujar su lista
            continue
        # X1esc 23/10 (Base): `todas` conserva en su orden las raices de la variante B (BB<esc>B..), quietas: los huesos de las
        # piezas de la zona B se numeran sobre TODAS las raices (id 65 > 26 huesos de las que quedaban: sub_82090820 con r3 = 0x290)
        suceso = (not todas) and (any(re.match(r"BB[A-Z]{3}B\d", nm) for nm in nombres) or any(w in nm.upper() for nm in nombres for w in SUCESO_B2))
        if suceso:
            suceso_raices.append(s)
            continue
        quietas += bool(nombres)
        quedan.append(s)
    # X1esc 23/10: el cambio de zona del motor lee una segunda raiz; g0 (Base Red Ribbon) se quedaba con una sola y
    # la segunda posicion de la lista a 0 -> cierre en sub_82090820 (guest 0x294). Se completa hasta 2 con raices de
    # suceso NEUTRALIZADAS (los 0x20 B a cero: una raiz vacia valida, como las de g7 y g8).
    # X1esc 23/10 (Base): la segunda raiz que lee el cambio de zona es la de la variante B (BB<esc>B..) y no una copia de
    # la A (el objeto 1 se dibujaba con una lista de nodos ajena: sub_82090820 con r3 = 0x290 desde lr 0x8208E5AC). Si
    # hay una raiz de variante B con #AMM se conserva, quieta en su cuadro 0.
    if os.environ.get("X1ESC_BASE_RAIZ_B", "1") != "0":
        for s_b in [x for x in suceso_raices if variante_raiz(mdd, x) == 1]:
            if len(quedan) >= min(2, n_raiz):
                break
            _quietar_raiz(mdd, out, s_b)
            quedan.append(s_b)
            suceso_raices.remove(s_b)
    for s_vacia in suceso_raices:
        if len(quedan) >= min(2, n_raiz):
            break
        out[s_vacia:s_vacia + 0x20] = mdd[quedan[0]:quedan[0] + 0x20] if quedan else bytes(0x20)   # copia de una raiz valida (la de ceros daba 'UNREGISTERED indirect call target 0' al cargar, lr 0x8208018C)
        quedan.append(s_vacia)
    for k, s in enumerate(quedan):
        struct.pack_into("<I", out, rl + 4 * k, s)
    struct.pack_into("<I", out, 8, len(quedan))
    return bytes(out), n_raiz - len(quedan), quietas


def sanear_anims_raiz_b2(mdd):
    """B2: en las raices, desactiva los adornos marcados como animacion. La raiz de cada escenario es un registro
    de 0x20 B con DOS pares (n, puntero): (0, +0xC) y (4, +0x10); cada entrada es [flag, bloque, parametros, 0].
    El grupo 1 (BUDO) es el unico cuya raiz trae en el SEGUNDO par una entrada ACTIVA (flag != 0) cuyo bloque es un
    #AMP (particula), no un #AMM (animacion); medido 2026-10-18 en los 9 grupos. El port se cierra al dibujar la
    lista de nodos de esa raiz (generated/dbz3_recomp.23.cpp:706, lr 0x8208E5AC). Los grupos que funcionan nunca
    tienen esa combinacion (sus entradas con flag != 0 apuntan siempre a #AMM). Aqui se pone esa entrada a
    flag = 0 y parametros = 0, como las raices que si funcionan; el bloque se deja (no se mueve nada mas).
    Ademas (2026-10-18, crash con flag=0 en lr 0x8208E5AC): la entrada se QUITA de su lista (la lista se compacta y
    el contador baja) [H: el guest crea un objeto por entrada del segundo par], y el #AMP no tiene
    manejador (la tabla 0x82310110 no lo tiene). Los grupos que funcionan tienen el segundo par vacio en 3 de cada 4
    raices (g0, g2, g4, g7, g8). Devuelve (mdd, n entradas quitadas)."""
    out = bytearray(mdd)
    rl, n_raiz = le(mdd, 0x24), le(mdd, 8)
    if not rl:
        return mdd, 0
    n = 0
    for k in range(n_raiz):
        s = le(mdd, rl + 4 * k)
        if not s:
            continue
        for cf, af in ((0, 12), (4, 16)):
            cnt, arr = le(mdd, s + cf), le(mdd, s + af)
            if not (cnt and arr):
                continue
            entradas = [mdd[arr + 16 * j:arr + 16 * (j + 1)] for j in range(cnt)]
            quedan = []
            for ent in entradas:
                flag, blk = struct.unpack_from("<II", ent, 0)
                if flag and 0 < blk < len(mdd) - 4 and mdd[blk:blk + 4] != b"#AMM":
                    n += 1          # se quita de la lista (el bloque queda en el fichero, sin referencia)
                    continue
                quedan.append(ent)
            if len(quedan) != cnt:
                # lista compactada y contador reducido: el juego recorre solo `cnt` entradas
                for j, ent in enumerate(quedan):
                    out[arr + 16 * j:arr + 16 * (j + 1)] = ent
                struct.pack_into("<I", out, s + cf, len(quedan))
                if not quedan:   # lista vacia = puntero 0, como en los grupos que funcionan (g0, g2, g4, g7, g8)
                    struct.pack_into("<I", out, s + af, 0)
    return bytes(out), n


def sin_camaras_suceso(mdd):
    """B2: los #AMC del objeto 0 NO son la presentacion: son las camaras de la destruccion. Sus clips duran lo mismo
    que las animaciones de las raices (medido en los 9 grupos: nieve 420/270/98/282, base 604/211, ciudad 120/160,
    grupo 2 300, grupo 4 170/210); los grupos sin destruccion (1, 3, 5, 6) no traen ninguno. Con el guion del donante
    esos clips metian la camara en la montana o tras los edificios. Se quita la lista del objeto 0 (+0xD4 n, +0xD8)
    y mdd_a_zdd le pone las camaras de presentacion del donante, como a B1. Devuelve (mdd, n quitadas)."""
    ob = le(mdd, le(mdd, 0x20)) if le(mdd, 4) else 0
    n = le(mdd, ob + 0xD4) if ob else 0
    if not n or n == 0xFCFCFCFC:
        return mdd, 0
    out = bytearray(mdd)
    struct.pack_into("<II", out, ob + 0xD4, 0, 0)
    return bytes(out), n


def llamadas_efectos_b2(spx):
    """Llamadas al efecto de escenario del #SPX de B2: builtin 0x96 con argumentos
    [capa, instancia, efecto, bucle, retardo]. Devuelve [(offset, [args])]."""
    out = []
    if not spx:
        return out
    for m in re.finditer(rb"\x01(\x10.|\x20..|\x30....)\x02\x80\x02", spx, re.S):
        op = m.group(1)
        bid = op[1] if op[0] == 0x10 else (struct.unpack("<H", op[1:])[0] if op[0] == 0x20 else struct.unpack("<I", op[1:])[0])
        if bid != 0x96:
            continue
        st = spx.rfind(b"\x0b", 0, m.start()) + 1
        seg, args, i = spx[st:m.start()], [], 0
        while i < len(seg):
            if seg[i] == 0x08 and i + 1 < len(seg) and seg[i + 1] in (0x10, 0x20, 0x30):
                n = {0x10: 1, 0x20: 2, 0x30: 4}[seg[i + 1]]
                args.append(int.from_bytes(seg[i + 2:i + 2 + n], "little"))
                i += 2 + n
            elif seg[i] == 0x09 and i + 1 < len(seg) and seg[i + 1] == 0x30:
                args.append(round(struct.unpack_from("<f", seg, i + 2)[0], 3))
                i += 6
            else:
                i += 1
        out.append((m.start(), args))
    return out


def efectos_b2(fuente, grupo):
    """Productor de efectos de un escenario de B2: el #SPX (llamadas 0x96) y el banco de efectos que va
    justo DESPUES del #SPX en el AFS (#AMB con #AMT + N #AME v2). El conversor no los instala: usa el #SPX y
    el registro de 641 casillas del donante 44. Esta funcion los deja medidos para la tabla y el sidecar."""
    i = fuente.escenarios()[grupo]
    mdd, mad, mas, spx = fuente.piezas(grupo)
    banco = fuente.entrada(i + 5)
    efectos, texturas = [], []
    if banco[:4] == b"#AMB":
        n, t = struct.unpack_from("<2I", banco, 0x10)
        for k in range(n):
            o, s, _ty, _fl = struct.unpack_from("<4I", banco, t + 16 * k)
            if not s:
                continue
            if banco[o:o + 4] == b"#AME":
                efectos.append({"slot": k, "off": o, "size": s})
            elif banco[o:o + 4] == b"#AMT":
                texturas.append({"slot": k, "off": o, "size": s})
    return {"grupo": grupo, "entrada": i, "spx": len(spx or b""), "banco": len(banco) if banco[:4] == b"#AMB" else 0,
            "efectos": efectos, "texturas": texturas, "llamadas": llamadas_efectos_b2(spx), "mas": le(mas, 4)}


def tabla_efectos_b2(fuente, grupos=None):
    """Texto de la tabla por escenario: llamadas al efecto del #SPX y banco #AME que se descarta."""
    filas = ["grupo\t#SPX\tllamadas 0x96\tefectos usados\t#AME en banco\t#AMT en banco\t#MAS"]
    for g in (grupos if grupos is not None else range(len(fuente.escenarios()))):
        e = efectos_b2(fuente, g)
        ids = sorted({a[2] for _o, a in e["llamadas"] if len(a) >= 3 and isinstance(a[2], int)})
        filas.append("%d\t%d\t%d\t%s\t%d\t%d\t%d" % (g, e["spx"], len(e["llamadas"]), ",".join(map(str, ids)) or "-",
                                                    len(e["efectos"]), len(e["texturas"]), e["mas"]))
    return "\n".join(filas) + "\n"


def conservar_efectos_b2(fuente, grupo, carpeta):
    """Deja en la carpeta del mod el #SPX de B2 y su banco de efectos (convertido a #AMB/#ACE) como
    EVIDENCIA, sin tocar el bin instalado. B3 no puede ejecutar el #SPX de B2 tal cual: sus nativas estan
    renumeradas (0x96 de B2 no existe en B3); el registro de 641 casillas tambien es otro. El sidecar sirve
    para medir y para un futuro traductor de llamadas; el juego sigue con el #SPX del donante."""
    e = efectos_b2(fuente, grupo)
    carpeta = Path(carpeta)
    (carpeta / "efectos_b2.txt").write_text(tabla_efectos_b2(fuente, [grupo]), encoding="utf-8")
    i = e["entrada"]
    spx = fuente.piezas(grupo)[3]
    if spx:
        (carpeta / "spx_b2.bin").write_bytes(spx)
    banco = fuente.entrada(i + 5)
    if banco[:4] == b"#AMB":
        (carpeta / "banco_efectos_b2.bin").write_bytes(banco)
        try:
            (carpeta / "banco_efectos_b2_hd.amb").write_bytes(ps2hd.convert_block(banco))
        except ps2hd.Unsupported:
            pass
    return e


def variante_raiz(mdd, s):
    """Variante (0 = A, 1 = B) de una raiz del #MDD por el nombre de su primer hueso: 'BB<esc><V>nn...'
    (BBSNWA02 = A, BBSNWB03 = B, BBLNDC01 = C). El #MDD declara cuantas variantes tiene en +4 (1 o 2);
    medido 2026-10-18: los 4 grupos con +4=2 (Base 0, grupos 4, 7 y 8) son tambien los 4 cuyo guion de
    escenario (#SPX) llama a FUN_001b95b0 (el cambio de zona A->B; opcode 0x80 = 'call_ext', indice 0x40).
    Devuelve None si la raiz esta vacia o su #AMM no tiene huesos."""
    if not le(mdd, s):
        return None
    blk = le(mdd, le(mdd, s + 12) + 4)
    if mdd[blk:blk + 4] != b"#AMM":
        return None
    _n_anim, _t, nb, no = struct.unpack_from("<4I", mdd, blk + 0x10)
    if not nb:
        return None
    nm = mdd[blk + no:blk + no + 32].split(b"\0")[0].decode("latin1")
    return 1 if len(nm) > 5 and nm[5] == "B" else 0


def raices_zona_b(mdd):
    """B2 zona B (variante 1): al reves que raices_quietas_b2. Se quitan de la lista las raices de la variante
    A (y las vacias) y las de la variante B se quedan quietas en su cuadro 0: B3 repite en bucle toda animacion
    de raiz y el cuadro 0 es el estado inicial de la zona B (como en la zona A con raices_quietas_b2).
    Devuelve (mdd, quitadas, quietas)."""
    out = bytearray(mdd)
    rl, n_raiz = le(mdd, 0x24), le(mdd, 8)
    if not rl:
        return mdd, 0, 0
    quedan, quietas = [], 0
    for k in range(n_raiz):
        s = le(mdd, rl + 4 * k)
        if variante_raiz(mdd, s) != 1:
            continue
        nombres = 0
        for j in range(le(mdd, s)):
            blk = le(mdd, le(mdd, s + 12) + 16 * j + 4)
            if mdd[blk:blk + 4] != b"#AMM":
                continue
            n_anim, t, nb, no = struct.unpack_from("<4I", mdd, blk + 0x10)
            nombres += nb
            for a in range(n_anim):   # mismo recorrido de pistas que raices_quietas_b2
                fl, _v, _nf, off = struct.unpack_from("<4I", mdd, blk + t + 16 * a)
                per = 3 if fl & 0x10 else 2
                for b in range(nb if off else 0):
                    for i in range(per):
                        pista = le(mdd, blk + off + 4 * (per * b + i))
                        if not pista:
                            continue
                        q, n_c = blk + pista, le(mdd, blk + pista + 8)
                        paso, ini = (8, 2) if i == 0 else (16, 4)
                        v0 = mdd[q + 12 + ini:q + 12 + paso]
                        for c in range(1, n_c):
                            out[q + 12 + paso * c + ini:q + 12 + paso * (c + 1)] = v0
        quietas += bool(nombres)
        quedan.append(s)
    for k, s in enumerate(quedan):
        struct.pack_into("<I", out, rl + 4 * k, s)
    struct.pack_into("<I", out, 8, len(quedan))
    return bytes(out), n_raiz - len(quedan), quietas


def camaras_transicion(mdd):
    """Bloques #AMC (PS2) del objeto de la variante 0 de un #MDD de B2: las camaras del suceso con el que el
    juego pasa de la zona A a la B (la destruccion; nieve 420/270/98/282, base 604/211, grupo 4 70/170/210,
    ciudad 120/160; sus clips duran lo mismo que las animaciones de las raices de ese mismo escenario, medido
    en R7). Van al objeto 0 del bin de la zona B para que el guion del donante las reproduzca."""
    ob = le(mdd, le(mdd, 0x20)) if le(mdd, 4) else 0
    n = le(mdd, ob + 0xD4) if ob else 0
    if not n or n == 0xFCFCFCFC:
        return []
    d8 = le(mdd, ob + 0xD8)
    out = []
    for k in range(n):
        o = le(mdd, d8 + 4 * k)
        if mdd[o:o + 4] != b"#AMC":
            continue
        sig = min((i for i in (mdd.find(m, o + 4) for m in MAGICS_MDD) if i > o), default=len(mdd))
        out.append(mdd[o:sig])
    return out


def objetos_zona_b(mdd):
    """Zona B: el objeto de la variante 1 pasa a ser el objeto 0 del #ZDD (el juego dibuja el objeto 0; la
    variante 1 es la otra zona del escenario). Intercambia las dos primeras entradas de la lista de objetos
    (+0x20 del #MDD). Devuelve (mdd, ok)."""
    n, lp = le(mdd, 4), le(mdd, 0x20)
    if n < 2 or not lp:
        return mdd, False
    out = bytearray(mdd)
    struct.pack_into("<II", out, lp, le(mdd, lp + 4), le(mdd, lp))
    return bytes(out), True


def _spx_tokens(b, start):
    out, i = [], start
    while i < len(b):
        op = b[i]
        if op == 0x0b:
            out.append((i, ("SEP",))); i += 1; continue
        if op == 0x08 and i + 1 < len(b) and b[i + 1] in (0x10, 0x20, 0x30):
            n = {0x10: 1, 0x20: 2, 0x30: 4}[b[i + 1]]
            out.append((i, ("PUSH", int.from_bytes(b[i + 2:i + 2 + n], "little")))); i += 2 + n; continue
        if op == 0x09 and i + 1 < len(b) and b[i + 1] == 0x30:
            out.append((i, ("PUSHF",))); i += 6; continue
        if op == 0x01 and i + 1 < len(b) and b[i + 1] in (0x10, 0x20, 0x30):
            n = {0x10: 1, 0x20: 2, 0x30: 4}[b[i + 1]]
            out.append((i, ("CALL", int.from_bytes(b[i + 2:i + 2 + n], "little")))); i += 2 + n; continue
        if op == 0x02 and i + 2 < len(b) and b[i + 2] == 0x02:
            out.append((i, ("END", b[i + 1]))); i += 3; continue
        if op in (0x12, 0x13) and i + 2 < len(b):
            out.append((i, ("OP", op))); i += 3; continue
        out.append((i, ("RAW", op))); i += 1
    return out


def completar_registro(reg, donante):
    """X1esc 23/10: el registro de 641 casillas del donante 44 trae vacias casillas que el juego pide en combates largos
    (el builtin sub_820CCAF8 pide 110+tipo, 124 y 125; sin ellas: nombre NULL en sub_8208DEE0 y cierre). Los nativos
    50, 54 y 60 las traen. Se rellenan las casillas vacias del donante que traen al menos dos de esos tres (con la
    version del 50 si la tiene, si no la del 60 o la del 54). X1ESC_REG_COMPLETO=0 lo apaga (pruebas)."""
    if os.environ.get("X1ESC_REG_COMPLETO") == "0":
        return reg
    fuentes = []
    for d in (50, 60, 54):
        if d == donante:
            continue
        nat = hd_nativo(d)
        k = hijos_amb(nat, ">")
        r = nat[k[4][0]:k[4][0] + k[4][1]]
        fuentes.append((r, hijos_amb(r, ">")))
    ch = hijos_amb(reg, ">")
    hijos = [(reg[o:o + s] if s else None, ty) for o, s, ty, _f in ch]
    puestas = []
    for i in range(len(hijos)):
        if hijos[i][0] or i >= 641:
            continue
        con = [(r, c[i]) for r, c in fuentes if i < len(c) and c[i][1]]
        if len(con) >= 2:
            r, (o, sz, ty, _f) = con[0]
            hijos[i] = (r[o:o + sz], ty)
            puestas.append(i)
    if puestas:
        print("registro completado con casillas del 50/60/54: %s" % puestas)
    return amb_hd(hijos) if puestas else reg


def nieve_g7(fuente, spx, reg):
    """EXPERIMENTAL (X1ESC_NIEVE=1): la nieve de Montanas nevadas (B2 grupo 7). Traduce las llamadas 0x96 de B2 a la
    llamada 289 del #SPX de B3 (a3 = casillero #ACE del registro; medida solo la forma, el significado de a1/a2/a4 es [H])
    para los efectos 25..34 (los 35 y 36 no tienen entrada en el banco: se omiten) y mete esos #ACE en los casilleros
    521..530 del registro. Devuelve (spx, reg)."""
    def push(v):
        return bytes([0x08, 0x10, v]) if v < 256 else (bytes([0x08, 0x20]) + struct.pack("<H", v) if v < 65536 else bytes([0x08, 0x30]) + struct.pack("<I", v))
    def c289(a0, a1, a2, a3, a4, a5):
        return (bytes.fromhex("0b") + push(a0) + push(a1) + bytes.fromhex("0830") + struct.pack("<I", a2) + push(a3)
                + bytes.fromhex("0930") + struct.pack("<f", a4) + push(a5) + bytes.fromhex("012021010280021210" + "18"))
    def espera(n):
        return bytes([0x0b]) + push(n) + bytes([0x01, 0x30, 0x5c, 0x14, 0x00, 0x00, 0x02, 0x73, 0x02, 0x12, 0x10, 0x04])
    b2spx = fuente.piezas(7)[3]
    llam = llamadas_efectos_b2(b2spx)
    banco = ps2hd.convert_block(fuente.entrada(fuente.escenarios()[7] + 5))
    kids = hijos_amb(banco, ">")
    bloques, j = [], 0
    for _off, a in llam:
        if len(a) < 5 or not isinstance(a[2], int) or not 25 <= a[2] <= 34 or not kids[a[2]][1]:
            continue
        bloques.append(c289(0, 0, 3204 - 16 * j, 521 + a[2] - 25, 0.0, int(a[3])))
        j += 1
    nuevo = b"".join(bloques) + espera(30)
    T = _spx_tokens(spx, struct.unpack_from("<I", spx, 20)[0])
    ins = None
    for k, (off, t) in enumerate(T):
        if t[0] == "SEP" and k + 1 < len(T) and T[k + 1][1] == ("RAW", 0x76):
            ins = off
    if ins is None:
        raise ValueError("nieve_g7: no encuentro la cola del #SPX del donante")
    spx = spx[:ins] + nuevo + spx[ins:]
    ch = hijos_amb(reg, ">")
    hijos = [(reg[o:o + s] if s else None, ty) for o, s, ty, _f in ch]
    for e in range(25, 35):
        o, s = kids[e][0], kids[e][1]
        if s:
            hijos[521 + e - 25] = (banco[o:o + s], 0xFFFFFFFF)
    print("nieve_g7 (experimental): %d llamadas 289, #SPX %d B, #ACE en casilleros 521..530" % (j, len(spx)))
    return spx, amb_hd(hijos)


def escenario_b2(fuente, grupo, donante=44, zona=0, escala=1.0):
    """Bin HD de un escenario de B2 (o B1): geometria, grafo, colision y camaras del juego de origen; script
    (#SPX) y registro de efectos (#AMB de 641 casillas) del escenario nativo `donante` de la HD del jugador. El
    registro de B2 (25-37 hijos sin indices fijos) no encaja en la tabla de 641 casillas que el juego indexa por
    numero fijo. B1 no trae camaras de presentacion en el objeto 0: van las del donante.
    zona=0 (A, por defecto): variante inicial (torre en pie), camaras de presentacion del donante.
    zona=1 (B): la otra zona (objeto de la variante 1 como objeto 0, sus raices quietas) y las camaras de la
    transicion de B2 (las del objeto de la variante 0) en el objeto 0."""
    mdd, mad, mas, _spx = fuente.piezas(grupo)
    if zona:
        transicion = camaras_transicion(mdd)
        mdd, ok = objetos_zona_b(mdd)
        if not ok:
            raise SystemExit("el grupo %d de %s no tiene variante B (un solo objeto)" % (grupo, fuente.juego))
        mdd, quitadas, quietas = raices_zona_b(mdd)
        mdd, saneadas = sanear_anims_raiz_b2(mdd)
        mdd, sucesos = sin_camaras_suceso(mdd)
        print("arreglos B2 zona B: %d raices de la variante A fuera, %d de la B quietas en su cuadro 0, "
              "%d camaras del objeto de la variante B fuera, %d animaciones de raiz desactivadas, "
              "%d camaras de transicion" % (quitadas, quietas, sucesos, saneadas, len(transicion)))
        nat = hd_nativo(donante)
        k = hijos_amb(nat, ">")
        spx = nat[k[3][0]:k[3][0] + k[3][1]]
        reg = completar_registro(nat[k[4][0]:k[4][0] + k[4][1]], donante)
        camaras = []
        for b in transicion:
            try:
                hd = ps2hd.convert_block(b)
            except ps2hd.Unsupported:
                continue
            if ps2hd.MAGIC.get(b[:4], b[:4]) == b"#ACC":
                camaras.append(hd)
        cas = nat[k[2][0]:k[2][0] + k[2][1]] if not le(mas, 4) else camara(mas, b"#CAS")   # #MAS vacio (grupo 5): el #CAS del donante
        if not le(mas, 4):
            print("arreglos B2: el #MAS del grupo %d esta vacio: se usa el #CAS del donante %d" % (grupo, donante))
        return amb_hd([(mdd_a_zdd(mdd, pistas_min=3, camaras=camaras, escala=escala), 0xFFFFFFFF), (nat[k[1][0]:k[1][0] + k[1][1]] if os.environ.get("X1ESC_CAD_DONANTE") else camara(mad, b"#CAD"), 0xFFFFFFFF),
                       (cas, 0xFFFFFFFF), (spx, 8), (reg, 0xFFFFFFFF)])
    if os.environ.get("X1ESC_OCULTAR"):      # DIAGNOSTICO: "10,11" oculta esas partes del #AMO grande (truco FFFFFF/FFFF)
        mdd = bytearray(mdd)
        for off0, tam0 in bloques_mdd(bytes(mdd)):
            if mdd[off0:off0 + 4] != b"#AMO" or tam0 < 500000:
                continue
            pm = amo2awo.PS2AMO(bytes(mdd[off0:off0 + tam0]))
            for k in os.environ["X1ESC_OCULTAR"].split(","):
                po = pm.amgs[0].all_parts[int(k)].off
                struct.pack_into("<II", mdd, off0 + po, 0xFFFFFF, 0xFFFF)
        mdd = bytes(mdd)
    mdd, quitadas, quietas = raices_quietas_b2(mdd, todas=(grupo == 0 and os.environ.get('X1ESC_BASE_RAIZ_B', '1') != '0'))
    mdd, saneadas = sanear_anims_raiz_b2(mdd)
    mdd, sucesos = sin_camaras_suceso(mdd)
    print("arreglos B2: %d piezas de destruccion quitadas, %d quietas en su cuadro 0, %d camaras de suceso "
          "fuera, %d animaciones de raiz desactivadas" % (quitadas, quietas, sucesos, saneadas))
    nat = hd_nativo(donante)
    k = hijos_amb(nat, ">")
    spx = nat[k[3][0]:k[3][0] + k[3][1]]
    reg = completar_registro(nat[k[4][0]:k[4][0] + k[4][1]], donante)
    camaras = camaras_hd(nat[k[0][0]:k[0][0] + k[0][1]])
    if os.environ.get("X1ESC_NIEVE") and grupo == 7 and fuente.juego == JUEGOS["b2"][0]:
        spx, reg = nieve_g7(fuente, spx, reg)
    cas = nat[k[2][0]:k[2][0] + k[2][1]] if not le(mas, 4) else camara(mas, b"#CAS")   # #MAS vacio (grupo 5): el #CAS del donante
    if not le(mas, 4):
        print("arreglos B2: el #MAS del grupo %d esta vacio: se usa el #CAS del donante %d" % (grupo, donante))
    return amb_hd([(mdd_a_zdd(mdd, pistas_min=3, camaras=camaras, escala=escala), 0xFFFFFFFF), (nat[k[1][0]:k[1][0] + k[1][1]] if os.environ.get("X1ESC_CAD_DONANTE") else camara(mad, b"#CAD"), 0xFFFFFFFF),
                   (cas, 0xFFFFFFFF), (spx, 8), (reg, 0xFFFFFFFF)])


def escenario_b3ps2(amb, registro):
    """Bin HD desde un escenario de B3 PS2 (mismo contenido que la HD): sirve para comprobar el conversor.
    El registro de efectos (641 casillas) se pasa ya en formato HD."""
    k = hijos_amb(amb, "<")
    p = [amb[o:o + s] for o, s, _, _ in k]
    return amb_hd([(mdd_a_zdd(p[0]), 0xFFFFFFFF), (camara(p[1], b"#CAD"), 0xFFFFFFFF),
                   (camara(p[2], b"#CAS"), 0xFFFFFFFF), (p[3], 8), (registro, 0xFFFFFFFF)])


STAGE_TOML = """# Escenario nuevo (convertido de {juego} desde la ISO del jugador por tools/escenarios_convertir.py).
# Anade una casilla al selector de escenarios del Duelo (src/escenarios.cpp); no sustituye nada.
[[escenario]]
nombre = "{nombre}"
titulo = "{titulo}"     # nombre para el HUB y los retos
entrada = {entrada}    # data_cmn.afs: geometria (entrada nueva)
menu = {menu}       # data_cmn.afs: imagenes de la casilla (circulo, vista y rotulo)
id_se = {id_se}       # data_cmn.afs: sonido/ambiente (del escenario nativo donante)
tema = {tema}          # luz y cielo: funcion de tema del registro {tema} (generica)
{zona_b_linea}"""
ZONA_B_LINEA = "zona_b = {entrada_a}     # esta variante es la zona B de la entrada {entrada_a} (al caer por el borde se pasa a ella: dbz3_escenarios_zonas_b)" + chr(10) + "selector = false     # la zona B no sale en el selector: solo se llega a ella en el combate" + chr(10)

# ---------------------------------------------------------------------------------------------------------
# Imagenes de la casilla del selector (#AZT propio: 0 circulo, 1 vista, 2 rotulo, 3 bola), con las medidas de las
# nativas (data_<idioma>.afs 2043: circulo 228 px en 256, vista 512x372 en 512 con fundido abajo, rotulos
# 288x48 HD = 192x32 logicos, letra marron con borde blanco)
# ---------------------------------------------------------------------------------------------------------
FUENTE = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts" / "ARLRDBD.TTF"
MARRON, BLANCO = (66, 18, 10), (242, 242, 242)


def rotulo(texto, w=288, h=48):
    """Nombre como los rotulos del selector: letra redondeada estrechada para caber, marron con borde blanco."""
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
    k = 4
    from capsulas import load_font  # noqa: PLC0415 (Arial Rounded viene con Office: sin ella, Roboto del zip)
    font = load_font(str(FUENTE), 38 * k)
    x0, y0, x1, y1 = font.getbbox(texto)
    m = Image.new("L", (x1 - x0 + 16 * k, 48 * k))
    ImageDraw.Draw(m).text((8 * k - x0, (48 * k - (y1 - y0)) // 2 - y0), texto, font=font, fill=255)
    ancho = min(m.width, (w - 10) * k)
    m = m.resize((ancho, m.height), Image.LANCZOS)
    borde = m.filter(ImageFilter.MaxFilter(2 * 3 * k + 1)).filter(ImageFilter.GaussianBlur(k))
    lienzo = Image.new("RGBA", (w * k, h * k), (0, 0, 0, 0))
    x = (w * k - ancho) // 2
    lienzo.paste(Image.new("RGBA", m.size, BLANCO + (255,)), (x, 0), borde)
    lienzo.paste(Image.new("RGBA", m.size, MARRON + (255,)), (x, 0), m)
    return lienzo.resize((w, h), Image.LANCZOS)


def vistas(foto):
    """Circulo (228 px, ojo de pez suave y borde difuminado) y vista ancha (512x372, fundido abajo)."""
    import numpy as np
    from PIL import Image
    im = foto.convert("RGB")
    W, H = im.size
    ancho = min(W, int(H * 512 / 372))
    alto = int(ancho * 372 / 512)
    v = im.crop(((W - ancho) // 2, (H - alto) // 2, (W + ancho) // 2, (H + alto) // 2)).resize((512, 372), Image.LANCZOS)
    filas = np.arange(372)
    a = 255.0 * np.clip(1 - (filas - 130) / (300 - 130), 0.04, 1)[:, None] * np.ones((1, 512))
    a[300:] = 0
    vista = Image.merge("RGBA", (*v.split(), Image.fromarray(a.astype(np.uint8))))
    lado = min(W, H)
    c = np.asarray(im.crop(((W - lado) // 2, (H - lado) // 2, (W + lado) // 2, (H + lado) // 2))
                   .resize((456, 456), Image.LANCZOS), np.float64)
    yy, xx = np.mgrid[0:228, 0:228]
    dx, dy = (xx - 113.5) / 114, (yy - 113.5) / 114
    r = np.sqrt(dx * dx + dy * dy)
    f = np.where(r > 0, np.sin(np.clip(r, 0, 1) * np.pi / 2) / np.maximum(r, 1e-6), 1)
    sx = np.clip((dx * f * 0.98 + 1) * 227.5, 0, 455).astype(int)
    sy = np.clip((dy * f * 0.98 + 1) * 227.5, 0, 455).astype(int)
    rgb = c[sy, sx]
    al = np.clip((110 - r * 114) / 16, 0, 1) * 255
    circulo = Image.fromarray(np.dstack([rgb, al]).astype(np.uint8))
    return circulo, vista


def _modelos_escenario(zdd):
    """Modelos del grafo (sub-mallas de cada objeto con la textura del objeto) como model_render.Model."""
    import model_render as MR  # noqa: PLC0415 (numpy/PIL solo para las imagenes)
    R = lambda o: struct.unpack_from(">I", zdd, o)[0]  # noqa: E731
    marcas = []
    for mg in (b"#AWO", b"#AZT", b"#ACM", b"#ACC", b"#ACE"):
        i = 0
        while (i := zdd.find(mg, i + 1)) > 0:
            marcas.append(i)
    marcas.sort()

    def blk(o):
        return zdd[o:next((a for a in marcas if a > o), len(zdd))]
    out = []
    for k in range(R(4)):
        ob = R(R(0x20) + 4 * k)
        tex, sa = R(ob + 0xA4), R(ob + 0xA8)
        for j in range(R(ob + 0x90)):
            try:
                out.append(MR.Model(amb_hd([(blk(R(R(sa + 4 * j) + 8)), 0xFFFFFFFF), (blk(tex), 0xFFFFFFFF)])))
            except Exception:  # noqa: BLE001 (una pieza rara no impide la foto)
                pass
    return out


def foto_escenario(zdd, W=1280, H=744, fov=58.0):
    """Foto en perspectiva del escenario (sin GPU): desde el borde del suelo hacia el centro, con las
    texturas y el color de vertice (los escenarios no llevan luz). Sirve para el circulo y la vista."""
    import numpy as np
    from PIL import Image
    ms = _modelos_escenario(zdd)
    if not ms:
        return Image.new("RGB", (W // 2, H // 2), (90, 120, 170))
    P0, _ = ms[0].posed(None)                      # B2/B3: la primera sub-malla es el suelo
    c = (P0.min(0) + P0.max(0)) / 2
    rad = max(np.ptp(P0[:, 0]), np.ptp(P0[:, 2])) / 2
    d = max(300.0, min(1400.0, 1.6 * rad))         # medido a ojo con el grupo 2 de B2 (420 de 350)
    suelo = P0[:, 1].max()
    cerca = max(8.0, 0.9 * (d - np.ptp(P0[:, 2]) / 2))   # nada entre el ojo y el borde del suelo
    if rad > 800:                                  # B1: todo el escenario es una malla; suelo en y = 0
        c, d, suelo, cerca = np.zeros(3), 700.0, 0.0, 8.0
    ojo = np.array([c[0], suelo + 0.11 * d, c[2] - d])
    mira = np.array([c[0], suelo + 0.06 * d, c[2]])
    f = (mira - ojo) / np.linalg.norm(mira - ojo)
    r = np.cross(f, [0, 1, 0])
    r /= np.linalg.norm(r)
    u = np.cross(r, f)
    ss = 2
    W2, H2 = W * ss, H * ss
    foc = (W2 / 2) / np.tan(np.radians(fov) / 2)
    color = np.zeros((H2, W2, 3), np.float32)
    color[:] = (0.55, 0.65, 0.8)
    zbuf = np.full((H2, W2), np.inf)

    def tex_bilineal(img, uu, vv):
        h, w = img.shape[:2]
        x = np.mod(uu, 1.0) * w - 0.5
        y = np.mod(vv, 1.0) * h - 0.5
        x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
        fx, fy = (x - x0)[:, None], (y - y0)[:, None]
        g = lambda yy, xx: img[yy % h, xx % w]  # noqa: E731
        return (g(y0, x0) * (1 - fx) * (1 - fy) + g(y0, x0 + 1) * fx * (1 - fy) +
                g(y0 + 1, x0) * (1 - fx) * fy + g(y0 + 1, x0 + 1) * fx * fy)
    capas = []
    for m in ms:
        P, _ = m.posed(None)
        col = np.array([[(v["marker"] >> 16) & 255, (v["marker"] >> 8) & 255, v["marker"] & 255]
                        for v in m.avb.vertices()], np.float32) / 255.0
        dd = P - ojo
        x, y, z = dd @ r, dd @ u, dd @ f
        cielo = np.ptp(P, axis=0).max() > 4 * rad and len(m.tris) < 1000   # cupula (pocos triangulos): al fondo
        for t, mi in zip(m.tris, m.tri_mat):
            capas.append((float(z[t].mean()), cielo, m, t, mi, x, y, z, col))
    capas.sort(key=lambda c_: (not c_[1], -c_[0]))
    for _, cielo, m, t, mi, x, y, z, col in capas:
        if (z[t] < cerca).any():
            continue
        sx = W2 / 2 + foc * x[t] / z[t]
        sy = H2 / 2 - foc * y[t] / z[t]
        x0, x1 = int(max(0, np.floor(sx.min()))), int(min(W2 - 1, np.ceil(sx.max())))
        y0, y1 = int(max(0, np.floor(sy.min()))), int(min(H2 - 1, np.ceil(sy.max())))
        if x1 < x0 or y1 < y0:
            continue
        ax, ay, bx, by, cx, cy = sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9:
            continue
        xs = np.arange(x0, x1 + 1) + 0.5
        ys = np.arange(y0, y1 + 1)[:, None] + 0.5
        w1 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / den
        w2 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / den
        w3 = 1 - w1 - w2
        dentro = (w1 >= 0) & (w2 >= 0) & (w3 >= 0)
        if not dentro.any():
            continue
        zz = 1 / np.maximum(w1 / z[t[0]] + w2 / z[t[1]] + w3 / z[t[2]], 1e-9)
        zb = zbuf[y0:y1 + 1, x0:x1 + 1]
        vis = dentro if cielo else dentro & (zz < zb)
        if not vis.any():
            continue
        p = [w[vis] / z[t[i]] * zz[vis] for i, w in enumerate((w1, w2, w3))]
        uv = sum(m.uv[t[i]] * p[i][:, None] for i in range(3))
        vc = sum(col[t[i]] * p[i][:, None] for i in range(3))
        mat = m.mats[mi]
        if mat["tex"] < len(m.tex):
            tx = tex_bilineal(m.tex[mat["tex"]], uv[:, 0], uv[:, 1])
            rgb, a = tx[:, :3] * vc, tx[:, 3]
        else:
            rgb, a = vc, np.ones(len(vc))
        dst = color[y0:y1 + 1, x0:x1 + 1]
        dst[vis] = dst[vis] * (1 - a[:, None]) + rgb * a[:, None]
        if not cielo:
            zs = zb[vis]
            zs[a > 0.5] = zz[vis][a > 0.5]
            zb[vis] = zs
    img = Image.fromarray((np.clip(color, 0, 1) * 255).astype(np.uint8))
    return img.resize((W, H), Image.LANCZOS)


def azt_menu(circulo, vista, rot, plantilla, bola=45):
    """#AZT de 4 texturas (DDS A8R8G8B8) con entradas copiadas de las nativas del selector (`plantilla`:
    #AZT de data_<idioma>.afs 2043; 17 = circulo, 30 = vista, 0 = rotulos, 45..56 = bolas de color): 0 circulo,
    1 vista, 2 rotulo y 3 la bola de color `bola` del propio juego (sale de los archivos del jugador)."""
    import io
    import numpy as np
    from PIL import Image
    P = lambda o: struct.unpack_from(">I", plantilla, o)[0]  # noqa: E731
    idx = P(0x14)
    eb = P(idx + 4 * bola)
    bola_img = Image.open(io.BytesIO(plantilla[P(eb + 0x14):P(eb + 0x14) + P(eb + 0x18)])).convert("RGBA")
    piezas = []
    for img, t, lienzo, hd, logico in ((circulo, 17, (256, 256), (228, 228), (152, 152)),
                                       (vista, 30, (512, 512), (512, 372), (440, 320)),
                                       (rot, 0, (512, 64), (288, 48), (192, 32)),
                                       (bola_img, bola, (256, 256), (228, 228), (152, 152))):
        ent = bytearray(plantilla[P(idx + 4 * t):P(idx + 4 * t) + 0x30])
        # cabecera DDS A8R8G8B8 (la del circulo 17: el atlas de rotulos 0 es A4R4G4B4 en algunos idiomas)
        do = P(P(idx + 4 * 17) + 0x14)
        dds = bytearray(plantilla[do:do + 128])
        W, H = lienzo
        struct.pack_into("<IIII", dds, 12, H, W, W * 4, 0)        # alto, ancho, paso; sin mipmaps
        struct.pack_into("<I", dds, 28, 0)
        c = Image.new("RGBA", lienzo, (0, 0, 0, 0))
        c.paste(img, (0, 0))
        px = np.asarray(c)[..., [2, 1, 0, 3]].astype(np.uint8).tobytes()
        log2 = lambda v: max(0, (v - 1).bit_length())  # noqa: E731
        struct.pack_into(">HHHHHH", ent, 8, hd[0], hd[1], log2(logico[0]), log2(logico[1]), logico[0], logico[1])
        piezas.append((ent, bytes(dds) + px))
    # Indice de 64 huecos (n = 4): los 4 nuestros y el resto a una entrada vacia (recurso 0). Mientras el
    # banco global es este #AZT, algun sprite del juego pide su textura (vista 37, medido 2026-10-15) y sin
    # hueco leia basura detras del indice (cierre con el alta de B1; con las de B2 salia un 0 por suerte).
    n, huecos = len(piezas), 64
    out = bytearray(plantilla[:0x20])
    struct.pack_into(">II", out, 0x10, n, 0x20)
    out += bytes(4 * huecos)
    ents = []
    for ent, _ in piezas:
        out += bytes(align(len(out), 16) - len(out))
        ents.append(len(out))
        out += ent
    out += bytes(align(len(out), 16) - len(out))
    vacia = len(out)
    out += bytes(0x30)
    for k, (ent, datos) in enumerate(piezas):
        out += bytes(align(len(out), 0x80) - len(out))
        struct.pack_into(">I", out, ents[k], k)
        struct.pack_into(">II", out, ents[k] + 0x14, len(out), len(datos))
        out += datos
    for k in range(huecos):
        struct.pack_into(">I", out, 0x20 + 4 * k, ents[k] if k < n else vacia)
    return bytes(out + bytes(align(len(out), 32) - len(out)))


def plantilla_menu():
    """#AZT nativo del selector (data_eng.afs 2043) del jugador: medidas y cabeceras de las texturas."""
    d = entrada_afs(US / "data_eng.afs", 2043)
    return lzx(d, False) if d[:4] == b"\x0f\xf5\x12\xee" else d


def instalar(datos, mods, nombre, entrada, juego, id_se=45, tema=16, foto=None, rotulo_txt=None, zona_b=0):
    from PIL import Image
    carpeta = Path(mods) / nombre
    menu = entrada + 100
    for n, bin_ in ((entrada, datos), (menu, None)):
        if bin_ is None:
            hijos = hijos_amb(datos, ">")
            imagen = Image.open(foto) if foto else foto_escenario(datos[hijos[0][0]:hijos[0][0] + hijos[0][1]])
            circulo, vista = vistas(imagen)
            bola = 45 + sum((rotulo_txt or nombre).encode("utf-8")) % 12      # color fijo por escenario
            bin_ = azt_menu(circulo, vista, rotulo(rotulo_txt or nombre), plantilla_menu(), bola)
        dst = carpeta / "us" / "data_cmn.afs" / str(n)
        dst.mkdir(parents=True, exist_ok=True)
        comp = lzx(bin_, True)
        (dst / "geom.bin").write_bytes(comp + bytes(align(len(comp), 0x800) - len(comp)))
    (carpeta / "stage.toml").write_text(STAGE_TOML.format(juego=juego, nombre=nombre.upper()[:15], titulo=rotulo_txt or nombre,
                                                          entrada=entrada,
                                                          menu=menu, id_se=id_se, tema=tema,
                                                          zona_b_linea=ZONA_B_LINEA.format(entrada_a=zona_b) if zona_b else ""),
                                        encoding="utf-8")
    (carpeta / "manifest.txt").write_text(
        "name=%s\ndescription=Escenario de %s convertido desde tu ISO (casilla nueva, no sustituye nada)\n"
        "type=stage\nversion=1\n" % (nombre, juego), encoding="utf-8")
    return carpeta


# ---------------------------------------------------------------------------------------------------------
# Comprobacion contra los pares nativos (B3 PS2 GH <-> HD)
# ---------------------------------------------------------------------------------------------------------
def etiquetas_modelo(z, m):
    """{off absoluto: etiqueta} de los puntos internos de un #AWO (huesos, listas, colision)."""
    H = lambda o: struct.unpack_from(">I", z, m + o)[0]  # noqa: E731
    nb, hb, namg, tbl, nlist = (H(0x10 + 4 * j) for j in range(5))
    et = {m: "base"}
    nbloque = 0
    for i in range(nb):
        et[m + hb + 0x20 * i] = "hueso%d" % i
        lp = H(hb + 0x20 * i + 4)
        for k in range(nlist if lp else 0):
            et[m + lp + 16 * k] = "lista%d.%d" % (i, k)
            g3 = H(lp + 16 * k + 12)
            if not g3:
                continue
            c = "col%d" % nbloque
            nbloque += 1
            et[m + g3] = c
            et[m + g3 + 0x40] = c + ".cab"
            p, j = H(g3 + 0x48), 0
            while p:
                va, ta = H(p + 4), H(p + 8)
                et[m + p] = "%s.n%d" % (c, j)
                et[m + va] = "%s.n%d.v" % (c, j)
                et[m + ta] = "%s.n%d.t" % (c, j)
                for t in range(H(ta + 12)):
                    et[m + ta + 16 + 32 * t] = "%s.n%d.t%d" % (c, j, t)
                p, j = H(p), j + 1
    return et


def firma_zdd(z):
    """Recorre un #ZDD HD con el esquema del reubicador: {campo: valor o destino etiquetado}."""
    R = lambda o: struct.unpack_from(">I", z, o)[0]  # noqa: E731
    bloques, mods = {}, {}
    pos, cuenta = 0x40, {}
    for mg in (b"#AZT", b"#ACC", b"#AWO", b"#AWG", b"#ACM", b"#ACE"):
        i = 0
        while (i := z.find(mg, i + 1)) > 0:
            if mg == b"#AWO":
                mods[i] = len(mods)
            if mg != b"#AWG":
                bloques[i] = "%s%d" % (mg.decode()[1:], sum(1 for v in bloques.values() if v.startswith(mg.decode()[1:])))
    del pos, cuenta
    internos = {}
    for m, k in mods.items():
        for a, e in etiquetas_modelo(z, m).items():
            internos[a] = "AWO%d.%s" % (k, e)
    out, nombre, regiones = {}, {}, []

    def reg(o, tam, et):
        nombre.setdefault(o, et)
        regiones.append((o, tam, et))

    campos = []

    def struct_(o, tam, et, ptrs):
        reg(o, tam, et)
        for i in range(0, tam, 4):
            if i in ptrs:
                campos.append(("%s+%X" % (et, i), R(o + i)))
            else:
                out["%s+%X" % (et, i)] = R(o + i)

    vistos = set()

    def st50(s, et):
        if not s or s in vistos:
            return
        vistos.add(s)
        struct_(s, 0x50, et, (0x34, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C))
        for af, cf in ((0x48, 0x2C), (0x4C, 0x30)):
            n, a = R(s + cf), R(s + af)
            if n and a:
                struct_(a, 4 * n, et + ".a%X" % af, range(0, 4 * n, 4))
        for f in (0x38, 0x3C, 0x34, 0x40, 0x44):
            st50(R(s + f), et + ".%X" % f)

    def arrays16(s, et):
        # +8 (parametros): el cocinado de la HD apunta siempre a los 16 B siguientes a la entrada; solo cuenta
        # (y se compara su contenido) si la entrada esta activa (+0 != 0)
        for cf, af in ((0, 12), (4, 16)):
            n, a = R(s + cf), R(s + af)
            if n and a:
                for k in range(n):
                    e = a + 16 * k
                    et2 = et + ".a%X[%d]" % (af, k)
                    reg(e, 16, et2)
                    out[et2 + "+0"], out[et2 + "+C"] = R(e), R(e + 12)
                    campos.append((et2 + "+4", R(e + 4)))
                    if R(e):
                        p = R(e + 8)
                        out[et2 + "+8"] = tuple(R(p + 4 * j) for j in range(4))

    n_obj, n_raiz, ol, rl = R(4), R(8), R(0x20), R(0x24)
    out["hdr"] = (n_obj, n_raiz)
    for k in range(n_obj):
        ob = R(ol + 4 * k)
        e = "obj%d" % k
        struct_(ob, 0xE0, e, (0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD8))
        for j in range(R(ob + 0x90)):
            s = R(R(ob + 0xA8) + 4 * j)
            struct_(s, 0x20, "%s.sm%d" % (e, j), (8, 12, 16))
            arrays16(s, "%s.sm%d" % (e, j))
        for pf, cf, tam, np_ in ((0xAC, 0x94, 0x20, 5), (0xB0, 0x98, 0x30, 8), (0xB4, 0x9C, 0x20, 5),
                                 (0xB8, 0xA0, 0x30, 8)):
            n, a = R(ob + cf), R(ob + pf)
            if n and a:
                struct_(a, tam * n, "%s.r%X" % (e, pf), [tam * q + 4 * j for q in range(n) for j in range(np_)])
        for f in (0xBC, 0xC4, 0xCC, 0xC0, 0xC8, 0xD0):
            st50(R(ob + f), "%s.t%X" % (e, f))
    for k in range(n_raiz):
        struct_(R(rl + 4 * k), 0x20, "raiz%d" % k, (8, 12, 16))
        arrays16(R(rl + 4 * k), "raiz%d" % k)
    regiones.sort()
    rs = [r[0] for r in regiones]

    def destino(v):
        if v == 0:
            return "0"
        for t in (bloques, internos, nombre):
            if v in t:
                return t[v]
        i = bisect.bisect_right(rs, v) - 1
        if i >= 0 and regiones[i][0] < v < regiones[i][0] + regiones[i][1]:
            return "%s@+%X" % (regiones[i][2], v - regiones[i][0])
        return "?"
    for c, v in campos:
        out[c] = "->" + destino(v)
    return out


def cmd_comprobar(a):
    ps2_afs = RAIZ / "ps2_games" / "Budokai 3 Greatest Hits (USA)" / "USR" / "data_cmn.afs"
    if not ps2_afs.exists():
        raise SystemExit("falta %s (pares de referencia)" % ps2_afs)
    fallos = 0
    for e in (a.escenarios or range(44, 70, 2)):
        amb = entrada_afs(ps2_afs, e)
        nat = hd_nativo(e)
        k4 = hijos_amb(nat, ">")[4]
        nuestro = escenario_b3ps2(amb, nat[k4[0]:k4[0] + k4[1]])
        zn = [nuestro[o:o + s] for o, s, _, _ in hijos_amb(nuestro, ">")]
        zh = [nat[o:o + s] for o, s, _, _ in hijos_amb(nat, ">")]
        fa, fb = firma_zdd(zn[0]), firma_zdd(zh[0])
        malos = sorted(k for k in set(fa) | set(fb) if fa.get(k) != fb.get(k))
        otros = [i for i in (1, 2, 3) if zn[i] != zh[i]]
        punteros = sum(1 for v in fb.values() if isinstance(v, str))
        print("escenario %d: grafo %d campos (%d punteros), distintos %d; camaras/script/registro %s"
              % (e, len(fb), punteros, len(malos), "identicos" if not otros else "DISTINTOS %s" % otros))
        for k in malos[:8]:
            print("    %s: nuestro %s / nativo %s" % (k, fa.get(k), fb.get(k)))
        fallos += bool(malos) + bool(otros)
    print("OK: el conversor reproduce los escenarios nativos" if not fallos else "FALLOS: %d" % fallos)
    return 1 if fallos else 0


def cmd_lista(a):
    b2 = IsoB2(a.iso, a.juego)
    print("ISO:", b2.ruta)
    for g, i in enumerate(b2.escenarios()):
        mdd = b2.entrada(i + 2)
        tipos = {}
        for off, _ in bloques_mdd(mdd):
            tipos[mdd[off:off + 4].decode()] = tipos.get(mdd[off:off + 4].decode(), 0) + 1
        print("  grupo %d: entradas %d-%d  #MDD %7d B  %s" % (g, i, i + 2, len(mdd), tipos))


def cmd_mod(a):
    if a.bin and Path(a.bin).exists():          # bin ya convertido (rehacer solo las imagenes)
        datos = Path(a.bin).read_bytes()
    else:
        datos = escenario_b2(IsoB2(a.iso, a.juego), a.grupo, a.donante, zona=1 if a.zona == "b" else 0,
                           escala=a.caja_escala)
    base = ENTRADA_BASE + (20 if a.juego == "b1" else 0)
    entrada = a.entrada if a.entrada is not None else base + (40 + a.grupo if a.zona == "b" else a.grupo)   # menu = entrada + 100
    nombre = a.nombre or "%s ESCENARIO %d%s" % (a.juego.upper(), a.grupo, " ZONA B" if a.zona == "b" else "")
    zona_b = base + a.grupo if a.zona == "b" else 0      # la entrada de la variante A (la de enfrente)
    carpeta = instalar(datos, a.mods or (RAIZ / "mods"), nombre, entrada, JUEGOS[a.juego][0], id_se=a.donante + 1,
                       tema=a.tema, foto=a.foto, rotulo_txt=a.rotulo, zona_b=zona_b)
    print("mod '%s' -> %s (entradas %d y %d, %d B)" % (nombre, carpeta, entrada, entrada + 100, len(datos)))
    try:
        conservar_efectos_b2(IsoB2(a.iso, a.juego), a.grupo, carpeta)
    except Exception as e:  # noqa: BLE001 (el sidecar no puede impedir instalar el escenario)
        print("aviso: no se pudo dejar la tabla de efectos de B2: %s" % e)
    if a.tabla_efectos:
        print(tabla_efectos_b2(IsoB2(a.iso, a.juego), [a.grupo]))
    if a.bin and not Path(a.bin).exists():
        Path(a.bin).write_bytes(datos)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = p.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("lista")
    s.add_argument("--iso")
    s.add_argument("--juego", choices=sorted(JUEGOS), default="b2")
    s.set_defaults(fn=cmd_lista)
    s = sp.add_parser("mod")
    s.add_argument("grupo", type=int)
    s.add_argument("--juego", choices=sorted(JUEGOS), default="b2", help="b2 (por defecto) o b1")
    s.add_argument("--nombre")
    s.add_argument("--entrada", type=int)
    s.add_argument("--mods", help="carpeta mods del juego (por defecto ./mods)")
    s.add_argument("--iso")
    s.add_argument("--donante", type=int, default=44, help="escenario nativo que presta script y efectos")
    s.add_argument("--tema", type=int, default=16, help="registro cuyo tema (luz/cielo) se usa")
    s.add_argument("--bin", help="bin sin comprimir: si existe se usa (no reconvierte), si no se guarda")
    s.add_argument("--foto", help="imagen del escenario (captura) para el circulo y la vista del selector")
    s.add_argument("--rotulo", help="texto del rotulo del selector (por defecto --nombre)")
    s.add_argument("--zona", choices=("a", "b"), default="a",
                   help="variante del escenario de B2: a (por defecto, la inicial) o b (la otra zona: el objeto "
                        "de la variante 1, sus raices y las camaras de la transicion; solo grupos con 2 variantes)")
    s.add_argument("--caja-escala", type=float, default=1.0,
                   help="estira la CAJA DE COLISION del escenario en x/z (no toca y): 1.0 = original. Sirve para "
                        "un escenario cuyo suelo es mas ancho que su colision (Base Red Ribbon de B2: poco sitio).")
    s.add_argument("--tabla-efectos", action="store_true",
                   help="imprime las llamadas de efecto del #SPX de B2 y el banco #AME que el bin del donante no usa")
    s.set_defaults(fn=cmd_mod)
    s = sp.add_parser("comprobar")
    s.add_argument("escenarios", nargs="*", type=int)
    s.set_defaults(fn=cmd_comprobar)
    a = p.parse_args()
    return a.fn(a) or 0


if __name__ == "__main__":
    sys.exit(main())
