"""
Decimacion de vertices por voxel grid (fusion de posiciones cercanas).

Agrupa los vertices en celdas de tamaño `cell` y fusiona los que caen en la
misma celda (usa el primer vertice como representante). Reconstruye el IB
apuntando a los representantes.

Uso:
  python decimar.py <verts.bin> <cell_size> <output_verts.bin> <output_ib.bin>
"""

import struct
import sys


def main():
    if len(sys.argv) < 5:
        print('Uso: python decimar.py <verts.bin> <cell> <out_verts> <out_ib>')
        return
    data = open(sys.argv[1], 'rb').read()
    cell = float(sys.argv[2])
    out_verts = sys.argv[3]
    out_ib = sys.argv[4]

    n = len(data) // 44
    cell_map = {}
    unique = []
    ib = bytearray()
    for i in range(n):
        vb = data[i * 44:(i + 1) * 44]
        px = struct.unpack('>f', vb[16:20])[0]
        py = struct.unpack('>f', vb[20:24])[0]
        pz = struct.unpack('>f', vb[12:16])[0]
        # celda voxel
        key = (int(px / cell), int(py / cell), int(pz / cell))
        if key not in cell_map:
            cell_map[key] = len(unique)
            unique.append(vb)
        ib += struct.pack('>H', cell_map[key])

    with open(out_verts, 'wb') as f:
        f.write(b''.join(unique))
    with open(out_ib, 'wb') as f:
        f.write(bytes(ib))
    print('Decimacion cell=%.3f: %d -> %d vertices, %d indices' % (
        cell, n, len(unique), len(ib) // 2))


if __name__ == '__main__':
    main()
