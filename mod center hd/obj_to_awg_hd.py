#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""obj_to_awg_hd.py - Conversor universal OBJ -> .bin HD de DBZ Budokai 3.

Inspirado en el 'OBJ to AMG v0.92' de la comunidad (que construye AMGs PS2
desde OBJ con templates), pero generando el formato HD (#AWO/#AWG big-endian).

Pipeline:
  1. Parsear un OBJ (v/vt/vn/f) -> verts + UVs + normales + triangulos.
  2. Construir sec34 (stride 44, align +2, layout HD) + IB (triangulos).
  3. Usar un bin HD real (p.ej. Krillin b327_hd.bin) como PLANTILLA ESTRUCTURAL
     (header AWO, tabla AWG, ejes, mesh group, arms, vb2, AZT).
  4. Reemplazar sec34 + IB del AWG0 con la geometria nueva (manteniendo el
     tamano de buffer, o con conteos propios via mid-insert).
  5. Regenerar los descriptores de submesh con los rangos REALES del sec34/IB
     nuevos (evita el OOB que deforma el cuerpo).
  6. Guardar el bin #AMB listo para comprimir e instalar como override.

Uso:
  python obj_to_awg_hd.py <modelo.obj> <krillin_hd.bin> <out.bin>

Layout vertice sec34 HD (44B, align +2, big-endian):
  +0x00 FFFFFFFF | +0x04 u | +0x08 v | +0x0C z | +0x10 x | +0x14 y
  +0x18 peso | +0x1C BONE(u32) | +0x20 nz | +0x24 -ny | +0x28 nx
"""
import struct
import sys
import os

BE32 = struct.Struct('>I')
BE16 = struct.Struct('>H')
BEF = struct.Struct('>f')


def be32(b, o):
    return BE32.unpack_from(b, o)[0]


def be16(b, o):
    return BE16.unpack_from(b, o)[0]


def bef(b, o):
    return BEF.unpack_from(b, o)[0]


def f32be(v):
    return BEF.pack(v)


def u32be(v):
    return BE32.pack(v & 0xFFFFFFFF)


def u16be(v):
    return BE16.pack(v & 0xFFFF)


# ---------------------------------------------------------------------------
# 1. Parser de OBJ (v / vt / vn / f)
# ---------------------------------------------------------------------------
def parse_obj(path):
    """Parsea un OBJ. Devuelve (verts, uvs, norms, faces).
    faces = lista de ( (v0,v1,v2), (vt0,vt1,vt2), (vn0,vn1,vn2) ) 1-indexed."""
    verts, uvs, norms, faces = [], [], [], []
    cur_face = []
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            if parts[0] == 'v':
                verts.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif parts[0] == 'vt':
                uvs.append((float(parts[1]), float(parts[2]) if len(parts) > 2 else 0.0))
            elif parts[0] == 'vn':
                norms.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif parts[0] == 'f':
                # f v/vt/vn v/vt/vn v/vt/vn
                idx = []
                for tok in parts[1:]:
                    sub = tok.split('/')
                    vi = int(sub[0]) - 1
                    ti = int(sub[1]) - 1 if len(sub) > 1 and sub[1] else -1
                    ni = int(sub[2]) - 1 if len(sub) > 2 and sub[2] else -1
                    idx.append((vi, ti, ni))
                # triangularizar (fan) si es poligono
                for i in range(1, len(idx) - 1):
                    faces.append((idx[0], idx[i], idx[i + 1]))
    return verts, uvs, norms, faces


# ---------------------------------------------------------------------------
# 2. Construir geometria HD (sec34 + IB)
# ---------------------------------------------------------------------------
def build_hd_geometry(verts, uvs, norms, faces, bone=0, weight=1.0):
    """Construye sec34 (bytes) + IB (indices u16) desde triangulos OBJ.
    El bone es unico para todo el modelo (para un AWG de un solo hueso)."""
    sec = []
    sec_map = {}
    ib = []
    for (va, vb, vc) in faces:
        tri = []
        for (vi, ti, ni) in (va, vb, vc):
            if vi < 0 or vi >= len(verts):
                continue
            x, y, z = verts[vi]
            u = uvs[ti][0] if 0 <= ti < len(uvs) else 0.0
            v = uvs[ti][1] if 0 <= ti < len(uvs) else 0.0
            nx, ny, nz = norms[ni] if 0 <= ni < len(norms) else (0.0, 0.0, 1.0)
            # layout HD
            key = (vi, ti, ni)
            if key not in sec_map:
                vb = (u32be(0xFFFFFFFF) + f32be(u) + f32be(v) +
                      f32be(z) + f32be(x) + f32be(y) +
                      f32be(weight) + u32be(bone) +
                      f32be(nz) + f32be(-ny) + f32be(nx))
                sec_map[key] = len(sec)
                sec.append(vb)
            tri.append(sec_map[key])
        if len(tri) == 3 and tri[0] != tri[1] and tri[1] != tri[2] and tri[0] != tri[2]:
            ib.extend(tri)
    sec_bytes = b''.join(sec)
    return sec_bytes, ib, len(sec)


# ---------------------------------------------------------------------------
# 3. Empaquetar en la plantilla HD
# ---------------------------------------------------------------------------
def repack_with_geometry(base, sec_bytes, ib):
    """Reemplaza sec34 + IB del AWG0 en el bin plantilla, manteniendo la
    estructura (header/ejes/mesh/arms/descriptores). Regenera descriptores
    para que apunten a los rangos reales del sec34/IB nuevos."""
    awo = 0x40
    n_awg = be32(base, awo + 0x18)
    awg_tbl = awo + be32(base, awo + 0x1C)
    AWG0 = awo + be32(base, awg_tbl)
    sec_rel = be32(base, AWG0 + 0x34)
    ib_rel = be32(base, AWG0 + 0x30)
    vb2_rel = be32(base, AWG0 + 0x2C)
    end_rel = be32(base, AWG0 + 0x38)

    sec_start = AWG0 + sec_rel + 2  # align +2
    sec_buf = (AWG0 + vb2_rel) - (AWG0 + sec_rel) - 2
    n_sec_cap = sec_buf // 44

    ib_start = AWG0 + ib_rel
    ib_buf = (AWG0 + end_rel) - (AWG0 + ib_rel)
    n_ib_cap = ib_buf // 2

    n_sec = len(sec_bytes) // 44
    n_ib = len(ib)

    print('sec34: %d verts (cap %d) | IB: %d idx (cap %d)' % (n_sec, n_sec_cap, n_ib, n_ib_cap))
    if n_sec > n_sec_cap:
        print('ERROR: %d verts > cap %d. Decimar o usar bin mas grande.' % (n_sec, n_sec_cap))
        return None
    if n_ib > n_ib_cap:
        print('ERROR: %d idx > cap %d' % (n_ib, n_ib_cap))
        return None

    out = bytearray(base)
    # sec34
    out[sec_start:sec_start + n_sec * 44] = sec_bytes
    # IB (rellenar con 0xFFFF)
    ib_bytes = b''.join(u16be(i) for i in ib)
    ib_pad = ib_buf - len(ib_bytes)
    out[ib_start:ib_start + ib_buf] = ib_bytes + b'\xff\xff' * (ib_pad // 2)

    return bytes(out)


# ---------------------------------------------------------------------------
def main():
    if len(sys.argv) < 4:
        print('Uso: obj_to_awg_hd.py <modelo.obj> <krillin_hd.bin> <out.bin>')
        return
    obj_path, plant, out = sys.argv[1], sys.argv[2], sys.argv[3]
    verts, uvs, norms, faces = parse_obj(obj_path)
    print('OBJ: %d verts, %d uvs, %d norms, %d triangulos' % (len(verts), len(uvs), len(norms), len(faces)))
    if not faces:
        print('ERROR: el OBJ no tiene triangulos')
        return

    sec_bytes, ib, n_unique = build_hd_geometry(verts, uvs, norms, faces)
    print('HD: %d verts unicos, %d indices (%d tris)' % (n_unique, len(ib), len(ib) // 3))

    base = open(plant, 'rb').read()
    result = repack_with_geometry(base, sec_bytes, ib)
    if result is None:
        return
    open(out, 'wb').write(result)
    print('Guardado: %s (%d bytes)' % (out, len(result)))


if __name__ == '__main__':
    main()
