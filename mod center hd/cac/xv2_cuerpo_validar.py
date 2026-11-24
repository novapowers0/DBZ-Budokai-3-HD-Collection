#!/usr/bin/env python3
"""Prueba local con archivos del jugador: costuras en todas las manos y bin B3."""
import argparse
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "awo_tools"), str(ROOT / "mod center hd/xv2"), str(Path(__file__).parent)]
import numpy as np
from scipy.spatial import cKDTree
import xv2_cac_b3 as C
import xv2_fuente as F
import xv2_proporcion as P
import xeno_a_b3 as X
import xeno_cuerpo as B
import sdbh_model as S
from xeno_gestos import HAND_NUMBERS, native_lists


def medir(c, raza, plantilla, trabajo):
    sel = dict(C.DEFECTO, raza=raza, bust=300, pants=300, boots=300, rist=300, cara=1, pelo=1, ojos=1)
    esk, piezas, _ = C.fuente_cac(c, sel, lod=0)
    tpl = S.Template(plantilla)
    tpl.gesture_lists = native_lists(Path(plantilla).read_bytes(), tpl)
    old_palette = list(tpl.palette)
    s = P.escala_xv2(tpl)
    suela = min(float(q["sm"].pos[:, 1].min()) for q in piezas if q["part"] in ("Boots", "Pants") and not q["attachment"])
    P.esqueleto(tpl, esk, X.Retarget(esk, tpl).direct, s, suela, C.GUARDIA)
    rig = X.Retarget(esk, tpl, s)
    prepared = B.preparar_brazos(rig, piezas)
    assert tpl.palette[:len(old_palette)] == old_palette
    assert sum(len(q["sm"].tris) for q in piezas) == sum(len(q["sm"].tris) for q in prepared)
    assert len(tpl.palette) <= 256
    slots = {b: i for i, b in enumerate(tpl.palette) if i and b < tpl.nb}
    seams, losses = [], []
    for p in (q for q in prepared if q.get("cuerpo_proximal")):
        sm = p["sm"]
        body = rig.geometry(sm)
        bs, ws, _ = X.remap_weights(rig, body, slots)
        for h in (q for q in prepared if q["part"] == "Rist" and not q.get("cuerpo_proximal") and q["sm"].name == sm.name):
            hand = h["sm"]
            d, ix = cKDTree(sm.pos).query(hand.pos)
            ids = np.flatnonzero(d < 1e-6)
            if not len(ids):
                continue
            for side in "LR":
                own = ids[(hand.pos[ids, 0] >= 0) if side == "L" else (hand.pos[ids, 0] < 0)]
                if not len(own):
                    continue
                wrist = tpl.bone(side + "HANDROT")
                np.testing.assert_array_equal(bs[ix[own]], np.full(len(own), wrist))
                np.testing.assert_allclose(ws[ix[own]], 1, atol=1e-7)
                error = 0.
                for number in HAND_NUMBERS:
                    posed = rig.geometry(X.pose_hand(esk, hand, side, number))
                    err = np.linalg.norm(body.pos[ix[own]] - posed.pos[own], axis=1).max()
                    error = max(error, float(err))
                assert error < 1e-6, (raza, sm.name, side, error)
                seams.append(dict(material=sm.name, lado=side, vertices=len(own), gestos=len(HAND_NUMBERS), error_max=error))
    for p in (q for q in prepared if q["part"] == "Bust"):
        _, _, dr = X.remap_weights(rig, rig.geometry(p["sm"]), slots, True)
        losses.append(dict(material=p["sm"].name, media=float(dr.mean()), max=float(dr.max()), mayores_25=int((dr > .25).sum())))
    assert seams, "no se ha ejercitado ninguna costura de guante"
    # El camino real completo, incluida la conversion de materiales y las capas.
    models = []
    for preview in (True, False):
        dest = trabajo / (raza + ("_previa.bin" if preview else "_combate.bin"))
        if dest.exists():
            raise FileExistsError(dest)
        stats = C.componer_xeno(sel, str(plantilla), str(dest), previa=preview)
        errors = S.verify(str(dest))
        assert not errors, errors
        from awg_vertex_buffer import AwgVertexBuffer
        from skin_slots import SkinSlots
        packed = S.Template(dest)
        decoded = SkinSlots(AwgVertexBuffer.load(str(dest)))
        assert decoded.table == packed.palette, "el lector debe respetar la paleta aditiva sin ordenarla"
        models.append(dict(previa=preview, bytes=dest.stat().st_size, estructura="PASS", cuerpo=stats["cuerpo"],
                           lectura_paleta="PASS",
                           roundtrip=stats["packed_bind_roundtrip_max_error"], pesos=stats["weights"]))
    return dict(raza=raza, costuras=seams, descarte_torso=losses, modelos=models, ok=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--trabajo", type=Path, required=True)
    ap.add_argument("--razas", nargs="+", default=["HUM", "MAM", "FRI"])
    a = ap.parse_args()
    if not a.trabajo.parent.is_dir() or a.trabajo.exists():
        ap.error("--trabajo debe estar ausente, con padre existente")
    a.trabajo.mkdir()
    os.environ["DBZ3_CAC_CACHE"] = str(a.trabajo / "cache")
    pl = F.plantilla_donante(0, None, str(a.trabajo / "plantilla.bin"))
    c = C.L.cpk_xv2()
    rows = []
    for raza in a.razas:
        row = medir(c, raza, pl, a.trabajo)
        rows.append(row)
        print("PASS", raza, "costuras", len(row["costuras"]), "gestos", len(HAND_NUMBERS),
              "error", max(q["error_max"] for q in row["costuras"]), "previa+combate B3 validos", flush=True)
        (a.trabajo / "validacion.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
