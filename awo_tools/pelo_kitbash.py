#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pelo_kitbash.py - PELO-02 (metodo v2): peinados por KITBASH de mechones NATIVOS de B3 -> injertar().

1. biblioteca(): el pelo de cada donante nativo (`vista_heroe.PELOS_NATIVOS`) se lleva al marco HEAD de Goku (mismo
   encaje que `_pelo_donante`: escala por ancho de craneo y ancla en los ojos) y se parte en mechones. El pelo de B3
   es una sola masa soldada: se segmenta por PUNTAS (watershed con persistencia sobre la distancia normalizada al
   craneo, sobre el grafo de la malla) y lo que queda por debajo de DC es el CASCO (pegado al craneo). Los huecos que
   deja cada mechon en el casco se tapan con un abanico. Cada mechon conserva posiciones, normales y UV del donante y
   se clasifica por la zona de su punta (flequillo, copete, lat_izq, lat_der, nuca).
2. armar(receta): casco de un donante + mechones (donante, zona[, indice], giro, largo) -> una malla.
3. ffd(): jaula trilineal 3x3x3 (Bernstein grado 2): variantes "alto" y "caido".
4. normales_capsula(): normales de un elipsoide proxy de la cabeza con sesgo hacia arriba (Arc System Works).
5. injertar_kitbash(): a_anfitrion + injertar() (tope 3261 ventanas). Color: texel del pelo del anfitrion (rgb2 = puntas).

Uso:  python pelo_kitbash.py --selftest [--salida E:\\DBZ3HD_trabajo\\pelo02]
Nota: las UV del donante se guardan en la biblioteca, pero el injerto de combate usa el texel/2 tonos del anfitrion
(el atlas por donante de `_atlas_pelo` solo admite UN donante; mezclar donantes pide un atlas compuesto: pendiente).
"""
import collections
import os
import sys
import tomllib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RECETAS = os.path.join(HERE, "recetas_pelo")
for _p in (HERE, os.path.join(HERE, "..", "mod center hd", "cac"), os.path.join(HERE, "..", "mod center hd")):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.append(_p)

C0 = np.array([0.0, 0.5, 0.35])       # craneo de Goku en su marco HEAD (el de pelo_receta v1)
R0 = np.array([1.15, 1.02, 1.05])
DC = 1.35                              # d normalizada: por debajo = casco; por encima = mechones
PERS = 0.12                            # persistencia minima de una punta (en d)
MIN_TRIS = 6


def _d(P):
    return np.linalg.norm((np.asarray(P) - C0) / R0, axis=-1)


# ---------------------------------------------------------------- biblioteca
def _soldar(P, tol=3):
    key = {}
    return np.array([key.setdefault(tuple(np.round(p, tol)), len(key)) for p in P])


def _segmentar(Pl, tris):
    """Pl: posiciones (marco Goku), tris: (n,3) de indices. Devuelve etiqueta por triangulo (-1 = casco, 0.. = mechon)."""
    idx = _soldar(Pl)
    nv = idx.max() + 1
    pos = np.zeros((nv, 3)); cnt = np.zeros(nv)
    np.add.at(pos, idx, Pl); np.add.at(cnt, idx, 1)
    pos /= cnt[:, None]
    d = _d(pos)
    adj = collections.defaultdict(set)
    for a, b, c in idx[tris]:
        adj[a] |= {b, c}; adj[b] |= {a, c}; adj[c] |= {a, b}
    padre = {}

    def f(x):
        while padre[x] != x:
            padre[x] = padre[padre[x]]
            x = padre[x]
        return x
    pico = {}
    for v in sorted((v for v in adj if d[v] >= DC), key=lambda v: -d[v]):
        rs = {f(u) for u in adj[v] if u in padre}
        if not rs:
            padre[v] = v; pico[v] = d[v]
            continue
        rs = sorted(rs, key=lambda r: -pico[r])
        padre[v] = rs[0]
        for r in rs[1:]:
            if pico[r] - d[v] < PERS:
                padre[r] = rs[0]
    lab = np.full(len(tris), -1)
    raices = {}
    for i, t in enumerate(idx[tris]):
        cs = collections.Counter(f(v) for v in t if v in padre)
        if cs:
            r, n = cs.most_common(1)[0]
            if n >= 2:
                lab[i] = raices.setdefault(r, len(raices))
    for k in range(len(raices)):                      # mechones diminutos -> casco
        if np.count_nonzero(lab == k) < MIN_TRIS:
            lab[lab == k] = -1
    # reetiqueta correlativo
    ids = sorted(set(lab[lab >= 0]))
    nl = np.full_like(lab, -1)
    for n, k in enumerate(ids):
        nl[lab == k] = n
    return nl, idx


def _zona(raiz):
    v = raiz - C0
    az = np.degrees(np.arctan2(v[0], v[2]))
    el = np.degrees(np.arcsin(np.clip(v[1] / max(np.linalg.norm(v), 1e-9), -1, 1)))
    if el >= 55:
        return "copete"
    if abs(az) < 55:
        return "flequillo"
    if abs(az) > 125:
        return "nuca"
    return "lat_izq" if az > 0 else "lat_der"


def _tapas(P, N, UV, tris, lab, idx):
    """Abanicos que cierran los huecos casco<->mechon (aristas compartidas por un triangulo de casco y uno de mechon)."""
    ar = collections.defaultdict(list)
    for i, t in enumerate(idx[tris]):
        for u, v in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            ar[(min(u, v), max(u, v))].append(i)
    corte = [e for e, ts in ar.items() if len(ts) == 2 and (lab[ts[0]] < 0) != (lab[ts[1]] < 0)]
    nx = collections.defaultdict(list)
    for u, v in corte:
        nx[u].append(v); nx[v].append(u)
    visto, bucles = set(), []
    for s in nx:
        if s in visto:
            continue
        comp, pila = [], [s]
        while pila:                                  # componente conexa de la frontera
            x = pila.pop()
            if x in visto:
                continue
            visto.add(x); comp.append(x)
            pila += nx[x]
        bucles.append(comp)
    pv = {}
    for i, v in enumerate(idx):
        pv.setdefault(v, i)
    Pc, Nc, Uc, Tc = [], [], [], []
    for comp in bucles:
        if len(comp) < 3:
            continue
        pts = np.array([P[pv[v]] for v in comp])
        ctr = pts.mean(0)
        r = ctr - C0
        ctr = C0 + r / max(np.linalg.norm(r / R0), 1e-9) * float(DC)   # centro sobre el isocontorno del casco
        n0 = len(Pc)
        Pc.append(ctr); Nc.append((ctr - C0) / R0 ** 2); Uc.append(UV[pv[comp[0]]])
        ang = np.arctan2((pts - ctr) @ np.cross(r, [0, 1.0, 0.001]), (pts - ctr) @ np.cross(np.cross(r, [0, 1.0, 0.001]), r))
        orden = np.argsort(ang)
        for v in (comp[o] for o in orden):
            Pc.append(P[pv[v]]); Nc.append(N[pv[v]]); Uc.append(UV[pv[v]])
        k = len(comp)
        for i in range(k):
            Tc.append((n0, n0 + 1 + i, n0 + 1 + (i + 1) % k))
    return Pc, Nc, Uc, Tc


def _orientar(P, T):
    """Triangulos CCW visto desde fuera del craneo."""
    P = np.asarray(P)
    out = []
    for a, b, c in T:
        g = np.cross(P[b] - P[a], P[c] - P[a])
        out.append((a, b, c) if g @ ((P[a] + P[b] + P[c]) / 3 - C0) >= 0 else (a, c, b))
    return out


def extraer_donante(vh, afs, fid, goku=None):
    """Pelo del cuerpo `fid` en el marco HEAD de Goku, partido. Devuelve dict(casco, mechones, fid)."""
    mr = vh.mr
    if goku is None:
        dm = mr.Model(bytes(afs.entry(264)))
        Pd, _ = dm.posed(vh.pose_de(dm))
        goku = (dm, Pd, vh.zonas_ficha(dm, Pd))
    dm, Pd, zd = goku
    m = mr.Model(bytes(afs.entry(fid)))
    pose = vh.pose_de(m)
    P, N = m.posed(pose)
    z = vh.zonas_ficha(m, P, pelo_color=True)
    ht = [i for i, x in enumerate(z) if x == "pelo"]
    hv = np.unique(m.tris[ht].reshape(-1))
    inv = np.linalg.inv(np.asarray(m.worlds(pose))[m.bone("HEAD")])
    ojos = vh._encaje_ojos(dm, Pd, m, P)
    ancho_d = vh._ancho_cabeza(m, P, vh.zonas_ficha(m, P))
    ancho_g = vh._ancho_cabeza(dm, Pd, zd)
    s = ancho_d / ancho_g if ancho_d and ancho_g else 1.0
    if not 0.5 < s < 1.8:
        s = 1.0
    loc = P[hv] @ inv[:3, :3].T + inv[:3, 3]
    nl = N[hv] @ inv[:3, :3].T
    if ojos:
        _, og, od = ojos
        loc = (loc - od) / s + og
    else:
        loc = loc / s
    nl /= np.maximum(np.linalg.norm(nl, axis=1), 1e-9)[:, None]
    rm = {int(v): i for i, v in enumerate(hv)}
    tris = np.array([[rm[int(v)] for v in m.tris[t]] for t in ht])
    UV = np.asarray(m.uv, float)[hv]
    lab, idx = _segmentar(loc, tris)
    cP, cN, cU, cT = _tapas(loc, nl, UV, tris, lab, idx)

    def pieza(sel):
        us = np.unique(tris[sel].reshape(-1))
        r = {int(v): i for i, v in enumerate(us)}
        return {"P": loc[us], "N": nl[us], "UV": UV[us], "tris": [tuple(r[int(v)] for v in t) for t in tris[sel]]}
    casco = pieza(lab < 0)
    n0 = len(casco["P"])
    casco["P"] = np.vstack([casco["P"], cP]) if cP else casco["P"]
    casco["N"] = np.vstack([casco["N"], cN]) if cP else casco["N"]
    casco["UV"] = np.vstack([casco["UV"], cU]) if cP else casco["UV"]
    casco["tris"] += _orientar(casco["P"], [(a + n0, b + n0, c + n0) for a, b, c in cT])
    mech = []
    for k in range(lab.max() + 1):
        mp = pieza(lab == k)
        dd = _d(mp["P"])
        pun = mp["P"][dd.argmax()]
        raiz = mp["P"][dd <= np.percentile(dd, 20)].mean(0)
        mp.update(tip=pun, raiz=raiz, zona=_zona(pun), fid=fid)
        mech.append(mp)
    por_zona = collections.defaultdict(list)
    for mp in sorted(mech, key=lambda q: -len(q["tris"])):
        mp["indice"] = len(por_zona[mp["zona"]])
        por_zona[mp["zona"]].append(mp)
    return {"fid": fid, "casco": casco, "mechones": mech, "escala": s}


_LIB = {}


def biblioteca(vh, afs, fids=None):
    """{fid: donante partido} para los donantes de PELOS_NATIVOS (o `fids`)."""
    mr = vh.mr
    dm = mr.Model(bytes(afs.entry(264)))
    Pd, _ = dm.posed(vh.pose_de(dm))
    goku = (dm, Pd, vh.zonas_ficha(dm, Pd))
    if fids is None:
        fids = sorted({(vh.KFID.get(c) or vh.KFID[0])[0] for c in vh.PELOS_NATIVOS.values()})
    for fid in fids:
        if fid not in _LIB:
            _LIB[fid] = extraer_donante(vh, afs, fid, goku)
    return {f: _LIB[f] for f in fids}


# ---------------------------------------------------------------- receta -> malla
def _rotar_y(v, grados):
    a = np.radians(grados)
    R = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
    return v @ R.T


def _mover(mp, giro=0.0, largo=1.0):
    P, N = mp["P"].copy(), mp["N"].copy()
    if largo != 1.0:                                    # estira a lo largo de raiz->punta
        a = mp["tip"] - mp["raiz"]
        a = a / max(np.linalg.norm(a), 1e-9)
        P = P + (largo - 1.0) * np.outer((P - mp["raiz"]) @ a, a)
    if giro:
        P = _rotar_y(P - C0, giro) + C0
        N = _rotar_y(N, giro)
    return P, N


def cargar_receta(path):
    if not os.path.isabs(path) and not os.path.exists(path):
        path = os.path.join(RECETAS, path)
    with open(path, "rb") as fh:
        return tomllib.load(fh)


def armar(rec, lib):
    """Receta + biblioteca -> dict(P, N, UV, tris, partes=[(nombre, ini_tri, fin_tri)])."""
    Ps, Ns, Us, Ts, partes = [], [], [], [], []

    def add(nombre, P, N, UV, T):
        n = sum(len(p) for p in Ps)
        Ps.append(P); Ns.append(N); Us.append(UV)
        partes.append((nombre, len(Ts), len(Ts) + len(T)))
        Ts.extend((a + n, b + n, c + n) for a, b, c in T)
    c = lib[int(rec["casco"]["donante"])]["casco"]
    add("casco %d" % rec["casco"]["donante"], c["P"], c["N"], c["UV"], c["tris"])
    for m in rec.get("mechon", []):
        don = lib[int(m["donante"])]["mechones"]
        sel = [q for q in don if q["zona"] == m["zona"] and (("indice" not in m) or q["indice"] == int(m["indice"]))]
        for q in sel:
            P, N = _mover(q, float(m.get("giro", 0.0)), float(m.get("largo", 1.0)))
            add("%d/%s/%d" % (q["fid"], q["zona"], q["indice"]), P, N, q["UV"], q["tris"])
    return {"P": np.vstack(Ps), "N": np.vstack(Ns), "UV": np.vstack(Us), "tris": Ts, "partes": partes}


# ---------------------------------------------------------------- FFD 3x3x3 (Bernstein grado 2)
def _bern(t):
    return np.stack([(1 - t) ** 2, 2 * t * (1 - t), t ** 2], -1)


VARIANTES = {   # desplazamientos de la capa/fila de control (i=x, j=y, k=z) en fracciones del tamano de la jaula
    "base": {},
    "alto": {"y": [(2, 0.30), (1, 0.08)], "sx": [(2, 0.85)]},          # sube la capa superior y la estrecha
    "caido": {"y": [(2, -0.12)], "z": [(2, -0.18)], "sx": [(2, 1.2)]},  # la capa superior cae hacia atras y se abre
}


def ffd(P, variante="base"):
    """Deforma P con una jaula 3x3x3 sobre su caja. El casco (d bajo) apenas se mueve: peso por d."""
    P = np.asarray(P, float)
    v = VARIANTES[variante]
    if not v:
        return P.copy()
    lo, hi = P.min(0), P.max(0)
    ext = np.maximum(hi - lo, 1e-9)
    u = (P - lo) / ext
    B = [_bern(u[:, k]) for k in range(3)]
    ctrl = np.zeros((3, 3, 3, 3))                       # desplazamiento de cada punto de control
    for j, f in v.get("y", []):
        ctrl[:, j, :, 1] += f * ext[1]
    for k, f in v.get("z", []):
        ctrl[:, :, 0, 2] += 0                           # (z se mueve por la capa superior j)
    for j, f in v.get("z", []):
        ctrl[:, j, :, 2] += f * ext[2]
    for j, f in v.get("sx", []):                        # escala en x de la capa j (puntos x=0 y x=2 se separan/juntan)
        cx = (hi[0] - lo[0])
        ctrl[0, j, :, 0] += (f - 1.0) * (-0.5 * cx)
        ctrl[2, j, :, 0] += (f - 1.0) * (0.5 * cx)
    dis = np.einsum("ni,nj,nk,ijkc->nc", B[0], B[1], B[2], ctrl)
    w = np.clip((_d(P) - 1.0) / 0.45, 0, 1)[:, None]    # el casco se queda en el craneo
    return P + dis * w


# ---------------------------------------------------------------- normales de capsula
def normales_capsula(P, sesgo=0.25, mezcla=0.0, N=None):
    """Normal = gradiente de un elipsoide proxy de la cabeza + sesgo hacia arriba (sombras en bloque). mezcla (0..1)
    conserva esa parte de la normal original N."""
    g = (np.asarray(P) - C0) / R0 ** 2
    g /= np.maximum(np.linalg.norm(g, axis=1), 1e-9)[:, None]
    n = g + np.array([0, sesgo, 0.0])
    if N is not None and mezcla:
        n = (1 - mezcla) * n / np.linalg.norm(n, axis=1)[:, None] + mezcla * N
    return n / np.maximum(np.linalg.norm(n, axis=1), 1e-9)[:, None]


# ---------------------------------------------------------------- al anfitrion
TONO2 = (0.30, 0.30, 0.34)   # 2.o tono (puntas, el 40 % de arriba) del pelo negro de B3


def injertar_kitbash(vh, afs, fid, malla, presupuesto=None, rgb1=None, rgb2=TONO2, dm=None):
    """malla (marco Goku) -> injertar(). Devuelve (bytes, resumen, Q, tris)."""
    import injertar_pelo as ip
    import pelo_receta as pr          # a_anfitrion: misma transformacion que el trasplante nativo (solo se importa)
    presupuesto = presupuesto or ip.WINDOW_BUDGET
    amb, model, P, zonas, Q, QN, s = pr.a_anfitrion(vh, afs, fid, {"P": malla["P"], "N": malla["N"]}, dm)
    out, r = ip.injertar(amb, model, [z == "pelo" for z in zonas], Q, QN, malla["tris"], presupuesto=presupuesto,
                         rgb1=rgb1, rgb2=rgb2, cara_mask=[z == "cara" for z in zonas])
    return out, r, Q, malla["tris"]


def construir(rec, lib, variante=None, capsula=True):
    m = armar(rec, lib)
    variante = variante or rec.get("variante", "base")
    P = ffd(m["P"], variante)
    N = normales_capsula(P, float(rec.get("capsula", {}).get("sesgo", 0.25)), float(rec.get("capsula", {}).get("mezcla", 0.0)), m["N"]) if capsula else m["N"]
    return dict(m, P=P, N=N)


# ---------------------------------------------------------------- depuracion y hoja
def _hoja_biblioteca(lib, path, size=300):
    """Un renglon por donante: casco gris + mechones de colores por indice (frente y perfil), para juzgar la segmentacion."""
    from PIL import Image, ImageDraw
    cols = [(230, 80, 80), (80, 200, 90), (80, 130, 240), (240, 200, 60), (200, 90, 220), (70, 210, 210), (240, 140, 60), (160, 160, 255)]
    img = Image.new("RGB", (size * 4, size * len(lib)), (50, 54, 62))
    d = ImageDraw.Draw(img)
    for r, (fid, dn) in enumerate(lib.items()):
        piezas = [(dn["casco"], (150, 150, 150))] + [(q, cols[k % len(cols)]) for k, q in enumerate(dn["mechones"])]
        for c, yaw in enumerate((0, 50, 90, 180)):
            a = np.radians(yaw)
            R = np.array([[np.cos(a), 0, -np.sin(a)], [0, 1, 0], [np.sin(a), 0, np.cos(a)]])
            polys = []
            for q, col in piezas:
                X = (q["P"] - C0) @ R.T
                for t in q["tris"]:
                    p = X[list(t)]
                    polys.append((p[:, 2].mean(), p[:, :2], col))
            polys.sort(key=lambda x: x[0])
            for _, p, col in polys:
                xy = [(c * size + size / 2 + x * size * 0.17, r * size + size * 0.62 - y * size * 0.17) for x, y in p]
                d.polygon(xy, fill=col, outline=(20, 20, 20))
        d.text((4, r * size + 4), "donante %d: %d mechones %s" % (fid, len(dn["mechones"]), dict(collections.Counter(q["zona"] for q in dn["mechones"]))), fill=(255, 255, 255))
    img.save(path)


def _hoja(vh, filas, path, size=320):
    import pelo_receta as pr
    pr.hoja(vh, filas, None, size).convert("RGB").save(path)


def _cerrado_en_su_lugar(m):
    return np.isfinite(m["P"]).all() and np.isfinite(m["N"]).all() and len(m["tris"]) > 0


# ---------------------------------------------------------------- selftest
RECETAS_BASE = ("goku_vegeta.toml", "gohan_largo.toml")
HOSTS = [("Goku 264", 264), ("Vegeta 416", 416), ("Gohan 235", 235)]


def selftest(salida):
    import injertar_pelo as ip
    import vista_heroe as vh
    os.makedirs(salida, exist_ok=True)
    work = os.path.join(salida, "w")
    os.makedirs(work, exist_ok=True)
    afs = vh.abrir_afs(None, work)
    lib = biblioteca(vh, afs)
    _hoja_biblioteca(lib, os.path.join(salida, "biblioteca_pelo02.png"))
    resumen = ["mechones por donante: " + ", ".join("%d=%d" % (f, len(d["mechones"])) for f, d in lib.items())]
    for fid, dn in lib.items():
        assert len(dn["casco"]["tris"]) > 20, "casco vacio en %d" % fid
        assert np.isfinite(dn["casco"]["P"]).all()
        for q in dn["mechones"]:
            assert len(q["tris"]) >= MIN_TRIS and q["UV"].shape == (len(q["P"]), 2), "mechon sin UV/tris en %d" % fid
            assert q["zona"] in ("flequillo", "copete", "lat_izq", "lat_der", "nuca")
    # FFD: identidad en "base"; "alto" sube la punta mas alta; el casco casi no se mueve
    P0 = lib[264]["casco"]["P"]
    assert np.allclose(ffd(P0, "base"), P0)
    Pm = np.vstack([q["P"] for q in lib[264]["mechones"]])
    assert ffd(Pm, "alto")[:, 1].max() > Pm[:, 1].max() + 0.1, "FFD alto no sube"
    assert ffd(Pm, "caido")[:, 1].max() < Pm[:, 1].max(), "FFD caido no baja"
    # normales de capsula: unitarias y con componente hacia arriba respecto al gradiente puro
    n = normales_capsula(Pm, 0.25)
    assert abs(np.linalg.norm(n, axis=1) - 1).max() < 1e-6 and (n[:, 1] >= normales_capsula(Pm, 0.0)[:, 1] - 1e-9).all()
    dm = vh.mr.Model(bytes(afs.entry(264)))
    refs = []
    for tit, f in (("Goku NATIVO (referencia)", 264), ("Vegeta NATIVO (referencia)", 416)):
        b = bytes(afs.entry(f))
        m0 = vh.mr.Model(b)
        P0_, _ = m0.posed(vh.pose_de(m0))
        z0 = vh.zonas_ficha(m0, P0_)
        if f == 416 and sum(z == "pelo" for z in z0) < 50:
            z0 = vh.zonas_ficha(m0, P0_, pelo_color=True)
        hv = np.unique(m0.tris[[i for i, z in enumerate(z0) if z == "pelo"]].reshape(-1))
        refs.append((tit, b, P0_[hv]))
    filas = list(refs)
    for rn in RECETAS_BASE:
        rec = cargar_receta(rn)
        m = construir(rec, lib)
        assert _cerrado_en_su_lugar(m)
        for tit, fid in HOSTS:
            out, r, Q, tris = injertar_kitbash(vh, afs, fid, m, dm=dm)
            v = r.get("ventanas")
            assert v, "%s/%s: sin injerto (%s)" % (rn, tit, r)
            assert v["total"] <= ip.WINDOW_BUDGET and "decimado" not in r, "%s/%s: %s" % (rn, tit, v)
            tag = "%s_%s" % (os.path.splitext(rn)[0], tit.split()[0].lower())
            open(os.path.join(salida, tag + ".bin"), "wb").write(out)
            filas.append(("%s + %s (%d tris)" % (tit, rec["nombre"], len(tris)), out, Q))
            resumen.append("%s en %s: ventanas %d/%d (pelo %d)" % (rec["nombre"], tit, v["total"], ip.WINDOW_BUDGET, v["pelo"]))
        for var in ("alto", "caido"):           # variantes FFD solo sobre Goku
            mv = construir(rec, lib, var)
            out, r, Q, tris = injertar_kitbash(vh, afs, 264, mv, dm=dm)
            v = r["ventanas"]
            assert v["total"] <= ip.WINDOW_BUDGET
            filas.append(("Goku 264 + %s FFD %s" % (rec["nombre"], var), out, Q))
            resumen.append("%s FFD %s en Goku: ventanas %d" % (rec["nombre"], var, v["total"]))
    ruta = os.path.join(salida, "hoja_pelo02.png")
    _hoja(vh, filas, ruta)
    print("\n".join(resumen))
    print("OK selftest PELO-02, hoja en %s" % ruta)


if __name__ == "__main__":
    a = sys.argv[1:]
    if "--selftest" in a:
        selftest(a[a.index("--salida") + 1] if "--salida" in a else r"E:\DBZ3HD_trabajo\pelo02")
    else:
        print(__doc__)
