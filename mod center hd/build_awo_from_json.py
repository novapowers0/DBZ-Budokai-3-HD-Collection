"""
Build AWO HD desde JSON v2 (SDBH WM / cualquier modelo FBX).

Corregido el mapeo cluster->mesh: cada cluster se conecta a un Skin que
pertenece a un mesh. Los indices del cluster son LOCALES al mesh.

Pipeline:
  1. Mapear cluster -> Skin -> mesh (via connections).
  2. Para cada mesh, cada vertice local -> (bone, weight) del cluster.
  3. Transformar world (del FBX) -> local del hueso KLL destino.
  4. Construir verts HD + IB.
  5. Empaquetar AWO HD con Krillin como plantilla (fase 2).

Uso:
  python build_awo_from_json.py <model_v2.json> <b327_ps2> <e326> <out_prefix>
"""

import io, sys, json, struct, math
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


def build_vertex_hd(pos_local, bone_idx, weight, nrm, uv):
    x, y, z = pos_local
    nx, ny, nz = nrm
    u, v = uv
    return (f32(float('nan')) + f32(u) + f32(v) +
            f32(z) + f32(x) + f32(y) +
            f32(float(weight)) + struct.pack('>I', bone_idx & 0xFFFFFFFF) +
            f32(nz) + f32(-ny) + f32(nx))


def build_vertex_vb2(pos_abs, nrm, uv):
    x, y, z = pos_abs
    nx, ny, nz = nrm
    return (f32(x) + f32(y) + f32(z) +
            f32(0.0) + f32(0.0) + f32(0.0) +
            f32(0.0) + struct.pack('>I', 0xFFFFFFFF) +
            f32(nx) + f32(ny) + f32(nz))


SDBH_TO_KLL = {
    'waist': 1, 'llegrot': 38, 'lleg1': 39, 'lleg2': 40, 'lfoot1': 41,
    'lfoot2': 42, 'nlf': 43, 'rlegrot': 44, 'rleg1': 45, 'rleg2': 46,
    'rfoot1': 47, 'rfoot2': 48, 'nrf': 49, 'stmc': 2, 'chest': 12,
    'lchn': 13, 'larmrot': 14, 'larm1': 15, 'larm2': 16, 'lhandrot': 17,
    'lhand': 18, 'nla': 19, 'rchn': 20, 'rarmrot': 21, 'rarm1': 22,
    'rarm2': 23, 'rhandrot': 24, 'rhand': 25, 'nra': 26, 'neck': 27,
    'head': 28, 'body': 0,
}


def cluster_to_bone(name):
    n = name.lower()
    for k, v in SDBH_TO_KLL.items():
        if k in n:
            return v
    if n.startswith('l') and any(f in n for f in ('finger','thumb','index','ring','pinky','middle')):
        return 18
    if n.startswith('r') and any(f in n for f in ('finger','thumb','index','ring','pinky','middle')):
        return 25
    if any(f in n for f in ('face','mouth','jaw','teeth')):
        return 36
    return -1


def main():
    if len(sys.argv) < 5:
        print('Uso: build_awo_from_json.py <model_v2.json> <b327_ps2> <e326> <out_prefix>')
        return
    data = json.load(open(sys.argv[1]))
    b3_ps2 = open(sys.argv[2], 'rb').read()
    hd = open(sys.argv[3], 'rb').read()
    out = sys.argv[4]

    mats_kll, _ = build_world_mats(b3_ps2, 0x40)

    # --- mapear cluster -> mesh via Skin connections ---
    # Skin ids por mesh (de la conexion Skin->mesh vista antes)
    skin_to_mesh = {}
    mesh_ids = {m['id']: m for m in data['meshes']}
    # las conexiones que van Skin->mesh ya las vimos; reconstruir con objects
    # Conexion: (tipo, child, parent). Skin(parent=mesh) se ve como OO skin_id, mesh_id
    skin_ids = set()
    for c in data['clusters']:
        pass
    # Del analisis previo: Skin_ARM=27913344->32738328 etc. Reconstruir:
    # en connections, parent es mesh cuando child es un Skin (Deformer::Skin_*)
    # Pero el JSON no tiene los Skin como objetos. Los cluster->Skin se ven en
    # connections (child=cluster, parent=Skin_id). Y Skin->mesh tambien.
    # Necesito identificar Skin ids: son parents de clusters que no son meshes.
    cluster_ids = {c['id'] for c in data['clusters']}
    # skin ids = parents de clusters
    skin_ids = set()
    for t, child, parent in data['connections']:
        if t == 'OO' and child in cluster_ids:
            skin_ids.add(parent)
    # skin -> mesh: conexiones donde child=skin, parent=mesh
    skin_to_mesh = {}
    for t, child, parent in data['connections']:
        if t == 'OO' and child in skin_ids and parent in mesh_ids:
            skin_to_mesh[child] = mesh_ids[parent]
    print('Skins mapeados a meshes:', len(skin_to_mesh))

    # cluster -> (mesh, bone)
    cluster_info = {}
    for c in data['clusters']:
        # find its skin (parent)
        skin = None
        for t, child, parent in data['connections']:
            if t == 'OO' and child == c['id']:
                skin = parent
                break
        mesh = skin_to_mesh.get(skin)
        bone = cluster_to_bone(c['name'])
        if mesh is not None and bone >= 0:
            cluster_info[c['id']] = (mesh, bone, c)

    # --- construir verts HD por mesh ---
    # SPLIT sec34/vb2 como el HD de Krillin:
    #   sec34 = bones 0-35 (skinned, coords locales)
    #   vb2   = bones >35 + sin skin (estatico, posiciones absolutas, bone=FFFF)
    # El sec34 nativo SOLO skinnea bones 0-35 (piernas 38-49 NO caben en sec34).
    hd_sec34 = []   # bytes layout HD skinned
    hd_vb2 = []     # bytes layout HD estatico
    hd_tris = []    # (a,b,c) indices globales HD
    n_skinned = 0
    n_static = 0
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
        # remapear verts del mesh a sec34/vb2 globales
        # sec34 indices: 0..len(hd_sec34)-1 (global)
        # vb2 indices: n_sec_acum..  (global, despues de todo el sec34)
        # Para simplificar: construimos remap con sec34 globales y vb2 marcados
        # con offset +100000 (se corrigen al final al conocer n_sec total).
        VB_OFF = 1000000
        remap = {}
        for vi in range(nv):
            world = mesh['verts'][vi]
            nrm = mesh['nrm'][vi] if vi < len(mesh['nrm']) else (0,0,0)
            uv = mesh['uv'][vi] if vi < len(mesh['uv']) else (0,0)
            sk = vert_skin.get(vi)
            bone = -1
            w = 1.0
            if sk:
                bone, w = max(sk, key=lambda x: x[1])
            if 0 <= bone <= 35 and bone in mats_kll:
                M3, p3 = mats_kll[bone]
                iM, ip = inv_rigid(M3, p3)
                local = apply_mat(iM, ip, world)
                remap[vi] = len(hd_sec34)
                hd_sec34.append(build_vertex_hd(local, bone, w, nrm, uv))
                n_skinned += 1
            else:
                remap[vi] = VB_OFF + len(hd_vb2)
                hd_vb2.append(build_vertex_vb2(world, nrm, uv))
                n_static += 1
        for t in mesh['tris']:
            if t[0] < nv and t[1] < nv and t[2] < nv:
                hd_tris.append((remap[t[0]], remap[t[1]], remap[t[2]]))

    print('Verts HD: sec34=%d (skinned) | vb2=%d (estatico) | Tris: %d' % (
        len(hd_sec34), len(hd_vb2), len(hd_tris)))

    # El IB del HD indexa sec34 y vb2 juntos: sec34 = 0..n_sec34-1,
    # vb2 = n_sec34..n_sec34+n_vb2-1. Corregir los indices VB_OFF.
    n_sec = len(hd_sec34)
    hd_tris_final = [tuple(v - VB_OFF + n_sec if v >= VB_OFF else v for v in t) for t in hd_tris]
    # OJO: los remap del vb2 empezaban en 0; los del sec34 tambien en 0.
    # Necesitamos distinguir: remap para sec34 = 0..n_sec-1, para vb2 = n_sec..n_sec+n_vb2-1.
    # El codigo anterior puso remap[vi]=len(hd_sec34) para sec34 y len(hd_vb2) para vb2,
    # ambos desde 0. Corregimos: en el bucle, al construir, ya era asi.
    # Reconstruir: el remap se hizo con len(hd_sec34) para sec34 (correcto, base global
    # de sec34) y len(hd_vb2) para vb2 (pero vb2 empieza en 0, no en n_sec).
    # Como no guardamos el remap, no podemos corregir aqui. Lo hacemos en el bucle.

    # guardar sec34/vb2/ib para la fase de empaquetado
    sec34 = b''.join(hd_sec34)
    vb2 = b''.join(hd_vb2)
    ib = b''.join(struct.pack('>HHH', t[0], t[1], t[2]) for t in hd_tris_final)
    with open(out + '.sec34', 'wb') as f:
        f.write(sec34)
    with open(out + '.vb2', 'wb') as f:
        f.write(vb2)
    with open(out + '.ib', 'wb') as f:
        f.write(ib)
    print('Guardado: %s.{sec34,vb2,ib} (%d, %d, %d bytes)' % (
        out, len(sec34), len(vb2), len(ib)))


if __name__ == '__main__':
    main()
