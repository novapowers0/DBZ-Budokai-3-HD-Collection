"""
Re-layout del AWG0: agrandar vb2 (buffer secundario, +0x2C) para ganar slots
de vertice extra, manteniendo sec34 (+0x34) y el IB sin cambios.

Esto preserva la particion del vertex pool: los indices 0..(sec34_n-1) van a
sec34, los indices sec34_n..van a vb2. Al agrandar vb2 SOLO se ganan slots al
final, sin romper la numeracion existente.

Uso:
  python relayout_vb2.py <bin_amb_hd> <n_verts_extra> <output.bin>
"""

import struct
import sys


def u32(data, off):
    return struct.unpack('>I', data[off:off + 4])[0]


def pack_u32(data, off, val):
    struct.pack_into('>I', data, off, val)


def main():
    if len(sys.argv) < 4:
        print('Uso: python relayout_vb2.py <bin_amb_hd> <n_verts_extra> <output.bin>')
        return
    src = sys.argv[1]
    n_extra = int(sys.argv[2])
    out = sys.argv[3]

    b = bytearray(open(src, 'rb').read())
    awo = 0x40
    amg_am = u32(b, awo + 0x18)
    tbl_amg = u32(b, awo + 0x1C)
    axes_base = u32(b, awo + 0x34)
    offs = [u32(b, awo + tbl_amg + i * 4) for i in range(amg_am)]
    awg0_off, awg1_off = offs[0], offs[1]
    awg0_size = awg1_off - awg0_off

    AWG = awg0_off + 0x40
    sec34_rel = u32(b, AWG + 0x34)
    vb2_rel = u32(b, AWG + 0x2C)
    ib_rel = u32(b, AWG + 0x30)
    restart_rel = u32(b, AWG + 0x38)

    # vb2 actual: de vb2_rel a ib_rel. Nuevo vb2 crece por n_extra*44.
    vb2_size = ib_rel - vb2_rel
    new_vb2_size = vb2_size + n_extra * 44
    new_ib_rel = ib_rel + n_extra * 44
    new_restart_rel = restart_rel + n_extra * 44
    delta = n_extra * 44
    print('vb2: %d -> %d bytes (+%d verts) | delta=0x%X' % (
        vb2_size, new_vb2_size, n_extra, delta))

    # Construir AWG0 nuevo: insertar padding al final de vb2 (en ib_rel)
    awg0_data = bytes(b[awg0_off:awg1_off])
    new_awg0 = bytearray(awg0_data[:ib_rel])
    new_awg0 += bytes(delta)
    new_awg0 += awg0_data[ib_rel:]

    # Actualizar header AWG: +0x2C vb2 no cambia, +0x30 ib, +0x38 restart
    pack_u32(new_awg0, 0x30 + 0x40, new_ib_rel)
    pack_u32(new_awg0, 0x38 + 0x40, new_restart_rel)

    # Ensamblar AWO nuevo
    new_awo = bytearray(b[:awg0_off])
    new_awo += new_awg0
    new_awo += b[awg1_off:]

    # Actualizar tabla AMG (entradas 1+ se desplazan +delta)
    for i in range(1, amg_am):
        pack_u32(new_awo, awo + tbl_amg + i * 4, offs[i] + delta)

    # Actualizar punteros del header AWO a la zona de ejes (>= axes_base)
    updated = 0
    for j in range(0x34, 0x700, 4):
        v = u32(new_awo, j)
        if axes_base <= v < len(b):
            pack_u32(new_awo, j, v + delta)
            updated += 1
    print('Punteros header AWO a zona ejes: %d actualizados' % updated)
    print('AWO nuevo: %d bytes (era %d)' % (len(new_awo), len(b)))

    with open(out, 'wb') as f:
        f.write(bytes(new_awo))
    print('Guardado: %s' % out)


if __name__ == '__main__':
    main()
