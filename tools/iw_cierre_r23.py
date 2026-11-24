"""PM-18: proyecta registros IW a una tabla; no ejecuta el arnés ni el juego."""

import argparse
import json
from pathlib import Path
import re
import sys
import tomllib


RAIZ = Path(__file__).resolve().parents[1]
CLAVES = tuple(f"0x{i:X}" for i in range(0x7D0, 0x7E2))  # pruebas.py: _IW_CLAVES
REGIONES = ("eu", "us")
METRICAS = ("tiempo_cs", "puntos", "rango", "cogidos")
RUTA_IW = re.compile(r"(?:studies 5/ENTREGAS/R23/escenarios_iw\d+/|"
                     r"docs/14_frentes/escenarios_pruebas/final/s3-iw-mapa/)\w+\.toml")


def texto(p):
    return p.read_text(encoding="utf-8-sig")


def region(r):
    return "us" if r.lower() == "usa" else r.lower()


def evidencia(p, n):
    return f"`{p.as_posix()}:{n}`"


def serie_json(p, r):
    d = json.loads(texto(p))
    if region(d["region"]) != r:
        raise ValueError(f"{p}: región incompatible con {r}")
    filas = {}
    for i, f in enumerate(d["veredictos"]):
        clave = f"0x{int(f['clave'], 16):X}"
        estado = f["estado"]
        v = f["veredicto"]
        if clave not in CLAVES or clave in filas or v != ("PASS" if estado == "superado" else "FAIL"):
            raise ValueError(f"{p}: fila incompatible o duplicada: {f}")
        if estado in ("superado", "no superado") and any(type(f.get(k)) is not int for k in METRICAS):
            raise ValueError(f"{p}: resultado sin métricas válidas: {clave}")
        datos = ", ".join(f"{k}={f[k]}" for k in METRICAS if k in f)
        filas[clave] = (v if estado != "no iniciada" else "SIN RESULTADO",
                        f"{estado}{'; ' + datos if datos else ''} (`{p.as_posix()}#/veredictos/{i}`)")
    incidencias = [str(d[k]) for k in ("error", "corte") if d[k] is not None]
    completa = not incidencias and len(filas) == len(CLAVES) and all(v == "PASS" for v, _ in filas.values())
    return filas, "; ".join(incidencias) or "sin corte ni error declarado", completa


def salida_consola(p, escenario):
    lineas = list(enumerate(texto(p).splitlines(), 1))
    fin = [(n, m) for n, l in lineas if (m := re.fullmatch(
        rf"VEREDICTO (PASS|FAIL) (\S+)/{re.escape(escenario)}\s+.*", l))]
    if len(fin) != 1:
        raise ValueError(f"{p}: se esperaba un único veredicto para {escenario}")
    n, m = fin[0]
    rutas = [Path(l.split(": ", 1)[1]) for _, l in lineas if l.strip().startswith("evidencia y resultados: ")]
    if len(rutas) != 1 or not rutas[0].is_absolute():
        raise ValueError(f"{p}: falta ruta inequívoca a la corrida")
    motivo = next((l.removeprefix("fallo: ") for _, l in lineas if l.startswith("fallo: ")), "")
    return lineas, m[1], m[2], rutas[0], evidencia(p, n), motivo


def serie_historica(p, r):
    lineas, v, copia, run, ref, motivo = salida_consola(p, "iw_serie")
    registro = run / "escenario.toml"
    d = tomllib.loads(texto(registro))
    # El arnés histórico usaba USA por omisión; reportamos la región solicitada.
    if d["escenario"] != "iw_serie" or region(json.loads(d["parametros_json"]).get("region", "us")) != r:
        raise ValueError(f"{registro}: no corresponde a la serie {r}")
    filas = {}
    for n, l in lineas:
        m = re.search(rf"\] {re.escape(copia)}/iw_serie: serie (\d+)/18: (0x[\dA-Fa-f]+) con resultado$", l)
        if m:
            clave = f"0x{int(m[2], 16):X}"
            i = len(filas)
            if i >= len(CLAVES) or int(m[1]) != i + 1 or clave != CLAVES[i]:
                raise ValueError(f"{p}:{n}: ronda fuera de orden")
            filas[clave] = ("RESULTADO SIN VEREDICTO", evidencia(p, n))
    corte = re.match(r"serie: el minijuego (\d+)/18 \((0x[\dA-Fa-f]+)\) no dio resultado en (\d+) s", motivo)
    if v == "FAIL" and corte and len(filas) < len(CLAVES):
        clave = CLAVES[len(filas)]
        if int(corte[1]) != len(filas) + 1 or int(corte[2], 16) != int(clave, 16):
            raise ValueError(f"{p}: el corte no coincide con la siguiente clave")
        filas[clave] = ("FAIL (tiempo agotado)", f"{corte[3]} s; {ref}")
    return filas, f"{v} global; {motivo or 'sin victoria por clave en la consola'}; {ref}", False


def escenarios(plan, registros):
    casos = {}
    for n, l in enumerate(texto(plan).splitlines(), 1):
        campos = l.split(" | ", 2)
        if len(campos) != 3:
            continue
        carpeta = None
        for parte in re.split(r",|\s+\+\s+", campos[1]):
            parte = parte.strip()
            if RUTA_IW.fullmatch(parte):
                ruta = parte
                carpeta = Path(ruta).parent
            elif carpeta and re.fullmatch(r"\w+\.toml", parte):
                ruta = (carpeta / parte).as_posix()
            else:
                continue
            if ruta in casos:
                raise ValueError(f"{plan}:{n}: escenario IW duplicado")
            m = re.search(r"(?i)(7D[0-9A-F]|7E[01])", Path(ruta).stem)
            casos[ruta] = (f"0x{m[1].upper()}" if m else None, n, campos[2])
    if not casos:
        raise ValueError(f"{plan}: no se encontraron escenarios IW")
    resultados = {}
    for entrada in registros:
        r, nombre, archivo = entrada["region"], entrada["guion"], Path(entrada["consola"])
        if r not in REGIONES or nombre not in casos or (nombre, r) in resultados:
            raise ValueError(f"registro de escenario inválido o duplicado: {entrada}")
        _, v, _, run, ref, motivo = salida_consola(archivo, (RAIZ / nombre).stem)
        log = run / "dbz3.log"
        montajes = []
        with log.open(encoding="utf-8-sig", errors="replace") as f:
            for n, l in enumerate(f, 1):
                if m := re.search(r"ApplyRegionMount: dbz3_region='(eu|us)'", l):
                    montajes.append((m[1], n))
        if {reg for reg, _ in montajes} != {r}:
            raise ValueError(f"{log}: región ejecutada ausente o contradictoria para {r}")
        resultados[nombre, r] = f"{v} {ref}; región {evidencia(log, montajes[0][1])}"
        if motivo:
            resultados[nombre, r] += f"; {motivo}"
    return casos, resultados


def tabla(plan, casos, resultados, series):
    def fila(*celdas):
        return "| " + " | ".join(c.replace("|", "&#124;") for c in celdas) + " |"

    lineas = ["# PM-18 · IW EU/USA", "", "Evidencia de corridas seleccionadas. La tabla no acredita el SHA del EXE final, "
              "la matriz gana/pierde 72/72 ni la campaña 96/96 por región.", ""]
    for r, (_, resumen, completa) in series.items():
        lineas.append(f"- **{r.upper() if r == 'eu' else 'USA'}:** {'18/18 PASS' if completa else '18/18 no acreditado'}; {resumen}")
    lineas += ["", fila("Clave", "EU · serie", "USA · serie", "Escenarios IW (ver abajo)"), fila("---", "---", "---", "---")]
    for clave in CLAVES:
        celdas = [" — ".join(series[r][0].get(clave, ("SIN RESULTADO", "sin fila en la corrida seleccionada"))) for r in REGIONES]
        guiones = ", ".join(Path(p).stem for p, (k, _, _) in casos.items() if k == clave)
        lineas.append(fila(f"`{clave}`", *celdas, guiones or "—"))
    lineas += ["", "## Escenarios de PRUEBAS_FINALES", "", "Cada PASS/FAIL corresponde al escenario indicado.", "",
               fila("Escenario", "Clave", "Criterio en PRUEBAS_FINALES", "EU", "USA"), fila("---", "---", "---", "---", "---")]
    for p, (clave, n, criterio) in casos.items():
        lineas.append(fila(f"`{p}`", clave or "General", f"{criterio} — {evidencia(plan, n)}",
                           *(resultados.get((p, r), "SIN EVIDENCIA") for r in REGIONES)))
    return "\n".join(lineas) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("manifiesto", type=Path)
    ap.add_argument("salida", type=Path, help="Markdown nuevo; no sobreescribe")
    ap.add_argument("--exigir-series", action="store_true", help="código 1 si falta 18/18 PASS en alguna región")
    a = ap.parse_args()
    try:
        d = tomllib.loads(texto(a.manifiesto))
        plan = Path(d["pruebas_finales"])
        series = {}
        for r in REGIONES:
            p = Path(d["serie"][r])
            lector = {".json": serie_json, ".txt": serie_historica}[p.suffix.lower()]
            series[r] = lector(p, r)
        casos, resultados = escenarios(plan, d.get("escenario", []))
        with a.salida.open("x", encoding="utf-8", newline="\n") as f:
            f.write(tabla(plan, casos, resultados, series))
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as e:
        print(f"IW: {e}", file=sys.stderr)
        return 2
    print(f"{len(CLAVES)} claves x 2 regiones; {len(resultados)}/{len(casos) * 2} escenarios con evidencia: {a.salida}")
    return int(a.exigir_series and not all(s[2] for s in series.values()))


if __name__ == "__main__":
    raise SystemExit(main())
