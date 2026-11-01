#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Pistas y esqueleto de Tenkaichi 3 PS2 (solo lectura; no instala mods).

Formato contrastado con los datos PAL del jugador y las descripciones de
Tenkaichi3Decomp: include/battle/bobj_a.h, BtlObjAnim_SamplePose,
BtlObjPose_CalcMatrices y Quat_Unpack. Implementacion independiente.
https://github.com/z3xox/Tenkaichi3Decomp

La cabecera NO es una tabla de 56 huesos en +4: son 71 offsets en +6.
+1 cuenta eventos, +2 es el ULTIMO cuadro (0..length), +0x94 los eventos.
Los giros son cuaterniones smallest-three de 64 bits, NO Euler u16.
Leer o convertir pistas no demuestra fidelidad de una tecnica: faltan su
maquina de estados, camara, efecto, colision, sonido y prueba frente al original.

  python awo_tools/t3_anim.py inventario banco_anim.bin --out inventario.json
  python awo_tools/t3_anim.py clip banco_anim.bin 261 --out clip.json
  python awo_tools/t3_anim.py esqueleto modelo.pmdl --out esqueleto.json
"""
import argparse
import bisect
import copy
import json
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "mod center hd"))
from t3 import bpe, pak  # noqa: E402

NODES = 71
HEADER = 0x94


def _span(data, offset, size, label):
    if offset < 0 or size < 0 or offset > len(data) - size:
        raise ValueError("%s fuera del archivo: %#x + %#x / %#x" % (label, offset, size, len(data)))


def unpack_quat(packed):
    """x,y,z,w unitario; indice omitido en bits 60..63, tres campos de 20 bits."""
    largest = packed >> 60
    if packed < 0 or largest > 3:
        raise ValueError("cuaternion: indice omitido invalido")
    values = [(((packed >> (20 * i)) & 0xFFFFF) / 1048575.0 - 0.5) * math.sqrt(2)
              for i in range(3)]
    remainder = 1.0 - sum(v * v for v in values)
    if remainder < -4e-6:
        raise ValueError("cuaternion: norma imposible")
    values.insert(largest, math.sqrt(max(0.0, remainder)))
    return values


def qmul(a, b):
    x, y, z, w = a
    X, Y, Z, W = b
    return [w * X + x * W + y * Z - z * Y, w * Y - x * Z + y * W + z * X,
            w * Z + x * Y - y * X + z * W, w * W - x * X - y * Y - z * Z]


def qrot(q, p):
    return qmul(qmul(q, list(p) + [0.0]), [-q[0], -q[1], -q[2], q[3]])[:3]


def slerp(a, b, t):
    dot = sum(x * y for x, y in zip(a, b))
    if dot < 0:
        b, dot = [-v for v in b], -dot
    dot = max(-1.0, min(1.0, dot))
    if dot > 0.999999:
        q = [x + (y - x) * t for x, y in zip(a, b)]
        norm = math.sqrt(sum(v * v for v in q))
        return [v / norm for v in q]
    angle = math.acos(dot)
    A, B = math.sin((1 - t) * angle) / math.sin(angle), math.sin(t * angle) / math.sin(angle)
    return [A * x + B * y for x, y in zip(a, b)]


def read_clip(data):
    """Clip descomprimido -> pistas por ID real de nodo y eventos sin traducir."""
    _span(data, 0, HEADER, "cabecera")
    marker, nevents, last, unknown = struct.unpack_from("<BBHH", data)
    if marker != 0x61:
        raise ValueError("clip T3: marcador distinto de 0x61")
    _span(data, HEADER, nevents * 16, "eventos")
    body = HEADER + nevents * 16
    offsets = struct.unpack_from("<71H", data, 6)
    tracks, spans = {}, []
    for node, word_offset in enumerate(offsets):
        if not word_offset:
            continue
        at = word_offset * 4
        if at < body:
            raise ValueError("pista %d solapa la cabecera/eventos" % node)
        _span(data, at, 4, "pista %d" % node)
        flags, count = struct.unpack_from("<HH", data, at)
        if flags not in (0, 1) or count == 0:
            raise ValueError("pista %d: tipo/conteo no soportado (%d, %d)" % (node, flags, count))
        size = 4 + count * (10 if flags else 24)
        _span(data, at, size, "claves %d" % node)
        keys = []
        for k in range(count):
            if flags:
                packed = struct.unpack_from("<Q", data, at + 4 + k * 8)[0]
                frame = struct.unpack_from("<H", data, at + 4 + count * 8 + k * 2)[0]
                pos = None
            else:
                x, y, z, frame, packed = struct.unpack_from("<3fIQ", data, at + 4 + k * 24)
                pos = [x, y, z]
                if not all(math.isfinite(v) for v in pos):
                    raise ValueError("pista %d: posicion no finita" % node)
            if frame > last or (keys and frame < keys[-1]["frame"]):
                raise ValueError("pista %d: tiempos fuera de rango o sin ordenar" % node)
            keys.append(dict(frame=frame, rotation=unpack_quat(packed), position=pos,
                             packed="%016x" % packed))
        tracks[node] = dict(flags=flags, offset=at, keys=keys)
        spans.append((at, at + size))
    for a, b in zip(sorted(set(spans)), sorted(set(spans))[1:]):
        if a[1] > b[0]:
            raise ValueError("pistas solapadas")
    events = []
    for k in range(nevents):
        attr, frame, shape, arg = struct.unpack_from("<QHHI", data, HEADER + k * 16)
        if frame > last:
            raise ValueError("evento fuera de la duracion")
        event = dict(attr="%016x" % attr, frame=frame, shape=shape, arg=arg)
        if attr & 1:
            _span(data, shape * 4, 28, "volumen del evento")
            volume = struct.unpack_from("<7f", data, shape * 4)
            if not all(math.isfinite(v) for v in volume):
                raise ValueError("volumen no finito")
            event["volume"] = volume
        events.append(event)
    return dict(marker=marker, last_frame=last, unknown=unknown, tracks=tracks, events=events)


def sample_track(track, frame):
    """Semantica de T3: fuera del tramo = identidad y posicion cero; una clave es constante."""
    keys = track["keys"]
    identity = ([0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.0] if not track["flags"] else None)
    if len(keys) == 1:
        return keys[0]["rotation"], keys[0]["position"]
    times = [k["frame"] for k in keys]
    if frame < times[0] or frame > times[-1]:
        return identity
    exact = bisect.bisect_left(times, frame)
    if exact < len(keys) and times[exact] == frame:
        return keys[exact]["rotation"], keys[exact]["position"]
    i = min(bisect.bisect_right(times, frame) - 1, len(keys) - 2)
    a, b = keys[i:i + 2]
    t = (frame - a["frame"]) / (b["frame"] - a["frame"])
    pos = None if track["flags"] else [x + (y - x) * t for x, y in zip(a["position"], b["position"])]
    return slerp(a["rotation"], b["rotation"], t), pos


def read_skeleton(data):
    """Lista PMDL de nodos enlazados, pila de padres, origen y traslado de reposo."""
    _span(data, 0, 0x70, "PMDL")
    if data[:4] != b"pmdl":
        raise ValueError("se esperaba pmdl")
    at = struct.unpack_from("<I", data, 0x6C)[0]
    seen, ids, stack, nodes = set(), set(), [], []
    for _ in range(512):
        _span(data, at, 0x40, "nodo PMDL")
        if at < 0x70 or at in seen:
            raise ValueError("lista de nodos ciclica/invalida")
        seen.add(at)
        nxt, pop, last, enabled, node = struct.unpack_from("<I4H", data, at)
        if node in ids or pop > len(stack) + 1:
            raise ValueError("ID repetido o pila de padres invalida")
        ids.add(node)
        origin = list(struct.unpack_from("<3f", data, at + 0x10))
        rest = list(struct.unpack_from("<3f", data, at + 0x30))
        if not all(math.isfinite(v) for v in origin + rest):
            raise ValueError("PMDL con posiciones no finitas")
        nodes.append(dict(id=node, parent=stack[-1] if stack else None, rest=rest, origin=origin,
                          offset=at, enabled=enabled))
        stack.append(node)
        if pop:
            del stack[-pop:]
        if last:
            return nodes
        if nxt < 0x40:
            raise ValueError("salto PMDL invalido")
        at += nxt
    raise ValueError("PMDL sin fin de lista")


def runtime_clip(clip, flags):
    """Copia con el ajuste de raiz de BtlAnim_ApplyTableFlags (tabla comun del juego).

    Los flags NO estan en el clip. 0x40000000 anula la traslacion del nodo 0;
    si no, 0x80000000 resta la primera posicion y el bit 2 puede anular X.
    No modifica el dato de origen ni pretende convertir la maquina de combate.
    """
    result = copy.deepcopy(clip)
    root = result["tracks"].get(0)
    if root is None or root["flags"] or not (flags & 0xC0000000):
        return result
    first = list(root["keys"][0]["position"])
    for key in root["keys"]:
        if flags & 0x40000000:
            key["position"] = [0.0, 0.0, 0.0]
        else:
            key["position"] = [a - b for a, b in zip(key["position"], first)]
            if flags & 2:
                key["position"][0] = 0.0
    return result


def pose(clip, skeleton, frame, secondary=None, mix=0.0):
    """Transformaciones model-space, opcionalmente con la segunda capa de apuntado.

    Como BtlObjAnim_SamplePose: la segunda capa usa el mismo tiempo normalizado,
    mezcla posicion/SLERP solo en sus nodos presentes. No incluye el blend desde
    la accion anterior, mirada, cadenas fisicas ni la matriz mundial del actor.
    """
    if not math.isfinite(mix) or not 0 <= mix <= 1 or (mix and secondary is None):
        raise ValueError("mezcla de capas invalida")
    secondary_frame = frame
    if secondary is not None and clip["last_frame"] != secondary["last_frame"]:
        secondary_frame = min(secondary["last_frame"], frame * secondary["last_frame"] / (clip["last_frame"] or 1))
    world = {}
    for node in skeleton:
        track = clip["tracks"].get(node["id"])
        rotation, position = sample_track(track, frame) if track else ([0.0, 0.0, 0.0, 1.0], None)
        if position is None:
            position = node["rest"]
        other = secondary["tracks"].get(node["id"]) if secondary is not None and mix else None
        if other is not None:
            other_rotation, other_position = sample_track(other, secondary_frame)
            if other_position is None:
                other_position = node["rest"]
            rotation = slerp(rotation, other_rotation, mix)
            position = [a + (b - a) * mix for a, b in zip(position, other_position)]
        parent = world.get(node["parent"])
        if parent:
            position = [a + b for a, b in zip(parent[1], qrot(parent[0], position))]
            rotation = qmul(parent[0], rotation)
        world[node["id"]] = rotation, position
    return world


def bank(data):
    entries = pak(data)
    if not entries or len(entries) != 440:
        raise ValueError("se esperaba un PAK de 440 animaciones")
    for i, (at, size) in enumerate(entries):
        raw = bpe(data[at:at + size])
        try:
            clip = read_clip(raw) if raw else None
        except ValueError as exc:
            raise ValueError("clip %d: %s" % (i, exc)) from exc
        yield i, clip


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=("inventario", "clip", "esqueleto"))
    ap.add_argument("input", type=Path)
    ap.add_argument("index", nargs="?", type=int)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    if a.out.exists():
        ap.error("la salida ya existe; usa otra ruta para conservar los datos")
    data = a.input.read_bytes()
    if a.action == "esqueleto":
        result = read_skeleton(data)
    elif a.action == "clip":
        if a.index is None or not 0 <= a.index < 440:
            ap.error("clip requiere un indice entre 0 y 439")
        result = next(c for i, c in bank(data) if i == a.index)
    else:
        result = [dict(index=i, last_frame=c["last_frame"], tracks=len(c["tracks"]), events=c["events"])
                  if c else dict(index=i, empty=True) for i, c in bank(data)]
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("Datos leidos ->", a.out)


if __name__ == "__main__":
    main()
