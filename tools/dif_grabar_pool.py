r"""dif_grabar_pool.py (N4, noche 6->7/11) - graba el dorado de camara de una mision IW en una instancia FIJA del pool de PCSX2.

  python tools\dif_grabar_pool.py <clave 0x7NN> [--pool 3] [--seg 40] [--iso RUTA] [--estado RUTA.p2s] [--sal DIR]

lector_camara.grabar() usa sonda.Pine() en el puerto 28011; aqui se apunta antes la sonda a la instancia del pool
(pcsx2_pool.registrar_en_sonda). Flujo: preparar -> lanzar (adopta el pid) -> esperar PINE -> grabar TSV -> dorado JSON
-> cerrar SOLO el pid de este pool -> liberar. No usa `adquirir`. Todo bajo un tiempo limite externo (--limite, 600 s).
Escribe el TSV en --sal (por defecto E:\DBZ3HD_trabajo\dorados_noche) y el dorado en tools\dorados\iw_<clave>.json.
"""
import argparse
import json
import re
import shutil
import sys
import time
from pathlib import Path

AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI))
ISO_EU = r"E:\DBZ3HD_trabajo\iw_eu\Dragon Ball Z - Infinite World (Europe).iso"
ESTADOS = Path(r"E:\DBZ3HD_trabajo\pool_estados\iw_eu")
ISO_USA = r"E:\DBZ3HD_trabajo\iw_usa\iw_usa.bin"
ESTADOS_USA = Path(r"E:\DBZ3HD_trabajo\pool_estados\iw_usa")


def estado_por_defecto(clave, usa=False):
    slot = 30 + (int(clave, 16) - 0x7D0)
    d = ESTADOS_USA if usa else ESTADOS
    cand = sorted(d.glob(f"*{slot}*.p2s")) + sorted(d.glob(f"*.p2s"))
    return next((c for c in cand if re.search(rf"(^|\D){slot}(\D|$)", c.stem)), None)


def _validos(x):
    return 0x100000 <= x < 0x2000000


CONT_IW = 0x4A4D40          # contador de campos (EU), validado 8/11: +1 por FrameAdvance, 49,8/s libre
CAMPOS_POR_S = 50
CAM_CAMPO = 0x49CBBC        # camara de campo (EU): ojo +0x80, objetivo +0x90, clip +0x114 (la usan los minijuegos de campo/persecucion)


def leer_fila(LC, p, cam_campo, cont, ctx):
    """Una lectura atomica (un lote PINE) de camara y luchadores -> (fila, contador); fila None si aun no hay escena.
    ctx guarda el nodo raiz del jugador (ctx['siempre']=True lo refresca en cada lectura: modo exacto)."""
    spec = LC.JUEGOS["iw"]
    ca, cam, cam2, c = p.lote([(spec["ptr_lucha"], 4), (spec["ptr_cam"], 4), (cam_campo, 4), (cont, 4)])
    # P2 7/11: *(0x49CD6C) (camara de los minijuegos de botones) vale 0 en los de campo/persecucion (7D1, 7DD...):
    # ahi la camara es *(0x49CBBC) (ojo +0x80, objetivo +0x90, clip +0x114 que se guarda como #AMC).
    campo = not cam and _validos(cam2)
    if not ca or not (cam or campo):
        return None, c
    cam0 = cam
    if campo:
        cam = cam2
    mod_idx = (0, 0)
    k = ctx["k"] = ctx.get("k", 0) + 1
    if ctx.get("siempre") or k % 25 == 1:      # la cadena de punteros del nodo raiz cambia poco: se refresca cada 25 vueltas
        nodo = 0
        mod = p.lote([(ca + spec["off_j"] + 0xB78, 4)])[0]
        if _validos(mod):
            idx = p.lote([(mod + 0x40C, 4)])[0]
            if idx < 47:
                nodo = p.lote([(mod + 8 + 4 * idx, 4)])[0]
            mod_idx = (mod, idx)
        ctx["nodo"], ctx["mi"] = nodo, mod_idx
    mod_idx = ctx.get("mi", (0, 0))
    nodo = ctx.get("nodo", 0)
    ctx["b"] = None
    if ctx.get("siempre") and _validos(nodo):          # lo que hace falta para repetir esta lectura con UNA llamada PINE
        ctx["b"] = (ca, cam0, cam2, mod_idx[0], mod_idx[1], nodo, campo)
    pares = [(cam + (0 if campo else spec["fov"]), 4)] + [(cam + spec["eye"] + 4 * i, 4) for i in range(3)] \
        + [(cam + spec["tgt"] + 4 * i, 4) for i in range(3)] \
        + [(ca + spec["off_j"] + spec["pos"] + 4 * i, 4) for i in range(3)] \
        + [(ca + spec["off_r"] + spec["pos"] + 4 * i, 4) for i in range(3)] + [(cam + (0x114 if campo else spec["amc"]), 4)]
    pares += [(nodo + 0x50 + 4 * i, 4) for i in range(3)] if _validos(nodo) else []
    v = p.lote(pares)
    if campo:
        v[0] = 0                       # esa camara no tiene fov en +0
    jr = [LC._f(x) for x in v[14:17]] if len(v) >= 17 else [0.0, 0.0, 0.0]
    return tuple([LC._f(x) for x in v[:13]] + [v[13]] + jr), c


def datos_fila(spec, cam, campo, ca, nodo):
    pares = [(cam + (0 if campo else spec["fov"]), 4)] + [(cam + spec["eye"] + 4 * i, 4) for i in range(3)] \
        + [(cam + spec["tgt"] + 4 * i, 4) for i in range(3)] \
        + [(ca + spec["off_j"] + spec["pos"] + 4 * i, 4) for i in range(3)] \
        + [(ca + spec["off_r"] + spec["pos"] + 4 * i, 4) for i in range(3)] + [(cam + (0x114 if campo else spec["amc"]), 4)]
    return pares + [(nodo + 0x50 + 4 * i, 4) for i in range(3)]


def fila_rapida(LC, c, n_ant, cam_campo, cont, ctx, tomar=True):
    """UN paso de juego + UNA llamada PINE (estado + contador + comprobaciones + datos). Las comprobaciones (punteros y cadena del
    nodo raiz) se leen en el mismo lote y se comparan con los de la lectura lenta anterior: si algo cambio, se repite la lectura
    lenta en el mismo campo (el emulador esta parado), asi el resultado es identico al de leer_fila en cada campo.
    tomar=False: solo avanza (para --cada K). Devuelve (fila o None, contador)."""
    spec = LC.JUEGOS["iw"]
    b = ctx.get("b")
    if not b:
        n, _ = c.paso_leer(n_ant, [])
        return leer_fila(LC, c.p, cam_campo, cont, ctx)[0] if tomar else None, n
    ca, cam0, cam2, mod, idx, nodo, campo = b
    chk = [(spec["ptr_lucha"], 4), (spec["ptr_cam"], 4), (cam_campo, 4), (ca + spec["off_j"] + 0xB78, 4), (mod + 0x40C, 4),
           (mod + 8 + 4 * idx, 4)]
    cam = cam2 if campo else cam0
    pares = chk + (datos_fila(spec, cam, campo, ca, nodo) if tomar else [])
    n, v = c.paso_leer(n_ant, pares)
    if v[:6] != [ca, cam0, cam2, mod, idx, nodo]:          # algo se movio: lectura lenta de este mismo campo
        return (leer_fila(LC, c.p, cam_campo, cont, ctx)[0] if tomar else None), n
    if not tomar:
        return None, n
    d = v[6:]
    if campo:
        d[0] = 0
    return tuple([LC._f(x) for x in d[:13]] + [d[13]] + [LC._f(x) for x in d[14:17]]), n


def escribir_tsv(LC, salida, filas, cont0):
    with open(salida, "w", encoding="utf-8") as f:
        f.write("\t".join(LC.COLUMNAS + ["jrx", "jry", "jrz", "cont"]) + "\n")
        for cc, fila in filas:
            f.write("%.3f\t" % ((cc - cont0) / CAMPOS_POR_S) + "\t".join(str(x) for x in fila) + "\t%d\n" % cc)


def grabar_ext(LC, p, campos, salida, cam_campo=CAM_CAMPO, cont=CONT_IW, ses=None, desde=None, est=None):
    """Como LC.grabar('iw') pero (a) con 3 columnas mas: jrx,jry,jrz = posicion LOCAL del nodo raiz del modelo del jugador
    (*(*(mgr+0x800+0xB78)+8+4*idx)+0x50), medido 7/11 (P2): en los minijuegos de botones/disparo/vuelo mgr+0x800+0x40 vale
    (0,0,0) a proposito y la posicion animada solo existe en ese nodo; (b) FECHADA POR CAMPOS DEL JUEGO (mejora 7, 8/11): el
    contador 0x4A4D40 (+1 por campo PAL, validado) se lee en el mismo sondeo; t = (contador - contador_inicial) / 50 y la
    ultima columna cont es el contador absoluto. Dura `campos` campos de juego, no segundos de reloj. Modo LIBRE (rapido): de
    cada campo se guarda el ultimo valor visto; la escena es la misma pero el instante de lectura dentro del campo depende del
    PC, asi que los valores intermedios pueden variar unos decimales entre pasadas: para series BIT A BIT iguales, grabar_exacto.
    `est` (dict) guarda las filas entre reintentos: si la partida se retoma desde un punto de control (`desde` = su contador),
    se descartan las filas posteriores a ese contador y se sigue. Devuelve el numero de filas."""
    est = est if est is not None else {}
    filas = est.setdefault("filas", [])                 # [(cont, fila)] solo cuando cambia
    if desde is None:
        filas.clear()
        est["cont0"] = None
    else:
        filas[:] = [r for r in filas if r[0] <= desde]
    cont0 = est.get("cont0")
    prev = filas[-1][1] if filas else None
    cur_c, cur_f, ctx = None, None, {}
    while True:
        fila, c = leer_fila(LC, p, cam_campo, cont, ctx)
        if cont0 is None:
            cont0 = est["cont0"] = c
        if ses is not None:
            ses.control_cada(p, 20)
        if cur_c is not None and c != cur_c:           # cambio de campo: se confirma el ultimo valor del campo anterior
            if cur_f is not None and cur_f != prev:
                filas.append((cur_c, cur_f))
                prev = cur_f
            cur_f = None
        cur_c = c
        if c - cont0 >= campos:
            break
        if fila is None:
            time.sleep(0.005)
            continue
        cur_f = fila
    escribir_tsv(LC, salida, filas, cont0)
    return len(filas)


def grabar_exacto(LC, p, campos, salida, cam_campo=CAM_CAMPO, cont=CONT_IW, ses=None, desde=None, est=None, antes=0, estado_cont=None, cada=1):
    """Igual que grabar_ext pero PARANDO el emulador en cada campo (FrameAdvance): cada lectura cae en un campo exacto, asi dos
    pasadas dan series identicas bit a bit aunque el PC vaya cargado. Mas lento (~0,1 s por campo). `antes` = campos que se
    dejan correr libres desde el savestate antes de empezar a grabar (el emulador debe arrancar PARADO: Sesion(pausado=True));
    `campos` = campos grabados DESPUES de `antes`."""
    import pcsx2_sonda as S
    import pcsx2_cuadros as C
    est = est if est is not None else {}
    filas = est.setdefault("filas", [])
    c = C.Cuadros(S, cont, p=p)
    c.exacto = False                  # el contador de IW cuenta cuadros de juego (a veces 2 campos): se avanza hasta que cambie
    c.pausa()
    if desde is None:
        filas.clear()
        est["cont0"] = estado_cont if estado_cont is not None else c.numero()
        if antes:
            c.avanzar_hasta(est["cont0"] + antes)
    else:
        filas[:] = [r for r in filas if r[0] <= desde]
    cont0, ctx = est["cont0"], {"siempre": True}
    prev = filas[-1][1] if filas else None
    from pcsx2_turnos import comprobar_tope
    fila, n = leer_fila(LC, p, cam_campo, cont, ctx)           # lectura lenta del campo actual (fija los punteros)
    paso = 0
    while True:
        if fila is not None and fila != prev:
            filas.append((n, fila))
            prev = fila
        if n - cont0 >= antes + campos:
            break
        comprobar_tope()
        if ses is not None:
            ses.control_cada(p, 90)
        paso += 1
        fila, n = fila_rapida(LC, c, n, cam_campo, cont, ctx, tomar=paso % cada == 0)
    escribir_tsv(LC, salida, filas, cont0)
    return len(filas)


def leer_serie(tsv):
    """TSV de grabar_ext -> {contador: fila (sin t ni cont)}; solo los cambios."""
    with open(tsv, encoding="utf-8") as f:
        cab = f.readline().rstrip("\n").split("\t")
        ic = cab.index("cont")
        out = {}
        for l in f:
            c = l.rstrip("\n").split("\t")
            if len(c) == len(cab):
                out[int(c[ic])] = tuple(c[1:ic])
    return out


def comparar_series(a, b, tol=1):
    """Compara dos TSV de grabar_ext POR CAMPO DE JUEGO (valor mantenido hasta el siguiente cambio). Cuenta los campos
    comunes distintos; `tol` = campos de margen al alinear un cambio (el valor se ve en el campo n o n+1).
    Devuelve (campos comparados, campos distintos, primer contador distinto o None)."""
    import bisect
    A, B = leer_serie(a), leer_serie(b)
    if not A or not B:
        return 0, 0, None
    lo, hi = max(min(A), min(B)), min(max(A), max(B))
    ka, kb = sorted(A), sorted(B)

    def val(S, ks, c):
        i = bisect.bisect_right(ks, c) - 1
        return S[ks[i]] if i >= 0 else None

    n = mal = 0
    primero = None
    for c in range(lo, hi + 1):
        n += 1
        va, vb = val(A, ka, c), val(B, kb, c)
        if va != vb and not any(va == val(B, kb, c + d) or val(A, ka, c + d) == vb for d in range(-tol, tol + 1)):
            mal += 1
            primero = primero or c
    return n, mal, primero


def dorado_ext(LC, tsv, salida):
    """LC.main('dorado') + 'jr' (nodo raiz) en cada muestra."""
    import json
    from datetime import datetime
    s = LC.serie_tsv("iw", tsv)
    jr = []
    with open(tsv, encoding="utf-8") as f:
        cab = f.readline().rstrip("\n").split("\t")
        for l in f:
            c = l.rstrip("\n").split("\t")
            if len(c) == len(cab):
                d = dict(zip(cab, c))
                jr.append([float(d.get(k, 0)) for k in ("jrx", "jry", "jrz")])
    for r, v in zip(s, jr):
        r["jr"] = v
    paso = max(1, len(s) // 200)
    oro = {"juego": "iw", "fuente": Path(tsv).name, "creado": datetime.now().isoformat(timespec="seconds"),
           "resumen": LC.resumen(s), "muestras": [s[i] for i in range(0, len(s), paso)]}
    Path(salida).write_text(json.dumps(oro, indent=1), encoding="utf-8")


def pid_del_pool(pool):
    import pcsx2_pool as P
    m = re.search(r"pid=(\d+)", P.estado_instancia(pool))
    return int(m.group(1)) if m else None


def main(a):
    ap = argparse.ArgumentParser()
    ap.add_argument("clave", nargs="?")   # opcional solo para --autoprueba
    ap.add_argument("--pool", type=int, default=3)
    ap.add_argument("--seg", type=float, default=40)
    ap.add_argument("--iso", default=ISO_EU)
    ap.add_argument("--estado")
    ap.add_argument("--sal", default=r"E:\DBZ3HD_trabajo\dorados_noche")
    ap.add_argument("--limite", type=int, default=600)
    ap.add_argument("--usa", action="store_true", help="IW USA: ISO/estados USA, direcciones por dif_mapa_iw_usa.usa_datos; escribe iw_usa_<clave>.json")
    ap.add_argument("--autoprueba", action="store_true")
    ap.add_argument("--no-dorado", action="store_true", dest="no_dorado", help="no toca tools/dorados (para repetir tomas)")
    ap.add_argument("--tope", type=float, default=30, help="minutos maximos con el turno de grabacion (aviso y corte)")
    ap.add_argument("--cada", type=int, default=1, help="con --exacto: leer un campo de cada K (los demas solo se avanzan)")
    ap.add_argument("--velocidad", type=float, choices=(1, 2, 3), default=1, help="limite de fotogramas del emulador x1/x2/x3 (solo modo libre)")
    ap.add_argument("--exacto", action="store_true", help="parar el emulador en cada campo: series identicas bit a bit (lento)")
    ap.add_argument("--antes", type=float, default=0, help="con --exacto: segundos de juego que corren libres antes de grabar")
    ap.add_argument("--comparar", nargs=2, metavar=("A.tsv", "B.tsv"), help="compara dos tomas por campo de juego")
    o = ap.parse_args(a)
    if o.comparar:
        n, mal, primero = comparar_series(*o.comparar)
        print("campos comparados %d, distintos %d%s -> %s" % (n, mal, "" if not mal else " (primero en %d)" % primero,
                                                              "IDENTICAS" if n and not mal else "DISTINTAS"))
        return 0 if n and not mal else 1
    if o.autoprueba:
        assert str(estado_por_defecto("0x7D0") or "x") is not None
        import lector_camara as LC
        assert callable(grabar_ext) and callable(dorado_ext) and LC.COLUMNAS[-1] == "amc"
        import tempfile
        with tempfile.TemporaryDirectory() as d:        # comparar_series: mismo valor mantenido = identicas; un cambio distinto = detectado
            cab = "\t".join(LC.COLUMNAS + ["jrx", "jry", "jrz", "cont"]) + "\n"
            fila = lambda c, x: "0\t" + "\t".join([str(x)] * 17) + "\t%d\n" % c      # noqa: E731
            for nom, filas in (("a", [(100, 1), (110, 2)]), ("b", [(101, 1), (111, 2), (111, 2)]), ("c", [(100, 1), (110, 3)])):
                Path(d, nom).write_text(cab + "".join(fila(c, x) for c, x in filas), encoding="utf-8")
            assert comparar_series(Path(d, "a"), Path(d, "b"))[1] == 0       # un campo de desfase se tolera
            assert comparar_series(Path(d, "a"), Path(d, "c"))[1] > 0
        print("dif_grabar_pool autoprueba OK")
        return 0
    if not o.clave:
        ap.error("falta la clave del minijuego (p. ej. 0x7D5)")
    est = Path(o.estado) if o.estado else estado_por_defecto(o.clave, o.usa)
    if o.usa and o.iso == ISO_EU:
        o.iso = ISO_USA
    if not est or not est.exists():
        print("sin savestate para", o.clave)
        return 2
    import pcsx2_pool as P
    import lector_camara as LC
    from pcsx2_turnos import Grabacion
    cont = CONT_IW
    cam_campo = CAM_CAMPO
    if o.usa:                              # direcciones USA = EU por tramos (dif_mapa_iw_usa)
        from dif_mapa_iw_usa import usa_datos
        cont = usa_datos(cont)
        cam_campo = usa_datos(cam_campo)
        for k in ("ptr_lucha", "ptr_cam"):
            LC.JUEGOS["iw"][k] = usa_datos(LC.JUEGOS["iw"][k])
    campos = int(o.seg * CAMPOS_POR_S)
    Path(o.sal).mkdir(parents=True, exist_ok=True)
    suf = "iw_usa_" if o.usa else "iw_"
    tsv = Path(o.sal) / f"{suf}{o.clave}_pool{o.pool}.tsv"
    if tsv.exists():                      # nunca se pisa una toma: la anterior se aparta
        dst = Path(r"D:\_PARA_BORRAR_CLAUDE") / "tomas_iw"
        dst.mkdir(parents=True, exist_ok=True)
        shutil.move(str(tsv), str(dst / f"{tsv.stem}_{int(time.time())}.tsv"))
    rc = 1
    t_ini = time.time()
    try:
        with Grabacion(f"iw {o.clave} pool{o.pool}", tope_s=o.tope * 60):          # una sola grabacion del original a la vez en el PC
            with P.Sesion(o.pool, o.iso, est, cont, limite=o.limite, pausado=o.exacto, velocidad=None if o.exacto else o.velocidad) as ses:
                estado = {}
                if o.exacto:       # parado en el cuadro del savestate; deja correr `antes` campos y graba `campos` en pasos exactos
                    import pcsx2_sonda as S
                    c0 = S.contador_en_estado(str(est), cont)
                    n = ses.ejecutar(lambda p, desde: grabar_exacto(LC, p, campos, str(tsv), cam_campo, cont, ses, desde, estado,
                                                                    int(o.antes * CAMPOS_POR_S), c0, o.cada))
                else:
                    n = ses.ejecutar(lambda p, desde: grabar_ext(LC, p, campos, str(tsv), cam_campo, cont, ses, desde, estado))
        tmp = Path(o.sal) / f"{suf}{o.clave}_pool{o.pool}.json"
        dorado_ext(LC, str(tsv), str(tmp))
        vivas = json.loads(tmp.read_text(encoding="utf-8"))["resumen"].get("n_vivas", 0)
        if vivas > 0 and not o.no_dorado:      # un dorado vacio NO sustituye al que haya
            (AQUI / "dorados" / f"{suf}{o.clave}.json").write_text(tmp.read_text(encoding="utf-8"), encoding="utf-8")
        print("filas", n, "vivas", vivas, "campos", campos, "velocidad x%g" % (1 if o.exacto else o.velocidad),
              "tiempo %.0f s (con espera de turno)" % (time.time() - t_ini), "->", tsv)
        rc = 0
    except TimeoutError as e:
        print("grabacion cortada por el tope de tiempo:", e)
        rc = 4
    except RuntimeError as e:
        print("grabacion fallida:", e)
        rc = 3
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
