#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""heroe_du_e2e.py - prueba de extremo a extremo del Heroe en el mapa de Dragon Universe (studies 5, linea C3).

Usa tools/pruebas.py como modulo (su Juego: teclas, pantallas, capturas y log) con un escenario propio y adaptativo:
  menu principal -> (entrada directa del Heroe, sin titulo ni rueda de DU) -> intro de la saga -> mapa con el cuerpo propio
  (ID 63) -> marca 1 (se toca sola) -> combate del heroe (ring out del rival) -> pantallas de nivel -> victoria con
  progreso del heroe -> marca 2 en el mapa.
y comprueba en el log cada paso; ademas, que el Heroe NO sale como luchador elegible en la rueda de DU (ni en las demas).

Uso:  python "mod center hd/cac/heroe_du_e2e.py" [--copia s5_c3] [--instantanea freezer_cap2]
La copia necesita mods/heroe_propio (con `celda = false`), mods/_roster y mods/historia_heroe (heroe_sagas_a_du.py --montar).
No toca la carpeta del usuario; las capturas van a D:\\DBZ3HD\\_pruebas\\<fecha>_<copia>_heroe_du/ (se copian a evidencias).
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

RES = []


def ok(nombre, cond, detalle=""):
    RES.append((nombre, bool(cond), detalle))
    print(("  PASS " if cond else "  FAIL ") + nombre + (("  " + detalle) if detalle and not cond else ""), flush=True)
    return bool(cond)


def hasta(j, rx, seg, tecla="Space", paso=2.5):
    """Pulsa `tecla` cada `paso` s hasta que el log case `rx`; en la pantalla de repartir puntos (10) reparte y confirma."""
    t0 = time.time()
    while time.time() - t0 < seg:
        if re.search(rx, j.texto_log()):
            return True
        n_guardar = len(re.findall(r"se lanza la escena 16384 entrada 4", j.texto_log()))
        if n_guardar > getattr(hasta, "guardados", 0):   # C4: cierre de saga -> "Guardar?" nativo: Si (arriba) y A
            hasta.guardados = n_guardar
            time.sleep(2.0)
            j.tecla("W", 600)
            j.tecla("Space", 2500)
            j.tecla("W", 600)
            j.tecla("Space", 2500)
            continue
        if j.pantalla() == 10:                      # subir de nivel: repartir los puntos (D sube el de la fila)
            hasta.n = getattr(hasta, "n", 0) + 1   # C4: el rotulo con el nombre del Heroe (Level Up / Battle Estimation)
            if hasta.n <= 4:
                time.sleep(2.0)
                j.captura("du_heroe_nivel_rotulo_%d" % hasta.n)
            for _ in range(4):
                j.tecla("D", 450)
            j.tecla("Space", 2500)
        else:
            j.tecla(tecla, int(paso * 1000))
    return bool(re.search(rx, j.texto_log()))


CAS = 36
LARGO = False   # --largo: de la saga 1 a la 4 (las 9 primeras luchas + la 1.a de la saga virtual)


def escenario(j, par):
    j.log(r"escena 102 entrada %d" % CAS, 90)                       # intro de la saga (tras la entrada directa)
    j.captura("du_heroe_1_intro")
    hasta(j, r"escena %d entrada 0" % (5100 + CAS), 60)                     # paginas de la intro hasta la llegada al mapa
    j.captura("du_heroe_2_llegada")
    hasta(j, r"escena %d entrada 0" % (5500 + CAS), 120)   # el mapa: el heroe vuela, la marca se toca sola
    j.captura("du_heroe_3_mapa")
    hasta(j, r"escena %d entrada 0" % (5900 + CAS), 60, tecla="Shift")   # C4: la charla antes del combate (retrato del Heroe)
    for i in range(4):
        time.sleep(0.8)
        j.captura("du_heroe_3b_charla_%d" % i)
        j.tecla("Space", 1500)
    hasta(j, r"tag: combate acabado: id 63", 200)             # charla, combate (ring out del rival) y fin
    j.captura("du_heroe_4_combate_fin")
    hasta(j, r"dbz3 heroe: DU saga 0 combate 0", 120)          # resultado, nivel, puntos y escena de victoria
    j.captura("du_heroe_5_victoria")
    hasta(j, r"se toca la marca de combate %d entrada 2" % (5500 + CAS), 150)   # la marca del 2.o combate de la saga
    j.captura("du_heroe_6_marca2")
    if LARGO:   # C4: cruza de la saga 3 a la 4 (codigos virtuales 12000+/12500+/13000+): 9 combates y el 10.o
        hasta(j, r"dbz3 heroe: DU saga 3 combate 0 ganado", 3000)
        j.captura("du_heroe_7_saga4")
    time.sleep(2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--copia", default="s5_c4")
    ap.add_argument("--instantanea", default="freezer_cap2")
    ap.add_argument("--casilla", type=int, default=36)
    ap.add_argument("--largo", action="store_true", help="cruza de la saga 3 a la 4 (sagas virtuales)")
    a = ap.parse_args()
    global CAS, LARGO
    CAS = a.casilla
    LARGO = a.largo
    P.ESCENARIOS["heroe_du"] = ("C3: Heroe en DU de extremo a extremo", lambda p: {}, escenario)
    env = ["DBZ3_LEVELUP_DIAG=1", "DBZ3_TEST_DU_HEROE=%d" % CAS, "DBZ3_AUTO_TOUCH=%s" % ("0" if LARGO else "2"), "DBZ3_TEST_RINGOUT=1"]
    rc = P.correr(a.copia, "heroe_du", env_extra=env, instantanea=None if a.instantanea == "ninguna" else a.instantanea)
    log = open(os.path.join(rc["run"], "dbz3.log"), encoding="utf-8", errors="replace").read()
    print("capturas:", rc["run"])
    ok("escenario terminado sin caida", rc["ok"], rc.get("msg", ""))
    ok("el Heroe NO tiene celda en ninguna rueda (el roster no la crea)", "casillas nuevas en la rueda del select" not in log)
    ok("la rueda de DU solo suma las casillas de las campanas de personaje (Freezer), nunca la del Heroe",
       re.findall(r"rueda de Dragon Universe con casillas de mas ([0-9A-F]+)", log) in ([], ["0000000000400000"]))
    ok("entrada directa (hub::RequestDu)", "Dragon Universe del Heroe: casilla %d" % CAS in log)
    ok("la rueda confirma sola la casilla 36", "la rueda de DU confirma sola la casilla %d" % CAS in log)
    ok("mapa: el personaje se crea en la casilla 36", re.search(r"personaje del mapa al crear \(casilla %d" % CAS, log))
    ok("intro propia (102.36 del mod)", "codigo 102 -> contenedor" in log and "(del mod)" in log)
    ok("marca 1: escena 5536.0 (combate del mod)", "escena %d entrada 0" % (5500 + CAS) in log)
    ok("combate: el jugador 1 es el ID 63 del Heroe", "jugador 1 = id 63" in log)
    ok("combate acabado: gana el Heroe", re.search(r"combate acabado: id 63 contra id 42, gana el lado 1", log))
    ok("progreso del Heroe al ganar (DuVictoria)", re.search(r"dbz3 heroe: DU saga 0 combate 0 ganado", log))
    ok("desbloqueo con el maestro (res/heroe/desbloqueos.toml)", re.search(r"DESBLOQUEO ki_dorado_goku", log))
    if LARGO:
        ok("saga 4 (virtual): combate 0 ganado con el heroe", re.search(r"dbz3 heroe: DU saga 3 combate 0 ganado", log))
        ok("saga 4 (virtual): el mapa usa el codigo 12000+", re.search(r"escena 120%d entrada" % CAS, log) or re.search(r"escena 125%d entrada" % CAS, log))
    ok("marca 2 en el mapa tras la victoria", "se toca la marca de combate %d entrada 2" % (5500 + CAS) in log)
    fallos = [n for n, c, _ in RES if not c]
    print("heroe_du_e2e: %d/%d PASS" % (len(RES) - len(fallos), len(RES)))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
