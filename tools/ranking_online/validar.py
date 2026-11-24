"""Ranking online de Dragon Budokai Survivors y Dragon Rogue (RANK-ONLINE): lo corre la GitHub Action del repo DBZ3HD-Ranking.

Lee los envios del buzon ntfy (`<ntfy>/<tema>/json?poll=1&since=12h`), comprueba la firma HMAC, quita duplicados,
rehace los puntos y los topes (plausibilidad) y escribe las tablas que descarga el juego:
  survivors/semana.json, survivors/siempre.json, rogue/diario/AAAA-MM-DD.json y recibidos.json (id -> ok o motivo).
Lo aceptado se guarda en datos/entradas.jsonl (el estado del repo; ntfy borra los mensajes a las 12 h).

  python validar.py --repo . --ntfy https://ntfy.sh --tema <tema>     (claves en RANK_HMAC_KEY y RANK_HMAC_KEY_ANTERIOR)
  python validar.py --repo . --entrada mensajes.jsonl                  (sin red: el mismo formato que da ntfy)
  python validar.py --autoprueba
El mensaje (lo arma src/ranking_online.cpp) es una query: v=1&m=r|s&id=..&u=..&al=..&ver=..&t=..&...&h=<hmac hex>,
firmada sobre todo lo que va antes de "&h=".
"""
import argparse
import datetime as dt
import hashlib
import hmac
import json
import os
import re
import sys
import tempfile
import time
import urllib.request
from collections import Counter, defaultdict
from pathlib import Path
from urllib.parse import parse_qsl

TOPE_FILAS = 2000          # ponytail: todas las filas en un JSON; con mas jugadores, paginar (top + "puestos")
DIAS_DIARIO = 30
DIAS_RECIBIDOS = 14
MAX_DIA_JUGADOR = 20
PROHIBIDAS = ("puta", "puto", "mierda", "polla", "coño", "cono", "nazi", "hitler", "fuck", "shit", "bitch", "cunt",
              "nigg", "fag", "retard", "maricon", "subnormal", "admin", "moderador")
ALIAS = re.compile(r"^[A-Za-z0-9_\-]{3,16}$")


def semilla_diaria(dia):   # = SemillaDiaria de src/rogue_resultado.cpp
    x = ((dia * 2654435761) & 0xFFFFFFFF) ^ 0x524F4755
    x ^= x >> 16
    x = (x * 0x45D9F3B) & 0xFFFFFFFF
    return x ^ (x >> 16)


def puntos_rogue(g, b, vi, sg):   # = PuntosDiario
    return max(g * 100 + b * 500 + vi // 10 - sg, 0)


def puntos_surv(sg, k, w):        # = PuntosSurvivors de src/ranking_online.cpp
    return k * 10 + sg * 30 + (3000 if w else 0)


def firma(clave, texto):
    return hmac.new(clave.encode(), texto.encode(), hashlib.sha256).hexdigest()


def fecha(dia):
    return (dt.date(1970, 1, 1) + dt.timedelta(days=dia)).isoformat()


def leer(msg, claves):
    """Mensaje -> (entrada, None) o (None, motivo)."""
    cuerpo, sep, h = msg.rpartition("&h=")
    if not sep or not any(hmac.compare_digest(firma(k, cuerpo), h.lower()) for k in claves if k):
        return None, "firma"
    q = dict(parse_qsl(cuerpo, keep_blank_values=True))
    try:
        e = {"m": q["m"], "id": q["id"], "u": q["u"], "al": q["al"], "ver": q["ver"][:16], "t": int(q["t"]),
             "j": int(q["j"]), "p": int(q["p"]), "sg": int(q["sg"])}
        if q.get("v") != "1" or not re.fullmatch(r"[0-9a-f]{16}", e["id"]) or not re.fullmatch(r"[0-9a-f]{16}", e["u"]):
            return None, "formato"
        if e["m"] == "r":
            e.update(d=int(q["d"]), se=int(q["se"]), g=int(q["g"]), b=int(q["b"]), vi=int(q["vi"]),
                     c=[c for c in q.get("c", "").split(",") if re.fullmatch(r"[a-z0-9_]{1,24}", c)][:40])
        elif e["m"] == "s":
            e.update(k=int(q["k"]), n=int(q["n"]), w=int(q["w"]), e=q.get("e", "")[:24], df=int(q.get("df", "0")))
        else:
            return None, "formato"
    except (KeyError, ValueError):
        return None, "formato"
    al = e["al"].lower()
    if not ALIAS.fullmatch(e["al"]) or any(w in al for w in PROHIBIDAS):
        return None, "alias"
    return e, plausible(e)


def plausible(e):
    """None = vale; si no, el motivo. Rehace los puntos y aplica topes (frena trampas burdas, no las imposibilita)."""
    if not 0 <= e["j"] < 64:
        return "luchador"
    if e["m"] == "r":
        if e["se"] != semilla_diaria(e["d"]):
            return "semilla"
        if not (0 <= e["g"] <= 200 and 0 <= e["b"] <= e["g"] // 5 + 1 and 0 <= e["vi"] <= 20000):
            return "topes"
        if e["sg"] < e["g"] * 15:   # un piso de menos de 15 s no existe
            return "tiempo"
        if e["p"] != puntos_rogue(e["g"], e["b"], e["vi"], e["sg"]):
            return "puntos"
    else:
        if not (0 < e["sg"] <= 1800 and 0 <= e["k"] <= e["sg"] * 10 + 50 and 1 <= e["n"] <= 300 and e["w"] in (0, 1)):
            return "topes"
        if e["w"] and e["sg"] < 600:
            return "tiempo"
        if e["p"] != puntos_surv(e["sg"], e["k"], e["w"]):
            return "puntos"
    return None


def procesar(mensajes, repo, claves, ahora=None):
    """mensajes: [(time ntfy, texto)]. Devuelve {id: estado} de esta tanda."""
    ahora = int(ahora or time.time())
    hoy = ahora // 86400
    repo = Path(repo)
    estado_f = repo / "datos" / "entradas.jsonl"
    entradas = [json.loads(x) for x in estado_f.read_text(encoding="utf-8").splitlines() if x.strip()] if estado_f.is_file() else []
    rec_f = repo / "recibidos.json"
    recibidos = json.loads(rec_f.read_text(encoding="utf-8")).get("ids", {}) if rec_f.is_file() else {}
    vistos = {e["id"] for e in entradas} | set(recibidos)   # lo ya contestado (ok o motivo) no se mira dos veces
    por_dia = Counter((e["u"], e["llego"] // 86400) for e in entradas)
    diario_hecho = {(e["u"], e["d"]) for e in entradas if e["m"] == "r"}
    nuevos = {}
    for t, msg in sorted(mensajes):
        e, motivo = leer(msg, claves)
        if e is None:
            continue   # sin firma valida no hay id fiable: ni se apunta
        if e["id"] in vistos:
            continue   # el juego reenvia hasta verlo: no duplica
        if motivo is None and e["m"] == "r":
            if not hoy - 1 <= e["d"] <= hoy or not e["d"] - 1 <= t // 86400 <= e["d"] + 1:
                motivo = "dia"
            elif (e["u"], e["d"]) in diario_hecho:
                motivo = "repetido"   # un intento puntuable al dia
        if motivo is None and por_dia[(e["u"], t // 86400)] >= MAX_DIA_JUGADOR:
            motivo = "limite"
        vistos.add(e["id"])
        recibidos[e["id"]] = [motivo or "ok", ahora]
        nuevos[e["id"]] = motivo or "ok"
        if motivo:
            continue
        e["llego"] = int(t)
        entradas.append(e)
        por_dia[(e["u"], t // 86400)] += 1
        if e["m"] == "r":
            diario_hecho.add((e["u"], e["d"]))
    # Rogue: el luchador del dia es el mismo para todos; con 3 o mas, se tira el que no sea el de la mayoria
    dias = defaultdict(list)
    for e in entradas:
        if e["m"] == "r":
            dias[e["d"]].append(e)
    for d, v in dias.items():
        if len(v) >= 3:
            comun = Counter(e["j"] for e in v).most_common(1)[0][0]
            for e in [e for e in v if e["j"] != comun]:
                entradas.remove(e)
                recibidos[e["id"]] = ["luchador", ahora]
                nuevos[e["id"]] = "luchador"
    entradas = [e for e in entradas if e["m"] != "r" or e["d"] >= hoy - DIAS_DIARIO]
    recibidos = {k: v for k, v in recibidos.items() if v[1] >= ahora - DIAS_RECIBIDOS * 86400}
    if nuevos or not estado_f.is_file():   # sin nada nuevo no se reescribe: la Action no sube un commit cada 10 min
        escribir(repo, entradas, recibidos, ahora)
    return nuevos


def perfil(entradas):
    """uid -> mejor Survivors, mejor Rogue diario, racha de dias, enviadas y luchador favorito (para la ficha 03)."""
    por = defaultdict(list)
    for e in entradas:
        por[e["u"]].append(e)
    out = {}
    for u, v in por.items():
        dias = sorted({e["d"] for e in v if e["m"] == "r"}, reverse=True)
        racha = 0
        for i, d in enumerate(dias):
            if d != dias[0] - i:
                break
            racha += 1
        out[u] = {"ms": max([e["p"] for e in v if e["m"] == "s"], default=0), "mr": max([e["p"] for e in v if e["m"] == "r"], default=0),
                  "ra": racha, "en": len(v), "fav": Counter(e["j"] for e in v).most_common(1)[0][0]}
    return out


def tabla(v, perf, ahora, **extra):
    mejor = {}
    for e in v:   # la mejor de cada jugador; a igualdad, la que llego antes
        if e["u"] not in mejor or (e["p"], -e["llego"]) > (mejor[e["u"]]["p"], -mejor[e["u"]]["llego"]):
            mejor[e["u"]] = e
    filas = sorted(mejor.values(), key=lambda e: (-e["p"], e["llego"]))[:TOPE_FILAS]
    out = []
    for i, e in enumerate(filas):
        f = {k: e[k] for k in e if k not in ("llego", "se")}
        f["pos"] = i + 1
        out.append(f)
    return {"generado": ahora, "total": len(mejor), **extra, "filas": out, "jugadores": {e["u"]: perf[e["u"]] for e in filas}}


def guardar(ruta, datos):
    ruta.parent.mkdir(parents=True, exist_ok=True)
    texto = datos if isinstance(datos, str) else json.dumps(datos, ensure_ascii=False, separators=(",", ":"))
    fd, tmp = tempfile.mkstemp(dir=ruta.parent)
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
        f.write(texto)
    os.replace(tmp, ruta)


def escribir(repo, entradas, recibidos, ahora):
    perf = perfil(entradas)
    surv = [e for e in entradas if e["m"] == "s"]
    lunes = (ahora // 86400 - (ahora // 86400 + 3) % 7)   # 1/1/1970 fue jueves
    guardar(repo / "survivors" / "semana.json", tabla([e for e in surv if e["llego"] // 86400 >= lunes], perf, ahora, periodo="semana", desde=lunes))
    guardar(repo / "survivors" / "siempre.json", tabla(surv, perf, ahora, periodo="siempre"))
    hoy = ahora // 86400
    for d in range(hoy - 1, hoy + 1):   # los dos dias que aun cambian (los viejos se quedan escritos)
        guardar(repo / "rogue" / "diario" / f"{fecha(d)}.json",
                tabla([e for e in entradas if e["m"] == "r" and e["d"] == d], perf, ahora, dia=d, cierra=(d + 1) * 86400))
    guardar(repo / "recibidos.json", {"generado": ahora, "ids": recibidos})
    guardar(repo / "datos" / "entradas.jsonl", "".join(json.dumps(e, ensure_ascii=False, separators=(",", ":")) + "\n" for e in entradas))


def leer_ntfy(base, tema):
    with urllib.request.urlopen(f"{base.rstrip('/')}/{tema}/json?poll=1&since=12h", timeout=60) as r:
        lineas = r.read().decode("utf-8", "replace").splitlines()
    return lineas_a_mensajes(lineas)


def lineas_a_mensajes(lineas):
    out = []
    for x in lineas:
        try:
            m = json.loads(x)
        except ValueError:
            continue
        if m.get("event") == "message" and m.get("message"):
            out.append((int(m.get("time", time.time())), m["message"]))
    return out


# ---------------------------------------------------------------- autoprueba (datos inventados)
def armar(clave, **c):
    """Lo mismo que arma el juego (src/ranking_online.cpp, Armar)."""
    orden = ["v", "m", "id", "u", "al", "ver", "t", "j", "sg", "p", "d", "se", "g", "b", "vi", "c", "k", "n", "w", "e", "df"]
    from urllib.parse import quote
    cuerpo = "&".join(f"{k}={quote(str(c[k]), safe='-_.,')}" for k in orden if k in c)
    return cuerpo + "&h=" + firma(clave, cuerpo)


def autoprueba():
    K = "clave-de-prueba"
    ahora = 20404 * 86400 + 3600 * 10
    hoy = 20404
    se = semilla_diaria(hoy)

    def rogue(id_, u, al, g=3, b=1, vi=3000, sg=120, j=7, d=hoy, **x):
        c = dict(v=1, m="r", id=id_, u=u, al=al, ver="1.5.7", t=ahora, j=j, sg=sg, p=puntos_rogue(g, b, vi, sg), d=d, se=se if d == hoy else semilla_diaria(d), g=g, b=b, vi=vi, c="ki_01,fisica_03")
        c.update(x)
        return armar(K, **c)

    def surv(id_, u, al, sg=900, k=3000, w=0, **x):
        c = dict(v=1, m="s", id=id_, u=u, al=al, ver="1.5.7", t=ahora, j=0, sg=sg, p=puntos_surv(sg, k, w), k=k, n=40, w=w, e="llanura", df=0)
        c.update(x)
        return armar(K, **c)

    U = [f"{i:016x}" for i in range(1, 9)]
    msgs = [rogue("a" * 16, U[0], "Javi_B3"),
            rogue("a" * 16, U[0], "Javi_B3"),                         # duplicado exacto: no cuenta dos veces
            rogue("b" * 16, U[0], "Javi_B3", g=9, sg=300),                  # segundo intento del dia: repetido
            rogue("c" * 16, U[1], "NamekStar", g=9, sg=200),
            rogue("d" * 16, U[2], "Trampas", p=99999),                # puntos que no cuadran
            rogue("e" * 16, U[3], "Rapido", g=9, sg=30),              # 9 pisos en 30 s
            rogue("f" * 16, U[4], "OtroLuch", j=12),                  # luchador distinto a la mayoria
            rogue("1" * 16, U[5], "Semilla", se=123),                 # semilla falsa
            rogue("2" * 16, U[6], "putoamo"),                         # alias prohibido
            surv("3" * 16, U[0], "Javi_B3"),
            surv("4" * 16, U[1], "NamekStar", sg=1800, k=4210, w=1),
            surv("5" * 16, U[2], "Kame_Senin", sg=1801, k=10),        # mas de 30 min
            surv("6" * 16, U[3], "Bajas", sg=100, k=5000),            # bajas imposibles
            surv("7" * 16, U[0], "Javi_B3", sg=1000, k=3500)]         # mejor partida del mismo jugador
    falsa = msgs[0].replace("&h=", "&h=0")                         # firma rota
    con_otra_clave = armar("clave-vieja", v=1, m="s", id="8" * 16, u=U[7], al="Viejo", ver="1.5.6", t=ahora, j=1, sg=600, p=puntos_surv(600, 900, 0), k=900, n=20, w=0, e="llanura", df=0)
    with tempfile.TemporaryDirectory() as tmp:
        res = procesar([(ahora - 50 + i, m) for i, m in enumerate(msgs + [falsa, con_otra_clave])], tmp, [K, "clave-vieja"], ahora)
        esperado = {"a" * 16: "ok", "b" * 16: "repetido", "c" * 16: "ok", "d" * 16: "puntos", "e" * 16: "tiempo",
                    "f" * 16: "luchador", "1" * 16: "semilla", "3" * 16: "ok", "4" * 16: "ok", "5" * 16: "topes",
                    "6" * 16: "topes", "7" * 16: "ok", "8" * 16: "ok"}
        assert all(res.get(k) == v for k, v in esperado.items()), res
        assert "2" * 16 not in res   # alias prohibido: ni entra
        sem = json.loads((Path(tmp) / "survivors" / "semana.json").read_text(encoding="utf-8"))
        assert [f["al"] for f in sem["filas"]] == ["NamekStar", "Javi_B3", "Viejo"], sem["filas"]
        assert sem["filas"][1]["id"] == "7" * 16 and sem["total"] == 3
        dia = json.loads((Path(tmp) / "rogue" / "diario" / f"{fecha(hoy)}.json").read_text(encoding="utf-8"))
        assert [f["al"] for f in dia["filas"]] == ["NamekStar", "Javi_B3"], dia["filas"]   # OtroLuch: no es el del dia
        assert dia["jugadores"][U[0]]["en"] == 3 and dia["jugadores"][U[0]]["ra"] == 1 and dia["cierra"] == (hoy + 1) * 86400
        # segunda pasada (la Action lee las 12 h otra vez): solo cuenta lo nuevo
        res2 = procesar([(ahora - 50 + i, m) for i, m in enumerate(msgs)] + [(ahora, rogue("9" * 16, U[7], "Tercero", g=2, sg=60))], tmp, [K], ahora + 60)
        assert res2 == {"9" * 16: "ok"}, res2
        dia = json.loads((Path(tmp) / "rogue" / "diario" / f"{fecha(hoy)}.json").read_text(encoding="utf-8"))
        assert [f["al"] for f in dia["filas"]] == ["NamekStar", "Javi_B3", "Tercero"]
        rec = json.loads((Path(tmp) / "recibidos.json").read_text(encoding="utf-8"))["ids"]
        assert rec["a" * 16][0] == "ok" and rec["f" * 16][0] == "luchador"
        # limite de 20 envios al dia por jugador
        muchos = [(ahora + 100 + i, surv(f"{0xabc000 + i:016x}", U[5], "Muchos", sg=700 + i)) for i in range(25)]
        res3 = procesar(muchos, tmp, [K], ahora + 200)
        assert Counter(res3.values()) == {"ok": 20, "limite": 5}, Counter(res3.values())
        # mensajes de ntfy tal cual (lineas JSON, con keepalive)
        lin = [json.dumps({"event": "open"}), json.dumps({"event": "message", "time": ahora, "message": msgs[0]})]
        assert lineas_a_mensajes(lin) == [(ahora, msgs[0])]
    print("autoprueba validar.py: OK (13 casos de firma, duplicados, plausibilidad, mayoria, limite y tablas)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--ntfy", default=os.environ.get("RANK_NTFY", "https://ntfy.sh"))
    ap.add_argument("--tema", default=os.environ.get("RANK_TEMA", ""))
    ap.add_argument("--entrada", help="archivo con las lineas JSON de ntfy (sin red)")
    ap.add_argument("--autoprueba", action="store_true")
    a = ap.parse_args()
    if a.autoprueba:
        return autoprueba()
    claves = [os.environ.get("RANK_HMAC_KEY", ""), os.environ.get("RANK_HMAC_KEY_ANTERIOR", "")]
    if not claves[0]:
        sys.exit("falta RANK_HMAC_KEY")
    if a.entrada:
        msgs = lineas_a_mensajes(Path(a.entrada).read_text(encoding="utf-8").splitlines())
    else:
        if not a.tema:
            sys.exit("falta --tema (o RANK_TEMA)")
        msgs = leer_ntfy(a.ntfy, a.tema)
    res = procesar(msgs, a.repo, claves)
    print(f"{len(msgs)} mensajes, {len(res)} nuevos: {dict(Counter(res.values()))}")


if __name__ == "__main__":
    main()
