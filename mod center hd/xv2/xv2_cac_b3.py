#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xv2_cac_b3.py - C6: un HEROE armado con PIEZAS del creador de Xenoverse 2 -> bin HD de B3 (cuerpo propio).

Reutiliza xv2_a_b3 (E3): solo cambia de donde salen las partes. En vez de las de un personaje, se eligen por modelo:
  sel = {"raza":"HUM", "bust":300, "pants":300, "boots":300, "rist":300, "cara":1, "pelo":1, "tipo":0}
  hsv = {"piel":(h,s,v), "pelo":..., "torso":..., "piernas":..., "botas":..., "guantes":...}   (0,0,0 = color de la pieza)
Esqueleto comun = <RAZA>_000.esk; el color por defecto de cada pieza es el del partset de XV2 (BCS) que la usa.
Se lee del data.cpk de XV2 del jugador (solo lectura); nada se guarda en el repo.
  python xv2_cac_b3.py componer plantilla.bin salida.bin [--sel bust=300,pants=300,...] [--hsv pelo=30,0,0;...]
  python xv2_cac_b3.py previa salida.png [--sel ...]
"""
import argparse
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, "..", "cac"), os.path.join(HERE, "..", "..", "awo_tools")]
import numpy as np  # noqa: E402
import xv2_leer as L  # noqa: E402
import xv2_a_b3 as X  # noqa: E402

xm = X.xm
TIPOS = {"bust": "Bust", "pants": "Pants", "boots": "Boots", "rist": "Rist", "pelo": "Hair"}
CARA = ("FaceBase", "FaceEye", "FaceEar", "FaceForehead", "FaceNose")
SUF = {"Bust": "Bust", "Pants": "Pants", "Boots": "Boots", "Rist": "Rist", "Hair": "Hair", "FaceBase": "Face_base",
       "FaceEye": "Face_eye", "FaceEar": "Face_Ear", "FaceForehead": "Face_forehead", "FaceNose": "Face_nose"}
PIEL_DEF = (0.93, 0.63, 0.56)      # medida en 3 CaC de XV2 (mejilla ~ (232,166,147)) frente a (215,161,126) del anterior
PIEL_RAZA = {"HUM": PIEL_DEF, "HUF": PIEL_DEF, "NMC": (0.50, 0.75, 0.38), "FRI": (0.93, 0.90, 0.95), "MAM": (0.96, 0.62, 0.72), "MAF": (0.96, 0.62, 0.72)}     # NMC/FRI/Majin: sin medir contra XV2 [H]
DEFECTO = {"raza": "HUM", "bust": 0, "pants": 0, "boots": 0, "rist": 0, "cara": 0, "pelo": 1, "tipo": -1, "ojos": -1}   # ojos -1 = los de la cara (K: fila Ojos del menu)


def leer_sel(txt):
    d = dict(DEFECTO)
    for par in (txt or "").replace(";", ",").split(","):
        if ":" in par or "=" in par:
            k, v = par.replace("=", ":").split(":", 1)
            d[k.strip()] = v.strip() if k.strip() == "raza" else int(v)
    return d


def _parte_bcs(bcs, tipo, modelo):
    """Entrada de partset del BCS que usa ese modelo (lleva los colores por defecto), o una vacia."""
    for ps in bcs["partsets"]:
        p = (ps or {}).get(tipo)
        if p and p["modelo"] == modelo and (p["colores"] or tipo.startswith("Face") or tipo == "Hair" or modelo == 0):
            return p
    return {"modelo": modelo, "colores": [], "fisicas": [], "ocultar": 0}


def aplicar_cuerpo(esk, bcs, partes, tipo):
    """Constitucion del cuerpo de XV2 (BCS [[cuerpo]], escala por hueso, heredada por los hijos): mueve los huesos y los
    vertices (v' = suma w * (J'_i + S_i (v - J_i)), S_i = escala acumulada del hueso). Alto y ancho salen de aqui."""
    esc = bcs["cuerpos"][tipo] if 0 <= tipo < len(bcs["cuerpos"]) else []
    if not esc:
        return
    n = len(esk.names)
    dic = {nm: np.array(v, np.float64) for nm, v in esc}
    W = np.asarray(esk.world, np.float64)
    S = np.ones((n, 3))
    Wn = W.copy()
    for i in range(n):                         # los padres van antes que los hijos en el ESK
        p = esk.parent[i]
        loc = dic.get(esk.names[i], np.ones(3))
        if p >= 0:
            S[i] = S[p] * loc
            Wn[i] = Wn[p] + S[p] * (W[i] - W[p])
        else:
            S[i] = loc
    idx = {nm: i for i, nm in enumerate(esk.names)}
    for d in partes.values():
        for sm in d["subs"]:
            ids = np.array([idx.get(b, 1) for b in sm.bone_names])
            v = sm.pos.astype(np.float64)
            new = np.zeros_like(v)
            for k in range(sm.bones.shape[1]):
                b = ids[np.clip(sm.bones[:, k], 0, len(ids) - 1)]
                new += sm.weights[:, k:k + 1] * (Wn[b] + S[b] * (v - W[b]))
            tot = sm.weights.sum(1, keepdims=True)
            sm.pos = np.where(tot > 1e-6, new / np.maximum(tot, 1e-6), v).astype(sm.pos.dtype)
    esk.world = [Wn[i] for i in range(n)]


def cargar_cac(c, sel):
    raza = sel.get("raza", "HUM")
    bcs = L.leer_bcs(c.get("data/chara/%s/%s.bcs" % (raza, raza)))
    esk = xm.Esk(c.get("data/chara/%s/%s_000.esk" % (raza, raza)))
    ps, partes = {}, {}
    pedidas = [(t, sel[k]) for k, t in TIPOS.items()] + [(t, sel["ojos"] if t == "FaceEye" and sel.get("ojos", -1) >= 0 else sel["cara"]) for t in CARA]
    for tipo, modelo in pedidas:
        if modelo < 0:
            continue
        base = "data/chara/%s/%s_%03d_%s" % (raza, raza, modelo, SUF[tipo])
        if not X._ci(c, base + ".emd"):                 # la raza no tiene ese modelo: el 000 de la raza si existe, si no se omite
            modelo = 0
            base = "data/chara/%s/%s_%03d_%s" % (raza, raza, modelo, SUF[tipo])
            if not X._ci(c, base + ".emd"):
                continue
        base0 = "data/chara/%s/%s_000_%s" % (raza, raza, SUF[tipo])      # sin textura propia: la de la pieza 000 (medido)
        g = lambda ext: X._ci(c, base + ext) or X._ci(c, base0 + ext)    # noqa: E731
        emb_ruta = g(".emb")
        if not emb_ruta and tipo in ("FaceForehead", "FaceNose", "FaceEar"):   # comparten el atlas de la cara base (medido: HUM_00x_Face_forehead no trae .emb)
            emb_ruta = X._ci(c, "data/chara/%s/%s_000_Face_base.emb" % (raza, raza))
        if not emb_ruta:
            continue
        ps[tipo] = _parte_bcs(bcs, tipo, modelo)
        partes[tipo] = {"subs": xm.leer_emd(c.get(X._ci(c, base + ".emd"))), "emb": xm.leer_emb(c.get(emb_ruta)),
                        "emm": L.leer_emm(c.get(g(".emm"))) if g(".emm") else [],
                        "dye": L.dye_filas(c.get(g(".dyt.emb"))) if g(".dyt.emb") else []}
    aplicar_cuerpo(esk, bcs, partes, sel.get("tipo", 0))
    bcs["partsets"].append(ps)
    return esk, bcs, partes, len(bcs["partsets"]) - 1


BASE_BIN = 300 * 1024      # K: texturas (AZT) + cabeceras del bin, ~constante
CALIB_DEF = 30.0           # bytes de bin por triangulo (se recalibra con cada pieza compuesta)


def _calib_ruta():
    import tempfile
    return os.path.join(os.environ.get("DBZ3_CAC_CACHE") or os.path.join(tempfile.gettempdir(), "dbz3_xv2_cache"), "calib_tris.txt")


def _calib_leer():
    try:
        with open(_calib_ruta()) as fh:
            return max(5.0, float(fh.read()))
    except (OSError, ValueError):
        return CALIB_DEF


def _calib_guardar(v):
    try:
        os.makedirs(os.path.dirname(_calib_ruta()), exist_ok=True)
        with open(_calib_ruta(), "w") as fh:
            fh.write("%.3f" % v)
    except OSError:
        pass


LIM_BIN = 1000 * 1024      # tope de tamano del bin (los nativos pesan ~948 KB)


def componer(sel, plantilla, salida, hsv=None, lim=LIM_BIN, previa=False):
    from xeno_importar import proteger_fuente
    source = L.buscar_xv2()
    if not source:
        raise FileNotFoundError('No encuentro Xenoverse 2; el creador conserva las piezas B3.')
    proteger_fuente(source, [salida, _calib_ruta()])
    sel = dict(DEFECTO, **sel)
    if sel['raza'] not in PIEL_RAZA:
        raise ValueError('raza CaC de XV2 no admitida')
    if not os.environ.get("C6_VIEJO"):
        return componer_xeno(sel, plantilla, salida, hsv, previa)
    c = L.cpk_xv2()
    piel = np.array(PIEL_RAZA.get(sel.get("raza", "HUM"), PIEL_DEF), np.float32)       # el grupo SKIN_ del BCS son 112 tonos de fantasia (fila 0 = gris): piel humana fija
    if hsv and "piel" in hsv and tuple(hsv["piel"]) != (0, 0, 0):
        piel = X.hsv_region(piel, hsv["piel"])
    decim = 1.0
    calib = _calib_leer()
    for intento in range(3):
        cargado = cargar_cac(c, sel)
        fijos, recortables = 0, 0           # K: triangulos que no se recortan / que si (mismo criterio que fuente_sintetica)
        for tipo, d in cargado[2].items():
            for sm in d["subs"]:
                if not tipo.startswith("Face") and len(sm.tris) > 500:
                    recortables += len(sm.tris)
                else:
                    fijos += len(sm.tris)
        if intento == 0 and recortables:     # K: estima el recorte de la 1.a pasada (antes se componia dos veces: ~2x tiempo)
            tris_ok = (lim * 0.94 - BASE_BIN) / calib      # 6 % de margen: los accesorios del donante (~15 KB) y la AZT se suman despues
            if fijos + recortables > tris_ok:
                decim = max(0.35, min(1.0, int((tris_ok - fijos) / recortables * 20) / 20))   # a pasos de 0.05: las piezas recortadas se reutilizan de la cache
        r = X.convertir(sel.get("raza", "HUM"), "000", plantilla, salida, piel=piel, cargado=cargado, hsv=hsv, decim=decim)
        tam = os.path.getsize(salida)
        tris_eff = fijos + recortables * decim
        if tris_eff > 0:
            _calib_guardar((tam - BASE_BIN) / tris_eff)
        if tam <= lim:
            break
        decim *= max(0.35, min(0.9, lim / tam * 0.88))      # el AWO (geometria) es ~70 % del bin
        print("C6: bin %d KB > %d KB: recorte de triangulos a x%.2f" % (tam // 1024, lim // 1024, decim))
    if tam > lim:
        raise ValueError('CaC XV2 excede el presupuesto: %d B > %d B; no se instala' % (tam, lim))
    return r


TIPOS_BCS = {"bust": "Bust", "pants": "Pants", "boots": "Boots", "rist": "Rist", "pelo": "Hair", "cara": "Face_base",
             "frente": "Face_forehead", "ojos": "Face_eye", "nariz": "Face_nose", "oreja": "Face_ear"}
ZONA_HSV = {"Hair": "pelo", "Bust": "torso", "Pants": "piernas", "Boots": "botas", "Rist": "guantes"}


def _rgb_bcs(bcs, grupo, fila):
    return np.clip(np.array(bcs["colors"][grupo]["entries"][fila]["rgba"][0][:3], float), 0, 1)


# ---- Transformaciones (XV2RAZAS 13/11). Lo que XV2 cambia de verdad en su heroe, medido en custom_skill.cus + BCS
# (E:/DBZ3HD_trabajo/xv2q/transf/INFORME.md): Saiyan = ultimates SS1/SS2/SS3 con los partsets 100/101/102 (HUM: solo los ojos, el
# peinado del jugador se conserva; HUF: ojos + pelo 13/14/15); raza de Freezer = forma dorada (sin partset: solo color); Majin =
# Purificacion (cambio al personaje MAP); Namek = gigante (ni partset ni modelo: solo tamano, no hay modelo que convertir).
# El rubio, el turquesa y el dorado no estan en los datos de XV2 (los pone su ejecutable): se usan los del SSJ nativo de B3.
FORMAS = {"HUM": 3, "HUF": 3, "FRI": 1, "MAM": 1, "MAF": 1, "NMC": 0}
ORO_SSJ, CEJA_SSJ, OJO_SSJ = (1.0, 0.86, 0.30), (0.98, 0.86, 0.30), (0.20, 0.62, 0.70)   # vista_heroe SSJ_CEJAS / SSJ_OJOS (B3 266/417)
DORADO_FRI = {"SKIN_A": (1.0, 0.82, 0.28), "SKIN_C": (0.86, 0.62, 0.14), "SKIN_D": (0.52, 0.25, 0.66)}
PERSONAJE_FORMA = {"MAM": "MAP", "MAF": "MAP"}


def _tinte(bcs, parte, skin_def, hsv, forzados=None):
    """Color de cada material, por nombre: grupo BCS cuyo nombre es prefijo (CC00_BUST_, SKIN_A_...), con el color que trae el
    partset o el HSV de la ficha. None = la curva DYT del material tal cual."""
    grupos = {bcs["colors"][s["group"]]["name"].upper().rstrip("_"): _rgb_bcs(bcs, s["group"], s["color"]) for s in parte.get("color_selectors", [])
              if s["group"] < len(bcs["colors"]) and s["color"] < len(bcs["colors"][s["group"]]["entries"])
              and not bcs["colors"][s["group"]]["name"].upper().startswith("SKIN")}   # la piel no la decide la prenda: la mano salia marron
    zona = ZONA_HSV.get(parte["type"])
    bcs_raza = parte.get("chara", "")

    def tinte(nombre):
        for pref, rgb in (forzados or {}).items():      # color de la forma: manda sobre el partset y sobre la ficha
            if nombre.upper().startswith(pref):
                return np.array(rgb, float)
        n, c = nombre.upper(), (np.array(X.IRIS_CAC) if nombre.lower().startswith("eye") and bcs_raza in ("HUM", "HUF") else None)
        for g, rgb in list(grupos.items()) + list(skin_def.items()):
            if n.startswith(g):
                c = rgb
                break
        if c is None and n[:2] == "CC" and n[2:4].isdigit():
            c = _color_defecto(n, parte["type"], grupos, bcs_raza)
        h = (hsv or {}).get("piel" if n.startswith("SKIN") else zona)
        if h and tuple(h) != (0, 0, 0):
            c = X.hsv_region(c if c is not None else np.array((.85, .6, .5)), h)
        return c
    return tinte


# Materiales tenibles (CCnn_<PIEZA>_: sombreador TOON_UNIF, el color es un uniforme y el DYT solo trae la luz en gris). En XV2 el
# color sale del partset o, en un heroe, de lo que eligio el jugador; sin ninguno de los dos la prenda salia GRIS (12/11: el
# gi y el traje de la raza de Freezer). Por defecto: CC00/CC03 el color que el partset da a esa pieza (el 300 de HUM trae CC03 amarillo;
# el de FRI, CC01 morado) o el del tipo; CC01 azul marino; CC02 rojo. La ficha (HSV) sigue mandando.
# 13/11 (usuario: "el gi sale amarillo, no naranja"): el amarillo era el selector CC03 del partset 300, que es de OTRO material;
# un color del partset solo vale para el material de su propio grupo (eso ya lo hace `_tinte`). Sin el, el color del tipo.
COLOR_TIPO = {"Bust": (0.96, 0.42, 0.07), "Pants": (0.96, 0.42, 0.07), "Rist": (0.13, 0.16, 0.42), "Boots": (0.13, 0.16, 0.42)}
COLOR_CC = {1: (0.13, 0.16, 0.42), 2: (0.80, 0.16, 0.12)}
COLOR_RAZA = {"FRI": (0.27, 0.17, 0.50)}      # el traje de la raza de Freezer: el morado que su partset da a la pieza (CC01 78)


def _color_defecto(nombre, tipo, grupos, raza=""):
    k = int(nombre[2:4])
    if raza in COLOR_RAZA:
        return np.array(COLOR_RAZA[raza])
    if k in COLOR_CC:
        return np.array(COLOR_CC[k])
    return np.array(COLOR_TIPO.get(tipo, (0.6, 0.6, 0.6)))


def tipo_neutro(bodies, alto):
    """Constitucion por defecto: la de estatura mas cercana a la de Goku (las 12 de XV2 son 4 alturas x 3 anchos y la 0 es la
    mas baja y delgada: con ella el heroe salia un 17 % mas bajo y fino). A igual altura, el ancho medio."""
    import xv2_proporcion as P
    cand = []
    for i, b in enumerate(bodies[:12]):
        base = next((e["scale"] for e in b["scales"] if e["bone"] == "b_C_Base"), (1.0, 1.0, 1.0))
        cand.append((round(abs(base[1] * alto - P.ALTO_XV2_REF), 2), base[0], i))
    if not cand:
        return 0
    mejor = min(c[0] for c in cand)
    iguales = sorted(c for c in cand if c[0] == mejor)
    return iguales[len(iguales) // 2][2]


def posar_cola(get, raza, esk, piezas):
    """Cola (raza de Freezer: X_T_TAIL1-10). En el bind sale recta hacia atras, 6 unidades: B3 no tiene huesos de cola y se quedaba
    asi, como una pica. Se hornea la pose de la animacion de reposo de XV2 (<RAZA>_BAS_STAND, cuadro 0): giros locales reales."""
    cola = [n for n in esk.names if n.startswith("X_T_TAIL")]
    if (not cola or os.environ.get("DBZ3_XV2_COLA", "1") == "0"
            or os.environ.get("DBZ3_XV2_COLA_ANIMADA") == "1"):
        return
    from scipy.spatial.transform import Rotation
    from xeno_gestos import FaceEan, skin_pose
    import xeno_a_b3 as XA
    try:
        ean = FaceEan(get("data/chara/%s/%s.ean" % (raza, raza)))
        vals = ean.values("%s_BAS_STAND" % raza, 0)
    except (KeyError, ValueError):
        return
    M = xm.MIRROR
    worlds = [None] * len(esk.names)
    for i in XA.order(esk.parent):
        p = esk.parent[i]
        local = (np.linalg.inv(esk.world_matrix[p]) @ esk.world_matrix[i]) if p >= 0 else esk.world_matrix[i].copy()
        if esk.names[i] in cola and esk.names[i] in vals:
            local = local.copy()
            local[:3, :3] = M @ Rotation.from_quat(vals[esk.names[i]][1]).as_matrix() @ M
        worlds[i] = worlds[p] @ local if p >= 0 else local
    matrices = {n: worlds[i] @ np.linalg.inv(esk.world_matrix[i]) for i, n in enumerate(esk.names)}
    for q in piezas:
        sm = q["sm"]
        ids = np.array([n in cola for n in sm.bone_names])
        if (ids[sm.bones] & (sm.weights > 0)).any():
            q["sm"] = skin_pose(sm, matrices)


def fuente_personaje(c, codigo, lod=0):
    """Un personaje entero de XV2 (su BCS, traje 0) por el mismo camino que GOK/VGT/PIC: la forma que XV2 resuelve cambiando de
    personaje (Purificacion Majin -> MAP)."""
    import xeno_a_b3 as XA

    def get(ruta):
        r = X._ci(c, ruta)
        if not r:
            raise KeyError(ruta)
        return c.get(r)
    return XA.load_source(get, lod, codigo, 0)


def fuente_cac(c, sel, hsv=None, lod=0):
    """Seleccion del creador -> (esk, piezas) para xeno_a_b3: mismo camino que GOK/VGT/PIC (tinta, rampas, gestos y manos)."""
    import xeno_a_b3 as XA
    import xeno_bcs as XB
    from xeno_gestos import FaceEan
    raza = sel.get("raza", "HUM")
    forma = min(int(sel.get("forma", 0) or 0), FORMAS.get(raza, 0))
    if forma and raza in PERSONAJE_FORMA:
        return fuente_personaje(c, PERSONAJE_FORMA[raza], lod)
    if raza == "MAM" and sel.get("cara", 0) == 0:     # la cara 0 de la Majin macho es la de ojos cerrados (sin ojos, medido en juego): la 1 los abre
        sel = dict(sel, cara=1)

    def get(ruta):
        r = X._ci(c, ruta)
        if not r:
            raise KeyError(ruta)
        return c.get(r)
    bcs = XB.leer_bcs(get("data/chara/%s/%s.bcs" % (raza, raza)))
    # Colores de piel por defecto de la raza: los del primer partset que los elige (FRI: SKIN_A/C/D).
    skin = {}
    for ps in bcs["part_sets"]:
        for p in ps["parts"]:
            for s in (p or {}).get("color_selectors", []):
                g = bcs["colors"][s["group"]]["name"].upper().rstrip("_") if s["group"] < len(bcs["colors"]) else ""
                if g.startswith("SKIN") and g not in skin and s["color"] < len(bcs["colors"][s["group"]]["entries"]) and raza == "FRI":
                    skin[g] = _rgb_bcs(bcs, s["group"], s["color"])
    if raza == "FRI":     # cara y manos claras, como las capturas reales de la raza (el partset 0 las trae rojas)
        skin["SKIN_A"] = np.array(PIEL_RAZA["FRI"], float)
    pedidas = {t: sel.get(k, sel.get("cara", 0) if t.startswith("Face") else 0) for k, t in TIPOS_BCS.items()}
    if sel.get("ojos", -1) < 0:
        pedidas["Face_eye"] = sel.get("cara", 0)
    forzados = None
    if forma and raza in ("HUM", "HUF"):
        forzados = {"HAIR_EYEBROW": CEJA_SSJ, "HAIR": ORO_SSJ, "EYE": OJO_SSJ}
    elif forma and raza == "FRI":
        skin.update({k: np.array(v, float) for k, v in DORADO_FRI.items()})
    de_forma = {}
    if forma and raza in ("HUM", "HUF"):      # partset 100/101/102: las piezas que XV2 cambia en SS1/SS2/SS3
        ps = next((x for x in bcs["part_sets"] if x["id"] == 99 + forma), None)
        de_forma = {q["type"]: q for q in (ps["parts"] if ps else []) if q and q["model"] >= 0}
    partes = []
    for tipo, q in de_forma.items():
        partes.append(dict(q, tint=_tinte(bcs, q, skin, hsv, forzados)))
    for tipo, modelo in pedidas.items():
        if tipo in de_forma:
            continue
        if modelo < 0:
            continue
        p = next((q for ps in bcs["part_sets"] for q in ps["parts"] if q and q["type"] == tipo and q["model"] == modelo), None)
        if p is None or not X._ci(c, "data/chara/%s/%s_%03d_%s.emd" % (p["chara"] or raza, p["chara"] or raza, modelo, tipo)):
            p = dict(type=tipo, model=0, model2=0, texture=0, chara=raza, physics=[], color_selectors=[], hide_flags=0, hide_materials=0,
                     paths=dict(emd="", emm="", emb="", ean=""))
            if not X._ci(c, "data/chara/%s/%s_000_%s.emd" % (raza, raza, tipo)):
                continue
        partes.append(dict(p, tint=_tinte(bcs, p, skin, hsv, forzados)))
    if not {"Bust", "Face_base"} <= {p["type"] for p in partes}:
        raise ValueError("CaC XV2: faltan piezas de %s" % raza)
    n = sel.get("cara", 0)
    u, l = "data/chara/%s/%s%03d%%s.fce.ean" % (raza, raza, n), "data/chara/%s/%s000%%s.fce.ean" % (raza, raza)
    ean = FaceEan(get(u % "U" if X._ci(c, u % "U") else l % "U")).mezclar(FaceEan(get(u % "L" if X._ci(c, u % "L") else l % "L")))
    esk = xm.Esk(get("data/chara/%s/%s_000.esk" % (raza, raza)))
    for _p in partes:      # C2/HER09: fisica cuyo attachment no existe en SU ESK (manto de algunas razas)
        if _p.get("physics"):
            _ok = []
            for _fz in _p["physics"]:
                try:
                    _e = xm.Esk(get("data/chara/%s/%s.esk" % (_fz.get("chara") or raza, _fz["paths"]["emd"])))
                except (KeyError, FileNotFoundError, ValueError):
                    continue
                if _fz["paths"].get("bone") in _e.index:
                    _ok.append(_fz)
            _p["physics"] = _ok
    esk, piezas, fisica = XA.load_source(get, lod, raza, parts=partes, face_ean=ean, esk=esk)
    tipo = sel.get("tipo", -1)
    if tipo < 0:
        tipo = tipo_neutro(bcs["bodies"], float(esk.world[esk.index["b_C_Head"]][1] - esk.world[esk.index["b_L_Foot"]][1]))
    posar_cola(get, raza, esk, piezas)
    if 0 <= tipo < len(bcs["bodies"]) and bcs["bodies"][tipo]["scales"]:     # constitucion: mismas cuentas que aplicar_cuerpo
        escalas = {e["bone"]: e["scale"] for e in bcs["bodies"][tipo]["scales"]}
        crudo = dict(cuerpos=[list(escalas.items())])
        partes_c = {"x": {"subs": list({id(q["sm"]): q["sm"] for q in piezas if not q["attachment"]}.values())}}
        aplicar_cuerpo(esk, crudo, partes_c, 0)
        for w, m in zip(esk.world, esk.world_matrix):
            m[:3, 3] = w
    return esk, piezas, fisica


# Razas con el largo de pierna de XV2 (piernas al 32 % de la altura en la Majin macho frente al 62 % de Goku; HUM/NMC/FRI al 80-84 %). Con la
# compensacion de guardia (GUARDIA) los pies quedan donde los de Goku en la guardia, medido en juego (SUB_XV2_1-11.md). "0" = ninguna,
# "todas" o "HUM,NMC,..." para elegir.
PROPORCIONES = os.environ.get("DBZ3_XV2_PROPORCIONES", "MAM")   # ponytail: HUM,NMC,FRI solo vistas en guardia; encender tras verlas en vuelo y caidas
# Caida de la cadera en guardia (unidades B3; medida en juego: reposo 9,44 sobre el suelo, guardia 6,7-7,2). Las pistas bajan la cadera
# esa cantidad en valor absoluto: con piernas mas cortas los pies se hunden (1-k)*caida. 0 = sin compensar (como hasta ahora).
GUARDIA = float(os.environ.get("DBZ3_XV2_GUARDIA", "2.4"))


OJOS_ADELANTE = {"MAM": float(os.environ.get("DBZ3_XV2_OJO_MAM", "0"))}     # unidades XV2 hacia delante (+z)


def adelantar_ojos(piezas, raza):
    """La Majin macho no mostraba ojos (medido en juego y en render): el rectangulo del ojo queda 0,07-0,16 bajo la piel
    abultada de la cara. Se adelanta el ojo para que asome sobre la piel; las demas razas no lo necesitan (ojos visibles)."""
    d = OJOS_ADELANTE.get(raza, 0.0)
    if not d:
        return
    for q in piezas:
        if q["part"] == "Face_eye":
            pos = q["sm"].pos.copy()
            pos[:, 2] += d
            q["sm"].pos = pos


def proporciones_piernas(tpl, esk, raza):
    """Largo de pierna de la raza (relacion pierna/torso de su esqueleto XV2) sobre el esqueleto de Goku de la plantilla.
    Todas las razas llevaban las piernas de Goku (62 % de la altura; la Majin de XV2 tiene el 32 %). Las posiciones de las
    pistas de animacion se SUMAN al reposo (altura.py), asi que bajar la cadera lo mismo que se acortan las piernas deja los pies
    en el suelo. Solo piernas; brazos, hombros y torso siguen los de Goku (rayos, agarres y camara usan esos huesos).
    Devuelve el factor aplicado (1.0 = sin cambio)."""
    if os.environ.get("DBZ3_XV2_PROPORCIONES") is None:
        # C2/HER09: tabla medida por raza (cac/xv2_razas.json). Si no hay banco o la
        # raza no paso la medida, 1.0 = como estaba (piernas de Goku). La variable de
        # entorno sigue mandando cuando se define (pruebas y legado).
        try:
            import sys as _sys
            _sys.path.insert(0, os.path.join(HERE, "..", "cac"))
            import xv2_proporcion as _prop
            if _prop.tabla() is not None:      # sin banco se sigue con el comportamiento de siempre
                return _prop.aplicar_tabla(tpl, esk, raza)
        except Exception:   # noqa: BLE001 (sin banco o sin numpy: no cambia nada)
            pass
    if PROPORCIONES in ("0", "") or (PROPORCIONES != "todas" and raza not in PROPORCIONES.replace("1", "MAM").split(",")):
        return 1.0
    ix = esk.index
    p = lambda n: np.array(esk.world[ix[n]], float)
    pierna = p("b_L_Leg1")[1] - p("b_L_Foot")[1]
    torso = p("b_C_Head")[1] - p("b_L_Leg1")[1]
    W = tpl.world()
    b = tpl.bone
    g_pierna = W[b("LLEG1")][1, 3] - W[b("LFOOT1")][1, 3]
    g_torso = W[b("HEAD")][1, 3] - W[b("LLEG1")][1, 3]
    k = float(np.clip((pierna / torso) / (g_pierna / g_torso), 0.45, 1.0))
    if k > 0.99:
        return 1.0
    suela = W[b("LLEG1")][1, 3] - W[b("LFOOT2")][1, 3]     # cadera -> punta del pie (el suelo del reposo)
    for lado in ("R", "L"):
        raiz = b(lado + "LEG1")
        hijos = [i for i in range(tpl.nb) if raiz in set(SM_ancestros(tpl, i)) and i != raiz]
        for i in hijos:
            t = tpl.local[i][:3, 3] * k
            tpl.local[i] = tpl.local[i].copy()
            tpl.local[i][:3, 3] = t
            tpl.axes[i][16:28] = struct.pack(">3f", *t)
    w = b("WAIST")
    t = tpl.local[w][:3, 3].copy()
    t[1] -= (1.0 - k) * (suela - GUARDIA)
    tpl.local[w] = tpl.local[w].copy()
    tpl.local[w][:3, 3] = t
    tpl.axes[w][16:28] = struct.pack(">3f", *t)
    return k


def SM_ancestros(tpl, i):
    while i != -1:
        yield i
        i = tpl.parent[i]


ESQUELETO = os.environ.get("DBZ3_XV2_ESQUELETO", "1") != "0"
# La vista del creador quita el contorno por encima de 12000 triangulos (VistaPesada, heroe_ui.cpp) y XV2 trae ~17.000.
TRIS_PREVIA = int(os.environ.get("DBZ3_XV2_TRIS_PREVIA", "6000"))


def soldar(sm):
    """Une los vertices repetidos (misma posicion, UV y pesos: los EMD los repiten al partir las tiras). El reductor no toca
    los vertices con la posicion compartida (costuras), asi que sin soldar se quedaba en ~10.000 triangulos."""
    import copy
    clave = np.concatenate([np.round(sm.pos, 4), np.round(sm.uv, 4), sm.bones.astype(float), np.round(sm.weights, 2)], 1)
    _, primero, inv = np.unique(clave, axis=0, return_index=True, return_inverse=True)
    if len(primero) == len(sm.pos):
        return sm
    inv = inv.reshape(-1)
    out = copy.copy(sm)
    nrm = np.zeros((len(primero), 3))
    np.add.at(nrm, inv, sm.nrm)
    out.nrm = (nrm / np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)).astype(sm.nrm.dtype)
    for k in ("pos", "uv", "bones", "weights"):
        setattr(out, k, getattr(sm, k)[primero])
    tris = inv[sm.tris]
    out.tris = tris[(tris[:, 0] != tris[:, 1]) & (tris[:, 1] != tris[:, 2]) & (tris[:, 0] != tris[:, 2])]
    return out


def aligerar(piezas, tope):
    """Simplifica las piezas (colapso de aristas con error cuadrico de sdbh_model: conserva costuras de UV, bordes y pesos)
    hasta `tope` triangulos en total. La cara no se toca (13/11: reducirla abria grietas en la frente y junto a los ojos, visto
    de cerca); las manos solo hasta el 65 %; el resto reparte el recorte por igual."""
    import sdbh_model as SM
    intocable = lambda q: any(k in q["sm"].name.lower() for k in ("eye", "mouth", "mouse", "tooth", "tongue", "brow"))  # noqa: E731
    suave = lambda q: q["part"].startswith("Face") or any(n.startswith("h_") for n in q["sm"].bone_names)  # noqa: E731
    fijos = sum(len(q["sm"].tris) for q in piezas if intocable(q))
    blandas = [q for q in piezas if not intocable(q) and suave(q)]
    resto = [q for q in piezas if not intocable(q) and not suave(q)]
    total = sum(len(q["sm"].tris) for q in resto)
    nb = sum(len(q["sm"].tris) for q in blandas)
    if fijos + nb + total <= tope:
        return 1.0
    rb = max(0.65, min(1.0, (tope - fijos) / max(nb + total, 1)))
    for q in blandas:
        if len(q["sm"].tris) > 60 and rb < 1.0:
            q["sm"] = SM.decimate(soldar(q["sm"]), max(40, int(len(q["sm"].tris) * rb)), tol_pesos=0.3)
    fijos += sum(len(q["sm"].tris) for q in blandas)
    r = 1.0
    for _ in range(2):       # dos pasadas como mucho (la segunda, sobre mallas ya pequenas, ~1 s): el reductor respeta costuras y bordes
        r = max(0.15, (tope - fijos) / total)
        for q in resto:
            if len(q["sm"].tris) > 60:
                q["sm"] = SM.decimate(soldar(q["sm"]), max(40, int(len(q["sm"].tris) * r)), tol_pesos=0.6)
        nuevo = sum(len(q["sm"].tris) for q in resto)
        if fijos + nuevo <= tope or nuevo > total * 0.97:
            break
        total = nuevo
    return r


def componer_xeno(sel, plantilla, salida, hsv=None, previa=False):
    """previa: para la vista del menu (cada cambio de pieza): LOD 1, 256 px, sin gestos; 8 veces mas ligero que el de combate."""
    import xeno_a_b3 as XA
    import sdbh_model as SM
    from xeno_gestos import native_lists
    c = L.cpk_xv2()
    esk, piezas, _fisica = fuente_cac(c, sel, hsv, 1 if previa else 0)
    tpl = SM.Template(plantilla)
    tpl.gesture_lists = native_lists(open(plantilla, "rb").read(), tpl)
    esq = None
    if ESQUELETO:
        # XV2RAZAS: la plantilla toma las proporciones del esqueleto XV2 (todos los tramos, no solo la pierna) y la malla entra
        # con una semejanza. Antes se estiraba a lo largo de los huesos de Goku sin engordar: brazos y piernas finos.
        import xv2_proporcion as P
        s = P.escala_xv2(tpl)
        suela = min(float(q["sm"].pos[:, 1].min()) for q in piezas if q["part"] in ("Boots", "Pants") and not q["attachment"])
        esq = P.esqueleto(tpl, esk, XA.Retarget(esk, tpl).direct, s, suela, GUARDIA)
        rig = XA.Retarget(esk, tpl, s)
    else:
        proporciones_piernas(tpl, esk, sel.get("raza", "HUM"))
        rig = XA.Retarget(esk, tpl)
    adelantar_ojos(piezas, sel.get("raza", "HUM"))
    if previa:
        aligerar(piezas, TRIS_PREVIA)
    conv, _red = XA.prepare_pieces(rig, piezas, 1.0, gestos=not previa)
    datos, stats = XA.build_model(tpl, rig, conv, 256 if previa else 1024, hueco=0.40 if previa else 0.12, pelo=False)   # hundir la frente bajo el pelo dejaba ver la base del peinado   # la tinta se reduce promediando (material_image)
    stats["esqueleto"] = esq
    stats["cola"] = rig.report["tail"]
    with open(salida, "wb") as fh:
        fh.write(datos)
    errores = SM.verify(salida)
    if errores:
        raise ValueError("CaC XV2: bin invalido: %s" % errores[:3])
    return dict(stats, bin=len(datos))


def selftest():
    from unittest.mock import patch
    from xeno_binary import rejects
    assert leer_sel('raza=NMC,bust=300;pelo=2')['raza'] == 'NMC'
    assert leer_sel('bust=300')['bust'] == 300
    before = dict(DEFECTO)
    with patch.object(L, 'buscar_xv2', return_value=None):
        try:
            componer({}, 'no_se_abre.bin', 'no_se_escribe.bin')
            raise AssertionError('ausencia aceptada')
        except FileNotFoundError:
            pass
    with patch.object(L, 'buscar_xv2', return_value='X:/Steam/common/XV2/cpk/data.cpk'):
        rejects(lambda: componer({'raza': 'NO'}, 'no.bin', 'Y:/salida.bin'))
        rejects(lambda: componer({}, 'no.bin', 'X:/Steam/common/XV2/prohibido.bin'))
    assert DEFECTO == before
    print('xv2_cac_b3: N0 OK (seleccion, ausencia, raza y Steam solo lectura; sin juego)')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument('--selftest', action='store_true')
    s = ap.add_subparsers(dest="cmd")
    a = s.add_parser("componer")
    a.add_argument("plantilla")
    a.add_argument("salida")
    b = s.add_parser("previa")
    b.add_argument("salida")
    b.add_argument("--donante", type=int, default=0)
    for p in (a, b):
        p.add_argument("--sel", default="")
        p.add_argument("--hsv", default="")
    x = ap.parse_args(argv)
    if x.selftest:
        selftest(); return
    hsv = {k: tuple(int(v) for v in r.split(",")) for k, r in (q.split("=") for q in x.hsv.split(";") if "=" in q)}
    sel = leer_sel(x.sel)
    if x.cmd == "componer":
        componer(sel, x.plantilla, x.salida, hsv)
    elif x.cmd == "previa":
        import xv2_fuente as F
        tmp = tempfile.mkdtemp(prefix="xv2_cac_", dir=F.TMP)
        tpl = F.plantilla_donante(x.donante, None, os.path.join(tmp, "plantilla.bin"))
        out = os.path.join(tmp, "previa.bin")
        componer(sel, tpl, out, hsv)
        import subprocess
        subprocess.run([sys.executable, os.path.join(HERE, "..", "model_render.py"), out, x.salida, "--size", "512"], check=True)
        print(x.salida, out)
    else:
        ap.print_help()


if __name__ == "__main__":
    main()
