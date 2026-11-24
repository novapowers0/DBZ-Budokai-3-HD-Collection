#!/usr/bin/env python3
"""Comprobaciones sin juego de los topes de personajes (roster_build): reparto por paginas y atlas de iconos."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "mod center hd"))
import numpy as np  # noqa: E402
import roster_build as r  # noqa: E402

import tempfile  # noqa: E402

TAMANOS = [int(x) for x in sys.argv[1:]] or [120, 300, 500]   # SLOTS_PJ-01: python tools/test_slots.py 120 300 500
for N in TAMANOS:
    srcs = [("m%03d" % i, "", {}) for i in range(N)] + [("heroe", "", {"celda": False}), ("equipo", "", {"equipo": [0, 1]})]
    ids, mapa, _ = r.plan_ids_paginas(srcs, {"m005": 44, "m100": 10007}, {})
    fijos = [n for n, i in ids.items() if i in r.ALL_IDS]
    assert len(ids) == len(srcs) and None not in ids.values()
    assert len(set(ids.values())) == len(srcs), "IDs repetidos (N=%d)" % N
    assert len(fijos) == r.FIJOS, len(fijos)
    assert ids["heroe"] in r.ALL_IDS and ids["equipo"] in r.ALL_IDS, "los que necesitan ID estable van fijos"
    assert ids["m005"] == 44 and (N <= 100 or ids["m100"] == 10007), "quien tiene ID lo conserva"
    assert r.plan_ids_paginas(srcs, mapa, {})[0] == ids      # estable: repetir con el mapa nuevo da lo mismo
    assert max(ids.values()) < r.LOGICO_BASE + len(srcs) + 10, "IDs logicos fuera de rango"

# <= 24 personajes: camino de siempre (todos con ID fijo de la rueda, nada de paginas), estable al repetir
for N in (1, 10, r.MAX_CELDAS):
    srcs = [("m%03d" % i, "", {}) for i in range(N)]
    with tempfile.TemporaryDirectory() as d:
        a = r.plan_ids(d, srcs)
        assert all(i in r.ALL_IDS for i in a[0].values()) and len(set(a[0].values())) == N, "N=%d: no todo en IDs fijos" % N
        assert max(a[0].values(), default=0) < r.LOGICO_BASE, "N=%d: aparecieron IDs de pagina" % N
        assert r.plan_ids(d, srcs)[0] == a[0]

# SLOTS_PJ-06: capsulas sin tope. 500 personajes x 3 capsulas = 1500 IDs nuevos (> 1388 de serie).
import capsulas  # noqa: E402

ids_libres = capsulas.ids_nuevas(True)
assert len(ids_libres) >= 1500, len(ids_libres)
assert len(set(ids_libres)) == len(ids_libres), "IDs de capsula repetidos"
assert ids_libres[:3] == [596, 597, 598]
assert not any(1984 <= i < 2048 for i in ids_libres), "el hueco de las capsulas unicas queda reservado"
assert capsulas.id_nueva_valida(2048, True) and not capsulas.id_nueva_valida(1984, True)
assert not capsulas.id_nueva_valida(1984, False) and not capsulas.id_nueva_valida(2048, False)

c = r.Capsules.__new__(r.Capsules)          # solo el reparto de IDs (sin AFS)
c._claves, c.fijas, c.usadas, c.next_id, c.sin_tope = set(), {}, set(), capsulas.N_NATIVE, True
asig = [c._id_fija("m%03d" % i, "especial", {"nombre": "Capsula %d" % i}) for i in range(1500)]
assert all(x is not None for x in asig), "con capsulas_sin_tope no puede faltar ninguno (500 x 3)"
assert len(set(asig)) == 1500 and max(asig) < capsulas.ID_MAX_SIN_TOPE
assert not any(1984 <= i < 2048 for i in asig)
c2 = r.Capsules.__new__(r.Capsules)         # apagado: los 1388 de siempre y el 1389 avisa (sin IDs libres)
c2._claves, c2.fijas, c2.usadas, c2.next_id, c2.sin_tope = set(), {}, set(), capsulas.N_NATIVE, False
serie = [c2._id_fija("m%03d" % i, "especial", {"nombre": "C%03d" % i}) for i in range(1388)]
assert all(x is not None for x in serie) and len(set(serie)) == 1388
assert c2._id_fija("extra", "especial", {"nombre": "una mas"}) is None

atlas = []
ic = np.full((84, 84, 4), 255, dtype=np.uint8)
pos = [r.icono_extra(atlas, ic) for _ in range(r.ATLAS_CELDAS ** 2 + 1)]
assert len(atlas) == 2 and pos[-1] == (2, 2, "@a1") and pos[0] == (2, 2, "@a0")
assert all(x + 56 <= r.ATLAS_LOGICO[0] and y + 56 <= r.ATLAS_LOGICO[1] for x, y, _ in pos)
assert (atlas[0]["img"][3, 3] == 255).all(), "el icono se pinta en el atlas"
print("test_slots: OK")
