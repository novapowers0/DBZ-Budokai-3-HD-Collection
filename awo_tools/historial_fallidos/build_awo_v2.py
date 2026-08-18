"""
Conversor #AMO0 (PS2) → #AWO (HD) v2 — usando plantilla AWG real completa.

Usa el AWO HD existente como plantilla binaria COMPLETA (13 ejes + mesh group +
mesh-ref blocks encadenados + materiales). Reemplaza SOLO el vertex buffer e
index buffer del AWG0 con la geometría PS2 convertida (layout HD stride 0x2C),
preservando la estructura recursiva de mesh parts que el runtime espera.

La geometría PS2 (menos densa) se empaqueta en el formato HD. Los mesh-ref
blocks del hueso0 (13 parts) dibujan rangos del nuevo index buffer.

Uso:
  python build_awo_v2.py <bin_ps2> <bin_amb_hd_template> <output.bin>
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
            f32(vx) + f32(vy) +    # +04 +08 (pos x/y)
            f32(1.0) +             # +0C
            u32(0) +               # +10 hueso (0 = BODY)
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
        print('Uso: python build_awo_v2.py <bin_ps2> <bin_amb_hd_template> <output.bin>')
        return
    ps2 = open(sys.argv[1], 'rb').read()
    templ = open(sys.argv[2], 'rb').read()

    # --- Plantilla AWO HD ---
    entries = read_amb(templ)
    awo_loc, awo_size = entries[0]
    awo = bytearray(templ[awo_loc:awo_loc + awo_size])
    print('Plantilla AWO: %d bytes' % len(awo))

    tbl = struct.unpack('>I', awo[0x1C:0x20])[0]
    amg_am = struct.unpack('>I', awo[0x18:0x1C])[0]
    awg0_off = struct.unpack('>I', awo[tbl:tbl + 4])[0]
    # El AWG0 está en awo[awg0_off] (magic #AWG ahí). 
    # En el AMB, el AWO empieza en awo_loc; dentro de 'awo', el AWG0 está en awg0_off.
    awg_hdr = awg0_off
    print('AWG0 header @0x%X (magic %s)' % (awg_hdr, awo[awg_hdr:awg_hdr + 4]))

    # Campos del header AWG0 (relativos al header AWG = awg_hdr)
    vb_off = struct.unpack('>I', awo[awg_hdr + 0x2C:awg_hdr + 0x30])[0]
    ib_off = struct.unpack('>I', awo[awg_hdr + 0x30:awg_hdr + 0x34])[0]
    restart_off = struct.unpack('>I', awo[awg_hdr + 0x38:awg_hdr + 0x3C])[0]
    print('AWG0: vb=0x%X ib=0x%X restart=0x%X' % (vb_off, ib_off, restart_off))

    # Fin del AWG0 = inicio del AWG1
    awg1_off = struct.unpack('>I', awo[tbl + 4:tbl + 8])[0] if amg_am > 1 else len(awo)
    print('AWG1 @0x%X (fin AWG0)' % awg1_off)

    # --- Geometría PS2 ---
    model = PS2Model(ps2)
    amgs = model.amg_offsets()
    amg0_base = model.amo0 + amgs[0]
    parts = model.mesh_parts(amg0_base)
    all_verts = []
    for p in parts:
        all_verts.extend(p['verts'])
    print('Geometría PS2: %d parts, %d vértices' % (len(parts), len(all_verts)))

    # Vertex buffer (atributos, stride 0x2C)
    vb = b''.join(build_vertex_hd(v) for v in all_verts)

    # Index buffer: triangle list (cada part PS2 expande triángulos)
    ib = b''
    offset = 0
    for p in parts:
        nv = p['n_verts']
        for i in range(nv // 3):
            ib += u16(offset + i * 3) + u16(offset + i * 3 + 1) + u16(offset + i * 3 + 2)
        offset += nv
    print('VB: %d bytes | IB: %d bytes (%d índices)' % (len(vb), len(ib), len(ib) // 2))

    # --- Construir el nuevo AWG0 ---
    # El AWG0 va desde awg0_off hasta el antiguo vb_off (parte fija: header +
    # labels + ejes + mesh group + mesh-ref blocks + materiales).
    # Luego la geometría nueva (vb + ib + restart).
    # Los offsets del header AWG (+0x2C etc) son relativos al inicio del AWG (awg0_off).
    fixed_end = awg0_off + vb_off
    fixed = awo[awg0_off:fixed_end]
    new_awg = bytearray(fixed)

    # Nuevo vertex buffer (alineado a 16)
    new_vb_off = (len(new_awg) + 15) & ~15
    new_awg += bytes(new_vb_off - len(new_awg))
    new_awg += vb

    # Nuevo index buffer
    new_ib_off = (len(new_awg) + 15) & ~15
    new_awg += bytes(new_ib_off - len(new_awg))
    new_awg += ib

    # Restart buffer
    new_restart_off = (len(new_awg) + 15) & ~15
    new_awg += bytes(new_restart_off - len(new_awg))
    new_awg += bytes(0x200)
    for i in range(0, 0x200, 2):
        new_awg[new_restart_off + i:new_restart_off + i + 2] = u16(0xFFFF)

    # Actualizar offsets en el header del AWG0 (relativos al inicio del AWG0)
    new_awg[0x2C:0x30] = u32(new_vb_off)
    new_awg[0x30:0x34] = u32(new_ib_off)
    new_awg[0x38:0x3C] = u32(new_restart_off)

    # --- Ensamblar el nuevo AWO ---
    # Header + tablas + labels del AWO (0 a awg0_off) + nuevo AWG0 + AWGs 1-17
    new_awo = bytearray()
    new_awo += awo[:awg0_off]              # header + relaciones + tabla + labels
    awg0_size = len(new_awg)
    new_awo += new_awg                     # AWG0 nuevo

    # AWGs 1-17 (intactos)
    for i in range(1, amg_am):
        off = struct.unpack('>I', awo[tbl + i * 4:tbl + i * 4 + 4])[0]
        end = struct.unpack('>I', awo[tbl + i * 4 + 4:tbl + i * 4 + 8])[0] if i + 1 < amg_am else len(awo)
        new_awo += awo[off:end]

    # Actualizar tabla de offsets AMG (offsets absolutos en el nuevo AWO)
    # La tabla está en el header (offset tbl relativo al AWO).
    # Los AWGs empiezan en awg0_off.
    awg_sizes = [awg0_size]
    for i in range(1, amg_am):
        off = struct.unpack('>I', awo[tbl + i * 4:tbl + i * 4 + 4])[0]
        end = struct.unpack('>I', awo[tbl + i * 4 + 4:tbl + i * 4 + 8])[0] if i + 1 < amg_am else len(awo)
        awg_sizes.append(end - off)

    cur = awg0_off
    for i in range(amg_am):
        new_awo[tbl + i * 4:tbl + i * 4 + 4] = u32(cur)
        cur += awg_sizes[i]

    # --- Empaquetar en #AMB ---
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
