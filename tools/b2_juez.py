r"""b2_juez.py (J0, R20) - juez diferencial del tablero B2: diferencia dos volcados b2-state-v1.

  python tools\b2_juez.py <a.jsonl> <b.jsonl> [--vblank 1] [--ordenes 2] [--salida r.json]
  python tools\b2_juez.py --autoprueba

Formato de cada linea (una por hito): studies 5/ENTREGAS/R19/PLAN/JUEZ_DIFERENCIAL.md §5; lo escribe el juego con
DBZ3_TEST_B2_STATE=<ruta> (o con DBZ3_TEST_B2_STATE=1 y se lee del log: aqui vale cualquier linea que lleve el JSON).

Alinea por (fase, entrada_id, hito) y, dentro de cada hito, por orden de aparicion. No inventa campos: si un volcado no
trae un dato, la fila es PENDIENTE_DATO (nunca un PASS por ausencia). El vocabulario de filas es el de
tools\comparar_modo.py: OK / FAIL / H / PENDIENTE_DATO. Los tiempos (vblank) fuera de tolerancia son [H] de ritmo,
no FAIL de logica.

Codigo de salida: 0 todo OK, 1 alguna diferencia, 2 sin datos suficientes o uso.
"""
import hashlib
import json
import sys
from pathlib import Path

# Campos de cada pieza que el juez compara (JUEZ_DIFERENCIAL.md §7.3); `aturdido` va aparte (debe ser 0 en los dos).
CAMPOS_PIEZA = ("c", "bando", "barras", "barras_max", "casilla", "slot", "fuera", "en_juego", "estado0")
CAMPOS_OBJETO = ("casilla", "tipo", "visible", "enterrado", "hecho")
HITOS = ("carga", "inicio_turno", "antes_orden", "despues_orden", "contacto", "resultado", "fin_guion", "cierre",
         "desenlace")


def cargar(ruta):
    """Lineas JSON de un volcado; acepta el JSONL puro y las lineas de log que lo lleven dentro."""
    out = []
    for n, linea in enumerate(Path(ruta).read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        i, f = linea.find("{"), linea.rfind("}")
        if i < 0 or f <= i:
            continue
        try:
            d = json.loads(linea[i:f + 1])
        except ValueError as e:
            raise ValueError(f"{ruta}:{n}: linea no es JSON ({e})") from None
        if not isinstance(d, dict) or d.get("formato") != "b2-state-v1":
            continue
        out.append(d)
    if not out:
        raise ValueError(f"{ruta}: sin lineas b2-state-v1")
    return out


def validar_volcado(doc, nombre="volcado"):
    """Problemas de forma que impiden juzgar ([] = vale)."""
    mal = []
    for k in ("fase", "evento", "hito"):
        if k not in doc:
            mal.append(f"{nombre}: falta {k}")
    if doc.get("hito") not in HITOS and "hito" in doc:
        mal.append(f"{nombre}: hito desconocido {doc.get('hito')!r}")
    if not isinstance(doc.get("piezas"), list):
        mal.append(f"{nombre}: falta piezas[]")
    return mal


def _clave(d):
    return (d.get("fase"), d.get("entrada_id"), d.get("hito"))


def _agrupar(docs):
    g = {}
    for d in docs:
        g.setdefault(_clave(d), []).append(d)
    return g


def _piezas(d):
    return {p.get("n"): p for p in d.get("piezas", []) if isinstance(p, dict) and "n" in p}


def comparar_piezas(a, b):
    pa, pb = _piezas(a), _piezas(b)
    faltan = sorted(set(pa) - set(pb)) + sorted(set(pb) - set(pa))
    if faltan:
        return False, f"piezas ausentes en un lado: {faltan[:6]}"
    for n in sorted(pa):
        x, y = pa[n], pb[n]
        sin = [c for c in CAMPOS_PIEZA if c not in x or c not in y]
        if sin:
            return None, f"pieza {n}: PENDIENTE_DATO (falta {sin})"
        dif = [c for c in CAMPOS_PIEZA if x[c] != y[c]]
        if dif:
            c = dif[0]
            return False, f"pieza {n} (c={x.get('c', '?')}): {c} {x[c]} != {y[c]}"
        if x.get("aturdido", 0) != 0 or y.get("aturdido", 0) != 0:
            return False, f"pieza {n}: aturdido {x.get('aturdido')} / {y.get('aturdido')} (debe ser 0 en los dos)"
    return True, f"{len(pa)} piezas iguales"


def comparar_objetos(a, b):
    oa, ob = a.get("objetos"), b.get("objetos")
    if not isinstance(oa, list) or not isinstance(ob, list):
        return None, "PENDIENTE_DATO: falta objetos[]"
    va = [tuple(o.get(c) for c in CAMPOS_OBJETO) for o in oa]
    vb = [tuple(o.get(c) for c in CAMPOS_OBJETO) for o in ob]
    if any(None in t for t in va) or any(None in t for t in vb):
        return None, "PENDIENTE_DATO: objeto sin alguno de " + str(CAMPOS_OBJETO)
    if sorted(va) != sorted(vb):
        for x in sorted(set(va) - set(vb))[:3]:
            return False, f"objeto solo en A: {x}"
        for y in sorted(set(vb) - set(va))[:3]:
            return False, f"objeto solo en B: {y}"
        return False, "misma lista de objetos, distinto orden/valor"
    return True, f"{len(va)} objetos iguales"


def comparar_bolas(a, b):
    ba, bb = a.get("bolas"), b.get("bolas")
    if not isinstance(ba, dict) or not isinstance(bb, dict) or "mascara" not in ba or "mascara" not in bb:
        return None, "PENDIENTE_DATO: falta bolas.mascara"
    if ba.get("radar") != bb.get("radar"):
        return False, f"radar {ba.get('radar')} != {bb.get('radar')}"
    if ba["mascara"] != bb["mascara"]:
        if a.get("semilla") is None and b.get("semilla") is None:
            return None, f"mascara {ba['mascara']:#x} != {bb['mascara']:#x} sin semilla comun [H]"
        return False, f"mascara {ba['mascara']:#x} != {bb['mascara']:#x}"
    return True, f"mascara {ba['mascara']:#x} igual"


def comparar_script(a, b):
    sa, sb = a.get("script"), b.get("script")
    if not isinstance(sa, dict) or not isinstance(sb, dict):
        return None, "PENDIENTE_DATO: falta script"
    filas = []
    for campo in ("codigo", "guion_siguiente"):
        x, y = sa.get(campo), sb.get(campo)
        if x is None or y is None:
            filas.append((f"script.{campo}", None, f"PENDIENTE_DATO: A={x}, B={y}"))
        else:
            filas.append((f"script.{campo}", x == y, f"A={x}, B={y}"))
    return filas


def comparar_turno(a, b):
    dif = [k for k in ("turno", "fase_actual", "fase_rival") if a.get(k) != b.get(k)]
    if dif:
        k = dif[0]
        return False, f"{k}: A={a.get(k)} B={b.get(k)}"
    return True, f"turno {a.get('turno')}, fase {a.get('fase_actual')}, rival {a.get('fase_rival')}"


def bloque_ordenes(ops_a, ops_b):
    """Bloque identico y seguido de A dentro de B (regla de comparar_modo.py), 0 ordenes de mas.

    Menos de 3 opcodes distintos no prueban nada (H). El port puede repetir una instruccion en cada cuadro, asi que se
    colapsan las repeticiones seguidas igual que hace comparar_modo.py.
    """
    def limpia(v):
        return [k for i, k in enumerate(v) if i == 0 or v[i - 1] != k]
    a, b = limpia([k for k in ops_a if k is not None]), limpia([k for k in ops_b if k is not None])
    if len(set(a)) < 3:
        return None, f"A solo tiene {len(set(a))} opcodes distintos: no cuenta como igual [H]"
    for s in (i for i, k in enumerate(b) if k == a[0]):
        j = s
        for k in a:
            while j < len(b) and b[j] != k:
                j += 1
            if j == len(b):
                break
            j += 1
        else:
            de_mas = j - s - len(a)
            return True, f"{len(a)} ordenes seguidas; {de_mas} de mas"
    primera = "ninguna"
    for i, k in enumerate(a):
        if i >= len(b) or b[i] != k:
            primera = f"A[{i}]={k}, B[{i}]={b[i] if i < len(b) else 'FIN'}"
            break
    return False, f"el bloque de {len(a)} ordenes de A no aparece seguido en B; primera diferencia: {primera}"


def juzgar(a_docs, b_docs, tol_vblank=1, tol_ordenes=2):
    """Filas (nombre, True/False/None, detalle) de dos volcados completos."""
    filas = []
    ga, gb = _agrupar(a_docs), _agrupar(b_docs)
    for clave in sorted(ga, key=lambda c: (str(c[0]), str(c[1]), HITOS.index(c[2]) if c[2] in HITOS else 99)):
        fase, entrada, hito = clave
        etiqueta = f"f{hito or '?'}:fase {fase}/entrada {entrada}"
        la, lb = ga[clave], gb.get(clave, [])
        if not lb:
            filas.append((etiqueta, None, "PENDIENTE_DATO: B no tiene ese hito"))
            continue
        sin_codigo = 0
        for i in range(min(len(la), len(lb))):
            a, b = la[i], lb[i]
            pre = etiqueta if len(la) == 1 else f"{etiqueta}[{i}]"
            mal = validar_volcado(a, "A") + validar_volcado(b, "B")
            if mal:
                filas.append((pre, None, "PENDIENTE_DATO: " + "; ".join(mal)))
                continue
            filas.append((pre + " turno", *comparar_turno(a, b)))
            filas.append((pre + " piezas", *comparar_piezas(a, b)))
            filas.append((pre + " objetos", *comparar_objetos(a, b)))
            filas.append((pre + " bolas", *comparar_bolas(a, b)))
            for n, ok, d in comparar_script(a, b):
                if ok is None and d == "PENDIENTE_DATO: A=None, B=None":
                    sin_codigo += 1      # se resume en una fila por hito (evita cientos de PENDIENTE repetidos)
                else:
                    filas.append((pre + " " + n, ok, d))
            # relojes logicos: ±tol_ordenes ordenes y ±tol_vblank campos. Fuera de rango es [H] de ritmo, no FAIL.
            da = a.get("orden_idx")
            db = b.get("orden_idx")
            va = a.get("vblank")
            vb = b.get("vblank")
            if None in (da, db):
                filas.append((pre + " orden_idx", None, f"PENDIENTE_DATO: A={da}, B={db}"))
            elif abs(da - db) > tol_ordenes:
                filas.append((pre + " orden_idx", None, f"ritmo: A={da}, B={db} (tolerancia {tol_ordenes}) [H]"))
            else:
                filas.append((pre + " orden_idx", True, f"A={da}, B={db}"))
            if None in (va, vb):
                filas.append((pre + " vblank", None, f"PENDIENTE_DATO: A={va}, B={vb}"))
            elif abs(va - vb) > tol_vblank:
                filas.append((pre + " vblank", None, f"ritmo: A={va}, B={vb} campos (tolerancia {tol_vblank}) [H]"))
            else:
                filas.append((pre + " vblank", True, f"A={va}, B={vb}"))
        if len(la) != len(lb):
            filas.append((etiqueta, None, f"PENDIENTE_DATO: A tiene {len(la)} estados y B {len(lb)} en ese hito"))
        if sin_codigo:
            filas.append((etiqueta + " script", None,
                          f"PENDIENTE_DATO: {sin_codigo} estados sin codigo/guion_siguiente en los dos (no medido)"))
    for clave in gb:
        if clave not in ga:
            filas.append((f"f{clave[2]}:fase {clave[0]}/entrada {clave[1]}", None, "PENDIENTE_DATO: A no tiene ese hito"))
    # Capa de ordenes: bloque identico y seguido (solo hitos despues_orden) y cierra la fase con el mismo argumento.
    ops = lambda docs: [d.get("script", {}).get("op") for d in docs if d.get("hito") == "despues_orden"]
    filas.append(("ordenes: bloque identico y seguido", *bloque_ordenes(ops(a_docs), ops(b_docs))))
    cierres = lambda docs: [(d.get("script", {}).get("op"), d.get("script", {}).get("guion_siguiente"))
                            for d in docs if d.get("hito") == "cierre"]
    ca, cb = cierres(a_docs), cierres(b_docs)
    if not ca and not cb:
        filas.append(("cierres: orden 41/42/43", None, "PENDIENTE_DATO: ninguno de los dos volcados llega al cierre"))
    elif ca == cb:
        filas.append(("cierres: orden 41/42/43", True, f"{len(ca)} cierres iguales: {ca}"))
    else:
        filas.append(("cierres: orden 41/42/43", False, f"A={ca} B={cb}"))
    return filas


def firma(filas):
    return hashlib.sha1(json.dumps(filas, sort_keys=True, ensure_ascii=False).encode("utf-8")).hexdigest()[:12]


def imprimir(filas):
    for nombre, ok, detalle in filas:
        marca = "OK  " if ok else "H   " if ok is None else "FAIL"
        print(f"{marca}  {nombre}: {detalle}")
    mal = sum(1 for f in filas if f[1] is False)
    pend = sum(1 for f in filas if f[1] is None)
    print(f"resumen: {len(filas) - mal - pend} ok, {mal} fallos, {pend} [H]/PENDIENTE_DATO  firma {firma(filas)}")
    return 1 if mal else 2 if pend else 0


# ------------------------------------------------------------------------------------------------------------ autoprueba
def _doc(**cambios):
    """Estado minimo valido con dos piezas, un objeto y 5 ordenes distintas (para que el bloque cuente)."""
    d = {
        "formato": "b2-state-v1", "carril": "nativo", "fuente": "port", "fase": 1, "semilla": 123,
        "entrada_id": "S1", "evento": 1, "hito": "despues_orden", "orden_idx": 1, "vblank": 10, "turno": 2,
        "fase_rival": False, "fase_actual": 0,
        "piezas": [
            {"n": 1, "c": 0, "bando": 4, "barras": 3, "barras_max": 5, "vida": 0, "ki": -1, "ataque": 0,
             "defensa": 0, "casilla": 5, "slot": 0, "desde": 5, "en_juego": True, "fuera": False, "movs": 1,
             "estado0": 0, "acciones": 1, "aturdido": 0},
            {"n": 2, "c": 22, "bando": 2, "barras": 2, "barras_max": 5, "vida": 0, "ki": -1, "ataque": 0,
             "defensa": 0, "casilla": 6, "slot": 0, "desde": 6, "en_juego": True, "fuera": False, "movs": 1,
             "estado0": 0, "acciones": 1, "aturdido": 0},
        ],
        "objetos": [{"casilla": 3, "tipo": 0, "valor": 0, "visible": True, "enterrado": False, "hecho": False}],
        "bolas": {"mascara": 1, "fase": 0, "radar": False},
        "script": {"id": 5, "pc": None, "op": 7, "suceso": 1, "codigo": 4, "guion_siguiente": 1},
        "banderas": {"301": True, "302": False, "305": False}, "camara": {"x": 0.0, "y": 0.0},
        "rng_estado": None,
    }
    d.update(cambios)
    return d


def autoprueba():
    a = [_doc(evento=k + 1, orden_idx=k + 1, script=dict(_doc()["script"], op=o))
         for k, o in enumerate((1, 7, 19, 44, 41))]
    b = json.loads(json.dumps(a))
    filas = juzgar(a, b)
    assert not any(f[1] is False for f in filas), [f for f in filas if f[1] is False]
    # una barra distinta = fallo de pieza
    b2 = json.loads(json.dumps(a))
    b2[2]["piezas"][1]["barras"] = 1
    fallos = [f for f in juzgar(a, b2) if f[1] is False]
    assert any("pieza 2" in f[2] and "barras" in f[2] for f in fallos), fallos
    # dato ausente = PENDIENTE_DATO, nunca PASS
    b3 = json.loads(json.dumps(a))
    del b3[1]["script"]["codigo"]
    assert any(f[1] is None and "script.codigo" in f[0] for f in juzgar(a, b3))
    # hito que falta en B (o con menos estados)
    b4 = json.loads(json.dumps(a))[:3]
    assert any(f[1] is None and "A tiene 5 estados y B 3" in f[2] for f in juzgar(a, b4))
    b4b = [dict(json.loads(json.dumps(a))[0], hito="cierre", evento=99)]
    assert any(f[1] is None and "B no tiene ese hito" in f[2] for f in juzgar(a, b4b)) or \
        any(f[1] is None and "A no tiene ese hito" in f[2] for f in juzgar(a, b4b))
    # bloque de ordenes roto
    b5 = json.loads(json.dumps(a))
    b5[4]["script"]["op"] = 42
    assert any(f[0].startswith("ordenes: bloque") and f[1] is False for f in juzgar(a, b5))
    # mascara de Bolas distinta sin semilla comun = [H], no fallo
    a6 = json.loads(json.dumps(a))
    b6 = json.loads(json.dumps(a))
    for d in a6 + b6:
        d["semilla"] = None
    b6[0]["bolas"]["mascara"] = 3
    assert any("bolas" in f[0] and f[1] is None for f in juzgar(a6, b6))
    # firma estable
    assert firma(filas) == firma(juzgar(json.loads(json.dumps(a)), json.loads(json.dumps(b))))
    print(f"AUTOPRUEBA b2_juez OK ({len(filas)} filas, firma {firma(filas)})")
    return 0


def main(a):
    if a[:1] == ["--autoprueba"]:
        return autoprueba()
    if len(a) < 2:
        print(__doc__)
        return 2
    tol_vblank = int(a[a.index("--vblank") + 1]) if "--vblank" in a else 1
    tol_ordenes = int(a[a.index("--ordenes") + 1]) if "--ordenes" in a else 2
    try:
        da, db = cargar(a[0]), cargar(a[1])
    except (OSError, ValueError) as e:
        print(f"PENDIENTE_DATO: {e}")
        return 2
    filas = juzgar(da, db, tol_vblank, tol_ordenes)
    salida = imprimir(filas)
    if "--salida" in a:
        destino = Path(a[a.index("--salida") + 1])
        destino.parent.mkdir(parents=True, exist_ok=True)
        destino.write_text(json.dumps({"a": a[0], "b": a[1], "filas": [
            {"nombre": n, "ok": ok, "detalle": d} for n, ok, d in filas], "firma": firma(filas)},
            indent=1, ensure_ascii=False), encoding="utf-8")
        print(f"resultado: {destino}")
    return salida


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
