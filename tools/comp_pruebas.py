"""COMP: Aura Burst de IW independiente del hiper de B3.

Uso (US):
  python tools/comp_pruebas.py correr sol competitivo
  python tools/comp_pruebas.py correr sol competitivo --par rush=1

Casos (Goku con su definitiva, capsula 10, contra un muneco quieto; teclas de src/launcher/settings.cpp:
L2 = Q (gatillo fisico), combo Hiper = L+P+Space+Quote, Arriba = W, E = Quote, R1 = 3):
  A  L2 con 500 de ki        -> no entra (IW pide una barra: 0x22AAE0)
  B  L2 con 3000 de ki       -> Aura Burst; gasta 10 de ki por cuadro, se apaga con ki < 20 y NO agota (IW 0x22BCD0)
  C  L2 dentro del Burst     -> no dispara la definitiva
  D  Arriba + E con 7000     -> definitiva sin hiper, cobrada una vez
  E  hiper + P+K + R1+E      -> el Dragon Rush (codigos 0x400..) no sale
Deja comp_resultado.json en la carpeta de la prueba. Los datos salen de la memoria del juego y del log.
"""
import json
import re
import struct
import sys
import time
from pathlib import Path

import pruebas as P

P.OVERRIDES.update(dbz3_combate_prueba_muneco='"quieto"', dbz3_modo_competitivo="true",
                   dbz3_combate_definitiva_directa="false")


def _x(j, k=0):
    b = P.leer(j.h, P.GBASE + 0x823D6CF4, 4)
    mgr = int.from_bytes(b, "big") if b and len(b) == 4 else 0
    x = P.leer(j.h, P.GBASE + mgr + 2096 + 8240 * k, 8240) if mgr else None
    if not x or len(x) < 8240:
        return None
    return dict(ki=struct.unpack_from(">f", x, 6100)[0], fatiga=struct.unpack_from(">f", x, 6120)[0],
                act=struct.unpack_from(">H", x, 3244)[0], cod=struct.unpack_from(">I", x, 2928)[0],
                hiper=x[8208], burst=(struct.unpack_from(">I", x, 6116)[0] & ~2) == 0x434D5001,
                agotado=x[8211], vida=struct.unpack_from(">f", x, 6080)[0],
                ata=struct.unpack_from(">i", x, 6668)[0], defe=struct.unpack_from(">i", x, 6672)[0])


def _hasta(j, cond, seg):
    fin = time.time() + seg
    while time.time() < fin:
        x = _x(j)
        if x and cond(x):
            return x
        time.sleep(0.03)
    return None


def _reposo(j, seg=20):
    return _hasta(j, lambda x: x["act"] <= 2 and not x["hiper"] and not x["burst"], seg)


def ejecutar(j, par):
    j.log(r"tag: empieza un combate", 90)
    P._esperar_pelea(j, 90)
    P.dormir(1.5)
    r = {}
    L2 = "Q"

    # A: sin una barra no entra
    j.tecla("set dbz3_combate_prueba_ki=500", 500)
    _hasta(j, lambda x: x["ki"] < 700, 6)   # R16: esperar a que el ki de prueba se aplique antes de L2
    j.tecla(L2, 1200)
    x = _x(j)
    r["A_sin_barra_no_entra"] = bool(x and not x["hiper"] and not x["burst"]
                                     and re.search(r"COMP L2 del lado 1 con ki \d+: sin una barra", j.texto_log()))

    # B: Aura Burst con 3000: gasto, apagado y sin agotamiento
    _reposo(j)
    j.tecla("set dbz3_combate_prueba_ki=3000", 500)
    _hasta(j, lambda x: x["ki"] >= 2900, 6)
    j.tecla("L+P+Space+Quote", 800)
    x = _x(j)
    r["B0_combo_hiper_bloqueado"] = bool(x and not x["hiper"] and not x["burst"])
    _reposo(j)
    x0 = _x(j)   # ataque y defensa fuera del Burst (IW 0x233CD0: +5 / +5 dentro)
    j.tecla(L2, 0)
    x = _hasta(j, lambda x: x["burst"], 4)
    r["B_entra"] = bool(x)
    r["B_estado_independiente"] = bool(x and not x["hiper"])
    if x:
        a = _hasta(j, lambda x: x["act"] <= 2, 6)        # acabada la animacion de entrada: dos muestras a 1 s
        t0 = time.time()
        P.dormir(1.0)
        b = _x(j)
        if a and b and b["burst"]:
            r["B_gasto_por_segundo"] = round((a["ki"] - b["ki"]) / (time.time() - t0), 1)   # IW: 10 x 60 = 600
            r["B6_ataque_defensa"] = dict(burst=[b["ata"], b["defe"]], antes=[x0["ata"], x0["defe"]] if x0 else None)
            r["B_gasto_correcto"] = 500 <= r["B_gasto_por_segundo"] <= 700
        # C: L2 otra vez dentro del Burst
        n_def = len(re.findall(r"15 definitiva [0-9A-F]+ del lado 1", j.texto_log()))
        j.tecla(L2, 600)
        r["C_L2_en_burst_sin_definitiva"] = len(re.findall(r"15 definitiva [0-9A-F]+ del lado 1", j.texto_log())) == n_def
        fin = _hasta(j, lambda x: not x["burst"], 15)
        P.dormir(0.5)
        m = re.findall(r"COMP Aura Burst del lado 1 apagado \(ki (\d+), fatiga ([\d.]+), agotado (\d), accion (\d+), (\d+) cuadros",
                       j.texto_log())
        r["B_apagado"] = dict(zip(("ki", "fatiga", "agotado", "accion", "cuadros"), m[-1])) if m else None
        x = _x(j)
        r["B_no_agota"] = bool(fin and m and m[-1][2] == "0" and x and not x["agotado"])
        r["B_apaga_bajo_20"] = bool(m and int(m[-1][0]) < 20)
        if b and fin:
            # B3 sub_82115A20 tambien resta defensa por barras de ki perdidas (9.cpp:9435-9501).
            # Comparar a IGUAL ki, despues del apagado: no confundir ese cambio nativo con el +5 de IW.
            ki = int(b["ki"])
            j.tecla(f"set dbz3_combate_prueba_ki={ki}", 500)
            fuera = _hasta(j, lambda x: abs(x["ki"] - ki) < 5 and x["act"] <= 2 and not x["burst"], 6)
            if fuera:
                r["B6_ataque_defensa"]["fuera_mismo_ki"] = [fuera["ata"], fuera["defe"]]
                r["B6_ataque_defensa"]["ki"] = [b["ki"], fuera["ki"]]
            r["B6_mas5"] = bool(fuera and b["ata"] == fuera["ata"] + 5 and b["defe"] == fuera["defe"] + 5)
    j.captura("b_tras_burst")

    # D: definitiva con Arriba + E, sin hiper, cobrada una vez
    _reposo(j)
    j.tecla("set dbz3_combate_prueba_ki=7000", 500)
    n_cobros = len(re.findall(r"15 cobro de", j.texto_log()))
    j.tecla("W+Quote 200", 300)
    try:
        j.log(r"COMP Arriba \+ E del lado 1: definitiva pedida", 5)
        j.log(r"15 definitiva [0-9A-F]+ del lado 1 .*modo hiper 0", 5)
        r["D_definitiva_sale"] = True
    except P.Fallo:
        r["D_definitiva_sale"] = False
    if r["D_definitiva_sale"]:
        P.dormir(2.0)
        j.captura("d_definitiva")
        try:
            j.log(r"15 fin de la definitiva del lado 1", 60)
        except P.Fallo:
            pass
        r["D_cobros"] = len(re.findall(r"15 cobro de", j.texto_log())) - n_cobros
        fin = re.findall(r"15 fin de la definitiva del lado 1: .*", j.texto_log())
        r["D_fin"] = fin[-1][:220] if fin else None

    # E: Dragon Rush (hiper + acercarse + P+K + R1+E): no debe salir
    _reposo(j, 30)
    j.tecla("set dbz3_combate_prueba_ki=7000", 500)
    j.tecla(L2, 2500)
    j.tecla("D 900", 1100)
    j.tecla("L+P", 450)
    rush = False
    for _ in range(4):
        j.tecla("3+Quote 220", 260)
        if _hasta(j, lambda x: 0x400 <= x["cod"] < 0x480, 1.2):
            rush = True
            break
    r["E_sin_rush"] = not rush
    j.captura("e_tras_rush")

    r["D_cobro_unico"] = r.get("D_cobros") == 1
    (j.run / "comp_resultado.json").write_text(json.dumps(r, indent=2, ensure_ascii=False), encoding="utf-8")
    j.info("COMP " + json.dumps(r, ensure_ascii=False))
    obligatorios = ["A_sin_barra_no_entra", "B0_combo_hiper_bloqueado", "B_entra", "B_estado_independiente",
                    "B_gasto_correcto", "B6_mas5", "B_no_agota", "B_apaga_bajo_20", "C_L2_en_burst_sin_definitiva",
                    "D_definitiva_sale", "D_cobro_unico"] + (["E_sin_rush"] if par.get("rush") == "1" else [])
    malos = [k for k in obligatorios if not r.get(k)]
    if malos:
        raise P.Fallo("COMP: fallan " + ", ".join(malos))


def entorno(p):
    return {"DBZ3_TEST_BATTLE": p.get("ids", "0,7,2"), "DBZ3_COMBATE_MUNECO": "quieto",
            "DBZ3_COMBATE_CAPS": p.get("caps", "10")}


P.ESCENARIOS["competitivo"] = ("COMP: Modo Competitivo (Aura Burst, definitiva con Arriba + E, sin Dragon Rush)",
                               entorno, ejecutar)


# Dragon Rush y persecucion. En B3 el Rush es la persecucion en hiper (sub_8210B1C0 -> evento 5). Las entradas las hace
# el mando virtual de prueba (DBZ3_TEST_COMP_RUSH en src/combate.cpp: hiper, rival a 15, P+K, a los 8 cuadros E + R1 y,
# en el modo, a los 20 L1): con teclas inyectadas no caben en la ventana. --par modo=0 es el control (el juego original
# lanza el Rush: codigo 0x400..); modo=1: no sale el Rush y L1 lanza la persecucion (accion 40) gratis en el Burst.
# --par hiper=0 (solo modo=1): sin Burst, L1 persigue y cobra 1000. Deja comp_rush.json.
def ejecutar_rush(j, par):
    j.log(r"tag: empieza un combate", 90)
    P._esperar_pelea(j, 90)
    modo, hiper = par.get("modo", "1"), par.get("hiper", "1") == "1"
    cadena, rush, chase, hiper_nativo = [], False, None, False
    fin = time.time() + float(par.get("seg", "40"))
    while time.time() < fin and not (rush or chase):
        x = _x(j)
        if x and x["hiper"]:
            hiper_nativo = True
        if x and (not cadena or cadena[-1][:3] != [hex(x["cod"]), x["act"], x["hiper"]]):
            cadena.append([hex(x["cod"]), x["act"], x["hiper"], round(x["ki"])])
        if x and 0x400 <= x["cod"] < 0x480:
            rush = True
        if x and x["act"] == 40:
            chase = dict(ki=round(x["ki"]), hiper=x["hiper"], burst=x["burst"])
        time.sleep(0.02)
    if rush or chase:
        j.captura("b_rush" if rush else "b_persecucion")
    P.dormir(2.0)
    log = j.texto_log()
    cobros = [dict(burst=b == "true", banderas=int(f), coste=int(c), antes=int(a), despues=int(d))
              for b, f, c, a, d in re.findall(
                  r"COMP prueba L1: lado 1, Burst (true|false), banderas (\d+), coste (\d+), ki (\d+) -> (\d+)", log)]
    pagos = [c for c in cobros if c["banderas"] == 0]
    cobro_correcto = bool(pagos and all(c["coste"] == 1000 and c["burst"] == hiper and
                                      c["antes"] - c["despues"] == (0 if hiper else 1000) for c in pagos))
    r = dict(modo=modo, hiper=hiper, rush=rush, persecucion=chase, cadena=cadena[:150],
             cobros=cobros, cobro_correcto=cobro_correcto,
             log=re.findall(r"(?:COMP (?:persecucion|L1 del lado)|prueba rush).*", log)[:12])
    (j.run / "comp_rush.json").write_text(json.dumps(r, indent=2, ensure_ascii=False), encoding="utf-8")
    j.info("COMP rush " + json.dumps({k: r[k] for k in ("modo", "hiper", "rush", "persecucion")}, ensure_ascii=False))
    if modo != "1":
        if not rush:
            raise P.Fallo("COMP rush: el control no lanzo el Rush")
    elif rush or not chase or hiper_nativo:
        raise P.Fallo("COMP rush: " + ("el Hiper nativo se activo" if hiper_nativo else
                                      "el Rush salio con el modo encendido" if rush else "L1 no lanzo la persecucion"))
    elif not cobro_correcto:
        raise P.Fallo("COMP L1: cobro incorrecto (gratis en Burst, 1000 fuera)")


def entorno_rush(p):
    P.OVERRIDES["dbz3_modo_competitivo"] = "true" if p.get("modo", "1") == "1" else "false"
    hiper = p.get("hiper", "1") == "1"
    return {"DBZ3_TEST_BATTLE": p.get("ids", "0,7,2"), "DBZ3_COMBATE_MUNECO": "quieto",
            "DBZ3_COMBATE_KI": p.get("ki", "4000"), "DBZ3_COMBATE_SONDA": "1",
            "DBZ3_TEST_COMP_RUSH": ("2" if p.get("modo", "1") == "1" else "1") if hiper else "4"}


# Aturdimiento por fatiga (COMP/6, IW 0x235180: banderas & 1, no agotado y fatiga >= 1000). La fatiga sube al recibir golpes
# con las marcas 0x6000 (sub_8211A5A8: dano x 0.5; la marca 0x2000 pide el aturdimiento). El muneco ataca (patadas) al
# jugador, que tiene 4000 de ki (B3 solo aturde con ki < 1000) y su fatiga se deja en 995 (escrita en la memoria del juego,
# solo la prueba) hasta que un golpe lo aturda (X+8211 = 1). --par modo=0 es el control: el original no lo aturde.
def _escribe_f(j, addr, valor):
    import ctypes
    h = P.K32.OpenProcess(0x0438, False, j.proc.pid)
    try:
        n = ctypes.c_size_t()
        d = struct.pack(">f", valor)
        return bool(P.K32.WriteProcessMemory(ctypes.c_void_p(h), ctypes.c_void_p(P.GBASE + addr), d, 4, ctypes.byref(n)))
    finally:
        P.K32.CloseHandle(ctypes.c_void_p(h))


def ejecutar_cobros(j, par):
    """Un especial equipado debe cobrar fuera y salir con menos de su precio en Burst."""
    j.log(r"tag: empieza un combate", 90)
    P._esperar_pelea(j, 90)
    P.dormir(1.5)
    patron = (r"COMP prueba cobro: lado 1, retorno ([0-9A-F]+), banderas (\d+), "
              r"coste (\d+), gratis (true|false), ki (\d+) -> (\d+)")

    def cobros():
        return [dict(retorno=a, flags=int(b), coste=int(c), gratis=d == "true", antes=int(e), despues=int(f))
                for a, b, c, d, e, f in re.findall(patron, j.texto_log())]

    _reposo(j)
    j.tecla("set dbz3_combate_prueba_ki=6000", 500)
    _hasta(j, lambda x: x["ki"] >= 5900, 6)
    n = len(cobros())
    j.tecla("D+Quote 180", 500)
    P.dormir(1)
    fuera = [c for c in cobros()[n:] if c["flags"] == 0 and c["retorno"] == "821159F0"]
    _reposo(j)
    j.tecla("set dbz3_combate_prueba_ki=7000", 500)
    j.tecla("Q", 0)
    if not _hasta(j, lambda x: x["burst"] and x["act"] <= 2, 6):
        raise P.Fallo("COMP cobros: no entro en Burst")
    mgr = int.from_bytes(P.leer(j.h, P.GBASE + 0x823D6CF4, 4), "big")
    if not _escribe_f(j, mgr + 2096 + 6100, 500.0):
        raise P.Fallo("COMP cobros: no se preparo el ki bajo")
    n = len(cobros())
    j.tecla("D+Quote 180", 400)
    P.dormir(1)
    dentro = cobros()[n:]
    r = dict(fuera=fuera, dentro=dentro,
             fuera_paga=any(not c["gratis"] and c["antes"] - c["despues"] == c["coste"] for c in fuera),
             dentro_gratis=any(c["flags"] == 0 and c["retorno"] == "821159F0" and c["gratis"] and
                               c["antes"] == c["despues"] for c in dentro),
             entrada_bajo_precio=any(c["flags"] == 2 and c["gratis"] and c["antes"] < c["coste"] for c in dentro))
    (j.run / "comp_cobros.json").write_text(json.dumps(r, indent=2, ensure_ascii=False), encoding="utf-8")
    j.info("COMP cobros " + json.dumps(r, ensure_ascii=False))
    j.captura("cobros")
    if not all(r[k] for k in ("fuera_paga", "dentro_gratis", "entrada_bajo_precio")):
        raise P.Fallo("COMP cobros: no se verifico el especial pagado fuera/gratis dentro con ki bajo")


P.ESCENARIOS["competitivo_cobros"] = (
    "COMP: especial equipado pagado fuera/gratis en Burst aun con ki bajo",
    lambda p: dict(entorno(dict(p, caps=p.get("caps", "6,10"))), DBZ3_TEST_COMP_COBROS="1"), ejecutar_cobros)


def ejecutar_fatiga(j, par):
    j.log(r"tag: empieza un combate", 90)
    P._esperar_pelea(j, 90)
    modo = par.get("modo", "1")
    b = P.leer(j.h, P.GBASE + 0x823D6CF4, 4)
    x0 = int.from_bytes(b, "big") + 2096
    aturdido, golpes, muestras = None, 0, []
    fin = time.time() + float(par.get("seg", "40"))
    vida0 = None
    while time.time() < fin and not aturdido:
        x = _x(j)
        if not x:
            time.sleep(0.05)
            continue
        vida0 = vida0 if vida0 is not None else x["vida"]
        if x["agotado"]:
            aturdido = dict(fatiga=round(x["fatiga"], 1), ki=round(x["ki"]), accion=x["act"], vida=round(x["vida"]))
            break
        if x["fatiga"] < 990:
            _escribe_f(j, x0 + 6120, 995.0)
        if not muestras or muestras[-1][0] != round(x["vida"]):
            muestras.append([round(x["vida"]), round(x["fatiga"], 1), x["act"], round(x["ki"])])
        time.sleep(0.02)
    P.dormir(1.5)
    j.captura("b_fatiga")
    log = j.texto_log()
    r = dict(modo=modo, aturdido=aturdido, muestras=muestras[:40],
             log=re.findall(r"COMP fatiga llena.*", log)[:4])
    (j.run / "comp_fatiga.json").write_text(json.dumps(r, indent=2, ensure_ascii=False), encoding="utf-8")
    j.info("COMP fatiga " + json.dumps({k: r[k] for k in ("modo", "aturdido", "log")}, ensure_ascii=False))
    if (modo == "1") != bool(aturdido):
        raise P.Fallo("COMP fatiga: " + ("no se aturdio con la barra llena" if modo == "1" else "el original aturdio con ki"))


def entorno_fatiga(p):
    P.OVERRIDES["dbz3_modo_competitivo"] = "true" if p.get("modo", "1") == "1" else "false"
    P.OVERRIDES["dbz3_combate_prueba_muneco"] = '"ataca"'   # el ajuste manda sobre DBZ3_COMBATE_MUNECO
    return {"DBZ3_TEST_BATTLE": p.get("ids", "0,7,2"), "DBZ3_COMBATE_MUNECO": "ataca",
            "DBZ3_COMBATE_KI": "4000", "DBZ3_COMBATE_SONDA": "1"}


P.ESCENARIOS["competitivo_fatiga"] = ("COMP: aturdimiento con la barra de fatiga llena (--par modo=0 control)",
                                      entorno_fatiga, ejecutar_fatiga)
P.ESCENARIOS["competitivo_rush"] = ("COMP: Dragon Rush (hiper + P+K + R1+B); --par modo=0 control, modo=1 bloqueado",
                                    entorno_rush, ejecutar_rush)


def ejecutar_esquiva(j, par):
    """Entrada real de guardia/esquiva ante el muneco; mide la suma dentro del gancho, no entre cuadros."""
    j.log(r"tag: empieza un combate", 90)
    P._esperar_pelea(j, 90)
    modo = par.get("modo", "1") == "1"
    patron = (r"COMP prueba esquiva: lado 1, modo (true|false), Burst (true|false), "
              r"fatiga ([\d.]+) -> ([\d.]+)")

    def muestras():
        return [dict(modo=m == "true", burst=b == "true", antes=float(a), despues=float(d))
                for m, b, a, d in re.findall(patron, j.texto_log())]

    for burst in ([False, True] if modo else [False]):
        if burst:
            j.tecla("set dbz3_combate_prueba_ki=6000", 500)
            j.tecla("set dbz3_combate_prueba_muneco=quieto", 300)
            _reposo(j)
            j.tecla("Q", 600)
            if not _hasta(j, lambda x: x["burst"], 4):
                raise P.Fallo("COMP esquiva: no entro en Burst")
            j.tecla("set dbz3_combate_prueba_muneco=ataca", 300)
        j.tecla("D 900", 1000)
        for i in range(60):
            j.tecla(("Space+" + "DAWS"[i % 4] + " 60") if i % 3 else "Space 60", 160)
            if any(m["burst"] == burst and m["antes"] < 940 for m in muestras()):
                break
    r = dict(modo=modo, muestras=muestras())
    r["suma_correcta"] = all(abs(m["despues"] - min(1000.0, m["antes"] +
                              (50.0 if modo and not m["burst"] else 0.0))) < 0.02 for m in r["muestras"])
    r["fuera_medida"] = any(not m["burst"] and m["antes"] < 940 for m in r["muestras"])
    r["dentro_medida"] = any(m["burst"] and m["antes"] < 940 for m in r["muestras"])
    (j.run / "comp_esquiva.json").write_text(json.dumps(r, indent=2, ensure_ascii=False), encoding="utf-8")
    j.captura("b_esquiva")
    j.info("COMP esquiva " + json.dumps(r, ensure_ascii=False))
    if not r["suma_correcta"] or not r["fuera_medida"] or (modo and not r["dentro_medida"]):
        raise P.Fallo("COMP esquiva: falta medir una esquiva real o su fatiga no coincide con IW")


def entorno_esquiva(p):
    P.OVERRIDES["dbz3_modo_competitivo"] = "true" if p.get("modo", "1") == "1" else "false"
    P.OVERRIDES["dbz3_combate_prueba_muneco"] = '"ataca"'
    return dict(entorno(p), DBZ3_COMBATE_MUNECO="ataca", DBZ3_COMBATE_KI="6000", DBZ3_TEST_COMP_ESQUIVA="1")


P.ESCENARIOS["competitivo_esquiva"] = ("COMP: esquiva suma 50 de fatiga fuera de Burst y 0 dentro",
                                       entorno_esquiva, ejecutar_esquiva)


def oraculo_iw():
    """Fronteras del original PAL en EE, sin lanzar el juego ni escribir una partida.

    Solo se intercepta la entrada al agotamiento: se observa su peticion, no sus efectos audiovisuales.
    Tambien ejecuta el alta/baja de las ventanas de animacion, incluida la armadura (flag 4).
    No equivale a un diferencial del combate entero ni valida las animaciones de todos los personajes.
    """
    import hashlib
    from iw_mips import ee
    elf = Path(r"E:\DBZ3HD_trabajo\_bancos_archivados\ps2_eu_iw_20261021\Infinite World (Europe)\SLES_553.47")
    data = elf.read_bytes()
    if data[:4] != b"\x7fELF":
        raise ValueError("el original IW no es ELF")
    mem = bytearray(ee.TOTAL)
    phoff, phnum = struct.unpack_from("<I", data, 28)[0], struct.unpack_from("<H", data, 44)[0]
    for n in range(phnum):
        kind, off, va, _, size, msize, _, _ = struct.unpack_from("<8I", data, phoff + 32 * n)
        if kind == 1:
            if va + msize > ee.RAM or off + size > len(data):
                raise ValueError("segmento IW fuera de limites")
            mem[va:va + size] = data[off:off + size]
    cpu = ee.Ee(mem, 0x4A3DF0)
    cpu.whitelist = {0x22AAE0, 0x235180, 0x240220, 0x2400E0, 0x200E50, 0x235320, 0x22E150, 0x22E290,
                     0x234F00, 0x2AC810, 0x2AC8D0}
    cpu.estricto = True
    agotamientos = []

    def agotar(c):
        agotamientos.append((c.r[4], c.r[5], c.r[6]))
        c.r[2] = 0

    cpu.hooks[0x22DFD0] = agotar
    x = 0x1000000
    casos = []
    for activo in (0, 1):
        for ki in (0.0, 999.0, 1000.0, 1000.25, 5000.0):
            for reloj in (0.0, 1.0):
                mem[x:x + 0x20D0] = bytes(0x20D0)
                mem[x + 0x2091] = activo
                struct.pack_into("<f", mem, x + 0x185C, ki)
                struct.pack_into("<f", mem, x + 0x1AB8, reloj)
                real = cpu.call(0x22AAE0, x, limite=10000) & 0xFFFFFFFF
                esperado = int(not activo and (ki >= 1000.0 or reloj > 0.0))
                assert real == esperado, ("entrada", activo, ki, reloj, real, esperado)
                casos.append(dict(funcion="22AAE0", activo=activo, ki=ki, reloj=reloj, resultado=real))
    for activo in (0, 1):
        for fatiga in (0.0, 300.0, 600.0, 995.0):
            for delta in (-5.0, 0.0, 5.0, 1000.0):
                for flags in (0, 1):
                    mem[x:x + 0x20D0] = bytes(0x20D0)
                    mem[x + 0x2091] = activo
                    struct.pack_into("<f", mem, x + 0x1868, fatiga)
                    agotamientos.clear()
                    # ABI EE: los argumentos enteros empiezan en a0 aunque el float vaya en f12.
                    cpu.call(0x235180, x, flags, fargs=(delta,), limite=10000)
                    real = struct.unpack_from("<f", mem, x + 0x1868)[0]
                    nivel = struct.unpack_from("<I", mem, x + 0x1870)[0]
                    esperado = min(1000.0, max(0.0, fatiga + delta))
                    assert real == esperado, ("fatiga", fatiga, delta, real, esperado)
                    assert nivel == (0 if real <= 300 else 1 if real <= 600 else 2), ("nivel", real, nivel)
                    assert bool(agotamientos) == bool(delta >= 0 and flags & 1 and real >= 1000), agotamientos
                    casos.append(dict(funcion="235180", activo=activo, antes=fatiga, delta=delta, flags=flags,
                                      fatiga=real, nivel=nivel, pide_agotamiento=bool(agotamientos)))
    # Cola ORIGINAL de 0x2165F0: se omiten solo pose/efectos ya anteriores a 0x21697C.
    # Se ejecutan el filtro Burst y la llamada real a FatigueBarScript, no una formula de sustitucion.
    assert cpu.r32(0x21697C) == 0x82022091 and cpu.r32(0x216994) == 0x0C08D460
    palabras = []
    cpu.hooks[0x22F960] = lambda c: palabras.append((c.r[4], c.r[5], c.r[6]))
    for activo in (0, 1):
        for fatiga in (0.0, 300.0, 600.0, 995.0):
            mem[x:x + 0x20D0] = bytes(0x20D0)
            mem[x + 0x2091] = activo
            struct.pack_into("<f", mem, x + 0x1868, fatiga)
            sp = ee.PILA + 0xFF000
            struct.pack_into("<Q", mem, sp + 0x10, ee.RETORNO)
            cpu.r[16] = x
            palabras.clear()
            agotamientos.clear()
            cpu.call(0x21697C, sp=sp, limite=10000)
            real = struct.unpack_from("<f", mem, x + 0x1868)[0]
            assert real == min(1000.0, fatiga + (0.0 if activo else 50.0)), ("esquiva", activo, fatiga, real)
            assert not agotamientos and palabras == [(x, 1, 1)]
            casos.append(dict(funcion="21697C cola 2165F0", activo=activo, antes=fatiga, despues=real))
    del cpu.hooks[0x22F960]
    # IW X+0xABA = (X+0x910)+0x1AA. Alta de nodo 0x240220 y baja 0x2400E0:
    # el contador viene de la propiedad 4 de la animacion, no del byte Burst X+0x2091.
    # Se incluyen combinaciones con 0x200/0x400: IW usa 0x70F, B3 solo 0x10F.
    # Esta prueba no autoriza a copiar esas propiedades nuevas a los datos B3 sin traducirlas.
    anim, nodo, pista = x + 0x910, x + 0x3000, x + 0x3100
    for activo in (0, 1):
        for flags in (0, 1, 2, 4, 8, 0x100, 0x104, 0x200, 0x204, 0x400, 0x404):
            mem[x:x + 0x3200] = bytes(0x3200)
            mem[x + 0x2091] = activo
            struct.pack_into("<I", mem, anim + 0x180, nodo)
            mem[pista + 2] = 7
            struct.pack_into("<H", mem, pista + 8, flags)
            cpu.call(0x240220, anim, pista, limite=10000)
            efectivos = struct.unpack_from("<H", mem, pista + 8)[0]
            armadura = mem[x + 0xABA]
            assert armadura == int((efectivos & 0x70F) == 4), ("armadura", activo, flags, armadura)
            assert mem[anim + 0x1A8] == 1 and cpu.r32(anim + 0x1A0) == nodo
            cpu.call(0x2400E0, anim, 7, limite=10000)
            assert mem[anim + 0x1A8:anim + 0x1B0] == bytes(8), ("fin ventana", activo, flags)
            assert cpu.r32(anim + 0x1A0) == 0 and mem[x + 0x2091] == activo
            casos.append(dict(funcion="240220/2400E0", activo=activo, flags=flags,
                              efectivos=efectivos, armadura=armadura, al_terminar=0))
    # Se ejecutan tambien las comprobaciones/cobros ORIGINALES, sin interceptar HitDamageCount.
    # A bajo ki un especial en Burst pasa; un coste ordinario o de definitiva no se vuelve gratis.
    entrada = x + 0x3000
    for activo in (0, 1):
        for ki in (500.0, 4000.0):
            for flags in (1, 9, 0x10001, 0x20001, 0x30001):
                mem[x:x + 0x3200] = bytes(0x3200)
                mem[x + 0x2091] = activo
                mem[x + 0x2060] = 2
                struct.pack_into("<f", mem, x + 0x185C, ki)
                struct.pack_into("<f", mem, x + 0x1860, 7000.0)
                struct.pack_into("<f", mem, x + 0x1AA8, 1.0)
                struct.pack_into("<I", mem, entrada + 8, flags)
                struct.pack_into("<h", mem, entrada + 0x12, 1000)
                real = cpu.call(0x200E50, entrada, x, limite=10000) & 0xFFFFFFFF
                esperado = int(ki >= 1000 or activo and flags & 0x30000 != 0)
                assert real == esperado, ("entrada ki", activo, ki, flags, real, esperado)
                assert struct.unpack_from("<f", mem, x + 0x185C)[0] == ki
                casos.append(dict(funcion="200E50", activo=activo, ki=ki, flags=flags, resultado=real))
            # El flag 2 IW consulta posesion/intervalo de capsulas; no consulta ni cobra ki.
            for equipado in (0, 1):
                for flags in (2, 0x10002):
                    mem[x:x + 0x3200] = bytes(0x3200)
                    mem[x + 0x2091], mem[x + 0x2060] = activo, 2
                    struct.pack_into("<f", mem, x + 0x185C, ki)
                    struct.pack_into("<I", mem, entrada + 8, flags)
                    struct.pack_into("<h", mem, entrada + 0x16, 6)
                    struct.pack_into("<h", mem, x + 0xB9C, 6 if equipado else -1)
                    real = cpu.call(0x200E50, entrada, x, limite=10000) & 0xFFFFFFFF
                    assert real == equipado, ("posesion", activo, ki, flags, equipado, real)
                    assert struct.unpack_from("<f", mem, x + 0x185C)[0] == ki
                    casos.append(dict(funcion="200E50 flag2", activo=activo, ki=ki, flags=flags,
                                      equipado=equipado, resultado=real))
            for funcion, coste in ((0x22E150, 3000), (0x22E290, 300)):
                for agotado in (0, 1):
                    mem[x:x + 0x20D0] = bytes(0x20D0)
                    mem[x + 0x2091], mem[x + 0x2060], mem[x + 0x1876] = activo, 2, agotado
                    struct.pack_into("<f", mem, x + 0x185C, ki)
                    struct.pack_into("<f", mem, x + 0x1860, 7000.0)
                    struct.pack_into("<f", mem, x + 0x1AA8, 1.0)
                    real = cpu.call(funcion, x, 0, 0, 0, limite=10000) & 0xFFFFFFFF
                    esperado = int(not agotado and (activo or ki >= coste))
                    assert real == esperado, ("defensa", hex(funcion), activo, ki, agotado, real, esperado)
                    assert struct.unpack_from("<f", mem, x + 0x185C)[0] == ki
                    casos.append(dict(funcion=f"{funcion:X}", activo=activo, ki=ki,
                                      agotado=agotado, resultado=real))
        # La funcion generica original cobra incluso en Burst: no hay una gratuidad global.
        for flags in (0, 2, 4, 10):
            mem[x:x + 0x20D0] = bytes(0x20D0)
            mem[x + 0x2091], mem[x + 0x2060] = activo, 2
            struct.pack_into("<f", mem, x + 0x185C, 4000.0)
            struct.pack_into("<f", mem, x + 0x1860, 7000.0)
            struct.pack_into("<f", mem, x + 0x1AA8, 1.0)
            cpu.call(0x235320, x, flags, fargs=(1000.0,), limite=10000)
            ki = struct.unpack_from("<f", mem, x + 0x185C)[0]
            assert ki == (4000.0 if flags & 2 else 3000.0), ("cobro generico", activo, flags, ki)
            casos.append(dict(funcion="235320", activo=activo, flags=flags, ki_final=ki))
    assert not cpu.sin_puente and not cpu.desconocidas and not cpu.io
    print(json.dumps(dict(elf_sha256=hashlib.sha256(data).hexdigest(), casos=len(casos), diferencias=0,
                          instrucciones=cpu.n,
                          alcance="entrada, fatiga, ventanas y cobros; sin colision ni efectos de agotamiento"), ensure_ascii=False))
    print(f"PASS oraculo IW: {len(casos)} casos de entrada, fatiga, ventanas y cobros, 0 diferencias")


if __name__ == "__main__":
    if sys.argv[1:] == ["--oraculo-iw"]:
        oraculo_iw()
    else:
        sys.exit(P.main())
