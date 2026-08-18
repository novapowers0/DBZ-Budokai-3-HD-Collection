"""
Construir el AMB de Janemba con vertices decimados.

Entrada: vertices ya convertidos al layout HD Y decimados (bytes), + IB.
Usa el AWO de Krillin como plantilla estructural y reemplaza los buffers
del AWG0 con la geometria de Janemba.

Uso:
  python build_janemba2.py <bin_krillin> <verts.bin> <ib.bin> <output_amb>
"""

import struct
import sys


def u32r(data, off):
    return struct.unpack('>I', data[off:off + 4])[0]


def pack_u32(data, off, val):
    struct.pack_into('>I', data, off, val)


def main():
    if len(sys.argv) < 5:
        print('Uso: python build_janemba2.py <bin_krillin> <sec34.verts> [<vb2.verts>] <ib> <output>')
        return
    krillin = open(sys.argv[1], 'rb').read()
    # Nuevo formato: <sec34> <vb2> <ib> <output> (5 args)
    # Formato viejo: <verts> <ib> <output> (4 args)
    if len(sys.argv) == 6:
        sec34_f = sys.argv[2]
        vb2_f = sys.argv[3]
        ib_f = sys.argv[4]
        out = sys.argv[5]
    else:
        sec34_f = sys.argv[2]
        vb2_f = None
        ib_f = sys.argv[3]
        out = sys.argv[4]
    verts = open(sec34_f, 'rb').read()
    vb2_new = open(vb2_f, 'rb').read() if vb2_f else None
    ib_data = open(ib_f, 'rb').read()

    n_sec34_real = len(verts) // 44
    n_vb2_real = len(vb2_new) // 44 if vb2_new else 0
    n_ib = len(ib_data) // 2
    print('Janemba decimado: sec34=%d, vb2=%d, IB=%d indices' % (n_sec34_real, n_vb2_real, n_ib))

    # Estructura del AWO de Krillin
    awo = bytearray(krillin[0x40:])
    amg_am = u32r(awo, 0x18)
    tbl = u32r(awo, 0x1C)
    offs = [u32r(awo, tbl + i * 4) for i in range(amg_am)]
    awg0_off, awg1_off = offs[0], offs[1]
    AWG = awg0_off

    sec34_rel = u32r(awo, AWG + 0x34)
    vb2_rel = u32r(awo, AWG + 0x2C)
    ib_rel = u32r(awo, AWG + 0x30)
    restart_rel = u32r(awo, AWG + 0x38)
    awg0_size = awg1_off - awg0_off

    # El AWG0 NO puede encogerse: el guest deserializa basandose en los
    # offsets del header. Sec34 se rellena al numero de slots de Krillin.
    n_sec34_orig = (vb2_rel - sec34_rel - 2) // 44  # slots originales de Krillin
    n_ib_orig = (restart_rel - ib_rel) // 2          # indices originales de Krillin

    # CLIPEAR el IB a los vertices disponibles del bin final. Si no pasamos
    # vb2 nuevo, el bin tiene sec34(n_sec34_orig) + vb2(226 nativo) slots.
    n_vb2_slots = n_vb2_real if vb2_new is not None else ((ib_rel - vb2_rel) // 44)
    max_vtx = n_sec34_orig + n_vb2_slots - 1
    if vb2_new is None:
        # Detectar IB secuencial (patron de Janemba v7: 0,1,2,...,N) que el
        # guest procesa sin crash. En ese caso NO clipar: se mantiene completo.
        ib_vals = list(struct.unpack('>%dH' % (len(ib_data) // 2), ib_data))
        is_seq = all(ib_vals[i] == i for i in range(min(len(ib_vals), 64)))
        if is_seq:
            print('  IB secuencial (patron Janemba v7): %d indices, sin clip' % len(ib_vals))
            n_ib = len(ib_vals)
        else:
            # El IB del pipeline referencia vb2 CONVERTIDO (indices n_sec34_real..
            # n_sec34_real+n_vb2_real-1). Con vb2 nativo (226 slots), los indices
            # >= n_sec34_orig apuntan a los 226 slots nativos. Descartamos los
            # triangulos que referencien indices >= n_sec34_orig (el vb2 convertido
            # no esta, su geometria no existe en el bin). Solo dibujamos sec34.
            tris = [ib_data[i:i + 6] for i in range(0, len(ib_data) - 5, 6)]
            kept = []
            for t in tris:
                a, b, c = struct.unpack('>HHH', t)
                if a < n_sec34_orig and b < n_sec34_orig and c < n_sec34_orig:
                    kept.append(t)
            ib_data = b''.join(kept)
            n_ib = len(ib_data) // 2
            print('  IB clip a sec34: %d indices (descartados %d tris de vb2)' % (
                n_ib, len(tris) - len(kept)))

    n_sec34 = max(n_sec34_real, n_sec34_orig) if n_sec34_real <= n_sec34_orig else n_sec34_real
    if n_sec34_real < n_sec34_orig:
        # Rellenar con un vertice BONE 0 (BODY) valido, como Janemba v7 que
        # tenia bone 0 mayoritario (84%) y funcionaba. Repetir el ULTIMO vertice
        # real causaba que 85% de slots fueran de la cabeza (bone 28) -> cuelgue.
        pad_slots = n_sec34_orig - n_sec34_real
        print('  sec34 real %d < slots Krillin %d -> rellenando %d slots (bone 0)' % (
            n_sec34_real, n_sec34_orig, pad_slots))
        pad_vtx = (struct.pack('>f', float('nan')) +   # flag
                   struct.pack('>f', 0.0) + struct.pack('>f', 0.0) +  # u,v
                   struct.pack('>f', 0.0) +   # z
                   struct.pack('>f', 0.0) + struct.pack('>f', 0.0) +  # x,y
                   struct.pack('>f', 1.0) +   # weight
                   struct.pack('>I', 0) +     # bone 0 (BODY)
                   struct.pack('>f', 0.0) + struct.pack('>f', 0.0) + struct.pack('>f', 0.0))  # normal
        verts = verts + pad_vtx * pad_slots
        # Desplazar los indices vb2 del IB: el pipeline los genero como
        # n_sec34_real + (i - n_sec34_real); el bin final los pone en
        # n_sec34_orig + (i - n_sec34_real) = +pad_slots.
        if vb2_new is not None:
            ib_list = [x for x in struct.unpack('>%dH' % (len(ib_data) // 2), ib_data)]
            ib_list = [x + pad_slots if x >= n_sec34_real else x for x in ib_list]
            ib_data = struct.pack('>%dH' % len(ib_list), *ib_list)
        n_sec34 = n_sec34_orig
    # El IB: si es mas pequeno que el de Krillin, rellenar con restart (0xFFFF)
    if n_ib < n_ib_orig:
        print('  IB real %d < IB Krillin %d -> rellenando con 0xFFFF' % (n_ib, n_ib_orig))
        ib_data = ib_data + b'\xff\xff' * (n_ib_orig - n_ib)
        n_ib = n_ib_orig

    # Nuevo layout: sec34 + vb2 + IB. El vb2 puede CRECER (modo archivo
    # completo) para acomodar la cabeza/caras convertidas del PS2; si es
    # menor que el original se rellena, y si es mayor se agranda el AWG0.
    n_vb2_orig = (ib_rel - vb2_rel) // 44  # 226
    if vb2_new is not None:
        n_vb2 = max(n_vb2_real, n_vb2_orig)
        if n_vb2_real < n_vb2_orig:
            vb2_new = vb2_new + bytes((n_vb2_orig - n_vb2_real) * 44)
        print('  vb2 real %d -> slots %d (crece)' % (n_vb2_real, n_vb2))
    else:
        vb2_new = None
        n_vb2 = n_vb2_orig
    vb2_size = n_vb2 * 44
    sec34_start = sec34_rel + 2
    new_vb2_rel = ((sec34_start + n_sec34 * 44) + 0xF) & ~0xF
    new_ib_rel = new_vb2_rel + vb2_size
    new_ib_size = ((n_ib + 1) & ~1) * 2
    new_restart_rel = ((new_ib_rel + new_ib_size) + 0xF) & ~0xF
    # El AWG0 NO puede encogerse (cuelga) ni crecer mucho (crash). Mantener el
    # tamanio ORIGINAL si el contenido cabe; crecer solo si es necesario.
    needed = new_restart_rel + (awg0_size - restart_rel)
    new_awg0_size = max(awg0_size, needed)
    delta_awgs = new_awg0_size - awg0_size
    print('sec34: %d | vb2: %d | ib: %d | restart: 0x%X | delta AWGs: 0x%X' % (
        n_sec34, n_vb2, n_ib, new_restart_rel, delta_awgs))

    # Construir AWG0 nuevo
    awg0_data = bytes(awo[awg0_off:awg1_off])
    new_awg0 = bytearray(awg0_data[:sec34_rel])
    new_awg0 += bytes(2)  # alineado +2
    new_awg0 += verts
    new_awg0 += bytes(new_vb2_rel - len(new_awg0))
    if vb2_new is not None:
        new_awg0 += vb2_new
    else:
        old_vb2 = awg0_data[vb2_rel:ib_rel][:vb2_size]
        new_awg0 += old_vb2
    new_awg0 += bytes(new_ib_rel - len(new_awg0))
    new_awg0 += ib_data
    if len(new_awg0) % 2:
        new_awg0 += b'\x00'
    new_awg0 += bytes(new_restart_rel - len(new_awg0))
    old_restart = awg0_data[restart_rel:]
    new_awg0 += old_restart
    if len(new_awg0) < new_awg0_size:
        new_awg0 += bytes(new_awg0_size - len(new_awg0))

    # Header AWG
    pack_u32(new_awg0, 0x2C, new_vb2_rel)
    pack_u32(new_awg0, 0x30, new_ib_rel)
    pack_u32(new_awg0, 0x34, sec34_rel)
    pack_u32(new_awg0, 0x38, new_restart_rel)

    # Ensamblar AWO
    new_awo = bytearray(awo[:awg0_off])
    new_awo += new_awg0
    new_awo += awo[awg1_off:]

    # Tabla AMG
    for i in range(1, amg_am):
        pack_u32(new_awo, tbl + i * 4, offs[i] + delta_awgs)

    # Punteros zona ejes
    axes_base = u32r(awo, 0x34)
    for j in range(0x34, 0x700, 4):
        if tbl <= j < tbl + amg_am * 4:
            continue
        v = u32r(new_awo, j)
        if axes_base <= v < len(awo):
            pack_u32(new_awo, j, v + delta_awgs)

    # Repack AMB
    n_orig = u32r(krillin, 0x0C)
    azt_loc, azt_size = None, None
    for i in range(n_orig):
        e = 0x20 + i * 16
        loc, sz = u32r(krillin, e), u32r(krillin, e + 4)
        if krillin[loc:loc + 4] == b'#AZT':
            azt_loc, azt_size = loc, sz
            break
    if azt_loc is None:
        print('ERROR: AZT no encontrado')
        return
    azt_start = ((0x40 + len(new_awo) + 15) & ~15)
    amb = bytearray()
    amb += b'#AMB'
    amb += struct.pack('>I', 0x20) + struct.pack('>I', 0)
    amb += struct.pack('>I', 2) + struct.pack('>I', 2)
    amb += struct.pack('>I', 0x20) + struct.pack('>I', 0x40)
    amb += struct.pack('>I', 0) + struct.pack('>I', 0x40)
    amb += struct.pack('>I', len(new_awo))
    amb += struct.pack('>I', 1) + struct.pack('>I', 0)
    amb += struct.pack('>I', azt_start) + struct.pack('>I', azt_size)
    amb += struct.pack('>I', 2) + struct.pack('>I', 0)
    amb += bytes(0x40 - len(amb))
    amb += new_awo
    amb += bytes(azt_start - len(amb))
    amb += krillin[azt_loc:azt_loc + azt_size]

    with open(out, 'wb') as f:
        f.write(bytes(amb))
    print('AMB Janemba: %d bytes' % len(amb))
    print('Guardado: %s' % out)


if __name__ == '__main__':
    main()
