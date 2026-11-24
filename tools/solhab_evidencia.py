"""Copias a escala 0,5 de las capturas solHAB, sin sustituir los originales."""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runs", nargs="+", type=Path)
    args = parser.parse_args()
    for run in args.runs:
        if not run.is_dir():
            parser.error(f"No existe la prueba: {run}")
        out = run / "escala_05"
        out.mkdir(exist_ok=True)
        manifest = []
        for source in sorted(run.glob("*.png")):
            destination = out / source.name
            with Image.open(source) as image:
                size = (image.width // 2, image.height // 2)
                image.resize(size, Image.Resampling.LANCZOS).save(destination)
            with Image.open(destination) as reduced:
                assert reduced.size == size
            manifest.append({"original": source.name, "tamano": size,
                             "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()})
        (out / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        print(f"OK: {len(manifest)} capturas a escala 0,5 en {out}")


if __name__ == "__main__":
    main()
