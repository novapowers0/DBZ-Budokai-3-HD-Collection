"""CAC-06: hojas de aprobacion de piezas B3 por anfitrion (cuerpo_traje).

  python tools/pruebas_heroe/cac_catalogo.py 7_0 8_0 39_0 [--zonas torso,piernas] [--juego D:/DBZ3HD/smoke_e]
Por anfitrion y zona: cada pieza aprobada en Goku (validacion_visual = perfecto) injertada SOLA en ese cuerpo, frente y
espalda, en una rejilla numerada -> studies 5/ENTREGAS/R23/cac/catalogo/<anfitrion>_<zona>.png (+ .json con el
resultado del injerto). La aprobacion la da un juez independiente mirando la hoja; se apunta con
  python tools/pruebas_heroe/cac_catalogo.py --aprobar 7_0 torso 270,416 ...
que anade el anfitrion a `aprobada_en` de esas piezas en piezas_b3_base.json (lo leen el creador y el montaje)."""
import argparse
import json
import os
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(Path(__file__).parent), str(RAIZ / "mod center hd" / "cac"), str(RAIZ / "mod center hd")]
from PIL import Image, ImageDraw   # noqa: E402

JSON = RAIZ / "mod center hd" / "cac" / "piezas_b3_base.json"
SALIDA = RAIZ / "studies 5" / "ENTREGAS" / "R23" / "cac" / "catalogo"
ZONAS = ("torso", "piernas", "guantes", "botas", "accesorios")


def hojas(anfitriones, zonas, juego, lado=200):
    os.environ["DBZ3_CAC_B3_TEST_TODOS"] = "1"   # solo aqui: ensayar piezas aun no aprobadas en este cuerpo
    import cac_juez as j
    import model_render as mr
    import vista_heroe as vh
    afs = vh.abrir_afs(None, tempfile.mkdtemp(prefix="cac_cat_", dir="D:/tmp"), juego)
    cat = json.loads(JSON.read_text(encoding="utf-8"))
    SALIDA.mkdir(parents=True, exist_ok=True)
    for an in anfitriones:
        c, t = map(int, an.split("_"))
        host = afs.entry(vh._modelos_traje(c, t)[0])
        base_img, ctr, esc = j._vistas(mr.Model(host), lado)
        for z in zonas:
            # CAC-07a: los accesorios se ANADEN (injertar_accesorio_b3): se prueban todos los donantes
            piezas = cat[z] if z == "accesorios" else [p for p in cat[z] if p.get("validacion_visual") == "perfecto"
                                                         and p.get("ok") is True]
            celdas, info = [("base", base_img)], {}
            for p in piezas:
                ficha = {"cuerpo": c, "traje": t, "b3_" + z: p["fid"]}
                try:
                    if z == "accesorios":
                        datos, r = vh.injertar_accesorio_b3(host, p["fid"], afs)
                    else:
                        datos, res = vh._aplicar_piezas_b3(host, ficha, juego, afs)
                        r = res.get(z)
                    ok = isinstance(r, dict)
                    info[p["fid"]] = r if ok else str(r)
                    img, _, _ = j._vistas(mr.Model(datos), lado, ctr, esc) if ok else (None, 0, 0)
                except Exception as e:   # noqa: BLE001 (una pieza rota no para la hoja)
                    ok, img, info[p["fid"]] = False, None, "%s: %s" % (type(e).__name__, e)
                if z == "accesorios" and img is None:
                    continue   # sin accesorios / sin rampa / sin hueco: no entra en la hoja (va en el .json)
                celdas.append(("%d %s" % (p["fid"], p["nombre"][:22]), img))
            cols = 4
            filas = (len(celdas) + cols - 1) // cols
            hoja = Image.new("RGB", (cols * 2 * lado, filas * (lado + 18)), (60, 70, 90))
            d = ImageDraw.Draw(hoja)
            for k, (rot, img) in enumerate(celdas):
                x, y = (k % cols) * 2 * lado, (k // cols) * (lado + 18)
                d.text((x + 4, y + 2), rot, fill=(255, 230, 120))
                if img is None:
                    d.text((x + 20, y + lado // 2), "INJERTO FALLIDO", fill=(255, 90, 90))
                    continue
                for v, ang in ((0, 0), (1, 3)):   # frente y espalda (VISTAS de cac_juez: 0 y 180 grados)
                    fondo = Image.new("RGBA", (lado, lado), (90, 110, 140, 255))
                    fondo.alpha_composite(Image.fromarray(img[ang]))
                    hoja.paste(fondo.convert("RGB"), (x + v * lado, y + 18))
            dst = SALIDA / ("%s_%s.png" % (an, z))
            hoja.save(dst)
            (SALIDA / ("%s_%s.json" % (an, z))).write_text(json.dumps(info, ensure_ascii=False, indent=1, default=str),
                                                         encoding="utf-8")
            print("%s: %d piezas, %d injertos fallidos" % (dst.name, len(piezas),
                                                           sum(1 for v in info.values() if not isinstance(v, dict))))


def aprobar(an, zona, fids):
    cat = json.loads(JSON.read_text(encoding="utf-8"))
    n = 0
    for p in cat[zona]:
        if p["fid"] in fids and an not in p.setdefault("aprobada_en", []):
            p["aprobada_en"].append(an)
            n += 1
    tmp = JSON.with_suffix(".tmp")
    tmp.write_text(json.dumps(cat, ensure_ascii=False, indent=1), encoding="utf-8")   # mismo formato que el original
    os.replace(tmp, JSON)
    print("%s %s: %d piezas aprobadas" % (an, zona, n))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("anfitriones", nargs="*")
    ap.add_argument("--zonas", default=",".join(ZONAS))
    ap.add_argument("--juego", default=r"D:\DBZ3HD\smoke_e")
    ap.add_argument("--aprobar", nargs=3, metavar=("ANFITRION", "ZONA", "FIDS"))
    a = ap.parse_args()
    if a.aprobar:
        an, zona, fids = a.aprobar
        aprobar(an, zona, {int(x) for x in fids.split(",") if x})
    else:
        hojas(a.anfitriones, a.zonas.split(","), a.juego)


if __name__ == "__main__":
    main()
