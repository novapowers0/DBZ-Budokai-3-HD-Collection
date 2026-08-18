"""
Empaquetar AWO HD v20: sec34/vb2/ib de Android 18 + re-mapeo de shadow arms.

Tecnica del CONSOLIDADO §13.5.13:
  - Los shadow arms (sello 0x204) definen los limites del IB en BYTES.
  - Escribir en +4 (end_byte) y +0xC (start_byte) = indice_del_IB * 2.
  - El bloque extra (bone 0x24 @0x1CD0) se actualiza al final del IB.
  - El runtime dibuja [offset_previo, offset_bone).

Uso:
  python empaquetar_v20.py <sec34> <vb2> <ib> <e326> <out_amb>
"""

import io, sys, struct
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

F32 = struct.Struct('>f')
U32 = struct.Struct('>I')
def u32r(b, o): return struct.unpack('>I', b[o:o+4])[0]
def pack_u32(b, o, v): struct.pack_into('>I', b, o, v)
def f32(v): return F32.pack(v)


def main():
    if len(sys.argv) < 6:
        print('Uso: empaquetar_v20.py <sec34> <vb2> <ib> <e326> <out>')
        return
    sec34 = open(sys.argv[1], 'rb').read()
    vb2 = open(sys.argv[2], 'rb').read()
    ib_data = open(sys.argv[3], 'rb').read()
    hd = open(sys.argv[4], 'rb').read()
    out = sys.argv[5]

    n_sec = len(sec34)//44
    n_vb2 = len(vb2)//44
    n_ib = len(ib_data)//2
    print('sec34=%d vb2=%d IB=%d' % (n_sec, n_vb2, n_ib))

    # --- Estructura del AWO de Krillin ---
    awo = bytearray(hd[0x40:])
    amg_am = u32r(awo, 0x18)
    tbl = u32r(awo, 0x1C)
    offs = [u32r(awo, tbl+i*4) for i in range(amg_am)]
    awg0_off, awg1_off = offs[0], offs[1]
    AWG = awg0_off
    sec34_rel = u32r(awo, AWG+0x34)
    vb2_rel = u32r(awo, AWG+0x2C)
    ib_rel = u32r(awo, AWG+0x30)
    restart_rel = u32r(awo, AWG+0x38)
    awg0_size = awg1_off - awg0_off
    print('AWG0: sec34@0x%X vb2@0x%X ib@0x%X rst@0x%X size=0x%X' % (
        sec34_rel, vb2_rel, ib_rel, restart_rel, awg0_size))

    # Nuevo layout dentro del AWG0
    sec34_start = sec34_rel + 2
    new_vb2_rel = ((sec34_start + n_sec*44) + 0xF) & ~0xF
    new_ib_rel = ((new_vb2_rel + n_vb2*44) + 0xF) & ~0xF
    new_ib_size = ((n_ib + 1) & ~1) * 2
    new_restart_rel = ((new_ib_rel + new_ib_size) + 0xF) & ~0xF
    needed = new_restart_rel + (awg0_size - restart_rel)
    new_awg0_size = max(awg0_size, needed)
    delta = new_awg0_size - awg0_size
    print('nuevo layout: vb2@0x%X ib@0x%X rst@0x%X delta=0x%X' % (
        new_vb2_rel, new_ib_rel, new_restart_rel, delta))

    # Construir AWG0 nuevo
    awg0_data = bytes(awo[awg0_off:awg1_off])
    new_awg0 = bytearray(awg0_data[:sec34_rel])
    new_awg0 += bytes(2)
    new_awg0 += sec34
    new_awg0 += bytes(new_vb2_rel - len(new_awg0))
    new_awg0 += vb2
    new_awg0 += bytes(new_ib_rel - len(new_awg0))
    new_awg0 += ib_data
    if len(new_awg0) % 2:
        new_awg0 += b'\x00'
    new_awg0 += bytes(new_restart_rel - len(new_awg0))
    new_awg0 += awg0_data[restart_rel:]
    if len(new_awg0) < new_awg0_size:
        new_awg0 += bytes(new_awg0_size - len(new_awg0))

    pack_u32(new_awg0, 0x2C, new_vb2_rel)
    pack_u32(new_awg0, 0x30, new_ib_rel)
    pack_u32(new_awg0, 0x34, sec34_rel)
    pack_u32(new_awg0, 0x38, new_restart_rel)

    # --- Re-mapear shadow arms (sello 0x204) ---
    # Zona de arms de Krillin (rel AWG): MR2=0x1BF4, MR9=0x1C80, MR12=0x1CBC,
    # extra=0x1CD0. Cada shadow: +4=end_byte, +0xC=start_byte.
    # El runtime dibuja [start_prev, end_cur). Con 3 shadows + extra, hay 4
    # regiones. Distribuimos los indices del IB en 4 partes iguales.
    r0 = n_ib // 4
    r1 = n_ib // 2
    r2 = 3 * n_ib // 4
    r3 = n_ib
    shadows = {
        0x1BF4: (r0, 0),        # MR2: end=r0
        0x1C80: (r1, 0),        # MR9: end=r1
        0x1CBC: (r2, 0),        # MR12: end=r2
        0x1CD0: (r3, 0),        # extra: end=r3 (fin del IB)
    }
    for arm_off, (end_idx, start_idx) in shadows.items():
        arm = awg0_off + arm_off
        pack_u32(new_awg0, arm - awg0_off + 4, end_idx * 2)
        if start_idx:
            pack_u32(new_awg0, arm - awg0_off + 0xC, start_idx * 2)
    print('Shadow arms re-mapeados: [0,{0}) [{1},{2}) [{3},{4}) [{5},{6}) indices'.format(
        0, r0, r0, r1, r1, r2, r2, r3))
    # --- Ensamblar AWO ---
    new_awo = bytearray(awo[:awg0_off])
    new_awo += new_awg0
    new_awo += awo[awg1_off:]
    for i in range(1, amg_am):
        pack_u32(new_awo, tbl+i*4, offs[i]+delta)
    axes_base = u32r(awo, 0x34)
    for j in range(0x34, 0x700, 4):
        if tbl <= j < tbl+amg_am*4:
            continue
        v = u32r(new_awo, j)
        if axes_base <= v < len(awo):
            pack_u32(new_awo, j, v+delta)

    # --- Repack AMB ---
    n_orig = u32r(hd, 0x0C)
    azt_loc = None
    for i in range(n_orig):
        e = 0x20+i*16
        loc, sz = u32r(hd, e), u32r(hd, e+4)
        if hd[loc:loc+4] == b'#AZT':
            azt_loc, azt_size = loc, sz
            break
    if azt_loc is None:
        print('ERROR: no AZT')
        return
    azt_start = ((0x40+len(new_awo)+15) & ~15)
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
    amb += bytes(0x40-len(amb))
    amb += new_awo
    amb += bytes(azt_start-len(amb))
    amb += hd[azt_loc:azt_loc+azt_size]
    with open(out, 'wb') as f:
        f.write(bytes(amb))
    print('AMB: %d bytes -> %s' % (len(amb), out))


if __name__ == '__main__':
    main()
