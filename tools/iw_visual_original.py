# SPDX-License-Identifier: MIT
r"""iw_visual_original.py (fase 1, 7/11) - graba en video el minijuego ORIGINAL de IW desde su savestate del pool de PCSX2.

  python tools\iw_visual_original.py 0x7E0 [0x7D6 ...] [--pool 2] [--seg 90] [--sal E:\DBZ3HD_trabajo\iw_visual]

Lanza la instancia del pool con el savestate del minijuego (dif_grabar_pool.estado_por_defecto), espera a que el contador
de cuadros avance y graba la ventana del emulador con ffmpeg gfxcapture (como tools/grabar.py) SEGUNDOS. Sin teclas.
Salida: <sal>\orig_<clave>.mp4. Cierra solo el pid de su pool.
"""
import argparse
import ctypes
import ctypes.wintypes
import subprocess
import sys
import time
from pathlib import Path

AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI))
import dif_grabar_pool as G  # noqa: E402

U32 = ctypes.WinDLL("user32", use_last_error=True)


def ventana_de(pid):
    """La ventana visible con mayor area del proceso pid (la del juego del emulador)."""
    res = []

    @ctypes.WINFUNCTYPE(ctypes.wintypes.BOOL, ctypes.wintypes.HWND, ctypes.wintypes.LPARAM)
    def cb(w, _):
        p = ctypes.wintypes.DWORD()
        U32.GetWindowThreadProcessId(w, ctypes.byref(p))
        if int(p.value) == pid and U32.IsWindowVisible(w):
            r = ctypes.wintypes.RECT()
            U32.GetClientRect(w, ctypes.byref(r))
            res.append(((r.right - r.left) * (r.bottom - r.top), w))
        return True
    U32.EnumWindows(cb, 0)
    return max(res)[1] if res and max(res)[0] > 0 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("claves", nargs="+")
    ap.add_argument("--pool", type=int, default=2)
    ap.add_argument("--seg", type=int, default=90)
    ap.add_argument("--sal", default=r"E:\DBZ3HD_trabajo\iw_visual")
    a = ap.parse_args()
    Path(a.sal).mkdir(parents=True, exist_ok=True)
    pool = [sys.executable, str(AQUI / "pcsx2_pool.py")]
    import pcsx2_pool as PP
    import pcsx2_sonda as S
    for clave in a.claves:
        est = G.estado_por_defecto(clave)
        subprocess.run(pool + ["preparar", str(a.pool), "--estado", str(est)], check=True, timeout=120, capture_output=True)
        subprocess.run(pool + ["lanzar", str(a.pool), "--iso", G.ISO_EU, "--estado", str(est), "--ejecutar"], check=True,
                       timeout=120, capture_output=True)
        pid = G.pid_del_pool(a.pool)
        try:
            PP.registrar_en_sonda(S, a.pool)
            t = time.time()
            while time.time() - t < 300:   # el savestate cargado y corriendo (el contador de cuadros avanza)
                try:
                    p = S.Pine(); x = p.u32(0x4A4D40); time.sleep(2)
                    if p.u32(0x4A4D40) != x:
                        break
                except Exception:   # noqa: BLE001
                    time.sleep(4)
            h = ventana_de(pid)
            if not h:
                print(clave, "sin ventana del emulador"); continue
            video = Path(a.sal) / f"orig_{clave}.mp4"
            fuente = "gfxcapture=hwnd=%d:capture_cursor=0:max_framerate=30,hwdownload,format=bgra,crop=trunc(iw/2)*2:trunc(ih/2)*2" % int(h)
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", fuente, "-t", str(a.seg),
                            "-c:v", "h264_nvenc", "-cq", "21", "-pix_fmt", "yuv420p", str(video)], check=False)
            print(clave, "->", video, video.exists() and video.stat().st_size, flush=True)
        finally:
            if pid:
                subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True); time.sleep(2)
            subprocess.run(pool + ["liberar", str(a.pool)], timeout=60, capture_output=True)


if __name__ == "__main__":
    main()
