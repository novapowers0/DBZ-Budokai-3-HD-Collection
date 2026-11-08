"""dsPHIRES: mide una pasada de 'batalla' con ventana y escala interna dadas.

Lee la linea `perf` del log (fps, p99, max, gpu_wait, cp_wait, vram) y muestrea
la VRAM real del proceso con nvidia-smi mientras dura el combate.

  python tools/dsPHIRES_medir.py --escala 1 --ventana 3840x2160 [--etiqueta 4k_1x]
  python tools/dsPHIRES_medir.py --escala 3 --ventana 1280x720 --afinidad 3
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import json
import os
import re
import subprocess
import threading
import time
from pathlib import Path

import pruebas as P

SALIDA = Path("D:/tmp/dsPHIRES")


def _imagen(pid):
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    h = k.OpenProcess(0x1000, False, pid)          # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return ""
    buf = ctypes.create_unicode_buffer(1024)
    n = wt.DWORD(1024)
    ok = k.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n))
    k.CloseHandle(h)
    return str(Path(buf.value).resolve()).lower() if ok else ""


def _muestrear(copia, parar, muestras, afinidad):
    base = str((Path(copia) / "dbz3.exe").resolve()).lower()
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    fijados = set()
    while not parar.is_set():
        try:
            out = subprocess.run(["nvidia-smi", "pmon", "-c", "1"], capture_output=True, text=True,
                                 timeout=10).stdout
            total = subprocess.run(["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits"],
                                   capture_output=True, text=True, timeout=10).stdout
            total_mb = int(total.strip().splitlines()[0])
        except Exception:  # noqa: BLE001
            out, total_mb = "", None
        for ln in out.splitlines():
            f = ln.split()
            if len(f) < 3 or not f[1].isdigit() or not f[0].isdigit():
                continue
            pid = int(f[1])
            if _imagen(pid) != base:
                continue
            sm = int(f[3]) if f[3].isdigit() else None
            muestras.append((time.time(), pid, sm, total_mb))
            if afinidad and pid not in fijados:
                h = k.OpenProcess(0x0200 | 0x0100, False, pid)   # SET_QUOTA | SET_INFORMATION
                if h:
                    k.SetProcessAffinityMask(h, ctypes.c_size_t(afinidad))
                    k.CloseHandle(h)
                fijados.add(pid)
        time.sleep(1.5)


def _perf(texto):
    filas = []
    for m in re.finditer(r"dbz3: perf fps=([0-9.]+) .*?scale:(\d+)x(\d+).*?"
                         r"vram=(\d+)MB/(\d+)MB.*?gpu_wait=([0-9.]+)ms/f.*?cp_wait=([0-9.]+)ms/f"
                         r"(?:.*?gpu_ms=([0-9.]+))?", texto):
        filas.append({"fps": float(m.group(1)), "escala": f"{m.group(2)}x{m.group(3)}",
                      "vram": int(m.group(4)), "vram_max": int(m.group(5)),
                      "gpu_wait": float(m.group(6)), "cp_wait": float(m.group(7)),
                      "gpu_ms": float(m.group(8)) if m.group(8) else None})
    return filas


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--escala", type=int, default=1)
    ap.add_argument("--ventana", default="1280x720")
    ap.add_argument("--copia", default="hires")
    ap.add_argument("--etiqueta", default="")
    ap.add_argument("--afinidad", default="", help="mascara de CPU para start /affinity (p. ej. 3 = 2 nucleos)")
    ap.add_argument("--render", default="", help="DBZ3_TEST_RENDER=ANCHOxALTO (baja solo el 3D)")
    a = ap.parse_args()
    os.environ.update(DBZ3_AGENTE="dsPHIRES", PYTHONUTF8="1", DBZ3_PRUEBAS_LENTO="1")
    P.OVERRIDES["dbz3_perf_logging"] = "true"
    P.OVERRIDES["dbz3_resolution_scale"] = str(a.escala)
    SALIDA.mkdir(parents=True, exist_ok=True)
    dest = P.BASE / a.copia
    antes = set(p.name for p in (dest / "logs").glob("dbz3_*.log"))

    def env_fn(_par):
        env = {"DBZ3_TEST_BATTLE": "0,7,2", "DBZ3_TEST_RINGOUT": "1"}
        if a.render:
            env["DBZ3_TEST_RENDER"] = a.render
        return env
    clave = "dsPHIRES_%s_%s_%s" % (a.escala, a.ventana.replace("x", "-"), a.render or "auto")
    P.ESCENARIOS[clave] = ("dsPHIRES: escala %d ventana %s" % (a.escala, a.ventana), env_fn, P._batalla)

    parar, muestras = threading.Event(), []
    hilo = threading.Thread(target=_muestrear, args=(str(dest), parar, muestras, int(a.afinidad) if a.afinidad else 0), daemon=True)
    hilo.start()
    t0 = time.time()
    try:
        r = P.correr(a.copia, clave, [], None, {"ventana": a.ventana}, True)
    finally:
        parar.set()
        hilo.join(timeout=5)

    nuevos = [p for p in (dest / "logs").glob("dbz3_*.log") if p.name not in antes]
    if not nuevos:
        nuevos = sorted((dest / "logs").glob("dbz3_*.log"), key=lambda p: p.stat().st_mtime)[-1:]
    texto = nuevos[-1].read_text(encoding="utf-8", errors="replace") if nuevos else ""
    perf = _perf(texto)
    total_mb = max((m[3] for m in muestras if m[3]), default=None)
    sm = max((m[2] for m in muestras if m[2] is not None), default=None)
    res = {"etiqueta": a.etiqueta or clave, "ok": r["ok"], "dur": round(r["dur"], 1),
           "escala": a.escala, "ventana": a.ventana, "render": a.render, "afinidad": a.afinidad,
           "perf": perf[-6:], "gpu_total_max_mb": total_mb, "sm_max_pct": sm, "muestras": len(muestras),
           "msg": r.get("msg", "")}
    f = SALIDA / ("medida_%s_%d.json" % ((a.etiqueta or clave).replace("/", "_"), time.time_ns()))
    f.write_text(json.dumps(res, indent=1, ensure_ascii=False, default=str), encoding="utf-8")
    print(json.dumps({k: res[k] for k in ("etiqueta", "ok", "dur", "gpu_total_max_mb", "sm_max_pct")},
                     ensure_ascii=False))
    if perf:
        print("  ultimo perf:", json.dumps(perf[-1], ensure_ascii=False))
    print("  ", f)
    return 0 if r["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
