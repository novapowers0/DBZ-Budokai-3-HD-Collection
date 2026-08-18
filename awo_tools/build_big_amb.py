"""
Re-layout completo del AMB: agrandar el AWG0 (sec34 o vb2) y re-empaquetar
el AMB con header actualizado (AWO size + AZT loc).

IMPORTANTE: el AWO empieza en 0x40 del archivo AMB. relayout_awo opera sobre
el AWO (bytes desde 0x40), y repack_amb reconstruye el AMB completo:
  header AMB (0x40) + AWO + AZT

Uso:
  python build_big_amb.py <bin_amb_original> <modo> <n> <output_amb>
  modo: 'sec34' agrandar sec34 | 'vb2' agrandar vb2
"""

import struct
import sys


def u32(data, off):
    return struct.unpack('>I', data[off:off + 4])[0]


def pack_u32(data, off, val):
    struct.pack_into('>I', data, off, val)


def relayout_awo(orig_amb, n_new, modo):
    """Devuelve el AWO nuevo (bytes, empezando en 0x40) con AWG0 agrandado."""
    awo = orig_amb[0x40:]  # el AWO empieza en 0x40
    b = bytearray(awo)
    amg_am = u32(b, 0x18)
    tbl_amg = u32(b, 0x1C)
    axes_base = u32(b, 0x34)
    offs = [u32(b, tbl_amg + i * 4) for i in range(amg_am)]
    awg0_off, awg1_off = offs[0], offs[1]
    awg0_size = awg1_off - awg0_off

    AWG = awg0_off  # magic #AWG (los offsets de la tabla AMG apuntan al magic)
    sec34_rel = u32(b, AWG + 0x34)
    vb2_rel = u32(b, AWG + 0x2C)
    ib_rel = u32(b, AWG + 0x30)
    restart_rel = u32(b, AWG + 0x38)

    if modo == 'sec34':
        sec34_start = sec34_rel + 2
        new_vb2_rel = ((sec34_start + n_new * 44) + 0xF) & ~0xF
        delta = new_vb2_rel - vb2_rel
        split = vb2_rel
    elif modo == 'vb2':
        delta = n_new * 44
        split = ib_rel
    else:
        raise ValueError('modo desconocido')

    if delta < 0:
        raise ValueError('delta negativo')
    print('  AWG0 %s: delta=0x%X (%d bytes)' % (modo, delta, delta))

    awg0_data = bytes(b[awg0_off:awg1_off])
    new_awg0 = bytearray(awg0_data[:split])
    new_awg0 += bytes(delta)
    new_awg0 += awg0_data[split:]

    # Header AWG (rel magic): +0x2C vb2, +0x30 ib, +0x38 restart
    new_vb2 = vb2_rel + (delta if modo == 'sec34' else 0)
    new_ib = ib_rel + delta
    new_restart = restart_rel + delta
    pack_u32(new_awg0, 0x2C, new_vb2)
    pack_u32(new_awg0, 0x30, new_ib)
    pack_u32(new_awg0, 0x38, new_restart)

    # Ensamblar AWO nuevo (sin header AMB)
    new_b = bytearray(b[:awg0_off])
    new_b += new_awg0
    new_b += b[awg1_off:]

    # Tabla AMG entradas 1+
    for i in range(1, amg_am):
        pack_u32(new_b, tbl_amg + i * 4, offs[i] + delta)

    # Punteros header AWO a zona ejes (>= axes_base), excluyendo la tabla AMG
    # (tbl_amg esta en 0x690, dentro del rango 0x34-0x700; sus offsets ya fueron
    # actualizados arriba y NO deben volver a desplazarse)
    updated = 0
    for j in range(0x34, 0x700, 4):
        if tbl_amg <= j < tbl_amg + amg_am * 4:
            continue
        v = u32(new_b, j)
        if axes_base <= v < len(b):
            pack_u32(new_b, j, v + delta)
            updated += 1
    print('  Punteros zona ejes: %d actualizados' % updated)
    return bytes(new_b)


def repack_amb(orig_amb, new_awo):
    """Reconstruye el AMB completo: header + AWO + AZT."""
    n_orig = u32(orig_amb, 0x0C)
    azt_loc, azt_size = None, None
    for i in range(n_orig):
        e = 0x20 + i * 16
        loc, size = u32(orig_amb, e), u32(orig_amb, e + 4)
        if orig_amb[loc:loc + 4] == b'#AZT':
            azt_loc, azt_size = loc, size
            break
    if azt_loc is None:
        raise ValueError('AZT no encontrado')

    azt_start = ((0x40 + len(new_awo) + 15) & ~15)
    amb = bytearray()
    amb += b'#AMB'
    amb += struct.pack('>I', 0x20)
    amb += struct.pack('>I', 0)
    amb += struct.pack('>I', 2)
    amb += struct.pack('>I', 2)
    amb += struct.pack('>I', 0x20)
    amb += struct.pack('>I', 0x40)
    amb += struct.pack('>I', 0)
    amb += struct.pack('>I', 0x40)
    amb += struct.pack('>I', len(new_awo))
    amb += struct.pack('>I', 1)
    amb += struct.pack('>I', 0)
    amb += struct.pack('>I', azt_start)
    amb += struct.pack('>I', azt_size)
    amb += struct.pack('>I', 2)
    amb += struct.pack('>I', 0)
    amb += bytes(0x40 - len(amb))
    amb += new_awo
    amb += bytes(azt_start - len(amb))
    amb += orig_amb[azt_loc:azt_loc + azt_size]
    return bytes(amb)


def main():
    if len(sys.argv) < 5:
        print('Uso: python build_big_amb.py <bin_orig> <sec34|vb2> <n> <output>')
        return
    orig = open(sys.argv[1], 'rb').read()
    modo = sys.argv[2]
    n = int(sys.argv[3])
    out = sys.argv[4]

    new_awo = relayout_awo(orig, n, modo)
    amb = repack_amb(orig, new_awo)
    with open(out, 'wb') as f:
        f.write(amb)
    print('AMB nuevo: %d bytes (era %d)' % (len(amb), len(orig)))
    print('Guardado: %s' % out)


if __name__ == '__main__':
    main()
