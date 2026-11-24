"""Compara fichas B2 en capturas, con regiones y apoyos revisados explícitamente.

Uso: python tools/b2_comparar_figuras.py medidas.json --salida carpeta_nueva
Las coordenadas son píxeles de la captura ORIGINAL, rectángulos semiabiertos.
No intenta reconocer personajes: una caja manual no demuestra identidad de modelo.
Normaliza por el rectángulo útil a 640x480 antes de comparar alturas. Genera
también el montaje de ambas capturas completas a escala EXACTA 0.5.
Salida: 0 alturas/apoyos comparables dentro del margen; 1 diferencias; 2 pendiente.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw


def identidad(path):
    return {"ruta": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def rectangulo(v, size):
    if len(v) != 4 or any(not isinstance(x, (int, float)) or not math.isfinite(x) for x in v):
        raise ValueError("Rectángulo inválido")
    x0, y0, x1, y1 = v
    if not (0 <= x0 < x1 <= size[0] and 0 <= y0 < y1 <= size[1]):
        raise ValueError(f"Rectángulo fuera de imagen {size}: {v}")
    return v


def punto_normalizado(p, vp):
    return [(p[0] - vp[0]) * 640 / (vp[2] - vp[0]),
            (p[1] - vp[1]) * 480 / (vp[3] - vp[1])]


def medir(entrada, size, viewport):
    if entrada is None or entrada.get("oculta") or entrada.get("caja") is None:
        return None
    caja = rectangulo(entrada["caja"], size)
    if not (viewport[0] <= caja[0] < caja[2] <= viewport[2] and
            viewport[1] <= caja[1] < caja[3] <= viewport[3]):
        raise ValueError("La caja de la figura sale del rectángulo útil")
    base = entrada.get("base")
    if base is not None:
        if len(base) != 2 or any(not isinstance(x, (int, float)) or not math.isfinite(x) for x in base):
            raise ValueError("Apoyo inválido")
        if not (viewport[0] <= base[0] < viewport[2] and viewport[1] <= base[1] < viewport[3]):
            raise ValueError("El apoyo sale del rectángulo útil")
    return {"caja_px": caja, "altura_px": caja[3] - caja[1],
            "altura_640x480": (caja[3] - caja[1]) * 480 / (viewport[3] - viewport[1]),
            "base_px": base, "base_640x480": punto_normalizado(base, viewport) if base else None,
            "metodo": entrada.get("metodo", "caja y apoyo anotados manualmente sobre captura")}


def evaluar(figura, imagenes, vps, comparable, tolerancia, base_max):
    medidas = {k: medir(figura.get(k), imagenes[k].size, vps[k]) for k in imagenes}
    r = {"id": figura["id"], **medidas, "estado": "PENDIENTE"}
    if not all(medidas.values()):
        r["motivo"] = "figura ausente, oculta o sin caja revisada"
        return r
    n, o = medidas["nativo"], medidas["nuestro"]
    r["error_altura_pct"] = 100 * (o["altura_640x480"] / n["altura_640x480"] - 1)
    r["altura_ok"] = abs(r["error_altura_pct"]) <= tolerancia + 1e-9
    if n["base_640x480"] is not None and o["base_640x480"] is not None:
        r["delta_base_640x480"] = [o["base_640x480"][i] - n["base_640x480"][i] for i in (0, 1)]
        r["distancia_base_640x480"] = math.dist(n["base_640x480"], o["base_640x480"])
    if not comparable:
        r["motivo"] = "estado, posición o encuadre no equivalentes; sólo diagnóstico"
    elif "distancia_base_640x480" not in r:
        r["motivo"] = "falta el apoyo revisado"
    else:
        r["base_ok"] = r["distancia_base_640x480"] <= base_max
        r["estado"] = "PASS" if r["altura_ok"] and r["base_ok"] else "FAIL"
    return r


def montaje(imagenes, titulos, path, escala=0.5):
    mini = [im.resize((round(im.width * escala), round(im.height * escala)), Image.Resampling.LANCZOS)
            for im in imagenes]
    out = Image.new("RGB", (sum(im.width for im in mini) + 12, max(im.height for im in mini) + 42), "#171b23")
    d = ImageDraw.Draw(out)
    x = 0
    for im, texto in zip(mini, titulos):
        out.paste(im, (x, 42))
        d.text((x + 5, 8), texto, fill="white")
        x += im.width + 12
    out.save(path)


def comparar(manifest, salida):
    datos = json.loads(manifest.read_text(encoding="utf-8-sig"))
    salida.mkdir(parents=True, exist_ok=False)  # no pisa capturas ni informes anteriores
    informe = {"manifest": identidad(manifest), "tolerancia_altura_pct": 10,
               "base_max_px_640x480": datos.get("base_max_px_640x480", 5), "pares": [],
               "alcance": "Altura y apoyo de cajas revisadas; no acredita modelo, animación, salud ni fidelidad global."}
    lineas = ["# Medidas de fichas B2", "", "Alturas normalizadas al área útil 640×480; objetivo nativo ±10 %.",
              "Cajas y apoyos anotados manualmente, salvo método explícito. Bases: margen declarado en JSON.", "",
              "| Par / figura | Nativo px | Nuestro px | Altura normalizada N / P | Error | Base Δx / Δy | Estado |",
              "|---|---:|---:|---:|---:|---:|---|"]
    estados = []
    for indice, par in enumerate(datos["pares"]):
        ims, vps, fuentes = {}, {}, {}
        for k in ("nativo", "nuestro"):
            p = Path(par[k]["imagen"])
            p = p if p.is_absolute() else manifest.parent / p
            ims[k] = Image.open(p).convert("RGB")
            vps[k] = rectangulo(par[k]["viewport"], ims[k].size)
            fuentes[k] = {**identidad(p), "dimensiones": list(ims[k].size), "viewport": vps[k]}
        prefijo = f"{indice:02d}"
        montaje(list(ims.values()), ["B2 original x0.5", "Port x0.5"], salida / f"{prefijo}_completas_05.png")
        marcadas = {k: im.copy() for k, im in ims.items()}
        filas = []
        for figura in par["figuras"]:
            r = evaluar(figura, ims, vps, par.get("comparable", False), 10, informe["base_max_px_640x480"])
            filas.append(r)
            estados.append(r["estado"])
            for k in ims:
                if r[k] is None:
                    continue
                d = ImageDraw.Draw(marcadas[k])
                box = r[k]["caja_px"]
                d.rectangle((box[0], box[1], box[2] - 1, box[3] - 1), outline="#ff30e0", width=2)
                d.text((box[0], max(0, box[1] - 14)), figura["id"], fill="white")
                if r[k]["base_px"] is not None:
                    x, y = r[k]["base_px"]
                    d.line((x-7, y, x+7, y), fill="cyan", width=2)
                    d.line((x, y-7, x, y+7), fill="cyan", width=2)
            n, o = r["nativo"], r["nuestro"]
            delta = r.get("delta_base_640x480")
            lineas.append(f"| {par['nombre']} / {figura['id']} | {n['altura_px'] if n else '?'} | "
                          f"{o['altura_px'] if o else '?'} | "
                          + (f"{n['altura_640x480']:.2f} / {o['altura_640x480']:.2f}" if n and o else "?")
                          + " | " + (f"{r['error_altura_pct']:+.2f}%" if "error_altura_pct" in r else "?")
                          + " | " + (f"{delta[0]:+.2f} / {delta[1]:+.2f}" if delta else "?")
                          + f" | {r['estado']} |")
            # Recorte común en coordenadas normalizadas: no reescala cada figura a la misma altura.
            if n and o:
                cajas = [punto_normalizado(m["caja_px"][:2], vps[k]) + punto_normalizado(m["caja_px"][2:], vps[k])
                         for k, m in (("nativo", n), ("nuestro", o))]
                roi = (max(0, int(min(b[0] for b in cajas))-12), max(0, int(min(b[1] for b in cajas))-16),
                       min(640, math.ceil(max(b[2] for b in cajas))+12), min(480, math.ceil(max(b[3] for b in cajas))+12))
                recortes = [ims[k].crop(vps[k]).resize((640, 480), Image.Resampling.LANCZOS).crop(roi) for k in ims]
                montaje(recortes, ["Nativo", "Port"], salida / f"{prefijo}_figura_{len(filas):02d}.png", escala=2)
        normales = [marcadas[k].crop(vps[k]).resize((640, 480), Image.Resampling.LANCZOS) for k in ims]
        montaje(normales, ["B2 util 640x480 x0.5", "Port util 640x480 x0.5"], salida / f"{prefijo}_medidas_05.png")
        informe["pares"].append({"nombre": par["nombre"], "comparable": par.get("comparable", False),
                                 "nota": par.get("nota", ""), "fuentes": fuentes, "figuras": filas})
    estado = "FAIL" if "FAIL" in estados else "PENDIENTE" if not estados or "PENDIENTE" in estados else "PASS"
    informe["estado"] = estado
    (salida / "medidas.json").write_text(json.dumps(informe, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (salida / "medidas.md").write_text("\n".join(lineas) + f"\n\nResultado: **{estado}**.\n", encoding="utf-8")
    print("\n".join(lineas[6:]))
    print(f"{estado}: {salida}")
    return {"PASS": 0, "FAIL": 1, "PENDIENTE": 2}[estado]


def autoprueba():
    im = {"nativo": Image.new("RGB", (640, 480)), "nuestro": Image.new("RGB", (1600, 900))}
    vp = {"nativo": [0, 0, 640, 480], "nuestro": [200, 0, 1400, 900]}
    fig = {"id": "calibracion", "nativo": {"caja": [10, 10, 30, 110], "base": [20, 110]},
           "nuestro": {"caja": [218.75, 18.75, 256.25, 206.25], "base": [237.5, 206.25]}}
    assert evaluar(fig, im, vp, True, 10, 5)["estado"] == "PASS", "compensar pillarbox y resolución"
    fig["nuestro"]["caja"][3] += 20.625  # +11%, fuera del margen
    assert evaluar(fig, im, vp, True, 10, 5)["estado"] == "FAIL"
    assert evaluar(fig, im, vp, False, 10, 5)["estado"] == "PENDIENTE", "no aprobar estados diferentes"
    fig["nuestro"] = None
    assert evaluar(fig, im, vp, True, 10, 5)["estado"] == "PENDIENTE", "no aprobar figura ausente"
    try:
        rectangulo([-1, 0, 4, 5], (640, 480))
    except ValueError:
        pass
    else:
        raise AssertionError("debe rechazar recortes fuera de imagen")
    print("PASS b2_comparar_figuras: resolución/bandas, +11%, no comparable, ausente y límites")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("manifest", type=Path, nargs="?")
    ap.add_argument("--salida", type=Path)
    ap.add_argument("--autoprueba", action="store_true")
    a = ap.parse_args()
    if a.autoprueba:
        autoprueba()
        return 0
    if not a.manifest or not a.salida:
        ap.error("indica medidas.json y --salida (carpeta nueva)")
    try:
        return comparar(a.manifest, a.salida)
    except (ValueError, KeyError, OSError) as e:
        print(f"PENDIENTE: {e}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
