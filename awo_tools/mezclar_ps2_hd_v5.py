"""Mezclar posiciones PS2 (B3) en slots del sec34 HD con layout REAL.

Layout REAL del vertice B3 (stride 44, alineacion +2, verificado 2026-08-17):
  +0  0xFFFFFFFF (nan marker)
  +4  u (float)
  +8  v (float)
  +12 z_local (float)
  +16 x_local (float)
  +20 y_local (float)
  +24 peso (float, 0.1-1.0)
  +28 BONE (u32, 0-35)
  +32 nrm.z (float)
  +36 nrm.y negado (float)
  +40 nrm.x (float)

El HD de Krillin comparte pose/esqueleto con el PS2 (51/51 matrices identicas).
Inyeccion: por cada slot sec34 HD con bone B, buscar el vertice PS2 del mismo
hueso (skin via rig) mas cercano por coords locales, y escribir sus coords en
+12/+16/+20 (z, x, y local). Mantiene IB/arms/vb2/AZT nativos -> el bin
mantiene su tamano y cabe en el slot (106496). Sin re-layout -> sin crash.

Uso:
  python mezclar_ps2_hd_v5.py <bin_hd> <bin_ps2> <output_bin>
"""
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_geometry import PS2Model

U32 = struct.Struct('>I')
F32 = struct.Struct('>f')


def u32(b, o): return U32.unpack_from(b, o)[0]
def f32(b, o): return F32.unpack_from(b, o)[0]
def pack_f32(v): return F32.pack(v)


def skin_ps2_by_bone(ps2):
    """Rig PS2: {bone: [coords_locales, ...]} (mismo que convert_personaje)."""
    model = PS2Model(ps2)
    amg0_base = model.amo0 + model.amg_offsets()[0]
    b = ps2
    amg = amg0_base
    bone_am = struct.unpack('<I', b[amg + 0x10:amg + 0x14])[0]
    axes_loc = struct.unpack('<I', b[amg + 0x14:amg + 0x18])[0]
    by_bone = {}
    for bi in range(bone_am):
        e0 = amg + axes_loc + bi * 80
        p34 = struct.unpack('<I', b[e0 + 0x34:e0 + 0x38])[0]
        if p34 == 0:
            continue
        arm = amg + p34
        rig_ptr = struct.unpack('<I', b[arm + 8:arm + 12])[0]
        if not rig_ptr:
            continue
        r = amg + rig_ptr
        wv_am = struct.unpack('<I', b[r + 12:r + 16])[0]
        for wg in range(wv_am):
            wo = r + 16 + wg * 32
            weight = struct.unpack('<f', b[wo:wo + 4])[0]
            vvn_am = struct.unpack('<I', b[wo + 4:wo + 8])[0]
            vvn_loc = struct.unpack('<I', b[wo + 8:wo + 12])[0]
            v_am = struct.unpack('<I', b[wo + 12:wo + 16])[0]
            v_loc = struct.unpack('<I', b[wo + 16:wo + 20])[0]
            if vvn_am and vvn_loc:
                vvn_abs = amg + vvn_loc
                for i in range(vvn_am):
                    e = vvn_abs + i * 32
                    coords = struct.unpack('<fff', b[e:e + 12])
                    by_bone.setdefault(bi, []).append((weight, coords))
            if v_am and v_loc:
                v_abs = amg + v_loc
                for i in range(v_am):
                    e = v_abs + i * 16
                    coords = struct.unpack('<fff', b[e:e + 12])
                    by_bone.setdefault(bi, []).append((weight, coords))
    return by_bone


def main():
    if len(sys.argv) < 4:
        print('Uso: mezclar_ps2_hd_v5.py <bin_hd> <bin_ps2> <output>')
        return
    hd = open(sys.argv[1], 'rb').read()
    ps2 = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    ps2_by_bone = skin_ps2_by_bone(ps2)
    total_ps2 = sum(len(v) for v in ps2_by_bone.values())
    print('PS2 skin: %d bones con %d coords locales' % (len(ps2_by_bone), total_ps2))

    AWO = 0x40
    awg_abs = AWO + u32(hd, AWO + u32(hd, AWO + 0x1C))
    sec34_abs = awg_abs + u32(hd, awg_abs + 0x34)
    vb2_abs = awg_abs + u32(hd, awg_abs + 0x2C)
    n_sec = (vb2_abs - sec34_abs - 2) // 44
    print('HD sec34: %d slots' % n_sec)

    data = bytearray(hd)
    changed = 0
    no_bone = 0
    no_cov = 0
    for i in range(n_sec):
        base = sec34_abs + 2 + i * 44
        hb = u32(data, base + 28)
        if hb not in ps2_by_bone:
            no_bone += 1
            continue
        hz = f32(data, base + 12); hx = f32(data, base + 16); hy = f32(data, base + 20)
        cand = ps2_by_bone[hb]
        best = None
        best_d = 1e18
        for w, c in cand:
            d = (c[0] - hx) ** 2 + (c[1] - hy) ** 2 + (c[2] - hz) ** 2
            if d < best_d:
                best_d = d
                best = c
        if best is None:
            no_cov += 1
            continue
        data[base + 12:base + 16] = pack_f32(best[2])  # z local
        data[base + 16:base + 20] = pack_f32(best[0])  # x local
        data[base + 20:base + 24] = pack_f32(best[1])  # y local
        changed += 1

    print('Slots reescritos: %d | sin bone en PS2: %d | sin cobertura: %d' % (
        changed, no_bone, no_cov))

    with open(out, 'wb') as f:
        f.write(bytes(data))
    print('Guardado: %s (%d bytes)' % (out, len(data)))


if __name__ == '__main__':
    main()