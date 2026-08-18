"""
Conversor #AMO0 (PS2) → #AWO (HD) v3 — plantilla con tamaños fijos.

La causa del crash del v2 era que el runtime lee el VB/IB con los TAMAÑOS
originales del HD (9984 / 10280 bytes). Al reemplazar con tamaños distintos,
los punteros internos del AWG (relativos al header AWG) se rompían.

v3 mantiene los tamaños originales:
- VB: rellena los 9984 bytes con vértices PS2 (los que quepan) + padding
- IB: rellena los 10280 bytes con índices PS2 + 0xFFFF (restart) al final

Así los offsets del header AWG y los mesh-ref blocks siguen válidos.

Uso:
  python build_awo_v3.py <bin_ps2> <bin_amb_hd_template> <output.bin>
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
    """Vértice PS2 (V+VN+VT) -> layout HD (stride 0x2C)."""
    vx, vy, vz = vert['pos']
    nx, ny, nz = vert['nrm']
    u, vt = vert['uv']
    return (f32(vz) +              # +00 V.z
            f32(vx) + f32(vy) +    # +04 +08
            f32(1.0) +             # +0C
            u32(0) +               # +10 hueso
            f32(nz) +              # +14 VN.z
            f32(-ny) +             # +18 -VN.y
            f32(nx) +              # +1C VN.x
            u32(0xFFFFFFFF) +      # +20
            f32(u) + f32(vt))      # +24 +28 VT


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
        print('Uso: python build_awo_v3.py <bin_ps2> <bin_amb_hd_template> <output.bin>')
        return
    ps2 = open(sys.argv[1], 'rb').read()
    templ = open(sys.argv[2], 'rb').read()

    entries = read_amb(templ)
    awo_loc, awo_size = entries[0]
    awo = bytearray(templ[awo_loc:awo_loc + awo_size])
    print('Plantilla AWO: %d bytes' % len(awo))

    tbl = struct.unpack('>I', awo[0x1C:0x20])[0]
    amg_am = struct.unpack('>I', awo[0x18:0x1C])[0]
    awg0_off = struct.unpack('>I', awo[tbl:tbl + 4])[0]
    awg_hdr = awg0_off
    print('AWG0 header @0x%X (magic %s)' % (awg_hdr, awo[awg_hdr:awg_hdr + 4]))

    vb_off = struct.unpack('>I', awo[awg_hdr + 0x2C:awg_hdr + 0x30])[0]
    ib_off = struct.unpack('>I', awo[awg_hdr + 0x30:awg_hdr + 0x34])[0]
    restart_off = struct.unpack('>I', awo[awg_hdr + 0x38:awg_hdr + 0x3C])[0]
    vb_size = ib_off - vb_off
    ib_size = restart_off - ib_off
    print('AWG0 original: vb=0x%X (%dB) ib=0x%X (%dB) restart=0x%X' % (
        vb_off, vb_size, ib_off, ib_size, restart_off))

    # Geometría PS2
    model = PS2Model(ps2)
    amgs = model.amg_offsets()
    amg0_base = model.amo0 + amgs[0]
    parts = model.mesh_parts(amg0_base)
    all_verts = []
    for p in parts:
        all_verts.extend(p['verts'])
    print('Geometría PS2: %d parts, %d vértices' % (len(parts), len(all_verts)))

    # VB: vértices PS2 al layout HD (stride 0x2C), rellenar al tamaño original
    vb_data = b''.join(build_vertex_hd(v) for v in all_verts)
    vb = vb_data[:vb_size].ljust(vb_size, b'\x00')
    print('VB: PS2=%d bytes -> %d bytes (relleno)' % (len(vb_data), len(vb)))

    # Cuántos vértices PS2 caben en el VB (para que los índices sean válidos)
    max_vb_verts = vb_size // 0x2C
    n_verts_fit = min(len(all_verts), max_vb_verts)
    print('VB: caben %d vértices de %d' % (n_verts_fit, len(all_verts)))

    # IB: triangle list PS2, pero SOLO con índices < n_verts_fit
    # (los triángulos que usen vértices fuera del VB se descartan)
    ib_data = b''
    offset = 0
    for p in parts:
        nv = p['n_verts']
        for i in range(nv // 3):
            i0 = offset + i * 3
            i1 = offset + i * 3 + 1
            i2 = offset + i * 3 + 2
            if i2 < n_verts_fit:
                ib_data += u16(i0) + u16(i1) + u16(i2)
        offset += nv
    ib = bytearray(ib_data[:ib_size])
    while len(ib) < ib_size:
        ib += u16(0xFFFF)
    print('IB: PS2=%d bytes -> %d bytes (relleno FFFF), triángulos válidos: %d' % (
        len(ib_data), len(ib), len(ib_data) // 6))

    # Construir el nuevo AWG0: copiar el AWG0 completo y reemplazar vb/ib
    # (los offsets del header AWG no cambian porque los tamaños son iguales)
    awg0 = bytearray(awo[awg0_off:awg0_off + vb_off + vb_size + ib_size + 0xDD0])
    awg0[vb_off:vb_off + vb_size] = vb
    awg0[ib_off:ib_off + ib_size] = ib
    # (restart se deja como estaba)

    # El nuevo AWO = header AWO + AWG0 nuevo + AWGs 1-17
    new_awo = bytearray()
    new_awo += awo[:awg0_off]
    new_awo += awg0
    awg0_size = len(awg0)
    for i in range(1, amg_am):
        off = struct.unpack('>I', awo[tbl + i * 4:tbl + i * 4 + 4])[0]
        end = struct.unpack('>I', awo[tbl + i * 4 + 4:tbl + i * 4 + 8])[0] if i + 1 < amg_am else len(awo)
        new_awo += awo[off:end]

    # Actualizar tabla de offsets AMG
    awg_sizes = [awg0_size]
    for i in range(1, amg_am):
        off = struct.unpack('>I', awo[tbl + i * 4:tbl + i * 4 + 4])[0]
        end = struct.unpack('>I', awo[tbl + i * 4 + 4:tbl + i * 4 + 8])[0] if i + 1 < amg_am else len(awo)
        awg_sizes.append(end - off)
    cur = awg0_off
    for i in range(amg_am):
        new_awo[tbl + i * 4:tbl + i * 4 + 4] = u32(cur)
        cur += awg_sizes[i]

    # Empaquetar en AMB
    final_awo = bytes(new_awo)
    amb = bytearray()
    amb += b'#AMB'
    amb += u32(0x20)
    amb += u32(0)
    amb += u32(1)
    amb += u32(1)
    amb += u32(0x20)
    amb += u32(0x40)
    amb += u32(0)
    amb += u32(0x40)
    amb += u32(len(final_awo))
    amb += u32(1)
    amb += u32(0)
    amb += bytes(0x40 - len(amb))
    amb += final_awo

    with open(sys.argv[3], 'wb') as f:
        f.write(amb)
    print()
    print('AWO final: %d bytes | AMB: %d bytes' % (len(final_awo), len(amb)))


if __name__ == '__main__':
    main()
