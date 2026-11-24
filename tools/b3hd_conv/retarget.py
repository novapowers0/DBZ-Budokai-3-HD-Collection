# -*- coding: utf-8 -*-
r"""retarget.py (kit b3hd_conv, 12/11) - animacion de PS2 (B1/B2/B3/IW: #AMO + #AMM) -> esqueleto de B3 HD (#AWO), por
NOMBRE de hueso, con pose base "cocinada" de un clip del destino. Sale un #ACM HD nativo (todas las pistas del destino,
un cuadro por cuadro) y un informe del mapa de huesos.

Fuentes (`juego:entrada[/hijo]` o una ruta de fichero):  b1 b2 sb sbar (ISOs de ps2_games), gh (B3 PS2), iw, hd (us/)
  python tools\b3hd_conv\b3hd_conv.py anim --esq-origen b2:327/0 --anim-origen b2:328/3 --clip 2 ^
         --esq-destino hd:534 --base hd:534/2 --base-clip 0 --base-cuadro 114 --relativa ^
         --mapa tools\b3hd_conv\mapas\shenron_b2.json --salida E:\DBZ3HD_trabajo\b3hd_conv\shen_b2_clip2.acm
Mapa automatico, por orden: (1) mismo sufijo tras el prefijo del personaje (GOK_RARM1 -> RARM1), (2) sinonimos (ALIAS),
(3) cadenas con otro numero de eslabones (LHIGE_1..B -> LHIGE1..3: se reparte por posicion en la cadena). El --mapa JSON
manda sobre lo automatico: {"huesos": {"DESTINO": "ORIGEN" | null}, "fijos": [...], "raiz": "DESTINO", "escala": f}.
Metodo (el de t3_retarget.convert, con delta de mundo): giro_mundo_destino = delta_origen(t) * giro_mundo_base_destino,
con delta_origen = mundo_origen(t) * inverso(mundo_origen_reposo). "fijos" = huesos que guardan su mundo de la pose base
(p. ej. el cuerpo de Shenron, que en B2 no tiene huesos: va cocido en la malla). La raiz mapeada lleva ademas la
traslacion del origen (por la escala). El resto de huesos sin pareja guardan su giro local de la pose base.
  python tools\b3hd_conv\retarget.py --autoprueba
"""
import json
import math
import os
import re
import struct
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
sys.path.insert(0, os.path.join(RAIZ, "awo_tools"))
sys.path.insert(0, os.path.join(RAIZ, "mod center hd"))
import altura  # noqa: E402  (skeleton, euler, qmul, qrot)

ALIAS = {"AGO": "JAW", "SKIN": "BODY", "KUBI": "NECK", "ATAMA": "HEAD"}   # sufijo de origen -> sufijo de destino
ISOS = {"b1": "*Budokai (Europe)*.iso", "b2": "*Budokai 2*.iso", "sb": "*Shin Budokai (USA)*.iso",
        "sbar": "*Another Road*.iso"}

qmul, qrot = altura.qmul, altura.qrot


def qinv(q):
    return (-q[0], -q[1], -q[2], q[3])


def euler16(q):
    """quaternion -> (x, y, z) u16 del #ACM/#AMM (orden Rz Ry Rx, el de altura.euler); copia de t3_retarget.euler16."""
    x, y, z, w = q
    sy = max(-1.0, min(1.0, 2 * (w * y - z * x)))
    ry = math.asin(sy)
    if abs(sy) < 0.999999999:
        rx = math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y))
        rz = math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))
    else:
        rx, rz = 0.0, math.atan2(-2 * (x * y - w * z), 1 - 2 * (x * x + z * z))
    return tuple(round(a * 32768 / math.pi) & 0xFFFF for a in (rx, ry, rz))


# ------------------------------------------------------------------------------------------------ lectura de fuentes
def fuente(spec):
    """bytes de `juego:entrada[/hijo]` o de un fichero. El hijo es el de un #AMB (PS2 o HD)."""
    if os.path.isfile(spec):
        return open(spec, "rb").read()
    import afs_pair as ap  # noqa: PLC0415
    juego, _, resto = spec.partition(":")
    n, _, hijo = resto.partition("/")
    n = int(n)
    if juego == "hd":
        b = bytes(ap.hd(n))
    elif juego == "gh":
        b = bytes(ap.ps2(n, real=True))
    elif juego == "iw":
        b = bytes(ap.iw(n))
    elif juego in ISOS:
        import glob  # noqa: PLC0415
        hits = sorted(glob.glob(os.path.join(RAIZ, "ps2_games", ISOS[juego])))
        if not hits:
            raise SystemExit("falta la ISO de %s en ps2_games" % juego)
        b = bytes(ap.entry(hits[0] + "/data_cmn.afs", n))
    else:
        raise SystemExit("fuente desconocida: %s" % spec)
    if hijo:
        e, kids = altura.amb_kids(b)
        o, s = kids[int(hijo)][:2]
        b = b[o:o + s]
    return b


def esqueleto(b):
    """[(nombre, quat, pos, padre)] de un #AMO PS2 o de un modelo HD (#AMB con el #AWO donde sea)."""
    if b[:4] == b"#AMB" and not altura._is_ps2(b):
        awo = struct.unpack(">I", b[0x20:0x24])[0]
        if awo > 0x40:   # #AMB de varios hijos (Shenron 534): el lector espera el #AWO en 0x40 (vista_b2.py hace igual)
            b = b[awo - 0x40:]
    return altura.skeleton(b)


def leer_anim(b, clip):
    """{nombre: (claves_giro [(f, x, y, z)], claves_pos [(f, x, y, z)])}, n_cuadros de un #AMM (LE) o #ACM (BE)."""
    e = ">" if b[:4] == b"#ACM" else "<"
    n, t, nb, no = struct.unpack(e + "4I", b[0x10:0x20])
    if clip >= n:
        raise SystemExit("clip %d fuera de rango (%d clips)" % (clip, n))
    flags, _, nf, off = struct.unpack_from(e + "4I", b, t + 16 * clip)
    per = 3 if flags & 0x10 else 2
    out = {}
    for j in range(nb):
        nm = b[no + 32 * j:no + 32 * j + 32].split(b"\0")[0].decode("latin1")
        rp, pp = struct.unpack_from(e + "2I", b, off + 4 * per * j)
        rot = [struct.unpack_from(e + "4H", b, rp + 12 + 8 * k) for k in range(struct.unpack_from(e + "I", b, rp + 8)[0])] if rp else []
        pos = []
        if pp:
            for k in range(struct.unpack_from(e + "I", b, pp + 8)[0]):
                f, = struct.unpack_from(e + "I", b, pp + 12 + 16 * k)
                pos.append((f,) + struct.unpack_from(e + "3f", b, pp + 16 + 16 * k))
        out[nm] = (rot, pos)
    return out, nf


def _muestra(ks, fc, giro):
    """valor de las claves en el cuadro fc (lineal; giros u16 por el camino corto), como ps2::Locales."""
    ia = 0
    while ia + 1 < len(ks) and ks[ia + 1][0] <= fc:
        ia += 1
    ib = min(ia + 1, len(ks) - 1)
    fa, fb = ks[ia][0], ks[ib][0]
    u = min(max((fc - fa) / (fb - fa), 0.0), 1.0) if fb > fa else 0.0
    if giro:
        return tuple(ks[ia][k] + u * (((ks[ib][k] - ks[ia][k] + 0x8000) & 0xFFFF) - 0x8000) for k in (1, 2, 3))
    return tuple(ks[ia][k] + u * (ks[ib][k] - ks[ia][k]) for k in (1, 2, 3))


def locales(sk, anim, fc, relativa):
    """[(q, p)] locales de `sk` en el cuadro fc. Giro: absoluto (B3/IW) o reposo*clave (B2, Anim::relativa). Posicion:
    reposo + clave (los dos; sb_amm.py). ponytail: la posicion absoluta de IW (ps2::Locales sin relativa) no esta."""
    out = []
    for nm, q, p, _ in sk:
        rot, pos = anim.get(nm, ([], []))
        if rot:
            k = altura.euler(_muestra(rot, fc, True))
            q = qmul(q, k) if relativa else k
        if pos:
            d = _muestra(pos, fc, False)
            p = tuple(a + b for a, b in zip(p, d))
        out.append((tuple(q), tuple(p)))
    return out


def mundo(sk, loc):
    w = [None] * len(sk)
    for i, (_, _, _, par) in enumerate(sk):   # los padres van antes que los hijos (asi en todos los #AMO/#AWO)
        q, p = loc[i]
        if par < 0 or par == i:
            w[i] = (q, p)
        else:
            pq, pp = w[par]
            w[i] = (qmul(pq, q), tuple(a + b for a, b in zip(pp, qrot(pq, p))))
    return w


# ------------------------------------------------------------------------------------------------ mapa de huesos
def _prefijo(nombres):
    pre = os.path.commonprefix(nombres)
    return pre[:pre.rfind("_") + 1] if "_" in pre else ""


def _eslabon(suf):
    """('HIGE', 'L', 3) de 'LHIGE3' / 'LHIGE_3'; indice en hexadecimal (B2 llega a LHIGE_B). None si no acaba en indice."""
    m = re.match(r"^([A-Z]+?)(?:_([0-9A-F])|([0-9]))$", suf)   # hexadecimal solo tras '_' (si no, HEAD = HEA + D)
    return (m.group(1), int(m.group(2) or m.group(3), 16)) if m else None


def mapa_auto(origen, destino):
    """{indice destino: indice origen} y lista de lineas del informe."""
    so = [n[len(_prefijo([x[0] for x in origen])):] for n, *_ in origen]
    sd = [n[len(_prefijo([x[0] for x in destino])):] for n, *_ in destino]
    idx_o = {s: i for i, s in enumerate(so)}
    m, por = {}, {}
    for j, s in enumerate(sd):
        for c in [s] + [o for o, d in ALIAS.items() if d == s]:
            if c in idx_o:
                m[j], por[j] = idx_o[c], "nombre" if c == s else "sinonimo"
                break
    cad_o, cad_d = {}, {}
    for i, s in enumerate(so):
        if (e := _eslabon(s)):
            cad_o.setdefault(e[0], []).append((e[1], i))
    for j, s in enumerate(sd):
        if j not in m and (e := _eslabon(s)):
            cad_d.setdefault(e[0], []).append((e[1], j))
    for raiz, dl in cad_d.items():
        ol = sorted(cad_o.get(raiz, []))
        if not ol:
            continue
        dl = sorted(dl)
        for k, (_, j) in enumerate(dl):   # el eslabon k de m cae en el de origen en la misma fraccion de la cadena
            m[j] = ol[round(k * (len(ol) - 1) / max(len(dl) - 1, 1))][1]
            por[j] = "cadena %s %d->%d" % (raiz, len(ol), len(dl))
    return m, por


# ------------------------------------------------------------------------------------------------ retarget
def retarget(sk_o, anim_o, nf, sk_d, base_loc, mapa, fijos=(), raiz=None, escala=1.0, relativa=False):
    """-> [cuadro][hueso destino] = (q local, p local). base_loc = locales del destino en la pose base."""
    reposo_o = mundo(sk_o, locales(sk_o, {}, 0, relativa))
    base_w = mundo(sk_d, base_loc)
    fijos = set(fijos)
    cuadros = []
    for f in range(nf):
        w_o = mundo(sk_o, locales(sk_o, anim_o, f, relativa))
        w_d = [None] * len(sk_d)
        loc = []
        for j, (_, _, _, par) in enumerate(sk_d):
            pq, pp = w_d[par] if 0 <= par != j else ((0.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0))
            if j in mapa and mapa[j] is not None:
                i = mapa[j]
                delta = qmul(w_o[i][0], qinv(reposo_o[i][0]))
                q = qmul(delta, base_w[j][0])
                if j == raiz:
                    d = tuple(escala * (a - b) for a, b in zip(w_o[i][1], reposo_o[i][1]))
                    p = tuple(a + b for a, b in zip(base_w[j][1], d))
                else:
                    p = tuple(a + b for a, b in zip(pp, qrot(pq, base_loc[j][1])))
            elif j in fijos:
                q, p = base_w[j]
            else:
                q = qmul(pq, base_loc[j][0])
                p = tuple(a + b for a, b in zip(pp, qrot(pq, base_loc[j][1])))
            w_d[j] = (q, p)
            lq = qmul(qinv(pq), q)
            lp = qrot(qinv(pq), tuple(a - b for a, b in zip(p, pp)))
            loc.append((lq, lp))
        cuadros.append(loc)
    return cuadros, base_w


def escribir_acm(sk_d, cuadros):
    """#ACM HD de un clip: giro (absoluto, como B3) en todos los huesos y posicion (sobre el reposo) donde se mueve."""
    nombres = [n for n, *_ in sk_d]
    pistas = []
    for j, (_, _, p0, _) in enumerate(sk_d):
        rot = [(f,) + euler16(c[j][0]) for f, c in enumerate(cuadros)]
        dp = [(f,) + tuple(a - b for a, b in zip(c[j][1], p0)) for f, c in enumerate(cuadros)]
        mueve = any(max(abs(v) for v in k[1:]) > 1e-3 for k in dp)
        r = struct.pack(">3I", 0, 0, len(rot)) + b"".join(struct.pack(">4H", *k) for k in rot)
        p = struct.pack(">3I", 0, 0, len(dp)) + b"".join(struct.pack(">I3f", *k) for k in dp) if mueve else None
        pistas.append((r, p))
    # mismo trazado que t3_retarget.build_acm_bank (un clip, banderas 9)
    out = bytearray(b"#ACM" + struct.pack(">7I", 0x20, 0, 2, 1, 0x20, len(nombres), 0))
    out += bytes(16)
    out += bytes((-len(out)) % 16)
    ini = len(out)
    struct.pack_into(">4I", out, 0x20, 9, 0, len(cuadros), ini)
    out += bytes(8 * len(nombres))
    for i, tr in enumerate(pistas):
        for k, x in enumerate(tr):
            if x is None:
                continue
            out += bytes((-len(out)) % 4)
            struct.pack_into(">I", out, ini + 8 * i + 4 * k, len(out))
            out += x
    out += bytes((-len(out)) % 16)
    struct.pack_into(">I", out, 0x1C, len(out))
    for n in nombres:
        out += n.encode("latin1")[:31].ljust(32, b"\0")
    return bytes(out)


def lamina_esqueletos(sk_o, w_o_cuadros, sk_d, w_d_cuadros, ruta, cuadros_muestra):
    """Monigotes lado a lado (origen | destino) vistos de frente (x, -y), para revisar a ojo sin juego."""
    from PIL import Image, ImageDraw  # noqa: PLC0415
    T = 220
    im = Image.new("RGB", (2 * T, T * len(cuadros_muestra)), (24, 24, 32))
    dr = ImageDraw.Draw(im)
    for fila, f in enumerate(cuadros_muestra):
        for col, (sk, w) in enumerate(((sk_o, w_o_cuadros[f]), (sk_d, w_d_cuadros[f]))):
            xs = [p[0] for _, p in w] + [p[2] for _, p in w]
            ys = [p[1] for _, p in w]
            lado = max(max(xs) - min(xs), max(ys) - min(ys), 1e-3)
            k = (T - 30) / lado
            cx, cy = sum(p[0] for _, p in w) / len(w), sum(p[1] for _, p in w) / len(w)

            def xy(p, col=col, k=k, cx=cx, cy=cy, fila=fila):
                return (col * T + T / 2 + (p[0] - cx) * k, fila * T + T / 2 - (p[1] - cy) * k)
            for i, (_, _, _, par) in enumerate(sk):
                if 0 <= par != i:
                    dr.line([xy(w[par][1]), xy(w[i][1])], fill=(120, 220, 120) if col else (220, 180, 90), width=2)
            dr.text((col * T + 4, fila * T + 4), ("destino" if col else "origen") + " cuadro %d" % f, fill=(255, 255, 0))
    os.makedirs(os.path.dirname(os.path.abspath(ruta)), exist_ok=True)
    im.save(ruta)


def render_destino(modelo, sk_d, cuadros, muestra, ruta, lado=320):
    """Malla HD del destino posada con los cuadros retargeteados (model_render, ortografica): de frente y de lado."""
    import numpy as np  # noqa: PLC0415
    from PIL import Image  # noqa: PLC0415
    import model_render as mr  # noqa: PLC0415
    from awg_vertex_buffer import qmat  # noqa: PLC0415
    if modelo[:4] == b"#AMB" and not altura._is_ps2(modelo):
        awo = struct.unpack(">I", modelo[0x20:0x24])[0]
        modelo = modelo[awo - 0x40:] if awo > 0x40 else modelo
    m = mr.Model(modelo)
    assert [x.lstrip("X") for x in m.labels] == [n for n, *_ in sk_d], "orden de huesos distinto entre model_render y altura"
    vistas = (np.eye(3), np.array([[0, 0, -1], [0, 1, 0], [1, 0, 0]], float))
    m.local = [np.array(qmat(*q, *p), float) for q, p in cuadros[muestra[0]]]
    P, _ = m.posed(None)
    lo, hi = P.min(0), P.max(0)
    centro, esc = (lo + hi) / 2, lado * 0.85 / max(float((hi - lo).max()), 1e-6)   # misma camara en todos los cuadros
    im = Image.new("RGB", (lado * len(vistas), lado * len(muestra)), (28, 32, 40))
    for fila, f in enumerate(muestra):
        m.local = [np.array(qmat(*q, *p), float) for q, p in cuadros[f]]
        for col, v in enumerate(vistas):
            rgba = mr.render(m, (lado, lado), v, centro, esc)
            im.paste(Image.fromarray(rgba).convert("RGB"), (col * lado, fila * lado))
    im.save(ruta)


# ------------------------------------------------------------------------------------------------ uso
def main(argv):
    import argparse  # noqa: PLC0415
    ap = argparse.ArgumentParser(prog="b3hd_conv anim")
    ap.add_argument("--esq-origen", required=True)
    ap.add_argument("--anim-origen", required=True)
    ap.add_argument("--clip", type=int, default=0)
    ap.add_argument("--esq-destino", required=True)
    ap.add_argument("--base", help="#ACM/#AMM del destino para la pose base (cocinado); sin el, el reposo del destino")
    ap.add_argument("--base-clip", type=int, default=0)
    ap.add_argument("--base-cuadro", type=int, default=0)
    ap.add_argument("--relativa", action="store_true", help="claves de giro sobre el reposo (anims de B2)")
    ap.add_argument("--mapa", help="JSON: huesos/fijos/raiz/escala (manda sobre lo automatico)")
    ap.add_argument("--salida", required=True, help="ruta del .acm; al lado .json (mapa) y _lamina.png")
    ap.add_argument("--render", action="store_true", help="destino HD: malla posada (model_render) en _render.png")
    a = ap.parse_args(argv)
    sk_o, sk_d = esqueleto(fuente(a.esq_origen)), esqueleto(fuente(a.esq_destino))
    anim_o, nf = leer_anim(fuente(a.anim_origen), a.clip)
    if a.base:
        ab, _ = leer_anim(fuente(a.base), a.base_clip)
        base_loc = locales(sk_d, ab, a.base_cuadro, False)
    else:
        base_loc = [(tuple(q), tuple(p)) for _, q, p, _ in sk_d]
    mapa, por = mapa_auto(sk_o, sk_d)
    cfg = json.load(open(a.mapa, encoding="utf-8")) if a.mapa else {}
    nd = {n: j for j, (n, *_) in enumerate(sk_d)}
    no = {n: i for i, (n, *_) in enumerate(sk_o)}
    suf_d = {altura.suffix(n): j for n, j in nd.items()}
    suf_o = {altura.suffix(n): i for n, i in no.items()}
    for d, o in cfg.get("huesos", {}).items():
        j = nd.get(d, suf_d.get(d))
        if j is None:
            raise SystemExit("hueso de destino desconocido en el mapa: %s" % d)
        mapa[j] = None if o is None else no.get(o, suf_o.get(o))
        por[j] = "mapa"
    fijos = [nd.get(x, suf_d.get(x)) for x in cfg.get("fijos", [])]
    raiz = nd.get(cfg.get("raiz"), suf_d.get(cfg.get("raiz"))) if cfg.get("raiz") else None
    escala = float(cfg.get("escala", 1.0))
    cuadros, _ = retarget(sk_o, anim_o, nf, sk_d, base_loc, mapa, fijos, raiz, escala, a.relativa)
    os.makedirs(os.path.dirname(os.path.abspath(a.salida)), exist_ok=True)
    open(a.salida, "wb").write(escribir_acm(sk_d, cuadros))
    base = os.path.splitext(a.salida)[0]
    filas = [{"destino": sk_d[j][0], "origen": (sk_o[mapa[j]][0] if mapa.get(j) is not None else None),
              "por": por.get(j, "fijo" if j in fijos else "pose base")} for j in range(len(sk_d))]
    json.dump({"origen": [a.esq_origen, a.anim_origen, a.clip], "destino": a.esq_destino, "cuadros": nf,
               "base": [a.base, a.base_clip, a.base_cuadro], "huesos": filas}, open(base + ".json", "w", encoding="utf-8"),
              indent=1, ensure_ascii=False)
    muestra = sorted({0, nf // 3, 2 * nf // 3, nf - 1})
    w_o = {f: mundo(sk_o, locales(sk_o, anim_o, f, a.relativa)) for f in muestra}
    w_d = {f: mundo(sk_d, cuadros[f]) for f in muestra}
    lamina_esqueletos(sk_o, w_o, sk_d, w_d, base + "_lamina.png", muestra)
    if a.render:
        render_destino(fuente(a.esq_destino), sk_d, cuadros, muestra, base + "_render.png")
    con = sum(1 for f in filas if f["origen"])
    print("anim: %d cuadros, %d/%d huesos con pareja, %d fijos -> %s (+ .json, _lamina.png)" % (nf, con, len(sk_d), len(fijos), a.salida))
    return 0


def autoprueba():
    """Sin juego: cadena de 3 huesos (origen PS2) -> cadena de 5 (destino) con otros nombres y otro reposo."""
    rad = lambda g: round(g * 65536 / 360) & 0xFFFF  # noqa: E731
    o = [("ZZ_WAIST", (0, 0, 0, 1), (0, 0, 0), -1), ("ZZ_AGO", (0, 0, 0, 1), (0, 1, 0), 0),
         ("ZZ_HIGE_1", (0, 0, 0, 1), (0, 1, 0), 1), ("ZZ_HIGE_2", (0, 0, 0, 1), (0, 1, 0), 2)]
    s45 = math.sin(math.pi / 8)
    d = [("DGN_WAIST", (0, 0, s45, math.cos(math.pi / 8)), (0, 0, 0), -1), ("DGN_JAW", (0, 0, 0, 1), (2, 0, 0), 0),
         ("DGN_HIGE1", (0, 0, 0, 1), (2, 0, 0), 1), ("DGN_HIGE2", (0, 0, 0, 1), (2, 0, 0), 2),
         ("DGN_HIGE3", (0, 0, 0, 1), (2, 0, 0), 3), ("DGN_TAIL", (0, 0, 0, 1), (-3, 0, 0), 0)]
    m, por = mapa_auto(o, d)
    assert m == {0: 0, 1: 1, 2: 2, 3: 2, 4: 3}, (m, por)   # HIGE1..3 <- HIGE_1,1,2 (3 eslabones sobre 2)
    assert por[1] == "sinonimo" and por[0] == "nombre" and por[2].startswith("cadena"), por
    # clip: la mandibula (AGO) gira 90 grados en z en 10 cuadros; la raiz sube 1
    anim = {"ZZ_AGO": ([(0, 0, 0, 0), (10, 0, 0, rad(90))], []), "ZZ_WAIST": ([], [(0, 0, 0, 0), (10, 0, 1, 0)])}
    base = [(tuple(q), tuple(p)) for _, q, p, _ in d]
    cu, base_w = retarget(o, anim, 11, d, base, m, fijos=[5], raiz=0, escala=2.0)
    w = mundo(d, cu[10])
    # delta de mundo de JAW = 90 en z sobre su base; la raiz sube 1*escala; TAIL (fijo) no se mueve
    dq = qmul(w[1][0], qinv(base_w[1][0]))
    assert abs(abs(dq[2]) - math.sin(math.pi / 4)) < 1e-3 and abs(abs(dq[3]) - math.cos(math.pi / 4)) < 1e-3, dq
    assert abs(w[0][1][1] - 2.0) < 1e-6, w[0][1]
    assert all(abs(a - b) < 1e-6 for a, b in zip(w[5][1], base_w[5][1])), (w[5], base_w[5])
    # ida y vuelta por el #ACM: lo escrito se lee igual (giros a 1/65536 de vuelta)
    acm = escribir_acm(d, cu)
    an2, nf2 = leer_anim(acm, 0)
    assert nf2 == 11 and set(an2) == {n for n, *_ in d}
    w2 = mundo(d, locales(d, an2, 10, False))
    for (q1, p1), (q2, p2) in zip(w, w2):
        assert abs(abs(sum(a * b for a, b in zip(q1, q2))) - 1) < 1e-6, (q1, q2)
        assert max(abs(a - b) for a, b in zip(p1, p2)) < 1e-3, (p1, p2)
    # muestreo por el camino corto: de 350 a 10 grados pasa por 0, no por 180
    v =_muestra([(0, 0, 0, rad(350)), (2, 0, 0, rad(10))], 1, True)[2]
    assert abs((v % 65536) - 0) < 2 or abs((v % 65536) - 65536) < 2, v
    print("retarget autoprueba OK")
    return 0


if __name__ == "__main__":
    sys.exit(autoprueba() if "--autoprueba" in sys.argv else main(sys.argv[1:]))
