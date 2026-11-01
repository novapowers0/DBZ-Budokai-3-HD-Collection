#!/usr/bin/env python3
"""videos_hd.py - Opening y final HD de SSGPrinceVegeta: codificar, hacer la lista y servirla en local.

  python tools/videos_hd.py codificar [--origen E:/DBZ3HD_trabajo/video] [--destino D:/DBZ3HD_videos_hd]
  python tools/videos_hd.py lista --destino D:/DBZ3HD_videos_hd     (escribe videos_hd.toml con tamanos y SHA-256)
  python tools/videos_hd.py servidor --destino D:/DBZ3HD_videos_hd --puerto 8765 [--cortar 50000000]
      HTTP con Range (reanudacion) para probar la tarjeta del launcher; --cortar corta la primera descarga a ese byte.

Sin audio: suena el .sfd del juego, cada region con su musica. Anchos multiplos de 32: sub_820917F8 redondea asi.
"""
import argparse
import hashlib
import http.server
import re
import shutil
import subprocess
from pathlib import Path

# nombre -> (fichero de PrinceVegeta, ancho:alto, CRF de x264). En la lista de descarga: lo que el juego reproduce.
VIDEOS = {
    "opening": ("DBZ B3 Opening 4K.mp4", "2880:2160", 18),
    "ending00": ("Ending00 Upscaled.mp4", "1280:1280", 18),
}
# El juego nunca abre Ending01 (el exe no lo nombra, EU no lo trae): no va en la lista. V2 = las mismas imagenes que
# "Upscaled" (SSIM 0,999999, VMAF 99,9) a 1/12 del tamano: se copia tal cual, sin recodificar.
EXTRA = {"ending01": ("Ending01 V2.mp4", None, 0)}


def codificar(origen, destino, nombres=None):
    destino.mkdir(parents=True, exist_ok=True)
    for n, (f, res, crf) in {**VIDEOS, **EXTRA}.items():
        if nombres and n not in nombres:
            continue
        if res is None:
            shutil.copyfile(origen / f, destino / f"{n}.mp4")
            continue
        subprocess.run(["ffmpeg", "-nostdin", "-y", "-v", "error", "-stats", "-i", str(origen / f), "-an",
                        "-vf", f"scale={res}:flags=lanczos", "-pix_fmt", "yuv420p", "-c:v", "libx264", "-preset", "slow",
                        "-tune", "animation", "-crf", str(crf), "-g", "96", "-movflags", "+faststart",
                        str(destino / f"{n}.mp4")], check=True, creationflags=0x4000, stdin=subprocess.DEVNULL)


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        while b := f.read(1 << 20):
            h.update(b)
    return h.hexdigest()


def lista(destino):
    o = "# Videos HD de SSGPrinceVegeta (tools/videos_hd.py lista). url = direccion propia; sin ella, junto a este fichero.\n"
    for n in VIDEOS:
        p = destino / f"{n}.mp4"
        o += f'\n[[video]]\nnombre = "{p.name}"\nsize = {p.stat().st_size}\nsha256 = "{sha256(p)}"\n'
    (destino / "videos_hd.toml").write_text(o, encoding="ascii", newline="\n")
    print(o)


def servidor(destino, puerto, cortar):
    pendiente = [cortar]

    class H(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            p = destino / Path(self.path).name
            if not p.is_file():
                return self.send_error(404)
            tam = p.stat().st_size
            desde = 0
            m = re.match(r"bytes=(\d+)-", self.headers.get("Range", ""))
            if m:
                desde = int(m[1])
                if desde >= tam:
                    return self.send_error(416)
            self.send_response(206 if m else 200)
            self.send_header("Content-Length", str(tam - desde))
            self.send_header("Accept-Ranges", "bytes")
            if m:
                self.send_header("Content-Range", f"bytes {desde}-{tam - 1}/{tam}")
            self.end_headers()
            print(f"{p.name} desde {desde}", flush=True)
            with open(p, "rb") as f:
                f.seek(desde)
                hecho = 0
                while b := f.read(1 << 20):
                    self.wfile.write(b)
                    hecho += len(b)
                    if pendiente[0] and p.suffix == ".mp4" and hecho >= pendiente[0]:
                        pendiente[0] = 0
                        print("corte de prueba", flush=True)
                        return

        def log_message(self, *a):
            pass

    http.server.ThreadingHTTPServer(("127.0.0.1", puerto), H).serve_forever()


if __name__ == "__main__":
    a = argparse.ArgumentParser()
    a.add_argument("orden", choices=["codificar", "lista", "servidor"])
    a.add_argument("--origen", type=Path, default=Path("E:/DBZ3HD_trabajo/video"))
    a.add_argument("--destino", type=Path, default=Path("D:/DBZ3HD_videos_hd"))
    a.add_argument("--solo", nargs="*")
    a.add_argument("--puerto", type=int, default=8765)
    a.add_argument("--cortar", type=int, default=0)
    x = a.parse_args()
    if x.orden == "codificar":
        codificar(x.origen, x.destino, x.solo)
    elif x.orden == "lista":
        lista(x.destino)
    else:
        servidor(x.destino, x.puerto, x.cortar)
