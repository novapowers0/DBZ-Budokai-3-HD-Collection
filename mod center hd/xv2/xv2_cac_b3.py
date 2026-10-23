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
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
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
DEFECTO = {"raza": "HUM", "bust": 0, "pants": 0, "boots": 0, "rist": 0, "cara": 0, "pelo": 1, "tipo": 0, "ojos": -1}   # ojos -1 = los de la cara (K: fila Ojos del menu)


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


def componer(sel, plantilla, salida, hsv=None, lim=LIM_BIN):
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
    return r


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
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
