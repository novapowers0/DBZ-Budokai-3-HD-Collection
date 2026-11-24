# Escenarios de P3 (Dragon Budokai Survivors: contenido v2 y audio v2). Se lanza igual que pruebas.py:
#   python E:\DBZ3HD_trabajo\survivors_comercial\p3_escenarios.py correr opus2 survivors_p3_partida --par escenario=nave --par personaje=3 --par dura=120
#   python E:\DBZ3HD_trabajo\survivors_comercial\p3_escenarios.py correr opus2 survivors_p3_audio
# survivors_p3_partida: como survivors_partida pero con contenido_v2 y audio_v2 encendidos (--par escenario=nave|planeta|..., personaje=0..7, dura=S, dificultad=0..2).
# survivors_p3_audio: como survivors_hub --par sonido=1 con audio_v2; comprueba en el registro la musica, los sonidos y la sonda de bancos.
# El coordinador puede pegar este mismo codigo en tools/pruebas.py (usa _survivors_partida y _survivors_hub de alli).
import re
import sys
from pathlib import Path

RAIZ = Path(r"C:/Users/javie/Desktop/PROYECTOS IA/DBZ Budokai 3 HD Collection")
P = sys.modules.get("__main__")   # pruebas.py lanzado como programa: registra en su propio ESCENARIOS (sin importarlo dos veces)
if not hasattr(P, "ESCENARIOS"):
    import pruebas as P  # noqa: E402

CFG = "dbz3_survivors=true;dbz3_survivors_audio_v2=true;dbz3_survivors_contenido_v2=true"


def _partida_env(p):
    e = {"DBZ3_TEST_CFG": CFG, "DBZ3_TEST_SURVIVORS_AUTO": "1", "DBZ3_TEST_SURVIVORS_DURA": p.get("dura", "120"),
         "DBZ3_TEST_SURVIVORS_PERSONAJE": p.get("personaje", "0"), "DBZ3_TEST_SURVIVORS_SEMILLA": p.get("semilla", "2024"),
         "DBZ3_TEST_SURVIVORS_SIN_SONIDO": "1"}   # silencio total: el registro "audio: suena X" sale igual
    if p.get("escenario"):
        e["DBZ3_TEST_SURVIVORS_ESCENARIO"] = p["escenario"]
    if p.get("dificultad"):
        e["DBZ3_TEST_SURVIVORS_DIFICULTAD"] = p["dificultad"]
    return e


def _partida(j, par):
    P._survivors_partida(j, par)
    t = j.texto_log()
    if re.search(r"survivors_v2\.toml.*(no |error|fall)", t, re.I):
        raise P.Fallo("survivors_v2.toml no cargo (mira el aviso en el registro)")
    P._survivors_registro(j, "registro_p3_partida.txt")
    j.info("survivors p3: partida OK (escenario %s, personaje %s)" % (par.get("escenario", "mapa"), par.get("personaje", "0")))


def _audio_env(p):
    # sin SIN_SONIDO: el juego pide musica, sonidos y combate de fondo de verdad (el arnes mantiene audio_mute/dbz3_mute: no se oye nada)
    return {"DBZ3_TEST_CFG": CFG, "DBZ3_TEST_SURVIVORS_SEMILLA": p.get("semilla", "2024"), "DBZ3_TEST_SURVIVORS_SONDA": "1"}


def _audio(j, par):
    P._survivors_hub(j, par)
    t = j.texto_log()
    falta = []
    for etiqueta, patron in (("musica de batalla", r"audio: suena musica batalla"), ("sonido de tecnica/golpe", r"audio: suena (tecnica|golpe)"), ("sonda de bancos", r"sonda: ranura \d+")):
        if not re.search(patron, t):
            falta.append(etiqueta)
    P._survivors_registro(j, "registro_p3_audio.txt")
    if falta:
        raise P.Fallo("faltan en el registro: " + ", ".join(falta))
    j.info("survivors p3: audio OK (musica, sonidos y sonda en el registro)")


P.ESCENARIOS["survivors_p3_partida"] = ("Dragon Budokai Survivors P3: partida con piloto y contenido_v2 + audio_v2 (--par escenario=nave|planeta personaje=0-7 dura=S dificultad=0-2)", _partida_env, _partida)
P.ESCENARIOS["survivors_p3_audio"] = ("Dragon Budokai Survivors P3/A16: HUB > partida con audio_v2 y sonido real pedido; PASS si el registro trae musica, sonidos y sonda de bancos", _audio_env, _audio)

