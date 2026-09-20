#!/usr/bin/env python3
"""Validador/empaquetador de packs de texturas (estilo PCSX2) para DBZ Budokai 3 HD.

Un "pack" es una carpeta dentro de `mods/` con ficheros nombrados igual que el
volcado dev:

    <hash:16 hex>_<W>x<H>_<sufijo>.dds        (o .png)

donde `hash` identifica la textura ORIGINAL del juego (XXH3-64 de su bitmap) y
`W`x`H` es el tamano de LA IMAGEN DEL PACK. El runtime deduce el factor de
escala:  factor = W_pack / W_original  (debe ser entero, 1..4, e igual en X e Y).

Uso:
    python "mod center hd\\texture_pack.py" validar <carpeta_pack> [--dump <carpeta_volcado>]
    python "mod center hd\\texture_pack.py" listar  <carpeta_pack>

Opciones:
    --dump DIR   carpeta del volcado dev (con index.jsonl). Si se pasa, comprueba
                 que cada hash existe y que el factor es valido respecto al
                 tamano original.
    --strict     trata los avisos como errores (codigo de salida 1).

Sin dependencias externas para el modo `listar`; para comprobar las dimensiones
reales de los ficheros DDS/PNG se usa Pillow (si esta instalado).
"""

import argparse
import json
import os
import re
import struct
import sys

NAME_RE = re.compile(r"^([0-9A-Fa-f]{16})_(\d+)x(\d+)_(.+)$")
MAX_FACTOR = 4


def parse_name(stem):
    m = NAME_RE.match(stem)
    if not m:
        return None
    return m.group(1).upper(), int(m.group(2)), int(m.group(3)), m.group(4)


def dds_dims(path):
    with open(path, "rb") as f:
        head = f.read(24)
    if len(head) < 24 or head[0:4] != b"DDS ":
        return None
    height, width = struct.unpack_from("<II", head, 12)
    return width, height


def image_dims(path):
    ext = os.path.splitext(path)[1].lower()
    if ext == ".dds":
        return dds_dims(path)
    if ext == ".png":
        try:
            from PIL import Image
        except Exception:
            return None
        try:
            with Image.open(path) as im:
                return im.width, im.height
        except Exception:
            return None
    return None


def load_dump_index(dump_dir):
    index = {}
    path = os.path.join(dump_dir, "index.jsonl")
    if not os.path.isfile(path):
        return None
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                e = json.loads(line)
            except Exception:
                continue
            index[e["hash"].upper()] = (int(e["width"]), int(e["height"]), e.get("file", ""))
    return index


def collect(pack_dir):
    files = []
    for name in sorted(os.listdir(pack_dir)):
        full = os.path.join(pack_dir, name)
        if not os.path.isfile(full):
            continue
        ext = os.path.splitext(name)[1].lower()
        if ext not in (".dds", ".png"):
            continue
        files.append(full)
    return files


def cmd_listar(pack_dir):
    files = collect(pack_dir)
    if not files:
        print(f"'{pack_dir}' no contiene .dds/.png")
        return 1
    print(f"Pack: {os.path.basename(os.path.normpath(pack_dir))}")
    print(f"Texturas: {len(files)}")
    for full in files:
        parsed = parse_name(os.path.splitext(os.path.basename(full))[0])
        if not parsed:
            print(f"  [!] {os.path.basename(full)}  (nombre no valido)")
            continue
        h, w, ht, suf = parsed
        dims = image_dims(full)
        extra = ""
        if dims and dims != (w, ht):
            extra = f"  [!] el fichero es {dims[0]}x{dims[1]} pero el nombre dice {w}x{ht}"
        print(f"  {h}_{w}x{ht}_{suf}{extra}")
    return 0


def cmd_validar(pack_dir, dump_dir, strict):
    errors = []
    warnings = []
    files = collect(pack_dir)
    if not files:
        print(f"ERROR: '{pack_dir}' no contiene .dds/.png")
        return 1

    index = load_dump_index(dump_dir) if dump_dir else None
    if dump_dir and index is None:
        warnings.append(f"no se encontro index.jsonl en '{dump_dir}' (no se valida el hash)")

    seen = {}
    ok = 0
    for full in files:
        base = os.path.basename(full)
        stem = os.path.splitext(base)[0]
        parsed = parse_name(stem)
        if not parsed:
            errors.append(f"{base}: el nombre no sigue <hash>_<W>x<H>_<sufijo>")
            continue
        h, w, ht, suf = parsed
        if h in seen:
            warnings.append(f"{base}: hash {h} repetido en el mismo pack ({seen[h]})")
        seen[h] = base

        dims = image_dims(full)
        if dims is None:
            warnings.append(f"{base}: no se pudieron leer las dimensiones (¿Pillow?)")
        elif dims != (w, ht):
            errors.append(f"{base}: el fichero es {dims[0]}x{dims[1]} pero el nombre dice {w}x{ht}")

        if index is not None:
            if h not in index:
                errors.append(f"{base}: el hash no existe en el volcado (¿textura equivocada?)")
                continue
            ow, oh, _ = index[h]
            if ow == 0 or oh == 0:
                errors.append(f"{base}: el volcado da dimensiones invalidas")
                continue
            if w % ow != 0 or ht % oh != 0:
                errors.append(
                    f"{base}: {w}x{ht} no es multiplo de {ow}x{oh} (factor no entero)"
                )
                continue
            fx, fy = w // ow, ht // oh
            if fx != fy:
                errors.append(f"{base}: factor distinto en X ({fx}) e Y ({fy})")
                continue
            if fx < 1 or fx > MAX_FACTOR:
                errors.append(f"{base}: factor x{fx} fuera de rango (1..{MAX_FACTOR})")
                continue
        ok += 1

    print(f"Pack: {os.path.basename(os.path.normpath(pack_dir))}")
    print(f"  ficheros: {len(files)}   validos: {ok}")
    for w in warnings:
        print(f"  AVISO: {w}")
    for e in errors:
        print(f"  ERROR: {e}")
    if errors or (strict and warnings):
        print(f"RESULTADO: {'FALLO' if errors else 'FALLO (strict)'}")
        return 1
    print("RESULTADO: OK")
    return 0


def main():
    ap = argparse.ArgumentParser(description="Validador de packs de texturas de DBZ B3 HD")
    ap.add_argument("accion", choices=["validar", "listar"])
    ap.add_argument("pack", help="carpeta del pack (dentro de mods/)")
    ap.add_argument("--dump", default="", help="carpeta del volcado dev (index.jsonl)")
    ap.add_argument("--strict", action="store_true", help="los avisos tambien fallan")
    args = ap.parse_args()
    if not os.path.isdir(args.pack):
        print(f"ERROR: no existe la carpeta '{args.pack}'")
        return 1
    if args.accion == "listar":
        return cmd_listar(args.pack)
    return cmd_validar(args.pack, args.dump, args.strict)


if __name__ == "__main__":
    sys.exit(main())
