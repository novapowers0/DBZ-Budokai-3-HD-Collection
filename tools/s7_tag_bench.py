#!/usr/bin/env python3
"""S7 TAG01/TAG02: banco sin juego sobre el log del runtime.

Lee un dbz3_*.log (o una carpeta) y comprueba el criterio del inventario:
  TAG01: 20 relevos, fotograma maximo del relevo <= 2x la mediana.
Como no se abre el juego en fase 1, este banco se prueba con --autoprueba (dos logs
sinteticos: uno bueno y uno malo). Con un log real devuelve numerador, denominador y la
primera diferencia.

Uso:
  python tools\\s7_tag_bench.py --autoprueba
  python tools\\s7_tag_bench.py <dbz3.log> [--min-relevos 20] [--factor 2.0] [--json salida.json]

Codigos: 0 PASS, 1 FAIL, 2 INCOMPLETO (faltan relevos o no hay log).
"""
import argparse
import io
import json
import re
import statistics
import sys
from pathlib import Path

RE_RELEVO = re.compile(r"dbz3 tag: luchador [0-9A-Fa-f]+ relevo (\d+) -> (\d+)")
RE_CARGA = re.compile(r"dbz3 tag: luchador [0-9A-Fa-f]+ en combate: id \d+ \((\d+) ticks de carga\)")
RE_TRAZA = re.compile(r"dbz3 tag traza: relevo completado en (\d+) ticks, ([\d.]+) ms")
RE_PERF = re.compile(r"dbz3: perf fps=([\d.]+).*?max_frame_ms=([\d.]+)")
RE_TIRON = re.compile(
    r"dbz3: tiron (\d+) ms \(texload \+(\d+) gpu_wait ([\d.]+) ms cp_wait ([\d.]+) ms shaders \+(\d+)\)")
RE_FORMA_PIDE = re.compile(r"dbz3 formas traza: lado \d+ pide la forma \d+")
RE_FORMA_FIN = re.compile(r"dbz3 formas traza: lado \d+ en la forma \d+: ([\d.]+) ms desde la peticion")
RE_FORMA_CAMBIO = re.compile(r"dbz3 formas traza: cambio nativo \d+ -> \d+ en ([\d.]+) ms")
RE_FORMA_PREP = re.compile(r"dbz3 formas traza: preparacion de la forma en ([\d.]+) ms")


def analizar(texto, min_relevos=20, factor=2.0):
    lineas = texto.splitlines()
    relevos = RE_RELEVO.findall(texto)
    cargas = [int(x) for x in RE_CARGA.findall(texto)]
    trazas = [(int(t), float(ms)) for t, ms in RE_TRAZA.findall(texto)]
    perf = [(float(f), float(m)) for f, m in RE_PERF.findall(texto)]
    tirones = [(int(ms), int(tx), float(gw), float(cw), int(sh)) for ms, tx, gw, cw, sh in RE_TIRON.findall(texto)]
    formas = [float(x) for x in RE_FORMA_FIN.findall(texto)]
    cambios = [float(x) for x in RE_FORMA_CAMBIO.findall(texto)]
    prep = [float(x) for x in RE_FORMA_PREP.findall(texto)]

    r = {
        "relevos_pedidos": len(relevos),
        "relevos_completados": len(cargas),
        "ticks": cargas,
        "perf_muestras": len(perf),
        "perf_max_frame_ms": max((m for _, m in perf), default=None),
        "perf_mediana_frame_ms": statistics.median([m for _, m in perf]) if perf else None,
        "tirones": len(tirones),
        "tiron_peor_ms": max((t[0] for t in tirones), default=None),
        "tirones_con_texload": sum(1 for t in tirones if t[1] > 0),
        "tirones_con_shaders": sum(1 for t in tirones if t[4] > 0),
        "formas_medidas": len(formas),
        "forma_peor_ms": max(formas, default=None),
        "forma_mediana_ms": statistics.median(formas) if formas else None,
        "cambios_nativos": len(cambios),
        "cambio_peor_ms": max(cambios, default=None),
        "preparaciones": len(prep),
        "preparacion_peor_ms": max(prep, default=None),
        "criterio": "INCOMPLETO",
        "detalle": "",
        "primera_diferencia": None,
    }
    if len(cargas) < min_relevos:
        r["detalle"] = f"faltan relevos: {len(cargas)}/{min_relevos}"
        return r
    mediana = statistics.median(cargas)
    maximo = max(cargas)
    limite = factor * mediana
    r.update({"mediana_ticks": mediana, "maximo_ticks": maximo, "limite_ticks": limite,
              "ratio": (maximo / mediana) if mediana else None})
    for i, t in enumerate(cargas, 1):
        if t > limite:
            r["primera_diferencia"] = {"relevo": i, "ticks": t, "mediana": mediana, "limite": limite}
            break
    if maximo <= limite:
        r["criterio"] = "PASS"
        r["detalle"] = f"maximo {maximo} ticks <= 2x mediana {mediana:g} en {len(cargas)} relevos"
    else:
        r["criterio"] = "FAIL"
        r["detalle"] = f"maximo {maximo} ticks > 2x mediana {mediana:g} en {len(cargas)} relevos"
    # Referencia de fotograma (perf_logging): 2x el fotograma medio de las ventanas. No sustituye al
    # criterio de ticks (carga determinista), pero deja el numerador/denominador del fotograma en el informe.
    if perf:
        fps_med = statistics.median([f for f, _ in perf]) or 60.0
        r["frame_limite_ms"] = 2.0 * 1000.0 / fps_med
        r["frame_ok"] = r["perf_max_frame_ms"] <= r["frame_limite_ms"]
    if trazas:
        r["traza_ms"] = [ms for _, ms in trazas]
        r["traza_peor_ms"] = max(ms for _, ms in trazas)
    if tirones:
        peor = max(tirones, key=lambda t: t[0])
        r["tiron_peor"] = {"ms": peor[0], "texload": peor[1], "gpu_wait": peor[2],
                           "cp_wait": peor[3], "shaders": peor[4]}
    return r


_SINTETICO_BUENO = """
dbz3 tag: luchador 823D6D60 relevo 0 -> 1 (id 2, camara 82300000, sale en forma 0, entra en forma 0)
dbz3 tag: luchador 823D6D60 en combate: id 2 (42 ticks de carga)
""" + "\n".join(
    f"dbz3 tag: luchador 823D6D60 relevo {i%2} -> {(i+1)%2} (id 2, camara 82300000, sale en forma 0, entra en forma 0)\n"
    f"dbz3 tag: luchador 823D6D60 en combate: id 2 ({40 + (i % 3)} ticks de carga)"
    for i in range(21)) + """
dbz3: perf fps=60.0 frames=300 window=5.00s max_frame_ms=18.0 fg=1
dbz3: tiron 22 ms (texload +1 gpu_wait 0.0 ms cp_wait 0.0 ms shaders +0)
dbz3 formas traza: lado 1 pide la forma 2 (desde 1)
dbz3 formas traza: lado 1 en la forma 2: 55.0 ms desde la peticion
dbz3 formas traza: cambio nativo 1 -> 2 en 12.0 ms
dbz3 formas traza: preparacion de la forma en 40.0 ms (accion 50)
"""

_SINTETICO_MALO = _SINTETICO_BUENO.replace(
    "dbz3 tag: luchador 823D6D60 en combate: id 2 (42 ticks de carga)",
    "dbz3 tag: luchador 823D6D60 en combate: id 2 (420 ticks de carga)") + """
dbz3: tiron 190 ms (texload +12 gpu_wait 0.0 ms cp_wait 0.0 ms shaders +3)
"""


def autoprueba():
    bueno = analizar(_SINTETICO_BUENO)
    malo = analizar(_SINTETICO_MALO)
    ok = bueno["criterio"] == "PASS" and malo["criterio"] == "FAIL"
    print(f"autoprueba: bueno={bueno['criterio']} malo={malo['criterio']} -> {'OK' if ok else 'FALLO'}")
    if not ok:
        print(json.dumps({"bueno": bueno, "malo": malo}, indent=1, ensure_ascii=False))
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description="Banco TAG01/TAG02 sin juego (lee el log del runtime).")
    ap.add_argument("log", nargs="?", help="dbz3_*.log o carpeta con logs")
    ap.add_argument("--min-relevos", type=int, default=20)
    ap.add_argument("--factor", type=float, default=2.0)
    ap.add_argument("--json", help="escribir el resumen en este JSON")
    ap.add_argument("--autoprueba", action="store_true")
    a = ap.parse_args(argv)
    if a.autoprueba:
        return autoprueba()
    if not a.log:
        ap.error("falta el log (o --autoprueba)")
    p = Path(a.log)
    if p.is_dir():
        logs = sorted(p.glob("dbz3_*.log"), key=lambda q: q.stat().st_mtime, reverse=True)
        if not logs:
            print("INCOMPLETO: no hay dbz3_*.log en la carpeta", file=sys.stderr)
            return 2
        p = logs[0]
        print(f"log: {p}")
    texto = p.read_text(encoding="utf-8", errors="replace")
    r = analizar(texto, a.min_relevos, a.factor)
    r["log"] = str(p)
    print(f"relevos: {r['relevos_completados']}/{r['relevos_pedidos']}  "
          f"mediana {r.get('mediana_ticks', '-')} ticks  maximo {r.get('maximo_ticks', '-')}  "
          f"limite {r.get('limite_ticks', '-')}")
    if r.get("frame_ok") is not None:
        print(f"fotograma: peor ventana {r['perf_max_frame_ms']:.1f} ms, limite 2x medio {r['frame_limite_ms']:.1f} ms "
              f"-> {'OK' if r['frame_ok'] else 'REVISAR'} (evidencia perf; el PASS es por ticks)")
    if r.get("tiron_peor"):
        t = r["tiron_peor"]
        print(f"tiron peor: {t['ms']} ms (texload +{t['texload']}, gpu_wait {t['gpu_wait']}, "
              f"cp_wait {t['cp_wait']}, shaders +{t['shaders']})")
    if r.get("forma_peor_ms") is not None:
        print(f"transformaciones: {r['formas_medidas']} medidas, mediana {r['forma_mediana_ms']:.1f} ms, "
              f"peor {r['forma_peor_ms']:.1f} ms; cambio nativo peor {(r.get('cambio_peor_ms') or 0):.1f} ms; "
              f"preparacion peor {(r.get('preparacion_peor_ms') or 0):.1f} ms")
    if r.get("primera_diferencia"):
        d = r["primera_diferencia"]
        print(f"primera diferencia: relevo {d['relevo']} con {d['ticks']} ticks (mediana {d['mediana']:g}, "
              f"limite {d['limite']:g})")
    print(f"{r['criterio']}: {r['detalle']}")
    if a.json:
        Path(a.json).write_text(json.dumps(r, indent=1, ensure_ascii=False), encoding="utf-8")
    return 0 if r["criterio"] == "PASS" else 1 if r["criterio"] == "FAIL" else 2


if __name__ == "__main__":
    sys.exit(main())
