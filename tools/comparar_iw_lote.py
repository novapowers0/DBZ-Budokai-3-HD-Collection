r"""comparar_iw_lote.py (B4, studies 5) - original (iw_original_lote.py) contra port (linea de tiempo de pruebas.py) por minijuego.

  python tools\comparar_iw_lote.py <orig_dir_capturas> <port_res.json> <salida_dir> [claves,...]

Por minijuego: de todas las parejas (captura del original, captura del port) se descartan las lisas (negro/blanco), se
ordenan por distancia pHash y las 4 mejores pasan por comparar.metricas (SSIM tras alinear + pHash). Nota = la mejor.
Escribe <salida_dir>\lado_<clave>.png (original | port | mapa de diferencias) con la mejor pareja y resultados.json.
Verde = SSIM >= 0.45 y pHash <= 18 (liston bajo a proposito: modelos y campos de PS2 contra HD nunca dan SSIM alto).
"""
import glob
import json
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import comparar as C  # noqa: E402

SSIM_MIN, PHASH_MAX = 0.45, 18


def lisa(im):
    g = np.asarray(im.convert("L").resize((64, 48)), dtype=float)
    return g.std() < 8


def main(a):
    orig_dir, res_json, salida = a[0], json.load(open(a[1])), Path(a[2])
    salida.mkdir(parents=True, exist_ok=True)
    claves = a[3].split(",") if len(a) > 3 else sorted(res_json)
    out = {}
    for c in claves:
        o = sorted(glob.glob(os.path.join(orig_dir, "b4_0x%s_*.png" % c)))
        p = sorted(glob.glob(os.path.join(res_json[c]["dir"], "t*.png")))
        oi = [(f, Image.open(f).convert("RGB")) for f in o]
        pi = [(f, Image.open(f).convert("RGB")) for f in p]
        oi = [(f, i, C.phash(i)) for f, i in oi if not lisa(i)]
        pi = [(f, i, C.phash(i)) for f, i in pi if not lisa(i)]
        pares = sorted(((C.hamming(ho, hp), fo, fp) for fo, _, ho in oi for fp, _, hp in pi))[:4]
        mejor = None
        for h, fo, fp in pares:
            m = C.metricas(fo, fp)
            if mejor is None or m["ssim"] > mejor[0]["ssim"]:
                mejor = (m, fo, fp)
        if not mejor:
            out[c] = {"ok": None, "detalle": "sin parejas utiles (capturas lisas o ausentes)", "n_orig": len(o), "n_port": len(p)}
            continue
        m, fo, fp = mejor
        C.comparar(fo, fp, str(salida / ("lado_" + c)))
        out[c] = {"ok": m["ssim"] >= SSIM_MIN and m["phash_hamming"] <= PHASH_MAX, "ssim": m["ssim"],
                  "phash": m["phash_hamming"], "alineado": m["alineado"], "orig": os.path.basename(fo),
                  "port": os.path.basename(fp), "n_orig": len(o), "n_port": len(p)}
        print(c, out[c], flush=True)
    (salida / "resultados.json").write_text(json.dumps(out, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main(sys.argv[1:])
