#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""xv2_razas.py - banco sin juego de HER09: cada raza del creador XV2 medida contra
los datos reales de XV2 (EMD/ESK/EMB del CPK instalado) con el mismo encuadre.

Para cada raza compone el bin B3 (xv2_cac_b3) en dos lineas base y mide:
  * geometria: relacion pierna/torso, cabeza/altura, brazo/altura y hombro/altura,
    antes y despues de la calibracion `xv2_proporcion`;
  * forma: SSIM de la silueta y del color plano (sin rampa) contra un render del
    modelo XV2 original, con el MISMO proyector (model_render);
  * fisica: bloque XV5S emitido (las seis razas dan `off_no_SCD`: sin SCD propio).

Encender una raza = todas las puertas de `xv2_tolerancias.toml` en verde y mejora
de SSIM frente a la linea base. El resultado se escribe en `xv2_razas.json` (lo
lee el parche de runtime) y el informe en `xv2_medidas.md`.

No abre el juego ni PCSX2: lee el CPK de XV2 (solo lectura) y escribe temporales
en E:\\DBZ3HD_trabajo. `--selftest` no necesita XV2.
"""
import argparse
import hashlib
import json
import os
import sys
import tempfile
import tomllib
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(HERE), str(ROOT / "mod center hd" / "xv2"), str(ROOT / "mod center hd"), str(ROOT / "awo_tools")]

RAZAS = ("HUM", "HUF", "NMC", "FRI", "MAM", "MAF")
TRABAJO = Path(os.environ.get("DBZ3_C2_TRABAJO") or r"E:\DBZ3HD_trabajo\c2-xv2")
TOL = HERE / "xv2_tolerancias.toml"
GATE = HERE / "xv2_razas.json"
INFORME = HERE / "xv2_medidas.md"


# ---------------------------------------------------------------- utilidades
def tolerancias():
    with open(TOL, "rb") as fh:
        return tomllib.load(fh)


def _modelo_bin(ruta):
    import model_render as MR
    return MR.Model(str(ruta))


def _plano(model, activo=True):
    """Copia barata del modelo con la rampa apagada (albedo plano, comparable)."""
    mats = [dict(x, shader=0xFFFFFFFF, blend=False) for x in model.mats] if activo else model.mats
    return _Vista(model, mats)


class _Vista:
    def __init__(self, m, mats):
        self.__dict__.update({k: getattr(m, k) for k in ("pos", "nrm", "uv", "tris", "tri_mat", "tex", "vbone")})
        self.mats = mats
        self._m = m

    def posed(self, pose=None):
        return self._m.posed(pose)


def _render(model, size=720, view=np.eye(3), center=None, scale=None, outline=1.0):
    import model_render as MR
    P, _ = model.posed(None)
    if center is None or scale is None:
        lo, hi = P.min(0), P.max(0)
        center = (lo + hi) / 2
        scale = size * 0.90 / max(float(hi[1] - lo[1]), float(hi[0] - lo[0]), 1e-9)
    return MR.render(model, (size, size), view, center, scale, outline=outline)


def _fondo(img):
    from PIL import Image
    out = Image.new("RGB", (img.shape[1], img.shape[0]), (255, 255, 255))
    out.paste(Image.fromarray(img).convert("RGBA"), (0, 0), Image.fromarray(img).convert("RGBA"))
    return np.asarray(out)


def _ssim(a, b):
    from skimage.metrics import structural_similarity as ssim
    if a.ndim == 2:
        return float(ssim(a, b, data_range=1.0))
    return float(ssim(a, b, channel_axis=2, data_range=255.0))


# ---------------------------------------------------------------- referencia XV2
class Ref:
    """Malla XV2 agregada de las piezas del creador, con sus texturas reales."""

    def __init__(self, raza, tam=720):
        import xv2_cac_b3 as C
        import xeno_a_b3 as XA
        c = C.L.cpk_xv2()
        # Las MISMAS piezas que convierte el conversor (fuente_cac: reglas de ocultar del BCS, constitucion y cola en la pose de
        # reposo), pero la malla ORIGINAL de XV2, sin pasar por el esqueleto de B3. Antes la referencia salia de otro lector
        # y le faltaban piezas (cuernos de FRI, pelo de MAF): la silueta fallaba por la referencia, no por el modelo.
        self.esk, piezas, _ = C.fuente_cac(c, _sel(raza), None, 0)
        pos, nrm, uv, tris, tm, tex = [], [], [], [], [], []
        off, mi = 0, 0
        for q in piezas:
            sm = q["sm"]
            tinte = q.get("tint")
            tinte = tinte(sm.name) if callable(tinte) else tinte
            ojo = "EYE" in q["material"]["shader"]
            img = np.asarray(XA.material_image(q["base"], q["dyt"], q["material"], 512, subtract_ramp=not ojo and tinte is not None,
                                               tint=tinte), np.float32)[..., :3] / 255.0
            if tinte is not None and not ojo:
                img = img * np.asarray(tinte, np.float32)
            tex.append(np.clip(img, 0, 1))
            p_, n_ = sm.pos, sm.nrm
            if q["attachment"]:
                Wm = self.esk.world_matrix[self.esk.index[q["attachment"]]]
                p_, n_ = p_ @ Wm[:3, :3].T + Wm[:3, 3], n_ @ Wm[:3, :3].T
            pos.append(p_)
            nrm.append(n_)
            uv.append(sm.uv - np.floor(sm.uv.mean(0)))
            tris.append(sm.tris + off)
            tm.append(np.full(len(sm.tris), mi))
            mi += 1
            off += len(sm.pos)
        class M:
            pass
        m = M()
        m.pos, m.nrm, m.uv = np.vstack(pos), np.vstack(nrm), np.vstack(uv)
        m.tris, m.tri_mat = np.vstack(tris), np.concatenate(tm)
        m.tex = tex
        m.mats = [dict(tex=i, shader=0xFFFFFFFF, env=False, blend=False, diffuse=[1, 1, 1]) for i in range(len(tex))]
        m.vbone = np.zeros(len(m.pos), int)
        m.posed = lambda pose=None: (m.pos, m.nrm)
        self.model = m
        self.img = _render(m, tam)
        self.metricas = C_ratios_esk(self.esk)


def _sel(raza):
    """Seleccion del banco: piezas base de la raza y la constitucion neutra (la que elige el conversor por defecto)."""
    import xv2_cac_b3 as C
    import xeno_bcs as XB
    c = C.L.cpk_xv2()
    bcs = XB.leer_bcs(c.get(C.X._ci(c, "data/chara/%s/%s.bcs" % (raza, raza))))
    esk = C.xm.Esk(c.get(C.X._ci(c, "data/chara/%s/%s_000.esk" % (raza, raza))))
    alto = float(esk.world[esk.index["b_C_Head"]][1] - esk.world[esk.index["b_L_Foot"]][1])
    return dict(C.DEFECTO, raza=raza, ojos=1, tipo=C.tipo_neutro(bcs["bodies"], alto))


def C_ratios_esk(esk):
    import xv2_proporcion as P
    return P.ratios_esk(esk)


def _ratio_b3(model):
    """Mismos puntos en el esqueleto del bin B3 (bind)."""
    W = model.worlds(None)
    b = model.bone
    def y(s):
        return float(W[b(s)][1, 3])
    def x(s):
        return float(W[b(s)][0, 3])
    alto = y("HEAD") - min(y("LFOOT2"), y("RFOOT2"))
    pierna = y("LLEG1") - y("LFOOT1")
    torso = y("HEAD") - y("LLEG1")
    out = dict(alto=alto, pierna=pierna, torso=torso,
               cabeza=y("HEAD") - y("NECK"),
               hombro=abs(x("LCHN") - x("RCHN")),
               brazo=abs(x("LARM1") - x("LHANDROT")))
    out["pierna_torso"] = pierna / max(torso, 1e-9)
    out["cabeza_alto"] = out["cabeza"] / max(alto, 1e-9)
    out["brazo_alto"] = out["brazo"] / max(alto, 1e-9)
    out["hombro_alto"] = out["hombro"] / max(alto, 1e-9)
    return out


def _malla(pos):
    """Cocientes de la malla en T (comparables entre XV2 y B3, sin depender de nombres de hueso)."""
    lo, hi = np.asarray(pos).min(0), np.asarray(pos).max(0)
    alto, ancho = float(hi[1] - lo[1]), float(hi[0] - lo[0])
    top = np.asarray(pos)[np.asarray(pos)[:, 1] > hi[1] - 0.15 * alto]
    return dict(aspecto=ancho / max(alto, 1e-9),
                cabeza_ancho=float(top[:, 0].max() - top[:, 0].min()) / max(alto, 1e-9),
                alto=alto, ancho=ancho)


def _errores(ref, b3, ref_m, b3_m):
    out = {k: abs(b3[k] - ref[k]) / max(abs(ref[k]), 1e-12) for k in ("pierna_torso",)}
    out["aspecto"] = abs(b3_m["aspecto"] - ref_m["aspecto"]) / max(abs(ref_m["aspecto"]), 1e-9)
    out["cabeza_ancho"] = abs(b3_m["cabeza_ancho"] - ref_m["cabeza_ancho"]) / max(abs(ref_m["cabeza_ancho"]), 1e-9)
    out["forma_media"] = float(np.mean([out["pierna_torso"], out["aspecto"]]))
    return out


# ---------------------------------------------------------------- medida de una raza
def componer(raza, plantilla, destino, params=None):
    """Compone el bin: params None = linea base (esqueleto de Goku, sin calibrar); si no, el conversor tal cual
    (XV2RAZAS 12/11: esqueleto completo en proporcion XV2, `xv2_proporcion.esqueleto`)."""
    import xv2_cac_b3 as C
    sel = _sel(raza)
    orig, orig_p = C.ESQUELETO, C.proporciones_piernas
    C.ESQUELETO = params is not None
    C.proporciones_piernas = lambda *a: 1.0
    try:
        stats = C.componer_xeno(sel, str(plantilla), str(destino))
    finally:
        C.ESQUELETO, C.proporciones_piernas = orig, orig_p
    if params is not None and stats.get("esqueleto"):
        params.update(k=round(float(stats["esqueleto"]["k_pierna"]), 6), d_cadera=round(float(stats["esqueleto"]["cadera"]), 6))
    return stats


def medir(raza, plantilla, ref, params=None, etiqueta="base"):
    import xv2_proporcion as P
    destino = TRABAJO / "razas" / f"{raza}_{etiqueta}.bin"
    destino.parent.mkdir(parents=True, exist_ok=True)
    stats = componer(raza, plantilla, destino, params)
    m = _modelo_bin(destino)
    b3 = _ratio_b3(m)
    b3_m, ref_m = _malla(m.posed(None)[0]), _malla(ref.model.pos)
    err = _errores(ref.metricas, b3, ref_m, b3_m)
    img = _render(_plano(m), ref.img.shape[0])
    sil = (img[..., 3] > 127).astype(float)
    sil_ref = (ref.img[..., 3] > 127).astype(float)
    col = _fondo(img)
    col_ref = _fondo(ref.img)
    return dict(raza=raza, modo=etiqueta, params=params, bin=str(destino), bytes=destino.stat().st_size,
                geometria=dict(b3=b3, malla=b3_m, malla_ref=ref_m, error=err),
                ssim_silueta=_ssim(sil, sil_ref), ssim_color=_ssim(col, col_ref),
                fisica=stats.get("physics_runtime"), awg0=stats.get("awg0"))


def _params_calibrados(raza, plantilla, esk):
    return dict(k=1.0, escala=1.0, d_cadera=0.0)      # los rellena `componer` con lo que aplico el conversor


def _plantilla(tmp):
    import xv2_fuente as F
    return Path(F.plantilla_donante(0, None, str(tmp / "plantilla_goku.bin")))


def _evaluar(raza, plantilla, ref, esk):
    base = medir(raza, plantilla, ref, None, "base")
    params = _params_calibrados(raza, plantilla, esk)
    cal = medir(raza, plantilla, ref, params, "cal")
    cal["mejora_silueta"] = cal["ssim_silueta"] - base["ssim_silueta"]
    cal["mejora_color"] = cal["ssim_color"] - base["ssim_color"]
    return base, cal


# MAM ya visible desde C6. HUF/FRI/MAF (12/11): casi-fallos de la puerta de silueta/pierna
# (0.86-0.91 frente a 0.90; pierna MAF 0.053 frente a 0.05) y sin ellas el creador ofrecia la
# raza con el cuerpo de Goku. Cuerpo XV2 real con silueta algo distinta > Goku; se miden igual
# y las puertas siguen publicadas en el json. Las tolerancias NO se tocan.
LEGADO = set()      # XV2RAZAS 12/11: las seis pasan las puertas con el esqueleto en proporcion XV2; ya no hay excepcion


def _puertas(cal, tol, base=None):
    g, f = tol["geometria"], tol["forma"]
    fis = cal.get("fisica") or {}
    ok_fis = (fis.get("controller") == "off_no_SCD" or
              (fis.get("nodes", 0) == 0 and fis.get("vertices", 0) == 0))
    if tol["fisica"].get("exigir_bloque_si_hay_scd", True) and fis.get("nodes", 0):
        ok_fis = fis.get("controller", "").startswith("XV5S") and fis.get("bytes", 0) > 32
    return {
        "ssim_silueta": cal["ssim_silueta"] >= f["ssim_silueta_min"],
        "ssim_color": cal["ssim_color"] >= f["ssim_color_min"],
        "mejora": (base is None or cal["ssim_silueta"] >= base["ssim_silueta"] - f["ssim_regresion_max"]),
        "error_pierna": cal["geometria"]["error"]["pierna_torso"] <= g["error_pierna_max"],
        "error_forma": cal["geometria"]["error"]["forma_media"] <= g["error_forma_max"],
        "fisica": ok_fis,
    }


def correr(razas=RAZAS, refrescar=False):
    tol = tolerancias()
    TRABAJO.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix="xv2_razas_", dir=TRABAJO))
    plantilla = _plantilla(tmp)
    resultados = {}
    for raza in razas:
        ref = Ref(raza)
        base, cal = _evaluar(raza, plantilla, ref, ref.esk)
        puertas = _puertas(cal, tol, base)
        resultados[raza] = dict(base=base, calibrada=cal, puertas=puertas,
                                heredada=raza in LEGADO,
                                encendida=all(puertas.values()) or raza in LEGADO,
                                fisica=cal.get("fisica"))
        print("%s: silueta %.4f (base %.4f) color %.4f error pierna %.3f forma %.3f -> %s" % (
            raza, cal["ssim_silueta"], base["ssim_silueta"], cal["ssim_color"],
            cal["geometria"]["error"]["pierna_torso"], cal["geometria"]["error"]["forma_media"],
            "ON" if resultados[raza]["encendida"] else "OFF"))
    return resultados, tol


def publicar(resultados, tol):
    import xv2_proporcion as P
    razas = {}
    for raza, r in resultados.items():
        cal = r["calibrada"]
        fallos = P.motivos_fallidos(r["puertas"])
        razas[raza] = dict(encendida=bool(r["encendida"]), heredada=bool(r["heredada"]),
                           k=cal["params"]["k"], escala=1.0,
                           d_cadera=cal["params"]["d_cadera"],
                           ssim_silueta=round(cal["ssim_silueta"], 6),
                           ssim_color=round(cal["ssim_color"], 6),
                           error_pierna=round(cal["geometria"]["error"]["pierna_torso"], 6),
                           error_forma=round(cal["geometria"]["error"]["forma_media"], 6),
                           fisica=(r.get("fisica") or {}).get("controller"),
                           puertas=r["puertas"],
                           motivo=(None if r["encendida"] or not fallos else "; ".join(fallos)))
    doc = dict(version=1, fuente="datos XV2 + render offline (no captura de juego)",
               tolerancias=hashlib.sha256(TOL.read_bytes()).hexdigest()[:16], razas=razas)
    GATE.write_text(json.dumps(doc, indent=1, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    lineas = ["# HER09 - medida por raza (generado por xv2_razas.py)", "",
              "| raza | ON | SSIM silueta | base | SSIM color | err. pierna | err. forma | fisica |",
              "|---|---|---|---|---|---|---|---|"]
    for raza, r in resultados.items():
        c = r["calibrada"]
        lineas.append("| %s | %s | %.4f | %.4f | %.4f | %.3f | %.3f | %s |" % (
            raza, "SI" if r["encendida"] else "no", c["ssim_silueta"], r["base"]["ssim_silueta"],
            c["ssim_color"], c["geometria"]["error"]["pierna_torso"],
            c["geometria"]["error"]["forma_media"], (r.get("fisica") or {}).get("controller")))
    lineas += ["", "Tolerancias (hash %s): %s" % (doc["tolerancias"], TOL.name)]
    fuera = [(raza, v["motivo"]) for raza, v in sorted(razas.items()) if not v["encendida"]]
    if fuera:
        lineas += ["", "## Razas ocultas y por que (HEROE-06)", ""]
        lineas += ["- %s: %s" % (raza, motivo) for raza, motivo in fuera]
    INFORME.write_text("\n".join(lineas) + "\n", encoding="utf-8")
    return doc


def motivos(ruta=None):
    """HEROE-06: rellena `motivo` en el banco ya medido, sin volver a medir."""
    import xv2_proporcion as P
    ruta = Path(ruta) if ruta else GATE
    doc = json.loads(ruta.read_text(encoding="utf-8"))
    for fila in doc.get("razas", {}).values():
        fallos = P.motivos_fallidos(fila.get("puertas"))
        fila["motivo"] = None if fila.get("encendida") or not fallos else "; ".join(fallos)
    ruta.write_text(json.dumps(doc, indent=1, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    if INFORME.is_file():
        txt = INFORME.read_text(encoding="utf-8").split("\n## Razas ocultas")[0].rstrip() + "\n"
        fuera = [(r, v["motivo"]) for r, v in sorted(doc.get("razas", {}).items())
                 if not v.get("encendida")]
        if fuera:
            txt += "\n## Razas ocultas y por que (HEROE-06)\n\n"
            txt += "".join("- %s: %s\n" % (r, m) for r, m in fuera)
        INFORME.write_text(txt, encoding="utf-8")
    return doc


def laminas(razas=RAZAS, salida=None):
    """HEROE-06: lamina de contacto XV2 (referencia real) contra el bin B3, con su cifra.
    No abre el juego; usa el render offline del banco. Una fila por raza.
    Se escribe FUERA del repo (arte de terceros: nunca al repo ni al zip)."""
    from PIL import Image, ImageDraw
    salida = Path(salida) if salida else TRABAJO / "xv2_laminas.png"
    tol = tolerancias()
    TRABAJO.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix="xv2_laminas_", dir=TRABAJO))
    plantilla = _plantilla(tmp)
    celda = 320
    import model_render as mr
    filas = []
    for raza in razas:
        ref = Ref(raza)
        params = _params_calibrados(raza, plantilla, ref.esk)
        cal = medir(raza, plantilla, ref, params, "cal")
        puertas = _puertas(cal, tol)
        # La lamina muestra el bin CON sus texturas y rampa (el `plano` del banco
        # apaga la rampa y sale en blanco: solo vale para la mascara).
        m_b3 = _modelo_bin(Path(cal["bin"]))
        img = _render(m_b3, celda)
        ref_img = np.asarray(Image.fromarray(ref.img).resize((celda, celda), Image.LANCZOS))
        img_lado = _render(m_b3, celda, view=mr.rot_y(90))
        ref_lado = _render(ref.model, celda, view=mr.rot_y(90))
        sil = (img[..., 3] > 127).astype(float)
        sil_ref = (ref_img[..., 3] > 127).astype(float)
        filas.append((raza, ref_img, img, cal, puertas,
                      _ssim(sil, sil_ref), _ssim(_fondo(img), _fondo(ref_img)),
                      ref_lado, img_lado))
    ancho, alto = celda * 2 + 24, (celda + 34) * len(filas) + 30
    hoja = Image.new("RGB", (ancho, alto), (250, 250, 250))
    dib = ImageDraw.Draw(hoja)
    dib.text((8, 8), "HEROE-06: XV2 real (izq) vs B3 con texturas (der). Cifra = banco: SSIM silueta / color", fill=(0, 0, 0))
    y = 30
    for raza, ref_img, img, cal, puertas, sil_v, col_v, _rl, _il in filas:
        hoja.paste(Image.fromarray(_fondo(ref_img).astype(np.uint8)), (8, y))
        hoja.paste(Image.fromarray(_fondo(img).astype(np.uint8)), (celda + 16, y))
        estado = "ON" if all(puertas.values()) else "OFF: " + ", ".join(
            k for k, v in sorted(puertas.items()) if not v)
        dib.text((8, y + celda + 4), "%s  banco sil %.3f col %.3f | lamina sil %.3f col %.3f  %s" % (
            raza, cal["ssim_silueta"], cal["ssim_color"], sil_v, col_v, estado), fill=(0, 0, 0))
        y += celda + 34
    hoja.save(salida)
    # lamina de LADO (la ficha pide frente y lado): mismas 6 razas, giro de 90 grados
    hoja = Image.new("RGB", (ancho, alto), (250, 250, 250))
    dib = ImageDraw.Draw(hoja)
    dib.text((8, 8), "HEROE-06: de lado. XV2 real (izq) vs B3 con texturas (der)", fill=(0, 0, 0))
    y = 30
    for raza, _ri, _ii, cal, puertas, _s, _c, ref_lado, img_lado in filas:
        hoja.paste(Image.fromarray(_fondo(ref_lado).astype(np.uint8)), (8, y))
        hoja.paste(Image.fromarray(_fondo(img_lado).astype(np.uint8)), (celda + 16, y))
        estado = "ON" if all(puertas.values()) else "OFF: " + ", ".join(
            k for k, v in sorted(puertas.items()) if not v)
        dib.text((8, y + celda + 4), "%s  banco sil %.3f col %.3f  %s" % (
            raza, cal["ssim_silueta"], cal["ssim_color"], estado), fill=(0, 0, 0))
        y += celda + 34
    lado = salida.with_name(salida.stem + "_lado" + salida.suffix)
    hoja.save(lado)
    return salida


def gates_activos():
    """Razas encendidas según el ultimo banco (vacío = ninguna: comportamiento actual)."""
    try:
        d = json.loads(GATE.read_text(encoding="utf-8"))
        return {r: v for r, v in d["razas"].items() if v.get("encendida")}
    except (OSError, ValueError, KeyError):
        return {}


def selftest():
    import xv2_proporcion as P
    P.selftest()
    # SSIM y puertas con datos inventados (sin XV2 ni archivos de juego).
    a = np.zeros((64, 64), float); a[8:56, 24:40] = 1
    assert abs(_ssim(a, a) - 1.0) < 1e-9
    b = a.copy(); b[8:56, 22:42] = 1
    assert 0 < _ssim(a, b) < 1
    tol = tolerancias()
    cal = dict(ssim_silueta=0.95, ssim_color=0.8, mejora_silueta=0.02,
               geometria=dict(error=dict(pierna_torso=0.01, forma_media=0.02)),
               fisica=dict(controller="off_no_SCD", nodes=0, vertices=0))
    assert all(_puertas(cal, tol).values())
    cal2 = dict(cal, ssim_silueta=0.5)
    assert not _puertas(cal2, tol)["ssim_silueta"]
    print("xv2_razas: OK (SSIM, puertas y tolerancias; sin XV2)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--todo", action="store_true")
    ap.add_argument("--raza", action="append", choices=RAZAS)
    ap.add_argument("--publicar", action="store_true", help="escribe xv2_razas.json e informe")
    ap.add_argument("--motivos", action="store_true", help="rellena el motivo de las razas ocultas sin medir")
    ap.add_argument("--laminas", action="store_true", help="lamina de contacto XV2 vs B3 (HEROE-06)")
    a = ap.parse_args(argv)
    if a.selftest:
        selftest()
        return
    if a.motivos:
        doc = motivos()
        print("motivos:", sum(1 for v in doc["razas"].values() if v.get("motivo")), "razas ocultas con motivo")
        return
    if a.laminas:
        ruta = laminas(tuple(a.raza) if a.raza else RAZAS)
        print("lamina escrita:", ruta)
        return
    razas = tuple(a.raza) if a.raza else RAZAS
    resultados, tol = correr(razas)
    if a.publicar:
        doc = publicar(resultados, tol)
        print("escrito", GATE, "y", INFORME, ":", sum(1 for r in doc["razas"].values() if r["encendida"]), "razas ON")


if __name__ == "__main__":
    main()
