#!/usr/bin/env python3
"""OBJ/MTL -> rig automático Blender -> personaje opcional de B3 HD.

No instala ni activa mods. Conserva fuente, informe, .blend y renders en --trabajo.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
from datetime import datetime
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / "awo_tools"))


def escribir_modelo(tpl, mesh, out):
    import numpy as np
    import amo2awo as A
    import sdbh_model as S
    from PIL import Image

    W = [np.array(m, dtype=float) for m in mesh["world"]]
    if len(W) != tpl.nb or any(m.shape != (4, 4) or not np.isfinite(m).all() for m in W):
        raise ValueError("Esqueleto incompleto o con coordenadas no finitas")
    if not mesh["vertices"] or not mesh["triangles"]:
        raise ValueError("La malla no contiene triángulos exportables")
    for v in mesh["vertices"]:
        b, weight = int(v["bone"]), float(v["weight"])
        if not 0 <= b < tpl.nb or not 0 < weight <= 1 or (weight < 1 and tpl.parent[b] < 0):
            raise ValueError("Peso o hueso incompatible con la mezcla padre-hijo de B3")
        for key, size in (("pos", 3), ("normal", 3), ("uv", 2)):
            values = np.asarray(v[key], dtype=float)
            if values.shape != (size,) or not np.isfinite(values).all():
                raise ValueError("Vértice con " + key + " inválido")
    for tri in mesh["triangles"]:
        if len(tri) != 3 or any(not isinstance(i, int) or not 0 <= i < len(mesh["vertices"]) for i in tri):
            raise ValueError("Índice de triángulo fuera de la malla")
    axes = []
    for i, raw in enumerate(tpl.axes):
        L = np.linalg.inv(W[tpl.parent[i]]) @ W[i] if tpl.parent[i] >= 0 else W[i]
        q = S.mat_q(L[:3, :3])
        if np.dot(q, S._bef(raw, 0, 4)) < 0:
            q = -q
        ax = bytearray(raw)
        struct.pack_into(">4f", ax, 0, *q)
        struct.pack_into(">3f", ax, 16, *L[:3, 3])
        axes.append(ax)
    palette = list(tpl.palette)
    for bi in sorted({int(v["bone"]) for v in mesh["vertices"]}):
        if bi not in palette:
            palette.append(bi)
    if len(palette) > 128:
        raise ValueError("La paleta supera los 128 slots admitidos")
    slots = {b: k for k, b in enumerate(palette) if b != 0xFFFFFFFF}
    inv = [np.linalg.inv(m) for m in W]
    # Mallas sin rig (Tripo y similares) llegan a veces con triángulos girados al
    # revés respecto a sus propias normales: B3 descarta la cara trasera y se ven
    # huecos o manchas oscuras (cara y coronilla en NovaPowers). Se corrige solo el
    # orden del triángulo; posiciones, UV y normales quedan intactas.
    P = np.array([v["pos"] for v in mesh["vertices"]], dtype=float)
    Nn = np.array([v["normal"] for v in mesh["vertices"]], dtype=float)
    triangles, reorientados = [], 0
    for a, b, c in mesh["triangles"]:
        g = np.cross(P[b] - P[a], P[c] - P[a])
        if np.linalg.norm(g) > 1e-9:
            g = g / np.linalg.norm(g)
            if sum(n @ g < 0 for n in (Nn[a], Nn[b], Nn[c])) >= 2:
                a, c = c, a
                reorientados += 1
        triangles.append([a, b, c])
    win, pts = [], []
    cloth_colors = sorted({tuple(v["color_sin_rim"]) for v in mesh["vertices"] if "color_sin_rim" in v})
    if len(cloth_colors) > 64:
        raise ValueError("La paleta de planos oscuros supera los 64 colores")
    cloth_tiles = {color: i for i, color in enumerate(cloth_colors)}
    max_error = 0.0
    for v in mesh["vertices"]:
        b, w = int(v["bone"]), float(v["weight"])
        p, n = np.array(v["pos"]), np.array(v["normal"])
        loc = inv[b][:3, :3] @ p + inv[b][:3, 3]
        norm = inv[b][:3, :3] @ n
        # Cabello negro: el VS nativo anula el brillo de borde con normal nula.
        # Conservar n hasta aquí permite corregir antes el winding de esas caras.
        if v.get("sin_luz", False) or v.get("sin_rim", False):
            norm[:] = 0
        uv = v["uv"]
        if v.get("sin_luz", False):
            # Tres negros propios distinguen planos de mechones sin reintroducir
            # brillo blanco. Se muestrea el centro de cada baldosa, sin costuras.
            light = float(n @ np.array([.35, .8, .48]))
            tile = 2 if light > .65 else 1 if light > .15 else 0
            uv = [(tile * 20 + 10) / 64, .5]
        elif "color_sin_rim" in v:
            tile = cloth_tiles[tuple(v["color_sin_rim"])]
            uv = [(tile % 8 * 8 + 4) / 64, (tile // 8 * 8 + 4) / 64]
        win.append(A.win_bytes(loc.tolist(), w, slots[b], norm.tolist(), None, uv))
        pts.append(p)
        max_error = max(max_error, float(np.linalg.norm(W[b][:3, :3] @ loc + W[b][:3, 3] - p)))
    rgba = np.array(Image.open(mesh["texture"]).convert("RGBA"))
    # B3 usa alfa como selector de sombreado, Blender como transparencia.
    # Mantener PNG/retrato opacos y convertir SOLO al escribir la textura HD.
    rgba[..., 3] = 0
    if any(v.get("sin_rim", False) for v in mesh["vertices"]):
        # N=0 tambien fija la coordenada toon. En los pigmentos enmascarados
        # conservar la base con el alfa nativo "sin sombrear", o el gi se ennegrece.
        rgba[..., 3][rgba[..., :3].max(axis=2) < 64] = 255
    h, w = rgba.shape[:2]
    textures = [(w, h, 0, S.dds_dxt3(w, h) + S.encode_dxt3(rgba))]
    # Una base de color necesita la rampa de ROPA, no la de piel (complementaria
    # azulada). Elegir la más frecuente mezclaba el tinte de piel con todo el gi.
    ri = tpl.ramp_of(lambda lab: "BODY" in lab)
    if ri is None:
        ri = tpl.ramp_of(lambda _: True)
    if ri is None or tpl.textures[ri] is None:
        raise ValueError("El donante no contiene una rampa toon nativa utilizable")
    textures.append(tpl.textures[ri])
    # El negro de la cabeza usa una textura PROPIA de tres tonos y material aparte.
    # Una isla UV contigua a ojos/blancos no puede así dejar puntos blancos en
    # una punta. No se copia geometría del juego ni se repinta el archivo de salida.
    groups = [[], [], []]
    for tri in triangles:
        group = (1 if all(mesh["vertices"][i].get("sin_luz", False) for i in tri) else
                 2 if all("color_sin_rim" in mesh["vertices"][i] for i in tri) else 0)
        groups[group].append(tri)
    pieces = []
    for material, tris in enumerate(groups):
        if not tris:
            continue
        used = sorted({i for tri in tris for i in tri})
        remap = {old: new for new, old in enumerate(used)}
        pieces.append({"skinned": True, "mat": material, "win": [win[i] for i in used],
                       "tris": [[remap[i] for i in tri] for tri in tris], "pts": np.array([pts[i] for i in used]),
                       "label": tpl.labels[0], "nmax": max(slots)})
    parts = {0: pieces}
    materials = [S.material(True, 0, 1)]
    if groups[1] or groups[2]:
        # B5 necesita un sampler ligado incluso con normales nulas. FFFFFFFF
        # dejaba en algunos ángulos la rampa del draw anterior (pelo verde/blanco).
        black = np.zeros((64, 64, 4), dtype=np.uint8)
        black[..., :3] = [8, 10, 16]
        black[:, 20:40, :3] = [15, 18, 26]
        black[:, 40:, :3] = [22, 25, 34]
        black[..., 3] = 255
        textures.append((64, 64, 0, S.dds_dxt3(64, 64) + S.encode_dxt3(black)))
        materials.append(S.material(True, 2, 1))
    if groups[2]:
        cloth = np.zeros((64, 64, 4), dtype=np.uint8)
        cloth[..., 3] = 255
        for color, tile in cloth_tiles.items():
            x, y = tile % 8 * 8, tile // 8 * 8
            cloth[y:y+8, x:x+8, :3] = color
        textures.append((64, 64, 0, S.dds_dxt3(64, 64) + S.encode_dxt3(cloth)))
        materials.append(S.material(True, 3, 1))
    raw, ax, stats = S.awg_bytes(tpl.names, axes, tpl.links, tpl.word0c, 0,
                                parts, materials, palette)
    lists = [[(0, b, b)] * tpl.nlist for b in range(tpl.nb)]
    awo = S.awo_bytes(tpl, [(raw, ax)], lists, tpl.nlist)
    azt = S.build_azt(textures)
    if len(azt) > 1200000:
        raise ValueError("El atlas supera el presupuesto AZT de 1,2 MB (1024 + rampa)")
    out.write_bytes(S.amb_pack([(awo, tpl.kid_types.get(b"#AWO", 1)),
                               (azt, tpl.kid_types.get(b"#AZT", 2))]))
    errors = S.verify(str(out))
    if errors or max_error > 1e-5:
        raise ValueError("Verificación HD: " + repr(errors) + "; error bind=" + str(max_error))
    return dict(stats, bytes=out.stat().st_size, azt=len(azt),
                 bind_error=max_error, palette=palette, verificacion="PASS",
                  vertices_sin_brillo=sum(bool(v.get("sin_luz") or v.get("sin_rim")) for v in mesh["vertices"]),
                  mascara_rim="normales_nulas_solo_pigmentos_oscuros; piel_conservada",
                triangulos_reorientados=reorientados)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("obj", type=Path)
    ap.add_argument("--nombre", required=True)
    ap.add_argument("--donante", type=int, required=True, help="ID del personaje B3, no número de bin")
    ap.add_argument("--mods", type=Path, help="carpeta de salida opcional; no se activa automáticamente")
    ap.add_argument("--trabajo", type=Path, default=Path(os.environ.get("DBZ3_OBJ_TRABAJO", "E:/DBZ3HD_trabajo/obj_import")))
    ap.add_argument("--blender", type=Path, default=Path(os.environ.get("BLENDER_EXE", "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe")))
    ap.add_argument("--us", type=Path)
    ap.add_argument("--arriba", choices=("auto", "y", "z"), default="auto")
    ap.add_argument("--giro", type=float, default=0, help="giro adicional sobre el eje vertical, grados")
    ap.add_argument("--triangulos", type=int, default=9000)
    ap.add_argument("--forma", choices=("fuente", "refinada"), default="fuente",
                    help="refinada: cabeza algo más ancha y alta, con cuello anclado al OBJ")
    ap.add_argument("--textura", type=int, choices=(128, 256, 512, 1024), default=1024,
                    help="resolución del atlas de color (1024 por defecto: la cara pierde ojos y cejas a 512)")
    ap.add_argument("--acabado", choices=("cel", "original"), default="cel",
                    help="cel limpia iluminación pintada; original conserva el color fuente")
    ap.add_argument("--landmarks", type=Path, help="JSON opcional: posiciones normalizadas B3 de articulaciones")
    a = ap.parse_args(argv)
    if not a.obj.is_file() or a.obj.suffix.lower() != ".obj":
        ap.error("se necesita un OBJ existente")
    if not a.blender.is_file():
        ap.error("no encuentro Blender; indica --blender RUTA")
    if not 500 <= a.triangulos <= 12000:
        ap.error("--triangulos debe estar entre 500 y 12000")
    import roster_build as R
    import afs_pair
    import sdbh_model as S
    donor = next((d for d in R.DB["ids"] if d["id"] == a.donante), None)
    if not donor or not donor.get("models"):
        ap.error("donante sin modelo B3 en el catálogo")
    slug = re.sub(r"[^a-z0-9_-]+", "_", a.nombre.lower()).strip("_") or "personaje_obj"
    mods = (a.mods or a.trabajo / "mods_opcionales").resolve()
    if (mods / slug).exists():
        ap.error("ya existe el mod; elige otra --mods para conservarlo")
    work = a.trabajo.resolve() / (slug + "_" + datetime.now().strftime("%Y%m%d_%H%M%S_%f"))
    work.mkdir(parents=True, exist_ok=False)
    # La copia completa mantiene mtllib, imágenes y rutas relativas sin tocar originales.
    source = work / "fuente"
    shutil.copytree(a.obj.resolve().parent, source)
    template = work / "donante.amb"
    template.write_bytes(afs_pair.hd(donor["models"][0], region=str(a.us or R.default_us())))
    tpl = S.Template(str(template))
    job = dict(obj=str(source / a.obj.name), work=str(work), arriba=a.arriba, giro=a.giro,
               triangulos=a.triangulos, textura=a.textura, nombre=a.nombre, acabado=a.acabado, forma=a.forma,
               labels=tpl.labels, parent=tpl.parent, world=[m.tolist() for m in tpl.world()],
               landmarks=json.loads(a.landmarks.read_text(encoding="utf-8")) if a.landmarks else {})
    job_path = work / "trabajo.json"
    job_path.write_text(json.dumps(job, indent=2), encoding="utf-8")
    print("Blender: " + str(work), flush=True)
    with (work / "blender.log").open("w", encoding="utf-8") as log:
        proc = subprocess.run([str(a.blender), "-b", "--factory-startup", "--python-exit-code", "1",
                               "--python", str(HERE / "obj_blender.py"), "--", str(job_path)],
                              stdout=log, stderr=subprocess.STDOUT, timeout=900)
    if proc.returncode:
        raise SystemExit("Blender falló; detalles: " + str(work / "blender.log"))
    mesh = json.loads((work / "malla.json").read_text(encoding="utf-8"))
    model = work / "personaje.amb"
    stats = escribir_modelo(tpl, mesh, model)
    mods.mkdir(parents=True, exist_ok=True)
    R.new_char(SimpleNamespace(mods=str(mods), mod=slug, modelo=[str(model)], captura=None,
                              cara=str(work / "retrato.png"), icono=None,
                              retrato=str(work / "retrato.png"), retrato_p1=None, retrato_p2=None,
                              nombre=a.nombre, donante=a.donante, id=None, por_traje=1,
                               despues_de=a.donante, us=str(a.us or R.default_us())))
    if a.acabado == "cel":
        # Metadato de acabado. La mascara esta en el modelo, no en una constante
        # de combate: el rival nativo conserva su iluminacion.
        with (mods / slug / "personaje.toml").open("a", encoding="utf-8") as config:
            config.write("\n# Acabado cel: mascara de brillo propia, rival nativo intacto.\nobj_cel = true\n")
    report = dict(nombre=a.nombre, donante=a.donante, fuente=str(a.obj.resolve()),
                   mod=str(mods / slug), trabajo=str(work), modelo=stats,
                   blender=mesh["report"], estado="generado; pendiente de validación en combate",
                   sha256_modelo=hashlib.sha256(model.read_bytes()).hexdigest(),
                   fuentes=[dict(ruta=p.relative_to(source).as_posix(), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                            for p in sorted(source.rglob("*")) if p.is_file()])
    (work / "informe.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    shutil.copy2(work / "informe.json", mods / slug / "importacion.json")
    (mods / slug / "LEEME_OBJ.md").write_text(
        "# " + a.nombre + " — mod OBJ opcional\n\n"
        "Donante de movimientos: ID " + str(a.donante) + ". Una forma; cara y dedos rígidos.\n\n"
        + ("Acabado cel: colores limpios y máscara de brillo propia en los pigmentos oscuros. "
           "La piel conserva normales y el rival nativo conserva su brillo HD. "
           "Requiere la recompilación FORMA2 para retirar el apagado compartido de versiones anteriores.\n\n" if a.acabado == "cel" else "") +
        "Para instalarlo, copia esta carpeta dentro de mods/ de tu juego y reconstruye los personajes "
        "desde el Kit o con `python \"mod center hd/roster_build.py\" construir --mods \"RUTA/mods\"`. "
        "Para desactivarlo, usa el Centro de mods y vuelve a reconstruir los personajes.\n\n"
        "Antes de darlo por aprobado, revisa selección, cuatro vistas, puñetazo, carga de ki y daño recibido. "
        "El PASS de estructura no sustituye esas pruebas. Detalles y huellas: importacion.json.\n",
        encoding="utf-8")
    print("PASS estructura y bind. Mod opcional: " + str(mods / slug))
    print("Revisar lámina: " + str(work / "lamina_blender.png"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
