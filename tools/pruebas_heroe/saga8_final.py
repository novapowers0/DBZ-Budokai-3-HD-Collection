#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DS20 - Modo Heroe: saga 8 completa (2 combates) y escena final (cierre de la ultima saga).

  python tools/pruebas_heroe/saga8_final.py --copia ds20 --visible

Entra directo al DU del Heroe en la saga 8 (DBZ3_TEST_HEROE_SAGA=7) y juega sus dos combates (auto-touch 2);
comprueba que el segundo deja el progreso (DU saga 7 combate 1) y lanza el final (escena 1 entrada 39).
"""
import argparse
import os
import re
import sys
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.abspath(os.path.join(AQUI, "..", ".."))
sys.path.insert(0, os.path.join(RAIZ, "tools"))
import pruebas as P  # noqa: E402

CAS = 36


def hasta(j, rx, seg, tecla="Space", paso=2.5):
    t0 = time.time()
    while time.time() - t0 < seg:
        if re.search(rx, j.texto_log()):
            return True
        if j.pantalla() == 10:   # subir de nivel: repartir los puntos y confirmar
            for _ in range(4):
                j.tecla("D", 450)
            j.tecla("Space", 2500)
        else:
            j.tecla(tecla, int(paso * 1000))
    return bool(re.search(rx, j.texto_log()))


def escenario(j, par):
    j.log(r"escena 100 entrada %d" % CAS, 150)
    time.sleep(5.0)
    j.captura("ds20_s8_final_cierre_7")
    hasta(j, r"se lanza la escena %d entrada 0" % (5000 + CAS), 150)
    j.captura("ds20_s8_final_mapa")
    # combate 0 (primero de la saga 8)
    hasta(j, r"se toca la marca de combate %d|se lanza la escena %d entrada 0" % (5400 + CAS, 5400 + CAS), 120)
    hasta(j, r"jugador 1 = id 63", 120)
    for _ in range(6):
        j.tecla("Space", 1800)
    time.sleep(4)
    j.captura("ds20_s8_final_combate_0")
    hasta(j, r"tag: combate acabado", 240)
    hasta(j, r"dbz3 heroe: DU saga 7 combate 0 ganado", 180)
    # vuelve al mapa: el auto-touch 2 toca la marca del segundo combate
    hasta(j, r"se toca la marca de combate %d entrada 2|se lanza la escena %d entrada 2" % (5400 + CAS, 5400 + CAS), 240)
    hasta(j, r"jugador 1 = id 63", 120)
    for _ in range(6):
        j.tecla("Space", 1800)
    time.sleep(4)
    j.captura("ds20_s8_final_combate_1")
    hasta(j, r"tag: combate acabado", 240)
    hasta(j, r"dbz3 heroe: DU saga 7 combate 1 ganado", 240)
    j.captura("ds20_s8_final_victoria")
    hasta(j, r"escena 1 entrada 39|se lanza la escena 1 entrada 39", 240)
    time.sleep(3)
    j.captura("ds20_s8_final_escena")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--copia", default="ds20")
    ap.add_argument("--instantanea", default="")
    ap.add_argument("--visible", action="store_true")
    a = ap.parse_args()
    P.ESCENARIOS["ds20_saga8_final"] = ("DS20: Heroe saga 8 completa y final", lambda p: {}, escenario)
    env = ["DBZ3_TEST_DU_HEROE=%d" % CAS, "DBZ3_TEST_HEROE_SAGA=7", "DBZ3_TEST_DU_SAGA=8",
           "DBZ3_AUTO_TOUCH=2", "DBZ3_TEST_RINGOUT=1"]
    if not a.visible:
        env.append("DBZ3_TEST_OCULTO=1")
    rc = P.correr(a.copia, "ds20_saga8_final", env_extra=env,
                  instantanea=(a.instantanea or None), forzar=True)
    log = open(os.path.join(rc["run"], "dbz3.log"), encoding="utf-8", errors="replace").read()
    print("run", rc["run"])
    checks = {
        "intro/cierre saga 7": bool(re.search(r"saga 8: codigo 100 -> contenedor", log)),
        "combate 0 ganado": bool(re.search(r"DU saga 7 combate 0 ganado", log)),
        "combate 1 ganado": bool(re.search(r"DU saga 7 combate 1 ganado", log)),
        "final (escena 1 entrada 39)": "escena 1 entrada 39" in log,
        "sin errores": not re.search(P.ERRORES, log),
    }
    for k, v in checks.items():
        print("  %-30s %s" % (k, "PASS" if v else "FAIL"))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
