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
import math
import os
import re
import struct
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
            for _ in range(5):   # cadena medida de un jugador: ver _hasta_libre
                j.tecla("Space", 3000)
            for _ in range(6):
                j.tecla("D", 400)
            j.tecla("Space", 2500)
            j.tecla("Space", 2500)
            if j.pantalla() == 10:
                j.tecla("Backspace", 1500)
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


FILA_RAZA = False   # --fila-raza: elige la ultima fila del menu de deseos ("cambiar raza y cuerpo")
SALIDA = False   # --salida: sigue tras el deseo por los creditos y el dialogo de guardar


def escenario_shenron(j, par):
    """La llegada al mapa salta al final del Heroe (DBZ3_TEST_HEROE_FINAL) con las 7 Esferas: Shenron nativo, deseo y creditos."""
    j.log(r"escena 102 entrada 35", 90)   # sin teclas hasta entonces: la entrada directa pulsa sola y un A de mas la descarrila
    if not hasta(j, r"se lanza la escena 1 entrada 39", 120):
        raise P.Fallo("no llega al final 1.39")
    t0 = time.time()
    while j.pantalla() != 11 and time.time() - t0 < 60:     # el guion del final llama a la nativa 0x115 (sub_8215C1B0)
        j.tecla("Space", 1500)
    time.sleep(14)
    j.captura("shenron_1_escena")
    t0 = time.time()
    while j.pantalla() == 11 and time.time() - t0 < 120:    # textos y menu de deseos: con --fila-raza baja a la ultima fila; si no, la primera
        if FILA_RAZA and "menu de deseos" in j.texto_log() and not getattr(escenario_shenron, "foto", False):
            escenario_shenron.foto = True
            time.sleep(2)
            j.captura("shenron_1a_menu_deseos")
            j.tecla("S", 1000)
            j.tecla("S", 1000)
            j.captura("shenron_1b_fila_raza")
        j.tecla("Space", 2000)
    j.captura("shenron_2_tras_deseo")
    if SALIDA:
        _salida(j)
    time.sleep(2)


def _salida(j):
    """Creditos (unos 6 min), "sobrescribir datos?" y vuelta: debe llegar al titulo de DU y de ahi al menu principal."""
    if True:
        t0 = time.time()
        dialogo = False
        while j.pantalla() == 12 and time.time() - t0 < 600:
            if not dialogo and time.time() - t0 > 330:
                dialogo = True
                j.captura("shenron_3_final_creditos")
            if dialogo:   # "sobrescribir datos?": el cursor sale en No y No vuelve a preguntar; arriba = Si (solo la copia de pruebas)
                j.tecla("W", 600)
            j.tecla("Space", 2000)
        j.captura("shenron_4_tras_guardar")
        j.info("tras los creditos: pantalla %s" % j.pantalla())
        t0 = time.time()
        while j.pantalla() != 5 and time.time() - t0 < 60:
            j.tecla("P", 2500)
        j.captura("shenron_5_menu")
        j.info("tras volver: pantalla %s" % j.pantalla())


def escenario_puntos(j, par):
    """Saga 1 (Namek): se tocan los puntos del mapa uno a uno (del ultimo hueco al primero): maestro, Esfera, objeto, opcional y luego el combate."""
    j.log(r"escena 102 entrada %d" % CAS, 90)
    if not hasta(j, r"escena %d entrada 0" % (5100 + CAS), 90):
        raise P.Fallo("no llega al mapa (escena %d.0)" % (5100 + CAS))
    time.sleep(5)
    j.captura("puntos_0_mapa_todos")
    vistos = 0
    t0 = ultimo = time.time()
    while time.time() - t0 < 1200 and vistos < 5:
        if time.time() - ultimo > 240:   # ninguna marca nueva: fallo con motivo, no espera sin fin
            raise P.Fallo("%d marcas tocadas y 4 min sin otra" % vistos)
        tx = j.texto_log()
        n = len(re.findall(r"se toca la marca", tx))
        if n > vistos:
            vistos = n; ultimo = time.time()
            m = re.findall(r"se toca la marca de combate (\d+) entrada (\d+)", tx)[-1]
            j.captura("puntos_%d_%s_%s" % (n, m[0], m[1]))
        if j.pantalla() == 10:
            for _ in range(4):
                j.tecla("D", 450)
            j.tecla("Space", 2500)
        else:
            j.tecla("Space", 2500)
    hasta(j, r"dbz3 heroe: DU saga 0 combate 0 ganado", 300)
    j.captura("puntos_9_final")
    time.sleep(2)


KMAP = 0x82479A60   # tarea del mapa: +180 registro del jugador, +216 estado (0 = sin evento)
NO_TOCADAS = []
FALLOS_A = {}
VISTO = {}          # saga -> palabras de marca tocadas ("codigo.entrada")
FOTOS = set()
TURBO = 1


def _mem(j, a, n):
    return P.leer(j.h, P.GBASE + a, n) or bytes(n)


def _u32(j, a):
    return struct.unpack(">I", _mem(j, a, 4))[0]


def _pos(j, rec):
    return struct.unpack(">3f", _mem(j, rec + 4544, 12))


def _marcas(j, rec):
    """[(direccion, palabra, icono, x, z)] de las marcas vivas del mapa (64 B cada una desde rec+128; posicion en +16)."""
    out = []
    for b in range(rec + 128, rec + 128 + 64 * 64, 64):
        d = _mem(j, b, 32)
        w, ico = struct.unpack(">II", d[:8])
        if w not in (0, 0xFFFFFFFF) and not w & 0x8000:   # 0x8000 en la entrada: el punto ya hecho (combate opcional repetible)
            x, _, z = struct.unpack(">3f", d[16:28])
            out.append((b, w, ico, x, z))
    return out


def _periodo(j, rec):
    """El mapa da la vuelta: periodo = 4000 * tabla por planeta (0x82373E2C, medido en sub_821C2B80 y sub_821BCC58)."""
    return 4000.0 * max(1, _mem(j, 0x82373E2C + _mem(j, rec + 4576, 1)[0], 1)[0])


def _rumbo(j, rec, x, z, per):
    """(distancia, error de rumbo en unidades de yaw) hacia (x, z)."""
    px, _y, pz = _pos(j, rec)
    dx = (x - px + per / 2) % per - per / 2
    dz = (z - pz + per / 2) % per - per / 2
    yaw = struct.unpack(">h", _mem(j, rec + 4562, 2))[0]
    return math.hypot(dx, dz), (round(math.atan2(-dx, -dz) / (2 * math.pi) * 65536) - yaw + 32768) % 65536 - 32768


def _pulso(j, tecla, ms):
    j.tecla("%s %d" % (tecla, ms), 0)
    time.sleep(ms / 1000 + 0.3)


def _volar(j, rec, x, z, foto=None, lejos=None):
    """Vuela con la palanca como un jugador: A/D giran (yaw s16 en rec+4562; rumbo = (-sen, -cos)) y W avanza; se para DELANTE de la marca,
    a 450-850 y mirandola: A solo la toca si esta en cuadro (sub_821C2B80: sub_82084FB0), no debajo del heroe."""
    per, giro, vel = _periodo(j, rec), 11400.0 * TURBO, 1300.0 * TURBO
    quieto, ant = 0, None
    for _ in range(150):
        d, err = _rumbo(j, rec, x, z, per)
        quieto = quieto + 1 if ant == (d, err) else 0
        ant = (d, err)
        if quieto >= 3:   # la palanca no hace nada: hay un dialogo abierto
            return None
        if lejos and d < 2500 and abs(err) < 900:   # a distancia de juego
            time.sleep(0.5)
            j.captura(lejos)
            lejos = None
        if d < 450:   # demasiado cerca: se aleja recto y vuelve
            _pulso(j, "W", int(min(1500, 1200 / vel * 1000)))
            continue
        if abs(err) > 900:
            ms = int(min(1500, max(50, abs(err) / giro * 1000)))
            _pulso(j, "A" if err > 0 else "D", ms)
            mov = abs(err - _rumbo(j, rec, x, z, per)[1])
            if 300 < mov < 30000:
                giro = 0.5 * giro + 0.5 * mov / (ms / 1000)
            continue
        if d < 850:
            time.sleep(0.8)   # que la camara acabe de girar
            if foto:
                j.captura(foto)
            return True
        ms = int(min(2500, max(60, (d - 650) / vel * 1000)))
        _pulso(j, "W", ms)
        d2 = _rumbo(j, rec, x, z, per)[0]
        if abs(d - d2) > 50:
            vel = 0.5 * vel + 0.5 * abs(d - d2) / (ms / 1000)
    return False


def _libre(j, rec):
    """Mapa sin evento ni dialogo: estado 0 y la palanca mueve al heroe (con dialogo abierto no se mueve)."""
    if _u32(j, KMAP + 216) != 0:
        return False
    for _ in range(2):   # dos veces: al recargar el mapa el heroe salta a su salida sin que la palanca haga nada
        p0 = _pos(j, rec)
        j.tecla("W 250", 0)
        time.sleep(0.6)
        if math.hypot(*(a - b for a, b in zip(_pos(j, rec), p0))) <= 5:
            return False
    return True


def _hasta_libre(j, rec, seg=600):
    """Pasa charlas, combate, resultados, nivel y Guardar como un jugador hasta volver a volar. False si sale Shenron."""
    t0 = time.time()
    while time.time() - t0 < seg:
        if j.pantalla() == 11:
            return False
        n_guardar = len(re.findall(r"se lanza la escena 16384 entrada 4", j.texto_log()))
        if n_guardar > getattr(hasta, "guardados", 0):
            hasta.guardados = n_guardar
            time.sleep(2.0)
            for _ in range(2):
                j.tecla("W", 600)
                j.tecla("Space", 2500)
            continue
        if j.pantalla() == 10:
            # MEDIDO (20261031-201048_opus2_heroe_nivel): el juego va bien; era el guion. Cadena real de un jugador: A, A = resultado;
            # A = cartel "Sube nvl"; A = sale "Has obtenido puntos Z"; A = lo cierra; D = reparte (Salud +1 por D); A = "Esta bien asi?";
            # A = Si; Contrasena (B). Teclear D antes de cerrar el aviso se pierde, y B tras A deshace el reparto.
            hasta.en10 = getattr(hasta, "en10", 0) + 1
            for _ in range(5):
                j.tecla("Space", 3000)
            for _ in range(6):
                j.tecla("D", 400)
            j.tecla("Space", 2500)
            j.tecla("Space", 2500)
            if j.pantalla() == 10:
                j.tecla("Backspace", 1500)
            continue
        hasta.en10 = 0
        if _libre(j, _u32(j, KMAP + 180) or rec):
            return True
        j.tecla("Space", 1500)
    raise P.Fallo("%d s sin volver al mapa (estado %d, pantalla %s)" % (seg, _u32(j, KMAP + 216), j.pantalla()))


def _saga(tx):
    """Saga en curso (1-8) segun el ultimo combate ganado."""
    m = re.findall(r"saga siguiente (\d+)", tx)
    return int(m[-1]) + 1 if m else 1


def _nom(w):
    return "%d.%d" % (w >> 16, w & 0xFFFF)


def escenario_completa(j, par):
    """Partida entera con pasos de jugador: sagas 1-8, todos los puntos de cada una y el combate principal al final, hasta Shenron."""
    j.log(r"escena 102 entrada %d" % CAS, 150)
    hasta(j, r"escena %d entrada 0" % (5100 + CAS), 90)
    rec = _u32(j, KMAP + 180)
    fin = time.time() + 4 * 3600
    if not _hasta_libre(j, rec):
        raise P.Fallo("Shenron antes de jugar")
    r1 = False
    while time.time() < fin:
        rec = _u32(j, KMAP + 180)
        s = _saga(j.texto_log())
        visto = VISTO.setdefault(s, [])
        vivas = _marcas(j, rec)
        if not r1:   # R1 (modo busqueda): el busto de la casilla
            r1 = True
            j.tecla("3", 0)
            for n in range(3):
                time.sleep(0.5 + 0.2 * n)
                j.captura("r1_busto_%d" % n)
            time.sleep(4)
            j.tecla("3", 0)   # R1 otra vez: vuelve a volar
            time.sleep(3)
        resto = [m for m in vivas if _nom(m[1]) not in visto and m[2] != 1] or [m for m in vivas if m[2] == 1 and _nom(m[1]) not in visto]
        if not resto:
            j.info("saga %d: sin marcas nuevas: %s" % (s, [(_nom(m[1]), m[2]) for m in vivas]))
            raise P.Fallo("saga %d sin marcas por tocar" % s)
        b, w, ico, x, z = resto[0]
        foto = "color_icono_%d" % ico if ico not in FOTOS else None
        FOTOS.add(ico)
        j.info("saga %d: voy a %s icono %d (%d, %d) desde %s" % (s, _nom(w), ico, x, z, _pos(j, rec)))
        llega = _volar(j, rec, x, z, foto=foto)
        if llega is None:
            j.tecla("Space", 1500)
            if not _hasta_libre(j, rec, 900):
                break
            continue
        if not llega:
            raise P.Fallo("no llego a la marca %s" % _nom(w))
        time.sleep(0.5)   # A solo vale con la palanca en reposo (sub_821C4DA0)
        for _ in range(4):
            j.tecla("Space", 1200)
            if _u32(j, KMAP + 216) != 0 or _u32(j, b) != w:
                break
        else:
            FALLOS_A[_nom(w)] = FALLOS_A.get(_nom(w), 0) + 1
            j.info("saga %d: la marca %s no responde a A (%d): pos %s marca %s" % (s, _nom(w), FALLOS_A[_nom(w)], _pos(j, rec), _mem(j, b, 64).hex()))
            if FALLOS_A[_nom(w)] < 3:
                if not _hasta_libre(j, rec, 900):
                    break
                continue
            NO_TOCADAS.append((s, _nom(w)))
        visto.append(_nom(w))
        if not _hasta_libre(j, rec, 900):
            break
    else:
        raise P.Fallo("sin Shenron en 4 h")
    j.captura("final_pantalla_11")
    time.sleep(14)
    j.captura("shenron_1_escena")
    t0 = time.time()
    while j.pantalla() == 11 and time.time() - t0 < 180:
        if "menu de deseos" in j.texto_log() and not getattr(escenario_completa, "foto", False):
            escenario_completa.foto = True
            time.sleep(2)
            j.captura("shenron_1a_menu_deseos")
            j.tecla("S", 1000)
            j.tecla("S", 1000)
            j.captura("shenron_1b_fila_raza")
        j.tecla("Space", 2000)
    j.captura("shenron_2_tras_deseo")
    if SALIDA:
        _salida(j)
    time.sleep(2)


def escenario_colores(j, par):
    """Vuela a cada marca de la saga 1, se para delante mirandola y fotografia: normal y con R1 (modo busqueda)."""
    j.log(r"escena 102 entrada %d" % CAS, 150)
    hasta(j, r"escena %d entrada 0" % (5100 + CAS), 90)
    rec = _u32(j, KMAP + 180)
    _hasta_libre(j, rec)
    for b, w, ico, x, z in sorted(_marcas(j, rec), key=lambda m: m[2], reverse=True):
        if not _volar(j, rec, x, z, lejos="marca_%d_lejos" % ico):
            raise P.Fallo("no llego a %s" % _nom(w))
        j.captura("marca_%d_normal" % ico)
        j.tecla("3", 0)
        time.sleep(3)
        j.captura("marca_%d_r1" % ico)
        time.sleep(3)
        j.captura("marca_%d_r1b" % ico)
        j.tecla("3", 0)
        time.sleep(3)


def main_completa(a):
    if a.colores:
        P.ESCENARIOS["heroe_colores"] = ("Heroe: marcas de la saga 1, normal y con R1", lambda p: {}, escenario_colores)
        rc = P.correr(a.copia, "heroe_colores", env_extra=["DBZ3_TEST_DU_HEROE=%d" % CAS], instantanea=a.instantanea, forzar=True)
        print("capturas:", rc["run"], rc["ok"], rc.get("msg", ""))
        return 0 if rc["ok"] else 1
    import tomllib  # noqa: PLC0415
    global TURBO
    TURBO = a.turbo
    P.ESCENARIOS["heroe_completa"] = ("Heroe: partida completa, sagas 1-8 hasta Shenron", lambda p: {}, escenario_completa)
    env = ["DBZ3_TEST_DU_HEROE=%d" % CAS, "DBZ3_TEST_RINGOUT=1", "DBZ3_LEVELUP_DIAG=1", "DBZ3_TEST_DU_ESTADOS=1"]
    inst = None if a.instantanea == "ninguna" else a.instantanea
    if a.real:   # ficha REAL: Crear > Confirmar como un jugador (el juego monta solo la historia), sin FORZAR; creditos y guardado al final
        global SALIDA
        SALIDA = True
        P.ESCENARIOS["heroe_nombre_1"] = ("Nombre: elegirlo como jugador", lambda p: {}, escenario_nombre_1)
        for n in ("historia_heroe_35", "historia_heroe_36", "historia_heroe_37", "heroe_nombres"):
            d = os.path.join(str(P.BASE), a.copia, "mods", n)
            if os.path.isdir(d):
                P.mover(P.Path(d), P.RESPALDO / ("%s_%s_%s" % (P.ts(), a.copia, n)))
        rc1 = P.correr(a.copia, "heroe_nombre_1", env_extra=(["REX_DBZ3_LANGUAGE=%d" % a.idioma] if a.idioma else []), instantanea=inst, forzar=True)
        ok("ficha real: Crear > Confirmar sin caida", rc1["ok"], rc1.get("msg", ""))
        import heroe_e2e  # noqa: PLC0415
        heroe_e2e.esperar_montaje(a.copia)
        inst = None
    else:
        env.append("DBZ3_TEST_DU_HEROE_FORZAR=1")   # la copia entra directa, sin ficha confirmada: sin esto las Esferas creen que no hay DU y dan el deseo al llegar a 7
    if a.idioma:
        env.append("REX_DBZ3_LANGUAGE=%d" % a.idioma)
    if TURBO > 1:
        env.append("DBZ3_TEST_TURBO=%d" % TURBO)
    rc = P.correr(a.copia, "heroe_completa", env_extra=env, instantanea=inst, forzar=True)
    log = open(os.path.join(rc["run"], "dbz3.log"), encoding="utf-8", errors="replace").read()
    print("capturas:", rc["run"])
    for s in sorted(VISTO):
        print("  saga %d: %s" % (s, " ".join(VISTO[s])))
    ok("escenario terminado sin caida", rc["ok"], rc.get("msg", ""))
    cfg = tomllib.load(open(os.path.join(RAIZ, "mod center hd", "campanas", "historia_heroe_%d.toml" % CAS), "rb"))
    for p in cfg["punto_mapa"]:
        s, pal = int(p["saga"]) + 1, str(p["palabra"])
        ok("saga %d: punto %s jugado" % (s, pal), pal in VISTO.get(s, []))
    ganados = re.findall(r"DU saga (\d+) combate (\d+) ganado", log)
    ok("los 27 combates principales ganados, en orden de saga", len(ganados) >= 27 and [int(g[0]) for g in ganados] == sorted(int(g[0]) for g in ganados), str(len(ganados)))
    ok("las 8 sagas seguidas", {int(g[0]) for g in ganados} == set(range(8)))
    ok("sagas con maestro: entrenados los 6", len(set(re.findall(r"entrenamiento con el maestro (\d+) ganado", log))) >= 6)
    ok("todas las marcas responden a A", not NO_TOCADAS, str(NO_TOCADAS))
    ok("7 Esferas", "(7/7)" in log)
    ok("aparece Shenron", "aparece Shenron" in log)
    ok("fila de raza: deseo concedido", "Shenron concede el deseo de raza" in log and re.search(r"deseo elegido: fila 3 de 3", log))
    ok("rotulo propio al subir de nivel", re.search(r"id 6[234] \(banco n 68\)", log))
    if a.real:
        ok("tras el deseo, creditos nativos (pantalla 12) y de ahi al menu principal (5)", re.search(r"pantalla 12 .*[\s\S]*du-estados: pantalla 5 ", log))
        ok("la 7.a Esfera no dio el deseo sin Shenron (DU reconocido por la ficha real)", "aparece Shenron" in log and "FORZAR" not in log)
        cac = open(os.path.join(str(P.BASE), a.copia, "user_data", "dbz3", "cac.toml"), encoding="utf8").read()
        ok("el guardado conserva el deseo (deseos = 1) y las Esferas gastadas", re.search(r"^deseos = 1$", cac, re.M) and re.search(r"^esferas = 0$", cac, re.M))
    fallos = [n for n, c, _ in RES if not c]
    print("heroe_du_e2e --completa: %d/%d PASS" % (len(RES) - len(fallos), len(RES)))
    return 1 if fallos else 0


def main_capsula(a):
    """Freezer (historia de mod): la capsula del combate entra de verdad en el inventario y el New! del guion sale solo si no la tenias.
    El juego: evento 19 de #LST (sub_8215D9A8): lista [obj+264] de cartas y bandera New! = (cuenta == 0); obj = [0x82439CC8], cuenta = 0x824A58C9 + capsula."""
    import threading  # noqa: PLC0415
    import ctypes  # noqa: PLC0415
    fallos_ini = len(RES)
    base_cuenta = 0x824A58C9

    def corrida(nombre, tener):
        muestras = {}

        def esc(j, par):
            vivo = [True]

            def vigila():   # lo de antes del combate es de la carga: solo cuenta tras acabar el combate
                vistas = set()
                puesto = False
                while vivo[0]:
                    o = _u32(j, 0x82439CC8)
                    n = _u32(j, o + 264) if o and "combate acabado" in j.texto_log() else 0
                    if not puesto and "combate acabado" in j.texto_log():
                        puesto = True
                        for cap in (96, 97, 98, 100, 101, 102, 405, 432):   # antes del guion de victoria: la tienes (1) o no (0)
                            got = ctypes.c_size_t()
                            hw = P.K32.OpenProcess(0x28, False, j.proc.pid)   # VM_OPERATION | VM_WRITE: el de j.h solo lee
                            P.K32.WriteProcessMemory(ctypes.c_void_p(hw), ctypes.c_void_p(P.GBASE + base_cuenta + cap + P.DESPL), b"\x01" if tener else b"\x00", 1, ctypes.byref(got))
                        continue
                    if 0 < n <= 4:
                        for i in range(n):
                            cap, new = _u32(j, o + (68 + i) * 4), _u32(j, o + (72 + i) * 4)
                            if 0 < cap < 2048 and (cap, new) not in vistas:
                                vistas.add((cap, new))
                                muestras[cap] = (new, _mem(j, base_cuenta + cap, 1)[0])
                                j.info("carta: capsula %d New %d cuenta %d" % (cap, new, muestras[cap][1]))
                    time.sleep(0.1)
            threading.Thread(target=vigila, daemon=True).start()
            try:
                P._du_continuar(j, {"hasta_combate": "1", "iter": "60"})
                t0 = time.time()
                fotos = 0
                while time.time() - t0 < 60 and not (muestras and fotos >= 9):
                    if muestras:
                        fotos += 1
                        j.captura("carta_%d" % fotos)
                    j.tecla("Space", 1500)
                time.sleep(2)
            finally:
                vivo[0] = False
        P.ESCENARIOS[nombre] = ("Freezer: capsula de premio", lambda p: {"DBZ3_TEST_RINGOUT": "1"}, esc)
        rc = P.correr(a.copia, nombre, instantanea="freezer_cap2", forzar=True)
        print("capturas:", rc["run"], muestras)
        ok(nombre + " sin caida", rc["ok"], rc.get("msg", ""))
        return muestras
    nueva = corrida("heroefin_cap_nueva", False)
    ok("capsula que no tenias: carta con New! y cuenta 0 -> 1", any(v[0] == 1 and v[1] in (0, 1) for v in nueva.values()), str(nueva))
    # la bandera se pone antes de sumar (sub_8215D9A8 -> sub_82158778); con cuenta previa 1, sin New! y suma
    otra = corrida("heroefin_cap_tenida", True)
    ok("capsula ya obtenida: carta sin New! y cuenta 1 -> 2", any(v[0] == 0 and v[1] in (1, 2) for v in otra.values()) and not any(v[0] for v in otra.values()), str(otra))
    fallos = [n for n, c, _ in RES[fallos_ini:] if not c]
    print("heroe_du_e2e --capsula: %d/%d PASS" % (len(RES) - fallos_ini - len(fallos), len(RES) - fallos_ini))
    return 1 if fallos else 0


def main_puntos(a):
    P.ESCENARIOS["heroe_puntos"] = ("Heroe: puntos del mapa por tipo", lambda p: {}, escenario_puntos)
    env = ["DBZ3_TEST_DU_HEROE=%d" % CAS, "DBZ3_AUTO_TOUCH=5", "DBZ3_AUTO_TOUCH_ULTIMA=1", "DBZ3_TEST_RINGOUT=1"]
    rc = P.correr(a.copia, "heroe_puntos", env_extra=env, instantanea=None if a.instantanea == "ninguna" else a.instantanea)
    log = open(os.path.join(rc["run"], "dbz3.log"), encoding="utf-8", errors="replace").read()
    print("capturas:", rc["run"])
    for l in re.findall(r"se toca la marca.*|entrenamiento con.*|esfera \d recogida.*|combate acabado.*", log):
        print("   ", l[:110])
    ok("escenario terminado sin caida", rc["ok"], rc.get("msg", ""))
    ok("marca del maestro (entrada 6, icono de combate)", "se toca la marca de combate %d entrada 6" % (5500 + CAS) in log)
    ok("su combate es contra el maestro (ID 0)", re.search(r"combate acabado: id 63 contra id 0, gana el lado 1", log))
    m = re.search(r"entrenamiento con el maestro 0 ganado: (\d+)%", log)
    ok("entrena +40 % (20 % del combate 1 ya jugado + 40 %)", m and m.group(1) == "60", m.group(0) if m else "")
    ok("Esfera del mapa (evento 38 nativo): 1/7 sin acabar la saga", "esfera 1 recogida (1/7)" in log and "ultimo de la saga" not in log)
    ok("objeto del mapa (2100.1)", "se toca la marca de combate 2100 entrada 1" in log)
    ok("combate opcional (2401.0) contra Saibaman con el heroe", "se toca la marca de combate 2401 entrada 0" in log and len(re.findall(r"combate acabado: id 63 contra id 42", log)) >= 2 and "combate acabado: id 41" not in log)
    ok("la cara del punto es del mod (codigo 2100 -> del mod)", re.search(r"codigo 2100 -> contenedor \d+ \(del mod\)", log))
    ok("no avanza la saga con los puntos", "esfera 2 recogida" not in log)
    fallos = [n for n, c, _ in RES if not c]
    print("heroe_du_e2e --puntos: %d/%d PASS" % (len(RES) - len(fallos), len(RES)))
    return 1 if fallos else 0


NAV_HEROE = [("W", 1200), ("Space", 3500)] + [("S", 450)] * 5 + [("Space", 2500)] + [("S", 700)] * 3 + [("Space", 3500)]


def escenario_nombre_1(j, par):
    """Como un jugador: HUB > Kumite > Modo Heroe > Apariencia > Crear > Nombre: escribe BCN con tilde y confirma."""
    j.log(r"menu principal", 60) if False else time.sleep(2.5)
    for k, t in NAV_HEROE:
        j.tecla(k, t)
    j.tecla("S", 500)
    j.tecla("S", 500)
    j.tecla("Space", 3000)
    j.log(r"Create abierto", 20)
    j.tecla("W", 500)
    j.tecla("A", 500)
    j.tecla("Space", 1500)
    for _ in range(16):
        j.tecla("L", 300)
    for k in ["D", "Space", "D", "Space", "S", "S", "D", "D", "D", "D", "Space"]:
        j.tecla(k, 350)
    j.captura("nombre_1_rejilla")
    j.tecla("P", 1200)
    j.log(r"nombre = BC", 20)
    j.tecla("D", 500)
    j.tecla("Space", 3000)
    j.log(r"Create: confirmar", 20)
    time.sleep(3)


def escenario_nombre_2(j, par):
    """Reiniciado: Modo Heroe > Empezar > mapa > A, como un jugador; charla, combate y subida de nivel."""
    time.sleep(2.5)
    for k, t in NAV_HEROE:
        j.tecla(k, t)
    for _ in range(9):
        j.tecla("S", 500)
    j.captura("nombre_2_empezar")
    j.tecla("D", 1500)
    j.captura("nombre_3_mapa")
    j.tecla("Space", 3000)
    j.log(r"el Heroe entra en el mapa de Dragon Universe", 20)
    j.log(r"escena 102 entrada 35", 90)
    if not hasta(j, r"escena 5535 entrada 0", 150):
        raise P.Fallo("no llega al primer combate")
    j.log(r"escena 5935 entrada 0", 60)   # la charla antes del combate: la 2.a pagina es la del heroe con su nombre
    time.sleep(7)
    for n in range(3):
        j.captura("nombre_4_charla_%d" % n)
        j.tecla("Space", 3500)
    if not hasta(j, r"tag: combate acabado", 200):
        raise P.Fallo("no acaba el combate")
    t0 = time.time()
    while j.pantalla() != 10 and time.time() - t0 < 90:
        j.tecla("Space", 1500)
    time.sleep(2.5)
    j.captura("nombre_5_rotulo")
    time.sleep(2)


def escenario_aviso(j, par):
    """Sin Python (DBZ3_TEST_SIN_PYTHON): Crear > Confirmar monta y falla; al pulsar Empezar el jugador ve el aviso, no las misiones."""
    escenario_nombre_1(j, par)
    time.sleep(2)
    for _ in range(2):   # Apariencia -> Piezas -> Empezar
        j.tecla("S", 600)
    j.captura("aviso_0_empezar")
    j.tecla("Space", 3000)   # lanza el montaje (falla al instante) y entra en las misiones
    j.tecla("Backspace", 2500)   # vuelve a la pantalla del heroe: el aviso sigue ahi
    j.captura("aviso_1")
    time.sleep(1)


def escenario_nivel(j, par):
    """Como escenario_nombre_2 hasta el primer combate; en la pantalla de nivel (10) reparte con el guion de _hasta_libre hasta cerrarla."""
    escenario_nombre_2(j, par)
    # DIAGNOSTICO (sin repartir a ciegas): una foto tras cada tecla, para ver que cierra el aviso y que mueve el cursor
    for i, k in enumerate(["Space", "Space", "Space", "Space", "Space", "D", "D", "D", "Space", "Space"]):
        j.tecla(k, 3500)
        j.captura("diag_%d_%s" % (i, k))
        if j.pantalla() != 10:
            break
    hasta.en10 = 0
    t0 = time.time()
    while j.pantalla() == 10 and time.time() - t0 < 120:
        hasta.en10 += 1
        n = hasta.en10
        j.tecla("Space", 2500)   # cierra el aviso "¡Has obtenido puntos Z!" (si no, se come las teclas de reparto; A lo vuelve a abrir)
        for _ in range(7):
            j.tecla("D", 350)
            j.tecla("S", 350)
        if n in (1, 2):
            j.captura("nivel_%d_antes_de_A" % n)
        j.tecla("Space", 2500)
        if j.pantalla() == 10 and n % 5 == 0:
            j.tecla("Backspace", 1000)
    if j.pantalla() == 10:
        raise P.Fallo("la pantalla de nivel sigue abierta tras 120 s: el guion no gasta los puntos")
    j.log(r"dbz3 heroe: progreso de la ficha", 5)


def main_nivel(a):
    P.ESCENARIOS["heroe_nivel"] = ("Puntos Z: reparto hasta cerrar", lambda p: {}, escenario_nivel)
    rc = P.correr(a.copia, "heroe_nivel", env_extra=["REX_DBZ3_LANGUAGE=%d" % (a.idioma or 5), "DBZ3_TEST_RINGOUT=1", "DBZ3_LEVELUP_DIAG=1", "DBZ3_TEST_DU_ESTADOS=1"],
                  instantanea=None if a.instantanea == "ninguna" else a.instantanea, forzar=True)
    ok("escenario sin caida (sale de la pantalla de nivel)", rc["ok"], rc.get("msg", ""))
    print("capturas:", rc["run"])
    return 0 if all(c for _, c, _ in RES) else 1


def escenario_arte(j, par):
    """Select Master y el creador (gancho DBZ3_TEST_HEROE_ABRIR, solo por pruebas.py)."""
    time.sleep(2.5)
    for k, t in NAV_HEROE:
        j.tecla(k, t)
    j.log(r"Select Master abierto|Create abierto", 30)
    time.sleep(3)
    j.captura("arte_" + par.get("que", "maestros"))


def main_arte(a):
    for que, v in (("maestros", "1"), ("creador", "2")):
        P.ESCENARIOS["heroe_arte_" + que] = ("Arte del Heroe: " + que, lambda p: {}, lambda j, par, q=que: escenario_arte(j, {"que": q}))
        rc = P.correr(a.copia, "heroe_arte_" + que, env_extra=["REX_DBZ3_LANGUAGE=%d" % (a.idioma or 5), "DBZ3_TEST_HEROE_ABRIR=" + v],
                      instantanea=None if a.instantanea == "ninguna" else a.instantanea, forzar=True)
        ok("escenario %s sin caida" % que, rc["ok"], rc.get("msg", ""))
        print("capturas:", rc["run"])
    return 0 if all(c for _, c, _ in RES) else 1


def main_aviso(a):
    P.ESCENARIOS["heroe_aviso"] = ("Aviso de montaje fallido", lambda p: {}, escenario_aviso)
    rc = P.correr(a.copia, "heroe_aviso", env_extra=["DBZ3_TEST_SIN_PYTHON=1", "REX_DBZ3_LANGUAGE=%d" % (a.idioma or 5)],
                  instantanea=None if a.instantanea == "ninguna" else a.instantanea, forzar=True)
    log = open(os.path.join(rc["run"], "dbz3.log"), encoding="utf-8", errors="replace").read()
    print("capturas:", rc["run"])
    ok("escenario sin caida", rc["ok"], rc.get("msg", ""))
    ok("el montaje fallido queda en el registro", "el montaje del heroe fallo" in log or "no se pudo generar" in log)
    return 0 if all(c for _, c, _ in RES) else 1


def main_nombre_real(a):
    import json   # noqa: PLC0415
    P.ESCENARIOS["heroe_nombre_1"] = ("Nombre: elegirlo como jugador", lambda p: {}, escenario_nombre_1)
    P.ESCENARIOS["heroe_nombre_2"] = ("Nombre: verlo tras reiniciar", lambda p: {}, escenario_nombre_2)
    idioma = ["REX_DBZ3_LANGUAGE=%d" % a.idioma] if a.idioma else []
    if a.instantanea != "ninguna":   # --instantanea ninguna: el nombre ya elegido, solo el paso 2
        for n in ("historia_heroe_35", "historia_heroe_36", "historia_heroe_37", "heroe_nombres"):   # que lo monte el juego, no lo que hubiera
            d = os.path.join(str(P.BASE), a.copia, "mods", n)
            if os.path.isdir(d):
                P.mover(P.Path(d), P.RESPALDO / ("%s_%s_%s" % (P.ts(), a.copia, n)))
        rc = P.correr(a.copia, "heroe_nombre_1", env_extra=idioma, instantanea=a.instantanea, forzar=True)
        ok("paso 1 sin caida", rc["ok"], rc.get("msg", ""))
    base = os.path.join(str(P.BASE), a.copia)
    cac = open(os.path.join(base, "user_data", "dbz3", "cac.toml"), encoding="utf8").read()
    ok("cac.toml guarda el nombre", 'nombre = "BC\u00d1"' in cac)
    t0 = time.time()
    arch = os.path.join(base, "mods", "heroe_nombres", "nombres.json")
    while time.time() - t0 < 600:   # el montaje que lanza el juego al confirmar sigue con el juego cerrado
        if os.path.isfile(arch) and "BC\u00d1" in json.load(open(arch, encoding="utf8")).values():
            break
        time.sleep(5)
    ok("el rotulo del nombre se monta solo", os.path.isfile(arch) and "BC\u00d1" in json.load(open(arch, encoding="utf8")).values())
    import heroe_e2e  # noqa: PLC0415
    heroe_e2e.esperar_montaje(a.copia)   # tras el rotulo sigue la historia de las casillas, con el juego ya cerrado
    ok("el juego deja montadas las 3 historias", all(os.path.isfile(os.path.join(base, "mods", "historia_heroe_%d" % c, "campana.toml")) for c in (35, 36, 37)),
       open(os.path.join(base, "ingame_ui", "heroe_vista", "montaje.log"), encoding="utf8", errors="replace").read()[-300:])
    ruta = os.path.join(base, "mod center hd", "campanas", "historia_heroe_35.toml")   # el montaje lo deja junto al heroe_sagas_a_du.py que corre
    toml = open(ruta if os.path.isfile(ruta) else os.path.join(RAIZ, "mod center hd", "campanas", "historia_heroe_35.toml"), encoding="utf8").read()
    ok("los dialogos llevan el nombre", "BC\u00d1" in toml)
    rc = P.correr(a.copia, "heroe_nombre_2", env_extra=["DBZ3_TEST_RINGOUT=1", "DBZ3_LEVELUP_DIAG=1"] + idioma, instantanea=None, forzar=True)
    log = open(os.path.join(rc["run"], "dbz3.log"), encoding="utf-8", errors="replace").read()
    print("capturas:", rc["run"])
    ok("paso 2 sin caida", rc["ok"], rc.get("msg", ""))
    ok("rotulo propio: banco de 68 texturas, casilla 35", re.search(r"id 62 \(banco n 68\) .*casilla 35", log))
    fallos = [n for n, c, _ in RES if not c]
    print("heroe_du_e2e --nombre-real: %d/%d PASS" % (len(RES) - len(fallos), len(RES)))
    return 1 if fallos else 0


def main_shenron(a):
    # la ficha activa es la 1: casilla 35
    P.ESCENARIOS["heroe_shenron"] = ("Heroe: 7 Esferas -> Shenron nativo y deseo", lambda p: {}, escenario_shenron)
    env = ["DBZ3_TEST_DU_HEROE=35", "DBZ3_TEST_HEROE_FINAL=1", "DBZ3_TEST_HEROE_ESFERAS=127", "DBZ3_TEST_DU_ESTADOS=1"]
    rc = P.correr(a.copia, "heroe_shenron", env_extra=env, instantanea=None if a.instantanea == "ninguna" else a.instantanea)
    log = open(os.path.join(rc["run"], "dbz3.log"), encoding="utf-8", errors="replace").read()
    print("capturas:", rc["run"])
    ok("escenario terminado sin caida", rc["ok"], rc.get("msg", ""))
    ok("la VM de guiones (sub_8209F568) llama a la comprobacion con la mascara del heroe",
       re.search(r"sub_8215C1B0 .*casilla 35 .*mascara antes 7F .*desde 8209F668", log))
    ok("aparece Shenron y se gastan las Esferas", "aparece Shenron" in log)
    if FILA_RAZA:
        ok("fila de raza: deseo concedido", "Shenron concede el deseo de raza (deseos 1)" in log and re.search(r"deseo elegido: fila 3 de 3", log))
    else:
        ok("otra fila: su capsula y sin deseo de raza", "deseo elegido: fila 1" in log and "deseo de raza" not in log)
    ok("pantalla 11 (Shenron nativo)", "du-estados: pantalla 11" in log)
    ok("tras el deseo, pantalla 12 (creditos nativos)", "du-estados: pantalla 12" in log)
    if SALIDA:
        cac = open(os.path.join(P.BASE if hasattr(P, "BASE") else r"D:\DBZ3HD", a.copia, "user_data", "dbz3", "cac.toml"), encoding="utf8").read()
        ok("la ficha conserva el deseo tras salir", re.search(r"^deseos = %d$" % (1 if FILA_RAZA else 0), cac, re.M) and re.search(r"^esferas = 0$", cac, re.M))
        ok("tras los creditos, titulo de DU (pantalla 6)", re.search(r"pantalla 12 .*[\s\S]*du-estados: pantalla 6 ", log))
        ok("y de ahi al menu principal (pantalla 5)", re.search(r"pantalla 12 .*[\s\S]*du-estados: pantalla 5 ", log))
    fallos = [n for n, c, _ in RES if not c]
    print("heroe_du_e2e --shenron: %d/%d PASS" % (len(RES) - len(fallos), len(RES)))
    return 1 if fallos else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--copia", default="s5_c4")
    ap.add_argument("--instantanea", default="freezer_cap2")
    ap.add_argument("--casilla", type=int, default=36)
    ap.add_argument("--largo", action="store_true", help="cruza de la saga 3 a la 4 (sagas virtuales)")
    ap.add_argument("--shenron", action="store_true", help="solo el final con las 7 Esferas: Shenron nativo y deseo")
    ap.add_argument("--puntos", action="store_true", help="los puntos del mapa de la saga 1 por tipo, con --casilla de la ficha activa")
    ap.add_argument("--nombre-real", action="store_true", help="elige el nombre como jugador, reinicia y lo busca en rotulo y dialogos")
    ap.add_argument("--fila-raza", action="store_true", help="con --shenron: elige la fila de cambiar raza")
    ap.add_argument("--completa", action="store_true", help="partida entera con pasos de jugador: sagas 1-8, todos los puntos, hasta Shenron (con --casilla de la ficha activa)")
    ap.add_argument("--idioma", type=int, default=0, help="REX_DBZ3_LANGUAGE (5 = espanol)")
    ap.add_argument("--turbo", type=int, default=1)
    ap.add_argument("--capsula", action="store_true", help="Freezer: capsula real y New! solo si no la tenias")
    ap.add_argument("--nivel", action="store_true", help="la pantalla de nivel del primer combate: reparte los puntos Z con el guion y la cierra")
    ap.add_argument("--arte", action="store_true", help="fotos del creador y de Select Master (con o sin el pack de arte)")
    ap.add_argument("--aviso", action="store_true", help="sin Python: el aviso en pantalla al pulsar Empezar con el montaje fallido")
    ap.add_argument("--real", action="store_true", help="con --completa: ficha creada por Crear > Confirmar, sin FORZAR, con creditos y guardado")
    ap.add_argument("--colores",action="store_true", help="con --completa: solo capturas de las marcas de la saga 1")
    ap.add_argument("--salida", action="store_true", help="con --shenron: tambien creditos, dialogo de guardar y vuelta al menu")
    a = ap.parse_args()
    global SALIDA, CAS, FILA_RAZA
    SALIDA = a.salida
    FILA_RAZA = a.fila_raza
    if a.shenron:
        return main_shenron(a)
    if a.arte:
        return main_arte(a)
    if a.nivel:
        return main_nivel(a)
    if a.aviso:
        return main_aviso(a)
    if a.nombre_real:
        return main_nombre_real(a)
    if a.capsula:
        return main_capsula(a)
    if a.completa:
        CAS = a.casilla
        return main_completa(a)
    if a.puntos:
        CAS = a.casilla
        return main_puntos(a)
    global LARGO
    CAS = a.casilla
    LARGO = a.largo
    P.ESCENARIOS["heroe_du"] = ("C3: Heroe en DU de extremo a extremo", lambda p: {}, escenario)
    env = ["DBZ3_LEVELUP_DIAG=1", "DBZ3_TEST_DU_HEROE=%d" % CAS, "DBZ3_AUTO_TOUCH=%s" % ("0" if LARGO else "2"), "DBZ3_TEST_RINGOUT=1"] + (["REX_DBZ3_LANGUAGE=%d" % a.idioma] if a.idioma else [])   # compfin: --idioma tambien en la ruta corta
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
        for i in range(3):   # una Esfera por saga completada, de verdad (sin ganchos de progreso)
            ok("Esfera de la saga %d al pasar a la siguiente" % (i + 1), "esfera %d recogida (%d/7)" % (i + 1, i + 1) in log)
        ok("saga 4 (virtual): combate 0 ganado con el heroe", re.search(r"dbz3 heroe: DU saga 3 combate 0 ganado", log))
        ok("saga 4: el mapa usa el bloque 0 (5000+casilla, DS20)", re.search(r"escena 50%d entrada" % CAS, log))
    ok("marca 2 en el mapa tras la victoria", "se toca la marca de combate %d entrada 2" % (5500 + CAS) in log)
    fallos = [n for n, c, _ in RES if not c]
    print("heroe_du_e2e: %d/%d PASS" % (len(RES) - len(fallos), len(RES)))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
