"""
Mezclar posiciones PS2 en los slots existentes del sec34 del bin HD, manteniendo
IB/arms/vb2/AZT nativos. Krillin como conejillo de indias.

Vía validada por Janemba v7: NO reconstruir el IB. El runtime dibuja por
mesh-ref blocks + arms (IB nativo). Solo reescribimos las posiciones locales de
los vértices del sec34 en sus slots, manteniendo bone indices.

Mapeo de huesos: HD bone = PS2 bone * 2 (labels en indices pares).
Para cada slot sec34 HD con bone B:
  - pb = B // 2  (bone PS2)
  - si el PS2 tiene vertices skinned a pb, tomar el mas cercano (por coords
    locales) al HD original y reemplazar la pos local.
  - si no, mantener la pos HD original.

Uso:
  python mezclar_ps2_hd.py <bin_hd> <bin_ps2> <output_bin>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..\..', 'awo_tools')))
from extract_geometry import PS2Model
from convert_personaje import SkinData

U32 = struct.Struct('>I')
F32 = struct.Struct('>f')


def u32(b, o): return U32.unpack_from(b, o)[0]
def f32(b, o): return F32.unpack_from(b, o)[0]


def pack_f32(v): return F32.pack(v)


def main():
    if len(sys.argv) < 4:
        print('Uso: mezclar_ps2_hd.py <bin_hd> <bin_ps2> <output>')
        return
    hd = open(sys.argv[1], 'rb').read()
    ps2 = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    # --- Leer skin PS2: bone -> lista de coords locales (pos_local al hueso) ---
    model = PS2Model(ps2)
    amg0_base = model.amo0 + model.amg_offsets()[0]
    skin = SkinData(ps2, amg0_base)
    ps2_coords = {}
    for bone, weight, coords, voff in skin.entries:
        ps2_coords.setdefault(bone, []).append(coords)
    print('PS2 skin bones:', sorted(ps2_coords.keys()))

    # --- Recorrer sec34 del HD ---
    awo = bytearray(hd[0x40:])
    AWG = u32(awo, u32(awo, 0x1C))
    sec34 = u32(awo, AWG + 0x34)
    vb2 = u32(awo, AWG + 0x2C)
    n_sec = (vb2 - sec34 - 2) // 44
    print('HD sec34: %d slots' % n_sec)

    changed = 0
    kept = 0
    no_ps2 = 0
    for i in range(n_sec):
        off = AWG + sec34 + 2 + i * 44
        bone = u32(awo, off + 28)
        pb = bone  # hueso PS2 DIRECTO (matrices HD zona[idx] == PS2[idx])
        cand = ps2_coords.get(pb)
        if not cand:
            no_ps2 += 1
            continue
        # pos local HD actual: +12 z, +16 x, +20 y
        hx, hy, hz = f32(awo, off + 16), f32(awo, off + 20), f32(awo, off + 12)
        # buscar el PS2 mas cercano
        best = None
        best_d = 1e18
        for (cx, cy, cz) in cand:
            d = (cx - hx) ** 2 + (cy - hy) ** 2 + (cz - hz) ** 2
            if d < best_d:
                best_d = d
                best = (cx, cy, cz)
        if best is None:
            kept += 1
            continue
        # reemplazar pos local (z,x,y)
        awo[off + 12:off + 16] = pack_f32(best[2])
        awo[off + 16:off + 20] = pack_f32(best[0])
        awo[off + 20:off + 24] = pack_f32(best[1])
        changed += 1

    print('Slots reescritos con PS2: %d | mantenidos HD: %d | sin PS2 (kept HD): %d' % (
        changed, kept, no_ps2))

    # --- Repack AMB (reemplazar el AWO dentro del AMB) ---
    amb = bytearray(hd[:0x40])
    amb += awo
    # el AWO puede tener el mismo tamano (solo reescribimos floats en sitio)
    with open(out, 'wb') as f:
        f.write(bytes(amb))
    print('Guardado: %s (%d bytes)' % (out, len(amb)))


if __name__ == '__main__':
    main()
