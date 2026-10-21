#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ut_catalogo.py - Catálogo del creador de Ultimate Tenkaichi (G06a).

Recorre las piezas del creador de UT YA CONVERTIDAS a B3 HD (`--convertidas`,
por defecto `DBZ3_CAC_UT_CONVERTIDAS` o `cac/convertidas`) y los datos
originales UT (`--extraido`, por defecto `DBZ3_CAC_UT` o `cac/fuente`), y genera:

    catalogo_ut.json    datos completos (familias, apariencias, piezas, indices AFS)
    catalogo_ut.md      tablas legibles ES/EN

Que es cada cosa (medido, no inventado):
  * familia `a0|a1|b0|b1|c0|c1` = CONSTITUCION x CARA.
      - letra -> constitucion UT (3): b=Ligera/Light (tipo 0), a=Estándar/Standard
        (tipo 1), c=Pesada/Heavy (tipo 2). Evidencia: `db_model_param_*` byte 9
        (0/1/2) y la malla UT sin animar: altura 1.30 m (b) / 1.82 m (a) / 2.04 m
        (c) (Fandom: Light 4'0", Standard 5'9", Heavy 6'5").
      - digito 0/1 -> cara A/B: mismo cuerpo (mismo alto/ancho; en a/c la malla
        difiere <0,02 u y cambian las texturas de cejas/ojos `*_mayu`, `*_eye`,
        eyeball/ramp). Nombres utiles de UT: `avatar_a0`/`avatar_a1`.
  * variante `NNp` (01..17) -> APARIENCIA completa (cuerpo + ropa + cola): la firma
    de grupos de ropa (`C_`, `D_`, `E_`, `F_`, `LOGO`) de `avatar_<fam>_NNp` es la
    MISMA en las 6 familias (mismo look para las 3 constituciones).

INDICES AFS: el cuerpo es la entrada `<idx>.ioram` (+1 `.spr`, +2 `.vram`); base
6929 (a0_01p, inventario studies 6), +51 por familia, +3 por variante. Si el
`piezas_cac.toml` esta al lado, se usa como fuente.

Uso:
    python ut_catalogo.py [--convertidas DIR] [--extraido DIR] [--salida DIR]
                          [--sin-mallas] [--check]
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
for p in (AQUI, os.path.join(AQUI, "..", "..", "awo_tools")):
    if os.path.isdir(p) and p not in sys.path:
        sys.path.append(os.path.abspath(p))

CONV_POR_DEFECTO = os.environ.get("DBZ3_CAC_UT_CONVERTIDAS", os.path.join(AQUI, "convertidas"))
EXTR_POR_DEFECTO = os.environ.get("DBZ3_CAC_UT", os.path.join(AQUI, "fuente"))
SALIDA_POR_DEFECTO = os.environ.get("DBZ3_CAC_UT_SALIDA", os.getcwd())

FAMILIAS = ("a0", "a1", "b0", "b1", "c0", "c1")
APARIENCIAS = ["%02dp" % i for i in range(1, 18)]

CONSTITUCION = {                       # letra -> (id, ES, EN)
    "b": (0, "Ligera", "Light"),
    "a": (1, "Estándar", "Standard"),
    "c": (2, "Pesada", "Heavy"),
}
IDX_BASE, IDX_FAM, IDX_VAR = 6929, 51, 3

# Firma de ropa -> etiqueta. Las claves son las que aparecen en el nombre del
# grupo UT (tras el ultimo ':'); ver `categorias_cuerpo.tsv` (studies 6).
ETIQUETAS = [
    (("ARMOR",), "Armadura Saiyan", "Saiyan armor"),
    (("ARMER",), "Armadura con hombreras", "Shouldered armor"),
    (("SCARF",), "Con bufanda", "With scarf"),
    (("BLOUSON", "BUTTON"), "Ropa de calle", "Casual clothes"),
    (("SHIRT",), "Camisa y zapatos", "Shirt and shoes"),
    (("SUITS",), "Traje de combate", "Battle suit"),
    (("UWAGI", "SUNE"), "Gi de calle", "Street gi"),
    (("C_CLOTH",), "Ropa ligera", "Light clothes"),
    (("BODY", "LINE", "PANT", "ARMR"), "Traje de combate", "Battle suit"),
    (("DOUGI",), "Gi de artes marciales", "Martial arts gi"),
    (("ZUBON",), "Pantalon de calle", "Street trousers"),
    (("SHOESE",), "Calzado", "Footwear"),
]


def _leer_ioram_par(ruta_spr):
    io = ruta_spr[:-4] + ".ioram"
    return open(io, "rb").read() if os.path.exists(io) else b""


def familia_de(nom):
    m = re.match(r"avatar_([a-z])(\d)_", nom)
    return (m.group(1) + m.group(2)) if m else None


def variante_de(nom):
    m = re.search(r"_(\d+)p", nom)
    return m.group(1) + "p" if m else None


def es_cuerpo(nom):
    return bool(re.match(r"avatar_[a-z]\d_\d+p", nom))


def es_pelo(nom):
    return nom.startswith("avatar_hair_")


def md5_y_tam(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()[:12], os.path.getsize(path)


def ventanas_huesos(path):
    """(ventanas, huesos) del bin B3, o (None, None) si no esta la herramienta."""
    try:
        from awg_vertex_buffer import AwgVertexBuffer
        avb = AwgVertexBuffer.load(path)
        return avb.n, avb.bone_count()
    except Exception:                       # noqa: BLE001  (herramienta ausente)
        return None, None


def malla_ut(ruta_spr, sin_mallas=False):
    """(dic, firma) desde el .spr UT original: altura/ancho model-space (sin
    animar) y firma de ropa por nombre de grupo. `dic` incompleto si falla."""
    if sin_mallas or not os.path.exists(ruta_spr):
        return {}, []
    try:
        import ut_formato
        spr = ut_formato.Spr(ut_formato.cargar_spr(ruta_spr), _leer_ioram_par(ruta_spr))
    except Exception as e:                  # noqa: BLE001
        return {"error": str(e)[:120]}, []
    ys, xs, firma = [], [], []
    for g in ut_formato.grupos(spr):
        if not (0 < g.vertex_count <= 100000):
            continue
        clave = g.hueso_del_nombre()
        if clave.startswith(("C_", "D_CLOTH", "E_OBI", "F_", "LOGO", "panel")):
            firma.append(clave)
        try:
            for (x, y, z, _nx, _ny, _nz, _u, _v, _b, _w) in g.vertices():
                xs.append(x)
                ys.append(y)
        except Exception:                   # noqa: BLE001
            pass
    dic = {}
    if ys:
        dic["altura_m"] = round(max(ys) - min(ys), 3)
        dic["ancho_m"] = round(max(xs) - min(xs), 3)
    return dic, sorted(set(firma))


def etiqueta(firma):
    utiles = [k for k in firma if not k.startswith(("LOGO", "panel"))]
    if not utiles:
        return "Cuerpo base (sin ropa)", "Base body (no clothes)"
    texto = " ".join(utiles).upper()
    for claves, es, en in ETIQUETAS:
        if any(c in texto for c in claves):
            return es, en
    return "", ""


def idx_cuerpo(fam_idx, var_idx):
    return IDX_BASE + IDX_FAM * fam_idx + IDX_VAR * var_idx


def leer_piezas_toml():
    """{nombre normalizado: idx_afs} de piezas_cac.toml si esta al lado."""
    ruta = os.path.join(AQUI, "piezas_cac.toml")
    idx = {}
    if not os.path.exists(ruta):
        return idx
    nombre, ia = None, None
    for ln in open(ruta, encoding="utf-8"):
        m = re.match(r'nombre\s*=\s*"([^"]+)"', ln)
        if m:
            nombre = m.group(1)
        m = re.match(r"idx_afs\s*=\s*(-?\d+)", ln)
        if m and nombre:
            idx[nombre] = int(m.group(1))
            nombre = None
    return idx


def escanear(conv, extr, sin_mallas):
    """Devuelve (cuerpos, piezas, pelos)."""
    cuerpos, piezas, pelos = [], {}, []
    for tipo in ("cuerpo", "cara", "ropa", "pelo", "cola"):
        d = os.path.join(conv, tipo)
        piezas[tipo] = {}
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if not f.endswith("_b3.bin"):
                continue
            nom = f[:-len("_b3.bin")]
            if tipo == "cuerpo":
                fam, var = familia_de(nom), variante_de(nom)
                if fam is None or var is None:
                    continue
                fam_idx, var_idx = FAMILIAS.index(fam), APARIENCIAS.index(var)
                spr = None
                for g2 in os.listdir(extr):
                    if g2.endswith("_avatar_%s_%s_X360.spr" % (fam, var)):
                        spr = os.path.join(extr, g2)
                        break
                m, firma = malla_ut(spr, sin_mallas) if spr else ({}, [])
                md5h, tam = md5_y_tam(os.path.join(d, f))
                nv, nb = ventanas_huesos(os.path.join(d, f))
                cuerpos.append({
                    "archivo": f, "familia": fam, "variante": var,
                    "constitucion": CONSTITUCION[fam[0]][1],
                    "constitucion_en": CONSTITUCION[fam[0]][2],
                    "tipo_id": CONSTITUCION[fam[0]][0], "cara": int(fam[1]),
                    "idx_afs": idx_cuerpo(fam_idx, var_idx),
                    "bytes": tam, "md5": md5h, "ventanas": nv, "huesos": nb,
                    "altura_m": m.get("altura_m"), "ancho_m": m.get("ancho_m"),
                    "firma_ropa": firma, "spr_ut": os.path.basename(spr) if spr else None,
                })
                piezas[tipo][nom] = {"archivo": f, "bytes": tam}
            elif tipo == "pelo":
                pelos.append({"archivo": f, "nombre": nom,
                              "bytes": os.path.getsize(os.path.join(d, f))})
            else:
                piezas[tipo][nom] = {"archivo": f, "bytes": os.path.getsize(os.path.join(d, f))}
    return cuerpos, piezas, pelos


def modelo_param(extr, fam, var):
    for g in os.listdir(extr):
        if g.startswith("db_model_param_") and g.endswith("_avatar_%s_%s" % (fam, var)):
            d = open(os.path.join(extr, g), "rb").read()
            if len(d) >= 10:
                import struct
                f0, f1 = struct.unpack_from(">2f", d, 0)
                return {"fichero": g, "f0": round(f0, 3), "f1": round(f1, 3),
                        "byte8": d[8], "byte9": d[9]}
    return None


def construir(conv, extr, sin_mallas):
    cuerpos, piezas, pelos = escanear(conv, extr, sin_mallas)
    idx_toml = leer_piezas_toml()

    familias = []
    for fam in FAMILIAS:
        c = next((x for x in cuerpos if x["familia"] == fam and x["variante"] == "01p"), None)
        mp = modelo_param(extr, fam, "01p")
        letra = fam[0]
        familias.append({
            "codigo": fam, "letra": letra, "cara": int(fam[1]),
            "constitucion": CONSTITUCION[letra][1], "constitucion_en": CONSTITUCION[letra][2],
            "tipo_id": CONSTITUCION[letra][0],
            "altura_m": c and c.get("altura_m"), "ancho_m": c and c.get("ancho_m"),
            "model_param": mp,
            "cuerpos": sum(1 for x in cuerpos if x["familia"] == fam),
            "nombre_es": "%s (cara %s)" % (CONSTITUCION[letra][1], "A" if fam[1] == "0" else "B"),
            "nombre_en": "%s (face %s)" % (CONSTITUCION[letra][2], "A" if fam[1] == "0" else "B"),
        })

    apariencias = []
    for var in APARIENCIAS:
        fams = {}
        for x in cuerpos:
            if x["variante"] == var:
                nom = "avatar_%s_%s" % (x["familia"], var)
                fams[x["familia"]] = {
                    "cuerpo": x["archivo"], "bytes": x["bytes"],
                    "ventanas": x["ventanas"], "huesos": x["huesos"],
                    "altura_m": x["altura_m"], "idx_afs": x["idx_afs"],
                }
        firma = sorted({k for x in cuerpos if x["variante"] == var for k in x["firma_ropa"]})
        es, en = etiqueta(firma)
        apariencias.append({
            "id": var, "nombre_es": "Apariencia %s%s" % (var[:2], (" — " + es) if es else ""),
            "nombre_en": "Appearance %s%s" % (var[:2], (" — " + en) if en else ""),
            "etiqueta_h": "descripcion por firma de grupos de ropa",
            "firma_ropa": firma,
            "familias": fams,
            "cara_b3": piezas["cara"].get("avatar_a0_%s" % var) is not None,
            "ropa_b3": piezas["ropa"].get("avatar_a0_%s" % var) is not None,
            "cola_b3": piezas["cola"].get("avatar_a0_%s" % var) is not None,
        })

    pelos = sorted(pelos, key=lambda x: x["nombre"])
    for p in pelos:
        p["idx_afs"] = idx_toml.get(p["nombre"].replace("avatar_", "").replace("_b3", ""), None)

    resumen = {
        "familias": len(familias), "cuerpos": len(cuerpos),
        "caras": len(piezas.get("cara", {})), "ropas": len(piezas.get("ropa", {})),
        "colas": len(piezas.get("cola", {})), "pelos": len(pelos),
        "ventanas_presentes": sum(1 for x in cuerpos if x["ventanas"]),
    }
    return {
        "generado": datetime.datetime.now().strftime("%Y-%m-%d %H:%M"),
        "fuentes": {"convertidas": conv, "extraido": extr},
        "resumen": resumen, "familias": familias, "apariencias": apariencias,
        "pelos": pelos,
        "piezas": {t: conteo for t, conteo in
                   ((t, len(piezas.get(t, {}))) for t in ("cuerpo", "cara", "ropa", "cola"))},
        "notas_h": [
            "Cara A/B (digito 0/1): mismo cuerpo (altura/ancho identicos, malla a/c difiere"
            " <0,02 u; b difiere en ~200 verts de cara/accesorio); cambian texturas de"
            " cejas/ojos (`*_mayu`, `*_eye`, eyeball/ramp). Semantica exacta [H].",
            "NNp 01..17: apariencias completas (cuerpo+ropa+cola). La descripcion ES/EN se"
            " deriva de la firma de grupos de ropa; el nombre oficial de UT no esta volcado [H].",
            "La cola (`A_TAIL`) se convierte al hueso WAIST: en B3 la cola sale en la cintura"
            " (B3 no tiene hueso de cola).",
            "`avatar_hair_000_00` extra no tiene `.vram` (sale sin textura).",
        ],
    }


def escribir_md(cat, ruta):
    L = []
    L.append("# Catálogo UT (cuerpos del creador de Ultimate Tenkaichi) — G06a\n")
    L.append("Generado: %s · Fuentes: `%s`, `%s`\n"
             % (cat["generado"], cat["fuentes"]["convertidas"], cat["fuentes"]["extraido"]))
    r = cat["resumen"]
    L.append("Resumen: **%d familias** (3 constituciones × 2 caras) · **%d cuerpos** · "
             "%d caras · %d ropas · %d colas · %d pelos.\n"
             % (r["familias"], r["cuerpos"], r["caras"], r["ropas"], r["colas"], r["pelos"]))
    L.append("| Familia | Constitución (ES/EN) | tipo (`db_model_param` b9) | alto × ancho UT | cuerpos | fichero de ejemplo |")
    L.append("|---|---|---|---|---|---|")
    for f in cat["familias"]:
        L.append("| `%s` | %s / %s — cara %s | %s | %s × %s m | %d | `%s` |"
                 % (f["codigo"], f["constitucion"], f["constitucion_en"],
                    "A" if f["cara"] == 0 else "B",
                    f["tipo_id"], f["altura_m"], f["ancho_m"], f["cuerpos"],
                    (f["model_param"] or {}).get("fichero", "")))
    L.append("\n## Apariencias (17 por constitución; la ropa marca el look)\n")
    L.append("| NNp | Etiqueta ES | Etiqueta EN | Grupos de ropa (firma) |")
    L.append("|---|---|---|---|")
    for a in cat["apariencias"]:
        firma = ", ".join(a["firma_ropa"][:6]) + ("…" if len(a["firma_ropa"]) > 6 else "")
        es = a["nombre_es"].split("— ")[-1] if "—" in a["nombre_es"] else ""
        en = a["nombre_en"].split("— ")[-1] if "—" in a["nombre_en"] else ""
        L.append("| `%s` | %s | %s | %s |" % (a["id"], es or "[H]", en or "[H]", firma))
    L.append("\n## Pelos disponibles (convertidos)\n")
    L.append("| fichero | idx AFS | bytes |")
    L.append("|---|---|---|")
    for p in cat["pelos"]:
        L.append("| `%s` | %s | %d |" % (p["archivo"], p["idx_afs"], p["bytes"]))
    L.append("\n## Piezas por cuerpo\n")
    L.append("Cada cuerpo incluye TODO (cuerpo + cara + ropa + cola); además hay "
             "subconjuntos ya convertidos por apariencia: `cara/`, `ropa/` y `cola/` "
             "(`avatar_<familia>_<NNp>_b3.bin`).\n")
    L.append("## Notas [H]\n")
    for n in cat["notas_h"]:
        L.append("- %s" % n)
    open(ruta, "w", encoding="utf-8").write("\n".join(L) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--convertidas", default=CONV_POR_DEFECTO)
    ap.add_argument("--extraido", default=EXTR_POR_DEFECTO)
    ap.add_argument("--salida", default=SALIDA_POR_DEFECTO)
    ap.add_argument("--sin-mallas", action="store_true",
                    help="no leer los .spr de UT (rapido; altura/ancho quedan a null)")
    ap.add_argument("--check", action="store_true", help="solo comprobar conteos")
    a = ap.parse_args()

    cat = construir(a.convertidas, a.extraido, a.sin_mallas)
    r = cat["resumen"]
    print("familias=%d cuerpos=%d caras=%d ropas=%d colas=%d pelos=%d"
          % (r["familias"], r["cuerpos"], r["caras"], r["ropas"], r["colas"], r["pelos"]))
    if a.check:
        ok = (r["familias"] == 6 and r["cuerpos"] == 102 and r["caras"] == 102
              and r["ropas"] == 102 and r["colas"] == 102)
        print("check:", "OK" if ok else "FAIL")
        return 0 if ok else 1
    os.makedirs(a.salida, exist_ok=True)
    j = os.path.join(a.salida, "catalogo_ut.json")
    m = os.path.join(a.salida, "catalogo_ut.md")
    with open(j, "w", encoding="utf-8") as o:
        json.dump(cat, o, ensure_ascii=False, indent=1)
    escribir_md(cat, m)
    print("escrito", j)
    print("escrito", m)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
