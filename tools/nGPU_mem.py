"""nGPU: estancia larga en una escena y tendencia de memoria (RAM/VRAM) de la linea `perf`.

No cambia nada del producto: solo lanza una escena con el arnes de pruebas (respeta las plazas)
y lee `mem=`/`priv=`/`vram=` de la linea `perf` cada 5 s.

  python tools/nGPU_mem.py --escena b2:tablero --seg 420
  python tools/nGPU_mem.py --escena b2:fase9 --seg 420 --etiqueta b2_fase9
  python tools/nGPU_mem.py --escena menu --seg 300 --copia smoke_a

La escena se salta con el mismo gancho DBZ3_TEST_ESCENA del arnes (escena de pruebas.py). El
resultado dice si la RAM o la VRAM crecen sin volver (fuga) o se quedan en una meseta.
"""
import argparse
import json
import os
import re
import time
from pathlib import Path

import pruebas as P

PERF = re.compile(
    r"perf fps=(?P<fps>[\d.]+).*?frames=(?P<frames>\d+).*?max_frame_ms=(?P<max_ms>[\d.]+).*?"
    r"cfg=scale:(?P<scx>\S+)x(?P<scy>\S+).*?msaa:(?P<msaa>\S+).*?hdtex:(?P<hdtex>\S+).*?"
    r"vram=(?P<vram>\d+)MB/(?P<vramb>\d+)MB.*?sres=(?P<sres>\d+)MB.*?edram=(?P<edram>\d+)MB.*?"
    r"gpu_wait=(?P<gpu>[\d.]+)ms/f.*?cp_wait=(?P<cp>[\d.]+)ms/f.*?"
    r"mem=(?P<mem>\d+)MB priv=(?P<priv>\d+)MB packram=(?P<packram>\d+)MB avail=(?P<avail>\d+)MB")
PRES = re.compile(r"perf fps=[\d.]+ frames=\d+.*?cap=\d+ pres_ms=(?P<pres>[\d.]+) pres_n=(?P<presn>\d+)")


def _estancia(j, par):
    spec = par["escena"]
    _env, rx, aviso = P.resolver_escena(spec)
    if aviso:
        j.info(aviso)
    if rx:
        j.log(rx, float(par.get("espera", "60")))
    j.info(f"escena '{spec}' puesta; estancia de {par.get('seg', '300')} s")
    P.dormir(float(par.get("seg", "300")))
    j.captura("fin")


def _fila(linea):
    d = {}
    m = PERF.search(linea)
    if m:
        for k, v in m.groupdict().items():
            if v is None:
                continue
            try:
                d[k] = float(v)
            except ValueError:
                d[k] = v
    p = PRES.search(linea)
    if p:
        d["pres_ms"] = float(p.group("pres"))
    return d


def _resumen(filas, campo):
    v = [f[campo] for f in filas if isinstance(f.get(campo), (int, float))]
    if not v:
        return None
    # Meseta = mediana de la segunda mitad; el pico y el crecimiento importan para una fuga.
    mitad = v[len(v) // 2:]
    mitad.sort()
    meseta = mitad[len(mitad) // 2]
    return {"min": min(v), "max": max(v), "primero": v[0], "ultimo": v[-1],
            "meseta": meseta, "crecimiento_mb": max(v) - v[0], "n": len(v)}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--escena", default="")
    ap.add_argument("--escenario", default="", help="escenario de pruebas.py (p. ej. r2_fase1)")
    ap.add_argument("--etiqueta", default="")
    ap.add_argument("--seg", default="300")
    ap.add_argument("--copia", default="smoke_a")
    ap.add_argument("--instantanea", default=None)
    ap.add_argument("--ps2", default="", help="carpeta de fuentes PS2 (con .ruta/ISO) para DBZ3_PS2_GAMES")
    ap.add_argument("--cfg", default="", help="cvars extra para DBZ3_TEST_CFG (cvar=valor;...); los textos entre comillas")
    a = ap.parse_args()

    os.environ.update(DBZ3_AGENTE="nGPU", PYTHONUTF8="1")
    os.environ["DBZ3_PRUEBAS_LENTO"] = "1"
    P.OVERRIDES["dbz3_perf_logging"] = "true"
    P.OVERRIDES["dbz3_skip_launcher"] = "true"
    if a.cfg:
        os.environ["DBZ3_TEST_CFG"] = a.cfg

    if a.escenario:
        clave = a.escenario
        par = {}
    else:
        if not a.escena:
            raise SystemExit("falta --escena o --escenario")
        env, _rx, _aviso = P.resolver_escena(a.escena)
        if a.ps2:
            env = dict(env, DBZ3_PS2_GAMES=a.ps2)
        P.ESCENARIOS["ngpu_estancia"] = ("nGPU: estancia larga en una escena",
                                         lambda p: dict(env), _estancia)
        clave = "ngpu_estancia"
        par = {"escena": a.escena, "seg": a.seg}
    r = P.correr(a.copia, clave, [], a.instantanea, par, True)
    etiqueta = a.etiqueta or a.escenario or a.escena

    filas = []
    if r.get("run"):
        for _sl, linea in P._lineas_log(r["run"]):
            d = _fila(linea)
            if d:
                filas.append(d)
    campos = ("mem", "priv", "vram", "sres", "packram", "avail", "gpu", "cp", "pres_ms")
    res = {"etiqueta": etiqueta, "ok": bool(r.get("ok")), "msg": r.get("msg"),
           "escena": a.escena or a.escenario, "seg": a.seg, "copia": a.copia, "n_ventanas": len(filas),
           "resumen": {c: _resumen(filas, c) for c in campos},
           "ventanas": filas,
           "log": str(r.get("run") / "dbz3.log") if r.get("run") else None}
    out = Path("D:/tmp/nGPU")
    out.mkdir(parents=True, exist_ok=True)
    f = out / f"{time.strftime('%Y%m%d-%H%M%S')}_{res['etiqueta'].replace(':', '_')}.json"
    f.write_text(json.dumps(res, indent=1, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({k: v for k, v in res.items() if k != "ventanas"}, ensure_ascii=False), flush=True)
    print(f"json: {f}", flush=True)
    return 0 if res["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
