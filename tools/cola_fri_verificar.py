"""Comprobaciones offline con los datos locales XV2/B3; no abre el juego.

python tools/cola_fri_verificar.py
No sustituye la inspeccion de las tiras de combate.
"""
import os
import sys
from pathlib import Path
from types import SimpleNamespace

import numpy as np

R = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(R / "mod center hd" / "xv2"), str(R / "awo_tools")]

import sdbh_model as SM
import xeno_a_b3 as XA
import xeno_bcs as BCS
import xv2_a_b3 as X
import xv2_cac_b3 as XC
import xv2_fuente as F


def main():
    os.environ["DBZ3_XV2_COLA_ANIMADA"] = "1"
    c = XC.L.cpk_xv2()
    sel = dict(raza="FRI", bust=300, pants=300, boots=300, rist=300,
               cara=1, pelo=1, ojos=1)
    esk, piezas, _ = XC.fuente_cac(c, sel)
    tpl = SM.Template(F.plantilla_donante(27, str(R / "us")))
    padres = list(tpl.parent)
    rotaciones = [m[:3, :3].copy() for m in tpl.local]
    rig = XA.Retarget(esk, tpl)
    informe = rig.report["tail"]
    assert informe["enabled"] and len(rig.tail_targets) == 10
    assert informe["target_indices"] == [48, 49, 50, 51, 52]
    assert informe["bind_error"] < 1e-5, informe
    assert list(tpl.parent) == padres
    assert all(np.array_equal(a, b[:3, :3]) for a, b in zip(rotaciones, tpl.local))
    print("PASS COLA FRI: 10->5, bind <1e-5, indices/padres/ejes nativos conservados", flush=True)

    nombres = ["X_T_TAIL%d" % n for n in range(1, 11)]
    sm = SimpleNamespace(pos=np.zeros((10, 3)), bone_names=nombres,
                         bones=np.arange(10)[:, None], weights=np.ones((10, 1)))
    bs, ws, perdidos = XA.remap_weights(rig, sm, set(tpl.palette))
    assert bs[-1] == 52 and ws[-1] == 1.0
    assert np.isfinite(ws).all() and not perdidos.any()
    assert all(48 <= b <= 52 for b in bs)
    n = 0
    for p in piezas:
        sm = p["sm"]
        if not any("TAIL" in name for name in sm.bone_names):
            continue
        bs, ws, _ = XA.remap_weights(rig, sm, set(tpl.palette), p["part"] == "Pants")
        assert np.isfinite(ws).all() and ((ws >= 0) & (ws <= 1)).all()
        n += int(np.isin(bs, informe["target_indices"]).sum())
    assert n > 0
    print(f"PASS COLA FRI: extremo TAIL10 sin peso cero/division; {n} vertices en cadena nativa", flush=True)

    # Un caso REAL de hide_materials: Pants 304 lleva bit Bust=64.
    # Su material oculto incluye seis vertices influidos por la cola: se deben
    # excluir sin eliminar las otras dos submallas visibles de la cola.
    bcs = BCS.leer_bcs(c.get(X._ci(c, "data/chara/FRI/FRI.bcs")))
    p304 = next(p for ps in bcs["part_sets"] for p in ps["parts"]
                if p and p["type"] == "Pants" and p["model"] == 304)
    assert p304["hide_materials"] & 64
    _, piezas304, _ = XC.fuente_cac(c, dict(sel, pants=304))
    nombres304 = {p["sm"].name.lower() for p in piezas304 if p["part"] == "Pants"}
    assert "hide_from_bust_skin_c_pants" not in nombres304
    assert {"skin_a_tail", "skin_c_tail"} <= nombres304
    assert not any(p and p["hide_flags"] & 128 for ps in bcs["part_sets"] for p in ps["parts"])
    print("PASS COLA FRI: BCS Pants 304 excluye material oculto y conserva cola visible", flush=True)
    print("LIMITACION: este BCS FRI no declara hide_flags Pants=128; ocultacion TOTAL de cola no probada", flush=True)


if __name__ == "__main__":
    main()
