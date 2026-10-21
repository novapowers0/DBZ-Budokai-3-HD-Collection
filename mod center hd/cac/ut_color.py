#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ut_color.py - Color por parte de los cuerpos del creador de Ultimate Tenkaichi.

Lee la tabla de color del CaC de UT (`*_hsv_000..004.dat`), el identificador de
borde (`*_edge_id.dat`) y el modelo `.spr` (+ `.ioram`/`.vram`) y aplica un color
POR MATERIAL / ZONA (piel, parte de arriba, parte de abajo, cinturon, calzado,
cola, pelo...). Genera renders de comprobacion con `awo_tools/model_render.py`
(el mismo pintor offline que usa `vista_heroe.py`).

Lo medido (2026-10-20):
  * `hsv_001.dat` = color PRINCIPAL por material (32 B de nombre + 0x60 reservado
    + 3 floats BE en +0x80). Hay 1 registro por material del cuerpo.
  * `hsv_000.dat` = segunda capa/color secundario; solo trae datos en cejas y
    algunas botas (p. ej. `A_EYEBROWS_00`, `F_BOOTS_06`); el resto a cero.
  * `hsv_002..004.dat` = 0 bytes (no se usan en estos cuerpos).
  * `edge_id.dat` = 32 B de nombre + u32 BE en +0x80 = ID de borde/grupo de
    contorno (1..17), NO es un color.
  * El `.spr` SI trae texturas separadas por parte (COLORMAP0/COLORMAP1 por
    material): piel, cara, dougi arriba, dougi abajo, obi, botas, camiseta,
    tela, cola, cejas... 53 TX2D / 34 MTRL en el cuerpo b0_01p.
  * CORRECCION: todos los grupos examinados tienen UV originales en usage 0x05.
    Usage 0x03 son tangentes. El lector antiguo confundía ambos y usaba el stride
    de posición para todos los flujos. No se deben inventar UV ni normalizarlas.

La interpretacion del HSV se valida con las texturas: la H va en radianes y es
un angulo ABSOLUTO con el signo invertido (`H_deg = (-H) mod 2pi`), mientras S y
V son GANANCIAS sobre la saturacion/valor de la textura (`S*(1+s)`, `V*(1+v)`).
Con eso los valores por defecto de UT reproducen el tono base de cada parte.

Uso:
  python ut_color.py zonas   --spr <cuerpo.spr> --hsv <hsv_001.dat> [--edge <edge_id.dat>] [--md salida.md]
  python ut_color.py render  --spr <cuerpo.spr> --ioram <cuerpo.ioram> --vram <cuerpo.vram>
                             --hsv <hsv_001.dat> [--edge <edge_id.dat>] --out <salida.png>
                             [--giros 6] [--size 320] [--texturas]
                             [--colores "piel=#E8B48C,arriba=#C0392B,..."]
  python ut_color.py medir   --spr ... --hsv ... [--edge ...] [--b3 <convertido.bin>] [--md salida.md]
  python ut_color.py b3      --b3 <convertido.bin> --out <salida.png> [--giros 4] [--size 320]

No toca ut_formato.py, ut_textura.py, vista_heroe.py, ropa_xeno.py ni src/*.
"""
import argparse
import colorsys
import json
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
for p in (HERE, os.path.join(HERE, ".."), os.path.join(HERE, "..", "..", "awo_tools")):
    if os.path.isdir(p) and p not in sys.path:
        sys.path.append(p)

import ut_formato  # noqa: E402
import ut_textura  # noqa: E402


# ---------------------------------------------------------------------------
# Zonas (las 4 obligatorias + cinturon/cola/pelo/ojos/accesorios)
# ---------------------------------------------------------------------------
ZONAS = [
    ("piel", "Piel", ("S_SKIN", "S_FACE", "S_NECK", "S_EAR", "S_SWEAT", "SIWA",
                      "SKIN", "FACE", "NECK", "EAR")),
    ("cinturon", "Cinturon", ("E_OBI", "OBI")),
    ("calzado", "Calzado", ("F_BOOTS", "C_BOOTS", "P_D_BOOTS", "P_B_BOOTS",
                            "B_BOOTS", "BOOTS", "KUTU")),
    ("abajo", "Parte de abajo", ("C_DOUGI_SHITA", "C_PANT", "P_B_DOUGI_SHITA",
                                 "P_D_DOUGI_SHITA", "D_CLOTH_ASHI", "CLOTH_ASHI", "ASHI", "PANT",
                                 "LEG", "SHITA")),
    ("arriba", "Parte de arriba", ("C_DOUGI_UE", "C_UWAGI", "C_BODY", "C_SUITS",
                                   "C_ARMOR", "C_MANTO", "C_WRIST", "C_LINE",
                                   "D_CLOTH_IN", "D_CLOTH", "LOGO", "PANEL",
                                   "UWAGI", "SUITS", "ARMOR", "DOUGI", "CHEST",
                                   "CLOTH", "SHIRT")),
    ("pelo", "Pelo / cejas", ("A_EYEBROWS", "H_HAIR", "H_EYEBROW", "A_MAYU",
                               "EYEBROW", "HAIR", "MAYU")),
    ("ojos", "Ojos / boca", ("A_EYE", "E_EYE", "T_TOOTH", "T_TONGUE", "TOOTH",
                              "TONGUE", "EYE", "MOUTH")),
    ("cola", "Cola", ("A_TAIL", "TAIL")),
    ("otros", "Accesorios", ("A_", "G_", "AURA", "SWORD", "METAL", "ACCE",
                              "POTARA", "SCOUTER", "BODY")),
]

COLORES_DEF = {
    "piel": "#E8B48C",
    "arriba": "#C0392B",
    "abajo": "#2E5FA3",
    "calzado": "#F1C40F",
    "cinturon": "#27AE60",
    "cola": "#8E44AD",
    "pelo": "#2C3E50",
    "ojos": "#FDFEFE",
    "otros": "#7F8C8D",
}


def zona_de(material):
    m = (material or "").upper()
    for clave, _etiqueta, prefijos in ZONAS:
        for p in prefijos:
            if m.startswith(p):
                return clave
    # tokens en cualquier posicion (nombres raros de otros conjuntos)
    for clave, _etiqueta, prefijos in ZONAS:
        for p in prefijos:
            if p in m:
                return clave
    return "otros"


def etiqueta_zona(clave):
    return next((e for c, e, _ in ZONAS if c == clave), clave)


# ---------------------------------------------------------------------------
# Lectura de las tablas .dat
# ---------------------------------------------------------------------------
def leer_hsv(path, capa=1):
    """{material: (h, s, v)} de un hsv_00X.dat (registros de 0x100 B)."""
    if not path or not os.path.exists(path) or os.path.getsize(path) < 0x80:
        return {}
    d = open(path, "rb").read()
    n = struct.unpack_from(">I", d, 0)[0]
    if 0x80 + n * 0x100 > len(d):
        n = max(0, (len(d) - 0x80) // 0x100)
    out = {}
    for i in range(n):
        r = d[0x80 + i * 0x100:0x80 + (i + 1) * 0x100]
        nombre = r[:32].split(b"\x00")[0].decode("latin1")
        if nombre:
            out[nombre] = struct.unpack_from(">3f", r, 0x80)
    return out


def leer_edge(path):
    """{material: id de borde} de edge_id.dat (u32 BE en +0x80)."""
    if not path or not os.path.exists(path) or os.path.getsize(path) < 0x80:
        return {}
    d = open(path, "rb").read()
    n = struct.unpack_from(">I", d, 0)[0]
    if 0x80 + n * 0x100 > len(d):
        n = max(0, (len(d) - 0x80) // 0x100)
    out = {}
    for i in range(n):
        r = d[0x80 + i * 0x100:0x80 + (i + 1) * 0x100]
        nombre = r[:32].split(b"\x00")[0].decode("latin1")
        if nombre:
            out[nombre] = struct.unpack_from(">I", r, 0x80)[0]
    return out


# ---------------------------------------------------------------------------
# MTRL: material -> texturas con nombre (COLORMAP0/1, TOONMAP, NORMALMAP...)
# ---------------------------------------------------------------------------
def mapa_materiales(spr):
    out = {}
    for e in spr.seccion(b"MTRL"):
        b = spr.bloque(e["data_offset"], e["data_size"])
        if len(b) < 0x90:
            continue
        vals = struct.unpack_from(">8I", b, 0x70)
        d = {}
        for k in range(0, 8, 2):
            clave = spr.cstr(vals[k])
            valor = spr.cstr(vals[k + 1]) if vals[k + 1] else ""
            if clave:
                d[clave] = valor
        out[e["nombre"]] = d
    return out


def color_material(spr, material):
    for e in spr.seccion(b"MTRL"):
        if e["nombre"] == material:
            b = spr.bloque(e["data_offset"], e["data_size"])
            if len(b) >= 0x1C:
                return struct.unpack_from(">3f", b, 0x10)
    return (1.0, 1.0, 1.0)


# ---------------------------------------------------------------------------
# Color: HSV de UT -> tinte sobre la textura
# ---------------------------------------------------------------------------
def _rgb_a_hsv(rgb):
    mx = rgb.max(-1)
    mn = rgb.min(-1)
    d = mx - mn
    h = np.zeros_like(mx)
    nz = d > 1e-8
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    m = nz & (mx == r)
    h[m] = ((g[m] - b[m]) / d[m]) % 6.0
    m = nz & (mx == g)
    h[m] = (b[m] - r[m]) / d[m] + 2.0
    m = nz & (mx == b)
    h[m] = (r[m] - g[m]) / d[m] + 4.0
    h = h / 6.0
    s = np.where(mx > 1e-8, d / np.maximum(mx, 1e-8), 0.0)
    return h, s, mx


def _hsv_a_rgb(h, s, v):
    h = np.mod(h, 1.0)
    i = np.floor(h * 6.0).astype(int) % 6
    f = h * 6.0 - np.floor(h * 6.0)
    p = v * (1.0 - s)
    q = v * (1.0 - f * s)
    t = v * (1.0 - (1.0 - f) * s)
    r = np.choose(i, [v, q, p, p, t, v])
    g = np.choose(i, [t, v, v, q, p, p])
    b = np.choose(i, [p, p, t, v, v, q])
    return np.stack([r, g, b], -1)


def color_de_hsv(hsv):
    """(H,S,V) de UT -> color RGB de referencia (con la H invertida)."""
    if hsv is None:
        return None
    h, s, v = hsv
    if abs(h) < 1e-9 and abs(s) < 1e-9 and abs(v) < 1e-9:
        return None
    hd = math.degrees((-h) % (2.0 * math.pi))
    return colorsys.hsv_to_rgb(hd / 360.0, min(max(s, 0.0), 1.0), min(max(v + 0.6, 0.0), 1.0))


def _hex_rgb(s):
    s = s.strip().lstrip("#")
    if len(s) == 3:
        s = "".join(c * 2 for c in s)
    return tuple(int(s[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


def tintar_textura(img, hsv=None, rgb=None):
    """Tinta una textura conservando su detalle (surcos, lineas, sombreado).

    - hsv de UT: H absoluta invertida; S,V = ganancias sobre la textura.
    - rgb (color forzado): lleva la media de S y V al color pedido.
    """
    a = np.asarray(img.convert("RGBA")).astype(np.float32) / 255.0
    pix = a[..., :3]
    h, s, v = _rgb_a_hsv(pix)
    if rgb is not None:
        oh, os_, ov = colorsys.rgb_to_hsv(*rgb)
        sm = max(float(s.mean()), 0.05)
        vm = max(float(v.mean()), 0.05)
        h2 = np.full_like(h, oh)
        s2 = np.clip(s * (os_ / sm), 0.0, 1.0)
        v2 = np.clip(v * (ov / vm), 0.0, 1.0)
    elif hsv is not None and (abs(hsv[0]) + abs(hsv[1]) + abs(hsv[2]) > 1e-9):
        h2 = np.full_like(h, ((-hsv[0]) % (2.0 * math.pi)) / (2.0 * math.pi))
        s2 = np.clip(s * (1.0 + hsv[1]), 0.0, 1.0)
        v2 = np.clip(v * (1.0 + hsv[2]), 0.0, 1.0)
    else:
        return img
    valido = v > 0.02
    h2 = np.where(valido, h2, h)
    s2 = np.where(valido, s2, s)
    v2 = np.where(valido, v2, v)
    out = _hsv_a_rgb(h2, s2, v2)
    return Image.fromarray(np.clip(np.concatenate([out, a[..., 3:]], -1) * 255.0 + 0.5, 0, 255).astype(np.uint8), "RGBA")


FLAT_CACHE = {}


def textura_plana(rgb, lado=8):
    clave = tuple(round(float(x), 4) for x in rgb)
    if clave not in FLAT_CACHE:
        arr = np.zeros((lado, lado, 4), np.float32)
        arr[..., :3] = np.array(clave, np.float32)
        arr[..., 3] = 1.0
        FLAT_CACHE[clave] = arr
    return FLAT_CACHE[clave]


# ---------------------------------------------------------------------------
# UV originales (el lector anterior confundía tangentes y UV)
# ---------------------------------------------------------------------------
def _uv_generada(pos, zona, uv_ut=None, usar_ut=False):
    # Nombre histórico conservado para los consumidores del módulo. S1-F05-UT:
    # la fuente sí tiene UV; conservar incluso valores negativos y mayores de 1.
    if uv_ut is None:
        raise ValueError("faltan UV originales UT")
    return np.asarray(uv_ut, np.float64).copy()


# ---------------------------------------------------------------------------
# Modelo UT compatible con model_render
# ---------------------------------------------------------------------------
def tris_seguro(g):
    try:
        return g.triangulos()
    except Exception:
        return []


def es_grupo_base(nombre):
    n = nombre.lower()
    return not any(x in n for x in ("befor_", "before|", "after_", "after|", "_clone"))


class ModeloUT:
    def __init__(self, spr, vram, hsv, edge, colores, texturas=False, usar_uv_ut=False):
        from ut_cuerpo import escenas
        scene = escenas(spr)
        self.spr = spr
        self.hsv = hsv or {}
        self.edge = edge or {}
        self.colores = {k: _hex_rgb(v) for k, v in (colores or {}).items()}
        self.materiales = mapa_materiales(spr)
        self.labels = []
        self.pos, self.nrm, self.uv = [], [], []
        self.tris, self.tri_mat = [], []
        self.vbone = []
        self.mats = []
        self.tex = []
        idx_mat = {}
        idx_tex = {}
        self.zonas_usadas = set()

        def tex_de(material, zona):
            clave = (material, texturas)
            if clave in idx_tex:
                return idx_tex[clave]
            if texturas:
                info = self.materiales.get(material, {})
                nombre_tex = info.get("COLORMAP0") or info.get("COLORMAP")
                if nombre_tex and not any(t["nombre"] == nombre_tex for t in ut_formato.texturas(spr)):
                    nombre_tex = info.get("COLORMAP1") or nombre_tex
                img = ut_textura.extraer(spr, vram, nombre_tex) if nombre_tex else None
                if img is not None:
                    rgb = self.colores.get(zona)
                    img = tintar_textura(img, self.hsv.get(material), rgb)
                    arr = np.asarray(img).astype(np.float32) / 255.0
                else:
                    arr = textura_plana(self.colores.get(zona, (0.5, 0.5, 0.5))).copy()
            else:
                arr = textura_plana(self.colores.get(zona, (0.5, 0.5, 0.5))).copy()
            # alfa 0.55: model_render usa el alfa como banda de la rampa toon
            arr[..., 3] = 0.55
            self.tex.append(arr)
            idx_tex[clave] = len(self.tex) - 1
            return idx_tex[clave]

        for g in ut_formato.grupos(spr):
            sc = scene.get(g.nombre)
            if not sc or not sc["activo"]:
                continue
            material = sc["material"]
            if material == "aura_sword":
                continue
            vd = g.vertices()
            tr = tris_seguro(g)
            if not vd or not tr:
                continue
            zona = zona_de(material)
            self.zonas_usadas.add(zona)
            p = np.array([[v[0], v[1], v[2]] for v in vd], np.float64)
            n = np.array([[v[3], v[4], v[5]] for v in vd], np.float64)
            uv_ut = np.array([[v[6], v[7]] for v in vd], np.float64)
            uv = _uv_generada(p, zona, uv_ut, usar_uv_ut)
            off = len(self.pos)
            self.pos += list(p)
            self.nrm += list(n)
            self.uv += list(uv)
            self.vbone += [0] * len(p)
            self.tris += [(a + off, b + off, c + off) for a, b, c in tr]
            if material not in idx_mat:
                ti = tex_de(material, zona)
                dif = color_material(spr, material)
                self.mats.append({"tex": ti, "shader": -1, "diffuse": np.array(dif, np.float32),
                                  "env": False, "blend": material.upper().startswith(("G_SCOUTER", "A_POTARA")),
                                  "zona": zona, "material": material})
                idx_mat[material] = len(self.mats) - 1
            self.tri_mat += [idx_mat[material]] * len(tr)

        # rampa toon neutra (64x64): el color = textura - rampa
        if self.tex:
            ramp = np.zeros((64, 64, 4), np.float32)
            for x in range(64):
                k = (1.0 - x / 63.0) * 0.38
                ramp[:, x, :3] = k
            ramp[..., 3] = 1.0
            self.tex.append(ramp)
            ramp_i = len(self.tex) - 1
            for m in self.mats:
                m["shader"] = ramp_i
        self.pos = np.array(self.pos, np.float64)
        self.nrm = np.array(self.nrm, np.float64)
        self.uv = np.array(self.uv, np.float64)
        self.tris = np.array(self.tris, int)
        self.tri_mat = np.array(self.tri_mat, int)
        self.vbone = np.array(self.vbone, int)

    def posed(self, pose=None):
        return self.pos.copy(), self.nrm.copy()

    def info(self):
        byz = {}
        for i, m in enumerate(self.mats):
            z = m["zona"]
            byz.setdefault(z, {"materiales": [], "tris": 0})
            byz[z]["materiales"].append(m["material"])
            byz[z]["tris"] += int((self.tri_mat == i).sum())
        return byz


# ---------------------------------------------------------------------------
# Render
# ---------------------------------------------------------------------------
def tira(model, size, giros, pitch=6.0, light=(-0.45, 0.55, 0.70), fondo=(28, 32, 40)):
    import model_render as mr
    P, _ = model.posed()
    usados = np.unique(model.tris.reshape(-1)) if len(model.tris) else np.arange(len(P))
    lo, hi = P[usados].min(0), P[usados].max(0)
    ctr = (lo + hi) / 2
    alto = max(hi[1] - lo[1], 1e-6)
    ancho = max(np.linalg.norm([hi[0] - lo[0], hi[2] - lo[2]]), 1e-6)
    esc = min(size * 0.88 / alto, size * 0.94 / ancho)
    out = Image.new("RGB", (size * giros, size), fondo)
    for k in range(giros):
        yaw = 360.0 * k / max(giros, 1)
        view = mr.rot_x(pitch) @ mr.rot_y(yaw)
        img = mr.render(model, (size, size), view, ctr, esc, outline=1.3, ss=2, light=light)
        out.paste(Image.fromarray(img).convert("RGB"), (size * k, 0), Image.fromarray(img).split()[-1])
    return out


def tira_con_leyenda(model, size, giros, colores, titulo=""):
    img = tira(model, size, giros)
    alto_ley = 86
    out = Image.new("RGB", (img.width, img.height + alto_ley), (22, 26, 33))
    out.paste(img, (0, 0))
    d = ImageDraw.Draw(out)
    if titulo:
        d.text((10, img.height + 6), titulo, fill=(230, 230, 230))
    x = 10
    y = img.height + 30
    for clave, etiqueta, _ in ZONAS:
        if clave not in model.zonas_usadas and clave not in colores:
            continue
        rgb = tuple(int(c * 255) for c in _hex_rgb(colores.get(clave, COLORES_DEF.get(clave, "#888888"))))
        d.rectangle([x, y, x + 14, y + 14], fill=rgb, outline=(255, 255, 255))
        d.text((x + 20, y + 1), etiqueta, fill=(220, 220, 220))
        x += 20 + 7 * len(etiqueta) + 22
        if x > out.width - 120:
            x = 10
            y += 20
    return out


# ---------------------------------------------------------------------------
# Tabla de zonas / medidas
# ---------------------------------------------------------------------------
def tabla_zonas(spr, hsv, edge, b3=None):
    mats = mapa_materiales(spr)
    texs = {t["nombre"]: t for t in ut_formato.texturas(spr)}
    grupos = {}
    for g in ut_formato.grupos(spr):
        if not es_grupo_base(g.nombre):
            continue
        m = g.hueso_del_nombre()
        if m == "aura_sword":
            continue
        d = grupos.setdefault(m, {"nv": 0, "nt": 0, "uv": False})
        d["nv"] += g.vertex_count
        d["nt"] += len(tris_seguro(g))
        d["uv"] = d["uv"] or any(x["usage"] == 0x05 for x in g.decls)
    filas = []
    for m in sorted(grupos):
        z = zona_de(m)
        info = mats.get(m, {})
        tn = info.get("COLORMAP0") or info.get("COLORMAP") or ""
        if tn and tn not in texs:
            tn = info.get("COLORMAP1") or tn
        t = texs.get(tn)
        h = hsv.get(m)
        filas.append({
            "material": m, "zona": z, "etiqueta": etiqueta_zona(z),
            "textura": tn, "tex_w": t["w"] if t else 0, "tex_h": t["h"] if t else 0,
            "hsv": [round(x, 4) for x in h] if h else None,
            "color": ("#%02X%02X%02X" % tuple(int(round(c * 255)) for c in color_de_hsv(h))) if h and color_de_hsv(h) else None,
            "edge": edge.get(m), "nv": grupos[m]["nv"], "nt": grupos[m]["nt"],
            "uv_decl": grupos[m]["uv"],
        })
    resumen = {
        "materiales": len(filas),
        "tx2d": len(texs),
        "zonas": {},
        "tri_sin_uv": sum(f["nt"] for f in filas if not f["uv_decl"]),
        "tri_total": sum(f["nt"] for f in filas),
    }
    for f in filas:
        z = resumen["zonas"].setdefault(f["zona"], {"materiales": [], "texturas": [], "tri": 0})
        z["materiales"].append(f["material"])
        if f["textura"] and f["textura"] not in z["texturas"]:
            z["texturas"].append(f["textura"])
        z["tri"] += f["nt"]
    if b3 and os.path.exists(b3):
        import model_render as mr
        m = mr.Model(b3)
        uv = m.uv[m.tris]
        zero = (np.abs(uv) < 1e-6).all(2).all(1)
        resumen["b3"] = {
            "tris": int(len(m.tris)), "materiales": int(len(m.mats)),
            "materiales_usados": sorted(set(int(x) for x in m.tri_mat)),
            "texturas": int(len(m.tex)),
            "tri_uv_cero": int(zero.sum()), "pct_uv_cero": round(100.0 * float(zero.mean()), 1),
            "uv_min": [round(float(x), 3) for x in m.uv.min(0)],
            "uv_max": [round(float(x), 3) for x in m.uv.max(0)],
        }
    return filas, resumen


def md_tabla(filas, resumen, titulo="Color por parte (UT)"):
    lines = ["## " + titulo, ""]
    lines.append("Materiales: **%d**; texturas TX2D: **%d**; triangulos con UV declarada: **%d/%d** (sin UV: **%.1f %%**)." % (
        resumen["materiales"], resumen["tx2d"],
        resumen["tri_total"] - resumen["tri_sin_uv"], resumen["tri_total"],
        100.0 * resumen["tri_sin_uv"] / max(resumen["tri_total"], 1)))
    if "b3" in resumen:
        b = resumen["b3"]
        lines.append("")
        lines.append("B3 convertido actual: **%d materiales** (usados %s), **%d texturas**, "
                     "**%d/%d triangulos con UV=(0,0)** (%.1f %%), UV [%.2f..%.2f]." % (
                         b["materiales"], b["materiales_usados"], b["texturas"],
                         b["tri_uv_cero"], b["tris"], b["pct_uv_cero"], b["uv_min"][0], b["uv_max"][0]))
    lines.append("")
    lines.append("| Material | Zona | Textura (COLORMAP0) | Tam | HSV (H rad, S, V) | Borde | Vert. | Tris | UV? |")
    lines.append("|---|---|---|---|---|---|---|---|---|")
    for f in filas:
        h = "(%g, %g, %g)" % tuple(f["hsv"]) if f["hsv"] else "-"
        lines.append("| %s | %s | %s | %s | %s | %s | %d | %d | %s |" % (
            f["material"], f["etiqueta"], f["textura"] or "-",
            "%dx%d" % (f["tex_w"], f["tex_h"]) if f["tex_w"] else "-", h,
            f["edge"] if f["edge"] is not None else "-", f["nv"], f["nt"],
            "si" if f["uv_decl"] else "NO"))
    lines.append("")
    lines.append("### Zonas")
    lines.append("")
    lines.append("| Zona | Materiales | Texturas propias | Tris |")
    lines.append("|---|---|---|---|")
    for z, d in sorted(resumen["zonas"].items()):
        lines.append("| %s | %s | %s | %d |" % (
            etiqueta_zona(z), ", ".join(d["materiales"]),
            ", ".join(d["texturas"]) or "-", d["tri"]))
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    z = sub.add_parser("zonas", help="tabla material -> zona -> textura")
    z.add_argument("--spr", required=True)
    z.add_argument("--ioram", default=None)
    z.add_argument("--hsv", required=True)
    z.add_argument("--edge", default=None)
    z.add_argument("--b3", default=None)
    z.add_argument("--md", default=None)
    z.add_argument("--json", default=None)

    r = sub.add_parser("render", help="tira de giros con colores por zona")
    r.add_argument("--spr", required=True)
    r.add_argument("--ioram", default=None)
    r.add_argument("--vram", default=None)
    r.add_argument("--hsv", required=True)
    r.add_argument("--edge", default=None)
    r.add_argument("--out", required=True)
    r.add_argument("--giros", type=int, default=6)
    r.add_argument("--size", type=int, default=320)
    r.add_argument("--texturas", action="store_true", help="usar las texturas UT tintadas (si no, color plano por zona)")
    r.add_argument("--uv-ut", action="store_true", help="usar las UV declaradas cuando existan (en vez de la proyeccion de reconstruccion)")
    r.add_argument("--colores", default=None, help='p. ej. "piel=#E8B48C,arriba=#C0392B,abajo=#2E5FA3,calzado=#F1C40F"')
    r.add_argument("--titulo", default="")

    m = sub.add_parser("medir", help="tabla + resumen (y comparacion con el B3 convertido)")
    m.add_argument("--spr", required=True)
    m.add_argument("--ioram", default=None)
    m.add_argument("--hsv", required=True)
    m.add_argument("--edge", default=None)
    m.add_argument("--b3", default=None)
    m.add_argument("--md", default=None)
    m.add_argument("--json", default=None)

    b = sub.add_parser("b3", help="render del B3 convertido actual (para ver el problema)")
    b.add_argument("--b3", required=True)
    b.add_argument("--out", required=True)
    b.add_argument("--giros", type=int, default=4)
    b.add_argument("--size", type=int, default=320)

    a = ap.parse_args()
    if a.cmd in ("zonas", "medir", "render"):
        ioram = getattr(a, "ioram", None)
        if not ioram:
            cand = a.spr.replace(".spr", ".ioram")
            if os.path.exists(cand):
                ioram = cand
        spr = ut_formato.Spr(open(a.spr, "rb").read(), open(ioram, "rb").read() if ioram else b"")
        hsv = leer_hsv(a.hsv)
        edge = leer_edge(a.edge) if getattr(a, "edge", None) else {}
        if a.cmd in ("zonas", "medir"):
            filas, resumen = tabla_zonas(spr, hsv, edge, getattr(a, "b3", None))
            txt = md_tabla(filas, resumen)
            if a.md:
                os.makedirs(os.path.dirname(os.path.abspath(a.md)), exist_ok=True)
                open(a.md, "w", encoding="utf-8").write(txt)
            if a.json:
                os.makedirs(os.path.dirname(os.path.abspath(a.json)), exist_ok=True)
                json.dump({"filas": filas, "resumen": resumen}, open(a.json, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
            print(txt)
            return 0
        colores = dict(COLORES_DEF)
        if a.colores:
            for par in a.colores.split(","):
                if "=" in par:
                    k, v = par.split("=", 1)
                    colores[k.strip()] = v.strip()
        vram = open(a.vram, "rb").read() if a.vram else b""
        if a.texturas and not vram:
            print("[X] --texturas necesita --vram")
            return 2
        modelo = ModeloUT(spr, vram, hsv, edge, colores, texturas=a.texturas, usar_uv_ut=a.uv_ut)
        img = tira_con_leyenda(modelo, a.size, max(1, a.giros), colores, a.titulo)
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        img.save(a.out)
        print("->", a.out, img.size, "materiales", len(modelo.mats), "tris", len(modelo.tris))
        return 0
    if a.cmd == "b3":
        import model_render as mr
        modelo = mr.Model(a.b3)
        img = tira(modelo, a.size, max(1, a.giros))
        out = Image.new("RGB", (img.width, img.height + 28), (22, 26, 33))
        out.paste(img, (0, 0))
        ImageDraw.Draw(out).text((8, img.height + 8), "B3 convertido: 1 material / 1 textura / UV=(0,0) en la mayoria", fill=(230, 230, 230))
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        out.save(a.out)
        print("->", a.out, out.size, "tris", len(modelo.tris))
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())



