#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Prueba offline de piezas B3: vista/combate, sin depender de piezas UT del jugador."""
import json
import os
import sys
from pathlib import Path

CAC = Path(__file__).resolve().parents[1]
ROOT = CAC.parents[1]
sys.path.insert(0, str(CAC))
sys.path.insert(0, str(CAC.parent))
import roster_build as rb  # noqa: E402
import model_render as mr  # noqa: E402
import vista_heroe as vh  # noqa: E402


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r"D:\tmp\cac_base\luna6")
    out.mkdir(parents=True, exist_ok=True)
    work = out / "afs_work"
    work.mkdir(exist_ok=True)
    us = ROOT / "us"
    afs = rb.Afs(str(us / "data_cmn.afs"), str(work))
    vh.buscar_pelo = lambda *_args, **_kwargs: None  # simula juego sin .bin de pelo UT del jugador
    vh.tira(mr.Model(afs.entry(264)), 256, 1).save(out / "base_goku_264.png")

    casos = (
        ("torso", 416, {"b3_torso": 416}),
        ("piernas", 298, {"b3_piernas": 298}),
        ("guantes", 383, {"b3_guantes": 383}),
        ("botas", 264, {"b3_botas": 264}),
        ("combinado", 416, {"b3_torso": 416, "b3_piernas": 298, "b3_guantes": 128, "b3_botas": 264}),
        ("combinado_tope", 416, {"b3_torso": 416, "b3_piernas": 298, "b3_guantes": 383, "b3_botas": 264}),
    )
    resultados = []
    for nombre, _fid, piezas in casos:
        zonas_elegidas = [key.removeprefix("b3_") for key in piezas]
        ficha = {"cuerpo": 0, "traje": 0, "partes": [(35, 20, 12), (210, 10, 0), (120, 0, 0), (25, 15, 5)],
                 "pelo": 1, "c1": (25, 0, 0), "c2": (0, 0, 0), "logo": 0, "scouter": 0, **piezas}
        # Malla que usa la vista previa del menú, incluidas las piezas B3 escogidas.
        preview_path = out / (nombre + "_vista.malla")
        summary_view = vh.ficha_malla(mr.Model(afs.entry(264)), ficha, str(out), str(preview_path), afs)
        if not preview_path.is_file() or not summary_view.get("piezas_b3"):
            raise AssertionError("vista previa sin salida/resumen: %s" % nombre)

        # Captura offline sencilla del mismo #AMB injertado, para revisar que la geometría no quedó vacía.
        data_view, summary_view_b3 = vh._aplicar_piezas_b3(afs.entry(264), ficha, str(out), afs, tintar=False)
        (out / (nombre + "_b3_only.bin")).write_bytes(data_view)
        if nombre == "combinado_tope":
            fallos = [z for z in zonas_elegidas if isinstance(summary_view_b3.get(z), str)]
            if fallos != ["guantes"] or not all(isinstance(summary_view_b3.get(z), dict) for z in zonas_elegidas if z not in fallos):
                raise AssertionError("fallback de vista inesperado: %s %s" % (nombre, summary_view_b3))
        elif any(not isinstance(summary_view_b3.get(z), dict) for z in zonas_elegidas):
            raise AssertionError("el injerto de vista volvió a Base: %s %s" % (nombre, summary_view_b3))
        vh.tira(mr.Model(data_view), 256, 1).save(out / (nombre + "_vista.png"))
        # La rutina de combate devuelve el bin que se instalaría en el mod; se vuelve a leer para validar el AWO.
        data_fight, summary_fight = vh.combate_bin(afs.entry(264), ficha, str(out), afs)
        model_fight = mr.Model(data_fight)
        fight_parts = summary_fight.get("piezas_b3", {})
        if nombre == "combinado_tope":
            fallos = [z for z in zonas_elegidas if isinstance(fight_parts.get(z), str)]
            if fallos != ["guantes"] or not all(isinstance(fight_parts.get(z), dict) for z in zonas_elegidas if z not in fallos):
                raise AssertionError("fallback de combate inesperado: %s %s" % (nombre, fight_parts))
        elif any(not isinstance(fight_parts.get(z), dict) for z in zonas_elegidas):
            raise AssertionError("combate con piezas en Base: %s %s" % (nombre, fight_parts))
        if not len(model_fight.tris):
            raise AssertionError("combate sin geometría/resumen: %s" % nombre)
        (out / (nombre + "_combate.bin")).write_bytes(data_fight)
        results = {"caso": nombre, "vista": summary_view_b3, "combate": fight_parts,
                   "triangulos_combate": len(model_fight.tris)}
        resultados.append(results)
        print("PASS", json.dumps(results, ensure_ascii=False))

    (out / "resumen.json").write_text(json.dumps(resultados, ensure_ascii=False, indent=2), encoding="utf-8")
    print("test_piezas_b3_offline: PASS (%d combinaciones; buscar_pelo desactivado)" % len(resultados))
    return 0


if __name__ == "__main__":
    sys.exit(main())
