"""
Mezclar posiciones PS2 en slots del sec34 HD por ORDEN SECUENCIAL del skin.

En vez de buscar el vertice PS2 'mas cercano' por posicion (que elige mal porque
PS2 y HD son geometrias distintas), asignar los vertices PS2 del hueso `pb` a los
slots HD del hueso `pb*2` EN ORDEN: el slot HD i-esimo del hueso recibe el
vertice PS2 i-esimo del hueso. Esto preserva la coherencia estructural.

Las coords PS2 se usan tal cual (espacio local del hueso, que es lo que el HD
espera). Sin escala (mix1 era mejor que mix2).

Uso:
  python mezclar_ps2_hd_v3.py <bin_hd> <bin_ps2> <output_bin>
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
        print('Uso: mezclar_ps2_hd_v3.py <bin_hd> <bin_ps2> <output>')
        return
    hd = open(sys.argv[1], 'rb').read()
    ps2 = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    awo = bytearray(hd[0x40:])
    AWG = u32(awo, u32(awo, 0x1C))
    sec34 = u32(awo, AWG + 0x34)
    vb2 = u32(awo, AWG + 0x2C)
    n_sec = (vb2 - sec34 - 2) // 44

    # Skin PS2: por hueso, lista de coords locales EN ORDEN
    model = PS2Model(ps2)
    amg0_base = model.amo0 + model.amg_offsets()[0]
    skin = SkinData(ps2, amg0_base)
    ps2_by_bone = {}
    for bone, weight, coords, voff in skin.entries:
        ps2_by_bone.setdefault(bone, []).append(coords)

    # Para cada slot HD, tomar el siguiente vertice PS2 disponible del hueso
    counters = {}
    changed = 0
    no_cov = 0
    for i in range(n_sec):
        off = AWG + sec34 + 2 + i * 44
        hb = u32(awo, off + 28)
        pb = hb // 2
        cand = ps2_by_bone.get(pb)
        if not cand:
            no_cov += 1
            continue
        # siguiente vertice PS2 del hueso (round-robin sobre los disponibles)
        k = counters.get(pb, 0)
        c = cand[k % len(cand)]
        counters[pb] = k + 1
        # reemplazar pos local (layout: +12 z, +16 x, +20 y)
        awo[off + 12:off + 16] = pack_f32(c[2])
        awo[off + 16:off + 20] = pack_f32(c[0])
        awo[off + 20:off + 24] = pack_f32(c[1])
        changed += 1

    print('Slots reescritos (orden secuencial): %d | sin cobertura: %d' % (changed, no_cov))

    amb = bytearray(hd[:0x40])
    amb += awo
    with open(out, 'wb') as f:
        f.write(bytes(amb))
    print('Guardado: %s (%d bytes)' % (out, len(amb)))


if __name__ == '__main__':
    main()
