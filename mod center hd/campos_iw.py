#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""campos_iw.py - Campos de los minijuegos de Infinite World (PS2) -> escenario NUEVO de Budokai 3 HD.

Frente R8 (2026-10-16). Los campos a pie de IW (modulo MSNMW: DATA_CMN 903, 897, 884, 900, 906, 909) no son escenarios
de combate: cada uno es un #AMB [#MCF niebla/cielo, (#AMO, #AMT) x N] con el cielo, las nubes, el terreno y extras
(la nave de Raditz en el 903), y su colision #WCF va en la entrada siguiente (904...). El juego de B3 solo sabe cargar
un bin de escenario (#AMB[#ZDD, #CAD, #CAS, #SPX, registro]).

Montaje: se parte del escenario de combate de IW `base` (DATA_CMN de IW 48..72 = el #MDD de B3 PS2 44..68 byte a byte)
y todo lo que se dibuja va en su objeto 0 (el juego no dibuja objetos nuevos de un alta): su sub-malla con colision se
queda con las partes ocultas (truco FFFFFF00 FFFF0000; la consulta de suelo necesita su modelo preparado) y las demas
pintan el terreno, que lleva pegados el cielo y las nubes (attach de amo2awo a su hueso raiz; texturas detras en el
mismo banco): el juego solo pinta tantas sub-mallas como tiene el escenario base. Camaras y script = los del base.
Colision = la caja del base (+-361 en Llanuras, suelo plano y paredes) estirada en x/z hasta el borde del terreno
(escala por campo en CAMPOS): se corre por todo el campo sin subir mesetas (ponytail: la colision real del #WCF va
aparte). Conversor de B1/B2 (tools/escenarios_convertir.py: mdd_a_zdd); se instala como mod (casilla nueva, no
sustituye nada). Todo sale de la copia del jugador (IW en ps2_games/ y su us/data_cmn.afs): nada se reparte.

Uso:
  python tools\campos_iw.py mod 903 --mods <juego>\mods          # crea mods\Campo IW 903
  python tools\campos_iw.py info 903                              # piezas del campo
"""
import argparse
import struct
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(Path(__file__).resolve().parent), str(RAIZ / "awo_tools")]   # mod center hd (Kit) y awo_tools

import escenarios_convertir as EC  # noqa: E402

# campo -> (titulo, escenario base de IW (= B3 HD -4), registro de B3 del tema, piezas (hijo #AMO, hijo #AMT), escala de
# la caja de colision del base (+-361 en Llanuras) en x/z para que sus paredes lleguen al borde del terreno)
# Piezas: 1/2 y 3/4 cielo y nubes, 5/6 terreno (el terreno va primero: objeto 0). La nave (903 7/8) no: la pinta
# iw_minijuegos.cpp como meta. Base = Llanuras (IW 58 = B3 54, registro 5); tema 16 (generico): con el tema 5 el juego
# se cierra al empezar el combate (lr 0x820BF918), porque el alta no lleva los grupos de objetos del registro 5.
# Cierres resueltos y pruebas: docs/14_frentes/RONDA4_PULIDO.md, seccion R8.
CAMPOS = {
    903: ("Llanura de Raditz (IW)", 58, 16, ((5, 6), (1, 2), (3, 4)), 4.2),     # terreno x/z +-1500 (suelo plano: bajo las mesetas se pasa por debajo a cualquier escala)
    897: ("Planeta del Kaio (IW)", 58, 16, ((5, 6), (1, 2), (3, 4)), 2.5),      # +-900
    884: ("Camino de la Serpiente (IW)", 58, 16, ((5, 6), (1, 2), (3, 4)), 10.4),  # x +-660, z -3640..3743
    900: ("Namek que explota (IW)", 58, 16, ((5, 6), (1, 2), (3, 4)), 4.2),
    906: ("Llanura de Piccolo (IW)", 58, 16, ((5, 6), (1, 2), (3, 4)), 4.2),
    909: ("Roquedal (IW)", 58, 16, ((5, 6), (1, 2), (3, 4)), 4.2),
    # campos de vuelo (FLIGHT, frente R12): sin #WCF ni #AMC propios (camaras de Llanuras); el mundo da la vuelta
    # (24000 / 16000) y se vuela a altura fija 400: la caja llega al borde del terreno. Las piezas 7/8 (881: un
    # objeto animado; 883: la casa de Guru) no van: las pone el minijuego donde toca.
    881: ("Tierra en vuelo (IW)", 52, 16, ((5, 6), (1, 2), (3, 4)), 46.0),   # x/z +-12000; base con 4 sub-mallas
    #      libres (IW 52 = B3 48, caja de colision +-268 x +-254): el terreno va partido en 2 (PARTES)
    883: ("Namek en vuelo (IW)", 52, 16, ((5, 6), (1, 2), (3, 4)), 30.7),    # x/z +-8000 (caja de IW 52)
    # interior de la nave de Goku (camara de gravedad, MSN_NB007): [#AMT, #AMO, #AMT, #AMO, #MTA], sin #MCF
    880: ("Nave de Goku por dentro (IW)", 58, 16, ((1, 0), (3, 2)), 4.0),
}
# data_cmn.afs del juego (B2 5000+, B1 5020+); menu = entrada + 100. Fijas: un campo nuevo no mueve las demas.
# El juego busca el alta por su carpeta de mod ("Campo IW <campo>": dbz3::escenarios::CampoIW).
ENTRADAS = {884: 5060, 897: 5061, 900: 5062, 903: 5063, 906: 5064, 909: 5065, 881: 5066, 883: 5067, 880: 5068}


def le(b, o):
    return struct.unpack_from("<I", b, o)[0]


def afs_iw():
    import importar  # noqa: PLC0415 (mod center hd: IW en carpeta o ISO, cualquier region)
    p = importar.source_path("iw")
    if not p:
        raise SystemExit("no encuentro Infinite World en ps2_games/")
    return importar.iw_afs(p)


def hijos(b):
    return [b[o:o + s] for o, s, _, _ in EC.hijos_amb(b, "<")]


NUBES = {}   # campo -> [(firma hex, n ventanas, du, dv, tam del #ZDD)] de las partes de cielo y nubes (X1esc 23/10)


def _nubes_de(capturas, n_tex_terreno, mcf, cuentas, tam_zdd):
    """Partes pegadas al terreno (textura >= las del terreno): primeras 3 ventanas (132 B = firma para hallarlas en la
    memoria del guest) y desplazamiento UV por cuadro de su capa: palabras (0x30 + 8*capa, 0x34 + 8*capa) del #MCF
    [H: capa k = k-esimo par; la deriva por cuadro a 60 Hz es lo plausible: 1e-4 * 60 = 0,006 de textura por segundo]."""
    out = []
    if mcf[:4] != b"#MCF":
        return out
    lim, t = [], n_tex_terreno
    for c in cuentas:
        lim.append((t, t + c)); t += c
    for draws, windows in capturas:
        for d in draws:
            tex = d["part"].tex
            if tex < n_tex_terreno or d["A"][1] < 3:
                continue
            capa = next((k for k, (lo, hi) in enumerate(lim) if lo <= tex < hi), 0)
            du, dv = struct.unpack_from("<2f", mcf, min(0x30 + 8 * capa, 0xB0))
            a0, n = d["A"]
            out.append(("".join("%02X" % x for x in b"".join(windows[a0:a0 + 3])), n, du, dv, tam_zdd))
    return out


def poner_nubes(carpeta, campo):
    """stage.toml: una linea `nubes = "firma,n,du,dv,tam"` por parte de cielo/nubes; src/escenarios.cpp mueve sus UV."""
    lineas = ['nubes = "%s,%d,%.7f,%.7f,%d"' % n for n in NUBES.get(campo, [])]
    if lineas:
        t = carpeta / "stage.toml"
        t.write_text(t.read_text(encoding="utf-8") + chr(10).join(lineas) + chr(10), encoding="utf-8")


def poner_niebla(carpeta, mcf):
    """Niebla del #MCF del campo (+0x1C color 0x00BBGGRR; +0x28/+0x2C inicio y fin) -> `niebla = "RRGGBBAA,ini,fin"`
    en su stage.toml (src/escenarios.cpp la pone en la entrada de niebla del registro anfitrion)."""
    c = le(mcf, 0x1C)
    ini, fin = struct.unpack_from("<2f", mcf, 0x28)
    linea = 'niebla = "%02X%02X%02XFF,%.0f,%.0f"     # niebla del #MCF del campo de IW' % (
        c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, ini, fin)
    t = carpeta / "stage.toml"
    t.write_text(t.read_text(encoding="utf-8") + linea + "\n", encoding="utf-8")


HIDE = bytes.fromhex("FFFFFF00FFFF0000")   # vtype/vflags de parte oculta (truco de modders; amo2awo la omite)


def tiene_colision(amo):
    """El #AMO lleva bloque de colision colgado de la lista de algun hueso (4a palabra)."""
    nb, bones, _namg, _tbl, nlist = (le(amo, 0x10 + 4 * j) for j in range(5))
    for i in range(nb):
        lp = le(amo, bones + 0x20 * i + 4)
        if lp and any(le(amo, lp + 16 * k + 12) not in (0, 0xFFFFFFFF) for k in range(nlist)):
            return True
    return False


def ocultar_partes(out, o, tam, quedan=0):
    """Oculta las partes dibujables del #AMO en out[o:o+tam] (huesos y colision siguen). quedan: deja las N mas
    pequenas (si el juego no admite un modelo sin partes)."""
    import amo2awo  # noqa: PLC0415
    partes = [p for g in amo2awo.PS2AMO(bytes(out[o:o + tam])).amgs for p in g.all_parts]
    for p in sorted(partes, key=lambda p: p.size)[quedan:]:
        out[o + p.off:o + p.off + 8] = HIDE
    return len(partes)




def escalar_colision(out, mo, k):
    """Colision de un #AMO PS2 (en out[mo:]) estirada x k en horizontal (x, z): vertices y esferas de los nodos.
    Las normales no cambian: en los escenarios de B3 solo hay paredes verticales y suelos planos (comprobado en
    Llanuras: ny 0 o 1); triangulo = [normal xyz, 1][?, i1, i2, i3] (indices a los vertices del nodo)."""
    a = bytes(out[mo:])
    nb, bones, _namg, _tbl, nlist = (le(a, 0x10 + 4 * j) for j in range(5))
    for i in range(nb):
        lp = le(a, bones + 0x20 * i + 4)
        for q in range(nlist if lp else 0):
            g3 = le(a, lp + 16 * q + 12)
            if g3 in (0, 0xFFFFFFFF):
                continue
            p = le(a, g3 + 0x48)
            while p:
                cx, cy, cz, cw, r, r2 = struct.unpack_from("<6f", a, p + 16)   # esfera del nodo y radio^2
                struct.pack_into("<6f", out, mo + p + 16, cx * k, cy, cz * k, cw, r * k, r2 * k * k)
                va = le(a, p + 4)
                for v in range(le(a, va + 12)):
                    x, y, z = struct.unpack_from("<3f", a, va + 16 + 16 * v)
                    struct.pack_into("<3f", out, mo + va + 16 + 16 * v, x * k, y, z * k)
                p = le(a, p)


def escalar_st50(out, mdd, s, k, vistos):
    """Arbol st50 del objeto (0x50 B por nodo: +0 centro xyz, +0x10 radio; hijos +0x34..+0x44) x k en horizontal."""
    if not s or s in vistos:
        return
    vistos.add(s)
    cx, cy, cz, cw, r = struct.unpack_from("<5f", mdd, s)
    struct.pack_into("<5f", out, s, cx * k, cy, cz * k, cw, r * k)
    for c in (0x34, 0x38, 0x3C, 0x40, 0x44):
        escalar_st50(out, mdd, le(mdd, s + c), k, vistos)


# Terrenos demasiado grandes para un solo #AWG (indices de 16 bits en la HD: 881 Tierra, ~104000 ventanas) se
# parten en N modelos con el truco de las partes ocultas; necesitan un escenario base con N sub-mallas libres.
PARTES = {881: 2}
# Campos sin #AMC propio (los de vuelo no tienen la entrada #WCF + #AMC): {campo: entrada de IW de la que tomar el
# #AMC}. Vacio: los de vuelo usan las del escenario base IW 52 (B3 48), que dan planos de tierra, costa y mar; las de
# Llanuras daban planos de solo cielo y las prestadas de 903/900 metian la camara bajo el agua o en una meseta.
CAMARAS = {}


def partir(amo, n):
    """n copias del #AMO, cada una con 1/n de sus partes visibles (por numero de vertices; las demas ocultas)."""
    if n <= 1:
        return [amo]
    import amo2awo  # noqa: PLC0415
    partes = [p for g in amo2awo.PS2AMO(bytes(amo)).amgs for p in g.all_parts]
    total, acum, trozo = sum(len(p.verts) for p in partes) or 1, 0, []
    for p in partes:
        trozo.append(min(n - 1, acum * n // total))
        acum += len(p.verts)
    out = []
    for i in range(n):
        b = bytearray(amo)
        for p, t in zip(partes, trozo):
            if t != i:
                b[p.off:p.off + 8] = HIDE
        out.append(bytes(b))
    return out


def mdd_con_campo(mdd, terreno, amt, quedan=0, k=1.0, camaras=None):
    """#MDD PS2 del escenario base que pinta el campo, todo en su objeto 0 (el juego no dibujaba objetos nuevos: el
    alta no lleva grupos de objetos en el registro). Sus sub-mallas con colision se quedan (la consulta de suelo
    sub_820CB0A0 necesita su modelo preparado, y el juego solo prepara los de una sub-malla) con las partes ocultas;
    las demas pintan el terreno (mismo numero e indices: el script del escenario las pide por indice). La colision
    del base se estira x k en horizontal. camaras: #AMC del campo (pistas de la presentacion, en sus coordenadas) en
    lugar del de Llanuras. Devuelve (mdd nuevo, [(off, tam)] de los bloques anadidos)."""
    out = bytearray(mdd)
    bloques = []
    tamanos = dict(EC.bloques_mdd(mdd))

    def poner(datos):
        out.extend(bytes(EC.align(len(out), 64) - len(out)))
        o = len(out)
        out.extend(datos)
        bloques.append((o, len(datos)))
        return o
    terrenos = terreno if isinstance(terreno, list) else [terreno]   # trozos del terreno (PARTES)
    p_ters, p_amt = [poner(t) for t in terrenos], poner(amt)
    ob0 = le(mdd, le(mdd, 0x20))
    if camaras and le(mdd, ob0 + 0xD4):            # primera camara del objeto 0 = la que pide el script (#SPX)
        struct.pack_into("<I", out, le(mdd, ob0 + 0xD8), poner(camaras))
    out.extend(bytes(EC.align(len(out), 64) - len(out)))
    lista0 = [le(mdd, le(mdd, ob0 + 0xA8) + 4 * q) for q in range(le(mdd, ob0 + 0x90))]

    def struct_(b):
        out.extend(bytes(EC.align(len(out), 16) - len(out)))
        o = len(out)
        out.extend(b)
        return o

    def submalla(p_amo):
        sm = bytearray(mdd[lista0[0]:lista0[0] + 0x20])
        struct.pack_into("<5I", sm, 0, 0, 0, p_amo, 0, 0)        # sin animaciones
        return struct_(sm)
    subs, libres, mo_col = [], [], None
    for s in lista0:
        mo = le(mdd, s + 8)
        if mdd[mo:mo + 4] == b"#AMO" and tiene_colision(mdd[mo:mo + tamanos[mo]]):
            ocultar_partes(out, mo, tamanos[mo], quedan)
            escalar_colision(out, mo, k)
            subs.append(s)                                        # la de siempre (sus animaciones incluidas)
            mo_col = mo_col or mo
        else:
            libres.append(len(subs))
            subs.append(None)
    if not libres:                                                # todas con colision: el terreno detras
        libres.append(len(subs))
        subs.append(None)
    if len(p_ters) > len(libres):
        raise ValueError("el escenario base tiene %d sub-mallas libres para %d trozos" % (len(libres), len(p_ters)))
    # trozos del terreno; las que sobran pintan el modelo de colision (partes ocultas: nada), cada una con su PROPIA
    # sub-malla: la misma dos veces en la lista se reubica dos veces (cierre al cargar, lr 0x82080148)
    for i, q in enumerate(libres):
        subs[q] = submalla(p_ters[i] if i < len(p_ters) else (mo_col or p_ters[0]))
    vistos = set()
    for c in (0xBC, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0):
        escalar_st50(out, mdd, le(mdd, ob0 + c), k, vistos)
    struct.pack_into("<I", out, ob0 + 0xA4, p_amt)
    struct.pack_into("<I", out, ob0 + 0x90, len(subs))
    struct.pack_into("<I", out, ob0 + 0xA8, struct_(struct.pack("<%dI" % len(subs), *subs)))
    return bytes(out), bloques


def _raices(amo):
    """[(offset de la traslacion, (x, y, z))] de los huesos raiz (padre 0) de cada #AMG del #AMO PS2."""
    import amo2awo  # noqa: PLC0415
    out = []
    for wm in amo2awo.PS2AMO(bytes(amo)).amgs:
        for i in range(wm.nb):
            o = wm.a + wm.axes_rel + 80 * i
            if le(amo, o + 0x40) == 0:
                out.append((o + 16, struct.unpack_from("<3f", amo, o + 16)))
    return out


def sin_raiz_del_terreno(extra, terreno):
    """studies 8 (NB007, medido en el juego): las piezas pegadas al hueso raiz del terreno (attach_rigid, head=0) se
    dibujan con la traslacion de esa raiz SUMADA OTRA VEZ. Solo pasa en la nave (880: raiz del suelo y del casco en
    (55, 0, 0); en los demas campos vale 0): el casco salia ~55 u desplazado en x respecto al suelo y Goku, en el origen
    de IW, quedaba dentro de la pared. Se resta la raiz del terreno a las raices del extra (copia)."""
    rt = next((t for _o, t in _raices(terreno)), (0.0, 0.0, 0.0))
    if not any(abs(v) > 1e-4 for v in rt):
        return extra
    b = bytearray(extra)
    for o, t in _raices(extra):
        struct.pack_into("<3f", b, o, *(t[k] - rt[k] for k in range(3)))
    return bytes(b)


def bin_campo(afs, campo, solo_terreno=False, quedan=0):
    """Bin HD del campo. El cielo y las nubes se pegan AL MODELO DEL TERRENO como partes rigidas de su hueso raiz
    (bind identidad), con el 'attach' de amo2awo (como las pelucas de B2) y sus texturas detras en el mismo banco:
    como sub-mallas propias no se dibujaban (el juego solo pinta las sub-mallas que tiene el escenario base, 2 en
    Llanuras; RE de sub_820BF558: tabla de 32 B por sub-malla)."""
    import amo2awo  # noqa: PLC0415 (awo_tools, en el path desde escenarios_convertir)
    import amt_ps2  # noqa: PLC0415
    _titulo, base, _tema, piezas, k = CAMPOS[campo]
    piezas = piezas[:1] if solo_terreno else piezas
    f = hijos(afs.entry(campo))
    b = hijos(afs.entry(base))
    (a0, t0), resto = piezas[0], piezas[1:]
    adjuntos, desde = [], le(f[t0], 0x10)         # banco comun: terreno, luego cada extra (indices n, n+1...)
    for a, t in resto:
        adjuntos.append((sin_raiz_del_terreno(f[a], f[a0]), desde))
        desde += le(f[t], 0x10)
    # camaras de la presentacion: el #AMC del propio campo (DATA_CMN campo+1, junto al #WCF): 3 pistas de 300
    # cuadros como las de los escenarios de combate (mismo formato), apuntando a lo que se ve en ese campo
    cam = next((h for h in hijos(afs.entry(CAMARAS.get(campo, campo + 1))) if h[:4] == b"#AMC"), None)
    terrenos = partir(f[a0], PARTES.get(campo, 1))
    mdd, blq = mdd_con_campo(b[0], terrenos if len(terrenos) > 1 else terrenos[0], f[t0], quedan, k, cam)
    viejo, conv, conv_amo, pegar = EC.bloques_mdd, EC.ps2hd.convert_block, amo2awo.convert_amo, amo2awo.attach_rigid

    def bloques(m):                       # los anadidos con su tamano exacto (detras van structs del grafo)
        return viejo(b[0]) + blq if m is mdd else viejo(m)

    def convertir(d):                     # el #AMT del terreno lleva detras las texturas del cielo y las nubes
        return amt_ps2.to_azt(d, extra=tuple(f[t] for _, t in resto)) if d == f[t0] else conv(d)

    def convertir_amo(d, report=None, attach=()):
        return conv_amo(d, report, adjuntos if d == terrenos[0] else attach)
    EC.bloques_mdd, EC.ps2hd.convert_block = bloques, convertir
    amo2awo.convert_amo = convertir_amo
    amo2awo.attach_rigid = lambda a, e, t: pegar(a, e, t, head=0)      # hueso 0 = raiz del campo
    amo2awo.build_awg_todos.clear()
    try:
        zdd = EC.mdd_a_zdd(mdd, pistas_min=3)
    finally:
        EC.bloques_mdd, EC.ps2hd.convert_block = viejo, conv
        amo2awo.convert_amo, amo2awo.attach_rigid = conv_amo, pegar
    NUBES[campo] = _nubes_de(amo2awo.build_awg_todos, le(f[t0], 0x10), hijos(afs.entry(campo))[0],
                         [le(f[t], 0x10) for _, t in resto], len(zdd))
    nat = EC.hd_nativo(base - 4)          # mismo escenario en la HD del jugador: script, efectos y camaras
    k = [nat[o:o + s] for o, s, _, _ in EC.hijos_amb(nat, ">")]
    return EC.amb_hd([(zdd, 0xFFFFFFFF), (k[1], 0xFFFFFFFF), (k[2], 0xFFFFFFFF), (k[3], 8), (k[4], 0xFFFFFFFF)])


def cmd_info(a):
    afs = afs_iw()
    for c in a.campos or sorted(CAMPOS):
        e = afs.entry(c)
        print(c, CAMPOS.get(c, ("?",))[0], [(k, h[:4].decode("latin1"), len(h)) for k, h in enumerate(hijos(e)) if h])


def cmd_mod(a):
    afs = afs_iw()
    for c in a.campos:
        titulo, base, tema, _, _k = CAMPOS[c]
        datos = Path(a.bin).read_bytes() if a.bin and Path(a.bin).exists() else bin_campo(afs, c, a.solo_terreno, a.quedan)
        if a.bin and not Path(a.bin).exists():
            Path(a.bin).write_bytes(datos)
        entrada = ENTRADAS[c]
        viejo = EC._modelos_escenario     # la foto del selector, sin el suelo del base (partes ocultas: vacio)
        EC._modelos_escenario = lambda z: [m for m in viejo(z) if len(m.pos)]
        try:
            carpeta = EC.instalar(datos, a.mods or (RAIZ / "mods"), "Campo IW %d" % c, entrada, "Infinite World",
                                  id_se=base - 4 + 1, tema=tema, foto=a.foto, rotulo_txt=titulo)
        finally:
            EC._modelos_escenario = viejo
        poner_niebla(carpeta, hijos(afs.entry(c))[0])
        poner_nubes(carpeta, c)
        print("campo %d '%s' -> %s (entradas %d y %d, %d B)" % (c, titulo, carpeta, entrada, entrada + 100, len(datos)))


# ---------------------------------------------------------------------------------------------------------
# auto: lo llama el launcher al pulsar Jugar (ModPipeline::BuildStages) con las carpetas del jugador. Crea en su
# carpeta mods los escenarios que faltan o cuya ISO cambio (sello en origen.txt) a partir de SUS juegos de PS2:
# los 6 campos de IW y los escenarios de B1/B2 probados en el juego. Nada del juego viaja en el Kit; un fallo no
# para nada (sale 0 y lo dice en el registro: el launcher juega igual).
# ---------------------------------------------------------------------------------------------------------
VERSION = 18  # 18 (X1esc 23/10): Base (g0) conserva TODAS las raices quietas, en su orden (los huesos de la zona B se numeran sobre todas): cambio de zona sin cierre. 17 (X1esc 23/10): registro de 641 casillas completado con las del 50/60/54 (sin ellas, cierre en peleas largas de los B2: nombre NULL en sub_8208DEE0); los B2 viejos se rehacen solos. 16 (X1esc): B2 g0 con 2 raices (cambio de zona del motor sin cierre). 15 (G02 studies 2): B2 grupo 1 sin la raiz #AMP que cierra al dibujar (lr 0x8208E5AC);
              # sidecar efectos_b2.txt/spx_b2.bin/banco_efectos_b2.bin (el conversor no puede ejecutar el
              # #SPX de B2 en B3: nativas renumeradas); sin cambios en la geometria de los grupos de B12.
              # 14: caja de colision de B2 por grupo (Base Red Ribbon 1.4, medido en el juego: docs/14_frentes). 13: rotulos de B2 corregidos por el usuario (las carpetas no cambian: las usan el tablero y la nave). 12: nombres de g3/g4/g6 corregidos con PCSX2 (R7 b2_integracion). 11: B2 sin animaciones ni camaras de destruccion (R7 escenarios).   subir si cambia el resultado del conversor: se rehacen en el equipo del jugador
B12 = (("b2", 2, "Templo de Kami", "Templo (B2)", 1.0),
       ("b2", 1, "Torneo B2", "Torneo (B2)", 1.0),         # X1esc 23/10: g1 convierte sin la raiz #AMP (G02); prueba visual en juego/PCSX2          # (juego, grupo, carpeta, rotulo, caja-escala): docs/14_frentes/
       ("b2", 7, "Montanas nevadas", "Muscle Tower (B2)", 1.0),       # escenarios.md. R7: probados en combate los B2
       ("b2", 0, "Base Red Ribbon B2", "Base de la Red Ribbon (B2)", 1.4),   # 0, 2, 3, 4, 6, 7 y 8. Fuera: g1 (se cierra
       ("b2", 3, "Namek B2", "Mundo de Kaiyoshin (B2)", 1.0),                    # al dibujar, lr 0x8208E5AC) y g5 (suelo
       ("b2", 4, "Llanura B2", "Llanura (B2)", 1.0),                        # negro). R7: escenario de B2 0-12 -> grupo
       ("b2", 6, "Islas B2", "Planeta Namek (B2)", 1.0),                            # (ELF 0x372AB0): 4 8 5 1 0 7 2 6 3 4 8 0 7
       # G02: g1 ya convierte sin la raiz #AMP (lr 0x8208E5AC) y g5 sigue con #MAS vacio (suelo negro).
       # Los dos quedan FUERA de B12 hasta la prueba visual; no incluirlos a ciegas.
       ("b2", 8, "Calle de la ciudad B2", "Zona urbana (B2)", 1.0),            # caja-escala: la CAJA de colision del
       ("b1", 3, "Paramo al atardecer", "Páramo al atardecer", 1.0))            # grupo (x/z; 1.0 = original de B2)


# S1-F03 (studies 1, Leyenda de Mr. Satan): el Ring de Cell de B1 entero, de la ISO del jugador. Su objeto 1 (la
# variante con el ring, las losas y la hierba) pasa a objeto 0 y las 4 columnas se pegan al terreno
# (awo_tools/amo2awo.py escenario_ring_cell_b1). La carpeta la busca pasarela.cpp (EscenarioB3, escenarios::PorMod).
B12 = B12 + (("b1", 11, "Ring de Cell B1", "Ring de Cell (B1)", 1.0),)
_escenario_b2_s1f03 = EC.escenario_b2


def _escenario_s1f03(fuente, grupo, **k):
    if grupo == 11 and fuente.juego == EC.JUEGOS["b1"][0]:
        import amo2awo  # noqa: PLC0415 (awo_tools, en el path desde escenarios_convertir)
        return amo2awo.escenario_ring_cell_b1(EC, fuente, grupo, convertir=_escenario_b2_s1f03, **k)
    return _escenario_b2_s1f03(fuente, grupo, **k)


EC.escenario_b2 = _escenario_s1f03   # S1-F03: fin


def _sello(ruta):
    # H8 24/10: sha1 de los primeros 4 MiB en vez de st_mtime (la fecha cambia al copiar la ISO y forzaba rehacer escenarios)
    import hashlib  # noqa: PLC0415
    st = Path(ruta).stat()
    with open(ruta, "rb") as fh:
        h = hashlib.sha1(fh.read(4 << 20)).hexdigest()
    return "%s|%d|%s|v%d" % (Path(ruta).name, st.st_size, h, VERSION)


def _al_dia(carpeta, sello):
    try:
        return (carpeta / "stage.toml").is_file() and (carpeta / "origen.txt").read_text(encoding="utf-8") == sello
    except OSError:
        return False


def _carpetas(us, ps2):
    EC.US, EC.PS2 = Path(us), Path(ps2)
    import importar  # noqa: PLC0415
    importar.PS2 = str(ps2)


def _apartar_base_vieja(mods):
    """UH (23/10): la Base Red Ribbon se llamaba "Base B2" en versiones anteriores del conversor y comparte la
    entrada 5000. Si las dos carpetas estan en mods, la vieja gana por orden alfabetico y sigue cerrando el
    juego al cambiar de zona. Se aparta (nunca se borra) a mods/_respaldo_escenarios."""
    import re  # noqa: PLC0415
    import shutil  # noqa: PLC0415
    import time  # noqa: PLC0415
    mods = Path(mods)
    nueva = mods / "Base Red Ribbon B2"
    if not (nueva / "stage.toml").is_file():
        return
    respaldo = mods / "_respaldo_escenarios"
    for d in sorted(mods.iterdir()):
        if not d.is_dir() or d == nueva or d.name.startswith("_"):
            continue
        try:
            t = (d / "stage.toml").read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        m = re.search(r"^\s*entrada\s*=\s*(\d+)", t, re.M)
        if not m or int(m.group(1)) != 5000:
            continue
        respaldo.mkdir(parents=True, exist_ok=True)
        destino = respaldo / ("%s_%s" % (d.name, time.strftime("%Y%m%d-%H%M%S")))
        n = 2
        while destino.exists():
            destino = respaldo / ("%s_%s_%d" % (d.name, time.strftime("%Y%m%d-%H%M%S"), n))
            n += 1
        try:
            shutil.move(str(d), str(destino))
            print("escenarios: Base vieja '%s' (entrada 5000) apartada a %s" % (d.name, destino))
        except OSError as e:  # noqa: BLE001 (un respaldo que falla no para la conversion)
            print("escenarios: aviso: no pude apartar la Base vieja '%s': %s" % (d.name, e))


def _tarea(t):
    """Un escenario (en un proceso aparte). t = (tipo, clave, mods, us, ps2, sello)."""
    tipo, clave, mods, us, ps2, sello = t
    _carpetas(us, ps2)
    viejo = EC._modelos_escenario     # foto del selector sin el suelo del base (partes ocultas: vacio)
    EC._modelos_escenario = lambda z: [m for m in viejo(z) if len(m.pos)]
    try:
        if tipo == "iw":
            titulo, base, tema, _, _k = CAMPOS[clave]
            afs = afs_iw()
            datos = bin_campo(afs, clave)
            carpeta = EC.instalar(datos, mods, "Campo IW %d" % clave, ENTRADAS[clave],
                                  "Infinite World", id_se=base - 4 + 1, tema=tema, rotulo_txt=titulo)
            poner_niebla(carpeta, hijos(afs.entry(clave))[0])
            poner_nubes(carpeta, clave)
        else:
            juego, grupo, nombre, rot, caja = next(x for x in B12 if x[:2] == clave)
            fuente = EC.IsoB2(None, juego)
            datos = EC.escenario_b2(fuente, grupo, escala=caja)
            carpeta = EC.instalar(datos, mods, nombre, EC.ENTRADA_BASE + (20 if juego == "b1" else 0) + grupo,
                                  EC.JUEGOS[juego][0], id_se=45, tema=16, rotulo_txt=rot)
            # G02: el #SPX y el banco #AME de B2 se guardan como evidencia; no se instalan (nativas de B3
            # renumeradas). La tabla por escenario queda en efectos_b2.txt dentro de la carpeta del mod.
            try:
                EC.conservar_efectos_b2(fuente, grupo, carpeta)
            except Exception:  # noqa: BLE001 (un sidecar no puede tumbar la conversion)
                pass
        (carpeta / "origen.txt").write_text(sello, encoding="utf-8")
        return "creado %s (%d B)" % (carpeta.name, len(datos))
    finally:
        EC._modelos_escenario = viejo


def cmd_auto(a):
    from concurrent.futures import ProcessPoolExecutor  # noqa: PLC0415
    import os  # noqa: PLC0415
    _carpetas(a.us, a.ps2)
    mods = Path(a.mods)
    import final_b1  # noqa: PLC0415  S5-A3b: poses de B1 de la escena final de la Leyenda (mod en tu carpeta mods)
    final_b1.auto(str(mods), a.us, a.ps2)
    _apartar_base_vieja(mods)
    tareas = []
    import importar  # noqa: PLC0415
    iw = importar.source_path("iw")
    if iw:
        usr = Path(iw) / "USR" / "DATA_CMN.AFS"
        sello = _sello(iw if Path(iw).is_file() else (usr if usr.exists() else iw))
        for c in sorted(CAMPOS):
            if not _al_dia(mods / ("Campo IW %d" % c), sello):
                tareas.append(("iw", c, str(mods), a.us, a.ps2, sello))
    else:
        print("escenarios: sin Infinite World en %s (campos de IW: nada que hacer)" % a.ps2)
    for juego, grupo, nombre, _rot, _caja in B12:
        ruta = EC.iso.find_game(juego, root=str(EC.PS2))
        if not ruta:
            print("escenarios: sin %s en %s" % (EC.JUEGOS[juego][0], a.ps2))
            continue
        sello_b12 = _sello(ruta) + ("|arbol2" if (juego, grupo) == ("b1", 11) else "")   # A3b: el ring de Cell de B1 se rehace una vez (arboles TREE2 verdes); el resto de escenarios, no
        if not _al_dia(mods / nombre, sello_b12):
            try:   # X1esc: aviso si el escenario B2 instalado lleva el registro viejo (sello < v17); se regenera aqui mismo
                viejo = (mods / nombre / "origen.txt").read_text(encoding="utf-8")
                v = int(viejo.split("|v")[1].split("|")[0]) if "|v" in viejo else 0
                if juego == "b2" and v < 17:
                    print("escenarios: aviso: %s lleva el registro de efectos incompleto (v%d): puede cerrar el juego en peleas largas; se regenera ahora" % (nombre, v))
            except (OSError, ValueError, IndexError):
                pass
            tareas.append(("b12", (juego, grupo), str(mods), a.us, a.ps2, sello_b12))
    if not tareas:
        print("escenarios: al dia")
        return 0
    with ProcessPoolExecutor(max_workers=max(1, min(6, (os.cpu_count() or 2) // 2))) as ex:
        for t, fut in [(t, ex.submit(_tarea, t)) for t in tareas]:
            try:
                print("escenarios: %s" % fut.result())
            except Exception as e:  # noqa: BLE001 (un escenario que falla no para los demas ni el juego)
                print("escenarios: fallo %s %s: %s" % (t[0], t[1], e))
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = p.add_subparsers(dest="cmd", required=True)
    s = sp.add_parser("info")
    s.add_argument("campos", nargs="*", type=int)
    s.set_defaults(fn=cmd_info)
    s = sp.add_parser("mod")
    s.add_argument("campos", nargs="+", type=int, choices=sorted(CAMPOS))
    s.add_argument("--mods", help="carpeta mods del juego (por defecto ./mods)")
    s.add_argument("--bin", help="bin sin comprimir: si existe se usa, si no se guarda (un solo campo)")
    s.add_argument("--solo-terreno", action="store_true", help="sin cielo ni nubes (solo el objeto 0)")
    s.add_argument("--quedan", type=int, default=0, help="partes del suelo del base que se siguen dibujando")
    s.add_argument("--foto", help="imagen para la casilla del selector (si no, foto sin GPU del escenario)")
    s.set_defaults(fn=cmd_mod)
    s = sp.add_parser("auto", help="los que falten en la carpeta mods del jugador (launcher, al pulsar Jugar)")
    s.add_argument("--mods", required=True)
    s.add_argument("--us", required=True, help="carpeta con data_cmn.afs de la HD")
    s.add_argument("--ps2", required=True, help="carpeta ps2_games")
    s.set_defaults(fn=cmd_auto)
    a = p.parse_args()
    return a.fn(a) or 0


if __name__ == "__main__":
    sys.exit(main())
