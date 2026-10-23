#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_xv2.py - N0 de los lectores de XV2 sobre ficheros REALES del CPK del jugador (se salta si no esta XV2).
  python test_xv2.py"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import numpy as np  # noqa: E402
import xv2_leer as L  # noqa: E402
xm = L.xm


def main():
    try:
        c = L.cpk_xv2()
    except FileNotFoundError as e:
        print("SKIP", e)
        return 0
    assert len(c.toc) > 100000, len(c.toc)
    # EMD + ESK
    subs = xm.leer_emd(c.get("data/chara/GOK/GOK_000_Bust.emd"))
    assert {s.name for s in subs} >= {"skin", "dougi_syatu"}, [s.name for s in subs]
    assert all(len(s.tris) and s.tris.max() < len(s.pos) for s in subs)
    esk = xm.Esk(c.get("data/chara/GOK/GOK_000.esk"))
    assert len(esk.names) == 122 and "b_C_Base" in esk.index
    # pesos suman 1 y los huesos del vertice existen en el esqueleto
    for s in subs:
        assert np.allclose(s.weights.sum(1), 1, atol=1e-2)
        assert set(s.bone_names) <= set(esk.names), set(s.bone_names) - set(esk.names)
    # EMM
    m = L.leer_emm(c.get("data/chara/GOK/GOK_000_Bust.emm"))
    assert [x["nombre"] for x in m][:1] == ["dougi_syatu"] and m[0]["shader"].startswith("TOON") and len(m[0]["params"]) == 21
    assert "MatCol0R" in m[0]["params"]
    # dye
    d = L.dye_filas(c.get("data/chara/GOK/GOK_000_Bust.dyt.emb"))
    assert d and d[0][1] is not None
    # BCS
    b = L.leer_bcs(c.get("data/chara/HUM/HUM.bcs"))
    assert b["version"] == "XV2" and len(b["partsets"]) == 1881 and len(b["colores"]) == 25 and len(b["cuerpos"]) == 12
    ps = next(p for p in b["partsets"] if p and "Hair" in p)
    assert ps["Hair"]["emd"].startswith("HUM_") or ps["Hair"]["emd"] == "", ps["Hair"]
    g = L.leer_bcs(c.get("data/chara/GOK/GOK.bcs"))
    assert g["partsets"] and g["colores"] is not None
    import xv2_a_b3 as X
    assert X.renombrar("b_R_Arm2") == "rarm2" and X.renombrar("b_C_Base") == "waist" and X.renombrar("h_R_Thumb1") == "h_R_Thumb1"
    import sdbh_model
    X.S = sdbh_model
    assert X.pow2(np.zeros((512, 1024, 4), np.uint8), 256).shape[:2] == (128, 256)
    print("OK test_xv2 (EMD, ESK, EMM, dye, BCS sobre ficheros reales de XV2)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

