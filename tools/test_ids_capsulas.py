"""Prueba del reparto estable de IDs de capsula de roster_build.Capsules (peticion d), sin AFS.
    python tools/test_ids_capsulas.py"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mod center hd"))
import roster_build as rb  # noqa: E402


def capsulas(fijas):
    c = object.__new__(rb.Capsules)
    c.next_id, c.usadas, c._claves = 596, set(), set()
    c.fijas = dict(fijas)
    return c


def montar(fijas, mods):
    c = capsulas(fijas)
    ids = {(m, n): c._id_fija(m, "especial", {"nombre": n}) for m, nombres in mods for n in nombres}
    return ids, c.fijas


# primera vez: mismo reparto que antes (seguidas desde 596, en orden de montaje)
ids, fijas = montar({}, [("Broly", ["Eraser", "Gigantic"]), ("Cooler", ["Supernova"])])
assert ids == {("Broly", "Eraser"): 596, ("Broly", "Gigantic"): 597, ("Cooler", "Supernova"): 598}, ids
# llega un mod que va ANTES por orden alfabetico: los de siempre conservan su ID, el nuevo toma uno libre
ids, fijas = montar(fijas, [("Abo", ["Doble"]), ("Broly", ["Eraser", "Gigantic"]), ("Cooler", ["Supernova"])])
assert ids[("Broly", "Eraser")] == 596 and ids[("Cooler", "Supernova")] == 598 and ids[("Abo", "Doble")] == 599, ids
# Broly desactivado: sus IDs quedan reservados; uno nuevo no se los quita
ids, fijas = montar(fijas, [("Abo", ["Doble"]), ("Cooler", ["Supernova"]), ("Dabura", ["Lanza"])])
assert ids[("Dabura", "Lanza")] == 600, ids
# vuelve Broly: las mismas de antes
ids, fijas = montar(fijas, [("Broly", ["Eraser", "Gigantic"])])
assert ids[("Broly", "Eraser")] == 596 and ids[("Broly", "Gigantic")] == 597, ids
# dos capsulas con el mismo nombre en un mod: claves distintas
ids, _ = montar({}, [("X", ["Ki", "Ki"])])
c = capsulas({})
assert [c._id_fija("X", "especial", {"nombre": "Ki"}) for _ in range(2)] == [596, 597]
print("OK: capsulas con ID estable")
