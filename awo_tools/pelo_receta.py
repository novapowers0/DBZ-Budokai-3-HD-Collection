#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pelo_receta.py - PELO-01: peinado ORIGINAL generado por receta (mechones en punta al estilo B3) -> injertar().

Receta (toml, ver recetas_pelo/goku_corto.toml): craneo (elipsoide en el marco HEAD de Goku), casquete (masa que
cubre el craneo) y una lista de mechones: raiz por angulos (az/el), direccion, largo, ancho, curva. Cada mechon es un
pincho de seccion triangular, cerrado (3 lados + tapa), con normales planas por lado (aspecto duro del toon de B3).
La malla se genera en el marco HEAD de Goku y se lleva al anfitrion igual que el trasplante de pelo nativo
(`vista_heroe._pelo_donante`: escala por ancho de craneo y ancla en los ojos), luego entra por
`injertar_pelo.injertar()` con el color/UV del pelo nativo del anfitrion. Si no cabe en el tope de ventanas se bajan
los segmentos (y, en ultimo extremo, se quitan mechones) en vez de dejar que se decime.

Uso:  python pelo_receta.py --selftest [--salida E:\\DBZ3HD_trabajo\\pelo01]
"""
import os
import sys
import tomllib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RECETAS = os.path.join(HERE, "recetas_pelo")
CAC = os.path.join(HERE, "..", "mod center hd", "cac")
for _p in (HERE, CAC, os.path.join(HERE, "..", "mod center hd")):
    if os.path.isdir(_p) and _p not in sys.path:
        sys.path.append(_p)


# ---------------------------------------------------------------- generacion (marco HEAD de Goku)
def _dir(az, el):
    az, el = np.radians(az), np.radians(el)
    return np.array([np.cos(el) * np.sin(az), np.sin(el), np.cos(el) * np.cos(az)])


def _unit(v):
    return v / max(np.linalg.norm(v), 1e-9)


def _mechon(m, cr, seg):
    """Pincho triangular cerrado. Devuelve (P, N, tris) con normales planas por lado y tapa trasera."""
    c, r = np.array(cr["centro"], float), np.array(cr["radio"], float)
    raiz = c + r * _dir(m["az"], m["el"]) * 0.97               # hundida un 3 % en el craneo
    d0 = _dir(m["dir_az"], m["dir_el"])
    hacia = np.array(m.get("hacia", [0, -1, 0]), float)
    L, W, curva = float(m["largo"]), float(m["ancho"]), float(m.get("curva", 0.0))
    seg = max(1, int(seg))
    # eje: la direccion gira hacia `hacia` con t^2 (el pincho se curva hacia la punta)
    ts = np.linspace(0.0, 1.0, seg + 1)
    ds = [_unit(d0 + curva * t * t * _unit(hacia) * 2.0) for t in ts]
    ejes = [raiz]
    for i in range(seg):
        ejes.append(ejes[-1] + L / seg * _unit(ds[i] + ds[i + 1]))
    anillos = []
    for i in range(seg):                                        # el ultimo punto es la punta (sin anillo)
        ref = np.array([0, 1.0, 0]) if abs(ds[i][1]) < 0.9 else np.array([0, 0, 1.0])
        u = _unit(np.cross(ds[i], ref))
        v = np.cross(ds[i], u)
        w = W * 0.5 * (1.0 - ts[i]) ** 0.85
        anillos.append([ejes[i] + w * (np.cos(a) * u + np.sin(a) * v) for a in (np.pi / 2, np.pi / 2 + 2.094395, np.pi / 2 + 4.18879)])
    punta = ejes[-1]
    P, N, T = [], [], []

    def tri(a, b, c, cen):                                      # CCW visto desde fuera; normal plana
        n = np.cross(P[b] - P[a], P[c] - P[a])
        if np.dot(n, (P[a] + P[b] + P[c]) / 3 - cen) < 0:
            b, c = c, b
            n = -n
        nn = _unit(n)
        for k in (a, b, c):
            N[k] = nn
        T.append((a, b, c))

    for k in range(3):                                          # un lado por arista del triangulo
        k2 = (k + 1) % 3
        ids = []
        for i in range(seg):
            ids.append((len(P), len(P) + 1))
            P.extend([anillos[i][k], anillos[i][k2]])
            N.extend([np.zeros(3), np.zeros(3)])
        for i in range(seg - 1):
            cen = (ejes[i] + ejes[i + 1]) / 2
            (a, b), (c, d) = ids[i], ids[i + 1]
            tri(a, b, d, cen)
            tri(a, d, c, cen)
        ip = len(P)
        P.append(punta)
        N.append(np.zeros(3))
        a, b = ids[-1]
        tri(a, b, ip, ejes[-2])
    # tapa trasera (dentro del craneo): triangulo plano
    i0 = len(P)
    P.extend(anillos[0])
    N.extend([np.zeros(3)] * 3)
    tri(i0, i0 + 1, i0 + 2, raiz - d0)
    for k in range(i0, i0 + 3):
        N[k] = -d0
    return np.array(P), np.array(N), T


def _casquete(cr, ca):
    """Cascara de pelo sobre el craneo, abierta por abajo; normales suaves de elipsoide."""
    c, r = np.array(cr["centro"], float), np.array(cr["radio"], float) * float(ca["escala"])
    nc, na = int(ca["cortes"]), int(ca["anillos"])
    P, T = [], []
    for j in range(na + 1):
        for i in range(nc):
            az = 360.0 * i / nc
            f = (1 + np.cos(np.radians(az))) / 2                # 1 al frente, 0 atras
            base = ca["lados_el"] + (ca["frente_el"] - ca["lados_el"]) * max(0.0, np.cos(np.radians(az))) ** 2
            if np.cos(np.radians(az)) < 0:
                base = ca["lados_el"] + (ca["atras_el"] - ca["lados_el"]) * (-np.cos(np.radians(az)))
            el = base + (88.0 - base) * j / na
            P.append(c + r * _dir(az, el))
    P.append(c + r * np.array([0, 1.0, 0]))                    # coronilla
    top = len(P) - 1
    for j in range(na):
        for i in range(nc):
            a, b = j * nc + i, j * nc + (i + 1) % nc
            e, f_ = (j + 1) * nc + i, (j + 1) * nc + (i + 1) % nc
            if j == na - 1:
                T.append((a, b, top))
            else:
                T.append((a, b, f_))
                T.append((a, f_, e))
    P = np.array(P)
    N = (P - c) / r ** 2
    N = N / np.linalg.norm(N, axis=1)[:, None]
    # orientacion: CCW visto desde fuera
    T2 = []
    for a, b, d in T:
        g = np.cross(P[b] - P[a], P[d] - P[a])
        T2.append((a, b, d) if np.dot(g, P[a] - c) > 0 else (a, d, b))
    return P, N, T2


def cargar_receta(path):
    if not os.path.isabs(path) and not os.path.exists(path):
        path = os.path.join(RECETAS, path)
    with open(path, "rb") as fh:
        return tomllib.load(fh)


def generar(rec, seg=None, max_mechones=None):
    """Receta -> dict(P, N, tris, mechones=[(ini, fin)], casquete=(ini, fin)) en el marco HEAD de Goku."""
    cr = rec["craneo"]
    seg = seg or rec.get("segmentos", 3)
    Ps, Ns, Ts, rangos = [], [], [], []
    n = 0
    for m in rec["mechon"][:max_mechones]:
        P, N, T = _mechon(m, cr, seg)
        Ps.append(P); Ns.append(N); Ts.extend((a + n, b + n, c + n) for a, b, c in T)
        rangos.append((n, n + len(P), len(Ts) - len(T), len(Ts)))
        n += len(P)
    cas = None
    if "casquete" in rec:
        P, N, T = _casquete(cr, rec["casquete"])
        Ps.append(P); Ns.append(N); Ts.extend((a + n, b + n, c + n) for a, b, c in T)
        cas = (n, n + len(P), len(Ts) - len(T), len(Ts))
        n += len(P)
    return {"P": np.vstack(Ps), "N": np.vstack(Ns), "tris": Ts, "mechones": rangos, "casquete": cas}


# ---------------------------------------------------------------- al anfitrion
def a_anfitrion(vh, afs, fid, malla, dm=None):
    """Lleva la malla (marco HEAD de Goku) al espacio modelo del cuerpo `fid`: misma transformacion que
    `_pelo_donante` (escala por ancho de craneo + ancla en el punto medio de los ojos)."""
    mr = vh.mr
    amb = bytes(afs.entry(fid))
    model = mr.Model(amb)
    pose = vh.pose_de(model)
    P, _ = model.posed(pose)
    zonas = vh.zonas_ficha(model, P)
    if sum(z == "pelo" for z in zonas) < 50:      # pelo claro (Trunks): la zona sale por el color, no por la textura
        zonas = vh.zonas_ficha(model, P, pelo_color=True)
    dm = dm or mr.Model(bytes(afs.entry(264)))
    Pd, _ = dm.posed(vh.pose_de(dm))
    zd = vh.zonas_ficha(dm, Pd)
    hw = np.asarray(model.worlds(pose))[model.bone("HEAD")]
    ojos = vh._encaje_ojos(dm, Pd, model, P)
    ancho_d, ancho_h = vh._ancho_cabeza(dm, Pd, zd), vh._ancho_cabeza(model, P, zonas)
    s = ancho_h / ancho_d if ancho_d and ancho_h else 1.0
    if not 0.5 < s < 1.8:
        s = 1.0
    if ojos:
        _, od, oh = ojos
        local = (malla["P"] - od) * s + oh
    else:
        local = malla["P"] * s
    Q = local @ hw[:3, :3].T + hw[:3, 3]
    QN = malla["N"] @ hw[:3, :3].T
    QN /= np.maximum(np.linalg.norm(QN, axis=1), 1e-9)[:, None]
    return amb, model, P, zonas, Q, QN, s


def injertar_receta(vh, afs, fid, rec, presupuesto=None, rgb1=None, rgb2=None, dm=None):
    """Genera, encaja e injerta. Baja segmentos (3 -> 2 -> 1) y luego mechones hasta que quepa sin decimar.
    Devuelve (bin_bytes, resumen, Q, tris, segmentos_usados, mechones_usados)."""
    import injertar_pelo as ip
    presupuesto = presupuesto or ip.WINDOW_BUDGET
    ult = None
    for seg, nm in [(s, None) for s in range(int(rec.get("segmentos", 3)), 0, -1)] + \
                   [(1, k) for k in range(len(rec["mechon"]) - 1, 5, -1)]:
        malla = generar(rec, seg, nm)
        amb, model, P, zonas, Q, QN, s = a_anfitrion(vh, afs, fid, malla, dm)
        out, r = ip.injertar(amb, model, [z == "pelo" for z in zonas], Q, QN, malla["tris"], presupuesto=presupuesto,
                             rgb1=rgb1, rgb2=rgb2, cara_mask=[z == "cara" for z in zonas])
        ult = (out, r, Q, malla["tris"], seg, nm or len(rec["mechon"]))
        v = r.get("ventanas")
        if v and v["total"] <= presupuesto and "decimado" not in r:
            break
    return ult


# ---------------------------------------------------------------- salida: OBJ y hoja
def escribir_obj(path, Q, tris):
    with open(path, "w") as f:
        for q in Q:
            f.write("v %.5f %.5f %.5f\n" % tuple(q))
        for a, b, c in tris:
            f.write("f %d %d %d\n" % (a + 1, b + 1, c + 1))


def hoja(vh, modelos, Qs, size=320):
    """modelos: [(titulo, bytes bin, Q cabeza)] -> imagen RGBA con 4 vistas (frente, 3/4, perfil, espalda) por fila."""
    from PIL import Image, ImageDraw
    mr = vh.mr
    if not getattr(mr._sample, "_parcheado", False):   # ponytail: mr._sample falla con v = -1e-17 (mod = alto); parche local
        _orig = mr._sample
        mr._sample = lambda img, u, v: _orig(img, np.asarray(u) + 1e-6, np.asarray(v) + 1e-6)
        mr._sample._parcheado = True
    vistas = [("frente", 0), ("3/4", 40), ("perfil", 90), ("espalda", 180)]
    img = Image.new("RGBA", (size * 4, (size + 18) * len(modelos)), (60, 64, 72, 255))
    d = ImageDraw.Draw(img)
    for fila, (tit, data, Q) in enumerate(modelos):
        m = mr.Model(bytes(data))
        pose = vh.pose_de(m)
        ctr = (Q.min(0) + Q.max(0)) / 2
        esc = size * 0.82 / max(float(np.ptp(Q, axis=0).max()), 1e-6)
        d.text((6, fila * (size + 18) + 3), tit, fill=(255, 255, 255, 255))
        for k, (nom, yaw) in enumerate(vistas):
            view = mr.rot_x(6) @ mr.rot_y(yaw)
            v = mr.render(m, (size, size), view, ctr, esc, pose=pose, outline=1.6, ss=2)
            img.alpha_composite(Image.fromarray(v), (k * size, fila * (size + 18) + 18))
            d.text((k * size + 6, fila * (size + 18) + 18 + size - 14), nom, fill=(220, 220, 220, 255))
    return img


# ---------------------------------------------------------------- selftest
def _cerrada(P, T):
    """Cada arista (con vertices soldados por posicion) la comparten exactamente 2 triangulos."""
    key = {}
    idx = [key.setdefault(tuple(np.round(p, 5)), len(key)) for p in P]
    ar = {}
    for a, b, c in T:
        for u, v in ((a, b), (b, c), (c, a)):
            e = tuple(sorted((idx[u], idx[v])))
            ar[e] = ar.get(e, 0) + 1
    return all(n == 2 for n in ar.values())


FIDS = [("Goku 264", 264), ("Vegeta 416", 416), ("Trunks 390", 390), ("Gohan 235", 235)]


def selftest(salida):
    import vista_heroe as vh
    import injertar_pelo as ip
    os.makedirs(salida, exist_ok=True)
    work = os.path.join(salida, "w")
    os.makedirs(work, exist_ok=True)
    rec = cargar_receta("goku_corto.toml")
    base = generar(rec)
    assert np.isfinite(base["P"]).all() and np.isfinite(base["N"]).all(), "NaN en la malla"
    for (a, b, t0, t1), m in zip(base["mechones"], rec["mechon"]):
        assert _cerrada(base["P"][a:b], [(x - a, y - a, z - a) for x, y, z in base["tris"][t0:t1]]), "mechon abierto: " + m["nombre"]
        assert np.abs(np.linalg.norm(base["N"][a:b], axis=1) - 1).max() < 1e-6, "normal no unitaria: " + m["nombre"]
    afs = vh.abrir_afs(None, work)
    dm = vh.mr.Model(bytes(afs.entry(264)))
    modelos, resumen = [], []
    # referencia: Goku nativo
    amb0 = bytes(afs.entry(264))
    m0 = vh.mr.Model(amb0)
    P0, _ = m0.posed(vh.pose_de(m0))
    z0 = vh.zonas_ficha(m0, P0)
    hv = np.unique(m0.tris[[i for i, z in enumerate(z0) if z == "pelo"]].reshape(-1))
    modelos.append(("Goku NATIVO (referencia)", amb0, P0[hv]))
    for tit, fid in FIDS:
        out, r, Q, tris, seg, nm = injertar_receta(vh, afs, fid, rec, dm=dm)
        v = r.get("ventanas")
        assert v, "%s: sin injerto (%s)" % (tit, r)
        assert v["total"] <= ip.WINDOW_BUDGET, "%s: %d ventanas > tope" % (tit, v["total"])
        assert np.isfinite(Q).all()
        tag = tit.split()[0].lower() + str(fid)
        open(os.path.join(salida, "%s_receta.bin" % tag), "wb").write(out)
        escribir_obj(os.path.join(salida, "%s_pelo.obj" % tag), Q, tris)
        modelos.append(("%s + receta goku_corto (seg %d, %d mechones)" % (tit, seg, nm), out, Q))
        resumen.append("%s: ventanas %d/%d (pelo %d, reutilizadas %d, nuevas %d) seg=%d mechones=%d" % (
            tit, v["total"], ip.WINDOW_BUDGET, v["pelo"], v["reutilizadas"], v["nuevas"], seg, nm))
    hoja(vh, modelos, None).save(os.path.join(salida, "hoja_pelo01.png"))
    print("\n".join(resumen))
    print("OK selftest: %d mechones cerrados, %d vertices de receta, hoja en %s" % (
        len(rec["mechon"]), len(base["P"]), os.path.join(salida, "hoja_pelo01.png")))


if __name__ == "__main__":
    a = sys.argv[1:]
    if "--selftest" in a:
        selftest(a[a.index("--salida") + 1] if "--salida" in a else r"E:\DBZ3HD_trabajo\pelo01")
    else:
        print(__doc__)
