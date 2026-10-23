#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""color_vista.py - Motor de la vista previa y los nombres de region del editor de colores del select (E1, studies 5).

  * ficha(cid): regiones de color de un personaje del juego con nombre legible (Piel, Pelo, Ropa 1...), sin las rampas
    toon ni las texturas que no se ven. El nombre sale de medir cuanto del cuerpo cubre cada textura y a que altura
    (se pinta de magenta y se renderiza, frontal y lateral): una vez por personaje, luego queda en la cache.
  * vista(cid, regiones, salida): PNG RGBA 512x640 del cuerpo recoloreado con el sombreado toon del juego.
Los llama recolorear.py (subcomandos ficha / vista); el juego los lanza en segundo plano desde el select.
"""
import io
import json
import os
import sys
import time

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import colores  # noqa: E402
import model_render as mr  # noqa: E402
import roster_build as rb  # noqa: E402
from texture_b3 import encode_dxt3  # noqa: E402

W, H = 512, 640
_AFS = None


MODS = None   # carpeta mods/ (la pone recolorear.py): para los personajes de mod (ports/importados), por su ID


def _nativo(cid):
    ms = rb.IDS.get(cid, {}).get("models") or []
    return bool(ms) and ms[0] != 0xFFFFFFFF


def fuente(cid):
    """Personaje de mod con ese ID: {carpeta, ficheros del traje 1 (uno por forma), mtime}; None si es del juego."""
    if _nativo(cid):
        return None
    import tomllib  # noqa: PLC0415
    mods = MODS or rb.default_mods()
    with open(os.path.join(mods, "roster_ids.toml"), "rb") as fh:
        ids = tomllib.load(fh).get("ids", {})
    carpeta = next((k for k, v in ids.items() if v == cid), None)
    if carpeta is None:
        raise SystemExit("el ID %d no es un personaje del juego ni de un mod montado" % cid)
    with open(os.path.join(mods, carpeta, "personaje.toml"), "rb") as fh:
        pc = tomllib.load(fh).get("personaje", {})
    per = max(1, int(pc.get("modelos_por_traje", 1)))
    files = [os.path.join(mods, carpeta, m) for m in pc.get("modelos", [])[:per]]
    if not files:
        raise SystemExit("%s no tiene modelos" % carpeta)
    return {"carpeta": carpeta, "ficheros": files, "mtime": max(os.path.getmtime(f) for f in files),
            "nombre": str(pc.get("nombre", carpeta))}


def bin_nativo(cid):
    """Modelo HD de la forma 0 del traje 1: del juego (data_cmn) o del mod del personaje."""
    global _AFS
    src = fuente(cid)
    if src:
        with open(src["ficheros"][0], "rb") as fh:
            return rb.b3_ramps(rb.to_hd(fh.read(), rb.work_dir("cv_")), src["carpeta"])
    if _AFS is None:
        _AFS = rb.Afs(os.path.join(rb.default_us(), "data_cmn.afs"), rb.work_dir("cv_"))
    return _AFS.entry(rb.IDS[cid]["models"][0])


# ------------------------------------------------------------------ nombres de region
def _render_view(data, yaw, size=384):
    m = mr.Model(data)
    P, _ = m.posed()
    lo, hi = P.min(0), P.max(0)
    sc = size * 0.9 / max(hi[1] - lo[1], hi[0] - lo[0], 1e-6)
    return mr.render(m, (size, size), mr.rot_y(yaw), (lo + hi) / 2, sc)


def _mide(base, mod):
    """Cobertura = pixeles del cuerpo que PASAN a magenta respecto al render original (una rampa pintada no da magenta)."""
    body = base[..., 3] > 0
    ys = np.nonzero(body)[0]
    m = mod[..., :3].astype(int)
    ch = (np.abs(base[..., :3].astype(int) - m).sum(-1) > 40) & body
    mag = ch & (m[..., 1] <= 10) & (m[..., 0] + m[..., 2] >= 30)
    if not mag.any():
        return 0.0, None
    yy = np.nonzero(mag)[0]
    return 100.0 * mag.sum() / body.sum(), float((yy.mean() - ys.min()) / max(1, ys.max() - ys.min()))


def _pinta(b, o, dl):
    dds = bytes(b[o:o + dl])
    img = np.array(Image.open(io.BytesIO(dds)).convert("RGBA"))
    img[..., 0], img[..., 1], img[..., 2] = 255, 0, 255
    out = bytearray(b)
    out[o:o + dl] = dds[:128] + encode_dxt3(img)
    return bytes(out)


def nombrar(regiones, medidas):
    """regiones: [{textura, ancho, alto, color}]; medidas: {textura: (pct, altura media 0 arriba..1 abajo)}.
    -> {textura: nombre}, None = descartar (rampa toon u oculta). El nombre es la ZONA del cuerpo que cubre la textura
    (medida sobre renders: 92 % de acierto en 276 regiones de 38 personajes etiquetadas mirando la imagen; separar
    piel de ropa por el color no es fiable porque el color final sale de textura - rampa): Cabeza, Torso y brazos,
    Piernas, Pies y botas; las repetidas van numeradas de arriba abajo. La vista previa ilumina la region elegida."""
    zonas = ("Cabeza", "Torso y brazos", "Piernas", "Pies y botas")
    z, out = {}, {}
    for r in regiones:
        pct, alt = medidas.get(r["textura"], (0.0, None))
        if pct < 0.05 or alt is None:
            out[r["textura"]] = None
        else:
            z[r["textura"]] = (0 if alt < 0.17 else 1 if alt < 0.5 else 2 if alt < 0.8 else 3, alt, pct)
    for k, nom in enumerate(zonas):
        mis = sorted((t for t in z if z[t][0] == k), key=lambda t: z[t][1])
        mayor = max(mis, key=lambda t: z[t][2]) if mis else None    # la mas grande del torso/piernas es la ropa (83 %)
        resto = [t for t in mis if t != mayor or k not in (1, 2)]
        for n, t in enumerate(resto, 1):
            out[t] = nom if len(resto) == 1 and k not in (1, 2) else "%s %d" % (nom, n) if len(resto) > 1 else nom
        if k in (1, 2) and mayor is not None:
            out[mayor] = "Ropa superior" if k == 1 else "Ropa inferior"
    return out


def ficha(cid, cache_dir):
    """JSON {personaje, regiones:[{textura, nombre, color}]} (cache en cache_dir/ficha_<cid>.json; los de mod se
    rehacen si cambia su carpeta o su modelo). Deja tambien res_<cid>_<textura>.png: el cuerpo con esa region en
    magenta, para iluminarla en la vista previa al seleccionarla."""
    src = fuente(cid)
    meta = {"v": 3, **({"carpeta": src["carpeta"], "mtime": src["mtime"]} if src else {})}   # v3: nombres por zona y tamano
    f = os.path.join(cache_dir, "ficha_%d.json" % cid)
    if os.path.isfile(f):
        res = json.load(open(f, encoding="utf-8"))
        if res.get("meta", {}) == meta and all(os.path.isfile(os.path.join(cache_dir, "res_%d_%d.png" % (cid, r["textura"])))
                                              for r in res["regiones"]):
            return res
        if res.get("meta", {}) == meta:
            _resaltados(cid, res, cache_dir)
            return res
    b = bin_nativo(cid)
    info = colores.regiones_info(b)
    tex = colores._azt_texturas(b)
    base_f, base_s = _render_view(b, 0), _render_view(b, 90)
    med = {}
    for r, (o, dl) in zip(info, tex):
        mod = _pinta(b, o, dl)
        pf, af = _mide(base_f, _render_view(mod, 0))
        ps, as_ = _mide(base_s, _render_view(mod, 90))
        med[r["textura"]] = (pf, af) if pf >= ps else (ps, as_)
    nom = nombrar(info, med)
    regs = [{"textura": r["textura"], "nombre": nom[r["textura"]], "color": r["color"], "pct": round(med[r["textura"]][0], 2),
             "alt": round(med[r["textura"]][1], 3)} for r in info if nom[r["textura"]]]
    regs.sort(key=lambda r: med[r["textura"]][1])      # de arriba abajo
    res = {"personaje": src["nombre"] if src else rb.IDS[cid]["name"], "id": cid, "meta": meta, "regiones": regs}
    os.makedirs(cache_dir, exist_ok=True)
    json.dump(res, open(f, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    _resaltados(cid, res, cache_dir)
    return res


def _resaltados(cid, res, cache_dir):
    b = bin_nativo(cid)
    tex = colores._azt_texturas(b)
    for r in res["regiones"]:
        o, dl = tex[r["textura"]]
        img = _render_cuerpo(mr.Model(_pinta(b, o, dl)), 2)
        Image.fromarray(img, "RGBA").save(os.path.join(cache_dir, "res_%d_%d.png" % (cid, r["textura"])))


# ------------------------------------------------------------------ vista previa
def _render_cuerpo(m, ss):
    view = mr.rot_x(mr.PORTRAIT_PITCH) @ mr.rot_y(mr.PORTRAIT_YAW)
    pose = m.arms_down()
    pose.update(m.mouth_closed())
    P, _ = m.posed(pose)
    C = P @ view.T
    lo, hi = C.min(0), C.max(0)
    ctr = view.T @ ((lo + hi) / 2)
    sc = min(0.9 * W / max(hi[0] - lo[0], 1e-6), 0.9 * H / max(hi[1] - lo[1], 1e-6))
    return mr.render(m, (W, H), view, ctr, sc, pose=pose, ss=ss, outline=1.3, light=mr.PORTRAIT_LIGHT)


def vista(cid, regiones, salida, ss=2):
    """PNG RGBA 512x640 del personaje con las regiones recoloreadas. -> segundos."""
    t0 = time.perf_counter()
    b = bin_nativo(cid)
    hd = colores.recolor_regiones(b, regiones)[0] if regiones else b
    img = _render_cuerpo(mr.Model(hd), ss)
    os.makedirs(os.path.dirname(salida) or ".", exist_ok=True)
    Image.fromarray(img, "RGBA").save(salida)
    return time.perf_counter() - t0


def _prueba():
    r = [{"textura": t, "ancho": 128, "alto": 128} for t in (0, 1, 2, 3, 5, 6, 7, 8)]
    n = nombrar(r, {0: (10.0, 0.1), 1: (0.0, None), 2: (9.0, 0.27), 3: (20.0, 0.3), 5: (30.0, 0.5), 6: (8.0, 0.65), 7: (6.0, 0.9), 8: (4.0, 0.7)})
    assert n[3] == "Ropa superior" and n[2] == "Torso y brazos" and n[5] == "Ropa inferior" and n[1] is None and n[7] == "Pies y botas", n
    print("color_vista: prueba OK")


if __name__ == "__main__":
    _prueba()
