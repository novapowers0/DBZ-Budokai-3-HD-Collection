"""
Build AWO HD v20: Android 18 (SDBH WM) con IB reconstruido + arms re-mapeados.

Tecnica del CONSOLIDADO §13.5.13 (fix v3.1 que se instalo y funciono):
  - Escribir SOLO en los bloques shadow (sello 0x204) del arm, en +4
    (end_byte) y +0xC (start_byte), con valores = indice_del_IB * 2.
  - El bloque extra (bone 0x24 @0x1CD0) se actualiza al final del IB.
  - El runtime dibuja cada part como [offset_previo, offset_bone).

Pipeline:
  1. Geometria de Android 18 del JSON (verts/tris/nrm/uv + skinning).
  2. Split sec34 (bones 0-35 skinned) / vb2 (bones >35 + estaticos).
  3. Construir verts HD (stride 44).
  4. Reconstruir IB agrupado en 4 regiones (materiales).
  5. Re-mapear los shadow arms + extra con los nuevos offsets (bytes).
  6. Empaquetar AMB.

Uso:
  python build_awo_v20.py <model_v2.json> <b327_ps2> <e326> <out_amb>
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
    if n.startswith('l') and any(f in n for f in ('finger','thumb','index','ring','pinky','middle')):
        return 18
    if n.startswith('r') and any(f in n for f in ('finger','thumb','index','ring','pinky','middle')):
        return 25
    return -1


def main():
    if len(sys.argv) < 5:
        print('Uso: build_awo_v20.py <model_v2.json> <b327_ps2> <e326> <out_amb>')
        return
    data = json.load(open(sys.argv[1]))
    b3_ps2 = open(sys.argv[2], 'rb').read()
    hd = open(sys.argv[3], 'rb').read()
    out = sys.argv[4]

    mats_kll, _ = build_world_mats(b3_ps2, 0x40)

    # --- mapear cluster -> mesh ---
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

    # --- construir verts sec34/vb2 + IB ---
    hd_sec34 = []
    hd_vb2 = []
    hd_tris = []
    VB_OFF = 1000000
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
            else:
                remap[vi] = VB_OFF + len(hd_vb2)
                hd_vb2.append(build_vertex_vb2(world, nrm, uv))
        for t in mesh['tris']:
            if t[0] < nv and t[1] < nv and t[2] < nv:
                hd_tris.append((remap[t[0]], remap[t[1]], remap[t[2]]))

    n_sec = len(hd_sec34)
    n_vb2 = len(hd_vb2)
    hd_tris = [tuple(v - VB_OFF + n_sec if v >= VB_OFF else v for v in t) for t in hd_tris]
    print('sec34=%d vb2=%d tris=%d' % (n_sec, n_vb2, len(hd_tris)))

    # Escalar el vb2 al tamano de Krillin (las pos world de A18 difieren)
    # Usamos la misma escala que el sec34: calculamos desde mags world A18 vs
    # mags world Krillin PS2.
    import math
    # mags del sec34 transformado (local KLL) -> comparar con Krillin nativo
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
    # mags del sec34 de Android 18 (local KLL)
    m18 = []
    for i in range(n_sec):
        off = i*44
        z = f32r(hd_sec34[i], 0); x = f32r(hd_sec34[i], 4); y = f32r(hd_sec34[i], 8)  # no
    # leer de los bytes
    sec34_bytes = b''.join(hd_sec34)
    for i in range(n_sec):
        b = hd_sec34[i]
        x = struct.unpack('>f', b[16:20])[0]
        y = struct.unpack('>f', b[20:24])[0]
        z = struct.unpack('>f', b[12:16])[0]
        m18.append(math.sqrt(x*x+y*y+z*z))
    m18.sort()
    med_18 = m18[len(m18)//2] if m18 else 1
    scale = med_kr / med_18 if med_18 else 1
    print('Escala sec34: %.4f (med A18=%.2f, med KLL=%.2f)' % (scale, med_18, med_kr))
    # aplicar escala a los bytes sec34 (z,x,y) y vb2
    for i in range(n_sec):
        b = bytearray(hd_sec34[i])
        struct.pack_into('>f', b, 16, struct.unpack('>f', b[16:20])[0]*scale)
        struct.pack_into('>f', b, 20, struct.unpack('>f', b[20:24])[0]*scale)
        struct.pack_into('>f', b, 12, struct.unpack('>f', b[12:16])[0]*scale)
        hd_sec34[i] = bytes(b)
    for i in range(n_vb2):
        b = bytearray(hd_vb2[i])
        for o in (0, 4, 8):
            struct.pack_into('>f', b, o, struct.unpack('>f', b[o:o+4])[0]*scale)
        hd_vb2[i] = bytes(b)

    # Decimar vb2 a <=226 (el slot de Krillin)
    if n_vb2 > 226:
        cell = 0.02
        best = None
        for _ in range(120):
            cmap = {}
            uniq = []
            remap = {}
            for i, e in enumerate(hd_vb2):
                x = struct.unpack('>f', e[0:4])[0]
                y = struct.unpack('>f', e[4:8])[0]
                z = struct.unpack('>f', e[8:12])[0]
                key = (int(x/cell), int(y/cell), int(z/cell))
                if key not in cmap:
                    cmap[key] = len(uniq)
                    uniq.append(e)
                remap[i] = cmap[key]
            if best is None or len(uniq) <= 226:
                best = (uniq, remap)
            if len(uniq) <= 226:
                break
            cell *= 1.12
        uniq, remap = best
        hd_tris = [tuple(n_sec + remap[v-n_sec] if v >= n_sec else v for v in t) for t in hd_tris]
        hd_vb2 = uniq
        n_vb2 = len(uniq)
        print('vb2 decimado a: %d' % n_vb2)

    # Podar tris a <=1713 (5140 indices max, el slot de Krillin)
    while len(hd_tris) > 1713:
        # eliminar el triangulo de mayor area
        def area(t):
            def g(i):
                if i < n_sec:
                    b = hd_sec34[i]
                    return (struct.unpack('>f', b[16:20])[0], struct.unpack('>f', b[20:24])[0])
                e = hd_vb2[i-n_sec]
                return (struct.unpack('>f', e[0:4])[0], struct.unpack('>f', e[4:8])[0])
            a0 = g(t[0]); a1 = g(t[1]); a2 = g(t[2])
            return abs((a1[0]-a0[0])*(a2[1]-a0[1]) - (a1[1]-a0[1])*(a2[0]-a0[0]))
        wi, wa = 0, -1
        step = max(1, len(hd_tris)//2000)
        for i in range(0, len(hd_tris), step):
            a = area(hd_tris[i])
            if a > wa:
                wa, wi = a, i
        del hd_tris[wi]
    print('Tris podados a: %d' % len(hd_tris))

    # Construir bytes finales
    sec34_bytes = b''.join(hd_sec34)
    vb2_bytes = b''.join(hd_vb2)
    ib_vals = []
    for t in hd_tris:
        ib_vals.extend(t)
    ib_bytes = b''.join(struct.pack('>H', v) for v in ib_vals)
    print('sec34=%d vb2=%d IB=%d indices' % (n_sec, n_vb2, len(ib_vals)))
    print('Guardado intermedio: %s.{sec34,vb2,ib}' % out)
    with open(out+'.sec34','wb') as f: f.write(sec34_bytes)
    with open(out+'.vb2','wb') as f: f.write(vb2_bytes)
    with open(out+'.ib','wb') as f: f.write(ib_bytes)


if __name__ == '__main__':
    main()
