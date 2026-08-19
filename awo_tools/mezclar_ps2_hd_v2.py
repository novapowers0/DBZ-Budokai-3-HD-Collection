"""
Mezclar posiciones PS2 en slots del sec34 con ESCALA POR HUESO.

El HD de Krillin es un modelo re-trabajado: las coords locales PS2 y HD no
comparten escala. Para cada hueso HD (par = PS2*2), derivar la escala como
mediana(mag coords HD) / mediana(mag coords world PS2 del hueso), y aplicar
esa escala a las coords PS2 antes de inyectar en los slots.

Las coords PS2 se transforman a world (pose_matrix) y luego se escalan al
rango del hueso HD. Si no hay cobertura, se mantiene la pos HD original.

Uso:
  python mezclar_ps2_hd_v2.py <bin_hd> <bin_ps2> <output_bin>
"""
import struct
import sys
import math

sys.path.insert(0, r'C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\awo_tools')
from extract_geometry import PS2Model
from convert_personaje import SkinData
from pose_matrix import build_world_mats, apply_mat

U32 = struct.Struct('>I')
F32 = struct.Struct('>f')


def u32(b, o): return U32.unpack_from(b, o)[0]
def f32(b, o): return F32.unpack_from(b, o)[0]
def pack_f32(v): return F32.pack(v)


def median(vals):
    if not vals:
        return 0.0
    s = sorted(vals)
    return s[len(s) // 2]


def main():
    if len(sys.argv) < 4:
        print('Uso: mezclar_ps2_hd_v2.py <bin_hd> <bin_ps2> <output>')
        return
    hd = open(sys.argv[1], 'rb').read()
    ps2 = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    # --- Leer sec34 HD por hueso (indices HD = pares) ---
    awo = bytearray(hd[0x40:])
    AWG = u32(awo, u32(awo, 0x1C))
    sec34 = u32(awo, AWG + 0x34)
    vb2 = u32(awo, AWG + 0x2C)
    n_sec = (vb2 - sec34 - 2) // 44
    hd_by_bone = {}
    for i in range(n_sec):
        off = AWG + sec34 + 2 + i * 44
        bn = u32(awo, off + 28)
        hd_by_bone.setdefault(bn, []).append(
            (f32(awo, off + 16), f32(awo, off + 20), f32(awo, off + 12)))

    # --- Skin PS2 + matrices world ---
    model = PS2Model(ps2)
    amg0_base = model.amo0 + model.amg_offsets()[0]
    skin = SkinData(ps2, amg0_base)
    mats, _ = build_world_mats(ps2, model.amo0)
    ps2_by_bone = {}
    for bone, weight, coords, voff in skin.entries:
        ps2_by_bone.setdefault(bone, []).append(coords)

    # --- Calcular escala por hueso HD (par) ---
    # escala(hd_bone) = median(mag coords HD) / median(mag coords world PS2)
    scales = {}
    for hb, hcoords in hd_by_bone.items():
        pb = hb // 2
        pc = ps2_by_bone.get(pb, [])
        if not pc or pb not in mats:
            continue
        m, p = mats[pb]
        world_mags = [math.sqrt(sum(v * v for v in apply_mat(m, p, c))) for c in pc]
        hd_mags = [math.sqrt(c[0] ** 2 + c[1] ** 2 + c[2] ** 2) for c in hcoords]
        hm, wm = median(hd_mags), median(world_mags)
        if wm > 0.01:
            scales[hb] = hm / wm
    print('Escalas por hueso HD: %d calculadas' % len(scales))

    # --- Inyectar: para cada slot, transformar PS2 -> world -> escalar -> local HD ---
    changed = 0
    kept = 0
    no_cov = 0
    for i in range(n_sec):
        off = AWG + sec34 + 2 + i * 44
        hb = u32(awo, off + 28)
        pb = hb // 2
        cand = ps2_by_bone.get(pb)
        m, p = mats.get(pb, (None, None))
        s = scales.get(hb)
        if not cand or m is None or s is None:
            no_cov += 1
            continue
        # pos local HD actual (para elegir el PS2 mas cercano en world)
        hx, hy, hz = f32(awo, off + 16), f32(awo, off + 20), f32(awo, off + 12)
        best = None
        best_d = 1e18
        for (cx, cy, cz) in cand:
            # coords world PS2 del vertice
            wx, wy, wz = apply_mat(m, p, (cx, cy, cz))
            # escalar al rango HD
            sx, sy, sz = wx * s, wy * s, wz * s
            d = (sx - hx) ** 2 + (sy - hy) ** 2 + (sz - hz) ** 2
            if d < best_d:
                best_d = d
                best = (sx, sy, sz)
        if best is None:
            kept += 1
            continue
        # reemplazar pos local (layout: +12 z, +16 x, +20 y)
        awo[off + 12:off + 16] = pack_f32(best[2])
        awo[off + 16:off + 20] = pack_f32(best[0])
        awo[off + 20:off + 24] = pack_f32(best[1])
        changed += 1

    print('Slots reescritos (escala por hueso): %d | mantenidos HD: %d | sin cobertura: %d' % (
        changed, kept, no_cov))

    amb = bytearray(hd[:0x40])
    amb += awo
    with open(out, 'wb') as f:
        f.write(bytes(amb))
    print('Guardado: %s (%d bytes)' % (out, len(amb)))


if __name__ == '__main__':
    main()
