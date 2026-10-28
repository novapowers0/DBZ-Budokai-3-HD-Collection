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
    881: ("Tierra en vuelo (IW)", 52, 16, ((5, 6), (3, 4), (1, 2)), 46.0),   # x/z +-12000; base con 4 sub-mallas
    #      libres (IW 52 = B3 48, caja de colision +-268 x +-254): el terreno va partido en 2 (PARTES)
    883: ("Namek en vuelo (IW)", 52, 16, ((5, 6), (3, 4), (1, 2)), 30.7),    # x/z +-8000 (caja de IW 52)
    # interior de la nave de Goku (camara de gravedad, MSN_NB007): [#AMT, #AMO, #AMT, #AMO, #MTA], sin #MCF
    880: ("Nave de Goku por dentro (IW)", 58, 16, ((1, 0), (3, 2)), 4.0),
}
# data_cmn.afs del juego (B2 5000+, B1 5020+); menu = entrada + 100. Fijas: un campo nuevo no mueve las demas.
# El juego busca el alta por su carpeta de mod ("Campo IW <campo>": dbz3::escenarios::CampoIW).
ENTRADAS = {884: 5060, 897: 5061, 900: 5062, 903: 5063, 906: 5064, 909: 5065, 881: 5066, 883: 5067, 880: 5068}
# Campos que el original usa en los minijuegos de VUELO: MgFly_UpdateStageByCameraDelta (EU 0x366B10) llama a
# Stage_UpdateAndDraw_ con el movimiento de la camara, asi que Node_ScrollBackgroundUV (0x306570) arrastra sus capas;
# los minijuegos de campo pasan 0 (MiniGame_UpdateTimers EU 0x296110) y sus capas solo derivan con la base.
VUELO = {881, 883}
# Pieza de CIELO de cada campo (indice dentro de las piezas de fondo: 0 = primera). s_Ground (EU 0x1E2B20) monta DOS
# objetos: el 0 (suelo, dibujo Stage_DrawTiledGround 0x1E2590) y el 1 (cielo, dibujo Stage_DrawCameraFollowModel
# 0x1E2880), que dibuja su modelo trasladado a (camara.x, 0, camara.z) salvo que Battle_GetStateOrInvalid (0x20E290)
# devuelva 0. En las piezas del #AMB ese objeto es la primera de las de fondo (CAMPOS: (1,2) = cielo, (3,4) = nubes),
# asi que se marca `sigue` en su linea `nubes` y src/escenarios.cpp mueve sus ventanas con la camara. El 880 es el
# interior de la nave: sus dos piezas son casco e interior, no hay cielo que seguir.
CIELO = {903: 0, 897: 0, 884: 0, 900: 0, 906: 0, 909: 0, 881: 0, 883: 0}

# DS63: montaje del escenario de combate de IW (tabla EU 0x44ABC4, 0x5C por ID; tabla de funciones 0x44B1D0,
# PTR_Stage_SetupObjectsWithLensFlare_0044b1d0). El parametro +0 de la tabla es la entrada #WCF (impar) y el paquete
# #MDD es la anterior (param-1): por eso los paquetes son 48..72 pares. Los campos de minijuego (881..909) NO pasan
# por esta tabla: MiniGame_LoadStageModel (EU 0x296DE0) monta su paquete directo y no llama a Stage_SetupObjects.
# Se guarda igualmente el montaje del escenario BASE para que el port sepa que no hay clima que lanzar (GlobalVar1,
# s_Trs_Ground) y para las altas de escenarios de combate que si lo tengan.
SETUP_POR_PAQUETE = {
    48: "Stage_SetupObjects",
    50: "Stage_SetupObjectsWithLensFlare",
    52: "s_Trs_Ground",
    54: "Stage_SetupObjectsWithClimate",
    56: "Stage_SetupObjectsWithSandDust",
    58: "Stage_SetupObjectsGlobalVar1",
    60: "Stage_SetupObjectsLensFlareB",
    62: "s_Ground",
    64: "Stage_SetupObjectsAnimUV",
    66: "Stage_UpdateObjectsAndClimate",
    68: "Stage_UpdateObjectsClimateLensFlare",
    70: "Stage_UpdateObjectsLensFlareFileIndex",
    72: "Stage_ReleaseAmaScreens",
}


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


NUBES = {}   # campo -> [(firma hex, n ventanas, du, dv, tam del #ZDD, px, py, capa)] de cielo y nubes


def capas_de(mcf):
    """Capas UV del fondo del campo: #MCF +0x10 flags (1 = envuelve x, 2 = envuelve z), +0x14/+0x18 extension del
    mundo y 8 capas de 16 B desde +0x30 = (base u, base v, arrastre x, arrastre y).

    RE: Stage_ParseResourceBundle (EU 0x307060) copia #MCF+0x10..0x2C y +0x30..0xB0 a iRam0049cdb8+0x630/+0x650 y
    Node_ScrollBackgroundUV (EU 0x306570) hace, por capa y cuadro: u = u + base_u + arrastre_x * dx (idem v),
    envolviendo en [0,1) y recortando dx a la extension si el flag lo pide. dx es el movimiento de la camara
    (MgFly_UpdateStageByCameraDelta 0x366B10; en los minijuegos de campo MiniGame_UpdateTimers 0x296110 pasa 0).
    """
    if mcf[:4] != b"#MCF":
        return None
    flags = le(mcf, 0x10)
    ex, ey = struct.unpack_from("<2f", mcf, 0x14)
    return flags, ex, ey, [struct.unpack_from("<4f", mcf, 0x30 + 16 * k) for k in range(8)]


def _nubes_de(capturas, n_tex_terreno, mcf, cuentas, tam_zdd, capas, cielo=0):
    """Partes pegadas al terreno (textura >= las del terreno): primeras 3 ventanas (132 B = firma para hallarlas en la
    memoria del guest) y su capa UV: Node_DrawBackgroundLayers (EU 0x306320) pone los canales 6..9 (capas 0..3 del
    #MCF) a los dos modelos de fondo antes de dibujarlos. En el alta cada pieza (cielo, nubes) es UN modelo, asi que
    se le da su capa en orden de pieza (0 = cielo, 1 = nubes); el reparto por material de los 4 canales del original
    no esta localizado [H: si se ve una capa quieta o demasiado rapida, es esto]. `cielo` = indice de la pieza que
    Stage_DrawCameraFollowModel (EU 0x1E2880) dibuja trasladada con la camara (-1 = ninguna)."""
    out = []
    if mcf[:4] != b"#MCF" or not capas:
        return out
    lim, t = [], n_tex_terreno
    for c in cuentas:
        lim.append((t, t + c)); t += c
    for draws, windows in capturas:
        for d in draws:
            tex = d["part"].tex
            if tex < n_tex_terreno or d["A"][1] < 3:
                continue
            # DS86: la textura tiene que estar DENTRO del banco de una pieza de fondo (cielo o nubes). Antes se
            # aceptaba cualquier indice >= las del terreno y, si no caia en ninguno, se daba por cielo (k=0): las
            # texturas de la escenografia del escenario BASE (Llanuras: mar y montanas azules, #AMT 7..18) entraban
            # como cielo con `sigue`=1 y el port las trasladaba con la camara; se veian como mesetas AZULES planas
            # detras del terreno (900, 26/10). El original no las trata como capas de fondo.
            k = next((k for k, (lo, hi) in enumerate(lim) if lo <= tex < hi), None)
            if k is None:
                continue
            capa = min(3, k)
            u, v, px, py = capas[capa]
            a0, n = d["A"]
            out.append(("".join("%02X" % x for x in b"".join(windows[a0:a0 + 3])), n, u, v, tam_zdd,
                        px, py, capa, 1 if k == cielo else 0))
    return out


def poner_nubes(carpeta, campo):
    """stage.toml: una linea `nubes = "firma,n,du,dv,tam,px,py,capa,sigue"` por parte de cielo/nubes;
    src/escenarios.cpp mueve sus UV (Node_ScrollBackgroundUV) y, si `sigue` = 1, su posicion con la camara
    (Stage_DrawCameraFollowModel, EU 0x1E2880)."""
    lineas = ['nubes = "%s,%d,%.7f,%.7f,%d,%.7f,%.7f,%d,%d"' % n for n in NUBES.get(campo, [])]
    if lineas:
        t = carpeta / "stage.toml"
        t.write_text(t.read_text(encoding="utf-8") + chr(10).join(lineas) + chr(10), encoding="utf-8")


def poner_setup(carpeta, campo):
    """stage.toml: `setup = "Stage_..."` con la funcion de montaje del escenario base de IW (tabla EU 0x44B1D0).

    DS63: sirve para no inventar clima. Los campos de minijuego usan el paquete directo (MiniGame_LoadStageModel,
    EU 0x296DE0), no Stage_SetupObjects; su base (CAMPOS[campo][1]) es un escenario de combate cuyo montaje dice si
    lleva clima (WithClimate 0x1E1E90, WithSandDust 0x1E23C0, LensFlareB 0x1E24E0...). GlobalVar1/s_Trs_Ground = sin
    clima: el port no escribe `clima`/`polvo`/`destello` para los 9 campos (no hay nada que ver de clima en ellos).
    """
    base = CAMPOS[campo][1]
    setup = SETUP_POR_PAQUETE.get(base, "Stage_SetupObjects")
    t = carpeta / "stage.toml"
    t.write_text(t.read_text(encoding="utf-8") +
                 'setup = "%s"   # DS63: montaje del escenario base de IW (tabla EU 0x44B1D0)\n' % setup,
                 encoding="utf-8")


def poner_capas(carpeta, campo):
    """stage.toml: `capas = "flags,ext_x,ext_y"` (envoltura del #MCF) y una `capa = "L,u,v,px,py"` por capa.
    Las usa src/escenarios.cpp (Node_ScrollBackgroundUV, EU 0x306570); antes solo se llevaba un par suelto como
    deriva de las nubes, inventada: la base del #MCF se suma CADA CUADRO en el original."""
    capas = NUBES.get(("capas", campo))
    if not capas:
        return
    flags, ex, ey, ls = capas
    t = carpeta / "stage.toml"
    txt = t.read_text(encoding="utf-8")
    txt += 'arrastre = %s   # Node_ScrollBackgroundUV recibe el movimiento de la camara (0x366B10) solo en los campos de vuelo\n' % (
        "true" if campo in VUELO else "false")
    txt += 'capas = "%d,%.1f,%.1f"   # #MCF +0x10 flags, +0x14/+0x18 extension\n' % (flags, ex, ey)
    for k, (u, v, px, py) in enumerate(ls):
        txt += 'capa = "%d,%.7f,%.7f,%.7f,%.7f"\n' % (k, u, v, px, py)
    t.write_text(txt, encoding="utf-8")


def poner_dinamicos(carpeta, campo):
    """stage.toml: `dinamico = "modelo,rejilla,x,y,z,nombre"` por objeto de la lista del campo (#MIF; el original los
    crea con Stage_SpawnObjectList EU 0x306B00 y los anima por nodo, Stage_UpdateObjAnims EU 0x3067E0). El port los
    pinta en su minijuego (iw_minijuegos.cpp: geiseres del 900; iw_mini_vuelo.cpp: la casa del 881/883)."""
    lineas = ['dinamico = "%d,%d,%.3f,%.3f,%.3f,%s"' % d for d in NUBES.get(("din", campo), [])]
    if lineas:
        t = carpeta / "stage.toml"
        t.write_text(t.read_text(encoding="utf-8") + "\n".join(lineas) + "\n", encoding="utf-8")


def _mcf_de(b, campo):
    """#MCF del campo -> NUBES[("capas", campo)] = (flags, ext_x, ext_y, [(u, v, px, py) x 8])."""
    m = next((h for h in b if h[:4] == b"#MCF"), None)
    if m is None:
        return
    c = capas_de(m)
    if c:
        NUBES[("capas", campo)] = c


def _dinamicos_de(b, campo):
    """#MIF del campo = lista de objetos que el original crea (Stage_ParseResourceBundle EU 0x307060 enruta la entrada
    con nombre MIF a Stage_SpawnObjectList EU 0x306B00): +4 numero; registros de 0x30 desde +0x10 con +0 modelo
    (u16, indice del modelo del paquete), +2 rejilla (u16), +4/+8/+0xC x,y,z (floats), +0x10 cuaternion de giro
    (16 B) y +0x20 nombre (16 B; el original crea el objeto por nombre). 900 = las 6 torres LAVAFIRE_TOWER1..6."""
    out = []
    for h in b:
        if h[:4] != b"#MIF" or len(h) < 0x20:
            continue
        n = le(h, 4)
        for k in range(min(n, 64)):
            o = 0x10 + 0x30 * k
            if o + 0x30 > len(h):
                break
            mod, rej = struct.unpack_from("<2H", h, o)
            x, y, z = struct.unpack_from("<3f", h, o + 4)
            nom = h[o + 0x20:o + 0x30].split(b"\0")[0].decode("latin1")
            out.append((mod, rej, x, y, z, nom))
    if out:
        NUBES[("din", campo)] = out


def _uvanim_de(e, campo):
    """#MTA del campo = animacion UV de un modelo (Stage_ParseResourceBundle EU 0x307060 -> Stage_SetUvAnim
    EU 0x306C30; la evalúa UvAnim_EvalAllChannels 0x1BAD80 cada cuadro con Stage_UpdateObjAnims 0x3067E0).

    Formato: +4 numero de canales (16), +8 offset de la tabla; canal = 0x80 B con 6 descriptores de 16 B
    [+0 claves (i32, -1 = ninguna), +4 cuadros del bucle, +8 offset de las claves, +0xC valor por defecto] y +0x60
    numero de descriptores. Clave = 16 B {+0 cuadro i32, +4 valor f32} (UvAnim_RelocateData 0x1BB120 avanza 0x10 y
    UvAnim_EvalChannel 0x1BAB60 lee piVar10[1] con paso de 4 enteros). Los dos ultimos descriptores (4 y 5) son la
    deriva u/v por cuadro (lo que UvAnim_EvalChannel suma al estado); los cuatro primeros, la matriz UV.

    La entrada del #AMB dice el destino en su 4.o campo como (indice<<16)|tipo, y Stage_SetUvAnim (l.394369) SOLO
    atiende los tipos 2, 3, 4 y 5 (canal 0, 1, 2+indice y 0x12+indice); con cualquier otro valor vuelve sin hacer
    nada. Medido: 881 = 0x10005 (tipo 5, indice 1), 900 = 0x50004 y 0x4/0x10004/0x20004/0x30004/0x40004 (tipo 4) y
    880 = 0x0 (tipo 0) -> el #MTA del casco de la nave de Goku NO se reproduce en el propio IW: no hay animacion
    que portar ahi. Se guardan solo los canales con claves: `uvanim = "tipo,indice,c1:cuadros1+c2:cuadros2"`."""
    lineas = []
    mascara = 0
    canales = {}          # indice de canal -> 6 descriptores (nk, bucle, defecto, [(cuadro, valor)])
    n, t = struct.unpack_from("<2I", e, 0x10)
    for k in range(n):
        o, s, _w3, w4 = struct.unpack_from("<4I", e, t + 16 * k)
        if (w4 & 0xFFFF) not in (2, 3, 4, 5) or s == 0:   # Stage_SetUvAnim l.394375-394392: otros tipos no hacen nada
            continue
        h = e[o:o + s]
        if h[:4] != b"#MTA" or len(h) < 0x80:
            continue
        nch, off = le(h, 4), le(h, 8)
        if nch < 1 or nch > 64 or off + 0x80 * nch > len(h):
            continue
        con = []
        for c in range(nch):
            oo = off + 0x80 * c
            if oo + 0x64 > len(h):
                break
            frames = 0
            descs = []
            for d in range(6):                        # 6 descriptores de 16 B: claves, bucle, offset, defecto
                nk, loop, koff = struct.unpack_from("<3i", h, oo + 16 * d)
                defecto = struct.unpack_from("<f", h, oo + 16 * d + 0xC)[0]
                claves = []
                if 0 < nk <= 4096 and koff > 0 and koff + 16 * nk <= len(h):
                    for q in range(nk):
                        f, v = struct.unpack_from("<if", h, koff + 16 * q)
                        claves.append((f, v))
                else:
                    nk = 0
                if loop > 0 and loop > frames:
                    frames = loop
                descs.append((nk, max(0, loop), float(defecto), claves))
            if frames or any(d[0] for d in descs):
                canales[c] = descs
                if c < 16:
                    mascara |= 1 << c
                con.append("%d:%d" % (c, frames))
        if con:
            lineas.append((w4 & 0xFFFF, w4 >> 16, "+".join(con)))
    if lineas:
        NUBES[("uv", campo)] = lineas
        NUBES[("uvfull", campo)] = (mascara & 0xFFFF, canales)


def poner_uvanim(carpeta, campo):
    """stage.toml: `uvanim = "modelo_slot,modelo_idx,canales"` (animacion UV del #MTA); src/escenarios.cpp la registra
    (s_Dynamic) y avisa de quien debe pintarla si el modelo no viaja en el alta (los geiseres del 900 los pinta
    iw_minijuegos.cpp con ps2::Escena)."""
    lineas = ['uvanim = "%d,%d,%s"' % t for t in NUBES.get(("uv", campo), [])]
    full = NUBES.get(("uvfull", campo))
    if full:
        # DS73: objeto UV animado del escenario. Stage_CreateAnimUVObject (EU 0x1E2ED0) siempre toma la entrada (0,1)
        # y le pone el flag UV; los canales salen del #MTA (UvAnim_EvalAllChannels 0x1BAD80). `uvobjeto` =
        # "entrada,mascara"; `uvcanal` = "canal;nk,bucle,defecto,cuadro=valor|...;...x6".
        mascara, canales = full
        sig, nwin = NUBES.get(("uvsig", campo), ("", 0))
        # Solo se aplica a vertices cuando el #MTA tiene UN canal: el reparto canal->material del objeto
        # (necesario con varios canales) no esta localizado [H]. Con varios, el port evalua el estado y no
        # toca el modelo (asi no se mueve una textura que no le toca).
        if sig and mascara and (mascara & (mascara - 1)) == 0:
            lineas.append('uvobjeto = "1,%d,%d,%s"' % (mascara, nwin, sig))
        else:
            lineas.append('uvobjeto = "1,%d"' % mascara)
        for c in sorted(canales):
            partes = []
            for nk, loop, defecto, claves in canales[c]:
                kv = "|".join("%d=%g" % (f, v) for f, v in claves)
                partes.append("%d,%d,%g,%s" % (nk, loop, defecto, kv))
            lineas.append('uvcanal = "%d;%s"' % (c, ";".join(partes)))
    if lineas:
        t = carpeta / "stage.toml"
        t.write_text(t.read_text(encoding="utf-8") + "\n".join(lineas) + "\n", encoding="utf-8")


def poner_niebla(carpeta, mcf):
    """Niebla del #MCF del campo -> `niebla = "RRGGBB,ini,fin"` en su stage.toml.

    #MCF: +0x1C color 0x00BBGGRR; +0x20/+0x24 los dos floats de la niebla; +0x28/+0x2C inicio y fin.
    Stage_ParseResourceBundle (EU 0x307060) copia todo a iRam0049cdb8+0x63c..0x64c y el dibujo del campo llama a
    FUN_0012c920(color), FUN_0012c960(+0x20,+0x24) y FUN_0012c930(+0x28,+0x2C).

    DS88: los dos floats +0x20/+0x24 NO se escriben en la entrada de niebla de B3 (+4/+8). IW los guarda
    COMPLEMENTADOS: FUN_0012c960 (iw_sles55347.c) hace `fRam0049c65c = 255 - a` y `INT_0049be34 = 255 - b`; B3
    (sub_820856B8) copia sus dos floats tal cual y el pase de dibujo los usa como color de fondo. Copiar los
    valores crudos del #MCF (el 900 trae 10/255) dejaba el fondo / la niebla lejana en azul (0,63,187): las
    "mesetas azules" del horizonte no eran una textura del mod (sus AZT no tienen azul) sino ese color de fondo
    plano detras del terreno. Las entradas nativas de B3 usan 0/255 y el mod v13 no escribia esos dos floats
    (los dejaba como el anfitrion): eso es lo que se restaura. Sin #MCF (880) se usan los valores por defecto del
    original (World_CharactersMiscContent EU 0x307410 l.394839-394844), pero sin tocar +4/+8."""
    if mcf[:4] == b"#MCF":
        c = le(mcf, 0x1C)
        ini, fin = struct.unpack_from("<2f", mcf, 0x28)
        r, g, bl = c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF
    else:
        r = g = bl = 0xFF
        ini, fin = 1000.0, 2000.0
    linea = 'niebla = "%02X%02X%02X,%.0f,%.0f"     # #MCF: color,ini,fin (DS88: +4/+8 de B3 no se tocan)\n' % (
        r, g, bl, ini, fin)
    t = carpeta / "stage.toml"
    t.write_text(t.read_text(encoding="utf-8") + linea, encoding="utf-8")


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

# DS147 (27/10): el mar de los campos de vuelo no puede acabar en un borde. El original dibuja el objeto 0 (suelo)
# repetido alrededor de la camara (Stage_DrawTiledGround EU 0x1E2590: `iVar4 < 0x2f` = 47 copias con los
# desplazamientos de la tabla 0x44b5b0, una rejilla de 450) y el #MCF pide envolver en x y z (flags 0xB) con
# losetas de 4000 (ext +0x14); la niebla del #MCF (881: 0x87FFFF, 2400..3700) no lo tapa en el port: medida sobre
# la captura del coordinador (b_vuelo, 27/10 08:09, jugador en (5014, 11294), borde a ~700 u) el agua del borde
# conserva su textura, no el color de la niebla. El terreno del mod SI cubre el mundo (348 huesos en rejilla de
# 4000: +-12000), pero el mundo de v1 mide 24000 (kPeriodo en iw_mini_vuelo.cpp) y el recorrido llega a +-11000:
# el borde queda a la vista. Se agranda UNA loseta de agua (tex 0 del #AMT del terreno, 256x256 azul con olas, local
# +-2000) x45 (+-90000) y se baja 1 u (DS173: solo la capa de abajo): el plano original manda en el interior y fuera del mundo
# solo se ve esta loseta, que pasa el plano lejano del juego (mgr+0x24 = 65535) desde cualquier punto del recorrido
# (borde a >=78000).
# DS149 (27/10): la UV se escala por el MISMO factor (u/v x45). Sin eso la textura queda estirada 45x y el agua de
# fuera del mundo se lee lisa y mas clara que la de dentro: en la prueba del coordinador (0x7E0, b_vuelo, 27/10
# 08:58, jugador (3546, 11881), a 119 u del borde z=12000) el borde era una franja de agua clara y lisa contra el
# agua oscura con olas. La densidad de texel del #AMO es una repeticion cada ~500 u (u 0..2 por tramo de 1000,
# fase = x/500, la misma en toda la rejilla); con u/v x45 se conserva dentro y fuera y la union no se distingue.
# (DS171: la bajada pasa a 1 u; ver el bloque DS171 de abajo.)
# DS150 (27/10): con la UV ya igualada quedaba una franja de OTRO TONO en el borde. El mar de 881 son DOS capas de
# lasetas planas: la de arriba (vtype 0x824008b6, mundo y=0, color de vertice uniforme 0,898/0,898/0,827) y la de abajo
# (vtype 0x20000b6, mundo y=-10, color variable 0,70..0,85; media 0,763). Dentro del mundo se ve la de arriba; fuera
# solo se ve la loseta agrandada, que es la de abajo -> tono mas oscuro (medido en b_vuelo 09:45: dentro 104/158/204,
# fuera 87/145/202; el cociente 1,18 es exactamente 0,898/0,763). Se copiaba el color de vertice de la capa de arriba
# a la loseta agrandada. Normales no hay (vtype & 1 = 0) y el alfa de vertice es 1 en las dos.
# DS171 (27/10): la "raya diagonal" que seguia en 0x7E0/0x7D6 NO era otra malla, ni una capa del 883/881, ni el plano
# lejano: era la propia loseta x45, por dos causas medidas en el bin instalado (v30):
#  (1) DS149 escalaba la UV guardada x45. La UV del agua esta cuantizada a 1/256 y cada tramo de 1000 u guarda 2
#      repeticiones (881) o 3 (883), con copias de costura; x45 multiplica ese error y cambia la fase
#      (45*c mod 1). En el #AWO v30 la loseta tenia UV 0,083/89,82 entre vertices consecutivos de 45.000 u (densidad
#      correcta) pero en el borde del mundo su fase era 0,394 (u) / 0,460 (v) frente al 0,002/0,003 de la rejilla del
#      mundo: casi media repeticion de desfase -> la costura recta.
#  (2) Solo se agrandaba la capa de ABAJO y se bajaba 35 u: dentro se ve la capa de ARRIBA (la opaca) y fuera se veia
#      la de abajo 45 u mas baja, asi que la textura saltaba en la linea (hasta ~20 px de paralaje con el borde a 2.000
#      u) y ademas cambiaba la capa visible. La raya era ese escalon, no el color.
# v31 agrandaba las DOS capas de la celda central; DS173 agranda solo la de ABAJO (la opaca) con el color de la de
# arriba, y la UV se RECALCULA desde la
# posicion escalada con la recta del agua (|du/dx| = 2/1000 en 881 y 3/1000 en 883; signo por la esquina), en vez de
# multiplicar la UV guardada. La densidad y la fase son las de la rejilla del mundo: medido en el #AMO generado, en el
# borde (+-12000 en 881, +-8000 en 883) la fase coincide con la de las losetas vecinas dentro de 0,006 de repeticion.
# DS173: agrandar tambien la capa de arriba la dejaba 1 u por debajo de la tapa e interceptaba su mezcla alfa dentro
# del mundo; y la de abajo conservaba el degradado de color de una celda de 4000 u estirado (bandas de 45.000 u). Ahora
# fuera solo se ve la de abajo (opaca) con el color uniforme de la de arriba (0,898) y la bajada de 1 u: el agua original
# queda 1 u por encima y gana el z-test dentro del mundo; fuera el escalon proyecta <1 px.
MAR_K = 45.0
MAR_BAJO = 1.0


def _uv_agua(p):
    """Recta UV del agua plana: (su, cu, sv, cv) con u = su*x + cu, v = sv*z + cv en coordenadas locales.

    La UV del #AMO esta cuantizada (1/256) y los tramos de 1000 u guardan 2 repeticiones (881) o 3 (883), con copias
    de costura que alternan el signo; se toma el modulo de la mediana (redondeado a entero) y el signo del extremo
    -x/-z, y las constantes de ese vertice de esquina. Asi la loseta agrandada conserva la densidad y la fase de la
    rejilla del mundo (ver DS171 en el comentario de arriba)."""
    def recta(eje, ic):
        vals = []
        for a in p.verts:
            for b in p.verts:
                if not a[3] or not b[3]:
                    continue
                d = b[1][eje] - a[1][eje]
                du = b[3][ic] - a[3][ic]
                if 900 <= abs(d) <= 1100 and 1.5 < abs(du) < 4.5:
                    vals.append(abs(du) / 1000.0)
        vals.sort()
        mag = round(vals[len(vals) // 2] * 1000) / 1000.0 if vals else 0.002
        xmin = min(v[1][eje] for v in p.verts)
        xmax = max(v[1][eje] for v in p.verts)
        imin = [v[3][ic] for v in p.verts if v[3] and abs(v[1][eje] - xmin) < 1]
        imax = [v[3][ic] for v in p.verts if v[3] and abs(v[1][eje] - xmax) < 1]
        signo = 1.0 if (not imin or not imax or sum(imax) >= sum(imin)) else -1.0
        return signo * mag
    su, sv = recta(0, 0), recta(2, 1)
    ref = min((v for v in p.verts if v[3]), key=lambda v: (v[1][0], v[1][2]))
    return su, ref[3][0] - su * ref[1][0], sv, ref[3][1] - sv * ref[1][2]


def _color_medio(p):
    """Color RGBA medio de los vertices con color de una parte (None si no tiene)."""
    vs = [v[4] for v in p.verts if v[4]]
    if not vs:
        return None
    return [sum(c[k] for c in vs) / len(vs) for k in range(4)]


def _celda_mundo(p, world):
    """(x0, x1, z0, z1) que ocupa la parte en coordenadas de mundo (posicion del hueso + vertices)."""
    pos = world[p.bone][:3, 3]
    xs = [v[1][0] for v in p.verts]
    zs = [v[1][2] for v in p.verts]
    return (float(pos[0]) + min(xs), float(pos[0]) + max(xs),
            float(pos[2]) + min(zs), float(pos[2]) + max(zs))


def _color_borde_agua(m, world, ref):
    """Color RGBA medio de las capas de agua OPACAS del BORDE del mundo.

    El agua del terreno tesela el mundo con celdas planas de 4000 u; las que tocan el borde (x o z en el extremo
    de la malla) son las que quedan justo al lado de la loseta agrandada y, por tanto, las que fijan el tono con el
    que tiene que casar. Se toman las partes del mismo tipo que `ref` (la capa opaca) y se pondera por vertices (cada
    celda aporta lo que mide su malla; las celdas de costa son parciales). None si no hay celdas."""
    celdas = []
    for q in m.all_parts:
        if q is ref or q.vtype != ref.vtype or q.tex != ref.tex or not q.verts:
            continue
        xs = [v[1][0] for v in q.verts]
        zs = [v[1][2] for v in q.verts]
        if max(xs) - min(xs) < 3900 or max(zs) - min(zs) < 3900:
            continue
        celdas.append((q, _celda_mundo(q, world)))
    if not celdas:
        return None
    x0 = min(c[1][0] for c in celdas)
    x1 = max(c[1][1] for c in celdas)
    z0 = min(c[1][2] for c in celdas)
    z1 = max(c[1][3] for c in celdas)
    tol = 40.0
    acc, n = [0.0] * 4, 0
    for q, (a0, a1, b0, b1) in celdas:
        if (abs(a0 - x0) > tol and abs(a1 - x1) > tol and abs(b0 - z0) > tol and abs(b1 - z1) > tol):
            continue                                  # celda interior: no toca el borde del mundo
        for v in q.verts:
            if v[4]:
                for k in range(4):
                    acc[k] += v[4][k]
                n += 1
    return [x / n for x in acc] if n else None


def _alfa_textura(amt, tex):
    """Media del alfa (0..1) de la textura `tex` del #AMT PS2. None si no se puede leer.

    Dato del fichero, no inventado: el material del agua es ALPHA_2 = 0x44 (Cs*As + Cd*(1-As)), asi que As es el
    alfa de la textura por texel y su media es el peso con el que la capa de arriba se mezcla con la de abajo."""
    if not amt:
        return None
    import amt_ps2  # noqa: PLC0415

    try:
        ents = amt_ps2.entries(amt, keep_empty=True)
        if tex >= len(ents) or not ents[tex]:
            return None
        a = amt_ps2.decode(amt, ents[tex])[:, :, 3]
        return float(a.mean()) / 255.0
    except Exception:                                  # noqa: BLE001 (un #AMT raro no puede romper el alta)
        return None


def mar_sin_borde(amo, amt=None):
    """Agranda la capa de agua OPACA de la celda central del #AMO del terreno (ver MAR_K, DS171, DS173 y DS176).
    Devuelve los bytes nuevos; si no hay una parte de agua plana de 4000x4000 devuelve el original (no rompe el alta).

    DS173: en 881 la celda tiene dos capas (abajo opaca 0x20000B6, arriba alfa 0x824008B6). Se agranda SOLO la de
    abajo: agrandar tambien la de arriba dejaba una copia a 1 u por debajo de la tapa, que intercepta su mezcla alfa
    dentro del mundo, y la capa de abajo conservaba el degradado de color de una celda de 4000 u estirado x45
    (0,636..0,898 = las bandas diagonales claras del mar, franjas de 45.000 u). En 883 hay una sola capa y se queda
    tal cual (0x7D6 no se toca).

    DS176: el color de fuera NO es el de la capa de arriba, es la MEZCLA de las dos capas que se ve dentro del mundo.
    La de arriba es SEMITRANSPARENTE (ALPHA_2 = 0x44; As = alfa de la textura del agua, media 0,1265 medida en el
    #AMT) y sobre ella manda el color de la de abajo. Copiar el color de la de arriba (0,898/0,898/0,827, DS173)
    dejaba fuera una loseta mas clara y menos azul: medido en la captura 20261027-192115_opus2__r12_vuelo, b_vuelo y
    c_control, fuera 99/160/197 (B/R 1,99) y dentro 93-95/151-155/209-220 (B/R 2,32). Se pone la mezcla
    a*arriba + (1-a)*abajo con a = alfa medio de la textura y `abajo` = color medio de las capas opacas que tocan el
    borde del mundo (0,801/0,818/0,836 en el 881; B/R 1,04 -> 2,33, el de dentro)."""
    import amo2awo  # noqa: PLC0415

    b = bytearray(amo)
    m = amo2awo.PS2AMO(bytes(amo)).amgs[0]
    world = []
    for i in range(m.nb):
        o = m.a + m.axes_rel + 80 * i
        q = [amo2awo.lef(m.b, o + 4 * k) for k in range(4)]
        t = [amo2awo.lef(m.b, o + 16 + 4 * k) for k in range(3)]
        par = amo2awo.le(m.b, o + 0x40)
        local = amo2awo._qmat(q, t)
        pi = (par - m.axes_rel) // 80 if par else -1
        world.append(world[pi] @ local if 0 <= pi < i else local)
    cand = []
    for p in m.all_parts:
        if p.tex != 0 or not p.verts:
            continue
        ys = [v[1][1] for v in p.verts]
        xs = [v[1][0] for v in p.verts]
        zs = [v[1][2] for v in p.verts]
        if max(ys) - min(ys) > 1 or max(xs) - min(xs) < 3900 or max(zs) - min(zs) < 3900:
            continue
        pos = world[p.bone][:3, 3]
        cand.append((abs(float(pos[0])) + abs(float(pos[2])), p))
    if not cand:
        return bytes(amo)
    cand.sort(key=lambda c: c[0])
    p = cand[0][1]
    # DS173: el agua de 881 tiene dos capas en la misma celda (mismo x/z, solo cambia la altura: abajo opaca, arriba
    # alfa) y la de 883 una sola. Se agranda SOLO la de abajo (p) y se le pone un color uniforme (DS176: la mezcla);
    # la de arriba no se agranda (ver mar_sin_borde).
    comp = None
    y0 = float(world[p.bone][1, 3]) + p.verts[0][1][1]
    bx, bz = float(world[p.bone][0, 3]), float(world[p.bone][2, 3])
    for q in m.all_parts:
        if q is p or q.tex != p.tex or not q.verts or len(q.verts) != len(p.verts) or not (q.vtype & 2):
            continue
        if abs(float(world[q.bone][0, 3]) - bx) > 1.0 or abs(float(world[q.bone][2, 3]) - bz) > 1.0:
            continue                                # otra celda: la malla es identica y no vale
        yq = float(world[q.bone][1, 3]) + q.verts[0][1][1]
        if yq <= y0 + 0.5:
            continue
        dx = max(abs(v[1][0] - w[1][0]) for v, w in zip(p.verts, q.verts))
        dz = max(abs(v[1][2] - w[1][2]) for v, w in zip(p.verts, q.verts))
        if dx > 2.0 or dz > 2.0:
            continue
        if comp is None or yq > comp[0]:
            comp = (yq, q)
    # DS173: solo se agranda p (la capa de ABAJO, opaca). Si hay companera (881), se le pone un color uniforme
    # para que fuera del mundo el agua tenga el mismo tono que dentro y no se estire el degradado de color de una
    # celda de 4000 u (bandas diagonales de 45.000 u). La companera NO se agranda: a 1 u por debajo de la tapa
    # interceptaba la mezcla alfa dentro del mundo. En 883 (sin companera) se conserva su color.
    # DS176: el color que se pone es la MEZCLA (alfa de la textura) de la capa de arriba con el color medio de las
    # capas opacas del borde del mundo, que es lo que se ve dentro; antes se copiaba solo el de la de arriba.
    col = None
    if comp is not None:
        top = _color_medio(comp[1])
        base = _color_borde_agua(m, world, p)
        a = _alfa_textura(amt, p.tex)
        if top is not None and base is not None:
            if a is None:
                a = 0.0                             # sin #AMT legible: el color de abajo tal cual (dato del bin)
            col = [a * top[k] + (1.0 - a) * base[k] for k in range(4)]
    for c in [p]:
        su, cu, sv, cv = _uv_agua(c)
        uv = 16 + (16 if c.vtype & 1 else 0) + (16 if c.vtype & 2 else 0)   # UV tras posicion/normal/color
        cc = 16 + (16 if c.vtype & 1 else 0)                                # color RGBA tras posicion (y normal)
        for off, xyz, _nrm, _tuv, _col in c.verts:
            X, Y, Z = xyz[0] * MAR_K, xyz[1] - MAR_BAJO, xyz[2] * MAR_K
            struct.pack_into("<3f", b, m.a + off, X, Y, Z)
            struct.pack_into("<2f", b, m.a + off + uv, su * X + cu, sv * Z + cv)
            if col is not None and (c.vtype & 2):
                struct.pack_into("<4f", b, m.a + off + cc, *col)
        struct.pack_into("<3f", b, c.off + 0x10, c.center[0] * MAR_K, c.center[1] - MAR_BAJO, c.center[2] * MAR_K)
        struct.pack_into("<f", b, c.off + 0x1C, c.radius * MAR_K)
    return bytes(b)


# DS167 (27/10) quito la fila de niebla del cielo del 881 copiandole el color del anillo, creyendo que era un defecto.
# DS173 (27/10) lo mide contra el original (video 0x7E0, 1946 s): la bruma turquesa del horizonte ES esa fila. En el
# fotograma, el cielo justo encima del agua y la bruma/agua del horizonte valen (110,206,220) y (115,212,221);
# 140/247/247 escalado por el video (0,82-0,89) da (115..125, 203..220, 220..231) -> coincide; el anillo 65/175/255
# daria (53..65, 144..175, 255) -> R y G muy por debajo. Ademas el agua del campo es ALFA (materiales 3/4 del #AWG
# del terreno, ALPHA_2 = 0x44 = Cs*As + Cd*(1-As); textura del agua con alfa media 32) y el cielo queda DETRAS del
# agua: al pintar el cielo con el color de niebla, el agua del horizonte se tine de turquesa como en el original.
# Medido en los #AMO del cielo (`hijo 3`) y en el #AWO convertido (ventanas de 44 B, color ARGB en +32):
#   881: filas y=-1689,3 y -756,7 = 0x8CF7F7 (140/247/247) ~ #MCF 0x87FFFF; anillo y=175,9 = 0x41AFFF (65/175/255).
#   883: filas y=-1687,3 y 3,0 = 0xF8FC66 (248/252/102) = #MCF 0x66FCF8; anillo y=832,8 = 0xBBFF63.
# Por eso se RESTAURA la fila del 881 (conjunto vacio = no se toca el cielo). 883 ya la tenia igual a su cielo y no se
# toca (0x7D6 queda byte a byte). NO se escribe la niebla +4/+8 de la entrada de B3 (DS88/DS167): eso ponia el fondo
# entero turquesa.
CIELO_SIN_BANDA = set()


def cielo_sin_banda(amo):
    """Copia el color del anillo del horizonte a las filas de niebla del cielo (ver CIELO_SIN_BANDA).

    La parte del cielo es la que no lleva textura (tex 0xFFFFFFFF) y tiene color por vertice (vtype & 2). Sus vertices
    estan agrupados en filas horizontales; las de niebla son las mas bajas (las que comparten el color de la fila mas
    baja) y el anillo es la primera fila por encima de ellas. Devuelve los bytes nuevos; si no encuentra esa forma (o
    la fila de niebla ya es igual al anillo) devuelve el original."""
    import amo2awo  # noqa: PLC0415

    b = bytearray(amo)
    for g in amo2awo.PS2AMO(bytes(amo)).amgs:
        for p in g.all_parts:
            if p.tex != 0xFFFFFFFF or not (p.vtype & 2) or (p.vtype & 4) or not p.verts:
                continue
            filas = {}                                  # altura (redondeada) -> [(off, color)]
            for off, xyz, _nrm, _uv, col in p.verts:
                filas.setdefault(round(xyz[1], 1), []).append((off, col))
            ys = sorted(filas)
            if len(ys) < 3:
                continue
            niebla = filas[ys[0]][0][1]
            if any(max(abs(niebla[k] - c[k]) for k in range(3)) > 0.02 for _o, c in filas[ys[0]]):
                continue                                # la fila de abajo no es uniforme: no es el cielo
            anillo = next((y for y in ys[1:] if max(abs(niebla[k] - filas[y][0][1][k]) for k in range(3)) > 0.02), None)
            if anillo is None:
                continue                                # todas las filas del mismo color: nada que hacer
            color = filas[anillo][0][1]
            cc = 16 + (16 if p.vtype & 1 else 0)        # color RGBA tras posicion (y normal si la hay)
            for y in ys:
                if y >= anillo:
                    break
                for off, _c in filas[y]:
                    struct.pack_into("<4f", b, g.a + off + cc, *color)
            return bytes(b)
    return bytes(amo)
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
    Llanuras; RE de sub_820BF558: tabla de 32 B por sub-malla).

    RE del cielo de IW (25/10): no lo monta Stage_BeginLoad (0x1E1AB0 solo guarda el ID); s_Ground (0x1E2B20) carga
    DOS objetos del escenario con MapLoader: el objeto 0 (suelo, dibujo 0x1E2590, update 0x1E2750 Stage_UpdateUVScroll)
    y el objeto 1 (0x83000000, dibujo 0x1E2880 Stage_DrawCameraFollowModel, update 0x1E29A0 Stage_UpdateUVScroll2).
    Ese objeto 1 es el cielo/mar: Stage_DrawCameraFollowModel le suma la X/Z de la CAMARA (sigue al jugador) y
    Stage_UpdateUVScroll2 deriva las UV. En el alta de B3 no hay callback de dibujo por objeto: el cielo va pegado al
    terreno y queda quieto. En 883 su caja es +-12000 (nubes +-17400) y el campo +-8000, asi que la camara no sale de
    el; la diferencia que queda (horizonte tapado/delante) es de estado de dibujo, no de posicion.
    [VERIFICAR] en pantalla 0x7D7/0x7D8: si el cielo se ve mal, el arreglo es mover en runtime las ventanas del cielo
    con la X/Z de la camara (como TickNubes de src/escenarios.cpp hace con las UV); eso toca escenarios.cpp, no este
    conversor."""
    import amo2awo  # noqa: PLC0415 (awo_tools, en el path desde escenarios_convertir)
    import amt_ps2  # noqa: PLC0415
    _titulo, base, _tema, piezas, k = CAMPOS[campo]
    piezas = piezas[:1] if solo_terreno else piezas
    f = hijos(afs.entry(campo))
    b = hijos(afs.entry(base))
    (a0, t0), resto = piezas[0], piezas[1:]
    if campo in VUELO:   # DS147: el mar no termina en el borde del mundo (ver MAR_K)
        f[a0] = mar_sin_borde(f[a0], f[t0])   # DS176: el #AMT da el alfa (peso) de la capa de arriba
    adjuntos, desde = [], le(f[t0], 0x10)         # banco comun: terreno, luego cada extra (indices n, n+1...)
    for ki, (a, t) in enumerate(resto):
        d = f[a]
        if ki == CIELO.get(campo, -1) and campo in CIELO_SIN_BANDA:   # DS173: vacio (la fila de niebla es del original)
            d = cielo_sin_banda(d)
        adjuntos.append((sin_raiz_del_terreno(d, f[a0]), desde))
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

    capturas = []                         # DS86: solo las del terreno con el cielo y las nubes pegados

    def convertir_amo(d, report=None, attach=()):
        if d == terrenos[0]:
            ini = len(amo2awo.build_awg_todos)
            r = conv_amo(d, report, adjuntos)
            capturas.extend(amo2awo.build_awg_todos[ini:])
            return r
        return conv_amo(d, report, attach)
    EC.bloques_mdd, EC.ps2hd.convert_block = bloques, convertir
    amo2awo.convert_amo = convertir_amo
    amo2awo.attach_rigid = lambda a, e, t: pegar(a, e, t, head=0)      # hueso 0 = raiz del campo
    amo2awo.build_awg_todos.clear()
    try:
        zdd = EC.mdd_a_zdd(mdd, pistas_min=3)
    finally:
        EC.bloques_mdd, EC.ps2hd.convert_block = viejo, conv
        amo2awo.convert_amo, amo2awo.attach_rigid = conv_amo, pegar
    _e = afs.entry(campo)
    _mcf_de(hijos(_e), campo)
    _capas = NUBES.get(("capas", campo))
    NUBES[campo] = _nubes_de(capturas, le(f[t0], 0x10), hijos(_e)[0],
                             [le(f[t], 0x10) for _, t in resto], len(zdd), _capas[3] if _capas else [],
                             CIELO.get(campo, -1))
    # DS73: el objeto UV animado es la entrada (0,1) del escenario (Stage_CreateAnimUVObject 0x1E2ED0); en los campos
    # de IW es la pieza de cielo (la primera de las de fondo), que ya tiene firma/ventanas de `_nubes_de`.
    cielo = next((x for x in NUBES.get(campo, []) if x[8] == 1), None)
    if cielo:
        NUBES[("uvsig", campo)] = (cielo[0], cielo[1])
    _dinamicos_de(hijos(_e), campo)
    _uvanim_de(_e, campo)
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
        poner_capas(carpeta, c)
        poner_dinamicos(carpeta, c)
        poner_uvanim(carpeta, c)
        poner_setup(carpeta, c)
        print("campo %d '%s' -> %s (entradas %d y %d, %d B)" % (c, titulo, carpeta, entrada, entrada + 100, len(datos)))


# ---------------------------------------------------------------------------------------------------------
# auto: lo llama el launcher al pulsar Jugar (ModPipeline::BuildStages) con las carpetas del jugador. Crea en su
# carpeta mods los escenarios que faltan o cuya ISO cambio (sello en origen.txt) a partir de SUS juegos de PS2:
# los 6 campos de IW y los escenarios de B1/B2 probados en el juego. Nada del juego viaja en el Kit; un fallo no
# para nada (sale 0 y lo dice en el registro: el launcher juega igual).
# ---------------------------------------------------------------------------------------------------------
VERSION = 33  # 33 (DS176 27/10): el color de la loseta de mar de fuera (881) es la MEZCLA de las dos capas del agua,
              # no el de la de arriba. La de arriba es semitransparente (ALPHA_2 0x44; As = alfa de la textura, media
              # 0,1265) y dentro del mundo manda el color de la de abajo; copiar el de arriba (DS173) dejaba fuera una
              # loseta mas clara y menos azul (medido en b_vuelo/c_control del 27/10 19:21: fuera 99/160/197, B/R 1,99;
              # dentro 95/155/220, B/R 2,32). Ahora fuera = a*arriba + (1-a)*color medio de las capas opacas del
              # BORDE del mundo (0,801/0,818/0,836; B/R 1,04 -> 2,33). Ver mar_sin_borde(). 883 no cambia.
              # 32 (DS173 27/10): se RESTAURA la fila de niebla del cielo del 881 (0x8CF7F7 = 140/247/247). DS167 la
              # habia sustituido por el anillo (0x41AFFF) por creerla un defecto, pero el original (video 0x7E0 1946 s)
              # tiene justo esa bruma turquesa entre mar y cielo: cielo (110,206,220) y agua del horizonte (115,212,221),
              # frente al anillo (65,175,255) que daria R/G muy bajos. El agua del 881 es alfa (ALPHA_2 0x44; textura
              # con alfa media 32) y el cielo queda detras, asi que la fila de niebla tine tambien el agua del horizonte.
              # 883/0x7D6 no se toca (su fila ya es el color de su cielo; el bin sale byte a byte igual). No se escribe
              # la niebla +4/+8 de B3 (pondria todo turquesa). Ademas, en 881 el mar agranda SOLO la capa opaca de
              # abajo y le copia el color uniforme de la de arriba: agrandar la de arriba dejaba una copia a 1 u por
              # debajo de la tapa (interceptaba su alfa dentro del mundo) y la de abajo estiraba el degradado de color
              # de una celda de 4000 u a 45.000 (las bandas diagonales claras del mar). Ver mar_sin_borde().
              # 31 (DS171 27/10): el mar de 881/883 agranda las DOS capas de agua (no solo la de abajo), sin la
              # bajada de 35 (1 u) y con la UV RECALCULADA desde la posicion (recta de 2/1000 en 881 y 3/1000 en 883:
              # misma densidad y fase que la rejilla del mundo en el borde, +-12000 / +-8000). Quita la raya diagonal
              # del borde (fase 45*c y escalon de 45 u de v30). Ver mar_sin_borde()/_uv_agua().
              # 30 (DS167 27/10): el cielo del 881 lleva las filas de abajo pintadas con el color de la niebla del
              # #MCF (0x8CF7F7 ~ 0x87FFFF); como la niebla no llega a la geometria en el port, se veian como una
              # banda clara sobre el agua (b_vuelo 10:06: el cielo sube de 70/175/255 a 85/198/255 y el agua empieza
              # en 255). Se copia el color del anillo del horizonte (0x41AFFF) a esas filas. Solo 881: en 883 la fila
              # de niebla YA es el color del cielo y su malla sale byte a byte igual. Ver cielo_sin_banda().
              # 29 (DS150 27/10): la loseta de mar agrandada de 881 copia el COLOR DE VERTICE de la capa de arriba
              # (mundo y=0, 0,898/0,898/0,827): la de abajo (la que se agranda) es mas oscura (0,763) y el borde del
              # mundo se leia como dos tonos (b_vuelo 09:45: dentro 104/158/204, fuera 87/145/202). 883 es de una sola
              # capa: no cambia. Normales no hay (vtype & 1 = 0) y el alfa de vertice es 1 en ambas. Ver mar_sin_borde().
              # 27 (DS147 27/10): el mar de los campos de vuelo (881/883) se agranda x45 y se baja 35 u para que no
              # acabe en un borde recto a la vista (el original dibuja el suelo en losetas alrededor de la camara,
              # Stage_DrawTiledGround 0x1E2590, y la niebla del #MCF no lo tapa en el port: medido en la captura
              # b_vuelo del 27/10 08:09). Ver mar_sin_borde(). 26 (DS101 26/10): en 881/883 el cielo (3,4) sigue a la camara y las nubes (1,2) son estaticas. 25 25 (DS88 26/10): la niebla del #MCF ya NO copia +0x20/+0x24 a la entrada de B3 +4/+8. IW los
              # guarda complementados (FUN_0012c960: 255-x) y B3 los usa como color de fondo; el valor crudo del
              # 900 (10/255) ponia el fondo azul (0,63,187) = las mesetas azules del horizonte. Las `nubes` de
              # DS86 se quedan como estan. 24 (DS86 26/10): las `nubes` solas de la pieza de fondo del campo (terreno convertido + cielo/nubes
              # pegados) y solo si su textura cae en el banco de una de esas piezas; antes entraba la escenografia del
              # escenario BASE por indice de textura y se marcaba como cielo (mesetas azules detras del terreno del 900).
              # 23 (DS63 26/10): `setup` = funcion de montaje del escenario base de IW (tabla EU 0x44B1D0); los
              # campos de minijuego no pasan por Stage_SetupObjects (MiniGame_LoadStageModel) y su base GlobalVar1 /
              # s_Trs_Ground no trae clima, asi que no se escribe clima/polvo/destello. 22 (DS58 26/10): niebla completa del #MCF: los dos floats +0x20/+0x24 van a la entrada de niebla de B3
              # (+4/+8, sub_820C2058) y un campo sin #MCF (880) usa los valores por defecto del original (0x307410:
              # blanco, 0/255, 1000..2000) en vez de leer el #AMT hijo 0 (niebla negra 0..0). 21 (DS51 26/10): la pieza de cielo lleva `sigue` en su linea `nubes` (Stage_DrawCameraFollowModel
              # 0x1E2880: el cielo se dibuja trasladado a (camara.x, 0, camara.z)) y el #MTA solo se anota si su tipo es
              # 2..5 (Stage_SetUvAnim 0x306C30; el 0x0 del 880 no hace nada en el original). 20 (DS40 25/10): capas UV del #MCF completas (capas/capa/arrastre), lista de dinamicos (#MIF) y animacion UV
              # (#MTA) en stage.toml; las nubes llevan capa y arrastre (Node_ScrollBackgroundUV 0x306570). 19 (ZB 25/10): los B2 con cambio de zona (g0, g4, g7, g8) llevan las camaras de suceso como ultimo #ACC y zona_escena en stage.toml (escena de camara al cambiar de zona). 18 (X1esc 23/10): Base (g0) conserva TODAS las raices quietas, en su orden (los huesos de la zona B se numeran sobre todas): cambio de zona sin cierre. 17 (X1esc 23/10): registro de 641 casillas completado con las del 50/60/54 (sin ellas, cierre en peleas largas de los B2: nombre NULL en sub_8208DEE0); los B2 viejos se rehacen solos. 16 (X1esc): B2 g0 con 2 raices (cambio de zona del motor sin cierre). 15 (G02 studies 2): B2 grupo 1 sin la raiz #AMP que cierra al dibujar (lr 0x8208E5AC);
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
       # DS127: g1 ya entro en B12 (Torneo B2, X1esc 23/10). g5 SIGUE FUERA: su alta convierte y carga, pero el
       # suelo/mar de nubes sale NEGRO (86 % de la mitad inferior frente a 0 % del nativo; X1esc §2-4). Sus capas
       # 9/10/14 son blancas con alfa 1-4 (mezcla aditiva en PS2) y el material convertido no las reproduce en B3.
       # Ademas el bin NATIVO 58 del data_cmn del jugador tiene las mismas 16 texturas (permutacion exacta por media
       # RGBA/alfa) y un #AWO principal de 413016 B frente a 413760 B del convertido: es la misma Camara del Tiempo.
       # Por eso el tablero usa la 58 nativa (pasarela_b2.cpp, kEscenarioB2[2] = nullptr) y no se instala g5.
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
            poner_capas(carpeta, clave)
            poner_dinamicos(carpeta, clave)
            poner_uvanim(carpeta, clave)
            poner_setup(carpeta, clave)
        else:
            juego, grupo, nombre, rot, caja = next(x for x in B12 if x[:2] == clave)
            fuente = EC.IsoB2(None, juego)
            datos = EC.escenario_b2(fuente, grupo, escala=caja)
            carpeta = EC.instalar(datos, mods, nombre, EC.ENTRADA_BASE + (20 if juego == "b1" else 0) + grupo,
                                  EC.JUEGOS[juego][0], id_se=45, tema=16, rotulo_txt=rot,
                                  escena=EC.ESCENA_ZONA.get(grupo, 0) if juego == "b2" and not EC.os.environ.get("X1ESC_SIN_ESCENA") else 0)
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
