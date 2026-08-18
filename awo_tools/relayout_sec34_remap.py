"""
Re-layout de sec34 (buffer principal) con RE-MAPEO DEL IB.

Al agrandar sec34 de 1956 a N vértices, el runtime interpreta los índices del
IB contra el nuevo tamaño de sec34. Los índices que antes apuntaban al buffer
secundario (vb2, índices >= 1956) DEBEN re-mapearse: sumar (N - 1956) para que
sigan apuntando a vb2 (que ahora empieza en el índice N).

También agranda vb2 para acomodar los índices re-mapeados (234 slots).

Uso:
  python relayout_sec34_remap.py <bin_amb> <n_sec34> <output>
"""

import struct
import sys


def u32(data, off):
    return struct.unpack('>I', data[off:off + 4])[0]


def pack_u32(data, off, val):
    struct.pack_into('>I', data, off, val)


def main():
    if len(sys.argv) < 4:
        print('Uso: python relayout_sec34_remap.py <bin_amb> <n_sec34> <output>')
        return
    src = sys.argv[1]
    n_sec34 = int(sys.argv[2])
    out = sys.argv[3]

    orig = open(src, 'rb').read()
    awo = orig[0x40:]  # slice del AWO (misma base que build_big_amb)
    b = bytearray(awo)

    amg_am = u32(b, 0x18)
    tbl_amg = u32(b, 0x1C)
    axes_base = u32(b, 0x34)
    offs = [u32(b, tbl_amg + i * 4) for i in range(amg_am)]
    awg0_off, awg1_off = offs[0], offs[1]
    AWG = awg0_off  # magic en 0xD40 del slice = 0xD80 del archivo
    sec34_rel = u32(b, AWG + 0x34)
    vb2_rel = u32(b, AWG + 0x2C)
    ib_rel = u32(b, AWG + 0x30)
    restart_rel = u32(b, AWG + 0x38)

    # Originales
    n_sec34_orig = 1956
    n_vb2_orig = (ib_rel - vb2_rel) // 44
    shift = n_sec34 - n_sec34_orig
    print('sec34: %d -> %d (shift=%d)' % (n_sec34_orig, n_sec34, shift))
    print('vb2 orig: %d slots' % n_vb2_orig)

    # Nuevo layout
    sec34_start = sec34_rel + 2
    new_vb2_rel = ((sec34_start + n_sec34 * 44) + 0xF) & ~0xF
    # vb2 debe acomodar indices hasta 2725 -> 234 slots
    max_idx = 2189 + shift
    n_vb2 = max_idx - n_sec34 + 1
    vb2_size = n_vb2 * 44
    new_ib_rel = new_vb2_rel + vb2_size
    new_restart_rel = new_ib_rel + (restart_rel - ib_rel)
    orig_vb2_size = ib_rel - vb2_rel
    delta_sec34 = new_vb2_rel - vb2_rel
    delta_vb2 = vb2_size - orig_vb2_size
    print('sec34: 0x%X -> 0x%X' % (sec34_rel, new_vb2_rel))
    print('vb2: 0x%X -> 0x%X (%d slots)' % (vb2_rel, new_vb2_rel, n_vb2))
    print('ib: 0x%X -> 0x%X' % (ib_rel, new_ib_rel))
    print('restart: 0x%X -> 0x%X' % (restart_rel, new_restart_rel))

    # Construir AWG0 nuevo
    awg0_data = bytes(b[awg0_off:awg1_off])
    # Insertar delta_sec34 en vb2_rel (agranda sec34)
    new_awg0 = bytearray(awg0_data[:vb2_rel])
    new_awg0 += bytes(delta_sec34)
    new_awg0 += awg0_data[vb2_rel:]
    # Insertar delta_vb2 en ib_rel (agranda vb2)
    split2 = ib_rel + delta_sec34
    new_awg0_2 = bytearray(new_awg0[:split2])
    new_awg0_2 += bytes(delta_vb2)
    new_awg0_2 += new_awg0[split2:]
    new_awg0 = new_awg0_2

    # Header AWG
    pack_u32(new_awg0, 0x2C, new_vb2_rel)
    pack_u32(new_awg0, 0x30, new_ib_rel)
    pack_u32(new_awg0, 0x38, new_restart_rel)

    # Re-mapear el IB: indices >= 1956 suman shift
    n_idx = (restart_rel - ib_rel) // 2
    # El IB original en new_awg0 esta en: ib_rel + delta_sec34 + delta_vb2
    ib_old = ib_rel + delta_sec34 + delta_vb2
    ib_data = bytearray()
    for i in range(n_idx):
        idx = struct.unpack_from('>H', new_awg0, ib_old + i * 2)[0]
        if idx >= 1956:
            idx += shift
        ib_data += struct.pack('>H', idx)
    new_awg0[new_ib_rel:new_ib_rel + len(ib_data)] = ib_data

    # Ensamblar AWO nuevo (slice)
    new_b = bytearray(b[:awg0_off])
    new_b += new_awg0
    new_b += b[awg1_off:]

    # Tabla AMG: los AWG1-17 se desplazan por el delta total
    total_delta_awgs = new_restart_rel - restart_rel
    for i in range(1, amg_am):
        pack_u32(new_b, tbl_amg + i * 4, offs[i] + total_delta_awgs)

    # Punteros zona ejes (excluyendo tabla AMG)
    updated = 0
    for j in range(0x34, 0x700, 4):
        if tbl_amg <= j < tbl_amg + amg_am * 4:
            continue
        v = u32(new_b, j)
        if axes_base <= v < len(b):
            pack_u32(new_b, j, v + total_delta_awgs)
            updated += 1
    print('Punteros zona ejes: %d actualizados' % updated)
    print('total_delta_awgs: 0x%X' % total_delta_awgs)

    # Repack AMB (header + AWO + AZT)
    n_orig_entries = u32(orig, 0x0C)
    azt_loc, azt_size = None, None
    for i in range(n_orig_entries):
        e = 0x20 + i * 16
        loc, size = u32(orig, e), u32(orig, e + 4)
        if orig[loc:loc + 4] == b'#AZT':
            azt_loc, azt_size = loc, size
            break
    if azt_loc is None:
        raise ValueError('AZT no encontrado')
    azt_start = ((0x40 + len(new_b) + 15) & ~15)
    amb = bytearray()
    amb += b'#AMB'
    amb += struct.pack('>I', 0x20) + struct.pack('>I', 0)
    amb += struct.pack('>I', 2) + struct.pack('>I', 2)
    amb += struct.pack('>I', 0x20) + struct.pack('>I', 0x40)
    amb += struct.pack('>I', 0) + struct.pack('>I', 0x40)
    amb += struct.pack('>I', len(new_b))
    amb += struct.pack('>I', 1) + struct.pack('>I', 0)
    amb += struct.pack('>I', azt_start) + struct.pack('>I', azt_size)
    amb += struct.pack('>I', 2) + struct.pack('>I', 0)
    amb += bytes(0x40 - len(amb))
    amb += new_b
    amb += bytes(azt_start - len(amb))
    amb += orig[azt_loc:azt_loc + azt_size]

    with open(out, 'wb') as f:
        f.write(bytes(amb))
    print('AMB: %d bytes' % len(amb))
    print('Guardado: %s' % out)


if __name__ == '__main__':
    main()
