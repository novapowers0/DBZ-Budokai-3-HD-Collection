#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Retira estados estructurales que no tienen aprobación visual perfecta."""
import json
import os
import shutil
import time
from pathlib import Path


HERE = Path(__file__).resolve().parent
CATALOGO = HERE / "piezas_b3_base.json"
RESPALDOS = Path(r"D:\_PARA_BORRAR_CLAUDE")


def main():
    original = CATALOGO.read_bytes()
    data = json.loads(original.decode("utf-8"))
    total = 0
    for zona, piezas in data.items():
        if not isinstance(piezas, list):
            continue
        for pieza in piezas:
            if pieza.get("ok") is True and pieza.get("validacion_visual") != "perfecto":
                pieza["ok"] = False
                nota = "No aprobada en Luna 8b: falta confirmar encaje perfecto en frente, espalda y juego."
                vieja = pieza.get("notas", "")
                if nota not in vieja:
                    pieza["notas"] = (vieja.rstrip(" .;") + "; " + nota) if vieja else nota
                total += 1
    if not total:
        print("sin cambios: no había aprobaciones visuales pendientes")
        return 0
    RESPALDOS.mkdir(parents=True, exist_ok=True)
    backup = RESPALDOS / ("luna8_piezas_b3_base_antes_%s.json" % time.strftime("%Y%m%d_%H%M%S"))
    shutil.copy2(CATALOGO, backup)
    temporal = CATALOGO.with_suffix(".json.tmp")
    temporal.write_text(json.dumps(data, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    os.replace(temporal, CATALOGO)
    print("marcadas no aprobadas: %d; respaldo conservado: %s" % (total, backup))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
