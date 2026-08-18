"""
Re-layout del AWG0: agrandar sec34 (buffer principal de vertices) para que
quepan mas vertices, desplazando vb2/ib/restart y todos los AWGs siguientes.

Base correcta del header AWG: el magic #AWG esta en awg0_off + 0x40 y los
offsets internos (+0x2C vb2, +0x30 ib, +0x34 sec34, +0x38 restart) son
relativos al magic.

Uso:
  python relayout_awg.py <bin_amb_hd> <n_verts_nuevos> <output.bin>
"""

import struct
import sys


def u32(data, off):
    return struct.unpack('>I', data[off:off + 4])[0]


def pack_u32(data, off, val):
    struct.pack_into('>I', data, off, val)


def main():
    if len(sys.argv) < 4:
        print('Uso: python relayout_awg.py <bin_amb_hd> <n_verts> <output.bin>')
        return
    src = sys.argv[1]
    n_new = int(sys.argv[2])
    out = sys.argv[3]

    b = bytearray(open(src, 'rb').read())
    awo = 0x40
    amg_am = u32(b, awo + 0x18)
    tbl_amg = u32(b, awo + 0x1C)
    axes_base = u32(b, awo + 0x34)
    offs = [u32(b, awo + tbl_amg + i * 4) for i in range(amg_am)]
    awg0_off, awg1_off = offs[0], offs[1]
    awg0_size = awg1_off - awg0_off

    # Header AWG (magic) en awg0_off + 0x40
    AWG = awg0_off + 0x40
    sec34_rel = u32(b, AWG + 0x34)
    vb2_rel = u32(b, AWG + 0x2C)
    ib_rel = u32(b, AWG + 0x30)
    restart_rel = u32(b, AWG + 0x38)

    # Nuevo layout sec34
    sec34_start = sec34_rel + 2
    new_vb2_rel = ((sec34_start + n_new * 44) + 0xF) & ~0xF
    delta = new_vb2_rel - vb2_rel
    if delta < 0:
        print('ERROR: n_verts menor que el buffer actual')
        return
    print('AWG0: sec34 %d verts (era %d), delta=0x%X' % (
        n_new, (vb2_rel - sec34_start) // 44, delta))

    # Construir AWG0 nuevo (insertar padding en vb2_rel)
    awg0_data = bytes(b[awg0_off:awg1_off])
    new_awg0 = bytearray(awg0_data[:vb2_rel])
    new_awg0 += bytes(delta)
    new_awg0 += awg0_data[vb2_rel:]

    # Actualizar header AWG (rel magic): +0x2C vb2, +0x30 ib, +0x38 restart
    pack_u32(new_awg0, 0x2C + 0x40, vb2_rel + delta)
    pack_u32(new_awg0, 0x30 + 0x40, ib_rel + delta)
    pack_u32(new_awg0, 0x38 + 0x40, restart_rel + delta)
    # sec34 (+0x34) no cambia

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
