#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tactics_fx.py - Textura y color de la tecnica de energia de cada unidad de Dragon Tactics, LEIDOS DE data_cmn.afs del jugador
(no hay arte nuevo: solo se anotan la entrada y el hash de cada textura; el juego las extrae en tiempo de ejecucion de la copia del jugador).

Como las liga el juego nativo (medido con colores.py / sb_tecnicas.py):
  capsula de ataque (roster_db "skills.ataques") -> #CCM de la cam -> codigos de ataque -> lineas AP7 clase 4 del #CSK de su anm
  -> codigo de efecto -> registro de 0xF0 B del #CST del BSP del personaje (entrada "bsp" de data_cmn):
     +0x10 u16 tipo, +0x12 u16 codigo, +0x48 u16 textura de la BOLA y +0x4A u16 textura del RAYO (indices del #AZT grande del BSP),
     enlaces a #ACE (+0x34, +0x5C, +0x68, +0x78, +0xB4) con las particulas: color RGBA f32 en +0x80/+0x90 de cada nodo, textura en +0xA4.
  Tipo 0 = bola + rayo con textura propia (ya coloreada: se dibuja sin tinte). Tipos 1/2/3/12 = solo particulas (se toma su textura de bola y su color).

  python tactics_fx.py generar     # escribe ../src/tactics/fx_nativo.inc
  python tactics_fx.py informe     # tabla legible por unidad
  python tactics_fx.py hoja        # D:/tmp/subTAC/fx/hoja.png (texturas elegidas por unidad)
"""
import json
import os
import re
import struct
import sys
import tomllib

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(AQUI)
sys.path[:0] = [AQUI, os.path.join(RAIZ, "awo_tools")]
SALIDA = os.path.join(RAIZ, "src", "tactics", "fx_nativo.inc")


def _carga():
    import roster_build as rb  # noqa: PLC0415
    work = rb.work_dir("tfx_")
    return rb, rb.Afs(os.path.join(rb.default_us(), "data_cmn.afs"), work)


def _dds(z, k):
    """(hash XXH3 del nivel base como native_ui, ancho, alto, bytes DDS) de la textura k del #AZT, o None."""
    import xxhash  # noqa: PLC0415
    from sb_tecnicas import azt_entries  # noqa: PLC0415
    ents = azt_entries(z)
    if k <= 0 or k >= len(ents) or not ents[k]:
        return None
    do, dl = struct.unpack_from(">II", z, ents[k] + 0x14)
    d = bytes(z[do:do + dl])
    if d[:4] != b"DDS ":
        return None
    h, w = struct.unpack_from("<II", d, 12)
    flags, bits = struct.unpack_from("<I", d, 80)[0], struct.unpack_from("<I", d, 88)[0]
    fcc = d[84:88]
    if flags & 4 and fcc in (b"DXT3", b"DXT5"):
        size = ((w + 3) // 4) * ((h + 3) // 4) * 16
    elif not flags & 4 and bits == 32:
        size = w * h * 4
    else:
        return None                                     # native_ui.cpp solo entiende RGBA8, DXT3 y DXT5
    return xxhash.xxh3_64_intdigest(d[128:128 + size]), w, h, d


def _color_ace(a):
    """Color medio (RGB 0..1, normalizado al canal mayor) de las particulas con textura de un #ACE, ponderado por saturacion."""
    import colores  # noqa: PLC0415
    acc, tot = [0.0, 0.0, 0.0], 0.0
    for o in colores._ace_nodes(a):
        if struct.unpack_from(">H", a, o + 4)[0] != 2:
            continue
        cols = struct.unpack_from(">8f", a, o + 0x80)
        for rgb in (cols[0:3], cols[4:7]):
            if not all(0.0 <= x <= 1.01 for x in rgb):
                continue
            w = (max(rgb) - min(rgb)) + 0.02
            for i in range(3):
                acc[i] += rgb[i] * w
            tot += w
    return [x / tot for x in acc] if tot else None


def _color_textura(d):
    """Color dominante de una textura DDS (pixeles opacos y saturados), normalizado al canal mayor."""
    import io  # noqa: PLC0415
    import numpy as np  # noqa: PLC0415
    from PIL import Image  # noqa: PLC0415
    px = np.array(Image.open(io.BytesIO(d)).convert("RGBA")).reshape(-1, 4).astype(float) / 255.0
    px = px[px[:, 3] > 0.25]
    if not len(px):
        return None
    sat = px[:, :3].max(1) - px[:, :3].min(1)
    w = sat ** 2 + 1e-4
    c = (px[:, :3] * w[:, None]).sum(0) / w.sum()
    return list(c / max(c.max(), 1e-3))


def _alfa(d):
    import io  # noqa: PLC0415
    import numpy as np  # noqa: PLC0415
    from PIL import Image  # noqa: PLC0415
    return np.array(Image.open(io.BytesIO(d)).convert("RGBA"))[..., 3].astype(float) / 255.0


def es_bola(d):
    """Resplandor redondo: centro opaco, esquinas transparentes y centro de masa en el medio (descarta humo, garras, chispas en mosaico)."""
    import numpy as np  # noqa: PLC0415
    a = _alfa(d)
    h, w = a.shape
    cy, cx = a[h * 3 // 8:h * 5 // 8, w * 3 // 8:w * 5 // 8].mean(), 0
    esq = np.concatenate([a[:h // 6, :w // 6].ravel(), a[:h // 6, -w // 6:].ravel(), a[-h // 6:, :w // 6].ravel(), a[-h // 6:, -w // 6:].ravel()]).mean()
    ys, xs = np.mgrid[0:h, 0:w]
    m = a.sum() + 1e-6
    off = max(abs((a * xs).sum() / m / w - 0.5), abs((a * ys).sum() / m / h - 0.5))
    return cy > 0.6 and esq < 0.12 and off < 0.06


def es_rayo(d):
    """Tira de rayo: bandas a lo largo del eje x (columnas parecidas entre si, filas distintas) en alfa y en luminancia; descarta nubes y manchas."""
    import io  # noqa: PLC0415
    import numpy as np  # noqa: PLC0415
    from PIL import Image  # noqa: PLC0415
    a = _alfa(d)
    lum = np.array(Image.open(io.BytesIO(d)).convert("RGB")).astype(float).mean(2) / 255.0
    return (a.mean() > 0.15 and a.mean(1).std() > 2.0 * a.mean(0).std() and a.mean(0).std() < 0.12
            and lum.mean(1).std() > 10.0 * max(lum.mean(0).std(), 1e-3))


def analizar():
    """{id unidad: informe} para las unidades de tactics_manifest.toml (id base, sin '_2' de estrellas)."""
    import colores  # noqa: PLC0415
    from sb_tecnicas import azt_entries, kids  # noqa: PLC0415
    rb, cmn = _carga()
    db = json.load(open(os.path.join(AQUI, "roster_db.json"), encoding="utf-8"))
    por_id = {x["id"]: x for x in db["ids"]}
    man = tomllib.load(open(os.path.join(AQUI, "tactics_manifest.toml"), "rb"))
    res = {}
    for u in man["unidad"]:
        if re.search(r"_\d$", u["id"]):
            continue
        e = por_id[u["b3"]]
        info = {"b3": u["b3"], "bsp": e["bsp"], "capsula": None, "tipo": None, "bola": None, "rayo": None, "color": None, "origen": "ninguno"}
        res[u["id"]] = info
        try:
            bsp = cmn.entry(e["bsp"])
            anms = [cmn.entry(f) for f in dict.fromkeys(e["anm"]) if 0 < f < 0xFFFFFFFF]
            cam = cmn.entry(e["cam"])
            top = kids(bsp, ">")
            amb = [list(kids(top[0][0], ">")), list(kids(top[1][0], ">"))]
            zs = [x for x, _ in top if x[:4] == b"#AZT"] + [x for a in (0, 1) for x, _ in amb[a] if x[:4] == b"#AZT"]
            z = max(zs, key=lambda x: len(azt_entries(x)))
            cst = amb[0][0][0]
            n, s0 = struct.unpack_from(">II", cst, 8)
        except Exception as ex:  # noqa: BLE001
            info["error"] = str(ex)
            continue
        cand = []   # (prioridad, capsula, tipo, bola, rayo, color)
        for cap in e["skills"]["ataques"]:
            try:
                ast, _ = colores.efectos_capsula(anms, cam, cap, bsp)
            except Exception:  # noqa: BLE001
                continue
            for k in range(n):
                o = s0 + k * 0xF0
                tipo, cod = struct.unpack_from(">HH", cst, o + 0x10)
                if cod not in ast:
                    continue
                tb, tr = struct.unpack_from(">HH", cst, o + 0x48)
                bola, rayo = _dds(z, tb), _dds(z, tr)
                if tipo == 0 and (bola or rayo):
                    col = _color_textura((rayo or bola)[3])
                    cand.append((0, cap, tipo, bola, rayo, col))
                    continue
                # particulas: primer nodo con textura del enlace 0x5C (la bola) y color medio de ese #ACE
                src, idx = struct.unpack_from(">HH", cst, o + 0x5C)
                if src == 2 and idx < len(amb[0]) and amb[0][idx][0][:4] == b"#ACE":
                    a = amb[0][idx][0]
                    tx = None
                    for no in colores._ace_nodes(a):
                        if struct.unpack_from(">H", a, no + 4)[0] == 2:
                            cnt, t = struct.unpack_from(">II", a, no + 0xA0)
                            if cnt == 1 and t < 0x100 and _dds(z, t):
                                tx = _dds(z, t)
                                break
                    col = _color_ace(a)
                    if col:
                        cand.append((1, cap, tipo, tx, None, col))
        if cand:
            cand.sort(key=lambda c: c[0])
            _, cap, tipo, bola, rayo, col = cand[0]
            info.update(capsula=cap, tipo=tipo, color=[round(float(x), 3) for x in col] if col else None, origen="textura" if cand[0][0] == 0 else "particulas")
            if cand[0][0] == 1:
                bola = None                       # particulas: sus texturas son humo, chispas o llamas sueltas, no una bola: solo se toma su color
            elif bola and not es_bola(bola[3]):
                bola = None
            if rayo and not es_rayo(rayo[3]):
                rayo = None
            for nom, t in (("bola", bola), ("rayo", rayo)):
                if t:
                    info[nom] = {"hash": t[0], "w": t[1], "h": t[2]}
            info["_dds"] = {"bola": bola[3] if bola else None, "rayo": rayo[3] if rayo else None}
            info["color"] = [float(x) for x in info["color"]] if info["color"] else None
    return res


def generar(res):
    lin = ["// Generado por mod center hd/tactics_fx.py (no editar a mano): textura y color de la tecnica de energia de cada unidad de Dragon Tactics,",
           "// leidos de data_cmn.afs del jugador. Solo entrada de data_cmn (BSP del personaje), hash XXH3 de la textura y color: ninguna imagen.",
           "// TKFX(id, entrada_bsp, hash_bola, hash_rayo, r, g, b)   hash 0 = sin esa textura; color 0,0,0 = sin dato (se usa el de ColorKi)."]
    for uid, i in res.items():
        if i["color"] is None and not i["bola"] and not i["rayo"]:
            continue
        if not i["bola"] and not i["rayo"] and max(i["color"]) - min(i["color"]) < 0.15:
            continue
        c = i["color"] or [0, 0, 0]
        if max(c) - min(c) < 0.15:
            c = [0, 0, 0]                                   # casi blanco (humo): sin color propio, vale el de ColorKi
        lin.append('TKFX("%s", %d, 0x%016Xull, 0x%016Xull, %.3ff, %.3ff, %.3ff)' % (
            uid, i["bsp"], i["bola"]["hash"] if i["bola"] else 0, i["rayo"]["hash"] if i["rayo"] else 0, c[0], c[1], c[2]))
    with open(SALIDA, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lin) + "\n")
    return len(lin) - 3


def hoja(res, ruta):
    import io  # noqa: PLC0415
    from PIL import Image, ImageDraw  # noqa: PLC0415
    filas = [(u, i) for u, i in res.items() if i.get("_dds")]
    S = 96
    img = Image.new("RGB", (S * 2 + 330, S * len(filas)), (40, 40, 40))
    dr = ImageDraw.Draw(img)
    for r, (u, i) in enumerate(filas):
        for c, nom in enumerate(("bola", "rayo")):
            d = i["_dds"][nom]
            if d:
                t = Image.open(io.BytesIO(d)).convert("RGBA").resize((S, S))
                fondo = Image.new("RGBA", (S, S), (20, 20, 20, 255))
                fondo.alpha_composite(t)
                img.paste(fondo.convert("RGB"), (c * S, r * S))
        col = i["color"] or [0, 0, 0]
        dr.rectangle([S * 2 + 4, r * S + 4, S * 2 + 40, r * S + 40], fill=tuple(int(x * 255) for x in col))
        dr.text((S * 2 + 50, r * S + 6), "%s cap%s tipo%s %s" % (u, i["capsula"], i["tipo"], i["origen"]), fill=(255, 255, 255))
    img.save(ruta)


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "informe"
    res = analizar()
    if cmd == "generar":
        print("escritas %d unidades en %s" % (generar(res), SALIDA))
    elif cmd == "hoja":
        hoja(res, sys.argv[2] if len(sys.argv) > 2 else "D:/tmp/subTAC/fx/hoja.png")
    for u, i in res.items():
        print(u, {k: v for k, v in i.items() if k != "_dds"})


if __name__ == "__main__":
    main()
