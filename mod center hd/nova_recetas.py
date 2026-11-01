#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""nova_recetas.py - Mods de Nova's Mods que se construyen con la copia del jugador (sin repartir arte de juegos).

  python nova_recetas.py lista --mods DIR                  -> receta<TAB>id<TAB>nombre_es<TAB>nombre_en<TAB>falta<TAB>estado<TAB>desc_es<TAB>desc_en
  python nova_recetas.py construir ID --mods DIR [--us DIR]
  python nova_recetas.py prueba                            (sin juego: comprueba las recetas contra las ISO del jugador)

Datos medidos (ELF de Budokai 2, tabla de personajes VA 0x389540, 712 B; +0xF4 modelos: filas = trajes x formas):
  FREEZA 140,141 | 142,143 | 144,141 = (normal, Mecha) | rojo (el "Kuriza" del jugador) | M de Majin
  CELL 124 | 125 | 126 = normal | azul | M de Majin; el tablero pide el traje 2 con T2 +7 = 2 (fases 5 y 6)
  YAMHAN 282,311,308,316 | 283,312,309,317 = Ten, Yamcha, fusion fallida (delgado, XYHS), Yamhan (XYMH)
  GOTAN (GOKOU/SATAN) 171,263,185 | 172,264,186 = Goku, Satan, Gotan (XGTA; el catalogo lo llamaba Gogeta por error)
  c47-52 BUU (GOTENKS, GOHAN L, VEGETA, CELL, FREEZA, TEN + YAM): tabla 0x3928B0 de 200 B, +0x84/+0x88 = paquete por traje:
  CMN 103/104, 101/102, 120/121, 97/98, 99/100, 122/123 = #AMB[AMM de la absorcion, AMO+AMT de Bu absorbido, ...]
Budokai 1: registro 23 CELL Jr. (DATA_EN 363 #AMO, 364 #AMT).
"""
import argparse
import json
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
for p in (os.path.join(HERE, "lib"), os.path.join(HERE, "..", "awo_tools")):
    if os.path.isdir(p) and p not in sys.path:
        sys.path.append(p)

# Carpeta -> (clave de importar.BU_ABSORBIDO, nombre interno que busca src/fusion.cpp, donante en B3, paquete en DATA_CMN, barras de ki)
BU_PAQUETES = {"nova_bu_vegeta": ("XBVG", "BU VEGETA", 7, 120, 6), "nova_bu_cell": ("XBCL", "BU CELL", 33, 97, 6),
               "nova_bu_freeza": ("XBFR", "BU FREEZA", 27, 99, 6), "nova_bu_tenyam": ("XBYM", "BU TENYAM", 12, 122, 5)}

RECETAS = {
    "nova_kuriza": {"nombre": ("Traje de Kuriza para Freezer", "Kuriza costume for Frieza"), "pide": ["b2"],
                    "desc": ("El traje rojo de Freezer de Budokai 2 (modelo original, con el sombreado de B3).",
                             "Frieza's red Budokai 2 costume (original model, with B3 shading).")},
    "nova_majin": {"nombre": ("Freezer y Cell con la M de Majin", "Frieza and Cell with the Majin M"), "pide": ["b2"],
                   "desc": ("Modelos de Budokai 3 con la M en la frente; el tablero de Budokai 2 los usa cuando toca.",
                            "Budokai 3 models with the M on the forehead; the Budokai 2 board uses them when due.")},
    "nova_bu_absorciones": {"nombre": ("Absorciones de Super Bu de Budokai 2", "Super Buu's Budokai 2 absorptions"), "pide": ["b2"],
                            "desc": ("Al conectar su definitiva, Bu puede absorber tambien a Vegeta, Cell, Freezer o Ten y Yamcha y lucha con su moveset de Budokai 3.",
                                     "When his ultimate connects, Buu may also absorb Vegeta, Cell, Frieza or Tien and Yamcha and fights with their Budokai 3 moveset.")},
    "nova_fusion_tiencha": {"nombre": ("Fusion de Yamcha y Ten: Yamhan", "Yamcha and Tien fusion: Yamhan"), "pide": ["b2"],
                            "desc": ("Capsula de fusion para Yamcha y Ten. Si falla (1 de cada 4), sale el Yamhan delgado, como en Budokai 2.",
                                     "Fusion capsule for Yamcha and Tien. If it fails (1 in 4), the skinny Yamhan appears, as in Budokai 2.")},
    "nova_fusion_gotan": {"nombre": ("Potara de Goku y Mr. Satan: Gotan", "Goku and Mr. Satan Potara: Gotan"), "pide": ["b2"],
                          "desc": ("Capsula de Potara para Goku y Mr. Satan: Gotan (el Gokule de los fans). Con Potara no hay fallo.",
                                   "Potara capsule for Goku and Mr. Satan: Gotan (the fans' Gokule). Potara never fails.")},
    "nova_cellin": {"nombre": ("Traje de Cell Jr. para Cell", "Cell Jr. costume for Cell"), "pide": ["b1"],
                    "desc": ("Cell Jr. de Budokai 1 como traje de Cell.", "Budokai 1's Cell Jr. as a Cell costume.")},
}
NOMBRE_JUEGO = {"b1": ("Budokai 1", "Budokai 1"), "b2": ("Budokai 2", "Budokai 2")}


def falta(rid):
    import iso  # noqa: PLC0415
    return [g for g in RECETAS[rid]["pide"] if not iso.find_game(g)]


def afs_de(game, inner):
    import importar  # noqa: PLC0415
    import iso  # noqa: PLC0415
    p = iso.find_game(game)
    if not p:
        raise SystemExit("ERROR: falta la ISO de %s en ps2_games" % NOMBRE_JUEGO[game][0])
    return importar.AfsFile(p, inner)


def b2_cmn():
    return afs_de("b2", "/USR/DATA_CMN.AFS")


def hijos_amb(d):
    n = struct.unpack_from("<I", d, 0x10)[0]
    return [d[a:a + s] for a, s in (struct.unpack_from("<II", d, 0x20 + 16 * k) for k in range(n))]


def escribe_mod(mods, carpeta, nombre, version="1"):
    d = os.path.join(mods, carpeta)
    os.makedirs(os.path.join(d, "modelos"), exist_ok=True)
    with open(os.path.join(d, "mod.toml"), "w", encoding="utf-8") as f:
        f.write('name = %s\nversion = "%s"\nauthor = "Nova\'s Mods (construido con tu copia)"\n' % (json.dumps(nombre), version))
    return d


def escribe_traje(d, personaje, nombre, modelos):
    with open(os.path.join(d, "traje.toml"), "w", encoding="utf-8") as f:
        f.write("[traje]\npersonaje = %d\nnombre = %s\nmodelos = %s\n" % (personaje, json.dumps(nombre), json.dumps(modelos)))


# ---------------------------------------------------------------- trajes con modelos de B1/B2 tal cual
def kuriza(a):
    cmn = b2_cmn()
    d = escribe_mod(a.mods, "nova_kuriza", RECETAS["nova_kuriza"]["nombre"][0])
    open(os.path.join(d, "modelos", "kuriza.amb"), "wb").write(cmn.entry(142))
    open(os.path.join(d, "modelos", "kuriza_mecha.amb"), "wb").write(cmn.entry(143))
    # Formas de Freezer en B3: 0-4 el cuerpo, 5 Mecha
    escribe_traje(d, 27, "Kuriza", ["modelos/kuriza.amb"] * 5 + ["modelos/kuriza_mecha.amb"])


def _con_huesos_de(a, amb_ps2, nativo):
    """#AMB PS2 -> HD con los huesos del modelo nativo (Bu con Cell es BCL_, Super Bu BUM_; sin esto las animaciones de B3
    no le llegan). Mismo paso que importar.py traje."""
    import importar  # noqa: PLC0415
    import roster_build as rb  # noqa: PLC0415
    work = rb.work_dir("nova_")
    cmn = rb.Afs(os.path.join(a.us or rb.default_us(), "data_cmn.afs"), work)
    hd = rb.to_hd(amb_ps2, work)
    want, have = rb.prefix(rb.skeleton(rb.to_hd(cmn.entry(nativo), work), work)[0]), rb.prefix(rb.skeleton(hd, work)[0])
    return importar._relabel_awo(hd, have, want) if want and have and want != have else hd


def cellin(a):
    import importar  # noqa: PLC0415
    b1 = afs_de("b1", "/USR/DATA_EN.AFS")
    d = escribe_mod(a.mods, "nova_cellin", RECETAS["nova_cellin"]["nombre"][0])
    hd = _con_huesos_de(a, importar.amb_pair(b1.entry(363), b1.entry(364)), 148)
    open(os.path.join(d, "modelos", "cellin.bin"), "wb").write(hd)
    escribe_traje(d, 33, "Cell Jr.", ["modelos/cellin.bin"])


def _anm_absorcion(paquete, nativo, donante_anm):
    """Fichero de animacion de una forma absorbida nativa de Super Bu: un solo cambio de modelo, el nativo, y en el mismo fichero
    la escena (el #BSK que espera su cinematica, con las animaciones de B2: mismas animaciones en los dos juegos, codigo de B3 >= 1168 =
    codigo de B2 + ... 168; Gotenks medido por duracion) y los golpes del absorbido en B3 (el #BSK de una forma manda sobre el de base)."""
    import afs_pair  # noqa: PLC0415
    import b1port  # noqa: PLC0415
    import ps2hd  # noqa: PLC0415
    cmn = b2_cmn()
    k = b1port.amb_kids(afs_pair.ps2(nativo))
    bsk, nat = k[0][0], b1port.Amm(k[1][0])
    b2_bsk = next(x for x, _ in b1port.amb_kids(cmn.entry(112)) if x[:4] == b"#BSK")
    b2_amm = b1port.Amm(b1port.amb_kids(cmn.entry(paquete))[0][0])
    b2_l = b1port.bsk_code_list(b2_bsk)
    anims = list(nat.anims)
    for c, at in enumerate(b1port.bsk_code_list(bsk)):
        if c < 1168 or not at or c + 168 >= len(b2_l) or not b2_l[c + 168]:
            continue
        an, po = struct.unpack_from("<HH", bsk, at)
        an2, po2 = struct.unpack_from("<HH", b2_bsk, b2_l[c + 168])
        if po == 5 and po2 == 3 and an2 < len(b2_amm.anims):
            anims[an] = b2_amm.anims[an2]
    d = b1port.amb_kids(afs_pair.ps2(donante_anm))
    d_bsk, d_amm = d[0][0], b1port.Amm(d[1][0])
    d_map = {}
    # La antena de Bu solo tiene su forma con las pistas de B2 (en el modelo esta recta): las de su reposo van en cada golpe del donante
    b2_mov = cmn.entry(114)
    bu_own = b1port.Amm(b1port.amb_kids(b2_mov)[0][0])
    an0, po0 = struct.unpack_from("<HH", b2_bsk, b2_l[0])
    reposo = (bu_own.anims[an0][3] if po0 == 1 else b2_amm.anims[an0][3]) or {}
    antena = {q: v for q, v in reposo.items() if q[1].startswith("HAIR")}

    def anim_map(i):
        if i not in d_map:
            d_map[i] = len(anims)
            fl, var, nf, bones = d_amm.anims[i] if i < len(d_amm.anims) else (9, 0, 1, None)
            # el pelo del donante no mueve la antena de Bu (huesos HAIR): lleva la suya de reposo
            anims.append((fl, var, nf, {**{q: v for q, v in bones.items() if not q[1].startswith("HAIR")}, **antena} if bones else bones))
        return d_map[i]

    propios = {c for c, at in enumerate(b1port.bsk_code_list(bsk)) if at and c >= 1168}
    codigos = [c for c, at in enumerate(b1port.bsk_code_list(d_bsk)) if at and c not in propios]
    bsk2, _ = b1port.bsk_graft(bsk, d_bsk, codigos, anim_map)
    bsk2 = bytearray(bsk2)
    for subs in b1port.bsk_blocks(bytes(bsk2)).values():   # lo injertado venia del almacen 3 del donante: aqui es el de la forma
        for q in subs:
            if struct.unpack_from("<H", bsk2, q + 2)[0] == 3:
                struct.pack_into("<H", bsk2, q + 2, 5)
    amm = nat.build(b2_amm.names, anims)
    return bytes(ps2hd.convert_block(b1port.amb_build([(bytes(bsk2), k[0][1]), (amm, k[1][1])] + list(k[2:]))))


def bu_absorciones(a):
    # Sin casilla: se llega a ellos solo conectando la definitiva de Bu (src/fusion.cpp, sorteo de B2). De B2 solo el modelo,
    # su boca y la escena de absorcion (abs1-3, segun la forma nativa sobre la que caiga el sorteo); moveset, tecnicas y definitiva, los
    # del absorbido en B3 HD. El personaje de la receta da el modelo y la boca a la forma de Bu, y es la pieza del tablero. Barras de ki: B2 no tiene este tipo de barras; elegidas como las formas absorbidas de B3 (5 y 6).
    import b1port  # noqa: PLC0415
    import ps2hd  # noqa: PLC0415
    import roster_build as rb  # noqa: PLC0415
    cmn_b3 = rb.Afs(os.path.join(a.us or rb.default_us(), "data_cmn.afs"), rb.work_dir("nova_"))
    capsulas = {7: [38, 39, 40, 41, 43, 44], 33: [117, 118, 119], 27: [100, 101, 102], 12: [63, 64, 65]}   # las de src/fusion.cpp kAbsorciones
    for carpeta, (clave, nombre, donante, paquete, ki) in BU_PAQUETES.items():
        _fusion(a, carpeta, clave, nombre, donante, una_forma=True, preset="solo_modelos")
        d = os.path.join(a.mods, carpeta)
        os.makedirs(os.path.join(d, "moveset"), exist_ok=True)
        dn = next(x for x in rb.DB["ids"] if x["id"] == donante)
        open(os.path.join(d, "moveset", "anm.bin"), "wb").write(cmn_b3.entry(dn["anm"][0]))
        # La antena de Bu son los huesos HAIR, HAIR1-3: el moveset del donante (Vegeta: XVGT_HAIR) la movia y la aplastaba contra la
        # cabeza. Con otro nombre, los golpes del donante no la tocan y la escena de B2 (mismo cambio en sus animaciones) si.
        m = os.path.join(d, "modelos", "traje1.amb")
        data = open(m, "rb").read()
        assert b"_HAIR" in data
        open(m, "wb").write(data.replace(b"_HAIR", b"_HAIQ"))
        for n, nativo in enumerate((137, 138, 139), 1):
            open(os.path.join(d, "moveset", "abs%d.bin" % n), "wb").write(_anm_absorcion(paquete, nativo, dn["anm"][0]).replace(b"_HAIR", b"_HAIQ"))
        # su boca: #AMB [AMO, AMM] dentro del paquete (hijos 5 y 6); la del donante no sirve, cada modelo ordena sus huesos a su manera
        k = b1port.amb_kids(b2_cmn().entry(paquete))
        open(os.path.join(d, "modelos", "bocas.bin"), "wb").write(bytes(ps2hd.convert_block(b1port.amb_build([k[5], k[6]]))))
        p = os.path.join(d, "personaje.toml")
        t = open(p, encoding="utf-8").read().replace('modelos = ["modelos/traje1.amb"]', 'modelos = ["modelos/traje1.amb"]\nbocas = ["modelos/bocas.bin"]\nmoveset = ["moveset/anm.bin", "moveset/abs1.bin", "moveset/abs2.bin", "moveset/abs3.bin"]\nki_base = [%d]\ncapsulas_nativas = %s' % (ki, capsulas[donante]), 1)
        open(p, "w", encoding="utf-8", newline="\n").write(t)


# ---------------------------------------------------------------- M de Majin sobre modelos de B3
def _modelo_b2(fid):
    import model_render as mr  # noqa: PLC0415
    import ps2hd  # noqa: PLC0415
    return mr.Model(ps2hd.conv_container(b2_cmn().entry(fid)))


def _texeles(m, tris, w, h):
    """(u, v en texeles, barycentricas, tri) de cada texel cubierto por los triangulos, en una textura de w x h."""
    import numpy as np  # noqa: PLC0415
    for t in tris:
        t = [int(k) for k in t]
        uv = m.uv[t] * (w, h)
        A = np.array([uv[:, 0], uv[:, 1], (1, 1, 1)])
        if abs(np.linalg.det(A)) < 1e-6:
            continue
        Ai = np.linalg.inv(A)
        for y in range(max(0, int(uv[:, 1].min())), min(h, int(uv[:, 1].max()) + 1)):
            for x in range(max(0, int(uv[:, 0].min())), min(w, int(uv[:, 0].max()) + 1)):
                b = Ai @ np.array((x + 0.5, y + 0.5, 1))
                if b.min() >= -0.02:
                    yield x, y, b, t


M_PASO = 0.008   # resolucion de la calca de la M (unidades del modelo; la M mide ~0,75 de ancho)


def _calca_m(m2, mat_m, tex_base=None):
    """Calca de la M en el plano de la frente: {(ix, iy): rgb}. Sale de los texeles de la textura de la M del modelo Majin
    de B2 que difieren de su textura normal `tex_base` (o, sin ella, de los claros)."""
    import numpy as np  # noqa: PLC0415
    P, _ = m2.posed()
    tex = m2.tex[m2.mats[mat_m]["tex"]]
    h, w = tex.shape[:2]
    if tex_base is not None:
        marca = np.abs(tex[..., :3] - m2.tex[tex_base][..., :3]).sum(-1) > 0.25
    else:
        marca = tex[..., :3].min(-1) > 0.6
    calca = {}
    for x, y, b, t in _texeles(m2, m2.tris[m2.tri_mat == mat_m], w * 3, h * 3):
        if marca[y // 3, x // 3]:
            p = b @ P[t]
            if p[2] > 0:
                calca[(round(p[0] / M_PASO), round(p[1] / M_PASO))] = tex[y // 3, x // 3, :3]
    if not calca:
        raise SystemExit("ERROR: no encuentro la M en el modelo de Budokai 2")
    llena = dict(calca)      # sin agujeros entre muestras
    for (i, j), c in calca.items():
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                llena.setdefault((i + di, j + dj), c)
    return llena


def _pinta_m(img, m, tris, P, calca):
    """Pinta la calca en `img` con 3x3 muestras por texel (bordes suaves). -> texeles tocados."""
    import numpy as np  # noqa: PLC0415
    h, w = img.shape[:2]
    suma = {}
    for x, y, b, t in _texeles(m, tris, w * 3, h * 3):
        p = b @ P[t]
        c = calca.get((round(p[0] / M_PASO), round(p[1] / M_PASO))) if p[2] > 0 else None
        if c is None:
            continue
        e = suma.setdefault((y // 3, x // 3), [0, np.zeros(3), set()])
        if (y, x) not in e[2]:      # uv espejada: la otra mitad de la cara cae en el mismo sitio
            e[2].add((y, x))
            e[0] += 1
            e[1] = e[1] + c
    for (y, x), (cob, col, _) in suma.items():
        f = min(cob, 9) / 9.0
        img[y, x, :3] = ((1 - f) * img[y, x, :3] + f * 255 * col / cob).astype(np.uint8)
    return len(suma)


def _pinta_textura(b, m, itex, img):
    """Sustituye la textura DXT3 `itex` del modelo por `img` (RGBA uint8, mismo tamano)."""
    import colores  # noqa: PLC0415
    from texture_b3 import encode_dxt3  # noqa: PLC0415
    o, dl = colores._azt_texturas(bytes(b))[_indice_dxt3(b, itex)]
    data = bytes(b[o:o + 128]) + encode_dxt3(img)
    if len(data) > dl:
        raise SystemExit("ERROR: la textura %d no cabe" % itex)
    b[o:o + len(data)] = data


def _indice_dxt3(b, itex):
    """Posicion de la textura en la lista de colores._azt_texturas (solo DXT3); itex cuenta todas."""
    import colores  # noqa: PLC0415
    bb = bytes(b)
    for magic, z, _ in colores.blocks(bb):
        if magic != b"#AZT":
            continue
        n, idx = struct.unpack_from(">II", bb, z + 0x10)
        k = 0
        for t in range(n):
            o = struct.unpack_from(">I", bb, z + idx + 4 * t)[0]
            do = struct.unpack_from(">I", bb, z + o + 0x14)[0]
            es = bb[z + do + 84:z + do + 88] == b"DXT3" and struct.unpack_from("<I", bb, z + do + 28)[0] <= 1
            if t == itex:
                if not es:
                    raise SystemExit("ERROR: la textura %d no es DXT3" % itex)
                return k
            k += es
    raise SystemExit("ERROR: textura %d no encontrada" % itex)


def _mover_draws(b, m, elegir, mat_nuevo):
    """Cambia de material los draws elegidos (el reflejo por normal ignora la uv: sin esto no se puede pintar la M)."""
    for d in m.table.draws:
        if elegir(d):
            v = struct.unpack_from(">I", b, d["abs"] + 0x24)[0]
            struct.pack_into(">I", b, d["abs"] + 0x24, (mat_nuevo << 16) | (v & 0xFFFF))


def majin_freezer(hd):
    """B3 Freezer (forma final): la cupula deja el reflejo por normal (no admite pintura) y pasa al material del borde
    de la cupula (misma disposicion de uv que la cupula de B2, medido): su textura recibe el reflejo horneado y la M."""
    import io  # noqa: PLC0415
    import numpy as np  # noqa: PLC0415
    import model_render as mr  # noqa: PLC0415
    import colores  # noqa: PLC0415
    from PIL import Image  # noqa: PLC0415
    b = bytearray(hd)
    m = mr.Model(bytes(b))
    P, _ = m.posed()
    MAT_ENV = next(k for k, x in enumerate(m.mats) if x["env"])
    # borde de la cupula: el material no-reflejo de pocos triangulos que solo esta en lo alto de la cabeza (61 en B3)
    centro = P[m.tris].mean(1)
    MAT_BORDE = next((k for k, x in enumerate(m.mats) if not x["env"] and 40 < (m.tri_mat == k).sum() < 100
                      and centro[m.tri_mat == k][:, 1].min() > 6.3), None)
    if MAT_BORDE is None or not any(d["material"] == MAT_ENV and d["B"][1] == 211 for d in m.table.draws):
        raise SystemExit("ERROR: el modelo de Freezer de este juego no es el esperado")
    TEX_BORDE = m.mats[MAT_BORDE]["tex"]
    i0, c0 = next(d["B"] for d in m.table.draws if d["material"] == MAT_ENV and d["B"][1] == 211)
    I = m.avb.indices()
    from b3_gateway import strip_tri  # noqa: PLC0415
    seq = I[i0:i0 + c0 + 2]
    cupula = [t for t in (strip_tri(seq, i) for i in range(len(seq) - 2)) if len(set(t)) == 3]
    o, dl = colores._azt_texturas(bytes(b))[_indice_dxt3(b, TEX_BORDE)]
    img = np.array(Image.open(io.BytesIO(bytes(b[o:o + dl]))).convert("RGBA"))
    h, w = img.shape[:2]
    env = m.tex[m.mats[MAT_ENV]["tex"]]
    # color plano de la cupula, como en B2 (el reflejo horneado salia a parches por la uv espejada): el morado medio del reflejo
    viva = env[(env[..., :3].max(-1) - env[..., :3].min(-1)) > 0.25][:, :3]
    plano = (np.clip(np.median(viva, 0) * 1.2, 0, 1) * 255).astype(np.uint8)
    alfa = int(np.median(img[..., 3]))
    pintados = 0
    for x, y, _, _t in _texeles(m, cupula, w, h):
        img[y, x, :3], img[y, x, 3] = plano, alfa
        pintados += 1
    img[img[..., :3].max(-1) < 40, :3] = plano    # lo negro de la textura era el hueco de la cupula (costura de la uv)
    m2 = _modelo_b2(144)
    marcados = _pinta_m(img, m, cupula, P, _calca_m(m2, 15, tex_base=m2.mats[10]["tex"]))
    if not marcados:
        raise SystemExit("ERROR: la M no cae en la cupula de Freezer")
    _pinta_textura(b, m, TEX_BORDE, img)
    _mover_draws(b, m, lambda d: d["material"] == MAT_ENV and d["B"][1] == 211, MAT_BORDE)
    return bytes(b), pintados, marcados


def majin_cell(hd):
    """B3 Cell perfecto: la gema de la frente lleva reflejo por normal; pasa al material de textura oscura de las alas
    (misma disposicion de uv que su textura de B2, medido) con la M pintada, en el AWG0 y en las caras de expresion."""
    import io  # noqa: PLC0415
    import numpy as np  # noqa: PLC0415
    import model_render as mr  # noqa: PLC0415
    import colores  # noqa: PLC0415
    from PIL import Image  # noqa: PLC0415
    from b3_gateway import strip_tri  # noqa: PLC0415
    b = bytearray(hd)
    m = mr.Model(bytes(b))
    P, _ = m.posed()
    MAT_ENV, TEX_OSCURA = 0, 8
    mat_osc = next((k for k, x in enumerate(m.mats) if x["tex"] == TEX_OSCURA and not x["env"]), None)
    if mat_osc is None or not m.mats[MAT_ENV]["env"]:
        raise SystemExit("ERROR: el modelo de Cell de este juego no es el esperado")
    gema = lambda d: d["material"] == MAT_ENV and ("FACE" in d["label"] or (d["B"][1] == 43 and d["group_bone"] == 0))  # noqa: E731
    I = m.avb.indices()
    tris = []
    for d in m.table.draws:
        if not gema(d):
            continue
        s, c = d["B"]
        if d["prim"] == 5:
            seq = I[s:s + c + 2]
            tris += [t for t in (strip_tri(seq, i) for i in range(len(seq) - 2)) if len(set(t)) == 3]
        else:
            seq = I[s:s + 3 * c]
            tris += [tuple(seq[i:i + 3]) for i in range(0, len(seq) - 2, 3)]
    o, dl = colores._azt_texturas(bytes(b))[_indice_dxt3(b, TEX_OSCURA)]
    img = np.array(Image.open(io.BytesIO(bytes(b[o:o + dl]))).convert("RGBA"))
    h, w = img.shape[:2]
    marcados = _pinta_m(img, m, tris, P, _calca_m(_modelo_b2(126), 8))
    if not marcados:
        raise SystemExit("ERROR: la M no cae en la frente de Cell")
    _pinta_textura(b, m, TEX_OSCURA, img)
    _mover_draws(b, m, gema, mat_osc)
    # caras de expresion: cada AWG repite la gema con su propia tabla de draws
    for k in m.variants().values():
        v = mr.Model(bytes(b), awg=k)
        mv = next((j for j, x in enumerate(v.mats) if x["tex"] == TEX_OSCURA and not x["env"]), None)
        if mv is None:
            # el AWG auxiliar no trae el material: se le cambia el de reflejo por la misma textura (uv de B2)
            for j, x in enumerate(v.mats):
                if x["env"]:
                    v.table.avb.data = b
                    v.table.mat_base = v.table.mat_base
                    struct.pack_into(">I", b, v.table.mat_base + j * 0x50 + 0x30, TEX_OSCURA)
                    fl = struct.unpack_from(">I", b, v.table.mat_base + j * 0x50 + 0x38)[0]
                    struct.pack_into(">I", b, v.table.mat_base + j * 0x50 + 0x38, fl & ~0x40)
            continue
        _mover_draws(b, v, lambda d: d["material"] == MAT_ENV and v.mats[MAT_ENV]["env"], mv)
    return bytes(b), marcados


def majin(a):
    import roster_build as rb  # noqa: PLC0415
    us = a.us or rb.default_us()
    cmn = rb.Afs(os.path.join(us, "data_cmn.afs"), rb.work_dir("nova_"))
    hd, _, n = majin_freezer(cmn.entry(184))
    hd2 = majin_freezer(cmn.entry(185))[0]
    d = escribe_mod(a.mods, "nova_freezer_majin", "Freezer con la M de Majin")
    open(os.path.join(d, "modelos", "final.bin"), "wb").write(hd)
    open(os.path.join(d, "modelos", "cien.bin"), "wb").write(hd2)
    escribe_traje(d, 27, "Freezer (Majin)", ["juego:181", "juego:182", "juego:183", "modelos/final.bin", "modelos/cien.bin", "juego:186"])
    hd, n2 = majin_cell(cmn.entry(148))
    d = escribe_mod(a.mods, "nova_cell_majin", "Cell con la M de Majin")
    open(os.path.join(d, "modelos", "perfecto.bin"), "wb").write(hd)
    escribe_traje(d, 33, "Cell (Majin)", ["juego:146", "juego:147", "modelos/perfecto.bin", "modelos/perfecto.bin"])
    print("M de Majin: %d texeles en Freezer, %d en Cell" % (n, n2))


# ---------------------------------------------------------------- fusiones (personaje + capsula)
def _fusion(a, carpeta, clave, nombre, donante, capsula=None, una_forma=False, preset="original"):
    """El personaje de B2 con su moveset (el importador de siempre) sin casilla en la rueda: solo se llega fusionandose.
    src/fusion.cpp lo busca por el nombre interno; `capsula` = (nombre, [IDs que la equipan])."""
    import subprocess  # noqa: PLC0415
    args = [sys.executable, os.path.join(HERE, "importar.py"), "importar", "b2", clave, "--mod", carpeta, "--nombre", nombre,
            "--donante", str(donante), "--preset", preset, "--mods", a.mods]
    if a.us:
        args += ["--us", a.us]
    r = subprocess.run(args, capture_output=True, text=True, errors="replace")
    if r.returncode:
        raise SystemExit("ERROR importando %s: %s" % (nombre, (r.stderr or r.stdout)[-400:]))
    p = os.path.join(a.mods, carpeta, "personaje.toml")
    t = open(p, encoding="utf-8").read().replace("[personaje]\n", "[personaje]\ncelda = false\n", 1)
    if una_forma:   # sin las transformaciones del donante
        t = t.replace("[personaje]\n", "[personaje]\nformas = 1\n", 1)
    if capsula:
        t += '\n[[capsula]]\nnombre = %s\ntipo = "especial"\nequipada = false\nki = 3\nfusion_de = %s\n' % (json.dumps(capsula[0]), json.dumps(capsula[1]))
    open(p, "w", encoding="utf-8", newline="\n").write(t)


def fusion_tiencha(a):
    _fusion(a, "nova_yamhan", "XYMH", "YAMHAN", 12, ("Fusion <Yamhan>", [12, 13]))
    _fusion(a, "nova_yamhan_s", "XYHS", "YAMHAN S", 12)


def fusion_gotan(a):
    _fusion(a, "nova_gotan", "XGTA", "GOTAN", 0, ("Potara <Gotan>", [0, 14]))


CONSTRUIR = {"nova_kuriza": kuriza, "nova_majin": majin, "nova_bu_absorciones": bu_absorciones,
             "nova_fusion_tiencha": fusion_tiencha, "nova_fusion_gotan": fusion_gotan, "nova_cellin": cellin}
CARPETAS = {"nova_kuriza": ["nova_kuriza"], "nova_majin": ["nova_freezer_majin", "nova_cell_majin"],
            "nova_bu_absorciones": list(BU_PAQUETES), "nova_fusion_tiencha": ["nova_yamhan", "nova_yamhan_s"],
            "nova_fusion_gotan": ["nova_gotan"], "nova_cellin": ["nova_cellin"]}


def estado(rid, mods):
    return "instalado" if all(os.path.isdir(os.path.join(mods, c)) or os.path.isdir(os.path.join(mods, c + ".disabled"))
                              for c in CARPETAS[rid]) else "no"


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    li = sub.add_parser("lista")
    li.add_argument("--mods", required=True)
    co = sub.add_parser("construir")
    co.add_argument("id")
    co.add_argument("--mods", required=True)
    co.add_argument("--us")
    sub.add_parser("prueba")
    a = ap.parse_args()
    if a.cmd == "lista":
        for rid, r in RECETAS.items():
            print("receta\t%s\t%s\t%s\t%s\t%s\t%s\t%s" % (rid, r["nombre"][0], r["nombre"][1], ",".join(falta(rid)),
                                                         estado(rid, a.mods), r["desc"][0], r["desc"][1]))
        return 0
    if a.cmd == "prueba":
        cmn = b2_cmn()
        assert hijos_amb(cmn.entry(97))[0][:4] == b"#AMM"
        for fid in (97, 99, 120, 122):
            h = hijos_amb(cmn.entry(fid))
            assert any(x[:4] == b"#AMO" and len(x) > 100000 for x in h), fid
        assert cmn.entry(142)[:4] == b"#AMB" and cmn.entry(185)[:4] == b"#AMB"
        print("OK")
        return 0
    if a.id not in RECETAS:
        raise SystemExit("ERROR: receta desconocida %s" % a.id)
    miss = falta(a.id)
    if miss:
        raise SystemExit("ERROR: falta la ISO de %s en ps2_games" % " y ".join(NOMBRE_JUEGO[g][0] for g in miss))
    CONSTRUIR[a.id](a)
    print("construido\t%s" % a.id)
    return 0


if __name__ == "__main__":
    sys.exit(main())
