"""
Conversor #AMO0 (PS2) → #AWO (HD) basado en PLANTILLA.

Usa el AWO HD existente como plantilla (estructura completa: ejes, armatures,
mesh groups, mesh-ref blocks intactos) y reemplaza SOLO la geometría (vértices
e índices) por la del modelo PS2 convertido al layout HD.

Esto evita reconstruir la estructura compleja del AWG desde cero y reduce el
riesgo de crash. La plantilla conserva el esqueleto y la jerarquía.

Uso:
  python build_awo_template.py <bin_ps2> <bin_amb_hd_template> <output.bin>
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
    """Convierte un vértice PS2 al layout HD (stride 0x2C)."""
    vx, vy, vz = vert['pos']
    nx, ny, nz = vert['nrm']
    u, vt = vert['uv']
    return (f32(vz) +              # +00 V.z
            f32(1.0) + f32(0.0) +  # +04 +08 weights placeholder
            f32(0.0) + f32(0.0) +  # +0C +10
            f32(nz) +              # +14 VN.z
            f32(-ny) +             # +18 -VN.y
            f32(nx) +              # +1C VN.x
            f32(1.0) +             # +20
            f32(u) + f32(vt))      # +24 +28 VT.u/v


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
        print('Uso: python build_awo_template.py <bin_ps2> <bin_amb_hd_template> <output.bin>')
        return
    ps2 = open(sys.argv[1], 'rb').read()
    templ = open(sys.argv[2], 'rb').read()

    # --- Leer la plantilla AWO HD ---
    entries = read_amb(templ)
    awo_loc = entries[0][0]
    awo_size = entries[0][1]
    awo = bytearray(templ[awo_loc:awo_loc + awo_size])
    print('Plantilla AWO: %d bytes' % len(awo))

    # Extraer la estructura del AWG0 de la plantilla (punteros del header)
    tbl = struct.unpack('>I', awo[0x1C:0x20])[0]
    amg_am = struct.unpack('>I', awo[0x18:0x1C])[0]
    awg0_off = struct.unpack('>I', awo[tbl:tbl + 4])[0]
    print('AWG0 @0x%X' % awg0_off)

    # Header AWG0 real: campos de datos
    axes_loc = struct.unpack('>I', awo[awg0_off + 0x14:awg0_off + 0x18])[0]
    vb_off = struct.unpack('>I', awo[awg0_off + 0x2C:awg0_off + 0x30])[0]
    ib_off = struct.unpack('>I', awo[awg0_off + 0x30:awg0_off + 0x34])[0]
    restart_off = struct.unpack('>I', awo[awg0_off + 0x38:awg0_off + 0x3C])[0]
    print('axes=0x%X vb=0x%X ib=0x%X restart=0x%X' % (axes_loc, vb_off, ib_off, restart_off))

    # --- Extraer la geometría PS2 ---
    model = PS2Model(ps2)
    amgs = model.amg_offsets()
    amg0_base = model.amo0 + amgs[0]
    parts = model.mesh_parts(amg0_base)
    all_verts = []
    for p in parts:
        all_verts.extend(p['verts'])
    print('Geometría PS2: %d parts, %d vértices' % (len(parts), len(all_verts)))

    # --- Construir el vertex buffer HD (atributos, stride 0x2C) ---
    vb = b''.join(build_vertex_hd(v) for v in all_verts)

    # --- Construir el index buffer (triangle list) ---
    ib = b''
    offset = 0
    for p in parts:
        nv = p['n_verts']
        for i in range(nv // 3):
            ib += u16(offset + i * 3) + u16(offset + i * 3 + 1) + u16(offset + i * 3 + 2)
        offset += nv
    print('VB: %d bytes | IB: %d bytes (%d indices)' % (len(vb), len(ib), len(ib) // 2))

    # --- Reemplazar en la plantilla ---
    # Calculamos el delta de tamaño: la geometría nueva vs la vieja.
    # Las zonas de datos del AWG0 van desde vb_off hasta restart_off + tamaño restart.
    # Preservamos: header, labels, ejes, mesh groups, mesh-ref blocks (hasta vb_off).
    # Luego insertamos: vb nuevo + ib nuevo + restart.
    #
    # NOTA: el tamaño puede cambiar. Re-layamos la geometría al final del AWG0.
    awg0_end = len(awo)
    # Calculamos cuánto espacio hay entre vb_off y el final del AWG0 (o siguiente AWG)
    if amg_am > 1:
        awg1_off = struct.unpack('>I', awo[tbl + 4:tbl + 8])[0]
        awg0_end = awg1_off
    # espacio utilizable para geometría
    data_start = vb_off
    data_end = awg0_end

    new_geometry_size = len(vb) + len(ib) + 0x100  # restart buffer
    old_geometry_size = data_end - data_start
    delta = new_geometry_size - old_geometry_size
    print('Geometría: old=%d new=%d delta=%d' % (old_geometry_size, new_geometry_size, delta))

    # Construimos el nuevo AWG0: parte fija (hasta vb_off) + nueva geometría
    fixed = awo[:vb_off]
    new_awg = bytearray(fixed)

    # Nuevo vertex buffer offset (después de la parte fija, alineado a 16)
    new_vb_off = (len(new_awg) + 15) & ~15
    new_awg += bytes(new_vb_off - len(new_awg))
    new_awg += vb
    new_ib_off = (len(new_awg) + 15) & ~15
    new_awg += bytes(new_ib_off - len(new_awg))
    new_awg += ib
    new_restart_off = (len(new_awg) + 15) & ~15
    new_awg += bytes(new_restart_off - len(new_awg))
    new_awg += bytes(0x100)  # restart buffer (FFFF)
    for i in range(0, 0x100, 2):
        new_awg[new_restart_off + i:new_restart_off + i + 2] = u16(0xFFFF)

    # Actualizar los offsets en el header del AWG0
    new_awg[awg0_off + 0x2C:awg0_off + 0x30] = u32(new_vb_off)
    new_awg[awg0_off + 0x30:awg0_off + 0x34] = u32(new_ib_off)
    new_awg[awg0_off + 0x38:awg0_off + 0x3C] = u32(new_restart_off)

    # NOTA: los AWGs 1-17 (manos/cara) se preservan del template.
    # El nuevo AWO = nuevo AWG0 + AWGs 1-17 intactos.
    # El delta desplaza los AWGs posteriores; actualizamos la tabla de offsets.
    new_awo = bytearray(awo[:awg0_off])
    new_awo += new_awg
    # Añadir AWGs 1-17 (con desplazamiento delta)
    for i in range(1, amg_am):
        off = struct.unpack('>I', awo[tbl + i * 4:tbl + i * 4 + 4])[0]
        end = struct.unpack('>I', awo[tbl + i * 4 + 4:tbl + i * 4 + 8])[0] if i + 1 < amg_am else len(awo)
        seg = awo[off:end]
        new_awo += seg

    # Actualizar la tabla de offsets AMG
    cur = awg0_off
    new_awo[tbl:tbl + 4] = u32(awg0_off)
    for i in range(1, amg_am):
        # calcular el nuevo offset del AWG i
        # (reconstruimos sumando tamaños)
        pass
    # recalculamos offsets: AWG0 nuevo + AWGs 1-17
    awg_sizes = []
    # AWG0
    awg_sizes.append(len(new_awg))
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
    print('AWO final: %d bytes | AMB: %d bytes' % (len(final_awo), len(amb)))


if __name__ == '__main__':
    main()
