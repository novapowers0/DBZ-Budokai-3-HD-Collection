#!/usr/bin/env python3
"""Comparador offline de capturas PNG OFF/ON, adaptado del lote NIWJ."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import tempfile


def comparar(off, on, roi=None, lamina=None):
    from PIL import Image, ImageChops, ImageDraw, ImageStat

    def leer(ruta):
        with Image.open(ruta, formats=["PNG"]) as im:
            return im.convert("RGB")

    a, b = leer(off), leer(on)
    if a.size != b.size:
        raise ValueError(f"Las capturas deben medir lo mismo: {a.size} y {b.size}")
    w, h = a.size
    diff = ImageChops.difference(a, b)
    caja = None
    if roi is not None:
        x0, y0, x1, y1 = roi
        if not (0 <= x0 < x1 <= 1 and 0 <= y0 < y1 <= 1):
            raise ValueError("ROI inválida: use x0 y0 x1 y1 entre 0 y 1, en orden")
        caja = int(x0 * w), int(y0 * h), int(x1 * w), int(y1 * h)
        if caja[0] == caja[2] or caja[1] == caja[3]:
            raise ValueError("La ROI no contiene píxeles")

    def medidas(im):
        r, g, b = im.split()
        hist = ImageChops.lighter(ImageChops.lighter(r, g), b).histogram()
        return {"mae": round(sum(ImageStat.Stat(im).mean) / 3, 3),
                "pct_gt12": round(100 * sum(hist[13:]) / (im.width * im.height), 3),
                "max_ch": max(i for i, cuenta in enumerate(hist) if cuenta)}

    out = {"size": [w, h], "full": medidas(diff),
           "roi": medidas(diff.crop(caja)) if caja else None, "roi_box": caja}
    if lamina is not None:
        escala = min(1, 640 / w, 360 / h)
        tw, th = max(1, round(w * escala)), max(1, round(h * escala))
        barra = 22
        hoja = Image.new("RGB", (3 * tw, th + barra), (18, 18, 22))
        dibujo = ImageDraw.Draw(hoja)
        for i, (imagen, nombre) in enumerate(zip(
                (a, b, diff.point(lambda v: min(255, v * 4))), ("OFF", "ON", "DIFF x4"))):
            x = i * tw
            hoja.paste(imagen.resize((tw, th), Image.Resampling.LANCZOS), (x, barra))
            dibujo.text((x + 4, 4), nombre, fill="white")
            if caja:
                dibujo.rectangle((x + caja[0] * tw // w, barra + caja[1] * th // h,
                                  x + (caja[2] * tw - 1) // w, barra + (caja[3] * th - 1) // h),
                                 outline=(255, 80, 80))
        with lamina.open("xb") as f:
            hoja.save(f, format="PNG")
    return out


def autoprueba():
    from PIL import Image

    with tempfile.TemporaryDirectory(prefix="iw_evidencia_") as td:
        a, b, lamina = (Path(td) / n for n in ("off.png", "on.png", "lamina.png"))
        base = Image.new("RGB", (32, 24))
        base.save(a)
        assert comparar(a, a)["full"]["mae"] == 0
        base.paste((0, 12, 120), (16, 0, 32, 24))
        base.save(b)
        med = comparar(a, b, (0.5, 0, 1, 1), lamina)
        assert med["full"] == {"mae": 22, "pct_gt12": 50, "max_ch": 120}
        assert med["roi"] == {"mae": 44, "pct_gt12": 100, "max_ch": 120}
        assert lamina.is_file()
        base.resize((31, 24)).save(b)
        try:
            comparar(a, b)
        except ValueError:
            pass
        else:
            raise AssertionError("Aceptó tamaños distintos")
    print("Autoprueba IW evidencia: identidad, ROI, lámina y rechazo de tamaños distintos OK")


def main():
    p = argparse.ArgumentParser(description=__doc__, epilog="Sincronice las tomas antes de comparar.")
    p.add_argument("off", type=Path, nargs="?", help="captura OFF en PNG")
    p.add_argument("on", type=Path, nargs="?", help="captura ON en PNG")
    p.add_argument("--roi", type=float, nargs=4, metavar=("X0", "Y0", "X1", "Y1"),
                   help="región de interés, coordenadas normalizadas de 0 a 1")
    p.add_argument("--lamina", type=Path, help="PNG nuevo para la lámina OFF | ON | DIFF x4")
    p.add_argument("--self-check", "--autoprueba", action="store_true", help="control sintético sin juego")
    a = p.parse_args()
    try:
        if a.self_check:
            autoprueba()
        else:
            if a.off is None or a.on is None:
                p.error("indique las dos capturas, o use --self-check")
            print(json.dumps(comparar(a.off, a.on, a.roi, a.lamina), indent=2))
    except ImportError:
        p.error("el comparador necesita Pillow")
    except (OSError, ValueError) as exc:
        p.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
