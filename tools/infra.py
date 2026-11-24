"""Biblioteca comun minima de las herramientas (solo biblioteca estandar).

Desde tools/:                   import infra
Desde mod center hd/, awo_tools/: sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools")); import infra

  rutas      RAIZ, TRABAJO (env DBZ3_TRABAJO), JUEGOS, PRUEBAS, COORD (env DBZ3_COORD), PAPELERA, tmp(nombre)
  cronometro with infra.cronometro("compilar", "dual"): ...   -> una linea JSON en TRABAJO/infra/tiempos.jsonl
  turno      with infra.turno("compilando", 20*60, 3600): ... -> carpeta-cerrojo en COORD (como tools/compilar.py)
  ejecutar   codigo, cola = infra.ejecutar(["cmake", "--build", d], timeout=900)
  mover_a_papelera(ruta)  copia + verifica + quita el origen (nunca sigue enlaces/junctions)
  u8/u16/u32/f32, be16/be32/bef32(datos, desplazamiento=0)

  python tools/infra.py tiempos [--desde AAAA-MM-DD]
  python tools/infra.py --autoprueba
"""
import contextlib
import json
import math
import os
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
JUEGOS = Path(r"D:\DBZ3HD")                      # = BASE de tools/pruebas.py
PRUEBAS = JUEGOS / "_pruebas"
PAPELERA = Path(r"D:\_PARA_BORRAR_CLAUDE")       # = PLAZAS_HUERFANAS de pruebas.py / destino de compilar.py


def _rutas():
    """TRABAJO y COORD se leen del entorno (la autoprueba los cambia)."""
    global TRABAJO, COORD
    TRABAJO = Path(os.environ.get("DBZ3_TRABAJO", r"E:\DBZ3HD_trabajo"))
    COORD = Path(os.environ.get("DBZ3_COORD") or TRABAJO / "coord")   # compilar.py: E:\DBZ3HD_trabajo\coord


_rutas()


def tmp(nombre):
    """Carpeta temporal nueva dentro de TRABAJO/tmp (nunca en C: ni D:\\tmp)."""
    base = TRABAJO / "tmp"
    base.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=nombre + "_", dir=base))


def _hora():
    return time.strftime("%Y-%m-%d %H:%M:%S")


# --- escritura de una linea de una vez (varios procesos a la vez) -------------------------------------------
def _anadir_linea(ruta, texto):
    """En Windows abre con FILE_APPEND_DATA sola: el sistema hace cada WriteFile atomico al final del archivo
    (open('a') de Python hace seek+write y dos procesos pueden pisarse)."""
    datos = texto.encode("utf-8")
    ruta.parent.mkdir(parents=True, exist_ok=True)
    if os.name != "nt":
        fd = os.open(ruta, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o644)
        try:
            os.write(fd, datos)
        finally:
            os.close(fd)
        return
    import ctypes
    from ctypes import wintypes as wt
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateFileW.restype = wt.HANDLE
    k32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, wt.LPVOID, wt.DWORD, wt.DWORD, wt.HANDLE]
    k32.WriteFile.argtypes = [wt.HANDLE, wt.LPCVOID, wt.DWORD, ctypes.POINTER(wt.DWORD), wt.LPVOID]
    k32.CloseHandle.argtypes = [wt.HANDLE]
    # FILE_APPEND_DATA | SYNCHRONIZE, compartido lectura+escritura+borrado, OPEN_ALWAYS
    h = k32.CreateFileW(str(ruta), 0x0004 | 0x00100000, 0x7, None, 4, 0x80, None)
    if h in (None, wt.HANDLE(-1).value):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        n = wt.DWORD()
        if not k32.WriteFile(h, datos, len(datos), ctypes.byref(n), None) or n.value != len(datos):
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        k32.CloseHandle(h)


@contextlib.contextmanager
def cronometro(flujo, detalle=""):
    """Mide el bloque y anade UNA linea JSON a TRABAJO/infra/tiempos.jsonl. Nunca lanza su propia excepcion
    ni tapa la del bloque (ok=false si el bloque fallo, tambien con SystemExit/Ctrl+C)."""
    t0, ok = time.perf_counter(), False
    try:
        yield
        ok = True
    finally:
        try:
            linea = json.dumps({"flujo": flujo, "detalle": detalle, "seg": round(time.perf_counter() - t0, 3),
                                "ok": ok, "hora": _hora(), "pid": os.getpid()}, ensure_ascii=False)
            _anadir_linea(TRABAJO / "infra" / "tiempos.jsonl", linea + "\n")
        except Exception:
            pass


def tiempos(desde=None, ruta=None):
    """Tabla por flujo: n, mediana, p90, maximo y % de fallos. Devuelve el texto."""
    ruta = ruta or TRABAJO / "infra" / "tiempos.jsonl"
    flujos = {}
    try:
        lineas = Path(ruta).read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        lineas = []
    for l in lineas:
        try:
            d = json.loads(l)
            if desde and d["hora"][:10] < desde:
                continue
            flujos.setdefault(d["flujo"], []).append((float(d["seg"]), bool(d["ok"])))
        except (ValueError, KeyError, TypeError):
            continue                                  # linea rota: no tumba la tabla
    filas = ["%-28s %5s %9s %9s %9s %7s" % ("flujo", "n", "mediana", "p90", "maximo", "fallos")]
    for f, v in sorted(flujos.items()):
        s = sorted(x for x, _ in v)
        p90 = s[max(0, math.ceil(0.9 * len(s)) - 1)]
        fallos = 100 * sum(1 for _, ok in v if not ok) / len(v)
        filas.append("%-28s %5d %9.1f %9.1f %9.1f %6.0f%%" % (f[:28], len(s), statistics.median(s), p90, s[-1], fallos))
    return "\n".join(filas)


# --- turno entre procesos -----------------------------------------------------------------------------------
def _vivo(pid):
    if os.name != "nt":
        try:
            os.kill(pid, 0)
            return True
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
    import ctypes
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = ctypes.c_void_p
    h = k32.OpenProcess(0x1000, False, pid)           # PROCESS_QUERY_LIMITED_INFORMATION (os.kill mataria)
    if not h:
        return ctypes.get_last_error() == 5           # acceso denegado = existe
    try:
        codigo = ctypes.c_ulong()
        return bool(k32.GetExitCodeProcess(ctypes.c_void_p(h), ctypes.byref(codigo))) and codigo.value == 259
    finally:
        k32.CloseHandle(ctypes.c_void_p(h))


def _leer_quien(carpeta):
    try:
        return int((carpeta / "quien.txt").read_text().split()[0])
    except (OSError, ValueError, IndexError):
        return None


@contextlib.contextmanager
def turno(nombre, caduca_seg=20 * 60, espera_seg=3600):
    """Cerrojo COORD/<nombre> (mkdir atomico) con quien.txt = 'pid hora'. Un cerrojo cuyo pid ya no existe o que
    tiene mas de caduca_seg se renombra (atomico: solo un proceso lo gana) y se aparta a PAPELERA; nunca se borra.
    TimeoutError si no hay turno en espera_seg."""
    carpeta = COORD / nombre
    carpeta.parent.mkdir(parents=True, exist_ok=True)
    fin = time.time() + espera_seg
    while True:
        try:
            carpeta.mkdir()
            (carpeta / "quien.txt").write_text("%d %s\n" % (os.getpid(), _hora()))
            break
        except FileExistsError:
            pid = _leer_quien(carpeta)
            try:
                edad = time.time() - carpeta.stat().st_mtime
            except OSError:
                continue                              # se libero justo ahora
            if (pid is not None and not _vivo(pid)) or edad > caduca_seg:
                viejo = carpeta.with_name("%s_viejo_%d_%d" % (nombre, int(time.time()), os.getpid()))
                try:
                    carpeta.rename(viejo)
                    mover_a_papelera(viejo)
                except OSError:
                    pass                              # otro proceso lo aparto antes
                continue
            if time.time() > fin:
                raise TimeoutError("sin turno '%s' en %d s (lo tiene el pid %s)" % (nombre, espera_seg, pid))
            time.sleep(1)
    try:
        yield carpeta
    finally:
        if _leer_quien(carpeta) == os.getpid():       # solo se suelta el propio (si caduco, ya es de otro)
            try:
                (carpeta / "quien.txt").unlink()
                carpeta.rmdir()
            except OSError:
                pass


# --- procesos -----------------------------------------------------------------------------------------------
def ejecutar(cmd, timeout, reintentos=0, cwd=None, lineas=40):
    """subprocess.run con lista de argumentos y tope de tiempo. Devuelve (codigo, ultimas 'lineas' de stdout+stderr).
    Tiempo agotado = codigo -1. Repite hasta 'reintentos' veces mientras el codigo no sea 0."""
    if not isinstance(cmd, (list, tuple)):
        raise TypeError("ejecutar: cmd va como lista de argumentos")
    for _ in range(reintentos + 1):
        try:
            r = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, errors="replace",
                               timeout=timeout, cwd=cwd, stdin=subprocess.DEVNULL)
            codigo, salida = r.returncode, (r.stdout or "") + (r.stderr or "")
        except subprocess.TimeoutExpired as e:
            def _t(x):
                return x.decode("utf-8", "replace") if isinstance(x, bytes) else (x or "")
            codigo, salida = -1, _t(e.stdout) + _t(e.stderr) + "\nTIEMPO AGOTADO (%s s)" % timeout
        if codigo == 0:
            break
    return codigo, "\n".join(salida.splitlines()[-lineas:])


# --- mover sin perder nada ----------------------------------------------------------------------------------
def _es_enlace(ruta):
    """Enlace simbolico o junction (punto de reanalisis): nunca se sigue."""
    st = os.lstat(ruta)
    return os.path.islink(ruta) or bool(getattr(st, "st_file_attributes", 0) & 0x400)


def _censo(ruta):
    """(archivos, bytes) sin seguir enlaces; ValueError si encuentra uno."""
    if _es_enlace(ruta):
        raise ValueError("enlace/junction: %s" % ruta)
    if ruta.is_file():
        return 1, ruta.stat().st_size
    n = b = 0
    for e in os.scandir(ruta):
        sn, sb = _censo(Path(e.path))
        n, b = n + sn, b + sb
    return n, b


def mover_a_papelera(ruta):
    """Mueve 'ruta' a PAPELERA/<nombre>_<fecha>. Mismo volumen: rename. Otro volumen: copia, comprueba numero de
    archivos y bytes, y solo entonces quita el origen. Devuelve el destino."""
    # ponytail: entre volumenes rechaza carpetas con enlaces/junctions (no los recrea); moverlas a mano con robocopy /XJ
    ruta = Path(ruta)
    PAPELERA.mkdir(parents=True, exist_ok=True)
    destino = PAPELERA / ("%s_%s" % (ruta.name, time.strftime("%Y%m%d_%H%M%S")))
    i = 1
    while destino.exists():
        destino = destino.with_name(destino.name.rsplit("~", 1)[0] + "~%d" % i)
        i += 1
    if os.path.splitdrive(ruta.resolve())[0].lower() == os.path.splitdrive(PAPELERA.resolve())[0].lower():
        os.rename(ruta, destino)
        return destino
    antes = _censo(ruta)
    if ruta.is_file():
        shutil.copy2(ruta, destino)
    else:
        shutil.copytree(ruta, destino, symlinks=True)
    despues = _censo(destino)
    if despues != antes:
        raise OSError("copia incompleta %s -> %s: %s != %s (el origen se queda)" % (ruta, destino, antes, despues))
    if ruta.is_file():
        ruta.unlink()
    else:
        shutil.rmtree(ruta)
    return destino


# --- binario ------------------------------------------------------------------------------------------------
def u8(b, o=0): return b[o]
def u16(b, o=0): return struct.unpack_from("<H", b, o)[0]
def u32(b, o=0): return struct.unpack_from("<I", b, o)[0]
def f32(b, o=0): return struct.unpack_from("<f", b, o)[0]
def be16(b, o=0): return struct.unpack_from(">H", b, o)[0]
def be32(b, o=0): return struct.unpack_from(">I", b, o)[0]
def bef32(b, o=0): return struct.unpack_from(">f", b, o)[0]


# --- autoprueba ---------------------------------------------------------------------------------------------
def _autoprueba():
    real = Path(os.environ.get("DBZ3_TRABAJO", r"E:\DBZ3HD_trabajo"))
    zona = real / "infra" / "_autoprueba"
    if zona.exists():
        mover_a_papelera(zona)                        # la de la vez anterior (no se borra)
    zona.mkdir(parents=True)
    os.environ["DBZ3_TRABAJO"] = str(zona / "trabajo")
    os.environ["DBZ3_COORD"] = str(zona / "coord")
    _rutas()
    assert TRABAJO == zona / "trabajo" and COORD == zona / "coord"
    assert (RAIZ / "tools" / "infra.py").is_file() and (RAIZ / "tools" / "pruebas.py").is_file()

    t = tmp("prueba")
    assert t.is_dir() and t.parent == TRABAJO / "tmp" and t.drive.upper() != "C:"

    # cronometro: escribe ok y fallo, no tapa la excepcion
    with cronometro("auto", "bien"):
        time.sleep(0.01)
    try:
        with cronometro("auto", "mal"):
            raise KeyError("x")
    except KeyError:
        pass
    else:
        raise AssertionError("cronometro tapo la excepcion")
    # varios procesos a la vez: 4 x 50 lineas, ninguna rota ni perdida
    codigo = ("import sys; sys.path.insert(0, %r); import infra\n"
              "for i in range(50):\n    with infra.cronometro('par', str(i)): pass\n") % str(Path(__file__).parent)
    ps = [subprocess.Popen([sys.executable, "-c", codigo]) for _ in range(4)]
    assert all(p.wait(60) == 0 for p in ps)
    lineas = [json.loads(l) for l in (TRABAJO / "infra" / "tiempos.jsonl").read_text(encoding="utf-8").splitlines()]
    assert len(lineas) == 202, len(lineas)
    assert [d["ok"] for d in lineas[:2]] == [True, False] and set(lineas[0]) == {"flujo", "detalle", "seg", "ok", "hora", "pid"}
    tabla = tiempos()
    assert "auto" in tabla and "50%" in tabla and "par" in tabla, tabla
    assert "auto" not in tiempos(desde="2999-01-01")

    # turno: exclusivo, caduca por pid muerto y por tiempo, aparta (no borra)
    with turno("t1", 60, 5) as c:
        assert (c / "quien.txt").read_text().split()[0] == str(os.getpid())
        try:
            with turno("t1", 60, 1):
                raise AssertionError("dos turnos a la vez")
        except TimeoutError:
            pass
    assert not (COORD / "t1").exists()
    muerto = subprocess.Popen([sys.executable, "-c", "pass"])
    muerto.wait()
    (COORD / "t2").mkdir()
    (COORD / "t2" / "quien.txt").write_text("%d x\n" % muerto.pid)
    with turno("t2", 600, 5):
        pass
    (COORD / "t3").mkdir()
    (COORD / "t3" / "quien.txt").write_text("%d x\n" % os.getpid())   # vivo pero viejo
    os.utime(COORD / "t3", (time.time() - 100, time.time() - 100))
    with turno("t3", 50, 5):
        pass
    apartados = [p.name for p in PAPELERA.glob("t[23]_viejo_*")]
    assert len([n for n in apartados if n.startswith("t2_")]) >= 1 and len([n for n in apartados if n.startswith("t3_")]) >= 1

    # ejecutar
    c, cola = ejecutar([sys.executable, "-c", "for i in range(100): print(i)"], 30, lineas=3)
    assert c == 0 and cola.split() == ["97", "98", "99"], cola
    c, cola = ejecutar([sys.executable, "-c", "import time; time.sleep(5)"], 0.5)
    assert c == -1 and "TIEMPO AGOTADO" in cola
    marca = zona / "intentos.txt"
    c, _ = ejecutar([sys.executable, "-c", "open(%r,'a').write('x'); raise SystemExit(3)" % str(marca)], 30, reintentos=2)
    assert c == 3 and marca.read_text() == "xxx"

    # mover_a_papelera entre volumenes (E: -> D:) y en el mismo
    origen = zona / "mover"
    (origen / "sub").mkdir(parents=True)
    (origen / "a.bin").write_bytes(b"1" * 1000)
    (origen / "sub" / "b.txt").write_text("hola")
    dest = mover_a_papelera(origen)
    assert not origen.exists() and _censo(dest) == (2, 1004) and dest.parent == PAPELERA
    if os.name == "nt":                               # junction: se rechaza y el origen se queda entero
        con = zona / "con_junction"
        con.mkdir()
        (con / "f.txt").write_text("x")
        (zona / "objetivo").mkdir()
        (zona / "objetivo" / "g.txt").write_text("y")
        r = subprocess.run(["cmd", "/c", "mklink", "/J", str(con / "j"), str(zona / "objetivo")], capture_output=True)
        if r.returncode == 0 and os.path.splitdrive(str(zona))[0].lower() != PAPELERA.drive.lower():
            try:
                mover_a_papelera(con)
                raise AssertionError("siguio una junction")
            except ValueError:
                pass
            assert (con / "f.txt").exists() and (zona / "objetivo" / "g.txt").exists()
        if (con / "j").exists():
            os.rmdir(con / "j")                       # quita solo el enlace (el destino sigue): la zona queda movible

    # binario
    b = struct.pack("<BHIf", 7, 0x1234, 0xDEADBEEF, 1.5) + struct.pack(">HIf", 0x1234, 0xDEADBEEF, -2.0)
    assert (u8(b), u16(b, 1), u32(b, 3), f32(b, 7)) == (7, 0x1234, 0xDEADBEEF, 1.5)
    assert (be16(b, 11), be32(b, 13), bef32(b, 17)) == (0x1234, 0xDEADBEEF, -2.0)
    print(tabla)
    print("autoprueba infra: OK (zona %s; lo apartado esta en %s)" % (zona, PAPELERA))


def main(argv):
    if argv[:1] == ["--autoprueba"]:
        _autoprueba()
        return 0
    if argv[:1] == ["tiempos"]:
        desde = argv[argv.index("--desde") + 1] if "--desde" in argv else None
        print(tiempos(desde))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
