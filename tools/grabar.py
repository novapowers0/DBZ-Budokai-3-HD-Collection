#!/usr/bin/env python3
r"""grabar.py - clips de video CON SONIDO del juego para los videos de las versiones.

Lanza una prueba de tools/pruebas.py (copia aislada en D:\DBZ3HD) y, mientras corre, graba la ventana del juego con
ffmpeg (gfxcapture + NVENC). El sonido no se puede grabar del sistema (no hay dispositivo de "lo que suena"), asi que el
propio juego lo guarda (cvar dbz3_audio_dump del SDK, float32 estereo 48 kHz, aunque este silenciado) y al final se
junta con la imagen usando las horas de inicio de los dos (la del audio sale en el log del juego).

  python tools\grabar.py COPIA ESCENARIO NOMBRE SEGUNDOS [--retraso S] [--par k=v] [--env K=V]
  python tools\grabar.py iso_test D:\tmp\rec\com_tag.toml 01_com_tagteam_a 90 --retraso 20
  python tools\grabar.py smoke_b D:\tmp\rec\vitrina.toml t16_goku_gt 10 --desde-log "dbz3 cine: vitrina empieza" --salida D:\DBZ3HD\video\clips\v156

SEGUNDOS es el maximo (para antes si el juego se cierra). Salida: video/clips/proxima/NOMBRE.mp4 (y NOMBRE_v.mp4 + NOMBRE.f32 de trabajo) o la carpeta de --salida. Nunca mata ffmpeg a medias (el mp4 se
quedaria sin indice): espera a que acabe sus SEGUNDOS. Se captura la propia ventana (Windows.Graphics.Capture):
lo que tenga encima no sale, pero no se puede minimizar. Con --desde-log empieza cuando esa linea sale
en el registro del juego (una sola ventana a la vez).
"""
import argparse
import ctypes
import ctypes.wintypes
import os
import re
import subprocess
import sys
import time
from pathlib import Path

R = Path(__file__).resolve().parent.parent
SALIDA = R / "video" / "clips" / "proxima"
TITULO = "Dragon Ball Z Budokai 3 HD"
U32 = ctypes.WinDLL("user32", use_last_error=True)
U32.FindWindowW.restype = ctypes.wintypes.HWND
U32.GetForegroundWindow.restype = ctypes.wintypes.HWND
U32.WindowFromPoint.restype = ctypes.wintypes.HWND
U32.SetWindowPos.argtypes = [ctypes.wintypes.HWND, ctypes.wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                             ctypes.c_int, ctypes.c_uint]
U32.SetWindowPos.restype = ctypes.wintypes.BOOL


def pids_titulo():
    """PIDs de los dbz3.exe con la ventana del juego abierta (puede haber partidas de otros agentes)."""
    pids = set()
    buf = ctypes.create_unicode_buffer(256)

    @ctypes.WINFUNCTYPE(ctypes.wintypes.BOOL, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)
    def cb(h, _):
        U32.GetWindowTextW(h, buf, 256)
        if buf.value == TITULO:
            pid = ctypes.wintypes.DWORD()
            U32.GetWindowThreadProcessId(h, ctypes.byref(pid))
            pids.add(int(pid.value))
        return True
    U32.EnumWindows(cb, 0)
    return pids


def ventana(pid=None):
    """Ventana del juego; con pid, la de ESE proceso (la partida que acabamos de lanzar)."""
    h = U32.FindWindowW(None, TITULO)
    if pid is None or not h:
        return h
    buf = ctypes.create_unicode_buffer(256)
    res = []

    @ctypes.WINFUNCTYPE(ctypes.wintypes.BOOL, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)
    def cb(w, _):
        U32.GetWindowTextW(w, buf, 256)
        if buf.value == TITULO:
            p = ctypes.wintypes.DWORD()
            U32.GetWindowThreadProcessId(w, ctypes.byref(p))
            if int(p.value) == pid:
                res.append(w)
        return True
    U32.EnumWindows(cb, 0)
    # puede haber mas de una con ese titulo en el mismo proceso: vale la visible y con area (la del juego)
    def area(w):
        r = ctypes.wintypes.RECT()
        U32.GetClientRect(w, ctypes.byref(r))
        return (r.right - r.left) * (r.bottom - r.top)
    visibles = [w for w in res if U32.IsWindowVisible(w) and area(w) > 0]
    return max(visibles, key=area) if visibles else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("copia")
    ap.add_argument("escenario")
    ap.add_argument("nombre")
    ap.add_argument("segundos", type=int)
    ap.add_argument("--retraso", type=float, default=0.0, help="segundos tras abrirse la ventana (o tras --desde-log) antes de grabar")
    ap.add_argument("--par", action="append", default=[])
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--escala", help="tamano final del clip WxH (si la ventana dio otro, se reescala)")
    ap.add_argument("--salida", help="carpeta de salida (por defecto video/clips/proxima)")
    ap.add_argument("--desde-log", help="empezar a grabar cuando esta regex salga en el registro del juego")
    ap.add_argument("--espera-log", type=float, default=300.0, help="segundos maximos esperando la linea de --desde-log")
    a = ap.parse_args()
    salida = Path(a.salida) if a.salida else SALIDA
    salida.mkdir(parents=True, exist_ok=True)
    audio = salida / (a.nombre + ".f32")
    video = salida / (a.nombre + "_v.mp4")
    final = salida / (a.nombre + ".mp4")
    cmd = [sys.executable, str(R / "tools" / "pruebas.py"), "correr", a.copia, a.escenario, "--forzar",
           "--env", "REX_DBZ3_AUDIO_DUMP=" + str(audio)]
    for p in a.par:
        cmd += ["--par", p]
    for e in a.env:
        cmd += ["--env", e]
    pids_antes = pids_titulo()          # puede haber partidas de otros agentes con la misma ventana
    prueba = subprocess.Popen(cmd, cwd=str(R))
    logs = Path(r"D:\DBZ3HD") / a.copia / "logs"
    previos = {p.name for p in logs.glob("dbz3_*.log")}   # el registro de esta partida es uno nuevo
    t0, pid = time.time(), None
    while True:
        if prueba.poll() is not None or time.time() - t0 > 600:
            sys.exit("no se abrio la ventana del juego")
        nuevos = pids_titulo() - pids_antes
        if len(nuevos) == 1:
            pid = next(iter(nuevos))
        if pid is not None and ventana(pid):
            break
        time.sleep(0.25)
    if a.desde_log:
        # el registro de la copia (dbz3_*.log): se espera la linea (p.ej. el empiezo de la camara de vitrina). Solo
        # los registros NUEVOS: el de una partida anterior ya tiene esa linea y empezaria a grabar al instante.
        t1 = time.time()
        while True:
            if prueba.poll() is not None:
                sys.exit("el juego se cerro antes de la linea del registro: " + a.desde_log)
            if time.time() - t1 > a.espera_log:
                sys.exit("no salio la linea del registro en %d s: %s" % (a.espera_log, a.desde_log))
            nuevos = sorted((p for p in logs.glob("dbz3_*.log") if p.name not in previos),
                            key=lambda p: p.stat().st_mtime)
            if nuevos:
                try:
                    with open(nuevos[-1], "r", encoding="utf-8", errors="replace") as fh:
                        fh.seek(max(0, fh.seek(0, 2) - 262144))   # solo el final: el registro crece
                        if re.search(a.desde_log, fh.read()):
                            break
                except OSError:
                    pass
            time.sleep(0.2)
    time.sleep(a.retraso)
    h = ventana(pid)
    print("  grabacion: ventana %#x" % int(h), flush=True)
    inicio_v = int(time.time() * 1000)
    # gfxcapture graba la ventana aunque otra la tape (gdigrab con title= da negro con el swapchain de D3D12)
    fuente = "gfxcapture=hwnd=%d:capture_cursor=0:max_framerate=%d,hwdownload,format=bgra,crop=trunc(iw/2)*2:trunc(ih/2)*2" % (int(h), a.fps)
    rec = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", fuente,
                            "-t", str(a.segundos), "-c:v", "h264_nvenc", "-cq", "19", "-pix_fmt", "yuv420p", str(video)],
                           stdin=subprocess.PIPE)
    # SEGUNDOS es el maximo: si el juego se cierra antes, ffmpeg para con "q" (cierra bien el mp4; matarlo lo
    # dejaria sin indice) y no graba lo que haya detras de la ventana (el escritorio del usuario)
    while rec.poll() is None:
        if not ventana(pid):
            try:
                rec.stdin.write(b"q")
                rec.stdin.flush()
            except OSError:
                pass
            break
        time.sleep(0.2)
    rec.wait()          # nunca se corta a medias
    prueba.wait()
    # la hora de inicio del audio: la linea del volcado en el log mas nuevo de la copia
    logs = sorted((Path(r"D:\DBZ3HD") / a.copia / "logs").glob("dbz3_*.log"), key=lambda p: p.stat().st_mtime)
    inicio_a = None
    for log in reversed(logs[-3:]):
        m = re.search(r"dbz3 audio: volcado abierto .*?\(inicio (\d+) ms", log.read_text("utf-8", "replace"))
        if m:
            inicio_a = int(m.group(1))
            break
    if inicio_a is None or not audio.exists() or audio.stat().st_size == 0:
        print("sin audio (cvar dbz3_audio_dump no activo?): queda solo la imagen en", video)
        return
    desfase = (inicio_v - inicio_a) / 1000.0   # cuanto audio sobra al principio
    # desfase > 0: el audio empezo antes (se recorta); < 0: empezo despues (el juego arranca tras el launcher: se
    # retrasa con silencio delante)
    corte = ["-ss", "%.3f" % desfase] if desfase >= 0 else ["-itsoffset", "%.3f" % -desfase]
    # --escala WxH: si la ventana no dio ese tamano (DPI), el clip se deja en el pedido
    imagen = ["-c:v", "copy"]
    if a.escala:
        actual = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                                 "stream=width,height", "-of", "csv=p=0", str(video)],
                                capture_output=True, text=True).stdout.strip().replace(",", "x")
        if actual != a.escala:
            w, h = a.escala.split("x")
            imagen = ["-vf", "scale=%s:%s:flags=lanczos" % (w, h), "-c:v", "h264_nvenc", "-cq", "19", "-pix_fmt", "yuv420p"]
            print("  grabacion: ventana %s -> %s" % (actual, a.escala))
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(video), "-f", "f32le", "-ar", "48000",
                    "-ac", "2", *corte, "-i", str(audio), "-map", "0:v", "-map", "1:a", *imagen,
                    "-af", "apad", "-c:a", "aac", "-b:a", "192k", "-shortest", str(final)], check=True)
    print("clip:", final, "(desfase audio %.3f s)" % desfase)


if __name__ == "__main__":
    main()
