#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""vista_b2.py - modelo de B3 HD CON HUESOS para dibujar a los luchadores grandes de B2 (tablero y nave de Babidi).

B3M (25/10/2026). Los luchadores de B2 se dibujan con su modelo de B3 (el mismo personaje y traje) animado con la animacion
de B2: el juego (pasarela_nave.cpp, FigSk) lee este fichero, pone a cada hueso la rotacion que la animacion de B2 da al hueso
del mismo SUFIJO (GOK_RARM1 -> RARM1; los esqueletos de B2 y B3 son los mismos en 26 de 29 luchadores, medido) y piel rigida:
el hueso de cada vertice es el de model_render (palette = slot).

El modelo REAL sale del data_cmn.afs del jugador (fid de la tabla char96 de la memoria del juego); nada se reparte.
Salida: <juego>/ingame_ui/nave_vista/sk_<fid>.sk   (formato HMS3, little endian):
  "HMS3", u32 ntex, u32 ntri, u32 nhuesos, f32 alto_reposo
  texturas: (u32 w, u32 h, RGBA8) x ntex            (las mismas ya "cocidas" que HMV1)
  huesos:   (i32 padre, char[24] sufijo, f32 local[12] = filas 3x4 R|t, u32 con_giro, f32 giro[9]) x nhuesos   (giro = rotacion LOCAL del select nativo de B3)
  triangulos: 3 x (pos xyz, normal xyz, uv; en el espacio del hueso, f32 x8, u32 hueso) ; i32 textura ; u32 banderas (1 = translucido)
  rampa de B3 (25/10): u32 n_orig; por textura original: u32 n, n x (f32 luz desde, u32 textura cocida de ese tramo)
  boca (R23 BOCA-03, opcional): "LIP1", u32 nposes, u32 nhuesos; nhuesos x (u32 hueso del .sk); nposes x nhuesos x f32[12]
    = matrices LOCALES de los huesos de boca en el ultimo cuadro de cada clip del #ACM del bin LIPS de la forma (pose =
    variante*6 + visema). Los lectores viejos paran tras la rampa y no lo ven.
Uso: python vista_b2.py --juego D:\DBZ3HD\copia --fid 264 [--id 0] [--us <carpeta us>]
"""
import argparse
import math
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
import vista_heroe as vh  # noqa: E402  (trae model_render y roster_build)
import vista_nave as vn  # noqa: E402  (euler como las animaciones de B2)


def giros_select(afs, m, cid):
    """{hueso: rotacion LOCAL 3x3} del primer cuadro de la animacion del select nativo de B3 (#CSK 3882 / #ACM 3881) del
    personaje `cid`: la pose propia de B3 para los huesos que B2 no anima (cola, capa, pelo...). {} si no hay."""
    rb = vh.rb
    try:
        acm, csk = afs.entry(3881), afs.entry(3882)
        n, lst = struct.unpack(">2I", csk[0x10:0x18])
        if cid is None or cid < 0 or cid >= n:
            return {}
        blk = struct.unpack_from(">I", csk, lst + 4 * cid)[0]
        if not blk:
            return {}
        anim = struct.unpack_from(">H", csk, blk)[0]
        _, t, nb, no = struct.unpack(">4I", acm[0x10:0x20])
        names = [acm[no + 32 * i:no + 32 * i + 32].split(b"\0")[0].decode("latin1") for i in range(nb)]
        flags, _, _, off = struct.unpack_from(">4I", acm, t + 16 * anim)
        per = 3 if flags & 0x10 else 2
        rot = {}
        for j in range(nb):
            p = struct.unpack_from(">I", acm, off + 4 * (per * j))[0]
            if p and struct.unpack_from(">I", acm, p + 8)[0]:
                rot[rb._suffix(names[j])] = struct.unpack_from(">4H", acm, p + 12)
        out = {}
        for i, lab in enumerate(m.labels):
            k = rot.get(rb._suffix(lab.lstrip("X")))
            if k:
                out[i] = vn._euler(*[v * 2 * math.pi / 65536.0 for v in k[1:4]])
        return out
    except Exception as e:   # noqa: BLE001
        print("vista_b2: pose del select no disponible (%s)" % e)
        return {}


# POST2/SHENRON-HD: modelos cuyo reposo no sirve para la escena de B2 y se exportan ya posados con un cuadro de su propia
# animacion de B3 ({fid: (anim, cuadro)}). Shenron (534): el reposo es RECTO (421 de largo en -z); el cuadro 114 de su
# anim 0 (#ACM, hijo 3 del #AMB) es el enroscado de la pantalla de deseos de B3. PonerShenronB3 lo lleva a la caja de B2.
POSE_FIJA = {534: (0, 114), 2209: (0, 0)}
# POST3/SHENRON-HD: el Shenron de la pantalla de deseos de B3 HD NO es el 534 (ese es dorado: bases blancas y rampa azul)
# sino tres entradas sueltas de data_cmn: #ACM 2208 (anim 0 = enroscado, 300 cuadros), #AWO 2209 (112 huesos SNR_*,
# 12056 triangulos) y #AZT 2210 (8 texturas, verdes). Mide como el de B2 (x -10..11, y 2..20). {fid del #AWO: (#AZT, #ACM)}
SUELTOS = {2209: (2210, 2208)}


def posar_con_acm(m, amb, awo_bin, anim, cuadro):
    """Pone en m.local las matrices locales del cuadro `cuadro` de la anim `anim` del #ACM del propio #AMB."""
    import altura  # noqa: PLC0415
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "studio"))
    import studio_core as sc  # noqa: PLC0415
    acm, e = (amb, ">") if amb[:4] == b"#ACM" else sc.anim_pool(amb)
    if acm is None:
        raise ValueError("el #AMB no trae #ACM")
    m.local = sc.anim_locals(altura.skeleton(awo_bin), acm, e, anim, cuadro)


def poses_lips(afs, fid, m):
    """[(hueso del modelo, [12 matrices 4x4])] del bin LIPS de la forma del modelo `fid` (roster_db.json: lips[traje*lpc +
    min(forma, lpc-1)]), o None. Mismo convenio que m.local (comprobado en Goku 264 / LIPS 289: reposo identico)."""
    import json  # noqa: PLC0415
    import altura  # noqa: PLC0415
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "studio"))
    import studio_core as sc  # noqa: PLC0415
    try:
        db = json.load(open(os.path.join(os.path.dirname(HERE), "roster_db.json"), encoding="utf-8"))
        lips = None
        for e in (db.get("ids") or []):
            if fid in e.get("models", []):
                i = e["models"].index(fid)
                mpc, lpc = max(1, e["models_per_costume"]), max(1, e.get("lips_per_costume", 1))
                k = (i // mpc) * lpc + min(i % mpc, lpc - 1)
                lips = e["lips"][k] if k < len(e.get("lips", [])) else None
                break
        if lips is None:
            return None
        lp = afs.entry(lips)
        if lp[:4] != b"#AMB":
            return None
        o = lp.find(b"#ACM", 0x40)   # #AMB -> #AWO (0x40) #AWG #ACM (Goku 289: 0x760)
        if o < 0:
            return None
        acm = lp[o:]
        sk = altura.skeleton(lp)
        nclips, = struct.unpack(">I", acm[0x10:0x14])
        tbl, = struct.unpack(">I", acm[0x14:0x18])
        poses = []
        for c in range(nclips):
            nf = struct.unpack_from(">I", acm, tbl + 16 * c + 8)[0]
            poses.append(sc.anim_locals(sk, acm, ">", c, max(0, nf - 1)))
        etiq = [l.lstrip("X") for l in m.labels]
        out = []
        for j, (nm, *_r) in enumerate(sk):
            nm = nm.lstrip("X")
            if "_M_" in nm and nm in etiq:
                out.append((etiq.index(nm), [p[j] for p in poses]))
        print("vista_b2: boca LIPS %d: %d poses, %d huesos" % (lips, nclips, len(out)))
        return out or None
    except Exception as e:   # noqa: BLE001
        print("vista_b2: boca LIPS no disponible (%s)" % e)
        return None


def exportar_sk(m, path, giros=None, boca=None):
    giros = giros or {}
    tris = m.tris
    texs, clave_tex, tri_tex, tramos = [], {}, [], []
    for mi in m.tri_mat:
        mat = m.mats[mi]
        k = (mat["tex"], mat["shader"], mat["env"], tuple(np.round(mat["diffuse"], 3)), mat["blend"])
        if k not in clave_tex:
            clave_tex[k] = len(texs)
            texs.append(vh._textura_cocida(m, mat))
            tramos.append(vh.tramos_rampa(m, mat))
        tri_tex.append(clave_tex[k])
    n_orig = len(texs)
    tabla = []   # por textura original: [(l_desde, indice de textura)]
    for i, tr in enumerate(tramos):
        if len(tr) <= 1:
            tabla.append([(0.0, i)])
            continue
        fila = []
        for l0, rgba in tr:
            fila.append((l0, len(texs)))
            texs.append(rgba)
        tabla.append(fila)
    blend = [m.mats[mi]["blend"] for mi in m.tri_mat]
    P, _ = m.posed(None)
    usados = np.unique(tris.reshape(-1)) if len(tris) else np.arange(len(P))
    lo, hi = P[usados].min(0), P[usados].max(0)
    out = bytearray(b"HMS3")   # HMS2 + al final la tabla de tramos de la rampa de B3
    nb = len(m.local)
    out += struct.pack("<IIIf", len(texs), len(tris), nb, float(hi[1] - lo[1]))
    for t in texs:
        img = (np.clip(t, 0, 1) * 255 + 0.5).astype(np.uint8)
        out += struct.pack("<II", img.shape[1], img.shape[0]) + img.tobytes()
    for i in range(nb):
        suf = vh.rb._suffix(m.labels[i].lstrip("X")).encode("latin1")[:23]
        out += struct.pack("<i", int(m.parent[i])) + suf.ljust(24, b"\0") + struct.pack("<12f", *m.local[i][:3, :4].reshape(-1))
        g = giros.get(i)
        out += struct.pack("<I9f", 1 if g is not None else 0, *(g.reshape(-1) if g is not None else np.eye(3).reshape(-1)))
    for i, tri in enumerate(tris):
        for v in tri:
            out += struct.pack("<8fI", *m.pos[v], *m.nrm[v], *m.uv[v], int(m.vbone[v]))
        out += struct.pack("<iI", tri_tex[i], 1 if blend[i] else 0)
    out += struct.pack("<I", n_orig)
    for fila in tabla:
        out += struct.pack("<I", len(fila))
        for l0, ti in fila:
            out += struct.pack("<fI", l0, ti)
    boca = boca or []   # LIP1 siempre (vacio sin LIPS): el juego sabe que el .sk ya es del horno con boca
    if True:
        npos = len(boca[0][1]) if boca else 0
        out += b"LIP1" + struct.pack("<II", npos, len(boca))
        for h, _p in boca:
            out += struct.pack("<I", h)
        for k in range(npos):
            for _h, p in boca:
                out += struct.pack("<12f", *np.asarray(p[k])[:3, :4].reshape(-1))
    tmp = path + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(out)
    os.replace(tmp, path)
    return len(tris), len(texs), nb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--juego", required=True)
    ap.add_argument("--fid", type=int, required=True)
    ap.add_argument("--us", default=None)
    ap.add_argument("--id", type=int, default=None, help="ID de B3 del personaje (su pose del select para los huesos que B2 no anima)")
    a = ap.parse_args()
    salida = os.path.join(a.juego, "ingame_ui", "nave_vista")
    os.makedirs(salida, exist_ok=True)
    afs = vh.rb.Afs(os.path.join(vh.rb.us_dir(a.us), "data_cmn.afs"), tempfile.mkdtemp(prefix="vista_b2_"))
    amb = d = afs.entry(a.fid)
    awo = struct.unpack(">I", d[0x20:0x24])[0] if d[:4] == b"#AMB" and len(d) > 0x24 else 0x40
    if awo > 0x40:   # B2-P13-HD: #AMB de 3 hijos (Shenron, 534): model_render espera el #AWO en 0x40
        d = d[awo - 0x40:]
    if a.fid in SUELTOS:   # #AWO suelto: model_render lo espera en 0x40 y con su #AZT detras
        d = bytes(1) * 0x40 + d + afs.entry(SUELTOS[a.fid][0])
        amb = afs.entry(SUELTOS[a.fid][1])
    m = vh.mr.Model(d)
    if a.fid in POSE_FIJA:
        posar_con_acm(m, amb, d, *POSE_FIJA[a.fid])
    nt, ntex, nb = exportar_sk(m, os.path.join(salida, "sk_%d.sk" % a.fid), giros_select(afs, m, a.id),
                               poses_lips(afs, a.fid, m))
    print("vista_b2: fid %d -> %d triangulos, %d texturas, %d huesos" % (a.fid, nt, ntex, nb))
    return 0


if __name__ == "__main__":
    sys.exit(main())
