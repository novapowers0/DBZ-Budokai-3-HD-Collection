"""CAC-01 (PLAN_CAC_MEJORA.md): inventario de trajes nativos del creador y juez de silueta.

  python tools/pruebas_heroe/cac_juez.py --inventario [--salida R23/cac/trajes_nativos.md]
      cuerpos del creador (heroe.cpp kRazas) x trajes del roster x modelos de cada forma; marca los trajes que el
      montaje (vista_heroe.KFID) no conoce o que apuntan a otro modelo.
  python tools/pruebas_heroe/cac_juez.py --lamina "cuerpo=0;traje=0" [--salida png] [--juego D:/DBZ3HD/smoke_e]
      monta el heroe de esa ficha FUERA del juego (vista_heroe.combate_bin, el mismo bin del combate) y lo pinta al lado
      del modelo nativo del traje en 4 vistas (frente, 3/4, lado, espalda). IoU de silueta por vista: la ficha sin cambios
      sobre su propio cuerpo debe dar >= 0,97 (si no, el montaje deforma algo).
Solo lee el AFS del jugador (us/data_cmn.afs); no escribe en el juego."""
import argparse
import os
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(RAIZ / "mod center hd" / "cac"), str(RAIZ / "mod center hd")]
import numpy as np                 # noqa: E402
from PIL import Image, ImageDraw   # noqa: E402

# heroe.cpp kRazas (cuerpos nativos de B3; los UT/XV2 no tienen trajes nativos)
CUERPOS = {"Saiyan": (0, 7, 39, 8), "Humano": (10, 12, 13, 15), "Namekiano": (11,), "Raza de Freezer": (27, 38),
           "Majin": (34, 36, 35), "Androide": (30, 29, 28, 33)}
VISTAS = (0, 45, 90, 180)


def inventario(juego=None):
    import roster_build as rb
    import vista_heroe as vh
    vh.cargar_trajes_extra(juego)
    out = ["# Trajes nativos del creador (CAC-01, generado por tools/pruebas_heroe/cac_juez.py --inventario)", "",
           "Por cuerpo: trajes del roster (B3) y lo que el montaje del héroe (`vista_heroe.KFID`) conoce. «falta» = el "
           "creador puede elegir ese traje (heroe.cpp lee la tabla del select) pero el montaje usa otro modelo.", "",
           "| Raza | Cuerpo | Trajes B3 | KFID | Traje: modelo base · formas (fid) | Estado |", "|---|---|---|---|---|---|"]
    malos = 0
    for raza, ids in CUERPOS.items():
        for cid in ids:
            row = rb.IDS[cid]
            per = int(row.get("models_per_costume") or 1)
            n = int(row["costumes"])
            kf = vh.KFID.get(cid, [])
            celdas, estado = [], []
            for t in range(n):
                mods = row["models"][t * per:(t + 1) * per]
                formas = sorted(set(mods[1:]) - {mods[0]})
                celdas.append("%d: %d%s" % (t, mods[0], (" · " + ",".join(map(str, formas))) if formas else ""))
                if t >= len(kf):
                    estado.append("t%d falta" % t)
                elif kf[t] != mods[0]:
                    estado.append("t%d KFID=%d" % (t, kf[t]))
            malos += len(estado)
            out.append("| %s | %d %s | %d | %d | %s | %s |" % (raza, cid, row["name"], n, len(kf), "<br>".join(celdas),
                                                                ", ".join(estado) or "OK"))
    out += ["", "Trajes con problema: %d." % malos, "", "## Trajes extra de mods en %s (CAC-02)" % juego, "",
            "| Cuerpo | Traje | Modelos por forma |", "|---|---|---|"]
    out += ["| %d %s | %d | %s |" % (c, rb.IDS[c]["name"], t, ",".join(map(str, f))) for (c, t), f in sorted(vh.EXTRA_FORMAS.items())]
    return "\n".join(out) + "\n", malos


def _vistas(model, size, ctr=None, esc=None):
    import model_render as mr
    import vista_heroe as vh
    pose = vh.pose_de(model)
    P, _ = model.posed(pose)
    usados = np.unique(model.tris.reshape(-1))
    lo, hi = P[usados].min(0), P[usados].max(0)
    if ctr is None:
        ctr = (lo + hi) / 2
        esc = size * 0.9 / max(hi[1] - lo[1], np.linalg.norm([hi[0] - lo[0], hi[2] - lo[2]]), 1e-6)
    imgs = [mr.render(model, (size, size), mr.rot_x(6.0) @ mr.rot_y(y), ctr, esc, pose=pose, outline=1.4, ss=2)
            for y in VISTAS]
    return imgs, ctr, esc


def lamina(ficha_txt, juego, salida, size=320, referencia_ficha=None):
    import model_render as mr
    import roster_build as rb
    import vista_heroe as vh
    f = vh.leer_ficha(ficha_txt)
    cid, t = int(f.get("cuerpo", 0)), int(f.get("traje", 0))
    ref = None if referencia_ficha is None else vh.leer_ficha(referencia_ficha)
    if ref is not None and (int(ref.get("cuerpo", 0)), int(ref.get("traje", 0))) != (cid, t):
        raise ValueError("La referencia debe usar el mismo cuerpo y traje")
    afs = vh.abrir_afs(None, tempfile.mkdtemp(prefix="cac_juez_", dir="D:/tmp"), juego)   # + trajes extra de mods
    fid = vh._modelos_traje(cid, t)[0]
    base = afs.entry(fid)
    etiqueta = "nativo fid %d (cuerpo %d traje %d)" % (fid, cid, t)
    if ref is not None:
        referencia, resumen_ref = vh.combate_bin(base, ref, juego, afs)
        etiqueta = "referencia %s" % referencia_ficha
    else:
        referencia = base
    nativo = mr.Model(referencia)
    datos, r = vh.combate_bin(base, f, juego, afs)
    if ref is not None:
        r = dict(r, referencia={k: v for k, v in resumen_ref.items() if not isinstance(v, (bytes, bytearray))})
    heroe = mr.Model(datos)
    a, ctr, esc = _vistas(nativo, size)
    b, _, _ = _vistas(heroe, size, ctr, esc)   # mismo encuadre: la diferencia de tamano tambien cuenta
    ious = []
    for x, y in zip(a, b):
        ma, mb = x[..., 3] > 0, y[..., 3] > 0
        ious.append(float((ma & mb).sum()) / max(1, float((ma | mb).sum())))
    im = Image.new("RGB", (size * len(VISTAS), size * 2 + 40), (40, 40, 48))
    d = ImageDraw.Draw(im)
    for k, (x, y) in enumerate(zip(a, b)):
        for fila, img in ((0, x), (1, y)):
            fondo = Image.new("RGBA", (size, size), (90, 110, 140, 255))
            fondo.alpha_composite(Image.fromarray(img))
            im.paste(fondo.convert("RGB"), (k * size, 20 + fila * size))
        d.text((k * size + 4, 2), "%d grados  IoU %.3f" % (VISTAS[k], ious[k]), fill=(255, 220, 80))
    d.text((4, size * 2 + 24), "arriba: %s | abajo: heroe %s" % (etiqueta, ficha_txt),
           fill=(200, 230, 255))
    os.makedirs(os.path.dirname(salida) or ".", exist_ok=True)
    im.save(salida)
    return ious, r


def tiras(ficha_txt, juego, salida, size=180):
    """CAC-07/16: una vista de espalda por tira (draw) del modelo de la ficha, con ESA tira oculta: para saber que
    indice poner en `ocultar=`."""
    import model_render as mr
    import vista_heroe as vh
    f = vh.leer_ficha(ficha_txt)
    afs = vh.abrir_afs(None, tempfile.mkdtemp(prefix="cac_tiras_", dir="D:/tmp"), juego)
    fid = vh._modelos_traje(int(f.get("cuerpo", 0)), int(f.get("traje", 0)))[0]
    base, r = vh.combate_bin(afs.entry(fid), f, juego, afs)
    draws = mr.Model(base).table.draws
    celdas = [("todo", base)] + [("%d %s" % (i, d["label"][-10:]), vh.combate_bin(afs.entry(fid), dict(f, ocultar=str(i)),
                                                                                 juego, afs)[0]) for i, d in enumerate(draws)]
    _, ctr, esc = _vistas(mr.Model(base), size)
    cols = 8
    im = Image.new("RGB", (cols * 2 * size, ((len(celdas) + cols - 1) // cols) * (size + 16)), (60, 70, 90))
    d = ImageDraw.Draw(im)
    for k, (rot, datos) in enumerate(celdas):
        imgs, _, _ = _vistas(mr.Model(datos), size, ctr, esc)
        x, y = (k % cols) * 2 * size, (k // cols) * (size + 16)
        d.text((x + 4, y + 2), rot, fill=(255, 230, 120))
        for v, ang in ((0, 0), (1, 3)):
            fondo = Image.new("RGBA", (size, size), (90, 110, 140, 255))
            fondo.alpha_composite(Image.fromarray(imgs[ang]))
            im.paste(fondo.convert("RGB"), (x + v * size, y + 16))
    os.makedirs(os.path.dirname(salida) or ".", exist_ok=True)
    im.save(salida)
    return len(draws)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--inventario", action="store_true")
    ap.add_argument("--lamina", default=None, help='ficha: "cuerpo=0;traje=0;..."')
    ap.add_argument("--referencia-ficha", help="Compara dos fichas del mismo traje; mide diferencias sin dar veredicto")
    ap.add_argument("--tiras", default=None, help='ficha: una vista por tira oculta (indices para ocultar=)')
    ap.add_argument("--juego", default=r"D:\DBZ3HD\smoke_e")
    ap.add_argument("--salida", default=None)
    ap.add_argument("--minimo", type=float, default=0.97, help="IoU minima por vista para dar PASS")
    a = ap.parse_args()
    if a.referencia_ficha is not None and not a.lamina:
        ap.error("--referencia-ficha requiere --lamina")
    if a.inventario:
        txt, malos = inventario(a.juego)
        dst = Path(a.salida or RAIZ / "studies 5" / "ENTREGAS" / "R23" / "cac" / "trajes_nativos.md")
        dst.write_text(txt, encoding="utf-8")
        print("inventario: %s (%d trajes con problema)" % (dst, malos))
    if a.tiras:
        dst = a.salida or str(RAIZ / "studies 5" / "ENTREGAS" / "R23" / "cac" / ("tiras_%s.png" % a.tiras.replace(";", "_").replace("=", "")))
        print("tiras: %s (%d tiras)" % (dst, tiras(a.tiras, a.juego, dst)))
    if a.lamina:
        slug = a.lamina.replace(";", "_").replace("=", "").replace(",", "-").replace("|", "+")[:80]
        dst = a.salida or str(RAIZ / "studies 5" / "ENTREGAS" / "R23" / "cac" / ("juez_%s.png" % slug))
        ious, r = lamina(a.lamina, a.juego, dst, referencia_ficha=a.referencia_ficha)
        ok = min(ious) >= a.minimo
        diagnostico = a.referencia_ficha is not None
        print("lamina: %s  IoU %s  minima %.3f  %s" % (dst, " ".join("%.3f" % x for x in ious), min(ious),
                                                       "SIN VEREDICTO" if diagnostico else ("PASS" if ok else "FAIL")))
        print("montaje: %s" % {k: v for k, v in r.items() if not isinstance(v, (bytes, bytearray))})
        return 0 if diagnostico or ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
