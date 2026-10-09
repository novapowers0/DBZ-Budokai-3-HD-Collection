#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""sb_tecnicas.py - Tecnicas de Shin Budokai (PSP) reformuladas para Budokai 3 HD.

RE 2026-10-06 (Gohan del Futuro de Another Road sobre Gohan adulto de B3; censo de los 38):

  Cadena B3: BCM (entrada, capsula w8) -> BSK (AP7 [frame][id][act][cat u32][val u32]) ->
  BSP: cat 4 = codigo de #AST (energia) o #ASE (efecto visual) del BSP del personaje;
  cat 0 = accion del motor (0x64/0x66 congelar, 0x68/0x69 beam struggle, 0x46/0x47 rayo...).
  Los c4 0x12c-0x13d (y unos pocos mas) son efectos comunes del motor; 0x15e/0x15f (y 0x160+)
  son el aspecto de las rafagas de ki: NUNCA se usan para tecnicas nuevas.

  #AST (bloques 0xF0, LE en PS2): +0x10 tipo (0 rayo, 1 bola, 3 disco/proyectil...),
    +0x12 codigo, +0x24 f32 duracion, +0x28 f32 velocidad, +0x2C hueso, +0x48/+0x4A texturas
    del rayo (indices del #AMT grande del BSP), +0xC8 dano, +0xCC f32 radio, +0xD4 aturdir,
    +0xDE 7 (no son formas: 7 en todos). Enlaces a #AME (grupos de u16 [.., origen, indice];
    origen 2 = hijo del propio #AMB, 0/6 = bancos comunes): +0x30..+0x36 (cola del rayo),
    +0x58..+0x5E (cabeza / la bola), +0x70..+0x7A y +0xAC..+0xB6 (impacto). Los bloques de
    Shin Budokai tienen los MISMOS campos; los que B3 no usa nunca (censo): +0x14 (variante
    normal/potenciada), +0x16, +0xCA (2o dano), +0xDC, +0xEC/+0xEE -> a 0.
  #ASE (B3 0xD0 / SB 0xB0): +0x50..+0x5E enlace al #AME, +0x6A codigos (inicio, medio, fin:
    el efecto empieza con el 1o y se apaga cuando suena el 3o), hueso B3 +0x80 / SB +0x48.
  #AME: B3 [n nodos, cabecera 0x10], arbol por punteros (+0x24 hijo, +0x28 hermano), tipo en
    +6 (1 emisor, 2 particula, 3 campo); particula: 2 colores RGBA f32 en +0x80 y textura
    (+0xA0 = 1, +0xA4 = indice del #AMT grande). El de SB es otro sistema (nodos Line/Sprite,
    colores RGBA8) -> no se convierte: se clonan los del donante con el tono de SB.
  Nombres oficiales: texturas data_btl_us bf<XXX>.amt / bf<XXX>m1-3.amt (una lista por forma:
    >E, <E, ^E). El "booster" w8 del BCM de SB es la MASCARA DE FORMAS (bit0 normal, bit1
    forma 2...), la misma que el +14 de la capsula #SKC de B3 (Super Kamehameha 0x0E en ambos).
  B3 resuelve una entrada compartida (misma direccion) por la mascara de formas de la capsula
    (Goku, Vegeta, Buu M: mascaras disjuntas por entrada). Todos los especiales nativos con
    entrada directa congelan (c0 0x64 ... 0x66; 72/79).
  Definitivo: P+K+G+E (cond 0x000A, cond2 0x8001) -> golpe HR tipo 3 codigo 0 -> SPX ranura 0,
    que empuja 0x4A0+ (animaciones de la cinematica; 17/38 nativos). La ranura 20 es el agarre
    (BASE 0x480: 0x480/0x481/0x488/0x489). La nota de b1port ("0x4A0 = lanzamientos, ranura 0 =
    acometida") estaba al reves: census en scratchpad/tec/census_4a0.txt.

Uso (las ISOs de PSP en ps2_games/, el moveset de sbport.py ya hecho):
  python sb_tecnicas.py todo --personaje GHF --donante 4 --salida DIR --moveset DIR_SBPORT
      -> DIR/tecnicas.bin (BSP HD), anm_forma1(_ps2).bin + camara(_ps2).bin con las tecnicas,
         capsulas.toml ([[capsula]] propuesto) y nombres/ (rotulos oficiales de SB en PNG)
  python sb_tecnicas.py bsp|nombres|aplicar ...   (partes sueltas, mismas opciones)
  python sb_tecnicas.py comprobar DIR [--donante 4]   (tecnicas.bin + *_ps2.bin frente a B3)
  python sb_tecnicas.py prueba                        (autocomprobacion)
  --receta r.json: {"0x491": {"nombre": "Masenko", "tono": 52, "dano": 250, "ki": 10}, ...}
"""
import argparse
import colorsys
import json
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "mod center hd")]

import iso as _iso  # noqa: E402
SB_ISO = {g: _iso.find_game(g) or os.path.join(ROOT, "ps2_games", fn) for g, fn in (   # cualquier region
    ("sb1", "Dragonball Z Shin Budokai (USA).iso"), ("sb2", "Dragonball Z Shin Budokai Another Road (USA).iso"))}
DMG_AST = 0.625          # dano de energia B3 / SB (Kamehameha de Gohan adulto: 250 / 400)
AST_ZERO = (0x14, 0x16, 0xCA, 0xDC, 0xEC, 0xEE)     # campos SB que ningun AST de B3 usa
AST_LOW = (1, 2, 3, 4)             # los de las tecnicas nuevas (ver build_bsp)
AST_NEW = list(range(1, 0x10))     # libres y BAJOS: los 38 nativos solo usan 0-4 (con 0x53 la bola
                                    # del Burning Attack no hacia dano, prueba 4); los equipados primero
ASE_NEW = list(range(0x58, 0x60)) + list(range(0x72, 0x7F))
RESERVED = {0x15E, 0x15F} | set(range(0x160, 0x170)) | set(range(0x12C, 0x140))
FREEZE_BEFORE = 11       # 0x66 (descongelar) 11 frames antes de soltar la energia (donante)


# ---------------------------------------------------------------- fuentes de Shin Budokai
class SbAfs:
    """AFS de una ISO de Shin Budokai (con tabla de nombres), leido en su sitio."""

    def __init__(self, game, name):
        import iso  # noqa: PLC0415
        self.iso = iso.Iso(SB_ISO[game])
        self.f = self.iso.open(self.iso.find_region(name))
        self.f.seek(4)
        n = struct.unpack("<I", self.f.read(4))[0]
        self.tab = [struct.unpack("<II", self.f.read(8)) for _ in range(n)]
        noff, nsz = struct.unpack("<II", self.f.read(8))
        self.f.seek(noff)
        blob = self.f.read(nsz)
        self.names = [blob[48 * i:48 * i + 32].split(b"\0")[0].decode("latin1").upper() for i in range(n)]

    def get(self, name):
        o, s = self.tab[self.names.index(name.upper())]
        self.f.seek(o)
        return self.f.read(s)


def sb_sources(game, code):
    a = SbAfs(game, "data_btl_cmn.afs")
    return a.get("BC%s.amb" % code), a.get("BSP_%s.amb" % code)


# ---------------------------------------------------------------- #AMB (PS2/PSP LE, HD BE)
def kids(d, e="<"):
    n, t = struct.unpack_from(e + "II", d, 0x10)
    return [(bytes(d[o:o + s]), ty) for o, s, ty, _ in (struct.unpack_from(e + "4I", d, t + 16 * k) for k in range(n))]


def amb_hd(children):
    """#AMB HD (version 2, hijos alineados a 32), el mismo formato que ps2hd.conv_container."""
    n = len(children)
    start = (0x20 + 16 * n + 31) // 32 * 32
    out = bytearray(b"#AMB" + struct.pack(">7I", 0x20, 0, 2, n, 0x20, start, 0))
    out += bytes(start - len(out))
    ents = []
    for data, ty in children:
        out += bytes((-len(out)) % 32)
        ents.append((len(out) if data else 0, len(data), ty, 0))
        out += data
    out += bytes((-len(out)) % 32)
    for k, ent in enumerate(ents):
        struct.pack_into(">4I", out, 0x20 + 16 * k, *ent)
    return bytes(out)


def wk_blocks(t):
    """Bloques de una tabla #AST/#ASE (LE): [magic][pj 4 B][n][inicio] + n bloques."""
    n, st = struct.unpack_from("<II", t, 8)
    bs = (len(t) - st) // n if n else 0
    return [bytearray(t[st + k * bs:st + (k + 1) * bs]) for k in range(n)]


def wk_table(head, blocks):
    out = bytearray(head[:0x10])
    struct.pack_into("<I", out, 8, len(blocks))
    for k, b in enumerate(blocks):
        b[0:4] = b"wk" + str(k).encode()[:2].ljust(2, b"\0")   # "wk0", "wk10"...
    return bytes(out) + b"".join(bytes(b) for b in blocks)


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def put16(b, o, v):
    struct.pack_into("<H", b, o, v & 0xFFFF)


# ---------------------------------------------------------------- Shin Budokai: BCM / BSK / BSP
def sb_bcm(bcm):
    """Entradas de tecnica del BCM de SB: [{dir, botones, cond, cond2, booster, ki, codigos}]."""
    n = u16(bcm, 0x1E)
    todo = [struct.unpack_from("<I", bcm, 0x50 + 4 * k)[0] for k in range(n)]
    seen, out = set(), []
    while todo:
        o = todo.pop(0)
        if o in seen or not 0x50 <= o <= len(bcm) - 0x40:
            continue
        seen.add(o)
        w = struct.unpack_from("<16H", bcm, o)
        # tecnica: E con direccion (->, <-, ^) y condicion de especial (2, 4 cuerpo a cuerpo) o de
        # definitivo (8); 0x20 = transformarse y 0x40/0x80 = estado aura de SB no lo son
        # (definitivo: cond 8 en Another Road, 0x10 en Shin Budokai 1)
        if w[1] == 8 and w[0] in (1, 2, 0x10) and w[4] & 0x001E and not w[4] & 0x00E0:
            out.append(dict(off=o, dir=w[0], botones=w[1], cond=w[4], cond2=w[6], booster=w[8], ki=w[9],
                            codigos=[c for c in w[12:15] if c]))
        todo += [struct.unpack_from("<I", bcm, o + 0x40 + 4 * k)[0] for k in range(min(w[7], 64))]
    return out


def sb_lines(bsk, code, step=20):
    """Lineas AP de un codigo del BSK de SB (lineas de 20 B): [(tipo, frame, cat, val, bytes)]."""
    n, lst, nhr, hr = struct.unpack_from("<4I", bsk, 0x10)
    a = struct.unpack_from("<I", bsk, lst + 4 * code)[0] if code < n else 0
    starts = set(struct.unpack_from("<%dI" % n, bsk, lst))
    out, s = [], a
    while a and s + 48 <= hr:
        pad, nap, apo = struct.unpack_from("<3I", bsk, s + 0x24)
        if pad or not 0 <= nap < 64:
            break
        for k in range(nap):
            t, nl, do = struct.unpack_from("<HHI", bsk, apo + 8 * k)
            for i in range(nl if t == 7 else 0):
                fr, _, _, cat, val = struct.unpack_from("<HBBII", bsk, do + step * i)
                out.append((fr, cat, val))
        s += 48
        if s in starts:
            break
    return out


def sb_bsp(bsp):
    """{'ast': {codigo: bloque variante 0}, 'ase': {codigo: bloque}, 'amb0': [...], 'amb1': [...]}"""
    top = kids(bsp)
    a0, a1 = kids(top[0][0]), kids(top[1][0])
    ast = {}
    for b in wk_blocks(a0[0][0]):
        if u16(b, 0x14) == 0:
            ast.setdefault(u16(b, 0x12), b)
    ase = {}
    for b in wk_blocks(a1[0][0]):
        ase.setdefault(u16(b, 0x6A), b)
    return dict(ast=ast, ase=ase, top=top, amb0=a0, amb1=a1)


def sb_tecnicas(bc, bsp):
    """Tecnicas del personaje SB: una por codigo de ataque de entrada (la variante tras combo
    comparte animacion y lineas). [{codigo, entrada, lineas, ast, ase}]"""
    k = kids(bc)
    bcm = next(x for x, t in k if x[:4] == b"#BCM")
    bsk = next(x for x, t in k if x[:4] == b"#BSK")
    p = sb_bsp(bsp)
    out, seen, var = [], set(), []
    for e in sb_bcm(bcm):
        c = e["codigos"][0]
        if e["cond2"] & 0x8000:                   # variante (w6 0x8000): mismas lineas, otro codigo
            var.append(e)
            continue
        if c in seen:
            continue
        seen.add(c)
        ls = sb_lines(bsk, c)
        c4 = list(dict.fromkeys(v for f, cat, v in ls if cat == 4))
        out.append(dict(codigo=c, entrada=e, lineas=ls, ast=[v for v in c4 if v in p["ast"]], variantes=[],
                        ase=[v for v in c4 if v in p["ase"]], otros=[v for v in c4 if v not in p["ast"] and v not in p["ase"]]))
    for e in var:
        t = next((t for t in out if t["entrada"]["dir"] == e["dir"] and t["entrada"]["booster"] == e["booster"]
                  and t["entrada"]["botones"] == e["botones"]), None)
        if t is not None:
            t["variantes"] += [c for c in e["codigos"][:2] if c not in t["variantes"]]
    return out, p


# ---------------------------------------------------------------- recetas (por personaje)
# tecnica SB (codigo de ataque) -> como se reformula. tono: grados HSV del color (None = el del
# donante); tipo: tipo de AST en B3 si cambia; dano: dano B3 fijo (si no, SB x DMG_AST);
# donante: {codigo SB: codigo del BSP del donante} (se usa su efecto tal cual).
RECETAS = {
    # equilibrio frente a los nativos (censo de 79 especiales): 1 barra (ki 10) ~250 de dano,
    # 2 barras (ki 20) ~400 (Finish Buster, Destructo Disc, Soaring Dragon Strike 410).
    # formas = mascara de la capsula (+14) = el booster de SB (bit 0 normal ... bit 3 Potencial).
    "GHF": {
        # el Kamehameha de GHF dispara en el frame 45 (el del donante, 0x24B, en el 46): sus
        # lineas de clase 0/4/6 (carga 0x4/0x5, rayo 0x0, 0x64/0x68/0x66, 0x46/0x47) con -1
        0x460: dict(nombre="Kamehameha", donante={0xA: 0x4, 0x15F: 0x0}, plantilla_donante=0x24B,
                    desfase=-1, beam_struggle=True, ki=10, equipada=True,
                    voz=[(11, 0x2E), (45, 0x2F)]),    # banco de F: 46 "Kamehame!", 47 "HA!" (gritos.sq_enable los enciende)
        # ast_nativo = (ID, codigo #AST) de una tecnica nativa de B3: su energia con sus efectos
        # (2026-10-08, pruebas en juego: la bola de SB salia como un destello morado sin rayo y
        # el proyectil de la Z Sword no se veia ni golpeaba)
        0x488: dict(nombre="Spirit Shot", ast_nativo=(39, 0), dano=250, ki=10, equipada=False),  # bola: Riot Javelin
        0x48B: dict(nombre="Evasive Kick", dano_golpe=250, ki=10, equipada=False),
        0x491: dict(nombre="Masenko", tipo=0, tono=52, dano=250, ki=10, equipada=False),     # rayo amarillo
        # Z Sword: lanza la espada. El Kienzan nativo (tipo 3) es un MODELO (#AMO del BSP de
        # Krilin): sin el no sale nada (prueba 2026-10-08). Proyectil de energia (bola, tipo 1)
        # azul; sin el 2o destello de SB (tras la carrera llenaba la pantalla)
        # Tras lanzar, SB lo hace correr hacia delante (encima de su proyectil): se queda en su
        # sitio (sin_retroceso fija la cintura desde el lanzamiento) y la carga acaba al lanzar
        0x495: dict(nombre="Z Sword", tipo=1, tono=200, dano=400, ki=20, equipada=False,
                    donante={0x14: 0x10}, sin_retroceso=True, extra=[(38, 4, 0x10)]),
        0x49C: dict(nombre="Burning Attack", tono=38, dano=400, ki=20, equipada=True,
                    sin_retroceso=True,      # el salto atras de 6 unidades en 3 frames al disparar
                    voz=[(41, 0x1A), (57, 0x16)]),   # GHF no tiene "Burning Attack!": "Take this!" + "HA!"
        0x499: dict(nombre="Special Beam Cannon", tono=300, tipo=0, dano=480, radio=6.0, especial=1, ki=30,
                    equipada=False, omitir="definitivo de SB como especial: el personaje se quedaba "
                    "congelado tras disparar (prueba en juego 2)"),
        0x464: dict(nombre="Super Kamehameha", definitivo=True, ki=50, equipada=True),
    },
    # Trunks con espada (Another Road). Rotulos oficiales (texturas bftrx/bftrxm1). B3 solo tiene
    # una definitiva: la de la forma normal (Burning Slash, con las poses de SB sobre la
    # cinematica de Trunks: cinematica.py) en todas las formas; Heat Dome Attack (SSJ) fuera.
    "TRX": {
        0x45C: dict(nombre="Masenko", tono=52, equipada=True),          # bola amarilla (sin tono: azul del donante)
        0x460: dict(nombre="Buster Cannon", equipada=True),
        0x468: dict(nombre="Shining Slash", equipada=True),
        0x471: dict(nombre="Burning Slash", definitivo=True, todas_las_formas=True, equipada=True),
        0x476: dict(nombre="Heat Dome Attack", omitir="B3 solo tiene una definitiva por personaje: "
                    "va Burning Slash (forma normal de SB) en todas las formas"),
    },
}


def donor_ult(donor):
    """Codigo del definitivo cinematico del donante (entrada P+K+G+E cond 8 del BCM) o None."""
    import afs_pair  # noqa: PLC0415
    import b1port as bp  # noqa: PLC0415
    st, bl = bp.bcm_parse(next(x for x, t in bp.amb_kids(afs_pair.ps2(donor_ids(donor)["cam"])) if x[:4] == b"#BCM"))
    return next((bp.w16(bl[o][0], 12) for o in st if bp.w16(bl[o][0], 4) & 0x8 and bp.w16(bl[o][0], 1) == 0xF), None)


def receta_de(code, techs, receta=None, donor=None):
    """Receta completa por codigo SB: la de RECETAS (o --receta) y, para lo que falte, la
    automatica: nombre provisional, ki de SB, dano SB x DMG_AST; de los ^E de SB el que vale en
    mas formas pasa a ser el definitivo cinematico del donante y el resto, especiales ->E."""
    base = receta if receta is not None else RECETAS.get(code, {})
    ults = [t for t in techs if t["entrada"]["dir"] == 0x10 or t["entrada"]["cond"] & 0x18]
    main = max(ults, key=lambda t: (bin(t["entrada"]["booster"]).count("1"), t["codigo"]), default=None)
    out, k = {}, 0
    for t in techs:
        r = dict(base.get(t["codigo"], {}))
        if not base.get(t["codigo"]):
            if t is main:
                r.update(nombre="Ultimate", definitivo=True)
            else:
                k += 1
                r.update(nombre="Special %d" % k)
                if t in ults:
                    r.update(especial=1)
        r.setdefault("ki", (t["entrada"]["ki"] + 500) // 1000 * 10)
        out[t["codigo"]] = r
    if donor is not None and any(r.get("definitivo") for r in out.values()) and donor_ult(donor) is None:
        for r in out.values():          # sin cinematica que heredar: especial ->E
            if r.pop("definitivo", None):
                r["especial"] = 1
    return out


def cadenas(techs, receta):
    """Tecnicas que EVOLUCIONAN con la forma (SB no gasta otra capsula: en la misma entrada, el
    Masenko de la forma normal pasa a Spirit Shot en SSJ y a Kamehameha en SSJ2; como el
    Kamehameha x10 del SSJ4 de B3). Especiales de la misma direccion/botones con mascaras de
    formas (booster) no nulas y disjuntas. -> [[tecnica, ...]] ordenadas por su 1a forma; la
    capsula (y la entrada del BCM) es la de la 1a, salvo que otra tenga beam struggle."""
    low = lambda t: (t["entrada"]["booster"] & -t["entrada"]["booster"]).bit_length()   # noqa: E731
    groups = {}
    for t in techs:
        r, e = receta[t["codigo"]], t["entrada"]
        if r.get("definitivo") or r.get("omitir") or r.get("especial") or not e["booster"]:
            continue
        groups.setdefault((e["dir"], e["botones"]), []).append(t)
    out = []
    for ts in groups.values():
        chain, mask = [], 0
        for t in sorted(ts, key=low):
            if not t["entrada"]["booster"] & mask:
                chain.append(t)
                mask |= t["entrada"]["booster"]
        if len(chain) > 1:
            out.append(chain)
    return out


def cadena_principal(chain, receta):
    return next((t for t in chain if receta[t["codigo"]].get("beam_struggle")), chain[0])


def placeholders(techs, receta):
    """Capsula provisional (en el BCM) de cada tecnica: la fija de PLACEHOLDER por nombre o la
    siguiente de la reserva (registros sin dueno: si no se reemplaza, el golpe no sale)."""
    names = [receta[t["codigo"]].get("nombre") for t in techs]
    fixed = {PLACEHOLDER[n] for n in names if n in PLACEHOLDER}
    pool = [x for x in (589, 590, 591, 592, 593, 594, 585, 586, 587, 588) if x not in fixed]
    return {t["codigo"]: PLACEHOLDER.get(n) or pool.pop(0) for t, n in zip(techs, names)}


# ---------------------------------------------------------------- color
def hue_rgb(rgb, hue):
    """Mismo brillo y saturacion, otro tono (grados). rgb en 0..1."""
    h, s, v = colorsys.rgb_to_hsv(*rgb)
    return colorsys.hsv_to_rgb(hue / 360.0, s, v) if s > 0.12 else tuple(rgb)


def hue_image(img, hue):
    a = img[..., :3].astype(np.float32) / 255.0
    mx, mn = a.max(-1), a.min(-1)
    s = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    # tono nuevo, misma saturacion y valor (vectorizado: HSV -> RGB)
    h = np.full_like(mx, hue / 60.0)
    i = np.floor(h).astype(int) % 6
    f = h - np.floor(h)
    p, q, t = mx * (1 - s), mx * (1 - s * f), mx * (1 - s * (1 - f))
    sel = [(mx, t, p), (q, mx, p), (p, mx, t), (p, q, mx), (t, p, mx), (mx, p, q)]
    out = np.zeros_like(a)
    for k, (r, g, b) in enumerate(sel):
        m = i == k
        out[..., 0][m], out[..., 1][m], out[..., 2][m] = r[m], g[m], b[m]
    res = img.copy()
    res[..., :3] = np.clip(np.round(out * 255), 0, 255).astype(np.uint8)
    return res


def img_saturated(img):
    a = img.astype(np.float32)
    vis = a[..., 3] > 20
    if not vis.any():
        return False
    return float((a[..., :3].max(-1) - a[..., :3].min(-1))[vis].mean()) > 40


def ame_nodes(a):
    """Offsets de los nodos de un #AME de B3 (PS2 LE), recorriendo el arbol."""
    n, hs = struct.unpack_from("<II", a, 8)
    seen, todo = [], [hs]
    while todo:
        o = todo.pop()
        if not o or o in seen or o + 0x40 > len(a):
            continue
        seen.append(o)
        todo += list(struct.unpack_from("<II", a, o + 0x24))[::-1]
    return seen


def ame_retex(a, tex_new):
    """#AME (LE) con los indices de textura de sus particulas cambiados (otro #AMT)."""
    out = bytearray(a)
    for o in ame_nodes(a):
        if u16(a, o + 6) != 2:
            continue
        cnt, tex = struct.unpack_from("<II", a, o + 0xA0)
        if cnt == 1 and tex < 0x10000:               # mismas reglas que ame_recolor
            struct.pack_into("<I", out, o + 0xA4, tex_new(tex))
        else:
            cnt2, tex2 = struct.unpack_from("<II", a, o + 0xC0) if o + 0xC8 <= len(a) else (9, 0)
            if cnt2 in (0, 1) and tex2 < 0x100:
                struct.pack_into("<I", out, o + 0xC4, tex_new(tex2))
    return bytes(out)


# pares (origen, indice) de los enlaces #AME de un bloque #AST: cola, cabeza, 2o enlace de la
# bola, impacto e impacto 2 (origen 2 = hijo del propio #AMB del BSP; 0/6 = bancos comunes)
AST_LINKS = (0x34, 0x5C, 0x68, 0x78, 0xB4)


def ame_recolor(a, hue, tex_new):
    """#AME (LE) con las particulas de otro tono. tex_new(indice) -> indice de la textura
    recoloreada (o el mismo si es gris)."""
    out = bytearray(a)
    for o in ame_nodes(a):
        if u16(a, o + 6) != 2:
            continue
        cols = struct.unpack_from("<8f", a, o + 0x80)
        if all(-0.01 <= c <= 1.01 for c in cols):
            c1 = hue_rgb(cols[0:3], hue) + (cols[3],)
            c2 = hue_rgb(cols[4:7], hue) + (cols[7],)
            struct.pack_into("<8f", out, o + 0x80, *(c1 + c2))
        cnt, tex = struct.unpack_from("<II", a, o + 0xA0)
        if cnt == 1 and tex < 0x10000:
            struct.pack_into("<I", out, o + 0xA4, tex_new(tex))
        else:
            # otra variante de particula (estela del rayo, carga): textura en +0xC4 y colores
            # blancos; el azul esta en la textura (prueba 7: Burning Attack "azulado")
            cnt2, tex2 = struct.unpack_from("<II", a, o + 0xC0) if o + 0xC8 <= len(a) else (9, 0)
            if cnt2 in (0, 1) and tex2 < 0x100:
                struct.pack_into("<I", out, o + 0xC4, tex_new(tex2))
    return bytes(out)


# ---------------------------------------------------------------- texturas (#AZT HD)
def azt_entries(z):
    n, idx = struct.unpack(">II", z[0x10:0x18])
    return [struct.unpack_from(">I", z, idx + 4 * k)[0] for k in range(n)]


def azt_image(z, k):
    """Textura k del #AZT HD a resolucion HD (DDS DXT3 o A8R8G8B8) -> RGBA."""
    import io  # noqa: PLC0415
    from PIL import Image  # noqa: PLC0415
    o = azt_entries(z)[k]
    do, dl = struct.unpack_from(">II", z, o + 0x14)
    return np.array(Image.open(io.BytesIO(bytes(z[do:do + dl]))).convert("RGBA"))


def azt_append(z, items):
    """Anade texturas al #AZT HD sin mover las nativas (las HD del juego no salen de ps2hd:
    algunas van a 128x128 sin comprimir): datos DXT3 alineados a 0x80, entradas copiadas de
    una plantilla y una tabla indice nueva al final (como roster_build.azt_append).
    items = [(rgba, indice plantilla, (ancho, alto) logicos)] -> (#AZT, [indices nuevos])."""
    import amt_ps2  # noqa: PLC0415
    from texture_b3 import encode_dxt3  # noqa: PLC0415
    offs = azt_entries(z)
    n = len(offs)
    out = bytearray(z)
    last = max(o for o in offs if o)
    vram = struct.unpack_from(">I", z, last + 0x24)[0]
    ents = []
    for k, (img, tpl, (lw, lh)) in enumerate(items):
        h, w = img.shape[:2]
        blob = amt_ps2.dds_dxt3_header(w, h) + encode_dxt3(img)
        out += bytes((-len(out)) % 0x80)
        ent = bytearray(z[offs[tpl]:offs[tpl] + 0x30])
        log2 = lambda v: max(0, (v - 1).bit_length())    # noqa: E731
        struct.pack_into(">3I2H2H2I", ent, 0, n + k, struct.unpack_from(">I", ent, 4)[0], (w << 16) | h,
                         log2(lw), log2(lh), lw, lh, len(out), len(blob))
        vram += len(blob) // 0x20
        struct.pack_into(">I", ent, 0x24, vram)
        out += blob
        ents.append(ent)
    out += bytes((-len(out)) % 0x10)
    new = []
    for ent in ents:
        new.append(len(out))
        out += ent
    new_idx = len(out)
    out += struct.pack(">%dI" % (n + len(ents)), *(offs + new))
    out += bytes((-len(out)) % 0x20)
    struct.pack_into(">II", out, 0x10, n + len(ents), new_idx)
    return bytes(out), list(range(n, n + len(ents)))


def psp_tex(sb_amt, i):
    """Textura i del #AMT de PSP (indices de 4/8 bits "swizzled") -> RGBA."""
    import amt_ps2  # noqa: PLC0415
    import psp_amo  # noqa: PLC0415
    t = amt_ps2.entries(sb_amt, keep_empty=True)[i]
    w, h = t["w"], t["h"]
    W = (w * (4 if t["psm"] == 4 else 8) // 8 + 15) // 16 * 16
    H = (h + 7) // 8 * 8
    raw = bytes(sb_amt[t["data"]:t["data"] + W * H]).ljust(W * H, b"\0")
    lin = np.frombuffer(psp_amo.unswizzle(raw, W, H), np.uint8).reshape(H, W)
    if t["psm"] == 4:
        idx = np.empty((H, W * 2), np.uint8)
        idx[:, 0::2], idx[:, 1::2] = lin & 15, lin >> 4
    else:
        idx = lin
    clut = np.frombuffer(bytes(sb_amt[t["clut"]:t["clut"] + t["csize"]]), np.uint8).reshape(-1, 4)
    return clut[idx[:h, :w]].copy()


# ---------------------------------------------------------------- BSP hibrido
def donor_ids(donor):
    db = json.load(open(os.path.join(ROOT, "mod center hd", "roster_db.json"), encoding="utf-8"))
    return next(e for e in db["ids"] if e["id"] == donor)


def donor_bsp(donor):
    import afs_pair  # noqa: PLC0415
    fid = donor_ids(donor)["bsp"]
    return afs_pair.ps2(fid), afs_pair.hd(fid)


def build_bsp(code, game="sb2", donor=4, receta=None, sources=None):
    """-> (tecnicas.bin HD, mapa {codigo c4 SB: codigo B3} por tecnica, informe, textos).
    sources = (BC, BSP) de SB ya leidos (si no, de la ISO)."""
    import amt_ps2  # noqa: PLC0415
    import ps2hd  # noqa: PLC0415
    bc, bsp = sources or sb_sources(game, code)
    techs, sbp = sb_tecnicas(bc, bsp)
    receta = receta_de(code, techs, receta, donor)
    rep = []
    P, H = donor_bsp(donor)
    ptop, htop = kids(P), kids(H, ">")
    p0, p1 = kids(ptop[0][0]), kids(ptop[1][0])
    h0, h1 = kids(htop[0][0], ">"), kids(htop[1][0], ">")
    ast_t, ase_t = wk_blocks(p0[0][0]), wk_blocks(p1[0][0])
    # el #AMT grande (texturas de los efectos): arriba en casi todos; Gotenks lo lleva dentro del
    # #AMB de los #AST. big = (None | 0 | 1, indice)
    amts = [((None, i), x) for i, (x, t) in enumerate(ptop) if x[:4] == b"#AMT"]
    amts += [((a, i), x) for a, pp in ((0, p0), (1, p1)) for i, (x, t) in enumerate(pp) if x[:4] == b"#AMT"]
    big, amt = max(amts, key=lambda lx: struct.unpack_from("<I", lx[1], 0x10)[0])
    hazt = (htop if big[0] is None else (h0, h1)[big[0]])[big[1]][0]
    ntex = struct.unpack_from("<I", amt, 0x10)[0]
    assert len(azt_entries(hazt)) == ntex
    texs = {t["idx"]: t for t in amt_ps2.entries(amt)}
    own = {u16(b, 0x12) for b in ast_t} | {c for b in ase_t for c in struct.unpack_from("<3H", b, 0x6A)} - {0}
    # de donde leen sus texturas los #AST de este BSP (+0x20) y una textura suya de plantilla
    # para las anadidas (la de un rayo o rafaga: mismo formato)
    srcs = [bytes(b[0x20:0x24]) for b in ast_t if u16(b, 0x20) in (1, 2)]
    tex_src = max(set(srcs), key=srcs.count) if srcs else b"\x01\x00\x02\x00"
    tex_tpl = next((u16(b, 0x48) for b in ast_t if u16(b, 0x48) and u16(b, 0x48) < ntex), 0)
    new_ame0, new_ame1, extra_tex = [], [], []
    tex_cache, clone_cache = {}, {}
    sb_amt = next((x for x, t in sbp["top"] if x[:4] == b"#AMT" and struct.unpack_from("<I", x, 0x10)[0] > 4), None)

    def tex_hue(i, hue):
        if i >= ntex or i not in texs:
            return i
        key = (i, hue)
        if key not in tex_cache:
            img = azt_image(hazt, i)                    # a resolucion HD
            if img_saturated(img):
                extra_tex.append((hue_image(img, hue), i, (texs[i]["w"], texs[i]["h"])))
                tex_cache[key] = ntex + len(extra_tex) - 1
            else:
                tex_cache[key] = i
        return tex_cache[key]

    def clone(amb, idx, hue):
        """Indice del hijo #AME `idx` del AMB 0/1 del donante (o de los ya anadidos) recoloreado."""
        if hue is None:
            return idx
        key = (amb, idx, hue)
        if key not in clone_cache:
            lst, base = (new_ame0, len(p0)) if amb == 0 else (new_ame1, len(p1))
            src = (p0 if amb == 0 else p1)[idx][0] if idx < base else lst[idx - base]
            lst.append(ame_recolor(src, hue, lambda t: tex_hue(t, hue)))
            clone_cache[key] = base + len(lst) - 1
        return clone_cache[key]

    natives = {}

    def native_ast(cid, code_n, hue):
        """Bloque #AST `code_n` del BSP del nativo `cid`, con los #AME propios (origen 2) clonados
        a este BSP (texturas importadas, tono opcional) y las texturas del rayo importadas."""
        if cid not in natives:
            np_, nh_ = donor_bsp(cid)
            nt, nht = kids(np_), kids(nh_, ">")
            # texturas de sus #AME y del rayo: el #AMT con mas entradas del BSP (el de arriba en
            # Gohan adulto; Gohan nino solo lo tiene dentro del #AMB de los #ASE)
            amts = [(x, h) for (x, _), (h, _) in zip(nt, nht) if x[:4] == b"#AMT"]
            for ai in (0, 1):
                amts += [(x, h) for (x, _), (h, _) in zip(kids(nt[ai][0]), kids(nht[ai][0], ">")) if x[:4] == b"#AMT"]
            amt, azt = max(amts, key=lambda xh: struct.unpack_from("<I", xh[0], 0x10)[0])
            natives[cid] = dict(p=(kids(nt[0][0]), kids(nt[1][0])), ast=wk_blocks(kids(nt[0][0])[0][0]), azt=azt,
                                texs={tt["idx"]: tt for tt in amt_ps2.entries(amt)}, tex={}, ame={})
        nb = natives[cid]
        if code_n is None:
            return None
        src = next(x for x in nb["ast"] if u16(x, 0x12) == code_n)

        def tex_import(i):
            if i not in nb["texs"]:
                return i
            if i not in nb["tex"]:
                img = azt_image(nb["azt"], i)
                if hue is not None and img_saturated(img):
                    img = hue_image(img, hue)
                extra_tex.append((img, tex_tpl, (nb["texs"][i]["w"], nb["texs"][i]["h"])))
                nb["tex"][i] = ntex + len(extra_tex) - 1
            return nb["tex"][i]

        def ame_import(amb, i):
            """origen 2 = hijo del #AMB de los #AST (amb 0), origen 1 = del de los #ASE (amb 1)."""
            if (amb, i) not in nb["ame"]:
                a = nb["p"][amb][i][0]
                lst, base = (new_ame0, len(p0)) if amb == 0 else (new_ame1, len(p1))
                lst.append(ame_recolor(a, hue, tex_import) if hue is not None else ame_retex(a, tex_import))
                nb["ame"][(amb, i)] = base + len(lst) - 1
            return nb["ame"][(amb, i)]

        b = bytearray(src)
        # +0x20/+0x22 = de donde salen las texturas del rayo: 2 el BSP (casi todos), 1 el #AMB de
        # los #ASE (Gohan nino). Aqui van en el #AMT grande del hibrido (prueba en juego
        # 2026-10-08: con el 1 de Gohan nino el Masenko leia una textura inexistente y colgaba)
        b[0x20:0x24] = tex_src
        for o in AST_LINKS:                          # (origen, indice); (0, 0) = sin enlace
            if u16(b, o) in (1, 2):
                put16(b, o + 2, ame_import(2 - u16(b, o), u16(b, o + 2)))
            elif u16(b, o + 2) and o == 0x68:        # 2o enlace de la bola al banco comun: el nucleo
                b[0x64:0x6C] = b[0x58:0x60]
            elif u16(b, o + 2) and plantilla:        # plantilla prestada: sin ese efecto de impacto
                put16(b, o + 2, 0)
            elif u16(b, o + 2):
                # el banco comun (origen 0) colgaba el juego desde un BSP hibrido (Masenko de Gohan
                # nino, prueba 2026-10-08): esas tecnicas no se copian
                raise ValueError("AST %#x del ID %d: enlace +%#x al banco comun" % (code_n, cid, o))
        for o in (0x48, 0x4A):                       # texturas del rayo (tipo 0)
            if u16(b, o):
                put16(b, o, tex_import(u16(b, o)))
        return b

    def native_ase(cid, pick):
        """#ASE del nativo `cid` que cumple `pick`, con sus #AME (origen 1/2) importados; los
        enlaces al banco comun se quitan (plantilla prestada)."""
        native_ast(cid, None, None) if cid not in natives else None
        nb = natives[cid]
        src = next(x for x in wk_blocks(nb["p"][1][0][0]) if pick(x))
        b = bytearray(src)
        # enlace del #ASE: +0x5C (2 = hijo de su propio #AMB, el de los #ASE) -> +0x5E; +0x50..+0x5A
        # son iguales en todos los nativos (como en build_bsp, se copian tal cual)
        if u16(b, 0x5C) == 2:
            new_ame1.append(ame_retex(nb["p"][1][u16(b, 0x5E)][0], lambda i: tex_of(cid, i)))
            put16(b, 0x5E, len(p1) + len(new_ame1) - 1)
        return b

    def tex_of(cid, i):
        nb = natives[cid]
        if i not in nb["texs"]:
            return i
        if i not in nb["tex"]:
            extra_tex.append((azt_image(nb["azt"], i), tex_tpl, (nb["texs"][i]["w"], nb["texs"][i]["h"])))
            nb["tex"][i] = ntex + len(extra_tex) - 1
        return nb["tex"][i]

    # plantillas: rayo (tipo 0; los 0x15E+ son el aspecto de las rafagas de ki, no valen) y #ASE
    # de carga (inicio/medio/fin, la del Kamehameha) y destello. Si el donante no las tiene
    # (Trunks, Piccolo, Cell, Krilin, Gotenks...: auditoria 2026-10-08) se toman las de Gohan
    # adulto (ID 4) con sus #AME y texturas
    TPL_ID = 4
    plantilla = True
    beam_tpl = next((b for b in ast_t if u16(b, 0x10) == 0 and u16(b, 0x12) not in RESERVED), None)
    if beam_tpl is None:
        beam_tpl = native_ast(TPL_ID, 0, None)
        rep.append("plantilla del rayo: la de Gohan adulto (el donante no tiene rayo propio)")
    charge = lambda b: u16(b, 0x6C) and u16(b, 0x5C) == 2              # noqa: E731
    shot = lambda b: not u16(b, 0x6C) and not u16(b, 0x6E) and u16(b, 0x5C) == 2   # noqa: E731
    charge_tpl = next((b for b in ase_t if charge(b)), None)
    if charge_tpl is None:
        charge_tpl = native_ase(TPL_ID, charge)
        rep.append("plantilla de la carga: la de Gohan adulto")
    shot_tpl = next((b for b in ase_t if shot(b)), None)
    if shot_tpl is None:
        shot_tpl = native_ase(TPL_ID, shot)
        rep.append("plantilla del destello: la de Gohan adulto")
    plantilla = False
    head_ame, tail_ame = u16(beam_tpl, 0x5E), u16(beam_tpl, 0x36)
    hit_link = bytes(beam_tpl[0x70:0x7C]), bytes(beam_tpl[0xAC:0xB8])

    free_ase = [c for c in ASE_NEW if c not in own]
    # Los #AST nuevos van en 1-4 (los 38 nativos solo usan 0-4; la Z Sword con el 6 no lanzaba
    # nada, prueba 2026-10-08). Si los #ASE del donante ocupan esos codigos (el 4/5 de la carga
    # del Kamehameha de Gohan adulto), se renumeran y aplicar() cambia sus lineas c4.
    need = sum(1 for t in techs for c in t["ast"]
               if not receta.get(t["codigo"], {}).get("omitir") and not receta.get(t["codigo"], {}).get("definitivo")
               and c not in receta.get(t["codigo"], {}).get("donante", {}))
    ast_codes = {u16(b, 0x12) for b in ast_t}
    ase_remap = {}
    for c in AST_LOW:
        if len([x for x in AST_LOW if x not in own]) >= need:
            break
        if c in own and c not in ast_codes:
            ase_remap[c] = free_ase.pop(0)
            own = (own - {c}) | {ase_remap[c]}
    for b in ase_t:
        b[0x6A:0x70] = struct.pack("<3H", *(ase_remap.get(x, x) for x in struct.unpack_from("<3H", b, 0x6A)))
    if ase_remap:
        rep.append("ASE del donante renumerados para dejar los AST en 1-4: %s" % {hex(k): hex(v) for k, v in ase_remap.items()})
    free_ast = [c for c in AST_LOW if c not in own] + [c for c in AST_NEW if c not in own and c not in AST_LOW]
    mapas = {"_ase_remap": ase_remap}
    for t in sorted(techs, key=lambda t: not receta.get(t["codigo"], {}).get("equipada", False)):
        r = receta.get(t["codigo"], {})
        if r.get("omitir"):
            rep.append("%#x %s: omitida (%s)" % (t["codigo"], r.get("nombre", ""), r["omitir"]))
            mapas[t["codigo"]] = {}
            continue
        if r.get("definitivo"):
            rep.append("%#x %s: definitivo cinematico del donante (sin BSP propio)" % (t["codigo"], r.get("nombre", "")))
            mapas[t["codigo"]] = {}
            continue
        hue = r.get("tono")
        m = {0x10: 0x10}
        m.update({k: ase_remap.get(v, v) for k, v in r.get("donante", {}).items()})
        # energia (#AST)
        for c in t["ast"]:
            if c in m:
                continue
            sbk = sbp["ast"][c]
            if r.get("ast_nativo"):
                # la energia de una tecnica nativa de B3 (rayo, disco...) con sus #AME y texturas;
                # de SB solo el punto de salida y el dano
                cid, code_n = r["ast_nativo"]
                b = native_ast(cid, code_n, hue)
                new = free_ast.pop(0)
                put16(b, 0x12, new)
                put16(b, 0xDE, 7)
                put16(b, 0x2C, u16(sbk, 0x2C))
                put16(b, 0xC8, r.get("dano", int(round(u16(sbk, 0xC8) * DMG_AST))))
                ast_t.append(b)
                m[c] = new
                rep.append("%#x %s: AST SB %#x -> %#x = AST %#x del ID %d (tipo %d, dano %d)%s" % (
                    t["codigo"], r.get("nombre", ""), c, new, code_n, cid, u16(b, 0x10), u16(b, 0xC8),
                    "" if hue is None else ", tono %d" % hue))
                continue
            typ = r.get("tipo", u16(sbk, 0x10))
            if typ != u16(sbk, 0x10):           # otro tipo: el rayo del donante con los datos de SB
                b = bytearray(beam_tpl)
                for o, n in ((0x24, 4), (0x28, 4), (0x2C, 2), (0xC8, 2), (0xCC, 4), (0xD4, 2)):
                    b[o:o + n] = sbk[o:o + n]
                b[0x10:0x12] = struct.pack("<H", typ)
            else:
                b = bytearray(sbk)
                for o in AST_ZERO:
                    put16(b, o, 0)
            new = free_ast.pop(0)
            put16(b, 0x12, new)
            put16(b, 0xDE, 7)
            put16(b, 0xC8, r.get("dano", int(round(u16(sbk, 0xC8) * DMG_AST))))
            if "radio" in r:
                struct.pack_into("<f", b, 0xCC, r["radio"])
            # +0x20/+0x22 = (1, origen) como los demas enlaces: SB pone 8 (su AMB), B3 2 o 1;
            # con el 8 de SB la bola no salia y el personaje se desplazaba (prueba en juego 2)
            b[0x20:0x24] = tex_src
            # enlaces a #AME: patron de B3 por tipo, con los efectos del donante (recoloreados)
            for o in range(0x30, 0x38, 2):
                put16(b, o, 0)
            for o in range(0x58, 0x60, 2):
                put16(b, o, 0)
            head = clone(0, head_ame, hue)
            if typ == 0:
                b[0x30:0x38] = struct.pack("<4H", 2, 2, 2, tail_ame)
                b[0x58:0x60] = struct.pack("<4H", 2, 2, 2, head)
            elif typ == 3:
                b[0x58:0x60] = struct.pack("<4H", 2, 1, 2, head)
            else:
                b[0x58:0x60] = struct.pack("<4H", 2, 2, 2, head)
            if typ == 1:
                # bola: los 22 nativos enlazan DOS #AME (+0x58 nucleo, +0x64 el siguiente) y llevan
                # 9 en +0xC0. El 2o es el nucleo otra vez: la cola del rayo del donante es una estela
                # de particulas blancas con texturas azules que no se recolorea (prueba 6: "azul")
                for o in range(0x60, 0x6C, 2):
                    put16(b, o, 0)
                b[0x64:0x6C] = struct.pack("<4H", 2, 2, 2, head if hue is not None else clone(0, tail_ame, hue))
                put16(b, 0xC0, 9)
            b[0x70:0x7C], b[0xAC:0xB8] = hit_link
            for o in (0x78, 0xB4):              # impacto: estela del rayo del donante, tambien teñida
                if u16(b, o) == 2:
                    put16(b, o + 2, clone(0, u16(b, o + 2), hue))
            # texturas del rayo: las de SB (importadas) o las del donante
            if typ == 0:
                t0, t1 = u16(sbk, 0x48), u16(sbk, 0x4A)
                if t0 and sb_amt is not None and r.get("texturas_sb", True):
                    ids = []
                    for ti in (t0, t1):
                        img = psp_tex(sb_amt, ti)
                        extra_tex.append((img, tex_tpl, (img.shape[1], img.shape[0])))
                        ids.append(ntex + len(extra_tex) - 1)
                    b[0x48:0x4C] = struct.pack("<2H", *ids)
                else:
                    b[0x48:0x4C] = struct.pack("<2H", *(tex_hue(u16(beam_tpl, o), hue) if hue is not None
                                                        else u16(beam_tpl, o) for o in (0x48, 0x4A)))
            else:
                b[0x48:0x4C] = bytes(4)
            ast_t.append(b)
            m[c] = new
            rep.append("%#x %s: AST SB %#x (tipo %d) -> %#x tipo %d, dano %d, hueso %d, efecto %s" % (
                t["codigo"], r.get("nombre", ""), c, u16(sbk, 0x10), new, typ, u16(b, 0xC8), u16(b, 0x2C),
                "donante" if hue is None else "tono %d" % hue))
        # efectos visuales (#ASE): plantilla del donante (carga o destello), hueso y codigos de SB
        for c in t["ase"]:
            if c in m:
                continue
            m[c] = free_ase.pop(0)
        for c in t["ase"]:
            sbk = sbp["ase"][c]
            if c in r.get("donante", {}):
                continue
            ends = struct.unpack_from("<3H", sbk, 0x6A)
            tpl = charge_tpl if ends[2] else shot_tpl
            b = bytearray(tpl)
            put16(b, 0x80, u16(sbk, 0x48))
            put16(b, 0x5E, clone(1, u16(tpl, 0x5E), hue))
            b[0x6A:0x70] = struct.pack("<3H", *(m.get(x, 0x10) if x else 0 for x in ends))   # 0 = ninguno
            ase_t.append(b)
            rep.append("   ASE SB %#x -> %#x (%s, hueso %d, codigos %s)" % (
                c, m[c], "carga" if ends[2] else "destello", u16(b, 0x80),
                "/".join("%#x" % x for x in struct.unpack_from("<3H", b, 0x6A))))
        mapas[t["codigo"]] = m
        if t["otros"]:
            rep.append("   %#x: efectos de SB sin equivalente (se quitan): %s" % (
                t["codigo"], [hex(x) for x in t["otros"] if x not in m]))
    # montar: PS2 (tablas, #AME clonados) -> HD; el resto de hijos HD del donante tal cual
    p_ast = wk_table(p0[0][0], ast_t)
    p_ase = wk_table(p1[0][0], ase_t)
    hd0 = [(ps2hd.conv_ast(p_ast), h0[0][1])] + h0[1:] + [(ps2hd.conv_u32_names(a), 0xFFFFFFFF) for a in new_ame0]
    hd1 = [(ps2hd.conv_ase(p_ase), h1[0][1])] + h1[1:] + [(ps2hd.conv_u32_names(a), 0xFFFFFFFF) for a in new_ame1]
    top = list(htop)
    if extra_tex:
        z, ids = azt_append(hazt, extra_tex)
        assert ids[0] == ntex
        where = top if big[0] is None else (hd0, hd1)[big[0]]
        where[big[1]] = (z, where[big[1]][1])
    top[0] = (amb_hd(hd0), htop[0][1])
    top[1] = (amb_hd(hd1), htop[1][1])
    out = amb_hd(top)
    rep.append("BSP: %d AST (+%d), %d ASE (+%d), #AME clonados %d+%d, texturas +%d (%d en total)" % (
        len(ast_t), len(ast_t) - len(wk_blocks(p0[0][0])), len(ase_t), len(ase_t) - len(wk_blocks(p1[0][0])),
        len(new_ame0), len(new_ame1), len(extra_tex), ntex + len(extra_tex)))
    return out, mapas, rep, techs


# ---------------------------------------------------------------- nombres oficiales (texturas)
DIR_ORDER = {1: 0, 2: 1, 0x10: 2}      # >E, <E, ^E: orden de las listas de SB


def nombres(code, game="sb2", out_dir=None, bc=None):
    """Rotulos de combate de SB (data_btl_us bf<xxx>.amt = forma normal, bf<xxx>m1..m3 = el
    personaje con su forma k): texturas 0-1 caras, 2 transformacion, 3-5 tecnicas de la forma
    normal (>E, <E, ^E), 6-8 las de la forma k, 11-14 boosters. El booster del BCM de SB es la
    mascara de formas: bit 0 -> lista base, bit k -> ranuras 6-8 de bf<xxx>m<k>.
    Devuelve {codigo SB: (fichero, textura)} y guarda <codigo>.png (y todas las texturas)."""
    from PIL import Image  # noqa: PLC0415
    a = SbAfs(game, "data_btl_us.afs")
    bc = bc or sb_sources(game, code)[0]
    bcm = next(x for x, t in kids(bc) if x[:4] == b"#BCM")
    techs = {}
    for e in sb_bcm(bcm):
        if e["cond2"] & 0x8000 or e["dir"] not in DIR_ORDER:
            continue
        techs.setdefault(e["codigos"][0], e)
    out = {}
    for k in range(4):
        name = "bf%s%s.amt" % (code.lower(), "m%d" % k if k else "")
        if name.upper() not in a.names:
            continue
        amt = a.get(name)
        for c, e in techs.items():
            if k == 0 and (e["booster"] & 1 or not e["booster"]):
                out.setdefault(c, (name, 3 + DIR_ORDER[e["dir"]]))
            elif k and e["booster"] & (1 << k) and not e["booster"] & 1:
                out.setdefault(c, (name, 6 + DIR_ORDER[e["dir"]]))
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
            for c, (fn, i) in out.items():
                if fn == name:
                    Image.fromarray(psp_tex(amt, i)).save(os.path.join(out_dir, "%03x.png" % c))
    return out


# ---------------------------------------------------------------- moveset: tecnicas sobre el de sbport
# capsulas provisionales del BCM, una por tecnica (`reemplaza` del [[capsula]] las cambia por
# las nuevas). 26/28 = Kamehameha / Super Kamehameha del donante; 589-594 son registros del
# catalogo sin dueno: si una no se reemplaza, su golpe no sale (no hereda nada ajeno).
PLACEHOLDER = {"Kamehameha": 26, "Super Kamehameha": 28, "Special Beam Cannon": 589, "Spirit Shot": 590,
               "Evasive Kick": 591, "Masenko": 592, "Z Sword": 593, "Burning Attack": 594}
# efectos c4 comunes del motor (los usan los nativos sin tenerlos en su BSP)
B3_COMMON_C4 = {0x1, 0x5, 0x6, 0x7, 0xc, 0x21, 0x2b, 0x2c, 0x30, 0x4b, 0x64, 0x82, 0x83, 0x84, 0x85, 0x92,
                0x95, 0x96, 0xb4, 0xb6, 0xc6, 0xc7, 0xc9, 0xe6, 0xe7, 0xe8} | set(range(0x12C, 0x13E))
LINE_GROUP = {0: 0, 1: 1, 2: 1, 3: 1, 4: 2}


def ap7_line(frame, cat, val, ident=0):
    return struct.pack("<HBBII", frame, ident, 2, cat, val) + bytes(4)


def ap7_sorted(lines):
    """Lineas AP7 por frame, con ids unicos 0..n-1 asignados por grupo (como los nativos)."""
    lines = sorted(lines, key=lambda x: struct.unpack_from("<H", x, 0)[0])
    order = sorted(range(len(lines)), key=lambda i: (LINE_GROUP.get(struct.unpack_from("<I", lines[i], 4)[0], 3),
                                                    struct.unpack_from("<H", lines[i], 0)[0], i))
    out = [bytearray(x) for x in lines]
    for ident, i in enumerate(order):
        out[i][2] = ident
    return [bytes(x) for x in out]


def bsk_aps(bsk, code):
    """[(tipo, [lineas 16 B])] del (unico) sub-bloque del codigo (PS2)."""
    import b1port as bp  # noqa: PLC0415
    a = bp.bsk_code_list(bsk)[code]
    _, nap, apo = struct.unpack_from("<3I", bsk, a + 0x24)
    out = []
    for k in range(nap):
        t, nl, do = struct.unpack_from("<HHI", bsk, apo + 8 * k)
        out.append((t, [bytes(bsk[do + 16 * i:do + 16 * i + 16]) for i in range(nl)]))
    return out


def bsk_put(bsk, items, new_hr=()):
    """BSK (PS2) con los codigos `items` {codigo: (sub-bloque 48 B o None = el actual, aps)}
    escritos de nuevo al final, cada uno con su tabla AP (el juego reubica una tabla por cada
    bloque que la apunta: nunca se comparten) y un cierre 0xFF. La seccion HR sigue al final."""
    import b1port as bp  # noqa: PLC0415
    n, lst, nhr, hr = bp.bsk_head(bsk)
    L = bp.bsk_code_list(bsk)
    out = bytearray(bsk[:hr])
    for code, (blk, aps) in sorted(items.items()):
        blk = bytearray(blk if blk is not None else bsk[L[code]:L[code] + 48])
        tab = []
        for t, lines in aps:
            out += bytes((-len(out)) % 16)
            tab.append((t, len(lines), len(out)))
            out += b"".join(lines)
        out += bytes((-len(out)) % 16)
        apo = len(out)
        for e in tab:
            out += struct.pack("<HHI", *e)
        out += bytes((-len(out)) % 16)
        struct.pack_into("<3I", blk, 0x24, 0, len(tab), apo if tab else 0)
        struct.pack_into("<I", out, lst + 4 * code, len(out))
        out += blk + b"\xff" * 48
    out += bytes((-len(out)) % 16)
    new_hr_at = len(out)
    out += bsk[hr:hr + 128 * nhr] + b"".join(new_hr)
    struct.pack_into("<4I", out, 0x10, n, lst, nhr + len(new_hr), new_hr_at)
    return bytes(out)


def tech_lines(sb_ls, m, aps, receta, donor_aps=None, shift=0, hits=(), freeze=True):
    """AP7 nuevas de una tecnica: las de A sin las clases 0 y 4 + las clases 0/4 de SB (o 0/4/6
    de una plantilla del donante, desplazadas) con los codigos del BSP hibrido + congelacion
    (0x64 en el frame 8 y 0x66 11 frames antes de soltar la energia, como los nativos)."""
    from sb_tablas import B3_C0, T7  # noqa: PLC0415
    from sb_voces import K3  # noqa: PLC0415   (agente F: tono de SB -> hueco del banco de gritos)
    drop = (0, 4, 3) if sb_ls is not None else (0, 4)
    keep = [x for t, ls in aps if t == 7 for x in ls if struct.unpack_from("<I", x, 4)[0] not in drop]
    new = []
    if sb_ls is not None:
        # gritos (clase 3): < 0x40 = tono del banco del personaje (sb_voces.K3, el de su banco de
        # B3); >= 0x40 = banco comun de SB (tabla por mayoria de sbport, sb_tablas.T7[3])
        for f, cat, v in sb_ls:
            if cat != 3 or receta.get("voz"):     # la receta pone sus gritos (y quita los de SB)
                continue
            nv = K3.get(v) if v < 0x40 else T7[3].get(v)
            if nv is not None:
                new.append(ap7_line(f, 3, nv))
        for f, v in receta.get("voz", ()):
            new.append(ap7_line(f, 3, v))
    if donor_aps is not None:
        for x in (x for t, ls in donor_aps if t == 7 for x in ls):
            cat = struct.unpack_from("<I", x, 4)[0]
            if cat in (0, 4, 6):
                f = max(0, struct.unpack_from("<H", x, 0)[0] + shift)
                new.append(ap7_line(f, cat, struct.unpack_from("<I", x, 8)[0]))
        keep = [x for x in keep if struct.unpack_from("<I", x, 4)[0] != 6]
    else:
        for f, cat, v in sb_ls or ():
            if cat == 4 and v in m:
                new.append(ap7_line(f, 4, m[v]))
            elif cat == 0 and v in B3_C0:
                new.append(ap7_line(f, 0, {0x67: 0x64}.get(v, v)))     # congelacion de definitivo SB
    for f, cat, v in receta.get("extra", ()):
        new.append(ap7_line(f, cat, v))
    c0 = [struct.unpack_from("<I", x, 8)[0] for x in new if struct.unpack_from("<I", x, 4)[0] == 0]
    rel = [struct.unpack_from("<H", x, 0)[0] for x in new
           if struct.unpack_from("<I", x, 4)[0] == 4 and struct.unpack_from("<I", x, 8)[0] in receta.get("_ast", ())]
    if freeze and 0x64 not in c0:
        new.append(ap7_line(8, 0, 0x64))
    if freeze and 0x66 not in c0:
        r = min(rel) - FREEZE_BEFORE if rel else (min(hits) - 6 if hits else 20)
        new.append(ap7_line(max(9, r), 0, 0x66))
    return ap7_sorted(keep + new)


def hit_frames(aps):
    return [struct.unpack_from("<H", x, 0)[0] for t, ls in aps if t == 1 for x in ls
            if struct.unpack_from("<H", x, 4)[0] != 0xFFFF]


def set_ap7(aps, lines):
    return sorted([(t, ls) for t, ls in aps if t != 7] + [(7, lines)], key=lambda e: e[0])


def waist_hold(amm, anim, frame):
    """Pista de posicion de la cintura (WAIST, la que mueve al personaje) de la animacion `anim`
    del AMM (PS2): desde `frame` se queda donde estaba (sin el retroceso de SB al disparar).
    Claves de 16 B [x, y, z f32][frame u32]; la ultima lleva frame 0 (= el final)."""
    n, t, nb, no = struct.unpack_from("<4I", amm, 0x10)
    flags, _, _, off = struct.unpack_from("<4I", amm, t + 16 * anim)
    per = 3 if flags & 0x10 else 2
    for j in range(nb):
        if not amm[no + 32 * j:no + 32 * j + 32].split(b"\0")[0].endswith(b"WAIST"):
            continue
        pp = struct.unpack_from("<I", amm, off + 4 * per * j + 4)[0]
        k = struct.unpack_from("<I", amm, pp + 8)[0] if pp else 0
        keys = [pp + 16 + 16 * i for i in range(k)]
        before = [o for i, o in enumerate(keys) if i < k - 1 and struct.unpack_from("<I", amm, o + 12)[0] <= frame]
        if not before:
            return
        z = struct.unpack_from("<f", amm, before[-1] + 8)[0]
        for o in keys[keys.index(before[-1]) + 1:]:
            struct.pack_into("<f", amm, o + 8, z)
        return


ULT_RECETA = {}      # la ultima definitiva de SB traducida (receta de cinematica.py; aplicar la rellena)


def receta_ult_sb(anm, bsk, code_b3, donor):
    """Animacion de la definitiva de SB (la de su codigo raiz en el moveset portado, numeracion
    de SB) repartida sobre la version 'gana' de la cinematica del donante, a su velocidad
    (b1port.receta_definitiva). {} si no se puede."""
    import afs_pair  # noqa: PLC0415
    import b1port as bp  # noqa: PLC0415
    L = bp.bsk_code_list(bsk)
    if code_b3 is None or code_b3 >= len(L) or not L[code_b3]:
        return {}
    an, bank = struct.unpack_from("<HH", bsk, L[code_b3])
    amm = bp.Amm(bp.amb_kids(anm)[1][0])
    nf = amm.anims[an][2] if bank == 3 and an < len(amm.anims) else 0
    dk = bp.amb_kids(afs_pair.ps2(donor_ids(donor)["anm"][0]))
    dspx = next((x for x, t in bp.amb_kids(afs_pair.ps2(donor_ids(donor)["cam"])) if x[:4] == b"#SPX"), None)
    win = bp.cine_win(dk[0][0], bp.Amm(dk[1][0]), dspx)
    if nf < 2 or not win or sum(f for _, f in win) < nf / 2:
        return {}
    return bp.receta_definitiva([(an, 0, nf - 1)], win)


def aplicar(anm, cam, info, code, game="sb2", donor=4, receta=None, mapas=None, sources=None):
    """Tecnicas sobre el moveset de sbport (PS2): lineas AP7 de cada especial con el BSP hibrido,
    congelacion, beam struggle del Kamehameha (bit 0x2000 + respuesta cond2 0x4003), definitivo
    cinematico del donante (0x25A -> SPX 0 -> 0x4A0+), la onda de SB como especial y capsulas
    provisionales por tecnica. info = sbport.json de A. -> (anm, cam, informe)."""
    import afs_pair  # noqa: PLC0415
    import b1port as bp  # noqa: PLC0415
    bc, bsp = sources or sb_sources(game, code)
    techs, sbp = sb_tecnicas(bc, bsp)
    receta = receta_de(code, techs, receta, donor)
    caps = placeholders(techs, receta)
    bsk_sb = next(x for x, t in kids(bc) if x[:4] == b"#BSK")
    cmap = {int(k, 16): int(v, 16) for k, v in info["codigos"].items()}
    ak, ck = bp.amb_kids(anm), bp.amb_kids(cam)
    bsk = ak[0][0]
    bcm = next(x for x, t in ck if x[:4] == b"#BCM")
    de = donor_ids(donor)
    d_bsk = bp.amb_kids(afs_pair.ps2(de["anm"][0]))[0][0]
    d_bcm = next(x for x, t in bp.amb_kids(afs_pair.ps2(de["cam"])) if x[:4] == b"#BCM")
    d_st, d_bl = bp.bcm_parse(d_bcm)
    d_ult = donor_ult(donor)
    d_resp = next((bp.w16(d_bl[o][0], 12) for o in d_st if bp.w16(d_bl[o][0], 6) == 0x4003), None)   # Trunks: sin respuesta
    L = bp.bsk_code_list(bsk)
    used = {c for c, a in enumerate(L) if a}
    st, bl = bp.bcm_parse(bcm)
    nodes = {o: [bytearray(b), list(k)] for o, (b, k) in bl.items()}
    by_code = {}
    for o in nodes:
        by_code.setdefault(bp.w16(nodes[o][0], 12), []).append(o)
    order = list(st)
    rep, items, new_hr = [], {}, []
    moved = {}                  # codigo B3 -> el de la zona de especiales
    final = {}                  # codigo SB -> [codigos B3 del suelo, aire y variantes] ya movidos
    nhr, hr = bp.bsk_head(bsk)[2:]
    amm, held = bytearray(ak[1][0]), set()

    def hit_dmg(x, dmg):
        """Linea de golpe con su bloque HR copiado y el dano cambiado (el HR puede ser compartido)."""
        h = struct.unpack_from("<H", x, 4)[0]
        if h == 0xFFFF or h >= nhr:
            return x
        blk = bytearray(bsk[hr + 128 * h:hr + 128 * h + 128])
        for ln in range(8):
            if struct.unpack_from("<H", blk, 16 * ln)[0]:
                struct.pack_into("<H", blk, 16 * ln, dmg)
        if bytes(blk) not in new_hr:
            new_hr.append(bytes(blk))
        out = bytearray(x)
        struct.pack_into("<H", out, 4, nhr + new_hr.index(bytes(blk)))
        return bytes(out)

    for t in techs:
        c = t["codigo"]
        r = dict(receta.get(c, {}))
        name = r.get("nombre", "%#x" % c)
        air = t["entrada"]["codigos"][1] if len(t["entrada"]["codigos"]) > 1 else c + 0x200
        g, a = cmap.get(c), cmap.get(air)
        if g is None:
            rep.append("aviso: %s (SB %#x) no esta en el moveset de A" % (name, c))
            continue
        cap = caps[c]
        ents = by_code.get(g, []) + (by_code.get(d_ult, []) if r.get("definitivo") else [])
        if r.get("omitir"):
            order[:] = [o for o in order if o not in ents]
            rep.append("%s: entrada quitada del BCM (%s)" % (name, r["omitir"]))
            continue
        if r.get("definitivo"):
            for o in ents:
                b = nodes[o][0]
                for i, v in zip((12, 13, 14), (d_ult, d_ult + 0x100, d_ult + 0x100)):
                    bp.set16(b, i, v)
                bp.set16(b, 8, cap)
            for code_b3 in (g, a):      # su bloque de SB queda sin entrada: fuera sus enlaces de SB
                if code_b3 is not None and L[code_b3]:
                    aps = bsk_aps(bsk, code_b3)
                    items[code_b3] = (None, [(tt, [x for x in ls if tt != 7 or struct.unpack_from("<I", x, 4)[0] != 4])
                                             for tt, ls in aps])
            rep.append("%s: entrada P+K+G+E (modo hiper) -> cinematica del donante %#x/%#x, capsula %d" % (
                name, d_ult, d_ult + 0x100, cap))
            try:                        # su animacion de SB dentro de esa cinematica
                ULT_RECETA.clear()
                ULT_RECETA.update(receta_ult_sb(anm, bsk, g, donor))
                if ULT_RECETA:
                    rep.append("%s: su animacion de SB en %d codigos de la cinematica del donante" % (
                        name, len(ULT_RECETA)))
            except Exception as ex:  # noqa: BLE001
                rep.append("aviso: %s: definitiva con las animaciones del donante (%s)" % (name, ex))
            continue
        m = (mapas or {}).get(c, {})
        r["_ast"] = {v for k, v in m.items() if k in sbp["ast"]}
        pairs = [(g, c), (a, air)] + [(cmap.get(v), v) for v in t["variantes"]]
        for code_b3, code_sb in pairs:
            if code_b3 is None or not L[code_b3]:
                continue
            aps = bsk_aps(bsk, code_b3)
            if r.get("dano_golpe"):
                aps = [(tt, [hit_dmg(x, r["dano_golpe"]) if tt == 1 else x for x in ls]) for tt, ls in aps]
            sb_ls = sb_lines(bsk_sb, code_sb) or t["lineas"]
            if r.get("plantilla_donante"):
                dc = r["plantilla_donante"] + (0x100 if code_b3 >= 0x300 else 0)
                lines = tech_lines(sb_ls, m, aps, r, bsk_aps(d_bsk, dc), r.get("desfase", 0))
            else:
                lines = tech_lines(sb_ls, m, aps, r, hits=hit_frames(aps))
            items[code_b3] = (None, set_ap7(aps, lines))
            if r.get("sin_retroceso"):
                fire = [struct.unpack_from("<H", x, 0)[0] for tt, ls in items[code_b3][1] if tt == 7 for x in ls
                        if struct.unpack_from("<I", x, 4)[0] == 4 and struct.unpack_from("<I", x, 8)[0] in r["_ast"]]
                anim, pool = struct.unpack_from("<HH", bsk, L[code_b3])
                if fire and pool == 3 and anim not in held:
                    held.add(anim)
                    waist_hold(amm, anim, min(fire))
        # Los 38 nativos tienen TODOS sus especiales en 0x240-0x27F (+0x100 aire); en la zona de
        # golpes normales (0x23B de SB) el juego no lanzaba la energia y lo desplazaba (prueba 3)
        for gb in [x for x, _ in pairs if x is not None and x < 0x300 and x in items and not 0x240 <= x < 0x280]:
            rg = next(x for x in range(0x240, 0x280) if x not in used and x + 0x100 not in used
                      and x not in bp.ENGINE and x + 0x100 not in bp.ENGINE)
            used.update((rg, rg + 0x100))
            remap = {gb: rg, gb + 0x100: rg + 0x100}
            moved.update(remap)
            for src, dst in remap.items():
                if src in items:
                    items[dst] = (bytes(bsk[L[src]:L[src] + 48]), items.pop(src)[1])
            for nd in nodes.values():
                for i in (12, 13, 14):
                    if bp.w16(nd[0], i) in remap:
                        bp.set16(nd[0], i, remap[bp.w16(nd[0], i)])
            g, a = remap.get(g, g), remap.get(a, a) if a is not None else None
            rep.append("%s: codigo %#x -> %#x (zona de especiales)" % (name, gb, rg))
        for o in ents:
            b = nodes[o][0]
            bp.set16(b, 8, cap)
            if r.get("especial") and bp.w16(b, 4) & 0x8:      # definitivo en SB -> especial
                for i, v in ((0, r["especial"]), (1, 8), (4, 0x0002), (5, 0), (6, 0x0001)):
                    bp.set16(b, i, v)
                if o in order:
                    order.remove(o)
                    k = next((k for k, x in enumerate(order) if bp.w16(nodes[x][0], 1) == 8
                              and bp.w16(nodes[x][0], 8) and bp.w16(nodes[x][0], 0) == r["especial"]), len(order))
                    order.insert(k, o)
                rep.append("%s: definitivo en SB -> especial %sE (delante de los de esa direccion)" % (
                    name, {1: "->", 2: "<-"}[r["especial"]]))
        final[c] = [moved.get(x, x) for x, _ in pairs]
        rep.append("%s: codigos %#x/%s, capsula provisional %d, BSP %s%s" % (
            name, g, hex(a) if a else "-", cap, {hex(k): hex(v) for k, v in m.items() if k != 0x10},
            ", golpe con dano %d" % r["dano_golpe"] if r.get("dano_golpe") else ""))
        if r.get("beam_struggle") and d_resp is not None:
            rg = next(x for x in range(0x260, 0x280) if x not in used and x + 0x100 not in used
                      and x not in bp.ENGINE and x + 0x100 not in bp.ENGINE)
            used.update((rg, rg + 0x100))
            for src, dst in ((g, rg), (a, rg + 0x100)):
                if src is None or src not in items:
                    continue
                aps = items[src][1]
                dc = d_resp + (0x100 if dst >= 0x300 else 0)
                lines = tech_lines(None, m, aps, r, bsk_aps(d_bsk, dc), r.get("desfase", 0), freeze=False)
                items[dst] = (bytes(bsk[L[src]:L[src] + 48]), set_ap7(aps, lines))
            for o in [x for x in ents if x in order]:
                b = nodes[o][0]
                bp.set16(b, 4, bp.w16(b, 4) | 0x2000)
                nb = bytearray(b)
                bp.set16(nb, 4, bp.w16(b, 4) & ~0x2000)
                bp.set16(nb, 6, 0x4003)
                for i, v in zip((12, 13, 14), (rg, rg + 0x100, rg + 0x100)):
                    bp.set16(nb, i, v)
                nodes[("resp", o)] = [nb, []]
                order.insert(order.index(o) + 1, ("resp", o))
            rep.append("%s: beam struggle (cond 0x2000 + c0 0x68) y respuesta cond2 0x4003 en %#x/%#x" % (
                name, rg, rg + 0x100))
    # resto de golpes: enlaces c4 que no existen en el BSP hibrido ni son comunes -> fuera
    known = set(B3_COMMON_C4) | {0x10}
    known |= {u16(b, 0x12) for b in wk_blocks(kids(kids(donor_bsp(donor)[0])[0][0])[0][0])}
    known |= {c for b in wk_blocks(kids(kids(donor_bsp(donor)[0])[1][0])[0][0]) for c in struct.unpack_from("<3H", b, 0x6A)}
    known |= {v for mm in (mapas or {}).values() for v in mm.values()}
    dropped = {}
    for code_b3, addr in enumerate(L):
        if not addr or code_b3 in items:
            continue
        aps = bsk_aps(bsk, code_b3)
        bad = [x for tt, ls in aps if tt == 7 for x in ls
               if struct.unpack_from("<I", x, 4)[0] == 4 and struct.unpack_from("<I", x, 8)[0] not in known]
        if bad:
            for x in bad:
                dropped.setdefault(hex(struct.unpack_from("<I", x, 8)[0]), []).append(hex(code_b3))
            items[code_b3] = (None, [(tt, [x for x in ls if x not in bad]) for tt, ls in aps])
    if dropped:
        rep.append("otros golpes: enlaces c4 de SB sin efecto en el BSP hibrido, quitados: %s" % dropped)
    # #ASE del donante renumerados en build_bsp: sus lineas c4 en todo el moveset
    remap = (mapas or {}).get("_ase_remap", {})
    if remap:
        n = 0
        # tambien los codigos nuevos (respuesta del beam struggle, especiales movidos)
        for code_b3 in sorted({c for c, x in enumerate(L) if x} | set(items)):
            blk, aps = items.get(code_b3, (None, None))
            aps = aps if aps is not None else bsk_aps(bsk, code_b3)
            new = []
            for tt, ls in aps:
                out = []
                for x in ls:
                    if tt == 7 and struct.unpack_from("<I", x, 4)[0] == 4 and struct.unpack_from("<I", x, 8)[0] in remap:
                        x = x[:8] + struct.pack("<I", remap[struct.unpack_from("<I", x, 8)[0]]) + x[12:]
                        n += 1
                    out.append(x)
                new.append((tt, out))
            if new != aps:
                items[code_b3] = (blk, new)
        rep.append("lineas c4 de los ASE renumerados: %d" % n)
    # tecnicas que evolucionan: una entrada (la principal) y, en el moveset de cada forma, sus
    # codigos con el bloque (animacion + lineas) de la tecnica de esa forma
    chains = [(cadena_principal(ch, receta), ch) for ch in cadenas(techs, receta)]
    chains = [(m, [t for t in ch if t["codigo"] in final]) for m, ch in chains if m["codigo"] in final]
    for m, ch in chains:
        for t in ch:
            if t is not m:
                gone = [o for o in by_code.get(final[t["codigo"]][0], []) if o in order]
                order[:] = [o for o in order if o not in gone]
        rep.append("evoluciona con la forma (una capsula): " + " -> ".join("%s %s" % (
            receta[t["codigo"]].get("nombre"), [i + 1 for i in range(8) if t["entrada"]["booster"] >> i & 1])
            for t in sorted(ch, key=lambda t: t["entrada"]["booster"] & -t["entrada"]["booster"])))
    nforms = max([t["entrada"]["booster"].bit_length() for t in techs] + [1]) if chains else 1

    def block(code):
        blk, aps = items.get(code, (None, None))
        hdr = blk if blk is not None else bytes(bsk[L[code]:L[code] + 48])
        return hdr, (aps if aps is not None else bsk_aps(bsk, code))

    bcm2 = bp.bcm_build(bcm, order, nodes)
    cam2 = bp.amb_build([(bcm2, t) if x[:4] == b"#BCM" else (x, t) for x, t in ck])
    anms = []
    for k in range(nforms):
        it = dict(items)
        for m, ch in chains:
            v = next((t for t in ch if t["entrada"]["booster"] >> k & 1), m)
            if v is m:
                continue
            src = [x for x in final[v["codigo"]] if x is not None]
            for i, dst in enumerate(x for x in final[m["codigo"]] if x is not None):
                it[dst] = block(src[i] if i < len(src) else src[0])
        anms.append(bp.amb_build([(bsk_put(bsk, it, new_hr), ak[0][1]), (bytes(amm), ak[1][1])] + ak[2:]))
    return anms[0], cam2, rep, anms[1:]


# ---------------------------------------------------------------- comprobaciones (sin el juego)
def bsp_codes_hd(bsp):
    """{'ast': {codigo: tipo}, 'ase': {inicio}, 'errores': [...]} de un tecnicas.bin HD."""
    errs = []
    top = kids(bsp, ">")
    out = dict(ast={}, ase=set(), errores=errs)
    # el #AZT grande: arriba o dentro de un #AMB de efectos (Gotenks)
    azts = [x for x, t in top if x[:4] == b"#AZT"] + [x for i in (0, 1) for x, t in kids(top[i][0], ">") if x[:4] == b"#AZT"]
    z = max(azts, key=lambda x: len(azt_entries(x)))
    offs = azt_entries(z)
    for k, o in enumerate(offs):
        if o:
            do, dl = struct.unpack_from(">II", z, o + 0x14)
            if do + dl > len(z) or z[do:do + 4] != b"DDS ":
                errs.append("textura %d sin DDS valido" % k)
    for ai, (magic, bs, links) in enumerate(((b"#CST", 0xF0, (0x34, 0x5C, 0x78, 0xB4)), (b"#CSE", 0xD0, (0x5C,)))):
        amb = kids(top[ai][0], ">")
        tab = amb[0][0]
        if tab[:4] != magic:
            errs.append("AMB %d sin %s" % (ai, magic.decode()))
            continue
        n, st = struct.unpack_from(">II", tab, 8)
        if st + n * bs != len(tab):
            errs.append("%s: %d bloques no miden %#x" % (magic.decode(), n, bs))
        for k in range(n):
            b = tab[st + k * bs:st + (k + 1) * bs]
            for o in links:
                src, idx = struct.unpack_from(">HH", b, o)
                if src == 2 and not (idx < len(amb) and amb[idx][0][:4] == b"#ACE"):
                    errs.append("%s bloque %d: enlace +%#x al hijo %d, que no es un #ACE" % (magic.decode(), k, o, idx))
            if ai == 0:
                code = struct.unpack_from(">H", b, 0x12)[0]
                if code in out["ast"]:
                    errs.append("AST %#x repetido" % code)
                out["ast"][code] = struct.unpack_from(">H", b, 0x10)[0]
                for o in (0x48, 0x4A):
                    t = struct.unpack_from(">H", b, o)[0]
                    if t and (t >= len(offs) or not offs[t]):
                        errs.append("AST %#x: textura %d inexistente" % (code, t))
            else:
                c = struct.unpack_from(">H", b, 0x6A)[0]
                # medio/fin: lineas c4 que apagan el efecto (Dabura 0x68)
                out.setdefault("ase_fin", set()).update(set(struct.unpack_from(">2H", b, 0x6C)) - {0})
                if c in out["ase"]:
                    errs.append("ASE %#x repetido" % c)
                out["ase"].add(c)
    # particulas de todos los #ACE: textura dentro del #AZT
    def aces(d):
        for x, t in kids(d, ">"):
            if x[:4] == b"#ACE":
                yield x
            elif x[:4] == b"#AMB":
                yield from aces(x)
    bad = 0
    for a in aces(bsp):
        n, hs = struct.unpack_from(">II", a, 8)
        seen, todo = set(), [hs]
        while todo:
            o = todo.pop()
            if not o or o in seen or o + 0xA8 > len(a):
                continue
            seen.add(o)
            todo += list(struct.unpack_from(">II", a, o + 0x24))
            if struct.unpack_from(">H", a, o + 4)[0] == 2:
                cnt, tex = struct.unpack_from(">II", a, o + 0xA0)
                if cnt == 1 and tex < 0x10000 and (tex >= len(offs) or not offs[tex]):
                    bad += 1
        if len(seen) != n:
            out.setdefault("notas", []).append("#ACE con %d nodos alcanzables de %d" % (len(seen), n))
    if bad:
        errs.append("%d particulas con textura inexistente" % bad)
    return out


def comprobar(bsp, anm, cam, donor=4):
    """Errores (lista) y notas de tecnicas.bin (HD) + moveset (PS2) frente a lo que exige B3."""
    import afs_pair  # noqa: PLC0415
    import b1port as bp  # noqa: PLC0415
    from sb_tablas import B3_C0  # noqa: PLC0415
    info = bsp_codes_hd(bsp)
    de = donor_ids(donor)
    P, H = donor_bsp(donor)
    base = bsp_codes_hd(H)                       # lo que ya trae el BSP nativo del donante no cuenta
    errs = [e for e in info["errores"] if e not in base["errores"]]
    # recuentos que ya trae el nativo (Dabura: 18 particulas con textura fuera del #AZT, Buu: ASE
    # repetidos): solo es error si el hibrido tiene MAS
    def count(lst, pat):
        import re  # noqa: PLC0415
        n = [int(re.match(r"(\d+) ", e).group(1)) for e in lst if pat in e and re.match(r"\d+ ", e)]
        return sum(n) + sum(1 for e in lst if pat in e and not re.match(r"\d+ ", e))
    for pat in ("particulas con textura inexistente", "ASE ", "AST "):
        if count(info["errores"], pat) <= count(base["errores"], pat):
            errs = [e for e in errs if pat not in e or "repetido" not in e and "particulas" not in e]
    notes = sorted(set(info.get("notas", [])) - set(base.get("notas", [])))
    known = set(info["ast"]) | info["ase"] | info.get("ase_fin", set()) | B3_COMMON_C4
    dtop, top = kids(H, ">"), kids(bsp, ">")
    if len(dtop) != len(top):
        errs.append("el BSP no tiene los mismos hijos que el del donante")
    else:
        for k, ((x, t), (y, u)) in enumerate(zip(dtop, top)):
            if k > 1 and x != y and x[:4] != b"#AZT":
                errs.append("hijo %d del BSP del donante cambiado" % k)
    ak, ck = bp.amb_kids(anm), bp.amb_kids(cam)
    bsk = ak[0][0]
    n, lst, nhr, hr = bp.bsk_head(bsk)
    L = bp.bsk_code_list(bsk)
    defined = {c for c, a in enumerate(L) if a}
    # tablas AP compartidas (los nativos nunca las comparten)
    apo = {}
    for c in defined:
        nap, o = struct.unpack_from("<II", bsk, L[c] + 0x28)
        if nap and o:
            apo.setdefault(o, []).append(c)
    shared = [v for v in apo.values() if len(v) > 1]
    if shared:
        errs.append("tablas AP compartidas: %s" % [[hex(c) for c in v] for v in shared[:5]])

    def lines(c, t=7):
        return [struct.unpack_from("<HBBII", x) for tt, ls in bsk_aps(bsk, c) if tt == t for x in ls]
    bad4, bad0 = {}, {}
    for c in defined:
        for f, i, a, cat, v in lines(c):
            if cat == 4 and v not in known:
                bad4.setdefault(v, []).append(c)
            if cat == 0 and v not in B3_C0:
                bad0.setdefault(v, []).append(c)
    if bad0:
        errs.append("acciones c0 que B3 no conoce: %s" % {hex(k): [hex(c) for c in v[:4]] for k, v in bad0.items()})
    bcm = next(x for x, t in ck if x[:4] == b"#BCM")
    spx = next(x for x, t in ck if x[:4] == b"#SPX")
    st, bl = bp.bcm_parse(bcm)
    reach = set()
    for o, (b, k) in bl.items():
        for i in (12, 13, 14):
            c = bp.w16(b, i)
            if c and c not in defined:
                errs.append("el BCM pide el codigo %#x, que no existe" % c)
            reach.add(c)
    lost = {hex(k): [hex(c) for c in v[:4]] for k, v in bad4.items() if set(v) & reach}
    if lost:
        errs.append("enlaces c4 de golpes del BCM que no estan en el BSP: %s" % lost)
    other = {hex(k): [hex(c) for c in v[:4]] for k, v in bad4.items() if not set(v) & reach}
    if other:
        notes.append("c4 sin destino en codigos que el BCM no lanza (no se ejecutan): %s" % other)
    starts = [[bp.w16(bl[o][0], i) for i in range(16)] for o in st]
    hyper = next((k for k, w in enumerate(starts) if w[4] & 0x400), None)
    for k, w in enumerate(starts):
        c0 = [(f, v) for f, i, a, cat, v in lines(w[12]) if cat == 0] if w[12] in defined else []
        if w[4] & 0x2000 and not any(v == 0x68 for f, v in c0):
            errs.append("entrada %#x con beam struggle (0x2000) sin c0 0x68" % w[12])
        if w[6] == 0x4003:
            if not any(v == 0x69 for f, v in c0):
                errs.append("respuesta %#x (cond2 0x4003) sin c0 0x69" % w[12])
            if not any(x[1] == w[1] and x[0] == w[0] and x[8] == w[8] and x[4] & 0x2000 for x in starts):
                errs.append("respuesta %#x sin su entrada con beam struggle" % w[12])
        if w[8] and w[4] & 2 and w[1] == 8 and w[6] != 0x4003:
            f64 = [f for f, v in c0 if v == 0x64]
            f66 = [f for f, v in c0 if v == 0x66]
            if not f64 or not f66 or f64[0] >= f66[0]:
                errs.append("especial %#x sin congelacion 0x64 ... 0x66" % w[12])
        if w[1] == 0xF and w[4] & 0x8:
            if hyper is None or hyper > k:
                errs.append("definitivo %#x antes que la entrada del modo hiper" % w[12])
            hr_types = []
            for f, i, a, hc, *_ in [struct.unpack_from("<HBBHH", x) for tt, ls in bsk_aps(bsk, w[12]) if tt == 1 for x in ls]:
                if hc != 0xFFFF and hc < nhr:
                    hr_types += [struct.unpack_from("<HH", bsk, hr + 128 * hc + 16 * ln + 4) for ln in range(8)]
            slots = {sc for ty, sc in hr_types if ty == 3}
            span = bp.spx_slot_span(spx, min(slots)) if slots else None
            if not slots or span is None:
                errs.append("definitivo %#x sin golpe de guion (HR tipo 3) a una ranura del SPX" % w[12])
            else:
                need = {v for v in bp.spx_code_refs(spx, min(slots))} | {
                    struct.unpack_from("<H", spx, i + 2)[0] for i in range(span[0], span[1] - 3)
                    if spx[i] == 8 and spx[i + 1] == 0x20 and 0x480 <= struct.unpack_from("<H", spx, i + 2)[0] < 0x500}
                d_bsk = bp.amb_kids(afs_pair.ps2(de["anm"][0]))[0][0]
                d_def = {c for c, a in enumerate(bp.bsk_code_list(d_bsk)) if a}
                need &= d_def                  # el resto son numeros (frames, dano), no codigos
                miss = sorted(c for c in need if c not in defined)
                if miss:
                    errs.append("la cinematica (ranura %d) pide codigos que faltan: %s" % (min(slots), [hex(c) for c in miss]))
                notes.append("definitivo %#x -> SPX ranura %d, codigos de la cinematica: %s" % (
                    w[12], min(slots), sorted(hex(c) for c in need)))
    d_ak = bp.amb_kids(afs_pair.ps2(de["anm"][0]))
    if ak[2][0] != d_ak[2][0]:
        errs.append("el AMM 2 (victimas de la cinematica) no es el del donante")
    caps = sorted({w[8] for w in starts if w[8]})
    notes.append("capsulas en el BCM: %s; AST %s; ASE %s" % (
        caps, sorted(hex(c) for c in info["ast"]), sorted(hex(c) for c in info["ase"])))
    return errs, notes


# ---------------------------------------------------------------- [[capsula]] propuesto
def capsulas_toml(code, techs, receta=None, donor=None):
    """Bloque [[capsula]] (una por tecnica, en el orden del BCM de SB; el definitivo al final) con
    `reemplaza` = capsula provisional del BCM, `ki` en barras (se gastan) y `formas` = formas en las
    que se puede usar (1 = normal), de la mascara de SB."""
    receta = receta_de(code, techs, receta, donor)
    caps = placeholders(techs, receta)
    evo = {}                 # codigo de la principal -> cadena; las demas no tienen capsula propia
    for ch in cadenas(techs, receta):
        evo[cadena_principal(ch, receta)["codigo"]] = ch
    skip = {t["codigo"] for ch in evo.values() for t in ch} - set(evo)
    dirs = {1: "->E", 2: "<-E", 0x10: "P+K+G+E en modo hiper"}
    out = []
    for t in sorted(techs, key=lambda t: bool(receta[t["codigo"]].get("definitivo"))):
        r, e = receta[t["codigo"]], t["entrada"]
        if r.get("omitir") or t["codigo"] in skip:
            continue
        ch = evo.get(t["codigo"])
        if ch:           # nombre de la de la 1a forma; se equipa si alguna de la cadena lo estaba
            extra = "evoluciona: " + " -> ".join("%s (formas %s)" % (receta[x["codigo"]]["nombre"], [
                i + 1 for i in range(8) if x["entrada"]["booster"] >> i & 1]) for x in ch)
            r = dict(r, nombre=receta[ch[0]["codigo"]]["nombre"],
                     equipada=any(receta[x["codigo"]].get("equipada", True) for x in ch))
        d = dirs[0x10] if r.get("definitivo") else dirs[r.get("especial", e["dir"])]
        if not ch:
            extra = "beam struggle" if r.get("beam_struggle") else "cinematica del donante" if r.get("definitivo") else ""
        formas = [i + 1 for i in range(8) if e["booster"] >> i & 1]
        out += ["", "[[capsula]]   # %s (SB %#x)%s" % (d, t["codigo"], ", " + extra if extra else ""),
                'nombre = "%s"' % r["nombre"], 'tipo = "%s"' % ("definitiva" if r.get("definitivo") else "especial"),
                "reemplaza = %d" % caps[t["codigo"]], "ki = %d" % round(r["ki"] / 10),
                # especiales en TODAS las formas: el BCM elige la 1a entrada equipada de esa
                # direccion; si su mascara excluye la forma actual el golpe falla (prueba en juego
                # 2026-10-06). Solo el definitivo conserva la de SB (como el 0x0E del donante).
                "formas = %s" % formas if formas and r.get("definitivo") and not r.get("todas_las_formas")
                else "# formas: todas",
                "equipada = %s" % ("true" if r.get("equipada", True) else "false")]
    return "\n".join(out[1:]) + "\n"


# ---------------------------------------------------------------- autocomprobacion
def prueba():
    """Comprobaciones sin escribir nada: utilidades puras y, si estan las ISOs/AFS, el BSP de GHF."""
    assert hue_rgb((0.0, 0.0, 1.0), 60) == (1.0, 1.0, 0.0)
    assert hue_rgb((1.0, 1.0, 1.0), 120) == (1.0, 1.0, 1.0)            # el blanco no cambia
    img = np.zeros((2, 2, 4), np.uint8)
    img[..., 2], img[..., 3] = 255, 255
    assert tuple(hue_image(img, 0)[0, 0]) == (255, 0, 0, 255)
    ls = ap7_sorted([ap7_line(30, 4, 1), ap7_line(8, 0, 0x64), ap7_line(30, 2, 5)])
    assert [struct.unpack_from("<H", x)[0] for x in ls] == [8, 30, 30]
    assert sorted(x[2] for x in ls) == [0, 1, 2] and ls[0][2] == 0
    t = wk_table(b"#AST" + b"GHF\0" + struct.pack("<II", 0, 0x10), [bytearray(0xF0) for _ in range(11)])
    assert wk_blocks(t)[10][:4] == b"wk10" and len(t) == 0x10 + 11 * 0xF0
    P, H = donor_bsp(4)
    assert amb_hd(kids(H, ">")) == H                                 # contenedor HD reconstruido igual
    if not os.path.exists(SB_ISO["sb2"]):
        print("prueba: utilidades OK (sin la ISO de SB2: no se prueba el BSP)")
        return
    bsp, mapas, rep, techs = build_bsp("GHF")
    info = bsp_codes_hd(bsp)
    assert not info["errores"], info["errores"]
    assert not RESERVED & (set(info["ast"]) - {0}), "codigos de rafaga de ki usados"
    assert mapas[0x460][0x15F] == 0 and mapas[0x491][0x162] in info["ast"]
    assert nombres("GHF")[0x499] == ("bfghf.amt", 5)                   # Special Beam Cannon
    print("prueba: OK (%d AST, %d ASE)" % (len(info["ast"]), len(info["ase"])))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("bsp", "nombres", "aplicar", "todo"):
        p = sub.add_parser(name)
        p.add_argument("--personaje", required=True, help="codigo de SB (GHF, GGT, VTO...)")
        p.add_argument("--juego", default="sb2", choices=sorted(SB_ISO))
        p.add_argument("--donante", type=int, default=4, help="ID B3 del donante (el de sbport)")
        p.add_argument("--salida", required=True)
        p.add_argument("--receta", help="JSON {codigo SB: {nombre, tono, dano, ki, ...}} (si no, RECETAS)")
        p.add_argument("--moveset", help="carpeta de sbport (anm_forma1_ps2.bin, camara_ps2.bin, sbport.json)")
    c = sub.add_parser("comprobar")
    c.add_argument("carpeta", help="con tecnicas.bin, anm_forma1_ps2.bin y camara_ps2.bin")
    c.add_argument("--donante", type=int, default=4)
    sub.add_parser("prueba")
    a = ap.parse_args()
    if a.cmd == "prueba":
        return prueba()
    if a.cmd == "comprobar":
        rd = lambda n: open(os.path.join(a.carpeta, n), "rb").read()    # noqa: E731
        errs, notes = comprobar(rd("tecnicas.bin"), rd("anm_forma1_ps2.bin"), rd("camara_ps2.bin"), a.donante)
        print("\n".join(["nota: " + x for x in notes] + ["ERROR: " + x for x in errs]))
        print("RESULTADO", "OK" if not errs else "CON ERRORES")
        return 1 if errs else 0
    receta = None
    if a.receta:
        receta = {int(k, 16): v for k, v in json.load(open(a.receta, encoding="utf-8")).items()}
    os.makedirs(a.salida, exist_ok=True)
    src = sb_sources(a.juego, a.personaje)
    if a.cmd in ("nombres", "todo"):
        r = nombres(a.personaje, a.juego, os.path.join(a.salida, "nombres"), bc=src[0])
        rc = receta if receta is not None else RECETAS.get(a.personaje, {})
        json.dump({"%#x" % c: dict(textura="%s #%d" % v, nombre=rc.get(c, {}).get("nombre")) for c, v in sorted(r.items())},
                  open(os.path.join(a.salida, "nombres", "nombres.json"), "w", encoding="utf-8"), indent=1)
        print("nombres: %d rotulos -> %s" % (len(r), os.path.join(a.salida, "nombres")))
    if a.cmd == "nombres":
        return 0
    bsp, mapas, rep, techs = build_bsp(a.personaje, a.juego, a.donante, receta, src)
    open(os.path.join(a.salida, "tecnicas.bin"), "wb").write(bsp)
    open(os.path.join(a.salida, "capsulas.toml"), "w", encoding="utf-8").write(capsulas_toml(a.personaje, techs, receta, a.donante))
    print("\n".join(rep))
    if a.cmd in ("aplicar", "todo"):
        import ps2hd  # noqa: PLC0415
        mv = a.moveset or os.path.join(os.path.dirname(os.path.abspath(a.salida)), "moveset")
        info = json.load(open(os.path.join(mv, "sbport.json"), encoding="utf-8"))
        anm, cam, rep2, extra = aplicar(open(os.path.join(mv, "anm_forma1_ps2.bin"), "rb").read(),
                                 open(os.path.join(mv, "camara_ps2.bin"), "rb").read(),
                                 info, a.personaje, a.juego, a.donante, receta, mapas, src)
        print("\n".join(rep2))
        for n, d in [("anm_forma1", anm), ("camara", cam)] + [("anm_forma%d" % (k + 2), x) for k, x in enumerate(extra)]:
            open(os.path.join(a.salida, n + "_ps2.bin"), "wb").write(d)
            open(os.path.join(a.salida, n + ".bin"), "wb").write(ps2hd.convert_block(d))
        if ULT_RECETA:                  # definitiva de SB traducida -> definitiva_animaciones
            json.dump(ULT_RECETA, open(os.path.join(a.salida, "definitiva.json"), "w", encoding="utf-8"), indent=1)
        errs, notes = comprobar(bsp, anm, cam, a.donante)
        for k, x in enumerate(extra):         # moveset de cada forma (tecnicas que evolucionan)
            errs += ["forma %d: %s" % (k + 2, e) for e in comprobar(bsp, x, cam, a.donante)[0] if e not in errs]
        # beam struggle en una cadena: basta con que su tecnica lo tenga en sus formas
        bs = [e for e in errs if "beam struggle" in e]
        if extra and len(bs) <= len(extra):
            errs = [e for e in errs if e not in bs]
            notes += ["beam struggle solo en las formas de su tecnica"] if bs else []
        print("\n".join(["nota: " + x for x in notes] + ["ERROR: " + x for x in errs]))
        print("RESULTADO", "OK" if not errs else "CON ERRORES")
        return 1 if errs else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())

