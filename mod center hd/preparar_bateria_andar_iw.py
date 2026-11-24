#!/usr/bin/env python3
"""Activa temporalmente la historia que exige la bateria en la copia smoke_f."""
import argparse
import shutil
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("accion", choices=("preparar", "restaurar"))
    a = p.parse_args()
    juego = Path("D:/DBZ3HD/smoke_f")
    marca = juego / "mods/historia_freezer/.disabled"
    respaldo = Path("D:/_PARA_BORRAR_CLAUDE/r16_solANDAR/bateria_historia_freezer.disabled")
    assert not Path("D:/DBZ3HD/_en_uso_smoke_f").exists(), "smoke_f ocupada"
    if a.accion == "preparar":
        assert marca.is_file(), "historia ya activa; no alterar su estado"
        assert not respaldo.exists(), "ya existe el respaldo del marcador"
        respaldo.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(marca), str(respaldo))
        print("PASS: historia_freezer activada temporalmente; marcador respaldado")
    else:
        assert respaldo.is_file(), "falta el marcador respaldado"
        assert not marca.exists(), "hay un marcador nuevo; no sobrescribir"
        shutil.move(str(respaldo), str(marca))
        print("PASS: historia_freezer vuelve a su estado desactivado anterior")


if __name__ == "__main__":
    main()
