#!/usr/bin/env python3
"""Comprobaciones sin juego de los topes de personajes (roster_build): reparto por paginas y atlas de iconos."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "mod center hd"))
import numpy as np  # noqa: E402
import roster_build as r  # noqa: E402

srcs = [("m%03d" % i, "", {}) for i in range(120)] + [("heroe", "", {"celda": False}), ("equipo", "", {"equipo": [0, 1]})]
ids, mapa, _ = r.plan_ids_paginas(srcs, {"m005": 44, "m100": 10007}, {})
fijos = [n for n, i in ids.items() if i in r.ALL_IDS]
assert len(ids) == len(srcs) and None not in ids.values()
assert len(set(ids.values())) == len(srcs), "IDs repetidos"
assert len(fijos) == r.FIJOS, len(fijos)
assert ids["heroe"] in r.ALL_IDS and ids["equipo"] in r.ALL_IDS, "los que necesitan ID estable van fijos"
assert ids["m005"] == 44 and ids["m100"] == 10007, "quien tiene ID lo conserva"
# estable: repetir con el mapa nuevo da lo mismo
assert r.plan_ids_paginas(srcs, mapa, {})[0] == ids

atlas = []
ic = np.full((84, 84, 4), 255, dtype=np.uint8)
pos = [r.icono_extra(atlas, ic) for _ in range(r.ATLAS_CELDAS ** 2 + 1)]
assert len(atlas) == 2 and pos[-1] == (2, 2, "@a1") and pos[0] == (2, 2, "@a0")
assert all(x + 56 <= r.ATLAS_LOGICO[0] and y + 56 <= r.ATLAS_LOGICO[1] for x, y, _ in pos)
assert (atlas[0]["img"][3, 3] == 255).all(), "el icono se pinta en el atlas"
print("test_slots: OK")
