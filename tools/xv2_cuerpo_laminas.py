"""R16: tres hojas de evidencia local; conserva fuentes y escala 0.5.

No certifica calidad artistica. No incluye ni distribuye archivos del juego.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw


FUENTES = {
    "HUM": dict(creador="20261114-202601_opus2_r16_solXV2_cuerpo_HUM",
                planos="20261114-204108_opus2_xv2_cuerpo_combate",
                nativo="20261114-205413_opus2_xv2_cuerpo_combate",
                acciones="20261114-213654_opus2_xv2_cuerpo_combate",
                rayo="20261114-220127_opus2_xv2_cuerpo_combate", segundo=.4,
                ki="20261114-214451_opus2_r16_solXV2_cuerpo_KI_HUM"),
    "MAM": dict(creador="20261114-202811_opus2_r16_solXV2_cuerpo_MAM",
                planos="20261114-205037_opus2_xv2_cuerpo_combate",
                nativo="20261114-205521_opus2_xv2_cuerpo_combate",
                acciones="20261114-213756_opus2_xv2_cuerpo_combate",
                rayo="20261114-215730_opus2_xv2_cuerpo_combate", segundo=.2,
                ki="20261114-214624_opus2_r16_solXV2_cuerpo_KI_MAM"),
    "FRI": dict(creador="20261114-202951_opus2_r16_solXV2_cuerpo_FRI",
                planos="20261114-205233_opus2_xv2_cuerpo_combate",
                nativo="20261114-205620_opus2_xv2_cuerpo_combate",
                acciones="20261114-213858_opus2_xv2_cuerpo_combate",
                rayo="20261114-215845_opus2_xv2_cuerpo_combate", segundo=0.,
                ki="20261114-214747_opus2_r16_solXV2_cuerpo_KI_FRI"),
}
PLANOS = ("a_frente_der", "b_espalda_der", "c_frente_izq", "d_espalda_izq")
CREADOR = ("c_scouter_on", "d_vista1", "d_vista2", "d_vista3")


def generar(pruebas, referencias, salida):
    if salida.exists() or not salida.parent.is_dir():
        raise ValueError("la salida debe estar ausente, con padre existente")
    registros = []

    def registrar(path, **datos):
        registros.append(dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                              escala=.5, **datos))

    def foto(path, titulo, creador=False):
        with Image.open(path) as original:
            im = original.convert("RGB")
        crop = None
        if creador:
            crop = tuple(int(v) for v in (im.width * .03, im.height * .16, im.width * .37, im.height * .90))
            im = im.crop(crop)
        im = im.resize((im.width // 2, im.height // 2), Image.Resampling.LANCZOS)
        registrar(path, recorte=crop, titulo=titulo)
        return titulo, im

    def cuadro(path, segundo):
        proc = subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-i", str(path),
                               "-ss", str(segundo), "-frames:v", "1", "-vf", "scale=iw/2:ih/2",
                               "-f", "image2pipe", "-vcodec", "png", "pipe:1"],
                              capture_output=True, check=True, timeout=30)
        with Image.open(io.BytesIO(proc.stdout)) as original:
            im = original.convert("RGB")
        registrar(path, segundo=segundo, titulo="Rayo P1; cuadro de video")
        return "Rayo P1; cuadro de video %.2f s" % segundo, im

    hojas = []
    for raza, fuentes in FUENTES.items():
        base = {k: pruebas / v for k, v in fuentes.items() if k != "segundo"}
        filas = [
            ("Creador: 4 vistas + referencia XV2 (trajes distintos)",
             [foto(base["creador"] / (n + ".png"), "Creador " + vista, True)
              for n, vista in zip(CREADOR, ("frente", "derecha", "espalda", "izquierda"))] +
             [foto(referencias / (raza + "_frente.jpg"), "XV2 original; referencia de raza")]),
            ("XV2 convertido en combate: camara sobre cada brazo",
             [foto(base["planos"] / (n + ".png"), n) for n in PLANOS]),
            ("Referencia B3 nativa: Goku / Majin Buu / Freezer, segun raza",
             [foto(base["nativo"] / (n + ".png"), n) for n in PLANOS]),
            ("Poses de combate y limite del clip etiquetado KI en el creador",
             [foto(base["acciones"] / "h_guardia.png", "Guardia nativa 1 / 002"),
              foto(base["acciones"] / "j_golpe.png", "Ataque; revisar tambien golpe.mp4"),
              cuadro(base["rayo"] / "rayo.mp4", fuentes["segundo"]),
              foto(base["ki"] / "ki_frente.png", "Creador KI: pose NO homologada como carga")]),
        ]
        ancho = max(sum(im.width + 8 for _, im in piezas) for _, piezas in filas) + 8
        alto = 76 + sum(50 + max(im.height for _, im in piezas) for _, piezas in filas)
        hoja = Image.new("RGB", (ancho, alto), (24, 24, 24))
        draw = ImageDraw.Draw(hoja)
        draw.text((8, 8), "R16 solXV2_CUERPO - " + raza + " - evidencia LOCAL; no distribuir", fill="white")
        draw.text((8, 28), "Fuentes a 0.5 sin ampliacion. Creador MAM: piezas 301; combate: 300.", fill="white")
        draw.text((8, 48), "No hay aprobacion artistica automatica. HUM/planos: capturas anteriores al fallo del especial.", fill="white")
        y = 76
        for titulo, piezas in filas:
            draw.text((8, y), titulo, fill=(255, 220, 120))
            x = 8
            for label, im in piezas:
                draw.text((x, y + 18), label, fill="white")
                hoja.paste(im, (x, y + 38))
                x += im.width + 8
            y += 50 + max(im.height for _, im in piezas)
        hojas.append((raza, hoja))

    salida.mkdir()
    for raza, hoja in hojas:
        path = salida / (raza + "_cuerpo_05.png")
        hoja.save(path)
        print(path, hoja.size)
    (salida / "fuentes.json").write_text(json.dumps(registros, ensure_ascii=False, indent=2), encoding="utf-8")
    print("PASS: 3 laminas, fuentes identificadas y escala 0.5; sin juicio artistico automatico")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pruebas", type=Path, required=True)
    ap.add_argument("--referencias", type=Path, required=True)
    ap.add_argument("--salida", type=Path, required=True)
    args = ap.parse_args()
    generar(args.pruebas, args.referencias, args.salida)
