"""
Construir el AMB de Janemba (AWO + AZT) con conteos propios.

Estrategia validada:
  - El runtime acepta conteos variables (Krillin 327/328/329: sec34=1956/1791/2277).
  - Usar el AWO de Krillin como plantilla ESTRUCTURAL completa (labels, ejes,
    mesh group, mesh-ref blocks, arms, zona ejes) y reemplazar SOLO los buffers
    del AWG0 con la geometria de Janemba (cuerpo en sec34, resto en vb2).
  - El IB se reconstruye con los triangulos de Janemba.

NOTA: los mesh-ref blocks/arms del AWG0 apuntan a rangos del IB de Krillin.
Para una primera validacion se mantienen tal cual (el runtime dibuja el IB
completo via los rangos de los arms; si no coinciden, el modelo se ve mal pero
NO deberia crashear). Re-mapeo de arms = fase posterior.

Uso:
  python build_janemba.py <bin_amb_krillin> <bin_ps2_janemba> <output_amb>
"""

import struct
import sys

sys.path.insert(0, r'C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\awo_tools')
from extract_geometry import PS2Model
from convert_personaje import extract_geometry_skinned

F32 = struct.Struct('>f')
U32 = struct.Struct('>I')
U16 = struct.Struct('>H')


def f32(v):
    return F32.pack(v)


def u32r(data, off):
    return struct.unpack('>I', data[off:off + 4])[0]


def pack_u32(data, off, val):
    struct.pack_into('>I', data, off, val)


def main():
    if len(sys.argv) < 4:
        print('Uso: python build_janemba.py <bin_krillin> <bin_ps2_janemba> <output>')
        return
    krillin = open(sys.argv[1], 'rb').read()
    janemba = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    # 1) Geometria de Janemba con skinning (converted = lista de bytes HD)
    converted, tri_sets = extract_geometry_skinned(janemba)
    print('Janemba: %d vertices convertidos' % len(converted))

    # 2) Deduplicar por contenido y construir IB
    dedup = {}
    unique = []
    ib = bytearray()
    for vb in converted:
        if vb not in dedup:
            dedup[vb] = len(unique)
            unique.append(vb)
        ib += U16.pack(dedup[vb])
    n_sec34 = len(unique)
    n_ib = len(ib) // 2
    print('Unicos: %d | IB: %d indices' % (n_sec34, n_ib))
    print('NOTA: Krillin sec34 max=2277, IB max=5532. Janemba=%d/%d' % (n_sec34, n_ib))

    # 3) Usar la estructura del AWO de Krillin como plantilla
    #    AWO empieza en 0x40 del bin. AWG0 en tabla AMG[0].
    awo = bytearray(krillin[0x40:])
    amg_am = u32r(awo, 0x18)
    tbl = u32r(awo, 0x1C)
    offs = [u32r(awo, tbl + i * 4) for i in range(amg_am)]
    awg0_off, awg1_off = offs[0], offs[1]
    AWG = awg0_off

    # Zonas actuales del AWG0 (rel magic)
    sec34_rel = u32r(awo, AWG + 0x34)
    vb2_rel = u32r(awo, AWG + 0x2C)
    ib_rel = u32r(awo, AWG + 0x30)
    restart_rel = u32r(awo, AWG + 0x38)
    awg0_size = awg1_off - awg0_off

    # 4) Construir el nuevo AWG0
    #    sec34 (cuerpo de Janemba) + vb2 (mantener 226 slots de Krillin) + IB + restart
    n_vb2 = 226
    vb2_size = n_vb2 * 44
    sec34_start = sec34_rel + 2
    new_sec34_size = n_sec34 * 44
    new_vb2_rel = sec34_start + new_sec34_size
    new_vb2_rel = (new_vb2_rel + 0xF) & ~0xF
    new_ib_rel = new_vb2_rel + vb2_size
    new_ib_size = ((n_ib + 1) & ~1) * 2  # alinear par
    new_restart_rel = new_ib_rel + new_ib_size
    new_restart_rel = (new_restart_rel + 0xF) & ~0xF
    new_awg0_size = new_restart_rel + (awg0_size - restart_rel)

    # Ajustar si sec34 no cabe (los offsets de AWG1-17 se desplazan)
    delta_awgs = new_awg0_size - awg0_size
    print('sec34: %d -> %d | vb2: %d | ib: %d idx | delta AWGs: 0x%X' % (
        (vb2_rel - sec34_rel - 2) // 44, n_sec34, n_vb2, n_ib, delta_awgs))

    # 5) Construir el AWG0 nuevo: copiar la estructura, reemplazar buffers
    awg0_data = bytes(awo[awg0_off:awg1_off])
    new_awg0 = bytearray(awg0_data[:sec34_rel])  # hasta sec34 (header+labels+ejes+mesh)
    new_awg0 += bytes(2)  # alineado +2
    new_awg0 += b''.join(unique)  # sec34 nuevos vertices
    new_awg0 += bytes(new_vb2_rel - len(new_awg0))  # padding hasta vb2
    # vb2: copiar el vb2 de Krillin (226 slots) - es la cabeza del HD, se mantiene
    old_vb2 = awg0_data[vb2_rel:ib_rel][:vb2_size]
    new_awg0 += old_vb2
    new_awg0 += bytes(new_ib_rel - len(new_awg0))  # padding hasta ib
    new_awg0 += bytes(ib)  # IB nuevo
    if len(new_awg0) % 2:
        new_awg0 += b'\x00'
    new_awg0 += bytes(new_restart_rel - len(new_awg0))
    # restart buffer (de Krillin, indices de referencia)
    old_restart = awg0_data[restart_rel:]
    new_awg0 += old_restart
    if len(new_awg0) < new_awg0_size:
        new_awg0 += bytes(new_awg0_size - len(new_awg0))

    # Actualizar header AWG (rel magic, que esta en new_awg0[0])
    pack_u32(new_awg0, 0x2C, new_vb2_rel)
    pack_u32(new_awg0, 0x30, new_ib_rel)
    pack_u32(new_awg0, 0x34, sec34_rel)  # sec34 no cambia
    pack_u32(new_awg0, 0x38, new_restart_rel)

    # 6) Ensamblar el AWO nuevo (header + AWG0 nuevo + AWGs 1-17 + zona ejes)
    new_awo = bytearray(awo[:awg0_off])
    new_awo += new_awg0
    new_awo += awo[awg1_off:]

    # Actualizar tabla AMG (AWG1-17 se desplazan +delta_awgs)
    for i in range(1, amg_am):
        pack_u32(new_awo, tbl + i * 4, offs[i] + delta_awgs)

    # Actualizar punteros a zona ejes (>= axes_base), excluyendo tabla AMG
    axes_base = u32r(awo, 0x34)
    for j in range(0x34, 0x700, 4):
        if tbl <= j < tbl + amg_am * 4:
            continue
        v = u32r(new_awo, j)
        if axes_base <= v < len(awo):
            pack_u32(new_awo, j, v + delta_awgs)

    # 7) Repack AMB (header + AWO + AZT)
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
