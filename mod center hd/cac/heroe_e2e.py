#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""heroe_e2e.py - prueba de extremo a extremo del Modo Heroe (studies 5, linea C).

Etapas (cada una se puede pedir con --solo):
  cac      modulo cac sin juego: 3 heroes, guardado v2, migracion v1 -> v2 con copia .v1.bak (e2e/cac_test.cpp)
  cartas   tecnicas_cartas.py: una carta por ataque del catalogo #SKC, sacadas del juego del jugador
  arte     heroe_arte.py: capa visual propia (piezas nativas recoloreadas)
  apariencia / deseo  en juego: la apariencia se edita siempre; la raza solo con un deseo de Shenron (7 Esferas)
  fichas   en juego: selector de 3 heroes (crear 2.o y 3.o, el 4.o no cabe) y cac.toml v2 en disco
  tecnicas en juego: Moveset > Tecnicas: equipa 2 cartas, empieza la mision y el combate CARGA esas capsulas
  du       C3: el Heroe en el mapa de Dragon Universe de extremo a extremo (heroe_du_e2e.py, copia s5_c3): entrada directa sin
           rueda, intro, mapa con su cuerpo, combate, progreso, desbloqueo con el maestro y marca 2; y que NO sale en ninguna rueda
  todo     las cinco de arriba (por defecto); `du` se pide aparte (tarda ~3 min)

Etapas futuras (C3, cuando existan el mapa DU y los maestros con %): crear -> saga -> entrenar -> cambiar tecnica ->
el combate usa la tecnica nueva con el mapa DU de por medio. Hoy `tecnicas` cubre "cambiar tecnica -> combate la usa".

Uso:  python "mod center hd/cac/heroe_e2e.py" [--solo cac|cartas|arte|fichas|tecnicas|todo] [--copia s5_c2] [--repetir 3]
Las pruebas en juego usan SOLO copias de D:\\DBZ3HD (tools/pruebas.py), mueven el progreso del heroe de la copia a
user_data/dbz3/_c2_respaldo/<fecha> (no se borra nada) y no tocan nunca la carpeta del usuario.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.abspath(os.path.join(AQUI, "..", ".."))
BASE = r"D:\DBZ3HD"
RES = []


def ok(nombre, cond, detalle=""):
    RES.append((nombre, bool(cond), detalle))
    print(("  PASS " if cond else "  FAIL ") + nombre + (("  " + detalle) if detalle and not cond else ""))
    return bool(cond)


def etapa_cac():
    clang = r"C:\Program Files\LLVM\bin\clang++.exe"
    exe = os.path.join(tempfile.gettempdir(), "heroe_e2e_cac_test.exe")
    r = subprocess.run([clang, "-std=gnu++23", "-O0", "-I", os.path.join(RAIZ, "rexglue", "include"), "-I",
                        os.path.join(RAIZ, "src"), os.path.join(AQUI, "e2e", "cac_test.cpp"),
                        os.path.join(RAIZ, "src", "cac", "cac.cpp"), "-o", exe], capture_output=True, text=True,
                       errors="replace")
    if not ok("cac: compila", r.returncode == 0, (r.stderr or "")[-400:]):
        return
    d = os.path.join(tempfile.gettempdir(), "heroe_e2e_cac")
    r = subprocess.run([exe, d], capture_output=True, text=True, errors="replace")
    ok("cac: 3 heroes, v2 y migracion v1->v2", r.returncode == 0 and "cac_test: OK" in r.stdout, r.stdout[-600:])
    # migracion con partidas REALES (copias de cac.toml v1 que escribio el juego en las copias de prueba)
    exe2 = os.path.join(tempfile.gettempdir(), "heroe_e2e_cac_real.exe")
    r = subprocess.run([clang, "-std=gnu++23", "-O0", "-I", os.path.join(RAIZ, "rexglue", "include"), "-I",
                        os.path.join(RAIZ, "src"), os.path.join(AQUI, "e2e", "cac_real.cpp"),
                        os.path.join(RAIZ, "src", "cac", "cac.cpp"), "-o", exe2], capture_output=True, text=True,
                       errors="replace")
    if not ok("cac_real: compila", r.returncode == 0, (r.stderr or "")[-400:]):
        return
    reales = [f for f in glob.glob(os.path.join(BASE, "*", "user_data", "dbz3", "cac.toml"))
              if open(f, encoding="utf8", errors="replace").readline().strip() == "version = 1"]
    if not reales:
        print("  (no hay cac.toml v1 reales en las copias de D:/DBZ3HD: se salta)")
    for k, f in enumerate(reales):
        r = subprocess.run([exe2, f, os.path.join(tempfile.gettempdir(), "heroe_e2e_real_%d" % k)], capture_output=True,
                           text=True, errors="replace")
        ok("cac_real: migra %s" % os.path.relpath(f, BASE).split(os.sep)[0], r.returncode == 0, r.stdout[-400:])


def etapa_cartas():
    juego = os.path.join(tempfile.gettempdir(), "heroe_e2e_cartas")
    shutil.rmtree(juego, ignore_errors=True)
    t0 = time.time()
    r = subprocess.run([sys.executable, "-I", os.path.join(AQUI, "tecnicas_cartas.py"), "--juego", juego, "--caras",
                        "0:476,1:474,7:500,11:487,27:468"], capture_output=True, text=True, errors="replace")
    d = os.path.join(juego, "ingame_ui", "heroe_tecnicas")
    n = len(glob.glob(os.path.join(d, "cap_*.png")))
    ok("cartas: se generan (sin juego del jugador no hay prueba)", r.returncode == 0, (r.stderr or "")[-300:])
    ok("cartas: >= 100 ataques con carta", n >= 100, "n=%d" % n)
    ok("cartas: listo.txt", os.path.exists(os.path.join(d, "listo.txt")))
    try:
        from PIL import Image
        im = Image.open(os.path.join(d, "cap_6.png"))
        ok("cartas: ancho de carta >= 584 (mide sus tiras de texto)", im.width >= 584, "ancho=%d" % im.width)
    except Exception as e:  # noqa: BLE001
        ok("cartas: abre cap_6.png", False, str(e))
    # d: capsula de un mod (ID >= 596): roster.toml con el registro de la #6 como id 700 -> cap_700.png
    try:
        sys.path[:0] = [os.path.join(RAIZ, "mod center hd"), AQUI]
        import roster_build as rb
        import capsulas as cp
        w = tempfile.mkdtemp(prefix="e2e_skc_", dir="D:/tmp" if os.path.isdir("D:/tmp") else None)
        rec = cp.skc_records(rb.Afs(os.path.join(rb.us_dir(None), "data_usi.afs"), w).entry(4))[6]
        os.makedirs(os.path.join(juego, "mods", "_roster"), exist_ok=True)
        with open(os.path.join(juego, "mods", "_roster", "roster.toml"), "w") as f:
            f.write('[[capsula]]\nid = 700\nregistro = "%s"\n' % rec.hex())
        subprocess.run([sys.executable, "-I", os.path.join(AQUI, "tecnicas_cartas.py"), "--juego", juego, "--solo", "cap_700"],
                       capture_output=True, text=True, errors="replace")
        ok("cartas: capsula de mod (id 700) tiene carta", os.path.exists(os.path.join(d, "cap_700.png")))
    except Exception as e:  # noqa: BLE001
        ok("cartas: capsula de mod", False, str(e))
    # texto cortado: ningun pixel de texto (oscuro en el cuerpo, claro en la cabecera) pegado al borde derecho de la carta
    try:
        from PIL import Image
        import numpy as np
        cortadas = []
        for f in sorted(glob.glob(os.path.join(d, "cap_*.png"))):
            a = np.array(Image.open(f).convert("RGBA")).astype(int)
            h, w = a.shape[:2]
            lum = a[..., :3].mean(-1)
            franja = slice(w - 14, w - 7)
            cuerpo = (a[112:h - 16, franja, 3] > 200) & (lum[112:h - 16, franja] < 90)
            cab = (a[18:96, franja, 3] > 200) & (lum[18:96, franja] > 215)
            if cuerpo.any() or cab.any():
                cortadas.append(os.path.basename(f))
        ok("cartas: ningun texto cortado en el borde derecho", not cortadas, "cortadas: %s" % cortadas[:8])
    except Exception as e:  # noqa: BLE001
        ok("cartas: comprobar texto cortado", False, str(e))
    print("  (%.1f s)" % (time.time() - t0))


def etapa_arte():
    juego = os.path.join(tempfile.gettempdir(), "heroe_e2e_arte")
    shutil.rmtree(juego, ignore_errors=True)
    r = subprocess.run([sys.executable, "-I", os.path.join(AQUI, "heroe_arte.py"), "--juego", juego],
                       capture_output=True, text=True, errors="replace")
    ok("arte: se genera", r.returncode == 0, (r.stderr or "")[-300:])
    d = os.path.join(juego, "ingame_ui", "heroe_arte")
    for n in ("anillo", "esfera", "agua"):
        ok("arte: %s.png" % n, os.path.exists(os.path.join(d, n + ".png")))
    try:
        from PIL import Image
        import numpy as np
        a = np.array(Image.open(os.path.join(d, "anillo.png")).convert("RGBA")).astype(int)
        m = a[..., 3] > 128
        r_, g_, b_ = a[m][:, 0].mean(), a[m][:, 1].mean(), a[m][:, 2].mean()
        ok("arte: el anillo ya no es cian (rojo > azul)", r_ > b_, "rgb=%.0f %.0f %.0f" % (r_, g_, b_))
    except Exception as e:  # noqa: BLE001
        ok("arte: comprobar tono", False, str(e))


def reiniciar_progreso(copia):
    """Mueve el progreso del heroe de la copia a una carpeta de respaldo (nada se borra)."""
    ud = os.path.join(BASE, copia, "user_data", "dbz3")
    dst = os.path.join(ud, "_c2_respaldo", time.strftime("%Y%m%d-%H%M%S"))
    os.makedirs(dst, exist_ok=True)
    for f in ("rpg.toml", "hub_heroe.toml", "cac.toml", "cac.toml.bak", "cac.toml.v1.bak", "rpg.toml.bak"):
        p = os.path.join(ud, f)
        if os.path.exists(p):
            shutil.move(p, os.path.join(dst, f))


def sembrar_cac(copia, extra=()):
    """Un cac.toml minimo (1 heroe) para que el juego lo LEA y aplique los DBZ3_TEST_HEROE_* (con perfil nuevo no se aplican)."""
    ud = os.path.join(BASE, copia, "user_data", "dbz3")
    with open(os.path.join(ud, "cac.toml"), "w", encoding="utf8") as f:
        f.write("\n".join(["version = 2", "activo = 0", "", "[[heroe]]", 'id = "h000001"', 'nombre = "Semilla"', "progreso = 2", *extra, ""]))


def esperar_montaje(copia, seg=420):
    """El juego lanza vista_heroe/heroe_sagas al confirmar y siguen al cerrarlo: la prueba siguiente leia sus ficheros a medio escribir."""
    fin = time.time() + seg
    cmd = ["powershell", "-NoProfile", "-Command",
           "(Get-CimInstance Win32_Process -Filter \"name like 'python%%'\" | Where-Object { $_.CommandLine -match 'vista_heroe|heroe_sagas_a_du|nombre_levelup|campana.py' -and $_.CommandLine -match '%s' }).Count" % copia]
    while time.time() < fin:
        r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        if r.stdout.strip() in ("", "0"):
            return
        time.sleep(5)
    print("  aviso: montaje de %s sin acabar tras %d s" % (copia, seg))


def correr_escenario(copia, toml):
    esperar_montaje(copia)
    r = subprocess.run([sys.executable, os.path.join(RAIZ, "tools", "pruebas.py"), "correr", copia, toml],
                       capture_output=True, text=True, errors="replace", cwd=RAIZ)
    out = (r.stdout or "") + (r.stderr or "")
    m = re.search(r"(?:capturas|evidencia) y resultados: (\S+)", out)
    carpeta = m.group(1) if m else None
    log = ""
    if carpeta and os.path.exists(os.path.join(carpeta, "dbz3.log")):
        log = open(os.path.join(carpeta, "dbz3.log"), encoding="utf8", errors="replace").read()
    return ("PASS" in out.split("\n")[-4:].__str__() or re.search(r"^PASS ", out, re.M) is not None), out, carpeta, log


def etapa_juego(copia, cual):
    if not os.path.isdir(os.path.join(BASE, copia)):
        ok("juego: existe la copia %s (python tools/pruebas.py prep %s --desde D:\\DBZ3HD\\smoke_a)" % (copia, copia), False)
        return
    # `prep` no copia res/heroe (desbloqueos.toml, sagas.toml): sin ello la copia no tiene desbloqueos de ki ni sagas
    shutil.copytree(os.path.join(RAIZ, "res", "heroe"), os.path.join(BASE, copia, "res", "heroe"), dirs_exist_ok=True)
    if cual in ("fichas", "todo"):
        reiniciar_progreso(copia)
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_fichas.toml"))
        ok("fichas: el escenario termina", paso, out[-300:])
        for pat, n in ((r"heroe nuevo en la ficha 2/3", "crea el 2.o heroe"), (r"heroe nuevo en la ficha 3/3", "crea el 3.o"),
                       (r"Create abierto \(ficha 2,", "A sobre 'Nueva partida' abre la creacion del 2.o"),
                       (r"Create abierto \(ficha 3,", "y la del 3.o"),
                       (r"maximo 3 heroes", "el 4.o no cabe")):
            ok("fichas: " + n, re.search(pat, log) is not None)
        cac = os.path.join(BASE, copia, "user_data", "dbz3", "cac.toml")
        txt = open(cac, encoding="utf8").read() if os.path.exists(cac) else ""
        ids = re.findall(r'^id = "(h[0-9a-f]{6})"', txt, re.M)
        ok("fichas: cac.toml v2 con 3 heroes e ids distintos", txt.startswith("version = 2") and txt.count("[[heroe]]") == 3
           and len(set(ids)) == 3, "heroes=%d ids=%s" % (txt.count("[[heroe]]"), ids))
    ud = os.path.join(BASE, copia, "user_data", "dbz3")
    leer = lambda: open(os.path.join(ud, "cac.toml"), encoding="utf8").read() if os.path.exists(os.path.join(ud, "cac.toml")) else ""
    if cual in ("v1", "todo"):   # C3: partida v1 REAL (rpg.toml que escribio el juego, sin cac.toml) -> ficha 1 en cac.toml v2
        reiniciar_progreso(copia)
        origen = os.path.join(BASE, "luna13_heroe", "user_data", "dbz3", "rpg.toml")
        if not os.path.exists(origen):
            print("  (sin rpg.toml real en luna13_heroe: se salta v1)")
        else:
            shutil.copy2(origen, os.path.join(ud, "rpg.toml"))   # copia de prueba, nunca la partida del usuario
            paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_apariencia.toml"))
            ok("v1: el escenario termina", paso, out[-300:])
            ok("v1: el heroe v1 pasa a la ficha 1", re.search(r"pasan a la ficha 1", log) is not None)
            t = leer()
            ok("v1: cac.toml v2 con progreso = 2", t.startswith("version = 2") and re.search(r"^progreso = 2$", t, re.M) is not None)
    if cual in ("apariencia", "todo"):
        reiniciar_progreso(copia)
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_apariencia.toml"))
        ok("apariencia: el escenario termina", paso, out[-300:])
        ok("apariencia: Create confirma un traje nuevo", re.search(r"Create: confirmar \(traje 2", log) is not None)
        ok("apariencia: cac.toml guarda traje = 1", re.search(r"^traje = 1$", leer(), re.M) is not None)
    if cual in ("nombre", "todo"):
        reiniciar_progreso(copia)
        sembrar_cac(copia)
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_nombre.toml"))
        ok("nombre: la rejilla escribe y acepta", paso and "nombre = BC" in log, out[-200:])
        ok("nombre: cac.toml guarda el nombre con acento", re.search(r'^nombre = "BCÑ"$', open(os.path.join(BASE, copia, "user_data", "dbz3", "cac.toml"), encoding="utf8").read(), re.M) is not None)
    if cual in ("deseo", "todo"):
        reiniciar_progreso(copia)
        sembrar_cac(copia)
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_deseo_cerrado.toml"))
        ok("deseo: sin deseo la raza queda cerrada", paso and "raza/cuerpo cerrados" in log, out[-200:])
        ok("deseo: cac.toml sigue con raza = 0", re.search(r"^raza = 0$", leer(), re.M) is not None)
        # el candado tal cual lo vive el jugador: ninguna variable de prueba, solo la ficha guardada con combates ganados
        sembrar_cac(copia, ["exp = 3", "saga = 1", "raza = 0"])
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_candado_real.toml"))
        ok("candado real: sin variables de prueba y con aventura empezada, raza cerrada", paso and "raza/cuerpo cerrados" in log, out[-200:])
        ok("candado real: cac.toml sigue con raza = 0", re.search(r"^raza = 0$", leer(), re.M) is not None)
        reiniciar_progreso(copia)
        sembrar_cac(copia, ["exp = 3", "saga = 8", "raza = 0", "deseos = 1"])
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_candado_real_deseo.toml"))
        ok("candado real: con un deseo en la ficha se abre sin variables", paso and "deseo concedido" in log, out[-200:])
        reiniciar_progreso(copia)
        reiniciar_progreso(copia)
        sembrar_cac(copia)
        antes = set(glob.glob(os.path.join(ud, "cac.toml.deseo-*.bak")))
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_deseo_abierto.toml"))
        ok("deseo: con 7 Esferas se concede y cambia la raza", paso and "deseo concedido" in log, out[-200:])
        t = leer()
        ok("deseo: la raza nueva persiste", re.search(r"^raza = ([1-9])$", t, re.M) is not None, "")
        ok("deseo: gastado (deseos = 0)", re.search(r"^deseos = 0$", t, re.M) is not None)
        ok("deseo: copia cac.toml.deseo-*.bak", bool(set(glob.glob(os.path.join(ud, "cac.toml.deseo-*.bak"))) - antes))
    if cual in ("tecnicas", "todo"):
        reiniciar_progreso(copia)
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_tecnicas.toml"))
        ok("tecnicas: el escenario termina", paso, out[-300:])
        ok("tecnicas: catalogo >= 90 ataques", re.search(r"catalogo de tecnicas: (\d+) ataques", log) is not None
           and int(re.search(r"catalogo de tecnicas: (\d+) ataques", log).group(1)) >= 90)
        eq = re.findall(r"tecnica equipada: hueco (\d) = capsula (\d+)", log)
        ok("tecnicas: equipa 2 cartas distintas", len(eq) >= 2 and eq[-2][1] != eq[-1][1] and eq[-1][1] != "0", str(eq))
        m = re.search(r"tecnicas equipadas en el combate: \+(\d+) \(equipo (\d+) (\d+) (\d+) (\d+); donante \d+; capsulas ([\d ]+)\)", log)
        ok("tecnicas: el combate carga las capsulas equipadas", m is not None and int(m.group(1)) >= 2, "")
        if m and eq:
            caps = m.group(6).split()
            ok("tecnicas: las capsulas del combate incluyen las elegidas", all(e[1] in caps for e in eq[-2:]), str(caps))
        cac = os.path.join(BASE, copia, "user_data", "dbz3", "cac.toml")
        txt = open(cac, encoding="utf8").read() if os.path.exists(cac) else ""
        ok("tecnicas: cac.toml guarda equipo e inventario", "equipo = [" in txt and "inventario = [" in txt)
    mods = os.path.join(BASE, copia, "mods")
    if cual in ("otro", "todo"):   # C4 (C2 #3): tecnica de otro donante -> aviso Si/No; con Si cambia el moveset
        reiniciar_progreso(copia)
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_tecnicas_otro.toml"))
        ok("otro: el escenario termina", paso, out[-300:])
        ok("otro: con Si el moveset pasa al donante de la tecnica", re.search(r"moveset cambiado al donante \d+ \(Si\)", log) is not None)
        m = re.search(r"moveset cambiado al donante (\d+) \(Si\)", log)
        c = re.search(r"moveset (\d+) \(.*\) para el estilo 0 en este combate", log)
        ok("otro: el combate siguiente usa ese moveset", m is not None and c is not None and m.group(1) == c.group(1),
           "%s / %s" % (m and m.group(1), c and c.group(1)))
    if cual in ("moveset", "todo"):   # C8m: cambiar de moveset en el apartado -> el siguiente combate lo usa
        reiniciar_progreso(copia)
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_moveset_cambio.toml"))
        ok("moveset: el escenario termina", paso, out[-300:])
        m = re.search(r"moveset elegido (\d+) \(", log)
        ok("moveset: el apartado elige otro moveset", m is not None)
        c = re.search(r"moveset (\d+) \(.*\) para el estilo 0 en este combate", log)
        ok("moveset: el combate usa el moveset elegido", c is not None and m is not None and c.group(1) == m.group(1),
           "%s / %s" % (m and m.group(1), c and c.group(1)))
    if cual in ("ki", "todo"):   # C4: color de ki desbloqueado -> cac.toml y personaje.toml del heroe propio
        reiniciar_progreso(copia)
        sembrar_cac(copia)
        cac = os.path.join(ud, "cac.toml")
        t = open(cac, encoding="utf8").read().replace("progreso = 2", 'progreso = 2\ndesbloqueos = ["ki_dorado_goku"]', 1)
        open(cac, "w", encoding="utf8").write(t)
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_ki.toml"))
        ok("ki: el escenario termina", paso, out[-300:])
        ok("ki: cac.toml guarda ki_col", re.search(r'^ki_col = "#F0C020"$', leer(), re.M) is not None)
        pt = os.path.join(mods, "heroe_propio", "personaje.toml")
        ok("ki: personaje.toml del heroe propio lleva ki_color", os.path.exists(pt) and 'ki_color = "#F0C020"' in open(pt, encoding="utf8").read())
    if cual in ("xv2", "todo"):
        shutil.copytree(os.path.join(RAIZ, "mod center hd", "xv2"), os.path.join(BASE, copia, "mod center hd", "xv2"), dirs_exist_ok=True)   # prep no la copia
        shutil.copy2(os.path.join(RAIZ, "mod center hd", "cac", "vista_heroe.py"), os.path.join(BASE, copia, "mod center hd", "cac", "vista_heroe.py"))   # C4: raza XV2 + piezas + color; se monta el heroe propio con xv2=
        reiniciar_progreso(copia)
        sembrar_cac(copia)
        ft0 = os.path.join(mods, "heroe_propio", "ficha.txt")
        if os.path.exists(ft0):   # no se borra: a respaldo (si no, un ficha.txt viejo daria un falso PASS)
            os.makedirs(os.path.join(ud, "_c2_respaldo"), exist_ok=True)
            shutil.move(ft0, os.path.join(ud, "_c2_respaldo", "ficha_%s.txt" % time.strftime("%H%M%S")))
        paso, out, carpeta, log = correr_escenario(copia, os.path.join(AQUI, "e2e", "heroe_xv2.toml"))
        ok("xv2: el escenario termina", paso, out[-300:])
        ok("xv2: se eligen pelo, cara y torso de XV2", all(re.search(q, log) for q in (r"XV2: Hair = modelo", r"XV2: Face = modelo", r"XV2: Torso = modelo")))
        t = leer()
        ok("xv2: cac.toml guarda raza 7, cuerpo 110 y las piezas", re.search(r"^raza = 7$", t, re.M) and re.search(r"^cuerpo = 110$", t, re.M)
           and re.search(r"^xv2 = \[", t, re.M) is not None)
        ft = os.path.join(mods, "heroe_propio", "ficha.txt")
        ok("xv2: se monta el heroe propio con las piezas", os.path.exists(ft) and "xv2=bust:" in open(ft, encoding="utf8").read())
        reiniciar_progreso(copia)   # la copia vuelve a un heroe nativo


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solo", default="todo")
    ap.add_argument("--copia", default="s5_c2")
    ap.add_argument("--repetir", type=int, default=1)
    a = ap.parse_args()
    for vuelta in range(a.repetir):
        print("== heroe_e2e vuelta %d/%d ==" % (vuelta + 1, a.repetir))
        if a.solo in ("cac", "todo"):
            etapa_cac()
        if a.solo in ("cartas", "todo"):
            etapa_cartas()
        if a.solo in ("arte", "todo"):
            etapa_arte()
        if a.solo == "du":
            r = subprocess.run([sys.executable, os.path.join(AQUI, "heroe_du_e2e.py"), "--copia", "s5_c3"], capture_output=True,
                               text=True, errors="replace")
            m = re.search(r"heroe_du_e2e: (\d+)/(\d+) PASS", r.stdout)
            print(r.stdout[-1800:])
            ok("du: heroe_du_e2e.py (%s)" % (m.group(0) if m else "sin resultado"), bool(m) and m.group(1) == m.group(2), r.stderr[-300:])
        if a.solo in ("fichas", "v1", "tecnicas", "apariencia", "deseo", "nombre", "otro", "moveset", "ki", "xv2", "todo"):
            etapa_juego(a.copia, a.solo)
    fallos = [r for r in RES if not r[1]]
    print("heroe_e2e: %d pruebas, %d fallos" % (len(RES), len(fallos)))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
