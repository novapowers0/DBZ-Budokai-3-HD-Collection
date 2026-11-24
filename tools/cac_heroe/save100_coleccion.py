"""Coteja copias de coleccion.toml antes y después de Desbloquear todo."""

import argparse
import struct
import tomllib
from pathlib import Path


def _iguales(a, b):
    if type(a) is not type(b):
        return False
    if isinstance(a, dict):
        return a.keys() == b.keys() and all(_iguales(a[k], b[k]) for k in a)
    if isinstance(a, list):
        return len(a) == len(b) and all(_iguales(x, y) for x, y in zip(a, b))
    if isinstance(a, float):
        # Distingue -0.0 de 0.0 y admite nan sin confundirlo con otro valor.
        return struct.pack(">d", a) == struct.pack(">d", b)
    return a == b


def verificar(antes: Path, despues: Path, respaldo: Path) -> None:
    original = antes.read_bytes()
    if respaldo.read_bytes() != original:
        raise ValueError("el .bak no coincide byte a byte con el original")
    esperado = tomllib.loads(original.decode("utf-8"))
    esperado["todo"] = True
    with despues.open("rb") as f:
        actual = tomllib.load(f)
    if not _iguales(esperado, actual):
        claves = sorted(k for k in esperado.keys() | actual.keys()
                        if k not in esperado or k not in actual or not _iguales(esperado[k], actual[k]))
        raise ValueError("diferencias inesperadas en: " + ", ".join(claves))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("antes", type=Path, help="TOML original")
    parser.add_argument("despues", type=Path, help="TOML tras Desbloquear todo")
    parser.add_argument("respaldo", type=Path, help="coleccion.toml.bak producido por el lanzador")
    args = parser.parse_args()
    try:
        verificar(args.antes, args.despues, args.respaldo)
    except (ValueError, OSError) as exc:
        parser.exit(1, f"FAIL: {exc}\n")
    print("PASS: solo se establece todo=true; .bak idéntico al original")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
