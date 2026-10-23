#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""vista_nave.py - modelo 3D de un luchador de B3 HD para la rueda de la nave de Babidi (A2nave, pasarela_nave.cpp).

El modelo REAL (entrada fid de data_cmn.afs del jugador; nada de arte en el repositorio) se exporta como malla posada HMV1
(el formato de vista_heroe.py) a <juego>/ingame_ui/nave_vista/<id>.malla. La pose es la del SELECT NATIVO de B3: #CSK 3882
(codigo = ID -> bloque, u16 = animacion) sobre el banco #ACM 3881, primer cuadro, rotacion local sustituida por la de la clave
(angulos 16 bits Rz*Ry*Rx, como las animaciones de B2). Sin pose propia (ID fuera de la tabla o sin pistas): la de los retratos.
El juego pasa el fid (tabla char96 de su memoria: fid = lista[modelo de la forma 0]).
Uso: python vista_nave.py --juego D:\DBZ3HD\copia --id 38 --fid 166 [--us <carpeta us>]
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
import vista_heroe as vh  # noqa: E402  (trae tambien model_render y roster_build)


def _euler(x, y, z):
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    return np.array([[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
                     [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
                     [-sy, cy * sx, cy * cx]])


def pose_select(afs, m, cid):
    """pose {hueso: rotacion} (formato de Model.posed) con el primer cuadro de la animacion del select de `cid`, o None."""
    rb = vh.rb
    try:
        acm, csk = afs.entry(3881), afs.entry(3882)
        n, lst = struct.unpack(">2I", csk[0x10:0x18])
        if cid >= n:
            return None
        blk = struct.unpack_from(">I", csk, lst + 4 * cid)[0]
        if not blk:
            return None
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
        nuevas = {}
        for i, lab in enumerate(m.labels):
            k = rot.get(rb._suffix(lab.lstrip("X")))
            if k:
                nuevas[i] = _euler(*[v * 2 * math.pi / 65536.0 for v in k[1:4]])
        if not nuevas:
            return None
        n_h = len(m.local)
        W = [None] * n_h
        pose = {}
        for i in range(n_h):
            p = m.parent[i]
            tiene_p = 0 <= p < n_h and p != i and W[p] is not None
            base = W[p] @ m.local[i] if tiene_p else m.local[i].copy()
            if i in nuevas:
                tgt = base.copy()
                tgt[:3, :3] = (W[p][:3, :3] @ nuevas[i]) if tiene_p else nuevas[i]
                pose[i] = tgt[:3, :3] @ np.linalg.inv(base[:3, :3])
                base = tgt
            W[i] = base
        return pose
    except Exception as e:   # noqa: BLE001 (sin pose propia: la de los retratos)
        print("vista_nave: pose del select no disponible (%s)" % e)
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--juego", required=True)
    ap.add_argument("--id", type=int, required=True)
    ap.add_argument("--fid", type=int, required=True)
    ap.add_argument("--us", default=None)
    a = ap.parse_args()
    salida = os.path.join(a.juego, "ingame_ui", "nave_vista")
    os.makedirs(salida, exist_ok=True)
    afs = vh.rb.Afs(os.path.join(vh.rb.us_dir(a.us), "data_cmn.afs"), tempfile.mkdtemp(prefix="vista_nave_"))
    m = vh.mr.Model(afs.entry(a.fid))
    pose = pose_select(afs, m, a.id)
    nativa = pose is not None
    if not nativa:
        pose = vh.pose_de(m)
    nt, ntex = vh.exportar_malla(m, pose, os.path.join(salida, "%d.malla" % a.id))
    print("vista_nave: id %d fid %d -> %d triangulos, %d texturas, pose %s" % (a.id, a.fid, nt, ntex, "del select" if nativa else "de retrato"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
