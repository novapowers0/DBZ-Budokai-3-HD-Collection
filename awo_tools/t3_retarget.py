#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Conversion experimental de pistas T3 a #ACM HD, con mapa de huesos EXPLICITO.

No convierte efectos, camaras, ordenes de combate ni instala una tecnica.
El mapa y el cambio de base se deben contrastar con el personaje de origen.
Salida: un #ACM con un clip, para el injertador; sin datos de juego en el codigo.

  python awo_tools/t3_retarget.py banco.bin modelo.pmdl modelo_hd.bin mapa.json
      --clip 295 --out pista.acm --source-fps 60 --target-fps 60
  --clips 295,296,297,298,301 permite un banco con varios clips, en ese orden.

mapa: {"bones": {"CHEST": 17, ...}, "basis": [x,y,z,w],
       "scale": 1.0, "offset": [0,0,0], "root": "WAIST",
       "animation_flags": {"297": "0x80000040"}}

Jeice PAL medido en PCSX2: 2,4 unidades de pista por cuadro de juego a 25 Hz
= 60 unidades/segundo. No confundirlo con los 30 Hz de los temporizadores.
"""
import argparse
import math
from pathlib import Path
import struct
import json

import altura
import t3_anim as t3


def inverse(q):
    return [-q[0], -q[1], -q[2], q[3]]


def euler16(q):
    """Quaternion -> Euler Rz Ry Rx de #ACM; no confundir con el orden YXZ de T3."""
    x, y, z, w = q
    sy = max(-1.0, min(1.0, 2 * (w * y - z * x)))
    ry = math.asin(sy)
    if abs(sy) < 0.999999999:
        rx = math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y))
        rz = math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))
    else:
        rx = 0.0
        rz = math.atan2(-2 * (x * y - w * z), 1 - 2 * (x * x + z * z))
    return [round(a * 32768 / math.pi) & 0xFFFF for a in (rx, ry, rz)]


def convert(clip, source_skeleton, target_skeleton, mapping, source_fps, target_fps, runtime_flags=0):
    """Anima por delta GLOBAL sobre el bind del destino; evita duplicar giros en huesos auxiliares.

    La traslacion de la raiz elegida es absoluta en T3 y aditiva al reposo en B3.
    Se muestrea SLERP antes de pasar a Euler para conservar la interpolacion de origen.
    """
    if not all(math.isfinite(v) and v > 0 for v in (source_fps, target_fps)):
        raise ValueError("frecuencia de cuadros invalida")
    clip = t3.runtime_clip(clip, runtime_flags)
    names = [name for name, _, _, _ in target_skeleton]
    suffixes = [altura.suffix(n) for n in names]
    if len(set(suffixes)) != len(suffixes):
        raise ValueError("el esqueleto destino contiene sufijos ambiguos")
    bone_map = mapping["bones"]
    basis = mapping["basis"]
    scale = mapping["scale"]
    offset = mapping["offset"]
    root = mapping["root"]
    if (len(basis) != 4 or not all(math.isfinite(v) for v in basis)
            or abs(sum(x*x for x in basis) - 1) > 1e-5 or len(offset) != 3
            or not all(math.isfinite(v) for v in offset)
            or not math.isfinite(scale) or scale <= 0):
        raise ValueError("base, escala o desplazamiento invalidos")
    source_ids = {n["id"] for n in source_skeleton}
    if not set(bone_map).issubset(suffixes) or not set(bone_map.values()).issubset(source_ids):
        raise ValueError("el mapa usa huesos inexistentes")
    if root not in bone_map:
        raise ValueError("falta el nodo de traslacion en el mapa")
    bind, visiting = {}, set()

    def target_bind(i):
        if i in bind:
            return bind[i]
        if i in visiting:
            raise ValueError("esqueleto destino ciclico")
        visiting.add(i)
        _, q, p, parent = target_skeleton[i]
        if parent < -1 or parent >= len(target_skeleton) or parent == i:
            raise ValueError("padre de destino invalido")
        if (len(q) != 4 or len(p) != 3 or not all(math.isfinite(v) for v in (*q, *p))
                or abs(sum(v*v for v in q) - 1) > 1e-3):
            raise ValueError("transformacion de reposo de destino invalida")
        if parent >= 0:
            pq, pp = target_bind(parent)
            bind[i] = t3.qmul(pq, q), [a+b for a,b in zip(pp, t3.qrot(pq, p))]
        else:
            bind[i] = list(q), list(p)
        visiting.remove(i)
        return bind[i]

    for i in range(len(names)):
        target_bind(i)
    last = round(clip["last_frame"] * target_fps / source_fps)
    if last > 65535:
        raise ValueError("el clip excede el tiempo u16 de ACM")
    rotations = {n: [] for n in suffixes}
    positions = []
    for frame in range(last + 1):
        src = t3.pose(clip, source_skeleton, min(clip["last_frame"], frame * source_fps / target_fps))
        desired, visited = {}, set()

        def destination(i):
            if i in desired:
                return desired[i]
            if i in visited:
                raise ValueError("esqueleto destino ciclico")
            visited.add(i)
            name, restq, restp, parent = target_skeleton[i]
            suffix = suffixes[i]
            pq, pp = destination(parent) if parent >= 0 else ([0,0,0,1], [0,0,0])
            if suffix in bone_map:
                sq, sp = src[bone_map[suffix]]
                # Cambio de base por conjugacion y orientacion propia del bind destino.
                worldq = t3.qmul(t3.qmul(t3.qmul(basis, sq), inverse(basis)), bind[i][0])
                localq = t3.qmul(inverse(pq), worldq)
            else:
                localq = list(restq)
                worldq = t3.qmul(pq, localq)
            worldp = [a+b for a,b in zip(pp, t3.qrot(pq, restp))]
            if suffix == root:
                worldp = [a * scale + b for a,b in zip(t3.qrot(basis, sp), offset)]
                localp = t3.qrot(inverse(pq), [a-b for a,b in zip(worldp,pp)])
                positions.append([frame] + [a-b for a,b in zip(localp,restp)])
            rotations[suffix].append([frame] + euler16(localq))
            desired[i] = worldq, worldp
            visited.remove(i)
            return desired[i]

        for i in range(len(names)):
            destination(i)
    bones = {}
    for n in suffixes:
        rot = struct.pack(">3I", 0, 0, len(rotations[n])) + b"".join(struct.pack(">4H", *k) for k in rotations[n])
        pos = (struct.pack(">3I", 0, 0, len(positions)) + b"".join(struct.pack(">I3f", *k) for k in positions)) if n == root else None
        bones[n] = [rot, pos]
    return names, (9, 0, last + 1, bones)


def build_acm(names, animation):
    """#ACM HD independiente con una animacion (pistas BE, nombres de 32 bytes)."""
    return build_acm_bank(names, [animation])


def build_acm_bank(names, animations):
    """Banco HD con tabla de nombres compartida; cada pista conserva sus propios tiempos."""
    if not animations:
        raise ValueError("no hay animaciones para el banco")
    count = len(animations)
    out = bytearray(b"#ACM" + struct.pack(">7I", 0x20, 0, 2, count, 0x20, len(names), 0))
    out += bytes(16 * count)
    for index, (flags, variation, frames, bones) in enumerate(animations):
        if flags & 0x10:
            raise ValueError("este escritor no admite pistas de escala")
        out += bytes((-len(out)) % 16)
        start = len(out)
        struct.pack_into(">4I", out, 0x20 + 16 * index, flags, variation, frames, start)
        out += bytes(8 * len(names))
        for i, name in enumerate(names):
            for k, tr in enumerate(bones[altura.suffix(name)]):
                if tr is None:
                    continue
                out += bytes((-len(out)) % 4)
                struct.pack_into(">I", out, start + 8 * i + 4 * k, len(out))
                out += tr
    out += bytes((-len(out)) % 16)
    struct.pack_into(">I", out, 0x1C, len(out))
    for name in names:
        raw = name.encode("latin1")
        if len(raw) > 31:
            raise ValueError("nombre de hueso demasiado largo")
        out += raw.ljust(32, b"\0")
    return bytes(out)


def main():
    import roster_build as rb
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bank", type=Path)
    ap.add_argument("pmdl", type=Path)
    ap.add_argument("target", type=Path)
    ap.add_argument("map", type=Path)
    selection = ap.add_mutually_exclusive_group(required=True)
    selection.add_argument("--clip", type=int)
    selection.add_argument("--clips", help="indices separados por comas; el orden sera el del banco de salida")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--source-fps", type=float, required=True)
    ap.add_argument("--target-fps", type=float, default=60)
    ap.add_argument("--flags", type=lambda v: int(v, 0), default=None,
                    help="flags de esta animacion en la tabla comun T3 (ajuste de raiz), p. ej. 0x80000040")
    a = ap.parse_args()
    if a.out.exists():
        ap.error("salida existente: usa otra ruta")
    a.out.parent.mkdir(parents=True, exist_ok=True)
    clips = dict(t3.bank(a.bank.read_bytes()))
    try:
        indices = [a.clip] if a.clip is not None else [int(v.strip(), 0) for v in a.clips.split(",")]
    except ValueError:
        ap.error("--clips requiere indices enteros separados por comas")
    if len(set(indices)) != len(indices) or any(clips.get(i) is None for i in indices):
        ap.error("clip vacio, inexistente o repetido")
    skel = altura.skeleton(rb.load_bin(str(a.target), str(a.out.parent)))
    source = t3.read_skeleton(a.pmdl.read_bytes())
    mapping = json.loads(a.map.read_text(encoding="utf-8"))
    animations = []
    for index in indices:
        flags = a.flags if a.flags is not None else mapping.get("animation_flags", {}).get(str(index), 0)
        flags = int(flags, 0) if isinstance(flags, str) else int(flags)
        names, animation = convert(clips[index], source, skel, mapping, a.source_fps, a.target_fps, flags)
        animations.append(animation)
        print("clip T3 %d -> animacion HD %d: %d cuadros, flags %#x" % (index, len(animations) - 1, animation[2], flags))
    a.out.write_bytes(build_acm_bank(names, animations))
    print("Pistas convertidas: %d animaciones, %d huesos. Tecnica NO instalada -> %s" % (len(animations), len(names), a.out))


if __name__ == "__main__":
    main()
