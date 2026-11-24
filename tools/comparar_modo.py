r"""comparar_modo.py (X0b, studies 5) - port contra original en UN comando, sin humano.

  python tools\comparar_modo.py <modo> <escena> [--copia smoke_d] [--log ruta.log] [--turbo N]
  python tools\comparar_modo.py crear-dorado b2 fase1 E:\DBZ3HD_trabajo\R7\emu\logs\b2_traza_ordenes.log
  python tools\comparar_modo.py b2 fase4 --manual --b2-estricto --log <log-ya-grabado>
  python tools\comparar_modo.py b2-quads <original.json> <port.json>  (solo ficheros; plano 640x448)
  python tools\comparar_modo.py b2-sonidos <original.json> <port.json>  (solo ficheros; campos PAL a 50 Hz)

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
import math
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


def secuencia_b2_estricta(oro):
    """Una llamada por (sitio, opcode); nunca funde instrucciones distintas iguales.

    Los sitios incluyen la instancia de tarea cuando la toma la proporciona (@N).
    Sin esa identidad no se pueden distinguir reentradas: no la inventamos.
    """
    g = oro.get("secuencia", [])
    sitios, tiempos = oro.get("sitios", []), oro.get("tiempos", [])
    if not isinstance(g, list) or not g or any(type(k) is not int or not 0 <= k <= 87 for k in g):
        raise ValueError("secuencia vacia o con opcode invalido")
    if not isinstance(sitios, list) or len(sitios) != len(g) or any(not isinstance(s, str) or not s.strip() for s in sitios):
        raise ValueError("faltan sitios completos (uno por llamada)")
    if not isinstance(tiempos, list) or len(tiempos) != len(g):
        raise ValueError("faltan tiempos completos (uno por llamada)")
    if any(type(t) not in (int, float) or not math.isfinite(t) or t < 0 for t in tiempos):
        raise ValueError("tiempos invalidos o no finitos")
    if any(b < a for a, b in zip(tiempos, tiempos[1:])):
        raise ValueError("los tiempos retroceden")
    vistos, salida = set(), []
    for k, s in zip(g, sitios):
        if k != 0 and (s, k) not in vistos:
            vistos.add((s, k))
            salida.append(k)
    if not salida:
        raise ValueError("secuencia sin instrucciones activas (solo opcode 0)")
    return salida


def comparar_ordenes_estrictas(oro, texto_port):
    """B2-01/02 opt-in: bloque, subsecuencia COMPLETA y extras; sin certificar el build."""
    try:
        g = secuencia_b2_estricta(oro)
    except ValueError as e:
        return [("referencia B2 estricta", None, f"PENDIENTE_DATO: {e}")]
    # El port registra cada instruccion una vez en Orden/CorrerHilos, no cada
    # cuadro de su consumidor. Una repetida aqui puede ser otra instruccion.
    port = [int(x) for x in _ORDEN_PORT.findall(texto_port) if int(x) != 0]
    bloque, extras, hechas = False, None, 0
    mejor_inicio = 0
    for inicio in (i for i, k in enumerate(port) if k == g[0]):
        j, n = inicio, 0
        for k in g:
            while j < len(port) and port[j] != k:
                j += 1
            if j == len(port):
                break
            n += 1
            j += 1
        if n > hechas:
            hechas, mejor_inicio = n, inicio
        if n == len(g):
            de_mas = j - inicio - len(g)
            extras = de_mas if extras is None else min(extras, de_mas)
            bloque |= de_mas == 0
    corta = len(set(g)) < 3
    primera = "ninguna"
    if not bloque:
        for i, k in enumerate(g):
            actual = port[mejor_inicio + i] if mejor_inicio + i < len(port) else None
            if k != actual:
                primera = f"original[{i}]={k}, port[{mejor_inicio + i}]={actual if actual is not None else 'FIN'}"
                break
    return [("referencia B2 estricta", True, f"{len(g)}/{len(oro['secuencia'])} instrucciones/llamadas; sitios y tiempos completos"),
            ("bloque del original identico y seguido en el port", None if corta else bloque,
             f"{len(g)} instrucciones; primera diferencia: {primera}" + ("; menos de 3 opcodes: PENDIENTE_DATO" if corta else "")),
            ("ordenes de mas del port dentro de la ventana del original", None if corta or extras is None else extras == 0,
             "sin ventana completa" if extras is None else f"{extras} de mas"),
            ("subsecuencia COMPLETA del original", hechas == len(g), f"{hechas}/{len(g)}; {primera}"),
            ("el port registra alguna orden de guion", bool(port), f"{len(port)} instrucciones (sin fusionar opcodes iguales)")]


def comparar_ordenes(oro, texto_port, estricto=False):
    if estricto:
        return comparar_ordenes_estrictas(oro, texto_port)
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


def comparar_quads_b2(original, port):
    """B2-03 opt-in. Sin alinear ni redimensionar: ambos exportan xy logicas.

    Formato b2-quads-v1: escena, region, campo, plano=[640,448], viewport=[x,y,w,h],
    completo=true, quads=[{ama,anim,cuadro,instancia,xy:[[x,y]*4]}].
    Las cuatro esquinas tienen el mismo orden. 'cuadro' es el del AMA;
    'campo' identifica el instante de la escena, no el tiempo de la captura.
    No evalua SSIM, alfa/UV ni la camara 3D y no certifica la procedencia.
    """
    def numero(v):
        return type(v) in (int, float) and math.isfinite(v)

    def leer(doc):
        if not isinstance(doc, dict) or doc.get('formato') != 'b2-quads-v1':
            raise ValueError('formato no es b2-quads-v1')
        if doc.get('plano') != [640, 448] or doc.get('region') not in ('PAL', 'NTSC'):
            raise ValueError('faltan plano 640x448 o region PAL/NTSC')
        if not isinstance(doc.get('escena'), str) or not doc['escena'] or type(doc.get('campo')) is not int or doc['campo'] < 0:
            raise ValueError('faltan escena/campo de la escena')
        v = doc.get('viewport')
        if not isinstance(v, list) or len(v) != 4 or not all(numero(x) for x in v) or min(v[2:]) <= 0:
            raise ValueError('viewport no declarado o invalido')
        if doc.get('completo') is not True:
            raise ValueError('cobertura de quads incompleta/no declarada')
        if not isinstance(doc.get('quads'), list) or not doc['quads']:
            raise ValueError('sin quads: no acredita imagen')
        filas = {}
        for q in doc['quads']:
            if not isinstance(q, dict) or any(type(q.get(k)) is not int or q[k] < 0 for k in ('ama', 'anim')):
                raise ValueError('falta identidad AMA/anim')
            if not numero(q.get('cuadro')) or q['cuadro'] < 0 or not isinstance(q.get('instancia'), str) or not q['instancia']:
                raise ValueError('falta cuadro/instancia')
            clave = (q['ama'], q['anim'], q['cuadro'], q['instancia'])
            xy = q.get('xy')
            if not isinstance(xy, list) or len(xy) != 4 or any(not isinstance(p, list) or len(p) != 2 or not all(numero(x) for x in p) for p in xy):
                raise ValueError('faltan cuatro xy finitos ordenados')
            if clave in filas:
                raise ValueError(f'identidad duplicada {clave}')
            filas[clave] = xy
        return filas

    try:
        a, b = leer(original), leer(port)
    except ValueError as e:
        return [('quads B2-03', None, f'PENDIENTE_DATO: {e}')]
    if any(original[k] != port[k] for k in ('escena', 'region', 'campo', 'plano')):
        return [('quads B2-03', None, 'PENDIENTE_DATO: escena/region/campo/plano distintos')]
    faltan, extras = sorted(a.keys() - b.keys()), sorted(b.keys() - a.keys())
    iguales, max_error, primera = 0, 0.0, None
    for k in sorted(a.keys() & b.keys()):
        error = max(math.dist(p, q) for p, q in zip(a[k], b[k]))
        def tam(xy, eje):
            return max(p[eje] for p in xy) - min(p[eje] for p in xy)
        error = max(error, abs(tam(a[k], 0) - tam(b[k], 0)), abs(tam(a[k], 1) - tam(b[k], 1)))
        max_error = max(max_error, error)
        iguales += error <= 1.0
        if error > 1.0 and primera is None:
            primera = f'{k}: {error:.6f} px'
    return [('identidad AMA/anim/cuadro/instancia', not faltan and not extras,
             f'{len(a.keys() & b.keys())}/{len(a)}; ausentes={len(faltan)}, extras={len(extras)}; primera={faltan[:1] or extras[:1] or "ninguna"}'),
            ('quads: vertices y ancho/alto <=1 px', iguales == len(a) and not extras,
             f'{iguales}/{len(a)}; max_error={max_error:.6f} px; primera diferencia={primera or ("identidad ausente/extra" if faltan or extras else "ninguna")}')]


def comparar_sonidos_b2(original, port):
    """B2-13 offline opt-in: eventos PAL completos, sin reagrupar ni alinear.

    b2-sonidos-v1: escena, region=PAL, hz=50, completo=true,
    eventos=[{campo:int, canal:int, id:int}]. Campo relativo al mismo hito.
    No convierte cuadros de animacion en campos ni prueba la procedencia.
    """
    def leer(doc):
        if not isinstance(doc, dict) or doc.get('formato') != 'b2-sonidos-v1':
            raise ValueError('formato no es b2-sonidos-v1')
        if doc.get('region') != 'PAL' or type(doc.get('hz')) is not int or doc['hz'] != 50:
            raise ValueError('solo banco PAL 50 Hz; NTSC requiere fuente propia')
        if not isinstance(doc.get('escena'), str) or not doc['escena'] or doc.get('completo') is not True:
            raise ValueError('faltan escena o cobertura completa')
        ev = doc.get('eventos')
        if not isinstance(ev, list) or not ev:
            raise ValueError('traza vacia/no disponible')
        for e in ev:
            if not isinstance(e, dict) or any(type(e.get(k)) is not int or e[k] < 0 for k in ('campo', 'canal', 'id')):
                raise ValueError('campo/canal/id no enteros no negativos')
        if any(b['campo'] < a['campo'] for a, b in zip(ev, ev[1:])):
            raise ValueError('los campos retroceden')
        return ev
    try:
        a, b = leer(original), leer(port)
    except ValueError as e:
        return [('sonidos B2-13', None, f'PENDIENTE_DATO: {e}')]
    if original['escena'] != port['escena']:
        return [('sonidos B2-13', None, 'PENDIENTE_DATO: escenas distintas')]
    identidad = [i for i in range(max(len(a), len(b))) if i >= len(a) or i >= len(b)
                 or (a[i]['canal'], a[i]['id']) != (b[i]['canal'], b[i]['id'])]
    if identidad:
        i = identidad[0]
        detalle = f'evento[{i}]: original={a[i] if i < len(a) else "FIN"}, port={b[i] if i < len(b) else "FIN"}'
        return [('sonidos: misma secuencia canal/id', False,
                 f'{max(len(a), len(b)) - len(identidad)}/{max(len(a), len(b))}; primera diferencia {detalle}'),
                ('sonidos: desfase <=1 campo PAL', None, 'sin secuencia emparejada completa')]
    errores = [abs(x['campo'] - y['campo']) for x, y in zip(a, b)]
    malos = [i for i, e in enumerate(errores) if e > 1]
    return [('sonidos: misma secuencia canal/id', True, f'{len(a)}/{len(a)}; primera diferencia ninguna'),
            ('sonidos: desfase <=1 campo PAL', not malos,
             f'{len(a) - len(malos)}/{len(a)}; max={max(errores)} campos; primera diferencia {malos[:1] or "ninguna"}')]


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
def lanzar_port(modo, escena, copia, turbo, oro_teclas="", oro_seg=40, manual=False, resultado_b2=None):
    import pruebas as P
    spec = ESCENA_PORT.get((modo, escena)) or (f"iw:mini:{escena}" if modo == "iw" else None)
    if not spec:
        raise SystemExit(f"no se como saltar a {modo}:{escena} en el port (ESCENA_PORT)")
    env = ["DBZ3_TEST_OCULTO=1", f"DBZ3_TEST_TURBO={turbo}"]
    # B2-02: la llegada a "fase N leida" espera a la lectura de IW del hilo de prueba (0,1-36 s medidos el 19/11; la fase 1
    # con --manual de R22 paso de 52 s y se corto a los 60 s; con 6 pruebas a la vez el 19/11 la ISO de B1 tardo 85 s). 360 s no cambia nada cuando llega antes.
    par = {"escena": spec, "quieto": "45" if modo == "b2" else "1.5", "espera": "360" if modo == "b2" else "60"}
    if modo == "b2" and oro_teclas:                  # mismas teclas que en el original (dorado["teclas"])
        par["teclas"] = oro_teclas
        par["quieto"] = str(max(10, int(oro_seg)))
    if modo == "b2" and (manual or (escena in ("fase4", "fase9") and oro_teclas)):
        par["manual"] = "1"
    if (modo, escena) in (("b2", "fase4"), ("b2", "fase9")) and resultado_b2:
        # Aislar lo que se mide (reglas del tablero), sin depender de la IA
        # del motor de lucha HD. La ruta libre sigue disponible con --manual.
        env.append(f"DBZ3_TEST_RINGOUT={1 if resultado_b2['codigo'] in (1, 4) else 2}")
        par.update(b2_juez="1", quieto="10")
    if modo == "iw":
        par["fin"] = "1"
    r = P.correr(copia, "escena", env, None, par)
    if not r.get("ok") or not r.get("run"):
        raise RuntimeError(r.get("msg") or "el port no dejó una ejecución completa")
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


# B2-02: modo valido de cada fase (medido el 19/11 con el exe de R22, tools/dorados/resultados/b2_fase*_20261119-*.json):
# 4 y 9 con el resultado del original fijado (sin --manual); el resto con --manual (sin el, el piloto no sigue la ruta del
# original y faltan las ordenes del combate: 62/58/75...). El veredicto cuenta solo las filas de ordenes; las imagenes van
# aparte (cotejo visual, B2-07) y salen como [H].
MODO_B2 = {1: "--manual", 2: "--manual", 3: "--manual", 4: "", 5: "--manual", 6: "--manual", 7: "--manual",
           8: "--manual", 9: ""}


def juez_b2(a):
    """b2-juez [--copias sol,smoke_d] [--fases 1,2,...]: las 9 fases, cada una en su modo, un veredicto por fase."""
    import subprocess
    from concurrent.futures import ThreadPoolExecutor
    copias = (a[a.index("--copias") + 1] if "--copias" in a else "sol,smoke_d").split(",")
    fases = [int(x) for x in (a[a.index("--fases") + 1] if "--fases" in a else "1,2,3,4,5,6,7,8,9").split(",")]
    libres = list(copias)

    def una(n):
        copia = libres.pop(0)
        try:
            t0 = time.time()
            cmd = [sys.executable, str(AQUI / "comparar_modo.py"), "b2", f"fase{n}", "--copia", copia]
            cmd += [MODO_B2[n]] if MODO_B2[n] else []
            p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
            res = [r for r in RESULTADOS.glob(f"b2_fase{n}_*.json") if r.stat().st_mtime >= t0 - 1]
            if not res:
                return n, "HERRAMIENTA", f"sin resultado (codigo {p.returncode}): {(p.stdout or p.stderr).strip()[-160:]}"
            filas = json.loads(max(res, key=lambda r: r.stat().st_mtime).read_text(encoding="utf-8"))["filas"]
            orden = [f for f in filas if not f["nombre"].startswith("imagen")]
            malos = [f for f in orden if f["ok"] is False]
            herr = [f for f in malos if f["clase"] == "herramienta"]
            ver = "HERRAMIENTA" if herr and len(herr) == len(malos) else "FAIL" if malos else "PASS"
            det = "; ".join(f"{f['nombre']}: {f['detalle'][:90]}" for f in malos) or \
                f"{sum(f['ok'] is True for f in orden)} filas de ordenes OK, imagenes aparte"
            return n, ver, f"[{MODO_B2[n] or 'resultado fijado'}, {copia}] {det}"
        finally:
            libres.append(copia)

    with ThreadPoolExecutor(len(copias)) as ex:
        out = sorted(ex.map(una, fases))
    for n, ver, det in out:
        print(f"fase {n}: {ver:11} {det}")
    malos = [n for n, v, _ in out if v != "PASS"]
    print(f"JUEZ B2: {len(out) - len(malos)}/{len(out)} fases PASS" + (f"; revisar {malos}" if malos else ""))
    return 1 if malos else 0


def main(a):
    if a[:1] == ["b2-juez"]:
        return juez_b2(a[1:])
    if a[:1] in (['b2-quads'], ['b2-sonidos']):
        if len(a) != 3:
            print(f'Uso offline: comparar_modo.py {a[0]} original.json port.json')
            return 2
        try:
            original, port = [json.loads(Path(p).read_text(encoding='utf-8-sig')) for p in a[1:]]
        except (OSError, ValueError) as e:
            print(f'PENDIENTE_DATO: {e}')
            return 2
        filas = (comparar_quads_b2 if a[0] == 'b2-quads' else comparar_sonidos_b2)(original, port)
        print(json.dumps([dict(nombre=n, ok=ok, detalle=d) for n, ok, d in filas], ensure_ascii=False, indent=2))
        return 1 if any(f[1] is False for f in filas) else 2 if any(f[1] is None for f in filas) else 0
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
    resultado_b2 = None if "--manual" in a or "--log" in a else oro.get("resultado_tablero")
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
        try:
            texto, run = lanzar_port(modo, escena, copia, turbo, oro.get("teclas", ""), oro.get("seg", 40),
                                    manual="--manual" in a, resultado_b2=resultado_b2)
        except RuntimeError as e:
            print(f"HERRAMIENTA: {modo}:{escena}: prueba incompleta: {e}. No se compara.")
            return 2
    if (modo, escena) == ("b2", "nave"):
        pass
    elif modo == "b2":
        filas = comparar_ordenes(oro, texto, estricto="--b2-estricto" in a)
        if resultado_b2:
            observado = re.search(r"tablero b2: resultado del suceso (\d+): codigo (\d+)", texto)
            esperado = (resultado_b2["suceso"], resultado_b2["codigo"])
            actual = tuple(map(int, observado.groups())) if observado else None
            filas.insert(0, ("resultado de entrada al tablero", actual == esperado,
                             f"(suceso, código): original {esperado}, port {actual}"))
        if '--b2-estricto' in a and escena in ('fase4', 'fase6', 'fase9'):
            if oro.get('poscombate') is not True or type(oro.get('seed')) is not int or oro.get('victoria') is not True:
                filas.append(('rama poscombate reproducible', None, 'PENDIENTE_DATO: declarar poscombate=true, seed y victoria=true desde la toma real'))
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
    return 1 if malos else 2 if pend and "--b2-estricto" in a else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
