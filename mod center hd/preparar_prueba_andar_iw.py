#!/usr/bin/env python3
"""Fixture SOLO para smoke_f mientras el runtime sigue conectado al horno B3.

El arnes recopia el manifiesto antes del arranque. Para probar TKB2 precocinados
sin editar ese arnes ni el runtime, instalar reserva 24 horas en la fecha de las
caches. verificar comprueba los 51 hashes y que el proceso cargo estas caches.
cerrar devuelve las fechas a la hora actual. Nunca se usa para distribuir.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
JUEGO = Path("D:/DBZ3HD/smoke_f")
RESULTADOS = Path("E:/DBZ3HD_trabajo/r16_andar/andar51_final/resultado.json")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("accion", choices=("instalar", "verificar", "cerrar", "resumen", "evidencia"))
    p.add_argument("--run", type=Path)
    a = p.parse_args()
    datos = json.loads(RESULTADOS.read_text(encoding="utf-8"))["unidades"]
    assert len(datos) == 51, "faltan comparativas de andar"
    if a.accion == "evidencia":
        if not a.run:
            p.error("evidencia requiere --run de survivors_partida")
        from PIL import Image
        destino = RESULTADOS.parent.parent / "pruebas_finales" / a.run.name
        destino.mkdir(parents=True, exist_ok=True)
        for nombre in ("dbz3.log", "escenario.toml", "pantallas.txt"):
            shutil.copy2(a.run / nombre, destino / nombre)
        with Image.open(a.run / "survivors_3_mitad.png") as im:
            salida = destino / "vegeta_andar_05.png"
            assert not salida.exists(), "evidencia ya guardada; no se sobrescribe"
            im.resize((im.width // 2, im.height // 2)).save(salida)
            print("PASS: captura Vegeta al 0,5: %dx%d -> %dx%d: %s" % (
                im.width, im.height, im.width // 2, im.height // 2, salida))
        return
    if a.accion == "resumen":
        for modo in ("andar", "carrera"):
            ruta = RESULTADOS.parent.parent / (modo + "51_final") / "resultado.json"
            u = json.loads(ruta.read_text(encoding="utf-8"))["unidades"]
            assert len(u) == 51 and all(len(x["siete_clips_intactos"]) == 7 for x in u)
            peor = max(u, key=lambda x: x["error_apoyo_relativo"])
            print("PASS: %s: %d unidades; %d poses; 7 clips intactos; apoyo max %.3f%% (%s)" % (
                modo, len(u), 8 * len(u), 100 * peor["error_apoyo_relativo"], peor["id"]))
        return
    if a.accion == "instalar":
        subprocess.run([sys.executable, str(HERE / "cac/vista_tactics_iw.py"),
                        "--juego", str(JUEGO), "--us", str(HERE.parent / "us"),
                        "--todas", "--exigir-iw"], check=True)
    for u in datos:
        ruta = JUEGO / "ingame_ui/nave_vista" / ("tk_%d.tk" % u["fid"])
        assert hashlib.sha256(ruta.read_bytes()).hexdigest() == u["tk_sha256"], "cache distinta: " + u["id"]
        if a.accion in ("instalar", "cerrar"):
            fecha = time.time() + (86400 if a.accion == "instalar" else 0)
            os.utime(ruta, (fecha, fecha))
    print("PASS: 51 caches IW con hashes identicos a las comparativas (%s)" % a.accion)
    if a.run:
        log = (a.run / "dbz3.log").read_text(encoding="utf-8", errors="replace")
        fids = {u["fid"] for u in datos}
        rehechos = {int(x) for x in re.findall(r"clips de B3 fid (\d+): cache anterior", log)}
        assert not (fids & rehechos), "el runtime invalido caches IW: %s" % (fids & rehechos)
        cargados = {int(x) for x in re.findall(r"clips de B3 fid (\d+) cargados", log)}
        assert cargados & fids, "el proceso no cargo ninguna cache IW verificada"
        print("PASS: proceso cargo %d caches IW verificadas; ninguna invalidada" % len(cargados & fids))


if __name__ == "__main__":
    main()
