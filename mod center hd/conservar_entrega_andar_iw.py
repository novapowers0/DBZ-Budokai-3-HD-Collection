#!/usr/bin/env python3
"""Conserva fuentes e informe de solANDAR en dos destinos, sin sobrescribir."""
import hashlib
import shutil
from datetime import datetime
from pathlib import Path


def main():
    raiz = Path(__file__).resolve().parent.parent
    nombres = ["mod center hd/" + x for x in (
        "tactics_manifest.py", "tactics_manifest.toml", "tactics_andar_iw.py",
        "cac/vista_tactics_iw.py", "prueba_andar_iw.py", "preparar_prueba_andar_iw.py",
        "preparar_bateria_andar_iw.py", "respaldar_andar_iw.py",
        "conservar_entrega_andar_iw.py")]
    nombres.append("studies 5/ENTREGAS/R16/R16_solANDAR.md")
    sello = datetime.now().strftime("entrega_%Y%m%d_%H%M%S_%f")
    for base in (Path("D:/_PARA_BORRAR_CLAUDE/r16_solANDAR"),
                 Path("E:/DBZ3HD_trabajo/r16_andar")):
        destino = base / sello
        assert not destino.exists(), "ya existe el destino"
        for nombre in nombres:
            origen, copia = raiz / nombre, destino / nombre
            copia.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(origen, copia)
            assert hashlib.sha256(origen.read_bytes()).digest() == hashlib.sha256(copia.read_bytes()).digest(), nombre
        print("PASS: %d archivos conservados y verificados: %s" % (len(nombres), destino))


if __name__ == "__main__":
    main()
