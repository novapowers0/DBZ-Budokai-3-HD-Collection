# -*- coding: utf-8 -*-
"""«Fusiones» (KIT-07b, issue #73) sin interfaz: cuanto dura una fusion y el ajuste que lo guarda.

El juego (src/fusion.cpp, DuracionCuadro) lee `dbz3_fusion_duracion` del dbz3_user.toml: "original" (de serie, no
toca nada) o reglas "todas=V" / "<ficha>=V" separadas por comas, V = original | sin | N% | Ns. Este modulo traduce
lo que elige el jugador (Corta / Como el original / Larga / Sin limite, o segundos en modo Avanzado) a ese texto y lo
escribe con copia en modkit_backups (tipo «copia» del indice: «Deshacer» la devuelve). RE en
studies 5/ENTREGAS/KIT_FUSIONES.md. Gogeta suelto (KIT-17): un mod `fusion_gogeta_suelto` con la plantilla probada en el juego (ver SUELTOS).
"""
from __future__ import annotations

import os
import re
import shutil
import time
import uuid

CVAR = "dbz3_fusion_duracion"
# clave, nombre es, en, fichas del juego (char96 0x8234ABB8 + 96*ID: «GOGITA (GOKOU)»...), quienes se fusionan
FUSIONES = [
    ("gogeta", "Gogeta", "Gogeta", (68, 69), ("Goku", "Vegeta")),
    ("vegito", "Vegetto", "Vegito", (72, 73), ("Goku", "Vegeta")),
    ("gotenks", "Gotenks", "Gotenks", (64, 65), ("Goten", "Trunks")),
    ("gogeta4", "Gogeta SSJ4", "Gogeta SSJ4", (70, 71), ("Goku SSJ4", "Vegeta SSJ4")),
    ("kibito", "Kibito-shin", "Kibito Kai", (76,), ("Kaio-shin", "Kibito")),
]
# nivel -> valor del ajuste (N% cambia el ritmo: la barra de la fusion sigue llena al empezar)
NIVELES = [("corta", "Corta", "Short", "50%"), ("original", "Como el original", "Like the original", "original"),
           ("larga", "Larga", "Long", "200%"), ("sin", "Sin límite", "No limit", "sin")]
VALOR = {k: v for k, _es, _en, v in NIVELES}
DE_VALOR = {v: k for k, v in VALOR.items()}
COMO_TODAS = "todas"   # por fusion: lo mismo que el ajuste general
# Fusionados como personajes sueltos (KIT-17): solo Gogeta esta probado en el juego (KIT_PRUEBAS_JUEGO.md, «Fusionados
# sueltos»); Vegetto (ficha 72) y Gotenks (64) no, y Gokule es un mod ajeno. clave, nombre, ficha, forma, probado.
SUELTOS = [("gogeta", "Gogeta", 68, 3, True), ("vegito", "Vegetto", 72, 2, False),
           ("gotenks", "Gotenks", 64, 3, False), ("gokule", "Gokule", None, None, False)]
SIN_PROBAR = ("sin probar en el juego", "not tested in the game yet")
AVISO_PLAZA = ("Necesita una casilla libre en la rueda; con muchos personajes instalados puede no haberla.",
               "It needs a free slot on the wheel; with many characters installed there may be none.")
MOD_GOGETA = "fusion_gogeta_suelto"
# personaje.toml del ejemplo PROBADO EN EL JUEGO (E:\DBZ3HD_trabajo\kit_14\mods_b\gogeta_suelto), byte a byte.
PLANTILLA_GOGETA = (b'[personaje]\nficha_nativa = 68    # Gogeta (ficha de combate del juego)\n'
                    b'forma_inicial = 3    # su forma fusionada\nnombre = "Gogeta"\n')


def T(es, en, ingles):
    return en if ingles else es


# ------------------------------------------------------------------------------------------ texto del ajuste
def valor_de(nivel=None, segundos=0):
    """Valor de UNA regla: segundos (> 0, modo Avanzado) manda sobre el nivel."""
    if segundos and int(segundos) > 0:
        return "%ds" % max(1, min(3600, int(segundos)))
    return VALOR.get(nivel or "original", "original")


def componer(general="original", por=None, segundos_general=0, segundos=None):
    """Texto del ajuste. general = nivel para todas; por = {clave: nivel | COMO_TODAS}; segundos = {clave: s}."""
    por, segundos = por or {}, segundos or {}
    reglas = []
    g = valor_de(general, segundos_general)
    if g != "original":
        reglas.append("todas=" + g)
    for clave, _es, _en, fichas, _q in FUSIONES:
        n, s = por.get(clave, COMO_TODAS), segundos.get(clave, 0)
        if n == COMO_TODAS and not s:
            continue
        v = valor_de(None if n == COMO_TODAS else n, s)
        if v == g:
            continue
        reglas += ["%d=%s" % (f, v) for f in fichas]
    return ",".join(reglas) or "original"


def leer_reglas(texto):
    """'todas=sin,68=45s' -> {'todas': 'sin', 68: '45s'} (lo que no se entiende se ignora, como el juego)."""
    out = {}
    for t in re.split(r"[,;]", (texto or "").strip()):
        t = t.strip()
        if not t or t == "original":
            continue
        clave, _, v = t.rpartition("=")
        clave = clave.strip() or "todas"
        out["todas" if clave == "todas" else int(clave) if clave.isdigit() else clave] = v.strip()
    return out


def regla(texto, ficha):
    """Lo que hara el juego con la fusion de esa ficha (mismo criterio que DuracionDe en src/fusion.cpp):
    ('original', None) | ('sin', None) | ('pct', N) | ('seg', N)."""
    r = leer_reglas(texto)
    v = r.get(ficha, r.get("todas", "original"))
    if v == "sin":
        return "sin", None
    if v.endswith("s") and v[:-1].isdigit():
        return "seg", max(1, min(3600, int(v[:-1])))
    if v.rstrip("%").isdigit():
        return "pct", max(1, min(1000, int(v.rstrip("%"))))
    return "original", None


def desde_texto(texto):
    """Inverso de componer para la pantalla: (general, segundos_general, {clave: nivel}, {clave: segundos})."""
    r = leer_reglas(texto)

    def nivel(v):
        if v is None:
            return "original", 0
        if v.endswith("s") and v[:-1].isdigit():
            return "original", int(v[:-1])
        return DE_VALOR.get(v, "original"), 0
    general, sg = nivel(r.get("todas"))
    por, seg = {}, {}
    for clave, _es, _en, fichas, _q in FUSIONES:
        v = r.get(fichas[0])
        if v is None:
            continue
        por[clave], seg[clave] = nivel(v)
        if seg[clave]:
            por[clave] = COMO_TODAS
    return general, sg, por, seg


def frase(texto, ingles=False):
    """Una linea llana: «duración sin límite», «fusiones cortas; Gogeta 45 s»…"""
    def de(v):
        k, n = regla("todas=" + v, 0)
        return {"sin": T("sin límite", "no limit", ingles), "original": T("como el original", "like the original", ingles),
                "pct": T("%d %% del original" % (n or 0), "%d %% of the original" % (n or 0), ingles),
                "seg": T("%d s" % (n or 0), "%d s" % (n or 0), ingles)}[k]
    r = leer_reglas(texto)
    partes = []
    if "todas" in r:
        partes.append(T("duración ", "duration ", ingles) + de(r["todas"]))
    for clave, es, en, fichas, _q in FUSIONES:
        if fichas[0] in r:
            partes.append("%s %s" % (T(es, en, ingles), de(r[fichas[0]])))
    return "; ".join(partes) or T("como el original", "like the original", ingles)


# ------------------------------------------------------------------------------------------ dbz3_user.toml
def ruta_toml(game):
    """Donde lo lee el juego (src/launcher/settings.cpp UserSettingsPath): junto al exe si existe o se puede
    escribir; si no, en la carpeta de datos del usuario (<juego>\\user_data\\dbz3 o la del usuario\\dbz3)."""
    home = os.path.expanduser("~")
    cands = [game, os.path.join(game, "user_data", "dbz3"), os.path.join(home, "dbz3"),
             os.path.join(home, "Documents", "dbz3")] if game else []
    for d in cands:
        if os.path.isfile(os.path.join(d, "dbz3_user.toml")):
            return os.path.join(d, "dbz3_user.toml")
    return os.path.join(game, "dbz3_user.toml") if game else ""


_LINEA = re.compile(r"(?m)^[ \t]*%s[ \t]*=[ \t]*([\"'])(.*?)\1[ \t]*$" % CVAR)


def leer_ajuste(game):
    try:
        with open(ruta_toml(game), encoding="utf-8", errors="replace") as fh:
            m = _LINEA.search(fh.read())
    except OSError:
        return "original"
    return m.group(2) if m else "original"


def con_ajuste(texto, valor):
    """El toml con la linea del ajuste puesta (o cambiada); lo demas, tal cual."""
    linea = '%s = "%s"' % (CVAR, valor.replace('"', ""))
    if _LINEA.search(texto):
        return _LINEA.sub(lambda _m: linea, texto, count=1)
    return texto + ("" if not texto or texto.endswith("\n") else "\n") + linea + "\n"


def guardar(env, valor, anotar):
    """Escribe el ajuste con copia en modkit_backups («Deshacer» de la portada/pantalla la devuelve).
    anotar = nucleo.anotar_indice. Devuelve la ruta escrita."""
    ruta = ruta_toml(env.game)
    if not ruta:
        raise OSError("no encuentro la carpeta del juego")
    try:
        with open(ruta, encoding="utf-8", errors="replace") as fh:
            texto = fh.read()
    except FileNotFoundError:
        texto = ""
    stamp = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:6]   # dos guardados en el mismo segundo: dos lotes
    if os.path.isfile(ruta):
        d = os.path.join(env.backups, stamp + "-fusiones")
        os.makedirs(d, exist_ok=True)
        shutil.copy2(ruta, os.path.join(d, "dbz3_user.toml"))
        anotar(env, stamp, "copia", ruta, os.path.relpath(os.path.join(d, "dbz3_user.toml"), env.backups))
    with open(ruta, "w", encoding="utf-8", newline="") as fh:
        fh.write(con_ajuste(texto, valor))
    return ruta


def resumen(game, ingles=False):
    """Para «Montar y probar» (kit/probar.py): [«✓ fusiones: duración sin límite»] o [] si esta como el original."""
    v = leer_ajuste(game) if game else "original"
    if v in ("", "original"):
        return []
    return ["✓ %s: %s" % (T("fusiones", "fusions", ingles), frase(v, ingles))]


# ------------------------------------------------------------------------------------------ Gogeta suelto (KIT-17)
def activo(mods):
    return os.path.isfile(os.path.join(mods, MOD_GOGETA, "personaje.toml"))


def plazas_libres(mods):
    """Plazas libres (<64) de la rueda con lo que ya hay en mods (roster_build.py estado, sin editarlo); None si no
    se pudo mirar."""
    import json
    import subprocess
    import sys
    mc = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    try:
        p = subprocess.run([sys.executable, os.path.join(mc, "roster_build.py"), "estado", "--json", "--mods", mods],
                           capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        datos = json.loads(p.stdout[p.stdout.index("{"):])
    except (OSError, ValueError, subprocess.SubprocessError):
        return None
    return sum(1 for x in datos.get("plazas", []) if x["estado"] == "libre" and x["id"] < 64)


def motivo_sin_plaza(ingles=False):
    return T("No hay ninguna casilla libre en la rueda, así que no he creado a Gogeta. Apaga algún personaje "
             "instalado y vuelve a guardar.", "There is no free slot on the wheel, so I did not create Gogeta. "
             "Turn off an installed character and save again.", ingles)


def crear_gogeta(env, libres=None):
    """Crea mods/fusion_gogeta_suelto/personaje.toml (la plantilla probada). Con libres == 0 no crea nada y lo dice."""
    if libres is None:
        libres = plazas_libres(env.mods)
    if libres == 0:
        raise RuntimeError(motivo_sin_plaza())
    d = os.path.join(env.mods, MOD_GOGETA)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "personaje.toml"), "wb") as fh:
        fh.write(PLANTILLA_GOGETA)
    return "creado " + MOD_GOGETA


def quitar_gogeta(env, anotar):
    """Lo aparta a modkit_backups (nada se borra; «Deshacer» lo devuelve)."""
    from kit import planes
    planes.Lote(env, anotar).mover(os.path.join(env.mods, MOD_GOGETA))
    return "apartado " + MOD_GOGETA


def datos_mod(mod_dir):
    """(ficha, forma, nombre) si el mod es un fusionado suelto (personaje.toml con ficha_nativa), si no None."""
    try:
        with open(os.path.join(mod_dir, "personaje.toml"), encoding="utf-8", errors="replace") as fh:
            t = fh.read()
    except OSError:
        return None
    f = re.search(r"(?m)^\s*ficha_nativa\s*=\s*(\d+)", t)
    if not f:
        return None
    n = re.search(r'(?m)^\s*nombre\s*=\s*"([^"]*)"', t)
    fo = re.search(r"(?m)^\s*forma_inicial\s*=\s*(\d+)", t)
    return int(f.group(1)), int(fo.group(1)) if fo else None, n.group(1) if n else os.path.basename(mod_dir)


def resumen_mod(montaje, mod_dir, ingles=False):
    """Para «Montar y probar»: «✓ 1 personaje nuevo: Gogeta (fusionado)» / «✕ … se omitió: motivo» ([] si no es de estos)."""
    d = datos_mod(mod_dir)
    if not d:
        return []
    nombre = d[2]
    m = os.path.basename(os.path.normpath(mod_dir))
    malos = [ln.split(": ", 1)[1].strip() for ln in montaje.splitlines() if ln.startswith("!! %s: " % m) and "omitido" in ln]
    if malos:
        return [T("✕ el personaje %s (fusionado) se omitió: %s" % (nombre, malos[0]),
                  "✕ the character %s (fused) was skipped: %s" % (nombre, malos[0]), ingles)]
    if re.search(r"^\+ %s: id " % re.escape(nombre), montaje, re.M):
        return [T("✓ 1 personaje nuevo: %s (fusionado)" % nombre, "✓ 1 new character: %s (fused)" % nombre, ingles)]
    return [T("✕ el personaje %s (fusionado) no aparece en el montaje" % nombre,
              "✕ the character %s (fused) is not in the build" % nombre, ingles)]
