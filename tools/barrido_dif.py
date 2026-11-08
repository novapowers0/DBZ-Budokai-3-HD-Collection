r"""barrido_dif.py (noche 6->7/11, N4) - corre el barrido port-vs-original y fusiona los resultados en un ranking.

  python tools\barrido_dif.py correr  [--copia smoke_d] [--grupo iw|b2|todo] [--turbo N] [--solo 0x7D0,0x7D1]
  python tools\barrido_dif.py fusionar [--copia smoke_d]       -> DIFERENCIAS_RANKING.md/.json (studies 5\ENTREGAS\noche)
  python tools\barrido_dif.py --autoprueba

`correr` lanza comparar_modo.py por escena (subproceso con tiempo limite; una escena que se cuelga no para el barrido) y
anota una linea por escena en D:\tmp\noche\N4\barrido.log. `fusionar` lee el ULTIMO resultado de tools\dorados\resultados
por (modo, escena) y solo usa los que traen `exe` (sha12 del exe probado) y `region`; los antiguos (sin exe) se listan
como "antiguo, sin exe" y no entran en el ranking principal. Clases (orden de diferencial.py): evento > decision > tiempo >
pose > camara > sonido; luego magnitud (ratio original/port).
"""
import hashlib
import json
import re
import subprocess
import sys
import time
from pathlib import Path

AQUI = Path(__file__).resolve().parent
RAIZ = AQUI.parent
RES = AQUI / "dorados" / "resultados"
SALIDA = RAIZ / "studies 5" / "ENTREGAS" / "noche"
TMP = Path(r"E:\DBZ3HD_trabajo\noche_0711\N4")   # D:\tmp se vacio solo a las 02:20 del 7/11
CLAVES_IW = [f"0x{n:X}" for n in range(0x7D0, 0x7E2)]
CLASES = ["evento", "decision", "tiempo", "pose", "camara", "sonido"]


def sha12_exe(copia):
    p = Path(r"D:\DBZ3HD") / copia / "dbz3.exe"
    return hashlib.sha256(p.read_bytes()).hexdigest()[:12] if p.exists() else "?"


def clase_de(nombre):
    n = nombre.lower()
    if re.search(r"orden|evento|fin|salida|resultado", n):
        return "evento"
    if re.search(r"decision|azar|esquiva", n):
        return "decision"
    if re.search(r"dur|tiempo|hueco|seg", n):
        return "tiempo"
    if re.search(r"pose|anim|hueso", n):
        return "pose"
    if re.search(r"ojo|camara|fov|dist|imagen|altura", n):
        return "camara"
    return "sonido" if re.search(r"voz|sonido|audio", n) else "pose"


def magnitud(detalle):
    """ratio port/original de 'original X port Y' (1.0 = igual); None si no hay dos numeros."""
    t = re.search(r"original dist (-?[\d.]+) alt (-?[\d.]+).*?dist (-?[\d.]+) alt (-?[\d.]+) giro (-?[\d.]+); dif", detalle)
    if t:                                  # fila de camara por tramo (P2): la peor de distancia y altura
        a, b, c, d = (float(x) for x in t.groups()[:4])
        return max(abs(c - a) / max(abs(a), 1.0), abs(d - b) / max(abs(b), 1.0))
    m = re.search(r"original\s+(-?[\d.]+)\s+port\s+(-?[\d.]+)", detalle)
    if not m:
        return None
    o, p = float(m.group(1)), float(m.group(2))
    return abs(p - o) / (abs(o) if abs(o) > 1e-9 else 1.0)


def jobs(grupo, solo):
    j = []
    if grupo in ("iw", "todo"):
        j += [("iw", k) for k in CLAVES_IW]
    if grupo in ("b2", "todo"):
        j += [("b2", f"fase{n}") for n in range(1, 10)]
    if solo:
        j = [x for x in j if x[1] in solo]
    return j


def correr(copia, grupo, turbo, solo, limite=900):
    TMP.mkdir(parents=True, exist_ok=True)
    sha = sha12_exe(copia)
    for modo, esc in jobs(grupo, solo):
        for _ in range(45):                    # espera a que el juego de la escena anterior termine de cerrar
            q = subprocess.run(["powershell", "-NoProfile", "-Command",
                                f"(Get-Process dbz3 -ErrorAction SilentlyContinue | Where-Object {{$_.Path -like '*\\{copia}\\*'}}).Count"],
                               capture_output=True, text=True).stdout.strip()
            if q in ("", "0"):
                break
            time.sleep(4)
        t0 = time.time()
        pr = subprocess.Popen([sys.executable, str(AQUI / "comparar_modo.py"), modo, esc, "--copia", copia,
                               "--turbo", str(turbo)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              encoding="utf-8", errors="replace")
        try:
            sal, _ = pr.communicate(timeout=limite)
            cod = pr.returncode
        except subprocess.TimeoutExpired:
            # mata el arbol de ESTE subproceso (incluye el juego que lanzo); nunca otros juegos
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(pr.pid)], capture_output=True)
            sal, _ = pr.communicate()
            cod, sal = 99, f"TIMEOUT {limite}s " + (sal or "")[-300:]
        (TMP / f"{modo}_{esc}.txt").write_text(sal, encoding="utf-8")
        # etiqueta el ultimo resultado escrito con exe y region
        cand = sorted(RES.glob(f"{modo}_{esc}_*.json"), key=lambda p: p.stat().st_mtime)
        if cand and cand[-1].stat().st_mtime >= t0 - 1:
            d = json.loads(cand[-1].read_text(encoding="utf-8"))
            d["exe"], d["region"], d["copia"] = sha, "port xex B3 us/eu vs dorado " + ("ISO IW EU" if modo == "iw" else "ISO B2"), copia
            cand[-1].write_text(json.dumps(d, indent=1, ensure_ascii=False), encoding="utf-8")
        with open(TMP / "barrido.log", "a", encoding="utf-8") as f:
            f.write(f"{time.strftime('%H:%M')} {modo}:{esc} exit {cod} {int(time.time() - t0)}s exe {sha}\n")
        print(f"{modo}:{esc} exit {cod}", flush=True)


def ultimos():
    out = {}
    for p in sorted(RES.glob("*.json"), key=lambda p: p.stat().st_mtime):
        d = json.loads(p.read_text(encoding="utf-8"))
        out[(d["modo"], d["escena"])] = (p, d)
    return out


def filas_ranking(res):
    filas, sin = [], []
    for (modo, esc), (p, d) in res.items():
        if "exe" not in d:
            sin.append({"escena": f"{modo}:{esc}", "nota": "antiguo, sin exe", "archivo": str(p.name)})
            continue
        mudas = [f for f in d["filas"] if f["ok"] is None and re.match(r"falta captura|el dorado no tiene|\[H\] el port no escribe|el original juega", f["detalle"])]
        if mudas:
            filas.append({"escena": f"{modo}:{esc}", "region": d["region"], "clase": "camara", "estado": "sin dato",
                          "nombre": f"{len(mudas)} medidas sin dato (imagen/fov/salida)", "detalle": "; ".join(sorted({f['nombre'].split()[0] for f in mudas})) + " (el dorado o el port no las trae)",
                          "mag": None, "evidencia": str(p), "exe": d["exe"]})
        for f in d["filas"]:
            if f["ok"] is True or f in mudas or (f["ok"] is None and "no cuenta" in f["nombre"]):   # tramos de presentacion: solo informativos
                continue
            cls = clase_de(f["nombre"])
            if modo == "iwusa":
                f = dict(f, detalle=f["detalle"].replace("original", "EU").replace("port", "USA") + " (original USA contra original EU: no es un fallo del port; puede ser otra partida)")
            elif d.get("escena") in NOTAS and f["ok"] is False:
                f = dict(f, detalle=f["detalle"] + NOTAS[d["escena"]])
            filas.append({"escena": f"{modo}:{esc}", "region": d["region"], "clase": cls, "estado": "FAIL" if f["ok"] is False else "sin dato",
                          "nombre": f["nombre"], "detalle": f["detalle"], "mag": magnitud(f["detalle"]),
                          "evidencia": str(p), "exe": d["exe"]})
    filas.sort(key=lambda r: (r["estado"] != "FAIL", r["escena"].startswith("iwusa"), CLASES.index(r["clase"]), -(r["mag"] or 0)))
    return filas, sin


NOTAS = {"fase1": " [el juego no llego a la escena M207 en 30 s: puede ser fallo de llegada, no de ordenes]", "fase2": " [el juego no llego a la escena en 30 s: puede ser fallo de llegada, no de ordenes]"}
MOTIVOS = {}      # "modo:escena" -> motivo de "sin dorado" (se rellena desde N4_motivos.json si existe)
EXTRA = {"b2:nave (Nave de Babidi, Pasarela)": "sin dorado (no hay captura numerica del original de la nave; r2_nave del port PASA, pantallas modo/planta/rueda/menu/burbuja/prueba/fin, 146 s)"}


def fusionar():
    mj = SALIDA / "N4_motivos.json"
    if mj.exists():
        MOTIVOS.update(json.loads(mj.read_text(encoding="utf-8")))
    res = ultimos()
    esperados = [("iw", k) for k in CLAVES_IW] + [("iwusa", k) for k in CLAVES_IW] + [("b2", f"fase{n}") for n in range(1, 10)]
    for k in list(res):                       # dorado stub (<1 KB): el resultado no mide nada
        g = AQUI / "dorados" / f"{k[0]}_{k[1]}.json"
        if g.exists() and g.stat().st_size < 1000 and k[0] == "iw":
            del res[k]
    filas, sin = filas_ranking(res)
    for k in esperados:
        if k not in res:
            g = AQUI / "dorados" / f"{k[0]}_{k[1]}.json"
            motivo = MOTIVOS.get(f"{k[0]}:{k[1]}") or ("sin dorado (stub de 0.5 KB; hay que capturarlo en PCSX2)" if g.exists() and g.stat().st_size < 1000 else "sin resultado en este barrido")
            filas.append({"escena": f"{k[0]}:{k[1]}", "region": "-", "clase": "evento", "estado": "sin dato", "nombre": "cobertura",
                          "detalle": motivo, "mag": None, "evidencia": "-", "exe": "-"})
    for nom, mot in EXTRA.items():            # familias sin dorado numerico
        filas.append({"escena": nom, "region": "-", "clase": "evento", "estado": "sin dato", "nombre": "cobertura", "detalle": mot,
                      "mag": None, "evidencia": "-", "exe": "-"})
    cob = []
    for k in esperados + [("iwamc0", c) for c in ("0x7D7", "0x7D8", "0x7DE")] + [("iwseg1", c) for c in ("0x7D7", "0x7D8", "0x7DE")]:
        if k not in res:
            cob.append({"escena": f"{k[0]}:{k[1]}", "resultado": next((f["detalle"] for f in filas if f["escena"] == f"{k[0]}:{k[1]}" and f["nombre"] == "cobertura"), "sin resultado"), "numeros": "-", "exe": "-"})
            continue
        d = res[k][1]
        num = "; ".join(f"{f['nombre'].split(' (')[0]} {'ok' if f['ok'] else 'FALLA'}: {f['detalle'].split(' (')[0]}" for f in d["filas"] if f["ok"] is not None)
        nf = sum(1 for f in d["filas"] if f["ok"] is False)
        cob.append({"escena": f"{k[0]}:{k[1]}", "resultado": "FALLA" if nf else "ok", "numeros": num or "solo medidas sin dato", "exe": d.get("exe", "-")[:12], "region": d.get("region", "")})
    ok = sum(1 for _, (_, d) in res.items() if "exe" in d for f in d["filas"] if f["ok"] is True)
    SALIDA.mkdir(parents=True, exist_ok=True)
    (SALIDA / "DIFERENCIAS_RANKING.json").write_text(json.dumps({"filas": filas, "antiguos": sin, "ok": ok, "cobertura": cob}, indent=1, ensure_ascii=False), encoding="utf-8")
    rs = SALIDA / "N4_resumen.txt"          # resumen en castellano llano, escrito a mano al cerrar
    L = ["# Diferencias contra el original (barrido N4)", ""] + (rs.read_text(encoding="utf-8").splitlines() + [""] if rs.exists() else []) + [ f"Filas OK: {ok}. Distintas o sin dato: {len(filas)}. Resultados antiguos sin exe: {len(sin)}.", "",
         "| # | escena | region | clase | estado | metrica | original vs port | exe | evidencia |", "|---|---|---|---|---|---|---|---|---|"]
    for i, r in enumerate(filas, 1):
        L.append(f"| {i} | {r['escena']} | {r['region']} | {r['clase']} | {r['estado']} | {r['nombre']} | {r['detalle'].replace('|', '/')} | {r['exe']} | {Path(r['evidencia']).name} |")
    L += ["", "## Cobertura: una linea por escena (numero o motivo)", "", "| escena | resultado | numeros (original vs port) | exe |", "|---|---|---|---|"]
    L += [f"| {c['escena']} | {c['resultado'][:90]} | {c['numeros'].replace('|', '/')} | {c['exe']} |" for c in cob]
    (SALIDA / "DIFERENCIAS_RANKING.md").write_text("\n".join(L) + "\n", encoding="utf-8")
    print(f"ranking: {len(filas)} filas, {ok} ok, {len(sin)} antiguos")


def autoprueba():
    assert clase_de("orden de guion") == "evento" and clase_de("dist_ojo_jugador (mediana)") == "camara"
    assert magnitud("original 54.33 port 54.35 (limite 8)") < 0.01 and magnitud("sin numeros") is None
    assert clase_de("camara tramo 1/2 (#AMC 1, 239 muestras)") == "camara"
    m = magnitud("original dist 90.0 alt 36.0 giro 10.0; port mas cercano (clip 0xBA, 1 muestras) dist 60.0 alt 31.0 giro 5.0; dif dist 30.0 alt 5.0 giro 5.0 [x]")
    assert abs(m - 1 / 3) < 1e-6, m
    fake = {("iw", "0x1"): (Path("a.json"), {"modo": "iw", "escena": "0x1", "exe": "abc", "region": "r",
                                             "filas": [{"nombre": "fov", "ok": None, "detalle": "x"},
                                                       {"nombre": "orden", "ok": False, "detalle": "original 1 port 2"},
                                                       {"nombre": "ok", "ok": True, "detalle": ""}]}),
            ("b2", "fase1"): (Path("b.json"), {"modo": "b2", "escena": "fase1", "filas": []})}
    filas, sin = filas_ranking(fake)
    assert [r["estado"] for r in filas] == ["FAIL", "sin dato"] and len(sin) == 1 and filas[0]["clase"] == "evento"
    assert len(jobs("todo", None)) == 27 and len(CLAVES_IW) == 18
    print("barrido_dif autoprueba OK")


if __name__ == "__main__":
    a = sys.argv[1:]
    if not a or a[0] == "--autoprueba":
        autoprueba()
    elif a[0] == "correr":
        g = lambda k, d: a[a.index(k) + 1] if k in a else d
        correr(g("--copia", "smoke_d"), g("--grupo", "todo"), int(g("--turbo", 1)), g("--solo", "").split(",") if "--solo" in a else None)
    elif a[0] == "fusionar":
        fusionar()
