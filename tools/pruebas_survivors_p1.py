# Escenario de P1/A07: jefe con su kit real y 2 fases. Se lanza igual que pruebas.py:
#   python E:\DBZ3HD_trabajo\survivors_comercial\p1_jefe.py correr opus2 survivors_jefe --par escenario=desierto
# (el coordinador puede pegarlo en tools/pruebas.py: ver peticiones_P1.md §4). Usa _survivors_foto y _survivors_registro de pruebas.py.
import re
import sys
from pathlib import Path

RAIZ = Path(r"C:/Users/javie/Desktop/PROYECTOS IA/DBZ Budokai 3 HD Collection")
P = sys.modules.get("__main__")   # pruebas.py lanzado como programa: registra en su propio ESCENARIOS (sin importarlo dos veces)
if not hasattr(P, "ESCENARIOS"):
    import pruebas as P  # noqa: E402

CFG = "dbz3_survivors_jefes_v2=true;dbz3_survivors_jugo=true"


def _jefe_env(p):
    return {"DBZ3_TEST_SURVIVORS_AUTO": "1", "DBZ3_TEST_SURVIVORS_DURA": p.get("dura", "180"), "DBZ3_TEST_SURVIVORS_ESCENARIO": p.get("escenario", "desierto"),
            "DBZ3_TEST_SURVIVORS_PERSONAJE": p.get("personaje", "0"), "DBZ3_TEST_SURVIVORS_SEMILLA": p.get("semilla", "2024"),
            "DBZ3_TEST_SURVIVORS_JEFE_FASE2": p.get("fase2", "120"), "DBZ3_TEST_SURVIVORS_INMORTAL": "1", "DBZ3_TEST_SURVIVORS_SIN_SONIDO": "1", "DBZ3_TEST_CFG": CFG}


def _espera_ataque(j, desde, patron=r"survivors: jefe ataque=(\w+) fase=(\d)", seg=60, solo=None):
    # espera una linea 'jefe ataque=' NUEVA (despues de `desde` lineas); `solo` = nombres validos. Devuelve (nombre, fase, n lineas)
    import time
    fin = time.time() + seg
    while time.time() < fin:
        m = re.findall(patron, j.texto_log())
        for k in range(desde, len(m)):
            if solo is None or m[k][0] in solo:
                return m[k][0], int(m[k][1]), k + 1
        time.sleep(0.15)
    raise P.Fallo("sin una linea 'jefe ataque=' nueva en %d s" % seg)


def _jefe(j, par):
    e = par.get("escenario", "desierto")   # prefijo de las capturas (varias pruebas a la vez no se pisan)
    # partida con piloto inmortal; el jefe sale al minuto 12 (dura=180: a los 144 s) y, si el piloto no lo baja solo, a los `fase2` s de aparecer cae al 49 % de vida (fase 2). Capturas 0,5:
    # intro (zoom + rotulo), aviso de un ataque de la fase 1, destello del cambio, forma nueva, ataque de la fase 2 y la definitiva si sale.
    j.log(r"survivors: partida empezada personaje=", seg=900)        # la primera vez genera los modelos de las dos formas (minutos)
    j.log(r"survivors: jefe aparece", seg=float(par.get("dura", "180")) * 4 + 240)
    P.dormir(0.9)
    P._survivors_foto(j, e + "_jefe_0_intro")
    nombre1, fase1, n = _espera_ataque(j, 0, seg=40)
    P.dormir(0.55)                                                    # a mitad de su aviso: marcas en el suelo
    P._survivors_foto(j, e + "_jefe_1_aviso_%s" % nombre1)
    j.log(r"survivors: jefe fase=2", seg=240)
    n = len(re.findall(r"survivors: jefe ataque=(\w+) fase=(\d)", j.texto_log()))   # los ataques de la fase 1 ya pasaron
    P.dormir(0.3)
    P._survivors_foto(j, e + "_jefe_2_destello")
    P.dormir(2.0)
    P._survivors_foto(j, e + "_jefe_3_forma2")
    nombre2, fase2, n = _espera_ataque(j, n, seg=40)
    P.dormir(0.55)
    P._survivors_foto(j, e + "_jefe_4_aviso_%s" % nombre2)
    try:   # la definitiva sale cada 3er ataque de la fase 2
        nombre3, _, n = _espera_ataque(j, n, seg=40, solo=("spirit_bomb", "death_ball", "supernova"))
        P.dormir(1.0)
        P._survivors_foto(j, e + "_jefe_5_definitiva_%s" % nombre3)
        P.dormir(1.4)
        P._survivors_foto(j, e + "_jefe_6_definitiva_cae")
    except P.Fallo:
        pass
    P._survivors_registro(j, e + "_registro_jefe.txt")
    t = j.texto_log()
    if len(set(re.findall(r"survivors: jefe ataque=(\w+)", t))) < 2:
        raise P.Fallo("menos de 2 ataques distintos del jefe en el registro")
    if "survivors: jefe fase=2" not in t:
        raise P.Fallo("el jefe no paso a la fase 2")
    if fase1 != 1 or fase2 != 2:
        raise P.Fallo("las fases de los ataques no cuadran: %s / %s" % (fase1, fase2))


P.ESCENARIOS["survivors_jefe"] = ("Dragon Budokai Survivors P1/A07: jefe con su kit real y 2 fases (--par escenario=llanura|desierto|ruinas|glaciar dura=180 fase2=120); capturas 0,5", _jefe_env, _jefe)

