#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pelo_nativos03.py - PELO-03 (v3): peinados NATIVOS ENTEROS de todos los luchadores de B3 HD, trasplantados intactos.

Sin mezclar ni inventar: el pelo del donante (`vista_heroe._pelo_donante`) entra por `injertar_pelo.injertar` con SU atlas
(`vista_heroe._atlas_pelo`: textura recortada y cocida con su rampa, 2 tonos y tinta propios, UV por vertice), no con el
reparto por altura. Variacion permitida: jaula FFD suave (+-15 %), siempre por encima de las cejas.

  inventario(): recorre roster_db (todos los ids/modelos), saca el pelo de cada modelo y lo deduplica por forma+textura.
  trasplantar(): donante -> cuerpo anfitrion (Goku 264, Vegeta 416), con atlas.
  hoja(): fila 1 = donante con su pelo; fila 2 = trasplante a Goku; 4 vistas.

Uso:  python pelo_nativos03.py --inventario | --selftest [--salida E:\\DBZ3HD_trabajo\\pelo03]
"""
import hashlib
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (HERE, os.path.join(HERE, "..", "mod center hd", "cac"), os.path.join(HERE, "..", "mod center hd")):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.append(_p)

SALIDA = r"E:\DBZ3HD_trabajo\pelo03"
TOPE = 3261          # ventanas
MIN_TRIS = 40        # por debajo: calvo / casco / adorno, no es un peinado
EPS = 1e-6


def parchear_sample(mr):
    """mr._sample falla con v = -1e-17 (mod con v negativo diminuto): se desplaza 1e-6 (solo dentro de este proceso)."""
    if not getattr(mr._sample, "_parcheado", False):
        _o = mr._sample
        mr._sample = lambda img, u, v: _o(img, np.asarray(u) + EPS, np.asarray(v) + EPS)
        mr._sample._parcheado = True


def _ids_y_fids(vh):
    """[(fid, nombre, id, rol)] sin repetir fid; incluye los donantes sueltos de KFID."""
    import roster_build as rb
    vistos, out = set(), []
    for cid, e in sorted(rb.IDS.items()):
        for i, f in enumerate(e.get("models", [])):
            if isinstance(f, int) and f < 4294967295 and f not in vistos:
                vistos.add(f)
                out.append((f, e["name"], cid, i))
    for cid, fids in vh.KFID.items():
        for f in fids:
            if f not in vistos:
                vistos.add(f)
                out.append((f, "KFID%d" % cid, cid, -1))
    return out


def pelo_de(vh, afs, fid):
    """(modelo, bytes, ht, dvs, P, huella) del pelo propio del modelo `fid`, o None si calvo / sin cabeza."""
    mr = vh.mr
    b = bytes(afs.entry(fid))
    m = mr.Model(b)
    if m.bone("HEAD") is None:
        return None
    pose = vh.pose_de(m)
    P, _ = m.posed(pose)
    z = vh.zonas_ficha(m, P, pelo_color=True)
    ht = [i for i, x in enumerate(z) if x == "pelo"]
    if len(ht) < MIN_TRIS:
        return None
    dvs = np.unique(m.tris[ht].reshape(-1))
    inv = np.linalg.inv(np.asarray(m.worlds(pose))[m.bone("HEAD")])
    loc = P[dvs] @ inv[:3, :3].T + inv[:3, 3]
    tex = sorted({int(m.mats[m.tri_mat[t]]["tex"]) for t in ht})
    th = hashlib.md5(b"".join(np.asarray(m.tex[t]).tobytes() for t in tex if t < len(m.tex))).hexdigest()[:8]
    forma = hashlib.md5(np.round(loc, 2).tobytes()).hexdigest()[:8]
    return m, b, ht, dvs, P, (len(ht), forma, th)


def inventario(vh, afs, cache=None):
    """Lista de dicts {fid, nombre, id, tris, huella, dup_de}; cacheada en json (la lectura tarda ~2 min)."""
    if cache and os.path.isfile(cache):
        return json.load(open(cache, encoding="utf-8"))
    res, vistos = [], {}
    for fid, nom, cid, rol in _ids_y_fids(vh):
        try:
            r = pelo_de(vh, afs, fid)
        except Exception:   # noqa: BLE001 (entrada que no es luchador)
            continue
        if r is None:
            continue
        h = list(r[5])
        k = tuple(h)
        res.append({"fid": fid, "nombre": nom, "id": cid, "rol": rol, "tris": h[0], "huella": h,
                    "dup_de": vistos.get(k)})
        vistos.setdefault(k, fid)
    if cache:
        json.dump(res, open(cache, "w", encoding="utf-8"), indent=0)
    return res


def unicos(inv):
    return [e for e in inv if e["dup_de"] is None]


# ---------------------------------------------------------------- trasplante con atlas del donante
def ffd_suave(Q, QN, model, P, zonas, alto=0.0):
    """Estira en y (+-15 % del alto del pelo) alrededor de la base, solo por encima de las cejas (limite sup. de la cara)."""
    if not alto:
        return Q
    alto = float(np.clip(alto, -0.15, 0.15))
    cara = np.unique(model.tris[[i for i, z in enumerate(zonas) if z == "cara"]].reshape(-1))
    ceja = P[cara][:, 1].max()
    y0, y1 = Q[:, 1].min(), Q[:, 1].max()
    Q = Q.copy()
    w = np.clip((Q[:, 1] - max(y0, ceja)) / max(y1 - max(y0, ceja), 1e-6), 0, 1)
    Q[:, 1] += alto * (y1 - y0) * w
    return Q


LIT_COCER = 1.0      # luz con la que se cuece la rampa en el atlas (1 = plena: sin oscurecer; el blanco sigue blanco)
CORONA_MIN = 0.35    # si el pelo detectado no sube de este tramo (ojos -> coronilla) falta la corona del donante
_AUG = {"on": False}


def completar_pelo(vh, model, P, z):
    """Donante con la corona fuera del pelo (Videl: la cupula va en el material del cuerpo, solo salian bucles y flequillo):
    si el pelo detectado no pasa del 35 % del tramo ojos -> coronilla, los triangulos de cuerpo (zona 0) por encima del 30 %
    del tramo pasan a pelo. Devuelve zonas (nuevas si cambia algo)."""
    L, W, inv = vh._local_head(model, P)
    if L is None:
        return z
    cara = [i for i, x in enumerate(z) if x == "cara"]
    mk = vh._marcas_cara(model, L, vh._tris_cara(model), W, inv)
    ht = [i for i, x in enumerate(z) if x == "pelo"]
    if not cara or not mk or not ht:
        return z
    ey = float((mk[0][0][1] + mk[0][1][1]) / 2)
    top = float(L[np.unique(model.tris[cara].reshape(-1))][:, 1].max())
    if top <= ey or L[np.unique(model.tris[ht].reshape(-1))][:, 1].max() >= ey + CORONA_MIN * (top - ey):
        return z
    corte = ey + 0.3 * (top - ey)
    z = list(z)
    for i, x in enumerate(z):
        if x == 0 and L[model.tris[i]][:, 1].mean() > corte:
            z[i] = "pelo"
    return z


def atlas03(dm, ht, dvs, tris, Q, QN, lado=256, lit=None):
    """Atlas del pelo del donante como `vista_heroe._atlas_pelo` pero (a) cocido con luz `lit` (def. LIT_COCER; 1 = sin
    oscurecer) y (b) si los recortes no caben a 1:1 se REDUCEN (caja) por un factor comun hasta que caben en lado x lado.
    Devuelve (Q, QN, tris, uv, rgba, resumen) o None."""
    from PIL import Image
    lit = LIT_COCER if lit is None else lit
    uvd = np.asarray(dm.uv, float)[np.asarray(dvs, int)]
    uvd = np.where((uvd < 0) | (uvd > 1), uvd % 1.0, uvd)   # UV fuera de [0,1]: la textura se repite
    tt = np.asarray(tris, int)
    texs, solidos = list(dm.tex), {}   # material sin textura (Gohan M, Goten): color plano = textura 4x4 de su difuso
    grupos = {}
    for k, t in enumerate(ht):
        mat = dm.mats[dm.tri_mat[int(t)]]
        tx, sh = int(mat["tex"]), int(mat["shader"])
        if tx >= len(dm.tex):
            d = np.ones(4, np.float32)
            d[:min(4, len(np.ravel(mat["diffuse"])))] = np.ravel(mat["diffuse"])[:4]
            if tx not in solidos:
                solidos[tx] = len(texs)
                texs.append(np.tile(d, (4, 4, 1)))
            tx, sh = solidos[tx], 0xFFFFFFFF
        grupos.setdefault((tx, sh), []).append(int(k))
    plan = []
    for (tx, sh), ks in sorted(grupos.items()):
        u = uvd[tt[ks].reshape(-1)]
        h, w = texs[tx].shape[:2]
        x0, x1 = max(0, int(np.floor(u[:, 0].min() * w)) - 1), min(w, int(np.ceil(u[:, 0].max() * w)) + 1)
        y0, y1 = max(0, int(np.floor(u[:, 1].min() * h)) - 1), min(h, int(np.ceil(u[:, 1].max() * h)) + 1)
        if tx in solidos.values():
            x0, y0, x1, y1 = 0, 0, w, h
        if x1 - x0 < 2 or y1 - y0 < 2:
            return None
        plan.append({"tx": tx, "sh": sh, "x0": x0, "y0": y0, "w": w, "h": h, "ks": ks, "rw": x1 - x0, "rh": y1 - y0})

    def empaquetar(s):
        x = y = fila = 0
        for p in sorted(plan, key=lambda p: -p["rh"]):
            sw, sh_ = max(2, int(round(p["rw"] * s))), max(2, int(round(p["rh"] * s)))
            if x + sw > lado:
                x, y, fila = 0, y + fila + 1, 0
            if y + sh_ > lado or sw > lado:
                return False
            p.update(rx=x, ry=y, sw=sw, sh_=sh_)
            x += sw + 1
            fila = max(fila, sh_)
        return True
    s = 1.0
    while not empaquetar(s):
        s *= 0.93
        if s < 0.15:
            return None
    rgb0 = np.asarray(texs[plan[0]["tx"]])[..., :3].reshape(-1, 3)
    atlas = np.zeros((lado, lado, 4), np.float32)
    atlas[..., :3] = np.median(rgb0[np.arange(0, len(rgb0), max(1, len(rgb0) // 512))], 0)
    atlas[..., 3] = 1.0
    for p in plan:
        base = np.array(texs[p["tx"]][p["y0"]:p["y0"] + p["rh"], p["x0"]:p["x0"] + p["rw"]], np.float32)
        if p["sh"] != 0xFFFFFFFF and p["sh"] < len(texs):
            ramp = texs[p["sh"]]
            rh_, rw_ = ramp.shape[:2]
            band = np.clip(np.round(base[..., 3] * 15), 0, 15)
            yi = np.clip(((band + 0.5) * rh_ / 16).astype(int), 0, rh_ - 1)
            xi = int(round(lit * (rw_ - 1)))
            c = np.clip(base[..., :3] - ramp[yi, xi, :3], 0.0, 1.0)
            base[..., :3] = np.where((band >= 15)[..., None], base[..., :3], c)   # banda 15 = sin sombrear (como el juego)
        base[..., 3] = 1.0
        if (p["sw"], p["sh_"]) != (p["rw"], p["rh"]):
            im = Image.fromarray((np.clip(base, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")
            base = np.asarray(im.resize((p["sw"], p["sh_"]), Image.BOX), np.float32) / 255.0
        atlas[p["ry"]:p["ry"] + p["sh_"], p["rx"]:p["rx"] + p["sw"]] = base
    mapa = {(p["tx"], p["sh"]): p for p in plan}
    clave, P2, N2, U2, T2 = {}, [], [], [], []
    for k, t in enumerate(ht):
        mat = dm.mats[dm.tri_mat[int(t)]]
        tx, sh = int(mat["tex"]), int(mat["shader"])
        if tx in solidos:
            tx, sh = solidos[tx], 0xFFFFFFFF
        p = mapa[(tx, sh)]
        fx, fy = p["sw"] / p["rw"], p["sh_"] / p["rh"]
        tri = []
        for c in range(3):
            v = int(tt[k][c])
            uu, vv = (0.5, 0.5) if tx in solidos.values() else (uvd[v][0], uvd[v][1])
            px = p["rx"] + (uu * p["w"] - p["x0"]) * fx
            py = p["ry"] + (vv * p["h"] - p["y0"]) * fy
            kk = (v, round(px, 3), round(py, 3))
            i = clave.get(kk)
            if i is None:
                i = clave[kk] = len(P2)
                P2.append(np.asarray(Q, float)[v])
                N2.append(np.asarray(QN, float)[v])
                U2.append((px / lado, py / lado))
            tri.append(i)
        if len(set(tri)) == 3:
            T2.append(tuple(tri))
    if not T2:
        return None
    res = {"grupos": len(plan), "reduccion": round(s, 2), "vertices": "%d -> %d" % (len(np.unique(tt)), len(P2))}
    return (np.asarray(P2, float), np.asarray(N2, float), np.asarray(T2, int), np.asarray(U2, float),
            (np.clip(atlas, 0, 1) * 255 + 0.5).astype(np.uint8), res)


def trasplantar(vh, afs, don_fid, host_fid, alto=0.0, dm_cache={}):
    """Pelo ENTERO de `don_fid` sobre el cuerpo `host_fid`. Devuelve (bin, resumen, Q, causa_fallo)."""
    import injertar_pelo as ip
    don_b = bytes(afs.entry(don_fid))
    amb = bytes(afs.entry(host_fid))
    model = vh.mr.Model(amb)
    P, _ = model.posed(vh.pose_de(model))
    zonas = vh.zonas_ficha(model, P)
    if sum(z == "pelo" for z in zonas) < 50:
        zonas = vh.zonas_ficha(model, P, pelo_color=True)
    _o = vh.zonas_ficha
    vh.zonas_ficha = lambda m, p, pelo_color=False: (completar_pelo(vh, m, p, _o(m, p, pelo_color)) if pelo_color else _o(m, p, pelo_color))
    try:
        d = vh._pelo_donante(model, P, zonas, don_b)
    finally:
        vh.zonas_ficha = _o
    if d is None:
        return None, None, None, "_pelo_donante sin resultado"
    dm, ht, dvs, Q, QN, tris, _raiz = d
    Q = vh._pelo_fuera_de_cara(model, P, zonas, Q, tris)
    host, excl = vh._host_pelo_exclusivo(model, zonas)
    if host is None or not excl:
        return None, None, None, "el material del pelo del anfitrion no es exclusivo"
    slot = vh._slot_atlas_pelo(amb, model)
    if slot is None:
        return None, None, None, "sin textura libre 256 en el anfitrion"
    Q = ffd_suave(Q, QN, model, P, zonas, alto)
    at = atlas03(dm, ht, dvs, tris, Q, QN)
    if at is None:
        return None, None, None, "atlas no se puede componer (recorte de textura vacio)"
    Q, QN, tris, uvs, rgba, est = at
    out, r = ip.injertar(amb, model, [z == "pelo" for z in zonas], Q, QN, tris,
                         cara_mask=[z == "cara" for z in zonas], uvs=uvs, tex_destino=slot, tex_rgba=rgba)
    v = r.get("ventanas")
    if not v:
        return None, r, None, "sin injerto: %s" % r.get("pelo")
    if v["total"] > TOPE or "decimado" in r:
        return None, r, None, "ventanas %d > %d" % (v["total"], TOPE)
    r["atlas"] = est
    return out, r, Q, None


# ---------------------------------------------------------------- hoja
def hoja(vh, filas, path, size=300, vistas=None):
    """filas: [(titulo, bin, Q)] -> png de 4 vistas por fila (frente, 3/4, perfil, espalda)."""
    from PIL import Image, ImageDraw
    mr = vh.mr
    parchear_sample(mr)
    vistas = vistas or [("frente", 0), ("3/4", 40), ("perfil", 90), ("espalda", 180)]
    img = Image.new("RGBA", (size * len(vistas), (size + 18) * len(filas)), (60, 64, 72, 255))
    d = ImageDraw.Draw(img)
    for f, (tit, data, Q) in enumerate(filas):
        m = mr.Model(bytes(data))
        pose = vh.pose_de(m)
        ctr = (Q.min(0) + Q.max(0)) / 2
        esc = size * 0.82 / max(float(np.ptp(Q, axis=0).max()), 1e-6)
        d.text((6, f * (size + 18) + 3), tit, fill=(255, 255, 255, 255))
        for k, (nom, yaw) in enumerate(vistas):
            v = mr.render(m, (size, size), mr.rot_x(6) @ mr.rot_y(yaw), ctr, esc, pose=pose, outline=1.6, ss=2)
            img.alpha_composite(Image.fromarray(v), (k * size, f * (size + 18) + 18))
            d.text((k * size + 6, f * (size + 18) + 18 + size - 14), nom, fill=(220, 220, 220, 255))
    img.convert("RGB").save(path)


# ---------------------------------------------------------------- elegidos (se rellena tras el inventario)
NO_PELO = ("PICCOLO", "KAIO", "FREEZA", "BUU", "CELL", "SAIBAIMAN", "DRAGON", "COOLER", "DABURA", "GINEW")   # casco, piel o cuerno
# candidatos por orden de variedad (fid): Gohan nino/adulto, Saiyaman, Trunks nino/adulto, Raditz, Videl, Uub, androides, Gero,
# Broly, Recoome, Yamcha, SSJ de Goku, Goten, Gohan M. Los que no caben en el atlas de 256 se anotan y se pasa al siguiente.
CANDIDATOS = [246, 227, 294, 384, 360, 411, 405, 70, 75, 80, 92, 119, 366, 446, 265, 277, 300, 395, 238, 225, 247, 383, 161]


def elegir(inv, n=12):
    """Peinados unicos con pelo de verdad (sin NO_PELO), fuera de PELOS_NATIVOS, en el orden de CANDIDATOS."""
    por_fid = {e["fid"]: e for e in unicos(inv)}
    return [por_fid[f] for f in CANDIDATOS if f in por_fid and not any(k in por_fid[f]["nombre"].upper() for k in NO_PELO)]


VISTAS2 = [("frente", 0), ("3/4", 40)]


def catalogo(salida, maximo=0):
    """Recorre los peinados reales unicos del inventario sobre Goku 264: hojas (donante + trasplante, frente y 3/4) y
    catalogo_pelo04.csv (fid, nombre, ventanas, OK/fallo, motivo). Devuelve las filas del csv."""
    import csv
    import vista_heroe as vh
    os.makedirs(os.path.join(salida, "w"), exist_ok=True)
    afs = vh.abrir_afs(None, os.path.join(salida, "w"))
    inv = inventario(vh, afs, os.path.join(SALIDA, "inventario_pelo03.json"))
    reales = [e for e in unicos(inv) if not any(k in e["nombre"].upper() for k in NO_PELO)]
    if maximo:
        reales = reales[:maximo]
    filas, tabla = [], []
    for e in reales:
        etq = "%d %s" % (e["fid"], e["nombre"])
        try:
            out, r, Q, causa = trasplantar(vh, afs, e["fid"], 264)
        except Exception as ex:   # noqa: BLE001 (se anota y se sigue)
            out, r, Q, causa = None, None, None, "excepcion %s: %s" % (type(ex).__name__, ex)
        r0 = pelo_de(vh, afs, e["fid"])
        filas.append((etq + " (donante)", bytes(afs.entry(e["fid"])), r0[4][r0[3]]))
        v = r["ventanas"]["total"] if r and r.get("ventanas") else ""
        if out:
            filas.append((etq + " -> Goku (ventanas %s, reduccion atlas %s)" % (v, r["atlas"].get("reduccion")), out, Q))
        tabla.append([e["fid"], e["nombre"], v, "OK" if out else "FALLO", causa or ""])
    # 2 filas por peinado, 3 peinados por hoja; filas ya intercaladas (fallo = solo donante)
    ho, bloque = 1, []
    for i, f in enumerate(filas):
        bloque.append(f)
        sig_es_don = i + 1 >= len(filas) or filas[i + 1][0].endswith("(donante)")
        if len(bloque) >= 6 and sig_es_don:
            hoja(vh, bloque, os.path.join(salida, "hoja_pelo04_%02d.png" % ho), 240, VISTAS2 + VISTAS2[:0])
            ho, bloque = ho + 1, []
    if bloque:
        hoja(vh, bloque, os.path.join(salida, "hoja_pelo04_%02d.png" % ho), 240, VISTAS2)
    with open(os.path.join(salida, "catalogo_pelo04.csv"), "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["fid", "nombre", "ventanas", "resultado", "motivo"])
        w.writerows(tabla)
    return tabla


def selftest(salida):
    import vista_heroe as vh
    os.makedirs(os.path.join(salida, "w"), exist_ok=True)
    afs = vh.abrir_afs(None, os.path.join(salida, "w"))
    inv = inventario(vh, afs, os.path.join(salida, "inventario_pelo03.json"))
    assert inv and all(e["tris"] >= MIN_TRIS for e in inv), "inventario vacio o con calvos"
    uni = unicos(inv)
    reales = [e for e in uni if not any(k in e["nombre"].upper() for k in NO_PELO)]
    assert 10 < len(reales) <= len(uni) <= len(inv)
    q = ffd_suave(np.array([[0, 1.0, 0], [0, 2, 0]]), None, _FakeM(), np.array([[0, 0.5, 0]]), ["cara"], 0.9)
    assert abs(q[1, 1] - 2.15) < 1e-9 and q[0, 1] == 1.0, "FFD no limita a 15 % / toca por debajo de las cejas"
    _selftest_atlas()
    print("inventario: %d modelos con pelo, %d formas distintas, %d de peinado real" % (len(inv), len(uni), len(reales)))
    filas, resumen, ok = [], [], 0
    for e in elegir(inv):
        if ok >= 12:
            break
        etq = "%d %s" % (e["fid"], e["nombre"])
        out, r, Q, causa = trasplantar(vh, afs, e["fid"], 264)
        if causa:
            resumen.append("FALLA %s: %s" % (etq, causa))
            continue
        v = r["ventanas"]
        assert v["total"] <= TOPE
        ov, rv, _q, cv = trasplantar(vh, afs, e["fid"], 416)
        r0 = pelo_de(vh, afs, e["fid"])
        filas.append((etq + " (donante, su pelo)", bytes(afs.entry(e["fid"])), r0[4][r0[3]]))
        filas.append((etq + " -> Goku 264 (ventanas %d)" % v["total"], out, Q))
        resumen.append("OK %s: Goku %d, Vegeta %s (%d tris)" % (etq, v["total"], rv["ventanas"]["total"] if rv and not cv else "FALLA " + str(cv), e["tris"]))
        ok += 1
    assert ok >= 1, "ningun trasplante"
    for k in range(0, len(filas), 6):
        hoja(vh, filas[k:k + 6], os.path.join(salida, "hoja_pelo03.png" if k == 0 else "hoja_pelo03_%d.png" % (k // 6 + 1)))
    print(chr(10).join(resumen))
    print("OK selftest PELO-03: %d trasplantes, hoja en %s" % (ok, os.path.join(salida, "hoja_pelo03.png")))


class _FakeM:
    tris = np.array([[0, 0, 0]])


class _FakeDM:
    """Modelo minimo: 2 texturas 256x256 (blanca y gris, alfa 0,5 = banda 7) con rampa gris, 2 triangulos."""
    def __init__(self):
        ramp = np.zeros((64, 64, 4), np.float32)
        ramp[..., :3] = np.linspace(0.3, 0.0, 64)[None, :, None]   # resta 0,3 en sombra .. 0 a plena luz
        t = np.ones((256, 256, 4), np.float32)
        t[..., 3] = 0.5
        self.tex = [t, t * np.array([.5, .5, .5, 1], np.float32), ramp]
        self.uv = np.array([[0, 0], [1, 0], [0, 1], [0, 0], [1, 0], [0, 1]], float)
        self.tri_mat = [0, 1]
        self.mats = [{"tex": 0, "shader": 2}, {"tex": 1, "shader": 2}]


def _selftest_atlas():
    q = np.zeros((6, 3))
    r = atlas03(_FakeDM(), [0, 1], np.arange(6), [(0, 1, 2), (3, 4, 5)], q, q)
    assert r, "atlas03: no compone dos 256x256 (deben reducirse)"
    rgba, est = r[4], r[5]
    assert rgba.shape == (256, 256, 4) and est["reduccion"] < 0.75, est
    assert rgba[0, 0, 0] >= 250, "el blanco debe seguir blanco con la rampa a plena luz"
    assert abs(float(r[3].min())) < 1 and float(r[3].max()) <= 1.0
    r2 = atlas03(_FakeDM(), [0, 1], np.arange(6), [(0, 1, 2), (3, 4, 5)], q, q, lit=0.5)
    assert r2[4][0, 0, 0] < 240, "a media luz el blanco oscurece (control)"


if __name__ == "__main__":
    a = sys.argv[1:]
    if "--catalogo" in a:
        sal = a[a.index("--salida") + 1] if "--salida" in a else r"E:\DBZ3HD_trabajo\pelo04"
        tb = catalogo(sal, int(a[a.index("--max") + 1]) if "--max" in a else 0)
        print("catalogo: %d OK de %d" % (sum(1 for x in tb if x[3] == "OK"), len(tb)))
    elif "--selftest" in a:
        selftest(a[a.index("--salida") + 1] if "--salida" in a else SALIDA)
    else:
        print(__doc__)
