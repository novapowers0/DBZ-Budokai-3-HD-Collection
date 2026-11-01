# -*- coding: utf-8 -*-
"""«Montar y probar»: arranca el juego y dice en palabras llanas si el mod ha cargado.
- Desarrollo (existe tools/pruebas.py y D:\\DBZ3HD\\smoke_c): copia los mods nuevos + _roster a la copia fija smoke_c,
  llama a `pruebas.py correr smoke_c menu` y devuelve la copia a como estaba (lo quitado va a D:\\_PARA_BORRAR_CLAUDE\\kit).
- Jugador: arranca SU juego (carpeta env.game) sin variables DBZ3_TEST_*, sin pulsar nada (solo se queda en el menu
  inicial, no llega a tocar la partida guardada), lee logs/dbz3_*.log y cierra el proceso que ha lanzado.
  python kit/probar.py [carpeta_de_mods] [mod ...]   (prueba suelta)
"""
from __future__ import annotations

import glob
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(HERE))
COPIA = r"D:\DBZ3HD\smoke_c"
RESPALDO = r"D:\_PARA_BORRAR_CLAUDE\kit"
LISTO = "quick settings menu ready"
ERR = re.compile(r"\[(error|critical)\]", re.I)


def T(es, en, ingles):
    return en if ingles else es


def analizar(texto, mods=(), ingles=False, personajes=0):
    """Lee el registro del juego -> (estado, frase). estado: ok | aviso | error.
    personajes = cuantos de los mods son de personaje (su linea de carga: «roster [_roster]: personaje id=N»)."""
    lineas = texto.splitlines()
    errs = [ln for ln in lineas if ERR.search(ln)]
    if errs:
        try:
            sys.path.insert(0, os.path.dirname(HERE))
            from kit import errores
            que, hacer, _a = errores.explicar(errs[0].split("]", 4)[-1].strip(), ingles)
        except Exception:                                    # noqa: BLE001
            que, hacer = T("El juego dio un error al arrancar.", "The game reported an error on start.", ingles), ""
        return "error", ("%s %s" % (que, hacer)).strip()
    if not any(LISTO in ln for ln in lineas):
        return "error", T("El juego no llegó al menú (se cerró o tardó demasiado). Mira el registro del juego.",
                          "The game did not reach the menu (it closed or took too long). Check the game log.", ingles)
    if "dbz3 roster:" not in texto:
        return "aviso", T("El juego arranca, pero no ha leído la lista de personajes: ¿se montó el mod?",
                          "The game starts, but did not read the roster: was the mod built?", ingles)
    cargados = len(re.findall(r"dbz3 roster \[[^\]]+\]: personaje id=", texto))
    if cargados < personajes:
        return "aviso", T("El juego arranca, pero solo ha cargado %d de %d personajes nuevos." % (cargados, personajes),
                          "The game starts, but loaded only %d of %d new characters." % (cargados, personajes), ingles)
    return "ok", T("Funciona: el juego arranca y ha cargado el mod sin errores%s." % (
        " (%d personaje(s) nuevo(s) en la rueda)" % cargados if cargados else ""),
                   "It works: the game starts and loaded the mod with no errors%s." % (
        " (%d new character(s) on the wheel)" % cargados if cargados else ""), ingles)


def _mover(origen, destino):
    os.makedirs(os.path.dirname(destino), exist_ok=True)
    shutil.move(origen, destino)


def _hay_juego(exe):
    r = subprocess.run(["tasklist", "/FI", "IMAGENAME eq " + os.path.basename(exe), "/FO", "CSV", "/NH"],
                       capture_output=True, text=True, errors="replace")
    return os.path.basename(exe).lower() in r.stdout.lower()


def _copia_fija():
    return os.path.isfile(os.path.join(RAIZ, "tools", "pruebas.py")) and os.path.isdir(COPIA)


def _probar_copia(mods_dir, mods, say):
    """Desarrollo: mods nuevos + _roster -> smoke_c; correr 'menu'; dejar smoke_c como estaba."""
    destino = os.path.join(COPIA, "mods")
    ts = time.strftime("%Y%m%d_%H%M%S")
    quitado, anadido = [], []
    try:
        if os.path.isdir(os.path.join(destino, "_roster")):      # el de la copia vuelve al acabar
            _mover(os.path.join(destino, "_roster"), os.path.join(RESPALDO, ts, "_roster"))
            quitado.append("_roster")
        anadido.append("_roster")                                # el que reconstruya pruebas.py se aparta al acabar
        for m in mods:   # pruebas.py reconstruye _roster en la copia con estos mods
            src = os.path.join(mods_dir, m)
            if not os.path.isdir(src):
                continue
            dst = os.path.join(destino, m)
            if os.path.isdir(dst):
                _mover(dst, os.path.join(RESPALDO, ts, m))
                quitado.append(m)
            shutil.copytree(src, dst)
            anadido.append(m)
        anadido = [x for x in anadido if x != "_roster"] + ["_roster"]
        say("prueba en la copia fija smoke_c: %s" % ", ".join(anadido))
        p = subprocess.run([sys.executable, os.path.join(RAIZ, "tools", "pruebas.py"), "correr", "smoke_c", "menu",
                            "--forzar"], capture_output=True, text=True, errors="replace", cwd=RAIZ, timeout=900)
        salida = (p.stdout or "") + (p.stderr or "")
        m = re.search(r"evidencia y resultados: (.+)", salida)
        log = os.path.join(m.group(1).strip(), "dbz3.log") if m else ""
        return (open(log, encoding="utf-8", errors="replace").read() if os.path.isfile(log) else ""), salida
    finally:
        for m in anadido:
            d = os.path.join(destino, m)
            if os.path.isdir(d):
                _mover(d, os.path.join(RESPALDO, ts, "prueba_" + m))
            if m in quitado:
                _mover(os.path.join(RESPALDO, ts, m), d)


def _leer(f):
    return open(f, encoding="utf-8", errors="replace").read()


def _probar_juego(game, say, segundos=120):
    """Jugador: su propio juego, sin DBZ3_TEST_*, sin teclas. Solo mira el registro."""
    exe = os.path.join(game, "dbz3.exe")
    if not os.path.isfile(exe):
        return "", "falta"
    if _hay_juego(exe):
        return "", "ocupado"
    env = {k: v for k, v in os.environ.items() if not k.startswith("DBZ3_TEST_")}
    t0 = time.time()
    proc = subprocess.Popen([exe], cwd=game, env=env)
    texto = ""
    try:
        while time.time() - t0 < segundos and proc.poll() is None:
            logs = [f for f in glob.glob(os.path.join(game, "logs", "dbz3_*.log")) if os.path.getmtime(f) >= t0 - 2]
            if logs:
                texto = _leer(max(logs, key=os.path.getmtime))
                if LISTO in texto or ERR.search(texto):
                    time.sleep(3)                              # deja escribir las ultimas lineas
                    texto = _leer(max(logs, key=os.path.getmtime))
                    break
            time.sleep(2)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(15)
            except subprocess.TimeoutExpired:
                proc.kill()
    return texto, ""


def probar(mods_dir, game, mods=(), ingles=False, say=print, forzar_jugador=False):
    """-> (estado, frase, modo). modo: 'copia' | 'juego'."""
    modo = "juego" if forzar_jugador or not _copia_fija() else "copia"
    if modo == "copia":
        texto, _extra = _probar_copia(mods_dir, mods, say)
    else:
        texto, extra = _probar_juego(game, say)
        if extra == "ocupado":
            return "aviso", T("El juego está abierto: ciérralo y vuelve a probar.",
                              "The game is open: close it and try again.", ingles), modo
        if extra:
            return "error", T("No encuentro el juego en la carpeta elegida.", "I cannot find the game in that folder.",
                              ingles), modo
    n = sum(os.path.isfile(os.path.join(mods_dir, m, "personaje.toml")) for m in mods)
    est, frase = analizar(texto, mods, ingles, n)
    return est, frase, modo


def rapido(say):
    """Para --selftest: el analizador con registros de ejemplo (sin abrir el juego)."""
    ok = True
    base = ("[info] dbz3 roster: imagen US/NA", "[info] dbz3 roster [_roster]: personaje id=22 donante=38", "")
    base = "\n".join(base)
    casos = [("ok", base + "[info] dbz3: " + LISTO, ["Zarbon B1"]),
             ("error", base + "[error] [core] fallo\n" + LISTO, ["Zarbon B1"]),
             ("error", base, ["Zarbon B1"]),
             ("aviso", base + LISTO, ["a", "b"]),
             ("aviso", LISTO, ["x"])]
    for esperado, txt, mods in casos:
        est, frase = analizar(txt, mods, False, 2 if mods == ["a", "b"] else 1)
        c = est == esperado and bool(frase) and "Traceback" not in frase
        ok &= c
        say(("OK" if c else "FAIL") + ": probar.analizar -> %s (esperado %s)" % (est, esperado))
    return ok


if __name__ == "__main__":
    md = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, "mods")
    e, f, m = probar(md, RAIZ, sys.argv[2:], say=print)
    print(m, e, f)
