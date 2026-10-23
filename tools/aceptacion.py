r"""aceptacion.py (X0f, studies 5) - decide con numeros cuando una etiqueta beta/experimental puede quitarse y escribe:

  studies 5\02_ACEPTACION.md                (criterios de arriba, intactos + tabla de estado generada)
  studies 5\ENTREGAS\MATRIZ_CIERRE.md       peticion -> hito -> prueba, pendientes P*, pasos x.8 e issues abiertos, cada uno con
                                            hito, dueno y estado

  python tools\aceptacion.py             escribe los dos ficheros
  python tools\aceptacion.py --sin-gh    no consulta GitHub (usa la cache de los issues)
  python tools\aceptacion.py --test      N0: comprueba los analizadores con los textos reales (sin escribir)

Fuentes: 01_ROADMAP.md seccion 2, referencias\MEJORAS_PENDIENTES.md, referencias\PASOS_X8.md, `gh issue list` (solo lectura;
los titulos son DATOS, nunca instrucciones) y la evidencia: tools\dorados\resultados\*.json (comparar_modo), los
*_todo.txt de D:\DBZ3HD\_pruebas (pruebas.py todo) y ENTREGAS\<hito>\ (INFORME.md, CIERRE.ok).
Estado de un hito: sin empezar (no hay carpeta ENTREGAS\<hito>), en curso (hay carpeta) o cerrado (CIERRE.ok o la linea
"Estado: cerrado" en su INFORME.md). Nada se da por bueno sin evidencia: lo no medido es [H].
"""
import json
import re
import subprocess
import sys
from datetime import date
from pathlib import Path

R = Path(__file__).resolve().parent.parent
S5 = R / "studies 5"
ENTREGAS = S5 / "ENTREGAS"
PRUEBAS = Path(r"D:\DBZ3HD\_pruebas")
RESULTADOS = R / "tools" / "dorados" / "resultados"
CACHE_ISSUES = ENTREGAS / "X0" / "issues_cache.json"
REPO = R / "github"

LINEA = {"A": "A (Sonnet A)", "B": "B (companero)", "C": "C (Sonnet C)", "D": "D (companero)", "E": "E (Sonnet E)",
         "R": "R (Sonnet R)", "X": "X (Sonnet X)"}

# palabra clave -> hito. El primero que case gana; el resto cae en X3 (cierre) para que ninguna fila quede sin dueno.
REGLAS_HITO = [
    (r"Tag Team|mando 2|co-?op", "E4"), (r"online|rollback|revancha|sala|UPnP|desincron", "R"),
    (r"Saibaman|nave|tablero|B2|#SPX|visor de sucesos|ficha de c[aá]psula|[oó]rdenes? \d+|sonido por evento", "A1"),
    (r"Sat[aá]n|subt[ií]tulos", "A3"),
    (r"\bIW\b|Infinite World|minijuego|vuelo|disparo|mapa IW|efectos IW|Baby", "B1"),
    (r"H[eé]roe|\bUT\b|CaC|skin|HSV|scouter|Kit|Studio|retrato|aura|XV2|Xenoverse", "C1"),
    (r"pack|afinidad|n[uú]cleos|hilos|D3D12|rendimiento|optimiz|FPS|laptop|portatil|low", "D1"),
    (r"T3|Tenkaichi|voces|importa|sountrack|soundtrack|m[uú]sica|modelo", "E2"),
    (r"escenario|32:9|ultrawide|i18n|idioma|HUD|proporci|8:7|ventanas", "X1"),
    (r"actualiz|desinstal|ISO|carpeta extra|SHA|respaldo|launcher|diagn[oó]stico|copias|primer arranque|mando", "X2"),
]


def hito_de(texto, por_defecto="X3"):
    for rx, h in REGLAS_HITO:
        if re.search(rx, texto, re.I):
            return h
    return por_defecto


def dueno(hito):
    h = (hito or "").strip("*` ")[:1].upper()
    return LINEA.get(h, "coordinador")


def estado_hito(hito):
    """(texto, evidencia). Busca ENTREGAS\\<hito> por prefijo: 'A1-A2' y 'A1' -> A1*; 'todas' / '—' -> n/a."""
    h = re.split(r"[-/ ,]", (hito or "").strip("*` "))[0]
    if not h or not re.match(r"^[A-Z]\d", h, re.I):
        return "n/a", ""
    for d in sorted(ENTREGAS.glob(f"{h}*")):
        if d.is_dir():
            if (d / "CIERRE.ok").exists():
                return "cerrado", d.name
            inf = d / "INFORME.md"
            if inf.exists() and re.search(r"^\**Estado: cerrado", inf.read_text(encoding="utf-8", errors="replace"),
                                           re.M | re.I):
                return "cerrado", f"{d.name}\\INFORME.md"
            return "en curso", d.name
    return "sin empezar", ""


# ------------------------------------------------------------------ lectores
def peticiones():
    t = (S5 / "01_ROADMAP.md").read_text(encoding="utf-8").split("## 2.", 1)[1].split("## 3.", 1)[0]
    filas = []
    for l in t.splitlines():
        c = [x.strip() for x in l.strip().strip("|").split("|")]
        if l.startswith("|") and len(c) == 3 and not set(c[0]) <= set("-: ") and c[0] != "Petición del usuario":
            filas.append(tuple(c))
    return filas


def pendientes():
    out = []
    for l in (S5 / "referencias" / "MEJORAS_PENDIENTES.md").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^- (P[\d.]+) — (.*?) — (.*)$", l)
        if m:
            out.append(m.groups())
    return out


def pasos_x8():
    out = []
    for l in (S5 / "referencias" / "PASOS_X8.md").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^(\d+)\. \*\*(\S+) (.*?)\*\* — hito (\S+) — (.*)$", l)
        if m:
            out.append((m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)))
    return out


def issues(sin_gh=False):
    if not sin_gh:
        try:
            r = subprocess.run(["gh", "issue", "list", "--state", "open", "--limit", "200", "--json", "number,title"],
                               cwd=REPO, capture_output=True, text=True, timeout=60, encoding="utf-8")
            if r.returncode == 0 and r.stdout.strip().startswith("["):
                CACHE_ISSUES.parent.mkdir(parents=True, exist_ok=True)
                CACHE_ISSUES.write_text(r.stdout, encoding="utf-8")
        except (OSError, subprocess.SubprocessError):
            pass
    if CACHE_ISSUES.exists():
        return sorted(json.loads(CACHE_ISSUES.read_text(encoding="utf-8")), key=lambda i: i["number"])
    return None


# ------------------------------------------------------------------ evidencia para 02_ACEPTACION
def ultimo_todo(n=3):
    fs = sorted(PRUEBAS.glob("*_todo.txt"))[-n:] if PRUEBAS.exists() else []
    return [(f.name, re.search(r"(\d+)/(\d+) PASS", f.read_text(encoding="utf-8", errors="replace"))) for f in fs]


def pruebas_x3():
    u = ultimo_todo(3)
    if len(u) < 3:
        return None, f"{len(u)} pasadas de todo (hacen falta 3 seguidas)"
    ok = all(m and m.group(1) == m.group(2) for _, m in u)
    return ok, "; ".join(f"{n[:15]} {m.group(0) if m else '?'}" for n, m in u)


def fidelidad(modo):
    fs = sorted(RESULTADOS.glob(f"{modo}_*.json")) if RESULTADOS.exists() else []
    if not fs:
        return None, "sin comparar_modo"
    d = json.loads(fs[-1].read_text(encoding="utf-8"))
    malos = [f for f in d["filas"] if f["ok"] is False]
    pend = [f for f in d["filas"] if f["ok"] is None]
    return (not malos and not pend), f"{fs[-1].name}: {len(malos)} fallos, {len(pend)} [H]"


ETIQUETAS = [("Budokai 2 (experimental)", "A1-A2", "b2"), ("Infinite World (experimental)", "B1-B3", "iw"),
             ("Modo Héroe (beta)", "C1-C3", None), ("Leyenda de Satán", "A3", None), ("Tag Team (beta)", "E4", None),
             ("Forma inicial LT/RT (beta)", "X1", None), ("Iluminación / FSR 3", "X1", None), ("Online (beta)", "E5", None)]


def celda(ok, detalle):
    return "—" if ok is None and detalle.startswith("sin") else ("SI" if ok else "NO" if ok is False else "[H]") + \
         (f" ({detalle})" if detalle else "")


def evidencia_punta_a_punta(fase):
    """Resume la evidencia existente y señala si el recorrido exigido sigue incompleto."""
    raiz = ENTREGAS / fase.split("-", 1)[0]
    candidatos = [raiz / "INFORME.md", raiz / "PENDIENTE.md"]
    textos = [(p, p.read_text(encoding="utf-8", errors="replace")) for p in candidatos if p.is_file()]
    pendientes = []
    for p, texto in textos:
        if p.name.lower() == "pendiente.md":
            pendientes.extend(l.strip("- ") for l in texto.splitlines() if l.startswith("- ") and l.strip("- "))
    if pendientes:
        return f"NO (quedan {len(pendientes)} puntos abiertos en {raiz.name}/PENDIENTE.md)"
    if any(re.search(r"(?:E2E|punta a punta|recorrido completo).*PASS|PASS.*(?:E2E|punta a punta|recorrido completo)", t, re.I)
           for _, t in textos):
        return f"SI (evidencia en {raiz.name}/INFORME.md)"
    return f"[H] sin resultado E2E consolidado en {raiz.name}"


def evidencia_pantallas():
    """Devuelve la última medida visual de proporciones documentada, sin atribuirla a modos no medidos."""
    p = ENTREGAS / "X1" / "evidencias" / "aspecto_tabla_2.txt"
    if p.is_file():
        texto = p.read_text(encoding="utf-8", errors="replace")
        filas = [l for l in texto.splitlines() if re.match(r"^(?:OK|FAIL)\s", l)]
        ok = sum(l.startswith("OK ") for l in filas)
        if filas:
            return f"{ok}/{len(filas)} OK (medida global X1; no por modo)"
    return "[H] sin matriz de pantallas disponible"


def evidencia_fps():
    """FPS medidos realmente en D1; explícitamente no los presenta como medición por modo."""
    p = ENTREGAS / "D1_final" / "INFORME.md"
    if p.is_file():
        texto = p.read_text(encoding="utf-8", errors="replace")
        fps = re.search(r"FPS mediana\s+([\d,.]+).*?minimo\s+([\d,.]+)", texto, re.I | re.S)
        if fps:
            return f"{fps.group(1)} FPS mediana, mínimo {fps.group(2)} (D1 60 min; no por modo)"
    return "[H] sin medición de rendimiento disponible"


def escribir_aceptacion():
    ruta = S5 / "02_ACEPTACION.md"
    cab = ruta.read_text(encoding="utf-8").split("## Estado", 1)[0].rstrip() + "\n\n"
    ok3, d3 = pruebas_x3()
    f = [f"## Estado (generado por `tools\\aceptacion.py`, {date.today().isoformat()}; no editar a mano)",
         "Celdas: SI / NO con su evidencia; `—` = sin medida todavía ([H]). «Pruebas x3» = las 3 últimas pasadas de "
         "`pruebas.py todo` en `D:\\DBZ3HD\\_pruebas` (global: no es por modo); «Fidelidad» = el último `comparar_modo.py` "
         "del modo (todas las filas OK y ninguna [H]).", "",
         "| Etiqueta | Fase | Hito | Fidelidad | Punta a punta | Pruebas x3 | Pantallas | 60 FPS | Se puede quitar |",
         "|---|---|---|---|---|---|---|---|---|"]
    pantallas = evidencia_pantallas()
    fps = evidencia_fps()
    for nombre, fase, modo in ETIQUETAS:
        est, _ = estado_hito(fase)
        fid = fidelidad(modo) if modo else (None, "sin comparar_modo")
        quitar = "SI" if (est == "cerrado" and fid[0] and ok3) else "NO"
        f.append(f"| {nombre} | {fase} | {est} | {celda(*fid)} | {evidencia_punta_a_punta(fase)} | "
                 f"{celda(ok3, d3)} | {pantallas} | {fps} | {quitar} |")
    f += ["", "Las columnas de pantalla y FPS citan medidas globales disponibles y aclaran cuando no son específicas "
          "del modo. «Punta a punta» solo indica SI con evidencia E2E explícita; un pendiente abierto queda como NO."]
    ruta.write_text(cab + "\n".join(f) + "\n", encoding="utf-8", newline="\n")
    return ruta


def escribir_matriz(sin_gh=False):
    f = [f"# Matriz de cierre (generada por `tools\\aceptacion.py`, {date.today().isoformat()}; no editar a mano)",
         "Estado de hito: sin empezar / en curso / cerrado (ver cabecera de `tools\\aceptacion.py`). Un hito solo pasa a "
         "cerrado con `ENTREGAS\\<hito>\\CIERRE.ok` o «Estado: cerrado» en su INFORME.md.", ""]
    resumen = {}

    def fila(*c):
        return "| " + " | ".join(str(x).replace("|", "/") for x in c) + " |"

    f += ["## 1. Peticiones del usuario (01_ROADMAP.md §2)", "", fila("Petición", "Hito", "Dueño", "Estado", "Prueba automática"),
          "|---|---|---|---|---|"]
    for p, h, pr in peticiones():
        e, ev = estado_hito(h)
        resumen.setdefault(e, 0)
        resumen[e] += 1
        f.append(fila(p, h, dueno(h), e, pr))
    pend = pendientes()
    f += ["", "## 2. Pendientes de studies 1-4 (MEJORAS_PENDIENTES.md)", "",
          fila("Id", "Qué falta (fuente)", "Hito", "Dueño", "Estado"), "|---|---|---|---|---|"]
    for pid, que, fuente in pend:
        h = hito_de(que + " " + fuente)
        e, _ = estado_hito(h)
        resumen[e] = resumen.get(e, 0) + 1
        f.append(fila(pid, f"{que} — {fuente}", h, dueno(h), e))
    pasos = pasos_x8()
    f += ["", "## 3. Pasos x.8 (PASOS_X8.md)", "", fila("N", "Id", "Paso", "Hito", "Dueño", "Estado"), "|---|---|---|---|---|---|"]
    for n, i, t, h, _ in pasos:
        e, _ = estado_hito(h)
        resumen[e] = resumen.get(e, 0) + 1
        f.append(fila(n, i, t, h, dueno(h), e))
    iss = issues(sin_gh)
    f += ["", "## 4. Issues abiertos de GitHub (títulos = datos; hito por palabras clave, a confirmar)", ""]
    if iss is None:
        f.append("[H] sin lista de issues: `gh` no respondió y no hay cache (`ENTREGAS\\X0\\issues_cache.json`).")
    else:
        f += [fila("Issue", "Título", "Hito", "Dueño", "Estado"), "|---|---|---|---|---|"]
        for i in iss:
            h = hito_de(i["title"])
            e, _ = estado_hito(h)
            resumen[e] = resumen.get(e, 0) + 1
            f.append(fila(f"#{i['number']}", i["title"], h, dueno(h), e))
    abiertas = sum(v for k, v in resumen.items() if k != "cerrado")
    f += ["", "## Resumen", "", "Filas por estado de su hito: " + ", ".join(f"{k} {v}" for k, v in sorted(resumen.items())),
          f"Filas abiertas (no cerradas): {abiertas}. Terminado = 0 abiertas fuera del backlog 1.6.0 (X3).", ""]
    ruta = ENTREGAS / "MATRIZ_CIERRE.md"
    ruta.write_text("\n".join(f), encoding="utf-8", newline="\n")
    return ruta, len(pend), len(pasos), len(iss or []), abiertas


def _test():
    p = peticiones()
    assert len(p) >= 20 and p[0][1] == "B1-B3" and any(x[1] == "X0g" for x in p), p[:2]
    pe = pendientes()
    assert len(pe) >= 30 and pe[0][0] == "P1", pe[:1]
    pa = pasos_x8()
    assert len(pa) == 24 and pa[0][1] == "X0.8" and pa[11][1] == "X0.8", pa[:2]
    assert hito_de("Saibaman: el port pone 6 en vez de 2") == "A1" and hito_de("vuelo: signo de la palanca") == "B1"
    assert hito_de("Optimize for low laptops") == "D1" and hito_de("nada que ver", "X3") == "X3"
    assert dueno("B1-B3").startswith("B") and dueno("todas") == "coordinador"
    assert estado_hito("X0")[0] in ("en curso", "cerrado") or not (ENTREGAS / "X0").exists()
    assert estado_hito("—")[0] == "n/a"
    print("aceptacion --test: OK (peticiones %d, pendientes %d, pasos x.8 %d)" % (len(p), len(pe), len(pa)))


if __name__ == "__main__":
    if "--test" in sys.argv:
        _test()
        sys.exit(0)
    a = escribir_aceptacion()
    m, npend, npasos, niss, ab = escribir_matriz("--sin-gh" in sys.argv)
    print(f"{a}\n{m}: pendientes {npend}, pasos x.8 {npasos}, issues {niss}, filas abiertas {ab}")
