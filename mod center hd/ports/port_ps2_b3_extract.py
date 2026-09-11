#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""port_ps2_b3_extract.py - Paso 1 del pipeline port PS2 -> B3 HD.

Parsea un modelo PS2 (#AMB o #AMO0) y extrae todo lo necesario para el port:
  - malla (mesh parts con verts/tris reales por FaceType)
  - rig/skin (bone+peso por vertice via chunks/sub-chunks del rig)
  - esqueleto (ejes 80B: quat+pos+sello) y labels de hueso

Consolida y generaliza (auto-deteccion del AMG0) lo validado en
parse_ps2_mesh.py / ps2_rig_skin.py / ps2_to_hd_geometry.py.

Uso:
  python port_ps2_b3_extract.py <ps2.amb|amo0> <salida.json>
"""
import struct
import sys
import json

def le32(b, o): return struct.unpack('<I', b[o:o+4])[0]
def lef(b, o): return struct.unpack('<f', b[o:o+4])[0]

VERT_STRIDE = {0xBD:48, 0xFD:48, 0x3D:48, 0xB5:48, 0xB6:48, 0xF5:48,
               0x199:32, 0xB4:32, 0xA4:32, 0x99:32, 0x92:32, 0x19:32,
               0x90:16}


def qmat(qx, qy, qz, qw, px, py, pz):
    x, y, z, w = qx, qy, qz, qw
    return [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w), px],
            [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w), py],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y), pz],
            [0, 0, 0, 1]]


def mmul(a, b):
    return [[sum(a[i][k]*b[k][j] for k in range(4)) for j in range(4)]
            for i in range(4)]


def mvec(m, v):
    return [sum(m[i][k]*v[k] for k in range(4)) for i in range(4)]


def compute_worlds(d, amg_abs, n_bones):
    """Worlds por hueso. El campo +0x40 del eje es el PADRE como offset relativo
    al AMG (parent = (poff - axes_rel)//80); axes_rel=0x20 en B3 PS2."""
    axes_rel = le32(d, amg_abs + 0x14)
    axes = amg_abs + axes_rel
    loc = []
    par = []
    for i in range(n_bones):
        e = axes + i * 80
        q = (lef(d, e), lef(d, e+4), lef(d, e+8), lef(d, e+12))
        p = (lef(d, e+16), lef(d, e+20), lef(d, e+24))
        poff = le32(d, e + 0x40)
        pi = (poff - axes_rel)//80 if poff else -1
        loc.append(qmat(q[0], q[1], q[2], q[3], p[0], p[1], p[2]))
        par.append(pi)
    world = [None]*n_bones
    for i in range(n_bones):
        pr = par[i]
        if 0 <= pr < i and world[pr] is not None:
            world[i] = mmul(world[pr], loc[i])
        else:
            world[i] = loc[i]
    return world, par


def detect_base(d):
    """Devuelve el offset base del AMO0: 0x40 si es #AMB, 0 si es #AMO0."""
    if d[:4] == b'#AMB':
        base = le32(d, 0x20)            # entrada 0 del AMB = el AMO0
        if d[base:base+4] in (b'#AMO', b'#AMO0'):
            return base
        if d[0x40:0x44] in (b'#AWO', b'#AWG') or d.find(b'#AWO', 0, 0x1000) >= 0:
            raise ValueError(
                'la entrada es un contenedor HD #AMB/#AWO, no una fuente PS2 #AMO0/#AMG'
            )
        return 0x40
    if d[:4] in (b'#AMO', b'#AMO0'):
        return 0
    if d[:4] == b'#AWO' or d.find(b'#AWO', 0, 0x1000) >= 0:
        raise ValueError('el archivo es #AWO HD; se esperaba #AMO0 PS2 LE')
    raise ValueError('no es #AMB ni #AMO0')


def find_amg0(d, base):
    """AMG0 = base + tabla AMG (+0x30 del AMO0, offset del primer AMG)."""
    n_amg = le32(d, base + 0x18)
    if n_amg < 1:
        raise ValueError('AMO0 sin AMGs')
    amg_off = le32(d, base + 0x30)
    return base + amg_off, n_amg


def parse_parts(d, amg_abs, worlds=None):
    """Devuelve (parts, all_verts_set) por mesh part del AMG0.
    part = {bone, tex, shader, vtype, verts:[(oa,x,y,z,nx,ny,nz,u,v)], tris:[(a,b,c)]}
    Los vertices se llevan a MODEL-SPACE aplicando el world del hueso del part:
    el cuerpo (hueso 0) ya es model-space (identidad); las partes 'L00' (manos,
    cara, dientes, cola) van en espacio LOCAL del hueso y hay que transformarlas."""
    bone_am = le32(d, amg_abs + 0x10)
    axes_loc = le32(d, amg_abs + 0x14)
    parts = []
    all_verts = set()
    for bi in range(bone_am):
        e0 = amg_abs + axes_loc + bi * 80
        p34 = le32(d, e0 + 0x34)
        if p34 == 0:
            continue
        arm = amg_abs + p34
        mesh_hdr = le32(d, arm + 4)
        if mesh_hdr == 0:
            continue
        mg = amg_abs + mesh_hdr
        mp_amnt = le32(d, mg)
        if mp_amnt == 0 or mp_amnt > 64:
            continue
        W = worlds[bi] if (worlds and bi < len(worlds)) else None
        part_offs = [le32(d, mg + 16 + i * 4) for i in range(mp_amnt)]
        for rel in part_offs:
            po = mg + rel
            size_field = le32(d, po + 0x90)
            mesh_size = (size_field - 0x60000000) * 16 if size_field >= 0x60000000 else 0
            tex = le32(d, po + 8)
            shader = le32(d, po + 0xC)
            vtype = le32(d, po) & 0xFF
            stride = VERT_STRIDE.get(vtype, 48)
            md = po + 0xA0
            end = md + mesh_size if mesh_size > 0 else po + 0x400
            pos = md
            verts = []
            tris = []
            bv = 0
            while pos + 0x20 < min(end, len(d)):
                facetype = le32(d, pos + 0x10)
                vertcount = le32(d, pos + 0x14)
                if vertcount == 0 or vertcount > 0xFFFF:
                    break
                vpos = pos + 0x20
                if vpos + vertcount * stride > min(end, len(d)):
                    break
                for x in range(vertcount):
                    oa = vpos + x * stride
                    vx, vy, vz = lef(d, oa), lef(d, oa+4), lef(d, oa+8)
                    if stride == 48:
                        nx, ny, nz = lef(d, oa+16), lef(d, oa+20), lef(d, oa+24)
                        tu, tv = lef(d, oa+32), lef(d, oa+36)
                    elif stride == 32:
                        nx, ny, nz = 0.0, 0.0, 0.0
                        tu, tv = lef(d, oa+16), lef(d, oa+20)
                    else:
                        nx, ny, nz = 0.0, 0.0, 0.0
                        tu, tv = 0.0, 0.0
                    if W is not None:
                        mv = mvec(W, [vx, vy, vz, 1.0])
                        vx, vy, vz = mv[0], mv[1], mv[2]
                        if nx or ny or nz:
                            mn = mvec(W, [nx, ny, nz, 0.0])
                            nx, ny, nz = mn[0], mn[1], mn[2]
                    verts.append([oa, vx, vy, vz, nx, ny, nz, tu, tv])
                    all_verts.add(oa)
                if facetype == 1:      # triangle strip (winding alternado)
                    for i in range(vertcount - 2):
                        a, c, cc = i, i+1, i+2
                        if i % 2 == 1:
                            a, cc = cc, a
                        tris.append([bv+a, bv+c, bv+cc])
                else:                  # tripletes
                    for i in range(0, vertcount - 2, 3):
                        tris.append([bv+i, bv+i+1, bv+i+2])
                bv += vertcount
                pos = vpos + vertcount * stride
            parts.append({'bone': bi, 'tex': tex, 'shader': shader,
                          'vtype': vtype, 'verts': verts, 'tris': tris})
    return parts, all_verts


def extract_skin(d, amg_abs, all_verts):
    """Rig -> dict voff_abs -> [bone, weight]. Desfase amg_abs en los offsets."""
    n_bones = le32(d, amg_abs + 0x10)
    skin = {}
    for bi in range(n_bones):
        bone_loc = amg_abs + 32 + bi * 80 + 52
        p = le32(d, bone_loc)
        if p == 0:
            continue
        cur = amg_abs + p
        rig_start = le32(d, cur + 8)
        if rig_start == 0:
            continue
        rig = amg_abs + rig_start
        chunk_amnt = le32(d, rig + 12)
        if chunk_amnt > 256:
            continue
        for i in range(chunk_amnt):
            ch = rig + 16 + i * 32
            weight = le32(d, ch)
            ch_len = le32(d, ch + 4)
            ch_loc = le32(d, ch + 8)
            sb_len = le32(d, ch + 12)
            sb_loc = le32(d, ch + 16)
            if ch_loc:
                for k in range(ch_len):
                    off = le32(d, amg_abs + ch_loc + k * 32 + 12) + amg_abs
                    if off in all_verts and off not in skin:
                        skin[off] = [bi, weight]
            if sb_loc:
                for k in range(sb_len):
                    off = le32(d, amg_abs + sb_loc + k * 16 + 12) + amg_abs
                    if off in all_verts and off not in skin:
                        skin[off] = [bi, weight]
    return skin


def extract_axes(d, amg_abs, n_bones):
    """Ejes de 80B: 12 floats + sello + arm + p38 (todo LE)."""
    axes_rel = le32(d, amg_abs + 0x14)
    axes_abs = amg_abs + axes_rel
    out = []
    for i in range(n_bones):
        e = axes_abs + i * 80
        floats = [lef(d, e + j*4) for j in range(12)]
        sello = le32(d, e + 0x30)
        arm = le32(d, e + 0x34)
        p38 = le32(d, e + 0x38)
        out.append({'floats': floats, 'sello': sello, 'arm': arm, 'p38': p38})
    return out


def extract_labels(d, amg_abs, n_bones):
    """Labels de hueso del AMG (best-effort: 32B o 16B por label)."""
    lab_off = le32(d, amg_abs + 0x1C)
    if lab_off == 0 or lab_off > len(d):
        return []
    lab_abs = amg_abs + lab_off
    for stride in (32, 16):
        names = []
        ok = True
        for i in range(n_bones):
            raw = d[lab_abs + i*stride: lab_abs + i*stride + stride].split(b'\x00')[0]
            try:
                s = raw.decode('ascii')
            except UnicodeDecodeError:
                ok = False
                break
            if not s.isupper() or not all(c.isalnum() or c == '_' for c in s):
                ok = False
                break
            names.append(s)
        if ok and names:
            return names
    return []


def main():
    if len(sys.argv) < 3:
        print('Uso: port_ps2_b3_extract.py <ps2.amb|amo0> <salida.json>')
        return
    d = open(sys.argv[1], 'rb').read()
    base = detect_base(d)
    amg_abs, n_amg = find_amg0(d, base)
    n_bones = le32(d, amg_abs + 0x10)
    worlds, parents = compute_worlds(d, amg_abs, n_bones)
    parts, all_verts = parse_parts(d, amg_abs, worlds)
    skin = extract_skin(d, amg_abs, all_verts)
    axes = extract_axes(d, amg_abs, n_bones)
    labels = extract_labels(d, amg_abs, n_bones)
    print('PS2: base=0x%X AMG0=0x%X n_bones=%d parts=%d verts=%d skinned=%d' %
          (base, amg_abs, n_bones, len(parts), len(all_verts), len(skin)))
    json.dump({'base': base, 'amg0_abs': amg_abs, 'n_bones': n_bones,
               'labels': labels, 'axes': axes, 'skin': skin, 'parts': parts,
               'parents': parents,
               'worlds': [[round(v, 6) for row in w for v in row] for w in worlds]},
              open(sys.argv[2], 'w'))
    print('guardado:', sys.argv[2])


if __name__ == '__main__':
    main()
