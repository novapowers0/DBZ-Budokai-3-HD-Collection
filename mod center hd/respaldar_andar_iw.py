#!/usr/bin/env python3
"""Conserva una copia fechada de las fuentes de solANDAR, sin sobrescribir."""
import shutil
import argparse
import json
import tomllib
from datetime import datetime
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--preparar-kaio", action="store_true")
    a = p.parse_args()
    raiz = Path(__file__).resolve().parent
    destino = Path("D:/_PARA_BORRAR_CLAUDE/r16_solANDAR") / datetime.now().strftime("fuentes_%Y%m%d_%H%M%S")
    assert not destino.exists(), "el respaldo ya existe"
    destino.mkdir(parents=True)
    nombres = ("tactics_manifest.py", "tactics_manifest.toml", "tactics_andar_iw.py",
               "cac/vista_tactics_iw.py", "prueba_andar_iw.py", "preparar_prueba_andar_iw.py",
               "preparar_bateria_andar_iw.py", "respaldar_andar_iw.py")
    for nombre in nombres:
        salida = destino / nombre
        salida.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(raiz / nombre, salida)
    print("PASS: respaldo de %d fuentes: %s" % (len(nombres), destino))
    if a.preparar_kaio:
        import tactics_manifest as tm
        trabajo = Path("E:/DBZ3HD_trabajo/r16_andar")
        previo = trabajo / "tactics_manifest_antes_reconstruido.toml"
        assert not previo.exists(), "el manifiesto previo reconstruido ya existe"
        datos = json.loads((trabajo / "andar51_final/resultado.json").read_text(encoding="utf-8"))["unidades"]
        antiguos = {u["id"]: u["anterior"] for u in datos}
        actuales = tomllib.loads((raiz / "tactics_manifest.toml").read_text(encoding="utf-8"))["unidad"]
        assert len(actuales) == len(antiguos) == 51
        for u in actuales:
            u["clips"]["andar"] = antiguos[u["id"]]
        tm.escribir(actuales, str(previo))
        for modo in ("andar", "carrera"):
            origen = trabajo / (modo + "51_final")
            archivo = trabajo / (modo + "51_antes_correccion_kaio")
            assert not archivo.exists(), "ya existe el archivo de comparativas"
            shutil.move(str(origen), str(archivo))
        print("PASS: comparativas anteriores archivadas; baseline B3 reconstruido desde anterior de cada unidad")


if __name__ == "__main__":
    main()
