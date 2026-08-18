"""
Construir verts+IB HD a partir de verts skinned EXPANDIDOS + triangulos PS2.

Pipeline (CORREGIDO 2026-08-14): los verts expandidos y los triangulos deben
tener los MISMOS indices. convert_personaje guarda verts expandidos (uno por
vertice de cada triangulo), y los triangulos globales referencian esos
indices 0..n-1. Aqui se decima por voxel y se remapean los indices JUNTOS
(verts y triangulos), sin perder triangulos.

  +00: 0xFFFFFFFF (flag)  +04: u  +08: v
  +12: pos.z_local  +16: pos.x_local  +20: pos.y_local
  +24: peso  +28: BONE INDEX (u32)
  +32: normal.z  +36: normal.y  +40: normal.x

Uso:
  python build_ib_from_ps2.py <verts_expanded.bin> <tris_global.bin> \
      <cell> <max_tri> <out_verts.bin> <out_ib.bin>
"""

import struct
import sys


def f32(data, off):
    return struct.unpack('>f', data[off:off + 4])[0]


def main():
    if len(sys.argv) < 7:
        print('Uso: build_ib_from_ps2.py <verts> <tris> <cell> <max_tri> <out_v> <out_ib>')
        return
    verts_raw = open(sys.argv[1], 'rb').read()
    tris_raw = open(sys.argv[2], 'rb').read()
    cell = float(sys.argv[3])
    max_tri = int(sys.argv[4])
    out_v = sys.argv[5]
    out_ib = sys.argv[6]

    n = len(verts_raw) // 44
    tris = []
    for i in range(0, len(tris_raw) - 2, 6):
        a, b, c = struct.unpack_from('<HHH', tris_raw, i)
        tris.append((a, b, c))
    print('Verts expandidos: %d | Triangulos: %d' % (n, len(tris)))

    # Posicion local de cada vertice expandido
    def pos(i):
        off = i * 44
        return (f32(verts_raw, off + 16), f32(verts_raw, off + 20), f32(verts_raw, off + 12))

    # Decimar por voxel (fusion de posiciones locales cercanas)
    cell_of = {}
    rep_of = {}
    for i in range(n):
        px, py, pz = pos(i)
        key = (int(px / cell), int(py / cell), int(pz / cell))
        cell_of[i] = key
        if key not in rep_of:
            rep_of[key] = i

    # Remapear triangulos: indices expandidos -> representantes
    new_tris = []
    for t in tris:
        if t[0] >= n or t[1] >= n or t[2] >= n:
            continue
        try:
            r0, r1, r2 = rep_of[cell_of[t[0]]], rep_of[cell_of[t[1]]], rep_of[cell_of[t[2]]]
        except KeyError:
            continue
        if r0 == r1 or r1 == r2 or r0 == r2:
            continue  # degenerado
        new_tris.append((r0, r1, r2))
    print('Triangulos tras decimar: %d' % len(new_tris))

    # Si exceden max_tri, eliminar los de mayor area (menos detalle)
    while len(new_tris) > max_tri:
        def area(t):
            a0, a1, a2 = pos(t[0]), pos(t[1]), pos(t[2])
            abx, aby = a1[0] - a0[0], a1[1] - a0[1]
            acx, acy = a2[0] - a0[0], a2[1] - a0[1]
            return abs(abx * acy - aby * acx)
        worst_i, worst_a = 0, -1
        step = max(1, len(new_tris) // 2000)
        for i in range(0, len(new_tris), step):
            a = area(new_tris[i])
            if a > worst_a:
                worst_a, worst_i = a, i
        del new_tris[worst_i]

    # Compactar indices: mapear representantes a 0..k-1 en orden de aparicion
    remap = {}
    new_rep = []
    for t in new_tris:
        for v in t:
            if v not in remap:
                remap[v] = len(new_rep)
                new_rep.append(v)
    final_ib = b''.join(struct.pack('>HHH', remap[t[0]], remap[t[1]], remap[t[2]])
                        for t in new_tris)
    out_vb = b''.join(verts_raw[r * 44:(r + 1) * 44] for r in new_rep)

    print('Resultado: %d verts, %d tris, IB=%d indices' % (
        len(new_rep), len(new_tris), len(new_tris) * 3))
    with open(out_v, 'wb') as f:
        f.write(out_vb)
    with open(out_ib, 'wb') as f:
        f.write(final_ib)
    print('Guardado: %s (%d) y %s (%d)' % (out_v, len(out_vb), out_ib, len(final_ib)))


if __name__ == '__main__':
    main()
