"""
Conversor #AMO0 (PS2) → #AWO (HD) v4 — re-layout completo.

Causa raíz del crash (v3): el AWO tiene un axes-array al FINAL (offset +0x34
= 0x42360). Al agrandar el AWG0 (geometría PS2), el axes-array quedó
desplazado pero el puntero +0x34 no se actualizó → el guest leía vértices PS2
en lugar de punteros de ejes → crash 0xC0000005.

v4 reconstruye el AWO completo con el layout correcto:
  header + relaciones + tabla AMG + labels + AWGs (18) + axes-array
y actualiza el puntero +0x34 (axes-array) y la tabla AMG.

Mantiene los tamaños originales del VB/IB dentro del AWG0 (para no romper los
punteros internos de los mesh-ref blocks), rellenando la geometría PS2.

Uso:
  python build_awo_v4.py <bin_ps2> <bin_amb_hd_template> <output.bin>
"""

import struct
import sys
from extract_geometry import PS2Model

F32 = struct.Struct('>f')
U32 = struct.Struct('>I')
U16 = struct.Struct('>H')


def f32(v):
    return F32.pack(v)


def u32(v):
    return U32.pack(v)


def u16(v):
    return U16.pack(v)


def build_vertex_hd(vert):
    """Vertice PS2 -> layout HD (stride 44, big-endian).

    Layout HD (verificado vs Krillin HD):
      +00: 0xFFFFFFFF (flag)
      +04: u (float)      +08: v (float)
      +12: pos.z_local    +16: pos.x_local    +20: pos.y_local
      +24: peso (float)
      +28: BONE INDEX (u32)
      +32: normal.z       +36: normal.y       +40: normal.x
    """
    vx, vy, vz = vert['pos']
    nx, ny, nz = vert['nrm']
    u, vt = vert['uv']
    return (f32(float('nan')) +
            f32(u) + f32(vt) +
            f32(vz) + f32(vx) + f32(vy) +
            f32(1.0) +
            u32(vert.get('bone', 0) & 0xFFFFFFFF) +
            f32(nz) + f32(-ny) + f32(nx))


def read_amb(amb):
    n = struct.unpack('>I', amb[0x0C:0x10])[0]
    entries = []
    for i in range(n):
        e = 0x20 + i * 16
        loc, size = struct.unpack('>II', amb[e:e + 8])
        entries.append((loc, size))
    return entries


def main():
    if len(sys.argv) < 4:
        print('Uso: python build_awo_v4.py <bin_ps2> <bin_amb_hd_template> <output.bin>')
        return
    ps2 = open(sys.argv[1], 'rb').read()
    templ = open(sys.argv[2], 'rb').read()

    entries = read_amb(templ)
    awo_loc, awo_size = entries[0]
    awo = templ[awo_loc:awo_loc + awo_size]
    print('Plantilla AWO: %d bytes' % len(awo))

    tbl = struct.unpack('>I', awo[0x1C:0x20])[0]
    amg_am = struct.unpack('>I', awo[0x18:0x1C])[0]
    axes_off = struct.unpack('>I', awo[0x34:0x38])[0]  # offset del axes-array
    offs = [struct.unpack('>I', awo[tbl + i * 4:tbl + i * 4 + 4])[0] for i in range(amg_am)]
    awg0_off = offs[0]
    awg1_off = offs[1] if amg_am > 1 else axes_off
    print('AWG0 @0x%X, AWG1 @0x%X, axes-array @0x%X' % (awg0_off, awg1_off, axes_off))

    # AWG0: header + campos de tamaño
    vb_off = struct.unpack('>I', awo[awg0_off + 0x2C:awg0_off + 0x30])[0]
    ib_off = struct.unpack('>I', awo[awg0_off + 0x30:awg0_off + 0x34])[0]
    restart_off = struct.unpack('>I', awo[awg0_off + 0x38:awg0_off + 0x3C])[0]
    vb_size = ib_off - vb_off
    ib_size = restart_off - ib_off
    # tamaño del AWG0 completo (hasta awg1_off)
    awg0_full_size = awg1_off - awg0_off
    print('AWG0: vb=0x%X(%dB) ib=0x%X(%dB) restart=0x%X | full=0x%X' % (
        vb_off, vb_size, ib_off, ib_size, restart_off, awg0_full_size))

    # Geometría PS2
    model = PS2Model(ps2)
    amgs = model.amg_offsets()
    amg0_base = model.amo0 + amgs[0]
    parts = model.mesh_parts(amg0_base)
    all_verts = []
    for p in parts:
        all_verts.extend(p['verts'])
    print('Geometría PS2: %d parts, %d vértices' % (len(parts), len(all_verts)))

    # VB: rellenar al tamaño original
    vb_data = b''.join(build_vertex_hd(v) for v in all_verts)
    vb = vb_data[:vb_size].ljust(vb_size, b'\x00')
    max_vb_verts = vb_size // 0x2C
    n_fit = min(len(all_verts), max_vb_verts)

    # IB: triangle list con índices válidos + relleno FFFF
    ib_data = b''
    offset = 0
    for p in parts:
        nv = p['n_verts']
        for i in range(nv // 3):
            i0, i1, i2 = offset + i * 3, offset + i * 3 + 1, offset + i * 3 + 2
            if i2 < n_fit:
                ib_data += u16(i0) + u16(i1) + u16(i2)
        offset += nv
    ib = bytearray(ib_data[:ib_size])
    while len(ib) < ib_size:
        ib += u16(0xFFFF)
    print('VB: %d verts PS2 -> %d bytes (caben %d). IB: %d triangulos validos' % (
        len(all_verts), vb_size, n_fit, len(ib_data) // 6))

    # Construir el nuevo AWG0 (mismo tamaño total, reemplazando vb/ib)
    awg0 = bytearray(awo[awg0_off:awg0_off + awg0_full_size])
    awg0[vb_off:vb_off + vb_size] = vb
    awg0[ib_off:ib_off + ib_size] = ib

    # Ensamblar el nuevo AWO:
    # header(0..awg0_off) + AWG0 nuevo + AWGs 1-17 (intactos) + axes-array (intacto)
    new_awo = bytearray()
    new_awo += awo[:awg0_off]                       # header + relaciones + tabla + labels
    new_awo += awg0                                 # AWG0 (mismo tamaño)
    # AWGs 1-17 (desde awg1_off hasta axes_off)
    new_awo += awo[awg1_off:axes_off]
    # axes-array (desde axes_off hasta fin)
    new_awo += awo[axes_off:]

    # Como el AWG0 mantuvo el mismo tamaño, los offsets no cambian.
    # La tabla AMG y +0x34 siguen válidos. Verificar.
    assert len(new_awo) == len(awo), 'Tamaño del AWO cambió: %d vs %d' % (len(new_awo), len(awo))
    print('AWO reconstruido: %d bytes (mismo tamaño)' % len(new_awo))

    # Empaquetar en AMB con DOS entradas: AWO (nuevo) + AZT (textura del original)
    # El bin original tiene: [0]=AWO (0x40, 290784), [1]=AZT (0x47020, 391680).
    # El conversor solo cambia la geometría (AWO), la textura AZT se copia tal cual.
    # Extraer la AZT del template original.
    n_orig = struct.unpack('>I', templ[0x0C:0x10])[0]
    azt_loc, azt_size = None, None
    for i in range(n_orig):
        e = 0x20 + i * 16
        loc, size = struct.unpack('>II', templ[e:e + 8])
        if templ[loc:loc + 4] == b'#AZT':
            azt_loc, azt_size = loc, size
            break
    if azt_loc is None:
        print('ERROR: no se encontró textura #AZT en la plantilla')
        return

    # AMB con 2 entradas: AWO (0x40) + AZT (después del AWO, alineado a 16)
    awo_start = 0x40
    azt_start = ((awo_start + len(new_awo) + 15) & ~15)
    amb = bytearray()
    amb += b'#AMB'
    amb += u32(0x20)
    amb += u32(0)
    amb += u32(2)               # 2 entradas
    amb += u32(2)
    amb += u32(0x20)
    amb += u32(0x40)
    amb += u32(0)
    # entrada 0: AWO
    amb += u32(awo_start)
    amb += u32(len(new_awo))
    amb += u32(1)
    amb += u32(0)
    # entrada 1: AZT
    amb += u32(azt_start)
    amb += u32(azt_size)
    amb += u32(2)
    amb += u32(0)
    amb += bytes(0x40 - len(amb))
    amb += new_awo
    amb += bytes(azt_start - len(amb))
    amb += templ[azt_loc:azt_loc + azt_size]

    with open(sys.argv[3], 'wb') as f:
        f.write(bytes(amb))
    print('AMB final: %d bytes (AWO %d + AZT %d)' % (len(amb), len(new_awo), azt_size))


if __name__ == '__main__':
    main()
