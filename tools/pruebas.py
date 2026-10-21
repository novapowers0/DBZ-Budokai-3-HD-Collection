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
import os
import queue
import re
import shlex
import shutil
import subprocess
import sys
import time
import tomllib
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
MAX_PARALELO = 3
LANZANDO = BASE / "_lanzando"                       # candado entre procesos: contar juegos + lanzar es atomico
ERRORES = (r"access violation|UNHANDLED EXCEPTION|physical write fault"   # main.cpp:976, xmemory.cpp:547/628
           r"|std::terminate called")                                        # main.cpp (set_terminate)
PAPELERA = Path(r"D:\_PARA_BORRAR_CLAUDE") / "pruebas_respaldos"   # respaldos automaticos viejos (no se borran)
MAX_RESPALDOS = 5                                   # por copia: los 5 mas recientes se quedan en _respaldo
VERBOS = {"tecla", "esperar", "esperar_pantalla", "captura", "dump", "write", "log"}

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


class Fallo(Exception):
    pass


def ts():
    return datetime.now().strftime("%Y%m%d-%H%M%S")


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


def _vivo(pid):
    h = K32.OpenProcess(0x1000, False, pid)                     # QUERY_LIMITED_INFORMATION
    if not h:
        return False
    code = wt.DWORD()
    K32.GetExitCodeProcess(h, ctypes.byref(code))
    K32.CloseHandle(h)
    return code.value == 259                                    # STILL_ACTIVE


@contextmanager
def copia_en_uso(dest):
    """Una prueba por copia a la vez, de principio a fin. comprobar_libre mira antes de la cola: dos agentes con la
    misma copia esperaban turno y abrian los dos (teclas y ficheros pisados, tag_swap en FAIL, 2026-10-15)."""
    lock, t0 = BASE / f"_en_uso_{dest.name}", time.time()
    while True:
        try:
            lock.mkdir()
            (lock / "pid").write_text(str(os.getpid()))
            break
        except FileExistsError:
            try:
                pid = int((lock / "pid").read_text())
            except (OSError, ValueError):
                pid = 0
            viejo = not _vivo(pid) if pid else time.time() - lock.stat().st_mtime > 60
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


def escribir_toml(ruta):
    ov = dict(OVERRIDES, dbz3_game_dir=f"'{R}'")
    lineas = ruta.read_text(encoding="utf-8", errors="surrogateescape").splitlines() if ruta.exists() else []
    out, vistos = [], set()
    for l in lineas:
        m = re.match(r"\s*([A-Za-z0-9_\-]+)\s*=", l)
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
        r = subprocess.run(["robocopy", str(src), str(dest), "/E", "/R:1", "/W:1", "/XD", str(src / "logs"),
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
    for n in ("dbz3_input.req", "dbz3_shot.req", "dbz3_shot.bmp"):   # senales de control viejas
        (dest / n).unlink(missing_ok=True)
    escribir_toml(dest / "dbz3_user.toml")
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

    def info(self, msg):
        print(f"  [{time.time() - self.t0:6.1f} s] {self.nombre}: {msg}", flush=True)

    def lanzar(self, env_extra):
        # como mucho 3 juegos abiertos a la vez en el PC (varios agentes probando): espera su turno.
        # El candado LANZANDO hace atomico "contar + lanzar" entre agentes (antes dos podian ver 2 y abrir 4).
        env = {k: v for k, v in os.environ.items() if not k.startswith("DBZ3_")}
        env.update(SDL_WINDOW_ACTIVATE_WHEN_SHOWN="0", SDL_WINDOW_ACTIVATE_WHEN_RAISED="0", DBZ3_NO_DIALOGS="1")
        env.update(env_extra)
        t_espera = time.time()
        while True:
            if time.time() - t_espera > 1800:
                raise Fallo("30 min esperando: ya hay 3 juegos abiertos")
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
                n = (subprocess.run(["tasklist", "/FI", "IMAGENAME eq dbz3.exe", "/NH"], capture_output=True,
                                    text=True, errors="replace").stdout or "").count("dbz3.exe")
                if n < MAX_PARALELO:
                    self.logs_antes = self._logs()      # el log de esta partida sera uno nuevo o uno que cambie
                    self.t0 = time.time()
                    # sin heredar la consola: si no, la llamada tarda hasta que el juego suelta esa salida
                    self.proc = subprocess.Popen([str(self.dest / "dbz3.exe"), f"--game_directory={R}"],
                                                 cwd=str(self.dest), env=env, stdin=subprocess.DEVNULL,
                                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    break
            finally:
                LANZANDO.rmdir()                                      # candado propio (carpeta vacia)
            time.sleep(5)
        self.t_cupo = self.t0 - t_espera
        self.h = K32.OpenProcess(0x0410, False, self.proc.pid)     # QUERY_INFORMATION | VM_READ
        if not self.h:
            raise Fallo("no pude leer la memoria del juego")
        self.base_real()

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
        req = self.dest / "dbz3_input.req"
        fin = time.time() + 2.5
        while req.exists() and time.time() < fin:       # esperar a que el juego consuma la anterior
            time.sleep(0.05)
        tmp = self.dest / "dbz3_input.req.tmp"
        tmp.write_text(k + "\n", encoding="ascii")
        os.replace(tmp, req)
        self.traza.append((round(time.time() - self.t0, 1), f"tecla {k}"))
        time.sleep(espera / 1000)

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
                    time.sleep(1.5)
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
    time.sleep(2.5)
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
        time.sleep(5)
    j.captura("b_tienda")
    j.tecla("Space", 1500)                              # compra la cura (o avisa de que no se puede)
    j.captura("c_tienda_compra")


def _hub_lab(j, par):
    # HUB con el fondo propio y sin tiras doradas; Laboratorio (entrada 7) con su fondo y el selector de luchador y
    # rival (izquierda/derecha en las filas 0 y 1). B vuelve al HUB.
    time.sleep(2.5)
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
    time.sleep(2.5)
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
    time.sleep(2.5)
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
    time.sleep(6)
    j.captura("f_mision1")
    time.sleep(5)
    j.captura("g_mision1_pelea")
    for l in [l for l in j.texto_log().splitlines() if "dbz3 rpg" in l or "capsulas" in l or "reglas" in l][-6:]:
        j.info(re.sub(r"^(\[[^\]]*\] ){3}", "", l)[:170])


def _r5_heroe(j, par):
    # frente R5 (ronda 4): Modo Heroe con un cuerpo y el moveset (estilo) de otro luchador. La receta se escribe
    # antes en user_data/dbz3/rpg.toml (raza/cuerpo/estilo); aqui: HUB > Kumite > Modo Heroe > creacion > Empezar.
    # --par estilo=N: pulsaciones a la derecha en la fila Estilo (por defecto 0, la del rpg.toml).
    time.sleep(2.5)
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
    time.sleep(float(par.get("espera", "16")))         # pasa la presentacion del escenario
    for k in range(int(par.get("rafaga", "0"))):        # --par rafaga=N: N capturas cada 2 s (intros, READY)
        j.captura(f"r{k:02d}")
        time.sleep(2)
    j.captura("d_pelea_1")
    time.sleep(5)
    j.captura("e_pelea_2")
    j.tecla("J", 300)                                   # un golpe (moveset del estilo)
    time.sleep(1)
    j.captura("f_pelea_3")
    if par.get("ringout") == "1":                       # gana por salida del ring: pantalla entre combates y sale
        j.log(r"combate 1 ganado", 60)
        time.sleep(6)
        j.captura("g_entre")
        if par.get("completa") == "1":                  # los 3 combates: fin de saga, nivel y saga siguiente
            for k in (2, 3):
                j.tecla_hasta_log("Space", rf"combate {k}: pedido", 3, 4)
                time.sleep(20)
                j.captura(f"h_pelea_{k}")
                if k == 2:
                    j.log(r"combate 2 ganado", 60)
                    time.sleep(6)
            j.log(r"fin de saga", 60)
            time.sleep(4)
            j.captura("i_fin")
        else:
            j.tecla("Backspace", 3000)                  # B: guardar y salir (el estilo vuelve a su modelo)
            j.log(r"vuelve a su modelo", 20)
            j.captura("h_fin")
    for l in [l for l in j.texto_log().splitlines() if "dbz3 rpg" in l or "dbz3 heroe" in l][-8:]:
        j.info(re.sub(r"^(\[[^\]]*\] ){3}", "", l)[:170])


def _du_continuar(j, par):
    # verificado 2026-10-15: DU (titulo, pantalla 6) -> Return -> menu (New Story) -> S = Continue -> rueda (7)
    # -> W x up (1 = Freezer desde Goku) -> Space -> mapa/escenas (8)
    j.tecla_hasta("Space", [6], 5, 4)                   # menu principal -> DU (la primera a veces no entra)
    time.sleep(3)                                       # "Pulsa START" no responde nada mas aparecer
    j.tecla("Return", 3000)                             # "Press START" -> menu de DU
    j.tecla("S", 1500)                                  # Continue
    j.tecla("Space", 3000)
    j.esperar_pantalla([7], int(par.get("seg", 40)))
    time.sleep(2.5)                                     # la rueda aun gira al aparecer: la W se perdia
    for _ in range(int(par.get("up", 1))):
        j.tecla("W", 2000)
    j.captura("00_rueda")
    j.tecla("Space", 4000)
    j.esperar_pantalla([8, 9], int(par.get("seg", 40)))  # mapa, o combate directo (freezer_cap2 hoy: escena 5522)
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
        time.sleep(0.25)
    time.sleep(1.0)                                     # su primera "opcion N" llega justo despues
    while time.time() < fin:
        t = j.texto_log()
        if t.count("menu de capsulas del jugador 1: elegido") > ya:
            return
        ops = re.findall(r"menu de capsulas del jugador 1: opcion (\d)", t)
        cur = int(ops[-1]) if ops else 1
        j.tecla("W" if cur > 1 else "S" if cur < 1 else "Space", 2500)
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
        time.sleep(0.25)
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
    time.sleep(1)
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
        time.sleep(0.12)
    time.sleep(3)
    j.captura("11_despues")


def _a_duelo(j):
    """Menu principal (cursor en Dragon Universe) -> Duelo (pantalla 14). La primera tecla nada mas llegar al menu
    se pierde (medido 2026-10-15: la S se perdio y el Space de despues entro en Dragon Universe)."""
    time.sleep(2.5)
    j.tecla("S", 1200)
    j.tecla("Space", 0)
    j.esperar_pantalla([14, 6], 8)
    if j.pantalla() != 14:
        raise Fallo(f"la S no movio el cursor del menu principal: entro en la pantalla {j.pantalla()}")
    time.sleep(1.5)


def _online_crear(j, par):
    # Bug 2026-10-15: tras elegir un equipo en el Tag Team de 1P, CREAR PARTIDA fallaba hasta reiniciar
    # ("F4: Tag Team necesita prueba..."). DBZ3_NET_TEAM hace de ese equipo; DBZ3_NET_LOCAL: sin UPnP ni STUN
    # (la prueba cierra el juego a la fuerza y no debe dejar puertos abiertos en el router).
    _a_duelo(j)
    for _ in range(4):                                  # 1P VS COM ... COM VS COM -> fila ONLINE
        j.tecla("S", 700)
    j.captura("01_fila_online")
    j.tecla("Space", 1500)                              # abre la pantalla ONLINE
    j.captura("02_online")
    j.tecla("Space", 2000)                              # CREAR PARTIDA (primera fila)
    j.log(r"dbz3 online: (partida creada|no se pudo crear)", 10)
    j.captura("03_crear")
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
    time.sleep(2.5)                                     # la primera tecla en el menu principal se pierde
    j.tecla("S", 1200)
    j.tecla("Space", 0)
    j.esperar_pantalla([14], 8)
    _rafaga(j, "a_entrada", float(par.get("seg", 3.5)))
    time.sleep(1)
    j.tecla("P", 0)                                     # Y: atras
    _rafaga(j, "b_salida_y", 2.5)
    j.esperar_pantalla([5], 10)
    time.sleep(2.5)
    j.tecla("Space", 0)                                 # el menu principal recuerda la fila del Duelo
    j.esperar_pantalla([14], 8)
    time.sleep(3.5)
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
    time.sleep(2.5)
    j.tecla("W", 1200)                                  # desde la fila 0, arriba = fila HUB (la ultima)
    j.tecla("Space", 3500)                              # pantalla HUB (cursor en Tag Team)
    j.captura("a_hub")
    j.tecla("S", 1200)                                  # Online
    j.tecla("Space", 0)
    j.esperar_pantalla([14], 10)
    _rafaga(j, "b_llegada", float(par.get("seg", 4.5)))
    j.log(r"se abre la pantalla ONLINE", 5)
    time.sleep(1)
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
    time.sleep(2)
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


def _satan(j, par):
    # Leyenda de Mr. Satan (src/pasarela.cpp, DBZ3_TEST_PASARELA=auto,seg=8): empieza sola desde el menu principal y
    # avanza cada 8 s; RINGOUT gana cada combate a los 5 s. desde=N empieza en el paso N (25 = Cell, el ultimo ->
    # final). Orden de B1 (F6): titulo, dificultad, prologo, Yamcha sin tarjeta, resultado con la cuenta, tablero de
    # 11 caras y la escena 3D del retador. El registro llega con unos segundos de retraso.
    d = int(par.get("desde", "0"))
    j.log(r"pasarela: prueba: disponible true; empieza", 90)
    j.log(rf"pasarela: paso {d + 1}/\d+", 20)
    time.sleep(3)
    j.captura("a_entrada")
    if d == 0:
        j.log(r"pasarela: titulo: START", 30)
        time.sleep(3)
        j.captura("a2_dificultad")
        j.log(r"pasarela: prologo 2/7", 40)
        time.sleep(1)
        j.captura("b_prologo")
    else:
        j.log(r"hub: combate nativo", 40)
        j.log(r"pasarela: final de la leyenda", 90)
        time.sleep(2)
        j.captura("e_final_cuenta")
        time.sleep(6)
        j.captura("f_final_total")
        return
    j.log(r"hub: combate nativo", 60)
    if par.get("golpes") == "1":                    # P3: Mr. Satan pega (puntos HIT/COMBO de B1 y su aviso)
        j.log(r"pasarela: paso \d+ estado del combate 4", 60)
        j.tecla("Right 700", 100)
        for k in ["L", "L", "L", "P", "L", "L", "P", "L"]:
            j.tecla(k, 220)
        j.captura("c_puntos")
    if par.get("cell") == "1":                      # s9b: escena de los Juegos de Cell (7 pasos) antes del combate 1
        for k in range(1, 8):
            j.log(rf"cinematica: paso {k}/", 90 if k == 1 else 40)
            time.sleep(1.6)
            j.captura(f"c_cell{k}")
    time.sleep(4)
    j.captura("c_combate")
    j.log(r"\): resultado", 150)
    time.sleep(1)
    j.captura("d_resultado")
    j.log(r"pasarela: paso 3/\d+ \(charla", 40)
    time.sleep(3)
    j.captura("e_tablero")
    j.log(r"cinematica: paso 1/", 60)                # la escena del retador va tras su tablero (RINGOUT la corta)
    time.sleep(1.5)
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
            time.sleep(2)
            j.captura(f"p{k + 1}_prologo")
        return
    if d == 1:
        j.log(r"hub: combate nativo", 60)
        j.log(r"pasarela: paso \d+ estado del combate 4", 60)
        time.sleep(6)
        j.captura("a_yamcha_sin_escena")
        return
    if d == 2:
        j.log(r"pasarela: paso 3/\d+ \(charla", 40)
        time.sleep(3)
        j.captura("b_tablero")
    j.log(r"hub: combate nativo", 60)
    if par.get("ki") == "1":                        # sonda de la carga de ki: 11 combinaciones de 2,5 s
        for k in range(1, 12):
            j.log(rf"cinematica: paso {k}/", 120 if k == 1 else 30)
            time.sleep(1.8)
            j.captura(f"k{k:02d}")
        return
    if par.get("ringout") == "1":                   # victoria nativa tras la escena cortada (sonda de codigos)
        j.log(r"cinematica: (terminada|cortada)", 120)
        for i in range(4):
            time.sleep(2)
            j.captura(f"v{i}_victoria")
        return
    espera = {1: 1.5, 2: 3.0, 3: 1.5, 4: 1.5}
    for k in range(1, 5):
        j.log(rf"cinematica: paso {k}/", 60)
        time.sleep(espera[k])
        j.captura(f"c{k}_paso")
    j.log(r"cinematica: terminada", 30)
    time.sleep(3)
    j.captura("d_pelea")


def _r3_golpes(j, par):
    # frente R3: combo del jugador (Fin::combo_max, para R1) y codigo de la carga de ki (sonda DBZ3_CINE_TRAZA): Leyenda
    # de Mr. Satan contra Yamcha sin escena, el rival es el muneco quieto; Y+B (P+Backspace) mantenido y luego un combo
    j.log(r"pasarela: prueba: disponible true; empieza", 90)
    j.log(r"pasarela: paso \d+ estado del combate 4", 120)
    time.sleep(1)
    j.tecla("P+Backspace 2500", 300)
    j.captura("a_ki")
    time.sleep(1)
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
            time.sleep(1)
            j.tecla("P+L+Space 2000", 800)
            if re.search(r"fusion: lado 1 mantiene", j.texto_log()):
                break
        j.log(r"fusion: lado 1 mantiene P\+K\+G", 10)
    j.log(r"fusion: lado 1: id 13 -> Yamhan", 30)
    j.log(r"tag: luchador .* en combate: id \d+", 30)
    for i in range(6):
        j.captura(f"02_{i:02d}")
        time.sleep(0.3)
    time.sleep(4)
    j.captura("03_yamhan")
    j.log(r"tag: pose de entrada|fusion", 5)


def _bordes(j, par):
    # frente C (SDK, dbz3_texture_pack.cpp): con "Bordes dorados" apagado la tira "borde" de los menus del juego se
    # sube transparente. --par bordes=true|false (REX_DBZ3_UI_SIDE_BORDERS). Menu principal y Duelo.
    time.sleep(3)
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
    time.sleep(3)
    j.captura("a_preparacion")
    j.tecla("Space", 2500)
    # R7: la intro del Mundo Dragon (DATA_<idioma> 155) tapa el tablero: B la salta; luego los guiones del turno 1
    # (sus ventanas se cierran solas a los 3 s) tienen que acabar antes de poder mover
    j.log(r"tablero b2: escena T155", 15)
    j.tecla("Backspace", 1500)
    j.captura("a1_llegada")   # R7: efectos del banco 343 (capsulas de Raditz y Nappa)
    j.log(r"tablero b2: escena M207", 30)
    time.sleep(1.5)
    j.captura("a1b_charla")   # cabezas de B2 (CMN 1196)
    # orden 4 del guion 301: ventana de mensaje (0 y 78) que espera a A antes de la orden 69. El test antiguo
    # no la cerraba y se quedaba 60 s esperando la eleccion (fallo de tiempo, no del juego).
    j.tecla_hasta_log("Space", r"tablero b2: script 301: eleccion de 1 aliado", intentos=10, seg=4)
    # orden 69 del guion 301: eleccion de 1 aliado (2 en las fases IV, V y IX); Space elige el primero
    time.sleep(1)
    j.captura("a2_aliado")
    j.tecla_hasta_log("Space", r"tablero b2: aliado elegido", intentos=4, seg=3)
    j.log(r"tablero b2: guiones terminados", 60)
    time.sleep(1)
    j.captura("b_tablero")
    j.tecla("W", 900)
    j.captura("c_destino")
    j.tecla("Space", 1500)
    j.log(r"tablero b2: turno 1: .* a la casilla", 10)
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
    for _ in range(12):
        j.tecla("Space", 500)
        j.tecla("Backspace", 3500)
        if re.search(r"tablero b2: combate:", j.texto_log()):
            break
    j.log(r"tablero b2: combate:", 20)
    j.captura("e_contacto")
    time.sleep(2)
    j.captura("f_tarjeta")
    j.tecla("Space", 1000)
    j.log(r"misiones: tablero combate", 20)
    time.sleep(24)
    j.captura("g_combate")


def _r2_nave(j, par):
    # frente R2 (ronda 4): Pasarela > Nave de Babidi de B2 (DBZ3_TEST_MISIONES=5,reset abre el modo con 0 Kili).
    # Graficos de la nave de la ISO (DATA_SP 1131/1132). A entra en la lista (empieza en 1P/2P) -> S S elige la III
    # (golpes 99 s) -> Space: luchador, oponente y sitio (Space en cada uno) -> tarjeta -> Space = combate -> fin a
    # los 99 s -> resultado con el Kili. Antes faltaba el A inicial y las 2 S caian en la pantalla 1P/2P.
    j.log(r"nave: entrar", 90)
    time.sleep(3)
    j.captura("a_nave")
    j.tecla("Space", 900)
    if "prueba" not in par:          # r2_nave: S S baja a la III; con --par prueba=N la fija el entorno
        j.tecla("S", 600)
        j.tecla("S", 600)
    j.captura("b_prueba3")
    j.tecla("Space", 1200)
    j.captura("c_luchador")
    j.tecla("Space", 1200)
    j.captura("d_rival")
    j.tecla("Space", 1200)
    j.captura("e_sitio")
    j.tecla("Space", 1500)
    j.log(r"nave: prueba %d:" % (int(par.get("prueba", "2")) + 1), 20)
    j.captura("f_tarjeta")
    j.tecla("Space", 1000)
    j.log(r"misiones: nave combate", 20)
    time.sleep(24)
    j.captura("g_combate")
    j.log(r"nave: fin:", 720 if par.get("prueba") == "1" else 160)
    j.log(r"nave: resultado de la prueba", 30)
    time.sleep(6)
    j.captura("h_resultado")


def _r2_fase1(j, par):
    # frente R2 (ronda 4): la fase I del Dragon World entera en automatico (DBZ3_TEST_MISIONES=1,auto,reset: Goku va
    # al rival mas cercano; DBZ3_TEST_RINGOUT gana cada combate a los 5 s). Presentacion del script 301, combates,
    # sucesos de contacto (T4/T5) y "Fase completada".
    j.log(r"tablero b2: script 301: mensaje", 120)
    time.sleep(1)
    j.captura("a_presentacion")
    j.log(r"misiones: tablero combate", 120)
    time.sleep(20)
    j.captura("b_combate")
    j.log(r"tablero b2: .*fase 1 completada", 1500)
    time.sleep(0.4)   # el automatico pasa de pantalla a los 1,4 s
    j.captura("c_fase_completada")
    j.log(r"tablero b2: fase 2 leida", 30)
    time.sleep(1.5)
    j.captura("d_fase2_presentacion")


def _r2_deseo(j, par):
    # frente R2 (ronda 4): los deseos de Shenron (DBZ3_TEST_B2_DESEO=1 abre su pantalla al entrar al tablero) y el
    # "Portal de la nave de Babidi", que abre la Nave en la Pasarela.
    j.log(r"tablero b2: fase 1 leida", 90)
    time.sleep(3)
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
        time.sleep(0.4)
        j.captura("fase_%d_completada" % n)


def _iw_misiones(j, par):
    # frente F (ronda 3): Pasarela > Misiones del Dragon de IW (DBZ3_TEST_MISIONES=4 abre el modo desde el menu
    # principal). La lista sale de la tabla del ELF de la ISO de IW (registro "IW mision N"); ficha de la mision 1
    # (Training No.1), su combate, y con --par desde=2 la ficha "no disponibles" de los entrenamientos 2-10.
    j.log(r"misiones: iw(?: \(\d+ pasos\)|: mapa de Dragon Mission)", 90)   # con el mapa de IW (P1), el mapa
    time.sleep(3)
    j.captura("a_ficha")
    if par.get("desde", "0") != "0":
        return
    j.tecla("Space", 2500)
    j.captura("b_tarjeta_combate")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    time.sleep(12)
    j.captura("c_combate")


def _iw_mapa(j, par):
    # frente P1 (ronda 3): Pasarela > Misiones del Dragon de IW con el mapa del mundo de IW (graficos leidos de la copia
    # del jugador). desde=N = superadas hasta la N (por defecto 10: el cursor en la 11, Goku contra Raditz).
    # Mapa, ficha (MISSION START), combate (DBZ3_TEST_RINGOUT lo gana), resultado, mapa despues y cursor movido.
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    time.sleep(4)
    j.captura("a_mapa")
    j.tecla("Space", 2500)
    j.captura("b_ficha")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    time.sleep(16)
    j.captura("c_combate")
    j.log(r"misiones: iw: mision \d+ superada", 90)
    time.sleep(5)
    j.captura("d_resultado")
    j.tecla("Space", 2500)
    if re.search(r"misiones: iw: escena de la historia despues", j.texto_log()):   # R1: escena (FD02 tras la 11)
        time.sleep(2)
        j.captura("d2_escena_despues")
        # el video original (R7) se salta con A; la escena de respaldo, con B
        j.tecla("Space" if re.search(r"video de la historia 2 \(\S+\): reproduciendo", j.texto_log()) else "Backspace", 2500)
    j.captura("e_mapa_despues")
    j.tecla("D", 1500)
    j.captura("f_cursor")
    j.tecla("Backspace", 2000)
    j.captura("g_menu")


def _r1_historia(j, par):
    # frente R1 (ronda 4): escenas de la historia de IW. Con el reproductor de R7 (video_sfd.h): antes de la mision 11
    # el video FD01B con sus subtitulos, despues FD02 (A los salta); sin video, las escenas de respaldo (modelos de B3
    # en el combate / subtitulos sobre el mapa). desde=10: mision 11 (Goku contra Raditz).
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    time.sleep(4)
    j.captura("a_mapa")
    j.tecla("Space", 2500)
    j.captura("b_ficha")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw: escena de la historia antes", 20)
    if re.search(r"video de la historia \d+ \(\S+\): reproduciendo", j.texto_log()):
        time.sleep(6)
        j.captura("c_video_antes")
        j.tecla("Space", 1500)                       # A: salta el video
        j.log(r"misiones: iw combate", 40)
        j.captura("d_tras_video")
    else:
        j.log(r"misiones: iw combate", 40)
        j.log(r"cinematica: paso 2/", 90)   # (DBZ3_TEST_RINGOUT acaba el combate a los 5 s de pelea: corta la escena)
        time.sleep(0.8)
        j.captura("c_escena_antes")
        time.sleep(1.6)
        j.captura("d_escena_antes2")
    j.log(r"misiones: iw: mision \d+ superada", 120)
    time.sleep(5)
    j.captura("e_resultado")
    j.tecla("Space", 2500)
    j.log(r"misiones: iw: escena de la historia despues", 20)
    time.sleep(5)
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
    time.sleep(3)
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
        time.sleep(2.5)
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
    time.sleep(1.5)
    j.captura("e_despues")


def _r1_road(j, par):
    # frente R1 (ronda 4): Fighter's Road de IW. Y (P) en el mapa de Dragon Mission -> eleccion del luchador
    # (D = derecha) -> A: mapa del Road (#DMD 1092) -> ficha -> combate (DBZ3_TEST_RINGOUT) -> resultado -> Y: vuelta.
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    time.sleep(4)
    j.tecla("P", 2500)
    j.log(r"Fighter's Road: eleccion del luchador", 10)
    j.captura("a_eleccion")
    j.tecla("D", 1500)
    j.captura("b_eleccion2")
    j.tecla("Space", 2500)
    j.log(r"Fighter's Road con el id", 10)
    time.sleep(2)
    j.captura("c_mapa_road")
    j.tecla("Space", 2500)
    j.captura("d_ficha")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    time.sleep(16)
    j.captura("e_combate")
    j.log(r"misiones: iw: mision \d+ superada", 120)
    time.sleep(5)
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
    time.sleep(4)
    j.tecla("Space", 2500)
    j.captura("a_ficha")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    j.log(r"iw paso 1 estado del combate 4", 90)
    time.sleep(1)
    j.captura("b_pelea")
    j.tecla("Space 4000", 4500)
    for _ in range(8):
        j.tecla("Space", 400)
    for k in ("D", "A", "W", "S"):
        j.tecla("Space+" + k + " 300", 600)
    j.tecla("Space 3000", 3500)
    j.captura("c_guardia")
    time.sleep(3)
    j.log(r"iw traza: estado 4", 5)


def _r1_entreno9(j, par):
    # frente R1 (ronda 4): Training No.9 de IW (transformarse con 2 barras de ki o mas y ganar). Goku contra Piccolo:
    # nada mas empezar la pelea, P+K+G (P+L+Space) = transformarse (Kaioken); DBZ3_TEST_RINGOUT gana a los 5 s.
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    time.sleep(4)
    j.tecla("Space", 2500)
    j.captura("a_ficha")
    j.tecla("Space", 1500)
    j.log(r"misiones: iw combate", 40)
    j.log(r"iw paso 1 estado del combate 4", 90)
    j.tecla("P+L+Space 200", 900)
    j.captura("b_transformado")
    j.log(r"iw: entrenamiento: transformado", 10)
    j.log(r"misiones: iw: mision \d+ superada", 60)
    time.sleep(5)
    j.captura("c_resultado")


def _r1_road_rueda(j, par):
    # frente R1 (ronda 4): Fighter's Road con la rueda del select del juego. Y (P) en el mapa de Dragon Mission -> el
    # piloto lleva a Duelo > 1P VS COM -> A (Space) confirma el personaje del cursor -> el piloto vuelve -> mapa del Road.
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    time.sleep(4)
    j.tecla("P", 1500)
    j.log(r"Fighter's Road: rueda del juego abierta", 30)
    time.sleep(2)
    j.captura("a_rueda")
    j.tecla("D", 1200)
    j.captura("b_rueda_cursor")
    j.tecla("Space", 1500)
    j.log(r"Fighter's Road con el id \d+ \(rueda del juego", 40)
    time.sleep(3)
    j.captura("c_mapa_road")
    j.tecla("Space", 2500)
    j.captura("d_ficha")


def _r1_traza2(j, par):
    # frente R1 (ronda 4): SONDA 2 de los entrenamientos (DBZ3_R1_TRAZA): mision 12 (Raditz, COM en Normal, ataca) y
    # guardia + direccion a rafagas cortas mientras pega, para que salgan la esquiva y el teletransporte en el registro.
    j.log(r"misiones: iw: mapa de Dragon Mission", 90)
    time.sleep(4)
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
    time.sleep(4)
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


def _iw_minijuego(j, par):
    # frente P4 (ronda 3): minijuego del mapa de IW (src/iw_minijuegos.cpp, DBZ3_TEST_IWMINI=<clave>[,auto]) lanzado
    # desde el menu principal en modo libre (un luchador, camara detras, aros/monedas de IW y TIME). auto=1 (de serie):
    # el jugador salta solo de aro en aro hasta la meta. auto=0: se mueve con la palanca (W/A/S/D) y se ve la marcha.
    j.log(r"iwmini: prueba: empieza", 90)
    j.log(r"iwmini: salida puesta", 60)
    time.sleep(2)
    j.captura("a_salida")
    if par.get("npc") == "1":                         # Bubbles (0x7D1) o Piccolo (0x7DD): se mueven solos
        if par.get("clave") == "0x7D1":               # Bubbles sale delante (con --env DBZ3_IWMINI_NPC_QUIETO=1
            j.tecla("W 900", 950)                     # no huye): acercarse y verle correr en el sitio
            j.tecla("D 250", 300)                     # un paso al lado: que Goku no lo tape
            j.captura("b_bubbles1")
            time.sleep(0.25)
            j.captura("b_bubbles2")
        j.captura("b_npc")
        if par.get("auto", "1") == "1":
            j.log(r"iwmini: (Bubbles atrapado|Piccolo te alcanza|META|TIEMPO)", 120)
            time.sleep(1)
            j.captura("c_fin")
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
        time.sleep(11)
        j.captura("b_quieto1")
        time.sleep(11)
        j.captura("b_quieto2")
        return
    if par.get("salto") == "1":                       # cruz (A del mando = Semicolon): salto de IW, 24 fotos
        if not any((j.dest / "mods").glob("Campo IW *")):   # el salto solo corre sobre el suelo de un campo de IW (d->campo)
            raise Fallo("el salto necesita un mod 'Campo IW <n>' en la carpeta mods de la copia: sin el no hay 'codigo 829'")
        j.log(r"entrada acabada: mando al jugador", 30)
        time.sleep(2.5)                               # el cartel del objetivo (140 cuadros) para el mando
        (j.dest / "dbz3_shot.req").write_text("24 3", encoding="ascii")
        time.sleep(0.6)
        j.tecla("Semicolon", 200)
        time.sleep(2.5)
        j.log(r"codigo 829", 5)
        return
    if par.get("scatto") == "1":                      # circulo (B del mando = Quote): scatto de IW hasta agotarse
        for i in range(4):                            # 47 de energia cada uno: el 3.o agota y el 4.o no sale
            if i == 0 and par.get("rafaga"):          # 24 fotos (1 de cada 2 cuadros) del arranque: el polvo
                j.log(r"entrada acabada: mando al jugador", 30)
                time.sleep(2.5)                       # el cartel del objetivo
                (j.dest / "dbz3_shot.req").write_text("72 1", encoding="ascii")
                time.sleep(0.45)
            j.tecla("Quote", 600)
            j.captura(f"b_scatto{i + 1}")
            time.sleep(1.4)
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
        j.tecla("Up 3000", 3500)
        j.captura("b_adelante")
        j.tecla("Right 1500", 2000)
        j.captura("c_derecha")
        if par.get("rafaga"):   # 24 fotos (1 de cada 6 cuadros) corriendo, en dbz3_shot_NNN.bmp
            (j.dest / "dbz3_shot.req").write_text("24 6", encoding="ascii")
            time.sleep(0.6)
        j.tecla("W 2500", 3000)
        j.captura("d_adelante2")
        j.log(r"iwmini: jugador", 10)
        return
    # los aros (y marcas del Radar) que da el #ITM en esa dificultad: "curso (dificultad d): A n B .. C .. D m"
    m = re.search(r"iwmini: curso \(dificultad \d+\): A (\d+) B \d+ C \d+ D (\d+)", j.texto_log())
    n = int(m.group(1)) + int(m.group(2)) if m else 8
    j.log(rf"iwmini: aro {min(3, n)} cogido", 40)
    time.sleep(0.3)
    j.captura("b_aros")
    j.log(rf"iwmini: aro {n} cogido", 40)
    time.sleep(0.3)
    j.captura("c_aros")
    j.log(r"iwmini: (META|TIEMPO):", 60)
    if par.get("rafaga"):   # --par rafaga=1: 24 fotos (1 de cada 10 cuadros) desde la meta, en dbz3_shot_NNN.bmp
        (j.dest / "dbz3_shot.req").write_text("24 10", encoding="ascii")
        time.sleep(4.5)
    time.sleep(1)
    j.captura("d_meta")
    j.log(r"iwmini: final de IW", 30)   # con --env DBZ3_IWMINI_FINAL=1: la animacion final (pantalla azul R16)
    time.sleep(1.5)
    j.captura("d_final")
    j.log(r"iwmini: prueba: resultado", 60)
    time.sleep(3)
    j.captura("e_menu")


def _r6_ranking(j, par):
    # frente R6 (ronda 4): HUB > Ranking (entrada 6): pagina del perfil y la siguiente (RB), y el Kumite (entrada 5)
    # con su selector de retos. Solo mira: no empieza nada.
    time.sleep(2.5)
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
    time.sleep(2.5)
    j.tecla("W", 1200)
    j.tecla("Space", 3500)                              # HUB
    for _ in range(3):                                  # tagteam .. torre
        j.tecla("S", 450)
    j.tecla("Space", 2500)                              # selector de retos (Torre arriba)
    j.captura("a_selector_torre")
    j.tecla_hasta_log("Space", r"hub: combate nativo", 4, 4)
    time.sleep(14)
    j.captura("b_combate")
    j.log(r"rival fuera del ring", 90)
    j.log(r"hub: fin del combate nativo", 40)
    time.sleep(5)
    j.captura("c_entre")


def _m100(j, par):
    # Mision 100 (rpg.cpp): DBZ3_TEST_MODO empieza la mision n con el luchador id; ring out (DBZ3_TEST_RINGOUT), su
    # pantalla final con el rango y, con A, vuelta a sus paginas.
    j.log(r"prueba: mision 100 n \d+ con id \d+: empieza", 120)
    j.log(r"hub: combate nativo", 60)
    time.sleep(14)
    j.captura("a_combate")
    j.log(r"rpg: fin de", 150)
    time.sleep(4)
    j.captura("b_fin")
    j.tecla("Space", 2500)
    j.captura("c_paginas")
    j.tecla("D", 800)                                   # otro luchador
    j.tecla("S", 800)
    j.captura("d_paginas2")


def _r6_hud(j, par):
    # frente R6 (ronda 4): HUD de los modos en combate contra varios rivales (Modo Heroe, Saibaman 2/2: el contador
    # nativo de rivales a la derecha del reloj) y la pantalla entre combates de rpg.cpp. Mismo camino que r5_heroe.
    time.sleep(2.5)
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
    time.sleep(7)
    j.captura("a0_presentacion")                        # sin HUD del juego: tampoco el del modo
    time.sleep(13)                                      # pasa la presentacion: HUD del juego a la vista
    j.captura("a_hud")
    time.sleep(3)
    j.captura("b_hud")
    j.log(r"combate \d ganado|rpg: fin de", 60)        # (una partida a medias sigue por el 2 o el 3: fin)
    time.sleep(6)
    j.captura("c_entre")
    if par.get("estilo") == "1":                      # RB (tecla 3): otro estilo para el combate siguiente y A
        j.tecla("3", 1500)
        j.log(r"heroe: estilo \d+ para el combate", 10)
        j.captura("d_estilo")
        j.tecla_hasta_log("Space", r"hub: combate nativo", 4, 6)
        time.sleep(20)
        j.captura("e_combate2")
        j.log(r"heroe: estilo \d+ \(", 30)


def _r6_pantallas(j, par):
    # frente R6 (ronda 4): pantallas de los modos que no salen en las otras pruebas. que=entre|derrota|fin: modos.cpp
    # las pinta en el menu principal con un reto de ejemplo (DBZ3_TEST_R6_PANTALLA, sin tocar nada); que=sim|torneo:
    # HUB > Supervivencia/Kumite > Simulacion/Torneo de 4 (su primera pantalla); que=abyss: HUB > Torre > Abyss, gana
    # el primer piso (ring out) y captura las cartas.
    que = par.get("que", "entre")
    time.sleep(3)
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
        time.sleep(6)
        j.captura("c_cartas")


def _r8_limite(j, par):
    # frente R8: hasta donde llega un luchador en un escenario. Combate del HUB (--par escenario=5 Llanuras, 17 = alta);
    # el jugador 1 anda con la palanca (--par tecla=Left) mientras se leen las posiciones de los dos (X+64/+68/+72,
    # X = mgr+2096+8240*lado) y el aviso de fuera del ring (mgr+35569, lo escribe sub_8211F650). Escribe limite.txt.
    # --par clave=0x7D2: en el modo libre del minijuego de IW (P4; su campo si esta instalado), tecla W por defecto.
    import struct as _st
    j.log(r"iwmini: salida puesta" if par.get("clave") else r"tag: empieza un combate", 90)
    time.sleep(1)

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
            time.sleep(0.05)
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
        time.sleep(0.05)
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
    time.sleep(4)
    j.captura("b_iconos")
    time.sleep(6)
    j.captura("c_iconos")
    j.log(r"r14: fin: ", int(par.get("seg", "240")))   # 0x7DA (10 rondas) tarda ~4 min: --par seg=420
    time.sleep(1.5)
    j.captura("d_fin")
    j.log(r"r14: resultado ", 30)
    j.log(r"iwmini: prueba: resultado superado", 30)


def _r12_vuelo(j, par):
    # frente R12: minijuegos de vuelo de IW (src/iw_mini_vuelo.cpp: 0x7D6 Vegeta, 0x7E0 carrera con Goten). auto=1 (de
    # serie): piloto que gira hacia el punto de control mas cercano (y luego la meta) con R1 si el medidor lo permite.
    j.log(r"iwmini: prueba: empieza", 90)
    j.log(r"r12: empieza el vuelo", 90)
    time.sleep(1.5)
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
    time.sleep(5)
    j.captura("b_vuelo")
    j.log(r"r12: punto de control", 60)
    time.sleep(0.3)
    j.captura("c_control")
    j.log(r"r12: fin \(", 240)
    time.sleep(1.0)
    j.captura("d_fin")
    j.log(r"r12: resultado ", 60)
    j.log(r"iwmini: prueba: resultado superado", 30)


ESCENARIOS = {
    "_r12_vuelo": ("R12: minijuego de vuelo de IW (--par clave=0x7D6 Vegeta | 0x7E0 Goten; auto=0 sin piloto)",
                   lambda p: {"DBZ3_TEST_IWMINI": p.get("clave", "0x7D6") + (",auto" if p.get("auto", "1") == "1"
                                                                               else ""),
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
                                "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _iw_minijuego),
    "iw_mapa": ("Pasarela > Misiones del Dragon de IW con el mapa de IW: mapa, ficha, combate, resultado (desde=N)",
                lambda p: {"DBZ3_TEST_MISIONES": f"4,reset,desde={p.get('desde', '10')}", "DBZ3_TEST_RINGOUT": "1",
                           "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"}, _iw_mapa),
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
                lambda p: dict({"DBZ3_TEST_MISIONES": "5,reset", "DBZ3_PS2_GAMES": r"D:\DBZ3HD\ps2_games"},
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
    "bordes": ("menu principal y Duelo con las tiras doradas nativas ocultas o visibles (--par bordes=true)",
               lambda p: {"REX_DBZ3_UI_SIDE_BORDERS": p.get("bordes", "false")}, _bordes),
    "satan": ("HUB > Leyenda de Mr. Satan: titulo, dificultad, prologo, combate ganado, resultado, tablero, escena "
              "(desde=25: final)",
              lambda p: {"DBZ3_TEST_PASARELA": f"auto,seg=8,desde={p.get('desde', '0')}", "DBZ3_TEST_RINGOUT": "1",
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
    "fusion_yamhan": ("combate normal Yamcha vs Vegeta: a los 3 s se fusiona en Yamhan (src/fusion.cpp)",
                      lambda p: dict({"DBZ3_TEST_BATTLE": p.get("ids", "13,7,2")},
                                     **({} if p.get("gesto") == "1" else {"DBZ3_TEST_FUSION": p.get("seg", "3")})),
                      _fusion_yamhan),
    "tag_swap": ("Tag Team 1P vs COM, relevo con SELECT",
                 lambda p: dict({"DBZ3_TAG_SLOW": "3"},
                                **({} if p.get("swap", "1") == "1" else {"DBZ3_TEST_KO": "1"})),
                 _tag_swap),
}


def ejecutar_dsl(j, pasos):
    for p in pasos:
        v, *a = shlex.split(p)
        if v == "tecla":
            j.tecla(a[0].replace(":", " "), int(a[1]) if len(a) > 1 else 600)
        elif v == "esperar":
            time.sleep(int(a[0]) / 1000)
        elif v == "esperar_pantalla":
            j.esperar_pantalla([int(x) for x in a[0].split(",")], int(a[1]) if len(a) > 1 else 60)
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
        return d.get("descripcion", ruta.name), (lambda j, _p: ejecutar_dsl(j, pasos)), env
    if esc not in ESCENARIOS:
        raise SystemExit(f"escenario desconocido '{esc}'. Opciones: {', '.join(ESCENARIOS)} o un .toml")
    desc, env_fn, fn = ESCENARIOS[esc]
    return desc, fn, env_fn(par)


def correr(copia, escenario, env_extra=(), instantanea=None, par=None):
    with copia_en_uso(BASE / copia):
        return _correr(copia, escenario, env_extra, instantanea, par)


def _correr(copia, escenario, env_extra=(), instantanea=None, par=None):
    par = par or {}
    desc, fn, env = cargar_escenario(escenario, par)
    env.update(_kv(x) for x in env_extra)
    nombre = f"{copia}/{Path(escenario).stem}"
    print(f"== {nombre}: {desc}", flush=True)
    t0 = time.time()
    preparar(BASE / copia)
    if instantanea:
        cargar_instantanea(copia, instantanea)
    else:
        respaldar(BASE / copia)
    j = Juego(copia, nombre)
    ok, msg, t_menu, t_cupo = True, "", None, 0.0
    try:
        j.lanzar(env)
        t_cupo = j.t_cupo
        # --par arranque=S: espera mas al menu (R8: el paso de escenarios de PS2 corre antes de arrancar)
        t_menu = j.arrancar(float(par.get("arranque", "120")), rapido=par.get("titulo_rapido", "1") == "1")
        j.info(f"menu principal en {t_menu:.1f} s")
        fn(j, par)
        if not j.vivo():
            raise Fallo("el juego se cerro durante la prueba")
    except Exception as e:
        ok, msg = False, str(e) or type(e).__name__
        j.captura_fallo()
    finally:
        muerto = j.proc is not None and j.proc.poll() is not None    # murio solo (no lo cerramos nosotros)
        t_fin = time.time()
        j.cerrar()
        t_cierre = time.time() - t_fin
    texto = j.texto_log()
    malos = [l for l in texto.splitlines() if re.search(ERRORES, l)]
    if muerto:
        rc = j.proc.returncode & 0xFFFFFFFF
        ok, msg = False, f"el juego murio (codigo 0x{rc:08X}{', access violation' if rc == 0xC0000005 else ''}). {msg}"
    if malos and ok:
        ok, msg = False, f"log con errores ({len(malos)}): {malos[0][:150]}"
    if texto:
        (j.run / "dbz3.log").write_text(texto, encoding="utf-8")
    (j.run / "pantallas.txt").write_text("".join(f"{t:6.1f} s  {sc}\n" for t, sc in j.traza), encoding="utf-8")
    if not ok:
        extracto = informe_fallo(texto, malos)
        (j.run / "fallo.txt").write_text(f"{msg}\n\n{extracto}\n", encoding="utf-8")
        print("\n".join("      | " + l for l in extracto.splitlines()[-12:]), flush=True)
    dur = time.time() - t0 - t_cupo
    print(f"{'PASS' if ok else 'FAIL'}  {nombre}  {dur:.1f} s (menu {t_menu or 0:.1f} s, cierre {t_cierre:.1f} s"
          f"{f', cola {t_cupo:.0f} s' if t_cupo > 1 else ''})  {msg}".rstrip(), flush=True)
    print(f"      capturas y resultados: {j.run}", flush=True)
    return {"ok": ok, "nombre": nombre, "dur": dur, "menu": t_menu, "cierre": t_cierre, "msg": msg, "run": j.run}


def informe_fallo(texto, malos):
    """Extracto del log para el fallo: lineas de error y las ultimas 25 (sin las de rendimiento)."""
    lineas = [l for l in texto.splitlines() if "perf fps=" not in l and "audio pico=" not in l]
    partes = []
    if malos:
        partes += ["-- errores --"] + [l[:220] for l in malos[:8]]
    partes += ["-- final del log --"] + [l[:220] for l in lineas[-25:]]
    return "\n".join(partes) if texto else "(sin log del juego)"


def paralelo(pares, env_extra=()):
    trabajos = []
    for p in pares:
        esc, _, cop = p.partition("@")
        if not esc or not cop:
            raise SystemExit(f"formato esc@copia, recibido: {p}")
        trabajos.append((cop, esc))
    if len(trabajos) > MAX_PARALELO:
        raise SystemExit(f"como maximo {MAX_PARALELO} a la vez")
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
COPIAS_SMOKE = ["smoke_a", "smoke_b", "smoke_c"]


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


def todo(solo=None, maxp=MAX_PARALELO, env_extra=()):
    lista = [s for s in SMOKE if not solo or s[0] in solo]
    if not lista:
        raise SystemExit(f"nada que correr; escenarios de la bateria: {', '.join(s[0] for s in SMOKE)}")
    faltan = sorted({i for _, i, _ in lista if i and not (INST / i / "user_data").exists()})
    if faltan:
        raise SystemExit(f"faltan instantaneas {faltan}: python tools\\pruebas.py biblioteca")
    libres = queue.Queue()
    for c in COPIAS_SMOKE[:max(1, min(maxp, MAX_PARALELO))]:
        libres.put(c)

    def uno(s):
        c = libres.get()
        try:
            return correr(c, s[0], env_extra, s[1], dict(s[2]))
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
    a = sub.add_parser("paralelo", help="hasta 3 pruebas a la vez, cada una en su copia")
    a.add_argument("pares", nargs="+", help="escenario@copia")
    a.add_argument("--env", action="append", default=[], metavar="K=V")
    a = sub.add_parser("todo", help="bateria rapida (smoke) en smoke_a/b/c, tabla PASS/FAIL")
    a.add_argument("--solo", help="lista separada por comas (" + ",".join(s[0] for s in SMOKE) + ")")
    a.add_argument("--max", type=int, default=MAX_PARALELO, help="juegos a la vez (1-3)")
    a.add_argument("--env", action="append", default=[], metavar="K=V")
    sub.add_parser("biblioteca", help="crea las instantaneas de prueba (perfil_nuevo)")
    a = p.parse_args(argv)

    if a.cmd == "todo":
        return 0 if todo(a.solo.split(",") if a.solo else None, a.max, a.env) else 1
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
        r = correr(a.copia, a.escenario, a.env, a.instantanea, dict(_kv(x) for x in a.par))
        return 0 if r["ok"] else 1
    return 0 if paralelo(a.pares, a.env) else 1


if __name__ == "__main__":
    sys.exit(main())
