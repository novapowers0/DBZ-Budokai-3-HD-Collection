#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""DS20 - Modo Heroe, sagas 5-8 en Dragon Universe con escenas nativas.

Entra al DU del Heroe en una sola saga (DBZ3_TEST_HEROE_SAGA = saga-1) y comprueba que:
  intro/cierre de la saga -> mapa -> marca (auto) -> combate -> victoria (progreso) -> vuelta al mapa.
Capturas de la intro (cierre de la anterior) y del combate de cada saga.

  python tools/pruebas_heroe/saga58_e2e.py --copia ds20 --saga 5
  python tools/pruebas_heroe/saga58_e2e.py --copia ds20 --sagas 5,6,7,8
"""
import argparse
import os
import re
import sys
import time
import tomllib

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.abspath(os.path.join(AQUI, "..", ".."))
sys.path.insert(0, os.path.join(RAIZ, "tools"))
import pruebas as P  # noqa: E402

CAS = 36
SAGA = 5
SAGAS = tomllib.load(open(os.path.join(RAIZ, "res", "heroe", "sagas.toml"), "rb"))


def hasta(j, rx, seg, tecla="Space", paso=2.5):
    t0 = time.time()
    while time.time() - t0 < seg:
        if re.search(rx, j.texto_log()):
            return True
        if j.pantalla() == 10:          # subir de nivel: repartir los puntos (D) y confirmar (Space)
            for _ in range(4):
                j.tecla("D", 450)
            j.tecla("Space", 2500)
        else:
            j.tecla(tecla, int(paso * 1000))
    return bool(re.search(rx, j.texto_log()))


def escenario(j, par):
    j.log(r"escena 100 entrada %d" % CAS, 150)      # intro/cierre de la saga (codigo 100 reutilizado)
    time.sleep(5.0)
    j.captura("ds20_s%d_cierre_intro" % SAGA)
    j.tecla("Space", 2000)
    time.sleep(1.0)
    j.captura("ds20_s%d_cierre_intro_b" % SAGA)
    hasta(j, r"se lanza la escena %d entrada 0" % (5000 + CAS), 150)
    j.captura("ds20_s%d_mapa" % SAGA)
    hasta(j, r"se toca la marca de combate %d|se lanza la escena %d entrada 0" % (5400 + CAS, 5400 + CAS), 120)
    hasta(j, r"jugador 1 = id 63", 120)
    j.captura("ds20_s%d_charla" % SAGA)
    for _ in range(6):
        j.tecla("Space", 1800)
    time.sleep(4)
    j.captura("ds20_s%d_combate" % SAGA)
    hasta(j, r"tag: combate acabado", 240)
    hasta(j, r"dbz3 heroe: DU saga %d combate 0 ganado" % (SAGA - 1), 180)
    j.captura("ds20_s%d_victoria" % SAGA)
    ncomb = len(SAGAS["saga"][SAGA - 1]["combate"])
    if ncomb > 1:   # aun quedan combates: vuelve al mapa de la misma saga
        hasta(j, r"se lanza la escena %d entrada 1" % (5000 + CAS), 180)
    else:           # era el ultimo: cierra la saga -> intro/cierre de la siguiente y su mapa
        hasta(j, r"saga %d: codigo 100 -> contenedor" % (SAGA + 1), 180)
        time.sleep(5.0)
        j.captura("ds20_s%d_cierre_siguiente" % SAGA)
        j.tecla("Space", 2000)
        time.sleep(1.0)
        j.captura("ds20_s%d_cierre_siguiente_b" % SAGA)
        hasta(j, r"saga %d: codigo %d -> contenedor" % (SAGA + 1, 5000 + CAS), 180)
    j.captura("ds20_s%d_vuelta_mapa" % SAGA)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--copia", default="ds20")
    ap.add_argument("--saga", type=int, default=5)
    ap.add_argument("--sagas", default="")
    ap.add_argument("--instantanea", default="")
    ap.add_argument("--visible", action="store_true")
    a = ap.parse_args()
    global SAGA
    sagas = [int(x) for x in a.sagas.split(",") if x.strip()] if a.sagas else [a.saga]
    fallos = []
    for s in sagas:
        SAGA = s
        nombre = "ds20_saga%d" % s
        P.ESCENARIOS[nombre] = ("DS20: Heroe saga %d (DU directo)" % s, lambda p: {}, escenario)
        env = ["DBZ3_TEST_DU_HEROE=%d" % CAS, "DBZ3_TEST_HEROE_SAGA=%d" % (s - 1), "DBZ3_TEST_DU_SAGA=%d" % s,
               "DBZ3_AUTO_TOUCH=1", "DBZ3_TEST_RINGOUT=1"]
        if not a.visible:
            env.append("DBZ3_TEST_OCULTO=1")
        rc = P.correr(a.copia, nombre, env_extra=env,
                      instantanea=(a.instantanea or None), forzar=True)
        log = open(os.path.join(rc["run"], "dbz3.log"), encoding="utf-8", errors="replace").read()
        print("run", rc["run"])
        checks = {
            "intro/cierre (escena 100)": bool(re.search(r"escena 100 entrada %d" % CAS, log)),
            "saga en curso (heroe::Saga)": bool(re.search(r"saga %d: codigo 100 -> contenedor" % s, log)),
            "mapa nativo (5000+casilla)": bool(re.search(r"saga %d: codigo %d -> contenedor" % (s, 5000 + CAS), log)),
            "marca de combate": bool(re.search(r"se toca la marca de combate %d|escena %d entrada 0" % (5400 + CAS, 5400 + CAS), log)),
            "combate del heroe": "jugador 1 = id 63" in log,
            "progreso de la saga": bool(re.search(r"DU saga %d combate 0 ganado" % (s - 1), log)),
            "vuelta al mapa": bool(re.search(r"se lanza la escena %d entrada 1" % (5000 + CAS), log))
                              or bool(re.search(r"saga %d: codigo %d -> contenedor" % (s + 1, 5000 + CAS), log)),
            "sin errores": not re.search(P.ERRORES, log) if hasattr(P, "ERRORES") else True,
        }
        print("saga %d:" % s)
        for k, v in checks.items():
            print("  %-26s %s" % (k, "PASS" if v else "FAIL"))
        if not all(checks.values()):
            fallos.append(s)
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
