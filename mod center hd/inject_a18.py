"""
Inyectar Android 18 (de JSON) en los slots del sec34 de Krillin e326.

Tecnica VALIDADA (v15, estable): mantener el bin e326 COMPLETO intacto
(IB/arms/vb2/AZT/offsets) y solo reescribir las posiciones locales de los
slots del sec34. Sin reconstruccion de IB (causa cuelgue en B3).

Para cada slot del sec34 B3 (con bone B 0-35), buscar el vertice de Android 18
con bone mapeado a B mas cercano (por coords world) y reemplazar la pos local.

Resultado: silueta de Android 18 con la topologia de Krillin (deformada pero
sin crash), validando el pipeline EMD->FBX->JSON->AWO->mod.

Uso:
  python inject_a18.py <model_v2.json> <b327_ps2> <e326> <out>
"""

import io, sys, json, struct
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
sys.path.insert(0, r'C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\awo_tools')
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
}


def cluster_to_bone(name):
    n = name.lower()
    for k, v in SDBH_TO_KLL.items():
        if k in n:
            return v
    return -1


def main():
    if len(sys.argv) < 5:
        print('Uso: inject_a18.py <model_v2.json> <b327_ps2> <e326> <out>')
        return
    data = json.load(open(sys.argv[1]))
    b3_ps2 = open(sys.argv[2], 'rb').read()
    hd = open(sys.argv[3], 'rb').read()
    out = sys.argv[4]

    mats_kll, _ = build_world_mats(b3_ps2, 0x40)

    # --- Extraer verts de Android 18 por bone (world) ---
    # cada vertice: (bone_kll, world). Solo bones 0-35 (sec34).
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
            sk = vert_skin.get(vi)
            if not sk:
                continue
            bone, w = max(sk, key=lambda x: x[1])
            if 0 <= bone <= 35:
                a18_by_bone.setdefault(bone, []).append(mesh['verts'][vi])
    print('Android 18 verts por bone (0-35):', sorted(a18_by_bone.keys()))

    # Escala: world A18 vs world Krillin (mediana de mags)
    import math
    all_w = []
    for bone, verts in a18_by_bone.items():
        for v in verts:
            all_w.append(math.sqrt(v[0]**2+v[1]**2+v[2]**2))
    all_w.sort()
    med_a18 = all_w[len(all_w)//2] if all_w else 1
    # mags Krillin sec34 (coords locales)
    awo_hd = hd[0x40:]
    AWG = u32r(awo_hd, u32r(awo_hd, 0x1C))
    sc3 = u32r(awo_hd, AWG+0x34); vb3 = u32r(awo_hd, AWG+0x2C)
    n3 = (vb3-(sc3+2))//44
    m3v = []
    for i in range(n3):
        off = AWG+sc3+2+i*44
        if u32r(awo_hd, off) != 0xFFFFFFFF: continue
        z = f32r(awo_hd, off+12); x = f32r(awo_hd, off+16); y = f32r(awo_hd, off+20)
        m3v.append(math.sqrt(x*x+y*y+z*z))
    m3v.sort()
    med_kr = m3v[len(m3v)//2] if m3v else 1
    scale = med_kr / med_a18 if med_a18 else 1
    print('Escala A18->Krillin: %.4f (med A18=%.2f, med KLL=%.2f)' % (scale, med_a18, med_kr))
    for bone in a18_by_bone:
        a18_by_bone[bone] = [(v[0]*scale, v[1]*scale, v[2]*scale) for v in a18_by_bone[bone]]

    # Inyectar en slots del sec34 (tecnica v15: por bone, vertice mas cercano)
    awo = bytearray(hd[0x40:])
    n_sec = (vb3-(sc3+2))//44
    changed = 0
    no_data = 0
    for i in range(n_sec):
        off = AWG+sc3+2+i*44
        bone = u32r(awo, off+28)
        cand = a18_by_bone.get(bone)
        if not cand:
            no_data += 1
            continue
        hz, hx, hy = f32r(awo, off+12), f32r(awo, off+16), f32r(awo, off+20)
        best, best_d = None, 1e18
        for (cx, cy, cz) in cand:
            d = (cx-hx)**2+(cy-hy)**2+(cz-hz)**2
            if d < best_d:
                best_d, best = d, (cx, cy, cz)
        if best is None:
            continue
        awo[off+12:off+16] = f32(best[2])
        awo[off+16:off+20] = f32(best[0])
        awo[off+20:off+24] = f32(best[1])
        changed += 1
    print('Slots reescritos con Android 18: %d | sin datos: %d' % (changed, no_data))

    amb = bytearray(hd[:0x40])
    amb += awo
    with open(out, 'wb') as f:
        f.write(bytes(amb))
    print('Guardado: %s (%d bytes)' % (out, len(amb)))


if __name__ == '__main__':
    main()
