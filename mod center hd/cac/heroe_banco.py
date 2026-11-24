#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""heroe_banco.py - banco offline del carril S8 (Modo Heroe) para la noche del 10/11.

No abre el juego ni el emulador: usa vista_heroe.py, roster_build.plan_ids y
heroe_sagas_a_du.py con las piezas del juego del proyecto (us/).

  python "mod center hd/cac/heroe_banco.py" todo
  python "mod center hd/cac/heroe_banco.py" ida-vuelta   # HER01: exportar -> receta -> importar y comparar bytes
  python "mod center hd/cac/heroe_banco.py" sin-id       # HER03: rama sin plaza (plan_ids sin ID)
  python "mod center hd/cac/heroe_banco.py" plan         # HER03: alcanzabilidad real de sin_id
  python "mod center hd/cac/heroe_banco.py" textos       # HER06/HER08: sagas, puntos, traducciones
  python "mod center hd/cac/heroe_banco.py" arte         # HER07: scouter nativo (bytes con 0 vs 1)
  python "mod center hd/cac/heroe_banco.py" nombres      # HER04: mods/heroe_nombres (1974 con 68 texturas)

Temporales en E:\\DBZ3HD_trabajo\\S8\\banco (nunca en C: ni D:\\tmp). Nada se borra.
"""
import filecmp
import json
import os
import shutil
import struct
import subprocess
import sys
import time
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
CAC = HERE
HD = os.path.dirname(HERE)
sys.path[:0] = [CAC, HD, os.path.join(ROOT, "tools")]

US = os.path.join(ROOT, "us")
WORK = r"E:\DBZ3HD_trabajo\S8\banco"
VISTA = os.path.join(CAC, "vista_heroe.py")
RES = []


def ok(nombre, cond, detalle=""):
    RES.append((nombre, bool(cond), detalle))
    print(("  PASS " if cond else "  FAIL ") + nombre + (("  " + detalle) if detalle and not cond else ""), flush=True)
    return bool(cond)


def run():
    d = os.path.join(WORK, time.strftime("%Y%m%d-%H%M%S"))
    os.makedirs(d, exist_ok=True)
    return d


def juego_en(base, nombre):
    j = os.path.join(base, nombre)
    os.makedirs(os.path.join(j, "mods"), exist_ok=True)
    os.makedirs(os.path.join(j, "ingame_ui", "heroe_vista"), exist_ok=True)
    return j


def vista(juego, args, salida, receta=None, ficha=None):
    cmd = [sys.executable, "-X", "utf8", VISTA, "--juego", juego, "--us", US, "--salida", salida]
    if receta:
        cmd += ["--receta", receta]
    else:
        cmd += ["--ficha", ficha, "--exportar", "1"]
    return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")


def leer_json(salida, nombre):
    p = os.path.join(salida, nombre)
    try:
        with open(p, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


FICHA = ("cuerpo=0;traje=0;partes=0,0,0|0,0,0|0,0,0|0,0,0;pelo=0;c1=0,0,0;c2=0,0,0;"
         "logo=0;scouter=0;nombre=Kakar\u00f6to \u00d1;donante=0;capsulas=6,10;excluidas=7,8")


def ida_vuelta():
    base = run()
    j1, j2 = juego_en(base, "juego1"), juego_en(base, "juego2")
    s1, s2 = os.path.join(base, "vista1"), os.path.join(base, "vista2")
    r = vista(j1, None, s1, ficha=FICHA)
    ok("HER01: exportar (rc 0)", r.returncode == 0, (r.stderr or "")[-400:])
    e = leer_json(s1, "exportar.json")
    ok("HER01: exportar estado ok", e.get("estado") == "ok", str(e)[:300])
    mod = e.get("mod")
    ok("HER01: nombre con acentos -> carpeta slug", mod == "heroe_kakaroto_n", "mod=%s" % mod)
    receta = os.path.join(j1, "mods", mod or "heroe_x", "receta.toml")
    ok("HER01: receta escrita", os.path.isfile(receta))
    if not os.path.isfile(receta):
        return
    copia_dir = os.path.join(base, "recetas con espacio")
    os.makedirs(copia_dir, exist_ok=True)
    copia = os.path.join(copia_dir, "receta \u00f1.toml")
    shutil.copy2(receta, copia)
    r = vista(j2, None, s2, receta=copia)
    ok("HER01: importar receta (rc 0)", r.returncode == 0, (r.stderr or "")[-400:])
    i = leer_json(s2, "importar.json")
    ok("HER01: importar estado ok", i.get("estado") == "ok", str(i)[:300])
    ok("HER01: importar aviso tecnicas excluidas", i.get("tecnicas_excluidas") == [7, 8], str(i.get("aviso")))
    mod2 = i.get("mod")
    modelos = ("base", "ssj1", "ssj2", "ssj3")
    iguales = 0
    for m in modelos:
        a = os.path.join(j1, "mods", mod or "heroe_x", "modelos", m + ".bin")
        b = os.path.join(j2, "mods", mod2 or "heroe_y", "modelos", m + ".bin")
        if os.path.isfile(a) and os.path.isfile(b) and filecmp.cmp(a, b, shallow=False):
            iguales += 1
    ok("HER01: base/ssj1-3 byte a byte (%d/%d)" % (iguales, len(modelos)), iguales == len(modelos))
    try:
        with open(receta, "rb") as f:
            rec = tomllib.load(f)["receta"]
        ok("HER02: receta declara tecnicas_excluidas", rec.get("tecnicas_excluidas") == [7, 8], str(rec.get("nota")))
        ok("HER02: ficha de la receta sin excluidas", "excluidas=" not in rec.get("ficha", ""))
    except (OSError, ValueError, KeyError) as exc:
        ok("HER02: receta legible", False, str(exc))


def sin_id():
    import roster_build as rb
    import vista_heroe as vh
    base = run()
    j = juego_en(base, "juego")
    work = os.path.join(base, "work")
    os.makedirs(work, exist_ok=True)
    afs = rb.Afs(os.path.join(US, "data_cmn.afs"), work)
    original = rb.plan_ids
    rb.plan_ids = lambda mods_dir, srcs: ({}, {}, {})   # roster lleno: el mod nuevo no recibe plaza
    try:
        r = vh.exportar_personaje(j, FICHA, afs)
    finally:
        rb.plan_ids = original
    mod = r.get("mod")
    carpeta = os.path.join(j, "mods", mod or "heroe_x")
    ok("HER03: rama sin_id devuelve sin_id", r.get("estado") == "sin_id", str(r)[:300])
    ok("HER03: deja el .disabled (no borra)", os.path.isfile(os.path.join(carpeta, ".disabled")))
    ok("HER03: el modelo se escribio antes de decidir", os.path.isfile(os.path.join(carpeta, "modelos", "base.bin")))
    ok("HER03: no se reescribe el roster real", not os.path.exists(os.path.join(j, "mods", "roster_ids.toml")))


def plan():
    import roster_build as rb
    base = run()
    mods = os.path.join(base, "mods")
    os.makedirs(mods, exist_ok=True)
    filas = []
    for n in (24, 25, 40):
        srcs = [("m%02d" % i, "", {}) for i in range(n)]
        ids, _nuevo, _t = rb.plan_ids(mods, srcs)
        sin = sum(1 for n2, _, _ in srcs if ids.get(n2) is None)
        filas.append((n, sin, sum(1 for v in ids.values() if v is not None)))
        print("  plan_ids: %d activos -> %d sin plaza, %d con ID" % (n, sin, filas[-1][2]), flush=True)
    ok("HER03: con <=MAX_CELDAS hay IDs libres (%d plazas, %d IDs)" % (rb.MAX_CELDAS, len(rb.ALL_IDS)),
       filas[0][1] == 0 and filas[0][2] == 24)
    ok("HER03: con mas de MAX_CELDAS la rueda pagina (nunca sin_id)", filas[1][1] == 0 and filas[2][1] == 0)
    ok("HER03: alcanzabilidad real de sin_id = 0/3 (solo defensiva)", sum(f[1] for f in filas) == 0)


def textos():
    import heroe_sagas_a_du as hs
    try:
        hs.comprobar()
        ok("HER06/HER08: heroe_sagas_a_du --comprobar", True)
    except AssertionError as exc:
        ok("HER06/HER08: heroe_sagas_a_du --comprobar", False, str(exc))
    d = hs.leer(os.path.join(ROOT, "res", "heroe", "sagas.toml"))
    txt, plan_c = hs.generar(d)
    cfg = tomllib.loads(txt)
    por = {}
    for p in cfg.get("punto_mapa", []):
        por.setdefault(p["saga"], []).append(p["marca"][0])
    ok("HER06: 8/8 sagas con objeto de mapa", all(3 in v for v in por.values()), str(por))
    ok("HER06: 8/8 sagas con evento opcional", all(1 in v for v in por.values()))
    ok("HER06: 7/7 Esferas (saga 8 sin Esfera)", sum(1 for v in por.values() if 4 in v) == 7)
    tot = {lg: [0, 0] for lg in ("fr", "de", "it", "pt", "ja")}
    for c in cfg["capitulo"]:
        for lg in tot:
            for k in ("dialogo", "victoria", "mapa"):
                for en, tr in zip(c.get(k + "_en", []), c.get(k + "_" + lg, [])):
                    tot[lg][0] += 1
                    tot[lg][1] += int(en == tr)
    ok("HER08: 135 frases por idioma, 0 sin traducir", all(v == [135, 0] for v in tot.values()), str(tot))
    try:
        import test_i18n
        falta = [x for x in test_i18n.comprobar() if x[0].startswith(("heroe", "campaigns"))]
        ok("HER08: test_i18n de heroe/campaigns sin faltas", not falta, str(falta[:4]))
    except Exception as exc:  # noqa: BLE001
        ok("HER08: test_i18n", False, str(exc))


def arte():
    """HER07: el scouter nativo se injerta de verdad (base.bin cambia con scouter=0 vs 1)."""
    import vista_heroe as vh
    ok("HER07: scouter nativo activo en el montaje", bool(getattr(vh, "SCOUTER_NATIVO_ACTIVO", False)))
    base = run()
    j = juego_en(base, "juego")
    s = os.path.join(base, "vista")
    modelos = {}
    for sc, nm in ((0, "Sin Scouter"), (1, "Con Scouter")):
        ficha = ("cuerpo=0;traje=0;partes=0,0,0|0,0,0|0,0,0|0,0,0;pelo=0;c1=0,0,0;c2=0,0,0;"
                 "logo=0;scouter=%d;nombre=%s;donante=0") % (sc, nm)
        r = vista(j, None, s, ficha=ficha)
        if r.returncode != 0:
            ok("HER07: exportar scouter %d" % sc, False, (r.stderr or "")[-300:])
            return
        modelos[sc] = os.path.join(j, "mods", "heroe_%s" % ("sin_scouter" if sc == 0 else "con_scouter"), "modelos")
    # HEROE-07: el scouter va en las 4 formas (base y SSJ1-3), no solo en la base
    n = 0
    for m in ("base", "ssj1", "ssj2", "ssj3"):
        try:
            a = open(os.path.join(modelos[0], m + ".bin"), "rb").read()
            b = open(os.path.join(modelos[1], m + ".bin"), "rb").read()
        except OSError:
            continue
        n += int(a != b and len(b) > len(a))
        print("  %s: bytes %d -> %d" % (m, len(a), len(b)), flush=True)
    ok("HER07: scouter=1 cambia el modelo en %d/4 formas" % n, n == 4)


def scouter():
    """HER07 (parte sin juego): el cristal del scouter del 281 es el mismo que el de
    Raditz por pixeles, y el emblema del logo tiene alfa y contraste legibles."""
    import hashlib

    import numpy as np

    import model_render as mr
    import roster_build as rb
    import vista_heroe as vh
    base = run()
    work = os.path.join(base, "work")
    os.makedirs(work, exist_ok=True)
    afs = rb.Afs(os.path.join(US, "data_cmn.afs"), work)
    m281 = mr.Model(bytes(afs.entry(vh.SCOUTER_DONANTE)))
    m360 = mr.Model(bytes(afs.entry(360)))          # catalog_b3.cat: Raditz
    n_base = len(mr.Model(bytes(afs.entry(vh.KFID[0][0]))).mats)

    tm = np.asarray(m281.tri_mat)
    idx = np.where(tm >= n_base)[0]
    extra = int(tm[idx[0]]) if len(idx) else -1
    ok("HER07: el 281 trae material de scouter aparte", 0 < len(idx) < 400 and extra > 0, "idx=%d" % len(idx))
    if not (0 <= extra < len(m281.mats)) or not len(idx):
        return
    mat = m281.mats[extra]
    ti = int(mat["tex"])
    tex281 = np.asarray(m281.tex[ti], float)
    if tex281.max() > 1.5:
        tex281 = tex281 / 255.0

    def verde_de(t):
        a = np.asarray(t, float)
        if a.max() > 1.5:
            a = a / 255.0
        vis = a[..., 3] > 0.05 if a.shape[-1] > 3 else np.ones(a.shape[:2], bool)
        v = a[vis][:, :3]
        if not len(v):
            return None
        r, g, b = v[:, 0], v[:, 1], v[:, 2]
        m = (g > 1.25 * r) & (g > 1.6 * b) & (g > 0.15)
        return (float(m.mean()), np.asarray(v[m].mean(0)) if m.any() else None)

    # Raditz: la textura del cristal es la de su misma forma y alfa; se elige por
    # pixeles (diferencia media RGB) y se exige el mismo verde (<= 0.02 por canal).
    cand = []
    for i, t in enumerate(m360.tex):
        a = np.asarray(t, float)
        if a.max() > 1.5:
            a = a / 255.0
        if a.shape != tex281.shape:
            continue
        cand.append((float(np.abs(a[..., :3] - tex281[..., :3]).mean()), i, a))
    cand.sort()
    g281 = verde_de(tex281)
    if not cand:
        ok("HER07: cristal del 281 == textura de Raditz 360 (pixeles)", False, "sin textura de la misma forma")
    else:
        dif, i360, a360 = cand[0]
        g360 = verde_de(a360)
        dverde = (float(np.abs(g281[1] - g360[1]).max())
                  if g281 and g281[1] is not None and g360 and g360[1] is not None else 9.9)
        ok("HER07: cristal del 281 == Raditz 360 tex %d (pixeles)" % i360,
           dif <= 0.02 and g281 and g281[1] is not None and dverde <= 0.03,
           "dif %.4f verde %.4f (281 %s / raditz %s)" % (
               dif, dverde, np.round(g281[1], 3).tolist() if g281 and g281[1] is not None else None,
               np.round(g360[1], 3).tolist() if g360 and g360[1] is not None else None))
    v = tex281.reshape(-1, 4)
    v = v[v[:, 3] > 0.05][:, :3]
    ok("HER07: la textura del cristal tiene pixeles con alfa", len(v) > 32, "n=%d" % len(v))
    if len(v):
        r, g, b = v[:, 0], v[:, 1], v[:, 2]
        verde = (g > 1.25 * r) & (g > 1.6 * b) & (g > 0.15)
        ok("HER07: el cristal es verde (pixeles)", verde.any(),
           "verde=%.1f%% medio=%s" % (100.0 * verde.mean(),
                                      np.round(v[verde].mean(0), 3).tolist() if verde.any() else None))

    # Logo: el emblema se dibuja por codigo con alfa limpio y contraste alto (mismo
    # tamano UV con y sin pack HD; solo cambia la resolucion de la textura del torso).
    for logo in range(1, 8):
        e = vh._emblema(logo)
        alfa = e[..., 3]
        dentro = alfa > 0.5
        borde = np.concatenate([alfa[0], alfa[-1], alfa[:, 0], alfa[:, -1]])
        rgb = (e[..., :3][dentro] * 255).astype(int)
        if not len(rgb):
            ok("HER07: emblema %d con alfa" % logo, False)
            continue
        # luminancia de la tinta (lo mas oscuro) contra el disco casi blanco
        lum = 0.2126 * rgb[:, 0] + 0.7152 * rgb[:, 1] + 0.0722 * rgb[:, 2]
        contraste = (255.0 + 5) / (max(float(np.percentile(lum, 10)), 0.0) + 5)
        ok("HER07: logo %d (alfa y contraste)" % logo,
           float(alfa.max()) >= 0.99 and int((borde > 0.5).sum()) == 0 and contraste >= 2.0,
           "alfa max %.2f borde %d contraste %.1f" % (float(alfa.max()), int((borde > 0.5).sum()), contraste))


def piezas():
    """HER10 (parte sin juego): presupuesto de ventanas de las piezas aprobadas y
    presencia de los cuerpos de Trunks 392/394 en las zonas declaradas."""
    import re
    BASE = os.path.join(CAC, "piezas_b3_base.json")
    with open(BASE, encoding="utf-8") as f:
        cat = json.load(f)
    with open(os.path.join(CAC, "piezas_b3_base_medidas.json"), encoding="utf-8") as f:
        medidas = json.load(f)
    aprobadas = {z: [e for e in v if e.get("ok") is True and e.get("validacion_visual") == "perfecto"]
                 for z, v in cat.items()}
    n = sum(len(v) for v in aprobadas.values())
    ok("HER10: piezas aprobadas con validacion visual (frente/espalda)", n == 72, "n=%d" % n)
    for fid, zonas in ((392, ("torso", "piernas", "botas")), (394, ("piernas", "botas"))):
        falta = [z for z in zonas if fid not in {e["fid"] for e in aprobadas.get(z, [])}]
        ok("HER10: Trunks %d aprobado en %s" % (fid, "/".join(zonas)), not falta, "falta en %s" % falta)
    peor, sin_cifra = (0, ""), []
    for z, v in aprobadas.items():
        for e in v:
            m = re.search(r"'total':\s*(\d+)", e.get("notas") or "")
            if not m:
                sin_cifra.append((z, e["fid"]))
                continue
            total = int(m.group(1))
            if total > peor[0]:
                peor = (total, "%s:%d" % (z, e["fid"]))
    ok("HER10: toda pieza medida dentro del presupuesto de ventanas (3261)", peor[0] <= 3261,
       "peor %d en %s" % peor)
    ok("HER10: cifra de ventanas en las piezas aprobadas", peor[0] > 0,
       "%d con cifra; %d aprobadas en juego sin nota de ventanas" % (n - len(sin_cifra), len(sin_cifra)))
    md = medidas.get("264") or {}
    ok("HER10: el cuerpo base 264 esta medido (vertices/tris)", bool(md.get("nverts") and md.get("ntris")),
       "%s" % {k: md.get(k) for k in ("nverts", "ntris", "altura")})


def silueta():
    """HER10: silueta de cada pieza aprobada contra el cuerpo base, frente y espalda,
    con la MISMA camara; pasa si la diferencia es <= 2 % del recuadro. Escribe CSV y
    hoja de contacto fuera del repo (nada de arte de terceros dentro)."""
    import numpy as np

    import model_render as mr
    import roster_build as rb
    import vista_heroe as vh
    base_dir = run()
    juego = os.path.join(base_dir, "juego")
    os.makedirs(os.path.join(juego, "mods"), exist_ok=True)
    work = os.path.join(base_dir, "work")
    os.makedirs(work, exist_ok=True)
    afs = rb.Afs(os.path.join(US, "data_cmn.afs"), work)
    CERO = (0, 0, 0)
    FICHA = ("cuerpo=0;traje=0;partes=%s;pelo=0;c1=0,0,0;c2=0,0,0;logo=0;scouter=0;nombre=%s;donante=0"
             % ("|".join("0,0,0" for _ in range(4)), "%s"))

    def exportar(nombre, extra=""):
        r = vh.exportar_personaje(juego, FICHA % nombre + extra, afs)
        if r.get("estado") != "ok":
            raise RuntimeError(str(r)[:300])
        return os.path.join(juego, "mods", "heroe_" + nombre, "modelos", "base.bin")

    base = mr.Model(bytes(open(exportar("sil_base"), "rb").read()))
    P, _ = base.posed(None)
    lo, hi = P.min(0), P.max(0)
    ctr = (lo + hi) / 2
    esc = 480 * 0.92 / max(float(hi[1] - lo[1]), float(hi[0] - lo[0]))
    VISTAS = (("frente", np.eye(3)),
              ("espalda", np.array([[0, 0, 1], [0, 1, 0], [-1, 0, 0]], float)))

    def mascara(m, view):
        img = mr.render(m, (480, 480), view, ctr, esc, ss=2)
        return img[..., 3] > 127

    def blanco(img):
        from PIL import Image as _I
        a = _I.fromarray(img).convert("RGBA")
        out = _I.new("RGB", a.size, (255, 255, 255))
        out.paste(a, (0, 0), a)
        return np.asarray(out)

    base_m = {n: mascara(base, v) for n, v in VISTAS}
    with open(os.path.join(CAC, "piezas_b3_base.json"), encoding="utf-8") as f:
        cat = json.load(f)
    filas, fallos = [], []
    for zona, v in cat.items():
        for e in v:
            if not (e.get("ok") is True and e.get("validacion_visual") == "perfecto"):
                continue
            fid = int(e["fid"])
            try:
                p = mr.Model(bytes(open(exportar("sil_%s_%d" % (zona, fid), ";b3_%s=%d" % (zona, fid)), "rb").read()))
            except Exception as exc:   # noqa: BLE001
                fallos.append((zona, fid, "fallo de montaje: %s" % exc))
                continue
            difs = {}
            for n, view in VISTAS:
                m = mascara(p, view)
                difs[n] = float((m ^ base_m[n]).sum()) / base_m[n].size
            filas.append((zona, fid, difs["frente"], difs["espalda"], str(e.get("nombre", ""))))
            if max(difs.values()) > 0.02:
                fallos.append((zona, fid, "frente %.2f%% espalda %.2f%%" % (100 * difs["frente"], 100 * difs["espalda"])))
    ok("HER10: silueta de las piezas aprobadas <= 2 %% del recuadro (%d/%d)" % (len(filas) - len(fallos), len(filas)),
       not fallos, str(fallos[:6]))
    salida = os.path.join(os.path.dirname(WORK), "HER10_silueta")
    os.makedirs(salida, exist_ok=True)
    with open(os.path.join(salida, "silueta.csv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("zona;fid;frente_pct;espalda_pct;nombre\n")
        for z, fid, df, de, nom in filas:
            f.write("%s;%d;%.3f;%.3f;%s\n" % (z, fid, 100 * df, 100 * de, nom))
    try:   # hoja de contacto con las 24 peores (para el usuario)
        from PIL import Image, ImageDraw
        peores = sorted(filas, key=lambda x: max(x[2], x[3]), reverse=True)[:24]
        W, H, S = 520, 30 + 90 * len(peores), 80
        hoja = Image.new("RGB", (W, H), (250, 250, 250))
        dr = ImageDraw.Draw(hoja)
        dr.text((6, 6), "HEROE-10: peores piezas por silueta (frente/espalda, % del recuadro; tope 2 %)", fill=(0, 0, 0))
        y = 24
        for z, fid, df, de, nom in peores:
            p = mr.Model(bytes(open(os.path.join(juego, "mods", "heroe_sil_%s_%d" % (z, fid), "modelos", "base.bin"), "rb").read()))
            img = mr.render(p, (S, S), np.eye(3), ctr, esc, ss=2)
            hoja.paste(Image.fromarray(blanco(img)), (6, y))
            img2 = mr.render(p, (S, S), VISTAS[1][1], ctr, esc, ss=2)
            hoja.paste(Image.fromarray(blanco(img2)), (6 + S + 6, y))
            est = "OK" if max(df, de) <= 0.02 else "FUERA"
            dr.text((6 + 2 * S + 12, y + 6), "%s %d %s" % (z, fid, est), fill=(200, 30, 30) if est == "FUERA" else (20, 20, 20))
            dr.text((6 + 2 * S + 12, y + 24), "frente %.2f%%  espalda %.2f%%" % (100 * df, 100 * de), fill=(20, 20, 20))
            dr.text((6 + 2 * S + 12, y + 42), nom[:22], fill=(90, 90, 90))
            y += 90
        hoja.save(os.path.join(salida, "hoja_silueta.png"))
    except Exception as exc:   # noqa: BLE001 (la hoja no es imprescindible)
        print("heroe_banco: hoja de silueta no hecha: %s" % exc)
    print("heroe_banco: silueta -> %s" % salida)


def nombres():
    import nombre_levelup as nl
    import roster_build as rb
    base = run()
    mods = os.path.join(base, "mods")
    os.makedirs(mods, exist_ok=True)
    nombres_d = {35: "Kakar\u00f6to", 36: "Mirai", 37: "Hero"}
    rc = nl.montar(mods, US, nombres_d)
    ok("HER04: montar mods/heroe_nombres (rc 0)", rc == 0)
    mod = os.path.join(mods, "heroe_nombres")
    try:
        with open(os.path.join(mod, "nombres.json"), encoding="utf-8") as f:
            js = {int(k): v for k, v in json.load(f).items()}
        ok("HER04: nombres.json con las 3 casillas", all(k in js for k in (35, 36, 37)), str(js))
    except (OSError, ValueError) as exc:
        ok("HER04: nombres.json", False, str(exc))
    langs = [l for l in nl.LANGS if os.path.isfile(os.path.join(US, l))]
    n_ok, detalle = 0, []
    work = os.path.join(base, "work")
    os.makedirs(work, exist_ok=True)
    for afs_name in langs:
        p = os.path.join(mod, "us", afs_name, str(nl.ENTRADA), "geom.bin")
        try:
            with open(p, "rb") as f:
                buf = rb.unlzx(f.read(), work)
            n = struct.unpack(">I", buf[0x10:0x14])[0]
            if n == 68:
                n_ok += 1
            detalle.append("%s=%d" % (afs_name, n))
        except (OSError, struct.error):
            detalle.append("%s=?" % afs_name)
    ok("HER04: 1974 con 68 texturas en %d/%d idiomas" % (n_ok, len(langs)), bool(langs) and n_ok == len(langs),
       " ".join(detalle))


def main():
    cual = sys.argv[1] if len(sys.argv) > 1 else "todo"
    os.makedirs(WORK, exist_ok=True)
    if cual in ("todo", "ida-vuelta"):
        ida_vuelta()
    if cual in ("todo", "sin-id"):
        sin_id()
    if cual in ("todo", "plan"):
        plan()
    if cual in ("todo", "textos"):
        textos()
    if cual in ("todo", "arte"):
        arte()
    if cual in ("todo", "nombres"):
        nombres()
    if cual in ("todo", "scouter"):
        scouter()
    if cual in ("todo", "piezas"):
        piezas()
    if cual in ("todo", "silueta"):
        silueta()
    fallos = [r for r in RES if not r[1]]
    print("\nheroe_banco %s: %d/%d PASS" % (cual, len(RES) - len(fallos), len(RES)))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
