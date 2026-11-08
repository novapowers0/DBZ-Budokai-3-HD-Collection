r"""comparar_modo.py (X0b, studies 5) - port contra original en UN comando, sin humano.

  python tools\comparar_modo.py <modo> <escena> [--copia smoke_d] [--log ruta.log] [--turbo N]
  python tools\comparar_modo.py crear-dorado b2 fase1 E:\DBZ3HD_trabajo\R7\emu\logs\b2_traza_ordenes.log

Capas (01_ROADMAP.md §3): 1) log dorado (numeros), 2) imagen (SSIM + pHash por pareja de capturas), 3) revisor con vision
(solo para las parejas que fallen: lo hace el coordinador con los recortes; aqui solo se listan).

  modo    escena    dorado (tools\dorados\<modo>_<escena>.json)         que compara
  b2      fase1     ordenes de guion del tablero (original, PINE)         orden y presencia de las ordenes de guion
  iw      <clave>   resumen de camara/luchadores (original, PINE)         salida y camara relativa al jugador

Sin --log lanza el port (pruebas.py, escena <modo>:<escena>, ventana oculta) en una copia N1 y lee su log. El resultado
es JSON en tools\dorados\resultados\ y una firma (sha1 de los numeros): dos pasadas iguales dan la misma firma.
Codigo de salida: 0 todo bien, 1 algo distinto, 2 sin dorado o sin datos del port.
"""
import ast
import hashlib
import json
import re
import sys
import time
from pathlib import Path

AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI))
import lector_camara as LC  # noqa: E402

DORADOS = AQUI / "dorados"
RESULTADOS = DORADOS / "resultados"
ESCENA_PORT = {("b2", f"fase{n}"): f"b2:fase{n}" for n in range(1, 10)}     # modo:escena -> escena de pruebas.py (iw: iw:mini:<clave>)
_ORDEN_PORT = re.compile(r"tablero b2: orden ejecutada (\d+)")   # traza completa (DBZ3_TEST_B2_ORDENES, pruebas.py b2:*)


# ------------------------------------------------------------------ T1B mejora 4: grabacion valida y clase de fallo
def validar_original(modo, escena, oro):
    """Problemas de la grabacion del original ANTES de comparar ([] = valida): numero de muestras, que no este vacia y que el
    contador avance. Si hay alguno, el resultado es 'herramienta' (la grabacion salio mala), no un FAIL del port."""
    mal = []
    if (modo, escena) == ("b2", "nave"):
        return [] if oro.get("plantas") else ["el dorado de la Nave no trae plantas"]
    if modo == "b2":
        if not oro.get("secuencia"):
            mal.append("sin ordenes de guion grabadas (secuencia vacia)")
        t = oro.get("tiempos") or []
        if any(b < a for a, b in zip(t, t[1:])):
            mal.append("los tiempos de la grabacion retroceden (contador no avanza)")
        return mal
    if modo == "iw":
        m = oro.get("muestras") or []
        vivas = [f for f in m if any(abs(x) > 1e-6 for x in f.get("eye", (0, 0, 0)))]
        if len(m) < 5 or len(vivas) < 5:
            mal.append(f"muestras insuficientes: {len(m)} en total, {len(vivas)} con ojo (minimo 5)")
        ts = [f["t"] for f in m if "t" in f]
        if len(ts) >= 2 and (ts[-1] <= ts[0] or any(b < a for a, b in zip(ts, ts[1:]))):
            mal.append("el contador de tiempo no avanza en la grabacion")
        if not (oro.get("resumen") or {}).get("n_vivas"):
            mal.append("el resumen del original no tiene filas vivas")
    return mal


def clase_fila(nombre, ok, detalle):
    """'' (ok) | 'medida' (no se pudo medir: [H] o dato que no cuenta) | 'herramienta' (sin datos) | 'juego' (diferencia real)."""
    if ok:
        return ""
    if ok is None:
        return "medida"
    if re.search(r"sin muestras|no arranc|sin linea|exe anterior|falta ", detalle + nombre):
        return "herramienta"
    return "juego"


# ------------------------------------------------------------------ b2: ordenes de guion
def ordenes_original(ruta_log):
    """b2_traza_ordenes.log (traza_ordenes_b2.py sobre PCSX2): primera pasada, ordenes en el orden en que se ejecutan."""
    seq, cuenta, dentro = [], {}, 0
    for l in Path(ruta_log).read_text(encoding="utf-8", errors="replace").splitlines():
        if l.startswith("==="):
            dentro += 1
            if dentro > 1:
                break
        m = re.search(r"\sorden\s+(\d+)\s", l)
        if m:
            k = int(m.group(1))
            if not seq or seq[-1] != k:
                seq.append(k)
        m = re.search(r"recuento: (\{.*\})", l)
        if m and dentro == 1:
            cuenta = {int(k): v for k, v in ast.literal_eval(m.group(1)).items()}   # {1: 2, 7: 101...}
    return seq, cuenta


def comparar_ordenes(oro, texto_port):
    port = [int(x) for x in _ORDEN_PORT.findall(texto_port)]
    port = [k for i, k in enumerate(port) if i == 0 or port[i - 1] != k]   # misma regla que el dorado (sin repetidas seguidas)
    gold = list(dict.fromkeys(oro["secuencia"]))
    faltan = [k for k in gold if k not in port]
    # B2_FASE2 7/11: el orden se mide como SUBSECUENCIA: la secuencia del original (con sus repeticiones) debe aparecer
    # entera y en orden dentro de la del port. Por primera aparicion fallaba con una orden de mas ANTES de la ventana del
    # dorado (fase 2: orden 1 del guion 308, previa a la lista de aliados donde empieza el savestate del original).
    g = oro["secuencia"]
    if oro.get("sitios") and len(oro["sitios"]) == len(g):
        # B2_FASE2: la traza del original anota cada LLAMADA (una orden que espera se llama en cada cuadro y se intercala con
        # la de otro hilo: 19 7 19 7...). El port anota cada orden una vez. Con la direccion de la orden en el guion (a0) se
        # cuenta cada instruccion una sola vez, en el orden de su primera llamada.
        vistos, g2 = set(), []
        for k, s in zip(g, oro["sitios"]):
            if (k, s) not in vistos:
                vistos.add((k, s))
                g2.append(k)
        g = [k for i, k in enumerate(g2) if i == 0 or g2[i - 1] != k]
    seq_g = [k for k in g if k in port]
    it, hechas = iter(port), 0
    for k in seq_g:
        if any(k == x for x in it):
            hechas += 1
        else:
            break
    # Mas estricta: el bloque del original seguido y sin huecos en el port. La traza del original solo cubre 1-86
    # (grabar_b2), asi que la orden 0 del port no cuenta.
    p86 = [k for k in port if 1 <= k <= 86]
    p86 = [k for i, k in enumerate(p86) if i == 0 or p86[i - 1] != k]
    bloque = bool(g) and any(p86[i:i + len(g)] == g for i in range(len(p86) - len(g) + 1))
    # Auditoria 7/11: las ordenes de MAS del port no restaban. Se cuentan las que el port mete entre la primera y la ultima
    # orden del original emparejadas (ventana del dorado). Un dorado con menos de 3 ordenes distintas no prueba nada: [H].
    de_mas = None   # ventana mas estrecha: se prueba cada arranque posible (la primera orden puede repetirse antes)
    for s in (i for i, x in enumerate(p86) if g and x == g[0]):
        j, ok = s, True
        for k in g:
            while j < len(p86) and p86[j] != k:
                j += 1
            if j == len(p86):
                ok = False
                break
            j += 1
        if ok and (de_mas is None or j - s - len(g) < de_mas):
            de_mas = j - s - len(g)
    corto = len(set(g)) < 3
    ok_bloque = None if (not g or corto) else bloque
    return [("bloque del original identico y seguido en el port", ok_bloque,
             f"{len(g)} ordenes {g}" + ("" if bloque else " no aparecen seguidas (sin contar la orden 0)")
             + (f"; dorado corto ({len(set(g))} distintas): no cuenta como igual [H]" if corto else "")),
            ("ordenes de mas del port dentro de la ventana del original", None if corto or de_mas is None else de_mas == 0,
             "sin emparejar entera" if de_mas is None else f"{de_mas} de mas"),
            ("ordenes del original vistas en el port", not faltan,
             f"{len(gold) - len(faltan)}/{len(gold)}; faltan {faltan} (el port no las registra o no las ejecuta)"),
            ("mismo orden relativo de las que estan", hechas == len(seq_g),
             f"{hechas}/{len(seq_g)} de la secuencia del original {seq_g} en orden dentro del port {port[:40]}"),
            ("el port registra alguna orden de guion", bool(port), "lineas 'orden ejecutada N' en el log del port")]


# ------------------------------------------------------------------ b2: Nave de Babidi (cifras por planta)
_HORA = re.compile(r"^\[\d{4}-\d\d-\d\d (\d\d):(\d\d):(\d\d\.\d+)\]")


def _seg(linea):
    m = _HORA.match(linea)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)) if m else None


def comparar_nave(oro, textos):
    """Una partida por planta (pruebas.py r2_nave --par prueba=N). Compara con el dorado b2_nave.json (original medido):
    regla de tiempo, pago por unidad, cuenta x pago = total, fin del combate (estado 5) -> torre, sin tarjeta VS, y en la
    planta I el rival que se levanta (si la partida tuvo KO)."""
    filas = []
    for texto in textos:
        lineas = texto.splitlines()
        m = next((re.search(r"nave: prueba (\d): .*regla (-?\d+) s pago (\d+)\)", l) for l in lineas
                  if re.search(r"nave: prueba \d: .*regla", l)), None)
        if not m:
            filas.append(("nave: partida sin linea 'prueba N: ... regla'", None, "exe anterior a B2_FASE2 o no llego al combate"))
            continue
        n, regla, pago = m.group(1), int(m.group(2)), int(m.group(3))
        o = oro["plantas"][n]
        filas.append((f"planta {n}: reloj", regla == o["tiempo"], f"port {regla} s, original {o['tiempo']} s ({o['nota']})"))
        if "pago" in o:
            filas.append((f"planta {n}: Kili por unidad", pago == o["pago"], f"port {pago}, original {o['pago']}"))
        r = next((re.search(r"resultado de la prueba \d: (\d+) x (\d+) = (\d+) Kili", l) for l in lineas
                  if "resultado de la prueba" in l), None)
        if r and "pago" in o:
            c, p, t = map(int, r.groups())
            filas.append((f"planta {n}: resultado cuenta x pago", (p == o["pago"] and t == c * p) if c else None,
                          f"port {c} x {p} = {t}" + (" (cuenta 0: no prueba la multiplicacion) [H]" if not c else "")))
        t5 = next((_seg(l) for l in lineas if "nave paso 1 estado del combate 5" in l), None)
        tt = next((_seg(l) for l in lineas if t5 is not None and "nave: pantalla planta" in l and _seg(l) and _seg(l) > t5),
                  None)
        if "fin_a_torre_s" in o:
            if t5 is None or tt is None:
                filas.append((f"planta {n}: fin del combate -> torre", None, "sin estado 5 o sin vuelta a la torre en el log"))
            else:
                a, b = o["fin_a_torre_s"]
                filas.append((f"planta {n}: fin del combate -> torre", a <= tt - t5 <= b,
                              f"port {tt - t5:.1f} s, original {a}-{b} s"))
        # marcador por planta (iw_nave_hud_b2): el de B3 se oculta y se pinta el de B2 (cotejado 01/11, NAVE_PLANTAS.md)
        filas.append((f"planta {n}: marcador propio de B2 (el de B3 oculto)", any("combate: marcador oculto" in l for l in lineas),
                      "linea 'combate: marcador oculto' durante el combate"))
        vs = any(re.search(r"nave: pantalla (tarjeta|vs)", l, re.I) for l in lineas)
        filas.append((f"planta {n}: sin tarjeta VS", vs == oro["tarjeta_vs"], "el original pasa de la burbuja a la carga"))
        if n == "1":
            # KO dentro del mismo combate (tagteam.cpp, desde 7/11); si no, el fin de B3 (estado 5) de la cadena vieja
            lev = [_seg(l) for l in lineas if "el rival se levanta" in l]
            ko = next((_seg(l) for l in lineas if "prueba I: KO del rival" in l), None) or                 next((_seg(l) for l in lineas if "nave paso 1 estado del combate 5" in l), None)
            if lev and ko:
                a, b = o["revive_s"]
                filas.append(("planta 1: el rival se levanta tras el KO", a <= lev[0] - ko <= b,
                              f"port {lev[0] - ko:.1f} s desde el KO, original ~2,5 s"))
            else:
                filas.append(("planta 1: el rival se levanta tras el KO", None, "la partida no tuvo KO del rival [H]"))
    return filas


# ------------------------------------------------------------------ imagenes
def comparar_imagenes(oro, run):
    out = []
    for par in oro.get("capturas", []):
        org, port = Path(par["original"]), Path(run) / (par["port"] + ".png")
        if not org.exists() or not port.exists():
            out.append((f"imagen {par['port']}", None, f"falta {'original' if not org.exists() else 'captura del port'} [H]"))
            continue
        import comparar
        m = comparar.metricas(str(org), str(port))
        ok = m["ssim"] >= par.get("ssim_min", 0.6) and m["phash_hamming"] <= par.get("phash_max", 16)
        if not ok and m["alineado"] == "sin alinear":   # PS2 frente a HD sin alinear: no es medida, va al revisor [H]
            ok = None
        out.append((f"imagen {par['port']}", ok, f"ssim {m['ssim']} (min {par.get('ssim_min', 0.6)}) "
                                                   f"phash {m['phash_hamming']} (max {par.get('phash_max', 16)}) {m['alineado']}"))
    if not oro.get("capturas"):
        out.append(("imagenes", None, "el dorado no tiene parejas de capturas [H]"))
    return out


# ------------------------------------------------------------------ orquestacion
def lanzar_port(modo, escena, copia, turbo, oro_teclas="", oro_seg=40):
    import pruebas as P
    spec = ESCENA_PORT.get((modo, escena)) or (f"iw:mini:{escena}" if modo == "iw" else None)
    if not spec:
        raise SystemExit(f"no se como saltar a {modo}:{escena} en el port (ESCENA_PORT)")
    env = ["DBZ3_TEST_OCULTO=1", f"DBZ3_TEST_TURBO={turbo}"]
    par = {"escena": spec, "quieto": "45" if modo == "b2" else "1.5"}
    if modo == "b2" and oro_teclas:                  # mismas teclas que en el original (dorado["teclas"])
        par["teclas"] = oro_teclas
        par["quieto"] = str(max(10, int(oro_seg)))
        if "--manual" in sys.argv:                    # sin piloto automatico: el tablero solo avanza con estas teclas
            par["manual"] = "1"
    if modo == "iw":
        par["fin"] = "1"
    r = P.correr(copia, "escena", env, None, par)
    if not r.get("run"):
        raise SystemExit("el port no arranco: " + r.get("msg", ""))
    return (Path(r["run"]) / "dbz3.log").read_text(encoding="utf-8", errors="replace"), Path(r["run"])


# Guion de teclas comun: sintaxis de sonda.py ("z espera2000 right*2 x"). En el original lo reproduce sonda.teclas; en el
# port, pruebas.py con la traduccion de abajo (cruz = Space, circulo = Backspace, triangulo = P, cuadrado = L).
TECLAS_PORT = {"z": "Space", "x": "Backspace", "s": "P", "a": "L", "up": "W", "down": "S", "left": "A", "right": "D",
               "start": "Return"}


def traducir_teclas(guion):
    """'z espera2000 right*2' -> [('Space', 0), ('esperar', 2.0), ('D', 0), ('D', 0)] para pruebas.py."""
    out = []
    for tok in guion.split():
        if tok.startswith("espera"):
            out.append(("esperar", int(tok[6:]) / 1000))
            continue
        if tok.startswith("cap="):                  # captura con nombre (pareja del dorado["capturas"][i]["port"])
            out.append(("captura", tok[4:]))
            continue
        rep = 1
        if "*" in tok:
            tok, r = tok.split("*")
            rep = int(r)
        ks = "+".join(TECLAS_PORT[k] for k in tok.lower().split("+"))
        out += [(ks, 0)] * rep
    return out


def grabar_b2(escena, estado, teclas, seg, mantener=False):
    """Original (PCSX2 por PINE): carga el savestate, traza la tabla de ordenes 0x379EC0 (1-86) y reproduce `teclas`.
    Escribe tools/dorados/b2_<escena>.json con la secuencia, el recuento y el guion de teclas. Requiere PCSX2 libre."""
    import threading
    import pcsx2_sonda as sonda
    lanzado = not sonda.nuestro_pid()
    if not lanzado and not str(estado).isdigit():
        raise SystemExit("un savestate por ruta necesita PCSX2 cerrado (se abre con -statefile)")
    p = sonda.Pine() if not lanzado else sonda.lanzar("b2", estado)
    orig = None
    try:
        if str(estado).isdigit():
            p.cargar(int(estado))
        time.sleep(2)
        orig = sonda.traza_tabla(p, 0x379EC0, list(range(1, 87)))
        threading.Thread(target=sonda.teclas, args=(teclas,), daemon=True).start()
        visto, t0, seq, cuenta, ultimo, tiempos, sitios = 0, time.time(), [], {}, None, [], []
        while time.time() - t0 < seg:
            tot, es = sonda.leer_traza(p, visto)
            for _sq, k, a0, a1, a2, a3, _ra, _arg in es:
                cuenta[k] = cuenta.get(k, 0) + 1
                if (k, a0, a1, a2, a3) != ultimo and (not seq or seq[-1] != k):
                    seq.append(k)
                    tiempos.append(round(time.time() - t0, 1))   # B2_FASE2: separar tiempo de logica
                    sitios.append(f"{a0:X}")   # direccion de la orden en el guion: misma = la misma orden repetida
                ultimo = (k, a0, a1, a2, a3)
            visto = tot
            time.sleep(0.03)
    finally:   # el emulador que lanzamos nosotros se cierra aunque falle la traza (no se toca el de otra linea)
        if orig is not None:
            sonda.quitar_traza_tabla(p, 0x379EC0, orig)
        if lanzado and not mantener:
            sonda.main(["cerrar"])
    DORADOS.mkdir(exist_ok=True)
    (DORADOS / f"b2_{escena}.json").write_text(json.dumps(
        {"modo": "b2", "escena": escena, "fuente": f"PCSX2 PINE, savestate {estado}, {seg} s",
         "estado": estado, "teclas": teclas, "seg": seg, "secuencia": seq, "tiempos": tiempos, "sitios": sitios,
         "recuento": dict(sorted(cuenta.items())), "capturas": []}, indent=1), encoding="utf-8")
    print(f"dorado b2_{escena}.json: {len(seq)} ordenes en secuencia, recuento {dict(sorted(cuenta.items()))}")


def crear_dorado_b2(escena, log):
    seq, cuenta = ordenes_original(log)
    DORADOS.mkdir(exist_ok=True)
    (DORADOS / f"b2_{escena}.json").write_text(json.dumps(
        {"modo": "b2", "escena": escena, "fuente": Path(log).name, "secuencia": seq, "recuento": cuenta, "capturas": []},
        indent=1), encoding="utf-8")
    print(f"dorado b2_{escena}.json: {len(seq)} ordenes en secuencia, recuento {cuenta}")


def autoprueba():
    ok = {"resumen": {"n_vivas": 9}, "muestras": [{"t": i, "eye": [1, 2, 3]} for i in range(9)]}
    assert validar_original("iw", "x", ok) == []
    assert validar_original("iw", "x", {"resumen": {}, "muestras": []})                        # vacia
    assert validar_original("iw", "x", dict(ok, muestras=ok["muestras"][:3]))                  # pocas muestras
    assert validar_original("iw", "x", dict(ok, muestras=[{"t": 5, "eye": [1, 0, 0]}] * 9))     # contador parado
    assert validar_original("b2", "fase1", {"secuencia": [1, 2]}) == [] and validar_original("b2", "f", {"secuencia": []})
    assert validar_original("b2", "f", {"secuencia": [1], "tiempos": [3, 1]})
    assert clase_fila("a", True, "") == "" and clase_fila("a", None, "[H]") == "medida"
    assert clase_fila("a", False, "sin muestras de ojo del port") == "herramienta" and clase_fila("a", False, "dist 3 vs 9") == "juego"
    for f in sorted(DORADOS.glob("iw_0x7D*.json")) + sorted(DORADOS.glob("b2_fase*.json")):   # los dorados guardados son validos
        o = json.loads(f.read_text(encoding="utf-8"))
        print("  ", f.name, validar_original(o["modo"] if "modo" in o else "iw", f.stem.split("_", 1)[1], o) or "valido")
    print("AUTOPRUEBA comparar_modo OK")


def main(a):
    if a[:1] == ["--autoprueba"]:
        return autoprueba() or 0
    if len(a) >= 5 and a[0] == "grabar-b2":      # grabar-b2 fase1 <estado> "<teclas>" [seg]
        grabar_b2(a[1], a[2], a[3], float(a[4]) if len(a) > 4 else 40)
        return 0
    if len(a) >= 4 and a[0] == "crear-dorado" and a[1] == "b2":
        crear_dorado_b2(a[2], a[3])
        return 0
    if len(a) < 2:
        print(__doc__)
        return 2
    modo, escena = a[0], a[1]
    copia = a[a.index("--copia") + 1] if "--copia" in a else "smoke_d"
    turbo = int(a[a.index("--turbo") + 1]) if "--turbo" in a else 1
    ruta = DORADOS / f"{modo}_{escena}.json"
    if not ruta.exists():
        print(f"SIN DORADO: falta {ruta}. Se crea con PCSX2 abierto (lector_camara.py dorado / traza_ordenes_b2.py).")
        return 2
    oro = json.loads(ruta.read_text(encoding="utf-8"))
    mal = validar_original(modo, escena, oro)
    if mal:
        print(f"HERRAMIENTA: grabacion del original no valida ({ruta.name}): " + "; ".join(mal)
              + ". No se compara: regrabar el original (no es un fallo del port).")
        return 2
    if (modo, escena) == ("b2", "nave"):              # --log a.log --log b.log ... (una partida por planta)
        logs = [a[i + 1] for i, x in enumerate(a) if x == "--log"]
        if not logs:
            import pruebas as P
            for k in "0123":
                # planta II: la cuenta del original medido fue 2 (2 s -> 4 Kili); la pantalla de resultado dura segun la cuenta
                env = ["DBZ3_TEST_OCULTO=1"] + (["DBZ3_TEST_NAVE_CUENTA=2"] if k == "1" else [])
                r = P.correr(copia, "r2_nave", env, None, dict({"prueba": k}, **({"golpes": "80"} if k == "0" else {})))
                if r.get("run"):
                    logs.append(str(Path(r["run"]) / "dbz3.log"))
        textos = [Path(x).read_text(encoding="utf-8", errors="replace") for x in logs]
        filas, run = comparar_nave(oro, textos), None
    elif "--log" in a:
        texto, run = Path(a[a.index("--log") + 1]).read_text(encoding="utf-8", errors="replace"), None
    else:
        texto, run = lanzar_port(modo, escena, copia, turbo, oro.get("teclas", ""), oro.get("seg", 40))
    if (modo, escena) == ("b2", "nave"):
        pass
    elif modo == "b2":
        filas = comparar_ordenes(oro, texto)
    elif modo == "iw":
        filas = LC.comparar_resumen(oro["resumen"], LC.resumen(LC.serie_log_port("iw", texto)))
        # P2 7/11: la camara se compara relativa al jugador y por tramo de #AMC (diferencial.comparar_camara_iw); las filas de
        # mediana unica (dist/altura/yaw absolutos) se sustituyen. Si el dorado trae menos de 5 muestras con ojo, quedan las viejas.
        import diferencial as DIF
        # (-15,0,0) es el valor por defecto de j antes de colocar al jugador (las capturas largas empiezan ahi): no es una "salida"
        if any(abs(x - y) < 1e-6 for x, y in [(oro["resumen"].get("salida", [0, 0, 0])[0], -15.0)]) and oro["resumen"].get("salida", [0, 0, 0])[1:] == [0.0, 0.0]:
            filas = [f for f in filas if f[0] != "salida del jugador"]
        nuevas = DIF.comparar_camara_iw(oro.get("muestras", []), DIF.serie_port_iw(texto))
        if nuevas and not (len(nuevas) == 1 and nuevas[0][1] is None):
            filas = [f for f in filas if not re.match(r"dist_ojo|altura_ojo|yaw del ojo", f[0])] + nuevas
        # fase 1: camara que corta entre >= 4 planos fijos (7D5/7DA) o, si no, giro de la de seguimiento de campo
        filas += (DIF.planos_iw(oro.get("muestras", []), DIF.serie_port_iw(texto))
                  or DIF.giro_reposo_iw(oro.get("muestras", []), DIF.serie_port_iw(texto)))
    else:
        print(f"modo '{modo}' sin comparador todavia [H]")
        return 2
    if run:
        filas += comparar_imagenes(oro, run)
    # fallos de herramienta (el port no dejo datos, no arranco): se repite UNA vez, sola, cuando el port lo lanzamos nosotros
    if any(clase_fila(*f) == "herramienta" for f in filas) and "--log" not in a and "--reintento" not in a             and (modo, escena) != ("b2", "nave"):
        print("REINTENTO (1 vez): fallo de herramienta en " + "; ".join(f[0] for f in filas if clase_fila(*f) == "herramienta"))
        return main(a + ["--reintento"])
    malos = [f for f in filas if f[1] is False]
    pend = [f for f in filas if f[1] is None]
    res = {"modo": modo, "escena": escena, "dorado": ruta.name, "fuente_dorado": oro.get("fuente"),
           "filas": [{"nombre": n, "ok": ok, "detalle": d, "clase": clase_fila(n, ok, d)} for n, ok, d in filas]}
    res["firma"] = hashlib.sha1(json.dumps(res["filas"], sort_keys=True).encode()).hexdigest()[:12]
    RESULTADOS.mkdir(parents=True, exist_ok=True)
    (RESULTADOS / f"{modo}_{escena}_{time.strftime('%Y%m%d-%H%M%S')}.json").write_text(
        json.dumps(res, indent=1, ensure_ascii=False), encoding="utf-8")
    for n, ok, d in filas:
        print(f"{'OK  ' if ok else 'H   ' if ok is None else 'FAIL'}{'[' + clase_fila(n, ok, d) + ']' if ok is False else ''}  {n}: {d}")
    herr = [f for f in malos if clase_fila(*f) == "herramienta"]
    print(f"{modo}:{escena} -> {len(filas) - len(malos) - len(pend)} ok, {len(malos) - len(herr)} fallos del juego, "
          f"{len(herr)} de herramienta, {len(pend)} [H]  firma {res['firma']}")
    if herr and len(herr) == len(malos):
        print("HERRAMIENTA: el port no dejo datos ni tras repetir; no cuenta como fallo del juego.")
        return 2
    return 1 if malos else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
