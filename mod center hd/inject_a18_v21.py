"""
Inyector v21: Android 18 (SDBH WM) en el 100% de los slots del sec34 de Krillin.

Tecnica del B1 seccion 12 (Goku B1 PS2 -> B1 HD, que funciono):
  - Mantener el bin e326 COMPLETO (conteos fijos sec34=1956, IB=5140, AWG0).
  - Rellenar TODOS los slots del sec34 con la geometria del personaje nuevo,
    por correspondencia bone/world, manteniendo el bone index de cada slot.
  - El IB del anfitrion dibuja la geometria nueva en los slots -> se ve el
    cuerpo del personaje (deformado por la topologia del anfitrion, pero el
    personaje esta presente).

Mejora vs v3: rellenar el 100% de los slots (no solo los bones que mapean).
Para los slots cuyo bone no tiene datos directos, usar el vertice de Android 18
mas cercano en WORLD (grid espacial), transformado al local de ese bone.

Uso:
  python inject_a18_v21.py <model_v2.json> <b327_ps2> <e326> <out>
"""

import os
import io, sys, json, struct, math
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..\..', 'awo_tools')))
from pose_matrix import build_world_mats, apply_mat

F32 = struct.Struct('>f')
U32 = struct.Struct('>I')
def f32(v): return F32.pack(v)
def u32r(b, o): return U32.unpack_from(b, o)[0]
def f32r(b, o): return F32.unpack_from(b, o)[0]


def inv_rigid(M, p):
    Rt = [[M[j][i] for j in range(3)] for i in range(3)]
    tp = [-sum(Rt[i][j]*p[j] for j in range(3)) for i in range(3)]
    return Rt, tp


SDBH_TO_KLL = {
    'waist': 1, 'stmc': 2, 'chest': 12, 'lchn': 13, 'larmrot': 14, 'larm1': 15,
    'larm2': 16, 'lhandrot': 17, 'lhand': 18, 'nla': 19, 'rchn': 20,
    'rarmrot': 21, 'rarm1': 22, 'rarm2': 23, 'rhandrot': 24, 'rhand': 25,
    'nra': 26, 'neck': 27, 'head': 28, 'body': 0,
    'llegrot': 38, 'lleg1': 39, 'lleg2': 40, 'lfoot1': 41, 'lfoot2': 42,
    'rlegrot': 44, 'rleg1': 45, 'rleg2': 46, 'rfoot1': 47, 'rfoot2': 48,
}


def cluster_to_bone(name):
    n = name.lower()
    for k, v in SDBH_TO_KLL.items():
        if k in n:
            return v
    return -1


def main():
    if len(sys.argv) < 5:
        print('Uso: inject_a18_v21.py <model_v2.json> <b327_ps2> <e326> <out>')
        return
    data = json.load(open(sys.argv[1]))
    b3_ps2 = open(sys.argv[2], 'rb').read()
    hd = open(sys.argv[3], 'rb').read()
    out = sys.argv[4]

    mats_kll, _ = build_world_mats(b3_ps2, 0x40)

    # --- Extraer verts de Android 18 por bone (world) ---
    mesh_ids = {m['id']: m for m in data['meshes']}
    cluster_ids = {c['id'] for c in data['clusters']}
    skin_ids = set()
    for t, child, parent in data['connections']:
        if t == 'OO' and child in cluster_ids:
            skin_ids.add(parent)
    skin_to_mesh = {}
    for t, child, parent in data['connections']:
        if t == 'OO' and child in skin_ids and parent in mesh_ids:
            skin_to_mesh[child] = mesh_ids[parent]
    cluster_info = {}
    for c in data['clusters']:
        skin = None
        for t, child, parent in data['connections']:
            if t == 'OO' and child == c['id']:
                skin = parent
                break
        mesh = skin_to_mesh.get(skin)
        bone = cluster_to_bone(c['name'])
        if mesh is not None and bone >= 0:
            cluster_info[c['id']] = (mesh, bone, c)

    a18_by_bone = {}
    a18_all_world = []
    for mesh in data['meshes']:
        nv = len(mesh['verts'])
        vert_skin = {}
        for cid, (m, bone, c) in cluster_info.items():
            if m['id'] != mesh['id']:
                continue
            for k, vi in enumerate(c.get('indexes', [])):
                if vi < nv:
                    w = c['weights'][k] if k < len(c.get('weights', [])) else 1.0
                    vert_skin.setdefault(vi, []).append((bone, w))
        for vi in range(nv):
            world = mesh['verts'][vi]
            sk = vert_skin.get(vi)
            bone = -1
            if sk:
                bone, w = max(sk, key=lambda x: x[1])
            a18_all_world.append((bone, world))
            if 0 <= bone <= 35:
                a18_by_bone.setdefault(bone, []).append(world)
    print('Android 18: %d verts totales, %d con bone 0-35' % (
        len(a18_all_world), sum(len(v) for v in a18_by_bone.values())))

    # --- Escala: world A18 -> world Krillin ---
    # Usamos la altura del cuerpo como referencia.
    ys = [w[1] for _, w in a18_all_world]
    h_a18 = max(ys) - min(ys)
    # altura Krillin B3 PS2: y[-5.1..7.5]=12.65
    scale = 12.65 / h_a18 if h_a18 else 1
    print('Escala: %.4f (altura A18 %.2f)' % (scale, h_a18))
    a18_by_bone = {k: [(v[0]*scale, v[1]*scale, v[2]*scale) for v in vals]
                   for k, vals in a18_by_bone.items()}
    a18_all_world = [(b, (w[0]*scale, w[1]*scale, w[2]*scale)) for b, w in a18_all_world]

    # --- Grid para busqueda por world ---
    cell = 0.4
    grid = {}
    for i, (b, w) in enumerate(a18_all_world):
        key = (int(w[0]/cell), int(w[1]/cell), int(w[2]/cell))
        grid.setdefault(key, []).append(i)
    def nearest(wx, wy, wz):
        kx, ky, kz = int(wx/cell), int(wy/cell), int(wz/cell)
        best, best_d = -1, 1e18
        for dx in range(-2, 3):
            for dy in range(-2, 3):
                for dz in range(-2, 3):
                    for i in grid.get((kx+dx, ky+dy, kz+dz), []):
                        x, y, z = a18_all_world[i][1]
                        d = (x-wx)**2+(y-wy)**2+(z-wz)**2
                        if d < best_d:
                            best_d, best = d, i
        return best if best >= 0 else 0

    # --- Rellenar TODOS los slots del sec34 ---
    awo = bytearray(hd[0x40:])
    AWG = u32r(awo, u32r(awo, 0x1C))
    sc3 = u32r(awo, AWG+0x34); vb3 = u32r(awo, AWG+0x2C)
    n_sec = (vb3-(sc3+2))//44
    print('sec34 slots: %d' % n_sec)

    changed = 0
    for i in range(n_sec):
        off = AWG+sc3+2+i*44
        bone = u32r(awo, off+28)
        # world del slot: mat_KLL[bone] * local_actual
        lz = f32r(awo, off+12); lx = f32r(awo, off+16); ly = f32r(awo, off+20)
        M3, p3 = mats_kll.get(bone, ([[1,0,0],[0,1,0],[0,0,1]], [0,0,0]))
        ws = apply_mat(M3, p3, (lx, ly, lz))
        # vertice A18 mas cercano por world
        bi = nearest(ws[0], ws[1], ws[2])
        wx, wy, wz = a18_all_world[bi][1]
        # transformar world A18 -> local del bone del slot
        iM, ip = inv_rigid(M3, p3)
        ll = apply_mat(iM, ip, (wx, wy, wz))
        awo[off+12:off+16] = f32(ll[2])
        awo[off+16:off+20] = f32(ll[0])
        awo[off+20:off+24] = f32(ll[1])
        changed += 1
    print('Slots reescritos con Android 18: %d/%d' % (changed, n_sec))

    amb = bytearray(hd[:0x40])
    amb += awo
    with open(out, 'wb') as f:
        f.write(bytes(amb))
    print('Guardado: %s (%d bytes)' % (out, len(amb)))


if __name__ == '__main__':
    main()
