"""R13/solTACT2: pruebas de variedad ganadora e historial real por rival.

Ejecutar (solo smoke_c, sin --forzar):
  python tools/tactics_r13.py correr variedad
  python tools/tactics_r13.py correr historial
Revisar una evidencia ya obtenida:
  python tools/tactics_r13.py verificar variedad <dbz3.log>
  python tools/tactics_r13.py verificar historial <dbz3.log> --guardado <toml>
Autoprueba del verificador (incluye evidencia incompleta/corrupta): --autoprueba.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tomllib


def exigir(cond, mensaje):
    if not cond:
        raise ValueError(mensaje)


def variedad(texto):
    filas = re.findall(r"tactics audit: torneo (\d+) fin (true|false) ronda (\d+) campeon (-?\d+) principal (\w+) composicion ([\w,]*)", texto)
    exigir(len(filas) == 32, "faltan torneos: se requieren exactamente 32")
    exigir({int(f[0]) for f in filas} == set(range(500000, 500032)), "semillas incompletas o repetidas")
    for semilla, fin, ronda, campeon, principal, comp in filas:
        exigir(fin == "true" and 0 <= int(campeon) < 8 and int(ronda) < 200 and comp,
               "torneo sin campeon/composicion: " + semilla)
        exigir(principal != "ninguna", "campeon sin sinergia activa: " + semilla)
    composiciones = {f[5] for f in filas}
    principales = Counter(f[4] for f in filas)
    exigir(len(composiciones) >= 8 and len(principales) >= 3, "no hay suficientes composiciones/sinergias ganadoras distintas")
    exigir(re.search(r"partida nueva, semilla 2024[^\n]*\(con variedad\)", texto), "el juego presentado no activo variedad")
    exigir(re.search(r"partida acabada en la ronda \d+: campeon \d+, tu puesto \d+ de 8", texto), "no termino el torneo presentado")
    return dict(torneos=len(filas), composiciones=len(composiciones), principales=dict(principales))


def historial(texto, guardado):
    esperado = [0] * 64
    guardadas = {}
    resultados = duelos = neutrales = copias = continuadas = 0
    rivales = set()
    ultima_matriz = None
    ultima_guardada = None
    for linea in texto.splitlines():
        d = re.search(r"tactics audit: duelo ronda (\d+) a (\d+) b (\d+) copia (true|false) ganador (-?\d+)", linea)
        if d:
            ronda, a, b, copia, ganador = d.groups()
            a, b, ganador = int(a), int(b), int(ganador)
            exigir(0 <= a < 8 and 0 <= b <= 8 and ganador in (-1, 0, 1), "duelo fuera de limites")
            if b == 8:
                neutrales += 1
            elif copia == "true":
                copias += 1
            else:
                exigir(a != b, "duelo contra si mismo")
                esperado[a * 8 + b] = 3 if ganador < 0 else 1 if ganador == 0 else 2
                esperado[b * 8 + a] = 3 if ganador < 0 else 1 if ganador == 1 else 2
                duelos += 1
                if a == 0:
                    rivales.add(b)
        h = re.search(r"tactics audit: historial (\w+) ronda (\d+) activo (true|false) huella ([0-9a-f]{16}) matriz ([0-3]+)", linea)
        if not h:
            continue
        momento, ronda, activo, huella, matriz = h.groups()
        exigir(activo == "true" and len(matriz) == 64, "historial apagado o matriz incompleta")
        exigir(list(map(int, matriz)) == esperado, "historial no coincide con los duelos en " + momento + "/" + ronda)
        ultima_matriz = matriz
        if momento == "resultado":
            resultados += 1
        elif momento == "guardada":
            guardadas[(ronda, huella)] = matriz
            ultima_guardada = (int(ronda), int(huella, 16))
        elif momento == "continuada":
            exigir(int(ronda) >= 6 and any(esperado), "se continua un historial vacio o demasiado temprano")
            exigir(guardadas.get((ronda, huella)) == matriz, "Continuar no restaura el ultimo guardado")
            continuadas += 1
    exigir(resultados >= 5 and duelos >= 8 and len(rivales) >= 2 and neutrales > 0 and continuadas > 0,
           "faltan duelos reales, varios rivales, ronda neutral o Continuar despues de combatir")
    exigir(guardado.get("historial") == 1 and guardado.get("variedad") == 1 and guardado.get("ronda", 0) >= 6,
           "el archivo no conserva las reglas y la ronda")
    exigir(guardado.get("estado", [])[-64:] == list(map(int, ultima_matriz)), "la matriz del archivo no coincide con el juego")
    exigir((guardado.get("ronda"), guardado.get("huella", 0) & ((1 << 64)-1)) == ultima_guardada,
           "el archivo no corresponde al ultimo guardado de esta corrida")
    return dict(resultados=resultados, duelos_reales=duelos, rivales_humano=sorted(rivales),
                neutrales=neutrales, copias=copias, continuadas=continuadas, matriz=ultima_matriz)


def autoprueba():
    # Una evidencia vacia nunca acredita ninguna de las dos mejoras.
    for fn in (lambda: variedad(""), lambda: historial("", {})):
        try:
            fn()
        except ValueError:
            pass
        else:
            raise AssertionError("se acepto evidencia vacia")
    filas = [f"tactics audit: torneo {500000+i} fin true ronda 25 campeon 0 principal s{i%3} composicion u{i},v" for i in range(32)]
    texto = "\n".join(filas + ["partida nueva, semilla 2024 (con variedad)", "partida acabada en la ronda 25: campeon 0, tu puesto 1 de 8"])
    assert variedad(texto)["composiciones"] == 32
    for malo in (texto.replace("fin true", "fin false", 1), texto.replace("u1,v", "", 1), "\n".join(filas)):
        try:
            variedad(malo)
        except ValueError:
            pass
        else:
            raise AssertionError("se acepto variedad incompleta")
    matriz, lineas = [0]*64, []
    for r in range(1, 6):
        for a, b in ((0, 1+r%2), (3, 4)):
            lineas.append(f"tactics audit: duelo ronda {r} a {a} b {b} copia false ganador 0")
            matriz[a*8+b], matriz[b*8+a] = 1, 2
        lineas += [f"tactics audit: duelo ronda {r} a 5 b 8 copia false ganador 0",
                   f"tactics audit: duelo ronda {r} a 6 b 7 copia true ganador 1",
                   f"tactics audit: historial resultado ronda {r} activo true huella 0000000000000001 matriz {''.join(map(str, matriz))}"]
    m = ''.join(map(str, matriz))
    lineas += [f"tactics audit: historial {tipo} ronda 6 activo true huella 0000000000000002 matriz {m}" for tipo in ("guardada", "continuada")]
    texto = '\n'.join(lineas)
    g = dict(historial=1, variedad=1, ronda=6, estado=matriz, huella=2)
    assert historial(texto, g)["copias"] == 5
    for malo, guardar in ((texto.replace("ganador 0", "ganador 1", 1), g), (texto, dict(g, estado=[0]*64)), (texto.replace("continuada", "ausente"), g)):
        try:
            historial(malo, guardar)
        except ValueError:
            pass
        else:
            raise AssertionError("se acepto historial corrupto/incompleto")
    print("AUTOPRUEBA tactics_r13 OK")


def main():
    if sys.argv[1:] == ["--autoprueba"]:
        autoprueba()
        return 0
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("accion", choices=("correr", "verificar"))
    ap.add_argument("tipo", choices=("variedad", "historial"))
    ap.add_argument("log", nargs="?", type=Path)
    ap.add_argument("--guardado", type=Path)
    a = ap.parse_args()
    if a.accion == "correr":
        raiz = Path(__file__).resolve().parents[1]
        cmd = [sys.executable, str(raiz/"tools/pruebas.py"), "correr", "smoke_c",
               "tactics_variedad" if a.tipo == "variedad" else "tactics_carrusel", "--env",
               "DBZ3_TEST_CFG=dbz3_tactics_carrusel=true;dbz3_tactics_bots_listos=true;dbz3_tactics_variedad=true;dbz3_tactics_historial=true",
               "--env", "DBZ3_TEST_TACTICS_AUDITAR=1"]
        res = subprocess.run(cmd, cwd=raiz, capture_output=True, text=True, encoding="utf-8", errors="replace")
        print(res.stdout, end="")
        print(res.stderr, end="", file=sys.stderr)
        exigir(res.returncode == 0, "fallo el escenario de juego")
        rutas = re.findall(r"evidencia y resultados: (.+)", res.stdout)
        exigir(rutas, "no se devolvio la carpeta de evidencia")
        a.log = Path(rutas[-1].strip())/"dbz3.log"
        a.guardado = Path(r"D:\DBZ3HD\smoke_c\user_data\dbz3\hub_tactics_prueba.toml")
    exigir(a.log is not None, "falta dbz3.log")
    texto = a.log.read_text(encoding="utf-8", errors="replace")
    if a.tipo == "variedad":
        resultado = variedad(texto)
    else:
        exigir(a.guardado is not None, "falta --guardado")
        resultado = historial(texto, tomllib.loads(a.guardado.read_text(encoding="utf-8")))
        if a.accion == "correr":
            shutil.copy2(a.guardado, a.log.parent/"historial_guardado.toml")
    if a.accion == "correr":
        (a.log.parent/"tactics_r13_resultado.json").write_text(json.dumps(resultado, indent=2), encoding="utf-8")
    print("PASS " + a.tipo + ": " + json.dumps(resultado, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError) as exc:
        print("FALLO: " + str(exc), file=sys.stderr)
        sys.exit(1)
