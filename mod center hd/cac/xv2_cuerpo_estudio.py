#!/usr/bin/env python3
"""Auditoria local de brazos CaC: lee XV2, conserva solo medidas en --trabajo.

No cambia Steam, no instala mods ni incluye mallas/texturas en el informe.
"""
import argparse
import json
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
import xeno_bcs as B
import sdbh_model as S


def pesos(sm):
    out = {}
    for i, name in enumerate(sm.bone_names):
        w = np.where(sm.bones == i, sm.weights, 0).sum(1)
        if w.max(initial=0) > 1e-5:
            out[name] = dict(vertices=int((w > .001).sum()), masa=round(float(w.sum()), 3), max=round(float(w.max()), 4))
    return out


def medir(c, raza, plantilla):
    raw = C.xm.Esk(c.get("data/chara/%s/%s_000.esk" % (raza, raza)))
    bcs = B.leer_bcs(c.get("data/chara/%s/%s.bcs" % (raza, raza)))
    sel = dict(C.DEFECTO, raza=raza, bust=300, pants=300, boots=300, rist=300, cara=1, pelo=1, ojos=1)
    esk, piezas, _ = C.fuente_cac(c, sel, lod=0)
    tpl = S.Template(plantilla)
    escala = P.escala_xv2(tpl)
    suela = min(float(q["sm"].pos[:, 1].min()) for q in piezas if q["part"] in ("Boots", "Pants") and not q["attachment"])
    prop = P.esqueleto(tpl, esk, X.Retarget(esk, tpl).direct, escala, suela, C.GUARDIA)
    rig = X.Retarget(esk, tpl, escala)
    helpers = []
    for i, name in enumerate(esk.names):
        if any(s in name for s in ("Shoulder", "Arm", "Elbow", "Hand")) and name.startswith(("b_", "x_")):
            helpers.append(dict(nombre=name, padre=esk.names[esk.parent[i]],
                                pivote=np.round(esk.world[i], 4).tolist(),
                                bind_original=np.round(raw.world[i], 4).tolist(),
                                destino=tpl.labels[rig.target[name]], ancla=rig.anchor[name]))
    subs = []
    slots = {b: s for s, b in enumerate(tpl.palette) if s and b < tpl.nb}
    for q in piezas:
        if q["part"] not in ("Bust", "Rist"):
            continue
        sm = q["sm"]
        helper = np.array([any(s in n for s in ("Arm", "Elbow", "Shoulder")) for n in sm.bone_names])
        mask = (helper[sm.bones] & (sm.weights > .001)).any(1)
        bs, ws, dr = X.remap_weights(rig, sm, slots, q["part"] == "Bust")
        hand = np.array([n.startswith("h_") or n in ("b_L_Hand", "b_R_Hand") for n in sm.bone_names])
        forearm = ((~hand[sm.bones]) & (sm.weights > 1e-5)).any(1)
        th = (~forearm[sm.tris]).all(1)
        border = np.intersect1d(np.unique(sm.tris[th]), np.unique(sm.tris[~th]))
        dedos = np.array([n.startswith("h_") for n in sm.bone_names])
        wf = (np.where(dedos[sm.bones], sm.weights, 0)).sum(1)
        subs.append(dict(parte=q["part"], nombre=sm.name, fuente=q["path"], vertices=len(sm.pos),
                         triangulos=len(sm.tris), pesos=pesos(sm),
                         descarte=dict(media=float(dr.mean()), max=float(dr.max()), mayores_25=int((dr>.25).sum())),
                         corte=dict(tris_mano=int(th.sum()), tris_pielados=int((~th).sum()), borde=len(border), dedos_en_borde=float(wf[border].max(initial=0))),
                         brazo_fuera_de_mano=int(mask.sum()),
                         rigida_por_conversor=q["part"] == "Rist" and any(n.startswith("h_") for n in sm.bone_names)))
    # Costuras reales coincidentes: no se supone que el nombre Rist signifique solo dedos.
    costuras = []
    for a in (q for q in piezas if q["part"] == "Bust"):
        for b in (q for q in piezas if q["part"] == "Rist"):
            d, ix = cKDTree(a["sm"].pos).query(b["sm"].pos)
            ids = np.flatnonzero(d < 1e-3)
            if not len(ids):
                continue
            ap, bp = rig.geometry(a["sm"]).pos, rig.geometry(b["sm"]).pos
            costuras.append(dict(bust=a["sm"].name, rist=b["sm"].name, vertices=len(ids),
                                 error_bind_max=float(d[ids].max()),
                                 error_rebind_max=float(np.linalg.norm(ap[ix[ids]] - bp[ids], axis=1).max()),
                                 mano_rigida=any(n.startswith("h_") for n in b["sm"].bone_names)))
    conjuntos = []
    for ps in bcs["part_sets"]:
        pp = [p for p in ps["parts"] if p and p["type"] in ("Bust", "Rist") and p["model"] in (0, 300)]
        if pp:
            conjuntos.append(dict(id=ps["id"], partes=[{k: p[k] for k in ("type", "model", "flags", "hide_flags", "hide_materials")} for p in pp]))
    return dict(raza=raza, huesos=len(esk.names), cuerpos=bcs["bodies"],
                paleta=[dict(slot=i, nombre=tpl.labels[b], padre=tpl.labels[tpl.parent[b]]) for i, b in enumerate(tpl.palette) if i and b < tpl.nb],
                escala=escala, proporciones=prop, esqueleto=helpers, submallas=subs,
                costuras=costuras, conjuntos=conjuntos)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--trabajo", required=True, type=Path)
    ap.add_argument("--razas", nargs="+", default=["HUM", "MAM", "FRI"])
    a = ap.parse_args()
    if not a.trabajo.parent.is_dir():
        ap.error("falta el directorio padre de trabajo")
    a.trabajo.mkdir(exist_ok=True)
    import os
    os.environ["DBZ3_CAC_CACHE"] = str(a.trabajo / "cache")
    pl = F.plantilla_donante(0, None, str(a.trabajo / "plantilla.bin"))
    c = C.L.cpk_xv2()
    rows = [medir(c, raza, pl) for raza in a.razas]
    dest = a.trabajo / "estudio.json"
    if dest.exists():
        raise FileExistsError(dest)
    dest.write_text(json.dumps(rows, ensure_ascii=False, indent=1), encoding="utf-8")
    for r in rows:
        print(r["raza"], "huesos", r["huesos"], "cuerpos", len(r["cuerpos"]))
        for s in r["submallas"]:
            print(" ", s["parte"], s["nombre"], "verts", s["vertices"], "brazo", s["brazo_fuera_de_mano"], "RIGIDA", s["rigida_por_conversor"])
        print(" COSTURAS", r["costuras"])
        for s in r["submallas"]:
            print(" CORTE/DESCARTE", s["nombre"], s["corte"], s["descarte"])
        print(" ROT", [(tplname, r["proporciones"]["factores"].get(tplname)) for tplname in ("GOK_LARM1", "GOK_LARM2")])
        if r["raza"] == "HUM":
            print(" PALETA", r["paleta"])
    print(dest)


if __name__ == "__main__":
    main()
