# dbz3 - SLOTS_ESC-01: copias de una alta real para las pruebas de muchos escenarios.
#
# Clona UNA alta real (un mod con stage.toml y sus entradas de data_cmn.afs) N veces y las deja en una
# copia fija de pruebas, para contar en el log "N escenarios nuevos" con 12, 40, 110 y 200.
#
#   python tools\escenarios_copia_n.py preparar --n 12        (crea la copia si falta y la deja con 12 copias)
#   python tools\escenarios_copia_n.py preparar --n 40
#   python tools\escenarios_copia_n.py preparar --n 200
#   python tools\escenarios_copia_n.py estado
#   python tools\escenarios_copia_n.py restaurar              (quita las copias y devuelve los mods apartados)
#
# Las copias se guardan en E:\DBZ3HD_trabajo\escenarios_n\<n>\ (una carpeta de mod por copia) y se
# instalan en <copia>\mods. Los demas escenarios de la copia se APAGAN con el marcador .disabled (no se
# borra nada) para que el recuento sea exacto. Lo que se retira se mueve a E:\DBZ3HD_trabajo\escenarios_n\_retirados.
#
# Numeros: entrada = 5200 + 2*i, menu = entrada + 1 (i = 0..N-1). 200 copias usan 5200..5599, fuera de
# 5000-5199 (escenarios generados de las ISO) y sin chocar entre si.

import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

R = Path(__file__).resolve().parent.parent
TRABAJO = Path(r"E:\DBZ3HD_trabajo\escenarios_n")
COPIA = Path(r"D:\DBZ3HD\esc_n")
DESDE = Path(r"D:\DBZ3HD\smoke_d")
PLANTILLA = R / "out" / "build" / "win-amd64-release" / "mods" / "Templo de Kami"
PREFIJO = "Copia ESC "
BASE_ENTRADA = 5200
TODAS = (12, 40, 110, 200)
RETIRADOS = TRABAJO / "_retirados"  # se recalcula con --trabajo


def _leer_escenario(stage: Path):
    """Primer bloque [[escenario]] del stage.toml de la plantilla (clave = valor)."""
    datos, dentro = {}, False
    for linea in stage.read_text(encoding="utf-8", errors="replace").splitlines():
        s = linea.split("#", 1)[0].strip()
        if not s:
            continue
        if s.startswith("["):
            dentro = s in ("[[escenario]]", "[escenario]")
            continue
        if not dentro or "=" not in s:
            continue
        k, v = s.split("=", 1)
        v = v.strip()
        if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
            v = v[1:-1]
        datos[k.strip()] = v
    if "entrada" not in datos:
        raise SystemExit(f"la plantilla no tiene [[escenario]] con 'entrada': {stage}")
    return datos


def _num(datos, k, defecto):
    try:
        return int(str(datos.get(k, defecto)).strip())
    except ValueError:
        return defecto


def _plantilla(path: Path):
    stage = path / "stage.toml"
    if not stage.is_file():
        raise SystemExit(f"no existe la plantilla del alta: {stage}")
    datos = _leer_escenario(stage)
    entrada = _num(datos, "entrada", -1)
    menu = _num(datos, "menu", entrada + 100)
    id_se = _num(datos, "id_se", 45)
    tema = _num(datos, "tema", 16)
    raiz = path / "us" / "data_cmn.afs"
    geo = raiz / str(entrada) / "geom.bin"
    men = raiz / str(menu) / "geom.bin"
    if not geo.is_file() or not men.is_file():
        raise SystemExit(f"la plantilla no trae las entradas {entrada} y {menu} con geom.bin en {raiz}")
    return {"ruta": path, "entrada": entrada, "menu": menu, "id_se": id_se, "tema": tema,
            "geo": geo, "men": men, "origen": (path / "origen.txt")}


def _carpeta(n: int, i: int) -> str:
    return f"{PREFIJO}{i:04d}"


def _stage_toml(n: int, i: int, entrada: int, menu: int, id_se: int, tema: int, plantilla: Path) -> str:
    return f"""# Copia {i} de {n} de una alta real (SLOTS_ESC-01, tools/escenarios_copia_n.py).
# Generado desde {plantilla.name}; no es contenido del juego: solo sirve para las pruebas de muchos escenarios.
[[escenario]]
nombre = "COPIA {i:04d}"
titulo = "Copia {i:04d}"
entrada = {entrada}    # data_cmn.afs: geometria (entrada nueva)
menu = {menu}       # data_cmn.afs: imagenes de la casilla
id_se = {id_se}       # sonido/ambiente de la plantilla
tema = {tema}          # luz y cielo de la plantilla
"""


def generar(n: int, plantilla: Path, destino: Path) -> Path:
    if n < 1:
        raise SystemExit("N tiene que ser >= 1")
    p = _plantilla(plantilla)
    set_dir = destino / str(n)
    if set_dir.exists():
        # No se borra: el juego generado anterior se aparta a _retirados.
        retirados = destino / "_retirados"
        retirados.mkdir(parents=True, exist_ok=True)
        out = retirados / time.strftime("%Y%m%d_%H%M%S")
        out.mkdir(parents=True, exist_ok=True)
        shutil.move(str(set_dir), str(out / set_dir.name))
    set_dir.mkdir(parents=True)
    for i in range(1, n + 1):
        entrada = BASE_ENTRADA + 2 * (i - 1)
        menu = entrada + 1
        mod = set_dir / _carpeta(n, i)
        (mod / "us" / "data_cmn.afs" / str(entrada)).mkdir(parents=True)
        (mod / "us" / "data_cmn.afs" / str(menu)).mkdir(parents=True)
        shutil.copy2(p["geo"], mod / "us" / "data_cmn.afs" / str(entrada) / "geom.bin")
        shutil.copy2(p["men"], mod / "us" / "data_cmn.afs" / str(menu) / "geom.bin")
        (mod / "stage.toml").write_text(
            _stage_toml(n, i, entrada, menu, p["id_se"], p["tema"], p["ruta"]), encoding="utf-8")
        (mod / "manifest.txt").write_text(
            f"name={_carpeta(n, i)}\ndescription=Copia {i} de {n} del alta {p['ruta'].name} (prueba de muchos escenarios)\n"
            f"type=stage\nversion=1\n", encoding="utf-8")
        if p["origen"].is_file():
            shutil.copy2(p["origen"], mod / "origen.txt")
    manifiesto = {"n": n, "plantilla": str(p["ruta"]), "entrada_base": BASE_ENTRADA,
                  "copias": [{"indice": i, "carpeta": _carpeta(n, i), "registro": 17 + i - 1,
                              "entrada": BASE_ENTRADA + 2 * (i - 1), "menu": BASE_ENTRADA + 2 * (i - 1) + 1}
                             for i in range(1, n + 1)]}
    (set_dir / "manifiesto.json").write_text(json.dumps(manifiesto, indent=2, ensure_ascii=False) + "\n",
                                             encoding="utf-8")
    return set_dir


def _en_uso(copia: Path) -> list:
    """Rutas de dbz3.exe en marcha dentro de la copia (para no tocar archivos de un juego abierto)."""
    try:
        r = subprocess.run(["powershell", "-NoProfile", "-Command",
                            "Get-Process dbz3 -ErrorAction SilentlyContinue | ForEach-Object { $_.Path }"],
                           capture_output=True, text=True, errors="replace", timeout=25)
    except (OSError, subprocess.SubprocessError):
        return []
    pref = str(copia).lower() + "\\"
    return [l.strip() for l in (r.stdout or "").splitlines() if l.strip().lower().startswith(pref)]


def _crear_copia(copia: Path, desde: Path):
    if (copia / "dbz3.exe").is_file():
        return False
    if not (desde / "dbz3.exe").is_file():
        raise SystemExit(f"no existe la copia fija {copia} ni la de origen {desde}")
    print(f"copiando {desde} -> {copia} (sin logs ni shader_cache)...", flush=True)
    copia.parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(["robocopy", str(desde), str(copia), "/E", "/XJ", "/R:1", "/W:1",
                        "/XD", str(desde / "logs"), str(desde / "shader_cache"), "/NDL", "/NJH", "/NJS", "/NP"],
                       capture_output=True, text=True, errors="replace")
    if r.returncode >= 8:
        raise SystemExit(f"robocopy fallo ({r.returncode}): {(r.stdout or r.stderr)[-400:]}")
    return True


def _apartar(destino: Path, trabajo: Path):
    retirados = trabajo / "_retirados"
    retirados.mkdir(parents=True, exist_ok=True)
    marca = time.strftime("%Y%m%d_%H%M%S")
    movidos = 0
    for d in sorted(destino.iterdir()):
        if d.is_dir() and d.name.startswith(PREFIJO):
            out = retirados / marca
            out.mkdir(parents=True, exist_ok=True)
            shutil.move(str(d), str(out / d.name))
            movidos += 1
    return movidos, (retirados / marca if movidos else None)


def instalar(n: int, copia: Path, trabajo: Path, plantilla: Path, desde: Path):
    set_dir = trabajo / str(n)
    if not (set_dir / "manifiesto.json").is_file():
        print(f"generando las {n} copias en {set_dir}...", flush=True)
        set_dir = generar(n, plantilla, trabajo)
    _crear_copia(copia, desde)
    en_uso = _en_uso(copia)
    if en_uso:
        raise SystemExit(f"hay un dbz3.exe abierto en la copia: {en_uso[0]} (cierra el juego y repite)")
    mods = copia / "mods"
    if not mods.is_dir():
        raise SystemExit(f"la copia no tiene carpeta mods: {mods}")
    movidos, donde = _apartar(mods, trabajo)
    copiados = 0
    for d in sorted(set_dir.iterdir()):
        if d.is_dir() and d.name.startswith(PREFIJO):
            shutil.copytree(d, mods / d.name, dirs_exist_ok=False)
            copiados += 1
    marca_propia = "apagado por SLOTS_ESC-01 (recuento exacto de escenarios)"
    apagados, ya = [], []
    for d in sorted(mods.iterdir()):
        if not d.is_dir() or d.name.startswith(PREFIJO) or d.name.startswith("_"):
            continue
        if not (d / "stage.toml").is_file():
            continue
        marca = d / ".disabled"
        if marca.exists():
            # Los que ya apague este paquete en una instalacion anterior tambien son mios (restaurar los devuelve).
            try:
                propio = marca.read_text(encoding="utf-8", errors="replace").startswith("apagado por SLOTS_ESC-01")
            except OSError:
                propio = False
            (apagados if propio else ya).append(d.name)
        else:
            marca.write_text(marca_propia + "\n", encoding="utf-8")
            apagados.append(d.name)
    estado = {"n": n, "copia": str(copia), "fecha": time.strftime("%Y-%m-%d %H:%M:%S"),
              "copias_instaladas": copiados, "apartados_antes": movidos, "retirados_en": str(donde) if donde else "",
              "apagados": apagados, "ya_apagados": ya}
    (trabajo / f"instalado_{copia.name}.json").write_text(
        json.dumps(estado, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"copia fija {copia}: {copiados} copias de alta (N={n}); {movidos} retiradas; "
          f"{len(apagados)} escenarios apagados, {len(ya)} ya estaban apagados", flush=True)
    print(f"  retirados: {donde}" if donde else "  retirados: nada", flush=True)
    print(f"  registro de la ultima: {17 + n - 1}; entrada {BASE_ENTRADA + 2 * (n - 1)}", flush=True)
    print(f"  prueba:  python tools\\pruebas.py correr {copia.name} "
          f"docs\\14_frentes\\escenarios_pruebas\\final\\slots_esc\\selector_desplazamiento_{n}.toml", flush=True)
    return estado


def restaurar(copia: Path, trabajo: Path):
    estado = trabajo / f"instalado_{copia.name}.json"
    if not estado.is_file():
        raise SystemExit(f"no hay registro de instalacion para {copia.name}: {estado}")
    d = json.loads(estado.read_text(encoding="utf-8"))
    mods = copia / "mods"
    en_uso = _en_uso(copia)
    if en_uso:
        raise SystemExit(f"hay un dbz3.exe abierto en la copia: {en_uso[0]} (cierra el juego y repite)")
    movidos, donde = _apartar(mods, trabajo)
    devueltos = 0
    for nombre in d.get("apagados", []):
        marca = mods / nombre / ".disabled"
        if marca.exists():
            marca.unlink()
            devueltos += 1
    print(f"{copia.name}: {movidos} copias retiradas (a {donde}); {devueltos} escenarios devueltos", flush=True)


def estado(trabajo: Path):
    print(f"trabajo: {trabajo}")
    if not trabajo.is_dir():
        print("  (sin generar)")
        return
    for d in sorted(trabajo.iterdir()):
        if d.is_dir() and d.name.isdigit():
            man = d / "manifiesto.json"
            n = json.loads(man.read_text(encoding="utf-8"))["n"] if man.is_file() else "?"
            mods = sum(1 for x in d.iterdir() if x.is_dir() and x.name.startswith(PREFIJO))
            print(f"  N={d.name:<4} copias={mods:<4} manifiesto={'si' if man.is_file() else 'NO'}")
        elif d.is_dir() and d.name == "_retirados":
            print(f"  retirados: {sum(1 for _ in d.rglob('stage.toml'))} copias apartadas")
    for f in sorted(trabajo.glob("instalado_*.json")):
        d = json.loads(f.read_text(encoding="utf-8"))
        print(f"  {d['copia']}: N={d['n']} ({d['fecha']}), {d['copias_instaladas']} copias, "
              f"{len(d.get('apagados', []))} escenarios apagados")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Clona una alta real N veces para las pruebas de escenarios (SLOTS_ESC-01)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    comunes = argparse.ArgumentParser(add_help=False)
    comunes.add_argument("--n", default="12,40,110,200", help="lista de N (por defecto 12,40,110,200)")
    comunes.add_argument("--trabajo", default=str(TRABAJO), help=f"carpeta de trabajo (por defecto {TRABAJO})")
    comunes.add_argument("--copia", default=str(COPIA), help=f"copia fija de pruebas (por defecto {COPIA})")
    comunes.add_argument("--desde", default=str(DESDE), help=f"copia de origen si hay que crearla (por defecto {DESDE})")
    comunes.add_argument("--plantilla", default=str(PLANTILLA), help=f"alta real a clonar (por defecto {PLANTILLA})")
    for nombre in ("generar", "instalar", "preparar"):
        sub.add_parser(nombre, parents=[comunes])
    sub.add_parser("estado", parents=[comunes])
    sub.add_parser("restaurar", parents=[comunes])
    a = ap.parse_args(argv)
    ns = [int(x) for x in str(a.n).split(",") if x.strip()]
    trabajo, copia, plantilla, desde = Path(a.trabajo), Path(a.copia), Path(a.plantilla), Path(a.desde)
    if a.cmd == "estado":
        return estado(trabajo)
    if a.cmd == "restaurar":
        return restaurar(copia, trabajo)
    if a.cmd in ("generar", "preparar"):
        for n in ns:
            generar(n, plantilla, trabajo)
            print(f"generado N={n}: {trabajo / str(n)}", flush=True)
    if a.cmd in ("instalar", "preparar"):
        # Se instala el ultimo N pedido; para cada N hay que volver a instalar (es un recuento distinto).
        for n in ns:
            instalar(n, copia, trabajo, plantilla, desde)


if __name__ == "__main__":
    main()
