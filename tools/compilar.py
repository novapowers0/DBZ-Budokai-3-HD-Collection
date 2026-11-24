r"""Compila el juego (out/build/win-amd64-dual) de uno en uno cuando varios agentes trabajan a la vez.

  python tools\compilar.py            (espera su turno, compila y deja solo las lineas de error)

Si compila bien, copia el exe dual al release y las DLL canonicas a los dos builds (DBZ3_SIN_COPIA=1 lo evita).
Lo que no se pueda copiar (un dbz3.exe abierto en esa carpeta) se avisa con "AVISO DLL" y se apunta en
coord\pendiente_copia.json: la siguiente llamada lo recopia aunque su build falle. Si ya son iguales, no se copian.

Si falla, la ultima linea dice que archivos fallan, su dueno (studies 5\ENTREGAS\DUENOS_ARCHIVOS.md) y hace
cuanto se tocaron. DBZ3_PAQUETE=<tu id> (p. ej. B2-P02) permite decir "tuyo" o "ajeno". Tiempo en infra\tiempos.jsonl.

Turno: carpeta E:\DBZ3HD_trabajo\coord\compilando (mkdir es atomico); si lleva mas de 20 min, se da por muerta y se
aparta (nunca se borra). Codigo de salida = el de cmake. Si falla en un archivo que no es tuyo, espera y reintenta:
otro agente lo esta editando.

Si la copia se ha movido de carpeta (integracion en E:\DBZ3HD_trabajo\tmp, otra maquina), CMakeCache.txt apunta al proyecto
original y `cmake --build` compilaria el arbol equivocado. En ese caso se aparta el build viejo (nunca se borra) y
se configura uno nuevo en esta raiz.
"""
import contextlib
import filecmp
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

try:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from infra import cronometro
except ImportError:
    def cronometro(*a, **k):
        return contextlib.nullcontext()

R = Path(__file__).resolve().parent.parent
BUILD = R / "out" / "build" / "win-amd64-dual"
TURNO = Path(os.environ.get("DBZ3_COORD", r"E:\DBZ3HD_trabajo\coord")) / "compilando"
PENDIENTE = TURNO.with_name("pendiente_copia.json")
DUENOS = R / "studies 5" / "ENTREGAS" / "DUENOS_ARCHIVOS.md"
CANON = R / "rexglue-sdk-0.10" / "out" / "win-amd64-baseline"
DLLS = ("rexgpu-xenos.dll", "rexruntime.dll", "amd_fidelityfx_dx12.dll")
DESTINOS = ("out\\build\\win-amd64-dual", "out\\build\\win-amd64-release")
RECIENTE_MIN = 20  # tocado hace menos de esto y no es tuyo: probablemente otro agente lo esta editando


def duenos():
    """{ruta sin extension: (dueno, celda entera con el orden en serie)} de la oleada 0+1, sacado de la tabla de DUENOS_ARCHIVOS.md."""
    d = {}
    try:
        for l in DUENOS.read_text(encoding="utf-8", errors="replace").splitlines():
            c = [x.strip() for x in l.strip().strip("|").split("|")]
            if len(c) > 2 and "/" in c[0]:
                d[os.path.splitext(c[0].split()[0])[0].lower()] = (re.split(r"\.\s|\s\(", c[1])[0].strip(), c[1])
    except OSError:
        pass
    return d


def resumen_fallo(salida, ahora=None, paquete=None, tabla=None):
    """Una linea: que archivos fallan, de quien son y hace cuanto se tocaron."""
    ahora = ahora or time.time()
    paquete = paquete if paquete is not None else os.environ.get("DBZ3_PAQUETE", "")
    tabla = duenos() if tabla is None else tabla
    vistos = []
    for l in salida.splitlines():
        m = re.match(r"\s*(.+?\.(?:c|cc|cpp|cxx|h|hpp|inl))[:(]\d+.*\berror\b", l, re.I)
        if m and m.group(1) not in vistos:
            vistos.append(m.group(1))
    if not vistos:
        enlace = next((l.strip() for l in salida.splitlines() if "lld-link: error" in l or "LNK" in l), "")
        return "FALLO en el enlace: " + enlace[:160] if enlace else "FALLO sin archivo reconocido (mira las lineas de arriba)"
    partes, espera = [], False
    for f in vistos[:4]:
        p = Path(f)
        try:
            rel = p.resolve().relative_to(R.resolve()).as_posix()
        except (ValueError, OSError):
            rel = f.replace("\\", "/")
        dueno, celda = tabla.get(os.path.splitext(rel)[0].lower(), ("", ""))
        try:
            mins = (ahora - p.stat().st_mtime) / 60
        except OSError:
            mins = None
        tuyo = bool(paquete) and paquete.lower() in celda.lower()
        quien = ("tuyo" if tuyo else f"ajeno, dueno {dueno}" if dueno and paquete
                 else f"dueno {dueno}" if dueno else "sin dueno en la tabla")
        hace = f", tocado hace {mins:.0f} min" if mins is not None else ""
        if not tuyo and mins is not None and mins < RECIENTE_MIN:
            espera = True
        partes.append(f"{rel} ({quien}{hace})")
    return ("FALLO en " + "; ".join(partes)
            + (" -> si no lo tocaste tu, otro agente lo esta editando: espera unos minutos y reintenta" if espera else ""))


def iguales(destino):
    """True si el exe y las DLL de `destino` ya son las buenas (tamano y fecha): no hace falta copiar."""
    d = R / destino
    pares = [(CANON / n, d / n) for n in DLLS]
    if destino.endswith("release"):
        pares.append((BUILD / "dbz3.exe", d / "dbz3.exe"))
    try:
        return all(filecmp.cmp(a, b) for a, b in pares)
    except OSError:
        return False


def copiar(objetivos):
    """Copia exe/DLL a cada destino; devuelve los que no se pudieron (se reintentan en la siguiente llamada)."""
    fallan = []
    for destino in objetivos:
        if not (R / destino).is_dir():      # VEL-10: carpeta release fantasma (ya no existe): ni se copia ni se avisa
            continue
        if iguales(destino):
            print(f"copy_sdk_dlls {destino}: ya iguales", flush=True)
            continue
        if destino.endswith("release"):
            try:
                shutil.copy2(BUILD / "dbz3.exe", R / destino / "dbz3.exe")
                print("exe dual copiado al release", flush=True)
            except OSError as e:
                print(f"AVISO: no se pudo copiar el exe al release ({e}); hay un juego abierto desde el release", flush=True)
                fallan.append(destino)
                continue
        p = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                            str(R / "tools" / "copy_sdk_dlls.ps1"), "-BuildDir", destino],
                           cwd=R, capture_output=True, text=True, errors="replace")
        if p.returncode == 0:
            print(f"copy_sdk_dlls {destino}: OK", flush=True)
        else:
            print(f"copy_sdk_dlls {destino}: AVISO " + (p.stderr or p.stdout).strip()[:200], flush=True)
            fallan.append(destino)
    try:
        PENDIENTE.write_text(json.dumps(fallan), encoding="utf-8")
    except OSError:
        pass
    if fallan:
        print("AVISO DLL: SIN RECOPIAR en " + ", ".join(fallan) + " (las pruebas desde ahi miden OTRO runtime;"
              " cierra el dbz3.exe de esa carpeta: la proxima llamada a compilar.py lo recopia)", flush=True)
    return fallan


def publicar_exe():
    """VEL-08: copia estable del ultimo exe bueno (E:/DBZ3HD_trabajo/exe_ultimo); pruebas.py la lee sin esperar compilaciones."""
    dst = Path(os.environ.get("DBZ3_EXE_ULTIMO", r"D:\Caches\dbz3_pruebas\exe_ultimo")) / "dbz3.exe"
    try:
        dst.parent.mkdir(parents=True, exist_ok=True)
        tmp = dst.with_name(f"dbz3.exe.{os.getpid()}.tmp")
        shutil.copy2(BUILD / "dbz3.exe", tmp)
        os.replace(tmp, dst)
    except OSError as e:
        print(f"AVISO: no pude publicar el exe estable ({e})", flush=True)


def pendientes():
    try:
        return [d for d in json.loads(PENDIENTE.read_text(encoding="utf-8")) if d in DESTINOS and (R / d).is_dir()]
    except (OSError, ValueError):
        return []


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


def compilar_sdk_si_pedido():
    """DBZ3_COMPILAR_SDK=1: recompila `rexruntime` del SDK baseline dentro del turno.

    La canonica (`rexglue-sdk-0.10/out/win-amd64-baseline`) es la SALIDA del build del
    SDK: por eso un cambio del SDK no llega a las pruebas hasta que se recompila este
    target. `copy_sdk_dlls.ps1` solo copia esa canonica al build del juego. Se compila
    SOLO `rexruntime` (rexgpu-xenos no cambia y se conserva byte a byte), y antes se
    respalda la DLL canonica en `_respaldo_canonicas` (nunca se borra).
    """
    if os.environ.get("DBZ3_COMPILAR_SDK") != "1":
        return 0
    sdk = R / "rexglue-sdk-0.10"
    build = sdk / "out" / "build-win-vulkan-baseline"
    canon = sdk / "out" / "win-amd64-baseline"
    if not (build / "CMakeCache.txt").is_file():
        print(f"SDK: no hay build configurado en {build}", flush=True)
        return 1
    respaldo = (sdk / "out" / "_respaldo_canonicas" /
                (time.strftime("%Y%m%d_%H%M%S") + "_antes_sdk"))
    respaldo.mkdir(parents=True, exist_ok=True)
    for n in ("rexruntime.dll", "rexruntime.lib"):
        if (canon / n).is_file():
            shutil.copy2(canon / n, respaldo / n)
    print(f"SDK: respaldo de la canonica en {respaldo}", flush=True)
    p = subprocess.run(["cmake", "--build", str(build), "--target", "rexruntime"],
                       capture_output=True, text=True, errors="replace")
    lineas = [l for l in (p.stdout + p.stderr).splitlines()
              if "error" in l.lower() or "FAILED" in l]
    texto = "\n".join(lineas[:40]) if lineas else "SDK rexruntime: sin errores"
    print(texto, flush=True)
    print(f"SDK rexruntime: codigo {p.returncode}", flush=True)
    dll = canon / "rexruntime.dll"
    if p.returncode == 0 and dll.is_file():
        print(f"SDK rexruntime: canonica {dll.stat().st_size} bytes", flush=True)
    return p.returncode


def main():
    preparar_build()
    TURNO.parent.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    with cronometro("compilar_espera_turno", detalle=os.environ.get("DBZ3_PAQUETE", "")):   # VEL-10: espera en tiempos.jsonl
        while True:
            try:
                TURNO.mkdir()
                break
            except FileExistsError:
                try:
                    if time.time() - TURNO.stat().st_mtime > 20 * 60:
                        TURNO.rename(TURNO.with_name(f"compilando_viejo_{int(time.time())}"))
                except OSError:
                    pass
                time.sleep(3)
    # Marca ENTREGAS\_compilando para pruebas.py (baja el tope de juegos); si ya la puso a mano otro paso, no se toca.
    marca = R / "studies 5" / "ENTREGAS" / "_compilando"
    puse_marca = False
    try:
        marca.mkdir()
        puse_marca = True
    except OSError:
        pass
    try:
        print(f"turno de compilar tras {time.time() - t0:.0f} s", flush=True)
        if os.environ.get("DBZ3_COMPILAR_SDK") == "1":
            codigo_sdk = compilar_sdk_si_pedido()
            if codigo_sdk:
                return codigo_sdk
        if os.environ.get('DBZ3_COMPILAR_SOLAR_Z') == '1':
            from compilar_solar_z import compilar
            codigo = compilar(R)
            if codigo:
                return codigo
        t1 = time.time()
        with cronometro("compilar", detalle=os.environ.get("DBZ3_PAQUETE", "")):
            r = subprocess.run(["cmake", "--build", str(BUILD)], capture_output=True, text=True, errors="replace")
        salida = r.stdout + r.stderr
        lineas = [l for l in salida.splitlines() if "error" in l.lower() or "FAILED" in l]
        print("\n".join(lineas[:40]) if lineas else "sin errores", flush=True)
        print(f"codigo {r.returncode}", flush=True)
        if not os.environ.get("DBZ3_SIN_COPIA"):
            # El exe del release sale del dual, y el build deja DLLs viejas: sin esto las pruebas miden otro runtime.
            # Si el build falla, solo se recopia lo que quedo pendiente de la vez anterior (el exe dual sigue siendo el bueno).
            copiar(DESTINOS if r.returncode == 0 else pendientes())
        if r.returncode == 0:
            publicar_exe()
        if r.returncode:
            print(resumen_fallo(salida), flush=True)
        print(f"compilar: {time.time() - t1:.0f} s (codigo {r.returncode})", flush=True)
        return r.returncode
    finally:
        try:
            TURNO.rmdir()
        except OSError:
            pass
        if puse_marca:
            try:
                marca.rmdir()
            except OSError:
                pass


def autoprueba():
    import tempfile
    d = Path(tempfile.mkdtemp())
    (d / "a.cpp").write_text("x")
    tabla = {"src/combate": ("B2-P02", "B2-P02 (RESTO-03 y HEROE-04 pasan parche)")}
    rel = (R / "src" / "combate.cpp").as_posix()
    s = f"FAILED: x\n{rel}:838:24: error: use of undeclared identifier\n{d.as_posix()}/a.cpp:3:1: error: x\n"
    out = resumen_fallo(s, paquete="IW-F01", tabla=tabla)
    assert "src/combate.cpp (ajeno, dueno B2-P02" in out and "a.cpp (sin dueno" in out, out
    assert "espera unos minutos" in out, out  # a.cpp se acaba de crear y no es tuyo
    out = resumen_fallo(f"{rel}:1:1: error: x", ahora=time.time() + 3600 * 24 * 365, paquete="RESTO-03", tabla=tabla)
    assert "(tuyo," in out and "espera" not in out, out
    assert resumen_fallo("lld-link: error: undefined symbol: foo").startswith("FALLO en el enlace: lld-link"), "enlace"
    print("compilar autoprueba OK")


def sintaxis(archivo):
    """VEL-10: `compilar.py --sintaxis src/x.cpp`: comprueba que el archivo compila (-fsyntax-only con los flags del
    build), SIN turno y sin tocar el build ni el exe. Codigo 0 = bien; imprime solo las lineas de error."""
    f = Path(archivo).resolve()
    p = subprocess.run(["ninja", "-C", str(BUILD), "-t", "compdb"], capture_output=True, text=True, errors="replace")
    try:
        db = json.loads(p.stdout)
    except ValueError:
        print("sintaxis: no pude leer la base de comandos del build (ninja -t compdb)")
        return 2
    ent = next((e for e in db if (Path(e["directory"]) / e["file"]).resolve() == f), None)
    if not ent:
        print(f"sintaxis: {archivo} no esta en el build")
        return 2
    cmd = re.sub(r'\s-o\s+"?\S+"?', " ", ent["command"]).replace(" -c ", " -fsyntax-only ", 1)
    t1 = time.time()
    r = subprocess.run(cmd, cwd=ent["directory"], capture_output=True, text=True, errors="replace", shell=True)
    err = [l for l in (r.stdout + r.stderr).splitlines() if "error" in l.lower()]
    print(chr(10).join(err[:40]) if err else "sintaxis OK")
    print(f"sintaxis: {time.time() - t1:.0f} s (codigo {r.returncode})")
    return r.returncode


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--sintaxis":
        sys.exit(sintaxis(sys.argv[2]))
    if sys.argv[1:] == ["--autoprueba"]:
        sys.exit(autoprueba())
    sys.exit(main())
