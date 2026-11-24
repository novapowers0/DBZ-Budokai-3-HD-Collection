#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""vista_tactics.py - clips de animacion de B3 HD HORNEADOS para Dragon Tactics (T5/T6).

Lee los clips del manifiesto (mod center hd/tactics_manifest.toml: banco, anim, cuadros de espera, andar, ataque, tecnica) del
data_cmn.afs del jugador y deja, para el modelo de B3 con huesos de vista_b2.py (el mismo orden de huesos que sk_<fid>.sk), la
matriz LOCAL de cada hueso en cada cuadro. El juego (src/pasarela_nave.cpp, figuras de Tactics) solo la reproduce: no hace falta
nada de B2. Regla de raiz del manifiesto (raiz = "ignorar_xz"): del desplazamiento de la pista de un hueso se conserva solo la
componente vertical (el avance lo da el tablero, nunca la animacion).
Salida: <juego>/ingame_ui/nave_vista/tk_<fid>.tk (little endian):
  "TKB2", u32 nhuesos, u32 nclips; por clip: u32 cuadros, u32 bucle, f32 local[12] x nhuesos x cuadros  (filas 3x4 R|t)
Clips en este orden: espera, andar, ataque, tecnica (ausente o pendiente = 0 cuadros).
Uso: python vista_tactics.py --juego D:\DBZ3HD\copia --fid 264 --b3 0 --forma 0 [--us <carpeta us>]
     python vista_tactics.py --juego <carpeta> --todas [--us <carpeta us>] [--salida <carpeta>]   # lote 51 entradas (34 unidades + formas)
"""
import argparse
import os
import struct
import sys
import tempfile
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [HERE, os.path.join(HERE, ".."), os.path.join(HERE, "..", "studio"), os.path.join(HERE, "..", "..", "awo_tools")]
import numpy as np  # noqa: E402
import altura  # noqa: E402,F401  (los imports se rastrean para empaquetar: studio_core los usa dentro de funciones)
import awg_vertex_buffer  # noqa: E402,F401
import studio_core as sc  # noqa: E402
import vista_b2 as vb  # noqa: E402
import vista_heroe as vh  # noqa: E402

CLIPS = ("espera", "andar", "ataque", "tecnica", "dano", "derrota", "levantarse", "victoria")


def sin_pista(skel, acm, e, anim):
    """Indices de `skel` cuyo hueso no tiene ninguna pista en la animacion `anim` (el motor de B3 los deja en su pose propia)."""
    n, tbl, nb, names = struct.unpack_from(e + "4I", acm, 0x10)
    flags, _v, _nf, off = struct.unpack_from(e + "4I", acm, tbl + 16 * anim)
    per = 3 if flags & 0x10 else 2
    con = set()
    for j in range(nb):
        if off and any(struct.unpack_from(e + "%dI" % per, acm, off + 4 * per * j)[:2]):
            con.add(altura.suffix(acm[names + 32 * j:names + 32 * j + 32].split(b"\0")[0].decode("latin1")))
    return [i for i, (nm, *_r) in enumerate(skel) if altura.suffix(nm) not in con]


_COLAPSAR = set()   # SUR-8: huesos a escala 0 (alas de Cell perfecto: sus alas abiertas y planas no tienen el plegado de B3 localizado)


def poses_fijas(afs, m, skel, acm, e, cid, padres):
    """{hueso: matriz local 3x3} de los huesos sin pista (cola, alas, pelo): la pose del select nativo de B3 si la hay; si no, para
    cola y alas, la del cuadro 0 de la animacion del banco que las anima y las deja mas recogidas (menos lejos de la cintura).
    Sin esto salen en el reposo del modelo (cola y alas rectas y extendidas, como lanzas)."""
    libres = range(len(skel))
    nombres = [altura.suffix(x[0]) for x in skel]
    _COLAPSAR.clear()
    if any('WING' in n for n in nombres) and not any('TAIL' in n for n in nombres):
        _COLAPSAR.update(i for i, n in enumerate(nombres) if 'WING' in n)
    sel = vb.giros_select(afs, m, cid)
    out = {i: np.array(sel[i], float) for i in libres if i in sel}
    pend = [i for i in libres if i not in out and any(k in altura.suffix(skel[i][0]) for k in ("TAIL", "WING"))]
    cint = next((i for i, s in enumerate(skel) if altura.suffix(s[0]) == "WAIST"), 1)
    cand = []   # (lejania media de los huesos pendientes, huesos que anima, rotaciones)
    for a in range(struct.unpack_from(e + "I", acm, 0x10)[0]):
        libre = set(sin_pista(skel, acm, e, a))
        hay = [i for i in pend if i not in libre]
        if not hay:
            continue
        loc = sc.anim_locals(skel, acm, e, a, 0)
        W = [None] * len(skel)
        for i, L in enumerate(loc):
            L4 = np.vstack([L[:3, :4], [0, 0, 0, 1]])
            W[i] = W[padres[i]] @ L4 if padres[i] >= 0 else L4
        lej = np.mean([np.linalg.norm((np.linalg.inv(W[cint]) @ W[i])[:3, 3]) for i in pend])
        cand.append((lej, hay, {i: loc[i][:3, :3] for i in hay}))
    for _lej, hay, rot in sorted(cand, key=lambda c: c[0]):
        for i in hay:
            if i in pend:
                out[i] = rot[i]
                pend.remove(i)
    return out


def hornear(skel, acm, e, anim, cuadros, padres, fijas, ignorar_xz=True):
    """[cuadro][hueso] = matriz local 3x4; la raiz solo conserva el movimiento vertical."""
    reposo = [np.array(p, float) for _n, _q, p, _par in skel]
    salida = []
    for f in range(cuadros):
        loc = sc.anim_locals(skel, acm, e, anim, f)
        for i, r in fijas.items():
            loc[i][:3, :3] = r
        for i in _COLAPSAR:
            loc[i][:3, :3] = 0.0
        if ignorar_xz:
            for i, L in enumerate(loc):
                d = L[:3, 3] - reposo[i]
                if np.abs(d).max() < 1e-5:
                    continue
                R = np.eye(3)   # rotacion del mundo del padre (las locales de los huesos de arriba)
                p = padres[i]
                cadena = []
                while p >= 0:
                    cadena.append(p)
                    p = padres[p]
                for k in reversed(cadena):
                    R = R @ loc[k][:3, :3]
                dy = (R @ d)[1]
                L[:3, 3] = reposo[i] + R.T @ np.array([0.0, dy, 0.0])
        salida.append([L[:3, :4].reshape(-1) for L in loc])
    return salida


def banco_anim(afs, entrada):
    """(bytes #ACM/#AMM, endian) de una entrada de data_cmn que puede ser un #AMB con el #ACM dentro
    (lo normal) o el propio #ACM suelto (la espera generica del select, entrada 3881: Yamcha/Ginyu)."""
    b = afs.entry(int(entrada))
    if b[:4] in (b"#ACM", b"#AMM"):
        return b, sc.endian(b)
    return sc.anim_pool(b)


def hornear_unidad(afs, uni, cid):
    """TKB2 completo de una entrada del manifiesto (o None y motivo si no se puede)."""
    mb = afs.entry(int(uni["modelo"]))
    m = vh.mr.Model(mb)
    skel = altura.skeleton(mb)
    acm, e = banco_anim(afs, uni["banco"])
    if acm is None or len(skel) != len(m.local):
        return None, "banco %s sin animaciones o esqueleto distinto (%d / %d)" % (uni["banco"], len(skel), len(m.local))
    padres = [int(x) for x in m.parent]
    fijas = poses_fijas(afs, m, skel, acm, e, cid, padres)
    out = bytearray(b"TKB2" + struct.pack("<II", len(m.local), len(CLIPS)))
    for nom in CLIPS:
        c = uni["clips"].get(nom, {})
        n = int(c.get("frames", 0)) if c.get("estado") != "pendiente" else 0
        out += struct.pack("<II", n, 1 if c.get("bucle") else 0)
        # H14: clip de otro banco (p. ej. la espera generica del select, 3881). Se extrae el #ACM del #AMB:
        # pasar el #AMB crudo a anim_locals devolvia la pose de reposo (T-pose) en dano/derrota/levantarse (D2 TAC03).
        acm_c, e_c = banco_anim(afs, c["banco"]) if c.get("banco") else (acm, e)
        if n and acm_c is None:
            return None, "clip %s: banco %s sin #ACM" % (nom, c["banco"])
        for fr in (hornear(skel, acm_c, e_c, int(c["anim"]), n, padres, {i: fijas[i] for i in sin_pista(skel, acm_c, e_c, int(c["anim"])) if i in fijas}) if n else []):
            for mat in fr:
                out += struct.pack("<12f", *mat)
    return (out, len(m.local)), None


def escribir_tk(destino, fid, datos):
    os.makedirs(destino, exist_ok=True)
    path = os.path.join(destino, "tk_%d.tk" % fid)
    with open(path + ".tmp", "wb") as fh:
        fh.write(datos)
    os.replace(path + ".tmp", path)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--juego", required=True)
    ap.add_argument("--fid", type=int, default=None)
    ap.add_argument("--b3", type=int, default=None, help="ID de B3 del personaje")
    ap.add_argument("--forma", type=int, default=0)
    ap.add_argument("--id", type=int, default=None, help="ID de B3 de la forma (su pose del select para cola y alas)")
    ap.add_argument("--todas", action="store_true", help="hornea las 51 entradas del manifiesto (34 unidades + sus formas)")
    ap.add_argument("--salida", default=None, help="carpeta de los tk_ (por defecto <juego>/ingame_ui/nave_vista)")
    ap.add_argument("--us", default=None)
    a = ap.parse_args()
    with open(os.path.join(HERE, "..", "tactics_manifest.toml"), "rb") as fh:
        man = tomllib.load(fh)
    if a.todas:
        unidades = man["unidad"]
    else:
        if a.fid is None or a.b3 is None:
            ap.error("sin --todas hacen falta --fid y --b3")
        unidades = [u for u in man["unidad"] if u["b3"] == a.b3 and u["forma"] == a.forma]
        if not unidades:
            print("vista_tactics: B3 %d forma %d no esta en el manifiesto de Tactics" % (a.b3, a.forma))
            return 3
    afs = vh.rb.Afs(os.path.join(vh.rb.us_dir(a.us), "data_cmn.afs"), vh.rb.work_dir("vista_tactics_"))
    destino = a.salida or os.path.join(a.juego, "ingame_ui", "nave_vista")
    fallos = 0
    for uni in unidades:
        datos, error = hornear_unidad(afs, uni, a.id if a.id is not None else int(uni["b3"]))
        if error:
            print("vista_tactics: %s: %s" % (uni["id"], error))
            fallos += 1
            continue
        escribir_tk(destino, int(uni["modelo"]), datos[0])
        print("vista_tactics: %s fid %d (B3 %d forma %d) -> %d huesos, %d bytes" % (
            uni["id"], uni["modelo"], uni["b3"], uni["forma"], datos[1], len(datos[0])))
    if fallos:
        print("vista_tactics: %d entradas con fallo" % fallos)
        return 4
    return 0


if __name__ == "__main__":
    sys.exit(main())
