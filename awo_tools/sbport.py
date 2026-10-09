#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""sbport.py - Moveset de Shin Budokai 1/2 (PSP) -> moveset de Budokai 3 (PS2 y HD).

RE 2026-10-06 (exploracion docs/07_ports/GOHAN_FUTURO_EXPLORACION_2026-10-06/01_moveset y
oraculo Gohan adulto: BCGHL de SB2 contra el ID 4 de B3, `--oraculo`):

  SB por personaje (data_btl_cmn.afs, nombres AFS de 48 B): BC<XXX>.amb = #AMB [AMC (vacio),
    SPX, BCM, BSK, (BSK 2 en los M<n>), AMM propio, AMM 2 (victima), AMM 3]; los AMM van
    comprimidos (sb_amm.py). BC<XXX>M<n>.amb = lo mismo + un 2.o #BSK que redefine codigos
    sueltos de una forma/modelo (GHF M1: solo 0x19F, ver INFORME).
  Mismo motor que B3 (BSK v4, BCM, SPX 0.01), con estas diferencias, todas medidas:
  - BSK: un sub-bloque de 48 B por codigo en los dos juegos (en los 45 000 codigos de SB y B3,
    sin direcciones ni tablas AP compartidas). Lineas AP de 20 B (B3 16): los 4 de cola son 0
    salvo en el tipo 1 (golpe), cuya caja cambia: radio +11 -> +12 y posicion 3 x s16 en
    +12/14/16 -> 3 x s8 en +13/14/15 (mismas unidades: 9020 golpes casados). Bloques HR de
    8 x 20 B (B3 8 x 16): se truncan; dano B3 = 0,85 x SB (mediana de 8847 golpes; si el
    personaje esta en B3, la suya: DMG_SB); el tipo 5 (exclusivo de SB, aura) y el 3 (guion de
    una ranura SPX de SB) pasan a 2 (despide).
    Sub-bloque +0x10: SB anade los bits 0x80/0x100/0x200 (B3 nunca pasa de 0x7F).
    +0x1C..+0x22 tienen la MISMA distribucion de valores en los dos juegos: no son codigos.
  - AP tipo 7 [frame][id][clase u32][valor u32]: clase 0 (accion del motor) igual al 97 %
    (se quitan las acciones que ningun B3 usa: aura, guiones); 1 sonido / 2 chispa / 3 hueco de
    grito del banco lang: renumerados (tabla por mayoria; los gritos del banco propio, < 0x40,
    con sb_voces.K3, por tono; sin pareja se quitan); 4 enlace al
    BSP: se copia (salvo renumeraciones sistematicas); 5-9 iguales; 10 = llamada a una ranura
    SPX de SB y 11+ exclusivas: se quitan. En los tipos 3-7 el byte +2 de cada linea es SIEMPRE
    una permutacion de 0..n-1 (77 000 tablas de SB y B3): al quitar lineas se renumera.
  - Codigos: SB 0x000-0x1FF suelo / +0x200 aire (B3 +0x100) para el motor; ataques del BCM
    0x400-0x5FF suelo / 0x600-0x7FF aire (B3 0x200-0x27F / 0x300-0x37F: el censo de los 38
    BCM de B3 nunca sale de ahi). La numeracion de ataques es propia de cada personaje (Gohan
    adulto: SB 0x400 = B3 0x203/0x21B), asi que se conserva el desplazamiento (0x4xx -> 0x2xx)
    y lo que choca con el motor o cae fuera se reubica en un hueco libre (pareja +0x100).
    Codigos del motor SB -> B3 por animacion casada (ENGINE_SB). Transformacion 0x500/0x700,
    agarre BASE 0x800 y guiones 0x52x: se quitan (el agarre, el modo hiper, el Dragon Rush, la
    familia 0x2E0/0x3E0, el definitivo cinematico 0x25A y todo lo >= 0x480, que solo piden sus
    guiones SPX, se injertan del donante B3). w14/w15 del BCM que el BSK de SB no define (el
    0x24E del Krilin de SB) se igualan a w13 / se ponen a 0.
  - Almacen 0 (animaciones globales): mismo contenido con otro indice (sb_tablas.GLOBAL, 206 de
    265; los 42 personajes solo piden de las casadas). AMM propio de SB: se conserva entero con sus
    indices (las de SB conservan su numero); detras van las del donante, con la cadera escalada
    a las piernas del personaje (altura.correccion) y sin posiciones salvo la cadera. El motor
    casa las pistas por nombre de hueso: los huesos de los modelos de otras formas con otro
    nombre para el mismo papel (B01 de GHF: GHL_L00_LHAND, XGHF_L00_SS_FACE; B02: XGF_NLA)
    reciben un alias con las mismas pistas.
  - BCM (PS2: w4 condicion, w6 estado): mismo bloque de 64 B.
      w0 0x10/0x20 (arriba/abajo, B3 no los tiene): definitivos (^E, cond 8/0x10) -> P+K+G+E en
        modo hiper (cond 0x000A, w6 0x8001, como los 32 definitivos de B3); abajo+E cond 0x20
        (transformarse, 4000 de ki) se quita; normales: arriba -> P+K, abajo -> K+G (en Gohan
        adulto el abajo+K de SB es la animacion del K+G de B3).
      Variantes w6 0x8000 / w3 0x7530 (estado aura de SB) y entradas solo-aura (w6 0x100,
        cond 0x40/0x80): se quitan del BCM (los golpes de las variantes de especiales se
        convierten igual, "variantes" en el json, por si se quieren como remate de combo). cond 4 (especial cuerpo a cuerpo en SB) -> 2 (especial; en
        B3 4 = transformarse). Especiales/definitivos: ki 0 (el coste va en la capsula, igual
        que en B3); rafagas de ki: se copia (misma escala). w6 0x100 se quita; w3/w5 de las
        entradas de arranque a 0. Cabecera de B3 (la del donante).
      Del donante: agarre (P+G) y modo hiper (P+K+G+E, cond 0x400) con su subarbol.
  - SPX: las funciones nativas estan renumeradas -> SPX y AMC (camaras) del donante; los
    codigos que sus guiones empujan quedan reservados (no se reescribe nada).

Uso:
  python sbport.py --lista [--juego sb2]
  python sbport.py --personaje GHF [--juego sb2] [--donante 4] --salida DIR
                   [--modelos m1.bin ...] [--forma-m 1] [--definitivo-donante 14] [--dano 0.85]
  python sbport.py --oraculo [--juego sb2]   (convierte GHL y lo compara con Gohan adulto de B3)
  python sbport.py --prueba                  (autocomprobacion sin ISO)
Salida en DIR: anm_forma1.bin y camara.bin (HD, como los mods), *_ps2.bin (PS2) y sbport.json
(mapa de codigos, entradas del BCM, enlaces c4 al BSP, gritos por codigo, lineas quitadas,
errores de la comprobacion). --lista escribe lineas TAB:
  personaje <juego> <codigo> <nombre> <indice AFS> <donante B3 sugerido> <n modelos B0n> <M>
Codigo de salida: 0 si la comprobacion (verify) no encuentra errores.
"""
import argparse
import collections
import json
import os
import statistics
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "mod center hd")]

import b1port as bp  # noqa: E402
import sb_amm  # noqa: E402
from sb_tablas import B3_C0, GLOBAL, T7  # noqa: E402
from sb_voces import K3  # noqa: E402  (agente F: tono de SB -> hueco del banco de gritos de B3)

import iso as _iso  # noqa: E402
ISOS = {g: _iso.find_game(g) or os.path.join(ROOT, "ps2_games", fn) for g, fn in (   # cualquier region
    ("sb1", "Dragonball Z Shin Budokai (USA).iso"), ("sb2", "Dragonball Z Shin Budokai Another Road (USA).iso"))}
DMG_SCALE = 0.85
# dano B3 / SB del MISMO personaje (mediana de golpes casados por animacion); si el personaje
# de SB no esta en B3 (Gohan del Futuro, Gogeta...) se usa la mediana global DMG_SCALE
DMG_SB = {"GOK": 0.857, "GHM": 0.763, "GHL": 0.727, "VGT": 0.786, "TRX": 0.812, "KLL": 0.722,
          "PIC": 0.894, "FRZ": 0.846, "18G": 0.71, "CEL": 0.875, "BUL": 1.0, "BUM": 0.863,
          "BUS": 0.893, "DBR": 0.831, "COO": 0.762, "BDK": 0.735, "BRL": 0.889}
w16, set16 = bp.w16, bp.set16

# codigos del motor SB -> B3 (misma animacion; aire SB +0x200 = B3 +0x100). Los grupos
# 0x18C-0x19B comparten dos animaciones en el mismo patron A B B A que B3 0xF8-0xFF.
ENGINE_SB = {0x0: 0x0, 0x1: 0x2, 0x3: 0x4, 0x6: 0x7, 0x7: 0x8, 0x8: 0x9, 0x9: 0xA, 0xA: 0xB,
             0x14: 0xF, 0x15: 0x10, 0x16: 0x11, 0x40: 0x3C, 0x41: 0x38, 0x42: 0x39, 0x43: 0x3A,
             0x44: 0x3B, 0x201: 0x102, 0x214: 0x10F, 0x240: 0x13C, 0x241: 0x138, 0x242: 0x139,
             0x243: 0x13A, 0x244: 0x13B, 0x194: 0xF4, 0x195: 0xF5, 0x196: 0xF6, 0x197: 0xF7,
             0x198: 0xF8, 0x199: 0xF9, 0x19A: 0xFA, 0x19B: 0xFB, 0x18C: 0xFC, 0x18D: 0xFD,
             0x190: 0xFE, 0x191: 0xFF}
ENGINE_SB.update({0x3E0 + i: 0x3C8 + i for i in range(8)})
TRANSFORM = bp.code_ranges("2e0-2f7 3e0-3f7")
GROUND, AIR = range(0x200, 0x280), range(0x300, 0x380)

# personajes de SB: nombre y donante B3 sugerido (el mismo personaje si esta en B3)
NAMES = {"GOK": ("Goku", 0), "VGT": ("Vegeta", 7), "PIC": ("Piccolo", 11), "KLL": ("Krillin", 10),
         "GHM": ("Teen Gohan", 3), "GHL": ("Adult Gohan", 4), "TRX": ("Future Trunks (sword)", 8),
         "TRF": ("Future Trunks (melee)", 8), "FRZ": ("Frieza", 27), "CEL": ("Cell", 33),
         "COO": ("Cooler", 38), "BRL": ("Broly", 40), "18G": ("Android 18", 30), "BUS": ("Kid Buu", 36),
         "BUL": ("Majin Buu", 34), "BUM": ("Super Buu", 35), "BDK": ("Bardock", 39),
         "DBR": ("Dabura", 37), "GHF": ("Future Gohan", 4), "GGT": ("Gogeta", 7),
         "VTO": ("Vegito", 7), "GTX": ("Gotenks", 6), "JNB": ("Janemba", 35), "PKH": ("Pikkon", 11)}


# ---------------------------------------------------------------- fuentes
class SbIso:
    """data_btl_cmn.afs de una ISO de PSP leido en su sitio (AFS con tabla de nombres)."""

    def __init__(self, path):
        import iso  # noqa: PLC0415
        self.iso = iso.Iso(path)
        self.f = self.iso.open(self.iso.find("data_btl_cmn.afs"))
        self.f.seek(4)
        n = struct.unpack("<I", self.f.read(4))[0]
        self.tab = [struct.unpack("<II", self.f.read(8)) for _ in range(n)]
        noff, nsz = struct.unpack("<II", self.f.read(8))
        self.names = [""] * n
        if noff and nsz == 48 * n:
            self.f.seek(noff)
            blob = self.f.read(nsz)
            self.names = [blob[48 * i:48 * i + 32].split(b"\0")[0].decode("ascii", "replace").upper()
                          for i in range(n)]

    def entry(self, i):
        o, s = self.tab[i]
        self.f.seek(o)
        return self.f.read(s)

    def find(self, name):
        return self.names.index(name.upper()) if name.upper() in self.names else None

    def characters(self):
        """[(codigo, indice)] de los BC<XXX>.amb."""
        return [(nm[2:-4], i) for i, nm in enumerate(self.names)
                if nm.startswith("BC") and nm.endswith(".AMB") and len(nm) == 9 and nm != "BCCMN.AMB"]


def sb_kids(d):
    return [k for k, t in bp.amb_kids(d)]


def roster_db():
    return json.load(open(os.path.join(ROOT, "mod center hd", "roster_db.json"), encoding="utf-8"))["ids"]


def donor(cid):
    """(anm, cam, modelo) PS2 GH del personaje B3 `cid`."""
    import afs_pair  # noqa: PLC0415
    e = roster_db()[cid]
    return (afs_pair.ps2(e["anm"][0]), afs_pair.ps2(e["cam"]),
            afs_pair.ps2(e["models"][0]) if e.get("models") else None)


# ---------------------------------------------------------------- lineas SB -> B3
def hit_line(x):
    """Linea AP tipo 1 de SB (20 B) -> B3 (16 B): radio +11 -> +12, posicion s16 -> s8."""
    out = bytearray(x[:16])
    xyz = [max(-128, min(127, v)) for v in struct.unpack_from("<3h", x, 12)]
    out[11], out[12] = 0, x[11]
    struct.pack_into("<3b", out, 13, *xyz)
    return bytes(out)


def fx_line(x, st):
    """Linea AP tipo 7 de SB -> B3 (None = se quita). st: contador de lo que pasa."""
    k, v = struct.unpack_from("<II", x, 4)
    out = bytearray(x[:16])
    if k == 0:
        keep, nv = v in B3_C0, v
    elif k in (1, 2, 3):
        # gritos del banco del personaje (< 0x40): el numero es el TONO de SB y sb_voces.K3 da el
        # hueco de B3 con ese tono; los >= 0x40 (banco comun) y los sonidos/chispas, por tabla
        nv = K3.get(v) if k == 3 and v < 0x40 else T7[k].get(v)
        keep = nv is not None
    elif k == 4:
        nv = T7[4].get(v, v)
        keep = True
    else:
        keep, nv = k <= 9, v
    st["c%d %s" % (k, "ok" if keep and nv == v else "cambiada" if keep else "quitada")] += 1
    if not keep:
        return None
    struct.pack_into("<I", out, 8, nv)
    return bytes(out)


def hr_block(x, scale):
    """Bloque HR de SB (8 x 20 B) -> B3 (8 x 16 B), dano escalado; tipos 3/5 -> 2 (despide)."""
    out = bytearray()
    for i in range(8):
        ln = bytearray(x[20 * i:20 * i + 16])
        dmg, grunt, vis, ty, code, push = struct.unpack_from("<HBBHHf", ln)
        if dmg:
            dmg = max(1, int(round(dmg * scale)))
        if ty in (3, 5):
            struct.pack_into("<HBBHHff", ln, 0, dmg or 50, grunt, vis, 2, 0, push or 40.0, 0.0)
        else:
            struct.pack_into("<H", ln, 0, dmg)
        out += ln
    return bytes(out)


renumber = bp.renumber


class Src:
    """Un #BSK de origen: de SB (lineas de 20 B, HR de 160 B) o de B3 (16 / 128)."""

    def __init__(self, b, sb):
        self.b, self.sb, self.LS = b, sb, 20 if sb else 16
        self.log = None                     # [(codigo, frame, clase, valor SB, valor B3 | None)]
        self.n, self.lst, self.nhr, self.hr = bp.bsk_head(b)
        self.L = bp.bsk_code_list(b)

    def has(self, code):
        return code < self.n and self.L[code] != 0

    def anim(self, code):
        return struct.unpack_from("<HH", self.b, self.L[code])

    def hr_bytes(self, h, scale):
        if self.sb:
            return hr_block(self.b[self.hr + 160 * h:self.hr + 160 * h + 160], scale)
        return self.b[self.hr + 128 * h:self.hr + 128 * h + 128]

    def block(self, code, anim_fn, hr_fn, st):
        """(sub-bloque 48 B, [(tipo AP, [lineas 16 B])]) del codigo, ya en formato B3."""
        a = self.L[code]
        blk = bytearray(self.b[a:a + 48])
        struct.pack_into("<HH", blk, 0, *anim_fn(self, *struct.unpack_from("<HH", blk, 0)))
        if self.sb:
            set16(blk, 8, w16(blk, 8) & 0x7F)
            # +0x0C y +0x14: 0 en los 141 936 sub-bloques nativos. SB lleva ahi un f32 (0.3) que
            # B3 tiene en +0x08; B3 lee +0x0C como puntero (cuelgue al cargar la rafaga, prueba 8)
            if not struct.unpack_from("<I", blk, 8)[0]:
                blk[8:12] = blk[12:16]
            blk[12:16] = blk[20:24] = bytes(4)
        _, n_ap, ap_off = struct.unpack_from("<3I", blk, 0x24)
        aps = []
        for k in range(n_ap):
            t, nl, do = struct.unpack_from("<HHI", self.b, ap_off + 8 * k)
            lines = []
            for i in range(nl):
                x = self.b[do + self.LS * i:do + self.LS * (i + 1)]
                if self.sb:
                    orig = x
                    x = hit_line(x) if t == 1 else fx_line(x, st) if t == 7 else x[:16]
                    if t == 7 and self.log is not None:
                        k_, v_ = struct.unpack_from("<II", orig, 4)
                        self.log.append((code, struct.unpack_from("<H", orig)[0], k_, v_,
                                         None if x is None else struct.unpack_from("<I", x, 8)[0]))
                    if x is None:
                        continue
                if t == 1:
                    h = struct.unpack_from("<H", x, 4)[0]
                    if h != 0xFFFF:
                        x = bytearray(x)
                        struct.pack_into("<H", x, 4, hr_fn(self, h))
                lines.append(bytes(x))
            if t >= 3 and len(lines) != nl:
                lines = renumber(lines)
            if lines or not self.sb:
                aps.append((t, lines))
        return blk, aps


def bsk_build(head, n, items, hrs):
    """#BSK B3 (PS2): items {codigo: (sub-bloque, aps)}, hrs [bloques de 128 B]. Como los
    nativos: cabecera, lista, un sub-bloque por codigo (sin compartir nada), AP, HR al final."""
    out = bytearray(head[:0x10]) + bytes(0x10) + bytes(4 * n)
    out += bytes((-len(out)) % 16)
    at = {}
    for c in sorted(items):
        at[c] = len(out)
        out += items[c][0]
    out += b"\xff" * 48                  # cierre: ningun lector une el ultimo con las AP
    for c in sorted(items):
        struct.pack_into("<I", out, 0x20 + 4 * c, at[c])
        aps = items[c][1]
        tab = []
        for t, lines in aps:
            out += bytes((-len(out)) % 16)
            tab.append((t, len(lines), len(out)))
            out += b"".join(lines)
        ap_off = 0
        if tab:
            out += bytes((-len(out)) % 16)
            ap_off = len(out)
            for e in tab:
                out += struct.pack("<HHI", *e)
        struct.pack_into("<3I", out, at[c] + 0x24, 0, len(tab), ap_off)
    out += bytes((-len(out)) % 16)
    hr = len(out)
    out += b"".join(hrs)
    struct.pack_into("<4I", out, 0x10, n, 0x20, len(hrs), hr)
    return bytes(out)


# ---------------------------------------------------------------- BCM
INPUT = {0: "", 1: "->", 2: "<-", 0x10: "^", 0x20: "v"}
BUTTONS = {1: "P", 2: "K", 3: "P+K", 5: "P+G", 6: "K+G", 7: "P+K+G", 8: "E", 0xF: "P+K+G+E"}


def entry_name(blk):
    return "%s%s%s" % ("aire " if w16(blk, 6) & 0x40 else "", INPUT.get(w16(blk, 0), "?%x" % w16(blk, 0)),
                       BUTTONS.get(w16(blk, 1), "?%x" % w16(blk, 1)))


def sb_bcm_nodes(c, report):
    """BCM de SB -> (arranques, nodos, variantes) con las reglas de B3 y los codigos de SB todavia
    sin cambiar. nodos {clave: [bloque, [hijas], tipo]}; tipo: normal, especial, rafaga,
    definitivo, variante. Las variantes (especiales con w6 0x8000, estado aura de SB) no van al
    BCM, pero sus golpes se convierten por si se quieren como remate de combo (cond 0x2002)."""
    st, bl = bp.bcm_parse(c)
    nodes, drops = {}, collections.Counter()

    memo = {}

    def conv(o, starter):
        if (o, starter) not in memo:
            memo[(o, starter)] = None          # ciclo: se corta
            memo[(o, starter)] = conv1(o, starter)
        return memo[(o, starter)]

    def conv1(o, starter):
        blk = bytearray(bl[o][0])
        w = [w16(blk, i) for i in range(16)]
        dirn, btn, cond, state, cap = w[0], w[1], w[4], w[6], w[8]
        why, variant = None, False
        if starter and state & 0x8000:
            if btn == 8 and cap and not cond & 0xF8:
                variant, state = True, state & 0xFF
            else:
                why = "variante de SB (w6 0x8000, estado aura)"
        elif state & 0x100 and not state & 0xFF or cond & 0xC0:
            why = "solo en aura de SB (w6 0x100 / cond 0x40-0x80)"
        elif cond & 0x20:
            why = "transformacion de SB (abajo+E, 4000 de ki)"
        elif btn == 8 and not dirn and not cap and not cond & 0x8:
            # las rafagas de SB (suelo, aire y cadenas) colgaban el juego al mantener E (pruebas
            # 8-9): van las del donante enteras (donor_entries), como el agarre
            why = "rafaga de ki de SB (va la del donante)"
        elif starter and btn == 5 and not cond and not cap:
            why = "agarre de SB (va el del donante)"
        elif any(w[i] >= 0x800 for i in (12, 13, 14, 15)):
            why = "codigo especial de SB (>= 0x800)"
        if why:
            drops[why] += 1
            return None
        kind = "normal"
        if btn & 8 and (dirn & 0x30 or cond & 0x18):
            if not (starter and cond & 0x18 and dirn & 0x30):
                drops["definitivo o arriba/abajo+E fuera de arranque"] += 1
                return None
            kind = "definitivo"
            dirn, btn, cond, state = 0, 0xF, 0x000A, 0x8001
        elif dirn & 0x30:
            if btn & 8 or btn not in (1, 2):
                drops["arriba/abajo con E u otros botones"] += 1
                return None
            dirn, btn = 0, 3 if dirn & 0x10 else 6
        if kind == "normal" and btn == 8:
            kind = "especial" if cond & 0x6 else "rafaga" if cond & 1 else "normal"
        if cond & 4:
            # especial cuerpo a cuerpo de SB -> especial con capsula de B3 (0x2) aunque su booster
            # (mascara de formas) sea 0: sin el 0x2 el Shining Slash de Trunks salia sin rotulo
            cond = (cond & ~4) | 2
        if kind != "definitivo":
            cond &= 0x3
            state &= 0xFF
        set16(blk, 0, dirn)
        set16(blk, 1, btn)
        set16(blk, 4, cond)
        set16(blk, 5, 0)
        set16(blk, 6, state)
        if starter:
            set16(blk, 3, 0)
        if kind in ("especial", "definitivo"):
            set16(blk, 9, 0)
        for i in (10, 11):
            set16(blk, i, 0)
        kids = [k for k in (conv(x, False) for x in bl[o][1]) if k]
        key = ("sb", o, starter)
        nodes[key] = [blk, kids, "variante" if variant else kind]
        return key

    starters, seen, variants = [], set(), []
    for o in st:
        k = conv(o, True)
        if not k:
            continue
        if nodes[k][2] == "variante":
            variants.append(k)
            continue
        b = nodes[k][0]
        sig = (w16(b, 0), w16(b, 1), w16(b, 4), w16(b, 6), w16(b, 8))
        if sig in seen:
            drops["entrada repetida tras convertir"] += 1
            continue
        seen.add(sig)
        starters.append(k)
    report.append("BCM de SB: %d arranques -> %d; quitadas: %s; variantes aura (solo golpes): %d" % (
        len(st), len(starters), dict(drops), len(variants)))
    return starters, nodes, variants


def bcm_codes(nodes, keys):
    """Codigos (w12-w15) de los nodos alcanzables desde `keys`."""
    out, todo, seen = [], list(keys), set()
    while todo:
        k = todo.pop(0)
        if k in seen:
            continue
        seen.add(k)
        b = nodes[k][0]
        out.append(tuple(w16(b, i) for i in (12, 13, 14, 15)))
        todo += nodes[k][1]
    return out


def alloc_codes(tuples, reserved):
    """{codigo SB: codigo B3} para los ataques del BCM: 0x4xx -> 0x2xx y 0x6xx -> 0x3xx si
    caben y estan libres; si no, a un hueco libre (suelo 0x200-0x27F, aire 0x300-0x37F),
    buscando que suelo y aire queden a +0x100 como en B3."""
    codes = sorted({c for t in tuples for c in t if c})
    taken, out = set(reserved), {}

    def air(c):
        return c >= 0x600 or 0x300 <= c < 0x400

    for c in codes:
        t = c - (0x300 if air(c) else 0x200)
        if t in (AIR if air(c) else GROUND) and t not in taken:
            out[c] = t
            taken.add(t)
    for tup in tuples:
        for c in tup:
            if not c or c in out:
                continue
            pool = AIR if air(c) else GROUND
            mates = [out[x] + (0x100 if not air(x) else -0x100) for x in tup if x in out and air(x) != air(c)]
            pref = [m for m in mates if m in pool and m not in taken]
            free = [x for x in pool if x not in taken
                    and (x + (-0x100 if air(c) else 0x100)) not in taken]
            t = (pref or free or [x for x in pool if x not in taken] or [None])[0]
            if t is None:
                raise ValueError("no quedan codigos libres para %#x" % c)
            out[c] = t
            taken.add(t)
    return out


def donor_entries(d_bcm, report):
    """Arranques del donante que se injertan: agarre (P+G), modo hiper (cond 0x400) y rafagas
    de ki (E sin direccion ni capsula, suelo y aire, con sus cadenas)."""
    st, bl = bp.bcm_parse(d_bcm)
    nodes, starters = {}, []

    def copy(o):
        key = ("d", o)
        if key not in nodes:
            nodes[key] = [bytearray(bl[o][0]), [], "donante"]
            nodes[key][1] = [copy(k) for k in bl[o][1]]
        return key

    for o in st:
        b = bl[o][0]
        ki = w16(b, 1) == 8 and not w16(b, 0) and not w16(b, 8) and not w16(b, 4) & 0x408
        if w16(b, 4) & 0x400 or (w16(b, 1) == 5 and not w16(b, 4) and not w16(b, 8)) or ki:
            starters.append(copy(o))
            report.append("BCM: del donante %s (cond %#x, codigo %#x)" % (
                "modo hiper" if w16(b, 4) & 0x400 else "rafaga de ki" if ki else "agarre", w16(b, 4), w16(b, 12)))
    return starters, nodes


# ---------------------------------------------------------------- AMM
def canon(suffix):
    """Papel de un hueso para los alias: las caras de cada forma (L00_NOR_FACE, L00_SS_FACE...)
    son el mismo hueso."""
    return suffix.split("_")[0] + "_FACE" if suffix.endswith("_FACE") else suffix


def model_bind(m):
    """{sufijo: (nombre, quat, pos)} y orden de nombres del esqueleto de un modelo (PS2, PSP o HD)."""
    import altura  # noqa: PLC0415
    sk = altura.skeleton(m)
    out, names = {}, []
    for nm, q, p, par in sk:
        suf = bp.bone_key(nm)[1]
        if suf not in out:
            out[suf] = (nm, q, p)
            names.append(nm)
    return out, names, sk


# ---------------------------------------------------------------- port
def port(sb, code, d_anm, d_cam, d_model, report, models=(), forma_m=None, scale=DMG_SCALE,
         ult_donor=None):
    """-> (anm PS2, cam PS2, info). sb: SbIso; code: 'GHF'; d_*: donante PS2 GH."""
    import altura  # noqa: PLC0415
    i = sb.find("BC%s.AMB" % code)
    if i is None:
        raise ValueError("no existe BC%s.amb en la ISO" % code)
    kids = {}
    for k in sb_kids(sb.entry(i)):
        if k[:4] in (b"#SPX", b"#BCM", b"#BSK"):
            kids.setdefault(k[:4], k)
        elif k[:4] == b"#AMM":
            kids.setdefault("amm%d" % sum(1 for x in kids if str(x).startswith("amm")), k)
    over = None
    if forma_m is not None:
        j = sb.find("BC%sM%d.AMB" % (code, forma_m))
        if j is None:
            raise ValueError("no existe BC%sM%d.amb" % (code, forma_m))
        bsks = [k for k in sb_kids(sb.entry(j)) if k[:4] == b"#BSK"]
        over = Src(bsks[1], True) if len(bsks) > 1 else None
        report.append("forma M%d: el 2.o #BSK redefine %s" % (forma_m, [hex(c) for c in range(over.n) if over.has(c)] if over else []))
    S = Src(kids[b"#BSK"], True)
    S.log = []
    if over is not None:
        over.log = S.log
    raw = kids["amm0"]
    own = bp.Amm(sb_amm.convert(raw) if sb_amm.is_sb(raw) else raw)
    dk = bp.amb_kids(d_anm)
    D = Src(dk[0][0], False)
    d_amm = bp.Amm(dk[1][0])
    ck = bp.amb_kids(d_cam)
    d_bcm = next(x for x, t in ck if x[:4] == b"#BCM")
    d_spx = next(x for x, t in ck if x[:4] == b"#SPX")

    def src_of(c):
        return over if over is not None and over.has(c) else S

    # 1) BCM de SB con las reglas de B3; codigos que pide (w14/w15 que el BSK no define, como el
    # 0x24E del Krilin de SB, se igualan a w13 / se ponen a 0; sin golpe en w12/w13 se quita)
    sb_st, nodes, sb_var = sb_bcm_nodes(kids[b"#BCM"], report)
    bad = set()
    for k, (b, _, _) in nodes.items():
        if w16(b, 14) and not src_of(w16(b, 14)).has(w16(b, 14)):
            set16(b, 14, w16(b, 13))
        if w16(b, 15) and not src_of(w16(b, 15)).has(w16(b, 15)):
            set16(b, 15, 0)
        if any(w16(b, j) and not src_of(w16(b, j)).has(w16(b, j)) for j in (12, 13)):
            bad.add(k)
    if bad:
        report.append("BCM de SB: %d entradas piden golpes que su BSK no define: quitadas" % len(bad))
        sb_st = [k for k in sb_st if k not in bad]
        sb_var = [k for k in sb_var if k not in bad]
        for v in nodes.values():
            v[1] = [k for k in v[1] if k not in bad]
    d_st, d_nodes = donor_entries(d_bcm, report)
    nodes.update(d_nodes)
    # 2) lo que se injerta del donante y lo que queda reservado
    d_used = {c for c in range(D.n) if D.has(c)}
    hyper = bp.bcm_hyper_codes(d_bcm)
    scripted = bp.bsk_st3_codes(D.b)                       # agarre (20) y definitivo (0)
    spx_refs = set()
    ns = struct.unpack_from("<I", d_spx, 0x18)[0]
    for sl in range(ns):
        for v in bp.spx_code_refs(d_spx, sl):
            spx_refs.add(v)
    engine = {}                                            # B3 -> SB
    for c in sorted(ENGINE_SB):
        if S.has(c) or (over and over.has(c)):
            engine.setdefault(ENGINE_SB[c], c)
    d_codes = {x for t in bcm_codes(nodes, d_st) for x in t if x}
    # >= 0x480: animaciones que solo piden los guiones SPX del donante (agarre en su BASE, que
    # varia: 0x480 Gohan, 0x4D0 Buu gordo; cinematica del definitivo 0x4A0+)
    graft = sorted(((d_used & (bp.ENGINE | hyper | scripted | spx_refs | TRANSFORM | d_codes))
                    - set(engine) - bp.BASIC) | (d_used & (d_codes | hyper)) | {c for c in d_used if c >= 0x480})
    reserved = bp.ENGINE | TRANSFORM | spx_refs | set(graft)
    tuples = bcm_codes(nodes, sb_st) + bcm_codes(nodes, sb_var)
    cmap = alloc_codes(tuples, reserved)
    for k in [k for k in nodes if k[0] == "sb"]:
        b = nodes[k][0]
        for j in (12, 13, 14, 15):
            if w16(b, j):
                set16(b, j, cmap[w16(b, j)])
    moved = {hex(c): hex(t) for c, t in cmap.items() if t != c - (0x300 if c >= 0x600 else 0x200)}
    report.append("ataques de SB: %d codigos (0x4xx -> 0x2xx, 0x6xx -> 0x3xx); reubicados %s" % (
        len(cmap), moved))
    # 3) bloques
    anims = list(own.anims)
    dmap = {}
    st = collections.Counter()
    hrs, hr_at = [], {}

    def hr_fn(src, h):
        b = src.hr_bytes(h, scale) if h < src.nhr else bytes(128)
        if b not in hr_at:
            hr_at[b] = len(hrs)
            hrs.append(b)
        return hr_at[b]

    def sb_anim(src, anim, pool):
        if pool != 0:
            return anim, pool
        if anim not in GLOBAL:
            # ponytail: ninguno de los 42 personajes de SB1/SB2 lo necesita; si aparece, copiar la
            # animacion de BCCMN.AMB (hijo 2) al AMM propio sin posiciones de hueso
            raise ValueError("animacion global %d de SB sin pareja en B3" % anim)
        return GLOBAL[anim], 0

    def d_anim(src, anim, pool):
        if pool != 3:
            return anim, pool
        if anim not in dmap:
            dmap[anim] = len(anims)
            anims.append(d_amm.anims[anim] if anim < len(d_amm.anims) else (9, 0, 1, None))
        return dmap[anim], 3

    items, origin = {}, {}
    for b3c, c in sorted(engine.items()):
        items[b3c] = src_of(c).block(c, sb_anim, hr_fn, st)
        origin[b3c] = "SB %#x" % c
    for c, b3c in sorted(cmap.items()):
        items[b3c] = src_of(c).block(c, sb_anim, hr_fn, st)
        origin[b3c] = "SB %#x" % c
    n_sb = len(anims)
    for c in graft:
        items[c] = D.block(c, d_anim, hr_fn, st)
        origin[c] = "donante"
    report.append("motor: %d codigos de SB (%s); injertados del donante %d codigos (%d animaciones)" % (
        len(engine), " ".join("%x>%x" % (c, b) for b, c in sorted(engine.items())), len(graft), len(dmap)))
    report.append("lineas AP7: %s" % dict(sorted(st.items())))
    n = max(D.n, max(items) + 1)
    bsk = bsk_build(D.b, n, items, hrs)
    report.append("BSK: %d codigos, %d bloques HR (dano x%.2f)" % (len(items), len(hrs), scale))
    # 4) AMM: esqueleto de destino, cadera de las del donante
    sbm = sb.entry(sb.find("BC%sB00.AMB" % code))
    bind_sb, _, sk_sb = model_bind(sbm)
    if models:
        tb = [model_bind(m) for m in models]
        bind_t, sk_t = tb[0][0], tb[0][2]
        names, seen = [], set()
        for _, nms, _ in tb:
            for nm in nms:
                if bp.bone_key(nm)[1] not in seen:
                    seen.add(bp.bone_key(nm)[1])
                    names.append(nm)
    else:
        bind_t, sk_t, names = bind_sb, sk_sb, list(own.names)
    # alias: huesos de los modelos con otro prefijo o cara de otra forma (B01 de GHF: GHL_L00_LHAND,
    # XGHF_L00_SS_FACE; B02: XGF_NLA) reciben las pistas del hueso con el mismo papel: el motor
    # casa las pistas por nombre
    alias = {}
    have = {canon(bp.bone_key(nm)[1]): bp.bone_key(nm)[1] for nm in names}
    forms = [sb.entry(j) for j in (sb.find("BC%sB%02d.AMB" % (code, f)) for f in range(10)) if j is not None]
    for m in list(models) + forms:
        for nm in model_bind(m)[1]:
            suf = bp.bone_key(nm)[1]
            if nm not in names and canon(suf) in have:
                names.append(nm)
                alias[nm] = have[canon(suf)]
    if alias:
        report.append("huesos con alias (otra forma): %s" % alias)
    idle = anims[S.anim(0)[0]] if S.has(0) and S.anim(0)[1] == 3 else None
    hips = [altura.correccion(sk_t, sk_sb, bp.amm_tracks(idle) if idle else None), (1.0, 0.0)]
    if d_model:
        hips[1] = altura.correccion(sk_t, altura.skeleton(d_model), altura.idle_tracks(d_anm))
    report.append("altura: cadera de SB x%.3f %+.2f, del donante x%.3f %+.2f" % (*hips[0], *hips[1]))

    def rekey(rt):
        return [(f, v, nf, None if b is None else
                 {bp.bone_key(nm): b[alias.get(nm, bp.bone_key(nm)[1])] for nm in names
                  if alias.get(nm, bp.bone_key(nm)[1]) in b})
                for f, v, nf, b in rt]

    rt = bp.retarget(anims, [(bind_sb, bind_t)], report, n_sb, hips)
    amm = own.build(names, rekey(rt), reduce=True)
    a3 = bp.Amm(dk[3][0])
    amm3 = a3.build(names, rekey(bp.retarget(a3.anims, [(bind_t, bind_t)], [], 0, hips, ())))
    anm = bp.amb_build([(bsk, dk[0][1]), (amm, dk[1][1]), (dk[2][0], dk[2][1]), (amm3, dk[3][1])])
    report.append("AMM: %d de SB + %d del donante; huesos %d" % (n_sb, len(dmap), len(names)))
    # 5) BCM: normales de SB, agarre e hiper del donante, definitivos y especiales de SB
    norm = [k for k in sb_st if nodes[k][2] == "normal"]
    late = [k for k in sb_st if nodes[k][2] != "normal"]
    ults = [k for k in late if nodes[k][2] == "definitivo"]
    if ult_donor is not None:
        k = next((k for k in ults if w16(nodes[k][0], 8) == ult_donor), None)
        if k is None:
            raise ValueError("ningun definitivo de SB usa la capsula %d" % ult_donor)
        b = nodes[k][0]
        for j, v in zip((12, 13, 14), (0x25A, 0x35A, 0x35A)):
            set16(b, j, v)
        report.append("definitivo de la capsula %d -> cinematica del donante 0x25A" % ult_donor)
    order = norm + d_st + ults + [k for k in late if k not in ults]
    bcm = bp.bcm_build(d_bcm, order, nodes)
    cam = bp.amb_build([(bcm, t) if x[:4] == b"#BCM" else (x, t) for x, t in ck])
    info = dict(personaje=code, codigos={("%#x" % c): ("%#x" % t) for c, t in sorted(cmap.items())},
                motor={("%#x" % c): ("%#x" % b) for b, c in sorted(engine.items())},
                injertados=["%#x" % c for c in graft], origen={"%#x" % c: o for c, o in sorted(origin.items())},
                entradas=[dict(entrada=entry_name(nodes[k][0]), tipo=nodes[k][2], capsula=w16(nodes[k][0], 8),
                               condicion="%#x" % w16(nodes[k][0], 4), ki=w16(nodes[k][0], 9),
                               codigos=["%#x" % w16(nodes[k][0], j) for j in (12, 13)]) for k in order],
                c4={"%#x" % c: c4_values(items[c][1]) for c in sorted(items)
                    if origin[c] != "donante" and c4_values(items[c][1])},
                variantes=[dict(entrada=entry_name(nodes[k][0]), capsula=w16(nodes[k][0], 8),
                                codigos=["%#x" % w16(nodes[k][0], j) for j in (12, 13)]) for k in sb_var],
                voces={}, ap7_quitadas=[], lineas_ap7=dict(st), dano=scale)
    back = {c: t for c, t in cmap.items()}
    back.update({c: b for b, c in engine.items()})
    for c, fr, k_, v, nv in S.log:
        if c not in back:
            continue
        if k_ == 3:
            info["voces"].setdefault("%#x" % back[c], []).append([fr, "%#x" % v, None if nv is None else "%#x" % nv])
        elif nv is None:
            info["ap7_quitadas"].append(["%#x" % back[c], fr, k_, "%#x" % v])
    return anm, cam, info


def c4_values(aps):
    return sorted({"%#x" % struct.unpack_from("<I", x, 8)[0] for t, lines in aps if t == 7
                   for x in lines if struct.unpack_from("<I", x, 4)[0] == 4})


# ---------------------------------------------------------------- comprobacion
def verify(anm, cam, d_anm, d_cam, models=()):
    """Errores de un moveset convertido (PS2) frente a lo que el juego espera."""
    errs = []
    k = bp.amb_kids(anm)
    bsk, A = k[0][0], bp.Amm(k[1][0])
    n, lst, nhr, hr = bp.bsk_head(bsk)
    L = bp.bsk_code_list(bsk)
    defined = {c for c, a in enumerate(L) if a}
    if len({L[c] for c in defined}) != len(defined):
        errs.append("codigos que comparten bloque")
    ns_spx = None
    ck = bp.amb_kids(cam)
    spx = next(x for x, t in ck if x[:4] == b"#SPX")
    dck = bp.amb_kids(d_cam)
    d_spx = next(x for x, t in dck if x[:4] == b"#SPX")
    if spx != d_spx:
        errs.append("el SPX no es el del donante")
    ns_spx = struct.unpack_from("<I", spx, 0x18)[0]
    T = struct.unpack_from("<%dI" % ns_spx, spx, 0x20)
    tabs = collections.Counter()
    D = Src(bp.amb_kids(d_anm)[0][0], False)
    d_slots = {struct.unpack_from("<H", D.b, D.hr + 128 * h + 16 * li + 6)[0]       # el donante ya
               for h in range(D.nhr) for li in range(8)                             # trae ese guion
               if struct.unpack_from("<H", D.b, D.hr + 128 * h + 16 * li + 4)[0] == 3}
    for c in defined:
        s = L[c]
        an, pl = struct.unpack_from("<HH", bsk, s)
        if pl == 3 and (an >= len(A.anims) or A.anims[an][3] is None):
            errs.append("animacion %d inexistente (codigo %#x)" % (an, c))
        if w16(bsk[s:s + 48], 8) & ~0x7F:
            errs.append("bandera de SB en el sub-bloque %#x" % c)
        _, nap, apo = struct.unpack_from("<3I", bsk, s + 0x24)
        if nap:
            tabs[apo] += 1
        for q in range(nap):
            t, nl, do = struct.unpack_from("<HHI", bsk, apo + 8 * q)
            for ln in range(nl):
                x = bsk[do + 16 * ln:do + 16 * ln + 16]
                if t == 1:
                    h = struct.unpack_from("<H", x, 4)[0]
                    if h != 0xFFFF and h >= nhr:
                        errs.append("HR fuera de rango (%#x)" % c)
                    if x[11]:
                        errs.append("linea de golpe con formato SB (%#x)" % c)
                    for line in range(8 if h < nhr else 0):
                        st, sc = struct.unpack_from("<HH", bsk, hr + 128 * h + 16 * line + 4)
                        if st > 4:
                            errs.append("tipo HR %d (%#x)" % (st, c))
                        if st == 3 and (sc >= ns_spx or T[sc] == 0xFFFFFFFF) and sc not in d_slots:
                            errs.append("guion SPX %d inexistente (%#x)" % (sc, c))
                if t >= 3 and ln == 0 and sorted(bsk[do + 16 * i + 2] for i in range(nl)) != list(range(nl)):
                    errs.append("ids de linea AP%d que no son 0..n-1 (%#x)" % (t, c))
                if t == 7:
                    kk, v = struct.unpack_from("<II", x, 4)
                    if kk > 9 or (kk == 0 and v not in B3_C0):
                        errs.append("linea AP7 clase %d valor %#x (%#x)" % (kk, v, c))
    if any(v > 1 for v in tabs.values()):
        errs.append("tablas AP compartidas")
    # motor y guiones del donante
    need = {c for c in range(D.n) if D.has(c) and c in bp.ENGINE and c not in bp.BASIC}
    if need - defined:
        errs.append("codigos del motor sin definir %s" % [hex(c) for c in sorted(need - defined)])
    stray = sorted(c for c in defined if c < 0x200 and c not in bp.ENGINE | bp.BASIC | {0})
    if stray:
        errs.append("codigos bajos fuera de lugar %s" % [hex(c) for c in stray])
    for sl in range(ns_spx):
        for v in bp.spx_code_refs(spx, sl):
            if D.has(v) and v not in defined:
                errs.append("el guion %d empuja %#x y no esta" % (sl, v))
    span = bp.spx_slot_span(spx, 20)
    if span:
        cs0 = struct.unpack_from("<I", spx, 0x14)[0]
        j = spx.find(b"\x08\x20", cs0 + T[20])
        base = struct.unpack_from("<H", spx, j + 2)[0]
        for c in (base, base + 1, base + 8, base + 9):
            if D.has(c) and c not in defined:
                errs.append("agarre: falta %#x" % c)
    # BCM
    bcm = next(x for x, t in ck if x[:4] == b"#BCM")
    st0, bl = bp.bcm_parse(bcm)
    allc = {w16(b, i) for b, _ in bl.values() for i in (12, 13, 14, 15)} - {0}
    if allc - defined:
        errs.append("combos sin golpe %s" % [hex(c) for c in sorted(allc - defined)])
    if any(c < 0x200 for c in allc):
        errs.append("combo a codigo bajo")
    # lo que llevan los nativos del donante (Broly 0x11, Freezer 0x1001...) tambien vale
    d_vals = {(w16(b, 0), w16(b, 4), w16(b, 6)) for b, _ in bp.bcm_parse(
        next(x for x, t in bp.amb_kids(d_cam) if x[:4] == b"#BCM"))[1].values()}
    for o, (b, _) in bl.items():
        if (w16(b, 0), w16(b, 4), w16(b, 6)) in d_vals:
            continue
        if w16(b, 0) not in (0, 1, 2) or w16(b, 4) & ~0x240F or w16(b, 6) & 0x100:
            errs.append("entrada con valores de SB: %s" % " ".join("%04x" % w16(b, i) for i in range(16)))
            break
    ws = [[w16(bl[o][0], i) for i in range(16)] for o in st0]
    if not any(w[1] == 8 and w[4] & 0xF == 1 for w in ws):     # Freezer: 0x1001
        errs.append("sin rafaga de ki")
    if not any(w[1] == 5 for w in ws):
        errs.append("sin agarre")
    if not any(w[1] == 0xF and w[4] & 0x400 for w in ws):
        errs.append("sin modo hiper")
    if any(w[4] & 4 for w in ws):
        errs.append("entrada de transformacion (la pone roster_build)")
    if models:
        mn = set()
        for m in models:
            mn |= set(model_bind(m)[1])
        # caras y esqueletos de formas que no se importan (Goku SSJ3 de SB1 / SSJ4 de SB2, mas
        # alla de 4 formas): el motor ignora las pistas de huesos que el modelo no tiene
        pre = {x.lstrip("X").split("_")[0] for x in mn}           # GOK, GK4 (SSJ4 de SB2)...
        bad = [x for x in A.names if x not in mn and not x.endswith("_FACE") and x.lstrip("X").split("_")[0] in pre]
        if bad:
            # aviso, no error: el motor ignora esas pistas (modelo de Heroes sin las alas de Cell)
            errs.append("aviso: huesos del AMM que no estan en el modelo: %s" % bad[:8])
    # HD
    import ps2hd  # noqa: PLC0415
    try:
        ps2hd.convert_block(anm)
        ps2hd.convert_block(cam)
    except Exception as e:  # noqa: BLE001
        errs.append("ps2hd: %s" % e)
    return errs


# ---------------------------------------------------------------- oraculo
def oracle(game="sb2", scale=None):
    """Convierte Gohan adulto de SB (BCGHL, donante 4) y lo compara con el Gohan adulto nativo
    de B3: cada codigo convertido contra el nativo que usa la misma animacion (casada por pose;
    entre varios nativos con esa animacion, el de AP mas parecidas) y las entradas del BCM."""
    sb = SbIso(ISOS[game])
    d_anm, d_cam, d_model = donor(4)
    rep = []
    scale = scale or DMG_SB.get("GHL", DMG_SCALE)
    anm, cam, info = port(sb, "GHL", d_anm, d_cam, d_model, rep, scale=scale)
    k = bp.amb_kids(anm)
    C, CA = Src(k[0][0], False), bp.Amm(k[1][0])
    N, NA = Src(bp.amb_kids(d_anm)[0][0], False), bp.Amm(bp.amb_kids(d_anm)[1][0])
    same = (lambda s, a, p: (a, p)), (lambda s, h: h), collections.Counter()

    def feats(A, idx):
        an = A.anims[idx]
        if not an[3]:
            return None
        nf = max(1, an[2])
        return an[2], [bp.pose(an, int(t * (nf - 1))) for t in (0, 0.5, 1)]

    by_anim = collections.defaultdict(list)
    for c in range(N.n):
        if N.has(c) and N.anim(c)[1] == 3:
            by_anim[N.anim(c)[0]].append(c)
    nf = {a: feats(NA, a) for a in by_anim}
    sb_codes = sorted({int(t, 16) for t in info["codigos"].values()} | {int(t, 16) for t in info["motor"].values()})

    def shape(aps):
        return sorted((t, len(ls)) for t, ls in aps)

    pairs, unmatched = [], []
    for c in sb_codes:
        an, pl = C.anim(c)
        f = feats(CA, an) if pl == 3 else None
        best = None
        for a, g in nf.items():
            if f and g and abs(f[0] - g[0]) <= max(3, 0.25 * f[0]):
                dd = sum(bp.pose_dist(x, y) for x, y in zip(f[1], g[1])) / 3
                if best is None or dd < best[0]:
                    best = (dd, a)
        if not best or best[0] > 6.0:
            unmatched.append(c)
            continue
        ca = C.block(c, *same)[1]
        cands = [(x, N.block(x, *same)[1]) for x in by_anim[best[1]]]
        x, na = max(cands, key=lambda xn: (shape(xn[1]) == shape(ca),
                                            -abs(len(xn[1]) - len(ca)), -abs(xn[0] - c)))
        pairs.append((c, x, ca, na))
    stat, ratios = collections.Counter(), []
    fx = collections.defaultdict(collections.Counter)
    for c, x, ca, na in pairs:
        stat["pares"] += 1
        stat["mismas AP (tipo y n de lineas)"] += shape(ca) == shape(na)
        h1 = [ln for t, ls in ca if t == 1 for ln in ls]
        h2 = [ln for t, ls in na if t == 1 for ln in ls]
        if h1 and len(h1) == len(h2):
            stat["pares con los mismos golpes"] += 1
            for p, q in zip(h1, h2):
                stat["lineas de golpe"] += 1
                stat["  frame igual"] += p[0:2] == q[0:2]
                stat["  props igual"] += p[8:10] == q[8:10]
                stat["  parte del cuerpo igual"] += p[10] == q[10]
                stat["  radio igual"] += p[12] == q[12]
                stat["  posicion igual"] += p[13:16] == q[13:16]
                hp, hq = struct.unpack_from("<H", p, 4)[0], struct.unpack_from("<H", q, 4)[0]
                if 0xFFFF in (hp, hq):
                    continue
                for li in range(8):
                    u = C.b[C.hr + 128 * hp + 16 * li:C.hr + 128 * hp + 16 * li + 16]
                    v = N.b[N.hr + 128 * hq + 16 * li:N.hr + 128 * hq + 16 * li + 16]
                    stat["lineas HR"] += 1
                    stat["  HR tipo igual"] += u[4:6] == v[4:6]
                    stat["  HR tipo+codigo+empuje+especifico igual"] += u[4:16] == v[4:16]
                    du, dv = struct.unpack_from("<H", u)[0], struct.unpack_from("<H", v)[0]
                    if li == 0 and du and dv:
                        ratios.append(dv / du)
        L1 = {(struct.unpack_from("<H", x_)[0], struct.unpack_from("<I", x_, 4)[0]): struct.unpack_from("<I", x_, 8)[0]
              for t, ls in ca if t == 7 for x_ in ls}
        L2 = {(struct.unpack_from("<H", x_)[0], struct.unpack_from("<I", x_, 4)[0]): struct.unpack_from("<I", x_, 8)[0]
              for t, ls in na if t == 7 for x_ in ls}
        for key in set(L1) | set(L2):
            fx[key[1]]["nativas" if key in L2 else "solo convertidas"] += 1
            if key in L1 and key in L2:
                fx[key[1]]["en los dos"] += 1
                fx[key[1]]["mismo valor"] += L1[key] == L2[key]
    pct = {"mismas AP (tipo y n de lineas)": "pares", "pares con los mismos golpes": "pares",
           "  frame igual": "lineas de golpe", "  props igual": "lineas de golpe",
           "  parte del cuerpo igual": "lineas de golpe", "  radio igual": "lineas de golpe",
           "  posicion igual": "lineas de golpe", "  HR tipo igual": "lineas HR",
           "  HR tipo+codigo+empuje+especifico igual": "lineas HR"}
    out = ["oraculo: GHL de %s convertido (dano x%.3f) contra Gohan adulto de B3 (ID 4)" % (game.upper(), scale),
           "  codigos de SB convertidos %d; con animacion casada %d; sin pareja %s" % (
               len(sb_codes), len(pairs), [hex(c) for c in unmatched])]
    for key in ("pares", "mismas AP (tipo y n de lineas)", "pares con los mismos golpes", "lineas de golpe",
                "  frame igual", "  props igual", "  parte del cuerpo igual", "  radio igual",
                "  posicion igual", "lineas HR",
                "  HR tipo igual", "  HR tipo+codigo+empuje+especifico igual"):
        v = stat[key]
        out.append("  %-44s %5d%s" % (key, v, "  (%.0f %%)" % (100 * v / stat[pct[key]]) if key in pct and stat[pct[key]] else ""))
    if ratios:
        out.append("  dano nativo / convertido (1.a linea HR): mediana %.3f, cuartiles %s (n %d)" % (
            statistics.median(ratios), [round(q, 2) for q in statistics.quantiles(ratios, n=4)], len(ratios)))
    for k_ in sorted(fx):
        f = fx[k_]
        out.append("  AP7 clase %d: nativas %d, en los dos (frame) %d, mismo valor %d%s, solo convertidas %d" % (
            k_, f["nativas"], f["en los dos"], f["mismo valor"],
            " (%.0f %%)" % (100 * f["mismo valor"] / f["en los dos"]) if f["en los dos"] else "", f["solo convertidas"]))
    # BCM: entradas de arranque (boton + direccion + estado + tipo) nativas y convertidas
    def entries(c):
        st, bl = bp.bcm_parse(c)
        return {(entry_name(bl[o][0]), w16(bl[o][0], 4) & 0x40F, w16(bl[o][0], 6) & 0x8000) for o in st}
    e1 = entries(next(x for x, t in bp.amb_kids(cam) if x[:4] == b"#BCM"))
    e2 = entries(next(x for x, t in bp.amb_kids(d_cam) if x[:4] == b"#BCM"))
    fmt = lambda s_: sorted("%s c%x%s" % (n_, c_, " hiper" if h_ else "") for n_, c_, h_ in s_)  # noqa: E731
    out.append("  BCM: entradas en los dos %d de %d nativas; solo nativas %s; solo convertidas %s" % (
        len(e1 & e2), len(e2), fmt(e2 - e1), fmt(e1 - e2)))
    errs = verify(anm, cam, d_anm, d_cam)
    out.append("  verify: %s" % ("OK" if not errs else errs))
    return out, stat, ratios, errs


# ---------------------------------------------------------------- autocomprobacion
def selftest():
    # golpe: radio y caja de SB pasan a su sitio de B3 (par casado de Krilin, 0x18C <-> 0xFC)
    sb = bytes.fromhex("1200000106010000600004960000000000000000")
    assert hit_line(sb)[8:16] == bytes.fromhex("6000040096000000")
    assert hit_line(bytes(11) + b"\x1e" + struct.pack("<3h", -20, 300, 5) + bytes(2))[11:16] == b"\x00\x1e\xec\x7f\x05"
    # efectos: clase 0 de SB sin uso en B3 fuera; sonido por tabla; clase 10 (guion SB) fuera
    st = collections.Counter()
    fx = lambda k, v: fx_line(struct.pack("<HHII", 5, 0x200, k, v) + bytes(8), st)  # noqa: E731
    assert fx(0, 0x7D0) is None and fx(0, 0x64) is not None
    assert struct.unpack_from("<I", fx(1, 0x3D), 8)[0] == 0x51 and fx(1, 0xC) is None
    assert struct.unpack_from("<I", fx(3, 0xA), 8)[0] == 0x12 and fx(3, 0x24) is None
    assert fx(10, 0x1E) is None and fx(6, 0x20) is not None
    assert [x[2] for x in renumber([bytes([0, 0, 4, 2]), bytes([0, 0, 1, 2]), bytes([0, 0, 5, 2])])] == [1, 0, 2]
    # HR: 160 -> 128, dano x0,85, guion SB -> despide
    blk = b"".join(struct.pack("<HBBHHf", 120, 2, 2, 3 if i == 0 else 0, 0x1E, 70.0) + bytes(8) for i in range(8))
    hb = hr_block(blk, 0.85)
    assert len(hb) == 128 and struct.unpack_from("<HBBHH", hb)[::3] == (102, 2) and struct.unpack_from("<H", hb, 16)[0] == 102
    # codigos: desplazamiento, choque con el motor -> hueco con pareja +0x100
    m = alloc_codes([(0x400, 0x600, 0x600, 0), (0x433, 0x633, 0x633, 0), (0x488, 0x67C, 0x67C, 0)],
                    bp.ENGINE)
    assert m[0x400] == 0x200 and m[0x600] == 0x300
    assert m[0x433] not in bp.ENGINE and m[0x633] == m[0x433] + 0x100
    assert m[0x67C] == 0x37C and m[0x488] in GROUND
    # BCM: especial cond 4 -> 2 sin ki; definitivo ^E -> P+K+G+E en hiper; transformacion fuera
    def bcm(*blocks):
        out = bytearray(0x50)
        struct.pack_into("<H", out, 0x1E, len(blocks))
        out += b"".join(struct.pack("<I", 0x50 + 4 * len(blocks) + 0x40 * i) for i in range(len(blocks)))
        return bytes(out + b"".join(struct.pack("<16H", *b) + bytes(32) for b in blocks))
    c = bcm((2, 8, 0, 0, 4, 0, 0x101, 0, 6, 1000, 0, 0, 0x48B, 0x68B, 0x68B, 0),
            (0x10, 8, 0, 0, 8, 0, 0x101, 0, 14, 5000, 0, 0, 0x464, 0x664, 0x664, 0),
            (0x20, 8, 0, 0, 0x20, 0, 0x101, 0, 0, 4000, 0, 0, 0x500, 0x700, 0x700, 0),
            (1, 8, 0, 0x7530, 2, 2, 0x8000, 0, 6, 1000, 0, 0, 0x48C, 0x68C, 0x68C, 0),
            (0x20, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0x43E, 0x63E, 0x63E, 0))
    rep = []
    st_, nodes, var_ = sb_bcm_nodes(c, rep)
    assert [(w16(nodes[k][0], 6), w16(nodes[k][0], 3), w16(nodes[k][0], 9)) for k in var_] == [(0, 0, 0)]
    got = [[w16(nodes[k][0], i) for i in (0, 1, 4, 6, 9)] + [nodes[k][2]] for k in st_]
    assert got == [[2, 8, 2, 1, 0, "especial"], [0, 0xF, 0xA, 0x8001, 0, "definitivo"],
                   [0, 6, 0, 1, 0, "normal"]], got
    print("autocomprobacion sbport: OK")


# ---------------------------------------------------------------- CLI
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--juego", choices=("sb1", "sb2"), default="sb2")
    ap.add_argument("--iso", help="ISO de PSP (por defecto la de ps2_games)")
    ap.add_argument("--lista", action="store_true", help="lista los personajes (TAB)")
    ap.add_argument("--personaje", help="codigo de 3 letras (GHF, GOK...)")
    ap.add_argument("--donante", type=int, help="ID del personaje B3 donante (por defecto el sugerido)")
    ap.add_argument("--salida", help="carpeta de salida")
    ap.add_argument("--modelos", nargs="*", default=[], help="modelos del personaje (PS2/PSP/HD) si no "
                    "son los de SB: el AMM se escribe con sus nombres de hueso")
    ap.add_argument("--forma-m", type=int, help="convierte tambien BC<XXX>M<n>.amb (anm_m<n>.bin)")
    ap.add_argument("--definitivo-donante", type=int, metavar="CAPSULA",
                    help="el definitivo de SB con esa capsula usa la cinematica del donante (0x25A)")
    ap.add_argument("--dano", type=float, help="escala de dano (por defecto la medida del personaje "
                    "si esta en B3, si no 0.85)")
    ap.add_argument("--oraculo", action="store_true")
    ap.add_argument("--prueba", action="store_true")
    a = ap.parse_args()
    if a.prueba:
        selftest()
        return 0
    if a.oraculo:
        out, _, _, errs = oracle(a.juego, a.dano)
        print("\n".join(out))
        return 1 if errs else 0
    sb = SbIso(a.iso or ISOS[a.juego])
    if a.lista:
        for code, i in sb.characters():
            nm, dn = NAMES.get(code, (code, 0))
            forms = sum(1 for x in sb.names if x.startswith("BC%sB" % code))
            ms = [x[len(code) + 3:-4] for x in sb.names if x.startswith("BC%sM" % code)]
            print("personaje\t%s\t%s\t%s\t%d\t%d\t%d\t%s" % (a.juego, code, nm, i, dn, forms, ",".join(ms)))
        return 0
    if not (a.personaje and a.salida):
        ap.error("hace falta --personaje y --salida (o --lista / --oraculo / --prueba)")
    code = a.personaje.upper()
    cid = a.donante if a.donante is not None else NAMES.get(code, ("", 4))[1]
    d_anm, d_cam, d_model = donor(cid)
    models = [open(m, "rb").read() for m in a.modelos]
    import ps2hd  # noqa: PLC0415
    os.makedirs(a.salida, exist_ok=True)
    report = []
    outs = [(None, "anm_forma1")] + ([(a.forma_m, "anm_m%d" % a.forma_m)] if a.forma_m is not None else [])
    info, errs = None, []
    for fm, name in outs:
        anm, cam, inf = port(sb, code, d_anm, d_cam, d_model, report, models, fm,
                             a.dano or DMG_SB.get(code, DMG_SCALE),
                             a.definitivo_donante)
        info = info or inf
        errs += verify(anm, cam, d_anm, d_cam, models)
        for data, fn in ((anm, name), (cam, "camara")):
            open(os.path.join(a.salida, fn + "_ps2.bin"), "wb").write(data)
            open(os.path.join(a.salida, fn + ".bin"), "wb").write(bytes(ps2hd.convert_block(data)))
    info.update(juego=a.juego, donante=cid, informe=report, errores=errs)
    json.dump(info, open(os.path.join(a.salida, "sbport.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print("\n".join(report))
    print("comprobacion: %s" % ("OK" if not errs else errs))
    print("ok: %s -> %s" % (code, a.salida))
    return 1 if [e for e in errs if not e.startswith("aviso:")] else 0


if __name__ == "__main__":
    sys.exit(main())
