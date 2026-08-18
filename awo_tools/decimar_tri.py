"""
Decimacion por triangulos: reduce el numero de triangulos y vertices para
que quepan en los buffers HD de Krillin (sec34=1956, IB=5140).

Pipeline:
  1. Leer vertices skinned (layout HD, stride 44) + IB (triangulos).
  2. Mapear cada vertice a su celda voxel (posicion local).
  3. Decimar vertices (fusion de celdas) -> representante por celda.
  4. Reconstruir IB con representantes; descartar triangulos degenerados.
  5. Si aun quedan >n_tri_max, eliminar triangulos (los mas redundantes).

Uso:
  python decimar_tri.py <verts.bin> <ib.bin> <cell> <max_tri> <out_verts> <out_ib>
"""

import struct
import sys


def main():
    if len(sys.argv) < 7:
        print('Uso: decimar_tri.py <verts> <ib> <cell> <max_tri> <out_v> <out_ib>')
        return
    verts_raw = open(sys.argv[1], 'rb').read()
    ib_raw = open(sys.argv[2], 'rb').read()
    cell = float(sys.argv[3])
    max_tri = int(sys.argv[4])
    out_v = sys.argv[5]
    out_ib = sys.argv[6]

    n_orig = len(verts_raw) // 44
    ib = struct.unpack('>%dH' % (len(ib_raw) // 2), ib_raw)
    tris = [(ib[i], ib[i + 1], ib[i + 2]) for i in range(0, len(ib) - 2, 3)]
    print('Original: %d verts, %d tris' % (n_orig, len(tris)))

    # Posiciones de los vertices (layout HD: [nan,VT,U,V.z, pos.x, pos.y, peso,0, VN])
    def pos(i):
        off = i * 44
        return (struct.unpack('>f', verts_raw[off + 16:off + 20])[0],
                struct.unpack('>f', verts_raw[off + 20:off + 24])[0],
                struct.unpack('>f', verts_raw[off + 12:off + 16])[0])

    # Asignar cada vertice a su celda voxel
    cell_of = {}
    rep_of = {}  # celda -> indice representante
    for i in range(n_orig):
        px, py, pz = pos(i)
        key = (int(px / cell), int(py / cell), int(pz / cell))
        cell_of[i] = key
        if key not in rep_of:
            rep_of[key] = i

    # Reconstruir IB con representantes
    new_ib = []
    for t in tris:
        r0, r1, r2 = rep_of[cell_of[t[0]]], rep_of[cell_of[t[1]]], rep_of[cell_of[t[2]]]
        if r0 == r1 or r1 == r2 or r0 == r2:
            continue  # degenerado
        new_ib.append((r0, r1, r2))

    # Mapear representantes a indices compactos
    rep_list = []
    rep_idx = {}
    for t in new_ib:
        for r in t:
            if r not in rep_idx:
                rep_idx[r] = len(rep_list)
                rep_list.append(r)
    compact_ib = []
    for t in new_ib:
        compact_ib.append((rep_idx[t[0]], rep_idx[t[1]], rep_idx[t[2]]))

    # Si sobran triangulos, eliminar los mas redundantes (menor area)
    while len(compact_ib) > max_tri:
        # eliminar el triangulo mas grande en area (menos detalle)
        def area(t):
            a0, a1, a2 = pos(rep_list[t[0]]), pos(rep_list[t[1]]), pos(rep_list[t[2]])
            abx, aby = a1[0] - a0[0], a1[1] - a0[1]
            acx, acy = a2[0] - a0[0], a2[1] - a0[1]
            return abs(abx * acy - aby * acx)
        # muestrear para eficiencia
        worst = 0
        worst_i = 0
        step = max(1, len(compact_ib) // 1000)
        for i in range(0, len(compact_ib), step):
            a = area(compact_ib[i])
            if a > worst:
                worst = a
                worst_i = i
        del compact_ib[worst_i]

    # Recompactar tras eliminar triangulos (quitar vertices no usados)
    used = set()
    for t in compact_ib:
        used.update(t)
    remap = {}
    new_rep = []
    for t in compact_ib:
        for v in t:
            if v not in remap:
                remap[v] = len(new_rep)
                new_rep.append(v)
    final_ib = []
    for t in compact_ib:
        final_ib.append((remap[t[0]], remap[t[1]], remap[t[2]]))

    # Escribir vertices (bytes de los representantes) y IB
    out_vb = b''.join(verts_raw[r * 44:(r + 1) * 44] for r in new_rep)
    out_ib_data = b''.join(struct.pack('>HHH', *t) for t in final_ib)

    print('Resultado: %d verts (max %d), %d tris (max %d), IB=%d idx' % (
        len(new_rep), 1956, len(final_ib), max_tri, len(final_ib) * 3))
    with open(out_v, 'wb') as f:
        f.write(out_vb)
    with open(out_ib, 'wb') as f:
        f.write(out_ib_data)
    print('Guardado: %s (%d bytes) y %s (%d bytes)' % (out_v, len(out_vb), out_ib, len(out_ib_data)))


if __name__ == '__main__':
    main()
