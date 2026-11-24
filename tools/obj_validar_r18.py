"""Pruebas reales del importador: variantes OBJ derivadas de la copia de Nova.

Escribe únicamente en una carpeta nueva de E:/DBZ3HD_trabajo/obj_import.
No instala mods ni modifica la fuente. Ejecuta un Blender cada vez.
"""
import copy
from datetime import datetime
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
from types import SimpleNamespace

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
BASE = Path("E:/DBZ3HD_trabajo/obj_import")


def main():
    work = BASE / ("variantes_r18_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    work.mkdir(exist_ok=False)
    source = BASE / "NovaPowers_fuente/NovaPowers_PS2.obj"
    lines = source.read_text(encoding="utf-8").splitlines()
    total_vertices = sum(line.startswith("v ") for line in lines)
    cases = []
    for name in ("z_grupos_materiales", "sin_mtl_uv_colores", "imagen_uv_directa"):
        folder = work / name
        folder.mkdir()
        result, face = [], 0
        for line in lines:
            parts = line.split()
            if not parts:
                continue
            tag = parts[0]
            if name == "z_grupos_materiales":
                if tag in ("v", "vn"):
                    x, y, z = map(float, parts[1:4])
                    k = 1000 if tag == "v" else 1
                    line = f"{tag} {x*k:.9f} {-z*k:.9f} {y*k:.9f}"
                if tag == "mtllib":
                    line = "mtllib variante.mtl"
                if tag == "usemtl":
                    line = "usemtl uno"
                if tag == "f":
                    face += 1
                    if face == 3500:
                        result += ["o segunda_parte", "g segundo_grupo", "usemtl dos"]
            elif name == "sin_mtl_uv_colores":
                if tag in ("mtllib", "usemtl", "vt", "vn"):
                    continue
                if tag == "v":
                    y = float(parts[2])
                    line += " 0.1 0.3 0.9" if y < .5 else " 0.9 0.2 0.1"
                if tag == "f":
                    # Índices relativos negativos y sin UV/normales.
                    line = "f " + " ".join(str(int(p.split("/")[0]) - total_vertices - 1) for p in parts[1:])
            result.append(line)
        obj = folder / (name + ".obj")
        obj.write_text("\n".join(result) + "\n", encoding="utf-8")
        if name == "imagen_uv_directa":
            for filename in ("NovaPowers_PS2.mtl", "NovaPowers_PS2_basecolor.jpg"):
                shutil.copy2(source.parent / filename, folder / filename)
        if name == "z_grupos_materiales":
            texture = source.parent / "NovaPowers_PS2_basecolor.jpg"
            shutil.copy2(texture, folder / texture.name)
            (folder / "variante.mtl").write_text(
                "newmtl uno\nKd 1 1 1\nmap_Kd " + texture.name +
                "\nnewmtl dos\nKd 0.7 0.9 1\nmap_Kd " + texture.name + "\n", encoding="utf-8")
        cmd = [sys.executable, str(ROOT / "mod center hd/importar_obj.py"), str(obj),
               "--nombre", name, "--donante", "0", "--trabajo", str(work / "conversiones"),
               "--mods", str(work / "mods"), "--triangulos", "1500", "--textura", "128",
               "--acabado", "original"]  # Regresión de conservación; cel tiene su prueba visual2.
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
        (work / (name + ".log")).write_text(proc.stdout + proc.stderr, encoding="utf-8")
        if proc.returncode:
            raise RuntimeError(name + ": falló; ver " + str(work / (name + ".log")))
        report = json.loads((work / "mods" / name / "importacion.json").read_text(encoding="utf-8"))
        assert report["modelo"]["verificacion"] == "PASS"
        assert report["blender"]["triangulos"] <= 1500
        assert report["blender"]["arriba"] == ("z" if name.startswith("z_") else "y")
        atlas = np.asarray(Image.open(Path(report["trabajo"]) / "atlas.png").convert("RGB"))
        assert len(np.unique(atlas.reshape(-1, 3), axis=0)) > 2, "atlas sin colores"
        if name == "imagen_uv_directa":
            assert report["blender"]["atlas_metodo"] == "imagen_uv_directa"
            with Image.open(folder / "NovaPowers_PS2_basecolor.jpg") as image:
                expected = np.asarray(image.convert("RGB").resize((128, 128), Image.Resampling.BILINEAR))
            error = float(np.abs(atlas.astype(float) - expected.astype(float)).mean())
            assert error < 8, "la textura directa cambió de color/orientación: " + str(error)
            report["error_atlas_rgb"] = error
        else:
            assert report["blender"]["atlas_metodo"] == "bake_materiales"
        cases.append(dict(caso=name, resultado="PASS", informe=report))
        print("PASS " + name, flush=True)

    # Rechazo antes de escribir un modelo corrupto: NaN, peso imposible y triángulo
    # fuera del pool. No basta con que un ida/vuelta de matrices dé error cero.
    spec = importlib.util.spec_from_file_location("importar_obj", ROOT / "mod center hd/importar_obj.py")
    importer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(importer)
    mesh = dict(world=[np.eye(4).tolist()], vertices=[dict(pos=[0, 0, 0], normal=[0, 1, 0], uv=[0, 0], bone=0, weight=1)],
                triangles=[[0, 0, 0]])
    for name, change in (("nan", lambda m: m["vertices"][0]["pos"].__setitem__(0, float("nan"))),
                         ("peso", lambda m: m["vertices"][0].__setitem__("weight", 1.5)),
                         ("indice", lambda m: m["triangles"][0].__setitem__(0, 9))):
        bad = copy.deepcopy(mesh)
        change(bad)
        target = work / (name + ".amb")
        try:
            importer.escribir_modelo(SimpleNamespace(nb=1, parent=[-1]), bad, target)
        except ValueError:
            assert not target.exists()
            cases.append(dict(caso="rechazo_" + name, resultado="PASS"))
            print("PASS rechazo_" + name, flush=True)
        else:
            raise AssertionError("se aceptó " + name)

    # Paleta cel: los grises de luz horneada no pueden acabar blancos y el dorado
    # del emblema no se puede confundir con la piel. obj_blender solo usa bpy al
    # ejecutar Blender; para las funciones de color basta un módulo simulado.
    import types
    if "mathutils" not in sys.modules:
        mu = types.ModuleType("mathutils"); mu.Vector = mu.Matrix = object
        sys.modules["mathutils"] = mu
    if "bpy" not in sys.modules:
        sys.modules["bpy"] = types.ModuleType("bpy")
    spec = importlib.util.spec_from_file_location("obj_blender", ROOT / "mod center hd/obj_blender.py")
    blender = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(blender)
    with Image.open(source.parent / "NovaPowers_PS2_basecolor.jpg") as image:
        sample = np.asarray(image.convert("RGB").resize((512, 512), Image.Resampling.LANCZOS),
                            dtype=np.float32).reshape(-1, 3) / 255.0
    out, palette = blender.paleta_cel(sample)
    hi, lo = sample.max(1), sample.min(1)
    sat = (hi - lo) / np.maximum(hi, .001)
    gris = (sat < .22) & (hi >= .58) & (hi <= .86)
    assert gris.sum() > 0, "la muestra no contiene grises de bake"
    assert float(out[gris].max()) < .55, "un gris de luz horneada sigue saliendo blanco"
    colores = np.array(palette) / 255.0
    assert any(c[0] > .75 and c[1] > .5 and c[2] < .4 for c in colores), "falta el dorado del emblema"
    assert len(palette) <= 10, "la paleta no se ha fundido por tono: " + str(palette)
    cases.append(dict(caso="paleta_cel", resultado="PASS", paleta=palette))
    print("PASS paleta_cel", palette, flush=True)

    (work / "resultados.json").write_text(json.dumps(cases, ensure_ascii=False, indent=2), encoding="utf-8")
    print("PASS variantes OBJ y rechazo de datos corruptos: " + str(work))


if __name__ == "__main__":
    main()
