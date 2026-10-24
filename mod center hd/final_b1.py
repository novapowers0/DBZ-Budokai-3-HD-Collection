#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""final_b1.py - Animaciones de la escena final de la Leyenda de Mr. Satan de B1 -> luchadores NATIVOS de B3 HD (S5-A3b).

Del paquete de la escena (DATA_EN.AFS #2374 de B1, ISO del jugador) se toman los clips de Mr. Satan (8 clips seguidos,
3170 cuadros a 60 Hz) y de Cell (1 clip: su pose de KO, 650 cuadros) y se llevan al esqueleto del modelo HD de B3 del propio
juego del jugador (nada de B1 sale de su ISO; nada va en el repositorio). El resultado es un mod por entrada
(mods/<mod>/us/data_cmn.afs/<fid>/geom.bin) con:
  - el #ACM del moveset (hijo 1 del ANM) con UN clip nuevo al final (relocalizado +16 B por la entrada de la tabla),
  - el #CSK del moveset con un codigo libre (0x2F8) que apunta a ese clip.
La escena final (src/pasarela.cpp) pide ese codigo a cada luchador (cinematica::Paso::anim) y los dos arrancan a la vez.

Conversion de cada pista (medida con retarget_pose.py contra la escena original):
  - giro: delta del reposo de B1 aplicado al reposo de B3 (R3 = R1 R1reposo^-1 R3reposo): los dos esqueletos tienen los mismos
    huesos y casi las mismas orientaciones de reposo, pero la copia directa del giro de B1 da otra pose (hasta 15 grados por
    hueso); solo se omiten los huesos de cara/boca/alas cuyo reposo es muy distinto.
  - WAIST: el giro pierde el giro de cara al rival (B1 lo lleva en mundo) y la posicion de B1 (mundo) pasa al marco del
    luchador (ancla de la escena; la pista de B3 se SUMA al reposo) con la altura corregida cada cuadro hasta que el vertice
    mas bajo del modelo posado toca y = 0 (suelo medido, no un desplazamiento a ojo).
Uso:  python final_b1.py mod --us <carpeta us> --mods <carpeta mods> [--iso <ISO de B1>]
"""
import argparse
import glob
import math
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "awo_tools"), os.path.join(HERE, "studio")]

SUF = lambda n: n.lstrip("X").split("_", 1)[-1]
CODIGO = 0x2F8                      # libre en los CSK de Cell (1897 codigos) y de Mr. Satan (2039)
FRAMES = 3170                       # duracion de la escena en cuadros (suma de los clips de Satan del paquete)
SKIP = ("M_", "NH", "NW", "NL", "NR", "L00_FACE", "WING", "PICK", "HAIR", "JPACK", "FIRE", "OBI", "GAME", "TAIL")

# luchador -> (entrada B1 #AMO, hijo del paquete con su #AMM, clips, fid del moveset B3 (form 0), fid del modelo B3, ancla B1 (x, z) y giro)
PERSONAJES = {
    "cell": dict(amo=335, hijo=1, clips=[0], anm=159, modelo=148, ancla=(-35.0, -0.2), giro=180.0, mantener=True),
    "satan": dict(amo=2365, hijo=0, clips=list(range(8)), anm=382, modelo=377, ancla=(-35.0, -44.2), giro=0.0, mantener=False),
}
SUELO_B1 = 3.9


def _rot(x, y, z):
    cx, sx, cy, sy, cz, sz = np.cos(x), np.sin(x), np.cos(y), np.sin(y), np.cos(z), np.sin(z)
    return np.array([[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
                     [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
                     [-sy, cy * sx, cy * cx]])


def _euler(R, ref=None):
    """Matriz -> (x, y, z) en radianes con R = Rz Ry Rx; de las dos soluciones, la mas cercana a `ref` (continuidad)."""
    sy = -R[2, 0]
    y1 = math.asin(max(-1.0, min(1.0, sy)))
    c = []
    for y in (y1, math.pi - y1):
        cy = math.cos(y)
        if abs(cy) < 1e-6:
            x, z = 0.0 if ref is None else ref[0], math.atan2(-R[0, 1], R[1, 1])
        else:
            x, z = math.atan2(R[2, 1] / cy, R[2, 2] / cy), math.atan2(R[1, 0] / cy, R[0, 0] / cy)
        c.append((x, y, z))
    if ref is None:
        return c[0]

    def d(a):
        return sum(abs((a[i] - ref[i] + math.pi) % (2 * math.pi) - math.pi) for i in range(3))
    return min(c, key=d)


def _ry(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


# ----------------------------------------------------------------------------- #ACM / #CSK (HD, big-endian)
def acm_add_clip(acm, flags, var, nframes, rot, pos):
    """acm: bytes del #ACM HD. rot/pos: {indice de hueso del #ACM: [(cuadro, (a, b, c))]} (giro u16, posicion f32).
    Devuelve (acm nuevo, indice del clip): una entrada mas en la tabla (todo lo que viene detras se desplaza 16 B)."""
    n, tbl, nb, nom = struct.unpack_from(">4I", acm, 0x10)
    out = bytearray(acm)
    ins = tbl + 16 * n                               # donde entra la nueva entrada
    out[ins:ins] = bytes(16)
    shift = lambda p: p + 16 if p >= ins else p      # noqa: E731
    struct.pack_into(">I", out, 0x10, n + 1)
    struct.pack_into(">I", out, 0x1C, nom + 16)
    for c in range(n):                               # entradas de la tabla y bloques de punteros por hueso
        e = tbl + 16 * c
        fl, v, nf, off = struct.unpack_from(">4I", out, e)
        if off:
            struct.pack_into(">I", out, e + 12, off + 16)
            per = 3 if fl & 0x10 else 2
            for j in range(nb * per):
                q = struct.unpack_from(">I", out, off + 16 + 4 * j)[0]
                if q:
                    struct.pack_into(">I", out, off + 16 + 4 * j, q + 16)
    out += bytes((-len(out)) % 16)
    blk = len(out)
    per = 2
    out += bytes(4 * per * nb)
    for j in range(nb):
        if j in rot and rot[j]:
            out += bytes((-len(out)) % 4)
            struct.pack_into(">I", out, blk + 4 * per * j, len(out))
            out += struct.pack(">III", 0, 1, len(rot[j]))
            for f, (a, b, c) in rot[j]:
                out += struct.pack(">4H", f & 0xFFFF, a & 0xFFFF, b & 0xFFFF, c & 0xFFFF)
        if j in pos and pos[j]:
            out += bytes((-len(out)) % 4)
            struct.pack_into(">I", out, blk + 4 * per * j + 4, len(out))
            out += struct.pack(">III", 0, 1, len(pos[j]))
            for f, (x, y, z) in pos[j]:
                out += struct.pack(">I3f", f, x, y, z)
    struct.pack_into(">4I", out, tbl + 16 * n, flags & ~0x10, var, nframes, blk)
    return bytes(out), n


def csk_add_code(csk, codigo, anim, ap6=()):
    """#CSK HD con el codigo `codigo` (libre) apuntando al clip `anim` del almacen 3 (propio).
    ap6: lineas AP tipo 6 (manos/cara) (cuadro, orden, parte, etiqueta): parte 0 mano izq, 1 mano der, 2 cara; etiqueta = el
    numero Lnn del modelo (18 = XSTN_L18_FACE, ojos apretados; 0 = la de reposo). Es lo que usa el juego en sus propias animaciones."""
    nn, lst = struct.unpack_from(">2I", csk, 0x10)
    if codigo >= nn or struct.unpack_from(">I", csk, lst + 4 * codigo)[0]:
        raise ValueError("el codigo %#x no esta libre" % codigo)
    out = bytearray(csk)
    out += bytes((-len(out)) % 16)
    addr = len(out)
    out += struct.pack(">HH", anim, 3) + bytes(20) + b"\xff" * 12 + struct.pack(">3I", 0, 0, 0)
    if ap6:
        out += bytes((-len(out)) % 16)
        lin = len(out) + 8
        tabla = len(out)
        out += struct.pack(">HHI", 6, len(ap6), lin)
        for fr, orden, parte, etq in ap6:
            out += struct.pack(">HBBIII", fr, orden, 2, parte, etq, 0)
        struct.pack_into(">2I", out, addr + 24 + 12 + 4, 1, tabla)   # n_ap y direccion de la tabla AP del sub-bloque
    struct.pack_into(">I", out, lst + 4 * codigo, addr)
    return bytes(out)


# ----------------------------------------------------------------------------- conversion
def _sec():
    import importar  # noqa: PLC0415
    import model_render  # noqa: PLC0415
    import studio_core as sc  # noqa: PLC0415
    import fk_b1 as K  # noqa: PLC0415
    return importar, model_render, sc, K


class Conversor:
    def __init__(self, nombre, afs, us=None):
        importar, model_render, sc, K = _sec()
        self.K, self.sc = K, sc
        p = PERSONAJES[nombre]
        self.p = p
        self.nombre = nombre
        pk = afs.entry(2374)
        n, t = struct.unpack_from("<II", pk, 0x10)
        o, s = struct.unpack_from("<2I", pk, t + 16 * p["hijo"])
        self.amm1 = pk[o:o + s]
        self.amo1 = afs.entry(p["amo"])
        self.names1, self.local1, self.parent1 = K.amo_info(self.amo1)
        self.d1 = {SUF(x): i for i, x in enumerate(self.names1)}
        self.nm1 = {SUF(x): x for x in self.names1}
        self.model = model_render.Model(sc.hd_entry(p["modelo"], us))
        self.anm = sc.hd_entry(p["anm"], us)
        e, kids = ">", sc.amb_kids(self.anm, ">")
        self.acm = self.anm[kids[1][0]:kids[1][0] + kids[1][1]]
        self.csk = self.anm[kids[0][0]:kids[0][0] + kids[0][1]]
        nc, tbl, nb, nom = struct.unpack_from(">4I", self.acm, 0x10)
        self.nb = nb
        self.nombres3 = [self.acm[nom + 32 * j:nom + 32 * j + 32].split(b"\0")[0].decode("latin1") for j in range(nb)]
        self.j3 = {SUF(x): j for j, x in enumerate(self.nombres3)}
        m = self.model
        self.suf_m = [SUF(l) for l in m.labels]
        self.iw = self.suf_m.index("WAIST")
        self.G = _ry(math.radians(float(os.environ.get("FINAL_B1_GIRO_" + nombre.upper(), p["giro"]))))   # la variable solo sirve para pruebas
        self.pos0 = np.array(m.local[self.iw][:3, 3])
        self.vb = m.vbone
        self.vp = m.pos

    def _valido(self, s):
        if s not in self.nm1 or s not in self.j3 or any(s.startswith(k) or k in s for k in SKIP) or s == "BODY":
            return False
        i = self.suf_m.index(s) if s in self.suf_m else None
        if i is None:
            return False
        return float(np.abs(self.local1[self.d1[s]][:3, :3] - self.model.local[i][:3, :3]).max()) < 0.6

    def rotacion(self, s, R1):
        i = self.suf_m.index(s)
        if s == "WAIST":
            return self.G @ R1
        return (R1 @ self.local1[self.d1[s]][:3, :3].T) @ self.model.local[i][:3, :3]

    def locales(self, tr, f, dy=0.0, waist=None):
        """Matrices locales B3 en el cuadro f del clip `tr` (K.clip_tracks)."""
        nf, ang, pos = tr
        m = self.model
        L = [x.copy() for x in m.local]
        for i, s in enumerate(self.suf_m):
            if not self._valido(s) or self.nm1[s] not in ang or not ang[self.nm1[s]]:
                continue
            L[i][:3, :3] = self.rotacion(s, self.K.rot(*self.K.interp_ang(ang[self.nm1[s]], f)))
        wk = self.nm1["WAIST"]
        a = self.p["ancla"]
        p1 = np.array(self.K.interp_pos(pos[wk], f)) if wk in pos and pos[wk] else self.local1[self.d1["WAIST"]][:3, 3]
        q = self.G @ (p1 - np.array([a[0], SUELO_B1, a[1]]))
        L[self.iw][:3, 3] = q
        L[self.iw][1, 3] += dy
        return L, q

    def suelo(self, L):
        m = self.model
        W = [None] * len(L)
        for i in range(len(L)):
            p = m.parent[i]
            W[i] = W[p] @ L[i] if 0 <= p < i else L[i]
        Wn = np.array(W)
        P = np.einsum("nij,nj->ni", Wn[self.vb, :3, :3], self.vp) + Wn[self.vb, :3, 3]
        return float(P[:, 1].min())

    def construir(self, paso=1):
        """-> (rot, pos) por indice de hueso del #ACM, con los clips del personaje concatenados."""
        K = self.K
        rot, pos = {}, {}
        ref_e = {}
        g0 = 0
        ultimo = None
        for c in self.p["clips"]:
            tr = K.clip_tracks(self.amm1, c)
            nf = tr[0]
            ang = tr[1]
            # giros: una clave por clave de B1
            for s, nm in self.nm1.items():
                if not self._valido(s) or nm not in ang or not ang[nm]:
                    continue
                j = self.j3[s]
                for k in ang[nm]:
                    R3 = self.rotacion(s, K.rot(*[v * 9.5873799e-5 for v in k[1:4]]))
                    e = _euler(R3, ref_e.get(s))
                    ref_e[s] = e
                    u = tuple(int(round(v / (2 * math.pi) * 65536)) & 0xFFFF for v in e)
                    rot.setdefault(j, []).append((g0 + k[0], u))
            # cadera: una clave por cuadro (altura corregida al suelo cada cuadro)
            fr = range(0, nf, paso) if self.p["mantener"] is False else (0,)
            for f in fr:
                L, q = self.locales(tr, f)
                dy = -self.suelo(L)
                ultimo = (q + np.array([0, dy, 0])) - self.pos0
                pos.setdefault(self.j3["WAIST"], []).append((g0 + f, tuple(float(v) for v in ultimo)))
            g0 += nf
        total = FRAMES
        if self.p["mantener"]:                       # Cell: su pose se queda hasta el final de la escena
            for j in rot:
                rot[j].append((total - 1, rot[j][-1][1]))
            pos[self.j3["WAIST"]].append((total - 1, tuple(float(v) for v in ultimo)))
        else:
            for j in rot:
                if rot[j][-1][0] < total - 1:
                    rot[j].append((total - 1, rot[j][-1][1]))
            if pos[self.j3["WAIST"]][-1][0] < total - 1:
                pos[self.j3["WAIST"]].append((total - 1, pos[self.j3["WAIST"]][-1][1]))
        for j in rot:                                # los fotogramas de cada pista, ordenados y sin repetidos
            d = {}
            for f, u in rot[j]:
                d[f] = u
            rot[j] = sorted(d.items())
        return rot, pos


    def deltas(self):
        """Por cuadro global G (0..FRAMES-1) del clip unico de Satan: float32 x 9 en el marco de la escena (el de la camara del #AMC):
        diferencia cabeza B3 - cabeza B1, diferencia (media de las dos manos) B3 - B1 y la cabeza de B1. La camara y la tarjeta de B1
        apuntan a los huesos de B1; el cuerpo de B3 es mas bajo (cabeza ~1 u): se suma esta diferencia. B3: x' = -22 + z_m, y = y_m (el suelo de la escena es y = 0), z' = -x_m;
        B1: x' = z + 22,2, y' = y - 3,9, z' = -(x + 35)."""
        m = self.model
        iK = {s: i for i, s in enumerate(self.suf_m)}
        huesos = ["HEAD", "L00_LHAND", "L00_RHAND"]
        out = np.zeros((FRAMES, 9), dtype="<f4")
        g0 = 0
        for c in self.p["clips"]:
            tr = self.K.clip_tracks(self.amm1, c)
            for f in range(tr[0]):
                if g0 + f >= FRAMES:
                    break
                pos1, _ = self.K.fk(self.amo1, self.amm1, c, f, (self.names1, self.local1, self.parent1), tr)
                L, q = self.locales(tr, f)
                dy = -self.suelo(L)
                L[self.iw][1, 3] += dy
                W = [None] * len(L)
                for i in range(len(L)):
                    pa = m.parent[i]
                    W[i] = W[pa] @ L[i] if 0 <= pa < i else L[i]
                P3, P1 = [], []
                for h in huesos:
                    b = W[iK[h]][:3, 3]
                    P3.append(np.array([-22.0 + b[2], b[1], -b[0]]))
                    w = pos1[self.d1[h]]
                    P1.append(np.array([w[2] + 22.2, w[1] - 3.9, -(w[0] + 35.0)]))
                out[g0 + f, 0:3] = P3[0] - P1[0]
                out[g0 + f, 3:6] = P3[2] - P1[2]   # mano derecha de Satan (la de la tarjeta en B1: la que sale a la izquierda de la imagen)
                out[g0 + f, 6:9] = P1[0]
            g0 += tr[0]
        if g0 < FRAMES:
            out[g0:] = out[g0 - 1]
        return out

    def caras(self):
        """Cara de Mr. Satan por tramos: ojos apretados (XSTN_L18) de 28,53 a 33,03 s de la escena (= 37,25-41,75 s del video de B1, desfase 8,72 s medido por los cortes de camara);
        FINAL_B1_CARA="cuadro:orden:parte:etiqueta,..." lo cambia para pruebas."""
        if self.nombre != "satan":
            return ()
        e = os.environ.get("FINAL_B1_CARA")
        if e is not None:
            return tuple(tuple(int(v) for v in t.split(":")) for t in e.split(",") if t)
        return ((1712, 2, 2, 18), (1982, 3, 2, 0))

    def mod_entradas(self, paso=1):
        """-> (anm HD nuevo, indice del clip)."""
        sc = self.sc
        rot, pos = self.construir(paso)
        acm, idx = acm_add_clip(self.acm, 0x9, int(os.environ.get("FINAL_B1_VAR_" + self.nombre.upper(), 100)), FRAMES, rot, pos)   # la variable solo sirve para pruebas de cara
        csk = csk_add_code(self.csk, CODIGO, idx, self.caras())
        e, kids = ">", sc.amb_kids(self.anm, ">")
        anm = sc.amb_rebuild(self.anm, ">", 1, acm)
        anm = sc.amb_rebuild(anm, ">", 0, csk)
        return anm, idx


def iso_b1():
    import b1port  # noqa: PLC0415
    return b1port.find_b1_iso()


MOD = "Final Leyenda Satan (B1)"
VERSION = 2   # subir si cambia el resultado: se rehace en el equipo del jugador


def _abrir_b1(iso):
    import importar  # noqa: PLC0415
    if os.path.isdir(iso):
        for r, _, fs in os.walk(iso):
            for f in fs:
                if f.lower() == "data_en.afs":
                    return importar.AfsFile(os.path.join(r, f))
        raise FileNotFoundError("sin DATA_EN.AFS en %s" % iso)
    return importar.AfsFile(iso, "/USR/DATA_EN.AFS")


def construir(afs, us, mods, solo=None, paso=1, log=print):
    """Crea mods/<MOD> con los dos moveset (Cell y Mr. Satan). Devuelve la carpeta."""
    import studio_core as sc  # noqa: PLC0415
    mdir = os.path.join(mods, MOD)
    for nombre in ([solo] if solo else list(PERSONAJES)):
        cv = Conversor(nombre, afs, us)
        anm, idx = cv.mod_entradas(paso)
        comp = sc.lzx(anm)
        slot = sc.afs_slot(cv.p["anm"], us)
        reserva = (max((slot + 0xFFF) & ~0xFFF, len(comp)) + 0xFFF) & ~0xFFF
        dst = os.path.join(mdir, "us", "data_cmn.afs", str(cv.p["anm"]), "geom.bin")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as fh:
            fh.write(comp + bytes(reserva - len(comp)))
        log("%s: anm %d, clip %d, codigo %#x, %d B -> %s" % (nombre, cv.p["anm"], idx, CODIGO, len(comp), dst))
        if nombre == "satan":
            cv.deltas().tofile(os.path.join(mdir, "delta_satan.bin"))   # la camara y la tarjeta de B1 se corrigen con esto (pasarela.cpp)
    with open(os.path.join(mdir, "manifest.txt"), "w", encoding="utf-8") as fh:
        fh.write("name=%s\ndescription=Final de la Leyenda de Mr. Satan: poses de B1 (Cell tumbado, Mr. Satan) en los luchadores de B3 "
                 "(creado de tu ISO de B1; no se reparte)\ntype=final_b1\nsource=B1 DATA_EN 2374\ntarget=159,382\nregion=us\n" % MOD)
    return mdir


def auto(mods, us, ps2):
    """Lo llama el launcher al pulsar Jugar (campos_iw.py auto): crea el mod si hay ISO de B1 y falta o cambio. Nunca falla el juego."""
    try:
        import importar  # noqa: PLC0415
        importar.PS2 = str(ps2)
        iso = importar.source_path("b1")
        if not iso:
            print("final B1: sin Budokai 1 en %s (la escena final usa sus props)" % ps2)
            return 0
        st = os.stat(iso)
        sello = "%s|%d|%d|v%d" % (os.path.basename(iso), st.st_size, int(st.st_mtime), VERSION)
        mdir = os.path.join(mods, MOD)
        ok = os.path.join(mdir, "origen.txt")
        if os.path.isfile(ok) and open(ok, encoding="utf-8").read() == sello and all(
                os.path.isfile(os.path.join(mdir, "us", "data_cmn.afs", str(PERSONAJES[k]["anm"]), "geom.bin")) for k in PERSONAJES):
            print("final B1: al dia")
            return 0
        construir(_abrir_b1(iso), us, str(mods), paso=2)
        with open(ok, "w", encoding="utf-8") as fh:
            fh.write(sello)
        print("final B1: creado %s" % MOD)
    except Exception as e:  # noqa: BLE001 (un fallo aqui no para el juego)
        print("final B1: fallo %s" % e)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("orden", choices=["mod"])
    ap.add_argument("--us", default=os.path.join(ROOT, "us"))
    ap.add_argument("--mods", default=os.path.join(ROOT, "mods"))
    ap.add_argument("--iso")
    ap.add_argument("--solo", choices=list(PERSONAJES))
    ap.add_argument("--paso", type=int, default=1)
    a = ap.parse_args()
    iso = a.iso or iso_b1()
    construir(_abrir_b1(iso), a.us, a.mods, a.solo, a.paso)


if __name__ == "__main__":
    main()