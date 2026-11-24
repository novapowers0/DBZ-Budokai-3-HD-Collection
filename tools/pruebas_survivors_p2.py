# Escenarios de P2 (Dragon Budokai Survivors, pantallas y meta-progresion). Se lanza igual que pruebas.py:
#   python E:\DBZ3HD_trabajo\survivors_comercial\p2_escenarios.py correr opus2 survivors_menu
# Registra survivors_menu / survivors_cofre / survivors_meta / survivors_ranking en pruebas.ESCENARIOS y llama a pruebas.main().
# El coordinador puede pegar este mismo codigo en tools/pruebas.py (sin cambios: usa _survivors_foto y _survivors_registro de alli).
import re
import sys
from pathlib import Path

RAIZ = Path(r"C:/Users/javie/Desktop/PROYECTOS IA/DBZ Budokai 3 HD Collection")
P = sys.modules.get("__main__")   # pruebas.py lanzado como programa: registra en su propio ESCENARIOS (sin importarlo dos veces)
if not hasattr(P, "ESCENARIOS"):
    import pruebas as P  # noqa: E402

MVP = P._SURVIVORS_MVP
CFG = "dbz3_survivors=true;dbz3_survivors_menu_v2=true;dbz3_survivors_meta_v2=true"


def _guardado(nombre):
    ruta = MVP / nombre
    MVP.mkdir(parents=True, exist_ok=True)
    for f in (ruta, Path(str(ruta) + ".bak"), Path(str(ruta) + ".tmp")):
        if f.exists():
            f.unlink()
    return str(ruta)


def _pulsar(j, k, cod, intentos=6):
    # el juego anota cada boton nuevo ("survivors: pad XXXX") solo con MANDO=1
    patron = r"survivors: pad %04x" % cod
    for i in range(intentos):
        antes = len(re.findall(patron, j.texto_log()))
        j.tecla(k + " %d" % (350 + 150 * i), 700 + 150 * i)
        P.dormir(0.15)
        if len(re.findall(patron, j.texto_log())) > antes:
            return
    raise P.Fallo("la pulsacion %s no llego al juego tras %d intentos" % (k, intentos))


# ---------------------------------------------------------------- survivors_menu
NL = chr(10)
SEMILLA_PROG = NL.join(["zeni = 2000", "desbloqueados = 3", "jefes = 2", "partidas = 6", "victorias = 1", "mejoras = [2, 1, 0, 0, 0, 0, 0, 0, 0, 0]",
                           "medallas = [3, 2, 0, 0, 0, 0, 0, 0]", "vistos_tec = 4207", "vistos_pas = 23", "vistos_enem = 7", "vistos_jefe = 1", "logros = 1035",
                           "cofres = 4", "total_derrotados = 2600", "total_zeni = 1900", "mejor_pts = 38000",
                           "records = [38000, 0, 21000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]", ""])


def _menu_env(p):
    ruta = _guardado("menu_prueba.toml")
    Path(ruta).write_text(SEMILLA_PROG, encoding="utf-8")   # progreso de PRUEBA con algo visto y 4 logros
    return {"DBZ3_TEST_CFG": CFG, "DBZ3_TEST_SURVIVORS_MANDO": "1", "DBZ3_TEST_SURVIVORS_MENU": p.get("menu", "1"), "DBZ3_TEST_SURVIVORS_ZENI": p.get("zeni", "2000"),
            "DBZ3_TEST_SURVIVORS_GUARDADO": ruta, "DBZ3_TEST_SURVIVORS_SIN_SONIDO": "1", "DBZ3_TEST_SURVIVORS_SEMILLA": "2024", "DBZ3_TEST_SURVIVORS_RANKING": "local"}


def _menu(j, par):
    # portada > cada estacion > volver (el recorrido es automatico: ver Portada en modo_pantallas.cpp). Captura 0,5 de la portada y de cada estacion.
    j.log(r"survivors: cargado \(", seg=900)
    P.dormir(1.2)
    P._survivors_foto(j, "menu_0_portada")
    nombres = ["jugar", "tienda", "coleccion", "logros", "clasificacion", "comojugar", "opciones"]
    for n in range(7):
        j.log(r"survivors: menu estacion=%d %s" % (n, nombres[n]), seg=30)
        P.dormir(1.3)
        P._survivors_foto(j, "menu_%d_%s" % (n + 1, nombres[n]))
    j.log(r"survivors: menu recorrido completo", seg=30)
    t = j.texto_log()
    P._survivors_registro(j, "registro_menu.txt")
    if len(re.findall(r"survivors: menu estacion=", t)) < 7:
        raise P.Fallo("faltan estaciones en el registro")
    j.info("survivors: menu OK, 7 estaciones")


# ---------------------------------------------------------------- survivors_cofre
def _cofre_env(p):
    return {"DBZ3_TEST_CFG": CFG, "DBZ3_TEST_SURVIVORS_AUTO": "1", "DBZ3_TEST_SURVIVORS_COFRE": p.get("tier", "5"), "DBZ3_TEST_SURVIVORS_DURA": p.get("dura", "120"),
            "DBZ3_TEST_SURVIVORS_SEMILLA": p.get("semilla", "2024"), "DBZ3_TEST_SURVIVORS_PERSONAJE": p.get("personaje", "0"),
            "DBZ3_TEST_SURVIVORS_GUARDADO": _guardado("cofre_prueba.toml"), "DBZ3_TEST_SURVIVORS_SIN_SONIDO": "1"}


def _cofre(j, par):
    tier = par.get("tier", "5")
    j.log(r"survivors: partida empezada personaje=", seg=900)
    j.log(r"survivors: cofre cerrado tier=%s" % tier, seg=120)
    P.dormir(0.3)
    P._survivors_foto(j, "cofre_1_cerrado")
    j.log(r"survivors: cofre abierto tier=%s" % tier, seg=15)
    P.dormir(1.0)
    P._survivors_foto(j, "cofre_2_abierto")
    P._survivors_registro(j, "registro_cofre.txt")
    m = re.search(r"survivors: cofre premios=(\d+) tier=(\d+)", j.texto_log())
    if not m or m.group(1) != tier:
        raise P.Fallo("el cofre no dio %s premios (%s)" % (tier, m.group(0) if m else "sin linea"))
    j.info("survivors: cofre de %s premios (%s)" % (tier, m.group(0)))


# ---------------------------------------------------------------- survivors_meta
def _meta_env(p):
    return {"DBZ3_TEST_CFG": CFG, "DBZ3_TEST_SURVIVORS_AUTO": "1", "DBZ3_TEST_SURVIVORS_LOGRO": "1", "DBZ3_TEST_SURVIVORS_DURA": p.get("dura", "60"),
            "DBZ3_TEST_SURVIVORS_SEMILLA": p.get("semilla", "2024"), "DBZ3_TEST_SURVIVORS_ESCENARIO": "llanura",
            "DBZ3_TEST_SURVIVORS_GUARDADO": _guardado("meta_prueba.toml"), "DBZ3_TEST_SURVIVORS_SIN_SONIDO": "1"}


def _meta(j, par):
    # un logro salta (aviso en partida), la partida acaba (resultados v2) y se abre la pantalla de Logros desde el menu (B, abajo x3, A)
    j.log(r"survivors: partida empezada personaje=", seg=900)
    j.log(r"survivors: logro desbloqueado id=", seg=120)
    P.dormir(0.8)
    P._survivors_foto(j, "meta_1_aviso")
    j.log(r"survivors: pantalla de resultados", seg=240)
    P.dormir(1.5)
    P._survivors_foto(j, "meta_2_resultados")
    j.tecla("1 350", 700)                   # LB: compartir (captura a disco)
    P.dormir(1.2)
    P._survivors_foto(j, "meta_2b_compartir")
    if "survivors: captura guardada" not in j.texto_log():
        raise P.Fallo("el boton compartir no guardo la captura")
    j.tecla("Backspace 350", 700)           # B: menu propio
    P.dormir(1.5)
    for _ in range(3):
        j.tecla("S 350", 700)               # abajo x3: Logros
        P.dormir(0.4)
    j.tecla("Space 350", 700)               # A
    P.dormir(1.5)
    P._survivors_foto(j, "meta_3_logros")
    P._survivors_registro(j, "registro_meta.txt")
    ruta = MVP / "meta_prueba.toml"
    texto = ruta.read_text(encoding="utf-8") if ruta.exists() else ""
    if not re.search(r"^logros = [1-9]", texto, re.M):
        raise P.Fallo("el archivo de prueba no guarda el logro: %r" % texto[:300])
    j.info("survivors: logro guardado; %s" % re.search(r"survivors: resultados v2 .*", j.texto_log()).group(0))


# ---------------------------------------------------------------- survivors_ranking
def _ranking_env(p):
    return {"DBZ3_TEST_CFG": CFG, "DBZ3_TEST_SURVIVORS_MANDO": "1", "DBZ3_TEST_SURVIVORS_MENU": "ranking", "DBZ3_TEST_SURVIVORS_RANKING": "local",
            "DBZ3_TEST_SURVIVORS_GUARDADO": _guardado("ranking_prueba.toml"), "DBZ3_TEST_SURVIVORS_SIN_SONIDO": "1"}


def _ranking(j, par):
    # tabla local sin red: captura de semana, siempre (LB), escenario (derecha) y dificultad (Start); luego Y salta a mi puesto
    j.log(r"survivors: ranking tabla local filas=", seg=900)
    P.dormir(1.0)
    P._survivors_foto(j, "ranking_1_semana")
    j.tecla("1 350", 700)                    # LB: cambia de periodo
    P.dormir(0.8)
    j.tecla("3 350", 700)                    # RB tambien cambia de periodo (vuelve)
    P.dormir(0.8)
    j.tecla("D 350", 700)                    # derecha: filtro de escenario
    P.dormir(0.8)
    P._survivors_foto(j, "ranking_2_escenario")
    j.tecla("Return 350", 700)               # Start: filtro de dificultad
    P.dormir(0.8)
    P._survivors_foto(j, "ranking_3_dificultad")
    P._survivors_registro(j, "registro_ranking.txt")
    j.info("survivors: ranking local OK")


# ---------------------------------------------------------------- survivors_subida (A10) y survivors_eleccion (A13)
def _subida_env(p):
    return {"DBZ3_TEST_CFG": CFG, "DBZ3_TEST_SURVIVORS_AUTO": "1", "DBZ3_TEST_SURVIVORS_SUBIDA": "1", "DBZ3_TEST_SURVIVORS_DURA": p.get("dura", "120"),
            "DBZ3_TEST_SURVIVORS_SEMILLA": p.get("semilla", "2024"), "DBZ3_TEST_SURVIVORS_GUARDADO": _guardado("subida_prueba.toml"), "DBZ3_TEST_SURVIVORS_SIN_SONIDO": "1"}


def _subida(j, par):
    # la 1a subida se ve (captura), se re-sortea (X), se ve, se destierra (LB), se ve; el piloto elige despues
    j.log(r"survivors: partida empezada personaje=", seg=900)
    for i, nombre in enumerate(("1_inicial", "2_resorteada", "3_desterrada")):
        j.log(r"(?s)(survivors: nivel \d+ opciones=.*?){%d}" % (i + 1), seg=120)
        P.dormir(0.5)
        P._survivors_foto(j, "subida_" + nombre)
    P._survivors_registro(j, "registro_subida.txt")
    t = j.texto_log()
    for pat in (r"survivors: subida resortear n=1", r"survivors: subida desterrada quedan="):
        if not re.search(pat, t):
            raise P.Fallo("falta en el registro: " + pat)
    j.info("survivors: subida v2 con re-sortear y desterrar OK")


def _eleccion_env(p):
    return {"DBZ3_TEST_CFG": CFG + ";dbz3_survivors_contenido_v2=true", "DBZ3_TEST_SURVIVORS_MANDO": "1", "DBZ3_TEST_SURVIVORS_ZENI": "500",
            "DBZ3_TEST_SURVIVORS_GUARDADO": _guardado("eleccion_prueba.toml"), "DBZ3_TEST_SURVIVORS_SIN_SONIDO": "1"}


def _eleccion(j, par):
    # con los 8 luchadores de survivors_v2.toml: rejilla de 2 filas; los 5 nuevos bloqueados con "?" y su condicion
    j.log(r"survivors: eleccion de personaje", seg=900)
    P.dormir(1.5)
    P._survivors_foto(j, "eleccion_1_rejilla")
    _pulsar(j, "S", 0x0002)
    P.dormir(0.8)
    P._survivors_foto(j, "eleccion_2_fila2")
    P._survivors_registro(j, "registro_eleccion.txt")


# ---------------------------------------------------------------- survivors_tienda_v2 (C06): comprar y "devolver todo"
def _tienda2_env(p):
    return {"DBZ3_TEST_CFG": CFG, "DBZ3_TEST_SURVIVORS_MANDO": "1", "DBZ3_TEST_SURVIVORS_ZENI": "2000", "DBZ3_TEST_SURVIVORS_GUARDADO": _guardado("tienda2_prueba.toml"),
            "DBZ3_TEST_SURVIVORS_SIN_SONIDO": "1"}


def _tienda2(j, par):
    j.log(r"survivors: eleccion de personaje", seg=900)
    P.dormir(1.0)
    _pulsar(j, "P", 0x8000)                       # Y: tienda
    j.log(r"survivors: tienda abierta zeni=2000", seg=10)
    P.dormir(0.6)
    _pulsar(j, "Space", 0x1000)                   # A: comprar la 1a
    _pulsar(j, "D", 0x0008)
    _pulsar(j, "Space", 0x1000)                   # A: comprar la 2a
    j.log(r"survivors: tienda compra \w+ nivel=1 zeni=1800", seg=10)
    P.dormir(0.6)
    P._survivors_foto(j, "tienda2_1_comprado")
    _pulsar(j, "L", 0x4000)                       # X: devolver todo
    j.log(r"survivors: tienda devuelto zeni=200 total=2000", seg=10)
    P.dormir(0.8)
    P._survivors_foto(j, "tienda2_2_devuelto")
    P._survivors_registro(j, "registro_tienda2.txt")
    j.info("survivors: devolver todo OK: " + re.search(r"survivors: tienda devuelto .*", j.texto_log()).group(0))


P.ESCENARIOS["survivors_tienda_v2"] = ("Dragon Budokai Survivors P2/C06: tienda v2, compra 2 mejoras y X = devolver todo (el zeni vuelve exacto)", _tienda2_env, _tienda2)
P.ESCENARIOS["survivors_subida"] = ("Dragon Budokai Survivors P2/A10: subida de nivel v2 con re-sortear y desterrar (capturas 0,5)", _subida_env, _subida)
P.ESCENARIOS["survivors_eleccion"] = ("Dragon Budokai Survivors P2/A13: seleccion de luchador v2 con 8 luchadores (rejilla 2 filas, bloqueados con ?)", _eleccion_env, _eleccion)
P.ESCENARIOS["survivors_menu"] = ("Dragon Budokai Survivors P2/A12: portada y menu propio; recorrido automatico portada > 7 estaciones > volver (capturas 0,5)", _menu_env, _menu)
P.ESCENARIOS["survivors_cofre"] = ("Dragon Budokai Survivors P2/A11: cofre de 5 premios forzado a los 20 s (--par tier=1|3|5); capturas cerrado y abierto", _cofre_env, _cofre)
P.ESCENARIOS["survivors_meta"] = ("Dragon Budokai Survivors P2/C08: un logro salta en partida, resultados v2 y pantalla de Logros; progreso de PRUEBA", _meta_env, _meta)
P.ESCENARIOS["survivors_ranking"] = ("Dragon Budokai Survivors P2/A15: clasificacion con tabla local sin red; periodo, escenario y dificultad", _ranking_env, _ranking)

