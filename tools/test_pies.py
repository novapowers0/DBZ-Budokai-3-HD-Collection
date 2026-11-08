"""eANI4: pies.py. Con un nativo como "importado" no cambia nada (idempotente); sin PS2 de B3 se salta."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "awo_tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "mod center hd"))


def test_nativo_no_cambia():
    import afs_pair
    import altura
    import pies
    import roster_build as rb
    try:
        e = next(x for x in rb.DB["ids"] if x["id"] == 0)
        model, anm = afs_pair.ps2(e["models"][0]), afs_pair.ps2(e["anm"][0])
    except Exception:  # noqa: BLE001  (sin la ISO/AFS de B3 PS2)
        return
    tr = pies.reposo(anm)
    new, info = pies.corregir(model, model, tr, tr)
    assert new == model, info
    assert abs(pies.nulo_y(altura.skeleton(new), tr) - pies.nulo_y(altura.skeleton(model), tr)) < 1e-6
    out, cods = pies.var_como_donante(anm, anm)
    assert out == anm and not cods


if __name__ == "__main__":
    test_nativo_no_cambia()
    print("OK")
