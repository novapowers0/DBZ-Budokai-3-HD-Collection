"""
AWG to OBJ v2 — exportar un modelo HD (AWO/AWG) a OBJ en WORLD SPACE.

Para retopología 3D: exporta los vertices del sec34 en world (aplicando la
matriz del hueso) para editar en Blender junto al modelo fuente (SDBH FBX).

Layout vertice HD (sec34, stride 44) — formato IDENTICO a B1 HD (v10+):
  +00 pos.x +04 pos.y +08 pos.z
  +12 weight +16 BONE(u32) +20 nrm.x +24 nrm.y +28 nrm.z
  +32 0xFFFFFFFF +36 blend/scale +40 uv

Offsets del header AWG0 — RELATIVOS al AWG0 (leccion 8 del proyecto B1):
  +0x28 sec_off  +0x2C sec_size (n_sec = sec_size//44)
  +0x30 post_off +0x34 post_size (n_ib = post_size//2)

El world = mat_world[bone] * (x_local, y_local, z_local).
Se usa la matriz world del PS2 de Krillin (b327_ps2), que comparte el
esqueleto KLL con el HD (validado: coords locales HD + mat_KLL PS2 = world
coherente). Esto evita la complejidad de la jerarquia de ejes HD.

Uso:
  python awg_to_obj.py <bin_amb_hd> <b327_ps2.bin> <salida.obj>
"""

import io, sys, struct, math
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
sys.path.insert(0, r'C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\awo_tools')
from pose_matrix import build_world_mats

def u32be(b, o): return struct.unpack('>I', b[o:o+4])[0]
def f32be(b, o): return struct.unpack('>f', b[o:o+4])[0]


def apply_mat(M, p, v):
    return (M[0][0]*v[0]+M[0][1]*v[1]+M[0][2]*v[2]+p[0],
            M[1][0]*v[0]+M[1][1]*v[1]+M[1][2]*v[2]+p[1],
            M[2][0]*v[0]+M[2][1]*v[1]+M[2][2]*v[2]+p[2])


def main():
    if len(sys.argv) < 4:
        print('Uso: awg_to_obj.py <bin_amb_hd> <b327_ps2.bin> <salida.obj>')
        return
    hd = open(sys.argv[1], 'rb').read()
    ps2 = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    mats_kll, _ = build_world_mats(ps2, 0x40)

    # Acepta #AMB (AWO en +0x40) o #AWO directo.
    awo = hd.find(b'#AWO')
    if awo < 0 or awo == 0 and hd[:4] != b'#AWO':
        print('ERROR: no se encontro #AWO en %s' % sys.argv[1])
        return
    if hd[:4] == b'#AMB':
        awo = 0x40
    elif hd[:4] != b'#AWO':
        awo = hd.find(b'#AWO')
    if awo < 0:
        print('ERROR: no se encontro #AWO en %s' % sys.argv[1])
        return
    tbl = u32be(hd, awo+0x1C)
    off0 = u32be(hd, awo+tbl)
    awg0 = awo+off0

    sec_off = u32be(hd, awg0+0x28)
    sec_size = u32be(hd, awg0+0x2C)
    post_off = u32be(hd, awg0+0x30)
    post_size = u32be(hd, awg0+0x34)
    n_sec = sec_size//44
    n_ib = post_size//2
    print('sec34=%d IB=%d' % (n_sec, n_ib))

    sec = awg0+sec_off
    obj_v = []
    obj_vt = []
    obj_vn = []
    remap_v = {}
    for i in range(n_sec):
        off = sec+i*44
        x = f32be(hd, off+0); y = f32be(hd, off+4); z = f32be(hd, off+8)
        weight = f32be(hd, off+12)
        bone = u32be(hd, off+16)
        nx = f32be(hd, off+20); ny = f32be(hd, off+24); nz = f32be(hd, off+28)
        u = f32be(hd, off+40); v = f32be(hd, off+44)
        M, p = mats_kll.get(bone, ([[1,0,0],[0,1,0],[0,0,1]], (0,0,0)))
        wx, wy, wz = apply_mat(M, p, (x, y, z))
        obj_v.append((wx, wy, wz))
        obj_vn.append((nx, ny, nz))
        obj_vt.append((u, v))
        remap_v[i] = len(obj_v)-1

    ib_abs = awg0+post_off
    faces = []
    for k in range(0, n_ib-2, 3):
        a = struct.unpack('>H', hd[ib_abs+k*2:ib_abs+k*2+2])[0]
        b = struct.unpack('>H', hd[ib_abs+(k+1)*2:ib_abs+(k+1)*2+2])[0]
        c = struct.unpack('>H', hd[ib_abs+(k+2)*2:ib_abs+(k+2)*2+2])[0]
        if a == 0xFFFF or b == 0xFFFF or c == 0xFFFF:
            continue
        if a < n_sec and b < n_sec and c < n_sec:
            faces.append((remap_v[a]+1, remap_v[b]+1, remap_v[c]+1))

    with open(out, 'w') as f:
        f.write('# AWG to OBJ export\n')
        for x, y, z in obj_v:
            f.write('v %f %f %f\n' % (x, y, z))
        for u, v in obj_vt:
            f.write('vt %f %f\n' % (u, v))
        for x, y, z in obj_vn:
            f.write('vn %f %f %f\n' % (x, y, z))
        for a, b, c in faces:
            f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (
                a, a, a, b, b, b, c, c, c))
    print('OBJ: %d verts, %d vt, %d vn, %d faces -> %s' % (
        len(obj_v), len(obj_vt), len(obj_vn), len(faces), out))


if __name__ == '__main__':
    main()
