"""Mezclar posiciones PS2 (B3) en slots del sec34 HD por WORLD COORDS.

v6 (2026-08-17): a diferencia de v5 (emparejamiento por coords LOCALES), aqui
se transforman ambos modelos al espacio WORLD usando las matrices de pose
(idénticas 47/47 verificadas entre HD y PS2) y se empareja cada slot HD con el
vertice PS2 del MISMO hueso cuyo WORLD este mas cerca.

Por que world y no local: las coords locales estan en el espacio del hueso.
Si el HD es un re-trabajo con proporciones distintas (conteos por hueso
distintos), la cercania LOCAL mezcla vertices de zonas distintas del cuerpo.
En world, el vertice del pecho esta arriba y el de la cadera abajo -> el
emparejamiento por world selecciona el PS2 en la MISMA zona fisica.

Layout REAL del vertice B3 (stride 44, alineacion +2, verificado):
  +0  0xFFFFFFFF | +4 u | +8 v | +12 z_local | +16 x_local | +20 y_local
  +24 peso | +28 BONE(u32) | +32 nrm.z | +36 -nrm.y | +40 nrm.x

Uso:
  python mezclar_ps2_hd_v6.py <bin_hd> <bin_ps2> <output_bin> [--keep-bone0] [--umbral F]
    --keep-bone0: no reescribir slots del bone 0 (BODY, cubre todo el torso;
                  suele ser el mas deforme por no tener coords PS2 validas)
    --umbral F:   solo reescribir slots cuyo match WORLD tenga distancia <= F
                  (0.15-0.3 = bueno; por defecto sin umbral, reescribe todos)
"""
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_geometry import PS2Model
from pose_matrix import build_world_mats, apply_mat

U32 = struct.Struct('>I')
F32 = struct.Struct('>f')


def u32(b, o): return U32.unpack_from(b, o)[0]
def f32(b, o): return F32.unpack_from(b, o)[0]
def pack_f32(v): return F32.pack(v)


def skin_ps2_by_bone(ps2):
    """Rig PS2: {bone: [(weight, coords_locales), ...]} (mismo que v5)."""
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
        print('Uso: mezclar_ps2_hd_v6.py <bin_hd> <bin_ps2> <output> [--keep-bone0]')
        return
    hd = open(sys.argv[1], 'rb').read()
    ps2 = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]
    keep_bone0 = '--keep-bone0' in sys.argv
    umbral = None
    if '--umbral' in sys.argv:
        umbral = float(sys.argv[sys.argv.index('--umbral') + 1])

    ps2_by_bone = skin_ps2_by_bone(ps2)
    total_ps2 = sum(len(v) for v in ps2_by_bone.values())
    print('PS2 skin: %d bones con %d coords locales' % (len(ps2_by_bone), total_ps2))

    # World mats PS2 (idénticas al HD 47/47)
    mats, _ = build_world_mats(ps2)
    # Pre-transformar coords PS2 a world: {bone: [world_xyz, local_xyz]}
    ps2_world = {}
    for bi, verts in ps2_by_bone.items():
        m = mats.get(bi)
        if not m:
            continue
        for w, c in verts:
            world = apply_mat(m[0], m[1], c)
            ps2_world.setdefault(bi, []).append((world, c))
    total_w = sum(len(v) for v in ps2_world.values())
    print('PS2 world: %d bones / %d coords' % (len(ps2_world), total_w))

    # Localizar sec34 HD
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
    keep = 0
    far = 0
    import math
    for i in range(n_sec):
        base = sec34_abs + 2 + i * 44
        hb = u32(data, base + 28)
        if keep_bone0 and hb == 0:
            keep += 1
            continue
        if hb not in ps2_world:
            no_bone += 1
            continue
        # coords locales HD del slot -> world HD (con la misma matriz)
        hz = f32(data, base + 12); hx = f32(data, base + 16); hy = f32(data, base + 20)
        m = mats.get(hb)
        if not m:
            no_bone += 1
            continue
        hworld = apply_mat(m[0], m[1], (hx, hy, hz))
        cand = ps2_world[hb]
        best = None
        best_d = 1e18
        for wxyz, lxyz in cand:
            d = (wxyz[0] - hworld[0]) ** 2 + (wxyz[1] - hworld[1]) ** 2 + (wxyz[2] - hworld[2]) ** 2
            if d < best_d:
                best_d = d
                best = lxyz
        if best is None:
            no_cov += 1
            continue
        if umbral is not None and math.sqrt(best_d) > umbral:
            far += 1
            continue
        data[base + 12:base + 16] = pack_f32(best[2])  # z local
        data[base + 16:base + 20] = pack_f32(best[0])  # x local
        data[base + 20:base + 24] = pack_f32(best[1])  # y local
        changed += 1

    print('Slots reescritos: %d | sin bone en PS2: %d | sin cobertura: %d | keep(bone0): %d | fuera umbral: %d' % (
        changed, no_bone, no_cov, keep, far))

    with open(out, 'wb') as f:
        f.write(bytes(data))
    print('Guardado: %s (%d bytes)' % (out, len(data)))


if __name__ == '__main__':
    main()