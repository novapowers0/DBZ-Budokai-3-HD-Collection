# -*- coding: utf-8 -*-
"""
build_awo_desde_cero.py — Construir el AWO HD de un personaje desde cero.

Patron validado por el Piccolo B1 que FUNCIONO (PLAN_AFS_OUT_RE_COMPARATIVA.md
seccion 7): 1 AWG por AMG del PS2, con los MISMO labels y conteos propios.
Cada AWG lleva su sec34 (vertice stride 44), vb2, IB, riggingData y zonas 80B.

Pipeline:
  1. Parsear Janemba.amb (PS2, #AMO0 LE, 17 AMGs) -> verts+IB por AMG
  2. Re-mapear huesos JNB -> KLL (por label, rig_mapeo)
  3. Transformar coords locales JNB -> local KLL (world mats)
  4. Construir el AWO HD BE: header + tabla AWG + N AWGs
  5. Empaquetar como #AMB (con #AZT de Krillin como relleno si hace falta)
  6. Instalar como mod via override por entrada (mods/<mod>/us/data_cmn.afs/327/)

Layout vertice HD (B3, stride 44, de AGENTS item 44):
  +00 nan | +04 u | +08 v | +12 z_local | +16 x_local | +20 y_local
  +24 peso | +28 BONE(u32) | +32 nz | +36 -ny | +40 nx

Uso:
  python build_awo_desde_cero.py <janemba.amb> <krillin_hd.bin> <out_amb>
"""

import sys, io, struct, re

# ---------------------------------------------------------------- helpers
def r8(b, o): return b[o]
def r16(b, o): return struct.unpack_from('<H', b, o)[0]
def r32(b, o): return struct.unpack_from('<I', b, o)[0]
def rf(b, o): return struct.unpack_from('<f', b, o)[0]
def u32be(b, o): return struct.unpack('>I', b[o:o+4])[0]

VERT_STRIDE = {0xBD:48,0xFD:48,0x3D:48,0xB5:48,0xB6:48,0xF5:48,
               0x199:32,0xB4:32,0xA4:32,0x99:32,0x92:32,0x19:32,0x90:16}

def read_vert(b, off, vtype):
    if vtype in (0xBD,0xFD,0x3D,0xB5,0xB6,0xF5):
        vx,vy,vz = rf(b,off),rf(b,off+4),rf(b,off+8)
        nx,ny,nz = rf(b,off+16),rf(b,off+20),rf(b,off+24)
        tu,tv = rf(b,off+32),rf(b,off+36)
        return (vx,vy,vz),(nx,ny,nz),(tu,tv)
    if vtype == 0x199:
        vx,vy,vz = rf(b,off),rf(b,off+4),rf(b,off+8)
        nx,ny,nz = rf(b,off+16),rf(b,off+20),rf(b,off+24)
        return (vx,vy,vz),(nx,ny,nz),(0,0)
    if vtype in (0xB4,0xA4,0x99,0x92,0x19):
        vx,vy,vz = rf(b,off),rf(b,off+4),rf(b,off+8)
        tu,tv = rf(b,off+16),rf(b,off+20)
        return (vx,vy,vz),(0,0,0),(tu,tv)
    if vtype == 0x90:
        vx,vy,vz = rf(b,off),rf(b,off+4),rf(b,off+8)
        return (vx,vy,vz),(0,0,0),(0,0)
    return (0,0,0),(0,0,0),(0,0)

def read_faces(vertcount, facetype):
    faces = []
    if facetype == 1:
        f1,f2 = 0,1; direction = -1
        for x in range(2, vertcount):
            f3 = x; direction *= -1
            if f1 != f2 and f2 != f3 and f3 != f1:
                if direction > 0: faces.append((f1,f2,f3))
                else: faces.append((f1,f3,f2))
            f1,f2 = f2,f3
    else:
        for x in range(1, vertcount+1, 3):
            if x+2 <= vertcount: faces.append((x-1,x,x+1))
    return faces

# ------------------------------------------------------- parseo PS2 AMG
def parse_ps2_amg(b, amg_abs):
    """Parsea un AMG PS2 completo. Devuelve (verts, tris) con verts como
    dicts {pos, nrm, uv} y tris como indices. La geometria se parsea por
    submeshes encadenados (como parse_ps2_mesh.py)."""
    bone_am = r32(b, amg_abs+0x10)
    axes_loc = r32(b, amg_abs+0x14)
    all_verts = []
    all_tris = []
    for bi in range(bone_am):
        e0 = amg_abs + axes_loc + bi*80
        p34 = r32(b, e0+0x34)
        if not p34: continue
        arm = amg_abs + p34
        mesh_hdr = r32(b, arm+4)
        if not mesh_hdr: continue
        mg = amg_abs + mesh_hdr
        mp_amnt = r32(b, mg)
        if not mp_amnt or mp_amnt > 64: continue
        part_offs = [r32(b, mg+16+i*4) for i in range(mp_amnt)]
        for rel in part_offs:
            po = mg + rel
            size_field = r32(b, po+0x90)
            mesh_size = (size_field - 0x60000000)*16 if size_field >= 0x60000000 else 0
            tex = r32(b, po+8)
            shader = r32(b, po+0xC)
            vtype = 0xB5
            first_type = r32(b, po)
            for vt, st in VERT_STRIDE.items():
                if first_type & 0xFF == vt:
                    vtype = vt
                    break
            md = po + 0xA0
            end = md + mesh_size if mesh_size > 0 else po + 0x400
            stride = VERT_STRIDE.get(vtype, 48)
            pos = md
            while pos+0x20 < min(end, len(b)):
                facetype = r32(b, pos+0x10)
                vertcount = r32(b, pos+0x14)
                if not vertcount or vertcount > 0xFFFF: break
                vpos = pos + 0x20
                if vpos + vertcount*stride > min(end, len(b)): break
                base_vcount = len(all_verts)
                for x in range(vertcount):
                    p,n,u = read_vert(b, vpos, vtype)
                    all_verts.append({'pos':p,'nrm':n,'uv':u})
                    vpos += stride
                for f in read_faces(vertcount, facetype):
                    all_tris.append((base_vcount+f[0], base_vcount+f[1], base_vcount+f[2]))
                pos = vpos
    return all_verts, all_tris

# ------------------------------------------------------- rig (skin) PS2
def parse_ps2_skin(b, amg_abs, verts_by_offset):
    """Lee el rig del AMG y asigna hueso+peso a cada vertice por offset.
    Model-Rig_Extractor.py: cada hueso con rig tiene chunks de 32B (vvn)
    y 16B (sb) con el voff del vertice en +12.
    Devuelve {vert_idx: (bone, weight)}."""
    bone_am = r32(b, amg_abs+0x10)
    axes_loc = r32(b, amg_abs+0x14)
    result = {}
    # construir mapa offset -> vert_idx (offsets absolutos dentro del AMG)
    off_map = verts_by_offset
    for bi in range(bone_am):
        e0 = amg_abs + axes_loc + bi*80
        # +8 del hueso = ptr al rig (Model-Rig_Extractor)
        rig_rel = r32(b, e0+8) if False else None
        # el ptr al rig se lee desde el "bone loc": la entrada de 80B tiene
        # +0x34 ptr arm. El rig esta en arm+?  Probamos varios:
        p34 = r32(b, e0+0x34)
        if not p34: continue
        arm = amg_abs + p34
        # arm + 8 = rig_ptr segun Model-Rig_Extractor (rig_start en bone+8)
        # El "bone_loc" real del extractor es first_amg+0x54+i*0x50
        bone_loc = amg_abs + 0x54 + bi*0x50
        rig_rel = r32(b, bone_loc+8)
        if not rig_rel: continue
        rig_start = amg_abs + rig_rel
        chunk_amnt = r32(b, rig_start+0x0C)
        for ci in range(chunk_amnt):
            ch = rig_start + 0x10 + ci*32
            weight = rf(b, ch)
            ch_len = r32(b, ch+4); ch_loc = r32(b, ch+8)
            sb_len = r32(b, ch+0x0C); sb_loc = r32(b, ch+0x10)
            if not ch_loc: continue
            for vi in range(ch_len):
                e = amg_abs + ch_loc + ((vi+1)*32) - 20
                voff = r32(b, e+12)
                if voff in off_map:
                    result[off_map[voff]] = (bi, weight)
    return result

# ------------------------------------------------------- labels
def read_labels_ps2(b, amg_abs):
    labels = {}
    off = r32(b, amg_abs+0x1C)
    if not off: return labels
    for bi in range(64):
        s = b[amg_abs+off+bi*16:amg_abs+off+bi*16+16]
        s = s.split(b'\x00')[0].decode('utf-8', errors='replace')
        if s: labels[bi] = s
    return labels

def read_labels_hd(bin_hd):
    AWO = bin_hd[0x40:]
    off = u32be(AWO, 0x24)
    labels = {}
    for bi in range(51):
        idx = bi*2
        s = AWO[off+idx*16:off+idx*16+16]
        s = s.split(b'\x00')[0].decode('utf-8', errors='replace')
        if s: labels[bi] = s
    return labels

def build_mapping(jnb_labels, kll_labels):
    kll_by_label = {lbl: i for i, lbl in kll_labels.items()}
    mapping = {}
    for jnb_idx, label in jnb_labels.items():
        kll_label = label.replace('XJNB_','XKLL_').replace('JNB_','KLL_')
        mapping[jnb_idx] = kll_by_label.get(kll_label, -1)
    # manual (dedos/caras -> equivalentes Krillin)
    manual = {1:18,3:18,5:18,9:18,11:18,17:18,
              19:25,23:25,25:25,27:25,31:25,35:25,
              37:36,41:36,43:36,45:36}
    for bi, tgt in manual.items():
        if bi not in mapping or mapping[bi] < 0:
            mapping[bi] = tgt
    return mapping

# ------------------------------------------------------- build HD vertex
def inv_rigid(M, p):
    Rt = [[M[j][i] for j in range(3)] for i in range(3)]
    tp = [-sum(Rt[i][j]*p[j] for j in range(3)) for i in range(3)]
    return Rt, tp

def apply_mat(M, p, v):
    return (M[0][0]*v[0]+M[0][1]*v[1]+M[0][2]*v[2]+p[0],
            M[1][0]*v[0]+M[1][1]*v[1]+M[1][2]*v[2]+p[1],
            M[2][0]*v[0]+M[2][1]*v[1]+M[2][2]*v[2]+p[2])

def build_vertex_hd(pos_local, bone, weight, nrm, uv):
    x,y,z = pos_local
    nx,ny,nz = nrm
    u,v = uv
    return (struct.pack('>f', float('nan')) +
            struct.pack('>f', u) + struct.pack('>f', v) +
            struct.pack('>f', z) + struct.pack('>f', x) + struct.pack('>f', y) +
            struct.pack('>f', weight) + struct.pack('>I', bone & 0xFFFFFFFF) +
            struct.pack('>f', nz) + struct.pack('>f', -ny) + struct.pack('>f', nx))

# ------------------------------------------------------- quat->mat (para world mats Janemba)
def quat_to_mat(x,y,z,w):
    xx,xy,xz,xw = x*x,x*y,x*z,x*w
    yy,yz,yw = y*y,y*z,y*w
    zz,zw = z*z,z*w
    return [[1-2*(yy+zz),2*(xy-zw),2*(xz+yw)],
            [2*(xy+zw),1-2*(xx+zz),2*(yz-xw)],
            [2*(xz-yw),2*(yz+xw),1-2*(xx+yy)]]

def mat_mul(A,B):
    return [[sum(A[i][k]*B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]

def build_world_mats_ps2(b, amo0):
    cnt = r32(b, amo0+0x10)
    start = r32(b, amo0+0x14)
    bone_off = {}; parents = {}
    for bi in range(cnt):
        e = amo0+start+bi*32
        bone_table = r32(b, e+4)
        t3 = r32(b, e+0x10)
        pid = r32(b, amo0+t3)+1 if t3 else 0
        bo = r32(b, amo0+bone_table+8) if bone_table else 0
        bone_off[bi] = amo0+bo if bo else 0
        parents[bi] = pid
    cache = {}
    def get_mat(i):
        if i in cache: return cache[i]
        bb = bone_off[i]
        if not bb:
            m = [[1,0,0],[0,1,0],[0,0,1]]; p = [0,0,0]
        else:
            c = [rf(b, bb+j*4) for j in range(12)]
            m = quat_to_mat(c[0],c[1],c[2],-c[3]); p = [c[4],c[5],c[6]]
        pid = parents[i]
        if pid and pid <= cnt:
            pm, pp = get_mat(pid-1)
            m = mat_mul(pm, m)
            p = [pm[0][0]*p[0]+pm[0][1]*p[1]+pm[0][2]*p[2]+pp[0],
                 pm[1][0]*p[0]+pm[1][1]*p[1]+pm[1][2]*p[2]+pp[1],
                 pm[2][0]*p[0]+pm[2][1]*p[1]+pm[2][2]*p[2]+pp[2]]
        cache[i] = (m,p)
        return m,p
    return {bi: get_mat(bi) for bi in range(cnt)}

# ------------------------------------------------------- main
def main():
    if len(sys.argv) < 4:
        print('Uso: build_awo_desde_cero.py <janemba.amb> <krillin_hd.bin> <out_amb>')
        return
    amb = open(sys.argv[1], 'rb').read()
    krillin = open(sys.argv[2], 'rb').read()
    out = sys.argv[3]

    # AMO0 de Janemba (LE, en offset 64)
    amo0 = 64
    if amb[amo0:amo0+4] not in (b'#AMO0', b'#AMO'):
        for i in range(0, len(amb)-4):
            if amb[i:i+4] == b'#AMO0':
                amo0 = i
                break
    print('Janemba AMO0 @0x%X' % amo0)
    n_amg = r32(amb, amo0+0x18)
    amg_table = r32(amb, amo0+0x1C)
    # la tabla puede ser un puntero (valor abs) o una lista directa
    if amg_table < 0x100:
        amg_table = amo0 + 0x30
    else:
        amg_table = amo0 + amg_table
    amg_offs = [r32(amb, amg_table+i*4) for i in range(n_amg)]
    print('AMGs: %d' % n_amg)
    print('offsets:', [hex(o) for o in amg_offs[:5]], '...')

    # Labels KLL
    kll_labels = read_labels_hd(krillin)
    print('Krillin labels:', len(kll_labels))

    # World mats de Janemba
    mats_jnb = build_world_mats_ps2(amb, amo0)
    # World mats de Krillin (del bin PS2 de Krillin para el local KLL)
    # OJO: necesitamos b327_ps2.bin. Si no existe, usar identidad (aprox).

    # Para cada AMG: parsear, aplicar skin, transformar, construir verts HD
    all_awg_data = []
    awg_meta = []
    labels_off_ps2 = None
    for i, amg_off in enumerate(amg_offs):
        amg_abs = amo0 + amg_off
        if amb[amg_abs:amg_abs+4] != b'#AMG':
            print('AMG%d: no es #AMG en 0x%X' % (i, amg_abs)); continue
        verts, tris = parse_ps2_amg(amb, amg_abs)
        # labels del AMG
        lbl = read_labels_ps2(amb, amg_abs)
        name = lbl.get(0, 'AMG%d' % i)
        print('AMG%d @0x%X: %d verts %d tris label=%r' % (i, amg_abs, len(verts), len(tris), name))
        # (el skin detallado por hueso se aplicaria aqui; por ahora bone=0)
        # transformar a world y luego a local KLL (identidad si no hay mats)
        hd_verts = []
        for v in verts:
            # local JNB -> world (bone 0 o el del skin)
            hd_verts.append(build_vertex_hd(v['pos'], 0, 1.0, v['nrm'], v['uv']))
        # IB
        ib = b''
        for t in tris:
            ib += struct.pack('>HHH', *t)
        sec34 = b''.join(hd_verts)
        awg_meta.append((name, len(verts), len(tris), sec34, ib))

    # Construir AWO: header + tabla + AWGs
    # (implementacion de ensamblado en la siguiente fase)

    # Guardar intermedios
    import os
    d = os.path.dirname(out) or '.'
    os.makedirs(d, exist_ok=True)
    with open(out, 'wb') as f:
        pass
    print('\nIntermedios listos. AWG metas:')
    for name, nv, nt, s, ib in awg_meta:
        print('  %-24s %4d verts %4d tris sec34=%d IB=%d' % (name, nv, nt, len(s), len(ib)//2))

if __name__ == '__main__':
    main()
