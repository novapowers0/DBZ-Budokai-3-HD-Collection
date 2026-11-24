#!/usr/bin/env python3
"""Comprueba la transición Low → Aim en un log de prueba de Stop Cell (7DE)."""

import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re


INICIO = "dbz3 iwdisparo: juego 0x7DE empieza"
LOW = "dbz3 pulanim disparo voz: LowEnergyWait, evento 7"
SALIDA = re.compile(r"dbz3 iwdisparo: LowEnergyWait -> Aim \((\d+) campos, mira=(\w+)\)")
FIJADA = re.compile(r"dbz3 iwdisparo: F4 semilla piloto 7DE ([\dA-F]{8})\b")
EFECTIVA = re.compile(r"dbz3 iwmips cell t=0 seed_in=([\dA-F]{8})\b")
DISPARO = re.compile(r"dbz3 iwdisparo: Kikoho \((le da|falla)\): .* energia (\d+\.\d+)")
FECHA = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\]")


def auditar(texto, campos=221, mira="respaldo", semilla=None):
    filas = list(enumerate(texto.splitlines(), 1))
    inicios = [n for n, l in filas if INICIO in l]
    if len(inicios) != 1:
        return dict(codigo=2, motivo="Se requiere exactamente un inicio 7DE", inicios=inicios)
    inicio = inicios[0]
    juego = [(n, l) for n, l in filas if n > inicio]
    entradas = [(n, l) for n, l in juego if LOW in l]
    salidas = [(n, l, m) for n, l in juego if (m := SALIDA.search(l))]
    if len(entradas) != 1 or len(salidas) != 1:
        return dict(codigo=2, motivo="Falta una transición Low/Aim inequívoca",
                    entradas=[n for n, _ in entradas], salidas=[n for n, _, _ in salidas])
    n_low, l_low = entradas[0]
    n_aim, l_aim, salida = salidas[0]
    fijadas = [m[1] for n, l in filas if n < inicio and (m := FIJADA.search(l))]
    efectivas = [m[1] for n, l in juego if n < n_low and (m := EFECTIVA.search(l))]
    disparos = [m.groups() for n, l in juego if n < n_low and (m := DISPARO.search(l))]
    tiempos = [datetime.fromisoformat(m[1]) for l in (l_low, l_aim) if (m := FECHA.match(l))]
    segundos = (tiempos[1] - tiempos[0]).total_seconds() if len(tiempos) == 2 else None
    observados = int(salida[1]), salida[2]
    if not inicio < n_low < n_aim or observados != (campos, mira):
        codigo, motivo = 1, "Orden, duración en campos o ruta distintos de lo esperado"
    elif semilla and (len(fijadas) != 1 or len(efectivas) != 1):
        codigo, motivo = 2, "Falta una semilla fijada y efectiva inequívoca"
    elif semilla and (fijadas[0] != semilla or efectivas[0] != semilla):
        codigo, motivo = 1, "La semilla fijada o efectiva no coincide"
    else:
        codigo, motivo = 0, "Transición observada"
    return dict(codigo=codigo, motivo=motivo, linea_inicio=inicio, linea_low=n_low, linea_aim=n_aim,
                campos=observados[0], mira=observados[1], segundos=segundos,
                semillas_fijadas=fijadas, semillas_efectivas=efectivas,
                disparos=len(disparos), impactos=sum(a == "le da" for a, _ in disparos),
                energia_ultimo_disparo=disparos[-1][1] if disparos else None)


def autoprueba():
    """Controles offline de auditar() sobre logs sinteticos de Stop Cell 7DE."""
    ok_log = "\n".join([
        "[2026-11-24 06:54:00.000] dbz3 iwdisparo: F4 semilla piloto 7DE 603F0359",
        "[2026-11-24 06:54:01.000] dbz3 iwdisparo: juego 0x7DE empieza",
        "[2026-11-24 06:54:01.100] dbz3 iwmips cell t=0 seed_in=603F0359",
        "[2026-11-24 06:54:10.000] dbz3 iwdisparo: Kikoho (le da): Cell a 0 de altura, energia 0.10",
        "[2026-11-24 06:54:10.500] dbz3 pulanim disparo voz: LowEnergyWait, evento 7",
        "[2026-11-24 06:54:14.184] dbz3 iwdisparo: LowEnergyWait -> Aim (221 campos, mira=respaldo)",
    ])
    r0 = auditar(ok_log, campos=221, mira="respaldo", semilla="603F0359")
    if r0["codigo"] != 0 or r0["campos"] != 221 or r0["impactos"] != 1:
        raise RuntimeError(f"control positivo iw_7de_log fallo: {r0}")
    if auditar(ok_log.replace("221 campos", "220 campos"), campos=221)["codigo"] != 1:
        raise RuntimeError("control negativo 220 campos no detectado")
    if auditar(ok_log, campos=221, semilla="1536C74D")["codigo"] != 1:
        raise RuntimeError("control negativo semilla distinta no detectado")
    if auditar(ok_log.splitlines()[1], campos=221)["codigo"] != 2:
        raise RuntimeError("control negativo sin Low/Aim no devolvio 2")
    if auditar(ok_log + "\n[2026-11-24 06:55:00.000] dbz3 iwdisparo: juego 0x7DE empieza")["codigo"] != 2:
        raise RuntimeError("control negativo doble inicio no devolvio 2")
    print("iw_7de_log autoprueba: OK (221 campos, semilla y 4 controles negativos)")


def main():
    p = argparse.ArgumentParser(description=__doc__, allow_abbrev=False, epilog=(
        "Salida: 0 coincide, 1 discrepa, 2 faltan datos o uso inválido, 3 error de lectura. "
        "Los segundos son observados; los campos se leen de la telemetría del juego."))
    p.add_argument("--self-check", "--autoprueba", dest="self_check", action="store_true",
                   help="controles offline sobre logs sinteticos sin disco ni juego")
    p.add_argument("log", nargs="?", type=Path, help="dbz3.log archivado de una sola partida")
    p.add_argument("--campos", type=int, default=221, help="campos esperados (por defecto: 221)")
    p.add_argument("--mira", choices=("respaldo", "elf"), default="respaldo")
    p.add_argument("--semilla", type=str.upper, help="exige que la semilla fijada y la efectiva coincidan")
    a = p.parse_args()
    if a.self_check:
        autoprueba()
        return 0
    if a.log is None:
        p.error("the following arguments are required: log")
    if a.campos <= 0:
        p.error("--campos debe ser positivo")
    if a.semilla is not None and not re.fullmatch(r"[0-9A-F]{7}[13579BDF]", a.semilla):
        p.error("--semilla debe tener ocho dígitos hexadecimales y ser impar")
    try:
        bruto = a.log.read_bytes()
        r = auditar(bruto.decode("utf-8-sig", errors="replace"), a.campos, a.mira, a.semilla)
    except (OSError, ValueError) as e:
        r = dict(codigo=3, motivo=str(e))
    else:
        r["sha256"] = hashlib.sha256(bruto).hexdigest()
    print(json.dumps(r, indent=2))
    return r["codigo"]


if __name__ == "__main__":
    raise SystemExit(main())
