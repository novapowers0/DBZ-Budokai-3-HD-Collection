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
ESCENA_PORT = {("b2", "fase1"): "b2:fase1", }       # modo:escena -> escena de pruebas.py (iw: iw:mini:<clave>)
_ORDEN_PORT = re.compile(r"tablero b2: orden ejecutada (\d+)")   # traza completa (DBZ3_TEST_B2_ORDENES, pruebas.py b2:*)


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
    primeras = list(dict.fromkeys(port))                          # orden de primera aparicion
    gold = list(dict.fromkeys(oro["secuencia"]))
    faltan = [k for k in gold if k not in port]
    # las que estan: deben aparecer en el mismo orden relativo que en el original
    comunes_g = [k for k in gold if k in port]
    comunes_p = [k for k in primeras if k in gold]
    return [("ordenes del original vistas en el port", not faltan,
             f"{len(gold) - len(faltan)}/{len(gold)}; faltan {faltan} (el port no las registra o no las ejecuta)"),
            ("mismo orden relativo de las que estan", comunes_g == comunes_p, f"original {comunes_g} port {comunes_p}"),
            ("el port registra alguna orden de guion", bool(port), "lineas 'orden ejecutada N' en el log del port")]


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
    sys.path.insert(0, LC.R7_EMU)
    import sonda
    lanzado = not sonda.nuestro_pid()
    p = sonda.Pine() if not lanzado else sonda.lanzar("b2", estado)
    orig = None
    try:
        p.cargar(estado)
        time.sleep(2)
        orig = sonda.traza_tabla(p, 0x379EC0, list(range(1, 87)))
        threading.Thread(target=sonda.teclas, args=(teclas,), daemon=True).start()
        visto, t0, seq, cuenta, ultimo = 0, time.time(), [], {}, None
        while time.time() - t0 < seg:
            tot, es = sonda.leer_traza(p, visto)
            for _sq, k, a0, a1, a2, a3, _ra, _arg in es:
                cuenta[k] = cuenta.get(k, 0) + 1
                if (k, a0, a1, a2, a3) != ultimo and (not seq or seq[-1] != k):
                    seq.append(k)
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
         "estado": estado, "teclas": teclas, "seg": seg, "secuencia": seq,
         "recuento": dict(sorted(cuenta.items())), "capturas": []}, indent=1), encoding="utf-8")
    print(f"dorado b2_{escena}.json: {len(seq)} ordenes en secuencia, recuento {dict(sorted(cuenta.items()))}")


def crear_dorado_b2(escena, log):
    seq, cuenta = ordenes_original(log)
    DORADOS.mkdir(exist_ok=True)
    (DORADOS / f"b2_{escena}.json").write_text(json.dumps(
        {"modo": "b2", "escena": escena, "fuente": Path(log).name, "secuencia": seq, "recuento": cuenta, "capturas": []},
        indent=1), encoding="utf-8")
    print(f"dorado b2_{escena}.json: {len(seq)} ordenes en secuencia, recuento {cuenta}")


def main(a):
    if len(a) >= 5 and a[0] == "grabar-b2":      # grabar-b2 fase1 <estado> "<teclas>" [seg]
        grabar_b2(a[1], int(a[2]), a[3], float(a[4]) if len(a) > 4 else 40)
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
    if "--log" in a:
        texto, run = Path(a[a.index("--log") + 1]).read_text(encoding="utf-8", errors="replace"), None
    else:
        texto, run = lanzar_port(modo, escena, copia, turbo, oro.get("teclas", ""), oro.get("seg", 40))
    if modo == "b2":
        filas = comparar_ordenes(oro, texto)
    elif modo == "iw":
        filas = LC.comparar_resumen(oro["resumen"], LC.resumen(LC.serie_log_port("iw", texto)))
    else:
        print(f"modo '{modo}' sin comparador todavia [H]")
        return 2
    if run:
        filas += comparar_imagenes(oro, run)
    malos = [f for f in filas if f[1] is False]
    pend = [f for f in filas if f[1] is None]
    res = {"modo": modo, "escena": escena, "dorado": ruta.name, "fuente_dorado": oro.get("fuente"),
           "filas": [{"nombre": n, "ok": ok, "detalle": d} for n, ok, d in filas]}
    res["firma"] = hashlib.sha1(json.dumps(res["filas"], sort_keys=True).encode()).hexdigest()[:12]
    RESULTADOS.mkdir(parents=True, exist_ok=True)
    (RESULTADOS / f"{modo}_{escena}_{time.strftime('%Y%m%d-%H%M%S')}.json").write_text(
        json.dumps(res, indent=1, ensure_ascii=False), encoding="utf-8")
    for n, ok, d in filas:
        print(f"{'OK  ' if ok else 'H   ' if ok is None else 'FAIL'}  {n}: {d}")
    print(f"{modo}:{escena} -> {len(filas) - len(malos) - len(pend)} ok, {len(malos)} fallos, {len(pend)} [H]  firma {res['firma']}")
    return 1 if malos else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
