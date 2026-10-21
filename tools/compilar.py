r"""Compila el juego (out/build/win-amd64-dual) de uno en uno cuando varios agentes trabajan a la vez.

  python tools\compilar.py            (espera su turno, compila y deja solo las lineas de error)

Turno: carpeta E:\DBZ3HD_trabajo\coord\compilando (mkdir es atomico); si lleva mas de 20 min, se da por muerta y se
aparta (nunca se borra). Codigo de salida = el de cmake. Si falla en un archivo que no es tuyo, espera y reintenta:
otro agente lo esta editando.

Si la copia se ha movido de carpeta (integracion en D:\tmp, otra maquina), CMakeCache.txt apunta al proyecto
original y `cmake --build` compilaria el arbol equivocado. En ese caso se aparta el build viejo (nunca se borra) y
se configura uno nuevo en esta raiz.
"""
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

R = Path(__file__).resolve().parent.parent
BUILD = R / "out" / "build" / "win-amd64-dual"
TURNO = Path(os.environ.get("DBZ3_COORD", r"E:\DBZ3HD_trabajo\coord")) / "compilando"


def preparar_build():
    """Devuelve True si CMake ya apunta a esta raiz; si no, aparta el build y configura."""
    cache = BUILD / "CMakeCache.txt"
    if cache.is_file():
        texto = cache.read_text(encoding="utf-8", errors="replace")
        m = re.search(r"CMAKE_HOME_DIRECTORY:INTERNAL=(.+)", texto)
        if m and Path(m.group(1).strip()).resolve() == R.resolve():
            return
        destino = Path(r"D:\_PARA_BORRAR_CLAUDE") / ("s3_build_otra_raiz_" + time.strftime("%Y%m%d_%H%M%S"))
        destino.parent.mkdir(parents=True, exist_ok=True)
        print("build de otra raiz; se aparta a %s" % destino, flush=True)
        shutil.move(str(BUILD), str(destino))
    BUILD.parent.mkdir(parents=True, exist_ok=True)
    clang = Path(r"C:/Program Files/LLVM/bin/clang.exe")
    clangxx = Path(r"C:/Program Files/LLVM/bin/clang++.exe")
    rc = Path(r"C:/Program Files/LLVM/bin/llvm-rc.exe")
    cmd = ["cmake", "-G", "Ninja", "-S", str(R), "-B", str(BUILD), "-DDBZ3_DUAL_REGION=ON",
           "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=%s" % (R / "rexglue")]
    if clang.is_file():
        cmd += ["-DCMAKE_C_COMPILER=%s" % clang.as_posix(), "-DCMAKE_CXX_COMPILER=%s" % clangxx.as_posix()]
    if rc.is_file():
        cmd += ["-DCMAKE_RC_COMPILER=%s" % rc.as_posix()]
    print("configurando %s" % BUILD, flush=True)
    p = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    if p.returncode != 0:
        print((p.stdout or "") + (p.stderr or ""), flush=True)
        raise SystemExit(p.returncode)


def main():
    preparar_build()
    TURNO.parent.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    while True:
        try:
            TURNO.mkdir()
            break
        except FileExistsError:
            if time.time() - TURNO.stat().st_mtime > 20 * 60:
                try:
                    TURNO.rename(TURNO.with_name(f"compilando_viejo_{int(time.time())}"))
                except OSError:
                    pass
            time.sleep(3)
    try:
        print(f"turno de compilar tras {time.time() - t0:.0f} s", flush=True)
        r = subprocess.run(["cmake", "--build", str(BUILD)], capture_output=True, text=True, errors="replace")
        lineas = [l for l in (r.stdout + r.stderr).splitlines() if "error" in l.lower() or "FAILED" in l]
        print("\n".join(lineas[:40]) if lineas else "sin errores", flush=True)
        print(f"codigo {r.returncode}", flush=True)
        return r.returncode
    finally:
        try:
            TURNO.rmdir()
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
