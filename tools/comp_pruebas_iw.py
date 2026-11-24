"""Validación independiente COMP contra IW PAL. No importa comp_pruebas.py.

  python tools/comp_pruebas_iw.py --oraculo
  python tools/comp_pruebas_iw.py --suite sol
  python tools/comp_pruebas_iw.py correr sol tools/comp_iw/burst.toml
  python tools/comp_pruebas_iw.py --visual <suite.json> <directorio-nuevo>

Los TOML también sirven como humo con tools/pruebas.py; este adaptador añade
las comprobaciones numéricas. Un dato no observado NO se convierte en PASS.
IW: 22AAE0 entrada, 22BCD0 drenaje/final, 235180 fatiga, 220EC0 L1,
240220/2400E0 ventanas. Referencia de lectura: MODO_COMPETITIVO_ESTUDIO.md §b.
"""
import argparse
import csv
import ctypes
import hashlib
import io
import json
import math
import os
import re
import shutil
import struct
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GUIONES = Path(__file__).with_name("comp_iw")
ELF = Path(r"E:\DBZ3HD_trabajo\_bancos_archivados\ps2_eu_iw_20261021\Infinite World (Europe)\SLES_553.47")
BURST = 0x434D5001


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for b in iter(lambda: f.read(1024 * 1024), b""):
            h.update(b)
    return h.hexdigest()


def guardar(path, data):
    # Evidencia inmutable: no pisar una ejecución anterior.
    with Path(path).open("x", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def oraculo():
    """EE original, memoria sintética; puentes audiovisuales explícitos.

    L1 se prueba hasta el despacho de la acción, no se simula su animación.
    La trayectoria/altura son entradas controladas, no geometría del juego.
    """
    from iw_mips import ee
    d = ELF.read_bytes()
    if d[:4] != b"\x7fELF":
        raise ValueError("IW original no es ELF")
    mem = bytearray(ee.TOTAL)
    ph, n = struct.unpack_from("<I", d, 28)[0], struct.unpack_from("<H", d, 44)[0]
    for i in range(n):
        kind, off, va, _, size, total, _, _ = struct.unpack_from("<8I", d, ph + 32 * i)
        if kind == 1:
            if off + size > len(d) or va + total > ee.RAM:
                raise ValueError("segmento IW fuera de límites")
            mem[va:va + size] = d[off:off + size]
    c = ee.Ee(mem, 0x4A3DF0)
    c.whitelist = {0x22AAE0, 0x22BCD0, 0x235320, 0x235180, 0x220EC0,
                   0x240220, 0x2400E0}
    c.estricto = True
    x, rival, mgr = 0x1000000, 0x1005000, 0x1100000
    efectos, agotamientos, lanzamientos, casos = [], [], [], []

    def w(a, v): struct.pack_into("<I", mem, a, v)
    def f(a, v): struct.pack_into("<f", mem, a, v)
    def rf(a): return struct.unpack_from("<f", mem, a)[0]
    def r16(a): return struct.unpack_from("<H", mem, a)[0]

    def efecto(cpu):
        efectos.append(cpu.r[4])
        cpu.r[2] = 0

    def agotar(cpu):
        agotamientos.append(list(cpu.r[4:7]))
        cpu.r[2] = 0

    def lanzar(cpu):
        lanzamientos.append(cpu.r[4])
        cpu.r[2] = 0

    def trayectoria(cpu):
        # El primer argumento es float (f12); salida=a0, dirección=a1.
        struct.pack_into("<4f", mem, cpu.r[4] & ee.M32, 0, 30, 100, 1)

    def suelo(cpu): cpu.f[0] = 0

    # No se puentean los cálculos de ki, fatiga, entrada ni filtros de L1.
    c.hooks.update({0x1D55A0: efecto, 0x1D1E50: efecto, 0x1D15D0: efecto,
                    0x240740: efecto, 0x25F030: efecto, 0x22DFD0: agotar,
                    0x2157B0: lanzar, 0x1424E0: trayectoria, 0x1E7070: suelo})

    def inicio(ki=3000, activo=1):
        mem[x:x + 0x3200] = bytes(0x3200)
        mem[rival:rival + 0x2200] = bytes(0x2200)
        mem[mgr:mgr + 0x9000] = bytes(0x9000)
        w(0x49CA70, mgr)
        mem[x + 0x2091], mem[x + 0x2060] = activo, 2
        f(x + 0x185C, ki); f(x + 0x1860, 7000)
        f(x + 0x1AA8, 1); f(x + 0x20A8, 1); f(x + 0x1868, 500)
        f(mgr + 0x8B80, 1)
        efectos.clear(); agotamientos.clear(); lanzamientos.clear()

    def comprobar(nombre, real, esperado, **datos):
        if real != esperado:
            raise AssertionError((nombre, datos, real, esperado))
        casos.append(dict(caso=nombre, observado=real, esperado=esperado, **datos))

    for activo in (0, 1):
        for ki in (0, 999, 999.99, 1000, 1000.25, 5000):
            for reloj in (0, 1):
                inicio(ki, activo); f(x + 0x1AB8, reloj)
                real = c.call(0x22AAE0, x, limite=10000) & ee.M32
                comprobar("entrada 22AAE0", real, int(not activo and (ki >= 1000 or reloj > 0)),
                          activo=activo, ki=ki, reloj=reloj)
    for ki in (10, 20, 29, 30, 1000, 3000):
        for coste in (0.5, 1, 2):
            for accion in (0, 0x1E, 0x32, 0x35):
                inicio(ki); f(x + 0x1AA8, coste)
                struct.pack_into("<h", mem, x + 0xCBC, accion)
                c.call(0x22BCD0, x, limite=15000)
                cobra = accion == 0
                esperado = max(0, ki - 10) if cobra else ki
                comprobar("drenaje 22BCD0", rf(x + 0x185C), esperado, ki=ki, accion=accion,
                          multiplicador_coste=coste)
                comprobar("final 22BCD0", mem[x + 0x2091], int(not cobra or esperado >= 20), ki=ki, accion=accion)
                comprobar("no agota al acabar", bool(agotamientos), False, ki=ki)
                if cobra and abs(rf(x + 0x1868) - 499.98) > 0.0001:
                    raise AssertionError(("recuperación Burst", rf(x + 0x1868)))
    for activo in (0, 1):
        for fatiga in (0, 299, 300, 599, 600, 995, 1000):
            for delta in (-5, 0, 5, 1000):
                for flags in (0, 1):
                    inicio(activo=activo); f(x + 0x1868, fatiga)
                    c.call(0x235180, x, flags, fargs=(delta,), limite=10000)
                    final = min(1000, max(0, fatiga + delta))
                    comprobar("fatiga 235180", rf(x + 0x1868), final, activo=activo, fatiga=fatiga, delta=delta, flags=flags)
                    comprobar("aturdimiento 235180", bool(agotamientos), bool(delta >= 0 and flags & 1 and final >= 1000))
    # Filtro de alcance temporal ORIGINAL: progreso de vuelo > umbral+8 (no son metros).
    for activo in (0, 1):
        for ki in (999, 1000, 4000):
            for seguidas in (0, 2, 3):
                for vuelo in (8, 8.01, 30):
                    inicio(ki, activo)
                    w(mgr + 0x8B8C, 3)
                    w(x + 0x1384, rival)
                    mem[x + 0x207C], mem[x + 0x207D], mem[x + 0x207F] = 1, 1, seguidas
                    struct.pack_into("<h", mem, rival + 0xCBC, 0x18)
                    f(rival + 0x18B8, vuelo); f(rival + 0x18BC, 0)
                    c.call(0x220EC0, x, limite=20000)
                    sale = seguidas < 3 and (activo or ki >= 1000) and vuelo > 8
                    comprobar("L1 despacho 220EC0", bool(lanzamientos), bool(sale), activo=activo, ki=ki,
                              seguidas=seguidas, progreso_vuelo=vuelo)
                    comprobar("L1 coste 220EC0", rf(x + 0x185C), ki - (1000 if sale and not activo else 0))
    for activo in (0, 1):
        for flags in (0, 4, 0x104, 0x204, 0x404):
            inicio(activo=activo)
            anim, nodo, pista = x + 0x910, x + 0x3000, x + 0x3100
            w(anim + 0x180, nodo); mem[pista + 2] = 7
            struct.pack_into("<H", mem, pista + 8, flags)
            c.call(0x240220, anim, pista, limite=10000)
            comprobar("armadura ventana", mem[x + 0xABA], int((r16(pista + 8) & 0x70F) == 4), activo=activo, flags=flags)
            c.call(0x2400E0, anim, 7, limite=10000)
            comprobar("armadura fin ventana", mem[x + 0xABA], 0)
    if c.sin_puente or c.desconocidas or c.io:
        raise AssertionError((c.sin_puente, c.desconocidas, c.io))
    return dict(elf=str(ELF), elf_sha256=hashlib.sha256(d).hexdigest(), casos=casos,
                diferencias=0, instrucciones=c.n, puentes=[hex(a) for a in c.hooks],
                limites=["L1: trayectoria/altura sintéticas y acción interceptada; no mide movimiento completo",
                         "Armadura: alta/baja de ventana original, no colisión completa",
                         "Sin render IW, no constituye referencia visual"])


def muestra(P, j, lado=0):
    b = P.leer(j.h, P.GBASE + 0x823D6CF4, 4)
    mgr = int.from_bytes(b, "big") if b and len(b) == 4 else 0
    raw = P.leer(j.h, P.GBASE + mgr + 2096 + 8240 * lado, 8240) if mgr else None
    if not raw or len(raw) != 8240:
        raise P.Fallo("IW REF: no pude leer el luchador")
    def f(off): return struct.unpack_from(">f", raw, off)[0]
    def n(off): return struct.unpack_from(">I", raw, off)[0]
    return dict(ki=f(6100), vida=f(6080), fatiga=f(6120), hiper=raw[8208], agotado=raw[8211],
                burst=(n(6116) & ~2) == BURST, accion=struct.unpack_from(">H", raw, 3244)[0],
                codigo=n(2928), ataque=struct.unpack_from(">i", raw, 6668)[0],
                defensa=struct.unpack_from(">i", raw, 6672)[0], factor_ata=f(6680), factor_def=f(6684),
                armadura=raw[2762], seguidas=raw[8218], pos=[f(64), f(68), f(72)], mgr=mgr)


def esperar(P, j, predicado, seg=10):
    fin = time.monotonic() + seg
    while time.monotonic() < fin:
        if not j.vivo():
            raise P.Fallo("IW REF: el juego murió")
        x = muestra(P, j)
        if predicado(x):
            return x
        time.sleep(0.015)
    raise P.Fallo("IW REF: condición de memoria no observada; última muestra " + json.dumps(x))


def escribir_float(P, j, offset, valor):
    x = muestra(P, j)
    addr = P.GBASE + P.DESPL + x["mgr"] + 2096 + offset
    h = P.K32.OpenProcess(0x0438, False, j.proc.pid)
    try:
        n, dato = ctypes.c_size_t(), struct.pack(">f", valor)
        if not h or not P.K32.WriteProcessMemory(ctypes.c_void_p(h), ctypes.c_void_p(addr), dato, 4, ctypes.byref(n)) or n.value != 4:
            raise P.Fallo("IW REF: no pude preparar el campo de prueba")
    finally:
        if h: P.K32.CloseHandle(ctypes.c_void_p(h))


def csv_actual(j, nombre):
    p = j.dest / "logs" / nombre
    return list(csv.DictReader(io.StringIO(p.read_text(encoding="utf-8", errors="replace")))) if p.is_file() else []


def ejecutar(P, j, caso):
    r = dict(caso=caso, fecha=datetime.now().isoformat(), criterios={}, mediciones={}, limites=[],
             binarios={n: sha(j.dest / n) for n in ("dbz3.exe", "rexruntime.dll", "rexgpu-xenos.dll")})
    fotos = []

    def check(nombre, ok, dato=None):
        r["criterios"][nombre] = None if ok is None else bool(ok)
        if dato is not None: r["mediciones"][nombre] = dato

    def foto(fase):
        antes = muestra(P, j)
        p = j.captura("iw_" + fase)
        despues = muestra(P, j)
        fotos.append(dict(fase=fase, port=str(p), iw=None, antes=antes, despues=despues,
                          alineacion="sin referencia visual IW homologada"))

    def reposo():
        return esperar(P, j, lambda x: x["accion"] <= 2 and not x["burst"] and not x["hiper"], 20)

    def ki(valor):
        reposo(); j.tecla(f"set dbz3_combate_prueba_ki={valor}", 300)
        return esperar(P, j, lambda x: abs(x["ki"] - valor) < 30, 8)

    try:
        j.log(r"tag: empieza un combate", 90)
        P._esperar_pelea(j, 90); P.dormir(1.5)
        if caso == "burst":
            ki(500); j.tecla("Q", 800)
            x = muestra(P, j)
            check("L2 sin barra", not x["burst"] and not x["hiper"] and
                  bool(re.search(r"COMP L2 del lado 1 con ki \d+: sin una barra", j.texto_log())))
            ki(3000); j.tecla("L+P+Space+Quote", 800)
            x = muestra(P, j)
            check("combo Hyper bloqueado", not x["burst"] and not x["hiper"])
            reposo(); ki(3000)
            j.tecla("Q", 0)
            x = esperar(P, j, lambda x: x["burst"], 6)
            check("entrada propia", x["burst"] and not x["hiper"])
            foto("inicio_burst")
            b = esperar(P, j, lambda x: x["burst"] and x["accion"] <= 2, 6)
            foto("bucle_burst")
            fin = esperar(P, j, lambda x: not x["burst"], 18)
            check("fin sin aturdimiento", not fin["agotado"])
            apagados = re.findall(r"COMP Aura Burst del lado 1 apagado \(ki (\d+), fatiga ([\d.]+), agotado (\d)", j.texto_log())
            check("fin bajo 20", bool(apagados and int(apagados[-1][0]) < 20), apagados)
            fuera = ki(round(b["ki"]))
            check("ataque y defensa +5 a igual ki", b["ataque"] == fuera["ataque"] + 5 and
                  b["defensa"] == fuera["defensa"] + 5, dict(dentro=b, fuera=fuera))
            check("factores coherentes", abs(b["factor_ata"] - b["ataque"] * .01) < 1e-5 and
                  abs(b["factor_def"] - b["defensa"] * .01) < 1e-5)
            P.dormir(1.1)
            filas = csv_actual(j, "combate_sonda.csv")
            pares = [(a, b_) for a, b_ in zip(filas, filas[1:]) if a.get("combate") == b_.get("combate") == "4"
                     and int(b_["cuadro"]) == int(a["cuadro"]) + 1 and a.get("1_hiper") == b_.get("1_hiper") == "0"
                     and a.get("1_accion") in ("0", "1", "2") and b_.get("1_accion") in ("0", "1", "2")
                     and 100 < float(b_["1_ki"]) < 2400 and 0 < float(a["1_ki"]) - float(b_["1_ki"]) < 30]
            deltas = [float(a["1_ki"]) - float(b_["1_ki"]) for a, b_ in pares]
            check("10 ki por cuadro", len(deltas) >= 30 and all(d == 10 for d in deltas),
                  dict(pares=len(deltas), deltas=sorted(set(deltas))))
            r["limites"].append("Umbral 999/1000: comprobado en EE; en juego se prepara 500/3000 para evitar regeneración durante la tecla")
        elif caso in ("l1_burst", "l1_fuera", "control_off"):
            cadena, entrada, fin = [], None, time.monotonic() + 40
            # La captura puede esperar 30 cuadros: muestrear en paralelo para
            # no perder todo el desplazamiento mientras se guarda el PNG.
            with ThreadPoolExecutor(max_workers=1) as captura:
                foto_pendiente = None
                while time.monotonic() < fin:
                    x, y = muestra(P, j), muestra(P, j, 1)
                    d = math.dist(x["pos"], y["pos"])
                    dato = dict(accion=x["accion"], codigo=x["codigo"], ki=x["ki"], hiper=x["hiper"],
                                burst=x["burst"], distancia=d, seguidas=x["seguidas"], vida_rival=y["vida"])
                    cadena.append(dato)
                    if caso == "control_off":
                        if 0x400 <= x["codigo"] < 0x480:
                            entrada = dato
                            foto_pendiente = captura.submit(foto, "control_rush")
                            break
                    elif x["accion"] == 40 and not entrada:
                        entrada = dato
                        foto_pendiente = captura.submit(foto, "l1")
                    elif entrada and x["accion"] != 40:
                        break
                    time.sleep(.015)
                if foto_pendiente:
                    foto_pendiente.result()
            if caso == "control_off":
                check("Versus conserva Dragon Rush", bool(entrada), entrada)
                check("Versus conserva Hyper", any(a["hiper"] for a in cadena))
                check("sin estado propio COMP", not any(a["burst"] for a in cadena))
                check("sin activaciones COMP", not re.search(r"COMP estado propio.*encendido", j.texto_log()))
                r["limites"].append("Control funcional de Hyper/Rush; no prueba igualdad byte a byte de todo Versus")
            else:
                check("L1 entra acción 40", bool(entrada), entrada)
                check("sin Hyper ni Dragon Rush", bool(cadena) and not any(a["hiper"] or 0x400 <= a["codigo"] < 0x480 for a in cadena))
                cobros = [dict(burst=b == "true", flags=int(f), coste=int(c), antes=int(a), despues=int(d))
                          for b, f, c, a, d in re.findall(r"COMP prueba L1: lado 1, Burst (true|false), banderas (\d+), coste (\d+), ki (\d+) -> (\d+)", j.texto_log())]
                pagos = [c for c in cobros if c["flags"] == 0]
                burst = caso == "l1_burst"
                check("L1 coste IW", bool(pagos) and all(c["burst"] == burst and c["coste"] == 1000 and
                      c["antes"] - c["despues"] == (0 if burst else 1000) for c in pagos), cobros)
                perseguidas = [c for c in cadena if c["accion"] == 40]
                trayecto = perseguidas + ([cadena[-1]] if entrada else [])
                minima = min((c["distancia"] for c in trayecto), default=None)
                check("L1 se aproxima al rival", bool(entrada and minima < entrada["distancia"] - .5),
                      dict(inicial=entrada["distancia"] if entrada else None, minima=minima))
                check("L1 seguimiento natural", bool(entrada and cadena[-1]["accion"] != 40), cadena[-1] if cadena else None)
                check("contador en rango IW", bool(perseguidas) and all(c["seguidas"] <= 3 for c in perseguidas))
                r["limites"].append("Aproximación y salida de acción medidas; igualdad de trayectoria completa con IW pendiente")
            r["mediciones"]["cadena"] = cadena
        elif caso == "fatiga":
            inicio = muestra(P, j)
            datos, fin = [], time.monotonic() + 35
            while time.monotonic() < fin:
                x = muestra(P, j); datos.append(x)
                if x["agotado"]: break
                if x["fatiga"] < 990: escribir_float(P, j, 6120, 995)
                time.sleep(.02)
            check("fatiga llena aturde con ki", bool(datos and datos[-1]["agotado"] and datos[-1]["ki"] > 1000), datos[-1] if datos else None)
            check("golpe real recibido", bool(datos and datos[-1]["vida"] < inicio["vida"]))
            check("umbral confirmado en log", bool(re.search(r"COMP fatiga llena del lado 1: aturdido", j.texto_log())))
            foto("fatiga")
        elif caso == "armadura":
            ki(7000); j.tecla("Q", 0)
            esperar(P, j, lambda x: x["burst"] and x["accion"] <= 2, 6)
            P._colocar(j, ["-4,0,0", "4,0,0"])
            j.tecla('set dbz3_combate_prueba_muneco="ataca"', 300)
            muestras, fin, siguiente = [], time.monotonic() + 9, 0
            while time.monotonic() < fin:
                x = muestra(P, j); muestras.append(x)
                if not x["burst"]: break
                if time.monotonic() >= siguiente:
                    # Provocar animaciones reales, sin alterar sus flags ni el
                    # contador. P/K del teclado = L/P en esta configuración.
                    j.tecla("L 100" if len(muestras) % 2 else "P 100", 0)
                    siguiente = time.monotonic() + .45
                time.sleep(.01)
            P.dormir(1.1)   # vaciado periódico del CSV de impactos
            golpes = [g for g in csv_actual(j, "combate_golpes.csv") if g.get("tipo") == "golpe" and g.get("victima") == "1"]
            r["mediciones"].update(muestras=muestras, golpes=golpes)
            # No forzar el contador de armadura: eso probaría el montaje y no los datos naturales.
            ventanas = [x for x in muestras if x["armadura"] > 0]
            check("ventana natural de armadura observada", True if ventanas else None, len(ventanas))
            r["limites"].append("Falta colisión sincronizada con ventana natural; este caso NO certifica que un golpe ligero se absorba")
            check("golpe ligero absorbido acreditado", None)
            foto("armadura")
        else:
            raise ValueError("caso IW desconocido: " + caso)
    finally:
        for nombre in ("combate_sonda.csv", "combate_golpes.csv"):
            src = j.dest / "logs" / nombre
            if src.is_file(): shutil.copy2(src, j.run / nombre)
        r["fotos"] = fotos
        r["ok"] = bool(r["criterios"]) and all(v is True for v in r["criterios"].values())
        r["estado"] = "PASS" if r["ok"] else "INCOMPLETO" if any(v is None for v in r["criterios"].values()) else "FAIL"
        guardar(j.run / "iw_ref.json", r)
    malos = [k for k, v in r["criterios"].items() if not v]
    j.info("IW REF: " + json.dumps(r["criterios"], ensure_ascii=False))
    if malos: raise P.Fallo("IW REF: " + ("pendiente de medida: " if r["estado"] == "INCOMPLETO" else "") + "; ".join(malos))


def instalar(P):
    original = P.cargar_escenario

    def cargar(esc, par):
        desc, fn, env, prep = original(esc, par)
        if str(esc).lower().endswith(".toml"):
            d = P._leer_guion(esc)
            caso = d.get("iw", {}).get("caso")
            if caso:
                env["DBZ3_TEST_COMP_REF_SHA"] = sha(__file__)
                fn = lambda j, _p: ejecutar(P, j, caso)
        return desc, fn, env, prep
    P.cargar_escenario = cargar


def visual(suite, destino):
    """Hoja a escala 0,5; un hueco IW sigue visible como pendiente.

    El manifiesto permite aportar 'iw' por foto. Nunca usa una captura del port
    como supuesto original ni convierte una hoja incompleta en equivalencia.
    """
    from PIL import Image, ImageDraw
    meta = json.loads(Path(suite).read_text(encoding="utf-8"))
    destino = Path(destino)
    if destino.exists() or not destino.parent.is_dir():
        raise ValueError("el destino visual debe ser nuevo y su padre existir")
    destino.mkdir()
    fotos = {}
    for prueba in meta.get("pruebas", []):
        origen = Path(prueba.get("evidencia", prueba["run"])) / "iw_ref.json"
        if origen.is_file():
            for foto in json.loads(origen.read_text(encoding="utf-8")).get("fotos", []):
                if foto["fase"] in ("inicio_burst", "bucle_burst", "l1"):
                    fotos[foto["fase"]] = foto
    # Una captura del IW original se aporta con coordenada de escena/cuadro en
    # el manifiesto. No se busca una imagen parecida ni se rellena por inferencia.
    for fase, referencia in meta.get("referencias_iw", {}).items():
        if fase in fotos:
            fotos[fase]["iw"] = referencia["ruta"]
            fotos[fase]["referencia_iw"] = referencia
    hoja = Image.new("RGB", (1280, 3 * 410), "#161d29")
    draw, manifiesto = ImageDraw.Draw(hoja), []
    for fila, fase in enumerate(("inicio_burst", "bucle_burst", "l1")):
        item = fotos.get(fase, dict(fase=fase, iw=None, port=None))
        for col, tipo in enumerate(("iw", "port")):
            xx, yy = col * 640, fila * 410
            draw.text((xx + 10, yy + 8), f"{fase} / {tipo.upper()}", fill="white")
            p = Path(item[tipo]) if item.get(tipo) else None
            if p and p.is_file():
                with Image.open(p) as im:
                    reducida = im.convert("RGB").resize((round(im.width * .5), round(im.height * .5)))
                    reducida.thumbnail((640, 360))
                    hoja.paste(reducida, (xx, yy + 35))
                item[tipo + "_sha256"] = sha(p)
            else:
                draw.text((xx + 20, yy + 130), "PENDIENTE: sin captura homologada", fill="#ffbd62")
        manifiesto.append(item)
    hoja.save(destino / "iw_vs_port.png")
    guardar(destino / "manifest.json", dict(estado="pendiente de homologación visual", escala_maxima=.5, fotos=manifiesto))
    print("VISUAL preparada (sin declarar equivalencia):", destino)


def main():
    args = sys.argv[1:]
    if args[:1] == ["--oraculo"]:
        r = oraculo()
        if len(args) > 1: guardar(args[1], r)
        print(f"PASS IW original: {len(r['casos'])} comprobaciones, 0 diferencias ({r['instrucciones']} instrucciones)")
        print("Límites:", "; ".join(r["limites"]))
        return 0
    if args[:1] == ["--visual"]:
        visual(*args[1:]); return 0
    import pruebas as P
    instalar(P)
    os.environ.setdefault("DBZ3_AGENTE", "solCOMP_REF")
    os.environ.setdefault("DBZ3_PRUEBAS_REINTENTO", "0")
    if args[:1] == ["--suite"]:
        p = argparse.ArgumentParser()
        p.add_argument("--suite", metavar="COPIA", required=True)
        p.add_argument("--solo", default="burst,l1_burst,l1_fuera,control_off,fatiga,armadura")
        a = p.parse_args(args)
        pruebas = []
        session = datetime.now().strftime("%Y%m%d-%H%M%S")
        for caso in a.solo.split(","):
            ruta = GUIONES / (caso + ".toml")
            r = P.correr(a.suite, str(ruta))
            evidencia = str(r["run"])
            if r.get("cache"):
                clave = re.search(r"hash=(\w+)", (Path(r["run"]) / "cache_hit.txt").read_text())[1]
                evidencia = P._leer_cache(clave)["run"]
            pruebas.append(dict(caso=caso, ok=r["ok"], run=str(r["run"]), evidencia=evidencia,
                                cache=r.get("cache"), mensaje=r["msg"]))
            guardar(P.PRUEBAS / (session + "_comp_ref_parcial_" + caso + ".json"),
                    dict(arnes_sha256=sha(__file__), pruebas=list(pruebas)))
        salida = P.PRUEBAS / (datetime.now().strftime("%Y%m%d-%H%M%S") + "_comp_ref_suite.json")
        guardar(salida, dict(fecha=datetime.now().isoformat(), arnes_sha256=sha(__file__), pruebas=pruebas))
        ok = sum(p["ok"] for p in pruebas)
        print(f"IW REF SUITE: {ok}/{len(pruebas)} PASS; {salida}")
        return 0 if ok == len(pruebas) else 1
    return P.main(args)


if __name__ == "__main__":
    sys.exit(main())
