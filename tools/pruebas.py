r"""Pruebas del juego en copias aisladas (D:\DBZ3HD\<copia>). Tu partida real no se toca.

Comandos (desde la carpeta del proyecto):
  python tools\pruebas.py prep du_test
  python tools\pruebas.py instantanea guardar du_test NOMBRE
  python tools\pruebas.py instantanea cargar du_test NOMBRE
  python tools\pruebas.py correr du_test menu
  python tools\pruebas.py correr du_test du_freezer_nuevo --par up=2 --par iter=20
  python tools\pruebas.py correr tag_test tag_swap --env DBZ3_TAG_SLOW=2
  python tools\pruebas.py correr du_test mi_prueba.toml --instantanea NOMBRE
  python tools\pruebas.py paralelo menu@du_test tag_swap@tag_test   (hasta 3 a la vez)
  python tools\pruebas.py todo                      (bateria rapida en smoke_a/b/c, tabla PASS/FAIL)
  python tools\pruebas.py todo --solo menu,batalla --max 2
  python tools\pruebas.py biblioteca                (crea las instantaneas base y perfil_nuevo)

Escenarios: menu, batalla, campana, du_freezer_nuevo, du_continuar, tag_swap, o un .toml con:
  descripcion = "..."
  env = ["DBZ3_TAG_SLOW=3"]
  pasos = ["tecla Space 4000", "esperar_pantalla 5,7 30", "captura rueda", 'log "texto" 20']
Pasos: tecla TECLA [ms] (TECLA:400 = mantener 400 ms) | esperar ms | esperar_pantalla N[,M] [seg]
       captura NOMBRE | dump | write | log "regex" [seg]
Nunca borra: lo que reemplaza se mueve a D:\DBZ3HD\_instantaneas\_respaldo\.
Nunca cierra juegos ajenos: si una copia tiene un juego abierto, se para y avisa.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import glob
import hashlib
import json
import os
import queue
import re
import shlex
import shutil
import struct
import subprocess
import sys
import time
import threading
import tomllib
import zlib
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # sin Pillow la captura se guarda como .bmp
    Image = None

R = Path(__file__).resolve().parent.parent          # carpeta del proyecto
BASE = Path(r"D:\DBZ3HD")                           # donde viven las copias
INST = BASE / "_instantaneas"
RESPALDO = INST / "_respaldo"
PRUEBAS = BASE / "_pruebas"                         # capturas y resultados de cada prueba
EXE = R / "out" / "build" / "win-amd64-dual" / "dbz3.exe"
DLLS = R / "rexglue-sdk-0.10" / "out" / "win-amd64-baseline"
GBASE = 0x200000000
PANTALLA = GBASE + 0x824BA110                       # byte con el id de pantalla (4 titulo, 5 menu, 7 rueda...)
PANTALLA_EU = GBASE + 0x824BA100                    # I03: el dual-region desplaza el byte 0x10 en el xex EU
ESTRUCTAS = (0x8247A8E0, 0x824BA110)                # bloques de DU (antes en du_mem.py)
BLOQUE, RANURA = 4624, 22
MAX_PARALELO = 3                                   # juegos a la vez con ventana (N2)
MAX_PARALELO_N1 = 6                                 # X0g/X0h: solo con DBZ3_TEST_OCULTO=1; el tope real son las 4 plazas
PLAZAS = R / "studies 5" / "ENTREGAS" / "_juego"      # X0h: plaza1..4 (mkdir atomico) = tope de 4 dbz3.exe a la vez
PLAZAS_HUERFANAS = Path(r"D:\_PARA_BORRAR_CLAUDE")
TOPE_JUEGOS = 3   # H5 24/10 (790 juegos en un dia): plaza1-2 con ventana, plaza3 oculta; con _compilando baja a 1
LANZANDO = BASE / "_lanzando"                       # candado entre procesos: contar juegos + lanzar es atomico
ERRORES = (r"access violation|UNHANDLED EXCEPTION|physical write fault"   # main.cpp:976, xmemory.cpp:547/628
           r"|std::terminate called")                                        # main.cpp (set_terminate)
PAPELERA = Path(r"D:\_PARA_BORRAR_CLAUDE") / "pruebas_respaldos"   # respaldos automaticos viejos (no se borran)
MAX_RESPALDOS = 5                                   # por copia: los 5 mas recientes se quedan en _respaldo
VERBOS = {"tecla", "esperar", "esperar_pantalla", "esperar_pelea", "captura", "dump", "write", "log"}

# Ajustes de la copia de prueba (sobrescriben dbz3_user.toml; el resto se conserva)
OVERRIDES = {
    "dbz3_skip_launcher": "true", "audio_mute": "true", "dbz3_mute": "true",
    "mnk_background_input": "true", "mnk_mode": "true", "dbz3_dim_unfocused": "false",
    "dbz3_update_check": "false", "fullscreen": "false", "dbz3_fullscreen_mode": '"windowed"',
    "dbz3_resolution_scale": "1", "dbz3_tag_team_enabled": "true",
    "dbz3_texture_packs_on": "false",               # el pack HD no se prueba aqui y alarga la carga
}

K32 = ctypes.WinDLL("kernel32", use_last_error=True)
K32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
K32.OpenProcess.restype = ctypes.c_void_p
K32.WaitForSingleObject.argtypes = [ctypes.c_void_p, wt.DWORD]
K32.WaitForSingleObject.restype = wt.DWORD
K32.CloseHandle.argtypes = [ctypes.c_void_p]
K32.GetProcessTimes.argtypes = [ctypes.c_void_p, ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME),
                                ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME)]
K32.QueryFullProcessImageNameW.argtypes = [ctypes.c_void_p, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)]
K32.QueryFullProcessImageNameW.restype = wt.BOOL


U32 = ctypes.WinDLL("user32", use_last_error=True)
_ENUM = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def _ocultar_ventanas(proc):
    """X0g: oculta (SW_HIDE) las ventanas del juego mientras viva; el juego sigue pintando (sin Present, sin audio)."""
    def cb(h, _):
        pid = wt.DWORD()
        U32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == proc.pid and U32.IsWindowVisible(h):
            U32.ShowWindow(h, 0)
        return True
    fn = _ENUM(cb)
    while proc.poll() is None:
        U32.EnumWindows(fn, 0)
        time.sleep(0.5)


class _RECT(ctypes.Structure):
    _fields_ = [("left", wt.LONG), ("top", wt.LONG), ("right", wt.LONG), ("bottom", wt.LONG)]


# DS164: entrada REAL (WM_KEYDOWN / mouse_event) para la camara libre. Las teclas inyectadas por
# dbz3_input.req llegan al mando emulado, pero no a ImGui (la camara libre lee WASD/QE/raton por ImGui):
# para probar el camino del jugador hay que mandar eventos de ventana de verdad.
U32.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(_RECT)]
U32.GetClientRect.restype = wt.BOOL
U32.ClientToScreen.argtypes = [wt.HWND, ctypes.POINTER(wt.POINT)]
U32.ClientToScreen.restype = wt.BOOL
U32.SetForegroundWindow.argtypes = [wt.HWND]
U32.SetForegroundWindow.restype = wt.BOOL
U32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
U32.SetCursorPos.restype = wt.BOOL
U32.mouse_event.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint, ctypes.c_size_t]
U32.mouse_event.restype = None
U32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
U32.PostMessageW.restype = wt.BOOL

_VK = {"Return": 0x0D, "Space": 0x20, "Escape": 0x1B, "Tab": 0x09, "Backspace": 0x08,
       "Up": 0x26, "Down": 0x28, "Left": 0x25, "Right": 0x27, "PageUp": 0x21, "PageDown": 0x22,
       "A": 0x41, "B": 0x42, "C": 0x43, "D": 0x44, "E": 0x45, "F": 0x46, "Q": 0x51, "R": 0x52,
       "S": 0x53, "W": 0x57, "Z": 0x5A}


def _hwnd_juego(pid):
    """Ventana grande y visible del proceso (la del juego), o 0."""
    hits = []

    def cb(h, _):
        p = wt.DWORD()
        U32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and U32.IsWindowVisible(h):
            r = _RECT()
            U32.GetClientRect(h, ctypes.byref(r))
            if r.right - r.left > 320 and r.bottom - r.top > 240:
                hits.append(((r.right - r.left) * (r.bottom - r.top), h))
        return True

    fn = _ENUM(cb)
    U32.EnumWindows(fn, 0)
    return max(hits)[1] if hits else 0


class _MONINFO(ctypes.Structure):
    _fields_ = [("cbSize", wt.DWORD), ("rcMonitor", _RECT), ("rcWork", _RECT), ("dwFlags", wt.DWORD)]


def _pantalla_secundaria():
    """Zona de trabajo (left, top, right, bottom) del primer monitor que NO es el principal, o None."""
    zonas = []
    fn_t = ctypes.WINFUNCTYPE(wt.BOOL, wt.HMONITOR, wt.HDC, ctypes.POINTER(_RECT), wt.LPARAM)

    def cb(hm, _hdc, _r, _l):
        mi = _MONINFO()
        mi.cbSize = ctypes.sizeof(_MONINFO)
        if U32.GetMonitorInfoW(hm, ctypes.byref(mi)) and not (mi.dwFlags & 1):   # 1 = MONITORINFOF_PRIMARY
            r = mi.rcWork
            zonas.append((r.left, r.top, r.right, r.bottom))
        return True
    U32.EnumDisplayMonitors(None, None, fn_t(cb), 0)
    return zonas[0] if zonas else None


def _a_pantalla_secundaria(proc, n=0):
    """27/10 (peticion del usuario): las ventanas de prueba van a la pantalla secundaria (la de la izquierda), sin
    robar el foco, para no molestar en la que trabaja. n escalona varias ventanas. Sin segunda pantalla no hace nada."""
    zona = _pantalla_secundaria()
    if not zona:
        return
    t = time.time()
    while proc.poll() is None and time.time() - t < 90:
        h = _hwnd_juego(proc.pid)
        if h:
            U32.SetWindowPos(h, None, zona[0] + 40 * n, zona[1] + 30 * n, 0, 0, 0x0001 | 0x0004 | 0x0010)  # NOSIZE|NOZORDER|NOACTIVATE
            return
        time.sleep(0.5)


class Fallo(Exception):
    pass


def ts():
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def _hash_archivo(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1024 * 1024), b""):
            h.update(b)
    return h.hexdigest()


_ARBOLES_HASH = {}


def _huella_arbol(path):
    """Hash del contenido completo; memoizado por mtimes para no releer mods entre escenarios de una misma tanda."""
    root = Path(path)
    if not root.is_dir():
        return "sin-carpeta"
    archivos = []
    firma = []
    for f in sorted(p for p in root.rglob("*") if p.is_file()):
        st = f.stat()
        rel = f.relative_to(root).as_posix()
        archivos.append((f, rel))
        firma.append((rel, st.st_size, st.st_mtime_ns))
    memo = (str(root.resolve()), tuple(firma))
    if memo in _ARBOLES_HASH:
        return _ARBOLES_HASH[memo]
    h = hashlib.sha256()
    for f, rel in archivos:
        h.update(rel.encode("utf-8", "surrogateescape") + b"\0")
        h.update(bytes.fromhex(_hash_archivo(f)))
    result = h.hexdigest()
    _ARBOLES_HASH[memo] = result
    return result


def _cache_key(dest, escenario, par, env, instantanea):
    """Clave por binarios reales, escenario/configuración, instantánea y estado de mods."""
    archivos = [Path(dest) / "dbz3.exe"]
    archivos += [Path(dest) / n for n in ("rexruntime.dll", "rexgpu-xenos.dll", "amd_fidelityfx_dx12.dll")]
    binarios = {p.name: _hash_archivo(p) for p in archivos if p.is_file()}
    ruta = Path(escenario)
    escenario_hash = _hash_archivo(ruta) if ruta.is_file() else "integrado:" + str(escenario)
    snap = INST / instantanea / "user_data" if instantanea else Path(dest) / "user_data"
    datos = {
        "binarios": binarios,
        "arnes_sha256": _hash_archivo(Path(__file__).resolve()),
        "escenario": str(escenario),
        "escenario_sha256": escenario_hash,
        "parametros": par or {},
        "entorno": dict(sorted(env.items())),
        "test_cfg": os.environ.get("DBZ3_TEST_CFG", ""),
        "instantanea": instantanea or "estado-copia",
        "user_data": _huella_arbol(snap),
        "mods": _huella_arbol(Path(dest) / "mods"),
    }
    return hashlib.sha256(json.dumps(datos, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()


def _archivo_cache(clave):
    return PRUEBAS / "_cache" / (clave + ".json")


def _leer_cache(clave):
    p = _archivo_cache(clave)
    try:
        d = json.loads(p.read_text(encoding="utf-8"))
        run = Path(d["run"])
        pngs = [p for p in run.glob("*.png") if p.name != "zz_sin_captura.png"]
        if (d.get("ok") is True and (run / "dbz3.log").is_file() and (run / "escenario.toml").is_file()
                and pngs):
            return d
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return None


def _guardar_cache(clave, resultado, turbo_solicitado, turbo_real):
    if not resultado.get("ok"):
        return
    p = _archivo_cache(clave)
    p.parent.mkdir(parents=True, exist_ok=True)
    d = {"ok": True, "hash": clave, "run": str(resultado["run"]), "dur": resultado.get("dur", 0),
         "menu": resultado.get("menu"), "t_escena": resultado.get("t_escena"), "t_fin": resultado.get("t_fin"),
         "turbo_solicitado": turbo_solicitado, "turbo_real": turbo_real, "guardado": datetime.now().isoformat()}
    tmp = p.with_name(p.name + f".{os.getpid()}.{threading.get_ident()}.tmp")
    tmp.write_text(json.dumps(d, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, p)


def _escenario_toml(run, escenario, desc, par, env, instantanea):
    src = Path(escenario)
    run = Path(run)
    run.mkdir(parents=True, exist_ok=True)
    dst = run / "escenario.toml"
    if src.is_file():
        shutil.copy2(src, dst)
        (Path(run) / "ejecucion.json").write_text(json.dumps(
            {"descripcion": desc, "parametros": par or {}, "entorno": env, "instantanea": instantanea},
            ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    else:
        # Es un TOML de auditoría (la familia integrada no tiene pasos DSL serializables).
        lineas = ["# Registro reproducible de la prueba integrada", f"escenario = {json.dumps(str(escenario), ensure_ascii=False)}",
                  f"descripcion = {json.dumps(str(desc), ensure_ascii=False)}",
                  f"parametros_json = {json.dumps(json.dumps(par or {}, ensure_ascii=False, sort_keys=True), ensure_ascii=False)}",
                  f"entorno_json = {json.dumps(json.dumps(env, ensure_ascii=False, sort_keys=True), ensure_ascii=False)}",
                  f"instantanea = {json.dumps(instantanea or '', ensure_ascii=False)}"]
        dst.write_text("\n".join(lineas) + "\n", encoding="utf-8")
    return dst


def _png_chunk(tipo, data):
    return struct.pack(">I", len(data)) + tipo + data + struct.pack(">I", zlib.crc32(tipo + data) & 0xFFFFFFFF)


def _png_sin_captura(path, motivo):
    """PNG de aviso claro cuando el proceso ya murió antes de permitir una captura real."""
    path = Path(path)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) +
                     _png_chunk(b"tEXt", b"Description\0SIN CAPTURA: " + motivo.encode("utf-8", "replace")) +
                     _png_chunk(b"IDAT", zlib.compress(b"\0\x50\x50\x50")) + _png_chunk(b"IEND", b""))
    path.with_suffix(".txt").write_text("No se pudo capturar una pantalla del juego. Motivo: " + motivo + "\n", encoding="utf-8")


def _hay_png_real(run):
    return any(p.suffix.lower() == ".png" and p.name != "zz_sin_captura.png" for p in Path(run).glob("*.png"))


def _evidencia_png(j, exito, motivo):
    if _hay_png_real(j.run):
        return True
    if j.vivo():
        try:
            j.captura("zz_final" if exito else "zz_fallo")
        except Exception:
            pass
    if _hay_png_real(j.run):
        return True
    _png_sin_captura(j.run / "zz_sin_captura.png", motivo or "no hubo respuesta del juego")
    return False


def _kv(x):
    k, sep, v = x.partition("=")
    if not sep:
        raise SystemExit(f"se esperaba CLAVE=VALOR, recibido: {x}")
    return k.strip(), v


# La arena de memoria del juego no siempre cae en la misma direccion (log: "Guest memory arena mapped: virtual base
# 0x...", antes siempre 0x200000000 = GBASE). DESPL = base real - GBASE; lo fija Juego.base_real() al arrancar.
DESPL = 0


def leer(h, addr, n):
    if not h:
        return None
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    if not K32.ReadProcessMemory(ctypes.c_void_p(h), ctypes.c_void_p(addr + DESPL), buf, n, ctypes.byref(got)):
        return None
    return buf.raw[:got.value]


def cerrar_pid(pid, espera=10):
    """Cierre amable (como CloseMainWindow); si no sale en 'espera' s, se mata."""
    subprocess.run(["taskkill", "/PID", str(pid)], capture_output=True)
    h = K32.OpenProcess(0x00100000, False, pid)     # SYNCHRONIZE
    if not h:
        return
    try:
        if K32.WaitForSingleObject(ctypes.c_void_p(h), int(espera * 1000)) == 0x102:   # WAIT_TIMEOUT
            subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
            K32.WaitForSingleObject(ctypes.c_void_p(h), 5000)
    finally:
        K32.CloseHandle(ctypes.c_void_p(h))


def pids_de_copia(dest):
    """Solo los dbz3.exe que corren desde esa copia (nunca los de otra copia)."""
    ps = ("Get-CimInstance Win32_Process -Filter \"Name='dbz3.exe'\" | "
          "ForEach-Object { '{0}|{1}' -f $_.ProcessId, $_.ExecutablePath }")
    # errors="replace" obligatorio: con -X utf8 (modo del tablero) la salida del hijo se decodifica como UTF-8 y
    # cualquier acento de una consola en espanol mataba el hilo lector -> stdout None.
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True,
                         errors="replace").stdout or ""
    pref = str(dest).rstrip("\\").lower() + "\\"
    pids = []
    for linea in out.splitlines():
        pid, sep, ruta = linea.partition("|")
        if sep and ruta.strip().lower().startswith(pref):
            pids.append(int(pid))
    return pids


def _vivo(pid, marca_tiempo=None):
    h = K32.OpenProcess(0x1000 | 0x0400, False, pid)            # QUERY_LIMITED_INFORMATION | QUERY_INFORMATION
    if not h:
        return False
    try:
        code = wt.DWORD()
        if not K32.GetExitCodeProcess(h, ctypes.byref(code)) or code.value != 259:
            return False
        nombre = ctypes.create_unicode_buffer(32768)
        largo = wt.DWORD(len(nombre))
        if not K32.QueryFullProcessImageNameW(h, 0, nombre, ctypes.byref(largo)):
            return False
        if Path(nombre.value).stem.lower() not in ("python", "pythonw"):
            return False  # PID reciclado por otro proceso (p. ej. SearchFilterHost) != dueño de la reserva
        if marca_tiempo:
            m = re.search(r"\d{8}-\d{6}", marca_tiempo)
            if m:
                creada = wt.FILETIME()
                salida, kernel, user = wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
                if K32.GetProcessTimes(h, ctypes.byref(creada), ctypes.byref(salida), ctypes.byref(kernel), ctypes.byref(user)):
                    ft = (creada.dwHighDateTime << 32) | creada.dwLowDateTime
                    inicio = ft / 10_000_000 - 11_644_473_600
                    if inicio > datetime.strptime(m.group(), "%Y%m%d-%H%M%S").timestamp() + 2:
                        return False  # el proceso Python apareció después de la plaza: el PID ya fue reutilizado
        return True
    finally:
        K32.CloseHandle(h)


@contextmanager
def copia_en_uso(dest):
    """Una prueba por copia a la vez, de principio a fin. comprobar_libre mira antes de la cola: dos agentes con la
    misma copia esperaban turno y abrian los dos (teclas y ficheros pisados, tag_swap en FAIL, 2026-10-15)."""
    lock, t0 = BASE / f"_en_uso_{dest.name}", time.time()
    while True:
        try:
            lock.mkdir()
            (lock / "pid").write_text(f"{os.getpid()} {ts()}")
            break
        except FileExistsError:
            try:
                owner = (lock / "pid").read_text(encoding="utf-8").split()
                pid = int(owner[0])
                marca = owner[1] if len(owner) > 1 else ""
            except (OSError, ValueError):
                pid = 0
                marca = ""
            viejo = not _vivo(pid, marca) if pid else time.time() - lock.stat().st_mtime > 60
            if viejo:                                           # de una prueba que murio: se aparta, no se borra
                try:
                    RESPALDO.mkdir(parents=True, exist_ok=True)
                    lock.rename(RESPALDO / f"_en_uso_viejo_{dest.name}_{ts()}_{os.getpid()}")
                except OSError:
                    pass
                continue
            if time.time() - t0 > 3600:
                raise SystemExit(f"una hora esperando la copia {dest.name} (la usa el proceso {pid})")
            time.sleep(5)
    try:
        yield
    finally:
        (lock / "pid").unlink(missing_ok=True)
        lock.rmdir()


def comprobar_libre(dest):
    """No cierra juegos ajenos: si la copia esta en uso (otra prueba o agente), se para y avisa."""
    pids = pids_de_copia(dest)
    if pids:
        raise SystemExit(f"la copia {dest.name} tiene un juego abierto (pid {pids}). No lo cierro: "
                         "espera a que termine o cierralo tu, y repite.")


def mover(src, dst):
    """Lo reemplazado no se borra: se mueve a _respaldo."""
    RESPALDO.mkdir(parents=True, exist_ok=True)
    shutil.move(str(src), str(dst))
    print(f"  movido a respaldo: {dst}", flush=True)


_TL = threading.local()   # X1: (ancho, alto) de la ventana de la partida (--par ventana=2560x1080), por hilo: dos corridas en paralelo no se pisan


def dormir(s):
    """X0h: espera en RELOJ DE JUEGO. Con turbo xN el guest corre N veces mas rapido: las esperas de los escenarios
    (animaciones, fundidos, menus) se dividen por la velocidad real medida del juego de ESTE hilo (fps del log / 60,
    tope N). Sin juego o a x1 es time.sleep normal. Esperas medidas con time.time() siguen en time.sleep."""
    j = getattr(_TL, "j", None)
    k = j.factor() if j is not None else 1.0
    time.sleep(s if k <= 1.0 else max(min(s, 0.05), s / k))



def _ventana():
    return getattr(_TL, "v", None)


def escribir_toml(ruta):
    ov = dict(OVERRIDES, dbz3_game_dir=f"'{R}'")
    ov.update(_kv(x) for x in os.environ.get("DBZ3_TEST_CFG", "").split(";") if x)   # X1: p. ej. "dbz3_relight=2;present_effect=\"fsr3\""
    if _ventana():
        ov.update(window_width=str(_ventana()[0]), window_height=str(_ventana()[1]))
    lineas = ruta.read_text(encoding="utf-8", errors="surrogateescape").splitlines() if ruta.exists() else []
    out, vistos = [], set()
    for l in lineas:
        m = re.match(r"\s*([A-Za-z0-9_\-]+)\s*=", l)
        if m and m.group(1) in ("window_width", "window_height") and not _ventana():
            continue                      # el tamano forzado vale solo para esa corrida (X1)
        if m and m.group(1) in ov:
            out.append(f"{m.group(1)} = {ov[m.group(1)]}")
            vistos.add(m.group(1))
        else:
            out.append(l)
    out += [f"{k} = {v}" for k, v in ov.items() if k not in vistos]
    ruta.write_text("\n".join(out) + "\n", encoding="utf-8", errors="surrogateescape")


def preparar(dest, desde=None, exe=EXE):
    """Crea o refresca la copia: exe y DLL canonicas nuevas, toml de pruebas, controles limpios."""
    if not exe.exists():
        raise SystemExit(f"no existe el exe {exe}: compila primero")
    comprobar_libre(dest)
    if not (dest / "dbz3.exe").exists():
        src = Path(desde) if desde else BASE / "du_test"
        if not src.exists():
            raise SystemExit(f"no existe la copia origen {src}")
        print(f"  copiando {src} -> {dest} (sin logs ni shader_cache)", flush=True)
        # /R:1 /W:1: si el origen tiene un juego abierto, robocopy reintentaba un archivo bloqueado 1 millon de
        # veces cada 30 s (visto 2026-10-15: smoke_b, smoke_c y hub_test colgados copiando de du_test)
        r = subprocess.run(["robocopy", str(src), str(dest), "/E", "/XJ", "/R:1", "/W:1", "/XD", str(src / "logs"),
                            str(src / "shader_cache"), "/NDL", "/NJH", "/NJS", "/NP"],
                           capture_output=True, text=True, errors="replace")
        if r.returncode >= 8:
            fallidos = [l.strip() for l in (r.stdout or "").splitlines() if "ERROR" in l][:5]
            print(f"  aviso: robocopy codigo {r.returncode}, archivos en uso sin copiar (el exe, las DLL y el toml "
                  f"se ponen igual): {fallidos}", flush=True)
    (dest / "logs").mkdir(exist_ok=True)
    shutil.copy2(exe, dest / "dbz3.exe")
    for n in ("rexruntime.dll", "rexgpu-xenos.dll"):
        shutil.copy2(DLLS / n, dest / n)
    for n in ("dbz3_input.req", "dbz3_shot.req", "dbz3_shot.bmp", "dbz3_cam.req"):   # senales de control viejas
        (dest / n).unlink(missing_ok=True)
    escribir_toml(dest / "dbz3_user.toml")
    # 25/10: los scripts que el juego lanza (figuras de B3 en el tablero/nave de B2, Modo Heroe, cartas de tecnicas),
    # como en el zip del juego (make_modpacks.copiar_scripts_juego); sin ellos las copias caian a los modelos de B2
    try:
        sys.path.insert(0, str(R / "tools"))
        import make_modpacks  # noqa: PLC0415
        print(f"  scripts del juego: {make_modpacks.copiar_scripts_juego(str(dest))}", flush=True)
    except Exception as e:   # noqa: BLE001 (la copia sigue sirviendo para lo demas)
        print(f"  aviso: scripts del juego sin copiar: {e}", flush=True)
    if (R / "res" / "datos").is_dir():                     # X0d: datos medidos (res\datos\*.toml) que lee el juego
        shutil.copytree(R / "res" / "datos", dest / "res" / "datos", dirs_exist_ok=True)
    # 25/10: JUGAR tambien crea los escenarios convertidos (campos de IW, B1/B2); sin ellos los minijuegos de IW y los
    # combates de B2 caian al escenario de B3 y las pruebas no veian lo que ve el jugador. Solo crea los que falten.
    if (dest / "mods").is_dir() and (R / "us" / "data_cmn.afs").exists():
        r = subprocess.run([sys.executable, str(R / "mod center hd" / "campos_iw.py"), "auto", "--mods", str(dest / "mods"),
                            "--us", str(R / "us"), "--ps2", _PS2["DBZ3_PS2_GAMES"]],
                           capture_output=True, text=True, errors="replace")
        print(f"  escenarios convertidos: {sum('creado' in l for l in r.stdout.splitlines())} nuevos" if r.returncode == 0
              else f"  aviso: campos_iw auto codigo {r.returncode}: {(r.stderr or r.stdout)[-300:]}", flush=True)
    # R9 (2026-10-16): las copias no pasan por JUGAR del launcher, que monta el roster; con uno viejo (modos_test del
    # 14/10, sin cam_pad_clips) Pan colgaba el juego. Igual que el launcher; no hace nada si el roster esta al dia.
    if (dest / "mods").is_dir():
        r = subprocess.run([sys.executable, str(R / "mod center hd" / "roster_build.py"), "construir", "--mods",
                            str(dest / "mods")], capture_output=True, text=True, errors="replace")
        ult = (r.stdout.strip().splitlines() or [""])[-1]
        print(f"  roster: {ult}" if r.returncode == 0 else
              f"  aviso: roster_build codigo {r.returncode}, se queda el roster anterior: {(r.stderr or r.stdout)[-300:]}",
              flush=True)


def podar_respaldos(copia):
    """Respaldos automaticos de una copia (_auto y _antes_de_cargar_*): se quedan los MAX_RESPALDOS mas recientes;
    los demas se mueven (nunca se borran) a D:\\_PARA_BORRAR_CLAUDE\\pruebas_respaldos."""
    if not RESPALDO.exists():
        return
    suyos = sorted(d for d in RESPALDO.iterdir() if d.is_dir() and re.fullmatch(
        r"\d{8}-\d{6}_" + re.escape(copia) + r"_(auto|antes_de_cargar_.+)", d.name))
    for d in suyos[:-MAX_RESPALDOS]:
        try:
            PAPELERA.mkdir(parents=True, exist_ok=True)
            shutil.move(str(d), str(PAPELERA / d.name))
        except OSError:
            pass                                    # otro agente lo esta usando o ya lo movio


def respaldar(dest):
    """Copia (no mueve) los datos de partida antes de correr: por si un escenario guarda encima."""
    ud = dest / "user_data"
    if ud.exists():
        shutil.copytree(ud, RESPALDO / f"{ts()}_{dest.name}_auto")
    podar_respaldos(dest.name)


def guardar_instantanea(copia, nombre):
    ud = BASE / copia / "user_data"
    if not ud.exists():
        raise SystemExit(f"no hay datos de partida en {ud}")
    comprobar_libre(BASE / copia)
    dst = INST / nombre / "user_data"
    if dst.exists():
        mover(dst, RESPALDO / f"{ts()}_instantanea_previa_{nombre}")
    shutil.copytree(ud, dst)
    print(f"instantanea '{nombre}' guardada desde {copia}", flush=True)


def cargar_instantanea(copia, nombre):
    src = INST / nombre / "user_data"
    if not src.exists():
        raise SystemExit(f"no existe la instantanea '{nombre}' ({src})")
    comprobar_libre(BASE / copia)
    ud = BASE / copia / "user_data"
    if ud.exists():
        mover(ud, RESPALDO / f"{ts()}_{copia}_antes_de_cargar_{nombre}")
    shutil.copytree(src, ud)
    podar_respaldos(copia)
    print(f"instantanea '{nombre}' cargada en {copia}", flush=True)


class Juego:
    """Un juego lanzado en una copia: lectura de pantalla, teclas, capturas, log."""

    def __init__(self, copia, nombre):
        self.dest = BASE / copia
        self.nombre = nombre
        self.run = PRUEBAS / f"{ts()}_{nombre.replace('/', '_')}"
        self.run.mkdir(parents=True, exist_ok=True)
        self.proc = None
        self.h = None
        self.t0 = time.time()
        self.vistas = set()
        self.traza = []
        self.t_cupo = 0.0
        self.logs_antes = None                          # logs de la copia antes de lanzar (texto_log)

    def factor(self):
        """X0h: velocidad real del guest respecto a 60 fps (turbo pedido, tope), leida del ultimo 'perf fps=' del log (cache 2 s)."""
        k = getattr(self, "turbo", 1)
        if k <= 1:
            return 1.0
        if time.time() - getattr(self, "_f_t", 0) < 2.0:
            return self._f_v
        v = float(k)
        try:
            fs = [f for f, x in self._logs().items() if self.logs_antes is not None and self.logs_antes.get(f) != x]
            if fs:
                f = max(fs, key=lambda q: os.stat(q).st_mtime_ns)
                with open(f, "rb") as fh:
                    fh.seek(0, 2)
                    fh.seek(max(0, fh.tell() - 65536))
                    m = re.findall(r"perf fps=([0-9.]+)", fh.read().decode("utf-8", "replace"))
                if m:
                    v = min(float(k), max(1.0, float(m[-1]) / 60.0))
        except (OSError, ValueError):
            pass
        self._f_t, self._f_v = time.time(), v
        return v

    def info(self, msg):
        print(f"  [{time.time() - self.t0:6.1f} s] {self.nombre}: {msg}", flush=True)

    def lanzar(self, env_extra):
        # como mucho 3 juegos abiertos a la vez en el PC (varios agentes probando): espera su turno.
        # El candado LANZANDO hace atomico "contar + lanzar" entre agentes (antes dos podian ver 2 y abrir 4).
        env = {k: v for k, v in os.environ.items() if not k.startswith("DBZ3_")}
        env.update(SDL_WINDOW_ACTIVATE_WHEN_SHOWN="0", SDL_WINDOW_ACTIVATE_WHEN_RAISED="0", DBZ3_NO_DIALOGS="1")
        env.update(env_extra)
        t_espera = time.time()
        try:
            self.turbo = max(1, min(16, int(env.get("DBZ3_TEST_TURBO", "1"))))   # X0h: para dormir() en reloj de juego
        except ValueError:
            self.turbo = 1
        _TL.j = self
        while True:
            if time.time() - t_espera > 3600:
                raise Fallo("60 min esperando una plaza de juego (8 dbz3.exe como maximo; ventana: solo plaza1-2)")
            try:
                LANZANDO.mkdir()
            except FileExistsError:
                try:
                    if time.time() - LANZANDO.stat().st_mtime > 60:  # candado de un lanzador que murio
                        RESPALDO.mkdir(parents=True, exist_ok=True)
                        LANZANDO.rename(RESPALDO / f"_lanzando_viejo_{ts()}_{os.getpid()}")
                except OSError:                                       # otro agente lo movio o lo solto
                    pass
                time.sleep(1)
                continue
            try:
                # tasklist en Windows en espanol imprime "INFORMACION: no hay tareas..." cuando no hay juego:
                # sin errors="replace" el hilo lector moria con -X utf8 y esto contaba sobre None (tablero del frente 12).
                # tope = las plazas (X0h): sin doble contabilidad con tasklist
                if self._tomar_plaza(env.get('DBZ3_TEST_OCULTO') == '1'):
                    self.logs_antes = self._logs()      # el log de esta partida sera uno nuevo o uno que cambie
                    self.t0 = time.time()
                    # sin heredar la consola: si no, la llamada tarda hasta que el juego suelta esa salida
                    try:
                        self.proc = subprocess.Popen([str(self.dest / "dbz3.exe"), f"--game_directory={R}"],
                                                     cwd=str(self.dest), env=env, stdin=subprocess.DEVNULL,
                                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    except OSError:
                        self._soltar_plaza()
                        raise
                    break
            finally:
                LANZANDO.rmdir()                                      # candado propio (carpeta vacia)
            time.sleep(5)
        self.t_cupo = self.t0 - t_espera
        if env.get("DBZ3_TEST_OCULTO") == "1":
            threading.Thread(target=_ocultar_ventanas, args=(self.proc,), daemon=True).start()
        else:
            threading.Thread(target=_a_pantalla_secundaria, args=(self.proc, len(list(PLAZAS.glob("plaza*"))) - 1),
                             daemon=True).start()
        self.h = K32.OpenProcess(0x0410, False, self.proc.pid)     # QUERY_INFORMATION | VM_READ
        if not self.h:
            raise Fallo("no pude leer la memoria del juego")
        self.base_real()

    def _tomar_plaza(self, oculto=False):
        if PLAZAS.with_name('_compilando').exists() and len(list(PLAZAS.glob('plaza*'))) >= TOPE_JUEGOS - 2:
            return False
        """X0h: primera _juego/plaza1..4 (studies 5/ENTREGAS) libre (mkdir atomico) con quien.txt; se quita al cerrar."""
        try:
            PLAZAS.mkdir(parents=True, exist_ok=True)
        except FileExistsError:
            # Otra corrida creó _juego entre exists() y mkdir(): no es un fallo de la prueba.
            if not PLAZAS.is_dir():
                return False
        for i in (range(3, TOPE_JUEGOS + 1) if oculto else (1, 2)):
            d = PLAZAS / f"plaza{i}"
            try:
                d.mkdir()
            except FileExistsError:
                _recuperar_plaza_huerfana(d)
                try:
                    d.mkdir()
                except FileExistsError:
                    continue
            sello = f"{os.getpid()}:{time.time_ns()}:{threading.get_ident()}"
            try:
                # 'x' evita pisar el quien.txt de otra reserva si el directorio se recicla en esta carrera.
                with (d / "quien.txt").open("x", encoding="utf-8") as f:
                    f.write(f"pruebas.py {self.nombre} copia {self.dest.name} pid {os.getpid()} {ts()} token={sello}\n")
            except FileExistsError:
                continue
            self.plaza = d
            self.plaza_token = sello
            return True
        return False

    def _soltar_plaza(self):
        d = getattr(self, "plaza", None)
        self.plaza = None
        if d:
            # Una reserva vieja nunca debe borrar la carpeta de quien ya reutilizó el mismo plazaN.
            token = getattr(self, "plaza_token", "")
            try:
                quien = (d / "quien.txt").read_text(encoding="utf-8", errors="replace")
            except OSError:
                quien = ""
            if token and f"token={token}" in quien:
                path = d / "quien.txt"
                for _ in range(50):  # Windows puede mantener el lector del otro proceso abierto unos ms
                    try:
                        path.unlink(missing_ok=True)
                        break
                    except PermissionError:
                        time.sleep(0.01)
                else:
                    return  # se conserva el lock; el siguiente intento lo reevalúa por PID/token
            else:
                return
            try:
                d.rmdir()
            except (OSError, FileExistsError):
                pass
            self.plaza_token = ""

    def base_real(self, seg=20):
        """Fija DESPL con la base de la arena que dice el log de esta partida (espera hasta seg)."""
        global DESPL
        fin = time.time() + seg
        while time.time() < fin:
            m = re.search(r"Guest memory arena mapped: virtual base 0x([0-9A-Fa-f]+)", self.texto_log())
            if m:
                DESPL = int(m.group(1), 16) - GBASE
                if DESPL:
                    self.info(f"arena de memoria en 0x{int(m.group(1), 16):X} (no en 0x{GBASE:X}): ajustado")
                return
            time.sleep(0.2)
        DESPL = 0

    def vivo(self):
        return self.proc is not None and self.proc.poll() is None

    def cerrar(self):
        if self.proc and self.proc.poll() is None:
            # copia desechable: 2 s de cortesia y fuera (el cierre amable tardaba >10 s tras
            # "Window close requested"); el juego guarda al pedirlo, no al salir
            cerrar_pid(self.proc.pid, 2)
            try:        # tras /F a veces tarda >5 s en desaparecer; la siguiente prueba en la copia lo veia vivo
                self.proc.wait(30)
            except subprocess.TimeoutExpired:
                self.info("el juego sigue vivo 30 s despues de cerrarlo")
        if self.h:
            K32.CloseHandle(ctypes.c_void_p(self.h))
            self.h = None
        if not self.vivo():
            self._soltar_plaza()

    def pantalla(self):
        addr = PANTALLA_EU if os.environ.get("DBZ3_TEST_PANTALLA_EU") == "1" else PANTALLA
        b = leer(self.h, addr, 1)
        sc = b[0] if b else None
        if sc is not None:
            self.vistas.add(sc)
        if not self.traza or self.traza[-1][1] != sc:     # cambios de pantalla con su segundo (pantallas.txt)
            self.traza.append((round(time.time() - self.t0, 1), sc))
        return sc

    def tecla(self, k, espera=600):
        if not self.vivo():
            raise Fallo("el juego se cerro")
        if k == "F5":     # X0d: la tecla virtual no llega al bind de SDL; en pruebas el juego mira este fichero
            (self.dest / "dbz3_datos.req").write_text("1", encoding="ascii")
            dormir(espera / 1000)
            return
        req = self.dest / "dbz3_input.req"
        fin = time.time() + 2.5
        while req.exists() and time.time() < fin:       # esperar a que el juego consuma la anterior
            time.sleep(0.05)
        tmp = self.dest / "dbz3_input.req.tmp"
        tmp.write_text(k + "\n", encoding="ascii")
        os.replace(tmp, req)
        self.traza.append((round(time.time() - self.t0, 1), f"tecla {k}"))
        dormir(espera / 1000)

    def ventana(self):
        h = _hwnd_juego(self.proc.pid) if self.proc else 0
        if not h:
            raise Fallo("no encuentro la ventana del juego para la entrada real")
        return h

    def enfocar(self):
        h = self.ventana()
        U32.SetForegroundWindow(h)
        time.sleep(0.25)
        return h

    def tecla_real(self, k, hold_ms=180):
        """Tecla de VERDAD (WM_KEYDOWN/UP): llega a SDL e ImGui, no solo al mando emulado."""
        h = self.enfocar()
        vk = _VK.get(k)
        if not vk:
            raise Fallo(f"tecla_real: tecla no soportada '{k}'")
        U32.PostMessageW(h, 0x0100, vk, 1)   # WM_KEYDOWN
        time.sleep(max(0.02, hold_ms / 1000))
        U32.PostMessageW(h, 0x0101, vk, 1)   # WM_KEYUP
        self.traza.append((round(time.time() - self.t0, 1), f"tecla real {k}"))
        time.sleep(0.15)

    def raton_derecho(self, dx=90, dy=0, hold_ms=350, pasos=6):
        """Boton derecho mantenido con movimiento real del cursor (mirar de la camara libre por ImGui)."""
        h = self.enfocar()
        r = _RECT()
        U32.GetClientRect(h, ctypes.byref(r))
        p = wt.POINT((r.left + r.right) // 2, (r.top + r.bottom) // 2)
        U32.ClientToScreen(h, ctypes.byref(p))
        U32.SetCursorPos(p.x, p.y)
        time.sleep(0.2)
        U32.mouse_event(0x0008, 0, 0, 0, 0)   # RIGHTDOWN
        for i in range(max(1, pasos)):
            U32.mouse_event(0x0001, dx // max(1, pasos), dy // max(1, pasos), 0, 0)   # MOVE relativo
            time.sleep(max(0.02, hold_ms / 1000 / max(1, pasos)))
        U32.mouse_event(0x0010, 0, 0, 0, 0)   # RIGHTUP
        time.sleep(0.2)
        self.traza.append((round(time.time() - self.t0, 1), "raton derecho"))

    def captura_estable(self, nombre, intentos=6, eps=2.0, min_lum=0.0):
        """X1: captura y repite hasta que dos seguidas casi no cambian (fin de un fundido o de una transicion)."""
        from PIL import ImageChops, ImageStat
        ant, out = None, None
        for i in range(intentos):
            out = self.captura(f"{nombre}_t{i}")
            im = Image.open(out).convert("L").resize((160, 90))
            if ant is not None and ImageStat.Stat(im).mean[0] >= min_lum and sum(ImageStat.Stat(ImageChops.difference(ant, im)).mean) < eps:
                break
            ant = im
            dormir(0.7)
        final = self.run / f"{nombre}.png"
        shutil.copyfile(out, final)
        return final

    def captura(self, nombre):
        bmp = self.dest / "dbz3_shot.bmp"
        bmp.unlink(missing_ok=True)
        (self.dest / "dbz3_shot.req").write_text("1", encoding="ascii")
        fin = time.time() + 6
        while time.time() < fin:
            if bmp.exists() and bmp.stat().st_size > 0:
                tam = bmp.stat().st_size
                time.sleep(0.15)                          # que el juego termine de escribirlo
                if bmp.exists() and bmp.stat().st_size == tam:
                    break
            time.sleep(0.05)
        else:
            raise Fallo(f"captura '{nombre}' sin respuesta del juego")
        out = self.run / f"{nombre}.png"
        if Image:
            with Image.open(bmp) as im:
                im.save(out)
        else:
            out = out.with_suffix(".bmp")
            shutil.copy(bmp, out)
        bmp.unlink(missing_ok=True)
        return out

    def captura_fallo(self):
        """Foto de como quedo la pantalla al fallar (si el juego sigue vivo); nunca tapa el fallo original."""
        try:
            if self.vivo():
                self.info(f"captura del fallo: {self.captura('zz_fallo')}")
        except Exception:
            pass

    def esperar_cambio(self, previa, seg=6):
        fin = time.time() + seg
        while time.time() < fin:
            sc = self.pantalla()
            if sc is not None and sc != previa:
                return sc
            time.sleep(0.5)
        return self.pantalla()

    def esperar_pantalla(self, ids, seg=60):
        fin = time.time() + seg
        while True:
            sc = self.pantalla()
            if sc in ids:
                return sc
            if not self.vivo():
                raise Fallo("el juego se cerro")
            if time.time() > fin:
                raise Fallo(f"no llego a la pantalla {sorted(ids)} en {seg} s (estaba en {sc})")
            time.sleep(0.25)

    def tecla_hasta(self, k, ids, intentos=4, seg=4):
        """Pulsa k hasta que la pantalla sea una de ids (la primera pulsacion a veces se pierde)."""
        for _ in range(intentos):
            if self.pantalla() in ids:
                return
            self.tecla(k, 0)
            try:
                self.esperar_pantalla(ids, seg)
                return
            except Fallo:
                if not self.vivo():
                    raise
        raise Fallo(f"'{k}' x{intentos} no llevo a la pantalla {sorted(ids)} (estaba en {self.pantalla()})")

    def tecla_hasta_log(self, k, regex, intentos=4, seg=4):
        """Pulsa k hasta que el log contenga regex (menus que no cambian el byte de pantalla)."""
        for _ in range(intentos):
            self.tecla(k, 0)
            fin = time.time() + seg
            while time.time() < fin:
                if re.search(regex, self.texto_log()):
                    return
                time.sleep(0.25)
        raise Fallo(f"'{k}' x{intentos} y el log sigue sin '{regex}'")

    def arrancar(self, seg=120, rapido=True, titulo=("Return", "Space")):
        """Espera el menu principal (pantalla 5) sondeando; en el titulo (4, el video de apertura ya va en ella)
        pulsa las teclas de 'titulo' por turnos cada 1,5 s (antes Space cada 6 s: el menu llegaba ~7 s tarde).
        Perfil sin partida: el titulo ofrece New Game / Load Game (cursor en Load): titulo=("Return","W","Space")."""
        fin = time.time() + seg
        n = 0
        sin_pantalla = None   # 18/10: titulo visible con pantalla None (zz_fallo.png) y nadie pulsaba START
        while True:
            sc = self.pantalla()
            if sc is None:
                sin_pantalla = sin_pantalla or time.time()
                if time.time() - sin_pantalla > 15 and self.vivo():   # vivo y sin pantalla: se trata como el titulo
                    self.tecla(titulo[n % len(titulo)], 0)
                    n += 1
                    dormir(1.5)
            else:
                sin_pantalla = None
            if sc == 5:
                return time.time() - self.t0
            if not self.vivo():
                raise Fallo("el juego se cerro al arrancar")
            if time.time() > fin:
                raise Fallo(f"no llego al menu principal en {seg} s (pantalla {sc})")
            if sc in (None, 0):                             # cargando
                time.sleep(0.25)
                continue
            if sc == 4 and rapido:
                self.tecla(titulo[n % len(titulo)], 0)
                n += 1
                self.esperar_cambio(sc, 1.5)
                continue
            self.tecla("Space" if sc == 4 else "Return", 0)
            self.esperar_cambio(sc, 6)

    def _logs(self):
        out = {}
        for f in glob.glob(str(self.dest / "logs" / "dbz3_*.log")):
            try:
                st = os.stat(f)
                out[f] = (st.st_size, st.st_mtime_ns)
            except OSError:
                pass
        return out

    def texto_log(self):
        """Log de ESTA partida: uno que no existia al lanzar o que ha cambiado desde entonces. Antes se filtraba
        por fecha (t0 - 5 s) y dos pruebas seguidas en la misma copia podian leer el log de la anterior."""
        ahora = self._logs()
        if self.logs_antes is None:                     # sin lanzar: nada de esta partida
            return ""
        fs = [f for f, v in ahora.items() if self.logs_antes.get(f) != v]
        if not fs:
            return ""
        try:
            return Path(max(fs, key=lambda f: ahora[f][1])).read_text(encoding="utf-8", errors="replace")
        except OSError:
            return ""

    def log(self, regex, seg=20):
        fin = time.time() + seg
        while not re.search(regex, self.texto_log()):
            if time.time() > fin:
                raise Fallo(f"el log no contiene '{regex}' tras {seg} s")
            time.sleep(0.5)
        self.info(f"log ok: {regex}")

    def bloque(self, cmd):
        """dump: guarda el bloque de DU de la casilla 22 en la carpeta de la prueba; write: lo restaura."""
        f = self.run / "bloque22.bin"
        h = K32.OpenProcess(0x0438, False, self.proc.pid)   # QUERY | VM_READ | VM_WRITE | VM_OPERATION
        if not h:
            raise Fallo("no pude abrir la memoria del juego para dump/write")
        try:
            addr = GBASE + ESTRUCTAS[0] + 336 + BLOQUE * RANURA
            if cmd == "dump":
                b = leer(h, addr, BLOQUE)
                if not b or len(b) < BLOQUE:
                    raise Fallo("dump sin respuesta del juego")
                f.write_bytes(b)
                self.info(f"dump {len(b)} bytes -> {f.name}")
            else:
                data = f.read_bytes()
                for s in ESTRUCTAS:
                    n = ctypes.c_size_t()
                    dir_s = GBASE + DESPL + s + 336 + BLOQUE * RANURA
                    if not K32.WriteProcessMemory(ctypes.c_void_p(h), ctypes.c_void_p(dir_s), data, len(data),
                                                  ctypes.byref(n)):
                        raise Fallo(f"write en {hex(s)} fallo")
                self.info("write en las dos estructuras")
        finally:
            K32.CloseHandle(ctypes.c_void_p(h))


# ---------- escenarios integrados (orden y tiempos tomados de du_full.ps1 y tt_iso.ps1) ----------

def _menu(j, par):
    j.captura("menu_principal")


def _ir_a_rueda(j):
    for _ in range(12):
        sc = j.pantalla()
        if sc == 7:
            return
        if sc == 5:
            j.tecla("Space", 4000)
        elif sc == 6:
            j.tecla("Return", 3000)
            if j.pantalla() == 6:
                j.tecla("Space", 7000)
        else:
            j.tecla("Return", 3000)
    raise Fallo(f"no llegue a la rueda de personajes (pantalla {j.pantalla()})")


def _du_freezer_nuevo(j, par):
    _ir_a_rueda(j)
    for _ in range(int(par.get("up", 2))):              # desde Goku: Broly, Freezer
        j.tecla("W", 2500)
    j.captura("00_rueda")
    ultimo = None
    for i in range(1, int(par.get("iter", 40)) + 1):
        sc = j.pantalla()
        if sc == 10:                                    # subir de nivel: repartir puntos
            for _ in range(5):
                j.tecla("D", 400)
            j.tecla("Space", 2500)
        else:
            j.tecla("Space", int(par.get("paso_ms", 5000)))
        if i % 3 == 0:
            j.captura(f"{i:03d}")
        if sc != ultimo:
            j.info(f"paso {i}: pantalla {sc}")
            ultimo = sc
    if not ({8, 9} & j.vistas):
        raise Fallo("nunca entro en escenas (8) ni en combate (9)")
    for l in [l for l in j.texto_log().splitlines() if "dbz3 campanas" in l][-3:]:
        j.info(re.sub(r"^(\[[^\]]*\] ){3}", "", l)[:170])


# DS163: poses de la camara libre de prueba, en la escala del mundo (la camara del combate esta a ~50 u del objetivo:
# ojo 0 13 50 -> objetivo 0 13 0). x,y,z = desplazamiento respecto al ojo del juego; yaw,pitch = giro en grados
# respecto a su orientacion; fov = vertical en grados (45 es el del juego). Las tres miran a los luchadores desde
# lados distintos (izquierda, derecha y desde arriba) para que las capturas no se confundan con la camara del juego.
FREECAM_POSES = "-28,5,-18,-41,-13,55;30,1,-16,41,-8,50;0,42,-30,0,-68,60"


def _freecam_env(p):
    """Variables del escenario freecam: combate normal, IW (vuelo/minijuego) o B2; y modo teclas o poses."""
    teclas = str(p.get("teclas", "0")) == "1"
    modo = str(p.get("modo", "")).lower()
    env = {"DBZ3_FREECAM_LOG": "2", "DBZ3_PS2_GAMES": rf"D:\DBZ3HD\ps2_games"}
    if modo == "iw":
        env["DBZ3_TEST_IWMINI"] = str(p.get("clave", "0x7D6")) + ",auto"
    elif modo == "iwmini":
        env["DBZ3_TEST_IWMINI"] = str(p.get("clave", "0x7D0")) + ",auto"
    elif modo == "b2":
        env["DBZ3_TEST_MISIONES"] = "1,reset"          # tablero B2 a mano (fight largo, sin RINGOUT)
    else:
        env["DBZ3_TEST_BATTLE"] = str(p.get("ids", "0,7,2"))
    if not teclas:
        env["DBZ3_TEST_FREECAM"] = FREECAM_POSES
    else:
        # Tutoriales OFF: "Ver tutoriales" es accion y se oculta, asi MODOS tiene 6 filas fijas hasta Camara libre
        # (y no sale el tutorial de los modos nuevos encima de la escena).
        env["REX_DBZ3_TUTORIALES"] = "false"
    return env


def _f1_abrir(j):
    """Abre el F1 con el mando (Back+Start = Tab+Return); las teclas van por dbz3_input.req (llegan al pad)."""
    j.tecla("Tab+Return", 900)
    dormir(0.6)


def _f1_modos_freecam(j, teclas_extra=(), encender=True):
    """F1 > MODOS > Camara libre (+ opciones). Navega con el mando emulado: RB x4 = MODOS, abajo xN = fila."""
    _f1_abrir(j)
    for _ in range(4):
        j.tecla("3", 450)                              # RB: IMAGEN -> ... -> MODOS
    for _ in range(5):
        j.tecla("Shift+Down", 300)                     # HUB/Tag/Online/Transformacion/Mostrar tutoriales
    if encender:
        j.tecla("Space", 700)                          # A: enciende "Camara libre"
        j.log(r"Settings menu: dbz3_freecam = true", 10)
        j.log(r"camara libre: activa", 15)
    for nombre, filas in teclas_extra:
        for _ in range(filas):
            j.tecla("Shift+Down", 300)
        j.tecla("Space", 700)
        j.log(rf"Settings menu: {nombre} = true", 10)
    j.tecla("Backspace", 700)                          # B: cierra el F1


def _freecam_mem(j):
    """(mgr, reloj de pelea) leidos de la memoria del guest (big-endian, como el resto de pruebas.py)."""
    b = leer(j.h, GBASE + 0x823D6CF4, 4)
    mgr = int.from_bytes(b, "big") if b and len(b) == 4 else 0
    if not mgr:
        return 0, None
    c = leer(j.h, GBASE + mgr + 35144, 4)
    return mgr, (struct.unpack(">f", c)[0] if c and len(c) == 4 else None)


def _freecam_ojo(j, desde=0):
    """Ultimo ojo escrito por la camara libre en el log (lineas 'escrito ojo (x y z)')."""
    o = None
    for l in j.texto_log()[desde:].splitlines():
        m = re.search(r"freecam\[4300pre\].*escrito ojo \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)", l)
        if m:
            o = tuple(float(x) for x in m.groups())
    return o


def _freecam(j, par):
    # DS164: escenario de la camara libre.
    #  - sin --par teclas=1: DBZ3_TEST_FREECAM enciende la camara sola y dbz3_freecam.req elige 3 poses (DS162/3).
    #  - --par teclas=1: la enciende el F1 con teclas (mando emulado), mueve con teclado/raton REALES, prueba
    #    Pausar/Ocultar HUD y la apaga con R. Nada de variables de prueba para encenderla.
    #  - --par modo=iw|iwmini|b2: la escena es el vuelo/minijuego de IW o el combate del tablero de B2.
    teclas = str(par.get("teclas", "0")) == "1"
    modo = str(par.get("modo", "")).lower()
    if modo == "b2":
        _b2_ataque_manual(j, par)                      # tablero a mano hasta "tablero b2: combate:"
        dormir(2)
    elif modo in ("iw", "iwmini"):
        j.log(r"iwmini: prueba: empieza", 120)
        if modo == "iw":
            j.log(r"terminado; empieza el vuelo", 120)
        else:
            j.log(r"iwmini: salida puesta", 120)
        dormir(1.5)
    else:
        j.log(r"hub: combate nativo", 30)
        j.log(r"tag: empieza un combate", 40)

    if teclas:
        _f1_modos_freecam(j)
        j.captura("freecam_antes")
        ojo0 = _freecam_ojo(j, 0)
        # Movimiento con teclas REALES (W, A, Q) y raton derecho mantenido: el camino del jugador.
        for k in ("W", "A", "Q"):
            j.tecla_real(k, 450)
        j.raton_derecho(120, 0, 400)
        dormir(0.8)
        j.captura("freecam_despues")
        ojo1 = _freecam_ojo(j, 0)
        if ojo0 and ojo1 and ojo0 == ojo1:
            raise Fallo("la camara libre no se movio con W/A/Q/raton (ojo igual en el log)")
        # Pausa real + HUD oculto desde el F1 (dos filas nuevas bajo "Camara libre").
        _f1_modos_freecam(j, [("dbz3_freecam_pausa", 1), ("dbz3_freecam_hud", 1)], encender=False)
        j.log(r"combate: marcador oculto", 10)
        j.log(r"camara libre: juego en pausa", 10)
        j.captura("freecam_pausa_hud")
        _, reloj0 = _freecam_mem(j)
        dormir(1.5)
        _, reloj1 = _freecam_mem(j)
        if reloj0 is not None and reloj1 is not None and reloj0 != reloj1:
            raise Fallo(f"la pausa no congela el reloj ({reloj0} -> {reloj1})")
        # R de verdad: vuelve la camara del juego y se suelta la pausa.
        j.tecla_real("R", 120)
        j.log(r"camara libre: apagada", 15)
        j.log(r"camara libre: juego reanudado", 15)
        dormir(0.8)
        j.captura("freecam_vuelta")
        return
    j.log(r"camara libre: activa", 30)
    for i in range(3):
        (j.dest / "dbz3_freecam.req").write_text(str(i), encoding="ascii")
        j.log(rf"camara libre: prueba pose {i + 1}", 20)
        dormir(0.6)
        j.captura(f"freecam_0{i + 1}")


def _batalla(j, par):
    # DBZ3_TEST_BATTLE (src/hub.cpp): 3 s despues del menu principal lanza el combate; RINGOUT lo acaba a los 5 s
    j.log(r"hub: combate nativo", 30)
    j.log(r"tag: empieza un combate", 40)
    j.captura("01_combate")
    j.log(r"tag: combate acabado", 40)
    # estados del combate (tagteam.cpp, con RINGOUT): 5 acabado, 6 pose del ganador, 7 menu final (Battle
    # Again? / Reselect Characters / Return to Main Menu, cursor en el primero; medido 2026-10-15). A
    # ciegas, una A antes de tiempo elegia la revancha con el PC cargado.
    j.log(r"tag: prueba: estado del combate 7", 40)
    # ronda 3: en los combates del HUB el menu final ya no sale (modos.cpp, sub_820FF750): sale solo al menu
    j.log(r"sin menu final", 20)
    j.log(r"hub: fin del combate nativo", 30)
    j.esperar_pantalla([5], 40)                         # vuelta al menu principal
    j.captura("02_vuelta_menu")


def _tienda(j, par):
    # HUB > Supervivencia > "Supervivencia con tienda": el primer combate se gana por ring out (DBZ3_TEST_RINGOUT) y
    # entre combates sale la tienda con las piezas de la Skill Shop del juego (botellas, caja de Zenie).
    dormir(2.5)
    j.tecla("W", 1200)
    j.tecla("Space", 3500)                              # HUB
    for _ in range(4):                                  # tagteam .. supervivencia
        j.tecla("S", 450)
    j.tecla("Space", 2500)                              # selector de retos
    j.tecla("S", 700)                                   # Supervivencia con tienda
    j.captura("a_selector")
    j.tecla("Space", 3500)
    if "partida reanudada entre combates" not in j.texto_log():   # sin partida a medias: se gana el primero
        j.tecla_hasta_log("Space", r"hub: combate nativo", 4, 4)
        j.log(r"rival fuera del ring", 90)
        j.log(r"hub: fin del combate nativo", 40)
        dormir(5)
    j.captura("b_tienda")
    j.tecla("Space", 1500)                              # compra la cura (o avisa de que no se puede)
    j.captura("c_tienda_compra")


def _hub_lab(j, par):
    # HUB con el fondo propio y sin tiras doradas; Laboratorio (entrada 7) con su fondo y el selector de luchador y
    # rival (izquierda/derecha en las filas 0 y 1). B vuelve al HUB.
    dormir(2.5)
    j.tecla("W", 1200)                                  # fila HUB (la ultima del menu principal)
    j.tecla("Space", 3500)                              # pantalla HUB (cursor en Tag Team)
    j.captura("a_hub")
    for _ in range(7):                                  # tagteam .. laboratorio
        j.tecla("S", 450)
    j.captura("b_hub_lab")
    j.tecla("Space", 2500)
    j.captura("c_lab")
    j.tecla("D", 700)                                   # luchador siguiente
    j.tecla("D", 700)
    j.captura("d_lab_luchador")
    j.tecla("S", 500)
    j.tecla("D", 700)                                   # rival siguiente
    j.captura("e_lab_rival")
    j.tecla("Backspace", 2000)
    j.captura("f_vuelta_hub")


def _hub_filas(j, par):
    # HUB pagina 2: la fila Pasarela con el avance de las campanas (bocadillo) y la fila nueva Mision 100, que se abre.
    dormir(2.5)
    j.tecla("W", 1200)                                  # fila HUB (la ultima del menu principal)
    j.tecla("Space", 3500)                              # pantalla HUB (cursor en Tag Team)
    for _ in range(8):                                  # tagteam .. pasarela (pagina 2; escenarios y minijuegos no salen)
        j.tecla("S", 450)
    j.captura("a_pasarela")
    for _ in range(2):                                  # .. satan .. mision 100
        j.tecla("S", 450)
    j.captura("b_mision100")
    j.tecla("Space", 2500)
    j.captura("c_paginas")
    j.tecla("Backspace", 2000)
    j.captura("d_vuelta_hub")


def _heroe_crear(j, par):
    # frente G (ronda 3): HUB > Kumite > Modo Heroe > pantalla de creacion (raza, cuerpo, apariencia, maestro) >
    # Empezar: Humano (Krilin) con traje 2 y maestro Krilin (sus tecnicas) -> mision 1 contra los Saibaman.
    dormir(2.5)
    j.tecla("W", 1200)
    j.tecla("Space", 3500)                              # HUB
    for _ in range(5):                                  # tagteam .. kumite
        j.tecla("S", 450)
    j.tecla("Space", 2500)                              # selector de retos
    for _ in range(3):                                  # kumite .. modo heroe
        j.tecla("S", 700)
    j.captura("a_selector_heroe")
    j.tecla("Space", 2500)                              # pantalla de creacion
    j.captura("b_crear")
    j.tecla("D", 700)                                   # raza: Humano
    j.captura("c_raza_humano")
    j.tecla("S", 500)
    j.tecla("S", 500)
    j.tecla("D", 700)                                   # apariencia: traje 2
    j.tecla("S", 500)
    for _ in range(3):                                  # maestro: Goku, Vegeta, Piccolo, Krilin
        j.tecla("D", 600)
    j.captura("d_maestro_krilin")
    j.tecla("S", 500)
    j.captura("e_empezar")
    j.tecla_hasta_log("Space", r"hub: combate nativo", 4, 4)
    j.log(r"capsulas del jugador", 40)
    dormir(6)
    j.captura("f_mision1")
    dormir(5)
    j.captura("g_mision1_pelea")
    for l in [l for l in j.texto_log().splitlines() if "dbz3 rpg" in l or "capsulas" in l or "reglas" in l][-6:]:
        j.info(re.sub(r"^(\[[^\]]*\] ){3}", "", l)[:170])


def _r5_heroe(j, par):
    # frente R5 (ronda 4): Modo Heroe con un cuerpo y el moveset (estilo) de otro luchador. La receta se escribe
    # antes en user_data/dbz3/rpg.toml (raza/cuerpo/estilo); aqui: HUB > Kumite > Modo Heroe > creacion > Empezar.
    # --par estilo=N: pulsaciones a la derecha en la fila Estilo (por defecto 0, la del rpg.toml).
    dormir(2.5)
    j.tecla("W", 1200)
    j.tecla("Space", 3500)                              # HUB
    for _ in range(5):                                  # tagteam .. kumite
        j.tecla("S", 450)
    j.tecla("Space", 2500)                              # selector de retos
    for _ in range(3):                                  # kumite .. modo heroe
        j.tecla("S", 700)
    j.captura("a_selector_heroe")
    j.tecla("Space", 2500)                              # pantalla de creacion
    for _ in range(3):
        j.tecla("S", 500)
    for _ in range(int(par.get("estilo", "0"))):
        j.tecla("D", 600)
    j.captura("b_estilo")
    j.tecla("S", 500)
    j.captura("c_empezar")
    j.tecla_hasta_log("Space", r"hub: combate nativo", 4, 4)
    j.log(r"dbz3 heroe: estilo", 40)
    dormir(float(par.get("espera", "16")))         # pasa la presentacion del escenario
    for k in range(int(par.get("rafaga", "0"))):        # --par rafaga=N: N capturas cada 2 s (intros, READY)
        j.captura(f"r{k:02d}")
        dormir(2)
    j.captura("d_pelea_1")
    dormir(5)
    j.captura("e_pelea_2")
    j.tecla("J", 300)                                   # un golpe (moveset del estilo)
    dormir(1)
    j.captura("f_pelea_3")
    if par.get("ringout") == "1":                       # gana por salida del ring: pantalla entre combates y sale
        j.log(r"combate 1 ganado", 60)
        dormir(6)
        j.captura("g_entre")
        if par.get("completa") == "1":                  # los 3 combates: fin de saga, nivel y saga siguiente
            for k in (2, 3):
                j.tecla_hasta_log("Space", rf"combate {k}: pedido", 3, 4)
                dormir(20)
                j.captura(f"h_pelea_{k}")
                if k == 2:
                    j.log(r"combate 2 ganado", 60)
                    dormir(6)
            j.log(r"fin de saga", 60)
            dormir(4)
            j.captura("i_fin")
        else:
            j.tecla("Backspace", 3000)                  # B: guardar y salir (el estilo vuelve a su modelo)
            j.log(r"vuelve a su modelo", 20)
            j.captura("h_fin")
    for l in [l for l in j.texto_log().splitlines() if "dbz3 rpg" in l or "dbz3 heroe" in l][-8:]:
        j.info(re.sub(r"^(\[[^\]]*\] ){3}", "", l)[:170])


# DS130: piezas de ropa del Modo Heroe (pelos 0-10 y guantes/mangas 70..447) vistas EN JUEGO, en movimiento.
# La pieza se escribe en la ficha activa de cac.toml (lo que lee src/heroe.cpp) y el modelo de combate se monta
# antes de lanzar con vista_heroe.py --combate + roster_build (lo mismo que hace JUGAR), para que el heroe propio
# ya salga con la pieza en ESTA sesion. Nunca se toca out/build/win-amd64-release/user_data.
# todas las piezas de la zona "guantes" del catalogo B3 (antes una lista fija de 11)
PIEZAS_GUANTES = tuple(x["fid"] for x in json.load(open(R / "mod center hd" / "cac" / "piezas_b3_base.json",
                                                       encoding="utf-8"))["guantes"])


def _heroe_pieza_valores(par):
    pelo = int(par["pelo"]) if par.get("pelo") not in (None, "") else -1
    guante = int(par["guante"]) if par.get("guante") not in (None, "") else -1
    if pelo < 0 and guante < 0:
        raise SystemExit("heroe_pieza: usa --par pelo=N (0-10) y/o --par guante=N (%s)" %
                         ", ".join(str(x) for x in PIEZAS_GUANTES))
    if pelo >= 0 and not 0 <= pelo <= 10:
        raise SystemExit("heroe_pieza: pelo va de 0 a 10 (0 = el del cuerpo)")
    if guante >= 0 and guante not in PIEZAS_GUANTES:
        raise SystemExit("heroe_pieza: guante debe ser uno de %s" % ", ".join(str(x) for x in PIEZAS_GUANTES))
    return (pelo if pelo >= 0 else 0, guante)


def _heroe_pieza_env(p):
    """DS154: enciende la camara de prueba si hay que ver la cabeza (cara) o los brazos (guante y brazo!=0). Con
    brazos la duracion es larga y la apaga el propio escenario con dbz3_cam.req=off; sin brazos, 4 s (DS141)."""
    con_brazos = p.get("guante") not in (None, "") and p.get("brazo", "1") != "0"
    env = {"DBZ3_CAC_B3_TEST_TODOS": "1", "DBZ3_CAC_XV2": "0"}
    if p.get("cara", "1") == "0" and not con_brazos:
        return env
    env.update({"DBZ3_TEST_CARA": "0",
                "DBZ3_TEST_CARA_DIST": p.get("cara_dist", "4.5"),
                "DBZ3_TEST_CARA_LADO": p.get("cara_lado", "0"),
                "DBZ3_TEST_CARA_ESPERA": p.get("cara_espera", "60"),
                "DBZ3_TEST_CARA_HUESO": p.get("cara_hueso", "cabeza"),
                "DBZ3_TEST_CARA_DIST_BRAZO": p.get("brazo_dist_codo", "7"),
                "DBZ3_TEST_CARA_LADO_BRAZO": p.get("brazo_lado", "2"),
                # DS155: con brazos, el combate se queda quieto (mandos sin pulsaciones + reloj parado) y el
                # heroe en reposo mientras se toman las fotos. Solo en la prueba.
                "DBZ3_TEST_CARA_QUIETO": "1" if con_brazos and p.get("quieto", "1") != "0" else "0",
                "DBZ3_TEST_CARA_DURACION": p.get("cara_duracion", "20000" if con_brazos else "4000")})
    return env


def _heroe_pieza_receta(dest, pelo, guante):
    """Escribe user_data/dbz3/cac.toml de la COPIA de pruebas con la pieza pedida. Devuelve el texto --ficha que
    entiende vista_heroe.py. Cuerpo Goku/traje 0: es el unico caso en el que _aplicar_piezas_b3 acepta donantes."""
    ud = Path(dest) / "user_data" / "dbz3"
    ud.mkdir(parents=True, exist_ok=True)
    cac = ud / "cac.toml"
    if cac.is_file():
        shutil.copy2(cac, ud / f"cac.toml.ds130-{ts()}.bak")
    cac.write_text(
        "version = 2\n"
        "schema = \"dbz3-cac/2\"\n"
        "activo = 0\n\n"
        "[[heroe]]\n"
        "id = \"h130001\"\n"
        "esferas = 0\n"
        "deseos = 0\n"
        "desbloqueos = []\n"
        "nombre = \"Heroe\"\n"
        "inventario = []\n"
        "equipo = [0, 0, 0, 0]\n"
        "raza = 0\n"
        "cuerpo = 0\n"
        "traje = 0\n"
        "cara = 0\n"
        f"pelo = {pelo}\n"
        "ropa = 0\n"
        "color = 0\n"
        "voz = 0\n"
        "aura = 0\n"
        "aura_col = \"\"\n"
        "ki_col = \"\"\n"
        "estilo = 0\n"
        "moveset = -1\n"
        "tecnicas = [0, 0, 0, 0]\n"
        "xp = 1\n"
        "nivel = 1\n"
        "logo = 0\n"
        "scouter = 0\n"
        "partes = [[0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0]]\n"
        "xv2 = [0, 0, 0, 0, 0, 0, 0]\n"
        f"b3_piezas = [-1, -1, {guante}, -1]\n"
        "pelo_color1 = [0, 0, 0]\n"
        "pelo_color2 = [0, 0, 0]\n"
        "ojos_color = [0, 0, 0]\n"
        "piel_color = [0, 0, 0]\n"
        "progreso = 2\n"
        "saga = 0\n"
        "ph = 1\n"
        "ph_vida = 0\n"
        "ph_dano = 0\n"
        "maestros = 0\n"
        "entreno = [0, 0, 0, 0, 0, 0]\n",
        encoding="utf-8", newline="\n")
    s = ("cuerpo=0;traje=0;partes=0,0,0|0,0,0|0,0,0|0,0,0"
         f";pelo={pelo};c1=0,0,0;c2=0,0,0;logo=0;scouter=0")
    if guante >= 0:
        s += f";b3_guantes={guante}"
    return s + ";nombre=Heroe"


def _apagar_xv2(dest):
    """La inyeccion B3 solo se aplica al cuerpo nativo: piezas XV2 apagadas en la copia de pruebas."""
    p = Path(dest) / "dbz3_user.toml"
    try:
        lineas = p.read_text(encoding="utf-8", errors="surrogateescape").splitlines()
    except OSError:
        return
    if not any(l.strip().startswith("dbz3_cac_xv2") for l in lineas):
        lineas.append("dbz3_cac_xv2 = false")
    else:
        lineas = [("dbz3_cac_xv2 = false" if l.strip().startswith("dbz3_cac_xv2") else l) for l in lineas]
    p.write_text("\n".join(lineas) + "\n", encoding="utf-8", errors="surrogateescape")


def _quitar_partida_heroe(ruta):
    """Borra (con copia .bak) la partida a medias del Modo Heroe para que 'Empezar' cree una sesion limpia."""
    p = Path(ruta)
    if not p.is_file():
        return
    shutil.copy2(p, p.with_name(f"rpg.toml.ds130-{ts()}.bak"))
    lineas = p.read_text(encoding="utf-8", errors="surrogateescape").splitlines()
    salida, i = [], 0
    while i < len(lineas):
        if lineas[i].strip() == "[[partida]]":
            k = i + 1
            while k < len(lineas) and not lineas[k].lstrip().startswith("[["):
                k += 1
            bloque = lineas[i:k]
            if any(re.match(r"\s*modo\s*=\s*5\b", l) for l in bloque):
                i = k
                continue
            salida.extend(bloque)
            i = k
            continue
        salida.append(lineas[i])
        i += 1
    p.write_text("\n".join(salida) + "\n", encoding="utf-8", errors="surrogateescape")


def _heroe_pieza_prep(j, par, env):
    """Antes de lanzar: escribe la ficha con la pieza y monta mods/heroe_propio + _roster (como JUGAR)."""
    pelo, guante = _heroe_pieza_valores(par)
    dest = j.dest
    args = _heroe_pieza_receta(dest, pelo, guante)
    _apagar_xv2(dest)
    _quitar_partida_heroe(dest / "user_data" / "dbz3" / "rpg.toml")
    entorno = dict(os.environ, DBZ3_CAC_XV2="0", DBZ3_CAC_B3_TEST_TODOS="1")
    # Los scripts de la COPIA (como los lanza el juego en JUGAR/PedirCombate): asi lo generado (campanas, mods)
    # cae en la copia de pruebas y no en el proyecto.
    script = dest / "mod center hd" / "cac" / "vista_heroe.py"
    if not script.is_file():
        script = R / "mod center hd" / "cac" / "vista_heroe.py"
    cmd = [sys.executable, "-X", "utf8", str(script), "--juego", str(dest),
           "--ficha", args + ";donante=0", "--combate", "1"]
    r = subprocess.run(cmd, cwd=str(dest), env=entorno, capture_output=True, text=True, errors="replace", timeout=900)
    salida = (r.stdout or "") + (r.stderr or "")
    (j.run / "prep_vista_heroe.log").write_text(salida, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        raise Fallo(f"vista_heroe.py --combate fallo ({r.returncode}): {salida[-400:]}")
    if "combate de la ficha 1" not in salida:
        raise Fallo(f"vista_heroe.py no monto la ficha 1: {salida[-400:]}")
    if pelo > 0 and "'pelo': '" in salida and ("'pelo': 'FALLO" in salida or "'pelo': 'injerto nativo FALLO" in salida):
        raise Fallo("el pelo no se pudo injertar en el modelo de combate")
    if guante >= 0:
        if "'piezas_b3'" not in salida:
            raise Fallo("la pieza de guantes no llego al modelo de combate (sin 'piezas_b3')")
        if re.search(r"'guantes': '(Base|FALLO)", salida):
            raise Fallo("la pieza de guantes no se pudo injertar (Base/FALLO)")
    dis = dest / "mods" / "heroe_propio" / ".disabled"   # que el roster de la copia lo lea
    if dis.is_file():
        shutil.move(str(dis), str(j.run / "heroe_propio.disabled.bak"))
    rb_script = dest / "mod center hd" / "roster_build.py"
    if not rb_script.is_file():
        rb_script = R / "mod center hd" / "roster_build.py"
    rb = [sys.executable, str(rb_script), "construir", "--mods", str(dest / "mods")]
    r2 = subprocess.run(rb, cwd=str(dest), env=entorno, capture_output=True, text=True, errors="replace", timeout=900)
    (j.run / "prep_roster.log").write_text((r2.stdout or "") + (r2.stderr or ""), encoding="utf-8", errors="replace")
    if r2.returncode != 0:
        raise Fallo(f"roster_build.py fallo ({r2.returncode}): {(r2.stderr or r2.stdout)[-400:]}")
    ft = dest / "mods" / "heroe_propio" / "ficha.txt"
    if not ft.is_file():
        raise Fallo("no se monto mods/heroe_propio/ficha.txt")
    txt = ft.read_text(encoding="utf-8", errors="replace")
    if f"pelo={pelo}" not in txt:
        raise Fallo(f"la ficha montada no lleva pelo={pelo}: {txt[:160]}")
    if guante >= 0 and f"b3_guantes={guante}" not in txt:
        raise Fallo(f"la ficha montada no lleva b3_guantes={guante}: {txt[:160]}")
    j.info(f"pieza preparada: pelo={pelo} guante={guante} (heroe_propio + _roster)")


def _esperar_brazos_quietos(j, par):
    """DS155: espera a que la sonda confirme que el combate esta quieto y el heroe en reposo. No falla si no llega:
    las fotos se toman igual (el log de la sonda dira si engancho)."""
    if par.get("quieto", "1") == "0":
        return
    seg = max(0.0, float(par.get("brazo_quieto", "2.5")))
    fin = time.time() + seg
    while time.time() < fin and "sonda de brazo: quieto" not in j.texto_log():
        time.sleep(0.1)
    if "sonda de brazo: quieto" in j.texto_log():
        j.info("brazos: combate quieto y heroe en reposo (confirmado por la sonda)")
    else:
        j.info("brazos: aviso: la sonda no confirmo la pose de reposo; se toman las fotos igual")


def _fotos_brazos(j, par):
    """DS154/DS155: 4 planos de cada brazo (codo y mano, de frente y de espalda) para aprobar guantes/mangas.
    La camara de prueba (src/cinematica.cpp) cambia con dbz3_cam.req sin relanzar el juego:
    "<hueso> <vista> <dist> <lado>" (o "off" para volver a la camara del juego). DS155: antes de la primera foto se
    espera a que el combate quede quieto (mandos sin pulsaciones) y el heroe en reposo; el encuadre nuevo abarca del
    hombro a la mano entera y mira el brazo de lado (antes el ojo caia casi a lo largo del antebrazo). DS160: el ojo
    va siempre del lado de FUERA del brazo (el opuesto al torso) y las 4 fotos (codo/mano, de frente y de espalda)
    se toman de ese mismo lado; los valores por defecto son los del encuadre aprobado (lado 2, codo 7, mano 6)."""
    req = j.dest / "dbz3_cam.req"
    lado = par.get("brazo_lado", "2")           # DS160: los valores del encuadre aprobado (7 / 6 / 2)
    d_codo = par.get("brazo_dist_codo", "7")
    d_mano = par.get("brazo_dist_mano", "6")
    espera = float(par.get("brazo_espera", "0.2"))   # la sonda relee el req cada 6 cuadros (~0,05 s)
    vistas = (("codo", "frente", d_codo), ("codo", "espalda", d_codo),
              ("mano", "frente", d_mano), ("mano", "espalda", d_mano))
    primera = True
    for etiqueta in ("der", "izq"):
        for k, (hueso, vista, dist) in enumerate(vistas):
            tmp = req.with_name(req.name + ".tmp")       # os.replace: la sonda nunca lee el fichero a medias
            tmp.write_text(f"{hueso}_{etiqueta} {vista} {dist} {lado}\n", encoding="ascii")
            os.replace(tmp, req)
            if primera:                                  # DS155: que el combate se pare antes de la primera foto
                primera = False
                _esperar_brazos_quietos(j, par)
            dormir(espera)
            j.captura(f"h_brazo_{etiqueta}_{k:02d}")
            dormir(0.05)
    tmp = req.with_name(req.name + ".tmp")
    tmp.write_text("off\n", encoding="ascii")            # devuelve la camara al juego (andar/golpear)
    os.replace(tmp, req)
    dormir(0.4)
    lineas = [l for l in j.texto_log().splitlines() if "sonda de brazo" in l]
    if not lineas:
        raise Fallo("la camara de brazo no engancho (no hay 'sonda de brazo' en el log)")
    for l in lineas[-8:]:
        j.info(re.sub(r"^(\[[^\]]*\] ){3}", "", l)[:170])


def _heroe_pieza(j, par):
    # DS130: Modo Heroe con la ficha de la pieza pedida. Mismo camino que _r5_heroe hasta el combate; despues:
    # presentacion (primeros planos), primer plano de frente de la cabeza al empezar la pelea (DS141, camara
    # DBZ3_TEST_CARA), de pie, andando (D mantenido), golpeando (J) y rafaga dbz3_shot.req. Con --par guante=N
    # (DS154) ademas 4 primeros planos de cada brazo (codo y mano, de frente y de espalda) para la pieza.
    pelo, guante = _heroe_pieza_valores(par)
    dormir(2.5)
    j.tecla("W", 1200)
    j.tecla("Space", 3500)                              # HUB
    for _ in range(5):                                  # tagteam .. kumite
        j.tecla("S", 450)
    j.tecla("Space", 2500)                              # selector de retos
    for _ in range(3):                                  # kumite .. modo heroe
        j.tecla("S", 700)
    j.captura("a_selector_heroe")
    j.tecla("Space", 2500)                              # pantalla del heroe / creacion
    j.captura("b_heroe")                                # vista previa de la ficha (primer plano)
    for _ in range(3):
        j.tecla("S", 500)
    for _ in range(int(par.get("estilo", "0"))):
        j.tecla("D", 600)
    j.captura("b_estilo")
    j.tecla("S", 500)
    j.captura("c_empezar")
    j.tecla_hasta_log("Space", r"hub: combate nativo", 6, 8)
    j.log(r"dbz3 rpg: heroe: estilo", 40)
    for k in range(int(par.get("intro", "6"))):          # camara de presentacion: primeros planos
        j.captura(f"c_intro_{k:02d}")
        dormir(1.0)
    dormir(float(par.get("espera", "16")))              # pasa la presentacion
    j.captura("d_de_pie")
    # DS141: primer plano de FRENTE de la cabeza AL EMPEZAR la pelea (de pie, sin golpes: en el primer plano a los 8 s
    # el heroe ya esta siendo derribado y la camara sigue la cabeza hasta el suelo). La camara de prueba DBZ3_TEST_CARA
    # (src/cinematica.cpp) mira la cabeza real del luchador 1 (punto de golpe, calculado por el juego del esqueleto) a
    # `cara_dist` de frente (`cara_lado`), y se apaga sola a los `cara_duracion` cuadros para no tapar las tomas de
    # cuerpo. Con --par cara=0 el escenario queda exactamente como antes.
    if par.get("cara", "1") != "0":
        j.log(r"dbz3 cinematica: sonda de cara", 20)   # la sonda avisa en el log cuando engancha la cabeza
        for k in range(int(par.get("cara_fotos", "4"))):
            j.captura(f"h_cara_frente_{k:02d}")
            dormir(0.15)
    # DS154: con la pieza de guantes puesta, 4 primeros planos de cada brazo nada mas empezar (de pie, antes de los
    # golpes): codo de frente/espalda y mano de frente/espalda. La sonda cambia de articulacion con dbz3_cam.req y
    # "off" devuelve la camara del juego para e_andando/f_golpe/g_combate.
    con_brazos = guante >= 0 and par.get("brazo", "1") != "0"
    if con_brazos:
        if par.get("cara", "1") == "0":
            j.log(r"dbz3 cinematica: sonda de", 20)    # la sonda ya esta enganchada (el escenario la enciende)
        _fotos_brazos(j, par)
    if par.get("cara", "1") != "0" and not con_brazos:
        dormir(4.0)                                     # que se apague la sonda (4 s) antes de andar/golpear
    elif con_brazos:
        dormir(0.4)
    req = j.dest / "dbz3_input.req"
    while req.exists():                                 # que el juego consuma la tecla anterior
        time.sleep(0.05)
    req.write_text("D 3000\n", encoding="ascii")        # andando (mantener)
    dormir(0.8)
    j.captura("e_andando")
    dormir(2.1)
    for k in (1, 2):                                    # golpeando
        while req.exists():
            time.sleep(0.05)
        req.write_text("J 700\n", encoding="ascii")
        dormir(0.12)
        j.captura(f"f_golpe_{k}")
        dormir(0.8)
    j.captura("g_combate")
    antes = set(j.dest.glob("dbz3_shot_*.bmp"))
    (j.dest / "dbz3_shot.req").write_text("24 4", encoding="ascii")   # rafaga de fotos
    dormir(0.2)
    while req.exists():
        time.sleep(0.05)
    req.write_text("J 900\n", encoding="ascii")
    dormir(4.0)
    nuevos = sorted(set(j.dest.glob("dbz3_shot_*.bmp")) - antes)
    for p in nuevos:
        try:
            if Image:
                with Image.open(p) as im:
                    im.save(j.run / (p.stem + ".png"))
            else:
                shutil.copy2(p, j.run / p.name)
        except Exception as e:                          # noqa: BLE001 (una foto mala no tira la prueba)
            j.info(f"rafaga: {p.name} no se pudo copiar: {e}")
    if nuevos:
        j.info(f"rafaga: {len(nuevos)} fotos en {j.run}")
    log = j.texto_log()
    patron = rf"dbz3 heroe: ficha \d+ apariencia: pelo {pelo}, b3_guantes {guante}"
    if not re.search(patron, log):
        raise Fallo(f"el log no confirma la pieza cargada (pelo {pelo}, b3_guantes {guante})")
    ft = j.dest / "mods" / "heroe_propio" / "ficha.txt"
    txt = ft.read_text(encoding="utf-8", errors="replace") if ft.is_file() else ""
    if f"pelo={pelo}" not in txt or (guante >= 0 and f"b3_guantes={guante}" not in txt):
        raise Fallo(f"la ficha de combate no coincide con la pieza pedida: {txt[:160]}")
    for l in [l for l in log.splitlines() if "dbz3 heroe: ficha" in l or "b3_guantes" in l][-6:]:
        j.info(re.sub(r"^(\[[^\]]*\] ){3}", "", l)[:170])


def _du_continuar(j, par):
    # verificado 2026-10-15: DU (titulo, pantalla 6) -> Return -> menu (New Story) -> S = Continue -> rueda (7)
    # -> W x up (1 = Freezer desde Goku) -> Space -> mapa/escenas (8)
    j.tecla_hasta("Space", [6], 5, 4)                   # menu principal -> DU (la primera a veces no entra)
    dormir(3)                                       # "Pulsa START" no responde nada mas aparecer
    j.tecla("Return", 3000)                             # "Press START" -> menu de DU
    j.tecla("S", 1500)                                  # Continue
    j.tecla("Space", 3000)
    j.esperar_pantalla([7], int(par.get("seg", 40)))
    dormir(2.5)                                     # la rueda aun gira al aparecer: la W se perdia
    for _ in range(int(par.get("up", 1))):
        j.tecla("W", 2000)
    j.captura("00_rueda")
    j.tecla_hasta("Space", [8, 9], intentos=4, seg=max(10, int(par.get("seg", 40)) // 2))  # mapa, o combate directo (freezer_cap2 hoy: escena 5522); X0h: repite Space si se pierde
    j.captura("01_continuar")
    j.log(r"historias de mods recuperadas", 5)          # la casilla de mods vuelve de historias_extra.bin
    j.log(r"personaje del mapa al crear \(casilla 22", 40)
    for i in range(1, int(par.get("iter", 10)) + 1):
        j.tecla("Space", int(par.get("paso_ms", 3000)))
        if i % 3 == 0:
            j.captura(f"{i:03d}")
        if par.get("hasta_combate") and "tag: combate acabado" in j.texto_log():
            j.info(f"combate de la campana acabado en el paso {i}")
            return
    if par.get("hasta_combate"):
        raise Fallo("la campana no llego a acabar un combate")


def _capsulas_custom(j, abiertos, seg=25):
    """Menu de capsulas del jugador 1 (Tag Team): lleva el cursor a Custom (opcion 1) mirando el log y elige.
    A ciegas (S + Space) fallaba con 3 juegos abiertos: el menu se abria en otra opcion y el Space abria la
    bandeja de Edit Skills (2026-10-15)."""
    ya = j.texto_log().count("menu de capsulas del jugador 1: elegido")
    fin = time.time() + seg
    while j.texto_log().count("menu de capsulas del jugador 1 abierto") < abiertos:   # el de este miembro
        if time.time() > fin:
            raise Fallo("no se abrio el menu de capsulas del jugador 1")
        dormir(0.25)
    dormir(1.0)                                     # su primera "opcion N" llega justo despues
    while time.time() < fin:
        t = j.texto_log()
        if t.count("menu de capsulas del jugador 1: elegido") > ya:
            return
        ops = re.findall(r"menu de capsulas del jugador 1: opcion (\d)", t)
        cur = int(ops[-1]) if ops else 1
        n_ops = len(ops)
        j.tecla("W" if cur > 1 else "S" if cur < 1 else "Space", 0)
        # X0h: esperar a que el juego conteste (nueva 'opcion N' o 'elegido') en vez de un tiempo fijo: con turbo el tiempo
        # fijo mandaba la tecla siguiente antes de que el menu (de pared) hubiera movido el cursor
        fin_k = time.time() + 3
        while time.time() < fin_k:
            tt = j.texto_log()
            if tt.count("menu de capsulas del jugador 1: elegido") > ya or len(re.findall(
                    r"menu de capsulas del jugador 1: opcion (\d)", tt)) > n_ops:
                break
            time.sleep(0.05)
    raise Fallo("el menu de capsulas del jugador 1 no llego a elegir Custom")


def _esperar_pelea(j, seg=60):
    """Espera el estado 4 del combate (pelea: mgr = *0x823D6CF4, estado mgr+35128; misma direccion en US y EU).
    Con 3 juegos abiertos la presentacion tarda mas y 8 s fijos dejaban el SELECT en ella (tag_swap en FAIL)."""
    fin = time.time() + seg
    while time.time() < fin:
        b = leer(j.h, GBASE + 0x823D6CF4, 4)
        mgr = int.from_bytes(b, "big") if b and len(b) == 4 else 0
        e = leer(j.h, GBASE + mgr + 35128, 4) if mgr else None
        if e and len(e) == 4 and int.from_bytes(e, "big") == 4:
            return
        dormir(0.25)
    raise Fallo("el combate no llego a la pelea (estado 4)")


def _tag_swap(j, par):
    swap = par.get("swap", "1") == "1"
    j.tecla("L", 3000)
    j.captura("02_tagteam")
    j.tecla("Space", 12000)
    j.captura("03_rueda")
    j.tecla("Space", 4000)
    j.captura("04_capsulas")
    _capsulas_custom(j, 1)
    j.captura("05_m1")
    j.tecla("S", 2000)
    j.tecla("Space", 4000)
    _capsulas_custom(j, 2)
    j.captura("06_m2")
    for _ in range(int(par.get("rival_skip", 0))):
        j.tecla("S", 1500)
    j.tecla("S", 2000)
    j.tecla("Space", 4000)
    j.tecla("Space", 6000)
    j.captura("07_r1")
    j.tecla("S", 2000)
    j.tecla("Space", 4000)
    j.tecla("Space", 6000)
    j.captura("08_r2")
    for _ in range(5):
        j.tecla("Space", 3000)
    j.log("tag: empieza un combate", 40)                # antes: 12 s fijos y luego el log
    _esperar_pelea(j)                                   # el SELECT durante la presentacion no releva
    dormir(1)
    j.captura("09_combate")
    if swap:
        j.tecla_hasta_log("Z", r"relevo .* -> ")        # relevo pedido con SELECT, repetido si no llega
    else:
        for _ in range(60):                             # golpear hasta el KO del rival
            j.tecla("D 400", 100)
            for _ in range(3):
                j.tecla("L", 150)
            if re.search(r"tag: KO de|relevo .* -> ", j.texto_log()):
                break
    j.log(r"tag: KO de|relevo .* -> ", 15)
    for i in range(int(par.get("rafaga", 12)) + 1):
        j.captura(f"10_{i:02d}")
        dormir(0.12)
    dormir(3)
    j.captura("11_despues")


def _a_duelo(j):
    """Menu principal (cursor en Dragon Universe) -> Duelo (pantalla 14). La primera tecla nada mas llegar al menu
    se pierde (medido 2026-10-15: la S se perdio y el Space de despues entro en Dragon Universe)."""
    dormir(2.5)
    j.tecla("S", 1200)
    j.tecla("Space", 0)
    j.esperar_pantalla([14, 6], 8)
    if j.pantalla() != 14:
        raise Fallo(f"la S no movio el cursor del menu principal: entro en la pantalla {j.pantalla()}")
    dormir(1.5)


_SOLO_UNO_ONLINE = threading.Lock()


def _online_crear(j, par):
    # Bug 2026-10-15: tras elegir un equipo en el Tag Team de 1P, CREAR PARTIDA fallaba hasta reiniciar
    # ("F4: Tag Team necesita prueba..."). DBZ3_NET_TEAM hace de ese equipo; DBZ3_NET_LOCAL: sin UPnP ni STUN
    # (la prueba cierra el juego a la fuerza y no debe dejar puertos abiertos en el router).
    with _SOLO_UNO_ONLINE:                              # X1: dos copias creando sala a la vez chocan en el puerto 7777
        _a_duelo(j)
        for _ in range(4):                              # 1P VS COM ... COM VS COM -> fila ONLINE
            j.tecla("S", 700)
        j.captura_estable("01_fila_online")
        j.tecla("Space", 1500)                          # abre la pantalla ONLINE
        j.captura_estable("02_online")
        j.tecla("Space", 2000)                          # CREAR PARTIDA (primera fila)
        j.log(r"dbz3 online: (partida creada|no se pudo crear)", 10)
        j.captura_estable("03_crear")
    texto = j.texto_log()
    if "no se pudo crear" in texto:
        raise Fallo("CREAR PARTIDA fallo: " + re.findall(r"no se pudo crear la partida: (.*)", texto)[-1][:120])
    if par.get("equipo", "1") == "1" and "no viaja al online" not in texto:
        raise Fallo("el equipo de prueba (DBZ3_NET_TEAM) no llego a Host()")


def _rafaga(j, prefijo, seg):
    """Capturas seguidas durante seg (el nombre lleva los ms desde el principio de la rafaga)."""
    t0 = time.time()
    while time.time() - t0 < seg:
        j.captura(f"{prefijo}_{int((time.time() - t0) * 1000):05d}")


def _duelo_fundido(j, par):
    # Fallo visto por el frente HUB (2026-10-15): el recuadro ONLINE del Duelo salia sobre el negro del fundido al
    # entrar y al salir. Rafagas de capturas: entrada desde el menu principal, salida con Y (P) y con B
    # (Backspace) desde la fila ONLINE elegida.
    dormir(2.5)                                     # la primera tecla en el menu principal se pierde
    j.tecla("S", 1200)
    j.tecla("Space", 0)
    j.esperar_pantalla([14], 8)
    _rafaga(j, "a_entrada", float(par.get("seg", 3.5)))
    dormir(1)
    j.tecla("P", 0)                                     # Y: atras
    _rafaga(j, "b_salida_y", 2.5)
    j.esperar_pantalla([5], 10)
    dormir(2.5)
    j.tecla("Space", 0)                                 # el menu principal recuerda la fila del Duelo
    j.esperar_pantalla([14], 8)
    dormir(3.5)
    for _ in range(4):                                  # 1P VS COM ... COM VS COM -> fila ONLINE
        j.tecla("S", 700)
    j.captura("c_fila_online")
    j.tecla("Backspace", 1500)                          # B desde la fila ONLINE: solo la suelta (el Duelo sigue)
    j.captura("d_fila_suelta")                          # la fila ONLINE sigue a la vista
    j.tecla("P", 0)                                     # Y: fuera del Duelo
    _rafaga(j, "e_salida_y", 2.5)
    j.esperar_pantalla([5], 10)


def _hub_online_fundido(j, par):
    # HUB > Online: el Duelo llega en negro y la pantalla ONLINE se abria encima del negro; ahora espera a que el
    # Duelo se vea y entra fundida. Luego B (cierra la pantalla) e Y (sale del Duelo) con rafagas.
    dormir(2.5)
    j.tecla("W", 1200)                                  # desde la fila 0, arriba = fila HUB (la ultima)
    j.tecla("Space", 3500)                              # pantalla HUB (cursor en Tag Team)
    j.captura("a_hub")
    j.tecla("S", 1200)                                  # Online
    j.tecla("Space", 0)
    j.esperar_pantalla([14], 10)
    _rafaga(j, "b_llegada", float(par.get("seg", 4.5)))
    j.log(r"se abre la pantalla ONLINE", 5)
    dormir(1)
    j.tecla("Backspace", 1500)                          # cierra la pantalla ONLINE
    j.captura("c_duelo")
    j.tecla("P", 0)                                     # Y: fuera del Duelo
    _rafaga(j, "d_salida", 2.5)


def _historia_y_duelo(j, par):
    # Bug 2026-10-15: la forma inicial y el traje del rival de un combate de historia de mod ([forma_jugador],
    # [traje_rival]) se quedaban para el siguiente combate fuera de Dragon Universe. Salir de DU a mano no es
    # practicable (la historia de Freezer encadena combates), asi que DBZ3_TEST_STORY_WORD hace como si se volviera
    # de DU tras el combate 5522.6 (Freezer forma 3 contra Vegeta con traje 3) al llegar al menu principal, y
    # DBZ3_TEST_BATTLE lanza alli un combate normal (Goku contra Vegeta). Con --par antes=1 (DBZ3_TEST_KEEP_STORY) se
    # ve el fallo de antes. Que los combates normales no tocan esa palabra lo dicen los logs: 0 "se lanza la escena".
    j.log(r"hub: combate nativo", 30)
    j.log(r"tag: empieza un combate", 40)
    dormir(2)
    j.captura("01_combate_duelo")
    texto = j.texto_log()
    despues = texto[texto.rfind("hub: combate nativo"):]
    heredado = re.findall(r"tu personaje empieza en la forma \d+|el rival lleva el traje \d+", despues)
    if par.get("antes") == "1":
        j.info(f"comportamiento de antes: {heredado or 'nada heredado'}")
        if not heredado:
            raise Fallo("con DBZ3_TEST_KEEP_STORY el combate deberia heredar la forma/traje (la prueba no reproduce)")
    elif heredado:
        raise Fallo(f"el combate normal hereda del combate de historia: {heredado}")
    elif "se olvida el ultimo combate de historia" not in texto:
        raise Fallo("el menu principal no olvido el combate de historia")


def _cam_seq(j):
    # CM 24/10: misma secuencia que tools/pruebas_cam/combate_camara.toml (sin la espera larga) para medir la camara
    # en combates que salen de los modos (tablero, IW, Satan). Despues: tools/pruebas_cam/detector.py <run>.
    dormir(1.5)
    for k, e in (("Space", 800), ("Return", 800), ("Space", 700)):
        j.tecla(k, e)
    j.captura("c03")
    dormir(5)
    j.captura("c08")
    dormir(7)
    j.captura("c15")
    j.tecla("D:1500", 1600)
    j.tecla("Space+L", 300)
    j.captura("g1")
    j.tecla("Space+L", 300)
    dormir(0.7)
    j.captura("g2")
    j.tecla("D:1500", 1600)
    j.tecla("Space+L", 300)
    dormir(0.5)
    j.captura("g3")
    dormir(1.5)
    j.captura("g4")
    j.tecla("A:1200", 1300)
    j.tecla("D:2500", 2600)
    j.tecla("Space+L", 300)
    dormir(0.8)
    j.captura("g5")
    dormir(2)
    j.captura("g6")


def _satan(j, par):
    # Leyenda de Mr. Satan (src/pasarela.cpp, DBZ3_TEST_PASARELA=auto,seg=8): empieza sola desde el menu principal y
    # avanza cada 8 s; RINGOUT gana cada combate a los 5 s. desde=N empieza en el paso N (25 = Cell, el ultimo ->
    # final). Orden de B1 (F6): titulo, dificultad, prologo, Yamcha sin tarjeta, resultado con la cuenta, tablero de
    # 11 caras y la escena 3D del retador. El registro llega con unos segundos de retraso.
    d = int(par.get("desde", "0"))
    j.log(r"pasarela: prueba: disponible true; empieza", 90)
    j.log(rf"pasarela: paso {d + 1}/\d+", 20)
    dormir(3)
    j.captura("a_entrada")
    if d == 0:
        j.log(r"pasarela: titulo: START", 30)
        dormir(3)
        j.captura("a2_dificultad")
        j.log(r"pasarela: prologo 2/7", 40)
        dormir(1)
        j.captura("b_prologo")
    else:
        j.log(r"hub: combate nativo", 40)
        j.log(r"pasarela: final de la leyenda", 90)
        dormir(2)
        j.captura("e_final_cuenta")
        dormir(6)
        j.captura("f_final_total")
        return
    j.log(r"hub: combate nativo", 60)
    if par.get("cam") == "1":
        j.log(r"pasarela: paso \d+ estado del combate 4", 90)   # tras la cinematica de entrada: pelea de verdad
        return _cam_seq(j)
    if par.get("golpes") == "1":                    # P3: Mr. Satan pega (puntos HIT/COMBO de B1 y su aviso)
        j.log(r"pasarela: paso \d+ estado del combate 4", 60)
        j.tecla("Right 700", 100)
        for k in ["L", "L", "L", "P", "L", "L", "P", "L"]:
            j.tecla(k, 220)
        j.captura("c_puntos")
    if par.get("cell") == "1":                      # s9b: escena de los Juegos de Cell (7 pasos) antes del combate 1
        for k in range(1, 8):
            j.log(rf"cinematica: paso {k}/", 90 if k == 1 else 40)
            dormir(1.6)
            j.captura(f"c_cell{k}")
    dormir(4)
    j.captura("c_combate")
    j.log(r"\): resultado", 150)
    dormir(1)
    j.captura("d_resultado")
    j.log(r"pasarela: paso 3/\d+ \(charla", 40)
    dormir(3)
    j.captura("e_tablero")
    j.log(r"cinematica: paso 1/", 60)                # la escena del retador va tras su tablero (RINGOUT la corta)
    dormir(1.5)
    j.captura("f_escena")


def _r3_cine(j, par):
    # frente R3/F6 (src/cinematica.cpp): escena 3D del retador tras su tablero. desde=2 = tablero de Ten Shin Han (1.a
    # charla con texto y voces) y su escena; desde=1 = Yamcha: sin escena (B1 no tiene charla para el 1.o). Sin
    # RINGOUT para que la escena se vea entera. prologo=1: el "Mr. Satan's Style".
    d = int(par.get("desde", "2"))
    j.log(r"pasarela: prueba: disponible true; empieza", 90)
    if par.get("prologo") == "1":
        for k in range(4):
            j.log(rf"pasarela: prologo {k + 1}/", 40)
            dormir(2)
            j.captura(f"p{k + 1}_prologo")
        return
    if d == 1:
        j.log(r"hub: combate nativo", 60)
        j.log(r"pasarela: paso \d+ estado del combate 4", 60)
        dormir(6)
        j.captura("a_yamcha_sin_escena")
        return
    if d == 2:
        j.log(r"pasarela: paso 3/\d+ \(charla", 40)
        dormir(3)
        j.captura("b_tablero")
    j.log(r"hub: combate nativo", 60)
    if par.get("ki") == "1":                        # sonda de la carga de ki: 11 combinaciones de 2,5 s
        for k in range(1, 12):
            j.log(rf"cinematica: paso {k}/", 120 if k == 1 else 30)
            dormir(1.8)
            j.captura(f"k{k:02d}")
        return
    if par.get("ringout") == "1":                   # victoria nativa tras la escena cortada (sonda de codigos)
        j.log(r"cinematica: (terminada|cortada)", 120)
        for i in range(4):
            dormir(2)
            j.captura(f"v{i}_victoria")
        return
    espera = {1: 1.5, 2: 3.0, 3: 1.5, 4: 1.5}
    for k in range(1, 5):
        j.log(rf"cinematica: paso {k}/", 60)
        dormir(espera[k])
        j.captura(f"c{k}_paso")
    j.log(r"cinematica: terminada", 30)
    dormir(3)
    j.captura("d_pelea")


def _r3_golpes(j, par):
    # frente R3: combo del jugador (Fin::combo_max, para R1) y codigo de la carga de ki (sonda DBZ3_CINE_TRAZA): Leyenda
    # de Mr. Satan contra Yamcha sin escena, el rival es el muneco quieto; Y+B (P+Backspace) mantenido y luego un combo
    j.log(r"pasarela: prueba: disponible true; empieza", 90)
    j.log(r"pasarela: paso \d+ estado del combate 4", 120)
    dormir(1)
    j.tecla("P+Backspace 2500", 300)
    j.captura("a_ki")
    dormir(1)
    j.tecla("Right 700", 100)
    for k in ["L", "L", "L", "P", "L", "L", "P", "L"]:
        j.tecla(k, 220)
    j.captura("b_combo")
    j.log(r"combo mayor del jugador [1-9]", 20)


def _fusion_yamhan(j, par):
    # frente B (src/fusion.cpp): combate normal Yamcha contra Vegeta (DBZ3_TEST_BATTLE); DBZ3_TEST_FUSION=3 hace que
    # el lado 1 se fusione a los 3 s de pelea con la carga del relevo: Yamcha pasa a Yamhan sin cerrarse
    j.log(r"tag: empieza un combate", 60)
    _esperar_pelea(j)
    j.captura("01_yamcha")
    if par.get("gesto") == "1":                     # P+K+G de verdad (X+A+B) mantenido 1,5 s, sin DBZ3_TEST_FUSION
        # P+K+G en el teclado = P+L+Space (Y+X+A, botones D000 en el combate; sonda DBZ3_FUSION_SONDA). X+A+B
        # (L+Space+Backspace, E000) y Y+X+B (B000) no lo son. Se mantiene 2 s y al soltar sale la fusion
        for _ in range(3):                          # (la pulsacion del arnes a veces no llega entera)
            dormir(1)
            j.tecla("P+L+Space 2000", 800)
            if re.search(r"fusion: lado 1 mantiene", j.texto_log()):
                break
        j.log(r"fusion: lado 1 mantiene P\+K\+G", 10)
    j.log(r"fusion: lado 1: id 13 -> Yamhan", 30)
    j.log(r"tag: luchador .* en combate: id \d+", 30)
    for i in range(6):
        j.captura(f"02_{i:02d}")
        dormir(0.3)
    dormir(4)
    j.captura("03_yamhan")
    j.log(r"tag: pose de entrada|fusion", 5)


def _bordes(j, par):
    # frente C (SDK, dbz3_texture_pack.cpp): con "Bordes dorados" apagado la tira "borde" de los menus del juego se
    # sube transparente. --par bordes=true|false (REX_DBZ3_UI_SIDE_BORDERS). Menu principal y Duelo.
    dormir(3)
    j.captura("a_menu_principal")
    _a_duelo(j)
    j.captura("b_duelo")
    if par.get("bordes", "false") == "false":
        j.log(r"pack 'bordes dorados apagados' (reemplaza|subido)", 10)


def _tablero(j, par):
    # frente P2 (ronda 3): Pasarela > Dragon World de B2 (DBZ3_TEST_MISIONES=1,reset lo abre desde el menu principal
    # en la fase 1). Todo de la ISO de B2: mapa cenital 346 en perspectiva, fichas 342, iconos 398, #YDW 387.
    # Preparacion (fase y companero) -> Space empieza -> W elige la casilla de arriba (2) -> Space mueve a Goku ->
    # Backspace (B, el circulo de B2) pasa con el companero -> S elige la casilla 3 (Saibaman) -> Space = contacto "!!" -> tarjeta -> Space = combate.
    j.log(r"tablero b2: fase 1 leida", 90)
    dormir(3)
    j.captura("a_preparacion")
    j.tecla("Space", 2500)
    # R7: la intro del Mundo Dragon (DATA_<idioma> 155) tapa el tablero: B la salta; luego los guiones del turno 1
    # (sus ventanas se cierran solas a los 3 s) tienen que acabar antes de poder mover
    j.log(r"tablero b2: escena T155", 15)
    j.tecla("Backspace", 1500)
    j.captura("a1_llegada")   # R7: efectos del banco 343 (capsulas de Raditz y Nappa)
    j.log(r"tablero b2: escena M207", 30)
    dormir(1.5)
    j.captura("a1b_charla")   # cabezas de B2 (CMN 1196)
    # orden 4 del guion 301: ventana de mensaje (0 y 78) que espera a A antes de la orden 69. El test antiguo
    # no la cerraba y se quedaba 60 s esperando la eleccion (fallo de tiempo, no del juego).
    j.tecla_hasta_log("Space", r"tablero b2: script 301: eleccion de 1 aliado", intentos=10, seg=4)
    # orden 69 del guion 301: eleccion de 1 aliado (2 en las fases IV, V y IX); Space elige el primero
    dormir(1)
    j.captura("a2_aliado")
    # desde el 22/10 hay "Is this okay?" (FUN_002A1640) tras marcar, con "No" resaltado: Space marca, W pasa a Si, Space confirma
    j.tecla_hasta_log("Space", r"tablero b2: aliado marcado", intentos=4, seg=3)
    dormir(0.5)
    j.tecla("W", 400)
    j.tecla_hasta_log("Space", r"tablero b2: aliado elegido", intentos=2, seg=3)
    j.log(r"tablero b2: guiones terminados", 60)
    dormir(1)
    j.captura("b_tablero")
    # pasarela_b2.cpp (~4860-4935): al empezar Goku ya va seleccionado (g_sel); W en ese paso cambia la ficha al companero,
    # por eso ya no se pulsa. Space (A) entra en "elegir casilla" (destino = primera vecina); otro Space (A) mueve.
    j.captura("c_destino")
    j.tecla("Space", 1500)
    j.tecla_hasta_log("Space", r"tablero b2: turno 1: .* a la casilla", intentos=3, seg=4)
    j.captura("d_movimiento")
    # F4: triangulo (P = Y) abre el menu del original (Salir / Reiniciar) y su Si/No; P vuelve atras dos veces
    j.tecla("P", 900)
    j.log(r"tablero b2: menu abierto", 10)
    j.captura("d2_menu")
    j.tecla("Space", 900)
    j.captura("d3_si_no")
    j.tecla("P", 600)
    j.tecla("P", 900)
    # R7: los Saibaman de la fase I salen ahora de sus guiones (ya no hay uno fijo en la casilla 3); Raditz (movimiento
    # 5 = sigue a la pieza 1, Goku) viene hacia Goku: se pasa turno (B) hasta el contacto. A cierra antes las ventanas
    # de mensaje del guion (ordenes 4/6): con una abierta, B no pasa turno y el test se quedaba sin contacto.
    # Dos B seguidas: la primera cancela el destino (si lo hay) y la segunda pasa turno. Space (A) solo cuando el log
    # tiene el contacto: su ventana de texto no la cierra B. Space suelto entre B y B creaba un ciclo destino/cancelar
    # que nunca pasaba turno (pasarela_b2.cpp ~4938). 40 vueltas: el turno rival tarda varios segundos.
    for _ in range(40):
        j.tecla("Backspace", 2500)
        j.tecla("Backspace", 2500)
        txt = j.texto_log()
        if re.search(r"tablero b2: combate:", txt):
            break
        if re.search(r"tablero b2: contacto ", txt):
            j.tecla("Space", 800)
    j.log(r"tablero b2: combate:", 20)
    j.captura("e_contacto")
    dormir(2)
    j.captura("f_tarjeta")
    j.tecla("Space", 1000)
    j.log(r"misiones: tablero combate", 20)
    if par.get("cam") == "1":
        return _cam_seq(j)
    dormir(24)
    j.captura("g_combate")


def _r2_nave(j, par):
    # X0h: flujo actual de la nave (A2nave, fines_nave.py): 1P/2P -> torre de plantas -> rueda 1P -> menu -> rueda COM ->
    # burbuja -> tarjeta -> combate (acotado por DBZ3_TEST_NAVE_TIEMPO, 6 s) -> resultado. --par prueba=0..3 = planta I..IV.
    # El flujo antiguo (lista de pruebas + S S) ya no existe: por eso fallaba hasta a x1.
    # DS131: se pulsa siguiendo las pantallas del log y sus esperas reales (DS127, 0x263A80 = 60 cuadros de rueda,
    # 0x2629B0 = 31 de 1P/2P, burbuja 45, salida de la lista 21). Antes iba a ciegas con 1500/600 ms y un Space se
    # perdia al entrar en la rueda (necesita 1 s); el guion acababa un pantalla antes y la tarjeta VS ("A ¡A luchar!")
    # se quedaba esperando su A. El ultimo Space de ahora es el de la tarjeta, como en el original.
    j.log(r"nave: entrar", 90)
    dormir(3.5)
    j.captura("a_nave")
    j.log(r"nave: pantalla modo", 20)
    dormir(0.8)                                 # 0x265310: sin mandos hasta 31 cuadros
    j.tecla("Space", 900)                       # 1P/2P -> torre
    j.log(r"nave: pantalla planta", 20)
    dormir(1.1)                                 # 0x264D00: la lista no lee mandos hasta 61 cuadros (la entrada)
    for _ in range(3):
        j.tecla("W", 300)
    for _ in range(int(par.get("prueba", "2"))):
        j.tecla("S", 300)
    dormir(0.3)
    j.tecla("Space", 400)                       # -> rueda 1P (salida de la lista: 21 cuadros)
    j.log(r"nave: pantalla rueda 1P", 20)
    dormir(1.1)                                 # 0x263A80: la rueda no acepta hasta 60 cuadros
    j.tecla("Space", 400)                       # -> menu
    j.log(r"nave: pantalla menu", 20)
    dormir(0.4)
    j.tecla("Space", 400)                       # -> rueda COM
    j.log(r"nave: pantalla rueda COM", 20)
    dormir(1.1)                                 # 60 cuadros de la rueda del COM
    j.tecla("Space", 400)                       # -> burbuja
    j.log(r"nave: pantalla burbuja", 20)
    dormir(0.9)                                 # la burbuja acepta desde 45 cuadros
    j.captura("b_burbuja")
    j.tecla("Space", 800)                       # elige el escenario -> tarjeta VS
    j.log(r"nave: prueba \d+:", 20)
    j.log(r"misiones: nave paso \d+/\d+ \(combate\): pantalla", 20)
    dormir(0.4)
    j.captura("c_tarjeta")
    j.tecla("Space", 800)                       # tarjeta VS -> combate ("A ¡A luchar!")
    j.log(r"nave: fin:", 240)
    # fin con 'gana el lado 0' = ganador -1: se acabo el tiempo (6 s) sin que nadie perdiera vida o salio por la pausa; el
    # juego vuelve a la torre SIN pantalla de resultado (Resultado() en pasarela_nave.cpp). Es una salida valida, no un fallo.
    if "nave: fin: gana el lado 0," not in j.texto_log():
        j.log(r"nave: resultado de la prueba", 30)
    else:
        j.log(r"nave: pantalla planta", 30)
        j.info("combate sin ganador (tiempo): vuelve a la torre sin resultado")
    dormir(2)
    j.captura("d_resultado")


def _luna13_b2_vineta(j, par):
    # Luna 13: reproduce la navegación manual de _tablero en la fase pedida (IV o IX).
    fase = int(par.get("fase", "4"))
    if fase not in (4, 9):
        raise Fallo("luna13_b2_vineta solo admite fase=4 o fase=9")
    j.log(r"tablero b2: fase %d leida" % fase, 90)
    j.captura("a_presentacion")
    # Igual que _tablero: avanzar las ventanas del guion hasta la selección de aliados.
    j.tecla_hasta_log("Space", r"tablero b2: script 301: eleccion de 2 aliado", intentos=12, seg=4)
    dormir(0.5)
    j.captura("b_aliados")
    j.tecla_hasta_log("Space", r"tablero b2: aliado marcado: .* quedan 1", intentos=4, seg=3)
    j.tecla("S", 450)                              # segunda ficha, siguiendo el control de _tablero
    j.tecla_hasta_log("Space", r"tablero b2: aliado marcado: .* quedan 0", intentos=4, seg=3)
    j.tecla("W", 400)                              # el original deja Sí arriba en la confirmación
    j.tecla_hasta_log("Space", r"tablero b2: aliado elegido", intentos=3, seg=4)
    j.log(r"tablero b2: guiones terminados", 90)
    dormir(0.8)
    j.captura("c_tablero")
    if fase == 9:
        # Reproduce las teclas y tiempos de tools/dorados/b2_fase9.json tras elegir aliados:
        # primer A -> mapa (p1); segundo A -> ventana cómica P (p2); tercero A -> Battle! (p3).
        for nombre, espera in (("p1", 4.0), ("p2", 9.0), ("p3", 9.0)):
            j.tecla("Space", 0)
            dormir(espera)
            j.captura(nombre)
            if nombre == "p2":                         # captura medida: Goku vs. Saibaman en el panel cómico
                j.captura("vineta_fase_ix")
                return
    # Mismo patrón de _tablero: A elige destino; repetir pulsaciones cortas de A hasta registrar el contacto P.
    j.tecla("Space", 1500)
    j.tecla_hasta_log("Space", r"tablero b2: escena P\d+", intentos=12, seg=4)
    dormir(0.5)
    j.captura("vineta_fase_%s" % ("iv" if fase == 4 else "ix"))


def _luna13_nave_rueda(j, par):
    # Luna 13: el mismo recorrido que _r2_nave, detenido en la rueda 1P para capturar a Goku.
    j.log(r"nave: entrar", 90)
    dormir(3.5)
    j.captura("a_nave")
    j.tecla("Space", 900)                       # 1P/2P -> torre
    for _ in range(3):
        j.tecla("W", 300)
    for _ in range(int(par.get("prueba", "2"))):
        j.tecla("S", 300)
    j.log(r"nave: pantalla planta", 20)
    j.tecla("Space", 1500)                      # -> rueda 1P
    # Muestreo fino para fijar el instante de captura respecto al ciclo de animación de Goku.
    limite = time.time() + 30
    while not re.search(r"nave: pantalla rueda 1P", j.texto_log()):
        if time.time() >= limite:
            raise Fallo("no llegó a la rueda de selección 1P")
        time.sleep(0.02)
    j.info("log ok: nave: pantalla rueda 1P")
    dormir(1.5)
    j.captura("rueda_goku")


def _r2_fase1(j, par):
    # frente R2 (ronda 4): la fase I del Dragon World entera en automatico (DBZ3_TEST_MISIONES=1,auto,reset: Goku va
    # al rival mas cercano; DBZ3_TEST_RINGOUT gana cada combate a los 5 s). Presentacion del script 301, combates,
    # sucesos de contacto (T4/T5) y "Fase completada".
    j.log(r"tablero b2: script 301: mensaje", 120)
    dormir(1)
    j.captura("a_presentacion")
    j.log(r"misiones: tablero combate", 120)
    dormir(20)
    j.captura("b_combate")
    j.log(r"tablero b2: .*fase 1 completada", 1500)
    dormir(0.4)   # el automatico pasa de pantalla a los 1,4 s
    j.captura("c_fase_completada")
    j.log(r"tablero b2: fase 2 leida", 30)
    dormir(1.5)
    j.captura("d_fase2_presentacion")


def _r2_deseo(j, par):
    # frente R2 (ronda 4): los deseos de Shenron (DBZ3_TEST_B2_DESEO=1 abre su pantalla al entrar al tablero) y el
    # "Portal de la nave de Babidi", que abre la Nave en la Pasarela.
    j.log(r"tablero b2: fase 1 leida", 90)
    dormir(3)
    j.captura("a_te_concedere")
    j.tecla("Space", 1000)
    j.captura("b_lista")
    j.tecla("S", 800)
    j.captura("c_otro_deseo")
    j.tecla("W", 800)
    j.tecla("Space", 1200)
    j.log(r"deseo: portal de la nave de Babidi", 10)
    j.captura("d_aceptable")
    j.tecla("Space", 1200)
    j.captura("e_debo_irme")
    j.tecla("Space", 2500)
    j.captura("f_fase_1")


def _r2_fases(j, par):
    # frente R2 (ronda 4): las fases del Dragon World seguidas en automatico desde la fase `desde` (1 = la II) hasta la
    # IX: la ficha va a la Bola de la fase y luego al rival mas cercano; DBZ3_TEST_RINGOUT gana los combates.
    desde = int(par.get("desde", "1"))
    hasta = int(par.get("hasta", "9"))   # --par hasta=N: para tras la fase N (una fase por prueba)
    for n in range(desde + 1, hasta + 1):
        j.log(r"tablero b2: .*fase %d completada" % n, 1200)
        dormir(0.4)
        j.captura("fase_%d_completada" % n)


def _b2_fase5_derrota(j, par):
    # B4 (DS146 §5, DS125 §3.3): fase V perdida a proposito. El guion 306 de la fase trae la orden 43 (derrota) al
    # final; DBZ3_TEST_B2_EVENTO=306 lo arranca al cargar la fase (pasarela_b2.cpp:1306). A en el GAME OVER (404 #0,
    # pasarela_b2.cpp:7151) reintenta la fase (pasarela_b2.cpp:7182).
    j.log(r"tablero b2: fase 5 leida", 90)
    dormir(2)
    j.captura("a_fase5")
    j.log(r"tablero b2: script 306: orden 43, fase 5 perdida", 300)   # pasarela_b2.cpp:1930
    dormir(0.5)
    j.captura("b_game_over")
    j.tecla("Space", 1500)
    j.log(r"tablero b2: reintento de la fase 5 \(A en GAME OVER\)", 30)   # pasarela_b2.cpp:7182
    j.log(r"tablero b2: fase 5 empieza", 30)                              # pasarela_b2.cpp:1319
    dormir(1)
    j.captura("c_reintento")


def _b2_fase9_final(j, par):
    # B7 (DS146 §5, DS125 §3.5): final de la fase IX. Ganada la fase, el guion 303 pide la orden 42
    # (pasarela_b2.cpp:1938); A en la pantalla de fase completada abre Shenron si estan las 7 Bolas
    # (pasarela_b2.cpp:7169-7173) o borra la partida del Mundo Dragon y vuelve a la fase I si falta alguna
    # (pasarela_b2.cpp:7176-7178). --par bolas=1 fuerza las 7 con DBZ3_TEST_B2_BOLAS (pasarela_b2.cpp:1284).
    con_bolas = par.get("bolas", "0") == "1"
    j.log(r"tablero b2: fase 9 leida", 90)
    j.log(r"tablero b2: script \d+: orden 42, fase 9 completada", 900)
    dormir(0.4)
    j.captura("a_fase9_completada")
    j.tecla("Space", 1500)
    if con_bolas:
        j.log(r"tablero b2: Shenron: \d+ deseos", 30)          # pasarela_b2.cpp:7173
        dormir(1)
        j.captura("b_shenron")
    else:
        j.log(r"tablero b2: fase 1 leida", 90)                 # pasarela_b2.cpp:995 (partida borrada)
        dormir(1)
        j.captura("b_partida_borrada")
        if re.search(r"tablero b2: Shenron: \d+ deseos", j.texto_log()):
            raise Fallo("sin las 7 Bolas salio la pantalla de Shenron")


def _b2_ataque_manual(j, par):
    # B8 (DS146 §5): juego normal, SIN DBZ3_TEST_RINGOUT. Fase I a mano: Goku (casilla 3) ataca a la pieza rival que
    # el guion de la fase pone en la casilla 8 (c22; el #YDW 387 la trae en la 8 y el guion la confirma con "aparece
    # Saibaman en la casilla 8"). A abre el cursor de destino, A elige la 8 (el #YDW 387 da vec [2,8,0,4] con la 2
    # como primer destino) y A mueve: Mover llama a Contacto y sale el "!!" (pasarela_b2.cpp:1410, 2589-2600).
    j.log(r"tablero b2: fase 1 leida", 90)
    dormir(3)
    j.captura("a_preparacion")
    j.tecla("Space", 2500)
    j.log(r"tablero b2: escena T155", 15)
    j.tecla("Backspace", 1500)
    j.captura("a1_llegada")
    j.log(r"tablero b2: escena M207", 30)
    dormir(1.5)
    j.captura("a1b_charla")
    j.tecla_hasta_log("Space", r"tablero b2: script 301: eleccion de 1 aliado", intentos=10, seg=4)
    dormir(1)
    j.captura("a2_aliado")
    j.tecla_hasta_log("Space", r"tablero b2: aliado marcado", intentos=4, seg=3)
    dormir(0.5)
    j.tecla("W", 400)
    j.tecla_hasta_log("Space", r"tablero b2: aliado elegido", intentos=2, seg=3)
    j.log(r"tablero b2: guiones terminados", 60)
    dormir(1)
    j.captura("b_tablero")
    j.tecla("Space", 1200)          # A: abre el cursor de destino (g_dest = primera vecina libre, la 2)
    j.tecla("A", 500)               # izquierda: con la 2 delante, elige la rival de la 8 (la mas lejana en ese eje)
    j.captura("c_destino_rival")
    corte = len(j.texto_log())
    j.tecla("Space", 1200)          # A: mover a la 8 = ataque manual con A sobre la pieza rival
    # Con un rival en la casilla de destino, Mover no registra "turno ... a la casilla": va directo al
    # contacto (pasarela_b2.cpp:2559-2600), que es lo que se comprueba aqui.
    _log_nuevo(j, r"tablero b2: contacto Goku y |tablero b2: Goku ataca al ocupante de la casilla", corte, 25)  # "!!"
    j.captura("d_contacto")
    dormir(2)
    j.captura("f_tarjeta")
    # DS159: el contacto abre primero la charla del #SPX (la captura f_tarjeta la ensena: "X : Next"); un Space la cierra
    # y otro, tras la entrada de la tarjeta "Battle!", la acepta. El combate empieza con la linea de pasarela_b2.cpp
    # (estado 2 de la tarjeta): "tablero b2: combate: A (id N) contra B (id M), turno ..., condicion ...".
    corte = len(j.texto_log())
    j.tecla("Space", 1200)          # cierra la charla del contacto
    dormir(2.0)                     # entrada de la tarjeta "Battle!" (~80 cuadros)
    j.tecla("Space", 1200)          # tarjeta "Battle!" -> combate (sin DBZ3_TEST_RINGOUT)
    _log_nuevo(j, r"tablero b2: combate: ", corte, 60)
    dormir(1)
    j.captura("g_combate")


def _iw_misiones(j, par):
    # frente F (ronda 3): Pasarela > Misiones del Dragon de IW (DBZ3_TEST_MISIONES=4 abre el modo desde el menu
    # principal). La lista sale de la tabla del ELF de la ISO de IW (registro "IW mision N"); ficha de la mision 1
    # (Training No.1), su combate, y con --par desde=2 la ficha "no disponibles" de los entrenamientos 2-10.
    j.log(r"misiones: iw(?: \(\d+ pasos\)|: mapa de Dragon Mission)", 90)   # con el mapa de IW (P1), el mapa
    dormir(3)
    j.captura("a_ficha")
    if par.get("desde", "0") != "0":
        return
    j.tecla("Space", 2500)
    j.captura("b_tarjeta_combate")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    if par.get("cam") == "1":
        return _cam_seq(j)
    dormir(12)
    j.captura("c_combate")


def _iw_mapa(j, par):
    # frente P1 (ronda 3): Pasarela > Misiones del Dragon de IW con el mapa del mundo de IW (graficos leidos de la copia
    # del jugador). desde=N = superadas hasta la N (por defecto 10: el cursor en la 11, Goku contra Raditz).
    # Mapa, ficha (MISSION START), combate (DBZ3_TEST_RINGOUT lo gana), resultado, mapa despues y cursor movido.
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    dormir(4)
    j.captura("a_mapa")
    j.tecla("Space", 2500)
    j.captura("b_ficha")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    dormir(16)
    j.captura("c_combate")
    j.log(r"misiones: iw: mision \d+ superada", 90)
    dormir(5)
    j.captura("d_resultado")
    j.tecla("Space", 2500)
    if re.search(r"misiones: iw: escena de la historia despues", j.texto_log()):   # R1: escena (FD02 tras la 11)
        dormir(2)
        j.captura("d2_escena_despues")
        # el video original (R7) se salta con A; la escena de respaldo, con B
        j.tecla("Space" if re.search(r"video de la historia 2 \(\S+\): reproduciendo", j.texto_log()) else "Backspace", 2500)
    j.captura("e_mapa_despues")
    j.tecla("D", 1500)
    j.captura("f_cursor")
    j.tecla("Backspace", 2000)
    j.captura("g_menu")


def _log_nuevo(j, regex, corte, seg):
    """Espera `regex` en el log escrito DESPUES de la posicion `corte` (evita casar una linea de antes)."""
    fin = time.time() + seg
    while time.time() < fin:
        if re.search(regex, j.texto_log()[corte:]):
            j.info(f"log ok (nuevo): {regex}")
            return
        time.sleep(0.5)
    raise Fallo(f"el log no contiene '{regex}' tras {seg} s desde la marca")


def _iw_cadena_combate(j):
    # Rama de combate de iw_cadena (--par ruta=... hasta una mision): ficha -> MISSION START -> combate -> resultado ->
    # escena de la historia si la hay -> mapa. DBZ3_TEST_RINGOUT (env del escenario) gana el combate.
    j.captura("c_ficha_combate")
    j.tecla("Space", 1800)                           # MISSION START (src/pasarela_iw.cpp, ficha)
    j.log(r"misiones: iw combate", 60)               # src/pasarela_misiones.cpp:1711 (kClaves "iw")
    j.captura("d_combate")
    j.log(r"misiones: iw: mision \d+ superada", 240)  # src/pasarela_misiones.cpp:1492
    dormir(5)
    j.captura("e_resultado")
    corte = len(j.texto_log())
    j.tecla("Space", 2500)
    if re.search(r"misiones: iw: escena de la historia despues", j.texto_log()):
        dormir(2)
        j.captura("f_escena_despues")
        j.tecla("Space" if re.search(r"video de la historia 2 \(\S+\): reproduciendo", j.texto_log()) else "Backspace", 2500)
    _log_nuevo(j, r"iw mapa: vuelta de combate: ScreenTransition_PushCommands", corte, 90)
    j.captura("g_vuelta_mapa")


def _iw_cadena(j, par):
    # Recorre el mapa real de IW como jugador: mapa -> casilla -> minijuego o combate -> resultado -> mapa.
    # Casilla por defecto: el minijuego 0x7D0 (Raditz) de la saga 0. Segun el #DMD 1093 (src/pasarela_iw.cpp:1155-1165)
    # la mision 11 (cursor con desde=10) esta en (890,610) y 0x7D0 en (920,700), asi que una S la elige
    # (Story_PickObjectAtCursor, src/pasarela_iw.cpp:2863-2884). Otras rutas desde la mision 11: 0x7D1 (1035,134) SDD,
    # 0x7D2 (815,98) WW, 0x7D3 (1090,108) SDDW, 0x7D4 (692,457) WA; --par ruta=... las sustituye (vacio = no moverse).
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    dormir(4)
    j.captura("a_mapa")
    for tecla in par.get("ruta", "S").split(","):
        if tecla.strip():
            j.tecla(tecla.strip(), 500)
    j.captura("b_casilla")
    j.tecla("Space", 1800)
    # La casilla puede ser minijuego (tipo 0) o mision (ficha y combate): ramas distintas.
    j.log(r"misiones: iw: ficha de la mision|misiones: iw: minijuego 0x|dbz3 iwmini: casilla 0x", 25)
    if re.search(r"misiones: iw: ficha de la mision", j.texto_log()):
        return _iw_cadena_combate(j)
    # La escena previa solo existe en algunas casillas; si aparece, capturarla y esperar fin.
    if re.search(r"misiones: iw: minijuego .* video previo", j.texto_log()):
        j.captura("c_video_previo")
        corte_video = len(j.texto_log())
        j.log(r"misiones: iw: video de la historia .*: (?:reproduciendo|sin video, subtitulos sobre el mapa)", 60)
        if re.search(r"video de la historia .*: reproduciendo", j.texto_log()[corte_video:]):
            dormir(2.2)         # el FD01A dura ~3,3 s: fotograma real del video, no el fundido de la carga
            j.captura("d_video_fin")
        j.tecla("Space", 1000)  # START/A salta el video y avanza la escena previa.
        j.log(r"misiones: iw: video previo .* acabado: empieza el minijuego", 180)
        if not re.search(r"video de la historia .*: reproduciendo", j.texto_log()[corte_video:]):
            j.captura("d_video_fin")
    j.log(r"iwmini: salida puesta", 90)
    j.captura("e_minijuego_inicio")
    corte = len(j.texto_log())   # antes del final: sin piloto la vuelta al mapa sale en el mismo cuadro que el resultado
    # DS159: 0x7D0 (MSNMW) es un combate: sin piloto automatico acaba con "modulo 0x...: resultado listo" y no con
    # "META/TIEMPO" (esa linea solo la escriben los modulos de carrera/vuelo). Vale cualquiera de las dos.
    j.log(r"iwmini: (?:META|TIEMPO): tiempo|iwmini: modulo 0x[0-9A-Fa-f]+: resultado listo", 300)
    j.captura("f_minijuego_fin")
    j.log(r"iwmini: modulo 0x[0-9A-Fa-f]+: resultado listo", 45)
    j.captura("g_resultado")
    j.tecla("Space", 2500)
    _log_nuevo(j, r"iw mapa: vuelta de combate: ScreenTransition_PushCommands", corte, 90)
    j.captura("h_vuelta_mapa")


def _r1_historia(j, par):
    # frente R1 (ronda 4): escenas de la historia de IW. Con el reproductor de R7 (video_sfd.h): antes de la mision 11
    # el video FD01B con sus subtitulos, despues FD02 (A los salta); sin video, las escenas de respaldo (modelos de B3
    # en el combate / subtitulos sobre el mapa). desde=10: mision 11 (Goku contra Raditz).
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    dormir(4)
    j.captura("a_mapa")
    j.tecla("Space", 2500)
    j.captura("b_ficha")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw: escena de la historia antes", 20)
    if re.search(r"video de la historia \d+ \(\S+\): reproduciendo", j.texto_log()):
        dormir(6)
        j.captura("c_video_antes")
        j.tecla("Space", 1500)                       # A: salta el video
        j.log(r"misiones: iw combate", 40)
        j.captura("d_tras_video")
    else:
        j.log(r"misiones: iw combate", 40)
        j.log(r"cinematica: paso 2/", 90)   # (DBZ3_TEST_RINGOUT acaba el combate a los 5 s de pelea: corta la escena)
        dormir(0.8)
        j.captura("c_escena_antes")
        dormir(1.6)
        j.captura("d_escena_antes2")
    j.log(r"misiones: iw: mision \d+ superada", 120)
    dormir(5)
    j.captura("e_resultado")
    j.tecla("Space", 2500)
    j.log(r"misiones: iw: escena de la historia despues", 20)
    dormir(5)
    j.captura("f_escena_despues")
    j.tecla("Space", 1500)
    j.captura("g_escena_despues2")
    if not re.search(r"video de la historia 2 \(\S+\): reproduciendo", j.texto_log()):
        j.tecla("Backspace", 2000)                   # respaldo: B salta los subtitulos
    j.captura("h_mapa")
    j.tecla("Backspace", 2000)
    j.captura("i_menu")


def _r7_video(j, par):
    # frente R7 (ronda 4): reproductor de videos SFD de IW (src/video_sfd.cpp, DBZ3_TEST_VIDEO=<video>,<espera s>).
    # FD01B: 22 s, subtitulos del CRITAG del SFD (canal 2) con las imagenes de DATA_USA 78: 5,9-8,3 s, 8,3-10,1 s...
    # saltar=1: A (Space) a los 9 s y se comprueba que se cierra. Empieza al escribir dbz3_video.req (ya en el menu).
    dormir(3)
    j.captura("0_menu")
    if par.get("voces") in ("jp", "en"):   # Configuracion > Idioma voces (fila 6) > japones/ingles, y vuelta al menu
        for _ in range(5):
            j.tecla("S", 600)
        j.tecla("Space", 5000)
        for _ in range(5):
            j.tecla("S", 700)
        j.captura("0_fila_idioma")
        j.tecla("Space", 2000)
        j.captura("0_idioma_voces")
        j.tecla(par.get("flecha", "A" if par["voces"] == "jp" else "D"), 800)   # Voz: < Ingles / Japones >
        j.captura("0_idioma_elegido")
        j.tecla("Space", 2500)
        j.captura("0_idioma_hecho")
        j.tecla("Backspace", 2000)
        j.tecla("Backspace", 3000)
        j.captura("0_vuelta_menu")
    (j.dest / "dbz3_video.req").write_text("1", encoding="ascii")
    j.log(r"video: reproducir .*" + par.get("video", "FD01B"), 20)
    j.log(r"lineas de subtitulo en el CRITAG", 20)
    j.log(r"voces japonesas" if par.get("jp") == "1" or par.get("voces") == "jp"
          else r"voces inglesas: opcion del juego 1", 5)
    t0 = time.time()
    for nombre, seg in (("a_inicio", 2.5), ("b_linea1", 7.0), ("c_linea2", 9.2)):
        time.sleep(max(0.0, t0 + seg - time.time()))
        j.captura(nombre)
    j.log(r"video: mando y sonido del juego parados \(audio_gain 0", 5)
    if par.get("saltar") == "1":
        j.tecla("Space", 1500)
        j.log(r"video: .* saltado", 10)
        j.log(r"video: mando y sonido del juego devueltos", 5)
        dormir(2.5)
        j.captura("d_saltado")
        # la A que salta no debe llegar al menu nativo (antes entraba en Universo Dragon: estado 5)
        tras = j.texto_log().split("saltado", 1)[-1]
        if re.search(r"hub: menu principal estado 5", tras):
            raise Fallo("la A que salta el video llego al menu del juego")
        j.info("la A que salta no llego al menu")
        return
    time.sleep(max(0.0, t0 + 13.0 - time.time()))
    j.captura("d_linea4")
    j.log(r"video: .* leido: ", 40)
    j.log(r"video: .* acabado", 40)
    j.log(r"video: subir textura", 10)
    j.log(r"video: mando y sonido del juego devueltos", 5)
    dormir(1.5)
    j.captura("e_despues")


def _r1_road(j, par):
    # frente R1 (ronda 4): Fighter's Road de IW. Y (P) en el mapa de Dragon Mission -> eleccion del luchador
    # (D = derecha) -> A: mapa del Road (#DMD 1092) -> ficha -> combate (DBZ3_TEST_RINGOUT) -> resultado -> Y: vuelta.
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    dormir(4)
    j.tecla("P", 2500)
    j.log(r"Fighter's Road: eleccion del luchador", 10)
    j.captura("a_eleccion")
    j.tecla("D", 1500)
    j.captura("b_eleccion2")
    j.tecla("Space", 2500)
    j.log(r"Fighter's Road con el id", 10)
    dormir(2)
    j.captura("c_mapa_road")
    j.tecla("Space", 2500)
    j.captura("d_ficha")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    dormir(16)
    j.captura("e_combate")
    j.log(r"misiones: iw: mision \d+ superada", 120)
    dormir(5)
    j.captura("f_resultado")
    j.tecla("Space", 2500)
    j.captura("g_mapa_despues")
    j.tecla("P", 2000)
    j.captura("h_dragon_mission")


def _r1_traza(j, par):
    # frente R1 (ronda 4): SONDA de los entrenamientos de IW (DBZ3_R1_TRAZA): mision 2 (Training No.2: guardia, esquiva,
    # teletransporte) con el registro de la accion (X+3244) y la animacion (X+2928) de los dos luchadores ("iw traza").
    # Guardia mantenida (Space = G), toques de guardia, guardia + direcciones; el COM ataca.
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    dormir(4)
    j.tecla("Space", 2500)
    j.captura("a_ficha")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    j.log(r"iw paso 1 estado del combate 4", 90)
    dormir(1)
    j.captura("b_pelea")
    j.tecla("Space 4000", 4500)
    for _ in range(8):
        j.tecla("Space", 400)
    for k in ("D", "A", "W", "S"):
        j.tecla("Space+" + k + " 300", 600)
    j.tecla("Space 3000", 3500)
    j.captura("c_guardia")
    dormir(3)
    j.log(r"iw traza: estado 4", 5)


def _r1_entreno9(j, par):
    # frente R1 (ronda 4): Training No.9 de IW (transformarse con 2 barras de ki o mas y ganar). Goku contra Piccolo:
    # nada mas empezar la pelea, P+K+G (P+L+Space) = transformarse (Kaioken); DBZ3_TEST_RINGOUT gana a los 5 s.
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    dormir(4)
    j.tecla("Space", 2500)
    j.captura("a_ficha")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    j.log(r"iw paso 1 estado del combate 4", 90)
    j.tecla("P+L+Space 200", 900)
    j.captura("b_transformado")
    j.log(r"iw: entrenamiento: transformado", 10)
    j.log(r"misiones: iw: mision \d+ superada", 60)
    dormir(5)
    j.captura("c_resultado")


def _r1_road_rueda(j, par):
    # frente R1 (ronda 4): Fighter's Road con la rueda del select del juego. Y (P) en el mapa de Dragon Mission -> el
    # piloto lleva a Duelo > 1P VS COM -> A (Space) confirma el personaje del cursor -> el piloto vuelve -> mapa del Road.
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    dormir(4)
    j.tecla("P", 1500)
    j.log(r"Fighter's Road: rueda del juego abierta", 30)
    dormir(2)
    j.captura("a_rueda")
    j.tecla("D", 1200)
    j.captura("b_rueda_cursor")
    j.tecla("Space", 1500)
    j.log(r"Fighter's Road con el id \d+ \(rueda del juego", 40)
    dormir(3)
    j.captura("c_mapa_road")
    j.tecla("Space", 2500)
    j.captura("d_ficha")


def _r1_traza2(j, par):
    # frente R1 (ronda 4): SONDA 2 de los entrenamientos (DBZ3_R1_TRAZA): mision 12 (Raditz, COM en Normal, ataca) y
    # guardia + direccion a rafagas cortas mientras pega, para que salgan la esquiva y el teletransporte en el registro.
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    dormir(4)
    j.tecla("Space", 2500)
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    j.log(r"iw paso 1 estado del combate 4", 90)
    for i in range(70):
        j.tecla("Space+" + "DAWS"[i % 4] + " 60", 140)
        if i % 10 == 0:
            j.tecla("Space 400", 450)
    j.captura("a_fin")
    j.log(r"iw traza: estado 4", 5)


def _r1_entreno2(j, par):
    # frente R1 (ronda 4): Training No.2 de IW (guardia, esquiva y teletransporte x5). Guardia mantenida, toques de G y
    # G + izquierda/derecha a rafagas mientras el COM ataca; se comprueba que cuenta (registro "entrenamiento 2").
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    dormir(4)
    j.tecla("Space", 2500)
    j.captura("a_ficha")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    j.log(r"iw paso 1 estado del combate 4", 90)
    j.tecla("Space 3000", 3500)
    for i in range(60):
        j.tecla(("Space+" + "DA"[i % 2] + " 60") if i % 3 else "Space 60", 160)
    j.captura("b_pelea")
    j.log(r"iw: entrenamiento 2: guardia [1-9]", 5)


def _iw_disparo(j, par):
    """Familia de disparo de IW (src/iw_mini_disparo.cpp): 0x7D3, 0x7D7, 0x7D8 y 0x7DE.

    No tienen aros: el escenario generico esperaba "iwmini: aro N cogido" y fallaba siempre. Aqui se
    esperan los logs propios del modulo (DS126): "iwdisparo: juego 0x.... empieza" al acabar la
    entrada y "iwdisparo: fin:" al cerrar (Acabar). Se captura el principio, a mitad de partida y el
    final. auto=1 (de serie) deja al piloto automatico apuntando y disparando; auto=0 solo toca teclas.
    """
    clave = par.get("clave", "0x7D8")
    auto = par.get("auto", "1") == "1"
    try:                                       # el log imprime la clave en mayusculas (0x{:X})
        clave_hex = "0x%X" % int(str(clave), 0)
    except ValueError:
        clave_hex = str(clave)
    # El 0x7D7 reproduce su entrada de 520 cuadros (950); el 9 la de 440 (1000): margen de sobra.
    j.log(rf"iwdisparo: juego {re.escape(clave_hex)} empieza", 120)
    dormir(0.5)
    j.captura("a_juego")
    if auto:
        dormir(7)                              # a mitad del minijuego, con el piloto disparando
        j.captura("b_mitad")
        j.log(r"iwdisparo: (fin:|Piccolo derribado|TIEMPO|7 aciertos|FALLO|fin del 15)", 220)
        dormir(1.2)                            # deja pasar el cierre/fundido
        j.captura("c_fin")
        dormir(2.5)
        j.captura("d_resultado")
    else:
        for i, k in enumerate(("W 800", "D 800", "Space 300", "Quote 300")):
            j.tecla(k, 450)
            dormir(0.4)
            j.captura(f"b_juego{i + 1}")
        j.log(r"iwdisparo: (fin:|Piccolo derribado|TIEMPO|7 aciertos|FALLO|fin del 15)", 220)
        dormir(1.2)
        j.captura("c_fin")


def _iw_minijuego(j, par):
    # Familia de disparo (0x7D3/0x7D7/0x7D8/0x7DE): sus propios logs y capturas.
    if str(par.get("clave", "")).lower() in ("0x7d3", "0x7d7", "0x7d8", "0x7de"):
        _iw_disparo(j, par)
        return
    # frente P4 (ronda 3): minijuego del mapa de IW (src/iw_minijuegos.cpp, DBZ3_TEST_IWMINI=<clave>[,auto]) lanzado
    # desde el menu principal en modo libre (un luchador, camara detras, aros/monedas de IW y TIME). auto=1 (de serie):
    # el jugador salta solo de aro en aro hasta la meta. auto=0: se mueve con la palanca (W/A/S/D) y se ve la marcha.
    j.log(r"iwmini: prueba: empieza", 90)
    j.log(r"iwmini: salida puesta", 60)
    dormir(2)
    j.captura("a_salida")
    if par.get("radar") == "1":                       # SOL1: radar solo mientras se mantiene triangulo (P/Y)
        j.log(r"iwmini: entrada acabada", 60)
        dormir(3)
        j.captura("b_radar_suelto")
        j.tecla("P 3000", 1500)
        j.captura("c_radar_pulsado")
        dormir(2)
        j.captura("d_radar_suelto")
        return
    if par.get("npc") == "1":                         # Bubbles (0x7D1) o Piccolo (0x7DD): se mueven solos
        if par.get("clave") == "0x7D1":               # Bubbles sale delante (con --env DBZ3_IWMINI_NPC_QUIETO=1
            j.tecla("W 900", 950)                     # no huye): acercarse y verle correr en el sitio
            j.tecla("D 250", 300)                     # un paso al lado: que Goku no lo tape
            j.captura("b_bubbles1")
            dormir(0.25)
            j.captura("b_bubbles2")
        j.captura("b_npc")
        if par.get("auto", "1") == "1":
            j.log(r"iwmini: (Bubbles atrapado|Piccolo te alcanza|META|TIEMPO)", 120)
            dormir(1)
            j.captura("c_fin")
            # SOL1: comprobar tambien la salida del minijuego (antes PASS cortaba durante CLEAR).
            j.log(r"iwmini: prueba: resultado", 60)
            dormir(3)
            j.captura("e_menu")
            if par.get("completa") == "1":
                dormir(8)
                j.captura("f_resultado_completo")
        else:
            j.tecla("W 3000", 3500)
            j.captura("c_npc")
            j.tecla("Right 1300", 1500)               # la cruceta (flecha): en el minijuego anda, no lanza tecnicas
            j.captura("d_mirar")
            if par.get("clave") == "0x7DD":           # Piccolo acaba alcanzando al jugador quieto
                j.log(r"iwmini: Piccolo te alcanza", 40)
                j.captura("e_fin")
        return
    if par.get("quieto") == "1":                      # para sondas: el jugador quieto 22 s
        dormir(11)
        j.captura("b_quieto1")
        dormir(11)
        j.captura("b_quieto2")
        return
    if par.get("salto") == "1":                       # cruz (A del mando = Semicolon): salto de IW, 24 fotos
        if not any((j.dest / "mods").glob("Campo IW *")):   # el salto solo corre sobre el suelo de un campo de IW (d->campo)
            raise Fallo("el salto necesita un mod 'Campo IW <n>' en la carpeta mods de la copia: sin el no hay 'codigo 829'")
        j.log(r"entrada acabada: mando al jugador", 30)
        dormir(2.5)                               # el cartel del objetivo (140 cuadros) para el mando
        (j.dest / "dbz3_shot.req").write_text("24 3", encoding="ascii")
        dormir(0.6)
        j.tecla("Semicolon", 200)
        dormir(2.5)
        j.log(r"codigo 829", 5)
        return
    if par.get("scatto") == "1":                      # circulo (B del mando = Quote): scatto de IW hasta agotarse
        for i in range(4):                            # 47 de energia cada uno: el 3.o agota y el 4.o no sale
            if i == 0 and par.get("rafaga"):          # 24 fotos (1 de cada 2 cuadros) del arranque: el polvo
                j.log(r"entrada acabada: mando al jugador", 30)
                dormir(2.5)                       # el cartel del objetivo
                (j.dest / "dbz3_shot.req").write_text("72 1", encoding="ascii")
                dormir(0.45)
            j.tecla("Quote", 600)
            j.captura(f"b_scatto{i + 1}")
            dormir(1.4)
        j.log(r"iwmini: scatto: sin energia", 5)
        j.captura("c_agotado")
        return
    if par.get("meta") == "1":                        # la meta (nave de Raditz, modelo de IW) abierta desde el principio
        j.tecla("W 4000", 4500)
        j.captura("b_nave")
        j.tecla("W 3000", 3500)
        j.captura("c_nave_cerca")
        return
    if par.get("lejos") == "1":                       # hasta donde se puede correr en el escenario de B3
        j.tecla("W 14000", 14500)
        j.captura("b_lejos")
        return
    if par.get("auto", "1") != "1":
        j.log(r"iwmini: entrada acabada", 60)          # antes de la intro el mando no mueve al jugador
        dormir(0.5)
        j.tecla("W 3000", 1500)   # W = palanca (las flechas son la cruceta)
        j.captura("b_corriendo")                       # con la palanca aun pulsada: la pose de correr
        dormir(2)
        j.captura("b_adelante")
        j.tecla("D 1500", 2000)
        j.captura("c_derecha")
        if par.get("rafaga"):   # 24 fotos (1 de cada 6 cuadros) corriendo, en dbz3_shot_NNN.bmp
            (j.dest / "dbz3_shot.req").write_text("24 6", encoding="ascii")
            dormir(0.6)
        j.tecla("W 2500", 3000)
        j.captura("d_adelante2")
        j.log(r"iwmini: jugador", 10)
        return
    # los aros (y marcas del Radar) que da el #ITM en esa dificultad: "curso (dificultad d): A n B .. C .. D m"
    m = re.search(r"iwmini: curso \(dificultad \d+\): A (\d+) B \d+ C \d+ D (\d+)", j.texto_log())
    # SOL1: D solo cuenta como aro en el Radar (grupo 3). En Namek son 16 recargas (grupo 4),
    # no aros: esperar A+D provocaba FAIL aun cuando la partida ya habia llegado a META.
    n = (int(m.group(2)) if str(par.get("clave", "")).lower() == "0x7df" else int(m.group(1))) if m else 8
    j.log(rf"iwmini: aro {min(3, n)} cogido", 40)
    dormir(0.3)
    j.captura("b_aros")
    j.log(rf"iwmini: aro {n} cogido", 40)
    dormir(0.3)
    j.captura("c_aros")
    j.log(r"iwmini: (META|TIEMPO):", 60)
    if par.get("rafaga"):   # --par rafaga=1: 24 fotos (1 de cada 10 cuadros) desde la meta, en dbz3_shot_NNN.bmp
        (j.dest / "dbz3_shot.req").write_text("24 10", encoding="ascii")
        dormir(4.5)
    dormir(1)
    j.captura("d_meta")
    j.log(r"iwmini: final de IW", 30)   # con --env DBZ3_IWMINI_FINAL=1: la animacion final (pantalla azul R16)
    dormir(1.5)
    j.captura("d_final")
    j.log(r"iwmini: prueba: resultado", 60)
    dormir(3)
    j.captura("e_menu")

    if par.get("completa") == "1":                      # SOL1: no comparar el inicio del conteo con una tabla ya terminada
        dormir(8)
        j.captura("f_resultado_completo")


def _r6_ranking(j, par):
    # frente R6 (ronda 4): HUB > Ranking (entrada 6): pagina del perfil y la siguiente (RB), y el Kumite (entrada 5)
    # con su selector de retos. Solo mira: no empieza nada.
    dormir(2.5)
    j.tecla("W", 1200)
    j.tecla("Space", 3500)                              # HUB (cursor en Tag Team)
    for _ in range(6):                                  # tagteam .. ranking
        j.tecla("S", 450)
    j.tecla("Space", 2500)
    j.captura("a_ranking")
    j.tecla("D", 1200)                                  # derecha (= RB): pagina siguiente
    j.captura("b_ranking_2")
    j.tecla("Backspace", 2500)
    j.tecla("W", 450)                                   # ranking -> kumite
    j.tecla("Space", 2500)
    j.captura("c_kumite")
    j.tecla("Backspace", 2000)


def _r6_torre(j, par):
    # frente R6 (ronda 4): HUB > Torre > primer reto: combate (marcador del piso bajo el reloj del juego), ring out
    # (DBZ3_TEST_RINGOUT) y la pantalla entre combates del HUB. No pulsa nada despues: el reto queda a medias.
    dormir(2.5)
    j.tecla("W", 1200)
    j.tecla("Space", 3500)                              # HUB
    for _ in range(3):                                  # tagteam .. torre
        j.tecla("S", 450)
    j.tecla("Space", 2500)                              # selector de retos (Torre arriba)
    j.captura("a_selector_torre")
    j.tecla_hasta_log("Space", r"hub: combate nativo", 4, 4)
    dormir(14)
    j.captura("b_combate")
    j.log(r"rival fuera del ring", 90)
    j.log(r"hub: fin del combate nativo", 40)
    dormir(5)
    j.captura("c_entre")


def _m100(j, par):
    # Mision 100 (rpg.cpp): DBZ3_TEST_MODO empieza la mision n con el luchador id; ring out (DBZ3_TEST_RINGOUT), su
    # pantalla final con el rango y, con A, vuelta a sus paginas.
    j.log(r"prueba: mision 100 n \d+ con id \d+: empieza", 120)
    j.log(r"hub: combate nativo", 60)
    dormir(14)
    j.captura("a_combate")
    j.log(r"rpg: fin de", 150)
    dormir(4)
    j.captura("b_fin")
    j.tecla("Space", 2500)
    j.captura("c_paginas")
    j.tecla("D", 800)                                   # otro luchador
    j.tecla("S", 800)
    j.captura("d_paginas2")


def _r6_hud(j, par):
    # frente R6 (ronda 4): HUD de los modos en combate contra varios rivales (Modo Heroe, Saibaman 2/2: el contador
    # nativo de rivales a la derecha del reloj) y la pantalla entre combates de rpg.cpp. Mismo camino que r5_heroe.
    dormir(2.5)
    j.tecla("W", 1200)
    j.tecla("Space", 3500)                              # HUB
    for _ in range(5):                                  # tagteam .. kumite
        j.tecla("S", 450)
    j.tecla("Space", 2500)                              # selector de retos
    for _ in range(3):                                  # kumite .. modo heroe
        j.tecla("S", 700)
    j.tecla("Space", 2500)                              # pantalla de creacion
    for _ in range(4):                                  # .. Empezar
        j.tecla("S", 500)
    j.tecla_hasta_log("Space", r"hub: combate nativo", 4, 4)
    dormir(7)
    j.captura("a0_presentacion")                        # sin HUD del juego: tampoco el del modo
    dormir(13)                                      # pasa la presentacion: HUD del juego a la vista
    j.captura("a_hud")
    dormir(3)
    j.captura("b_hud")
    j.log(r"combate \d ganado|rpg: fin de", 60)        # (una partida a medias sigue por el 2 o el 3: fin)
    dormir(6)
    j.captura("c_entre")
    if par.get("estilo") == "1":                      # RB (tecla 3): otro estilo para el combate siguiente y A
        j.tecla("3", 1500)
        j.log(r"heroe: estilo \d+ para el combate", 10)
        j.captura("d_estilo")
        j.tecla_hasta_log("Space", r"hub: combate nativo", 4, 6)
        dormir(20)
        j.captura("e_combate2")
        j.log(r"heroe: estilo \d+ \(", 30)


def _torneo(j, par):
    # DS193 (issue #55): Torneo Mundial NATIVO, nivel Novato, de principio a fin. Entra desde el menu principal,
    # elige el personaje que trae la rueda, gana los tres combates con DBZ3_TEST_RINGOUT (tagteam.cpp: aviso
    # mgr+35569 a los 5 s de pelea; sub_820FD678 lo cobra como motivo 4 en cualquier modo) y comprueba que el juego
    # VUELVE AL MENU PRINCIPAL (pantalla 5) sin cerrarse. Es el caso del reporte: "tras ganar el torneo novato el
    # juego se congela y deja de contar FPS".
    # No se pulsa a ciegas dentro del combate: se avanza con Space y se esperan las marcas del log:
    #   "VS del torneo" (roster_ext, pantalla VS), "tag: prueba: rival fuera del ring" y "tag: combate acabado".
    dormir(3)
    j.tecla("S", 1200)
    j.tecla("S", 1200)                                  # menu principal: Torneo (2 filas)
    j.tecla("Space", 3500)                              # menu del Torneo (nivel y participantes)
    j.captura("a_torneo")
    j.tecla("Space", 3000)                              # Novato (primera opcion; el cursor arranca ahi)
    j.captura("b_nivel")
    # Rueda de personajes y VS: se avanza hasta que el log diga que la pantalla VS del Torneo ya se dibujo.
    fin = time.time() + 90
    while time.time() < fin and j.vivo() and not re.search(r"VS del torneo", j.texto_log()):
        j.tecla("Space", 2200)
    if not re.search(r"VS del torneo", j.texto_log()):
        raise Fallo("no se llego a la pantalla VS del Torneo (pantalla %s)" % j.pantalla())
    j.captura("c_vs")
    # Combates: Space adelanta VS/resultados; el ring out de la prueba cierra cada pelea a los 5 s.
    combates = 0
    fin = time.time() + 300
    while time.time() < fin and j.vivo() and combates < 3:
        j.tecla("Space", 1500)
        n = len(re.findall(r"tag: combate acabado", j.texto_log()))
        if n > combates:
            combates = n
            j.info("combate %d acabado (pantalla %s)" % (combates, j.pantalla()))
            dormir(3)
            j.captura("d_combate%d" % combates)
        dormir(0.5)
    if not j.vivo():
        raise Fallo("el juego se cerro en el Torneo (combates acabados: %d)" % combates)
    if combates < 3:
        raise Fallo("el Torneo no llego a los 3 combates ganados (acabados: %d; pantalla %s)" % (combates, j.pantalla()))
    # Lo que reporta el issue: tras el ultimo combate tiene que volver al menu principal, no quedarse clavado.
    # A partir de aqui NO se pulsa a ciegas: cada Space va seguido de una espera; si la pantalla ya es la 5 se para
    # (una tecla al entrar en el menu podia abrir otra fila y dar el test por fallido sin motivo).
    for _ in range(15):
        try:
            j.esperar_pantalla([5], 6)
            break
        except Fallo:
            if not j.vivo():
                raise
            j.tecla("Space", 1500)
            dormir(1)
    else:
        raise Fallo("no volvio al menu principal tras ganar el Torneo (pantalla %s)" % j.pantalla())
    j.captura("e_menu")
    j.info("vuelta al menu principal tras ganar el Torneo (3 combates)")


def _r6_pantallas(j, par):
    # frente R6 (ronda 4): pantallas de los modos que no salen en las otras pruebas. que=entre|derrota|fin: modos.cpp
    # las pinta en el menu principal con un reto de ejemplo (DBZ3_TEST_R6_PANTALLA, sin tocar nada); que=sim|torneo:
    # HUB > Supervivencia/Kumite > Simulacion/Torneo de 4 (su primera pantalla); que=abyss: HUB > Torre > Abyss, gana
    # el primer piso (ring out) y captura las cartas.
    que = par.get("que", "entre")
    dormir(3)
    if que in ("entre", "derrota", "fin"):
        j.captura(f"a_{que}")
        return
    entrada = {"sim": 4, "torneo": 5, "abyss": 3}[que]
    j.tecla("W", 1200)
    j.tecla("Space", 3500)                              # HUB
    for _ in range(entrada):
        j.tecla("S", 450)
    j.tecla("Space", 2500)                              # selector de retos
    j.tecla("S", 600)
    j.tecla("S", 600)                                   # tercera fila: Simulacion / Torneo de 4 / Abyss
    j.captura("a_selector")
    j.tecla("Space", 3000)
    j.captura(f"b_{que}")
    if que == "abyss":
        j.tecla_hasta_log("Space", r"hub: combate nativo", 4, 4)
        j.log(r"rival fuera del ring", 90)
        j.log(r"hub: fin del combate nativo", 40)
        dormir(6)
        j.captura("c_cartas")


def _r8_limite(j, par):
    # frente R8: hasta donde llega un luchador en un escenario. Combate del HUB (--par escenario=5 Llanuras, 17 = alta);
    # el jugador 1 anda con la palanca (--par tecla=Left) mientras se leen las posiciones de los dos (X+64/+68/+72,
    # X = mgr+2096+8240*lado) y el aviso de fuera del ring (mgr+35569, lo escribe sub_8211F650). Escribe limite.txt.
    # --par clave=0x7D2: en el modo libre del minijuego de IW (P4; su campo si esta instalado), tecla W por defecto.
    import struct as _st
    j.log(r"iwmini: salida puesta" if par.get("clave") else r"tag: empieza un combate", 90)
    dormir(1)

    def f(a):
        b = leer(j.h, GBASE + a, 4)
        return _st.unpack(">f", b)[0] if b and len(b) == 4 else float("nan")
    lineas, maxr, aviso = [], [0.0, 0.0], None
    tecla, seg = par.get("tecla", "W" if par.get("clave") else "Left"), float(par.get("seg", "20"))
    fin = time.time() + seg
    siguiente = 0.0
    while time.time() < fin and j.vivo():
        if time.time() >= siguiente:                  # mantener la palanca (tramos de 3 s)
            j.tecla(f"{tecla} 3000", 0)
            siguiente = time.time() + 2.9
        b = leer(j.h, GBASE + 0x823D6CF4, 4)
        mgr = _st.unpack(">I", b)[0] if b else 0
        if not mgr:
            dormir(0.05)
            continue
        if not lineas:                                # camara: ojo +0x190, objetivo +0x1A0, fov +0x1C4 y vecinos
            lineas.append("camara mgr+0x180..0x1E0: " + " ".join("%.2f" % f(mgr + o) for o in range(0x180, 0x1E0, 4)))
            nb = leer(j.h, GBASE + 0x8238941C, 12) or bytes(12)   # niebla puesta (clima.cpp): activa, ?, color
            lineas.append("niebla: activa %08X color %08X, %.0f..%.0f" % (
                _st.unpack_from(">I", nb, 0)[0], _st.unpack_from(">I", nb, 8)[0], f(0x823106B4), f(0x823106B8)))
        pos = [tuple(f(mgr + 2096 + 8240 * l + o) for o in (64, 68, 72)) for l in (0, 1)]
        flag = (leer(j.h, GBASE + mgr + 35569, 1) or b"\x00")[0]
        for l in (0, 1):
            maxr[l] = max(maxr[l], (pos[l][0] ** 2 + pos[l][2] ** 2) ** 0.5)
        lineas.append("%.2f %s flag=%d" % (time.time() - j.t0, " ".join("(%.1f,%.1f,%.1f)" % p for p in pos), flag))
        if flag and aviso is None:
            aviso = lineas[-1]
            j.captura("b_fuera")
        dormir(0.05)
    j.captura("a_lejos")
    res = "radio maximo: jugador %.1f, rival %.1f; aviso de fuera del ring: %s" % (maxr[0], maxr[1], aviso)
    (j.run / "limite.txt").write_text("\n".join([res] + lineas), encoding="utf-8")
    j.info(res)


def _r14_botones(j, par):
    # frente R14: minijuego de botones de IW (src/iw_mini_botones.cpp: 0x7D4 Gohan/Piccolo, 0x7DB Camara del Tiempo,
    # 0x7DC gravedad). auto=1 (de serie): pulsa solo el boton correcto de cada icono al llegar al objetivo.
    j.log(r"iwmini: prueba: empieza", 90)
    j.log(r"r14: en juego", 90)
    j.captura("a_ready")
    if par.get("teclas") == "1":                      # sonda: cada boton del mando (teclado de pruebas) por turno
        for k in ("L", "P", "Semicolon", "Quote", "1", "3", "Q", "E", "W", "D", "S", "A", "Shift+Up", "Shift+Right"):
            j.tecla(k + " 150", 500)
        return
    dormir(4)
    j.captura("b_iconos")
    dormir(6)
    j.captura("c_iconos")
    j.log(r"r14: fin: ", int(par.get("seg", "240")))   # 0x7DA (10 rondas) tarda ~4 min: --par seg=420
    dormir(1.5)
    j.captura("d_fin")
    j.log(r"r14: resultado ", 30)
    j.log(r"iwmini: prueba: resultado superado", 30)


def _r12_vuelo(j, par):
    # frente R12: minijuegos de vuelo de IW (src/iw_mini_vuelo.cpp: 0x7D6 Vegeta, 0x7E0 carrera con Goten). auto=1 (de
    # serie): piloto que gira hacia el punto de control mas cercano (y luego la meta) con R1 si el medidor lo permite.
    j.log(r"iwmini: prueba: empieza", 90)
    j.log(r"terminado; empieza el vuelo", 90)
    dormir(1.5)
    j.captura("a_salida")
    if par.get("auto", "1") != "1":                   # a mano: palanca arriba y a la derecha (W / D)
        j.tecla("W 4000", 4200)
        j.captura("b_adelante")
        j.tecla("D 1200", 1400)
        j.captura("c_derecha")
        j.tecla("W 3000", 3200)
        j.captura("d_adelante2")
        for k in ("L", "P", "Semicolon", "Quote"):     # botones de golpe: el luchador de B3 no debe atacar
            j.tecla(k + " 150", 300)
        j.captura("e_botones")
        j.log(r"r12: \(", 10)
        return
    dormir(5)
    j.captura("b_vuelo")
    j.log(r"r12: punto de control", 60)
    dormir(0.3)
    j.captura("c_control")
    j.log(r"r12: fin \(", 240)
    dormir(1.0)
    j.captura("d_fin")
    j.log(r"r12: resultado ", 60)
    j.log(r"iwmini: prueba: resultado superado", 30)


ESCENARIOS = {
    "_r12_vuelo": ("R12: minijuego de vuelo de IW (--par clave=0x7D6 Vegeta | 0x7E0 Goten; auto=0 sin piloto; "
                   "dificultad=N 0..5)",
                   lambda p: {"DBZ3_TEST_IWMINI": p.get("clave", "0x7D6") + (",auto" if p.get("auto", "1") == "1"
                                                                               else ""),
                              # DS173: el vuelo toma la dificultad de iwmini::Empezar(clave,-1), que lee g_dificultad
                              # (src/iw_minijuegos.cpp:4052, bloque g_test); sin esta variable usa el defecto 2.
                              "DBZ3_TEST_IWMINI_DIFICULTAD": p.get("dificultad", ""),
                              "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r12_vuelo),
    "_r14_botones": ("R14: minijuego de botones de IW (--par clave=0x7DC; auto=0 sin piloto)",
                     lambda p: {"DBZ3_TEST_IWMINI": p.get("clave", "0x7DC") + (",auto" if p.get("auto", "1") == "1"
                                                                                 else "") +
                                                    (",fallos" if p.get("fallos") == "1" else ""),
                                "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r14_botones),
    "r8_limite": ("R8: radio hasta el que llega un luchador y fuera del ring (--par escenario=5, tecla=Left, seg=20)",
                  lambda p: ({"DBZ3_TEST_IWMINI": p["clave"], "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"} if p.get("clave")
                             else {"DBZ3_TEST_BATTLE": "0,7," + p.get("escenario", "5")}), _r8_limite),
    "r6_pantallas": ("Pantallas de los modos (que=entre|derrota|fin|sim|torneo|abyss) para revisar a ojo",
                     lambda p: dict({"DBZ3_TEST_RINGOUT": "1"},
                                    **({"DBZ3_TEST_R6_PANTALLA": p.get("que", "entre")}
                                       if p.get("que", "entre") in ("entre", "derrota", "fin") else {})),
                     _r6_pantallas),
    "torneo": ("DS193 (issue #55): Torneo Mundial NATIVO nivel Novato: entra, gana los 3 combates "
               "(DBZ3_TEST_RINGOUT) y espera la vuelta al menu principal (pantalla 5)",
               lambda p: {"DBZ3_TEST_RINGOUT": "1"}, _torneo),
    "hub_filas": ("HUB pagina 2: fila Pasarela (avance de las campanas) y fila Mision 100 abierta desde el HUB",
                  lambda p: {}, _hub_filas),
    "m100": ("Mision 100 (rpg.cpp): mision n con el luchador id (--par n=5 id=0), ring out, rango y sus paginas",
             lambda p: {"DBZ3_TEST_MODO": "mision100,%s,%s" % (p.get("id", "0"), p.get("n", "1")),
                        "DBZ3_TEST_RINGOUT": "1"}, _m100),
    "r6_hud": ("HUD de los modos con el contador nativo de rivales (Modo Heroe vs Saibaman) y entre combates",
               lambda p: {"DBZ3_TEST_RINGOUT": "1"}, _r6_hud),
    "r6_torre": ("HUB > Torre: combate con el marcador del HUB y pantalla entre combates (ring out)",
                 lambda p: {"DBZ3_TEST_RINGOUT": "1"}, _r6_torre),
    "r6_ranking": ("HUB > Ranking (perfil y pagina 2) y selector del Kumite (solo mirar)", lambda p: {}, _r6_ranking),
    "iw_minijuego": ("Minijuego del mapa de IW (--par clave=0x7D0: Raditz; auto=0 sin piloto automatico)",
                     lambda p: {"DBZ3_TEST_IWMINI": p.get("clave", "0x7D0") + (",auto" if p.get("auto", "1") == "1"
                                                                                 else "") +
                                                     (",meta" if p.get("meta") == "1" else ""),
                                "DBZ3_TEST_IWMINI_DIFICULTAD": p.get("dificultad", ""),
                                "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _iw_minijuego),
    "iw_mapa": ("Pasarela > Misiones del Dragon de IW con el mapa de IW: mapa, ficha, combate, resultado (desde=N)",
                 lambda p: {"DBZ3_TEST_MISIONES": f"4,reset,desde={p.get('desde', '10')}", "DBZ3_TEST_RINGOUT": "1",
                            "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _iw_mapa),
    "iw_cadena": ("IW como jugador: mapa, casilla (por defecto el minijuego 0x7D0; --par ruta=S,D,... para otra), "
                  "video previo opcional, minijuego o combate, resultado y vuelta al mapa",
                  lambda p: {"DBZ3_TEST_MISIONES": f"4,reset,desde={p.get('desde', '10')}",
                             "DBZ3_TEST_RINGOUT": "1", "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _iw_cadena),
    "r1_historia": ("Pasarela > IW: escenas de la historia antes (motor de B3) y despues (mapa) de la mision 11",
                    lambda p: {"DBZ3_TEST_MISIONES": f"4,reset,desde={p.get('desde', '10')}", "DBZ3_TEST_RINGOUT": "1",
                               "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r1_historia),
    "r1_road": ("Pasarela > IW: Fighter's Road (Y en el mapa): luchador, mapa del #DMD 1092, combate y resultado",
                lambda p: {"DBZ3_TEST_MISIONES": "4,reset,desde=10", "DBZ3_TEST_RINGOUT": "1",
                           "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r1_road),
    "r1_traza": ("Pasarela > IW: sonda de los entrenamientos (mision 2, accion y animacion de los luchadores al registro)",
                 lambda p: {"DBZ3_TEST_MISIONES": "4,reset,desde=1", "DBZ3_R1_TRAZA": "1",
                            "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r1_traza),
    "r1_entreno9": ("Pasarela > IW: Training No.9 (transformarse y ganar): Kaioken y K.O. por ring out",
                    lambda p: {"DBZ3_TEST_MISIONES": "4,reset,desde=8", "DBZ3_TEST_RINGOUT": "1",
                               "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r1_entreno9),
    "r1_road_rueda": ("Pasarela > IW: Fighter's Road con la rueda del select del juego (piloto del Tag Team)",
                      lambda p: {"DBZ3_TEST_MISIONES": "4,reset,desde=10",
                                 "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r1_road_rueda),
    "r1_traza2": ("Pasarela > IW: sonda 2 (mision 12, guardia+direccion a rafagas: esquiva y teletransporte)",
                  lambda p: {"DBZ3_TEST_MISIONES": "4,reset,desde=11", "DBZ3_R1_TRAZA": "1",
                             "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r1_traza2),
    "r1_entreno2": ("Pasarela > IW: Training No.2 (guardia, esquiva y teletransporte): el recuento en el registro",
                    lambda p: {"DBZ3_TEST_MISIONES": "4,reset,desde=1", "DBZ3_R1_TRAZA": "1",
                               "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r1_entreno2),
    "iw_misiones": ("Pasarela > Misiones del Dragon de IW (tabla del ELF): ficha de la mision 1 y su combate "
                    "(--par desde=2: ficha de no disponibles)",
                    lambda p: {"DBZ3_TEST_MISIONES": f"4,reset,desde={p.get('desde', '0')}",
                               "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _iw_misiones),
    "r2_nave": ("Pasarela > Nave de Babidi de B2 (ISO del jugador): menu de pruebas, prueba III, combate, Kili "
                "(--par prueba=0..3: las cuatro pruebas con su duracion real)",
                lambda p: dict({"DBZ3_TEST_MISIONES": "5,reset", "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games",
                                "DBZ3_TEST_NAVE_TIEMPO": p.get("tiempo", "6"), "DBZ3_TEST_NAVE_TODO": "1"},
                               **({"DBZ3_TEST_NAVE_PRUEBA": p["prueba"]} if "prueba" in p else {})), _r2_nave),
    "r2_fase1": ("Pasarela > Dragon World de B2: la fase I entera en automatico (presentacion, combates, sucesos, fin)",
                 lambda p: {"DBZ3_TEST_MISIONES": "1,auto,reset", "DBZ3_TEST_RINGOUT": "1",
                            "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r2_fase1),
    "r2_deseo": ("Pasarela > Dragon World de B2: deseos de Shenron y el portal de la Nave de Babidi",
                 lambda p: {"DBZ3_TEST_MISIONES": "1,reset", "DBZ3_TEST_B2_DESEO": "1",
                            "DBZ3_TEST_SHEN_ANIM": p.get("anim", "2"),
                            "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r2_deseo),
    "r2_fases": ("Pasarela > Dragon World de B2: fases seguidas en automatico hasta la IX (--par desde=1 = la II)",
                 lambda p: {"DBZ3_TEST_MISIONES": "1,auto,reset,desde=%s" % p.get("desde", "1"),
                            "DBZ3_TEST_RINGOUT": "1", "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r2_fases),
    "tablero": ("Pasarela > Dragon World de B2 (ISO del jugador): preparacion, tablero, movimiento, contacto, combate",
                lambda p: {"DBZ3_TEST_MISIONES": "1,reset", "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games",
                           "DBZ3_TEST_B2_CONDICION": p.get("condicion", "0")}, _tablero),
    "r2_fase5_derrota": ("B4: fase V perdida a proposito con la orden 43 del guion 306 (GAME OVER 404 #0) y reintento "
                         "con A en el GAME OVER",
                         lambda p: {"DBZ3_TEST_MISIONES": "1,reset,desde=4", "DBZ3_TEST_B2_EVENTO": "306",
                                    "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _b2_fase5_derrota),
    "r2_fase9_final": ("B7: final de la fase IX (--par bolas=1 con las 7 Bolas: Shenron; sin el, se borra la partida y "
                       "vuelve la fase I)",
                       lambda p: dict({"DBZ3_TEST_MISIONES": "1,auto,reset,desde=8", "DBZ3_TEST_RINGOUT": "1",
                                       "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"},
                                      **({"DBZ3_TEST_B2_BOLAS": "127"} if p.get("bolas", "0") == "1" else {})),
                       _b2_fase9_final),
    "tablero_ataque": ("B8: juego normal (sin DBZ3_TEST_RINGOUT); en la fase I, ataque manual con A a la pieza rival de "
                       "la casilla 8, \"!!\" y combate",
                       lambda p: {"DBZ3_TEST_MISIONES": "1,reset", "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"},
                       _b2_ataque_manual),
    "bordes": ("menu principal y Duelo con las tiras doradas nativas ocultas o visibles (--par bordes=true)",
               lambda p: {"REX_DBZ3_UI_SIDE_BORDERS": p.get("bordes", "false")}, _bordes),
    "satan": ("HUB > Leyenda de Mr. Satan: titulo, dificultad, prologo, combate ganado, resultado, tablero, escena "
              "(desde=25: final)",
              lambda p: {"DBZ3_TEST_PASARELA": f"auto,seg=8,desde={p.get('desde', '0')}",
                         **({} if p.get("cam") == "1" else {"DBZ3_TEST_RINGOUT": "1"}),
                         **({"DBZ3_SIN_CINE": "1"} if p.get("golpes") == "1" else {}),
                         "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _satan),
    "_r3_cine": ("Leyenda de Mr. Satan: tablero y escena 3D del retador (desde=2 Ten Shin Han; 1 Yamcha sin escena; "
                 "prologo=1)",
                 lambda p: {"DBZ3_TEST_PASARELA": f"auto,seg=8,desde={p.get('desde', '2')}"
                            + (",prologo" if p.get("prologo") == "1" else ""),
                            "DBZ3_CINE_TRAZA": "1", **({"DBZ3_TEST_RINGOUT": "1"} if p.get("ringout") == "1" else {}),
                            **({"DBZ3_CINE_KI": "1"} if p.get("ki") == "1" else {}),
                            "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r3_cine),
    "_r7_video": ("Video SFD de IW a pantalla completa con audio y subtitulos (--par video=FD01B|ENDING, saltar=1, "
                  "jp=1 voces japonesas)",
                  lambda p: {"DBZ3_TEST_VIDEO": p.get("video", "FD01B") + (",jp" if p.get("jp") == "1" else ""),
                             "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r7_video),
    "_r3_golpes": ("Leyenda de Mr. Satan: combo del jugador (combo_max) y carga de ki (sonda), sin escena",
                   lambda p: {"DBZ3_TEST_PASARELA": "auto,seg=8,desde=1", "DBZ3_SIN_CINE": "1", "DBZ3_CINE_TRAZA": "1",
                              "DBZ3_COMBATE_MUNECO": "quieto", "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _r3_golpes),
    "menu": ("llegar al menu principal y captura", lambda p: {}, _menu),
    "du_freezer_nuevo": ("DU > nueva historia > rueda > Freezer > bucle de combate",
                         lambda p: {}, _du_freezer_nuevo),
    "du_continuar": ("DU > Continuar (Freezer: con --instantanea freezer_cap2)", lambda p: {}, _du_continuar),
    "batalla": ("combate nativo desde el menu (DBZ3_TEST_BATTLE) y vuelta al menu",
                lambda p: {"DBZ3_TEST_BATTLE": p.get("ids", "0,7,2"), "DBZ3_TEST_RINGOUT": "1"}, _batalla),
    "freecam": ("camara libre: 3 poses por variable, o el camino REAL con --par teclas=1 (F1 con teclas + WASD/QE/"
                "raton reales, Pausar/Ocultar HUD, R); --par modo=iw|iwmini|b2 la prueba en IW o B2",
                _freecam_env, _freecam),
    # con perfil_nuevo la rueda es otra (W x2 caia en Bardock y se quedaba en la pantalla de capsulas):
    # se reutiliza el camino verificado de du_continuar y se avanza hasta acabar el siguiente combate
    "campana": ("DU > Continuar > Freezer > hasta acabar el siguiente combate (ring out a los 5 s)",
                lambda p: {"DBZ3_TEST_RINGOUT": "1"},
                lambda j, p: _du_continuar(j, dict({"hasta_combate": "1", "iter": "40"}, **p))),
    "online_crear": ("Duelo > ONLINE > CREAR PARTIDA con un equipo de Tag Team elegido antes",
                     lambda p: {"DBZ3_NET_LOCAL": "1", **({"DBZ3_NET_TEAM": "0,11"} if p.get("equipo", "1") == "1" else {})},
                     _online_crear),
    "historia_y_duelo": ("combate normal tras volver de un combate de historia de mod: sin su forma ni su traje",
                         lambda p: {"DBZ3_TEST_STORY_WORD": p.get("palabra", "0x15920006"),
                                    "DBZ3_TEST_BATTLE": p.get("ids", "0,7,2"), "DBZ3_TEST_RINGOUT": "1",
                                    **({"DBZ3_TEST_KEEP_STORY": "1"} if p.get("antes") == "1" else {})},
                         _historia_y_duelo),
    "duelo_fundido": ("entrar y salir del Duelo con rafagas de capturas (recuadro ONLINE y fundido del juego)",
                      lambda p: {}, _duelo_fundido),
    "hub_online_fundido": ("HUB > Online: llegada al Duelo y pantalla ONLINE con rafagas de capturas",
                           lambda p: {}, _hub_online_fundido),
    "tienda": ("HUB > Supervivencia con tienda: gana el primer combate y captura la tienda (piezas de la Skill Shop)",
               lambda p: {"DBZ3_TEST_RINGOUT": "1"}, _tienda),
    "hub_lab": ("HUB (fondo propio, sin tiras) > Laboratorio: fondo y selector de luchador y rival",
                lambda p: {}, _hub_lab),
    "heroe": ("HUB > Kumite > Modo Heroe: crear (Humano, Krilin, traje 2, maestro Krilin) y empezar la mision",
              lambda p: {"DBZ3_TEST_RINGOUT": "1"}, _heroe_crear),
    "r5_heroe": ("Modo Heroe (R5): cuerpo del rpg.toml con el moveset de otro luchador (estilo) en combate",
                 lambda p: {"DBZ3_TEST_RINGOUT": "1"} if p.get("ringout") == "1" else {}, _r5_heroe),
    "heroe_pieza": ("DS130/DS133/DS154/DS155: Modo Heroe en combate con UNA pieza puesta (--par pelo=N 0-10 y/o guante=N: %s); "
                    "presentacion, de pie, andando, golpeando, PRIMER PLANO DE FRENTE de la cabeza "
                    "(h_cara_frente_* al empezar la pelea, camara DBZ3_TEST_CARA; --par cara=0 lo quita); con guante=N "
                    "ademas 4 planos de cada brazo DEL HOMBRO A LA MANO (h_brazo_der_*/h_brazo_izq_*: codo y mano, de "
                    "frente y de espalda), con el combate QUIETO y el heroe en reposo (--par quieto=0 lo quita) y "
                    "rafaga dbz3_shot.req"
                    % ", ".join(str(x) for x in PIEZAS_GUANTES),
                    _heroe_pieza_env,
                    _heroe_pieza, _heroe_pieza_prep),
    "fusion_yamhan": ("combate normal Yamcha vs Vegeta: a los 3 s se fusiona en Yamhan (src/fusion.cpp)",
                      lambda p: dict({"DBZ3_TEST_BATTLE": p.get("ids", "13,7,2")},
                                     **({} if p.get("gesto") == "1" else {"DBZ3_TEST_FUSION": p.get("seg", "3")})),
                      _fusion_yamhan),
    "tag_swap": ("Tag Team 1P vs COM, relevo con SELECT",
                 lambda p: dict({"DBZ3_TAG_SLOW": "3"},
                                **({} if p.get("swap", "1") == "1" else {"DBZ3_TEST_KO": "1"})),
                 _tag_swap),
}


# Luna 13: escenario dedicado, registrado una vez definido ESCENARIOS.
ESCENARIOS["luna13_nave_rueda"] = (
    "Luna 13: nave de Babidi, navegación de _r2_nave hasta la rueda 1P",
    lambda p: dict(_PS2, DBZ3_TEST_MISIONES="5,reset", DBZ3_TEST_NAVE_TIEMPO="6",
                   DBZ3_TEST_NAVE_TODO="1"), _luna13_nave_rueda)
ESCENARIOS["luna13_b2_vineta"] = (
    "Luna 13: viñeta P del tablero B2, navegación manual como _tablero",
    lambda p: dict(_PS2, DBZ3_TEST_MISIONES=f"1,reset,desde={int(p.get('fase', '4')) - 1}",
                   DBZ3_TEST_B2_ORDENES="1", DBZ3_TEST_RINGOUT="1"),
    _luna13_b2_vineta)


# ---------- X0e: saltar a una escena (DBZ3_TEST_ESCENA) ----------
# Sintaxis: <juego>:<escena>[:<parametro>] ; varias separadas por "|" (cada una en su partida).
#   b2:fase3        tablero de B2, fase III (auto)     b2:nave[:prueba2]   Nave de Babidi      b2:tablero  b2:deseo
#   iw:mini:0x7D9   minijuego de IW por clave          iw:mision:5         Misiones del Dragon desde la 5
#   satan:ronda2    Leyenda de Mr. Satan desde el paso 2                   video:FD01B         video SFD de IW
#   batalla:0,7,2   combate nativo con esos ids        menu                menu principal (control)
# Cada salto da el env de los ganchos DBZ3_TEST_* que ya existen y el log que prueba haber llegado. `:evento<N>`
# (b2) se acepta pero el juego aun no tiene gancho para saltar a un evento de guion: [H], se avisa y se ignora.
_PS2 = {"DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}


def _salto_b2(r, extra):
    m = re.fullmatch(r"fase(\d+)", r[0]) if r else None
    if m:
        n = int(m.group(1))
        if not 1 <= n <= 9:
            raise SystemExit(f"b2:fase{n}: la fase va de 1 a 9")
        pre = "1,auto,reset" + (f",desde={n - 1}" if n > 1 else "")
        return dict(_PS2, DBZ3_TEST_MISIONES=pre, DBZ3_TEST_RINGOUT="1"), rf"tablero b2: fase {n} leida"
    if r and r[0] == "nave":
        env = dict(_PS2, DBZ3_TEST_MISIONES="5,reset")
        for x in extra:
            m = re.fullmatch(r"prueba(\d)", x)
            if m:
                env["DBZ3_TEST_NAVE_PRUEBA"] = m.group(1)
        return env, r"nave: entrar"
    if r and r[0] == "tablero":
        return dict(_PS2, DBZ3_TEST_MISIONES="1,reset"), r"tablero b2: fase 1 leida"
    if r and r[0] == "deseo":
        return dict(_PS2, DBZ3_TEST_MISIONES="1,reset", DBZ3_TEST_B2_DESEO="1", DBZ3_TEST_SHEN_ANIM="2"), r"tablero b2: fase 1 leida"
    raise SystemExit(f"b2:{':'.join(r)}: usa fase<1-9>, nave[:prueba<0-3>], tablero o deseo")


def resolver_escena(spec):
    """'b2:fase3:evento12' -> (env, regex de llegada, aviso). SystemExit si no se entiende (antes de tocar el juego)."""
    partes = [p.strip() for p in spec.split(":") if p.strip()]
    if not partes:
        raise SystemExit("escena vacia")
    juego, resto = partes[0].lower(), partes[1:]
    aviso = ""
    extra = [x for x in resto if re.fullmatch(r"evento\d+", x)]
    resto = [x for x in resto if x not in extra]
    if juego == "b2":
        env, rx = _salto_b2(resto, [])
        env["DBZ3_TEST_B2_ORDENES"] = "1"             # el port escribe cada orden ejecutada (comparar_modo.py b2)
        if extra:                                     # evento<N> = guion N de la fase, ademas del suyo (gancho de pasarela_b2.cpp)
            env["DBZ3_TEST_B2_EVENTO"] = extra[0][6:]
        for x in resto:                               # parametros sueltos (prueba2) para nave
            m = re.fullmatch(r"prueba(\d)", x)
            if m:
                env["DBZ3_TEST_NAVE_PRUEBA"] = m.group(1)
        return env, rx, aviso
    if juego == "iw" and resto[:1] == ["mini"] and len(resto) == 2:
        if not re.fullmatch(r"0x[0-9A-Fa-f]+", resto[1]):
            raise SystemExit(f"iw:mini:<clave>: la clave es hexadecimal (0x7D9), recibido {resto[1]}")
        clave = int(resto[1], 16)
        if clave in (0x7D4, 0x7D5, 0x7DA, 0x7DB, 0x7DC):
            llegada = r"r14: en juego"
        elif clave in (0x7D6, 0x7E0):
            llegada = r"terminado; empieza el vuelo"
        else:
            llegada = r"iwmini: salida puesta"
        return dict(_PS2, DBZ3_TEST_IWMINI=resto[1] + ",auto"), llegada, aviso
    if juego == "iw" and resto[:1] == ["mision"] and len(resto) == 2 and resto[1].isdigit():
        return (dict(_PS2, DBZ3_TEST_MISIONES=f"4,reset,desde={resto[1]}", DBZ3_TEST_RINGOUT="1"),
                r"misiones: iw(?: \(\d+ pasos\)|: mapa de Dragon Mission)", aviso)
    if juego == "satan":
        m = re.fullmatch(r"ronda(\d+)", resto[0]) if resto else None
        if not m:
            raise SystemExit("satan:ronda<N> (N=0 es el titulo)")
        n = int(m.group(1))
        return (dict(_PS2, DBZ3_TEST_PASARELA=f"auto,seg={os.environ.get('DBZ3_SEG_PRUEBA', '8')},desde={n}", DBZ3_TEST_RINGOUT="1"),
                rf"pasarela: paso {n + 1}/\d+", aviso)
    if juego == "video" and len(resto) == 1:
        return dict(_PS2, DBZ3_TEST_VIDEO=resto[0]), r"video|sfd", aviso
    if juego == "batalla":
        ids = resto[0] if resto else "0,7,2"
        return {"DBZ3_TEST_BATTLE": ids, "DBZ3_TEST_RINGOUT": "1"}, r"hub: combate nativo|battle", aviso
    if juego == "menu":
        return {}, "", aviso
    if juego == "du" and resto == ["mapa"]:
        return {}, "", "du:mapa: usa el escenario du_mapa (--instantanea freezer_cap2)"
    if juego == "heroe" and resto == ["tecnicas"]:
        return {"DBZ3_TEST_HEROE_TEC": "1", "DBZ3_TEST_RINGOUT": "1"}, "", "heroe:tecnicas: usa el escenario heroe_tecnicas"
    raise SystemExit(f"escena desconocida '{spec}': b2:fase<N>|b2:nave|b2:tablero|b2:deseo, iw:mini:<clave>, "
                     f"iw:mision:<N>, satan:ronda<N>, video:<id>, batalla:<ids>, menu")


# X0g: fin de cada escena (el log que dice "acabo") y firma (lineas del log que deben salir IGUALES a x1, x2 y x4).
FIN_ESCENA = {"b2:fase": r"tablero b2: guiones terminados",
              "iw:mini": r"iwmini: (META|TIEMPO|Bubbles atrapado|Piccolo te alcanza)",
              "batalla": r"hub: fin del combate nativo",
              "satan": r"pasarela: paso \d+/\d+ \(charla",
              "b2:nave": r"nave: (fin|resultado)|hub: fin del combate nativo",
              "b2:tablero": r"tablero b2: guiones terminados"}
FIRMA_ESCENA = {"b2:fase": r"tablero b2: (escena|script \d+|orden ejecutada|guiones terminados|fase)",
                "iw:mini": r"iwmini: (curso|salida puesta|entrada acabada|aro \d+ cogido|bebida|META|TIEMPO|final de IW)",   # sin las lineas "jugador (...)": son muestras por reloj
                "batalla": r"dbz3 (hub: combate nativo|tag: combate acabado|hub: fin del combate nativo)",
                "b2:nave": r"nave: (pantalla|eleccion|menu|prueba \d+:|voz de Babidi|musica)|hub: (combate nativo|fin del combate)",
                "b2:tablero": r"tablero b2: (escena|script \d+|orden ejecutada|guiones terminados|fase)",
                "satan": r"pasarela: (paso \d+/\d+|prologo|titulo)|hub: (combate nativo|fin del combate)"}


def _clave_escena(spec):
    s = spec.lower().split("|")[0]
    for k in sorted(FIN_ESCENA, key=len, reverse=True):
        if s.startswith(k):
            return k
    return None


def _fin_escena(spec):
    """Fin propio de los minijuegos de IW: R14 (botones) y R12 (vuelo) acaban con sus propias lineas; el marcador
    generico de iw:mini solo vale para los campos (MSNMW). Asi `correr ... --par fin=1` puede esperarlos."""
    if spec.startswith("iw:mini:"):
        try:
            clave = int(spec.split(":")[2], 16)
        except (ValueError, IndexError):
            return FIN_ESCENA.get("iw:mini")
        if clave in (0x7D4, 0x7D5, 0x7DA, 0x7DB, 0x7DC):
            return r"r14: (resultado|fin: )"
        if clave in (0x7D6, 0x7E0):
            return r"r12: (resultado|fin \()"
    k = _clave_escena(spec)
    return FIN_ESCENA.get(k) if k else None


def _escena_env(p):
    spec = p.get("escena") or os.environ.get("DBZ3_TEST_ESCENA", "")
    return resolver_escena(spec.split("|")[0])[0] if spec else {}


def _escena(j, par):
    spec = (par.get("escena") or os.environ.get("DBZ3_TEST_ESCENA", "")).split("|")[0]
    _, rx, aviso = resolver_escena(spec)
    if aviso:
        j.info(aviso)
    if spec in ("du:mapa", "heroe:tecnicas"):                # escenas que necesitan pasos de menu
        return {"du:mapa": _du_mapa, "heroe:tecnicas": _heroe_tecnicas}[spec](j, par)
    t_log = time.time()
    if rx:
        j.log(rx, float(par.get("espera", "60")))
    # tiempo desde el lanzamiento hasta que el log prueba que la escena esta puesta (lo que pide X0e: < 30 s)
    j.t_escena = time.time() - j.t0
    j.info(f"escena '{spec}' puesta a los {j.t_escena:.1f} s del lanzamiento")
    if par.get("fin") == "1":                         # X0g: dejarla correr hasta su final (prueba de igualdad)
        rx_fin = _fin_escena(spec)
        if not rx_fin:
            raise Fallo(f"'{spec}' no tiene fin definido (FIN_ESCENA)")
        j.log(rx_fin, float(par.get("espera_fin", "240")))
        j.t_fin_escena = time.time() - j.t0
        dormir(0.5)
        return
    if par.get("teclas"):                             # X0: el MISMO guion de teclas que el original (comparar_modo.py)
        import comparar_modo as CM
        if spec.startswith("b2:"):                    # lleva el tablero al estado del savestate 3 del original: pieza por elegir
            j.tecla("Space", 2500)
            j.log(r"tablero b2: escena T155", 20)
            j.tecla("Backspace", 1500)
            j.log(r"tablero b2: escena M207", 30)
            dormir(1.5)
            j.tecla_hasta_log("Space", r"tablero b2: script \d+: eleccion de \d+ aliado", intentos=10, seg=4)
            j.tecla_hasta_log("Space", r"tablero b2: aliado elegido", intentos=4, seg=3)
            j.log(r"tablero b2: guiones terminados", 60)
        t_teclas = time.time()
        for k, v in CM.traducir_teclas(par["teclas"]):
            if k == "esperar":
                dormir(v)
            elif k == "captura":
                j.captura(f"{spec.split(':')[-1]}_{v}")    # fase1_lista: el nombre que espera el dorado
            else:
                j.tecla(k, 150)
        j.info(f"guion de teclas repetido en {time.time() - t_teclas:.1f} s")
        time.sleep(max(0.0, float(par.get("quieto", "1.5")) - (time.time() - t_teclas)))
    else:
        dormir(float(par.get("quieto", "1.5")))
    if par.get("min_lum"):                            # X1: escenas que salen de un fundido a negro (Satan)
        j.captura_estable("escena", intentos=14, min_lum=float(par["min_lum"]))
    else:
        j.captura("escena")


ESCENARIOS["escena"] = ("X0e: salta a una escena (--par escena=b2:fase3|iw:mini:0x7D9|satan:ronda2|batalla:0,7,2|menu)",
                        _escena_env, _escena)


def escenas(spec, copias, env_extra=()):
    """Una partida por escena (las de 'a|b|c'), repartidas entre las copias; tabla con el tiempo de salto."""
    lista = [s for s in spec.split("|") if s.strip()]
    for s in lista:
        resolver_escena(s)                            # valida todas antes de lanzar nada
    libres = queue.Queue()
    n1 = _es_n1(env_extra)
    for c in copias[:max(1, min(len(copias), MAX_PARALELO_N1 if n1 else MAX_PARALELO))]:
        libres.put(c)

    def una(s):
        c = libres.get()
        try:
            r = correr(c, "escena", env_extra, None, {"escena": s})
            r["escena"] = s
            return r
        except (Exception, SystemExit) as e:
            return {"ok": False, "nombre": f"{c}/escena", "dur": 0.0, "msg": str(e), "run": None, "escena": s}
        finally:
            libres.put(c)

    with ThreadPoolExecutor(libres.qsize()) as ex:
        res = list(ex.map(una, lista))
    print("\n== SALTO A ESCENA ==", flush=True)
    for r in res:
        print(f"{'PASS' if r['ok'] else 'FAIL'}  {r['escena']:<24} salto {r.get('t_escena') or 0:5.1f} s  "
              f"{r['msg'][:90]}", flush=True)
    return all(r["ok"] for r in res)


# ---------- X0g: igualdad del turbo y FPS por numero de instancias (todo en N1: oculto, sin audio, x1) ----------
_PREFIJO = re.compile(r"\[[^\]]*\] \[[^\]]*\] \[[^\]]*\] \[t\d+\] (.*)")


def _lineas_log(run):
    txt = (Path(run) / "dbz3.log").read_text(encoding="utf-8", errors="replace")
    for l in txt.splitlines():
        m = _PREFIJO.match(l)
        yield (m.group(1) if m else l), l


def firma(run, escena):
    """Lineas del log de la escena que deben ser iguales a cualquier turbo (FIRMA_ESCENA), sin hora ni hilo."""
    k = _clave_escena(escena)
    rx = re.compile(FIRMA_ESCENA[k])
    return [s for s, _ in _lineas_log(run) if rx.search(s)]


def _hora(linea):
    m = re.match(r"\[(\d{4}-\d\d-\d\d) (\d\d):(\d\d):(\d\d\.\d+)\]", linea)
    return int(m.group(2)) * 3600 + int(m.group(3)) * 60 + float(m.group(4)) if m else None


def fps_combate(run, ini=r"hub: combate nativo", fin=r"hub: fin del combate nativo"):
    """(FPS medianos del hilo de GPU dentro del combate, segundos de pared del combate, n de medidas)."""
    t_ini = t_fin = None
    medidas = []
    for s, l in _lineas_log(run):
        h = _hora(l)
        if h is None:
            continue
        if t_ini is None and re.search(ini, s):
            t_ini = h
        elif t_ini is not None and t_fin is None and re.search(fin, s):
            t_fin = h
        m = re.search(r"\[gpu\].*perf fps=([\d.]+)", l)
        if m and t_ini is not None and t_fin is None:
            medidas.append(float(m.group(1)))
    if t_ini is None or t_fin is None:
        return None, None, 0
    medidas.sort()
    return (medidas[len(medidas) // 2] if medidas else None), t_fin - t_ini, len(medidas)


def _lote(escenas, copias, env, par_extra=None):
    """Una partida por (escena, copia), todas a la vez; devuelve los resultados en el mismo orden."""
    def una(a):
        esc, cop = a
        try:
            return correr(cop, "escena", env, None, dict({"escena": esc}, **(par_extra or {})))
        except (Exception, SystemExit) as e:
            return {"ok": False, "nombre": f"{cop}/escena", "msg": str(e), "run": None, "dur": 0.0}
    with ThreadPoolExecutor(len(copias)) as ex:
        return list(ex.map(una, list(zip(escenas, copias))))


_NUM = re.compile(r"-?\d+(?:\.\d+)?")
TOL_TIEMPO = 1.5          # s: los cronometros del juego usan tambien reloj de pared; el resto de la linea debe ser identico


def comparar_firmas(a, b, tol=TOL_TIEMPO):
    """('igual' | 'tolerancia' | 'distinto', max diferencia numerica, primeras diferencias). Misma estructura de lineas;
    'tolerancia' = solo cambian numeros y ninguno mas de tol (cronometros)."""
    if len(a) != len(b):
        return "distinto", None, [f"{len(a)} lineas frente a {len(b)}"]
    estado, mx, difs = "igual", 0.0, []
    for x, y in zip(a, b):
        if x == y:
            continue
        if _NUM.sub("#", x) != _NUM.sub("#", y):
            return "distinto", None, [f"{x[:90]}  <>  {y[:90]}"]
        # solo los numeros con decimales son cronometros; un entero distinto (lado 1 / lado 2) es una diferencia real
        d = max((abs(float(p) - float(q)) if ('.' in p or '.' in q) else (0.0 if p == q else 1e9))
                for p, q in zip(_NUM.findall(x), _NUM.findall(y)))
        mx = max(mx, d)
        difs.append(f"{x[:90]}  <>  {y[:90]}")
        estado = "tolerancia" if d <= tol and estado != "distinto" else "distinto"
        if estado == "distinto":
            return "distinto", mx, difs[:3]
    return estado, mx, difs[:3]


def igualdad(escenas, turbos, copias):
    """Mismas escenas hasta su fin a x1, x2, x4...: la firma del log tiene que salir identica."""
    ref, out = {}, []
    for T in turbos:
        env = ["DBZ3_TEST_OCULTO=1", f"DBZ3_TEST_TURBO={T}"]
        res = _lote(escenas, copias[:len(escenas)], env, {"fin": "1"})
        for esc, r in zip(escenas, res):
            if not r["ok"] or not r.get("run"):
                out.append((T, esc, "FALLO", r["msg"][:100], None))
                continue
            sig = firma(r["run"], esc)
            if T == turbos[0]:
                ref[esc] = sig
            est, mx, difs = comparar_firmas(sig, ref[esc])
            seg = (r["t_fin"] - r["t_escena"]) if r.get("t_fin") and r.get("t_escena") else None
            out.append((T, esc, {"igual": "IGUAL", "tolerancia": "TOLERANCIA", "distinto": "DISTINTO"}[est],
                        f"{len(sig)} lineas" + (f", max dif {mx:.2f}" if mx else "") + (f" | {difs[0]}" if difs else ""),
                        seg))
    print("\n== IGUALDAD DEL TURBO ==", flush=True)
    for T, esc, v, d, seg in out:
        print(f"x{T:<2} {esc:<20} {v:<9} escena->fin {seg if seg is None else round(seg, 1)} s  {d}", flush=True)
    (PRUEBAS / f"{ts()}_igualdad.txt").write_text("\n".join(f"x{T} {e} {v} {s} {d}" for T, e, v, d, s in out), encoding="utf-8")
    return all(v in ("IGUAL", "TOLERANCIA") for _, _, v, _, _ in out)


SIG_GENERICA = r"dbz3 (heroe|tag|hub|nave|tablero b2|pasarela|rpg|campaigns?): (?!prueba: estado del combate)"   # el estado final depende de cuando se cierra


def _estructura(x):
    """Solo la estructura del suceso: sin direcciones del guest (8 hex), ni decimales (posiciones, tiempos), ni ticks."""
    x = re.sub(r"\(\d+ ticks de carga\)", "(N ticks)", x)
    x = re.sub(r"\b[0-9A-F]{8}\b", "HEX", x)
    return re.sub(r"-?\d+\.\d+", "F", x)


def igualdad_esc(items, turbos, copias, firma_rx=SIG_GENERICA, estructura=False, hasta=None, sin=None):
    """X0h: igualdad del turbo para escenarios por nombre o .toml ('esc[@instantanea]'): PASS en todos los turbos y la
    misma firma de log (lineas de firma_rx, sin hora). Para las familias que no son una 'escena' (Heroe, Tag Team...)."""
    rx = re.compile(firma_rx)
    ref, out = {}, []
    for T in turbos:
        env = ["DBZ3_TEST_OCULTO=1", f"DBZ3_TEST_TURBO={T}"]

        def una(a):
            it, cop = a
            esc, _, inst = it.partition("@")
            try:
                return correr(cop, esc, env, inst or None, {})
            except (Exception, SystemExit) as e:
                return {"ok": False, "nombre": f"{cop}/{esc}", "msg": str(e), "run": None, "dur": 0.0}
        with ThreadPoolExecutor(len(items)) as ex:
            res = list(ex.map(una, list(zip(items, copias[:len(items)]))))
        for it, r in zip(items, res):
            if not r["ok"] or not r.get("run"):
                out.append((T, it, "FALLO", r["msg"][:100], r.get("dur")))
                continue
            sig = [_estructura(x) if estructura else re.sub(r"\b\d+ ms\b", "N ms", re.sub(r"\(\d+ ticks de carga\)", "(N ticks de carga)", x)) for x, _ in _lineas_log(r["run"]) if rx.search(x)]   # la carga del modelo es de pared
            if sin:      # lineas que dependen de cuantas teclas hicieron falta (p. ej. 'opcion N' del menu)
                sig = [x for x in sig if not re.search(sin, x)]
            if hasta:    # solo hasta el primer suceso que marca el final de lo determinista (el resto depende de la IA del COM)
                for k, x in enumerate(sig):
                    if re.search(hasta, x):
                        sig = sig[:k + 1]
                        break
            ref.setdefault(it, sig)    # la primera corrida que acaba bien es la referencia
            est, mx, difs = comparar_firmas(sig, ref[it])
            out.append((T, it, {"igual": "IGUAL", "tolerancia": "TOLERANCIA", "distinto": "DISTINTO"}[est],
                        f"{len(sig)} lineas" + (f", max dif {mx:.2f}" if mx else "") + (f" | {difs[0]}" if difs else ""),
                        r.get("dur")))
    print("\n== IGUALDAD DEL TURBO (escenarios) ==", flush=True)
    for T, it, v, d, dur in out:
        print(f"x{T:<2} {it:<28} {v:<9} dur {dur if dur is None else round(dur, 1)} s  {d}", flush=True)
    (PRUEBAS / f"{ts()}_igualdad_esc.txt").write_text("\n".join(f"x{T} {e} {v} {s} {d}" for T, e, v, d, s in out), encoding="utf-8")
    return all(v in ("IGUAL", "TOLERANCIA") for _, _, v, _, _ in out)


def medir_fps(ns, turbo, escena, copias):
    """FPS del hilo de GPU en combate con n juegos a la vez (todos oculto, sin audio, x1)."""
    filas = []
    for n in ns:
        env = ["DBZ3_TEST_OCULTO=1", f"DBZ3_TEST_TURBO={turbo}"]
        res = _lote([escena] * n, copias[:n], env, {"fin": "1"})
        v = [fps_combate(r["run"]) for r in res if r.get("run")]
        fps = [x[0] for x in v if x[0]]
        seg = [x[1] for x in v if x[1]]
        filas.append((n, sum(r["ok"] for r in res), fps, seg))
        print(f"  n={n} turbo=x{turbo}: ok {filas[-1][1]}/{n}  fps por juego {fps}  combate {[round(s, 1) for s in seg]} s",
              flush=True)
    print("\n== FPS POR INSTANCIAS (turbo x%d, %s) ==" % (turbo, escena), flush=True)
    for n, ok, fps, seg in filas:
        print(f"{n} juegos: ok {ok}/{n}  FPS medio {sum(fps) / len(fps) if fps else 0:6.1f}  minimo {min(fps) if fps else 0:6.1f}  "
              f"total {sum(fps):6.1f}  combate {sum(seg) / len(seg) if seg else 0:5.1f} s", flush=True)
    return filas


AJUSTES = {   # X1: variantes para medir el coste de una opcion marcada beta/experimental (misma escena, mismo PC)
    "base": 'dbz3_relight=0;dbz3_present_effect="fsr"',
    "relight1": 'dbz3_relight=1;dbz3_present_effect="fsr"',
    "relight2": 'dbz3_relight=2;dbz3_present_effect="fsr"',
    "fsr3": 'dbz3_relight=0;dbz3_present_effect="fsr3"',
}


def medir_ajustes(nombres, copia="x1_hub", instantanea="freezer_cap2", escena="batalla"):
    """FPS / p1 / p99 del hilo de GPU en el combate de `escena` con cada ajuste de AJUSTES (uno tras otro: 1 juego)."""
    out = {}
    for n in nombres:
        os.environ["DBZ3_TEST_CFG"] = AJUSTES[n]
        try:
            r = correr(copia, escena, (), instantanea, {})
        finally:
            os.environ.pop("DBZ3_TEST_CFG", None)
        fps, p1, p99 = [], [], []
        t_ini = t_fin = None
        for sl, l in _lineas_log(r["run"]):
            h = _hora(l)
            if h is None:
                continue
            if t_ini is None and re.search(r"hub: combate nativo", sl):
                t_ini = h
            elif t_ini is not None and t_fin is None and re.search(r"hub: fin del combate nativo", sl):
                t_fin = h
            m = re.search(r"\[gpu\].*perf fps=([\d.]+) p1=([\d.]+) p99_ms=([\d.]+)", l)
            if m and t_ini is not None and t_fin is None:
                fps.append(float(m[1])); p1.append(float(m[2])); p99.append(float(m[3]))
        med = lambda v: sorted(v)[len(v) // 2] if v else None
        out[n] = dict(ok=r["ok"], n=len(fps), fps=med(fps), p1=med(p1), p99_ms=med(p99), min_fps=min(fps) if fps else None)
        print(f"AJUSTE {n}: {out[n]}", flush=True)
    os.environ["DBZ3_TEST_CFG"] = AJUSTES["base"]    # no dejar el ultimo ajuste (p. ej. fsr3) puesto en la copia
    try:
        escribir_toml(BASE / copia / "dbz3_user.toml")
    finally:
        os.environ.pop("DBZ3_TEST_CFG", None)
    return out


def _datos_recarga(j, par):
    # X0d: el juego lee res\datos\*.toml y los recarga al cambiar (en pruebas) o con F5. Escribe un fichero de prueba en
    # la copia con fov=0.5, lo cambia a 0.9 y a 1.3 sin reiniciar y mide cuanto tarda el juego en verlo (log).
    f = j.dest / "res" / "datos" / "prueba_datos.toml"
    f.parent.mkdir(parents=True, exist_ok=True)
    out = {}
    for valor in ("0.5", "0.9", "1.3"):
        t0 = time.time()
        f.write_text(f"[camara]\nfov = {valor}\n", encoding="utf-8")
        j.log(rf"datos: prueba: prueba_datos camara\.fov = {re.escape(valor)}\b", 15)
        out[valor] = time.time() - t0
        j.info(f"fov={valor} visto en el juego tras {out[valor]:.2f} s sin reiniciar")
    if par.get("f5") == "1":                              # tecla F5 (bind de SDL del juego): informativo, no falla
        j.tecla("F5", 300)
        try:
            j.log(r"datos: F5: \d+ ficheros recargados", 3)
            j.info("F5 de pruebas (dbz3_datos.req) recibido por el juego")
        except Fallo:
            j.info("F5: la tecla virtual de pruebas NO llega al bind de SDL [H]: probar F5 a mano en una ventana normal")
    (j.run / "datos_recarga.txt").write_text("".join(f"fov {k}: {v:.2f} s\n" for k, v in out.items()), encoding="utf-8")


ESCENARIOS["datos_recarga"] = ("X0d: un cambio en res\\datos llega al juego en marcha sin recompilar (--par f5=1 prueba tambien F5)",
                               lambda p: {"DBZ3_TEST_DATOS": "prueba_datos:camara.fov"}, _datos_recarga)


def _forma_ltrt(j, par):
    # X1: selector de forma inicial LT/RT (E = RT en el teclado del juego). En la rueda, "D+E" mueve el cursor (el
    # selector solo actua con la rueda en uso, <250 ms) y pulsa RT: debe salir el rotulo "Forma inicial" en el
    # lado izquierdo. Medida: el recuadro del rotulo cambia mucho mas que entre dos capturas sin tocar nada.
    _ir_a_rueda(j)
    dormir(2)
    a1 = j.captura("a1_sin_tocar")
    a2 = j.captura("a2_sin_tocar")
    pasos = int(par.get("rt", "1"))
    ok = []
    for i in range(pasos):
        j.tecla("A+E" if i % 2 == 0 else "D+E", 120)
        ok.append(j.captura(f"b{i + 1}_rt"))
    j.info(f"capturas: {[Path(x).name for x in [a1, a2] + ok]}")
    ref = _recuadro(a1, ZONA_FORMA)
    ruido = _dif(ref, _recuadro(a2, ZONA_FORMA))
    difs = [_dif(ref, _recuadro(x, ZONA_FORMA)) for x in ok]
    j.info(f"diferencia del recuadro: ruido {ruido:.2f}, tras RT {[round(d, 2) for d in difs]}")
    if not any(d > max(3 * ruido, 4.0) for d in difs):
        raise Fallo(f"el rotulo de forma inicial no aparece tras RT (ruido {ruido:.2f}, medidas {difs})")


# Fraccion del lienzo 1280x720: el chip de forma ("Normal - Ki x" / "Forma N - ..."). DS142 lo movio de la
# banda bajo la franja del nombre (0.16,0.305)-(0.36,0.345) a la banda azul sobre la lista de estadisticas,
# a la derecha del aro (x 492..1110 u, y 113..164 u del area 4:3 -> ~554..1050 x 90..133 px a 720p).
ZONA_FORMA = (0.42, 0.12, 0.83, 0.19)


def _recuadro(png, z):
    im = Image.open(png).convert("L")
    w, h = im.size
    return im.crop((int(z[0] * w), int(z[1] * h), int(z[2] * w), int(z[3] * h)))


def _dif(a, b):
    from PIL import ImageChops
    return sum(ImageChops.difference(a, b).getdata()) / (a.size[0] * a.size[1])


ESCENARIOS["forma_ltrt"] = ("X1: LT/RT en la rueda muestra el rotulo de forma inicial (--instantanea freezer_cap2)",
                            lambda p: {}, _forma_ltrt)


def _musica_combate(j, par):
    # X1: con dbz3_musica_combate = aleatoria[:fuente] el combate arranca una pista de la copia del jugador y al acabar
    # la corta (log "dbz3 musica"). Sin audio real (la prueba va silenciada): se mide el log, no el sonido.
    _batalla(j, par)
    t = j.texto_log()
    m = re.search(r"dbz3 musica: combate: (.+)", t)
    if not m:
        raise Fallo("el combate no arranco ninguna pista de musica (log 'dbz3 musica: combate:')")
    if "no hay pistas" in m[1]:
        raise Fallo(f"sin pistas para la fuente pedida: {m[1]}")
    j.info(f"pista elegida: {m[1][:100]}")
    # DBZ3_AUDIO_VERIFICA (pasarela_b1.cpp): decodifica la pista elegida y apunta su RMS por segundo (winmm no entra en el volcado)
    j.log(r"VERIFICA musica .*RMS por segundo", 20)
    v = re.findall(r"VERIFICA musica (\S+) (\d+): ([\d.]+) s a (\d+) Hz, RMS por segundo:([ \d]*)", j.texto_log())
    if not v:
        raise Fallo("la pista elegida no se pudo decodificar (VERIFICA sin datos)")
    rms = [int(x) for x in v[-1][4].split()]
    j.info(f"audio verificado: {v[-1][0]} #{v[-1][1]}, {v[-1][2]} s a {v[-1][3]} Hz, RMS/s {rms}")
    if not rms or max(rms) < 200:
        raise Fallo(f"la pista elegida suena casi en silencio (RMS {rms})")
    j.log(r"dbz3 musica: fin del combate: musica cortada", 5)


def _cfg_musica(p):
    os.environ["DBZ3_TEST_CFG"] = f'dbz3_musica_combate="aleatoria:{p.get("fuente", "b3")}"'
    return {"DBZ3_TEST_BATTLE": "0,7,2", "DBZ3_TEST_RINGOUT": "1", "DBZ3_PS2_GAMES": str(BASE / "ps2_games"), "DBZ3_AUDIO_VERIFICA": "1"}


ESCENARIOS["musica_combate"] = ("X1: musica de combate elegida (--par fuente=b3|b2|iw|b1): suena en el combate y se corta al acabar",
                                _cfg_musica, _musica_combate)


def _menu_rapido(j, par):
    # X1: abre los ajustes rapidos con el mando (Back + Start = Tab + Return del teclado virtual) y captura la seccion
    # SONIDO Y MANDO (LB/RB cambian de seccion: Q / E no, son los gatillos; se avanza con la tecla `seccion`).
    dormir(2)
    j.tecla("Tab+Return", 400)
    dormir(1)
    j.captura("a_menu_rapido")
    for i in range(int(par.get("secciones", "3"))):
        j.tecla(par.get("seccion", "3"), 600)
    dormir(1)
    j.captura("b_seccion")


ESCENARIOS["menu_rapido"] = ("X1: abre el menu rapido (Back+Start) y captura (--par secciones=N seccion=<tecla LB/RB>)",
                             lambda p: {"DBZ3_PS2_GAMES": str(BASE / "ps2_games")}, _menu_rapido)


def _heroe_a_creacion(j):
    dormir(2.5)
    j.tecla("W", 1200)
    j.tecla("Space", 3500)                              # HUB
    for _ in range(5):                                  # tagteam .. kumite
        j.tecla("S", 450)
    j.tecla("Space", 2500)                              # selector de retos
    for _ in range(3):                                  # kumite .. modo heroe
        j.tecla("S", 700)
    j.tecla("Space", 2500)                              # pantalla de creacion


def _heroe_tecnicas(j, par):
    # C2 / Luna 13: desde Apariencia, RB abre la pestana Combate y abajo selecciona Moveset (fila 4).
    # DBZ3_TEST_HEROE_TEC=1 abre la pestaña Tecnicas al entrar a Movesets. Equipar 2 cartas y volver.
    _heroe_a_creacion(j)
    j.captura("a_creacion")
    j.tecla("3", 600)                                   # RB: Apariencia -> Combate
    j.tecla("S", 600)                                   # Combat: Estilo -> Moveset
    j.tecla_hasta_log("Space", r"heroe: pantalla de movesets abierta", 3, 5)
    # Las cartas se cachean en la copia; una segunda pasada puede encontrar listo.txt ya creado
    # y no volver a escribir el mensaje de log. Esperar el artefacto o la generación de esta sesión.
    cartas_listo = j.dest / "ingame_ui" / "heroe_tecnicas" / "listo.txt"
    fin_cartas = time.time() + 30
    while not cartas_listo.is_file() and not re.search(r"heroe: vista previa cartas generada", j.texto_log()):
        if time.time() >= fin_cartas:
            raise Fallo("la generación de las cartas de técnicas no terminó")
        time.sleep(0.1)
    dormir(0.5)
    j.captura("b_tecnicas")


def _heroe_moveset(j, par):
    # DS144: Heroe > Combate > Estilo (A) -> pestana Estilo > A: elige uno de los estilos desbloqueados.
    # Con DBZ3_TEST_HEROE_TODO=1 hay varios; la prueba navega al siguiente y comprueba el log de la eleccion
    # (rpg.cpp Guardar lo escribe en la ficha al confirmar).
    _heroe_a_creacion(j)
    j.captura("a_creacion")
    j.tecla("3", 600)                                   # RB: Apariencia -> Combate (cursor en Estilo)
    j.tecla_hasta_log("Space", r"heroe: pantalla de movesets abierta", 3, 5)   # A en Estilo abre la lista
    j.captura("b_estilos")
    j.tecla("D", 600)                                   # siguiente estilo (con DBZ3_TEST_HEROE_TODO hay varios)
    j.tecla("Space", 800)                               # A: elige el estilo resaltado
    j.log(r"heroe: estilo \d+ \(.+\) elegido en la lista", 20)
    j.captura("c_estilo_elegido")


def _du_mapa(j, par):
    # C2: Dragon Universe hasta el mapa (pantalla 8) y captura. Con --instantanea freezer_cap2 (el SMOKE la usa).
    _du_continuar(j, dict(par, iter="0"))
    dormir(1.5)
    j.captura("mapa")


ESCENARIOS["heroe_tecnicas"] = ("C2: Heroe > Movesets > Tecnicas: equipa 2 cartas y el combate las usa",
                                lambda p: {"DBZ3_TEST_HEROE_TEC": "1", "DBZ3_TEST_RINGOUT": "1"}, _heroe_tecnicas)
ESCENARIOS["heroe_moveset"] = ("DS144: Heroe > Moveset > pestana Estilo: lista los estilos y elige uno para el combate siguiente",
                               lambda p: {"DBZ3_TEST_HEROE_TODO": "1"}, _heroe_moveset)
ESCENARIOS["du_mapa"] = ("C2: DU > Continuar hasta el mapa (pantalla 8) y captura (--instantanea freezer_cap2)",
                         lambda p: {}, _du_mapa)


def ejecutar_dsl(j, pasos):
    for p in pasos:
        v, *a = shlex.split(p)
        if v == "tecla":
            j.tecla(a[0].replace(":", " "), int(a[1]) if len(a) > 1 else 600)
        elif v == "esperar":
            dormir(int(a[0]) / 1000)
        elif v == "esperar_pantalla":
            j.esperar_pantalla([int(x) for x in a[0].split(",")], int(a[1]) if len(a) > 1 else 60)
        elif v == "esperar_pelea":
            _esperar_pelea(j, int(a[0]) if a else 60)
        elif v == "captura":
            j.captura(a[0])
        elif v in ("dump", "write"):
            j.bloque(v)
        elif v == "log":
            j.log(a[0], int(a[1]) if len(a) > 1 else 20)


def cargar_escenario(esc, par):
    """Devuelve (descripcion, funcion, env). Valida antes de tocar el juego."""
    if esc.lower().endswith(".toml"):
        ruta = Path(esc)
        if not ruta.exists():
            raise SystemExit(f"no existe el escenario {ruta}")
        d = tomllib.loads(ruta.read_text(encoding="utf-8"))
        pasos = d.get("pasos", [])
        if not pasos:
            raise SystemExit(f"{ruta.name} no tiene 'pasos'")
        for p in pasos:
            if shlex.split(p)[0] not in VERBOS:
                raise SystemExit(f"paso desconocido en {ruta.name}: {p!r}")
        env = dict(_kv(x) for x in d.get("env", []))
        escena = d.get("escena")
        if escena:
            env.update(resolver_escena(escena)[0])
        ejecutar = (lambda j, _p: ejecutar_dsl(j, pasos))
        if escena:
            def ejecutar_con_escena(j, _p):
                _escena(j, {"escena": escena})
                ejecutar_dsl(j, pasos)
            ejecutar = ejecutar_con_escena
        return d.get("descripcion", ruta.name), ejecutar, env, None
    if esc not in ESCENARIOS:
        raise SystemExit(f"escenario desconocido '{esc}'. Opciones: {', '.join(ESCENARIOS)} o un .toml")
    item = ESCENARIOS[esc]
    desc, env_fn, fn = item[:3]
    if esc == "heroe_pieza":
        _heroe_pieza_valores(par)                        # valida antes de tocar la copia
    return desc, fn, env_fn(par), (item[3] if len(item) > 3 else None)


def correr(copia, escenario, env_extra=(), instantanea=None, par=None, forzar=False):
    with copia_en_uso(BASE / copia):
        return _correr(copia, escenario, env_extra, instantanea, par, forzar)


# X0h: familias que pasaron `pruebas.py igualdad` (firma del log igual a x1): corren por defecto a turbo xN y ocultas
# (plaza 3-8). Clave = escenario, o prefijo de la escena (batalla / satan / iw). Se amplian solo con igualdad medida.
# Medido 23/10 (igualdad x1 vs xN, firma del log): batalla, satan, iw:mini, b2:fase1 y menu a x4; campana, du_continuar y
# heroe_fichas (sus 7 comprobaciones) a x3; tag_swap a x3 (igual hasta el primer relevo, `igualdad-esc --estructura
# --sin opcion --hasta "relevo 0 -> 1"`: lo posterior depende de la IA del COM y no es reproducible ni a x1). NO pasan:
# campana/du_continuar/Heroe/tag_swap a x4 (la rueda del select no acepta la tecla), r2_nave (combate con IA; sin turbo).
FAMILIAS_RAPIDAS = {"batalla": 4, "satan": 4, "iw": 4, "b2:fase": 4, "menu": 4, "campana": 3, "tag_swap": 3, "du_continuar": 3,
                    "heroe_fichas": 3}


def _rapido(escenario, par):
    if os.environ.get("DBZ3_PRUEBAS_LENTO") == "1" or par.get("ventana"):
        return {}
    fam = Path(escenario).stem
    if fam == "escena":
        sp = (par.get("escena") or os.environ.get("DBZ3_TEST_ESCENA", "")).split("|")[0].split(":")
        fam = "b2:fase" if sp[0] == "b2" and len(sp) > 1 and sp[1].startswith("fase") else sp[0]
    t = FAMILIAS_RAPIDAS.get(fam)
    return {"DBZ3_TEST_TURBO": str(t), "DBZ3_TEST_OCULTO": "1"} if t else {}


def _correr(copia, escenario, env_extra=(), instantanea=None, par=None, forzar=False):
    par = par or {}
    nombre = f"{copia}/{Path(escenario).stem}"
    t0 = time.time()
    _TL.v = tuple(int(x) for x in par["ventana"].split("x")) if par.get("ventana") else None
    j = Juego(copia, nombre)
    desc, fn, env, prep = str(escenario), None, {}, None
    ok, msg, t_menu, t_cupo, t_cierre = True, "", None, 0.0, 0.0
    cache_key, cache, preparado, muerto = "", None, False, False
    try:
        desc, fn, env, prep = cargar_escenario(escenario, par)
        for k, v in _rapido(escenario, par).items():
            env.setdefault(k, v)
        env.update(_kv(x) for x in os.environ.get("DBZ3_PRUEBAS_ENV", "").split(";") if x)
        env.update(_kv(x) for x in env_extra)
        print(f"== {nombre}: {desc}", flush=True)
        _escenario_toml(j.run, escenario, desc, par, env, instantanea)
        preparar(BASE / copia, exe=Path(os.environ.get("DBZ3_PRUEBAS_EXE") or EXE))   # CAM: biseccion con un exe viejo
        preparado = True
        cache_key = _cache_key(BASE / copia, escenario, par, env, instantanea)
        turbo_solicitado = int(env.get("DBZ3_TEST_TURBO", "1"))
        if not forzar:
            cache = _leer_cache(cache_key)
        if cache:
            origen = Path(cache["run"])
            for p in (origen / "dbz3.log", origen / "pantallas.txt"):
                if p.is_file(): shutil.copy2(p, j.run / p.name)
            for p in origen.glob("*.png"):
                shutil.copy2(p, j.run / p.name)
            (j.run / "cache_hit.txt").write_text(f"PASS cache hash={cache_key}\n", encoding="utf-8")
            turbo_real = float(cache.get("turbo_real", 1))
            previo = (j.run / "dbz3.log").read_text(encoding="utf-8", errors="replace")
            (j.run / "dbz3.log").write_text(
                f"dbz3: prueba hash={cache_key} turbo_solicitado={turbo_solicitado} turbo_real={turbo_real:.2f} cache=PASS\n"
                + previo, encoding="utf-8")
            resultado = {"ok": True, "nombre": nombre, "dur": cache.get("dur", 0), "menu": cache.get("menu"),
                         "cierre": 0.0, "msg": "cache PASS", "run": j.run,
                         "t_escena": cache.get("t_escena"), "t_fin": cache.get("t_fin"), "cache": True}
            print(f"CACHE PASS {nombre} hash={cache_key} turbo={turbo_real:.2f}", flush=True)
            print(f"      log, PNG y escenario: {j.run}", flush=True)
            return resultado
        if prep:
            prep(j, par, env)                            # DS130: ficha de la pieza + modelo de combate antes de lanzar
        if instantanea:
            cargar_instantanea(copia, instantanea)
            if prep:                                     # la instantanea trae su user_data: se reaplica la pieza
                prep(j, par, env)
        else:
            respaldar(BASE / copia)
        j.lanzar(env)
        t_cupo = j.t_cupo
        t_menu = j.arrancar(float(par.get("arranque", "120")), rapido=par.get("titulo_rapido", "1") == "1")
        j.info(f"menu principal en {t_menu:.1f} s")
        fn(j, par)
        if not j.vivo():
            raise Fallo("el juego se cerro durante la prueba")
        if not any(j.run.glob("*.png")):
            try:
                j.captura("zz_final")
            except Exception as e:
                j.info(f"sin captura final automatica: {e}")
    except (Exception, SystemExit) as e:
        ok, msg = False, str(e) or type(e).__name__
        j.captura_fallo()
    finally:
        muerto = j.proc is not None and j.proc.poll() is not None
        t_fin = time.time()
        j.cerrar()
        t_cierre = time.time() - t_fin
        if preparado and _ventana():
            _TL.v = None
            escribir_toml(BASE / copia / "dbz3_user.toml")
    texto = j.texto_log()
    malos = [l for l in texto.splitlines() if re.search(ERRORES, l)]
    if muerto:
        rc = j.proc.returncode & 0xFFFFFFFF
        ok, msg = False, f"el juego murio (codigo 0x{rc:08X}{', access violation' if rc == 0xC0000005 else ''}). {msg}"
    if malos and ok:
        ok, msg = False, f"log con errores ({len(malos)}): {malos[0][:150]}"
    if not (j.run / "escenario.toml").is_file():
        _escenario_toml(j.run, escenario, desc, par, env, instantanea)
    turbo_solicitado = int(env.get("DBZ3_TEST_TURBO", "1")) if env else 1
    turbo_real = j.factor() if j.proc is not None else 1.0
    meta = (f"dbz3: prueba hash={cache_key or 'sin-hash'} turbo_solicitado={turbo_solicitado} "
            f"turbo_real={turbo_real:.2f} cache={'forzada' if forzar else 'MISS'}\n")
    (j.run / "dbz3.log").write_text(meta + texto, encoding="utf-8")
    (j.run / "pantallas.txt").write_text("".join(f"{t:6.1f} s  {sc}\n" for t, sc in j.traza), encoding="utf-8")
    evidencia_real = _evidencia_png(j, ok, msg)
    if not ok:
        extracto = informe_fallo(texto, malos)
        (j.run / "fallo.txt").write_text(f"{msg}\n\n{extracto}\n", encoding="utf-8")
        print("\n".join("      | " + l for l in extracto.splitlines()[-12:]), flush=True)
    dur = time.time() - t0 - t_cierre - t_cupo
    resultado = {"ok": ok, "nombre": nombre, "dur": dur, "menu": t_menu, "cierre": t_cierre, "msg": msg,
                 "run": j.run, "t_escena": getattr(j, "t_escena", None), "t_fin": getattr(j, "t_fin_escena", None),
                 "cache": False}
    if cache_key and ok and evidencia_real:
        _guardar_cache(cache_key, resultado, turbo_solicitado, turbo_real)
    print(f"{'PASS' if ok else 'FAIL'}  {nombre}  {dur:.1f} s (menu {t_menu or 0:.1f} s, cierre {t_cierre:.1f} s"
          f"{f', cola {t_cupo:.0f} s' if t_cupo > 1 else ''})  {msg}".rstrip(), flush=True)
    print(f"      evidencia y resultados: {j.run}", flush=True)
    return resultado


def informe_fallo(texto, malos):
    """Extracto del log para el fallo: lineas de error y las ultimas 25 (sin las de rendimiento)."""
    lineas = [l for l in texto.splitlines() if "perf fps=" not in l and "audio pico=" not in l]
    partes = []
    if malos:
        partes += ["-- errores --"] + [l[:220] for l in malos[:8]]
    partes += ["-- final del log --"] + [l[:220] for l in lineas[-25:]]
    return "\n".join(partes) if texto else "(sin log del juego)"


def _es_n1(env_extra):
    """X0g: con DBZ3_TEST_OCULTO=1 (ventana oculta, sin audio, x1) caben 6 juegos a la vez; con ventana, 3."""
    return any(_kv(x) == ("DBZ3_TEST_OCULTO", "1") for x in env_extra)


_RB_GUION = ["Space", "D", "Space", "A", "S", "Space", "W", "E", "Space", "D"]   # el mismo guion en las dos copias


def _online_rb(j, par):
    # R: una de las dos copias de un combate online (DBZ3_NET_AUTO=host|guest, src/net/netplay_hooks.cpp RbAuto): espera
    # a que el lockstep arranque, juega el guion de teclas (el MISMO en las dos) y espera el fin del combate.
    j.log(r"dbz3 rb: auto: empezando", int(par.get("espera", "150")))
    j.log(r"dbz3 rb: auto: combate pedido \(ok\)", 30)         # sin teclas antes: sacarian al menu de su pantalla
    j.log(r"tag: empieza un combate", 60)
    fin = time.time() + float(par.get("seg", "40"))
    i = 0
    while time.time() < fin and j.vivo():
        j.tecla(_RB_GUION[i % len(_RB_GUION)], 250)
        i += 1
    j.log(r"tag: combate acabado|hub: fin del combate nativo", int(par.get("fin", "120")))
    t = j.texto_log()
    ult = re.findall(r"auto tick (\d+): sumas comparadas (\d+) distintas (\d+) \(RNG: (\w+)", t)
    j.info(f"sumas por tick (ultima medida): {ult[-1] if ult else 'sin medida (combate < 600 ticks)'}")
    acab = [l for l in t.splitlines() if "tag: combate acabado" in l]
    j.info("resultado: " + (re.sub(r"^(\[[^\]]*\] ){3}", "", acab[-1])[:140] if acab else "?"))
    if ult and (int(ult[-1][2]) or ult[-1][3] != "ok"):
        raise Fallo(f"sumas distintas {ult[-1][2]} / RNG {ult[-1][3]}")


def _env_online_rb(p):
    e = {"DBZ3_NET_AUTO": p["rol"], "DBZ3_NET_LOCAL": "1", "DBZ3_NET_SUMAS": "1", "DBZ3_NET_NOFAIL": "1"}
    if p.get("puerto"):   # [H] RbAuto aun no lee estas dos variables (src/net es de la linea R): hoy el puerto es 7777
        e["DBZ3_NET_PORT"] = p["puerto"]
    if p.get("ringout") == "1":
        e["DBZ3_TEST_RINGOUT"] = "1"
    return e


ESCENARIOS["online_rb"] = ("R: una copia del combate online de dos copias (--par rol=host|guest seg=40); se lanza con `online-rb`",
                            _env_online_rb, _online_rb)


def _recuperar_plaza_huerfana(plaza, cuarentena=None):
    """Aparta una plaza ocupada solo si su PID registrado ya no existe."""
    plaza = Path(plaza)
    quien = plaza / "quien.txt"
    try:
        texto = quien.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False
    m = re.search(r"\bpid\s+(\d+)\b", texto, re.I)
    marca = re.search(r"\b\d{8}-\d{6}\b", texto)
    if not m or _vivo(int(m.group(1)), marca.group() if marca else ""):
        return False
    cuarentena = Path(cuarentena) if cuarentena is not None else PLAZAS_HUERFANAS
    try:
        cuarentena.mkdir(parents=True, exist_ok=True)
        base = cuarentena / f"plazas_huerfanas_{datetime.now():%Y%m%d-%H%M%S}"
        destino = base
        n = 2
        while destino.exists():
            destino = base.with_name(f"{base.name}_{n}")
            n += 1
        shutil.move(str(plaza), str(destino))   # R6: rename falla entre volumenes (C: -> D:) y las plazas huerfanas nunca se liberaban
        return True
    except OSError:
        return False


def online_rb(copias=("rb_a", "rb_b"), seg="40", delay_ms=0, loss_every=0, instantanea="freezer_cap2", ringout="0"):
    """Dos juegos online (anfitrion y invitado) en copias distintas, mismo guion de teclas, sumas por tick del log.
    Con delay_ms o loss_every > 0 pasa el trafico por tools/fault_proxy.py: necesita que el juego acepte puertos
    distintos (DBZ3_NET_PORT / DBZ3_NET_JOIN_PORT, pendiente en netplay_hooks.cpp [H])."""
    proxy = None
    puertos = {}
    if delay_ms or loss_every:
        sys.path.insert(0, str(R / "tools"))
        from fault_proxy import FaultProxy
        puertos = {"host": "7778", "guest": "7777"}   # el invitado se une al frontal (7777), el anfitrion escucha en 7778
        proxy = FaultProxy(7777, 7778, delay_ms, loss_every)
        proxy.start()
    try:
        pares = list(zip(("host", "guest"), copias))
        with ThreadPoolExecutor(2) as ex:
            fut = [ex.submit(correr, c, "online_rb", [], instantanea,
                             dict(rol=rol, seg=seg, ringout=ringout, **({"puerto": puertos[rol]} if puertos else {})))
                   for rol, c in pares]
            res = [f.result() for f in fut]
    finally:
        if proxy:
            proxy.close()
    print(tabla(res), flush=True)
    return all(r["ok"] for r in res)


def paralelo(pares, env_extra=()):
    trabajos = []
    for p in pares:
        esc, _, cop = p.partition("@")
        if not esc or not cop:
            raise SystemExit(f"formato esc@copia, recibido: {p}")
        trabajos.append((cop, esc))
    tope = MAX_PARALELO_N1 if _es_n1(env_extra) else MAX_PARALELO
    if len(trabajos) > tope:
        raise SystemExit(f"como maximo {tope} a la vez")
    if len({c for c, _ in trabajos}) < len(trabajos):
        raise SystemExit("cada copia solo puede usarse una vez")
    with ThreadPoolExecutor(len(trabajos)) as ex:
        res = [f.result() for f in [ex.submit(correr, c, e, env_extra) for c, e in trabajos]]
    print(tabla(res), flush=True)
    return all(r["ok"] for r in res)


# Bateria rapida: (escenario, instantanea, parametros). Las largas primero: asi el total es menor.
SMOKE = [
    ("tag_swap", "freezer_cap2", {"rafaga": "4"}),
    ("campana", "freezer_cap2", {}),
    ("du_continuar", "freezer_cap2", {"iter": "3"}),
    ("batalla", "freezer_cap2", {}),
    ("menu", "freezer_cap2", {}),
]
COPIAS_SMOKE = ["smoke_a", "smoke_b", "smoke_c"]          # la bateria "todo" sigue con 3
COPIAS_N1 = COPIAS_SMOKE + ["smoke_d", "smoke_e", "smoke_f"]  # X0g: se crean copiando smoke_a (robocopy /XJ)
if os.environ.get("DBZ3_PRUEBAS_COPIAS"):   # X0h: copias propias de una linea (6 nombres separados por comas), sin pisar smoke_*
    COPIAS_N1 = os.environ["DBZ3_PRUEBAS_COPIAS"].split(",")
    COPIAS_SMOKE = COPIAS_N1[:3]


def tabla(res, total=None):
    out = ["", "== RESUMEN ==", f"{'':4}  {'prueba':<24} {'total':>7} {'menu':>6} {'cierre':>6}  detalle"]
    for r in res:
        menu = f"{r['menu']:5.1f}s" if r.get("menu") else "     -"
        cierre = f"{r['cierre']:5.1f}s" if r.get("cierre") is not None else "     -"
        out.append(f"{'PASS' if r['ok'] else 'FAIL'}  {r['nombre']:<24} {r['dur']:6.1f}s {menu} {cierre}  "
                   f"{r['msg'][:110]}".rstrip())
    if total is not None:
        out.append(f"{sum(r['ok'] for r in res)}/{len(res)} PASS en {total:.0f} s de reloj "
                   f"(suma de pruebas {sum(r['dur'] for r in res):.0f} s)")
    return "\n".join(out)


def comprobar_n0():
    """N0: las copias de la bateria 'todo' no llevan window_* (un tamano forzado rompe las capturas)."""
    malas = [c for c in COPIAS_SMOKE if (BASE / c / "dbz3_user.toml").exists()
             and re.search(r"^\s*window_(width|height)\s*=", (BASE / c / "dbz3_user.toml").read_text(encoding="utf-8", errors="replace"), re.M)]
    if malas:
        raise SystemExit(f"N0: {malas} llevan window_width/height en dbz3_user.toml; quitalas (se limpian solas al correr cualquier escenario en esa copia)")


def todo(solo=None, maxp=MAX_PARALELO, env_extra=(), forzar=False):
    comprobar_n0()
    lista = [s for s in SMOKE if not solo or s[0] in solo]
    if not lista:
        raise SystemExit(f"nada que correr; escenarios de la bateria: {', '.join(s[0] for s in SMOKE)}")
    faltan = sorted({i for _, i, _ in lista if i and not (INST / i / "user_data").exists()})
    if faltan:
        raise SystemExit(f"faltan instantaneas {faltan}: python tools\\pruebas.py biblioteca")
    libres = queue.Queue()
    n1 = _es_n1(env_extra)
    for c in (COPIAS_N1 if n1 else COPIAS_SMOKE)[:max(1, min(maxp, MAX_PARALELO_N1 if n1 else MAX_PARALELO))]:
        libres.put(c)

    def uno(s):
        c = libres.get()
        try:
            return correr(c, s[0], env_extra, s[1], dict(s[2]), forzar)
        except (Exception, SystemExit) as e:          # copia ocupada, exe sin compilar...
            return {"ok": False, "nombre": f"{c}/{s[0]}", "dur": 0.0, "msg": str(e), "run": None}
        finally:
            libres.put(c)

    t0 = time.time()
    with ThreadPoolExecutor(libres.qsize()) as ex:
        res = list(ex.map(uno, lista))
    txt = tabla(res, time.time() - t0)
    print(txt, flush=True)
    PRUEBAS.mkdir(parents=True, exist_ok=True)
    (PRUEBAS / f"{ts()}_todo.txt").write_text(txt + "\n", encoding="utf-8")
    return all(r["ok"] for r in res)


def biblioteca(copia="smoke_a", seg=120):
    """Instantaneas de partida para las pruebas: perfil_nuevo (la que crea el juego en el primer arranque).
    freezer_cap2 se hizo a mano (ver docs/14_frentes/pruebas.md); todo_desbloqueado no se puede crear aun."""
    dest = BASE / copia
    preparar(dest)
    ud = dest / "user_data"
    if ud.exists():
        mover(ud, RESPALDO / f"{ts()}_{copia}_antes_de_perfil_nuevo")
    j = Juego(copia, "biblioteca_perfil_nuevo")
    try:
        j.lanzar({})
        j.info(f"menu principal en {j.arrancar(seg, titulo=('Return', 'W', 'Space')):.1f} s (perfil nuevo)")
        j.captura("menu_perfil_nuevo")
        time.sleep(3)                                   # que termine de escribir la partida nueva
    except Exception:
        j.captura_fallo()
        raise
    finally:
        j.cerrar()
        (j.run / "pantallas.txt").write_text("".join(f"{t:6.1f} s  {sc}\n" for t, sc in j.traza), encoding="utf-8")
    if not ud.exists():
        ud.mkdir()                                      # el juego no guardo nada: perfil vacio igualmente valido
    guardar_instantanea(copia, "perfil_nuevo")
    for n in ("perfil_nuevo", "freezer_cap2", "todo_desbloqueado"):
        print(f"  {n:<18} {'OK' if (INST / n / 'user_data').exists() else 'falta'}", flush=True)


def main(argv=None):
    p = argparse.ArgumentParser(description="Pruebas del juego en copias aisladas (ver cabecera del archivo).")
    sub = p.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("prep", help="crear o refrescar una copia")
    a.add_argument("copia")
    a.add_argument("--desde", help="copia origen si la copia no existe (por defecto du_test)")
    a.add_argument("--exe", default=str(EXE))
    a = sub.add_parser("instantanea", help="guardar o cargar los datos de partida de una copia")
    a.add_argument("accion", choices=["guardar", "cargar"])
    a.add_argument("copia")
    a.add_argument("nombre")
    a = sub.add_parser("correr", help="lanzar el juego y ejecutar un escenario")
    a.add_argument("copia")
    a.add_argument("escenario")
    a.add_argument("--env", action="append", default=[], metavar="K=V")
    a.add_argument("--par", action="append", default=[], metavar="K=V", help="parametros del escenario")
    a.add_argument("--instantanea", help="cargar esta instantanea antes de correr")
    a.add_argument("--forzar", action="store_true", help="no reutilizar resultados PASS de la cache")
    a = sub.add_parser("paralelo", help="hasta 3 pruebas a la vez, cada una en su copia")
    a.add_argument("pares", nargs="+", help="escenario@copia")
    a.add_argument("--env", action="append", default=[], metavar="K=V")
    a = sub.add_parser("todo", help="bateria rapida (smoke) en smoke_a/b/c, tabla PASS/FAIL")
    a.add_argument("--solo", help="lista separada por comas (" + ",".join(s[0] for s in SMOKE) + ")")
    a.add_argument("--max", type=int, default=MAX_PARALELO, help="juegos a la vez (1-3; hasta 6 con --env DBZ3_TEST_OCULTO=1)")
    a.add_argument("--env", action="append", default=[], metavar="K=V")
    a.add_argument("--forzar", action="store_true", help="no reutilizar resultados PASS de la cache")
    a = sub.add_parser("escena", help="X0e: salta a una o varias escenas (a|b|c), una partida por escena")
    a.add_argument("spec", nargs="?", default=os.environ.get("DBZ3_TEST_ESCENA", ""))
    a.add_argument("--copias", default=",".join(COPIAS_N1))
    a.add_argument("--env", action="append", default=[], metavar="K=V")
    a = sub.add_parser("igualdad", help="X0g: las mismas escenas a x1/x2/x4: la firma del log debe ser identica")
    a.add_argument("--escenas", default="batalla:0,7,2|iw:mini:0x7D9|satan:ronda1")
    a.add_argument("--turbos", default="1,2,4")
    a = sub.add_parser("igualdad-esc", help="X0h: igualdad del turbo para escenarios por nombre o .toml ('esc[@instantanea]|...')")
    a.add_argument("items")
    a.add_argument("--turbos", default="1,4")
    a.add_argument("--firma", default=SIG_GENERICA)
    a.add_argument("--hasta", help="regex: la firma se corta en la primera linea que la cumpla")
    a.add_argument("--sin", help="regex: lineas que se quitan de la firma")
    a.add_argument("--estructura", action="store_true", help="compara solo la estructura (sin posiciones, direcciones ni ticks)")
    a = sub.add_parser("fps", help="X0g: FPS en combate con 1..6 juegos a la vez (ocultos, sin audio, x1)")
    a.add_argument("--n", default="1,3,4,5,6")
    a.add_argument("--turbo", type=int, default=1)
    a.add_argument("--escena", default="batalla:0,7,2")
    a = sub.add_parser("ajustes", help="X1: coste en FPS de una opcion beta (relight1/2, fsr3) frente a base, en el combate")
    a.add_argument("nombres", nargs="*", default=list(AJUSTES))
    a.add_argument("--copia", default="x1_hub")
    a = sub.add_parser("online-rb", help="R: dos juegos online (host+guest), mismo guion de teclas, sumas por tick")
    a.add_argument("--copias", default="rb_a,rb_b")
    a.add_argument("--seg", default="40")
    a.add_argument("--delay-ms", type=int, default=0)
    a.add_argument("--loss-every", type=int, default=0)
    a.add_argument("--ringout", default="0")
    sub.add_parser("biblioteca", help="crea las instantaneas de prueba (perfil_nuevo)")
    a = p.parse_args(argv)

    if a.cmd == "todo":
        return 0 if todo(a.solo.split(",") if a.solo else None, a.max, a.env, a.forzar) else 1
    if a.cmd == "escena":
        if not a.spec:
            raise SystemExit("falta la escena (argumento o DBZ3_TEST_ESCENA)")
        return 0 if escenas(a.spec, a.copias.split(","), a.env) else 1
    if a.cmd == "igualdad":
        return 0 if igualdad(a.escenas.split("|"), [int(x) for x in a.turbos.split(",")], COPIAS_N1) else 1
    if a.cmd == "igualdad-esc":
        return 0 if igualdad_esc(a.items.split("|"), [int(x) for x in a.turbos.split(",")], COPIAS_N1, a.firma, a.estructura, a.hasta, a.sin) else 1
    if a.cmd == "fps":
        medir_fps([int(x) for x in a.n.split(",")], a.turbo, a.escena, COPIAS_N1)
        return 0
    if a.cmd == "ajustes":
        return 0 if all(v["ok"] for v in medir_ajustes(a.nombres, a.copia).values()) else 1
    if a.cmd == "online-rb":
        return 0 if online_rb(a.copias.split(","), a.seg, a.delay_ms, a.loss_every, ringout=a.ringout) else 1
    if a.cmd == "biblioteca":
        biblioteca()
        return 0

    if a.cmd == "prep":
        preparar(BASE / a.copia, a.desde, Path(a.exe))
        print(f"copia lista: {BASE / a.copia}", flush=True)
        return 0
    if a.cmd == "instantanea":
        (guardar_instantanea if a.accion == "guardar" else cargar_instantanea)(a.copia, a.nombre)
        return 0
    if a.cmd == "correr":
        r = correr(a.copia, a.escenario, a.env, a.instantanea, dict(_kv(x) for x in a.par), a.forzar)
        return 0 if r["ok"] else 1
    return 0 if paralelo(a.pares, a.env) else 1


if __name__ == "__main__":
    sys.exit(main())
