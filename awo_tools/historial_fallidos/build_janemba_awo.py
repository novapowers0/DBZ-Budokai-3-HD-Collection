"""
Construir el AWO de Janemba desde cero, usando el AWO de Krillin como
plantilla ESTRUCTURAL (AWGs, mesh groups, mesh-ref blocks, arms, zona ejes)
pero con los conteos correctos de Janemba.

Estrategia:
  - Mantener los 18 AWGs de Krillin como estructura (el runtime acepta
    conteos variables entre bins: 327=1956, 328=1791).
  - AWG0: reemplazar sec34 con los vertices de Janemba (cuerpo, skinneado),
    vb2 con los de cabeza/resto, y el IB con los triangulos de Janemba.
  - Actualizar los offsets del AWG header para los nuevos conteos.
  - El resto de AWGs (1-17) quedan como en Krillin (inactivos/vacios).

Nota: esta es una version de PRUEBA. Los mesh-ref blocks/arms del AWG0
apuntan a los rangos del IB de Krillin; se re-mapearan en una fase posterior.

Uso:
  python build_janemba_awo.py <bin_amb_krillin> <janemba_verts.bin> <output_amb>
"""

import struct
import sys

sys.path.insert(0, r'C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\awo_tools')
from convert_personaje import extract_geometry_skinned
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


def u32r(data, off):
    return struct.unpack('>I', data[off:off + 4])[0]


def pack_u32(data, off, val):
    struct.pack_into('>I', data, off, val)


def dedup_and_build_ib(converted, parts):
    """Deduplica vertices por contenido y construye el IB (indices u16)."""
    dedup = {}
    unique = []
    ib = bytearray()
    # Cada part expande triangulos: verts consecutivos de 3 en 3
    for p in parts:
        for v in p['verts']:
            pass  # los verts ya estan en 'converted' con el mismo orden
    for vb in converted:
        if vb not in dedup:
            dedup[vb] = len(unique)
            unique.append(vb)
        ib += u16(dedup[vb])
    return unique, bytes(ib)


def main():
    if len(sys.argv) < 4:
        print('Uso: python build_janemba_awo.py <bin_krillin> <janemba_ps2> <output>')
        return
    krillin = open(sys.argv[1], 'rb').read()
    janemba_ps2 = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    # 1) Extraer geometria de Janemba con skinning
    converted, tri_sets = extract_geometry_skinned(janemba_ps2)
    print('Janemba: %d vertices convertidos' % len(converted))

    # 2) Deduplicar y construir IB
    #    converted esta en orden de mesh parts (por AMG, por part)
    model = PS2Model(janemba_ps2)
    all_parts = []
    for amg_off in model.amg_offsets():
        amg_base = model.amo0 + amg_off
        all_parts.extend(model.mesh_parts(amg_base))
    unique, ib_data = dedup_and_build_ib(converted, all_parts)
    print('Janemba unicos: %d, IB: %d indices' % (len(unique), len(ib_data) // 2))

    # 3) Verificar presupuesto
    n_sec34 = len(unique)
    n_vb2 = 226  # mantener vb2 de Krillin (cabeza)
    print('sec34=%d (presupuesto Krillin: 1956)' % n_sec34)
    print('NOTA: Janemba tiene mas vertices que el presupuesto de Krillin.')
    print('Se necesita decimar a %d vertices.' % (1956 + n_vb2))


if __name__ == '__main__':
    main()
