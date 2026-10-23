r"""capturas_aspecto.py (X1, studies 5) - capturas automaticas de cada pantalla propia a 4:3, 8:7, 16:9, 16:10 y 21:9 y
comprobacion numerica de que el lienzo 4:3 no cambia (solo cambia la capa ancha).

  python tools\capturas_aspecto.py [--escenas menu,hub_lab,...] [--copias smoke_d,smoke_e] [--alto 720]

Para cada escena y cada proporcion lanza pruebas.py (ventana oculta, `--par ventana=WxH`) y compara cada captura con la
misma captura a 4:3: se recorta el lienzo 4:3 (centro de la imagen 16:9 del juego, como S::GameArea) de las dos, se
reescala al mismo tamano y se mide SSIM + pHash (tools\comparar.py). Una captura a 4:3 repetida da el suelo de ruido
(animaciones). FALLA si SSIM < max(SSIM_MIN, suelo - MARGEN). Salida: D:\DBZ3HD\_pruebas\aspecto_<fecha>\{tabla.txt,resultado.json,hojas\<escena>_<captura>.png} (hoja = las 5 lado a lado).
"""
import json
import re
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PIL import Image

AQUI = Path(__file__).resolve().parent
sys.path.insert(0, str(AQUI))
import pruebas as P  # noqa: E402
import comparar  # noqa: E402

# Tamanos de ventana (ancho multiplo de 64: con otros anchos la captura del juego (dbz3_shot) no responde, [H] fallo de
# la ruta de captura del presentador con RowPitch != ancho*4; el juego en si pinta bien). Cuatro son exactos.
PROPORCIONES = {"4:3": (960, 720), "8:7": (832, 728), "16:9": (1280, 720), "16:10": (1152, 720), "21:9": (1664, 714)}
SSIM_MIN, MARGEN = 0.80, 0.08
OCULTO = False   # --oculto: sin ventana; las escenas 3D no responden a la captura en oculto (medido 22/10), por defecto visible detras
# X1: escenas 3D: el mundo cambia con la proporcion por diseno; solo se comparan las zonas de HUD (fracciones del lienzo
# 4:3: x0, y0, x1, y1). TIME arriba a la derecha; cara y barras abajo a la izquierda.
MASCARAS = {"iw_mini": [(0.62, 0.0, 1.0, 0.16), (0.0, 0.78, 0.45, 1.0)],
            "satan": [(0.62, 0.0, 1.0, 0.16), (0.0, 0.78, 0.45, 1.0)]}

# nombre: (escenario de pruebas.py, par, instantanea)
ESCENAS = {
    "menu": ("menu", {}, None),
    "hub_filas": ("hub_filas", {}, None),
    "hub_lab": ("hub_lab", {}, None),
    "online": ("online_crear", {}, None),
    "tagteam": ("tag_swap", {"rafaga": "1"}, "freezer_cap2"),
    "heroe": ("heroe_tecnicas", {}, None),
    "du_mapa": ("du_mapa", {}, "freezer_cap2"),
    "b2_tablero": ("escena", {"escena": "b2:fase1", "quieto": "3"}, None),
    "iw_mini": ("escena", {"escena": "iw:mini:0x7D9", "quieto": "2"}, None),
    "satan": ("escena", {"escena": "satan:ronda1", "quieto": "2", "min_lum": "12"}, None),
}


def tam(r, alto):
    w, h = PROPORCIONES[r]
    k = alto / 720
    return round(w * k / 64) * 64, round(h * k)


def lienzo(im):
    """Rect (x0, y0, x1, y1) del lienzo 4:3 en una captura de ventana completa: imagen 16:9 del juego centrada (con
    barras si la ventana no es 16:9) y, dentro, el 4:3 de ese alto (S::GameArea)."""
    W, H = im.size
    iw, ih = (W, round(W * 9 / 16)) if W / H < 16 / 9 else (round(H * 16 / 9), H)
    ix, iy = (W - iw) // 2, (H - ih) // 2
    cw = min(iw, round(ih * 4 / 3))
    ch = round(cw * 3 / 4) if cw < round(ih * 4 / 3) else ih
    x0, y0 = ix + (iw - cw) // 2, iy + (ih - ch) // 2
    return (x0, y0, x0 + cw, y0 + ch)


def una_corrida(copia, escena, ratio, alto, tag):
    esc, par, inst = ESCENAS[escena]
    w, h = tam(ratio, alto)
    par = dict(par, ventana=f"{w}x{h}")
    try:
        r = P.correr(copia, esc, ["DBZ3_TEST_OCULTO=1"] if OCULTO else [], inst, par)
    except (Exception, SystemExit) as e:
        return {"ok": False, "msg": str(e), "run": None}
    return {"ok": r["ok"], "msg": r["msg"], "run": r["run"]}


def _mascara(im, zonas):
    """Junta las zonas de HUD (fracciones del lienzo) en una tira horizontal del mismo alto."""
    w, h = im.size
    cortes = [im.crop((round(z[0] * w), round(z[1] * h), round(z[2] * w), round(z[3] * h))).resize((320, 120)) for z in zonas]
    tira = Image.new("RGB", (320 * len(cortes), 120))
    for i, c in enumerate(cortes):
        tira.paste(c, (320 * i, 0))
    return tira


def comparar_capturas(ref, cand, nombre, zonas=None):
    a, b = Image.open(ref).convert("RGB"), Image.open(cand).convert("RGB")
    ca, cb = a.crop(lienzo(a)), b.crop(lienzo(b))
    cb = cb.resize(ca.size)
    if zonas:
        ca, cb = _mascara(ca, zonas), _mascara(cb, zonas)
    tmp = Path(cand).with_suffix(".lienzo_ref.png")
    ca.save(tmp)
    tmp2 = Path(cand).with_suffix(".lienzo.png")
    cb.save(tmp2)
    m = comparar.metricas(str(tmp), str(tmp2))
    return m


def main(a):
    escenas = a[a.index("--escenas") + 1].split(",") if "--escenas" in a else list(ESCENAS)
    copias = a[a.index("--copias") + 1].split(",") if "--copias" in a else ["smoke_d", "smoke_e"]
    global OCULTO
    OCULTO = "--oculto" in a
    alto = int(a[a.index("--alto") + 1]) if "--alto" in a else 720
    if {"smoke_a", "smoke_b", "smoke_c"} & set(copias):
        raise SystemExit("smoke_a/b/c son de `pruebas.py todo`: usa smoke_d/e/f u otras copias para la matriz de proporciones")
    salida = P.PRUEBAS / f"aspecto_{time.strftime('%Y%m%d-%H%M%S')}"
    (salida / "hojas").mkdir(parents=True, exist_ok=True)
    trabajos = [(e, r) for e in escenas for r in list(PROPORCIONES) + ["4:3 (2)"]]
    import queue
    libres = queue.Queue()
    for c in copias:
        libres.put(c)

    def ejecutar(t):
        e, r = t
        c = libres.get()
        try:
            return t, una_corrida(c, e, r.split(" ")[0], alto, r)
        finally:
            libres.put(c)

    with ThreadPoolExecutor(len(copias)) as ex:
        res = dict(ex.map(ejecutar, trabajos))
    filas, json_out, malos = [], [], 0
    for e in escenas:
        ref = res[(e, "4:3")]
        ref2 = res[(e, "4:3 (2)")]
        if not ref["run"] or not ref2["run"]:
            filas.append(f"{e:<11} sin referencia 4:3: {ref['msg'] or ref2['msg']}")
            malos += 1
            continue
        nombres = sorted(p.stem for p in Path(ref["run"]).glob("*.png") if not p.stem.startswith("zz")
                         and "lienzo" not in p.stem and not re.search(r"_t\d+$", p.stem))
        for n in nombres:
            if not (Path(ref2["run"]) / f"{n}.png").exists():
                continue
            suelo = comparar_capturas(Path(ref["run"]) / f"{n}.png", Path(ref2["run"]) / f"{n}.png", n, MASCARAS.get(e))["ssim"]
            hoja = []
            for r in PROPORCIONES:
                rr = res[(e, r)]
                cand = Path(rr["run"]) / f"{n}.png" if rr["run"] else None
                if not cand or not cand.exists():
                    filas.append(f"{e:<11} {n:<22} {r:<6} FALTA captura ({rr['msg'][:60]})")
                    malos += 1
                    continue
                hoja.append(Image.open(cand).convert("RGB"))
                if r == "4:3":
                    continue
                m = comparar_capturas(Path(ref["run"]) / f"{n}.png", cand, n, MASCARAS.get(e))
                lim = min(SSIM_MIN, suelo - MARGEN)   # pantallas animadas: suelo bajo -> limite bajo
                ok = m["ssim"] >= lim
                malos += not ok
                filas.append(f"{'OK  ' if ok else 'FAIL'} {e:<11} {n:<22} {r:<6} ssim {m['ssim']:.3f} (limite {lim:.2f}, suelo "
                             f"{suelo:.3f}) phash {m['phash_hamming']:>2}")
                json_out.append({"escena": e, "captura": n, "ratio": r, "ok": ok, **m, "suelo": suelo})
            if hoja:
                ancho = sum(i.width * 360 // i.height for i in hoja)
                lienzo_h = Image.new("RGB", (ancho, 360))
                x = 0
                for i in hoja:
                    j = i.resize((i.width * 360 // i.height, 360))
                    lienzo_h.paste(j, (x, 0))
                    x += j.width
                lienzo_h.save(salida / "hojas" / f"{e}_{n}.png")
    (salida / "tabla.txt").write_text("\n".join(filas) + "\n", encoding="utf-8")
    (salida / "resultado.json").write_text(json.dumps(json_out, indent=1), encoding="utf-8")
    print("\n".join(filas))
    print(f"\n{len(json_out) - malos}/{len(json_out)} OK; salida {salida}")
    return 1 if malos else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
