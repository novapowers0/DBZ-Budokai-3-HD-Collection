"""R6: dos roles, cada uno por el comando autorizado paralelo ... --max 1.

No modifica el arnes compartido. Comprueba contenido identico antes de abrir.
Los dos procesos de pruebas.py conservan sus candados, plazas y cierre propio.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import tomllib

ROOT = Path(__file__).resolve().parents[2]
BASE = Path("D:/DBZ3HD")
SCENARIO = "docs/14_frentes/escenarios_pruebas/final/f10-online"


def manifest(root):
    if not root.is_dir():
        raise SystemExit(f"Falta el directorio de mods: {root}")
    rows = {}
    for file in sorted(root.rglob("*")):
        if file.is_file():
            with file.open("rb") as stream:
                rows[file.relative_to(root).as_posix()] = hashlib.file_digest(stream, "sha256").hexdigest()
    return rows


def align_reserved_ids(out, copies):
    """Solo ampliar reservas de mods ausentes; guardar primero el original en D:."""
    paths = [BASE / c / "mods/roster_ids.toml" for c in copies]
    raw = [p.read_bytes() for p in paths]
    docs = [tomllib.loads(b.decode("utf-8-sig")) for b in raw]
    old, new = (d.get("ids", {}) for d in docs)
    if ({k: v for k, v in docs[0].items() if k != "ids"} !=
            {k: v for k, v in docs[1].items() if k != "ids"} or
            any(new.get(k) != v for k, v in old.items())):
        raise SystemExit("No se iguala roster_ids: hay otros datos o IDs asignados distintos")
    extra = sorted(new.keys() - old.keys())
    if not extra or any((BASE / c / "mods" / k).exists() for k in extra for c in copies):
        raise SystemExit("No se iguala roster_ids: las reservas extra tienen un mod presente")
    backup = Path("D:/_PARA_BORRAR_CLAUDE") / ("solF10_" + out.name)
    backup.mkdir(exist_ok=False)
    shutil.move(str(paths[0]), str(backup / "rb_a_roster_ids.toml"))
    paths[0].write_bytes(raw[1])
    (out / "reservas_igualadas.json").write_text(json.dumps(dict(extra_inactive=extra,
        backup=str(backup), source=str(paths[1]), target=str(paths[0]),
        previous_sha256=hashlib.sha256(raw[0]).hexdigest(),
        current_sha256=hashlib.sha256(raw[1]).hexdigest()), indent=2), encoding="utf-8")


def worker(out, align_roster=False):
    sys.path.insert(0, str(ROOT / "tools"))
    import pruebas as P
    copies = ("rb_a", "rb_b")
    for copy in copies:
        P.comprobar_libre(BASE / copy)
    a, b = (manifest(BASE / c / "mods") for c in copies)
    differences = [k for k in sorted(a.keys() | b.keys()) if a.get(k) != b.get(k)]
    (out / "contenido.json").write_text(json.dumps(dict(files_a=len(a), files_b=len(b),
        equal=not differences, differences=differences), indent=2), encoding="utf-8")
    if differences == ["roster_ids.toml"] and align_roster:
        align_reserved_ids(out, copies)
        a["roster_ids.toml"] = b["roster_ids.toml"]
        differences = []
        (out / "contenido_igualado.json").write_text(json.dumps(dict(files_a=len(a), files_b=len(b),
            equal=True, differences=[]), indent=2), encoding="utf-8")
    if differences:
        print(f"BLOQUEADO: {len(differences)} archivos de mods distintos; ver contenido.json", flush=True)
        return 2
    # Antes de arrancar un lado, esperar las DOS plazas; solo recuperar marcas de PID muerto.
    deadline = time.monotonic() + 3600
    print("Contenido identico. Esperando dos plazas visibles libres.", flush=True)
    while True:
        for n in range(1, P.VENTANAS + 1):
            slot = P.PLAZAS / f"plaza{n}"
            if slot.exists():
                P._recuperar_plaza_huerfana(slot)
        if (sum((P.PLAZAS / f"plaza{n}").exists() for n in range(1, P.VENTANAS + 1)) <= P.VENTANAS - 2
                and not P.PLAZAS.with_name("_compilando").exists()):
            break
        if time.monotonic() >= deadline:
            print("BLOQUEADO: sin dos plazas en 60 minutos; no se abrio ningun juego", flush=True)
            return 3
        time.sleep(5)
    command = [sys.executable, "tools/pruebas.py", "paralelo", SCENARIO, "--max", "1"]
    children, streams = [], []
    for copy, role in zip(copies, ("host", "guest")):
        env = dict(os.environ, DBZ3_AGENTE="solF10", DBZ3_PRUEBAS_COPIAS=copy,
                   DBZ3_PRUEBAS_ENV="DBZ3_NET_AUTO=" + role, PYTHONUNBUFFERED="1",
                   DBZ3_PRUEBAS_EXE=str(ROOT / "out/build/win-amd64-dual/dbz3.exe"))
        stream = (out / (role + ".log")).open("x", encoding="utf-8")
        streams.append(stream)
        child = subprocess.Popen(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT)
        children.append(child)
        print(f"{role}: pruebas.py PID {child.pid}, copia {copy}, --max 1", flush=True)
    (out / "procesos.json").write_text(json.dumps(dict(command=command,
        children=[dict(pid=p.pid, copy=c, role=r) for p, c, r in zip(children, copies, ("host", "guest"))]),
        indent=2), encoding="utf-8")
    codes = [p.wait() for p in children]
    for stream in streams:
        stream.close()
    (out / "resultado.json").write_text(json.dumps(dict(exit_codes=codes), indent=2), encoding="utf-8")
    print(f"Final: codigos {codes}; validar ambos logs antes de aceptar", flush=True)
    return int(any(codes))


def finalize_stopped(out):
    """Conservar evidencia tras cancelar los arneses de una pareja ya fallida."""
    sys.path.insert(0, str(ROOT / "tools"))
    import pruebas as P
    children = json.loads((out / "procesos.json").read_text(encoding="utf-8"))["children"]
    if {(c["copy"], c["role"]) for c in children} != {("rb_a", "host"), ("rb_b", "guest")}:
        raise SystemExit("No es una pareja F10 reconocida")
    if any(P._vivo(c["pid"]) for c in children):
        raise SystemExit("Los arneses siguen vivos; no se limpia su configuracion")
    for c in children:
        P.comprobar_libre(BASE / c["copy"])
    evidence = out / "sesion_interrumpida"
    evidence.mkdir(exist_ok=False)
    for c in children:
        copy = BASE / c["copy"]
        target = evidence / c["role"]
        target.mkdir()
        latest = max((copy / "logs").glob("dbz3_*.log"), key=lambda p: p.stat().st_mtime_ns)
        shutil.copy2(latest, target / "dbz3.log")
        for report in (copy / "informes_online").glob("desincronia_20261110_234553.txt"):
            shutil.copy2(report, target / report.name)
        shutil.copy2(copy / "dbz3_user.toml", target / "dbz3_user_prueba.toml")
        P.escribir_toml(copy / "dbz3_user.toml", "")
    for slot in P.PLAZAS.glob("plaza*"):
        P._recuperar_plaza_huerfana(slot)
    (evidence / "motivo.txt").write_text(
        "FAIL: F10 corto ambos lados por suma distinta en tick 5710, mascara 00100.\n"
        "4081 iguales antes; no se llego a 25 combates ni al cierre 17840.\n"
        "Juegos cerrados mediante WM_CLOSE; arneses cancelados porque log() no observa la muerte del juego.\n",
        encoding="utf-8")
    print(f"Evidencia conservada y configuraciones de pruebas retiradas: {evidence}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--worker", action="store_true")
    ap.add_argument("--align-roster", action="store_true")
    ap.add_argument("--finalize-stopped", action="store_true")
    args = ap.parse_args()
    if args.finalize_stopped:
        return finalize_stopped(args.output)
    if args.worker:
        return worker(args.output, args.align_roster)
    args.output.mkdir(exist_ok=False)
    with (args.output / "captura.log").open("x", encoding="utf-8") as stream:
        child = subprocess.Popen([sys.executable, "-B", str(Path(__file__).resolve()),
            "--output", str(args.output), "--worker"] + (["--align-roster"] if args.align_roster else []),
            cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    print(f"Captura R6 iniciada: PID {child.pid}; resultados {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
