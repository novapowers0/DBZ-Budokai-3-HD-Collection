"""
OBJ to AWG v1 — reimportar un OBJ editado al bin HD B3.

El inverso de awg_to_obj.py. Toma un OBJ editado en Blender (la geometria
del personaje nuevo superpuesta sobre Krillin) y reescribe las posiciones
del sec34 del bin HD, manteniendo la estructura (conteos fijos, IB, arms).

IMPORTANTE: el OBJ debe tener el MISMO numero de vertices que el sec34
(1956 para Krillin). El usuario edita las posiciones world de los verts,
no agrega/elimina.

El pipeline de retopologia:
  1. awg_to_obj.py -> exporta Krillin a OBJ (world)
  2. Blender: importa Krillin.obj + Android18.fbx, alinea y re-riggea
     (pone la forma de Android 18 sobre el esqueleto de Krillin)
  3. obj_to_awg.py -> reimporta el OBJ editado al bin HD

Uso:
  python obj_to_awg.py <bin_amb_hd> <b327_ps2.bin> <modelo.obj> <salida_amb>
"""

import io, sys, struct
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
sys.path.insert(0, r'C:\Users\javie\Desktop\PROYECTOS IA\DBZ Budokai 3 HD Collection\awo_tools')
from pose_matrix import build_world_mats

F32 = struct.Struct('>f')
U32 = struct.Struct('>I')
def f32(v): return F32.pack(v)
def u32r(b, o): return U32.unpack_from(b, o)[0]
def f32r(b, o): return F32.unpack_from(b, o)[0]


def inv_rigid(M, p):
    Rt = [[M[j][i] for j in range(3)] for i in range(3)]
    tp = [-sum(Rt[i][j]*p[j] for j in range(3)) for i in range(3)]
    return Rt, tp


def apply_mat(M, p, v):
    return (M[0][0]*v[0]+M[0][1]*v[1]+M[0][2]*v[2]+p[0],
            M[1][0]*v[0]+M[1][1]*v[1]+M[1][2]*v[2]+p[1],
            M[2][0]*v[0]+M[2][1]*v[1]+M[2][2]*v[2]+p[2])


def main():
    if len(sys.argv) < 5:
        print('Uso: obj_to_awg.py <bin_amb_hd> <b327_ps2.bin> <modelo.obj> <salida>')
        return
    hd = open(sys.argv[1], 'rb').read()
    ps2 = open(sys.argv[2], 'rb').read()
    obj = open(sys.argv[3]).read()
    out = sys.argv[4]

    mats_kll, _ = build_world_mats(ps2, 0x40)

    # --- Leer verts del OBJ ---
    verts = []
    for line in obj.split('\n'):
        if line.startswith('v '):
            x, y, z = map(float, line.split()[1:4])
            verts.append((x, y, z))
    print('OBJ verts: %d' % len(verts))

    # Estructura del bin HD
    awo = bytearray(hd[0x40:])
    AWG = u32r(awo, u32r(awo, 0x1C))
    sc = u32r(awo, AWG+0x34); vb = u32r(awo, AWG+0x2C)
    n_sec = (vb-(sc+2))//44
    print('sec34 slots: %d' % n_sec)

    if len(verts) != n_sec:
        print('ERROR: el OBJ debe tener %d verts (el sec34), tiene %d' % (n_sec, len(verts)))
        print('El pipeline: exportar Krillin.obj (n verts), editar posiciones en Blender,')
        print('reimportar sin cambiar el numero de verts.')
        return

    # Reescibir posiciones locales: local = inv(mat_world[bone]) * world_OBJ
    changed = 0
    for i in range(n_sec):
        off = AWG+sc+2+i*44
        bone = u32r(awo, off+28)
        M3, p3 = mats_kll.get(bone, ([[1,0,0],[0,1,0],[0,0,1]], (0,0,0)))
        iM, ip = inv_rigid(M3, p3)
        local = apply_mat(iM, ip, verts[i])
        # layout: +12 z, +16 x, +20 y
        struct.pack_into('>f', awo, off+12, local[2])
        struct.pack_into('>f', awo, off+16, local[0])
        struct.pack_into('>f', awo, off+20, local[1])
        changed += 1
    print('Slots reescritos: %d' % changed)

    amb = bytearray(hd[:0x40])
    amb += awo
    with open(out, 'wb') as f:
        f.write(bytes(amb))
    print('Guardado: %s (%d bytes)' % (out, len(amb)))


if __name__ == '__main__':
    main()
