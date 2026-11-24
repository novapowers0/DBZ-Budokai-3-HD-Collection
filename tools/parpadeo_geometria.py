"""Auditoria offline de capas/duplicados XV2; lee la copia local del jugador."""
import argparse
from collections import Counter
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
for rel in ("awo_tools", "mod center hd/xv2", "mod center hd/cac"):
    sys.path.insert(0, str(ROOT / rel))


def auditar(piezas, nombre):
    globales = {}
    cruces = Counter()
    print(nombre, flush=True)
    from amo2awo import stripify, strip_tri
    for numero, p in enumerate(piezas):
        if p.get("variant"):
            continue
        sm = p["sm"]
        vistos, duplicados, opuestos = {}, 0, 0
        for tri in sm.tris:
            puntos = [tuple(x) for x in np.round(sm.pos[tri], 5)]
            key = tuple(sorted(puntos))
            oriented = min(tuple(puntos[i:] + puntos[:i]) for i in range(3))
            if key in vistos:
                duplicados += 1
                opuestos += oriented != vistos[key]
            vistos[key] = oriented
            # Dos submallas pueden compartir nombre/material (pantalon, guantes).
            # Su identidad no es el nombre: tambien se comparan entre ellas.
            origen = (numero, p["part"], sm.name)
            if key in globales and globales[key] != origen:
                cruces[(globales[key], origen)] += 1
            globales[key] = origen
        print(p["part"], sm.name, "rigid", p.get("rigid"), "verts", len(sm.pos),
              "tris", len(sm.tris), "duplicados", duplicados, "opuestos", opuestos, flush=True)
        if p.get("rigid") is None:
            canon = lambda t: min(tuple(t[i:]) + tuple(t[:i]) for i in range(3))
            esperados = Counter(canon(list(t)) for t in sm.tris if len(set(t)) == 3)
            tira = stripify(sm.tris)
            obtenidos = Counter(canon(list(strip_tri(tira, i))) for i in range(len(tira) - 2)
                                if len(set(tira[i:i + 3])) == 3)
            print("  tira: faltan", sum((esperados - obtenidos).values()),
                  "sobran", sum((obtenidos - esperados).values()), flush=True)
    print("coincidencias entre piezas:", dict(cruces), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--raza", default="HUM")
    ap.add_argument("--plantilla", default="D:/DBZ3HD/sol/user_data/dbz3/cac_xv2/plantilla_0.bin")
    a = ap.parse_args()
    import xv2_cac_b3 as C
    import xeno_a_b3 as X
    import sdbh_model as S
    import xv2_proporcion as P
    esk, piezas, _ = C.fuente_cac(C.L.cpk_xv2(), dict(raza=a.raza, bust=300, pants=300, boots=300, rist=300, cara=1, pelo=1, ojos=1))
    tpl = S.Template(a.plantilla)
    s = P.escala_xv2(tpl)
    suela = min(float(q["sm"].pos[:, 1].min()) for q in piezas if q["part"] in ("Boots", "Pants") and not q["attachment"])
    P.esqueleto(tpl, esk, X.Retarget(esk, tpl).direct, s, suela, C.GUARDIA)
    rig = X.Retarget(esk, tpl, s)
    conv, _ = X.prepare_pieces(rig, piezas, 1.0, gestos=False)
    auditar(conv, "ANTES DE SEPARAR CAPAS")
    slots = {b: s for s, b in enumerate(tpl.palette) if s and b < tpl.nb}
    pesos = {id(p): X.remap_weights(rig, p["sm"], slots, p["part"] in ("Bust", "Pants", "Boots")) for p in conv if p["rigid"] is None}
    X.separar_capas(rig, conv, slots, pesos)
    auditar(conv, "DESPUES DE SEPARAR CAPAS")


if __name__ == "__main__":
    main()
