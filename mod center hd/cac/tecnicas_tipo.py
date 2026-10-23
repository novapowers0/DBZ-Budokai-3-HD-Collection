#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tecnicas_tipo.py - tipo de entrada de cada tecnica (capsula #SKC) segun el #CCM del moveset de su duenyo (C4, item 3).

Medido sobre los 44 nativos: en los bloques de entrada (starters) del #CCM, w0 = 1 -> "E + derecha", w0 = 2 -> "E + izquierda",
y la condicion 0x0008 = definitiva (P+K+G+E en modo hiper). Escribe <juego>/ingame_ui/heroe_vista/tecnicas_tipo.txt con
lineas `capsula tipo donante` (tipo 0 = definitiva, 1 = E+derecha, 2 = E+izquierda). Lo lee heroe.cpp (TipoTecnica):
un unico ultimate activo, una tecnica en E+derecha y otra en E+izquierda.
  python tecnicas_tipo.py --juego D:/DBZ3HD/copia [--us carpeta_us]
"""
import argparse
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import capsulas  # noqa: E402
import roster_build as rb  # noqa: E402


def tipos(afs):
    out = {}
    for cid in range(0, 44):
        dn = rb.IDS.get(cid)
        if not dn or dn.get("cam") is None:
            continue
        cam = afs.entry(dn["cam"])
        at = capsulas.ccm_child(cam)
        if not at:
            continue
        ccm = cam[at[0]:at[0] + at[1]]
        for o, st in capsulas.ccm_blocks(ccm):
            cap = capsulas.w(ccm, o, 8)
            if not st or not cap:
                continue
            c1, w0 = capsulas.w(ccm, o, capsulas.COND), capsulas.w(ccm, o, 0)
            t = 0 if c1 & 8 else 1 if w0 == 1 else 2 if w0 == 2 else None
            if t is not None:
                out.setdefault(cap, (t, cid))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--juego", required=True)
    ap.add_argument("--us")
    a = ap.parse_args()
    afs = rb.Afs(os.path.join(rb.us_dir(a.us), "data_cmn.afs"), tempfile.mkdtemp(prefix="tec_tipo_"))
    t = tipos(afs)
    d = os.path.join(a.juego, "ingame_ui", "heroe_vista")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "tecnicas_tipo.txt"), "w", encoding="utf-8", newline="\n") as f:
        for cap in sorted(t):
            f.write("%d %d %d\n" % (cap, t[cap][0], t[cap][1]))
    print("tecnicas_tipo: %d capsulas (%d definitivas, %d E+der, %d E+izq)" % (
        len(t), sum(1 for v in t.values() if v[0] == 0), sum(1 for v in t.values() if v[0] == 1),
        sum(1 for v in t.values() if v[0] == 2)))


if __name__ == "__main__":
    main()
